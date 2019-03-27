/*
 * /src/NTP/ntp4-dev/libparse/clk_hopf6021.c,v 4.10 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *
 * clk_hopf6021.c,v 4.10 2004/11/14 15:29:41 kardel RELEASE_20050508_A
 *
 * Radiocode Clocks HOPF Funkuhr 6021 mit serieller Schnittstelle
 * base code version from 24th Nov 1995 - history at end
 *
 * Created by F.Schnekenbuehl <frank@comsys.dofn.de> from clk_rcc8000.c
 * Nortel DASA Network Systems GmbH, Department: ND250
 * A Joint venture of Daimler-Benz Aerospace and Nortel
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_HOPF6021)

#include "ntp_fp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"
#include "ascii.h"

#include "parse.h"

#ifndef PARSESTREAM
#include "ntp_stdlib.h"
#include <stdio.h>
#else
#include "sys/parsestreams.h"
extern int printf (const char *, ...);
#endif

/*
 * hopf Funkuhr 6021
 *      used with 9600,8N1,
 *      UTC ueber serielle Schnittstelle
 *      Sekundenvorlauf ON
 *      ETX zum Sekundenvorlauf ON
 *      Datenstring 6021
 *      Ausgabe Uhrzeit und Datum
 *      Senden mit Steuerzeichen
 *      Senden sekuendlich
 */

/*
 *  Type 6021 Serial Output format
 *
 *      000000000011111111 / char
 *      012345678901234567 \ position
 *      sABHHMMSSDDMMYYnre  Actual
 *       C4110046231195     Parse
 *      s              enr  Check
 *
 *  s = STX (0x02), e = ETX (0x03)
 *  n = NL  (0x0A), r = CR  (0x0D)
 *
 *  A B - Status and weekday
 *
 *  A - Status
 *
 *      8 4 2 1
 *      x x x 0  - no announcement
 *      x x x 1  - Summertime - wintertime - summertime announcement
 *      x x 0 x  - Wintertime
 *      x x 1 x  - Summertime
 *      0 0 x x  - Time/Date invalid
 *      0 1 x x  - Internal clock used
 *      1 0 x x  - Radio clock
 *      1 1 x x  - Radio clock highprecision
 *
 *  B - 8 4 2 1
 *      0 x x x  - MESZ/MEZ
 *      1 x x x  - UTC
 *      x 0 0 1  - Monday
 *      x 0 1 0  - Tuesday
 *      x 0 1 1  - Wednesday
 *      x 1 0 0  - Thursday
 *      x 1 0 1  - Friday
 *      x 1 1 0  - Saturday
 *      x 1 1 1  - Sunday
 */

#define HOPF_DSTWARN	0x01	/* DST switch warning */
#define HOPF_DST	0x02	/* DST in effect */

#define HOPF_MODE	0x0C	/* operation mode mask */
#define  HOPF_INVALID	0x00	/* no time code available */
#define  HOPF_INTERNAL	0x04	/* internal clock */
#define  HOPF_RADIO	0x08	/* radio clock */
#define  HOPF_RADIOHP	0x0C	/* high precision radio clock */

#define HOPF_UTC	0x08	/* time code in UTC */
#define HOPF_WMASK	0x07	/* mask for weekday code */

static struct format hopf6021_fmt =
{
	{
		{  9, 2 }, {11, 2}, { 13, 2}, /* Day, Month, Year */
		{  3, 2 }, { 5, 2}, {  7, 2}, /* Hour, Minute, Second */
		{  2, 1 }, { 1, 1}, {  0, 0}, /* Weekday, Flags, Zone */
		/* ... */
	},
	(const unsigned char *)"\002              \n\r\003",
	0
};

#define OFFS(x) format->field_offsets[(x)].offset
#define STOI(x, y) Stoi(&buffer[OFFS(x)], y, format->field_offsets[(x)].length)

static parse_cvt_fnc_t cvt_hopf6021;
static parse_inp_fnc_t inp_hopf6021;
static unsigned char   hexval(unsigned char);

clockformat_t clock_hopf6021 =
{
  inp_hopf6021,			/* HOPF 6021 input handling */
  cvt_hopf6021,                 /* Radiocode clock conversion */
  0,				/* no direct PPS monitoring */
  (void *)&hopf6021_fmt,        /* conversion configuration */
  "hopf Funkuhr 6021",          /* clock format name */
  19,                           /* string buffer */
  0                            /* private data length, no private data */
};

/* parse_cvt_fnc_t cvt_hopf6021 */
static u_long
cvt_hopf6021(
	     unsigned char *buffer,
	     int            size,
	     struct format *format,
	     clocktime_t   *clock_time,
	     void          *local
	     )
{
	unsigned char status,weekday;

	if (!Strok(buffer, format->fixed_string))
	{
		return CVT_NONE;
	}

	if (  STOI(O_DAY,   &clock_time->day)    ||
	      STOI(O_MONTH, &clock_time->month)  ||
	      STOI(O_YEAR,  &clock_time->year)   ||
	      STOI(O_HOUR,  &clock_time->hour)   ||
	      STOI(O_MIN,   &clock_time->minute) ||
	      STOI(O_SEC,   &clock_time->second)
	      )
	{
		return CVT_FAIL|CVT_BADFMT;
	}

	clock_time->usecond = 0;
	clock_time->flags   = 0;

	status  = hexval(buffer[OFFS(O_FLAGS)]);
	weekday = hexval(buffer[OFFS(O_WDAY)]);

	if ((status == 0xFF) || (weekday == 0xFF))
	{
		return CVT_FAIL|CVT_BADFMT;
	}

	if (weekday & HOPF_UTC)
	{
		clock_time->flags     |= PARSEB_UTC;
		clock_time->utcoffset  = 0;
	}
	else if (status & HOPF_DST)
	{
		clock_time->flags     |= PARSEB_DST;
		clock_time->utcoffset  = -2*60*60; /* MET DST */
	}
	else
	{
		clock_time->utcoffset  = -1*60*60; /* MET */
	}

	if (status & HOPF_DSTWARN)
	{
		clock_time->flags |= PARSEB_ANNOUNCE;
	}
	
	switch (status & HOPF_MODE)
	{
	    default:	/* dummy: we cover all 4 cases. */
	    case HOPF_INVALID:  /* Time/Date invalid */
		clock_time->flags |= PARSEB_POWERUP;
		break;

	    case HOPF_INTERNAL: /* internal clock */
		clock_time->flags |= PARSEB_NOSYNC;
		break;

	    case HOPF_RADIO:    /* Radio clock */
	    case HOPF_RADIOHP:  /* Radio clock high precision */
		break;
	}

	return CVT_OK;
}

/*
 * parse_inp_fnc_t inp_hopf6021
 *
 * grab data from input stream
 */
static u_long
inp_hopf6021(
	     parse_t      *parseio,
	     char         ch,
	     timestamp_t  *tstamp
	  )
{
	unsigned int rtc;

	parseprintf(DD_PARSE, ("inp_hopf6021(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	switch (ch)
	{
	case ETX:
		parseprintf(DD_PARSE, ("inp_hopf6021: EOL seen\n"));
		parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */
		if ((rtc = parse_addchar(parseio, ch)) == PARSE_INP_SKIP)
			return parse_end(parseio);
		else
			return rtc;

	default:
		return parse_addchar(parseio, ch);
	}
}

/*
 * convert a hex-digit to numeric value
 */
static unsigned char
hexval(
	unsigned char ch
	)
{
	unsigned int dv;
	
	if ((dv = ch - '0') >= 10u)
	{
		if ((dv -= 'A'-'0') < 6u || (dv -= 'a'-'A') < 6u)
		{
			dv += 10;
		}
		else
		{
			dv = 0xFF;
		}
	}
	return (unsigned char)dv;
}

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_HOPF6021) */
int clk_hopf6021_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_HOPF6021) */

/*
 * History:
 *
 * clk_hopf6021.c,v
 * Revision 4.10  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.7  1999/11/28 09:13:49  kardel
 * RECON_4_0_98F
 *
 * Revision 4.6  1998/11/15 20:27:57  kardel
 * Release 4.0.73e13 reconcilation
 *
 * Revision 4.5  1998/06/14 21:09:35  kardel
 * Sun acc cleanup
 *
 * Revision 4.4  1998/06/13 12:02:38  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.3  1998/06/12 15:22:27  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/06/12 09:13:25  kardel
 * conditional compile macros fixed
 * printf prototype
 *
 * Revision 4.1  1998/05/24 09:39:52  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:29  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.6 log info deleted 1998/04/11 kardel
 */
