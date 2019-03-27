/*
 * pretty printing of status information
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include "ntp_stdlib.h"
#include "ntp_fp.h"
#include "ntp.h"
#include "lib_strbuf.h"
#include "ntp_refclock.h"
#include "ntp_control.h"
#include "ntp_string.h"
#ifdef KERNEL_PLL
# include "ntp_syscall.h"
#endif


/*
 * Structure for turning various constants into a readable string.
 */
struct codestring {
	int code;
	const char * const string1;
	const char * const string0;
};

/*
 * Leap status (leap)
 */
static const struct codestring leap_codes[] = {
	{ LEAP_NOWARNING,	"leap_none",	0 },
	{ LEAP_ADDSECOND,	"leap_add_sec",	0 },
	{ LEAP_DELSECOND,	"leap_del_sec",	0 },
	{ LEAP_NOTINSYNC,	"leap_alarm",	0 },
	{ -1,			"leap",		0 }
};

/*
 * Clock source status (sync)
 */
static const struct codestring sync_codes[] = {
	{ CTL_SST_TS_UNSPEC,	"sync_unspec",		0 },
	{ CTL_SST_TS_ATOM,	"sync_pps",		0 },
	{ CTL_SST_TS_LF,	"sync_lf_radio",	0 },
	{ CTL_SST_TS_HF,	"sync_hf_radio",	0 },
	{ CTL_SST_TS_UHF,	"sync_uhf_radio",	0 },
	{ CTL_SST_TS_LOCAL,	"sync_local",		0 },
	{ CTL_SST_TS_NTP,	"sync_ntp",		0 },
	{ CTL_SST_TS_UDPTIME,	"sync_other",		0 },
	{ CTL_SST_TS_WRSTWTCH,	"sync_wristwatch",	0 },
	{ CTL_SST_TS_TELEPHONE,	"sync_telephone",	0 },
	{ -1,			"sync",			0 }
};

/*
 * Peer selection status (sel)
 */
static const struct codestring select_codes[] = {
	{ CTL_PST_SEL_REJECT,	"sel_reject",		0 },
	{ CTL_PST_SEL_SANE,	"sel_falsetick",	0 },
	{ CTL_PST_SEL_CORRECT,	"sel_excess",		0 },
	{ CTL_PST_SEL_SELCAND,	"sel_outlier",		0 },
	{ CTL_PST_SEL_SYNCCAND,	"sel_candidate",	0 },
	{ CTL_PST_SEL_EXCESS,	"sel_backup",		0 },
	{ CTL_PST_SEL_SYSPEER,	"sel_sys.peer",		0 },
	{ CTL_PST_SEL_PPS,	"sel_pps.peer",		0 },
	{ -1,			"sel",			0 }
};

/*
 * Clock status (clk)
 */
static const struct codestring clock_codes[] = {
	{ CTL_CLK_OKAY,		"clk_unspec",		0 },
	{ CTL_CLK_NOREPLY,	"clk_no_reply",		0 },
	{ CTL_CLK_BADFORMAT,	"clk_bad_format",	0 },
	{ CTL_CLK_FAULT,	"clk_fault",		0 },
	{ CTL_CLK_PROPAGATION,	"clk_bad_signal",	0 },
	{ CTL_CLK_BADDATE,	"clk_bad_date",		0 },
	{ CTL_CLK_BADTIME,	"clk_bad_time",		0 },
	{ -1,			"clk",			0 }
};


#ifdef FLASH_CODES_UNUSED
/*
 * Flash bits -- see ntpq.c tstflags & tstflagnames
 */
static const struct codestring flash_codes[] = {
	{ TEST1,		"pkt_dup",	0 },
	{ TEST2,		"pkt_bogus",	0 },
	{ TEST3,		"pkt_unsync",	0 },
	{ TEST4,		"pkt_denied",	0 },
	{ TEST5,		"pkt_auth",	0 },
	{ TEST6,		"pkt_stratum",	0 },
	{ TEST7,		"pkt_header",	0 },
	{ TEST8,		"pkt_autokey",	0 },
	{ TEST9,		"pkt_crypto",	0 },
	{ TEST10,		"peer_stratum",	0 },
	{ TEST11,		"peer_dist",	0 },
	{ TEST12,		"peer_loop",	0 },
	{ TEST13,		"peer_unreach",	0 },
	{ -1,			"flash",	0 }
};
#endif


/*
 * System events (sys)
 */
static const struct codestring sys_codes[] = {
	{ EVNT_UNSPEC,		"unspecified",			0 },
	{ EVNT_NSET,		"freq_not_set",			0 },
	{ EVNT_FSET,		"freq_set",			0 },
	{ EVNT_SPIK,		"spike_detect",			0 },
	{ EVNT_FREQ,		"freq_mode",			0 },
	{ EVNT_SYNC,		"clock_sync",			0 },
	{ EVNT_SYSRESTART,	"restart",			0 },
	{ EVNT_SYSFAULT,	"panic_stop",			0 },
	{ EVNT_NOPEER,		"no_sys_peer",			0 },
	{ EVNT_ARMED,		"leap_armed",			0 },
	{ EVNT_DISARMED,	"leap_disarmed",		0 },
	{ EVNT_LEAP,		"leap_event",			0 },
	{ EVNT_CLOCKRESET,	"clock_step",			0 },
	{ EVNT_KERN,		"kern",				0 },
	{ EVNT_TAI,		"TAI",				0 },
	{ EVNT_LEAPVAL,		"stale_leapsecond_values",	0 },
	{ -1,			"",				0 }
};

/*
 * Peer events (peer)
 */
static const struct codestring peer_codes[] = {
	{ PEVNT_MOBIL & ~PEER_EVENT,	"mobilize",		0 },
	{ PEVNT_DEMOBIL & ~PEER_EVENT,	"demobilize",		0 },
	{ PEVNT_UNREACH & ~PEER_EVENT,	"unreachable",		0 },
	{ PEVNT_REACH & ~PEER_EVENT,	"reachable",		0 },
	{ PEVNT_RESTART & ~PEER_EVENT,	"restart",		0 },
	{ PEVNT_REPLY & ~PEER_EVENT,	"no_reply",		0 },
	{ PEVNT_RATE & ~PEER_EVENT,	"rate_exceeded",	0 },
	{ PEVNT_DENY & ~PEER_EVENT,	"access_denied",	0 },
	{ PEVNT_ARMED & ~PEER_EVENT,	"leap_armed",		0 },
	{ PEVNT_NEWPEER & ~PEER_EVENT,	"sys_peer",		0 },
	{ PEVNT_CLOCK & ~PEER_EVENT,	"clock_event",		0 },
	{ PEVNT_AUTH & ~PEER_EVENT,	"bad_auth",		0 },
	{ PEVNT_POPCORN & ~PEER_EVENT,	"popcorn",		0 },
	{ PEVNT_XLEAVE & ~PEER_EVENT,	"interleave_mode",	0 },
	{ PEVNT_XERR & ~PEER_EVENT,	"interleave_error",	0 },
	{ -1,				"",			0 }
};

/*
 * Peer status bits
 */
static const struct codestring peer_st_bits[] = {
	{ CTL_PST_CONFIG,		"conf",		0 },
	{ CTL_PST_AUTHENABLE,		"authenb",	0 },
	{ CTL_PST_AUTHENTIC,		"auth",		0 },
	{ CTL_PST_REACH,		"reach",	0 },
	{ CTL_PST_BCAST,		"bcast",	0 },
	/* not used with getcode(), no terminating entry needed */
};

/*
 * Restriction match bits
 */
static const struct codestring res_match_bits[] = {
	{ RESM_NTPONLY,			"ntpport",	0 },
	{ RESM_INTERFACE,		"interface",	0 },
	{ RESM_SOURCE,			"source",	0 },
	/* not used with getcode(), no terminating entry needed */
};

/*
 * Restriction access bits
 */
static const struct codestring res_access_bits[] = {
	{ RES_IGNORE,			"ignore",	0 },
	{ RES_DONTSERVE,		"noserve",	"serve" },
	{ RES_DONTTRUST,		"notrust",	"trust" },
	{ RES_NOQUERY,			"noquery",	"query" },
	{ RES_NOMODIFY,			"nomodify",	0 },
	{ RES_NOPEER,			"nopeer",	"peer" },
	{ RES_NOEPEER,			"noepeer",	"epeer" },
	{ RES_NOTRAP,			"notrap",	"trap" },
	{ RES_LPTRAP,			"lptrap",	0 },
	{ RES_LIMITED,			"limited",	0 },
	{ RES_VERSION,			"version",	0 },
	{ RES_KOD,			"kod",		0 },
	{ RES_FLAKE,			"flake",	0 },
	/* not used with getcode(), no terminating entry needed */
};

#ifdef AUTOKEY
/*
 * Crypto events (cryp)
 */
static const struct codestring crypto_codes[] = {
	{ XEVNT_OK & ~CRPT_EVENT,	"success",			0 },
	{ XEVNT_LEN & ~CRPT_EVENT,	"bad_field_format_or_length",	0 },
	{ XEVNT_TSP & ~CRPT_EVENT,	"bad_timestamp",		0 },
	{ XEVNT_FSP & ~CRPT_EVENT,	"bad_filestamp",		0 },
	{ XEVNT_PUB & ~CRPT_EVENT,	"bad_or_missing_public_key",	0 },
	{ XEVNT_MD & ~CRPT_EVENT,	"unsupported_digest_type",	0 },
	{ XEVNT_KEY & ~CRPT_EVENT,	"unsupported_identity_type",	0 },
	{ XEVNT_SGL & ~CRPT_EVENT,	"bad_signature_length",		0 },
	{ XEVNT_SIG & ~CRPT_EVENT,	"signature_not_verified",	0 },
	{ XEVNT_VFY & ~CRPT_EVENT,	"certificate_not_verified",	0 },
	{ XEVNT_PER & ~CRPT_EVENT,	"host_certificate_expired",	0 },
	{ XEVNT_CKY & ~CRPT_EVENT,	"bad_or_missing_cookie",	0 },
	{ XEVNT_DAT & ~CRPT_EVENT,	"bad_or_missing_leapseconds",	0 },
	{ XEVNT_CRT & ~CRPT_EVENT,	"bad_or_missing_certificate",	0 },	
	{ XEVNT_ID & ~CRPT_EVENT,	"bad_or_missing_group key",	0 },
	{ XEVNT_ERR & ~CRPT_EVENT,	"protocol_error",		0 },
	{ -1,				"",				0 }
};
#endif	/* AUTOKEY */

#ifdef KERNEL_PLL
/*
 * kernel discipline status bits
 */
static const struct codestring k_st_bits[] = {
# ifdef STA_PLL
	{ STA_PLL,			"pll",		0 },
# endif
# ifdef STA_PPSFREQ
	{ STA_PPSFREQ,			"ppsfreq",	0 },
# endif
# ifdef STA_PPSTIME
	{ STA_PPSTIME,			"ppstime",	0 },
# endif
# ifdef STA_FLL
	{ STA_FLL,			"fll",		0 },
# endif
# ifdef STA_INS
	{ STA_INS,			"ins",		0 },
# endif
# ifdef STA_DEL
	{ STA_DEL,			"del",		0 },
# endif
# ifdef STA_UNSYNC
	{ STA_UNSYNC,			"unsync",	0 },
# endif
# ifdef STA_FREQHOLD
	{ STA_FREQHOLD,			"freqhold",	0 },
# endif
# ifdef STA_PPSSIGNAL
	{ STA_PPSSIGNAL,		"ppssignal",	0 },
# endif
# ifdef STA_PPSJITTER
	{ STA_PPSJITTER,		"ppsjitter",	0 },
# endif
# ifdef STA_PPSWANDER
	{ STA_PPSWANDER,		"ppswander",	0 },
# endif
# ifdef STA_PPSERROR
	{ STA_PPSERROR,			"ppserror",	0 },
# endif
# ifdef STA_CLOCKERR
	{ STA_CLOCKERR,			"clockerr",	0 },
# endif
# ifdef STA_NANO
	{ STA_NANO,			"nano",		0 },
# endif
# ifdef STA_MODE
	{ STA_MODE,			"mode=fll",	0 },
# endif
# ifdef STA_CLK
	{ STA_CLK,			"src=B",	0 },
# endif
	/* not used with getcode(), no terminating entry needed */
};
#endif	/* KERNEL_PLL */

/* Forwards */
static const char *	getcode(int, const struct codestring *);
static const char *	getevents(int);
static const char *	peer_st_flags(u_char pst);

/*
 * getcode - return string corresponding to code
 */
static const char *
getcode(
	int				code,
	const struct codestring *	codetab
	)
{
	char *	buf;

	while (codetab->code != -1) {
		if (codetab->code == code)
			return codetab->string1;
		codetab++;
	}

	LIB_GETBUF(buf);
	snprintf(buf, LIB_BUFLENGTH, "%s_%d", codetab->string1, code);

	return buf;
}

/*
 * getevents - return a descriptive string for the event count
 */
static const char *
getevents(
	int cnt
	)
{
	char *	buf;

	if (cnt == 0)
		return "no events";

	LIB_GETBUF(buf);
	snprintf(buf, LIB_BUFLENGTH, "%d event%s", cnt,
		 (1 == cnt)
		     ? ""
		     : "s");

	return buf;
}


/*
 * decode_bitflags()
 *
 * returns a human-readable string with a keyword from tab for each bit
 * set in bits, separating multiple entries with text of sep2.
 */
static const char *
decode_bitflags(
	int				bits,
	const char *			sep2,
	const struct codestring *	tab,
	size_t				tab_ct
	)
{
	const char *	sep;
	char *		buf;
	char *		pch;
	char *		lim;
	size_t		b;
	int		rc;
	int		saved_errno;	/* for use in DPRINTF with %m */

	saved_errno = errno;
	LIB_GETBUF(buf);
	pch = buf;
	lim = buf + LIB_BUFLENGTH;
	sep = "";

	for (b = 0; b < tab_ct; b++) {
		const char * flagstr;

		if (tab[b].code & bits) {
			flagstr = tab[b].string1;
		} else {
			flagstr = tab[b].string0;
		}

		if (flagstr) {
			size_t avail = lim - pch;
			rc = snprintf(pch, avail, "%s%s", sep,
				      flagstr);
			if ((size_t)rc >= avail)
				goto toosmall;
			pch += rc;
			sep = sep2;
		}
	}

	return buf;

    toosmall:
	snprintf(buf, LIB_BUFLENGTH,
		 "decode_bitflags(%s) can't decode 0x%x in %d bytes",
		 (tab == peer_st_bits)
		     ? "peer_st"
		     : 
#ifdef KERNEL_PLL
		       (tab == k_st_bits)
			   ? "kern_st"
			   :
#endif
			     "",
		 bits, (int)LIB_BUFLENGTH);
	errno = saved_errno;

	return buf;
}


static const char *
peer_st_flags(
	u_char pst
	)
{
	return decode_bitflags(pst, ", ", peer_st_bits,
			       COUNTOF(peer_st_bits));
}


const char *
res_match_flags(
	u_short mf
	)
{
	return decode_bitflags(mf, " ", res_match_bits,
			       COUNTOF(res_match_bits));
}


const char *
res_access_flags(
	u_short af
	)
{
	return decode_bitflags(af, " ", res_access_bits,
			       COUNTOF(res_access_bits));
}


#ifdef KERNEL_PLL
const char *
k_st_flags(
	u_int32 st
	)
{
	return decode_bitflags(st, " ", k_st_bits, COUNTOF(k_st_bits));
}
#endif	/* KERNEL_PLL */


/*
 * statustoa - return a descriptive string for a peer status
 */
char *
statustoa(
	int type,
	int st
	)
{
	char *	cb;
	char *	cc;
	u_char	pst;

	LIB_GETBUF(cb);

	switch (type) {

	case TYPE_SYS:
		snprintf(cb, LIB_BUFLENGTH, "%s, %s, %s, %s",
			 getcode(CTL_SYS_LI(st), leap_codes),
			 getcode(CTL_SYS_SOURCE(st), sync_codes),
			 getevents(CTL_SYS_NEVNT(st)),
			 getcode(CTL_SYS_EVENT(st), sys_codes));
		break;
	
	case TYPE_PEER:
		pst = (u_char)CTL_PEER_STATVAL(st);
		snprintf(cb, LIB_BUFLENGTH, "%s, %s, %s",
			 peer_st_flags(pst),
			 getcode(pst & 0x7, select_codes),
			 getevents(CTL_PEER_NEVNT(st)));
		if (CTL_PEER_EVENT(st) != EVNT_UNSPEC) {
			cc = cb + strlen(cb);
			snprintf(cc, LIB_BUFLENGTH - (cc - cb), ", %s",
				 getcode(CTL_PEER_EVENT(st),
					 peer_codes));
		}
		break;
	
	case TYPE_CLOCK:
		snprintf(cb, LIB_BUFLENGTH, "%s, %s",
			 getevents(CTL_SYS_NEVNT(st)),
			 getcode((st) & 0xf, clock_codes));
		break;
	}

	return cb;
}

const char *
eventstr(
	int num
	)
{
	if (num & PEER_EVENT)
		return (getcode(num & ~PEER_EVENT, peer_codes));
#ifdef AUTOKEY
	else if (num & CRPT_EVENT)
		return (getcode(num & ~CRPT_EVENT, crypto_codes));
#endif	/* AUTOKEY */
	else
		return (getcode(num, sys_codes));
}

const char *
ceventstr(
	int num
	)
{
	return getcode(num, clock_codes);
}
