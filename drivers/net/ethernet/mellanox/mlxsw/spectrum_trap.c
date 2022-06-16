// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019 Mellanox Technologies. All rights reserved */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/netlink.h>
#include <net/devlink.h>
#include <uapi/linux/devlink.h>

#include "core.h"
#include "reg.h"
#include "spectrum.h"
#include "spectrum_trap.h"

struct mlxsw_sp_trap_policer_item {
	struct devlink_trap_policer policer;
	u16 hw_id;
};

struct mlxsw_sp_trap_group_item {
	struct devlink_trap_group group;
	u16 hw_group_id;
	u8 priority;
	u8 fixed_policer:1; /* Whether policer binding can change */
};

#define MLXSW_SP_TRAP_LISTENERS_MAX 3

struct mlxsw_sp_trap_item {
	struct devlink_trap trap;
	struct mlxsw_listener listeners_arr[MLXSW_SP_TRAP_LISTENERS_MAX];
	u8 is_source:1;
};

/* All driver-specific traps must be documented in
 * Documentation/networking/devlink/mlxsw.rst
 */
enum {
	DEVLINK_MLXSW_TRAP_ID_BASE = DEVLINK_TRAP_GENERIC_ID_MAX,
	DEVLINK_MLXSW_TRAP_ID_IRIF_DISABLED,
	DEVLINK_MLXSW_TRAP_ID_ERIF_DISABLED,
};

#define DEVLINK_MLXSW_TRAP_NAME_IRIF_DISABLED \
	"irif_disabled"
#define DEVLINK_MLXSW_TRAP_NAME_ERIF_DISABLED \
	"erif_disabled"

#define MLXSW_SP_TRAP_METADATA DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT

enum {
	/* Packet was mirrored from ingress. */
	MLXSW_SP_MIRROR_REASON_INGRESS = 1,
	/* Packet was mirrored from policy engine. */
	MLXSW_SP_MIRROR_REASON_POLICY_ENGINE = 2,
	/* Packet was early dropped. */
	MLXSW_SP_MIRROR_REASON_INGRESS_WRED = 9,
	/* Packet was mirrored from egress. */
	MLXSW_SP_MIRROR_REASON_EGRESS = 14,
};

static int mlxsw_sp_rx_listener(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
				u16 local_port,
				struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;

	if (unlikely(!mlxsw_sp_port)) {
		dev_warn_ratelimited(mlxsw_sp->bus_info->dev, "Port %d: skb received for non-existent port\n",
				     local_port);
		kfree_skb(skb);
		return -EINVAL;
	}

	skb->dev = mlxsw_sp_port->dev;

	pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	skb->protocol = eth_type_trans(skb, skb->dev);

	return 0;
}

static void mlxsw_sp_rx_drop_listener(struct sk_buff *skb, u16 local_port,
				      void *trap_ctx)
{
	struct devlink_port *in_devlink_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	struct devlink *devlink;
	int err;

	mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];

	err = mlxsw_sp_rx_listener(mlxsw_sp, skb, local_port, mlxsw_sp_port);
	if (err)
		return;

	devlink = priv_to_devlink(mlxsw_sp->core);
	in_devlink_port = mlxsw_core_port_devlink_port_get(mlxsw_sp->core,
							   local_port);
	skb_push(skb, ETH_HLEN);
	devlink_trap_report(devlink, skb, trap_ctx, in_devlink_port, NULL);
	consume_skb(skb);
}

static void mlxsw_sp_rx_acl_drop_listener(struct sk_buff *skb, u16 local_port,
					  void *trap_ctx)
{
	u32 cookie_index = mlxsw_skb_cb(skb)->rx_md_info.cookie_index;
	const struct flow_action_cookie *fa_cookie;
	struct devlink_port *in_devlink_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	struct devlink *devlink;
	int err;

	mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];

	err = mlxsw_sp_rx_listener(mlxsw_sp, skb, local_port, mlxsw_sp_port);
	if (err)
		return;

	devlink = priv_to_devlink(mlxsw_sp->core);
	in_devlink_port = mlxsw_core_port_devlink_port_get(mlxsw_sp->core,
							   local_port);
	skb_push(skb, ETH_HLEN);
	rcu_read_lock();
	fa_cookie = mlxsw_sp_acl_act_cookie_lookup(mlxsw_sp, cookie_index);
	devlink_trap_report(devlink, skb, trap_ctx, in_devlink_port, fa_cookie);
	rcu_read_unlock();
	consume_skb(skb);
}

static int __mlxsw_sp_rx_no_mark_listener(struct sk_buff *skb, u16 local_port,
					  void *trap_ctx)
{
	struct devlink_port *in_devlink_port;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	struct devlink *devlink;
	int err;

	mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];

	err = mlxsw_sp_rx_listener(mlxsw_sp, skb, local_port, mlxsw_sp_port);
	if (err)
		return err;

	devlink = priv_to_devlink(mlxsw_sp->core);
	in_devlink_port = mlxsw_core_port_devlink_port_get(mlxsw_sp->core,
							   local_port);
	skb_push(skb, ETH_HLEN);
	devlink_trap_report(devlink, skb, trap_ctx, in_devlink_port, NULL);
	skb_pull(skb, ETH_HLEN);

	return 0;
}

static void mlxsw_sp_rx_no_mark_listener(struct sk_buff *skb, u16 local_port,
					 void *trap_ctx)
{
	int err;

	err = __mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
	if (err)
		return;

	netif_receive_skb(skb);
}

static void mlxsw_sp_rx_mark_listener(struct sk_buff *skb, u16 local_port,
				      void *trap_ctx)
{
	skb->offload_fwd_mark = 1;
	mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
}

static void mlxsw_sp_rx_l3_mark_listener(struct sk_buff *skb, u16 local_port,
					 void *trap_ctx)
{
	skb->offload_l3_fwd_mark = 1;
	skb->offload_fwd_mark = 1;
	mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
}

static void mlxsw_sp_rx_ptp_listener(struct sk_buff *skb, u16 local_port,
				     void *trap_ctx)
{
	struct mlxsw_sp *mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	int err;

	err = __mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
	if (err)
		return;

	/* The PTP handler expects skb->data to point to the start of the
	 * Ethernet header.
	 */
	skb_push(skb, ETH_HLEN);
	mlxsw_sp_ptp_receive(mlxsw_sp, skb, local_port);
}

static struct mlxsw_sp_port *
mlxsw_sp_sample_tx_port_get(struct mlxsw_sp *mlxsw_sp,
			    const struct mlxsw_rx_md_info *rx_md_info)
{
	u16 local_port;

	if (!rx_md_info->tx_port_valid)
		return NULL;

	if (rx_md_info->tx_port_is_lag)
		local_port = mlxsw_core_lag_mapping_get(mlxsw_sp->core,
							rx_md_info->tx_lag_id,
							rx_md_info->tx_lag_port_index);
	else
		local_port = rx_md_info->tx_sys_port;

	if (local_port >= mlxsw_core_max_ports(mlxsw_sp->core))
		return NULL;

	return mlxsw_sp->ports[local_port];
}

/* The latency units are determined according to MOGCR.mirror_latency_units. It
 * defaults to 64 nanoseconds.
 */
#define MLXSW_SP_MIRROR_LATENCY_SHIFT	6

static void mlxsw_sp_psample_md_init(struct mlxsw_sp *mlxsw_sp,
				     struct psample_metadata *md,
				     struct sk_buff *skb, int in_ifindex,
				     bool truncate, u32 trunc_size)
{
	struct mlxsw_rx_md_info *rx_md_info = &mlxsw_skb_cb(skb)->rx_md_info;
	struct mlxsw_sp_port *mlxsw_sp_port;

	md->trunc_size = truncate ? trunc_size : skb->len;
	md->in_ifindex = in_ifindex;
	mlxsw_sp_port = mlxsw_sp_sample_tx_port_get(mlxsw_sp, rx_md_info);
	md->out_ifindex = mlxsw_sp_port && mlxsw_sp_port->dev ?
			  mlxsw_sp_port->dev->ifindex : 0;
	md->out_tc_valid = rx_md_info->tx_tc_valid;
	md->out_tc = rx_md_info->tx_tc;
	md->out_tc_occ_valid = rx_md_info->tx_congestion_valid;
	md->out_tc_occ = rx_md_info->tx_congestion;
	md->latency_valid = rx_md_info->latency_valid;
	md->latency = rx_md_info->latency;
	md->latency <<= MLXSW_SP_MIRROR_LATENCY_SHIFT;
}

static void mlxsw_sp_rx_sample_listener(struct sk_buff *skb, u16 local_port,
					void *trap_ctx)
{
	struct mlxsw_sp *mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	struct mlxsw_sp_sample_trigger trigger;
	struct mlxsw_sp_sample_params *params;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct psample_metadata md = {};
	int err;

	err = __mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
	if (err)
		return;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		goto out;

	trigger.type = MLXSW_SP_SAMPLE_TRIGGER_TYPE_INGRESS;
	trigger.local_port = local_port;
	params = mlxsw_sp_sample_trigger_params_lookup(mlxsw_sp, &trigger);
	if (!params)
		goto out;

	/* The psample module expects skb->data to point to the start of the
	 * Ethernet header.
	 */
	skb_push(skb, ETH_HLEN);
	mlxsw_sp_psample_md_init(mlxsw_sp, &md, skb,
				 mlxsw_sp_port->dev->ifindex, params->truncate,
				 params->trunc_size);
	psample_sample_packet(params->psample_group, skb, params->rate, &md);
out:
	consume_skb(skb);
}

static void mlxsw_sp_rx_sample_tx_listener(struct sk_buff *skb, u16 local_port,
					   void *trap_ctx)
{
	struct mlxsw_rx_md_info *rx_md_info = &mlxsw_skb_cb(skb)->rx_md_info;
	struct mlxsw_sp *mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	struct mlxsw_sp_port *mlxsw_sp_port, *mlxsw_sp_port_tx;
	struct mlxsw_sp_sample_trigger trigger;
	struct mlxsw_sp_sample_params *params;
	struct psample_metadata md = {};
	int err;

	/* Locally generated packets are not reported from the policy engine
	 * trigger, so do not report them from the egress trigger as well.
	 */
	if (local_port == MLXSW_PORT_CPU_PORT)
		goto out;

	err = __mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
	if (err)
		return;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		goto out;

	/* Packet was sampled from Tx, so we need to retrieve the sample
	 * parameters based on the Tx port and not the Rx port.
	 */
	mlxsw_sp_port_tx = mlxsw_sp_sample_tx_port_get(mlxsw_sp, rx_md_info);
	if (!mlxsw_sp_port_tx)
		goto out;

	trigger.type = MLXSW_SP_SAMPLE_TRIGGER_TYPE_EGRESS;
	trigger.local_port = mlxsw_sp_port_tx->local_port;
	params = mlxsw_sp_sample_trigger_params_lookup(mlxsw_sp, &trigger);
	if (!params)
		goto out;

	/* The psample module expects skb->data to point to the start of the
	 * Ethernet header.
	 */
	skb_push(skb, ETH_HLEN);
	mlxsw_sp_psample_md_init(mlxsw_sp, &md, skb,
				 mlxsw_sp_port->dev->ifindex, params->truncate,
				 params->trunc_size);
	psample_sample_packet(params->psample_group, skb, params->rate, &md);
out:
	consume_skb(skb);
}

static void mlxsw_sp_rx_sample_acl_listener(struct sk_buff *skb, u16 local_port,
					    void *trap_ctx)
{
	struct mlxsw_sp *mlxsw_sp = devlink_trap_ctx_priv(trap_ctx);
	struct mlxsw_sp_sample_trigger trigger = {
		.type = MLXSW_SP_SAMPLE_TRIGGER_TYPE_POLICY_ENGINE,
	};
	struct mlxsw_sp_sample_params *params;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct psample_metadata md = {};
	int err;

	err = __mlxsw_sp_rx_no_mark_listener(skb, local_port, trap_ctx);
	if (err)
		return;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		goto out;

	params = mlxsw_sp_sample_trigger_params_lookup(mlxsw_sp, &trigger);
	if (!params)
		goto out;

	/* The psample module expects skb->data to point to the start of the
	 * Ethernet header.
	 */
	skb_push(skb, ETH_HLEN);
	mlxsw_sp_psample_md_init(mlxsw_sp, &md, skb,
				 mlxsw_sp_port->dev->ifindex, params->truncate,
				 params->trunc_size);
	psample_sample_packet(params->psample_group, skb, params->rate, &md);
out:
	consume_skb(skb);
}

#define MLXSW_SP_TRAP_DROP(_id, _group_id)				      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_DROP_EXT(_id, _group_id, _metadata)		      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA | (_metadata))

#define MLXSW_SP_TRAP_BUFFER_DROP(_id)					      \
	DEVLINK_TRAP_GENERIC(DROP, TRAP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_BUFFER_DROPS,      \
			     MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_DRIVER_DROP(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(DROP, DROP, DEVLINK_MLXSW_TRAP_ID_##_id,	      \
			    DEVLINK_MLXSW_TRAP_NAME_##_id,		      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_EXCEPTION(_id, _group_id)		      \
	DEVLINK_TRAP_GENERIC(EXCEPTION, TRAP, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_CONTROL(_id, _group_id, _action)			      \
	DEVLINK_TRAP_GENERIC(CONTROL, _action, _id,			      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_RXL_DISCARD(_id, _group_id)				      \
	MLXSW_RXL_DIS(mlxsw_sp_rx_drop_listener, DISCARD_##_id,		      \
		      TRAP_EXCEPTION_TO_CPU, false, SP_##_group_id,	      \
		      SET_FW_DEFAULT, SP_##_group_id)

#define MLXSW_SP_RXL_ACL_DISCARD(_id, _en_group_id, _dis_group_id)	      \
	MLXSW_RXL_DIS(mlxsw_sp_rx_acl_drop_listener, DISCARD_##_id,	      \
		      TRAP_EXCEPTION_TO_CPU, false, SP_##_en_group_id,	      \
		      SET_FW_DEFAULT, SP_##_dis_group_id)

#define MLXSW_SP_RXL_BUFFER_DISCARD(_mirror_reason)			      \
	MLXSW_RXL_MIRROR(mlxsw_sp_rx_drop_listener, 0, SP_BUFFER_DISCARDS,    \
			 MLXSW_SP_MIRROR_REASON_##_mirror_reason)

#define MLXSW_SP_RXL_EXCEPTION(_id, _group_id, _action)			      \
	MLXSW_RXL(mlxsw_sp_rx_mark_listener, _id,			      \
		   _action, false, SP_##_group_id, SET_FW_DEFAULT)

#define MLXSW_SP_RXL_NO_MARK(_id, _group_id, _action, _is_ctrl)		      \
	MLXSW_RXL(mlxsw_sp_rx_no_mark_listener, _id, _action,		      \
		  _is_ctrl, SP_##_group_id, DISCARD)

#define MLXSW_SP_RXL_MARK(_id, _group_id, _action, _is_ctrl)		      \
	MLXSW_RXL(mlxsw_sp_rx_mark_listener, _id, _action, _is_ctrl,	      \
		  SP_##_group_id, DISCARD)

#define MLXSW_SP_RXL_L3_MARK(_id, _group_id, _action, _is_ctrl)		      \
	MLXSW_RXL(mlxsw_sp_rx_l3_mark_listener, _id, _action, _is_ctrl,	      \
		  SP_##_group_id, DISCARD)

#define MLXSW_SP_TRAP_POLICER(_id, _rate, _burst)			      \
	DEVLINK_TRAP_POLICER(_id, _rate, _burst,			      \
			     MLXSW_REG_QPCR_HIGHEST_CIR,		      \
			     MLXSW_REG_QPCR_LOWEST_CIR,			      \
			     1 << MLXSW_REG_QPCR_HIGHEST_CBS,		      \
			     1 << MLXSW_REG_QPCR_LOWEST_CBS)

/* Ordered by policer identifier */
static const struct mlxsw_sp_trap_policer_item
mlxsw_sp_trap_policer_items_arr[] = {
	{
		.policer = MLXSW_SP_TRAP_POLICER(1, 10 * 1024, 4096),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(2, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(3, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(4, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(5, 16 * 1024, 8192),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(6, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(7, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(8, 20 * 1024, 8192),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(9, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(10, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(11, 256, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(12, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(13, 128, 128),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(14, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(15, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(16, 24 * 1024, 16384),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(17, 19 * 1024, 8192),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(18, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(19, 1024, 512),
	},
	{
		.policer = MLXSW_SP_TRAP_POLICER(20, 10240, 4096),
	},
};

static const struct mlxsw_sp_trap_group_item mlxsw_sp_trap_group_items_arr[] = {
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS, 1),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_L2_DISCARDS,
		.priority = 0,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(L3_DROPS, 1),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_L3_DISCARDS,
		.priority = 0,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(L3_EXCEPTIONS, 1),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_L3_EXCEPTIONS,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(TUNNEL_DROPS, 1),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_TUNNEL_DISCARDS,
		.priority = 0,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(ACL_DROPS, 1),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_ACL_DISCARDS,
		.priority = 0,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(STP, 2),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_STP,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(LACP, 3),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_LACP,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(LLDP, 4),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_LLDP,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(MC_SNOOPING, 5),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_MC_SNOOPING,
		.priority = 3,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(DHCP, 6),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_DHCP,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(NEIGH_DISCOVERY, 7),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_NEIGH_DISCOVERY,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(BFD, 8),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_BFD,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(OSPF, 9),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_OSPF,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(BGP, 10),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_BGP,
		.priority = 4,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(VRRP, 11),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_VRRP,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(PIM, 12),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_PIM,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(UC_LB, 13),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_LBERROR,
		.priority = 0,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(LOCAL_DELIVERY, 14),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_IP2ME,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(EXTERNAL_DELIVERY, 19),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_EXTERNAL_ROUTE,
		.priority = 1,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(IPV6, 15),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_IPV6,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(PTP_EVENT, 16),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_PTP0,
		.priority = 5,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(PTP_GENERAL, 17),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_PTP1,
		.priority = 2,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(ACL_TRAP, 18),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_FLOW_LOGGING,
		.priority = 4,
	},
};

static const struct mlxsw_sp_trap_item mlxsw_sp_trap_items_arr[] = {
	{
		.trap = MLXSW_SP_TRAP_DROP(SMAC_MC, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_PACKET_SMAC_MC, L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(VLAN_TAG_MISMATCH, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_SWITCH_VTAG_ALLOW,
					     L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(INGRESS_VLAN_FILTER, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_SWITCH_VLAN, L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(INGRESS_STP_FILTER, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_SWITCH_STP, L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(EMPTY_TX_LIST, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_UC, L2_DISCARDS),
			MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_MC_NULL, L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(PORT_LOOPBACK_FILTER, L2_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_LB, L2_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(BLACKHOLE_ROUTE, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ROUTER2, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(NON_IP_PACKET, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_NON_IP_PACKET,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(UC_DIP_MC_DMAC, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_UC_DIP_MC_DMAC,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(DIP_LB, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_DIP_LB, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(SIP_MC, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_SIP_MC, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(SIP_LB, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_SIP_LB, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(CORRUPTED_IP_HDR, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_CORRUPTED_IP_HDR,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(IPV4_SIP_BC, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ING_ROUTER_IPV4_SIP_BC,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(IPV6_MC_DIP_RESERVED_SCOPE,
					   L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(IPV6_MC_DIP_RESERVED_SCOPE,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE,
					   L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE,
					     L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(MTU_ERROR, L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(MTUERROR, L3_EXCEPTIONS,
					       TRAP_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(TTL_ERROR, L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(TTLERROR, L3_EXCEPTIONS,
					       TRAP_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(RPF, L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(RPF, L3_EXCEPTIONS, TRAP_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(REJECT_ROUTE, L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(RTR_INGRESS1, L3_EXCEPTIONS,
					       TRAP_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(UNRESOLVED_NEIGH,
						L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(HOST_MISS_IPV4, L3_EXCEPTIONS,
					       TRAP_TO_CPU),
			MLXSW_SP_RXL_EXCEPTION(HOST_MISS_IPV6, L3_EXCEPTIONS,
					       TRAP_TO_CPU),
			MLXSW_SP_RXL_EXCEPTION(RTR_EGRESS0, L3_EXCEPTIONS,
					       TRAP_EXCEPTION_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(IPV4_LPM_UNICAST_MISS,
						L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(DISCARD_ROUTER_LPM4,
					       L3_EXCEPTIONS,
					       TRAP_EXCEPTION_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(IPV6_LPM_UNICAST_MISS,
						L3_EXCEPTIONS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(DISCARD_ROUTER_LPM6,
					       L3_EXCEPTIONS,
					       TRAP_EXCEPTION_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DRIVER_DROP(IRIF_DISABLED, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ROUTER_IRIF_EN, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DRIVER_DROP(ERIF_DISABLED, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ROUTER_ERIF_EN, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(NON_ROUTABLE, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(NON_ROUTABLE, L3_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_EXCEPTION(DECAP_ERROR, TUNNEL_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_EXCEPTION(DECAP_ECN0, TUNNEL_DISCARDS,
					       TRAP_EXCEPTION_TO_CPU),
			MLXSW_SP_RXL_EXCEPTION(IPIP_DECAP_ERROR,
					       TUNNEL_DISCARDS,
					       TRAP_EXCEPTION_TO_CPU),
			MLXSW_SP_RXL_EXCEPTION(DISCARD_DEC_PKT, TUNNEL_DISCARDS,
					       TRAP_EXCEPTION_TO_CPU),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(OVERLAY_SMAC_MC, TUNNEL_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(OVERLAY_SMAC_MC, TUNNEL_DISCARDS),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP_EXT(INGRESS_FLOW_ACTION_DROP,
					       ACL_DROPS,
					       DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
		.listeners_arr = {
			MLXSW_SP_RXL_ACL_DISCARD(INGRESS_ACL, ACL_DISCARDS,
						 DUMMY),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP_EXT(EGRESS_FLOW_ACTION_DROP,
					       ACL_DROPS,
					       DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
		.listeners_arr = {
			MLXSW_SP_RXL_ACL_DISCARD(EGRESS_ACL, ACL_DISCARDS,
						 DUMMY),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(STP, STP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(STP, STP, TRAP_TO_CPU, true),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(LACP, LACP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(LACP, LACP, TRAP_TO_CPU, true),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(LLDP, LLDP, TRAP),
		.listeners_arr = {
			MLXSW_RXL(mlxsw_sp_rx_ptp_listener, LLDP, TRAP_TO_CPU,
				  true, SP_LLDP, DISCARD),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IGMP_QUERY, MC_SNOOPING, MIRROR),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IGMP_QUERY, MC_SNOOPING,
					  MIRROR_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IGMP_V1_REPORT, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IGMP_V1_REPORT, MC_SNOOPING,
					     TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IGMP_V2_REPORT, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IGMP_V2_REPORT, MC_SNOOPING,
					     TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IGMP_V3_REPORT, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IGMP_V3_REPORT, MC_SNOOPING,
					     TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IGMP_V2_LEAVE, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IGMP_V2_LEAVE, MC_SNOOPING,
					     TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(MLD_QUERY, MC_SNOOPING, MIRROR),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_MLDV12_LISTENER_QUERY,
					  MC_SNOOPING, MIRROR_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(MLD_V1_REPORT, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IPV6_MLDV1_LISTENER_REPORT,
					     MC_SNOOPING, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(MLD_V2_REPORT, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IPV6_MLDV2_LISTENER_REPORT,
					     MC_SNOOPING, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(MLD_V1_DONE, MC_SNOOPING,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(IPV6_MLDV1_LISTENER_DONE,
					     MC_SNOOPING, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_DHCP, DHCP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_DHCP, DHCP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_DHCP, DHCP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_DHCP, DHCP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(ARP_REQUEST, NEIGH_DISCOVERY,
					      MIRROR),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(ROUTER_ARPBC, NEIGH_DISCOVERY,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(ARP_RESPONSE, NEIGH_DISCOVERY,
					      MIRROR),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(ROUTER_ARPUC, NEIGH_DISCOVERY,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(ARP_OVERLAY, NEIGH_DISCOVERY,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(NVE_DECAP_ARP, NEIGH_DISCOVERY,
					     TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_NEIGH_SOLICIT,
					      NEIGH_DISCOVERY, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(L3_IPV6_NEIGHBOR_SOLICITATION,
					  NEIGH_DISCOVERY, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_NEIGH_ADVERT,
					      NEIGH_DISCOVERY, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(L3_IPV6_NEIGHBOR_ADVERTISEMENT,
					  NEIGH_DISCOVERY, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_BFD, BFD, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_BFD, BFD, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_BFD, BFD, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_BFD, BFD, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_OSPF, OSPF, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_OSPF, OSPF, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_OSPF, OSPF, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_OSPF, OSPF, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_BGP, BGP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_BGP, BGP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_BGP, BGP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_BGP, BGP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_VRRP, VRRP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_VRRP, VRRP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_VRRP, VRRP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_VRRP, VRRP, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_PIM, PIM, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV4_PIM, PIM, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_PIM, PIM, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_PIM, PIM, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(UC_LB, UC_LB, MIRROR),
		.listeners_arr = {
			MLXSW_SP_RXL_L3_MARK(LBERROR, LBERROR, MIRROR_TO_CPU,
					     false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(LOCAL_ROUTE, LOCAL_DELIVERY,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IP2ME, IP2ME, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(EXTERNAL_ROUTE, EXTERNAL_DELIVERY,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(RTR_INGRESS0, EXTERNAL_ROUTE,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_UC_DIP_LINK_LOCAL_SCOPE,
					      LOCAL_DELIVERY, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_LINK_LOCAL_DEST, IP2ME,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV4_ROUTER_ALERT, LOCAL_DELIVERY,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(ROUTER_ALERT_IPV4, IP2ME, TRAP_TO_CPU,
					  false),
		},
	},
	{
		/* IPV6_ROUTER_ALERT is defined in uAPI as 22, but it is not
		 * used in this file, so undefine it.
		 */
		#undef IPV6_ROUTER_ALERT
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_ROUTER_ALERT, LOCAL_DELIVERY,
					      TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(ROUTER_ALERT_IPV6, IP2ME, TRAP_TO_CPU,
					  false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_DIP_ALL_NODES, IPV6, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_ALL_NODES_LINK, IPV6,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_DIP_ALL_ROUTERS, IPV6, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(IPV6_ALL_ROUTERS_LINK, IPV6,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_ROUTER_SOLICIT, IPV6, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(L3_IPV6_ROUTER_SOLICITATION, IPV6,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_ROUTER_ADVERT, IPV6, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(L3_IPV6_ROUTER_ADVERTISEMENT, IPV6,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(IPV6_REDIRECT, IPV6, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_MARK(L3_IPV6_REDIRECTION, IPV6,
					  TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(PTP_EVENT, PTP_EVENT, TRAP),
		.listeners_arr = {
			MLXSW_RXL(mlxsw_sp_rx_ptp_listener, PTP0, TRAP_TO_CPU,
				  false, SP_PTP0, DISCARD),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(PTP_GENERAL, PTP_GENERAL, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(PTP1, PTP1, TRAP_TO_CPU, false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(FLOW_ACTION_TRAP, ACL_TRAP, TRAP),
		.listeners_arr = {
			MLXSW_SP_RXL_NO_MARK(ACL0, FLOW_LOGGING, TRAP_TO_CPU,
					     false),
		},
	},
	{
		.trap = MLXSW_SP_TRAP_DROP(BLACKHOLE_NEXTHOP, L3_DROPS),
		.listeners_arr = {
			MLXSW_SP_RXL_DISCARD(ROUTER3, L3_DISCARDS),
		},
	},
};

static struct mlxsw_sp_trap_policer_item *
mlxsw_sp_trap_policer_item_lookup(struct mlxsw_sp *mlxsw_sp, u32 id)
{
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = 0; i < trap->policers_count; i++) {
		if (trap->policer_items_arr[i].policer.id == id)
			return &trap->policer_items_arr[i];
	}

	return NULL;
}

static struct mlxsw_sp_trap_group_item *
mlxsw_sp_trap_group_item_lookup(struct mlxsw_sp *mlxsw_sp, u16 id)
{
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = 0; i < trap->groups_count; i++) {
		if (trap->group_items_arr[i].group.id == id)
			return &trap->group_items_arr[i];
	}

	return NULL;
}

static struct mlxsw_sp_trap_item *
mlxsw_sp_trap_item_lookup(struct mlxsw_sp *mlxsw_sp, u16 id)
{
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = 0; i < trap->traps_count; i++) {
		if (trap->trap_items_arr[i].trap.id == id)
			return &trap->trap_items_arr[i];
	}

	return NULL;
}

static int mlxsw_sp_trap_cpu_policers_set(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	char qpcr_pl[MLXSW_REG_QPCR_LEN];
	u16 hw_id;

	/* The purpose of "thin" policer is to drop as many packets
	 * as possible. The dummy group is using it.
	 */
	hw_id = find_first_zero_bit(trap->policers_usage, trap->max_policers);
	if (WARN_ON(hw_id == trap->max_policers))
		return -ENOBUFS;

	__set_bit(hw_id, trap->policers_usage);
	trap->thin_policer_hw_id = hw_id;
	mlxsw_reg_qpcr_pack(qpcr_pl, hw_id, MLXSW_REG_QPCR_IR_UNITS_M,
			    false, 1, 4);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
}

static int mlxsw_sp_trap_dummy_group_init(struct mlxsw_sp *mlxsw_sp)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_SP_DUMMY,
			    mlxsw_sp->trap->thin_policer_hw_id, 0, 1);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
}

static int mlxsw_sp_trap_policer_items_arr_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t arr_size = ARRAY_SIZE(mlxsw_sp_trap_policer_items_arr);
	size_t elem_size = sizeof(struct mlxsw_sp_trap_policer_item);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	size_t free_policers = 0;
	u32 last_id;
	int i;

	for_each_clear_bit(i, trap->policers_usage, trap->max_policers)
		free_policers++;

	if (arr_size > free_policers) {
		dev_err(mlxsw_sp->bus_info->dev, "Exceeded number of supported packet trap policers\n");
		return -ENOBUFS;
	}

	trap->policer_items_arr = kcalloc(free_policers, elem_size, GFP_KERNEL);
	if (!trap->policer_items_arr)
		return -ENOMEM;

	trap->policers_count = free_policers;

	/* Initialize policer items array with pre-defined policers. */
	memcpy(trap->policer_items_arr, mlxsw_sp_trap_policer_items_arr,
	       elem_size * arr_size);

	/* Initialize policer items array with the rest of the available
	 * policers.
	 */
	last_id = mlxsw_sp_trap_policer_items_arr[arr_size - 1].policer.id;
	for (i = arr_size; i < trap->policers_count; i++) {
		const struct mlxsw_sp_trap_policer_item *policer_item;

		/* Use parameters set for first policer and override
		 * relevant ones.
		 */
		policer_item = &mlxsw_sp_trap_policer_items_arr[0];
		trap->policer_items_arr[i] = *policer_item;
		trap->policer_items_arr[i].policer.id = ++last_id;
		trap->policer_items_arr[i].policer.init_rate = 1;
		trap->policer_items_arr[i].policer.init_burst = 16;
	}

	return 0;
}

static void mlxsw_sp_trap_policer_items_arr_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->trap->policer_items_arr);
}

static int mlxsw_sp_trap_policers_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	const struct mlxsw_sp_trap_policer_item *policer_item;
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int err, i;

	err = mlxsw_sp_trap_policer_items_arr_init(mlxsw_sp);
	if (err)
		return err;

	for (i = 0; i < trap->policers_count; i++) {
		policer_item = &trap->policer_items_arr[i];
		err = devlink_trap_policers_register(devlink,
						     &policer_item->policer, 1);
		if (err)
			goto err_trap_policer_register;
	}

	return 0;

err_trap_policer_register:
	for (i--; i >= 0; i--) {
		policer_item = &trap->policer_items_arr[i];
		devlink_trap_policers_unregister(devlink,
						 &policer_item->policer, 1);
	}
	mlxsw_sp_trap_policer_items_arr_fini(mlxsw_sp);
	return err;
}

static void mlxsw_sp_trap_policers_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	const struct mlxsw_sp_trap_policer_item *policer_item;
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = trap->policers_count - 1; i >= 0; i--) {
		policer_item = &trap->policer_items_arr[i];
		devlink_trap_policers_unregister(devlink,
						 &policer_item->policer, 1);
	}
	mlxsw_sp_trap_policer_items_arr_fini(mlxsw_sp);
}

static int mlxsw_sp_trap_group_items_arr_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t common_groups_count = ARRAY_SIZE(mlxsw_sp_trap_group_items_arr);
	const struct mlxsw_sp_trap_group_item *spec_group_items_arr;
	size_t elem_size = sizeof(struct mlxsw_sp_trap_group_item);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	size_t groups_count, spec_groups_count;
	int err;

	err = mlxsw_sp->trap_ops->groups_init(mlxsw_sp, &spec_group_items_arr,
					      &spec_groups_count);
	if (err)
		return err;

	/* The group items array is created by concatenating the common trap
	 * group items and the ASIC-specific trap group items.
	 */
	groups_count = common_groups_count + spec_groups_count;
	trap->group_items_arr = kcalloc(groups_count, elem_size, GFP_KERNEL);
	if (!trap->group_items_arr)
		return -ENOMEM;

	memcpy(trap->group_items_arr, mlxsw_sp_trap_group_items_arr,
	       elem_size * common_groups_count);
	memcpy(trap->group_items_arr + common_groups_count,
	       spec_group_items_arr, elem_size * spec_groups_count);

	trap->groups_count = groups_count;

	return 0;
}

static void mlxsw_sp_trap_group_items_arr_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->trap->group_items_arr);
}

static int mlxsw_sp_trap_groups_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	const struct mlxsw_sp_trap_group_item *group_item;
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int err, i;

	err = mlxsw_sp_trap_group_items_arr_init(mlxsw_sp);
	if (err)
		return err;

	for (i = 0; i < trap->groups_count; i++) {
		group_item = &trap->group_items_arr[i];
		err = devlink_trap_groups_register(devlink, &group_item->group,
						   1);
		if (err)
			goto err_trap_group_register;
	}

	return 0;

err_trap_group_register:
	for (i--; i >= 0; i--) {
		group_item = &trap->group_items_arr[i];
		devlink_trap_groups_unregister(devlink, &group_item->group, 1);
	}
	mlxsw_sp_trap_group_items_arr_fini(mlxsw_sp);
	return err;
}

static void mlxsw_sp_trap_groups_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = trap->groups_count - 1; i >= 0; i--) {
		const struct mlxsw_sp_trap_group_item *group_item;

		group_item = &trap->group_items_arr[i];
		devlink_trap_groups_unregister(devlink, &group_item->group, 1);
	}
	mlxsw_sp_trap_group_items_arr_fini(mlxsw_sp);
}

static bool
mlxsw_sp_trap_listener_is_valid(const struct mlxsw_listener *listener)
{
	return listener->trap_id != 0;
}

static int mlxsw_sp_trap_items_arr_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t common_traps_count = ARRAY_SIZE(mlxsw_sp_trap_items_arr);
	const struct mlxsw_sp_trap_item *spec_trap_items_arr;
	size_t elem_size = sizeof(struct mlxsw_sp_trap_item);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	size_t traps_count, spec_traps_count;
	int err;

	err = mlxsw_sp->trap_ops->traps_init(mlxsw_sp, &spec_trap_items_arr,
					     &spec_traps_count);
	if (err)
		return err;

	/* The trap items array is created by concatenating the common trap
	 * items and the ASIC-specific trap items.
	 */
	traps_count = common_traps_count + spec_traps_count;
	trap->trap_items_arr = kcalloc(traps_count, elem_size, GFP_KERNEL);
	if (!trap->trap_items_arr)
		return -ENOMEM;

	memcpy(trap->trap_items_arr, mlxsw_sp_trap_items_arr,
	       elem_size * common_traps_count);
	memcpy(trap->trap_items_arr + common_traps_count,
	       spec_trap_items_arr, elem_size * spec_traps_count);

	trap->traps_count = traps_count;

	return 0;
}

static void mlxsw_sp_trap_items_arr_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->trap->trap_items_arr);
}

static int mlxsw_sp_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	const struct mlxsw_sp_trap_item *trap_item;
	int err, i;

	err = mlxsw_sp_trap_items_arr_init(mlxsw_sp);
	if (err)
		return err;

	for (i = 0; i < trap->traps_count; i++) {
		trap_item = &trap->trap_items_arr[i];
		err = devlink_traps_register(devlink, &trap_item->trap, 1,
					     mlxsw_sp);
		if (err)
			goto err_trap_register;
	}

	return 0;

err_trap_register:
	for (i--; i >= 0; i--) {
		trap_item = &trap->trap_items_arr[i];
		devlink_traps_unregister(devlink, &trap_item->trap, 1);
	}
	mlxsw_sp_trap_items_arr_fini(mlxsw_sp);
	return err;
}

static void mlxsw_sp_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	int i;

	for (i = trap->traps_count - 1; i >= 0; i--) {
		const struct mlxsw_sp_trap_item *trap_item;

		trap_item = &trap->trap_items_arr[i];
		devlink_traps_unregister(devlink, &trap_item->trap, 1);
	}
	mlxsw_sp_trap_items_arr_fini(mlxsw_sp);
}

int mlxsw_sp_devlink_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = mlxsw_sp_trap_cpu_policers_set(mlxsw_sp);
	if (err)
		return err;

	err = mlxsw_sp_trap_dummy_group_init(mlxsw_sp);
	if (err)
		return err;

	err = mlxsw_sp_trap_policers_init(mlxsw_sp);
	if (err)
		return err;

	err = mlxsw_sp_trap_groups_init(mlxsw_sp);
	if (err)
		goto err_trap_groups_init;

	err = mlxsw_sp_traps_init(mlxsw_sp);
	if (err)
		goto err_traps_init;

	return 0;

err_traps_init:
	mlxsw_sp_trap_groups_fini(mlxsw_sp);
err_trap_groups_init:
	mlxsw_sp_trap_policers_fini(mlxsw_sp);
	return err;
}

void mlxsw_sp_devlink_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_traps_fini(mlxsw_sp);
	mlxsw_sp_trap_groups_fini(mlxsw_sp);
	mlxsw_sp_trap_policers_fini(mlxsw_sp);
}

int mlxsw_sp_trap_init(struct mlxsw_core *mlxsw_core,
		       const struct devlink_trap *trap, void *trap_ctx)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	const struct mlxsw_sp_trap_item *trap_item;
	int i;

	trap_item = mlxsw_sp_trap_item_lookup(mlxsw_sp, trap->id);
	if (WARN_ON(!trap_item))
		return -EINVAL;

	for (i = 0; i < MLXSW_SP_TRAP_LISTENERS_MAX; i++) {
		const struct mlxsw_listener *listener;
		int err;

		listener = &trap_item->listeners_arr[i];
		if (!mlxsw_sp_trap_listener_is_valid(listener))
			continue;
		err = mlxsw_core_trap_register(mlxsw_core, listener, trap_ctx);
		if (err)
			return err;
	}

	return 0;
}

void mlxsw_sp_trap_fini(struct mlxsw_core *mlxsw_core,
			const struct devlink_trap *trap, void *trap_ctx)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	const struct mlxsw_sp_trap_item *trap_item;
	int i;

	trap_item = mlxsw_sp_trap_item_lookup(mlxsw_sp, trap->id);
	if (WARN_ON(!trap_item))
		return;

	for (i = MLXSW_SP_TRAP_LISTENERS_MAX - 1; i >= 0; i--) {
		const struct mlxsw_listener *listener;

		listener = &trap_item->listeners_arr[i];
		if (!mlxsw_sp_trap_listener_is_valid(listener))
			continue;
		mlxsw_core_trap_unregister(mlxsw_core, listener, trap_ctx);
	}
}

int mlxsw_sp_trap_action_set(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap *trap,
			     enum devlink_trap_action action,
			     struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	const struct mlxsw_sp_trap_item *trap_item;
	int i;

	trap_item = mlxsw_sp_trap_item_lookup(mlxsw_sp, trap->id);
	if (WARN_ON(!trap_item))
		return -EINVAL;

	if (trap_item->is_source) {
		NL_SET_ERR_MSG_MOD(extack, "Changing the action of source traps is not supported");
		return -EOPNOTSUPP;
	}

	for (i = 0; i < MLXSW_SP_TRAP_LISTENERS_MAX; i++) {
		const struct mlxsw_listener *listener;
		bool enabled;
		int err;

		listener = &trap_item->listeners_arr[i];
		if (!mlxsw_sp_trap_listener_is_valid(listener))
			continue;

		switch (action) {
		case DEVLINK_TRAP_ACTION_DROP:
			enabled = false;
			break;
		case DEVLINK_TRAP_ACTION_TRAP:
			enabled = true;
			break;
		default:
			return -EINVAL;
		}
		err = mlxsw_core_trap_state_set(mlxsw_core, listener, enabled);
		if (err)
			return err;
	}

	return 0;
}

static int
__mlxsw_sp_trap_group_init(struct mlxsw_core *mlxsw_core,
			   const struct devlink_trap_group *group,
			   u32 policer_id, struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	u16 hw_policer_id = MLXSW_REG_HTGT_INVALID_POLICER;
	const struct mlxsw_sp_trap_group_item *group_item;
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	group_item = mlxsw_sp_trap_group_item_lookup(mlxsw_sp, group->id);
	if (WARN_ON(!group_item))
		return -EINVAL;

	if (group_item->fixed_policer && policer_id != group->init_policer_id) {
		NL_SET_ERR_MSG_MOD(extack, "Changing the policer binding of this group is not supported");
		return -EOPNOTSUPP;
	}

	if (policer_id) {
		struct mlxsw_sp_trap_policer_item *policer_item;

		policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp,
								 policer_id);
		if (WARN_ON(!policer_item))
			return -EINVAL;
		hw_policer_id = policer_item->hw_id;
	}

	mlxsw_reg_htgt_pack(htgt_pl, group_item->hw_group_id, hw_policer_id,
			    group_item->priority, group_item->priority);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
}

int mlxsw_sp_trap_group_init(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap_group *group)
{
	return __mlxsw_sp_trap_group_init(mlxsw_core, group,
					  group->init_policer_id, NULL);
}

int mlxsw_sp_trap_group_set(struct mlxsw_core *mlxsw_core,
			    const struct devlink_trap_group *group,
			    const struct devlink_trap_policer *policer,
			    struct netlink_ext_ack *extack)
{
	u32 policer_id = policer ? policer->id : 0;

	return __mlxsw_sp_trap_group_init(mlxsw_core, group, policer_id,
					  extack);
}

static int
mlxsw_sp_trap_policer_item_init(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_trap_policer_item *policer_item)
{
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	u16 hw_id;

	/* We should be able to allocate a policer because the number of
	 * policers we registered with devlink is in according with the number
	 * of available policers.
	 */
	hw_id = find_first_zero_bit(trap->policers_usage, trap->max_policers);
	if (WARN_ON(hw_id == trap->max_policers))
		return -ENOBUFS;

	__set_bit(hw_id, trap->policers_usage);
	policer_item->hw_id = hw_id;

	return 0;
}

static void
mlxsw_sp_trap_policer_item_fini(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_trap_policer_item *policer_item)
{
	__clear_bit(policer_item->hw_id, mlxsw_sp->trap->policers_usage);
}

static int mlxsw_sp_trap_policer_bs(u64 burst, u8 *p_burst_size,
				    struct netlink_ext_ack *extack)
{
	int bs = fls64(burst) - 1;

	if (burst != (BIT_ULL(bs))) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size is not power of two");
		return -EINVAL;
	}

	*p_burst_size = bs;

	return 0;
}

static int __mlxsw_sp_trap_policer_set(struct mlxsw_sp *mlxsw_sp, u16 hw_id,
				       u64 rate, u64 burst, bool clear_counter,
				       struct netlink_ext_ack *extack)
{
	char qpcr_pl[MLXSW_REG_QPCR_LEN];
	u8 burst_size;
	int err;

	err = mlxsw_sp_trap_policer_bs(burst, &burst_size, extack);
	if (err)
		return err;

	mlxsw_reg_qpcr_pack(qpcr_pl, hw_id, MLXSW_REG_QPCR_IR_UNITS_M, false,
			    rate, burst_size);
	mlxsw_reg_qpcr_clear_counter_set(qpcr_pl, clear_counter);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
}

int mlxsw_sp_trap_policer_init(struct mlxsw_core *mlxsw_core,
			       const struct devlink_trap_policer *policer)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_trap_policer_item *policer_item;
	int err;

	policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp, policer->id);
	if (WARN_ON(!policer_item))
		return -EINVAL;

	err = mlxsw_sp_trap_policer_item_init(mlxsw_sp, policer_item);
	if (err)
		return err;

	err = __mlxsw_sp_trap_policer_set(mlxsw_sp, policer_item->hw_id,
					  policer->init_rate,
					  policer->init_burst, true, NULL);
	if (err)
		goto err_trap_policer_set;

	return 0;

err_trap_policer_set:
	mlxsw_sp_trap_policer_item_fini(mlxsw_sp, policer_item);
	return err;
}

void mlxsw_sp_trap_policer_fini(struct mlxsw_core *mlxsw_core,
				const struct devlink_trap_policer *policer)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_trap_policer_item *policer_item;

	policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp, policer->id);
	if (WARN_ON(!policer_item))
		return;

	mlxsw_sp_trap_policer_item_fini(mlxsw_sp, policer_item);
}

int mlxsw_sp_trap_policer_set(struct mlxsw_core *mlxsw_core,
			      const struct devlink_trap_policer *policer,
			      u64 rate, u64 burst,
			      struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_trap_policer_item *policer_item;

	policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp, policer->id);
	if (WARN_ON(!policer_item))
		return -EINVAL;

	return __mlxsw_sp_trap_policer_set(mlxsw_sp, policer_item->hw_id,
					   rate, burst, false, extack);
}

int
mlxsw_sp_trap_policer_counter_get(struct mlxsw_core *mlxsw_core,
				  const struct devlink_trap_policer *policer,
				  u64 *p_drops)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_trap_policer_item *policer_item;
	char qpcr_pl[MLXSW_REG_QPCR_LEN];
	int err;

	policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp, policer->id);
	if (WARN_ON(!policer_item))
		return -EINVAL;

	mlxsw_reg_qpcr_pack(qpcr_pl, policer_item->hw_id,
			    MLXSW_REG_QPCR_IR_UNITS_M, false, 0, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
	if (err)
		return err;

	*p_drops = mlxsw_reg_qpcr_violate_count_get(qpcr_pl);

	return 0;
}

int mlxsw_sp_trap_group_policer_hw_id_get(struct mlxsw_sp *mlxsw_sp, u16 id,
					  bool *p_enabled, u16 *p_hw_id)
{
	struct mlxsw_sp_trap_policer_item *pol_item;
	struct mlxsw_sp_trap_group_item *gr_item;
	u32 pol_id;

	gr_item = mlxsw_sp_trap_group_item_lookup(mlxsw_sp, id);
	if (!gr_item)
		return -ENOENT;

	pol_id = gr_item->group.init_policer_id;
	if (!pol_id) {
		*p_enabled = false;
		return 0;
	}

	pol_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp, pol_id);
	if (WARN_ON(!pol_item))
		return -ENOENT;

	*p_enabled = true;
	*p_hw_id = pol_item->hw_id;
	return 0;
}

static const struct mlxsw_sp_trap_group_item
mlxsw_sp1_trap_group_items_arr[] = {
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(ACL_SAMPLE, 0),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_PKT_SAMPLE,
		.priority = 0,
	},
};

static const struct mlxsw_sp_trap_item
mlxsw_sp1_trap_items_arr[] = {
	{
		.trap = MLXSW_SP_TRAP_CONTROL(FLOW_ACTION_SAMPLE, ACL_SAMPLE,
					      MIRROR),
		.listeners_arr = {
			MLXSW_RXL(mlxsw_sp_rx_sample_listener, PKT_SAMPLE,
				  MIRROR_TO_CPU, false, SP_PKT_SAMPLE, DISCARD),
		},
	},
};

static int
mlxsw_sp1_trap_groups_init(struct mlxsw_sp *mlxsw_sp,
			   const struct mlxsw_sp_trap_group_item **arr,
			   size_t *p_groups_count)
{
	*arr = mlxsw_sp1_trap_group_items_arr;
	*p_groups_count = ARRAY_SIZE(mlxsw_sp1_trap_group_items_arr);

	return 0;
}

static int mlxsw_sp1_traps_init(struct mlxsw_sp *mlxsw_sp,
				const struct mlxsw_sp_trap_item **arr,
				size_t *p_traps_count)
{
	*arr = mlxsw_sp1_trap_items_arr;
	*p_traps_count = ARRAY_SIZE(mlxsw_sp1_trap_items_arr);

	return 0;
}

const struct mlxsw_sp_trap_ops mlxsw_sp1_trap_ops = {
	.groups_init = mlxsw_sp1_trap_groups_init,
	.traps_init = mlxsw_sp1_traps_init,
};

static const struct mlxsw_sp_trap_group_item
mlxsw_sp2_trap_group_items_arr[] = {
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(BUFFER_DROPS, 20),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_BUFFER_DISCARDS,
		.priority = 0,
		.fixed_policer = true,
	},
	{
		.group = DEVLINK_TRAP_GROUP_GENERIC(ACL_SAMPLE, 0),
		.hw_group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_PKT_SAMPLE,
		.priority = 0,
		.fixed_policer = true,
	},
};

static const struct mlxsw_sp_trap_item
mlxsw_sp2_trap_items_arr[] = {
	{
		.trap = MLXSW_SP_TRAP_BUFFER_DROP(EARLY_DROP),
		.listeners_arr = {
			MLXSW_SP_RXL_BUFFER_DISCARD(INGRESS_WRED),
		},
		.is_source = true,
	},
	{
		.trap = MLXSW_SP_TRAP_CONTROL(FLOW_ACTION_SAMPLE, ACL_SAMPLE,
					      MIRROR),
		.listeners_arr = {
			MLXSW_RXL_MIRROR(mlxsw_sp_rx_sample_listener, 1,
					 SP_PKT_SAMPLE,
					 MLXSW_SP_MIRROR_REASON_INGRESS),
			MLXSW_RXL_MIRROR(mlxsw_sp_rx_sample_tx_listener, 1,
					 SP_PKT_SAMPLE,
					 MLXSW_SP_MIRROR_REASON_EGRESS),
			MLXSW_RXL_MIRROR(mlxsw_sp_rx_sample_acl_listener, 1,
					 SP_PKT_SAMPLE,
					 MLXSW_SP_MIRROR_REASON_POLICY_ENGINE),
		},
	},
};

static int
mlxsw_sp2_trap_groups_init(struct mlxsw_sp *mlxsw_sp,
			   const struct mlxsw_sp_trap_group_item **arr,
			   size_t *p_groups_count)
{
	*arr = mlxsw_sp2_trap_group_items_arr;
	*p_groups_count = ARRAY_SIZE(mlxsw_sp2_trap_group_items_arr);

	return 0;
}

static int mlxsw_sp2_traps_init(struct mlxsw_sp *mlxsw_sp,
				const struct mlxsw_sp_trap_item **arr,
				size_t *p_traps_count)
{
	*arr = mlxsw_sp2_trap_items_arr;
	*p_traps_count = ARRAY_SIZE(mlxsw_sp2_trap_items_arr);

	return 0;
}

const struct mlxsw_sp_trap_ops mlxsw_sp2_trap_ops = {
	.groups_init = mlxsw_sp2_trap_groups_init,
	.traps_init = mlxsw_sp2_traps_init,
};
