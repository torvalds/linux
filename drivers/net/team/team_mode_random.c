/*
 * drivers/net/team/team_mode_random.c - Random mode for team
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/if_team.h>

static bool rnd_transmit(struct team *team, struct sk_buff *skb)
{
	struct team_port *port;
	int port_index;

	port_index = prandom_u32_max(team->en_port_count);
	port = team_get_port_by_index_rcu(team, port_index);
	if (unlikely(!port))
		goto drop;
	port = team_get_first_port_txable_rcu(team, port);
	if (unlikely(!port))
		goto drop;
	if (team_dev_queue_xmit(team, port, skb))
		return false;
	return true;

drop:
	dev_kfree_skb_any(skb);
	return false;
}

static const struct team_mode_ops rnd_mode_ops = {
	.transmit		= rnd_transmit,
	.port_enter		= team_modeop_port_enter,
	.port_change_dev_addr	= team_modeop_port_change_dev_addr,
};

static const struct team_mode rnd_mode = {
	.kind		= "random",
	.owner		= THIS_MODULE,
	.ops		= &rnd_mode_ops,
	.lag_tx_type	= NETDEV_LAG_TX_TYPE_RANDOM,
};

static int __init rnd_init_module(void)
{
	return team_mode_register(&rnd_mode);
}

static void __exit rnd_cleanup_module(void)
{
	team_mode_unregister(&rnd_mode);
}

module_init(rnd_init_module);
module_exit(rnd_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_DESCRIPTION("Random mode for team");
MODULE_ALIAS_TEAM_MODE("random");
