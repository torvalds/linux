/*
 * refclock_chronolog - clock driver for Chronolog K-series WWVB receiver.
 */

/*
 * Must interpolate back to local time.  Very annoying.
 */
#define GET_LOCALTIME

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_CHRONOLOG)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

/*
 * This driver supports the Chronolog K-series WWVB receiver.
 *
 * Input format:
 *
 *	Y YY/MM/DD<cr><lf>
 *      Z hh:mm:ss<cr><lf>
 *
 * YY/MM/DD -- what you'd expect.  This arrives a few seconds before the
 * timestamp.
 * hh:mm:ss -- what you'd expect.  We take time on the <cr>.
 *
 * Our Chronolog writes time out at 2400 bps 8/N/1, but it can be configured
 * otherwise.  The clock seems to appear every 60 seconds, which doesn't make
 * for good statistics collection.
 *
 * The original source of this module was the WWVB module.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/chronolog%d" /* device name and unit */
#define	SPEED232	B2400	/* uart speed (2400 baud) */
#define	PRECISION	(-13)	/* precision assumed (about 100 us) */
#define	REFID		"chronolog"	/* reference ID */
#define	DESCRIPTION	"Chrono-log K" /* WRU */

#define MONLIN		15	/* number of monitoring lines */

/*
 * Chrono-log unit control structure
 */
struct chronolog_unit {
	u_char	tcswitch;	/* timecode switch */
	l_fp	laststamp;	/* last receive timestamp */
	u_char	lasthour;	/* last hour (for monitor) */
	int   	year;	        /* Y2K-adjusted year */
	int   	day;	        /* day-of-month */
        int   	month;	        /* month-of-year */
};

/*
 * Function prototypes
 */
static	int	chronolog_start		(int, struct peer *);
static	void	chronolog_shutdown	(int, struct peer *);
static	void	chronolog_receive	(struct recvbuf *);
static	void	chronolog_poll		(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_chronolog = {
	chronolog_start,	/* start up driver */
	chronolog_shutdown,	/* shut down driver */
	chronolog_poll,		/* poll the driver -- a nice fabrication */
	noentry,		/* not used */
	noentry,		/* not used */
	noentry,		/* not used */
	NOFLAGS			/* not used */
};


/*
 * chronolog_start - open the devices and initialize data for processing
 */
static int
chronolog_start(
	int unit,
	struct peer *peer
	)
{
	register struct chronolog_unit *up;
	struct refclockproc *pp;
	int fd;
	char device[20];

	/*
	 * Open serial port. Don't bother with CLK line discipline, since
	 * it's not available.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
#ifdef DEBUG
	if (debug)
		printf ("starting Chronolog with device %s\n",device);
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
	pp->io.clock_recv = chronolog_receive;
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

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	return (1);
}


/*
 * chronolog_shutdown - shut down the clock
 */
static void
chronolog_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct chronolog_unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * chronolog_receive - receive data from the serial interface
 */
static void
chronolog_receive(
	struct recvbuf *rbufp
	)
{
	struct chronolog_unit *up;
	struct refclockproc *pp;
	struct peer *peer;

	l_fp	     trtmp;	/* arrival timestamp */
	int          hours;	/* hour-of-day */
	int	     minutes;	/* minutes-past-the-hour */
	int          seconds;	/* seconds */
	int	     temp;	/* int temp */
	int          got_good;	/* got a good time flag */

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
	pp->lencode = temp;
	pp->lastrec = up->laststamp;
	up->laststamp = trtmp;
	up->tcswitch = 1;

#ifdef DEBUG
	if (debug)
		printf("chronolog: timecode %d %s\n", pp->lencode,
		    pp->a_lastcode);
#endif

	/*
	 * We get down to business. Check the timecode format and decode
	 * its contents. This code uses the first character to see whether
	 * we're looking at a date or a time.  We store data data across
	 * calls since it is transmitted a few seconds ahead of the
	 * timestamp.
	 */
	got_good=0;
	if (sscanf(pp->a_lastcode, "Y %d/%d/%d", &up->year,&up->month,&up->day))
	{
	    /*
	     * Y2K convert the 2-digit year
	     */
	    up->year = up->year >= 69 ? up->year : up->year + 100;
	    return;
	}
	if (sscanf(pp->a_lastcode,"Z %02d:%02d:%02d",
		   &hours,&minutes,&seconds) == 3)
	{
#ifdef GET_LOCALTIME
	    struct tm  local;
	    struct tm *gmtp;
	    time_t     unixtime;
	    int        adjyear;
	    int        adjmon;

	    /*
	     * Convert to GMT for sites that distribute localtime.  This
             * means we have to do Y2K conversion on the 2-digit year;
	     * otherwise, we get the time wrong.
	     */
	    
	    memset(&local, 0, sizeof(local));

	    local.tm_year  = up->year;
	    local.tm_mon   = up->month-1;
	    local.tm_mday  = up->day;
	    local.tm_hour  = hours;
	    local.tm_min   = minutes;
	    local.tm_sec   = seconds;
	    local.tm_isdst = -1;

	    unixtime = mktime (&local);
	    if ((gmtp = gmtime (&unixtime)) == NULL)
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
	    
#else
	    /*
	     * For more rational sites distributing UTC
	     */
	    pp->day    = ymd2yd(year+1900,month,day);
	    pp->hour   = hours;
	    pp->minute = minutes;
	    pp->second = seconds;

#endif
	    got_good=1;
	}

	if (!got_good)
	    return;


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


/*
 * chronolog_poll - called by the transmit procedure
 */
static void
chronolog_poll(
	int unit,
	struct peer *peer
	)
{
	/*
	 * Time to poll the clock. The Chrono-log clock is supposed to
	 * respond to a 'T' by returning a timecode in the format(s)
	 * specified above.  Ours does (can?) not, but this seems to be
	 * an installation-specific problem.  This code is dyked out,
	 * but may be re-enabled if anyone ever finds a Chrono-log that
	 * actually listens to this command.
	 */
#if 0
	register struct chronolog_unit *up;
	struct refclockproc *pp;
	char pollchar;

	pp = peer->procptr;
	up = pp->unitptr;
	if (peer->burst == 0 && peer->reach == 0)
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

#else
int refclock_chronolog_bs;
#endif /* REFCLOCK */
