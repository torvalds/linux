/*
 * /src/NTP/ntp4-dev/libparse/clk_schmid.c,v 4.9 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * clk_schmid.c,v 4.9 2005/04/16 17:32:10 kardel RELEASE_20050508_A
 *
 * Schmid clock support
 * based on information and testing from Adam W. Feigin et. al (Swisstime iis.ethz.ch)
 *
 * Copyright (c) 1995-2015 by Frank Kardel <kardel <AT> ntp.org>
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

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_SCHMID)

#include "ntp_fp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"

#include "parse.h"

#ifndef PARSESTREAM
#include "ntp_stdlib.h"
#include <stdio.h>
#else
#include "sys/parsestreams.h"
extern int printf (const char *, ...);
#endif

/*
 * Description courtesy of Adam W. Feigin et. al (Swisstime iis.ethz.ch)
 *
 * The command to Schmid's DCF77 clock is a single byte; each bit
 * allows the user to select some part of the time string, as follows (the
 * output for the lsb is sent first).
 *
 * Bit 0:	time in MEZ, 4 bytes *binary, not BCD*; hh.mm.ss.tenths
 * Bit 1:	date 3 bytes *binary, not BCD: dd.mm.yy
 * Bit 2:	week day, 1 byte (unused here)
 * Bit 3:	time zone, 1 byte, 0=MET, 1=MEST. (unused here)
 * Bit 4:	clock status, 1 byte,	0=time invalid,
 *					1=time from crystal backup,
 *					3=time from DCF77
 * Bit 5:	transmitter status, 1 byte,
 *					bit 0: backup antenna
 *					bit 1: time zone change within 1h
 *					bit 3,2: TZ 01=MEST, 10=MET
 *					bit 4: leap second will be
 *						added within one hour
 *					bits 5-7: Zero
 * Bit 6:	time in backup mode, units of 5 minutes (unused here)
 *
 */
#define WS_TIME		0x01
#define WS_SIGNAL	0x02

#define WS_CALLBIT	0x01  /* "call bit" used to signalize irregularities in the control facilities */
#define WS_ANNOUNCE	0x02
#define WS_TZ		0x0c
#define   WS_MET	0x08
#define   WS_MEST	0x04
#define WS_LEAP		0x10

static parse_cvt_fnc_t cvt_schmid;
static parse_inp_fnc_t inp_schmid;

clockformat_t clock_schmid =
{
  inp_schmid,			/* no input handling */
  cvt_schmid,			/* Schmid conversion */
  0,				/* not direct PPS monitoring */
  0,				/* conversion configuration */
  "Schmid",			/* Schmid receiver */
  12,				/* binary data buffer */
  0,				/* no private data (complete messages) */
};

/* parse_cvt_fnc_t */
static u_long
cvt_schmid(
	   unsigned char *buffer,
	   int            size,
	   struct format *format,
	   clocktime_t   *clock_time,
	   void          *local
	)
{
	if ((size != 11) || (buffer[10] != (unsigned char)'\375'))
	{
		return CVT_NONE;
	}
	else
	{
		if (buffer[0] > 23 || buffer[1] > 59 || buffer[2] > 59 || buffer[3] >  9) /* Time */
		{
			return CVT_FAIL|CVT_BADTIME;
		}
		else
		    if (buffer[4] <  1 || buffer[4] > 31 || buffer[5] <  1 || buffer[5] > 12
			||  buffer[6] > 99)
		    {
			    return CVT_FAIL|CVT_BADDATE;
		    }
		    else
		    {
			    clock_time->hour    = buffer[0];
			    clock_time->minute  = buffer[1];
			    clock_time->second  = buffer[2];
			    clock_time->usecond = buffer[3] * 100000;
			    clock_time->day     = buffer[4];
			    clock_time->month   = buffer[5];
			    clock_time->year    = buffer[6];

			    clock_time->flags   = 0;

			    switch (buffer[8] & WS_TZ)
			    {
				case WS_MET:
				    clock_time->utcoffset = -1*60*60;
				    break;

				case WS_MEST:
				    clock_time->utcoffset = -2*60*60;
				    clock_time->flags    |= PARSEB_DST;
				    break;

				default:
				    return CVT_FAIL|CVT_BADFMT;
			    }

			    if (!(buffer[7] & WS_TIME))
			    {
				    clock_time->flags |= PARSEB_POWERUP;
			    }

			    if (!(buffer[7] & WS_SIGNAL))
			    {
				    clock_time->flags |= PARSEB_NOSYNC;
			    }

			    if (buffer[7] & WS_SIGNAL)
			    {
				    if (buffer[8] & WS_CALLBIT)
				    {
					    clock_time->flags |= PARSEB_CALLBIT;
				    }

				    if (buffer[8] & WS_ANNOUNCE)
				    {
					    clock_time->flags |= PARSEB_ANNOUNCE;
				    }

				    if (buffer[8] & WS_LEAP)
				    {
					    clock_time->flags |= PARSEB_LEAPADD; /* default: DCF77 data format deficiency */
				    }
			    }

			    clock_time->flags |= PARSEB_S_LEAP|PARSEB_S_CALLBIT;

			    return CVT_OK;
		    }
	}
}

/*
 * parse_inp_fnc_t inp_schmid
 *
 * grab data from input stream
 */
static u_long
inp_schmid(
	  parse_t      *parseio,
	  char         ch,
	  timestamp_t  *tstamp
	  )
{
	unsigned int rtc;

	parseprintf(DD_PARSE, ("inp_schmid(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	switch ((uint8_t)ch)
	{
	case 0xFD:		/*  */
		parseprintf(DD_PARSE, ("inp_schmid: 0xFD seen\n"));
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;

	default:
		return parse_addchar(parseio, ch);
	}
}

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_SCHMID) */
int clk_schmid_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_SCHMID) */

/*
 * History:
 *
 * clk_schmid.c,v
 * Revision 4.9  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.8  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.5  1999/11/28 09:13:51  kardel
 * RECON_4_0_98F
 *
 * Revision 4.4  1998/06/13 12:06:03  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.3  1998/06/12 15:22:29  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/06/12 09:13:26  kardel
 * conditional compile macros fixed
 * printf prototype
 *
 * Revision 4.1  1998/05/24 09:39:53  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:31  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.22 log info deleted 1998/04/11 kardel
 */
