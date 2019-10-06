// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/net/team/team_mode_loadbalance.c - Load-balancing mode for team
 * Copyright (c) 2012 Jiri Pirko <jpirko@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/if_team.h>

static rx_handler_result_t lb_receive(struct team *team, struct team_port *port,
				      struct sk_buff *skb)
{
	if (unlikely(skb->protocol == htons(ETH_P_SLOW))) {
		/* LACPDU packets should go to exact delivery */
		const unsigned char *dest = eth_hdr(skb)->h_dest;

		if (is_link_local_ether_addr(dest) && dest[5] == 0x02)
			return RX_HANDLER_EXACT;
	}
	return RX_HANDLER_ANOTHER;
}

struct lb_priv;

typedef struct team_port *lb_select_tx_port_func_t(struct team *,
						   struct lb_priv *,
						   struct sk_buff *,
						   unsigned char);

#define LB_TX_HASHTABLE_SIZE 256 /* hash is a char */

struct lb_stats {
	u64 tx_bytes;
};

struct lb_pcpu_stats {
	struct lb_stats hash_stats[LB_TX_HASHTABLE_SIZE];
	struct u64_stats_sync syncp;
};

struct lb_stats_info {
	struct lb_stats stats;
	struct lb_stats last_stats;
	struct team_option_inst_info *opt_inst_info;
};

struct lb_port_mapping {
	struct team_port __rcu *port;
	struct team_option_inst_info *opt_inst_info;
};

struct lb_priv_ex {
	struct team *team;
	struct lb_port_mapping tx_hash_to_port_mapping[LB_TX_HASHTABLE_SIZE];
	struct sock_fprog_kern *orig_fprog;
	struct {
		unsigned int refresh_interval; /* in tenths of second */
		struct delayed_work refresh_dw;
		struct lb_stats_info info[LB_TX_HASHTABLE_SIZE];
	} stats;
};

struct lb_priv {
	struct bpf_prog __rcu *fp;
	lb_select_tx_port_func_t __rcu *select_tx_port_func;
	struct lb_pcpu_stats __percpu *pcpu_stats;
	struct lb_priv_ex *ex; /* priv extension */
};

static struct lb_priv *get_lb_priv(struct team *team)
{
	return (struct lb_priv *) &team->mode_priv;
}

struct lb_port_priv {
	struct lb_stats __percpu *pcpu_stats;
	struct lb_stats_info stats_info;
};

static struct lb_port_priv *get_lb_port_priv(struct team_port *port)
{
	return (struct lb_port_priv *) &port->mode_priv;
}

#define LB_HTPM_PORT_BY_HASH(lp_priv, hash) \
	(lb_priv)->ex->tx_hash_to_port_mapping[hash].port

#define LB_HTPM_OPT_INST_INFO_BY_HASH(lp_priv, hash) \
	(lb_priv)->ex->tx_hash_to_port_mapping[hash].opt_inst_info

static void lb_tx_hash_to_port_mapping_null_port(struct team *team,
						 struct team_port *port)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	bool changed = false;
	int i;

	for (i = 0; i < LB_TX_HASHTABLE_SIZE; i++) {
		struct lb_port_mapping *pm;

		pm = &lb_priv->ex->tx_hash_to_port_mapping[i];
		if (rcu_access_pointer(pm->port) == port) {
			RCU_INIT_POINTER(pm->port, NULL);
			team_option_inst_set_change(pm->opt_inst_info);
			changed = true;
		}
	}
	if (changed)
		team_options_change_check(team);
}

/* Basic tx selection based solely by hash */
static struct team_port *lb_hash_select_tx_port(struct team *team,
						struct lb_priv *lb_priv,
						struct sk_buff *skb,
						unsigned char hash)
{
	int port_index = team_num_to_port_index(team, hash);

	return team_get_port_by_index_rcu(team, port_index);
}

/* Hash to port mapping select tx port */
static struct team_port *lb_htpm_select_tx_port(struct team *team,
						struct lb_priv *lb_priv,
						struct sk_buff *skb,
						unsigned char hash)
{
	struct team_port *port;

	port = rcu_dereference_bh(LB_HTPM_PORT_BY_HASH(lb_priv, hash));
	if (likely(port))
		return port;
	/* If no valid port in the table, fall back to simple hash */
	return lb_hash_select_tx_port(team, lb_priv, skb, hash);
}

struct lb_select_tx_port {
	char *name;
	lb_select_tx_port_func_t *func;
};

static const struct lb_select_tx_port lb_select_tx_port_list[] = {
	{
		.name = "hash",
		.func = lb_hash_select_tx_port,
	},
	{
		.name = "hash_to_port_mapping",
		.func = lb_htpm_select_tx_port,
	},
};
#define LB_SELECT_TX_PORT_LIST_COUNT ARRAY_SIZE(lb_select_tx_port_list)

static char *lb_select_tx_port_get_name(lb_select_tx_port_func_t *func)
{
	int i;

	for (i = 0; i < LB_SELECT_TX_PORT_LIST_COUNT; i++) {
		const struct lb_select_tx_port *item;

		item = &lb_select_tx_port_list[i];
		if (item->func == func)
			return item->name;
	}
	return NULL;
}

static lb_select_tx_port_func_t *lb_select_tx_port_get_func(const char *name)
{
	int i;

	for (i = 0; i < LB_SELECT_TX_PORT_LIST_COUNT; i++) {
		const struct lb_select_tx_port *item;

		item = &lb_select_tx_port_list[i];
		if (!strcmp(item->name, name))
			return item->func;
	}
	return NULL;
}

static unsigned int lb_get_skb_hash(struct lb_priv *lb_priv,
				    struct sk_buff *skb)
{
	struct bpf_prog *fp;
	uint32_t lhash;
	unsigned char *c;

	fp = rcu_dereference_bh(lb_priv->fp);
	if (unlikely(!fp))
		return 0;
	lhash = BPF_PROG_RUN(fp, skb);
	c = (char *) &lhash;
	return c[0] ^ c[1] ^ c[2] ^ c[3];
}

static void lb_update_tx_stats(unsigned int tx_bytes, struct lb_priv *lb_priv,
			       struct lb_port_priv *lb_port_priv,
			       unsigned char hash)
{
	struct lb_pcpu_stats *pcpu_stats;
	struct lb_stats *port_stats;
	struct lb_stats *hash_stats;

	pcpu_stats = this_cpu_ptr(lb_priv->pcpu_stats);
	port_stats = this_cpu_ptr(lb_port_priv->pcpu_stats);
	hash_stats = &pcpu_stats->hash_stats[hash];
	u64_stats_update_begin(&pcpu_stats->syncp);
	port_stats->tx_bytes += tx_bytes;
	hash_stats->tx_bytes += tx_bytes;
	u64_stats_update_end(&pcpu_stats->syncp);
}

static bool lb_transmit(struct team *team, struct sk_buff *skb)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	lb_select_tx_port_func_t *select_tx_port_func;
	struct team_port *port;
	unsigned char hash;
	unsigned int tx_bytes = skb->len;

	hash = lb_get_skb_hash(lb_priv, skb);
	select_tx_port_func = rcu_dereference_bh(lb_priv->select_tx_port_func);
	port = select_tx_port_func(team, lb_priv, skb, hash);
	if (unlikely(!port))
		goto drop;
	if (team_dev_queue_xmit(team, port, skb))
		return false;
	lb_update_tx_stats(tx_bytes, lb_priv, get_lb_port_priv(port), hash);
	return true;

drop:
	dev_kfree_skb_any(skb);
	return false;
}

static int lb_bpf_func_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);

	if (!lb_priv->ex->orig_fprog) {
		ctx->data.bin_val.len = 0;
		ctx->data.bin_val.ptr = NULL;
		return 0;
	}
	ctx->data.bin_val.len = lb_priv->ex->orig_fprog->len *
				sizeof(struct sock_filter);
	ctx->data.bin_val.ptr = lb_priv->ex->orig_fprog->filter;
	return 0;
}

static int __fprog_create(struct sock_fprog_kern **pfprog, u32 data_len,
			  const void *data)
{
	struct sock_fprog_kern *fprog;
	struct sock_filter *filter = (struct sock_filter *) data;

	if (data_len % sizeof(struct sock_filter))
		return -EINVAL;
	fprog = kmalloc(sizeof(*fprog), GFP_KERNEL);
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

static void __fprog_destroy(struct sock_fprog_kern *fprog)
{
	kfree(fprog->filter);
	kfree(fprog);
}

static int lb_bpf_func_set(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	struct bpf_prog *fp = NULL;
	struct bpf_prog *orig_fp = NULL;
	struct sock_fprog_kern *fprog = NULL;
	int err;

	if (ctx->data.bin_val.len) {
		err = __fprog_create(&fprog, ctx->data.bin_val.len,
				     ctx->data.bin_val.ptr);
		if (err)
			return err;
		err = bpf_prog_create(&fp, fprog);
		if (err) {
			__fprog_destroy(fprog);
			return err;
		}
	}

	if (lb_priv->ex->orig_fprog) {
		/* Clear old filter data */
		__fprog_destroy(lb_priv->ex->orig_fprog);
		orig_fp = rcu_dereference_protected(lb_priv->fp,
						lockdep_is_held(&team->lock));
	}

	rcu_assign_pointer(lb_priv->fp, fp);
	lb_priv->ex->orig_fprog = fprog;

	if (orig_fp) {
		synchronize_rcu();
		bpf_prog_destroy(orig_fp);
	}
	return 0;
}

static void lb_bpf_func_free(struct team *team)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	struct bpf_prog *fp;

	if (!lb_priv->ex->orig_fprog)
		return;

	__fprog_destroy(lb_priv->ex->orig_fprog);
	fp = rcu_dereference_protected(lb_priv->fp,
				       lockdep_is_held(&team->lock));
	bpf_prog_destroy(fp);
}

static int lb_tx_method_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	lb_select_tx_port_func_t *func;
	char *name;

	func = rcu_dereference_protected(lb_priv->select_tx_port_func,
					 lockdep_is_held(&team->lock));
	name = lb_select_tx_port_get_name(func);
	BUG_ON(!name);
	ctx->data.str_val = name;
	return 0;
}

static int lb_tx_method_set(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	lb_select_tx_port_func_t *func;

	func = lb_select_tx_port_get_func(ctx->data.str_val);
	if (!func)
		return -EINVAL;
	rcu_assign_pointer(lb_priv->select_tx_port_func, func);
	return 0;
}

static int lb_tx_hash_to_port_mapping_init(struct team *team,
					   struct team_option_inst_info *info)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	unsigned char hash = info->array_index;

	LB_HTPM_OPT_INST_INFO_BY_HASH(lb_priv, hash) = info;
	return 0;
}

static int lb_tx_hash_to_port_mapping_get(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	struct team_port *port;
	unsigned char hash = ctx->info->array_index;

	port = LB_HTPM_PORT_BY_HASH(lb_priv, hash);
	ctx->data.u32_val = port ? port->dev->ifindex : 0;
	return 0;
}

static int lb_tx_hash_to_port_mapping_set(struct team *team,
					  struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	struct team_port *port;
	unsigned char hash = ctx->info->array_index;

	list_for_each_entry(port, &team->port_list, list) {
		if (ctx->data.u32_val == port->dev->ifindex &&
		    team_port_enabled(port)) {
			rcu_assign_pointer(LB_HTPM_PORT_BY_HASH(lb_priv, hash),
					   port);
			return 0;
		}
	}
	return -ENODEV;
}

static int lb_hash_stats_init(struct team *team,
			      struct team_option_inst_info *info)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	unsigned char hash = info->array_index;

	lb_priv->ex->stats.info[hash].opt_inst_info = info;
	return 0;
}

static int lb_hash_stats_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	unsigned char hash = ctx->info->array_index;

	ctx->data.bin_val.ptr = &lb_priv->ex->stats.info[hash].stats;
	ctx->data.bin_val.len = sizeof(struct lb_stats);
	return 0;
}

static int lb_port_stats_init(struct team *team,
			      struct team_option_inst_info *info)
{
	struct team_port *port = info->port;
	struct lb_port_priv *lb_port_priv = get_lb_port_priv(port);

	lb_port_priv->stats_info.opt_inst_info = info;
	return 0;
}

static int lb_port_stats_get(struct team *team, struct team_gsetter_ctx *ctx)
{
	struct team_port *port = ctx->info->port;
	struct lb_port_priv *lb_port_priv = get_lb_port_priv(port);

	ctx->data.bin_val.ptr = &lb_port_priv->stats_info.stats;
	ctx->data.bin_val.len = sizeof(struct lb_stats);
	return 0;
}

static void __lb_stats_info_refresh_prepare(struct lb_stats_info *s_info)
{
	memcpy(&s_info->last_stats, &s_info->stats, sizeof(struct lb_stats));
	memset(&s_info->stats, 0, sizeof(struct lb_stats));
}

static bool __lb_stats_info_refresh_check(struct lb_stats_info *s_info,
					  struct team *team)
{
	if (memcmp(&s_info->last_stats, &s_info->stats,
	    sizeof(struct lb_stats))) {
		team_option_inst_set_change(s_info->opt_inst_info);
		return true;
	}
	return false;
}

static void __lb_one_cpu_stats_add(struct lb_stats *acc_stats,
				   struct lb_stats *cpu_stats,
				   struct u64_stats_sync *syncp)
{
	unsigned int start;
	struct lb_stats tmp;

	do {
		start = u64_stats_fetch_begin_irq(syncp);
		tmp.tx_bytes = cpu_stats->tx_bytes;
	} while (u64_stats_fetch_retry_irq(syncp, start));
	acc_stats->tx_bytes += tmp.tx_bytes;
}

static void lb_stats_refresh(struct work_struct *work)
{
	struct team *team;
	struct lb_priv *lb_priv;
	struct lb_priv_ex *lb_priv_ex;
	struct lb_pcpu_stats *pcpu_stats;
	struct lb_stats *stats;
	struct lb_stats_info *s_info;
	struct team_port *port;
	bool changed = false;
	int i;
	int j;

	lb_priv_ex = container_of(work, struct lb_priv_ex,
				  stats.refresh_dw.work);

	team = lb_priv_ex->team;
	lb_priv = get_lb_priv(team);

	if (!mutex_trylock(&team->lock)) {
		schedule_delayed_work(&lb_priv_ex->stats.refresh_dw, 0);
		return;
	}

	for (j = 0; j < LB_TX_HASHTABLE_SIZE; j++) {
		s_info = &lb_priv->ex->stats.info[j];
		__lb_stats_info_refresh_prepare(s_info);
		for_each_possible_cpu(i) {
			pcpu_stats = per_cpu_ptr(lb_priv->pcpu_stats, i);
			stats = &pcpu_stats->hash_stats[j];
			__lb_one_cpu_stats_add(&s_info->stats, stats,
					       &pcpu_stats->syncp);
		}
		changed |= __lb_stats_info_refresh_check(s_info, team);
	}

	list_for_each_entry(port, &team->port_list, list) {
		struct lb_port_priv *lb_port_priv = get_lb_port_priv(port);

		s_info = &lb_port_priv->stats_info;
		__lb_stats_info_refresh_prepare(s_info);
		for_each_possible_cpu(i) {
			pcpu_stats = per_cpu_ptr(lb_priv->pcpu_stats, i);
			stats = per_cpu_ptr(lb_port_priv->pcpu_stats, i);
			__lb_one_cpu_stats_add(&s_info->stats, stats,
					       &pcpu_stats->syncp);
		}
		changed |= __lb_stats_info_refresh_check(s_info, team);
	}

	if (changed)
		team_options_change_check(team);

	schedule_delayed_work(&lb_priv_ex->stats.refresh_dw,
			      (lb_priv_ex->stats.refresh_interval * HZ) / 10);

	mutex_unlock(&team->lock);
}

static int lb_stats_refresh_interval_get(struct team *team,
					 struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);

	ctx->data.u32_val = lb_priv->ex->stats.refresh_interval;
	return 0;
}

static int lb_stats_refresh_interval_set(struct team *team,
					 struct team_gsetter_ctx *ctx)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	unsigned int interval;

	interval = ctx->data.u32_val;
	if (lb_priv->ex->stats.refresh_interval == interval)
		return 0;
	lb_priv->ex->stats.refresh_interval = interval;
	if (interval)
		schedule_delayed_work(&lb_priv->ex->stats.refresh_dw, 0);
	else
		cancel_delayed_work(&lb_priv->ex->stats.refresh_dw);
	return 0;
}

static const struct team_option lb_options[] = {
	{
		.name = "bpf_hash_func",
		.type = TEAM_OPTION_TYPE_BINARY,
		.getter = lb_bpf_func_get,
		.setter = lb_bpf_func_set,
	},
	{
		.name = "lb_tx_method",
		.type = TEAM_OPTION_TYPE_STRING,
		.getter = lb_tx_method_get,
		.setter = lb_tx_method_set,
	},
	{
		.name = "lb_tx_hash_to_port_mapping",
		.array_size = LB_TX_HASHTABLE_SIZE,
		.type = TEAM_OPTION_TYPE_U32,
		.init = lb_tx_hash_to_port_mapping_init,
		.getter = lb_tx_hash_to_port_mapping_get,
		.setter = lb_tx_hash_to_port_mapping_set,
	},
	{
		.name = "lb_hash_stats",
		.array_size = LB_TX_HASHTABLE_SIZE,
		.type = TEAM_OPTION_TYPE_BINARY,
		.init = lb_hash_stats_init,
		.getter = lb_hash_stats_get,
	},
	{
		.name = "lb_port_stats",
		.per_port = true,
		.type = TEAM_OPTION_TYPE_BINARY,
		.init = lb_port_stats_init,
		.getter = lb_port_stats_get,
	},
	{
		.name = "lb_stats_refresh_interval",
		.type = TEAM_OPTION_TYPE_U32,
		.getter = lb_stats_refresh_interval_get,
		.setter = lb_stats_refresh_interval_set,
	},
};

static int lb_init(struct team *team)
{
	struct lb_priv *lb_priv = get_lb_priv(team);
	lb_select_tx_port_func_t *func;
	int i, err;

	/* set default tx port selector */
	func = lb_select_tx_port_get_func("hash");
	BUG_ON(!func);
	rcu_assign_pointer(lb_priv->select_tx_port_func, func);

	lb_priv->ex = kzalloc(sizeof(*lb_priv->ex), GFP_KERNEL);
	if (!lb_priv->ex)
		return -ENOMEM;
	lb_priv->ex->team = team;

	lb_priv->pcpu_stats = alloc_percpu(struct lb_pcpu_stats);
	if (!lb_priv->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_pcpu_stats;
	}

	for_each_possible_cpu(i) {
		struct lb_pcpu_stats *team_lb_stats;
		team_lb_stats = per_cpu_ptr(lb_priv->pcpu_stats, i);
		u64_stats_init(&team_lb_stats->syncp);
	}


	INIT_DELAYED_WORK(&lb_priv->ex->stats.refresh_dw, lb_stats_refresh);

	err = team_options_register(team, lb_options, ARRAY_SIZE(lb_options));
	if (err)
		goto err_options_register;
	return 0;

err_options_register:
	free_percpu(lb_priv->pcpu_stats);
err_alloc_pcpu_stats:
	kfree(lb_priv->ex);
	return err;
}

static void lb_exit(struct team *team)
{
	struct lb_priv *lb_priv = get_lb_priv(team);

	team_options_unregister(team, lb_options,
				ARRAY_SIZE(lb_options));
	lb_bpf_func_free(team);
	cancel_delayed_work_sync(&lb_priv->ex->stats.refresh_dw);
	free_percpu(lb_priv->pcpu_stats);
	kfree(lb_priv->ex);
}

static int lb_port_enter(struct team *team, struct team_port *port)
{
	struct lb_port_priv *lb_port_priv = get_lb_port_priv(port);

	lb_port_priv->pcpu_stats = alloc_percpu(struct lb_stats);
	if (!lb_port_priv->pcpu_stats)
		return -ENOMEM;
	return 0;
}

static void lb_port_leave(struct team *team, struct team_port *port)
{
	struct lb_port_priv *lb_port_priv = get_lb_port_priv(port);

	free_percpu(lb_port_priv->pcpu_stats);
}

static void lb_port_disabled(struct team *team, struct team_port *port)
{
	lb_tx_hash_to_port_mapping_null_port(team, port);
}

static const struct team_mode_ops lb_mode_ops = {
	.init			= lb_init,
	.exit			= lb_exit,
	.port_enter		= lb_port_enter,
	.port_leave		= lb_port_leave,
	.port_disabled		= lb_port_disabled,
	.receive		= lb_receive,
	.transmit		= lb_transmit,
};

static const struct team_mode lb_mode = {
	.kind		= "loadbalance",
	.owner		= THIS_MODULE,
	.priv_size	= sizeof(struct lb_priv),
	.port_priv_size	= sizeof(struct lb_port_priv),
	.ops		= &lb_mode_ops,
	.lag_tx_type	= NETDEV_LAG_TX_TYPE_HASH,
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
MODULE_ALIAS_TEAM_MODE("loadbalance");
