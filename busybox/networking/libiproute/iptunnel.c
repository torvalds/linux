/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *
 * Rani Assaf <rani@magic.metawire.com> 980929: resolve addresses
 * Rani Assaf <rani@magic.metawire.com> 980930: do not allow key for ipip/sit
 * Phil Karn <karn@ka9q.ampr.org>       990408: "pmtudisc" flag
 */
#include <netinet/ip.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <asm/types.h>

#ifndef __constant_htons
#define __constant_htons htons
#endif

// FYI: #define SIOCDEVPRIVATE 0x89F0

/* From linux/if_tunnel.h. #including it proved troublesome
 * (redefiniton errors due to name collisions in linux/ and net[inet]/) */
#define SIOCGETTUNNEL   (SIOCDEVPRIVATE + 0)
#define SIOCADDTUNNEL   (SIOCDEVPRIVATE + 1)
#define SIOCDELTUNNEL   (SIOCDEVPRIVATE + 2)
#define SIOCCHGTUNNEL   (SIOCDEVPRIVATE + 3)
//#define SIOCGETPRL      (SIOCDEVPRIVATE + 4)
//#define SIOCADDPRL      (SIOCDEVPRIVATE + 5)
//#define SIOCDELPRL      (SIOCDEVPRIVATE + 6)
//#define SIOCCHGPRL      (SIOCDEVPRIVATE + 7)
#define GRE_CSUM        __constant_htons(0x8000)
//#define GRE_ROUTING     __constant_htons(0x4000)
#define GRE_KEY         __constant_htons(0x2000)
#define GRE_SEQ         __constant_htons(0x1000)
//#define GRE_STRICT      __constant_htons(0x0800)
//#define GRE_REC         __constant_htons(0x0700)
//#define GRE_FLAGS       __constant_htons(0x00F8)
//#define GRE_VERSION     __constant_htons(0x0007)
struct ip_tunnel_parm {
	char            name[IFNAMSIZ];
	int             link;
	uint16_t        i_flags;
	uint16_t        o_flags;
	uint32_t        i_key;
	uint32_t        o_key;
	struct iphdr    iph;
};
/* SIT-mode i_flags */
//#define SIT_ISATAP 0x0001
//struct ip_tunnel_prl {
//	uint32_t          addr;
//	uint16_t          flags;
//	uint16_t          __reserved;
//	uint32_t          datalen;
//	uint32_t          __reserved2;
//	/* data follows */
//};
///* PRL flags */
//#define PRL_DEFAULT 0x0001

#include "ip_common.h"  /* #include "libbb.h" is inside */
#include "rt_names.h"
#include "utils.h"


/* Dies on error */
static int do_ioctl_get_ifindex(char *dev)
{
	struct ifreq ifr;
	int fd;

	strncpy_IFNAMSIZ(ifr.ifr_name, dev);
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
	xioctl(fd, SIOCGIFINDEX, &ifr);
	close(fd);
	return ifr.ifr_ifindex;
}

static int do_ioctl_get_iftype(char *dev)
{
	struct ifreq ifr;
	int fd;
	int err;

	strncpy_IFNAMSIZ(ifr.ifr_name, dev);
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
	err = ioctl_or_warn(fd, SIOCGIFHWADDR, &ifr);
	close(fd);
	return err ? -1 : ifr.ifr_addr.sa_family;
}

static char *do_ioctl_get_ifname(int idx)
{
	struct ifreq ifr;
	int fd;
	int err;

	ifr.ifr_ifindex = idx;
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
	err = ioctl_or_warn(fd, SIOCGIFNAME, &ifr);
	close(fd);
	return err ? NULL : xstrndup(ifr.ifr_name, sizeof(ifr.ifr_name));
}

static int do_get_ioctl(const char *basedev, struct ip_tunnel_parm *p)
{
	struct ifreq ifr;
	int fd;
	int err;

	strncpy_IFNAMSIZ(ifr.ifr_name, basedev);
	ifr.ifr_ifru.ifru_data = (void*)p;
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
	err = ioctl_or_warn(fd, SIOCGETTUNNEL, &ifr);
	close(fd);
	return err;
}

/* Dies on error, otherwise returns 0 */
static int do_add_ioctl(int cmd, const char *basedev, struct ip_tunnel_parm *p)
{
	struct ifreq ifr;
	int fd;

	if (cmd == SIOCCHGTUNNEL && p->name[0]) {
		strncpy_IFNAMSIZ(ifr.ifr_name, p->name);
	} else {
		strncpy_IFNAMSIZ(ifr.ifr_name, basedev);
	}
	ifr.ifr_ifru.ifru_data = (void*)p;
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
#if ENABLE_IOCTL_HEX2STR_ERROR
	/* #define magic will turn ioctl# into string */
	if (cmd == SIOCCHGTUNNEL)
		xioctl(fd, SIOCCHGTUNNEL, &ifr);
	else
		xioctl(fd, SIOCADDTUNNEL, &ifr);
#else
	xioctl(fd, cmd, &ifr);
#endif
	close(fd);
	return 0;
}

/* Dies on error, otherwise returns 0 */
static int do_del_ioctl(const char *basedev, struct ip_tunnel_parm *p)
{
	struct ifreq ifr;
	int fd;

	if (p->name[0]) {
		strncpy_IFNAMSIZ(ifr.ifr_name, p->name);
	} else {
		strncpy_IFNAMSIZ(ifr.ifr_name, basedev);
	}
	ifr.ifr_ifru.ifru_data = (void*)p;
	fd = xsocket(AF_INET, SOCK_DGRAM, 0);
	xioctl(fd, SIOCDELTUNNEL, &ifr);
	close(fd);
	return 0;
}

/* Dies on error */
static void parse_args(char **argv, int cmd, struct ip_tunnel_parm *p)
{
	static const char keywords[] ALIGN1 =
		"mode\0""ipip\0""ip/ip\0""gre\0""gre/ip\0""sit\0""ipv6/ip\0"
		"key\0""ikey\0""okey\0""seq\0""iseq\0""oseq\0"
		"csum\0""icsum\0""ocsum\0""nopmtudisc\0""pmtudisc\0"
		"remote\0""any\0""local\0""dev\0"
		"ttl\0""inherit\0""tos\0""dsfield\0"
		"name\0";
	enum {
		ARG_mode, ARG_ipip, ARG_ip_ip, ARG_gre, ARG_gre_ip, ARG_sit, ARG_ip6_ip,
		ARG_key, ARG_ikey, ARG_okey, ARG_seq, ARG_iseq, ARG_oseq,
		ARG_csum, ARG_icsum, ARG_ocsum, ARG_nopmtudisc, ARG_pmtudisc,
		ARG_remote, ARG_any, ARG_local, ARG_dev,
		ARG_ttl, ARG_inherit, ARG_tos, ARG_dsfield,
		ARG_name
	};
	int count = 0;
	char medium[IFNAMSIZ];
	int key;

	memset(p, 0, sizeof(*p));
	medium[0] = '\0';

	p->iph.version = 4;
	p->iph.ihl = 5;
#ifndef IP_DF
#define IP_DF 0x4000  /* Flag: "Don't Fragment" */
#endif
	p->iph.frag_off = htons(IP_DF);

	while (*argv) {
		key = index_in_strings(keywords, *argv);
		if (key == ARG_mode) {
			NEXT_ARG();
			key = index_in_strings(keywords, *argv);
			if (key == ARG_ipip ||
			    key == ARG_ip_ip
			) {
				if (p->iph.protocol && p->iph.protocol != IPPROTO_IPIP) {
					bb_error_msg_and_die("%s tunnel mode", "you managed to ask for more than one");
				}
				p->iph.protocol = IPPROTO_IPIP;
			} else if (key == ARG_gre ||
				   key == ARG_gre_ip
			) {
				if (p->iph.protocol && p->iph.protocol != IPPROTO_GRE) {
					bb_error_msg_and_die("%s tunnel mode", "you managed to ask for more than one");
				}
				p->iph.protocol = IPPROTO_GRE;
			} else if (key == ARG_sit ||
				   key == ARG_ip6_ip
			) {
				if (p->iph.protocol && p->iph.protocol != IPPROTO_IPV6) {
					bb_error_msg_and_die("%s tunnel mode", "you managed to ask for more than one");
				}
				p->iph.protocol = IPPROTO_IPV6;
			} else {
				bb_error_msg_and_die("%s tunnel mode", "can't guess");
			}
		} else if (key == ARG_key) {
			unsigned uval;
			NEXT_ARG();
			p->i_flags |= GRE_KEY;
			p->o_flags |= GRE_KEY;
			if (strchr(*argv, '.'))
				p->i_key = p->o_key = get_addr32(*argv);
			else {
				uval = get_unsigned(*argv, "key");
				p->i_key = p->o_key = htonl(uval);
			}
		} else if (key == ARG_ikey) {
			unsigned uval;
			NEXT_ARG();
			p->i_flags |= GRE_KEY;
			if (strchr(*argv, '.'))
				p->o_key = get_addr32(*argv);
			else {
				uval = get_unsigned(*argv, "ikey");
				p->i_key = htonl(uval);
			}
		} else if (key == ARG_okey) {
			unsigned uval;
			NEXT_ARG();
			p->o_flags |= GRE_KEY;
			if (strchr(*argv, '.'))
				p->o_key = get_addr32(*argv);
			else {
				uval = get_unsigned(*argv, "okey");
				p->o_key = htonl(uval);
			}
		} else if (key == ARG_seq) {
			p->i_flags |= GRE_SEQ;
			p->o_flags |= GRE_SEQ;
		} else if (key == ARG_iseq) {
			p->i_flags |= GRE_SEQ;
		} else if (key == ARG_oseq) {
			p->o_flags |= GRE_SEQ;
		} else if (key == ARG_csum) {
			p->i_flags |= GRE_CSUM;
			p->o_flags |= GRE_CSUM;
		} else if (key == ARG_icsum) {
			p->i_flags |= GRE_CSUM;
		} else if (key == ARG_ocsum) {
			p->o_flags |= GRE_CSUM;
		} else if (key == ARG_nopmtudisc) {
			p->iph.frag_off = 0;
		} else if (key == ARG_pmtudisc) {
			p->iph.frag_off = htons(IP_DF);
		} else if (key == ARG_remote) {
			NEXT_ARG();
			key = index_in_strings(keywords, *argv);
			if (key != ARG_any)
				p->iph.daddr = get_addr32(*argv);
		} else if (key == ARG_local) {
			NEXT_ARG();
			key = index_in_strings(keywords, *argv);
			if (key != ARG_any)
				p->iph.saddr = get_addr32(*argv);
		} else if (key == ARG_dev) {
			NEXT_ARG();
			strncpy_IFNAMSIZ(medium, *argv);
		} else if (key == ARG_ttl) {
			unsigned uval;
			NEXT_ARG();
			key = index_in_strings(keywords, *argv);
			if (key != ARG_inherit) {
				uval = get_unsigned(*argv, "TTL");
				if (uval > 255)
					invarg_1_to_2(*argv, "TTL");
				p->iph.ttl = uval;
			}
		} else if (key == ARG_tos ||
			   key == ARG_dsfield
		) {
			uint32_t uval;
			NEXT_ARG();
			key = index_in_strings(keywords, *argv);
			if (key != ARG_inherit) {
				if (rtnl_dsfield_a2n(&uval, *argv))
					invarg_1_to_2(*argv, "TOS");
				p->iph.tos = uval;
			} else
				p->iph.tos = 1;
		} else {
			if (key == ARG_name) {
				NEXT_ARG();
			}
			if (p->name[0])
				duparg2("name", *argv);
			strncpy_IFNAMSIZ(p->name, *argv);
			if (cmd == SIOCCHGTUNNEL && count == 0) {
				struct ip_tunnel_parm old_p;
				memset(&old_p, 0, sizeof(old_p));
				if (do_get_ioctl(*argv, &old_p))
					exit(EXIT_FAILURE);
				*p = old_p;
			}
		}
		count++;
		argv++;
	}

	if (p->iph.protocol == 0) {
		if (memcmp(p->name, "gre", 3) == 0)
			p->iph.protocol = IPPROTO_GRE;
		else if (memcmp(p->name, "ipip", 4) == 0)
			p->iph.protocol = IPPROTO_IPIP;
		else if (memcmp(p->name, "sit", 3) == 0)
			p->iph.protocol = IPPROTO_IPV6;
	}

	if (p->iph.protocol == IPPROTO_IPIP || p->iph.protocol == IPPROTO_IPV6) {
		if ((p->i_flags & GRE_KEY) || (p->o_flags & GRE_KEY)) {
			bb_error_msg_and_die("keys are not allowed with ipip and sit");
		}
	}

	if (medium[0]) {
		p->link = do_ioctl_get_ifindex(medium);
	}

	if (p->i_key == 0 && IN_MULTICAST(ntohl(p->iph.daddr))) {
		p->i_key = p->iph.daddr;
		p->i_flags |= GRE_KEY;
	}
	if (p->o_key == 0 && IN_MULTICAST(ntohl(p->iph.daddr))) {
		p->o_key = p->iph.daddr;
		p->o_flags |= GRE_KEY;
	}
	if (IN_MULTICAST(ntohl(p->iph.daddr)) && !p->iph.saddr) {
		bb_error_msg_and_die("broadcast tunnel requires a source address");
	}
}

/* Return value becomes exitcode. It's okay to not return at all */
static int do_add(int cmd, char **argv)
{
	struct ip_tunnel_parm p;

	parse_args(argv, cmd, &p);

	if (p.iph.ttl && p.iph.frag_off == 0) {
		bb_error_msg_and_die("ttl != 0 and noptmudisc are incompatible");
	}

	switch (p.iph.protocol) {
	case IPPROTO_IPIP:
		return do_add_ioctl(cmd, "tunl0", &p);
	case IPPROTO_GRE:
		return do_add_ioctl(cmd, "gre0", &p);
	case IPPROTO_IPV6:
		return do_add_ioctl(cmd, "sit0", &p);
	default:
		bb_error_msg_and_die("can't determine tunnel mode (ipip, gre or sit)");
	}
}

/* Return value becomes exitcode. It's okay to not return at all */
static int do_del(char **argv)
{
	struct ip_tunnel_parm p;

	parse_args(argv, SIOCDELTUNNEL, &p);

	switch (p.iph.protocol) {
	case IPPROTO_IPIP:
		return do_del_ioctl("tunl0", &p);
	case IPPROTO_GRE:
		return do_del_ioctl("gre0", &p);
	case IPPROTO_IPV6:
		return do_del_ioctl("sit0", &p);
	default:
		return do_del_ioctl(p.name, &p);
	}
}

static void print_tunnel(struct ip_tunnel_parm *p)
{
	char s3[INET_ADDRSTRLEN];
	char s4[INET_ADDRSTRLEN];

	printf("%s: %s/ip  remote %s  local %s ",
		p->name,
		p->iph.protocol == IPPROTO_IPIP ? "ip" :
			p->iph.protocol == IPPROTO_GRE ? "gre" :
			p->iph.protocol == IPPROTO_IPV6 ? "ipv6" :
			"unknown",
		p->iph.daddr ? format_host(AF_INET, 4, &p->iph.daddr) : "any",
		p->iph.saddr ? format_host(AF_INET, 4, &p->iph.saddr) : "any"
	);
	if (p->link) {
		char *n = do_ioctl_get_ifname(p->link);
		if (n) {
			printf(" dev %s ", n);
			free(n);
		}
	}
	if (p->iph.ttl)
		printf(" ttl %d ", p->iph.ttl);
	else
		printf(" ttl inherit ");
	if (p->iph.tos) {
		printf(" tos");
		if (p->iph.tos & 1)
			printf(" inherit");
		if (p->iph.tos & ~1)
			printf("%c%s ", p->iph.tos & 1 ? '/' : ' ',
				rtnl_dsfield_n2a(p->iph.tos & ~1));
	}
	if (!(p->iph.frag_off & htons(IP_DF)))
		printf(" nopmtudisc");

	inet_ntop(AF_INET, &p->i_key, s3, sizeof(s3));
	inet_ntop(AF_INET, &p->o_key, s4, sizeof(s4));
	if ((p->i_flags & GRE_KEY) && (p->o_flags & GRE_KEY) && p->o_key == p->i_key)
		printf(" key %s", s3);
	else {
		if (p->i_flags & GRE_KEY)
			printf(" ikey %s ", s3);
		if (p->o_flags & GRE_KEY)
			printf(" okey %s ", s4);
	}

	if (p->i_flags & GRE_SEQ)
		printf("%c  Drop packets out of sequence.\n", _SL_);
	if (p->i_flags & GRE_CSUM)
		printf("%c  Checksum in received packet is required.", _SL_);
	if (p->o_flags & GRE_SEQ)
		printf("%c  Sequence packets on output.", _SL_);
	if (p->o_flags & GRE_CSUM)
		printf("%c  Checksum output packets.", _SL_);
}

static void do_tunnels_list(struct ip_tunnel_parm *p)
{
	char name[IFNAMSIZ];
	unsigned long rx_bytes, rx_packets, rx_errs, rx_drops,
		rx_fifo, rx_frame,
		tx_bytes, tx_packets, tx_errs, tx_drops,
		tx_fifo, tx_colls, tx_carrier, rx_multi;
	int type;
	struct ip_tunnel_parm p1;
	char buf[512];
	FILE *fp = fopen_or_warn("/proc/net/dev", "r");

	if (fp == NULL) {
		return;
	}
	/* skip headers */
	fgets(buf, sizeof(buf), fp);
	fgets(buf, sizeof(buf), fp);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		char *ptr;

		/*buf[sizeof(buf) - 1] = 0; - fgets is safe anyway */
		ptr = strchr(buf, ':');
		if (ptr == NULL ||
		    (*ptr++ = 0, sscanf(buf, "%s", name) != 1)
		) {
			bb_error_msg("wrong format of /proc/net/dev");
			return;
		}
		if (sscanf(ptr, "%lu%lu%lu%lu%lu%lu%lu%*d%lu%lu%lu%lu%lu%lu%lu",
			   &rx_bytes, &rx_packets, &rx_errs, &rx_drops,
			   &rx_fifo, &rx_frame, &rx_multi,
			   &tx_bytes, &tx_packets, &tx_errs, &tx_drops,
			   &tx_fifo, &tx_colls, &tx_carrier) != 14)
			continue;
		if (p->name[0] && strcmp(p->name, name))
			continue;
		type = do_ioctl_get_iftype(name);
		if (type == -1) {
			bb_error_msg("can't get type of [%s]", name);
			continue;
		}
		if (type != ARPHRD_TUNNEL && type != ARPHRD_IPGRE && type != ARPHRD_SIT)
			continue;
		memset(&p1, 0, sizeof(p1));
		if (do_get_ioctl(name, &p1))
			continue;
		if ((p->link && p1.link != p->link) ||
		    (p->name[0] && strcmp(p1.name, p->name)) ||
		    (p->iph.daddr && p1.iph.daddr != p->iph.daddr) ||
		    (p->iph.saddr && p1.iph.saddr != p->iph.saddr) ||
		    (p->i_key && p1.i_key != p->i_key)
		) {
			continue;
		}
		print_tunnel(&p1);
		bb_putchar('\n');
	}
}

/* Return value becomes exitcode. It's okay to not return at all */
static int do_show(char **argv)
{
	int err;
	struct ip_tunnel_parm p;

	parse_args(argv, SIOCGETTUNNEL, &p);

	switch (p.iph.protocol) {
	case IPPROTO_IPIP:
		err = do_get_ioctl(p.name[0] ? p.name : "tunl0", &p);
		break;
	case IPPROTO_GRE:
		err = do_get_ioctl(p.name[0] ? p.name : "gre0", &p);
		break;
	case IPPROTO_IPV6:
		err = do_get_ioctl(p.name[0] ? p.name : "sit0", &p);
		break;
	default:
		do_tunnels_list(&p);
		return 0;
	}
	if (err)
		return -1;

	print_tunnel(&p);
	bb_putchar('\n');
	return 0;
}

/* Return value becomes exitcode. It's okay to not return at all */
int FAST_FUNC do_iptunnel(char **argv)
{
	static const char keywords[] ALIGN1 =
		"add\0""change\0""delete\0""show\0""list\0""lst\0";
	enum { ARG_add = 0, ARG_change, ARG_del, ARG_show, ARG_list, ARG_lst };

	if (*argv) {
		int key = index_in_substrings(keywords, *argv);
		if (key < 0)
			invarg_1_to_2(*argv, applet_name);
		argv++;
		if (key == ARG_add)
			return do_add(SIOCADDTUNNEL, argv);
		if (key == ARG_change)
			return do_add(SIOCCHGTUNNEL, argv);
		if (key == ARG_del)
			return do_del(argv);
	}
	return do_show(argv);
}
