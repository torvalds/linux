/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_EN_TC_PRIV_H__
#define __MLX5_EN_TC_PRIV_H__

#include "en_tc.h"

#define MLX5E_TC_FLOW_BASE (MLX5E_TC_FLAG_LAST_EXPORTED_BIT + 1)

#define MLX5E_TC_MAX_SPLITS 1

enum {
	MLX5E_TC_FLOW_FLAG_INGRESS               = MLX5E_TC_FLAG_INGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_EGRESS                = MLX5E_TC_FLAG_EGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_ESWITCH               = MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_FT                    = MLX5E_TC_FLAG_FT_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_NIC                   = MLX5E_TC_FLAG_NIC_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_OFFLOADED             = MLX5E_TC_FLOW_BASE,
	MLX5E_TC_FLOW_FLAG_HAIRPIN               = MLX5E_TC_FLOW_BASE + 1,
	MLX5E_TC_FLOW_FLAG_HAIRPIN_RSS           = MLX5E_TC_FLOW_BASE + 2,
	MLX5E_TC_FLOW_FLAG_SLOW                  = MLX5E_TC_FLOW_BASE + 3,
	MLX5E_TC_FLOW_FLAG_DUP                   = MLX5E_TC_FLOW_BASE + 4,
	MLX5E_TC_FLOW_FLAG_NOT_READY             = MLX5E_TC_FLOW_BASE + 5,
	MLX5E_TC_FLOW_FLAG_DELETED               = MLX5E_TC_FLOW_BASE + 6,
	MLX5E_TC_FLOW_FLAG_CT                    = MLX5E_TC_FLOW_BASE + 7,
	MLX5E_TC_FLOW_FLAG_L3_TO_L2_DECAP        = MLX5E_TC_FLOW_BASE + 8,
	MLX5E_TC_FLOW_FLAG_TUN_RX                = MLX5E_TC_FLOW_BASE + 9,
	MLX5E_TC_FLOW_FLAG_FAILED                = MLX5E_TC_FLOW_BASE + 10,
};

struct mlx5e_tc_flow_parse_attr {
	const struct ip_tunnel_info *tun_info[MLX5_MAX_FLOW_FWD_VPORTS];
	struct net_device *filter_dev;
	struct mlx5_flow_spec spec;
	struct mlx5e_tc_mod_hdr_acts mod_hdr_acts;
	int mirred_ifindex[MLX5_MAX_FLOW_FWD_VPORTS];
	struct ethhdr eth;
};

/* Helper struct for accessing a struct containing list_head array.
 * Containing struct
 *   |- Helper array
 *      [0] Helper item 0
 *          |- list_head item 0
 *          |- index (0)
 *      [1] Helper item 1
 *          |- list_head item 1
 *          |- index (1)
 * To access the containing struct from one of the list_head items:
 * 1. Get the helper item from the list_head item using
 *    helper item =
 *        container_of(list_head item, helper struct type, list_head field)
 * 2. Get the contining struct from the helper item and its index in the array:
 *    containing struct =
 *        container_of(helper item, containing struct type, helper field[index])
 */
struct encap_flow_item {
	struct mlx5e_encap_entry *e; /* attached encap instance */
	struct list_head list;
	int index;
};

struct encap_route_flow_item {
	struct mlx5e_route_entry *r; /* attached route instance */
	int index;
};

struct mlx5e_tc_flow {
	struct rhash_head node;
	struct mlx5e_priv *priv;
	u64 cookie;
	unsigned long flags;
	struct mlx5_flow_handle *rule[MLX5E_TC_MAX_SPLITS + 1];

	/* flows sharing the same reformat object - currently mpls decap */
	struct list_head l3_to_l2_reformat;
	struct mlx5e_decap_entry *decap_reformat;

	/* flows sharing same route entry */
	struct list_head decap_routes;
	struct mlx5e_route_entry *decap_route;
	struct encap_route_flow_item encap_routes[MLX5_MAX_FLOW_FWD_VPORTS];

	/* Flow can be associated with multiple encap IDs.
	 * The number of encaps is bounded by the number of supported
	 * destinations.
	 */
	struct encap_flow_item encaps[MLX5_MAX_FLOW_FWD_VPORTS];
	struct mlx5e_tc_flow *peer_flow;
	struct mlx5e_mod_hdr_handle *mh; /* attached mod header instance */
	struct mlx5e_hairpin_entry *hpe; /* attached hairpin instance */
	struct list_head hairpin; /* flows sharing the same hairpin */
	struct list_head peer;    /* flows with peer flow */
	struct list_head unready; /* flows not ready to be offloaded (e.g
				   * due to missing route)
				   */
	struct net_device *orig_dev; /* netdev adding flow first */
	int tmp_entry_index;
	struct list_head tmp_list; /* temporary flow list used by neigh update */
	refcount_t refcnt;
	struct rcu_head rcu_head;
	struct completion init_done;
	int tunnel_id; /* the mapped tunnel id of this flow */
	struct mlx5_flow_attr *attr;
};

u8 mlx5e_tc_get_ip_version(struct mlx5_flow_spec *spec, bool outer);

struct mlx5_flow_handle *
mlx5e_tc_offload_fdb_rules(struct mlx5_eswitch *esw,
			   struct mlx5e_tc_flow *flow,
			   struct mlx5_flow_spec *spec,
			   struct mlx5_flow_attr *attr);

bool mlx5e_is_offloaded_flow(struct mlx5e_tc_flow *flow);

static inline void __flow_flag_set(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	/* Complete all memory stores before setting bit. */
	smp_mb__before_atomic();
	set_bit(flag, &flow->flags);
}

#define flow_flag_set(flow, flag) __flow_flag_set(flow, MLX5E_TC_FLOW_FLAG_##flag)

static inline bool __flow_flag_test_and_set(struct mlx5e_tc_flow *flow,
					    unsigned long flag)
{
	/* test_and_set_bit() provides all necessary barriers */
	return test_and_set_bit(flag, &flow->flags);
}

#define flow_flag_test_and_set(flow, flag)			\
	__flow_flag_test_and_set(flow,				\
				 MLX5E_TC_FLOW_FLAG_##flag)

static inline void __flow_flag_clear(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	/* Complete all memory stores before clearing bit. */
	smp_mb__before_atomic();
	clear_bit(flag, &flow->flags);
}

#define flow_flag_clear(flow, flag) __flow_flag_clear(flow,		\
						      MLX5E_TC_FLOW_FLAG_##flag)

static inline bool __flow_flag_test(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	bool ret = test_bit(flag, &flow->flags);

	/* Read fields of flow structure only after checking flags. */
	smp_mb__after_atomic();
	return ret;
}

#define flow_flag_test(flow, flag) __flow_flag_test(flow,		\
						    MLX5E_TC_FLOW_FLAG_##flag)

void mlx5e_tc_unoffload_from_slow_path(struct mlx5_eswitch *esw,
				       struct mlx5e_tc_flow *flow);
struct mlx5_flow_handle *
mlx5e_tc_offload_to_slow_path(struct mlx5_eswitch *esw,
			      struct mlx5e_tc_flow *flow,
			      struct mlx5_flow_spec *spec);
void mlx5e_tc_unoffload_fdb_rules(struct mlx5_eswitch *esw,
				  struct mlx5e_tc_flow *flow,
				  struct mlx5_flow_attr *attr);

struct mlx5e_tc_flow *mlx5e_flow_get(struct mlx5e_tc_flow *flow);
void mlx5e_flow_put(struct mlx5e_priv *priv, struct mlx5e_tc_flow *flow);

struct mlx5_fc *mlx5e_tc_get_counter(struct mlx5e_tc_flow *flow);

#endif /* __MLX5_EN_TC_PRIV_H__ */
