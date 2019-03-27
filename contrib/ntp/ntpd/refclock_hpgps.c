/*
 * refclock_hpgps - clock driver for HP 58503A GPS receiver
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(REFCLOCK) && defined(CLOCK_HPGPS)

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_stdlib.h"

#include <stdio.h>
#include <ctype.h>

/* Version 0.1 April  1, 1995  
 *         0.2 April 25, 1995
 *             tolerant of missing timecode response prompt and sends
 *             clear status if prompt indicates error;
 *             can use either local time or UTC from receiver;
 *             can get receiver status screen via flag4
 *
 * WARNING!: This driver is UNDER CONSTRUCTION
 * Everything in here should be treated with suspicion.
 * If it looks wrong, it probably is.
 *
 * Comments and/or questions to: Dave Vitanye
 *                               Hewlett Packard Company
 *                               dave@scd.hp.com
 *                               (408) 553-2856
 *
 * Thanks to the author of the PST driver, which was the starting point for
 * this one.
 *
 * This driver supports the HP 58503A Time and Frequency Reference Receiver.
 * This receiver uses HP SmartClock (TM) to implement an Enhanced GPS receiver.
 * The receiver accuracy when locked to GPS in normal operation is better
 * than 1 usec. The accuracy when operating in holdover is typically better
 * than 10 usec. per day.
 *
 * The same driver also handles the HP Z3801A which is available surplus
 * from the cell phone industry.  It's popular with hams.
 * It needs a different line setup: 19200 baud, 7 data bits, odd parity
 * That is selected by adding "mode 1" to the server line in ntp.conf
 * HP Z3801A code from Jeff Mock added by Hal Murray, Sep 2005
 *
 *
 * The receiver should be operated with factory default settings.
 * Initial driver operation: expects the receiver to be already locked
 * to GPS, configured and able to output timecode format 2 messages.
 *
 * The driver uses the poll sequence :PTIME:TCODE? to get a response from
 * the receiver. The receiver responds with a timecode string of ASCII
 * printing characters, followed by a <cr><lf>, followed by a prompt string
 * issued by the receiver, in the following format:
 * T#yyyymmddhhmmssMFLRVcc<cr><lf>scpi > 
 *
 * The driver processes the response at the <cr> and <lf>, so what the
 * driver sees is the prompt from the previous poll, followed by this
 * timecode. The prompt from the current poll is (usually) left unread until
 * the next poll. So (except on the very first poll) the driver sees this:
 *
 * scpi > T#yyyymmddhhmmssMFLRVcc<cr><lf>
 *
 * The T is the on-time character, at 980 msec. before the next 1PPS edge.
 * The # is the timecode format type. We look for format 2.
 * Without any of the CLK or PPS stuff, then, the receiver buffer timestamp
 * at the <cr> is 24 characters later, which is about 25 msec. at 9600 bps,
 * so the first approximation for fudge time1 is nominally -0.955 seconds.
 * This number probably needs adjusting for each machine / OS type, so far:
 *  -0.955000 on an HP 9000 Model 712/80 HP-UX 9.05
 *  -0.953175 on an HP 9000 Model 370    HP-UX 9.10 
 *
 * This receiver also provides a 1PPS signal, but I haven't figured out
 * how to deal with any of the CLK or PPS stuff yet. Stay tuned.
 *
 */

/*
 * Fudge Factors
 *
 * Fudge time1 is used to accomodate the timecode serial interface adjustment.
 * Fudge flag4 can be set to request a receiver status screen summary, which
 * is recorded in the clockstats file.
 */

/*
 * Interface definitions
 */
#define	DEVICE		"/dev/hpgps%d" /* device name and unit */
#define	SPEED232	B9600	/* uart speed (9600 baud) */
#define	SPEED232Z	B19200	/* uart speed (19200 baud) */
#define	PRECISION	(-10)	/* precision assumed (about 1 ms) */
#define	REFID		"GPS\0"	/*  reference ID */
#define	DESCRIPTION	"HP 58503A GPS Time and Frequency Reference Receiver" 

#define SMAX            23*80+1 /* for :SYSTEM:PRINT? status screen response */

#define MTZONE          2       /* number of fields in timezone reply */
#define MTCODET2        12      /* number of fields in timecode format T2 */
#define NTCODET2        21      /* number of chars to checksum in format T2 */

/*
 * Tables to compute the day of year from yyyymmdd timecode.
 * Viva la leap.
 */
static int day1tab[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int day2tab[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/*
 * Unit control structure
 */
struct hpgpsunit {
	int	pollcnt;	/* poll message counter */
	int     tzhour;         /* timezone offset, hours */
	int     tzminute;       /* timezone offset, minutes */
	int     linecnt;        /* set for expected multiple line responses */
	char	*lastptr;	/* pointer to receiver response data */
	char    statscrn[SMAX]; /* receiver status screen buffer */
};

/*
 * Function prototypes
 */
static	int	hpgps_start	(int, struct peer *);
static	void	hpgps_shutdown	(int, struct peer *);
static	void	hpgps_receive	(struct recvbuf *);
static	void	hpgps_poll	(int, struct peer *);

/*
 * Transfer vector
 */
struct	refclock refclock_hpgps = {
	hpgps_start,		/* start up driver */
	hpgps_shutdown,		/* shut down driver */
	hpgps_poll,		/* transmit poll message */
	noentry,		/* not used (old hpgps_control) */
	noentry,		/* initialize driver */
	noentry,		/* not used (old hpgps_buginfo) */
	NOFLAGS			/* not used */
};


/*
 * hpgps_start - open the devices and initialize data for processing
 */
static int
hpgps_start(
	int unit,
	struct peer *peer
	)
{
	register struct hpgpsunit *up;
	struct refclockproc *pp;
	int fd;
	int speed, ldisc;
	char device[20];

	/*
	 * Open serial port. Use CLK line discipline, if available.
	 * Default is HP 58503A, mode arg selects HP Z3801A
	 */
	snprintf(device, sizeof(device), DEVICE, unit);
	ldisc = LDISC_CLK;
	speed = SPEED232;
	/* mode parameter to server config line shares ttl slot */
	if (1 == peer->ttl) {
		ldisc |= LDISC_7O1;
		speed = SPEED232Z;
	}
	fd = refclock_open(device, speed, ldisc);
	if (fd <= 0)
		return (0);
	/*
	 * Allocate and initialize unit structure
	 */
	up = emalloc_zero(sizeof(*up));
	pp = peer->procptr;
	pp->io.clock_recv = hpgps_receive;
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
	up->tzhour = 0;
	up->tzminute = 0;

	*up->statscrn = '\0';
	up->lastptr = up->statscrn;
	up->pollcnt = 2;

	/*
	 * Get the identifier string, which is logged but otherwise ignored,
	 * and get the local timezone information
	 */
	up->linecnt = 1;
	if (write(pp->io.fd, "*IDN?\r:PTIME:TZONE?\r", 20) != 20)
	    refclock_report(peer, CEVNT_FAULT);

	return (1);
}


/*
 * hpgps_shutdown - shut down the clock
 */
static void
hpgps_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct hpgpsunit *up;
	struct refclockproc *pp;

	pp = peer->procptr;
	up = pp->unitptr;
	if (-1 != pp->io.fd)
		io_closeclock(&pp->io);
	if (NULL != up)
		free(up);
}


/*
 * hpgps_receive - receive data from the serial interface
 */
static void
hpgps_receive(
	struct recvbuf *rbufp
	)
{
	register struct hpgpsunit *up;
	struct refclockproc *pp;
	struct peer *peer;
	l_fp trtmp;
	char tcodechar1;        /* identifies timecode format */
	char tcodechar2;        /* identifies timecode format */
	char timequal;          /* time figure of merit: 0-9 */
	char freqqual;          /* frequency figure of merit: 0-3 */
	char leapchar;          /* leapsecond: + or 0 or - */
	char servchar;          /* request for service: 0 = no, 1 = yes */
	char syncchar;          /* time info is invalid: 0 = no, 1 = yes */
	short expectedsm;       /* expected timecode byte checksum */
	short tcodechksm;       /* computed timecode byte checksum */
	int i,m,n;
	int month, day, lastday;
	char *tcp;              /* timecode pointer (skips over the prompt) */
	char prompt[BMAX];      /* prompt in response from receiver */

	/*
	 * Initialize pointers and read the receiver response
	 */
	peer = rbufp->recv_peer;
	pp = peer->procptr;
	up = pp->unitptr;
	*pp->a_lastcode = '\0';
	pp->lencode = refclock_gtlin(rbufp, pp->a_lastcode, BMAX, &trtmp);

#ifdef DEBUG
	if (debug)
	    printf("hpgps: lencode: %d timecode:%s\n",
		   pp->lencode, pp->a_lastcode);
#endif

	/*
	 * If there's no characters in the reply, we can quit now
	 */
	if (pp->lencode == 0)
	    return;

	/*
	 * If linecnt is greater than zero, we are getting information only,
	 * such as the receiver identification string or the receiver status
	 * screen, so put the receiver response at the end of the status
	 * screen buffer. When we have the last line, write the buffer to
	 * the clockstats file and return without further processing.
	 *
	 * If linecnt is zero, we are expecting either the timezone
	 * or a timecode. At this point, also write the response
	 * to the clockstats file, and go on to process the prompt (if any),
	 * timezone, or timecode and timestamp.
	 */


	if (up->linecnt-- > 0) {
		if ((int)(pp->lencode + 2) <= (SMAX - (up->lastptr - up->statscrn))) {
			*up->lastptr++ = '\n';
			memcpy(up->lastptr, pp->a_lastcode, pp->lencode);
			up->lastptr += pp->lencode;
		}
		if (up->linecnt == 0) 
		    record_clock_stats(&peer->srcadr, up->statscrn);
               
		return;
	}

	record_clock_stats(&peer->srcadr, pp->a_lastcode);
	pp->lastrec = trtmp;
            
	up->lastptr = up->statscrn;
	*up->lastptr = '\0';
	up->pollcnt = 2;

	/*
	 * We get down to business: get a prompt if one is there, issue
	 * a clear status command if it contains an error indication.
	 * Next, check for either the timezone reply or the timecode reply
	 * and decode it.  If we don't recognize the reply, or don't get the
	 * proper number of decoded fields, or get an out of range timezone,
	 * or if the timecode checksum is bad, then we declare bad format
	 * and exit.
	 *
	 * Timezone format (including nominal prompt):
	 * scpi > -H,-M<cr><lf>
	 *
	 * Timecode format (including nominal prompt):
	 * scpi > T2yyyymmddhhmmssMFLRVcc<cr><lf>
	 *
	 */

	strlcpy(prompt, pp->a_lastcode, sizeof(prompt));
	tcp = strrchr(pp->a_lastcode,'>');
	if (tcp == NULL)
	    tcp = pp->a_lastcode; 
	else
	    tcp++;
	prompt[tcp - pp->a_lastcode] = '\0';
	while ((*tcp == ' ') || (*tcp == '\t')) tcp++;

	/*
	 * deal with an error indication in the prompt here
	 */
	if (strrchr(prompt,'E') > strrchr(prompt,'s')){
#ifdef DEBUG
		if (debug)
		    printf("hpgps: error indicated in prompt: %s\n", prompt);
#endif
		if (write(pp->io.fd, "*CLS\r\r", 6) != 6)
		    refclock_report(peer, CEVNT_FAULT);
	}

	/*
	 * make sure we got a timezone or timecode format and 
	 * then process accordingly
	 */
	m = sscanf(tcp,"%c%c", &tcodechar1, &tcodechar2);

	if (m != 2){
#ifdef DEBUG
		if (debug)
		    printf("hpgps: no format indicator\n");
#endif
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	switch (tcodechar1) {

	    case '+':
	    case '-':
		m = sscanf(tcp,"%d,%d", &up->tzhour, &up->tzminute);
		if (m != MTZONE) {
#ifdef DEBUG
			if (debug)
			    printf("hpgps: only %d fields recognized in timezone\n", m);
#endif
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		}
		if ((up->tzhour < -12) || (up->tzhour > 13) || 
		    (up->tzminute < -59) || (up->tzminute > 59)){
#ifdef DEBUG
			if (debug)
			    printf("hpgps: timezone %d, %d out of range\n",
				   up->tzhour, up->tzminute);
#endif
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		}
		return;

	    case 'T':
		break;

	    default:
#ifdef DEBUG
		if (debug)
		    printf("hpgps: unrecognized reply format %c%c\n",
			   tcodechar1, tcodechar2);
#endif
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	} /* end of tcodechar1 switch */


	switch (tcodechar2) {

	    case '2':
		m = sscanf(tcp,"%*c%*c%4d%2d%2d%2d%2d%2d%c%c%c%c%c%2hx",
			   &pp->year, &month, &day, &pp->hour, &pp->minute, &pp->second,
			   &timequal, &freqqual, &leapchar, &servchar, &syncchar,
			   &expectedsm);
		n = NTCODET2;

		if (m != MTCODET2){
#ifdef DEBUG
			if (debug)
			    printf("hpgps: only %d fields recognized in timecode\n", m);
#endif
			refclock_report(peer, CEVNT_BADREPLY);
			return;
		}
		break;

	    default:
#ifdef DEBUG
		if (debug)
		    printf("hpgps: unrecognized timecode format %c%c\n",
			   tcodechar1, tcodechar2);
#endif
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	} /* end of tcodechar2 format switch */
           
	/* 
	 * Compute and verify the checksum.
	 * Characters are summed starting at tcodechar1, ending at just
	 * before the expected checksum.  Bail out if incorrect.
	 */
	tcodechksm = 0;
	while (n-- > 0) tcodechksm += *tcp++;
	tcodechksm &= 0x00ff;

	if (tcodechksm != expectedsm) {
#ifdef DEBUG
		if (debug)
		    printf("hpgps: checksum %2hX doesn't match %2hX expected\n",
			   tcodechksm, expectedsm);
#endif
		refclock_report(peer, CEVNT_BADREPLY);
		return;
	}

	/* 
	 * Compute the day of year from the yyyymmdd format.
	 */
	if (month < 1 || month > 12 || day < 1) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}

	if ( ! isleap_4(pp->year) ) {				/* Y2KFixes */
		/* not a leap year */
		if (day > day1tab[month - 1]) {
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}
		for (i = 0; i < month - 1; i++) day += day1tab[i];
		lastday = 365;
	} else {
		/* a leap year */
		if (day > day2tab[month - 1]) {
			refclock_report(peer, CEVNT_BADTIME);
			return;
		}
		for (i = 0; i < month - 1; i++) day += day2tab[i];
		lastday = 366;
	}

	/*
	 * Deal with the timezone offset here. The receiver timecode is in
	 * local time = UTC + :PTIME:TZONE, so SUBTRACT the timezone values.
	 * For example, Pacific Standard Time is -8 hours , 0 minutes.
	 * Deal with the underflows and overflows.
	 */
	pp->minute -= up->tzminute;
	pp->hour -= up->tzhour;

	if (pp->minute < 0) {
		pp->minute += 60;
		pp->hour--;
	}
	if (pp->minute > 59) {
		pp->minute -= 60;
		pp->hour++;
	}
	if (pp->hour < 0)  {
		pp->hour += 24;
		day--;
		if (day < 1) {
			pp->year--;
			if ( isleap_4(pp->year) )		/* Y2KFixes */
			    day = 366;
			else
			    day = 365;
		}
	}

	if (pp->hour > 23) {
		pp->hour -= 24;
		day++;
		if (day > lastday) {
			pp->year++;
			day = 1;
		}
	}

	pp->day = day;

	/*
	 * Decode the MFLRV indicators.
	 * NEED TO FIGURE OUT how to deal with the request for service,
	 * time quality, and frequency quality indicators some day. 
	 */
	if (syncchar != '0') {
		pp->leap = LEAP_NOTINSYNC;
	}
	else {
		pp->leap = LEAP_NOWARNING;
		switch (leapchar) {

		    case '0':
			break;
                     
		    /* See http://bugs.ntp.org/1090
		     * Ignore leap announcements unless June or December.
		     * Better would be to use :GPSTime? to find the month,
		     * but that seems too likely to introduce other bugs.
		     */
		    case '+':
			if ((month==6) || (month==12))
			    pp->leap = LEAP_ADDSECOND;
			break;
                     
		    case '-':
			if ((month==6) || (month==12))
			    pp->leap = LEAP_DELSECOND;
			break;
                     
		    default:
#ifdef DEBUG
			if (debug)
			    printf("hpgps: unrecognized leap indicator: %c\n",
				   leapchar);
#endif
			refclock_report(peer, CEVNT_BADTIME);
			return;
		} /* end of leapchar switch */
	}

	/*
	 * Process the new sample in the median filter and determine the
	 * reference clock offset and dispersion. We use lastrec as both
	 * the reference time and receive time in order to avoid being
	 * cute, like setting the reference time later than the receive
	 * time, which may cause a paranoid protocol module to chuck out
	 * the data.
	 */
	if (!refclock_process(pp)) {
		refclock_report(peer, CEVNT_BADTIME);
		return;
	}
	pp->lastref = pp->lastrec;
	refclock_receive(peer);

	/*
	 * If CLK_FLAG4 is set, ask for the status screen response.
	 */
	if (pp->sloppyclockflag & CLK_FLAG4){
		up->linecnt = 22; 
		if (write(pp->io.fd, ":SYSTEM:PRINT?\r", 15) != 15)
		    refclock_report(peer, CEVNT_FAULT);
	}
}


/*
 * hpgps_poll - called by the transmit procedure
 */
static void
hpgps_poll(
	int unit,
	struct peer *peer
	)
{
	register struct hpgpsunit *up;
	struct refclockproc *pp;

	/*
	 * Time to poll the clock. The HP 58503A responds to a
	 * ":PTIME:TCODE?" by returning a timecode in the format specified
	 * above. If nothing is heard from the clock for two polls,
	 * declare a timeout and keep going.
	 */
	pp = peer->procptr;
	up = pp->unitptr;
	if (up->pollcnt == 0)
	    refclock_report(peer, CEVNT_TIMEOUT);
	else
	    up->pollcnt--;
	if (write(pp->io.fd, ":PTIME:TCODE?\r", 14) != 14) {
		refclock_report(peer, CEVNT_FAULT);
	}
	else
	    pp->polls++;
}

#else
int refclock_hpgps_bs;
#endif /* REFCLOCK */
