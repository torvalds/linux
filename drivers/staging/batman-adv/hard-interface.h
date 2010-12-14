/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_HARD_INTERFACE_H_
#define _NET_BATMAN_ADV_HARD_INTERFACE_H_

#define IF_NOT_IN_USE 0
#define IF_TO_BE_REMOVED 1
#define IF_INACTIVE 2
#define IF_ACTIVE 3
#define IF_TO_BE_ACTIVATED 4
#define IF_I_WANT_YOU 5

extern struct notifier_block hard_if_notifier;

struct batman_if *get_batman_if_by_netdev(struct net_device *net_dev);
int hardif_enable_interface(struct batman_if *batman_if, char *iface_name);
void hardif_disable_interface(struct batman_if *batman_if);
void hardif_remove_interfaces(void);
int batman_skb_recv(struct sk_buff *skb,
				struct net_device *dev,
				struct packet_type *ptype,
				struct net_device *orig_dev);
int hardif_min_mtu(struct net_device *soft_iface);
void update_min_mtu(struct net_device *soft_iface);

static inline void hardif_hold(struct batman_if *batman_if)
{
	atomic_inc(&batman_if->refcnt);
}

static inline void hardif_put(struct batman_if *batman_if)
{
	if (atomic_dec_and_test(&batman_if->refcnt)) {
		dev_put(batman_if->net_dev);
		kfree(batman_if);
	}
}

#endif /* _NET_BATMAN_ADV_HARD_INTERFACE_H_ */
