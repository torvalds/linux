#ifndef __BACKPORT_NET_IPV6_H
#define __BACKPORT_NET_IPV6_H
#include_next <net/ipv6.h>
#include <linux/version.h>
#include <net/addrconf.h>
#include <net/inet_frag.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
/*
 *	Equivalent of ipv4 struct ip
 */
struct frag_queue {
	struct inet_frag_queue  q;

	__be32                  id;             /* fragment id          */
	u32                     user;
	struct in6_addr         saddr;
	struct in6_addr         daddr;

	int                     iif;
	unsigned int            csum;
	__u16                   nhoffset;
};
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
#define ipv6_addr_hash LINUX_BACKPORT(ipv6_addr_hash)
static inline u32 ipv6_addr_hash(const struct in6_addr *a)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	const unsigned long *ul = (const unsigned long *)a;
	unsigned long x = ul[0] ^ ul[1];

	return (u32)(x ^ (x >> 32));
#else
	return (__force u32)(a->s6_addr32[0] ^ a->s6_addr32[1] ^
			     a->s6_addr32[2] ^ a->s6_addr32[3]);
#endif
}
#endif

#endif /* __BACKPORT_NET_IPV6_H */
