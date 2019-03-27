/*
 * /src/NTP/ntp4-dev/libparse/parse.c,v 4.20 2005/08/06 17:39:40 kardel RELEASE_20050806_A
 *
 * parse.c,v 4.20 2005/08/06 17:39:40 kardel RELEASE_20050806_A
 *
 * Parser module for reference clock
 *
 * PARSEKERNEL define switches between two personalities of the module
 * if PARSEKERNEL is defined this module can be used
 * as kernel module. In this case the time stamps will be
 * a struct timeval.
 * when PARSEKERNEL is not defined NTP time stamps will be used.
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

#if	!(defined(lint) || defined(__GNUC__))
static char rcsid[] = "parse.c,v 4.20 2005/08/06 17:39:40 kardel RELEASE_20050806_A";
#endif

#include "ntp_fp.h"
#include "timevalops.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"
#include "ntp_machine.h"
#include "ntp.h"		/* (get Y2KFixes definitions) 	Y2KFixes */

#include "parse.h"

#ifndef PARSESTREAM
# include <stdio.h>
#else
# include "sys/parsestreams.h"
#endif

extern clockformat_t *clockformats[];
extern unsigned short nformats;

static u_long timepacket (parse_t *);

/*
 * strings support usually not in kernel - duplicated, but what the heck
 */
static int
Strlen(
	register const char *s
	)
{
	register int c;

	c = 0;
	if (s)
	{
		while (*s++)
		{
			c++;
		}
	}
	return c;
}

static int
Strcmp(
	register const char *s,
	register const char *t
	)
{
	register int c = 0;

	if (!s || !t || (s == t))
	{
		return 0;
	}

	while (!(c = *s++ - *t++) && *s && *t)
	    /* empty loop */;

	return c;
}

int
parse_timedout(
	       parse_t *parseio,
	       timestamp_t *tstamp,
	       struct timeval *del
	       )
{
	struct timeval delta;

#ifdef PARSEKERNEL
	delta.tv_sec = tstamp->tv.tv_sec - parseio->parse_lastchar.tv.tv_sec;
	delta.tv_usec = tstamp->tv.tv_usec - parseio->parse_lastchar.tv.tv_usec;
	if (delta.tv_usec < 0)
	{
		delta.tv_sec  -= 1;
		delta.tv_usec += 1000000;
	}
#else
	l_fp delt;

	delt = tstamp->fp;
	L_SUB(&delt, &parseio->parse_lastchar.fp);
	TSTOTV(&delt, &delta);
#endif

	if (timercmp(&delta, del, >))
	{
		parseprintf(DD_PARSE, ("parse: timedout: TRUE\n"));
		return 1;
	}
	else
	{
		parseprintf(DD_PARSE, ("parse: timedout: FALSE\n"));
		return 0;
	}
}

/*ARGSUSED*/
int
parse_ioinit(
	register parse_t *parseio
	)
{
	parseprintf(DD_PARSE, ("parse_iostart\n"));

	parseio->parse_plen = 0;
	parseio->parse_pdata = (void *)0;

	parseio->parse_data = 0;
	parseio->parse_ldata = 0;
	parseio->parse_dsize = 0;

	parseio->parse_badformat = 0;
	parseio->parse_ioflags   = PARSE_IO_CS7;	/* usual unix default */
	parseio->parse_index     = 0;
	parseio->parse_ldsize    = 0;

	return 1;
}

/*ARGSUSED*/
void
parse_ioend(
	register parse_t *parseio
	)
{
	parseprintf(DD_PARSE, ("parse_ioend\n"));

	if (parseio->parse_pdata)
	    FREE(parseio->parse_pdata, parseio->parse_plen);

	if (parseio->parse_data)
	    FREE(parseio->parse_data, (unsigned)(parseio->parse_dsize * 2 + 2));
}

unsigned int
parse_restart(
	      parse_t *parseio,
	      char ch
	      )
{
	unsigned int updated = PARSE_INP_SKIP;

	/*
	 * re-start packet - timeout - overflow - start symbol
	 */

	if (parseio->parse_index)
	{
		/*
		 * filled buffer - thus not end character found
		 * do processing now
		 */
		parseio->parse_data[parseio->parse_index] = '\0';
		memcpy(parseio->parse_ldata, parseio->parse_data, (unsigned)(parseio->parse_index+1));
		parseio->parse_ldsize = parseio->parse_index;
		updated = PARSE_INP_TIME;
	}

	parseio->parse_index = 1;
	parseio->parse_data[0] = ch;
	parseprintf(DD_PARSE, ("parse: parse_restart: buffer start (updated = %x)\n", updated));
	return updated;
}

unsigned int
parse_addchar(
	      parse_t *parseio,
	      char ch
	      )
{
	/*
	 * add to buffer
	 */
	if (parseio->parse_index < parseio->parse_dsize)
	{
		/*
		 * collect into buffer
		 */
		parseprintf(DD_PARSE, ("parse: parse_addchar: buffer[%d] = 0x%x\n", parseio->parse_index, ch));
		parseio->parse_data[parseio->parse_index++] = (char)ch;
		return PARSE_INP_SKIP;
	}
	else
		/*
		 * buffer overflow - attempt to make the best of it
		 */
		return parse_restart(parseio, ch);
}

unsigned int
parse_end(
	  parse_t *parseio
	  )
{
	/*
	 * message complete processing
	 */
	parseio->parse_data[parseio->parse_index] = '\0';
	memcpy(parseio->parse_ldata, parseio->parse_data, (unsigned)(parseio->parse_index+1));
	parseio->parse_ldsize = parseio->parse_index;
	parseio->parse_index = 0;
	parseprintf(DD_PARSE, ("parse: parse_end: buffer end\n"));
	return PARSE_INP_TIME;
}

/*ARGSUSED*/
int
parse_ioread(
	register parse_t *parseio,
	register char ch,
	register timestamp_t *tstamp
	)
{
	register u_int updated = CVT_NONE;
	/*
	 * within STREAMS CSx (x < 8) chars still have the upper bits set
	 * so we normalize the characters by masking unecessary bits off.
	 */
	switch (parseio->parse_ioflags & PARSE_IO_CSIZE)
	{
	    case PARSE_IO_CS5:
		ch &= 0x1F;
		break;

	    case PARSE_IO_CS6:
		ch &= 0x3F;
		break;

	    case PARSE_IO_CS7:
		ch &= 0x7F;
		break;

	    case PARSE_IO_CS8:
		ch &= (char) 0xFFU;
		break;
	}

	parseprintf(DD_PARSE, ("parse_ioread(0x%p, char=0x%x, ..., ...)\n", (void*)parseio, ch & 0xFF));

	if (!clockformats[parseio->parse_lformat]->convert)
	{
		parseprintf(DD_PARSE, ("parse_ioread: input dropped.\n"));
		return CVT_NONE;
	}

	if (clockformats[parseio->parse_lformat]->input)
	{
		unsigned long input_status;

		input_status = clockformats[parseio->parse_lformat]->input(parseio, ch, tstamp);

		if (input_status & PARSE_INP_SYNTH)
		{
			updated = CVT_OK;
		}

		if (input_status & PARSE_INP_TIME)	/* time sample is available */
		{
			updated = (u_int) timepacket(parseio);
		}

		if (input_status & PARSE_INP_DATA) /* got additional data */
		{
			updated |= CVT_ADDITIONAL;
		}
	}


	/*
	 * remember last character time
	 */
	parseio->parse_lastchar = *tstamp;

#ifdef DEBUG
	if ((updated & CVT_MASK) != CVT_NONE)
	{
		parseprintf(DD_PARSE, ("parse_ioread: time sample accumulated (status=0x%x)\n", updated));
	}
#endif

	parseio->parse_dtime.parse_status = updated;

	return (((updated & CVT_MASK) != CVT_NONE) ||
		((updated & CVT_ADDITIONAL) != 0));
}

/*
 * parse_iopps
 *
 * take status line indication and derive synchronisation information
 * from it.
 * It can also be used to decode a serial serial data format (such as the
 * ONE, ZERO, MINUTE sync data stream from DCF77)
 */
/*ARGSUSED*/
int
parse_iopps(
	register parse_t *parseio,
	register int status,
	register timestamp_t *ptime
	)
{
	register u_int updated = CVT_NONE;

	/*
	 * PPS pulse information will only be delivered to ONE clock format
	 * this is either the last successful conversion module with a ppssync
	 * routine, or a fixed format with a ppssync routine
	 */
	parseprintf(DD_PARSE, ("parse_iopps: STATUS %s\n", (status == SYNC_ONE) ? "ONE" : "ZERO"));

	if (clockformats[parseio->parse_lformat]->syncpps)
	{
		updated = (u_int) clockformats[parseio->parse_lformat]->syncpps(parseio, status == SYNC_ONE, ptime);
		parseprintf(DD_PARSE, ("parse_iopps: updated = 0x%x\n", updated));
	}

	return (updated & CVT_MASK) != CVT_NONE;
}

/*
 * parse_iodone
 *
 * clean up internal status for new round
 */
/*ARGSUSED*/
void
parse_iodone(
	register parse_t *parseio
	)
{
	/*
	 * we need to clean up certain flags for the next round
	 */
	parseprintf(DD_PARSE, ("parse_iodone: DONE\n"));
	parseio->parse_dtime.parse_state = 0; /* no problems with ISRs */
}

/*---------- conversion implementation --------------------*/

/*
 * convert a struct clock to UTC since Jan, 1st 1970 0:00 (the UNIX EPOCH)
 */
#define days_per_year(x)	((x) % 4 ? 365 : ((x % 400) ? ((x % 100) ? 366 : 365) : 366))

time_t
parse_to_unixtime(
	register clocktime_t   *clock_time,
	register u_long *cvtrtc
	)
{
#define SETRTC(_X_)	{ if (cvtrtc) *cvtrtc = (_X_); }
	static int days_of_month[] =
	{
		0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	register int i;
	time_t t;

	if (clock_time->utctime)
	    return clock_time->utctime;	/* if the conversion routine gets it right away - why not */

	if ( clock_time->year < YEAR_PIVOT )			/* Y2KFixes [ */
	    clock_time->year += 100;	/* convert 20xx%100 to 20xx-1900 */
	if ( clock_time->year < YEAR_BREAK )	/* expand to full four-digits */
	    clock_time->year += 1900;

	if (clock_time->year < 1970 )				/* Y2KFixes ] */
	{
		SETRTC(CVT_FAIL|CVT_BADDATE);
		return -1;
	}

	/*
	 * sorry, slow section here - but it's not time critical anyway
	 */
	t = julian0(clock_time->year) - julian0(1970);		/* Y2kFixes */
				/* month */
	if (clock_time->month <= 0 || clock_time->month > 12)
	{
		SETRTC(CVT_FAIL|CVT_BADDATE);
		return -1;		/* bad month */
	}

#if 0								/* Y2KFixes */
				/* adjust leap year */
	if (clock_time->month < 3 && days_per_year(clock_time->year) == 366)
	    t--;
#else								/* Y2KFixes [ */
	if ( clock_time->month >= 3  &&  isleap_4(clock_time->year) )
	    t++;		/* add one more if within leap year */
#endif								/* Y2KFixes ] */

	for (i = 1; i < clock_time->month; i++)
	{
		t += days_of_month[i];
	}
				/* day */
	if (clock_time->day < 1 || ((clock_time->month == 2 && days_per_year(clock_time->year) == 366) ?
			       clock_time->day > 29 : clock_time->day > days_of_month[clock_time->month]))
	{
		SETRTC(CVT_FAIL|CVT_BADDATE);
		return -1;		/* bad day */
	}

	t += clock_time->day - 1;
				/* hour */
	if (clock_time->hour < 0 || clock_time->hour >= 24)
	{
		SETRTC(CVT_FAIL|CVT_BADTIME);
		return -1;		/* bad hour */
	}

	t = TIMES24(t) + clock_time->hour;

				/* min */
	if (clock_time->minute < 0 || clock_time->minute > 59)
	{
		SETRTC(CVT_FAIL|CVT_BADTIME);
		return -1;		/* bad min */
	}

	t = TIMES60(t) + clock_time->minute;
				/* sec */

	if (clock_time->second < 0 || clock_time->second > 60)	/* allow for LEAPs */
	{
		SETRTC(CVT_FAIL|CVT_BADTIME);
		return -1;		/* bad sec */
	}

	t  = TIMES60(t) + clock_time->second;

	t += clock_time->utcoffset;	/* warp to UTC */

				/* done */

	clock_time->utctime = t;		/* documentray only */

	return t;
}

/*--------------- format conversion -----------------------------------*/

int
Stoi(
	const unsigned char *s,
	long *zp,
	int cnt
	)
{
	char unsigned const *b = s;
	int f,z,v;
	char unsigned c;

	f=z=v=0;

	while(*s == ' ')
	    s++;

	if (*s == '-')
	{
		s++;
		v = 1;
	}
	else
	    if (*s == '+')
		s++;

	for(;;)
	{
		c = *s++;
		if (c == '\0' || c < '0' || c > '9' || (cnt && ((s-b) > cnt)))
		{
			if (f == 0)
			{
				return(-1);
			}
			if (v)
			    z = -z;
			*zp = z;
			return(0);
		}
		z = (z << 3) + (z << 1) + ( c - '0' );
		f=1;
	}
}

int
Strok(
	const unsigned char *s,
	const unsigned char *m
	)
{
	if (!s || !m)
	    return 0;

	while(*s && *m)
	{
		if ((*m == ' ') ? 1 : (*s == *m))
		{
			s++;
			m++;
		}
		else
		{
			return 0;
		}
	}
	return !*m;
}

u_long
updatetimeinfo(
	       register parse_t *parseio,
	       register u_long   flags
	       )
{
#ifdef PARSEKERNEL
	{
		int s = splhigh();
#endif

		parseio->parse_lstate          = parseio->parse_dtime.parse_state | flags | PARSEB_TIMECODE;

		parseio->parse_dtime.parse_state = parseio->parse_lstate;

#ifdef PARSEKERNEL
		(void)splx((unsigned int)s);
	}
#endif


#ifdef PARSEKERNEL
	parseprintf(DD_PARSE, ("updatetimeinfo status=0x%x, time=%x\n", parseio->parse_dtime.parse_state,
			       parseio->parse_dtime.parse_time.tv.tv_sec));
#else
	parseprintf(DD_PARSE, ("updatetimeinfo status=0x%lx, time=%x\n", (long)parseio->parse_dtime.parse_state,
	                       parseio->parse_dtime.parse_time.fp.l_ui));
#endif

	return CVT_OK;		/* everything fine and dandy... */
}


/*
 * syn_simple
 *
 * handle a sync time stamp
 */
/*ARGSUSED*/
void
syn_simple(
	register parse_t *parseio,
	register timestamp_t *ts,
	register struct format *format,
	register u_long why
	)
{
	parseio->parse_dtime.parse_stime = *ts;
}

/*
 * parse_pps_fnc_t pps_simple
 *
 * handle a pps time stamp
 */
/*ARGSUSED*/
u_long
pps_simple(
	register parse_t *parseio,
	register int status,
	register timestamp_t *ptime
	)
{
	parseio->parse_dtime.parse_ptime  = *ptime;
	parseio->parse_dtime.parse_state |= PARSEB_PPS|PARSEB_S_PPS;

	return CVT_NONE;
}

/*
 * parse_pps_fnc_t pps_one
 *
 * handle a pps time stamp in ONE edge
 */
/*ARGSUSED*/
u_long
pps_one(
	register parse_t *parseio,
	register int status,
	register timestamp_t *ptime
	)
{
	if (status)
		return pps_simple(parseio, status, ptime);

	return CVT_NONE;
}

/*
 * parse_pps_fnc_t pps_zero
 *
 * handle a pps time stamp in ZERO edge
 */
/*ARGSUSED*/
u_long
pps_zero(
	register parse_t *parseio,
	register int status,
	register timestamp_t *ptime
	)
{
	if (!status)
		return pps_simple(parseio, status, ptime);

	return CVT_NONE;
}

/*
 * timepacket
 *
 * process a data packet
 */
static u_long
timepacket(
	register parse_t *parseio
	)
{
	register unsigned short format;
	register time_t t;
	u_long cvtrtc;		/* current conversion result */
	clocktime_t clock_time;

	memset((char *)&clock_time, 0, sizeof clock_time);
	format = parseio->parse_lformat;

	if (format == (unsigned short)~0)
		return CVT_NONE;

	switch ((cvtrtc = clockformats[format]->convert ?
		 clockformats[format]->convert((unsigned char *)parseio->parse_ldata, parseio->parse_ldsize, (struct format *)(clockformats[format]->data), &clock_time, parseio->parse_pdata) :
		 CVT_NONE) & CVT_MASK)
	{
	case CVT_FAIL:
		parseio->parse_badformat++;
		return cvtrtc;

	case CVT_NONE:
		/*
		 * too bad - pretend bad format
		 */
		parseio->parse_badformat++;
		return CVT_NONE;

	case CVT_OK:
		break;

	case CVT_SKIP:
		return CVT_NONE;

	default:
		/* shouldn't happen */
#ifndef PARSEKERNEL
		msyslog(LOG_WARNING, "parse: INTERNAL error: bad return code of convert routine \"%s\"", clockformats[format]->name);
#endif
		return CVT_FAIL|cvtrtc;
	}

	if ((t = parse_to_unixtime(&clock_time, &cvtrtc)) == -1)
	{
		return CVT_FAIL|cvtrtc;
	}

	/*
	 * time stamp
	 */
#ifdef PARSEKERNEL
	parseio->parse_dtime.parse_time.tv.tv_sec  = t;
	parseio->parse_dtime.parse_time.tv.tv_usec = clock_time.usecond;
#else
	parseio->parse_dtime.parse_time.fp.l_ui = (uint32_t) (t + JAN_1970);
	TVUTOTSF(clock_time.usecond, parseio->parse_dtime.parse_time.fp.l_uf);
#endif

	parseio->parse_dtime.parse_format       = format;

	return updatetimeinfo(parseio, clock_time.flags);
}

/*ARGSUSED*/
int
parse_timecode(
	parsectl_t *dct,
	parse_t    *parse
	)
{
	dct->parsegettc.parse_state  = parse->parse_lstate;
	dct->parsegettc.parse_format = parse->parse_lformat;
	/*
	 * move out current bad packet count
	 * user program is expected to sum these up
	 * this is not a problem, as "parse" module are
	 * exclusive open only
	 */
	dct->parsegettc.parse_badformat = parse->parse_badformat;
	parse->parse_badformat = 0;

	if (parse->parse_ldsize <= PARSE_TCMAX)
	{
		dct->parsegettc.parse_count = parse->parse_ldsize;
		memcpy(dct->parsegettc.parse_buffer, parse->parse_ldata, dct->parsegettc.parse_count);
		return 1;
	}
	else
	{
		return 0;
	}
}


/*ARGSUSED*/
int
parse_setfmt(
	parsectl_t *dct,
	parse_t    *parse
	)
{
	if (dct->parseformat.parse_count <= PARSE_TCMAX)
	{
		if (dct->parseformat.parse_count)
		{
			register unsigned short i;

			for (i = 0; i < nformats; i++)
			{
				if (!Strcmp(dct->parseformat.parse_buffer, clockformats[i]->name))
				{
					if (parse->parse_pdata)
						FREE(parse->parse_pdata, parse->parse_plen);
					parse->parse_pdata = 0;

					parse->parse_plen = clockformats[i]->plen;

					if (parse->parse_plen)
					{
						parse->parse_pdata = MALLOC(parse->parse_plen);
						if (!parse->parse_pdata)
						{
							parseprintf(DD_PARSE, ("set format failed: malloc for private data area failed\n"));
							return 0;
						}
						memset((char *)parse->parse_pdata, 0, parse->parse_plen);
					}

					if (parse->parse_data)
						FREE(parse->parse_data, (unsigned)(parse->parse_dsize * 2 + 2));
					parse->parse_ldata = parse->parse_data = 0;

					parse->parse_dsize = clockformats[i]->length;

					if (parse->parse_dsize)
					{
						parse->parse_data = (char*)MALLOC((unsigned)(parse->parse_dsize * 2 + 2));
						if (!parse->parse_data)
						{
							if (parse->parse_pdata)
								FREE(parse->parse_pdata, parse->parse_plen);
							parse->parse_pdata = 0;

							parseprintf(DD_PARSE, ("init failed: malloc for data area failed\n"));
							return 0;
						}
					}


					/*
					 * leave room for '\0'
					 */
					parse->parse_ldata     = parse->parse_data + parse->parse_dsize + 1;

					parse->parse_lformat  = i;

					return 1;
				}
			}
		}
	}
	return 0;
}

/*ARGSUSED*/
int
parse_getfmt(
	parsectl_t *dct,
	parse_t    *parse
	)
{
	if (dct->parseformat.parse_format < nformats &&
	    Strlen(clockformats[dct->parseformat.parse_format]->name) <= PARSE_TCMAX)
	{
		dct->parseformat.parse_count = (unsigned short) (Strlen(clockformats[dct->parseformat.parse_format]->name) + 1);
		memcpy(dct->parseformat.parse_buffer, clockformats[dct->parseformat.parse_format]->name, dct->parseformat.parse_count);
		return 1;
	}
	else
	{
		return 0;
	}
}

/*ARGSUSED*/
int
parse_setcs(
	parsectl_t *dct,
	parse_t    *parse
	)
{
	parse->parse_ioflags &= ~PARSE_IO_CSIZE;
	parse->parse_ioflags |= (int) (dct->parsesetcs.parse_cs & PARSE_IO_CSIZE);
	return 1;
}

#else /* not (REFCLOCK && CLOCK_PARSE) */
int parse_bs;
#endif /* not (REFCLOCK && CLOCK_PARSE) */

/*
 * History:
 *
 * parse.c,v
 * Revision 4.20  2005/08/06 17:39:40  kardel
 * cleanup size handling wrt/ to buffer boundaries
 *
 * Revision 4.19  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.18  2004/11/14 16:11:05  kardel
 * update Id tags
 *
 * Revision 4.17  2004/11/14 15:29:41  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 * Revision 4.14  1999/11/28 09:13:52  kardel
 * RECON_4_0_98F
 *
 * Revision 4.13  1999/02/28 11:50:20  kardel
 * (timepacket): removed unecessary code
 *
 * Revision 4.12  1999/02/21 12:17:44  kardel
 * 4.91f reconcilation
 *
 * Revision 4.11  1999/02/21 11:09:47  kardel
 * unified debug output
 *
 * Revision 4.10  1998/12/20 23:45:30  kardel
 * fix types and warnings
 *
 * Revision 4.9  1998/08/09 22:26:06  kardel
 * Trimble TSIP support
 *
 * Revision 4.8  1998/06/14 21:09:39  kardel
 * Sun acc cleanup
 *
 * Revision 4.7  1998/06/13 15:19:13  kardel
 * fix mem*() to b*() function macro emulation
 *
 * Revision 4.6  1998/06/13 13:24:13  kardel
 * printf fmt
 *
 * Revision 4.5  1998/06/13 13:01:10  kardel
 * printf fmt
 *
 * Revision 4.4  1998/06/13 12:12:10  kardel
 * bcopy/memcpy cleanup
 * fix SVSV name clash
 *
 * Revision 4.3  1998/06/12 15:22:30  kardel
 * fix prototypes
 *
 * Revision 4.2  1998/06/12 09:13:27  kardel
 * conditional compile macros fixed
 * printf prototype
 *
 * Revision 4.1  1998/05/24 09:39:55  kardel
 * implementation of the new IO handling model
 *
 * Revision 4.0  1998/04/10 19:45:36  kardel
 * Start 4.0 release version numbering
 *
 * from V3 3.46 log info deleted 1998/04/11 kardel
 */
