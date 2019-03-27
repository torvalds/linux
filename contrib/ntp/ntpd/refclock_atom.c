/*
 * refclock_atom - clock driver for 1-pps signals
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_unixtime.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

/*
 * This driver requires the PPSAPI interface (RFC 2783)
 */
#if defined(REFCLOCK) && defined(CLOCK_ATOM) && defined(HAVE_PPSAPI)
#include "ppsapi_timepps.h"
#include "refclock_atom.h"

/*
 * This driver furnishes an interface for pulse-per-second (PPS) signals
 * produced by a cesium clock, timing receiver or related equipment. It
 * can be used to remove accumulated jitter over a congested link and
 * retime a server before redistributing the time to clients. It can 
 *also be used as a holdover should all other synchronization sources
 * beconme unreachable.
 *
 * Before this driver becomes active, the local clock must be set to
 * within +-0.4 s by another means, such as a radio clock or NTP
 * itself. There are two ways to connect the PPS signal, normally at TTL
 * levels, to the computer. One is to shift to EIA levels and connect to
 * pin 8 (DCD) of a serial port. This requires a level converter and
 * may require a one-shot flipflop to lengthen the pulse. The other is
 * to connect the PPS signal directly to pin 10 (ACK) of a PC paralell
 * port. These methods are architecture dependent.
 *
 * This driver requires the Pulse-per-Second API for Unix-like Operating
 * Systems, Version 1.0, RFC-2783 (PPSAPI). Implementations are
 * available for FreeBSD, Linux, SunOS, Solaris and Tru64. However, at
 * present only the Tru64 implementation provides the full generality of
 * the API with multiple PPS drivers and multiple handles per driver. If
 * the PPSAPI is normally implemented in the /usr/include/sys/timepps.h
 * header file and kernel support specific to each operating system.
 *
 * This driver normally uses the PLL/FLL clock discipline implemented in
 * the ntpd code. Ordinarily, this is the most accurate means, as the
 * median filter in the driver interface is much larger than in the
 * kernel. However, if the systemic clock frequency error is large (tens
 * to hundreds of PPM), it's better to used the kernel support, if
 * available.
 *
 * This deriver is subject to the mitigation rules described in the
 * "mitigation rulse and the prefer peer" page. However, there is an
 * important difference. If this driver becomes the PPS driver according
 * to these rules, it is acrive only if (a) a prefer peer other than
 * this driver is among the survivors or (b) there are no survivors and
 * the minsane option of the tos command is zero. This is intended to
 * support space missions where updates from other spacecraft are
 * infrequent, but a reliable PPS signal, such as from an Ultra Stable
 * Oscillator (USO) is available.
 *
 * Fudge Factors
 *
 * The PPS timestamp is captured on the rising (assert) edge if flag2 is
 * dim (default) and on the falling (clear) edge if lit. If flag3 is dim
 * (default), the kernel PPS support is disabled; if lit it is enabled.
 * If flag4 is lit, each timesampt is copied to the clockstats file for
 * later analysis. This can be useful when constructing Allan deviation
 * plots. The time1 parameter can be used to compensate for
 * miscellaneous device driver and OS delays.
 */
/*
 * Interface definitions
 */
#define DEVICE		"/dev/pps%d" /* device name and unit */
#define	PRECISION	(-20)	/* precision assumed (about 1 us) */
#define	REFID		"PPS\0"	/* reference ID */
#define	DESCRIPTION	"PPS Clock Discipline" /* WRU */

/*
 * PPS unit control structure
 */
struct ppsunit {
	struct refclock_atom atom; /* atom structure pointer */
	int	fddev;		/* file descriptor */
};

/*
 * Function prototypes
 */
static	int	atom_start	(int, struct peer *);
static	void	atom_shutdown	(int, struct peer *);
static	void	atom_poll	(int, struct peer *);
static	void	atom_timer	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_atom = {
	atom_start,		/* start up driver */
	atom_shutdown,		/* shut down driver */
	atom_poll,		/* transmit poll message */
	noentry,		/* control (not used) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* buginfo (not used) */
	atom_timer,		/* called once per second */
};


/*
 * atom_start - initialize data for processing
 */
static int
atom_start(
	int unit,		/* unit number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct ppsunit *up;
	char	device[80];

	/*
	 * Allocate and initialize unit structure
	 */
	pp = peer->procptr;
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	pp->stratum = STRATUM_UNSPEC;
	memcpy((char *)&pp->refid, REFID, 4);
	up = emalloc(sizeof(struct ppsunit));
	memset(up, 0, sizeof(struct ppsunit));
	pp->unitptr = up;

	/*
	 * Open PPS device. This can be any serial or parallel port and
	 * not necessarily the port used for the associated radio.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	up->fddev = tty_open(device, O_RDWR, 0777);
	if (up->fddev <= 0) {
		msyslog(LOG_ERR,
			"refclock_atom: %s: %m", device);
		return (0);
	}

	/*
	 * Light up the PPSAPI interface.
	 */
	return (refclock_ppsapi(up->fddev, &up->atom));
}


/*
 * atom_shutdown - shut down the clock
 */
static void
atom_shutdown(
	int unit,		/* unit number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;
	struct ppsunit *up;

	pp = peer->procptr;
	up = pp->unitptr;
	if (up->fddev > 0)
		close(up->fddev);
	free(up);
}

/*
 * atom_timer - called once per second
 */
void
atom_timer(
	int	unit,		/* unit pointer (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct ppsunit *up;
	struct refclockproc *pp;
	char	tbuf[80];

	pp = peer->procptr;
	up = pp->unitptr;
	if (refclock_pps(peer, &up->atom, pp->sloppyclockflag) <= 0)
		return;

	peer->flags |= FLAG_PPS;

	/*
	 * If flag4 is lit, record each second offset to clockstats.
	 * That's so we can make awesome Allan deviation plots.
	 */
	if (pp->sloppyclockflag & CLK_FLAG4) {
		snprintf(tbuf, sizeof(tbuf), "%.9f",
			 pp->filter[pp->coderecv]);
		record_clock_stats(&peer->srcadr, tbuf);
	}
}


/*
 * atom_poll - called by the transmit procedure
 */
static void
atom_poll(
	int unit,		/* unit number (not used) */
	struct peer *peer	/* peer structure pointer */
	)
{
	struct refclockproc *pp;

	/*
	 * Don't wiggle the clock until some other driver has numbered
	 * the seconds.
	 */
	if (sys_leap == LEAP_NOTINSYNC)
		return;

	pp = peer->procptr;
	pp->polls++;
	if (pp->codeproc == pp->coderecv) {
		peer->flags &= ~FLAG_PPS;
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
}
#else
int refclock_atom_bs;
#endif /* REFCLOCK */
