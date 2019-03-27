
/*
 * refclock_local - local pseudo-clock driver
 *
 * wjm 17-aug-1995: add a hook for special treatment of VMS_LOCALUNIT
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef REFCLOCK

#include "ntpd.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

#ifdef KERNEL_PLL
#include "ntp_syscall.h"
#endif

/*
 * This is a hack to allow a machine to use its own system clock as a
 * reference clock, i.e., to free-run using no outside clock discipline
 * source. Note that the clock selection algorithm will not select this
 * driver unless all other sources of synchronization have been lost.
 * This is useful if you want to use NTP in an isolated environment
 * with no radio clock or NIST modem available. Pick a machine that you
 * figure has a good clock oscillator and configure it with this
 * driver. Set the clock using the best means available, like
 * eyeball-and-wristwatch. Then, point all the other machines at this
 * one or use broadcast (not multicast) mode to distribute time.
 *
 * Another application for this driver is if you want to use a
 * particular server's clock as the clock of last resort when all other
 * normal synchronization sources have gone away. This is especially
 * useful if that server has an ovenized oscillator. However, the
 * preferred was to do this is using orphan mode. See the documentation.
 *
 * A third application for this driver is when an external discipline
 * source is available, such as the NIST "lockclock" program, which
 * synchronizes the local clock via a telephone modem and the NIST
 * Automated Computer Time Service (ACTS), or the Digital Time
 * Synchronization Service (DTSS), which runs on DCE machines. In this
 * case the stratum should be set at zero, indicating a bona fide
 * stratum-1 source. Exercise some caution with this, since there is no
 * easy way to telegraph via NTP that something might be wrong in the
 * discipline source itself. In the case of DTSS, the local clock can
 * have a rather large jitter, depending on the interval between
 * corrections and the intrinsic frequency error of the clock
 * oscillator. In extreme cases, this can cause clients to exceed the
 * 128-ms slew window and drop off the NTP subnet.
 *
 * Fudge Factors
 *
 * None currently supported.
 */
/*
 * Local interface definitions
 */
#define PRECISION	(-7)	/* about 10 ms precision */
#define DESCRIPTION "Undisciplined local clock" /* WRU */
#define STRATUM 	5	/* default stratum */
#define DISPERSION	.01	/* default dispersion (10 ms) */

/*
 * Imported from the timer module
 */
extern u_long current_time;

/*
 * Imported from ntp_proto
 */
extern s_char sys_precision;

/*
 * Function prototypes
 */
static	int local_start (int, struct peer *);
static	void	local_poll	(int, struct peer *);

/*
 * Local variables
 */
static	u_long poll_time;	/* last time polled */
	
/*
 * Transfer vector
 */
struct	refclock refclock_local = {
	local_start,		/* start up driver */
	noentry,		/* shut down driver (not used) */
	local_poll,	 	/* transmit poll message */
	noentry,		/* not used (old lcl_control) */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used (old lcl_buginfo) */
	NOFLAGS 		/* not used */
};


/*
 * local_start - start up the clock
 */
static int
local_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;

	pp = peer->procptr;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = sys_precision;
	pp->leap = LEAP_NOTINSYNC;
	peer->stratum = STRATUM;
	pp->stratum = STRATUM;
	pp->clockdesc = DESCRIPTION;
	memcpy(&pp->refid, "LOCL", 4);
	poll_time = current_time;
	return (1);
}


/*
 * local_poll - called by the transmit procedure
 *
 * LOCKCLOCK: If the kernel supports the nanokernel or microkernel
 * system calls, the leap bits are extracted from the kernel. If there
 * is a kernel error or the kernel leap bits are set to 11, the NTP leap
 * bits are set to 11 and the stratum is set to infinity. Otherwise, the
 * NTP leap bits are set to the kernel leap bits and the stratum is set
 * as fudged. This behavior does not faithfully follow the
 * specification, but is probably more appropriate in a multiple-server
 * national laboratory network.
 */
static void
local_poll(
	int unit,
	struct peer *peer
	)
{
#if defined(KERNEL_PLL) && defined(LOCKCLOCK)
	struct timex ntv;
#endif /* KERNEL_PLL LOCKCLOCK */
	struct refclockproc *pp;

	/*
	 * Do no evil unless the house is dark or lit with our own lamp.
	 */
	if (!(sys_peer == NULL || sys_peer == peer))
		return;

#if defined(VMS) && defined(VMS_LOCALUNIT)
	if (unit == VMS_LOCALUNIT) {
		extern void vms_local_poll(struct peer *);

		vms_local_poll(peer);
		return;
	}
#endif /* VMS && VMS_LOCALUNIT */

	pp = peer->procptr;
	pp->polls++;

	/*
	 * Ramble through the usual filtering and grooming code, which
	 * is essentially a no-op and included mostly for pretty
	 * billboards.
	 */
	poll_time = current_time;
	refclock_process_offset(pp, pp->lastrec, pp->lastrec, 0);

	/*
	 * If another process is disciplining the system clock, we set
	 * the leap bits and quality indicators from the kernel.
	 */
#if defined(KERNEL_PLL) && defined(LOCKCLOCK)
	memset(&ntv,  0, sizeof ntv);
	switch (ntp_adjtime(&ntv)) {
	case TIME_OK:
		pp->leap = LEAP_NOWARNING;
		peer->stratum = pp->stratum;
		break;

	case TIME_INS:
		pp->leap = LEAP_ADDSECOND;
		peer->stratum = pp->stratum;
		break;

	case TIME_DEL:
		pp->leap = LEAP_DELSECOND;
		peer->stratum = pp->stratum;
		break;

	default:
		pp->leap = LEAP_NOTINSYNC;
		peer->stratum = STRATUM_UNSPEC;
	}
	pp->disp = 0;
	pp->jitter = 0;
#else /* KERNEL_PLL LOCKCLOCK */
	pp->leap = LEAP_NOWARNING;
	pp->disp = DISPERSION;
	pp->jitter = 0;
#endif /* KERNEL_PLL LOCKCLOCK */
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
}
#else
int refclock_local_bs;
#endif /* REFCLOCK */
