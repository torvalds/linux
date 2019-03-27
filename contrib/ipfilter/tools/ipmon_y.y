/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
%{
#include "ipf.h"
#include <syslog.h>
#undef	OPT_NAT
#undef	OPT_VERBOSE
#include "ipmon_l.h"
#include "ipmon.h"

#include <dlfcn.h>

#define	YYDEBUG	1

extern	void	yyerror __P((char *));
extern	int	yyparse __P((void));
extern	int	yylex __P((void));
extern	int	yydebug;
extern	FILE	*yyin;
extern	int	yylineNum;
extern	int	ipmonopts;

typedef	struct	opt_s	{
	struct	opt_s	*o_next;
	int		o_line;
	int		o_type;
	int		o_num;
	char		*o_str;
	struct in_addr	o_ip;
	int		o_logfac;
	int		o_logpri;
} opt_t;

static	void	build_action __P((opt_t *, ipmon_doing_t *));
static	opt_t	*new_opt __P((int));
static	void	free_action __P((ipmon_action_t *));
static	void	print_action __P((ipmon_action_t *));
static	int	find_doing __P((char *));
static	ipmon_doing_t *build_doing __P((char *, char *));
static	void	print_match __P((ipmon_action_t *));
static	int	install_saver __P((char *, char *));

static	ipmon_action_t	*alist = NULL;

ipmon_saver_int_t	*saverlist = NULL;
%}

%union	{
	char	*str;
	u_32_t	num;
	struct in_addr	addr;
	struct opt_s	*opt;
	union	i6addr	ip6;
	struct ipmon_doing_s	*ipmd;
}

%token	<num>	YY_NUMBER YY_HEX
%token	<str>	YY_STR
%token	<ip6>	YY_IPV6
%token	YY_COMMENT
%token	YY_CMP_EQ YY_CMP_NE YY_CMP_LE YY_CMP_GE YY_CMP_LT YY_CMP_GT
%token	YY_RANGE_OUT YY_RANGE_IN

%token	IPM_MATCH IPM_BODY IPM_COMMENT IPM_DIRECTION IPM_DSTIP IPM_DSTPORT
%token	IPM_EVERY IPM_GROUP IPM_INTERFACE IPM_IN IPM_NO IPM_OUT IPM_LOADACTION
%token	IPM_PACKET IPM_PACKETS IPM_POOL IPM_PROTOCOL IPM_RESULT IPM_RULE
%token	IPM_SECOND IPM_SECONDS IPM_SRCIP IPM_SRCPORT IPM_LOGTAG IPM_WITH
%token	IPM_DO IPM_DOING IPM_TYPE IPM_NAT
%token	IPM_STATE IPM_NATTAG IPM_IPF
%type	<addr> ipv4
%type	<opt> direction dstip dstport every group interface
%type	<opt> protocol result rule srcip srcport logtag matching
%type	<opt> matchopt nattag type
%type	<num> typeopt
%type	<ipmd> doopt doing

%%
file:	action
	| file action
	;

action:	line ';'
	| assign ';'
	| IPM_COMMENT
	| YY_COMMENT
	;

line:	IPM_MATCH '{' matching ';' '}' IPM_DO '{' doing ';' '}'
						{ build_action($3, $8);
						  resetlexer();
						}
	| IPM_LOADACTION YY_STR YY_STR 	{ if (install_saver($2, $3))
						yyerror("install saver");
					}
	;

assign:	YY_STR assigning YY_STR 		{ set_variable($1, $3);
						  resetlexer();
						  free($1);
						  free($3);
						  yyvarnext = 0;
						}
	;

assigning:
	'='					{ yyvarnext = 1; }
	;

matching:
	matchopt				{ $$ = $1; }
	| matchopt ',' matching			{ $1->o_next = $3; $$ = $1; }
	;

matchopt:
	direction				{ $$ = $1; }
	| dstip					{ $$ = $1; }
	| dstport				{ $$ = $1; }
	| every					{ $$ = $1; }
	| group					{ $$ = $1; }
	| interface				{ $$ = $1; }
	| protocol				{ $$ = $1; }
	| result				{ $$ = $1; }
	| rule					{ $$ = $1; }
	| srcip					{ $$ = $1; }
	| srcport				{ $$ = $1; }
	| logtag				{ $$ = $1; }
	| nattag				{ $$ = $1; }
	| type					{ $$ = $1; }
	;

doing:
	doopt					{ $$ = $1; }
	| doopt ',' doing			{ $1->ipmd_next = $3; $$ = $1; }
	;

doopt:
	YY_STR				{ if (find_doing($1) != IPM_DOING)
						yyerror("unknown action");
					}
	'(' YY_STR ')'			{ $$ = build_doing($1, $4);
					  if ($$ == NULL)
						yyerror("action building");
					}
	| YY_STR			{ if (find_doing($1) == IPM_DOING)
						$$ = build_doing($1, NULL);
					}
	;

direction:
	IPM_DIRECTION '=' IPM_IN		{ $$ = new_opt(IPM_DIRECTION);
						  $$->o_num = IPM_IN; }
	| IPM_DIRECTION '=' IPM_OUT		{ $$ = new_opt(IPM_DIRECTION);
						  $$->o_num = IPM_OUT; }
	;

dstip:	IPM_DSTIP '=' ipv4 '/' YY_NUMBER	{ $$ = new_opt(IPM_DSTIP);
						  $$->o_ip = $3;
						  $$->o_num = $5; }
	;

dstport:
	IPM_DSTPORT '=' YY_NUMBER		{ $$ = new_opt(IPM_DSTPORT);
						  $$->o_num = $3; }
	| IPM_DSTPORT '=' YY_STR		{ $$ = new_opt(IPM_DSTPORT);
						  $$->o_str = $3; }
	;

every:	IPM_EVERY IPM_SECOND			{ $$ = new_opt(IPM_SECOND);
						  $$->o_num = 1; }
	| IPM_EVERY YY_NUMBER IPM_SECONDS	{ $$ = new_opt(IPM_SECOND);
						  $$->o_num = $2; }
	| IPM_EVERY IPM_PACKET			{ $$ = new_opt(IPM_PACKET);
						  $$->o_num = 1; }
	| IPM_EVERY YY_NUMBER IPM_PACKETS	{ $$ = new_opt(IPM_PACKET);
						  $$->o_num = $2; }
	;

group:	IPM_GROUP '=' YY_NUMBER			{ $$ = new_opt(IPM_GROUP);
						  $$->o_num = $3; }
	| IPM_GROUP '=' YY_STR			{ $$ = new_opt(IPM_GROUP);
						  $$->o_str = $3; }
	;

interface:
	IPM_INTERFACE '=' YY_STR		{ $$ = new_opt(IPM_INTERFACE);
						  $$->o_str = $3; }
	;

logtag:	IPM_LOGTAG '=' YY_NUMBER		{ $$ = new_opt(IPM_LOGTAG);
						  $$->o_num = $3; }
	;

nattag:	IPM_NATTAG '=' YY_STR			{ $$ = new_opt(IPM_NATTAG);
						  $$->o_str = $3; }
	;

protocol:
	IPM_PROTOCOL '=' YY_NUMBER		{ $$ = new_opt(IPM_PROTOCOL);
						  $$->o_num = $3; }
	| IPM_PROTOCOL '=' YY_STR		{ $$ = new_opt(IPM_PROTOCOL);
						  $$->o_num = getproto($3);
						  free($3);
						}
	;

result:	IPM_RESULT '=' YY_STR			{ $$ = new_opt(IPM_RESULT);
						  $$->o_str = $3; }
	;

rule:	IPM_RULE '=' YY_NUMBER			{ $$ = new_opt(IPM_RULE);
						  $$->o_num = YY_NUMBER; }
	;

srcip:	IPM_SRCIP '=' ipv4 '/' YY_NUMBER	{ $$ = new_opt(IPM_SRCIP);
						  $$->o_ip = $3;
						  $$->o_num = $5; }
	;

srcport:
	IPM_SRCPORT '=' YY_NUMBER		{ $$ = new_opt(IPM_SRCPORT);
						  $$->o_num = $3; }
	| IPM_SRCPORT '=' YY_STR		{ $$ = new_opt(IPM_SRCPORT);
						  $$->o_str = $3; }
	;

type:	IPM_TYPE '=' typeopt			{ $$ = new_opt(IPM_TYPE);
						  $$->o_num = $3; }
	;

typeopt:
	IPM_IPF					{ $$ = IPL_MAGIC; }
	| IPM_NAT				{ $$ = IPL_MAGIC_NAT; }
	| IPM_STATE				{ $$ = IPL_MAGIC_STATE; }
	;



ipv4:   YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER
		{ if ($1 > 255 || $3 > 255 || $5 > 255 || $7 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  $$.s_addr = ($1 << 24) | ($3 << 16) | ($5 << 8) | $7;
		  $$.s_addr = htonl($$.s_addr);
		}
%%
static	struct	wordtab	yywords[] = {
	{ "body",	IPM_BODY },
	{ "direction",	IPM_DIRECTION },
	{ "do",		IPM_DO },
	{ "dstip",	IPM_DSTIP },
	{ "dstport",	IPM_DSTPORT },
	{ "every",	IPM_EVERY },
	{ "group",	IPM_GROUP },
	{ "in",		IPM_IN },
	{ "interface",	IPM_INTERFACE },
	{ "ipf",	IPM_IPF },
	{ "load_action",IPM_LOADACTION },
	{ "logtag",	IPM_LOGTAG },
	{ "match",	IPM_MATCH },
	{ "nat",	IPM_NAT },
	{ "nattag",	IPM_NATTAG },
	{ "no",		IPM_NO },
	{ "out",	IPM_OUT },
	{ "packet",	IPM_PACKET },
	{ "packets",	IPM_PACKETS },
	{ "protocol",	IPM_PROTOCOL },
	{ "result",	IPM_RESULT },
	{ "rule",	IPM_RULE },
	{ "second",	IPM_SECOND },
	{ "seconds",	IPM_SECONDS },
	{ "srcip",	IPM_SRCIP },
	{ "srcport",	IPM_SRCPORT },
	{ "state",	IPM_STATE },
	{ "with",	IPM_WITH },
	{ NULL,		0 }
};

static int macflags[17][2] = {
	{ IPM_DIRECTION,	IPMAC_DIRECTION	},
	{ IPM_DSTIP,		IPMAC_DSTIP	},
	{ IPM_DSTPORT,		IPMAC_DSTPORT	},
	{ IPM_GROUP,		IPMAC_GROUP	},
	{ IPM_INTERFACE,	IPMAC_INTERFACE	},
	{ IPM_LOGTAG,		IPMAC_LOGTAG 	},
	{ IPM_NATTAG,		IPMAC_NATTAG 	},
	{ IPM_PACKET,		IPMAC_EVERY	},
	{ IPM_PROTOCOL,		IPMAC_PROTOCOL	},
	{ IPM_RESULT,		IPMAC_RESULT	},
	{ IPM_RULE,		IPMAC_RULE	},
	{ IPM_SECOND,		IPMAC_EVERY	},
	{ IPM_SRCIP,		IPMAC_SRCIP	},
	{ IPM_SRCPORT,		IPMAC_SRCPORT	},
	{ IPM_TYPE,		IPMAC_TYPE 	},
	{ IPM_WITH,		IPMAC_WITH 	},
	{ 0, 0 }
};

static opt_t *
new_opt(type)
	int type;
{
	opt_t *o;

	o = (opt_t *)calloc(1, sizeof(*o));
	o->o_type = type;
	o->o_line = yylineNum;
	o->o_logfac = -1;
	o->o_logpri = -1;
	return o;
}

static void
build_action(olist, todo)
	opt_t *olist;
	ipmon_doing_t *todo;
{
	ipmon_action_t *a;
	opt_t *o;
	int i;

	a = (ipmon_action_t *)calloc(1, sizeof(*a));
	if (a == NULL)
		return;

	while ((o = olist) != NULL) {
		/*
		 * Check to see if the same comparator is being used more than
		 * once per matching statement.
		 */
		for (i = 0; macflags[i][0]; i++)
			if (macflags[i][0] == o->o_type)
				break;
		if (macflags[i][1] & a->ac_mflag) {
			fprintf(stderr, "%s redfined on line %d\n",
				yykeytostr(o->o_type), yylineNum);
			if (o->o_str != NULL)
				free(o->o_str);
			olist = o->o_next;
			free(o);
			continue;
		}

		a->ac_mflag |= macflags[i][1];

		switch (o->o_type)
		{
		case IPM_DIRECTION :
			a->ac_direction = o->o_num;
			break;
		case IPM_DSTIP :
			a->ac_dip = o->o_ip.s_addr;
			a->ac_dmsk = htonl(0xffffffff << (32 - o->o_num));
			break;
		case IPM_DSTPORT :
			a->ac_dport = htons(o->o_num);
			break;
		case IPM_INTERFACE :
			a->ac_iface = o->o_str;
			o->o_str = NULL;
			break;
		case IPM_GROUP :
			if (o->o_str != NULL)
				strncpy(a->ac_group, o->o_str, FR_GROUPLEN);
			else
				sprintf(a->ac_group, "%d", o->o_num);
			break;
		case IPM_LOGTAG :
			a->ac_logtag = o->o_num;
			break;
		case IPM_NATTAG :
			strncpy(a->ac_nattag, o->o_str, sizeof(a->ac_nattag));
			break;
		case IPM_PACKET :
			a->ac_packet = o->o_num;
			break;
		case IPM_PROTOCOL :
			a->ac_proto = o->o_num;
			break;
		case IPM_RULE :
			a->ac_rule = o->o_num;
			break;
		case IPM_RESULT :
			if (!strcasecmp(o->o_str, "pass"))
				a->ac_result = IPMR_PASS;
			else if (!strcasecmp(o->o_str, "block"))
				a->ac_result = IPMR_BLOCK;
			else if (!strcasecmp(o->o_str, "nomatch"))
				a->ac_result = IPMR_NOMATCH;
			else if (!strcasecmp(o->o_str, "log"))
				a->ac_result = IPMR_LOG;
			break;
		case IPM_SECOND :
			a->ac_second = o->o_num;
			break;
		case IPM_SRCIP :
			a->ac_sip = o->o_ip.s_addr;
			a->ac_smsk = htonl(0xffffffff << (32 - o->o_num));
			break;
		case IPM_SRCPORT :
			a->ac_sport = htons(o->o_num);
			break;
		case IPM_TYPE :
			a->ac_type = o->o_num;
			break;
		case IPM_WITH :
			break;
		default :
			break;
		}

		olist = o->o_next;
		if (o->o_str != NULL)
			free(o->o_str);
		free(o);
	}

	a->ac_doing = todo;
	a->ac_next = alist;
	alist = a;

	if (ipmonopts & IPMON_VERBOSE)
		print_action(a);
}


int
check_action(buf, log, opts, lvl)
	char *buf, *log;
	int opts, lvl;
{
	ipmon_action_t *a;
	struct timeval tv;
	ipmon_doing_t *d;
	ipmon_msg_t msg;
	ipflog_t *ipf;
	tcphdr_t *tcp;
	iplog_t *ipl;
	int matched;
	u_long t1;
	ip_t *ip;

	matched = 0;
	ipl = (iplog_t *)buf;
	ipf = (ipflog_t *)(ipl +1);
	ip = (ip_t *)(ipf + 1);
	tcp = (tcphdr_t *)((char *)ip + (IP_HL(ip) << 2));

	msg.imm_data = ipl;
	msg.imm_dsize = ipl->ipl_dsize;
	msg.imm_when = ipl->ipl_time.tv_sec;
	msg.imm_msg = log;
	msg.imm_msglen = strlen(log);
	msg.imm_loglevel = lvl;

	for (a = alist; a != NULL; a = a->ac_next) {
		verbose(0, "== checking config rule\n");
		if ((a->ac_mflag & IPMAC_DIRECTION) != 0) {
			if (a->ac_direction == IPM_IN) {
				if ((ipf->fl_flags & FR_INQUE) == 0) {
					verbose(8, "-- direction not in\n");
					continue;
				}
			} else if (a->ac_direction == IPM_OUT) {
				if ((ipf->fl_flags & FR_OUTQUE) == 0) {
					verbose(8, "-- direction not out\n");
					continue;
				}
			}
		}

		if ((a->ac_type != 0) && (a->ac_type != ipl->ipl_magic)) {
			verbose(8, "-- type mismatch\n");
			continue;
		}

		if ((a->ac_mflag & IPMAC_EVERY) != 0) {
			gettimeofday(&tv, NULL);
			t1 = tv.tv_sec - a->ac_lastsec;
			if (tv.tv_usec <= a->ac_lastusec)
				t1--;
			if (a->ac_second != 0) {
				if (t1 < a->ac_second) {
					verbose(8, "-- too soon\n");
					continue;
				}
				a->ac_lastsec = tv.tv_sec;
				a->ac_lastusec = tv.tv_usec;
			}

			if (a->ac_packet != 0) {
				if (a->ac_pktcnt == 0)
					a->ac_pktcnt++;
				else if (a->ac_pktcnt == a->ac_packet) {
					a->ac_pktcnt = 0;
					verbose(8, "-- packet count\n");
					continue;
				} else {
					a->ac_pktcnt++;
					verbose(8, "-- packet count\n");
					continue;
				}
			}
		}

		if ((a->ac_mflag & IPMAC_DSTIP) != 0) {
			if ((ip->ip_dst.s_addr & a->ac_dmsk) != a->ac_dip) {
				verbose(8, "-- dstip wrong\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_DSTPORT) != 0) {
			if (ip->ip_p != IPPROTO_UDP &&
			    ip->ip_p != IPPROTO_TCP) {
				verbose(8, "-- not port protocol\n");
				continue;
			}
			if (tcp->th_dport != a->ac_dport) {
				verbose(8, "-- dport mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_GROUP) != 0) {
			if (strncmp(a->ac_group, ipf->fl_group,
				    FR_GROUPLEN) != 0) {
				verbose(8, "-- group mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_INTERFACE) != 0) {
			if (strcmp(a->ac_iface, ipf->fl_ifname)) {
				verbose(8, "-- ifname mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_PROTOCOL) != 0) {
			if (a->ac_proto != ip->ip_p) {
				verbose(8, "-- protocol mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_RESULT) != 0) {
			if ((ipf->fl_flags & FF_LOGNOMATCH) != 0) {
				if (a->ac_result != IPMR_NOMATCH) {
					verbose(8, "-- ff-flags mismatch\n");
					continue;
				}
			} else if (FR_ISPASS(ipf->fl_flags)) {
				if (a->ac_result != IPMR_PASS) {
					verbose(8, "-- pass mismatch\n");
					continue;
				}
			} else if (FR_ISBLOCK(ipf->fl_flags)) {
				if (a->ac_result != IPMR_BLOCK) {
					verbose(8, "-- block mismatch\n");
					continue;
				}
			} else {	/* Log only */
				if (a->ac_result != IPMR_LOG) {
					verbose(8, "-- log mismatch\n");
					continue;
				}
			}
		}

		if ((a->ac_mflag & IPMAC_RULE) != 0) {
			if (a->ac_rule != ipf->fl_rule) {
				verbose(8, "-- rule mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_SRCIP) != 0) {
			if ((ip->ip_src.s_addr & a->ac_smsk) != a->ac_sip) {
				verbose(8, "-- srcip mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_SRCPORT) != 0) {
			if (ip->ip_p != IPPROTO_UDP &&
			    ip->ip_p != IPPROTO_TCP) {
				verbose(8, "-- port protocol mismatch\n");
				continue;
			}
			if (tcp->th_sport != a->ac_sport) {
				verbose(8, "-- sport mismatch\n");
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_LOGTAG) != 0) {
			if (a->ac_logtag != ipf->fl_logtag) {
				verbose(8, "-- logtag %d != %d\n",
					a->ac_logtag, ipf->fl_logtag);
				continue;
			}
		}

		if ((a->ac_mflag & IPMAC_NATTAG) != 0) {
			if (strncmp(a->ac_nattag, ipf->fl_nattag.ipt_tag,
				    IPFTAG_LEN) != 0) {
				verbose(8, "-- nattag mismatch\n");
				continue;
			}
		}

		matched = 1;
		verbose(8, "++ matched\n");

		/*
		 * It matched so now perform the saves
		 */
		for (d = a->ac_doing; d != NULL; d = d->ipmd_next)
			(*d->ipmd_store)(d->ipmd_token, &msg);
	}

	return matched;
}


static void
free_action(a)
	ipmon_action_t *a;
{
	ipmon_doing_t *d;

	while ((d = a->ac_doing) != NULL) {
		a->ac_doing = d->ipmd_next;
		(*d->ipmd_saver->ims_destroy)(d->ipmd_token);
		free(d);
	}

	if (a->ac_iface != NULL) {
		free(a->ac_iface);
		a->ac_iface = NULL;
	}
	a->ac_next = NULL;
	free(a);
}


int
load_config(file)
	char *file;
{
	FILE *fp;
	char *s;

	unload_config();

	s = getenv("YYDEBUG");
	if (s != NULL)
		yydebug = atoi(s);
	else
		yydebug = 0;

	yylineNum = 1;

	(void) yysettab(yywords);

	fp = fopen(file, "r");
	if (!fp) {
		perror("load_config:fopen:");
		return -1;
	}
	yyin = fp;
	while (!feof(fp))
		yyparse();
	fclose(fp);
	return 0;
}


void
unload_config()
{
	ipmon_saver_int_t *sav, **imsip;
	ipmon_saver_t *is;
	ipmon_action_t *a;

	while ((a = alist) != NULL) {
		alist = a->ac_next;
		free_action(a);
	}

	/*
	 * Look for savers that have been added in dynamically from the
	 * configuration file.
	 */
	for (imsip = &saverlist; (sav = *imsip) != NULL; ) {
		if (sav->imsi_handle == NULL)
			imsip = &sav->imsi_next;
		else {
			dlclose(sav->imsi_handle);

			*imsip = sav->imsi_next;
			is = sav->imsi_stor;
			free(sav);

			free(is->ims_name);
			free(is);
		}
	}
}


void
dump_config()
{
	ipmon_action_t *a;

	for (a = alist; a != NULL; a = a->ac_next) {
		print_action(a);

		printf("#\n");
	}
}


static void
print_action(a)
	ipmon_action_t *a;
{
	ipmon_doing_t *d;

	printf("match { ");
	print_match(a);
	printf("; }\n");
	printf("do {");
	for (d = a->ac_doing; d != NULL; d = d->ipmd_next) {
		printf("%s", d->ipmd_saver->ims_name);
		if (d->ipmd_saver->ims_print != NULL) {
			printf("(\"");
			(*d->ipmd_saver->ims_print)(d->ipmd_token);
			printf("\")");
		}
		printf(";");
	}
	printf("};\n");
}


void *
add_doing(saver)
	ipmon_saver_t *saver;
{
	ipmon_saver_int_t *it;

	if (find_doing(saver->ims_name) == IPM_DOING)
		return NULL;

	it = calloc(1, sizeof(*it));
	if (it == NULL)
		return NULL;
	it->imsi_stor = saver;
	it->imsi_next = saverlist;
	saverlist = it;
	return it;
}


static int
find_doing(string)
	char *string;
{
	ipmon_saver_int_t *it;

	for (it = saverlist; it != NULL; it = it->imsi_next) {
		if (!strcmp(it->imsi_stor->ims_name, string))
			return IPM_DOING;
	}
	return 0;
}


static ipmon_doing_t *
build_doing(target, options)
	char *target;
	char *options;
{
	ipmon_saver_int_t *it;
	char *strarray[2];
	ipmon_doing_t *d, *d1;
	ipmon_action_t *a;
	ipmon_saver_t *save;

	d = calloc(1, sizeof(*d));
	if (d == NULL)
		return NULL;

	for (it = saverlist; it != NULL; it = it->imsi_next) {
		if (!strcmp(it->imsi_stor->ims_name, target))
			break;
	}
	if (it == NULL) {
		free(d);
		return NULL;
	}

	strarray[0] = options;
	strarray[1] = NULL;

	d->ipmd_token = (*it->imsi_stor->ims_parse)(strarray);
	if (d->ipmd_token == NULL) {
		free(d);
		return NULL;
	}

	save = it->imsi_stor;
	d->ipmd_saver = save;
	d->ipmd_store = it->imsi_stor->ims_store;

	/*
	 * Look for duplicate do-things that need to be dup'd
	 */
	for (a = alist; a != NULL; a = a->ac_next) {
		for (d1 = a->ac_doing; d1 != NULL; d1 = d1->ipmd_next) {
			if (save != d1->ipmd_saver)
				continue;
			if (save->ims_match == NULL || save->ims_dup == NULL)
				continue;
			if ((*save->ims_match)(d->ipmd_token, d1->ipmd_token))
				continue;

			(*d->ipmd_saver->ims_destroy)(d->ipmd_token);
			d->ipmd_token = (*save->ims_dup)(d1->ipmd_token);
			break;
		}
	}

	return d;
}


static void
print_match(a)
	ipmon_action_t *a;
{
	char *coma = "";

	if ((a->ac_mflag & IPMAC_DIRECTION) != 0) {
		printf("direction = ");
		if (a->ac_direction == IPM_IN)
			printf("in");
		else if (a->ac_direction == IPM_OUT)
			printf("out");
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_DSTIP) != 0) {
		printf("%sdstip = ", coma);
		printhostmask(AF_INET, &a->ac_dip, &a->ac_dmsk);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_DSTPORT) != 0) {
		printf("%sdstport = %hu", coma, ntohs(a->ac_dport));
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_GROUP) != 0) {
		char group[FR_GROUPLEN+1];

		strncpy(group, a->ac_group, FR_GROUPLEN);
		group[FR_GROUPLEN] = '\0';
		printf("%sgroup = %s", coma, group);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_INTERFACE) != 0) {
		printf("%siface = %s", coma, a->ac_iface);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_LOGTAG) != 0) {
		printf("%slogtag = %u", coma, a->ac_logtag);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_NATTAG) != 0) {
		char tag[17];

		strncpy(tag, a->ac_nattag, 16);
		tag[16] = '\0';
		printf("%snattag = %s", coma, tag);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_PROTOCOL) != 0) {
		printf("%sprotocol = %u", coma, a->ac_proto);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_RESULT) != 0) {
		printf("%sresult = ", coma);
		switch (a->ac_result)
		{
		case IPMR_LOG :
			printf("log");
			break;
		case IPMR_PASS :
			printf("pass");
			break;
		case IPMR_BLOCK :
			printf("block");
			break;
		case IPMR_NOMATCH :
			printf("nomatch");
			break;
		}
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_RULE) != 0) {
		printf("%srule = %u", coma, a->ac_rule);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_EVERY) != 0) {
		if (a->ac_packet > 1) {
			printf("%severy %d packets", coma, a->ac_packet);
			coma = ", ";
		} else if (a->ac_packet == 1) {
			printf("%severy packet", coma);
			coma = ", ";
		}
		if (a->ac_second > 1) {
			printf("%severy %d seconds", coma, a->ac_second);
			coma = ", ";
		} else if (a->ac_second == 1) {
			printf("%severy second", coma);
			coma = ", ";
		}
	}

	if ((a->ac_mflag & IPMAC_SRCIP) != 0) {
		printf("%ssrcip = ", coma);
		printhostmask(AF_INET, &a->ac_sip, &a->ac_smsk);
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_SRCPORT) != 0) {
		printf("%ssrcport = %hu", coma, ntohs(a->ac_sport));
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_TYPE) != 0) {
		printf("%stype = ", coma);
		switch (a->ac_type)
		{
		case IPL_LOGIPF :
			printf("ipf");
			break;
		case IPL_LOGSTATE :
			printf("state");
			break;
		case IPL_LOGNAT :
			printf("nat");
			break;
		}
		coma = ", ";
	}

	if ((a->ac_mflag & IPMAC_WITH) != 0) {
		printf("%swith ", coma);
		coma = ", ";
	}
}


static int
install_saver(name, path)
	char *name, *path;
{
	ipmon_saver_int_t *isi;
	ipmon_saver_t *is;
	char nbuf[80];

	if (find_doing(name) == IPM_DOING)
		return -1;

	isi = calloc(1, sizeof(*isi));
	if (isi == NULL)
		return -1;

	is = calloc(1, sizeof(*is));
	if (is == NULL)
		goto loaderror;

	is->ims_name = name;

#ifdef RTLD_LAZY
	isi->imsi_handle = dlopen(path, RTLD_LAZY);
#endif
#ifdef DL_LAZY
	isi->imsi_handle = dlopen(path, DL_LAZY);
#endif

	if (isi->imsi_handle == NULL)
		goto loaderror;

	snprintf(nbuf, sizeof(nbuf), "%sdup", name);
	is->ims_dup = (ims_dup_func_t)dlsym(isi->imsi_handle, nbuf);

	snprintf(nbuf, sizeof(nbuf), "%sdestroy", name);
	is->ims_destroy = (ims_destroy_func_t)dlsym(isi->imsi_handle, nbuf);
	if (is->ims_destroy == NULL)
		goto loaderror;

	snprintf(nbuf, sizeof(nbuf), "%smatch", name);
	is->ims_match = (ims_match_func_t)dlsym(isi->imsi_handle, nbuf);

	snprintf(nbuf, sizeof(nbuf), "%sparse", name);
	is->ims_parse = (ims_parse_func_t)dlsym(isi->imsi_handle, nbuf);
	if (is->ims_parse == NULL)
		goto loaderror;

	snprintf(nbuf, sizeof(nbuf), "%sprint", name);
	is->ims_print = (ims_print_func_t)dlsym(isi->imsi_handle, nbuf);
	if (is->ims_print == NULL)
		goto loaderror;

	snprintf(nbuf, sizeof(nbuf), "%sstore", name);
	is->ims_store = (ims_store_func_t)dlsym(isi->imsi_handle, nbuf);
	if (is->ims_store == NULL)
		goto loaderror;

	isi->imsi_stor = is;
	isi->imsi_next = saverlist;
	saverlist = isi;

	return 0;

loaderror:
	if (isi->imsi_handle != NULL)
		dlclose(isi->imsi_handle);
	free(isi);
	if (is != NULL)
		free(is);
	return -1;
}
