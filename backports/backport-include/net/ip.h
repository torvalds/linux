#ifndef __BACKPORT_NET_IP_H
#define __BACKPORT_NET_IP_H
#include_next <net/ip.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
/* Backports 56f8a75c */
static inline bool ip_is_fragment(const struct iphdr *iph)
{
	return (iph->frag_off & htons(IP_MF | IP_OFFSET)) != 0;
}
#endif

#endif /* __BACKPORT_NET_IP_H */
