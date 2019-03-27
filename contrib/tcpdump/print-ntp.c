/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	By Jeffrey Mogul/DECWRL
 *	loosely based on print-bootp.c
 */

/* \summary: Network Time Protocol (NTP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#ifdef HAVE_STRFTIME
#include <time.h>
#endif

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

/*
 * Based on ntp.h from the U of MD implementation
 *	This file is based on Version 2 of the NTP spec (RFC1119).
 */

/*
 *  Definitions for the masses
 */
#define	JAN_1970	2208988800U	/* 1970 - 1900 in seconds */

/*
 * Structure definitions for NTP fixed point values
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			       Integer Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |			       Fraction Part			     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |		  Integer Part	     |	   Fraction Part	     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
struct l_fixedpt {
	uint32_t int_part;
	uint32_t fraction;
};

struct s_fixedpt {
	uint16_t int_part;
	uint16_t fraction;
};

/* rfc2030
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |LI | VN  |Mode |    Stratum    |     Poll      |   Precision   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Root Delay                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Root Dispersion                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Reference Identifier                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                   Reference Timestamp (64)                    |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                   Originate Timestamp (64)                    |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                    Receive Timestamp (64)                     |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                    Transmit Timestamp (64)                    |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Key Identifier (optional) (32)                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                                                               |
 * |                 Message Digest (optional) (128)               |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct ntpdata {
	u_char status;		/* status of local clock and leap info */
	u_char stratum;		/* Stratum level */
	u_char ppoll;		/* poll value */
	int precision:8;
	struct s_fixedpt root_delay;
	struct s_fixedpt root_dispersion;
	uint32_t refid;
	struct l_fixedpt ref_timestamp;
	struct l_fixedpt org_timestamp;
	struct l_fixedpt rec_timestamp;
	struct l_fixedpt xmt_timestamp;
        uint32_t key_id;
        uint8_t  message_digest[16];
};
/*
 *	Leap Second Codes (high order two bits)
 */
#define	NO_WARNING	0x00	/* no warning */
#define	PLUS_SEC	0x40	/* add a second (61 seconds) */
#define	MINUS_SEC	0x80	/* minus a second (59 seconds) */
#define	ALARM		0xc0	/* alarm condition (clock unsynchronized) */

/*
 *	Clock Status Bits that Encode Version
 */
#define	NTPVERSION_1	0x08
#define	VERSIONMASK	0x38
#define LEAPMASK	0xc0
#ifdef MODEMASK
#undef MODEMASK					/* Solaris sucks */
#endif
#define	MODEMASK	0x07

/*
 *	Code values
 */
#define	MODE_UNSPEC	0	/* unspecified */
#define	MODE_SYM_ACT	1	/* symmetric active */
#define	MODE_SYM_PAS	2	/* symmetric passive */
#define	MODE_CLIENT	3	/* client */
#define	MODE_SERVER	4	/* server */
#define	MODE_BROADCAST	5	/* broadcast */
#define	MODE_RES1	6	/* reserved */
#define	MODE_RES2	7	/* reserved */

/*
 *	Stratum Definitions
 */
#define	UNSPECIFIED	0
#define	PRIM_REF	1	/* radio clock */
#define	INFO_QUERY	62	/* **** THIS implementation dependent **** */
#define	INFO_REPLY	63	/* **** THIS implementation dependent **** */

static void p_sfix(netdissect_options *ndo, const struct s_fixedpt *);
static void p_ntp_time(netdissect_options *, const struct l_fixedpt *);
static void p_ntp_delta(netdissect_options *, const struct l_fixedpt *, const struct l_fixedpt *);

static const struct tok ntp_mode_values[] = {
    { MODE_UNSPEC,    "unspecified" },
    { MODE_SYM_ACT,   "symmetric active" },
    { MODE_SYM_PAS,   "symmetric passive" },
    { MODE_CLIENT,    "Client" },
    { MODE_SERVER,    "Server" },
    { MODE_BROADCAST, "Broadcast" },
    { MODE_RES1,      "Reserved" },
    { MODE_RES2,      "Reserved" },
    { 0, NULL }
};

static const struct tok ntp_leapind_values[] = {
    { NO_WARNING,     "" },
    { PLUS_SEC,       "+1s" },
    { MINUS_SEC,      "-1s" },
    { ALARM,          "clock unsynchronized" },
    { 0, NULL }
};

static const struct tok ntp_stratum_values[] = {
	{ UNSPECIFIED,	"unspecified" },
	{ PRIM_REF, 	"primary reference" },
	{ 0, NULL }
};

/*
 * Print ntp requests
 */
void
ntp_print(netdissect_options *ndo,
          register const u_char *cp, u_int length)
{
	register const struct ntpdata *bp;
	int mode, version, leapind;

	bp = (const struct ntpdata *)cp;

	ND_TCHECK(bp->status);

	version = (int)(bp->status & VERSIONMASK) >> 3;
	ND_PRINT((ndo, "NTPv%d", version));

	mode = bp->status & MODEMASK;
	if (!ndo->ndo_vflag) {
		ND_PRINT((ndo, ", %s, length %u",
		          tok2str(ntp_mode_values, "Unknown mode", mode),
		          length));
		return;
	}

	ND_PRINT((ndo, ", length %u\n\t%s",
	          length,
	          tok2str(ntp_mode_values, "Unknown mode", mode)));

	leapind = bp->status & LEAPMASK;
	ND_PRINT((ndo, ", Leap indicator: %s (%u)",
	          tok2str(ntp_leapind_values, "Unknown", leapind),
	          leapind));

	ND_TCHECK(bp->stratum);
	ND_PRINT((ndo, ", Stratum %u (%s)",
		bp->stratum,
		tok2str(ntp_stratum_values, (bp->stratum >=2 && bp->stratum<=15) ? "secondary reference" : "reserved", bp->stratum)));

	ND_TCHECK(bp->ppoll);
	ND_PRINT((ndo, ", poll %u (%us)", bp->ppoll, 1 << bp->ppoll));

	/* Can't ND_TCHECK bp->precision bitfield so bp->distance + 0 instead */
	ND_TCHECK2(bp->root_delay, 0);
	ND_PRINT((ndo, ", precision %d", bp->precision));

	ND_TCHECK(bp->root_delay);
	ND_PRINT((ndo, "\n\tRoot Delay: "));
	p_sfix(ndo, &bp->root_delay);

	ND_TCHECK(bp->root_dispersion);
	ND_PRINT((ndo, ", Root dispersion: "));
	p_sfix(ndo, &bp->root_dispersion);

	ND_TCHECK(bp->refid);
	ND_PRINT((ndo, ", Reference-ID: "));
	/* Interpretation depends on stratum */
	switch (bp->stratum) {

	case UNSPECIFIED:
		ND_PRINT((ndo, "(unspec)"));
		break;

	case PRIM_REF:
		if (fn_printn(ndo, (const u_char *)&(bp->refid), 4, ndo->ndo_snapend))
			goto trunc;
		break;

	case INFO_QUERY:
		ND_PRINT((ndo, "%s INFO_QUERY", ipaddr_string(ndo, &(bp->refid))));
		/* this doesn't have more content */
		return;

	case INFO_REPLY:
		ND_PRINT((ndo, "%s INFO_REPLY", ipaddr_string(ndo, &(bp->refid))));
		/* this is too complex to be worth printing */
		return;

	default:
		ND_PRINT((ndo, "%s", ipaddr_string(ndo, &(bp->refid))));
		break;
	}

	ND_TCHECK(bp->ref_timestamp);
	ND_PRINT((ndo, "\n\t  Reference Timestamp:  "));
	p_ntp_time(ndo, &(bp->ref_timestamp));

	ND_TCHECK(bp->org_timestamp);
	ND_PRINT((ndo, "\n\t  Originator Timestamp: "));
	p_ntp_time(ndo, &(bp->org_timestamp));

	ND_TCHECK(bp->rec_timestamp);
	ND_PRINT((ndo, "\n\t  Receive Timestamp:    "));
	p_ntp_time(ndo, &(bp->rec_timestamp));

	ND_TCHECK(bp->xmt_timestamp);
	ND_PRINT((ndo, "\n\t  Transmit Timestamp:   "));
	p_ntp_time(ndo, &(bp->xmt_timestamp));

	ND_PRINT((ndo, "\n\t    Originator - Receive Timestamp:  "));
	p_ntp_delta(ndo, &(bp->org_timestamp), &(bp->rec_timestamp));

	ND_PRINT((ndo, "\n\t    Originator - Transmit Timestamp: "));
	p_ntp_delta(ndo, &(bp->org_timestamp), &(bp->xmt_timestamp));

	if ( (sizeof(struct ntpdata) - length) == 16) { 	/* Optional: key-id */
		ND_TCHECK(bp->key_id);
		ND_PRINT((ndo, "\n\tKey id: %u", bp->key_id));
	} else if ( (sizeof(struct ntpdata) - length) == 0) { 	/* Optional: key-id + authentication */
		ND_TCHECK(bp->key_id);
		ND_PRINT((ndo, "\n\tKey id: %u", bp->key_id));
		ND_TCHECK2(bp->message_digest, sizeof (bp->message_digest));
                ND_PRINT((ndo, "\n\tAuthentication: %08x%08x%08x%08x",
        		       EXTRACT_32BITS(bp->message_digest),
		               EXTRACT_32BITS(bp->message_digest + 4),
		               EXTRACT_32BITS(bp->message_digest + 8),
		               EXTRACT_32BITS(bp->message_digest + 12)));
        }
	return;

trunc:
	ND_PRINT((ndo, " [|ntp]"));
}

static void
p_sfix(netdissect_options *ndo,
       register const struct s_fixedpt *sfp)
{
	register int i;
	register int f;
	register double ff;

	i = EXTRACT_16BITS(&sfp->int_part);
	f = EXTRACT_16BITS(&sfp->fraction);
	ff = f / 65536.0;		/* shift radix point by 16 bits */
	f = (int)(ff * 1000000.0);	/* Treat fraction as parts per million */
	ND_PRINT((ndo, "%d.%06d", i, f));
}

#define	FMAXINT	(4294967296.0)	/* floating point rep. of MAXINT */

static void
p_ntp_time(netdissect_options *ndo,
           register const struct l_fixedpt *lfp)
{
	register int32_t i;
	register uint32_t uf;
	register uint32_t f;
	register double ff;

	i = EXTRACT_32BITS(&lfp->int_part);
	uf = EXTRACT_32BITS(&lfp->fraction);
	ff = uf;
	if (ff < 0.0)		/* some compilers are buggy */
		ff += FMAXINT;
	ff = ff / FMAXINT;			/* shift radix point by 32 bits */
	f = (uint32_t)(ff * 1000000000.0);	/* treat fraction as parts per billion */
	ND_PRINT((ndo, "%u.%09d", i, f));

#ifdef HAVE_STRFTIME
	/*
	 * print the time in human-readable format.
	 */
	if (i) {
	    time_t seconds = i - JAN_1970;
	    struct tm *tm;
	    char time_buf[128];

	    tm = localtime(&seconds);
	    strftime(time_buf, sizeof (time_buf), "%Y/%m/%d %H:%M:%S", tm);
	    ND_PRINT((ndo, " (%s)", time_buf));
	}
#endif
}

/* Prints time difference between *lfp and *olfp */
static void
p_ntp_delta(netdissect_options *ndo,
            register const struct l_fixedpt *olfp,
            register const struct l_fixedpt *lfp)
{
	register int32_t i;
	register uint32_t u, uf;
	register uint32_t ou, ouf;
	register uint32_t f;
	register double ff;
	int signbit;

	u = EXTRACT_32BITS(&lfp->int_part);
	ou = EXTRACT_32BITS(&olfp->int_part);
	uf = EXTRACT_32BITS(&lfp->fraction);
	ouf = EXTRACT_32BITS(&olfp->fraction);
	if (ou == 0 && ouf == 0) {
		p_ntp_time(ndo, lfp);
		return;
	}

	i = u - ou;

	if (i > 0) {		/* new is definitely greater than old */
		signbit = 0;
		f = uf - ouf;
		if (ouf > uf)	/* must borrow from high-order bits */
			i -= 1;
	} else if (i < 0) {	/* new is definitely less than old */
		signbit = 1;
		f = ouf - uf;
		if (uf > ouf)	/* must carry into the high-order bits */
			i += 1;
		i = -i;
	} else {		/* int_part is zero */
		if (uf > ouf) {
			signbit = 0;
			f = uf - ouf;
		} else {
			signbit = 1;
			f = ouf - uf;
		}
	}

	ff = f;
	if (ff < 0.0)		/* some compilers are buggy */
		ff += FMAXINT;
	ff = ff / FMAXINT;			/* shift radix point by 32 bits */
	f = (uint32_t)(ff * 1000000000.0);	/* treat fraction as parts per billion */
	ND_PRINT((ndo, "%s%d.%09d", signbit ? "-" : "+", i, f));
}

