/*
 * /src/NTP/ntp4-dev/libparse/clk_rcc8000.c,v 4.9 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *
 * clk_rcc8000.c,v 4.9 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *
 * Radiocode Clocks Ltd RCC 8000 Intelligent Off-Air Master Clock support
 *
 * Created by R.E.Broughton from clk_trimtaip.c
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_RCC8000)

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

/* Type II Serial Output format
 *
 *	0000000000111111111122222222223	/ char
 *	0123456789012345678901234567890	\ posn
 *	HH:MM:SS.XYZ DD/MM/YY DDD W Prn   Actual
 *      33 44 55 666 00 11 22       7     Parse
 *        :  :  .      /  /          rn   Check
 *     "15:50:36.534 30/09/94 273 5 A\x0d\x0a"
 *
 * DDD - Day of year number
 *   W - Day of week number (Sunday is 0)
 * P is the Status. See comment below for details.
 */

#define	O_USEC		O_WDAY
static struct format rcc8000_fmt =
{ { { 13, 2 }, {16, 2}, { 19, 2}, /* Day, Month, Year */
    {  0, 2 }, { 3, 2}, {  6, 2}, /* Hour, Minute, Second */
    {  9, 3 }, {28, 1}, {  0, 0}, /* uSec, Status (Valid,Reject,BST,Leapyear) */  },
  (const unsigned char *)"  :  :  .      /  /          \r\n",
  /*"15:50:36.534 30/09/94 273 5 A\x0d\x0a" */
  0
};

static parse_cvt_fnc_t cvt_rcc8000;
static parse_inp_fnc_t inp_rcc8000;

clockformat_t clock_rcc8000 =
{
  inp_rcc8000,			/* no input handling */
  cvt_rcc8000,			/* Radiocode clock conversion */
  0,				/* no direct PPS monitoring */
  (void *)&rcc8000_fmt,		/* conversion configuration */
  "Radiocode RCC8000",
  31,				/* string buffer */
  0				/* no private data */
};

/* parse_cvt_fnc_t cvt_rcc8000 */
static u_long
cvt_rcc8000(
	    unsigned char *buffer,
	    int            size,
	    struct format *format,
	    clocktime_t   *clock_time,
	    void          *local
	    )
{
	if (!Strok(buffer, format->fixed_string)) return CVT_NONE;
#define	OFFS(x) format->field_offsets[(x)].offset
#define STOI(x, y) Stoi(&buffer[OFFS(x)], y, format->field_offsets[(x)].length)
	if (	STOI(O_DAY,	&clock_time->day)	||
		STOI(O_MONTH,	&clock_time->month)	||
		STOI(O_YEAR,	&clock_time->year)	||
		STOI(O_HOUR,	&clock_time->hour)	||
		STOI(O_MIN,	&clock_time->minute)	||
		STOI(O_SEC,	&clock_time->second)	||
		STOI(O_USEC,	&clock_time->usecond)
		) return CVT_FAIL|CVT_BADFMT;
	clock_time->usecond *= 1000;

	clock_time->utcoffset = 0;

#define RCCP buffer[28]
	/*
	 * buffer[28] is the ASCII representation of a hex character ( 0 through F )
	 *      The four bits correspond to:
	 *      8 - Valid Time
	 *      4 - Reject Code
	 *      2 - British Summer Time (receiver set to emit GMT all year.)
	 *      1 - Leap year
	 */
#define RCC8000_VALID  0x8
#define RCC8000_REJECT 0x4
#define RCC8000_BST    0x2
#define RCC8000_LEAPY  0x1

	clock_time->flags = 0;

	if ( (RCCP >= '0' && RCCP <= '9') || (RCCP >= 'A' && RCCP <= 'F') )
	{
		register int flag;

		flag = (RCCP >= '0' && RCCP <= '9' ) ?  RCCP - '0' : RCCP - 'A' + 10;

		if (!(flag & RCC8000_VALID))
		    clock_time->flags |= PARSEB_POWERUP;

		clock_time->flags |= PARSEB_UTC; /* British special - guess why 8-) */

		/* other flags not used */
	}
	return CVT_OK;
}
/*
 * parse_inp_fnc_t inp_rcc8000
 *
 * grab data from input stream
 */
static u_long
inp_rcc8000(
	    parse_t      *parseio,
	    char         ch,
	    timestamp_t  *tstamp
	  )
{
	unsigned int rtc;

	parseprintf(DD_PARSE, ("inp_rcc8000(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	switch (ch)
	{
	case '\n':
		parseprintf(DD_PARSE, ("inp_rcc8000: EOL seen\n"));
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;


	default:
		if (parseio->parse_index == 0) /* take sample at start of message */
		{
			parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */
		}
		return parse_addchar(parseio, ch);
	}
}

#else  /* not (REFCLOCK && CLOCK_PARSE && CLOCK_RCC8000) */
int clk_rcc8000_bs;
#endif  /* not (REFCLOCK && CLOCK_PARSE && CLOCK_RCC8000) */

/*
 * History:
 *
 * clk_rcc8000.c,v
 * Revision 4.9  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.6  1999/11/28 09:13:51  kardel
 * RECON_4_0_98F
 *
 * Revision 4.5  1998/06/14 21:09:38  kardel
 * Sun acc cleanup
 *
 * Revision 4.4  1998/06/13 12:05:02  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.3  1998/06/12 15:22:29  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/06/12 09:13:25  kardel
 * conditional compile macros fixed
 * printf prototype
 *
 * Revision 4.1  1998/05/24 09:39:53  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:30  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.5 log info deleted 1998/04/11 kardel
 */
