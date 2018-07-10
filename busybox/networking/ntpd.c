/*
 * NTP client/server, based on OpenNTPD 3.9p1
 *
 * Busybox port author: Adam Tkac (C) 2009 <vonsch@gmail.com>
 *
 * OpenNTPd 3.9p1 copyright holders:
 *   Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *   Copyright (c) 2004 Alexander Guy <alexander.guy@andern.org>
 *
 * OpenNTPd code is licensed under ISC-style licence:
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ***********************************************************************
 *
 * Parts of OpenNTPD clock syncronization code is replaced by
 * code which is based on ntp-4.2.6, which carries the following
 * copyright notice:
 *
 * Copyright (c) University of Delaware 1992-2009
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose with or without fee is hereby
 * granted, provided that the above copyright notice appears in all
 * copies and that both the copyright notice and this permission
 * notice appear in supporting documentation, and that the name
 * University of Delaware not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission. The University of Delaware makes no
 * representations about the suitability this software for any
 * purpose. It is provided "as is" without express or implied warranty.
 ***********************************************************************
 */
//config:config NTPD
//config:	bool "ntpd (17 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The NTP client/server daemon.
//config:
//config:config FEATURE_NTPD_SERVER
//config:	bool "Make ntpd usable as a NTP server"
//config:	default y
//config:	depends on NTPD
//config:	help
//config:	Make ntpd usable as a NTP server. If you disable this option
//config:	ntpd will be usable only as a NTP client.
//config:
//config:config FEATURE_NTPD_CONF
//config:	bool "Make ntpd understand /etc/ntp.conf"
//config:	default y
//config:	depends on NTPD
//config:	help
//config:	Make ntpd look in /etc/ntp.conf for peers. Only "server address"
//config:	is supported.

//applet:IF_NTPD(APPLET(ntpd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_NTPD) += ntpd.o

//usage:#define ntpd_trivial_usage
//usage:	"[-dnqNw"IF_FEATURE_NTPD_SERVER("l -I IFACE")"] [-S PROG] [-p PEER]..."
//usage:#define ntpd_full_usage "\n\n"
//usage:       "NTP client/server\n"
//usage:     "\n	-d	Verbose (may be repeated)"
//usage:     "\n	-n	Do not daemonize"
//usage:     "\n	-q	Quit after clock is set"
//usage:     "\n	-N	Run at high priority"
//usage:     "\n	-w	Do not set time (only query peers), implies -n"
//usage:     "\n	-S PROG	Run PROG after stepping time, stratum change, and every 11 mins"
//usage:     "\n	-p PEER	Obtain time from PEER (may be repeated)"
//usage:	IF_FEATURE_NTPD_CONF(
//usage:     "\n		If -p is not given, 'server HOST' lines"
//usage:     "\n		from /etc/ntp.conf are used"
//usage:	)
//usage:	IF_FEATURE_NTPD_SERVER(
//usage:     "\n	-l	Also run as server on port 123"
//usage:     "\n	-I IFACE Bind server to IFACE, implies -l"
//usage:	)

// -l and -p options are not compatible with "standard" ntpd:
// it has them as "-l logfile" and "-p pidfile".
// -S and -w are not compat either, "standard" ntpd has no such opts.

#include "libbb.h"
#include <math.h>
#include <netinet/ip.h> /* For IPTOS_DSCP_AF21 definition */
#include <sys/timex.h>
#ifndef IPTOS_DSCP_AF21
# define IPTOS_DSCP_AF21 0x48
#endif


/* Verbosity control (max level of -dddd options accepted).
 * max 6 is very talkative (and bloated). 3 is non-bloated,
 * production level setting.
 */
#define MAX_VERBOSE     3


/* High-level description of the algorithm:
 *
 * We start running with very small poll_exp, BURSTPOLL,
 * in order to quickly accumulate INITIAL_SAMPLES datapoints
 * for each peer. Then, time is stepped if the offset is larger
 * than STEP_THRESHOLD, otherwise it isn't; anyway, we enlarge
 * poll_exp to MINPOLL and enter frequency measurement step:
 * we collect new datapoints but ignore them for WATCH_THRESHOLD
 * seconds. After WATCH_THRESHOLD seconds we look at accumulated
 * offset and estimate frequency drift.
 *
 * (frequency measurement step seems to not be strictly needed,
 * it is conditionally disabled with USING_INITIAL_FREQ_ESTIMATION
 * define set to 0)
 *
 * After this, we enter "steady state": we collect a datapoint,
 * we select the best peer, if this datapoint is not a new one
 * (IOW: if this datapoint isn't for selected peer), sleep
 * and collect another one; otherwise, use its offset to update
 * frequency drift, if offset is somewhat large, reduce poll_exp,
 * otherwise increase poll_exp.
 *
 * If offset is larger than STEP_THRESHOLD, which shouldn't normally
 * happen, we assume that something "bad" happened (computer
 * was hibernated, someone set totally wrong date, etc),
 * then the time is stepped, all datapoints are discarded,
 * and we go back to steady state.
 *
 * Made some changes to speed up re-syncing after our clock goes bad
 * (tested with suspending my laptop):
 * - if largish offset (>= STEP_THRESHOLD == 1 sec) is seen
 *   from a peer, schedule next query for this peer soon
 *   without drastically lowering poll interval for everybody.
 *   This makes us collect enough data for step much faster:
 *   e.g. at poll = 10 (1024 secs), step was done within 5 minutes
 *   after first reply which indicated that our clock is 14 seconds off.
 * - on step, do not discard d_dispersion data of the existing datapoints,
 *   do not clear reachable_bits. This prevents discarding first ~8
 *   datapoints after the step.
 */

#define INITIAL_SAMPLES    4    /* how many samples do we want for init */
#define MIN_FREQHOLD      10    /* adjust offset, but not freq in this many first adjustments */
#define BAD_DELAY_GROWTH   4    /* drop packet if its delay grew by more than this */

#define RETRY_INTERVAL    32    /* on send/recv error, retry in N secs (need to be power of 2) */
#define NOREPLY_INTERVAL 512    /* sent, but got no reply: cap next query by this many seconds */
#define RESPONSE_INTERVAL 16    /* wait for reply up to N secs */
#define HOSTNAME_INTERVAL  4    /* hostname lookup failed. Wait N * peer->dns_errors secs for next try */
#define DNS_ERRORS_CAP  0x3f    /* peer->dns_errors is in [0..63] */

/* Step threshold (sec). std ntpd uses 0.128.
 */
#define STEP_THRESHOLD     1
/* Slew threshold (sec): adjtimex() won't accept offsets larger than this.
 * Using exact power of 2 (1/8) results in smaller code
 */
#define SLEW_THRESHOLD 0.125
//^^^^^^^^^^^^^^^^^^^^^^^^^^ TODO: man adjtimex about tmx.offset:
// "Since Linux 2.6.26, the supplied value is clamped to the range (-0.5s, +0.5s)"
// - can use this larger value instead?

/* Stepout threshold (sec). std ntpd uses 900 (11 mins (!)) */
//UNUSED: #define WATCH_THRESHOLD  128
/* NB: set WATCH_THRESHOLD to ~60 when debugging to save time) */
//UNUSED: #define PANIC_THRESHOLD 1000    /* panic threshold (sec) */

/*
 * If we got |offset| > BIGOFF from a peer, cap next query interval
 * for this peer by this many seconds:
 */
#define BIGOFF          STEP_THRESHOLD
#define BIGOFF_INTERVAL (1 << 7) /* 128 s */

#define FREQ_TOLERANCE  0.000015 /* frequency tolerance (15 PPM) */
#define BURSTPOLL       0       /* initial poll */
#define MINPOLL         5       /* minimum poll interval. std ntpd uses 6 (6: 64 sec) */
/*
 * If offset > discipline_jitter * POLLADJ_GATE, and poll interval is > 2^BIGPOLL,
 * then it is decreased _at once_. (If <= 2^BIGPOLL, it will be decreased _eventually_).
 */
#define BIGPOLL         9       /* 2^9 sec ~= 8.5 min */
#define MAXPOLL         12      /* maximum poll interval (12: 1.1h, 17: 36.4h). std ntpd uses 17 */
/*
 * Actively lower poll when we see such big offsets.
 * With SLEW_THRESHOLD = 0.125, it means we try to sync more aggressively
 * if offset increases over ~0.04 sec
 */
//#define POLLDOWN_OFFSET (SLEW_THRESHOLD / 3)
#define MINDISP         0.01    /* minimum dispersion (sec) */
#define MAXDISP         16      /* maximum dispersion (sec) */
#define MAXSTRAT        16      /* maximum stratum (infinity metric) */
#define MAXDIST         1       /* distance threshold (sec) */
#define MIN_SELECTED    1       /* minimum intersection survivors */
#define MIN_CLUSTERED   3       /* minimum cluster survivors */

#define MAXDRIFT        0.000500 /* frequency drift we can correct (500 PPM) */

/* Poll-adjust threshold.
 * When we see that offset is small enough compared to discipline jitter,
 * we grow a counter: += MINPOLL. When counter goes over POLLADJ_LIMIT,
 * we poll_exp++. If offset isn't small, counter -= poll_exp*2,
 * and when it goes below -POLLADJ_LIMIT, we poll_exp--.
 * (Bumped from 30 to 40 since otherwise I often see poll_exp going *2* steps down)
 */
#define POLLADJ_LIMIT   40
/* If offset < discipline_jitter * POLLADJ_GATE, then we decide to increase
 * poll interval (we think we can't improve timekeeping
 * by staying at smaller poll).
 */
#define POLLADJ_GATE    4
#define TIMECONST_HACK_GATE 2
/* Compromise Allan intercept (sec). doc uses 1500, std ntpd uses 512 */
#define ALLAN           512
/* PLL loop gain */
#define PLL             65536
/* FLL loop gain [why it depends on MAXPOLL??] */
#define FLL             (MAXPOLL + 1)
/* Parameter averaging constant */
#define AVG             4


enum {
	NTP_VERSION     = 4,
	NTP_MAXSTRATUM  = 15,

	NTP_DIGESTSIZE     = 16,
	NTP_MSGSIZE_NOAUTH = 48,
	NTP_MSGSIZE        = (NTP_MSGSIZE_NOAUTH + 4 + NTP_DIGESTSIZE),

	/* Status Masks */
	MODE_MASK       = (7 << 0),
	VERSION_MASK    = (7 << 3),
	VERSION_SHIFT   = 3,
	LI_MASK         = (3 << 6),

	/* Leap Second Codes (high order two bits of m_status) */
	LI_NOWARNING    = (0 << 6),    /* no warning */
	LI_PLUSSEC      = (1 << 6),    /* add a second (61 seconds) */
	LI_MINUSSEC     = (2 << 6),    /* minus a second (59 seconds) */
	LI_ALARM        = (3 << 6),    /* alarm condition */

	/* Mode values */
	MODE_RES0       = 0,    /* reserved */
	MODE_SYM_ACT    = 1,    /* symmetric active */
	MODE_SYM_PAS    = 2,    /* symmetric passive */
	MODE_CLIENT     = 3,    /* client */
	MODE_SERVER     = 4,    /* server */
	MODE_BROADCAST  = 5,    /* broadcast */
	MODE_RES1       = 6,    /* reserved for NTP control message */
	MODE_RES2       = 7,    /* reserved for private use */
};

//TODO: better base selection
#define OFFSET_1900_1970 2208988800UL  /* 1970 - 1900 in seconds */

#define NUM_DATAPOINTS  8

typedef struct {
	uint32_t int_partl;
	uint32_t fractionl;
} l_fixedpt_t;

typedef struct {
	uint16_t int_parts;
	uint16_t fractions;
} s_fixedpt_t;

typedef struct {
	uint8_t     m_status;     /* status of local clock and leap info */
	uint8_t     m_stratum;
	uint8_t     m_ppoll;      /* poll value */
	int8_t      m_precision_exp;
	s_fixedpt_t m_rootdelay;
	s_fixedpt_t m_rootdisp;
	uint32_t    m_refid;
	l_fixedpt_t m_reftime;
	l_fixedpt_t m_orgtime;
	l_fixedpt_t m_rectime;
	l_fixedpt_t m_xmttime;
	uint32_t    m_keyid;
	uint8_t     m_digest[NTP_DIGESTSIZE];
} msg_t;

typedef struct {
	double d_offset;
	double d_recv_time;
	double d_dispersion;
} datapoint_t;

typedef struct {
	len_and_sockaddr *p_lsa;
	char             *p_dotted;
	int              p_fd;
	int              datapoint_idx;
	uint32_t         lastpkt_refid;
	uint8_t          lastpkt_status;
	uint8_t          lastpkt_stratum;
	uint8_t          reachable_bits;
	uint8_t          dns_errors;
	/* when to send new query (if p_fd == -1)
	 * or when receive times out (if p_fd >= 0): */
	double           next_action_time;
	double           p_xmttime;
	double           p_raw_delay;
	/* p_raw_delay is set even by "high delay" packets */
	/* lastpkt_delay isn't */
	double           lastpkt_recv_time;
	double           lastpkt_delay;
	double           lastpkt_rootdelay;
	double           lastpkt_rootdisp;
	/* produced by filter algorithm: */
	double           filter_offset;
	double           filter_dispersion;
	double           filter_jitter;
	datapoint_t      filter_datapoint[NUM_DATAPOINTS];
	/* last sent packet: */
	msg_t            p_xmt_msg;
	char             p_hostname[1];
} peer_t;


#define USING_KERNEL_PLL_LOOP          1
#define USING_INITIAL_FREQ_ESTIMATION  0

enum {
	OPT_n = (1 << 0),
	OPT_q = (1 << 1),
	OPT_N = (1 << 2),
	OPT_x = (1 << 3),
	/* Insert new options above this line. */
	/* Non-compat options: */
	OPT_w = (1 << 4),
	OPT_p = (1 << 5),
	OPT_S = (1 << 6),
	OPT_l = (1 << 7) * ENABLE_FEATURE_NTPD_SERVER,
	OPT_I = (1 << 8) * ENABLE_FEATURE_NTPD_SERVER,
	/* We hijack some bits for other purposes */
	OPT_qq = (1 << 31),
};

struct globals {
	double   cur_time;
	/* total round trip delay to currently selected reference clock */
	double   rootdelay;
	/* reference timestamp: time when the system clock was last set or corrected */
	double   reftime;
	/* total dispersion to currently selected reference clock */
	double   rootdisp;

	double   last_script_run;
	char     *script_name;
	llist_t  *ntp_peers;
#if ENABLE_FEATURE_NTPD_SERVER
	int      listen_fd;
	char     *if_name;
# define G_listen_fd (G.listen_fd)
#else
# define G_listen_fd (-1)
#endif
	unsigned verbose;
	unsigned peer_cnt;
	/* refid: 32-bit code identifying the particular server or reference clock
	 * in stratum 0 packets this is a four-character ASCII string,
	 * called the kiss code, used for debugging and monitoring
	 * in stratum 1 packets this is a four-character ASCII string
	 * assigned to the reference clock by IANA. Example: "GPS "
	 * in stratum 2+ packets, it's IPv4 address or 4 first bytes
	 * of MD5 hash of IPv6
	 */
	uint32_t refid;
	uint8_t  ntp_status;
	/* precision is defined as the larger of the resolution and time to
	 * read the clock, in log2 units.  For instance, the precision of a
	 * mains-frequency clock incrementing at 60 Hz is 16 ms, even when the
	 * system clock hardware representation is to the nanosecond.
	 *
	 * Delays, jitters of various kinds are clamped down to precision.
	 *
	 * If precision_sec is too large, discipline_jitter gets clamped to it
	 * and if offset is smaller than discipline_jitter * POLLADJ_GATE, poll
	 * interval grows even though we really can benefit from staying at
	 * smaller one, collecting non-lagged datapoits and correcting offset.
	 * (Lagged datapoits exist when poll_exp is large but we still have
	 * systematic offset error - the time distance between datapoints
	 * is significant and older datapoints have smaller offsets.
	 * This makes our offset estimation a bit smaller than reality)
	 * Due to this effect, setting G_precision_sec close to
	 * STEP_THRESHOLD isn't such a good idea - offsets may grow
	 * too big and we will step. I observed it with -6.
	 *
	 * OTOH, setting precision_sec far too small would result in futile
	 * attempts to synchronize to an unachievable precision.
	 *
	 * -6 is 1/64 sec, -7 is 1/128 sec and so on.
	 * -8 is 1/256 ~= 0.003906 (worked well for me --vda)
	 * -9 is 1/512 ~= 0.001953 (let's try this for some time)
	 */
#define G_precision_exp  -9
	/*
	 * G_precision_exp is used only for construction outgoing packets.
	 * It's ok to set G_precision_sec to a slightly different value
	 * (One which is "nicer looking" in logs).
	 * Exact value would be (1.0 / (1 << (- G_precision_exp))):
	 */
#define G_precision_sec  0.002
	uint8_t  stratum;

#define STATE_NSET      0       /* initial state, "nothing is set" */
//#define STATE_FSET    1       /* frequency set from file */
//#define STATE_SPIK    2       /* spike detected */
//#define STATE_FREQ    3       /* initial frequency */
#define STATE_SYNC      4       /* clock synchronized (normal operation) */
	uint8_t  discipline_state;      // doc calls it c.state
	uint8_t  poll_exp;              // s.poll
	int      polladj_count;         // c.count
	int      FREQHOLD_cnt;
	long     kernel_freq_drift;
	peer_t   *last_update_peer;
	double   last_update_offset;    // c.last
	double   last_update_recv_time; // s.t
	double   discipline_jitter;     // c.jitter
	/* Since we only compare it with ints, can simplify code
	 * by not making this variable floating point:
	 */
	unsigned offset_to_jitter_ratio;
	//double   cluster_offset;        // s.offset
	//double   cluster_jitter;        // s.jitter
#if !USING_KERNEL_PLL_LOOP
	double   discipline_freq_drift; // c.freq
	/* Maybe conditionally calculate wander? it's used only for logging */
	double   discipline_wander;     // c.wander
#endif
};
#define G (*ptr_to_globals)


#define VERB1 if (MAX_VERBOSE && G.verbose)
#define VERB2 if (MAX_VERBOSE >= 2 && G.verbose >= 2)
#define VERB3 if (MAX_VERBOSE >= 3 && G.verbose >= 3)
#define VERB4 if (MAX_VERBOSE >= 4 && G.verbose >= 4)
#define VERB5 if (MAX_VERBOSE >= 5 && G.verbose >= 5)
#define VERB6 if (MAX_VERBOSE >= 6 && G.verbose >= 6)


static double LOG2D(int a)
{
	if (a < 0)
		return 1.0 / (1UL << -a);
	return 1UL << a;
}
static ALWAYS_INLINE double SQUARE(double x)
{
	return x * x;
}
static ALWAYS_INLINE double MAXD(double a, double b)
{
	if (a > b)
		return a;
	return b;
}
static ALWAYS_INLINE double MIND(double a, double b)
{
	if (a < b)
		return a;
	return b;
}
static NOINLINE double my_SQRT(double X)
{
	union {
		float   f;
		int32_t i;
	} v;
	double invsqrt;
	double Xhalf = X * 0.5;

	/* Fast and good approximation to 1/sqrt(X), black magic */
	v.f = X;
	/*v.i = 0x5f3759df - (v.i >> 1);*/
	v.i = 0x5f375a86 - (v.i >> 1); /* - this constant is slightly better */
	invsqrt = v.f; /* better than 0.2% accuracy */

	/* Refining it using Newton's method: x1 = x0 - f(x0)/f'(x0)
	 * f(x) = 1/(x*x) - X  (f==0 when x = 1/sqrt(X))
	 * f'(x) = -2/(x*x*x)
	 * f(x)/f'(x) = (X - 1/(x*x)) / (2/(x*x*x)) = X*x*x*x/2 - x/2
	 * x1 = x0 - (X*x0*x0*x0/2 - x0/2) = 1.5*x0 - X*x0*x0*x0/2 = x0*(1.5 - (X/2)*x0*x0)
	 */
	invsqrt = invsqrt * (1.5 - Xhalf * invsqrt * invsqrt); /* ~0.05% accuracy */
	/* invsqrt = invsqrt * (1.5 - Xhalf * invsqrt * invsqrt); 2nd iter: ~0.0001% accuracy */
	/* With 4 iterations, more than half results will be exact,
	 * at 6th iterations result stabilizes with about 72% results exact.
	 * We are well satisfied with 0.05% accuracy.
	 */

	return X * invsqrt; /* X * 1/sqrt(X) ~= sqrt(X) */
}
static ALWAYS_INLINE double SQRT(double X)
{
	/* If this arch doesn't use IEEE 754 floats, fall back to using libm */
	if (sizeof(float) != 4)
		return sqrt(X);

	/* This avoids needing libm, saves about 0.5k on x86-32 */
	return my_SQRT(X);
}

static double
gettime1900d(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL); /* never fails */
	G.cur_time = tv.tv_sec + (1.0e-6 * tv.tv_usec) + OFFSET_1900_1970;
	return G.cur_time;
}

static void
d_to_tv(double d, struct timeval *tv)
{
	tv->tv_sec = (long)d;
	tv->tv_usec = (d - tv->tv_sec) * 1000000;
}

static double
lfp_to_d(l_fixedpt_t lfp)
{
	double ret;
	lfp.int_partl = ntohl(lfp.int_partl);
	lfp.fractionl = ntohl(lfp.fractionl);
	ret = (double)lfp.int_partl + ((double)lfp.fractionl / UINT_MAX);
	return ret;
}
static double
sfp_to_d(s_fixedpt_t sfp)
{
	double ret;
	sfp.int_parts = ntohs(sfp.int_parts);
	sfp.fractions = ntohs(sfp.fractions);
	ret = (double)sfp.int_parts + ((double)sfp.fractions / USHRT_MAX);
	return ret;
}
#if ENABLE_FEATURE_NTPD_SERVER
static l_fixedpt_t
d_to_lfp(double d)
{
	l_fixedpt_t lfp;
	lfp.int_partl = (uint32_t)d;
	lfp.fractionl = (uint32_t)((d - lfp.int_partl) * UINT_MAX);
	lfp.int_partl = htonl(lfp.int_partl);
	lfp.fractionl = htonl(lfp.fractionl);
	return lfp;
}
static s_fixedpt_t
d_to_sfp(double d)
{
	s_fixedpt_t sfp;
	sfp.int_parts = (uint16_t)d;
	sfp.fractions = (uint16_t)((d - sfp.int_parts) * USHRT_MAX);
	sfp.int_parts = htons(sfp.int_parts);
	sfp.fractions = htons(sfp.fractions);
	return sfp;
}
#endif

static double
dispersion(const datapoint_t *dp)
{
	return dp->d_dispersion + FREQ_TOLERANCE * (G.cur_time - dp->d_recv_time);
}

static double
root_distance(peer_t *p)
{
	/* The root synchronization distance is the maximum error due to
	 * all causes of the local clock relative to the primary server.
	 * It is defined as half the total delay plus total dispersion
	 * plus peer jitter.
	 */
	return MAXD(MINDISP, p->lastpkt_rootdelay + p->lastpkt_delay) / 2
		+ p->lastpkt_rootdisp
		+ p->filter_dispersion
		+ FREQ_TOLERANCE * (G.cur_time - p->lastpkt_recv_time)
		+ p->filter_jitter;
}

static void
set_next(peer_t *p, unsigned t)
{
	p->next_action_time = G.cur_time + t;
}

/*
 * Peer clock filter and its helpers
 */
static void
filter_datapoints(peer_t *p)
{
	int i, idx;
	double sum, wavg;
	datapoint_t *fdp;

#if 0
/* Simulations have shown that use of *averaged* offset for p->filter_offset
 * is in fact worse than simply using last received one: with large poll intervals
 * (>= 2048) averaging code uses offset values which are outdated by hours,
 * and time/frequency correction goes totally wrong when fed essentially bogus offsets.
 */
	int got_newest;
	double minoff, maxoff, w;
	double x = x; /* for compiler */
	double oldest_off = oldest_off;
	double oldest_age = oldest_age;
	double newest_off = newest_off;
	double newest_age = newest_age;

	fdp = p->filter_datapoint;

	minoff = maxoff = fdp[0].d_offset;
	for (i = 1; i < NUM_DATAPOINTS; i++) {
		if (minoff > fdp[i].d_offset)
			minoff = fdp[i].d_offset;
		if (maxoff < fdp[i].d_offset)
			maxoff = fdp[i].d_offset;
	}

	idx = p->datapoint_idx; /* most recent datapoint's index */
	/* Average offset:
	 * Drop two outliers and take weighted average of the rest:
	 * most_recent/2 + older1/4 + older2/8 ... + older5/32 + older6/32
	 * we use older6/32, not older6/64 since sum of weights should be 1:
	 * 1/2 + 1/4 + 1/8 + 1/16 + 1/32 + 1/32 = 1
	 */
	wavg = 0;
	w = 0.5;
	/*                     n-1
	 *                     ---    dispersion(i)
	 * filter_dispersion =  \     -------------
	 *                      /       (i+1)
	 *                     ---     2
	 *                     i=0
	 */
	got_newest = 0;
	sum = 0;
	for (i = 0; i < NUM_DATAPOINTS; i++) {
		VERB5 {
			bb_error_msg("datapoint[%d]: off:%f disp:%f(%f) age:%f%s",
				i,
				fdp[idx].d_offset,
				fdp[idx].d_dispersion, dispersion(&fdp[idx]),
				G.cur_time - fdp[idx].d_recv_time,
				(minoff == fdp[idx].d_offset || maxoff == fdp[idx].d_offset)
					? " (outlier by offset)" : ""
			);
		}

		sum += dispersion(&fdp[idx]) / (2 << i);

		if (minoff == fdp[idx].d_offset) {
			minoff -= 1; /* so that we don't match it ever again */
		} else
		if (maxoff == fdp[idx].d_offset) {
			maxoff += 1;
		} else {
			oldest_off = fdp[idx].d_offset;
			oldest_age = G.cur_time - fdp[idx].d_recv_time;
			if (!got_newest) {
				got_newest = 1;
				newest_off = oldest_off;
				newest_age = oldest_age;
			}
			x = oldest_off * w;
			wavg += x;
			w /= 2;
		}

		idx = (idx - 1) & (NUM_DATAPOINTS - 1);
	}
	p->filter_dispersion = sum;
	wavg += x; /* add another older6/64 to form older6/32 */
	/* Fix systematic underestimation with large poll intervals.
	 * Imagine that we still have a bit of uncorrected drift,
	 * and poll interval is big (say, 100 sec). Offsets form a progression:
	 * 0.0 0.1 0.2 0.3 0.4 0.5 0.6 0.7 - 0.7 is most recent.
	 * The algorithm above drops 0.0 and 0.7 as outliers,
	 * and then we have this estimation, ~25% off from 0.7:
	 * 0.1/32 + 0.2/32 + 0.3/16 + 0.4/8 + 0.5/4 + 0.6/2 = 0.503125
	 */
	x = oldest_age - newest_age;
	if (x != 0) {
		x = newest_age / x; /* in above example, 100 / (600 - 100) */
		if (x < 1) { /* paranoia check */
			x = (newest_off - oldest_off) * x; /* 0.5 * 100/500 = 0.1 */
			wavg += x;
		}
	}
	p->filter_offset = wavg;

#else

	fdp = p->filter_datapoint;
	idx = p->datapoint_idx; /* most recent datapoint's index */

	/* filter_offset: simply use the most recent value */
	p->filter_offset = fdp[idx].d_offset;

	/*                     n-1
	 *                     ---    dispersion(i)
	 * filter_dispersion =  \     -------------
	 *                      /       (i+1)
	 *                     ---     2
	 *                     i=0
	 */
	wavg = 0;
	sum = 0;
	for (i = 0; i < NUM_DATAPOINTS; i++) {
		sum += dispersion(&fdp[idx]) / (2 << i);
		wavg += fdp[idx].d_offset;
		idx = (idx - 1) & (NUM_DATAPOINTS - 1);
	}
	wavg /= NUM_DATAPOINTS;
	p->filter_dispersion = sum;
#endif

	/*                  +-----                 -----+ ^ 1/2
	 *                  |       n-1                 |
	 *                  |       ---                 |
	 *                  |  1    \                2  |
	 * filter_jitter =  | --- * /  (avg-offset_j)   |
	 *                  |  n    ---                 |
	 *                  |       j=0                 |
	 *                  +-----                 -----+
	 * where n is the number of valid datapoints in the filter (n > 1);
	 * if filter_jitter < precision then filter_jitter = precision
	 */
	sum = 0;
	for (i = 0; i < NUM_DATAPOINTS; i++) {
		sum += SQUARE(wavg - fdp[i].d_offset);
	}
	sum = SQRT(sum / NUM_DATAPOINTS);
	p->filter_jitter = sum > G_precision_sec ? sum : G_precision_sec;

	VERB4 bb_error_msg("filter offset:%+f disp:%f jitter:%f",
			p->filter_offset,
			p->filter_dispersion,
			p->filter_jitter);
}

static void
reset_peer_stats(peer_t *p, double offset)
{
	int i;
	bool small_ofs = fabs(offset) < STEP_THRESHOLD;

	/* Used to set p->filter_datapoint[i].d_dispersion = MAXDISP
	 * and clear reachable bits, but this proved to be too aggressive:
	 * after step (tested with suspending laptop for ~30 secs),
	 * this caused all previous data to be considered invalid,
	 * making us needing to collect full ~8 datapoints per peer
	 * after step in order to start trusting them.
	 * In turn, this was making poll interval decrease even after
	 * step was done. (Poll interval decreases already before step
	 * in this scenario, because we see large offsets and end up with
	 * no good peer to select).
	 */

	for (i = 0; i < NUM_DATAPOINTS; i++) {
		if (small_ofs) {
			p->filter_datapoint[i].d_recv_time += offset;
			if (p->filter_datapoint[i].d_offset != 0) {
				p->filter_datapoint[i].d_offset -= offset;
				//bb_error_msg("p->filter_datapoint[%d].d_offset %f -> %f",
				//	i,
				//	p->filter_datapoint[i].d_offset + offset,
				//	p->filter_datapoint[i].d_offset);
			}
		} else {
			p->filter_datapoint[i].d_recv_time  = G.cur_time;
			p->filter_datapoint[i].d_offset     = 0;
			/*p->filter_datapoint[i].d_dispersion = MAXDISP;*/
		}
	}
	if (small_ofs) {
		p->lastpkt_recv_time += offset;
	} else {
		/*p->reachable_bits = 0;*/
		p->lastpkt_recv_time = G.cur_time;
	}
	filter_datapoints(p); /* recalc p->filter_xxx */
	VERB6 bb_error_msg("%s->lastpkt_recv_time=%f", p->p_dotted, p->lastpkt_recv_time);
}

static len_and_sockaddr*
resolve_peer_hostname(peer_t *p)
{
	len_and_sockaddr *lsa = host2sockaddr(p->p_hostname, 123);
	if (lsa) {
		free(p->p_lsa);
		free(p->p_dotted);
		p->p_lsa = lsa;
		p->p_dotted = xmalloc_sockaddr2dotted_noport(&lsa->u.sa);
		VERB1 if (strcmp(p->p_hostname, p->p_dotted) != 0)
			bb_error_msg("'%s' is %s", p->p_hostname, p->p_dotted);
		p->dns_errors = 0;
		return lsa;
	}
	p->dns_errors = ((p->dns_errors << 1) | 1) & DNS_ERRORS_CAP;
	return lsa;
}

static void
add_peers(const char *s)
{
	llist_t *item;
	peer_t *p;

	p = xzalloc(sizeof(*p) + strlen(s));
	strcpy(p->p_hostname, s);
	p->p_fd = -1;
	p->p_xmt_msg.m_status = MODE_CLIENT | (NTP_VERSION << 3);
	p->next_action_time = G.cur_time; /* = set_next(p, 0); */
	reset_peer_stats(p, STEP_THRESHOLD);

	/* Names like N.<country2chars>.pool.ntp.org are randomly resolved
	 * to a pool of machines. Sometimes different N's resolve to the same IP.
	 * It is not useful to have two peers with same IP. We skip duplicates.
	 */
	if (resolve_peer_hostname(p)) {
		for (item = G.ntp_peers; item != NULL; item = item->link) {
			peer_t *pp = (peer_t *) item->data;
			if (pp->p_dotted && strcmp(p->p_dotted, pp->p_dotted) == 0) {
				bb_error_msg("duplicate peer %s (%s)", s, p->p_dotted);
				free(p->p_lsa);
				free(p->p_dotted);
				free(p);
				return;
			}
		}
	}

	llist_add_to(&G.ntp_peers, p);
	G.peer_cnt++;
}

static int
do_sendto(int fd,
		const struct sockaddr *from, const struct sockaddr *to, socklen_t addrlen,
		msg_t *msg, ssize_t len)
{
	ssize_t ret;

	errno = 0;
	if (!from) {
		ret = sendto(fd, msg, len, MSG_DONTWAIT, to, addrlen);
	} else {
		ret = send_to_from(fd, msg, len, MSG_DONTWAIT, to, from, addrlen);
	}
	if (ret != len) {
		bb_perror_msg("send failed");
		return -1;
	}
	return 0;
}

static void
send_query_to_peer(peer_t *p)
{
	if (!p->p_lsa)
		return;

	/* Why do we need to bind()?
	 * See what happens when we don't bind:
	 *
	 * socket(PF_INET, SOCK_DGRAM, IPPROTO_IP) = 3
	 * setsockopt(3, SOL_IP, IP_TOS, [16], 4) = 0
	 * gettimeofday({1259071266, 327885}, NULL) = 0
	 * sendto(3, "xxx", 48, MSG_DONTWAIT, {sa_family=AF_INET, sin_port=htons(123), sin_addr=inet_addr("10.34.32.125")}, 16) = 48
	 * ^^^ we sent it from some source port picked by kernel.
	 * time(NULL)              = 1259071266
	 * write(2, "ntpd: entering poll 15 secs\n", 28) = 28
	 * poll([{fd=3, events=POLLIN}], 1, 15000) = 1 ([{fd=3, revents=POLLIN}])
	 * recv(3, "yyy", 68, MSG_DONTWAIT) = 48
	 * ^^^ this recv will receive packets to any local port!
	 *
	 * Uncomment this and use strace to see it in action:
	 */
#define PROBE_LOCAL_ADDR /* { len_and_sockaddr lsa; lsa.len = LSA_SIZEOF_SA; getsockname(p->query.fd, &lsa.u.sa, &lsa.len); } */

	if (p->p_fd == -1) {
		int fd, family;
		len_and_sockaddr *local_lsa;

		family = p->p_lsa->u.sa.sa_family;
		p->p_fd = fd = xsocket_type(&local_lsa, family, SOCK_DGRAM);
		/* local_lsa has "null" address and port 0 now.
		 * bind() ensures we have a *particular port* selected by kernel
		 * and remembered in p->p_fd, thus later recv(p->p_fd)
		 * receives only packets sent to this port.
		 */
		PROBE_LOCAL_ADDR
		xbind(fd, &local_lsa->u.sa, local_lsa->len);
		PROBE_LOCAL_ADDR
#if ENABLE_FEATURE_IPV6
		if (family == AF_INET)
#endif
			setsockopt_int(fd, IPPROTO_IP, IP_TOS, IPTOS_DSCP_AF21);
		free(local_lsa);
	}

	/* Emit message _before_ attempted send. Think of a very short
	 * roundtrip networks: we need to go back to recv loop ASAP,
	 * to reduce delay. Printing messages after send works against that.
	 */
	VERB1 bb_error_msg("sending query to %s", p->p_dotted);

	/*
	 * Send out a random 64-bit number as our transmit time.  The NTP
	 * server will copy said number into the originate field on the
	 * response that it sends us.  This is totally legal per the SNTP spec.
	 *
	 * The impact of this is two fold: we no longer send out the current
	 * system time for the world to see (which may aid an attacker), and
	 * it gives us a (not very secure) way of knowing that we're not
	 * getting spoofed by an attacker that can't capture our traffic
	 * but can spoof packets from the NTP server we're communicating with.
	 *
	 * Save the real transmit timestamp locally.
	 */
	p->p_xmt_msg.m_xmttime.int_partl = rand();
	p->p_xmt_msg.m_xmttime.fractionl = rand();
	p->p_xmttime = gettime1900d();

	/* Were doing it only if sendto worked, but
	 * loss of sync detection needs reachable_bits updated
	 * even if sending fails *locally*:
	 * "network is unreachable" because cable was pulled?
	 * We still need to declare "unsync" if this condition persists.
	 */
	p->reachable_bits <<= 1;

	if (do_sendto(p->p_fd, /*from:*/ NULL, /*to:*/ &p->p_lsa->u.sa, /*addrlen:*/ p->p_lsa->len,
			&p->p_xmt_msg, NTP_MSGSIZE_NOAUTH) == -1
	) {
		close(p->p_fd);
		p->p_fd = -1;
		/*
		 * We know that we sent nothing.
		 * We can retry *soon* without fearing
		 * that we are flooding the peer.
		 */
		set_next(p, RETRY_INTERVAL);
		return;
	}

	set_next(p, RESPONSE_INTERVAL);
}


/* Note that there is no provision to prevent several run_scripts
 * to be started in quick succession. In fact, it happens rather often
 * if initial syncronization results in a step.
 * You will see "step" and then "stratum" script runs, sometimes
 * as close as only 0.002 seconds apart.
 * Script should be ready to deal with this.
 */
static void run_script(const char *action, double offset)
{
	char *argv[3];
	char *env1, *env2, *env3, *env4;

	G.last_script_run = G.cur_time;

	if (!G.script_name)
		return;

	argv[0] = (char*) G.script_name;
	argv[1] = (char*) action;
	argv[2] = NULL;

	VERB1 bb_error_msg("executing '%s %s'", G.script_name, action);

	env1 = xasprintf("%s=%u", "stratum", G.stratum);
	putenv(env1);
	env2 = xasprintf("%s=%ld", "freq_drift_ppm", G.kernel_freq_drift);
	putenv(env2);
	env3 = xasprintf("%s=%u", "poll_interval", 1 << G.poll_exp);
	putenv(env3);
	env4 = xasprintf("%s=%f", "offset", offset);
	putenv(env4);
	/* Other items of potential interest: selected peer,
	 * rootdelay, reftime, rootdisp, refid, ntp_status,
	 * last_update_offset, last_update_recv_time, discipline_jitter,
	 * how many peers have reachable_bits = 0?
	 */

	/* Don't want to wait: it may run hwclock --systohc, and that
	 * may take some time (seconds): */
	/*spawn_and_wait(argv);*/
	spawn(argv);

	unsetenv("stratum");
	unsetenv("freq_drift_ppm");
	unsetenv("poll_interval");
	unsetenv("offset");
	free(env1);
	free(env2);
	free(env3);
	free(env4);
}

static NOINLINE void
step_time(double offset)
{
	llist_t *item;
	double dtime;
	struct timeval tvc, tvn;
	char buf[sizeof("yyyy-mm-dd hh:mm:ss") + /*paranoia:*/ 4];
	time_t tval;

	gettimeofday(&tvc, NULL); /* never fails */
	dtime = tvc.tv_sec + (1.0e-6 * tvc.tv_usec) + offset;
	d_to_tv(dtime, &tvn);
	if (settimeofday(&tvn, NULL) == -1)
		bb_perror_msg_and_die("settimeofday");

	VERB2 {
		tval = tvc.tv_sec;
		strftime_YYYYMMDDHHMMSS(buf, sizeof(buf), &tval);
		bb_error_msg("current time is %s.%06u", buf, (unsigned)tvc.tv_usec);
	}
	tval = tvn.tv_sec;
	strftime_YYYYMMDDHHMMSS(buf, sizeof(buf), &tval);
	bb_error_msg("setting time to %s.%06u (offset %+fs)", buf, (unsigned)tvn.tv_usec, offset);
	//maybe? G.FREQHOLD_cnt = 0;

	/* Correct various fields which contain time-relative values: */

	/* Globals: */
	G.cur_time += offset;
	G.last_update_recv_time += offset;
	G.last_script_run += offset;

	/* p->lastpkt_recv_time, p->next_action_time and such: */
	for (item = G.ntp_peers; item != NULL; item = item->link) {
		peer_t *pp = (peer_t *) item->data;
		reset_peer_stats(pp, offset);
		//bb_error_msg("offset:%+f pp->next_action_time:%f -> %f",
		//	offset, pp->next_action_time, pp->next_action_time + offset);
		pp->next_action_time += offset;
		if (pp->p_fd >= 0) {
			/* We wait for reply from this peer too.
			 * But due to step we are doing, reply's data is no longer
			 * useful (in fact, it'll be bogus). Stop waiting for it.
			 */
			close(pp->p_fd);
			pp->p_fd = -1;
			set_next(pp, RETRY_INTERVAL);
		}
	}
}

static void clamp_pollexp_and_set_MAXSTRAT(void)
{
	if (G.poll_exp < MINPOLL)
		G.poll_exp = MINPOLL;
	if (G.poll_exp > BIGPOLL)
		G.poll_exp = BIGPOLL;
	G.polladj_count = 0;
	G.stratum = MAXSTRAT;
}


/*
 * Selection and clustering, and their helpers
 */
typedef struct {
	peer_t *p;
	int    type;
	double edge;
	double opt_rd; /* optimization */
} point_t;
static int
compare_point_edge(const void *aa, const void *bb)
{
	const point_t *a = aa;
	const point_t *b = bb;
	if (a->edge < b->edge) {
		return -1;
	}
	return (a->edge > b->edge);
}
typedef struct {
	peer_t *p;
	double metric;
} survivor_t;
static int
compare_survivor_metric(const void *aa, const void *bb)
{
	const survivor_t *a = aa;
	const survivor_t *b = bb;
	if (a->metric < b->metric) {
		return -1;
	}
	return (a->metric > b->metric);
}
static int
fit(peer_t *p, double rd)
{
	if ((p->reachable_bits & (p->reachable_bits-1)) == 0) {
		/* One or zero bits in reachable_bits */
		VERB4 bb_error_msg("peer %s unfit for selection: unreachable", p->p_dotted);
		return 0;
	}
#if 0 /* we filter out such packets earlier */
	if ((p->lastpkt_status & LI_ALARM) == LI_ALARM
	 || p->lastpkt_stratum >= MAXSTRAT
	) {
		VERB4 bb_error_msg("peer %s unfit for selection: bad status/stratum", p->p_dotted);
		return 0;
	}
#endif
	/* rd is root_distance(p) */
	if (rd > MAXDIST + FREQ_TOLERANCE * (1 << G.poll_exp)) {
		VERB4 bb_error_msg("peer %s unfit for selection: root distance too high", p->p_dotted);
		return 0;
	}
//TODO
//	/* Do we have a loop? */
//	if (p->refid == p->dstaddr || p->refid == s.refid)
//		return 0;
	return 1;
}
static peer_t*
select_and_cluster(void)
{
	peer_t     *p;
	llist_t    *item;
	int        i, j;
	int        size = 3 * G.peer_cnt;
	/* for selection algorithm */
	point_t    point[size];
	unsigned   num_points, num_candidates;
	double     low, high;
	unsigned   num_falsetickers;
	/* for cluster algorithm */
	survivor_t survivor[size];
	unsigned   num_survivors;

	/* Selection */

	num_points = 0;
	item = G.ntp_peers;
	while (item != NULL) {
		double rd, offset;

		p = (peer_t *) item->data;
		rd = root_distance(p);
		offset = p->filter_offset;
		if (!fit(p, rd)) {
			item = item->link;
			continue;
		}

		VERB5 bb_error_msg("interval: [%f %f %f] %s",
				offset - rd,
				offset,
				offset + rd,
				p->p_dotted
		);
		point[num_points].p = p;
		point[num_points].type = -1;
		point[num_points].edge = offset - rd;
		point[num_points].opt_rd = rd;
		num_points++;
		point[num_points].p = p;
		point[num_points].type = 0;
		point[num_points].edge = offset;
		point[num_points].opt_rd = rd;
		num_points++;
		point[num_points].p = p;
		point[num_points].type = 1;
		point[num_points].edge = offset + rd;
		point[num_points].opt_rd = rd;
		num_points++;
		item = item->link;
	}
	num_candidates = num_points / 3;
	if (num_candidates == 0) {
		VERB3 bb_error_msg("no valid datapoints%s", ", no peer selected");
		return NULL;
	}
//TODO: sorting does not seem to be done in reference code
	qsort(point, num_points, sizeof(point[0]), compare_point_edge);

	/* Start with the assumption that there are no falsetickers.
	 * Attempt to find a nonempty intersection interval containing
	 * the midpoints of all truechimers.
	 * If a nonempty interval cannot be found, increase the number
	 * of assumed falsetickers by one and try again.
	 * If a nonempty interval is found and the number of falsetickers
	 * is less than the number of truechimers, a majority has been found
	 * and the midpoint of each truechimer represents
	 * the candidates available to the cluster algorithm.
	 */
	num_falsetickers = 0;
	while (1) {
		int c;
		unsigned num_midpoints = 0;

		low = 1 << 9;
		high = - (1 << 9);
		c = 0;
		for (i = 0; i < num_points; i++) {
			/* We want to do:
			 * if (point[i].type == -1) c++;
			 * if (point[i].type == 1) c--;
			 * and it's simpler to do it this way:
			 */
			c -= point[i].type;
			if (c >= num_candidates - num_falsetickers) {
				/* If it was c++ and it got big enough... */
				low = point[i].edge;
				break;
			}
			if (point[i].type == 0)
				num_midpoints++;
		}
		c = 0;
		for (i = num_points-1; i >= 0; i--) {
			c += point[i].type;
			if (c >= num_candidates - num_falsetickers) {
				high = point[i].edge;
				break;
			}
			if (point[i].type == 0)
				num_midpoints++;
		}
		/* If the number of midpoints is greater than the number
		 * of allowed falsetickers, the intersection contains at
		 * least one truechimer with no midpoint - bad.
		 * Also, interval should be nonempty.
		 */
		if (num_midpoints <= num_falsetickers && low < high)
			break;
		num_falsetickers++;
		if (num_falsetickers * 2 >= num_candidates) {
			VERB3 bb_error_msg("falsetickers:%d, candidates:%d%s",
					num_falsetickers, num_candidates,
					", no peer selected");
			return NULL;
		}
	}
	VERB4 bb_error_msg("selected interval: [%f, %f]; candidates:%d falsetickers:%d",
			low, high, num_candidates, num_falsetickers);

	/* Clustering */

	/* Construct a list of survivors (p, metric)
	 * from the chime list, where metric is dominated
	 * first by stratum and then by root distance.
	 * All other things being equal, this is the order of preference.
	 */
	num_survivors = 0;
	for (i = 0; i < num_points; i++) {
		if (point[i].edge < low || point[i].edge > high)
			continue;
		p = point[i].p;
		survivor[num_survivors].p = p;
		/* x.opt_rd == root_distance(p); */
		survivor[num_survivors].metric = MAXDIST * p->lastpkt_stratum + point[i].opt_rd;
		VERB5 bb_error_msg("survivor[%d] metric:%f peer:%s",
			num_survivors, survivor[num_survivors].metric, p->p_dotted);
		num_survivors++;
	}
	/* There must be at least MIN_SELECTED survivors to satisfy the
	 * correctness assertions. Ordinarily, the Byzantine criteria
	 * require four survivors, but for the demonstration here, one
	 * is acceptable.
	 */
	if (num_survivors < MIN_SELECTED) {
		VERB3 bb_error_msg("survivors:%d%s",
				num_survivors,
				", no peer selected");
		return NULL;
	}

//looks like this is ONLY used by the fact that later we pick survivor[0].
//we can avoid sorting then, just find the minimum once!
	qsort(survivor, num_survivors, sizeof(survivor[0]), compare_survivor_metric);

	/* For each association p in turn, calculate the selection
	 * jitter p->sjitter as the square root of the sum of squares
	 * (p->offset - q->offset) over all q associations. The idea is
	 * to repeatedly discard the survivor with maximum selection
	 * jitter until a termination condition is met.
	 */
	while (1) {
		unsigned max_idx = max_idx;
		double max_selection_jitter = max_selection_jitter;
		double min_jitter = min_jitter;

		if (num_survivors <= MIN_CLUSTERED) {
			VERB4 bb_error_msg("num_survivors %d <= %d, not discarding more",
					num_survivors, MIN_CLUSTERED);
			break;
		}

		/* To make sure a few survivors are left
		 * for the clustering algorithm to chew on,
		 * we stop if the number of survivors
		 * is less than or equal to MIN_CLUSTERED (3).
		 */
		for (i = 0; i < num_survivors; i++) {
			double selection_jitter_sq;

			p = survivor[i].p;
			if (i == 0 || p->filter_jitter < min_jitter)
				min_jitter = p->filter_jitter;

			selection_jitter_sq = 0;
			for (j = 0; j < num_survivors; j++) {
				peer_t *q = survivor[j].p;
				selection_jitter_sq += SQUARE(p->filter_offset - q->filter_offset);
			}
			if (i == 0 || selection_jitter_sq > max_selection_jitter) {
				max_selection_jitter = selection_jitter_sq;
				max_idx = i;
			}
			VERB6 bb_error_msg("survivor %d selection_jitter^2:%f",
					i, selection_jitter_sq);
		}
		max_selection_jitter = SQRT(max_selection_jitter / num_survivors);
		VERB5 bb_error_msg("max_selection_jitter (at %d):%f min_jitter:%f",
				max_idx, max_selection_jitter, min_jitter);

		/* If the maximum selection jitter is less than the
		 * minimum peer jitter, then tossing out more survivors
		 * will not lower the minimum peer jitter, so we might
		 * as well stop.
		 */
		if (max_selection_jitter < min_jitter) {
			VERB4 bb_error_msg("max_selection_jitter:%f < min_jitter:%f, num_survivors:%d, not discarding more",
					max_selection_jitter, min_jitter, num_survivors);
			break;
		}

		/* Delete survivor[max_idx] from the list
		 * and go around again.
		 */
		VERB6 bb_error_msg("dropping survivor %d", max_idx);
		num_survivors--;
		while (max_idx < num_survivors) {
			survivor[max_idx] = survivor[max_idx + 1];
			max_idx++;
		}
	}

	if (0) {
		/* Combine the offsets of the clustering algorithm survivors
		 * using a weighted average with weight determined by the root
		 * distance. Compute the selection jitter as the weighted RMS
		 * difference between the first survivor and the remaining
		 * survivors. In some cases the inherent clock jitter can be
		 * reduced by not using this algorithm, especially when frequent
		 * clockhopping is involved. bbox: thus we don't do it.
		 */
		double x, y, z, w;
		y = z = w = 0;
		for (i = 0; i < num_survivors; i++) {
			p = survivor[i].p;
			x = root_distance(p);
			y += 1 / x;
			z += p->filter_offset / x;
			w += SQUARE(p->filter_offset - survivor[0].p->filter_offset) / x;
		}
		//G.cluster_offset = z / y;
		//G.cluster_jitter = SQRT(w / y);
	}

	/* Pick the best clock. If the old system peer is on the list
	 * and at the same stratum as the first survivor on the list,
	 * then don't do a clock hop. Otherwise, select the first
	 * survivor on the list as the new system peer.
	 */
	p = survivor[0].p;
	if (G.last_update_peer
	 && G.last_update_peer->lastpkt_stratum <= p->lastpkt_stratum
	) {
		/* Starting from 1 is ok here */
		for (i = 1; i < num_survivors; i++) {
			if (G.last_update_peer == survivor[i].p) {
				VERB5 bb_error_msg("keeping old synced peer");
				p = G.last_update_peer;
				goto keep_old;
			}
		}
	}
	G.last_update_peer = p;
 keep_old:
	VERB4 bb_error_msg("selected peer %s filter_offset:%+f age:%f",
			p->p_dotted,
			p->filter_offset,
			G.cur_time - p->lastpkt_recv_time
	);
	return p;
}


/*
 * Local clock discipline and its helpers
 */
static void
set_new_values(int disc_state, double offset, double recv_time)
{
	/* Enter new state and set state variables. Note we use the time
	 * of the last clock filter sample, which must be earlier than
	 * the current time.
	 */
	VERB4 bb_error_msg("disc_state=%d last update offset=%f recv_time=%f",
			disc_state, offset, recv_time);
	G.discipline_state = disc_state;
	G.last_update_offset = offset;
	G.last_update_recv_time = recv_time;
}
/* Return: -1: decrease poll interval, 0: leave as is, 1: increase */
static NOINLINE int
update_local_clock(peer_t *p)
{
	int rc;
	struct timex tmx;
	/* Note: can use G.cluster_offset instead: */
	double offset = p->filter_offset;
	double recv_time = p->lastpkt_recv_time;
	double abs_offset;
#if !USING_KERNEL_PLL_LOOP
	double freq_drift;
#endif
#if !USING_KERNEL_PLL_LOOP || USING_INITIAL_FREQ_ESTIMATION
	double since_last_update;
#endif
	double etemp, dtemp;

	abs_offset = fabs(offset);

#if 0
	/* If needed, -S script can do it by looking at $offset
	 * env var and killing parent */
	/* If the offset is too large, give up and go home */
	if (abs_offset > PANIC_THRESHOLD) {
		bb_error_msg_and_die("offset %f far too big, exiting", offset);
	}
#endif

	/* If this is an old update, for instance as the result
	 * of a system peer change, avoid it. We never use
	 * an old sample or the same sample twice.
	 */
	if (recv_time <= G.last_update_recv_time) {
		VERB3 bb_error_msg("update from %s: same or older datapoint, not using it",
			p->p_dotted);
		return 0; /* "leave poll interval as is" */
	}

	/* Clock state machine transition function. This is where the
	 * action is and defines how the system reacts to large time
	 * and frequency errors.
	 */
#if !USING_KERNEL_PLL_LOOP || USING_INITIAL_FREQ_ESTIMATION
	since_last_update = recv_time - G.reftime;
#endif
#if !USING_KERNEL_PLL_LOOP
	freq_drift = 0;
#endif
#if USING_INITIAL_FREQ_ESTIMATION
	if (G.discipline_state == STATE_FREQ) {
		/* Ignore updates until the stepout threshold */
		if (since_last_update < WATCH_THRESHOLD) {
			VERB4 bb_error_msg("measuring drift, datapoint ignored, %f sec remains",
					WATCH_THRESHOLD - since_last_update);
			return 0; /* "leave poll interval as is" */
		}
# if !USING_KERNEL_PLL_LOOP
		freq_drift = (offset - G.last_update_offset) / since_last_update;
# endif
	}
#endif

	/* There are two main regimes: when the
	 * offset exceeds the step threshold and when it does not.
	 */
	if (abs_offset > STEP_THRESHOLD) {
#if 0
		double remains;

// This "spike state" seems to be useless, peer selection already drops
// occassional "bad" datapoints. If we are here, there were _many_
// large offsets. When a few first large offsets are seen,
// we end up in "no valid datapoints, no peer selected" state.
// Only when enough of them are seen (which means it's not a fluke),
// we end up here. Looks like _our_ clock is off.
		switch (G.discipline_state) {
		case STATE_SYNC:
			/* The first outlyer: ignore it, switch to SPIK state */
			VERB3 bb_error_msg("update from %s: offset:%+f, spike%s",
				p->p_dotted, offset,
				"");
			G.discipline_state = STATE_SPIK;
			return -1; /* "decrease poll interval" */

		case STATE_SPIK:
			/* Ignore succeeding outlyers until either an inlyer
			 * is found or the stepout threshold is exceeded.
			 */
			remains = WATCH_THRESHOLD - since_last_update;
			if (remains > 0) {
				VERB3 bb_error_msg("update from %s: offset:%+f, spike%s",
					p->p_dotted, offset,
					", datapoint ignored");
				return -1; /* "decrease poll interval" */
			}
			/* fall through: we need to step */
		} /* switch */
#endif

		/* Step the time and clamp down the poll interval.
		 *
		 * In NSET state an initial frequency correction is
		 * not available, usually because the frequency file has
		 * not yet been written. Since the time is outside the
		 * capture range, the clock is stepped. The frequency
		 * will be set directly following the stepout interval.
		 *
		 * In FSET state the initial frequency has been set
		 * from the frequency file. Since the time is outside
		 * the capture range, the clock is stepped immediately,
		 * rather than after the stepout interval. Guys get
		 * nervous if it takes 17 minutes to set the clock for
		 * the first time.
		 *
		 * In SPIK state the stepout threshold has expired and
		 * the phase is still above the step threshold. Note
		 * that a single spike greater than the step threshold
		 * is always suppressed, even at the longer poll
		 * intervals.
		 */
		VERB4 bb_error_msg("stepping time by %+f; poll_exp=MINPOLL", offset);
		step_time(offset);
		if (option_mask32 & OPT_q) {
			/* We were only asked to set time once. Done. */
			exit(0);
		}

		clamp_pollexp_and_set_MAXSTRAT();

		run_script("step", offset);

		recv_time += offset;

#if USING_INITIAL_FREQ_ESTIMATION
		if (G.discipline_state == STATE_NSET) {
			set_new_values(STATE_FREQ, /*offset:*/ 0, recv_time);
			return 1; /* "ok to increase poll interval" */
		}
#endif
		abs_offset = offset = 0;
		set_new_values(STATE_SYNC, offset, recv_time);
	} else { /* abs_offset <= STEP_THRESHOLD */

		/* The ratio is calculated before jitter is updated to make
		 * poll adjust code more sensitive to large offsets.
		 */
		G.offset_to_jitter_ratio = abs_offset / G.discipline_jitter;

		/* Compute the clock jitter as the RMS of exponentially
		 * weighted offset differences. Used by the poll adjust code.
		 */
		etemp = SQUARE(G.discipline_jitter);
		dtemp = SQUARE(offset - G.last_update_offset);
		G.discipline_jitter = SQRT(etemp + (dtemp - etemp) / AVG);
		if (G.discipline_jitter < G_precision_sec)
			G.discipline_jitter = G_precision_sec;

		switch (G.discipline_state) {
		case STATE_NSET:
			if (option_mask32 & OPT_q) {
				/* We were only asked to set time once.
				 * The clock is precise enough, no need to step.
				 */
				exit(0);
			}
#if USING_INITIAL_FREQ_ESTIMATION
			/* This is the first update received and the frequency
			 * has not been initialized. The first thing to do
			 * is directly measure the oscillator frequency.
			 */
			set_new_values(STATE_FREQ, offset, recv_time);
#else
			set_new_values(STATE_SYNC, offset, recv_time);
#endif
			VERB4 bb_error_msg("transitioning to FREQ, datapoint ignored");
			return 0; /* "leave poll interval as is" */

#if 0 /* this is dead code for now */
		case STATE_FSET:
			/* This is the first update and the frequency
			 * has been initialized. Adjust the phase, but
			 * don't adjust the frequency until the next update.
			 */
			set_new_values(STATE_SYNC, offset, recv_time);
			/* freq_drift remains 0 */
			break;
#endif

#if USING_INITIAL_FREQ_ESTIMATION
		case STATE_FREQ:
			/* since_last_update >= WATCH_THRESHOLD, we waited enough.
			 * Correct the phase and frequency and switch to SYNC state.
			 * freq_drift was already estimated (see code above)
			 */
			set_new_values(STATE_SYNC, offset, recv_time);
			break;
#endif

		default:
#if !USING_KERNEL_PLL_LOOP
			/* Compute freq_drift due to PLL and FLL contributions.
			 *
			 * The FLL and PLL frequency gain constants
			 * depend on the poll interval and Allan
			 * intercept. The FLL is not used below one-half
			 * the Allan intercept. Above that the loop gain
			 * increases in steps to 1 / AVG.
			 */
			if ((1 << G.poll_exp) > ALLAN / 2) {
				etemp = FLL - G.poll_exp;
				if (etemp < AVG)
					etemp = AVG;
				freq_drift += (offset - G.last_update_offset) / (MAXD(since_last_update, ALLAN) * etemp);
			}
			/* For the PLL the integration interval
			 * (numerator) is the minimum of the update
			 * interval and poll interval. This allows
			 * oversampling, but not undersampling.
			 */
			etemp = MIND(since_last_update, (1 << G.poll_exp));
			dtemp = (4 * PLL) << G.poll_exp;
			freq_drift += offset * etemp / SQUARE(dtemp);
#endif
			set_new_values(STATE_SYNC, offset, recv_time);
			break;
		}
		if (G.stratum != p->lastpkt_stratum + 1) {
			G.stratum = p->lastpkt_stratum + 1;
			run_script("stratum", offset);
		}
	}

	G.reftime = G.cur_time;
	G.ntp_status = p->lastpkt_status;
	G.refid = p->lastpkt_refid;
	G.rootdelay = p->lastpkt_rootdelay + p->lastpkt_delay;
	dtemp = p->filter_jitter; // SQRT(SQUARE(p->filter_jitter) + SQUARE(G.cluster_jitter));
	dtemp += MAXD(p->filter_dispersion + FREQ_TOLERANCE * (G.cur_time - p->lastpkt_recv_time) + abs_offset, MINDISP);
	G.rootdisp = p->lastpkt_rootdisp + dtemp;
	VERB4 bb_error_msg("updating leap/refid/reftime/rootdisp from peer %s", p->p_dotted);

	/* We are in STATE_SYNC now, but did not do adjtimex yet.
	 * (Any other state does not reach this, they all return earlier)
	 * By this time, freq_drift and offset are set
	 * to values suitable for adjtimex.
	 */
#if !USING_KERNEL_PLL_LOOP
	/* Calculate the new frequency drift and frequency stability (wander).
	 * Compute the clock wander as the RMS of exponentially weighted
	 * frequency differences. This is not used directly, but can,
	 * along with the jitter, be a highly useful monitoring and
	 * debugging tool.
	 */
	dtemp = G.discipline_freq_drift + freq_drift;
	G.discipline_freq_drift = MAXD(MIND(MAXDRIFT, dtemp), -MAXDRIFT);
	etemp = SQUARE(G.discipline_wander);
	dtemp = SQUARE(dtemp);
	G.discipline_wander = SQRT(etemp + (dtemp - etemp) / AVG);

	VERB4 bb_error_msg("discipline freq_drift=%.9f(int:%ld corr:%e) wander=%f",
			G.discipline_freq_drift,
			(long)(G.discipline_freq_drift * 65536e6),
			freq_drift,
			G.discipline_wander);
#endif
	VERB4 {
		memset(&tmx, 0, sizeof(tmx));
		if (adjtimex(&tmx) < 0)
			bb_perror_msg_and_die("adjtimex");
		bb_error_msg("p adjtimex freq:%ld offset:%+ld status:0x%x tc:%ld",
				tmx.freq, tmx.offset, tmx.status, tmx.constant);
	}

	memset(&tmx, 0, sizeof(tmx));
#if 0
//doesn't work, offset remains 0 (!) in kernel:
//ntpd:  set adjtimex freq:1786097 tmx.offset:77487
//ntpd: prev adjtimex freq:1786097 tmx.offset:0
//ntpd:  cur adjtimex freq:1786097 tmx.offset:0
	tmx.modes = ADJ_FREQUENCY | ADJ_OFFSET;
	/* 65536 is one ppm */
	tmx.freq = G.discipline_freq_drift * 65536e6;
#endif
	tmx.modes = ADJ_OFFSET | ADJ_STATUS | ADJ_TIMECONST;// | ADJ_MAXERROR | ADJ_ESTERROR;

	tmx.offset = (long)(offset * 1000000); /* usec */
	if (SLEW_THRESHOLD < STEP_THRESHOLD) {
		if (tmx.offset > (long)(SLEW_THRESHOLD * 1000000)) {
			tmx.offset = (long)(SLEW_THRESHOLD * 1000000);
		}
		if (tmx.offset < -(long)(SLEW_THRESHOLD * 1000000)) {
			tmx.offset = -(long)(SLEW_THRESHOLD * 1000000);
		}
	}

	tmx.status = STA_PLL;
	if (G.FREQHOLD_cnt != 0) {
		/* man adjtimex on STA_FREQHOLD:
		 * "Normally adjustments made via ADJ_OFFSET result in dampened
		 * frequency adjustments also being made.
		 * This flag prevents the small frequency adjustment from being
		 * made when correcting for an ADJ_OFFSET value."
		 *
		 * Use this flag for a few first adjustments at the beginning
		 * of ntpd execution, otherwise even relatively small initial
		 * offset tend to cause largish changes to in-kernel tmx.freq.
		 * If ntpd was restarted due to e.g. switch to another network,
		 * this destroys already well-established tmx.freq value.
		 */
		if (G.FREQHOLD_cnt < 0) {
			/* Initialize it */
// Example: a laptop whose clock runs slower when hibernated,
// after wake up it still has good tmx.freq, but accumulated ~0.5 sec offset:
// Run with code where initial G.FREQHOLD_cnt was always 8:
//15:17:52.947 no valid datapoints, no peer selected
//15:17:56.515 update from:<IP> offset:+0.485133 delay:0.157762 jitter:0.209310 clock drift:-1.393ppm tc:4
//15:17:57.719 update from:<IP> offset:+0.483825 delay:0.158070 jitter:0.181159 clock drift:-1.393ppm tc:4
//15:17:59.925 update from:<IP> offset:+0.479504 delay:0.158147 jitter:0.156657 clock drift:-1.393ppm tc:4
//15:18:33.322 update from:<IP> offset:+0.428119 delay:0.158317 jitter:0.138071 clock drift:-1.393ppm tc:4
//15:19:06.718 update from:<IP> offset:+0.376932 delay:0.158276 jitter:0.122075 clock drift:-1.393ppm tc:4
//15:19:39.114 update from:<IP> offset:+0.327022 delay:0.158384 jitter:0.108538 clock drift:-1.393ppm tc:4
//15:20:12.715 update from:<IP> offset:+0.275596 delay:0.158297 jitter:0.097292 clock drift:-1.393ppm tc:4
//15:20:45.111 update from:<IP> offset:+0.225715 delay:0.158271 jitter:0.087841 clock drift:-1.393ppm tc:4
// If allowed to continue, it would start increasing tmx.freq now.
// Instead, it was ^Ced, and started anew:
//15:21:15.043 no valid datapoints, no peer selected
//15:21:17.408 update from:<IP> offset:+0.175910 delay:0.158314 jitter:0.076683 clock drift:-1.393ppm tc:4
//15:21:19.774 update from:<IP> offset:+0.171784 delay:0.158401 jitter:0.066436 clock drift:-1.393ppm tc:4
//15:21:22.140 update from:<IP> offset:+0.171660 delay:0.158592 jitter:0.057536 clock drift:-1.393ppm tc:4
//15:21:22.140 update from:<IP> offset:+0.167126 delay:0.158507 jitter:0.049792 clock drift:-1.393ppm tc:4
//15:21:55.696 update from:<IP> offset:+0.115223 delay:0.158277 jitter:0.050240 clock drift:-1.393ppm tc:4
//15:22:29.093 update from:<IP> offset:+0.068051 delay:0.158243 jitter:0.049405 clock drift:-1.393ppm tc:5
//15:23:02.490 update from:<IP> offset:+0.051632 delay:0.158215 jitter:0.043545 clock drift:-1.393ppm tc:5
//15:23:34.726 update from:<IP> offset:+0.039984 delay:0.158157 jitter:0.038106 clock drift:-1.393ppm tc:5
// STA_FREQHOLD no longer set, started increasing tmx.freq now:
//15:24:06.961 update from:<IP> offset:+0.030968 delay:0.158190 jitter:0.033306 clock drift:+2.387ppm tc:5
//15:24:40.357 update from:<IP> offset:+0.023648 delay:0.158211 jitter:0.029072 clock drift:+5.454ppm tc:5
//15:25:13.774 update from:<IP> offset:+0.018068 delay:0.157660 jitter:0.025288 clock drift:+7.728ppm tc:5
//15:26:19.173 update from:<IP> offset:+0.010057 delay:0.157969 jitter:0.022255 clock drift:+8.361ppm tc:6
//15:27:26.602 update from:<IP> offset:+0.006737 delay:0.158103 jitter:0.019316 clock drift:+8.792ppm tc:6
//15:28:33.030 update from:<IP> offset:+0.004513 delay:0.158294 jitter:0.016765 clock drift:+9.080ppm tc:6
//15:29:40.617 update from:<IP> offset:+0.002787 delay:0.157745 jitter:0.014543 clock drift:+9.258ppm tc:6
//15:30:47.045 update from:<IP> offset:+0.001324 delay:0.157709 jitter:0.012594 clock drift:+9.342ppm tc:6
//15:31:53.473 update from:<IP> offset:+0.000007 delay:0.158142 jitter:0.010922 clock drift:+9.343ppm tc:6
//15:32:58.902 update from:<IP> offset:-0.000728 delay:0.158222 jitter:0.009454 clock drift:+9.298ppm tc:6
			/*
			 * This expression would choose MIN_FREQHOLD + 7 in the above example.
			 */
			G.FREQHOLD_cnt = MIN_FREQHOLD + ((unsigned)(abs(tmx.offset)) >> 16);
		}
		G.FREQHOLD_cnt--;
		tmx.status |= STA_FREQHOLD;
	}
	if (G.ntp_status & LI_PLUSSEC)
		tmx.status |= STA_INS;
	if (G.ntp_status & LI_MINUSSEC)
		tmx.status |= STA_DEL;

	tmx.constant = (int)G.poll_exp - 4;
	/* EXPERIMENTAL.
	 * The below if statement should be unnecessary, but...
	 * It looks like Linux kernel's PLL is far too gentle in changing
	 * tmx.freq in response to clock offset. Offset keeps growing
	 * and eventually we fall back to smaller poll intervals.
	 * We can make correction more aggressive (about x2) by supplying
	 * PLL time constant which is one less than the real one.
	 * To be on a safe side, let's do it only if offset is significantly
	 * larger than jitter.
	 */
	if (G.offset_to_jitter_ratio >= TIMECONST_HACK_GATE)
		tmx.constant--;
	if (tmx.constant < 0)
		tmx.constant = 0;

	//tmx.esterror = (uint32_t)(clock_jitter * 1e6);
	//tmx.maxerror = (uint32_t)((sys_rootdelay / 2 + sys_rootdisp) * 1e6);
	rc = adjtimex(&tmx);
	if (rc < 0)
		bb_perror_msg_and_die("adjtimex");
	/* NB: here kernel returns constant == G.poll_exp, not == G.poll_exp - 4.
	 * Not sure why. Perhaps it is normal.
	 */
	VERB4 bb_error_msg("adjtimex:%d freq:%ld offset:%+ld status:0x%x",
				rc, tmx.freq, tmx.offset, tmx.status);
	G.kernel_freq_drift = tmx.freq / 65536;
	VERB2 bb_error_msg("update from:%s offset:%+f delay:%f jitter:%f clock drift:%+.3fppm tc:%d",
			p->p_dotted,
			offset,
			p->lastpkt_delay,
			G.discipline_jitter,
			(double)tmx.freq / 65536,
			(int)tmx.constant
	);

	return 1; /* "ok to increase poll interval" */
}


/*
 * We've got a new reply packet from a peer, process it
 * (helpers first)
 */
static unsigned
poll_interval(int upper_bound)
{
	unsigned interval, r, mask;
	interval = 1 << G.poll_exp;
	if (interval > upper_bound)
		interval = upper_bound;
	mask = ((interval-1) >> 4) | 1;
	r = rand();
	interval += r & mask; /* ~ random(0..1) * interval/16 */
	VERB4 bb_error_msg("chose poll interval:%u (poll_exp:%d)", interval, G.poll_exp);
	return interval;
}
static void
adjust_poll(int count)
{
	G.polladj_count += count;
	if (G.polladj_count > POLLADJ_LIMIT) {
		G.polladj_count = 0;
		if (G.poll_exp < MAXPOLL) {
			G.poll_exp++;
			VERB4 bb_error_msg("polladj: discipline_jitter:%f ++poll_exp=%d",
					G.discipline_jitter, G.poll_exp);
		}
	} else if (G.polladj_count < -POLLADJ_LIMIT || (count < 0 && G.poll_exp > BIGPOLL)) {
		G.polladj_count = 0;
		if (G.poll_exp > MINPOLL) {
			llist_t *item;

			G.poll_exp--;
			/* Correct p->next_action_time in each peer
			 * which waits for sending, so that they send earlier.
			 * Old pp->next_action_time are on the order
			 * of t + (1 << old_poll_exp) + small_random,
			 * we simply need to subtract ~half of that.
			 */
			for (item = G.ntp_peers; item != NULL; item = item->link) {
				peer_t *pp = (peer_t *) item->data;
				if (pp->p_fd < 0)
					pp->next_action_time -= (1 << G.poll_exp);
			}
			VERB4 bb_error_msg("polladj: discipline_jitter:%f --poll_exp=%d",
					G.discipline_jitter, G.poll_exp);
		}
	} else {
		VERB4 bb_error_msg("polladj: count:%d", G.polladj_count);
	}
}
static NOINLINE void
recv_and_process_peer_pkt(peer_t *p)
{
	int         rc;
	ssize_t     size;
	msg_t       msg;
	double      T1, T2, T3, T4;
	double      offset;
	double      prev_delay, delay;
	unsigned    interval;
	datapoint_t *datapoint;
	peer_t      *q;

	offset = 0;

	/* We can recvfrom here and check from.IP, but some multihomed
	 * ntp servers reply from their *other IP*.
	 * TODO: maybe we should check at least what we can: from.port == 123?
	 */
 recv_again:
	size = recv(p->p_fd, &msg, sizeof(msg), MSG_DONTWAIT);
	if (size < 0) {
		if (errno == EINTR)
			/* Signal caught */
			goto recv_again;
		if (errno == EAGAIN)
			/* There was no packet after all
			 * (poll() returning POLLIN for a fd
			 * is not a ironclad guarantee that data is there)
			 */
			return;
		/*
		 * If you need a different handling for a specific
		 * errno, always explain it in comment.
		 */
		bb_perror_msg_and_die("recv(%s) error", p->p_dotted);
	}

	if (size != NTP_MSGSIZE_NOAUTH && size != NTP_MSGSIZE) {
		bb_error_msg("malformed packet received from %s", p->p_dotted);
		return;
	}

	if (msg.m_orgtime.int_partl != p->p_xmt_msg.m_xmttime.int_partl
	 || msg.m_orgtime.fractionl != p->p_xmt_msg.m_xmttime.fractionl
	) {
		/* Somebody else's packet */
		return;
	}

	/* We do not expect any more packets from this peer for now.
	 * Closing the socket informs kernel about it.
	 * We open a new socket when we send a new query.
	 */
	close(p->p_fd);
	p->p_fd = -1;

	if ((msg.m_status & LI_ALARM) == LI_ALARM
	 || msg.m_stratum == 0
	 || msg.m_stratum > NTP_MAXSTRATUM
	) {
		bb_error_msg("reply from %s: peer is unsynced", p->p_dotted);
		/*
		 * Stratum 0 responses may have commands in 32-bit m_refid field:
		 * "DENY", "RSTR" - peer does not like us at all,
		 * "RATE" - peer is overloaded, reduce polling freq.
		 * If poll interval is small, increase it.
		 */
		if (G.poll_exp < BIGPOLL)
			goto increase_interval;
		goto pick_normal_interval;
	}

//	/* Verify valid root distance */
//	if (msg.m_rootdelay / 2 + msg.m_rootdisp >= MAXDISP || p->lastpkt_reftime > msg.m_xmt)
//		return;                 /* invalid header values */

	/*
	 * From RFC 2030 (with a correction to the delay math):
	 *
	 * Timestamp Name          ID   When Generated
	 * ------------------------------------------------------------
	 * Originate Timestamp     T1   time request sent by client
	 * Receive Timestamp       T2   time request received by server
	 * Transmit Timestamp      T3   time reply sent by server
	 * Destination Timestamp   T4   time reply received by client
	 *
	 * The roundtrip delay and local clock offset are defined as
	 *
	 * delay = (T4 - T1) - (T3 - T2); offset = ((T2 - T1) + (T3 - T4)) / 2
	 */
	T1 = p->p_xmttime;
	T2 = lfp_to_d(msg.m_rectime);
	T3 = lfp_to_d(msg.m_xmttime);
	T4 = G.cur_time;

	/* The delay calculation is a special case. In cases where the
	 * server and client clocks are running at different rates and
	 * with very fast networks, the delay can appear negative. In
	 * order to avoid violating the Principle of Least Astonishment,
	 * the delay is clamped not less than the system precision.
	 */
	delay = (T4 - T1) - (T3 - T2);
	if (delay < G_precision_sec)
		delay = G_precision_sec;
	/*
	 * If this packet's delay is much bigger than the last one,
	 * it's better to just ignore it than use its much less precise value.
	 */
	prev_delay = p->p_raw_delay;
	p->p_raw_delay = delay;
	if (p->reachable_bits && delay > prev_delay * BAD_DELAY_GROWTH) {
		bb_error_msg("reply from %s: delay %f is too high, ignoring", p->p_dotted, delay);
		goto pick_normal_interval;
	}

	p->lastpkt_delay = delay;
	p->lastpkt_recv_time = T4;
	VERB6 bb_error_msg("%s->lastpkt_recv_time=%f", p->p_dotted, p->lastpkt_recv_time);
	p->lastpkt_status = msg.m_status;
	p->lastpkt_stratum = msg.m_stratum;
	p->lastpkt_rootdelay = sfp_to_d(msg.m_rootdelay);
	p->lastpkt_rootdisp = sfp_to_d(msg.m_rootdisp);
	p->lastpkt_refid = msg.m_refid;

	p->datapoint_idx = p->reachable_bits ? (p->datapoint_idx + 1) % NUM_DATAPOINTS : 0;
	datapoint = &p->filter_datapoint[p->datapoint_idx];
	datapoint->d_recv_time = T4;
	datapoint->d_offset    = offset = ((T2 - T1) + (T3 - T4)) / 2;
	datapoint->d_dispersion = LOG2D(msg.m_precision_exp) + G_precision_sec;
	if (!p->reachable_bits) {
		/* 1st datapoint ever - replicate offset in every element */
		int i;
		for (i = 0; i < NUM_DATAPOINTS; i++) {
			p->filter_datapoint[i].d_offset = offset;
		}
	}

	p->reachable_bits |= 1;
	if ((MAX_VERBOSE && G.verbose) || (option_mask32 & OPT_w)) {
		bb_error_msg("reply from %s: offset:%+f delay:%f status:0x%02x strat:%d refid:0x%08x rootdelay:%f reach:0x%02x",
			p->p_dotted,
			offset,
			p->lastpkt_delay,
			p->lastpkt_status,
			p->lastpkt_stratum,
			p->lastpkt_refid,
			p->lastpkt_rootdelay,
			p->reachable_bits
			/* not shown: m_ppoll, m_precision_exp, m_rootdisp,
			 * m_reftime, m_orgtime, m_rectime, m_xmttime
			 */
		);
	}

	/* Muck with statictics and update the clock */
	filter_datapoints(p);
	q = select_and_cluster();
	rc = 0;
	if (q) {
		if (!(option_mask32 & OPT_w)) {
			rc = update_local_clock(q);
#if 0
//Disabled this because there is a case where largish offsets
//are unavoidable: if network round-trip delay is, say, ~0.6s,
//error in offset estimation would be ~delay/2 ~= 0.3s.
//Thus, offsets will be usually in -0.3...0.3s range.
//In this case, this code would keep poll interval small,
//but it won't be helping.
//BIGOFF check below deals with a case of seeing multi-second offsets.

			/* If drift is dangerously large, immediately
			 * drop poll interval one step down.
			 */
			if (fabs(q->filter_offset) >= POLLDOWN_OFFSET) {
				VERB4 bb_error_msg("offset:%+f > POLLDOWN_OFFSET", q->filter_offset);
				adjust_poll(-POLLADJ_LIMIT * 3);
				rc = 0;
			}
#endif
		}
	} else {
		/* No peer selected.
		 * If poll interval is small, increase it.
		 */
		if (G.poll_exp < BIGPOLL)
			goto increase_interval;
	}

	if (rc != 0) {
		/* Adjust the poll interval by comparing the current offset
		 * with the clock jitter. If the offset is less than
		 * the clock jitter times a constant, then the averaging interval
		 * is increased, otherwise it is decreased. A bit of hysteresis
		 * helps calm the dance. Works best using burst mode.
		 */
		if (rc > 0 && G.offset_to_jitter_ratio <= POLLADJ_GATE) {
			/* was += G.poll_exp but it is a bit
			 * too optimistic for my taste at high poll_exp's */
 increase_interval:
			adjust_poll(MINPOLL);
		} else {
			VERB3 if (rc > 0)
				bb_error_msg("want smaller interval: offset/jitter = %u",
					G.offset_to_jitter_ratio);
			adjust_poll(-G.poll_exp * 2);
		}
	}

	/* Decide when to send new query for this peer */
 pick_normal_interval:
	interval = poll_interval(INT_MAX);
	if (fabs(offset) >= BIGOFF && interval > BIGOFF_INTERVAL) {
		/* If we are synced, offsets are less than SLEW_THRESHOLD,
		 * or at the very least not much larger than it.
		 * Now we see a largish one.
		 * Either this peer is feeling bad, or packet got corrupted,
		 * or _our_ clock is wrong now and _all_ peers will show similar
		 * largish offsets too.
		 * I observed this with laptop suspend stopping clock.
		 * In any case, it makes sense to make next request soonish:
		 * cases 1 and 2: get a better datapoint,
		 * case 3: allows to resync faster.
		 */
		interval = BIGOFF_INTERVAL;
	}

	set_next(p, interval);
}

#if ENABLE_FEATURE_NTPD_SERVER
static NOINLINE void
recv_and_process_client_pkt(void /*int fd*/)
{
	ssize_t          size;
	//uint8_t          version;
	len_and_sockaddr *to;
	struct sockaddr  *from;
	msg_t            msg;
	uint8_t          query_status;
	l_fixedpt_t      query_xmttime;

	to = get_sock_lsa(G_listen_fd);
	from = xzalloc(to->len);

	size = recv_from_to(G_listen_fd, &msg, sizeof(msg), MSG_DONTWAIT, from, &to->u.sa, to->len);
	if (size != NTP_MSGSIZE_NOAUTH && size != NTP_MSGSIZE) {
		char *addr;
		if (size < 0) {
			if (errno == EAGAIN)
				goto bail;
			bb_perror_msg_and_die("recv");
		}
		addr = xmalloc_sockaddr2dotted_noport(from);
		bb_error_msg("malformed packet received from %s: size %u", addr, (int)size);
		free(addr);
		goto bail;
	}

	/* Respond only to client and symmetric active packets */
	if ((msg.m_status & MODE_MASK) != MODE_CLIENT
	 && (msg.m_status & MODE_MASK) != MODE_SYM_ACT
	) {
		goto bail;
	}

	query_status = msg.m_status;
	query_xmttime = msg.m_xmttime;

	/* Build a reply packet */
	memset(&msg, 0, sizeof(msg));
	msg.m_status = G.stratum < MAXSTRAT ? (G.ntp_status & LI_MASK) : LI_ALARM;
	msg.m_status |= (query_status & VERSION_MASK);
	msg.m_status |= ((query_status & MODE_MASK) == MODE_CLIENT) ?
			MODE_SERVER : MODE_SYM_PAS;
	msg.m_stratum = G.stratum;
	msg.m_ppoll = G.poll_exp;
	msg.m_precision_exp = G_precision_exp;
	/* this time was obtained between poll() and recv() */
	msg.m_rectime = d_to_lfp(G.cur_time);
	msg.m_xmttime = d_to_lfp(gettime1900d()); /* this instant */
	if (G.peer_cnt == 0) {
		/* we have no peers: "stratum 1 server" mode. reftime = our own time */
		G.reftime = G.cur_time;
	}
	msg.m_reftime = d_to_lfp(G.reftime);
	msg.m_orgtime = query_xmttime;
	msg.m_rootdelay = d_to_sfp(G.rootdelay);
//simple code does not do this, fix simple code!
	msg.m_rootdisp = d_to_sfp(G.rootdisp);
	//version = (query_status & VERSION_MASK); /* ... >> VERSION_SHIFT - done below instead */
	msg.m_refid = G.refid; // (version > (3 << VERSION_SHIFT)) ? G.refid : G.refid3;

	/* We reply from the local address packet was sent to,
	 * this makes to/from look swapped here: */
	do_sendto(G_listen_fd,
		/*from:*/ &to->u.sa, /*to:*/ from, /*addrlen:*/ to->len,
		&msg, size);

 bail:
	free(to);
	free(from);
}
#endif

/* Upstream ntpd's options:
 *
 * -4   Force DNS resolution of host names to the IPv4 namespace.
 * -6   Force DNS resolution of host names to the IPv6 namespace.
 * -a   Require cryptographic authentication for broadcast client,
 *      multicast client and symmetric passive associations.
 *      This is the default.
 * -A   Do not require cryptographic authentication for broadcast client,
 *      multicast client and symmetric passive associations.
 *      This is almost never a good idea.
 * -b   Enable the client to synchronize to broadcast servers.
 * -c conffile
 *      Specify the name and path of the configuration file,
 *      default /etc/ntp.conf
 * -d   Specify debugging mode. This option may occur more than once,
 *      with each occurrence indicating greater detail of display.
 * -D level
 *      Specify debugging level directly.
 * -f driftfile
 *      Specify the name and path of the frequency file.
 *      This is the same operation as the "driftfile FILE"
 *      configuration command.
 * -g   Normally, ntpd exits with a message to the system log
 *      if the offset exceeds the panic threshold, which is 1000 s
 *      by default. This option allows the time to be set to any value
 *      without restriction; however, this can happen only once.
 *      If the threshold is exceeded after that, ntpd will exit
 *      with a message to the system log. This option can be used
 *      with the -q and -x options. See the tinker command for other options.
 * -i jaildir
 *      Chroot the server to the directory jaildir. This option also implies
 *      that the server attempts to drop root privileges at startup
 *      (otherwise, chroot gives very little additional security).
 *      You may need to also specify a -u option.
 * -k keyfile
 *      Specify the name and path of the symmetric key file,
 *      default /etc/ntp/keys. This is the same operation
 *      as the "keys FILE" configuration command.
 * -l logfile
 *      Specify the name and path of the log file. The default
 *      is the system log file. This is the same operation as
 *      the "logfile FILE" configuration command.
 * -L   Do not listen to virtual IPs. The default is to listen.
 * -n   Don't fork.
 * -N   To the extent permitted by the operating system,
 *      run the ntpd at the highest priority.
 * -p pidfile
 *      Specify the name and path of the file used to record the ntpd
 *      process ID. This is the same operation as the "pidfile FILE"
 *      configuration command.
 * -P priority
 *      To the extent permitted by the operating system,
 *      run the ntpd at the specified priority.
 * -q   Exit the ntpd just after the first time the clock is set.
 *      This behavior mimics that of the ntpdate program, which is
 *      to be retired. The -g and -x options can be used with this option.
 *      Note: The kernel time discipline is disabled with this option.
 * -r broadcastdelay
 *      Specify the default propagation delay from the broadcast/multicast
 *      server to this client. This is necessary only if the delay
 *      cannot be computed automatically by the protocol.
 * -s statsdir
 *      Specify the directory path for files created by the statistics
 *      facility. This is the same operation as the "statsdir DIR"
 *      configuration command.
 * -t key
 *      Add a key number to the trusted key list. This option can occur
 *      more than once.
 * -u user[:group]
 *      Specify a user, and optionally a group, to switch to.
 * -v variable
 * -V variable
 *      Add a system variable listed by default.
 * -x   Normally, the time is slewed if the offset is less than the step
 *      threshold, which is 128 ms by default, and stepped if above
 *      the threshold. This option sets the threshold to 600 s, which is
 *      well within the accuracy window to set the clock manually.
 *      Note: since the slew rate of typical Unix kernels is limited
 *      to 0.5 ms/s, each second of adjustment requires an amortization
 *      interval of 2000 s. Thus, an adjustment as much as 600 s
 *      will take almost 14 days to complete. This option can be used
 *      with the -g and -q options. See the tinker command for other options.
 *      Note: The kernel time discipline is disabled with this option.
 */

/* By doing init in a separate function we decrease stack usage
 * in main loop.
 */
static NOINLINE void ntp_init(char **argv)
{
	unsigned opts;
	llist_t *peers;

	srand(getpid());

	if (getuid())
		bb_error_msg_and_die(bb_msg_you_must_be_root);

	/* Set some globals */
	G.discipline_jitter = G_precision_sec;
	G.stratum = MAXSTRAT;
	if (BURSTPOLL != 0)
		G.poll_exp = BURSTPOLL; /* speeds up initial sync */
	G.last_script_run = G.reftime = G.last_update_recv_time = gettime1900d(); /* sets G.cur_time too */
	G.FREQHOLD_cnt = -1;

	/* Parse options */
	peers = NULL;
	opts = getopt32(argv, "^"
			"nqNx" /* compat */
			"wp:*S:"IF_FEATURE_NTPD_SERVER("l") /* NOT compat */
			IF_FEATURE_NTPD_SERVER("I:") /* compat */
			"d" /* compat */
			"46aAbgL" /* compat, ignored */
				"\0"
				"dd:wn"  /* -d: counter; -p: list; -w implies -n */
				IF_FEATURE_NTPD_SERVER(":Il") /* -I implies -l */
			, &peers, &G.script_name,
#if ENABLE_FEATURE_NTPD_SERVER
			&G.if_name,
#endif
			&G.verbose);

//	if (opts & OPT_x) /* disable stepping, only slew is allowed */
//		G.time_was_stepped = 1;

#if ENABLE_FEATURE_NTPD_SERVER
	G_listen_fd = -1;
	if (opts & OPT_l) {
		G_listen_fd = create_and_bind_dgram_or_die(NULL, 123);
		if (G.if_name) {
			if (setsockopt_bindtodevice(G_listen_fd, G.if_name))
				xfunc_die();
		}
		socket_want_pktinfo(G_listen_fd);
		setsockopt_int(G_listen_fd, IPPROTO_IP, IP_TOS, IPTOS_DSCP_AF21);
	}
#endif
	/* I hesitate to set -20 prio. -15 should be high enough for timekeeping */
	if (opts & OPT_N)
		setpriority(PRIO_PROCESS, 0, -15);

	if (!(opts & OPT_n)) {
		bb_daemonize_or_rexec(DAEMON_DEVNULL_STDIO, argv);
		logmode = LOGMODE_NONE;
	}

	if (peers) {
		while (peers)
			add_peers(llist_pop(&peers));
	}
#if ENABLE_FEATURE_NTPD_CONF
	else {
		parser_t *parser;
		char *token[3];

		parser = config_open("/etc/ntp.conf");
		while (config_read(parser, token, 3, 1, "# \t", PARSE_NORMAL)) {
			if (strcmp(token[0], "server") == 0 && token[1]) {
				add_peers(token[1]);
				continue;
			}
			bb_error_msg("skipping %s:%u: unimplemented command '%s'",
				"/etc/ntp.conf", parser->lineno, token[0]
			);
		}
		config_close(parser);
	}
#endif
	if (G.peer_cnt == 0) {
		if (!(opts & OPT_l))
			bb_show_usage();
		/* -l but no peers: "stratum 1 server" mode */
		G.stratum = 1;
	}
	/* If network is up, syncronization occurs in ~10 seconds.
	 * We give "ntpd -q" 10 seconds to get first reply,
	 * then another 50 seconds to finish syncing.
	 *
	 * I tested ntpd 4.2.6p1 and apparently it never exits
	 * (will try forever), but it does not feel right.
	 * The goal of -q is to act like ntpdate: set time
	 * after a reasonably small period of polling, or fail.
	 */
	if (opts & OPT_q) {
		option_mask32 |= OPT_qq;
		alarm(10);
	}

	bb_signals(0
		| (1 << SIGTERM)
		| (1 << SIGINT)
		| (1 << SIGALRM)
		, record_signo
	);
	bb_signals(0
		| (1 << SIGPIPE)
		| (1 << SIGCHLD)
		, SIG_IGN
	);
}

int ntpd_main(int argc UNUSED_PARAM, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ntpd_main(int argc UNUSED_PARAM, char **argv)
{
#undef G
	struct globals G;
	struct pollfd *pfd;
	peer_t **idx2peer;
	unsigned cnt;

	memset(&G, 0, sizeof(G));
	SET_PTR_TO_GLOBALS(&G);

	ntp_init(argv);

	/* If ENABLE_FEATURE_NTPD_SERVER, + 1 for listen_fd: */
	cnt = G.peer_cnt + ENABLE_FEATURE_NTPD_SERVER;
	idx2peer = xzalloc(sizeof(idx2peer[0]) * cnt);
	pfd = xzalloc(sizeof(pfd[0]) * cnt);

	/* Countdown: we never sync before we sent INITIAL_SAMPLES+1
	 * packets to each peer.
	 * NB: if some peer is not responding, we may end up sending
	 * fewer packets to it and more to other peers.
	 * NB2: sync usually happens using INITIAL_SAMPLES packets,
	 * since last reply does not come back instantaneously.
	 */
	cnt = G.peer_cnt * (INITIAL_SAMPLES + 1);

	write_pidfile(CONFIG_PID_FILE_PATH "/ntpd.pid");

	while (!bb_got_signal) {
		llist_t *item;
		unsigned i, j;
		int nfds, timeout;
		double nextaction;

		/* Nothing between here and poll() blocks for any significant time */

		nextaction = G.last_script_run + (11*60);
		if (nextaction < G.cur_time + 1)
			nextaction = G.cur_time + 1;

		i = 0;
#if ENABLE_FEATURE_NTPD_SERVER
		if (G_listen_fd != -1) {
			pfd[0].fd = G_listen_fd;
			pfd[0].events = POLLIN;
			i++;
		}
#endif
		/* Pass over peer list, send requests, time out on receives */
		for (item = G.ntp_peers; item != NULL; item = item->link) {
			peer_t *p = (peer_t *) item->data;

			if (p->next_action_time <= G.cur_time) {
				if (p->p_fd == -1) {
					/* Time to send new req */
					if (--cnt == 0) {
						VERB4 bb_error_msg("disabling burst mode");
						G.polladj_count = 0;
						G.poll_exp = MINPOLL;
					}
					send_query_to_peer(p);
				} else {
					/* Timed out waiting for reply */
					close(p->p_fd);
					p->p_fd = -1;
					/* If poll interval is small, increase it */
					if (G.poll_exp < BIGPOLL)
						adjust_poll(MINPOLL);
					timeout = poll_interval(NOREPLY_INTERVAL);
					bb_error_msg("timed out waiting for %s, reach 0x%02x, next query in %us",
							p->p_dotted, p->reachable_bits, timeout);

					/* What if don't see it because it changed its IP? */
					if (p->reachable_bits == 0)
						resolve_peer_hostname(p);

					set_next(p, timeout);
				}
			}

			if (p->next_action_time < nextaction)
				nextaction = p->next_action_time;

			if (p->p_fd >= 0) {
				/* Wait for reply from this peer */
				pfd[i].fd = p->p_fd;
				pfd[i].events = POLLIN;
				idx2peer[i] = p;
				i++;
			}
		}

		timeout = nextaction - G.cur_time;
		if (timeout < 0)
			timeout = 0;
		timeout++; /* (nextaction - G.cur_time) rounds down, compensating */

		/* Here we may block */
		VERB2 {
			if (i > (ENABLE_FEATURE_NTPD_SERVER && G_listen_fd != -1)) {
				/* We wait for at least one reply.
				 * Poll for it, without wasting time for message.
				 * Since replies often come under 1 second, this also
				 * reduces clutter in logs.
				 */
				nfds = poll(pfd, i, 1000);
				if (nfds != 0)
					goto did_poll;
				if (--timeout <= 0)
					goto did_poll;
			}
			bb_error_msg("poll:%us sockets:%u interval:%us", timeout, i, 1 << G.poll_exp);
		}
		nfds = poll(pfd, i, timeout * 1000);
 did_poll:
		gettime1900d(); /* sets G.cur_time */
		if (nfds <= 0) {
			double ct;
			int dns_error;

			if (bb_got_signal)
				break; /* poll was interrupted by a signal */

			if (G.cur_time - G.last_script_run > 11*60) {
				/* Useful for updating battery-backed RTC and such */
				run_script("periodic", G.last_update_offset);
				gettime1900d(); /* sets G.cur_time */
			}

			/* Resolve peer names to IPs, if not resolved yet.
			 * We do it only when poll timed out:
			 * this way, we almost never overlap DNS resolution with
			 * "request-reply" packet round trip.
			 */
			dns_error = 0;
			ct = G.cur_time;
			for (item = G.ntp_peers; item != NULL; item = item->link) {
				peer_t *p = (peer_t *) item->data;
				if (p->next_action_time <= ct && !p->p_lsa) {
					/* This can take up to ~10 sec per each DNS query */
					dns_error |= (!resolve_peer_hostname(p));
				}
			}
			if (!dns_error)
				goto check_unsync;
			/* Set next time for those which are still not resolved */
			gettime1900d(); /* sets G.cur_time (needed for set_next()) */
			for (item = G.ntp_peers; item != NULL; item = item->link) {
				peer_t *p = (peer_t *) item->data;
				if (p->next_action_time <= ct && !p->p_lsa) {
					set_next(p, HOSTNAME_INTERVAL * p->dns_errors);
				}
			}
			goto check_unsync;
		}

		/* Process any received packets */
		j = 0;
#if ENABLE_FEATURE_NTPD_SERVER
		if (G.listen_fd != -1) {
			if (pfd[0].revents /* & (POLLIN|POLLERR)*/) {
				nfds--;
				recv_and_process_client_pkt(/*G.listen_fd*/);
				gettime1900d(); /* sets G.cur_time */
			}
			j = 1;
		}
#endif
		for (; nfds != 0 && j < i; j++) {
			if (pfd[j].revents /* & (POLLIN|POLLERR)*/) {
				/*
				 * At init, alarm was set to 10 sec.
				 * Now we did get a reply.
				 * Increase timeout to 50 seconds to finish syncing.
				 */
				if (option_mask32 & OPT_qq) {
					option_mask32 &= ~OPT_qq;
					alarm(50);
				}
				nfds--;
				recv_and_process_peer_pkt(idx2peer[j]);
				gettime1900d(); /* sets G.cur_time */
			}
		}

 check_unsync:
		if (G.ntp_peers && G.stratum != MAXSTRAT) {
			for (item = G.ntp_peers; item != NULL; item = item->link) {
				peer_t *p = (peer_t *) item->data;
				if (p->reachable_bits)
					goto have_reachable_peer;
			}
			/* No peer responded for last 8 packets, panic */
			clamp_pollexp_and_set_MAXSTRAT();
			run_script("unsync", 0.0);
 have_reachable_peer: ;
		}
	} /* while (!bb_got_signal) */

	remove_pidfile(CONFIG_PID_FILE_PATH "/ntpd.pid");
	kill_myself_with_sig(bb_got_signal);
}






/*** openntpd-4.6 uses only adjtime, not adjtimex ***/

/*** ntp-4.2.6/ntpd/ntp_loopfilter.c - adjtimex usage ***/

#if 0
static double
direct_freq(double fp_offset)
{
#ifdef KERNEL_PLL
	/*
	 * If the kernel is enabled, we need the residual offset to
	 * calculate the frequency correction.
	 */
	if (pll_control && kern_enable) {
		memset(&ntv, 0, sizeof(ntv));
		ntp_adjtime(&ntv);
#ifdef STA_NANO
		clock_offset = ntv.offset / 1e9;
#else /* STA_NANO */
		clock_offset = ntv.offset / 1e6;
#endif /* STA_NANO */
		drift_comp = FREQTOD(ntv.freq);
	}
#endif /* KERNEL_PLL */
	set_freq((fp_offset - clock_offset) / (current_time - clock_epoch) + drift_comp);
	wander_resid = 0;
	return drift_comp;
}

static void
set_freq(double freq) /* frequency update */
{
	char tbuf[80];

	drift_comp = freq;

#ifdef KERNEL_PLL
	/*
	 * If the kernel is enabled, update the kernel frequency.
	 */
	if (pll_control && kern_enable) {
		memset(&ntv, 0, sizeof(ntv));
		ntv.modes = MOD_FREQUENCY;
		ntv.freq = DTOFREQ(drift_comp);
		ntp_adjtime(&ntv);
		snprintf(tbuf, sizeof(tbuf), "kernel %.3f PPM", drift_comp * 1e6);
		report_event(EVNT_FSET, NULL, tbuf);
	} else {
		snprintf(tbuf, sizeof(tbuf), "ntpd %.3f PPM", drift_comp * 1e6);
		report_event(EVNT_FSET, NULL, tbuf);
	}
#else /* KERNEL_PLL */
	snprintf(tbuf, sizeof(tbuf), "ntpd %.3f PPM", drift_comp * 1e6);
	report_event(EVNT_FSET, NULL, tbuf);
#endif /* KERNEL_PLL */
}

...
...
...

#ifdef KERNEL_PLL
	/*
	 * This code segment works when clock adjustments are made using
	 * precision time kernel support and the ntp_adjtime() system
	 * call. This support is available in Solaris 2.6 and later,
	 * Digital Unix 4.0 and later, FreeBSD, Linux and specially
	 * modified kernels for HP-UX 9 and Ultrix 4. In the case of the
	 * DECstation 5000/240 and Alpha AXP, additional kernel
	 * modifications provide a true microsecond clock and nanosecond
	 * clock, respectively.
	 *
	 * Important note: The kernel discipline is used only if the
	 * step threshold is less than 0.5 s, as anything higher can
	 * lead to overflow problems. This might occur if some misguided
	 * lad set the step threshold to something ridiculous.
	 */
	if (pll_control && kern_enable) {

#define MOD_BITS (MOD_OFFSET | MOD_MAXERROR | MOD_ESTERROR | MOD_STATUS | MOD_TIMECONST)

		/*
		 * We initialize the structure for the ntp_adjtime()
		 * system call. We have to convert everything to
		 * microseconds or nanoseconds first. Do not update the
		 * system variables if the ext_enable flag is set. In
		 * this case, the external clock driver will update the
		 * variables, which will be read later by the local
		 * clock driver. Afterwards, remember the time and
		 * frequency offsets for jitter and stability values and
		 * to update the frequency file.
		 */
		memset(&ntv,  0, sizeof(ntv));
		if (ext_enable) {
			ntv.modes = MOD_STATUS;
		} else {
#ifdef STA_NANO
			ntv.modes = MOD_BITS | MOD_NANO;
#else /* STA_NANO */
			ntv.modes = MOD_BITS;
#endif /* STA_NANO */
			if (clock_offset < 0)
				dtemp = -.5;
			else
				dtemp = .5;
#ifdef STA_NANO
			ntv.offset = (int32)(clock_offset * 1e9 + dtemp);
			ntv.constant = sys_poll;
#else /* STA_NANO */
			ntv.offset = (int32)(clock_offset * 1e6 + dtemp);
			ntv.constant = sys_poll - 4;
#endif /* STA_NANO */
			ntv.esterror = (u_int32)(clock_jitter * 1e6);
			ntv.maxerror = (u_int32)((sys_rootdelay / 2 + sys_rootdisp) * 1e6);
			ntv.status = STA_PLL;

			/*
			 * Enable/disable the PPS if requested.
			 */
			if (pps_enable) {
				if (!(pll_status & STA_PPSTIME))
					report_event(EVNT_KERN,
						NULL, "PPS enabled");
				ntv.status |= STA_PPSTIME | STA_PPSFREQ;
			} else {
				if (pll_status & STA_PPSTIME)
					report_event(EVNT_KERN,
						NULL, "PPS disabled");
				ntv.status &= ~(STA_PPSTIME | STA_PPSFREQ);
			}
			if (sys_leap == LEAP_ADDSECOND)
				ntv.status |= STA_INS;
			else if (sys_leap == LEAP_DELSECOND)
				ntv.status |= STA_DEL;
		}

		/*
		 * Pass the stuff to the kernel. If it squeals, turn off
		 * the pps. In any case, fetch the kernel offset,
		 * frequency and jitter.
		 */
		if (ntp_adjtime(&ntv) == TIME_ERROR) {
			if (!(ntv.status & STA_PPSSIGNAL))
				report_event(EVNT_KERN, NULL,
						"PPS no signal");
		}
		pll_status = ntv.status;
#ifdef STA_NANO
		clock_offset = ntv.offset / 1e9;
#else /* STA_NANO */
		clock_offset = ntv.offset / 1e6;
#endif /* STA_NANO */
		clock_frequency = FREQTOD(ntv.freq);

		/*
		 * If the kernel PPS is lit, monitor its performance.
		 */
		if (ntv.status & STA_PPSTIME) {
#ifdef STA_NANO
			clock_jitter = ntv.jitter / 1e9;
#else /* STA_NANO */
			clock_jitter = ntv.jitter / 1e6;
#endif /* STA_NANO */
		}

#if defined(STA_NANO) && NTP_API == 4
		/*
		 * If the TAI changes, update the kernel TAI.
		 */
		if (loop_tai != sys_tai) {
			loop_tai = sys_tai;
			ntv.modes = MOD_TAI;
			ntv.constant = sys_tai;
			ntp_adjtime(&ntv);
		}
#endif /* STA_NANO */
	}
#endif /* KERNEL_PLL */
#endif
