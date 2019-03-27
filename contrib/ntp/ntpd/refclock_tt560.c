/*
 * refclock_tt560 - clock driver for the TrueTime 560 IRIG-B decoder
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_TT560)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "sys/tt560_api.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

/*
 * This driver supports the TrueTime 560 IRIG-B decoder for the PCI bus.
 */ 

/*
 * TT560 interface definitions
 */
#define	DEVICE		 "/dev/tt560%d" /* device name and unit */
#define	PRECISION	(-20)	/* precision assumed (1 us) */
#define	REFID		"IRIG"	/* reference ID */
#define	DESCRIPTION	"TrueTime 560 IRIG-B PCI Decoder"

/*
 * Unit control structure
 */
struct tt560unit {
	tt_mem_space_t	 *tt_mem;	/* mapped address of PCI board */
	time_freeze_reg_t tt560rawt;	/* data returned from PCI board */
};

typedef union byteswap_u
{
    unsigned int long_word;
    unsigned char byte[4];
} byteswap_t;

/*
 * Function prototypes
 */
static	int	tt560_start	(int, struct peer *);
static	void	tt560_shutdown	(int, struct peer *);
static	void	tt560_poll	(int unit, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_tt560 = {
	tt560_start,		/* clock_start    */
	tt560_shutdown,		/* clock_shutdown */
	tt560_poll,		/* clock_poll     */
	noentry,		/* clock_control (not used) */
	noentry,		/* clock_init    (not used) */
	noentry,		/* clock_buginfo (not used) */
	NOFLAGS			/* clock_flags   (not used) */
};


/*
 * tt560_start - open the TT560 device and initialize data for processing
 */
static int
tt560_start(
	int unit,
	struct peer *peer
	)
{
	register struct tt560unit *up;
	struct refclockproc *pp;
	char	device[20];
	int	fd;
	caddr_t membase;

	/*
	 * Open TT560 device
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = open(device, O_RDWR);
	if (fd == -1) {
		msyslog(LOG_ERR, "tt560_start: open of %s: %m", device);
		return (0);
	}

	/*
	 * Map the device registers into user space.
	 */
	membase = mmap ((caddr_t) 0, TTIME_MEMORY_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, (off_t)0);

	if (membase == (caddr_t) -1) {
		msyslog(LOG_ERR, "tt560_start: mapping of %s: %m", device);
		(void) close(fd);
		return (0);
	}

	/*
	 * Allocate and initialize unit structure
	 */
	if (!(up = (struct tt560unit *) emalloc(sizeof(struct tt560unit)))) {
		(void) close(fd);
		return (0);
	}
	memset((char *)up, 0, sizeof(struct tt560unit));
	up->tt_mem = (tt_mem_space_t *)membase;
	pp = peer->procptr;
	pp->io.clock_recv = noentry;
	pp->io.srcclock = (caddr_t)peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	pp->unitptr = (caddr_t)up;

	/*
	 * Initialize miscellaneous peer variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	return (1);
}


/*
 * tt560_shutdown - shut down the clock
 */
static void
tt560_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct tt560unit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = (struct tt560unit *)pp->unitptr;
	io_closeclock(&pp->io);
	free(up);
}


/*
 * tt560_poll - called by the transmit procedure
 */
static void
tt560_poll(
	int unit,
	struct peer *peer
	)
{
	register struct tt560unit *up;
	struct refclockproc       *pp;
	time_freeze_reg_t         *tp;
	tt_mem_space_t            *mp;

	int i;
	unsigned int *p_time_t, *tt_mem_t;

	/*
	 * This is the main routine. It snatches the time from the TT560
	 * board and tacks on a local timestamp.
	 */
	pp = peer->procptr;
	up = (struct tt560unit *)pp->unitptr;
	mp = up->tt_mem;
	tp = &up->tt560rawt;

	p_time_t = (unsigned int *)tp;
	tt_mem_t = (unsigned int *)&mp->time_freeze_reg;

	*tt_mem_t = 0;		/* update the time freeze register */
				/* and copy time stamp to memory */
	for (i=0; i < TIME_FREEZE_REG_LEN; i++) {
	    *p_time_t = byte_swap(*tt_mem_t);
	     p_time_t++;
	     tt_mem_t++;
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
	 */
	snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
	    "%1x%1x%1x %1x%1x:%1x%1x:%1x%1x.%1x%1x%1x%1x%1x%1x %1x",
	    tp->hun_day,  tp->tens_day,  tp->unit_day,
	                  tp->tens_hour, tp->unit_hour,
	                  tp->tens_min,  tp->unit_min,
	                  tp->tens_sec,  tp->unit_sec,
	    tp->hun_ms,   tp->tens_ms,   tp->unit_ms,
	    tp->hun_us,   tp->tens_us,   tp->unit_us,
	    tp->status);
	    pp->lencode = strlen(pp->a_lastcode);
#ifdef DEBUG
	if (debug)
		printf("tt560: time %s timecode %d %s\n",
		   ulfptoa(&pp->lastrec, 6), pp->lencode,
		   pp->a_lastcode);
#endif
	if (sscanf(pp->a_lastcode, "%3d %2d:%2d:%2d.%6ld", 
                  &pp->day, &pp->hour, &pp->minute, &pp->second, &pp->usec)
	    != 5) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	if ((tp->status & 0x6) != 0x6)
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
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	refclock_receive(peer);
}

/******************************************************************
 *
 *  byte_swap
 *
 *  Inputs: 32 bit integer
 *
 *  Output: byte swapped 32 bit integer.
 *
 *  This routine is used to compensate for the byte alignment
 *  differences between big-endian and little-endian integers.
 *
 ******************************************************************/
static unsigned int
byte_swap(unsigned int input_num)
{
    byteswap_t    byte_swap;
    unsigned char temp;

    byte_swap.long_word = input_num;

    temp              = byte_swap.byte[3];
    byte_swap.byte[3] = byte_swap.byte[0];
    byte_swap.byte[0] = temp;

    temp              = byte_swap.byte[2];
    byte_swap.byte[2] = byte_swap.byte[1];
    byte_swap.byte[1] = temp;

    return (byte_swap.long_word);
}

#else
int refclock_tt560_bs;
#endif /* REFCLOCK */
