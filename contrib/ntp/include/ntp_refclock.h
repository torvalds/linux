/*
 * ntp_refclock.h - definitions for reference clock support
 */

#ifndef NTP_REFCLOCK_H
#define NTP_REFCLOCK_H

#if defined(HAVE_SYS_MODEM_H)
#include <sys/modem.h>
#endif

#include "ntp_types.h"
#include "ntp_tty.h"
#include "recvbuff.h"


#define SAMPLE(x)	pp->coderecv = (pp->coderecv + 1) % MAXSTAGE; \
			pp->filter[pp->coderecv] = (x); \
			if (pp->coderecv == pp->codeproc) \
				pp->codeproc = (pp->codeproc + 1) % MAXSTAGE;

/*
 * Macros to determine the clock type and unit numbers from a
 * 127.127.t.u address
 */
#define	REFCLOCKTYPE(srcadr)	((SRCADR(srcadr) >> 8) & 0xff)
#define REFCLOCKUNIT(srcadr)	(SRCADR(srcadr) & 0xff)

/*
 * List of reference clock names and descriptions. These must agree with
 * lib/clocktypes.c and ntpd/refclock_conf.c.
 */
struct clktype {
	int code;		/* driver "major" number */
	const char *clocktype;	/* long description */
	const char *abbrev;	/* short description */
};
extern struct clktype clktypes[];

/*
 * Configuration flag values
 */
#define	CLK_HAVETIME1	0x1
#define	CLK_HAVETIME2	0x2
#define	CLK_HAVEVAL1	0x4
#define	CLK_HAVEVAL2	0x8

#define	CLK_FLAG1	0x1
#define	CLK_FLAG2	0x2
#define	CLK_FLAG3	0x4
#define	CLK_FLAG4	0x8

#define	CLK_HAVEFLAG1	0x10
#define	CLK_HAVEFLAG2	0x20
#define	CLK_HAVEFLAG3	0x40
#define	CLK_HAVEFLAG4	0x80

/*
 * Constant for disabling event reporting in
 * refclock_receive. ORed in leap
 * parameter
 */
#define REFCLOCK_OWN_STATES	0x80

/*
 * Structure for returning clock status
 */
struct refclockstat {
	u_char	type;		/* clock type */
	u_char	flags;		/* clock flags */
	u_char	haveflags;	/* bit array of valid flags */
	u_short	lencode;	/* length of last timecode */
	const char *p_lastcode;	/* last timecode received */
	u_int32	polls;		/* transmit polls */
	u_int32	noresponse;	/* no response to poll */
	u_int32	badformat;	/* bad format timecode received */
	u_int32	baddata;	/* invalid data timecode received */
	u_int32	timereset;	/* driver resets */
	const char *clockdesc;	/* ASCII description */
	double	fudgetime1;	/* configure fudge time1 */
	double	fudgetime2;	/* configure fudge time2 */
	int32	fudgeval1;	/* configure fudge value1 */
	u_int32	fudgeval2;	/* configure fudge value2 */
	u_char	currentstatus;	/* clock status */
	u_char	lastevent;	/* last exception event */
	u_char	leap;		/* leap bits */
	struct	ctl_var *kv_list; /* additional variables */
};

/*
 * Reference clock I/O structure.  Used to provide an interface between
 * the reference clock drivers and the I/O module.
 */
struct refclockio {
	struct	refclockio *next; /* link to next structure */
	void	(*clock_recv) (struct recvbuf *); /* completion routine */
	int 	(*io_input)   (struct recvbuf *); /* input routine -
				to avoid excessive buffer use
				due to small bursts
				of refclock input data */
	struct peer *srcclock;	/* refclock peer */
	int	datalen;	/* length of data */
	int	fd;		/* file descriptor */
	u_long	recvcount;	/* count of receive completions */
	int	active;		/* nonzero when in use */

#ifdef HAVE_IO_COMPLETION_PORT
	void *	ioreg_ctx;	/* IO registration context */
	void *	device_ctx;	/* device-related data for i/o subsystem */
#endif
};

/*
 * Structure for returning debugging info
 */
#define	NCLKBUGVALUES	16
#define	NCLKBUGTIMES	32

struct refclockbug {
	u_char	nvalues;	/* values following */
	u_char	ntimes;		/* times following */
	u_short	svalues;	/* values format sign array */
	u_int32	stimes;		/* times format sign array */
	u_int32	values[NCLKBUGVALUES]; /* real values */
	l_fp	times[NCLKBUGTIMES]; /* real times */
};

#ifdef HAVE_IO_COMPLETION_PORT
extern	HANDLE	WaitableIoEventHandle;
#endif

/*
 * Structure interface between the reference clock support
 * ntp_refclock.c and the driver utility routines
 */
#define MAXSTAGE	60	/* max median filter stages  */
#define NSTAGE		5	/* default median filter stages */
#define BMAX		128	/* max timecode length */
#define GMT		0	/* I hope nobody sees this */
#define MAXDIAL		60	/* max length of modem dial strings */


struct refclockproc {
	void *	unitptr;	/* pointer to unit structure */
	struct refclock * conf;	/* refclock_conf[type] */
	struct refclockio io;	/* I/O handler structure */
	u_char	leap;		/* leap/synchronization code */
	u_char	currentstatus;	/* clock status */
	u_char	lastevent;	/* last exception event */
	u_char	type;		/* clock type */
	const char *clockdesc;	/* clock description */
	u_long	nextaction;	/* local activity timeout */
	void	(*action)(struct peer *); /* timeout callback */

	char	a_lastcode[BMAX]; /* last timecode received */
	int	lencode;	/* length of last timecode */

	int	year;		/* year of eternity */
	int	day;		/* day of year */
	int	hour;		/* hour of day */
	int	minute;		/* minute of hour */
	int	second;		/* second of minute */
	long	nsec;		/* nanosecond of second */
	u_long	yearstart;	/* beginning of year */
	int	coderecv;	/* put pointer */
	int	codeproc;	/* get pointer */
	l_fp	lastref;	/* reference timestamp */
	l_fp	lastrec;	/* receive timestamp */
	double	offset;		/* mean offset */
	double	disp;		/* sample dispersion */
	double	jitter;		/* jitter (mean squares) */
	double	filter[MAXSTAGE]; /* median filter */

	/*
	 * Configuration data
	 */
	double	fudgetime1;	/* fudge time1 */
	double	fudgetime2;	/* fudge time2 */
	u_char	stratum;	/* server stratum */
	u_int32	refid;		/* reference identifier */
	u_char	sloppyclockflag; /* fudge flags */

	/*
	 * Status tallies
 	 */
	u_long	timestarted;	/* time we started this */
	u_long	polls;		/* polls sent */
	u_long	noreply;	/* no replies to polls */
	u_long	badformat;	/* bad format reply */
	u_long	baddata;	/* bad data reply */
};

/*
 * Structure interface between the reference clock support
 * ntp_refclock.c and particular clock drivers. This must agree with the
 * structure defined in the driver.
 */
#define	noentry	0		/* flag for null routine */
#define	NOFLAGS	0		/* flag for null flags */

struct refclock {
	int (*clock_start)	(int, struct peer *);
	void (*clock_shutdown)	(int, struct peer *);
	void (*clock_poll)	(int, struct peer *);
	void (*clock_control)	(int, const struct refclockstat *,
				 struct refclockstat *, struct peer *);
	void (*clock_init)	(void);
	void (*clock_buginfo)	(int, struct refclockbug *, struct peer *);
	void (*clock_timer)	(int, struct peer *);
};

/*
 * Function prototypes
 */
extern	int	io_addclock	(struct refclockio *);
extern	void	io_closeclock	(struct refclockio *);

#ifdef REFCLOCK
extern	void	refclock_buginfo(sockaddr_u *,
				 struct refclockbug *);
extern	void	refclock_control(sockaddr_u *,
				 const struct refclockstat *,
				 struct refclockstat *);
extern	int	refclock_open	(const char *, u_int, u_int);
extern	int	refclock_setup	(int, u_int, u_int);
extern	void	refclock_timer	(struct peer *);
extern	void	refclock_transmit(struct peer *);
extern 	int	refclock_process(struct refclockproc *);
extern 	int	refclock_process_f(struct refclockproc *, double);
extern 	void	refclock_process_offset(struct refclockproc *, l_fp,
					l_fp, double);
extern	void	refclock_report	(struct peer *, int);
extern	int	refclock_gtlin	(struct recvbuf *, char *, int, l_fp *);
extern	int	refclock_gtraw	(struct recvbuf *, char *, int, l_fp *);
extern	int	indicate_refclock_packet(struct refclockio *,
					 struct recvbuf *);
extern	void	process_refclock_packet(struct recvbuf *);
#endif /* REFCLOCK */

#endif /* NTP_REFCLOCK_H */
