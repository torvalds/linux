/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
%{
#include <sys/types.h>
#include <sys/ioctl.h>
#include "ipf.h"
#include "opts.h"
#include "kmem.h"
#include "ipscan_l.h"
#include "netinet/ip_scan.h"
#include <ctype.h>

#define	YYDEBUG	1

extern	char	*optarg;
extern	void	yyerror __P((char *));
extern	int	yyparse __P((void));
extern	int	yylex __P((void));
extern	int	yydebug;
extern	FILE	*yyin;
extern	int	yylineNum;
extern	void	printbuf __P((char *, int, int));


void		printent __P((ipscan_t *));
void		showlist __P((void));
int		getportnum __P((char *));
struct in_addr	gethostip __P((char *));
struct in_addr	combine __P((int, int, int, int));
char		**makepair __P((char *, char *));
void		addtag __P((char *, char **, char **, struct action *));
int		cram __P((char *, char *));
void		usage __P((char *));
int		main __P((int, char **));

int		opts = 0;
int		fd = -1;


%}

%union	{
	char	*str;
	char	**astr;
	u_32_t	num;
	struct	in_addr	ipa;
	struct	action	act;
	union	i6addr	ip6;
}

%type	<str> tag
%type	<act> action redirect result
%type	<ipa> ipaddr
%type	<num> portnum
%type	<astr> matchup onehalf twohalves

%token  <num>   YY_NUMBER YY_HEX
%token  <str>   YY_STR
%token          YY_COMMENT
%token          YY_CMP_EQ YY_CMP_NE YY_CMP_LE YY_CMP_GE YY_CMP_LT YY_CMP_GT
%token          YY_RANGE_OUT YY_RANGE_IN
%token  <ip6>   YY_IPV6
%token		IPSL_START IPSL_STARTGROUP IPSL_CONTENT

%token	IPSL_CLOSE IPSL_TRACK IPSL_EOF IPSL_REDIRECT IPSL_ELSE

%%
file:	line ';'
	| assign ';'
	| file line ';'
	| file assign ';'
	| YY_COMMENT
	;

line:	IPSL_START dline
	| IPSL_STARTGROUP gline
	| IPSL_CONTENT oline
	;

dline:	cline					{ resetlexer(); }
	| sline					{ resetlexer(); }
	| csline				{ resetlexer(); }
	;

gline:	YY_STR ':' glist '=' action
	;

oline:	cline
	| sline
	| csline
	;

assign:	YY_STR assigning YY_STR
						{ set_variable($1, $3);
						  resetlexer();
						  free($1);
						  free($3);
						  yyvarnext = 0;
						}
	;

assigning:
	'='					{ yyvarnext = 1; }
	;

cline:	tag ':' matchup '=' action		{ addtag($1, $3, NULL, &$5); }
	;

sline:	tag ':' '(' ')' ',' matchup '=' action	{ addtag($1, NULL, $6, &$8); }
	;

csline:	tag ':' matchup ',' matchup '=' action	{ addtag($1, $3, $5, &$7); }
	;

glist:	YY_STR
	| glist ',' YY_STR
	;

tag:	YY_STR					{ $$ = $1; }
	;

matchup:
	onehalf					{ $$ = $1; }
	| twohalves				{ $$ = $1; }
	;

action:	result				{ $$.act_val = $1.act_val;
					  $$.act_ip = $1.act_ip;
					  $$.act_port = $1.act_port; }
	| result IPSL_ELSE result	{ $$.act_val = $1.act_val;
					  $$.act_else = $3.act_val;
					  if ($1.act_val == IPSL_REDIRECT) {
						  $$.act_ip = $1.act_ip;
						  $$.act_port = $1.act_port;
					  }
					  if ($3.act_val == IPSL_REDIRECT) {
						  $$.act_eip = $3.act_eip;
						  $$.act_eport = $3.act_eport;
					  }
					}

result:	IPSL_CLOSE				{ $$.act_val = IPSL_CLOSE; }
	| IPSL_TRACK				{ $$.act_val = IPSL_TRACK; }
	| redirect				{ $$.act_val = IPSL_REDIRECT;
						  $$.act_ip = $1.act_ip;
						  $$.act_port = $1.act_port; }
	;

onehalf:
	'(' YY_STR ')'			{ $$ = makepair($2, NULL); }
	;

twohalves:
	'(' YY_STR ',' YY_STR ')'	{ $$ = makepair($2, $4); }
	;

redirect:
	IPSL_REDIRECT '(' ipaddr ')'		{ $$.act_ip = $3;
						  $$.act_port = 0; }
	| IPSL_REDIRECT '(' ipaddr ',' portnum ')'
						{ $$.act_ip = $3;
						  $$.act_port = $5; }
	;


ipaddr:	YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER
						{ $$ = combine($1,$3,$5,$7); }
	| YY_STR				{ $$ = gethostip($1);
						  free($1);
						}
	;

portnum:
	YY_NUMBER				{ $$ = htons($1); }
	| YY_STR				{ $$ = getportnum($1);
						  free($1);
						}
	;

%%


static	struct	wordtab	yywords[] = {
	{ "close",		IPSL_CLOSE },
	{ "content",		IPSL_CONTENT },
	{ "else",		IPSL_ELSE },
	{ "start-group",	IPSL_STARTGROUP },
	{ "redirect",		IPSL_REDIRECT },
	{ "start",		IPSL_START },
	{ "track",		IPSL_TRACK },
	{ NULL,		0 }
};


int cram(dst, src)
char *dst;
char *src;
{
	char c, *s, *t, *u;
	int i, j, k;

	c = *src;
	s = src + 1;
	t = strchr(s, c);
	*t = '\0';
	for (u = dst, i = 0; (i <= ISC_TLEN) && (s < t); ) {
		c = *s++;
		if (c == '\\') {
			if (s >= t)
				break;
			j = k = 0;
			do {
				c = *s++;
				if (j && (!ISDIGIT(c) || (c > '7') ||
				     (k >= 248))) {
					*u++ = k, i++;
					j = k = 0;
					s--;
					break;
				}
				i++;

				if (ISALPHA(c) || (c > '7')) {
					switch (c)
					{
					case 'n' :
						*u++ = '\n';
						break;
					case 'r' :
						*u++ = '\r';
						break;
					case 't' :
						*u++ = '\t';
						break;
					default :
						*u++ = c;
						break;
					}
				} else if (ISDIGIT(c)) {
					j = 1;
					k <<= 3;
					k |= (c - '0');
					i--;
				} else
						*u++ = c;
			} while ((i <= ISC_TLEN) && (s <= t) && (j > 0));
		} else
			*u++ = c, i++;
	}
	return i;
}


void printent(isc)
ipscan_t *isc;
{
	char buf[ISC_TLEN+1];
	u_char *u;
	int i, j;

	buf[ISC_TLEN] = '\0';
	bcopy(isc->ipsc_ctxt, buf, ISC_TLEN);
	printf("%s : (\"", isc->ipsc_tag);
	printbuf(isc->ipsc_ctxt, isc->ipsc_clen, 0);

	bcopy(isc->ipsc_cmsk, buf, ISC_TLEN);
	printf("\", \"%s\"), (\"", buf);

	printbuf(isc->ipsc_stxt, isc->ipsc_slen, 0);

	bcopy(isc->ipsc_smsk, buf, ISC_TLEN);
	printf("\", \"%s\") = ", buf);

	switch (isc->ipsc_action)
	{
	case ISC_A_TRACK :
		printf("track");
		break;
	case ISC_A_REDIRECT :
		printf("redirect");
		printf("(%s", inet_ntoa(isc->ipsc_ip));
		if (isc->ipsc_port)
			printf(",%d", isc->ipsc_port);
		printf(")");
		break;
	case ISC_A_CLOSE :
		printf("close");
		break;
	default :
		break;
	}

	if (isc->ipsc_else != ISC_A_NONE) {
		printf(" else ");
		switch (isc->ipsc_else)
		{
		case ISC_A_TRACK :
			printf("track");
			break;
		case ISC_A_REDIRECT :
			printf("redirect");
			printf("(%s", inet_ntoa(isc->ipsc_eip));
			if (isc->ipsc_eport)
				printf(",%d", isc->ipsc_eport);
			printf(")");
			break;
		case ISC_A_CLOSE :
			printf("close");
			break;
		default :
			break;
		}
	}
	printf("\n");

	if (opts & OPT_DEBUG) {
		for (u = (u_char *)isc, i = sizeof(*isc); i; ) {
			printf("#");
			for (j = 32; (j > 0) && (i > 0); j--, i--)
				printf("%s%02x", (j & 7) ? "" : " ", *u++);
			printf("\n");
		}
	}
	if (opts & OPT_VERBOSE) {
		printf("# hits %d active %d fref %d sref %d\n",
			isc->ipsc_hits, isc->ipsc_active, isc->ipsc_fref,
			isc->ipsc_sref);
	}
}


void addtag(tstr, cp, sp, act)
char *tstr;
char **cp, **sp;
struct action *act;
{
	ipscan_t isc, *iscp;

	bzero((char *)&isc, sizeof(isc));

	strncpy(isc.ipsc_tag, tstr, sizeof(isc.ipsc_tag));
	isc.ipsc_tag[sizeof(isc.ipsc_tag) - 1] = '\0';

	if (cp) {
		isc.ipsc_clen = cram(isc.ipsc_ctxt, cp[0]);
		if (cp[1]) {
			if (cram(isc.ipsc_cmsk, cp[1]) != isc.ipsc_clen) {
				fprintf(stderr,
					"client text/mask strings different length\n");
				return;
			}
		}
	}

	if (sp) {
		isc.ipsc_slen = cram(isc.ipsc_stxt, sp[0]);
		if (sp[1]) {
			if (cram(isc.ipsc_smsk, sp[1]) != isc.ipsc_slen) {
				fprintf(stderr,
					"server text/mask strings different length\n");
				return;
			}
		}
	}

	if (act->act_val == IPSL_CLOSE) {
		isc.ipsc_action = ISC_A_CLOSE;
	} else if (act->act_val == IPSL_TRACK) {
		isc.ipsc_action = ISC_A_TRACK;
	} else if (act->act_val == IPSL_REDIRECT) {
		isc.ipsc_action = ISC_A_REDIRECT;
		isc.ipsc_ip = act->act_ip;
		isc.ipsc_port = act->act_port;
		fprintf(stderr, "%d: redirect unsupported\n", yylineNum + 1);
	}

	if (act->act_else == IPSL_CLOSE) {
		isc.ipsc_else = ISC_A_CLOSE;
	} else if (act->act_else == IPSL_TRACK) {
		isc.ipsc_else = ISC_A_TRACK;
	} else if (act->act_else == IPSL_REDIRECT) {
		isc.ipsc_else = ISC_A_REDIRECT;
		isc.ipsc_eip = act->act_eip;
		isc.ipsc_eport = act->act_eport;
		fprintf(stderr, "%d: redirect unsupported\n", yylineNum + 1);
	}

	if (!(opts & OPT_DONOTHING)) {
		iscp = &isc;
		if (opts & OPT_REMOVE) {
			if (ioctl(fd, SIOCRMSCA, &iscp) == -1)
				perror("SIOCADSCA");
		} else {
			if (ioctl(fd, SIOCADSCA, &iscp) == -1)
				perror("SIOCADSCA");
		}
	}

	if (opts & OPT_VERBOSE)
		printent(&isc);
}


char **makepair(s1, s2)
char *s1, *s2;
{
	char **a;

	a = malloc(sizeof(char *) * 2);
	a[0] = s1;
	a[1] = s2;
	return a;
}


struct in_addr combine(a1, a2, a3, a4)
int a1, a2, a3, a4;
{
	struct in_addr in;

	a1 &= 0xff;
	in.s_addr = a1 << 24;
	a2 &= 0xff;
	in.s_addr |= (a2 << 16);
	a3 &= 0xff;
	in.s_addr |= (a3 << 8);
	a4 &= 0xff;
	in.s_addr |= a4;
	in.s_addr = htonl(in.s_addr);
	return in;
}


struct in_addr gethostip(host)
char *host;
{
	struct hostent *hp;
	struct in_addr in;

	in.s_addr = 0;

	hp = gethostbyname(host);
	if (!hp)
		return in;
	bcopy(hp->h_addr, (char *)&in, sizeof(in));
	return in;
}


int getportnum(port)
char *port;
{
	struct servent *s;

	s = getservbyname(port, "tcp");
	if (s == NULL)
		return -1;
	return s->s_port;
}


void showlist()
{
	ipscanstat_t ipsc, *ipscp = &ipsc;
	ipscan_t isc;

	if (ioctl(fd, SIOCGSCST, &ipscp) == -1)
		perror("ioctl(SIOCGSCST)");
	else if (opts & OPT_SHOWLIST) {
		while (ipsc.iscs_list != NULL) {
			if (kmemcpy((char *)&isc, (u_long)ipsc.iscs_list,
				    sizeof(isc)) == -1) {
				perror("kmemcpy");
				break;
			} else {
				printent(&isc);
				ipsc.iscs_list = isc.ipsc_next;
			}
		}
	} else {
		printf("scan entries loaded\t%d\n", ipsc.iscs_entries);
		printf("scan entries matches\t%ld\n", ipsc.iscs_acted);
		printf("negative matches\t%ld\n", ipsc.iscs_else);
	}
}


void usage(prog)
char *prog;
{
	fprintf(stderr, "Usage:\t%s [-dnrv] -f <filename>\n", prog);
	fprintf(stderr, "\t%s [-dlv]\n", prog);
	exit(1);
}


int main(argc, argv)
int argc;
char *argv[];
{
	FILE *fp = NULL;
	int c;

	(void) yysettab(yywords);

	if (argc < 2)
		usage(argv[0]);

	while ((c = getopt(argc, argv, "df:lnrsv")) != -1)
		switch (c)
		{
		case 'd' :
			opts |= OPT_DEBUG;
			yydebug++;
			break;
		case 'f' :
			if (!strcmp(optarg, "-"))
				fp = stdin;
			else {
				fp = fopen(optarg, "r");
				if (!fp) {
					perror("open");
					exit(1);
				}
			}
			yyin = fp;
			break;
		case 'l' :
			opts |= OPT_SHOWLIST;
			break;
		case 'n' :
			opts |= OPT_DONOTHING;
			break;
		case 'r' :
			opts |= OPT_REMOVE;
			break;
		case 's' :
			opts |= OPT_STAT;
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		}

	if (!(opts & OPT_DONOTHING)) {
		fd = open(IPL_SCAN, O_RDWR);
		if (fd == -1) {
			perror("open(IPL_SCAN)");
			exit(1);
		}
	}

	if (fp != NULL) {
		yylineNum = 1;

		while (!feof(fp))
			yyparse();
		fclose(fp);
		exit(0);
	}

	if (opts & (OPT_SHOWLIST|OPT_STAT)) {
		showlist();
		exit(0);
	}
	exit(1);
}
