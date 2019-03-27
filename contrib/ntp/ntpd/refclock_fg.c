/*
 * refclock_fg - clock driver for the Forum Graphic GPS datating station
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_FG)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_calendar.h"
#include "ntp_stdlib.h"

/*
 * This driver supports the Forum Graphic GPS dating station.
 * More information about FG GPS is available on http://www.forumgraphic.com
 * Contact das@amt.ru for any question about this driver.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/fgclock%d"
#define	PRECISION	(-10)	/* precision assumed (about 1 ms) */
#define REFID		"GPS"
#define DESCRIPTION	"Forum Graphic GPS dating station"
#define LENFG		26	/* timecode length */
#define SPEED232        B9600   /* uart speed (9600 baud) */

/*
 * Function prototypes
 */
static	int 	fg_init 	(int);
static	int 	fg_start 	(int, struct peer *);
static	void	fg_shutdown	(int, struct peer *);
static	void	fg_poll		(int, struct peer *);
static	void	fg_receive	(struct recvbuf *);

/* 
 * Forum Graphic unit control structure
 */

struct fgunit {
	int pollnum;	/* Use peer.poll instead? */
	int status; 	/* Hug to check status information on GPS */
	int y2kwarn;	/* Y2K bug */
};

/* 
 * Queries definition
 */
static char fginit[] = { 0x10, 0x48, 0x10, 0x0D, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0 };
static char fgdate[] = { 0x10, 0x44, 0x10, 0x0D, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0 };

/*
 * Transfer vector
 */
struct  refclock refclock_fg = {
	fg_start,		/* start up driver */
	fg_shutdown,		/* shut down driver */
	fg_poll,		/* transmit poll message */
	noentry,		/* not used */
	noentry,		/* initialize driver (not used) */
	noentry,		/* not used */
	NOFLAGS			/* not used */
};

/*
 * fg_init - Initialization of FG GPS.
 */

static int
fg_init(
	int fd
	)
{
	if (write(fd, fginit, LENFG) != LENFG)
		return 0;

	return 1;
}

/*
 * fg_start - open the device and initialize data for processing
 */
static int
fg_start(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct fgunit *up;
	int fd;
	char device[20];


	/*
	 * Open device file for reading.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);

	DPRINTF(1, ("starting FG with device %s\n",device));

	fd = refclock_open(device, SPEED232, LDISC_CLK);
	if (fd <= 0)
		return (0);
	
	/*
	 * Allocate and initialize unit structure
	 */

	up = emalloc(sizeof(struct fgunit));
	memset(up, 0, sizeof(struct fgunit));
	pp = peer->procptr;
	pp->unitptr = up;
	pp->io.clock_recv = fg_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
 	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		return 0;
	}

	
	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy(&pp->refid, REFID, 3);
	up->pollnum = 0;
	
	/* 
	 * Setup dating station to use GPS receiver.
	 * GPS receiver should work before this operation.
	 */
	if(!fg_init(pp->io.fd))
		refclock_report(peer, CEVNT_FAULT);

	return (1);
}


/*
 * fg_shutdown - shut down the clock
 */
static void
fg_shutdown(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	struct fgunit *up;
	
	pp = peer->procptr;
	up = pp->unitptr;
	if (pp->io.fd != -1)
		io_closeclock(&pp->io);
	if (up != NULL)
		free(up);
}


/*
 * fg_poll - called by the transmit procedure
 */
static void
fg_poll(
	int unit,
	struct peer *peer
	)
{
	struct refclockproc *pp;
	
	pp = peer->procptr;

	/*
	 * Time to poll the clock. The FG clock responds to a
	 * "<DLE>D<DLE><CR>" by returning a timecode in the format specified
	 * above. If nothing is heard from the clock for two polls,
	 * declare a timeout and keep going.
	 */

	if (write(pp->io.fd, fgdate, LENFG) != LENFG)
		refclock_report(peer, CEVNT_FAULT);
	else
		pp->polls++;

	/*
	if (pp->coderecv == pp->codeproc) {
		refclock_report(peer, CEVNT_TIMEOUT);
		return;
	}
	*/

	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	
	return;

}

/*
 * fg_receive - receive data from the serial interface
 */
static void
fg_receive(
	struct recvbuf *rbufp
	)
{
	struct refclockproc *pp;
	struct fgunit *up;
	struct peer *peer;
	char *bpt;

	/*
	 * Initialize pointers and read the timecode and timestamp
	 * We can't use gtlin function because we need bynary data in buf */

	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;

	/*
	 * Below hug to implement receiving of status information
	 */
	if(!up->pollnum) {
		up->pollnum++;
		return;
	}

	
	if (rbufp->recv_length < (LENFG - 2)) {
		refclock_report(peer, CEVNT_BADREPLY);
		return; /* The reply is invalid discard it. */
	}

	/* Below I trying to find a correct reply in buffer.
	 * Sometime GPS reply located in the beginnig of buffer,
	 * sometime you can find it with some offset.
	 */

	bpt = (char *)rbufp->recv_space.X_recv_buffer;
	while (*bpt != '\x10')
		bpt++;

#define BP2(x) ( bpt[x] & 15 )
#define BP1(x) (( bpt[x] & 240 ) >> 4)
	
	pp->year = BP1(2) * 10 + BP2(2);
	
	if (pp->year == 94) {
		refclock_report(peer, CEVNT_BADREPLY);
		if (!fg_init(pp->io.fd))
			refclock_report(peer, CEVNT_FAULT);
		return;
		 /* GPS is just powered up. The date is invalid -
		 discarding it. Initilize GPS one more time */
		/* Sorry - this driver will broken in 2094 ;) */
	}	
	
	if (pp->year < 99)
		pp->year += 100;

	pp->year +=  1900;
	pp->day = 100 * BP2(3) + 10 * BP1(4) + BP2(4);

/*
   After Jan, 10 2000 Forum Graphic GPS receiver had a very strange
   benahour. It doubles day number for an hours in replys after 10:10:10 UTC
   and doubles min every hour at HH:10:ss for a minute.
   Hope it is a problem of my unit only and not a Y2K problem of FG GPS. 
   Below small code to avoid such situation.
*/
	if (up->y2kwarn > 10)
		pp->hour = BP1(6)*10 + BP2(6);
	else
		pp->hour = BP1(5)*10 + BP2(5);

	if ((up->y2kwarn > 10) && (pp->hour == 10)) {
		pp->minute = BP1(7)*10 + BP2(7);
		pp->second = BP1(8)*10 + BP2(8);
		pp->nsec = (BP1(9)*10 + BP2(9)) * 1000000;
		pp->nsec += BP1(10) * 1000;
	} else {
		pp->hour = BP1(5)*10 + BP2(5);
		pp->minute = BP1(6)*10 + BP2(6);
		pp->second = BP1(7)*10 + BP2(7);
		pp->nsec = (BP1(8)*10 + BP2(8)) * 1000000;
		pp->nsec += BP1(9) * 1000;
	}

	if ((pp->hour == 10) && (pp->minute == 10)) {
		up->y2kwarn++;
	}

	snprintf(pp->a_lastcode, sizeof(pp->a_lastcode),
		 "%d %d %d %d %d", pp->year, pp->day, pp->hour,
		 pp->minute, pp->second);
	pp->lencode = strlen(pp->a_lastcode);
	/*get_systime(&pp->lastrec);*/

#ifdef DEBUG
	if (debug)
		printf("fg: time is %04d/%03d %02d:%02d:%02d UTC\n",
		       pp->year, pp->day, pp->hour, pp->minute, pp->second);
#endif
	pp->disp =  (10e-6);
	pp->lastrec = rbufp->recv_time; /* Is it better than get_systime()? */
	/* pp->leap = LEAP_NOWARNING; */

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp.
	 */

	if (!refclock_process(pp))
		refclock_report(peer, CEVNT_BADTIME);
	pp->lastref = pp->lastrec;
	refclock_receive(peer);
	return;
}


#else
int refclock_fg_bs;
#endif /* REFCLOCK */
