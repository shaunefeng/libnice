/*
 * This file is part of the Nice GLib ICE library.
 *
 * (C) 2006, 2007 Collabora Ltd.
 *  Contact: Dafydd Harries
 * (C) 2006, 2007 Nokia Corporation. All rights reserved.
 *  Contact: Kai Vehmanen
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Nice GLib ICE library.
 *
 * The Initial Developers of the Original Code are Collabora Ltd and Nokia
 * Corporation. All Rights Reserved.
 *
 * Contributors:
 *   Dafydd Harries, Collabora Ltd.
 *   Rémi Denis-Courmont, Nokia
 *   Kai Vehmanen
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * the GNU Lesser General Public License Version 2.1 (the "LGPL"), in which
 * case the provisions of LGPL are applicable instead of those above. If you
 * wish to allow use of your version of this file only under the terms of the
 * LGPL and not to allow others to use your version of this file under the
 * MPL, indicate your decision by deleting the provisions above and replace
 * them with the notice and other provisions required by the LGPL. If you do
 * not delete the provisions above, a recipient may use your version of this
 * file under either the MPL or the LGPL.
 */

/*
 * Implementation of UDP socket interface using Berkeley sockets. (See
 * http://en.wikipedia.org/wiki/Berkeley_sockets.)
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <arpa/inet.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "udp-bsd.h"

/*** NiceSocket ***/
static int sock_recv_err (int fd)
{
#ifdef MSG_ERRQUEUE
  /* Silently dequeue any error message if any */
  struct msghdr hdr;
  int saved = errno, val;

  memset (&hdr, 0, sizeof (hdr));
  val = recvmsg (fd, &hdr, MSG_ERRQUEUE);
  errno = saved;
  return val == 0;
#else
  return 0;
#endif
}


static gint
socket_recv (
  NiceSocket *sock,
  NiceAddress *from,
  guint len,
  gchar *buf)
{
  gint recvd;
  struct sockaddr_storage sa;
  guint from_len = sizeof (sa);

  recvd = recvfrom (sock->fileno, buf, len, 0, (struct sockaddr *) &sa,
      &from_len);
  if (recvd == -1)
  {
    sock_recv_err (sock->fileno);
    return -1;
  }

  nice_address_set_from_sockaddr (from, (struct sockaddr *)&sa);
  return recvd;
}

static gboolean
socket_send (
  NiceSocket *sock,
  const NiceAddress *to,
  guint len,
  const gchar *buf)
{
  struct sockaddr_storage sa;
  ssize_t sent;

  nice_address_copy_to_sockaddr (to, (struct sockaddr *)&sa);

  do
    sent = sendto (sock->fileno, buf, len, 0, (struct sockaddr *) &sa,
                   sizeof (sa));
  while ((sent == -1) && sock_recv_err (sock->fileno));
  
  return sent == (ssize_t)len;
}

static void
socket_close (NiceSocket *sock)
{
  close (sock->fileno);
}

/*** NiceSocketFactory ***/

static gboolean
socket_factory_init_socket (
  G_GNUC_UNUSED
  NiceSocketFactory *man,
  NiceSocket *sock,
  NiceAddress *addr)
{
  int sockfd = -1;
  struct sockaddr_storage name;
  guint name_len = sizeof (name);

  (void)man;

  if (addr != NULL)
    {
      nice_address_copy_to_sockaddr(addr, (struct sockaddr *)&name);
    }
  else
    {
      memset (&name, 0, sizeof (name));
      name.ss_family = AF_UNSPEC;
    }

#if 0
  if ((name.ss_family == AF_INET6) || (name.ss_family == AF_UNSPEC))
    {
      sockfd = socket (PF_INET6, SOCK_DGRAM, 0);
      if (sockfd != -1)
        {
          int v6 = name.ss_family == AF_INET6;

#if defined (IPV6_V6ONLY)
          if (setsockopt (sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6, sizeof (v6)))
#else
          if (!v6)
#endif
            {
              close (sockfd);
              sockfd = -1;
            }
          else
            {
# ifdef IPV6_RECVERR
              int yes = 1;
              setsockopt (sockfd, SOL_IPV6, IPV6_RECVERR, &yes, sizeof (yes));
# endif
              name.ss_family = AF_INET6;
# ifdef HAVE_SA_LEN
              name.ss_len = sizeof (struct sockaddr_in6);
# endif
            }
        }
    }
#endif
  if ((sockfd == -1)
   && ((name.ss_family == AF_UNSPEC) || (name.ss_family == AF_INET)))
    {
      sockfd = socket (PF_INET, SOCK_DGRAM, 0);
      name.ss_family = AF_INET;
#ifdef HAVE_SA_LEN
      name.ss_len = sizeof (struct sockaddr_in);
#endif
    }

  if (sockfd == -1)
    return FALSE;
#ifdef IP_RECVERR
  else
    {
      int yes = 1;
      setsockopt (sockfd, SOL_IP, IP_RECVERR, &yes, sizeof (yes));
    }
#endif

#ifdef FD_CLOEXEC
  fcntl (sockfd, F_SETFD, fcntl (sockfd, F_GETFD) | FD_CLOEXEC);
#endif
#ifdef O_NONBLOCK
  fcntl (sockfd, F_SETFL, fcntl (sockfd, F_GETFL) | O_NONBLOCK);
#endif

  if(bind (sockfd, (struct sockaddr *) &name, sizeof (name)) != 0)
    {
      close (sockfd);
      return FALSE;
    }

  if (getsockname (sockfd, (struct sockaddr *) &name, &name_len) != 0)
    {
      close (sockfd);
      return FALSE;
    }

  nice_address_set_from_sockaddr (&sock->addr, (struct sockaddr *)&name);

  sock->fileno = sockfd;
  sock->send = socket_send;
  sock->recv = socket_recv;
  sock->close = socket_close;
  return TRUE;
}

static void
socket_factory_close (
  G_GNUC_UNUSED
  NiceSocketFactory *man)
{
  (void)man;
}

NICEAPI_EXPORT void
nice_udp_bsd_socket_factory_init (
  G_GNUC_UNUSED
  NiceSocketFactory *man)
{
  man->init = socket_factory_init_socket;
  man->close = socket_factory_close;
}

