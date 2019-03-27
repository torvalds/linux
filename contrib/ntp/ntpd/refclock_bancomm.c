/* refclock_bancomm.c - clock driver for the  Datum/Bancomm bc635VME 
 * Time and Frequency Processor. It requires the BANCOMM bc635VME/
 * bc350VXI Time and Frequency Processor Module Driver for SunOS4.x 
 * and SunOS5.x UNIX Systems. It has been tested on a UltraSparc 
 * IIi-cEngine running Solaris 2.6.
 * 
 * Author(s): 	Ganesh Ramasivan & Gary Cliff, Computing Devices Canada,
 *		Ottawa, Canada
 *
 * Date: 	July 1999
 *
 * Note(s):	The refclock type has been defined as 16.
 *
 *		This program has been modelled after the Bancomm driver
 *		originally written by R. Schmidt of Time Service, U.S. 
 *		Naval Observatory for a HP-UX machine. Since the original
 *		authors no longer plan to maintain this code, all 
 *		references to the HP-UX vme2 driver subsystem bave been
 *		removed. Functions vme_report_event(), vme_receive(), 
 *		vme_control() and vme_buginfo() have been deleted because
 *		they are no longer being used.
 *
 *	04/28/2005 Rob Neal 
 *		Modified to add support for Symmetricom bc637PCI-U Time & 
 *		Frequency Processor. 
 *	2/21/2007 Ali Ghorashi
 *	        Modified to add support for Symmetricom bc637PCI-U Time & 
 *		Frequency Processor on Solaris.
 *		Tested on Solaris 10 with a bc635 card.
 *
 *		Card bus type (VME/VXI or PCI) and environment are specified via the
 *		"mode" keyword on the server command in ntp.conf.
 *		server 127.127.16.u prefer mode M
 *		where u is the id (usually 0) of the entry in /dev (/dev/stfp0)
 *	
 *		and M is one of the following modes: 
 *		1		: FreeBSD PCI 635/637.
 *		2		: Linux or Windows PCI 635/637.
 *		3		: Solaris PCI 635/637
 *		not specified, or other number: 
 *				: Assumed to be VME/VXI legacy Bancomm card on Solaris.
 *		Linux and Windows platforms require Symmetricoms' proprietary driver
 *		for the TFP card.
 *		Solaris requires Symmetricom's driver and its header file (freely distributed) to 
 *		be installed and running.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_BANC) 

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <syslog.h>
#include <ctype.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

struct btfp_time                /* Structure for reading 5 time words   */
                                /* in one ioctl(2) operation.           */
{
	unsigned short btfp_time[5];  /* Time words 0,1,2,3, and 4. (16bit)*/
};
/* SunOS5 ioctl commands definitions.*/
#define BTFPIOC            ( 'b'<< 8 )
#define IOCIO( l, n )      ( BTFPIOC | n )
#define IOCIOR( l, n, s )  ( BTFPIOC | n )
#define IOCIORN( l, n, s ) ( BTFPIOC | n )
#define IOCIOWN( l, n, s ) ( BTFPIOC | n )

/***** Simple ioctl commands *****/
#define RUNLOCK		IOCIOR(b, 19, int )  /* Release Capture Lockout */
#define RCR0		IOCIOR(b, 22, int )  /* Read control register zero.*/
#define	WCR0		IOCIOWN(b, 23, int)  /* Write control register zero*/
/***** Compound ioctl commands *****/

/* Read all 5 time words in one call.   */
#if defined(__FreeBSD__) 
# define READTIME	_IOR('u', 5, struct btfp_time )
#else
# define READTIME	IOCIORN(b, 32, sizeof( struct btfp_time ))
#endif 

/* Solaris specific section */
struct	stfp_tm {
	int32_t tm_sec; 
	int32_t tm_min;
	int32_t tm_hour;
	int32_t tm_mday;
	int32_t tm_mon;
	int32_t tm_year;
	int32_t tm_wday;
	int32_t tm_yday;
	int32_t tm_isdst;
};

struct stfp_time {
	struct stfp_tm	tm;
	int32_t 	usec;			/* usec 0 - 999999 */
	int32_t 	hnsec;			/* hnsec 0 - 9 (hundreds of nsecs) */
	int32_t 	status;
};

#define SELTIMEFORMAT	2	
#	define TIME_DECIMAL 0
#	define TIME_BINARY	1

#if defined(__sun__)
#undef	READTIME
#define READTIME		9
#endif /** __sun___ **/
/* end solaris specific section */

struct vmedate {			   /* structure returned by get_vmetime.c */
	unsigned short year;
	unsigned short day;
	unsigned short hr;
	unsigned short mn;
	unsigned short sec;
	long frac;
	unsigned short status;
};

typedef void *SYMMT_PCI_HANDLE;

/*
 * VME interface parameters. 
 */
#define VMEPRECISION    (-21)   /* precision assumed (1 us) */
#define USNOREFID       "BTFP"  /* or whatever */
#define VMEREFID        "BTFP"  /* reference id */
#define VMEDESCRIPTION  "Bancomm bc635 TFP" /* who we are */
#define VMEHSREFID      0x7f7f1000 /* 127.127.16.00 refid hi strata */
/* clock type 16 is used here  */
#define GMT           	0       /* hour offset from Greenwich */

/*
 * Imported from ntp_timer module
 */
extern u_long current_time;     /* current time(s) */

/*
 * VME unit control structure.
 * Changes made to vmeunit structure. Most members are now available in the 
 * new refclockproc structure in ntp_refclock.h - 07/99 - Ganesh Ramasivan
 */
struct vmeunit {
	struct vmedate vmedata; /* data returned from vme read */
	u_long lasttime;        /* last time clock heard from */
};

/*
 * Function prototypes
 */
static  int     vme_start       (int, struct peer *);
static  void    vme_shutdown    (int, struct peer *);
static  void    vme_receive     (struct recvbuf *);
static  void    vme_poll        (int unit, struct peer *);
struct vmedate *get_datumtime(struct vmedate *);	
void	tvme_fill(struct vmedate *, uint32_t btm[2]);
void	stfp_time2tvme(struct vmedate *time_vme, struct stfp_time *stfp);
static const char *get_devicename(int n);

/* [Bug 3558] and [Bug 1674]  perlinger@ntp.org says:
 *
 * bcReadBinTime() is defined to use two DWORD pointers on Windows and
 * Linux in the BANCOMM SDK.  DWORD is of course Windows-specific
 * (*shudder*), and it is defined as 'unsigned long' under
 * Linux/Unix. (*sigh*)
 *
 * This creates quite some headache.  The size of 'unsigned long' is
 * platform/compiler/memory-model dependent (LP32 vs LP64 vs LLP64),
 * while the card itself always creates 32bit time stamps.  What a
 * bummer.  And DWORD has tendency to contain 64bit on Win64 (which is
 * why we have a DWORD32 defined on Win64) so it can be used as
 * substitute for 'UINT_PTR' in Windows API headers.  I won't even try
 * to comment on that, because anything I have to say will not be civil.
 *
 * We work around this by possibly using a wrapper function that makes
 * the necessary conversions/casts.  It might be a bit tricky to
 * maintain the conditional logic below, but any lingering disease needs
 * constant care to avoid a breakout.
 */
#if defined(__linux__)
  typedef unsigned long bcBinTimeT;
# if SIZEOF_LONG == 4
#   define safeReadBinTime bcReadBinTime
# endif
#elif defined(SYS_WINNT)
  typedef DWORD bcBinTimeT;
# if !defined(_WIN64) || _WIN64 == 0
#   define safeReadBinTime bcReadBinTime
# endif
#else
  typedef uint32_t bcBinTimeT;
# define safeReadBinTime bcReadBinTime
#endif

/*
 * Define the bc*() functions as weak so we can compile/link without them.
 * Only clients with the card will have the proprietary vendor device driver
 * and interface library needed for use on Linux/Windows platforms.
 */
extern uint32_t __attribute__ ((weak)) bcReadBinTime(SYMMT_PCI_HANDLE, bcBinTimeT*, bcBinTimeT*, uint8_t*);
extern SYMMT_PCI_HANDLE __attribute__ ((weak)) bcStartPci(void);
extern void __attribute__ ((weak)) bcStopPci(SYMMT_PCI_HANDLE);

/* This is the conversion wrapper for the long/DWORD/uint32_t clash in
 * reading binary times.
 */
#ifndef safeReadBinTime
static uint32_t
safeReadBinTime(
	SYMMT_PCI_HANDLE hnd,
	uint32_t        *pt1,
	uint32_t        *pt2,
	uint8_t         *p3
	)
{
	bcBinTimeT t1, t2;
	uint32_t   rc;

	rc = bcReadBinTime(hnd, &t1, &t2, p3);
	if (rc != 0) {
		*pt1 = (uint32_t)t1;
		*pt2 = (uint32_t)t2;
	}
	return rc;
}
#endif /* !defined(safeReadBinTime) */

/*
 * Transfer vector
 */
struct  refclock refclock_bancomm = {
	vme_start, 		/* start up driver */
	vme_shutdown,		/* shut down driver */
	vme_poll,		/* transmit poll message */
	noentry,		/* not used (old vme_control) */
	noentry,		/* initialize driver */ 
	noentry,		/* not used (old vme_buginfo) */ 
	NOFLAGS			/* not used */
};

int fd_vme;  /* file descriptor for ioctls */
int regvalue;
int tfp_type;	/* mode selector, indicate platform and driver interface */
SYMMT_PCI_HANDLE stfp_handle;

/* This helper function returns the device name based on the platform we
 * are running on and the device number.
 *
 * Uses a static buffer, so the result is valid only to the next call of
 * this function!
 */
static const char*
get_devicename(int n)
{
	
#   if defined(__sun__)
	static const char * const template ="/dev/stfp%d";
#   else
	static const char * const template ="/dev/btfp%d";
#   endif
	static char namebuf[20];
	
	snprintf(namebuf, sizeof(namebuf), template, n);
	namebuf[sizeof(namebuf)-1] = '\0'; /* paranoia rulez! */
	return namebuf;
}

/*
 * vme_start - open the VME device and initialize data for processing
 */
static int
vme_start(
	int unit,
	struct peer *peer
	)
{
	register struct vmeunit *vme;
	struct refclockproc *pp;
	int dummy;
	char vmedev[20];
	
	tfp_type = (int)(peer->ttl);
	switch (tfp_type) {		
		case 1:
		case 3:
			break;
		case 2:
			stfp_handle = bcStartPci(); 	/* init the card in lin/win */
			break;
		default:
			break;
	}
	/*
	 * Open VME device
	 */
#ifdef DEBUG

	printf("Opening DATUM DEVICE %s\n",get_devicename(peer->refclkunit));
#endif
	if ( (fd_vme = open(get_devicename(peer->refclkunit), O_RDWR)) < 0) {
		msyslog(LOG_ERR, "vme_start: failed open of %s: %m", vmedev);
		return (0);
	}
	else  { 
		switch (tfp_type) {
		  	case 1:	break;
			case 2: break;
			case 3:break;
			default: 
				/* Release capture lockout in case it was set before. */
				if( ioctl( fd_vme, RUNLOCK, &dummy ) )
		    		msyslog(LOG_ERR, "vme_start: RUNLOCK failed %m");

				regvalue = 0; /* More esoteric stuff to do... */
				if( ioctl( fd_vme, WCR0, &regvalue ) )
		    		msyslog(LOG_ERR, "vme_start: WCR0 failed %m");
				break;
		}
	}

	/*
	 * Allocate unit structure
	 */
	vme = emalloc_zero(sizeof(struct vmeunit));


	/*
	 * Set up the structures
	 */
	pp = peer->procptr;
	pp->unitptr = vme;
	pp->timestarted = current_time;

	pp->io.clock_recv = vme_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd_vme;
	/* shouldn't there be an io_addclock() call? */

	/*
	 * All done.  Initialize a few random peer variables, then
 	 * return success. Note that root delay and root dispersion are
	 * always zero for this clock.
	 */
	peer->precision = VMEPRECISION;
	memcpy(&pp->refid, USNOREFID,4);
	return (1);
}


/*
 * vme_shutdown - shut down a VME clock
 */
static void
vme_shutdown(
	int unit, 
	struct peer *peer
	)
{
	register struct vmeunit *vme;
	struct refclockproc *pp;

	/*
	 * Tell the I/O module to turn us off.  We're history.
	 */
	pp = peer->procptr;
	vme = pp->unitptr;
	io_closeclock(&pp->io);
	pp->unitptr = NULL;
	if (NULL != vme)
		free(vme);
	if (tfp_type == 2)
		bcStopPci(stfp_handle); 
}


/*
 * vme_receive - receive data from the VME device.
 *
 * Note: This interface would be interrupt-driven. We don't use that
 * now, but include a dummy routine for possible future adventures.
 */
static void
vme_receive(
	struct recvbuf *rbufp
	)
{
}


/*
 * vme_poll - called by the transmit procedure
 */
static void
vme_poll(
	int unit,
	struct peer *peer
	)
{
	struct vmedate *tptr; 
	struct vmeunit *vme;
	struct refclockproc *pp;
	time_t tloc;
	struct tm *tadr;
        
	pp = peer->procptr;	 
	vme = pp->unitptr;        /* Here is the structure */

	tptr = &vme->vmedata; 
	if ((tptr = get_datumtime(tptr)) == NULL ) {
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	get_systime(&pp->lastrec);
	pp->polls++;
	vme->lasttime = current_time;

	/*
	 * Get VME time and convert to timestamp format. 
	 * The year must come from the system clock.
	 */
	
	  time(&tloc);
	  tadr = gmtime(&tloc);
	  tptr->year = (unsigned short)(tadr->tm_year + 1900);

	snprintf(pp->a_lastcode,
		 sizeof(pp->a_lastcode),
		 "%3.3d %2.2d:%2.2d:%2.2d.%.6ld %1d",
		 tptr->day, 
		 tptr->hr, 
		 tptr->mn,
		 tptr->sec, 
		 tptr->frac, 
		 tptr->status);

	pp->lencode = (u_short) strlen(pp->a_lastcode);

	pp->day =  tptr->day;
	pp->hour =   tptr->hr;
	pp->minute =  tptr->mn;
	pp->second =  tptr->sec;
	pp->nsec =   tptr->frac;	

#ifdef DEBUG
	if (debug)
	    printf("pp: %3d %02d:%02d:%02d.%06ld %1x\n",
		   pp->day, pp->hour, pp->minute, pp->second,
		   pp->nsec, tptr->status);
#endif
	if (tptr->status ) {       /*  Status 0 is locked to ref., 1 is not */
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/*
	 * Now, compute the reference time value. Use the heavy
	 * machinery for the seconds and the millisecond field for the
	 * fraction when present. If an error in conversion to internal
	 * format is found, the program declares bad data and exits.
	 * Note that this code does not yet know how to do the years and
	 * relies on the clock-calendar chip for sanity.
	 */
	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);
}

struct vmedate *
get_datumtime(struct vmedate *time_vme)
{
	char cbuf[7];
	struct btfp_time vts;
	uint32_t btm[2];
	uint8_t dmy;
	struct stfp_time stfpm;
	
	if (time_vme == NULL)
  		time_vme = emalloc(sizeof(*time_vme));

	switch (tfp_type) {
		case 1:				/* BSD, PCI, 2 32bit time words */
			if (ioctl(fd_vme, READTIME, &btm)) {
	    		msyslog(LOG_ERR, "get_bc63x error: %m");
				return(NULL);
			}
			tvme_fill(time_vme, btm);
			break;

		case 2:				/* Linux/Windows, PCI, 2 32bit time words */
			if (safeReadBinTime(stfp_handle, &btm[1], &btm[0], &dmy) == 0) {
	    		msyslog(LOG_ERR, "get_datumtime error: %m"); 
				return(NULL);
			}
			tvme_fill(time_vme, btm);
			break;
			
		case 3: /** solaris **/
			memset(&stfpm,0,sizeof(stfpm));
			
			/* we need the time in decimal format */
			/* Here we rudely assume that we are the only user of the driver.
			 * Other programs will have to set their own time format before reading 
			 * the time.
			 */
			if(ioctl (fd_vme, SELTIMEFORMAT, TIME_DECIMAL)){	
					msyslog(LOG_ERR, "Could not set time format");
					return (NULL);	
			}
			/* read the time */
			if (ioctl(fd_vme, READTIME, &stfpm)) {
				msyslog(LOG_ERR, "ioctl error: %m");
				return(NULL);
			}
			stfp_time2tvme(time_vme,  &stfpm);
			break;			

		default:			/* legacy bancomm card */

			if (ioctl(fd_vme, READTIME, &vts)) {
				msyslog(LOG_ERR,
					"get_datumtime error: %m");
				return(NULL);
			}
			/* Get day */
			snprintf(cbuf, sizeof(cbuf), "%3.3x",
				 ((vts.btfp_time[ 0 ] & 0x000f) << 8) +
				  ((vts.btfp_time[ 1 ] & 0xff00) >> 8));  
			time_vme->day = (unsigned short)atoi(cbuf);

			/* Get hour */
			snprintf(cbuf, sizeof(cbuf), "%2.2x",
				 vts.btfp_time[ 1 ] & 0x00ff);
			time_vme->hr = (unsigned short)atoi(cbuf);

			/* Get minutes */
			snprintf(cbuf, sizeof(cbuf), "%2.2x",
				 (vts.btfp_time[ 2 ] & 0xff00) >> 8);
			time_vme->mn = (unsigned short)atoi(cbuf);

			/* Get seconds */
			snprintf(cbuf, sizeof(cbuf), "%2.2x",
				 vts.btfp_time[ 2 ] & 0x00ff);
			time_vme->sec = (unsigned short)atoi(cbuf);

			/* Get microseconds.  Yes, we ignore the 0.1 microsecond digit so
				 we can use the TVTOTSF function  later on...*/

			snprintf(cbuf, sizeof(cbuf), "%4.4x%2.2x",
				 vts.btfp_time[ 3 ],
				 vts.btfp_time[ 4 ] >> 8);
			time_vme->frac = (u_long) atoi(cbuf);

			/* Get status bit */
			time_vme->status = (vts.btfp_time[0] & 0x0010) >> 4;

			break;
	}

	if (time_vme->status) 
		return ((void *)NULL);
	else
	    return (time_vme);
}
/* Assign values to time_vme struct. Mostly for readability */
void
tvme_fill(struct vmedate *time_vme, uint32_t btm[2])
{
	struct tm maj;
	time_t   dmaj;
	uint32_t dmin;

	dmaj = btm[1];			/* syntax sugar & expansion */
	dmin = btm[0];			/* just syntax sugar */

	gmtime_r(&dmaj, &maj);
	time_vme->day  = maj.tm_yday+1;
	time_vme->hr   = maj.tm_hour;
	time_vme->mn   = maj.tm_min;
	time_vme->sec  = maj.tm_sec;
	time_vme->frac = (dmin & 0x000fffff) * 1000; 
	time_vme->frac += ((dmin & 0x00f00000) >> 20) * 100;
	time_vme->status = (dmin & 0x01000000) >> 24;
	return;
}


/* Assign values to time_vme struct. Mostly for readability */
void
stfp_time2tvme(struct vmedate *time_vme, struct stfp_time *stfp)
{

	time_vme->day  = stfp->tm.tm_yday+1;
	time_vme->hr   = stfp->tm.tm_hour;
	time_vme->mn   = stfp->tm.tm_min;
	time_vme->sec  = stfp->tm.tm_sec;
	time_vme->frac = stfp->usec*1000;  
	time_vme->frac += stfp->hnsec * 100;
	time_vme->status = stfp->status;
	return;
}
#else
int refclock_bancomm_bs;
#endif /* REFCLOCK */
