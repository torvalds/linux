/* vi: set sw=4 ts=4: */
/*
 * ifconfig
 *
 * Similar to the standard Unix ifconfig, but with only the necessary
 * parts for AF_INET, and without any printing of if info (for now).
 *
 * Bjorn Wesen, Axis Communications AB
 *
 * Authors of the original ifconfig was:
 *              Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * Heavily modified by Manuel Novoa III       Mar 6, 2001
 *
 * From initial port to busybox, removed most of the redundancy by
 * converting to a table-driven approach.  Added several (optional)
 * args missing from initial port.
 *
 * Still missing:  media, tunnel.
 *
 * 2002-04-20
 * IPV6 support added by Bart Visscher <magick@linux-fan.com>
 */
//config:config IFCONFIG
//config:	bool "ifconfig (12 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Ifconfig is used to configure the kernel-resident network interfaces.
//config:
//config:config FEATURE_IFCONFIG_STATUS
//config:	bool "Enable status reporting output (+7k)"
//config:	default y
//config:	depends on IFCONFIG
//config:	help
//config:	If ifconfig is called with no arguments it will display the status
//config:	of the currently active interfaces.
//config:
//config:config FEATURE_IFCONFIG_SLIP
//config:	bool "Enable slip-specific options \"keepalive\" and \"outfill\""
//config:	default y
//config:	depends on IFCONFIG
//config:	help
//config:	Allow "keepalive" and "outfill" support for SLIP. If you're not
//config:	planning on using serial lines, leave this unchecked.
//config:
//config:config FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
//config:	bool "Enable options \"mem_start\", \"io_addr\", and \"irq\""
//config:	default y
//config:	depends on IFCONFIG
//config:	help
//config:	Allow the start address for shared memory, start address for I/O,
//config:	and/or the interrupt line used by the specified device.
//config:
//config:config FEATURE_IFCONFIG_HW
//config:	bool "Enable option \"hw\" (ether only)"
//config:	default y
//config:	depends on IFCONFIG
//config:	help
//config:	Set the hardware address of this interface, if the device driver
//config:	supports  this  operation. Currently, we only support the 'ether'
//config:	class.
//config:
//config:config FEATURE_IFCONFIG_BROADCAST_PLUS
//config:	bool "Set the broadcast automatically"
//config:	default y
//config:	depends on IFCONFIG
//config:	help
//config:	Setting this will make ifconfig attempt to find the broadcast
//config:	automatically if the value '+' is used.

//applet:IF_IFCONFIG(APPLET(ifconfig, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_IFCONFIG) += ifconfig.o interface.o

//usage:#define ifconfig_trivial_usage
//usage:	IF_FEATURE_IFCONFIG_STATUS("[-a]") " interface [address]"
//usage:#define ifconfig_full_usage "\n\n"
//usage:       "Configure a network interface\n"
//usage:     "\n"
//usage:	IF_FEATURE_IPV6(
//usage:       "	[add ADDRESS[/PREFIXLEN]]\n")
//usage:	IF_FEATURE_IPV6(
//usage:       "	[del ADDRESS[/PREFIXLEN]]\n")
//usage:       "	[[-]broadcast [ADDRESS]] [[-]pointopoint [ADDRESS]]\n"
//usage:       "	[netmask ADDRESS] [dstaddr ADDRESS]\n"
//usage:	IF_FEATURE_IFCONFIG_SLIP(
//usage:       "	[outfill NN] [keepalive NN]\n")
//usage:       "	" IF_FEATURE_IFCONFIG_HW("[hw ether" IF_FEATURE_HWIB("|infiniband")" ADDRESS] ") "[metric NN] [mtu NN]\n"
//usage:       "	[[-]trailers] [[-]arp] [[-]allmulti]\n"
//usage:       "	[multicast] [[-]promisc] [txqueuelen NN] [[-]dynamic]\n"
//usage:	IF_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ(
//usage:       "	[mem_start NN] [io_addr NN] [irq NN]\n")
//usage:       "	[up|down] ..."

#include "libbb.h"
#include "inet_common.h"
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#ifdef HAVE_NET_ETHERNET_H
# include <net/ethernet.h>
#endif

#if ENABLE_FEATURE_IFCONFIG_SLIP
# include <linux/if_slip.h>
#endif

/* I don't know if this is needed for busybox or not.  Anyone? */
#define QUESTIONABLE_ALIAS_CASE


/* Defines for glibc2.0 users. */
#ifndef SIOCSIFTXQLEN
# define SIOCSIFTXQLEN      0x8943
# define SIOCGIFTXQLEN      0x8942
#endif

/* ifr_qlen is ifru_ivalue, but it isn't present in 2.0 kernel headers */
#ifndef ifr_qlen
# define ifr_qlen        ifr_ifru.ifru_mtu
#endif

#ifndef IFF_DYNAMIC
# define IFF_DYNAMIC     0x8000	/* dialup device with changing addresses */
#endif

#if ENABLE_FEATURE_IPV6
struct in6_ifreq {
	struct in6_addr ifr6_addr;
	uint32_t ifr6_prefixlen;
	int ifr6_ifindex;
};
#endif

/*
 * Here are the bit masks for the "flags" member of struct options below.
 * N_ signifies no arg prefix; M_ signifies arg prefixed by '-'.
 * CLR clears the flag; SET sets the flag; ARG signifies (optional) arg.
 */
#define N_CLR            0x01
#define M_CLR            0x02
#define N_SET            0x04
#define M_SET            0x08
#define N_ARG            0x10
#define M_ARG            0x20

#define M_MASK           (M_CLR | M_SET | M_ARG)
#define N_MASK           (N_CLR | N_SET | N_ARG)
#define SET_MASK         (N_SET | M_SET)
#define CLR_MASK         (N_CLR | M_CLR)
#define SET_CLR_MASK     (SET_MASK | CLR_MASK)
#define ARG_MASK         (M_ARG | N_ARG)

/*
 * Here are the bit masks for the "arg_flags" member of struct options below.
 */

/*
 * cast type:
 *   00 int
 *   01 char *
 *   02 HOST_COPY in_ether
 *   03 HOST_COPY INET_resolve
 */
#define A_CAST_TYPE      0x03
/*
 * map type:
 *   00 not a map type (mem_start, io_addr, irq)
 *   04 memstart (unsigned long)
 *   08 io_addr  (unsigned short)
 *   0C irq      (unsigned char)
 */
#define A_MAP_TYPE       0x0C
#define A_ARG_REQ        0x10	/* Set if an arg is required. */
#define A_NETMASK        0x20	/* Set if netmask (check for multiple sets). */
#define A_SET_AFTER      0x40	/* Set a flag at the end. */
#define A_COLON_CHK      0x80	/* Is this needed?  See below. */
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
#define A_HOSTNAME      0x100	/* Set if it is ip addr. */
#define A_BROADCAST     0x200	/* Set if it is broadcast addr. */
#else
#define A_HOSTNAME          0
#define A_BROADCAST         0
#endif

/*
 * These defines are for dealing with the A_CAST_TYPE field.
 */
#define A_CAST_CHAR_PTR  0x01
#define A_CAST_RESOLVE   0x01
#define A_CAST_HOST_COPY 0x02
#define A_CAST_HOST_COPY_IN_ETHER    A_CAST_HOST_COPY
#define A_CAST_HOST_COPY_RESOLVE     (A_CAST_HOST_COPY | A_CAST_RESOLVE)

/*
 * These defines are for dealing with the A_MAP_TYPE field.
 */
#define A_MAP_ULONG      0x04	/* memstart */
#define A_MAP_USHORT     0x08	/* io_addr */
#define A_MAP_UCHAR      0x0C	/* irq */

/*
 * Define the bit masks signifying which operations to perform for each arg.
 */

#define ARG_METRIC       (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_MTU          (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_TXQUEUELEN   (A_ARG_REQ /*| A_CAST_INT*/)
#define ARG_MEM_START    (A_ARG_REQ | A_MAP_ULONG)
#define ARG_IO_ADDR      (A_ARG_REQ | A_MAP_ULONG)
#define ARG_IRQ          (A_ARG_REQ | A_MAP_UCHAR)
#define ARG_DSTADDR      (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE)
#define ARG_NETMASK      (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_NETMASK)
#define ARG_BROADCAST    (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER | A_BROADCAST)
#define ARG_HW           (A_ARG_REQ | A_CAST_HOST_COPY_IN_ETHER)
#define ARG_POINTOPOINT  (A_ARG_REQ | A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER)
#define ARG_KEEPALIVE    (A_ARG_REQ | A_CAST_CHAR_PTR)
#define ARG_OUTFILL      (A_ARG_REQ | A_CAST_CHAR_PTR)
#define ARG_HOSTNAME     (A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER | A_COLON_CHK | A_HOSTNAME)
#define ARG_ADD_DEL      (A_CAST_HOST_COPY_RESOLVE | A_SET_AFTER)


struct arg1opt {
	const char *name;
	unsigned short selector;
	unsigned short ifr_offset;
};

struct options {
	const char *name;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	const unsigned int flags:6;
	const unsigned int arg_flags:10;
#else
	const unsigned char flags;
	const unsigned char arg_flags;
#endif
	const unsigned short selector;
};

#define ifreq_offsetof(x)  offsetof(struct ifreq, x)

/*
 * Set up the tables.  Warning!  They must have corresponding order!
 */

static const struct arg1opt Arg1Opt[] = {
	{ "SIFMETRIC",  SIOCSIFMETRIC,  ifreq_offsetof(ifr_metric) },
	{ "SIFMTU",     SIOCSIFMTU,     ifreq_offsetof(ifr_mtu) },
	{ "SIFTXQLEN",  SIOCSIFTXQLEN,  ifreq_offsetof(ifr_qlen) },
	{ "SIFDSTADDR", SIOCSIFDSTADDR, ifreq_offsetof(ifr_dstaddr) },
	{ "SIFNETMASK", SIOCSIFNETMASK, ifreq_offsetof(ifr_netmask) },
	{ "SIFBRDADDR", SIOCSIFBRDADDR, ifreq_offsetof(ifr_broadaddr) },
#if ENABLE_FEATURE_IFCONFIG_HW
	{ "SIFHWADDR",  SIOCSIFHWADDR,  ifreq_offsetof(ifr_hwaddr) },
#endif
	{ "SIFDSTADDR", SIOCSIFDSTADDR, ifreq_offsetof(ifr_dstaddr) },
#ifdef SIOCSKEEPALIVE
	{ "SKEEPALIVE", SIOCSKEEPALIVE, ifreq_offsetof(ifr_data) },
#endif
#ifdef SIOCSOUTFILL
	{ "SOUTFILL",   SIOCSOUTFILL,   ifreq_offsetof(ifr_data) },
#endif
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
	{ "SIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.mem_start) },
	{ "SIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.base_addr) },
	{ "SIFMAP",     SIOCSIFMAP,     ifreq_offsetof(ifr_map.irq) },
#endif
#if ENABLE_FEATURE_IPV6
	{ "SIFADDR",    SIOCSIFADDR,    ifreq_offsetof(ifr_addr) }, /* IPv6 version ignores the offset */
	{ "DIFADDR",    SIOCDIFADDR,    ifreq_offsetof(ifr_addr) }, /* IPv6 version ignores the offset */
#endif
	/* Last entry is for unmatched (assumed to be hostname/address) arg. */
	{ "SIFADDR",    SIOCSIFADDR,    ifreq_offsetof(ifr_addr) },
};

static const struct options OptArray[] = {
	{ "metric",      N_ARG,         ARG_METRIC,      0 },
	{ "mtu",         N_ARG,         ARG_MTU,         0 },
	{ "txqueuelen",  N_ARG,         ARG_TXQUEUELEN,  0 },
	{ "dstaddr",     N_ARG,         ARG_DSTADDR,     0 },
	{ "netmask",     N_ARG,         ARG_NETMASK,     0 },
	{ "broadcast",   N_ARG | M_CLR, ARG_BROADCAST,   IFF_BROADCAST },
#if ENABLE_FEATURE_IFCONFIG_HW
	{ "hw",          N_ARG,         ARG_HW,          0 },
#endif
	{ "pointopoint", N_ARG | M_CLR, ARG_POINTOPOINT, IFF_POINTOPOINT },
#ifdef SIOCSKEEPALIVE
	{ "keepalive",   N_ARG,         ARG_KEEPALIVE,   0 },
#endif
#ifdef SIOCSOUTFILL
	{ "outfill",     N_ARG,         ARG_OUTFILL,     0 },
#endif
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
	{ "mem_start",   N_ARG,         ARG_MEM_START,   0 },
	{ "io_addr",     N_ARG,         ARG_IO_ADDR,     0 },
	{ "irq",         N_ARG,         ARG_IRQ,         0 },
#endif
#if ENABLE_FEATURE_IPV6
	{ "add",         N_ARG,         ARG_ADD_DEL,     0 },
	{ "del",         N_ARG,         ARG_ADD_DEL,     0 },
#endif
	{ "arp",         N_CLR | M_SET, 0,               IFF_NOARP },
	{ "trailers",    N_CLR | M_SET, 0,               IFF_NOTRAILERS },
	{ "promisc",     N_SET | M_CLR, 0,               IFF_PROMISC },
	{ "multicast",   N_SET | M_CLR, 0,               IFF_MULTICAST },
	{ "allmulti",    N_SET | M_CLR, 0,               IFF_ALLMULTI },
	{ "dynamic",     N_SET | M_CLR, 0,               IFF_DYNAMIC },
	{ "up",          N_SET,         0,               (IFF_UP | IFF_RUNNING) },
	{ "down",        N_CLR,         0,               IFF_UP },
	{ NULL,          0,             ARG_HOSTNAME,    (IFF_UP | IFF_RUNNING) }
};

int ifconfig_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ifconfig_main(int argc UNUSED_PARAM, char **argv)
{
	struct ifreq ifr;
	struct sockaddr_in sai;
#if ENABLE_FEATURE_IFCONFIG_HW
	struct sockaddr sa;
#endif
	const struct arg1opt *a1op;
	const struct options *op;
	int sockfd;			/* socket fd we use to manipulate stuff with */
	int selector;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	unsigned int mask;
	unsigned int did_flags;
	unsigned int sai_hostname, sai_netmask;
#else
	unsigned char mask;
	unsigned char did_flags;
#endif
	char *p;
	/*char host[128];*/
	const char *host = NULL; /* make gcc happy */
	IF_FEATURE_IFCONFIG_STATUS(char *show_all_param;)

	did_flags = 0;
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
	sai_hostname = 0;
	sai_netmask = 0;
#endif

	/* skip argv[0] */
	++argv;

#if ENABLE_FEATURE_IFCONFIG_STATUS
	show_all_param = NULL;
	if (argv[0] && argv[0][0] == '-' && argv[0][1] == 'a' && !argv[0][2]) {
		++argv;
		show_all_param = IFNAME_SHOW_DOWNED_TOO;
	}
#endif

	if (!argv[0] || !argv[1]) { /* one or no args */
#if ENABLE_FEATURE_IFCONFIG_STATUS
		return display_interfaces(argv[0] ? argv[0] : show_all_param);
#else
		bb_error_msg_and_die("no support for status display");
#endif
	}

	/* Create a channel to the NET kernel. */
	sockfd = xsocket(AF_INET, SOCK_DGRAM, 0);

	/* get interface name */
	strncpy_IFNAMSIZ(ifr.ifr_name, *argv);

	/* Process the remaining arguments. */
	while (*++argv != NULL) {
		p = *argv;
		mask = N_MASK;
		if (*p == '-') {	/* If the arg starts with '-'... */
			++p;		/*    advance past it and */
			mask = M_MASK;	/*    set the appropriate mask. */
		}
		for (op = OptArray; op->name; op++) {	/* Find table entry. */
			if (strcmp(p, op->name) == 0) {	/* If name matches... */
				mask &= op->flags;
				if (mask)	/* set the mask and go. */
					goto FOUND_ARG;
				/* If we get here, there was a valid arg with an */
				/* invalid '-' prefix. */
				bb_error_msg_and_die("bad: '%s'", p-1);
			}
		}

		/* We fell through, so treat as possible hostname. */
		a1op = Arg1Opt + ARRAY_SIZE(Arg1Opt) - 1;
		mask = op->arg_flags;
		goto HOSTNAME;

 FOUND_ARG:
		if (mask & ARG_MASK) {
			mask = op->arg_flags;
			if (mask & A_NETMASK & did_flags)
				bb_show_usage();
			a1op = Arg1Opt + (op - OptArray);
			if (*++argv == NULL) {
				if (mask & A_ARG_REQ)
					bb_show_usage();
				--argv;
				mask &= A_SET_AFTER;	/* just for broadcast */
			} else {	/* got an arg so process it */
 HOSTNAME:
				did_flags |= (mask & (A_NETMASK|A_HOSTNAME));
				if (mask & A_CAST_HOST_COPY) {
#if ENABLE_FEATURE_IFCONFIG_HW
					if (mask & A_CAST_RESOLVE) {
#endif
						host = *argv;
						if (strcmp(host, "inet") == 0)
							continue; /* compat stuff */
						sai.sin_family = AF_INET;
						sai.sin_port = 0;
						if (strcmp(host, "default") == 0) {
							/* Default is special, meaning 0.0.0.0. */
							sai.sin_addr.s_addr = INADDR_ANY;
						}
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
						else if ((host[0] == '+' && !host[1])
						 && (mask & A_BROADCAST)
						 && (did_flags & (A_NETMASK|A_HOSTNAME)) == (A_NETMASK|A_HOSTNAME)
						) {
							/* + is special, meaning broadcast is derived. */
							sai.sin_addr.s_addr = (~sai_netmask) | (sai_hostname & sai_netmask);
						}
#endif
						else {
							len_and_sockaddr *lsa;
#if ENABLE_FEATURE_IPV6
							char *prefix;
							int prefix_len = 0;
							prefix = strchr(host, '/');
							if (prefix) {
								prefix_len = xatou_range(prefix + 1, 0, 128);
								*prefix = '\0';
							}
 resolve:
#endif
							lsa = xhost2sockaddr(host, 0);
#if ENABLE_FEATURE_IPV6
							if (lsa->u.sa.sa_family != AF_INET6 && prefix) {
/* TODO: we do not support "ifconfig eth0 up 1.2.3.4/17".
 * For now, just make it fail instead of silently ignoring "/17" part:
 */
								*prefix = '/';
								goto resolve;
							}
							if (lsa->u.sa.sa_family == AF_INET6) {
								int sockfd6;
								struct in6_ifreq ifr6;

								sockfd6 = xsocket(AF_INET6, SOCK_DGRAM, 0);
								xioctl(sockfd6, SIOCGIFINDEX, &ifr);
								ifr6.ifr6_ifindex = ifr.ifr_ifindex;
								ifr6.ifr6_prefixlen = prefix_len;
								memcpy(&ifr6.ifr6_addr,
										&lsa->u.sin6.sin6_addr,
										sizeof(struct in6_addr));
								ioctl_or_perror_and_die(sockfd6, a1op->selector, &ifr6, "SIOC%s", a1op->name);
								if (ENABLE_FEATURE_CLEAN_UP)
									free(lsa);
								continue;
							}
#endif
							sai.sin_addr = lsa->u.sin.sin_addr;
							if (ENABLE_FEATURE_CLEAN_UP)
								free(lsa);
						}
#if ENABLE_FEATURE_IFCONFIG_BROADCAST_PLUS
						if (mask & A_HOSTNAME)
							sai_hostname = sai.sin_addr.s_addr;
						if (mask & A_NETMASK)
							sai_netmask = sai.sin_addr.s_addr;
#endif
						p = (char *) &sai;
#if ENABLE_FEATURE_IFCONFIG_HW
					} else {	/* A_CAST_HOST_COPY_IN_ETHER */
						/* This is the "hw" arg case. */
						smalluint hw_class = index_in_substrings("ether\0"
								IF_FEATURE_HWIB("infiniband\0"), *argv) + 1;
						if (!hw_class || !*++argv)
							bb_show_usage();
						host = *argv;
						if (hw_class == 1 ? in_ether(host, &sa) : in_ib(host, &sa))
							bb_error_msg_and_die("invalid hw-addr %s", host);
						p = (char *) &sa;
					}
#endif
					memcpy( ((char *)&ifr) + a1op->ifr_offset,
						p, sizeof(struct sockaddr));
				} else {
					/* FIXME: error check?? */
					unsigned long i = strtoul(*argv, NULL, 0);
					p = ((char *)&ifr) + a1op->ifr_offset;
#if ENABLE_FEATURE_IFCONFIG_MEMSTART_IOADDR_IRQ
					if (mask & A_MAP_TYPE) {
						xioctl(sockfd, SIOCGIFMAP, &ifr);
						if ((mask & A_MAP_UCHAR) == A_MAP_UCHAR)
							*(unsigned char *) p = i;
						else if (mask & A_MAP_USHORT)
							*(unsigned short *) p = i;
						else
							*(unsigned long *) p = i;
					} else
#endif
					if (mask & A_CAST_CHAR_PTR)
						*(caddr_t *) p = (caddr_t) i;
					else	/* A_CAST_INT */
						*(int *) p = i;
				}

				ioctl_or_perror_and_die(sockfd, a1op->selector, &ifr, "SIOC%s", a1op->name);
#ifdef QUESTIONABLE_ALIAS_CASE
				if (mask & A_COLON_CHK) {
					/*
					 * Don't do the set_flag() if the address is an alias with
					 * a '-' at the end, since it's deleted already! - Roman
					 *
					 * Should really use regex.h here, not sure though how well
					 * it'll go with the cross-platform support etc.
					 */
					char *ptr;
					short int found_colon = 0;
					for (ptr = ifr.ifr_name; *ptr; ptr++)
						if (*ptr == ':')
							found_colon++;
					if (found_colon && ptr[-1] == '-')
						continue;
				}
#endif
			}
			if (!(mask & A_SET_AFTER))
				continue;
			mask = N_SET;
		} /* if (mask & ARG_MASK) */

		xioctl(sockfd, SIOCGIFFLAGS, &ifr);
		selector = op->selector;
		if (mask & SET_MASK)
			ifr.ifr_flags |= selector;
		else
			ifr.ifr_flags &= ~selector;
		xioctl(sockfd, SIOCSIFFLAGS, &ifr);
	} /* while () */

	if (ENABLE_FEATURE_CLEAN_UP)
		close(sockfd);
	return 0;
}
