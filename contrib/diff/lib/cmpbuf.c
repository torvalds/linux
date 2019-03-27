/* Buffer primitives for comparison operations.

   Copyright (C) 1993, 1995, 1998, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <limits.h>

#include <signal.h>
#ifndef SA_RESTART
# ifdef SA_INTERRUPT /* e.g. SunOS 4.1.x */
#  define SA_RESTART SA_INTERRUPT
# else
#  define SA_RESTART 0
# endif
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#include <sys/types.h>
#include "cmpbuf.h"

/* Determine whether an integer type is signed, and its bounds.
   This code assumes two's (or one's!) complement with no holes.  */

/* The extra casts work around common compiler bugs,
   e.g. Cray C 5.0.3.0 when t == time_t.  */
#ifndef TYPE_SIGNED
# define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
#endif
#ifndef TYPE_MINIMUM
# define TYPE_MINIMUM(t) ((t) (TYPE_SIGNED (t) \
			       ? ~ (t) 0 << (sizeof (t) * CHAR_BIT - 1) \
			       : (t) 0))
#endif
#ifndef TYPE_MAXIMUM
# define TYPE_MAXIMUM(t) ((t) (~ (t) 0 - TYPE_MINIMUM (t)))
#endif

#ifndef PTRDIFF_MAX
# define PTRDIFF_MAX TYPE_MAXIMUM (ptrdiff_t)
#endif
#ifndef SIZE_MAX
# define SIZE_MAX TYPE_MAXIMUM (size_t)
#endif
#ifndef SSIZE_MAX
# define SSIZE_MAX TYPE_MAXIMUM (ssize_t)
#endif

#undef MIN
#define MIN(a, b) ((a) <= (b) ? (a) : (b))

/* Read NBYTES bytes from descriptor FD into BUF.
   NBYTES must not be SIZE_MAX.
   Return the number of characters successfully read.
   On error, return SIZE_MAX, setting errno.
   The number returned is always NBYTES unless end-of-file or error.  */

size_t
block_read (int fd, char *buf, size_t nbytes)
{
  char *bp = buf;
  char const *buflim = buf + nbytes;
  size_t readlim = SSIZE_MAX;

  do
    {
      size_t bytes_to_read = MIN (buflim - bp, readlim);
      ssize_t nread = read (fd, bp, bytes_to_read);
      if (nread <= 0)
	{
	  if (nread == 0)
	    break;

	  /* Accommodate Tru64 5.1, which can't read more than INT_MAX
	     bytes at a time.  They call that a 64-bit OS?  */
	  if (errno == EINVAL && INT_MAX < bytes_to_read)
	    {
	      readlim = INT_MAX;
	      continue;
	    }

	  /* This is needed for programs that have signal handlers on
	     older hosts without SA_RESTART.  It also accommodates
	     ancient AIX hosts that set errno to EINTR after uncaught
	     SIGCONT.  See <news:1r77ojINN85n@ftp.UU.NET>
	     (1993-04-22).  */
	  if (! SA_RESTART && errno == EINTR)
	    continue;

	  return SIZE_MAX;
	}
      bp += nread;
    }
  while (bp < buflim);

  return bp - buf;
}

/* Least common multiple of two buffer sizes A and B.  However, if
   either A or B is zero, or if the multiple is greater than LCM_MAX,
   return a reasonable buffer size.  */

size_t
buffer_lcm (size_t a, size_t b, size_t lcm_max)
{
  size_t lcm, m, n, q, r;

  /* Yield reasonable values if buffer sizes are zero.  */
  if (!a)
    return b ? b : 8 * 1024;
  if (!b)
    return a;

  /* n = gcd (a, b) */
  for (m = a, n = b;  (r = m % n) != 0;  m = n, n = r)
    continue;

  /* Yield a if there is an overflow.  */
  q = a / n;
  lcm = q * b;
  return lcm <= lcm_max && lcm / b == q ? lcm : a;
}
