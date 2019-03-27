/* Licensed under the OpenIB.org BSD license (FreeBSD Variant) - See COPYING.md
 */

#include "config.h"
#include <net/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <infiniband/endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#if HAVE_WORKING_IF_H
#include <net/if.h>
#endif

#include <netlink/route/rtnl.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>
#include <netlink/route/neighbour.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <assert.h>

#if !HAVE_WORKING_IF_H
/* We need this decl from net/if.h but old systems do not let use co-include
   net/if.h and netlink/route/link.h */
extern unsigned int if_nametoindex(__const char *__ifname) __THROW;
#endif

/* for PFX */
#include "ibverbs.h"
#include <sys/param.h>

#include "neigh.h"

#ifndef HAVE_LIBNL1
#include <netlink/route/link/vlan.h>
#endif

static pthread_once_t device_neigh_alloc = PTHREAD_ONCE_INIT;
static struct nl_sock *zero_socket;

union sktaddr {
	struct sockaddr s;
	struct sockaddr_in s4;
	struct sockaddr_in6 s6;
};

struct skt {
	union sktaddr sktaddr;
	socklen_t len;
};

static int set_link_port(union sktaddr *s, __be16 port, int oif)
{
	switch (s->s.sa_family) {
	case AF_INET:
		s->s4.sin_port = port;
		break;
	case AF_INET6:
		s->s6.sin6_port = port;
		s->s6.sin6_scope_id = oif;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static bool cmp_address(const struct sockaddr *s1,
			const struct sockaddr *s2)
{
	if (s1->sa_family != s2->sa_family)
		return false;

	switch (s1->sa_family) {
	case AF_INET:
		return ((struct sockaddr_in *)s1)->sin_addr.s_addr ==
		       ((struct sockaddr_in *)s2)->sin_addr.s_addr;
	case AF_INET6:
		return !memcmp(
			((struct sockaddr_in6 *)s1)->sin6_addr.s6_addr,
			((struct sockaddr_in6 *)s2)->sin6_addr.s6_addr,
			sizeof(((struct sockaddr_in6 *)s1)->sin6_addr.s6_addr));
	default:
		return false;
	}
}

static int get_ifindex(const struct sockaddr *s)
{
	struct ifaddrs *ifaddr, *ifa;
	int name2index = -ENODEV;

	if (-1 == getifaddrs(&ifaddr))
		return errno;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		if (cmp_address(ifa->ifa_addr, s)) {
			name2index = if_nametoindex(ifa->ifa_name);
			break;
		}
	}

	freeifaddrs(ifaddr);

	return name2index;
}

static struct nl_addr *get_neigh_mac(struct get_neigh_handler *neigh_handler)
{
	struct rtnl_neigh *neigh;
	struct nl_addr *ll_addr = NULL;

	/* future optimization - if link local address - parse address and
	 * return mac now instead of doing so after the routing CB. This
	 * is of course referred to GIDs */
	neigh = rtnl_neigh_get(neigh_handler->neigh_cache,
			       neigh_handler->oif,
			       neigh_handler->dst);
	if (neigh == NULL)
		return NULL;

	ll_addr = rtnl_neigh_get_lladdr(neigh);
	if (NULL != ll_addr)
		ll_addr = nl_addr_clone(ll_addr);

	rtnl_neigh_put(neigh);
	return ll_addr;
}

static void get_neigh_cb_event(struct nl_object *obj, void *arg)
{
	struct get_neigh_handler *neigh_handler =
		(struct get_neigh_handler *)arg;
	/* assumed serilized callback (no parallel execution of function) */
	if (nl_object_match_filter(
		obj,
		(struct nl_object *)neigh_handler->filter_neigh)) {
		struct rtnl_neigh *neigh = (struct rtnl_neigh *)obj;
		/* check that we didn't set it already */
		if (neigh_handler->found_ll_addr == NULL) {
			if (rtnl_neigh_get_lladdr(neigh) == NULL)
				return;

			neigh_handler->found_ll_addr =
				nl_addr_clone(rtnl_neigh_get_lladdr(neigh));
		}
	}
}

static int get_neigh_cb(struct nl_msg *msg, void *arg)
{
	struct get_neigh_handler *neigh_handler =
		(struct get_neigh_handler *)arg;

	if (nl_msg_parse(msg, &get_neigh_cb_event, neigh_handler) < 0)
		errno = ENOMSG;

	return NL_OK;
}

static void set_neigh_filter(struct get_neigh_handler *neigh_handler,
			     struct rtnl_neigh *filter) {
	neigh_handler->filter_neigh = filter;
}

static struct rtnl_neigh *create_filter_neigh_for_dst(struct nl_addr *dst_addr,
						      int oif)
{
	struct rtnl_neigh *filter_neigh;

	filter_neigh = rtnl_neigh_alloc();
	if (filter_neigh == NULL)
		return NULL;

	rtnl_neigh_set_ifindex(filter_neigh, oif);
	rtnl_neigh_set_dst(filter_neigh, dst_addr);

	return filter_neigh;
}

#define PORT_DISCARD htobe16(9)
#define SEND_PAYLOAD "H"

static int create_socket(struct get_neigh_handler *neigh_handler,
			 struct skt *addr_dst, int *psock_fd)
{
	int err;
	struct skt addr_src;
	int sock_fd;

	memset(addr_dst, 0, sizeof(*addr_dst));
	memset(&addr_src, 0, sizeof(addr_src));
	addr_src.len = sizeof(addr_src.sktaddr);

	err = nl_addr_fill_sockaddr(neigh_handler->src,
				    &addr_src.sktaddr.s,
				    &addr_src.len);
	if (err) {
		errno = EADDRNOTAVAIL;
		return -1;
	}

	addr_dst->len = sizeof(addr_dst->sktaddr);
	err = nl_addr_fill_sockaddr(neigh_handler->dst,
				    &addr_dst->sktaddr.s,
				    &addr_dst->len);
	if (err) {
		errno = EADDRNOTAVAIL;
		return -1;
	}

	err = set_link_port(&addr_dst->sktaddr, PORT_DISCARD,
			    neigh_handler->oif);
	if (err)
		return -1;

	sock_fd = socket(addr_dst->sktaddr.s.sa_family,
			 SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock_fd == -1)
		return -1;
	err = bind(sock_fd, &addr_src.sktaddr.s, addr_src.len);
	if (err) {
		close(sock_fd);
		return -1;
	}

	*psock_fd = sock_fd;

	return 0;
}

#define NUM_OF_RETRIES 10
#define NUM_OF_TRIES ((NUM_OF_RETRIES) + 1)
#if NUM_OF_TRIES < 1
#error "neigh: invalid value of NUM_OF_RETRIES"
#endif
static int create_timer(struct get_neigh_handler *neigh_handler)
{
	int user_timeout = neigh_handler->timeout/NUM_OF_TRIES;
	struct timespec timeout = {
		.tv_sec = user_timeout / 1000,
		.tv_nsec = (user_timeout % 1000) * 1000000
	};
	struct itimerspec timer_time = {.it_value = timeout};
	int timer_fd;

	timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_fd == -1)
		return timer_fd;

	if (neigh_handler->timeout) {
		if (NUM_OF_TRIES <= 1)
			bzero(&timer_time.it_interval,
			      sizeof(timer_time.it_interval));
		else
			timer_time.it_interval = timeout;
		if (timerfd_settime(timer_fd, 0, &timer_time, NULL)) {
			close(timer_fd);
			return -1;
		}
	}

	return timer_fd;
}

#define UDP_SOCKET_MAX_SENDTO 100000ULL
static int try_send_to(int sock_fd, void *buff, size_t buf_size,
		       struct skt *addr_dst)
{
	uint64_t max_count = UDP_SOCKET_MAX_SENDTO;
	int err;

	do {
		err = sendto(sock_fd, buff, buf_size, 0,
			     &addr_dst->sktaddr.s,
			     addr_dst->len);
		if (err > 0)
			err = 0;
	} while (-1 == err && EADDRNOTAVAIL == errno && --max_count);

	return err;
}

static struct nl_addr *process_get_neigh_mac(
		struct get_neigh_handler *neigh_handler)
{
	int err;
	struct nl_addr *ll_addr = get_neigh_mac(neigh_handler);
	struct rtnl_neigh *neigh_filter;
	fd_set fdset;
	int sock_fd;
	int fd;
	int nfds;
	int timer_fd;
	int ret;
	struct skt addr_dst;
	char buff[sizeof(SEND_PAYLOAD)] = SEND_PAYLOAD;
	int retries = 0;

	if (NULL != ll_addr)
		return ll_addr;

	err = nl_socket_add_membership(neigh_handler->sock,
				       RTNLGRP_NEIGH);
	if (err < 0)
		return NULL;

	neigh_filter = create_filter_neigh_for_dst(neigh_handler->dst,
						   neigh_handler->oif);
	if (neigh_filter == NULL)
		return NULL;

	set_neigh_filter(neigh_handler, neigh_filter);

	nl_socket_disable_seq_check(neigh_handler->sock);
	nl_socket_modify_cb(neigh_handler->sock, NL_CB_VALID, NL_CB_CUSTOM,
			    &get_neigh_cb, neigh_handler);

	fd = nl_socket_get_fd(neigh_handler->sock);

	err = create_socket(neigh_handler, &addr_dst, &sock_fd);

	if (err)
		return NULL;

	err = try_send_to(sock_fd, buff, sizeof(buff), &addr_dst);
	if (err)
		goto close_socket;

	timer_fd = create_timer(neigh_handler);
	if (timer_fd < 0)
		goto close_socket;

	nfds = MAX(fd, timer_fd) + 1;

	while (1) {
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		FD_SET(timer_fd, &fdset);

		/* wait for an incoming message on the netlink socket */
		ret = select(nfds, &fdset, NULL, NULL, NULL);
		if (ret == -1) {
			goto select_err;
		} else if (ret) {
			if (FD_ISSET(fd, &fdset)) {
				nl_recvmsgs_default(neigh_handler->sock);
				if (neigh_handler->found_ll_addr)
					break;
			} else {
				nl_cache_refill(neigh_handler->sock,
						neigh_handler->neigh_cache);
				ll_addr = get_neigh_mac(neigh_handler);
				if (NULL != ll_addr) {
					break;
				} else if (FD_ISSET(timer_fd, &fdset) &&
					   retries < NUM_OF_RETRIES) {
					try_send_to(sock_fd, buff, sizeof(buff),
						    &addr_dst);
				}
			}

			if (FD_ISSET(timer_fd, &fdset)) {
				uint64_t read_val;
				ssize_t rc;

				rc =
				    read(timer_fd, &read_val, sizeof(read_val));
				assert(rc == sizeof(read_val));
				if (++retries >=  NUM_OF_TRIES) {
					if (!errno)
						errno = EDESTADDRREQ;
					break;
				}
			}
		}
	}
select_err:
	close(timer_fd);
close_socket:
	close(sock_fd);
	return ll_addr ? ll_addr : neigh_handler->found_ll_addr;
}

static int get_mcast_mac_ipv4(struct nl_addr *dst, struct nl_addr **ll_addr)
{
	uint8_t mac_addr[6] = {0x01, 0x00, 0x5E};
	uint32_t addr = be32toh(*(__be32 *)nl_addr_get_binary_addr(dst));

	mac_addr[5] = addr & 0xFF;
	addr >>= 8;
	mac_addr[4] = addr & 0xFF;
	addr >>= 8;
	mac_addr[3] = addr & 0x7F;

	*ll_addr = nl_addr_build(AF_LLC, mac_addr, sizeof(mac_addr));

	return *ll_addr == NULL ? -EINVAL : 0;
}

static int get_mcast_mac_ipv6(struct nl_addr *dst, struct nl_addr **ll_addr)
{
	uint8_t mac_addr[6] = {0x33, 0x33};

	memcpy(mac_addr + 2, (uint8_t *)nl_addr_get_binary_addr(dst) + 12, 4);

	*ll_addr = nl_addr_build(AF_LLC, mac_addr, sizeof(mac_addr));

	return *ll_addr == NULL ? -EINVAL : 0;
}

static int get_link_local_mac_ipv6(struct nl_addr *dst,
				   struct nl_addr **ll_addr)
{
	uint8_t mac_addr[6];

	memcpy(mac_addr + 3, (uint8_t *)nl_addr_get_binary_addr(dst) + 13, 3);
	memcpy(mac_addr, (uint8_t *)nl_addr_get_binary_addr(dst) + 8, 3);
	mac_addr[0] ^= 2;

	*ll_addr = nl_addr_build(AF_LLC, mac_addr, sizeof(mac_addr));
	return *ll_addr == NULL ? -EINVAL : 0;
}

static const struct encoded_l3_addr {
	short family;
	uint8_t prefix_bits;
	const uint8_t data[16];
	int (*getter)(struct nl_addr *dst, struct nl_addr **ll_addr);
} encoded_prefixes[] = {
	{.family = AF_INET,
	 .prefix_bits = 4,
	 .data = {0xe0},
	 .getter = &get_mcast_mac_ipv4},
	{.family = AF_INET6,
	 .prefix_bits = 8,
	 .data = {0xff},
	 .getter = &get_mcast_mac_ipv6},
	{.family = AF_INET6,
	 .prefix_bits = 64,
	 .data = {0xfe, 0x80},
	 .getter = get_link_local_mac_ipv6},
};

static int nl_addr_cmp_prefix_msb(void *addr1, int len1, void *addr2, int len2)
{
	int len = min(len1, len2);
	int bytes = len / 8;
	int d = memcmp(addr1, addr2, bytes);

	if (d == 0) {
		int mask = ((1UL << (len % 8)) - 1UL) << (8 - len);

		d = (((uint8_t *)addr1)[bytes] & mask) -
		    (((uint8_t *)addr2)[bytes] & mask);
	}

	return d;
}

static int handle_encoded_mac(struct nl_addr *dst, struct nl_addr **ll_addr)
{
	uint32_t family = nl_addr_get_family(dst);
	struct nl_addr *prefix = NULL;
	int i;
	int ret = 1;

	for (i = 0;
	     i < sizeof(encoded_prefixes)/sizeof(encoded_prefixes[0]) &&
	     ret; prefix = NULL, i++) {
		if (encoded_prefixes[i].family != family)
			continue;

		prefix = nl_addr_build(
		    family, (void *)encoded_prefixes[i].data,
		    min_t(size_t, encoded_prefixes[i].prefix_bits / 8 +
				      !!(encoded_prefixes[i].prefix_bits % 8),
			  sizeof(encoded_prefixes[i].data)));

		if (prefix == NULL)
			return -ENOMEM;
		nl_addr_set_prefixlen(prefix,
				      encoded_prefixes[i].prefix_bits);

		if (nl_addr_cmp_prefix_msb(nl_addr_get_binary_addr(dst),
					   nl_addr_get_prefixlen(dst),
					   nl_addr_get_binary_addr(prefix),
					   nl_addr_get_prefixlen(prefix)))
			continue;

		ret = encoded_prefixes[i].getter(dst, ll_addr);
		nl_addr_put(prefix);
	}

	return ret;
}

static void get_route_cb_parser(struct nl_object *obj, void *arg)
{
	struct get_neigh_handler *neigh_handler =
		(struct get_neigh_handler *)arg;

	struct rtnl_route *route = (struct rtnl_route *)obj;
	struct nl_addr *gateway = NULL;
	struct nl_addr *src = rtnl_route_get_pref_src(route);
	int oif;
	int type = rtnl_route_get_type(route);
	struct rtnl_link *link;

	struct rtnl_nexthop *nh = rtnl_route_nexthop_n(route, 0);

	if (nh != NULL)
		gateway = rtnl_route_nh_get_gateway(nh);
	oif = rtnl_route_nh_get_ifindex(nh);

	if (gateway) {
		nl_addr_put(neigh_handler->dst);
		neigh_handler->dst = nl_addr_clone(gateway);
	}

	if (RTN_BLACKHOLE == type ||
	    RTN_UNREACHABLE == type ||
	    RTN_PROHIBIT == type ||
	    RTN_THROW == type) {
		errno = ENETUNREACH;
		goto err;
	}

	if (!neigh_handler->src && src)
		neigh_handler->src = nl_addr_clone(src);

	if (neigh_handler->oif < 0 && oif > 0)
		neigh_handler->oif = oif;

	/* Link Local */
	if (RTN_LOCAL == type) {
		struct nl_addr *lladdr;

		link = rtnl_link_get(neigh_handler->link_cache,
				     neigh_handler->oif);

		if (link == NULL)
			goto err;

		lladdr = rtnl_link_get_addr(link);

		if (lladdr == NULL)
			goto err_link;

		neigh_handler->found_ll_addr = nl_addr_clone(lladdr);
		rtnl_link_put(link);
	} else {
		handle_encoded_mac(
			neigh_handler->dst,
			&neigh_handler->found_ll_addr);
	}

	return;

err_link:
	rtnl_link_put(link);
err:
	if (neigh_handler->src) {
		nl_addr_put(neigh_handler->src);
		neigh_handler->src = NULL;
	}
}

static int get_route_cb(struct nl_msg *msg, void *arg)
{
	struct get_neigh_handler *neigh_handler =
		(struct get_neigh_handler *)arg;
	int err;

	err = nl_msg_parse(msg, &get_route_cb_parser, neigh_handler);
	if (err < 0) {
		errno = ENOMSG;
		return err;
	}

	if (!neigh_handler->dst || !neigh_handler->src ||
	    neigh_handler->oif <= 0) {
		errno = EINVAL;
		return -1;
	}

	if (NULL != neigh_handler->found_ll_addr)
		goto found;

	neigh_handler->found_ll_addr =
		process_get_neigh_mac(neigh_handler);

found:
	return neigh_handler->found_ll_addr ? 0 : -1;
}

int neigh_get_oif_from_src(struct get_neigh_handler *neigh_handler)
{
	int oif = -ENODEV;
	struct addrinfo *src_info;
	int err;

	err = nl_addr_info(neigh_handler->src, &src_info);
	if (err) {
		if (!errno)
			errno = ENXIO;
		return oif;
	}

	oif = get_ifindex(src_info->ai_addr);
	if (oif <= 0)
		goto free;

free:
	freeaddrinfo(src_info);
	return oif;
}

static void alloc_zero_based_socket(void)
{
	zero_socket = nl_socket_alloc();
}

int neigh_init_resources(struct get_neigh_handler *neigh_handler, int timeout)
{
	int err;

	pthread_once(&device_neigh_alloc, &alloc_zero_based_socket);
	neigh_handler->sock = nl_socket_alloc();
	if (neigh_handler->sock == NULL) {
		errno = ENOMEM;
		return -1;
	}

	err = nl_connect(neigh_handler->sock, NETLINK_ROUTE);
	if (err < 0)
		goto free_socket;

	err = rtnl_link_alloc_cache(neigh_handler->sock, AF_UNSPEC,
				    &neigh_handler->link_cache);
	if (err) {
		err = -1;
		errno = ENOMEM;
		goto close_connection;
	}

	nl_cache_mngt_provide(neigh_handler->link_cache);

	err = rtnl_route_alloc_cache(neigh_handler->sock, AF_UNSPEC, 0,
				     &neigh_handler->route_cache);
	if (err) {
		err = -1;
		errno = ENOMEM;
		goto free_link_cache;
	}

	nl_cache_mngt_provide(neigh_handler->route_cache);

	err = rtnl_neigh_alloc_cache(neigh_handler->sock,
				     &neigh_handler->neigh_cache);
	if (err) {
		err = -ENOMEM;
		goto free_route_cache;
	}

	nl_cache_mngt_provide(neigh_handler->neigh_cache);

	/* init structure */
	neigh_handler->timeout = timeout;
	neigh_handler->oif = -1;
	neigh_handler->filter_neigh = NULL;
	neigh_handler->found_ll_addr = NULL;
	neigh_handler->dst = NULL;
	neigh_handler->src = NULL;
	neigh_handler->vid = -1;

	return 0;

free_route_cache:
	nl_cache_mngt_unprovide(neigh_handler->route_cache);
	nl_cache_free(neigh_handler->route_cache);
	neigh_handler->route_cache = NULL;
free_link_cache:
	nl_cache_mngt_unprovide(neigh_handler->link_cache);
	nl_cache_free(neigh_handler->link_cache);
	neigh_handler->link_cache = NULL;
close_connection:
	nl_close(neigh_handler->sock);
free_socket:
	nl_socket_free(neigh_handler->sock);
	neigh_handler->sock = NULL;
	return err;
}

uint16_t neigh_get_vlan_id_from_dev(struct get_neigh_handler *neigh_handler)
{
	struct rtnl_link *link;
	int vid = 0xffff;

	link = rtnl_link_get(neigh_handler->link_cache, neigh_handler->oif);
	if (link == NULL) {
		errno = EINVAL;
		return vid;
	}

	if (rtnl_link_is_vlan(link))
		vid = rtnl_link_vlan_get_id(link);
	rtnl_link_put(link);
	return vid >= 0 && vid <= 0xfff ? vid : 0xffff;
}

void neigh_set_vlan_id(struct get_neigh_handler *neigh_handler, uint16_t vid)
{
	if (vid <= 0xfff)
		neigh_handler->vid = vid;
}

int neigh_set_dst(struct get_neigh_handler *neigh_handler,
		  int family, void *buf, size_t size)
{
	neigh_handler->dst = nl_addr_build(family, buf, size);
	return neigh_handler->dst == NULL;
}

int neigh_set_src(struct get_neigh_handler *neigh_handler,
		  int family, void *buf, size_t size)
{
	neigh_handler->src = nl_addr_build(family, buf, size);
	return neigh_handler->src == NULL;
}

void neigh_set_oif(struct get_neigh_handler *neigh_handler, int oif)
{
	neigh_handler->oif = oif;
}

int neigh_get_ll(struct get_neigh_handler *neigh_handler, void *addr_buff,
		 int addr_size) {
	int neigh_len;

	if (neigh_handler->found_ll_addr == NULL)
		return -EINVAL;

	 neigh_len = nl_addr_get_len(neigh_handler->found_ll_addr);

	if (neigh_len > addr_size)
		return -EINVAL;

	memcpy(addr_buff, nl_addr_get_binary_addr(neigh_handler->found_ll_addr),
	       neigh_len);

	return neigh_len;
}

void neigh_free_resources(struct get_neigh_handler *neigh_handler)
{
	/* Should be released first because it's holding a reference to dst */
	if (neigh_handler->filter_neigh != NULL) {
		rtnl_neigh_put(neigh_handler->filter_neigh);
		neigh_handler->filter_neigh = NULL;
	}

	if (neigh_handler->src != NULL) {
		nl_addr_put(neigh_handler->src);
		neigh_handler->src = NULL;
	}

	if (neigh_handler->dst != NULL) {
		nl_addr_put(neigh_handler->dst);
		neigh_handler->dst = NULL;
	}

	if (neigh_handler->found_ll_addr != NULL) {
		nl_addr_put(neigh_handler->found_ll_addr);
		neigh_handler->found_ll_addr = NULL;
	}

	if (neigh_handler->neigh_cache != NULL) {
		nl_cache_mngt_unprovide(neigh_handler->neigh_cache);
		nl_cache_free(neigh_handler->neigh_cache);
		neigh_handler->neigh_cache = NULL;
	}

	if (neigh_handler->route_cache != NULL) {
		nl_cache_mngt_unprovide(neigh_handler->route_cache);
		nl_cache_free(neigh_handler->route_cache);
		neigh_handler->route_cache = NULL;
	}

	if (neigh_handler->link_cache != NULL) {
		nl_cache_mngt_unprovide(neigh_handler->link_cache);
		nl_cache_free(neigh_handler->link_cache);
		neigh_handler->link_cache = NULL;
	}

	if (neigh_handler->sock != NULL) {
		nl_close(neigh_handler->sock);
		nl_socket_free(neigh_handler->sock);
		neigh_handler->sock = NULL;
	}
}

int process_get_neigh(struct get_neigh_handler *neigh_handler)
{
	struct nl_msg *m;
	struct rtmsg rmsg = {
		.rtm_family = nl_addr_get_family(neigh_handler->dst),
		.rtm_dst_len = nl_addr_get_prefixlen(neigh_handler->dst),
	};
	int err;

	m = nlmsg_alloc_simple(RTM_GETROUTE, 0);

	if (m == NULL)
		return -ENOMEM;

	nlmsg_append(m, &rmsg, sizeof(rmsg), NLMSG_ALIGNTO);

	nla_put_addr(m, RTA_DST, neigh_handler->dst);

	if (neigh_handler->oif > 0)
		nla_put_u32(m, RTA_OIF, neigh_handler->oif);

	err = nl_send_auto_complete(neigh_handler->sock, m);
	nlmsg_free(m);
	if (err < 0)
		return err;

	nl_socket_modify_cb(neigh_handler->sock, NL_CB_VALID,
			    NL_CB_CUSTOM, &get_route_cb, neigh_handler);

	err = nl_recvmsgs_default(neigh_handler->sock);

	return err;
}
