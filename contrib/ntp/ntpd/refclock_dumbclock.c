/*
 * refclock_dumbclock - clock driver for a unknown time distribution system
 * that only provides hh:mm:ss (in local time, yet!).
 */

/*
 * Must interpolate back to local time.  Very annoying.
 */
#define GET_LOCALTIME

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_DUMBCLOCK)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#ifdef SYS_WINNT
extern int async_write(int, const void *, unsigned int);
#undef write
#define write(fd, data, octets)	async_write(fd, data, octets)
#endif

/*
 * This driver supports a generic dumb clock that only outputs hh:mm:ss,
 * in local time, no less.
 *
 * Input format:
 *
 *	hh:mm:ss   <cr>
 *
 * hh:mm:ss -- what you'd expect, with a 24 hour clock.  (Heck, that's the only
 * way it could get stupider.)  We take time on the <cr>.
 *
 * The original source of this module was the WWVB module.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/dumbclock%d" /* device name and unit */
#define	SPEED232	B9600	/* uart speed (9600 baud) */
#define	PRECISION	(-13)	/* precision assumed (about 100 us) */
#define	REFID		"dumbclock"	/* reference ID */
#define	DESCRIPTION	"Dumb clock" /* WRU */


/*
 * Insanity check.  Since the time is local, we need to make sure that during midnight
 * transitions, we can convert back to Unix time.  If the conversion results in some number
 * worse than this number of seconds away, assume the next day and retry.
 */
#define INSANE_SECONDS 3600

/*
 * Dumb clock control structure
 */
struct dumbclock_unit {
	u_char	  tcswitch;	/* timecode switch */
	l_fp	  laststamp;	/* last receive timestamp */
	u_char	  lasthour;	/* last hour (for monitor) */
	u_char	  linect;	/* count ignored lines (for monitor */
	struct tm ymd;		/* struct tm for y/m/d only */
};

/*
 * Function prototypes
 */
static	int	dumbclock_start		(int, struct peer *);
static	void	dumbclock_shutdown	(int, struct peer *);
static	void	dumbclock_receive	(struct recvbuf *);
#if 0
static	void	dumbclock_poll		(int, struct peer *);
#endif

/*
 * Transfer vector
 */
struct	refclock refclock_dumbclock = {
	dumbclock_start,		     /* start up driver */
	dumbclock_shutdown,		     /* shut down driver */
	noentry,			     /* poll the driver -- a nice fabrication */
	noentry,			     /* not used */
	noentry,			     /* not used */
	noentry,			     /* not used */
	NOFLAGS				     /* not used */
};


/*
 * dumbclock_start - open the devices and initialize data for processing
 */
static int
dumbclock_start(
	int unit,
	struct peer *peer
	)
{
	register struct dumbclock_unit *up;
	struct refclockproc *pp;
	int fd;
	char device[20];
	struct tm *tm_time_p;
	time_t     now;

	/*
	 * Open serial port. Don't bother with CLK line discipline, since
	 * it's not available.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
#ifdef DEBUG
	if (debug)
		printf ("starting Dumbclock with device %s\n",device);
#endif
	fd = refclock_open(device, SPEED232, 0);
	if (fd <= 0)
		return (0);

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->unitptr = up;
	pp->io.clock_recv = dumbclock_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(up);
		pp->unitptr = NULL;
		return (0);
	}


	time(&now);
#ifdef GET_LOCALTIME
	tm_time_p = localtime(&now);
#else
	tm_time_p = gmtime(&now);
#endif
	if (tm_time_p)
		up->ymd = *tm_time_p;
	else
		return 0;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	return (1);
}


/*
 * dumbclock_shutdown - shut down the clock
 */
static void
dumbclock_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct dumbclock_unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * dumbclock_receive - receive data from the serial interface
 */
static void
dumbclock_receive(
	struct recvbuf *rbufp
	)
{
	struct dumbclock_unit *up;
	struct refclockproc *pp;
	struct peer *peer;

	l_fp	trtmp;		/* arrival timestamp */
	int	hours;		/* hour-of-day */
	int	minutes;	/* minutes-past-the-hour */
	int	seconds;	/* seconds */
	int	temp;		/* int temp */
	int	got_good;	/* got a good time flag */

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	temp = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &trtmp);

	if (temp == 0) {
		if (up->tcswitch == 0) {
			up->tcswitch = 1;
			up->laststamp = trtmp;
		} else
			up->tcswitch = 0;
		return;
	}
	pp->lencode = (u_short)temp;
	pp->lastrec = up->laststamp;
	up->laststamp = trtmp;
	up->tcswitch = 1;

#ifdef DEBUG
	if (debug)
		printf("dumbclock: timecode %d %s\n",
		       pp->lencode, pp->a_lastcode);
#endif

	/*
	 * We get down to business. Check the timecode format...
	 */
	got_good=0;
	if (sscanf(pp->a_lastcode,"%02d:%02d:%02d",
		   &hours,&minutes,&seconds) == 3)
	{
	    struct tm *gmtp;
	    struct tm *lt_p;
	    time_t     asserted_time;	     /* the SPM time based on the composite time+date */
	    struct tm  asserted_tm;	     /* the struct tm of the same */
	    int        adjyear;
	    int        adjmon;
	    time_t     reality_delta;
	    time_t     now;


	    /*
	     * Convert to GMT for sites that distribute localtime.  This
	     * means we have to figure out what day it is.  Easier said
	     * than done...
	     */

	    memset(&asserted_tm, 0, sizeof(asserted_tm));

	    asserted_tm.tm_year  = up->ymd.tm_year;
	    asserted_tm.tm_mon   = up->ymd.tm_mon;
	    asserted_tm.tm_mday  = up->ymd.tm_mday;
	    asserted_tm.tm_hour  = hours;
	    asserted_tm.tm_min   = minutes;
	    asserted_tm.tm_sec   = seconds;
	    asserted_tm.tm_isdst = -1;

#ifdef GET_LOCALTIME
	    asserted_time = mktime (&asserted_tm);
	    time(&now);
#else
#include "GMT unsupported for dumbclock!"
#endif
	    reality_delta = asserted_time - now;

	    /*
	     * We assume that if the time is grossly wrong, it's because we got the
	     * year/month/day wrong.
	     */
	    if (reality_delta > INSANE_SECONDS)
	    {
		asserted_time -= SECSPERDAY; /* local clock behind real time */
	    }
	    else if (-reality_delta > INSANE_SECONDS)
	    {
		asserted_time += SECSPERDAY; /* local clock ahead of real time */
	    }
	    lt_p = localtime(&asserted_time);
	    if (lt_p)
	    {
		up->ymd = *lt_p;
	    }
	    else
	    {
		refclock_report (peer, CEVNT_FAULT);
		return;
	    }

	    if ((gmtp = gmtime (&asserted_time)) == NULL)
	    {
		refclock_report (peer, CEVNT_FAULT);
		return;
	    }
	    adjyear = gmtp->tm_year+1900;
	    adjmon  = gmtp->tm_mon+1;
	    pp->day = ymd2yd (adjyear, adjmon, gmtp->tm_mday);
	    pp->hour   = gmtp->tm_hour;
	    pp->minute = gmtp->tm_min;
	    pp->second = gmtp->tm_sec;
#ifdef DEBUG
	    if (debug)
		printf ("time is %04d/%02d/%02d %02d:%02d:%02d UTC\n",
			adjyear,adjmon,gmtp->tm_mday,pp->hour,pp->minute,
			pp->second);
#endif

	    got_good=1;
	}

	if (!got_good)
	{
	    if (up->linect > 0)
	    	up->linect--;
	    else
	    	refclock_report(peer, CEVNT_BADREPLY);
	    return;
	}

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp.
	 */
	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	up->lasthour = (u_char)pp->hour;
}

#if 0
/*
 * dumbclock_poll - called by the transmit procedure
 */
static void
dumbclock_poll(
	int unit,
	struct peer *peer
	)
{
	register struct dumbclock_unit *up;
	struct refclockproc *pp;
	char pollchar;

	/*
	 * Time to poll the clock. The Chrono-log clock is supposed to
	 * respond to a 'T' by returning a timecode in the format(s)
	 * specified above.  Ours does (can?) not, but this seems to be
	 * an installation-specific problem.  This code is dyked out,
	 * but may be re-enabled if anyone ever finds a Chrono-log that
	 * actually listens to this command.
	 */
#if 0
	pp = peer->procptr;
	up = pp->unitptr;
	if (peer->reach == 0)
		refclock_report(peer, CEVNT_TIMEOUT);
	if (up->linect > 0)
		pollchar = 'R';
	else
		pollchar = 'T';
	if (write(pp->io.fd, &pollchar, 1) != 1)
		refclock_report(peer, CEVNT_FAULT);
	else
		pp->polls++;
#endif
}
#endif

#else
int refclock_dumbclock_bs;
#endif	/* defined(REFCLOCK) && defined(CLOCK_DUMBCLOCK) */
