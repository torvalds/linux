// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/mlx5/device.h>
#include <linux/mlx5/transobj.h>
#include "clock.h"
#include "aso.h"
#include "wq.h"

struct mlx5_aso_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq           wq;

	/* data path - accessed per napi poll */
	struct mlx5_core_cq        mcq;

	/* control */
	struct mlx5_core_dev      *mdev;
	struct mlx5_wq_ctrl        wq_ctrl;
} ____cacheline_aligned_in_smp;

struct mlx5_aso {
	/* data path */
	u16                        cc;
	u16                        pc;

	struct mlx5_wqe_ctrl_seg  *doorbell_cseg;
	struct mlx5_aso_cq         cq;

	/* read only */
	struct mlx5_wq_cyc         wq;
	void __iomem              *uar_map;
	u32                        sqn;

	/* control path */
	struct mlx5_wq_ctrl        wq_ctrl;

} ____cacheline_aligned_in_smp;

static void mlx5_aso_free_cq(struct mlx5_aso_cq *cq)
{
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static int mlx5_aso_alloc_cq(struct mlx5_core_dev *mdev, int numa_node,
			     void *cqc_data, struct mlx5_aso_cq *cq)
{
	struct mlx5_core_cq *mcq = &cq->mcq;
	struct mlx5_wq_param param;
	int err;
	u32 i;

	param.buf_numa_node = numa_node;
	param.db_numa_node = numa_node;

	err = mlx5_cqwq_create(mdev, &param, cqc_data, &cq->wq, &cq->wq_ctrl);
	if (err)
		return err;

	mcq->cqe_sz     = 64;
	mcq->set_ci_db  = cq->wq_ctrl.db.db;
	mcq->arm_db     = cq->wq_ctrl.db.db + 1;

	for (i = 0; i < mlx5_cqwq_get_size(&cq->wq); i++) {
		struct mlx5_cqe64 *cqe = mlx5_cqwq_get_wqe(&cq->wq, i);

		cqe->op_own = 0xf1;
	}

	cq->mdev = mdev;

	return 0;
}

static int create_aso_cq(struct mlx5_aso_cq *cq, void *cqc_data)
{
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_core_dev *mdev = cq->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	void *in, *cqc;
	int inlen, eqn;
	int err;

	err = mlx5_vector2eqn(mdev, 0, &eqn);
	if (err)
		return err;

	inlen = MLX5_ST_SZ_BYTES(create_cq_in) +
		sizeof(u64) * cq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);

	memcpy(cqc, cqc_data, MLX5_ST_SZ_BYTES(cqc));

	mlx5_fill_page_frag_array(&cq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(create_cq_in, in, pas));

	MLX5_SET(cqc,   cqc, cq_period_mode, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);
	MLX5_SET(cqc,   cqc, c_eqn_or_apu_element, eqn);
	MLX5_SET(cqc,   cqc, uar_page,      mdev->priv.uar->index);
	MLX5_SET(cqc,   cqc, log_page_size, cq->wq_ctrl.buf.page_shift -
					    MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(cqc, cqc, dbr_addr,      cq->wq_ctrl.db.dma);

	err = mlx5_core_create_cq(mdev, mcq, in, inlen, out, sizeof(out));

	kvfree(in);

	return err;
}

static void mlx5_aso_destroy_cq(struct mlx5_aso_cq *cq)
{
	mlx5_core_destroy_cq(cq->mdev, &cq->mcq);
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static int mlx5_aso_create_cq(struct mlx5_core_dev *mdev, int numa_node,
			      struct mlx5_aso_cq *cq)
{
	void *cqc_data;
	int err;

	cqc_data = kvzalloc(MLX5_ST_SZ_BYTES(cqc), GFP_KERNEL);
	if (!cqc_data)
		return -ENOMEM;

	MLX5_SET(cqc, cqc_data, log_cq_size, 1);
	MLX5_SET(cqc, cqc_data, uar_page, mdev->priv.uar->index);
	if (MLX5_CAP_GEN(mdev, cqe_128_always) && cache_line_size() >= 128)
		MLX5_SET(cqc, cqc_data, cqe_sz, CQE_STRIDE_128_PAD);

	err = mlx5_aso_alloc_cq(mdev, numa_node, cqc_data, cq);
	if (err) {
		mlx5_core_err(mdev, "Failed to alloc aso wq cq, err=%d\n", err);
		goto err_out;
	}

	err = create_aso_cq(cq, cqc_data);
	if (err) {
		mlx5_core_err(mdev, "Failed to create aso wq cq, err=%d\n", err);
		goto err_free_cq;
	}

	kvfree(cqc_data);
	return 0;

err_free_cq:
	mlx5_aso_free_cq(cq);
err_out:
	kvfree(cqc_data);
	return err;
}

static int mlx5_aso_alloc_sq(struct mlx5_core_dev *mdev, int numa_node,
			     void *sqc_data, struct mlx5_aso *sq)
{
	void *sqc_wq = MLX5_ADDR_OF(sqc, sqc_data, wq);
	struct mlx5_wq_cyc *wq = &sq->wq;
	struct mlx5_wq_param param;
	int err;

	sq->uar_map = mdev->mlx5e_res.hw_objs.bfreg.map;

	param.db_numa_node = numa_node;
	param.buf_numa_node = numa_node;
	err = mlx5_wq_cyc_create(mdev, &param, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db = &wq->db[MLX5_SND_DBR];

	return 0;
}

static int create_aso_sq(struct mlx5_core_dev *mdev, int pdn,
			 void *sqc_data, struct mlx5_aso *sq)
{
	void *in, *sqc, *wq;
	int inlen, err;
	u8 ts_format;

	inlen = MLX5_ST_SZ_BYTES(create_sq_in) +
		sizeof(u64) * sq->wq_ctrl.buf.npages;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	sqc = MLX5_ADDR_OF(create_sq_in, in, ctx);
	wq = MLX5_ADDR_OF(sqc, sqc, wq);

	memcpy(sqc, sqc_data, MLX5_ST_SZ_BYTES(sqc));
	MLX5_SET(sqc,  sqc, cqn, sq->cq.mcq.cqn);

	MLX5_SET(sqc,  sqc, state, MLX5_SQC_STATE_RST);
	MLX5_SET(sqc,  sqc, flush_in_error_en, 1);

	ts_format = mlx5_is_real_time_sq(mdev) ?
			MLX5_TIMESTAMP_FORMAT_REAL_TIME :
			MLX5_TIMESTAMP_FORMAT_FREE_RUNNING;
	MLX5_SET(sqc, sqc, ts_format, ts_format);

	MLX5_SET(wq,   wq, wq_type,       MLX5_WQ_TYPE_CYCLIC);
	MLX5_SET(wq,   wq, uar_page,      mdev->mlx5e_res.hw_objs.bfreg.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  sq->wq_ctrl.buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      sq->wq_ctrl.db.dma);

	mlx5_fill_page_frag_array(&sq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, &sq->sqn);

	kvfree(in);

	return err;
}

static int mlx5_aso_set_sq_rdy(struct mlx5_core_dev *mdev, u32 sqn)
{
	void *in, *sqc;
	int inlen, err;

	inlen = MLX5_ST_SZ_BYTES(modify_sq_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(modify_sq_in, in, sq_state, MLX5_SQC_STATE_RST);
	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RDY);

	err = mlx5_core_modify_sq(mdev, sqn, in);

	kvfree(in);

	return err;
}

static int mlx5_aso_create_sq_rdy(struct mlx5_core_dev *mdev, u32 pdn,
				  void *sqc_data, struct mlx5_aso *sq)
{
	int err;

	err = create_aso_sq(mdev, pdn, sqc_data, sq);
	if (err)
		return err;

	err = mlx5_aso_set_sq_rdy(mdev, sq->sqn);
	if (err)
		mlx5_core_destroy_sq(mdev, sq->sqn);

	return err;
}

static void mlx5_aso_free_sq(struct mlx5_aso *sq)
{
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static void mlx5_aso_destroy_sq(struct mlx5_aso *sq)
{
	mlx5_core_destroy_sq(sq->cq.mdev, sq->sqn);
	mlx5_aso_free_sq(sq);
}

static int mlx5_aso_create_sq(struct mlx5_core_dev *mdev, int numa_node,
			      u32 pdn, struct mlx5_aso *sq)
{
	void *sqc_data, *wq;
	int err;

	sqc_data = kvzalloc(MLX5_ST_SZ_BYTES(sqc), GFP_KERNEL);
	if (!sqc_data)
		return -ENOMEM;

	wq = MLX5_ADDR_OF(sqc, sqc_data, wq);
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd, pdn);
	MLX5_SET(wq, wq, log_wq_sz, 1);

	err = mlx5_aso_alloc_sq(mdev, numa_node, sqc_data, sq);
	if (err) {
		mlx5_core_err(mdev, "Failed to alloc aso wq sq, err=%d\n", err);
		goto err_out;
	}

	err = mlx5_aso_create_sq_rdy(mdev, pdn, sqc_data, sq);
	if (err) {
		mlx5_core_err(mdev, "Failed to open aso wq sq, err=%d\n", err);
		goto err_free_asosq;
	}

	mlx5_core_dbg(mdev, "aso sq->sqn = 0x%x\n", sq->sqn);

	kvfree(sqc_data);
	return 0;

err_free_asosq:
	mlx5_aso_free_sq(sq);
err_out:
	kvfree(sqc_data);
	return err;
}

struct mlx5_aso *mlx5_aso_create(struct mlx5_core_dev *mdev, u32 pdn)
{
	int numa_node = dev_to_node(mlx5_core_dma_dev(mdev));
	struct mlx5_aso *aso;
	int err;

	aso = kzalloc(sizeof(*aso), GFP_KERNEL);
	if (!aso)
		return ERR_PTR(-ENOMEM);

	err = mlx5_aso_create_cq(mdev, numa_node, &aso->cq);
	if (err)
		goto err_cq;

	err = mlx5_aso_create_sq(mdev, numa_node, pdn, aso);
	if (err)
		goto err_sq;

	return aso;

err_sq:
	mlx5_aso_destroy_cq(&aso->cq);
err_cq:
	kfree(aso);
	return ERR_PTR(err);
}

void mlx5_aso_destroy(struct mlx5_aso *aso)
{
	if (IS_ERR_OR_NULL(aso))
		return;

	mlx5_aso_destroy_sq(aso);
	mlx5_aso_destroy_cq(&aso->cq);
	kfree(aso);
}

void mlx5_aso_build_wqe(struct mlx5_aso *aso, u8 ds_cnt,
			struct mlx5_aso_wqe *aso_wqe,
			u32 obj_id, u32 opc_mode)
{
	struct mlx5_wqe_ctrl_seg *cseg = &aso_wqe->ctrl;

	cseg->opmod_idx_opcode = cpu_to_be32((opc_mode << MLX5_WQE_CTRL_WQE_OPC_MOD_SHIFT) |
					     (aso->pc << MLX5_WQE_CTRL_WQE_INDEX_SHIFT) |
					     MLX5_OPCODE_ACCESS_ASO);
	cseg->qpn_ds     = cpu_to_be32((aso->sqn << MLX5_WQE_CTRL_QPN_SHIFT) | ds_cnt);
	cseg->fm_ce_se   = MLX5_WQE_CTRL_CQ_UPDATE;
	cseg->general_id = cpu_to_be32(obj_id);
}

void *mlx5_aso_get_wqe(struct mlx5_aso *aso)
{
	u16 pi;

	pi = mlx5_wq_cyc_ctr2ix(&aso->wq, aso->pc);
	return mlx5_wq_cyc_get_wqe(&aso->wq, pi);
}

void mlx5_aso_post_wqe(struct mlx5_aso *aso, bool with_data,
		       struct mlx5_wqe_ctrl_seg *doorbell_cseg)
{
	doorbell_cseg->fm_ce_se |= MLX5_WQE_CTRL_CQ_UPDATE;
	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	if (with_data)
		aso->pc += MLX5_ASO_WQEBBS_DATA;
	else
		aso->pc += MLX5_ASO_WQEBBS;
	*aso->wq.db = cpu_to_be32(aso->pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	mlx5_write64((__be32 *)doorbell_cseg, aso->uar_map);

	/* Ensure doorbell is written on uar_page before poll_cq */
	WRITE_ONCE(doorbell_cseg, NULL);
}

int mlx5_aso_poll_cq(struct mlx5_aso *aso, bool with_data)
{
	struct mlx5_aso_cq *cq = &aso->cq;
	struct mlx5_cqe64 *cqe;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return -ETIMEDOUT;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	mlx5_cqwq_pop(&cq->wq);

	if (unlikely(get_cqe_opcode(cqe) != MLX5_CQE_REQ)) {
		struct mlx5_err_cqe *err_cqe;

		mlx5_core_err(cq->mdev, "Bad OP in ASOSQ CQE: 0x%x\n",
			      get_cqe_opcode(cqe));

		err_cqe = (struct mlx5_err_cqe *)cqe;
		mlx5_core_err(cq->mdev, "vendor_err_synd=%x\n",
			      err_cqe->vendor_err_synd);
		mlx5_core_err(cq->mdev, "syndrome=%x\n",
			      err_cqe->syndrome);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       16, 1, err_cqe,
			       sizeof(*err_cqe), false);
	}

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	if (with_data)
		aso->cc += MLX5_ASO_WQEBBS_DATA;
	else
		aso->cc += MLX5_ASO_WQEBBS;

	return 0;
}
