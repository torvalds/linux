// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/team/team_mode_broadcast.c - Broadcast mode for team
 * Copyright (c) 2012 Jiri Pirko <jpirko@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if_team.h>

static bool bc_transmit(struct team *team, struct sk_buff *skb)
{
	struct team_port *cur;
	struct team_port *last = NULL;
	struct sk_buff *skb2;
	bool ret;
	bool sum_ret = false;

	list_for_each_entry_rcu(cur, &team->port_list, list) {
		if (team_port_txable(cur)) {
			if (last) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					ret = !team_dev_queue_xmit(team, last,
								   skb2);
					if (!sum_ret)
						sum_ret = ret;
				}
			}
			last = cur;
		}
	}
	if (last) {
		ret = !team_dev_queue_xmit(team, last, skb);
		if (!sum_ret)
			sum_ret = ret;
	}
	return sum_ret;
}

static const struct team_mode_ops bc_mode_ops = {
	.transmit		= bc_transmit,
	.port_enter		= team_modeop_port_enter,
	.port_change_dev_addr	= team_modeop_port_change_dev_addr,
};

static const struct team_mode bc_mode = {
	.kind		= "broadcast",
	.owner		= THIS_MODULE,
	.ops		= &bc_mode_ops,
	.lag_tx_type	= NETDEV_LAG_TX_TYPE_BROADCAST,
};

static int __init bc_init_module(void)
{
	return team_mode_register(&bc_mode);
}

static void __exit bc_cleanup_module(void)
{
	team_mode_unregister(&bc_mode);
}

module_init(bc_init_module);
module_exit(bc_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jpirko@redhat.com>");
MODULE_DESCRIPTION("Broadcast mode for team");
MODULE_ALIAS_TEAM_MODE("broadcast");
