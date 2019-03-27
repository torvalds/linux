/*
 * refclock_heath - clock driver for Heath GC-1000
 * (but no longer the GC-1001 Model II, which apparently never worked)
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_HEATH)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif /* not HAVE_SYS_IOCTL_H */

/*
 * This driver supports the Heath GC-1000 Most Accurate Clock, with
 * RS232C Output Accessory. This is a WWV/WWVH receiver somewhat less
 * robust than other supported receivers. Its claimed accuracy is 100 ms
 * when actually synchronized to the broadcast signal, but this doesn't
 * happen even most of the time, due to propagation conditions, ambient
 * noise sources, etc. When not synchronized, the accuracy is at the
 * whim of the internal clock oscillator, which can wander into the
 * sunset without warning. Since the indicated precision is 100 ms,
 * expect a host synchronized only to this thing to wander to and fro,
 * occasionally being rudely stepped when the offset exceeds the default
 * clock_max of 128 ms. 
 *
 * There were two GC-1000 versions supported by this driver. The original
 * GC-1000 with RS-232 output first appeared in 1983, but dissapeared
 * from the market a few years later. The GC-1001 II with RS-232 output
 * first appeared circa 1990, but apparently is no longer manufactured.
 * The two models differ considerably, both in interface and commands.
 * The GC-1000 has a pseudo-bipolar timecode output triggered by a RTS
 * transition. The timecode includes both the day of year and time of
 * day. The GC-1001 II has a true bipolar output and a complement of
 * single character commands. The timecode includes only the time of
 * day.
 *
 * The GC-1001 II was apparently never tested and, based on a Coverity
 * scan, apparently never worked [Bug 689].  Related code has been disabled.
 *
 * GC-1000
 *
 * The internal DIPswitches should be set to operate in MANUAL mode. The
 * external DIPswitches should be set to GMT and 24-hour format.
 *
 * In MANUAL mode the clock responds to a rising edge of the request to
 * send (RTS) modem control line by sending the timecode. Therefore, it
 * is necessary that the operating system implement the TIOCMBIC and
 * TIOCMBIS ioctl system calls and TIOCM_RTS control bit. Present
 * restrictions require the use of a POSIX-compatible programming
 * interface, although other interfaces may work as well.
 *
 * A simple hardware modification to the clock can be made which
 * prevents the clock hearing the request to send (RTS) if the HI SPEC
 * lamp is out. Route the HISPEC signal to the tone decoder board pin
 * 19, from the display, pin 19. Isolate pin 19 of the decoder board
 * first, but maintain connection with pin 10. Also isolate pin 38 of
 * the CPU on the tone board, and use half an added 7400 to gate the
 * original signal to pin 38 with that from pin 19.
 *
 * The clock message consists of 23 ASCII printing characters in the
 * following format:
 *
 * hh:mm:ss.f AM  dd/mm/yr<cr>
 *
 *	hh:mm:ss.f = hours, minutes, seconds
 *	f = deciseconds ('?' when out of spec)
 *	AM/PM/bb = blank in 24-hour mode
 *	dd/mm/yr = day, month, year
 *
 * The alarm condition is indicated by '?', rather than a digit, at f.
 * Note that 0?:??:??.? is displayed before synchronization is first
 * established and hh:mm:ss.? once synchronization is established and
 * then lost again for about a day.
 *
 * GC-1001 II
 *
 * Commands consist of a single letter and are case sensitive. When
 * enterred in lower case, a description of the action performed is
 * displayed. When enterred in upper case the action is performed.
 * Following is a summary of descriptions as displayed by the clock:
 *
 * The clock responds with a command The 'A' command returns an ASCII
 * local time string:  HH:MM:SS.T xx<CR>, where
 *
 *	HH = hours
 *	MM = minutes
 *	SS = seconds
 *	T = tenths-of-seconds
 *	xx = 'AM', 'PM', or '  '
 *	<CR> = carriage return
 *
 * The 'D' command returns 24 pairs of bytes containing the variable
 * divisor value at the end of each of the previous 24 hours. This
 * allows the timebase trimming process to be observed.  UTC hour 00 is
 * always returned first. The first byte of each pair is the high byte
 * of (variable divisor * 16); the second byte is the low byte of
 * (variable divisor * 16). For example, the byte pair 3C 10 would be
 * returned for a divisor of 03C1 hex (961 decimal).
 *
 * The 'I' command returns:  | TH | TL | ER | DH | DL | U1 | I1 | I2 | ,
 * where
 *
 *	TH = minutes since timebase last trimmed (high byte)
 *	TL = minutes since timebase last trimmed (low byte)
 *	ER = last accumulated error in 1.25 ms increments
 *	DH = high byte of (current variable divisor * 16)
 *	DL = low byte of (current variable divisor * 16)
 *	U1 = UT1 offset (/.1 s):  | + | 4 | 2 | 1 | 0 | 0 | 0 | 0 |
 *	I1 = information byte 1:  | W | C | D | I | U | T | Z | 1 | ,
 *	     where
 *
 *		W = set by WWV(H)
 *		C = CAPTURE LED on
 *		D = TRIM DN LED on
 *		I = HI SPEC LED on
 *		U = TRIM UP LED on
 *		T = DST switch on
 *		Z = UTC switch on
 *		1 = UT1 switch on
 *
 *	I2 = information byte 2:  | 8 | 8 | 4 | 2 | 1 | D | d | S | ,
 *	     where
 *
 *		8, 8, 4, 2, 1 = TIME ZONE switch settings
 *		D = DST bit (#55) in last-received frame
 *		d = DST bit (#2) in last-received frame
 *		S = clock is in simulation mode
 *
 * The 'P' command returns 24 bytes containing the number of frames
 * received without error during UTC hours 00 through 23, providing an
 * indication of hourly propagation.  These bytes are updated each hour
 * to reflect the previous 24 hour period.  UTC hour 00 is always
 * returned first.
 *
 * The 'T' command returns the UTC time:  | HH | MM | SS | T0 | , where
 *	HH = tens-of-hours and hours (packed BCD)
 *	MM = tens-of-minutes and minutes (packed BCD)
 *	SS = tens-of-seconds and seconds (packed BCD)
 *	T = tenths-of-seconds (BCD)
 *
 * Fudge Factors
 *
 * A fudge time1 value of .04 s appears to center the clock offset
 * residuals. The fudge time2 parameter is the local time offset east of
 * Greenwich, which depends on DST. Sorry about that, but the clock
 * gives no hint on what the DIPswitches say.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/heath%d" /* device name and unit */
#define	PRECISION	(-4)	/* precision assumed (about 100 ms) */
#define	REFID		"WWV\0"	/* reference ID */
#define	DESCRIPTION	"Heath GC-1000 Most Accurate Clock" /* WRU */

#define LENHEATH1	23	/* min timecode length */
#if 0	/* BUG 689 */
#define LENHEATH2	13	/* min timecode length */
#endif

/*
 * Tables to compute the ddd of year form icky dd/mm timecode. Viva la
 * leap.
 */
static int day1tab[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int day2tab[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * Baud rate table. The GC-1000 supports 1200, 2400 and 4800; the
 * GC-1001 II supports only 9600.
 */
static int speed[] = {B1200, B2400, B4800, B9600};

/*
 * Function prototypes
 */
static	int	heath_start	(int, struct peer *);
static	void	heath_shutdown	(int, struct peer *);
static	void	heath_receive	(struct recvbuf *);
static	void	heath_poll	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_heath = {
	heath_start,		/* start up driver */
	heath_shutdown,		/* shut down driver */
	heath_poll,		/* transmit poll message */
	noentry,		/* not used (old heath_control) */
	noentry,		/* initialize driver */
	noentry,		/* not used (old heath_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * heath_start - open the devices and initialize data for processing
 */
static int
heath_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	int fd;
	char device[20];

	/*
	 * Open serial port
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = refclock_open(device, speed[peer->ttl & 0x3],
			   LDISC_REMOTE);
	if (fd <= 0)
		return (0);
	pp = peer->procptr;
	pp->io.clock_recv = heath_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		return (0);
	}

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy(&pp->refid, REFID, 4);
	return (1);
}


/*
 * heath_shutdown - shut down the clock
 */
static void
heath_shutdown(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;

	pp = peer->procptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
}


/*
 * heath_receive - receive data from the serial interface
 */
static void
heath_receive(
	struct recvbuf *rbufp
	)
{
	struct refclockproc *pp;
	struct peer *peer;
	l_fp trtmp;
	int month, day;
	int i;
	char dsec, a[5];

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	pp->lencode = refclock_gtlin(rbufp, pp->a_lastcode, BMAX,
	    &trtmp);

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. If the timecode has invalid length or is not in
	 * proper format, we declare bad format and exit.
	 */
	switch (pp->lencode) {

	/*
	 * GC-1000 timecode format: "hh:mm:ss.f AM  mm/dd/yy"
	 * GC-1001 II timecode format: "hh:mm:ss.f   "
	 */
	case LENHEATH1:
		if (sscanf(pp->a_lastcode,
		    "%2d:%2d:%2d.%c%5c%2d/%2d/%2d", &pp->hour,
		    &pp->minute, &pp->second, &dsec, a, &month, &day,
		    &pp->year) != 8) {
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		}
		break;

#if 0	/* BUG 689 */
	/*
	 * GC-1001 II timecode format: "hh:mm:ss.f   "
	 */
	case LENHEATH2:
		if (sscanf(pp->a_lastcode, "%2d:%2d:%2d.%c", &pp->hour,
		    &pp->minute, &pp->second, &dsec) != 4) {
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		} else {
			struct tm *tm_time_p;
			time_t     now;

			time(&now);	/* we should grab 'now' earlier */
			tm_time_p = gmtime(&now);
			/*
			 * There is a window of time around midnight
			 * where this will Do The Wrong Thing.
			 */
			if (tm_time_p) {
				month = tm_time_p->tm_mon + 1;
				day = tm_time_p->tm_mday;
			} else {
				refclock_report(peer, CEVNT_FAULT);
				return;
			}
		}
		break;
#endif

	default:
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/*
	 * We determine the day of the year from the DIPswitches. This
	 * should be fixed, since somebody might forget to set them.
	 * Someday this hazard will be fixed by a fiendish scheme that
	 * looks at the timecode and year the radio shows, then computes
	 * the residue of the seconds mod the seconds in a leap cycle.
	 * If in the third year of that cycle and the third and later
	 * months of that year, add one to the day. Then, correct the
	 * timecode accordingly. Icky pooh. This bit of nonsense could
	 * be avoided if the engineers had been required to write a
	 * device driver before finalizing the timecode format.
	 */
	if (month < 1 || month > 12 || day < 1) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	if (pp->year % 4) {
		if (day > day1tab[month - 1]) {
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}
		for (i = 0; i < month - 1; i++)
		    day += day1tab[i];
	} else {
		if (day > day2tab[month - 1]) {
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}
		for (i = 0; i < month - 1; i++)
		    day += day2tab[i];
	}
	pp->day = day;

	/*
	 * Determine synchronization and last update
	 */
	if (!isdigit((unsigned char)dsec))
		pp->leap = LEAP_NOTINSYNC;
	else {
		pp->nsec = (dsec - '0') * 100000000;
		pp->leap = LEAP_NOWARNING;
	}
	if (!refclock_process(pp))
		refclock_report(peer, CEVNT_BADTIME);
}


/*
 * heath_poll - called by the transmit procedure
 */
static void
heath_poll(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	int bits = TIOCM_RTS;

	/*
	 * At each poll we check for timeout and toggle the RTS modem
	 * control line, then take a timestamp. Presumably, this is the
	 * event the radio captures to generate the timecode.
	 * Apparently, the radio takes about a second to make up its
	 * mind to send a timecode, so the receive timestamp is
	 * worthless.
	 */
	pp = peer->procptr;

	/*
	 * We toggle the RTS modem control lead (GC-1000) and sent a T
	 * (GC-1001 II) to kick a timecode loose from the radio. This
	 * code works only for POSIX and SYSV interfaces. With bsd you
	 * are on your own. We take a timestamp between the up and down
	 * edges to lengthen the pulse, which should be about 50 usec on
	 * a Sun IPC. With hotshot CPUs, the pulse might get too short.
	 * Later.
	 *
	 * Bug 689: Even though we no longer support the GC-1001 II,
	 * I'm leaving the 'T' write in for timing purposes.
	 */
	if (ioctl(pp->io.fd, TIOCMBIC, (char *)&bits) < 0)
		refclock_report(peer, CEVNT_FAULT);
	get_systime(&pp->lastrec);
	if (write(pp->io.fd, "T", 1) != 1)
		refclock_report(peer, CEVNT_FAULT);
	ioctl(pp->io.fd, TIOCMBIS, (char *)&bits);
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
#ifdef DEBUG
	if (debug)
	    printf("heath: timecode %d %s\n", pp->lencode,
		   pp->a_lastcode);
#endif
	pp->polls++;
}

#else
int refclock_heath_bs;
#endif /* REFCLOCK */
