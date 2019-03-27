/*
 * /src/NTP/REPOSITORY/ntp4-dev/libparse/clk_rawdcf.c,v 4.18 2006/06/22 18:40:01 kardel RELEASE_20060622_A
 *
 * clk_rawdcf.c,v 4.18 2006/06/22 18:40:01 kardel RELEASE_20060622_A
 *
 * Raw DCF77 pulse clock support
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PARSE) && defined(CLOCK_RAWDCF)

#include "ntp_fp.h"
#include "timevalops.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"

#include "parse.h"
#ifdef PARSESTREAM
# include <sys/parsestreams.h>
#endif

#ifndef PARSEKERNEL
# include "ntp_stdlib.h"
#endif

/*
 * DCF77 raw time code
 *
 * From "Zur Zeit", Physikalisch-Technische Bundesanstalt (PTB), Braunschweig
 * und Berlin, Maerz 1989
 *
 * Timecode transmission:
 * AM:
 *	time marks are send every second except for the second before the
 *	next minute mark
 *	time marks consist of a reduction of transmitter power to 25%
 *	of the nominal level
 *	the falling edge is the time indication (on time)
 *	time marks of a 100ms duration constitute a logical 0
 *	time marks of a 200ms duration constitute a logical 1
 * FM:
 *	see the spec. (basically a (non-)inverted psuedo random phase shift)
 *
 * Encoding:
 * Second	Contents
 * 0  - 10	AM: free, FM: 0
 * 11 - 14	free
 * 15		R     - "call bit" used to signalize irregularities in the control facilities
 *		        (until 2003 indicated transmission via alternate antenna)
 * 16		A1    - expect zone change (1 hour before)
 * 17 - 18	Z1,Z2 - time zone
 *		 0  0 illegal
 *		 0  1 MEZ  (MET)
 *		 1  0 MESZ (MED, MET DST)
 *		 1  1 illegal
 * 19		A2    - expect leap insertion/deletion (1 hour before)
 * 20		S     - start of time code (1)
 * 21 - 24	M1    - BCD (lsb first) Minutes
 * 25 - 27	M10   - BCD (lsb first) 10 Minutes
 * 28		P1    - Minute Parity (even)
 * 29 - 32	H1    - BCD (lsb first) Hours
 * 33 - 34      H10   - BCD (lsb first) 10 Hours
 * 35		P2    - Hour Parity (even)
 * 36 - 39	D1    - BCD (lsb first) Days
 * 40 - 41	D10   - BCD (lsb first) 10 Days
 * 42 - 44	DW    - BCD (lsb first) day of week (1: Monday -> 7: Sunday)
 * 45 - 49	MO    - BCD (lsb first) Month
 * 50           MO0   - 10 Months
 * 51 - 53	Y1    - BCD (lsb first) Years
 * 54 - 57	Y10   - BCD (lsb first) 10 Years
 * 58 		P3    - Date Parity (even)
 * 59		      - usually missing (minute indication), except for leap insertion
 */

static parse_pps_fnc_t pps_rawdcf;
static parse_cvt_fnc_t cvt_rawdcf;
static parse_inp_fnc_t inp_rawdcf;

typedef struct last_tcode {
	time_t      tcode;	/* last converted time code */
        timestamp_t tminute;	/* sample time for minute start */
        timestamp_t timeout;	/* last timeout timestamp */
} last_tcode_t;

#define BUFFER_MAX	61

clockformat_t clock_rawdcf =
{
  inp_rawdcf,			/* DCF77 input handling */
  cvt_rawdcf,			/* raw dcf input conversion */
  pps_rawdcf,			/* examining PPS information */
  0,				/* no private configuration data */
  "RAW DCF77 Timecode",		/* direct decoding / time synthesis */

  BUFFER_MAX,			/* bit buffer */
  sizeof(last_tcode_t)
};

static struct dcfparam
{
	const unsigned char *onebits;
	const unsigned char *zerobits;
} dcfparameter =
{
	(const unsigned char *)"###############RADMLS1248124P124812P1248121241248112481248P??", /* 'ONE' representation */
	(const unsigned char *)"--------------------s-------p------p----------------------p__"  /* 'ZERO' representation */
};

static struct rawdcfcode
{
	char offset;			/* start bit */
} rawdcfcode[] =
{
	{  0 }, { 15 }, { 16 }, { 17 }, { 19 }, { 20 }, { 21 }, { 25 }, { 28 }, { 29 },
	{ 33 }, { 35 }, { 36 }, { 40 }, { 42 }, { 45 }, { 49 }, { 50 }, { 54 }, { 58 }, { 59 }
};

#define DCF_M	0
#define DCF_R	1
#define DCF_A1	2
#define DCF_Z	3
#define DCF_A2	4
#define DCF_S	5
#define DCF_M1	6
#define DCF_M10	7
#define DCF_P1	8
#define DCF_H1	9
#define DCF_H10	10
#define DCF_P2	11
#define DCF_D1	12
#define DCF_D10	13
#define DCF_DW	14
#define DCF_MO	15
#define DCF_MO0	16
#define DCF_Y1	17
#define DCF_Y10	18
#define DCF_P3	19

static struct partab
{
	char offset;			/* start bit of parity field */
} partab[] =
{
	{ 21 }, { 29 }, { 36 }, { 59 }
};

#define DCF_P_P1	0
#define DCF_P_P2	1
#define DCF_P_P3	2

#define DCF_Z_MET 0x2
#define DCF_Z_MED 0x1

static u_long
ext_bf(
	unsigned char *buf,
	int   idx,
	const unsigned char *zero
	)
{
	u_long sum = 0;
	int i, first;

	first = rawdcfcode[idx].offset;

	for (i = rawdcfcode[idx+1].offset - 1; i >= first; i--)
	{
		sum <<= 1;
		sum |= (buf[i] != zero[i]);
	}
	return sum;
}

static unsigned
pcheck(
       unsigned char *buf,
       int   idx,
       const unsigned char *zero
       )
{
	int i,last;
	unsigned psum = 1;

	last = partab[idx+1].offset;

	for (i = partab[idx].offset; i < last; i++)
	    psum ^= (buf[i] != zero[i]);

	return psum;
}

static u_long
convert_rawdcf(
	       unsigned char   *buffer,
	       int              size,
	       struct dcfparam *dcfprm,
	       clocktime_t     *clock_time
	       )
{
	unsigned char *s = buffer;
	const unsigned char *b = dcfprm->onebits;
	const unsigned char *c = dcfprm->zerobits;
	int i;

	parseprintf(DD_RAWDCF,("parse: convert_rawdcf: \"%.*s\"\n", size, buffer));

	if (size < 57)
	{
#ifndef PARSEKERNEL
		msyslog(LOG_ERR, "parse: convert_rawdcf: INCOMPLETE DATA - time code only has %d bits", size);
#endif
		return CVT_FAIL|CVT_BADFMT;
	}

	for (i = 0; i < size; i++)
	{
		if ((*s != *b) && (*s != *c))
		{
			/*
			 * we only have two types of bytes (ones and zeros)
			 */
#ifndef PARSEKERNEL
			msyslog(LOG_ERR, "parse: convert_rawdcf: BAD DATA - no conversion");
#endif
			return CVT_FAIL|CVT_BADFMT;
		}
		if (*b) b++;
		if (*c) c++;
		s++;
	}

	/*
	 * check Start and Parity bits
	 */
	if ((ext_bf(buffer, DCF_S, dcfprm->zerobits) == 1) &&
	    pcheck(buffer, DCF_P_P1, dcfprm->zerobits) &&
	    pcheck(buffer, DCF_P_P2, dcfprm->zerobits) &&
	    pcheck(buffer, DCF_P_P3, dcfprm->zerobits))
	{
		/*
		 * buffer OK
		 */
		parseprintf(DD_RAWDCF,("parse: convert_rawdcf: parity check passed\n"));

		clock_time->flags  = PARSEB_S_CALLBIT|PARSEB_S_LEAP;
		clock_time->utctime= 0;
		clock_time->usecond= 0;
		clock_time->second = 0;
		clock_time->minute = ext_bf(buffer, DCF_M10, dcfprm->zerobits);
		clock_time->minute = TIMES10(clock_time->minute) + ext_bf(buffer, DCF_M1, dcfprm->zerobits);
		clock_time->hour   = ext_bf(buffer, DCF_H10, dcfprm->zerobits);
		clock_time->hour   = TIMES10(clock_time->hour) + ext_bf(buffer, DCF_H1, dcfprm->zerobits);
		clock_time->day    = ext_bf(buffer, DCF_D10, dcfprm->zerobits);
		clock_time->day    = TIMES10(clock_time->day) + ext_bf(buffer, DCF_D1, dcfprm->zerobits);
		clock_time->month  = ext_bf(buffer, DCF_MO0, dcfprm->zerobits);
		clock_time->month  = TIMES10(clock_time->month) + ext_bf(buffer, DCF_MO, dcfprm->zerobits);
		clock_time->year   = ext_bf(buffer, DCF_Y10, dcfprm->zerobits);
		clock_time->year   = TIMES10(clock_time->year) + ext_bf(buffer, DCF_Y1, dcfprm->zerobits);

		switch (ext_bf(buffer, DCF_Z, dcfprm->zerobits))
		{
		    case DCF_Z_MET:
			clock_time->utcoffset = -1*60*60;
			break;

		    case DCF_Z_MED:
			clock_time->flags     |= PARSEB_DST;
			clock_time->utcoffset  = -2*60*60;
			break;

		    default:
			parseprintf(DD_RAWDCF,("parse: convert_rawdcf: BAD TIME ZONE\n"));
			return CVT_FAIL|CVT_BADFMT;
		}

		if (ext_bf(buffer, DCF_A1, dcfprm->zerobits))
		    clock_time->flags |= PARSEB_ANNOUNCE;

		if (ext_bf(buffer, DCF_A2, dcfprm->zerobits))
		    clock_time->flags |= PARSEB_LEAPADD; /* default: DCF77 data format deficiency */

		if (ext_bf(buffer, DCF_R, dcfprm->zerobits))
		    clock_time->flags |= PARSEB_CALLBIT;

		parseprintf(DD_RAWDCF,("parse: convert_rawdcf: TIME CODE OK: %02d:%02d, %02d.%02d.%02d, flags 0x%lx\n",
				       (int)clock_time->hour, (int)clock_time->minute, (int)clock_time->day, (int)clock_time->month,(int) clock_time->year,
				       (u_long)clock_time->flags));
		return CVT_OK;
	}
	else
	{
		/*
		 * bad format - not for us
		 */
#ifndef PARSEKERNEL
		msyslog(LOG_ERR, "parse: convert_rawdcf: start bit / parity check FAILED for \"%.*s\"", size, buffer);
#endif
		return CVT_FAIL|CVT_BADFMT;
	}
}

/*
 * parse_cvt_fnc_t cvt_rawdcf
 * raw dcf input routine - needs to fix up 50 baud
 * characters for 1/0 decision
 */
static u_long
cvt_rawdcf(
	   unsigned char   *buffer,
	   int              size,
	   struct format   *param,
	   clocktime_t     *clock_time,
	   void            *local
	   )
{
	last_tcode_t  *t = (last_tcode_t *)local;
	unsigned char *s = (unsigned char *)buffer;
	unsigned char *e = s + size;
	const unsigned char *b = dcfparameter.onebits;
	const unsigned char *c = dcfparameter.zerobits;
	u_long       rtc = CVT_NONE;
	unsigned int i, lowmax, highmax, cutoff, span;
#define BITS 9
	unsigned char     histbuf[BITS];
	/*
	 * the input buffer contains characters with runs of consecutive
	 * bits set. These set bits are an indication of the DCF77 pulse
	 * length. We assume that we receive the pulse at 50 Baud. Thus
	 * a 100ms pulse would generate a 4 bit train (20ms per bit and
	 * start bit)
	 * a 200ms pulse would create all zeroes (and probably a frame error)
	 */

	for (i = 0; i < BITS; i++)
	{
		histbuf[i] = 0;
	}

	cutoff = 0;
	lowmax = 0;

	while (s < e)
	{
		unsigned int ch = *s ^ 0xFF;
		/*
		 * these lines are left as an excercise to the reader 8-)
		 */
		if (!((ch+1) & ch) || !*s)
		{

			for (i = 0; ch; i++)
			{
				ch >>= 1;
			}

			*s = (unsigned char) i;
			histbuf[i]++;
			cutoff += i;
			lowmax++;
		}
		else
		{
			parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: character check for 0x%x@%d FAILED\n", *s, (int)(s - (unsigned char *)buffer)));
			*s = (unsigned char)~0;
			rtc = CVT_FAIL|CVT_BADFMT;
		}
		s++;
	}

	if (lowmax)
	{
		cutoff /= lowmax;
	}
	else
	{
		cutoff = 4;	/* doesn't really matter - it'll fail anyway, but gives error output */
	}

	parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: average bit count: %d\n", cutoff));

	lowmax = 0;
	highmax = 0;

	parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: histogram:"));
	for (i = 0; i <= cutoff; i++)
	{
		lowmax+=histbuf[i] * i;
		highmax += histbuf[i];
		parseprintf(DD_RAWDCF,(" %d", histbuf[i]));
	}
	parseprintf(DD_RAWDCF, (" <M>"));

	lowmax += highmax / 2;

	if (highmax)
	{
		lowmax /= highmax;
	}
	else
	{
		lowmax = 0;
	}

	highmax = 0;
	cutoff = 0;

	for (; i < BITS; i++)
	{
		highmax+=histbuf[i] * i;
		cutoff +=histbuf[i];
		parseprintf(DD_RAWDCF,(" %d", histbuf[i]));
	}
	parseprintf(DD_RAWDCF,("\n"));

	if (cutoff)
	{
		highmax /= cutoff;
	}
	else
	{
		highmax = BITS-1;
	}

	span = cutoff = lowmax;
	for (i = lowmax; i <= highmax; i++)
	{
		if (histbuf[cutoff] > histbuf[i])
		{
			cutoff = i;
			span = i;
		}
		else
		    if (histbuf[cutoff] == histbuf[i])
		    {
			    span = i;
		    }
	}

	cutoff = (cutoff + span) / 2;

	parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: lower maximum %d, higher maximum %d, cutoff %d\n", lowmax, highmax, cutoff));

	s = (unsigned char *)buffer;
	while (s < e)
	{
		if (*s == (unsigned char)~0)
		{
			*s = '?';
		}
		else
		{
			*s = (*s >= cutoff) ? *b : *c;
		}
		s++;
		if (*b) b++;
		if (*c) c++;
	}

	*s = '\0';

        if (rtc == CVT_NONE)
        {
	       rtc = convert_rawdcf(buffer, size, &dcfparameter, clock_time);
	       if (rtc == CVT_OK)
	       {
			time_t newtime;

			newtime = parse_to_unixtime(clock_time, &rtc);
			if ((rtc == CVT_OK) && t)
			{
				if ((newtime - t->tcode) <= 600) /* require a successful telegram within last 10 minutes */
				{
				        parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: recent timestamp check OK\n"));
					clock_time->utctime = newtime;
				}
				else
				{
					parseprintf(DD_RAWDCF,("parse: cvt_rawdcf: recent timestamp check FAIL - ignore timestamp\n"));
					rtc = CVT_SKIP;
				}
				t->tcode            = newtime;
			}
	       }
        }

    	return rtc;
}

/*
 * parse_pps_fnc_t pps_rawdcf
 *
 * currently a very stupid version - should be extended to decode
 * also ones and zeros (which is easy)
 */
/*ARGSUSED*/
static u_long
pps_rawdcf(
	parse_t *parseio,
	int status,
	timestamp_t *ptime
	)
{
	if (!status)		/* negative edge for simpler wiring (Rx->DCD) */
	{
		parseio->parse_dtime.parse_ptime  = *ptime;
		parseio->parse_dtime.parse_state |= PARSEB_PPS|PARSEB_S_PPS;
	}

	return CVT_NONE;
}

static long
calc_usecdiff(
	timestamp_t *ref,
	timestamp_t *base,
	long         offset
	)
{
	struct timeval delta;
	long delta_usec = 0;

#ifdef PARSEKERNEL
	delta.tv_sec = ref->tv.tv_sec - offset - base->tv.tv_sec;
	delta.tv_usec = ref->tv.tv_usec - base->tv.tv_usec;
	if (delta.tv_usec < 0)
	{
		delta.tv_sec  -= 1;
		delta.tv_usec += 1000000;
	}
#else
	l_fp delt;
	
	delt = ref->fp;
	delt.l_i -= offset;
	L_SUB(&delt, &base->fp);
	TSTOTV(&delt, &delta);
#endif

	delta_usec = 1000000 * (int32_t)delta.tv_sec + delta.tv_usec;
	return delta_usec;
}

static u_long
snt_rawdcf(
	parse_t *parseio,
	timestamp_t *ptime
	)
{
	/*
	 * only synthesize if all of following conditions are met:
	 * - CVT_OK parse_status (we have a time stamp base)
	 * - ABS(ptime - tminute - (parse_index - 1) sec) < 500ms (spaced by 1 sec +- 500ms)
	 * - minute marker is available (confirms minute raster as base)
	 */
	last_tcode_t  *t = (last_tcode_t *)parseio->parse_pdata;
	long delta_usec = -1;

	if (t != NULL && t->tminute.tv.tv_sec != 0) {
		delta_usec = calc_usecdiff(ptime, &t->tminute, parseio->parse_index - 1);
		if (delta_usec < 0)
			delta_usec = -delta_usec;
	}
	
	parseprintf(DD_RAWDCF,("parse: snt_rawdcf: synth for offset %d seconds - absolute usec error %ld\n",
			       parseio->parse_index - 1, delta_usec));

	if (((parseio->parse_dtime.parse_status & CVT_MASK) == CVT_OK) &&
	    (delta_usec < 500000 && delta_usec >= 0)) /* only if minute marker is available */
	{
		parseio->parse_dtime.parse_stime = *ptime;

#ifdef PARSEKERNEL
		parseio->parse_dtime.parse_time.tv.tv_sec++;
#else
		parseio->parse_dtime.parse_time.fp.l_ui++;
#endif

		parseprintf(DD_RAWDCF,("parse: snt_rawdcf: time stamp synthesized offset %d seconds\n", parseio->parse_index - 1));

		return updatetimeinfo(parseio, parseio->parse_lstate);
	}
	return CVT_NONE;
}

/*
 * parse_inp_fnc_t inp_rawdcf
 *
 * grab DCF77 data from input stream
 */
static u_long
inp_rawdcf(
	  parse_t      *parseio,
	  char         ch,
	  timestamp_t  *tstamp
	  )
{
	static struct timeval timeout = { 1, 500000 }; /* 1.5 secongs denote second #60 */

	parseprintf(DD_PARSE, ("inp_rawdcf(0x%p, 0x%x, ...)\n", (void*)parseio, ch));

	parseio->parse_dtime.parse_stime = *tstamp; /* collect timestamp */

	if (parse_timedout(parseio, tstamp, &timeout))
	{
		last_tcode_t *t = (last_tcode_t *)parseio->parse_pdata;
		long delta_usec;
		
		parseprintf(DD_RAWDCF, ("inp_rawdcf: time out seen\n"));
		/* finish collection */
		(void) parse_end(parseio);

		if (t != NULL)
		{
			/* remember minute start sample time if timeouts occur in minute raster */
			if (t->timeout.tv.tv_sec != 0)
			{
				delta_usec = calc_usecdiff(tstamp, &t->timeout, 60);
				if (delta_usec < 0)
					delta_usec = -delta_usec;
			}
			else
			{
				delta_usec = -1;
			}

			if (delta_usec < 500000 && delta_usec >= 0)
			{
				parseprintf(DD_RAWDCF, ("inp_rawdcf: timeout time difference %ld usec - minute marker set\n", delta_usec));
				/* collect minute markers only if spaced by 60 seconds */
				t->tminute = *tstamp;
			}
			else
			{
				parseprintf(DD_RAWDCF, ("inp_rawdcf: timeout time difference %ld usec - minute marker cleared\n", delta_usec));
				memset((char *)&t->tminute, 0, sizeof(t->tminute));
			}
			t->timeout = *tstamp;
		}
		(void) parse_addchar(parseio, ch);

		/* pass up to higher layers */
		return PARSE_INP_TIME;
	}
	else
	{
		unsigned int rtc;

		rtc = parse_addchar(parseio, ch);
		if (rtc == PARSE_INP_SKIP)
		{
			if (snt_rawdcf(parseio, tstamp) == CVT_OK)
				return PARSE_INP_SYNTH;
		}
		return rtc;
	}
}

#else /* not (REFCLOCK && CLOCK_PARSE && CLOCK_RAWDCF) */
int clk_rawdcf_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE && CLOCK_RAWDCF) */

/*
 * History:
 *
 * clk_rawdcf.c,v
 * Revision 4.18  2006/06/22 18:40:01  kardel
 * clean up signedness (gcc 4)
 *
 * Revision 4.17  2006/01/22 16:01:55  kardel
 * update version information
 *
 * Revision 4.16  2006/01/22 15:51:22  kardel
 * generate reasonable timecode output on invalid input
 *
 * Revision 4.15  2005/08/06 19:17:06  kardel
 * clean log output
 *
 * Revision 4.14  2005/08/06 17:39:40  kardel
 * cleanup size handling wrt/ to buffer boundaries
 *
 * Revision 4.13  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.12  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.9  1999/12/06 13:42:23  kardel
 * transfer correctly converted time codes always into tcode
 *
 * Revision 4.8  1999/11/28 09:13:50  kardel
 * RECON_4_0_98F
 *
 * Revision 4.7  1999/04/01 20:07:20  kardel
 * added checking for minutie increment of timestamps in clk_rawdcf.c
 *
 * Revision 4.6  1998/06/14 21:09:37  kardel
 * Sun acc cleanup
 *
 * Revision 4.5  1998/06/13 12:04:16  kardel
 * fix SYSV clock name clash
 *
 * Revision 4.4  1998/06/12 15:22:28  kardel
 * fix prototypes
 *
 * Revision 4.3  1998/06/06 18:33:36  kardel
 * simplified condidional compile expression
 *
 * Revision 4.2  1998/05/24 11:04:18  kardel
 * triggering PPS on negative edge for simpler wiring (Rx->DCD)
 *
 * Revision 4.1  1998/05/24 09:39:53  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:30  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.24 log info deleted 1998/04/11 kardel
 *
 */
