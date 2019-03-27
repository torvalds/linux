/* refclock_psc.c:  clock driver for Brandywine PCI-SyncClock32/HP-UX 11.X */

#ifdef	HAVE_CONFIG_H
#include	<config.h>
#endif	/* HAVE_CONFIG_H	*/

#if defined(REFCLOCK) && defined(CLOCK_GPSVME)

#include	"ntpd.h"
#include	"ntp_io.h"
#include	"ntp_refclock.h"
#include	"ntp_unixtime.h"
#include	"ntp_stdlib.h"

#ifdef	__hpux
#include	<sys/rtprio.h>	/* may already be included above	*/
#include	<sys/lock.h>	/* NEEDED for PROCLOCK			*/
#endif	/* __hpux	*/

#ifdef	__linux__
#include	<sys/ioctl.h>	/* for _IOR, ioctl			*/
#endif	/* __linux__	*/

enum {				/* constants	*/
    BUFSIZE			=	32,
    PSC_SYNC_OK			=	0x40,	/* Sync status bit	*/
    DP_LEAPSEC_DAY10DAY1 	= 	0x82,	/* DP RAM address	*/
    DP_LEAPSEC_DAY1000DAY100	=	0x83,
    DELAY			=	1,
    NUNIT			=	2	/* max UNITS		*/
};

/*	clock card registers	*/
struct psc_regs {
    uint32_t		low_time;	/* card base + 0x00	*/
    uint32_t		high_time;	/* card base + 0x04	*/
    uint32_t		ext_low_time;	/* card base + 0x08	*/
    uint32_t		ext_high_time;	/* card base + 0x0C	*/
    uint8_t		device_status;	/* card base + 0x10	*/
    uint8_t		device_control;	/* card base + 0x11	*/
    uint8_t		reserved0;	/* card base + 0x12	*/
    uint8_t		ext_100ns;	/* card base + 0x13	*/
    uint8_t		match_usec;	/* card base + 0x14	*/
    uint8_t		match_msec;	/* card base + 0x15	*/
    uint8_t		reserved1;	/* card base + 0x16	*/
    uint8_t		reserved2;	/* card base + 0x17	*/
    uint8_t		reserved3;	/* card base + 0x18	*/
    uint8_t		reserved4;	/* card base + 0x19	*/
    uint8_t		dp_ram_addr;	/* card base + 0x1A	*/
    uint8_t		reserved5;	/* card base + 0x1B	*/
    uint8_t		reserved6;	/* card base + 0x1C	*/
    uint8_t		reserved7;	/* card base + 0x1D	*/
    uint8_t		dp_ram_data;	/* card base + 0x1E	*/
    uint8_t		reserved8;	/* card base + 0x1F	*/
} *volatile regp[NUNIT];

#define	PSC_REGS	_IOR('K', 0, long)     	/* ioctl argument	*/

/* Macros to swap byte order and convert BCD to binary	*/
#define SWAP(val) ( ((val) >> 24) | (((val) & 0x00ff0000) >> 8) | \
(((val) & 0x0000ff00) << 8) | (((val) & 0x000000ff) << 24) )
#define BCD2INT2(val)  ( ((val) >> 4 & 0x0f)*10 + ((val) & 0x0f) )
#define BCD2INT3(val)  ( ((val) >> 8 & 0x0f)*100 + ((val) >> 4 & 0x0f)*10 + \
((val) & 0x0f) )

/* PSC interface definitions */
#define PRECISION	(-20)	/* precision assumed (1 us)	*/
#define REFID		"USNO"	/* reference ID	*/
#define DESCRIPTION	"Brandywine PCI-SyncClock32"
#define DEVICE		"/dev/refclock%1d"	/* device file	*/

/* clock unit control structure */
struct psc_unit {
    short	unit;		/* NTP refclock unit number	*/
    short	last_hour;	/* last hour (monitor leap sec)	*/
    int		msg_flag[2];	/* count error messages		*/
};
int	fd[NUNIT];		/* file descriptor	*/

/* Local function prototypes */
static int		psc_start(int, struct peer *);
static void		psc_shutdown(int, struct peer *);
static void		psc_poll(int, struct peer *);
static void		check_leap_sec(struct refclockproc *, int);

/* Transfer vector	*/
struct refclock	refclock_gpsvme = {
    psc_start, psc_shutdown, psc_poll, noentry, noentry, noentry, NOFLAGS
};

/* psc_start:  open device and initialize data for processing */
static int
psc_start(
    int		unit,
    struct peer	*peer
    )
{
    char			buf[BUFSIZE];
    struct refclockproc		*pp;
    struct psc_unit		*up = emalloc(sizeof *up);

    if (unit < 0 || unit > 1) {		/* support units 0 and 1	*/
	msyslog(LOG_ERR, "psc_start: bad unit: %d", unit);
	return 0;
    }

    memset(up, '\0', sizeof *up);

    snprintf(buf, sizeof(buf), DEVICE, unit);	/* dev file name	*/
    fd[unit] = open(buf, O_RDONLY);	/* open device file	*/
    if (fd[unit] < 0) {
	msyslog(LOG_ERR, "psc_start: unit: %d, open failed.  %m", unit);
	return 0;
    }
     
    /* get the address of the mapped regs	*/
    if (ioctl(fd[unit], PSC_REGS, &regp[unit]) < 0) {
	msyslog(LOG_ERR, "psc_start: unit: %d, ioctl failed.  %m", unit);
	return 0;
    }

    /* initialize peer variables	*/
    pp = peer->procptr;
    pp->io.clock_recv = noentry;
    pp->io.srcclock = peer;
    pp->io.datalen = 0;
    pp->io.fd = -1;
    pp->unitptr = up;
    get_systime(&pp->lastrec);
    memcpy(&pp->refid, REFID, 4);
    peer->precision = PRECISION;
    pp->clockdesc = DESCRIPTION;
    up->unit = unit;
#ifdef	__hpux     
    rtprio(0,120); 		/* set real time priority	*/
    plock(PROCLOCK); 		/* lock process in memory	*/
#endif	/* __hpux	*/     
    return 1;
}

/* psc_shutdown:  shut down the clock */
static void
psc_shutdown(
    int		unit,
    struct peer	*peer
    )
{
    if (NULL != peer->procptr->unitptr)
	free(peer->procptr->unitptr);
    if (fd[unit] > 0)
	close(fd[unit]);
}

/* psc_poll:  read, decode, and record device time */
static void
psc_poll(
    int		unit,
    struct peer	*peer
    )
{
    struct refclockproc	*pp = peer->procptr;
    struct psc_unit		*up;
    unsigned			tlo, thi;
    unsigned char		status;

    up = (struct psc_unit *) pp->unitptr;
    tlo = regp[unit]->low_time;		/* latch and read first 4 bytes	*/
    thi = regp[unit]->high_time;	/* read 4 higher order bytes	*/
    status = regp[unit]->device_status;	/* read device status byte	*/

    if (!(status & PSC_SYNC_OK)) {
	refclock_report(peer, CEVNT_BADTIME);
	if (!up->msg_flag[unit]) {	/* write once to system log	*/
	    msyslog(LOG_WARNING,
		"SYNCHRONIZATION LOST on unit %1d, status %02x\n",
		unit, status);
	    up->msg_flag[unit] = 1;
	}
	return;
    }

    get_systime(&pp->lastrec);
    pp->polls++;
     
    tlo = SWAP(tlo);			/* little to big endian swap on	*/
    thi = SWAP(thi);			/* copy of data			*/
    /* convert the BCD time to broken down time used by refclockproc	*/
    pp->day	= BCD2INT3((thi & 0x0FFF0000) >> 16);
    pp->hour	= BCD2INT2((thi & 0x0000FF00) >> 8);
    pp->minute	= BCD2INT2(thi & 0x000000FF);
    pp->second	= BCD2INT2(tlo >> 24);
    /* ntp_process() in ntp_refclock.c appears to use usec as fraction of
       second in microseconds if usec is nonzero. */
    pp->nsec	= 1000000*BCD2INT3((tlo & 0x00FFF000) >> 12) +
	BCD2INT3(tlo & 0x00000FFF);

    snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
	     "%3.3d %2.2d:%2.2d:%2.2d.%09ld %02x %08x %08x", pp->day,
	     pp->hour, pp->minute, pp->second, pp->nsec, status, thi,
	     tlo);
    pp->lencode = strlen(pp->a_lastcode);

    /* compute the timecode timestamp	*/
    if (!refclock_process(pp)) {
	refclock_report(peer, CEVNT_BADTIME);
	return;
    }
    /* simulate the NTP receive and packet procedures	*/
    refclock_receive(peer);
    /* write clock statistics to file	*/
    record_clock_stats(&peer->srcadr, pp->a_lastcode);	

    /* With the first timecode beginning the day, check for a GPS
       leap second notification.      */
    if (pp->hour < up->last_hour) {
	check_leap_sec(pp, unit);
	up->msg_flag[0] = up->msg_flag[1] = 0;	/* reset flags	*/
    }
    up->last_hour = pp->hour;
}

/* check_leap_sec:  read the Dual Port RAM leap second day registers.  The
   onboard GPS receiver should write the hundreds digit of day of year in
   DP_LeapSec_Day1000Day100 and the tens and ones digits in
   DP_LeapSec_Day10Day1.  If these values are nonzero and today, we have
   a leap second pending, so we set the pp->leap flag to LEAP_ADDSECOND.
   If the BCD data are zero or a date other than today, set pp->leap to
   LEAP_NOWARNING.  */
static void
check_leap_sec(struct refclockproc *pp, int unit)
{
    unsigned char	dhi, dlo;
    int			leap_day;
     
    regp[unit]->dp_ram_addr = DP_LEAPSEC_DAY10DAY1;
    usleep(DELAY);
    dlo = regp[unit]->dp_ram_data;
    regp[unit]->dp_ram_addr = DP_LEAPSEC_DAY1000DAY100;
    usleep(DELAY);
    dhi = regp[unit]->dp_ram_data;
    leap_day = BCD2INT2(dlo) + 100*(dhi & 0x0F);

    pp->leap = LEAP_NOWARNING;			/* default	*/
    if (leap_day && leap_day == pp->day) {
	pp->leap = LEAP_ADDSECOND;		/* leap second today	*/
	msyslog(LOG_ERR, "LEAP_ADDSECOND flag set, day %d (%x %x).",
	    leap_day, dhi, dlo);
    }
}

#else
int	refclock_gpsvme_bs;
#endif	/* REFCLOCK	*/
