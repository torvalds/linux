#ifndef __BACKPORT_NET_IP6_ROUTE_H
#define __BACKPORT_NET_IP6_ROUTE_H
#include_next <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#define rt6_nexthop LINUX_BACKPORT(rt6_nexthop)
static inline struct in6_addr *rt6_nexthop(struct rt6_info *rt)
{
	return &rt->rt6i_gateway;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0) */

#endif /* __BACKPORT_NET_IP6_ROUTE_H */
