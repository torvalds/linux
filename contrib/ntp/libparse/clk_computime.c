#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_COMPUTIME)
/*
 * /src/NTP/ntp4-dev/libparse/clk_computime.c,v 4.10 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * clk_computime.c,v 4.10 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * Supports Diem's Computime Radio Clock
 *
 * Used the Meinberg clock as a template for Diem's Computime Radio Clock
 *
 * adapted by Alois Camenzind <alois.camenzind@ubs.ch>
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

#include "ntp_fp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

#include "parse.h"

#ifndef PARSESTREAM
#include <stdio.h>
#else
#include "sys/parsestreams.h"
extern int printf (const char *, ...);
#endif

/*
 * The Computime receiver sends a datagram in the following format every minute
 *
 * Timestamp	T:YY:MM:MD:WD:HH:MM:SSCRLF
 * Pos          0123456789012345678901 2 3
 *              0000000000111111111122 2 2
 * Parse        T:  :  :  :  :  :  :  rn
 *
 * T	Startcharacter "T" specifies start of the timestamp
 * YY	Year MM	Month 1-12
 * MD	Day of the month
 * WD	Day of week
 * HH	Hour
 * MM   Minute
 * SS   Second
 * CR   Carriage return
 * LF   Linefeed
 *
 */

static struct format computime_fmt =
{
	{
		{8, 2},  {5,  2}, {2,  2},	/* day, month, year */
		{14, 2}, {17, 2}, {20, 2},	/* hour, minute, second */
		{11, 2},                        /* dayofweek,  */
	},
	(const unsigned char *)"T:  :  :  :  :  :  :  \r\n",
	0
};

static parse_cvt_fnc_t cvt_computime;
static parse_inp_fnc_t inp_computime;

clockformat_t clock_computime =
{
	inp_computime,		/* Computime input handling */
	cvt_computime,		/* Computime conversion */
	0,			/* no PPS monitoring */
	(void *)&computime_fmt,	/* conversion configuration */
	"Diem's Computime Radio Clock",	/* Computime Radio Clock */
	24,			/* string buffer */
	0			/* no private data (complete packets) */
};

/*
 * parse_cvt_fnc_t cvt_computime
 *
 * convert simple type format
 */
static u_long
cvt_computime(
	unsigned char *buffer,
	int            size,
	struct format *format,
	clocktime_t   *clock_time,
	void          *local
	)
{

	if (!Strok(buffer, format->fixed_string)) {
		return CVT_NONE;
	} else {
		if (Stoi(&buffer[format->field_offsets[O_DAY].offset], &clock_time->day,
			 format->field_offsets[O_DAY].length) ||
		    Stoi(&buffer[format->field_offsets[O_MONTH].offset], &clock_time->month,
			 format->field_offsets[O_MONTH].length) ||
		    Stoi(&buffer[format->field_offsets[O_YEAR].offset], &clock_time->year,
			 format->field_offsets[O_YEAR].length) ||
		    Stoi(&buffer[format->field_offsets[O_HOUR].offset], &clock_time->hour,
			 format->field_offsets[O_HOUR].length) ||
		    Stoi(&buffer[format->field_offsets[O_MIN].offset], &clock_time->minute,
			 format->field_offsets[O_MIN].length) ||
		    Stoi(&buffer[format->field_offsets[O_SEC].offset], &clock_time->second,
			 format->field_offsets[O_SEC].length)) {
			return CVT_FAIL | CVT_BADFMT;
		} else {

			clock_time->flags = 0;
			clock_time->utcoffset = 0;	/* We have UTC time */

			return CVT_OK;
		}
	}
}

/*
 * parse_inp_fnc_t inp_computime
 *
 * grab data from input stream
 */
static u_long
inp_computime(
	      parse_t      *parseio,
	      char         ch,
	      timestamp_t  *tstamp
	      )
{
	unsigned int rtc;

	parseprintf(DD_PARSE, ("inp_computime(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	switch (ch)
	{
	case 'T':
		parseprintf(DD_PARSE, ("inp_computime: START seen\n"));

		parseio->parse_index = 1;
		parseio->parse_data[0] = ch;
		parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */
		return PARSE_INP_SKIP;

	case '\n':
		parseprintf(DD_PARSE, ("inp_computime: END seen\n"));
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;

	default:
		return parse_addchar(parseio, ch);
	}
}

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_COMPUTIME) */
int clk_computime_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_COMPUTIME) */

/*
 * clk_computime.c,v
 * Revision 4.10  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.9  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.6  1999/11/28 09:13:49  kardel
 * RECON_4_0_98F
 *
 * Revision 4.5  1998/06/14 21:09:34  kardel
 * Sun acc cleanup
 *
 * Revision 4.4  1998/06/13 12:00:38  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.3  1998/06/12 15:22:26  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/06/12 09:13:24  kardel
 * conditional compile macros fixed
 * printf prototype
 *
 * Revision 4.1  1998/05/24 09:39:51  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:27  kardel
 * Start 4.0 release version numbering
 *
 * from V3 1.8 log info deleted 1998/04/11 kardel
 */
