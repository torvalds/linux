/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_ROUTER_H_
#define _MLXSW_ROUTER_H_

#include "spectrum.h"
#include "reg.h"

struct mlxsw_sp_router_nve_decap {
	u32 ul_tb_id;
	u32 tunnel_index;
	enum mlxsw_sp_l3proto ul_proto;
	union mlxsw_sp_l3addr ul_sip;
	u8 valid:1;
};

struct mlxsw_sp_fib_entry_op_ctx {
	u8 bulk_ok:1, /* Indicate to the low-level op it is ok to bulk
		       * the actual entry with the one that is the next
		       * in queue.
		       */
	   initialized:1; /* Bit that the low-level op sets in case
			   * the context priv is initialized.
			   */
	struct list_head fib_entry_priv_list;
	unsigned long ll_priv[];
};

static inline void
mlxsw_sp_fib_entry_op_ctx_clear(struct mlxsw_sp_fib_entry_op_ctx *op_ctx)
{
	WARN_ON_ONCE(!list_empty(&op_ctx->fib_entry_priv_list));
	memset(op_ctx, 0, sizeof(*op_ctx));
	INIT_LIST_HEAD(&op_ctx->fib_entry_priv_list);
}

struct mlxsw_sp_router {
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif **rifs;
	struct mlxsw_sp_vr *vrs;
	struct rhashtable neigh_ht;
	struct rhashtable nexthop_group_ht;
	struct rhashtable nexthop_ht;
	struct list_head nexthop_list;
	struct {
		/* One tree for each protocol: IPv4 and IPv6 */
		struct mlxsw_sp_lpm_tree *proto_trees[2];
		struct mlxsw_sp_lpm_tree *trees;
		unsigned int tree_count;
	} lpm;
	struct {
		struct delayed_work dw;
		unsigned long interval;	/* ms */
	} neighs_update;
	struct delayed_work nexthop_probe_dw;
#define MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL 5000 /* ms */
	struct list_head nexthop_neighs_list;
	struct list_head ipip_list;
	bool aborted;
	struct notifier_block nexthop_nb;
	struct notifier_block fib_nb;
	struct notifier_block netevent_nb;
	struct notifier_block inetaddr_nb;
	struct notifier_block inet6addr_nb;
	const struct mlxsw_sp_rif_ops **rif_ops_arr;
	const struct mlxsw_sp_ipip_ops **ipip_ops_arr;
	u32 adj_discard_index;
	bool adj_discard_index_valid;
	struct mlxsw_sp_router_nve_decap nve_decap_config;
	struct mutex lock; /* Protects shared router resources */
	struct work_struct fib_event_work;
	struct list_head fib_event_queue;
	spinlock_t fib_event_queue_lock; /* Protects fib event queue list */
	/* One set of ops for each protocol: IPv4 and IPv6 */
	const struct mlxsw_sp_router_ll_ops *proto_ll_ops[MLXSW_SP_L3_PROTO_MAX];
	struct mlxsw_sp_fib_entry_op_ctx *ll_op_ctx;
};

struct mlxsw_sp_fib_entry_priv {
	refcount_t refcnt;
	struct list_head list; /* Member in op_ctx->fib_entry_priv_list */
	unsigned long priv[];
};

enum mlxsw_sp_fib_entry_op {
	MLXSW_SP_FIB_ENTRY_OP_WRITE,
	MLXSW_SP_FIB_ENTRY_OP_UPDATE,
	MLXSW_SP_FIB_ENTRY_OP_DELETE,
};

/* Low-level router ops. Basically this is to handle the different
 * register sets to work with ordinary and XM trees and FIB entries.
 */
struct mlxsw_sp_router_ll_ops {
	int (*ralta_write)(struct mlxsw_sp *mlxsw_sp, char *xralta_pl);
	int (*ralst_write)(struct mlxsw_sp *mlxsw_sp, char *xralst_pl);
	int (*raltb_write)(struct mlxsw_sp *mlxsw_sp, char *xraltb_pl);
	size_t fib_entry_op_ctx_size;
	size_t fib_entry_priv_size;
	void (*fib_entry_pack)(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
			       enum mlxsw_sp_l3proto proto, enum mlxsw_sp_fib_entry_op op,
			       u16 virtual_router, u8 prefix_len, unsigned char *addr,
			       struct mlxsw_sp_fib_entry_priv *priv);
	void (*fib_entry_act_remote_pack)(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
					  enum mlxsw_reg_ralue_trap_action trap_action,
					  u16 trap_id, u32 adjacency_index, u16 ecmp_size);
	void (*fib_entry_act_local_pack)(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
					 enum mlxsw_reg_ralue_trap_action trap_action,
					 u16 trap_id, u16 local_erif);
	void (*fib_entry_act_ip2me_pack)(struct mlxsw_sp_fib_entry_op_ctx *op_ctx);
	void (*fib_entry_act_ip2me_tun_pack)(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
					     u32 tunnel_ptr);
	int (*fib_entry_commit)(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
				bool *postponed_for_bulk);
	bool (*fib_entry_is_committed)(struct mlxsw_sp_fib_entry_priv *priv);
};

int mlxsw_sp_fib_entry_commit(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
			      const struct mlxsw_sp_router_ll_ops *ll_ops);

struct mlxsw_sp_rif_ipip_lb;
struct mlxsw_sp_rif_ipip_lb_config {
	enum mlxsw_reg_ritr_loopback_ipip_type lb_ipipt;
	u32 okey;
	enum mlxsw_sp_l3proto ul_protocol; /* Underlay. */
	union mlxsw_sp_l3addr saddr;
};

enum mlxsw_sp_rif_counter_dir {
	MLXSW_SP_RIF_COUNTER_INGRESS,
	MLXSW_SP_RIF_COUNTER_EGRESS,
};

struct mlxsw_sp_neigh_entry;
struct mlxsw_sp_nexthop;
struct mlxsw_sp_ipip_entry;

struct mlxsw_sp_rif *mlxsw_sp_rif_by_index(const struct mlxsw_sp *mlxsw_sp,
					   u16 rif_index);
u16 mlxsw_sp_rif_index(const struct mlxsw_sp_rif *rif);
u16 mlxsw_sp_ipip_lb_rif_index(const struct mlxsw_sp_rif_ipip_lb *rif);
u16 mlxsw_sp_ipip_lb_ul_vr_id(const struct mlxsw_sp_rif_ipip_lb *rif);
u16 mlxsw_sp_ipip_lb_ul_rif_id(const struct mlxsw_sp_rif_ipip_lb *lb_rif);
u32 mlxsw_sp_ipip_dev_ul_tb_id(const struct net_device *ol_dev);
int mlxsw_sp_rif_dev_ifindex(const struct mlxsw_sp_rif *rif);
const struct net_device *mlxsw_sp_rif_dev(const struct mlxsw_sp_rif *rif);
int mlxsw_sp_rif_counter_value_get(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_rif *rif,
				   enum mlxsw_sp_rif_counter_dir dir,
				   u64 *cnt);
void mlxsw_sp_rif_counter_free(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir);
int mlxsw_sp_rif_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir);
struct mlxsw_sp_neigh_entry *
mlxsw_sp_rif_neigh_next(struct mlxsw_sp_rif *rif,
			struct mlxsw_sp_neigh_entry *neigh_entry);
int mlxsw_sp_neigh_entry_type(struct mlxsw_sp_neigh_entry *neigh_entry);
unsigned char *
mlxsw_sp_neigh_entry_ha(struct mlxsw_sp_neigh_entry *neigh_entry);
u32 mlxsw_sp_neigh4_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry);
struct in6_addr *
mlxsw_sp_neigh6_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry);

#define mlxsw_sp_rif_neigh_for_each(neigh_entry, rif)				\
	for (neigh_entry = mlxsw_sp_rif_neigh_next(rif, NULL); neigh_entry;	\
	     neigh_entry = mlxsw_sp_rif_neigh_next(rif, neigh_entry))
int mlxsw_sp_neigh_counter_get(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_neigh_entry *neigh_entry,
			       u64 *p_counter);
void
mlxsw_sp_neigh_entry_counter_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_neigh_entry *neigh_entry,
				    bool adding);
bool mlxsw_sp_neigh_ipv6_ignore(struct mlxsw_sp_neigh_entry *neigh_entry);
int __mlxsw_sp_ipip_entry_update_tunnel(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_ipip_entry *ipip_entry,
					bool recreate_loopback,
					bool keep_encap,
					bool update_nexthops,
					struct netlink_ext_ack *extack);
void mlxsw_sp_ipip_entry_demote_tunnel(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_ipip_entry *ipip_entry);
bool
mlxsw_sp_ipip_demote_tunnel_by_saddr(struct mlxsw_sp *mlxsw_sp,
				     enum mlxsw_sp_l3proto ul_proto,
				     union mlxsw_sp_l3addr saddr,
				     u32 ul_tb_id,
				     const struct mlxsw_sp_ipip_entry *except);
struct mlxsw_sp_nexthop *mlxsw_sp_nexthop_next(struct mlxsw_sp_router *router,
					       struct mlxsw_sp_nexthop *nh);
bool mlxsw_sp_nexthop_offload(struct mlxsw_sp_nexthop *nh);
unsigned char *mlxsw_sp_nexthop_ha(struct mlxsw_sp_nexthop *nh);
int mlxsw_sp_nexthop_indexes(struct mlxsw_sp_nexthop *nh, u32 *p_adj_index,
			     u32 *p_adj_size, u32 *p_adj_hash_index);
struct mlxsw_sp_rif *mlxsw_sp_nexthop_rif(struct mlxsw_sp_nexthop *nh);
bool mlxsw_sp_nexthop_group_has_ipip(struct mlxsw_sp_nexthop *nh);
#define mlxsw_sp_nexthop_for_each(nh, router)				\
	for (nh = mlxsw_sp_nexthop_next(router, NULL); nh;		\
	     nh = mlxsw_sp_nexthop_next(router, nh))
int mlxsw_sp_nexthop_counter_get(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_nexthop *nh, u64 *p_counter);
int mlxsw_sp_nexthop_update(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
			    struct mlxsw_sp_nexthop *nh);
void mlxsw_sp_nexthop_counter_alloc(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop *nh);
void mlxsw_sp_nexthop_counter_free(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh);

static inline bool mlxsw_sp_l3addr_eq(const union mlxsw_sp_l3addr *addr1,
				      const union mlxsw_sp_l3addr *addr2)
{
	return !memcmp(addr1, addr2, sizeof(*addr1));
}

int mlxsw_sp_ipip_ecn_encap_init(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_ipip_ecn_decap_init(struct mlxsw_sp *mlxsw_sp);

#endif /* _MLXSW_ROUTER_H_*/
