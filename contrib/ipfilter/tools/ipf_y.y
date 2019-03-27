/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
%{
#include "ipf.h"
#include <sys/ioctl.h>
#include <syslog.h>
#include <err.h>
#ifdef IPFILTER_BPF
# include <pcap.h>
#endif
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#include "netinet/ipl.h"
#include "ipf_l.h"

#define	YYDEBUG	1
#define	DOALL(x)	for (fr = frc; fr != NULL; fr = fr->fr_next) { x }
#define	DOREM(x)	for (; fr != NULL; fr = fr->fr_next) { x }

extern	void	yyerror __P((char *));
extern	int	yyparse __P((void));
extern	int	yylex __P((void));
extern	int	yydebug;
extern	FILE	*yyin;
extern	int	yylineNum;

static	int	addname __P((frentry_t **, char *));
static	frentry_t *addrule __P((void));
static frentry_t *allocfr __P((void));
static	void	build_dstaddr_af __P((frentry_t *, void *));
static	void	build_srcaddr_af __P((frentry_t *, void *));
static	void	dobpf __P((int, char *));
static	void	doipfexpr __P((char *));
static	void	do_tuneint __P((char *, int));
static	void	do_tunestr __P((char *, char *));
static	void	fillgroup __P((frentry_t *));
static	int	lookuphost __P((char *, i6addr_t *));
static	u_int	makehash __P((struct alist_s *));
static	int	makepool __P((struct alist_s *));
static	struct	alist_s	*newalist __P((struct alist_s *));
static	void	newrule __P((void));
static	void	resetaddr __P((void));
static	void	setgroup __P((frentry_t **, char *));
static	void	setgrhead __P((frentry_t **, char *));
static	void	seticmphead __P((frentry_t **, char *));
static	void	setifname __P((frentry_t **, int, char *));
static	void	setipftype __P((void));
static	void	setsyslog __P((void));
static	void	unsetsyslog __P((void));

frentry_t	*fr = NULL, *frc = NULL, *frtop = NULL, *frold = NULL;

static	int		ifpflag = 0;
static	int		nowith = 0;
static	int		dynamic = -1;
static	int		pooled = 0;
static	int		hashed = 0;
static	int		nrules = 0;
static	int		newlist = 0;
static	int		added = 0;
static	int		ipffd = -1;
static	int		*yycont = NULL;
static	ioctlfunc_t	ipfioctls[IPL_LOGSIZE];
static	addfunc_t	ipfaddfunc = NULL;

%}
%union	{
	char	*str;
	u_32_t	num;
	frentry_t	fr;
	frtuc_t	*frt;
	struct	alist_s	*alist;
	u_short	port;
	struct	in_addr	ip4;
	struct	{
		u_short	p1;
		u_short	p2;
		int	pc;
	} pc;
	struct ipp_s {
		int		type;
		int		ifpos;
		int		f;
		int		v;
		int		lif;
		union	i6addr	a;
		union	i6addr	m;
		char		*name;
	} ipp;
	struct	{
		i6addr_t	adr;
		int		f;
	} adr;
	i6addr_t	ip6;
	struct	{
		char	*if1;
		char	*if2;
	} ifs;
	char	gname[FR_GROUPLEN];
};

%type	<port>	portnum
%type	<num>	facility priority icmpcode seclevel secname icmptype
%type	<num>	opt compare range opttype flagset optlist ipv6hdrlist ipv6hdr
%type	<num>	portc porteq ipmask maskopts
%type	<ip4>	ipv4 ipv4_16 ipv4_24
%type	<adr>	hostname
%type	<ipp>	addr ipaddr
%type	<str>	servicename name interfacename groupname
%type	<pc>	portrange portcomp
%type	<alist>	addrlist poollist
%type	<ifs>	onname

%token	<num>	YY_NUMBER YY_HEX
%token	<str>	YY_STR
%token		YY_COMMENT
%token		YY_CMP_EQ YY_CMP_NE YY_CMP_LE YY_CMP_GE YY_CMP_LT YY_CMP_GT
%token		YY_RANGE_OUT YY_RANGE_IN
%token	<ip6>	YY_IPV6

%token	IPFY_SET
%token	IPFY_PASS IPFY_BLOCK IPFY_COUNT IPFY_CALL IPFY_NOMATCH
%token	IPFY_RETICMP IPFY_RETRST IPFY_RETICMPASDST
%token	IPFY_IN IPFY_OUT
%token	IPFY_QUICK IPFY_ON IPFY_OUTVIA IPFY_INVIA
%token	IPFY_DUPTO IPFY_TO IPFY_FROUTE IPFY_REPLY_TO IPFY_ROUTETO
%token	IPFY_TOS IPFY_TTL IPFY_PROTO IPFY_INET IPFY_INET6
%token	IPFY_HEAD IPFY_GROUP
%token	IPFY_AUTH IPFY_PREAUTH
%token	IPFY_LOG IPFY_BODY IPFY_FIRST IPFY_LEVEL IPFY_ORBLOCK IPFY_L5AS
%token	IPFY_LOGTAG IPFY_MATCHTAG IPFY_SETTAG IPFY_SKIP IPFY_DECAPS
%token	IPFY_FROM IPFY_ALL IPFY_ANY IPFY_BPFV4 IPFY_BPFV6 IPFY_POOL IPFY_HASH
%token	IPFY_IPFEXPR IPFY_PPS IPFY_FAMILY IPFY_DSTLIST
%token	IPFY_ESP IPFY_AH
%token	IPFY_WITH IPFY_AND IPFY_NOT IPFY_NO IPFY_OPT
%token	IPFY_TCPUDP IPFY_TCP IPFY_UDP
%token	IPFY_FLAGS IPFY_MULTICAST
%token	IPFY_MASK IPFY_BROADCAST IPFY_NETWORK IPFY_NETMASKED IPFY_PEER
%token	IPFY_RPC IPFY_PORT
%token	IPFY_NOW IPFY_COMMENT IPFY_RULETTL
%token	IPFY_ICMP IPFY_ICMPTYPE IPFY_ICMPCODE
%token	IPFY_IPOPTS IPFY_SHORT IPFY_NAT IPFY_BADSRC IPFY_LOWTTL IPFY_FRAG
%token	IPFY_MBCAST IPFY_BAD IPFY_BADNAT IPFY_OOW IPFY_NEWISN IPFY_NOICMPERR
%token	IPFY_KEEP IPFY_STATE IPFY_FRAGS IPFY_LIMIT IPFY_STRICT IPFY_AGE
%token	IPFY_SYNC IPFY_FRAGBODY IPFY_ICMPHEAD IPFY_NOLOG IPFY_LOOSE
%token	IPFY_MAX_SRCS IPFY_MAX_PER_SRC
%token	IPFY_IPOPT_NOP IPFY_IPOPT_RR IPFY_IPOPT_ZSU IPFY_IPOPT_MTUP
%token	IPFY_IPOPT_MTUR IPFY_IPOPT_ENCODE IPFY_IPOPT_TS IPFY_IPOPT_TR
%token	IPFY_IPOPT_SEC IPFY_IPOPT_LSRR IPFY_IPOPT_ESEC IPFY_IPOPT_CIPSO
%token	IPFY_IPOPT_SATID IPFY_IPOPT_SSRR IPFY_IPOPT_ADDEXT IPFY_IPOPT_VISA
%token	IPFY_IPOPT_IMITD IPFY_IPOPT_EIP IPFY_IPOPT_FINN IPFY_IPOPT_DPS
%token	IPFY_IPOPT_SDB IPFY_IPOPT_NSAPA IPFY_IPOPT_RTRALRT IPFY_IPOPT_UMP
%token	IPFY_SECCLASS IPFY_SEC_UNC IPFY_SEC_CONF IPFY_SEC_RSV1 IPFY_SEC_RSV2
%token	IPFY_SEC_RSV4 IPFY_SEC_SEC IPFY_SEC_TS IPFY_SEC_RSV3 IPFY_DOI

%token	IPFY_V6HDRS IPFY_IPV6OPT IPFY_IPV6OPT_DSTOPTS IPFY_IPV6OPT_HOPOPTS
%token	IPFY_IPV6OPT_IPV6 IPFY_IPV6OPT_NONE IPFY_IPV6OPT_ROUTING IPFY_V6HDR
%token	IPFY_IPV6OPT_MOBILITY IPFY_IPV6OPT_ESP IPFY_IPV6OPT_FRAG

%token	IPFY_ICMPT_UNR IPFY_ICMPT_ECHO IPFY_ICMPT_ECHOR IPFY_ICMPT_SQUENCH
%token	IPFY_ICMPT_REDIR IPFY_ICMPT_TIMEX IPFY_ICMPT_PARAMP IPFY_ICMPT_TIMEST
%token	IPFY_ICMPT_TIMESTREP IPFY_ICMPT_INFOREQ IPFY_ICMPT_INFOREP
%token	IPFY_ICMPT_MASKREQ IPFY_ICMPT_MASKREP IPFY_ICMPT_ROUTERAD
%token	IPFY_ICMPT_ROUTERSOL

%token	IPFY_ICMPC_NETUNR IPFY_ICMPC_HSTUNR IPFY_ICMPC_PROUNR IPFY_ICMPC_PORUNR
%token	IPFY_ICMPC_NEEDF IPFY_ICMPC_SRCFAIL IPFY_ICMPC_NETUNK IPFY_ICMPC_HSTUNK
%token	IPFY_ICMPC_ISOLATE IPFY_ICMPC_NETPRO IPFY_ICMPC_HSTPRO
%token	IPFY_ICMPC_NETTOS IPFY_ICMPC_HSTTOS IPFY_ICMPC_FLTPRO IPFY_ICMPC_HSTPRE
%token	IPFY_ICMPC_CUTPRE

%token	IPFY_FAC_KERN IPFY_FAC_USER IPFY_FAC_MAIL IPFY_FAC_DAEMON IPFY_FAC_AUTH
%token	IPFY_FAC_SYSLOG IPFY_FAC_LPR IPFY_FAC_NEWS IPFY_FAC_UUCP IPFY_FAC_CRON
%token	IPFY_FAC_LOCAL0 IPFY_FAC_LOCAL1 IPFY_FAC_LOCAL2 IPFY_FAC_LOCAL3
%token	IPFY_FAC_LOCAL4 IPFY_FAC_LOCAL5 IPFY_FAC_LOCAL6 IPFY_FAC_LOCAL7
%token	IPFY_FAC_SECURITY IPFY_FAC_FTP IPFY_FAC_AUTHPRIV IPFY_FAC_AUDIT
%token	IPFY_FAC_LFMT IPFY_FAC_CONSOLE

%token	IPFY_PRI_EMERG IPFY_PRI_ALERT IPFY_PRI_CRIT IPFY_PRI_ERR IPFY_PRI_WARN
%token	IPFY_PRI_NOTICE IPFY_PRI_INFO IPFY_PRI_DEBUG
%%
file:	settings rules
	| rules
	;

settings:
	YY_COMMENT
	| setting
	| settings setting
	;

rules:	line
	| assign
	| rules line
	| rules assign
	;

setting:
	IPFY_SET YY_STR YY_NUMBER ';'	{ do_tuneint($2, $3); }
	| IPFY_SET YY_STR YY_HEX ';'	{ do_tuneint($2, $3); }
	| IPFY_SET YY_STR YY_STR ';'	{ do_tunestr($2, $3); }
	;

line:	rule		{ while ((fr = frtop) != NULL) {
				frtop = fr->fr_next;
				fr->fr_next = NULL;
				if ((fr->fr_type == FR_T_IPF) &&
				    (fr->fr_ip.fi_v == 0))
					fr->fr_mip.fi_v = 0;
				/* XXX validate ? */
				(*ipfaddfunc)(ipffd, ipfioctls[IPL_LOGIPF], fr);
				fr->fr_next = frold;
				frold = fr;
			  }
			  resetlexer();
			}
	| YY_COMMENT
	;

xx:					{ newrule(); }
	;

assign:	YY_STR assigning YY_STR ';'	{ set_variable($1, $3);
					  resetlexer();
					  free($1);
					  free($3);
					  yyvarnext = 0;
					}
	;

assigning:
	'='				{ yyvarnext = 1; }
	;

rule:	inrule eol
	| outrule eol
	;

eol:	| ';'
	;

inrule:
	rulehead markin inopts rulemain ruletail intag ruletail2
	;

outrule:
	rulehead markout outopts rulemain ruletail outtag ruletail2
	;

rulehead:
	xx collection action
	| xx insert collection action
	;

markin:	IPFY_IN				{ fr->fr_flags |= FR_INQUE; }
	;

markout:
	IPFY_OUT			{ fr->fr_flags |= FR_OUTQUE; }
	;

rulemain:
	ipfrule
	| bpfrule
	| exprrule
	;

ipfrule:
	family tos ttl proto ip
	;

family:	| IPFY_FAMILY IPFY_INET		{ if (use_inet6 == 1) {
						YYERROR;
					  } else {
						frc->fr_family = AF_INET;
					  }
					}
	| IPFY_INET			{ if (use_inet6 == 1) {
						YYERROR;
					  } else {
						frc->fr_family = AF_INET;
					  }
					}
	| IPFY_FAMILY IPFY_INET6	{ if (use_inet6 == -1) {
						YYERROR;
					  } else {
						frc->fr_family = AF_INET6;
					  }
					}
	| IPFY_INET6			{ if (use_inet6 == -1) {
						YYERROR;
					  } else {
						frc->fr_family = AF_INET6;
					  }
					}
	;

bpfrule:
	IPFY_BPFV4 '{' YY_STR '}' 	{ dobpf(4, $3); free($3); }
	| IPFY_BPFV6 '{' YY_STR '}' 	{ dobpf(6, $3); free($3); }
	;

exprrule:
	IPFY_IPFEXPR '{' YY_STR '}'	{ doipfexpr($3); }
	;

ruletail:
	with keep head group
	;

ruletail2:
	pps age new rulettl comment
	;

intag:	settagin matchtagin
	;

outtag:	settagout matchtagout
	;

insert:
	'@' YY_NUMBER			{ fr->fr_hits = (U_QUAD_T)$2 + 1; }
	;

collection:
	| YY_NUMBER			{ fr->fr_collect = $1; }
	;

action:	block
	| IPFY_PASS			{ fr->fr_flags |= FR_PASS; }
	| IPFY_NOMATCH			{ fr->fr_flags |= FR_NOMATCH; }
	| log
	| IPFY_COUNT			{ fr->fr_flags |= FR_ACCOUNT; }
	| decaps			{ fr->fr_flags |= FR_DECAPSULATE; }
	| auth
	| IPFY_SKIP YY_NUMBER		{ fr->fr_flags |= FR_SKIP;
					  fr->fr_arg = $2; }
	| IPFY_CALL func
	| IPFY_CALL IPFY_NOW func	{ fr->fr_flags |= FR_CALLNOW; }
	;

block:	blocked
	| blocked blockreturn
	;

blocked:
	IPFY_BLOCK			{ fr->fr_flags = FR_BLOCK; }
	;
blockreturn:
	IPFY_RETICMP			{ fr->fr_flags |= FR_RETICMP; }
	| IPFY_RETICMP returncode	{ fr->fr_flags |= FR_RETICMP; }
	| IPFY_RETICMPASDST		{ fr->fr_flags |= FR_FAKEICMP; }
	| IPFY_RETICMPASDST returncode	{ fr->fr_flags |= FR_FAKEICMP; }
	| IPFY_RETRST			{ fr->fr_flags |= FR_RETRST; }
	;

decaps:	IPFY_DECAPS
	| IPFY_DECAPS IPFY_L5AS '(' YY_STR ')'
					{ fr->fr_icode = atoi($4); }
	;

log:	IPFY_LOG			{ fr->fr_flags |= FR_LOG; }
	| IPFY_LOG logoptions		{ fr->fr_flags |= FR_LOG; }
	;

auth:	IPFY_AUTH			{ fr->fr_flags |= FR_AUTH; }
	| IPFY_AUTH blockreturn		{ fr->fr_flags |= FR_AUTH;}
	| IPFY_PREAUTH			{ fr->fr_flags |= FR_PREAUTH; }
	;

func:	YY_STR '/' YY_NUMBER
			{ fr->fr_func = nametokva($1, ipfioctls[IPL_LOGIPF]);
			  fr->fr_arg = $3;
			  free($1);
			}
	;

inopts:
	| inopts inopt
	;

inopt:
	logopt
	| quick
	| on
	| dup
	| froute
	| proute
	| replyto
	;

outopts:
	| outopts outopt
	;

outopt:
	logopt
	| quick
	| on
	| dup
	| proute
	| froute
	| replyto
	;

tos:	| settos YY_NUMBER	{ DOALL(fr->fr_tos = $2; fr->fr_mtos = 0xff;) }
	| settos YY_HEX	{ DOALL(fr->fr_tos = $2; fr->fr_mtos = 0xff;) }
	| settos lstart toslist lend
	;

settos:	IPFY_TOS			{ setipftype(); }
	;

toslist:
	YY_NUMBER	{ DOALL(fr->fr_tos = $1; fr->fr_mtos = 0xff;) }
	| YY_HEX	{ DOREM(fr->fr_tos = $1; fr->fr_mtos = 0xff;) }
	| toslist lmore YY_NUMBER
			{ DOREM(fr->fr_tos = $3; fr->fr_mtos = 0xff;) }
	| toslist lmore YY_HEX
			{ DOREM(fr->fr_tos = $3; fr->fr_mtos = 0xff;) }
	;

ttl:	| setttl YY_NUMBER
			{ DOALL(fr->fr_ttl = $2; fr->fr_mttl = 0xff;) }
	| setttl lstart ttllist lend
	;

lstart:	'{'				{ newlist = 1; fr = frc; added = 0; }
	;

lend:	'}'				{ nrules += added; }
	;

lmore:	lanother			{ if (newlist == 1) {
						newlist = 0;
					  }
					  fr = addrule();
					  if (yycont != NULL)
						*yycont = 1;
					}
	;

lanother:
	| ','
	;

setttl:	IPFY_TTL			{ setipftype(); }
	;

ttllist:
	YY_NUMBER	{ DOREM(fr->fr_ttl = $1; fr->fr_mttl = 0xff;) }
	| ttllist lmore YY_NUMBER
			{ DOREM(fr->fr_ttl = $3; fr->fr_mttl = 0xff;) }
	;

proto:	| protox protocol		{ yyresetdict(); }
	;

protox:	IPFY_PROTO			{ setipftype();
					  fr = frc;
					  yysetdict(NULL); }
	;

ip:	srcdst flags icmp
	;

group:	| IPFY_GROUP groupname		{ DOALL(setgroup(&fr, $2); \
						fillgroup(fr););
					  free($2);
					}
	;

head:	| IPFY_HEAD groupname		{ DOALL(setgrhead(&fr, $2););
					  free($2);
					}
	;

groupname:
	YY_STR				{ $$ = $1;
					  if (strlen($$) >= FR_GROUPLEN)
						$$[FR_GROUPLEN - 1] = '\0';
					}
	| YY_NUMBER			{ $$ = malloc(16);
					  sprintf($$, "%d", $1);
					}
	;

settagin:
	| IPFY_SETTAG '(' taginlist ')'
	;

taginlist:
	taginspec
	| taginlist ',' taginspec
	;

taginspec:
	logtag
	;

nattag:	IPFY_NAT '=' YY_STR		{ DOALL(strncpy(fr->fr_nattag.ipt_tag,\
						$3, IPFTAG_LEN););
					  free($3); }
	| IPFY_NAT '=' YY_NUMBER	{ DOALL(sprintf(fr->fr_nattag.ipt_tag,\
						"%d", $3 & 0xffffffff);) }
	;

logtag:	IPFY_LOG '=' YY_NUMBER		{ DOALL(fr->fr_logtag = $3;) }
	;

settagout:
	| IPFY_SETTAG '(' tagoutlist ')'
	;

tagoutlist:
	tagoutspec
	| tagoutlist ',' tagoutspec
	;

tagoutspec:
	logtag
	| nattag
	;

matchtagin:
	| IPFY_MATCHTAG '(' tagoutlist ')'
	;

matchtagout:
	| IPFY_MATCHTAG '(' taginlist ')'
	;

pps:	| IPFY_PPS YY_NUMBER		{ DOALL(fr->fr_pps = $2;) }
	;

new:	| savegroup file restoregroup
	;

rulettl:
	| IPFY_RULETTL YY_NUMBER	{ DOALL(fr->fr_die = $2;) }
	;

comment:
	| IPFY_COMMENT YY_STR		{ DOALL(fr->fr_comment = addname(&fr, \
						$2);) }
	;

savegroup:
	'{'
	;

restoregroup:
	'}'
	;

logopt:	log
	;

quick:	IPFY_QUICK				{ fr->fr_flags |= FR_QUICK; }
	;

on:	IPFY_ON onname				{ setifname(&fr, 0, $2.if1);
						  free($2.if1);
						  if ($2.if2 != NULL) {
							setifname(&fr, 1,
								  $2.if2);
							free($2.if2);
						  }
						}
	| IPFY_ON lstart onlist lend
	| IPFY_ON onname IPFY_INVIA vianame	{ setifname(&fr, 0, $2.if1);
						  free($2.if1);
						  if ($2.if2 != NULL) {
							setifname(&fr, 1,
								  $2.if2);
							free($2.if2);
						  }
						}
	| IPFY_ON onname IPFY_OUTVIA vianame	{ setifname(&fr, 0, $2.if1);
						  free($2.if1);
						  if ($2.if2 != NULL) {
							setifname(&fr, 1,
								  $2.if2);
							free($2.if2);
						  }
						}
	;

onlist:	onname			{ DOREM(setifname(&fr, 0, $1.if1);	   \
					if ($1.if2 != NULL)		   \
						setifname(&fr, 1, $1.if2); \
					)
				  free($1.if1);
				  if ($1.if2 != NULL)
					free($1.if2);
				}
	| onlist lmore onname	{ DOREM(setifname(&fr, 0, $3.if1);	   \
					if ($3.if2 != NULL)		   \
						setifname(&fr, 1, $3.if2); \
					)
				  free($3.if1);
				  if ($3.if2 != NULL)
					free($3.if2);
				}
	;

onname:	interfacename		{ $$.if1 = $1;
				  $$.if2 = NULL;
				}
	| interfacename ',' interfacename
				{ $$.if1 = $1;
				  $$.if2 = $3;
				}
	;

vianame:
	name			{ setifname(&fr, 2, $1);
				  free($1);
				}
	| name ',' name		{ setifname(&fr, 2, $1);
				  free($1);
				  setifname(&fr, 3, $3);
				  free($3);
				}
	;

dup:	IPFY_DUPTO name
	{ int idx = addname(&fr, $2);
	  fr->fr_dif.fd_name = idx;
	  free($2);
	}
	| IPFY_DUPTO IPFY_DSTLIST '/' name
	{ int idx = addname(&fr, $4);
	  fr->fr_dif.fd_name = idx;
	  fr->fr_dif.fd_type = FRD_DSTLIST;
	  free($4);
	}
	| IPFY_DUPTO name duptoseparator hostname
	{ int idx = addname(&fr, $2);
	  fr->fr_dif.fd_name = idx;
	  fr->fr_dif.fd_ptr = (void *)-1;
	  fr->fr_dif.fd_ip6 = $4.adr;
	  if (fr->fr_family == AF_UNSPEC && $4.f != AF_UNSPEC)
		fr->fr_family = $4.f;
	  yyexpectaddr = 0;
	  free($2);
	}
	;

duptoseparator:
	':'	{ yyexpectaddr = 1; yycont = &yyexpectaddr; resetaddr(); }
	;

froute:	IPFY_FROUTE			{ fr->fr_flags |= FR_FASTROUTE; }
	;

proute:	routeto name
	{ int idx = addname(&fr, $2);
	  fr->fr_tif.fd_name = idx;
	  free($2);
	}
	| routeto IPFY_DSTLIST '/' name
	{ int idx = addname(&fr, $4);
	  fr->fr_tif.fd_name = idx;
	  fr->fr_tif.fd_type = FRD_DSTLIST;
	  free($4);
	}
	| routeto name duptoseparator hostname
	{ int idx = addname(&fr, $2);
	  fr->fr_tif.fd_name = idx;
	  fr->fr_tif.fd_ptr = (void *)-1;
	  fr->fr_tif.fd_ip6 = $4.adr;
	  if (fr->fr_family == AF_UNSPEC && $4.f != AF_UNSPEC)
		fr->fr_family = $4.f;
	  yyexpectaddr = 0;
	  free($2);
	}
	;

routeto:
	IPFY_TO
	| IPFY_ROUTETO
	;

replyto:
	IPFY_REPLY_TO name
	{ int idx = addname(&fr, $2);
	  fr->fr_rif.fd_name = idx;
	  free($2);
	}
	| IPFY_REPLY_TO IPFY_DSTLIST '/' name
	{ fr->fr_rif.fd_name = addname(&fr, $4);
	  fr->fr_rif.fd_type = FRD_DSTLIST;
	  free($4);
	}
	| IPFY_REPLY_TO name duptoseparator hostname
	{ int idx = addname(&fr, $2);
	  fr->fr_rif.fd_name = idx;
	  fr->fr_rif.fd_ptr = (void *)-1;
	  fr->fr_rif.fd_ip6 = $4.adr;
	  if (fr->fr_family == AF_UNSPEC && $4.f != AF_UNSPEC)
		fr->fr_family = $4.f;
	  free($2);
	}
	;

logoptions:
	logoption
	| logoptions logoption
	;

logoption:
	IPFY_BODY			{ fr->fr_flags |= FR_LOGBODY; }
	| IPFY_FIRST			{ fr->fr_flags |= FR_LOGFIRST; }
	| IPFY_ORBLOCK			{ fr->fr_flags |= FR_LOGORBLOCK; }
	| level loglevel		{ unsetsyslog(); }
	;

returncode:
	starticmpcode icmpcode ')'	{ fr->fr_icode = $2; yyresetdict(); }
	;

starticmpcode:
	'('				{ yysetdict(icmpcodewords); }
	;

srcdst:	| IPFY_ALL
	| fromto
	;

protocol:
	YY_NUMBER		{ DOALL(fr->fr_proto = $1; \
					fr->fr_mproto = 0xff;)
				}
	| YY_STR		{ if (!strcmp($1, "tcp-udp")) {
					DOALL(fr->fr_flx |= FI_TCPUDP; \
					      fr->fr_mflx |= FI_TCPUDP;)
				  } else {
					int p = getproto($1);
					if (p == -1)
						yyerror("protocol unknown");
					DOALL(fr->fr_proto = p; \
						fr->fr_mproto = 0xff;)
				  }
				  free($1);
				}
	| YY_STR nextstring YY_STR
				{ if (!strcmp($1, "tcp") &&
				      !strcmp($3, "udp")) {
					DOREM(fr->fr_flx |= FI_TCPUDP; \
					      fr->fr_mflx |= FI_TCPUDP;)
				  } else {
					YYERROR;
				  }
				  free($1);
				  free($3);
				}
	;

nextstring:
	'/'			{ yysetdict(NULL); }
	;

fromto:	from srcobject to dstobject	{ yyexpectaddr = 0; yycont = NULL; }
	| to dstobject			{ yyexpectaddr = 0; yycont = NULL; }
	| from srcobject		{ yyexpectaddr = 0; yycont = NULL; }
	;

from:	IPFY_FROM			{ setipftype();
					  if (fr == NULL)
						fr = frc;
					  yyexpectaddr = 1;
					  if (yydebug)
						printf("set yyexpectaddr\n");
					  yycont = &yyexpectaddr;
					  yysetdict(addrwords);
					  resetaddr(); }
	;

to:	IPFY_TO				{ if (fr == NULL)
						fr = frc;
					  yyexpectaddr = 1;
					  if (yydebug)
						printf("set yyexpectaddr\n");
					  yycont = &yyexpectaddr;
					  yysetdict(addrwords);
					  resetaddr();
					}
	;

with:	| andwith withlist
	;

andwith:
	IPFY_WITH			{ nowith = 0; setipftype(); }
	| IPFY_AND			{ nowith = 0; setipftype(); }
	;

flags:	| startflags flagset
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = FR_TCPFMAX;) }
	| startflags flagset '/' flagset
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = $4;) }
	| startflags '/' flagset
		{ DOALL(fr->fr_tcpf = 0; fr->fr_tcpfm = $3;) }
	| startflags YY_NUMBER
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = FR_TCPFMAX;) }
	| startflags '/' YY_NUMBER
		{ DOALL(fr->fr_tcpf = 0; fr->fr_tcpfm = $3;) }
	| startflags YY_NUMBER '/' YY_NUMBER
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = $4;) }
	| startflags flagset '/' YY_NUMBER
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = $4;) }
	| startflags YY_NUMBER '/' flagset
		{ DOALL(fr->fr_tcpf = $2; fr->fr_tcpfm = $4;) }
	;

startflags:
	IPFY_FLAGS	{ if (frc->fr_type != FR_T_IPF)
				yyerror("flags with non-ipf type rule");
			  if (frc->fr_proto != IPPROTO_TCP)
				yyerror("flags with non-TCP rule");
			}
	;

flagset:
	YY_STR				{ $$ = tcpflags($1); free($1); }
	| YY_HEX			{ $$ = $1; }
	;

srcobject:
	{ yyresetdict(); } fromport
	| srcaddr srcport
	| '!' srcaddr srcport
		{ DOALL(fr->fr_flags |= FR_NOTSRCIP;) }
	;

srcaddr:
	addr	{ build_srcaddr_af(fr, &$1); }
	| lstart srcaddrlist lend
	;

srcaddrlist:
	addr	{ build_srcaddr_af(fr, &$1); }
	| srcaddrlist lmore addr
		{ build_srcaddr_af(fr, &$3); }
	;

srcport:
	| portcomp
		{ DOALL(fr->fr_scmp = $1.pc; fr->fr_sport = $1.p1;) }
	| portrange
		{ DOALL(fr->fr_scmp = $1.pc; fr->fr_sport = $1.p1; \
			fr->fr_stop = $1.p2;) }
	| porteq lstart srcportlist lend
		{ yyresetdict(); }
	;

fromport:
	portcomp
		{ DOALL(fr->fr_scmp = $1.pc; fr->fr_sport = $1.p1;) }
	| portrange
		{ DOALL(fr->fr_scmp = $1.pc; fr->fr_sport = $1.p1; \
			fr->fr_stop = $1.p2;) }
	| porteq lstart srcportlist lend
		{ yyresetdict(); }
	;

srcportlist:
	portnum		{ DOREM(fr->fr_scmp = FR_EQUAL; fr->fr_sport = $1;) }
	| portnum ':' portnum
			{ DOREM(fr->fr_scmp = FR_INCRANGE; fr->fr_sport = $1; \
				fr->fr_stop = $3;) }
	| portnum YY_RANGE_IN portnum
			{ DOREM(fr->fr_scmp = FR_INRANGE; fr->fr_sport = $1; \
				fr->fr_stop = $3;) }
	| srcportlist lmore portnum
			{ DOREM(fr->fr_scmp = FR_EQUAL; fr->fr_sport = $3;) }
	| srcportlist lmore portnum ':' portnum
			{ DOREM(fr->fr_scmp = FR_INCRANGE; fr->fr_sport = $3; \
				fr->fr_stop = $5;) }
	| srcportlist lmore portnum YY_RANGE_IN portnum
			{ DOREM(fr->fr_scmp = FR_INRANGE; fr->fr_sport = $3; \
				fr->fr_stop = $5;) }
	;

dstobject:
	{ yyresetdict(); } toport
	| dstaddr dstport
	| '!' dstaddr dstport
			{ DOALL(fr->fr_flags |= FR_NOTDSTIP;) }
	;

dstaddr:
	addr	{ if (($1.f != AF_UNSPEC) && (frc->fr_family != AF_UNSPEC) &&
		      ($1.f != frc->fr_family))
			yyerror("1.src/dst address family mismatch");
		  build_dstaddr_af(fr, &$1);
		}
	| lstart dstaddrlist lend
	;

dstaddrlist:
	addr	{ if (($1.f != AF_UNSPEC) && (frc->fr_family != AF_UNSPEC) &&
		      ($1.f != frc->fr_family))
			yyerror("2.src/dst address family mismatch");
		  build_dstaddr_af(fr, &$1);
		}
	| dstaddrlist lmore addr
		{ if (($3.f != AF_UNSPEC) && (frc->fr_family != AF_UNSPEC) &&
		      ($3.f != frc->fr_family))
			yyerror("3.src/dst address family mismatch");
		  build_dstaddr_af(fr, &$3);
		}
	;


dstport:
	| portcomp
		{ DOALL(fr->fr_dcmp = $1.pc; fr->fr_dport = $1.p1;) }
	| portrange
		{ DOALL(fr->fr_dcmp = $1.pc; fr->fr_dport = $1.p1; \
			fr->fr_dtop = $1.p2;) }
	| porteq lstart dstportlist lend
		{ yyresetdict(); }
	;

toport:
	portcomp
		{ DOALL(fr->fr_dcmp = $1.pc; fr->fr_dport = $1.p1;) }
	| portrange
		{ DOALL(fr->fr_dcmp = $1.pc; fr->fr_dport = $1.p1; \
			fr->fr_dtop = $1.p2;) }
	| porteq lstart dstportlist lend
		{ yyresetdict(); }
	;

dstportlist:
	portnum		{ DOREM(fr->fr_dcmp = FR_EQUAL; fr->fr_dport = $1;) }
	| portnum ':' portnum
			{ DOREM(fr->fr_dcmp = FR_INCRANGE; fr->fr_dport = $1; \
				fr->fr_dtop = $3;) }
	| portnum YY_RANGE_IN portnum
			{ DOREM(fr->fr_dcmp = FR_INRANGE; fr->fr_dport = $1; \
				fr->fr_dtop = $3;) }
	| dstportlist lmore portnum
			{ DOREM(fr->fr_dcmp = FR_EQUAL; fr->fr_dport = $3;) }
	| dstportlist lmore portnum ':' portnum
			{ DOREM(fr->fr_dcmp = FR_INCRANGE; fr->fr_dport = $3; \
				fr->fr_dtop = $5;) }
	| dstportlist lmore portnum YY_RANGE_IN portnum
			{ DOREM(fr->fr_dcmp = FR_INRANGE; fr->fr_dport = $3; \
				fr->fr_dtop = $5;) }
	;

addr:	pool '/' YY_NUMBER		{ pooled = 1;
					  yyexpectaddr = 0;
					  $$.type = FRI_LOOKUP;
					  $$.v = 0;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.a.iplookuptype = IPLT_POOL;
					  $$.a.iplookupsubtype = 0;
					  $$.a.iplookupnum = $3; }
	| pool '/' YY_STR		{ pooled = 1;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.type = FRI_LOOKUP;
					  $$.a.iplookuptype = IPLT_POOL;
					  $$.a.iplookupsubtype = 1;
					  $$.a.iplookupname = addname(&fr, $3);
					}
	| pool '=' '('			{ yyexpectaddr = 1;
					  pooled = 1;
					}
			poollist ')'	{ yyexpectaddr = 0;
					  $$.v = 0;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.type = FRI_LOOKUP;
					  $$.a.iplookuptype = IPLT_POOL;
					  $$.a.iplookupsubtype = 0;
					  $$.a.iplookupnum = makepool($5);
					}
	| hash '/' YY_NUMBER		{ hashed = 1;
					  yyexpectaddr = 0;
					  $$.v = 0;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.type = FRI_LOOKUP;
					  $$.a.iplookuptype = IPLT_HASH;
					  $$.a.iplookupsubtype = 0;
					  $$.a.iplookupnum = $3;
					}
	| hash '/' YY_STR		{ hashed = 1;
					  $$.type = FRI_LOOKUP;
					  $$.v = 0;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.a.iplookuptype = IPLT_HASH;
					  $$.a.iplookupsubtype = 1;
					  $$.a.iplookupname = addname(&fr, $3);
					}
	| hash '=' '(' 			{ hashed = 1;
					  yyexpectaddr = 1;
					}
			addrlist ')'	{ yyexpectaddr = 0;
					  $$.v = 0;
					  $$.ifpos = -1;
					  $$.f = AF_UNSPEC;
					  $$.type = FRI_LOOKUP;
					  $$.a.iplookuptype = IPLT_HASH;
					  $$.a.iplookupsubtype = 0;
					  $$.a.iplookupnum = makehash($5);
					}
	| ipaddr			{ $$ = $1;
					  yyexpectaddr = 0; }
	;

ipaddr:	IPFY_ANY			{ memset(&($$), 0, sizeof($$));
					  $$.type = FRI_NORMAL;
					  $$.ifpos = -1;
					  yyexpectaddr = 0;
					}
	| hostname			{ memset(&($$), 0, sizeof($$));
					  $$.a = $1.adr;
					  $$.f = $1.f;
					  if ($1.f == AF_INET6)
						  fill6bits(128, $$.m.i6);
					  else if ($1.f == AF_INET)
						  fill6bits(32, $$.m.i6);
					  $$.v = ftov($1.f);
					  $$.ifpos = dynamic;
					  $$.type = FRI_NORMAL;
					}
	| hostname			{ yyresetdict(); }
		maskspace		{ yysetdict(maskwords);
					  yyexpectaddr = 2; }
		ipmask			{ memset(&($$), 0, sizeof($$));
					  ntomask($1.f, $5, $$.m.i6);
					  $$.a = $1.adr;
					  $$.a.i6[0] &= $$.m.i6[0];
					  $$.a.i6[1] &= $$.m.i6[1];
					  $$.a.i6[2] &= $$.m.i6[2];
					  $$.a.i6[3] &= $$.m.i6[3];
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  $$.type = ifpflag;
					  $$.ifpos = dynamic;
					  if (ifpflag != 0 && $$.v == 0) {
						if (frc->fr_family == AF_INET6){
							$$.v = 6;
							$$.f = AF_INET6;
						} else {
							$$.v = 4;
							$$.f = AF_INET;
						}
					  }
					  yyresetdict();
					  yyexpectaddr = 0;
					}
	| '(' YY_STR ')'		{ memset(&($$), 0, sizeof($$));
					  $$.type = FRI_DYNAMIC;
					  ifpflag = FRI_DYNAMIC;
					  $$.ifpos = addname(&fr, $2);
					  $$.lif = 0;
					}
	| '(' YY_STR ')' '/'
	  { ifpflag = FRI_DYNAMIC; yysetdict(maskwords); }
	  maskopts
					{ memset(&($$), 0, sizeof($$));
					  $$.type = ifpflag;
					  $$.ifpos = addname(&fr, $2);
					  $$.lif = 0;
					  if (frc->fr_family == AF_UNSPEC)
						frc->fr_family = AF_INET;
					  if (ifpflag == FRI_DYNAMIC) {
						ntomask(frc->fr_family,
							$6, $$.m.i6);
					  }
					  yyresetdict();
					  yyexpectaddr = 0;
					}
	| '(' YY_STR ':' YY_NUMBER ')' '/'
	  { ifpflag = FRI_DYNAMIC; yysetdict(maskwords); }
	  maskopts
					{ memset(&($$), 0, sizeof($$));
					  $$.type = ifpflag;
					  $$.ifpos = addname(&fr, $2);
					  $$.lif = $4;
					  if (frc->fr_family == AF_UNSPEC)
						frc->fr_family = AF_INET;
					  if (ifpflag == FRI_DYNAMIC) {
						ntomask(frc->fr_family,
							$8, $$.m.i6);
					  }
					  yyresetdict();
					  yyexpectaddr = 0;
					}
	;

maskspace:
	'/'
	| IPFY_MASK
	;

ipmask:	ipv4				{ $$ = count4bits($1.s_addr); }
	| YY_HEX			{ $$ = count4bits(htonl($1)); }
	| YY_NUMBER			{ $$ = $1; }
	| YY_IPV6			{ $$ = count6bits($1.i6); }
	| maskopts			{ $$ = $1; }
	;

maskopts:
	IPFY_BROADCAST			{ if (ifpflag == FRI_DYNAMIC) {
						ifpflag = FRI_BROADCAST;
					  } else {
						YYERROR;
					  }
					  $$ = 0;
					}
	| IPFY_NETWORK			{ if (ifpflag == FRI_DYNAMIC) {
						ifpflag = FRI_NETWORK;
					  } else {
						YYERROR;
					  }
					  $$ = 0;
					}
	| IPFY_NETMASKED		{ if (ifpflag == FRI_DYNAMIC) {
						ifpflag = FRI_NETMASKED;
					  } else {
						YYERROR;
					  }
					  $$ = 0;
					}
	| IPFY_PEER			{ if (ifpflag == FRI_DYNAMIC) {
						ifpflag = FRI_PEERADDR;
					  } else {
						YYERROR;
					  }
					  $$ = 0;
					}
	| YY_NUMBER			{ $$ = $1; }
	;

hostname:
	ipv4				{ memset(&($$), 0, sizeof($$));
					  $$.adr.in4 = $1;
					  if (frc->fr_family == AF_INET6)
						YYERROR;
					  $$.f = AF_INET;
					  yyexpectaddr = 2;
					}
	| YY_NUMBER			{ memset(&($$), 0, sizeof($$));
					  if (frc->fr_family == AF_INET6)
						YYERROR;
					  $$.adr.in4_addr = $1;
					  $$.f = AF_INET;
					  yyexpectaddr = 2;
					}
	| YY_HEX			{ memset(&($$), 0, sizeof($$));
					  if (frc->fr_family == AF_INET6)
						YYERROR;
					  $$.adr.in4_addr = $1;
					  $$.f = AF_INET;
					  yyexpectaddr = 2;
					}
	| YY_STR			{ memset(&($$), 0, sizeof($$));
					  if (lookuphost($1, &$$.adr) == 0)
						  $$.f = AF_INET;
					  free($1);
					  yyexpectaddr = 2;
					}
	| YY_IPV6			{ memset(&($$), 0, sizeof($$));
					  if (frc->fr_family == AF_INET)
						YYERROR;
					  $$.adr = $1;
					  $$.f = AF_INET6;
					  yyexpectaddr = 2;
					}
	;

addrlist:
	ipaddr		{ $$ = newalist(NULL);
			  $$->al_family = $1.f;
			  $$->al_i6addr = $1.a;
			  $$->al_i6mask = $1.m;
			}
	| ipaddr ',' { yyexpectaddr = 1; } addrlist
			{ $$ = newalist($4);
			  $$->al_family = $1.f;
			  $$->al_i6addr = $1.a;
			  $$->al_i6mask = $1.m;
			}
	;

pool:	IPFY_POOL	{ yyexpectaddr = 0; yycont = NULL; yyresetdict(); }
	;

hash:	IPFY_HASH	{ yyexpectaddr = 0; yycont = NULL; yyresetdict(); }
	;

poollist:
	ipaddr		{ $$ = newalist(NULL);
			  $$->al_family = $1.f;
			  $$->al_i6addr = $1.a;
			  $$->al_i6mask = $1.m;
			}
	| '!' ipaddr	{ $$ = newalist(NULL);
			  $$->al_not = 1;
			  $$->al_family = $2.f;
			  $$->al_i6addr = $2.a;
			  $$->al_i6mask = $2.m;
			}
	| poollist ',' ipaddr
			{ $$ = newalist($1);
			  $$->al_family = $3.f;
			  $$->al_i6addr = $3.a;
			  $$->al_i6mask = $3.m;
			}
	| poollist ',' '!' ipaddr
			{ $$ = newalist($1);
			  $$->al_not = 1;
			  $$->al_family = $4.f;
			  $$->al_i6addr = $4.a;
			  $$->al_i6mask = $4.m;
			}
	;

port:	IPFY_PORT			{ yyexpectaddr = 0;
					  yycont = NULL;
					  if (frc->fr_proto != 0 &&
					      frc->fr_proto != IPPROTO_UDP &&
					      frc->fr_proto != IPPROTO_TCP)
						yyerror("port use incorrect");
					}
	;

portc:	port compare			{ $$ = $2;
					  yysetdict(NULL);
					}
	| porteq			{ $$ = $1; }
	;

porteq:	port '='			{ $$ = FR_EQUAL;
					  yysetdict(NULL);
					}
	;

portr:	IPFY_PORT			{ yyexpectaddr = 0;
					  yycont = NULL;
					  yysetdict(NULL);
					}
	;

portcomp:
	portc portnum			{ $$.pc = $1;
					  $$.p1 = $2;
					  yyresetdict();
					}
	;

portrange:
	portr portnum range portnum	{ $$.p1 = $2;
					  $$.pc = $3;
					  $$.p2 = $4;
					  yyresetdict();
					}
	;

icmp:	| itype icode
	;

itype:	seticmptype icmptype
	{ DOALL(fr->fr_icmp = htons($2 << 8); fr->fr_icmpm = htons(0xff00););
	  yyresetdict();
	}
	| seticmptype lstart typelist lend	{ yyresetdict(); }
	;

seticmptype:
	IPFY_ICMPTYPE		{ if (frc->fr_family == AF_UNSPEC)
					frc->fr_family = AF_INET;
				  if (frc->fr_family == AF_INET &&
				      frc->fr_type == FR_T_IPF &&
				      frc->fr_proto != IPPROTO_ICMP) {
					yyerror("proto not icmp");
				  }
				  if (frc->fr_family == AF_INET6 &&
				      frc->fr_type == FR_T_IPF &&
				      frc->fr_proto != IPPROTO_ICMPV6) {
					yyerror("proto not ipv6-icmp");
				  }
				  setipftype();
				  DOALL(if (fr->fr_family == AF_INET) { \
						fr->fr_ip.fi_v = 4; \
						fr->fr_mip.fi_v = 0xf; \
					}
					if (fr->fr_family == AF_INET6) { \
						fr->fr_ip.fi_v = 6; \
						fr->fr_mip.fi_v = 0xf; \
					}
				  )
				  yysetdict(NULL);
				}
	;

icode:	| seticmpcode icmpcode
	{ DOALL(fr->fr_icmp |= htons($2); fr->fr_icmpm |= htons(0xff););
	  yyresetdict();
	}
	| seticmpcode lstart codelist lend	{ yyresetdict(); }
	;

seticmpcode:
	IPFY_ICMPCODE				{ yysetdict(icmpcodewords); }
	;

typelist:
	icmptype
	{ DOREM(fr->fr_icmp = htons($1 << 8); fr->fr_icmpm = htons(0xff00);) }
	| typelist lmore icmptype
	{ DOREM(fr->fr_icmp = htons($3 << 8); fr->fr_icmpm = htons(0xff00);) }
	;

codelist:
	icmpcode
	{ DOREM(fr->fr_icmp |= htons($1); fr->fr_icmpm |= htons(0xff);) }
	| codelist lmore icmpcode
	{ DOREM(fr->fr_icmp &= htons(0xff00); fr->fr_icmp |= htons($3); \
		fr->fr_icmpm |= htons(0xff);) }
	;

age:	| IPFY_AGE YY_NUMBER		{ DOALL(fr->fr_age[0] = $2; \
						fr->fr_age[1] = $2;) }
	| IPFY_AGE YY_NUMBER '/' YY_NUMBER
					{ DOALL(fr->fr_age[0] = $2; \
						fr->fr_age[1] = $4;) }
	;

keep:	| IPFY_KEEP keepstate keep
	| IPFY_KEEP keepfrag keep
	;

keepstate:
	IPFY_STATE stateoptlist		{ DOALL(fr->fr_flags |= FR_KEEPSTATE;)}
	;

keepfrag:
	IPFY_FRAGS fragoptlist		{ DOALL(fr->fr_flags |= FR_KEEPFRAG;) }
	| IPFY_FRAG fragoptlist		{ DOALL(fr->fr_flags |= FR_KEEPFRAG;) }
	;

fragoptlist:
	| '(' fragopts ')'
	;

fragopts:
	fragopt lanother fragopts
	| fragopt
	;

fragopt:
	IPFY_STRICT			{ DOALL(fr->fr_flags |= FR_FRSTRICT;) }
	;

stateoptlist:
	| '(' stateopts ')'
	;

stateopts:
	stateopt lanother stateopts
	| stateopt
	;

stateopt:
	IPFY_LIMIT YY_NUMBER	{ DOALL(fr->fr_statemax = $2;) }
	| IPFY_STRICT		{ DOALL(if (fr->fr_proto != IPPROTO_TCP) { \
						YYERROR; \
					} else if (fr->fr_flags & FR_STLOOSE) {\
						YYERROR; \
					} else \
						fr->fr_flags |= FR_STSTRICT;)
				}
	| IPFY_LOOSE		{ DOALL(if (fr->fr_proto != IPPROTO_TCP) { \
						YYERROR; \
					} else if (fr->fr_flags & FR_STSTRICT){\
						YYERROR; \
					} else \
						fr->fr_flags |= FR_STLOOSE;)
				}
	| IPFY_NEWISN		{ DOALL(if (fr->fr_proto != IPPROTO_TCP) { \
						YYERROR; \
					  } else \
						fr->fr_flags |= FR_NEWISN;)
				}
	| IPFY_NOICMPERR	{ DOALL(fr->fr_flags |= FR_NOICMPERR;) }

	| IPFY_SYNC		{ DOALL(fr->fr_flags |= FR_STATESYNC;) }
	| IPFY_AGE YY_NUMBER		{ DOALL(fr->fr_age[0] = $2; \
						fr->fr_age[1] = $2;) }
	| IPFY_AGE YY_NUMBER '/' YY_NUMBER
					{ DOALL(fr->fr_age[0] = $2; \
						fr->fr_age[1] = $4;) }
	| IPFY_ICMPHEAD groupname
				{ DOALL(seticmphead(&fr, $2);)
				  free($2);
				}
	| IPFY_NOLOG
				{ DOALL(fr->fr_nostatelog = 1;) }
	| IPFY_RPC
				{ DOALL(fr->fr_rpc = 1;) }
	| IPFY_RPC IPFY_IN YY_STR
				{ DOALL(fr->fr_rpc = 1;) }
	| IPFY_MAX_SRCS YY_NUMBER
				{ DOALL(fr->fr_srctrack.ht_max_nodes = $2;) }
	| IPFY_MAX_PER_SRC YY_NUMBER
				{ DOALL(fr->fr_srctrack.ht_max_per_node = $2; \
					fr->fr_srctrack.ht_netmask = \
					fr->fr_family == AF_INET ? 32: 128;)
				}
	| IPFY_MAX_PER_SRC YY_NUMBER '/' YY_NUMBER
				{ DOALL(fr->fr_srctrack.ht_max_per_node = $2; \
					fr->fr_srctrack.ht_netmask = $4;)
				}
	;

portnum:
	servicename			{ if (getport(frc, $1,
						      &($$), NULL) == -1)
						yyerror("service unknown");
					  $$ = ntohs($$);
					  free($1);
					}
	| YY_NUMBER			{ if ($1 > 65535)	/* Unsigned */
						yyerror("invalid port number");
					  else
						$$ = $1;
					}
	;

withlist:
	withopt				{ nowith = 0; }
	| withlist withopt		{ nowith = 0; }
	| withlist ',' withopt		{ nowith = 0; }
	;

withopt:
	opttype		{ DOALL(fr->fr_flx |= $1; fr->fr_mflx |= $1;) }
	| notwith opttype		{ DOALL(fr->fr_mflx |= $2;) }
	| ipopt ipopts			{ yyresetdict(); }
	| notwith ipopt ipopts		{ yyresetdict(); }
	| startv6hdr ipv6hdrs		{ yyresetdict(); }
	;

ipopt:	IPFY_OPT			{ yysetdict(ipv4optwords); }
	;

startv6hdr:
	IPFY_V6HDR	{ if (frc->fr_family != AF_INET6)
				yyerror("only available with IPv6");
			  yysetdict(ipv6optwords);
			}
	;

notwith:
	IPFY_NOT			{ nowith = 1; }
	| IPFY_NO			{ nowith = 1; }
	;

opttype:
	IPFY_IPOPTS			{ $$ = FI_OPTIONS; }
	| IPFY_SHORT			{ $$ = FI_SHORT; }
	| IPFY_NAT			{ $$ = FI_NATED; }
	| IPFY_BAD			{ $$ = FI_BAD; }
	| IPFY_BADNAT			{ $$ = FI_BADNAT; }
	| IPFY_BADSRC			{ $$ = FI_BADSRC; }
	| IPFY_LOWTTL			{ $$ = FI_LOWTTL; }
	| IPFY_FRAG			{ $$ = FI_FRAG; }
	| IPFY_FRAGBODY			{ $$ = FI_FRAGBODY; }
	| IPFY_FRAGS			{ $$ = FI_FRAG; }
	| IPFY_MBCAST			{ $$ = FI_MBCAST; }
	| IPFY_MULTICAST		{ $$ = FI_MULTICAST; }
	| IPFY_BROADCAST		{ $$ = FI_BROADCAST; }
	| IPFY_STATE			{ $$ = FI_STATE; }
	| IPFY_OOW			{ $$ = FI_OOW; }
	| IPFY_AH			{ $$ = FI_AH; }
	| IPFY_V6HDRS			{ $$ = FI_V6EXTHDR; }
	;

ipopts:	optlist		{ DOALL(fr->fr_mip.fi_optmsk |= $1;
				if (fr->fr_family == AF_UNSPEC) {
					fr->fr_family = AF_INET;
					fr->fr_ip.fi_v = 4;
					fr->fr_mip.fi_v = 0xf;
				} else if (fr->fr_family != AF_INET) {
					YYERROR;
				}
				if (!nowith)
					fr->fr_ip.fi_optmsk |= $1;)
			}
	;

optlist:
	opt				{ $$ |= $1; }
	| optlist ',' opt		{ $$ |= $1 | $3; }
	;

ipv6hdrs:
	ipv6hdrlist	{ DOALL(fr->fr_mip.fi_optmsk |= $1;
				if (!nowith)
					fr->fr_ip.fi_optmsk |= $1;)
			}
	;

ipv6hdrlist:
	ipv6hdr				{ $$ |= $1; }
	| ipv6hdrlist ',' ipv6hdr	{ $$ |= $1 | $3; }
	;

secname:
	seclevel			{ $$ |= $1; }
	| secname ',' seclevel		{ $$ |= $1 | $3; }
	;

seclevel:
	IPFY_SEC_UNC			{ $$ = secbit(IPSO_CLASS_UNCL); }
	| IPFY_SEC_CONF			{ $$ = secbit(IPSO_CLASS_CONF); }
	| IPFY_SEC_RSV1			{ $$ = secbit(IPSO_CLASS_RES1); }
	| IPFY_SEC_RSV2			{ $$ = secbit(IPSO_CLASS_RES2); }
	| IPFY_SEC_RSV3			{ $$ = secbit(IPSO_CLASS_RES3); }
	| IPFY_SEC_RSV4			{ $$ = secbit(IPSO_CLASS_RES4); }
	| IPFY_SEC_SEC			{ $$ = secbit(IPSO_CLASS_SECR); }
	| IPFY_SEC_TS			{ $$ = secbit(IPSO_CLASS_TOPS); }
	;

icmptype:
	YY_NUMBER		{ $$ = $1; }
	| YY_STR		{ $$ = geticmptype(frc->fr_family, $1);
				  if ($$ == -1)
					yyerror("unrecognised icmp type");
				}
	;

icmpcode:
	YY_NUMBER			{ $$ = $1; }
	| IPFY_ICMPC_NETUNR		{ $$ = ICMP_UNREACH_NET; }
	| IPFY_ICMPC_HSTUNR		{ $$ = ICMP_UNREACH_HOST; }
	| IPFY_ICMPC_PROUNR		{ $$ = ICMP_UNREACH_PROTOCOL; }
	| IPFY_ICMPC_PORUNR		{ $$ = ICMP_UNREACH_PORT; }
	| IPFY_ICMPC_NEEDF		{ $$ = ICMP_UNREACH_NEEDFRAG; }
	| IPFY_ICMPC_SRCFAIL		{ $$ = ICMP_UNREACH_SRCFAIL; }
	| IPFY_ICMPC_NETUNK		{ $$ = ICMP_UNREACH_NET_UNKNOWN; }
	| IPFY_ICMPC_HSTUNK		{ $$ = ICMP_UNREACH_HOST_UNKNOWN; }
	| IPFY_ICMPC_ISOLATE		{ $$ = ICMP_UNREACH_ISOLATED; }
	| IPFY_ICMPC_NETPRO		{ $$ = ICMP_UNREACH_NET_PROHIB; }
	| IPFY_ICMPC_HSTPRO		{ $$ = ICMP_UNREACH_HOST_PROHIB; }
	| IPFY_ICMPC_NETTOS		{ $$ = ICMP_UNREACH_TOSNET; }
	| IPFY_ICMPC_HSTTOS		{ $$ = ICMP_UNREACH_TOSHOST; }
	| IPFY_ICMPC_FLTPRO		{ $$ = ICMP_UNREACH_ADMIN_PROHIBIT; }
	| IPFY_ICMPC_HSTPRE		{ $$ = 14; }
	| IPFY_ICMPC_CUTPRE		{ $$ = 15; }
	;

opt:
	IPFY_IPOPT_NOP			{ $$ = getoptbyvalue(IPOPT_NOP); }
	| IPFY_IPOPT_RR			{ $$ = getoptbyvalue(IPOPT_RR); }
	| IPFY_IPOPT_ZSU		{ $$ = getoptbyvalue(IPOPT_ZSU); }
	| IPFY_IPOPT_MTUP		{ $$ = getoptbyvalue(IPOPT_MTUP); }
	| IPFY_IPOPT_MTUR		{ $$ = getoptbyvalue(IPOPT_MTUR); }
	| IPFY_IPOPT_ENCODE		{ $$ = getoptbyvalue(IPOPT_ENCODE); }
	| IPFY_IPOPT_TS			{ $$ = getoptbyvalue(IPOPT_TS); }
	| IPFY_IPOPT_TR			{ $$ = getoptbyvalue(IPOPT_TR); }
	| IPFY_IPOPT_SEC		{ $$ = getoptbyvalue(IPOPT_SECURITY); }
	| IPFY_IPOPT_LSRR		{ $$ = getoptbyvalue(IPOPT_LSRR); }
	| IPFY_IPOPT_ESEC		{ $$ = getoptbyvalue(IPOPT_E_SEC); }
	| IPFY_IPOPT_CIPSO 		{ $$ = getoptbyvalue(IPOPT_CIPSO); }
	| IPFY_IPOPT_CIPSO doi		{ $$ = getoptbyvalue(IPOPT_CIPSO); }
	| IPFY_IPOPT_SATID		{ $$ = getoptbyvalue(IPOPT_SATID); }
	| IPFY_IPOPT_SSRR		{ $$ = getoptbyvalue(IPOPT_SSRR); }
	| IPFY_IPOPT_ADDEXT		{ $$ = getoptbyvalue(IPOPT_ADDEXT); }
	| IPFY_IPOPT_VISA		{ $$ = getoptbyvalue(IPOPT_VISA); }
	| IPFY_IPOPT_IMITD		{ $$ = getoptbyvalue(IPOPT_IMITD); }
	| IPFY_IPOPT_EIP		{ $$ = getoptbyvalue(IPOPT_EIP); }
	| IPFY_IPOPT_FINN		{ $$ = getoptbyvalue(IPOPT_FINN); }
	| IPFY_IPOPT_DPS		{ $$ = getoptbyvalue(IPOPT_DPS); }
	| IPFY_IPOPT_SDB		{ $$ = getoptbyvalue(IPOPT_SDB); }
	| IPFY_IPOPT_NSAPA		{ $$ = getoptbyvalue(IPOPT_NSAPA); }
	| IPFY_IPOPT_RTRALRT		{ $$ = getoptbyvalue(IPOPT_RTRALRT); }
	| IPFY_IPOPT_UMP		{ $$ = getoptbyvalue(IPOPT_UMP); }
	| setsecclass secname
			{ DOALL(fr->fr_mip.fi_secmsk |= $2;
				if (fr->fr_family == AF_UNSPEC) {
					fr->fr_family = AF_INET;
					fr->fr_ip.fi_v = 4;
					fr->fr_mip.fi_v = 0xf;
				} else if (fr->fr_family != AF_INET) {
					YYERROR;
				}
				if (!nowith)
					fr->fr_ip.fi_secmsk |= $2;)
			  $$ = 0;
			  yyresetdict();
			}
	;

setsecclass:
	IPFY_SECCLASS			{ yysetdict(ipv4secwords); }
	;

doi:	IPFY_DOI YY_NUMBER		{ DOALL(fr->fr_doimask = 0xffffffff; \
						if (!nowith) \
							fr->fr_doi = $2;) }
	| IPFY_DOI YY_HEX		{ DOALL(fr->fr_doimask = 0xffffffff; \
						if (!nowith) \
							fr->fr_doi = $2;) }
	;

ipv6hdr:
	IPFY_AH			{ $$ = getv6optbyvalue(IPPROTO_AH); }
	| IPFY_IPV6OPT_DSTOPTS	{ $$ = getv6optbyvalue(IPPROTO_DSTOPTS); }
	| IPFY_IPV6OPT_ESP	{ $$ = getv6optbyvalue(IPPROTO_ESP); }
	| IPFY_IPV6OPT_HOPOPTS	{ $$ = getv6optbyvalue(IPPROTO_HOPOPTS); }
	| IPFY_IPV6OPT_IPV6	{ $$ = getv6optbyvalue(IPPROTO_IPV6); }
	| IPFY_IPV6OPT_NONE	{ $$ = getv6optbyvalue(IPPROTO_NONE); }
	| IPFY_IPV6OPT_ROUTING	{ $$ = getv6optbyvalue(IPPROTO_ROUTING); }
	| IPFY_IPV6OPT_FRAG	{ $$ = getv6optbyvalue(IPPROTO_FRAGMENT); }
	| IPFY_IPV6OPT_MOBILITY	{ $$ = getv6optbyvalue(IPPROTO_MOBILITY); }
	;

level:	IPFY_LEVEL			{ setsyslog(); }
	;

loglevel:
	priority			{ fr->fr_loglevel = LOG_LOCAL0|$1; }
	| facility '.' priority		{ fr->fr_loglevel = $1 | $3; }
	;

facility:
	IPFY_FAC_KERN			{ $$ = LOG_KERN; }
	| IPFY_FAC_USER			{ $$ = LOG_USER; }
	| IPFY_FAC_MAIL			{ $$ = LOG_MAIL; }
	| IPFY_FAC_DAEMON		{ $$ = LOG_DAEMON; }
	| IPFY_FAC_AUTH			{ $$ = LOG_AUTH; }
	| IPFY_FAC_SYSLOG		{ $$ = LOG_SYSLOG; }
	| IPFY_FAC_LPR			{ $$ = LOG_LPR; }
	| IPFY_FAC_NEWS			{ $$ = LOG_NEWS; }
	| IPFY_FAC_UUCP			{ $$ = LOG_UUCP; }
	| IPFY_FAC_CRON			{ $$ = LOG_CRON; }
	| IPFY_FAC_FTP			{ $$ = LOG_FTP; }
	| IPFY_FAC_AUTHPRIV		{ $$ = LOG_AUTHPRIV; }
	| IPFY_FAC_AUDIT		{ $$ = LOG_AUDIT; }
	| IPFY_FAC_LFMT			{ $$ = LOG_LFMT; }
	| IPFY_FAC_LOCAL0		{ $$ = LOG_LOCAL0; }
	| IPFY_FAC_LOCAL1		{ $$ = LOG_LOCAL1; }
	| IPFY_FAC_LOCAL2		{ $$ = LOG_LOCAL2; }
	| IPFY_FAC_LOCAL3		{ $$ = LOG_LOCAL3; }
	| IPFY_FAC_LOCAL4		{ $$ = LOG_LOCAL4; }
	| IPFY_FAC_LOCAL5		{ $$ = LOG_LOCAL5; }
	| IPFY_FAC_LOCAL6		{ $$ = LOG_LOCAL6; }
	| IPFY_FAC_LOCAL7		{ $$ = LOG_LOCAL7; }
	| IPFY_FAC_SECURITY		{ $$ = LOG_SECURITY; }
	;

priority:
	IPFY_PRI_EMERG			{ $$ = LOG_EMERG; }
	| IPFY_PRI_ALERT		{ $$ = LOG_ALERT; }
	| IPFY_PRI_CRIT			{ $$ = LOG_CRIT; }
	| IPFY_PRI_ERR			{ $$ = LOG_ERR; }
	| IPFY_PRI_WARN			{ $$ = LOG_WARNING; }
	| IPFY_PRI_NOTICE		{ $$ = LOG_NOTICE; }
	| IPFY_PRI_INFO			{ $$ = LOG_INFO; }
	| IPFY_PRI_DEBUG		{ $$ = LOG_DEBUG; }
	;

compare:
	YY_CMP_EQ			{ $$ = FR_EQUAL; }
	| YY_CMP_NE			{ $$ = FR_NEQUAL; }
	| YY_CMP_LT			{ $$ = FR_LESST; }
	| YY_CMP_LE			{ $$ = FR_LESSTE; }
	| YY_CMP_GT			{ $$ = FR_GREATERT; }
	| YY_CMP_GE			{ $$ = FR_GREATERTE; }
	;

range:	YY_RANGE_IN			{ $$ = FR_INRANGE; }
	| YY_RANGE_OUT			{ $$ = FR_OUTRANGE; }
	| ':'				{ $$ = FR_INCRANGE; }
	;

servicename:
	YY_STR				{ $$ = $1; }
	;

interfacename:	name				{ $$ = $1; }
	| name ':' YY_NUMBER
		{ $$ = $1;
		  fprintf(stderr, "%d: Logical interface %s:%d unsupported, "
			  "use the physical interface %s instead.\n",
			  yylineNum, $1, $3, $1);
		}
	;

name:	YY_STR				{ $$ = $1; }
	| '-'				{ $$ = strdup("-"); }
	;

ipv4_16:
	YY_NUMBER '.' YY_NUMBER
		{ if ($1 > 255 || $3 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  $$.s_addr = ($1 << 24) | ($3 << 16);
		  $$.s_addr = htonl($$.s_addr);
		}
	;

ipv4_24:
	ipv4_16 '.' YY_NUMBER
		{ if ($3 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  $$.s_addr |= htonl($3 << 8);
		}
	;

ipv4:	ipv4_24 '.' YY_NUMBER
		{ if ($3 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  $$.s_addr |= htonl($3);
		}
	| ipv4_24
	| ipv4_16
	;

%%


static	struct	wordtab ipfwords[] = {
	{ "age",			IPFY_AGE },
	{ "ah",				IPFY_AH },
	{ "all",			IPFY_ALL },
	{ "and",			IPFY_AND },
	{ "auth",			IPFY_AUTH },
	{ "bad",			IPFY_BAD },
	{ "bad-nat",			IPFY_BADNAT },
	{ "bad-src",			IPFY_BADSRC },
	{ "bcast",			IPFY_BROADCAST },
	{ "block",			IPFY_BLOCK },
	{ "body",			IPFY_BODY },
	{ "bpf-v4",			IPFY_BPFV4 },
#ifdef USE_INET6
	{ "bpf-v6",			IPFY_BPFV6 },
#endif
	{ "call",			IPFY_CALL },
	{ "code",			IPFY_ICMPCODE },
	{ "comment",			IPFY_COMMENT },
	{ "count",			IPFY_COUNT },
	{ "decapsulate",		IPFY_DECAPS },
	{ "dstlist",			IPFY_DSTLIST },
	{ "doi",			IPFY_DOI },
	{ "dup-to",			IPFY_DUPTO },
	{ "eq",				YY_CMP_EQ },
	{ "esp",			IPFY_ESP },
	{ "exp",			IPFY_IPFEXPR },
	{ "family",			IPFY_FAMILY },
	{ "fastroute",			IPFY_FROUTE },
	{ "first",			IPFY_FIRST },
	{ "flags",			IPFY_FLAGS },
	{ "frag",			IPFY_FRAG },
	{ "frag-body",			IPFY_FRAGBODY },
	{ "frags",			IPFY_FRAGS },
	{ "from",			IPFY_FROM },
	{ "ge",				YY_CMP_GE },
	{ "group",			IPFY_GROUP },
	{ "gt",				YY_CMP_GT },
	{ "head",			IPFY_HEAD },
	{ "icmp",			IPFY_ICMP },
	{ "icmp-head",			IPFY_ICMPHEAD },
	{ "icmp-type",			IPFY_ICMPTYPE },
	{ "in",				IPFY_IN },
	{ "in-via",			IPFY_INVIA },
	{ "inet",			IPFY_INET },
	{ "inet6",			IPFY_INET6 },
	{ "ipopt",			IPFY_IPOPTS },
	{ "ipopts",			IPFY_IPOPTS },
	{ "keep",			IPFY_KEEP },
	{ "l5-as",			IPFY_L5AS },
	{ "le",				YY_CMP_LE },
	{ "level",			IPFY_LEVEL },
	{ "limit",			IPFY_LIMIT },
	{ "log",			IPFY_LOG },
	{ "loose",			IPFY_LOOSE },
	{ "lowttl",			IPFY_LOWTTL },
	{ "lt",				YY_CMP_LT },
	{ "mask",			IPFY_MASK },
	{ "match-tag",			IPFY_MATCHTAG },
	{ "max-per-src",		IPFY_MAX_PER_SRC },
	{ "max-srcs",			IPFY_MAX_SRCS },
	{ "mbcast",			IPFY_MBCAST },
	{ "mcast",			IPFY_MULTICAST },
	{ "multicast",			IPFY_MULTICAST },
	{ "nat",			IPFY_NAT },
	{ "ne",				YY_CMP_NE },
	{ "net",			IPFY_NETWORK },
	{ "newisn",			IPFY_NEWISN },
	{ "no",				IPFY_NO },
	{ "no-icmp-err",		IPFY_NOICMPERR },
	{ "nolog",			IPFY_NOLOG },
	{ "nomatch",			IPFY_NOMATCH },
	{ "now",			IPFY_NOW },
	{ "not",			IPFY_NOT },
	{ "oow",			IPFY_OOW },
	{ "on",				IPFY_ON },
	{ "opt",			IPFY_OPT },
	{ "or-block",			IPFY_ORBLOCK },
	{ "out",			IPFY_OUT },
	{ "out-via",			IPFY_OUTVIA },
	{ "pass",			IPFY_PASS },
	{ "port",			IPFY_PORT },
	{ "pps",			IPFY_PPS },
	{ "preauth",			IPFY_PREAUTH },
	{ "proto",			IPFY_PROTO },
	{ "quick",			IPFY_QUICK },
	{ "reply-to",			IPFY_REPLY_TO },
	{ "return-icmp",		IPFY_RETICMP },
	{ "return-icmp-as-dest",	IPFY_RETICMPASDST },
	{ "return-rst",			IPFY_RETRST },
	{ "route-to",			IPFY_ROUTETO },
	{ "rule-ttl",			IPFY_RULETTL },
	{ "rpc",			IPFY_RPC },
	{ "sec-class",			IPFY_SECCLASS },
	{ "set",			IPFY_SET },
	{ "set-tag",			IPFY_SETTAG },
	{ "skip",			IPFY_SKIP },
	{ "short",			IPFY_SHORT },
	{ "state",			IPFY_STATE },
	{ "state-age",			IPFY_AGE },
	{ "strict",			IPFY_STRICT },
	{ "sync",			IPFY_SYNC },
	{ "tcp",			IPFY_TCP },
	{ "tcp-udp",			IPFY_TCPUDP },
	{ "tos",			IPFY_TOS },
	{ "to",				IPFY_TO },
	{ "ttl",			IPFY_TTL },
	{ "udp",			IPFY_UDP },
	{ "v6hdr",			IPFY_V6HDR },
	{ "v6hdrs",			IPFY_V6HDRS },
	{ "with",			IPFY_WITH },
	{ NULL,				0 }
};

static	struct	wordtab	addrwords[] = {
	{ "any",			IPFY_ANY },
	{ "hash",			IPFY_HASH },
	{ "pool",			IPFY_POOL },
	{ NULL,				0 }
};

static	struct	wordtab	maskwords[] = {
	{ "broadcast",			IPFY_BROADCAST },
	{ "netmasked",			IPFY_NETMASKED },
	{ "network",			IPFY_NETWORK },
	{ "peer",			IPFY_PEER },
	{ NULL,				0 }
};

static	struct	wordtab icmpcodewords[] = {
	{ "cutoff-preced",		IPFY_ICMPC_CUTPRE },
	{ "filter-prohib",		IPFY_ICMPC_FLTPRO },
	{ "isolate",			IPFY_ICMPC_ISOLATE },
	{ "needfrag",			IPFY_ICMPC_NEEDF },
	{ "net-prohib",			IPFY_ICMPC_NETPRO },
	{ "net-tos",			IPFY_ICMPC_NETTOS },
	{ "host-preced",		IPFY_ICMPC_HSTPRE },
	{ "host-prohib",		IPFY_ICMPC_HSTPRO },
	{ "host-tos",			IPFY_ICMPC_HSTTOS },
	{ "host-unk",			IPFY_ICMPC_HSTUNK },
	{ "host-unr",			IPFY_ICMPC_HSTUNR },
	{ "net-unk",			IPFY_ICMPC_NETUNK },
	{ "net-unr",			IPFY_ICMPC_NETUNR },
	{ "port-unr",			IPFY_ICMPC_PORUNR },
	{ "proto-unr",			IPFY_ICMPC_PROUNR },
	{ "srcfail",			IPFY_ICMPC_SRCFAIL },
	{ NULL,				0 },
};

static	struct	wordtab ipv4optwords[] = {
	{ "addext",			IPFY_IPOPT_ADDEXT },
	{ "cipso",			IPFY_IPOPT_CIPSO },
	{ "dps",			IPFY_IPOPT_DPS },
	{ "e-sec",			IPFY_IPOPT_ESEC },
	{ "eip",			IPFY_IPOPT_EIP },
	{ "encode",			IPFY_IPOPT_ENCODE },
	{ "finn",			IPFY_IPOPT_FINN },
	{ "imitd",			IPFY_IPOPT_IMITD },
	{ "lsrr",			IPFY_IPOPT_LSRR },
	{ "mtup",			IPFY_IPOPT_MTUP },
	{ "mtur",			IPFY_IPOPT_MTUR },
	{ "nop",			IPFY_IPOPT_NOP },
	{ "nsapa",			IPFY_IPOPT_NSAPA },
	{ "rr",				IPFY_IPOPT_RR },
	{ "rtralrt",			IPFY_IPOPT_RTRALRT },
	{ "satid",			IPFY_IPOPT_SATID },
	{ "sdb",			IPFY_IPOPT_SDB },
	{ "sec",			IPFY_IPOPT_SEC },
	{ "ssrr",			IPFY_IPOPT_SSRR },
	{ "tr",				IPFY_IPOPT_TR },
	{ "ts",				IPFY_IPOPT_TS },
	{ "ump",			IPFY_IPOPT_UMP },
	{ "visa",			IPFY_IPOPT_VISA },
	{ "zsu",			IPFY_IPOPT_ZSU },
	{ NULL,				0 },
};

static	struct	wordtab ipv4secwords[] = {
	{ "confid",			IPFY_SEC_CONF },
	{ "reserv-1",			IPFY_SEC_RSV1 },
	{ "reserv-2",			IPFY_SEC_RSV2 },
	{ "reserv-3",			IPFY_SEC_RSV3 },
	{ "reserv-4",			IPFY_SEC_RSV4 },
	{ "secret",			IPFY_SEC_SEC },
	{ "topsecret",			IPFY_SEC_TS },
	{ "unclass",			IPFY_SEC_UNC },
	{ NULL,				0 },
};

static	struct	wordtab ipv6optwords[] = {
	{ "dstopts",			IPFY_IPV6OPT_DSTOPTS },
	{ "esp",			IPFY_IPV6OPT_ESP },
	{ "frag",			IPFY_IPV6OPT_FRAG },
	{ "hopopts",			IPFY_IPV6OPT_HOPOPTS },
	{ "ipv6",			IPFY_IPV6OPT_IPV6 },
	{ "mobility",			IPFY_IPV6OPT_MOBILITY },
	{ "none",			IPFY_IPV6OPT_NONE },
	{ "routing",			IPFY_IPV6OPT_ROUTING },
	{ NULL,				0 },
};

static	struct	wordtab logwords[] = {
	{ "kern",			IPFY_FAC_KERN },
	{ "user",			IPFY_FAC_USER },
	{ "mail",			IPFY_FAC_MAIL },
	{ "daemon",			IPFY_FAC_DAEMON },
	{ "auth",			IPFY_FAC_AUTH },
	{ "syslog",			IPFY_FAC_SYSLOG },
	{ "lpr",			IPFY_FAC_LPR },
	{ "news",			IPFY_FAC_NEWS },
	{ "uucp",			IPFY_FAC_UUCP },
	{ "cron",			IPFY_FAC_CRON },
	{ "ftp",			IPFY_FAC_FTP },
	{ "authpriv",			IPFY_FAC_AUTHPRIV },
	{ "audit",			IPFY_FAC_AUDIT },
	{ "logalert",			IPFY_FAC_LFMT },
	{ "console",			IPFY_FAC_CONSOLE },
	{ "security",			IPFY_FAC_SECURITY },
	{ "local0",			IPFY_FAC_LOCAL0 },
	{ "local1",			IPFY_FAC_LOCAL1 },
	{ "local2",			IPFY_FAC_LOCAL2 },
	{ "local3",			IPFY_FAC_LOCAL3 },
	{ "local4",			IPFY_FAC_LOCAL4 },
	{ "local5",			IPFY_FAC_LOCAL5 },
	{ "local6",			IPFY_FAC_LOCAL6 },
	{ "local7",			IPFY_FAC_LOCAL7 },
	{ "emerg",			IPFY_PRI_EMERG },
	{ "alert",			IPFY_PRI_ALERT },
	{ "crit",			IPFY_PRI_CRIT },
	{ "err",			IPFY_PRI_ERR },
	{ "warn",			IPFY_PRI_WARN },
	{ "notice",			IPFY_PRI_NOTICE },
	{ "info",			IPFY_PRI_INFO },
	{ "debug",			IPFY_PRI_DEBUG },
	{ NULL,				0 },
};




int ipf_parsefile(fd, addfunc, iocfuncs, filename)
int fd;
addfunc_t addfunc;
ioctlfunc_t *iocfuncs;
char *filename;
{
	FILE *fp = NULL;
	char *s;

	yylineNum = 1;
	yysettab(ipfwords);

	s = getenv("YYDEBUG");
	if (s != NULL)
		yydebug = atoi(s);
	else
		yydebug = 0;

	if (strcmp(filename, "-")) {
		fp = fopen(filename, "r");
		if (fp == NULL) {
			fprintf(stderr, "fopen(%s) failed: %s\n", filename,
				STRERROR(errno));
			return -1;
		}
	} else
		fp = stdin;

	while (ipf_parsesome(fd, addfunc, iocfuncs, fp) == 1)
		;
	if (fp != NULL)
		fclose(fp);
	return 0;
}


int ipf_parsesome(fd, addfunc, iocfuncs, fp)
int fd;
addfunc_t addfunc;
ioctlfunc_t *iocfuncs;
FILE *fp;
{
	char *s;
	int i;

	ipffd = fd;
	for (i = 0; i <= IPL_LOGMAX; i++)
		ipfioctls[i] = iocfuncs[i];
	ipfaddfunc = addfunc;

	if (feof(fp))
		return 0;
	i = fgetc(fp);
	if (i == EOF)
		return 0;
	if (ungetc(i, fp) == 0)
		return 0;
	if (feof(fp))
		return 0;
	s = getenv("YYDEBUG");
	if (s != NULL)
		yydebug = atoi(s);
	else
		yydebug = 0;

	yyin = fp;
	yyparse();
	return 1;
}


static void newrule()
{
	frentry_t *frn;

	frn = allocfr();
	for (fr = frtop; fr != NULL && fr->fr_next != NULL; fr = fr->fr_next)
		;
	if (fr != NULL) {
		fr->fr_next = frn;
		frn->fr_pnext = &fr->fr_next;
	}
	if (frtop == NULL) {
		frtop = frn;
		frn->fr_pnext = &frtop;
	}
	fr = frn;
	frc = frn;
	fr->fr_loglevel = 0xffff;
	fr->fr_isc = (void *)-1;
	fr->fr_logtag = FR_NOLOGTAG;
	fr->fr_type = FR_T_NONE;
	fr->fr_flineno = yylineNum;

	if (use_inet6 == 1)
		fr->fr_family = AF_INET6;
	else if (use_inet6 == -1)
		fr->fr_family = AF_INET;

	nrules = 1;
}


static void setipftype()
{
	for (fr = frc; fr != NULL; fr = fr->fr_next) {
		if (fr->fr_type == FR_T_NONE) {
			fr->fr_type = FR_T_IPF;
			fr->fr_data = (void *)calloc(sizeof(fripf_t), 1);
			fr->fr_dsize = sizeof(fripf_t);
			fr->fr_family = frc->fr_family;
			if (fr->fr_family == AF_INET) {
				fr->fr_ip.fi_v = 4;
			}
			else if (fr->fr_family == AF_INET6) {
				fr->fr_ip.fi_v = 6;
			}
			fr->fr_mip.fi_v = 0xf;
			fr->fr_ipf->fri_sifpidx = -1;
			fr->fr_ipf->fri_difpidx = -1;
		}
		if (fr->fr_type != FR_T_IPF) {
			fprintf(stderr, "IPF Type not set\n");
		}
	}
}


static frentry_t *addrule()
{
	frentry_t *f, *f1, *f2;
	int count;

	for (f2 = frc; f2->fr_next != NULL; f2 = f2->fr_next)
		;

	count = nrules;
	f = f2;
	for (f1 = frc; count > 0; count--, f1 = f1->fr_next) {
		f->fr_next = allocfr();
		if (f->fr_next == NULL)
			return NULL;
		f->fr_next->fr_pnext = &f->fr_next;
		added++;
		f = f->fr_next;
		*f = *f1;
		f->fr_next = NULL;
		if (f->fr_caddr != NULL) {
			f->fr_caddr = malloc(f->fr_dsize);
			bcopy(f1->fr_caddr, f->fr_caddr, f->fr_dsize);
		}
	}

	return f2->fr_next;
}


static int
lookuphost(name, addrp)
	char *name;
	i6addr_t *addrp;
{
	int i;

	hashed = 0;
	pooled = 0;
	dynamic = -1;

	for (i = 0; i < 4; i++) {
		if (fr->fr_ifnames[i] == -1)
			continue;
		if (strcmp(name, fr->fr_names + fr->fr_ifnames[i]) == 0) {
			ifpflag = FRI_DYNAMIC;
			dynamic = addname(&fr, name);
			return 1;
		}
	}

	if (gethost(AF_INET, name, addrp) == -1) {
		fprintf(stderr, "unknown name \"%s\"\n", name);
		return -1;
	}
	return 0;
}


static void dobpf(v, phrase)
int v;
char *phrase;
{
#ifdef IPFILTER_BPF
	struct bpf_program bpf;
	struct pcap *p;
#endif
	fakebpf_t *fb;
	u_32_t l;
	char *s;
	int i;

	for (fr = frc; fr != NULL; fr = fr->fr_next) {
		if (fr->fr_type != FR_T_NONE) {
			fprintf(stderr, "cannot mix IPF and BPF matching\n");
			return;
		}
		fr->fr_family = vtof(v);
		fr->fr_type = FR_T_BPFOPC;

		if (!strncmp(phrase, "0x", 2)) {
			fb = malloc(sizeof(fakebpf_t));

			for (i = 0, s = strtok(phrase, " \r\n\t"); s != NULL;
			     s = strtok(NULL, " \r\n\t"), i++) {
				fb = reallocarray(fb, i / 4 + 1, sizeof(*fb));
				if (fb == NULL) {
					warnx("memory allocation error at %d in %s in %s", __LINE__, __FUNCTION__, __FILE__);
					abort();
				}
				l = (u_32_t)strtol(s, NULL, 0);
				switch (i & 3)
				{
				case 0 :
					fb[i / 4].fb_c = l & 0xffff;
					break;
				case 1 :
					fb[i / 4].fb_t = l & 0xff;
					break;
				case 2 :
					fb[i / 4].fb_f = l & 0xff;
					break;
				case 3 :
					fb[i / 4].fb_k = l;
					break;
				}
			}
			if ((i & 3) != 0) {
				fprintf(stderr,
					"Odd number of bytes in BPF code\n");
				exit(1);
			}
			i--;
			fr->fr_dsize = (i / 4 + 1) * sizeof(*fb);
			fr->fr_data = fb;
			return;
		}

#ifdef IPFILTER_BPF
		bzero((char *)&bpf, sizeof(bpf));
		p = pcap_open_dead(DLT_RAW, 1);
		if (!p) {
			fprintf(stderr, "pcap_open_dead failed\n");
			return;
		}

		if (pcap_compile(p, &bpf, phrase, 1, 0xffffffff)) {
			pcap_perror(p, "ipf");
			pcap_close(p);
			fprintf(stderr, "pcap parsing failed (%s)\n", phrase);
			return;
		}
		pcap_close(p);

		fr->fr_dsize = bpf.bf_len * sizeof(struct bpf_insn);
		fr->fr_data = malloc(fr->fr_dsize);
		bcopy((char *)bpf.bf_insns, fr->fr_data, fr->fr_dsize);
		if (!bpf_validate(fr->fr_data, bpf.bf_len)) {
			fprintf(stderr, "BPF validation failed\n");
			return;
		}
#endif
	}

#ifdef IPFILTER_BPF
	if (opts & OPT_DEBUG)
		bpf_dump(&bpf, 0);
#else
	fprintf(stderr, "BPF filter expressions not supported\n");
	exit(1);
#endif
}


static void resetaddr()
{
	hashed = 0;
	pooled = 0;
	dynamic = -1;
}


static alist_t *newalist(ptr)
alist_t *ptr;
{
	alist_t *al;

	al = malloc(sizeof(*al));
	if (al == NULL)
		return NULL;
	al->al_not = 0;
	al->al_next = ptr;
	return al;
}


static int
makepool(list)
	alist_t *list;
{
	ip_pool_node_t *n, *top;
	ip_pool_t pool;
	alist_t *a;
	int num;

	if (list == NULL)
		return 0;
	top = calloc(1, sizeof(*top));
	if (top == NULL)
		return 0;

	for (n = top, a = list; (n != NULL) && (a != NULL); a = a->al_next) {
		if (use_inet6 == 1) {
#ifdef USE_INET6
			n->ipn_addr.adf_family = AF_INET6;
			n->ipn_addr.adf_addr = a->al_i6addr;
			n->ipn_addr.adf_len = offsetof(addrfamily_t,
						       adf_addr) + 16;
			n->ipn_mask.adf_family = AF_INET6;
			n->ipn_mask.adf_addr = a->al_i6mask;
			n->ipn_mask.adf_len = offsetof(addrfamily_t,
						       adf_addr) + 16;

#endif
		} else {
			n->ipn_addr.adf_family = AF_INET;
			n->ipn_addr.adf_addr.in4.s_addr = a->al_1;
			n->ipn_addr.adf_len = offsetof(addrfamily_t,
						       adf_addr) + 4;
			n->ipn_mask.adf_family = AF_INET;
			n->ipn_mask.adf_addr.in4.s_addr = a->al_2;
			n->ipn_mask.adf_len = offsetof(addrfamily_t,
						       adf_addr) + 4;
		}
		n->ipn_info = a->al_not;
		if (a->al_next != NULL) {
			n->ipn_next = calloc(1, sizeof(*n));
			n = n->ipn_next;
		}
	}

	bzero((char *)&pool, sizeof(pool));
	pool.ipo_unit = IPL_LOGIPF;
	pool.ipo_list = top;
	num = load_pool(&pool, ipfioctls[IPL_LOGLOOKUP]);

	while ((n = top) != NULL) {
		top = n->ipn_next;
		free(n);
	}
	return num;
}


static u_int makehash(list)
alist_t *list;
{
	iphtent_t *n, *top;
	iphtable_t iph;
	alist_t *a;
	int num;

	if (list == NULL)
		return 0;
	top = calloc(1, sizeof(*top));
	if (top == NULL)
		return 0;

	for (n = top, a = list; (n != NULL) && (a != NULL); a = a->al_next) {
		if (a->al_family == AF_INET6) {
			n->ipe_family = AF_INET6;
			n->ipe_addr = a->al_i6addr;
			n->ipe_mask = a->al_i6mask;
		} else {
			n->ipe_family = AF_INET;
			n->ipe_addr.in4_addr = a->al_1;
			n->ipe_mask.in4_addr = a->al_2;
		}
		n->ipe_value = 0;
		if (a->al_next != NULL) {
			n->ipe_next = calloc(1, sizeof(*n));
			n = n->ipe_next;
		}
	}

	bzero((char *)&iph, sizeof(iph));
	iph.iph_unit = IPL_LOGIPF;
	iph.iph_type = IPHASH_LOOKUP;
	*iph.iph_name = '\0';

	if (load_hash(&iph, top, ipfioctls[IPL_LOGLOOKUP]) == 0)
		sscanf(iph.iph_name, "%u", &num);
	else
		num = 0;

	while ((n = top) != NULL) {
		top = n->ipe_next;
		free(n);
	}
	return num;
}


int ipf_addrule(fd, ioctlfunc, ptr)
int fd;
ioctlfunc_t ioctlfunc;
void *ptr;
{
	ioctlcmd_t add, del;
	frentry_t *fr;
	ipfobj_t obj;

	if (ptr == NULL)
		return 0;

	fr = ptr;
	add = 0;
	del = 0;

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = fr->fr_size;
	obj.ipfo_type = IPFOBJ_FRENTRY;
	obj.ipfo_ptr = ptr;

	if ((opts & OPT_DONOTHING) != 0)
		fd = -1;

	if (opts & OPT_ZERORULEST) {
		add = SIOCZRLST;
	} else if (opts & OPT_INACTIVE) {
		add = (u_int)fr->fr_hits ? SIOCINIFR :
					   SIOCADIFR;
		del = SIOCRMIFR;
	} else {
		add = (u_int)fr->fr_hits ? SIOCINAFR :
					   SIOCADAFR;
		del = SIOCRMAFR;
	}

	if ((opts & OPT_OUTQUE) != 0)
		fr->fr_flags |= FR_OUTQUE;
	if (fr->fr_hits)
		fr->fr_hits--;
	if ((opts & OPT_VERBOSE) != 0)
		printfr(fr, ioctlfunc);

	if ((opts & OPT_DEBUG) != 0) {
		binprint(fr, sizeof(*fr));
		if (fr->fr_data != NULL)
			binprint(fr->fr_data, fr->fr_dsize);
	}

	if ((opts & OPT_ZERORULEST) != 0) {
		if ((*ioctlfunc)(fd, add, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(zero rule)",
					fr->fr_flineno);
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		} else {
#ifdef	USE_QUAD_T
			printf("hits %qd bytes %qd ",
				(long long)fr->fr_hits,
				(long long)fr->fr_bytes);
#else
			printf("hits %ld bytes %ld ",
				fr->fr_hits, fr->fr_bytes);
#endif
			printfr(fr, ioctlfunc);
		}
	} else if ((opts & OPT_REMOVE) != 0) {
		if ((*ioctlfunc)(fd, del, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(delete rule)",
					fr->fr_flineno);
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		}
	} else {
		if ((*ioctlfunc)(fd, add, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(add/insert rule)",
					fr->fr_flineno);
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		}
	}
	return 0;
}

static void setsyslog()
{
	yysetdict(logwords);
	yybreakondot = 1;
}


static void unsetsyslog()
{
	yyresetdict();
	yybreakondot = 0;
}


static void fillgroup(fr)
frentry_t *fr;
{
	frentry_t *f;

	for (f = frold; f != NULL; f = f->fr_next) {
		if (f->fr_grhead == -1 && fr->fr_group == -1)
			break;
		if (f->fr_grhead == -1 || fr->fr_group == -1)
			continue;
		if (strcmp(f->fr_names + f->fr_grhead,
			   fr->fr_names + fr->fr_group) == 0)
			break;
	}

	if (f == NULL)
		return;

	/*
	 * Only copy down matching fields if the rules are of the same type
	 * and are of ipf type.   The only fields that are copied are those
	 * that impact the rule parsing itself, eg. need for knowing what the
	 * protocol should be for rules with port comparisons in them.
	 */
	if (f->fr_type != fr->fr_type || f->fr_type != FR_T_IPF)
		return;

	if (fr->fr_family == 0 && f->fr_family != 0)
		fr->fr_family = f->fr_family;

	if (fr->fr_mproto == 0 && f->fr_mproto != 0)
		fr->fr_mproto = f->fr_mproto;
	if (fr->fr_proto == 0 && f->fr_proto != 0)
		fr->fr_proto = f->fr_proto;

	if ((fr->fr_mproto == 0) && ((fr->fr_flx & FI_TCPUDP) == 0) &&
	    ((f->fr_flx & FI_TCPUDP) != 0)) {
		fr->fr_flx |= FI_TCPUDP;
		fr->fr_mflx |= FI_TCPUDP;
	}
}


static void doipfexpr(line)
char *line;
{
	int *array;
	char *error;

	array = parseipfexpr(line, &error);
	if (array == NULL) {
		fprintf(stderr, "%s:", error);
		yyerror("error parsing ipf matching expression");
		return;
	}

	fr->fr_type = FR_T_IPFEXPR;
	fr->fr_data = array;
	fr->fr_dsize = array[0] * sizeof(*array);
}


static void do_tuneint(varname, value)
char *varname;
int value;
{
	char buffer[80];

	strncpy(buffer, varname, 60);
	buffer[59] = '\0';
	strcat(buffer, "=");
	sprintf(buffer, "%u", value);
	ipf_dotuning(ipffd, buffer, ioctl);
}


static void do_tunestr(varname, value)
char *varname, *value;
{

	if (!strcasecmp(value, "true")) {
		do_tuneint(varname, 1);
	} else if (!strcasecmp(value, "false")) {
		do_tuneint(varname, 0);
	} else {
		yyerror("did not find true/false where expected");
	}
}


static void setifname(frp, idx, name)
frentry_t **frp;
int idx;
char *name;
{
	int pos;

	pos = addname(frp, name);
	if (pos == -1)
		return;
	(*frp)->fr_ifnames[idx] = pos;
}


static int addname(frp, name)
frentry_t **frp;
char *name;
{
	frentry_t *f;
	int nlen;
	int pos;

	nlen = strlen(name) + 1;
	f = realloc(*frp, (*frp)->fr_size + nlen);
	if (*frp == frc)
		frc = f;
	*frp = f;
	if (f == NULL)
		return -1;
	if (f->fr_pnext != NULL)
		*f->fr_pnext = f;
	f->fr_size += nlen;
	pos = f->fr_namelen;
	f->fr_namelen += nlen;
	strcpy(f->fr_names + pos, name);
	f->fr_names[f->fr_namelen] = '\0';
	return pos;
}


static frentry_t *allocfr()
{
	frentry_t *fr;

	fr = calloc(1, sizeof(*fr));
	if (fr != NULL) {
		fr->fr_size = sizeof(*fr);
		fr->fr_comment = -1;
		fr->fr_group = -1;
		fr->fr_grhead = -1;
		fr->fr_icmphead = -1;
		fr->fr_ifnames[0] = -1;
		fr->fr_ifnames[1] = -1;
		fr->fr_ifnames[2] = -1;
		fr->fr_ifnames[3] = -1;
		fr->fr_tif.fd_name = -1;
		fr->fr_rif.fd_name = -1;
		fr->fr_dif.fd_name = -1;
	}
	return fr;
}


static void setgroup(frp, name)
frentry_t **frp;
char *name;
{
	int pos;

	pos = addname(frp, name);
	if (pos == -1)
		return;
	(*frp)->fr_group = pos;
}


static void setgrhead(frp, name)
frentry_t **frp;
char *name;
{
	int pos;

	pos = addname(frp, name);
	if (pos == -1)
		return;
	(*frp)->fr_grhead = pos;
}


static void seticmphead(frp, name)
frentry_t **frp;
char *name;
{
	int pos;

	pos = addname(frp, name);
	if (pos == -1)
		return;
	(*frp)->fr_icmphead = pos;
}


static void
build_dstaddr_af(fp, ptr)
	frentry_t *fp;
	void *ptr;
{
	struct ipp_s *ipp = ptr;
	frentry_t *f = fp;

	if (f->fr_family != AF_UNSPEC && ipp->f == AF_UNSPEC) {
		ipp->f = f->fr_family;
		ipp->v = f->fr_ip.fi_v;
	}
	if (ipp->f == AF_INET)
		ipp->v = 4;
	else if (ipp->f == AF_INET6)
		ipp->v = 6;

	for (; f != NULL; f = f->fr_next) {
		f->fr_ip.fi_dst = ipp->a;
		f->fr_mip.fi_dst = ipp->m;
		f->fr_family = ipp->f;
		f->fr_ip.fi_v = ipp->v;
		f->fr_mip.fi_v = 0xf;
		f->fr_datype = ipp->type;
		if (ipp->ifpos != -1)
			f->fr_ipf->fri_difpidx = ipp->ifpos;
	}
	fr = NULL;
}


static void
build_srcaddr_af(fp, ptr)
	frentry_t *fp;
	void *ptr;
{
	struct ipp_s *ipp = ptr;
	frentry_t *f = fp;

	if (f->fr_family != AF_UNSPEC && ipp->f == AF_UNSPEC) {
		ipp->f = f->fr_family;
		ipp->v = f->fr_ip.fi_v;
	}
	if (ipp->f == AF_INET)
		ipp->v = 4;
	else if (ipp->f == AF_INET6)
		ipp->v = 6;

	for (; f != NULL; f = f->fr_next) {
		f->fr_ip.fi_src = ipp->a;
		f->fr_mip.fi_src = ipp->m;
		f->fr_family = ipp->f;
		f->fr_ip.fi_v = ipp->v;
		f->fr_mip.fi_v = 0xf;
		f->fr_satype = ipp->type;
		f->fr_ipf->fri_sifpidx = ipp->ifpos;
	}
	fr = NULL;
}
