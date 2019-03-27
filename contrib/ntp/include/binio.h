/*
 * /src/NTP/ntp4-dev/include/binio.h,v 4.5 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * binio.h,v 4.5 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Jul 20 13:03:05 1997 $
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
#ifndef BINIO_H
#define BINIO_H

#include "ntp_stdlib.h"

long get_lsb_short (unsigned char **);
void put_lsb_short (unsigned char **, long);
long get_lsb_long (unsigned char **);
void put_lsb_long (unsigned char **, long);

#define get_lsb_int16( _x_ )   ((int16_t) get_lsb_short( _x_ ))
#define get_lsb_uint16( _x_ )  ((uint16_t) get_lsb_short( _x_ ))
#define get_lsb_int32( _x_ )   ((int32_t) get_lsb_long( _x_ ))
#define get_lsb_uint32( _x_ )  ((uint32_t) get_lsb_long( _x_ ))

long get_msb_short (unsigned char **);
void put_msb_short (unsigned char **, long);
long get_msb_long (unsigned char **);
void put_msb_long (unsigned char **, long);

#define get_msb_int16( _x_ )   ((int16_t) get_msb_short( _x_ ))
#define get_msb_uint16( _x_ )  ((uint16_t) get_msb_short( _x_ ))
#define get_msb_int32( _x_ )   ((int32_t) get_msb_long( _x_ ))
#define get_msb_uint32( _x_ )  ((uint32_t) get_msb_long( _x_ ))

#endif
/*
 * History:
 *
 * binio.h,v
 * Revision 4.5  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.4  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.2  1998/06/28 16:52:15  kardel
 * added binio MSB prototypes for {get,put}_msb_{short,long}
 *
 * Revision 4.1  1998/06/12 15:07:40  kardel
 * fixed prototyping
 *
 * Revision 4.0  1998/04/10 19:50:38  kardel
 * Start 4.0 release version numbering
 *
 * Revision 1.1  1998/04/10 19:27:32  kardel
 * initial NTP VERSION 4 integration of PARSE with GPS166 binary support
 *
 * Revision 1.1  1997/10/06 20:55:37  kardel
 * new parse structure
 *
 */
