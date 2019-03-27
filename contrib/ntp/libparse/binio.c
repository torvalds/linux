/*
 * /src/NTP/ntp4-dev/libntp/binio.c,v 4.5 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * binio.c,v 4.5 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Jul 20 12:55:33 1997 $
 *
 * Copyright (c) 1997-2005 by Frank Kardel <kardel <AT> ntp.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <config.h>
#include "binio.h"

long
get_lsb_short(
	unsigned char **bufpp
	)
{
  long retval;

  retval  = *((*bufpp)++);
  retval |= *((*bufpp)++) << 8;

  return (retval & 0x8000) ? (~0xFFFF | retval) : retval;
}

void
put_lsb_short(
	unsigned char **bufpp,
	long val
	)
{
  *((*bufpp)++) = (unsigned char) (val        & 0xFF);
  *((*bufpp)++) = (unsigned char) ((val >> 8) & 0xFF);
}

long
get_lsb_long(
	unsigned char **bufpp
	)
{
  long retval;

  retval  = *((*bufpp)++);
  retval |= *((*bufpp)++) << 8;
  retval |= *((*bufpp)++) << 16;
  retval |= (u_long)*((*bufpp)++) << 24;

  return retval;
}

void
put_lsb_long(
	unsigned char **bufpp,
	long val
	)
{
  *((*bufpp)++) = (unsigned char)(val         & 0xFF);
  *((*bufpp)++) = (unsigned char)((val >> 8)  & 0xFF);
  *((*bufpp)++) = (unsigned char)((val >> 16) & 0xFF);
  *((*bufpp)++) = (unsigned char)((val >> 24) & 0xFF);
}

long
get_msb_short(
	unsigned char **bufpp
	)
{
  long retval;

  retval  = *((*bufpp)++) << 8;
  retval |= *((*bufpp)++);

  return (retval & 0x8000) ? (~0xFFFF | retval) : retval;
}

void
put_msb_short(
	unsigned char **bufpp,
	long val
	)
{
  *((*bufpp)++) = (unsigned char)((val >> 8) & 0xFF);
  *((*bufpp)++) = (unsigned char)( val       & 0xFF);
}

long
get_msb_long(
	unsigned char **bufpp
	)
{
  long retval;

  retval  = (u_long)*((*bufpp)++) << 24;
  retval |= *((*bufpp)++) << 16;
  retval |= *((*bufpp)++) << 8;
  retval |= *((*bufpp)++);

  return retval;
}

void
put_msb_long(
	unsigned char **bufpp,
	long val
	)
{
  *((*bufpp)++) = (unsigned char)((val >> 24) & 0xFF);
  *((*bufpp)++) = (unsigned char)((val >> 16) & 0xFF);
  *((*bufpp)++) = (unsigned char)((val >> 8 ) & 0xFF);
  *((*bufpp)++) = (unsigned char)( val        & 0xFF);
}

/*
 * binio.c,v
 * Revision 4.2  1999/02/21 12:17:34  kardel
 * 4.91f reconcilation
 *
 * Revision 4.1  1998/06/28 16:47:50  kardel
 * added {get,put}_msb_{short,long} functions
 *
 * Revision 4.0  1998/04/10 19:46:16  kardel
 * Start 4.0 release version numbering
 *
 * Revision 1.1  1998/04/10 19:27:46  kardel
 * initial NTP VERSION 4 integration of PARSE with GPS166 binary support
 *
 * Revision 1.1  1997/10/06 21:05:46  kardel
 * new parse structure
 *
 */
