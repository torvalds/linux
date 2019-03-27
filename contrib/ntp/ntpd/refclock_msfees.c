/* refclock_ees - clock driver for the EES M201 receiver */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ntp_types.h"

#if defined(REFCLOCK) && defined(CLOCK_MSFEES) && defined(PPS)

/* Currently REQUIRES STREAM and PPSCD. CLK and CBREAK modes
 * were removed as the code was overly hairy, they weren't in use
 * (hence probably didn't work).  Still in RCS file at cl.cam.ac.uk
 */

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "timevalops.h"

#include <ctype.h>
#if defined(HAVE_BSD_TTYS)
#include <sgtty.h>
#endif /* HAVE_BSD_TTYS */
#if defined(HAVE_SYSV_TTYS)
#include <termio.h>
#endif /* HAVE_SYSV_TTYS */
#if defined(HAVE_TERMIOS)
#include <termios.h>
#endif
#if defined(STREAM)
#include <stropts.h>
#endif

#ifdef HAVE_SYS_TERMIOS_H
# include <sys/termios.h>
#endif
#ifdef HAVE_SYS_PPSCLOCK_H
# include <sys/ppsclock.h>
#endif

#include "ntp_stdlib.h"

int dbg = 0;
/*
	fudgefactor	= fudgetime1;
	os_delay	= fudgetime2;
	   offset_fudge	= os_delay + fudgefactor + inherent_delay;
	stratumtouse	= fudgeval1 & 0xf
	dbg		= fudgeval2;
	sloppyclockflag	= flags & CLK_FLAG1;
		1	  log smoothing summary when processing sample
		4	  dump the buffer from the clock
		8	  EIOGETKD the last n uS time stamps
	if (flags & CLK_FLAG2 && unitinuse) ees->leaphold = 0;
	ees->dump_vals	= flags & CLK_FLAG3;
	ees->usealldata	= flags & CLK_FLAG4;


	bug->values[0] = (ees->lasttime) ? current_time - ees->lasttime : 0;
	bug->values[1] = (ees->clocklastgood)?current_time-ees->clocklastgood:0;
	bug->values[2] = (u_long)ees->status;
	bug->values[3] = (u_long)ees->lastevent;
	bug->values[4] = (u_long)ees->reason;
	bug->values[5] = (u_long)ees->nsamples;
	bug->values[6] = (u_long)ees->codestate;
	bug->values[7] = (u_long)ees->day;
	bug->values[8] = (u_long)ees->hour;
	bug->values[9] = (u_long)ees->minute;
	bug->values[10] = (u_long)ees->second;
	bug->values[11] = (u_long)ees->tz;
	bug->values[12] = ees->yearstart;
	bug->values[13] = (ees->leaphold > current_time) ?
				ees->leaphold - current_time : 0;
	bug->values[14] = inherent_delay[unit].l_uf;
	bug->values[15] = offset_fudge[unit].l_uf;

	bug->times[0] = ees->reftime;
	bug->times[1] = ees->arrvtime;
	bug->times[2] = ees->lastsampletime;
	bug->times[3] = ees->offset;
	bug->times[4] = ees->lowoffset;
	bug->times[5] = ees->highoffset;
	bug->times[6] = inherent_delay[unit];
	bug->times[8] = os_delay[unit];
	bug->times[7] = fudgefactor[unit];
	bug->times[9] = offset_fudge[unit];
	bug->times[10]= ees->yearstart, 0;
	*/

/* This should support the use of an EES M201 receiver with RS232
 * output (modified to transmit time once per second).
 *
 * For the format of the message sent by the clock, see the EESM_
 * definitions below.
 *
 * It appears to run free for an integral number of minutes, until the error
 * reaches 4mS, at which point it steps at second = 01.
 * It appears that sometimes it steps 4mS (say at 7 min interval),
 * then the next minute it decides that it was an error, so steps back.
 * On the next minute it steps forward again :-(
 * This is typically 16.5uS/S then 3975uS at the 4min re-sync,
 * or 9.5uS/S then 3990.5uS at a 7min re-sync,
 * at which point it may lose the "00" second time stamp.
 * I assume that the most accurate time is just AFTER the re-sync.
 * Hence remember the last cycle interval,
 *
 * Can run in any one of:
 *
 *	PPSCD	PPS signal sets CD which interupts, and grabs the current TOD
 *	(sun)		*in the interupt code*, so as to avoid problems with
 *			the STREAMS scheduling.
 *
 * It appears that it goes 16.5 uS slow each second, then every 4 mins it
 * generates no "00" second tick, and gains 3975 uS. Ho Hum ! (93/2/7)
 */

/* Definitions */
#ifndef	MAXUNITS
#define	MAXUNITS	4	/* maximum number of EES units permitted */
#endif

#ifndef	EES232
#define	EES232	"/dev/ees%d"	/* Device to open to read the data */
#endif

/* Other constant stuff */
#ifndef	EESPRECISION
#define	EESPRECISION	(-10)		/* what the heck - 2**-10 = 1ms */
#endif
#ifndef	EESREFID
#define	EESREFID	"MSF\0"		/* String to identify the clock */
#endif
#ifndef	EESHSREFID
#define	EESHSREFID	(0x7f7f0000 | ((REFCLK_MSF_EES) << 8)) /* Numeric refid */
#endif

/* Description of clock */
#define	EESDESCRIPTION		"EES M201 MSF Receiver"

/* Speed we run the clock port at. If this is changed the UARTDELAY
 * value should be recomputed to suit.
 */
#ifndef	SPEED232
#define	SPEED232	B9600	/* 9600 baud */
#endif

/* What is the inherent delay for this mode of working, i.e. when is the
 * data time stamped.
 */
#define	SAFETY_SHIFT	10	/* Split the shift to avoid overflow */
#define	BITS_TO_L_FP(bits, baud) \
(((((bits)*2 +1) << (FRACTION_PREC-SAFETY_SHIFT)) / (2*baud)) << SAFETY_SHIFT)
#define	INH_DELAY_CBREAK	BITS_TO_L_FP(119, 9600)
#define	INH_DELAY_PPS		BITS_TO_L_FP(  0, 9600)

#ifndef	STREAM_PP1
#define	STREAM_PP1	"ppsclocd\0<-- patch space for module name1 -->"
#endif
#ifndef	STREAM_PP2
#define	STREAM_PP2	"ppsclock\0<-- patch space for module name2 -->"
#endif

     /* Offsets of the bytes of the serial line code.  The clock gives
 * local time with a GMT/BST indication. The EESM_ definitions
 * give offsets into ees->lastcode.
 */
#define EESM_CSEC	 0	/* centiseconds - always zero in our clock  */
#define EESM_SEC	 1	/* seconds in BCD			    */
#define EESM_MIN	 2	/* minutes in BCD			    */
#define EESM_HOUR	 3	/* hours in BCD				    */
#define EESM_DAYWK	 4	/* day of week (Sun = 0 etc)		    */
#define EESM_DAY	 5	/* day of month in BCD			    */
#define EESM_MON	 6	/* month in BCD				    */
#define EESM_YEAR	 7	/* year MOD 100 in BCD			    */
#define EESM_LEAP	 8	/* 0x0f if leap year, otherwise zero        */
#define EESM_BST	 9	/* 0x03 if BST, 0x00 if GMT		    */
#define EESM_MSFOK	10	/* 0x3f if radio good, otherwise zero	    */
				/* followed by a frame alignment byte (0xff) /
				/  which is not put into the lastcode buffer*/

/* Length of the serial time code, in characters.  The first length
 * is less the frame alignment byte.
 */
#define	LENEESPRT	(EESM_MSFOK+1)
#define	LENEESCODE	(LENEESPRT+1)

     /* Code state. */
#define	EESCS_WAIT	0       /* waiting for start of timecode */
#define	EESCS_GOTSOME	1	/* have an incomplete time code buffered */

     /* Default fudge factor and character to receive */
#define	DEFFUDGETIME	0	/* Default user supplied fudge factor */
#ifndef	DEFOSTIME
#define	DEFOSTIME	0	/* Default OS delay -- passed by Make ? */
#endif
#define	DEFINHTIME	INH_DELAY_PPS /* inherent delay due to sample point*/

     /* Limits on things.  Reduce the number of samples to SAMPLEREDUCE by median
 * elimination.  If we're running with an accurate clock, chose the BESTSAMPLE
 * as the estimated offset, otherwise average the remainder.
 */
#define	FULLSHIFT	6			/* NCODES root 2 */
#define NCODES		(1<< FULLSHIFT)		/* 64 */
#define	REDUCESHIFT	(FULLSHIFT -1)		/* SAMPLEREDUCE root 2 */

     /* Towards the high ( Why ?) end of half */
#define	BESTSAMPLE	((samplereduce * 3) /4)	/* 24 */

     /* Leap hold time.  After a leap second the clock will no longer be
 * reliable until it resynchronizes.  Hope 40 minutes is enough. */
#define	EESLEAPHOLD	(40 * 60)

#define	EES_STEP_F	(1 << 24) /* the receiver steps in units of about 4ms */
#define	EES_STEP_F_GRACE (EES_STEP_F/8) /*Allow for slop of 1/8 which is .5ms*/
#define	EES_STEP_NOTE	(1 << 21)/* Log any unexpected jumps, say .5 ms .... */
#define	EES_STEP_NOTES	50	/* Only do a limited number */
#define	MAX_STEP	16	/* Max number of steps to remember */

     /* debug is a bit mask of debugging that is wanted */
#define	DB_SYSLOG_SMPLI		0x0001
#define	DB_SYSLOG_SMPLE		0x0002
#define	DB_SYSLOG_SMTHI		0x0004
#define	DB_SYSLOG_NSMTHE	0x0008
#define	DB_SYSLOG_NSMTHI	0x0010
#define	DB_SYSLOG_SMTHE		0x0020
#define	DB_PRINT_EV		0x0040
#define	DB_PRINT_CDT		0x0080
#define	DB_PRINT_CDTC		0x0100
#define	DB_SYSLOG_KEEPD		0x0800
#define	DB_SYSLOG_KEEPE		0x1000
#define	DB_LOG_DELTAS		0x2000
#define	DB_PRINT_DELTAS		0x4000
#define	DB_LOG_AWAITMORE	0x8000
#define	DB_LOG_SAMPLES		0x10000
#define	DB_NO_PPS		0x20000
#define	DB_INC_PPS		0x40000
#define	DB_DUMP_DELTAS		0x80000

     struct eesunit {			/* EES unit control structure. */
	     struct peer *peer;		/* associated peer structure */
	     struct refclockio io;		/* given to the I/O handler */
	     l_fp	reftime;		/* reference time */
	     l_fp	lastsampletime;		/* time as in txt from last EES msg */
	     l_fp	arrvtime;		/* Time at which pkt arrived */
	     l_fp	codeoffsets[NCODES];	/* the time of arrival of 232 codes */
	     l_fp	offset;			/* chosen offset        (for clkbug) */
	     l_fp	lowoffset;		/* lowest sample offset (for clkbug) */
	     l_fp	highoffset;		/* highest   "     "    (for clkbug) */
	     char	lastcode[LENEESCODE+6];	/* last time code we received */
	     u_long	lasttime;		/* last time clock heard from */
	     u_long	clocklastgood;		/* last time good radio seen */
	     u_char	lencode;		/* length of code in buffer */
	     u_char	nsamples;		/* number of samples we've collected */
	     u_char	codestate;		/* state of 232 code reception */
	     u_char	unit;			/* unit number for this guy */
	     u_char	status;			/* clock status */
	     u_char	lastevent;		/* last clock event */
	     u_char	reason;			/* reason for last abort */
	     u_char	hour;			/* hour of day */
	     u_char	minute;			/* minute of hour */
	     u_char	second;			/* seconds of minute */
	     char	tz;			/* timezone from clock */
	     u_char	ttytype;		/* method used */
	     u_char	dump_vals;		/* Should clock values be dumped */
	     u_char	usealldata;		/* Use ALL samples */
	     u_short	day;			/* day of year from last code */
	     u_long	yearstart;		/* start of current year */
	     u_long	leaphold;		/* time of leap hold expiry */
	     u_long	badformat;		/* number of bad format codes */
	     u_long	baddata;		/* number of invalid time codes */
	     u_long	timestarted;		/* time we started this */
	     long	last_pps_no;		/* The serial # of the last PPS */
	     char	fix_pending;		/* Is a "sync to time" pending ? */
	     /* Fine tuning - compensate for 4 mS ramping .... */
	     l_fp	last_l;			/* last time stamp */
	     u_char	last_steps[MAX_STEP];	/* Most recent n steps */
	     int	best_av_step;		/* Best guess at average step */
	     char	best_av_step_count;	/* # of steps over used above */
	     char	this_step;		/* Current pos in buffer */
	     int	last_step_late;		/* How late the last step was (0-59) */
	     long	jump_fsecs;		/* # of fractions of a sec last jump */
	     u_long	last_step;		/* time of last step */
	     int	last_step_secs;		/* Number of seconds in last step */
	     int	using_ramp;		/* 1 -> noemal, -1 -> over stepped */
     };
#define	last_sec	last_l.l_ui
#define	last_sfsec	last_l.l_f
#define	this_uisec	((ees->arrvtime).l_ui)
#define	this_sfsec	((ees->arrvtime).l_f)
#define	msec(x)		((x) / (1<<22))
#define	LAST_STEPS	(sizeof ees->last_steps / sizeof ees->last_steps[0])
#define	subms(x)	((((((x < 0) ? (-(x)) : (x)) % (1<<22))/2) * 625) / (1<<(22 -5)))

/* Bitmask for what methods to try to use -- currently only PPS enabled */
#define	T_CBREAK	1
#define	T_PPS		8
/* macros to test above */
#define	is_cbreak(x)	((x)->ttytype & T_CBREAK)
#define	is_pps(x)	((x)->ttytype & T_PPS)
#define	is_any(x)	((x)->ttytype)

#define	CODEREASON	20	/* reason codes */

/* Data space for the unit structures.  Note that we allocate these on
 * the fly, but never give them back. */
static struct eesunit *eesunits[MAXUNITS];
static u_char unitinuse[MAXUNITS];

/* Keep the fudge factors separately so they can be set even
 * when no clock is configured. */
static l_fp inherent_delay[MAXUNITS];		/* when time stamp is taken */
static l_fp fudgefactor[MAXUNITS];		/* fudgetime1 */
static l_fp os_delay[MAXUNITS];			/* fudgetime2 */
static l_fp offset_fudge[MAXUNITS];		/* Sum of above */
static u_char stratumtouse[MAXUNITS];
static u_char sloppyclockflag[MAXUNITS];

static int deltas[60];

static l_fp acceptable_slop; /* = { 0, 1 << (FRACTION_PREC -2) }; */
static l_fp onesec; /* = { 1, 0 }; */

#ifndef	DUMP_BUF_SIZE	/* Size of buffer to be used by dump_buf */
#define	DUMP_BUF_SIZE	10112
#endif

/* ees_reset - reset the count back to zero */
#define	ees_reset(ees) (ees)->nsamples = 0; \
(ees)->codestate = EESCS_WAIT

/* ees_event - record and report an event */
#define	ees_event(ees, evcode) if ((ees)->status != (u_char)(evcode)) \
ees_report_event((ees), (evcode))

     /* Find the precision of the system clock by reading it */
#define	USECS	1000000
#define	MINSTEP	5	/* some systems increment uS on each call */
#define	MAXLOOPS (USECS/9)

/*
 * Function prototypes
 */

static	int	msfees_start	P((int unit, struct peer *peer));
static	void	msfees_shutdown	P((int unit, struct peer *peer));
static	void	msfees_poll	P((int unit, struct peer *peer));
static	void	msfees_init	P((void));
static	void	dump_buf	P((l_fp *coffs, int from, int to, char *text));
static	void	ees_report_event P((struct eesunit *ees, int code));
static	void	ees_receive	P((struct recvbuf *rbufp));
static	void	ees_process	P((struct eesunit *ees));
static	int	offcompare	P((const void *va, const void *vb));


/*
 * Transfer vector
 */
struct	refclock refclock_msfees = {
	msfees_start,		/* start up driver */
	msfees_shutdown,	/* shut down driver */
	msfees_poll,		/* transmit poll message */
	noentry,		/* not used */
	msfees_init,		/* initialize driver */
	noentry,		/* not used */
	NOFLAGS			/* not used */
};


static void
dump_buf(
	l_fp *coffs,
	int from,
	int to,
	char *text
	)
{
	char buff[DUMP_BUF_SIZE + 80];
	int i;
	register char *ptr = buff;

	snprintf(buff, sizeof(buff), text);
	for (i = from; i < to; i++) {
		ptr += strlen(ptr);
		if ((ptr - buff) > DUMP_BUF_SIZE) {
			msyslog(LOG_DEBUG, "D: %s", buff);
			ptr = buff;
		}
		snprintf(ptr, sizeof(buff) - (ptr - buff),
			 " %06d", ((int)coffs[i].l_f) / 4295);
	}
	msyslog(LOG_DEBUG, "D: %s", buff);
}

/* msfees_init - initialize internal ees driver data */
static void
msfees_init(void)
{
	register int i;
	/* Just zero the data arrays */
	memset((char *)eesunits, 0, sizeof eesunits);
	memset((char *)unitinuse, 0, sizeof unitinuse);

	acceptable_slop.l_ui = 0;
	acceptable_slop.l_uf = 1 << (FRACTION_PREC -2);

	onesec.l_ui = 1;
	onesec.l_uf = 0;

	/* Initialize fudge factors to default. */
	for (i = 0; i < MAXUNITS; i++) {
		fudgefactor[i].l_ui	= 0;
		fudgefactor[i].l_uf	= DEFFUDGETIME;
		os_delay[i].l_ui	= 0;
		os_delay[i].l_uf	= DEFOSTIME;
		inherent_delay[i].l_ui	= 0;
		inherent_delay[i].l_uf	= DEFINHTIME;
		offset_fudge[i]		= os_delay[i];
		L_ADD(&offset_fudge[i], &fudgefactor[i]);
		L_ADD(&offset_fudge[i], &inherent_delay[i]);
		stratumtouse[i]		= 0;
		sloppyclockflag[i]	= 0;
	}
}


/* msfees_start - open the EES devices and initialize data for processing */
static int
msfees_start(
	int unit,
	struct peer *peer
	)
{
	register struct eesunit *ees;
	register int i;
	int fd232 = -1;
	char eesdev[20];
	struct termios ttyb, *ttyp;
	struct refclockproc *pp;
	pp = peer->procptr;

	if (unit >= MAXUNITS) {
		msyslog(LOG_ERR, "ees clock: unit number %d invalid (max %d)",
			unit, MAXUNITS-1);
		return 0;
	}
	if (unitinuse[unit]) {
		msyslog(LOG_ERR, "ees clock: unit number %d in use", unit);
		return 0;
	}

	/* Unit okay, attempt to open the devices.  We do them both at
	 * once to make sure we can */
	snprintf(eesdev, sizeof(eesdev), EES232, unit);

	fd232 = open(eesdev, O_RDWR, 0777);
	if (fd232 == -1) {
		msyslog(LOG_ERR, "ees clock: open of %s failed: %m", eesdev);
		return 0;
	}

#ifdef	TIOCEXCL
	/* Set for exclusive use */
	if (ioctl(fd232, TIOCEXCL, (char *)0) < 0) {
		msyslog(LOG_ERR, "ees clock: ioctl(%s, TIOCEXCL): %m", eesdev);
		goto screwed;
	}
#endif

	/* STRIPPED DOWN VERSION: Only PPS CD is supported at the moment */

	/* Set port characteristics.  If we don't have a STREAMS module or
	 * a clock line discipline, cooked mode is just usable, even though it
	 * strips the top bit.  The only EES byte which uses the top
	 * bit is the year, and we don't use that anyway. If we do
	 * have the line discipline, we choose raw mode, and the
	 * line discipline code will block up the messages.
	 */

	/* STIPPED DOWN VERSION: Only PPS CD is supported at the moment */

	ttyp = &ttyb;
	if (tcgetattr(fd232, ttyp) < 0) {
		msyslog(LOG_ERR, "msfees_start: tcgetattr(%s): %m", eesdev);
		goto screwed;
	}

	ttyp->c_iflag = IGNBRK|IGNPAR|ICRNL;
	ttyp->c_cflag = SPEED232|CS8|CLOCAL|CREAD;
	ttyp->c_oflag = 0;
	ttyp->c_lflag = ICANON;
	ttyp->c_cc[VERASE] = ttyp->c_cc[VKILL] = '\0';
	if (tcsetattr(fd232, TCSANOW, ttyp) < 0) {
		msyslog(LOG_ERR, "msfees_start: tcsetattr(%s): %m", eesdev);
		goto screwed;
	}

	if (tcflush(fd232, TCIOFLUSH) < 0) {
		msyslog(LOG_ERR, "msfees_start: tcflush(%s): %m", eesdev);
		goto screwed;
	}

	inherent_delay[unit].l_uf = INH_DELAY_PPS;

	/* offset fudge (how *late* the timestamp is) = fudge + os delays */
	offset_fudge[unit] = os_delay[unit];
	L_ADD(&offset_fudge[unit], &fudgefactor[unit]);
	L_ADD(&offset_fudge[unit], &inherent_delay[unit]);

	/* Looks like this might succeed.  Find memory for the structure.
	 * Look to see if there are any unused ones, if not we malloc() one.
	 */
	if (eesunits[unit] != 0) /* The one we want is okay */
	    ees = eesunits[unit];
	else {
		/* Look for an unused, but allocated struct */
		for (i = 0; i < MAXUNITS; i++) {
			if (!unitinuse[i] && eesunits[i] != 0)
			    break;
		}

		if (i < MAXUNITS) {	/* Reclaim this one */
			ees = eesunits[i];
			eesunits[i] = 0;
		}			/* no spare -- make a new one */
		else ees = (struct eesunit *) emalloc(sizeof(struct eesunit));
	}
	memset((char *)ees, 0, sizeof(struct eesunit));
	eesunits[unit] = ees;

	/* Set up the structures */
	ees->peer	= peer;
	ees->unit	= (u_char)unit;
	ees->timestarted= current_time;
	ees->ttytype	= 0;
	ees->io.clock_recv= ees_receive;
	ees->io.srcclock= peer;
	ees->io.datalen	= 0;
	ees->io.fd	= fd232;

	/* Okay.  Push one of the two (linked into the kernel, or dynamically
	 * loaded) STREAMS module, and give it to the I/O code to start
	 * receiving stuff.
	 */

#ifdef STREAM
	{
		int rc1;
		/* Pop any existing onews first ... */
		while (ioctl(fd232, I_POP, 0 ) >= 0) ;

		/* Now try pushing either of the possible modules */
		if ((rc1=ioctl(fd232, I_PUSH, STREAM_PP1)) < 0 &&
		    ioctl(fd232, I_PUSH, STREAM_PP2) < 0) {
			msyslog(LOG_ERR,
				"ees clock: Push of `%s' and `%s' to %s failed %m",
				STREAM_PP1, STREAM_PP2, eesdev);
			goto screwed;
		}
		else {
			NLOG(NLOG_CLOCKINFO) /* conditional if clause for conditional syslog */
				msyslog(LOG_INFO, "I: ees clock: PUSHed %s on %s",
					(rc1 >= 0) ? STREAM_PP1 : STREAM_PP2, eesdev);
			ees->ttytype |= T_PPS;
		}
	}
#endif /* STREAM */

	/* Add the clock */
	if (!io_addclock(&ees->io)) {
		/* Oh shit.  Just close and return. */
		msyslog(LOG_ERR, "ees clock: io_addclock(%s): %m", eesdev);
		goto screwed;
	}


	/* All done.  Initialize a few random peer variables, then
	 * return success. */
	peer->precision	= sys_precision;
	peer->stratum	= stratumtouse[unit];
	if (stratumtouse[unit] <= 1) {
		memcpy((char *)&pp->refid, EESREFID, 4);
		if (unit > 0 && unit < 10)
		    ((char *)&pp->refid)[3] = '0' + unit;
	} else {
		peer->refid = htonl(EESHSREFID);
	}
	unitinuse[unit] = 1;
	pp->unitptr = &eesunits[unit];
	pp->clockdesc = EESDESCRIPTION;
	msyslog(LOG_ERR, "ees clock: %s OK on %d", eesdev, unit);
	return (1);

    screwed:
	if (fd232 != -1)
	    (void) close(fd232);
	return (0);
}


/* msfees_shutdown - shut down a EES clock */
static void
msfees_shutdown(
	int unit,
	struct peer *peer
	)
{
	register struct eesunit *ees;

	if (unit >= MAXUNITS) {
		msyslog(LOG_ERR,
			"ees clock: INTERNAL ERROR, unit number %d invalid (max %d)",
			unit, MAXUNITS);
		return;
	}
	if (!unitinuse[unit]) {
		msyslog(LOG_ERR,
			"ees clock: INTERNAL ERROR, unit number %d not in use", unit);
		return;
	}

	/* Tell the I/O module to turn us off.  We're history. */
	ees = eesunits[unit];
	io_closeclock(&ees->io);
	unitinuse[unit] = 0;
}


/* ees_report_event - note the occurance of an event */
static void
ees_report_event(
	struct eesunit *ees,
	int code
	)
{
	if (ees->status != (u_char)code) {
		ees->status = (u_char)code;
		if (code != CEVNT_NOMINAL)
		    ees->lastevent = (u_char)code;
		/* Should report event to trap handler in here.
		 * Soon...
		 */
	}
}


/* ees_receive - receive data from the serial interface on an EES clock */
static void
ees_receive(
	struct recvbuf *rbufp
	)
{
	register int n_sample;
	register int day;
	register struct eesunit *ees;
	register u_char *dpt;		/* Data PoinTeR: move along ... */
	register u_char *dpend;		/* Points just *after* last data char */
	register char *cp;
	l_fp tmp;
	int call_pps_sample = 0;
	l_fp pps_arrvstamp;
	int	sincelast;
	int	pps_step = 0;
	int	suspect_4ms_step = 0;
	struct ppsclockev ppsclockev;
	long *ptr = (long *) &ppsclockev;
	int rc;
	int request;
#ifdef HAVE_CIOGETEV
	request = CIOGETEV;
#endif
#ifdef HAVE_TIOCGPPSEV
	request = TIOCGPPSEV;
#endif

	/* Get the clock this applies to and a pointer to the data */
	ees = (struct eesunit *)rbufp->recv_peer->procptr->unitptr;
	dpt = (u_char *)&rbufp->recv_space;
	dpend = dpt + rbufp->recv_length;
	if ((dbg & DB_LOG_AWAITMORE) && (rbufp->recv_length != LENEESCODE))
	    printf("[%d] ", rbufp->recv_length);

	/* Check out our state and process appropriately */
	switch (ees->codestate) {
	    case EESCS_WAIT:
		/* Set an initial guess at the timestamp as the recv time.
		 * If just running in CBREAK mode, we can't improve this.
		 * If we have the CLOCK Line Discipline, PPSCD, or sime such,
		 * then we will do better later ....
		 */
		ees->arrvtime = rbufp->recv_time;
		ees->codestate = EESCS_GOTSOME;
		ees->lencode = 0;
		/*FALLSTHROUGH*/

	    case EESCS_GOTSOME:
		cp = &(ees->lastcode[ees->lencode]);

		/* Gobble the bytes until the final (possibly stripped) 0xff */
		while (dpt < dpend && (*dpt & 0x7f) != 0x7f) {
			*cp++ = (char)*dpt++;
			ees->lencode++;
			/* Oh dear -- too many bytes .. */
			if (ees->lencode > LENEESPRT) {
				NLOG(NLOG_CLOCKINFO) /* conditional if clause for conditional syslog */
					msyslog(LOG_INFO,
						"I: ees clock: %d + %d > %d [%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x]",
						ees->lencode, dpend - dpt, LENEESPRT,
#define D(x) (ees->lastcode[x])
						D(0), D(1), D(2), D(3), D(4), D(5), D(6),
						D(7), D(8), D(9), D(10), D(11), D(12));
#undef	D
				ees->badformat++;
				ees->reason = CODEREASON + 1;
				ees_event(ees, CEVNT_BADREPLY);
				ees_reset(ees);
				return;
			}
		}
		/* Gave up because it was end of the buffer, rather than ff */
		if (dpt == dpend) {
			/* Incomplete.  Wait for more. */
			if (dbg & DB_LOG_AWAITMORE)
			    msyslog(LOG_INFO,
				    "I: ees clock %d: %p == %p: await more",
				    ees->unit, dpt, dpend);
			return;
		}

		/* This shouldn't happen ... ! */
		if ((*dpt & 0x7f) != 0x7f) {
			msyslog(LOG_INFO, "I: ees clock: %0x & 0x7f != 0x7f", *dpt);
			ees->badformat++;
			ees->reason = CODEREASON + 2;
			ees_event(ees, CEVNT_BADREPLY);
			ees_reset(ees);
			return;
		}

		/* Skip the 0xff */
		dpt++;

		/* Finally, got a complete buffer.  Mainline code will
		 * continue on. */
		cp = ees->lastcode;
		break;

	    default:
		msyslog(LOG_ERR, "ees clock: INTERNAL ERROR: %d state %d",
			ees->unit, ees->codestate);
		ees->reason = CODEREASON + 5;
		ees_event(ees, CEVNT_FAULT);
		ees_reset(ees);
		return;
	}

	/* Boy!  After all that crap, the lastcode buffer now contains
	 * something we hope will be a valid time code.  Do length
	 * checks and sanity checks on constant data.
	 */
	ees->codestate = EESCS_WAIT;
	ees->lasttime = current_time;
	if (ees->lencode != LENEESPRT) {
		ees->badformat++;
		ees->reason = CODEREASON + 6;
		ees_event(ees, CEVNT_BADREPLY);
		ees_reset(ees);
		return;
	}

	cp = ees->lastcode;

	/* Check that centisecond is zero */
	if (cp[EESM_CSEC] != 0) {
		ees->baddata++;
		ees->reason = CODEREASON + 7;
		ees_event(ees, CEVNT_BADREPLY);
		ees_reset(ees);
		return;
	}

	/* Check flag formats */
	if (cp[EESM_LEAP] != 0 && cp[EESM_LEAP] != 0x0f) {
		ees->badformat++;
		ees->reason = CODEREASON + 8;
		ees_event(ees, CEVNT_BADREPLY);
		ees_reset(ees);
		return;
	}

	if (cp[EESM_BST] != 0 && cp[EESM_BST] != 0x03) {
		ees->badformat++;
		ees->reason = CODEREASON + 9;
		ees_event(ees, CEVNT_BADREPLY);
		ees_reset(ees);
		return;
	}

	if (cp[EESM_MSFOK] != 0 && cp[EESM_MSFOK] != 0x3f) {
		ees->badformat++;
		ees->reason = CODEREASON + 10;
		ees_event(ees, CEVNT_BADREPLY);
		ees_reset(ees);
		return;
	}

	/* So far, so good.  Compute day, hours, minutes, seconds,
	 * time zone.  Do range checks on these.
	 */

#define bcdunpack(val)	( (((val)>>4) & 0x0f) * 10 + ((val) & 0x0f) )
#define istrue(x)	((x)?1:0)

	ees->second  = bcdunpack(cp[EESM_SEC]);  /* second       */
	ees->minute  = bcdunpack(cp[EESM_MIN]);  /* minute       */
	ees->hour    = bcdunpack(cp[EESM_HOUR]); /* hour         */

	day          = bcdunpack(cp[EESM_DAY]);  /* day of month */

	switch (bcdunpack(cp[EESM_MON])) {       /* month        */

		/*  Add in lengths of all previous months.  Add one more
		    if it is a leap year and after February.
		*/
	    case 12:	day += NOV;			  /*FALLSTHROUGH*/
	    case 11:	day += OCT;			  /*FALLSTHROUGH*/
	    case 10:	day += SEP;			  /*FALLSTHROUGH*/
	    case  9:	day += AUG;			  /*FALLSTHROUGH*/
	    case  8:	day += JUL;			  /*FALLSTHROUGH*/
	    case  7:	day += JUN;			  /*FALLSTHROUGH*/
	    case  6:	day += MAY;			  /*FALLSTHROUGH*/
	    case  5:	day += APR;			  /*FALLSTHROUGH*/
	    case  4:	day += MAR;			  /*FALLSTHROUGH*/
	    case  3:	day += FEB;
		if (istrue(cp[EESM_LEAP])) day++; /*FALLSTHROUGH*/
	    case  2:	day += JAN;			  /*FALLSTHROUGH*/
	    case  1:	break;
	    default:	ees->baddata++;
		ees->reason = CODEREASON + 11;
		ees_event(ees, CEVNT_BADDATE);
		ees_reset(ees);
		return;
	}

	ees->day     = day;

	/* Get timezone. The clocktime routine wants the number
	 * of hours to add to the delivered time to get UT.
	 * Currently -1 if BST flag set, 0 otherwise.  This
	 * is the place to tweak things if double summer time
	 * ever happens.
	 */
	ees->tz      = istrue(cp[EESM_BST]) ? -1 : 0;

	if (ees->day > 366 || ees->day < 1 ||
	    ees->hour > 23 || ees->minute > 59 || ees->second > 59) {
		ees->baddata++;
		ees->reason = CODEREASON + 12;
		ees_event(ees, CEVNT_BADDATE);
		ees_reset(ees);
		return;
	}

	n_sample = ees->nsamples;

	/* Now, compute the reference time value: text -> tmp.l_ui */
	if (!clocktime(ees->day, ees->hour, ees->minute, ees->second,
		       ees->tz, rbufp->recv_time.l_ui, &ees->yearstart,
		       &tmp.l_ui)) {
		ees->baddata++;
		ees->reason = CODEREASON + 13;
		ees_event(ees, CEVNT_BADDATE);
		ees_reset(ees);
		return;
	}
	tmp.l_uf = 0;

	/*  DON'T use ees->arrvtime -- it may be < reftime */
	ees->lastsampletime = tmp;

	/* If we are synchronised to the radio, update the reference time.
	 * Also keep a note of when clock was last good.
	 */
	if (istrue(cp[EESM_MSFOK])) {
		ees->reftime = tmp;
		ees->clocklastgood = current_time;
	}


	/* Compute the offset.  For the fractional part of the
	 * offset we use the expected delay for the message.
	 */
	ees->codeoffsets[n_sample].l_ui = tmp.l_ui;
	ees->codeoffsets[n_sample].l_uf = 0;

	/* Number of seconds since the last step */
	sincelast = this_uisec - ees->last_step;

	memset((char *) &ppsclockev, 0, sizeof ppsclockev);

	rc = ioctl(ees->io.fd, request, (char *) &ppsclockev);
	if (dbg & DB_PRINT_EV) fprintf(stderr,
					 "[%x] CIOGETEV u%d %d (%x %d) gave %d (%d): %08lx %08lx %ld\n",
					 DB_PRINT_EV, ees->unit, ees->io.fd, request, is_pps(ees),
					 rc, errno, ptr[0], ptr[1], ptr[2]);

	/* If we managed to get the time of arrival, process the info */
	if (rc >= 0) {
		int conv = -1;
		pps_step = ppsclockev.serial - ees->last_pps_no;

		/* Possible that PPS triggered, but text message didn't */
		if (pps_step == 2) msyslog(LOG_ERR, "pps step = 2 @ %02d", ees->second);
		if (pps_step == 2 && ees->second == 1) suspect_4ms_step |= 1;
		if (pps_step == 2 && ees->second == 2) suspect_4ms_step |= 4;

		/* allow for single loss of PPS only */
		if (pps_step != 1 && pps_step != 2)
		    fprintf(stderr, "PPS step: %d too far off %ld (%d)\n",
			    ppsclockev.serial, ees->last_pps_no, pps_step);
		else {
			pps_arrvstamp = tval_stamp_to_lfp(ppsclockev.tv);
			/* if ((ABS(time difference) - 0.25) < 0)
			 * then believe it ...
			 */
			l_fp diff;
			diff = pps_arrvstamp;
			conv = 0;
			L_SUB(&diff, &ees->arrvtime);
			if (dbg & DB_PRINT_CDT)
			    printf("[%x] Have %lx.%08lx and %lx.%08lx -> %lx.%08lx @ %s",
				   DB_PRINT_CDT, (long)ees->arrvtime.l_ui, (long)ees->arrvtime.l_uf,
				   (long)pps_arrvstamp.l_ui, (long)pps_arrvstamp.l_uf,
				   (long)diff.l_ui, (long)diff.l_uf,
				   ctime(&(ppsclockev.tv.tv_sec)));
			if (L_ISNEG(&diff)) M_NEG(diff.l_ui, diff.l_uf);
			L_SUB(&diff, &acceptable_slop);
			if (L_ISNEG(&diff)) {	/* AOK -- pps_sample */
				ees->arrvtime = pps_arrvstamp;
				conv++;
				call_pps_sample++;
			}
			/* Some loss of some signals around sec = 1 */
			else if (ees->second == 1) {
				diff = pps_arrvstamp;
				L_ADD(&diff, &onesec);
				L_SUB(&diff, &ees->arrvtime);
				if (L_ISNEG(&diff)) M_NEG(diff.l_ui, diff.l_uf);
				L_SUB(&diff, &acceptable_slop);
				msyslog(LOG_ERR, "Have sec==1 slip %ds a=%08x-p=%08x -> %x.%08x (u=%d) %s",
					pps_arrvstamp.l_ui - ees->arrvtime.l_ui,
					pps_arrvstamp.l_uf,
					ees->arrvtime.l_uf,
					diff.l_ui, diff.l_uf,
					(int)ppsclockev.tv.tv_usec,
					ctime(&(ppsclockev.tv.tv_sec)));
				if (L_ISNEG(&diff)) {	/* AOK -- pps_sample */
					suspect_4ms_step |= 2;
					ees->arrvtime = pps_arrvstamp;
					L_ADD(&ees->arrvtime, &onesec);
					conv++;
					call_pps_sample++;
				}
			}
		}
		ees->last_pps_no = ppsclockev.serial;
		if (dbg & DB_PRINT_CDTC)
		    printf(
			    "[%x] %08lx %08lx %d u%d (%d %d)\n",
			    DB_PRINT_CDTC, (long)pps_arrvstamp.l_ui,
			    (long)pps_arrvstamp.l_uf, conv, ees->unit,
			    call_pps_sample, pps_step);
	}

	/* See if there has been a 4ms jump at a minute boundry */
	{	l_fp	delta;
#define	delta_isec	delta.l_ui
#define	delta_ssec	delta.l_i
#define	delta_sfsec	delta.l_f
	long	delta_f_abs;

	delta.l_i = ees->arrvtime.l_i;
	delta.l_f = ees->arrvtime.l_f;

	L_SUB(&delta, &ees->last_l);
	delta_f_abs = delta_sfsec;
	if (delta_f_abs < 0) delta_f_abs = -delta_f_abs;

	/* Dump the deltas each minute */
	if (dbg & DB_DUMP_DELTAS)
	{	
		if (/*0 <= ees->second && */
		    ees->second < COUNTOF(deltas))
			deltas[ees->second] = delta_sfsec;
	/* Dump on second 1, as second 0 sometimes missed */
	if (ees->second == 1) {
		char text[16 * COUNTOF(deltas)];
		char *cptr=text;
		int i;
		for (i = 0; i < COUNTOF(deltas); i++) {
			snprintf(cptr, sizeof(text) / COUNTOF(deltas),
				" %d.%04d", msec(deltas[i]),
				subms(deltas[i]));
			cptr += strlen(cptr);
		}
		msyslog(LOG_ERR, "Deltas: %d.%04d<->%d.%04d: %s",
			msec(EES_STEP_F - EES_STEP_F_GRACE), subms(EES_STEP_F - EES_STEP_F_GRACE),
			msec(EES_STEP_F + EES_STEP_F_GRACE), subms(EES_STEP_F + EES_STEP_F_GRACE),
			text+1);
		for (i=0; i<((sizeof deltas) / (sizeof deltas[0])); i++) deltas[i] = 0;
	}
	}

	/* Lets see if we have a 4 mS step at a minute boundaary */
	if (	((EES_STEP_F - EES_STEP_F_GRACE) < delta_f_abs) &&
		(delta_f_abs < (EES_STEP_F + EES_STEP_F_GRACE)) &&
		(ees->second == 0 || ees->second == 1 || ees->second == 2) &&
		(sincelast < 0 || sincelast > 122)
		) {	/* 4ms jump at min boundry */
		int old_sincelast;
		int count=0;
		int sum = 0;
		/* Yes -- so compute the ramp time */
		if (ees->last_step == 0) sincelast = 0;
		old_sincelast = sincelast;

		/* First time in, just set "ees->last_step" */
		if(ees->last_step) {
			int other_step = 0;
			int third_step = 0;
			int this_step = (sincelast + (60 /2)) / 60;
			int p_step = ees->this_step;
			int p;
			ees->last_steps[p_step] = this_step;
			p= p_step;
			p_step++;
			if (p_step >= LAST_STEPS) p_step = 0;
			ees->this_step = p_step;
				/* Find the "average" interval */
			while (p != p_step) {
				int this = ees->last_steps[p];
				if (this == 0) break;
				if (this != this_step) {
					if (other_step == 0 && (
						this== (this_step +2) ||
						this== (this_step -2) ||
						this== (this_step +1) ||
						this== (this_step -1)))
					    other_step = this;
					if (other_step != this) {
						int idelta = (this_step - other_step);
						if (idelta < 0) idelta = - idelta;
						if (third_step == 0 && (
							(idelta == 1) ? (
								this == (other_step +1) ||
								this == (other_step -1) ||
								this == (this_step +1) ||
								this == (this_step -1))
							:
							(
								this == (this_step + other_step)/2
								)
							)) third_step = this;
						if (third_step != this) break;
					}
				}
				sum += this;
				p--;
				if (p < 0) p += LAST_STEPS;
				count++;
			}
			msyslog(LOG_ERR, "MSF%d: %d: This=%d (%d), other=%d/%d, sum=%d, count=%d, pps_step=%d, suspect=%x", ees->unit, p, ees->last_steps[p], this_step, other_step, third_step, sum, count, pps_step, suspect_4ms_step);
			if (count != 0) sum = ((sum * 60) + (count /2)) / count;
#define	SV(x) (ees->last_steps[(x + p_step) % LAST_STEPS])
			msyslog(LOG_ERR, "MSF%d: %x steps %d: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
				ees->unit, suspect_4ms_step, p_step, SV(0), SV(1), SV(2), SV(3), SV(4), SV(5), SV(6),
				SV(7), SV(8), SV(9), SV(10), SV(11), SV(12), SV(13), SV(14), SV(15));
			printf("MSF%d: steps %d: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
			       ees->unit, p_step, SV(0), SV(1), SV(2), SV(3), SV(4), SV(5), SV(6),
			       SV(7), SV(8), SV(9), SV(10), SV(11), SV(12), SV(13), SV(14), SV(15));
#undef SV
			ees->jump_fsecs = delta_sfsec;
			ees->using_ramp = 1;
			if (sincelast > 170)
			    ees->last_step_late += sincelast - ((sum) ? sum : ees->last_step_secs);
			else ees->last_step_late = 30;
			if (ees->last_step_late < -60 || ees->last_step_late > 120) ees->last_step_late = 30;
			if (ees->last_step_late < 0) ees->last_step_late = 0;
			if (ees->last_step_late >= 60) ees->last_step_late = 59;
			sincelast = 0;
		}
		else {	/* First time in -- just save info */
			ees->last_step_late = 30;
			ees->jump_fsecs = delta_sfsec;
			ees->using_ramp = 1;
			sum = 4 * 60;
		}
		ees->last_step = this_uisec;
		printf("MSF%d: d=%3ld.%04ld@%d :%d:%d:$%d:%d:%d\n",
		       ees->unit, (long)msec(delta_sfsec), (long)subms(delta_sfsec),
		       ees->second, old_sincelast, ees->last_step_late, count, sum,
		       ees->last_step_secs);
		msyslog(LOG_ERR, "MSF%d: d=%3d.%04d@%d :%d:%d:%d:%d:%d",
			ees->unit, msec(delta_sfsec), subms(delta_sfsec), ees->second,
			old_sincelast, ees->last_step_late, count, sum, ees->last_step_secs);
		if (sum) ees->last_step_secs = sum;
	}
	/* OK, so not a 4ms step at a minute boundry */
	else {
		if (suspect_4ms_step) msyslog(LOG_ERR,
					      "MSF%d: suspect = %x, but delta of %d.%04d [%d.%04d<%d.%04d<%d.%04d: %d %d]",
					      ees->unit, suspect_4ms_step, msec(delta_sfsec), subms(delta_sfsec),
					      msec(EES_STEP_F - EES_STEP_F_GRACE),
					      subms(EES_STEP_F - EES_STEP_F_GRACE),
					      (int)msec(delta_f_abs),
					      (int)subms(delta_f_abs),
					      msec(EES_STEP_F + EES_STEP_F_GRACE),
					      subms(EES_STEP_F + EES_STEP_F_GRACE),
					      ees->second,
					      sincelast);
		if ((delta_f_abs > EES_STEP_NOTE) && ees->last_l.l_i) {
			static int ees_step_notes = EES_STEP_NOTES;
			if (ees_step_notes > 0) {
				ees_step_notes--;
				printf("MSF%d: D=%3ld.%04ld@%02d :%d%s\n",
				       ees->unit, (long)msec(delta_sfsec), (long)subms(delta_sfsec),
				       ees->second, sincelast, ees_step_notes ? "" : " -- NO MORE !");
				msyslog(LOG_ERR, "MSF%d: D=%3d.%04d@%02d :%d%s",
					ees->unit, msec(delta_sfsec), subms(delta_sfsec), ees->second, (ees->last_step) ? sincelast : -1, ees_step_notes ? "" : " -- NO MORE !");
			}
		}
	}
	}
	ees->last_l = ees->arrvtime;

	/* IF we have found that it's ramping
	 * && it's within twice the expected ramp period
	 * && there is a non zero step size (avoid /0 !)
	 * THEN we twiddle things
	 */
	if (ees->using_ramp &&
	    sincelast < (ees->last_step_secs)*2 &&
	    ees->last_step_secs)
	{	long	sec_of_ramp = sincelast + ees->last_step_late;
	long	fsecs;
	l_fp	inc;

	/* Ramp time may vary, so may ramp for longer than last time */
	if (sec_of_ramp > (ees->last_step_secs + 120))
	    sec_of_ramp =  ees->last_step_secs;

	/* sec_of_ramp * ees->jump_fsecs may overflow 2**32 */
	fsecs = sec_of_ramp * (ees->jump_fsecs /  ees->last_step_secs);

	if (dbg & DB_LOG_DELTAS) msyslog(LOG_ERR,
					   "[%x] MSF%d: %3ld/%03d -> d=%11ld (%d|%ld)",
					   DB_LOG_DELTAS,
					   ees->unit, sec_of_ramp, ees->last_step_secs, fsecs,
					   pps_arrvstamp.l_f, pps_arrvstamp.l_f + fsecs);
	if (dbg & DB_PRINT_DELTAS) printf(
		"MSF%d: %3ld/%03d -> d=%11ld (%ld|%ld)\n",
		ees->unit, sec_of_ramp, ees->last_step_secs, fsecs,
		(long)pps_arrvstamp.l_f, pps_arrvstamp.l_f + fsecs);

	/* Must sign extend the result */
	inc.l_i = (fsecs < 0) ? -1 : 0;
	inc.l_f = fsecs;
	if (dbg & DB_INC_PPS)
	{	L_SUB(&pps_arrvstamp, &inc);
	L_SUB(&ees->arrvtime, &inc);
	}
	else
	{	L_ADD(&pps_arrvstamp, &inc);
	L_ADD(&ees->arrvtime, &inc);
	}
	}
	else {
		if (dbg & DB_LOG_DELTAS) msyslog(LOG_ERR,
						   "[%x] MSF%d: ees->using_ramp=%d, sincelast=%x / %x, ees->last_step_secs=%x",
						   DB_LOG_DELTAS,
						   ees->unit, ees->using_ramp,
						   sincelast,
						   (ees->last_step_secs)*2,
						   ees->last_step_secs);
		if (dbg & DB_PRINT_DELTAS) printf(
			"[%x] MSF%d: ees->using_ramp=%d, sincelast=%x / %x, ees->last_step_secs=%x\n",
			DB_LOG_DELTAS,
			ees->unit, ees->using_ramp,
			sincelast,
			(ees->last_step_secs)*2,
			ees->last_step_secs);
	}

	L_SUB(&ees->arrvtime, &offset_fudge[ees->unit]);
	L_SUB(&pps_arrvstamp, &offset_fudge[ees->unit]);

	if (call_pps_sample && !(dbg & DB_NO_PPS)) {
		/* Sigh -- it expects its args negated */
		L_NEG(&pps_arrvstamp);
		/*
		 * I had to disable this here, since it appears there is no pointer to the
		 * peer structure.
		 *
		 (void) pps_sample(peer, &pps_arrvstamp);
		*/
	}

	/* Subtract off the local clock time stamp */
	L_SUB(&ees->codeoffsets[n_sample], &ees->arrvtime);
	if (dbg & DB_LOG_SAMPLES) msyslog(LOG_ERR,
					    "MSF%d: [%x] %d (ees: %d %d) (pps: %d %d)%s",
					    ees->unit, DB_LOG_DELTAS, n_sample,
					    ees->codeoffsets[n_sample].l_f,
					    ees->codeoffsets[n_sample].l_f / 4295,
					    pps_arrvstamp.l_f,
					    pps_arrvstamp.l_f /4295,
					    (dbg & DB_NO_PPS) ? " [no PPS]" : "");

	if (ees->nsamples++ == NCODES-1) ees_process(ees);

	/* Done! */
}


/* offcompare - auxiliary comparison routine for offset sort */

static int
offcompare(
	const void *va,
	const void *vb
	)
{
	const l_fp *a = (const l_fp *)va;
	const l_fp *b = (const l_fp *)vb;
	return(L_ISGEQ(a, b) ? (L_ISEQU(a, b) ? 0 : 1) : -1);
}


/* ees_process - process a pile of samples from the clock */
static void
ees_process(
	struct eesunit *ees
	)
{
	static int last_samples = -1;
	register int i, j;
	register int noff;
	register l_fp *coffs = ees->codeoffsets;
	l_fp offset, tmp;
	double dispersion;	/* ++++ */
	int lostsync, isinsync;
	int samples = ees->nsamples;
	int samplelog = 0;	/* keep "gcc -Wall" happy ! */
	int samplereduce = (samples + 1) / 2;
	double doffset;

	/* Reset things to zero so we don't have to worry later */
	ees_reset(ees);

	if (sloppyclockflag[ees->unit]) {
		samplelog = (samples <  2) ? 0 :
			(samples <  5) ? 1 :
			(samples <  9) ? 2 :
			(samples < 17) ? 3 :
			(samples < 33) ? 4 : 5;
		samplereduce = (1 << samplelog);
	}

	if (samples != last_samples &&
	    ((samples != (last_samples-1)) || samples < 3)) {
		msyslog(LOG_ERR, "Samples=%d (%d), samplereduce=%d ....",
			samples, last_samples, samplereduce);
		last_samples = samples;
	}
	if (samples < 1) return;

	/* If requested, dump the raw data we have in the buffer */
	if (ees->dump_vals)
		dump_buf(coffs, 0, samples, "Raw  data  is:");

	/* Sort the offsets, trim off the extremes, then choose one. */
	qsort(coffs, (size_t)samples, sizeof(coffs[0]), offcompare);

	noff = samples;
	i = 0;
	while ((noff - i) > samplereduce) {
		/* Trim off the sample which is further away
		 * from the median.  We work this out by doubling
		 * the median, subtracting off the end samples, and
		 * looking at the sign of the answer, using the
		 * identity (c-b)-(b-a) == 2*b-a-c
		 */
		tmp = coffs[(noff + i)/2];
		L_ADD(&tmp, &tmp);
		L_SUB(&tmp, &coffs[i]);
		L_SUB(&tmp, &coffs[noff-1]);
		if (L_ISNEG(&tmp)) noff--; else i++;
	}

	/* If requested, dump the reduce data we have in the buffer */
	if (ees->dump_vals) dump_buf(coffs, i, noff, "Reduced    to:");

	/* What we do next depends on the setting of the sloppy clock flag.
	 * If it is on, average the remainder to derive our estimate.
	 * Otherwise, just pick a representative value from the remaining stuff
	 */
	if (sloppyclockflag[ees->unit]) {
		offset.l_ui = offset.l_uf = 0;
		for (j = i; j < noff; j++)
		    L_ADD(&offset, &coffs[j]);
		for (j = samplelog; j > 0; j--)
		    L_RSHIFTU(&offset);
	}
	else offset = coffs[i+BESTSAMPLE];

	/* Compute the dispersion as the difference between the
	 * lowest and highest offsets that remain in the
	 * consideration list.
	 *
	 * It looks like MOST clocks have MOD (max error), so halve it !
	 */
	tmp = coffs[noff-1];
	L_SUB(&tmp, &coffs[i]);
#define	FRACT_SEC(n) ((1 << 30) / (n/2))
	dispersion = LFPTOFP(&tmp) / 2; /* ++++ */
	if (dbg & (DB_SYSLOG_SMPLI | DB_SYSLOG_SMPLE)) msyslog(
		(dbg & DB_SYSLOG_SMPLE) ? LOG_ERR : LOG_INFO,
		"I: [%x] Offset=%06d (%d), disp=%f%s [%d], %d %d=%d %d:%d %d=%d %d",
		dbg & (DB_SYSLOG_SMPLI | DB_SYSLOG_SMPLE),
		offset.l_f / 4295, offset.l_f,
		(dispersion * 1526) / 100,
		(sloppyclockflag[ees->unit]) ? " by averaging" : "",
		FRACT_SEC(10) / 4295,
		(coffs[0].l_f) / 4295,
		i,
		(coffs[i].l_f) / 4295,
		(coffs[samples/2].l_f) / 4295,
		(coffs[i+BESTSAMPLE].l_f) / 4295,
		noff-1,
		(coffs[noff-1].l_f) / 4295,
		(coffs[samples-1].l_f) / 4295);

	/* Are we playing silly wotsits ?
	 * If we are using all data, see if there is a "small" delta,
	 * and if so, blurr this with 3/4 of the delta from the last value
	 */
	if (ees->usealldata && ees->offset.l_uf) {
		long diff = (long) (ees->offset.l_uf - offset.l_uf);

		/* is the delta small enough ? */
		if ((- FRACT_SEC(100)) < diff && diff < FRACT_SEC(100)) {
			int samd = (64 * 4) / samples;
			long new;
			if (samd < 2) samd = 2;
			new = offset.l_uf + ((diff * (samd -1)) / samd);

			/* Sign change -> need to fix up int part */
			if ((new & 0x80000000) !=
			    (((long) offset.l_uf) & 0x80000000))
			{	NLOG(NLOG_CLOCKINFO) /* conditional if clause for conditional syslog */
					msyslog(LOG_INFO, "I: %lx != %lx (%lx %lx), so add %d",
						new & 0x80000000,
						((long) offset.l_uf) & 0x80000000,
						new, (long) offset.l_uf,
						(new < 0) ? -1 : 1);
				offset.l_ui += (new < 0) ? -1 : 1;
			}
			dispersion /= 4;
			if (dbg & (DB_SYSLOG_SMTHI | DB_SYSLOG_SMTHE)) msyslog(
				(dbg & DB_SYSLOG_SMTHE) ? LOG_ERR : LOG_INFO,
				"I: [%x] Smooth data: %ld -> %ld, dispersion now %f",
				dbg & (DB_SYSLOG_SMTHI | DB_SYSLOG_SMTHE),
				((long) offset.l_uf) / 4295, new / 4295,
				(dispersion * 1526) / 100);
			offset.l_uf = (unsigned long) new;
		}
		else if (dbg & (DB_SYSLOG_NSMTHI | DB_SYSLOG_NSMTHE)) msyslog(
			(dbg & DB_SYSLOG_NSMTHE) ? LOG_ERR : LOG_INFO,
			"[%x] No smooth as delta not %d < %ld < %d",
			dbg & (DB_SYSLOG_NSMTHI | DB_SYSLOG_NSMTHE),
			- FRACT_SEC(100), diff, FRACT_SEC(100));
	}
	else if (dbg & (DB_SYSLOG_NSMTHI | DB_SYSLOG_NSMTHE)) msyslog(
		(dbg & DB_SYSLOG_NSMTHE) ? LOG_ERR : LOG_INFO,
		"I: [%x] No smooth as flag=%x and old=%x=%d (%d:%d)",
		dbg & (DB_SYSLOG_NSMTHI | DB_SYSLOG_NSMTHE),
		ees->usealldata, ees->offset.l_f, ees->offset.l_uf,
		offset.l_f, ees->offset.l_f - offset.l_f);

	/* Collect offset info for debugging info */
	ees->offset = offset;
	ees->lowoffset = coffs[i];
	ees->highoffset = coffs[noff-1];

	/* Determine synchronization status.  Can be unsync'd either
	 * by a report from the clock or by a leap hold.
	 *
	 * Loss of the radio signal for a short time does not cause
	 * us to go unsynchronised, since the receiver keeps quite
	 * good time on its own.  The spec says 20ms in 4 hours; the
	 * observed drift in our clock (Cambridge) is about a second
	 * a day, but even that keeps us within the inherent tolerance
	 * of the clock for about 15 minutes. Observation shows that
	 * the typical "short" outage is 3 minutes, so to allow us
	 * to ride out those, we will give it 5 minutes.
	 */
	lostsync = current_time - ees->clocklastgood > 300 ? 1 : 0;
	isinsync = (lostsync || ees->leaphold > current_time) ? 0 : 1;

	/* Done.  Use time of last good, synchronised code as the
	 * reference time, and lastsampletime as the receive time.
	 */
	if (ees->fix_pending) {
		msyslog(LOG_ERR, "MSF%d: fix_pending=%d -> jump %x.%08x",
			ees->fix_pending, ees->unit, offset.l_i, offset.l_f);
		ees->fix_pending = 0;
	}
	LFPTOD(&offset, doffset);
	refclock_receive(ees->peer);
	ees_event(ees, lostsync ? CEVNT_PROP : CEVNT_NOMINAL);
}

/* msfees_poll - called by the transmit procedure */
static void
msfees_poll(
	int unit,
	struct peer *peer
	)
{
	if (unit >= MAXUNITS) {
		msyslog(LOG_ERR, "ees clock poll: INTERNAL: unit %d invalid",
			unit);
		return;
	}
	if (!unitinuse[unit]) {
		msyslog(LOG_ERR, "ees clock poll: INTERNAL: unit %d unused",
			unit);
		return;
	}

	ees_process(eesunits[unit]);

	if ((current_time - eesunits[unit]->lasttime) > 150)
	    ees_event(eesunits[unit], CEVNT_FAULT);
}


#else
NONEMPTY_TRANSLATION_UNIT
#endif /* REFCLOCK */
