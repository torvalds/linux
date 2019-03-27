/*
 * refclock_tpro - clock driver for the KSI/Odetics TPRO-S IRIG-B reader
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_TPRO)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "sys/tpro.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

/*
 * This driver supports the KSI/Odetecs TPRO-S IRIG-B reader and TPRO-
 * SAT GPS receiver for the Sun Microsystems SBus. It requires that the
 * tpro.o device driver be installed and loaded.
 */ 

/*
 * TPRO interface definitions
 */
#define	DEVICE		 "/dev/tpro%d" /* device name and unit */
#define	PRECISION	(-20)	/* precision assumed (1 us) */
#define	REFID		"IRIG"	/* reference ID */
#define	DESCRIPTION	"KSI/Odetics TPRO/S IRIG Interface" /* WRU */

/*
 * Unit control structure
 */
struct tprounit {
	struct	tproval tprodata; /* data returned from tpro read */
};

/*
 * Function prototypes
 */
static	int	tpro_start	(int, struct peer *);
static	void	tpro_shutdown	(int, struct peer *);
static	void	tpro_poll	(int unit, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_tpro = {
	tpro_start,		/* start up driver */
	tpro_shutdown,		/* shut down driver */
	tpro_poll,		/* transmit poll message */
	noentry,		/* not used (old tpro_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old tpro_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * tpro_start - open the TPRO device and initialize data for processing
 */
static int
tpro_start(
	int unit,
	struct peer *peer
	)
{
	register struct tprounit *up;
	struct refclockproc *pp;
	char device[20];
	int fd;

	/*
	 * Open TPRO device
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = open(device, O_RDONLY | O_NDELAY, 0777);
	if (fd == -1) {
		msyslog(LOG_ERR, "tpro_start: open of %s: %m", device);
		return (0);
	}

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = noentry;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous peer variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	return (1);
}


/*
 * tpro_shutdown - shut down the clock
 */
static void
tpro_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct tprounit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * tpro_poll - called by the transmit procedure
 */
static void
tpro_poll(
	int unit,
	struct peer *peer
	)
{
	register struct tprounit *up;
	struct refclockproc *pp;
	struct tproval *tp;

	/*
	 * This is the main routine. It snatches the time from the TPRO
	 * board and tacks on a local timestamp.
	 */
	pp = peer->procptr;
	up = pp->unitptr;

	tp = &up->tprodata;
	if (read(pp->io.fd, (char *)tp, sizeof(struct tproval)) < 0) {
		refclock_report(peer, CEVNT_FAULT);
		return;
	}
	get_systime(&pp->lastrec);
	pp->polls++;

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. If the timecode has invalid length or is not in
	 * proper format, we declare bad format and exit. Note: we
	 * can't use the sec/usec conversion produced by the driver,
	 * since the year may be suspect. All format error checking is
	 * done by the snprintf() and sscanf() routines.
	 *
	 * Note that the refclockproc usec member has now become nsec.
	 * We could either multiply the read-in usec value by 1000 or
	 * we could pad the written string appropriately and read the
	 * resulting value in already scaled.
	 */
	snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
		 "%1x%1x%1x %1x%1x:%1x%1x:%1x%1x.%1x%1x%1x%1x%1x%1x %1x",
		 tp->day100, tp->day10, tp->day1, tp->hour10, tp->hour1,
		 tp->min10, tp->min1, tp->sec10, tp->sec1, tp->ms100,
		 tp->ms10, tp->ms1, tp->usec100, tp->usec10, tp->usec1,
		 tp->status);
	pp->lencode = strlen(pp->a_lastcode);
#ifdef DEBUG
	if (debug)
		printf("tpro: time %s timecode %d %s\n",
		   ulfptoa(&pp->lastrec, 6), pp->lencode,
		   pp->a_lastcode);
#endif
	if (sscanf(pp->a_lastcode, "%3d %2d:%2d:%2d.%6ld", &pp->day,
	    &pp->hour, &pp->minute, &pp->second, &pp->nsec)
	    != 5) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	pp->nsec *= 1000;	/* Convert usec to nsec */
	if (!tp->status & 0x3)
		pp->leap = LEAP_NOTINSYNC;
	else
		pp->leap = LEAP_NOWARNING;
	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	pp->lastref = pp->lastrec;
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	refclock_receive(peer);
}

#else
int refclock_tpro_bs;
#endif /* REFCLOCK */
