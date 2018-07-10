/* vi: set sw=4 ts=4: */
/*
 * stolen from net-tools-1.59 and stripped down for busybox by
 *			Erik Andersen <andersen@codepoet.org>
 *
 * Heavily modified by Manuel Novoa III       Mar 12, 2001
 *
 * Added print_bytes_scaled function to reduce code size.
 * Added some (potentially) missing defines.
 * Improved display support for -a and for a named interface.
 *
 * -----------------------------------------------------------
 *
 * ifconfig   This file contains an implementation of the command
 *              that either displays or sets the characteristics of
 *              one or more of the system's networking interfaces.
 *
 *
 * Author:      Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *              and others.  Copyright 1993 MicroWalt Corporation
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Patched to support 'add' and 'del' keywords for INET(4) addresses
 * by Mrs. Brisby <mrs.brisby@nimh.org>
 *
 * {1.34} - 19980630 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *                     - gettext instead of catgets for i18n
 *          10/1998  - Andi Kleen. Use interface list primitives.
 *          20001008 - Bernd Eckenfels, Patch from RH for setting mtu
 *			(default AF was wrong)
 */
#include "libbb.h"
#include "inet_common.h"
#include <net/if.h>
#include <net/if_arp.h>
#ifdef HAVE_NET_ETHERNET_H
# include <net/ethernet.h>
#endif

#if ENABLE_FEATURE_HWIB
/* #include <linux/if_infiniband.h> */
# undef INFINIBAND_ALEN
# define INFINIBAND_ALEN 20
#endif

#if ENABLE_FEATURE_IPV6
# define HAVE_AFINET6 1
#else
# undef HAVE_AFINET6
#endif

#define _PATH_PROCNET_DEV               "/proc/net/dev"
#define _PATH_PROCNET_IFINET6           "/proc/net/if_inet6"

#ifdef HAVE_AFINET6
# ifndef _LINUX_IN6_H
/*
 * This is from linux/include/net/ipv6.h
 */
struct in6_ifreq {
	struct in6_addr ifr6_addr;
	uint32_t ifr6_prefixlen;
	unsigned int ifr6_ifindex;
};
# endif
#endif /* HAVE_AFINET6 */

/* Defines for glibc2.0 users. */
#ifndef SIOCSIFTXQLEN
# define SIOCSIFTXQLEN      0x8943
# define SIOCGIFTXQLEN      0x8942
#endif

/* ifr_qlen is ifru_ivalue, but it isn't present in 2.0 kernel headers */
#ifndef ifr_qlen
# define ifr_qlen        ifr_ifru.ifru_mtu
#endif

#ifndef HAVE_TXQUEUELEN
# define HAVE_TXQUEUELEN 1
#endif

#ifndef IFF_DYNAMIC
# define IFF_DYNAMIC     0x8000 /* dialup device with changing addresses */
#endif

/* Display an Internet socket address. */
static const char* FAST_FUNC INET_sprint(struct sockaddr *sap, int numeric)
{
	if (sap->sa_family == 0xFFFF || sap->sa_family == 0)
		return "[NONE SET]";
	return auto_string(INET_rresolve((struct sockaddr_in *) sap, numeric, 0xffffff00));
}

#ifdef UNUSED_AND_BUGGY
static int INET_getsock(char *bufp, struct sockaddr *sap)
{
	char *sp = bufp, *bp;
	unsigned int i;
	unsigned val;
	struct sockaddr_in *sock_in;

	sock_in = (struct sockaddr_in *) sap;
	sock_in->sin_family = AF_INET;
	sock_in->sin_port = 0;

	val = 0;
	bp = (char *) &val;
	for (i = 0; i < sizeof(sock_in->sin_addr.s_addr); i++) {
		*sp = toupper(*sp);

		if ((unsigned)(*sp - 'A') <= 5)
			bp[i] |= (int) (*sp - ('A' - 10));
		else if (isdigit(*sp))
			bp[i] |= (int) (*sp - '0');
		else
			return -1;

		bp[i] <<= 4;
		sp++;
		*sp = toupper(*sp);

		if ((unsigned)(*sp - 'A') <= 5)
			bp[i] |= (int) (*sp - ('A' - 10));
		else if (isdigit(*sp))
			bp[i] |= (int) (*sp - '0');
		else
			return -1;

		sp++;
	}
	sock_in->sin_addr.s_addr = htonl(val);

	return (sp - bufp);
}
#endif

static int FAST_FUNC INET_input(/*int type,*/ const char *bufp, struct sockaddr *sap)
{
	return INET_resolve(bufp, (struct sockaddr_in *) sap, 0);
/*
	switch (type) {
	case 1:
		return (INET_getsock(bufp, sap));
	case 256:
		return (INET_resolve(bufp, (struct sockaddr_in *) sap, 1));
	default:
		return (INET_resolve(bufp, (struct sockaddr_in *) sap, 0));
	}
*/
}

static const struct aftype inet_aftype = {
	.name   = "inet",
	.title  = "DARPA Internet",
	.af     = AF_INET,
	.alen   = 4,
	.sprint = INET_sprint,
	.input  = INET_input,
};

#ifdef HAVE_AFINET6

/* Display an Internet socket address. */
/* dirty! struct sockaddr usually doesn't suffer for inet6 addresses, fst. */
static const char* FAST_FUNC INET6_sprint(struct sockaddr *sap, int numeric)
{
	if (sap->sa_family == 0xFFFF || sap->sa_family == 0)
		return "[NONE SET]";
	return auto_string(INET6_rresolve((struct sockaddr_in6 *) sap, numeric));
}

#ifdef UNUSED
static int INET6_getsock(char *bufp, struct sockaddr *sap)
{
	struct sockaddr_in6 *sin6;

	sin6 = (struct sockaddr_in6 *) sap;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = 0;

	if (inet_pton(AF_INET6, bufp, sin6->sin6_addr.s6_addr) <= 0)
		return -1;

	return 16;			/* ?;) */
}
#endif

static int FAST_FUNC INET6_input(/*int type,*/ const char *bufp, struct sockaddr *sap)
{
	return INET6_resolve(bufp, (struct sockaddr_in6 *) sap);
/*
	switch (type) {
	case 1:
		return (INET6_getsock(bufp, sap));
	default:
		return (INET6_resolve(bufp, (struct sockaddr_in6 *) sap));
	}
*/
}

static const struct aftype inet6_aftype = {
	.name   = "inet6",
	.title  = "IPv6",
	.af     = AF_INET6,
	.alen   = sizeof(struct in6_addr),
	.sprint = INET6_sprint,
	.input  = INET6_input,
};

#endif /* HAVE_AFINET6 */

/* Display an UNSPEC address. */
static char* FAST_FUNC UNSPEC_print(unsigned char *ptr)
{
	char *buff;
	char *pos;
	unsigned int i;

	buff = auto_string(xmalloc(sizeof(struct sockaddr) * 3 + 1));
	pos = buff;
	for (i = 0; i < sizeof(struct sockaddr); i++) {
		/* careful -- not every libc's sprintf returns # bytes written */
		sprintf(pos, "%02X-", *ptr++);
		pos += 3;
	}
	/* Erase trailing "-".  Works as long as sizeof(struct sockaddr) != 0 */
	*--pos = '\0';
	return buff;
}

/* Display an UNSPEC socket address. */
static const char* FAST_FUNC UNSPEC_sprint(struct sockaddr *sap, int numeric UNUSED_PARAM)
{
	if (sap->sa_family == 0xFFFF || sap->sa_family == 0)
		return "[NONE SET]";
	return UNSPEC_print((unsigned char *)sap->sa_data);
}

static const struct aftype unspec_aftype = {
	.name   = "unspec",
	.title  = "UNSPEC",
	.af     = AF_UNSPEC,
	.alen   = 0,
	.print  = UNSPEC_print,
	.sprint = UNSPEC_sprint,
};

static const struct aftype *const aftypes[] = {
	&inet_aftype,
#ifdef HAVE_AFINET6
	&inet6_aftype,
#endif
	&unspec_aftype,
	NULL
};

/* Check our protocol family table for this family. */
const struct aftype* FAST_FUNC get_aftype(const char *name)
{
	const struct aftype *const *afp;

	afp = aftypes;
	while (*afp != NULL) {
		if (strcmp((*afp)->name, name) == 0)
			return (*afp);
		afp++;
	}
	return NULL;
}

/* Check our protocol family table for this family. */
static const struct aftype *get_afntype(int af)
{
	const struct aftype *const *afp;

	afp = aftypes;
	while (*afp != NULL) {
		if ((*afp)->af == af)
			return *afp;
		afp++;
	}
	return NULL;
}

struct user_net_device_stats {
	unsigned long long rx_packets;	/* total packets received       */
	unsigned long long tx_packets;	/* total packets transmitted    */
	unsigned long long rx_bytes;	/* total bytes received         */
	unsigned long long tx_bytes;	/* total bytes transmitted      */
	unsigned long rx_errors;	/* bad packets received         */
	unsigned long tx_errors;	/* packet transmit problems     */
	unsigned long rx_dropped;	/* no space in linux buffers    */
	unsigned long tx_dropped;	/* no space available in linux  */
	unsigned long rx_multicast;	/* multicast packets received   */
	unsigned long rx_compressed;
	unsigned long tx_compressed;
	unsigned long collisions;

	/* detailed rx_errors: */
	unsigned long rx_length_errors;
	unsigned long rx_over_errors;	/* receiver ring buff overflow  */
	unsigned long rx_crc_errors;	/* recved pkt with crc error    */
	unsigned long rx_frame_errors;	/* recv'd frame alignment error */
	unsigned long rx_fifo_errors;	/* recv'r fifo overrun          */
	unsigned long rx_missed_errors;	/* receiver missed packet     */
	/* detailed tx_errors */
	unsigned long tx_aborted_errors;
	unsigned long tx_carrier_errors;
	unsigned long tx_fifo_errors;
	unsigned long tx_heartbeat_errors;
	unsigned long tx_window_errors;
};

struct interface {
	struct interface *next, *prev;
	char name[IFNAMSIZ];                    /* interface name        */
	short type;                             /* if type               */
	short flags;                            /* various flags         */
	int tx_queue_len;                       /* transmit queue length */

	/* these should be contiguous, zeroed in one go in if_fetch(): */
#define FIRST_TO_ZERO metric
	int metric;                             /* routing metric        */
	int mtu;                                /* MTU value             */
	struct ifmap map;                       /* hardware setup        */
	struct sockaddr addr;                   /* IP address            */
	struct sockaddr dstaddr;                /* P-P IP address        */
	struct sockaddr broadaddr;              /* IP broadcast address  */
	struct sockaddr netmask;                /* IP network mask       */
	char hwaddr[32];                        /* HW address            */
#define LAST_TO_ZERO hwaddr

	smallint has_ip;
	smallint statistics_valid;
	struct user_net_device_stats stats;     /* statistics            */
#if 0 /* UNUSED */
	int keepalive;                          /* keepalive value for SLIP */
	int outfill;                            /* outfill value for SLIP */
#endif
};

struct iface_list {
	struct interface *int_list, *int_last;
};


#if 0
/* like strcmp(), but knows about numbers */
except that the freshly added calls to xatoul() barf on ethernet aliases with
uClibc with e.g.: ife->name='lo'  name='eth0:1'
static int nstrcmp(const char *a, const char *b)
{
	const char *a_ptr = a;
	const char *b_ptr = b;

	while (*a == *b) {
		if (*a == '\0') {
			return 0;
		}
		if (!isdigit(*a) && isdigit(*(a+1))) {
			a_ptr = a+1;
			b_ptr = b+1;
		}
		a++;
		b++;
	}

	if (isdigit(*a) && isdigit(*b)) {
		return xatoul(a_ptr) > xatoul(b_ptr) ? 1 : -1;
	}
	return *a - *b;
}
#endif

static struct interface *add_interface(struct iface_list *ilist, char *name)
{
	struct interface *ife, **nextp, *new;

	for (ife = ilist->int_last; ife; ife = ife->prev) {
		int n = /*n*/strcmp(ife->name, name);

		if (n == 0)
			return ife;
		if (n < 0)
			break;
	}

	new = xzalloc(sizeof(*new));
	strncpy_IFNAMSIZ(new->name, name);

	nextp = ife ? &ife->next : &ilist->int_list;
	new->prev = ife;
	new->next = *nextp;
	if (new->next)
		new->next->prev = new;
	else
		ilist->int_last = new;
	*nextp = new;
	return new;
}

static char *get_name(char name[IFNAMSIZ], char *p)
{
	/* Extract NAME from nul-terminated p of the form "<whitespace>NAME:"
	 * If match is not made, set NAME to "" and return unchanged p.
	 */
	char *nameend;
	char *namestart;

	nameend = namestart = skip_whitespace(p);

	for (;;) {
		if ((nameend - namestart) >= IFNAMSIZ)
			break; /* interface name too large - return "" */
		if (*nameend == ':') {
			memcpy(name, namestart, nameend - namestart);
			name[nameend - namestart] = '\0';
			return nameend + 1;
		}
		nameend++;
		/* isspace, NUL, any control char? */
		if ((unsigned char)*nameend <= (unsigned char)' ')
			break; /* trailing ':' not found - return "" */
	}
	name[0] = '\0';
	return p;
}

/* If scanf supports size qualifiers for %n conversions, then we can
 * use a modified fmt that simply stores the position in the fields
 * having no associated fields in the proc string.  Of course, we need
 * to zero them again when we're done.  But that is smaller than the
 * old approach of multiple scanf occurrences with large numbers of
 * args. */

/* static const char *const ss_fmt[] = { */
/*	"%lln%llu%lu%lu%lu%lu%ln%ln%lln%llu%lu%lu%lu%lu%lu", */
/*	"%llu%llu%lu%lu%lu%lu%ln%ln%llu%llu%lu%lu%lu%lu%lu", */
/*	"%llu%llu%lu%lu%lu%lu%lu%lu%llu%llu%lu%lu%lu%lu%lu%lu" */
/* }; */

/* We use %n for unavailable data in older versions of /proc/net/dev formats.
 * This results in bogus stores to ife->FOO members corresponding to
 * %n specifiers (even the size of integers may not match).
 */
#if INT_MAX == LONG_MAX
static const char *const ss_fmt[] = {
	"%n%llu%u%u%u%u%n%n%n%llu%u%u%u%u%u",
	"%llu%llu%u%u%u%u%n%n%llu%llu%u%u%u%u%u",
	"%llu%llu%u%u%u%u%u%u%llu%llu%u%u%u%u%u%u"
};
#else
static const char *const ss_fmt[] = {
	"%n%llu%lu%lu%lu%lu%n%n%n%llu%lu%lu%lu%lu%lu",
	"%llu%llu%lu%lu%lu%lu%n%n%llu%llu%lu%lu%lu%lu%lu",
	"%llu%llu%lu%lu%lu%lu%lu%lu%llu%llu%lu%lu%lu%lu%lu%lu"
};
#endif

static void get_dev_fields(char *bp, struct interface *ife, int procnetdev_vsn)
{
	memset(&ife->stats, 0, sizeof(struct user_net_device_stats));

	sscanf(bp, ss_fmt[procnetdev_vsn],
		   &ife->stats.rx_bytes, /* missing for v0 */
		   &ife->stats.rx_packets,
		   &ife->stats.rx_errors,
		   &ife->stats.rx_dropped,
		   &ife->stats.rx_fifo_errors,
		   &ife->stats.rx_frame_errors,
		   &ife->stats.rx_compressed, /* missing for v0, v1 */
		   &ife->stats.rx_multicast, /* missing for v0, v1 */
		   &ife->stats.tx_bytes, /* missing for v0 */
		   &ife->stats.tx_packets,
		   &ife->stats.tx_errors,
		   &ife->stats.tx_dropped,
		   &ife->stats.tx_fifo_errors,
		   &ife->stats.collisions,
		   &ife->stats.tx_carrier_errors,
		   &ife->stats.tx_compressed /* missing for v0, v1 */
		   );

	if (procnetdev_vsn <= 1) {
		if (procnetdev_vsn == 0) {
			ife->stats.rx_bytes = 0;
			ife->stats.tx_bytes = 0;
		}
		ife->stats.rx_multicast = 0;
		ife->stats.rx_compressed = 0;
		ife->stats.tx_compressed = 0;
	}
}

static int procnetdev_version(char *buf)
{
	if (strstr(buf, "compressed"))
		return 2;
	if (strstr(buf, "bytes"))
		return 1;
	return 0;
}

static void if_readconf(struct iface_list *ilist)
{
	int numreqs = 30;
	struct ifconf ifc;
	struct ifreq *ifr;
	int n;
	int skfd;

	ifc.ifc_buf = NULL;

	/* SIOCGIFCONF currently seems to only work properly on AF_INET sockets
	   (as of 2.1.128) */
	skfd = xsocket(AF_INET, SOCK_DGRAM, 0);

	for (;;) {
		ifc.ifc_len = sizeof(struct ifreq) * numreqs;
		ifc.ifc_buf = xrealloc(ifc.ifc_buf, ifc.ifc_len);

		xioctl(skfd, SIOCGIFCONF, &ifc);
		if (ifc.ifc_len == (int)(sizeof(struct ifreq) * numreqs)) {
			/* assume it overflowed and try again */
			numreqs += 10;
			continue;
		}
		break;
	}

	ifr = ifc.ifc_req;
	for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
		add_interface(ilist, ifr->ifr_name);
		ifr++;
	}

	close(skfd);
	free(ifc.ifc_buf);
}

static int if_readlist_proc(struct iface_list *ilist, char *ifname)
{
	FILE *fh;
	char buf[512];
	struct interface *ife;
	int procnetdev_vsn;
	int ret;

	fh = fopen_or_warn(_PATH_PROCNET_DEV, "r");
	if (!fh) {
		return 0; /* "not found" */
	}
	fgets(buf, sizeof buf, fh);	/* eat line */
	fgets(buf, sizeof buf, fh);

	procnetdev_vsn = procnetdev_version(buf);

	ret = 0;
	while (fgets(buf, sizeof buf, fh)) {
		char *s, name[IFNAMSIZ];

		s = get_name(name, buf);
		ife = add_interface(ilist, name);
		get_dev_fields(s, ife, procnetdev_vsn);
		ife->statistics_valid = 1;
		if (ifname && strcmp(ifname, name) == 0) {
			ret = 1; /* found */
			break;
		}
	}
	fclose(fh);
	return ret;
}

static void if_readlist(struct iface_list *ilist, char *ifname)
{
	int found = if_readlist_proc(ilist, ifname);
	/* Needed in order to get ethN:M aliases */
	if (!found)
		if_readconf(ilist);
}

/* Fetch the interface configuration from the kernel. */
static int if_fetch(struct interface *ife)
{
	struct ifreq ifr;
	char *ifname = ife->name;
	int skfd;

	skfd = xsocket(AF_INET, SOCK_DGRAM, 0);

	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0) {
		close(skfd);
		return -1;
	}
	ife->flags = ifr.ifr_flags;

	/* set up default values if ioctl's would fail */
	ife->tx_queue_len = -1;	/* unknown value */
	memset(&ife->FIRST_TO_ZERO, 0,
		offsetof(struct interface, LAST_TO_ZERO)
			- offsetof(struct interface, FIRST_TO_ZERO)
		+ sizeof(ife->LAST_TO_ZERO)
	);

	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFHWADDR, &ifr) >= 0)
		memcpy(ife->hwaddr, ifr.ifr_hwaddr.sa_data, 8);

//er.... why this _isnt_ inside if()?
	ife->type = ifr.ifr_hwaddr.sa_family;

	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFMETRIC, &ifr) >= 0)
		ife->metric = ifr.ifr_metric;

	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFMTU, &ifr) >= 0)
		ife->mtu = ifr.ifr_mtu;

#ifdef SIOCGIFMAP
	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFMAP, &ifr) == 0)
		ife->map = ifr.ifr_map;
#endif

#ifdef HAVE_TXQUEUELEN
	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	if (ioctl(skfd, SIOCGIFTXQLEN, &ifr) >= 0)
		ife->tx_queue_len = ifr.ifr_qlen;
#endif

	strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
	ifr.ifr_addr.sa_family = AF_INET;
	if (ioctl(skfd, SIOCGIFADDR, &ifr) == 0) {
		ife->has_ip = 1;
		ife->addr = ifr.ifr_addr;
		strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
		if (ioctl(skfd, SIOCGIFDSTADDR, &ifr) >= 0)
			ife->dstaddr = ifr.ifr_dstaddr;

		strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
		if (ioctl(skfd, SIOCGIFBRDADDR, &ifr) >= 0)
			ife->broadaddr = ifr.ifr_broadaddr;

		strncpy_IFNAMSIZ(ifr.ifr_name, ifname);
		if (ioctl(skfd, SIOCGIFNETMASK, &ifr) >= 0)
			ife->netmask = ifr.ifr_netmask;
	}

	close(skfd);
	return 0;
}

static int do_if_fetch(struct interface *ife)
{
	if (if_fetch(ife) < 0) {
		const char *errmsg;

		if (errno == ENODEV) {
			/* Give better error message for this case. */
			errmsg = "Device not found";
		} else {
			errmsg = strerror(errno);
		}
		bb_error_msg("%s: error fetching interface information: %s",
				ife->name, errmsg);
		return -1;
	}
	return 0;
}

static const struct hwtype unspec_hwtype = {
	.name =		"unspec",
	.title =	"UNSPEC",
	.type =		-1,
	.print =	UNSPEC_print
};

static const struct hwtype loop_hwtype = {
	.name =		"loop",
	.title =	"Local Loopback",
	.type =		ARPHRD_LOOPBACK
};

/* Display an Ethernet address in readable format. */
static char* FAST_FUNC ether_print(unsigned char *ptr)
{
	char *buff;
	buff = xasprintf("%02X:%02X:%02X:%02X:%02X:%02X",
		ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]
	);
	return auto_string(buff);
}

static const struct hwtype ether_hwtype = {
	.name  = "ether",
	.title = "Ethernet",
	.type  = ARPHRD_ETHER,
	.alen  = ETH_ALEN,
	.print = ether_print,
	.input = in_ether
};

static const struct hwtype ppp_hwtype = {
	.name =		"ppp",
	.title =	"Point-to-Point Protocol",
	.type =		ARPHRD_PPP
};

#if ENABLE_FEATURE_IPV6
static const struct hwtype sit_hwtype = {
	.name =			"sit",
	.title =		"IPv6-in-IPv4",
	.type =			ARPHRD_SIT,
	.print =		UNSPEC_print,
	.suppress_null_addr =	1
};
#endif
#if ENABLE_FEATURE_HWIB
static const struct hwtype ib_hwtype = {
	.name  = "infiniband",
	.title = "InfiniBand",
	.type  = ARPHRD_INFINIBAND,
	.alen  = INFINIBAND_ALEN,
	.print = UNSPEC_print,
	.input = in_ib,
};
#endif


static const struct hwtype *const hwtypes[] = {
	&loop_hwtype,
	&ether_hwtype,
	&ppp_hwtype,
	&unspec_hwtype,
#if ENABLE_FEATURE_IPV6
	&sit_hwtype,
#endif
#if ENABLE_FEATURE_HWIB
	&ib_hwtype,
#endif
	NULL
};

#ifdef IFF_PORTSEL
static const char *const if_port_text[] = {
	/* Keep in step with <linux/netdevice.h> */
	"unknown",
	"10base2",
	"10baseT",
	"AUI",
	"100baseT",
	"100baseTX",
	"100baseFX",
	NULL
};
#endif

/* Check our hardware type table for this type. */
const struct hwtype* FAST_FUNC get_hwtype(const char *name)
{
	const struct hwtype *const *hwp;

	hwp = hwtypes;
	while (*hwp != NULL) {
		if (strcmp((*hwp)->name, name) == 0)
			return (*hwp);
		hwp++;
	}
	return NULL;
}

/* Check our hardware type table for this type. */
const struct hwtype* FAST_FUNC get_hwntype(int type)
{
	const struct hwtype *const *hwp;

	hwp = hwtypes;
	while (*hwp != NULL) {
		if ((*hwp)->type == type)
			return *hwp;
		hwp++;
	}
	return NULL;
}

/* return 1 if address is all zeros */
static int hw_null_address(const struct hwtype *hw, void *ap)
{
	int i;
	unsigned char *address = (unsigned char *) ap;

	for (i = 0; i < hw->alen; i++)
		if (address[i])
			return 0;
	return 1;
}

static const char TRext[] ALIGN1 = "\0\0\0Ki\0Mi\0Gi\0Ti";

static void print_bytes_scaled(unsigned long long ull, const char *end)
{
	unsigned long long int_part;
	const char *ext;
	unsigned int frac_part;
	int i;

	frac_part = 0;
	ext = TRext;
	int_part = ull;
	i = 4;
	do {
		if (int_part >= 1024) {
			frac_part = ((((unsigned int) int_part) & (1024-1)) * 10) / 1024;
			int_part /= 1024;
			ext += 3;	/* KiB, MiB, GiB, TiB */
		}
		--i;
	} while (i);

	printf("X bytes:%llu (%llu.%u %sB)%s", ull, int_part, frac_part, ext, end);
}


#ifdef HAVE_AFINET6
#define IPV6_ADDR_ANY           0x0000U

#define IPV6_ADDR_UNICAST       0x0001U
#define IPV6_ADDR_MULTICAST     0x0002U
#define IPV6_ADDR_ANYCAST       0x0004U

#define IPV6_ADDR_LOOPBACK      0x0010U
#define IPV6_ADDR_LINKLOCAL     0x0020U
#define IPV6_ADDR_SITELOCAL     0x0040U

#define IPV6_ADDR_COMPATv4      0x0080U

#define IPV6_ADDR_SCOPE_MASK    0x00f0U

#define IPV6_ADDR_MAPPED        0x1000U
#define IPV6_ADDR_RESERVED      0x2000U	/* reserved address space */


static void ife_print6(struct interface *ptr)
{
	FILE *f;
	char addr6[40], devname[21];
	struct sockaddr_in6 sap;
	int plen, scope, dad_status, if_idx;
	char addr6p[8][5];

	f = fopen_for_read(_PATH_PROCNET_IFINET6);
	if (f == NULL)
		return;

	while (fscanf
		   (f, "%4s%4s%4s%4s%4s%4s%4s%4s %08x %02x %02x %02x %20s\n",
			addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4],
			addr6p[5], addr6p[6], addr6p[7], &if_idx, &plen, &scope,
			&dad_status, devname) != EOF
	) {
		if (strcmp(devname, ptr->name) == 0) {
			sprintf(addr6, "%s:%s:%s:%s:%s:%s:%s:%s",
					addr6p[0], addr6p[1], addr6p[2], addr6p[3],
					addr6p[4], addr6p[5], addr6p[6], addr6p[7]);
			memset(&sap, 0, sizeof(sap));
			inet_pton(AF_INET6, addr6,
					  (struct sockaddr *) &sap.sin6_addr);
			sap.sin6_family = AF_INET6;
			printf("          inet6 addr: %s/%d",
				INET6_sprint((struct sockaddr *) &sap, 1),
				plen);
			printf(" Scope:");
			switch (scope & IPV6_ADDR_SCOPE_MASK) {
			case 0:
				puts("Global");
				break;
			case IPV6_ADDR_LINKLOCAL:
				puts("Link");
				break;
			case IPV6_ADDR_SITELOCAL:
				puts("Site");
				break;
			case IPV6_ADDR_COMPATv4:
				puts("Compat");
				break;
			case IPV6_ADDR_LOOPBACK:
				puts("Host");
				break;
			default:
				puts("Unknown");
			}
		}
	}
	fclose(f);
}
#else
#define ife_print6(a) ((void)0)
#endif

static void ife_print(struct interface *ptr)
{
	const struct aftype *ap;
	const struct hwtype *hw;
	int hf;
	int can_compress = 0;

	ap = get_afntype(ptr->addr.sa_family);
	if (ap == NULL)
		ap = get_afntype(0);

	hf = ptr->type;

	if (hf == ARPHRD_CSLIP || hf == ARPHRD_CSLIP6)
		can_compress = 1;

	hw = get_hwntype(hf);
	if (hw == NULL)
		hw = get_hwntype(-1);

	printf("%-9s Link encap:%s  ", ptr->name, hw->title);
	/* For some hardware types (eg Ash, ATM) we don't print the
	   hardware address if it's null.  */
	if (hw->print != NULL
	 && !(hw_null_address(hw, ptr->hwaddr) && hw->suppress_null_addr)
	) {
		printf("HWaddr %s  ", hw->print((unsigned char *)ptr->hwaddr));
	}
#ifdef IFF_PORTSEL
	if (ptr->flags & IFF_PORTSEL) {
		printf("Media:%s", if_port_text[ptr->map.port] /* [0] */);
		if (ptr->flags & IFF_AUTOMEDIA)
			printf("(auto)");
	}
#endif
	bb_putchar('\n');

	if (ptr->has_ip) {
		printf("          %s addr:%s ", ap->name,
			ap->sprint(&ptr->addr, 1));
		if (ptr->flags & IFF_POINTOPOINT) {
			printf(" P-t-P:%s ", ap->sprint(&ptr->dstaddr, 1));
		}
		if (ptr->flags & IFF_BROADCAST) {
			printf(" Bcast:%s ", ap->sprint(&ptr->broadaddr, 1));
		}
		printf(" Mask:%s\n", ap->sprint(&ptr->netmask, 1));
	}

	ife_print6(ptr);

	printf("          ");
	/* DONT FORGET TO ADD THE FLAGS IN ife_print_short, too */

	if (ptr->flags == 0) {
		printf("[NO FLAGS] ");
	} else {
		static const char ife_print_flags_strs[] ALIGN1 =
			"UP\0"
			"BROADCAST\0"
			"DEBUG\0"
			"LOOPBACK\0"
			"POINTOPOINT\0"
			"NOTRAILERS\0"
			"RUNNING\0"
			"NOARP\0"
			"PROMISC\0"
			"ALLMULTI\0"
			"SLAVE\0"
			"MASTER\0"
			"MULTICAST\0"
#ifdef HAVE_DYNAMIC
			"DYNAMIC\0"
#endif
			;
		static const unsigned short ife_print_flags_mask[] ALIGN2 = {
			IFF_UP,
			IFF_BROADCAST,
			IFF_DEBUG,
			IFF_LOOPBACK,
			IFF_POINTOPOINT,
			IFF_NOTRAILERS,
			IFF_RUNNING,
			IFF_NOARP,
			IFF_PROMISC,
			IFF_ALLMULTI,
			IFF_SLAVE,
			IFF_MASTER,
			IFF_MULTICAST
#ifdef HAVE_DYNAMIC
			,IFF_DYNAMIC
#endif
		};
		const unsigned short *mask = ife_print_flags_mask;
		const char *str = ife_print_flags_strs;
		do {
			if (ptr->flags & *mask) {
				printf("%s ", str);
			}
			mask++;
			str += strlen(str) + 1;
		} while (*str);
	}

	/* DONT FORGET TO ADD THE FLAGS IN ife_print_short */
	printf(" MTU:%d  Metric:%d", ptr->mtu, ptr->metric ? ptr->metric : 1);
#if 0
#ifdef SIOCSKEEPALIVE
	if (ptr->outfill || ptr->keepalive)
		printf("  Outfill:%d  Keepalive:%d", ptr->outfill, ptr->keepalive);
#endif
#endif
	bb_putchar('\n');

	/* If needed, display the interface statistics. */

	if (ptr->statistics_valid) {
		/* XXX: statistics are currently only printed for the primary address,
		 *      not for the aliases, although strictly speaking they're shared
		 *      by all addresses.
		 */
		printf("          ");

		printf("RX packets:%llu errors:%lu dropped:%lu overruns:%lu frame:%lu\n",
			ptr->stats.rx_packets, ptr->stats.rx_errors,
			ptr->stats.rx_dropped, ptr->stats.rx_fifo_errors,
			ptr->stats.rx_frame_errors);
		if (can_compress)
			printf("             compressed:%lu\n",
				ptr->stats.rx_compressed);
		printf("          ");
		printf("TX packets:%llu errors:%lu dropped:%lu overruns:%lu carrier:%lu\n",
			ptr->stats.tx_packets, ptr->stats.tx_errors,
			ptr->stats.tx_dropped, ptr->stats.tx_fifo_errors,
			ptr->stats.tx_carrier_errors);
		printf("          collisions:%lu ", ptr->stats.collisions);
		if (can_compress)
			printf("compressed:%lu ", ptr->stats.tx_compressed);
		if (ptr->tx_queue_len != -1)
			printf("txqueuelen:%d ", ptr->tx_queue_len);
		printf("\n          R");
		print_bytes_scaled(ptr->stats.rx_bytes, "  T");
		print_bytes_scaled(ptr->stats.tx_bytes, "\n");
	}

	if (ptr->map.irq || ptr->map.mem_start
	 || ptr->map.dma || ptr->map.base_addr
	) {
		printf("          ");
		if (ptr->map.irq)
			printf("Interrupt:%d ", ptr->map.irq);
		if (ptr->map.base_addr >= 0x100) /* Only print devices using it for I/O maps */
			printf("Base address:0x%lx ",
				(unsigned long) ptr->map.base_addr);
		if (ptr->map.mem_start) {
			printf("Memory:%lx-%lx ", ptr->map.mem_start,
				ptr->map.mem_end);
		}
		if (ptr->map.dma)
			printf("DMA chan:%x ", ptr->map.dma);
		bb_putchar('\n');
	}
	bb_putchar('\n');
}

static int do_if_print(struct interface *ife, int show_downed_too)
{
	int res;

	res = do_if_fetch(ife);
	if (res >= 0) {
		if ((ife->flags & IFF_UP) || show_downed_too)
			ife_print(ife);
	}
	return res;
}

int FAST_FUNC display_interfaces(char *ifname)
{
	struct interface *ife;
	int res;
	struct iface_list ilist;

	ilist.int_list = NULL;
	ilist.int_last = NULL;
	if_readlist(&ilist, ifname != IFNAME_SHOW_DOWNED_TOO ? ifname : NULL);

	if (!ifname || ifname == IFNAME_SHOW_DOWNED_TOO) {
		for (ife = ilist.int_list; ife; ife = ife->next) {

			BUILD_BUG_ON((int)(intptr_t)IFNAME_SHOW_DOWNED_TOO != 1);

			res = do_if_print(ife, (int)(intptr_t)ifname);
			if (res < 0)
				goto ret;
		}
		return 0;
	}

	ife = add_interface(&ilist, ifname);
	res = do_if_print(ife, /*show_downed_too:*/ 1);
 ret:
	return (res < 0); /* status < 0 == 1 -- error */
}

#if ENABLE_FEATURE_HWIB
/* Input an Infiniband address and convert to binary. */
int FAST_FUNC in_ib(const char *bufp, struct sockaddr *sap)
{
	sap->sa_family = ib_hwtype.type;
//TODO: error check?
	hex2bin((char*)sap->sa_data, bufp, INFINIBAND_ALEN);
# ifdef HWIB_DEBUG
	fprintf(stderr, "in_ib(%s): %s\n", bufp, UNSPEC_print(sap->sa_data));
# endif
	return 0;
}
#endif
