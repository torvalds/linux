/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#include <sys/ioctl.h>
#include <ctype.h>
#include <fcntl.h>
# include <nlist.h>
#include <ctype.h>
#if defined(sun) && defined(__SVR4)
# include <stddef.h>
#endif
#include "ipf.h"
#include "netinet/ipl.h"
#if defined(STATETOP) 
# if defined(sun) && defined(__SVR4)
#   include <sys/select.h>
# endif
# include <netinet/ip_var.h>
# include <netinet/tcp_fsm.h>
# include <ctype.h>
# include <signal.h>
# include <time.h>
# if SOLARIS || defined(__NetBSD__)
#  ifdef ERR
#   undef ERR
#  endif
#  include <curses.h>
# else /* SOLARIS */
#  include <ncurses.h>
# endif /* SOLARIS */
#endif /* STATETOP */
#include "kmem.h"
#if defined(__NetBSD__)
# include <paths.h>
#endif

#if !defined(lint)
static const char sccsid[] = "@(#)fils.c	1.21 4/20/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif


extern	char	*optarg;
extern	int	optind;
extern	int	opterr;

#define	PRINTF	(void)printf
#define	FPRINTF	(void)fprintf
static	char	*filters[4] = { "ipfilter(in)", "ipfilter(out)",
				"ipacct(in)", "ipacct(out)" };
static	int	state_logging = -1;
static	wordtab_t	*state_fields = NULL;

int	nohdrfields = 0;
int	opts = 0;
int	use_inet6 = 0;
int	live_kernel = 1;
int	state_fd = -1;
int	ipf_fd = -1;
int	auth_fd = -1;
int	nat_fd = -1;
frgroup_t *grtop = NULL;
frgroup_t *grtail = NULL;

char *blockreasons[FRB_MAX_VALUE + 1] = {
	"packet blocked",
	"log rule failure",
	"pps rate exceeded",
	"jumbogram",
	"makefrip failed",
	"cannot add state",
	"IP ID update failed",
	"log-or-block failed",
	"decapsulate failure",
	"cannot create new auth entry",
	"packet queued for auth",
	"buffer coalesce failure",
	"buffer pullup failure",
	"auth feedback",
	"bad fragment",
	"IPv4 NAT failure",
	"IPv6 NAT failure"
};

#ifdef STATETOP
#define	STSTRSIZE 	80
#define	STGROWSIZE	16
#define	HOSTNMLEN	40

#define	STSORT_PR	0
#define	STSORT_PKTS	1
#define	STSORT_BYTES	2
#define	STSORT_TTL	3
#define	STSORT_SRCIP	4
#define	STSORT_SRCPT	5
#define	STSORT_DSTIP	6
#define	STSORT_DSTPT	7
#define	STSORT_MAX	STSORT_DSTPT
#define	STSORT_DEFAULT	STSORT_BYTES


typedef struct statetop {
	i6addr_t	st_src;
	i6addr_t	st_dst;
	u_short		st_sport;
	u_short 	st_dport;
	u_char		st_p;
	u_char		st_v;
	u_char		st_state[2];
	U_QUAD_T	st_pkts;
	U_QUAD_T	st_bytes;
	u_long		st_age;
} statetop_t;
#endif

int		main __P((int, char *[]));

static	int	fetchfrag __P((int, int, ipfr_t *));
static	void	showstats __P((friostat_t *, u_32_t));
static	void	showfrstates __P((ipfrstat_t *, u_long));
static	void	showlist __P((friostat_t *));
static	void	showstatestats __P((ips_stat_t *));
static	void	showipstates __P((ips_stat_t *, int *));
static	void	showauthstates __P((ipf_authstat_t *));
static	void	showtqtable_live __P((int));
static	void	showgroups __P((friostat_t *));
static	void	usage __P((char *));
static	int	state_matcharray __P((ipstate_t *, int *));
static	int	printlivelist __P((friostat_t *, int, int, frentry_t *,
				   char *, char *));
static	void	printdeadlist __P((friostat_t *, int, int, frentry_t *,
				   char *, char *));
static	void	printside __P((char *, ipf_statistics_t *));
static	void	parse_ipportstr __P((const char *, i6addr_t *, int *));
static	void	ipfstate_live __P((char *, friostat_t **, ips_stat_t **,
				   ipfrstat_t **, ipf_authstat_t **, u_32_t *));
static	void	ipfstate_dead __P((char *, friostat_t **, ips_stat_t **,
				   ipfrstat_t **, ipf_authstat_t **, u_32_t *));
static	ipstate_t *fetchstate __P((ipstate_t *, ipstate_t *));
#ifdef STATETOP
static	void	topipstates __P((i6addr_t, i6addr_t, int, int, int,
				 int, int, int, int *));
static	void	sig_break __P((int));
static	void	sig_resize __P((int));
static	char	*getip __P((int, i6addr_t *));
static	char	*ttl_to_string __P((long));
static	int	sort_p __P((const void *, const void *));
static	int	sort_pkts __P((const void *, const void *));
static	int	sort_bytes __P((const void *, const void *));
static	int	sort_ttl __P((const void *, const void *));
static	int	sort_srcip __P((const void *, const void *));
static	int	sort_srcpt __P((const void *, const void *));
static	int	sort_dstip __P((const void *, const void *));
static	int	sort_dstpt __P((const void *, const void *));
#endif


static void usage(name)
	char *name;
{
#ifdef  USE_INET6
	fprintf(stderr, "Usage: %s [-6aAdfghIilnoRsv]\n", name);
#else
	fprintf(stderr, "Usage: %s [-aAdfghIilnoRsv]\n", name);
#endif
	fprintf(stderr, "       %s [-M corefile] [-N symbol-list]\n", name);
#ifdef	USE_INET6
	fprintf(stderr, "       %s -t [-6C] ", name);
#else
	fprintf(stderr, "       %s -t [-C] ", name);
#endif
	fprintf(stderr, "[-D destination address] [-P protocol] [-S source address] [-T refresh time]\n");
	exit(1);
}


int main(argc,argv)
	int argc;
	char *argv[];
{
	ipf_authstat_t	frauthst;
	ipf_authstat_t	*frauthstp = &frauthst;
	friostat_t fio;
	friostat_t *fiop = &fio;
	ips_stat_t ipsst;
	ips_stat_t *ipsstp = &ipsst;
	ipfrstat_t ifrst;
	ipfrstat_t *ifrstp = &ifrst;
	char *options;
	char *kern = NULL;
	char *memf = NULL;
	int c;
	int myoptind;
	int *filter = NULL;

	int protocol = -1;		/* -1 = wild card for any protocol */
	int refreshtime = 1; 		/* default update time */
	int sport = -1;			/* -1 = wild card for any source port */
	int dport = -1;			/* -1 = wild card for any dest port */
	int topclosed = 0;		/* do not show closed tcp sessions */
	i6addr_t saddr, daddr;
	u_32_t frf;

#ifdef	USE_INET6
	options = "6aACdfghIilnostvD:m:M:N:O:P:RS:T:";
#else
	options = "aACdfghIilnostvD:m:M:N:O:P:RS:T:";
#endif

	saddr.in4.s_addr = INADDR_ANY; 	/* default any v4 source addr */
	daddr.in4.s_addr = INADDR_ANY; 	/* default any v4 dest addr */
#ifdef	USE_INET6
	saddr.in6 = in6addr_any;	/* default any v6 source addr */
	daddr.in6 = in6addr_any;	/* default any v6 dest addr */
#endif

	/* Don't warn about invalid flags when we run getopt for the 1st time */
	opterr = 0;

	/*
	 * Parse these two arguments now lest there be any buffer overflows
	 * in the parsing of the rest.
	 */
	myoptind = optind;
	while ((c = getopt(argc, argv, options)) != -1) {
		switch (c)
		{
		case 'M' :
			memf = optarg;
			live_kernel = 0;
			break;
		case 'N' :
			kern = optarg;
			live_kernel = 0;
			break;
		}
	}
	optind = myoptind;

	if (live_kernel == 1) {
		if ((state_fd = open(IPSTATE_NAME, O_RDONLY)) == -1) {
			perror("open(IPSTATE_NAME)");
			exit(-1);
		}
		if ((auth_fd = open(IPAUTH_NAME, O_RDONLY)) == -1) {
			perror("open(IPAUTH_NAME)");
			exit(-1);
		}
		if ((nat_fd = open(IPNAT_NAME, O_RDONLY)) == -1) {
			perror("open(IPAUTH_NAME)");
			exit(-1);
		}
		if ((ipf_fd = open(IPL_NAME, O_RDONLY)) == -1) {
			fprintf(stderr, "open(%s)", IPL_NAME);
			perror("");
			exit(-1);
		}
	}

	if (kern != NULL || memf != NULL) {
		(void)setgid(getgid());
		(void)setuid(getuid());
	}

	if (live_kernel == 1) {
		(void) checkrev(IPL_NAME);
	} else {
		if (openkmem(kern, memf) == -1)
			exit(-1);
	}

	(void)setgid(getgid());
	(void)setuid(getuid());

	opterr = 1;

	while ((c = getopt(argc, argv, options)) != -1)
	{
		switch (c)
		{
#ifdef	USE_INET6
		case '6' :
			use_inet6 = 1;
			break;
#endif
		case 'a' :
			opts |= OPT_ACCNT|OPT_SHOWLIST;
			break;
		case 'A' :
			opts |= OPT_AUTHSTATS;
			break;
		case 'C' :
			topclosed = 1;
			break;
		case 'd' :
			opts |= OPT_DEBUG;
			break;
		case 'D' :
			parse_ipportstr(optarg, &daddr, &dport);
			break;
		case 'f' :
			opts |= OPT_FRSTATES;
			break;
		case 'g' :
			opts |= OPT_GROUPS;
			break;
		case 'h' :
			opts |= OPT_HITS;
			break;
		case 'i' :
			opts |= OPT_INQUE|OPT_SHOWLIST;
			break;
		case 'I' :
			opts |= OPT_INACTIVE;
			break;
		case 'l' :
			opts |= OPT_SHOWLIST;
			break;
		case 'm' :
			filter = parseipfexpr(optarg, NULL);
			if (filter == NULL) {
				fprintf(stderr, "Error parseing '%s'\n",
					optarg);
				exit(1);
			}
			break;
		case 'M' :
			break;
		case 'N' :
			break;
		case 'n' :
			opts |= OPT_SHOWLINENO;
			break;
		case 'o' :
			opts |= OPT_OUTQUE|OPT_SHOWLIST;
			break;
		case 'O' :
			state_fields = parsefields(statefields, optarg);
			break;
		case 'P' :
			protocol = getproto(optarg);
			if (protocol == -1) {
				fprintf(stderr, "%s: Invalid protocol: %s\n",
					argv[0], optarg);
				exit(-2);
			}
			break;
		case 'R' :
			opts |= OPT_NORESOLVE;
			break;
		case 's' :
			opts |= OPT_IPSTATES;
			break;
		case 'S' :
			parse_ipportstr(optarg, &saddr, &sport);
			break;
		case 't' :
#ifdef STATETOP
			opts |= OPT_STATETOP;
			break;
#else
			fprintf(stderr,
				"%s: state top facility not compiled in\n",
				argv[0]);
			exit(-2);
#endif
		case 'T' :
			if (!sscanf(optarg, "%d", &refreshtime) ||
				    (refreshtime <= 0)) {
				fprintf(stderr,
					"%s: Invalid refreshtime < 1 : %s\n",
					argv[0], optarg);
				exit(-2);
			}
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		default :
			usage(argv[0]);
			break;
		}
	}

	if (live_kernel == 1) {
		bzero((char *)&fio, sizeof(fio));
		bzero((char *)&ipsst, sizeof(ipsst));
		bzero((char *)&ifrst, sizeof(ifrst));

		ipfstate_live(IPL_NAME, &fiop, &ipsstp, &ifrstp,
			      &frauthstp, &frf);
	} else {
		ipfstate_dead(kern, &fiop, &ipsstp, &ifrstp, &frauthstp, &frf);
	}

	if (opts & OPT_IPSTATES) {
		showipstates(ipsstp, filter);
	} else if (opts & OPT_SHOWLIST) {
		showlist(fiop);
		if ((opts & OPT_OUTQUE) && (opts & OPT_INQUE)){
			opts &= ~OPT_OUTQUE;
			showlist(fiop);
		}
	} else if (opts & OPT_FRSTATES)
		showfrstates(ifrstp, fiop->f_ticks);
#ifdef STATETOP
	else if (opts & OPT_STATETOP)
		topipstates(saddr, daddr, sport, dport, protocol,
			    use_inet6 ? 6 : 4, refreshtime, topclosed, filter);
#endif
	else if (opts & OPT_AUTHSTATS)
		showauthstates(frauthstp);
	else if (opts & OPT_GROUPS)
		showgroups(fiop);
	else
		showstats(fiop, frf);

	return 0;
}


/*
 * Fill in the stats structures from the live kernel, using a combination
 * of ioctl's and copying directly from kernel memory.
 */
static void ipfstate_live(device, fiopp, ipsstpp, ifrstpp, frauthstpp, frfp)
	char *device;
	friostat_t **fiopp;
	ips_stat_t **ipsstpp;
	ipfrstat_t **ifrstpp;
	ipf_authstat_t **frauthstpp;
	u_32_t *frfp;
{
	ipfobj_t ipfo;

	if (checkrev(device) == -1) {
		fprintf(stderr, "User/kernel version check failed\n");
		exit(1);
	}

	if ((opts & OPT_AUTHSTATS) == 0) {
		bzero((caddr_t)&ipfo, sizeof(ipfo));
		ipfo.ipfo_rev = IPFILTER_VERSION;
		ipfo.ipfo_type = IPFOBJ_IPFSTAT;
		ipfo.ipfo_size = sizeof(friostat_t);
		ipfo.ipfo_ptr = (void *)*fiopp;

		if (ioctl(ipf_fd, SIOCGETFS, &ipfo) == -1) {
			ipferror(ipf_fd, "ioctl(ipf:SIOCGETFS)");
			exit(-1);
		}

		if (ioctl(ipf_fd, SIOCGETFF, frfp) == -1)
			ipferror(ipf_fd, "ioctl(SIOCGETFF)");
	}

	if ((opts & OPT_IPSTATES) != 0) {

		bzero((caddr_t)&ipfo, sizeof(ipfo));
		ipfo.ipfo_rev = IPFILTER_VERSION;
		ipfo.ipfo_type = IPFOBJ_STATESTAT;
		ipfo.ipfo_size = sizeof(ips_stat_t);
		ipfo.ipfo_ptr = (void *)*ipsstpp;

		if ((ioctl(state_fd, SIOCGETFS, &ipfo) == -1)) {
			ipferror(state_fd, "ioctl(state:SIOCGETFS)");
			exit(-1);
		}
		if (ioctl(state_fd, SIOCGETLG, &state_logging) == -1) {
			ipferror(state_fd, "ioctl(state:SIOCGETLG)");
			exit(-1);
		}
	}

	if ((opts & OPT_FRSTATES) != 0) {
		bzero((caddr_t)&ipfo, sizeof(ipfo));
		ipfo.ipfo_rev = IPFILTER_VERSION;
		ipfo.ipfo_type = IPFOBJ_FRAGSTAT;
		ipfo.ipfo_size = sizeof(ipfrstat_t);
		ipfo.ipfo_ptr = (void *)*ifrstpp;

		if (ioctl(ipf_fd, SIOCGFRST, &ipfo) == -1) {
			ipferror(ipf_fd, "ioctl(SIOCGFRST)");
			exit(-1);
		}
	}

	if (opts & OPT_DEBUG)
		PRINTF("opts %#x name %s\n", opts, device);

	if ((opts & OPT_AUTHSTATS) != 0) {
		bzero((caddr_t)&ipfo, sizeof(ipfo));
		ipfo.ipfo_rev = IPFILTER_VERSION;
		ipfo.ipfo_type = IPFOBJ_AUTHSTAT;
		ipfo.ipfo_size = sizeof(ipf_authstat_t);
		ipfo.ipfo_ptr = (void *)*frauthstpp;

	    	if (ioctl(auth_fd, SIOCATHST, &ipfo) == -1) {
			ipferror(auth_fd, "ioctl(SIOCATHST)");
			exit(-1);
		}
	}
}


/*
 * Build up the stats structures from data held in the "core" memory.
 * This is mainly useful when looking at data in crash dumps and ioctl's
 * just won't work any more.
 */
static void ipfstate_dead(kernel, fiopp, ipsstpp, ifrstpp, frauthstpp, frfp)
	char *kernel;
	friostat_t **fiopp;
	ips_stat_t **ipsstpp;
	ipfrstat_t **ifrstpp;
	ipf_authstat_t **frauthstpp;
	u_32_t *frfp;
{
	static ipf_authstat_t frauthst, *frauthstp;
	static ipftq_t ipstcptab[IPF_TCP_NSTATES];
	static ips_stat_t ipsst, *ipsstp;
	static ipfrstat_t ifrst, *ifrstp;
	static friostat_t fio, *fiop;
	int temp;

	void *rules[2][2];
	struct nlist deadlist[44] = {
		{ "ipf_auth_stats",	0, 0, 0, 0 },		/* 0 */
		{ "fae_list",		0, 0, 0, 0 },
		{ "ipauth",		0, 0, 0, 0 },
		{ "ipf_auth_list",		0, 0, 0, 0 },
		{ "ipf_auth_start",		0, 0, 0, 0 },
		{ "ipf_auth_end",		0, 0, 0, 0 },		/* 5 */
		{ "ipf_auth_next",		0, 0, 0, 0 },
		{ "ipf_auth",		0, 0, 0, 0 },
		{ "ipf_auth_used",		0, 0, 0, 0 },
		{ "ipf_auth_size",		0, 0, 0, 0 },
		{ "ipf_auth_defaultage",		0, 0, 0, 0 },	/* 10 */
		{ "ipf_auth_pkts",		0, 0, 0, 0 },
		{ "ipf_auth_lock",		0, 0, 0, 0 },
		{ "frstats",		0, 0, 0, 0 },
		{ "ips_stats",		0, 0, 0, 0 },
		{ "ips_num",		0, 0, 0, 0 },			/* 15 */
		{ "ips_wild",		0, 0, 0, 0 },
		{ "ips_list",		0, 0, 0, 0 },
		{ "ips_table",		0, 0, 0, 0 },
		{ "ipf_state_max",		0, 0, 0, 0 },
		{ "ipf_state_size",		0, 0, 0, 0 },		/* 20 */
		{ "ipf_state_doflush",		0, 0, 0, 0 },
		{ "ipf_state_lock",		0, 0, 0, 0 },
		{ "ipfr_heads",		0, 0, 0, 0 },
		{ "ipfr_nattab",		0, 0, 0, 0 },
		{ "ipfr_stats",		0, 0, 0, 0 },		/* 25 */
		{ "ipfr_inuse",		0, 0, 0, 0 },
		{ "ipf_ipfrttl",		0, 0, 0, 0 },
		{ "ipf_frag_lock",		0, 0, 0, 0 },
		{ "ipfr_timer_id",		0, 0, 0, 0 },
		{ "ipf_nat_lock",		0, 0, 0, 0 },		/* 30 */
		{ "ipf_rules",		0, 0, 0, 0 },
		{ "ipf_acct",		0, 0, 0, 0 },
		{ "ipl_frouteok",		0, 0, 0, 0 },
		{ "ipf_running",		0, 0, 0, 0 },
		{ "ipf_groups",		0, 0, 0, 0 },		/* 35 */
		{ "ipf_active",		0, 0, 0, 0 },
		{ "ipf_pass",		0, 0, 0, 0 },
		{ "ipf_flags",		0, 0, 0, 0 },
		{ "ipf_state_logging",		0, 0, 0, 0 },
		{ "ips_tqtqb",		0, 0, 0, 0 },		/* 40 */
		{ NULL,		0, 0, 0, 0 }
	};


	frauthstp = &frauthst;
	ipsstp = &ipsst;
	ifrstp = &ifrst;
	fiop = &fio;

	*frfp = 0;
	*fiopp = fiop;
	*ipsstpp = ipsstp;
	*ifrstpp = ifrstp;
	*frauthstpp = frauthstp;

	bzero((char *)fiop, sizeof(*fiop));
	bzero((char *)ipsstp, sizeof(*ipsstp));
	bzero((char *)ifrstp, sizeof(*ifrstp));
	bzero((char *)frauthstp, sizeof(*frauthstp));

	if (nlist(kernel, deadlist) == -1) {
		fprintf(stderr, "nlist error\n");
		return;
	}

	/*
	 * This is for SIOCGETFF.
	 */
	kmemcpy((char *)frfp, (u_long)deadlist[40].n_value, sizeof(*frfp));

	/*
	 * f_locks is a combination of the lock variable from each part of
	 * ipfilter (state, auth, nat, fragments).
	 */
	kmemcpy((char *)fiop, (u_long)deadlist[13].n_value, sizeof(*fiop));
	kmemcpy((char *)&fiop->f_locks[0], (u_long)deadlist[22].n_value,
		sizeof(fiop->f_locks[0]));
	kmemcpy((char *)&fiop->f_locks[0], (u_long)deadlist[30].n_value,
		sizeof(fiop->f_locks[1]));
	kmemcpy((char *)&fiop->f_locks[2], (u_long)deadlist[28].n_value,
		sizeof(fiop->f_locks[2]));
	kmemcpy((char *)&fiop->f_locks[3], (u_long)deadlist[12].n_value,
		sizeof(fiop->f_locks[3]));

	/*
	 * Get pointers to each list of rules (active, inactive, in, out)
	 */
	kmemcpy((char *)&rules, (u_long)deadlist[31].n_value, sizeof(rules));
	fiop->f_fin[0] = rules[0][0];
	fiop->f_fin[1] = rules[0][1];
	fiop->f_fout[0] = rules[1][0];
	fiop->f_fout[1] = rules[1][1];

	/*
	 * Now get accounting rules pointers.
	 */
	kmemcpy((char *)&rules, (u_long)deadlist[33].n_value, sizeof(rules));
	fiop->f_acctin[0] = rules[0][0];
	fiop->f_acctin[1] = rules[0][1];
	fiop->f_acctout[0] = rules[1][0];
	fiop->f_acctout[1] = rules[1][1];

	/*
	 * A collection of "global" variables used inside the kernel which
	 * are all collected in friostat_t via ioctl.
	 */
	kmemcpy((char *)&fiop->f_froute, (u_long)deadlist[33].n_value,
		sizeof(fiop->f_froute));
	kmemcpy((char *)&fiop->f_running, (u_long)deadlist[34].n_value,
		sizeof(fiop->f_running));
	kmemcpy((char *)&fiop->f_groups, (u_long)deadlist[35].n_value,
		sizeof(fiop->f_groups));
	kmemcpy((char *)&fiop->f_active, (u_long)deadlist[36].n_value,
		sizeof(fiop->f_active));
	kmemcpy((char *)&fiop->f_defpass, (u_long)deadlist[37].n_value,
		sizeof(fiop->f_defpass));

	/*
	 * Build up the state information stats structure.
	 */
	kmemcpy((char *)ipsstp, (u_long)deadlist[14].n_value, sizeof(*ipsstp));
	kmemcpy((char *)&temp, (u_long)deadlist[15].n_value, sizeof(temp));
	kmemcpy((char *)ipstcptab, (u_long)deadlist[40].n_value,
		sizeof(ipstcptab));
	ipsstp->iss_active = temp;
	ipsstp->iss_table = (void *)deadlist[18].n_value;
	ipsstp->iss_list = (void *)deadlist[17].n_value;
	ipsstp->iss_tcptab = ipstcptab;

	/*
	 * Build up the authentiation information stats structure.
	 */
	kmemcpy((char *)frauthstp, (u_long)deadlist[0].n_value,
		sizeof(*frauthstp));
	frauthstp->fas_faelist = (void *)deadlist[1].n_value;

	/*
	 * Build up the fragment information stats structure.
	 */
	kmemcpy((char *)ifrstp, (u_long)deadlist[25].n_value,
		sizeof(*ifrstp));
	ifrstp->ifs_table = (void *)deadlist[23].n_value;
	ifrstp->ifs_nattab = (void *)deadlist[24].n_value;
	kmemcpy((char *)&ifrstp->ifs_inuse, (u_long)deadlist[26].n_value,
		sizeof(ifrstp->ifs_inuse));

	/*
	 * Get logging on/off switches
	 */
	kmemcpy((char *)&state_logging, (u_long)deadlist[41].n_value,
		sizeof(state_logging));
}


static void printside(side, frs)
	char *side;
	ipf_statistics_t *frs;
{
	int i;

	PRINTF("%lu\t%s bad packets\n", frs->fr_bad, side);
#ifdef	USE_INET6
	PRINTF("%lu\t%s IPv6 packets\n", frs->fr_ipv6, side);
#endif
	PRINTF("%lu\t%s packets blocked\n", frs->fr_block, side);
	PRINTF("%lu\t%s packets passed\n", frs->fr_pass, side);
	PRINTF("%lu\t%s packets not matched\n", frs->fr_nom, side);
	PRINTF("%lu\t%s packets counted\n", frs->fr_acct, side);
	PRINTF("%lu\t%s packets short\n", frs->fr_short, side);
	PRINTF("%lu\t%s packets logged and blocked\n", frs->fr_bpkl, side);
	PRINTF("%lu\t%s packets logged and passed\n", frs->fr_ppkl, side);
	PRINTF("%lu\t%s fragment state kept\n", frs->fr_nfr, side);
	PRINTF("%lu\t%s fragment state lost\n", frs->fr_bnfr, side);
	PRINTF("%lu\t%s packet state kept\n", frs->fr_ads, side);
	PRINTF("%lu\t%s packet state lost\n", frs->fr_bads, side);
	PRINTF("%lu\t%s invalid source\n", frs->fr_v4_badsrc, side);
	PRINTF("%lu\t%s cache hits\n", frs->fr_chit, side);
	PRINTF("%lu\t%s cache misses\n", frs->fr_cmiss, side);
	PRINTF("%lu\t%s bad coalesces\n", frs->fr_badcoalesces, side);
	PRINTF("%lu\t%s pullups succeeded\n", frs->fr_pull[0], side);
	PRINTF("%lu\t%s pullups failed\n", frs->fr_pull[1], side);
	PRINTF("%lu\t%s TCP checksum failures\n", frs->fr_tcpbad, side);
	for (i = 0; i <= FRB_MAX_VALUE; i++)
		PRINTF("%lu\t%s block reason %s\n",
			frs->fr_blocked[i], side, blockreasons[i]);
}


/*
 * Display the kernel stats for packets blocked and passed and other
 * associated running totals which are kept.
 */
static	void	showstats(fp, frf)
	struct	friostat	*fp;
	u_32_t frf;
{
	printside("input", &fp->f_st[0]);
	printside("output", &fp->f_st[1]);

	PRINTF("%lu\tpackets logged\n", fp->f_log_ok);
	PRINTF("%lu\tlog failures\n", fp->f_log_fail);
	PRINTF("%lu\tred-black no memory\n", fp->f_rb_no_mem);
	PRINTF("%lu\tred-black node maximum\n", fp->f_rb_node_max);
	PRINTF("%lu\tICMP replies sent\n", fp->f_st[0].fr_ret);
	PRINTF("%lu\tTCP RSTs sent\n", fp->f_st[1].fr_ret);
	PRINTF("%lu\tfastroute successes\n", fp->f_froute[0]);
	PRINTF("%lu\tfastroute failures\n", fp->f_froute[1]);
	PRINTF("%u\tIPF Ticks\n", fp->f_ticks);

	PRINTF("%x\tPacket log flags set:\n", frf);
	if (frf & FF_LOGPASS)
		PRINTF("\tpackets passed through filter\n");
	if (frf & FF_LOGBLOCK)
		PRINTF("\tpackets blocked by filter\n");
	if (frf & FF_LOGNOMATCH)
		PRINTF("\tpackets not matched by filter\n");
	if (!frf)
		PRINTF("\tnone\n");
}


/*
 * Print out a list of rules from the kernel, starting at the one passed.
 */
static int
printlivelist(fiop, out, set, fp, group, comment)
	struct friostat *fiop;
	int out, set;
	frentry_t *fp;
	char *group, *comment;
{
	struct	frentry	fb;
	ipfruleiter_t rule;
	frentry_t zero;
	frgroup_t *g;
	ipfobj_t obj;
	int rules;
	int num;

	rules = 0;

	rule.iri_inout = out;
	rule.iri_active = set;
	rule.iri_rule = &fb;
	rule.iri_nrules = 1;
	if (group != NULL)
		strncpy(rule.iri_group, group, FR_GROUPLEN);
	else
		rule.iri_group[0] = '\0';

	bzero((char *)&zero, sizeof(zero));

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_IPFITER;
	obj.ipfo_size = sizeof(rule);
	obj.ipfo_ptr = &rule;

	while (rule.iri_rule != NULL) {
		u_long array[1000];

		memset(array, 0xff, sizeof(array));
		fp = (frentry_t *)array;
		rule.iri_rule = fp;
		if (ioctl(ipf_fd, SIOCIPFITER, &obj) == -1) {
			ipferror(ipf_fd, "ioctl(SIOCIPFITER)");
			num = IPFGENITER_IPF;
			(void) ioctl(ipf_fd,SIOCIPFDELTOK, &num);
			return rules;
		}
		if (bcmp(fp, &zero, sizeof(zero)) == 0)
			break;
		if (rule.iri_rule == NULL)
			break;
#ifdef USE_INET6
		if (use_inet6 != 0) {
			if (fp->fr_family != 0 && fp->fr_family != AF_INET6)
				continue;
		} else
#endif
		{
			if (fp->fr_family != 0 && fp->fr_family != AF_INET)
				continue;
		}
		if (fp->fr_data != NULL)
			fp->fr_data = (char *)fp + fp->fr_size;

		rules++;

		if (opts & (OPT_HITS|OPT_DEBUG))
#ifdef	USE_QUAD_T
			PRINTF("%"PRIu64" ", (unsigned long long) fp->fr_hits);
#else
			PRINTF("%lu ", fp->fr_hits);
#endif
		if (opts & (OPT_ACCNT|OPT_DEBUG))
#ifdef	USE_QUAD_T
			PRINTF("%"PRIu64" ", (unsigned long long) fp->fr_bytes);
#else
			PRINTF("%lu ", fp->fr_bytes);
#endif
		if (opts & OPT_SHOWLINENO)
			PRINTF("@%d ", rules);

		if (fp->fr_die != 0)
			fp->fr_die -= fiop->f_ticks;

		printfr(fp, ioctl);
		if (opts & OPT_DEBUG) {
			binprint(fp, fp->fr_size);
			if (fp->fr_data != NULL && fp->fr_dsize > 0)
				binprint(fp->fr_data, fp->fr_dsize);
		}
		if (fp->fr_grhead != -1) {
			for (g = grtop; g != NULL; g = g->fg_next) {
				if (!strncmp(fp->fr_names + fp->fr_grhead,
					     g->fg_name,
					     FR_GROUPLEN))
					break;
			}
			if (g == NULL) {
				g = calloc(1, sizeof(*g));

				if (g != NULL) {
					strncpy(g->fg_name,
						fp->fr_names + fp->fr_grhead,
						FR_GROUPLEN);
					if (grtop == NULL) {
						grtop = g;
						grtail = g;
					} else {
						grtail->fg_next = g;
						grtail = g;
					}
				}
			}
		}
		if (fp->fr_type == FR_T_CALLFUNC) {
			rules += printlivelist(fiop, out, set, fp->fr_data,
					       group, "# callfunc: ");
		}
	}

	num = IPFGENITER_IPF;
	(void) ioctl(ipf_fd,SIOCIPFDELTOK, &num);

	return rules;
}


static void printdeadlist(fiop, out, set, fp, group, comment)
	friostat_t *fiop;
	int out, set;
	frentry_t *fp;
	char *group, *comment;
{
	frgroup_t *grtop, *grtail, *g;
	struct	frentry	fb;
	char	*data;
	u_32_t	type;
	int	n;

	fb.fr_next = fp;
	n = 0;
	grtop = NULL;
	grtail = NULL;

	for (n = 1; fp; fp = fb.fr_next, n++) {
		if (kmemcpy((char *)&fb, (u_long)fb.fr_next,
			    fb.fr_size) == -1) {
			perror("kmemcpy");
			return;
		}
		fp = &fb;
		if (use_inet6 != 0) {
			if (fp->fr_family != 0 && fp->fr_family != 6)
				continue;
		} else {
			if (fp->fr_family != 0 && fp->fr_family != 4)
				continue;
		}

		data = NULL;
		type = fb.fr_type & ~FR_T_BUILTIN;
		if (type == FR_T_IPF || type == FR_T_BPFOPC) {
			if (fb.fr_dsize) {
				data = malloc(fb.fr_dsize);

				if (kmemcpy(data, (u_long)fb.fr_data,
					    fb.fr_dsize) == -1) {
					perror("kmemcpy");
					return;
				}
				fb.fr_data = data;
			}
		}

		if (opts & OPT_HITS)
#ifdef	USE_QUAD_T
			PRINTF("%"PRIu64" ", (unsigned long long) fb.fr_hits);
#else
			PRINTF("%lu ", fb.fr_hits);
#endif
		if (opts & OPT_ACCNT)
#ifdef	USE_QUAD_T
			PRINTF("%"PRIu64" ", (unsigned long long) fb.fr_bytes);
#else
			PRINTF("%lu ", fb.fr_bytes);
#endif
		if (opts & OPT_SHOWLINENO)
			PRINTF("@%d ", n);

		printfr(fp, ioctl);
		if (opts & OPT_DEBUG) {
			binprint(fp, fp->fr_size);
			if (fb.fr_data != NULL && fb.fr_dsize > 0)
				binprint(fb.fr_data, fb.fr_dsize);
		}
		if (data != NULL)
			free(data);
		if (fb.fr_grhead != -1) {
			g = calloc(1, sizeof(*g));

			if (g != NULL) {
				strncpy(g->fg_name, fb.fr_names + fb.fr_grhead,
					FR_GROUPLEN);
				if (grtop == NULL) {
					grtop = g;
					grtail = g;
				} else {
					grtail->fg_next = g;
					grtail = g;
				}
			}
		}
		if (type == FR_T_CALLFUNC) {
			printdeadlist(fiop, out, set, fb.fr_data, group,
				      "# callfunc: ");
		}
	}

	while ((g = grtop) != NULL) {
		printdeadlist(fiop, out, set, NULL, g->fg_name, comment);
		grtop = g->fg_next;
		free(g);
	}
}

/*
 * print out all of the asked for rule sets, using the stats struct as
 * the base from which to get the pointers.
 */
static	void	showlist(fiop)
	struct	friostat	*fiop;
{
	struct	frentry	*fp = NULL;
	int	i, set;

	set = fiop->f_active;
	if (opts & OPT_INACTIVE)
		set = 1 - set;
	if (opts & OPT_ACCNT) {
		if (opts & OPT_OUTQUE) {
			i = F_ACOUT;
			fp = (struct frentry *)fiop->f_acctout[set];
		} else if (opts & OPT_INQUE) {
			i = F_ACIN;
			fp = (struct frentry *)fiop->f_acctin[set];
		} else {
			FPRINTF(stderr, "No -i or -o given with -a\n");
			return;
		}
	} else {
		if (opts & OPT_OUTQUE) {
			i = F_OUT;
			fp = (struct frentry *)fiop->f_fout[set];
		} else if (opts & OPT_INQUE) {
			i = F_IN;
			fp = (struct frentry *)fiop->f_fin[set];
		} else
			return;
	}
	if (opts & OPT_DEBUG)
		FPRINTF(stderr, "showlist:opts %#x i %d\n", opts, i);

	if (opts & OPT_DEBUG)
		PRINTF("fp %p set %d\n", fp, set);

	if (live_kernel == 1) {
		int printed;

		printed = printlivelist(fiop, i, set, fp, NULL, NULL);
		if (printed == 0) {
			FPRINTF(stderr, "# empty list for %s%s\n",
			        (opts & OPT_INACTIVE) ? "inactive " : "",
							filters[i]);
		}
	} else {
		if (!fp) {
			FPRINTF(stderr, "# empty list for %s%s\n",
			        (opts & OPT_INACTIVE) ? "inactive " : "",
							filters[i]);
		} else {
			printdeadlist(fiop, i, set, fp, NULL, NULL);
		}
	}
}


/*
 * Display ipfilter stateful filtering information
 */
static void showipstates(ipsp, filter)
	ips_stat_t *ipsp;
	int *filter;
{
	ipstate_t *is;
	int i;

	/*
	 * If a list of states hasn't been asked for, only print out stats
	 */
	if (!(opts & OPT_SHOWLIST)) {
		showstatestats(ipsp);
		return;
	}

	if ((state_fields != NULL) && (nohdrfields == 0)) {
		for (i = 0; state_fields[i].w_value != 0; i++) {
			printfieldhdr(statefields, state_fields + i);
			if (state_fields[i + 1].w_value != 0)
				printf("\t");
		}
		printf("\n");
	}

	/*
	 * Print out all the state information currently held in the kernel.
	 */
	for (is = ipsp->iss_list; is != NULL; ) {
		ipstate_t ips;

		is = fetchstate(is, &ips);

		if (is == NULL)
			break;

		is = ips.is_next;
		if ((filter != NULL) &&
		    (state_matcharray(&ips, filter) == 0)) {
			continue;
		}
		if (state_fields != NULL) {
			for (i = 0; state_fields[i].w_value != 0; i++) {
				printstatefield(&ips, state_fields[i].w_value);
				if (state_fields[i + 1].w_value != 0)
					printf("\t");
			}
			printf("\n");
		} else {
			printstate(&ips, opts, ipsp->iss_ticks);
		}
	}
}


static void showstatestats(ipsp)
	ips_stat_t *ipsp;
{
	int minlen, maxlen, totallen;
	ipftable_t table;
	u_int *buckets;
	ipfobj_t obj;
	int i, sz;

	/*
	 * If a list of states hasn't been asked for, only print out stats
	 */

	sz = sizeof(*buckets) * ipsp->iss_state_size;
	buckets = (u_int *)malloc(sz);

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GTABLE;
	obj.ipfo_size = sizeof(table);
	obj.ipfo_ptr = &table;

	table.ita_type = IPFTABLE_BUCKETS;
	table.ita_table = buckets;

	if (live_kernel == 1) {
		if (ioctl(state_fd, SIOCGTABL, &obj) != 0) {
			free(buckets);
			return;
		}
	} else {
		if (kmemcpy((char *)buckets,
			    (u_long)ipsp->iss_bucketlen, sz)) {
			free(buckets);
			return;
		}
	}

	PRINTF("%u\tactive state table entries\n",ipsp->iss_active);
	PRINTF("%lu\tadd bad\n", ipsp->iss_add_bad);
	PRINTF("%lu\tadd duplicate\n", ipsp->iss_add_dup);
	PRINTF("%lu\tadd locked\n", ipsp->iss_add_locked);
	PRINTF("%lu\tadd oow\n", ipsp->iss_add_oow);
	PRINTF("%lu\tbucket full\n", ipsp->iss_bucket_full);
	PRINTF("%lu\tcheck bad\n", ipsp->iss_check_bad);
	PRINTF("%lu\tcheck miss\n", ipsp->iss_check_miss);
	PRINTF("%lu\tcheck nattag\n", ipsp->iss_check_nattag);
	PRINTF("%lu\tclone nomem\n", ipsp->iss_clone_nomem);
	PRINTF("%lu\tcheck notag\n", ipsp->iss_check_notag);
	PRINTF("%lu\tcheck success\n", ipsp->iss_hits);
	PRINTF("%lu\tcloned\n", ipsp->iss_cloned);
	PRINTF("%lu\texpired\n", ipsp->iss_expire);
	PRINTF("%lu\tflush all\n", ipsp->iss_flush_all);
	PRINTF("%lu\tflush closing\n", ipsp->iss_flush_closing);
	PRINTF("%lu\tflush queue\n", ipsp->iss_flush_queue);
	PRINTF("%lu\tflush state\n", ipsp->iss_flush_state);
	PRINTF("%lu\tflush timeout\n", ipsp->iss_flush_timeout);
	PRINTF("%u\thash buckets in use\n", ipsp->iss_inuse);
	PRINTF("%lu\tICMP bad\n", ipsp->iss_icmp_bad);
	PRINTF("%lu\tICMP banned\n", ipsp->iss_icmp_banned);
	PRINTF("%lu\tICMP errors\n", ipsp->iss_icmp_icmperr);
	PRINTF("%lu\tICMP head block\n", ipsp->iss_icmp_headblock);
	PRINTF("%lu\tICMP hits\n", ipsp->iss_icmp_hits);
	PRINTF("%lu\tICMP not query\n",	ipsp->iss_icmp_notquery);
	PRINTF("%lu\tICMP short\n", ipsp->iss_icmp_short);
	PRINTF("%lu\tICMP too many\n", ipsp->iss_icmp_toomany);
	PRINTF("%lu\tICMPv6 errors\n", ipsp->iss_icmp6_icmperr);
	PRINTF("%lu\tICMPv6 miss\n", ipsp->iss_icmp6_miss);
	PRINTF("%lu\tICMPv6 not info\n", ipsp->iss_icmp6_notinfo);
	PRINTF("%lu\tICMPv6 not query\n", ipsp->iss_icmp6_notquery);
	PRINTF("%lu\tlog fail\n", ipsp->iss_log_fail);
	PRINTF("%lu\tlog ok\n", ipsp->iss_log_ok);
	PRINTF("%lu\tlookup interface mismatch\n", ipsp->iss_lookup_badifp);
	PRINTF("%lu\tlookup mask mismatch\n", ipsp->iss_miss_mask);
	PRINTF("%lu\tlookup port mismatch\n", ipsp->iss_lookup_badport);
	PRINTF("%lu\tlookup miss\n", ipsp->iss_lookup_miss);
	PRINTF("%lu\tmaximum rule references\n", ipsp->iss_max_ref);
	PRINTF("%lu\tmaximum hosts per rule\n", ipsp->iss_max_track);
	PRINTF("%lu\tno memory\n", ipsp->iss_nomem);
	PRINTF("%lu\tout of window\n", ipsp->iss_oow);
	PRINTF("%lu\torphans\n", ipsp->iss_orphan);
	PRINTF("%lu\tscan block\n", ipsp->iss_scan_block);
	PRINTF("%lu\tstate table maximum reached\n", ipsp->iss_max);
	PRINTF("%lu\tTCP closing\n", ipsp->iss_tcp_closing);
	PRINTF("%lu\tTCP OOW\n", ipsp->iss_tcp_oow);
	PRINTF("%lu\tTCP RST add\n", ipsp->iss_tcp_rstadd);
	PRINTF("%lu\tTCP too small\n", ipsp->iss_tcp_toosmall);
	PRINTF("%lu\tTCP bad options\n", ipsp->iss_tcp_badopt);
	PRINTF("%lu\tTCP removed\n", ipsp->iss_fin);
	PRINTF("%lu\tTCP FSM\n", ipsp->iss_tcp_fsm);
	PRINTF("%lu\tTCP strict\n", ipsp->iss_tcp_strict);
	PRINTF("%lu\tTCP wild\n", ipsp->iss_wild);
	PRINTF("%lu\tMicrosoft Windows SACK\n", ipsp->iss_winsack);

	PRINTF("State logging %sabled\n", state_logging ? "en" : "dis");

	PRINTF("IP states added:\n");
	for (i = 0; i < 256; i++) {
		if (ipsp->iss_proto[i] != 0) {
			struct protoent *proto;

			proto = getprotobynumber(i);
			PRINTF("%lu", ipsp->iss_proto[i]);
			if (proto != NULL)
				PRINTF("\t%s\n", proto->p_name);
			else
				PRINTF("\t%d\n", i);
		}
	}

	PRINTF("\nState table bucket statistics:\n");
	PRINTF("%u\tin use\n", ipsp->iss_inuse);

	minlen = ipsp->iss_max;
	totallen = 0;
	maxlen = 0;

	for (i = 0; i < ipsp->iss_state_size; i++) {
		if (buckets[i] > maxlen)
			maxlen = buckets[i];
		if (buckets[i] < minlen)
			minlen = buckets[i];
		totallen += buckets[i];
	}

	PRINTF("%d\thash efficiency\n",
		totallen ? ipsp->iss_inuse * 100 / totallen : 0);
	PRINTF("%2.2f%%\tbucket usage\n%u\tminimal length\n",
		((float)ipsp->iss_inuse / ipsp->iss_state_size) * 100.0,
		minlen);
	PRINTF("%u\tmaximal length\n%.3f\taverage length\n",
		maxlen,
		ipsp->iss_inuse ? (float) totallen/ ipsp->iss_inuse :
				  0.0);

#define ENTRIES_PER_LINE 5

	if (opts & OPT_VERBOSE) {
		PRINTF("\nCurrent bucket sizes :\n");
		for (i = 0; i < ipsp->iss_state_size; i++) {
			if ((i % ENTRIES_PER_LINE) == 0)
				PRINTF("\t");
			PRINTF("%4d -> %4u", i, buckets[i]);
			if ((i % ENTRIES_PER_LINE) ==
			    (ENTRIES_PER_LINE - 1))
				PRINTF("\n");
			else
				PRINTF("  ");
		}
		PRINTF("\n");
	}
	PRINTF("\n");

	free(buckets);

	if (live_kernel == 1) {
		showtqtable_live(state_fd);
	} else {
		printtqtable(ipsp->iss_tcptab);
	}
}


#ifdef STATETOP
static int handle_resize = 0, handle_break = 0;

static void topipstates(saddr, daddr, sport, dport, protocol, ver,
		        refreshtime, topclosed, filter)
	i6addr_t saddr;
	i6addr_t daddr;
	int sport;
	int dport;
	int protocol;
	int ver;
	int refreshtime;
	int topclosed;
	int *filter;
{
	char str1[STSTRSIZE], str2[STSTRSIZE], str3[STSTRSIZE], str4[STSTRSIZE];
	int maxtsentries = 0, reverse = 0, sorting = STSORT_DEFAULT;
	int i, j, winy, tsentry, maxx, maxy, redraw = 0, ret = 0;
	int len, srclen, dstlen, forward = 1, c = 0;
	ips_stat_t ipsst, *ipsstp = &ipsst;
	int token_type = IPFGENITER_STATE;
	statetop_t *tstable = NULL, *tp;
	const char *errstr = "";
	ipstate_t ips;
	ipfobj_t ipfo;
	struct timeval selecttimeout;
	char hostnm[HOSTNMLEN];
	struct protoent *proto;
	fd_set readfd;
	time_t t;

	/* install signal handlers */
	signal(SIGINT, sig_break);
	signal(SIGQUIT, sig_break);
	signal(SIGTERM, sig_break);
	signal(SIGWINCH, sig_resize);

	/* init ncurses stuff */
  	initscr();
  	cbreak();
  	noecho();
	curs_set(0);
	timeout(0);
	getmaxyx(stdscr, maxy, maxx);

	/* init hostname */
	gethostname(hostnm, sizeof(hostnm) - 1);
	hostnm[sizeof(hostnm) - 1] = '\0';

	/* init ipfobj_t stuff */
	bzero((caddr_t)&ipfo, sizeof(ipfo));
	ipfo.ipfo_rev = IPFILTER_VERSION;
	ipfo.ipfo_type = IPFOBJ_STATESTAT;
	ipfo.ipfo_size = sizeof(*ipsstp);
	ipfo.ipfo_ptr = (void *)ipsstp;

	/* repeat until user aborts */
	while ( 1 ) {

		/* get state table */
		bzero((char *)&ipsst, sizeof(ipsst));
		if ((ioctl(state_fd, SIOCGETFS, &ipfo) == -1)) {
			errstr = "ioctl(SIOCGETFS)";
			ret = -1;
			goto out;
		}

		/* clear the history */
		tsentry = -1;

		/* reset max str len */
		srclen = dstlen = 0;

		/* read the state table and store in tstable */
		for (; ipsstp->iss_list; ipsstp->iss_list = ips.is_next) {

			ipsstp->iss_list = fetchstate(ipsstp->iss_list, &ips);
			if (ipsstp->iss_list == NULL)
				break;

			if (ips.is_v != ver)
				continue;

			if ((filter != NULL) &&
			    (state_matcharray(&ips, filter) == 0))
				continue;

			/* check v4 src/dest addresses */
			if (ips.is_v == 4) {
				if ((saddr.in4.s_addr != INADDR_ANY &&
				     saddr.in4.s_addr != ips.is_saddr) ||
				    (daddr.in4.s_addr != INADDR_ANY &&
				     daddr.in4.s_addr != ips.is_daddr))
					continue;
			}
#ifdef	USE_INET6
			/* check v6 src/dest addresses */
			if (ips.is_v == 6) {
				if ((IP6_NEQ(&saddr, &in6addr_any) &&
				     IP6_NEQ(&saddr, &ips.is_src)) ||
				    (IP6_NEQ(&daddr, &in6addr_any) &&
				     IP6_NEQ(&daddr, &ips.is_dst)))
					continue;
			}
#endif
			/* check protocol */
			if (protocol > 0 && protocol != ips.is_p)
				continue;

			/* check ports if protocol is TCP or UDP */
			if (((ips.is_p == IPPROTO_TCP) ||
			     (ips.is_p == IPPROTO_UDP)) &&
			   (((sport > 0) && (htons(sport) != ips.is_sport)) ||
			    ((dport > 0) && (htons(dport) != ips.is_dport))))
				continue;

			/* show closed TCP sessions ? */
			if ((topclosed == 0) && (ips.is_p == IPPROTO_TCP) &&
			    (ips.is_state[0] >= IPF_TCPS_LAST_ACK) &&
			    (ips.is_state[1] >= IPF_TCPS_LAST_ACK))
				continue;

			/*
			 * if necessary make room for this state
			 * entry
			 */
			tsentry++;
			if (!maxtsentries || tsentry == maxtsentries) {
				maxtsentries += STGROWSIZE;
				tstable = reallocarray(tstable, maxtsentries,
				    sizeof(statetop_t));
				if (tstable == NULL) {
					perror("realloc");
					exit(-1);
				}
			}

			/* get max src/dest address string length */
			len = strlen(getip(ips.is_v, &ips.is_src));
			if (srclen < len)
				srclen = len;
			len = strlen(getip(ips.is_v, &ips.is_dst));
			if (dstlen < len)
				dstlen = len;

			/* fill structure */
			tp = tstable + tsentry;
			tp->st_src = ips.is_src;
			tp->st_dst = ips.is_dst;
			tp->st_p = ips.is_p;
			tp->st_v = ips.is_v;
			tp->st_state[0] = ips.is_state[0];
			tp->st_state[1] = ips.is_state[1];
			if (forward) {
				tp->st_pkts = ips.is_pkts[0]+ips.is_pkts[1];
				tp->st_bytes = ips.is_bytes[0]+ips.is_bytes[1];
			} else {
				tp->st_pkts = ips.is_pkts[2]+ips.is_pkts[3];
				tp->st_bytes = ips.is_bytes[2]+ips.is_bytes[3];
			}
			tp->st_age = ips.is_die - ipsstp->iss_ticks;
			if ((ips.is_p == IPPROTO_TCP) ||
			    (ips.is_p == IPPROTO_UDP)) {
				tp->st_sport = ips.is_sport;
				tp->st_dport = ips.is_dport;
			}
		}

		(void) ioctl(state_fd, SIOCIPFDELTOK, &token_type);

		/* sort the array */
		if (tsentry != -1) {
			switch (sorting)
			{
			case STSORT_PR:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_p);
				break;
			case STSORT_PKTS:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_pkts);
				break;
			case STSORT_BYTES:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_bytes);
				break;
			case STSORT_TTL:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_ttl);
				break;
			case STSORT_SRCIP:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_srcip);
				break;
			case STSORT_SRCPT:
				qsort(tstable, tsentry +1,
					sizeof(statetop_t), sort_srcpt);
				break;
			case STSORT_DSTIP:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_dstip);
				break;
			case STSORT_DSTPT:
				qsort(tstable, tsentry + 1,
				      sizeof(statetop_t), sort_dstpt);
				break;
			default:
				break;
			}
		}

		/* handle window resizes */
		if (handle_resize) {
			endwin();
			initscr();
			cbreak();
			noecho();
			curs_set(0);
			timeout(0);
			getmaxyx(stdscr, maxy, maxx);
			redraw = 1;
			handle_resize = 0;
                }

		/* stop program? */
		if (handle_break)
			break;

		/* print title */
		erase();
		attron(A_BOLD);
		winy = 0;
		move(winy,0);
		sprintf(str1, "%s - %s - state top", hostnm, IPL_VERSION);
		for (j = 0 ; j < (maxx - 8 - strlen(str1)) / 2; j++)
			printw(" ");
		printw("%s", str1);
		attroff(A_BOLD);

		/* just for fun add a clock */
		move(winy, maxx - 8);
		t = time(NULL);
		strftime(str1, 80, "%T", localtime(&t));
		printw("%s\n", str1);

		/*
		 * print the display filters, this is placed in the loop,
		 * because someday I might add code for changing these
		 * while the programming is running :-)
		 */
		if (sport >= 0)
			sprintf(str1, "%s,%d", getip(ver, &saddr), sport);
		else
			sprintf(str1, "%s", getip(ver, &saddr));

		if (dport >= 0)
			sprintf(str2, "%s,%d", getip(ver, &daddr), dport);
		else
			sprintf(str2, "%s", getip(ver, &daddr));

		if (protocol < 0)
			strcpy(str3, "any");
		else if ((proto = getprotobynumber(protocol)) != NULL)
			sprintf(str3, "%s", proto->p_name);
		else
			sprintf(str3, "%d", protocol);

		switch (sorting)
		{
		case STSORT_PR:
			sprintf(str4, "proto");
			break;
		case STSORT_PKTS:
			sprintf(str4, "# pkts");
			break;
		case STSORT_BYTES:
			sprintf(str4, "# bytes");
			break;
		case STSORT_TTL:
			sprintf(str4, "ttl");
			break;
		case STSORT_SRCIP:
			sprintf(str4, "src ip");
			break;
		case STSORT_SRCPT:
			sprintf(str4, "src port");
			break;
		case STSORT_DSTIP:
			sprintf(str4, "dest ip");
			break;
		case STSORT_DSTPT:
			sprintf(str4, "dest port");
			break;
		default:
			sprintf(str4, "unknown");
			break;
		}

		if (reverse)
			strcat(str4, " (reverse)");

		winy += 2;
		move(winy,0);
		printw("Src: %s, Dest: %s, Proto: %s, Sorted by: %s\n\n",
		       str1, str2, str3, str4);

		/*
		 * For an IPv4 IP address we need at most 15 characters,
		 * 4 tuples of 3 digits, separated by 3 dots. Enforce this
		 * length, so the colums do not change positions based
		 * on the size of the IP address. This length makes the
		 * output fit in a 80 column terminal.
		 * We are lacking a good solution for IPv6 addresses (that
		 * can be longer that 15 characters), so we do not enforce
		 * a maximum on the IP field size.
		 */
		if (srclen < 15)
			srclen = 15;
		if (dstlen < 15)
			dstlen = 15;

		/* print column description */
		winy += 2;
		move(winy,0);
		attron(A_BOLD);
		printw("%-*s %-*s %3s %4s %7s %9s %9s\n",
		       srclen + 6, "Source IP", dstlen + 6, "Destination IP",
		       "ST", "PR", "#pkts", "#bytes", "ttl");
		attroff(A_BOLD);

		/* print all the entries */
		tp = tstable;
		if (reverse)
			tp += tsentry;

		if (tsentry > maxy - 6)
			tsentry = maxy - 6;
		for (i = 0; i <= tsentry; i++) {
			/* print src/dest and port */
			if ((tp->st_p == IPPROTO_TCP) ||
			    (tp->st_p == IPPROTO_UDP)) {
				sprintf(str1, "%s,%hu",
					getip(tp->st_v, &tp->st_src),
					ntohs(tp->st_sport));
				sprintf(str2, "%s,%hu",
					getip(tp->st_v, &tp->st_dst),
					ntohs(tp->st_dport));
			} else {
				sprintf(str1, "%s", getip(tp->st_v,
				    &tp->st_src));
				sprintf(str2, "%s", getip(tp->st_v,
				    &tp->st_dst));
			}
			winy++;
			move(winy, 0);
			printw("%-*s %-*s", srclen + 6, str1, dstlen + 6, str2);

			/* print state */
			sprintf(str1, "%X/%X", tp->st_state[0],
				tp->st_state[1]);
			printw(" %3s", str1);

			/* print protocol */
			proto = getprotobynumber(tp->st_p);
			if (proto) {
				strncpy(str1, proto->p_name, 4);
				str1[4] = '\0';
			} else {
				sprintf(str1, "%d", tp->st_p);
			}
			/* just print icmp for IPv6-ICMP */
			if (tp->st_p == IPPROTO_ICMPV6)
				strcpy(str1, "icmp");
			printw(" %4s", str1);

			/* print #pkt/#bytes */
#ifdef	USE_QUAD_T
			printw(" %7qu %9qu", (unsigned long long) tp->st_pkts,
				(unsigned long long) tp->st_bytes);
#else
			printw(" %7lu %9lu", tp->st_pkts, tp->st_bytes);
#endif
			printw(" %9s", ttl_to_string(tp->st_age));

			if (reverse)
				tp--;
			else
				tp++;
		}

		/* screen data structure is filled, now update the screen */
		if (redraw)
			clearok(stdscr,1);

		if (refresh() == ERR)
			break;
		if (redraw) {
			clearok(stdscr,0);
			redraw = 0;
		}

		/* wait for key press or a 1 second time out period */
		selecttimeout.tv_sec = refreshtime;
		selecttimeout.tv_usec = 0;
		FD_ZERO(&readfd);
		FD_SET(0, &readfd);
		select(1, &readfd, NULL, NULL, &selecttimeout);

		/* if key pressed, read all waiting keys */
		if (FD_ISSET(0, &readfd)) {
			c = wgetch(stdscr);
			if (c == ERR)
				continue;

			if (ISALPHA(c) && ISUPPER(c))
				c = TOLOWER(c);
			if (c == 'l') {
				redraw = 1;
			} else if (c == 'q') {
				break;
			} else if (c == 'r') {
				reverse = !reverse;
			} else if (c == 'b') {
				forward = 0;
			} else if (c == 'f') {
				forward = 1;
			} else if (c == 's') {
				if (++sorting > STSORT_MAX)
					sorting = 0;
			}
		}
	} /* while */

out:
	printw("\n");
	curs_set(1);
	/* nocbreak(); XXX - endwin() should make this redundant */
	endwin();

	free(tstable);
	if (ret != 0)
		perror(errstr);
}
#endif


/*
 * Show fragment cache information that's held in the kernel.
 */
static void showfrstates(ifsp, ticks)
	ipfrstat_t *ifsp;
	u_long ticks;
{
	struct ipfr *ipfrtab[IPFT_SIZE], ifr;
	int i;

	/*
	 * print out the numeric statistics
	 */
	PRINTF("IP fragment states:\n%lu\tnew\n%lu\texpired\n%lu\thits\n",
		ifsp->ifs_new, ifsp->ifs_expire, ifsp->ifs_hits);
	PRINTF("%lu\tretrans\n%lu\ttoo short\n",
		ifsp->ifs_retrans0, ifsp->ifs_short);
	PRINTF("%lu\tno memory\n%lu\talready exist\n",
		ifsp->ifs_nomem, ifsp->ifs_exists);
	PRINTF("%lu\tinuse\n", ifsp->ifs_inuse);
	PRINTF("\n");

	if (live_kernel == 0) {
		if (kmemcpy((char *)ipfrtab, (u_long)ifsp->ifs_table,
			    sizeof(ipfrtab)))
			return;
	}

	/*
	 * Print out the contents (if any) of the fragment cache table.
	 */
	if (live_kernel == 1) {
		do {
			if (fetchfrag(ipf_fd, IPFGENITER_FRAG, &ifr) != 0)
				break;
			if (ifr.ipfr_ifp == NULL)
				break;
			ifr.ipfr_ttl -= ticks;
			printfraginfo("", &ifr);
		} while (ifr.ipfr_next != NULL);
	} else {
		for (i = 0; i < IPFT_SIZE; i++)
			while (ipfrtab[i] != NULL) {
				if (kmemcpy((char *)&ifr, (u_long)ipfrtab[i],
					    sizeof(ifr)) == -1)
					break;
				printfraginfo("", &ifr);
				ipfrtab[i] = ifr.ipfr_next;
			}
	}
	/*
	 * Print out the contents (if any) of the NAT fragment cache table.
	 */

	if (live_kernel == 0) {
		if (kmemcpy((char *)ipfrtab, (u_long)ifsp->ifs_nattab,
			    sizeof(ipfrtab)))
			return;
	}

	if (live_kernel == 1) {
		do {
			if (fetchfrag(nat_fd, IPFGENITER_NATFRAG, &ifr) != 0)
				break;
			if (ifr.ipfr_ifp == NULL)
				break;
			ifr.ipfr_ttl -= ticks;
			printfraginfo("NAT: ", &ifr);
		} while (ifr.ipfr_next != NULL);
	} else {
		for (i = 0; i < IPFT_SIZE; i++)
			while (ipfrtab[i] != NULL) {
				if (kmemcpy((char *)&ifr, (u_long)ipfrtab[i],
					    sizeof(ifr)) == -1)
					break;
				printfraginfo("NAT: ", &ifr);
				ipfrtab[i] = ifr.ipfr_next;
			}
	}
}


/*
 * Show stats on how auth within IPFilter has been used
 */
static void showauthstates(asp)
	ipf_authstat_t *asp;
{
	frauthent_t *frap, fra;
	ipfgeniter_t auth;
	ipfobj_t obj;

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GENITER;
	obj.ipfo_size = sizeof(auth);
	obj.ipfo_ptr = &auth;

	auth.igi_type = IPFGENITER_AUTH;
	auth.igi_nitems = 1;
	auth.igi_data = &fra;

#ifdef	USE_QUAD_T
	printf("Authorisation hits: %"PRIu64"\tmisses %"PRIu64"\n",
		(unsigned long long) asp->fas_hits,
		(unsigned long long) asp->fas_miss);
#else
	printf("Authorisation hits: %ld\tmisses %ld\n", asp->fas_hits,
		asp->fas_miss);
#endif
	printf("nospace %ld\nadded %ld\nsendfail %ld\nsendok %ld\n",
		asp->fas_nospace, asp->fas_added, asp->fas_sendfail,
		asp->fas_sendok);
	printf("queok %ld\nquefail %ld\nexpire %ld\n",
		asp->fas_queok, asp->fas_quefail, asp->fas_expire);

	frap = asp->fas_faelist;
	while (frap) {
		if (live_kernel == 1) {
			if (ioctl(auth_fd, SIOCGENITER, &obj))
				break;
		} else {
			if (kmemcpy((char *)&fra, (u_long)frap,
				    sizeof(fra)) == -1)
				break;
		}
		printf("age %ld\t", fra.fae_age);
		printfr(&fra.fae_fr, ioctl);
		frap = fra.fae_next;
	}
}


/*
 * Display groups used for each of filter rules, accounting rules and
 * authentication, separately.
 */
static void showgroups(fiop)
	struct friostat	*fiop;
{
	static char *gnames[3] = { "Filter", "Accounting", "Authentication" };
	static int gnums[3] = { IPL_LOGIPF, IPL_LOGCOUNT, IPL_LOGAUTH };
	frgroup_t *fp, grp;
	int on, off, i;

	on = fiop->f_active;
	off = 1 - on;

	for (i = 0; i < 3; i++) {
		printf("%s groups (active):\n", gnames[i]);
		for (fp = fiop->f_groups[gnums[i]][on]; fp != NULL;
		     fp = grp.fg_next)
			if (kmemcpy((char *)&grp, (u_long)fp, sizeof(grp)))
				break;
			else
				printf("%s\n", grp.fg_name);
		printf("%s groups (inactive):\n", gnames[i]);
		for (fp = fiop->f_groups[gnums[i]][off]; fp != NULL;
		     fp = grp.fg_next)
			if (kmemcpy((char *)&grp, (u_long)fp, sizeof(grp)))
				break;
			else
				printf("%s\n", grp.fg_name);
	}
}


static void parse_ipportstr(argument, ip, port)
	const char *argument;
	i6addr_t *ip;
	int *port;
{
	char *s, *comma;
	int ok = 0;

	/* make working copy of argument, Theoretically you must be able
	 * to write to optarg, but that seems very ugly to me....
	 */
	s = strdup(argument);
	if (s == NULL)
		return;

	/* get port */
	if ((comma = strchr(s, ',')) != NULL) {
		if (!strcasecmp(comma + 1, "any")) {
			*port = -1;
		} else if (!sscanf(comma + 1, "%d", port) ||
			   (*port < 0) || (*port > 65535)) {
			fprintf(stderr, "Invalid port specification in %s\n",
				argument);
			free(s);
			exit(-2);
		}
		*comma = '\0';
	}


	/* get ip address */
	if (!strcasecmp(s, "any")) {
		ip->in4.s_addr = INADDR_ANY;
		ok = 1;
#ifdef	USE_INET6
		ip->in6 = in6addr_any;
	} else if (use_inet6 && inet_pton(AF_INET6, s, &ip->in6)) {
		ok = 1;
#endif
	} else if (inet_aton(s, &ip->in4))
		ok = 1;

	if (ok == 0) {
		fprintf(stderr, "Invalid IP address: %s\n", s);
		free(s);
		exit(-2);
	}

	/* free allocated memory */
	free(s);
}


#ifdef STATETOP
static void sig_resize(s)
	int s;
{
	handle_resize = 1;
}

static void sig_break(s)
	int s;
{
	handle_break = 1;
}

static char *getip(v, addr)
	int v;
	i6addr_t *addr;
{
#ifdef  USE_INET6
	static char hostbuf[MAXHOSTNAMELEN+1];
#endif

	if (v == 4)
		return inet_ntoa(addr->in4);

#ifdef  USE_INET6
	(void) inet_ntop(AF_INET6, &addr->in6, hostbuf, sizeof(hostbuf) - 1);
	hostbuf[MAXHOSTNAMELEN] = '\0';
	return hostbuf;
#else
	return "IPv6";
#endif
}


static char *ttl_to_string(ttl)
	long int ttl;
{
	static char ttlbuf[STSTRSIZE];
	int hours, minutes, seconds;

	/* ttl is in half seconds */
	ttl /= 2;

	hours = ttl / 3600;
	ttl = ttl % 3600;
	minutes = ttl / 60;
	seconds = ttl % 60;

	if (hours > 0)
		sprintf(ttlbuf, "%2d:%02d:%02d", hours, minutes, seconds);
	else
		sprintf(ttlbuf, "%2d:%02d", minutes, seconds);
	return ttlbuf;
}


static int sort_pkts(a, b)
	const void *a;
	const void *b;
{

	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (ap->st_pkts == bp->st_pkts)
		return 0;
	else if (ap->st_pkts < bp->st_pkts)
		return 1;
	return -1;
}


static int sort_bytes(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (ap->st_bytes == bp->st_bytes)
		return 0;
	else if (ap->st_bytes < bp->st_bytes)
		return 1;
	return -1;
}


static int sort_p(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (ap->st_p == bp->st_p)
		return 0;
	else if (ap->st_p < bp->st_p)
		return 1;
	return -1;
}


static int sort_ttl(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (ap->st_age == bp->st_age)
		return 0;
	else if (ap->st_age < bp->st_age)
		return 1;
	return -1;
}

static int sort_srcip(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

#ifdef USE_INET6
	if (use_inet6) {
		if (IP6_EQ(&ap->st_src, &bp->st_src))
			return 0;
		else if (IP6_GT(&ap->st_src, &bp->st_src))
			return 1;
	} else
#endif
	{
		if (ntohl(ap->st_src.in4.s_addr) ==
		    ntohl(bp->st_src.in4.s_addr))
			return 0;
		else if (ntohl(ap->st_src.in4.s_addr) >
		         ntohl(bp->st_src.in4.s_addr))
			return 1;
	}
	return -1;
}

static int sort_srcpt(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (htons(ap->st_sport) == htons(bp->st_sport))
		return 0;
	else if (htons(ap->st_sport) > htons(bp->st_sport))
		return 1;
	return -1;
}

static int sort_dstip(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

#ifdef USE_INET6
	if (use_inet6) {
		if (IP6_EQ(&ap->st_dst, &bp->st_dst))
			return 0;
		else if (IP6_GT(&ap->st_dst, &bp->st_dst))
			return 1;
	} else
#endif
	{
		if (ntohl(ap->st_dst.in4.s_addr) ==
		    ntohl(bp->st_dst.in4.s_addr))
			return 0;
		else if (ntohl(ap->st_dst.in4.s_addr) >
		         ntohl(bp->st_dst.in4.s_addr))
			return 1;
	}
	return -1;
}

static int sort_dstpt(a, b)
	const void *a;
	const void *b;
{
	register const statetop_t *ap = a;
	register const statetop_t *bp = b;

	if (htons(ap->st_dport) == htons(bp->st_dport))
		return 0;
	else if (htons(ap->st_dport) > htons(bp->st_dport))
		return 1;
	return -1;
}

#endif


ipstate_t *fetchstate(src, dst)
	ipstate_t *src, *dst;
{

	if (live_kernel == 1) {
		ipfgeniter_t state;
		ipfobj_t obj;

		obj.ipfo_rev = IPFILTER_VERSION;
		obj.ipfo_type = IPFOBJ_GENITER;
		obj.ipfo_size = sizeof(state);
		obj.ipfo_ptr = &state;

		state.igi_type = IPFGENITER_STATE;
		state.igi_nitems = 1;
		state.igi_data = dst;

		if (ioctl(state_fd, SIOCGENITER, &obj) != 0)
			return NULL;
		if (dst->is_next == NULL) {
			int n = IPFGENITER_STATE;
			(void) ioctl(ipf_fd,SIOCIPFDELTOK, &n);
		}
	} else {
		if (kmemcpy((char *)dst, (u_long)src, sizeof(*dst)))
			return NULL;
	}
	return dst;
}


static int fetchfrag(fd, type, frp)
	int fd, type;
	ipfr_t *frp;
{
	ipfgeniter_t frag;
	ipfobj_t obj;

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GENITER;
	obj.ipfo_size = sizeof(frag);
	obj.ipfo_ptr = &frag;

	frag.igi_type = type;
	frag.igi_nitems = 1;
	frag.igi_data = frp;

	if (ioctl(fd, SIOCGENITER, &obj))
		return EFAULT;
	return 0;
}


static int state_matcharray(stp, array)
	ipstate_t *stp;
	int *array;
{
	int i, n, *x, rv, p;
	ipfexp_t *e;

	rv = 0;

	for (n = array[0], x = array + 1; n > 0; x += e->ipfe_size) {
		e = (ipfexp_t *)x;
		if (e->ipfe_cmd == IPF_EXP_END)
			break;
		n -= e->ipfe_size;

		rv = 0;
		/*
		 * The upper 16 bits currently store the protocol value.
		 * This is currently used with TCP and UDP port compares and
		 * allows "tcp.port = 80" without requiring an explicit
		 " "ip.pr = tcp" first.
		 */
		p = e->ipfe_cmd >> 16;
		if ((p != 0) && (p != stp->is_p))
			break;

		switch (e->ipfe_cmd)
		{
		case IPF_EXP_IP_PR :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_p == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_IP_SRCADDR :
			if (stp->is_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((stp->is_saddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_DSTADDR :
			if (stp->is_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((stp->is_daddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_ADDR :
			if (stp->is_v != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((stp->is_saddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				      ((stp->is_daddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

#ifdef USE_INET6
		case IPF_EXP_IP6_SRCADDR :
			if (stp->is_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&stp->is_src,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_DSTADDR :
			if (stp->is_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&stp->is_dst,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_ADDR :
			if (stp->is_v != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&stp->is_src,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&stp->is_dst,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;
#endif

		case IPF_EXP_UDP_PORT :
		case IPF_EXP_TCP_PORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_sport == e->ipfe_arg0[i]) ||
				      (stp->is_dport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_SPORT :
		case IPF_EXP_TCP_SPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_sport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_DPORT :
		case IPF_EXP_TCP_DPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_dport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_IDLE_GT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_die < e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_TCP_STATE :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (stp->is_state[0] == e->ipfe_arg0[i]) ||
				      (stp->is_state[1] == e->ipfe_arg0[i]);
			}
			break;
		}
		rv ^= e->ipfe_not;

		if (rv == 0)
			break;
	}

	return rv;
}


static void showtqtable_live(fd)
	int fd;
{
	ipftq_t table[IPF_TCP_NSTATES];
	ipfobj_t obj;

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof(table);
	obj.ipfo_ptr = (void *)table;
	obj.ipfo_type = IPFOBJ_STATETQTAB;

	if (ioctl(fd, SIOCGTQTAB, &obj) == 0) {
		printtqtable(table);
	}
}
