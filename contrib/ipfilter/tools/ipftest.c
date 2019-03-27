/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#include "ipf.h"
#include "ipt.h"
#include <sys/ioctl.h>
#include <sys/file.h>

#if !defined(lint)
static const char sccsid[] = "@(#)ipt.c	1.19 6/3/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

extern	char	*optarg;
extern	struct ipread	pcap, iptext, iphex;
extern	struct ifnet	*get_unit __P((char *, int));
extern	void	init_ifp __P((void));
extern	ipnat_t	*natparse __P((char *, int));
extern	hostmap_t **ipf_hm_maptable;
extern	hostmap_t *ipf_hm_maplist;

ipfmutex_t	ipl_mutex, ipf_auth_mx, ipf_rw, ipf_stinsert;
ipfmutex_t	ipf_nat_new, ipf_natio, ipf_timeoutlock;
ipfrwlock_t	ipf_mutex, ipf_global, ipf_ipidfrag, ip_poolrw, ipf_frcache;
ipfrwlock_t	ipf_frag, ipf_state, ipf_nat, ipf_natfrag, ipf_authlk;
ipfrwlock_t	ipf_tokens;
int	opts = OPT_DONTOPEN;
int	use_inet6 = 0;
int	docksum = 0;
int	pfil_delayed_copy = 0;
int	main __P((int, char *[]));
int	loadrules __P((char *, int));
int	kmemcpy __P((char *, long, int));
int     kstrncpy __P((char *, long, int n));
int	blockreason;
void	dumpnat __P((void *));
void	dumpgroups __P((ipf_main_softc_t *));
void	dumprules __P((frentry_t *));
void	drain_log __P((char *));
void	fixv4sums __P((mb_t *, ip_t *));

int ipftestioctl __P((int, ioctlcmd_t, ...));
int ipnattestioctl __P((int, ioctlcmd_t, ...));
int ipstatetestioctl __P((int, ioctlcmd_t, ...));
int ipauthtestioctl __P((int, ioctlcmd_t, ...));
int ipscantestioctl __P((int, ioctlcmd_t, ...));
int ipsynctestioctl __P((int, ioctlcmd_t, ...));
int ipooltestioctl __P((int, ioctlcmd_t, ...));

static	ioctlfunc_t	iocfunctions[IPL_LOGSIZE] = { ipftestioctl,
						      ipnattestioctl,
						      ipstatetestioctl,
						      ipauthtestioctl,
						      ipsynctestioctl,
						      ipscantestioctl,
						      ipooltestioctl,
						      NULL };
static	ipf_main_softc_t	*softc = NULL;


int
main(argc,argv)
	int argc;
	char *argv[];
{
	char	*datain, *iface, *ifname, *logout;
	int	fd, i, dir, c, loaded, dump, hlen;
	struct	in_addr	sip;
	struct	ifnet	*ifp;
	struct	ipread	*r;
	mb_t	mb, *m, *n;
	ip_t	*ip;

	m = &mb;
	dir = 0;
	dump = 0;
	hlen = 0;
	loaded = 0;
	r = &iptext;
	iface = NULL;
	logout = NULL;
	datain = NULL;
	sip.s_addr = 0;
	ifname = "anon0";

	initparse();

	ipf_load_all();

	softc = ipf_create_all(NULL);
	if (softc == NULL)
		exit(1);

	if (ipf_init_all(softc) == -1)
		exit(1);

	i = 1;
	if (ipftestioctl(IPL_LOGIPF, SIOCFRENB, &i) != 0)
		exit(1);

	while ((c = getopt(argc, argv, "6bCdDF:i:I:l:N:P:or:RS:T:vxX")) != -1)
		switch (c)
		{
		case '6' :
#ifdef	USE_INET6
			use_inet6 = 1;
#else
			fprintf(stderr, "IPv6 not supported\n");
			exit(1);
#endif
			break;
		case 'b' :
			opts |= OPT_BRIEF;
			break;
		case 'd' :
			opts |= OPT_DEBUG;
			break;
		case 'C' :
			docksum = 1;
			break;
		case 'D' :
			dump = 1;
			break;
		case 'F' :
			if (strcasecmp(optarg, "pcap") == 0)
				r = &pcap;
			else if (strcasecmp(optarg, "hex") == 0)
				r = &iphex;
			else if (strcasecmp(optarg, "text") == 0)
				r = &iptext;
			break;
		case 'i' :
			datain = optarg;
			break;
		case 'I' :
			ifname = optarg;
			break;
		case 'l' :
			logout = optarg;
			break;
		case 'N' :
			if (ipnat_parsefile(-1, ipnat_addrule, ipnattestioctl,
					    optarg) == -1)
				return -1;
			loaded = 1;
			opts |= OPT_NAT;
			break;
		case 'o' :
			opts |= OPT_SAVEOUT;
			break;
		case 'P' :
			if (ippool_parsefile(-1, optarg, ipooltestioctl) == -1)
				return -1;
			loaded = 1;
			break;
		case 'r' :
			if (ipf_parsefile(-1, ipf_addrule, iocfunctions,
					  optarg) == -1)
				return -1;
			loaded = 1;
			break;
		case 'S' :
			sip.s_addr = inet_addr(optarg);
			break;
		case 'R' :
			opts |= OPT_NORESOLVE;
			break;
		case 'T' :
			ipf_dotuning(-1, optarg, ipftestioctl);
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		case 'x' :
			opts |= OPT_HEX;
			break;
		}

	if (loaded == 0) {
		(void)fprintf(stderr,"no rules loaded\n");
		exit(-1);
	}

	if (opts & OPT_SAVEOUT)
		init_ifp();

	if (datain)
		fd = (*r->r_open)(datain);
	else
		fd = (*r->r_open)("-");

	if (fd < 0) {
		perror("error opening input");
		exit(-1);
	}

	m->m_data = (char *)m->mb_buf;
	while ((i = (*r->r_readip)(m, &iface, &dir)) > 0) {

		if ((iface == NULL) || (*iface == '\0'))
			iface = ifname;

		ip = MTOD(m, ip_t *);
		ifp = get_unit(iface, IP_V(ip));

		if (IP_V(ip) == 4) {
			if ((r->r_flags & R_DO_CKSUM) || docksum)
				fixv4sums(m, ip);
			hlen = IP_HL(ip) << 2;
			if (sip.s_addr)
				dir = !(sip.s_addr == ip->ip_src.s_addr);
		}
#ifdef	USE_INET6
		else
			hlen = sizeof(ip6_t);
#endif
		/* ipfr_slowtimer(); */
		blockreason = 0;
		m = &mb;
		m->mb_ifp = ifp;
		m->mb_len = i;
		i = ipf_check(softc, ip, hlen, ifp, dir, &m);
		if ((opts & OPT_NAT) == 0)
			switch (i)
			{
			case -4 :
				(void)printf("preauth");
				break;
			case -3 :
				(void)printf("account");
				break;
			case -2 :
				(void)printf("auth");
				break;
			case -1 :
				(void)printf("block");
				break;
			case 0 :
				(void)printf("pass");
				break;
			case 1 :
				if (m == NULL)
					(void)printf("bad-packet");
				else
					(void)printf("nomatch");
				break;
			case 3 :
				(void)printf("block return-rst");
				break;
			case 4 :
				(void)printf("block return-icmp");
				break;
			case 5 :
				(void)printf("block return-icmp-as-dest");
				break;
			default :
				(void)printf("recognised return %#x\n", i);
				break;
			}

		if (!(opts & OPT_BRIEF)) {
			putchar(' ');
			if (m != NULL)
				printpacket(dir, m);
			else
				printpacket(dir, &mb);
			printf("--------------");
		} else if ((opts & (OPT_BRIEF|OPT_NAT)) ==
			   (OPT_NAT|OPT_BRIEF)) {
			if (m != NULL)
				printpacket(dir, m);
			else
				PRINTF("%d\n", blockreason);
		}

		ipf_state_flush(softc, 1, 0);

		if (dir && (ifp != NULL) && IP_V(ip) && (m != NULL))
			(*ifp->if_output)(ifp, (void *)m, NULL, 0);

		while ((m != NULL) && (m != &mb)) {
			n = m->mb_next;
			freembt(m);
			m = n;
		}

		if ((opts & (OPT_BRIEF|OPT_NAT)) != (OPT_NAT|OPT_BRIEF))
			putchar('\n');
		dir = 0;
		if (iface != ifname) {
			free(iface);
			iface = ifname;
		}
		m = &mb;
		m->mb_data = (char *)m->mb_buf;
	}

	if (i != 0)
		fprintf(stderr, "readip failed: %d\n", i);
	(*r->r_close)();

	if (logout != NULL) {
		drain_log(logout);
	}

	if (dump == 1)  {
		dumpnat(softc->ipf_nat_soft);
		ipf_state_dump(softc, softc->ipf_state_soft);
		ipf_lookup_dump(softc, softc->ipf_state_soft);
		dumpgroups(softc);
	}

	ipf_fini_all(softc);

	ipf_destroy_all(softc);

	ipf_unload_all();

	ipf_mutex_clean();
	ipf_rwlock_clean();

	if (getenv("FINDLEAKS")) {
		fflush(stdout);
		abort();
	}
	return 0;
}


int ipftestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGIPF, cmd, data, FWRITE|FREAD);
	if (opts & OPT_DEBUG)
		fprintf(stderr, "ipfioctl(IPF,%#x,%p) = %d (%d)\n",
			(u_int)cmd, data, i, softc->ipf_interror);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipnattestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGNAT, cmd, data, FWRITE|FREAD);
	if (opts & OPT_DEBUG)
		fprintf(stderr, "ipfioctl(NAT,%#x,%p) = %d\n",
			(u_int)cmd, data, i);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipstatetestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGSTATE, cmd, data, FWRITE|FREAD);
	if ((opts & OPT_DEBUG) || (i != 0))
		fprintf(stderr, "ipfioctl(STATE,%#x,%p) = %d\n",
			(u_int)cmd, data, i);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipauthtestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGAUTH, cmd, data, FWRITE|FREAD);
	if ((opts & OPT_DEBUG) || (i != 0))
		fprintf(stderr, "ipfioctl(AUTH,%#x,%p) = %d\n",
			(u_int)cmd, data, i);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipscantestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGSCAN, cmd, data, FWRITE|FREAD);
	if ((opts & OPT_DEBUG) || (i != 0))
		fprintf(stderr, "ipfioctl(SCAN,%#x,%p) = %d\n",
			(u_int)cmd, data, i);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipsynctestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGSYNC, cmd, data, FWRITE|FREAD);
	if ((opts & OPT_DEBUG) || (i != 0))
		fprintf(stderr, "ipfioctl(SYNC,%#x,%p) = %d\n",
			(u_int)cmd, data, i);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int ipooltestioctl(int dev, ioctlcmd_t cmd, ...)
{
	caddr_t data;
	va_list ap;
	int i;

	dev = dev;	/* gcc -Wextra */
	va_start(ap, cmd);
	data = va_arg(ap, caddr_t);
	va_end(ap);

	i = ipfioctl(softc, IPL_LOGLOOKUP, cmd, data, FWRITE|FREAD);
	if ((opts & OPT_DEBUG) || (i != 0))
		fprintf(stderr, "ipfioctl(POOL,%#x,%p) = %d (%d)\n",
			(u_int)cmd, data, i, softc->ipf_interror);
	if (i != 0) {
		errno = i;
		return -1;
	}
	return 0;
}


int kmemcpy(addr, offset, size)
	char *addr;
	long offset;
	int size;
{
	bcopy((char *)offset, addr, size);
	return 0;
}


int kstrncpy(buf, pos, n)
	char *buf;
	long pos;
	int n;
{
	char *ptr;

	ptr = (char *)pos;

	while ((n > 0) && (*buf++ = *ptr++))
		;
	return 0;
}


/*
 * Display the built up NAT table rules and mapping entries.
 */
void dumpnat(arg)
	void *arg;
{
	ipf_nat_softc_t *softn = arg;
	hostmap_t *hm;
	ipnat_t	*ipn;
	nat_t *nat;

	printf("List of active MAP/Redirect filters:\n");
	for (ipn = softn->ipf_nat_list; ipn != NULL; ipn = ipn->in_next)
		printnat(ipn, opts & (OPT_DEBUG|OPT_VERBOSE));
	printf("\nList of active sessions:\n");
	for (nat = softn->ipf_nat_instances; nat; nat = nat->nat_next) {
		printactivenat(nat, opts, 0);
		if (nat->nat_aps)
			printf("\tproxy active\n");
	}

	printf("\nHostmap table:\n");
	for (hm = softn->ipf_hm_maplist; hm != NULL; hm = hm->hm_next)
		printhostmap(hm, hm->hm_hv);
}


void dumpgroups(softc)
	ipf_main_softc_t *softc;
{
	frgroup_t *fg;
	int i;

	printf("List of groups configured (set 0)\n");
	for (i = 0; i < IPL_LOGSIZE; i++)
		for (fg =  softc->ipf_groups[i][0]; fg != NULL;
		     fg = fg->fg_next) {
			printf("Dev.%d. Group %s Ref %d Flags %#x\n",
				i, fg->fg_name, fg->fg_ref, fg->fg_flags);
			dumprules(fg->fg_start);
		}

	printf("List of groups configured (set 1)\n");
	for (i = 0; i < IPL_LOGSIZE; i++)
		for (fg =  softc->ipf_groups[i][1]; fg != NULL;
		     fg = fg->fg_next) {
			printf("Dev.%d. Group %s Ref %d Flags %#x\n",
				i, fg->fg_name, fg->fg_ref, fg->fg_flags);
			dumprules(fg->fg_start);
		}

	printf("Rules configured (set 0, in)\n");
	dumprules(softc->ipf_rules[0][0]);
	printf("Rules configured (set 0, out)\n");
	dumprules(softc->ipf_rules[1][0]);
	printf("Rules configured (set 1, in)\n");
	dumprules(softc->ipf_rules[0][1]);
	printf("Rules configured (set 1, out)\n");
	dumprules(softc->ipf_rules[1][1]);

	printf("Accounting rules configured (set 0, in)\n");
	dumprules(softc->ipf_acct[0][0]);
	printf("Accounting rules configured (set 0, out)\n");
	dumprules(softc->ipf_acct[0][1]);
	printf("Accounting rules configured (set 1, in)\n");
	dumprules(softc->ipf_acct[1][0]);
	printf("Accounting rules configured (set 1, out)\n");
	dumprules(softc->ipf_acct[1][1]);
}

void dumprules(rulehead)
	frentry_t *rulehead;
{
	frentry_t *fr;

	for (fr = rulehead; fr != NULL; fr = fr->fr_next) {
#ifdef	USE_QUAD_T
		printf("%"PRIu64" ",(unsigned long long)fr->fr_hits);
#else
		printf("%ld ", fr->fr_hits);
#endif
		printfr(fr, ipftestioctl);
	}
}


void drain_log(filename)
	char *filename;
{
	char buffer[DEFAULT_IPFLOGSIZE];
	struct iovec iov;
	struct uio uio;
	size_t resid;
	int fd, i;

	fd = open(filename, O_CREAT|O_TRUNC|O_WRONLY, 0644);
	if (fd == -1) {
		perror("drain_log:open");
		return;
	}

	for (i = 0; i <= IPL_LOGMAX; i++)
		while (1) {
			bzero((char *)&iov, sizeof(iov));
			iov.iov_base = buffer;
			iov.iov_len = sizeof(buffer);

			bzero((char *)&uio, sizeof(uio));
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_resid = iov.iov_len;
			resid = uio.uio_resid;

			if (ipf_log_read(softc, i, &uio) == 0) {
				/*
				 * If nothing was read then break out.
				 */
				if (uio.uio_resid == resid)
					break;
				write(fd, buffer, resid - uio.uio_resid);
			} else
				break;
	}

	close(fd);
}


void fixv4sums(m, ip)
	mb_t *m;
	ip_t *ip;
{
	u_char *csump, *hdr, p;
	fr_info_t tmp;
	int len;

	p = 0;
	len = 0;
	bzero((char *)&tmp, sizeof(tmp));

	csump = (u_char *)ip;
	if (IP_V(ip) == 4) {
		ip->ip_sum = 0;
		ip->ip_sum = ipf_cksum((u_short *)ip, IP_HL(ip) << 2);
		tmp.fin_hlen = IP_HL(ip) << 2;
		csump += IP_HL(ip) << 2;
		p = ip->ip_p;
		len = ntohs(ip->ip_len);
#ifdef USE_INET6
	} else if (IP_V(ip) == 6) {
		tmp.fin_hlen = sizeof(ip6_t);
		csump += sizeof(ip6_t);
		p = ((ip6_t *)ip)->ip6_nxt;
		len = ntohs(((ip6_t *)ip)->ip6_plen);
		len += sizeof(ip6_t);
#endif
	}
	tmp.fin_plen = len;
	tmp.fin_dlen = len - tmp.fin_hlen;

	switch (p)
	{
	case IPPROTO_TCP :
		hdr = csump;
		csump += offsetof(tcphdr_t, th_sum);
		break;
	case IPPROTO_UDP :
		hdr = csump;
		csump += offsetof(udphdr_t, uh_sum);
		break;
	case IPPROTO_ICMP :
		hdr = csump;
		csump += offsetof(icmphdr_t, icmp_cksum);
		break;
	default :
		csump = NULL;
		hdr = NULL;
		break;
	}
	if (hdr != NULL) {
		tmp.fin_m = m;
		tmp.fin_mp = &m;
		tmp.fin_dp = hdr;
		tmp.fin_ip = ip;
		tmp.fin_plen = len;
		*csump = 0;
		*(u_short *)csump = fr_cksum(&tmp, ip, p, hdr);
	}
}

void
ip_fillid(struct ip *ip)
{
	static uint16_t ip_id;

	ip->ip_id = ip_id++;
}
