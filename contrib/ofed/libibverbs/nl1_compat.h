/* Licensed under the OpenIB.org BSD license (FreeBSD Variant) - See COPYING.md
 */

#ifndef _NL1_COMPAT_H_
#define _NL1_COMPAT_H_

#include <netlink/route/rtnl.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>
#include <netlink/route/neighbour.h>

struct nl_handle;

/* Workaround - declaration missing */
extern int		rtnl_link_vlan_get_id(struct rtnl_link *);

#define nl_geterror(x) nl_geterror()
#define nl_sock nl_handle

static inline void nl_socket_disable_seq_check(struct nl_sock *sock)
{
	nl_disable_sequence_check(sock);
}

struct rtnl_nexthop {};

static inline struct rtnl_nexthop *rtnl_route_nexthop_n(
	struct rtnl_route *r, int n)
{
	return (struct rtnl_nexthop *)r;
}

static inline struct nl_addr *rtnl_route_nh_get_gateway(struct rtnl_nexthop *nh)
{
	return rtnl_route_get_gateway((struct rtnl_route *)nh);
}

static inline int rtnl_route_nh_get_ifindex(struct rtnl_nexthop *nh)
{
	return rtnl_route_get_oif((struct rtnl_route *)nh);
}

#define nl_addr_info(addr, result)	(		\
	*(result) = nl_addr_info(addr),			\
	(*(result) == NULL) ? nl_get_errno() : 0		\
)

static inline void nl_socket_free(struct nl_sock *sock)
{
	nl_close(sock);
}

static inline struct nl_sock *nl_socket_alloc(void)
{
	return nl_handle_alloc();
}

#define rtnl_link_alloc_cache(sock, family, result) (	\
	*result = rtnl_link_alloc_cache(sock),		\
	(*result == NULL) ? nl_get_errno() : 0		\
)

#define rtnl_route_alloc_cache(sock, family, flags, result) (	\
	*result = rtnl_route_alloc_cache(sock),			\
	(*result == NULL) ? nl_get_errno() : 0			\
)

#define rtnl_neigh_alloc_cache(sock, result) (			\
	*result = rtnl_neigh_alloc_cache(sock),			\
	(*result == NULL) ? nl_get_errno() : 0			\
)

static inline int rtnl_link_is_vlan(struct rtnl_link *link)
{
	return rtnl_link_vlan_get_id(link) <= 0;
}

#endif
