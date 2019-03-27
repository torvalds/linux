/*	$FreeBSD$	*/

%{
/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#if defined(__SVR4) || defined(__sysv__)
#include <sys/stream.h>
#endif
#include <sys/types.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include "iplang_y.h"
#include "ipf.h"

#ifndef	__P
# ifdef __STDC__
#  define	__P(x)	x
# else
#  define	__P(x)	()
# endif
#endif

extern int opts;

int lineNum = 0, ipproto = 0, oldipproto = 0, next = -1, laststate = 0;
int *prstack = NULL, numpr = 0, state = 0, token = 0;

void    yyerror __P((char *));
void	push_proto __P((void));
void	pop_proto __P((void));
int	next_state __P((int, int));
int	next_item __P((int));
int	save_token __P((void));
void	swallow __P((void));
int	yylex __P((void));

struct	lwordtab	{
	char	*word;
	int	state;
	int	next;
};

struct	lwordtab	words[] = {
	{ "interface",	IL_INTERFACE,		-1 },
	{ "iface",	IL_INTERFACE,		-1 },
	{ "name",	IL_IFNAME,		IL_TOKEN },
	{ "ifname",	IL_IFNAME,		IL_TOKEN },
	{ "router",	IL_DEFROUTER,		IL_TOKEN },
	{ "mtu",	IL_MTU,			IL_NUMBER },
	{ "eaddr",	IL_EADDR,		IL_TOKEN },
	{ "v4addr",	IL_V4ADDR,		IL_TOKEN },
	{ "ipv4",	IL_IPV4,		-1 },
	{ "v",		IL_V4V,			IL_TOKEN },
	{ "proto",	IL_V4PROTO,		IL_TOKEN },
	{ "hl",		IL_V4HL,		IL_TOKEN },
	{ "id",		IL_V4ID,		IL_TOKEN },
	{ "ttl",	IL_V4TTL,		IL_TOKEN },
	{ "tos",	IL_V4TOS,		IL_TOKEN },
	{ "src",	IL_V4SRC,		IL_TOKEN },
	{ "dst",	IL_V4DST,		IL_TOKEN },
	{ "opt",	IL_OPT,			-1 },
	{ "len",	IL_LEN,			IL_TOKEN },
	{ "off",	IL_OFF,			IL_TOKEN },
	{ "sum",	IL_SUM,			IL_TOKEN },
	{ "tcp",	IL_TCP,			-1 },
	{ "sport",	IL_SPORT,		IL_TOKEN },
	{ "dport",	IL_DPORT,		IL_TOKEN },
	{ "seq",	IL_TCPSEQ,		IL_TOKEN },
	{ "ack",	IL_TCPACK,		IL_TOKEN },
	{ "flags",	IL_TCPFL,		IL_TOKEN },
	{ "urp",	IL_TCPURP,		IL_TOKEN },
	{ "win",	IL_TCPWIN,		IL_TOKEN },
	{ "udp",	IL_UDP,			-1 },
	{ "send",	IL_SEND,		-1 },
	{ "via",	IL_VIA,			IL_TOKEN },
	{ "arp",	IL_ARP,			-1 },
	{ "data",	IL_DATA,		-1 },
	{ "value",	IL_DVALUE,		IL_TOKEN },
	{ "file",	IL_DFILE,		IL_TOKEN },
	{ "nop",	IL_IPO_NOP,		-1 },
	{ "eol",	IL_IPO_EOL,		-1 },
	{ "rr",		IL_IPO_RR,		-1 },
	{ "zsu",	IL_IPO_ZSU,		-1 },
	{ "mtup",	IL_IPO_MTUP,		-1 },
	{ "mtur",	IL_IPO_MTUR,		-1 },
	{ "encode",	IL_IPO_ENCODE,		-1 },
	{ "ts",		IL_IPO_TS,		-1 },
	{ "tr",		IL_IPO_TR,		-1 },
	{ "sec",	IL_IPO_SEC,		-1 },
	{ "secclass",	IL_IPO_SECCLASS,	IL_TOKEN },
	{ "lsrr",	IL_IPO_LSRR,		-1 },
	{ "esec",	IL_IPO_ESEC,		-1 },
	{ "cipso",	IL_IPO_CIPSO,		-1 },
	{ "satid",	IL_IPO_SATID,		-1 },
	{ "ssrr",	IL_IPO_SSRR,		-1 },
	{ "addext",	IL_IPO_ADDEXT,		-1 },
	{ "visa",	IL_IPO_VISA,		-1 },
	{ "imitd",	IL_IPO_IMITD,		-1 },
	{ "eip",	IL_IPO_EIP,		-1 },
	{ "finn",	IL_IPO_FINN,		-1 },
	{ "mss",	IL_TCPO_MSS,		IL_TOKEN },
	{ "wscale",	IL_TCPO_WSCALE,		IL_TOKEN },
	{ "reserv-4",	IL_IPS_RESERV4,		-1 },
	{ "topsecret",	IL_IPS_TOPSECRET,	-1 },
	{ "secret",	IL_IPS_SECRET,		-1 },
	{ "reserv-3",	IL_IPS_RESERV3,		-1 },
	{ "confid",	IL_IPS_CONFID,		-1 },
	{ "unclass",	IL_IPS_UNCLASS,		-1 },
	{ "reserv-2",	IL_IPS_RESERV2,		-1 },
	{ "reserv-1",	IL_IPS_RESERV1,		-1 },
	{ "icmp",	IL_ICMP,		-1 },
	{ "type",	IL_ICMPTYPE,		-1 },
	{ "code",	IL_ICMPCODE,		-1 },
	{ "echorep",	IL_ICMP_ECHOREPLY,	-1 },
	{ "unreach",	IL_ICMP_UNREACH,	-1 },
	{ "squench",	IL_ICMP_SOURCEQUENCH,	-1 },
	{ "redir",	IL_ICMP_REDIRECT,	-1 },
	{ "echo",	IL_ICMP_ECHO,		-1 },
	{ "routerad",	IL_ICMP_ROUTERADVERT,	-1 },
	{ "routersol",	IL_ICMP_ROUTERSOLICIT,	-1 },
	{ "timex",	IL_ICMP_TIMXCEED,	-1 },
	{ "paramprob",	IL_ICMP_PARAMPROB,	-1 },
	{ "timest",	IL_ICMP_TSTAMP,		-1 },
	{ "timestrep",	IL_ICMP_TSTAMPREPLY,	-1 },
	{ "inforeq",	IL_ICMP_IREQ,		-1 },
	{ "inforep",	IL_ICMP_IREQREPLY,	-1 },
	{ "maskreq",	IL_ICMP_MASKREQ,	-1 },
	{ "maskrep",	IL_ICMP_MASKREPLY,	-1 },
	{ "net-unr",	IL_ICMP_UNREACH_NET,	-1 },
	{ "host-unr",	IL_ICMP_UNREACH_HOST,	-1 },
	{ "proto-unr",	IL_ICMP_UNREACH_PROTOCOL,	-1 },
	{ "port-unr",	IL_ICMP_UNREACH_PORT,	-1 },
	{ "needfrag",	IL_ICMP_UNREACH_NEEDFRAG,	-1 },
	{ "srcfail",	IL_ICMP_UNREACH_SRCFAIL,	-1 },
	{ "net-unk",	IL_ICMP_UNREACH_NET_UNKNOWN,	-1 },
	{ "host-unk",	IL_ICMP_UNREACH_HOST_UNKNOWN,	-1 },
	{ "isolate",	IL_ICMP_UNREACH_ISOLATED,	-1 },
	{ "net-prohib",	IL_ICMP_UNREACH_NET_PROHIB,	-1 },
	{ "host-prohib",	IL_ICMP_UNREACH_HOST_PROHIB,	-1 },
	{ "net-tos",	IL_ICMP_UNREACH_TOSNET,	-1 },
	{ "host-tos",	IL_ICMP_UNREACH_TOSHOST,	-1 },
	{ "filter-prohib",	IL_ICMP_UNREACH_FILTER_PROHIB,	-1 },
	{ "host-preced",	IL_ICMP_UNREACH_HOST_PRECEDENCE,	-1 },
	{ "cutoff-preced",	IL_ICMP_UNREACH_PRECEDENCE_CUTOFF,	-1 },
	{ "net-redir",	IL_ICMP_REDIRECT_NET,	-1 },
	{ "host-redir",	IL_ICMP_REDIRECT_HOST,	-1 },
	{ "tos-net-redir",	IL_ICMP_REDIRECT_TOSNET,	-1 },
	{ "tos-host-redir",	IL_ICMP_REDIRECT_TOSHOST,	-1 },
	{ "intrans",	IL_ICMP_TIMXCEED_INTRANS,	-1 },
	{ "reass",	IL_ICMP_TIMXCEED_REASS,	-1 },
	{ "optabsent",	IL_ICMP_PARAMPROB_OPTABSENT,	-1 },
	{ "otime",	IL_ICMP_OTIME,		-1 },
	{ "rtime",	IL_ICMP_RTIME,		-1 },
	{ "ttime",	IL_ICMP_TTIME,		-1 },
	{ "icmpseq",	IL_ICMP_SEQ,		-1 },
	{ "icmpid",	IL_ICMP_SEQ,		-1 },
	{ ".",		IL_DOT,			-1 },
	{ NULL, 0, 0 }
};
%}
white	[ \t\r]+
%%
{white}	;
\n	{ lineNum++; swallow(); }
\{	{ push_proto(); return next_item('{'); }
\}	{ pop_proto(); return next_item('}'); }
;	{ return next_item(';'); }
[0-9]+	{ return next_item(IL_NUMBER); }
[0-9a-fA-F]	{ return next_item(IL_HEXDIGIT); }
:	{ return next_item(IL_COLON); }
#[^\n]*	{ return next_item(IL_COMMENT); }
[^ \{\}\n\t;:{}]*	{ return next_item(IL_TOKEN); }
\"[^\"]*\"	{ return next_item(IL_TOKEN); }
%%
void    yyerror(msg)
char    *msg;
{
	fprintf(stderr, "%s error at \"%s\", line %d\n", msg, yytext,
		lineNum + 1);
	exit(1);
}


void push_proto()
{
	numpr++;
	if (!prstack)
		prstack = (int *)malloc(sizeof(int));
	else
		prstack = (int *)reallocarray((char *)prstack, numpr,
		    sizeof(int));
	prstack[numpr - 1] = oldipproto;
}


void pop_proto()
{
	numpr--;
	ipproto = prstack[numpr];
	if (!numpr) {
		free(prstack);
		prstack = NULL;
		return;
	}
	prstack = (int *)realloc((char *)prstack, numpr * sizeof(int));
}


int save_token()
{

	yylval.str = strdup((char *)yytext);
	return IL_TOKEN;
}


int next_item(nstate)
int nstate;
{
	struct	lwordtab	*wt;

	if (opts & OPT_DEBUG)
		printf("text=[%s] id=%d next=%d\n", yytext, nstate, next);
	if (next == IL_TOKEN) {
		next = -1;
		return save_token();
	}
	token++;

	for (wt = words; wt->word; wt++)
		if (!strcasecmp(wt->word, (char *)yytext))
			return next_state(wt->state, wt->next);
	if (opts & OPT_DEBUG)
		printf("unknown keyword=[%s]\n", yytext);
	next = -1;
	if (nstate == IL_NUMBER)
		yylval.num = atoi((char *)yytext);
	token++;
	return nstate;
}


int next_state(nstate, fornext)
int nstate, fornext;
{
	next = fornext;

	switch (nstate)
	{
	case IL_IPV4 :
	case IL_TCP :
	case IL_UDP :
	case IL_ICMP :
	case IL_DATA :
	case IL_INTERFACE :
	case IL_ARP :
		oldipproto = ipproto;
		ipproto = nstate;
		break;
	case IL_SUM :
		if (ipproto == IL_IPV4)
			nstate = IL_V4SUM;
		else if (ipproto == IL_TCP)
			nstate = IL_TCPSUM;
		else if (ipproto == IL_UDP)
			nstate = IL_UDPSUM;
		break;
	case IL_OPT :
		if (ipproto == IL_IPV4)
			nstate = IL_V4OPT;
		else if (ipproto == IL_TCP)
			nstate = IL_TCPOPT;
		break;
	case IL_IPO_NOP :
		if (ipproto == IL_TCP)
			nstate = IL_TCPO_NOP;
		break;
	case IL_IPO_EOL :
		if (ipproto == IL_TCP)
			nstate = IL_TCPO_EOL;
		break;
	case IL_IPO_TS :
		if (ipproto == IL_TCP)
			nstate = IL_TCPO_TS;
		break;
	case IL_OFF :
		if (ipproto == IL_IPV4)
			nstate = IL_V4OFF;
		else if (ipproto == IL_TCP)
			nstate = IL_TCPOFF;
		break;
	case IL_LEN :
		if (ipproto == IL_IPV4)
			nstate = IL_V4LEN;
		else if (ipproto == IL_UDP)
			nstate = IL_UDPLEN;
		break;
	}
	return nstate;
}


void swallow()
{
	int c;

	c = input();

	if (c == '#') {
		while ((c != '\n') && (c != EOF))
			c = input();
	}
	if (c != EOF)
		unput(c);
}
