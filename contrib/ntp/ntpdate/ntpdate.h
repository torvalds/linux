/*
 * ntpdate.h - declarations for the ntpdate and ntptimeset programs
 */

#include "ntp_malloc.h"

extern void	loadservers	(char *cfgpath);

/*
 * The server structure is a much simplified version of the
 * peer structure, for ntpdate's use.  Since we always send
 * in client mode and expect to receive in server mode, this
 * leaves only a very limited number of things we need to
 * remember about the server.
 */
struct server {
	struct server *next_server;	/* next server in build list */
	sockaddr_u srcadr;		/* address of remote host */
	u_char version;			/* version to use */
	u_char leap;			/* leap indicator */
	u_char stratum;			/* stratum of remote server */
	s_char precision;		/* server's clock precision */
	u_char trust;			/* trustability of the filtered data */
	u_fp rootdelay;			/* distance from primary clock */
	u_fp rootdisp;			/* peer clock dispersion */
	u_int32 refid;			/* peer reference ID */
	l_fp reftime;			/* time of peer's last update */
	u_long event_time;		/* time for next timeout */
	u_long last_xmit;		/* time of last transmit */
	u_short xmtcnt;			/* number of packets transmitted */
	u_short rcvcnt;			/* number of packets received */
	u_char reach;			/* reachability, NTP_WINDOW bits */
	u_short filter_nextpt;		/* index into filter shift register */
	s_fp filter_delay[NTP_SHIFT];	/* delay part of shift register */
	l_fp filter_offset[NTP_SHIFT];	/* offset part of shift register */
	s_fp filter_soffset[NTP_SHIFT]; /* offset in s_fp format, for disp */
	u_fp filter_error[NTP_SHIFT];	/* error part of shift register */
	l_fp org;			/* peer's originate time stamp */
	l_fp xmt;			/* transmit time stamp */
	u_fp delay;			/* filter estimated delay */
	u_fp dispersion;		/* filter estimated dispersion */
	l_fp offset;			/* filter estimated clock offset */
	s_fp soffset;			/* fp version of above */
};


/*
 * ntpdate runs everything on a simple, short timeout.  It sends a
 * packet and sets the timeout (by default, to a small value suitable
 * for a LAN).  If it receives a response it sends another request.
 * If it times out it shifts zeroes into the filter and sends another
 * request.
 *
 * The timer routine is run often (once every 1/5 second currently)
 * so that time outs are done with reasonable precision.
 */
#define TIMER_HZ	(5)		/* 5 per second */

/*
 * ntpdate will make a long adjustment using adjtime() if the times
 * are close, or step the time if the times are farther apart.  The
 * following defines what is "close".
 */
#define	NTPDATE_THRESHOLD	(FP_SECOND >> 1)	/* 1/2 second */

#define NTP_MAXAGE	86400	/* one day in seconds */

/*
 * When doing adjustments, ntpdate actually overadjusts (currently
 * by 50%, though this may change).  While this will make it take longer
 * to reach a steady state condition, it will typically result in
 * the clock keeping more accurate time, on average.  The amount of
 * overshoot is limited.
 */
#ifdef	NOTNOW
#define	ADJ_OVERSHOOT	1/2	/* this is hard coded */
#endif	/* NOTNOW */
#define	ADJ_MAXOVERSHOOT	0x10000000	/* 50 ms as a ts fraction */

/*
 * Since ntpdate isn't aware of some of the things that normally get
 * put in an NTP packet, we fix some values.
 */
#define	NTPDATE_PRECISION	(-6)		/* use this precision */
#define	NTPDATE_DISTANCE	FP_SECOND	/* distance is 1 sec */
#define	NTPDATE_DISP		FP_SECOND	/* so is the dispersion */
#define	NTPDATE_REFID		(0)		/* reference ID to use */
#define PEER_MAXDISP	(64*FP_SECOND)	/* maximum dispersion (fp 64) */


/*
 * No less than 2s between requests to a server to stay within ntpd's
 * default "discard minimum 1" (and 1s enforcement slop).  That is
 * enforced only if the nondefault limited restriction is in place, such
 * as with "restrict ... limited" and "restrict ... kod limited".
 */
#define	MINTIMEOUT	(1 * TIMER_HZ)	/* 1s min. between packets */
#define	DEFTIMEOUT	(2 * TIMER_HZ)	/* 2s by default */
#define	DEFSAMPLES	4		/* get 4 samples per server */
#define	DEFPRECISION	(-5)		/* the precision we claim */
#define	DEFMAXPERIOD	60		/* maximum time to wait */
#define	DEFMINSERVERS	3		/* minimum responding servers */
#define	DEFMINVALID	1		/* mimimum servers with valid time */

/*
 * Define the max number of sockets we can open
 */
#define MAX_AF 2
