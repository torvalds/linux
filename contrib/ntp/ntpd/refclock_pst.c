/*
 * refclock_pst - clock driver for PSTI/Traconex WWV/WWVH receivers
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PST)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

/*
 * This driver supports the PSTI 1010 and Traconex 1020 WWV/WWVH
 * Receivers. No specific claim of accuracy is made for these receiver,
 * but actual experience suggests that 10 ms would be a conservative
 * assumption.
 * 
 * The DIPswitches should be set for 9600 bps line speed, 24-hour day-
 * of-year format and UTC time zone. Automatic correction for DST should
 * be disabled. It is very important that the year be set correctly in
 * the DIPswitches; otherwise, the day of year will be incorrect after
 * 28 April of a normal or leap year. The propagation delay DIPswitches
 * should be set according to the distance from the transmitter for both
 * WWV and WWVH, as described in the instructions. While the delay can
 * be set only to within 11 ms, the fudge time1 parameter can be used
 * for vernier corrections.
 *
 * Using the poll sequence QTQDQM, the response timecode is in three
 * sections totalling 50 ASCII printing characters, as concatenated by
 * the driver, in the following format:
 *
 * ahh:mm:ss.fffs<cr> yy/dd/mm/ddd<cr> frdzycchhSSFTttttuuxx<cr>
 *
 *	on-time = first <cr>
 *	hh:mm:ss.fff = hours, minutes, seconds, milliseconds
 *	a = AM/PM indicator (' ' for 24-hour mode)
 *	yy = year (from internal switches)
 *	dd/mm/ddd = day of month, month, day of year
 *	s = daylight-saving indicator (' ' for 24-hour mode)
 *	f = frequency enable (O = all frequencies enabled)
 *	r = baud rate (3 = 1200, 6 = 9600)
 *	d = features indicator (@ = month/day display enabled)
 *	z = time zone (0 = UTC)
 *	y = year (5 = 91)
 *	cc = WWV propagation delay (52 = 22 ms)
 *	hh = WWVH propagation delay (81 = 33 ms)
 *	SS = status (80 or 82 = operating correctly)
 *	F = current receive frequency (4 = 15 MHz)
 *	T = transmitter (C = WWV, H = WWVH)
 *	tttt = time since last update (0000 = minutes)
 *	uu = flush character (03 = ^c)
 *	xx = 94 (unknown)
 *
 * The alarm condition is indicated by other than '8' at A, which occurs
 * during initial synchronization and when received signal is lost for
 * an extended period; unlock condition is indicated by other than
 * "0000" in the tttt subfield at Q.
 *
 * Fudge Factors
 *
 * There are no special fudge factors other than the generic.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/wwv%d" /* device name and unit */
#define	SPEED232	B9600	/* uart speed (9600 baud) */
#define	PRECISION	(-10)	/* precision assumed (about 1 ms) */
#define	WWVREFID	"WWV\0"	/* WWV reference ID */
#define	WWVHREFID	"WWVH"	/* WWVH reference ID */
#define	DESCRIPTION	"PSTI/Traconex WWV/WWVH Receiver" /* WRU */
#define PST_PHI		(10e-6)	/* max clock oscillator offset */
#define LENPST		46	/* min timecode length */

/*
 * Unit control structure
 */
struct pstunit {
	int	tcswitch;	/* timecode switch */
	char	*lastptr;	/* pointer to timecode data */
};

/*
 * Function prototypes
 */
static	int	pst_start	(int, struct peer *);
static	void	pst_shutdown	(int, struct peer *);
static	void	pst_receive	(struct recvbuf *);
static	void	pst_poll	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_pst = {
	pst_start,		/* start up driver */
	pst_shutdown,		/* shut down driver */
	pst_poll,		/* transmit poll message */
	noentry,		/* not used (old pst_control) */
	noentry,		/* initialize driver */
	noentry,		/* not used (old pst_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * pst_start - open the devices and initialize data for processing
 */
static int
pst_start(
	int unit,
	struct peer *peer
	)
{
	register struct pstunit *up;
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
	pp->io.clock_recv = pst_receive;
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
	memcpy((char *)&pp->refid, WWVREFID, 4);
	return (1);
}


/*
 * pst_shutdown - shut down the clock
 */
static void
pst_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct pstunit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * pst_receive - receive data from the serial interface
 */
static void
pst_receive(
	struct recvbuf *rbufp
	)
{
	register struct pstunit *up;
	struct refclockproc *pp;
	struct peer *peer;
	l_fp trtmp;
	u_long ltemp;
	char ampmchar;		/* AM/PM indicator */
	char daychar;		/* standard/daylight indicator */
	char junque[10];	/* "yy/dd/mm/" discard */
	char info[14];		/* "frdzycchhSSFT" clock info */

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	up->lastptr += refclock_gtlin(rbufp, up->lastptr, pp->a_lastcode
	    + BMAX - 2 - up->lastptr, &trtmp);
	*up->lastptr++ = ' ';
	*up->lastptr = '\0';

	/*
	 * Note we get a buffer and timestamp for each <cr>, but only
	 * the first timestamp is retained.
	 */
	if (up->tcswitch == 0)
		pp->lastrec = trtmp;
	up->tcswitch++;
	pp->lencode = up->lastptr - pp->a_lastcode;
	if (up->tcswitch < 3)
		return;

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. If the timecode has invalid length or is not in
	 * proper format, we declare bad format and exit.
	 */
	if (pp->lencode < LENPST) {
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/*
	 * Timecode format:
	 * "ahh:mm:ss.fffs yy/dd/mm/ddd frdzycchhSSFTttttuuxx"
	 */
	if (sscanf(pp->a_lastcode,
	    "%c%2d:%2d:%2d.%3ld%c %9s%3d%13s%4ld",
	    &ampmchar, &pp->hour, &pp->minute, &pp->second, &pp->nsec,
	    &daychar, junque, &pp->day, info, &ltemp) != 10) {
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}
	pp->nsec *= 1000000;

	/*
	 * Decode synchronization, quality and last update. If
	 * unsynchronized, set the leap bits accordingly and exit. Once
	 * synchronized, the dispersion depends only on when the clock
	 * was last heard, which depends on the time since last update,
	 * as reported by the clock.
	 */
	if (info[9] != '8')
		pp->leap = LEAP_NOTINSYNC;
	if (info[12] == 'H')
		memcpy((char *)&pp->refid, WWVHREFID, 4);
	else
		memcpy((char *)&pp->refid, WWVREFID, 4);
	if (peer->stratum <= 1)
		peer->refid = pp->refid;
	if (ltemp == 0)
		pp->lastref = pp->lastrec;
	pp->disp = PST_PHI * ltemp * 60;

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp.
	 */
	if (!refclock_process(pp))
		refclock_report(peer, CEVNT_BADTIME);
	else if (peer->disp > MAXDISTANCE)
		refclock_receive(peer);
}


/*
 * pst_poll - called by the transmit procedure
 */
static void
pst_poll(
	int unit,
	struct peer *peer
	)
{
	register struct pstunit *up;
	struct refclockproc *pp;

	/*
	 * Time to poll the clock. The PSTI/Traconex clock responds to a
	 * "QTQDQMT" by returning a timecode in the format specified
	 * above. Note there is no checking on state, since this may not
	 * be the only customer reading the clock. Only one customer
	 * need poll the clock; all others just listen in. If the clock
	 * becomes unreachable, declare a timeout and keep going.
	 */
	pp = peer->procptr;
	up = pp->unitptr;
	up->tcswitch = 0;
	up->lastptr = pp->a_lastcode;
	if (write(pp->io.fd, "QTQDQMT", 6) != 6)
		refclock_report(peer, CEVNT_FAULT);
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
#ifdef DEBUG
	if (debug)
		printf("pst: timecode %d %s\n", pp->lencode,
		    pp->a_lastcode);
#endif
	pp->polls++;
}

#else
int refclock_pst_int;
#endif /* REFCLOCK */
