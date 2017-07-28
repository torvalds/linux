/*
 * drivers/net/team/team_mode_activebackup.c - Active-backup mode for team
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
#include <net/rtnetlink.h>
#include <linux/if_team.h>

struct ab_priv {
	struct team_port __rcu *active_port;
	struct team_option_inst_info *ap_opt_inst_info;
};

static struct ab_priv *ab_priv(struct team *team)
{
	return (struct ab_priv *) &team->mode_priv;
}

static rx_handler_result_t ab_receive(struct team *team, struct team_port *port,
				      struct sk_buff *skb) {
	struct team_port *active_port;

	active_port = rcu_dereference(ab_priv(team)->active_port);
	if (active_port != port)
		return RX_HANDLER_EXACT;
	return RX_HANDLER_ANOTHER;
}

static bool ab_transmit(struct team *team, struct sk_buff *skb)
{
	struct team_port *active_port;

	active_port = rcu_dereference_bh(ab_priv(team)->active_port);
	if (unlikely(!active_port))
		goto drop;
	if (team_dev_queue_xmit(team, active_port, skb))
		return false;
	return true;

drop:
	dev_kfree_skb_any(skb);
	return false;
}

static void ab_port_leave(struct team *team, struct team_port *port)
{
	if (ab_priv(team)->active_port == port) {
		RCU_INIT_POINTER(ab_priv(team)->active_port, NULL);
		team_option_inst_set_change(ab_priv(team)->ap_opt_inst_info);
	}
}

static int ab_active_port_init(struct team *team,
			       struct team_option_inst_info *info)
{
	ab_priv(team)->ap_opt_inst_info = info;
	return 0;
}

static int ab_active_port_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct team_port *active_port;

	active_port = rcu_dereference_protected(ab_priv(team)->active_port,
						lockdep_is_held(&team->lock));
	if (active_port)
		ctx->data.u32_val = active_port->dev->ifindex;
	else
		ctx->data.u32_val = 0;
	return 0;
}

static int ab_active_port_set(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct team_port *port;

	list_for_each_entry(port, &team->port_list, list) {
		if (port->dev->ifindex == ctx->data.u32_val) {
			rcu_assign_pointer(ab_priv(team)->active_port, port);
			return 0;
		}
	}
	return -ENOENT;
}

static const struct team_option ab_options[] = {
	{
		.name = "activeport",
		.type = TEAM_OPTION_TYPE_U32,
		.init = ab_active_port_init,
		.getter = ab_active_port_get,
		.setter = ab_active_port_set,
	},
};

static int ab_init(struct team *team)
{
	return team_options_register(team, ab_options, ARRAY_SIZE(ab_options));
}

static void ab_exit(struct team *team)
{
	team_options_unregister(team, ab_options, ARRAY_SIZE(ab_options));
}

static const struct team_mode_ops ab_mode_ops = {
	.init			= ab_init,
	.exit			= ab_exit,
	.receive		= ab_receive,
	.transmit		= ab_transmit,
	.port_leave		= ab_port_leave,
};

static const struct team_mode ab_mode = {
	.kind		= "activebackup",
	.owner		= THIS_MODULE,
	.priv_size	= sizeof(struct ab_priv),
	.ops		= &ab_mode_ops,
	.lag_tx_type	= NETDEV_LAG_TX_TYPE_ACTIVEBACKUP,
};

static int __init ab_init_module(void)
{
	return team_mode_register(&ab_mode);
}

static void __exit ab_cleanup_module(void)
{
	team_mode_unregister(&ab_mode);
}

module_init(ab_init_module);
module_exit(ab_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jpirko@redhat.com>");
MODULE_DESCRIPTION("Active-backup mode for team");
MODULE_ALIAS_TEAM_MODE("activebackup");
