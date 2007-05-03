/* AFS network device helpers
 *
 * Copyright (c) 2007 Patrick McHardy <kaber@trash.net>
 */

#include <linux/string.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include "internal.h"

int afs_get_MAC_address(u8 mac[ETH_ALEN])
{
	struct net_device *dev;
	int ret = -ENODEV;

	rtnl_lock();
	dev = __dev_getfirstbyhwtype(ARPHRD_ETHER);
	if (dev) {
		memcpy(mac, dev->dev_addr, ETH_ALEN);
		ret = 0;
	}
	rtnl_unlock();
	return ret;
}

int afs_get_ipv4_interfaces(struct afs_interface *bufs, size_t maxbufs,
			    bool wantloopback)
{
	struct net_device *dev;
	struct in_device *idev;
	int n = 0;

	rtnl_lock();
	for (dev = dev_base; dev; dev = dev->next) {
		if (dev->type == ARPHRD_LOOPBACK && !wantloopback)
			continue;
		idev = __in_dev_get_rtnl(dev);
		if (!idev)
			continue;
		for_primary_ifa(idev) {
			if (n == maxbufs)
				goto out;
			bufs[n].address.s_addr = ifa->ifa_address;
			bufs[n].netmask.s_addr = ifa->ifa_mask;
			bufs[n].mtu = dev->mtu;
			n++;
		} endfor_ifa(idev)
	}
out:
	rtnl_unlock();
	return n;
}
