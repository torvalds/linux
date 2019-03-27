/*
 * /src/NTP/ntp4-dev/libparse/parse_conf.c,v 4.9 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * parse_conf.c,v 4.9 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * Parser configuration module for reference clocks
 *
 * STREAM define switches between two personalities of the module
 * if STREAM is defined this module can be used with dcf77sync.c as
 * a STREAMS kernel module. In this case the time stamps will be
 * a struct timeval.
 * when STREAM is not defined NTP time stamps will be used.
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE)

#include "ntp_fp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"

#include "parse.h"

#ifdef CLOCK_SCHMID
extern clockformat_t clock_schmid;
#endif

#ifdef CLOCK_DCF7000
extern clockformat_t clock_dcf7000;
#endif

#ifdef CLOCK_MEINBERG
extern clockformat_t clock_meinberg[];
#endif

#ifdef CLOCK_RAWDCF
extern clockformat_t clock_rawdcf;
#endif

#ifdef CLOCK_TRIMTAIP
extern clockformat_t clock_trimtaip;
#endif

#ifdef CLOCK_TRIMTSIP
extern clockformat_t clock_trimtsip;
#endif

#ifdef CLOCK_RCC8000
extern clockformat_t clock_rcc8000;
#endif

#ifdef CLOCK_HOPF6021
extern clockformat_t clock_hopf6021;
#endif

#ifdef CLOCK_COMPUTIME
extern clockformat_t clock_computime;
#endif

#ifdef CLOCK_WHARTON_400A
extern clockformat_t clock_wharton_400a;
#endif

#ifdef CLOCK_VARITEXT
extern clockformat_t clock_varitext;
#endif

#ifdef CLOCK_SEL240X
extern clockformat_t clock_sel240x;
#endif

/*
 * format definitions
 */
clockformat_t *clockformats[] =
{
#ifdef CLOCK_MEINBERG
	&clock_meinberg[0],
	&clock_meinberg[1],
	&clock_meinberg[2],
#endif
#ifdef CLOCK_DCF7000
	&clock_dcf7000,
#endif
#ifdef CLOCK_SCHMID
	&clock_schmid,
#endif
#ifdef CLOCK_RAWDCF
	&clock_rawdcf,
#endif
#ifdef CLOCK_TRIMTAIP
	&clock_trimtaip,
#endif
#ifdef CLOCK_TRIMTSIP
	&clock_trimtsip,
#endif
#ifdef CLOCK_RCC8000
	&clock_rcc8000,
#endif
#ifdef CLOCK_HOPF6021
	&clock_hopf6021,
#endif
#ifdef CLOCK_COMPUTIME
	&clock_computime,
#endif
#ifdef CLOCK_WHARTON_400A
	&clock_wharton_400a,
#endif
#ifdef CLOCK_VARITEXT
        &clock_varitext,
#endif
#ifdef CLOCK_SEL240X
        &clock_sel240x,
#endif
	0};

unsigned short nformats = sizeof(clockformats) / sizeof(clockformats[0]) - 1;

#else /* not (REFCLOCK && CLOCK_PARSE) */
int parse_conf_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE) */

/*
 * History:
 *
 * parse_conf.c,v
 * Revision 4.9  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.8  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.5  1999/11/28 09:13:53  kardel
 * RECON_4_0_98F
 *
 * Revision 4.4  1999/02/28 15:27:25  kardel
 * wharton clock integration
 *
 * Revision 4.3  1998/08/16 18:52:15  kardel
 * (clockformats): Trimble TSIP driver now also
 * available for kernel operation
 *
 * Revision 4.2  1998/06/12 09:13:48  kardel
 * conditional compile macros fixed
 *
 * Revision 4.1  1998/05/24 09:40:49  kardel
 * adjustments of log messages
 *
 *
 * from V3 3.24 log info deleted 1998/04/11 kardel
 */
