/*
 * /src/NTP/ntp4-dev/kernel/sys/parsestreams.h,v 4.5 2005/06/25 10:52:47 kardel RELEASE_20050625_A
 *
 * parsestreams.h,v 4.5 2005/06/25 10:52:47 kardel RELEASE_20050625_A
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
 */

#if	!(defined(lint) || defined(__GNUC__))
  static char sysparsehrcsid[] = "parsestreams.h,v 4.5 2005/06/25 10:52:47 kardel RELEASE_20050625_A";
#endif

#undef PARSEKERNEL
#if defined(KERNEL) || defined(_KERNEL)
#ifndef PARSESTREAM
#define PARSESTREAM
#endif
#endif
#if defined(PARSESTREAM) && defined(HAVE_SYS_STREAM_H)
#define PARSEKERNEL

#ifdef HAVE_SYS_TERMIOS_H
#include <sys/termios.h>
#endif

#include <sys/ppsclock.h>

#ifndef NTP_NEED_BOPS
#define NTP_NEED_BOPS
#endif

#if defined(PARSESTREAM) && (defined(_sun) || defined(__sun)) && defined(HAVE_SYS_STREAM_H)
/*
 * Sorry, but in SunOS 4.x AND Solaris 2.x kernels there are no
 * mem* operations. I don't want them - bcopy, bzero
 * are fine in the kernel
 */
#undef HAVE_STRING_H	/* don't include that at kernel level - prototype mismatch in Solaris 2.6 */
#include "ntp_string.h"
#else
#include <stdio.h>
#endif

struct parsestream		/* parse module local data */
{
  queue_t       *parse_queue;	/* read stream for this channel */
  queue_t	*parse_dqueue;	/* driver queue entry (PPS support) */
  unsigned long  parse_status;  /* operation flags */
  void          *parse_data;	/* local data space (PPS support) */
  parse_t	 parse_io;	/* io structure */
  struct ppsclockev parse_ppsclockev; /* copy of last pps event */
};

typedef struct parsestream parsestream_t;

#define PARSE_ENABLE	0x0001

/*--------------- debugging support ---------------------------------*/

#define DD_OPEN    0x00000001
#define DD_CLOSE   0x00000002
#define DD_RPUT    0x00000004
#define DD_WPUT    0x00000008
#define DD_RSVC    0x00000010
#define DD_PARSE   0x00000020
#define DD_INSTALL 0x00000040
#define DD_ISR     0x00000080
#define DD_RAWDCF  0x00000100

extern int parsedebug;

#ifdef DEBUG_PARSE

#define parseprintf(X, Y) if ((X) & parsedebug) printf Y

#else

#define parseprintf(X, Y)

#endif
#endif

/*
 * History:
 *
 * parsestreams.h,v
 * Revision 4.5  2005/06/25 10:52:47  kardel
 * fix version id / add version log
 *
 * Revision 4.4  1998/06/14 21:09:32  kardel
 * Sun acc cleanup
 *
 * Revision 4.3  1998/06/13 18:14:32  kardel
 * make mem*() to b*() mapping magic work on Solaris too
 *
 * Revision 4.2  1998/06/13 15:16:22  kardel
 * fix mem*() to b*() function macro emulation
 *
 * Revision 4.1  1998/06/13 11:50:37  kardel
 * STREAM macro gone in favor of HAVE_SYS_STREAM_H
 *
 * Revision 4.0  1998/04/10 19:51:30  kardel
 * Start 4.0 release version numbering
 *
 * Revision 1.2  1998/04/10 19:27:42  kardel
 * initial NTP VERSION 4 integration of PARSE with GPS166 binary support
 *
 */
