// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "spectrum_router.h"

struct mlxsw_sp_router_xm_fib_entry {
	bool committed;
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
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		len = mlxsw_reg_xmdr_c_ltr_pack6(op_ctx_xm->xmdr_pl, op_ctx_xm->trans_offset,
						 op_ctx_xm->entries_count, xmdr_c_ltr_op,
						 virtual_router, prefix_len, addr);
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
