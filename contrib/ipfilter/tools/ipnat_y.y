/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
%{
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#if !defined(__SVR4) && !defined(__GNUC__)
#include <strings.h>
#endif
#include <sys/types.h>
#include <sys/param.h>
#include <sys/file.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <sys/time.h>
#include <syslog.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include "ipf.h"
#include "netinet/ipl.h"
#include "ipnat_l.h"

#define	YYDEBUG	1

extern	void	yyerror __P((char *));
extern	int	yyparse __P((void));
extern	int	yylex __P((void));
extern	int	yydebug;
extern	FILE	*yyin;
extern	int	yylineNum;

static	ipnat_t		*nattop = NULL;
static	ipnat_t		*nat = NULL;
static	int		natfd = -1;
static	ioctlfunc_t	natioctlfunc = NULL;
static	addfunc_t	nataddfunc = NULL;
static	int		suggest_port = 0;
static	proxyrule_t	*prules = NULL;
static	int		parser_error = 0;

static	void	newnatrule __P((void));
static	void	setnatproto __P((int));
static	void	setmapifnames __P((void));
static	void	setrdrifnames __P((void));
static	void	proxy_setconfig __P((int));
static	void	proxy_unsetconfig __P((void));
static	namelist_t *proxy_dns_add_pass __P((char *, char *));
static	namelist_t *proxy_dns_add_block __P((char *, char *));
static	void	proxy_addconfig __P((char *, int, char *, namelist_t *));
static	void	proxy_loadconfig __P((int, ioctlfunc_t, char *, int,
				      char *, namelist_t *));
static	void	proxy_loadrules __P((int, ioctlfunc_t, proxyrule_t *));
static	void	setmapifnames __P((void));
static	void	setrdrifnames __P((void));
static	void	setifname __P((ipnat_t **, int, char *));
static	int	addname __P((ipnat_t **, char *));
%}
%union	{
	char	*str;
	u_32_t	num;
	struct {
		i6addr_t	a;
		int		f;
	} ipa;
	frentry_t	fr;
	frtuc_t	*frt;
	u_short	port;
	struct	{
		int	p1;
		int	p2;
		int	pc;
	} pc;
	struct	{
		i6addr_t	a;
		i6addr_t	m;
		int	t;		/* Address type */
		int	u;
		int	f;		/* Family */
		int	v;		/* IP version */
		int	s;		/* 0 = number, 1 = text */
		int	n;		/* number */
	} ipp;
	union	i6addr	ip6;
	namelist_t	*names;
};

%token  <num>   YY_NUMBER YY_HEX
%token  <str>   YY_STR
%token	  YY_COMMENT
%token	  YY_CMP_EQ YY_CMP_NE YY_CMP_LE YY_CMP_GE YY_CMP_LT YY_CMP_GT
%token	  YY_RANGE_OUT YY_RANGE_IN
%token  <ip6>   YY_IPV6

%token	IPNY_MAPBLOCK IPNY_RDR IPNY_PORT IPNY_PORTS IPNY_AUTO IPNY_RANGE
%token	IPNY_MAP IPNY_BIMAP IPNY_FROM IPNY_TO IPNY_MASK IPNY_PORTMAP IPNY_ANY
%token	IPNY_ROUNDROBIN IPNY_FRAG IPNY_AGE IPNY_ICMPIDMAP IPNY_PROXY
%token	IPNY_TCP IPNY_UDP IPNY_TCPUDP IPNY_STICKY IPNY_MSSCLAMP IPNY_TAG
%token	IPNY_TLATE IPNY_POOL IPNY_HASH IPNY_NO IPNY_REWRITE IPNY_PROTO
%token	IPNY_ON IPNY_SRC IPNY_DST IPNY_IN IPNY_OUT IPNY_DIVERT
%token	IPNY_CONFIG IPNY_ALLOW IPNY_DENY IPNY_DNS IPNY_INET IPNY_INET6
%token	IPNY_SEQUENTIAL IPNY_DSTLIST IPNY_PURGE
%type	<port> portspec
%type	<num> hexnumber compare range proto
%type	<num> saddr daddr sobject dobject mapfrom rdrfrom dip
%type	<ipa> hostname ipv4 ipaddr
%type	<ipp> addr rhsaddr rhdaddr erhdaddr
%type	<pc> portstuff portpair comaports srcports dstports
%type	<names> dnslines dnsline
%%
file:	line
	| assign
	| file line
	| file assign
	| file pconf ';'
	;

line:	xx rule		{ int err;
			  while ((nat = nattop) != NULL) {
				if (nat->in_v[0] == 0)
					nat->in_v[0] = 4;
				if (nat->in_v[1] == 0)
					nat->in_v[1] = nat->in_v[0];
				nattop = nat->in_next;
				err = (*nataddfunc)(natfd, natioctlfunc, nat);
				free(nat);
				if (err != 0) {
					parser_error = err;
					break;
				}
			  }
			  if (parser_error == 0 && prules != NULL) {
				proxy_loadrules(natfd, natioctlfunc, prules);
				prules = NULL;
			  }
			  resetlexer();
			}
	| YY_COMMENT
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

xx:					{ newnatrule(); }
	;

rule:	map eol
	| mapblock eol
	| redir eol
	| rewrite ';'
	| divert ';'
	;

no:	IPNY_NO				{ nat->in_flags |= IPN_NO; }
	;

eol:	| ';'
	;

map:	mapit ifnames addr tlate rhsaddr proxy mapoptions
				{ if ($3.f != 0 && $3.f != $5.f && $5.f != 0)
					yyerror("3.address family mismatch");
				  if (nat->in_v[0] == 0 && $5.v != 0)
					nat->in_v[0] = $5.v;
				  else if (nat->in_v[0] == 0 && $3.v != 0)
					nat->in_v[0] = $3.v;
				  if (nat->in_v[1] == 0 && $5.v != 0)
					nat->in_v[1] = $5.v;
				  else if (nat->in_v[1] == 0 && $3.v != 0)
					nat->in_v[1] = $3.v;
				  nat->in_osrcatype = $3.t;
				  bcopy(&$3.a, &nat->in_osrc.na_addr[0],
					sizeof($3.a));
				  bcopy(&$3.m, &nat->in_osrc.na_addr[1],
					sizeof($3.a));
				  nat->in_nsrcatype = $5.t;
				  nat->in_nsrcafunc = $5.u;
				  bcopy(&$5.a, &nat->in_nsrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_nsrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	| mapit ifnames addr tlate rhsaddr mapport mapoptions
				{ if ($3.f != $5.f && $3.f != 0 && $5.f != 0)
					yyerror("4.address family mismatch");
				  if (nat->in_v[1] == 0 && $5.v != 0)
					nat->in_v[1] = $5.v;
				  else if (nat->in_v[0] == 0 && $3.v != 0)
					nat->in_v[0] = $3.v;
				  if (nat->in_v[0] == 0 && $5.v != 0)
					nat->in_v[0] = $5.v;
				  else if (nat->in_v[1] == 0 && $3.v != 0)
					nat->in_v[1] = $3.v;
				  nat->in_osrcatype = $3.t;
				  bcopy(&$3.a, &nat->in_osrc.na_addr[0],
					sizeof($3.a));
				  bcopy(&$3.m, &nat->in_osrc.na_addr[1],
					sizeof($3.a));
				  nat->in_nsrcatype = $5.t;
				  nat->in_nsrcafunc = $5.u;
				  bcopy(&$5.a, &nat->in_nsrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_nsrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	| no mapit ifnames addr setproto ';'
				{ if (nat->in_v[0] == 0)
					nat->in_v[0] = $4.v;
				  nat->in_osrcatype = $4.t;
				  bcopy(&$4.a, &nat->in_osrc.na_addr[0],
					sizeof($4.a));
				  bcopy(&$4.m, &nat->in_osrc.na_addr[1],
					sizeof($4.a));

				  setmapifnames();
				}
	| mapit ifnames mapfrom tlate rhsaddr proxy mapoptions
				{ if ($3 != 0 && $5.f != 0 && $3 != $5.f)
					yyerror("5.address family mismatch");
				  if (nat->in_v[0] == 0 && $5.v != 0)
					nat->in_v[0] = $5.v;
				  else if (nat->in_v[0] == 0 && $3 != 0)
					nat->in_v[0] = ftov($3);
				  if (nat->in_v[1] == 0 && $5.v != 0)
					nat->in_v[1] = $5.v;
				  else if (nat->in_v[1] == 0 && $3 != 0)
					nat->in_v[1] = ftov($3);
				  nat->in_nsrcatype = $5.t;
				  nat->in_nsrcafunc = $5.u;
				  bcopy(&$5.a, &nat->in_nsrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_nsrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	| no mapit ifnames mapfrom setproto ';'
				{ nat->in_v[0] = ftov($4);
				  setmapifnames();
				}
	| mapit ifnames mapfrom tlate rhsaddr mapport mapoptions
				{ if ($3 != 0 && $5.f != 0 && $3 != $5.f)
					yyerror("6.address family mismatch");
				  if (nat->in_v[0] == 0 && $5.v != 0)
					nat->in_v[0] = $5.v;
				  else if (nat->in_v[0] == 0 && $3 != 0)
					nat->in_v[0] = ftov($3);
				  if (nat->in_v[1] == 0 && $5.v != 0)
					nat->in_v[1] = $5.v;
				  else if (nat->in_v[1] == 0 && $3 != 0)
					nat->in_v[1] = ftov($3);
				  nat->in_nsrcatype = $5.t;
				  nat->in_nsrcafunc = $5.u;
				  bcopy(&$5.a, &nat->in_nsrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_nsrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	;

mapblock:
	mapblockit ifnames addr tlate addr ports mapoptions
				{ if ($3.f != 0 && $5.f != 0 && $3.f != $5.f)
					yyerror("7.address family mismatch");
				  if (nat->in_v[0] == 0 && $5.v != 0)
					nat->in_v[0] = $5.v;
				  else if (nat->in_v[0] == 0 && $3.v != 0)
					nat->in_v[0] = $3.v;
				  if (nat->in_v[1] == 0 && $5.v != 0)
					nat->in_v[1] = $5.v;
				  else if (nat->in_v[1] == 0 && $3.v != 0)
					nat->in_v[1] = $3.v;
				  nat->in_osrcatype = $3.t;
				  bcopy(&$3.a, &nat->in_osrc.na_addr[0],
					sizeof($3.a));
				  bcopy(&$3.m, &nat->in_osrc.na_addr[1],
					sizeof($3.a));
				  nat->in_nsrcatype = $5.t;
				  nat->in_nsrcafunc = $5.u;
				  bcopy(&$5.a, &nat->in_nsrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_nsrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	| no mapblockit ifnames { yyexpectaddr = 1; } addr setproto ';'
				{ if (nat->in_v[0] == 0)
					nat->in_v[0] = $5.v;
				  if (nat->in_v[1] == 0)
					nat->in_v[1] = $5.v;
				  nat->in_osrcatype = $5.t;
				  bcopy(&$5.a, &nat->in_osrc.na_addr[0],
					sizeof($5.a));
				  bcopy(&$5.m, &nat->in_osrc.na_addr[1],
					sizeof($5.a));

				  setmapifnames();
				}
	;

redir:	rdrit ifnames addr dport tlate dip nport setproto rdroptions
				{ if ($6 != 0 && $3.f != 0 && $6 != $3.f)
					yyerror("21.address family mismatch");
				  if (nat->in_v[0] == 0) {
					if ($3.v != AF_UNSPEC)
						nat->in_v[0] = ftov($3.f);
					  else
						nat->in_v[0] = ftov($6);
				  }
				  nat->in_odstatype = $3.t;
				  bcopy(&$3.a, &nat->in_odst.na_addr[0],
					sizeof($3.a));
				  bcopy(&$3.m, &nat->in_odst.na_addr[1],
					sizeof($3.a));

				  setrdrifnames();
				}
	| no rdrit ifnames addr dport setproto ';'
				{ if (nat->in_v[0] == 0)
					nat->in_v[0] = ftov($4.f);
				  nat->in_odstatype = $4.t;
				  bcopy(&$4.a, &nat->in_odst.na_addr[0],
					sizeof($4.a));
				  bcopy(&$4.m, &nat->in_odst.na_addr[1],
					sizeof($4.a));

				  setrdrifnames();
				}
	| rdrit ifnames rdrfrom tlate dip nport setproto rdroptions
				{ if ($5 != 0 && $3 != 0 && $5 != $3)
					yyerror("20.address family mismatch");
				  if (nat->in_v[0] == 0) {
					  if ($3 != AF_UNSPEC)
						nat->in_v[0] = ftov($3);
					  else
						nat->in_v[0] = ftov($5);
				  }
				  setrdrifnames();
				}
	| no rdrit ifnames rdrfrom setproto ';'
				{ nat->in_v[0] = ftov($4);

				  setrdrifnames();
				}
	;

rewrite:
	IPNY_REWRITE oninout rwrproto mapfrom tlate newdst newopts
				{ if (nat->in_v[0] == 0)
					nat->in_v[0] = ftov($4);
				  if (nat->in_redir & NAT_MAP)
					setmapifnames();
				  else
					setrdrifnames();
				  nat->in_redir |= NAT_REWRITE;
				}
	;

divert:	IPNY_DIVERT oninout rwrproto mapfrom tlate divdst newopts
				{ if (nat->in_v[0] == 0)
					nat->in_v[0] = ftov($4);
				  if (nat->in_redir & NAT_MAP) {
					setmapifnames();
					nat->in_pr[0] = IPPROTO_UDP;
				  } else {
					setrdrifnames();
					nat->in_pr[1] = IPPROTO_UDP;
				  }
				  nat->in_flags &= ~IPN_TCP;
				}
	;

tlate:	IPNY_TLATE		{ yyexpectaddr = 1; }
	;

pconf:	IPNY_PROXY		{ yysetdict(proxies); }
	IPNY_DNS '/' proto IPNY_CONFIG YY_STR '{'
				{ proxy_setconfig(IPNY_DNS); }
	dnslines ';' '}'
				{ proxy_addconfig("dns", $5, $7, $10);
				  proxy_unsetconfig();
				}
	;

dnslines:
	dnsline 		{ $$ = $1; }
	| dnslines ';' dnsline	{ $$ = $1; $1->na_next = $3; }
	;

dnsline:
	IPNY_ALLOW YY_STR	{ $$ = proxy_dns_add_pass(NULL, $2); }
	| IPNY_DENY YY_STR	{ $$ = proxy_dns_add_block(NULL, $2); }
	| IPNY_ALLOW '.' YY_STR	{ $$ = proxy_dns_add_pass(".", $3); }
	| IPNY_DENY '.' YY_STR	{ $$ = proxy_dns_add_block(".", $3); }
	;

oninout:
	inout IPNY_ON ifnames	{ ; }
	;

inout:	IPNY_IN			{ nat->in_redir = NAT_REDIRECT; }
	| IPNY_OUT		{ nat->in_redir = NAT_MAP; }
	;

rwrproto:
	| IPNY_PROTO setproto
	;

newdst:	src rhsaddr srcports dst erhdaddr dstports
				{ nat->in_nsrc.na_addr[0] = $2.a;
				  nat->in_nsrc.na_addr[1] = $2.m;
				  nat->in_nsrc.na_atype = $2.t;
				  if ($2.t == FRI_LOOKUP) {
					nat->in_nsrc.na_type = $2.u;
					nat->in_nsrc.na_subtype = $2.s;
					nat->in_nsrc.na_num = $2.n;
				  }
				  nat->in_nsports[0] = $3.p1;
				  nat->in_nsports[1] = $3.p2;
				  nat->in_ndst.na_addr[0] = $5.a;
				  nat->in_ndst.na_addr[1] = $5.m;
				  nat->in_ndst.na_atype = $5.t;
				  if ($5.t == FRI_LOOKUP) {
					nat->in_ndst.na_type = $5.u;
					nat->in_ndst.na_subtype = $5.s;
					nat->in_ndst.na_num = $5.n;
				  }
				  nat->in_ndports[0] = $6.p1;
				  nat->in_ndports[1] = $6.p2;
				}
	;

divdst:	src addr ',' portspec dst addr ',' portspec IPNY_UDP
				{ nat->in_nsrc.na_addr[0] = $2.a;
				  if ($2.m.in4.s_addr != 0xffffffff)
					yyerror("divert must have /32 dest");
				  nat->in_nsrc.na_addr[1] = $2.m;
				  nat->in_nsports[0] = $4;
				  nat->in_nsports[1] = $4;

				  nat->in_ndst.na_addr[0] = $6.a;
				  nat->in_ndst.na_addr[1] = $6.m;
				  if ($6.m.in4.s_addr != 0xffffffff)
					yyerror("divert must have /32 dest");
				  nat->in_ndports[0] = $8;
				  nat->in_ndports[1] = $8;

				  nat->in_redir |= NAT_DIVERTUDP;
				}
	;

src:	IPNY_SRC		{ yyexpectaddr = 1; }
	;

dst:	IPNY_DST		{ yyexpectaddr = 1; }
	;

srcports:
	comaports		{ $$.p1 = $1.p1;
				  $$.p2 = $1.p2;
				}
	| IPNY_PORT '=' portspec
				{ $$.p1 = $3;
				  $$.p2 = $3;
				  nat->in_flags |= IPN_FIXEDSPORT;
				}
	;

dstports:
	comaports		{ $$.p1 = $1.p1;
				  $$.p2 = $1.p2;
				}
	| IPNY_PORT '=' portspec
				{ $$.p1 = $3;
				  $$.p2 = $3;
				  nat->in_flags |= IPN_FIXEDDPORT;
				}
	;

comaports:
				{ $$.p1 = 0;
				  $$.p2 = 0;
				}
	| ','			{ if (!(nat->in_flags & IPN_TCPUDP))
					yyerror("must be TCP/UDP for ports");
				}
	portpair		{ $$.p1 = $3.p1;
				  $$.p2 = $3.p2;
				}
	;

proxy:	| IPNY_PROXY port portspec YY_STR '/' proto
			{ int pos;
			  pos = addname(&nat, $4);
			  nat->in_plabel = pos;
			  if (nat->in_dcmp == 0) {
				nat->in_odport = $3;
			  } else if ($3 != nat->in_odport) {
				yyerror("proxy port numbers not consistant");
			  }
			  nat->in_ndport = $3;
			  setnatproto($6);
			  free($4);
			}
	| IPNY_PROXY port YY_STR YY_STR '/' proto
			{ int pnum, pos;
			  pos = addname(&nat, $4);
			  nat->in_plabel = pos;
			  pnum = getportproto($3, $6);
			  if (pnum == -1)
				yyerror("invalid port number");
			  nat->in_odport = ntohs(pnum);
			  nat->in_ndport = ntohs(pnum);
			  setnatproto($6);
			  free($3);
			  free($4);
			}
	| IPNY_PROXY port portspec YY_STR '/' proto IPNY_CONFIG YY_STR
			{ int pos;
			  pos = addname(&nat, $4);
			  nat->in_plabel = pos;
			  if (nat->in_dcmp == 0) {
				nat->in_odport = $3;
			  } else if ($3 != nat->in_odport) {
				yyerror("proxy port numbers not consistant");
			  }
			  nat->in_ndport = $3;
			  setnatproto($6);
			  nat->in_pconfig = addname(&nat, $8);
			  free($4);
			  free($8);
			}
	| IPNY_PROXY port YY_STR YY_STR '/' proto IPNY_CONFIG YY_STR
			{ int pnum, pos;
			  pos = addname(&nat, $4);
			  nat->in_plabel = pos;
			  pnum = getportproto($3, $6);
			  if (pnum == -1)
				yyerror("invalid port number");
			  nat->in_odport = ntohs(pnum);
			  nat->in_ndport = ntohs(pnum);
			  setnatproto($6);
			  pos = addname(&nat, $8);
			  nat->in_pconfig = pos;
			  free($3);
			  free($4);
			  free($8);
			}
	;
setproto:
	| proto				{ if (nat->in_pr[0] != 0 ||
					      nat->in_pr[1] != 0 ||
					      nat->in_flags & IPN_TCPUDP)
						yyerror("protocol set twice");
					  setnatproto($1);
					}
	| IPNY_TCPUDP			{ if (nat->in_pr[0] != 0 ||
					      nat->in_pr[1] != 0 ||
					      nat->in_flags & IPN_TCPUDP)
						yyerror("protocol set twice");
					  nat->in_flags |= IPN_TCPUDP;
					  nat->in_pr[0] = 0;
					  nat->in_pr[1] = 0;
					}
	| IPNY_TCP '/' IPNY_UDP		{ if (nat->in_pr[0] != 0 ||
					      nat->in_pr[1] != 0 ||
					      nat->in_flags & IPN_TCPUDP)
						yyerror("protocol set twice");
					  nat->in_flags |= IPN_TCPUDP;
					  nat->in_pr[0] = 0;
					  nat->in_pr[1] = 0;
					}
	;

rhsaddr:
	addr				{ $$ = $1;
					  yyexpectaddr = 0;
					}
	| hostname '-' { yyexpectaddr = 1; } hostname
					{ $$.t = FRI_RANGE;
					  if ($1.f != $4.f)
						yyerror("8.address family "
							"mismatch");
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  $$.a = $1.a;
					  $$.m = $4.a;
					  nat->in_flags |= IPN_SIPRANGE;
					  yyexpectaddr = 0;
					}
	| IPNY_RANGE hostname '-' { yyexpectaddr = 1; } hostname
					{ $$.t = FRI_RANGE;
					  if ($2.f != $5.f)
						yyerror("9.address family "
							"mismatch");
					  $$.f = $2.f;
					  $$.v = ftov($2.f);
					  $$.a = $2.a;
					  $$.m = $5.a;
					  nat->in_flags |= IPN_SIPRANGE;
					  yyexpectaddr = 0;
					}
	;

dip:
	hostname ',' { yyexpectaddr = 1; } hostname
				{ nat->in_flags |= IPN_SPLIT;
				  if ($1.f != $4.f)
					yyerror("10.address family "
						"mismatch");
				  $$ = $1.f;
				  nat->in_ndstip6 = $1.a;
				  nat->in_ndstmsk6 = $4.a;
				  nat->in_ndstatype = FRI_SPLIT;
				  yyexpectaddr = 0;
				}
	| rhdaddr		{ int bits;
				  nat->in_ndstip6 = $1.a;
				  nat->in_ndstmsk6 = $1.m;
				  nat->in_ndst.na_atype = $1.t;
				  yyexpectaddr = 0;
				  if ($1.f == AF_INET)
					bits = count4bits($1.m.in4.s_addr);
				  else
					bits = count6bits($1.m.i6);
				  if (($1.f == AF_INET) && (bits != 0) &&
				      (bits != 32)) {
					yyerror("dest ip bitmask not /32");
				  } else if (($1.f == AF_INET6) &&
					     (bits != 0) && (bits != 128)) {
					yyerror("dest ip bitmask not /128");
				  }
				  $$ = $1.f;
				}
	;

rhdaddr:
	addr				{ $$ = $1;
					  yyexpectaddr = 0;
					}
	| hostname '-' hostname		{ bzero(&$$, sizeof($$));
					  $$.t = FRI_RANGE;
					  if ($1.f != 0 && $3.f != 0 &&
					      $1.f != $3.f)
						yyerror("11.address family "
							"mismatch");
					  $$.a = $1.a;
					  $$.m = $3.a;
					  nat->in_flags |= IPN_DIPRANGE;
					  yyexpectaddr = 0;
					}
	| IPNY_RANGE hostname '-' hostname
					{ bzero(&$$, sizeof($$));
					  $$.t = FRI_RANGE;
					  if ($2.f != 0 && $4.f != 0 &&
					      $2.f != $4.f)
						yyerror("12.address family "
							"mismatch");
					  $$.a = $2.a;
					  $$.m = $4.a;
					  nat->in_flags |= IPN_DIPRANGE;
					  yyexpectaddr = 0;
					}
	;

erhdaddr:
	rhdaddr				{ $$ = $1; }
	| IPNY_DSTLIST '/' YY_NUMBER	{ $$.t = FRI_LOOKUP;
					  $$.u = IPLT_DSTLIST;
					  $$.s = 0;
					  $$.n = $3;
					}
	| IPNY_DSTLIST '/' YY_STR	{ $$.t = FRI_LOOKUP;
					  $$.u = IPLT_DSTLIST;
					  $$.s = 1;
					  $$.n = addname(&nat, $3);
					}
	;

port:	IPNY_PORT			{ suggest_port = 1; }
	;

portspec:
	YY_NUMBER			{ if ($1 > 65535)	/* Unsigned */
						yyerror("invalid port number");
					  else
						$$ = $1;
					}
	| YY_STR			{ if (getport(NULL, $1,
						      &($$), NULL) == -1)
						yyerror("invalid port number");
					  $$ = ntohs($$);
					}
	;

portpair:
	portspec			{ $$.p1 = $1; $$.p2 = $1; }
	| portspec '-' portspec		{ $$.p1 = $1; $$.p2 = $3; }
	| portspec ':' portspec		{ $$.p1 = $1; $$.p2 = $3; }
	;

dport:	| port portpair			{ nat->in_odport = $2.p1;
					  if ($2.p2 == 0)
						nat->in_dtop = $2.p1;
					  else
						nat->in_dtop = $2.p2;
					}
	;

nport:	| port portpair			{ nat->in_dpmin = $2.p1;
					  nat->in_dpnext = $2.p1;
					  nat->in_dpmax = $2.p2;
					  nat->in_ndport = $2.p1;
					  if (nat->in_dtop == 0)
						nat->in_dtop = $2.p2;
					}
	| port '=' portspec		{ nat->in_dpmin = $3;
					  nat->in_dpnext = $3;
					  nat->in_ndport = $3;
					  if (nat->in_dtop == 0)
						nat->in_dtop = nat->in_odport;
					  nat->in_flags |= IPN_FIXEDDPORT;
					}
	;

ports:	| IPNY_PORTS YY_NUMBER		{ nat->in_spmin = $2; }
	| IPNY_PORTS IPNY_AUTO		{ nat->in_flags |= IPN_AUTOPORTMAP; }
	;

mapit:	IPNY_MAP			{ nat->in_redir = NAT_MAP; }
	| IPNY_BIMAP			{ nat->in_redir = NAT_BIMAP; }
	;

rdrit:	IPNY_RDR			{ nat->in_redir = NAT_REDIRECT; }
	;

mapblockit:
	IPNY_MAPBLOCK			{ nat->in_redir = NAT_MAPBLK; }
	;

mapfrom:
	from sobject to dobject		{ if ($2 != 0 && $4 != 0 && $2 != $4)
						yyerror("13.address family "
							"mismatch");
					  $$ = $2;
					}
	| from sobject '!' to dobject
					{ if ($2 != 0 && $5 != 0 && $2 != $5)
						yyerror("14.address family "
							"mismatch");
					  nat->in_flags |= IPN_NOTDST;
					  $$ = $2;
					}
	| from sobject to '!' dobject
					{ if ($2 != 0 && $5 != 0 && $2 != $5)
						yyerror("15.address family "
							"mismatch");
					  nat->in_flags |= IPN_NOTDST;
					  $$ = $2;
					}
	;

rdrfrom:
	from sobject to dobject		{ if ($2 != 0 && $4 != 0 && $2 != $4)
						yyerror("16.address family "
							"mismatch");
					  $$ = $2;
					}
	| '!' from sobject to dobject
					{ if ($3 != 0 && $5 != 0 && $3 != $5)
						yyerror("17.address family "
							"mismatch");
					  nat->in_flags |= IPN_NOTSRC;
					  $$ = $3;
					}
	| from '!' sobject to dobject
					{ if ($3 != 0 && $5 != 0 && $3 != $5)
						yyerror("18.address family "
							"mismatch");
					  nat->in_flags |= IPN_NOTSRC;
					  $$ = $3;
					}
	;

from:	IPNY_FROM			{ nat->in_flags |= IPN_FILTER;
					  yyexpectaddr = 1;
					}
	;

to:	IPNY_TO				{ yyexpectaddr = 1; }
	;

ifnames:
	ifname family			{ yyexpectaddr = 1; } 
	| ifname ',' otherifname family	{ yyexpectaddr = 1; }
	;

ifname:	YY_STR				{ setifname(&nat, 0, $1);
					  free($1);
					}
	;

family:	| IPNY_INET			{ nat->in_v[0] = 4; nat->in_v[1] = 4; }
	| IPNY_INET6			{ nat->in_v[0] = 6; nat->in_v[1] = 6; }
	;

otherifname:
	YY_STR				{ setifname(&nat, 1, $1);
					  free($1);
					}
	;

mapport:
	IPNY_PORTMAP tcpudp portpair sequential
					{ nat->in_spmin = $3.p1;
					  nat->in_spmax = $3.p2;
					}
	| IPNY_PORTMAP portpair tcpudp sequential
					{ nat->in_spmin = $2.p1;
					  nat->in_spmax = $2.p2;
					}
	| IPNY_PORTMAP tcpudp IPNY_AUTO sequential
					{ nat->in_flags |= IPN_AUTOPORTMAP;
					  nat->in_spmin = 1024;
					  nat->in_spmax = 65535;
					}
	| IPNY_ICMPIDMAP YY_STR portpair sequential
			{ if (strcmp($2, "icmp") != 0 &&
			      strcmp($2, "ipv6-icmp") != 0) {
				yyerror("icmpidmap not followed by icmp");
			  }
			  free($2);
			  if ($3.p1 < 0 || $3.p1 > 65535)
				yyerror("invalid 1st ICMP Id number");
			  if ($3.p2 < 0 || $3.p2 > 65535)
				yyerror("invalid 2nd ICMP Id number");
			  if (strcmp($2, "ipv6-icmp") == 0) {
				nat->in_pr[0] = IPPROTO_ICMPV6;
				nat->in_pr[1] = IPPROTO_ICMPV6;
			  } else {
				nat->in_pr[0] = IPPROTO_ICMP;
				nat->in_pr[1] = IPPROTO_ICMP;
			  }
			  nat->in_flags = IPN_ICMPQUERY;
			  nat->in_spmin = $3.p1;
			  nat->in_spmax = $3.p2;
			}
	;

sobject:
	saddr				{ $$ = $1; }
	| saddr port portstuff		{ nat->in_osport = $3.p1;
					  nat->in_stop = $3.p2;
					  nat->in_scmp = $3.pc;
					  $$ = $1;
					}
	;

saddr:	addr				{ nat->in_osrcatype = $1.t;
					  bcopy(&$1.a,
						&nat->in_osrc.na_addr[0],
						sizeof($1.a));
					  bcopy(&$1.m,
						&nat->in_osrc.na_addr[1],
						sizeof($1.m));
					  $$ = $1.f;
					}
	;

dobject:
	daddr				{ $$ = $1; }
	| daddr port portstuff		{ nat->in_odport = $3.p1;
					  nat->in_dtop = $3.p2;
					  nat->in_dcmp = $3.pc;
					  $$ = $1;
					}
	;

daddr:	addr				{ nat->in_odstatype = $1.t;
					  bcopy(&$1.a,
						&nat->in_odst.na_addr[0],
						sizeof($1.a));
					  bcopy(&$1.m,
						&nat->in_odst.na_addr[1],
						sizeof($1.m));
					  $$ = $1.f;
					}
	;

addr:	IPNY_ANY			{ yyexpectaddr = 0;
					  bzero(&$$, sizeof($$));
					  $$.t = FRI_NORMAL;
					}
	| hostname			{ bzero(&$$, sizeof($$));
					  $$.a = $1.a;
					  $$.t = FRI_NORMAL;
					  $$.v = ftov($1.f);
					  $$.f = $1.f;
					  if ($$.f == AF_INET) {
						  $$.m.in4.s_addr = 0xffffffff;
					  } else if ($$.f == AF_INET6) {
						  $$.m.i6[0] = 0xffffffff;
						  $$.m.i6[1] = 0xffffffff;
						  $$.m.i6[2] = 0xffffffff;
						  $$.m.i6[3] = 0xffffffff;
					  }
					  yyexpectaddr = 0;
					}
	| hostname slash YY_NUMBER
					{ bzero(&$$, sizeof($$));
					  $$.a = $1.a;
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  $$.t = FRI_NORMAL;
					  ntomask($$.f, $3, (u_32_t *)&$$.m);
					  $$.a.i6[0] &= $$.m.i6[0];
					  $$.a.i6[1] &= $$.m.i6[1];
					  $$.a.i6[2] &= $$.m.i6[2];
					  $$.a.i6[3] &= $$.m.i6[3];
					  yyexpectaddr = 0;
					}
	| hostname slash ipaddr		{ bzero(&$$, sizeof($$));
					  if ($1.f != $3.f) {
						yyerror("1.address family "
							"mismatch");
					  }
					  $$.a = $1.a;
					  $$.m = $3.a;
					  $$.t = FRI_NORMAL;
					  $$.a.i6[0] &= $$.m.i6[0];
					  $$.a.i6[1] &= $$.m.i6[1];
					  $$.a.i6[2] &= $$.m.i6[2];
					  $$.a.i6[3] &= $$.m.i6[3];
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  yyexpectaddr = 0;
					}
	| hostname slash hexnumber	{ bzero(&$$, sizeof($$));
					  $$.a = $1.a;
					  $$.m.in4.s_addr = htonl($3);
					  $$.t = FRI_NORMAL;
					  $$.a.in4.s_addr &= $$.m.in4.s_addr;
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  if ($$.f == AF_INET6)
						yyerror("incorrect inet6 mask");
					}
	| hostname mask ipaddr		{ bzero(&$$, sizeof($$));
					  if ($1.f != $3.f) {
						yyerror("2.address family "
							"mismatch");
					  }
					  $$.a = $1.a;
					  $$.m = $3.a;
					  $$.t = FRI_NORMAL;
					  $$.a.i6[0] &= $$.m.i6[0];
					  $$.a.i6[1] &= $$.m.i6[1];
					  $$.a.i6[2] &= $$.m.i6[2];
					  $$.a.i6[3] &= $$.m.i6[3];
					  $$.f = $1.f;
					  $$.v = ftov($1.f);
					  yyexpectaddr = 0;
					}
	| hostname mask hexnumber	{ bzero(&$$, sizeof($$));
					  $$.a = $1.a;
					  $$.m.in4.s_addr = htonl($3);
					  $$.t = FRI_NORMAL;
					  $$.a.in4.s_addr &= $$.m.in4.s_addr;
					  $$.f = AF_INET;
					  $$.v = 4;
					}
	| pool slash YY_NUMBER		{ bzero(&$$, sizeof($$));
					  $$.a.iplookupnum = $3;
					  $$.a.iplookuptype = IPLT_POOL;
					  $$.a.iplookupsubtype = 0;
					  $$.t = FRI_LOOKUP;
					}
	| pool slash YY_STR		{ bzero(&$$, sizeof($$));
					  $$.a.iplookupname = addname(&nat,$3);
					  $$.a.iplookuptype = IPLT_POOL;
					  $$.a.iplookupsubtype = 1;
					  $$.t = FRI_LOOKUP;
					}
	| hash slash YY_NUMBER		{ bzero(&$$, sizeof($$));
					  $$.a.iplookupnum = $3;
					  $$.a.iplookuptype = IPLT_HASH;
					  $$.a.iplookupsubtype = 0;
					  $$.t = FRI_LOOKUP;
					}
	| hash slash YY_STR		{ bzero(&$$, sizeof($$));
					  $$.a.iplookupname = addname(&nat,$3);
					  $$.a.iplookuptype = IPLT_HASH;
					  $$.a.iplookupsubtype = 1;
					  $$.t = FRI_LOOKUP;
					}
	;

slash:	'/'				{ yyexpectaddr = 0; }
	;

mask:	IPNY_MASK			{ yyexpectaddr = 0; }
	;

pool:	IPNY_POOL			{ if (!(nat->in_flags & IPN_FILTER)) {
						yyerror("Can only use pool with from/to rules\n");
					  }
					  yyexpectaddr = 0;
					  yyresetdict();
					}
	;

hash:	IPNY_HASH			{ if (!(nat->in_flags & IPN_FILTER)) {
						yyerror("Can only use hash with from/to rules\n");
					  }
					  yyexpectaddr = 0;
					  yyresetdict();
					}
	;

portstuff:
	compare portspec		{ $$.pc = $1; $$.p1 = $2; $$.p2 = 0; }
	| portspec range portspec	{ $$.pc = $2; $$.p1 = $1; $$.p2 = $3; }
	;

mapoptions:
	rr frag age mssclamp nattag setproto purge
	;

rdroptions:
	rr frag age sticky mssclamp rdrproxy nattag purge
	;

nattag:	| IPNY_TAG YY_STR		{ strncpy(nat->in_tag.ipt_tag, $2,
						  sizeof(nat->in_tag.ipt_tag));
					}
rr:	| IPNY_ROUNDROBIN		{ nat->in_flags |= IPN_ROUNDR; }
	;

frag:	| IPNY_FRAG			{ nat->in_flags |= IPN_FRAG; }
	;

age:	| IPNY_AGE YY_NUMBER			{ nat->in_age[0] = $2;
						  nat->in_age[1] = $2; }
	| IPNY_AGE YY_NUMBER '/' YY_NUMBER	{ nat->in_age[0] = $2;
						  nat->in_age[1] = $4; }
	;

sticky: | IPNY_STICKY			{ if (!(nat->in_flags & IPN_ROUNDR) &&
					      !(nat->in_flags & IPN_SPLIT)) {
						FPRINTF(stderr,
		"'sticky' for use with round-robin/IP splitting only\n");
					  } else
						nat->in_flags |= IPN_STICKY;
					}
	;

mssclamp:
	| IPNY_MSSCLAMP YY_NUMBER		{ nat->in_mssclamp = $2; }
	;

tcpudp:	IPNY_TCP			{ setnatproto(IPPROTO_TCP); }
	| IPNY_UDP			{ setnatproto(IPPROTO_UDP); }
	| IPNY_TCPUDP			{ nat->in_flags |= IPN_TCPUDP;
					  nat->in_pr[0] = 0;
					  nat->in_pr[1] = 0;
					}
	| IPNY_TCP '/' IPNY_UDP		{ nat->in_flags |= IPN_TCPUDP;
					  nat->in_pr[0] = 0;
					  nat->in_pr[1] = 0;
					}
	;

sequential:
	| IPNY_SEQUENTIAL		{ nat->in_flags |= IPN_SEQUENTIAL; }
	;

purge:
	| IPNY_PURGE			{ nat->in_flags |= IPN_PURGE; }
	;

rdrproxy:
	IPNY_PROXY YY_STR
					{ int pos;
					  pos = addname(&nat, $2);
					  nat->in_plabel = pos;
					  nat->in_odport = nat->in_dpnext;
					  nat->in_dtop = nat->in_odport;
					  free($2);
					}
	| proxy			{ if (nat->in_plabel != -1) {
					nat->in_ndport = nat->in_odport;
					nat->in_dpmin = nat->in_odport;
					nat->in_dpmax = nat->in_dpmin;
					nat->in_dtop = nat->in_dpmin;
					nat->in_dpnext = nat->in_dpmin;
				  }
				}
	;

newopts:
	| IPNY_PURGE			{ nat->in_flags |= IPN_PURGE; }
	;

proto:	YY_NUMBER			{ $$ = $1;
					  if ($$ != IPPROTO_TCP &&
					      $$ != IPPROTO_UDP)
						suggest_port = 0;
					}
	| IPNY_TCP			{ $$ = IPPROTO_TCP; }
	| IPNY_UDP			{ $$ = IPPROTO_UDP; }
	| YY_STR			{ $$ = getproto($1);
					  free($1);
					  if ($$ == -1)
						yyerror("unknown protocol");
					  if ($$ != IPPROTO_TCP &&
					      $$ != IPPROTO_UDP)
						suggest_port = 0;
					}
	;

hexnumber:
	YY_HEX				{ $$ = $1; }
	;

hostname:
	YY_STR				{ i6addr_t addr;
					  int family;

#ifdef USE_INET6
					  if (nat->in_v[0] == 6)
						family = AF_INET6;
					  else
#endif
						family = AF_INET;
					  memset(&($$), 0, sizeof($$));
					  memset(&addr, 0, sizeof(addr));
					  $$.f = family;
					  if (gethost(family, $1,
						      &addr) == 0) {
						$$.a = addr;
					  } else {
						FPRINTF(stderr,
							"Unknown host '%s'\n",
							$1);
					  }
					  free($1);
					}
	| YY_NUMBER			{ memset(&($$), 0, sizeof($$));
					  $$.a.in4.s_addr = htonl($1);
					  if ($$.a.in4.s_addr != 0)
						$$.f = AF_INET;
					}
	| ipv4				{ $$ = $1; }
	| YY_IPV6			{ memset(&($$), 0, sizeof($$));
					  $$.a = $1;
					  $$.f = AF_INET6;
					}
	| YY_NUMBER YY_IPV6		{ memset(&($$), 0, sizeof($$));
					  $$.a = $2;
					  $$.f = AF_INET6;
					}
	;

compare:
	'='				{ $$ = FR_EQUAL; }
	| YY_CMP_EQ			{ $$ = FR_EQUAL; }
	| YY_CMP_NE			{ $$ = FR_NEQUAL; }
	| YY_CMP_LT			{ $$ = FR_LESST; }
	| YY_CMP_LE			{ $$ = FR_LESSTE; }
	| YY_CMP_GT			{ $$ = FR_GREATERT; }
	| YY_CMP_GE			{ $$ = FR_GREATERTE; }

range:
	YY_RANGE_OUT			{ $$ = FR_OUTRANGE; }
	| YY_RANGE_IN			{ $$ = FR_INRANGE; }
	| ':'				{ $$ = FR_INCRANGE; }
	;

ipaddr:	ipv4				{ $$ = $1; }
	| YY_IPV6			{ $$.a = $1;
					  $$.f = AF_INET6;
					}
	;

ipv4:	YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER
		{ if ($1 > 255 || $3 > 255 || $5 > 255 || $7 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  bzero((char *)&$$, sizeof($$));
		  $$.a.in4.s_addr = ($1 << 24) | ($3 << 16) | ($5 << 8) | $7;
		  $$.a.in4.s_addr = htonl($$.a.in4.s_addr);
		  $$.f = AF_INET;
		}
	;

%%


static	wordtab_t	proxies[] = {
	{ "dns",	IPNY_DNS }
};

static	wordtab_t	dnswords[] = {
	{ "allow",	IPNY_ALLOW },
	{ "block",	IPNY_DENY },
	{ "deny",	IPNY_DENY },
	{ "drop",	IPNY_DENY },
	{ "pass",	IPNY_ALLOW },

};

static	wordtab_t	yywords[] = {
	{ "age",	IPNY_AGE },
	{ "any",	IPNY_ANY },
	{ "auto",	IPNY_AUTO },
	{ "bimap",	IPNY_BIMAP },
	{ "config",	IPNY_CONFIG },
	{ "divert",	IPNY_DIVERT },
	{ "dst",	IPNY_DST },
	{ "dstlist",	IPNY_DSTLIST },
	{ "frag",	IPNY_FRAG },
	{ "from",	IPNY_FROM },
	{ "hash",	IPNY_HASH },
	{ "icmpidmap",	IPNY_ICMPIDMAP },
	{ "in",		IPNY_IN },
	{ "inet",	IPNY_INET },
	{ "inet6",	IPNY_INET6 },
	{ "mask",	IPNY_MASK },
	{ "map",	IPNY_MAP },
	{ "map-block",	IPNY_MAPBLOCK },
	{ "mssclamp",	IPNY_MSSCLAMP },
	{ "netmask",	IPNY_MASK },
	{ "no",		IPNY_NO },
	{ "on",		IPNY_ON },
	{ "out",	IPNY_OUT },
	{ "pool",	IPNY_POOL },
	{ "port",	IPNY_PORT },
	{ "portmap",	IPNY_PORTMAP },
	{ "ports",	IPNY_PORTS },
	{ "proto",	IPNY_PROTO },
	{ "proxy",	IPNY_PROXY },
	{ "purge",	IPNY_PURGE },
	{ "range",	IPNY_RANGE },
	{ "rewrite",	IPNY_REWRITE },
	{ "rdr",	IPNY_RDR },
	{ "round-robin",IPNY_ROUNDROBIN },
	{ "sequential",	IPNY_SEQUENTIAL },
	{ "src",	IPNY_SRC },
	{ "sticky",	IPNY_STICKY },
	{ "tag",	IPNY_TAG },
	{ "tcp",	IPNY_TCP },
	{ "tcpudp",	IPNY_TCPUDP },
	{ "to",		IPNY_TO },
	{ "udp",	IPNY_UDP },
	{ "-",		'-' },
	{ "->",		IPNY_TLATE },
	{ "eq",		YY_CMP_EQ },
	{ "ne",		YY_CMP_NE },
	{ "lt",		YY_CMP_LT },
	{ "gt",		YY_CMP_GT },
	{ "le",		YY_CMP_LE },
	{ "ge",		YY_CMP_GE },
	{ NULL,		0 }
};


int
ipnat_parsefile(fd, addfunc, ioctlfunc, filename)
	int fd;
	addfunc_t addfunc;
	ioctlfunc_t ioctlfunc;
	char *filename;
{
	FILE *fp = NULL;
	int rval;
	char *s;

	yylineNum = 1;

	(void) yysettab(yywords);

	s = getenv("YYDEBUG");
	if (s)
		yydebug = atoi(s);
	else
		yydebug = 0;

	if (strcmp(filename, "-")) {
		fp = fopen(filename, "r");
		if (!fp) {
			FPRINTF(stderr, "fopen(%s) failed: %s\n", filename,
				STRERROR(errno));
			return -1;
		}
	} else
		fp = stdin;

	while ((rval = ipnat_parsesome(fd, addfunc, ioctlfunc, fp)) == 0)
		;
	if (fp != NULL)
		fclose(fp);
	if (rval == -1)
		rval = 0;
	else if (rval != 0)
		rval = 1;
	return rval;
}


int
ipnat_parsesome(fd, addfunc, ioctlfunc, fp)
	int fd;
	addfunc_t addfunc;
	ioctlfunc_t ioctlfunc;
	FILE *fp;
{
	char *s;
	int i;

	natfd = fd;
	parser_error = 0;
	nataddfunc = addfunc;
	natioctlfunc = ioctlfunc;

	if (feof(fp))
		return -1;
	i = fgetc(fp);
	if (i == EOF)
		return -1;
	if (ungetc(i, fp) == EOF)
		return -1;
	if (feof(fp))
		return -1;
	s = getenv("YYDEBUG");
	if (s)
		yydebug = atoi(s);
	else
		yydebug = 0;

	yyin = fp;
	yyparse();
	return parser_error;
}


static void
newnatrule()
{
	ipnat_t *n;

	n = calloc(1, sizeof(*n));
	if (n == NULL)
		return;

	if (nat == NULL) {
		nattop = nat = n;
		n->in_pnext = &nattop;
	} else {
		nat->in_next = n;
		n->in_pnext = &nat->in_next;
		nat = n;
	}

	n->in_flineno = yylineNum;
	n->in_ifnames[0] = -1;
	n->in_ifnames[1] = -1;
	n->in_plabel = -1;
	n->in_pconfig = -1;
	n->in_size = sizeof(*n);

	suggest_port = 0;
}


static void
setnatproto(p)
	int p;
{
	nat->in_pr[0] = p;
	nat->in_pr[1] = p;

	switch (p)
	{
	case IPPROTO_TCP :
		nat->in_flags |= IPN_TCP;
		nat->in_flags &= ~IPN_UDP;
		break;
	case IPPROTO_UDP :
		nat->in_flags |= IPN_UDP;
		nat->in_flags &= ~IPN_TCP;
		break;
#ifdef USE_INET6
	case IPPROTO_ICMPV6 :
#endif
	case IPPROTO_ICMP :
		nat->in_flags &= ~IPN_TCPUDP;
		if (!(nat->in_flags & IPN_ICMPQUERY) &&
		    !(nat->in_redir & NAT_DIVERTUDP)) {
			nat->in_dcmp = 0;
			nat->in_scmp = 0;
			nat->in_dpmin = 0;
			nat->in_dpmax = 0;
			nat->in_dpnext = 0;
			nat->in_spmin = 0;
			nat->in_spmax = 0;
			nat->in_spnext = 0;
		}
		break;
	default :
		if ((nat->in_redir & NAT_MAPBLK) == 0) {
			nat->in_flags &= ~IPN_TCPUDP;
			nat->in_dcmp = 0;
			nat->in_scmp = 0;
			nat->in_dpmin = 0;
			nat->in_dpmax = 0;
			nat->in_dpnext = 0;
			nat->in_spmin = 0;
			nat->in_spmax = 0;
			nat->in_spnext = 0;
		}
		break;
	}

	if ((nat->in_flags & (IPN_TCP|IPN_UDP)) == 0) {
		nat->in_stop = 0;
		nat->in_dtop = 0;
		nat->in_osport = 0;
		nat->in_odport = 0;
		nat->in_stop = 0;
		nat->in_osport = 0;
		nat->in_dtop = 0;
		nat->in_odport = 0;
	}
	if ((nat->in_flags & (IPN_TCPUDP|IPN_FIXEDDPORT)) == IPN_FIXEDDPORT)
		nat->in_flags &= ~IPN_FIXEDDPORT;
}


int
ipnat_addrule(fd, ioctlfunc, ptr)
	int fd;
	ioctlfunc_t ioctlfunc;
	void *ptr;
{
	ioctlcmd_t add, del;
	ipfobj_t obj;
	ipnat_t *ipn;

	ipn = ptr;
	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = ipn->in_size;
	obj.ipfo_type = IPFOBJ_IPNAT;
	obj.ipfo_ptr = ptr;

	if ((opts & OPT_DONOTHING) != 0)
		fd = -1;

	if (opts & OPT_ZERORULEST) {
		add = SIOCZRLST;
		del = 0;
	} else if (opts & OPT_PURGE) {
		add = 0;
		del = SIOCPURGENAT;
	} else {
		add = SIOCADNAT;
		del = SIOCRMNAT;
	}

	if ((opts & OPT_VERBOSE) != 0)
		printnat(ipn, opts);

	if (opts & OPT_DEBUG)
		binprint(ipn, ipn->in_size);

	if ((opts & OPT_ZERORULEST) != 0) {
		if ((*ioctlfunc)(fd, add, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(zero nat rule)",
					ipn->in_flineno);
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		} else {
			PRINTF("hits %lu ", ipn->in_hits);
#ifdef USE_QUAD_T
			PRINTF("bytes %"PRIu64" ",
			       ipn->in_bytes[0] + ipn->in_bytes[1]);
#else
			PRINTF("bytes %lu ",
			       ipn->in_bytes[0] + ipn->in_bytes[1]);
#endif
			printnat(ipn, opts);
		}
	} else if ((opts & OPT_REMOVE) != 0) {
		if ((*ioctlfunc)(fd, del, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(delete nat rule)",
					ipn->in_flineno);
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		}
	} else {
		if ((*ioctlfunc)(fd, add, (void *)&obj) == -1) {
			if ((opts & OPT_DONOTHING) == 0) {
				char msg[80];

				sprintf(msg, "%d:ioctl(add/insert nat rule)",
					ipn->in_flineno);
				if (errno == EEXIST) {
					sprintf(msg + strlen(msg), "(line %d)",
						ipn->in_flineno);
				}
				return ipf_perror_fd(fd, ioctlfunc, msg);
			}
		}
	}
	return 0;
}


static void
setmapifnames()
{
	if (nat->in_ifnames[1] == -1)
		nat->in_ifnames[1] = nat->in_ifnames[0];

	if ((suggest_port == 1) && (nat->in_flags & IPN_TCPUDP) == 0)
		nat->in_flags |= IPN_TCPUDP;

	if ((nat->in_flags & IPN_TCPUDP) == 0)
		setnatproto(nat->in_pr[1]);

	if (((nat->in_redir & NAT_MAPBLK) != 0) ||
	      ((nat->in_flags & IPN_AUTOPORTMAP) != 0))
		nat_setgroupmap(nat);
}


static void
setrdrifnames()
{
	if ((suggest_port == 1) && (nat->in_flags & IPN_TCPUDP) == 0)
		nat->in_flags |= IPN_TCPUDP;

	if ((nat->in_pr[0] == 0) && ((nat->in_flags & IPN_TCPUDP) == 0) &&
	    (nat->in_dpmin != 0 || nat->in_dpmax != 0 || nat->in_dpnext != 0))
		setnatproto(IPPROTO_TCP);

	if (nat->in_ifnames[1] == -1)
		nat->in_ifnames[1] = nat->in_ifnames[0];
}


static void
proxy_setconfig(proxy)
	int proxy;
{
	if (proxy == IPNY_DNS) {
		yysetfixeddict(dnswords);
	}
}


static void
proxy_unsetconfig()
{
	yyresetdict();
}


static namelist_t *
proxy_dns_add_pass(prefix, name)
	char *prefix, *name;
{
	namelist_t *n;

	n = calloc(1, sizeof(*n));
	if (n != NULL) {
		if (prefix == NULL || *prefix == '\0') {
			n->na_name = strdup(name);
		} else {
			n->na_name = malloc(strlen(name) + strlen(prefix) + 1);
			strcpy(n->na_name, prefix);
			strcat(n->na_name, name);
		}
	}
	return n;
}


static namelist_t *
proxy_dns_add_block(prefix, name)
	char *prefix, *name;
{
	namelist_t *n;

	n = calloc(1, sizeof(*n));
	if (n != NULL) {
		if (prefix == NULL || *prefix == '\0') {
			n->na_name = strdup(name);
		} else {
			n->na_name = malloc(strlen(name) + strlen(prefix) + 1);
			strcpy(n->na_name, prefix);
			strcat(n->na_name, name);
		}
		n->na_value = 1;
	}
	return n;
}


static void
proxy_addconfig(proxy, proto, conf, list)
	char *proxy, *conf;
	int proto;
	namelist_t *list;
{
	proxyrule_t *pr;

	pr = calloc(1, sizeof(*pr));
	if (pr != NULL) {
		pr->pr_proto = proto;
		pr->pr_proxy = proxy;
		pr->pr_conf = conf;
		pr->pr_names = list;
		pr->pr_next = prules;
		prules = pr;
	}
}


static void
proxy_loadrules(fd, ioctlfunc, rules)
	int fd;
	ioctlfunc_t ioctlfunc;
	proxyrule_t *rules;
{
	proxyrule_t *pr;

	while ((pr = rules) != NULL) {
		proxy_loadconfig(fd, ioctlfunc, pr->pr_proxy, pr->pr_proto,
				 pr->pr_conf, pr->pr_names);
		rules = pr->pr_next;
		free(pr->pr_conf);
		free(pr);
	}
}


static void
proxy_loadconfig(fd, ioctlfunc, proxy, proto, conf, list)
	int fd;
	ioctlfunc_t ioctlfunc;
	char *proxy, *conf;
	int proto;
	namelist_t *list;
{
	namelist_t *na;
	ipfobj_t obj;
	ap_ctl_t pcmd;

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_PROXYCTL;
	obj.ipfo_size = sizeof(pcmd);
	obj.ipfo_ptr = &pcmd;

	while ((na = list) != NULL) {
		if ((opts & OPT_REMOVE) != 0)
			pcmd.apc_cmd = APC_CMD_DEL;
		else
			pcmd.apc_cmd = APC_CMD_ADD;
		pcmd.apc_dsize = strlen(na->na_name) + 1;
		pcmd.apc_data = na->na_name;
		pcmd.apc_arg = na->na_value;
		pcmd.apc_p = proto;

		strncpy(pcmd.apc_label, proxy, APR_LABELLEN);
		pcmd.apc_label[APR_LABELLEN - 1] = '\0';

		strncpy(pcmd.apc_config, conf, APR_LABELLEN);
		pcmd.apc_config[APR_LABELLEN - 1] = '\0';

		if ((*ioctlfunc)(fd, SIOCPROXY, (void *)&obj) == -1) {
                        if ((opts & OPT_DONOTHING) == 0) {
                                char msg[80];

                                sprintf(msg, "%d:ioctl(add/remove proxy rule)",
					yylineNum);
                                ipf_perror_fd(fd, ioctlfunc, msg);
				return;
                        }
		}

		list = na->na_next;
		free(na->na_name);
		free(na);
	}
}


static void
setifname(np, idx, name)
	ipnat_t **np;
	int idx;
	char *name;
{
	int pos;

	pos = addname(np, name);
	if (pos == -1)
		return;
	(*np)->in_ifnames[idx] = pos;
}


static int
addname(np, name)
	ipnat_t **np;
	char *name;
{
	ipnat_t *n;
	int nlen;
	int pos;

	nlen = strlen(name) + 1;
	n = realloc(*np, (*np)->in_size + nlen);
	if (*np == nattop)
		nattop = n;
	*np = n;
	if (n == NULL)
		return -1;
	if (n->in_pnext != NULL)
		*n->in_pnext = n;
	n->in_size += nlen;
	pos = n->in_namelen;
	n->in_namelen += nlen;
	strcpy(n->in_names + pos, name);
	n->in_names[n->in_namelen] = '\0';
	return pos;
}
