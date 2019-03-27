/*
 * We want a reentrant parser.
 */
%pure-parser

/*
 * We also want a reentrant scanner, so we have to pass the
 * handle for the reentrant scanner to the parser, and the
 * parser has to pass it to the lexical analyzer.
 *
 * We use void * rather than yyscan_t because, at least with some
 * versions of Flex and Bison, if you use yyscan_t in %parse-param and
 * %lex-param, you have to include scanner.h before grammar.h to get
 * yyscan_t declared, and you have to include grammar.h before scanner.h
 * to get YYSTYPE declared.  Using void * breaks the cycle; the Flex
 * documentation says yyscan_t is just a void *.
 */
%parse-param   {void *yyscanner}
%lex-param   {void *yyscanner}

/*
 * And we need to pass the compiler state to the scanner.
 */
%parse-param { compiler_state_t *cstate }

%{
/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>

#if __STDC__
struct mbuf;
struct rtentry;
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#endif /* _WIN32 */

#include <stdio.h>

#include "diag-control.h"

#include "pcap-int.h"

#include "gencode.h"
#include "grammar.h"
#include "scanner.h"

#ifdef HAVE_NET_PFVAR_H
#include <net/if.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>
#endif
#include "llc.h"
#include "ieee80211.h"
#include <pcap/namedb.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#ifdef YYBYACC
/*
 * Both Berkeley YACC and Bison define yydebug (under whatever name
 * it has) as a global, but Bison does so only if YYDEBUG is defined.
 * Berkeley YACC define it even if YYDEBUG isn't defined; declare it
 * here to suppress a warning.
 */
#if !defined(YYDEBUG)
extern int yydebug;
#endif

/*
 * In Berkeley YACC, yynerrs (under whatever name it has) is global,
 * even if it's building a reentrant parser.  In Bison, it's local
 * in reentrant parsers.
 *
 * Declare it to squelch a warning.
 */
extern int yynerrs;
#endif

#define QSET(q, p, d, a) (q).proto = (unsigned char)(p),\
			 (q).dir = (unsigned char)(d),\
			 (q).addr = (unsigned char)(a)

struct tok {
	int v;			/* value */
	const char *s;		/* string */
};

static const struct tok ieee80211_types[] = {
	{ IEEE80211_FC0_TYPE_DATA, "data" },
	{ IEEE80211_FC0_TYPE_MGT, "mgt" },
	{ IEEE80211_FC0_TYPE_MGT, "management" },
	{ IEEE80211_FC0_TYPE_CTL, "ctl" },
	{ IEEE80211_FC0_TYPE_CTL, "control" },
	{ 0, NULL }
};
static const struct tok ieee80211_mgt_subtypes[] = {
	{ IEEE80211_FC0_SUBTYPE_ASSOC_REQ, "assocreq" },
	{ IEEE80211_FC0_SUBTYPE_ASSOC_REQ, "assoc-req" },
	{ IEEE80211_FC0_SUBTYPE_ASSOC_RESP, "assocresp" },
	{ IEEE80211_FC0_SUBTYPE_ASSOC_RESP, "assoc-resp" },
	{ IEEE80211_FC0_SUBTYPE_REASSOC_REQ, "reassocreq" },
	{ IEEE80211_FC0_SUBTYPE_REASSOC_REQ, "reassoc-req" },
	{ IEEE80211_FC0_SUBTYPE_REASSOC_RESP, "reassocresp" },
	{ IEEE80211_FC0_SUBTYPE_REASSOC_RESP, "reassoc-resp" },
	{ IEEE80211_FC0_SUBTYPE_PROBE_REQ, "probereq" },
	{ IEEE80211_FC0_SUBTYPE_PROBE_REQ, "probe-req" },
	{ IEEE80211_FC0_SUBTYPE_PROBE_RESP, "proberesp" },
	{ IEEE80211_FC0_SUBTYPE_PROBE_RESP, "probe-resp" },
	{ IEEE80211_FC0_SUBTYPE_BEACON, "beacon" },
	{ IEEE80211_FC0_SUBTYPE_ATIM, "atim" },
	{ IEEE80211_FC0_SUBTYPE_DISASSOC, "disassoc" },
	{ IEEE80211_FC0_SUBTYPE_DISASSOC, "disassociation" },
	{ IEEE80211_FC0_SUBTYPE_AUTH, "auth" },
	{ IEEE80211_FC0_SUBTYPE_AUTH, "authentication" },
	{ IEEE80211_FC0_SUBTYPE_DEAUTH, "deauth" },
	{ IEEE80211_FC0_SUBTYPE_DEAUTH, "deauthentication" },
	{ 0, NULL }
};
static const struct tok ieee80211_ctl_subtypes[] = {
	{ IEEE80211_FC0_SUBTYPE_PS_POLL, "ps-poll" },
	{ IEEE80211_FC0_SUBTYPE_RTS, "rts" },
	{ IEEE80211_FC0_SUBTYPE_CTS, "cts" },
	{ IEEE80211_FC0_SUBTYPE_ACK, "ack" },
	{ IEEE80211_FC0_SUBTYPE_CF_END, "cf-end" },
	{ IEEE80211_FC0_SUBTYPE_CF_END_ACK, "cf-end-ack" },
	{ 0, NULL }
};
static const struct tok ieee80211_data_subtypes[] = {
	{ IEEE80211_FC0_SUBTYPE_DATA, "data" },
	{ IEEE80211_FC0_SUBTYPE_CF_ACK, "data-cf-ack" },
	{ IEEE80211_FC0_SUBTYPE_CF_POLL, "data-cf-poll" },
	{ IEEE80211_FC0_SUBTYPE_CF_ACPL, "data-cf-ack-poll" },
	{ IEEE80211_FC0_SUBTYPE_NODATA, "null" },
	{ IEEE80211_FC0_SUBTYPE_NODATA_CF_ACK, "cf-ack" },
	{ IEEE80211_FC0_SUBTYPE_NODATA_CF_POLL, "cf-poll"  },
	{ IEEE80211_FC0_SUBTYPE_NODATA_CF_ACPL, "cf-ack-poll" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_DATA, "qos-data" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_CF_ACK, "qos-data-cf-ack" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_CF_POLL, "qos-data-cf-poll" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_CF_ACPL, "qos-data-cf-ack-poll" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_NODATA, "qos" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_NODATA_CF_POLL, "qos-cf-poll" },
	{ IEEE80211_FC0_SUBTYPE_QOS|IEEE80211_FC0_SUBTYPE_NODATA_CF_ACPL, "qos-cf-ack-poll" },
	{ 0, NULL }
};
static const struct tok llc_s_subtypes[] = {
	{ LLC_RR, "rr" },
	{ LLC_RNR, "rnr" },
	{ LLC_REJ, "rej" },
	{ 0, NULL }
};
static const struct tok llc_u_subtypes[] = {
	{ LLC_UI, "ui" },
	{ LLC_UA, "ua" },
	{ LLC_DISC, "disc" },
	{ LLC_DM, "dm" },
	{ LLC_SABME, "sabme" },
	{ LLC_TEST, "test" },
	{ LLC_XID, "xid" },
	{ LLC_FRMR, "frmr" },
	{ 0, NULL }
};
struct type2tok {
	int type;
	const struct tok *tok;
};
static const struct type2tok ieee80211_type_subtypes[] = {
	{ IEEE80211_FC0_TYPE_MGT, ieee80211_mgt_subtypes },
	{ IEEE80211_FC0_TYPE_CTL, ieee80211_ctl_subtypes },
	{ IEEE80211_FC0_TYPE_DATA, ieee80211_data_subtypes },
	{ 0, NULL }
};

static int
str2tok(const char *str, const struct tok *toks)
{
	int i;

	for (i = 0; toks[i].s != NULL; i++) {
		if (pcap_strcasecmp(toks[i].s, str) == 0)
			return (toks[i].v);
	}
	return (-1);
}

static struct qual qerr = { Q_UNDEF, Q_UNDEF, Q_UNDEF, Q_UNDEF };

static PCAP_NORETURN_DEF void
yyerror(void *yyscanner _U_, compiler_state_t *cstate, const char *msg)
{
	bpf_syntax_error(cstate, msg);
	/* NOTREACHED */
}

#ifdef HAVE_NET_PFVAR_H
static int
pfreason_to_num(compiler_state_t *cstate, const char *reason)
{
	const char *reasons[] = PFRES_NAMES;
	int i;

	for (i = 0; reasons[i]; i++) {
		if (pcap_strcasecmp(reason, reasons[i]) == 0)
			return (i);
	}
	bpf_error(cstate, "unknown PF reason");
	/*NOTREACHED*/
}

static int
pfaction_to_num(compiler_state_t *cstate, const char *action)
{
	if (pcap_strcasecmp(action, "pass") == 0 ||
	    pcap_strcasecmp(action, "accept") == 0)
		return (PF_PASS);
	else if (pcap_strcasecmp(action, "drop") == 0 ||
		pcap_strcasecmp(action, "block") == 0)
		return (PF_DROP);
#if HAVE_PF_NAT_THROUGH_PF_NORDR
	else if (pcap_strcasecmp(action, "rdr") == 0)
		return (PF_RDR);
	else if (pcap_strcasecmp(action, "nat") == 0)
		return (PF_NAT);
	else if (pcap_strcasecmp(action, "binat") == 0)
		return (PF_BINAT);
	else if (pcap_strcasecmp(action, "nordr") == 0)
		return (PF_NORDR);
#endif
	else {
		bpf_error(cstate, "unknown PF action");
		/*NOTREACHED*/
	}
}
#else /* !HAVE_NET_PFVAR_H */
static PCAP_NORETURN_DEF int
pfreason_to_num(compiler_state_t *cstate, const char *reason _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/*NOTREACHED*/
}

static PCAP_NORETURN_DEF int
pfaction_to_num(compiler_state_t *cstate, const char *action _U_)
{
	bpf_error(cstate, "libpcap was compiled on a machine without pf support");
	/*NOTREACHED*/
}
#endif /* HAVE_NET_PFVAR_H */

DIAG_OFF_BISON_BYACC
%}

%union {
	int i;
	bpf_u_int32 h;
	u_char *e;
	char *s;
	struct stmt *stmt;
	struct arth *a;
	struct {
		struct qual q;
		int atmfieldtype;
		int mtp3fieldtype;
		struct block *b;
	} blk;
	struct block *rblk;
}

%type	<blk>	expr id nid pid term rterm qid
%type	<blk>	head
%type	<i>	pqual dqual aqual ndaqual
%type	<a>	arth narth
%type	<i>	byteop pname pnum relop irelop
%type	<blk>	and or paren not null prog
%type	<rblk>	other pfvar p80211 pllc
%type	<i>	atmtype atmmultitype
%type	<blk>	atmfield
%type	<blk>	atmfieldvalue atmvalue atmlistvalue
%type	<i>	mtp2type
%type	<blk>	mtp3field
%type	<blk>	mtp3fieldvalue mtp3value mtp3listvalue


%token  DST SRC HOST GATEWAY
%token  NET NETMASK PORT PORTRANGE LESS GREATER PROTO PROTOCHAIN CBYTE
%token  ARP RARP IP SCTP TCP UDP ICMP IGMP IGRP PIM VRRP CARP
%token  ATALK AARP DECNET LAT SCA MOPRC MOPDL
%token  TK_BROADCAST TK_MULTICAST
%token  NUM INBOUND OUTBOUND
%token  PF_IFNAME PF_RSET PF_RNR PF_SRNR PF_REASON PF_ACTION
%token	TYPE SUBTYPE DIR ADDR1 ADDR2 ADDR3 ADDR4 RA TA
%token  LINK
%token	GEQ LEQ NEQ
%token	ID EID HID HID6 AID
%token	LSH RSH
%token  LEN
%token  IPV6 ICMPV6 AH ESP
%token	VLAN MPLS
%token	PPPOED PPPOES GENEVE
%token  ISO ESIS CLNP ISIS L1 L2 IIH LSP SNP CSNP PSNP
%token  STP
%token  IPX
%token  NETBEUI
%token	LANE LLC METAC BCC SC ILMIC OAMF4EC OAMF4SC
%token	OAM OAMF4 CONNECTMSG METACONNECT
%token	VPI VCI
%token	RADIO
%token	FISU LSSU MSU HFISU HLSSU HMSU
%token	SIO OPC DPC SLS HSIO HOPC HDPC HSLS


%type	<s> ID
%type	<e> EID
%type	<e> AID
%type	<s> HID HID6
%type	<i> NUM action reason type subtype type_subtype dir

%left OR AND
%nonassoc  '!'
%left '|'
%left '&'
%left LSH RSH
%left '+' '-'
%left '*' '/'
%nonassoc UMINUS
%%
prog:	  null expr
{
	finish_parse(cstate, $2.b);
}
	| null
	;
null:	  /* null */		{ $$.q = qerr; }
	;
expr:	  term
	| expr and term		{ gen_and($1.b, $3.b); $$ = $3; }
	| expr and id		{ gen_and($1.b, $3.b); $$ = $3; }
	| expr or term		{ gen_or($1.b, $3.b); $$ = $3; }
	| expr or id		{ gen_or($1.b, $3.b); $$ = $3; }
	;
and:	  AND			{ $$ = $<blk>0; }
	;
or:	  OR			{ $$ = $<blk>0; }
	;
id:	  nid
	| pnum			{ $$.b = gen_ncode(cstate, NULL, (bpf_u_int32)$1,
						   $$.q = $<blk>0.q); }
	| paren pid ')'		{ $$ = $2; }
	;
nid:	  ID			{ $$.b = gen_scode(cstate, $1, $$.q = $<blk>0.q); }
	| HID '/' NUM		{ $$.b = gen_mcode(cstate, $1, NULL, $3,
				    $$.q = $<blk>0.q); }
	| HID NETMASK HID	{ $$.b = gen_mcode(cstate, $1, $3, 0,
				    $$.q = $<blk>0.q); }
	| HID			{
				  /* Decide how to parse HID based on proto */
				  $$.q = $<blk>0.q;
				  if ($$.q.addr == Q_PORT)
				  	bpf_error(cstate, "'port' modifier applied to ip host");
				  else if ($$.q.addr == Q_PORTRANGE)
				  	bpf_error(cstate, "'portrange' modifier applied to ip host");
				  else if ($$.q.addr == Q_PROTO)
				  	bpf_error(cstate, "'proto' modifier applied to ip host");
				  else if ($$.q.addr == Q_PROTOCHAIN)
				  	bpf_error(cstate, "'protochain' modifier applied to ip host");
				  $$.b = gen_ncode(cstate, $1, 0, $$.q);
				}
	| HID6 '/' NUM		{
#ifdef INET6
				  $$.b = gen_mcode6(cstate, $1, NULL, $3,
				    $$.q = $<blk>0.q);
#else
				  bpf_error(cstate, "'ip6addr/prefixlen' not supported "
					"in this configuration");
#endif /*INET6*/
				}
	| HID6			{
#ifdef INET6
				  $$.b = gen_mcode6(cstate, $1, 0, 128,
				    $$.q = $<blk>0.q);
#else
				  bpf_error(cstate, "'ip6addr' not supported "
					"in this configuration");
#endif /*INET6*/
				}
	| EID			{
				  $$.b = gen_ecode(cstate, $1, $$.q = $<blk>0.q);
				  /*
				   * $1 was allocated by "pcap_ether_aton()",
				   * so we must free it now that we're done
				   * with it.
				   */
				  free($1);
				}
	| AID			{
				  $$.b = gen_acode(cstate, $1, $$.q = $<blk>0.q);
				  /*
				   * $1 was allocated by "pcap_ether_aton()",
				   * so we must free it now that we're done
				   * with it.
				   */
				  free($1);
				}
	| not id		{ gen_not($2.b); $$ = $2; }
	;
not:	  '!'			{ $$ = $<blk>0; }
	;
paren:	  '('			{ $$ = $<blk>0; }
	;
pid:	  nid
	| qid and id		{ gen_and($1.b, $3.b); $$ = $3; }
	| qid or id		{ gen_or($1.b, $3.b); $$ = $3; }
	;
qid:	  pnum			{ $$.b = gen_ncode(cstate, NULL, (bpf_u_int32)$1,
						   $$.q = $<blk>0.q); }
	| pid
	;
term:	  rterm
	| not term		{ gen_not($2.b); $$ = $2; }
	;
head:	  pqual dqual aqual	{ QSET($$.q, $1, $2, $3); }
	| pqual dqual		{ QSET($$.q, $1, $2, Q_DEFAULT); }
	| pqual aqual		{ QSET($$.q, $1, Q_DEFAULT, $2); }
	| pqual PROTO		{ QSET($$.q, $1, Q_DEFAULT, Q_PROTO); }
	| pqual PROTOCHAIN	{ QSET($$.q, $1, Q_DEFAULT, Q_PROTOCHAIN); }
	| pqual ndaqual		{ QSET($$.q, $1, Q_DEFAULT, $2); }
	;
rterm:	  head id		{ $$ = $2; }
	| paren expr ')'	{ $$.b = $2.b; $$.q = $1.q; }
	| pname			{ $$.b = gen_proto_abbrev(cstate, $1); $$.q = qerr; }
	| arth relop arth	{ $$.b = gen_relation(cstate, $2, $1, $3, 0);
				  $$.q = qerr; }
	| arth irelop arth	{ $$.b = gen_relation(cstate, $2, $1, $3, 1);
				  $$.q = qerr; }
	| other			{ $$.b = $1; $$.q = qerr; }
	| atmtype		{ $$.b = gen_atmtype_abbrev(cstate, $1); $$.q = qerr; }
	| atmmultitype		{ $$.b = gen_atmmulti_abbrev(cstate, $1); $$.q = qerr; }
	| atmfield atmvalue	{ $$.b = $2.b; $$.q = qerr; }
	| mtp2type		{ $$.b = gen_mtp2type_abbrev(cstate, $1); $$.q = qerr; }
	| mtp3field mtp3value	{ $$.b = $2.b; $$.q = qerr; }
	;
/* protocol level qualifiers */
pqual:	  pname
	|			{ $$ = Q_DEFAULT; }
	;
/* 'direction' qualifiers */
dqual:	  SRC			{ $$ = Q_SRC; }
	| DST			{ $$ = Q_DST; }
	| SRC OR DST		{ $$ = Q_OR; }
	| DST OR SRC		{ $$ = Q_OR; }
	| SRC AND DST		{ $$ = Q_AND; }
	| DST AND SRC		{ $$ = Q_AND; }
	| ADDR1			{ $$ = Q_ADDR1; }
	| ADDR2			{ $$ = Q_ADDR2; }
	| ADDR3			{ $$ = Q_ADDR3; }
	| ADDR4			{ $$ = Q_ADDR4; }
	| RA			{ $$ = Q_RA; }
	| TA			{ $$ = Q_TA; }
	;
/* address type qualifiers */
aqual:	  HOST			{ $$ = Q_HOST; }
	| NET			{ $$ = Q_NET; }
	| PORT			{ $$ = Q_PORT; }
	| PORTRANGE		{ $$ = Q_PORTRANGE; }
	;
/* non-directional address type qualifiers */
ndaqual:  GATEWAY		{ $$ = Q_GATEWAY; }
	;
pname:	  LINK			{ $$ = Q_LINK; }
	| IP			{ $$ = Q_IP; }
	| ARP			{ $$ = Q_ARP; }
	| RARP			{ $$ = Q_RARP; }
	| SCTP			{ $$ = Q_SCTP; }
	| TCP			{ $$ = Q_TCP; }
	| UDP			{ $$ = Q_UDP; }
	| ICMP			{ $$ = Q_ICMP; }
	| IGMP			{ $$ = Q_IGMP; }
	| IGRP			{ $$ = Q_IGRP; }
	| PIM			{ $$ = Q_PIM; }
	| VRRP			{ $$ = Q_VRRP; }
	| CARP 			{ $$ = Q_CARP; }
	| ATALK			{ $$ = Q_ATALK; }
	| AARP			{ $$ = Q_AARP; }
	| DECNET		{ $$ = Q_DECNET; }
	| LAT			{ $$ = Q_LAT; }
	| SCA			{ $$ = Q_SCA; }
	| MOPDL			{ $$ = Q_MOPDL; }
	| MOPRC			{ $$ = Q_MOPRC; }
	| IPV6			{ $$ = Q_IPV6; }
	| ICMPV6		{ $$ = Q_ICMPV6; }
	| AH			{ $$ = Q_AH; }
	| ESP			{ $$ = Q_ESP; }
	| ISO			{ $$ = Q_ISO; }
	| ESIS			{ $$ = Q_ESIS; }
	| ISIS			{ $$ = Q_ISIS; }
	| L1			{ $$ = Q_ISIS_L1; }
	| L2			{ $$ = Q_ISIS_L2; }
	| IIH			{ $$ = Q_ISIS_IIH; }
	| LSP			{ $$ = Q_ISIS_LSP; }
	| SNP			{ $$ = Q_ISIS_SNP; }
	| PSNP			{ $$ = Q_ISIS_PSNP; }
	| CSNP			{ $$ = Q_ISIS_CSNP; }
	| CLNP			{ $$ = Q_CLNP; }
	| STP			{ $$ = Q_STP; }
	| IPX			{ $$ = Q_IPX; }
	| NETBEUI		{ $$ = Q_NETBEUI; }
	| RADIO			{ $$ = Q_RADIO; }
	;
other:	  pqual TK_BROADCAST	{ $$ = gen_broadcast(cstate, $1); }
	| pqual TK_MULTICAST	{ $$ = gen_multicast(cstate, $1); }
	| LESS NUM		{ $$ = gen_less(cstate, $2); }
	| GREATER NUM		{ $$ = gen_greater(cstate, $2); }
	| CBYTE NUM byteop NUM	{ $$ = gen_byteop(cstate, $3, $2, $4); }
	| INBOUND		{ $$ = gen_inbound(cstate, 0); }
	| OUTBOUND		{ $$ = gen_inbound(cstate, 1); }
	| VLAN pnum		{ $$ = gen_vlan(cstate, $2); }
	| VLAN			{ $$ = gen_vlan(cstate, -1); }
	| MPLS pnum		{ $$ = gen_mpls(cstate, $2); }
	| MPLS			{ $$ = gen_mpls(cstate, -1); }
	| PPPOED		{ $$ = gen_pppoed(cstate); }
	| PPPOES pnum		{ $$ = gen_pppoes(cstate, $2); }
	| PPPOES		{ $$ = gen_pppoes(cstate, -1); }
	| GENEVE pnum		{ $$ = gen_geneve(cstate, $2); }
	| GENEVE		{ $$ = gen_geneve(cstate, -1); }
	| pfvar			{ $$ = $1; }
	| pqual p80211		{ $$ = $2; }
	| pllc			{ $$ = $1; }
	;

pfvar:	  PF_IFNAME ID		{ $$ = gen_pf_ifname(cstate, $2); }
	| PF_RSET ID		{ $$ = gen_pf_ruleset(cstate, $2); }
	| PF_RNR NUM		{ $$ = gen_pf_rnr(cstate, $2); }
	| PF_SRNR NUM		{ $$ = gen_pf_srnr(cstate, $2); }
	| PF_REASON reason	{ $$ = gen_pf_reason(cstate, $2); }
	| PF_ACTION action	{ $$ = gen_pf_action(cstate, $2); }
	;

p80211:   TYPE type SUBTYPE subtype
				{ $$ = gen_p80211_type(cstate, $2 | $4,
					IEEE80211_FC0_TYPE_MASK |
					IEEE80211_FC0_SUBTYPE_MASK);
				}
	| TYPE type		{ $$ = gen_p80211_type(cstate, $2,
					IEEE80211_FC0_TYPE_MASK);
				}
	| SUBTYPE type_subtype	{ $$ = gen_p80211_type(cstate, $2,
					IEEE80211_FC0_TYPE_MASK |
					IEEE80211_FC0_SUBTYPE_MASK);
				}
	| DIR dir		{ $$ = gen_p80211_fcdir(cstate, $2); }
	;

type:	  NUM
	| ID			{ $$ = str2tok($1, ieee80211_types);
				  if ($$ == -1)
				  	bpf_error(cstate, "unknown 802.11 type name");
				}
	;

subtype:  NUM
	| ID			{ const struct tok *types = NULL;
				  int i;
				  for (i = 0;; i++) {
				  	if (ieee80211_type_subtypes[i].tok == NULL) {
				  		/* Ran out of types */
						bpf_error(cstate, "unknown 802.11 type");
						break;
					}
					if ($<i>-1 == ieee80211_type_subtypes[i].type) {
						types = ieee80211_type_subtypes[i].tok;
						break;
					}
				  }

				  $$ = str2tok($1, types);
				  if ($$ == -1)
					bpf_error(cstate, "unknown 802.11 subtype name");
				}
	;

type_subtype:	ID		{ int i;
				  for (i = 0;; i++) {
				  	if (ieee80211_type_subtypes[i].tok == NULL) {
				  		/* Ran out of types */
						bpf_error(cstate, "unknown 802.11 type name");
						break;
					}
					$$ = str2tok($1, ieee80211_type_subtypes[i].tok);
					if ($$ != -1) {
						$$ |= ieee80211_type_subtypes[i].type;
						break;
					}
				  }
				}
		;

pllc:	LLC			{ $$ = gen_llc(cstate); }
	| LLC ID		{ if (pcap_strcasecmp($2, "i") == 0)
					$$ = gen_llc_i(cstate);
				  else if (pcap_strcasecmp($2, "s") == 0)
					$$ = gen_llc_s(cstate);
				  else if (pcap_strcasecmp($2, "u") == 0)
					$$ = gen_llc_u(cstate);
				  else {
					int subtype;

					subtype = str2tok($2, llc_s_subtypes);
					if (subtype != -1)
						$$ = gen_llc_s_subtype(cstate, subtype);
					else {
						subtype = str2tok($2, llc_u_subtypes);
						if (subtype == -1)
					  		bpf_error(cstate, "unknown LLC type name \"%s\"", $2);
						$$ = gen_llc_u_subtype(cstate, subtype);
					}
				  }
				}
				/* sigh, "rnr" is already a keyword for PF */
	| LLC PF_RNR		{ $$ = gen_llc_s_subtype(cstate, LLC_RNR); }
	;

dir:	  NUM
	| ID			{ if (pcap_strcasecmp($1, "nods") == 0)
					$$ = IEEE80211_FC1_DIR_NODS;
				  else if (pcap_strcasecmp($1, "tods") == 0)
					$$ = IEEE80211_FC1_DIR_TODS;
				  else if (pcap_strcasecmp($1, "fromds") == 0)
					$$ = IEEE80211_FC1_DIR_FROMDS;
				  else if (pcap_strcasecmp($1, "dstods") == 0)
					$$ = IEEE80211_FC1_DIR_DSTODS;
				  else
					bpf_error(cstate, "unknown 802.11 direction");
				}
	;

reason:	  NUM			{ $$ = $1; }
	| ID			{ $$ = pfreason_to_num(cstate, $1); }
	;

action:	  ID			{ $$ = pfaction_to_num(cstate, $1); }
	;

relop:	  '>'			{ $$ = BPF_JGT; }
	| GEQ			{ $$ = BPF_JGE; }
	| '='			{ $$ = BPF_JEQ; }
	;
irelop:	  LEQ			{ $$ = BPF_JGT; }
	| '<'			{ $$ = BPF_JGE; }
	| NEQ			{ $$ = BPF_JEQ; }
	;
arth:	  pnum			{ $$ = gen_loadi(cstate, $1); }
	| narth
	;
narth:	  pname '[' arth ']'		{ $$ = gen_load(cstate, $1, $3, 1); }
	| pname '[' arth ':' NUM ']'	{ $$ = gen_load(cstate, $1, $3, $5); }
	| arth '+' arth			{ $$ = gen_arth(cstate, BPF_ADD, $1, $3); }
	| arth '-' arth			{ $$ = gen_arth(cstate, BPF_SUB, $1, $3); }
	| arth '*' arth			{ $$ = gen_arth(cstate, BPF_MUL, $1, $3); }
	| arth '/' arth			{ $$ = gen_arth(cstate, BPF_DIV, $1, $3); }
	| arth '%' arth			{ $$ = gen_arth(cstate, BPF_MOD, $1, $3); }
	| arth '&' arth			{ $$ = gen_arth(cstate, BPF_AND, $1, $3); }
	| arth '|' arth			{ $$ = gen_arth(cstate, BPF_OR, $1, $3); }
	| arth '^' arth			{ $$ = gen_arth(cstate, BPF_XOR, $1, $3); }
	| arth LSH arth			{ $$ = gen_arth(cstate, BPF_LSH, $1, $3); }
	| arth RSH arth			{ $$ = gen_arth(cstate, BPF_RSH, $1, $3); }
	| '-' arth %prec UMINUS		{ $$ = gen_neg(cstate, $2); }
	| paren narth ')'		{ $$ = $2; }
	| LEN				{ $$ = gen_loadlen(cstate); }
	;
byteop:	  '&'			{ $$ = '&'; }
	| '|'			{ $$ = '|'; }
	| '<'			{ $$ = '<'; }
	| '>'			{ $$ = '>'; }
	| '='			{ $$ = '='; }
	;
pnum:	  NUM
	| paren pnum ')'	{ $$ = $2; }
	;
atmtype: LANE			{ $$ = A_LANE; }
	| METAC			{ $$ = A_METAC;	}
	| BCC			{ $$ = A_BCC; }
	| OAMF4EC		{ $$ = A_OAMF4EC; }
	| OAMF4SC		{ $$ = A_OAMF4SC; }
	| SC			{ $$ = A_SC; }
	| ILMIC			{ $$ = A_ILMIC; }
	;
atmmultitype: OAM		{ $$ = A_OAM; }
	| OAMF4			{ $$ = A_OAMF4; }
	| CONNECTMSG		{ $$ = A_CONNECTMSG; }
	| METACONNECT		{ $$ = A_METACONNECT; }
	;
	/* ATM field types quantifier */
atmfield: VPI			{ $$.atmfieldtype = A_VPI; }
	| VCI			{ $$.atmfieldtype = A_VCI; }
	;
atmvalue: atmfieldvalue
	| relop NUM		{ $$.b = gen_atmfield_code(cstate, $<blk>0.atmfieldtype, (bpf_int32)$2, (bpf_u_int32)$1, 0); }
	| irelop NUM		{ $$.b = gen_atmfield_code(cstate, $<blk>0.atmfieldtype, (bpf_int32)$2, (bpf_u_int32)$1, 1); }
	| paren atmlistvalue ')' { $$.b = $2.b; $$.q = qerr; }
	;
atmfieldvalue: NUM {
	$$.atmfieldtype = $<blk>0.atmfieldtype;
	if ($$.atmfieldtype == A_VPI ||
	    $$.atmfieldtype == A_VCI)
		$$.b = gen_atmfield_code(cstate, $$.atmfieldtype, (bpf_int32) $1, BPF_JEQ, 0);
	}
	;
atmlistvalue: atmfieldvalue
	| atmlistvalue or atmfieldvalue { gen_or($1.b, $3.b); $$ = $3; }
	;
	/* MTP2 types quantifier */
mtp2type: FISU			{ $$ = M_FISU; }
	| LSSU			{ $$ = M_LSSU; }
	| MSU			{ $$ = M_MSU; }
	| HFISU			{ $$ = MH_FISU; }
	| HLSSU			{ $$ = MH_LSSU; }
	| HMSU			{ $$ = MH_MSU; }
	;
	/* MTP3 field types quantifier */
mtp3field: SIO			{ $$.mtp3fieldtype = M_SIO; }
	| OPC			{ $$.mtp3fieldtype = M_OPC; }
	| DPC			{ $$.mtp3fieldtype = M_DPC; }
	| SLS                   { $$.mtp3fieldtype = M_SLS; }
	| HSIO			{ $$.mtp3fieldtype = MH_SIO; }
	| HOPC			{ $$.mtp3fieldtype = MH_OPC; }
	| HDPC			{ $$.mtp3fieldtype = MH_DPC; }
	| HSLS                  { $$.mtp3fieldtype = MH_SLS; }
	;
mtp3value: mtp3fieldvalue
	| relop NUM		{ $$.b = gen_mtp3field_code(cstate, $<blk>0.mtp3fieldtype, (u_int)$2, (u_int)$1, 0); }
	| irelop NUM		{ $$.b = gen_mtp3field_code(cstate, $<blk>0.mtp3fieldtype, (u_int)$2, (u_int)$1, 1); }
	| paren mtp3listvalue ')' { $$.b = $2.b; $$.q = qerr; }
	;
mtp3fieldvalue: NUM {
	$$.mtp3fieldtype = $<blk>0.mtp3fieldtype;
	if ($$.mtp3fieldtype == M_SIO ||
	    $$.mtp3fieldtype == M_OPC ||
	    $$.mtp3fieldtype == M_DPC ||
	    $$.mtp3fieldtype == M_SLS ||
	    $$.mtp3fieldtype == MH_SIO ||
	    $$.mtp3fieldtype == MH_OPC ||
	    $$.mtp3fieldtype == MH_DPC ||
	    $$.mtp3fieldtype == MH_SLS)
		$$.b = gen_mtp3field_code(cstate, $$.mtp3fieldtype, (u_int) $1, BPF_JEQ, 0);
	}
	;
mtp3listvalue: mtp3fieldvalue
	| mtp3listvalue or mtp3fieldvalue { gen_or($1.b, $3.b); $$ = $3; }
	;
%%
