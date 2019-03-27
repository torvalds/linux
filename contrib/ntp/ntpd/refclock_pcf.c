/*
 * refclock_pcf - clock driver for the Conrad parallel port radio clock
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_PCF)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

/*
 * This driver supports the parallel port radio clock sold by Conrad
 * Electronic under order numbers 967602 and 642002.
 *
 * It requires that the local timezone be CET/CEST and that the pcfclock
 * device driver be installed.  A device driver for Linux is available at
 * http://home.pages.de/~voegele/pcf.html.  Information about a FreeBSD
 * driver is available at http://schumann.cx/pcfclock/.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/pcfclocks/%d"
#define	OLDDEVICE	"/dev/pcfclock%d"
#define	PRECISION	(-1)	/* precision assumed (about 0.5 s) */
#define REFID		"PCF"
#define DESCRIPTION	"Conrad parallel port radio clock"

#define LENPCF		18	/* timecode length */

/*
 * Function prototypes
 */
static	int 	pcf_start 		(int, struct peer *);
static	void	pcf_shutdown		(int, struct peer *);
static	void	pcf_poll		(int, struct peer *);

/*
 * Transfer vector
 */
struct  refclock refclock_pcf = {
	pcf_start,              /* start up driver */
	pcf_shutdown,           /* shut down driver */
	pcf_poll,               /* transmit poll message */
	noentry,                /* not used */
	noentry,                /* initialize driver (not used) */
	noentry,                /* not used */
	NOFLAGS                 /* not used */
};


/*
 * pcf_start - open the device and initialize data for processing
 */
static int
pcf_start(
     	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	int fd;
	char device[128];

	/*
	 * Open device file for reading.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = open(device, O_RDONLY);
	if (fd == -1) {
		snprintf(device, sizeof(device), OLDDEVICE, unit);
		fd = open(device, O_RDONLY);
	}
#ifdef DEBUG
	if (debug)
		printf ("starting PCF with device %s\n",device);
#endif
	if (fd == -1) {
		return (0);
	}
	
	pp = peer->procptr;
	pp->io.clock_recv = noentry;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	
	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	/* one transmission takes 172.5 milliseconds since the radio clock
	   transmits 69 bits with a period of 2.5 milliseconds per bit */
	pp->fudgetime1 = 0.1725;
	memcpy((char *)&pp->refid, REFID, 4);

	return (1);
}


/*
 * pcf_shutdown - shut down the clock
 */
static void
pcf_shutdown(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	
	pp = peer->procptr;
	if (NULL != pp)
		close(pp->io.fd);
}


/*
 * pcf_poll - called by the transmit procedure
 */
static void
pcf_poll(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	char buf[LENPCF];
	struct tm tm, *tp;
	time_t t;
	
	pp = peer->procptr;

	buf[0] = 0;
	if (read(pp->io.fd, buf, sizeof(buf)) < (ssize_t)sizeof(buf) || buf[0] != 9) {
		refclock_report(peer, CEVNT_FAULT);
		return;
	}

	ZERO(tm);

	tm.tm_mday = buf[11] * 10 + buf[10];
	tm.tm_mon = buf[13] * 10 + buf[12] - 1;
	tm.tm_year = buf[15] * 10 + buf[14];
	tm.tm_hour = buf[7] * 10 + buf[6];
	tm.tm_min = buf[5] * 10 + buf[4];
	tm.tm_sec = buf[3] * 10 + buf[2];
	tm.tm_isdst = (buf[8] & 1) ? 1 : (buf[8] & 2) ? 0 : -1;

	/*
	 * Y2K convert the 2-digit year
	 */
	if (tm.tm_year < 99)
		tm.tm_year += 100;
	
	t = mktime(&tm);
	if (t == (time_t) -1) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}

#if defined(__GLIBC__) && defined(_BSD_SOURCE)
	if ((tm.tm_isdst > 0 && tm.tm_gmtoff != 7200)
	    || (tm.tm_isdst == 0 && tm.tm_gmtoff != 3600)
	    || tm.tm_isdst < 0) {
#ifdef DEBUG
		if (debug)
			printf ("local time zone not set to CET/CEST\n");
#endif
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
#endif

	pp->lencode = strftime(pp->a_lastcode, BMAX, "%Y %m %d %H %M %S", &tm);

#if defined(_REENTRANT) || defined(_THREAD_SAFE)
	tp = gmtime_r(&t, &tm);
#else
	tp = gmtime(&t);
#endif
	if (!tp) {
		refclock_report(peer, CEVNT_FAULT);
		return;
	}

	get_systime(&pp->lastrec);
	pp->polls++;
	pp->year = tp->tm_year + 1900;
	pp->day = tp->tm_yday + 1;
	pp->hour = tp->tm_hour;
	pp->minute = tp->tm_min;
	pp->second = tp->tm_sec;
	pp->nsec = buf[16] * 31250000;
	if (buf[17] & 1)
		pp->nsec += 500000000;

#ifdef DEBUG
	if (debug)
		printf ("pcf%d: time is %04d/%02d/%02d %02d:%02d:%02d UTC\n",
			unit, pp->year, tp->tm_mon + 1, tp->tm_mday, pp->hour,
			pp->minute, pp->second);
#endif

	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	if ((buf[1] & 1) && !(pp->sloppyclockflag & CLK_FLAG2))
		pp->leap = LEAP_NOTINSYNC;
	else
		pp->leap = LEAP_NOWARNING;
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
}
#else
int refclock_pcf_bs;
#endif /* REFCLOCK */
