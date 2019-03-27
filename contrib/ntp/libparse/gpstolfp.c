/*
 * /src/NTP/ntp4-dev/libntp/gpstolfp.c,v 4.8 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * gpstolfp.c,v 4.8 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * $Created: Sun Jun 28 16:30:38 1998 $
 *
 * Copyright (c) 1998-2005 by Frank Kardel <kardel <AT> ntp.org>
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
#include "ntp_fp.h"
#include "ntp_calendar.h"
#include "parse.h"

void
gpstolfp(
	 u_int weeks,
	 u_int days,
	 unsigned long  seconds,
	 l_fp * lfp
	 )
{
  lfp->l_ui = (uint32_t)(weeks * SECSPERWEEK + days * SECSPERDAY + seconds + GPSORIGIN); /* convert to NTP time */
  lfp->l_uf = 0;
}

/*
 * History:
 *
 * gpstolfp.c,v
 * Revision 4.8  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.7  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.3  1999/02/28 11:42:44  kardel
 * (GPSWRAP): update GPS rollover to 990 weeks
 *
 * Revision 4.2  1998/07/11 10:05:25  kardel
 * Release 4.0.73d reconcilation
 *
 * Revision 4.1  1998/06/28 16:47:15  kardel
 * added gpstolfp() function
 */
