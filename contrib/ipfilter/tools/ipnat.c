/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Added redirect stuff and a variety of bug fixes. (mcn@EnGarde.com)
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#if !defined(__SVR4)
#include <strings.h>
#else
#include <sys/byteorder.h>
#endif
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/file.h>
#define _KERNEL
#include <sys/uio.h>
#undef _KERNEL
#include <sys/socket.h>
#include <sys/ioctl.h>
#if defined(sun) && defined(__SVR4)
# include <sys/ioccom.h>
# include <sys/sysmacros.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <ctype.h>
# include <nlist.h>
#include "ipf.h"
#include "netinet/ipl.h"
#include "kmem.h"


# define	STRERROR(x)	strerror(x)

#if !defined(lint)
static const char sccsid[] ="@(#)ipnat.c	1.9 6/5/96 (C) 1993 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif


#if	SOLARIS
#define	bzero(a,b)	memset(a,0,b)
#endif
int	use_inet6 = 0;
char	thishost[MAXHOSTNAMELEN];

extern	char	*optarg;

void	dostats __P((int, natstat_t *, int, int, int *));
void	dotable __P((natstat_t *, int, int, int, char *));
void	flushtable __P((int, int, int *));
void	usage __P((char *));
int	main __P((int, char*[]));
void	showhostmap __P((natstat_t *nsp));
void	natstat_dead __P((natstat_t *, char *));
void	dostats_live __P((int, natstat_t *, int, int *));
void	showhostmap_dead __P((natstat_t *));
void	showhostmap_live __P((int, natstat_t *));
void	dostats_dead __P((natstat_t *, int, int *));
int	nat_matcharray __P((nat_t *, int *));

int		opts;
int		nohdrfields = 0;
wordtab_t	*nat_fields = NULL;

void usage(name)
	char *name;
{
	fprintf(stderr, "Usage: %s [-CFhlnrRsv] [-f filename]\n", name);
	exit(1);
}


int main(argc, argv)
	int argc;
	char *argv[];
{
	int fd, c, mode, *natfilter;
	char *file, *core, *kernel;
	natstat_t ns, *nsp;
	ipfobj_t obj;

	fd = -1;
	opts = 0;
	nsp = &ns;
	file = NULL;
	core = NULL;
	kernel = NULL;
	mode = O_RDWR;
	natfilter = NULL;

	assigndefined(getenv("IPNAT_PREDEFINED"));

	while ((c = getopt(argc, argv, "CdFf:hlm:M:N:nO:prRsv")) != -1)
		switch (c)
		{
		case 'C' :
			opts |= OPT_CLEAR;
			break;
		case 'd' :
			opts |= OPT_DEBUG;
			break;
		case 'f' :
			file = optarg;
			break;
		case 'F' :
			opts |= OPT_FLUSH;
			break;
		case 'h' :
			opts |=OPT_HITS;
			break;
		case 'l' :
			opts |= OPT_LIST;
			mode = O_RDONLY;
			break;
		case 'm' :
			natfilter = parseipfexpr(optarg, NULL);
			break;
		case 'M' :
			core = optarg;
			break;
		case 'N' :
			kernel = optarg;
			break;
		case 'n' :
			opts |= OPT_DONOTHING|OPT_DONTOPEN;
			mode = O_RDONLY;
			break;
		case 'O' :
			nat_fields = parsefields(natfields, optarg);
			break;
		case 'p' :
			opts |= OPT_PURGE;
			break;
		case 'R' :
			opts |= OPT_NORESOLVE;
			break;
		case 'r' :
			opts |= OPT_REMOVE;
			break;
		case 's' :
			opts |= OPT_STAT;
			mode = O_RDONLY;
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		default :
			usage(argv[0]);
		}

	if (((opts & OPT_PURGE) != 0) && ((opts & OPT_REMOVE) == 0)) {
		(void) fprintf(stderr, "%s: -p must be used with -r\n",
			       argv[0]);
		exit(1);
	}

	initparse();

	if ((kernel != NULL) || (core != NULL)) {
		(void) setgid(getgid());
		(void) setuid(getuid());
	}

	if (!(opts & OPT_DONOTHING)) {
		if (((fd = open(IPNAT_NAME, mode)) == -1) &&
		    ((fd = open(IPNAT_NAME, O_RDONLY)) == -1)) {
			(void) fprintf(stderr, "%s: open: %s\n", IPNAT_NAME,
				STRERROR(errno));
			exit(1);
		}
	}

	bzero((char *)&ns, sizeof(ns));

	if ((opts & OPT_DONOTHING) == 0) {
		if (checkrev(IPL_NAME) == -1) {
			fprintf(stderr, "User/kernel version check failed\n");
			exit(1);
		}
	}

	if (!(opts & OPT_DONOTHING) && (kernel == NULL) && (core == NULL)) {
		bzero((char *)&obj, sizeof(obj));
		obj.ipfo_rev = IPFILTER_VERSION;
		obj.ipfo_type = IPFOBJ_NATSTAT;
		obj.ipfo_size = sizeof(*nsp);
		obj.ipfo_ptr = (void *)nsp;
		if (ioctl(fd, SIOCGNATS, &obj) == -1) {
			ipferror(fd, "ioctl(SIOCGNATS)");
			exit(1);
		}
		(void) setgid(getgid());
		(void) setuid(getuid());
	} else if ((kernel != NULL) || (core != NULL)) {
		if (openkmem(kernel, core) == -1)
			exit(1);

		natstat_dead(nsp, kernel);
		if (opts & (OPT_LIST|OPT_STAT))
			dostats(fd, nsp, opts, 0, natfilter);
		exit(0);
	}

	if (opts & (OPT_FLUSH|OPT_CLEAR))
		flushtable(fd, opts, natfilter);
	if (file) {
		return ipnat_parsefile(fd, ipnat_addrule, ioctl, file);
	}
	if (opts & (OPT_LIST|OPT_STAT))
		dostats(fd, nsp, opts, 1, natfilter);
	return 0;
}


/*
 * Read NAT statistic information in using a symbol table and memory file
 * rather than doing ioctl's.
 */
void natstat_dead(nsp, kernel)
	natstat_t *nsp;
	char *kernel;
{
	struct nlist nat_nlist[10] = {
		{ "nat_table" },		/* 0 */
		{ "nat_list" },
		{ "maptable" },
		{ "ipf_nattable_sz" },
		{ "ipf_natrules_sz" },
		{ "ipf_rdrrules_sz" },		/* 5 */
		{ "ipf_hostmap_sz" },
		{ "nat_instances" },
		{ NULL }
	};
	void *tables[2];

	if (nlist(kernel, nat_nlist) == -1) {
		fprintf(stderr, "nlist error\n");
		return;
	}

	/*
	 * Normally the ioctl copies all of these values into the structure
	 * for us, before returning it to userland, so here we must copy each
	 * one in individually.
	 */
	kmemcpy((char *)&tables, nat_nlist[0].n_value, sizeof(tables));
	nsp->ns_side[0].ns_table = tables[0];
	nsp->ns_side[1].ns_table = tables[1];

	kmemcpy((char *)&nsp->ns_list, nat_nlist[1].n_value,
		sizeof(nsp->ns_list));
	kmemcpy((char *)&nsp->ns_maptable, nat_nlist[2].n_value,
		sizeof(nsp->ns_maptable));
	kmemcpy((char *)&nsp->ns_nattab_sz, nat_nlist[3].n_value,
		sizeof(nsp->ns_nattab_sz));
	kmemcpy((char *)&nsp->ns_rultab_sz, nat_nlist[4].n_value,
		sizeof(nsp->ns_rultab_sz));
	kmemcpy((char *)&nsp->ns_rdrtab_sz, nat_nlist[5].n_value,
		sizeof(nsp->ns_rdrtab_sz));
	kmemcpy((char *)&nsp->ns_hostmap_sz, nat_nlist[6].n_value,
		sizeof(nsp->ns_hostmap_sz));
	kmemcpy((char *)&nsp->ns_instances, nat_nlist[7].n_value,
		sizeof(nsp->ns_instances));
}


/*
 * Issue an ioctl to flush either the NAT rules table or the active mapping
 * table or both.
 */
void flushtable(fd, opts, match)
	int fd, opts, *match;
{
	int n = 0;

	if (opts & OPT_FLUSH) {
		n = 0;
		if (!(opts & OPT_DONOTHING)) {
			if (match != NULL) {
				ipfobj_t obj;

				obj.ipfo_rev = IPFILTER_VERSION;
				obj.ipfo_size = match[0] * sizeof(int);
				obj.ipfo_type = IPFOBJ_IPFEXPR;
				obj.ipfo_ptr = match;
				if (ioctl(fd, SIOCMATCHFLUSH, &obj) == -1) {
					ipferror(fd, "ioctl(SIOCMATCHFLUSH)");
					n = -1;
				} else {
					n = obj.ipfo_retval;
				}
			} else if (ioctl(fd, SIOCIPFFL, &n) == -1) {
				ipferror(fd, "ioctl(SIOCIPFFL)");
				n = -1;
			}
		}
		if (n >= 0)
			printf("%d entries flushed from NAT table\n", n);
	}

	if (opts & OPT_CLEAR) {
		n = 1;
		if (!(opts & OPT_DONOTHING) && ioctl(fd, SIOCIPFFL, &n) == -1)
			ipferror(fd, "ioctl(SIOCCNATL)");
		else
			printf("%d entries flushed from NAT list\n", n);
	}
}


/*
 * Display NAT statistics.
 */
void dostats_dead(nsp, opts, filter)
	natstat_t *nsp;
	int opts, *filter;
{
	nat_t *np, nat;
	ipnat_t	ipn;
	int i;

	if (nat_fields == NULL) {
		printf("List of active MAP/Redirect filters:\n");
		while (nsp->ns_list) {
			if (kmemcpy((char *)&ipn, (long)nsp->ns_list,
				    sizeof(ipn))) {
				perror("kmemcpy");
				break;
			}
			if (opts & OPT_HITS)
				printf("%lu ", ipn.in_hits);
			printnat(&ipn, opts & (OPT_DEBUG|OPT_VERBOSE));
			nsp->ns_list = ipn.in_next;
		}
	}

	if (nat_fields == NULL) {
		printf("\nList of active sessions:\n");

	} else if (nohdrfields == 0) {
		for (i = 0; nat_fields[i].w_value != 0; i++) {
			printfieldhdr(natfields, nat_fields + i);
			if (nat_fields[i + 1].w_value != 0)
				printf("\t");
		}
		printf("\n");
	}

	for (np = nsp->ns_instances; np; np = nat.nat_next) {
		if (kmemcpy((char *)&nat, (long)np, sizeof(nat)))
			break;
		if ((filter != NULL) && (nat_matcharray(&nat, filter) == 0))
			continue;
		if (nat_fields != NULL) {
			for (i = 0; nat_fields[i].w_value != 0; i++) {
				printnatfield(&nat, nat_fields[i].w_value);
				if (nat_fields[i + 1].w_value != 0)
					printf("\t");
			}
			printf("\n");
		} else {
			printactivenat(&nat, opts, nsp->ns_ticks);
			if (nat.nat_aps) {
				int proto;

				if (nat.nat_dir & NAT_OUTBOUND)
					proto = nat.nat_pr[1];
				else
					proto = nat.nat_pr[0];
				printaps(nat.nat_aps, opts, proto);
			}
		}
	}

	if (opts & OPT_VERBOSE)
		showhostmap_dead(nsp);
}


void dotable(nsp, fd, alive, which, side)
	natstat_t *nsp;
	int fd, alive, which;
	char *side;
{
	int sz, i, used, maxlen, minlen, totallen;
	ipftable_t table;
	u_int *buckets;
	ipfobj_t obj;

	sz = sizeof(*buckets) * nsp->ns_nattab_sz;
	buckets = (u_int *)malloc(sz);
	if (buckets == NULL) {
		fprintf(stderr,
			"cannot allocate memory (%d) for buckets\n", sz);
		return;
	}

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GTABLE;
	obj.ipfo_size = sizeof(table);
	obj.ipfo_ptr = &table;

	if (which == 0) {
		table.ita_type = IPFTABLE_BUCKETS_NATIN;
	} else if (which == 1) {
		table.ita_type = IPFTABLE_BUCKETS_NATOUT;
	}
	table.ita_table = buckets;

	if (alive) {
		if (ioctl(fd, SIOCGTABL, &obj) != 0) {
			ipferror(fd, "SIOCFTABL");
			free(buckets);
			return;
		}
	} else {
		if (kmemcpy((char *)buckets, (u_long)nsp->ns_nattab_sz, sz)) {
			free(buckets);
			return;
		}
	}

	minlen = nsp->ns_side[which].ns_inuse;
	totallen = 0;
	maxlen = 0;
	used = 0;

	for (i = 0; i < nsp->ns_nattab_sz; i++) {
		if (buckets[i] > maxlen)
			maxlen = buckets[i];
		if (buckets[i] < minlen)
			minlen = buckets[i];
		if (buckets[i] != 0)
			used++;
		totallen += buckets[i];
	}

	printf("%d%%\thash efficiency %s\n",
	       totallen ? used * 100 / totallen : 0, side);
	printf("%2.2f%%\tbucket usage %s\n",
	       ((float)used / nsp->ns_nattab_sz) * 100.0, side);
	printf("%d\tminimal length %s\n", minlen, side);
	printf("%d\tmaximal length %s\n", maxlen, side);
	printf("%.3f\taverage length %s\n",
	       used ? ((float)totallen / used) : 0.0, side);

	free(buckets);
}


void dostats(fd, nsp, opts, alive, filter)
	natstat_t *nsp;
	int fd, opts, alive, *filter;
{
	/*
	 * Show statistics ?
	 */
	if (opts & OPT_STAT) {
		printnatside("in", &nsp->ns_side[0]);
		dotable(nsp, fd, alive, 0, "in");

		printnatside("out", &nsp->ns_side[1]);
		dotable(nsp, fd, alive, 1, "out");

		printf("%lu\tlog successes\n", nsp->ns_side[0].ns_log);
		printf("%lu\tlog failures\n", nsp->ns_side[1].ns_log);
		printf("%lu\tadded in\n%lu\tadded out\n",
			nsp->ns_side[0].ns_added,
			nsp->ns_side[1].ns_added);
		printf("%u\tactive\n", nsp->ns_active);
		printf("%lu\ttransparent adds\n", nsp->ns_addtrpnt);
		printf("%lu\tdivert build\n", nsp->ns_divert_build);
		printf("%lu\texpired\n", nsp->ns_expire);
		printf("%lu\tflush all\n", nsp->ns_flush_all);
		printf("%lu\tflush closing\n", nsp->ns_flush_closing);
		printf("%lu\tflush queue\n", nsp->ns_flush_queue);
		printf("%lu\tflush state\n", nsp->ns_flush_state);
		printf("%lu\tflush timeout\n", nsp->ns_flush_timeout);
		printf("%lu\thostmap new\n", nsp->ns_hm_new);
		printf("%lu\thostmap fails\n", nsp->ns_hm_newfail);
		printf("%lu\thostmap add\n", nsp->ns_hm_addref);
		printf("%lu\thostmap NULL rule\n", nsp->ns_hm_nullnp);
		printf("%lu\tlog ok\n", nsp->ns_log_ok);
		printf("%lu\tlog fail\n", nsp->ns_log_fail);
		printf("%u\torphan count\n", nsp->ns_orphans);
		printf("%u\trule count\n", nsp->ns_rules);
		printf("%u\tmap rules\n", nsp->ns_rules_map);
		printf("%u\trdr rules\n", nsp->ns_rules_rdr);
		printf("%u\twilds\n", nsp->ns_wilds);
		if (opts & OPT_VERBOSE)
			printf("list %p\n", nsp->ns_list);
	}

	if (opts & OPT_LIST) {
		if (alive)
			dostats_live(fd, nsp, opts, filter);
		else
			dostats_dead(nsp, opts, filter);
	}
}


/*
 * Display NAT statistics.
 */
void dostats_live(fd, nsp, opts, filter)
	natstat_t *nsp;
	int fd, opts, *filter;
{
	ipfgeniter_t iter;
	char buffer[2000];
	ipfobj_t obj;
	ipnat_t	*ipn;
	nat_t nat;
	int i;

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GENITER;
	obj.ipfo_size = sizeof(iter);
	obj.ipfo_ptr = &iter;

	iter.igi_type = IPFGENITER_IPNAT;
	iter.igi_nitems = 1;
	iter.igi_data = buffer;
	ipn = (ipnat_t *)buffer;

	/*
	 * Show list of NAT rules and NAT sessions ?
	 */
	if (nat_fields == NULL) {
		printf("List of active MAP/Redirect filters:\n");
		while (nsp->ns_list) {
			if (ioctl(fd, SIOCGENITER, &obj) == -1)
				break;
			if (opts & OPT_HITS)
				printf("%lu ", ipn->in_hits);
			printnat(ipn, opts & (OPT_DEBUG|OPT_VERBOSE));
			nsp->ns_list = ipn->in_next;
		}
	}

	if (nat_fields == NULL) {
		printf("\nList of active sessions:\n");

	} else if (nohdrfields == 0) {
		for (i = 0; nat_fields[i].w_value != 0; i++) {
			printfieldhdr(natfields, nat_fields + i);
			if (nat_fields[i + 1].w_value != 0)
				printf("\t");
		}
		printf("\n");
	}

	i = IPFGENITER_IPNAT;
	(void) ioctl(fd,SIOCIPFDELTOK, &i);


	iter.igi_type = IPFGENITER_NAT;
	iter.igi_nitems = 1;
	iter.igi_data = &nat;

	while (nsp->ns_instances != NULL) {
		if (ioctl(fd, SIOCGENITER, &obj) == -1)
			break;
		if ((filter != NULL) && (nat_matcharray(&nat, filter) == 0))
			continue;
		if (nat_fields != NULL) {
			for (i = 0; nat_fields[i].w_value != 0; i++) {
				printnatfield(&nat, nat_fields[i].w_value);
				if (nat_fields[i + 1].w_value != 0)
					printf("\t");
			}
			printf("\n");
		} else {
			printactivenat(&nat, opts, nsp->ns_ticks);
			if (nat.nat_aps) {
				int proto;

				if (nat.nat_dir & NAT_OUTBOUND)
					proto = nat.nat_pr[1];
				else
					proto = nat.nat_pr[0];
				printaps(nat.nat_aps, opts, proto);
			}
		}
		nsp->ns_instances = nat.nat_next;
	}

	if (opts & OPT_VERBOSE)
		showhostmap_live(fd, nsp);

	i = IPFGENITER_NAT;
	(void) ioctl(fd,SIOCIPFDELTOK, &i);
}


/*
 * Display the active host mapping table.
 */
void showhostmap_dead(nsp)
	natstat_t *nsp;
{
	hostmap_t hm, *hmp, **maptable;
	u_int hv;

	printf("\nList of active host mappings:\n");

	maptable = (hostmap_t **)malloc(sizeof(hostmap_t *) *
					nsp->ns_hostmap_sz);
	if (kmemcpy((char *)maptable, (u_long)nsp->ns_maptable,
		    sizeof(hostmap_t *) * nsp->ns_hostmap_sz)) {
		perror("kmemcpy (maptable)");
		return;
	}

	for (hv = 0; hv < nsp->ns_hostmap_sz; hv++) {
		hmp = maptable[hv];

		while (hmp) {
			if (kmemcpy((char *)&hm, (u_long)hmp, sizeof(hm))) {
				perror("kmemcpy (hostmap)");
				return;
			}

			printhostmap(&hm, hv);
			hmp = hm.hm_next;
		}
	}
	free(maptable);
}


/*
 * Display the active host mapping table.
 */
void showhostmap_live(fd, nsp)
	int fd;
	natstat_t *nsp;
{
	ipfgeniter_t iter;
	hostmap_t hm;
	ipfobj_t obj;
	int i;

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_GENITER;
	obj.ipfo_size = sizeof(iter);
	obj.ipfo_ptr = &iter;

	iter.igi_type = IPFGENITER_HOSTMAP;
	iter.igi_nitems = 1;
	iter.igi_data = &hm;

	printf("\nList of active host mappings:\n");

	while (nsp->ns_maplist != NULL) {
		if (ioctl(fd, SIOCGENITER, &obj) == -1)
			break;
		printhostmap(&hm, hm.hm_hv);
		nsp->ns_maplist = hm.hm_next;
	}

	i = IPFGENITER_HOSTMAP;
	(void) ioctl(fd,SIOCIPFDELTOK, &i);
}


int nat_matcharray(nat, array)
	nat_t *nat;
	int *array;
{
	int i, n, *x, rv, p;
	ipfexp_t *e;

	rv = 0;
	n = array[0];
	x = array + 1;

	for (; n > 0; x += 3 + x[3], rv = 0) {
		e = (ipfexp_t *)x;
		if (e->ipfe_cmd == IPF_EXP_END)
			break;
		n -= e->ipfe_size;

		p = e->ipfe_cmd >> 16;
		if ((p != 0) && (p != nat->nat_pr[1]))
			break;

		switch (e->ipfe_cmd)
		{
		case IPF_EXP_IP_PR :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (nat->nat_pr[1] == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_IP_SRCADDR :
			if (nat->nat_v[0] != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((nat->nat_osrcaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				      ((nat->nat_nsrcaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_DSTADDR :
			if (nat->nat_v[0] != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((nat->nat_odstaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				      ((nat->nat_ndstaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

		case IPF_EXP_IP_ADDR :
			if (nat->nat_v[0] != 4)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= ((nat->nat_osrcaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				      ((nat->nat_nsrcaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				     ((nat->nat_odstaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]) ||
				     ((nat->nat_ndstaddr &
					e->ipfe_arg0[i * 2 + 1]) ==
				       e->ipfe_arg0[i * 2]);
			}
			break;

#ifdef USE_INET6
		case IPF_EXP_IP6_SRCADDR :
			if (nat->nat_v[0] != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&nat->nat_osrc6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&nat->nat_nsrc6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_DSTADDR :
			if (nat->nat_v[0] != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&nat->nat_odst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&nat->nat_ndst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;

		case IPF_EXP_IP6_ADDR :
			if (nat->nat_v[0] != 6)
				break;
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= IP6_MASKEQ(&nat->nat_osrc6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&nat->nat_nsrc6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&nat->nat_odst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]) ||
				      IP6_MASKEQ(&nat->nat_ndst6,
						 &e->ipfe_arg0[i * 8 + 4],
						 &e->ipfe_arg0[i * 8]);
			}
			break;
#endif

		case IPF_EXP_UDP_PORT :
		case IPF_EXP_TCP_PORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (nat->nat_osport == e->ipfe_arg0[i]) ||
				      (nat->nat_nsport == e->ipfe_arg0[i]) ||
				      (nat->nat_odport == e->ipfe_arg0[i]) ||
				      (nat->nat_ndport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_SPORT :
		case IPF_EXP_TCP_SPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (nat->nat_osport == e->ipfe_arg0[i]) ||
				      (nat->nat_nsport == e->ipfe_arg0[i]);
			}
			break;

		case IPF_EXP_UDP_DPORT :
		case IPF_EXP_TCP_DPORT :
			for (i = 0; !rv && i < e->ipfe_narg; i++) {
				rv |= (nat->nat_odport == e->ipfe_arg0[i]) ||
				      (nat->nat_ndport == e->ipfe_arg0[i]);
			}
			break;
		}
		rv ^= e->ipfe_not;

		if (rv == 0)
			break;
	}

	return rv;
}
