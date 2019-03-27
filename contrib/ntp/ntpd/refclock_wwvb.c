/*
 * refclock_wwvb - clock driver for Spectracom WWVB and GPS receivers
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_SPECTRACOM)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_PPSAPI
#include "ppsapi_timepps.h"
#include "refclock_atom.h"
#endif /* HAVE_PPSAPI */

/*
 * This driver supports the Spectracom Model 8170 and Netclock/2 WWVB
 * Synchronized Clocks and the Netclock/GPS Master Clock. Both the WWVB
 * and GPS clocks have proven reliable sources of time; however, the
 * WWVB clocks have proven vulnerable to high ambient conductive RF
 * interference. The claimed accuracy of the WWVB clocks is 100 us
 * relative to the broadcast signal, while the claimed accuracy of the
 * GPS clock is 50 ns; however, in most cases the actual accuracy is
 * limited by the resolution of the timecode and the latencies of the
 * serial interface and operating system.
 *
 * The WWVB and GPS clocks should be configured for 24-hour display,
 * AUTO DST off, time zone 0 (UTC), data format 0 or 2 (see below) and
 * baud rate 9600. If the clock is to used as the source for the IRIG
 * Audio Decoder (refclock_irig.c in this distribution), it should be
 * configured for AM IRIG output and IRIG format 1 (IRIG B with
 * signature control). The GPS clock can be configured either to respond
 * to a 'T' poll character or left running continuously. 
 *
 * There are two timecode formats used by these clocks. Format 0, which
 * is available with both the Netclock/2 and 8170, and format 2, which
 * is available only with the Netclock/2, specially modified 8170 and
 * GPS.
 *
 * Format 0 (22 ASCII printing characters):
 *
 * <cr><lf>i  ddd hh:mm:ss TZ=zz<cr><lf>
 *
 *	on-time = first <cr>
 *	hh:mm:ss = hours, minutes, seconds
 *	i = synchronization flag (' ' = in synch, '?' = out of synch)
 *
 * The alarm condition is indicated by other than ' ' at i, which occurs
 * during initial synchronization and when received signal is lost for
 * about ten hours.
 *
 * Format 2 (24 ASCII printing characters):
 *
 * <cr><lf>iqyy ddd hh:mm:ss.fff ld
 *
 *	on-time = <cr>
 *	i = synchronization flag (' ' = in synch, '?' = out of synch)
 *	q = quality indicator (' ' = locked, 'A'...'D' = unlocked)
 *	yy = year (as broadcast)
 *	ddd = day of year
 *	hh:mm:ss.fff = hours, minutes, seconds, milliseconds
 *
 * The alarm condition is indicated by other than ' ' at i, which occurs
 * during initial synchronization and when received signal is lost for
 * about ten hours. The unlock condition is indicated by other than ' '
 * at q.
 *
 * The q is normally ' ' when the time error is less than 1 ms and a
 * character in the set 'A'...'D' when the time error is less than 10,
 * 100, 500 and greater than 500 ms respectively. The l is normally ' ',
 * but is set to 'L' early in the month of an upcoming UTC leap second
 * and reset to ' ' on the first day of the following month. The d is
 * set to 'S' for standard time 'I' on the day preceding a switch to
 * daylight time, 'D' for daylight time and 'O' on the day preceding a
 * switch to standard time. The start bit of the first <cr> is
 * synchronized to the indicated time as returned.
 *
 * This driver does not need to be told which format is in use - it
 * figures out which one from the length of the message. The driver
 * makes no attempt to correct for the intrinsic jitter of the radio
 * itself, which is a known problem with the older radios.
 *
 * PPS Signal Processing
 *
 * When PPS signal processing is enabled, and when the system clock has
 * been set by this or another driver and the PPS signal offset is
 * within 0.4 s of the system clock offset, the PPS signal replaces the
 * timecode for as long as the PPS signal is active. If for some reason
 * the PPS signal fails for one or more poll intervals, the driver
 * reverts to the timecode. If the timecode fails for one or more poll
 * intervals, the PPS signal is disconnected.
 *
 * Fudge Factors
 *
 * This driver can retrieve a table of quality data maintained
 * internally by the Netclock/2 clock. If flag4 of the fudge
 * configuration command is set to 1, the driver will retrieve this
 * table and write it to the clockstats file when the first timecode
 * message of a new day is received.
 *
 * PPS calibration fudge time 1: format 0 .003134, format 2 .004034
 */
/*
 * Interface definitions
 */
#define	DEVICE		"/dev/wwvb%d" /* device name and unit */
#define	SPEED232	B9600	/* uart speed (9600 baud) */
#define	PRECISION	(-13)	/* precision assumed (about 100 us) */
#define	PPS_PRECISION	(-13)	/* precision assumed (about 100 us) */
#define	REFID		"WWVB"	/* reference ID */
#define	DESCRIPTION	"Spectracom WWVB/GPS Receiver" /* WRU */

#define	LENWWVB0	22	/* format 0 timecode length */
#define	LENWWVB2	24	/* format 2 timecode length */
#define LENWWVB3	29	/* format 3 timecode length */
#define MONLIN		15	/* number of monitoring lines */

/*
 * WWVB unit control structure
 */
struct wwvbunit {
#ifdef HAVE_PPSAPI
	struct refclock_atom atom; /* PPSAPI structure */
	int	ppsapi_tried;	/* attempt PPSAPI once */
	int	ppsapi_lit;	/* time_pps_create() worked */
	int	tcount;		/* timecode sample counter */
	int	pcount;		/* PPS sample counter */
#endif /* HAVE_PPSAPI */
	l_fp	laststamp;	/* last <CR> timestamp */
	int	prev_eol_cr;	/* was last EOL <CR> (not <LF>)? */
	u_char	lasthour;	/* last hour (for monitor) */
	u_char	linect;		/* count ignored lines (for monitor */
};

/*
 * Function prototypes
 */
static	int	wwvb_start	(int, struct peer *);
static	void	wwvb_shutdown	(int, struct peer *);
static	void	wwvb_receive	(struct recvbuf *);
static	void	wwvb_poll	(int, struct peer *);
static	void	wwvb_timer	(int, struct peer *);
#ifdef HAVE_PPSAPI
static	void	wwvb_control	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
#define		WWVB_CONTROL	wwvb_control
#else
#define		WWVB_CONTROL	noentry
#endif /* HAVE_PPSAPI */

/*
 * Transfer vector
 */
struct	refclock refclock_wwvb = {
	wwvb_start,		/* start up driver */
	wwvb_shutdown,		/* shut down driver */
	wwvb_poll,		/* transmit poll message */
	WWVB_CONTROL,		/* fudge set/change notification */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old wwvb_buginfo) */
	wwvb_timer		/* called once per second */
};


/*
 * wwvb_start - open the devices and initialize data for processing
 */
static int
wwvb_start(
	int unit,
	struct peer *peer
	)
{
	register struct wwvbunit *up;
	struct refclockproc *pp;
	int fd;
	char device[20];

	/*
	 * Open serial port. Use CLK line discipline, if available.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = refclock_open(device, SPEED232, LDISC_CLK);
	if (fd <= 0)
		return (0);

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = wwvb_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(up);
		return (0);
	}
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy(&pp->refid, REFID, 4);
	return (1);
}


/*
 * wwvb_shutdown - shut down the clock
 */
static void
wwvb_shutdown(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *	pp;
	struct wwvbunit *	up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * wwvb_receive - receive data from the serial interface
 */
static void
wwvb_receive(
	struct recvbuf *rbufp
	)
{
	struct wwvbunit *up;
	struct refclockproc *pp;
	struct peer *peer;

	l_fp	trtmp;		/* arrival timestamp */
	int	tz;		/* time zone */
	int	day, month;	/* ddd conversion */
	int	temp;		/* int temp */
	char	syncchar;	/* synchronization indicator */
	char	qualchar;	/* quality indicator */
	char	leapchar;	/* leap indicator */
	char	dstchar;	/* daylight/standard indicator */
	char	tmpchar;	/* trashbin */

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	temp = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &trtmp);

	/*
	 * Note we get a buffer and timestamp for both a <cr> and <lf>,
	 * but only the <cr> timestamp is retained. Note: in format 0 on
	 * a Netclock/2 or upgraded 8170 the start bit is delayed 100
	 * +-50 us relative to the pps; however, on an unmodified 8170
	 * the start bit can be delayed up to 10 ms. In format 2 the
	 * reading precision is only to the millisecond. Thus, unless
	 * you have a PPS gadget and don't have to have the year, format
	 * 0 provides the lowest jitter.
	 * Save the timestamp of each <CR> in up->laststamp.  Lines with
	 * no characters occur for every <LF>, and for some <CR>s when
	 * format 0 is used. Format 0 starts and ends each cycle with a
	 * <CR><LF> pair, format 2 starts each cycle with its only pair.
	 * The preceding <CR> is the on-time character for both formats.
	 * The timestamp provided with non-empty lines corresponds to
	 * the <CR> following the timecode, which is ultimately not used
	 * with format 0 and is used for the following timecode for
	 * format 2.
	 */
	if (temp == 0) {
		if (up->prev_eol_cr) {
			DPRINTF(2, ("wwvb: <LF> @ %s\n",
				    prettydate(&trtmp)));
		} else {
			up->laststamp = trtmp;
			DPRINTF(2, ("wwvb: <CR> @ %s\n", 
				    prettydate(&trtmp)));
		}
		up->prev_eol_cr = !up->prev_eol_cr;
		return;
	}
	pp->lencode = temp;
	pp->lastrec = up->laststamp;
	up->laststamp = trtmp;
	up->prev_eol_cr = TRUE;
	DPRINTF(2, ("wwvb: code @ %s\n"
		    "       using %s minus one char\n",
		    prettydate(&trtmp), prettydate(&pp->lastrec)));
	if (L_ISZERO(&pp->lastrec))
		return;

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. This code uses the timecode length to determine
	 * format 0, 2 or 3. If the timecode has invalid length or is
	 * not in proper format, we declare bad format and exit.
	 */
	syncchar = qualchar = leapchar = dstchar = ' ';
	tz = 0;
	switch (pp->lencode) {

	case LENWWVB0:

		/*
		 * Timecode format 0: "I  ddd hh:mm:ss DTZ=nn"
		 */
		if (sscanf(pp->a_lastcode,
		    "%c %3d %2d:%2d:%2d%c%cTZ=%2d",
		    &syncchar, &pp->day, &pp->hour, &pp->minute,
		    &pp->second, &tmpchar, &dstchar, &tz) == 8) {
			pp->nsec = 0;
			break;
		}
		goto bad_format;

	case LENWWVB2:

		/*
		 * Timecode format 2: "IQyy ddd hh:mm:ss.mmm LD" */
		if (sscanf(pp->a_lastcode,
		    "%c%c %2d %3d %2d:%2d:%2d.%3ld %c",
		    &syncchar, &qualchar, &pp->year, &pp->day,
		    &pp->hour, &pp->minute, &pp->second, &pp->nsec,
		    &leapchar) == 9) {
			pp->nsec *= 1000000;
			break;
		}
		goto bad_format;

	case LENWWVB3:

		/*
		 * Timecode format 3: "0003I yyyymmdd hhmmss+0000SL#"
		 * WARNING: Undocumented, and the on-time character # is
		 * not yet handled correctly by this driver.  It may be
		 * as simple as compensating for an additional 1/960 s.
		 */
		if (sscanf(pp->a_lastcode,
		    "0003%c %4d%2d%2d %2d%2d%2d+0000%c%c",
		    &syncchar, &pp->year, &month, &day, &pp->hour,
		    &pp->minute, &pp->second, &dstchar, &leapchar) == 8)
		    {
			pp->day = ymd2yd(pp->year, month, day);
			pp->nsec = 0;
			break;
		}
		goto bad_format;

	default:
	bad_format:

		/*
		 * Unknown format: If dumping internal table, record
		 * stats; otherwise, declare bad format.
		 */
		if (up->linect > 0) {
			up->linect--;
			record_clock_stats(&peer->srcadr,
			    pp->a_lastcode);
		} else {
			refclock_report(peer, CEVNT_BADREPLY);
		}
		return;
	}

	/*
	 * Decode synchronization, quality and leap characters. If
	 * unsynchronized, set the leap bits accordingly and exit.
	 * Otherwise, set the leap bits according to the leap character.
	 * Once synchronized, the dispersion depends only on the
	 * quality character.
	 */
	switch (qualchar) {

	case ' ':
		pp->disp = .001;
		pp->lastref = pp->lastrec;
		break;

	case 'A':
		pp->disp = .01;
		break;

	case 'B':
		pp->disp = .1;
		break;

	case 'C':
		pp->disp = .5;
		break;

	case 'D':
		pp->disp = MAXDISPERSE;
		break;

	default:
		pp->disp = MAXDISPERSE;
		refclock_report(peer, CEVNT_BADREPLY);
		break;
	}
	if (syncchar != ' ')
		pp->leap = LEAP_NOTINSYNC;
	else if (leapchar == 'L')
		pp->leap = LEAP_ADDSECOND;
	else
		pp->leap = LEAP_NOWARNING;

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp, but only if the PPS is not in control.
	 */
#ifdef HAVE_PPSAPI
	up->tcount++;
	if (peer->flags & FLAG_PPS)
		return;

#endif /* HAVE_PPSAPI */
	if (!refclock_process_f(pp, pp->fudgetime2))
		refclock_report(peer, CEVNT_BADTIME);
}


/*
 * wwvb_timer - called once per second by the transmit procedure
 */
static void
wwvb_timer(
	int unit,
	struct peer *peer
	)
{
	register struct wwvbunit *up;
	struct refclockproc *pp;
	char	pollchar;	/* character sent to clock */
#ifdef DEBUG
	l_fp	now;
#endif

	/*
	 * Time to poll the clock. The Spectracom clock responds to a
	 * 'T' by returning a timecode in the format(s) specified above.
	 * Note there is no checking on state, since this may not be the
	 * only customer reading the clock. Only one customer need poll
	 * the clock; all others just listen in.
	 */
	pp = peer->procptr;
	up = pp->unitptr;
	if (up->linect > 0)
		pollchar = 'R';
	else
		pollchar = 'T';
	if (write(pp->io.fd, &pollchar, 1) != 1)
		refclock_report(peer, CEVNT_FAULT);
#ifdef DEBUG
	get_systime(&now);
	if (debug)
		printf("%c poll at %s\n", pollchar, prettydate(&now));
#endif
#ifdef HAVE_PPSAPI
	if (up->ppsapi_lit &&
	    refclock_pps(peer, &up->atom, pp->sloppyclockflag) > 0) {
		up->pcount++,
		peer->flags |= FLAG_PPS;
		peer->precision = PPS_PRECISION;
	}
#endif /* HAVE_PPSAPI */
}


/*
 * wwvb_poll - called by the transmit procedure
 */
static void
wwvb_poll(
	int unit,
	struct peer *peer
	)
{
	register struct wwvbunit *up;
	struct refclockproc *pp;

	/*
	 * Sweep up the samples received since the last poll. If none
	 * are received, declare a timeout and keep going.
	 */
	pp = peer->procptr;
	up = pp->unitptr;
	pp->polls++;

	/*
	 * If the monitor flag is set (flag4), we dump the internal
	 * quality table at the first timecode beginning the day.
	 */
	if (pp->sloppyclockflag & CLK_FLAG4 && pp->hour <
	    (int)up->lasthour)
		up->linect = MONLIN;
	up->lasthour = (u_char)pp->hour;

	/*
	 * Process median filter samples. If none received, declare a
	 * timeout and keep going.
	 */
#ifdef HAVE_PPSAPI
	if (up->pcount == 0) {
		peer->flags &= ~FLAG_PPS;
		peer->precision = PRECISION;
	}
	if (up->tcount == 0) {
		pp->coderecv = pp->codeproc;
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	up->pcount = up->tcount = 0;
#else /* HAVE_PPSAPI */
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
#endif /* HAVE_PPSAPI */
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
#ifdef DEBUG
	if (debug)
		printf("wwvb: timecode %d %s\n", pp->lencode,
		    pp->a_lastcode);
#endif
}


/*
 * wwvb_control - fudge parameters have been set or changed
 */
#ifdef HAVE_PPSAPI
static void
wwvb_control(
	int unit,
	const struct refclockstat *in_st,
	struct refclockstat *out_st,
	struct peer *peer
	)
{
	register struct wwvbunit *up;
	struct refclockproc *pp;
	
	pp = peer->procptr;
	up = pp->unitptr;

	if (!(pp->sloppyclockflag & CLK_FLAG1)) {
		if (!up->ppsapi_tried)
			return;
		up->ppsapi_tried = 0;
		if (!up->ppsapi_lit)
			return;
		peer->flags &= ~FLAG_PPS;
		peer->precision = PRECISION;
		time_pps_destroy(up->atom.handle);
		up->atom.handle = 0;
		up->ppsapi_lit = 0;
		return;
	}

	if (up->ppsapi_tried)
		return;
	/*
	 * Light up the PPSAPI interface.
	 */
	up->ppsapi_tried = 1;
	if (refclock_ppsapi(pp->io.fd, &up->atom)) {
		up->ppsapi_lit = 1;
		return;
	}

	msyslog(LOG_WARNING, "%s flag1 1 but PPSAPI fails",
		refnumtoa(&peer->srcadr));
}
#endif	/* HAVE_PPSAPI */

#else
int refclock_wwvb_bs;
#endif /* REFCLOCK */
