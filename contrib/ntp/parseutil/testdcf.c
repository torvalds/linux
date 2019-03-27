/*
 * /src/NTP/ntp4-dev/parseutil/testdcf.c,v 4.10 2005/08/06 14:18:43 kardel RELEASE_20050806_A
 *
 * testdcf.c,v 4.10 2005/08/06 14:18:43 kardel RELEASE_20050806_A
 *
 * simple DCF77 100/200ms pulse test program (via 50Baud serial line)
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

#include <config.h>
#include "ntp_stdlib.h"

#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>

/*
 * state flags
 */
#define DCFB_ANNOUNCE   0x0001 /* switch time zone warning (DST switch) */
#define DCFB_DST        0x0002 /* DST in effect */
#define DCFB_LEAP       0x0004 /* LEAP warning (1 hour prior to occurrence) */
#define DCFB_CALLBIT    0x0008 /* "call bit" used to signalize irregularities in the control facilities */

struct clocktime		/* clock time broken up from time code */
{
	long wday;
	long day;
	long month;
	long year;
	long hour;
	long minute;
	long second;
	long usecond;
	long utcoffset;	/* in minutes */
	long flags;		/* current clock status */
};

typedef struct clocktime clocktime_t;

static char type(unsigned int);

#define TIMES10(_X_) (((_X_) << 3) + ((_X_) << 1))

/*
 * parser related return/error codes
 */
#define CVT_MASK	0x0000000F /* conversion exit code */
#define   CVT_NONE	0x00000001 /* format not applicable */
#define   CVT_FAIL	0x00000002 /* conversion failed - error code returned */
#define   CVT_OK	0x00000004 /* conversion succeeded */
#define CVT_BADFMT	0x00000010 /* general format error - (unparsable) */

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

static char revision[] = "4.10";

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

static unsigned long
ext_bf(
	register unsigned char *buf,
	register int   idx
	)
{
	register unsigned long sum = 0;
	register int i, first;

	first = rawdcfcode[idx].offset;

	for (i = rawdcfcode[idx+1].offset - 1; i >= first; i--)
	{
		sum <<= 1;
		sum |= (buf[i] != '-');
	}
	return sum;
}

static unsigned
pcheck(
	register unsigned char *buf,
	register int   idx
	)
{
	register int i,last;
	register unsigned psum = 1;

	last = partab[idx+1].offset;

	for (i = partab[idx].offset; i < last; i++)
	    psum ^= (buf[i] != '-');

	return psum;
}

static unsigned long
convert_rawdcf(
	register unsigned char   *buffer,
	register int              size,
	register clocktime_t     *clock_time
	)
{
	if (size < 57)
	{
		printf("%-30s", "*** INCOMPLETE");
		return CVT_NONE;
	}

	/*
	 * check Start and Parity bits
	 */
	if ((ext_bf(buffer, DCF_S) == 1) &&
	    pcheck(buffer, DCF_P_P1) &&
	    pcheck(buffer, DCF_P_P2) &&
	    pcheck(buffer, DCF_P_P3))
	{
		/*
		 * buffer OK
		 */

		clock_time->flags  = 0;
		clock_time->usecond= 0;
		clock_time->second = 0;
		clock_time->minute = ext_bf(buffer, DCF_M10);
		clock_time->minute = TIMES10(clock_time->minute) + ext_bf(buffer, DCF_M1);
		clock_time->hour   = ext_bf(buffer, DCF_H10);
		clock_time->hour   = TIMES10(clock_time->hour) + ext_bf(buffer, DCF_H1);
		clock_time->day    = ext_bf(buffer, DCF_D10);
		clock_time->day    = TIMES10(clock_time->day) + ext_bf(buffer, DCF_D1);
		clock_time->month  = ext_bf(buffer, DCF_MO0);
		clock_time->month  = TIMES10(clock_time->month) + ext_bf(buffer, DCF_MO);
		clock_time->year   = ext_bf(buffer, DCF_Y10);
		clock_time->year   = TIMES10(clock_time->year) + ext_bf(buffer, DCF_Y1);
		clock_time->wday   = ext_bf(buffer, DCF_DW);

		switch (ext_bf(buffer, DCF_Z))
		{
		    case DCF_Z_MET:
			clock_time->utcoffset = -60;
			break;

		    case DCF_Z_MED:
			clock_time->flags     |= DCFB_DST;
			clock_time->utcoffset  = -120;
			break;

		    default:
			printf("%-30s", "*** BAD TIME ZONE");
			return CVT_FAIL|CVT_BADFMT;
		}

		if (ext_bf(buffer, DCF_A1))
		    clock_time->flags |= DCFB_ANNOUNCE;

		if (ext_bf(buffer, DCF_A2))
		    clock_time->flags |= DCFB_LEAP;

		if (ext_bf(buffer, DCF_R))
		    clock_time->flags |= DCFB_CALLBIT;

		return CVT_OK;
	}
	else
	{
		/*
		 * bad format - not for us
		 */
		printf("%-30s", "*** BAD FORMAT (invalid/parity)");
		return CVT_FAIL|CVT_BADFMT;
	}
}

static char
type(
	unsigned int c
	)
{
	c ^= 0xFF;
	return (c >= 0xF);
}

static const char *wday[8] =
{
	"??",
	"Mo",
	"Tu",
	"We",
	"Th",
	"Fr",
	"Sa",
	"Su"
};

static char pat[] = "-\\|/";

#define LINES (24-2)	/* error lines after which the two headlines are repeated */

int
main(
	int argc,
	char *argv[]
	)
{
	if ((argc != 2) && (argc != 3))
	{
		fprintf(stderr, "usage: %s [-f|-t|-ft|-tf] <device>\n", argv[0]);
		exit(1);
	}
	else
	{
		unsigned char c;
		char *file;
		int fd;
		int offset = 15;
		int trace = 0;
		int errs = LINES+1;

		/*
		 * SIMPLE(!) argument "parser"
		 */
		if (argc == 3)
		{
			if (strcmp(argv[1], "-f") == 0)
			    offset = 0;
			if (strcmp(argv[1], "-t") == 0)
			    trace = 1;
			if ((strcmp(argv[1], "-ft") == 0) ||
			    (strcmp(argv[1], "-tf") == 0))
			{
				offset = 0;
				trace = 1;
			}
			file = argv[2];
		}
		else
		{
			file = argv[1];
		}

		fd = open(file, O_RDONLY);
		if (fd == -1)
		{
			perror(file);
			exit(1);
		}
		else
		{
			int i;
#ifdef TIOCM_RTS
			int on = TIOCM_RTS;
#endif
			struct timeval t, tt, tlast;
			char buf[61];
			clocktime_t clock_time;
			struct termios term;
			int rtc = CVT_NONE;

			if (tcgetattr(fd,  &term) == -1)
			{
				perror("tcgetattr");
				exit(1);
			}

			memset(term.c_cc, 0, sizeof(term.c_cc));
			term.c_cc[VMIN] = 1;
#ifdef NO_PARENB_IGNPAR /* Was: defined(SYS_IRIX4) || defined (SYS_IRIX5) */
			/* somehow doesn't grok PARENB & IGNPAR (mj) */
			term.c_cflag = CS8|CREAD|CLOCAL;
#else
			term.c_cflag = CS8|CREAD|CLOCAL|PARENB;
#endif
			term.c_iflag = IGNPAR;
			term.c_oflag = 0;
			term.c_lflag = 0;

			cfsetispeed(&term, B50);
			cfsetospeed(&term, B50);

			if (tcsetattr(fd, TCSANOW, &term) == -1)
			{
				perror("tcsetattr");
				exit(1);
			}

#ifdef I_POP
			while (ioctl(fd, I_POP, 0) == 0)
			    ;
#endif
#if defined(TIOCMBIC) && defined(TIOCM_RTS)
			if (ioctl(fd, TIOCMBIC, (caddr_t)&on) == -1)
			{
				perror("TIOCM_RTS");
			}
#endif

			printf("  DCF77 monitor %s - Copyright (C) 1993-2005, Frank Kardel\n\n", revision);

			clock_time.hour = 0;
			clock_time.minute = 0;
			clock_time.day = 0;
			clock_time.wday = 0;
			clock_time.month = 0;
			clock_time.year = 0;
			clock_time.flags = 0;
			buf[60] = '\0';
			for ( i = 0; i < 60; i++)
			    buf[i] = '.';

			gettimeofday(&tlast, 0L);
			i = 0;
			while (read(fd, &c, 1) == 1)
			{
				gettimeofday(&t, 0L);
				tt = t;
				t.tv_sec -= tlast.tv_sec;
				t.tv_usec -= tlast.tv_usec;
				if (t.tv_usec < 0)
				{
					t.tv_usec += 1000000;
					t.tv_sec  -= 1;
				}

				if (errs > LINES)
				{
					printf("  %s", &"PTB private....RADMLSMin....PHour..PMDay..DayMonthYear....P\n"[offset]);
					printf("  %s", &"---------------RADMLS1248124P124812P1248121241248112481248P\n"[offset]);
					errs = 0;
				}

				if (t.tv_sec > 1 ||
				    (t.tv_sec == 1 &&
				     t.tv_usec > 500000))
				{
					printf("%c %.*s ", pat[i % (sizeof(pat)-1)], 59 - offset, &buf[offset]);

					if ((rtc = convert_rawdcf((unsigned char *)buf, i, &clock_time)) != CVT_OK)
					{
						printf("\n");
						clock_time.hour = 0;
						clock_time.minute = 0;
						clock_time.day = 0;
						clock_time.wday = 0;
						clock_time.month = 0;
						clock_time.year = 0;
						clock_time.flags = 0;
						errs++;
					}

					if (((c^0xFF)+1) & (c^0xFF))
					    buf[0] = '?';
					else
					    buf[0] = type(c) ? '#' : '-';

					for ( i = 1; i < 60; i++)
					    buf[i] = '.';

					i = 0;
				}
				else
				{
					if (((c^0xFF)+1) & (c^0xFF))
					    buf[i] = '?';
					else
					    buf[i] = type(c) ? '#' : '-';

					printf("%c %.*s ", pat[i % (sizeof(pat)-1)], 59 - offset, &buf[offset]);
				}

				if (rtc == CVT_OK)
				{
					printf("%s, %2d:%02d:%02d, %d.%02d.%02d, <%s%s%s%s>",
					       wday[clock_time.wday],
					       (int)clock_time.hour, (int)clock_time.minute, (int)i, (int)clock_time.day, (int)clock_time.month,
					       (int)clock_time.year,
					       (clock_time.flags & DCFB_CALLBIT) ? "R" : "_",
					       (clock_time.flags & DCFB_ANNOUNCE) ? "A" : "_",
					       (clock_time.flags & DCFB_DST) ? "D" : "_",
					       (clock_time.flags & DCFB_LEAP) ? "L" : "_"
					       );
					if (trace && (i == 0))
					{
						printf("\n");
						errs++;
					}
				}

				printf("\r");

				if (i < 60)
				{
					i++;
				}

				tlast = tt;

				fflush(stdout);
			}
			close(fd);
		}
	}
	return 0;
}

/*
 * History:
 *
 * testdcf.c,v
 * Revision 4.10  2005/08/06 14:18:43  kardel
 * cleanup warnings
 *
 * Revision 4.9  2005/08/06 14:14:38  kardel
 * document revision on startup
 *
 * Revision 4.8  2005/08/06 14:10:08  kardel
 * fix setting of baud rate
 *
 * Revision 4.7  2005/04/16 17:32:10  kardel
 * update copyright
 *
 * Revision 4.6  2004/11/14 15:29:42  kardel
 * support PPSAPI, upgrade Copyright to Berkeley style
 *
 */
