/*
 * refclock_hopfpci.c
 *
 * - clock driver for hopf 6039 PCI board (GPS or DCF77)
 * Bernd Altmeier altmeier@atlsoft.de
 *
 * latest source and further information can be found at:
 * http://www.ATLSoft.de/ntp
 *
 * In order to run this driver you have to install and test
 * the PCI-board driver for your system first.
 *
 * On Linux/UNIX
 *
 * The driver attempts to open the device /dev/hopf6039 .
 * The device entry will be made by the installation process of
 * the kernel module for the PCI-bus board. The driver sources
 * belongs to the delivery equipment of the PCI-board.
 *
 * On Windows NT/2000
 *
 * The driver attempts to open the device by calling the function
 * "OpenHopfDevice()". This function will be installed by the
 * Device Driver for the PCI-bus board. The driver belongs to the
 * delivery equipment of the PCI-board.
 *
 *
 * Start   21.03.2000 Revision: 01.20
 * changes 22.12.2000 Revision: 01.40 flag1 = 1 sync even if Quarz
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_HOPF_PCI)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#undef fileno
#include <ctype.h>
#undef fileno

#ifndef SYS_WINNT
# include <sys/ipc.h>
# include <sys/ioctl.h>
# include <assert.h>
# include <unistd.h>
# include <stdio.h>
# include "hopf6039.h"
#else
# include "hopf_PCI_io.h"
#endif

/*
 * hopfpci interface definitions
 */
#define PRECISION       (-10)    /* precision assumed (1 ms) */
#define REFID           "hopf"   /* reference ID */
#define DESCRIPTION     "hopf Elektronik PCI radio board"

#define NSAMPLES        3       /* stages of median filter */
#ifndef SYS_WINNT
# define	DEVICE	"/dev/hopf6039" 	/* device name inode*/
#else
# define	DEVICE	"hopf6039" 	/* device name WinNT  */
#endif

#define LEWAPWAR	0x20	/* leap second warning bit */

#define	HOPF_OPMODE	0xC0	/* operation mode mask */
#define HOPF_INVALID	0x00	/* no time code available */
#define HOPF_INTERNAL	0x40	/* internal clock */
#define HOPF_RADIO	0x80	/* radio clock */
#define HOPF_RADIOHP	0xC0	/* high precision radio clock */


/*
 * hopfclock unit control structure.
 */
struct hopfclock_unit {
	short	unit;		/* NTP refclock unit number */
	char	leap_status;	/* leap second flag */
};
int	fd;			/* file descr. */

/*
 * Function prototypes
 */
static  int     hopfpci_start       (int, struct peer *);
static  void    hopfpci_shutdown    (int, struct peer *);
static  void    hopfpci_poll        (int unit, struct peer *);

/*
 * Transfer vector
 */
struct  refclock refclock_hopfpci = {
	hopfpci_start,          /* start up driver */
	hopfpci_shutdown,       /* shut down driver */
	hopfpci_poll,           /* transmit poll message */
	noentry,                /* not used */
	noentry,                /* initialize driver (not used) */
	noentry,                /* not used */
	NOFLAGS                 /* not used */
};

/*
 * hopfpci_start - attach to hopf PCI board 6039
 */
static int
hopfpci_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct hopfclock_unit *up;

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));

#ifndef SYS_WINNT

 	fd = open(DEVICE,O_RDWR); /* try to open hopf clock device */

#else
	if (!OpenHopfDevice()) {
		msyslog(LOG_ERR, "Start: %s unit: %d failed!", DEVICE, unit);
		free(up);
		return (0);
	}
#endif

	pp = peer->procptr;
	pp->io.clock_recv = noentry;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = INVALID_SOCKET;
	pp->unitptr = up;

	get_systime(&pp->lastrec);

	/*
	 * Initialize miscellaneous peer variables
	 */
	memcpy((char *)&pp->refid, REFID, 4);
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	up->leap_status = 0;
	up->unit = (short) unit;
	return (1);
}


/*
 * hopfpci_shutdown - shut down the clock
 */
static void
hopfpci_shutdown(
	int unit,
	struct peer *peer
	)
{

#ifndef SYS_WINNT
	close(fd);
#else
	CloseHopfDevice();
#endif
	if (NULL != peer->procptr->unitptr)
		free(peer->procptr->unitptr);
}


/*
 * hopfpci_poll - called by the transmit procedure
 */
static void
hopfpci_poll(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	HOPFTIME m_time;

	pp = peer->procptr;

#ifndef SYS_WINNT
	if (ioctl(fd, HOPF_CLOCK_GET_UTC, &m_time) < 0)
		msyslog(LOG_ERR, "HOPF_P(%d): HOPF_CLOCK_GET_UTC: %m",
			unit);
#else
	GetHopfSystemTime(&m_time);
#endif
	pp->polls++;

	pp->day    = ymd2yd(m_time.wYear,m_time.wMonth,m_time.wDay);
	pp->hour   = m_time.wHour;
	pp->minute = m_time.wMinute;
	pp->second = m_time.wSecond;
	pp->nsec   = m_time.wMilliseconds * 1000000;
	if (m_time.wStatus & LEWAPWAR)
		pp->leap = LEAP_ADDSECOND;
	else
		pp->leap = LEAP_NOWARNING;

	snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
		 "ST: %02X T: %02d:%02d:%02d.%03ld D: %02d.%02d.%04d",
		 m_time.wStatus, pp->hour, pp->minute, pp->second,
		 pp->nsec / 1000000, m_time.wDay, m_time.wMonth,
		 m_time.wYear);
	pp->lencode = (u_short)strlen(pp->a_lastcode);

	get_systime(&pp->lastrec);

	/*
	 * If clock has no valid status then report error and exit
	 */
	if ((m_time.wStatus & HOPF_OPMODE) == HOPF_INVALID) {  /* time ok? */
		refclock_report(peer, CEVNT_BADTIME);
		pp->leap = LEAP_NOTINSYNC;
		return;
	}

	/*
	 * Test if time is running on internal quarz
	 * if CLK_FLAG1 is set, sychronize even if no radio operation
	 */

	if ((m_time.wStatus & HOPF_OPMODE) == HOPF_INTERNAL){
		if ((pp->sloppyclockflag & CLK_FLAG1) == 0) {
			refclock_report(peer, CEVNT_BADTIME);
			pp->leap = LEAP_NOTINSYNC;
			return;
		}
	}

	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	return;
}

#else
int refclock_hopfpci_bs;
#endif /* REFCLOCK */
