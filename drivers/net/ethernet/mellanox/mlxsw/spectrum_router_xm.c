// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/rhashtable.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "spectrum_router.h"

#define MLXSW_SP_ROUTER_XM_M_VAL 16

static const u8 mlxsw_sp_router_xm_m_val[] = {
	[MLXSW_SP_L3_PROTO_IPV4] = MLXSW_SP_ROUTER_XM_M_VAL,
	[MLXSW_SP_L3_PROTO_IPV6] = 0, /* Currently unused. */
};

#define MLXSW_SP_ROUTER_XM_L_VAL_MAX 16

struct mlxsw_sp_router_xm {
	bool ipv4_supported;
	bool ipv6_supported;
	unsigned int entries_size;
	struct rhashtable ltable_ht;
	struct rhashtable flush_ht; /* Stores items about to be flushed from cache */
	unsigned int flush_count;
	bool flush_all_mode;
};

struct mlxsw_sp_router_xm_ltable_node {
	struct rhash_head ht_node; /* Member of router_xm->ltable_ht */
	u16 mindex;
	u8 current_lvalue;
	refcount_t refcnt;
	unsigned int lvalue_ref[MLXSW_SP_ROUTER_XM_L_VAL_MAX + 1];
};

static const struct rhashtable_params mlxsw_sp_router_xm_ltable_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_router_xm_ltable_node, mindex),
	.head_offset = offsetof(struct mlxsw_sp_router_xm_ltable_node, ht_node),
	.key_len = sizeof(u16),
	.automatic_shrinking = true,
};

struct mlxsw_sp_router_xm_flush_info {
	bool all;
	enum mlxsw_sp_l3proto proto;
	u16 virtual_router;
	u8 prefix_len;
	unsigned char addr[sizeof(struct in6_addr)];
};

struct mlxsw_sp_router_xm_fib_entry {
	bool committed;
	struct mlxsw_sp_router_xm_ltable_node *ltable_node; /* Parent node */
	u16 mindex; /* Store for processing from commit op */
	u8 lvalue;
	struct mlxsw_sp_router_xm_flush_info flush_info;
};

#define MLXSW_SP_ROUTE_LL_XM_ENTRIES_MAX \
	(MLXSW_REG_XMDR_TRANS_LEN / MLXSW_REG_XMDR_C_LT_ROUTE_V4_LEN)

struct mlxsw_sp_fib_entry_op_ctx_xm {
	bool initialized;
	char xmdr_pl[MLXSW_REG_XMDR_LEN];
	unsigned int trans_offset; /* Offset of the current command within one
				    * transaction of XMDR register.
				    */
	unsigned int trans_item_len; /* The current command length. This is used
				      * to advance 'trans_offset' when the next
				      * command is appended.
				      */
	unsigned int entries_count;
	struct mlxsw_sp_router_xm_fib_entry *entries[MLXSW_SP_ROUTE_LL_XM_ENTRIES_MAX];
};

static int mlxsw_sp_router_ll_xm_init(struct mlxsw_sp *mlxsw_sp, u16 vr_id,
				      enum mlxsw_sp_l3proto proto)
{
	char rxlte_pl[MLXSW_REG_RXLTE_LEN];

	mlxsw_reg_rxlte_pack(rxlte_pl, vr_id,
			     (enum mlxsw_reg_rxlte_protocol) proto, true);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rxlte), rxlte_pl);
}

static int mlxsw_sp_router_ll_xm_ralta_write(struct mlxsw_sp *mlxsw_sp, char *xralta_pl)
{
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(xralta), xralta_pl);
}

static int mlxsw_sp_router_ll_xm_ralst_write(struct mlxsw_sp *mlxsw_sp, char *xralst_pl)
{
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(xralst), xralst_pl);
}

static int mlxsw_sp_router_ll_xm_raltb_write(struct mlxsw_sp *mlxsw_sp, char *xraltb_pl)
{
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(xraltb), xraltb_pl);
}

static u16 mlxsw_sp_router_ll_xm_mindex_get4(const u32 addr)
{
	/* Currently the M-index is set to linear mode. That means it is defined
	 * as 16 MSB of IP address.
	 */
	return addr >> MLXSW_SP_ROUTER_XM_L_VAL_MAX;
}

static u16 mlxsw_sp_router_ll_xm_mindex_get6(const unsigned char *addr)
{
	WARN_ON_ONCE(1);
	return 0; /* currently unused */
}

static void mlxsw_sp_router_ll_xm_op_ctx_check_init(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
						    struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm)
{
	if (op_ctx->initialized)
		return;
	op_ctx->initialized = true;

	mlxsw_reg_xmdr_pack(op_ctx_xm->xmdr_pl, true);
	op_ctx_xm->trans_offset = 0;
	op_ctx_xm->entries_count = 0;
}

static void mlxsw_sp_router_ll_xm_fib_entry_pack(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
						 enum mlxsw_sp_l3proto proto,
						 enum mlxsw_sp_fib_entry_op op,
						 u16 virtual_router, u8 prefix_len,
						 unsigned char *addr,
						 struct mlxsw_sp_fib_entry_priv *priv)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;
	struct mlxsw_sp_router_xm_fib_entry *fib_entry = (void *) priv->priv;
	struct mlxsw_sp_router_xm_flush_info *flush_info;
	enum mlxsw_reg_xmdr_c_ltr_op xmdr_c_ltr_op;
	unsigned int len;

	mlxsw_sp_router_ll_xm_op_ctx_check_init(op_ctx, op_ctx_xm);

	switch (op) {
	case MLXSW_SP_FIB_ENTRY_OP_WRITE:
		xmdr_c_ltr_op = MLXSW_REG_XMDR_C_LTR_OP_WRITE;
		break;
	case MLXSW_SP_FIB_ENTRY_OP_UPDATE:
		xmdr_c_ltr_op = MLXSW_REG_XMDR_C_LTR_OP_UPDATE;
		break;
	case MLXSW_SP_FIB_ENTRY_OP_DELETE:
		xmdr_c_ltr_op = MLXSW_REG_XMDR_C_LTR_OP_DELETE;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		len = mlxsw_reg_xmdr_c_ltr_pack4(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
						 op_ctx_xm->entries_count, xmdr_c_ltr_op,
						 virtual_router, prefix_len, (u32 *) addr);
		fib_entry->mindex = mlxsw_sp_router_ll_xm_mindex_get4(*((u32 *) addr));
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		len = mlxsw_reg_xmdr_c_ltr_pack6(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
						 op_ctx_xm->entries_count, xmdr_c_ltr_op,
						 virtual_router, prefix_len, addr);
		fib_entry->mindex = mlxsw_sp_router_ll_xm_mindex_get6(addr);
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}
	if (!op_ctx_xm->trans_offset)
		op_ctx_xm->trans_item_len = len;
	else
		WARN_ON_ONCE(op_ctx_xm->trans_item_len != len);

	op_ctx_xm->entries[op_ctx_xm->entries_count] = fib_entry;

	fib_entry->lvalue = prefix_len > mlxsw_sp_router_xm_m_val[proto] ?
			       prefix_len - mlxsw_sp_router_xm_m_val[proto] : 0;

	flush_info = &fib_entry->flush_info;
	flush_info->proto = proto;
	flush_info->virtual_router = virtual_router;
	flush_info->prefix_len = prefix_len;
	if (addr)
		memcpy(flush_info->addr, addr, sizeof(flush_info->addr));
	else
		memset(flush_info->addr, 0, sizeof(flush_info->addr));
}

static void
mlxsw_sp_router_ll_xm_fib_entry_act_remote_pack(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
						enum mlxsw_reg_ralue_trap_action trap_action,
						u16 trap_id, u32 adjacency_index, u16 ecmp_size)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;

	mlxsw_reg_xmdr_c_ltr_act_remote_pack(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
					     trap_action, trap_id, adjacency_index, ecmp_size);
}

static void
mlxsw_sp_router_ll_xm_fib_entry_act_local_pack(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
					      enum mlxsw_reg_ralue_trap_action trap_action,
					       u16 trap_id, u16 local_erif)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;

	mlxsw_reg_xmdr_c_ltr_act_local_pack(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
					    trap_action, trap_id, local_erif);
}

static void
mlxsw_sp_router_ll_xm_fib_entry_act_ip2me_pack(struct mlxsw_sp_fib_entry_op_ctx *op_ctx)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;

	mlxsw_reg_xmdr_c_ltr_act_ip2me_pack(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset);
}

static void
mlxsw_sp_router_ll_xm_fib_entry_act_ip2me_tun_pack(struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
						   u32 tunnel_ptr)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;

	mlxsw_reg_xmdr_c_ltr_act_ip2me_tun_pack(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
						tunnel_ptr);
}

static struct mlxsw_sp_router_xm_ltable_node *
mlxsw_sp_router_xm_ltable_node_get(struct mlxsw_sp_router_xm *router_xm, u16 mindex)
{
	struct mlxsw_sp_router_xm_ltable_node *ltable_node;
	int err;

	ltable_node = rhashtable_lookup_fast(&router_xm->ltable_ht, &mindex,
					     mlxsw_sp_router_xm_ltable_ht_params);
	if (ltable_node) {
		refcount_inc(&ltable_node->refcnt);
		return ltable_node;
	}
	ltable_node = kzalloc(sizeof(*ltable_node), GFP_KERNEL);
	if (!ltable_node)
		return ERR_PTR(-ENOMEM);
	ltable_node->mindex = mindex;
	refcount_set(&ltable_node->refcnt, 1);

	err = rhashtable_insert_fast(&router_xm->ltable_ht, &ltable_node->ht_node,
				     mlxsw_sp_router_xm_ltable_ht_params);
	if (err)
		goto err_insert;

	return ltable_node;

err_insert:
	kfree(ltable_node);
	return ERR_PTR(err);
}

static void mlxsw_sp_router_xm_ltable_node_put(struct mlxsw_sp_router_xm *router_xm,
					       struct mlxsw_sp_router_xm_ltable_node *ltable_node)
{
	if (!refcount_dec_and_test(&ltable_node->refcnt))
		return;
	rhashtable_remove_fast(&router_xm->ltable_ht, &ltable_node->ht_node,
			       mlxsw_sp_router_xm_ltable_ht_params);
	kfree(ltable_node);
}

static int mlxsw_sp_router_xm_ltable_lvalue_set(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_router_xm_ltable_node *ltable_node)
{
	char xrmt_pl[MLXSW_REG_XRMT_LEN];

	mlxsw_reg_xrmt_pack(xrmt_pl, ltable_node->mindex, ltable_node->current_lvalue);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(xrmt), xrmt_pl);
}

struct mlxsw_sp_router_xm_flush_node {
	struct rhash_head ht_node; /* Member of router_xm->flush_ht */
	struct list_head list;
	struct mlxsw_sp_router_xm_flush_info flush_info;
	struct delayed_work dw;
	struct mlxsw_sp *mlxsw_sp;
	unsigned long start_jiffies;
	unsigned int reuses; /* By how many flush calls this was reused. */
	refcount_t refcnt;
};

static const struct rhashtable_params mlxsw_sp_router_xm_flush_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_router_xm_flush_node, flush_info),
	.head_offset = offsetof(struct mlxsw_sp_router_xm_flush_node, ht_node),
	.key_len = sizeof(struct mlxsw_sp_router_xm_flush_info),
	.automatic_shrinking = true,
};

static struct mlxsw_sp_router_xm_flush_node *
mlxsw_sp_router_xm_cache_flush_node_create(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_router_xm_flush_info *flush_info)
{
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;
	struct mlxsw_sp_router_xm_flush_node *flush_node;
	int err;

	flush_node = kzalloc(sizeof(*flush_node), GFP_KERNEL);
	if (!flush_node)
		return ERR_PTR(-ENOMEM);

	flush_node->flush_info = *flush_info;
	err = rhashtable_insert_fast(&router_xm->flush_ht, &flush_node->ht_node,
				     mlxsw_sp_router_xm_flush_ht_params);
	if (err) {
		kfree(flush_node);
		return ERR_PTR(err);
	}
	router_xm->flush_count++;
	flush_node->mlxsw_sp = mlxsw_sp;
	flush_node->start_jiffies = jiffies;
	refcount_set(&flush_node->refcnt, 1);
	return flush_node;
}

static void
mlxsw_sp_router_xm_cache_flush_node_hold(struct mlxsw_sp_router_xm_flush_node *flush_node)
{
	if (!flush_node)
		return;
	refcount_inc(&flush_node->refcnt);
}

static void
mlxsw_sp_router_xm_cache_flush_node_put(struct mlxsw_sp_router_xm_flush_node *flush_node)
{
	if (!flush_node || !refcount_dec_and_test(&flush_node->refcnt))
		return;
	kfree(flush_node);
}

static void
mlxsw_sp_router_xm_cache_flush_node_destroy(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_router_xm_flush_node *flush_node)
{
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;

	router_xm->flush_count--;
	rhashtable_remove_fast(&router_xm->flush_ht, &flush_node->ht_node,
			       mlxsw_sp_router_xm_flush_ht_params);
	mlxsw_sp_router_xm_cache_flush_node_put(flush_node);
}

static u32 mlxsw_sp_router_xm_flush_mask4(u8 prefix_len)
{
	return GENMASK(31, 32 - prefix_len);
}

static unsigned char *mlxsw_sp_router_xm_flush_mask6(u8 prefix_len)
{
	static unsigned char mask[sizeof(struct in6_addr)];

	memset(mask, 0, sizeof(mask));
	memset(mask, 0xff, prefix_len / 8);
	mask[prefix_len / 8] = GENMASK(8, 8 - prefix_len % 8);
	return mask;
}

#define MLXSW_SP_ROUTER_XM_CACHE_PARALLEL_FLUSHES_LIMIT 15
#define MLXSW_SP_ROUTER_XM_CACHE_FLUSH_ALL_MIN_REUSES 15
#define MLXSW_SP_ROUTER_XM_CACHE_DELAY 50 /* usecs */
#define MLXSW_SP_ROUTER_XM_CACHE_MAX_WAIT (MLXSW_SP_ROUTER_XM_CACHE_DELAY * 10)

static void mlxsw_sp_router_xm_cache_flush_work(struct work_struct *work)
{
	struct mlxsw_sp_router_xm_flush_info *flush_info;
	struct mlxsw_sp_router_xm_flush_node *flush_node;
	char rlcmld_pl[MLXSW_REG_RLCMLD_LEN];
	enum mlxsw_reg_rlcmld_select select;
	struct mlxsw_sp *mlxsw_sp;
	u32 addr4;
	int err;

	flush_node = container_of(work, struct mlxsw_sp_router_xm_flush_node,
				  dw.work);
	mlxsw_sp = flush_node->mlxsw_sp;
	flush_info = &flush_node->flush_info;

	if (flush_info->all) {
		char rlpmce_pl[MLXSW_REG_RLPMCE_LEN];

		mlxsw_reg_rlpmce_pack(rlpmce_pl, true, false);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rlpmce),
				      rlpmce_pl);
		if (err)
			dev_err(mlxsw_sp->bus_info->dev, "Failed to flush XM cache\n");

		if (flush_node->reuses <
		    MLXSW_SP_ROUTER_XM_CACHE_FLUSH_ALL_MIN_REUSES)
			/* Leaving flush-all mode. */
			mlxsw_sp->router->xm->flush_all_mode = false;
		goto out;
	}

	select = MLXSW_REG_RLCMLD_SELECT_M_AND_ML_ENTRIES;

	switch (flush_info->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		addr4 = *((u32 *) flush_info->addr);
		addr4 &= mlxsw_sp_router_xm_flush_mask4(flush_info->prefix_len);

		/* In case the flush prefix length is bigger than M-value,
		 * it makes no sense to flush M entries. So just flush
		 * the ML entries.
		 */
		if (flush_info->prefix_len > MLXSW_SP_ROUTER_XM_M_VAL)
			select = MLXSW_REG_RLCMLD_SELECT_ML_ENTRIES;

		mlxsw_reg_rlcmld_pack4(rlcmld_pl, select,
				       flush_info->virtual_router, addr4,
				       mlxsw_sp_router_xm_flush_mask4(flush_info->prefix_len));
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_reg_rlcmld_pack6(rlcmld_pl, select,
				       flush_info->virtual_router, flush_info->addr,
				       mlxsw_sp_router_xm_flush_mask6(flush_info->prefix_len));
		break;
	default:
		WARN_ON(true);
		goto out;
	}
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rlcmld), rlcmld_pl);
	if (err)
		dev_err(mlxsw_sp->bus_info->dev, "Failed to flush XM cache\n");

out:
	mlxsw_sp_router_xm_cache_flush_node_destroy(mlxsw_sp, flush_node);
}

static bool
mlxsw_sp_router_xm_cache_flush_may_cancel(struct mlxsw_sp_router_xm_flush_node *flush_node)
{
	unsigned long max_wait = usecs_to_jiffies(MLXSW_SP_ROUTER_XM_CACHE_MAX_WAIT);
	unsigned long delay = usecs_to_jiffies(MLXSW_SP_ROUTER_XM_CACHE_DELAY);

	/* In case there is the same flushing work pending, check
	 * if we can consolidate with it. We can do it up to MAX_WAIT.
	 * Cancel the delayed work. If the work was still pending.
	 */
	if (time_is_before_jiffies(flush_node->start_jiffies + max_wait - delay) &&
	    cancel_delayed_work_sync(&flush_node->dw))
		return true;
	return false;
}

static int
mlxsw_sp_router_xm_cache_flush_schedule(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_router_xm_flush_info *flush_info)
{
	unsigned long delay = usecs_to_jiffies(MLXSW_SP_ROUTER_XM_CACHE_DELAY);
	struct mlxsw_sp_router_xm_flush_info flush_all_info = {.all = true};
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;
	struct mlxsw_sp_router_xm_flush_node *flush_node;

	/* Check if the queued number of flushes reached critical amount after
	 * which it is better to just flush the whole cache.
	 */
	if (router_xm->flush_count == MLXSW_SP_ROUTER_XM_CACHE_PARALLEL_FLUSHES_LIMIT)
		/* Entering flush-all mode. */
		router_xm->flush_all_mode = true;

	if (router_xm->flush_all_mode)
		flush_info = &flush_all_info;

	rcu_read_lock();
	flush_node = rhashtable_lookup_fast(&router_xm->flush_ht, flush_info,
					    mlxsw_sp_router_xm_flush_ht_params);
	/* Take a reference so the object is not freed before possible
	 * delayed work cancel could be done.
	 */
	mlxsw_sp_router_xm_cache_flush_node_hold(flush_node);
	rcu_read_unlock();

	if (flush_node && mlxsw_sp_router_xm_cache_flush_may_cancel(flush_node)) {
		flush_node->reuses++;
		mlxsw_sp_router_xm_cache_flush_node_put(flush_node);
		 /* Original work was within wait period and was canceled.
		  * That means that the reference is still held and the
		  * flush_node_put() call above did not free the flush_node.
		  * Reschedule it with fresh delay.
		  */
		goto schedule_work;
	} else {
		mlxsw_sp_router_xm_cache_flush_node_put(flush_node);
	}

	flush_node = mlxsw_sp_router_xm_cache_flush_node_create(mlxsw_sp, flush_info);
	if (IS_ERR(flush_node))
		return PTR_ERR(flush_node);
	INIT_DELAYED_WORK(&flush_node->dw, mlxsw_sp_router_xm_cache_flush_work);

schedule_work:
	mlxsw_core_schedule_dw(&flush_node->dw, delay);
	return 0;
}

static int
mlxsw_sp_router_xm_ml_entry_add(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_router_xm_fib_entry *fib_entry)
{
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;
	struct mlxsw_sp_router_xm_ltable_node *ltable_node;
	u8 lvalue = fib_entry->lvalue;
	int err;

	ltable_node = mlxsw_sp_router_xm_ltable_node_get(router_xm,
							 fib_entry->mindex);
	if (IS_ERR(ltable_node))
		return PTR_ERR(ltable_node);
	if (lvalue > ltable_node->current_lvalue) {
		/* The L-value is bigger then the one currently set, update. */
		ltable_node->current_lvalue = lvalue;
		err = mlxsw_sp_router_xm_ltable_lvalue_set(mlxsw_sp,
							   ltable_node);
		if (err)
			goto err_lvalue_set;

		/* The L value for prefix/M is increased.
		 * Therefore, all entries in M and ML caches matching
		 * {prefix/M, proto, VR} need to be flushed. Set the flush
		 * prefix length to M to achieve that.
		 */
		fib_entry->flush_info.prefix_len = MLXSW_SP_ROUTER_XM_M_VAL;
	}

	ltable_node->lvalue_ref[lvalue]++;
	fib_entry->ltable_node = ltable_node;

	return 0;

err_lvalue_set:
	mlxsw_sp_router_xm_ltable_node_put(router_xm, ltable_node);
	return err;
}

static void
mlxsw_sp_router_xm_ml_entry_del(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_router_xm_fib_entry *fib_entry)
{
	struct mlxsw_sp_router_xm_ltable_node *ltable_node =
							fib_entry->ltable_node;
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;
	u8 lvalue = fib_entry->lvalue;

	ltable_node->lvalue_ref[lvalue]--;
	if (lvalue == ltable_node->current_lvalue && lvalue &&
	    !ltable_node->lvalue_ref[lvalue]) {
		u8 new_lvalue = lvalue - 1;

		/* Find the biggest L-value left out there. */
		while (new_lvalue > 0 && !ltable_node->lvalue_ref[lvalue])
			new_lvalue--;

		ltable_node->current_lvalue = new_lvalue;
		mlxsw_sp_router_xm_ltable_lvalue_set(mlxsw_sp, ltable_node);

		/* The L value for prefix/M is decreased.
		 * Therefore, all entries in M and ML caches matching
		 * {prefix/M, proto, VR} need to be flushed. Set the flush
		 * prefix length to M to achieve that.
		 */
		fib_entry->flush_info.prefix_len = MLXSW_SP_ROUTER_XM_M_VAL;
	}
	mlxsw_sp_router_xm_ltable_node_put(router_xm, ltable_node);
}

static int
mlxsw_sp_router_xm_ml_entries_add(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm)
{
	struct mlxsw_sp_router_xm_fib_entry *fib_entry;
	int err;
	int i;

	for (i = 0; i < op_ctx_xm->entries_count; i++) {
		fib_entry = op_ctx_xm->entries[i];
		err = mlxsw_sp_router_xm_ml_entry_add(mlxsw_sp, fib_entry);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	for (i--; i >= 0; i--) {
		fib_entry = op_ctx_xm->entries[i];
		mlxsw_sp_router_xm_ml_entry_del(mlxsw_sp, fib_entry);
	}
	return err;
}

static void
mlxsw_sp_router_xm_ml_entries_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm)
{
	struct mlxsw_sp_router_xm_fib_entry *fib_entry;
	int i;

	for (i = 0; i < op_ctx_xm->entries_count; i++) {
		fib_entry = op_ctx_xm->entries[i];
		mlxsw_sp_router_xm_ml_entry_del(mlxsw_sp, fib_entry);
	}
}

static void
mlxsw_sp_router_xm_ml_entries_cache_flush(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm)
{
	struct mlxsw_sp_router_xm_fib_entry *fib_entry;
	int err;
	int i;

	for (i = 0; i < op_ctx_xm->entries_count; i++) {
		fib_entry = op_ctx_xm->entries[i];
		err = mlxsw_sp_router_xm_cache_flush_schedule(mlxsw_sp,
							      &fib_entry->flush_info);
		if (err)
			dev_err(mlxsw_sp->bus_info->dev, "Failed to flush XM cache\n");
	}
}

static int mlxsw_sp_router_ll_xm_fib_entry_commit(struct mlxsw_sp *mlxsw_sp,
						  struct mlxsw_sp_fib_entry_op_ctx *op_ctx,
						  bool *postponed_for_bulk)
{
	struct mlxsw_sp_fib_entry_op_ctx_xm *op_ctx_xm = (void *) op_ctx->ll_priv;
	struct mlxsw_sp_router_xm_fib_entry *fib_entry;
	u8 num_rec;
	int err;
	int i;

	op_ctx_xm->trans_offset += op_ctx_xm->trans_item_len;
	op_ctx_xm->entries_count++;

	/* Check if bulking is possible and there is still room for another
	 * FIB entry record. The size of 'trans_item_len' is either size of IPv4
	 * command or size of IPv6 command. Not possible to mix those in a
	 * single XMDR write.
	 */
	if (op_ctx->bulk_ok &&
	    op_ctx_xm->trans_offset + op_ctx_xm->trans_item_len <= MLXSW_REG_XMDR_TRANS_LEN) {
		if (postponed_for_bulk)
			*postponed_for_bulk = true;
		return 0;
	}

	if (op_ctx->event == FIB_EVENT_ENTRY_REPLACE) {
		/* The L-table is updated inside. It has to be done before
		 * the prefix is inserted.
		 */
		err = mlxsw_sp_router_xm_ml_entries_add(mlxsw_sp, op_ctx_xm);
		if (err)
			goto out;
	}

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(xmdr), op_ctx_xm->xmdr_pl);
	if (err)
		goto out;
	num_rec = mlxsw_reg_xmdr_num_rec_get(op_ctx_xm->xmdr_pl);
	if (num_rec > op_ctx_xm->entries_count) {
		dev_err(mlxsw_sp->bus_info->dev, "Invalid XMDR number of records\n");
		err = -EIO;
		goto out;
	}
	for (i = 0; i < num_rec; i++) {
		if (!mlxsw_reg_xmdr_reply_vect_get(op_ctx_xm->xmdr_pl, i)) {
			dev_err(mlxsw_sp->bus_info->dev, "Command send over XMDR failed\n");
			err = -EIO;
			goto out;
		} else {
			fib_entry = op_ctx_xm->entries[i];
			fib_entry->committed = true;
		}
	}

	if (op_ctx->event == FIB_EVENT_ENTRY_DEL)
		/* The L-table is updated inside. It has to be done after
		 * the prefix was removed.
		 */
		mlxsw_sp_router_xm_ml_entries_del(mlxsw_sp, op_ctx_xm);

	/* At the very end, do the XLT cache flushing to evict stale
	 * M and ML cache entries after prefixes were inserted/removed.
	 */
	mlxsw_sp_router_xm_ml_entries_cache_flush(mlxsw_sp, op_ctx_xm);

out:
	/* Next pack call is going to do reinitialization */
	op_ctx->initialized = false;
	return err;
}

static bool mlxsw_sp_router_ll_xm_fib_entry_is_committed(struct mlxsw_sp_fib_entry_priv *priv)
{
	struct mlxsw_sp_router_xm_fib_entry *fib_entry = (void *) priv->priv;

	return fib_entry->committed;
}

const struct mlxsw_sp_router_ll_ops mlxsw_sp_router_ll_xm_ops = {
	.init = mlxsw_sp_router_ll_xm_init,
	.ralta_write = mlxsw_sp_router_ll_xm_ralta_write,
	.ralst_write = mlxsw_sp_router_ll_xm_ralst_write,
	.raltb_write = mlxsw_sp_router_ll_xm_raltb_write,
	.fib_entry_op_ctx_size = sizeof(struct mlxsw_sp_fib_entry_op_ctx_xm),
	.fib_entry_priv_size = sizeof(struct mlxsw_sp_router_xm_fib_entry),
	.fib_entry_pack = mlxsw_sp_router_ll_xm_fib_entry_pack,
	.fib_entry_act_remote_pack = mlxsw_sp_router_ll_xm_fib_entry_act_remote_pack,
	.fib_entry_act_local_pack = mlxsw_sp_router_ll_xm_fib_entry_act_local_pack,
	.fib_entry_act_ip2me_pack = mlxsw_sp_router_ll_xm_fib_entry_act_ip2me_pack,
	.fib_entry_act_ip2me_tun_pack = mlxsw_sp_router_ll_xm_fib_entry_act_ip2me_tun_pack,
	.fib_entry_commit = mlxsw_sp_router_ll_xm_fib_entry_commit,
	.fib_entry_is_committed = mlxsw_sp_router_ll_xm_fib_entry_is_committed,
};

#define MLXSW_SP_ROUTER_XM_MINDEX_SIZE (64 * 1024)

int mlxsw_sp_router_xm_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_router_xm *router_xm;
	char rxltm_pl[MLXSW_REG_RXLTM_LEN];
	char xltq_pl[MLXSW_REG_XLTQ_LEN];
	u32 mindex_size;
	u16 device_id;
	int err;

	if (!mlxsw_sp->bus_info->xm_exists)
		return 0;

	router_xm = kzalloc(sizeof(*router_xm), GFP_KERNEL);
	if (!router_xm)
		return -ENOMEM;

	mlxsw_reg_xltq_pack(xltq_pl);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(xltq), xltq_pl);
	if (err)
		goto err_xltq_query;
	mlxsw_reg_xltq_unpack(xltq_pl, &device_id, &router_xm->ipv4_supported,
			      &router_xm->ipv6_supported, &router_xm->entries_size, &mindex_size);

	if (device_id != MLXSW_REG_XLTQ_XM_DEVICE_ID_XLT) {
		dev_err(mlxsw_sp->bus_info->dev, "Invalid XM device id\n");
		err = -EINVAL;
		goto err_device_id_check;
	}

	if (mindex_size != MLXSW_SP_ROUTER_XM_MINDEX_SIZE) {
		dev_err(mlxsw_sp->bus_info->dev, "Unexpected M-index size\n");
		err = -EINVAL;
		goto err_mindex_size_check;
	}

	mlxsw_reg_rxltm_pack(rxltm_pl, mlxsw_sp_router_xm_m_val[MLXSW_SP_L3_PROTO_IPV4],
			     mlxsw_sp_router_xm_m_val[MLXSW_SP_L3_PROTO_IPV6]);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rxltm), rxltm_pl);
	if (err)
		goto err_rxltm_write;

	err = rhashtable_init(&router_xm->ltable_ht, &mlxsw_sp_router_xm_ltable_ht_params);
	if (err)
		goto err_ltable_ht_init;

	err = rhashtable_init(&router_xm->flush_ht, &mlxsw_sp_router_xm_flush_ht_params);
	if (err)
		goto err_flush_ht_init;

	mlxsw_sp->router->xm = router_xm;
	return 0;

err_flush_ht_init:
	rhashtable_destroy(&router_xm->ltable_ht);
err_ltable_ht_init:
err_rxltm_write:
err_mindex_size_check:
err_device_id_check:
err_xltq_query:
	kfree(router_xm);
	return err;
}

void mlxsw_sp_router_xm_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;

	if (!mlxsw_sp->bus_info->xm_exists)
		return;

	rhashtable_destroy(&router_xm->flush_ht);
	rhashtable_destroy(&router_xm->ltable_ht);
	kfree(router_xm);
}

bool mlxsw_sp_router_xm_ipv4_is_supported(const struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_router_xm *router_xm = mlxsw_sp->router->xm;

	return router_xm && router_xm->ipv4_supported;
}
