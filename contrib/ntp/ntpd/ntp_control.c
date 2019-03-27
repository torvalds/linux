/*
 * ntp_control.c - respond to mode 6 control messages and send async
 *		   traps.  Provides service to ntpq and others.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#include <arpa/inet.h>

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_refclock.h"
#include "ntp_control.h"
#include "ntp_unixtime.h"
#include "ntp_stdlib.h"
#include "ntp_config.h"
#include "ntp_crypto.h"
#include "ntp_assert.h"
#include "ntp_leapsec.h"
#include "ntp_md5.h"	/* provides OpenSSL digest API */
#include "lib_strbuf.h"
#include <rc_cmdlength.h>
#ifdef KERNEL_PLL
# include "ntp_syscall.h"
#endif

/*
 * Structure to hold request procedure information
 */

struct ctl_proc {
	short control_code;		/* defined request code */
#define NO_REQUEST	(-1)
	u_short flags;			/* flags word */
	/* Only one flag.  Authentication required or not. */
#define NOAUTH	0
#define AUTH	1
	void (*handler) (struct recvbuf *, int); /* handle request */
};


/*
 * Request processing routines
 */
static	void	ctl_error	(u_char);
#ifdef REFCLOCK
static	u_short ctlclkstatus	(struct refclockstat *);
#endif
static	void	ctl_flushpkt	(u_char);
static	void	ctl_putdata	(const char *, unsigned int, int);
static	void	ctl_putstr	(const char *, const char *, size_t);
static	void	ctl_putdblf	(const char *, int, int, double);
#define	ctl_putdbl(tag, d)	ctl_putdblf(tag, 1, 3, d)
#define	ctl_putdbl6(tag, d)	ctl_putdblf(tag, 1, 6, d)
#define	ctl_putsfp(tag, sfp)	ctl_putdblf(tag, 0, -1, \
					    FPTOD(sfp))
static	void	ctl_putuint	(const char *, u_long);
static	void	ctl_puthex	(const char *, u_long);
static	void	ctl_putint	(const char *, long);
static	void	ctl_putts	(const char *, l_fp *);
static	void	ctl_putadr	(const char *, u_int32,
				 sockaddr_u *);
static	void	ctl_putrefid	(const char *, u_int32);
static	void	ctl_putarray	(const char *, double *, int);
static	void	ctl_putsys	(int);
static	void	ctl_putpeer	(int, struct peer *);
static	void	ctl_putfs	(const char *, tstamp_t);
static	void	ctl_printf	(const char *, ...) NTP_PRINTF(1, 2);
#ifdef REFCLOCK
static	void	ctl_putclock	(int, struct refclockstat *, int);
#endif	/* REFCLOCK */
static	const struct ctl_var *ctl_getitem(const struct ctl_var *,
					  char **);
static	u_short	count_var	(const struct ctl_var *);
static	void	control_unspec	(struct recvbuf *, int);
static	void	read_status	(struct recvbuf *, int);
static	void	read_sysvars	(void);
static	void	read_peervars	(void);
static	void	read_variables	(struct recvbuf *, int);
static	void	write_variables (struct recvbuf *, int);
static	void	read_clockstatus(struct recvbuf *, int);
static	void	write_clockstatus(struct recvbuf *, int);
static	void	set_trap	(struct recvbuf *, int);
static	void	save_config	(struct recvbuf *, int);
static	void	configure	(struct recvbuf *, int);
static	void	send_mru_entry	(mon_entry *, int);
static	void	send_random_tag_value(int);
static	void	read_mru_list	(struct recvbuf *, int);
static	void	send_ifstats_entry(endpt *, u_int);
static	void	read_ifstats	(struct recvbuf *);
static	void	sockaddrs_from_restrict_u(sockaddr_u *,	sockaddr_u *,
					  restrict_u *, int);
static	void	send_restrict_entry(restrict_u *, int, u_int);
static	void	send_restrict_list(restrict_u *, int, u_int *);
static	void	read_addr_restrictions(struct recvbuf *);
static	void	read_ordlist	(struct recvbuf *, int);
static	u_int32	derive_nonce	(sockaddr_u *, u_int32, u_int32);
static	void	generate_nonce	(struct recvbuf *, char *, size_t);
static	int	validate_nonce	(const char *, struct recvbuf *);
static	void	req_nonce	(struct recvbuf *, int);
static	void	unset_trap	(struct recvbuf *, int);
static	struct ctl_trap *ctlfindtrap(sockaddr_u *,
				     struct interface *);

int/*BOOL*/ is_safe_filename(const char * name);

static const struct ctl_proc control_codes[] = {
	{ CTL_OP_UNSPEC,		NOAUTH,	control_unspec },
	{ CTL_OP_READSTAT,		NOAUTH,	read_status },
	{ CTL_OP_READVAR,		NOAUTH,	read_variables },
	{ CTL_OP_WRITEVAR,		AUTH,	write_variables },
	{ CTL_OP_READCLOCK,		NOAUTH,	read_clockstatus },
	{ CTL_OP_WRITECLOCK,		AUTH,	write_clockstatus },
	{ CTL_OP_SETTRAP,		AUTH,	set_trap },
	{ CTL_OP_CONFIGURE,		AUTH,	configure },
	{ CTL_OP_SAVECONFIG,		AUTH,	save_config },
	{ CTL_OP_READ_MRU,		NOAUTH,	read_mru_list },
	{ CTL_OP_READ_ORDLIST_A,	AUTH,	read_ordlist },
	{ CTL_OP_REQ_NONCE,		NOAUTH,	req_nonce },
	{ CTL_OP_UNSETTRAP,		AUTH,	unset_trap },
	{ NO_REQUEST,			0,	NULL }
};

/*
 * System variables we understand
 */
#define	CS_LEAP			1
#define	CS_STRATUM		2
#define	CS_PRECISION		3
#define	CS_ROOTDELAY		4
#define	CS_ROOTDISPERSION	5
#define	CS_REFID		6
#define	CS_REFTIME		7
#define	CS_POLL			8
#define	CS_PEERID		9
#define	CS_OFFSET		10
#define	CS_DRIFT		11
#define	CS_JITTER		12
#define	CS_ERROR		13
#define	CS_CLOCK		14
#define	CS_PROCESSOR		15
#define	CS_SYSTEM		16
#define	CS_VERSION		17
#define	CS_STABIL		18
#define	CS_VARLIST		19
#define	CS_TAI			20
#define	CS_LEAPTAB		21
#define	CS_LEAPEND		22
#define	CS_RATE			23
#define	CS_MRU_ENABLED		24
#define	CS_MRU_DEPTH		25
#define	CS_MRU_DEEPEST		26
#define	CS_MRU_MINDEPTH		27
#define	CS_MRU_MAXAGE		28
#define	CS_MRU_MAXDEPTH		29
#define	CS_MRU_MEM		30
#define	CS_MRU_MAXMEM		31
#define	CS_SS_UPTIME		32
#define	CS_SS_RESET		33
#define	CS_SS_RECEIVED		34
#define	CS_SS_THISVER		35
#define	CS_SS_OLDVER		36
#define	CS_SS_BADFORMAT		37
#define	CS_SS_BADAUTH		38
#define	CS_SS_DECLINED		39
#define	CS_SS_RESTRICTED	40
#define	CS_SS_LIMITED		41
#define	CS_SS_KODSENT		42
#define	CS_SS_PROCESSED		43
#define	CS_SS_LAMPORT		44
#define	CS_SS_TSROUNDING	45
#define	CS_PEERADR		46
#define	CS_PEERMODE		47
#define	CS_BCASTDELAY		48
#define	CS_AUTHDELAY		49
#define	CS_AUTHKEYS		50
#define	CS_AUTHFREEK		51
#define	CS_AUTHKLOOKUPS		52
#define	CS_AUTHKNOTFOUND	53
#define	CS_AUTHKUNCACHED	54
#define	CS_AUTHKEXPIRED		55
#define	CS_AUTHENCRYPTS		56
#define	CS_AUTHDECRYPTS		57
#define	CS_AUTHRESET		58
#define	CS_K_OFFSET		59
#define	CS_K_FREQ		60
#define	CS_K_MAXERR		61
#define	CS_K_ESTERR		62
#define	CS_K_STFLAGS		63
#define	CS_K_TIMECONST		64
#define	CS_K_PRECISION		65
#define	CS_K_FREQTOL		66
#define	CS_K_PPS_FREQ		67
#define	CS_K_PPS_STABIL		68
#define	CS_K_PPS_JITTER		69
#define	CS_K_PPS_CALIBDUR	70
#define	CS_K_PPS_CALIBS		71
#define	CS_K_PPS_CALIBERRS	72
#define	CS_K_PPS_JITEXC		73
#define	CS_K_PPS_STBEXC		74
#define	CS_KERN_FIRST		CS_K_OFFSET
#define	CS_KERN_LAST		CS_K_PPS_STBEXC
#define	CS_IOSTATS_RESET	75
#define	CS_TOTAL_RBUF		76
#define	CS_FREE_RBUF		77
#define	CS_USED_RBUF		78
#define	CS_RBUF_LOWATER		79
#define	CS_IO_DROPPED		80
#define	CS_IO_IGNORED		81
#define	CS_IO_RECEIVED		82
#define	CS_IO_SENT		83
#define	CS_IO_SENDFAILED	84
#define	CS_IO_WAKEUPS		85
#define	CS_IO_GOODWAKEUPS	86
#define	CS_TIMERSTATS_RESET	87
#define	CS_TIMER_OVERRUNS	88
#define	CS_TIMER_XMTS		89
#define	CS_FUZZ			90
#define	CS_WANDER_THRESH	91
#define	CS_LEAPSMEARINTV	92
#define	CS_LEAPSMEAROFFS	93
#define	CS_MAX_NOAUTOKEY	CS_LEAPSMEAROFFS
#ifdef AUTOKEY
#define	CS_FLAGS		(1 + CS_MAX_NOAUTOKEY)
#define	CS_HOST			(2 + CS_MAX_NOAUTOKEY)
#define	CS_PUBLIC		(3 + CS_MAX_NOAUTOKEY)
#define	CS_CERTIF		(4 + CS_MAX_NOAUTOKEY)
#define	CS_SIGNATURE		(5 + CS_MAX_NOAUTOKEY)
#define	CS_REVTIME		(6 + CS_MAX_NOAUTOKEY)
#define	CS_IDENT		(7 + CS_MAX_NOAUTOKEY)
#define	CS_DIGEST		(8 + CS_MAX_NOAUTOKEY)
#define	CS_MAXCODE		CS_DIGEST
#else	/* !AUTOKEY follows */
#define	CS_MAXCODE		CS_MAX_NOAUTOKEY
#endif	/* !AUTOKEY */

/*
 * Peer variables we understand
 */
#define	CP_CONFIG		1
#define	CP_AUTHENABLE		2
#define	CP_AUTHENTIC		3
#define	CP_SRCADR		4
#define	CP_SRCPORT		5
#define	CP_DSTADR		6
#define	CP_DSTPORT		7
#define	CP_LEAP			8
#define	CP_HMODE		9
#define	CP_STRATUM		10
#define	CP_PPOLL		11
#define	CP_HPOLL		12
#define	CP_PRECISION		13
#define	CP_ROOTDELAY		14
#define	CP_ROOTDISPERSION	15
#define	CP_REFID		16
#define	CP_REFTIME		17
#define	CP_ORG			18
#define	CP_REC			19
#define	CP_XMT			20
#define	CP_REACH		21
#define	CP_UNREACH		22
#define	CP_TIMER		23
#define	CP_DELAY		24
#define	CP_OFFSET		25
#define	CP_JITTER		26
#define	CP_DISPERSION		27
#define	CP_KEYID		28
#define	CP_FILTDELAY		29
#define	CP_FILTOFFSET		30
#define	CP_PMODE		31
#define	CP_RECEIVED		32
#define	CP_SENT			33
#define	CP_FILTERROR		34
#define	CP_FLASH		35
#define	CP_TTL			36
#define	CP_VARLIST		37
#define	CP_IN			38
#define	CP_OUT			39
#define	CP_RATE			40
#define	CP_BIAS			41
#define	CP_SRCHOST		42
#define	CP_TIMEREC		43
#define	CP_TIMEREACH		44
#define	CP_BADAUTH		45
#define	CP_BOGUSORG		46
#define	CP_OLDPKT		47
#define	CP_SELDISP		48
#define	CP_SELBROKEN		49
#define	CP_CANDIDATE		50
#define	CP_MAX_NOAUTOKEY	CP_CANDIDATE
#ifdef AUTOKEY
#define	CP_FLAGS		(1 + CP_MAX_NOAUTOKEY)
#define	CP_HOST			(2 + CP_MAX_NOAUTOKEY)
#define	CP_VALID		(3 + CP_MAX_NOAUTOKEY)
#define	CP_INITSEQ		(4 + CP_MAX_NOAUTOKEY)
#define	CP_INITKEY		(5 + CP_MAX_NOAUTOKEY)
#define	CP_INITTSP		(6 + CP_MAX_NOAUTOKEY)
#define	CP_SIGNATURE		(7 + CP_MAX_NOAUTOKEY)
#define	CP_IDENT		(8 + CP_MAX_NOAUTOKEY)
#define	CP_MAXCODE		CP_IDENT
#else	/* !AUTOKEY follows */
#define	CP_MAXCODE		CP_MAX_NOAUTOKEY
#endif	/* !AUTOKEY */

/*
 * Clock variables we understand
 */
#define	CC_TYPE		1
#define	CC_TIMECODE	2
#define	CC_POLL		3
#define	CC_NOREPLY	4
#define	CC_BADFORMAT	5
#define	CC_BADDATA	6
#define	CC_FUDGETIME1	7
#define	CC_FUDGETIME2	8
#define	CC_FUDGEVAL1	9
#define	CC_FUDGEVAL2	10
#define	CC_FLAGS	11
#define	CC_DEVICE	12
#define	CC_VARLIST	13
#define	CC_MAXCODE	CC_VARLIST

/*
 * System variable values. The array can be indexed by the variable
 * index to find the textual name.
 */
static const struct ctl_var sys_var[] = {
	{ 0,		PADDING, "" },		/* 0 */
	{ CS_LEAP,	RW, "leap" },		/* 1 */
	{ CS_STRATUM,	RO, "stratum" },	/* 2 */
	{ CS_PRECISION, RO, "precision" },	/* 3 */
	{ CS_ROOTDELAY, RO, "rootdelay" },	/* 4 */
	{ CS_ROOTDISPERSION, RO, "rootdisp" },	/* 5 */
	{ CS_REFID,	RO, "refid" },		/* 6 */
	{ CS_REFTIME,	RO, "reftime" },	/* 7 */
	{ CS_POLL,	RO, "tc" },		/* 8 */
	{ CS_PEERID,	RO, "peer" },		/* 9 */
	{ CS_OFFSET,	RO, "offset" },		/* 10 */
	{ CS_DRIFT,	RO, "frequency" },	/* 11 */
	{ CS_JITTER,	RO, "sys_jitter" },	/* 12 */
	{ CS_ERROR,	RO, "clk_jitter" },	/* 13 */
	{ CS_CLOCK,	RO, "clock" },		/* 14 */
	{ CS_PROCESSOR, RO, "processor" },	/* 15 */
	{ CS_SYSTEM,	RO, "system" },		/* 16 */
	{ CS_VERSION,	RO, "version" },	/* 17 */
	{ CS_STABIL,	RO, "clk_wander" },	/* 18 */
	{ CS_VARLIST,	RO, "sys_var_list" },	/* 19 */
	{ CS_TAI,	RO, "tai" },		/* 20 */
	{ CS_LEAPTAB,	RO, "leapsec" },	/* 21 */
	{ CS_LEAPEND,	RO, "expire" },		/* 22 */
	{ CS_RATE,	RO, "mintc" },		/* 23 */
	{ CS_MRU_ENABLED,	RO, "mru_enabled" },	/* 24 */
	{ CS_MRU_DEPTH,		RO, "mru_depth" },	/* 25 */
	{ CS_MRU_DEEPEST,	RO, "mru_deepest" },	/* 26 */
	{ CS_MRU_MINDEPTH,	RO, "mru_mindepth" },	/* 27 */
	{ CS_MRU_MAXAGE,	RO, "mru_maxage" },	/* 28 */
	{ CS_MRU_MAXDEPTH,	RO, "mru_maxdepth" },	/* 29 */
	{ CS_MRU_MEM,		RO, "mru_mem" },	/* 30 */
	{ CS_MRU_MAXMEM,	RO, "mru_maxmem" },	/* 31 */
	{ CS_SS_UPTIME,		RO, "ss_uptime" },	/* 32 */
	{ CS_SS_RESET,		RO, "ss_reset" },	/* 33 */
	{ CS_SS_RECEIVED,	RO, "ss_received" },	/* 34 */
	{ CS_SS_THISVER,	RO, "ss_thisver" },	/* 35 */
	{ CS_SS_OLDVER,		RO, "ss_oldver" },	/* 36 */
	{ CS_SS_BADFORMAT,	RO, "ss_badformat" },	/* 37 */
	{ CS_SS_BADAUTH,	RO, "ss_badauth" },	/* 38 */
	{ CS_SS_DECLINED,	RO, "ss_declined" },	/* 39 */
	{ CS_SS_RESTRICTED,	RO, "ss_restricted" },	/* 40 */
	{ CS_SS_LIMITED,	RO, "ss_limited" },	/* 41 */
	{ CS_SS_KODSENT,	RO, "ss_kodsent" },	/* 42 */
	{ CS_SS_PROCESSED,	RO, "ss_processed" },	/* 43 */
	{ CS_SS_LAMPORT,	RO, "ss_lamport" },	/* 44 */
	{ CS_SS_TSROUNDING,	RO, "ss_tsrounding" },	/* 45 */
	{ CS_PEERADR,		RO, "peeradr" },	/* 46 */
	{ CS_PEERMODE,		RO, "peermode" },	/* 47 */
	{ CS_BCASTDELAY,	RO, "bcastdelay" },	/* 48 */
	{ CS_AUTHDELAY,		RO, "authdelay" },	/* 49 */
	{ CS_AUTHKEYS,		RO, "authkeys" },	/* 50 */
	{ CS_AUTHFREEK,		RO, "authfreek" },	/* 51 */
	{ CS_AUTHKLOOKUPS,	RO, "authklookups" },	/* 52 */
	{ CS_AUTHKNOTFOUND,	RO, "authknotfound" },	/* 53 */
	{ CS_AUTHKUNCACHED,	RO, "authkuncached" },	/* 54 */
	{ CS_AUTHKEXPIRED,	RO, "authkexpired" },	/* 55 */
	{ CS_AUTHENCRYPTS,	RO, "authencrypts" },	/* 56 */
	{ CS_AUTHDECRYPTS,	RO, "authdecrypts" },	/* 57 */
	{ CS_AUTHRESET,		RO, "authreset" },	/* 58 */
	{ CS_K_OFFSET,		RO, "koffset" },	/* 59 */
	{ CS_K_FREQ,		RO, "kfreq" },		/* 60 */
	{ CS_K_MAXERR,		RO, "kmaxerr" },	/* 61 */
	{ CS_K_ESTERR,		RO, "kesterr" },	/* 62 */
	{ CS_K_STFLAGS,		RO, "kstflags" },	/* 63 */
	{ CS_K_TIMECONST,	RO, "ktimeconst" },	/* 64 */
	{ CS_K_PRECISION,	RO, "kprecis" },	/* 65 */
	{ CS_K_FREQTOL,		RO, "kfreqtol" },	/* 66 */
	{ CS_K_PPS_FREQ,	RO, "kppsfreq" },	/* 67 */
	{ CS_K_PPS_STABIL,	RO, "kppsstab" },	/* 68 */
	{ CS_K_PPS_JITTER,	RO, "kppsjitter" },	/* 69 */
	{ CS_K_PPS_CALIBDUR,	RO, "kppscalibdur" },	/* 70 */
	{ CS_K_PPS_CALIBS,	RO, "kppscalibs" },	/* 71 */
	{ CS_K_PPS_CALIBERRS,	RO, "kppscaliberrs" },	/* 72 */
	{ CS_K_PPS_JITEXC,	RO, "kppsjitexc" },	/* 73 */
	{ CS_K_PPS_STBEXC,	RO, "kppsstbexc" },	/* 74 */
	{ CS_IOSTATS_RESET,	RO, "iostats_reset" },	/* 75 */
	{ CS_TOTAL_RBUF,	RO, "total_rbuf" },	/* 76 */
	{ CS_FREE_RBUF,		RO, "free_rbuf" },	/* 77 */
	{ CS_USED_RBUF,		RO, "used_rbuf" },	/* 78 */
	{ CS_RBUF_LOWATER,	RO, "rbuf_lowater" },	/* 79 */
	{ CS_IO_DROPPED,	RO, "io_dropped" },	/* 80 */
	{ CS_IO_IGNORED,	RO, "io_ignored" },	/* 81 */
	{ CS_IO_RECEIVED,	RO, "io_received" },	/* 82 */
	{ CS_IO_SENT,		RO, "io_sent" },	/* 83 */
	{ CS_IO_SENDFAILED,	RO, "io_sendfailed" },	/* 84 */
	{ CS_IO_WAKEUPS,	RO, "io_wakeups" },	/* 85 */
	{ CS_IO_GOODWAKEUPS,	RO, "io_goodwakeups" },	/* 86 */
	{ CS_TIMERSTATS_RESET,	RO, "timerstats_reset" },/* 87 */
	{ CS_TIMER_OVERRUNS,	RO, "timer_overruns" },	/* 88 */
	{ CS_TIMER_XMTS,	RO, "timer_xmts" },	/* 89 */
	{ CS_FUZZ,		RO, "fuzz" },		/* 90 */
	{ CS_WANDER_THRESH,	RO, "clk_wander_threshold" }, /* 91 */

	{ CS_LEAPSMEARINTV,	RO, "leapsmearinterval" },    /* 92 */
	{ CS_LEAPSMEAROFFS,	RO, "leapsmearoffset" },      /* 93 */

#ifdef AUTOKEY
	{ CS_FLAGS,	RO, "flags" },		/* 1 + CS_MAX_NOAUTOKEY */
	{ CS_HOST,	RO, "host" },		/* 2 + CS_MAX_NOAUTOKEY */
	{ CS_PUBLIC,	RO, "update" },		/* 3 + CS_MAX_NOAUTOKEY */
	{ CS_CERTIF,	RO, "cert" },		/* 4 + CS_MAX_NOAUTOKEY */
	{ CS_SIGNATURE,	RO, "signature" },	/* 5 + CS_MAX_NOAUTOKEY */
	{ CS_REVTIME,	RO, "until" },		/* 6 + CS_MAX_NOAUTOKEY */
	{ CS_IDENT,	RO, "ident" },		/* 7 + CS_MAX_NOAUTOKEY */
	{ CS_DIGEST,	RO, "digest" },		/* 8 + CS_MAX_NOAUTOKEY */
#endif	/* AUTOKEY */
	{ 0,		EOV, "" }		/* 94/102 */
};

static struct ctl_var *ext_sys_var = NULL;

/*
 * System variables we print by default (in fuzzball order,
 * more-or-less)
 */
static const u_char def_sys_var[] = {
	CS_VERSION,
	CS_PROCESSOR,
	CS_SYSTEM,
	CS_LEAP,
	CS_STRATUM,
	CS_PRECISION,
	CS_ROOTDELAY,
	CS_ROOTDISPERSION,
	CS_REFID,
	CS_REFTIME,
	CS_CLOCK,
	CS_PEERID,
	CS_POLL,
	CS_RATE,
	CS_OFFSET,
	CS_DRIFT,
	CS_JITTER,
	CS_ERROR,
	CS_STABIL,
	CS_TAI,
	CS_LEAPTAB,
	CS_LEAPEND,
	CS_LEAPSMEARINTV,
	CS_LEAPSMEAROFFS,
#ifdef AUTOKEY
	CS_HOST,
	CS_IDENT,
	CS_FLAGS,
	CS_DIGEST,
	CS_SIGNATURE,
	CS_PUBLIC,
	CS_CERTIF,
#endif	/* AUTOKEY */
	0
};


/*
 * Peer variable list
 */
static const struct ctl_var peer_var[] = {
	{ 0,		PADDING, "" },		/* 0 */
	{ CP_CONFIG,	RO, "config" },		/* 1 */
	{ CP_AUTHENABLE, RO,	"authenable" },	/* 2 */
	{ CP_AUTHENTIC, RO, "authentic" },	/* 3 */
	{ CP_SRCADR,	RO, "srcadr" },		/* 4 */
	{ CP_SRCPORT,	RO, "srcport" },	/* 5 */
	{ CP_DSTADR,	RO, "dstadr" },		/* 6 */
	{ CP_DSTPORT,	RO, "dstport" },	/* 7 */
	{ CP_LEAP,	RO, "leap" },		/* 8 */
	{ CP_HMODE,	RO, "hmode" },		/* 9 */
	{ CP_STRATUM,	RO, "stratum" },	/* 10 */
	{ CP_PPOLL,	RO, "ppoll" },		/* 11 */
	{ CP_HPOLL,	RO, "hpoll" },		/* 12 */
	{ CP_PRECISION,	RO, "precision" },	/* 13 */
	{ CP_ROOTDELAY,	RO, "rootdelay" },	/* 14 */
	{ CP_ROOTDISPERSION, RO, "rootdisp" },	/* 15 */
	{ CP_REFID,	RO, "refid" },		/* 16 */
	{ CP_REFTIME,	RO, "reftime" },	/* 17 */
	{ CP_ORG,	RO, "org" },		/* 18 */
	{ CP_REC,	RO, "rec" },		/* 19 */
	{ CP_XMT,	RO, "xleave" },		/* 20 */
	{ CP_REACH,	RO, "reach" },		/* 21 */
	{ CP_UNREACH,	RO, "unreach" },	/* 22 */
	{ CP_TIMER,	RO, "timer" },		/* 23 */
	{ CP_DELAY,	RO, "delay" },		/* 24 */
	{ CP_OFFSET,	RO, "offset" },		/* 25 */
	{ CP_JITTER,	RO, "jitter" },		/* 26 */
	{ CP_DISPERSION, RO, "dispersion" },	/* 27 */
	{ CP_KEYID,	RO, "keyid" },		/* 28 */
	{ CP_FILTDELAY,	RO, "filtdelay" },	/* 29 */
	{ CP_FILTOFFSET, RO, "filtoffset" },	/* 30 */
	{ CP_PMODE,	RO, "pmode" },		/* 31 */
	{ CP_RECEIVED,	RO, "received"},	/* 32 */
	{ CP_SENT,	RO, "sent" },		/* 33 */
	{ CP_FILTERROR,	RO, "filtdisp" },	/* 34 */
	{ CP_FLASH,	RO, "flash" },		/* 35 */
	{ CP_TTL,	RO, "ttl" },		/* 36 */
	{ CP_VARLIST,	RO, "peer_var_list" },	/* 37 */
	{ CP_IN,	RO, "in" },		/* 38 */
	{ CP_OUT,	RO, "out" },		/* 39 */
	{ CP_RATE,	RO, "headway" },	/* 40 */
	{ CP_BIAS,	RO, "bias" },		/* 41 */
	{ CP_SRCHOST,	RO, "srchost" },	/* 42 */
	{ CP_TIMEREC,	RO, "timerec" },	/* 43 */
	{ CP_TIMEREACH,	RO, "timereach" },	/* 44 */
	{ CP_BADAUTH,	RO, "badauth" },	/* 45 */
	{ CP_BOGUSORG,	RO, "bogusorg" },	/* 46 */
	{ CP_OLDPKT,	RO, "oldpkt" },		/* 47 */
	{ CP_SELDISP,	RO, "seldisp" },	/* 48 */
	{ CP_SELBROKEN,	RO, "selbroken" },	/* 49 */
	{ CP_CANDIDATE, RO, "candidate" },	/* 50 */
#ifdef AUTOKEY
	{ CP_FLAGS,	RO, "flags" },		/* 1 + CP_MAX_NOAUTOKEY */
	{ CP_HOST,	RO, "host" },		/* 2 + CP_MAX_NOAUTOKEY */
	{ CP_VALID,	RO, "valid" },		/* 3 + CP_MAX_NOAUTOKEY */
	{ CP_INITSEQ,	RO, "initsequence" },	/* 4 + CP_MAX_NOAUTOKEY */
	{ CP_INITKEY,	RO, "initkey" },	/* 5 + CP_MAX_NOAUTOKEY */
	{ CP_INITTSP,	RO, "timestamp" },	/* 6 + CP_MAX_NOAUTOKEY */
	{ CP_SIGNATURE,	RO, "signature" },	/* 7 + CP_MAX_NOAUTOKEY */
	{ CP_IDENT,	RO, "ident" },		/* 8 + CP_MAX_NOAUTOKEY */
#endif	/* AUTOKEY */
	{ 0,		EOV, "" }		/* 50/58 */
};


/*
 * Peer variables we print by default
 */
static const u_char def_peer_var[] = {
	CP_SRCADR,
	CP_SRCPORT,
	CP_SRCHOST,
	CP_DSTADR,
	CP_DSTPORT,
	CP_OUT,
	CP_IN,
	CP_LEAP,
	CP_STRATUM,
	CP_PRECISION,
	CP_ROOTDELAY,
	CP_ROOTDISPERSION,
	CP_REFID,
	CP_REFTIME,
	CP_REC,
	CP_REACH,
	CP_UNREACH,
	CP_HMODE,
	CP_PMODE,
	CP_HPOLL,
	CP_PPOLL,
	CP_RATE,
	CP_FLASH,
	CP_KEYID,
	CP_TTL,
	CP_OFFSET,
	CP_DELAY,
	CP_DISPERSION,
	CP_JITTER,
	CP_XMT,
	CP_BIAS,
	CP_FILTDELAY,
	CP_FILTOFFSET,
	CP_FILTERROR,
#ifdef AUTOKEY
	CP_HOST,
	CP_FLAGS,
	CP_SIGNATURE,
	CP_VALID,
	CP_INITSEQ,
	CP_IDENT,
#endif	/* AUTOKEY */
	0
};


#ifdef REFCLOCK
/*
 * Clock variable list
 */
static const struct ctl_var clock_var[] = {
	{ 0,		PADDING, "" },		/* 0 */
	{ CC_TYPE,	RO, "type" },		/* 1 */
	{ CC_TIMECODE,	RO, "timecode" },	/* 2 */
	{ CC_POLL,	RO, "poll" },		/* 3 */
	{ CC_NOREPLY,	RO, "noreply" },	/* 4 */
	{ CC_BADFORMAT, RO, "badformat" },	/* 5 */
	{ CC_BADDATA,	RO, "baddata" },	/* 6 */
	{ CC_FUDGETIME1, RO, "fudgetime1" },	/* 7 */
	{ CC_FUDGETIME2, RO, "fudgetime2" },	/* 8 */
	{ CC_FUDGEVAL1, RO, "stratum" },	/* 9 */
	{ CC_FUDGEVAL2, RO, "refid" },		/* 10 */
	{ CC_FLAGS,	RO, "flags" },		/* 11 */
	{ CC_DEVICE,	RO, "device" },		/* 12 */
	{ CC_VARLIST,	RO, "clock_var_list" },	/* 13 */
	{ 0,		EOV, ""  }		/* 14 */
};


/*
 * Clock variables printed by default
 */
static const u_char def_clock_var[] = {
	CC_DEVICE,
	CC_TYPE,	/* won't be output if device = known */
	CC_TIMECODE,
	CC_POLL,
	CC_NOREPLY,
	CC_BADFORMAT,
	CC_BADDATA,
	CC_FUDGETIME1,
	CC_FUDGETIME2,
	CC_FUDGEVAL1,
	CC_FUDGEVAL2,
	CC_FLAGS,
	0
};
#endif

/*
 * MRU string constants shared by send_mru_entry() and read_mru_list().
 */
static const char addr_fmt[] =		"addr.%d";
static const char last_fmt[] =		"last.%d";

/*
 * System and processor definitions.
 */
#ifndef HAVE_UNAME
# ifndef STR_SYSTEM
#  define		STR_SYSTEM	"UNIX"
# endif
# ifndef STR_PROCESSOR
#  define		STR_PROCESSOR	"unknown"
# endif

static const char str_system[] = STR_SYSTEM;
static const char str_processor[] = STR_PROCESSOR;
#else
# include <sys/utsname.h>
static struct utsname utsnamebuf;
#endif /* HAVE_UNAME */

/*
 * Trap structures. We only allow a few of these, and send a copy of
 * each async message to each live one. Traps time out after an hour, it
 * is up to the trap receipient to keep resetting it to avoid being
 * timed out.
 */
/* ntp_request.c */
struct ctl_trap ctl_traps[CTL_MAXTRAPS];
int num_ctl_traps;

/*
 * Type bits, for ctlsettrap() call.
 */
#define TRAP_TYPE_CONFIG	0	/* used by configuration code */
#define TRAP_TYPE_PRIO		1	/* priority trap */
#define TRAP_TYPE_NONPRIO	2	/* nonpriority trap */


/*
 * List relating reference clock types to control message time sources.
 * Index by the reference clock type. This list will only be used iff
 * the reference clock driver doesn't set peer->sstclktype to something
 * different than CTL_SST_TS_UNSPEC.
 */
#ifdef REFCLOCK
static const u_char clocktypes[] = {
	CTL_SST_TS_NTP,		/* REFCLK_NONE (0) */
	CTL_SST_TS_LOCAL,	/* REFCLK_LOCALCLOCK (1) */
	CTL_SST_TS_UHF,		/* deprecated REFCLK_GPS_TRAK (2) */
	CTL_SST_TS_HF,		/* REFCLK_WWV_PST (3) */
	CTL_SST_TS_LF,		/* REFCLK_WWVB_SPECTRACOM (4) */
	CTL_SST_TS_UHF,		/* REFCLK_TRUETIME (5) */
	CTL_SST_TS_UHF,		/* REFCLK_IRIG_AUDIO (6) */
	CTL_SST_TS_HF,		/* REFCLK_CHU (7) */
	CTL_SST_TS_LF,		/* REFCLOCK_PARSE (default) (8) */
	CTL_SST_TS_LF,		/* REFCLK_GPS_MX4200 (9) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_AS2201 (10) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_ARBITER (11) */
	CTL_SST_TS_UHF,		/* REFCLK_IRIG_TPRO (12) */
	CTL_SST_TS_ATOM,	/* REFCLK_ATOM_LEITCH (13) */
	CTL_SST_TS_LF,		/* deprecated REFCLK_MSF_EES (14) */
	CTL_SST_TS_NTP,		/* not used (15) */
	CTL_SST_TS_UHF,		/* REFCLK_IRIG_BANCOMM (16) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_DATU (17) */
	CTL_SST_TS_TELEPHONE,	/* REFCLK_NIST_ACTS (18) */
	CTL_SST_TS_HF,		/* REFCLK_WWV_HEATH (19) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_NMEA (20) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_VME (21) */
	CTL_SST_TS_ATOM,	/* REFCLK_ATOM_PPS (22) */
	CTL_SST_TS_NTP,		/* not used (23) */
	CTL_SST_TS_NTP,		/* not used (24) */
	CTL_SST_TS_NTP,		/* not used (25) */
	CTL_SST_TS_UHF,		/* REFCLK_GPS_HP (26) */
	CTL_SST_TS_LF,		/* REFCLK_ARCRON_MSF (27) */
	CTL_SST_TS_UHF,		/* REFCLK_SHM (28) */
	CTL_SST_TS_UHF,		/* REFCLK_PALISADE (29) */
	CTL_SST_TS_UHF,		/* REFCLK_ONCORE (30) */
	CTL_SST_TS_UHF,		/* REFCLK_JUPITER (31) */
	CTL_SST_TS_LF,		/* REFCLK_CHRONOLOG (32) */
	CTL_SST_TS_LF,		/* REFCLK_DUMBCLOCK (33) */
	CTL_SST_TS_LF,		/* REFCLK_ULINK (34) */
	CTL_SST_TS_LF,		/* REFCLK_PCF (35) */
	CTL_SST_TS_HF,		/* REFCLK_WWV (36) */
	CTL_SST_TS_LF,		/* REFCLK_FG (37) */
	CTL_SST_TS_UHF,		/* REFCLK_HOPF_SERIAL (38) */
	CTL_SST_TS_UHF,		/* REFCLK_HOPF_PCI (39) */
	CTL_SST_TS_LF,		/* REFCLK_JJY (40) */
	CTL_SST_TS_UHF,		/* REFCLK_TT560 (41) */
	CTL_SST_TS_UHF,		/* REFCLK_ZYFER (42) */
	CTL_SST_TS_UHF,		/* REFCLK_RIPENCC (43) */
	CTL_SST_TS_UHF,		/* REFCLK_NEOCLOCK4X (44) */
	CTL_SST_TS_UHF,		/* REFCLK_TSYNCPCI (45) */
	CTL_SST_TS_UHF		/* REFCLK_GPSDJSON (46) */
};
#endif  /* REFCLOCK */


/*
 * Keyid used for authenticating write requests.
 */
keyid_t ctl_auth_keyid;

/*
 * We keep track of the last error reported by the system internally
 */
static	u_char ctl_sys_last_event;
static	u_char ctl_sys_num_events;


/*
 * Statistic counters to keep track of requests and responses.
 */
u_long ctltimereset;		/* time stats reset */
u_long numctlreq;		/* number of requests we've received */
u_long numctlbadpkts;		/* number of bad control packets */
u_long numctlresponses;		/* number of resp packets sent with data */
u_long numctlfrags;		/* number of fragments sent */
u_long numctlerrors;		/* number of error responses sent */
u_long numctltooshort;		/* number of too short input packets */
u_long numctlinputresp;		/* number of responses on input */
u_long numctlinputfrag;		/* number of fragments on input */
u_long numctlinputerr;		/* number of input pkts with err bit set */
u_long numctlbadoffset;		/* number of input pkts with nonzero offset */
u_long numctlbadversion;	/* number of input pkts with unknown version */
u_long numctldatatooshort;	/* data too short for count */
u_long numctlbadop;		/* bad op code found in packet */
u_long numasyncmsgs;		/* number of async messages we've sent */

/*
 * Response packet used by these routines. Also some state information
 * so that we can handle packet formatting within a common set of
 * subroutines.  Note we try to enter data in place whenever possible,
 * but the need to set the more bit correctly means we occasionally
 * use the extra buffer and copy.
 */
static struct ntp_control rpkt;
static u_char	res_version;
static u_char	res_opcode;
static associd_t res_associd;
static u_short	res_frags;	/* datagrams in this response */
static int	res_offset;	/* offset of payload in response */
static u_char * datapt;
static u_char * dataend;
static int	datalinelen;
static int	datasent;	/* flag to avoid initial ", " */
static int	datanotbinflag;
static sockaddr_u *rmt_addr;
static struct interface *lcl_inter;

static u_char	res_authenticate;
static u_char	res_authokay;
static keyid_t	res_keyid;

#define MAXDATALINELEN	(72)

static u_char	res_async;	/* sending async trap response? */

/*
 * Pointers for saving state when decoding request packets
 */
static	char *reqpt;
static	char *reqend;

#ifndef MIN
#define MIN(a, b) (((a) <= (b)) ? (a) : (b))
#endif

/*
 * init_control - initialize request data
 */
void
init_control(void)
{
	size_t i;

#ifdef HAVE_UNAME
	uname(&utsnamebuf);
#endif /* HAVE_UNAME */

	ctl_clr_stats();

	ctl_auth_keyid = 0;
	ctl_sys_last_event = EVNT_UNSPEC;
	ctl_sys_num_events = 0;

	num_ctl_traps = 0;
	for (i = 0; i < COUNTOF(ctl_traps); i++)
		ctl_traps[i].tr_flags = 0;
}


/*
 * ctl_error - send an error response for the current request
 */
static void
ctl_error(
	u_char errcode
	)
{
	size_t		maclen;

	numctlerrors++;
	DPRINTF(3, ("sending control error %u\n", errcode));

	/*
	 * Fill in the fields. We assume rpkt.sequence and rpkt.associd
	 * have already been filled in.
	 */
	rpkt.r_m_e_op = (u_char)CTL_RESPONSE | CTL_ERROR |
			(res_opcode & CTL_OP_MASK);
	rpkt.status = htons((u_short)(errcode << 8) & 0xff00);
	rpkt.count = 0;

	/*
	 * send packet and bump counters
	 */
	if (res_authenticate && sys_authenticate) {
		maclen = authencrypt(res_keyid, (u_int32 *)&rpkt,
				     CTL_HEADER_LEN);
		sendpkt(rmt_addr, lcl_inter, -2, (void *)&rpkt,
			CTL_HEADER_LEN + maclen);
	} else
		sendpkt(rmt_addr, lcl_inter, -3, (void *)&rpkt,
			CTL_HEADER_LEN);
}

int/*BOOL*/
is_safe_filename(const char * name)
{
	/* We need a strict validation of filenames we should write: The
	 * daemon might run with special permissions and is remote
	 * controllable, so we better take care what we allow as file
	 * name!
	 *
	 * The first character must be digit or a letter from the ASCII
	 * base plane or a '_' ([_A-Za-z0-9]), the following characters
	 * must be from [-._+A-Za-z0-9].
	 *
	 * We do not trust the character classification much here: Since
	 * the NTP protocol makes no provisions for UTF-8 or local code
	 * pages, we strictly require the 7bit ASCII code page.
	 *
	 * The following table is a packed bit field of 128 two-bit
	 * groups. The LSB in each group tells us if a character is
	 * acceptable at the first position, the MSB if the character is
	 * accepted at any other position.
	 *
	 * This does not ensure that the file name is syntactically
	 * correct (multiple dots will not work with VMS...) but it will
	 * exclude potential globbing bombs and directory traversal. It
	 * also rules out drive selection. (For systems that have this
	 * notion, like Windows or VMS.)
	 */
	static const uint32_t chclass[8] = {
		0x00000000, 0x00000000,
		0x28800000, 0x000FFFFF,
		0xFFFFFFFC, 0xC03FFFFF,
		0xFFFFFFFC, 0x003FFFFF
	};

	u_int widx, bidx, mask;
	if ( ! (name && *name))
		return FALSE;

	mask = 1u;
	while (0 != (widx = (u_char)*name++)) {
		bidx = (widx & 15) << 1;
		widx = widx >> 4;
		if (widx >= sizeof(chclass)/sizeof(chclass[0]))
			return FALSE;
		if (0 == ((chclass[widx] >> bidx) & mask))
			return FALSE;
		mask = 2u;
	}
	return TRUE;
}


/*
 * save_config - Implements ntpq -c "saveconfig <filename>"
 *		 Writes current configuration including any runtime
 *		 changes by ntpq's :config or config-from-file
 *
 * Note: There should be no buffer overflow or truncation in the
 * processing of file names -- both cause security problems. This is bit
 * painful to code but essential here.
 */
void
save_config(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	/* block directory traversal by searching for characters that
	 * indicate directory components in a file path.
	 *
	 * Conceptually we should be searching for DIRSEP in filename,
	 * however Windows actually recognizes both forward and
	 * backslashes as equivalent directory separators at the API
	 * level.  On POSIX systems we could allow '\\' but such
	 * filenames are tricky to manipulate from a shell, so just
	 * reject both types of slashes on all platforms.
	 */
	/* TALOS-CAN-0062: block directory traversal for VMS, too */
	static const char * illegal_in_filename =
#if defined(VMS)
	    ":[]"	/* do not allow drive and path components here */
#elif defined(SYS_WINNT)
	    ":\\/"	/* path and drive separators */
#else
	    "\\/"	/* separator and critical char for POSIX */
#endif
	    ;
	char reply[128];
#ifdef SAVECONFIG
	static const char savedconfig_eq[] = "savedconfig=";

	/* Build a safe open mode from the available mode flags. We want
	 * to create a new file and write it in text mode (when
	 * applicable -- only Windows does this...)
	 */
	static const int openmode = O_CREAT | O_TRUNC | O_WRONLY
#  if defined(O_EXCL)		/* posix, vms */
	    | O_EXCL
#  elif defined(_O_EXCL)	/* windows is alway very special... */
	    | _O_EXCL
#  endif
#  if defined(_O_TEXT)		/* windows, again */
	    | _O_TEXT
#endif
	    ;

	char filespec[128];
	char filename[128];
	char fullpath[512];
	char savedconfig[sizeof(savedconfig_eq) + sizeof(filename)];
	time_t now;
	int fd;
	FILE *fptr;
	int prc;
	size_t reqlen;
#endif

	if (RES_NOMODIFY & restrict_mask) {
		ctl_printf("%s", "saveconfig prohibited by restrict ... nomodify");
		ctl_flushpkt(0);
		NLOG(NLOG_SYSINFO)
			msyslog(LOG_NOTICE,
				"saveconfig from %s rejected due to nomodify restriction",
				stoa(&rbufp->recv_srcadr));
		sys_restricted++;
		return;
	}

#ifdef SAVECONFIG
	if (NULL == saveconfigdir) {
		ctl_printf("%s", "saveconfig prohibited, no saveconfigdir configured");
		ctl_flushpkt(0);
		NLOG(NLOG_SYSINFO)
			msyslog(LOG_NOTICE,
				"saveconfig from %s rejected, no saveconfigdir",
				stoa(&rbufp->recv_srcadr));
		return;
	}

	/* The length checking stuff gets serious. Do not assume a NUL
	 * byte can be found, but if so, use it to calculate the needed
	 * buffer size. If the available buffer is too short, bail out;
	 * likewise if there is no file spec. (The latter will not
	 * happen when using NTPQ, but there are other ways to craft a
	 * network packet!)
	 */
	reqlen = (size_t)(reqend - reqpt);
	if (0 != reqlen) {
		char * nulpos = (char*)memchr(reqpt, 0, reqlen);
		if (NULL != nulpos)
			reqlen = (size_t)(nulpos - reqpt);
	}
	if (0 == reqlen)
		return;
	if (reqlen >= sizeof(filespec)) {
		ctl_printf("saveconfig exceeded maximum raw name length (%u)",
			   (u_int)sizeof(filespec));
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"saveconfig exceeded maximum raw name length from %s",
			stoa(&rbufp->recv_srcadr));
		return;
	}

	/* copy data directly as we exactly know the size */
	memcpy(filespec, reqpt, reqlen);
	filespec[reqlen] = '\0';

	/*
	 * allow timestamping of the saved config filename with
	 * strftime() format such as:
	 *   ntpq -c "saveconfig ntp-%Y%m%d-%H%M%S.conf"
	 * XXX: Nice feature, but not too safe.
	 * YYY: The check for permitted characters in file names should
	 *      weed out the worst. Let's hope 'strftime()' does not
	 *      develop pathological problems.
	 */
	time(&now);
	if (0 == strftime(filename, sizeof(filename), filespec,
			  localtime(&now)))
	{
		/*
		 * If we arrive here, 'strftime()' balked; most likely
		 * the buffer was too short. (Or it encounterd an empty
		 * format, or just a format that expands to an empty
		 * string.) We try to use the original name, though this
		 * is very likely to fail later if there are format
		 * specs in the string. Note that truncation cannot
		 * happen here as long as both buffers have the same
		 * size!
		 */
		strlcpy(filename, filespec, sizeof(filename));
	}

	/*
	 * Check the file name for sanity. This might/will rule out file
	 * names that would be legal but problematic, and it blocks
	 * directory traversal.
	 */
	if (!is_safe_filename(filename)) {
		ctl_printf("saveconfig rejects unsafe file name '%s'",
			   filename);
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"saveconfig rejects unsafe file name from %s",
			stoa(&rbufp->recv_srcadr));
		return;
	}

	/*
	 * XXX: This next test may not be needed with is_safe_filename()
	 */

	/* block directory/drive traversal */
	/* TALOS-CAN-0062: block directory traversal for VMS, too */
	if (NULL != strpbrk(filename, illegal_in_filename)) {
		snprintf(reply, sizeof(reply),
			 "saveconfig does not allow directory in filename");
		ctl_putdata(reply, strlen(reply), 0);
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"saveconfig rejects unsafe file name from %s",
			stoa(&rbufp->recv_srcadr));
		return;
	}

	/* concatenation of directory and path can cause another
	 * truncation...
	 */
	prc = snprintf(fullpath, sizeof(fullpath), "%s%s",
		       saveconfigdir, filename);
	if (prc < 0 || (size_t)prc >= sizeof(fullpath)) {
		ctl_printf("saveconfig exceeded maximum path length (%u)",
			   (u_int)sizeof(fullpath));
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"saveconfig exceeded maximum path length from %s",
			stoa(&rbufp->recv_srcadr));
		return;
	}

	fd = open(fullpath, openmode, S_IRUSR | S_IWUSR);
	if (-1 == fd)
		fptr = NULL;
	else
		fptr = fdopen(fd, "w");

	if (NULL == fptr || -1 == dump_all_config_trees(fptr, 1)) {
		ctl_printf("Unable to save configuration to file '%s': %s",
			   filename, strerror(errno));
		msyslog(LOG_ERR,
			"saveconfig %s from %s failed", filename,
			stoa(&rbufp->recv_srcadr));
	} else {
		ctl_printf("Configuration saved to '%s'", filename);
		msyslog(LOG_NOTICE,
			"Configuration saved to '%s' (requested by %s)",
			fullpath, stoa(&rbufp->recv_srcadr));
		/*
		 * save the output filename in system variable
		 * savedconfig, retrieved with:
		 *   ntpq -c "rv 0 savedconfig"
		 * Note: the way 'savedconfig' is defined makes overflow
		 * checks unnecessary here.
		 */
		snprintf(savedconfig, sizeof(savedconfig), "%s%s",
			 savedconfig_eq, filename);
		set_sys_var(savedconfig, strlen(savedconfig) + 1, RO);
	}

	if (NULL != fptr)
		fclose(fptr);
#else	/* !SAVECONFIG follows */
	ctl_printf("%s",
		   "saveconfig unavailable, configured with --disable-saveconfig");
#endif
	ctl_flushpkt(0);
}


/*
 * process_control - process an incoming control message
 */
void
process_control(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	struct ntp_control *pkt;
	int req_count;
	int req_data;
	const struct ctl_proc *cc;
	keyid_t *pkid;
	int properlen;
	size_t maclen;

	DPRINTF(3, ("in process_control()\n"));

	/*
	 * Save the addresses for error responses
	 */
	numctlreq++;
	rmt_addr = &rbufp->recv_srcadr;
	lcl_inter = rbufp->dstadr;
	pkt = (struct ntp_control *)&rbufp->recv_pkt;

	/*
	 * If the length is less than required for the header, or
	 * it is a response or a fragment, ignore this.
	 */
	if (rbufp->recv_length < (int)CTL_HEADER_LEN
	    || (CTL_RESPONSE | CTL_MORE | CTL_ERROR) & pkt->r_m_e_op
	    || pkt->offset != 0) {
		DPRINTF(1, ("invalid format in control packet\n"));
		if (rbufp->recv_length < (int)CTL_HEADER_LEN)
			numctltooshort++;
		if (CTL_RESPONSE & pkt->r_m_e_op)
			numctlinputresp++;
		if (CTL_MORE & pkt->r_m_e_op)
			numctlinputfrag++;
		if (CTL_ERROR & pkt->r_m_e_op)
			numctlinputerr++;
		if (pkt->offset != 0)
			numctlbadoffset++;
		return;
	}
	res_version = PKT_VERSION(pkt->li_vn_mode);
	if (res_version > NTP_VERSION || res_version < NTP_OLDVERSION) {
		DPRINTF(1, ("unknown version %d in control packet\n",
			    res_version));
		numctlbadversion++;
		return;
	}

	/*
	 * Pull enough data from the packet to make intelligent
	 * responses
	 */
	rpkt.li_vn_mode = PKT_LI_VN_MODE(sys_leap, res_version,
					 MODE_CONTROL);
	res_opcode = pkt->r_m_e_op;
	rpkt.sequence = pkt->sequence;
	rpkt.associd = pkt->associd;
	rpkt.status = 0;
	res_frags = 1;
	res_offset = 0;
	res_associd = htons(pkt->associd);
	res_async = FALSE;
	res_authenticate = FALSE;
	res_keyid = 0;
	res_authokay = FALSE;
	req_count = (int)ntohs(pkt->count);
	datanotbinflag = FALSE;
	datalinelen = 0;
	datasent = 0;
	datapt = rpkt.u.data;
	dataend = &rpkt.u.data[CTL_MAX_DATA_LEN];

	if ((rbufp->recv_length & 0x3) != 0)
		DPRINTF(3, ("Control packet length %d unrounded\n",
			    rbufp->recv_length));

	/*
	 * We're set up now. Make sure we've got at least enough
	 * incoming data space to match the count.
	 */
	req_data = rbufp->recv_length - CTL_HEADER_LEN;
	if (req_data < req_count || rbufp->recv_length & 0x3) {
		ctl_error(CERR_BADFMT);
		numctldatatooshort++;
		return;
	}

	properlen = req_count + CTL_HEADER_LEN;
	/* round up proper len to a 8 octet boundary */

	properlen = (properlen + 7) & ~7;
	maclen = rbufp->recv_length - properlen;
	if ((rbufp->recv_length & 3) == 0 &&
	    maclen >= MIN_MAC_LEN && maclen <= MAX_MAC_LEN &&
	    sys_authenticate) {
		res_authenticate = TRUE;
		pkid = (void *)((char *)pkt + properlen);
		res_keyid = ntohl(*pkid);
		DPRINTF(3, ("recv_len %d, properlen %d, wants auth with keyid %08x, MAC length=%zu\n",
			    rbufp->recv_length, properlen, res_keyid,
			    maclen));

		if (!authistrustedip(res_keyid, &rbufp->recv_srcadr))
			DPRINTF(3, ("invalid keyid %08x\n", res_keyid));
		else if (authdecrypt(res_keyid, (u_int32 *)pkt,
				     rbufp->recv_length - maclen,
				     maclen)) {
			res_authokay = TRUE;
			DPRINTF(3, ("authenticated okay\n"));
		} else {
			res_keyid = 0;
			DPRINTF(3, ("authentication failed\n"));
		}
	}

	/*
	 * Set up translate pointers
	 */
	reqpt = (char *)pkt->u.data;
	reqend = reqpt + req_count;

	/*
	 * Look for the opcode processor
	 */
	for (cc = control_codes; cc->control_code != NO_REQUEST; cc++) {
		if (cc->control_code == res_opcode) {
			DPRINTF(3, ("opcode %d, found command handler\n",
				    res_opcode));
			if (cc->flags == AUTH
			    && (!res_authokay
				|| res_keyid != ctl_auth_keyid)) {
				ctl_error(CERR_PERMISSION);
				return;
			}
			(cc->handler)(rbufp, restrict_mask);
			return;
		}
	}

	/*
	 * Can't find this one, return an error.
	 */
	numctlbadop++;
	ctl_error(CERR_BADOP);
	return;
}


/*
 * ctlpeerstatus - return a status word for this peer
 */
u_short
ctlpeerstatus(
	register struct peer *p
	)
{
	u_short status;

	status = p->status;
	if (FLAG_CONFIG & p->flags)
		status |= CTL_PST_CONFIG;
	if (p->keyid)
		status |= CTL_PST_AUTHENABLE;
	if (FLAG_AUTHENTIC & p->flags)
		status |= CTL_PST_AUTHENTIC;
	if (p->reach)
		status |= CTL_PST_REACH;
	if (MDF_TXONLY_MASK & p->cast_flags)
		status |= CTL_PST_BCAST;

	return CTL_PEER_STATUS(status, p->num_events, p->last_event);
}


/*
 * ctlclkstatus - return a status word for this clock
 */
#ifdef REFCLOCK
static u_short
ctlclkstatus(
	struct refclockstat *pcs
	)
{
	return CTL_PEER_STATUS(0, pcs->lastevent, pcs->currentstatus);
}
#endif


/*
 * ctlsysstatus - return the system status word
 */
u_short
ctlsysstatus(void)
{
	register u_char this_clock;

	this_clock = CTL_SST_TS_UNSPEC;
#ifdef REFCLOCK
	if (sys_peer != NULL) {
		if (CTL_SST_TS_UNSPEC != sys_peer->sstclktype)
			this_clock = sys_peer->sstclktype;
		else if (sys_peer->refclktype < COUNTOF(clocktypes))
			this_clock = clocktypes[sys_peer->refclktype];
	}
#else /* REFCLOCK */
	if (sys_peer != 0)
		this_clock = CTL_SST_TS_NTP;
#endif /* REFCLOCK */
	return CTL_SYS_STATUS(sys_leap, this_clock, ctl_sys_num_events,
			      ctl_sys_last_event);
}


/*
 * ctl_flushpkt - write out the current packet and prepare
 *		  another if necessary.
 */
static void
ctl_flushpkt(
	u_char more
	)
{
	size_t i;
	size_t dlen;
	size_t sendlen;
	size_t maclen;
	size_t totlen;
	keyid_t keyid;

	dlen = datapt - rpkt.u.data;
	if (!more && datanotbinflag && dlen + 2 < CTL_MAX_DATA_LEN) {
		/*
		 * Big hack, output a trailing \r\n
		 */
		*datapt++ = '\r';
		*datapt++ = '\n';
		dlen += 2;
	}
	sendlen = dlen + CTL_HEADER_LEN;

	/*
	 * Pad to a multiple of 32 bits
	 */
	while (sendlen & 0x3) {
		*datapt++ = '\0';
		sendlen++;
	}

	/*
	 * Fill in the packet with the current info
	 */
	rpkt.r_m_e_op = CTL_RESPONSE | more |
			(res_opcode & CTL_OP_MASK);
	rpkt.count = htons((u_short)dlen);
	rpkt.offset = htons((u_short)res_offset);
	if (res_async) {
		for (i = 0; i < COUNTOF(ctl_traps); i++) {
			if (TRAP_INUSE & ctl_traps[i].tr_flags) {
				rpkt.li_vn_mode =
				    PKT_LI_VN_MODE(
					sys_leap,
					ctl_traps[i].tr_version,
					MODE_CONTROL);
				rpkt.sequence =
				    htons(ctl_traps[i].tr_sequence);
				sendpkt(&ctl_traps[i].tr_addr,
					ctl_traps[i].tr_localaddr, -4,
					(struct pkt *)&rpkt, sendlen);
				if (!more)
					ctl_traps[i].tr_sequence++;
				numasyncmsgs++;
			}
		}
	} else {
		if (res_authenticate && sys_authenticate) {
			totlen = sendlen;
			/*
			 * If we are going to authenticate, then there
			 * is an additional requirement that the MAC
			 * begin on a 64 bit boundary.
			 */
			while (totlen & 7) {
				*datapt++ = '\0';
				totlen++;
			}
			keyid = htonl(res_keyid);
			memcpy(datapt, &keyid, sizeof(keyid));
			maclen = authencrypt(res_keyid,
					     (u_int32 *)&rpkt, totlen);
			sendpkt(rmt_addr, lcl_inter, -5,
				(struct pkt *)&rpkt, totlen + maclen);
		} else {
			sendpkt(rmt_addr, lcl_inter, -6,
				(struct pkt *)&rpkt, sendlen);
		}
		if (more)
			numctlfrags++;
		else
			numctlresponses++;
	}

	/*
	 * Set us up for another go around.
	 */
	res_frags++;
	res_offset += dlen;
	datapt = rpkt.u.data;
}


/* --------------------------------------------------------------------
 * block transfer API -- stream string/data fragments into xmit buffer
 * without additional copying
 */

/* buffer descriptor: address & size of fragment
 * 'buf' may only be NULL when 'len' is zero!
 */
typedef struct {
	const void  *buf;
	size_t       len;
} CtlMemBufT;

/* put ctl data in a gather-style operation */
static void
ctl_putdata_ex(
	const CtlMemBufT * argv,
	size_t             argc,
	int/*BOOL*/        bin		/* set to 1 when data is binary */
	)
{
	const char * src_ptr;
	size_t       src_len, cur_len, add_len, argi;

	/* text / binary preprocessing, possibly create new linefeed */
	if (bin) {
		add_len = 0;
	} else {
		datanotbinflag = TRUE;
		add_len = 3;

		if (datasent) {
			*datapt++ = ',';
			datalinelen++;

			/* sum up total length */
			for (argi = 0, src_len = 0; argi < argc; ++argi)
				src_len += argv[argi].len;
			/* possibly start a new line, assume no size_t overflow */
			if ((src_len + datalinelen + 1) >= MAXDATALINELEN) {
				*datapt++ = '\r';
				*datapt++ = '\n';
				datalinelen = 0;
			} else {
				*datapt++ = ' ';
				datalinelen++;
			}
		}
	}

	/* now stream out all buffers */
	for (argi = 0; argi < argc; ++argi) {
		src_ptr = argv[argi].buf;
		src_len = argv[argi].len;

		if ( ! (src_ptr && src_len))
			continue;

		cur_len = (size_t)(dataend - datapt);
		while ((src_len + add_len) > cur_len) {
			/* Not enough room in this one, flush it out. */
			if (src_len < cur_len)
				cur_len = src_len;

			memcpy(datapt, src_ptr, cur_len);
			datapt      += cur_len;
			datalinelen += cur_len;

			src_ptr     += cur_len;
			src_len     -= cur_len;

			ctl_flushpkt(CTL_MORE);
			cur_len = (size_t)(dataend - datapt);
		}

		memcpy(datapt, src_ptr, src_len);
		datapt      += src_len;
		datalinelen += src_len;

		datasent = TRUE;
	}
}

/*
 * ctl_putdata - write data into the packet, fragmenting and starting
 * another if this one is full.
 */
static void
ctl_putdata(
	const char *dp,
	unsigned int dlen,
	int bin			/* set to 1 when data is binary */
	)
{
	CtlMemBufT args[1];

	args[0].buf = dp;
	args[0].len = dlen;
	ctl_putdata_ex(args, 1, bin);
}

/*
 * ctl_putstr - write a tagged string into the response packet
 *		in the form:
 *
 *		tag="data"
 *
 *		len is the data length excluding the NUL terminator,
 *		as in ctl_putstr("var", "value", strlen("value"));
 */
static void
ctl_putstr(
	const char *	tag,
	const char *	data,
	size_t		len
	)
{
	CtlMemBufT args[4];

	args[0].buf = tag;
	args[0].len = strlen(tag);
	if (data && len) {
	    args[1].buf = "=\"";
	    args[1].len = 2;
	    args[2].buf = data;
	    args[2].len = len;
	    args[3].buf = "\"";
	    args[3].len = 1;
	    ctl_putdata_ex(args, 4, FALSE);
	} else {
	    args[1].buf = "=\"\"";
	    args[1].len = 3;
	    ctl_putdata_ex(args, 2, FALSE);
	}
}


/*
 * ctl_putunqstr - write a tagged string into the response packet
 *		   in the form:
 *
 *		   tag=data
 *
 *	len is the data length excluding the NUL terminator.
 *	data must not contain a comma or whitespace.
 */
static void
ctl_putunqstr(
	const char *	tag,
	const char *	data,
	size_t		len
	)
{
	CtlMemBufT args[3];

	args[0].buf = tag;
	args[0].len = strlen(tag);
	args[1].buf = "=";
	args[1].len = 1;
	if (data && len) {
		args[2].buf = data;
		args[2].len = len;
		ctl_putdata_ex(args, 3, FALSE);
	} else {
		ctl_putdata_ex(args, 2, FALSE);
	}
}


/*
 * ctl_putdblf - write a tagged, signed double into the response packet
 */
static void
ctl_putdblf(
	const char *	tag,
	int		use_f,
	int		precision,
	double		d
	)
{
	char buffer[40];
	int  rc;

	rc = snprintf(buffer, sizeof(buffer),
		      (use_f ? "%.*f" : "%.*g"),
		      precision, d);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}

/*
 * ctl_putuint - write a tagged unsigned integer into the response
 */
static void
ctl_putuint(
	const char *tag,
	u_long uval
	)
{
	char buffer[24]; /* needs to fit for 64 bits! */
	int  rc;

	rc = snprintf(buffer, sizeof(buffer), "%lu", uval);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}

/*
 * ctl_putcal - write a decoded calendar data into the response.
 * only used with AUTOKEY currently, so compiled conditional
 */
#ifdef AUTOKEY
static void
ctl_putcal(
	const char *tag,
	const struct calendar *pcal
	)
{
	char buffer[16];
	int  rc;

	rc = snprintf(buffer, sizeof(buffer),
		      "%04d%02d%02d%02d%02d",
		      pcal->year, pcal->month, pcal->monthday,
		      pcal->hour, pcal->minute
		);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}
#endif

/*
 * ctl_putfs - write a decoded filestamp into the response
 */
static void
ctl_putfs(
	const char *tag,
	tstamp_t uval
	)
{
	char buffer[16];
	int  rc;

	time_t fstamp = (time_t)uval - JAN_1970;
	struct tm *tm = gmtime(&fstamp);

	if (NULL == tm)
		return;

	rc = snprintf(buffer, sizeof(buffer),
		      "%04d%02d%02d%02d%02d",
		      tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		      tm->tm_hour, tm->tm_min);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}


/*
 * ctl_puthex - write a tagged unsigned integer, in hex, into the
 * response
 */
static void
ctl_puthex(
	const char *tag,
	u_long uval
	)
{
	char buffer[24];	/* must fit 64bit int! */
	int  rc;

	rc = snprintf(buffer, sizeof(buffer), "0x%lx", uval);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}


/*
 * ctl_putint - write a tagged signed integer into the response
 */
static void
ctl_putint(
	const char *tag,
	long ival
	)
{
	char buffer[24];	/*must fit 64bit int */
	int  rc;

	rc = snprintf(buffer, sizeof(buffer), "%ld", ival);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}


/*
 * ctl_putts - write a tagged timestamp, in hex, into the response
 */
static void
ctl_putts(
	const char *tag,
	l_fp *ts
	)
{
	char buffer[24];
	int  rc;

	rc = snprintf(buffer, sizeof(buffer),
		      "0x%08lx.%08lx",
		      (u_long)ts->l_ui, (u_long)ts->l_uf);
	INSIST(rc >= 0 && (size_t)rc < sizeof(buffer));
	ctl_putunqstr(tag, buffer, rc);
}


/*
 * ctl_putadr - write an IP address into the response
 */
static void
ctl_putadr(
	const char *tag,
	u_int32 addr32,
	sockaddr_u *addr
	)
{
	const char *cq;

	if (NULL == addr)
		cq = numtoa(addr32);
	else
		cq = stoa(addr);
	ctl_putunqstr(tag, cq, strlen(cq));
}


/*
 * ctl_putrefid - send a u_int32 refid as printable text
 */
static void
ctl_putrefid(
	const char *	tag,
	u_int32		refid
	)
{
	size_t nc;

	union {
		uint32_t w;
		uint8_t  b[sizeof(uint32_t)];
	} bytes;

	bytes.w = refid;
	for (nc = 0; nc < sizeof(bytes.b) && bytes.b[nc]; ++nc)
		if (  !isprint(bytes.b[nc])
		    || isspace(bytes.b[nc])
		    || bytes.b[nc] == ','  )
			bytes.b[nc] = '.';
	ctl_putunqstr(tag, (const char*)bytes.b, nc);
}


/*
 * ctl_putarray - write a tagged eight element double array into the response
 */
static void
ctl_putarray(
	const char *tag,
	double *arr,
	int start
	)
{
	char *cp, *ep;
	char buffer[200];
	int  i, rc;

	cp = buffer;
	ep = buffer + sizeof(buffer);
	i  = start;
	do {
		if (i == 0)
			i = NTP_SHIFT;
		i--;
		rc = snprintf(cp, (size_t)(ep - cp), " %.2f", arr[i] * 1e3);
		INSIST(rc >= 0 && (size_t)rc < (size_t)(ep - cp));
		cp += rc;
	} while (i != start);
	ctl_putunqstr(tag, buffer, (size_t)(cp - buffer));
}

/*
 * ctl_printf - put a formatted string into the data buffer
 */
static void
ctl_printf(
	const char * fmt,
	...
	)
{
	static const char * ellipsis = "[...]";
	va_list va;
	char    fmtbuf[128];
	int     rc;

	va_start(va, fmt);
	rc = vsnprintf(fmtbuf, sizeof(fmtbuf), fmt, va);
	va_end(va);
	if (rc < 0 || (size_t)rc >= sizeof(fmtbuf))
		strcpy(fmtbuf + sizeof(fmtbuf) - strlen(ellipsis) - 1,
		       ellipsis);
	ctl_putdata(fmtbuf, strlen(fmtbuf), 0);
}


/*
 * ctl_putsys - output a system variable
 */
static void
ctl_putsys(
	int varid
	)
{
	l_fp tmp;
	char str[256];
	u_int u;
	double kb;
	double dtemp;
	const char *ss;
#ifdef AUTOKEY
	struct cert_info *cp;
#endif	/* AUTOKEY */
#ifdef KERNEL_PLL
	static struct timex ntx;
	static u_long ntp_adjtime_time;

	static const double to_ms_usec =
		1.0e-3; /* usec to msec */
	static const double to_ms_nusec =
# ifdef STA_NANO
		1.0e-6; /* nsec to msec */
# else
		to_ms_usec;
# endif

	/*
	 * CS_K_* variables depend on up-to-date output of ntp_adjtime()
	 */
	if (CS_KERN_FIRST <= varid && varid <= CS_KERN_LAST &&
	    current_time != ntp_adjtime_time) {
		ZERO(ntx);
		if (ntp_adjtime(&ntx) < 0)
			msyslog(LOG_ERR, "ntp_adjtime() for mode 6 query failed: %m");
		else
			ntp_adjtime_time = current_time;
	}
#endif	/* KERNEL_PLL */

	switch (varid) {

	case CS_LEAP:
		ctl_putuint(sys_var[CS_LEAP].text, sys_leap);
		break;

	case CS_STRATUM:
		ctl_putuint(sys_var[CS_STRATUM].text, sys_stratum);
		break;

	case CS_PRECISION:
		ctl_putint(sys_var[CS_PRECISION].text, sys_precision);
		break;

	case CS_ROOTDELAY:
		ctl_putdbl(sys_var[CS_ROOTDELAY].text, sys_rootdelay *
			   1e3);
		break;

	case CS_ROOTDISPERSION:
		ctl_putdbl(sys_var[CS_ROOTDISPERSION].text,
			   sys_rootdisp * 1e3);
		break;

	case CS_REFID:
		if (REFID_ISTEXT(sys_stratum))
			ctl_putrefid(sys_var[varid].text, sys_refid);
		else
			ctl_putadr(sys_var[varid].text, sys_refid, NULL);
		break;

	case CS_REFTIME:
		ctl_putts(sys_var[CS_REFTIME].text, &sys_reftime);
		break;

	case CS_POLL:
		ctl_putuint(sys_var[CS_POLL].text, sys_poll);
		break;

	case CS_PEERID:
		if (sys_peer == NULL)
			ctl_putuint(sys_var[CS_PEERID].text, 0);
		else
			ctl_putuint(sys_var[CS_PEERID].text,
				    sys_peer->associd);
		break;

	case CS_PEERADR:
		if (sys_peer != NULL && sys_peer->dstadr != NULL)
			ss = sptoa(&sys_peer->srcadr);
		else
			ss = "0.0.0.0:0";
		ctl_putunqstr(sys_var[CS_PEERADR].text, ss, strlen(ss));
		break;

	case CS_PEERMODE:
		u = (sys_peer != NULL)
			? sys_peer->hmode
			: MODE_UNSPEC;
		ctl_putuint(sys_var[CS_PEERMODE].text, u);
		break;

	case CS_OFFSET:
		ctl_putdbl6(sys_var[CS_OFFSET].text, last_offset * 1e3);
		break;

	case CS_DRIFT:
		ctl_putdbl(sys_var[CS_DRIFT].text, drift_comp * 1e6);
		break;

	case CS_JITTER:
		ctl_putdbl6(sys_var[CS_JITTER].text, sys_jitter * 1e3);
		break;

	case CS_ERROR:
		ctl_putdbl(sys_var[CS_ERROR].text, clock_jitter * 1e3);
		break;

	case CS_CLOCK:
		get_systime(&tmp);
		ctl_putts(sys_var[CS_CLOCK].text, &tmp);
		break;

	case CS_PROCESSOR:
#ifndef HAVE_UNAME
		ctl_putstr(sys_var[CS_PROCESSOR].text, str_processor,
			   sizeof(str_processor) - 1);
#else
		ctl_putstr(sys_var[CS_PROCESSOR].text,
			   utsnamebuf.machine, strlen(utsnamebuf.machine));
#endif /* HAVE_UNAME */
		break;

	case CS_SYSTEM:
#ifndef HAVE_UNAME
		ctl_putstr(sys_var[CS_SYSTEM].text, str_system,
			   sizeof(str_system) - 1);
#else
		snprintf(str, sizeof(str), "%s/%s", utsnamebuf.sysname,
			 utsnamebuf.release);
		ctl_putstr(sys_var[CS_SYSTEM].text, str, strlen(str));
#endif /* HAVE_UNAME */
		break;

	case CS_VERSION:
		ctl_putstr(sys_var[CS_VERSION].text, Version,
			   strlen(Version));
		break;

	case CS_STABIL:
		ctl_putdbl(sys_var[CS_STABIL].text, clock_stability *
			   1e6);
		break;

	case CS_VARLIST:
	{
		char buf[CTL_MAX_DATA_LEN];
		//buffPointer, firstElementPointer, buffEndPointer
		char *buffp, *buffend;
		int firstVarName;
		const char *ss1;
		int len;
		const struct ctl_var *k;

		buffp = buf;
		buffend = buf + sizeof(buf);
		if (strlen(sys_var[CS_VARLIST].text) > (sizeof(buf) - 4))
			break;	/* really long var name */

		snprintf(buffp, sizeof(buf), "%s=\"",sys_var[CS_VARLIST].text);
		buffp += strlen(buffp);
		firstVarName = TRUE;
		for (k = sys_var; !(k->flags & EOV); k++) {
			if (k->flags & PADDING)
				continue;
			len = strlen(k->text);
			if (len + 1 >= buffend - buffp)
				break;
			if (!firstVarName)
				*buffp++ = ',';
			else
				firstVarName = FALSE;
			memcpy(buffp, k->text, len);
			buffp += len;
		}

		for (k = ext_sys_var; k && !(k->flags & EOV); k++) {
			if (k->flags & PADDING)
				continue;
			if (NULL == k->text)
				continue;
			ss1 = strchr(k->text, '=');
			if (NULL == ss1)
				len = strlen(k->text);
			else
				len = ss1 - k->text;
			if (len + 1 >= buffend - buffp)
				break;
			if (firstVarName) {
				*buffp++ = ',';
				firstVarName = FALSE;
			}
			memcpy(buffp, k->text,(unsigned)len);
			buffp += len;
		}
		if (2 >= buffend - buffp)
			break;

		*buffp++ = '"';
		*buffp = '\0';

		ctl_putdata(buf, (unsigned)( buffp - buf ), 0);
		break;
	}

	case CS_TAI:
		if (sys_tai > 0)
			ctl_putuint(sys_var[CS_TAI].text, sys_tai);
		break;

	case CS_LEAPTAB:
	{
		leap_signature_t lsig;
		leapsec_getsig(&lsig);
		if (lsig.ttime > 0)
			ctl_putfs(sys_var[CS_LEAPTAB].text, lsig.ttime);
		break;
	}

	case CS_LEAPEND:
	{
		leap_signature_t lsig;
		leapsec_getsig(&lsig);
		if (lsig.etime > 0)
			ctl_putfs(sys_var[CS_LEAPEND].text, lsig.etime);
		break;
	}

#ifdef LEAP_SMEAR
	case CS_LEAPSMEARINTV:
		if (leap_smear_intv > 0)
			ctl_putuint(sys_var[CS_LEAPSMEARINTV].text, leap_smear_intv);
		break;

	case CS_LEAPSMEAROFFS:
		if (leap_smear_intv > 0)
			ctl_putdbl(sys_var[CS_LEAPSMEAROFFS].text,
				   leap_smear.doffset * 1e3);
		break;
#endif	/* LEAP_SMEAR */

	case CS_RATE:
		ctl_putuint(sys_var[CS_RATE].text, ntp_minpoll);
		break;

	case CS_MRU_ENABLED:
		ctl_puthex(sys_var[varid].text, mon_enabled);
		break;

	case CS_MRU_DEPTH:
		ctl_putuint(sys_var[varid].text, mru_entries);
		break;

	case CS_MRU_MEM:
		kb = mru_entries * (sizeof(mon_entry) / 1024.);
		u = (u_int)kb;
		if (kb - u >= 0.5)
			u++;
		ctl_putuint(sys_var[varid].text, u);
		break;

	case CS_MRU_DEEPEST:
		ctl_putuint(sys_var[varid].text, mru_peakentries);
		break;

	case CS_MRU_MINDEPTH:
		ctl_putuint(sys_var[varid].text, mru_mindepth);
		break;

	case CS_MRU_MAXAGE:
		ctl_putint(sys_var[varid].text, mru_maxage);
		break;

	case CS_MRU_MAXDEPTH:
		ctl_putuint(sys_var[varid].text, mru_maxdepth);
		break;

	case CS_MRU_MAXMEM:
		kb = mru_maxdepth * (sizeof(mon_entry) / 1024.);
		u = (u_int)kb;
		if (kb - u >= 0.5)
			u++;
		ctl_putuint(sys_var[varid].text, u);
		break;

	case CS_SS_UPTIME:
		ctl_putuint(sys_var[varid].text, current_time);
		break;

	case CS_SS_RESET:
		ctl_putuint(sys_var[varid].text,
			    current_time - sys_stattime);
		break;

	case CS_SS_RECEIVED:
		ctl_putuint(sys_var[varid].text, sys_received);
		break;

	case CS_SS_THISVER:
		ctl_putuint(sys_var[varid].text, sys_newversion);
		break;

	case CS_SS_OLDVER:
		ctl_putuint(sys_var[varid].text, sys_oldversion);
		break;

	case CS_SS_BADFORMAT:
		ctl_putuint(sys_var[varid].text, sys_badlength);
		break;

	case CS_SS_BADAUTH:
		ctl_putuint(sys_var[varid].text, sys_badauth);
		break;

	case CS_SS_DECLINED:
		ctl_putuint(sys_var[varid].text, sys_declined);
		break;

	case CS_SS_RESTRICTED:
		ctl_putuint(sys_var[varid].text, sys_restricted);
		break;

	case CS_SS_LIMITED:
		ctl_putuint(sys_var[varid].text, sys_limitrejected);
		break;

	case CS_SS_LAMPORT:
		ctl_putuint(sys_var[varid].text, sys_lamport);
		break;

	case CS_SS_TSROUNDING:
		ctl_putuint(sys_var[varid].text, sys_tsrounding);
		break;

	case CS_SS_KODSENT:
		ctl_putuint(sys_var[varid].text, sys_kodsent);
		break;

	case CS_SS_PROCESSED:
		ctl_putuint(sys_var[varid].text, sys_processed);
		break;

	case CS_BCASTDELAY:
		ctl_putdbl(sys_var[varid].text, sys_bdelay * 1e3);
		break;

	case CS_AUTHDELAY:
		LFPTOD(&sys_authdelay, dtemp);
		ctl_putdbl(sys_var[varid].text, dtemp * 1e3);
		break;

	case CS_AUTHKEYS:
		ctl_putuint(sys_var[varid].text, authnumkeys);
		break;

	case CS_AUTHFREEK:
		ctl_putuint(sys_var[varid].text, authnumfreekeys);
		break;

	case CS_AUTHKLOOKUPS:
		ctl_putuint(sys_var[varid].text, authkeylookups);
		break;

	case CS_AUTHKNOTFOUND:
		ctl_putuint(sys_var[varid].text, authkeynotfound);
		break;

	case CS_AUTHKUNCACHED:
		ctl_putuint(sys_var[varid].text, authkeyuncached);
		break;

	case CS_AUTHKEXPIRED:
		ctl_putuint(sys_var[varid].text, authkeyexpired);
		break;

	case CS_AUTHENCRYPTS:
		ctl_putuint(sys_var[varid].text, authencryptions);
		break;

	case CS_AUTHDECRYPTS:
		ctl_putuint(sys_var[varid].text, authdecryptions);
		break;

	case CS_AUTHRESET:
		ctl_putuint(sys_var[varid].text,
			    current_time - auth_timereset);
		break;

		/*
		 * CTL_IF_KERNLOOP() puts a zero if the kernel loop is
		 * unavailable, otherwise calls putfunc with args.
		 */
#ifndef KERNEL_PLL
# define	CTL_IF_KERNLOOP(putfunc, args)	\
		ctl_putint(sys_var[varid].text, 0)
#else
# define	CTL_IF_KERNLOOP(putfunc, args)	\
		putfunc args
#endif

		/*
		 * CTL_IF_KERNPPS() puts a zero if either the kernel
		 * loop is unavailable, or kernel hard PPS is not
		 * active, otherwise calls putfunc with args.
		 */
#ifndef KERNEL_PLL
# define	CTL_IF_KERNPPS(putfunc, args)	\
		ctl_putint(sys_var[varid].text, 0)
#else
# define	CTL_IF_KERNPPS(putfunc, args)			\
		if (0 == ntx.shift)				\
			ctl_putint(sys_var[varid].text, 0);	\
		else						\
			putfunc args	/* no trailing ; */
#endif

	case CS_K_OFFSET:
		CTL_IF_KERNLOOP(
			ctl_putdblf,
			(sys_var[varid].text, 0, -1, to_ms_nusec * ntx.offset)
		);
		break;

	case CS_K_FREQ:
		CTL_IF_KERNLOOP(
			ctl_putsfp,
			(sys_var[varid].text, ntx.freq)
		);
		break;

	case CS_K_MAXERR:
		CTL_IF_KERNLOOP(
			ctl_putdblf,
			(sys_var[varid].text, 0, 6,
			 to_ms_usec * ntx.maxerror)
		);
		break;

	case CS_K_ESTERR:
		CTL_IF_KERNLOOP(
			ctl_putdblf,
			(sys_var[varid].text, 0, 6,
			 to_ms_usec * ntx.esterror)
		);
		break;

	case CS_K_STFLAGS:
#ifndef KERNEL_PLL
		ss = "";
#else
		ss = k_st_flags(ntx.status);
#endif
		ctl_putstr(sys_var[varid].text, ss, strlen(ss));
		break;

	case CS_K_TIMECONST:
		CTL_IF_KERNLOOP(
			ctl_putint,
			(sys_var[varid].text, ntx.constant)
		);
		break;

	case CS_K_PRECISION:
		CTL_IF_KERNLOOP(
			ctl_putdblf,
			(sys_var[varid].text, 0, 6,
			    to_ms_usec * ntx.precision)
		);
		break;

	case CS_K_FREQTOL:
		CTL_IF_KERNLOOP(
			ctl_putsfp,
			(sys_var[varid].text, ntx.tolerance)
		);
		break;

	case CS_K_PPS_FREQ:
		CTL_IF_KERNPPS(
			ctl_putsfp,
			(sys_var[varid].text, ntx.ppsfreq)
		);
		break;

	case CS_K_PPS_STABIL:
		CTL_IF_KERNPPS(
			ctl_putsfp,
			(sys_var[varid].text, ntx.stabil)
		);
		break;

	case CS_K_PPS_JITTER:
		CTL_IF_KERNPPS(
			ctl_putdbl,
			(sys_var[varid].text, to_ms_nusec * ntx.jitter)
		);
		break;

	case CS_K_PPS_CALIBDUR:
		CTL_IF_KERNPPS(
			ctl_putint,
			(sys_var[varid].text, 1 << ntx.shift)
		);
		break;

	case CS_K_PPS_CALIBS:
		CTL_IF_KERNPPS(
			ctl_putint,
			(sys_var[varid].text, ntx.calcnt)
		);
		break;

	case CS_K_PPS_CALIBERRS:
		CTL_IF_KERNPPS(
			ctl_putint,
			(sys_var[varid].text, ntx.errcnt)
		);
		break;

	case CS_K_PPS_JITEXC:
		CTL_IF_KERNPPS(
			ctl_putint,
			(sys_var[varid].text, ntx.jitcnt)
		);
		break;

	case CS_K_PPS_STBEXC:
		CTL_IF_KERNPPS(
			ctl_putint,
			(sys_var[varid].text, ntx.stbcnt)
		);
		break;

	case CS_IOSTATS_RESET:
		ctl_putuint(sys_var[varid].text,
			    current_time - io_timereset);
		break;

	case CS_TOTAL_RBUF:
		ctl_putuint(sys_var[varid].text, total_recvbuffs());
		break;

	case CS_FREE_RBUF:
		ctl_putuint(sys_var[varid].text, free_recvbuffs());
		break;

	case CS_USED_RBUF:
		ctl_putuint(sys_var[varid].text, full_recvbuffs());
		break;

	case CS_RBUF_LOWATER:
		ctl_putuint(sys_var[varid].text, lowater_additions());
		break;

	case CS_IO_DROPPED:
		ctl_putuint(sys_var[varid].text, packets_dropped);
		break;

	case CS_IO_IGNORED:
		ctl_putuint(sys_var[varid].text, packets_ignored);
		break;

	case CS_IO_RECEIVED:
		ctl_putuint(sys_var[varid].text, packets_received);
		break;

	case CS_IO_SENT:
		ctl_putuint(sys_var[varid].text, packets_sent);
		break;

	case CS_IO_SENDFAILED:
		ctl_putuint(sys_var[varid].text, packets_notsent);
		break;

	case CS_IO_WAKEUPS:
		ctl_putuint(sys_var[varid].text, handler_calls);
		break;

	case CS_IO_GOODWAKEUPS:
		ctl_putuint(sys_var[varid].text, handler_pkts);
		break;

	case CS_TIMERSTATS_RESET:
		ctl_putuint(sys_var[varid].text,
			    current_time - timer_timereset);
		break;

	case CS_TIMER_OVERRUNS:
		ctl_putuint(sys_var[varid].text, alarm_overflow);
		break;

	case CS_TIMER_XMTS:
		ctl_putuint(sys_var[varid].text, timer_xmtcalls);
		break;

	case CS_FUZZ:
		ctl_putdbl(sys_var[varid].text, sys_fuzz * 1e3);
		break;
	case CS_WANDER_THRESH:
		ctl_putdbl(sys_var[varid].text, wander_threshold * 1e6);
		break;
#ifdef AUTOKEY
	case CS_FLAGS:
		if (crypto_flags)
			ctl_puthex(sys_var[CS_FLAGS].text,
			    crypto_flags);
		break;

	case CS_DIGEST:
		if (crypto_flags) {
			strlcpy(str, OBJ_nid2ln(crypto_nid),
			    COUNTOF(str));
			ctl_putstr(sys_var[CS_DIGEST].text, str,
			    strlen(str));
		}
		break;

	case CS_SIGNATURE:
		if (crypto_flags) {
			const EVP_MD *dp;

			dp = EVP_get_digestbynid(crypto_flags >> 16);
			strlcpy(str, OBJ_nid2ln(EVP_MD_pkey_type(dp)),
			    COUNTOF(str));
			ctl_putstr(sys_var[CS_SIGNATURE].text, str,
			    strlen(str));
		}
		break;

	case CS_HOST:
		if (hostval.ptr != NULL)
			ctl_putstr(sys_var[CS_HOST].text, hostval.ptr,
			    strlen(hostval.ptr));
		break;

	case CS_IDENT:
		if (sys_ident != NULL)
			ctl_putstr(sys_var[CS_IDENT].text, sys_ident,
			    strlen(sys_ident));
		break;

	case CS_CERTIF:
		for (cp = cinfo; cp != NULL; cp = cp->link) {
			snprintf(str, sizeof(str), "%s %s 0x%x",
			    cp->subject, cp->issuer, cp->flags);
			ctl_putstr(sys_var[CS_CERTIF].text, str,
			    strlen(str));
			ctl_putcal(sys_var[CS_REVTIME].text, &(cp->last));
		}
		break;

	case CS_PUBLIC:
		if (hostval.tstamp != 0)
			ctl_putfs(sys_var[CS_PUBLIC].text,
			    ntohl(hostval.tstamp));
		break;
#endif	/* AUTOKEY */

	default:
		break;
	}
}


/*
 * ctl_putpeer - output a peer variable
 */
static void
ctl_putpeer(
	int id,
	struct peer *p
	)
{
	char buf[CTL_MAX_DATA_LEN];
	char *s;
	char *t;
	char *be;
	int i;
	const struct ctl_var *k;
#ifdef AUTOKEY
	struct autokey *ap;
	const EVP_MD *dp;
	const char *str;
#endif	/* AUTOKEY */

	switch (id) {

	case CP_CONFIG:
		ctl_putuint(peer_var[id].text,
			    !(FLAG_PREEMPT & p->flags));
		break;

	case CP_AUTHENABLE:
		ctl_putuint(peer_var[id].text, !(p->keyid));
		break;

	case CP_AUTHENTIC:
		ctl_putuint(peer_var[id].text,
			    !!(FLAG_AUTHENTIC & p->flags));
		break;

	case CP_SRCADR:
		ctl_putadr(peer_var[id].text, 0, &p->srcadr);
		break;

	case CP_SRCPORT:
		ctl_putuint(peer_var[id].text, SRCPORT(&p->srcadr));
		break;

	case CP_SRCHOST:
		if (p->hostname != NULL)
			ctl_putstr(peer_var[id].text, p->hostname,
				   strlen(p->hostname));
		break;

	case CP_DSTADR:
		ctl_putadr(peer_var[id].text, 0,
			   (p->dstadr != NULL)
				? &p->dstadr->sin
				: NULL);
		break;

	case CP_DSTPORT:
		ctl_putuint(peer_var[id].text,
			    (p->dstadr != NULL)
				? SRCPORT(&p->dstadr->sin)
				: 0);
		break;

	case CP_IN:
		if (p->r21 > 0.)
			ctl_putdbl(peer_var[id].text, p->r21 / 1e3);
		break;

	case CP_OUT:
		if (p->r34 > 0.)
			ctl_putdbl(peer_var[id].text, p->r34 / 1e3);
		break;

	case CP_RATE:
		ctl_putuint(peer_var[id].text, p->throttle);
		break;

	case CP_LEAP:
		ctl_putuint(peer_var[id].text, p->leap);
		break;

	case CP_HMODE:
		ctl_putuint(peer_var[id].text, p->hmode);
		break;

	case CP_STRATUM:
		ctl_putuint(peer_var[id].text, p->stratum);
		break;

	case CP_PPOLL:
		ctl_putuint(peer_var[id].text, p->ppoll);
		break;

	case CP_HPOLL:
		ctl_putuint(peer_var[id].text, p->hpoll);
		break;

	case CP_PRECISION:
		ctl_putint(peer_var[id].text, p->precision);
		break;

	case CP_ROOTDELAY:
		ctl_putdbl(peer_var[id].text, p->rootdelay * 1e3);
		break;

	case CP_ROOTDISPERSION:
		ctl_putdbl(peer_var[id].text, p->rootdisp * 1e3);
		break;

	case CP_REFID:
#ifdef REFCLOCK
		if (p->flags & FLAG_REFCLOCK) {
			ctl_putrefid(peer_var[id].text, p->refid);
			break;
		}
#endif
		if (REFID_ISTEXT(p->stratum))
			ctl_putrefid(peer_var[id].text, p->refid);
		else
			ctl_putadr(peer_var[id].text, p->refid, NULL);
		break;

	case CP_REFTIME:
		ctl_putts(peer_var[id].text, &p->reftime);
		break;

	case CP_ORG:
		ctl_putts(peer_var[id].text, &p->aorg);
		break;

	case CP_REC:
		ctl_putts(peer_var[id].text, &p->dst);
		break;

	case CP_XMT:
		if (p->xleave)
			ctl_putdbl(peer_var[id].text, p->xleave * 1e3);
		break;

	case CP_BIAS:
		if (p->bias != 0.)
			ctl_putdbl(peer_var[id].text, p->bias * 1e3);
		break;

	case CP_REACH:
		ctl_puthex(peer_var[id].text, p->reach);
		break;

	case CP_FLASH:
		ctl_puthex(peer_var[id].text, p->flash);
		break;

	case CP_TTL:
#ifdef REFCLOCK
		if (p->flags & FLAG_REFCLOCK) {
			ctl_putuint(peer_var[id].text, p->ttl);
			break;
		}
#endif
		if (p->ttl > 0 && p->ttl < COUNTOF(sys_ttl))
			ctl_putint(peer_var[id].text,
				   sys_ttl[p->ttl]);
		break;

	case CP_UNREACH:
		ctl_putuint(peer_var[id].text, p->unreach);
		break;

	case CP_TIMER:
		ctl_putuint(peer_var[id].text,
			    p->nextdate - current_time);
		break;

	case CP_DELAY:
		ctl_putdbl(peer_var[id].text, p->delay * 1e3);
		break;

	case CP_OFFSET:
		ctl_putdbl(peer_var[id].text, p->offset * 1e3);
		break;

	case CP_JITTER:
		ctl_putdbl(peer_var[id].text, p->jitter * 1e3);
		break;

	case CP_DISPERSION:
		ctl_putdbl(peer_var[id].text, p->disp * 1e3);
		break;

	case CP_KEYID:
		if (p->keyid > NTP_MAXKEY)
			ctl_puthex(peer_var[id].text, p->keyid);
		else
			ctl_putuint(peer_var[id].text, p->keyid);
		break;

	case CP_FILTDELAY:
		ctl_putarray(peer_var[id].text, p->filter_delay,
			     p->filter_nextpt);
		break;

	case CP_FILTOFFSET:
		ctl_putarray(peer_var[id].text, p->filter_offset,
			     p->filter_nextpt);
		break;

	case CP_FILTERROR:
		ctl_putarray(peer_var[id].text, p->filter_disp,
			     p->filter_nextpt);
		break;

	case CP_PMODE:
		ctl_putuint(peer_var[id].text, p->pmode);
		break;

	case CP_RECEIVED:
		ctl_putuint(peer_var[id].text, p->received);
		break;

	case CP_SENT:
		ctl_putuint(peer_var[id].text, p->sent);
		break;

	case CP_VARLIST:
		s = buf;
		be = buf + sizeof(buf);
		if (strlen(peer_var[id].text) + 4 > sizeof(buf))
			break;	/* really long var name */

		snprintf(s, sizeof(buf), "%s=\"", peer_var[id].text);
		s += strlen(s);
		t = s;
		for (k = peer_var; !(EOV & k->flags); k++) {
			if (PADDING & k->flags)
				continue;
			i = strlen(k->text);
			if (s + i + 1 >= be)
				break;
			if (s != t)
				*s++ = ',';
			memcpy(s, k->text, i);
			s += i;
		}
		if (s + 2 < be) {
			*s++ = '"';
			*s = '\0';
			ctl_putdata(buf, (u_int)(s - buf), 0);
		}
		break;

	case CP_TIMEREC:
		ctl_putuint(peer_var[id].text,
			    current_time - p->timereceived);
		break;

	case CP_TIMEREACH:
		ctl_putuint(peer_var[id].text,
			    current_time - p->timereachable);
		break;

	case CP_BADAUTH:
		ctl_putuint(peer_var[id].text, p->badauth);
		break;

	case CP_BOGUSORG:
		ctl_putuint(peer_var[id].text, p->bogusorg);
		break;

	case CP_OLDPKT:
		ctl_putuint(peer_var[id].text, p->oldpkt);
		break;

	case CP_SELDISP:
		ctl_putuint(peer_var[id].text, p->seldisptoolarge);
		break;

	case CP_SELBROKEN:
		ctl_putuint(peer_var[id].text, p->selbroken);
		break;

	case CP_CANDIDATE:
		ctl_putuint(peer_var[id].text, p->status);
		break;
#ifdef AUTOKEY
	case CP_FLAGS:
		if (p->crypto)
			ctl_puthex(peer_var[id].text, p->crypto);
		break;

	case CP_SIGNATURE:
		if (p->crypto) {
			dp = EVP_get_digestbynid(p->crypto >> 16);
			str = OBJ_nid2ln(EVP_MD_pkey_type(dp));
			ctl_putstr(peer_var[id].text, str, strlen(str));
		}
		break;

	case CP_HOST:
		if (p->subject != NULL)
			ctl_putstr(peer_var[id].text, p->subject,
			    strlen(p->subject));
		break;

	case CP_VALID:		/* not used */
		break;

	case CP_INITSEQ:
		if (NULL == (ap = p->recval.ptr))
			break;

		ctl_putint(peer_var[CP_INITSEQ].text, ap->seq);
		ctl_puthex(peer_var[CP_INITKEY].text, ap->key);
		ctl_putfs(peer_var[CP_INITTSP].text,
			  ntohl(p->recval.tstamp));
		break;

	case CP_IDENT:
		if (p->ident != NULL)
			ctl_putstr(peer_var[id].text, p->ident,
			    strlen(p->ident));
		break;


#endif	/* AUTOKEY */
	}
}


#ifdef REFCLOCK
/*
 * ctl_putclock - output clock variables
 */
static void
ctl_putclock(
	int id,
	struct refclockstat *pcs,
	int mustput
	)
{
	char buf[CTL_MAX_DATA_LEN];
	char *s, *t, *be;
	const char *ss;
	int i;
	const struct ctl_var *k;

	switch (id) {

	case CC_TYPE:
		if (mustput || pcs->clockdesc == NULL
		    || *(pcs->clockdesc) == '\0') {
			ctl_putuint(clock_var[id].text, pcs->type);
		}
		break;
	case CC_TIMECODE:
		ctl_putstr(clock_var[id].text,
			   pcs->p_lastcode,
			   (unsigned)pcs->lencode);
		break;

	case CC_POLL:
		ctl_putuint(clock_var[id].text, pcs->polls);
		break;

	case CC_NOREPLY:
		ctl_putuint(clock_var[id].text,
			    pcs->noresponse);
		break;

	case CC_BADFORMAT:
		ctl_putuint(clock_var[id].text,
			    pcs->badformat);
		break;

	case CC_BADDATA:
		ctl_putuint(clock_var[id].text,
			    pcs->baddata);
		break;

	case CC_FUDGETIME1:
		if (mustput || (pcs->haveflags & CLK_HAVETIME1))
			ctl_putdbl(clock_var[id].text,
				   pcs->fudgetime1 * 1e3);
		break;

	case CC_FUDGETIME2:
		if (mustput || (pcs->haveflags & CLK_HAVETIME2))
			ctl_putdbl(clock_var[id].text,
				   pcs->fudgetime2 * 1e3);
		break;

	case CC_FUDGEVAL1:
		if (mustput || (pcs->haveflags & CLK_HAVEVAL1))
			ctl_putint(clock_var[id].text,
				   pcs->fudgeval1);
		break;

	case CC_FUDGEVAL2:
		if (mustput || (pcs->haveflags & CLK_HAVEVAL2)) {
			if (pcs->fudgeval1 > 1)
				ctl_putadr(clock_var[id].text,
					   pcs->fudgeval2, NULL);
			else
				ctl_putrefid(clock_var[id].text,
					     pcs->fudgeval2);
		}
		break;

	case CC_FLAGS:
		ctl_putuint(clock_var[id].text, pcs->flags);
		break;

	case CC_DEVICE:
		if (pcs->clockdesc == NULL ||
		    *(pcs->clockdesc) == '\0') {
			if (mustput)
				ctl_putstr(clock_var[id].text,
					   "", 0);
		} else {
			ctl_putstr(clock_var[id].text,
				   pcs->clockdesc,
				   strlen(pcs->clockdesc));
		}
		break;

	case CC_VARLIST:
		s = buf;
		be = buf + sizeof(buf);
		if (strlen(clock_var[CC_VARLIST].text) + 4 >
		    sizeof(buf))
			break;	/* really long var name */

		snprintf(s, sizeof(buf), "%s=\"",
			 clock_var[CC_VARLIST].text);
		s += strlen(s);
		t = s;

		for (k = clock_var; !(EOV & k->flags); k++) {
			if (PADDING & k->flags)
				continue;

			i = strlen(k->text);
			if (s + i + 1 >= be)
				break;

			if (s != t)
				*s++ = ',';
			memcpy(s, k->text, i);
			s += i;
		}

		for (k = pcs->kv_list; k && !(EOV & k->flags); k++) {
			if (PADDING & k->flags)
				continue;

			ss = k->text;
			if (NULL == ss)
				continue;

			while (*ss && *ss != '=')
				ss++;
			i = ss - k->text;
			if (s + i + 1 >= be)
				break;

			if (s != t)
				*s++ = ',';
			memcpy(s, k->text, (unsigned)i);
			s += i;
			*s = '\0';
		}
		if (s + 2 >= be)
			break;

		*s++ = '"';
		*s = '\0';
		ctl_putdata(buf, (unsigned)(s - buf), 0);
		break;
	}
}
#endif



/*
 * ctl_getitem - get the next data item from the incoming packet
 */
static const struct ctl_var *
ctl_getitem(
	const struct ctl_var *var_list,
	char **data
	)
{
	/* [Bug 3008] First check the packet data sanity, then search
	 * the key. This improves the consistency of result values: If
	 * the result is NULL once, it will never be EOV again for this
	 * packet; If it's EOV, it will never be NULL again until the
	 * variable is found and processed in a given 'var_list'. (That
	 * is, a result is returned that is neither NULL nor EOV).
	 */
	static const struct ctl_var eol = { 0, EOV, NULL };
	static char buf[128];
	static u_long quiet_until;
	const struct ctl_var *v;
	char *cp;
	char *tp;

	/*
	 * Part One: Validate the packet state
	 */

	/* Delete leading commas and white space */
	while (reqpt < reqend && (*reqpt == ',' ||
				  isspace((unsigned char)*reqpt)))
		reqpt++;
	if (reqpt >= reqend)
		return NULL;

	/* Scan the string in the packet until we hit comma or
	 * EoB. Register position of first '=' on the fly. */
	for (tp = NULL, cp = reqpt; cp != reqend; ++cp) {
		if (*cp == '=' && tp == NULL)
			tp = cp;
		if (*cp == ',')
			break;
	}

	/* Process payload, if any. */
	*data = NULL;
	if (NULL != tp) {
		/* eventually strip white space from argument. */
		const char *plhead = tp + 1; /* skip the '=' */
		const char *pltail = cp;
		size_t      plsize;

		while (plhead != pltail && isspace((u_char)plhead[0]))
			++plhead;
		while (plhead != pltail && isspace((u_char)pltail[-1]))
			--pltail;

		/* check payload size, terminate packet on overflow */
		plsize = (size_t)(pltail - plhead);
		if (plsize >= sizeof(buf))
			goto badpacket;

		/* copy data, NUL terminate, and set result data ptr */
		memcpy(buf, plhead, plsize);
		buf[plsize] = '\0';
		*data = buf;
	} else {
		/* no payload, current end --> current name termination */
		tp = cp;
	}

	/* Part Two
	 *
	 * Now we're sure that the packet data itself is sane. Scan the
	 * list now. Make sure a NULL list is properly treated by
	 * returning a synthetic End-Of-Values record. We must not
	 * return NULL pointers after this point, or the behaviour would
	 * become inconsistent if called several times with different
	 * variable lists after an EoV was returned.  (Such a behavior
	 * actually caused Bug 3008.)
	 */

	if (NULL == var_list)
		return &eol;

	for (v = var_list; !(EOV & v->flags); ++v)
		if (!(PADDING & v->flags)) {
			/* Check if the var name matches the buffer. The
			 * name is bracketed by [reqpt..tp] and not NUL
			 * terminated, and it contains no '=' char. The
			 * lookup value IS NUL-terminated but might
			 * include a '='... We have to look out for
			 * that!
			 */
			const char *sp1 = reqpt;
			const char *sp2 = v->text;

			/* [Bug 3412] do not compare past NUL byte in name */
			while (   (sp1 != tp)
			       && ('\0' != *sp2) && (*sp1 == *sp2)) {
				++sp1;
				++sp2;
			}
			if (sp1 == tp && (*sp2 == '\0' || *sp2 == '='))
				break;
		}

	/* See if we have found a valid entry or not. If found, advance
	 * the request pointer for the next round; if not, clear the
	 * data pointer so we have no dangling garbage here.
	 */
	if (EOV & v->flags)
		*data = NULL;
	else
		reqpt = cp + (cp != reqend);
	return v;

  badpacket:
	/*TODO? somehow indicate this packet was bad, apart from syslog? */
	numctlbadpkts++;
	NLOG(NLOG_SYSEVENT)
	    if (quiet_until <= current_time) {
		    quiet_until = current_time + 300;
		    msyslog(LOG_WARNING,
			    "Possible 'ntpdx' exploit from %s#%u (possibly spoofed)",
			    stoa(rmt_addr), SRCPORT(rmt_addr));
	    }
	reqpt = reqend; /* never again for this packet! */
	return NULL;
}


/*
 * control_unspec - response to an unspecified op-code
 */
/*ARGSUSED*/
static void
control_unspec(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	struct peer *peer;

	/*
	 * What is an appropriate response to an unspecified op-code?
	 * I return no errors and no data, unless a specified assocation
	 * doesn't exist.
	 */
	if (res_associd) {
		peer = findpeerbyassoc(res_associd);
		if (NULL == peer) {
			ctl_error(CERR_BADASSOC);
			return;
		}
		rpkt.status = htons(ctlpeerstatus(peer));
	} else
		rpkt.status = htons(ctlsysstatus());
	ctl_flushpkt(0);
}


/*
 * read_status - return either a list of associd's, or a particular
 * peer's status.
 */
/*ARGSUSED*/
static void
read_status(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	struct peer *peer;
	const u_char *cp;
	size_t n;
	/* a_st holds association ID, status pairs alternating */
	u_short a_st[CTL_MAX_DATA_LEN / sizeof(u_short)];

#ifdef DEBUG
	if (debug > 2)
		printf("read_status: ID %d\n", res_associd);
#endif
	/*
	 * Two choices here. If the specified association ID is
	 * zero we return all known assocation ID's.  Otherwise
	 * we return a bunch of stuff about the particular peer.
	 */
	if (res_associd) {
		peer = findpeerbyassoc(res_associd);
		if (NULL == peer) {
			ctl_error(CERR_BADASSOC);
			return;
		}
		rpkt.status = htons(ctlpeerstatus(peer));
		if (res_authokay)
			peer->num_events = 0;
		/*
		 * For now, output everything we know about the
		 * peer. May be more selective later.
		 */
		for (cp = def_peer_var; *cp != 0; cp++)
			ctl_putpeer((int)*cp, peer);
		ctl_flushpkt(0);
		return;
	}
	n = 0;
	rpkt.status = htons(ctlsysstatus());
	for (peer = peer_list; peer != NULL; peer = peer->p_link) {
		a_st[n++] = htons(peer->associd);
		a_st[n++] = htons(ctlpeerstatus(peer));
		/* two entries each loop iteration, so n + 1 */
		if (n + 1 >= COUNTOF(a_st)) {
			ctl_putdata((void *)a_st, n * sizeof(a_st[0]),
				    1);
			n = 0;
		}
	}
	if (n)
		ctl_putdata((void *)a_st, n * sizeof(a_st[0]), 1);
	ctl_flushpkt(0);
}


/*
 * read_peervars - half of read_variables() implementation
 */
static void
read_peervars(void)
{
	const struct ctl_var *v;
	struct peer *peer;
	const u_char *cp;
	size_t i;
	char *	valuep;
	u_char	wants[CP_MAXCODE + 1];
	u_int	gotvar;

	/*
	 * Wants info for a particular peer. See if we know
	 * the guy.
	 */
	peer = findpeerbyassoc(res_associd);
	if (NULL == peer) {
		ctl_error(CERR_BADASSOC);
		return;
	}
	rpkt.status = htons(ctlpeerstatus(peer));
	if (res_authokay)
		peer->num_events = 0;
	ZERO(wants);
	gotvar = 0;
	while (NULL != (v = ctl_getitem(peer_var, &valuep))) {
		if (v->flags & EOV) {
			ctl_error(CERR_UNKNOWNVAR);
			return;
		}
		INSIST(v->code < COUNTOF(wants));
		wants[v->code] = 1;
		gotvar = 1;
	}
	if (gotvar) {
		for (i = 1; i < COUNTOF(wants); i++)
			if (wants[i])
				ctl_putpeer(i, peer);
	} else
		for (cp = def_peer_var; *cp != 0; cp++)
			ctl_putpeer((int)*cp, peer);
	ctl_flushpkt(0);
}


/*
 * read_sysvars - half of read_variables() implementation
 */
static void
read_sysvars(void)
{
	const struct ctl_var *v;
	struct ctl_var *kv;
	u_int	n;
	u_int	gotvar;
	const u_char *cs;
	char *	valuep;
	const char * pch;
	u_char *wants;
	size_t	wants_count;

	/*
	 * Wants system variables. Figure out which he wants
	 * and give them to him.
	 */
	rpkt.status = htons(ctlsysstatus());
	if (res_authokay)
		ctl_sys_num_events = 0;
	wants_count = CS_MAXCODE + 1 + count_var(ext_sys_var);
	wants = emalloc_zero(wants_count);
	gotvar = 0;
	while (NULL != (v = ctl_getitem(sys_var, &valuep))) {
		if (!(EOV & v->flags)) {
			INSIST(v->code < wants_count);
			wants[v->code] = 1;
			gotvar = 1;
		} else {
			v = ctl_getitem(ext_sys_var, &valuep);
			if (NULL == v) {
				ctl_error(CERR_BADVALUE);
				free(wants);
				return;
			}
			if (EOV & v->flags) {
				ctl_error(CERR_UNKNOWNVAR);
				free(wants);
				return;
			}
			n = v->code + CS_MAXCODE + 1;
			INSIST(n < wants_count);
			wants[n] = 1;
			gotvar = 1;
		}
	}
	if (gotvar) {
		for (n = 1; n <= CS_MAXCODE; n++)
			if (wants[n])
				ctl_putsys(n);
		for (n = 0; n + CS_MAXCODE + 1 < wants_count; n++)
			if (wants[n + CS_MAXCODE + 1]) {
				pch = ext_sys_var[n].text;
				ctl_putdata(pch, strlen(pch), 0);
			}
	} else {
		for (cs = def_sys_var; *cs != 0; cs++)
			ctl_putsys((int)*cs);
		for (kv = ext_sys_var; kv && !(EOV & kv->flags); kv++)
			if (DEF & kv->flags)
				ctl_putdata(kv->text, strlen(kv->text),
					    0);
	}
	free(wants);
	ctl_flushpkt(0);
}


/*
 * read_variables - return the variables the caller asks for
 */
/*ARGSUSED*/
static void
read_variables(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	if (res_associd)
		read_peervars();
	else
		read_sysvars();
}


/*
 * write_variables - write into variables. We only allow leap bit
 * writing this way.
 */
/*ARGSUSED*/
static void
write_variables(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	const struct ctl_var *v;
	int ext_var;
	char *valuep;
	long val;
	size_t octets;
	char *vareqv;
	const char *t;
	char *tt;

	val = 0;
	/*
	 * If he's trying to write into a peer tell him no way
	 */
	if (res_associd != 0) {
		ctl_error(CERR_PERMISSION);
		return;
	}

	/*
	 * Set status
	 */
	rpkt.status = htons(ctlsysstatus());

	/*
	 * Look through the variables. Dump out at the first sign of
	 * trouble.
	 */
	while ((v = ctl_getitem(sys_var, &valuep)) != NULL) {
		ext_var = 0;
		if (v->flags & EOV) {
			v = ctl_getitem(ext_sys_var, &valuep);
			if (v != NULL) {
				if (v->flags & EOV) {
					ctl_error(CERR_UNKNOWNVAR);
					return;
				}
				ext_var = 1;
			} else {
				break;
			}
		}
		if (!(v->flags & CAN_WRITE)) {
			ctl_error(CERR_PERMISSION);
			return;
		}
		/* [bug 3565] writing makes sense only if we *have* a
		 * value in the packet!
		 */
		if (valuep == NULL) {
			ctl_error(CERR_BADFMT);
			return;
		}
		if (!ext_var) {
			if ( !(*valuep && atoint(valuep, &val))) {
				ctl_error(CERR_BADFMT);
				return;
			}
			if ((val & ~LEAP_NOTINSYNC) != 0) {
				ctl_error(CERR_BADVALUE);
				return;
			}
		}
		
		if (ext_var) {
			octets = strlen(v->text) + strlen(valuep) + 2;
			vareqv = emalloc(octets);
			tt = vareqv;
			t = v->text;
			while (*t && *t != '=')
				*tt++ = *t++;
			*tt++ = '=';
			memcpy(tt, valuep, 1 + strlen(valuep));
			set_sys_var(vareqv, 1 + strlen(vareqv), v->flags);
			free(vareqv);
		} else {
			ctl_error(CERR_UNSPEC); /* really */
			return;
		}
	}

	/*
	 * If we got anything, do it. xxx nothing to do ***
	 */
	/*
	  if (leapind != ~0 || leapwarn != ~0) {
	  if (!leap_setleap((int)leapind, (int)leapwarn)) {
	  ctl_error(CERR_PERMISSION);
	  return;
	  }
	  }
	*/
	ctl_flushpkt(0);
}


/*
 * configure() processes ntpq :config/config-from-file, allowing
 *		generic runtime reconfiguration.
 */
static void configure(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	size_t data_count;
	int retval;

	/* I haven't yet implemented changes to an existing association.
	 * Hence check if the association id is 0
	 */
	if (res_associd != 0) {
		ctl_error(CERR_BADVALUE);
		return;
	}

	if (RES_NOMODIFY & restrict_mask) {
		snprintf(remote_config.err_msg,
			 sizeof(remote_config.err_msg),
			 "runtime configuration prohibited by restrict ... nomodify");
		ctl_putdata(remote_config.err_msg,
			    strlen(remote_config.err_msg), 0);
		ctl_flushpkt(0);
		NLOG(NLOG_SYSINFO)
			msyslog(LOG_NOTICE,
				"runtime config from %s rejected due to nomodify restriction",
				stoa(&rbufp->recv_srcadr));
		sys_restricted++;
		return;
	}

	/* Initialize the remote config buffer */
	data_count = remoteconfig_cmdlength(reqpt, reqend);

	if (data_count > sizeof(remote_config.buffer) - 2) {
		snprintf(remote_config.err_msg,
			 sizeof(remote_config.err_msg),
			 "runtime configuration failed: request too long");
		ctl_putdata(remote_config.err_msg,
			    strlen(remote_config.err_msg), 0);
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"runtime config from %s rejected: request too long",
			stoa(&rbufp->recv_srcadr));
		return;
	}
	/* Bug 2853 -- check if all characters were acceptable */
	if (data_count != (size_t)(reqend - reqpt)) {
		snprintf(remote_config.err_msg,
			 sizeof(remote_config.err_msg),
			 "runtime configuration failed: request contains an unprintable character");
		ctl_putdata(remote_config.err_msg,
			    strlen(remote_config.err_msg), 0);
		ctl_flushpkt(0);
		msyslog(LOG_NOTICE,
			"runtime config from %s rejected: request contains an unprintable character: %0x",
			stoa(&rbufp->recv_srcadr),
			reqpt[data_count]);
		return;
	}

	memcpy(remote_config.buffer, reqpt, data_count);
	/* The buffer has no trailing linefeed or NUL right now. For
	 * logging, we do not want a newline, so we do that first after
	 * adding the necessary NUL byte.
	 */
	remote_config.buffer[data_count] = '\0';
	DPRINTF(1, ("Got Remote Configuration Command: %s\n",
		remote_config.buffer));
	msyslog(LOG_NOTICE, "%s config: %s",
		stoa(&rbufp->recv_srcadr),
		remote_config.buffer);

	/* Now we have to make sure there is a NL/NUL sequence at the
	 * end of the buffer before we parse it.
	 */
	remote_config.buffer[data_count++] = '\n';
	remote_config.buffer[data_count] = '\0';
	remote_config.pos = 0;
	remote_config.err_pos = 0;
	remote_config.no_errors = 0;
	config_remotely(&rbufp->recv_srcadr);

	/*
	 * Check if errors were reported. If not, output 'Config
	 * Succeeded'.  Else output the error count.  It would be nice
	 * to output any parser error messages.
	 */
	if (0 == remote_config.no_errors) {
		retval = snprintf(remote_config.err_msg,
				  sizeof(remote_config.err_msg),
				  "Config Succeeded");
		if (retval > 0)
			remote_config.err_pos += retval;
	}

	ctl_putdata(remote_config.err_msg, remote_config.err_pos, 0);
	ctl_flushpkt(0);

	DPRINTF(1, ("Reply: %s\n", remote_config.err_msg));

	if (remote_config.no_errors > 0)
		msyslog(LOG_NOTICE, "%d error in %s config",
			remote_config.no_errors,
			stoa(&rbufp->recv_srcadr));
}


/*
 * derive_nonce - generate client-address-specific nonce value
 *		  associated with a given timestamp.
 */
static u_int32 derive_nonce(
	sockaddr_u *	addr,
	u_int32		ts_i,
	u_int32		ts_f
	)
{
	static u_int32	salt[4];
	static u_long	last_salt_update;
	union d_tag {
		u_char	digest[EVP_MAX_MD_SIZE];
		u_int32 extract;
	}		d;
	EVP_MD_CTX	*ctx;
	u_int		len;

	while (!salt[0] || current_time - last_salt_update >= 3600) {
		salt[0] = ntp_random();
		salt[1] = ntp_random();
		salt[2] = ntp_random();
		salt[3] = ntp_random();
		last_salt_update = current_time;
	}

	ctx = EVP_MD_CTX_new();
#   if defined(OPENSSL) && defined(EVP_MD_CTX_FLAG_NON_FIPS_ALLOW)
	/* [Bug 3457] set flags and don't kill them again */
	EVP_MD_CTX_set_flags(ctx, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
	EVP_DigestInit_ex(ctx, EVP_get_digestbynid(NID_md5), NULL);
#   else
	EVP_DigestInit(ctx, EVP_get_digestbynid(NID_md5));
#   endif
	EVP_DigestUpdate(ctx, salt, sizeof(salt));
	EVP_DigestUpdate(ctx, &ts_i, sizeof(ts_i));
	EVP_DigestUpdate(ctx, &ts_f, sizeof(ts_f));
	if (IS_IPV4(addr))
		EVP_DigestUpdate(ctx, &SOCK_ADDR4(addr),
			         sizeof(SOCK_ADDR4(addr)));
	else
		EVP_DigestUpdate(ctx, &SOCK_ADDR6(addr),
			         sizeof(SOCK_ADDR6(addr)));
	EVP_DigestUpdate(ctx, &NSRCPORT(addr), sizeof(NSRCPORT(addr)));
	EVP_DigestUpdate(ctx, salt, sizeof(salt));
	EVP_DigestFinal(ctx, d.digest, &len);
	EVP_MD_CTX_free(ctx);

	return d.extract;
}


/*
 * generate_nonce - generate client-address-specific nonce string.
 */
static void generate_nonce(
	struct recvbuf *	rbufp,
	char *			nonce,
	size_t			nonce_octets
	)
{
	u_int32 derived;

	derived = derive_nonce(&rbufp->recv_srcadr,
			       rbufp->recv_time.l_ui,
			       rbufp->recv_time.l_uf);
	snprintf(nonce, nonce_octets, "%08x%08x%08x",
		 rbufp->recv_time.l_ui, rbufp->recv_time.l_uf, derived);
}


/*
 * validate_nonce - validate client-address-specific nonce string.
 *
 * Returns TRUE if the local calculation of the nonce matches the
 * client-provided value and the timestamp is recent enough.
 */
static int validate_nonce(
	const char *		pnonce,
	struct recvbuf *	rbufp
	)
{
	u_int	ts_i;
	u_int	ts_f;
	l_fp	ts;
	l_fp	now_delta;
	u_int	supposed;
	u_int	derived;

	if (3 != sscanf(pnonce, "%08x%08x%08x", &ts_i, &ts_f, &supposed))
		return FALSE;

	ts.l_ui = (u_int32)ts_i;
	ts.l_uf = (u_int32)ts_f;
	derived = derive_nonce(&rbufp->recv_srcadr, ts.l_ui, ts.l_uf);
	get_systime(&now_delta);
	L_SUB(&now_delta, &ts);

	return (supposed == derived && now_delta.l_ui < 16);
}


/*
 * send_random_tag_value - send a randomly-generated three character
 *			   tag prefix, a '.', an index, a '=' and a
 *			   random integer value.
 *
 * To try to force clients to ignore unrecognized tags in mrulist,
 * reslist, and ifstats responses, the first and last rows are spiced
 * with randomly-generated tag names with correct .# index.  Make it
 * three characters knowing that none of the currently-used subscripted
 * tags have that length, avoiding the need to test for
 * tag collision.
 */
static void
send_random_tag_value(
	int	indx
	)
{
	int	noise;
	char	buf[32];

	noise = rand() ^ (rand() << 16);
	buf[0] = 'a' + noise % 26;
	noise >>= 5;
	buf[1] = 'a' + noise % 26;
	noise >>= 5;
	buf[2] = 'a' + noise % 26;
	noise >>= 5;
	buf[3] = '.';
	snprintf(&buf[4], sizeof(buf) - 4, "%d", indx);
	ctl_putuint(buf, noise);
}


/*
 * Send a MRU list entry in response to a "ntpq -c mrulist" operation.
 *
 * To keep clients honest about not depending on the order of values,
 * and thereby avoid being locked into ugly workarounds to maintain
 * backward compatibility later as new fields are added to the response,
 * the order is random.
 */
static void
send_mru_entry(
	mon_entry *	mon,
	int		count
	)
{
	const char first_fmt[] =	"first.%d";
	const char ct_fmt[] =		"ct.%d";
	const char mv_fmt[] =		"mv.%d";
	const char rs_fmt[] =		"rs.%d";
	char	tag[32];
	u_char	sent[6]; /* 6 tag=value pairs */
	u_int32 noise;
	u_int	which;
	u_int	remaining;
	const char * pch;

	remaining = COUNTOF(sent);
	ZERO(sent);
	noise = (u_int32)(rand() ^ (rand() << 16));
	while (remaining > 0) {
		which = (noise & 7) % COUNTOF(sent);
		noise >>= 3;
		while (sent[which])
			which = (which + 1) % COUNTOF(sent);

		switch (which) {

		case 0:
			snprintf(tag, sizeof(tag), addr_fmt, count);
			pch = sptoa(&mon->rmtadr);
			ctl_putunqstr(tag, pch, strlen(pch));
			break;

		case 1:
			snprintf(tag, sizeof(tag), last_fmt, count);
			ctl_putts(tag, &mon->last);
			break;

		case 2:
			snprintf(tag, sizeof(tag), first_fmt, count);
			ctl_putts(tag, &mon->first);
			break;

		case 3:
			snprintf(tag, sizeof(tag), ct_fmt, count);
			ctl_putint(tag, mon->count);
			break;

		case 4:
			snprintf(tag, sizeof(tag), mv_fmt, count);
			ctl_putuint(tag, mon->vn_mode);
			break;

		case 5:
			snprintf(tag, sizeof(tag), rs_fmt, count);
			ctl_puthex(tag, mon->flags);
			break;
		}
		sent[which] = TRUE;
		remaining--;
	}
}


/*
 * read_mru_list - supports ntpq's mrulist command.
 *
 * The challenge here is to match ntpdc's monlist functionality without
 * being limited to hundreds of entries returned total, and without
 * requiring state on the server.  If state were required, ntpq's
 * mrulist command would require authentication.
 *
 * The approach was suggested by Ry Jones.  A finite and variable number
 * of entries are retrieved per request, to avoid having responses with
 * such large numbers of packets that socket buffers are overflowed and
 * packets lost.  The entries are retrieved oldest-first, taking into
 * account that the MRU list will be changing between each request.  We
 * can expect to see duplicate entries for addresses updated in the MRU
 * list during the fetch operation.  In the end, the client can assemble
 * a close approximation of the MRU list at the point in time the last
 * response was sent by ntpd.  The only difference is it may be longer,
 * containing some number of oldest entries which have since been
 * reclaimed.  If necessary, the protocol could be extended to zap those
 * from the client snapshot at the end, but so far that doesn't seem
 * useful.
 *
 * To accomodate the changing MRU list, the starting point for requests
 * after the first request is supplied as a series of last seen
 * timestamps and associated addresses, the newest ones the client has
 * received.  As long as at least one of those entries hasn't been
 * bumped to the head of the MRU list, ntpd can pick up at that point.
 * Otherwise, the request is failed and it is up to ntpq to back up and
 * provide the next newest entry's timestamps and addresses, conceivably
 * backing up all the way to the starting point.
 *
 * input parameters:
 *	nonce=		Regurgitated nonce retrieved by the client
 *			previously using CTL_OP_REQ_NONCE, demonstrating
 *			ability to receive traffic sent to its address.
 *	frags=		Limit on datagrams (fragments) in response.  Used
 *			by newer ntpq versions instead of limit= when
 *			retrieving multiple entries.
 *	limit=		Limit on MRU entries returned.  One of frags= or
 *			limit= must be provided.
 *			limit=1 is a special case:  Instead of fetching
 *			beginning with the supplied starting point's
 *			newer neighbor, fetch the supplied entry, and
 *			in that case the #.last timestamp can be zero.
 *			This enables fetching a single entry by IP
 *			address.  When limit is not one and frags= is
 *			provided, the fragment limit controls.
 *	mincount=	(decimal) Return entries with count >= mincount.
 *	laddr=		Return entries associated with the server's IP
 *			address given.  No port specification is needed,
 *			and any supplied is ignored.
 *	resall=		0x-prefixed hex restrict bits which must all be
 *			lit for an MRU entry to be included.
 *			Has precedence over any resany=.
 *	resany=		0x-prefixed hex restrict bits, at least one of
 *			which must be list for an MRU entry to be
 *			included.
 *	last.0=		0x-prefixed hex l_fp timestamp of newest entry
 *			which client previously received.
 *	addr.0=		text of newest entry's IP address and port,
 *			IPv6 addresses in bracketed form: [::]:123
 *	last.1=		timestamp of 2nd newest entry client has.
 *	addr.1=		address of 2nd newest entry.
 *	[...]
 *
 * ntpq provides as many last/addr pairs as will fit in a single request
 * packet, except for the first request in a MRU fetch operation.
 *
 * The response begins with a new nonce value to be used for any
 * followup request.  Following the nonce is the next newer entry than
 * referred to by last.0 and addr.0, if the "0" entry has not been
 * bumped to the front.  If it has, the first entry returned will be the
 * next entry newer than referred to by last.1 and addr.1, and so on.
 * If none of the referenced entries remain unchanged, the request fails
 * and ntpq backs up to the next earlier set of entries to resync.
 *
 * Except for the first response, the response begins with confirmation
 * of the entry that precedes the first additional entry provided:
 *
 *	last.older=	hex l_fp timestamp matching one of the input
 *			.last timestamps, which entry now precedes the
 *			response 0. entry in the MRU list.
 *	addr.older=	text of address corresponding to older.last.
 *
 * And in any case, a successful response contains sets of values
 * comprising entries, with the oldest numbered 0 and incrementing from
 * there:
 *
 *	addr.#		text of IPv4 or IPv6 address and port
 *	last.#		hex l_fp timestamp of last receipt
 *	first.#		hex l_fp timestamp of first receipt
 *	ct.#		count of packets received
 *	mv.#		mode and version
 *	rs.#		restriction mask (RES_* bits)
 *
 * Note the code currently assumes there are no valid three letter
 * tags sent with each row, and needs to be adjusted if that changes.
 *
 * The client should accept the values in any order, and ignore .#
 * values which it does not understand, to allow a smooth path to
 * future changes without requiring a new opcode.  Clients can rely
 * on all *.0 values preceding any *.1 values, that is all values for
 * a given index number are together in the response.
 *
 * The end of the response list is noted with one or two tag=value
 * pairs.  Unconditionally:
 *
 *	now=		0x-prefixed l_fp timestamp at the server marking
 *			the end of the operation.
 *
 * If any entries were returned, now= is followed by:
 *
 *	last.newest=	hex l_fp identical to last.# of the prior
 *			entry.
 */
static void read_mru_list(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	static const char	nulltxt[1] = 		{ '\0' };
	static const char	nonce_text[] =		"nonce";
	static const char	frags_text[] =		"frags";
	static const char	limit_text[] =		"limit";
	static const char	mincount_text[] =	"mincount";
	static const char	resall_text[] =		"resall";
	static const char	resany_text[] =		"resany";
	static const char	maxlstint_text[] =	"maxlstint";
	static const char	laddr_text[] =		"laddr";
	static const char	resaxx_fmt[] =		"0x%hx";

	u_int			limit;
	u_short			frags;
	u_short			resall;
	u_short			resany;
	int			mincount;
	u_int			maxlstint;
	sockaddr_u		laddr;
	struct interface *	lcladr;
	u_int			count;
	u_int			ui;
	u_int			uf;
	l_fp			last[16];
	sockaddr_u		addr[COUNTOF(last)];
	char			buf[128];
	struct ctl_var *	in_parms;
	const struct ctl_var *	v;
	const char *		val;
	const char *		pch;
	char *			pnonce;
	int			nonce_valid;
	size_t			i;
	int			priors;
	u_short			hash;
	mon_entry *		mon;
	mon_entry *		prior_mon;
	l_fp			now;

	if (RES_NOMRULIST & restrict_mask) {
		ctl_error(CERR_PERMISSION);
		NLOG(NLOG_SYSINFO)
			msyslog(LOG_NOTICE,
				"mrulist from %s rejected due to nomrulist restriction",
				stoa(&rbufp->recv_srcadr));
		sys_restricted++;
		return;
	}
	/*
	 * fill in_parms var list with all possible input parameters.
	 */
	in_parms = NULL;
	set_var(&in_parms, nonce_text, sizeof(nonce_text), 0);
	set_var(&in_parms, frags_text, sizeof(frags_text), 0);
	set_var(&in_parms, limit_text, sizeof(limit_text), 0);
	set_var(&in_parms, mincount_text, sizeof(mincount_text), 0);
	set_var(&in_parms, resall_text, sizeof(resall_text), 0);
	set_var(&in_parms, resany_text, sizeof(resany_text), 0);
	set_var(&in_parms, maxlstint_text, sizeof(maxlstint_text), 0);
	set_var(&in_parms, laddr_text, sizeof(laddr_text), 0);
	for (i = 0; i < COUNTOF(last); i++) {
		snprintf(buf, sizeof(buf), last_fmt, (int)i);
		set_var(&in_parms, buf, strlen(buf) + 1, 0);
		snprintf(buf, sizeof(buf), addr_fmt, (int)i);
		set_var(&in_parms, buf, strlen(buf) + 1, 0);
	}

	/* decode input parms */
	pnonce = NULL;
	frags = 0;
	limit = 0;
	mincount = 0;
	resall = 0;
	resany = 0;
	maxlstint = 0;
	lcladr = NULL;
	priors = 0;
	ZERO(last);
	ZERO(addr);

	/* have to go through '(void*)' to drop 'const' property from pointer.
	 * ctl_getitem()' needs some cleanup, too.... perlinger@ntp.org
	 */
	while (NULL != (v = ctl_getitem(in_parms, (void*)&val)) &&
	       !(EOV & v->flags)) {
		int si;

		if (NULL == val)
			val = nulltxt;

		if (!strcmp(nonce_text, v->text)) {
			free(pnonce);
			pnonce = (*val) ? estrdup(val) : NULL;
		} else if (!strcmp(frags_text, v->text)) {
			if (1 != sscanf(val, "%hu", &frags))
				goto blooper;
		} else if (!strcmp(limit_text, v->text)) {
			if (1 != sscanf(val, "%u", &limit))
				goto blooper;
		} else if (!strcmp(mincount_text, v->text)) {
			if (1 != sscanf(val, "%d", &mincount))
				goto blooper;
			if (mincount < 0)
				mincount = 0;
		} else if (!strcmp(resall_text, v->text)) {
			if (1 != sscanf(val, resaxx_fmt, &resall))
				goto blooper;
		} else if (!strcmp(resany_text, v->text)) {
			if (1 != sscanf(val, resaxx_fmt, &resany))
				goto blooper;
		} else if (!strcmp(maxlstint_text, v->text)) {
			if (1 != sscanf(val, "%u", &maxlstint))
				goto blooper;
		} else if (!strcmp(laddr_text, v->text)) {
			if (!decodenetnum(val, &laddr))
				goto blooper;
			lcladr = getinterface(&laddr, 0);
		} else if (1 == sscanf(v->text, last_fmt, &si) &&
			   (size_t)si < COUNTOF(last)) {
			if (2 != sscanf(val, "0x%08x.%08x", &ui, &uf))
				goto blooper;
			last[si].l_ui = ui;
			last[si].l_uf = uf;
			if (!SOCK_UNSPEC(&addr[si]) && si == priors)
				priors++;
		} else if (1 == sscanf(v->text, addr_fmt, &si) &&
			   (size_t)si < COUNTOF(addr)) {
			if (!decodenetnum(val, &addr[si]))
				goto blooper;
			if (last[si].l_ui && last[si].l_uf && si == priors)
				priors++;
		} else {
			DPRINTF(1, ("read_mru_list: invalid key item: '%s' (ignored)\n",
				    v->text));
			continue;

		blooper:
			DPRINTF(1, ("read_mru_list: invalid param for '%s': '%s' (bailing)\n",
				    v->text, val));
			free(pnonce);
			pnonce = NULL;
			break;
		}
	}
	free_varlist(in_parms);
	in_parms = NULL;

	/* return no responses until the nonce is validated */
	if (NULL == pnonce)
		return;

	nonce_valid = validate_nonce(pnonce, rbufp);
	free(pnonce);
	if (!nonce_valid)
		return;

	if ((0 == frags && !(0 < limit && limit <= MRU_ROW_LIMIT)) ||
	    frags > MRU_FRAGS_LIMIT) {
		ctl_error(CERR_BADVALUE);
		return;
	}

	/*
	 * If either frags or limit is not given, use the max.
	 */
	if (0 != frags && 0 == limit)
		limit = UINT_MAX;
	else if (0 != limit && 0 == frags)
		frags = MRU_FRAGS_LIMIT;

	/*
	 * Find the starting point if one was provided.
	 */
	mon = NULL;
	for (i = 0; i < (size_t)priors; i++) {
		hash = MON_HASH(&addr[i]);
		for (mon = mon_hash[hash];
		     mon != NULL;
		     mon = mon->hash_next)
			if (ADDR_PORT_EQ(&mon->rmtadr, &addr[i]))
				break;
		if (mon != NULL) {
			if (L_ISEQU(&mon->last, &last[i]))
				break;
			mon = NULL;
		}
	}

	/* If a starting point was provided... */
	if (priors) {
		/* and none could be found unmodified... */
		if (NULL == mon) {
			/* tell ntpq to try again with older entries */
			ctl_error(CERR_UNKNOWNVAR);
			return;
		}
		/* confirm the prior entry used as starting point */
		ctl_putts("last.older", &mon->last);
		pch = sptoa(&mon->rmtadr);
		ctl_putunqstr("addr.older", pch, strlen(pch));

		/*
		 * Move on to the first entry the client doesn't have,
		 * except in the special case of a limit of one.  In
		 * that case return the starting point entry.
		 */
		if (limit > 1)
			mon = PREV_DLIST(mon_mru_list, mon, mru);
	} else {	/* start with the oldest */
		mon = TAIL_DLIST(mon_mru_list, mru);
	}

	/*
	 * send up to limit= entries in up to frags= datagrams
	 */
	get_systime(&now);
	generate_nonce(rbufp, buf, sizeof(buf));
	ctl_putunqstr("nonce", buf, strlen(buf));
	prior_mon = NULL;
	for (count = 0;
	     mon != NULL && res_frags < frags && count < limit;
	     mon = PREV_DLIST(mon_mru_list, mon, mru)) {

		if (mon->count < mincount)
			continue;
		if (resall && resall != (resall & mon->flags))
			continue;
		if (resany && !(resany & mon->flags))
			continue;
		if (maxlstint > 0 && now.l_ui - mon->last.l_ui >
		    maxlstint)
			continue;
		if (lcladr != NULL && mon->lcladr != lcladr)
			continue;

		send_mru_entry(mon, count);
		if (!count)
			send_random_tag_value(0);
		count++;
		prior_mon = mon;
	}

	/*
	 * If this batch completes the MRU list, say so explicitly with
	 * a now= l_fp timestamp.
	 */
	if (NULL == mon) {
		if (count > 1)
			send_random_tag_value(count - 1);
		ctl_putts("now", &now);
		/* if any entries were returned confirm the last */
		if (prior_mon != NULL)
			ctl_putts("last.newest", &prior_mon->last);
	}
	ctl_flushpkt(0);
}


/*
 * Send a ifstats entry in response to a "ntpq -c ifstats" request.
 *
 * To keep clients honest about not depending on the order of values,
 * and thereby avoid being locked into ugly workarounds to maintain
 * backward compatibility later as new fields are added to the response,
 * the order is random.
 */
static void
send_ifstats_entry(
	endpt *	la,
	u_int	ifnum
	)
{
	const char addr_fmtu[] =	"addr.%u";
	const char bcast_fmt[] =	"bcast.%u";
	const char en_fmt[] =		"en.%u";	/* enabled */
	const char name_fmt[] =		"name.%u";
	const char flags_fmt[] =	"flags.%u";
	const char tl_fmt[] =		"tl.%u";	/* ttl */
	const char mc_fmt[] =		"mc.%u";	/* mcast count */
	const char rx_fmt[] =		"rx.%u";
	const char tx_fmt[] =		"tx.%u";
	const char txerr_fmt[] =	"txerr.%u";
	const char pc_fmt[] =		"pc.%u";	/* peer count */
	const char up_fmt[] =		"up.%u";	/* uptime */
	char	tag[32];
	u_char	sent[IFSTATS_FIELDS]; /* 12 tag=value pairs */
	int	noisebits;
	u_int32 noise;
	u_int	which;
	u_int	remaining;
	const char *pch;

	remaining = COUNTOF(sent);
	ZERO(sent);
	noise = 0;
	noisebits = 0;
	while (remaining > 0) {
		if (noisebits < 4) {
			noise = rand() ^ (rand() << 16);
			noisebits = 31;
		}
		which = (noise & 0xf) % COUNTOF(sent);
		noise >>= 4;
		noisebits -= 4;

		while (sent[which])
			which = (which + 1) % COUNTOF(sent);

		switch (which) {

		case 0:
			snprintf(tag, sizeof(tag), addr_fmtu, ifnum);
			pch = sptoa(&la->sin);
			ctl_putunqstr(tag, pch, strlen(pch));
			break;

		case 1:
			snprintf(tag, sizeof(tag), bcast_fmt, ifnum);
			if (INT_BCASTOPEN & la->flags)
				pch = sptoa(&la->bcast);
			else
				pch = "";
			ctl_putunqstr(tag, pch, strlen(pch));
			break;

		case 2:
			snprintf(tag, sizeof(tag), en_fmt, ifnum);
			ctl_putint(tag, !la->ignore_packets);
			break;

		case 3:
			snprintf(tag, sizeof(tag), name_fmt, ifnum);
			ctl_putstr(tag, la->name, strlen(la->name));
			break;

		case 4:
			snprintf(tag, sizeof(tag), flags_fmt, ifnum);
			ctl_puthex(tag, (u_int)la->flags);
			break;

		case 5:
			snprintf(tag, sizeof(tag), tl_fmt, ifnum);
			ctl_putint(tag, la->last_ttl);
			break;

		case 6:
			snprintf(tag, sizeof(tag), mc_fmt, ifnum);
			ctl_putint(tag, la->num_mcast);
			break;

		case 7:
			snprintf(tag, sizeof(tag), rx_fmt, ifnum);
			ctl_putint(tag, la->received);
			break;

		case 8:
			snprintf(tag, sizeof(tag), tx_fmt, ifnum);
			ctl_putint(tag, la->sent);
			break;

		case 9:
			snprintf(tag, sizeof(tag), txerr_fmt, ifnum);
			ctl_putint(tag, la->notsent);
			break;

		case 10:
			snprintf(tag, sizeof(tag), pc_fmt, ifnum);
			ctl_putuint(tag, la->peercnt);
			break;

		case 11:
			snprintf(tag, sizeof(tag), up_fmt, ifnum);
			ctl_putuint(tag, current_time - la->starttime);
			break;
		}
		sent[which] = TRUE;
		remaining--;
	}
	send_random_tag_value((int)ifnum);
}


/*
 * read_ifstats - send statistics for each local address, exposed by
 *		  ntpq -c ifstats
 */
static void
read_ifstats(
	struct recvbuf *	rbufp
	)
{
	u_int	ifidx;
	endpt *	la;

	/*
	 * loop over [0..sys_ifnum] searching ep_list for each
	 * ifnum in turn.
	 */
	for (ifidx = 0; ifidx < sys_ifnum; ifidx++) {
		for (la = ep_list; la != NULL; la = la->elink)
			if (ifidx == la->ifnum)
				break;
		if (NULL == la)
			continue;
		/* return stats for one local address */
		send_ifstats_entry(la, ifidx);
	}
	ctl_flushpkt(0);
}

static void
sockaddrs_from_restrict_u(
	sockaddr_u *	psaA,
	sockaddr_u *	psaM,
	restrict_u *	pres,
	int		ipv6
	)
{
	ZERO(*psaA);
	ZERO(*psaM);
	if (!ipv6) {
		psaA->sa.sa_family = AF_INET;
		psaA->sa4.sin_addr.s_addr = htonl(pres->u.v4.addr);
		psaM->sa.sa_family = AF_INET;
		psaM->sa4.sin_addr.s_addr = htonl(pres->u.v4.mask);
	} else {
		psaA->sa.sa_family = AF_INET6;
		memcpy(&psaA->sa6.sin6_addr, &pres->u.v6.addr,
		       sizeof(psaA->sa6.sin6_addr));
		psaM->sa.sa_family = AF_INET6;
		memcpy(&psaM->sa6.sin6_addr, &pres->u.v6.mask,
		       sizeof(psaA->sa6.sin6_addr));
	}
}


/*
 * Send a restrict entry in response to a "ntpq -c reslist" request.
 *
 * To keep clients honest about not depending on the order of values,
 * and thereby avoid being locked into ugly workarounds to maintain
 * backward compatibility later as new fields are added to the response,
 * the order is random.
 */
static void
send_restrict_entry(
	restrict_u *	pres,
	int		ipv6,
	u_int		idx
	)
{
	const char addr_fmtu[] =	"addr.%u";
	const char mask_fmtu[] =	"mask.%u";
	const char hits_fmt[] =		"hits.%u";
	const char flags_fmt[] =	"flags.%u";
	char		tag[32];
	u_char		sent[RESLIST_FIELDS]; /* 4 tag=value pairs */
	int		noisebits;
	u_int32		noise;
	u_int		which;
	u_int		remaining;
	sockaddr_u	addr;
	sockaddr_u	mask;
	const char *	pch;
	char *		buf;
	const char *	match_str;
	const char *	access_str;

	sockaddrs_from_restrict_u(&addr, &mask, pres, ipv6);
	remaining = COUNTOF(sent);
	ZERO(sent);
	noise = 0;
	noisebits = 0;
	while (remaining > 0) {
		if (noisebits < 2) {
			noise = rand() ^ (rand() << 16);
			noisebits = 31;
		}
		which = (noise & 0x3) % COUNTOF(sent);
		noise >>= 2;
		noisebits -= 2;

		while (sent[which])
			which = (which + 1) % COUNTOF(sent);

		/* XXX: Numbers?  Really? */
		switch (which) {

		case 0:
			snprintf(tag, sizeof(tag), addr_fmtu, idx);
			pch = stoa(&addr);
			ctl_putunqstr(tag, pch, strlen(pch));
			break;

		case 1:
			snprintf(tag, sizeof(tag), mask_fmtu, idx);
			pch = stoa(&mask);
			ctl_putunqstr(tag, pch, strlen(pch));
			break;

		case 2:
			snprintf(tag, sizeof(tag), hits_fmt, idx);
			ctl_putuint(tag, pres->count);
			break;

		case 3:
			snprintf(tag, sizeof(tag), flags_fmt, idx);
			match_str = res_match_flags(pres->mflags);
			access_str = res_access_flags(pres->rflags);
			if ('\0' == match_str[0]) {
				pch = access_str;
			} else {
				LIB_GETBUF(buf);
				snprintf(buf, LIB_BUFLENGTH, "%s %s",
					 match_str, access_str);
				pch = buf;
			}
			ctl_putunqstr(tag, pch, strlen(pch));
			break;
		}
		sent[which] = TRUE;
		remaining--;
	}
	send_random_tag_value((int)idx);
}


static void
send_restrict_list(
	restrict_u *	pres,
	int		ipv6,
	u_int *		pidx
	)
{
	for ( ; pres != NULL; pres = pres->link) {
		send_restrict_entry(pres, ipv6, *pidx);
		(*pidx)++;
	}
}


/*
 * read_addr_restrictions - returns IPv4 and IPv6 access control lists
 */
static void
read_addr_restrictions(
	struct recvbuf *	rbufp
)
{
	u_int idx;

	idx = 0;
	send_restrict_list(restrictlist4, FALSE, &idx);
	send_restrict_list(restrictlist6, TRUE, &idx);
	ctl_flushpkt(0);
}


/*
 * read_ordlist - CTL_OP_READ_ORDLIST_A for ntpq -c ifstats & reslist
 */
static void
read_ordlist(
	struct recvbuf *	rbufp,
	int			restrict_mask
	)
{
	const char ifstats_s[] = "ifstats";
	const size_t ifstats_chars = COUNTOF(ifstats_s) - 1;
	const char addr_rst_s[] = "addr_restrictions";
	const size_t a_r_chars = COUNTOF(addr_rst_s) - 1;
	struct ntp_control *	cpkt;
	u_short			qdata_octets;

	/*
	 * CTL_OP_READ_ORDLIST_A was first named CTL_OP_READ_IFSTATS and
	 * used only for ntpq -c ifstats.  With the addition of reslist
	 * the same opcode was generalized to retrieve ordered lists
	 * which require authentication.  The request data is empty or
	 * contains "ifstats" (not null terminated) to retrieve local
	 * addresses and associated stats.  It is "addr_restrictions"
	 * to retrieve the IPv4 then IPv6 remote address restrictions,
	 * which are access control lists.  Other request data return
	 * CERR_UNKNOWNVAR.
	 */
	cpkt = (struct ntp_control *)&rbufp->recv_pkt;
	qdata_octets = ntohs(cpkt->count);
	if (0 == qdata_octets || (ifstats_chars == qdata_octets &&
	    !memcmp(ifstats_s, cpkt->u.data, ifstats_chars))) {
		read_ifstats(rbufp);
		return;
	}
	if (a_r_chars == qdata_octets &&
	    !memcmp(addr_rst_s, cpkt->u.data, a_r_chars)) {
		read_addr_restrictions(rbufp);
		return;
	}
	ctl_error(CERR_UNKNOWNVAR);
}


/*
 * req_nonce - CTL_OP_REQ_NONCE for ntpq -c mrulist prerequisite.
 */
static void req_nonce(
	struct recvbuf *	rbufp,
	int			restrict_mask
	)
{
	char	buf[64];

	generate_nonce(rbufp, buf, sizeof(buf));
	ctl_putunqstr("nonce", buf, strlen(buf));
	ctl_flushpkt(0);
}


/*
 * read_clockstatus - return clock radio status
 */
/*ARGSUSED*/
static void
read_clockstatus(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
#ifndef REFCLOCK
	/*
	 * If no refclock support, no data to return
	 */
	ctl_error(CERR_BADASSOC);
#else
	const struct ctl_var *	v;
	int			i;
	struct peer *		peer;
	char *			valuep;
	u_char *		wants;
	size_t			wants_alloc;
	int			gotvar;
	const u_char *		cc;
	struct ctl_var *	kv;
	struct refclockstat	cs;

	if (res_associd != 0) {
		peer = findpeerbyassoc(res_associd);
	} else {
		/*
		 * Find a clock for this jerk.	If the system peer
		 * is a clock use it, else search peer_list for one.
		 */
		if (sys_peer != NULL && (FLAG_REFCLOCK &
		    sys_peer->flags))
			peer = sys_peer;
		else
			for (peer = peer_list;
			     peer != NULL;
			     peer = peer->p_link)
				if (FLAG_REFCLOCK & peer->flags)
					break;
	}
	if (NULL == peer || !(FLAG_REFCLOCK & peer->flags)) {
		ctl_error(CERR_BADASSOC);
		return;
	}
	/*
	 * If we got here we have a peer which is a clock. Get his
	 * status.
	 */
	cs.kv_list = NULL;
	refclock_control(&peer->srcadr, NULL, &cs);
	kv = cs.kv_list;
	/*
	 * Look for variables in the packet.
	 */
	rpkt.status = htons(ctlclkstatus(&cs));
	wants_alloc = CC_MAXCODE + 1 + count_var(kv);
	wants = emalloc_zero(wants_alloc);
	gotvar = FALSE;
	while (NULL != (v = ctl_getitem(clock_var, &valuep))) {
		if (!(EOV & v->flags)) {
			wants[v->code] = TRUE;
			gotvar = TRUE;
		} else {
			v = ctl_getitem(kv, &valuep);
			if (NULL == v) {
				ctl_error(CERR_BADVALUE);
				free(wants);
				free_varlist(cs.kv_list);
				return;
			}
			if (EOV & v->flags) {
				ctl_error(CERR_UNKNOWNVAR);
				free(wants);
				free_varlist(cs.kv_list);
				return;
			}
			wants[CC_MAXCODE + 1 + v->code] = TRUE;
			gotvar = TRUE;
		}
	}

	if (gotvar) {
		for (i = 1; i <= CC_MAXCODE; i++)
			if (wants[i])
				ctl_putclock(i, &cs, TRUE);
		if (kv != NULL)
			for (i = 0; !(EOV & kv[i].flags); i++)
				if (wants[i + CC_MAXCODE + 1])
					ctl_putdata(kv[i].text,
						    strlen(kv[i].text),
						    FALSE);
	} else {
		for (cc = def_clock_var; *cc != 0; cc++)
			ctl_putclock((int)*cc, &cs, FALSE);
		for ( ; kv != NULL && !(EOV & kv->flags); kv++)
			if (DEF & kv->flags)
				ctl_putdata(kv->text, strlen(kv->text),
					    FALSE);
	}

	free(wants);
	free_varlist(cs.kv_list);

	ctl_flushpkt(0);
#endif
}


/*
 * write_clockstatus - we don't do this
 */
/*ARGSUSED*/
static void
write_clockstatus(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	ctl_error(CERR_PERMISSION);
}

/*
 * Trap support from here on down. We send async trap messages when the
 * upper levels report trouble. Traps can by set either by control
 * messages or by configuration.
 */
/*
 * set_trap - set a trap in response to a control message
 */
static void
set_trap(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	int traptype;

	/*
	 * See if this guy is allowed
	 */
	if (restrict_mask & RES_NOTRAP) {
		ctl_error(CERR_PERMISSION);
		return;
	}

	/*
	 * Determine his allowed trap type.
	 */
	traptype = TRAP_TYPE_PRIO;
	if (restrict_mask & RES_LPTRAP)
		traptype = TRAP_TYPE_NONPRIO;

	/*
	 * Call ctlsettrap() to do the work.  Return
	 * an error if it can't assign the trap.
	 */
	if (!ctlsettrap(&rbufp->recv_srcadr, rbufp->dstadr, traptype,
			(int)res_version))
		ctl_error(CERR_NORESOURCE);
	ctl_flushpkt(0);
}


/*
 * unset_trap - unset a trap in response to a control message
 */
static void
unset_trap(
	struct recvbuf *rbufp,
	int restrict_mask
	)
{
	int traptype;

	/*
	 * We don't prevent anyone from removing his own trap unless the
	 * trap is configured. Note we also must be aware of the
	 * possibility that restriction flags were changed since this
	 * guy last set his trap. Set the trap type based on this.
	 */
	traptype = TRAP_TYPE_PRIO;
	if (restrict_mask & RES_LPTRAP)
		traptype = TRAP_TYPE_NONPRIO;

	/*
	 * Call ctlclrtrap() to clear this out.
	 */
	if (!ctlclrtrap(&rbufp->recv_srcadr, rbufp->dstadr, traptype))
		ctl_error(CERR_BADASSOC);
	ctl_flushpkt(0);
}


/*
 * ctlsettrap - called to set a trap
 */
int
ctlsettrap(
	sockaddr_u *raddr,
	struct interface *linter,
	int traptype,
	int version
	)
{
	size_t n;
	struct ctl_trap *tp;
	struct ctl_trap *tptouse;

	/*
	 * See if we can find this trap.  If so, we only need update
	 * the flags and the time.
	 */
	if ((tp = ctlfindtrap(raddr, linter)) != NULL) {
		switch (traptype) {

		case TRAP_TYPE_CONFIG:
			tp->tr_flags = TRAP_INUSE|TRAP_CONFIGURED;
			break;

		case TRAP_TYPE_PRIO:
			if (tp->tr_flags & TRAP_CONFIGURED)
				return (1); /* don't change anything */
			tp->tr_flags = TRAP_INUSE;
			break;

		case TRAP_TYPE_NONPRIO:
			if (tp->tr_flags & TRAP_CONFIGURED)
				return (1); /* don't change anything */
			tp->tr_flags = TRAP_INUSE|TRAP_NONPRIO;
			break;
		}
		tp->tr_settime = current_time;
		tp->tr_resets++;
		return (1);
	}

	/*
	 * First we heard of this guy.	Try to find a trap structure
	 * for him to use, clearing out lesser priority guys if we
	 * have to. Clear out anyone who's expired while we're at it.
	 */
	tptouse = NULL;
	for (n = 0; n < COUNTOF(ctl_traps); n++) {
		tp = &ctl_traps[n];
		if ((TRAP_INUSE & tp->tr_flags) &&
		    !(TRAP_CONFIGURED & tp->tr_flags) &&
		    ((tp->tr_settime + CTL_TRAPTIME) > current_time)) {
			tp->tr_flags = 0;
			num_ctl_traps--;
		}
		if (!(TRAP_INUSE & tp->tr_flags)) {
			tptouse = tp;
		} else if (!(TRAP_CONFIGURED & tp->tr_flags)) {
			switch (traptype) {

			case TRAP_TYPE_CONFIG:
				if (tptouse == NULL) {
					tptouse = tp;
					break;
				}
				if ((TRAP_NONPRIO & tptouse->tr_flags) &&
				    !(TRAP_NONPRIO & tp->tr_flags))
					break;

				if (!(TRAP_NONPRIO & tptouse->tr_flags)
				    && (TRAP_NONPRIO & tp->tr_flags)) {
					tptouse = tp;
					break;
				}
				if (tptouse->tr_origtime <
				    tp->tr_origtime)
					tptouse = tp;
				break;

			case TRAP_TYPE_PRIO:
				if ( TRAP_NONPRIO & tp->tr_flags) {
					if (tptouse == NULL ||
					    ((TRAP_INUSE &
					      tptouse->tr_flags) &&
					     tptouse->tr_origtime <
					     tp->tr_origtime))
						tptouse = tp;
				}
				break;

			case TRAP_TYPE_NONPRIO:
				break;
			}
		}
	}

	/*
	 * If we don't have room for him return an error.
	 */
	if (tptouse == NULL)
		return (0);

	/*
	 * Set up this structure for him.
	 */
	tptouse->tr_settime = tptouse->tr_origtime = current_time;
	tptouse->tr_count = tptouse->tr_resets = 0;
	tptouse->tr_sequence = 1;
	tptouse->tr_addr = *raddr;
	tptouse->tr_localaddr = linter;
	tptouse->tr_version = (u_char) version;
	tptouse->tr_flags = TRAP_INUSE;
	if (traptype == TRAP_TYPE_CONFIG)
		tptouse->tr_flags |= TRAP_CONFIGURED;
	else if (traptype == TRAP_TYPE_NONPRIO)
		tptouse->tr_flags |= TRAP_NONPRIO;
	num_ctl_traps++;
	return (1);
}


/*
 * ctlclrtrap - called to clear a trap
 */
int
ctlclrtrap(
	sockaddr_u *raddr,
	struct interface *linter,
	int traptype
	)
{
	register struct ctl_trap *tp;

	if ((tp = ctlfindtrap(raddr, linter)) == NULL)
		return (0);

	if (tp->tr_flags & TRAP_CONFIGURED
	    && traptype != TRAP_TYPE_CONFIG)
		return (0);

	tp->tr_flags = 0;
	num_ctl_traps--;
	return (1);
}


/*
 * ctlfindtrap - find a trap given the remote and local addresses
 */
static struct ctl_trap *
ctlfindtrap(
	sockaddr_u *raddr,
	struct interface *linter
	)
{
	size_t	n;

	for (n = 0; n < COUNTOF(ctl_traps); n++)
		if ((ctl_traps[n].tr_flags & TRAP_INUSE)
		    && ADDR_PORT_EQ(raddr, &ctl_traps[n].tr_addr)
		    && (linter == ctl_traps[n].tr_localaddr))
			return &ctl_traps[n];

	return NULL;
}


/*
 * report_event - report an event to the trappers
 */
void
report_event(
	int	err,		/* error code */
	struct peer *peer,	/* peer structure pointer */
	const char *str		/* protostats string */
	)
{
	char	statstr[NTP_MAXSTRLEN];
	int	i;
	size_t	len;

	/*
	 * Report the error to the protostats file, system log and
	 * trappers.
	 */
	if (peer == NULL) {

		/*
		 * Discard a system report if the number of reports of
		 * the same type exceeds the maximum.
		 */
		if (ctl_sys_last_event != (u_char)err)
			ctl_sys_num_events= 0;
		if (ctl_sys_num_events >= CTL_SYS_MAXEVENTS)
			return;

		ctl_sys_last_event = (u_char)err;
		ctl_sys_num_events++;
		snprintf(statstr, sizeof(statstr),
		    "0.0.0.0 %04x %02x %s",
		    ctlsysstatus(), err, eventstr(err));
		if (str != NULL) {
			len = strlen(statstr);
			snprintf(statstr + len, sizeof(statstr) - len,
			    " %s", str);
		}
		NLOG(NLOG_SYSEVENT)
			msyslog(LOG_INFO, "%s", statstr);
	} else {

		/*
		 * Discard a peer report if the number of reports of
		 * the same type exceeds the maximum for that peer.
		 */
		const char *	src;
		u_char		errlast;

		errlast = (u_char)err & ~PEER_EVENT;
		if (peer->last_event != errlast)
			peer->num_events = 0;
		if (peer->num_events >= CTL_PEER_MAXEVENTS)
			return;

		peer->last_event = errlast;
		peer->num_events++;
		if (ISREFCLOCKADR(&peer->srcadr))
			src = refnumtoa(&peer->srcadr);
		else
			src = stoa(&peer->srcadr);

		snprintf(statstr, sizeof(statstr),
		    "%s %04x %02x %s", src,
		    ctlpeerstatus(peer), err, eventstr(err));
		if (str != NULL) {
			len = strlen(statstr);
			snprintf(statstr + len, sizeof(statstr) - len,
			    " %s", str);
		}
		NLOG(NLOG_PEEREVENT)
			msyslog(LOG_INFO, "%s", statstr);
	}
	record_proto_stats(statstr);
#if DEBUG
	if (debug)
		printf("event at %lu %s\n", current_time, statstr);
#endif

	/*
	 * If no trappers, return.
	 */
	if (num_ctl_traps <= 0)
		return;

	/* [Bug 3119]
	 * Peer Events should be associated with a peer -- hence the
	 * name. But there are instances where this function is called
	 * *without* a valid peer. This happens e.g. with an unsolicited
	 * CryptoNAK, or when a leap second alarm is going off while
	 * currently without a system peer.
	 *
	 * The most sensible approach to this seems to bail out here if
	 * this happens. Avoiding to call this function would also
	 * bypass the log reporting in the first part of this function,
	 * and this is probably not the best of all options.
	 *   -*-perlinger@ntp.org-*-
	 */
	if ((err & PEER_EVENT) && !peer)
		return;

	/*
	 * Set up the outgoing packet variables
	 */
	res_opcode = CTL_OP_ASYNCMSG;
	res_offset = 0;
	res_async = TRUE;
	res_authenticate = FALSE;
	datapt = rpkt.u.data;
	dataend = &rpkt.u.data[CTL_MAX_DATA_LEN];
	if (!(err & PEER_EVENT)) {
		rpkt.associd = 0;
		rpkt.status = htons(ctlsysstatus());

		/* Include the core system variables and the list. */
		for (i = 1; i <= CS_VARLIST; i++)
			ctl_putsys(i);
	} else if (NULL != peer) { /* paranoia -- skip output */
		rpkt.associd = htons(peer->associd);
		rpkt.status = htons(ctlpeerstatus(peer));

		/* Dump it all. Later, maybe less. */
		for (i = 1; i <= CP_MAX_NOAUTOKEY; i++)
			ctl_putpeer(i, peer);
#	    ifdef REFCLOCK
		/*
		 * for clock exception events: add clock variables to
		 * reflect info on exception
		 */
		if (err == PEVNT_CLOCK) {
			struct refclockstat cs;
			struct ctl_var *kv;

			cs.kv_list = NULL;
			refclock_control(&peer->srcadr, NULL, &cs);

			ctl_puthex("refclockstatus",
				   ctlclkstatus(&cs));

			for (i = 1; i <= CC_MAXCODE; i++)
				ctl_putclock(i, &cs, FALSE);
			for (kv = cs.kv_list;
			     kv != NULL && !(EOV & kv->flags);
			     kv++)
				if (DEF & kv->flags)
					ctl_putdata(kv->text,
						    strlen(kv->text),
						    FALSE);
			free_varlist(cs.kv_list);
		}
#	    endif /* REFCLOCK */
	}

	/*
	 * We're done, return.
	 */
	ctl_flushpkt(0);
}


/*
 * mprintf_event - printf-style varargs variant of report_event()
 */
int
mprintf_event(
	int		evcode,		/* event code */
	struct peer *	p,		/* may be NULL */
	const char *	fmt,		/* msnprintf format */
	...
	)
{
	va_list	ap;
	int	rc;
	char	msg[512];

	va_start(ap, fmt);
	rc = mvsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	report_event(evcode, p, msg);

	return rc;
}


/*
 * ctl_clr_stats - clear stat counters
 */
void
ctl_clr_stats(void)
{
	ctltimereset = current_time;
	numctlreq = 0;
	numctlbadpkts = 0;
	numctlresponses = 0;
	numctlfrags = 0;
	numctlerrors = 0;
	numctlfrags = 0;
	numctltooshort = 0;
	numctlinputresp = 0;
	numctlinputfrag = 0;
	numctlinputerr = 0;
	numctlbadoffset = 0;
	numctlbadversion = 0;
	numctldatatooshort = 0;
	numctlbadop = 0;
	numasyncmsgs = 0;
}

static u_short
count_var(
	const struct ctl_var *k
	)
{
	u_int c;

	if (NULL == k)
		return 0;

	c = 0;
	while (!(EOV & (k++)->flags))
		c++;

	ENSURE(c <= USHRT_MAX);
	return (u_short)c;
}


char *
add_var(
	struct ctl_var **kv,
	u_long size,
	u_short def
	)
{
	u_short		c;
	struct ctl_var *k;
	char *		buf;

	c = count_var(*kv);
	*kv  = erealloc(*kv, (c + 2) * sizeof(**kv));
	k = *kv;
	buf = emalloc(size);
	k[c].code  = c;
	k[c].text  = buf;
	k[c].flags = def;
	k[c + 1].code  = 0;
	k[c + 1].text  = NULL;
	k[c + 1].flags = EOV;

	return buf;
}


void
set_var(
	struct ctl_var **kv,
	const char *data,
	u_long size,
	u_short def
	)
{
	struct ctl_var *k;
	const char *s;
	const char *t;
	char *td;

	if (NULL == data || !size)
		return;

	k = *kv;
	if (k != NULL) {
		while (!(EOV & k->flags)) {
			if (NULL == k->text)	{
				td = emalloc(size);
				memcpy(td, data, size);
				k->text = td;
				k->flags = def;
				return;
			} else {
				s = data;
				t = k->text;
				while (*t != '=' && *s == *t) {
					s++;
					t++;
				}
				if (*s == *t && ((*t == '=') || !*t)) {
					td = erealloc((void *)(intptr_t)k->text, size);
					memcpy(td, data, size);
					k->text = td;
					k->flags = def;
					return;
				}
			}
			k++;
		}
	}
	td = add_var(kv, size, def);
	memcpy(td, data, size);
}


void
set_sys_var(
	const char *data,
	u_long size,
	u_short def
	)
{
	set_var(&ext_sys_var, data, size, def);
}


/*
 * get_ext_sys_var() retrieves the value of a user-defined variable or
 * NULL if the variable has not been setvar'd.
 */
const char *
get_ext_sys_var(const char *tag)
{
	struct ctl_var *	v;
	size_t			c;
	const char *		val;

	val = NULL;
	c = strlen(tag);
	for (v = ext_sys_var; !(EOV & v->flags); v++) {
		if (NULL != v->text && !memcmp(tag, v->text, c)) {
			if ('=' == v->text[c]) {
				val = v->text + c + 1;
				break;
			} else if ('\0' == v->text[c]) {
				val = "";
				break;
			}
		}
	}

	return val;
}


void
free_varlist(
	struct ctl_var *kv
	)
{
	struct ctl_var *k;
	if (kv) {
		for (k = kv; !(k->flags & EOV); k++)
			free((void *)(intptr_t)k->text);
		free((void *)kv);
	}
}
