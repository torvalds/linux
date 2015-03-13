#ifndef _COMPAT_NET_NET_NAMESPACE_H
#define _COMPAT_NET_NET_NAMESPACE_H 1

#include_next <net/net_namespace.h>

#if IS_ENABLED(CPTCFG_IEEE802154_6LOWPAN)
#include <linux/version.h>
#include <net/netns/ieee802154_6lowpan.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
/*
 * we provide backport for 6lowpan as per the dependencies file
 * down to 3.5 only.
 */
extern struct netns_ieee802154_lowpan ieee802154_lowpan;
struct netns_ieee802154_lowpan *net_ieee802154_lowpan(struct net *net);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
/* This can be removed once and if this gets upstream */
static inline struct netns_ieee802154_lowpan *
net_ieee802154_lowpan(struct net *net)
{
	return &net->ieee802154_lowpan;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0) */
#endif /* CPTCFG_IEEE802154_6LOWPAN */

#endif	/* _COMPAT_NET_NET_NAMESPACE_H */
