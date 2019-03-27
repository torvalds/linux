/*
 * tg.c generate WWV or IRIG signals for test
 */
/*
 * This program can generate audio signals that simulate the WWV/H
 * broadcast timecode. Alternatively, it can generate the IRIG-B
 * timecode commonly used to synchronize laboratory equipment. It is
 * intended to test the WWV/H driver (refclock_wwv.c) and the IRIG
 * driver (refclock_irig.c) in the NTP driver collection.
 *
 * Besides testing the drivers themselves, this program can be used to
 * synchronize remote machines over audio transmission lines or program
 * feeds. The program reads the time on the local machine and sets the
 * initial epoch of the signal generator within one millisecond.
 * Alernatively, the initial epoch can be set to an arbitrary time. This
 * is useful when searching for bugs and testing for correct response to
 * a leap second in UTC. Note however, the ultimate accuracy is limited
 * by the intrinsic frequency error of the codec sample clock, which can
 # reach well over 100 PPM.
 *
 * The default is to route generated signals to the line output
 * jack; the s option on the command line routes these signals to the
 * internal speaker as well. The v option controls the speaker volume
 * over the range 0-255. The signal generator by default uses WWV
 * format; the h option switches to WWVH format and the i option
 * switches to IRIG-B format.
 *
 * Once started the program runs continuously. The default initial epoch
 * for the signal generator is read from the computer system clock when
 * the program starts. The y option specifies an alternate epoch using a
 * string yydddhhmmss, where yy is the year of century, ddd the day of
 * year, hh the hour of day and mm the minute of hour. For instance,
 * 1946Z on 1 January 2006 is 060011946. The l option lights the leap
 * warning bit in the WWV/H timecode, so is handy to check for correct
 * behavior at the next leap second epoch. The remaining options are
 * specified below under the Parse Options heading. Most of these are
 * for testing.
 *
 * During operation the program displays the WWV/H timecode (9 digits)
 * or IRIG timecode (20 digits) as each new string is constructed. The
 * display is followed by the BCD binary bits as transmitted. Note that
 * the transmissionorder is low-order first as the frame is processed
 * left to right. For WWV/H The leap warning L preceeds the first bit.
 * For IRIG the on-time marker M preceeds the first (units) bit, so its
 * code is delayed one bit and the next digit (tens) needs only three
 * bits.
 *
 * The program has been tested with the Sun Blade 1500 running Solaris
 * 10, but not yet with other machines. It uses no special features and
 * should be readily portable to other hardware and operating systems.
 *
 * $Log: tg.c,v $
 * Revision 1.28  2007/02/12 23:57:45  dmw
 * v0.23 2007-02-12 dmw:
 * - Changed statistics to include calculated error
 *   of frequency, based on number of added or removed
 *   cycles over time.
 *
 * Revision 1.27  2007/02/09 02:28:59  dmw
 * v0.22 2007-02-08 dmw:
 * - Changed default for rate correction to "enabled", "-j" switch now disables.
 * - Adjusted help message accordingly.
 * - Added "2007" to modifications note at end of help message.
 *
 * Revision 1.26  2007/02/08 03:36:17  dmw
 * v0.21 2007-02-07 dmw:
 * - adjusted strings for shorten and lengthen to make
 *   fit on smaller screen.
 *
 * Revision 1.25  2007/02/01 06:08:09  dmw
 * v0.20 2007-02-01 dmw:
 * - Added periodic display of running time along with legend on IRIG-B, allows tracking how
 *   close IRIG output is to actual clock time.
 *
 * Revision 1.24  2007/01/31 19:24:11  dmw
 * v0.19 2007-01-31 dmw:
 * - Added tracking of how many seconds have been adjusted,
 *   how many cycles added (actually in milliseconds), how
 *   many cycles removed, print periodically if verbose is
 *   active.
 * - Corrected lack of lengthen or shorten of minute & hour
 *   pulses for WWV format.
 *
 * Revision 1.23  2007/01/13 07:09:12  dmw
 * v0.18 2007-01-13 dmw:
 * - added -k option, which allows force of long or short
 *   cycles, to test against IRIG-B decoder.
 *
 * Revision 1.22  2007/01/08 16:27:23  dmw
 * v0.17 2007-01-08 dmw:
 * - Changed -j option to **enable** rate correction, not disable.
 *
 * Revision 1.21  2007/01/08 06:22:36  dmw
 * v0.17 2007-01-08 dmw:
 * - Run stability check versus ongoing system clock (assume NTP correction)
 *   and adjust time code rate to try to correct, if gets too far out of sync.
 *   Disable this algorithm with -j option.
 *
 * Revision 1.20  2006/12/19 04:59:04  dmw
 * v0.16 2006-12-18 dmw
 * - Corrected print of setting of output frequency, always
 *   showed 8000 samples/sec, now as specified on command line.
 * - Modified to reflect new employer Norscan.
 *
 * Revision 1.19  2006/12/19 03:45:38  dmw
 * v0.15 2006-12-18 dmw:
 * - Added count of number of seconds to output then exit,
 *   default zero for forever.
 *
 * Revision 1.18  2006/12/18 05:43:36  dmw
 * v0.14 2006-12-17 dmw:
 * - Corrected WWV(H) signal to leave "tick" sound off of 29th and 59th second of minute.
 * - Adjusted verbose output format for WWV(H).
 *
 * Revision 1.17  2006/12/18 02:31:33  dmw
 * v0.13 2006-12-17 dmw:
 * - Put SPARC code back in, hopefully will work, but I don't have
 *   a SPARC to try it on...
 * - Reworked Verbose mode, different flag to initiate (x not v)
 *   and actually implement turn off of verbosity when this flag used.
 * - Re-claimed v flag for output level.
 * - Note that you must define OSS_MODS to get OSS to compile,
 *   otherwise will expect to compile using old SPARC options, as
 *   it used to be.
 *
 * Revision 1.16  2006/10/26 19:08:43  dmw
 * v0.12 2006-10-26 dmw:
 * - Reversed output binary dump for IRIG, makes it easier to read the numbers.
 *
 * Revision 1.15  2006/10/24 15:57:09  dmw
 * v0.11 2006-10-24 dmw:
 * - another tweak.
 *
 * Revision 1.14  2006/10/24 15:55:53  dmw
 * v0.11 2006-10-24 dmw:
 * - Curses a fix to the fix to the fix of the usaeg.
 *
 * Revision 1.13  2006/10/24 15:53:25  dmw
 * v0.11 (still) 2006-10-24 dmw:
 * - Messed with usage message that's all.
 *
 * Revision 1.12  2006/10/24 15:50:05  dmw
 * v0.11 2006-10-24 dmw:
 * - oops, needed to note "hours" in usage of that offset.
 *
 * Revision 1.11  2006/10/24 15:49:09  dmw
 * v0.11 2006-10-24 dmw:
 * - Added ability to offset actual time sent, from the UTC time
 *   as per the computer.
 *
 * Revision 1.10  2006/10/24 03:25:55  dmw
 * v0.10 2006-10-23 dmw:
 * - Corrected polarity of correction of offset when going into or out of DST.
 * - Ensure that zero offset is always positive (pet peeve).
 *
 * Revision 1.9  2006/10/24 00:00:35  dmw
 * v0.9 2006-10-23 dmw:
 * - Shift time offset when DST in or out.
 *
 * Revision 1.8  2006/10/23 23:49:28  dmw
 * v0.8 2006-10-23 dmw:
 * - made offset of zero default positive.
 *
 * Revision 1.7  2006/10/23 23:44:13  dmw
 * v0.7 2006-10-23 dmw:
 * - Added unmodulated and inverted unmodulated output.
 *
 * Revision 1.6  2006/10/23 18:10:37  dmw
 * v0.6 2006-10-23 dmw:
 * - Cleaned up usage message.
 * - Require at least one option, or prints usage message and exits.
 *
 * Revision 1.5  2006/10/23 16:58:10  dmw
 * v0.5 2006-10-23 dmw:
 * - Finally added a usage message.
 * - Added leap second pending and DST change pending into IEEE 1344.
 * - Default code type is now IRIG-B with IEEE 1344.
 *
 * Revision 1.4  2006/10/23 03:27:25  dmw
 * v0.4 2006-10-22 dmw:
 * - Added leap second addition and deletion.
 * - Added DST changing forward and backward.
 * - Changed date specification to more conventional year, month, and day of month
 *   (rather than day of year).
 *
 * Revision 1.3  2006/10/22 21:04:12  dmw
 * v0.2 2006-10-22 dmw:
 * - Corrected format of legend line.
 *
 * Revision 1.2  2006/10/22 21:01:07  dmw
 * v0.1 2006-10-22 dmw:
 * - Added some more verbose output (as is my style)
 * - Corrected frame format - there were markers in the
 *   middle of frames, now correctly as "zero" bits.
 * - Added header line to show fields of output.
 * - Added straight binary seconds, were not implemented
 *   before.
 * - Added IEEE 1344 with parity.
 *
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef  HAVE_CONFIG_H
#include "config.h"
#undef VERSION		/* avoid conflict below */
#endif

#ifdef  HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#else
# ifdef HAVE_SYS_AUDIOIO_H
# include <sys/audioio.h>
# else
# include <sys/audio.h>
# endif
#endif

#include "ntp_stdlib.h"	/* for strlcat(), strlcpy() */

#include <math.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define VERSION		(0)
#define	ISSUE		(23)
#define	ISSUE_DATE	"2007-02-12"

#define	SECOND	(8000)			/* one second of 125-us samples */
#define BUFLNG	(400)			/* buffer size */
#define	DEVICE	"/dev/audio"	/* default audio device */
#define	WWV		(0)				/* WWV encoder */
#define	IRIG	(1)				/* IRIG-B encoder */
#define	OFF		(0)				/* zero amplitude */
#define	LOW		(1)				/* low amplitude */
#define	HIGH	(2)				/* high amplitude */
#define	DATA0	(200)			/* WWV/H 0 pulse */
#define	DATA1	(500)			/* WWV/H 1 pulse */
#define PI		(800)			/* WWV/H PI pulse */
#define	M2		(2)				/* IRIG 0 pulse */
#define	M5		(5)				/* IRIG 1 pulse */
#define	M8		(8)				/* IRIG PI pulse */

#define	NUL		(0)

#define	SECONDS_PER_MINUTE	(60)
#define SECONDS_PER_HOUR	(3600)

#define	OUTPUT_DATA_STRING_LENGTH	(200)

/* Attempt at unmodulated - "high" */
int u6000[] = {
	247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/*  0- 9 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/* 10-19 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/* 20-29 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/* 30-39 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/* 40-49 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 	/* 50-59 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247,	/* 60-69 */
    247, 247, 247, 247, 247, 247, 247, 247, 247, 247}; 	/* 70-79 */

/* Attempt at unmodulated - "low" */
int u3000[] = {
	119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/*  0- 9 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/* 10-19 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/* 20-29 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/* 30-39 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/* 40-49 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 	/* 50-59 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119,	/* 60-69 */
    119, 119, 119, 119, 119, 119, 119, 119, 119, 119}; 	/* 70-79 */

/*
 * Companded sine table amplitude 3000 units
 */
int c3000[] = {1, 48, 63, 70, 78, 82, 85, 89, 92, 94,	/* 0-9 */
     96,  98,  99, 100, 101, 101, 102, 103, 103, 103,	/* 10-19 */
    103, 103, 103, 103, 102, 101, 101, 100,  99,  98,	/* 20-29 */
     96,  94,  92,  89,  85,  82,  78,  70,  63,  48,	/* 30-39 */
    129, 176, 191, 198, 206, 210, 213, 217, 220, 222,	/* 40-49 */
    224, 226, 227, 228, 229, 229, 230, 231, 231, 231, 	/* 50-59 */
    231, 231, 231, 231, 230, 229, 229, 228, 227, 226,	/* 60-69 */
    224, 222, 220, 217, 213, 210, 206, 198, 191, 176}; 	/* 70-79 */
/*
 * Companded sine table amplitude 6000 units
 */
int c6000[] = {1, 63, 78, 86, 93, 98, 101, 104, 107, 110, /* 0-9 */
    112, 113, 115, 116, 117, 117, 118, 118, 119, 119,	/* 10-19 */
    119, 119, 119, 118, 118, 117, 117, 116, 115, 113,	/* 20-29 */
    112, 110, 107, 104, 101,  98,  93,  86,  78,  63,	/* 30-39 */
    129, 191, 206, 214, 221, 226, 229, 232, 235, 238,	/* 40-49 */
    240, 241, 243, 244, 245, 245, 246, 246, 247, 247, 	/* 50-59 */
    247, 247, 247, 246, 246, 245, 245, 244, 243, 241,	/* 60-69 */
    240, 238, 235, 232, 229, 226, 221, 214, 206, 191}; 	/* 70-79 */

/*
 * Decoder operations at the end of each second are driven by a state
 * machine. The transition matrix consists of a dispatch table indexed
 * by second number. Each entry in the table contains a case switch
 * number and argument.
 */
struct progx {
	int sw;			/* case switch number */
	int arg;		/* argument */
};

/*
 * Case switch numbers
 */
#define DATA	(0)		/* send data (0, 1, PI) */
#define COEF	(1)		/* send BCD bit */
#define	DEC		(2)		/* decrement to next digit and send PI */
#define	MIN		(3)		/* minute pulse */
#define	LEAP	(4)		/* leap warning */
#define	DUT1	(5)		/* DUT1 bits */
#define	DST1	(6)		/* DST1 bit */
#define	DST2	(7)		/* DST2 bit */
#define DECZ	(8)		/* decrement to next digit and send zero */
#define DECC	(9)		/* decrement to next digit and send bit */
#define NODEC	(10)	/* no decerement to next digit, send PI */
#define DECX	(11)	/* decrement to next digit, send PI, but no tick */
#define DATAX	(12)	/* send data (0, 1, PI), but no tick */

/*
 * WWV/H format (100-Hz, 9 digits, 1 m frame)
 */
struct progx progx[] = {
	{MIN,	800},		/* 0 minute sync pulse */
	{DATA,	DATA0},		/* 1 */
	{DST2,	0},		/* 2 DST2 */
	{LEAP,	0},		/* 3 leap warning */
	{COEF,	1},		/* 4 1 year units */
	{COEF,	2},		/* 5 2 */
	{COEF,	4},		/* 6 4 */
	{COEF,	8},		/* 7 8 */
	{DEC,	DATA0},		/* 8 */
	{DATA,	PI},		/* 9 p1 */
	{COEF,	1},		/* 10 1 minute units */
	{COEF,	2},		/* 11 2 */
	{COEF,	4},		/* 12 4 */
	{COEF,	8},		/* 13 8 */
	{DEC,	DATA0},		/* 14 */
	{COEF,	1},		/* 15 10 minute tens */
	{COEF,	2},		/* 16 20 */
	{COEF,	4},		/* 17 40 */
	{COEF,	8},		/* 18 80 (not used) */
	{DEC,	PI},		/* 19 p2 */
	{COEF,	1},		/* 20 1 hour units */
	{COEF,	2},		/* 21 2 */
	{COEF,	4},		/* 22 4 */
	{COEF,	8},		/* 23 8 */
	{DEC,	DATA0},		/* 24 */
	{COEF,	1},		/* 25 10 hour tens */
	{COEF,	2},		/* 26 20 */
	{COEF,	4},		/* 27 40 (not used) */
	{COEF,	8},		/* 28 80 (not used) */
	{DECX,	PI},		/* 29 p3 */
	{COEF,	1},		/* 30 1 day units */
	{COEF,	2},		/* 31 2 */
	{COEF,	4},		/* 32 4 */
	{COEF,	8},		/* 33 8 */
	{DEC,	DATA0},		/* 34 not used */
	{COEF,	1},		/* 35 10 day tens */
	{COEF,	2},		/* 36 20 */
	{COEF,	4},		/* 37 40 */
	{COEF,	8},		/* 38 80 */
	{DEC,	PI},		/* 39 p4 */
	{COEF,	1},		/* 40 100 day hundreds */
	{COEF,	2},		/* 41 200 */
	{COEF,	4},		/* 42 400 (not used) */
	{COEF,	8},		/* 43 800 (not used) */
	{DEC,	DATA0},		/* 44 */
	{DATA,	DATA0},		/* 45 */
	{DATA,	DATA0},		/* 46 */
	{DATA,	DATA0},		/* 47 */
	{DATA,	DATA0},		/* 48 */
	{DATA,	PI},		/* 49 p5 */
	{DUT1,	8},		/* 50 DUT1 sign */
	{COEF,	1},		/* 51 10 year tens */
	{COEF,	2},		/* 52 20 */
	{COEF,	4},		/* 53 40 */
	{COEF,	8},		/* 54 80 */
	{DST1,	0},		/* 55 DST1 */
	{DUT1,	1},		/* 56 0.1 DUT1 fraction */
	{DUT1,	2},		/* 57 0.2 */
	{DUT1,	4},		/* 58 0.4 */
	{DATAX,	PI},		/* 59 p6 */
	{DATA,	DATA0},		/* 60 leap */
};

/*
 * IRIG format frames (1000 Hz, 1 second for 10 frames of data)
 */

/*
 * IRIG format frame 10 - MS straight binary seconds
 */
struct progx progu[] = {
	{COEF,	2},		/* 0 0x0 0200 seconds */
	{COEF,	4},		/* 1 0x0 0400 */
	{COEF,	8},		/* 2 0x0 0800 */
	{DECC,	1},		/* 3 0x0 1000 */
	{COEF,	2},		/* 4 0x0 2000 */
	{COEF,	4},		/* 6 0x0 4000 */
	{COEF,	8},		/* 7 0x0 8000 */
	{DECC,	1},		/* 8 0x1 0000 */
	{COEF,  2},     /* 9 0x2 0000 - but only 86,401 / 0x1 5181 seconds in a day, so always zero */
	{NODEC,	M8},	/* 9 PI */
};

/*
 * IRIG format frame 8 - MS control functions
 */
struct progx progv[] = {
	{COEF,	2},		/*  0 CF # 19 */
	{COEF,	4},		/*  1 CF # 20 */
	{COEF,	8},		/*  2 CF # 21 */
	{DECC,	1},		/*  3 CF # 22 */
	{COEF,	2},		/*  4 CF # 23 */
	{COEF,	4},		/*  6 CF # 24 */
	{COEF,	8},		/*  7 CF # 25 */
	{DECC,	1},		/*  8 CF # 26 */
	{COEF,  2},		/*  9 CF # 27 */
	{DEC,	M8},	/* 10 PI */
};

/*
 * IRIG format frames 7 & 9 - LS control functions & LS straight binary seconds
 */
struct progx progw[] = {
	{COEF,	1},		/*  0  CF # 10, 0x0 0001 seconds */
	{COEF,	2},		/*  1  CF # 11, 0x0 0002 */
	{COEF,	4},		/*  2  CF # 12, 0x0 0004 */
	{COEF,	8},		/*  3  CF # 13, 0x0 0008 */
	{DECC,	1},		/*  4  CF # 14, 0x0 0010 */
	{COEF,	2},		/*  6  CF # 15, 0x0 0020 */
	{COEF,	4},		/*  7  CF # 16, 0x0 0040 */
	{COEF,	8},		/*  8  CF # 17, 0x0 0080 */
	{DECC,  1},		/*  9  CF # 18, 0x0 0100 */
	{NODEC,	M8},	/* 10  PI */
};

/*
 * IRIG format frames 2 to 6 - minutes, hours, days, hundreds days, 2 digit years (also called control functions bits 1-9)
 */
struct progx progy[] = {
	{COEF,	1},		/* 0 1 units, CF # 1 */
	{COEF,	2},		/* 1 2 units, CF # 2 */
	{COEF,	4},		/* 2 4 units, CF # 3 */
	{COEF,	8},		/* 3 8 units, CF # 4 */
	{DECZ,	M2},	/* 4 zero bit, CF # 5 / unused, default zero in years */
	{COEF,	1},		/* 5 10 tens, CF # 6 */
	{COEF,	2},		/* 6 20 tens, CF # 7*/
	{COEF,	4},		/* 7 40 tens, CF # 8*/
	{COEF,	8},		/* 8 80 tens, CF # 9*/
	{DEC,	M8},	/* 9 PI */
};

/*
 * IRIG format first frame, frame 1 - seconds
 */
struct progx progz[] = {
	{MIN,	M8},	/* 0 PI (on-time marker for the second at zero cross of 1st cycle) */
	{COEF,	1},		/* 1 1 units */
	{COEF,	2},		/* 2 2 */
	{COEF,	4},		/* 3 4 */
	{COEF,	8},		/* 4 8 */
	{DECZ,	M2},	/* 5 zero bit */
	{COEF,	1},		/* 6 10 tens */
	{COEF,	2},		/* 7 20 */
	{COEF,	4},		/* 8 40 */
	{DEC,	M8},	/* 9 PI */
};

/* LeapState values. */
#define	LEAPSTATE_NORMAL			(0)
#define	LEAPSTATE_DELETING			(1)
#define	LEAPSTATE_INSERTING			(2)
#define	LEAPSTATE_ZERO_AFTER_INSERT	(3)


/*
 * Forward declarations
 */
void	WWV_Second(int, int);		/* send second */
void	WWV_SecondNoTick(int, int);	/* send second with no tick */
void	digit(int);		/* encode digit */
void	peep(int, int, int);	/* send cycles */
void	poop(int, int, int, int); /* Generate unmodulated from similar tables */
void	delay(int);		/* delay samples */
int		ConvertMonthDayToDayOfYear (int, int, int);	/* Calc day of year from year month & day */
void	Help (void);	/* Usage message */
void	ReverseString(char *);

/*
 * Extern declarations, don't know why not in headers
 */
//float	round ( float );

/*
 * Global variables
 */
char	buffer[BUFLNG];		/* output buffer */
int	bufcnt = 0;		/* buffer counter */
int	fd;			/* audio codec file descriptor */
int	tone = 1000;		/* WWV sync frequency */
int HourTone = 1500;	/* WWV hour on-time frequency */
int	encode = IRIG;		/* encoder select */
int	leap = 0;		/* leap indicator */
int	DstFlag = 0;		/* winter/summer time */
int	dut1 = 0;		/* DUT1 correction (sign, magnitude) */
int	utc = 0;		/* option epoch */
int IrigIncludeYear = FALSE;	/* Whether to send year in first control functions area, between P5 and P6. */
int IrigIncludeIeee = FALSE;	/* Whether to send IEEE 1344 control functions extensions between P6 and P8. */
int	StraightBinarySeconds = 0;
int	ControlFunctions = 0;
int	Debug = FALSE;
int Verbose = TRUE;
char	*CommandName;

#ifndef  HAVE_SYS_SOUNDCARD_H
int	level = AUDIO_MAX_GAIN / 8; /* output level */
int	port = AUDIO_LINE_OUT;	/* output port */
#endif

int		TotalSecondsCorrected = 0;
int		TotalCyclesAdded = 0;
int		TotalCyclesRemoved = 0;


/*
 * Main program
 */
int
main(
	int		argc,		/* command line options */
	char	**argv		/* poiniter to list of tokens */
	)
{
#ifndef  HAVE_SYS_SOUNDCARD_H
	audio_info_t info;	/* Sun audio structure */
	int	rval;           /* For IOCTL calls */
#endif

	struct	timeval	 TimeValue;				/* System clock at startup */
	time_t			 SecondsPartOfTime;		/* Sent to gmtime() for calculation of TimeStructure (can apply offset). */
	time_t			 BaseRealTime;			/* Base realtime so can determine seconds since starting. */
	time_t			 NowRealTime;			/* New realtime to can determine seconds as of now. */
	unsigned		 SecondsRunningRealTime;	/* Difference between NowRealTime and BaseRealTime. */
	unsigned		 SecondsRunningSimulationTime;	/* Time that the simulator has been running. */
	int				 SecondsRunningDifference;	/* Difference between what real time says we have been running */
												/* and what simulator says we have been running - will slowly  */
												/* change because of clock drift. */
	int				 ExpectedRunningDifference = 0;	/* Stable value that we've obtained from check at initial start-up.	*/
	unsigned		 StabilityCount;		/* Used to check stability of difference while starting */
#define	RUN_BEFORE_STABILITY_CHECK	(30)	// Must run this many seconds before even checking stability.
#define	MINIMUM_STABILITY_COUNT		(10)	// Number of consecutive differences that need to be within initial stability band to say we are stable.
#define	INITIAL_STABILITY_BAND		( 2)	// Determining initial stability for consecutive differences within +/- this value.
#define	RUNNING_STABILITY_BAND		( 5)	// When running, stability is defined as difference within +/- this value.

	struct	tm		*TimeStructure = NULL;	/* Structure returned by gmtime */
	char	device[200];	/* audio device */
	char	code[200];	/* timecode */
	int	temp;
	int	arg = 0;
	int	sw = 0;
	int	ptr = 0;

	int	Year;
	int	Month;
	int	DayOfMonth;
	int	Hour;
	int	Minute;
	int	Second = 0;
	int	DayOfYear;

	int	BitNumber;
#ifdef HAVE_SYS_SOUNDCARD_H
	int	AudioFormat;
	int	MonoStereo;     /* 0=mono, 1=stereo */
#define	MONO	(0)
#define	STEREO	(1)
	int	SampleRate;
	int	SampleRateDifference;
#endif
	int	SetSampleRate;
	char FormatCharacter = '3';		/* Default is IRIG-B with IEEE 1344 extensions */
	char AsciiValue;
	int	HexValue;
	int	OldPtr = 0;
	int FrameNumber = 0;

	/* Time offset for IEEE 1344 indication. */
	float TimeOffset = 0.0;
	int	OffsetSignBit = 0;
	int OffsetOnes = 0;
	int OffsetHalf = 0;

	int	TimeQuality = 0;	/* Time quality for IEEE 1344 indication. */
	char ParityString[200];	/* Partial output string, to calculate parity on. */
	int	ParitySum = 0;
	int	ParityValue;
	char *StringPointer;

	/* Flags to indicate requested leap second addition or deletion by command line option. */
	/* Should be mutually exclusive - generally ensured by code which interprets command line option. */
	int	InsertLeapSecond = FALSE;
	int	DeleteLeapSecond = FALSE;

	/* Date and time of requested leap second addition or deletion. */
	int	LeapYear					= 0;
	int LeapMonth					= 0;
	int	LeapDayOfMonth				= 0;
	int LeapHour					= 0;
	int	LeapMinute					= 0;
	int	LeapDayOfYear				= 0;

	/* State flag for the insertion and deletion of leap seconds, esp. deletion, */
	/* where the logic gets a bit tricky. */
	int	LeapState = LEAPSTATE_NORMAL;

	/* Flags for indication of leap second pending and leap secod polarity in IEEE 1344 */
	int	LeapSecondPending = FALSE;
	int	LeapSecondPolarity = FALSE;

	/* Date and time of requested switch into or out of DST by command line option. */
	int	DstSwitchYear				= 0;
	int DstSwitchMonth				= 0;
	int	DstSwitchDayOfMonth			= 0;
	int DstSwitchHour				= 0;
	int	DstSwitchMinute				= 0;
	int	DstSwitchDayOfYear			= 0;

	/* Indicate when we have been asked to switch into or out of DST by command line option. */
	int	DstSwitchFlag = FALSE;

	/* To allow predict for DstPendingFlag in IEEE 1344 */
	int	DstSwitchPendingYear		= 0;	/* Default value isn't valid, but I don't care. */
	int	DstSwitchPendingDayOfYear	= 0;
	int	DstSwitchPendingHour		= 0;
	int	DstSwitchPendingMinute		= 0;

	/* /Flag for indication of a DST switch pending in IEEE 1344 */
	int	DstPendingFlag = FALSE;

	/* Attempt at unmodulated */
	int	Unmodulated = FALSE;
	int UnmodulatedInverted = FALSE;

	/* Offset to actual time value sent. */
	float	UseOffsetHoursFloat;
	int		UseOffsetSecondsInt = 0;
	float	UseOffsetSecondsFloat;

	/* String to allow us to put out reversed data - so can read the binary numbers. */
	char	OutputDataString[OUTPUT_DATA_STRING_LENGTH];
	
	/* Number of seconds to send before exiting.  Default = 0 = forever. */
	int		SecondsToSend = 0;
	int		CountOfSecondsSent = 0;	/* Counter of seconds */
	
	/* Flags to indicate whether to add or remove a cycle for time adjustment. */
	int		AddCycle = FALSE;	 	// We are ahead, add cycle to slow down and get back in sync.
	int		RemoveCycle = FALSE;	// We are behind, remove cycle to slow down and get back in sync.
	int		RateCorrection;			// Aggregate flag for passing to subroutines.
	int		EnableRateCorrection = TRUE;
	
	float	RatioError;


	CommandName = argv[0];

	if	(argc < 1)
		{
		Help ();
		exit (-1);
		}

	/*
	 * Parse options
	 */
	strlcpy(device, DEVICE, sizeof(device));
	Year = 0;
	SetSampleRate = SECOND;
	
#if	HAVE_SYS_SOUNDCARD_H
	while ((temp = getopt(argc, argv, "a:b:c:df:g:hHi:jk:l:o:q:r:stu:xy:z?")) != -1) {
#else
	while ((temp = getopt(argc, argv, "a:b:c:df:g:hHi:jk:l:o:q:r:stu:v:xy:z?")) != -1) {
#endif
		switch (temp) {

		case 'a':	/* specify audio device (/dev/audio) */
			strlcpy(device, optarg, sizeof(device));
			break;

		case 'b':	/* Remove (delete) a leap second at the end of the specified minute. */
			sscanf(optarg, "%2d%2d%2d%2d%2d", &LeapYear, &LeapMonth, &LeapDayOfMonth,
			    &LeapHour, &LeapMinute);
			InsertLeapSecond = FALSE;
			DeleteLeapSecond = TRUE;
			break;
			
		case 'c':	/* specify number of seconds to send output for before exiting, 0 = forever */
			sscanf(optarg, "%d", &SecondsToSend);
			break;

		case 'd':	/* set DST for summer (WWV/H only) / start with DST active (IRIG) */
			DstFlag++;
			break;

		case 'f':	/* select format: i=IRIG-98 (default) 2=IRIG-2004 3-IRIG+IEEE-1344 w=WWV(H) */
			sscanf(optarg, "%c", &FormatCharacter);
			break;

		case 'g':	/* Date and time to switch back into / out of DST active. */
			sscanf(optarg, "%2d%2d%2d%2d%2d", &DstSwitchYear, &DstSwitchMonth, &DstSwitchDayOfMonth,
			    &DstSwitchHour, &DstSwitchMinute);
			DstSwitchFlag = TRUE;
			break;

		case 'h':
		case 'H':
		case '?':
			Help ();
			exit(-1);
			break;

		case 'i':	/* Insert (add) a leap second at the end of the specified minute. */
			sscanf(optarg, "%2d%2d%2d%2d%2d", &LeapYear, &LeapMonth, &LeapDayOfMonth,
			    &LeapHour, &LeapMinute);
			InsertLeapSecond = TRUE;
			DeleteLeapSecond = FALSE;
			break;
			
		case 'j':
			EnableRateCorrection = FALSE;
			break;

		case 'k':
			sscanf (optarg, "%d", &RateCorrection);
			EnableRateCorrection = FALSE;
			if  (RateCorrection < 0)
				{
				RemoveCycle = TRUE;
				AddCycle = FALSE;
				
				if  (Verbose)
					printf ("\n> Forcing rate correction removal of cycle...\n");
				}
			else
				{
				if  (RateCorrection > 0)
					{
					RemoveCycle = FALSE;
					AddCycle = TRUE;
				
					if  (Verbose)
						printf ("\n> Forcing rate correction addition of cycle...\n");
					}
				}
			break;

		case 'l':	/* use time offset from UTC */
			sscanf(optarg, "%f", &UseOffsetHoursFloat);
			UseOffsetSecondsFloat = UseOffsetHoursFloat * (float) SECONDS_PER_HOUR;
			UseOffsetSecondsInt = (int) (UseOffsetSecondsFloat + 0.5);
			break;

		case 'o':	/* Set IEEE 1344 time offset in hours - positive or negative, to the half hour */
			sscanf(optarg, "%f", &TimeOffset);
			if  (TimeOffset >= -0.2)
				{
				OffsetSignBit = 0;

				if  (TimeOffset > 0)
					{
					OffsetOnes    = TimeOffset;

					if  ( (TimeOffset - floor(TimeOffset)) >= 0.4)
						OffsetHalf = 1;
					else
						OffsetHalf = 0;
					}
				else
					{
					OffsetOnes    = 0;
					OffsetHalf    = 0;
					}
				}
			else
				{
				OffsetSignBit = 1;
				OffsetOnes    = -TimeOffset;

				if  ( (ceil(TimeOffset) - TimeOffset) >= 0.4)
					OffsetHalf = 1;
				else
					OffsetHalf = 0;
				}

			/*printf ("\nGot TimeOffset = %3.1f, OffsetSignBit = %d, OffsetOnes = %d, OffsetHalf = %d...\n",
					TimeOffset, OffsetSignBit, OffsetOnes, OffsetHalf);
			*/
			break;

		case 'q':	/* Hex quality code 0 to 0x0F - 0 = maximum, 0x0F = no lock */
			sscanf(optarg, "%x", &TimeQuality);
			TimeQuality &= 0x0F;
			/*printf ("\nGot TimeQuality = 0x%1X...\n", TimeQuality);
			*/
			break;

		case 'r':	/* sample rate (nominally 8000, integer close to 8000 I hope) */
			sscanf(optarg, "%d", &SetSampleRate);
			break;

		case 's':	/* set leap warning bit (WWV/H only) */
			leap++;
			break;

		case 't':	/* select WWVH sync frequency */
			tone = 1200;
			break;

		case 'u':	/* set DUT1 offset (-7 to +7) */
			sscanf(optarg, "%d", &dut1);
			if (dut1 < 0)
				dut1 = abs(dut1);
			else
				dut1 |= 0x8;
			break;

#ifndef  HAVE_SYS_SOUNDCARD_H
		case 'v':	/* set output level (0-255) */
			sscanf(optarg, "%d", &level);
			break;
#endif

		case 'x':	/* Turn off verbose output. */
			Verbose = FALSE;
			break;

		case 'y':	/* Set initial date and time */
			sscanf(optarg, "%2d%2d%2d%2d%2d%2d", &Year, &Month, &DayOfMonth,
			    &Hour, &Minute, &Second);
			utc++;
			break;

		case 'z':	/* Turn on Debug output (also turns on Verbose below) */
			Debug = TRUE;
			break;

		default:
			printf("Invalid option \"%c\", aborting...\n", temp);
			exit (-1);
			break;
		}
	}

	if  (Debug)
	    Verbose = TRUE;

	if  (InsertLeapSecond || DeleteLeapSecond)
		{
		LeapDayOfYear = ConvertMonthDayToDayOfYear (LeapYear, LeapMonth, LeapDayOfMonth);

		if	(Debug)
			{
			printf ("\nHave request for leap second %s at year %4d day %3d at %2.2dh%2.2d....\n",\
					DeleteLeapSecond ? "DELETION" : (InsertLeapSecond ? "ADDITION" : "( error ! )" ),
					LeapYear, LeapDayOfYear, LeapHour, LeapMinute);
			}
		}

	if	(DstSwitchFlag)
		{
		DstSwitchDayOfYear = ConvertMonthDayToDayOfYear (DstSwitchYear, DstSwitchMonth, DstSwitchDayOfMonth);

		/* Figure out time of minute previous to DST switch, so can put up warning flag in IEEE 1344 */
		DstSwitchPendingYear		= DstSwitchYear;
		DstSwitchPendingDayOfYear	= DstSwitchDayOfYear;
		DstSwitchPendingHour		= DstSwitchHour;
		DstSwitchPendingMinute		= DstSwitchMinute - 1;
		if 	(DstSwitchPendingMinute < 0)
			{
			DstSwitchPendingMinute = 59;
			DstSwitchPendingHour--;
			if	(DstSwitchPendingHour < 0)
				{
				DstSwitchPendingHour = 23;
				DstSwitchPendingDayOfYear--;
				if	(DstSwitchPendingDayOfYear < 1)
					{
					DstSwitchPendingYear--;
					}
				}
			}

		if	(Debug)
			{
			printf ("\nHave DST switch request for year %4d day %3d at %2.2dh%2.2d,",
					DstSwitchYear, DstSwitchDayOfYear, DstSwitchHour, DstSwitchMinute);
			printf ("\n    so will have warning at year %4d day %3d at %2.2dh%2.2d.\n",
					DstSwitchPendingYear, DstSwitchPendingDayOfYear, DstSwitchPendingHour, DstSwitchPendingMinute);
			}
		}

	switch (tolower(FormatCharacter)) {
	case 'i':
		printf ("\nFormat is IRIG-1998 (no year coded)...\n\n");
		encode = IRIG;
		IrigIncludeYear = FALSE;
		IrigIncludeIeee = FALSE;
		break;

	case '2':
		printf ("\nFormat is IRIG-2004 (BCD year coded)...\n\n");
		encode = IRIG;
		IrigIncludeYear = TRUE;
		IrigIncludeIeee = FALSE;
		break;

	case '3':
		printf ("\nFormat is IRIG with IEEE-1344 (BCD year coded, and more control functions)...\n\n");
		encode = IRIG;
		IrigIncludeYear = TRUE;
		IrigIncludeIeee = TRUE;
		break;

	case '4':
		printf ("\nFormat is unmodulated IRIG with IEEE-1344 (BCD year coded, and more control functions)...\n\n");
		encode = IRIG;
		IrigIncludeYear = TRUE;
		IrigIncludeIeee = TRUE;

		Unmodulated = TRUE;
		UnmodulatedInverted = FALSE;
		break;

	case '5':
		printf ("\nFormat is inverted unmodulated IRIG with IEEE-1344 (BCD year coded, and more control functions)...\n\n");
		encode = IRIG;
		IrigIncludeYear = TRUE;
		IrigIncludeIeee = TRUE;

		Unmodulated = TRUE;
		UnmodulatedInverted = TRUE;
		break;

	case 'w':
		printf ("\nFormat is WWV(H)...\n\n");
		encode = WWV;
		break;

	default:
		printf ("\n\nUnexpected format value of \'%c\', cannot parse, aborting...\n\n", FormatCharacter);
		exit (-1);
		break;
	}

	/*
	 * Open audio device and set options
	 */
	fd = open(device, O_WRONLY);
	if (fd <= 0) {
		printf("Unable to open audio device \"%s\", aborting: %s\n", device, strerror(errno));
		exit(1);
	}

#ifdef  HAVE_SYS_SOUNDCARD_H
	/* First set coding type */
	AudioFormat = AFMT_MU_LAW;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &AudioFormat)==-1)
	{ /* Fatal error */
	printf ("\nUnable to set output format, aborting...\n\n");
	exit(-1);
	}

	if  (AudioFormat != AFMT_MU_LAW)
	{
	printf ("\nUnable to set output format for mu law, aborting...\n\n");
	exit(-1);
	}

	/* Next set number of channels */
	MonoStereo = MONO;	/* Mono */
	if (ioctl(fd, SNDCTL_DSP_STEREO, &MonoStereo)==-1)
	{ /* Fatal error */
	printf ("\nUnable to set mono/stereo, aborting...\n\n");
	exit(-1);
	}

	if (MonoStereo != MONO)
	{
	printf ("\nUnable to set mono/stereo for mono, aborting...\n\n");
	exit(-1);
	}

	/* Now set sample rate */
	SampleRate = SetSampleRate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &SampleRate)==-1)
	{ /* Fatal error */
	printf ("\nUnable to set sample rate to %d, returned %d, aborting...\n\n", SetSampleRate, SampleRate);
	exit(-1);
	}

	SampleRateDifference = SampleRate - SetSampleRate;

	if  (SampleRateDifference < 0)
		SampleRateDifference = - SampleRateDifference;

	/* Fixed allowable sample rate error 0.1% */
	if (SampleRateDifference > (SetSampleRate/1000))
	{
	printf ("\nUnable to set sample rate to %d, result was %d, more than 0.1 percent, aborting...\n\n", SetSampleRate, SampleRate);
	exit(-1);
	}
	else
	{
	/* printf ("\nAttempt to set sample rate to %d, actual %d...\n\n", SetSampleRate, SampleRate); */
	}
#else
	rval = ioctl(fd, AUDIO_GETINFO, &info);
	if (rval < 0) {
		printf("\naudio control %s", strerror(errno));
		exit(0);
	}
	info.play.port = port;
	info.play.gain = level;
	info.play.sample_rate = SetSampleRate;
	info.play.channels = 1;
	info.play.precision = 8;
	info.play.encoding = AUDIO_ENCODING_ULAW;
	printf("\nport %d gain %d rate %d chan %d prec %d encode %d\n",
	    info.play.port, info.play.gain, info.play.sample_rate,
	    info.play.channels, info.play.precision,
	    info.play.encoding);
	ioctl(fd, AUDIO_SETINFO, &info);
#endif

 	/*
	 * Unless specified otherwise, read the system clock and
	 * initialize the time.
	 */
	gettimeofday(&TimeValue, NULL);		// Now always read the system time to keep "real time" of operation.
	NowRealTime = BaseRealTime = SecondsPartOfTime = TimeValue.tv_sec;
	SecondsRunningSimulationTime = 0;	// Just starting simulation, running zero seconds as of now.
	StabilityCount = 0;					// No stability yet.

	if	(utc)
		{
		DayOfYear = ConvertMonthDayToDayOfYear (Year, Month, DayOfMonth);
		}
	else
		{
		/* Apply offset to time. */
		if	(UseOffsetSecondsInt >= 0)
			SecondsPartOfTime += (time_t)   UseOffsetSecondsInt;
		else
			SecondsPartOfTime -= (time_t) (-UseOffsetSecondsInt);

		TimeStructure = gmtime(&SecondsPartOfTime);
		Minute = TimeStructure->tm_min;
		Hour = TimeStructure->tm_hour;
		DayOfYear = TimeStructure->tm_yday + 1;
		Year = TimeStructure->tm_year % 100;
		Second = TimeStructure->tm_sec;

		/*
		 * Delay the first second so the generator is accurately
		 * aligned with the system clock within one sample (125
		 * microseconds ).
		 */
		delay(SECOND - TimeValue.tv_usec * 8 / 1000);
		}

	StraightBinarySeconds = Second + (Minute * SECONDS_PER_MINUTE) + (Hour * SECONDS_PER_HOUR);

	memset(code, 0, sizeof(code));
	switch (encode) {

	/*
	 * For WWV/H and default time, carefully set the signal
	 * generator seconds number to agree with the current time.
	 */
	case WWV:
		printf("WWV time signal, starting point:\n");
		printf(" Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Minute tone = %d Hz, Hour tone = %d Hz.\n",
		    Year, DayOfYear, Hour, Minute, Second, tone, HourTone);
		snprintf(code, sizeof(code), "%01d%03d%02d%02d%01d",
		    Year / 10, DayOfYear, Hour, Minute, Year % 10);
		if  (Verbose)
			{
		    printf("\n Year = %2.2d, Day of year = %3d, Time = %2.2d:%2.2d:%2.2d, Code = %s", 
				Year, DayOfYear, Hour, Minute, Second, code);

				if  ((EnableRateCorrection) || (RemoveCycle) || (AddCycle))
				printf (", CountOfSecondsSent = %d, TotalCyclesAdded = %d, TotalCyclesRemoved = %d\n", CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
			else
				printf ("\n");
			}

		ptr = 8;
		for (BitNumber = 0; BitNumber <= Second; BitNumber++) {
			if (progx[BitNumber].sw == DEC)
				ptr--;
		}
		break;

	/*
	 * For IRIG the signal generator runs every second, so requires
	 * no additional alignment.
	 */
	case IRIG:
		printf ("IRIG-B time signal, starting point:\n");
		printf (" Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Straight binary seconds (SBS) = %05d / 0x%04X.\n",
		    Year, DayOfYear, Hour, Minute, Second, StraightBinarySeconds, StraightBinarySeconds);
		printf ("\n");
		if  (Verbose)
		    {
    		printf ("Codes: \".\" = marker/position indicator, \"-\" = zero dummy bit, \"0\" = zero bit, \"1\" = one bit.\n");
			if  ((EnableRateCorrection) || (AddCycle) || (RemoveCycle))
				{
				printf ("       \"o\" = short zero, \"*\" = long zero, \"x\" = short one, \"+\" = long one.\n");
				}
	    	printf ("Numerical values are time order reversed in output to make it easier to read.\n");
    		/*                 111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999 */
	    	/*       0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789 */
		    printf ("\n");
    		printf ("Legend of output codes:\n");
	    	//printf ("\n");
		    //printf ("|  StraightBinSecs  | IEEE_1344_Control |   Year  |    Day_of_Year    |  Hours  | Minutes |Seconds |\n");
    		//printf ("|  ---------------  | ----------------- |   ----  |    -----------    |  -----  | ------- |------- |\n");
	    	//printf ("|                   |                   |         |                   |         |         |        |\n");
	    	}
		break;
	}

	/*
	 * Run the signal generator to generate new timecode strings
	 * once per minute for WWV/H and once per second for IRIG.
	 */
	for (CountOfSecondsSent=0; ((SecondsToSend==0) || (CountOfSecondsSent<SecondsToSend)); CountOfSecondsSent++)
		{
		if  ((encode == IRIG) && (((Second % 20) == 0) || (CountOfSecondsSent == 0)))
			{
	    	printf ("\n");

			printf (" Year = %02d, Day of year = %03d, Time = %02d:%02d:%02d, Straight binary seconds (SBS) = %05d / 0x%04X.\n",
			    Year, DayOfYear, Hour, Minute, Second, StraightBinarySeconds, StraightBinarySeconds);
			if  ((EnableRateCorrection) || (RemoveCycle) || (AddCycle))
				{
				printf (" CountOfSecondsSent = %d, TotalCyclesAdded = %d, TotalCyclesRemoved = %d\n", CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
				if  ((CountOfSecondsSent != 0) && ((TotalCyclesAdded != 0) || (TotalCyclesRemoved != 0)))
					{
					RatioError = ((float) (TotalCyclesAdded - TotalCyclesRemoved)) / (1000.0 * (float) CountOfSecondsSent);
					printf (" Adjusted by %2.1f%%, apparent send frequency is %4.2f Hz not %d Hz.\n\n", 
									RatioError*100.0, (1.0+RatioError)*((float) SetSampleRate), SetSampleRate);
					}
				}
			else
				printf ("\n");

		    /* printf ("|Seconds | Minutes |  Hours  |    Day_of_Year    |   Year  | IEEE_1344_Control |  StraightBinSecs  |\n");
    		printf ("|------- | ------- |  -----  |    -----------    |   ----  | ----------------- |-------------------|\n");
	    	printf ("|        |         |         |                   |         |                   |                   |\n");*/
		    printf ("|  StraightBinSecs  | IEEE_1344_Control |   Year  |    Day_of_Year    |  Hours  | Minutes |Seconds |\n");
    		printf ("|  ---------------  | ----------------- |   ----  |    -----------    |  -----  | ------- |------- |\n");
	    	printf ("|                   |                   |         |                   |         |         |        |\n");
			}

		if  (RemoveCycle)
			{
			RateCorrection = -1;
			TotalSecondsCorrected ++;
			}
		else
			{
			if  (AddCycle)
				{
				TotalSecondsCorrected ++;
				RateCorrection = +1;
				}
			else
				RateCorrection = 0;
			}

		/*
		 * Crank the state machine to propagate carries to the
		 * year of century. Note that we delayed up to one
		 * second for alignment after reading the time, so this
		 * is the next second.
		 */

		if  (LeapState == LEAPSTATE_NORMAL)
			{
			/* If on the second of a leap (second 59 in the specified minute), then add or delete a second */
			if  ((Year == LeapYear) && (DayOfYear == LeapDayOfYear) && (Hour == LeapHour) && (Minute == LeapMinute))
				{
				/* To delete a second, which means we go from 58->60 instead of 58->59->00. */
				if  ((DeleteLeapSecond) && (Second == 58))
					{
					LeapState = LEAPSTATE_DELETING;

					if	(Debug)
						printf ("\n<--- Ready to delete a leap second...\n");
					}
				else
					{	/* Delete takes precedence over insert. */
					/* To add a second, which means we go from 59->60->00 instead of 59->00. */
					if  ((InsertLeapSecond) && (Second == 59))
						{
						LeapState = LEAPSTATE_INSERTING;

						if	(Debug)
							printf ("\n<--- Ready to insert a leap second...\n");
						}
					}
				}
			}

		switch (LeapState)
			{
			case LEAPSTATE_NORMAL:
				Second = (Second + 1) % 60;
				break;

			case LEAPSTATE_DELETING:
				Second = 0;
				LeapState = LEAPSTATE_NORMAL;

				if	(Debug)
					printf ("\n<--- Deleting a leap second...\n");
				break;

			case LEAPSTATE_INSERTING:
				Second = 60;
				LeapState = LEAPSTATE_ZERO_AFTER_INSERT;

				if	(Debug)
					printf ("\n<--- Inserting a leap second...\n");
				break;

			case LEAPSTATE_ZERO_AFTER_INSERT:
				Second = 0;
				LeapState = LEAPSTATE_NORMAL;

				if	(Debug)
					printf ("\n<--- Inserted a leap second, now back to zero...\n");
				break;

			default:
				printf ("\n\nLeap second state invalid value of %d, aborting...", LeapState);
				exit (-1);
				break;
			}

		/* Check for second rollover, increment minutes and ripple upward if required. */
		if (Second == 0) {
			Minute++;
			if (Minute >= 60) {
				Minute = 0;
				Hour++;
			}

			/* Check for activation of DST switch. */
			/* If DST is active, this would mean that at the appointed time, we de-activate DST, */
			/* which translates to going backward an hour (repeating the last hour). */
			/* If DST is not active, this would mean that at the appointed time, we activate DST, */
			/* which translates to going forward an hour (skipping the next hour). */
			if	(DstSwitchFlag)
				{
				/* The actual switch happens on the zero'th second of the actual minute specified. */
				if	((Year == DstSwitchYear) && (DayOfYear == DstSwitchDayOfYear) && (Hour == DstSwitchHour) && (Minute == DstSwitchMinute))
					{
					if  (DstFlag == 0)
						{	/* DST flag is zero, not in DST, going to DST, "spring ahead", so increment hour by two instead of one. */
						Hour++;
						DstFlag = 1;

						/* Must adjust offset to keep consistent with UTC. */
						/* Here we have to increase offset by one hour.  If it goes from negative to positive, then we fix that. */
						if	(OffsetSignBit == 0)
							{	/* Offset is positive */
							if	(OffsetOnes == 0x0F)
								{
								OffsetSignBit = 1;
								OffsetOnes    = (OffsetHalf == 0) ? 8 : 7;
								}
							else
								OffsetOnes++;
							}
						else
							{	/* Offset is negative */
							if  (OffsetOnes == 0)
								{
								OffsetSignBit = 0;
								OffsetOnes    = (OffsetHalf == 0) ? 1 : 0;
								}
							else
								OffsetOnes--;
							}

						if	(Debug)
							printf ("\n<--- DST activated, spring ahead an hour, new offset !...\n");
						}
					else
						{	/* DST flag is non zero, in DST, going out of DST, "fall back", so no increment of hour. */
						Hour--;
						DstFlag = 0;

						/* Must adjust offset to keep consistent with UTC. */
						/* Here we have to reduce offset by one hour.  If it goes negative, then we fix that. */
						if	(OffsetSignBit == 0)
							{	/* Offset is positive */
							if  (OffsetOnes == 0)
								{
								OffsetSignBit = 1;
								OffsetOnes    = (OffsetHalf == 0) ? 1 : 0;
								}
							else
								OffsetOnes--;
							}
						else
							{	/* Offset is negative */
							if	(OffsetOnes == 0x0F)
								{
								OffsetSignBit = 0;
								OffsetOnes    = (OffsetHalf == 0) ? 8 : 7;
								}
							else
								OffsetOnes++;
							}

						if	(Debug)
							printf ("\n<--- DST de-activated, fall back an hour!...\n");
						}

					DstSwitchFlag = FALSE;	/* One time deal, not intended to run this program past two switches... */
					}
				}

			if (Hour >= 24) {
				/* Modified, just in case dumb case where activating DST advances 23h59:59 -> 01h00:00 */
				Hour = Hour % 24;
				DayOfYear++;
			}

			/*
			 * At year rollover check for leap second.
			 */
			if (DayOfYear >= (Year & 0x3 ? 366 : 367)) {
				if (leap) {
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("\nLeap!");
					leap = 0;
				}
				DayOfYear = 1;
				Year++;
			}
			if (encode == WWV) {
				snprintf(code, sizeof(code),
				    "%01d%03d%02d%02d%01d", Year / 10,
				    DayOfYear, Hour, Minute, Year % 10);
				if  (Verbose)
				    printf("\n Year = %2.2d, Day of year = %3d, Time = %2.2d:%2.2d:%2.2d, Code = %s", 
						Year, DayOfYear, Hour, Minute, Second, code);

				if  ((EnableRateCorrection) || (RemoveCycle) || (AddCycle))
					{
					printf (", CountOfSecondsSent = %d, TotalCyclesAdded = %d, TotalCyclesRemoved = %d\n", CountOfSecondsSent, TotalCyclesAdded, TotalCyclesRemoved);
					if  ((CountOfSecondsSent != 0) && ((TotalCyclesAdded != 0) || (TotalCyclesRemoved != 0)))
						{
						RatioError = ((float) (TotalCyclesAdded - TotalCyclesRemoved)) / (1000.0 * (float) CountOfSecondsSent);
						printf (" Adjusted by %2.1f%%, apparent send frequency is %4.2f Hz not %d Hz.\n\n", 
										RatioError*100.0, (1.0+RatioError)*((float) SetSampleRate), SetSampleRate);
						}
					}
				else
					printf ("\n");

				ptr = 8;
			}
		}	/* End of "if  (Second == 0)" */

		/* After all that, if we are in the minute just prior to a leap second, warn of leap second pending */
		/* and of the polarity */
		if  ((Year == LeapYear) && (DayOfYear == LeapDayOfYear) && (Hour == LeapHour) && (Minute == LeapMinute))
			{
			LeapSecondPending = TRUE;
			LeapSecondPolarity = DeleteLeapSecond;
			}
		else
			{
			LeapSecondPending = FALSE;
			LeapSecondPolarity = FALSE;
			}

		/* Notification through IEEE 1344 happens during the whole minute previous to the minute specified. */
		/* The time of that minute has been previously calculated. */
		if	((Year == DstSwitchPendingYear) && (DayOfYear == DstSwitchPendingDayOfYear) &&
					(Hour == DstSwitchPendingHour) && (Minute == DstSwitchPendingMinute))
			{
			DstPendingFlag = TRUE;
			}
		else
			{
			DstPendingFlag = FALSE;
			}


		StraightBinarySeconds = Second + (Minute * SECONDS_PER_MINUTE) + (Hour * SECONDS_PER_HOUR);

		if (encode == IRIG) {
			if  (IrigIncludeIeee)
				{
				if  ((OffsetOnes == 0) && (OffsetHalf == 0))
					OffsetSignBit = 0;

				ControlFunctions = (LeapSecondPending == 0 ? 0x00000 : 0x00001) | (LeapSecondPolarity == 0 ? 0x00000 : 0x00002)
						| (DstPendingFlag == 0 ? 0x00000 : 0x00004) | (DstFlag == 0 ? 0x00000 : 0x00008)
						| (OffsetSignBit == 0 ? 0x00000 : 0x00010)  | ((OffsetOnes & 0x0F) << 5)           | (OffsetHalf == 0 ? 0x00000 : 0x00200)
						| ((TimeQuality & 0x0F) << 10);
				/* if  (Verbose)
				        printf ("\nDstFlag = %d, OffsetSignBit = %d, OffsetOnes = %d, OffsetHalf = %d, TimeQuality = 0x%1.1X ==> ControlFunctions = 0x%5.5X...",
						    DstFlag, OffsetSignBit, OffsetOnes, OffsetHalf, TimeQuality, ControlFunctions);
				*/
				}
			else
				ControlFunctions = 0;

			/*
						      YearDay HourMin Sec
			snprintf(code, sizeof(code), "%04x%04d%06d%02d%02d%02d",
				0, Year, DayOfYear, Hour, Minute, Second);
			*/
			if  (IrigIncludeYear) {
				snprintf(ParityString, sizeof(ParityString),
				    "%04X%02d%04d%02d%02d%02d",
				    ControlFunctions & 0x7FFF, Year,
				    DayOfYear, Hour, Minute, Second);
			} else {
				snprintf(ParityString, sizeof(ParityString),
				    "%04X%02d%04d%02d%02d%02d",
				    ControlFunctions & 0x7FFF,
				    0, DayOfYear, Hour, Minute, Second);
			}

			if  (IrigIncludeIeee)
				{
				ParitySum = 0;
				for (StringPointer=ParityString; *StringPointer!=NUL; StringPointer++)
					{
					switch (toupper(*StringPointer))
						{
						case '1':
						case '2':
						case '4':
						case '8':
							ParitySum += 1;
							break;

						case '3':
						case '5':
						case '6':
						case '9':
						case 'A':
						case 'C':
							ParitySum += 2;
							break;

						case '7':
						case 'B':
						case 'D':
						case 'E':
							ParitySum += 3;
							break;

						case 'F':
							ParitySum += 4;
							break;
						}
					}

				if  ((ParitySum & 0x01) == 0x01)
					ParityValue = 0x01;
				else
					ParityValue = 0;
				}
			else
				ParityValue = 0;

			ControlFunctions |= ((ParityValue & 0x01) << 14);

			if  (IrigIncludeYear) {
				snprintf(code, sizeof(code),
				    /* YearDay HourMin Sec */
				    "%05X%05X%02d%04d%02d%02d%02d",
				    StraightBinarySeconds,
				    ControlFunctions, Year, DayOfYear,
				    Hour, Minute, Second);
			} else {
				snprintf(code, sizeof(code),
				    /* YearDay HourMin Sec */
				    "%05X%05X%02d%04d%02d%02d%02d",
				    StraightBinarySeconds,
				    ControlFunctions, 0, DayOfYear,
				    Hour, Minute, Second);
			}

			if  (Debug)
				printf("\nCode string: %s, ParityString = %s, ParitySum = 0x%2.2X, ParityValue = %d, DstFlag = %d...\n", code, ParityString, ParitySum, ParityValue, DstFlag);

			ptr = strlen(code)-1;
			OldPtr = 0;
		}

		/*
		 * Generate data for the second
		 */
		switch (encode) {

		/*
		 * The IRIG second consists of 20 BCD digits of width-
		 * modulateod pulses at 2, 5 and 8 ms and modulated 50
		 * percent on the 1000-Hz carrier.
		 */
		case IRIG:
			/* Initialize the output string */
			OutputDataString[0] = '\0';

			for (BitNumber = 0; BitNumber < 100; BitNumber++) {
				FrameNumber = (BitNumber/10) + 1;
				switch (FrameNumber)
					{
					case 1:
						/* bits 0 to 9, first frame */
						sw  = progz[BitNumber % 10].sw;
						arg = progz[BitNumber % 10].arg;
						break;

					case 2:
					case 3:
					case 4:
					case 5:
					case 6:
						/* bits 10 to 59, second to sixth frame */
						sw  = progy[BitNumber % 10].sw;
						arg = progy[BitNumber % 10].arg;
						break;

					case 7:
						/* bits 60 to 69, seventh frame */
						sw  = progw[BitNumber % 10].sw;
						arg = progw[BitNumber % 10].arg;
						break;

					case 8:
						/* bits 70 to 79, eighth frame */
						sw  = progv[BitNumber % 10].sw;
						arg = progv[BitNumber % 10].arg;
						break;

					case 9:
						/* bits 80 to 89, ninth frame */
						sw  = progw[BitNumber % 10].sw;
						arg = progw[BitNumber % 10].arg;
						break;

					case 10:
						/* bits 90 to 99, tenth frame */
						sw  = progu[BitNumber % 10].sw;
						arg = progu[BitNumber % 10].arg;
						break;

					default:
						/* , Unexpected values of FrameNumber */
						printf ("\n\nUnexpected value of FrameNumber = %d, cannot parse, aborting...\n\n", FrameNumber);
						exit (-1);
						break;
					}

				switch(sw) {

				case DECC:	/* decrement pointer and send bit. */
					ptr--;
				case COEF:	/* send BCD bit */
					AsciiValue = toupper(code[ptr]);
					HexValue   = isdigit(AsciiValue) ? AsciiValue - '0' : (AsciiValue - 'A')+10;
					/* if  (Debug) {
						if  (ptr != OldPtr) {
						if  (Verbose)
						    printf("\n(%c->%X)", AsciiValue, HexValue);
						OldPtr = ptr;
						}
					}
					*/
					// OK, adjust all unused bits in hundreds of days.
					if  ((FrameNumber == 5) && ((BitNumber % 10) > 1))
						{
						if  (RateCorrection < 0)
							{	// Need to remove cycles to catch up.
							if  ((HexValue & arg) != 0) 
								{
								if  (Unmodulated)
									{
									poop(M5, 1000, HIGH, UnmodulatedInverted);
									poop(M5-1, 1000, LOW,  UnmodulatedInverted);

									TotalCyclesRemoved += 1;
									}
								else
									{
									peep(M5, 1000, HIGH);
									peep(M5-1, 1000, LOW);

									TotalCyclesRemoved += 1;
									}
								strlcat(OutputDataString, "x", OUTPUT_DATA_STRING_LENGTH);
								}
							else 
								{
								if	(Unmodulated)
									{
									poop(M2, 1000, HIGH, UnmodulatedInverted);
									poop(M8-1, 1000, LOW,  UnmodulatedInverted);

									TotalCyclesRemoved += 1;
									}
								else
									{
									peep(M2, 1000, HIGH);
									peep(M8-1, 1000, LOW);

									TotalCyclesRemoved += 1;
									}
								strlcat(OutputDataString, "o", OUTPUT_DATA_STRING_LENGTH);
								}
							}	// End of true clause for "if  (RateCorrection < 0)"
						else
							{	// Else clause for "if  (RateCorrection < 0)"
							if  (RateCorrection > 0)
								{	// Need to add cycles to slow back down.
								if  ((HexValue & arg) != 0) 
									{
									if  (Unmodulated)
										{
										poop(M5, 1000, HIGH, UnmodulatedInverted);
										poop(M5+1, 1000, LOW,  UnmodulatedInverted);

										TotalCyclesAdded += 1;
										}
									else
										{
										peep(M5, 1000, HIGH);
										peep(M5+1, 1000, LOW);

										TotalCyclesAdded += 1;
										}
									strlcat(OutputDataString, "+", OUTPUT_DATA_STRING_LENGTH);
									}
								else 
									{
									if	(Unmodulated)
										{
										poop(M2, 1000, HIGH, UnmodulatedInverted);
										poop(M8+1, 1000, LOW,  UnmodulatedInverted);

										TotalCyclesAdded += 1;
										}
									else
										{
										peep(M2, 1000, HIGH);
										peep(M8+1, 1000, LOW);

										TotalCyclesAdded += 1;
										}
									strlcat(OutputDataString, "*", OUTPUT_DATA_STRING_LENGTH);
									}
								}	// End of true clause for "if  (RateCorrection > 0)"
							else
								{	// Else clause for "if  (RateCorrection > 0)"
								// Rate is OK, just do what you feel!
								if  ((HexValue & arg) != 0) 
									{
									if  (Unmodulated)
										{
										poop(M5, 1000, HIGH, UnmodulatedInverted);
										poop(M5, 1000, LOW,  UnmodulatedInverted);
										}
									else
										{
										peep(M5, 1000, HIGH);
										peep(M5, 1000, LOW);
										}
									strlcat(OutputDataString, "1", OUTPUT_DATA_STRING_LENGTH);
									}
								else 
									{
									if	(Unmodulated)
										{
										poop(M2, 1000, HIGH, UnmodulatedInverted);
										poop(M8, 1000, LOW,  UnmodulatedInverted);
										}
									else
										{
										peep(M2, 1000, HIGH);
										peep(M8, 1000, LOW);
										}
									strlcat(OutputDataString, "0", OUTPUT_DATA_STRING_LENGTH);
									}
								}	// End of else clause for "if  (RateCorrection > 0)"
							}	// End of else claues for "if  (RateCorrection < 0)"
						}	// End of true clause for "if  ((FrameNumber == 5) && (BitNumber == 8))"
					else
						{	// Else clause for "if  ((FrameNumber == 5) && (BitNumber == 8))"
						if  ((HexValue & arg) != 0) 
							{
							if  (Unmodulated)
								{
								poop(M5, 1000, HIGH, UnmodulatedInverted);
								poop(M5, 1000, LOW,  UnmodulatedInverted);
								}
							else
								{
								peep(M5, 1000, HIGH);
								peep(M5, 1000, LOW);
								}
							strlcat(OutputDataString, "1", OUTPUT_DATA_STRING_LENGTH);
							}
						else 
							{
							if	(Unmodulated)
								{
								poop(M2, 1000, HIGH, UnmodulatedInverted);
								poop(M8, 1000, LOW,  UnmodulatedInverted);
								}
							else
								{
								peep(M2, 1000, HIGH);
								peep(M8, 1000, LOW);
								}
							strlcat(OutputDataString, "0", OUTPUT_DATA_STRING_LENGTH);
							}
						} // end of else clause for "if  ((FrameNumber == 5) && (BitNumber == 8))"
					break;

				case DECZ:	/* decrement pointer and send zero bit */
					ptr--;
					if	(Unmodulated)
						{
						poop(M2, 1000, HIGH, UnmodulatedInverted);
						poop(M8, 1000, LOW,  UnmodulatedInverted);
						}
					else
						{
						peep(M2, 1000, HIGH);
						peep(M8, 1000, LOW);
						}
					strlcat(OutputDataString, "-", OUTPUT_DATA_STRING_LENGTH);
					break;

				case DEC:	/* send marker/position indicator IM/PI bit */
					ptr--;
				case NODEC:	/* send marker/position indicator IM/PI bit but no decrement pointer */
				case MIN:	/* send "second start" marker/position indicator IM/PI bit */
					if  (Unmodulated)
						{
						poop(arg,      1000, HIGH, UnmodulatedInverted);
						poop(10 - arg, 1000, LOW,  UnmodulatedInverted);
						}
					else
						{
						peep(arg,      1000, HIGH);
						peep(10 - arg, 1000, LOW);
						}
					strlcat(OutputDataString, ".", OUTPUT_DATA_STRING_LENGTH);
					break;

				default:
					printf ("\n\nUnknown state machine value \"%d\", unable to continue, aborting...\n\n", sw);
					exit (-1);
					break;
				}
				if (ptr < 0)
					break;
			}
			ReverseString ( OutputDataString );
			if  (Verbose)
				{
    			printf("%s", OutputDataString);
				if  (RateCorrection > 0)
					printf(" fast\n");
				else
					{
					if  (RateCorrection < 0)
						printf (" slow\n");
					else
						printf ("\n");
					}
				}
			break;

		/*
		 * The WWV/H second consists of 9 BCD digits of width-
		 * modulateod pulses 200, 500 and 800 ms at 100-Hz.
		 */
		case WWV:
			sw = progx[Second].sw;
			arg = progx[Second].arg;
			switch(sw) {

			case DATA:		/* send data bit */
				WWV_Second(arg, RateCorrection);
				if  (Verbose)
					{
					if  (arg == DATA0)
						printf ("0");
					else
						{
						if  (arg == DATA1)
							printf ("1");
						else
							{
							if  (arg == PI)
								printf ("P");
							else
								printf ("?");
							}
						}
					}
				break;

			case DATAX:		/* send data bit */
				WWV_SecondNoTick(arg, RateCorrection);
				if  (Verbose)
					{
					if  (arg == DATA0)
						printf ("0");
					else
						{
						if  (arg == DATA1)
							printf ("1");
						else
							{
							if  (arg == PI)
								printf ("P");
							else
								printf ("?");
							}
						}
					}
				break;

			case COEF:		/* send BCD bit */
				if (code[ptr] & arg) {
					WWV_Second(DATA1, RateCorrection);
					if  (Verbose)
					    printf("1");
				} else {
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("0");
				}
				break;

			case LEAP:		/* send leap bit */
				if (leap) {
					WWV_Second(DATA1, RateCorrection);
					if  (Verbose)
					    printf("L");
				} else {
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("0");
				}
				break;

			case DEC:		/* send data bit */
				ptr--;
				WWV_Second(arg, RateCorrection);
				if  (Verbose)
					{
					if  (arg == DATA0)
						printf ("0");
					else
						{
						if  (arg == DATA1)
							printf ("1");
						else
							{
							if  (arg == PI)
								printf ("P");
							else
								printf ("?");
							}
						}
					}
				break;

			case DECX:		/* send data bit with no tick */
				ptr--;
				WWV_SecondNoTick(arg, RateCorrection);
				if  (Verbose)
					{
					if  (arg == DATA0)
						printf ("0");
					else
						{
						if  (arg == DATA1)
							printf ("1");
						else
							{
							if  (arg == PI)
								printf ("P");
							else
								printf ("?");
							}
						}
					}
				break;

			case MIN:		/* send minute sync */
				if  (Minute == 0)
					{
					peep(arg, HourTone, HIGH);

					if  (RateCorrection < 0)
						{
						peep( 990 - arg, HourTone, OFF);
						TotalCyclesRemoved += 10;

						if  (Debug)
							printf ("\n* Shorter Second: ");
						}
					else
						{
						if	(RateCorrection > 0)
							{
							peep(1010 - arg, HourTone, OFF);

							TotalCyclesAdded += 10;

							if  (Debug)
								printf ("\n* Longer Second: ");
							}
						else
							{
							peep(1000 - arg, HourTone, OFF);
							}
						}

					if  (Verbose)
					    printf("H");
					}
				else
					{
					peep(arg, tone, HIGH);

					if  (RateCorrection < 0)
						{
						peep( 990 - arg, tone, OFF);
						TotalCyclesRemoved += 10;

						if  (Debug)
							printf ("\n* Shorter Second: ");
						}
					else
						{
						if	(RateCorrection > 0)
							{
							peep(1010 - arg, tone, OFF);

							TotalCyclesAdded += 10;

							if  (Debug)
								printf ("\n* Longer Second: ");
							}
						else
							{
							peep(1000 - arg, tone, OFF);
							}
						}

					if  (Verbose)
					    printf("M");
					}
				break;

			case DUT1:		/* send DUT1 bits */
				if (dut1 & arg)
					{
					WWV_Second(DATA1, RateCorrection);
					if  (Verbose)
					    printf("1");
					}
				else
					{
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("0");
					}
				break;

			case DST1:		/* send DST1 bit */
				ptr--;
				if (DstFlag)
					{
					WWV_Second(DATA1, RateCorrection);
					if  (Verbose)
					    printf("1");
					}
				else
					{
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("0");
					}
				break;

			case DST2:		/* send DST2 bit */
				if (DstFlag)
					{
					WWV_Second(DATA1, RateCorrection);
					if  (Verbose)
					    printf("1");
					}
				else
					{
					WWV_Second(DATA0, RateCorrection);
					if  (Verbose)
					    printf("0");
					}
				break;
			}
		}

	if  (EnableRateCorrection)
		{
		SecondsRunningSimulationTime++;

		gettimeofday(&TimeValue, NULL);
		NowRealTime = TimeValue.tv_sec;

		if  (NowRealTime >= BaseRealTime)		// Just in case system time corrects backwards, do not blow up.
			{
			SecondsRunningRealTime = (unsigned) (NowRealTime - BaseRealTime);
			SecondsRunningDifference = SecondsRunningSimulationTime - SecondsRunningRealTime;

			if  (Debug)
				{
				printf ("> NowRealTime = 0x%8.8X, BaseRealtime = 0x%8.8X, SecondsRunningRealTime = 0x%8.8X, SecondsRunningSimulationTime = 0x%8.8X.\n",
							(unsigned) NowRealTime, (unsigned) BaseRealTime, SecondsRunningRealTime, SecondsRunningSimulationTime);
				printf ("> SecondsRunningDifference = 0x%8.8X, ExpectedRunningDifference = 0x%8.8X.\n",
							SecondsRunningDifference, ExpectedRunningDifference);
				}

			if  (SecondsRunningSimulationTime > RUN_BEFORE_STABILITY_CHECK)
				{
				if  (StabilityCount < MINIMUM_STABILITY_COUNT)
					{
					if  (StabilityCount == 0)
						{
						ExpectedRunningDifference = SecondsRunningDifference;
						StabilityCount++;
						if  (Debug)
							printf ("> Starting stability check.\n");
						}
					else
						{	// Else for "if  (StabilityCount == 0)"
						if  ((ExpectedRunningDifference+INITIAL_STABILITY_BAND > SecondsRunningDifference)
								&& (ExpectedRunningDifference-INITIAL_STABILITY_BAND < SecondsRunningDifference))
							{	// So far, still within stability band, increment count.
							StabilityCount++;
							if  (Debug)
								printf ("> StabilityCount = %d.\n", StabilityCount);
							}
						else
							{	// Outside of stability band, start over.
							StabilityCount = 0;
							if  (Debug)
								printf ("> Out of stability band, start over.\n");
							}
						} // End of else for "if  (StabilityCount == 0)"
					}	// End of true clause for "if  (StabilityCount < MINIMUM_STABILITY_COUNT))"
				else
					{	// Else clause for "if  (StabilityCount < MINIMUM_STABILITY_COUNT))" - OK, so we are supposed to be stable.
					if  (AddCycle)
						{
						if  (ExpectedRunningDifference >= SecondsRunningDifference)
							{
							if  (Debug)
								printf ("> Was adding cycles, ExpectedRunningDifference >= SecondsRunningDifference, can stop it now.\n");

							AddCycle = FALSE;
							RemoveCycle = FALSE;
							}
						else
							{
							if  (Debug)
								printf ("> Was adding cycles, not done yet.\n");
							}
						}
					else
						{
						if  (RemoveCycle)
							{
							if  (ExpectedRunningDifference <= SecondsRunningDifference)
								{
								if  (Debug)
									printf ("> Was removing cycles, ExpectedRunningDifference <= SecondsRunningDifference, can stop it now.\n");

								AddCycle = FALSE;
								RemoveCycle = FALSE;
								}
							else
								{
								if  (Debug)
									printf ("> Was removing cycles, not done yet.\n");
								}
							}
						else
							{
							if  ((ExpectedRunningDifference+RUNNING_STABILITY_BAND > SecondsRunningDifference)
									&& (ExpectedRunningDifference-RUNNING_STABILITY_BAND < SecondsRunningDifference))
								{	// All is well, within tolerances.
								if  (Debug)
									printf ("> All is well, within tolerances.\n");
								}
							else
								{	// Oops, outside tolerances.  Else clause of "if  ((ExpectedRunningDifference...SecondsRunningDifference)"
								if  (ExpectedRunningDifference > SecondsRunningDifference)
									{
									if  (Debug)
										printf ("> ExpectedRunningDifference > SecondsRunningDifference, running behind real time.\n");

									// Behind real time, have to add a cycle to slow down and get back in sync.
									AddCycle = FALSE;
									RemoveCycle = TRUE;
									}
								else
									{	// Else clause of "if  (ExpectedRunningDifference < SecondsRunningDifference)"
									if  (ExpectedRunningDifference < SecondsRunningDifference)
										{
										if  (Debug)
											printf ("> ExpectedRunningDifference < SecondsRunningDifference, running ahead of real time.\n");

										// Ahead of real time, have to remove a cycle to speed up and get back in sync.
										AddCycle = TRUE;
										RemoveCycle = FALSE;
										}
									else
										{
										if  (Debug)
											printf ("> Oops, outside tolerances, but doesn't fit the profiles, how can this be?\n");
										}
									}	// End of else clause of "if  (ExpectedRunningDifference > SecondsRunningDifference)"
								}	// End of else clause of "if  ((ExpectedRunningDifference...SecondsRunningDifference)"
							}	// End of else clause of "if  (RemoveCycle)".
						}	// End of else clause of "if  (AddCycle)".
					}	// End of else clause for "if  (StabilityCount < MINIMUM_STABILITY_COUNT))"
				}	// End of true clause for "if  ((SecondsRunningSimulationTime > RUN_BEFORE_STABILITY_CHECK)"
			}	// End of true clause for "if  (NowRealTime >= BaseRealTime)"
		else
			{
			if  (Debug)
				printf ("> Hmm, time going backwards?\n");
			}
		}	// End of true clause for "if  (EnableRateCorrection)"
		
	fflush (stdout);
	}
	
	
printf ("\n\n>> Completed %d seconds, exiting...\n\n", SecondsToSend);
return (0);
}


/*
 * Generate WWV/H 0 or 1 data pulse.
 */
void WWV_Second(
	int	code,		/* DATA0, DATA1, PI */
	int Rate		/* <0 -> do a short second, 0 -> normal second, >0 -> long second */
	)
{
	/*
	 * The WWV data pulse begins with 5 ms of 1000 Hz follwed by a
	 * guard time of 25 ms. The data pulse is 170, 570 or 770 ms at
	 * 100 Hz corresponding to 0, 1 or position indicator (PI),
	 * respectively. Note the 100-Hz data pulses are transmitted 6
	 * dB below the 1000-Hz sync pulses. Originally the data pulses
	 * were transmited 10 dB below the sync pulses, but the station
	 * engineers increased that to 6 dB because the Heath GC-1000
	 * WWV/H radio clock worked much better.
	 */
	peep(5, tone, HIGH);		/* send seconds tick */
	peep(25, tone, OFF);
	peep(code - 30, 100, LOW);	/* send data */
	
	/* The quiet time is shortened or lengthened to get us back on time */
	if  (Rate < 0)
		{
		peep( 990 - code, 100, OFF);
		
		TotalCyclesRemoved += 10;

		if  (Debug)
			printf ("\n* Shorter Second: ");
		}
	else
		{
		if  (Rate > 0)
			{
			peep(1010 - code, 100, OFF);

			TotalCyclesAdded += 10;

			if  (Debug)
				printf ("\n* Longer Second: ");
			}
		else
			peep(1000 - code, 100, OFF);
		}
}

/*
 * Generate WWV/H 0 or 1 data pulse, with no tick, for 29th and 59th seconds
 */
void WWV_SecondNoTick(
	int	code,		/* DATA0, DATA1, PI */
	int Rate		/* <0 -> do a short second, 0 -> normal second, >0 -> long second */
	)
{
	/*
	 * The WWV data pulse begins with 5 ms of 1000 Hz follwed by a
	 * guard time of 25 ms. The data pulse is 170, 570 or 770 ms at
	 * 100 Hz corresponding to 0, 1 or position indicator (PI),
	 * respectively. Note the 100-Hz data pulses are transmitted 6
	 * dB below the 1000-Hz sync pulses. Originally the data pulses
	 * were transmited 10 dB below the sync pulses, but the station
	 * engineers increased that to 6 dB because the Heath GC-1000
	 * WWV/H radio clock worked much better.
	 */
	peep(30, tone, OFF);		/* send seconds non-tick */
	peep(code - 30, 100, LOW);	/* send data */

	/* The quiet time is shortened or lengthened to get us back on time */
	if  (Rate < 0)
		{
		peep( 990 - code, 100, OFF);

		TotalCyclesRemoved += 10;

		if  (Debug)
			printf ("\n* Shorter Second: ");
		}
	else
		{
		if  (Rate > 0)
			{
			peep(1010 - code, 100, OFF);

			TotalCyclesAdded += 10;

			if  (Debug)
				printf ("\n* Longer Second: ");
			}
		else
			peep(1000 - code, 100, OFF);
		}
}

/*
 * Generate cycles of 100 Hz or any multiple of 100 Hz.
 */
void peep(
	int	pulse,		/* pulse length (ms) */
	int	freq,		/* frequency (Hz) */
	int	amp		/* amplitude */
	)
{
	int	increm;		/* phase increment */
	int	i, j;

	if (amp == OFF || freq == 0)
		increm = 10;
	else
		increm = freq / 100;
	j = 0;
	for (i = 0 ; i < pulse * 8; i++) {
		switch (amp) {

		case HIGH:
			buffer[bufcnt++] = ~c6000[j];
			break;

		case LOW:
			buffer[bufcnt++] = ~c3000[j];
			break;

		default:
			buffer[bufcnt++] = ~0;
		}
		if (bufcnt >= BUFLNG) {
			write(fd, buffer, BUFLNG);
			bufcnt = 0;
		}
		j = (j + increm) % 80;
	}
}


/*
 * Generate unmodulated from similar tables.
 */
void poop(
	int	pulse,		/* pulse length (ms) */
	int	freq,		/* frequency (Hz) */
	int	amp,		/* amplitude */
	int inverted	/* is upside down */
	)
{
	int	increm;		/* phase increment */
	int	i, j;

	if (amp == OFF || freq == 0)
		increm = 10;
	else
		increm = freq / 100;
	j = 0;
	for (i = 0 ; i < pulse * 8; i++) {
		switch (amp) {

		case HIGH:
			if  (inverted)
				buffer[bufcnt++] = ~u3000[j];
			else
				buffer[bufcnt++] = ~u6000[j];
			break;

		case LOW:
			if  (inverted)
				buffer[bufcnt++] = ~u6000[j];
			else
				buffer[bufcnt++] = ~u3000[j];
			break;

		default:
			buffer[bufcnt++] = ~0;
		}
		if (bufcnt >= BUFLNG) {
			write(fd, buffer, BUFLNG);
			bufcnt = 0;
		}
		j = (j + increm) % 80;
	}
}

/*
 * Delay for initial phasing
 */
void delay (
	int	Delay		/* delay in samples */
	)
{
	int	samples;	/* samples remaining */

	samples = Delay;
	memset(buffer, 0, BUFLNG);
	while (samples >= BUFLNG) {
		write(fd, buffer, BUFLNG);
		samples -= BUFLNG;
	}
		write(fd, buffer, samples);
}


/* Calc day of year from year month & day */
/* Year - 0 means 2000, 100 means 2100. */
/* Month - 1 means January, 12 means December. */
/* DayOfMonth - 1 is first day of month */
int
ConvertMonthDayToDayOfYear (int YearValue, int MonthValue, int DayOfMonthValue)
	{
	int	ReturnValue;
	int	LeapYear;
	int	MonthCounter;

	/* Array of days in a month.  Note that here January is zero. */
	/* NB: have to add 1 to days in February in a leap year! */
	int DaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};


	LeapYear = FALSE;
	if  ((YearValue % 4) == 0)
		{
		if  ((YearValue % 100) == 0)
			{
			if  ((YearValue % 400) == 0)
				{
				LeapYear = TRUE;
				}
			}
		else
			{
			LeapYear = TRUE;
			}
		}

	if  (Debug)
		printf ("\nConvertMonthDayToDayOfYear(): Year %d %s a leap year.\n", YearValue+2000, LeapYear ? "is" : "is not");

	/* Day of month given us starts in this algorithm. */
	ReturnValue = DayOfMonthValue;

	/* Add in days in month for each month past January. */
	for (MonthCounter=1; MonthCounter<MonthValue; MonthCounter++)
		{
		ReturnValue += DaysInMonth [ MonthCounter - 1 ];
		}

	/* Add a day for leap years where we are past February. */
	if  ((LeapYear) && (MonthValue > 2))
		{
		ReturnValue++;
		}

	if  (Debug)
		printf ("\nConvertMonthDayToDayOfYear(): %4.4d-%2.2d-%2.2d represents day %3d of year.\n",
				YearValue+2000, MonthValue, DayOfMonthValue, ReturnValue);

	return (ReturnValue);
	}


void
Help ( void )
	{
	printf ("\n\nTime Code Generation - IRIG-B or WWV, v%d.%d, %s dmw", VERSION, ISSUE, ISSUE_DATE);
	printf ("\n\nRCS Info:");
	printf (  "\n  $Header: /home/dmw/src/IRIG_generation/ntp-4.2.2p3/util/RCS/tg.c,v 1.28 2007/02/12 23:57:45 dmw Exp $");
	printf ("\n\nUsage: %s [option]*", CommandName);
	printf ("\n\nOptions: -a device_name                 Output audio device name (default /dev/audio)");
	printf (  "\n         -b yymmddhhmm                  Remove leap second at end of minute specified");
	printf (  "\n         -c seconds_to_send             Number of seconds to send (default 0 = forever)");
	printf (  "\n         -d                             Start with IEEE 1344 DST active");
	printf (  "\n         -f format_type                 i = Modulated IRIG-B 1998 (no year coded)");
	printf (  "\n                                        2 = Modulated IRIG-B 2002 (year coded)");
	printf (  "\n                                        3 = Modulated IRIG-B w/IEEE 1344 (year & control funcs) (default)");
	printf (  "\n                                        4 = Unmodulated IRIG-B w/IEEE 1344 (year & control funcs)");
	printf (  "\n                                        5 = Inverted unmodulated IRIG-B w/IEEE 1344 (year & control funcs)");
	printf (  "\n                                        w = WWV(H)");
	printf (  "\n         -g yymmddhhmm                  Switch into/out of DST at beginning of minute specified");
	printf (  "\n         -i yymmddhhmm                  Insert leap second at end of minute specified");
	printf (  "\n         -j                             Disable time rate correction against system clock (default enabled)");
	printf (  "\n         -k nn                          Force rate correction for testing (+1 = add cycle, -1 = remove cycle)");
	printf (  "\n         -l time_offset                 Set offset of time sent to UTC as per computer, +/- float hours");
	printf (  "\n         -o time_offset                 Set IEEE 1344 time offset, +/-, to 0.5 hour (default 0)");
	printf (  "\n         -q quality_code_hex            Set IEEE 1344 quality code (default 0)");
	printf (  "\n         -r sample_rate                 Audio sample rate (default 8000)");
	printf (  "\n         -s                             Set leap warning bit (WWV[H] only)");
	printf (  "\n         -t sync_frequency              WWV(H) on-time pulse tone frequency (default 1200)");
	printf (  "\n         -u DUT1_offset                 Set WWV(H) DUT1 offset -7 to +7 (default 0)");
#ifndef  HAVE_SYS_SOUNDCARD_H
	printf (  "\n         -v initial_output_level        Set initial output level (default %d, must be 0 to 255)", AUDIO_MAX_GAIN/8);
#endif
	printf (  "\n         -x                             Turn off verbose output (default on)");
	printf (  "\n         -y yymmddhhmmss                Set initial date and time as specified (default system time)");
	printf ("\n\nThis software licenced under the GPL, modifications performed 2006 & 2007 by Dean Weiten");
	printf (  "\nContact: Dean Weiten, Norscan Instruments Ltd., Winnipeg, MB, Canada, ph (204)-233-9138, E-mail dmw@norscan.com");
	printf ("\n\n");
	}

/* Reverse string order for nicer print. */
void
ReverseString(char *str)
	{
	int		StringLength;
	int		IndexCounter;
	int		CentreOfString;
	char	TemporaryCharacter;


	StringLength	= strlen(str);
	CentreOfString	= (StringLength/2)+1;
	for (IndexCounter = StringLength; IndexCounter >= CentreOfString; IndexCounter--)
		{
		TemporaryCharacter				= str[IndexCounter-1];
		str[IndexCounter-1]				= str[StringLength-IndexCounter];
		str[StringLength-IndexCounter]	= TemporaryCharacter;
		}
	}

