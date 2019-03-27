/* chutest.c,v 3.1 1993/07/06 01:05:21 jbj Exp
 * chutest - test the CHU clock
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_STROPTS_H
# include <stropts.h>
#else
# ifdef HAVE_SYS_STROPTS_H
#  include <sys/stropts.h>
# endif
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/file.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#else
# ifdef HAVE_SGTTY_H
#  include <sgtty.h>
# endif
#endif

#include "ntp_fp.h"
#include "ntp.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"

#ifdef CHULDISC
# ifdef HAVE_SYS_CHUDEFS_H
#  include <sys/chudefs.h>
# endif
#endif


#ifndef CHULDISC
#define	NCHUCHARS	(10)

struct chucode {
	u_char codechars[NCHUCHARS];	/* code characters */
	u_char ncodechars;		/* number of code characters */
	u_char chustatus;		/* not used currently */
	struct timeval codetimes[NCHUCHARS];	/* arrival times */
};
#endif

#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

char const *progname;

int dofilter = 0;	/* set to 1 when we should run filter algorithm */
int showtimes = 0;	/* set to 1 when we should show char arrival times */
int doprocess = 0;	/* set to 1 when we do processing analogous to driver */
#ifdef CHULDISC
int usechuldisc = 0;	/* set to 1 when CHU line discipline should be used */
#endif
#ifdef STREAM
int usechuldisc = 0;	/* set to 1 when CHU line discipline should be used */
#endif

struct timeval lasttv;
struct chucode chudata;

void	error(char *fmt, char *s1, char *s2);
void	init_chu(void);
int	openterm(char *dev);
int	process_raw(int s);
int	process_ldisc(int s);
void	raw_filter(unsigned int c, struct timeval *tv);
void	chufilter(struct chucode *chuc,	l_fp *rtime);


/*
 * main - parse arguments and handle options
 */
int
main(
	int argc,
	char *argv[]
	)
{
	int c;
	int errflg = 0;
	extern int ntp_optind;

	progname = argv[0];
	while ((c = ntp_getopt(argc, argv, "cdfpt")) != EOF)
	    switch (c) {
		case 'c':
#ifdef STREAM
		    usechuldisc = 1;
		    break;
#endif
#ifdef CHULDISC
		    usechuldisc = 1;
		    break;
#endif
#ifndef STREAM
#ifndef CHULDISC
		    (void) fprintf(stderr,
				   "%s: CHU line discipline not available on this machine\n",
				   progname);
		    exit(2);
#endif
#endif
		case 'd':
		    ++debug;
		    break;
		case 'f':
		    dofilter = 1;
		    break;
		case 'p':
		    doprocess = 1;
		case 't':
		    showtimes = 1;
		    break;
		default:
		    errflg++;
		    break;
	    }
	if (errflg || ntp_optind+1 != argc) {
#ifdef STREAM
		(void) fprintf(stderr, "usage: %s [-dft] tty_device\n",
			       progname);
#endif
#ifdef CHULDISC
		(void) fprintf(stderr, "usage: %s [-dft] tty_device\n",
			       progname);
#endif
#ifndef STREAM
#ifndef CHULDISC
		(void) fprintf(stderr, "usage: %s [-cdft] tty_device\n",
			       progname);
#endif
#endif
		exit(2);
	}

	(void) gettimeofday(&lasttv, (struct timezone *)0);
	c = openterm(argv[ntp_optind]);
	init_chu();
#ifdef STREAM
	if (usechuldisc)
	    process_ldisc(c);
	else
#endif
#ifdef CHULDISC
	    if (usechuldisc)
		process_ldisc(c);
	    else
#endif
		process_raw(c);
	/*NOTREACHED*/
}


/*
 * openterm - open a port to the CHU clock
 */
int
openterm(
	char *dev
	)
{
	int s;
	struct sgttyb ttyb;

	if (debug)
	    (void) fprintf(stderr, "Doing open...");
	if ((s = open(dev, O_RDONLY, 0777)) < 0)
	    error("open(%s)", dev, "");
	if (debug)
	    (void) fprintf(stderr, "open okay\n");

	if (debug)
	    (void) fprintf(stderr, "Setting exclusive use...");
	if (ioctl(s, TIOCEXCL, (char *)0) < 0)
	    error("ioctl(TIOCEXCL)", "", "");
	if (debug)
	    (void) fprintf(stderr, "done\n");
	
	ttyb.sg_ispeed = ttyb.sg_ospeed = B300;
	ttyb.sg_erase = ttyb.sg_kill = 0;
	ttyb.sg_flags = EVENP|ODDP|RAW;
	if (debug)
	    (void) fprintf(stderr, "Setting baud rate et al...");
	if (ioctl(s, TIOCSETP, (char *)&ttyb) < 0)
	    error("ioctl(TIOCSETP, raw)", "", "");
	if (debug)
	    (void) fprintf(stderr, "done\n");

#ifdef CHULDISC
	if (usechuldisc) {
		int ldisc;

		if (debug)
		    (void) fprintf(stderr, "Switching to CHU ldisc...");
		ldisc = CHULDISC;
		if (ioctl(s, TIOCSETD, (char *)&ldisc) < 0)
		    error("ioctl(TIOCSETD, CHULDISC)", "", "");
		if (debug)
		    (void) fprintf(stderr, "okay\n");
	}
#endif
#ifdef STREAM
	if (usechuldisc) {

		if (debug)
		    (void) fprintf(stderr, "Poping off streams...");
		while (ioctl(s, I_POP, 0) >=0) ;
		if (debug)
		    (void) fprintf(stderr, "okay\n");
		if (debug)
		    (void) fprintf(stderr, "Pushing CHU stream...");
		if (ioctl(s, I_PUSH, "chu") < 0)
		    error("ioctl(I_PUSH, \"chu\")", "", "");
		if (debug)
		    (void) fprintf(stderr, "okay\n");
	}
#endif
	return s;
}


/*
 * process_raw - process characters in raw mode
 */
int
process_raw(
	int s
	)
{
	u_char c;
	int n;
	struct timeval tv;
	struct timeval difftv;

	while ((n = read(s, &c, sizeof(char))) > 0) {
		(void) gettimeofday(&tv, (struct timezone *)0);
		if (dofilter)
		    raw_filter((unsigned int)c, &tv);
		else {
			difftv.tv_sec = tv.tv_sec - lasttv.tv_sec;
			difftv.tv_usec = tv.tv_usec - lasttv.tv_usec;
			if (difftv.tv_usec < 0) {
				difftv.tv_sec--;
				difftv.tv_usec += 1000000;
			}
			(void) printf("%02x\t%lu.%06lu\t%lu.%06lu\n",
				      c, tv.tv_sec, tv.tv_usec, difftv.tv_sec,
				      difftv.tv_usec);
			lasttv = tv;
		}
	}

	if (n == 0) {
		(void) fprintf(stderr, "%s: zero returned on read\n", progname);
		exit(1);
	} else
	    error("read()", "", "");
}


/*
 * raw_filter - run the line discipline filter over raw data
 */
void
raw_filter(
	unsigned int c,
	struct timeval *tv
	)
{
	static struct timeval diffs[10];
	struct timeval diff;
	l_fp ts;

	if ((c & 0xf) > 9 || ((c>>4)&0xf) > 9) {
		if (debug)
		    (void) fprintf(stderr,
				   "character %02x failed BCD test\n", c);
		chudata.ncodechars = 0;
		return;
	}

	if (chudata.ncodechars > 0) {
		diff.tv_sec = tv->tv_sec
			- chudata.codetimes[chudata.ncodechars].tv_sec;
		diff.tv_usec = tv->tv_usec
			- chudata.codetimes[chudata.ncodechars].tv_usec;
		if (diff.tv_usec < 0) {
			diff.tv_sec--;
			diff.tv_usec += 1000000;
		} /*
		    if (diff.tv_sec != 0 || diff.tv_usec > 900000) {
		    if (debug)
		    (void) fprintf(stderr,
		    "character %02x failed time test\n");
		    chudata.ncodechars = 0;
		    return;
		    } */
	}

	chudata.codechars[chudata.ncodechars] = c;
	chudata.codetimes[chudata.ncodechars] = *tv;
	if (chudata.ncodechars > 0)
	    diffs[chudata.ncodechars] = diff;
	if (++chudata.ncodechars == 10) {
		if (doprocess) {
			TVTOTS(&chudata.codetimes[NCHUCHARS-1], &ts);
			ts.l_ui += JAN_1970;
			chufilter(&chudata, &chudata.codetimes[NCHUCHARS-1]);
		} else {
			register int i;

			for (i = 0; i < chudata.ncodechars; i++) {
				(void) printf("%x%x\t%lu.%06lu\t%lu.%06lu\n",
					      chudata.codechars[i] & 0xf,
					      (chudata.codechars[i] >>4 ) & 0xf,
					      chudata.codetimes[i].tv_sec,
					      chudata.codetimes[i].tv_usec,
					      diffs[i].tv_sec, diffs[i].tv_usec);
			}
		}
		chudata.ncodechars = 0;
	}
}


/* #ifdef CHULDISC*/
/*
 * process_ldisc - process line discipline
 */
int
process_ldisc(
	int s
	)
{
	struct chucode chu;
	int n;
	register int i;
	struct timeval diff;
	l_fp ts;
	void chufilter();

	while ((n = read(s, (char *)&chu, sizeof chu)) > 0) {
		if (n != sizeof chu) {
			(void) fprintf(stderr, "Expected %d, got %d\n",
				       sizeof chu, n);
			continue;
		}

		if (doprocess) {
			TVTOTS(&chu.codetimes[NCHUCHARS-1], &ts);
			ts.l_ui += JAN_1970;
			chufilter(&chu, &ts);
		} else {
			for (i = 0; i < NCHUCHARS; i++) {
				if (i == 0)
				    diff.tv_sec = diff.tv_usec = 0;
				else {
					diff.tv_sec = chu.codetimes[i].tv_sec
						- chu.codetimes[i-1].tv_sec;
					diff.tv_usec = chu.codetimes[i].tv_usec
						- chu.codetimes[i-1].tv_usec;
					if (diff.tv_usec < 0) {
						diff.tv_sec--;
						diff.tv_usec += 1000000;
					}
				}
				(void) printf("%x%x\t%lu.%06lu\t%lu.%06lu\n",
					      chu.codechars[i] & 0xf, (chu.codechars[i]>>4)&0xf,
					      chu.codetimes[i].tv_sec, chu.codetimes[i].tv_usec,
					      diff.tv_sec, diff.tv_usec);
			}
		}
	}
	if (n == 0) {
		(void) fprintf(stderr, "%s: zero returned on read\n", progname);
		exit(1);
	} else
	    error("read()", "", "");
}
/*#endif*/


/*
 * error - print an error message
 */
void
error(
	char *fmt,
	char *s1,
	char *s2
	)
{
	(void) fprintf(stderr, "%s: ", progname);
	(void) fprintf(stderr, fmt, s1, s2);
	(void) fprintf(stderr, ": ");
	perror("");
	exit(1);
}

/*
 * Definitions
 */
#define	MAXUNITS	4	/* maximum number of CHU units permitted */
#define	CHUDEV	"/dev/chu%d"	/* device we open.  %d is unit number */
#define	NCHUCODES	9	/* expect 9 CHU codes per minute */

/*
 * When CHU is operating optimally we want the primary clock distance
 * to come out at 300 ms.  Thus, peer.distance in the CHU peer structure
 * is set to 290 ms and we compute delays which are at least 10 ms long.
 * The following are 290 ms and 10 ms expressed in u_fp format
 */
#define	CHUDISTANCE	0x00004a3d
#define	CHUBASEDELAY	0x0000028f

/*
 * To compute a quality for the estimate (a pseudo delay) we add a
 * fixed 10 ms for each missing code in the minute and add to this
 * the sum of the differences between the remaining offsets and the
 * estimated sample offset.
 */
#define	CHUDELAYPENALTY	0x0000028f

/*
 * Other constant stuff
 */
#define	CHUPRECISION	(-9)		/* what the heck */
#define	CHUREFID	"CHU\0"

/*
 * Default fudge factors
 */
#define	DEFPROPDELAY	0x00624dd3	/* 0.0015 seconds, 1.5 ms */
#define	DEFFILTFUDGE	0x000d1b71	/* 0.0002 seconds, 200 us */

/*
 * Hacks to avoid excercising the multiplier.  I have no pride.
 */
#define	MULBY10(x)	(((x)<<3) + ((x)<<1))
#define	MULBY60(x)	(((x)<<6) - ((x)<<2))	/* watch overflow */
#define	MULBY24(x)	(((x)<<4) + ((x)<<3))

/*
 * Constants for use when multiplying by 0.1.  ZEROPTONE is 0.1
 * as an l_fp fraction, NZPOBITS is the number of significant bits
 * in ZEROPTONE.
 */
#define	ZEROPTONE	0x1999999a
#define	NZPOBITS	29

/*
 * The CHU table.  This gives the expected time of arrival of each
 * character after the on-time second and is computed as follows:
 * The CHU time code is sent at 300 bps.  Your average UART will
 * synchronize at the edge of the start bit and will consider the
 * character complete at the center of the first stop bit, i.e.
 * 0.031667 ms later.  Thus the expected time of each interrupt
 * is the start bit time plus 0.031667 seconds.  These times are
 * in chutable[].  To this we add such things as propagation delay
 * and delay fudge factor.
 */
#define	CHARDELAY	0x081b4e80

static u_long chutable[NCHUCHARS] = {
	0x2147ae14 + CHARDELAY,		/* 0.130 (exactly) */
	0x2ac08312 + CHARDELAY,		/* 0.167 (exactly) */
	0x34395810 + CHARDELAY,		/* 0.204 (exactly) */
	0x3db22d0e + CHARDELAY,		/* 0.241 (exactly) */
	0x472b020c + CHARDELAY,		/* 0.278 (exactly) */
	0x50a3d70a + CHARDELAY,		/* 0.315 (exactly) */
	0x5a1cac08 + CHARDELAY,		/* 0.352 (exactly) */
	0x63958106 + CHARDELAY,		/* 0.389 (exactly) */
	0x6d0e5604 + CHARDELAY,		/* 0.426 (exactly) */
	0x76872b02 + CHARDELAY,		/* 0.463 (exactly) */
};

/*
 * Keep the fudge factors separately so they can be set even
 * when no clock is configured.
 */
static l_fp propagation_delay;
static l_fp fudgefactor;
static l_fp offset_fudge;

/*
 * We keep track of the start of the year, watching for changes.
 * We also keep track of whether the year is a leap year or not.
 * All because stupid CHU doesn't include the year in the time code.
 */
static u_long yearstart;

/*
 * Imported from the timer module
 */
extern u_long current_time;
extern struct event timerqueue[];

/*
 * init_chu - initialize internal chu driver data
 */
void
init_chu(void)
{

	/*
	 * Initialize fudge factors to default.
	 */
	propagation_delay.l_ui = 0;
	propagation_delay.l_uf = DEFPROPDELAY;
	fudgefactor.l_ui = 0;
	fudgefactor.l_uf = DEFFILTFUDGE;
	offset_fudge = propagation_delay;
	L_ADD(&offset_fudge, &fudgefactor);

	yearstart = 0;
}


void
chufilter(
	struct chucode *chuc,
	l_fp *rtime
	)
{
	register int i;
	register u_long date_ui;
	register u_long tmp;
	register u_char *code;
	int isneg;
	int imin;
	int imax;
	u_long reftime;
	l_fp off[NCHUCHARS];
	l_fp ts;
	int day, hour, minute, second;
	static u_char lastcode[NCHUCHARS];

	/*
	 * We'll skip the checks made in the kernel, but assume they've
	 * been done.  This means that all characters are BCD and
	 * the intercharacter spacing isn't unreasonable.
	 */

	/*
	 * print the code
	 */
	for (i = 0; i < NCHUCHARS; i++)
	    printf("%c%c", (chuc->codechars[i] & 0xf) + '0',
		   ((chuc->codechars[i]>>4) & 0xf) + '0');
	printf("\n");

	/*
	 * Format check.  Make sure the two halves match.
	 */
	for (i = 0; i < NCHUCHARS/2; i++)
	    if (chuc->codechars[i] != chuc->codechars[i+(NCHUCHARS/2)]) {
		    (void) printf("Bad format, halves don't match\n");
		    return;
	    }
	
	/*
	 * Break out the code into the BCD nibbles.  Only need to fiddle
	 * with the first half since both are identical.  Note the first
	 * BCD character is the low order nibble, the second the high order.
	 */
	code = lastcode;
	for (i = 0; i < NCHUCHARS/2; i++) {
		*code++ = chuc->codechars[i] & 0xf;
		*code++ = (chuc->codechars[i] >> 4) & 0xf;
	}

	/*
	 * If the first nibble isn't a 6, we're up the creek
	 */
	code = lastcode;
	if (*code++ != 6) {
		(void) printf("Bad format, no 6 at start\n");
		return;
	}

	/*
	 * Collect the day, the hour, the minute and the second.
	 */
	day = *code++;
	day = MULBY10(day) + *code++;
	day = MULBY10(day) + *code++;
	hour = *code++;
	hour = MULBY10(hour) + *code++;
	minute = *code++;
	minute = MULBY10(minute) + *code++;
	second = *code++;
	second = MULBY10(second) + *code++;

	/*
	 * Sanity check the day and time.  Note that this
	 * only occurs on the 31st through the 39th second
	 * of the minute.
	 */
	if (day < 1 || day > 366
	    || hour > 23 || minute > 59
	    || second < 31 || second > 39) {
		(void) printf("Failed date sanity check: %d %d %d %d\n",
			      day, hour, minute, second);
		return;
	}

	/*
	 * Compute seconds into the year.
	 */
	tmp = (u_long)(MULBY24((day-1)) + hour);	/* hours */
	tmp = MULBY60(tmp) + (u_long)minute;		/* minutes */
	tmp = MULBY60(tmp) + (u_long)second;		/* seconds */

	/*
	 * Now the fun begins.  We demand that the received time code
	 * be within CLOCK_WAYTOOBIG of the receive timestamp, but
	 * there is uncertainty about the year the timestamp is in.
	 * Use the current year start for the first check, this should
	 * work most of the time.
	 */
	date_ui = tmp + yearstart;
#define CLOCK_WAYTOOBIG 1000 /* revived from ancient sources */
	if (date_ui < (rtime->l_ui + CLOCK_WAYTOOBIG)
	    && date_ui > (rtime->l_ui - CLOCK_WAYTOOBIG))
	    goto codeokay;	/* looks good */

	/*
	 * Trouble.  Next check is to see if the year rolled over and, if
	 * so, try again with the new year's start.
	 */
	date_ui = calyearstart(rtime->l_ui, NULL);
	if (date_ui != yearstart) {
		yearstart = date_ui;
		date_ui += tmp;
		(void) printf("time %u, code %u, difference %d\n",
			      date_ui, rtime->l_ui, (long)date_ui-(long)rtime->l_ui);
		if (date_ui < (rtime->l_ui + CLOCK_WAYTOOBIG)
		    && date_ui > (rtime->l_ui - CLOCK_WAYTOOBIG))
		    goto codeokay;	/* okay this time */
	}

	ts.l_uf = 0;
	ts.l_ui = yearstart;
	printf("yearstart %s\n", prettydate(&ts));
	printf("received %s\n", prettydate(rtime));
	ts.l_ui = date_ui;
	printf("date_ui %s\n", prettydate(&ts));

	/*
	 * Here we know the year start matches the current system
	 * time.  One remaining possibility is that the time code
	 * is in the year previous to that of the system time.  This
	 * is only worth checking if the receive timestamp is less
	 * than CLOCK_WAYTOOBIG seconds into the new year.
	 */
	if ((rtime->l_ui - yearstart) < CLOCK_WAYTOOBIG) {
		date_ui = tmp; 
		date_ui += calyearstart(yearstart - CLOCK_WAYTOOBIG,
					NULL);
		if ((rtime->l_ui - date_ui) < CLOCK_WAYTOOBIG)
		    goto codeokay;
	}

	/*
	 * One last possibility is that the time stamp is in the year
	 * following the year the system is in.  Try this one before
	 * giving up.
	 */
	date_ui = tmp;
	date_ui += calyearstart(yearstart + (400 * SECSPERDAY),
				NULL);
	if ((date_ui - rtime->l_ui) >= CLOCK_WAYTOOBIG) {
		printf("Date hopelessly off\n");
		return;		/* hopeless, let it sync to other peers */
	}

    codeokay:
	reftime = date_ui;
	/*
	 * We've now got the integral seconds part of the time code (we hope).
	 * The fractional part comes from the table.  We next compute
	 * the offsets for each character.
	 */
	for (i = 0; i < NCHUCHARS; i++) {
		register u_long tmp2;

		off[i].l_ui = date_ui;
		off[i].l_uf = chutable[i];
		tmp = chuc->codetimes[i].tv_sec + JAN_1970;
		TVUTOTSF(chuc->codetimes[i].tv_usec, tmp2);
		M_SUB(off[i].l_ui, off[i].l_uf, tmp, tmp2);
	}

	/*
	 * Here is a *big* problem.  What one would normally
	 * do here on a machine with lots of clock bits (say
	 * a Vax or the gizmo board) is pick the most positive
	 * offset and the estimate, since this is the one that
	 * is most likely suffered the smallest interrupt delay.
	 * The trouble is that the low order clock bit on an IBM
	 * RT, which is the machine I had in mind when doing this,
	 * ticks at just under the millisecond mark.  This isn't
	 * precise enough.  What we can do to improve this is to
	 * average all 10 samples and rely on the second level
	 * filtering to pick the least delayed estimate.  Trouble
	 * is, this means we have to divide a 64 bit fixed point
	 * number by 10, a procedure which really sucks.  Oh, well.
	 * First compute the sum.
	 */
	date_ui = 0;
	tmp = 0;
	for (i = 0; i < NCHUCHARS; i++)
	    M_ADD(date_ui, tmp, off[i].l_ui, off[i].l_uf);
	if (M_ISNEG(date_ui, tmp))
	    isneg = 1;
	else
	    isneg = 0;
	
	/*
	 * Here is a multiply-by-0.1 optimization that should apply
	 * just about everywhere.  If the magnitude of the sum
	 * is less than 9 we don't have to worry about overflow
	 * out of a 64 bit product, even after rounding.
	 */
	if (date_ui < 9 || date_ui > 0xfffffff7) {
		register u_long prod_ui;
		register u_long prod_uf;

		prod_ui = prod_uf = 0;
		/*
		 * This code knows the low order bit in 0.1 is zero
		 */
		for (i = 1; i < NZPOBITS; i++) {
			M_LSHIFT(date_ui, tmp);
			if (ZEROPTONE & (1<<i))
			    M_ADD(prod_ui, prod_uf, date_ui, tmp);
		}

		/*
		 * Done, round it correctly.  Prod_ui contains the
		 * fraction.
		 */
		if (prod_uf & 0x80000000)
		    prod_ui++;
		if (isneg)
		    date_ui = 0xffffffff;
		else
		    date_ui = 0;
		tmp = prod_ui;
		/*
		 * date_ui is integral part, tmp is fraction.
		 */
	} else {
		register u_long prod_ovr;
		register u_long prod_ui;
		register u_long prod_uf;
		register u_long highbits;

		prod_ovr = prod_ui = prod_uf = 0;
		if (isneg)
		    highbits = 0xffffffff;	/* sign extend */
		else
		    highbits = 0;
		/*
		 * This code knows the low order bit in 0.1 is zero
		 */
		for (i = 1; i < NZPOBITS; i++) {
			M_LSHIFT3(highbits, date_ui, tmp);
			if (ZEROPTONE & (1<<i))
			    M_ADD3(prod_ovr, prod_uf, prod_ui,
				   highbits, date_ui, tmp);
		}

		if (prod_uf & 0x80000000)
		    M_ADDUF(prod_ovr, prod_ui, (u_long)1);
		date_ui = prod_ovr;
		tmp = prod_ui;
	}

	/*
	 * At this point we have the mean offset, with the integral
	 * part in date_ui and the fractional part in tmp.  Store
	 * it in the structure.
	 */
	/*
	 * Add in fudge factor.
	 */
	M_ADD(date_ui, tmp, offset_fudge.l_ui, offset_fudge.l_uf);

	/*
	 * Find the minimun and maximum offset
	 */
	imin = imax = 0;
	for (i = 1; i < NCHUCHARS; i++) {
		if (L_ISGEQ(&off[i], &off[imax])) {
			imax = i;
		} else if (L_ISGEQ(&off[imin], &off[i])) {
			imin = i;
		}
	}

	L_ADD(&off[imin], &offset_fudge);
	if (imin != imax)
	    L_ADD(&off[imax], &offset_fudge);
	(void) printf("mean %s, min %s, max %s\n",
		      mfptoa(date_ui, tmp, 8), lfptoa(&off[imin], 8),
		      lfptoa(&off[imax], 8));
}
