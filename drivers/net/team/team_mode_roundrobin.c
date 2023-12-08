// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/team/team_mode_roundrobin.c - Round-robin mode for team
 * Copyright (c) 2011 Jiri Pirko <jpirko@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if_team.h>

struct rr_priv {
	unsigned int sent_packets;
};

static struct rr_priv *rr_priv(struct team *team)
{
	return (struct rr_priv *) &team->mode_priv;
}

static bool rr_transmit(struct team *team, struct sk_buff *skb)
{
	struct team_port *port;
	int port_index;

	port_index = team_num_to_port_index(team,
					    rr_priv(team)->sent_packets++);
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

static const struct team_mode_ops rr_mode_ops = {
	.transmit		= rr_transmit,
	.port_enter		= team_modeop_port_enter,
	.port_change_dev_addr	= team_modeop_port_change_dev_addr,
};

static const struct team_mode rr_mode = {
	.kind		= "roundrobin",
	.owner		= THIS_MODULE,
	.priv_size	= sizeof(struct rr_priv),
	.ops		= &rr_mode_ops,
	.lag_tx_type	= NETDEV_LAG_TX_TYPE_ROUNDROBIN,
};

static int __init rr_init_module(void)
{
	return team_mode_register(&rr_mode);
}

static void __exit rr_cleanup_module(void)
{
	team_mode_unregister(&rr_mode);
}

module_init(rr_init_module);
module_exit(rr_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jpirko@redhat.com>");
MODULE_DESCRIPTION("Round-robin mode for team");
MODULE_ALIAS_TEAM_MODE("roundrobin");
