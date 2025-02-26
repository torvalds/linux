// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/io.h>
#include <linux/mlx5/transobj.h>
#include "lib/clock.h"
#include "mlx5_core.h"
#include "wq.h"

#define TEST_WC_NUM_WQES 255
#define TEST_WC_LOG_CQ_SZ (order_base_2(TEST_WC_NUM_WQES))
#define TEST_WC_SQ_LOG_WQ_SZ TEST_WC_LOG_CQ_SZ
#define TEST_WC_POLLING_MAX_TIME_JIFFIES msecs_to_jiffies(100)

struct mlx5_wc_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq wq;

	/* data path - accessed per napi poll */
	struct mlx5_core_cq mcq;

	/* control */
	struct mlx5_core_dev *mdev;
	struct mlx5_wq_ctrl wq_ctrl;
};

struct mlx5_wc_sq {
	/* data path */
	u16 cc;
	u16 pc;

	/* read only */
	struct mlx5_wq_cyc wq;
	u32 sqn;

	/* control path */
	struct mlx5_wq_ctrl wq_ctrl;

	struct mlx5_wc_cq cq;
	struct mlx5_sq_bfreg bfreg;
};

static int mlx5_wc_create_cqwq(struct mlx5_core_dev *mdev, void *cqc,
			       struct mlx5_wc_cq *cq)
{
	struct mlx5_core_cq *mcq = &cq->mcq;
	struct mlx5_wq_param param = {};
	int err;
	u32 i;

	err = mlx5_cqwq_create(mdev, &param, cqc, &cq->wq, &cq->wq_ctrl);
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

static int create_wc_cq(struct mlx5_wc_cq *cq, void *cqc_data)
{
	u32 out[MLX5_ST_SZ_DW(create_cq_out)];
	struct mlx5_core_dev *mdev = cq->mdev;
	struct mlx5_core_cq *mcq = &cq->mcq;
	int err, inlen, eqn;
	void *in, *cqc;

	err = mlx5_comp_eqn_get(mdev, 0, &eqn);
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

static int mlx5_wc_create_cq(struct mlx5_core_dev *mdev, struct mlx5_wc_cq *cq)
{
	void *cqc;
	int err;

	cqc = kvzalloc(MLX5_ST_SZ_BYTES(cqc), GFP_KERNEL);
	if (!cqc)
		return -ENOMEM;

	MLX5_SET(cqc, cqc, log_cq_size, TEST_WC_LOG_CQ_SZ);
	MLX5_SET(cqc, cqc, uar_page, mdev->priv.uar->index);
	if (MLX5_CAP_GEN(mdev, cqe_128_always) && cache_line_size() >= 128)
		MLX5_SET(cqc, cqc, cqe_sz, CQE_STRIDE_128_PAD);

	err = mlx5_wc_create_cqwq(mdev, cqc, cq);
	if (err) {
		mlx5_core_err(mdev, "Failed to create wc cq wq, err=%d\n", err);
		goto err_create_cqwq;
	}

	err = create_wc_cq(cq, cqc);
	if (err) {
		mlx5_core_err(mdev, "Failed to create wc cq, err=%d\n", err);
		goto err_create_cq;
	}

	kvfree(cqc);
	return 0;

err_create_cq:
	mlx5_wq_destroy(&cq->wq_ctrl);
err_create_cqwq:
	kvfree(cqc);
	return err;
}

static void mlx5_wc_destroy_cq(struct mlx5_wc_cq *cq)
{
	mlx5_core_destroy_cq(cq->mdev, &cq->mcq);
	mlx5_wq_destroy(&cq->wq_ctrl);
}

static int create_wc_sq(struct mlx5_core_dev *mdev, void *sqc_data,
			struct mlx5_wc_sq *sq)
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
	MLX5_SET(wq,   wq, uar_page,      sq->bfreg.index);
	MLX5_SET(wq,   wq, log_wq_pg_sz,  sq->wq_ctrl.buf.page_shift -
					  MLX5_ADAPTER_PAGE_SHIFT);
	MLX5_SET64(wq, wq, dbr_addr,      sq->wq_ctrl.db.dma);

	mlx5_fill_page_frag_array(&sq->wq_ctrl.buf,
				  (__be64 *)MLX5_ADDR_OF(wq, wq, pas));

	err = mlx5_core_create_sq(mdev, in, inlen, &sq->sqn);
	if (err) {
		mlx5_core_err(mdev, "Failed to create wc sq, err=%d\n", err);
		goto err_create_sq;
	}

	memset(in, 0,  MLX5_ST_SZ_BYTES(modify_sq_in));
	MLX5_SET(modify_sq_in, in, sq_state, MLX5_SQC_STATE_RST);
	sqc = MLX5_ADDR_OF(modify_sq_in, in, ctx);
	MLX5_SET(sqc, sqc, state, MLX5_SQC_STATE_RDY);

	err = mlx5_core_modify_sq(mdev, sq->sqn, in);
	if (err) {
		mlx5_core_err(mdev, "Failed to set wc sq(sqn=0x%x) ready, err=%d\n",
			      sq->sqn, err);
		goto err_modify_sq;
	}

	kvfree(in);
	return 0;

err_modify_sq:
	mlx5_core_destroy_sq(mdev, sq->sqn);
err_create_sq:
	kvfree(in);
	return err;
}

static int mlx5_wc_create_sq(struct mlx5_core_dev *mdev, struct mlx5_wc_sq *sq)
{
	struct mlx5_wq_param param = {};
	void *sqc_data, *wq;
	int err;

	sqc_data = kvzalloc(MLX5_ST_SZ_BYTES(sqc), GFP_KERNEL);
	if (!sqc_data)
		return -ENOMEM;

	wq = MLX5_ADDR_OF(sqc, sqc_data, wq);
	MLX5_SET(wq, wq, log_wq_stride, ilog2(MLX5_SEND_WQE_BB));
	MLX5_SET(wq, wq, pd, mdev->mlx5e_res.hw_objs.pdn);
	MLX5_SET(wq, wq, log_wq_sz, TEST_WC_SQ_LOG_WQ_SZ);

	err = mlx5_wq_cyc_create(mdev, &param, wq, &sq->wq, &sq->wq_ctrl);
	if (err) {
		mlx5_core_err(mdev, "Failed to create wc sq wq, err=%d\n", err);
		goto err_create_wq_cyc;
	}

	err = create_wc_sq(mdev, sqc_data, sq);
	if (err)
		goto err_create_sq;

	mlx5_core_dbg(mdev, "wc sq->sqn = 0x%x created\n", sq->sqn);

	kvfree(sqc_data);
	return 0;

err_create_sq:
	mlx5_wq_destroy(&sq->wq_ctrl);
err_create_wq_cyc:
	kvfree(sqc_data);
	return err;
}

static void mlx5_wc_destroy_sq(struct mlx5_wc_sq *sq)
{
	mlx5_core_destroy_sq(sq->cq.mdev, sq->sqn);
	mlx5_wq_destroy(&sq->wq_ctrl);
}

static void mlx5_wc_post_nop(struct mlx5_wc_sq *sq, bool signaled)
{
	int buf_size = (1 << MLX5_CAP_GEN(sq->cq.mdev, log_bf_reg_size)) / 2;
	struct mlx5_wqe_ctrl_seg *ctrl;
	__be32 mmio_wqe[16] = {};
	u16 pi;

	pi = mlx5_wq_cyc_ctr2ix(&sq->wq, sq->pc);
	ctrl = mlx5_wq_cyc_get_wqe(&sq->wq, pi);
	memset(ctrl, 0, sizeof(*ctrl));
	ctrl->opmod_idx_opcode =
		cpu_to_be32((sq->pc << MLX5_WQE_CTRL_WQE_INDEX_SHIFT) | MLX5_OPCODE_NOP);
	ctrl->qpn_ds =
		cpu_to_be32((sq->sqn << MLX5_WQE_CTRL_QPN_SHIFT) |
			    DIV_ROUND_UP(sizeof(struct mlx5_wqe_ctrl_seg), MLX5_SEND_WQE_DS));
	if (signaled)
		ctrl->fm_ce_se |= MLX5_WQE_CTRL_CQ_UPDATE;

	memcpy(mmio_wqe, ctrl, sizeof(*ctrl));
	((struct mlx5_wqe_ctrl_seg *)&mmio_wqe)->fm_ce_se |=
		MLX5_WQE_CTRL_CQ_UPDATE;

	/* ensure wqe is visible to device before updating doorbell record */
	dma_wmb();

	sq->pc++;
	sq->wq.db[MLX5_SND_DBR] = cpu_to_be32(sq->pc);

	/* ensure doorbell record is visible to device before ringing the
	 * doorbell
	 */
	wmb();

	__iowrite64_copy(sq->bfreg.map + sq->bfreg.offset, mmio_wqe,
			 sizeof(mmio_wqe) / 8);

	sq->bfreg.offset ^= buf_size;
}

static int mlx5_wc_poll_cq(struct mlx5_wc_sq *sq)
{
	struct mlx5_wc_cq *cq = &sq->cq;
	struct mlx5_cqe64 *cqe;

	cqe = mlx5_cqwq_get_cqe(&cq->wq);
	if (!cqe)
		return -ETIMEDOUT;

	/* sq->cc must be updated only after mlx5_cqwq_update_db_record(),
	 * otherwise a cq overrun may occur
	 */
	mlx5_cqwq_pop(&cq->wq);

	if (get_cqe_opcode(cqe) == MLX5_CQE_REQ) {
		int wqe_counter = be16_to_cpu(cqe->wqe_counter);
		struct mlx5_core_dev *mdev = cq->mdev;

		if (wqe_counter == TEST_WC_NUM_WQES - 1)
			mdev->wc_state = MLX5_WC_STATE_UNSUPPORTED;
		else
			mdev->wc_state = MLX5_WC_STATE_SUPPORTED;

		mlx5_core_dbg(mdev, "wc wqe_counter = 0x%x\n", wqe_counter);
	}

	mlx5_cqwq_update_db_record(&cq->wq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	sq->cc++;

	return 0;
}

static void mlx5_core_test_wc(struct mlx5_core_dev *mdev)
{
	unsigned long expires;
	struct mlx5_wc_sq *sq;
	int i, err;

	if (mdev->wc_state != MLX5_WC_STATE_UNINITIALIZED)
		return;

	sq = kzalloc(sizeof(*sq), GFP_KERNEL);
	if (!sq)
		return;

	err = mlx5_alloc_bfreg(mdev, &sq->bfreg, true, false);
	if (err) {
		mlx5_core_err(mdev, "Failed to alloc bfreg for wc, err=%d\n", err);
		goto err_alloc_bfreg;
	}

	err = mlx5_wc_create_cq(mdev, &sq->cq);
	if (err)
		goto err_create_cq;

	err = mlx5_wc_create_sq(mdev, sq);
	if (err)
		goto err_create_sq;

	for (i = 0; i < TEST_WC_NUM_WQES - 1; i++)
		mlx5_wc_post_nop(sq, false);

	mlx5_wc_post_nop(sq, true);

	expires = jiffies + TEST_WC_POLLING_MAX_TIME_JIFFIES;
	do {
		err = mlx5_wc_poll_cq(sq);
		if (err)
			usleep_range(2, 10);
	} while (mdev->wc_state == MLX5_WC_STATE_UNINITIALIZED &&
		 time_is_after_jiffies(expires));

	mlx5_wc_destroy_sq(sq);

err_create_sq:
	mlx5_wc_destroy_cq(&sq->cq);
err_create_cq:
	mlx5_free_bfreg(mdev, &sq->bfreg);
err_alloc_bfreg:
	kfree(sq);
}

bool mlx5_wc_support_get(struct mlx5_core_dev *mdev)
{
	struct mutex *wc_state_lock = &mdev->wc_state_lock;
	struct mlx5_core_dev *parent = NULL;

	if (!MLX5_CAP_GEN(mdev, bf)) {
		mlx5_core_dbg(mdev, "BlueFlame not supported\n");
		goto out;
	}

	if (!MLX5_CAP_GEN(mdev, log_max_sq)) {
		mlx5_core_dbg(mdev, "SQ not supported\n");
		goto out;
	}

	if (mdev->wc_state != MLX5_WC_STATE_UNINITIALIZED)
		/* No need to lock anything as we perform WC test only
		 * once for whole device and was already done.
		 */
		goto out;

#ifdef CONFIG_MLX5_SF
	if (mlx5_core_is_sf(mdev)) {
		parent = mdev->priv.parent_mdev;
		wc_state_lock = &parent->wc_state_lock;
	}
#endif

	mutex_lock(wc_state_lock);

	if (mdev->wc_state != MLX5_WC_STATE_UNINITIALIZED)
		goto unlock;

	if (parent) {
		mlx5_core_test_wc(parent);

		mlx5_core_dbg(mdev, "parent set wc_state=%d\n",
			      parent->wc_state);
		mdev->wc_state = parent->wc_state;

	} else {
		mlx5_core_test_wc(mdev);
	}

unlock:
	mutex_unlock(wc_state_lock);
out:
	mlx5_core_dbg(mdev, "wc_state=%d\n", mdev->wc_state);

	return mdev->wc_state == MLX5_WC_STATE_SUPPORTED;
}
EXPORT_SYMBOL(mlx5_wc_support_get);
