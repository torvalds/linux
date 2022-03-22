// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <linux/pm_runtime.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#include "mpp_rkvdec2_link.h"

#include "hack/mpp_rkvdec2_hack_rk3568.c"

/*
 * hardware information
 */
static struct mpp_hw_info rkvdec_v2_hw_info = {
	.reg_num = RKVDEC_REG_NUM,
	.reg_id = RKVDEC_REG_HW_ID_INDEX,
	.reg_start = RKVDEC_REG_START_INDEX,
	.reg_end = RKVDEC_REG_END_INDEX,
	.reg_en = RKVDEC_REG_START_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_h264d[] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
	161, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176,
	177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
	192, 193, 194, 195, 196, 197
};

static const u16 trans_tbl_h265d[] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
	161, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176,
	177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
	192, 193, 194, 195, 196, 197
};

static const u16 trans_tbl_vp9d[] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
	160, 162, 164, 165, 166, 167, 168, 169, 170, 171, 172, 180, 181, 182, 183,
	184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197
};

static const u16 trans_tbl_avs2d[] = {
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
	161, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176,
	177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
	192, 193, 194, 195, 196, 197
};

static struct mpp_trans_info rkvdec_v2_trans[] = {
	[RKVDEC_FMT_H265D] = {
		.count = ARRAY_SIZE(trans_tbl_h265d),
		.table = trans_tbl_h265d,
	},
	[RKVDEC_FMT_H264D] = {
		.count = ARRAY_SIZE(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[RKVDEC_FMT_VP9D] = {
		.count = ARRAY_SIZE(trans_tbl_vp9d),
		.table = trans_tbl_vp9d,
	},
	[RKVDEC_FMT_AVS2] = {
		.count = ARRAY_SIZE(trans_tbl_avs2d),
		.table = trans_tbl_avs2d,
	}
};

static int mpp_extract_rcb_info(struct rkvdec2_rcb_info *rcb_inf,
				struct mpp_request *req)
{
	int max_size = ARRAY_SIZE(rcb_inf->elem);
	int cnt = req->size / sizeof(rcb_inf->elem[0]);

	if (req->size > sizeof(rcb_inf->elem)) {
		mpp_err("count %d,max_size %d\n", cnt, max_size);
		return -EINVAL;
	}
	if (copy_from_user(rcb_inf->elem, req->data, req->size)) {
		mpp_err("copy_from_user failed\n");
		return -EINVAL;
	}
	rcb_inf->cnt = cnt;

	return 0;
}

static int rkvdec2_extract_task_msg(struct mpp_session *session,
				    struct rkvdec2_task *task,
				    struct mpp_task_msgs *msgs)
{
	u32 i;
	int ret;
	struct mpp_request *req;
	struct mpp_hw_info *hw_info = task->mpp_task.hw_info;

	for (i = 0; i < msgs->req_cnt; i++) {
		u32 off_s, off_e;

		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			off_s = hw_info->reg_start * sizeof(u32);
			off_e = hw_info->reg_end * sizeof(u32);
			ret = mpp_check_req(req, 0, sizeof(task->reg), off_s, off_e);
			if (ret)
				continue;
			if (copy_from_user((u8 *)task->reg + req->offset,
					   req->data, req->size)) {
				mpp_err("copy_from_user reg failed\n");
				return -EIO;
			}
			memcpy(&task->w_reqs[task->w_req_cnt++], req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_READ: {
			int req_base;
			int max_size;

			if (req->offset >= RKVDEC_PERF_SEL_OFFSET) {
				req_base = RKVDEC_PERF_SEL_OFFSET;
				max_size = sizeof(task->reg_sel);
			} else {
				req_base = 0;
				max_size = sizeof(task->reg);
			}

			ret = mpp_check_req(req, req_base, max_size, 0, max_size);
			if (ret)
				continue;

			memcpy(&task->r_reqs[task->r_req_cnt++], req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_ADDR_OFFSET: {
			mpp_extract_reg_offset_info(&task->off_inf, req);
		} break;
		case MPP_CMD_SET_RCB_INFO: {
			struct rkvdec2_session_priv *priv = session->priv;

			if (priv)
				mpp_extract_rcb_info(&priv->rcb_inf, req);
		} break;
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt %d, r_req_cnt %d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

int mpp_set_rcbbuf(struct mpp_dev *mpp, struct mpp_session *session,
		   struct mpp_task *task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec2_session_priv *priv = session->priv;

	mpp_debug_enter();

	if (priv && dec->rcb_iova) {
		int i;
		u32 reg_idx, rcb_size, rcb_offset;
		struct rkvdec2_rcb_info *rcb_inf = &priv->rcb_inf;
		u32 width = priv->codec_info[DEC_INFO_WIDTH].val;

		if (width < dec->rcb_min_width)
			goto done;

		rcb_offset = 0;
		for (i = 0; i < rcb_inf->cnt; i++) {
			reg_idx = rcb_inf->elem[i].index;
			rcb_size = rcb_inf->elem[i].size;
			if ((rcb_offset + rcb_size) > dec->rcb_size) {
				mpp_debug(DEBUG_SRAM_INFO,
					  "rcb: reg %d use original buffer\n", reg_idx);
				continue;
			}
			mpp_debug(DEBUG_SRAM_INFO, "rcb: reg %d offset %d, size %d\n",
				  reg_idx, rcb_offset, rcb_size);
			task->reg[reg_idx] = dec->rcb_iova + rcb_offset;
			rcb_offset += rcb_size;
		}
	}
done:
	mpp_debug_leave();

	return 0;
}

int rkvdec2_task_init(struct mpp_dev *mpp, struct mpp_session *session,
		      struct rkvdec2_task *task, struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = &task->mpp_task;

	mpp_debug_enter();

	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	mpp_task->reg = task->reg;
	/* extract reqs for current task */
	ret = rkvdec2_extract_task_msg(session, task, msgs);
	if (ret)
		return ret;

	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_FORMAT_INDEX]);

		ret = mpp_translate_reg_address(session, mpp_task,
						fmt, task->reg, &task->off_inf);
		if (ret)
			goto fail;

		mpp_translate_reg_offset_info(mpp_task, &task->off_inf, task->reg);
	}

	task->strm_addr = task->reg[RKVDEC_REG_RLC_BASE_INDEX];
	task->clk_mode = CLK_MODE_NORMAL;
	task->slot_idx = -1;
	init_waitqueue_head(&task->wait);
	/* get resolution info */
	if (session->priv) {
		struct rkvdec2_session_priv *priv = session->priv;
		u32 width = priv->codec_info[DEC_INFO_WIDTH].val;
		u32 bitdepth = priv->codec_info[DEC_INFO_BITDEPTH].val;

		task->width =  (bitdepth > 8) ? ((width * bitdepth + 7) >> 3) : width;
		task->height = priv->codec_info[DEC_INFO_HEIGHT].val;
		task->pixels = task->width * task->height;
		mpp_debug(DEBUG_TASK_INFO, "width=%d, bitdepth=%d, height=%d\n",
			  width, bitdepth, task->height);
	}

	mpp_debug_leave();

	return 0;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	return ret;
}

void *rkvdec2_alloc_task(struct mpp_session *session,
			 struct mpp_task_msgs *msgs)
{
	int ret;
	struct rkvdec2_task *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	ret = rkvdec2_task_init(session->mpp, session, task, msgs);
	if (ret) {
		kfree(task);
		return NULL;
	}
	mpp_set_rcbbuf(session->mpp, session, &task->mpp_task);

	return &task->mpp_task;
}

static void *rkvdec2_rk3568_alloc_task(struct mpp_session *session,
				struct mpp_task_msgs *msgs)
{
	u32 fmt;
	struct mpp_task *mpp_task = NULL;
	struct rkvdec2_task *task = NULL;

	mpp_task = rkvdec2_alloc_task(session, msgs);
	if (!mpp_task)
		return NULL;

	task = to_rkvdec2_task(mpp_task);
	fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_FORMAT_INDEX]);
	/* workaround for rk356x, fix the hw bug of cabac/cavlc switch only in h264d */
	task->need_hack = (fmt == RKVDEC_FMT_H264D);

	return mpp_task;
}

static int rkvdec2_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	u32 reg_en = mpp_task->hw_info->reg_en;
	/* set cache size */
	u32 reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS |
		  RKVDEC_CACHE_PERMIT_READ_ALLOCATE;
	int i;

	mpp_debug_enter();

	if (!mpp_debug_unlikely(DEBUG_CACHE_32B))
		reg |= RKVDEC_CACHE_LINE_SIZE_64_BYTES;

	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE1_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE2_SIZE_BASE, reg);
	/* clear cache */
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE0_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE1_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE2_BASE, 1);

	/* set registers for hardware */
	for (i = 0; i < task->w_req_cnt; i++) {
		int s, e;
		struct mpp_request *req = &task->w_reqs[i];

		s = req->offset / sizeof(u32);
		e = s + req->size / sizeof(u32);
		mpp_write_req(mpp, task->reg, s, e, reg_en);
	}

	/* flush tlb before starting hardware */
	mpp_iommu_flush_tlb(mpp->iommu_info);

	/* init current task */
	mpp->cur_task = mpp_task;
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, RKVDEC_REG_START_EN_BASE, task->reg[reg_en] | RKVDEC_START_EN);

	mpp_debug_leave();

	return 0;
}

static int rkvdec2_rk3568_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	int ret = 0;

	mpp_debug_enter();

	/*
	 * run fix before task processing
	 * workaround for rk356x, fix the hw bug of cabac/cavlc switch only in h264d
	 */
	if (task->need_hack)
		rkvdec2_3568_hack_fix(mpp);

	ret = rkvdec2_run(mpp, mpp_task);

	mpp_debug_leave();

	return ret;
}

static int rkvdec2_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, RKVDEC_REG_INT_EN);
	if (!(mpp->irq_status & RKVDEC_IRQ_RAW))
		return IRQ_NONE;

	mpp_write(mpp, RKVDEC_REG_INT_EN, 0);

	return IRQ_WAKE_THREAD;
}

static int rkvdec2_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct rkvdec2_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvdec2_task(mpp_task);
	task->irq_status = mpp->irq_status;

	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);
	err_mask = RKVDEC_COLMV_REF_ERR_STA | RKVDEC_BUF_EMPTY_STA |
		   RKVDEC_TIMEOUT_STA | RKVDEC_ERROR_STA;
	if (err_mask & task->irq_status) {
		atomic_inc(&mpp->reset_request);
		mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n", task->irq_status);
		mpp_task_dump_hw_reg(mpp);
	}

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();
	return IRQ_HANDLED;
}

static int rkvdec2_read_perf_sel(struct mpp_dev *mpp, u32 *regs, u32 s, u32 e)
{
	u32 i;
	u32 sel0, sel1, sel2, val;

	for (i = s; i < e; i += 3) {
		/* set sel */
		sel0 = i;
		sel1 = ((i + 1) < e) ? (i + 1) : 0;
		sel2 = ((i + 2) < e) ? (i + 2) : 0;
		val = RKVDEC_SET_PERF_SEL(sel0, sel1, sel2);
		writel_relaxed(val, mpp->reg_base + RKVDEC_PERF_SEL_BASE);
		/* read data */
		regs[sel0] = readl_relaxed(mpp->reg_base + RKVDEC_SEL_VAL0_BASE);
		mpp_debug(DEBUG_GET_PERF_VAL, "sel[%d]:%u\n", sel0, regs[sel0]);
		if (sel1) {
			regs[sel1] = readl_relaxed(mpp->reg_base + RKVDEC_SEL_VAL1_BASE);
			mpp_debug(DEBUG_GET_PERF_VAL, "sel[%d]:%u\n", sel1, regs[sel1]);
		}
		if (sel2) {
			regs[sel2] = readl_relaxed(mpp->reg_base + RKVDEC_SEL_VAL2_BASE);
			mpp_debug(DEBUG_GET_PERF_VAL, "sel[%d]:%u\n", sel2, regs[sel2]);
		}
	}

	return 0;
}

static int rkvdec2_finish(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i;
	u32 dec_get;
	s32 dec_length;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	struct mpp_request *req;
	u32 s, e;

	mpp_debug_enter();

	/* read register after running */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];
		/* read perf register */
		if (req->offset >= RKVDEC_PERF_SEL_OFFSET) {
			int off = req->offset - RKVDEC_PERF_SEL_OFFSET;

			s = off / sizeof(u32);
			e = s + req->size / sizeof(u32);
			rkvdec2_read_perf_sel(mpp, task->reg_sel, s, e);
		} else {
			s = req->offset / sizeof(u32);
			e = s + req->size / sizeof(u32);
			mpp_read_req(mpp, task->reg, s, e);
		}
	}
	/* revert hack for irq status */
	task->reg[RKVDEC_REG_INT_EN_INDEX] = task->irq_status;
	/* revert hack for decoded length */
	dec_get = mpp_read_relaxed(mpp, RKVDEC_REG_RLC_BASE);
	dec_length = dec_get - task->strm_addr;
	task->reg[RKVDEC_REG_RLC_BASE_INDEX] = dec_length << 10;
	mpp_debug(DEBUG_REGISTER, "dec_get %08x dec_length %d\n", dec_get, dec_length);

	mpp_debug_leave();

	return 0;
}

int rkvdec2_result(struct mpp_dev *mpp, struct mpp_task *mpp_task,
		   struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];

		if (req->offset >= RKVDEC_PERF_SEL_OFFSET) {
			int off = req->offset - RKVDEC_PERF_SEL_OFFSET;

			if (copy_to_user(req->data,
					 (u8 *)task->reg_sel + off,
					 req->size)) {
				mpp_err("copy_to_user perf_sel fail\n");
				return -EIO;
			}
		} else {
			if (copy_to_user(req->data,
					 (u8 *)task->reg + req->offset,
					 req->size)) {
				mpp_err("copy_to_user reg fail\n");
				return -EIO;
			}
		}
	}

	return 0;
}

int rkvdec2_free_task(struct mpp_session *session, struct mpp_task *mpp_task)
{
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

static int rkvdec2_control(struct mpp_session *session, struct mpp_request *req)
{
	switch (req->cmd) {
	case MPP_CMD_SEND_CODEC_INFO: {
		int i;
		int cnt;
		struct codec_info_elem elem;
		struct rkvdec2_session_priv *priv;

		if (!session || !session->priv) {
			mpp_err("session info null\n");
			return -EINVAL;
		}
		priv = session->priv;

		cnt = req->size / sizeof(elem);
		cnt = (cnt > DEC_INFO_BUTT) ? DEC_INFO_BUTT : cnt;
		mpp_debug(DEBUG_IOCTL, "codec info count %d\n", cnt);
		for (i = 0; i < cnt; i++) {
			if (copy_from_user(&elem, req->data + i * sizeof(elem), sizeof(elem))) {
				mpp_err("copy_from_user failed\n");
				continue;
			}
			if (elem.type > DEC_INFO_BASE && elem.type < DEC_INFO_BUTT &&
			    elem.flag > CODEC_INFO_FLAG_NULL && elem.flag < CODEC_INFO_FLAG_BUTT) {
				elem.type = array_index_nospec(elem.type, DEC_INFO_BUTT);
				priv->codec_info[elem.type].flag = elem.flag;
				priv->codec_info[elem.type].val = elem.data;
			} else {
				mpp_err("codec info invalid, type %d, flag %d\n",
					elem.type, elem.flag);
			}
		}
	} break;
	default: {
		mpp_err("unknown mpp ioctl cmd %x\n", req->cmd);
	} break;
	}

	return 0;
}

int rkvdec2_free_session(struct mpp_session *session)
{
	if (session && session->priv) {
		kfree(session->priv);
		session->priv = NULL;
	}

	return 0;
}

static int rkvdec2_init_session(struct mpp_session *session)
{
	struct rkvdec2_session_priv *priv;

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	session->priv = priv;

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int rkvdec2_procfs_remove(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int rkvdec2_show_pref_sel_offset(struct seq_file *file, void *v)
{
	seq_printf(file, "0x%08x\n", RKVDEC_PERF_SEL_OFFSET);

	return 0;
}

static int rkvdec2_procfs_init(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	char name[32];

	if (!mpp->dev || !mpp->dev->of_node || !mpp->dev->of_node->name ||
	    !mpp->srv || !mpp->srv->procfs)
		return -EINVAL;

	snprintf(name, sizeof(name) - 1, "%s%d",
		 mpp->dev->of_node->name, mpp->core_id);
	dec->procfs = proc_mkdir(name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(dec->procfs)) {
		mpp_err("failed on open procfs\n");
		dec->procfs = NULL;
		return -EIO;
	}
	mpp_procfs_create_u32("aclk", 0644,
			      dec->procfs, &dec->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_core", 0644,
			      dec->procfs, &dec->core_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_cabac", 0644,
			      dec->procfs, &dec->cabac_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_hevc_cabac", 0644,
			      dec->procfs, &dec->hevc_cabac_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      dec->procfs, &mpp->session_max_buffers);
	proc_create_single("perf_sel_offset", 0444,
			   dec->procfs, rkvdec2_show_pref_sel_offset);
	mpp_procfs_create_u32("task_count", 0644,
			      dec->procfs, &mpp->task_index);
	mpp_procfs_create_u32("disable_work", 0644,
			      dec->procfs, &dec->disable_work);

	return 0;
}
#else
static inline int rkvdec2_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvdec2_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int rkvdec2_init(struct mpp_dev *mpp)
{
	int ret;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	mutex_init(&dec->sip_reset_lock);
	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVDEC];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &dec->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->core_clk_info, "clk_core");
	if (ret)
		mpp_err("failed on clk_get clk_core\n");
	ret = mpp_get_clk_info(mpp, &dec->cabac_clk_info, "clk_cabac");
	if (ret)
		mpp_err("failed on clk_get clk_cabac\n");
	ret = mpp_get_clk_info(mpp, &dec->hevc_cabac_clk_info, "clk_hevc_cabac");
	if (ret)
		mpp_err("failed on clk_get clk_hevc_cabac\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&dec->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->core_clk_info, CLK_MODE_DEFAULT, 200 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->cabac_clk_info, CLK_MODE_DEFAULT, 200 * MHZ);
	mpp_set_clk_info_rate_hz(&dec->hevc_cabac_clk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	/* Get normal max workload from dtsi */
	of_property_read_u32(mpp->dev->of_node,
			     "rockchip,default-max-load", &dec->default_max_load);
	/* Get reset control from dtsi */
	dec->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!dec->rst_a)
		mpp_err("No aclk reset resource define\n");
	dec->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!dec->rst_h)
		mpp_err("No hclk reset resource define\n");
	dec->rst_niu_a = mpp_reset_control_get(mpp, RST_TYPE_NIU_A, "niu_a");
	if (!dec->rst_niu_a)
		mpp_err("No niu aclk reset resource define\n");
	dec->rst_niu_h = mpp_reset_control_get(mpp, RST_TYPE_NIU_H, "niu_h");
	if (!dec->rst_niu_h)
		mpp_err("No niu hclk reset resource define\n");
	dec->rst_core = mpp_reset_control_get(mpp, RST_TYPE_CORE, "video_core");
	if (!dec->rst_core)
		mpp_err("No core reset resource define\n");
	dec->rst_cabac = mpp_reset_control_get(mpp, RST_TYPE_CABAC, "video_cabac");
	if (!dec->rst_cabac)
		mpp_err("No cabac reset resource define\n");
	dec->rst_hevc_cabac = mpp_reset_control_get(mpp, RST_TYPE_HEVC_CABAC, "video_hevc_cabac");
	if (!dec->rst_hevc_cabac)
		mpp_err("No hevc cabac reset resource define\n");

	return 0;
}

static int rkvdec2_rk3568_init(struct mpp_dev *mpp)
{
	int ret;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	dec->fix = mpp_dma_alloc(mpp->dev, FIX_RK3568_BUF_SIZE);
	ret = dec->fix ? 0 : -ENOMEM;
	if (!ret)
		rkvdec2_3568_hack_data_setup(dec->fix);
	else
		dev_err(mpp->dev, "failed to create buffer for hack\n");

	ret = rkvdec2_init(mpp);

	return ret;
}

static int rkvdec2_rk3568_exit(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	if (dec->fix)
		mpp_dma_free(dec->fix);

	return 0;
}

static int rkvdec2_clk_on(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);
	mpp_clk_safe_enable(dec->core_clk_info.clk);
	mpp_clk_safe_enable(dec->cabac_clk_info.clk);
	mpp_clk_safe_enable(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec2_clk_off(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	clk_disable_unprepare(dec->aclk_info.clk);
	clk_disable_unprepare(dec->hclk_info.clk);
	clk_disable_unprepare(dec->core_clk_info.clk);
	clk_disable_unprepare(dec->cabac_clk_info.clk);
	clk_disable_unprepare(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec2_get_freq(struct mpp_dev *mpp,
			    struct mpp_task *mpp_task)
{
	u32 task_cnt;
	u32 workload;
	struct mpp_task *loop = NULL, *n;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	/* if not set max load, consider not have advanced mode */
	if (!dec->default_max_load || !task->pixels)
		return 0;

	task_cnt = 1;
	workload = task->pixels;
	/* calc workload in pending list */
	mutex_lock(&mpp->queue->pending_lock);
	list_for_each_entry_safe(loop, n,
				 &mpp->queue->pending_list,
				 queue_link) {
		struct rkvdec2_task *loop_task = to_rkvdec2_task(loop);

		task_cnt++;
		workload += loop_task->pixels;
	}
	mutex_unlock(&mpp->queue->pending_lock);

	if (workload > dec->default_max_load)
		task->clk_mode = CLK_MODE_ADVANCED;

	mpp_debug(DEBUG_TASK_INFO, "pending task %d, workload %d, clk_mode=%d\n",
		  task_cnt, workload, task->clk_mode);

	return 0;
}

static int rkvdec2_set_freq(struct mpp_dev *mpp,
			    struct mpp_task *mpp_task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec2_task *task =  to_rkvdec2_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->core_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->cabac_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, task->clk_mode);

	return 0;
}

int rkvdec2_reset(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	mpp_debug_enter();
	if (dec->rst_a && dec->rst_h) {
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(dec->rst_niu_a);
		mpp_safe_reset(dec->rst_niu_h);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		mpp_safe_reset(dec->rst_core);
		mpp_safe_reset(dec->rst_cabac);
		mpp_safe_reset(dec->rst_hevc_cabac);
		udelay(5);
		mpp_safe_unreset(dec->rst_niu_h);
		mpp_safe_unreset(dec->rst_niu_a);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		mpp_safe_unreset(dec->rst_core);
		mpp_safe_unreset(dec->rst_cabac);
		mpp_safe_unreset(dec->rst_hevc_cabac);
		mpp_pmu_idle_request(mpp, false);
	}
	mpp_debug_leave();

	return 0;
}

static struct mpp_hw_ops rkvdec_v2_hw_ops = {
	.init = rkvdec2_init,
	.clk_on = rkvdec2_clk_on,
	.clk_off = rkvdec2_clk_off,
	.get_freq = rkvdec2_get_freq,
	.set_freq = rkvdec2_set_freq,
	.reset = rkvdec2_reset,
};

static struct mpp_hw_ops rkvdec_rk3568_hw_ops = {
	.init = rkvdec2_rk3568_init,
	.exit = rkvdec2_rk3568_exit,
	.clk_on = rkvdec2_clk_on,
	.clk_off = rkvdec2_clk_off,
	.get_freq = rkvdec2_get_freq,
	.set_freq = rkvdec2_set_freq,
	.reset = rkvdec2_reset,
};

static struct mpp_dev_ops rkvdec_v2_dev_ops = {
	.alloc_task = rkvdec2_alloc_task,
	.run = rkvdec2_run,
	.irq = rkvdec2_irq,
	.isr = rkvdec2_isr,
	.finish = rkvdec2_finish,
	.result = rkvdec2_result,
	.free_task = rkvdec2_free_task,
	.ioctl = rkvdec2_control,
	.init_session = rkvdec2_init_session,
	.free_session = rkvdec2_free_session,
};

static struct mpp_dev_ops rkvdec_rk3568_dev_ops = {
	.alloc_task = rkvdec2_rk3568_alloc_task,
	.run = rkvdec2_rk3568_run,
	.irq = rkvdec2_irq,
	.isr = rkvdec2_isr,
	.finish = rkvdec2_finish,
	.result = rkvdec2_result,
	.free_task = rkvdec2_free_task,
	.ioctl = rkvdec2_control,
	.init_session = rkvdec2_init_session,
	.free_session = rkvdec2_free_session,
	.dump_dev = rkvdec_link_dump,
};

static const struct mpp_dev_var rkvdec_v2_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v2_hw_info,
	.trans_info = rkvdec_v2_trans,
	.hw_ops = &rkvdec_v2_hw_ops,
	.dev_ops = &rkvdec_v2_dev_ops,
};

static const struct mpp_dev_var rkvdec_rk3568_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v2_hw_info,
	.trans_info = rkvdec_v2_trans,
	.hw_ops = &rkvdec_rk3568_hw_ops,
	.dev_ops = &rkvdec_rk3568_dev_ops,
};

static const struct of_device_id mpp_rkvdec2_dt_match[] = {
	{
		.compatible = "rockchip,rkv-decoder-v2",
		.data = &rkvdec_v2_data,
	},
#ifdef CONFIG_CPU_RK3568
	{
		.compatible = "rockchip,rkv-decoder-rk3568",
		.data = &rkvdec_rk3568_data,
	},
#endif
#ifdef CONFIG_CPU_RK3588
	{
		.compatible = "rockchip,rkv-decoder-v2-ccu",
	},
#endif
	{},
};

static int rkvdec2_ccu_remove(struct device *dev)
{
	device_init_wakeup(dev, false);
	pm_runtime_disable(dev);

	return 0;
}

static int rkvdec2_ccu_probe(struct platform_device *pdev)
{
	struct rkvdec2_ccu *ccu;
	struct resource *res;
	struct device *dev = &pdev->dev;

	ccu = devm_kzalloc(dev, sizeof(*ccu), GFP_KERNEL);
	if (!ccu)
		return -ENOMEM;

	ccu->dev = dev;
	atomic_set(&ccu->power_enabled, 0);
	platform_set_drvdata(pdev, ccu);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ccu");
	if (!res) {
		dev_err(dev, "no memory resource defined\n");
		return -ENODEV;
	}

	ccu->reg_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!ccu->reg_base) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		return -ENODEV;
	}

	device_init_wakeup(dev, true);
	pm_runtime_enable(dev);
	/* power domain autosuspend delay 2s */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

	ccu->aclk_info.clk = devm_clk_get(dev, "aclk_ccu");
	if (!ccu->aclk_info.clk)
		mpp_err("failed on clk_get ccu aclk\n");

	ccu->rst_a = devm_reset_control_get(dev, "video_ccu");
	if (ccu->rst_a)
		mpp_safe_unreset(ccu->rst_a);
	else
		mpp_err("failed on clk_get ccu reset\n");

	return 0;
}

static int rkvdec2_alloc_rcbbuf(struct platform_device *pdev, struct rkvdec2_dev *dec)
{
	int ret;
	u32 vals[2];
	dma_addr_t iova;
	u32 rcb_size, sram_size;
	struct device_node *sram_np;
	struct resource sram_res;
	resource_size_t sram_start, sram_end;
	struct iommu_domain *domain;
	struct device *dev = &pdev->dev;

	/* get rcb iova start and size */
	ret = device_property_read_u32_array(dev, "rockchip,rcb-iova", vals, 2);
	if (ret) {
		dev_err(dev, "could not find property rcb-iova\n");
		return ret;
	}
	iova = PAGE_ALIGN(vals[0]);
	rcb_size = PAGE_ALIGN(vals[1]);
	if (!rcb_size) {
		dev_err(dev, "rcb_size invalid.\n");
		return -EINVAL;
	}
	/* alloc reserve iova for rcb */
	ret = iommu_dma_reserve_iova(dev, iova, rcb_size);
	if (ret) {
		dev_err(dev, "alloc rcb iova error.\n");
		return ret;
	}
	/* get sram device node */
	sram_np = of_parse_phandle(dev->of_node, "rockchip,sram", 0);
	if (!sram_np) {
		dev_err(dev, "could not find phandle sram\n");
		return -ENODEV;
	}
	/* get sram start and size */
	ret = of_address_to_resource(sram_np, 0, &sram_res);
	of_node_put(sram_np);
	if (ret) {
		dev_err(dev, "find sram res error\n");
		return ret;
	}
	/* check sram start and size is PAGE_SIZE align */
	sram_start = round_up(sram_res.start, PAGE_SIZE);
	sram_end = round_down(sram_res.start + resource_size(&sram_res), PAGE_SIZE);
	if (sram_end <= sram_start) {
		dev_err(dev, "no available sram, phy_start %pa, phy_end %pa\n",
			&sram_start, &sram_end);
		return -ENOMEM;
	}
	sram_size = sram_end - sram_start;
	sram_size = rcb_size < sram_size ? rcb_size : sram_size;
	/* iova map to sram */
	domain = dec->mpp.iommu_info->domain;
	ret = iommu_map(domain, iova, sram_start, sram_size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "sram iommu_map error.\n");
		return ret;
	}
	/* alloc dma for the remaining buffer, sram + dma */
	if (sram_size < rcb_size) {
		struct page *page;
		size_t page_size = PAGE_ALIGN(rcb_size - sram_size);

		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(page_size));
		if (!page) {
			dev_err(dev, "unable to allocate pages\n");
			ret = -ENOMEM;
			goto err_sram_map;
		}
		/* iova map to dma */
		ret = iommu_map(domain, iova + sram_size, page_to_phys(page),
				page_size, IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			dev_err(dev, "page iommu_map error.\n");
			__free_pages(page, get_order(page_size));
			goto err_sram_map;
		}
		dec->rcb_page = page;
	}
	dec->sram_size = sram_size;
	dec->rcb_size = rcb_size;
	dec->rcb_iova = iova;
	dev_info(dev, "sram_start %pa\n", &sram_start);
	dev_info(dev, "rcb_iova %pad\n", &dec->rcb_iova);
	dev_info(dev, "sram_size %u\n", dec->sram_size);
	dev_info(dev, "rcb_size %u\n", dec->rcb_size);

	ret = of_property_read_u32(dev->of_node, "rockchip,rcb-min-width", &dec->rcb_min_width);
	if (!ret && dec->rcb_min_width)
		dev_info(dev, "min_width %u\n", dec->rcb_min_width);

	return 0;

err_sram_map:
	iommu_unmap(domain, iova, sram_size);

	return ret;
}

static int rkvdec2_core_probe(struct platform_device *pdev)
{
	int ret;
	struct rkvdec2_dev *dec;
	struct mpp_dev *mpp;
	struct device *dev = &pdev->dev;

	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	mpp = &dec->mpp;
	platform_set_drvdata(pdev, mpp);
	mpp->is_irq_startup = false;
	if (dev->of_node) {
		struct device_node *np = pdev->dev.of_node;
		const struct of_device_id *match;

		match = of_match_node(mpp_rkvdec2_dt_match, dev->of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
		mpp->core_id = of_alias_get_id(np, "rkvdec");
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return ret;
	}
	/* attach core to ccu */
	ret = rkvdec2_attach_ccu(dev, dec);
	if (ret) {
		dev_err(dev, "attach ccu failed\n");
		return ret;
	}
	/* power domain autosuspend delay 2s */
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

	/* alloc rcb buffer */
	rkvdec2_alloc_rcbbuf(pdev, dec);

	/* set device for link */
	rkvdec2_ccu_link_init(pdev, dec);

	mpp->dev_ops->alloc_task = rkvdec2_ccu_alloc_task;
	mpp->dev_ops->task_worker = rkvdec2_soft_ccu_worker;
	kthread_init_work(&mpp->work, rkvdec2_soft_ccu_worker);

	mpp->iommu_info->hdl = rkvdec2_ccu_iommu_fault_handle;
	/* get irq request */
	ret = devm_request_threaded_irq(dev, mpp->irq, rkvdec2_soft_ccu_irq, NULL,
					IRQF_SHARED, dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}
	/*make sure mpp->irq is startup then can be en/disable*/
	mpp->is_irq_startup = true;

	mpp->session_max_buffers = RKVDEC_SESSION_MAX_BUFFERS;
	rkvdec2_procfs_init(mpp);

	/* if is main-core, register to mpp service */
	if (mpp->core_id == 0)
		mpp_dev_register_srv(mpp, mpp->srv);

	return ret;
}

static int rkvdec2_probe_default(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec2_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	mpp = &dec->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvdec2_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return ret;
	}

	rkvdec2_alloc_rcbbuf(pdev, dec);
	rkvdec2_link_init(pdev, dec);

	if (dec->link_dec) {
		ret = devm_request_threaded_irq(dev, mpp->irq,
						rkvdec2_link_irq_proc, NULL,
						IRQF_SHARED, dev_name(dev), mpp);
		mpp->dev_ops->process_task = rkvdec2_link_process_task;
		mpp->dev_ops->wait_result = rkvdec2_link_wait_result;
		mpp->dev_ops->task_worker = rkvdec2_link_worker;
		mpp->dev_ops->deinit = rkvdec2_link_session_deinit;
		kthread_init_work(&mpp->work, rkvdec2_link_worker);
	} else {
		ret = devm_request_threaded_irq(dev, mpp->irq,
						mpp_dev_irq, mpp_dev_isr_sched,
						IRQF_SHARED, dev_name(dev), mpp);
	}
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	mpp->session_max_buffers = RKVDEC_SESSION_MAX_BUFFERS;
	rkvdec2_procfs_init(mpp);
	rkvdec2_link_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);

	return ret;
}

static int rkvdec2_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_info(dev, "%s, probing start\n", np->name);

	if (strstr(np->name, "ccu"))
		ret = rkvdec2_ccu_probe(pdev);
	else if (strstr(np->name, "core"))
		ret = rkvdec2_core_probe(pdev);
	else
		ret = rkvdec2_probe_default(pdev);

	dev_info(dev, "probing finish\n");

	return ret;
}

static int rkvdec2_free_rcbbuf(struct platform_device *pdev, struct rkvdec2_dev *dec)
{
	struct iommu_domain *domain;

	if (dec->rcb_page) {
		size_t page_size = PAGE_ALIGN(dec->rcb_size - dec->sram_size);

		__free_pages(dec->rcb_page, get_order(page_size));
	}
	if (dec->rcb_iova) {
		domain = dec->mpp.iommu_info->domain;
		iommu_unmap(domain, dec->rcb_iova, dec->rcb_size);
	}

	return 0;
}

static int rkvdec2_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (strstr(dev_name(dev), "ccu")) {
		dev_info(dev, "remove ccu device\n");
		rkvdec2_ccu_remove(dev);
	} else {
		struct mpp_dev *mpp = dev_get_drvdata(dev);
		struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

		dev_info(dev, "remove device\n");

		rkvdec2_free_rcbbuf(pdev, dec);
		mpp_dev_remove(mpp);
		rkvdec2_procfs_remove(mpp);
		rkvdec2_link_remove(mpp, dec->link_dec);
	}

	return 0;
}

static void rkvdec2_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!strstr(dev_name(dev), "ccu"))
		mpp_dev_shutdown(pdev);
}

static int __maybe_unused rkvdec2_runtime_suspend(struct device *dev)
{
	if (strstr(dev_name(dev), "ccu")) {
		struct rkvdec2_ccu *ccu = dev_get_drvdata(dev);

		mpp_clk_safe_disable(ccu->aclk_info.clk);
	} else {
		u32 val;
		struct mpp_dev *mpp = dev_get_drvdata(dev);

		/* soft reset */
		mpp_write(mpp, RKVDEC_REG_IMPORTANT_BASE, RKVDEC_SOFTREST_EN);
		udelay(5);
		val = mpp_read(mpp, RKVDEC_REG_INT_EN);
		if (!(val & RKVDEC_SOFT_RESET_READY))
			mpp_err("soft reset fail, int %08x\n", val);
		mpp_write(mpp, RKVDEC_REG_INT_EN, 0);

		if (mpp->is_irq_startup) {
			/* disable core irq */
			disable_irq(mpp->irq);
			if (mpp->iommu_info && mpp->iommu_info->got_irq)
				/* disable mmu irq */
				disable_irq(mpp->iommu_info->irq);
		}

		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);
	}

	return 0;
}

static int __maybe_unused rkvdec2_runtime_resume(struct device *dev)
{
	if (strstr(dev_name(dev), "ccu")) {
		struct rkvdec2_ccu *ccu = dev_get_drvdata(dev);

		mpp_clk_safe_enable(ccu->aclk_info.clk);
	} else {
		struct mpp_dev *mpp = dev_get_drvdata(dev);

		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);
		if (mpp->is_irq_startup) {
			/* enable core irq */
			enable_irq(mpp->irq);
			/* enable mmu irq */
			if (mpp->iommu_info && mpp->iommu_info->got_irq)
				enable_irq(mpp->iommu_info->irq);
		}

	}

	return 0;
}

static const struct dev_pm_ops rkvdec2_pm_ops = {
	SET_RUNTIME_PM_OPS(rkvdec2_runtime_suspend, rkvdec2_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

struct platform_driver rockchip_rkvdec2_driver = {
	.probe = rkvdec2_probe,
	.remove = rkvdec2_remove,
	.shutdown = rkvdec2_shutdown,
	.driver = {
		.name = RKVDEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvdec2_dt_match),
		.pm = &rkvdec2_pm_ops,
	},
};
EXPORT_SYMBOL(rockchip_rkvdec2_driver);
