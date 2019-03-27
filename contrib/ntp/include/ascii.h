/*
 * /src/NTP/ntp4-dev/include/ascii.h,v 4.4 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * ascii.h,v 4.4 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Jul 20 11:42:53 1997 $
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
#ifndef ASCII_H
#define ASCII_H

/*
 * just name the common ASCII control codes
 */
#define NUL	  0
#define SOH	  1
#define STX	  2
#define ETX	  3
#define EOT	  4
#define ENQ	  5
#define ACK	  6
#define BEL	  7
#define BS	  8
#define HT	  9
#define NL	 10
#define VT	 11
#define NP	 12
#define CR	 13
#define SO	 14
#define SI	 15
#define DLE	 16
#define DC1	 17
#define DC2	 18
#define DC3	 19
#define DC4	 20
#define NAK	 21
#define SYN	 22
#define ETB	 23
#define CAN	 24
#define EM	 25
#define SUB	 26
#define ESC	 27
#define FS	 28
#define GS	 29
#define RS	 30
#define US	 31
#define SP	 32
#define DEL	127

#endif
/*
 * History:
 *
 * ascii.h,v
 * Revision 4.4  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.3  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.1  1998/07/11 10:05:22  kardel
 * Release 4.0.73d reconcilation
 *
 * Revision 4.0  1998/04/10 19:50:38  kardel
 * Start 4.0 release version numbering
 *
 * Revision 4.0  1998/04/10 19:50:38  kardel
 * Start 4.0 release version numbering
 *
 */
