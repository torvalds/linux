/*	$NetBSD: print-telnet.c,v 1.2 1999/10/11 12:40:12 sjg Exp $ 	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Simon J. Gerraty.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *      @(#)Copyright (c) 1994, Simon J. Gerraty.
 *
 *      This is free software.  It comes with NO WARRANTY.
 *      Permission to use, modify and distribute this source code
 *      is granted subject to the following conditions.
 *      1/ that the above copyright notice and this notice
 *      are preserved in all copies.
 */

/* \summary: Telnet option printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>

#include "netdissect.h"

static const char tstr[] = " [|telnet]";

#define TELCMDS
#define TELOPTS

/*	NetBSD: telnet.h,v 1.9 2001/06/11 01:50:50 wiz Exp 	*/

/*
 * Definitions for the TELNET protocol.
 */
#define	IAC	255		/* interpret as command: */
#define	DONT	254		/* you are not to use option */
#define	DO	253		/* please, you use option */
#define	WONT	252		/* I won't use option */
#define	WILL	251		/* I will use option */
#define	SB	250		/* interpret as subnegotiation */
#define	GA	249		/* you may reverse the line */
#define	EL	248		/* erase the current line */
#define	EC	247		/* erase the current character */
#define	AYT	246		/* are you there */
#define	AO	245		/* abort output--but let prog finish */
#define	IP	244		/* interrupt process--permanently */
#define	BREAK	243		/* break */
#define	DM	242		/* data mark--for connect. cleaning */
#define	NOP	241		/* nop */
#define	SE	240		/* end sub negotiation */
#define EOR     239             /* end of record (transparent mode) */
#define	ABORT	238		/* Abort process */
#define	SUSP	237		/* Suspend process */
#define	xEOF	236		/* End of file: EOF is already used... */

#define SYNCH	242		/* for telfunc calls */

#ifdef TELCMDS
static const char *telcmds[] = {
	"EOF", "SUSP", "ABORT", "EOR",
	"SE", "NOP", "DMARK", "BRK", "IP", "AO", "AYT", "EC",
	"EL", "GA", "SB", "WILL", "WONT", "DO", "DONT", "IAC", 0,
};
#else
extern char *telcmds[];
#endif

#define	TELCMD_FIRST	xEOF
#define	TELCMD_LAST	IAC
#define	TELCMD_OK(x)	((unsigned int)(x) <= TELCMD_LAST && \
			 (unsigned int)(x) >= TELCMD_FIRST)
#define	TELCMD(x)	telcmds[(x)-TELCMD_FIRST]

/* telnet options */
#define TELOPT_BINARY	0	/* 8-bit data path */
#define TELOPT_ECHO	1	/* echo */
#define	TELOPT_RCP	2	/* prepare to reconnect */
#define	TELOPT_SGA	3	/* suppress go ahead */
#define	TELOPT_NAMS	4	/* approximate message size */
#define	TELOPT_STATUS	5	/* give status */
#define	TELOPT_TM	6	/* timing mark */
#define	TELOPT_RCTE	7	/* remote controlled transmission and echo */
#define TELOPT_NAOL 	8	/* negotiate about output line width */
#define TELOPT_NAOP 	9	/* negotiate about output page size */
#define TELOPT_NAOCRD	10	/* negotiate about CR disposition */
#define TELOPT_NAOHTS	11	/* negotiate about horizontal tabstops */
#define TELOPT_NAOHTD	12	/* negotiate about horizontal tab disposition */
#define TELOPT_NAOFFD	13	/* negotiate about formfeed disposition */
#define TELOPT_NAOVTS	14	/* negotiate about vertical tab stops */
#define TELOPT_NAOVTD	15	/* negotiate about vertical tab disposition */
#define TELOPT_NAOLFD	16	/* negotiate about output LF disposition */
#define TELOPT_XASCII	17	/* extended ascic character set */
#define	TELOPT_LOGOUT	18	/* force logout */
#define	TELOPT_BM	19	/* byte macro */
#define	TELOPT_DET	20	/* data entry terminal */
#define	TELOPT_SUPDUP	21	/* supdup protocol */
#define	TELOPT_SUPDUPOUTPUT 22	/* supdup output */
#define	TELOPT_SNDLOC	23	/* send location */
#define	TELOPT_TTYPE	24	/* terminal type */
#define	TELOPT_EOR	25	/* end or record */
#define	TELOPT_TUID	26	/* TACACS user identification */
#define	TELOPT_OUTMRK	27	/* output marking */
#define	TELOPT_TTYLOC	28	/* terminal location number */
#define	TELOPT_3270REGIME 29	/* 3270 regime */
#define	TELOPT_X3PAD	30	/* X.3 PAD */
#define	TELOPT_NAWS	31	/* window size */
#define	TELOPT_TSPEED	32	/* terminal speed */
#define	TELOPT_LFLOW	33	/* remote flow control */
#define TELOPT_LINEMODE	34	/* Linemode option */
#define TELOPT_XDISPLOC	35	/* X Display Location */
#define TELOPT_OLD_ENVIRON 36	/* Old - Environment variables */
#define	TELOPT_AUTHENTICATION 37/* Authenticate */
#define	TELOPT_ENCRYPT	38	/* Encryption option */
#define TELOPT_NEW_ENVIRON 39	/* New - Environment variables */
#define	TELOPT_EXOPL	255	/* extended-options-list */


#define	NTELOPTS	(1+TELOPT_NEW_ENVIRON)
#ifdef TELOPTS
static const char *telopts[NTELOPTS+1] = {
	"BINARY", "ECHO", "RCP", "SUPPRESS GO AHEAD", "NAME",
	"STATUS", "TIMING MARK", "RCTE", "NAOL", "NAOP",
	"NAOCRD", "NAOHTS", "NAOHTD", "NAOFFD", "NAOVTS",
	"NAOVTD", "NAOLFD", "EXTEND ASCII", "LOGOUT", "BYTE MACRO",
	"DATA ENTRY TERMINAL", "SUPDUP", "SUPDUP OUTPUT",
	"SEND LOCATION", "TERMINAL TYPE", "END OF RECORD",
	"TACACS UID", "OUTPUT MARKING", "TTYLOC",
	"3270 REGIME", "X.3 PAD", "NAWS", "TSPEED", "LFLOW",
	"LINEMODE", "XDISPLOC", "OLD-ENVIRON", "AUTHENTICATION",
	"ENCRYPT", "NEW-ENVIRON",
	0,
};
#define	TELOPT_FIRST	TELOPT_BINARY
#define	TELOPT_LAST	TELOPT_NEW_ENVIRON
#define	TELOPT_OK(x)	((unsigned int)(x) <= TELOPT_LAST)
#define	TELOPT(x)	telopts[(x)-TELOPT_FIRST]
#endif

/* sub-option qualifiers */
#define	TELQUAL_IS	0	/* option is... */
#define	TELQUAL_SEND	1	/* send option */
#define	TELQUAL_INFO	2	/* ENVIRON: informational version of IS */
#define	TELQUAL_REPLY	2	/* AUTHENTICATION: client version of IS */
#define	TELQUAL_NAME	3	/* AUTHENTICATION: client version of IS */

#define	LFLOW_OFF		0	/* Disable remote flow control */
#define	LFLOW_ON		1	/* Enable remote flow control */
#define	LFLOW_RESTART_ANY	2	/* Restart output on any char */
#define	LFLOW_RESTART_XON	3	/* Restart output only on XON */

/*
 * LINEMODE suboptions
 */

#define	LM_MODE		1
#define	LM_FORWARDMASK	2
#define	LM_SLC		3

#define	MODE_EDIT	0x01
#define	MODE_TRAPSIG	0x02
#define	MODE_ACK	0x04
#define MODE_SOFT_TAB	0x08
#define MODE_LIT_ECHO	0x10

#define	MODE_MASK	0x1f

#define	SLC_SYNCH	1
#define	SLC_BRK		2
#define	SLC_IP		3
#define	SLC_AO		4
#define	SLC_AYT		5
#define	SLC_EOR		6
#define	SLC_ABORT	7
#define	SLC_EOF		8
#define	SLC_SUSP	9
#define	SLC_EC		10
#define	SLC_EL		11
#define	SLC_EW		12
#define	SLC_RP		13
#define	SLC_LNEXT	14
#define	SLC_XON		15
#define	SLC_XOFF	16
#define	SLC_FORW1	17
#define	SLC_FORW2	18
#define	SLC_MCL         19
#define	SLC_MCR         20
#define	SLC_MCWL        21
#define	SLC_MCWR        22
#define	SLC_MCBOL       23
#define	SLC_MCEOL       24
#define	SLC_INSRT       25
#define	SLC_OVER        26
#define	SLC_ECR         27
#define	SLC_EWR         28
#define	SLC_EBOL        29
#define	SLC_EEOL        30

#define	NSLC		30

/*
 * For backwards compatibility, we define SLC_NAMES to be the
 * list of names if SLC_NAMES is not defined.
 */
#define	SLC_NAMELIST	"0", "SYNCH", "BRK", "IP", "AO", "AYT", "EOR",	\
			"ABORT", "EOF", "SUSP", "EC", "EL", "EW", "RP",	\
			"LNEXT", "XON", "XOFF", "FORW1", "FORW2",	\
			"MCL", "MCR", "MCWL", "MCWR", "MCBOL",		\
			"MCEOL", "INSRT", "OVER", "ECR", "EWR",		\
			"EBOL", "EEOL",					\
			0,

#ifdef	SLC_NAMES
const char *slc_names[] = {
	SLC_NAMELIST
};
#else
extern char *slc_names[];
#define	SLC_NAMES SLC_NAMELIST
#endif

#define	SLC_NAME_OK(x)	((unsigned int)(x) <= NSLC)
#define SLC_NAME(x)	slc_names[x]

#define	SLC_NOSUPPORT	0
#define	SLC_CANTCHANGE	1
#define	SLC_VARIABLE	2
#define	SLC_DEFAULT	3
#define	SLC_LEVELBITS	0x03

#define	SLC_FUNC	0
#define	SLC_FLAGS	1
#define	SLC_VALUE	2

#define	SLC_ACK		0x80
#define	SLC_FLUSHIN	0x40
#define	SLC_FLUSHOUT	0x20

#define	OLD_ENV_VAR	1
#define	OLD_ENV_VALUE	0
#define	NEW_ENV_VAR	0
#define	NEW_ENV_VALUE	1
#define	ENV_ESC		2
#define ENV_USERVAR	3

/*
 * AUTHENTICATION suboptions
 */

/*
 * Who is authenticating who ...
 */
#define	AUTH_WHO_CLIENT		0	/* Client authenticating server */
#define	AUTH_WHO_SERVER		1	/* Server authenticating client */
#define	AUTH_WHO_MASK		1

#define	AUTHTYPE_NULL		0
#define	AUTHTYPE_KERBEROS_V4	1
#define	AUTHTYPE_KERBEROS_V5	2
#define	AUTHTYPE_SPX		3
#define	AUTHTYPE_MINK		4
#define	AUTHTYPE_CNT		5

#define	AUTHTYPE_TEST		99

#ifdef	AUTH_NAMES
const char *authtype_names[] = {
	"NULL", "KERBEROS_V4", "KERBEROS_V5", "SPX", "MINK", 0,
};
#else
extern char *authtype_names[];
#endif

#define	AUTHTYPE_NAME_OK(x)	((unsigned int)(x) < AUTHTYPE_CNT)
#define	AUTHTYPE_NAME(x)	authtype_names[x]

/*
 * ENCRYPTion suboptions
 */
#define	ENCRYPT_IS		0	/* I pick encryption type ... */
#define	ENCRYPT_SUPPORT		1	/* I support encryption types ... */
#define	ENCRYPT_REPLY		2	/* Initial setup response */
#define	ENCRYPT_START		3	/* Am starting to send encrypted */
#define	ENCRYPT_END		4	/* Am ending encrypted */
#define	ENCRYPT_REQSTART	5	/* Request you start encrypting */
#define	ENCRYPT_REQEND		6	/* Request you send encrypting */
#define	ENCRYPT_ENC_KEYID	7
#define	ENCRYPT_DEC_KEYID	8
#define	ENCRYPT_CNT		9

#define	ENCTYPE_ANY		0
#define	ENCTYPE_DES_CFB64	1
#define	ENCTYPE_DES_OFB64	2
#define	ENCTYPE_CNT		3

#ifdef	ENCRYPT_NAMES
const char *encrypt_names[] = {
	"IS", "SUPPORT", "REPLY", "START", "END",
	"REQUEST-START", "REQUEST-END", "ENC-KEYID", "DEC-KEYID",
	0,
};
const char *enctype_names[] = {
	"ANY", "DES_CFB64",  "DES_OFB64",  0,
};
#else
extern char *encrypt_names[];
extern char *enctype_names[];
#endif

#define	ENCRYPT_NAME_OK(x)	((unsigned int)(x) < ENCRYPT_CNT)
#define	ENCRYPT_NAME(x)		encrypt_names[x]

#define	ENCTYPE_NAME_OK(x)	((unsigned int)(x) < ENCTYPE_CNT)
#define	ENCTYPE_NAME(x)		enctype_names[x]

/* normal */
static const char *cmds[] = {
	"IS", "SEND", "INFO",
};

/* 37: Authentication */
static const char *authcmd[] = {
	"IS", "SEND", "REPLY", "NAME",
};
static const char *authtype[] = {
	"NULL", "KERBEROS_V4", "KERBEROS_V5", "SPX", "MINK",
	"SRP", "RSA", "SSL", NULL, NULL,
	"LOKI", "SSA", "KEA_SJ", "KEA_SJ_INTEG", "DSS",
	"NTLM",
};

/* 38: Encryption */
static const char *enccmd[] = {
	"IS", "SUPPORT", "REPLY", "START", "END",
	"REQUEST-START", "REQUEST-END", "END_KEYID", "DEC_KEYID",
};
static const char *enctype[] = {
	"NULL", "DES_CFB64", "DES_OFB64", "DES3_CFB64", "DES3_OFB64",
	NULL, "CAST5_40_CFB64", "CAST5_40_OFB64", "CAST128_CFB64", "CAST128_OFB64",
};

#define STR_OR_ID(x, tab) \
	(((x) < sizeof(tab)/sizeof(tab[0]) && tab[(x)]) ? tab[(x)] : numstr(x))

static char *
numstr(int x)
{
	static char buf[20];

	snprintf(buf, sizeof(buf), "%#x", x);
	return buf;
}

/* sp points to IAC byte */
static int
telnet_parse(netdissect_options *ndo, const u_char *sp, u_int length, int print)
{
	int i, x;
	u_int c;
	const u_char *osp, *p;
#define FETCH(c, sp, length) \
	do { \
		if (length < 1) \
			goto pktend; \
		ND_TCHECK(*sp); \
		c = *sp++; \
		length--; \
	} while (0)

	osp = sp;

	FETCH(c, sp, length);
	if (c != IAC)
		goto pktend;
	FETCH(c, sp, length);
	if (c == IAC) {		/* <IAC><IAC>! */
		if (print)
			ND_PRINT((ndo, "IAC IAC"));
		goto done;
	}

	i = c - TELCMD_FIRST;
	if (i < 0 || i > IAC - TELCMD_FIRST)
		goto pktend;

	switch (c) {
	case DONT:
	case DO:
	case WONT:
	case WILL:
	case SB:
		/* DONT/DO/WONT/WILL x */
		FETCH(x, sp, length);
		if (x >= 0 && x < NTELOPTS) {
			if (print)
				ND_PRINT((ndo, "%s %s", telcmds[i], telopts[x]));
		} else {
			if (print)
				ND_PRINT((ndo, "%s %#x", telcmds[i], x));
		}
		if (c != SB)
			break;
		/* IAC SB .... IAC SE */
		p = sp;
		while (length > (u_int)(p + 1 - sp)) {
			ND_TCHECK2(*p, 2);
			if (p[0] == IAC && p[1] == SE)
				break;
			p++;
		}
		ND_TCHECK(*p);
		if (*p != IAC)
			goto pktend;

		switch (x) {
		case TELOPT_AUTHENTICATION:
			if (p <= sp)
				break;
			FETCH(c, sp, length);
			if (print)
				ND_PRINT((ndo, " %s", STR_OR_ID(c, authcmd)));
			if (p <= sp)
				break;
			FETCH(c, sp, length);
			if (print)
				ND_PRINT((ndo, " %s", STR_OR_ID(c, authtype)));
			break;
		case TELOPT_ENCRYPT:
			if (p <= sp)
				break;
			FETCH(c, sp, length);
			if (print)
				ND_PRINT((ndo, " %s", STR_OR_ID(c, enccmd)));
			if (p <= sp)
				break;
			FETCH(c, sp, length);
			if (print)
				ND_PRINT((ndo, " %s", STR_OR_ID(c, enctype)));
			break;
		default:
			if (p <= sp)
				break;
			FETCH(c, sp, length);
			if (print)
				ND_PRINT((ndo, " %s", STR_OR_ID(c, cmds)));
			break;
		}
		while (p > sp) {
			FETCH(x, sp, length);
			if (print)
				ND_PRINT((ndo, " %#x", x));
		}
		/* terminating IAC SE */
		if (print)
			ND_PRINT((ndo, " SE"));
		sp += 2;
		break;
	default:
		if (print)
			ND_PRINT((ndo, "%s", telcmds[i]));
		goto done;
	}

done:
	return sp - osp;

trunc:
	ND_PRINT((ndo, "%s", tstr));
pktend:
	return -1;
#undef FETCH
}

void
telnet_print(netdissect_options *ndo, const u_char *sp, u_int length)
{
	int first = 1;
	const u_char *osp;
	int l;

	osp = sp;

	ND_TCHECK(*sp);
	while (length > 0 && *sp == IAC) {
		/*
		 * Parse the Telnet command without printing it,
		 * to determine its length.
		 */
		l = telnet_parse(ndo, sp, length, 0);
		if (l < 0)
			break;

		/*
		 * now print it
		 */
		if (ndo->ndo_Xflag && 2 < ndo->ndo_vflag) {
			if (first)
				ND_PRINT((ndo, "\nTelnet:"));
			hex_print_with_offset(ndo, "\n", sp, l, sp - osp);
			if (l > 8)
				ND_PRINT((ndo, "\n\t\t\t\t"));
			else
				ND_PRINT((ndo, "%*s\t", (8 - l) * 3, ""));
		} else
			ND_PRINT((ndo, "%s", (first) ? " [telnet " : ", "));

		(void)telnet_parse(ndo, sp, length, 1);
		first = 0;

		sp += l;
		length -= l;
		ND_TCHECK(*sp);
	}
	if (!first) {
		if (ndo->ndo_Xflag && 2 < ndo->ndo_vflag)
			ND_PRINT((ndo, "\n"));
		else
			ND_PRINT((ndo, "]"));
	}
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}
