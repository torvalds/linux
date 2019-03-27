/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ip_fil.c	2.41 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#include "ipf.h"
#include "md5.h"
#include "ipt.h"

ipf_main_softc_t	ipfmain;

static	struct	ifnet **ifneta = NULL;
static	int	nifs = 0;

struct	rtentry;

static	void	ipf_setifpaddr __P((struct ifnet *, char *));
void	init_ifp __P((void));
static int 	no_output __P((struct ifnet *, struct mbuf *,
			       struct sockaddr *, struct rtentry *));
static int	write_output __P((struct ifnet *, struct mbuf *,
				  struct sockaddr *, struct rtentry *));

struct ifaddr {
	struct sockaddr_storage ifa_addr;
};

int
ipfattach(softc)
	ipf_main_softc_t *softc;
{
	return 0;
}


int
ipfdetach(softc)
	ipf_main_softc_t *softc;
{
	return 0;
}


/*
 * Filter ioctl interface.
 */
int
ipfioctl(softc, dev, cmd, data, mode)
	ipf_main_softc_t *softc;
	int dev;
	ioctlcmd_t cmd;
	caddr_t data;
	int mode;
{
	int error = 0, unit = 0, uid;

	uid = getuid();
	unit = dev;

	SPL_NET(s);

	error = ipf_ioctlswitch(softc, unit, data, cmd, mode, uid, NULL);
	if (error != -1) {
		SPL_X(s);
		return error;
	}
	SPL_X(s);
	return error;
}


void
ipf_forgetifp(softc, ifp)
	ipf_main_softc_t *softc;
	void *ifp;
{
	register frentry_t *f;

	WRITE_ENTER(&softc->ipf_mutex);
	for (f = softc->ipf_acct[0][softc->ipf_active]; (f != NULL);
	     f = f->fr_next)
		if (f->fr_ifa == ifp)
			f->fr_ifa = (void *)-1;
	for (f = softc->ipf_acct[1][softc->ipf_active]; (f != NULL);
	     f = f->fr_next)
		if (f->fr_ifa == ifp)
			f->fr_ifa = (void *)-1;
	for (f = softc->ipf_rules[0][softc->ipf_active]; (f != NULL);
	     f = f->fr_next)
		if (f->fr_ifa == ifp)
			f->fr_ifa = (void *)-1;
	for (f = softc->ipf_rules[1][softc->ipf_active]; (f != NULL);
	     f = f->fr_next)
		if (f->fr_ifa == ifp)
			f->fr_ifa = (void *)-1;
	RWLOCK_EXIT(&softc->ipf_mutex);
	ipf_nat_sync(softc, ifp);
	ipf_lookup_sync(softc, ifp);
}


static int
no_output(ifp, m, s, rt)
	struct rtentry *rt;
	struct ifnet *ifp;
	struct mbuf *m;
	struct sockaddr *s;
{
	return 0;
}


static int
write_output(ifp, m, s, rt)
	struct rtentry *rt;
	struct ifnet *ifp;
	struct mbuf *m;
	struct sockaddr *s;
{
	char fname[32];
	mb_t *mb;
	ip_t *ip;
	int fd;

	mb = (mb_t *)m;
	ip = MTOD(mb, ip_t *);

#if (defined(NetBSD) && (NetBSD <= 1991011) && (NetBSD >= 199606)) || \
    defined(__FreeBSD__)
	sprintf(fname, "/tmp/%s", ifp->if_xname);
#else
	sprintf(fname, "/tmp/%s%d", ifp->if_name, ifp->if_unit);
#endif
	fd = open(fname, O_WRONLY|O_APPEND);
	if (fd == -1) {
		perror("open");
		return -1;
	}
	write(fd, (char *)ip, ntohs(ip->ip_len));
	close(fd);
	return 0;
}


static void
ipf_setifpaddr(ifp, addr)
	struct ifnet *ifp;
	char *addr;
{
	struct ifaddr *ifa;

#if defined(__NetBSD__) || defined(__FreeBSD__)
	if (ifp->if_addrlist.tqh_first != NULL)
#else
	if (ifp->if_addrlist != NULL)
#endif
		return;

	ifa = (struct ifaddr *)malloc(sizeof(*ifa));
#if defined(__NetBSD__) || defined(__FreeBSD__)
	ifp->if_addrlist.tqh_first = ifa;
#else
	ifp->if_addrlist = ifa;
#endif

	if (ifa != NULL) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)&ifa->ifa_addr;
#ifdef USE_INET6
		if (index(addr, ':') != NULL) {
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)&ifa->ifa_addr;
			sin6->sin6_family = AF_INET6;
			/* Abort if bad address. */
			switch (inet_pton(AF_INET6, addr, &sin6->sin6_addr))
			{
			case 1:
				break;
			case -1:
				perror("inet_pton");
				abort();
				break;
			default:
				abort();
				break;
			}
		} else
#endif
		{
			sin->sin_family = AF_INET;
			sin->sin_addr.s_addr = inet_addr(addr);
			if (sin->sin_addr.s_addr == 0)
				abort();
		}
	}
}

struct ifnet *
get_unit(name, family)
	char *name;
	int family;
{
	struct ifnet *ifp, **ifpp, **old_ifneta;
	char *addr;
#if (defined(NetBSD) && (NetBSD <= 1991011) && (NetBSD >= 199606)) || \
    defined(__FreeBSD__)

	if (!*name)
		return NULL;

	if (name == NULL)
		name = "anon0";

	addr = strchr(name, '=');
	if (addr != NULL)
		*addr++ = '\0';

	for (ifpp = ifneta; ifpp && (ifp = *ifpp); ifpp++) {
		if (!strcmp(name, ifp->if_xname)) {
			if (addr != NULL)
				ipf_setifpaddr(ifp, addr);
			return ifp;
		}
	}
#else
	char *s, ifname[LIFNAMSIZ+1];

	if (name == NULL)
		name = "anon0";

	addr = strchr(name, '=');
	if (addr != NULL)
		*addr++ = '\0';

	for (ifpp = ifneta; ifpp && (ifp = *ifpp); ifpp++) {
		COPYIFNAME(family, ifp, ifname);
		if (!strcmp(name, ifname)) {
			if (addr != NULL)
				ipf_setifpaddr(ifp, addr);
			return ifp;
		}
	}
#endif

	if (!ifneta) {
		ifneta = (struct ifnet **)malloc(sizeof(ifp) * 2);
		if (!ifneta)
			return NULL;
		ifneta[1] = NULL;
		ifneta[0] = (struct ifnet *)calloc(1, sizeof(*ifp));
		if (!ifneta[0]) {
			free(ifneta);
			return NULL;
		}
		nifs = 1;
	} else {
		old_ifneta = ifneta;
		nifs++;
		ifneta = (struct ifnet **)reallocarray(ifneta, nifs + 1, 
						  sizeof(ifp));
		if (!ifneta) {
			free(old_ifneta);
			nifs = 0;
			return NULL;
		}
		ifneta[nifs] = NULL;
		ifneta[nifs - 1] = (struct ifnet *)malloc(sizeof(*ifp));
		if (!ifneta[nifs - 1]) {
			nifs--;
			return NULL;
		}
	}
	ifp = ifneta[nifs - 1];

#if defined(__NetBSD__) || defined(__FreeBSD__)
	TAILQ_INIT(&ifp->if_addrlist);
#endif
#if (defined(NetBSD) && (NetBSD <= 1991011) && (NetBSD >= 199606)) || \
    defined(__FreeBSD__)
	(void) strncpy(ifp->if_xname, name, sizeof(ifp->if_xname));
#else
	s = name + strlen(name) - 1;
	for (; s > name; s--) {
		if (!ISDIGIT(*s)) {
			s++;
			break;
		}
	}
		
	if ((s > name) && (*s != 0) && ISDIGIT(*s)) {
		ifp->if_unit = atoi(s);
		ifp->if_name = (char *)malloc(s - name + 1);
		(void) strncpy(ifp->if_name, name, s - name);
		ifp->if_name[s - name] = '\0';
	} else {
		ifp->if_name = strdup(name);
		ifp->if_unit = -1;
	}
#endif
	ifp->if_output = (void *)no_output;

	if (addr != NULL) {
		ipf_setifpaddr(ifp, addr);
	}

	return ifp;
}


char *
get_ifname(ifp)
	struct ifnet *ifp;
{
	static char ifname[LIFNAMSIZ];

#if defined(__NetBSD__) || defined(__FreeBSD__)
	sprintf(ifname, "%s", ifp->if_xname);
#else
	if (ifp->if_unit != -1)
		sprintf(ifname, "%s%d", ifp->if_name, ifp->if_unit);
	else
		strcpy(ifname, ifp->if_name);
#endif
	return ifname;
}



void
init_ifp()
{
	struct ifnet *ifp, **ifpp;
	char fname[32];
	int fd;

#if (defined(NetBSD) && (NetBSD <= 1991011) && (NetBSD >= 199606)) || \
    defined(__FreeBSD__)
	for (ifpp = ifneta; ifpp && (ifp = *ifpp); ifpp++) {
		ifp->if_output = (void *)write_output;
		sprintf(fname, "/tmp/%s", ifp->if_xname);
		fd = open(fname, O_WRONLY|O_CREAT|O_EXCL|O_TRUNC, 0600);
		if (fd == -1)
			perror("open");
		else
			close(fd);
	}
#else

	for (ifpp = ifneta; ifpp && (ifp = *ifpp); ifpp++) {
		ifp->if_output = (void *)write_output;
		sprintf(fname, "/tmp/%s%d", ifp->if_name, ifp->if_unit);
		fd = open(fname, O_WRONLY|O_CREAT|O_EXCL|O_TRUNC, 0600);
		if (fd == -1)
			perror("open");
		else
			close(fd);
	}
#endif
}


int
ipf_fastroute(m, mpp, fin, fdp)
	mb_t *m, **mpp;
	fr_info_t *fin;
	frdest_t *fdp;
{
	struct ifnet *ifp;
	ip_t *ip = fin->fin_ip;
	frdest_t node;
	int error = 0;
	frentry_t *fr;
	void *sifp;
	int sout;

	sifp = fin->fin_ifp;
	sout = fin->fin_out;
	fr = fin->fin_fr;
	ip->ip_sum = 0;

	if (!(fr->fr_flags & FR_KEEPSTATE) && (fdp != NULL) &&
	    (fdp->fd_type == FRD_DSTLIST)) {
		bzero(&node, sizeof(node));
		ipf_dstlist_select_node(fin, fdp->fd_ptr, NULL, &node);
		fdp = &node;
	}
	ifp = fdp->fd_ptr;

	if (ifp == NULL)
		return 0;	/* no routing table out here */

	if (fin->fin_out == 0) {
		fin->fin_ifp = ifp;
		fin->fin_out = 1;
		(void) ipf_acctpkt(fin, NULL);
		fin->fin_fr = NULL;
		if (!fr || !(fr->fr_flags & FR_RETMASK)) {
			u_32_t pass;

			(void) ipf_state_check(fin, &pass);
		}

		switch (ipf_nat_checkout(fin, NULL))
		{
		case 0 :
			break;
		case 1 :
			ip->ip_sum = 0;
			break;
		case -1 :
			error = -1;
			goto done;
			break;
		}

	}

	m->mb_ifp = ifp;
	printpacket(fin->fin_out, m);

	(*ifp->if_output)(ifp, (void *)m, NULL, 0);
done:
	fin->fin_ifp = sifp;
	fin->fin_out = sout;
	return error;
}


int
ipf_send_reset(fin)
	fr_info_t *fin;
{
	ipfkverbose("- TCP RST sent\n");
	return 0;
}


int
ipf_send_icmp_err(type, fin, dst)
	int type;
	fr_info_t *fin;
	int dst;
{
	ipfkverbose("- ICMP unreachable sent\n");
	return 0;
}


void
m_freem(m)
	mb_t *m;
{
	return;
}


void
m_copydata(m, off, len, cp)
	mb_t *m;
	int off, len;
	caddr_t cp;
{
	bcopy((char *)m + off, cp, len);
}


int
ipfuiomove(buf, len, rwflag, uio)
	caddr_t buf;
	int len, rwflag;
	struct uio *uio;
{
	int left, ioc, num, offset;
	struct iovec *io;
	char *start;

	if (rwflag == UIO_READ) {
		left = len;
		ioc = 0;

		offset = uio->uio_offset;

		while ((left > 0) && (ioc < uio->uio_iovcnt)) {
			io = uio->uio_iov + ioc;
			num = io->iov_len;
			if (num > left)
				num = left;
			start = (char *)io->iov_base + offset;
			if (start > (char *)io->iov_base + io->iov_len) {
				offset -= io->iov_len;
				ioc++;
				continue;
			}
			bcopy(buf, start, num);
			uio->uio_resid -= num;
			uio->uio_offset += num;
			left -= num;
			if (left > 0)
				ioc++;
		}
		if (left > 0)
			return EFAULT;
	}
	return 0;
}


u_32_t
ipf_newisn(fin)
	fr_info_t *fin;
{
	static int iss_seq_off = 0;
	u_char hash[16];
	u_32_t newiss;
	MD5_CTX ctx;

	/*
	 * Compute the base value of the ISS.  It is a hash
	 * of (saddr, sport, daddr, dport, secret).
	 */
	MD5Init(&ctx);

	MD5Update(&ctx, (u_char *) &fin->fin_fi.fi_src,
		  sizeof(fin->fin_fi.fi_src));
	MD5Update(&ctx, (u_char *) &fin->fin_fi.fi_dst,
		  sizeof(fin->fin_fi.fi_dst));
	MD5Update(&ctx, (u_char *) &fin->fin_dat, sizeof(fin->fin_dat));

	/* MD5Update(&ctx, ipf_iss_secret, sizeof(ipf_iss_secret)); */

	MD5Final(hash, &ctx);

	memcpy(&newiss, hash, sizeof(newiss));

	/*
	 * Now increment our "timer", and add it in to
	 * the computed value.
	 *
	 * XXX Use `addin'?
	 * XXX TCP_ISSINCR too large to use?
	 */
	iss_seq_off += 0x00010000;
	newiss += iss_seq_off;
	return newiss;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_nextipid                                                */
/* Returns:     int - 0 == success, -1 == error (packet should be droppped) */
/* Parameters:  fin(I) - pointer to packet information                      */
/*                                                                          */
/* Returns the next IPv4 ID to use for this packet.                         */
/* ------------------------------------------------------------------------ */
INLINE u_short
ipf_nextipid(fin)
	fr_info_t *fin;
{
	static u_short ipid = 0;
	ipf_main_softc_t *softc = fin->fin_main_soft;
	u_short id;

	MUTEX_ENTER(&softc->ipf_rw);
	if (fin->fin_pktnum != 0) {
		/*
		 * The -1 is for aligned test results.
		 */
		id = (fin->fin_pktnum - 1) & 0xffff;
	} else {
	}
		id = ipid++;
	MUTEX_EXIT(&softc->ipf_rw);

	return id;
}


INLINE int
ipf_checkv4sum(fin)
	fr_info_t *fin;
{

	if (fin->fin_flx & FI_SHORT)
		return 1;

	if (ipf_checkl4sum(fin) == -1) {
		fin->fin_flx |= FI_BAD;
		return -1;
	}
	return 0;
}


#ifdef	USE_INET6
INLINE int
ipf_checkv6sum(fin)
	fr_info_t *fin;
{
	if (fin->fin_flx & FI_SHORT)
		return 1;

	if (ipf_checkl4sum(fin) == -1) {
		fin->fin_flx |= FI_BAD;
		return -1;
	}
	return 0;
}
#endif


#if 0
/*
 * See above for description, except that all addressing is in user space.
 */
int
copyoutptr(softc, src, dst, size)
	void *src, *dst;
	size_t size;
{
	caddr_t ca;

	bcopy(dst, (char *)&ca, sizeof(ca));
	bcopy(src, ca, size);
	return 0;
}


/*
 * See above for description, except that all addressing is in user space.
 */
int
copyinptr(src, dst, size)
	void *src, *dst;
	size_t size;
{
	caddr_t ca;

	bcopy(src, (char *)&ca, sizeof(ca));
	bcopy(ca, dst, size);
	return 0;
}
#endif


/*
 * return the first IP Address associated with an interface
 */
int
ipf_ifpaddr(softc, v, atype, ifptr, inp, inpmask)
	ipf_main_softc_t *softc;
	int v, atype;
	void *ifptr;
	i6addr_t *inp, *inpmask;
{
	struct ifnet *ifp = ifptr;
	struct ifaddr *ifa;

#if defined(__NetBSD__) || defined(__FreeBSD__)
	ifa = ifp->if_addrlist.tqh_first;
#else
	ifa = ifp->if_addrlist;
#endif
	if (ifa != NULL) {
		if (v == 4) {
			struct sockaddr_in *sin, mask;

			mask.sin_addr.s_addr = 0xffffffff;

			sin = (struct sockaddr_in *)&ifa->ifa_addr;

			return ipf_ifpfillv4addr(atype, sin, &mask,
						 &inp->in4, &inpmask->in4);
		}
#ifdef USE_INET6
		if (v == 6) {
			struct sockaddr_in6 *sin6, mask;

			sin6 = (struct sockaddr_in6 *)&ifa->ifa_addr;
			((i6addr_t *)&mask.sin6_addr)->i6[0] = 0xffffffff;
			((i6addr_t *)&mask.sin6_addr)->i6[1] = 0xffffffff;
			((i6addr_t *)&mask.sin6_addr)->i6[2] = 0xffffffff;
			((i6addr_t *)&mask.sin6_addr)->i6[3] = 0xffffffff;
			return ipf_ifpfillv6addr(atype, sin6, &mask,
						 inp, inpmask);
		}
#endif
	}
	return 0;
}


/*
 * This function is not meant to be random, rather just produce a
 * sequence of numbers that isn't linear to show "randomness".
 */
u_32_t
ipf_random()
{
	static unsigned int last = 0xa5a5a5a5;
	static int calls = 0;
	int number;

	calls++;

	/*
	 * These are deliberately chosen to ensure that there is some
	 * attempt to test whether the output covers the range in test n18.
	 */
	switch (calls)
	{
	case 1 :
		number = 0;
		break;
	case 2 :
		number = 4;
		break;
	case 3 :
		number = 3999;
		break;
	case 4 :
		number = 4000;
		break;
	case 5 :
		number = 48999;
		break;
	case 6 :
		number = 49000;
		break;
	default :
		number = last;
		last *= calls;
		last++;
		number ^= last;
		break;
	}
	return number;
}


int
ipf_verifysrc(fin)
	fr_info_t *fin;
{
	return 1;
}


int
ipf_inject(fin, m)
	fr_info_t *fin;
	mb_t *m;
{
	FREE_MB_T(m);

	return 0;
}


u_int
ipf_pcksum(fin, hlen, sum)
	fr_info_t *fin;
	int hlen;
	u_int sum;
{
	u_short *sp;
	u_int sum2;
	int slen;

	slen = fin->fin_plen - hlen;
	sp = (u_short *)((u_char *)fin->fin_ip + hlen);

	for (; slen > 1; slen -= 2)
		sum += *sp++;
	if (slen)
		sum += ntohs(*(u_char *)sp << 8);
	while (sum > 0xffff)
		sum = (sum & 0xffff) + (sum >> 16);
	sum2 = (u_short)(~sum & 0xffff);

	return sum2;
}


void *
ipf_pullup(m, fin, plen)
	mb_t *m;
	fr_info_t *fin;
	int plen;
{
	if (M_LEN(m) >= plen)
		return fin->fin_ip;

	/*
	 * Fake ipf_pullup failing
	 */
	fin->fin_reason = FRB_PULLUP;
	*fin->fin_mp = NULL;
	fin->fin_m = NULL;
	fin->fin_ip = NULL;
	return NULL;
}
