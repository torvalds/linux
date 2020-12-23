// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#include <asm/cacheflush.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/kernel.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/regulator/consumer.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_sip.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define RKVDEC_DRIVER_NAME		"mpp_rkvdec2"

#define	RKVDEC_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define RKVDEC_REG_NUM			278
#define RKVDEC_REG_HW_ID_INDEX		0
#define RKVDEC_REG_START_INDEX		0
#define RKVDEC_REG_END_INDEX		277

#define REVDEC_GET_PROD_NUM(x)		(((x) >> 16) & 0xffff)
#define RKVDEC_REG_FORMAT_INDEX		9
#define RKVDEC_GET_FORMAT(x)		((x) & 0x3FF)

#define RKVDEC_REG_START_EN_BASE       0x28

#define RKVDEC_REG_START_EN_INDEX      10

#define RKVDEC_START_EN			BIT(0)

#define RKVDEC_REG_YSTRIDE_INDEX	20

#define RKVDEC_REG_RLC_BASE		0x200
#define RKVDEC_REG_RLC_BASE_INDEX	(128)

#define RKVDEC_REG_INT_EN		0x380
#define RKVDEC_REG_INT_EN_INDEX		(224)
#define RKVDEC_SOFT_RESET_READY		BIT(9)
#define RKVDEC_CABAC_END_STA		BIT(8)
#define RKVDEC_COLMV_REF_ERR_STA	BIT(7)
#define RKVDEC_BUF_EMPTY_STA		BIT(6)
#define RKVDEC_TIMEOUT_STA		BIT(5)
#define RKVDEC_ERROR_STA		BIT(4)
#define RKVDEC_BUS_STA			BIT(3)
#define RKVDEC_READY_STA		BIT(2)
#define RKVDEC_IRQ_RAW			BIT(1)
#define RKVDEC_IRQ			BIT(0)

/* perf sel reference register */
#define RKVDEC_PERF_SEL_OFFSET		0x20000
#define RKVDEC_PERF_SEL_NUM		64
#define RKVDEC_PERF_SEL_BASE		0x424
#define RKVDEC_SEL_VAL0_BASE		0x428
#define RKVDEC_SEL_VAL1_BASE		0x42c
#define RKVDEC_SEL_VAL2_BASE		0x430
#define RKVDEC_SET_PERF_SEL(a, b, c)	((a) | ((b) << 8) | ((c) << 16))

/* cache reference register */
#define RKVDEC_REG_CACHE0_SIZE_BASE	0x51c
#define RKVDEC_REG_CACHE1_SIZE_BASE	0x55c
#define RKVDEC_REG_CACHE2_SIZE_BASE	0x59c
#define RKVDEC_REG_CLR_CACHE0_BASE	0x510
#define RKVDEC_REG_CLR_CACHE1_BASE	0x550
#define RKVDEC_REG_CLR_CACHE2_BASE	0x590

#define RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS	BIT(0)
#define RKVDEC_CACHE_PERMIT_READ_ALLOCATE	BIT(1)
#define RKVDEC_CACHE_LINE_SIZE_64_BYTES		BIT(4)

#define to_rkvdec_task(task)		\
		container_of(task, struct rkvdec_task, mpp_task)
#define to_rkvdec_dev(dev)		\
		container_of(dev, struct rkvdec_dev, mpp)

enum RKVDEC_STATE {
	RKVDEC_STATE_NORMAL,
	RKVDEC_STATE_LT_START,
	RKVDEC_STATE_LT_RUN,
};

enum RKVDEC_FMT {
	RKVDEC_FMT_H265D	= 0,
	RKVDEC_FMT_H264D	= 1,
	RKVDEC_FMT_VP9D		= 2,
	RKVDEC_FMT_AVS2		= 3,
};

struct rkvdec_task {
	struct mpp_task mpp_task;

	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[RKVDEC_REG_NUM];
	struct reg_offset_info off_inf;

	/* perf sel data back */
	u32 reg_sel[RKVDEC_PERF_SEL_NUM];

	u32 strm_addr;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct rkvdec_dev {
	struct mpp_dev mpp;
	/* sip smc reset lock */
	struct mutex sip_reset_lock;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info core_clk_info;
	struct mpp_clk_info cabac_clk_info;
	struct mpp_clk_info hevc_cabac_clk_info;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_niu_a;
	struct reset_control *rst_niu_h;
	struct reset_control *rst_core;
	struct reset_control *rst_cabac;
	struct reset_control *rst_hevc_cabac;

	enum RKVDEC_STATE state;
};

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
};

static int rkvdec_extract_task_msg(struct rkvdec_task *task,
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
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt %d, r_req_cnt %d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

static void *rkvdec_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct rkvdec_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	mpp_task->reg = task->reg;
	/* extract reqs for current task */
	ret = rkvdec_extract_task_msg(task, msgs);
	if (ret)
		goto fail;

	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		u32 fmt = RKVDEC_GET_FORMAT(task->reg[RKVDEC_REG_FORMAT_INDEX]);

		ret = mpp_translate_reg_address(session, &task->mpp_task,
						fmt, task->reg, &task->off_inf);
		if (ret)
			goto fail;

		mpp_translate_reg_offset_info(&task->mpp_task, &task->off_inf, task->reg);
	}
	task->strm_addr = task->reg[RKVDEC_REG_RLC_BASE_INDEX];
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	kfree(task);
	return NULL;
}

static int rkvdec_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	int i;
	u32 reg_en;
	struct rkvdec_dev *dec = NULL;
	struct rkvdec_task *task = NULL;

	mpp_debug_enter();

	dec = to_rkvdec_dev(mpp);
	task = to_rkvdec_task(mpp_task);
	reg_en = mpp_task->hw_info->reg_en;
	switch (dec->state) {
	case RKVDEC_STATE_NORMAL: {
		u32 reg;

		/* set cache size */
		reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS
			| RKVDEC_CACHE_PERMIT_READ_ALLOCATE;
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
		/* init current task */
		mpp->cur_task = mpp_task;
		/* Flush the register before the start the device */
		wmb();
		mpp_write(mpp, RKVDEC_REG_START_EN_BASE, task->reg[reg_en] | RKVDEC_START_EN);

	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, RKVDEC_REG_INT_EN);
	if (!(mpp->irq_status & RKVDEC_IRQ_RAW))
		return IRQ_NONE;

	mpp_write(mpp, RKVDEC_REG_INT_EN, 0);

	return IRQ_WAKE_THREAD;
}

static int rkvdec_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct rkvdec_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvdec_task(mpp_task);
	task->irq_status = mpp->irq_status;
	switch (dec->state) {
	case RKVDEC_STATE_NORMAL:
		mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);
		err_mask = RKVDEC_COLMV_REF_ERR_STA | RKVDEC_BUF_EMPTY_STA |
			   RKVDEC_TIMEOUT_STA | RKVDEC_ERROR_STA;
		if (err_mask & task->irq_status) {
			atomic_inc(&mpp->reset_request);
			mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n",
				  task->irq_status);
			mpp_task_dump_hw_reg(mpp, mpp_task);
		}

		mpp_task_finish(mpp_task->session, mpp_task);

		mpp_debug_leave();
		return IRQ_HANDLED;
	default:
		goto fail;
	}
fail:
	return IRQ_HANDLED;
}

static int rkvenc_read_perf_sel(struct mpp_dev *mpp, u32 *regs, u32 s, u32 e)
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

static int rkvdec_finish(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i;
	u32 dec_get;
	s32 dec_length;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	mpp_debug_enter();

	switch (dec->state) {
	case RKVDEC_STATE_NORMAL: {
		u32 s, e;
		struct mpp_request *req;

		/* read register after running */
		for (i = 0; i < task->r_req_cnt; i++) {
			req = &task->r_reqs[i];
			/* read perf register */
			if (req->offset >= RKVDEC_PERF_SEL_OFFSET) {
				int off = req->offset - RKVDEC_PERF_SEL_OFFSET;

				s = off / sizeof(u32);
				e = s + req->size / sizeof(u32);
				rkvenc_read_perf_sel(mpp, task->reg_sel, s, e);
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
	} break;
	default:
		break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

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

static int rkvdec_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct rkvdec_task *task = to_rkvdec_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

#ifdef CONFIG_PROC_FS
static int rkvdec_procfs_remove(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int rkvdec_show_pref_sel_offset(struct seq_file *file, void *v)
{
	seq_printf(file, "0x%08x\n", RKVDEC_PERF_SEL_OFFSET);

	return 0;
}

static int rkvdec_procfs_init(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	dec->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
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
			   dec->procfs, rkvdec_show_pref_sel_offset);

	return 0;
}
#else
static inline int rkvdec_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvdec_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int rkvdec_init(struct mpp_dev *mpp)
{
	int ret;
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

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

static int rkvdec_clk_on(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);
	mpp_clk_safe_enable(dec->core_clk_info.clk);
	mpp_clk_safe_enable(dec->cabac_clk_info.clk);
	mpp_clk_safe_enable(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec_clk_off(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	clk_disable_unprepare(dec->aclk_info.clk);
	clk_disable_unprepare(dec->hclk_info.clk);
	clk_disable_unprepare(dec->core_clk_info.clk);
	clk_disable_unprepare(dec->cabac_clk_info.clk);
	clk_disable_unprepare(dec->hevc_cabac_clk_info.clk);

	return 0;
}

static int rkvdec_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);
	struct rkvdec_task *task =  to_rkvdec_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->core_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->cabac_clk_info, task->clk_mode);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, task->clk_mode);

	return 0;
}

static int rkvdec_reduce_freq(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->core_clk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&dec->hevc_cabac_clk_info, CLK_MODE_REDUCE);

	return 0;
}

static int rkvdec_reset(struct mpp_dev *mpp)
{
	struct rkvdec_dev *dec = to_rkvdec_dev(mpp);

	mpp_debug_enter();
	if (dec->rst_a && dec->rst_h) {
		rockchip_pmu_idle_request(mpp->dev, true);
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
		rockchip_pmu_idle_request(mpp->dev, false);
	}
	mpp_debug_leave();

	return 0;
}

static struct mpp_hw_ops rkvdec_v2_hw_ops = {
	.init = rkvdec_init,
	.clk_on = rkvdec_clk_on,
	.clk_off = rkvdec_clk_off,
	.set_freq = rkvdec_set_freq,
	.reduce_freq = rkvdec_reduce_freq,
	.reset = rkvdec_reset,
};

static struct mpp_dev_ops rkvdec_v2_dev_ops = {
	.alloc_task = rkvdec_alloc_task,
	.run = rkvdec_run,
	.irq = rkvdec_irq,
	.isr = rkvdec_isr,
	.finish = rkvdec_finish,
	.result = rkvdec_result,
	.free_task = rkvdec_free_task,
};

static const struct mpp_dev_var rkvdec_v2_data = {
	.device_type = MPP_DEVICE_RKVDEC,
	.hw_info = &rkvdec_v2_hw_info,
	.trans_info = rkvdec_v2_trans,
	.hw_ops = &rkvdec_v2_hw_ops,
	.dev_ops = &rkvdec_v2_dev_ops,
};

static const struct of_device_id mpp_rkvdec_dt_match[] = {
	{
		.compatible = "rockchip,rkv-decoder-v2",
		.data = &rkvdec_v2_data,
	},
	{},
};

static int rkvdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probing start\n");
	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	mpp = &dec->mpp;
	platform_set_drvdata(pdev, dec);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvdec_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return ret;
	}

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq, mpp_dev_isr_sched,
					IRQF_SHARED, dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	dec->state = RKVDEC_STATE_NORMAL;
	mpp->session_max_buffers = RKVDEC_SESSION_MAX_BUFFERS;
	rkvdec_procfs_init(mpp);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int rkvdec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&dec->mpp);
	rkvdec_procfs_remove(&dec->mpp);

	return 0;
}

static void rkvdec_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct rkvdec_dev *dec = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &dec->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_rkvdec2_driver = {
	.probe = rkvdec_probe,
	.remove = rkvdec_remove,
	.shutdown = rkvdec_shutdown,
	.driver = {
		.name = RKVDEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvdec_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_rkvdec2_driver);
