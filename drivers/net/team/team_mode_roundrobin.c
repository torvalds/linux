/*
 * net/drivers/team/team_mode_roundrobin.c - Round-robin mode for team
 * Copyright (c) 2011 Jiri Pirko <jpirko@redhat.com>
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

static struct team_port *__get_first_port_up(struct team *team,
					     struct team_port *port)
{
	struct team_port *cur;

	if (port->linkup)
		return port;
	cur = port;
	list_for_each_entry_continue_rcu(cur, &team->port_list, list)
		if (cur->linkup)
			return cur;
	list_for_each_entry_rcu(cur, &team->port_list, list) {
		if (cur == port)
			break;
		if (cur->linkup)
			return cur;
	}
	return NULL;
}

static bool rr_transmit(struct team *team, struct sk_buff *skb)
{
	struct team_port *port;
	int port_index;

	port_index = rr_priv(team)->sent_packets++ % team->port_count;
	port = team_get_port_by_index_rcu(team, port_index);
	if (unlikely(!port))
		goto drop;
	port = __get_first_port_up(team, port);
	if (unlikely(!port))
		goto drop;
	skb->dev = port->dev;
	if (dev_queue_xmit(skb))
		return false;
	return true;

drop:
	dev_kfree_skb_any(skb);
	return false;
}

static int rr_port_enter(struct team *team, struct team_port *port)
{
	return team_port_set_team_mac(port);
}

static void rr_port_change_mac(struct team *team, struct team_port *port)
{
	team_port_set_team_mac(port);
}

static const struct team_mode_ops rr_mode_ops = {
	.transmit		= rr_transmit,
	.port_enter		= rr_port_enter,
	.port_change_mac	= rr_port_change_mac,
};

static struct team_mode rr_mode = {
	.kind		= "roundrobin",
	.owner		= THIS_MODULE,
	.priv_size	= sizeof(struct rr_priv),
	.ops		= &rr_mode_ops,
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
MODULE_ALIAS("team-mode-roundrobin");
