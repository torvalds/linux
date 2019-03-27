/*
 * Copyright (c) 1997, 1998 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_net_write (krb5_context context,
		void *p_fd,
		const void *buf,
		size_t len)
{
    krb5_socket_t fd = *((krb5_socket_t *)p_fd);
    return net_write(fd, buf, len);
}

KRB5_LIB_FUNCTION krb5_ssize_t KRB5_LIB_CALL
krb5_net_write_block(krb5_context context,
		     void *p_fd,
		     const void *buf,
		     size_t len,
		     time_t timeout)
{
  krb5_socket_t fd = *((krb5_socket_t *)p_fd);
  int ret;
  struct timeval tv, *tvp;
  const char *cbuf = (const char *)buf;
  size_t rem = len;
  ssize_t count;
  fd_set wfds;

  do {
      FD_ZERO(&wfds);
      FD_SET(fd, &wfds);

      if (timeout != 0) {
	  tv.tv_sec = timeout;
	  tv.tv_usec = 0;
	  tvp = &tv;
      } else
	  tvp = NULL;

      ret = select(fd + 1, NULL, &wfds, NULL, tvp);
      if (rk_IS_SOCKET_ERROR(ret)) {
	  if (rk_SOCK_ERRNO == EINTR)
	      continue;
	  return -1;
      }

#ifdef HAVE_WINSOCK
      if (ret == 0) {
	  WSASetLastError( WSAETIMEDOUT );
	  return 0;
      }

      count = send (fd, cbuf, rem, 0);

      if (rk_IS_SOCKET_ERROR(count)) {
	  return -1;
      }

#else
      if (ret == 0) {
	  return 0;
      }

      if (!FD_ISSET(fd, &wfds)) {
	  errno = ETIMEDOUT;
	  return -1;
      }

      count = write (fd, cbuf, rem);

      if (count < 0) {
	  if (errno == EINTR)
	      continue;
	  else
	      return count;
      }

#endif

      cbuf += count;
      rem -= count;

  } while (rem > 0);

  return len;
}
