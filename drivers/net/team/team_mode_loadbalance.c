/*
 * drivers/net/team/team_mode_loadbalance.c - Load-balancing mode for team
 * Copyright (c) 2012 Jiri Pirko <jpirko@redhat.com>
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
#include <linux/filter.h>
#include <linux/if_team.h>

struct lb_priv {
	struct sk_filter __rcu *fp;
	struct sock_fprog *orig_fprog;
};

static struct lb_priv *lb_priv(struct team *team)
{
	return (struct lb_priv *) &team->mode_priv;
}

static bool lb_transmit(struct team *team, struct sk_buff *skb)
{
	struct sk_filter *fp;
	struct team_port *port;
	unsigned int hash;
	int port_index;

	fp = rcu_dereference(lb_priv(team)->fp);
	if (unlikely(!fp))
		goto drop;
	hash = SK_RUN_FILTER(fp, skb);
	port_index = hash % team->en_port_count;
	port = team_get_port_by_index_rcu(team, port_index);
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

static int lb_bpf_func_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	if (!lb_priv(team)->orig_fprog) {
		ctx->data.bin_val.len = 0;
		ctx->data.bin_val.ptr = NULL;
		return 0;
	}
	ctx->data.bin_val.len = lb_priv(team)->orig_fprog->len *
				sizeof(struct sock_filter);
	ctx->data.bin_val.ptr = lb_priv(team)->orig_fprog->filter;
	return 0;
}

static int __fprog_create(struct sock_fprog **pfprog, u32 data_len,
			  const void *data)
{
	struct sock_fprog *fprog;
	struct sock_filter *filter = (struct sock_filter *) data;

	if (data_len % sizeof(struct sock_filter))
		return -EINVAL;
	fprog = kmalloc(sizeof(struct sock_fprog), GFP_KERNEL);
	if (!fprog)
		return -ENOMEM;
	fprog->filter = kmemdup(filter, data_len, GFP_KERNEL);
	if (!fprog->filter) {
		kfree(fprog);
		return -ENOMEM;
	}
	fprog->len = data_len / sizeof(struct sock_filter);
	*pfprog = fprog;
	return 0;
}

static void __fprog_destroy(struct sock_fprog *fprog)
{
	kfree(fprog->filter);
	kfree(fprog);
}

static int lb_bpf_func_set(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct sk_filter *fp = NULL;
	struct sock_fprog *fprog = NULL;
	int err;

	if (ctx->data.bin_val.len) {
		err = __fprog_create(&fprog, ctx->data.bin_val.len,
				     ctx->data.bin_val.ptr);
		if (err)
			return err;
		err = sk_unattached_filter_create(&fp, fprog);
		if (err) {
			__fprog_destroy(fprog);
			return err;
		}
	}

	if (lb_priv(team)->orig_fprog) {
		/* Clear old filter data */
		__fprog_destroy(lb_priv(team)->orig_fprog);
		sk_unattached_filter_destroy(lb_priv(team)->fp);
	}

	rcu_assign_pointer(lb_priv(team)->fp, fp);
	lb_priv(team)->orig_fprog = fprog;
	return 0;
}

static const struct team_option lb_options[] = {
	{
		.name = "bpf_hash_func",
		.type = TEAM_OPTION_TYPE_BINARY,
		.getter = lb_bpf_func_get,
		.setter = lb_bpf_func_set,
	},
};

static int lb_init(struct team *team)
{
	return team_options_register(team, lb_options,
				     ARRAY_SIZE(lb_options));
}

static void lb_exit(struct team *team)
{
	team_options_unregister(team, lb_options,
				ARRAY_SIZE(lb_options));
}

static const struct team_mode_ops lb_mode_ops = {
	.init			= lb_init,
	.exit			= lb_exit,
	.transmit		= lb_transmit,
};

static struct team_mode lb_mode = {
	.kind		= "loadbalance",
	.owner		= THIS_MODULE,
	.priv_size	= sizeof(struct lb_priv),
	.ops		= &lb_mode_ops,
};

static int __init lb_init_module(void)
{
	return team_mode_register(&lb_mode);
}

static void __exit lb_cleanup_module(void)
{
	team_mode_unregister(&lb_mode);
}

module_init(lb_init_module);
module_exit(lb_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jpirko@redhat.com>");
MODULE_DESCRIPTION("Load-balancing mode for team");
MODULE_ALIAS("team-mode-loadbalance");
