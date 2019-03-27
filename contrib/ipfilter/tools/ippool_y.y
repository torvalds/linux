/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
%{
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/socket.h>
# include <sys/cdefs.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <ctype.h>
#include <unistd.h>

#include "ipf.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"
#include "netinet/ip_htable.h"
#include "netinet/ip_dstlist.h"
#include "ippool_l.h"
#include "kmem.h"

#define	YYDEBUG	1
#define	YYSTACKSIZE	0x00ffffff

extern	int	yyparse __P((void));
extern	int	yydebug;
extern	FILE	*yyin;

static	iphtable_t	ipht;
static	iphtent_t	iphte;
static	ip_pool_t	iplo;
static	ippool_dst_t	ipld;
static	ioctlfunc_t	poolioctl = NULL;
static	char		poolname[FR_GROUPLEN];

static iphtent_t *add_htablehosts __P((char *));
static ip_pool_node_t *add_poolhosts __P((char *));
static ip_pool_node_t *read_whoisfile __P((char *));
static void setadflen __P((addrfamily_t *));

%}

%union	{
	char	*str;
	u_32_t	num;
	struct	in_addr	ip4;
	struct	alist_s	*alist;
	addrfamily_t	adrmsk[2];
	iphtent_t	*ipe;
	ip_pool_node_t	*ipp;
	ipf_dstnode_t	*ipd;
	addrfamily_t	ipa;
	i6addr_t	ip6;
}

%token  <num>	YY_NUMBER YY_HEX
%token  <str>	YY_STR
%token  <ip6>	YY_IPV6
%token	YY_COMMENT
%token	YY_CMP_EQ YY_CMP_NE YY_CMP_LE YY_CMP_GE YY_CMP_LT YY_CMP_GT
%token	YY_RANGE_OUT YY_RANGE_IN
%token	IPT_IPF IPT_NAT IPT_COUNT IPT_AUTH IPT_IN IPT_OUT IPT_ALL
%token	IPT_TABLE IPT_GROUPMAP IPT_HASH IPT_SRCHASH IPT_DSTHASH
%token	IPT_ROLE IPT_TYPE IPT_TREE
%token	IPT_GROUP IPT_SIZE IPT_SEED IPT_NUM IPT_NAME IPT_POLICY
%token	IPT_POOL IPT_DSTLIST IPT_ROUNDROBIN
%token	IPT_WEIGHTED IPT_RANDOM IPT_CONNECTION
%token	IPT_WHOIS IPT_FILE
%type	<num> role table inout unit dstopts weighting
%type	<ipp> ipftree range addrlist
%type	<adrmsk> addrmask
%type	<ipe> ipfgroup ipfhash hashlist hashentry
%type	<ipe> groupentry setgrouplist grouplist
%type	<ipa> ipaddr mask
%type	<ip4> ipv4
%type	<str> number setgroup name
%type	<ipd> dstentry dstentries dstlist

%%
file:	line
	| assign
	| file line
	| file assign
	;

line:	table role ipftree eol		{ ip_pool_node_t *n;
					  iplo.ipo_unit = $2;
					  iplo.ipo_list = $3;
					  load_pool(&iplo, poolioctl);
					  while ((n = $3) != NULL) {
						$3 = n->ipn_next;
						free(n);
					  }
					  resetlexer();
					  use_inet6 = 0;
					}
	| table role ipfhash eol	{ iphtent_t *h;
					  ipht.iph_unit = $2;
					  ipht.iph_type = IPHASH_LOOKUP;
					  load_hash(&ipht, $3, poolioctl);
					  while ((h = $3) != NULL) {
						$3 = h->ipe_next;
						free(h);
					  }
					  resetlexer();
					  use_inet6 = 0;
					}
	| groupmap role number ipfgroup eol
					{ iphtent_t *h;
					  ipht.iph_unit = $2;
					  strncpy(ipht.iph_name, $3,
						  sizeof(ipht.iph_name));
					  ipht.iph_type = IPHASH_GROUPMAP;
					  load_hash(&ipht, $4, poolioctl);
					  while ((h = $4) != NULL) {
						$4 = h->ipe_next;
						free(h);
					  }
					  resetlexer();
					  use_inet6 = 0;
					}
	| YY_COMMENT
	| poolline eol
	;

eol:	';'
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

table:	IPT_TABLE		{ bzero((char *)&ipht, sizeof(ipht));
				  bzero((char *)&iphte, sizeof(iphte));
				  bzero((char *)&iplo, sizeof(iplo));
				  bzero((char *)&ipld, sizeof(ipld));
				  *ipht.iph_name = '\0';
				  iplo.ipo_flags = IPHASH_ANON;
				  iplo.ipo_name[0] = '\0';
				}
	;

groupmap:
	IPT_GROUPMAP inout	{ bzero((char *)&ipht, sizeof(ipht));
				  bzero((char *)&iphte, sizeof(iphte));
				  *ipht.iph_name = '\0';
				  ipht.iph_unit = IPHASH_GROUPMAP;
				  ipht.iph_flags = $2;
				}
	;

inout:	IPT_IN				{ $$ = FR_INQUE; }
	| IPT_OUT			{ $$ = FR_OUTQUE; }
	;

role:	IPT_ROLE '=' unit		{ $$ = $3; }
	;

unit:	IPT_IPF				{ $$ = IPL_LOGIPF; }
	| IPT_NAT			{ $$ = IPL_LOGNAT; }
	| IPT_AUTH			{ $$ = IPL_LOGAUTH; }
	| IPT_COUNT			{ $$ = IPL_LOGCOUNT; }
	| IPT_ALL			{ $$ = IPL_LOGALL; }
	;

ipftree:
	IPT_TYPE '=' IPT_TREE number start addrlist end
					{ strncpy(iplo.ipo_name, $4,
						  sizeof(iplo.ipo_name));
					  $$ = $6;
					}
	;

ipfhash:
	IPT_TYPE '=' IPT_HASH number hashopts start hashlist end
					{ strncpy(ipht.iph_name, $4,
						  sizeof(ipht.iph_name));
					  $$ = $7;
					}
	;

ipfgroup:
	setgroup hashopts start grouplist end
					{ iphtent_t *e;
					  for (e = $4; e != NULL;
					       e = e->ipe_next)
						if (e->ipe_group[0] == '\0')
							strncpy(e->ipe_group,
								$1,
								FR_GROUPLEN);
					  $$ = $4;
					  free($1);
					}
	| hashopts start setgrouplist end
					{ $$ = $3; }
	;

number:	IPT_NUM '=' YY_NUMBER			{ sprintf(poolname, "%u", $3);
						  $$ = poolname;
						}
	| IPT_NAME '=' YY_STR			{ strncpy(poolname, $3,
							  FR_GROUPLEN);
						  poolname[FR_GROUPLEN-1]='\0';
						  free($3);
						  $$ = poolname;
						}
	|					{ $$ = ""; }
	;

setgroup:
	IPT_GROUP '=' YY_STR		{ char tmp[FR_GROUPLEN+1];
					  strncpy(tmp, $3, FR_GROUPLEN);
					  $$ = strdup(tmp);
					  free($3);
					}
	| IPT_GROUP '=' YY_NUMBER	{ char tmp[FR_GROUPLEN+1];
					  sprintf(tmp, "%u", $3);
					  $$ = strdup(tmp);
					}
	;

hashopts:
	| size
	| seed
	| size seed
	;

addrlist:
	';'				{ $$ = NULL; }
	| range next addrlist		{ $$ = $1;
					  while ($1->ipn_next != NULL)
						$1 = $1->ipn_next;
					  $1->ipn_next = $3;
					}
	| range next			{ $$ = $1; }
	;

grouplist:
	';'				{ $$ = NULL; }
	| groupentry next grouplist	{ $$ = $1; $1->ipe_next = $3; }
	| addrmask next grouplist	{ $$ = calloc(1, sizeof(iphtent_t));
					  $$->ipe_addr = $1[0].adf_addr;
					  $$->ipe_mask = $1[1].adf_addr;
					  $$->ipe_family = $1[0].adf_family;
					  $$->ipe_next = $3;
					}
	| groupentry next		{ $$ = $1; }
	| addrmask next			{ $$ = calloc(1, sizeof(iphtent_t));
					  $$->ipe_addr = $1[0].adf_addr;
					  $$->ipe_mask = $1[1].adf_addr;
#ifdef USE_INET6
					  if (use_inet6)
						$$->ipe_family = AF_INET6;
					  else
#endif
						$$->ipe_family = AF_INET;
					}
	| YY_STR			{ $$ = add_htablehosts($1);
					  free($1);
					}
	;

setgrouplist:
	';'				{ $$ = NULL; }
	| groupentry next		{ $$ = $1; }
	| groupentry next setgrouplist	{ $1->ipe_next = $3; $$ = $1; }
	;

groupentry:
	addrmask ',' setgroup		{ $$ = calloc(1, sizeof(iphtent_t));
					  $$->ipe_addr = $1[0].adf_addr;
					  $$->ipe_mask = $1[1].adf_addr;
					  strncpy($$->ipe_group, $3,
						  FR_GROUPLEN);
#ifdef USE_INET6
					  if (use_inet6)
						$$->ipe_family = AF_INET6;
					  else
#endif
						$$->ipe_family = AF_INET;
					  free($3);
					}
	;

range:	addrmask			{ $$ = calloc(1, sizeof(*$$));
					  $$->ipn_info = 0;
					  $$->ipn_addr = $1[0];
					  $$->ipn_mask = $1[1];
					}
	| '!' addrmask			{ $$ = calloc(1, sizeof(*$$));
					  $$->ipn_info = 1;
					  $$->ipn_addr = $2[0];
					  $$->ipn_mask = $2[1];
					}
	| YY_STR			{ $$ = add_poolhosts($1);
					  free($1);
					}
	| IPT_WHOIS IPT_FILE YY_STR	{ $$ = read_whoisfile($3);
					  free($3);
					}
	;

hashlist:
	';'				{ $$ = NULL; }
	| hashentry next		{ $$ = $1; }
	| hashentry next hashlist	{ $1->ipe_next = $3; $$ = $1; }
	;

hashentry:
	addrmask 		{ $$ = calloc(1, sizeof(iphtent_t));
				  $$->ipe_addr = $1[0].adf_addr;
				  $$->ipe_mask = $1[1].adf_addr;
#ifdef USE_INET6
				  if (use_inet6)
					$$->ipe_family = AF_INET6;
				  else
#endif
					$$->ipe_family = AF_INET;
				}
	| YY_STR		{ $$ = add_htablehosts($1);
				  free($1);
				}
	;

addrmask:
	ipaddr '/' mask		{ $$[0] = $1;
				  setadflen(&$$[0]);
				  $$[1] = $3;
				  $$[1].adf_len = $$[0].adf_len;
				}
	| ipaddr		{ $$[0] = $1;
				  setadflen(&$$[1]);
				  $$[1].adf_len = $$[0].adf_len;
#ifdef USE_INET6
				  if (use_inet6)
					memset(&$$[1].adf_addr, 0xff,
					       sizeof($$[1].adf_addr.in6));
				  else
#endif
					memset(&$$[1].adf_addr, 0xff, 
					       sizeof($$[1].adf_addr.in4));
				}
	;

ipaddr:	ipv4			{ $$.adf_addr.in4 = $1;
				  $$.adf_family = AF_INET;
				  setadflen(&$$);
				  use_inet6 = 0;
				}
	| YY_NUMBER		{ $$.adf_addr.in4.s_addr = htonl($1);
				  $$.adf_family = AF_INET;
				  setadflen(&$$);
				  use_inet6 = 0;
				}
	| YY_IPV6		{ $$.adf_addr = $1;
				  $$.adf_family = AF_INET6;
				  setadflen(&$$);
				  use_inet6 = 1;
				}
	;

mask:	YY_NUMBER	{ bzero(&$$, sizeof($$));
			  if (use_inet6) {
				if (ntomask(AF_INET6, $1,
					    (u_32_t *)&$$.adf_addr) == -1)
					yyerror("bad bitmask");
			  } else {
				if (ntomask(AF_INET, $1,
					    (u_32_t *)&$$.adf_addr.in4) == -1)
					yyerror("bad bitmask");
			  }
			}
	| ipv4		{ bzero(&$$, sizeof($$));
			  $$.adf_addr.in4 = $1;
			}
	| YY_IPV6	{ bzero(&$$, sizeof($$));
			  $$.adf_addr = $1;
			}
	;

size:	IPT_SIZE '=' YY_NUMBER		{ ipht.iph_size = $3; }
	;

seed:	IPT_SEED '=' YY_NUMBER		{ ipht.iph_seed = $3; }
	;

ipv4:	YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER '.' YY_NUMBER
		{ if ($1 > 255 || $3 > 255 || $5 > 255 || $7 > 255) {
			yyerror("Invalid octet string for IP address");
			return 0;
		  }
		  $$.s_addr = ($1 << 24) | ($3 << 16) | ($5 << 8) | $7;
		  $$.s_addr = htonl($$.s_addr);
		}
	;

next:	';'				{ yyexpectaddr = 1; }
	;

start:	'{'				{ yyexpectaddr = 1; }
	;

end:	'}'				{ yyexpectaddr = 0; }
	;

poolline:
	IPT_POOL unit '/' IPT_DSTLIST '(' name ';' dstopts ')'
	start dstlist end
					{ bzero((char *)&ipld, sizeof(ipld));
					  strncpy(ipld.ipld_name, $6,
						  sizeof(ipld.ipld_name));
					  ipld.ipld_unit = $2;
					  ipld.ipld_policy = $8;
					  load_dstlist(&ipld, poolioctl, $11);
					  resetlexer();
					  use_inet6 = 0;
					  free($6);
					}
	| IPT_POOL unit '/' IPT_TREE '(' name ';' ')'
	  start addrlist end
					{ bzero((char *)&iplo, sizeof(iplo));
					  strncpy(iplo.ipo_name, $6,
						  sizeof(iplo.ipo_name));
					  iplo.ipo_list = $10;
					  iplo.ipo_unit = $2;
					  load_pool(&iplo, poolioctl);
					  resetlexer();
					  use_inet6 = 0;
					  free($6);
					}
	| IPT_POOL '(' name ';' ')' start addrlist end
					{ bzero((char *)&iplo, sizeof(iplo));
					  strncpy(iplo.ipo_name, $3,
						  sizeof(iplo.ipo_name));
					  iplo.ipo_list = $7;
					  iplo.ipo_unit = IPL_LOGALL;
					  load_pool(&iplo, poolioctl);
					  resetlexer();
					  use_inet6 = 0;
					  free($3);
					}
	| IPT_POOL unit '/' IPT_HASH '(' name ';' hashoptlist ')'
	  start hashlist end
					{ iphtent_t *h;
					  bzero((char *)&ipht, sizeof(ipht));
					  strncpy(ipht.iph_name, $6,
						  sizeof(ipht.iph_name));
					  ipht.iph_unit = $2;
					  load_hash(&ipht, $11, poolioctl);
					  while ((h = ipht.iph_list) != NULL) {
						ipht.iph_list = h->ipe_next;
						free(h);
					  }
					  resetlexer();
					  use_inet6 = 0;
					  free($6);
					}
	| IPT_GROUPMAP '(' name ';' inout ';' ')'
	  start setgrouplist end
					{ iphtent_t *h;
					  bzero((char *)&ipht, sizeof(ipht));
					  strncpy(ipht.iph_name, $3,
						  sizeof(ipht.iph_name));
					  ipht.iph_type = IPHASH_GROUPMAP;
					  ipht.iph_unit = IPL_LOGIPF;
					  ipht.iph_flags = $5;
					  load_hash(&ipht, $9, poolioctl);
					  while ((h = ipht.iph_list) != NULL) {
						ipht.iph_list = h->ipe_next;
						free(h);
					  }
					  resetlexer();
					  use_inet6 = 0;
					  free($3);
					}
	;

name:	IPT_NAME YY_STR			{ $$ = $2; }
	| IPT_NUM YY_NUMBER		{ char name[80];
					  sprintf(name, "%d", $2);
					  $$ = strdup(name);
					}
	;

hashoptlist:
	| hashopt ';'
	| hashoptlist ';' hashopt ';'
	;
hashopt:
	IPT_SIZE YY_NUMBER
	| IPT_SEED YY_NUMBER
	;

dstlist:
	dstentries			{ $$ = $1; }
	| ';'				{ $$ = NULL; }
	;

dstentries:
	dstentry next			{ $$ = $1; }
	| dstentry next dstentries	{ $1->ipfd_next = $3; $$ = $1; }
	;

dstentry:
	YY_STR ':' ipaddr	{ int size = sizeof(*$$) + strlen($1) + 1;
				  $$ = calloc(1, size);
				  if ($$ != NULL) {
					$$->ipfd_dest.fd_name = strlen($1) + 1;
					bcopy($1, $$->ipfd_names,
					      $$->ipfd_dest.fd_name);
					$$->ipfd_dest.fd_addr = $3;
					$$->ipfd_size = size;
				  }
				  free($1);
				}
	| ipaddr		{ $$ = calloc(1, sizeof(*$$));
				  if ($$ != NULL) {
					$$->ipfd_dest.fd_name = -1;
					$$->ipfd_dest.fd_addr = $1;
					$$->ipfd_size = sizeof(*$$);
				  }
				}
	;

dstopts:
						{ $$ = IPLDP_NONE; }
	| IPT_POLICY IPT_ROUNDROBIN ';'		{ $$ = IPLDP_ROUNDROBIN; }
	| IPT_POLICY IPT_WEIGHTED weighting ';'	{ $$ = $3; }
	| IPT_POLICY IPT_RANDOM ';'		{ $$ = IPLDP_RANDOM; }
	| IPT_POLICY IPT_HASH ';'		{ $$ = IPLDP_HASHED; }
	| IPT_POLICY IPT_SRCHASH ';'		{ $$ = IPLDP_SRCHASH; }
	| IPT_POLICY IPT_DSTHASH ';'		{ $$ = IPLDP_DSTHASH; }
	;

weighting:
	IPT_CONNECTION				{ $$ = IPLDP_CONNECTION; }
	;
%%
static	wordtab_t	yywords[] = {
	{ "all",		IPT_ALL },
	{ "auth",		IPT_AUTH },
	{ "connection",		IPT_CONNECTION },
	{ "count",		IPT_COUNT },
	{ "dst-hash",		IPT_DSTHASH },
	{ "dstlist",		IPT_DSTLIST },
	{ "file",		IPT_FILE },
	{ "group",		IPT_GROUP },
	{ "group-map",		IPT_GROUPMAP },
	{ "hash",		IPT_HASH },
	{ "in",			IPT_IN },
	{ "ipf",		IPT_IPF },
	{ "name",		IPT_NAME },
	{ "nat",		IPT_NAT },
	{ "number",		IPT_NUM },
	{ "out",		IPT_OUT },
	{ "policy",		IPT_POLICY },
	{ "pool",		IPT_POOL },
	{ "random",		IPT_RANDOM },
	{ "round-robin",	IPT_ROUNDROBIN },
	{ "role",		IPT_ROLE },
	{ "seed",		IPT_SEED },
	{ "size",		IPT_SIZE },
	{ "src-hash",		IPT_SRCHASH },
	{ "table",		IPT_TABLE },
	{ "tree",		IPT_TREE },
	{ "type",		IPT_TYPE },
	{ "weighted",		IPT_WEIGHTED },
	{ "whois",		IPT_WHOIS },
	{ NULL,			0 }
};


int ippool_parsefile(fd, filename, iocfunc)
int fd;
char *filename;
ioctlfunc_t iocfunc;
{
	FILE *fp = NULL;
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
			fprintf(stderr, "fopen(%s) failed: %s\n", filename,
				STRERROR(errno));
			return -1;
		}
	} else
		fp = stdin;

	while (ippool_parsesome(fd, fp, iocfunc) == 1)
		;
	if (fp != NULL)
		fclose(fp);
	return 0;
}


int ippool_parsesome(fd, fp, iocfunc)
int fd;
FILE *fp;
ioctlfunc_t iocfunc;
{
	char *s;
	int i;

	poolioctl = iocfunc;

	if (feof(fp))
		return 0;
	i = fgetc(fp);
	if (i == EOF)
		return 0;
	if (ungetc(i, fp) == EOF)
		return 0;
	if (feof(fp))
		return 0;
	s = getenv("YYDEBUG");
	if (s)
		yydebug = atoi(s);
	else
		yydebug = 0;

	yyin = fp;
	yyparse();
	return 1;
}


static iphtent_t *
add_htablehosts(url)
char *url;
{
	iphtent_t *htop, *hbot, *h;
	alist_t *a, *hlist;

	if (!strncmp(url, "file://", 7) || !strncmp(url, "http://", 7)) {
		hlist = load_url(url);
	} else {
		use_inet6 = 0;

		hlist = calloc(1, sizeof(*hlist));
		if (hlist == NULL)
			return NULL;

		if (gethost(hlist->al_family, url, &hlist->al_i6addr) == -1) {
			yyerror("Unknown hostname");
		}
	}

	hbot = NULL;
	htop = NULL;

	for (a = hlist; a != NULL; a = a->al_next) {
		h = calloc(1, sizeof(*h));
		if (h == NULL)
			break;

		h->ipe_family = a->al_family;
		h->ipe_addr = a->al_i6addr;
		h->ipe_mask = a->al_i6mask;

		if (hbot != NULL)
			hbot->ipe_next = h;
		else
			htop = h;
		hbot = h;
	}

	alist_free(hlist);

	return htop;
}


static ip_pool_node_t *
add_poolhosts(url)
char *url;
{
	ip_pool_node_t *ptop, *pbot, *p;
	alist_t *a, *hlist;

	if (!strncmp(url, "file://", 7) || !strncmp(url, "http://", 7)) {
		hlist = load_url(url);
	} else {
		use_inet6 = 0;

		hlist = calloc(1, sizeof(*hlist));
		if (hlist == NULL)
			return NULL;

		if (gethost(hlist->al_family, url, &hlist->al_i6addr) == -1) {
			yyerror("Unknown hostname");
		}
	}

	pbot = NULL;
	ptop = NULL;

	for (a = hlist; a != NULL; a = a->al_next) {
		p = calloc(1, sizeof(*p));
		if (p == NULL)
			break;
		p->ipn_mask.adf_addr = a->al_i6mask;

		if (a->al_family == AF_INET) {
			p->ipn_addr.adf_family = AF_INET;
#ifdef USE_INET6
		} else if (a->al_family == AF_INET6) {
			p->ipn_addr.adf_family = AF_INET6;
#endif
		}
		setadflen(&p->ipn_addr);
		p->ipn_addr.adf_addr = a->al_i6addr;
		p->ipn_info = a->al_not;
		p->ipn_mask.adf_len = p->ipn_addr.adf_len;

		if (pbot != NULL)
			pbot->ipn_next = p;
		else
			ptop = p;
		pbot = p;
	}

	alist_free(hlist);

	return ptop;
}


ip_pool_node_t *
read_whoisfile(file)
	char *file;
{
	ip_pool_node_t *ntop, *ipn, node, *last;
	char line[1024];
	FILE *fp;

	fp = fopen(file, "r");
	if (fp == NULL)
		return NULL;

	last = NULL;
	ntop = NULL;
	while (fgets(line, sizeof(line) - 1, fp) != NULL) {
		line[sizeof(line) - 1] = '\0';

		if (parsewhoisline(line, &node.ipn_addr, &node.ipn_mask))
			continue;
		ipn = calloc(1, sizeof(*ipn));
		if (ipn == NULL)
			continue;
		ipn->ipn_addr = node.ipn_addr;
		ipn->ipn_mask = node.ipn_mask;
		if (last == NULL)
			ntop = ipn;
		else
			last->ipn_next = ipn;
		last = ipn;
	}
	fclose(fp);
	return ntop;
}


static void
setadflen(afp)
	addrfamily_t *afp;
{
	afp->adf_len = offsetof(addrfamily_t, adf_addr);
	switch (afp->adf_family)
	{
	case AF_INET :
		afp->adf_len += sizeof(struct in_addr);
		break;
#ifdef USE_INET6
	case AF_INET6 :
		afp->adf_len += sizeof(struct in6_addr);
		break;
#endif
	default :
		break;
	}
}
