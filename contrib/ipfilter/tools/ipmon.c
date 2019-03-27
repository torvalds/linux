/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#include "ipf.h"
#include "ipmon.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>

#if !defined(lint)
static const char sccsid[] = "@(#)ipmon.c	1.21 6/5/96 (C)1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif


#define	STRERROR(x)	strerror(x)

extern	int	optind;
extern	char	*optarg;

extern	ipmon_saver_t	executesaver;
extern	ipmon_saver_t	filesaver;
extern	ipmon_saver_t	nothingsaver;
extern	ipmon_saver_t	snmpv1saver;
extern	ipmon_saver_t	snmpv2saver;
extern	ipmon_saver_t	syslogsaver;


struct	flags {
	int	value;
	char	flag;
};

typedef	struct	logsource {
	int	fd;
	int	logtype;
	char	*file;
	int	regular;
	size_t	size;
} logsource_t;

typedef struct config {
	int		opts;
	int		maxfd;
	logsource_t	logsrc[3];
	fd_set		fdmr;
	FILE		*blog;
	char		*bfile;
	FILE		*log;
	char		*file;
	char		*cfile;
} config_t;

typedef	struct	icmp_subtype {
	int	ist_val;
	char	*ist_name;
} icmp_subtype_t;

typedef	struct	icmp_type {
	int	it_val;
	struct	icmp_subtype *it_subtable;
	size_t	it_stsize;
	char	*it_name;
} icmp_type_t;


#define	IST_SZ(x)	(sizeof(x)/sizeof(icmp_subtype_t))


struct	flags	tcpfl[] = {
	{ TH_ACK, 'A' },
	{ TH_RST, 'R' },
	{ TH_SYN, 'S' },
	{ TH_FIN, 'F' },
	{ TH_URG, 'U' },
	{ TH_PUSH,'P' },
	{ TH_ECN, 'E' },
	{ TH_CWR, 'C' },
	{ 0, '\0' }
};

char *reasons[] = {
	"filter-rule",
	"log-or-block_1",
	"pps-rate",
	"jumbogram",
	"makefrip-fail",
	"state_add-fail",
	"updateipid-fail",
	"log-or-block_2",
	"decap-fail",
	"auth_new-fail",
	"auth_captured",
	"coalesce-fail",
	"pullup-fail",
	"auth-feedback",
	"bad-frag",
	"natv4_out-fail",
	"natv4_in-fail",
	"natv6_out-fail",
	"natv6_in-fail",
};

#ifdef	MENTAT
static	char	*pidfile = "/etc/opt/ipf/ipmon.pid";
#else
static	char	*pidfile = "/var/run/ipmon.pid";
#endif

static	char	line[2048];
static	int	donehup = 0;
static	void	usage __P((char *));
static	void	handlehup __P((int));
static	void	flushlogs __P((char *, FILE *));
static	void	print_log __P((config_t *, logsource_t *, char *, int));
static	void	print_ipflog __P((config_t *, char *, int));
static	void	print_natlog __P((config_t *, char *, int));
static	void	print_statelog __P((config_t *, char *, int));
static	int	read_log __P((int, int *, char *, int));
static	void	write_pid __P((char *));
static	char	*icmpname __P((u_int, u_int));
static	char	*icmpname6 __P((u_int, u_int));
static	icmp_type_t *find_icmptype __P((int, icmp_type_t *, size_t));
static	icmp_subtype_t *find_icmpsubtype __P((int, icmp_subtype_t *, size_t));
static	struct	tm	*get_tm __P((time_t));

char	*portlocalname __P((int, char *, u_int));
int	main __P((int, char *[]));

static	void	logopts __P((int, char *));
static	void	init_tabs __P((void));
static	char	*getlocalproto __P((u_int));
static	void	openlogs __P((config_t *conf));
static	int	read_loginfo __P((config_t *conf));
static	void	initconfig __P((config_t *conf));

static	char	**protocols = NULL;
static	char	**udp_ports = NULL;
static	char	**tcp_ports = NULL;


#define	HOSTNAMEV4(b)	hostname(AF_INET, (u_32_t *)&(b))

#ifndef	LOGFAC
#define	LOGFAC	LOG_LOCAL0
#endif
int	logfac = LOGFAC;
int	ipmonopts = 0;
int	opts = OPT_NORESOLVE;
int	use_inet6 = 0;


static icmp_subtype_t icmpunreachnames[] = {
	{ ICMP_UNREACH_NET,		"net" },
	{ ICMP_UNREACH_HOST,		"host" },
	{ ICMP_UNREACH_PROTOCOL,	"protocol" },
	{ ICMP_UNREACH_PORT,		"port" },
	{ ICMP_UNREACH_NEEDFRAG,	"needfrag" },
	{ ICMP_UNREACH_SRCFAIL,		"srcfail" },
	{ ICMP_UNREACH_NET_UNKNOWN,	"net_unknown" },
	{ ICMP_UNREACH_HOST_UNKNOWN,	"host_unknown" },
	{ ICMP_UNREACH_NET,		"isolated" },
	{ ICMP_UNREACH_NET_PROHIB,	"net_prohib" },
	{ ICMP_UNREACH_NET_PROHIB,	"host_prohib" },
	{ ICMP_UNREACH_TOSNET,		"tosnet" },
	{ ICMP_UNREACH_TOSHOST,		"toshost" },
	{ ICMP_UNREACH_ADMIN_PROHIBIT,	"admin_prohibit" },
	{ -2,				NULL }
};

static icmp_subtype_t redirectnames[] = {
	{ ICMP_REDIRECT_NET,		"net" },
	{ ICMP_REDIRECT_HOST,		"host" },
	{ ICMP_REDIRECT_TOSNET,		"tosnet" },
	{ ICMP_REDIRECT_TOSHOST,	"toshost" },
	{ -2,				NULL }
};

static icmp_subtype_t timxceednames[] = {
	{ ICMP_TIMXCEED_INTRANS,	"transit" },
	{ ICMP_TIMXCEED_REASS,		"reassem" },
	{ -2,				NULL }
};

static icmp_subtype_t paramnames[] = {
	{ ICMP_PARAMPROB_ERRATPTR,	"errata_pointer" },
	{ ICMP_PARAMPROB_OPTABSENT,	"optmissing" },
	{ ICMP_PARAMPROB_LENGTH,	"length" },
	{ -2,				NULL }
};

static icmp_type_t icmptypes4[] = {
	{ ICMP_ECHOREPLY,	NULL,	0,		"echoreply" },
	{ -1,			NULL,	0,		NULL },
	{ -1,			NULL,	0,		NULL },
	{ ICMP_UNREACH,		icmpunreachnames,
				IST_SZ(icmpunreachnames),"unreach" },
	{ ICMP_SOURCEQUENCH,	NULL,	0,		"sourcequench" },
	{ ICMP_REDIRECT,	redirectnames,
				IST_SZ(redirectnames),	"redirect" },
	{ -1,			NULL,	0,		NULL },
	{ -1,			NULL,	0,		NULL },
	{ ICMP_ECHO,		NULL,	0,		"echo" },
	{ ICMP_ROUTERADVERT,	NULL,	0,		"routeradvert" },
	{ ICMP_ROUTERSOLICIT,	NULL,	0,		"routersolicit" },
	{ ICMP_TIMXCEED,	timxceednames,
				IST_SZ(timxceednames),	"timxceed" },
	{ ICMP_PARAMPROB,	paramnames,
				IST_SZ(paramnames),	"paramprob" },
	{ ICMP_TSTAMP,		NULL,	0,		"timestamp" },
	{ ICMP_TSTAMPREPLY,	NULL,	0,		"timestampreply" },
	{ ICMP_IREQ,		NULL,	0,		"inforeq" },
	{ ICMP_IREQREPLY,	NULL,	0,		"inforeply" },
	{ ICMP_MASKREQ,		NULL,	0,		"maskreq" },
	{ ICMP_MASKREPLY,	NULL,	0,		"maskreply" },
	{ -2,			NULL,	0,		NULL }
};

static icmp_subtype_t icmpredirect6[] = {
	{ ICMP6_DST_UNREACH_NOROUTE,		"noroute" },
	{ ICMP6_DST_UNREACH_ADMIN,		"admin" },
	{ ICMP6_DST_UNREACH_NOTNEIGHBOR,	"neighbour" },
	{ ICMP6_DST_UNREACH_ADDR,		"address" },
	{ ICMP6_DST_UNREACH_NOPORT,		"noport" },
	{ -2,					NULL }
};

static icmp_subtype_t icmptimexceed6[] = {
	{ ICMP6_TIME_EXCEED_TRANSIT,		"intransit" },
	{ ICMP6_TIME_EXCEED_REASSEMBLY,		"reassem" },
	{ -2,					NULL }
};

static icmp_subtype_t icmpparamprob6[] = {
	{ ICMP6_PARAMPROB_HEADER,		"header" },
	{ ICMP6_PARAMPROB_NEXTHEADER,		"nextheader" },
	{ ICMP6_PARAMPROB_OPTION,		"option" },
	{ -2,					NULL }
};

static icmp_subtype_t icmpquerysubject6[] = {
	{ ICMP6_NI_SUBJ_IPV6,			"ipv6" },
	{ ICMP6_NI_SUBJ_FQDN,			"fqdn" },
	{ ICMP6_NI_SUBJ_IPV4,			"ipv4" },
	{ -2,					NULL },
};

static icmp_subtype_t icmpnodeinfo6[] = {
	{ ICMP6_NI_SUCCESS,			"success" },
	{ ICMP6_NI_REFUSED,			"refused" },
	{ ICMP6_NI_UNKNOWN,			"unknown" },
	{ -2,					NULL }
};

static icmp_subtype_t icmprenumber6[] = {
	{ ICMP6_ROUTER_RENUMBERING_COMMAND,		"command" },
	{ ICMP6_ROUTER_RENUMBERING_RESULT,		"result" },
	{ ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET,	"seqnum_reset" },
	{ -2,						NULL }
};

static icmp_type_t icmptypes6[] = {
	{ 0,			NULL,	0,		NULL },
	{ ICMP6_DST_UNREACH,	icmpredirect6,
			IST_SZ(icmpredirect6),		"unreach" },
	{ ICMP6_PACKET_TOO_BIG,	NULL,	0,		"toobig" },
	{ ICMP6_TIME_EXCEEDED,	icmptimexceed6,
			IST_SZ(icmptimexceed6),		"timxceed" },
	{ ICMP6_PARAM_PROB,	icmpparamprob6,
			IST_SZ(icmpparamprob6),		"paramprob" },
	{ ICMP6_ECHO_REQUEST,	NULL,	0,		"echo" },
	{ ICMP6_ECHO_REPLY,	NULL,	0,		"echoreply" },
	{ ICMP6_MEMBERSHIP_QUERY, icmpquerysubject6,
			IST_SZ(icmpquerysubject6),	"groupmemberquery" },
	{ ICMP6_MEMBERSHIP_REPORT,NULL,	0,		"groupmemberreport" },
	{ ICMP6_MEMBERSHIP_REDUCTION,NULL,	0,	"groupmemberterm" },
	{ ND_ROUTER_SOLICIT,	NULL,	0,		"routersolicit" },
	{ ND_ROUTER_ADVERT,	NULL,	0,		"routeradvert" },
	{ ND_NEIGHBOR_SOLICIT,	NULL,	0,		"neighborsolicit" },
	{ ND_NEIGHBOR_ADVERT,	NULL,	0,		"neighboradvert" },
	{ ND_REDIRECT,		NULL,	0,		"redirect" },
	{ ICMP6_ROUTER_RENUMBERING,	icmprenumber6,
			IST_SZ(icmprenumber6),		"routerrenumber" },
	{ ICMP6_WRUREQUEST,	NULL,	0,		"whoareyourequest" },
	{ ICMP6_WRUREPLY,	NULL,	0,		"whoareyoureply" },
	{ ICMP6_FQDN_QUERY,	NULL,	0,		"fqdnquery" },
	{ ICMP6_FQDN_REPLY,	NULL,	0,		"fqdnreply" },
	{ ICMP6_NI_QUERY,	icmpnodeinfo6,
			IST_SZ(icmpnodeinfo6),		"nodeinforequest" },
	{ ICMP6_NI_REPLY,	NULL,	0,		"nodeinforeply" },
	{ MLD6_MTRACE_RESP,	NULL,	0,		"mtraceresponse" },
	{ MLD6_MTRACE,		NULL,	0,		"mtracerequest" },
	{ -2,			NULL,	0,		NULL }
};

static icmp_subtype_t *find_icmpsubtype(type, table, tablesz)
	int type;
	icmp_subtype_t *table;
	size_t tablesz;
{
	icmp_subtype_t *ist;
	int i;

	if (tablesz < 2)
		return NULL;

	if ((type < 0) || (type > table[tablesz - 2].ist_val))
		return NULL;

	i = type;
	if (table[type].ist_val == type)
		return table + type;

	for (i = 0, ist = table; ist->ist_val != -2; i++, ist++)
		if (ist->ist_val == type)
			return ist;
	return NULL;
}


static icmp_type_t *find_icmptype(type, table, tablesz)
	int type;
	icmp_type_t *table;
	size_t tablesz;
{
	icmp_type_t *it;
	int i;

	if (tablesz < 2)
		return NULL;

	if ((type < 0) || (type > table[tablesz - 2].it_val))
		return NULL;

	i = type;
	if (table[type].it_val == type)
		return table + type;

	for (i = 0, it = table; it->it_val != -2; i++, it++)
		if (it->it_val == type)
			return it;
	return NULL;
}


static void handlehup(sig)
	int sig;
{
	signal(SIGHUP, handlehup);
	donehup = 1;
}


static void init_tabs()
{
	struct	protoent	*p;
	struct	servent	*s;
	char	*name, **tab;
	int	port, i;

	if (protocols != NULL) {
		for (i = 0; i < 256; i++)
			if (protocols[i] != NULL) {
				free(protocols[i]);
				protocols[i] = NULL;
			}
		free(protocols);
		protocols = NULL;
	}
	protocols = (char **)malloc(256 * sizeof(*protocols));
	if (protocols != NULL) {
		bzero((char *)protocols, 256 * sizeof(*protocols));

		setprotoent(1);
		while ((p = getprotoent()) != NULL)
			if (p->p_proto >= 0 && p->p_proto <= 255 &&
			    p->p_name != NULL && protocols[p->p_proto] == NULL)
				protocols[p->p_proto] = strdup(p->p_name);
		endprotoent();
		if (protocols[0])
			free(protocols[0]);
		protocols[0] = strdup("ip");
	}

	if (udp_ports != NULL) {
		for (i = 0; i < 65536; i++)
			if (udp_ports[i] != NULL) {
				free(udp_ports[i]);
				udp_ports[i] = NULL;
			}
		free(udp_ports);
		udp_ports = NULL;
	}
	udp_ports = (char **)malloc(65536 * sizeof(*udp_ports));
	if (udp_ports != NULL)
		bzero((char *)udp_ports, 65536 * sizeof(*udp_ports));

	if (tcp_ports != NULL) {
		for (i = 0; i < 65536; i++)
			if (tcp_ports[i] != NULL) {
				free(tcp_ports[i]);
				tcp_ports[i] = NULL;
			}
		free(tcp_ports);
		tcp_ports = NULL;
	}
	tcp_ports = (char **)malloc(65536 * sizeof(*tcp_ports));
	if (tcp_ports != NULL)
		bzero((char *)tcp_ports, 65536 * sizeof(*tcp_ports));

	setservent(1);
	while ((s = getservent()) != NULL) {
		if (s->s_proto == NULL)
			continue;
		else if (!strcmp(s->s_proto, "tcp")) {
			port = ntohs(s->s_port);
			name = s->s_name;
			tab = tcp_ports;
		} else if (!strcmp(s->s_proto, "udp")) {
			port = ntohs(s->s_port);
			name = s->s_name;
			tab = udp_ports;
		} else
			continue;
		if ((port < 0 || port > 65535) || (name == NULL))
			continue;
		if (tab != NULL)
			tab[port] = strdup(name);
	}
	endservent();
}


static char *getlocalproto(p)
	u_int p;
{
	static char pnum[4];
	char *s;

	p &= 0xff;
	s = protocols ? protocols[p] : NULL;
	if (s == NULL) {
		sprintf(pnum, "%u", p);
		s = pnum;
	}
	return s;
}


static int read_log(fd, lenp, buf, bufsize)
	int fd, bufsize, *lenp;
	char *buf;
{
	int	nr;

	if (bufsize > IPFILTER_LOGSIZE)
		bufsize = IPFILTER_LOGSIZE;

	nr = read(fd, buf, bufsize);
	if (!nr)
		return 2;
	if ((nr < 0) && (errno != EINTR))
		return -1;
	*lenp = nr;
	return 0;
}


char *portlocalname(res, proto, port)
	int res;
	char *proto;
	u_int port;
{
	static char pname[8];
	char *s;

	port = ntohs(port);
	port &= 0xffff;
	sprintf(pname, "%u", port);
	if (!res || (ipmonopts & IPMON_PORTNUM))
		return pname;
	s = NULL;
	if (!strcmp(proto, "tcp"))
		s = tcp_ports[port];
	else if (!strcmp(proto, "udp"))
		s = udp_ports[port];
	if (s == NULL)
		s = pname;
	return s;
}


static char *icmpname(type, code)
	u_int type;
	u_int code;
{
	static char name[80];
	icmp_subtype_t *ist;
	icmp_type_t *it;
	char *s;

	s = NULL;
	it = find_icmptype(type, icmptypes4, sizeof(icmptypes4) / sizeof(*it));
	if (it != NULL)
		s = it->it_name;

	if (s == NULL)
		sprintf(name, "icmptype(%d)/", type);
	else
		sprintf(name, "%s/", s);

	ist = NULL;
	if (it != NULL && it->it_subtable != NULL)
		ist = find_icmpsubtype(code, it->it_subtable, it->it_stsize);

	if (ist != NULL && ist->ist_name != NULL)
		strcat(name, ist->ist_name);
	else
		sprintf(name + strlen(name), "%d", code);

	return name;
}

static char *icmpname6(type, code)
	u_int type;
	u_int code;
{
	static char name[80];
	icmp_subtype_t *ist;
	icmp_type_t *it;
	char *s;

	s = NULL;
	it = find_icmptype(type, icmptypes6, sizeof(icmptypes6) / sizeof(*it));
	if (it != NULL)
		s = it->it_name;

	if (s == NULL)
		sprintf(name, "icmpv6type(%d)/", type);
	else
		sprintf(name, "%s/", s);

	ist = NULL;
	if (it != NULL && it->it_subtable != NULL)
		ist = find_icmpsubtype(code, it->it_subtable, it->it_stsize);

	if (ist != NULL && ist->ist_name != NULL)
		strcat(name, ist->ist_name);
	else
		sprintf(name + strlen(name), "%d", code);

	return name;
}


void dumphex(log, dopts, buf, len)
	FILE *log;
	int dopts;
	char *buf;
	int len;
{
	char	hline[80];
	int	i, j, k;
	u_char	*s = (u_char *)buf, *t = (u_char *)hline;

	if (buf == NULL || len == 0)
		return;

	*hline = '\0';

	for (i = len, j = 0; i; i--, j++, s++) {
		if (j && !(j & 0xf)) {
			*t++ = '\n';
			*t = '\0';
			if ((dopts & IPMON_SYSLOG))
				syslog(LOG_INFO, "%s", hline);
			else if (log != NULL)
				fputs(hline, log);
			t = (u_char *)hline;
			*t = '\0';
		}
		sprintf((char *)t, "%02x", *s & 0xff);
		t += 2;
		if (!((j + 1) & 0xf)) {
			s -= 15;
			sprintf((char *)t, "        ");
			t += 8;
			for (k = 16; k; k--, s++)
				*t++ = (isprint(*s) ? *s : '.');
			s--;
		}

		if ((j + 1) & 0xf)
			*t++ = ' ';;
	}

	if (j & 0xf) {
		for (k = 16 - (j & 0xf); k; k--) {
			*t++ = ' ';
			*t++ = ' ';
			*t++ = ' ';
		}
		sprintf((char *)t, "       ");
		t += 7;
		s -= j & 0xf;
		for (k = j & 0xf; k; k--, s++)
			*t++ = (isprint(*s) ? *s : '.');
		*t++ = '\n';
		*t = '\0';
	}
	if ((dopts & IPMON_SYSLOG) != 0)
		syslog(LOG_INFO, "%s", hline);
	else if (log != NULL) {
		fputs(hline, log);
		fflush(log);
	}
}


static struct tm *get_tm(sec)
	time_t sec;
{
	struct tm *tm;
	time_t t;

	t = sec;
	tm = localtime(&t);
	return tm;
}

static void print_natlog(conf, buf, blen)
	config_t *conf;
	char *buf;
	int blen;
{
	static u_32_t seqnum = 0;
	int res, i, len, family;
	struct natlog *nl;
	struct tm *tm;
	iplog_t	*ipl;
	char *proto;
	int simple;
	char *t;

	t = line;
	simple = 0;
	ipl = (iplog_t *)buf;
	if (ipl->ipl_seqnum != seqnum) {
		if ((ipmonopts & IPMON_SYSLOG) != 0) {
			syslog(LOG_WARNING,
			       "missed %u NAT log entries: %u %u",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		} else {
			(void) fprintf(conf->log,
				"missed %u NAT log entries: %u %u\n",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		}
	}
	seqnum = ipl->ipl_seqnum + ipl->ipl_count;

	nl = (struct natlog *)((char *)ipl + sizeof(*ipl));
	res = (ipmonopts & IPMON_RESOLVE) ? 1 : 0;
	tm = get_tm(ipl->ipl_sec);
	len = sizeof(line);

	if (!(ipmonopts & IPMON_SYSLOG)) {
		(void) strftime(t, len, "%d/%m/%Y ", tm);
		i = strlen(t);
		len -= i;
		t += i;
	}
	(void) strftime(t, len, "%T", tm);
	t += strlen(t);
	sprintf(t, ".%-.6ld @%hd ", (long)ipl->ipl_usec, nl->nl_rule + 1);
	t += strlen(t);

	switch (nl->nl_action)
	{
	case NL_NEW :
		strcpy(t, "NAT:NEW");
		break;

	case NL_FLUSH :
		strcpy(t, "NAT:FLUSH");
		break;

	case NL_CLONE :
		strcpy(t, "NAT:CLONE");
		break;

	case NL_EXPIRE :
		strcpy(t, "NAT:EXPIRE");
		break;

	case NL_DESTROY :
		strcpy(t, "NAT:DESTROY");
		break;

	case NL_PURGE :
		strcpy(t, "NAT:PURGE");
		break;

	default :
		sprintf(t, "NAT:Action(%d)", nl->nl_action);
		break;
	}
	t += strlen(t);


	switch (nl->nl_type)
	{
	case NAT_MAP :
		strcpy(t, "-MAP ");
		simple = 1;
		break;

	case NAT_REDIRECT :
		strcpy(t, "-RDR ");
		simple = 1;
		break;

	case NAT_BIMAP :
		strcpy(t, "-BIMAP ");
		simple = 1;
		break;

	case NAT_MAPBLK :
		strcpy(t, "-MAPBLOCK ");
		simple = 1;
		break;

	case NAT_REWRITE|NAT_MAP :
		strcpy(t, "-RWR_MAP ");
		break;

	case NAT_REWRITE|NAT_REDIRECT :
		strcpy(t, "-RWR_RDR ");
		break;

	case NAT_ENCAP|NAT_MAP :
		strcpy(t, "-ENC_MAP ");
		break;

	case NAT_ENCAP|NAT_REDIRECT :
		strcpy(t, "-ENC_RDR ");
		break;

	case NAT_DIVERTUDP|NAT_MAP :
		strcpy(t, "-DIV_MAP ");
		break;

	case NAT_DIVERTUDP|NAT_REDIRECT :
		strcpy(t, "-DIV_RDR ");
		break;

	default :
		sprintf(t, "-Type(%d) ", nl->nl_type);
		break;
	}
	t += strlen(t);

	proto = getlocalproto(nl->nl_p[0]);

	family = vtof(nl->nl_v[0]);

	if (simple == 1) {
		sprintf(t, "%s,%s <- -> ", hostname(family, nl->nl_osrcip.i6),
			portlocalname(res, proto, (u_int)nl->nl_osrcport));
		t += strlen(t);
		sprintf(t, "%s,%s ", hostname(family, nl->nl_nsrcip.i6),
			portlocalname(res, proto, (u_int)nl->nl_nsrcport));
		t += strlen(t);
		sprintf(t, "[%s,%s] ", hostname(family, nl->nl_odstip.i6),
			portlocalname(res, proto, (u_int)nl->nl_odstport));
	} else {
		sprintf(t, "%s,%s ", hostname(family, nl->nl_osrcip.i6),
			portlocalname(res, proto, (u_int)nl->nl_osrcport));
		t += strlen(t);
		sprintf(t, "%s,%s <- -> ", hostname(family, nl->nl_odstip.i6),
			portlocalname(res, proto, (u_int)nl->nl_odstport));
		t += strlen(t);
		sprintf(t, "%s,%s ", hostname(family, nl->nl_nsrcip.i6),
			portlocalname(res, proto, (u_int)nl->nl_nsrcport));
		t += strlen(t);
		sprintf(t, "%s,%s ", hostname(family, nl->nl_ndstip.i6),
			portlocalname(res, proto, (u_int)nl->nl_ndstport));
	}
	t += strlen(t);

	strcpy(t, getlocalproto(nl->nl_p[0]));
	t += strlen(t);

	if (nl->nl_action == NL_EXPIRE || nl->nl_action == NL_FLUSH) {
#ifdef	USE_QUAD_T
# ifdef	PRId64
		sprintf(t, " Pkts %" PRId64 "/%" PRId64 " Bytes %" PRId64 "/%"
			PRId64,
# else
		sprintf(t, " Pkts %qd/%qd Bytes %qd/%qd",
# endif
#else
		sprintf(t, " Pkts %ld/%ld Bytes %ld/%ld",
#endif
				nl->nl_pkts[0], nl->nl_pkts[1],
				nl->nl_bytes[0], nl->nl_bytes[1]);
		t += strlen(t);
	}

	*t++ = '\n';
	*t++ = '\0';
	if (ipmonopts & IPMON_SYSLOG)
		syslog(LOG_INFO, "%s", line);
	else if (conf->log != NULL)
		(void) fprintf(conf->log, "%s", line);
}


static void print_statelog(conf, buf, blen)
	config_t *conf;
	char *buf;
	int blen;
{
	static u_32_t seqnum = 0;
	int res, i, len, family;
	struct ipslog *sl;
	char *t, *proto;
	struct tm *tm;
	iplog_t *ipl;

	t = line;
	ipl = (iplog_t *)buf;
	if (ipl->ipl_seqnum != seqnum) {
		if ((ipmonopts & IPMON_SYSLOG) != 0) {
			syslog(LOG_WARNING,
			       "missed %u state log entries: %u %u",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		} else {
			(void) fprintf(conf->log,
				"missed %u state log entries: %u %u\n",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		}
	}
	seqnum = ipl->ipl_seqnum + ipl->ipl_count;

	sl = (struct ipslog *)((char *)ipl + sizeof(*ipl));
	res = (ipmonopts & IPMON_RESOLVE) ? 1 : 0;
	tm = get_tm(ipl->ipl_sec);
	len = sizeof(line);
	if (!(ipmonopts & IPMON_SYSLOG)) {
		(void) strftime(t, len, "%d/%m/%Y ", tm);
		i = strlen(t);
		len -= i;
		t += i;
	}
	(void) strftime(t, len, "%T", tm);
	t += strlen(t);
	sprintf(t, ".%-.6ld ", (long)ipl->ipl_usec);
	t += strlen(t);

	family = vtof(sl->isl_v);

	switch (sl->isl_type)
	{
	case ISL_NEW :
		strcpy(t, "STATE:NEW ");
		break;

	case ISL_CLONE :
		strcpy(t, "STATE:CLONED ");
		break;

	case ISL_EXPIRE :
		if ((sl->isl_p == IPPROTO_TCP) &&
		    (sl->isl_state[0] > IPF_TCPS_ESTABLISHED ||
		     sl->isl_state[1] > IPF_TCPS_ESTABLISHED))
			strcpy(t, "STATE:CLOSE ");
		else
			strcpy(t, "STATE:EXPIRE ");
		break;

	case ISL_FLUSH :
		strcpy(t, "STATE:FLUSH ");
		break;

	case ISL_INTERMEDIATE :
		strcpy(t, "STATE:INTERMEDIATE ");
		break;

	case ISL_REMOVE :
		strcpy(t, "STATE:REMOVE ");
		break;

	case ISL_KILLED :
		strcpy(t, "STATE:KILLED ");
		break;

	case ISL_UNLOAD :
		strcpy(t, "STATE:UNLOAD ");
		break;

	default :
		sprintf(t, "Type: %d ", sl->isl_type);
		break;
	}
	t += strlen(t);

	proto = getlocalproto(sl->isl_p);

	if (sl->isl_p == IPPROTO_TCP || sl->isl_p == IPPROTO_UDP) {
		sprintf(t, "%s,%s -> ",
			hostname(family, (u_32_t *)&sl->isl_src),
			portlocalname(res, proto, (u_int)sl->isl_sport));
		t += strlen(t);
		sprintf(t, "%s,%s PR %s",
			hostname(family, (u_32_t *)&sl->isl_dst),
			portlocalname(res, proto, (u_int)sl->isl_dport), proto);
	} else if (sl->isl_p == IPPROTO_ICMP) {
		sprintf(t, "%s -> ", hostname(family, (u_32_t *)&sl->isl_src));
		t += strlen(t);
		sprintf(t, "%s PR icmp %d",
			hostname(family, (u_32_t *)&sl->isl_dst),
			sl->isl_itype);
	} else if (sl->isl_p == IPPROTO_ICMPV6) {
		sprintf(t, "%s -> ", hostname(family, (u_32_t *)&sl->isl_src));
		t += strlen(t);
		sprintf(t, "%s PR icmpv6 %d",
			hostname(family, (u_32_t *)&sl->isl_dst),
			sl->isl_itype);
	} else {
		sprintf(t, "%s -> ", hostname(family, (u_32_t *)&sl->isl_src));
		t += strlen(t);
		sprintf(t, "%s PR %s",
			hostname(family, (u_32_t *)&sl->isl_dst), proto);
	}
	t += strlen(t);
	if (sl->isl_tag != FR_NOLOGTAG) {
		sprintf(t, " tag %u", sl->isl_tag);
		t += strlen(t);
	}
	if (sl->isl_type != ISL_NEW) {
		sprintf(t,
#ifdef	USE_QUAD_T
#ifdef	PRId64
			" Forward: Pkts in %" PRId64 " Bytes in %" PRId64
			" Pkts out %" PRId64 " Bytes out %" PRId64
			" Backward: Pkts in %" PRId64 " Bytes in %" PRId64
			" Pkts out %" PRId64 " Bytes out %" PRId64,
#else
			" Forward: Pkts in %qd Bytes in %qd Pkts out %qd Bytes out %qd Backward: Pkts in %qd Bytes in %qd Pkts out %qd Bytes out %qd",
#endif /* PRId64 */
#else
			" Forward: Pkts in %ld Bytes in %ld Pkts out %ld Bytes out %ld Backward: Pkts in %ld Bytes in %ld Pkts out %ld Bytes out %ld",
#endif
			sl->isl_pkts[0], sl->isl_bytes[0],
			sl->isl_pkts[1], sl->isl_bytes[1],
			sl->isl_pkts[2], sl->isl_bytes[2],
			sl->isl_pkts[3], sl->isl_bytes[3]);

		t += strlen(t);
	}

	*t++ = '\n';
	*t++ = '\0';
	if (ipmonopts & IPMON_SYSLOG)
		syslog(LOG_INFO, "%s", line);
	else if (conf->log != NULL)
		(void) fprintf(conf->log, "%s", line);
}


static void print_log(conf, log, buf, blen)
	config_t *conf;
	logsource_t *log;
	char *buf;
	int blen;
{
	char *bp, *bpo;
	iplog_t	*ipl;
	int psize;

	bp = NULL;
	bpo = NULL;

	while (blen > 0) {
		ipl = (iplog_t *)buf;
		if ((u_long)ipl & (sizeof(long)-1)) {
			if (bp)
				bpo = bp;
			bp = (char *)malloc(blen);
			bcopy((char *)ipl, bp, blen);
			if (bpo) {
				free(bpo);
				bpo = NULL;
			}
			buf = bp;
			continue;
		}

		psize = ipl->ipl_dsize;
		if (psize > blen)
			break;

		if (conf->blog != NULL) {
			fwrite(buf, psize, 1, conf->blog);
			fflush(conf->blog);
		}

		if (log->logtype == IPL_LOGIPF) {
			if (ipl->ipl_magic == IPL_MAGIC)
				print_ipflog(conf, buf, psize);

		} else if (log->logtype == IPL_LOGNAT) {
			if (ipl->ipl_magic == IPL_MAGIC_NAT)
				print_natlog(conf, buf, psize);

		} else if (log->logtype == IPL_LOGSTATE) {
			if (ipl->ipl_magic == IPL_MAGIC_STATE)
				print_statelog(conf, buf, psize);
		}

		blen -= psize;
		buf += psize;
	}
	if (bp)
		free(bp);
	return;
}


static void print_ipflog(conf, buf, blen)
	config_t *conf;
	char *buf;
	int blen;
{
	static u_32_t seqnum = 0;
	int i, f, lvl, res, len, off, plen, ipoff, defaction;
	struct icmp *icmp;
	struct icmp *ic;
	char *t, *proto;
	ip_t *ipc, *ip;
	struct tm *tm;
	u_32_t *s, *d;
	u_short hl, p;
	ipflog_t *ipf;
	iplog_t *ipl;
	tcphdr_t *tp;
#ifdef	USE_INET6
	struct ip6_ext *ehp;
	u_short ehl;
	ip6_t *ip6;
	int go;
#endif

	ipl = (iplog_t *)buf;
	if (ipl->ipl_seqnum != seqnum) {
		if ((ipmonopts & IPMON_SYSLOG) != 0) {
			syslog(LOG_WARNING,
			       "missed %u ipf log entries: %u %u",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		} else {
			(void) fprintf(conf->log,
				"missed %u ipf log entries: %u %u\n",
			       ipl->ipl_seqnum - seqnum, seqnum,
			       ipl->ipl_seqnum);
		}
	}
	seqnum = ipl->ipl_seqnum + ipl->ipl_count;

	ipf = (ipflog_t *)((char *)buf + sizeof(*ipl));
	ip = (ip_t *)((char *)ipf + sizeof(*ipf));
	f = ipf->fl_family;
	res = (ipmonopts & IPMON_RESOLVE) ? 1 : 0;
	t = line;
	*t = '\0';
	tm = get_tm(ipl->ipl_sec);

	len = sizeof(line);
	if (!(ipmonopts & IPMON_SYSLOG)) {
		(void) strftime(t, len, "%d/%m/%Y ", tm);
		i = strlen(t);
		len -= i;
		t += i;
	}
	(void) strftime(t, len, "%T", tm);
	t += strlen(t);
	sprintf(t, ".%-.6ld ", (long)ipl->ipl_usec);
	t += strlen(t);
	if (ipl->ipl_count > 1) {
		sprintf(t, "%dx ", ipl->ipl_count);
		t += strlen(t);
	}
	{
	char	ifname[sizeof(ipf->fl_ifname) + 1];

	strncpy(ifname, ipf->fl_ifname, sizeof(ipf->fl_ifname));
	ifname[sizeof(ipf->fl_ifname)] = '\0';
	sprintf(t, "%s", ifname);
	t += strlen(t);
# if defined(MENTAT)
		if (ISALPHA(*(t - 1))) {
			sprintf(t, "%d", ipf->fl_unit);
			t += strlen(t);
		}
# endif
	}
	if ((ipf->fl_group[0] == (char)~0) && (ipf->fl_group[1] == '\0'))
		strcat(t, " @-1:");
	else if (ipf->fl_group[0] == '\0')
		(void) strcpy(t, " @0:");
	else
		sprintf(t, " @%s:", ipf->fl_group);
	t += strlen(t);
	if (ipf->fl_rule == 0xffffffff)
		strcat(t, "-1 ");
	else
		sprintf(t, "%u ", ipf->fl_rule + 1);
	t += strlen(t);

	lvl = LOG_NOTICE;

 	if (ipf->fl_lflags & FI_SHORT) {
		*t++ = 'S';
		lvl = LOG_ERR;
	}

	if (FR_ISPASS(ipf->fl_flags)) {
		if (ipf->fl_flags & FR_LOGP)
			*t++ = 'p';
		else
			*t++ = 'P';
	} else if (FR_ISBLOCK(ipf->fl_flags)) {
		if (ipf->fl_flags & FR_LOGB)
			*t++ = 'b';
		else
			*t++ = 'B';
		lvl = LOG_WARNING;
	} else if ((ipf->fl_flags & FR_LOGMASK) == FR_LOG) {
		*t++ = 'L';
		lvl = LOG_INFO;
	} else if (ipf->fl_flags & FF_LOGNOMATCH) {
		*t++ = 'n';
	} else {
		*t++ = '?';
		lvl = LOG_EMERG;
	}
	if (ipf->fl_loglevel != 0xffff)
		lvl = ipf->fl_loglevel;
	*t++ = ' ';
	*t = '\0';

	if (f == AF_INET) {
		hl = IP_HL(ip) << 2;
		ipoff = ntohs(ip->ip_off);
		off = ipoff & IP_OFFMASK;
		p = (u_short)ip->ip_p;
		s = (u_32_t *)&ip->ip_src;
		d = (u_32_t *)&ip->ip_dst;
		plen = ntohs(ip->ip_len);
	} else
#ifdef	USE_INET6
	if (f == AF_INET6) {
		off = 0;
		ipoff = 0;
		hl = sizeof(ip6_t);
		ip6 = (ip6_t *)ip;
		p = (u_short)ip6->ip6_nxt;
		s = (u_32_t *)&ip6->ip6_src;
		d = (u_32_t *)&ip6->ip6_dst;
		plen = hl + ntohs(ip6->ip6_plen);
		go = 1;
		ehp = (struct ip6_ext *)((char *)ip6 + hl);
		while (go == 1) {
			switch (p)
			{
			case IPPROTO_HOPOPTS :
			case IPPROTO_MOBILITY :
			case IPPROTO_DSTOPTS :
			case IPPROTO_ROUTING :
			case IPPROTO_AH :
				p = ehp->ip6e_nxt;
				ehl = 8 + (ehp->ip6e_len << 3);
				hl += ehl;
				ehp = (struct ip6_ext *)((char *)ehp + ehl);
				break;
			case IPPROTO_FRAGMENT :
				hl += sizeof(struct ip6_frag);
				/* FALLTHROUGH */
			default :
				go = 0;
				break;
			}
		}
	} else
#endif
	{
		goto printipflog;
	}
	proto = getlocalproto(p);

	if ((p == IPPROTO_TCP || p == IPPROTO_UDP) && !off) {
		tp = (tcphdr_t *)((char *)ip + hl);
		if (!(ipf->fl_lflags & FI_SHORT)) {
			sprintf(t, "%s,%s -> ", hostname(f, s),
				portlocalname(res, proto, (u_int)tp->th_sport));
			t += strlen(t);
			sprintf(t, "%s,%s PR %s len %hu %hu",
				hostname(f, d),
				portlocalname(res, proto, (u_int)tp->th_dport),
				proto, hl, plen);
			t += strlen(t);

			if (p == IPPROTO_TCP) {
				*t++ = ' ';
				*t++ = '-';
				for (i = 0; tcpfl[i].value; i++)
					if (tp->th_flags & tcpfl[i].value)
						*t++ = tcpfl[i].flag;
				if (ipmonopts & IPMON_VERBOSE) {
					sprintf(t, " %lu %lu %hu",
						(u_long)(ntohl(tp->th_seq)),
						(u_long)(ntohl(tp->th_ack)),
						ntohs(tp->th_win));
					t += strlen(t);
				}
			}
			*t = '\0';
		} else {
			sprintf(t, "%s -> ", hostname(f, s));
			t += strlen(t);
			sprintf(t, "%s PR %s len %hu %hu",
				hostname(f, d), proto, hl, plen);
		}
#if defined(AF_INET6) && defined(IPPROTO_ICMPV6)
	} else if ((p == IPPROTO_ICMPV6) && !off && (f == AF_INET6)) {
		ic = (struct icmp *)((char *)ip + hl);
		sprintf(t, "%s -> ", hostname(f, s));
		t += strlen(t);
		sprintf(t, "%s PR icmpv6 len %hu %hu icmpv6 %s",
			hostname(f, d), hl, plen,
			icmpname6(ic->icmp_type, ic->icmp_code));
#endif
	} else if ((p == IPPROTO_ICMP) && !off && (f == AF_INET)) {
		ic = (struct icmp *)((char *)ip + hl);
		sprintf(t, "%s -> ", hostname(f, s));
		t += strlen(t);
		sprintf(t, "%s PR icmp len %hu %hu icmp %s",
			hostname(f, d), hl, plen,
			icmpname(ic->icmp_type, ic->icmp_code));
		if (ic->icmp_type == ICMP_UNREACH ||
		    ic->icmp_type == ICMP_SOURCEQUENCH ||
		    ic->icmp_type == ICMP_PARAMPROB ||
		    ic->icmp_type == ICMP_REDIRECT ||
		    ic->icmp_type == ICMP_TIMXCEED) {
			ipc = &ic->icmp_ip;
			i = ntohs(ipc->ip_len);
			/*
			 * XXX - try to guess endian of ip_len in ICMP
			 * returned data.
			 */
			if (i > 1500)
				i = ipc->ip_len;
			ipoff = ntohs(ipc->ip_off);
			proto = getlocalproto(ipc->ip_p);

			if (!(ipoff & IP_OFFMASK) &&
			    ((ipc->ip_p == IPPROTO_TCP) ||
			     (ipc->ip_p == IPPROTO_UDP))) {
				tp = (tcphdr_t *)((char *)ipc + hl);
				t += strlen(t);
				sprintf(t, " for %s,%s -",
					HOSTNAMEV4(ipc->ip_src),
					portlocalname(res, proto,
						 (u_int)tp->th_sport));
				t += strlen(t);
				sprintf(t, " %s,%s PR %s len %hu %hu",
					HOSTNAMEV4(ipc->ip_dst),
					portlocalname(res, proto,
						 (u_int)tp->th_dport),
					proto, IP_HL(ipc) << 2, i);
			} else if (!(ipoff & IP_OFFMASK) &&
				   (ipc->ip_p == IPPROTO_ICMP)) {
				icmp = (icmphdr_t *)((char *)ipc + hl);

				t += strlen(t);
				sprintf(t, " for %s -",
					HOSTNAMEV4(ipc->ip_src));
				t += strlen(t);
				sprintf(t,
					" %s PR icmp len %hu %hu icmp %d/%d",
					HOSTNAMEV4(ipc->ip_dst),
					IP_HL(ipc) << 2, i,
					icmp->icmp_type, icmp->icmp_code);
			} else {
				t += strlen(t);
				sprintf(t, " for %s -",
					HOSTNAMEV4(ipc->ip_src));
				t += strlen(t);
				sprintf(t, " %s PR %s len %hu (%hu)",
					HOSTNAMEV4(ipc->ip_dst), proto,
					IP_HL(ipc) << 2, i);
				t += strlen(t);
				if (ipoff & IP_OFFMASK) {
					sprintf(t, "(frag %d:%hu@%hu%s%s)",
						ntohs(ipc->ip_id),
						i - (IP_HL(ipc) << 2),
						(ipoff & IP_OFFMASK) << 3,
						ipoff & IP_MF ? "+" : "",
						ipoff & IP_DF ? "-" : "");
				}
			}

		}
	} else {
		sprintf(t, "%s -> ", hostname(f, s));
		t += strlen(t);
		sprintf(t, "%s PR %s len %hu (%hu)",
			hostname(f, d), proto, hl, plen);
		t += strlen(t);
		if (off & IP_OFFMASK)
			sprintf(t, " (frag %d:%hu@%hu%s%s)",
				ntohs(ip->ip_id),
				plen - hl, (off & IP_OFFMASK) << 3,
				ipoff & IP_MF ? "+" : "",
				ipoff & IP_DF ? "-" : "");
	}
	t += strlen(t);

printipflog:
	if (ipf->fl_flags & FR_KEEPSTATE) {
		(void) strcpy(t, " K-S");
		t += strlen(t);
	}

	if (ipf->fl_flags & FR_KEEPFRAG) {
		(void) strcpy(t, " K-F");
		t += strlen(t);
	}

	if (ipf->fl_dir == 0)
		strcpy(t, " IN");
	else if (ipf->fl_dir == 1)
		strcpy(t, " OUT");
	t += strlen(t);
	if (ipf->fl_logtag != 0) {
		sprintf(t, " log-tag %d", ipf->fl_logtag);
		t += strlen(t);
	}
	if (ipf->fl_nattag.ipt_num[0] != 0) {
		strcpy(t, " nat-tag ");
		t += strlen(t);
		strncpy(t, ipf->fl_nattag.ipt_tag, sizeof(ipf->fl_nattag));
		t += strlen(t);
	}
	if ((ipf->fl_lflags & FI_LOWTTL) != 0) {
			strcpy(t, " low-ttl");
			t += 8;
	}
	if ((ipf->fl_lflags & FI_OOW) != 0) {
			strcpy(t, " OOW");
			t += 4;
	}
	if ((ipf->fl_lflags & FI_BAD) != 0) {
			strcpy(t, " bad");
			t += 4;
	}
	if ((ipf->fl_lflags & FI_NATED) != 0) {
			strcpy(t, " NAT");
			t += 4;
	}
	if ((ipf->fl_lflags & FI_BADNAT) != 0) {
			strcpy(t, " bad-NAT");
			t += 8;
	}
	if ((ipf->fl_lflags & FI_BADSRC) != 0) {
			strcpy(t, " bad-src");
			t += 8;
	}
	if ((ipf->fl_lflags & FI_MULTICAST) != 0) {
			strcpy(t, " multicast");
			t += 10;
	}
	if ((ipf->fl_lflags & FI_BROADCAST) != 0) {
			strcpy(t, " broadcast");
			t += 10;
	}
	if ((ipf->fl_lflags & (FI_MULTICAST|FI_BROADCAST|FI_MBCAST)) ==
	    FI_MBCAST) {
			strcpy(t, " mbcast");
			t += 7;
	}
	if (ipf->fl_breason != 0) {
		strcpy(t, " reason:");
		t += 8;
		strcpy(t, reasons[ipf->fl_breason]);
		t += strlen(reasons[ipf->fl_breason]);
	}
	*t++ = '\n';
	*t++ = '\0';
	defaction = 0;
	if (conf->cfile != NULL)
		defaction = check_action(buf, line, ipmonopts, lvl);

	if (defaction == 0) {
		if (ipmonopts & IPMON_SYSLOG) {
			syslog(lvl, "%s", line);
		} else if (conf->log != NULL) {
			(void) fprintf(conf->log, "%s", line);
		}

		if (ipmonopts & IPMON_HEXHDR) {
			dumphex(conf->log, ipmonopts, buf,
				sizeof(iplog_t) + sizeof(*ipf));
		}
		if (ipmonopts & IPMON_HEXBODY) {
			dumphex(conf->log, ipmonopts, (char *)ip,
				ipf->fl_plen + ipf->fl_hlen);
		} else if ((ipmonopts & IPMON_LOGBODY) &&
			   (ipf->fl_flags & FR_LOGBODY)) {
			dumphex(conf->log, ipmonopts, (char *)ip + ipf->fl_hlen,
				ipf->fl_plen);
		}
	}
}


static void usage(prog)
	char *prog;
{
	fprintf(stderr, "%s: [-NFhstvxX] [-f <logfile>]\n", prog);
	exit(1);
}


static void write_pid(file)
	char *file;
{
	FILE *fp = NULL;
	int fd;

	if ((fd = open(file, O_CREAT|O_TRUNC|O_WRONLY, 0644)) >= 0) {
		fp = fdopen(fd, "w");
		if (fp == NULL) {
			close(fd);
			fprintf(stderr,
				"unable to open/create pid file: %s\n", file);
			return;
		}
		fprintf(fp, "%d", getpid());
		fclose(fp);
	}
}


static void flushlogs(file, log)
	char *file;
	FILE *log;
{
	int	fd, flushed = 0;

	if ((fd = open(file, O_RDWR)) == -1) {
		(void) fprintf(stderr, "%s: open: %s\n",
			       file, STRERROR(errno));
		exit(1);
	}

	if (ioctl(fd, SIOCIPFFB, &flushed) == 0) {
		printf("%d bytes flushed from log buffer\n",
			flushed);
		fflush(stdout);
	} else
		ipferror(fd, "SIOCIPFFB");
	(void) close(fd);

	if (flushed) {
		if (ipmonopts & IPMON_SYSLOG) {
			syslog(LOG_INFO, "%d bytes flushed from log\n",
				flushed);
		} else if ((log != stdout) && (log != NULL)) {
			fprintf(log, "%d bytes flushed from log\n", flushed);
		}
	}
}


static void logopts(turnon, options)
	int turnon;
	char *options;
{
	int flags = 0;
	char *s;

	for (s = options; *s; s++)
	{
		switch (*s)
		{
		case 'N' :
			flags |= IPMON_NAT;
			break;
		case 'S' :
			flags |= IPMON_STATE;
			break;
		case 'I' :
			flags |= IPMON_FILTER;
			break;
		default :
			fprintf(stderr, "Unknown log option %c\n", *s);
			exit(1);
		}
	}

	if (turnon)
		ipmonopts |= flags;
	else
		ipmonopts &= ~(flags);
}

static void initconfig(config_t *conf)
{
	int i;

	memset(conf, 0, sizeof(*conf));

	conf->log = stdout;
	conf->maxfd = -1;

	for (i = 0; i < 3; i++) {
		conf->logsrc[i].fd = -1;
		conf->logsrc[i].logtype = -1;
		conf->logsrc[i].regular = -1;
	}

	conf->logsrc[0].file = IPL_NAME;
	conf->logsrc[1].file = IPNAT_NAME;
	conf->logsrc[2].file = IPSTATE_NAME;

	add_doing(&executesaver);
	add_doing(&snmpv1saver);
	add_doing(&snmpv2saver);
	add_doing(&syslogsaver);
	add_doing(&filesaver);
	add_doing(&nothingsaver);
}


int main(argc, argv)
	int argc;
	char *argv[];
{
	int	doread, c, make_daemon = 0;
	char	*prog;
	config_t	config;

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];
	else
		prog++;

	initconfig(&config);

	while ((c = getopt(argc, argv,
			   "?abB:C:Df:FhL:nN:o:O:pP:sS:tvxX")) != -1)
		switch (c)
		{
		case 'a' :
			ipmonopts |= IPMON_LOGALL;
			config.logsrc[0].logtype = IPL_LOGIPF;
			config.logsrc[1].logtype = IPL_LOGNAT;
			config.logsrc[2].logtype = IPL_LOGSTATE;
			break;
		case 'b' :
			ipmonopts |= IPMON_LOGBODY;
			break;
		case 'B' :
			config.bfile = optarg;
			config.blog = fopen(optarg, "a");
			break;
		case 'C' :
			config.cfile = optarg;
			break;
		case 'D' :
			make_daemon = 1;
			break;
		case 'f' : case 'I' :
			ipmonopts |= IPMON_FILTER;
			config.logsrc[0].logtype = IPL_LOGIPF;
			config.logsrc[0].file = optarg;
			break;
		case 'F' :
			flushlogs(config.logsrc[0].file, config.log);
			flushlogs(config.logsrc[1].file, config.log);
			flushlogs(config.logsrc[2].file, config.log);
			break;
		case 'L' :
			logfac = fac_findname(optarg);
			if (logfac == -1) {
				fprintf(stderr,
					"Unknown syslog facility '%s'\n",
					 optarg);
				exit(1);
			}
			break;
		case 'n' :
			ipmonopts |= IPMON_RESOLVE;
			opts &= ~OPT_NORESOLVE;
			break;
		case 'N' :
			ipmonopts |= IPMON_NAT;
			config.logsrc[1].logtype = IPL_LOGNAT;
			config.logsrc[1].file = optarg;
			break;
		case 'o' : case 'O' :
			logopts(c == 'o', optarg);
			if (ipmonopts & IPMON_FILTER)
				config.logsrc[0].logtype = IPL_LOGIPF;
			if (ipmonopts & IPMON_NAT)
				config.logsrc[1].logtype = IPL_LOGNAT;
			if (ipmonopts & IPMON_STATE)
				config.logsrc[2].logtype = IPL_LOGSTATE;
			break;
		case 'p' :
			ipmonopts |= IPMON_PORTNUM;
			break;
		case 'P' :
			pidfile = optarg;
			break;
		case 's' :
			ipmonopts |= IPMON_SYSLOG;
			config.log = NULL;
			break;
		case 'S' :
			ipmonopts |= IPMON_STATE;
			config.logsrc[2].logtype = IPL_LOGSTATE;
			config.logsrc[2].file = optarg;
			break;
		case 't' :
			ipmonopts |= IPMON_TAIL;
			break;
		case 'v' :
			ipmonopts |= IPMON_VERBOSE;
			break;
		case 'x' :
			ipmonopts |= IPMON_HEXBODY;
			break;
		case 'X' :
			ipmonopts |= IPMON_HEXHDR;
			break;
		default :
		case 'h' :
		case '?' :
			usage(argv[0]);
		}

	if (ipmonopts & IPMON_SYSLOG)
		openlog(prog, LOG_NDELAY|LOG_PID, logfac);

	init_tabs();
	if (config.cfile)
		if (load_config(config.cfile) == -1) {
			unload_config();
			exit(1);
		}

	/*
	 * Default action is to only open the filter log file.
	 */
	if ((config.logsrc[0].logtype == -1) &&
	    (config.logsrc[0].logtype == -1) &&
	    (config.logsrc[0].logtype == -1))
		config.logsrc[0].logtype = IPL_LOGIPF;

	openlogs(&config);

	if (!(ipmonopts & IPMON_SYSLOG)) {
		config.file = argv[optind];
		config.log = config.file ? fopen(config.file, "a") : stdout;
		if (config.log == NULL) {
			(void) fprintf(stderr, "%s: fopen: %s\n",
				       argv[optind], STRERROR(errno));
			exit(1);
			/* NOTREACHED */
		}
		setvbuf(config.log, NULL, _IONBF, 0);
	} else {
		config.log = NULL;
	}

	if (make_daemon &&
	    ((config.log != stdout) || (ipmonopts & IPMON_SYSLOG))) {
#if BSD >= 199306
		daemon(0, !(ipmonopts & IPMON_SYSLOG));
#else
		int pid;

		switch (fork())
		{
		case -1 :
			(void) fprintf(stderr, "%s: fork() failed: %s\n",
				       argv[0], STRERROR(errno));
			exit(1);
			/* NOTREACHED */
		case 0 :
			break;
		default :
			exit(0);
		}

		setsid();
		if ((ipmonopts & IPMON_SYSLOG))
			close(2);
#endif /* !BSD */
		close(0);
		close(1);
		write_pid(pidfile);
	}

	signal(SIGHUP, handlehup);

	for (doread = 1; doread; )
		doread = read_loginfo(&config);

	unload_config();

	return(0);
	/* NOTREACHED */
}


static void openlogs(config_t *conf)
{
	logsource_t *l;
	struct stat sb;
	int i;

	for (i = 0; i < 3; i++) {
		l = &conf->logsrc[i];
		if (l->logtype == -1)
			continue;
		if (!strcmp(l->file, "-"))
			l->fd = 0;
		else {
			if ((l->fd= open(l->file, O_RDONLY)) == -1) {
				(void) fprintf(stderr,
					       "%s: open: %s\n", l->file,
					       STRERROR(errno));
				exit(1);
				/* NOTREACHED */
			}

			if (fstat(l->fd, &sb) == -1) {
				(void) fprintf(stderr, "%d: fstat: %s\n",
					       l->fd, STRERROR(errno));
				exit(1);
				/* NOTREACHED */
			}

			l->regular = !S_ISCHR(sb.st_mode);
			if (l->regular)
				l->size = sb.st_size;

			FD_SET(l->fd, &conf->fdmr);
			if (l->fd > conf->maxfd)
				conf->maxfd = l->fd;
		}
	}
}


static int read_loginfo(config_t *conf)
{
	iplog_t buf[DEFAULT_IPFLOGSIZE/sizeof(iplog_t)+1];
	int n, tr, nr, i;
	logsource_t *l;
	fd_set fdr;

	fdr = conf->fdmr;

	n = select(conf->maxfd + 1, &fdr, NULL, NULL, NULL);
	if (n == 0)
		return 1;
	if (n == -1) {
		if (errno == EINTR)
			return 1;
		return -1;
	}

	for (i = 0, nr = 0; i < 3; i++) {
		l = &conf->logsrc[i];

		if ((l->logtype == -1) || !FD_ISSET(l->fd, &fdr))
			continue;

		tr = 0;
		if (l->regular) {
			tr = (lseek(l->fd, 0, SEEK_CUR) < l->size);
			if (!tr && !(ipmonopts & IPMON_TAIL))
				return 0;
		}

		n = 0;
		tr = read_log(l->fd, &n, (char *)buf, sizeof(buf));
		if (donehup) {
			if (conf->file != NULL) {
				if (conf->log != NULL) {
					fclose(conf->log);
					conf->log = NULL;
				}
				conf->log = fopen(conf->file, "a");
			}

			if (conf->bfile != NULL) {
				if (conf->blog != NULL) {
					fclose(conf->blog);
					conf->blog = NULL;
				}
				conf->blog = fopen(conf->bfile, "a");
			}

			init_tabs();
			if (conf->cfile != NULL)
				load_config(conf->cfile);
			donehup = 0;
		}

		switch (tr)
		{
		case -1 :
			if (ipmonopts & IPMON_SYSLOG)
				syslog(LOG_CRIT, "read: %m\n");
			else {
				ipferror(l->fd, "read");
			}
			return 0;
		case 1 :
			if (ipmonopts & IPMON_SYSLOG)
				syslog(LOG_CRIT, "aborting logging\n");
			else if (conf->log != NULL)
				fprintf(conf->log, "aborting logging\n");
			return 0;
		case 2 :
			break;
		case 0 :
			nr += tr;
			if (n > 0) {
				print_log(conf, l, (char *)buf, n);
				if (!(ipmonopts & IPMON_SYSLOG))
					fflush(conf->log);
			}
			break;
		}
	}

	if (!nr && (ipmonopts & IPMON_TAIL))
		sleep(1);

	return 1;
}
