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

static int mlxsw_sp_rx_listener(struct mlxsw_sp *mlxsw_sp, struct sk_buff *skb,
				u8 local_port,
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

static void mlxsw_sp_rx_drop_listener(struct sk_buff *skb, u8 local_port,
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

static void mlxsw_sp_rx_acl_drop_listener(struct sk_buff *skb, u8 local_port,
					  void *trap_ctx)
{
	u32 cookie_index = mlxsw_skb_cb(skb)->cookie_index;
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

static void mlxsw_sp_rx_exception_listener(struct sk_buff *skb, u8 local_port,
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
	skb_pull(skb, ETH_HLEN);
	skb->offload_fwd_mark = 1;
	netif_receive_skb(skb);
}

#define MLXSW_SP_TRAP_DROP(_id, _group_id)				      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_DROP_EXT(_id, _group_id, _metadata)		      \
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				      \
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			     MLXSW_SP_TRAP_METADATA | (_metadata))

#define MLXSW_SP_TRAP_DRIVER_DROP(_id, _group_id)			      \
	DEVLINK_TRAP_DRIVER(DROP, DROP, DEVLINK_MLXSW_TRAP_ID_##_id,	      \
			    DEVLINK_MLXSW_TRAP_NAME_##_id,		      \
			    DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id,	      \
			    MLXSW_SP_TRAP_METADATA)

#define MLXSW_SP_TRAP_EXCEPTION(_id, _group_id)		      \
	DEVLINK_TRAP_GENERIC(EXCEPTION, TRAP, _id,			      \
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

#define MLXSW_SP_RXL_EXCEPTION(_id, _group_id, _action)			      \
	MLXSW_RXL(mlxsw_sp_rx_exception_listener, _id,			      \
		   _action, false, SP_##_group_id, SET_FW_DEFAULT)

#define MLXSW_SP_TRAP_POLICER(_id, _rate, _burst)			      \
	DEVLINK_TRAP_POLICER(_id, _rate, _burst,			      \
			     MLXSW_REG_QPCR_HIGHEST_CIR,		      \
			     MLXSW_REG_QPCR_LOWEST_CIR,			      \
			     1 << MLXSW_REG_QPCR_HIGHEST_CBS,		      \
			     1 << MLXSW_REG_QPCR_LOWEST_CBS)

/* Ordered by policer identifier */
static const struct devlink_trap_policer mlxsw_sp_trap_policers_arr[] = {
	MLXSW_SP_TRAP_POLICER(1, 10 * 1024, 128),
};

static const struct devlink_trap_group mlxsw_sp_trap_groups_arr[] = {
	DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS, 1),
	DEVLINK_TRAP_GROUP_GENERIC(L3_DROPS, 1),
	DEVLINK_TRAP_GROUP_GENERIC(TUNNEL_DROPS, 1),
	DEVLINK_TRAP_GROUP_GENERIC(ACL_DROPS, 1),
};

static const struct devlink_trap mlxsw_sp_traps_arr[] = {
	MLXSW_SP_TRAP_DROP(SMAC_MC, L2_DROPS),
	MLXSW_SP_TRAP_DROP(VLAN_TAG_MISMATCH, L2_DROPS),
	MLXSW_SP_TRAP_DROP(INGRESS_VLAN_FILTER, L2_DROPS),
	MLXSW_SP_TRAP_DROP(INGRESS_STP_FILTER, L2_DROPS),
	MLXSW_SP_TRAP_DROP(EMPTY_TX_LIST, L2_DROPS),
	MLXSW_SP_TRAP_DROP(PORT_LOOPBACK_FILTER, L2_DROPS),
	MLXSW_SP_TRAP_DROP(BLACKHOLE_ROUTE, L3_DROPS),
	MLXSW_SP_TRAP_DROP(NON_IP_PACKET, L3_DROPS),
	MLXSW_SP_TRAP_DROP(UC_DIP_MC_DMAC, L3_DROPS),
	MLXSW_SP_TRAP_DROP(DIP_LB, L3_DROPS),
	MLXSW_SP_TRAP_DROP(SIP_MC, L3_DROPS),
	MLXSW_SP_TRAP_DROP(SIP_LB, L3_DROPS),
	MLXSW_SP_TRAP_DROP(CORRUPTED_IP_HDR, L3_DROPS),
	MLXSW_SP_TRAP_DROP(IPV4_SIP_BC, L3_DROPS),
	MLXSW_SP_TRAP_DROP(IPV6_MC_DIP_RESERVED_SCOPE, L3_DROPS),
	MLXSW_SP_TRAP_DROP(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(MTU_ERROR, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(TTL_ERROR, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(RPF, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(REJECT_ROUTE, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(UNRESOLVED_NEIGH, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(IPV4_LPM_UNICAST_MISS, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(IPV6_LPM_UNICAST_MISS, L3_DROPS),
	MLXSW_SP_TRAP_DRIVER_DROP(IRIF_DISABLED, L3_DROPS),
	MLXSW_SP_TRAP_DRIVER_DROP(ERIF_DISABLED, L3_DROPS),
	MLXSW_SP_TRAP_DROP(NON_ROUTABLE, L3_DROPS),
	MLXSW_SP_TRAP_EXCEPTION(DECAP_ERROR, TUNNEL_DROPS),
	MLXSW_SP_TRAP_DROP(OVERLAY_SMAC_MC, TUNNEL_DROPS),
	MLXSW_SP_TRAP_DROP_EXT(INGRESS_FLOW_ACTION_DROP, ACL_DROPS,
			       DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
	MLXSW_SP_TRAP_DROP_EXT(EGRESS_FLOW_ACTION_DROP, ACL_DROPS,
			       DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE),
};

static const struct mlxsw_listener mlxsw_sp_listeners_arr[] = {
	MLXSW_SP_RXL_DISCARD(ING_PACKET_SMAC_MC, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_SWITCH_VTAG_ALLOW, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_SWITCH_VLAN, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_SWITCH_STP, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_UC, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_MC_NULL, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(LOOKUP_SWITCH_LB, L2_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ROUTER2, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_NON_IP_PACKET, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_UC_DIP_MC_DMAC, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_DIP_LB, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_SIP_MC, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_SIP_LB, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_CORRUPTED_IP_HDR, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ING_ROUTER_IPV4_SIP_BC, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(IPV6_MC_DIP_RESERVED_SCOPE, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE, L3_DISCARDS),
	MLXSW_SP_RXL_EXCEPTION(MTUERROR, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(TTLERROR, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(RPF, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(RTR_INGRESS1, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(HOST_MISS_IPV4, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(HOST_MISS_IPV6, L3_DISCARDS, TRAP_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(DISCARD_ROUTER3, L3_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(DISCARD_ROUTER_LPM4, L3_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(DISCARD_ROUTER_LPM6, L3_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_DISCARD(ROUTER_IRIF_EN, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(ROUTER_ERIF_EN, L3_DISCARDS),
	MLXSW_SP_RXL_DISCARD(NON_ROUTABLE, L3_DISCARDS),
	MLXSW_SP_RXL_EXCEPTION(DECAP_ECN0, TUNNEL_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(IPIP_DECAP_ERROR, TUNNEL_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_EXCEPTION(DISCARD_DEC_PKT, TUNNEL_DISCARDS,
			       TRAP_EXCEPTION_TO_CPU),
	MLXSW_SP_RXL_DISCARD(OVERLAY_SMAC_MC, TUNNEL_DISCARDS),
	MLXSW_SP_RXL_ACL_DISCARD(INGRESS_ACL, ACL_DISCARDS, DUMMY),
	MLXSW_SP_RXL_ACL_DISCARD(EGRESS_ACL, ACL_DISCARDS, DUMMY),
};

/* Mapping between hardware trap and devlink trap. Multiple hardware traps can
 * be mapped to the same devlink trap. Order is according to
 * 'mlxsw_sp_listeners_arr'.
 */
static const u16 mlxsw_sp_listener_devlink_map[] = {
	DEVLINK_TRAP_GENERIC_ID_SMAC_MC,
	DEVLINK_TRAP_GENERIC_ID_VLAN_TAG_MISMATCH,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_VLAN_FILTER,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_STP_FILTER,
	DEVLINK_TRAP_GENERIC_ID_EMPTY_TX_LIST,
	DEVLINK_TRAP_GENERIC_ID_EMPTY_TX_LIST,
	DEVLINK_TRAP_GENERIC_ID_PORT_LOOPBACK_FILTER,
	DEVLINK_TRAP_GENERIC_ID_BLACKHOLE_ROUTE,
	DEVLINK_TRAP_GENERIC_ID_NON_IP_PACKET,
	DEVLINK_TRAP_GENERIC_ID_UC_DIP_MC_DMAC,
	DEVLINK_TRAP_GENERIC_ID_DIP_LB,
	DEVLINK_TRAP_GENERIC_ID_SIP_MC,
	DEVLINK_TRAP_GENERIC_ID_SIP_LB,
	DEVLINK_TRAP_GENERIC_ID_CORRUPTED_IP_HDR,
	DEVLINK_TRAP_GENERIC_ID_IPV4_SIP_BC,
	DEVLINK_TRAP_GENERIC_ID_IPV6_MC_DIP_RESERVED_SCOPE,
	DEVLINK_TRAP_GENERIC_ID_IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE,
	DEVLINK_TRAP_GENERIC_ID_MTU_ERROR,
	DEVLINK_TRAP_GENERIC_ID_TTL_ERROR,
	DEVLINK_TRAP_GENERIC_ID_RPF,
	DEVLINK_TRAP_GENERIC_ID_REJECT_ROUTE,
	DEVLINK_TRAP_GENERIC_ID_UNRESOLVED_NEIGH,
	DEVLINK_TRAP_GENERIC_ID_UNRESOLVED_NEIGH,
	DEVLINK_TRAP_GENERIC_ID_UNRESOLVED_NEIGH,
	DEVLINK_TRAP_GENERIC_ID_IPV4_LPM_UNICAST_MISS,
	DEVLINK_TRAP_GENERIC_ID_IPV6_LPM_UNICAST_MISS,
	DEVLINK_MLXSW_TRAP_ID_IRIF_DISABLED,
	DEVLINK_MLXSW_TRAP_ID_ERIF_DISABLED,
	DEVLINK_TRAP_GENERIC_ID_NON_ROUTABLE,
	DEVLINK_TRAP_GENERIC_ID_DECAP_ERROR,
	DEVLINK_TRAP_GENERIC_ID_DECAP_ERROR,
	DEVLINK_TRAP_GENERIC_ID_DECAP_ERROR,
	DEVLINK_TRAP_GENERIC_ID_OVERLAY_SMAC_MC,
	DEVLINK_TRAP_GENERIC_ID_INGRESS_FLOW_ACTION_DROP,
	DEVLINK_TRAP_GENERIC_ID_EGRESS_FLOW_ACTION_DROP,
};

#define MLXSW_SP_THIN_POLICER_ID	(MLXSW_REG_HTGT_TRAP_GROUP_MAX + 1)

static struct mlxsw_sp_trap_policer_item *
mlxsw_sp_trap_policer_item_lookup(struct mlxsw_sp *mlxsw_sp, u32 id)
{
	struct mlxsw_sp_trap_policer_item *policer_item;
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;

	list_for_each_entry(policer_item, &trap->policer_item_list, list) {
		if (policer_item->id == id)
			return policer_item;
	}

	return NULL;
}

static int mlxsw_sp_trap_cpu_policers_set(struct mlxsw_sp *mlxsw_sp)
{
	char qpcr_pl[MLXSW_REG_QPCR_LEN];

	/* The purpose of "thin" policer is to drop as many packets
	 * as possible. The dummy group is using it.
	 */
	__set_bit(MLXSW_SP_THIN_POLICER_ID, mlxsw_sp->trap->policers_usage);
	mlxsw_reg_qpcr_pack(qpcr_pl, MLXSW_SP_THIN_POLICER_ID,
			    MLXSW_REG_QPCR_IR_UNITS_M, false, 1, 4);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
}

static int mlxsw_sp_trap_dummy_group_init(struct mlxsw_sp *mlxsw_sp)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_SP_DUMMY,
			    MLXSW_SP_THIN_POLICER_ID, 0, 1);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
}

static int mlxsw_sp_trap_policers_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	u64 free_policers = 0;
	u32 last_id = 0;
	int err, i;

	for_each_clear_bit(i, trap->policers_usage, trap->max_policers)
		free_policers++;

	if (ARRAY_SIZE(mlxsw_sp_trap_policers_arr) > free_policers) {
		dev_err(mlxsw_sp->bus_info->dev, "Exceeded number of supported packet trap policers\n");
		return -ENOBUFS;
	}

	trap->policers_arr = kcalloc(free_policers,
				     sizeof(struct devlink_trap_policer),
				     GFP_KERNEL);
	if (!trap->policers_arr)
		return -ENOMEM;

	trap->policers_count = free_policers;

	for (i = 0; i < free_policers; i++) {
		const struct devlink_trap_policer *policer;

		if (i < ARRAY_SIZE(mlxsw_sp_trap_policers_arr)) {
			policer = &mlxsw_sp_trap_policers_arr[i];
			trap->policers_arr[i] = *policer;
			last_id = policer->id;
		} else {
			/* Use parameters set for first policer and override
			 * relevant ones.
			 */
			policer = &mlxsw_sp_trap_policers_arr[0];
			trap->policers_arr[i] = *policer;
			trap->policers_arr[i].id = ++last_id;
			trap->policers_arr[i].init_rate = 1;
			trap->policers_arr[i].init_burst = 16;
		}
	}

	INIT_LIST_HEAD(&trap->policer_item_list);

	err = devlink_trap_policers_register(devlink, trap->policers_arr,
					     trap->policers_count);
	if (err)
		goto err_trap_policers_register;

	return 0;

err_trap_policers_register:
	kfree(trap->policers_arr);
	return err;
}

static void mlxsw_sp_trap_policers_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;

	devlink_trap_policers_unregister(devlink, trap->policers_arr,
					 trap->policers_count);
	WARN_ON(!list_empty(&trap->policer_item_list));
	kfree(trap->policers_arr);
}

int mlxsw_sp_devlink_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t groups_count = ARRAY_SIZE(mlxsw_sp_trap_groups_arr);
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = mlxsw_sp_trap_cpu_policers_set(mlxsw_sp);
	if (err)
		return err;

	err = mlxsw_sp_trap_dummy_group_init(mlxsw_sp);
	if (err)
		return err;

	if (WARN_ON(ARRAY_SIZE(mlxsw_sp_listener_devlink_map) !=
		    ARRAY_SIZE(mlxsw_sp_listeners_arr)))
		return -EINVAL;

	err = mlxsw_sp_trap_policers_init(mlxsw_sp);
	if (err)
		return err;

	err = devlink_trap_groups_register(devlink, mlxsw_sp_trap_groups_arr,
					   groups_count);
	if (err)
		goto err_trap_groups_register;

	err = devlink_traps_register(devlink, mlxsw_sp_traps_arr,
				     ARRAY_SIZE(mlxsw_sp_traps_arr), mlxsw_sp);
	if (err)
		goto err_traps_register;

	return 0;

err_traps_register:
	devlink_trap_groups_unregister(devlink, mlxsw_sp_trap_groups_arr,
				       groups_count);
err_trap_groups_register:
	mlxsw_sp_trap_policers_fini(mlxsw_sp);
	return err;
}

void mlxsw_sp_devlink_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	size_t groups_count = ARRAY_SIZE(mlxsw_sp_trap_groups_arr);
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_traps_unregister(devlink, mlxsw_sp_traps_arr,
				 ARRAY_SIZE(mlxsw_sp_traps_arr));
	devlink_trap_groups_unregister(devlink, mlxsw_sp_trap_groups_arr,
				       groups_count);
	mlxsw_sp_trap_policers_fini(mlxsw_sp);
}

int mlxsw_sp_trap_init(struct mlxsw_core *mlxsw_core,
		       const struct devlink_trap *trap, void *trap_ctx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_listener_devlink_map); i++) {
		const struct mlxsw_listener *listener;
		int err;

		if (mlxsw_sp_listener_devlink_map[i] != trap->id)
			continue;
		listener = &mlxsw_sp_listeners_arr[i];

		err = mlxsw_core_trap_register(mlxsw_core, listener, trap_ctx);
		if (err)
			return err;
	}

	return 0;
}

void mlxsw_sp_trap_fini(struct mlxsw_core *mlxsw_core,
			const struct devlink_trap *trap, void *trap_ctx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_listener_devlink_map); i++) {
		const struct mlxsw_listener *listener;

		if (mlxsw_sp_listener_devlink_map[i] != trap->id)
			continue;
		listener = &mlxsw_sp_listeners_arr[i];

		mlxsw_core_trap_unregister(mlxsw_core, listener, trap_ctx);
	}
}

int mlxsw_sp_trap_action_set(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap *trap,
			     enum devlink_trap_action action)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_listener_devlink_map); i++) {
		const struct mlxsw_listener *listener;
		bool enabled;
		int err;

		if (mlxsw_sp_listener_devlink_map[i] != trap->id)
			continue;
		listener = &mlxsw_sp_listeners_arr[i];
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
			   u32 policer_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	u16 hw_policer_id = MLXSW_REG_HTGT_INVALID_POLICER;
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	u8 priority, tc, group_id;

	switch (group->id) {
	case DEVLINK_TRAP_GROUP_GENERIC_ID_L2_DROPS:
		group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_L2_DISCARDS;
		priority = 0;
		tc = 1;
		break;
	case DEVLINK_TRAP_GROUP_GENERIC_ID_L3_DROPS:
		group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_L3_DISCARDS;
		priority = 0;
		tc = 1;
		break;
	case DEVLINK_TRAP_GROUP_GENERIC_ID_TUNNEL_DROPS:
		group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_TUNNEL_DISCARDS;
		priority = 0;
		tc = 1;
		break;
	case DEVLINK_TRAP_GROUP_GENERIC_ID_ACL_DROPS:
		group_id = MLXSW_REG_HTGT_TRAP_GROUP_SP_ACL_DISCARDS;
		priority = 0;
		tc = 1;
		break;
	default:
		return -EINVAL;
	}

	if (policer_id) {
		struct mlxsw_sp_trap_policer_item *policer_item;

		policer_item = mlxsw_sp_trap_policer_item_lookup(mlxsw_sp,
								 policer_id);
		if (WARN_ON(!policer_item))
			return -EINVAL;
		hw_policer_id = policer_item->hw_id;
	}

	mlxsw_reg_htgt_pack(htgt_pl, group_id, hw_policer_id, priority, tc);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
}

int mlxsw_sp_trap_group_init(struct mlxsw_core *mlxsw_core,
			     const struct devlink_trap_group *group)
{
	return __mlxsw_sp_trap_group_init(mlxsw_core, group,
					  group->init_policer_id);
}

int mlxsw_sp_trap_group_set(struct mlxsw_core *mlxsw_core,
			    const struct devlink_trap_group *group,
			    const struct devlink_trap_policer *policer)
{
	u32 policer_id = policer ? policer->id : 0;

	return __mlxsw_sp_trap_group_init(mlxsw_core, group, policer_id);
}

static struct mlxsw_sp_trap_policer_item *
mlxsw_sp_trap_policer_item_init(struct mlxsw_sp *mlxsw_sp, u32 id)
{
	struct mlxsw_sp_trap_policer_item *policer_item;
	struct mlxsw_sp_trap *trap = mlxsw_sp->trap;
	u16 hw_id;

	/* We should be able to allocate a policer because the number of
	 * policers we registered with devlink is in according with the number
	 * of available policers.
	 */
	hw_id = find_first_zero_bit(trap->policers_usage, trap->max_policers);
	if (WARN_ON(hw_id == trap->max_policers))
		return ERR_PTR(-ENOBUFS);

	policer_item = kzalloc(sizeof(*policer_item), GFP_KERNEL);
	if (!policer_item)
		return ERR_PTR(-ENOMEM);

	__set_bit(hw_id, trap->policers_usage);
	policer_item->hw_id = hw_id;
	policer_item->id = id;
	list_add_tail(&policer_item->list, &trap->policer_item_list);

	return policer_item;
}

static void
mlxsw_sp_trap_policer_item_fini(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_trap_policer_item *policer_item)
{
	list_del(&policer_item->list);
	__clear_bit(policer_item->hw_id, mlxsw_sp->trap->policers_usage);
	kfree(policer_item);
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

	policer_item = mlxsw_sp_trap_policer_item_init(mlxsw_sp, policer->id);
	if (IS_ERR(policer_item))
		return PTR_ERR(policer_item);

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
