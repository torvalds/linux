/*
 * Copyright(c) 2008 Hewlett-Packard Development Company, L.P.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/if_vlan.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/netns/generic.h>
#include "bonding.h"

/*
 * Assign bond->master_ipv6 to the next IPv6 address in the list, or
 * zero it out if there are none.
 */
static void bond_glean_dev_ipv6(struct net_device *dev, struct in6_addr *addr)
{
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;

	if (!dev)
		return;

	idev = in6_dev_get(dev);
	if (!idev)
		return;

	read_lock_bh(&idev->lock);
	ifa = idev->addr_list;
	if (ifa)
		ipv6_addr_copy(addr, &ifa->addr);
	else
		ipv6_addr_set(addr, 0, 0, 0, 0);

	read_unlock_bh(&idev->lock);

	in6_dev_put(idev);
}

static void bond_na_send(struct net_device *slave_dev,
			 struct in6_addr *daddr,
			 int router,
			 unsigned short vlan_id)
{
	struct in6_addr mcaddr;
	struct icmp6hdr icmp6h = {
		.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT,
	};
	struct sk_buff *skb;

	icmp6h.icmp6_router = router;
	icmp6h.icmp6_solicited = 0;
	icmp6h.icmp6_override = 1;

	addrconf_addr_solict_mult(daddr, &mcaddr);

	pr_debug("ipv6 na on slave %s: dest %pI6, src %pI6\n",
		 slave_dev->name, &mcaddr, daddr);

	skb = ndisc_build_skb(slave_dev, &mcaddr, daddr, &icmp6h, daddr,
			      ND_OPT_TARGET_LL_ADDR);

	if (!skb) {
		pr_err("NA packet allocation failed\n");
		return;
	}

	if (vlan_id) {
		skb = vlan_put_tag(skb, vlan_id);
		if (!skb) {
			pr_err("failed to insert VLAN tag\n");
			return;
		}
	}

	ndisc_send_skb(skb, slave_dev, NULL, &mcaddr, daddr, &icmp6h);
}

/*
 * Kick out an unsolicited Neighbor Advertisement for an IPv6 address on
 * the bonding master.  This will help the switch learn our address
 * if in active-backup mode.
 *
 * Caller must hold curr_slave_lock for read or better
 */
void bond_send_unsolicited_na(struct bonding *bond)
{
	struct slave *slave = bond->curr_active_slave;
	struct vlan_entry *vlan;
	struct inet6_dev *idev;
	int is_router;

	pr_debug("%s: bond %s slave %s\n", bond->dev->name,
		 __func__, slave ? slave->dev->name : "NULL");

	if (!slave || !bond->send_unsol_na ||
	    test_bit(__LINK_STATE_LINKWATCH_PENDING, &slave->dev->state))
		return;

	bond->send_unsol_na--;

	idev = in6_dev_get(bond->dev);
	if (!idev)
		return;

	is_router = !!idev->cnf.forwarding;

	in6_dev_put(idev);

	if (!ipv6_addr_any(&bond->master_ipv6))
		bond_na_send(slave->dev, &bond->master_ipv6, is_router, 0);

	list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
		if (!ipv6_addr_any(&vlan->vlan_ipv6)) {
			bond_na_send(slave->dev, &vlan->vlan_ipv6, is_router,
				     vlan->vlan_id);
		}
	}
}

/*
 * bond_inet6addr_event: handle inet6addr notifier chain events.
 *
 * We keep track of device IPv6 addresses primarily to use as source
 * addresses in NS probes.
 *
 * We track one IPv6 for the main device (if it has one).
 */
static int bond_inet6addr_event(struct notifier_block *this,
				unsigned long event,
				void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	struct net_device *vlan_dev, *event_dev = ifa->idev->dev;
	struct bonding *bond;
	struct vlan_entry *vlan;
	struct bond_net *bn = net_generic(dev_net(event_dev), bond_net_id);

	list_for_each_entry(bond, &bn->dev_list, bond_list) {
		if (bond->dev == event_dev) {
			switch (event) {
			case NETDEV_UP:
				if (ipv6_addr_any(&bond->master_ipv6))
					ipv6_addr_copy(&bond->master_ipv6,
						       &ifa->addr);
				return NOTIFY_OK;
			case NETDEV_DOWN:
				if (ipv6_addr_equal(&bond->master_ipv6,
						    &ifa->addr))
					bond_glean_dev_ipv6(bond->dev,
							    &bond->master_ipv6);
				return NOTIFY_OK;
			default:
				return NOTIFY_DONE;
			}
		}

		list_for_each_entry(vlan, &bond->vlan_list, vlan_list) {
			vlan_dev = vlan_group_get_device(bond->vlgrp,
							 vlan->vlan_id);
			if (vlan_dev == event_dev) {
				switch (event) {
				case NETDEV_UP:
					if (ipv6_addr_any(&vlan->vlan_ipv6))
						ipv6_addr_copy(&vlan->vlan_ipv6,
							       &ifa->addr);
					return NOTIFY_OK;
				case NETDEV_DOWN:
					if (ipv6_addr_equal(&vlan->vlan_ipv6,
							    &ifa->addr))
						bond_glean_dev_ipv6(vlan_dev,
								    &vlan->vlan_ipv6);
					return NOTIFY_OK;
				default:
					return NOTIFY_DONE;
				}
			}
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block bond_inet6addr_notifier = {
	.notifier_call = bond_inet6addr_event,
};

void bond_register_ipv6_notifier(void)
{
	register_inet6addr_notifier(&bond_inet6addr_notifier);
}

void bond_unregister_ipv6_notifier(void)
{
	unregister_inet6addr_notifier(&bond_inet6addr_notifier);
}

