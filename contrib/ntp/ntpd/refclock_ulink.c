/*
 * refclock_ulink - clock driver for Ultralink  WWVB receiver
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_ULINK)

#include <stdio.h>
#include <ctype.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

/* This driver supports ultralink Model 320,325,330,331,332 WWVB radios
 *
 * this driver was based on the refclock_wwvb.c driver
 * in the ntp distribution.
 *
 * Fudge Factors
 *
 * fudge flag1 0 don't poll clock
 *             1 send poll character
 *
 * revision history:
 *		99/9/09 j.c.lang	original edit's
 *		99/9/11 j.c.lang	changed timecode parse to 
 *                                      match what the radio actually
 *                                      sends. 
 *              99/10/11 j.c.lang       added support for continous
 *                                      time code mode (dipsw2)
 *		99/11/26 j.c.lang	added support for 320 decoder
 *                                      (taken from Dave Strout's
 *                                      Model 320 driver)
 *		99/11/29 j.c.lang	added fudge flag 1 to control
 *					clock polling
 *		99/12/15 j.c.lang	fixed 320 quality flag
 *		01/02/21 s.l.smith	fixed 33x quality flag
 *					added more debugging stuff
 *					updated 33x time code explanation
 *		04/01/23 frank migge	added support for 325 decoder
 *                                      (tested with ULM325.F)
 *
 * Questions, bugs, ideas send to:
 *	Joseph C. Lang
 *	tcnojl1@earthlink.net
 *
 *	Dave Strout
 *	dstrout@linuxfoundry.com
 *
 *      Frank Migge
 *      frank.migge@oracle.com
 *
 *
 * on the Ultralink model 33X decoder Dip switch 2 controls
 * polled or continous timecode 
 * set fudge flag1 if using polled (needed for model 320 and 325)
 * dont set fudge flag1 if dip switch 2 is set on model 33x decoder
*/


/*
 * Interface definitions
 */
#define	DEVICE		"/dev/wwvb%d" /* device name and unit */
#define	SPEED232	B9600	/* uart speed (9600 baud) */
#define	PRECISION	(-10)	/* precision assumed (about 10 ms) */
#define	REFID		"WWVB"	/* reference ID */
#define	DESCRIPTION	"Ultralink WWVB Receiver" /* WRU */

#define	LEN33X		32	/* timecode length Model 33X and 325 */
#define LEN320		24	/* timecode length Model 320 */

#define	SIGLCHAR33x	'S'	/* signal strength identifier char 325 */
#define	SIGLCHAR325	'R'	/* signal strength identifier char 33x */

/*
 *  unit control structure
 */
struct ulinkunit {
	u_char	tcswitch;	/* timecode switch */
	l_fp	laststamp;	/* last receive timestamp */
};

/*
 * Function prototypes
 */
static	int	ulink_start	(int, struct peer *);
static	void	ulink_shutdown	(int, struct peer *);
static	void	ulink_receive	(struct recvbuf *);
static	void	ulink_poll	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_ulink = {
	ulink_start,		/* start up driver */
	ulink_shutdown,		/* shut down driver */
	ulink_poll,		/* transmit poll message */
	noentry,		/* not used  */
	noentry,		/* not used  */
	noentry,		/* not used  */
	NOFLAGS
};


/*
 * ulink_start - open the devices and initialize data for processing
 */
static int
ulink_start(
	int unit,
	struct peer *peer
	)
{
	register struct ulinkunit *up;
	struct refclockproc *pp;
	int fd;
	char device[20];

	/*
	 * Open serial port. Use CLK line discipline, if available.
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	fd = refclock_open(device, SPEED232, LDISC_CLK);
	if (fd <= 0)
		return (0);

	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc(sizeof(struct ulinkunit));
	memset(up, 0, sizeof(struct ulinkunit));
	pp = peer->procptr;
	pp->io.clock_recv = ulink_receive;
	pp->io.srcclock = peer;
	pp->io.datalen = 0;
	pp->io.fd = fd;
	if (!io_addclock(&pp->io)) {
		close(fd);
		pp->io.fd = -1;
		free(up);
		return (0);
	}
	pp->unitptr = up;

	/*
	 * Initialize miscellaneous variables
	 */
	peer->precision = PRECISION;
	pp->clockdesc = DESCRIPTION;
	memcpy((char *)&pp->refid, REFID, 4);
	return (1);
}


/*
 * ulink_shutdown - shut down the clock
 */
static void
ulink_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct ulinkunit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (pp->io.fd != -1)
		io_closeclock(&pp->io);
	if (up != NULL)
		free(up);
}


/*
 * ulink_receive - receive data from the serial interface
 */
static void
ulink_receive(
	struct recvbuf *rbufp
	)
{
	struct ulinkunit *up;
	struct refclockproc *pp;
	struct peer *peer;

	l_fp	trtmp;			/* arrival timestamp */
	int	quality = INT_MAX;	/* quality indicator */
	int	temp;			/* int temp */
	char	syncchar;		/* synchronization indicator */
	char	leapchar;		/* leap indicator */
	char	modechar;		/* model 320 mode flag */
        char	siglchar;		/* model difference between 33x/325 */
	char	char_quality[2];	/* temp quality flag */

	/*
	 * Initialize pointers and read the timecode and timestamp
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	temp = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &trtmp);

	/*
	 * Note we get a buffer and timestamp for both a <cr> and <lf>,
	 * but only the <cr> timestamp is retained. 
	 */
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
		printf("ulink: timecode %d %s\n", pp->lencode,
		    pp->a_lastcode);
#endif

	/*
	 * We get down to business, check the timecode format and decode
	 * its contents. If the timecode has invalid length or is not in
	 * proper format, we declare bad format and exit.
	 */
	syncchar = leapchar = modechar = siglchar = ' ';
	switch (pp->lencode ) {
		case LEN33X:

		/*
                 * First we check if the format is 33x or 325:
		 *   <CR><LF>S9+D 00 YYYY+DDDUTCS HH:MM:SSL+5 (33x)
		 *   <CR><LF>R5_1C00LYYYY+DDDUTCS HH:MM:SSL+5 (325)
		 * simply by comparing if the signal level is 'S' or 'R'
                 */

                 if (sscanf(pp->a_lastcode, "%c%*31c",
                            &siglchar) == 1) {

                    if(siglchar == SIGLCHAR325) {

       		   /*
		    * decode for a Model 325 decoder.
		    * Timecode format from January 23, 2004 datasheet is:
                    *
		    *   <CR><LF>R5_1C00LYYYY+DDDUTCS HH:MM:SSL+5
                    *
		    *   R      WWVB decodersignal readability R1 - R5
		    *   5      R1 is unreadable, R5 is best
		    *   space  a space (0x20)
		    *   1      Data bit 0, 1, M (pos mark), or ? (unknown).
		    *   C      Reception from either (C)olorado or (H)awaii 
		    *   00     Hours since last good WWVB frame sync. Will 
		    *          be 00-99
		    *   space  Space char (0x20) or (0xa5) if locked to wwvb
		    *   YYYY   Current year, 2000-2099
		    *   +      Leap year indicator. '+' if a leap year,
		    *          a space (0x20) if not.
		    *   DDD    Day of year, 000 - 365.
		    *   UTC    Timezone (always 'UTC').
		    *   S      Daylight savings indicator
		    *             S - standard time (STD) in effect
		    *             O - during STD to DST day 0000-2400
		    *             D - daylight savings time (DST) in effect
		    *             I - during DST to STD day 0000-2400
		    *   space  Space character (0x20)
		    *   HH     Hours 00-23
		    *   :      This is the REAL in sync indicator (: = insync)	
		    *   MM     Minutes 00-59
		    *   :      : = in sync ? = NOT in sync
		    *   SS     Seconds 00-59
		    *   L      Leap second flag. Changes from space (0x20)
		    *          to 'I' or 'D' during month preceding leap
		    *          second adjustment. (I)nsert or (D)elete
		    *   +5     UT1 correction (sign + digit ))
		    */

   		       if (sscanf(pp->a_lastcode, 
                          "%*2c %*2c%2c%*c%4d%*c%3d%*4c %2d%c%2d:%2d%c%*2c",
   		          char_quality, &pp->year, &pp->day, 
                          &pp->hour, &syncchar, &pp->minute, &pp->second, 
                          &leapchar) == 8) { 
   		
   			  if (char_quality[0] == '0') {
   				quality = 0;
   			  } else if (char_quality[0] == '0') {
   				quality = (char_quality[1] & 0x0f);
   			  } else  {
   				quality = 99;
   			  }

   		          if (leapchar == 'I' ) leapchar = '+';
   		          if (leapchar == 'D' ) leapchar = '-';

		          /*
		          #ifdef DEBUG
		          if (debug) {
		             printf("ulink: char_quality %c %c\n", 
                                    char_quality[0], char_quality[1]);
			     printf("ulink: quality %d\n", quality);
			     printf("ulink: syncchar %x\n", syncchar);
			     printf("ulink: leapchar %x\n", leapchar);
                          }
                          #endif
                          */

                       }
		
                    } 
                    if(siglchar == SIGLCHAR33x) {
                
		   /*
		    * We got a Model 33X decoder.
		    * Timecode format from January 29, 2001 datasheet is:
		    *   <CR><LF>S9+D 00 YYYY+DDDUTCS HH:MM:SSL+5
		    *   S      WWVB decoder sync indicator. S for in-sync(?)
		    *          or N for noisy signal.
		    *   9+     RF signal level in S-units, 0-9 followed by
		    *          a space (0x20). The space turns to '+' if the
		    *          level is over 9.
		    *   D      Data bit 0, 1, 2 (position mark), or
		    *          3 (unknown).
		    *   space  Space character (0x20)
		    *   00     Hours since last good WWVB frame sync. Will 
		    *          be 00-23 hrs, or '1d' to '7d'. Will be 'Lk'
                    *          if currently in sync. 
		    *   space  Space character (0x20)
		    *   YYYY   Current year, 1990-2089
		    *   +      Leap year indicator. '+' if a leap year,
		    *          a space (0x20) if not.
		    *   DDD    Day of year, 001 - 366.
		    *   UTC    Timezone (always 'UTC').
		    *   S      Daylight savings indicator
		    *             S - standard time (STD) in effect
		    *             O - during STD to DST day 0000-2400
		    *             D - daylight savings time (DST) in effect
		    *             I - during DST to STD day 0000-2400
		    *   space  Space character (0x20)
		    *   HH     Hours 00-23
		    *   :      This is the REAL in sync indicator (: = insync)	
		    *   MM     Minutes 00-59
		    *   :      : = in sync ? = NOT in sync
		    *   SS     Seconds 00-59
		    *   L      Leap second flag. Changes from space (0x20)
		    *          to '+' or '-' during month preceding leap
		    *          second adjustment.
		    *   +5     UT1 correction (sign + digit ))
		    */

		       if (sscanf(pp->a_lastcode, 
                           "%*4c %2c %4d%*c%3d%*4c %2d%c%2d:%2d%c%*2c",
		           char_quality, &pp->year, &pp->day, 
                           &pp->hour, &syncchar, &pp->minute, &pp->second, 
                           &leapchar) == 8) { 
		
			   if (char_quality[0] == 'L') {
				quality = 0;
			   } else if (char_quality[0] == '0') {
				quality = (char_quality[1] & 0x0f);
			   } else  {
				quality = 99;
		           }
	
                           /*
                           #ifdef DEBUG
         		   if (debug) {
         			printf("ulink: char_quality %c %c\n", 
                                        char_quality[0], char_quality[1]);
         			printf("ulink: quality %d\n", quality);
         			printf("ulink: syncchar %x\n", syncchar);
         			printf("ulink: leapchar %x\n", leapchar);
                           }
                           #endif
                           */

		        }
                    }
		    break;
		}

		case LEN320:

	        /*
		 * Model 320 Decoder
		 * The timecode format is:
		 *
		 *  <cr><lf>SQRYYYYDDD+HH:MM:SS.mmLT<cr>
		 *
		 * where:
		 *
		 * S = 'S' -- sync'd in last hour,
		 *     '0'-'9' - hours x 10 since last update,
		 *     '?' -- not in sync
		 * Q = Number of correlating time-frames, from 0 to 5
		 * R = 'R' -- reception in progress,
		 *     'N' -- Noisy reception,
		 *     ' ' -- standby mode
		 * YYYY = year from 1990 to 2089
		 * DDD = current day from 1 to 366
		 * + = '+' if current year is a leap year, else ' '
		 * HH = UTC hour 0 to 23
		 * MM = Minutes of current hour from 0 to 59
		 * SS = Seconds of current minute from 0 to 59
		 * mm = 10's milliseconds of the current second from 00 to 99
		 * L  = Leap second pending at end of month
		 *     'I' = insert, 'D'= delete
		 * T  = DST <-> STD transition indicators
		 *
        	 */

		if (sscanf(pp->a_lastcode, "%c%1d%c%4d%3d%*c%2d:%2d:%2d.%2ld%c",
	               &syncchar, &quality, &modechar, &pp->year, &pp->day,
        	       &pp->hour, &pp->minute, &pp->second,
			&pp->nsec, &leapchar) == 10) {
		pp->nsec *= 10000000; /* M320 returns 10's of msecs */
		if (leapchar == 'I' ) leapchar = '+';
		if (leapchar == 'D' ) leapchar = '-';
		if (syncchar != '?' ) syncchar = ':';

 		break;
		}

		default:
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/*
	 * Decode quality indicator
	 * For the 325 & 33x series, the lower the number the "better" 
	 * the time is. I used the dispersion as the measure of time 
	 * quality. The quality indicator in the 320 is the number of 
	 * correlating time frames (the more the better)
	 */

	/* 
	 * The spec sheet for the 325 & 33x series states the clock will
	 * maintain +/-0.002 seconds accuracy when locked to WWVB. This 
	 * is indicated by 'Lk' in the quality portion of the incoming 
	 * string. When not in lock, a drift of +/-0.015 seconds should 
	 * be allowed for.
	 * With the quality indicator decoding scheme above, the 'Lk' 
	 * condition will produce a quality value of 0. If the quality 
	 * indicator starts with '0' then the second character is the 
	 * number of hours since we were last locked. If the first 
	 * character is anything other than 'L' or '0' then we have been 
	 * out of lock for more than 9 hours so we assume the worst and 
	 * force a quality value that selects the 'default' maximum 
	 * dispersion. The dispersion values below are what came with the
	 * driver. They're not unreasonable so they've not been changed.
	 */

	if (pp->lencode == LEN33X) {
		switch (quality) {
			case 0 :
				pp->disp=.002;
				break;
			case 1 :
				pp->disp=.02;
				break;
			case 2 :
				pp->disp=.04;
				break;
			case 3 :
				pp->disp=.08;
				break;
			default:
				pp->disp=MAXDISPERSE;
				break;
		}
	} else {
		switch (quality) {
			case 5 :
				pp->disp=.002;
				break;
			case 4 :
				pp->disp=.02;
				break;
			case 3 :
				pp->disp=.04;
				break;
			case 2 :
				pp->disp=.08;
				break;
			case 1 :
				pp->disp=.16;
				break;
			default:
				pp->disp=MAXDISPERSE;
				break;
		}

	}

	/*
	 * Decode synchronization, and leap characters. If
	 * unsynchronized, set the leap bits accordingly and exit.
	 * Otherwise, set the leap bits according to the leap character.
	 */

	if (syncchar != ':')
		pp->leap = LEAP_NOTINSYNC;
	else if (leapchar == '+')
		pp->leap = LEAP_ADDSECOND;
	else if (leapchar == '-')
		pp->leap = LEAP_DELSECOND;
	else
		pp->leap = LEAP_NOWARNING;

	/*
	 * Process the new sample in the median filter and determine the
	 * timecode timestamp.
	 */
	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
	}

}

/*
 * ulink_poll - called by the transmit procedure
 */

static void
ulink_poll(
	int unit,
	struct peer *peer
	)
{
        struct refclockproc *pp;
        char pollchar;

        pp = peer->procptr;
        pollchar = 'T';
	if (pp->sloppyclockflag & CLK_FLAG1) {
	        if (write(pp->io.fd, &pollchar, 1) != 1)
        	        refclock_report(peer, CEVNT_FAULT);
        	else
      	            pp->polls++;
	}
	else
      	            pp->polls++;

        if (pp->coderecv == pp->codeproc) {
                refclock_report(peer, CEVNT_TIMEOUT);
                return;
        }
        pp->lastref = pp->lastrec;
	refclock_receive(peer);
	record_clock_stats(&peer->srcadr, pp->a_lastcode);

}

#else
int refclock_ulink_bs;
#endif /* REFCLOCK */
