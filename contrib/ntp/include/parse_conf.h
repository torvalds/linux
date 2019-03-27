/*
 * /src/NTP/ntp4-dev/include/parse_conf.h,v 4.7 2005/06/25 10:58:45 kardel RELEASE_20050625_A
 *
 * parse_conf.h,v 4.7 2005/06/25 10:58:45 kardel RELEASE_20050625_A
 *
 * Copyright (c) 1995-2005 by Frank Kardel <kardel <AT> ntp.org>
 * Copyright (c) 1989-1994 by Frank Kardel, Friedrich-Alexander Universitaet Erlangen-Nuernberg, Germany
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

#ifndef __PARSE_CONF_H__
#define __PARSE_CONF_H__
#if	!(defined(lint) || defined(__GNUC__))
  static char prshrcsid[] = "parse_conf.h,v 4.7 2005/06/25 10:58:45 kardel RELEASE_20050625_A";
#endif

/*
 * field location structure
 */
#define O_DAY   0
#define O_MONTH 1
#define O_YEAR  2
#define O_HOUR  3
#define O_MIN   4
#define O_SEC   5
#define O_WDAY  6
#define O_FLAGS 7
#define O_ZONE  8
#define O_UTCHOFFSET 9
#define O_UTCMOFFSET 10
#define O_UTCSOFFSET 11
#define O_COUNT (O_UTCSOFFSET+1)

#define MBG_EXTENDED	0x00000001

/*
 * see below for field offsets
 */

struct format
{
  struct foff
    {
      unsigned short offset;		/* offset into buffer */
      unsigned short length;		/* length of field */
    }         field_offsets[O_COUNT];
  const unsigned char *fixed_string;		/* string with must be chars (blanks = wildcards) */
  u_long      flags;
};
#endif

/*
 * History:
 *
 * parse_conf.h,v
 * Revision 4.7  2005/06/25 10:58:45  kardel
 * add missing log keywords
 *
 */
