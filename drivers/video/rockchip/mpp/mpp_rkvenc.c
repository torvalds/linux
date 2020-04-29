// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */

#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_iommu.h"
#include "mpp_common.h"

#define RKVENC_DRIVER_NAME			"mpp_rkvenc"

#define	RKVENC_SESSION_MAX_BUFFERS		40
/* The maximum registers number of all the version */
#define RKVENC_REG_L1_NUM			780
#define RKVENC_REG_L2_NUM			200
#define RKVENC_REG_START_INDEX			0
#define RKVENC_REG_END_INDEX			131
/* rkvenc register info */
#define RKVENC_REG_NUM				112
#define RKVENC_REG_HW_ID_INDEX			0
#define RKVENC_REG_CLR_CACHE_BASE		0x884

#define RKVENC_ENC_START_INDEX			1
#define RKVENC_ENC_START_BASE			0x004
#define RKVENC_LKT_NUM(x)			((x) & 0xff)
#define RKVENC_CMD(x)				(((x) & 0x3) << 8)
#define RKVENC_CLK_GATE_EN			BIT(16)
#define RKVENC_CLR_BASE				0x008
#define RKVENC_SAFE_CLR_BIT			BIT(0)
#define RKVENC_FORCE_CLR_BIT			BIT(1)
#define RKVENC_LKT_ADDR_BASE			0x00c

#define RKVENC_INT_EN_INDEX			4
#define RKVENC_INT_EN_BASE			0x010
#define RKVENC_INT_MSK_BASE			0x014
#define RKVENC_INT_CLR_BASE			0x018
#define RKVENC_INT_STATUS_INDEX			7
#define RKVENC_INT_STATUS_BASE			0x01c
/* bit for int mask clr status */
#define RKVENC_BIT_ONE_FRAME			BIT(0)
#define RKVENC_BIT_LINK_TABLE			BIT(1)
#define RKVENC_BIT_SAFE_CLEAR			BIT(2)
#define RKVENC_BIT_ONE_SLICE			BIT(3)
#define RKVENC_BIT_STREAM_OVERFLOW		BIT(4)
#define RKVENC_BIT_AXI_WRITE_FIFO_FULL		BIT(5)
#define RKVENC_BIT_AXI_WRITE_CHANNEL		BIT(6)
#define RKVENC_BIT_AXI_READ_CHANNEL		BIT(7)
#define RKVENC_BIT_TIMEOUT			BIT(8)
#define RKVENC_INT_ERROR_BITS	((RKVENC_BIT_STREAM_OVERFLOW) |\
				(RKVENC_BIT_AXI_WRITE_FIFO_FULL) |\
				(RKVENC_BIT_AXI_WRITE_CHANNEL) |\
				(RKVENC_BIT_AXI_READ_CHANNEL) |\
				(RKVENC_BIT_TIMEOUT))

#define RKVENC_ENC_PIC_INDEX			10
#define RKVENC_ENC_PIC_BASE			0x034
#define RKVENC_GET_FORMAT(x)			((x) & 0x1)
#define RKVENC_ENC_PIC_NODE_INT_EN		BIT(31)
#define RKVENC_ENC_WDG_BASE			0x038
#define RKVENC_PPLN_ENC_LMT(x)			((x) & 0xf)
#define RKVENC_OSD_CFG_BASE			0x1c0
#define RKVENC_OSD_PLT_TYPE			BIT(17)
#define RKVENC_OSD_CLK_SEL_BIT			BIT(16)
#define RKVENC_STATUS_BASE(i)			(0x210 + (4 * (i)))
#define RKVENC_BSL_STATUS_BASE			0x210
#define RKVENC_BITSTREAM_LENGTH(x)		((x) & 0x7FFFFFF)
#define RKVENC_ENC_STATUS_BASE			0x220
#define RKVENC_ENC_STATUS_ENC(x)		(((x) >> 0) & 0x3)
#define RKVENC_LKT_STATUS_BASE			0x224
#define RKVENC_LKT_STATUS_FNUM_ENC(x)		(((x) >> 0) & 0xff)
#define RKVENC_LKT_STATUS_FNUM_CFG(x)		(((x) >> 8) & 0xff)
#define RKVENC_LKT_STATUS_FNUM_INT(x)		(((x) >> 16) & 0xff)
#define RKVENC_OSD_PLT_BASE(i)			(0x400 + (4 * (i)))

#define RKVENC_L2_OFFSET			(0x10000)
#define RKVENC_L2_ADDR_BASE			(0x3f0)
#define RKVENC_L2_WRITE_BASE			(0x3f4)
#define RKVENC_L2_READ_BASE			(0x3f8)
#define RKVENC_L2_BURST_TYPE			BIT(0)

#define to_rkvenc_task(ctx)		\
		container_of(ctx, struct rkvenc_task, mpp_task)
#define to_rkvenc_dev(dev)		\
		container_of(dev, struct rkvenc_dev, mpp)

enum rkvenc_format_type {
	RKVENC_FMT_H264E = 0,
	RKVENC_FMT_H265E = 1,
	RKVENC_FMT_BUTT,
};

enum RKVENC_MODE {
	RKVENC_MODE_NONE,
	RKVENC_MODE_ONEFRAME,
	RKVENC_MODE_LINKTABLE_FIX,
	RKVENC_MODE_LINKTABLE_UPDATE,
	RKVENC_MODE_BUTT
};

struct rkvenc_task {
	struct mpp_task mpp_task;

	int link_flags;
	int fmt;
	enum RKVENC_MODE link_mode;

	/* level 1 register setting */
	u32 reg_offset;
	u32 reg_num;
	u32 reg[RKVENC_REG_L1_NUM];
	/* level 2 register setting */
	u32 reg_l2_offset;
	u32 reg_l2_num;
	u32 reg_l2[RKVENC_REG_L2_NUM];
	/* register offset info */
	struct reg_offset_info off_inf;

	unsigned long aclk_freq;
	unsigned long clk_core_freq;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct rkvenc_dev {
	struct mpp_dev mpp;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *clk_core;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
	u32 aclk_debug;
	u32 clk_core_debug;
	u32 session_max_buffers_debug;

	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_core;
};

struct link_table_elem {
	dma_addr_t lkt_dma_addr;
	void *lkt_cpu_addr;
	u32 lkt_index;
	struct list_head list;
};

static struct mpp_hw_info rkvenc_hw_info = {
	.reg_num = RKVENC_REG_NUM,
	.reg_id = RKVENC_REG_HW_ID_INDEX,
	.reg_en = RKVENC_ENC_START_INDEX,
	.reg_start = RKVENC_REG_START_INDEX,
	.reg_end = RKVENC_REG_END_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_h264e[] = {
	70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	80, 81, 82, 83, 84, 85, 86, 124, 125,
	126, 127, 128, 129, 130, 131
};

static const u16 trans_tbl_h265e[] = {
	70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	80, 81, 82, 83, 84, 85, 86, 124, 125,
	126, 127, 128, 129, 130, 131
};

static struct mpp_trans_info trans_rk_rkvenc[] = {
	[RKVENC_FMT_H264E] = {
		.count = ARRAY_SIZE(trans_tbl_h264e),
		.table = trans_tbl_h264e,
	},
	[RKVENC_FMT_H265E] = {
		.count = ARRAY_SIZE(trans_tbl_h265e),
		.table = trans_tbl_h265e,
	},
};

static int rkvenc_extract_task_msg(struct rkvenc_task *task,
				   struct mpp_task_msgs *msgs)
{
	u32 i;
	int ret;
	struct mpp_request *req;
	struct reg_offset_info *off_inf = &task->off_inf;

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			int req_base;
			int max_size;
			u8 *dst = NULL;

			if (req->offset >= RKVENC_L2_OFFSET) {
				req_base = RKVENC_L2_OFFSET;
				max_size = sizeof(task->reg_l2);
				dst = (u8 *)task->reg_l2;
			} else {
				req_base = 0;
				max_size = sizeof(task->reg);
				dst = (u8 *)task->reg;
			}

			ret = mpp_check_req(req, req_base, max_size,
					    0, max_size);
			if (ret)
				return ret;

			dst += req->offset - req_base;
			if (copy_from_user(dst, req->data, req->size)) {
				mpp_err("copy_from_user reg failed\n");
				return -EIO;
			}
			memcpy(&task->w_reqs[task->w_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_READ: {
			int req_base;
			int max_size;

			if (req->offset >= RKVENC_L2_OFFSET) {
				req_base = RKVENC_L2_OFFSET;
				max_size = sizeof(task->reg_l2);
			} else {
				req_base = 0;
				max_size = sizeof(task->reg);
			}

			ret = mpp_check_req(req, req_base, max_size,
					    0, max_size);
			if (ret)
				return ret;

			memcpy(&task->r_reqs[task->r_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_ADDR_OFFSET: {
			ret = mpp_check_req(req, req->offset,
					    sizeof(off_inf->elem),
					    0, sizeof(off_inf->elem));
			if (ret)
				return ret;
			if (copy_from_user(&off_inf->elem[off_inf->cnt],
					   req->data,
					   req->size)) {
				mpp_err("copy_from_user failed\n");
				return -EINVAL;
			}
			off_inf->cnt += req->size / sizeof(off_inf->elem[0]);
		} break;
		default:
			break;
		}
	}
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt=%d, r_req_cnt=%d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

static void *rkvenc_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct rkvenc_task *task = NULL;
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
	ret = rkvenc_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = mpp_translate_reg_address(session,
						mpp_task, task->fmt,
						task->reg, &task->off_inf);
		if (ret)
			goto fail;
		mpp_translate_reg_offset_info(mpp_task,
					      &task->off_inf, task->reg);
	}
	task->link_mode = RKVENC_MODE_ONEFRAME;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	kfree(task);
	return NULL;
}

static int rkvenc_write_req_l2(struct mpp_dev *mpp,
			       u32 *regs,
			       u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		mpp_write_relaxed(mpp, RKVENC_L2_ADDR_BASE, i * sizeof(u32));
		mpp_write_relaxed(mpp, RKVENC_L2_WRITE_BASE, regs[i]);
	}

	return 0;
}

static int rkvenc_read_req_l2(struct mpp_dev *mpp,
			      u32 *regs,
			      u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		mpp_write_relaxed(mpp, RKVENC_L2_ADDR_BASE, i * sizeof(u32));
		regs[i] = mpp_read_relaxed(mpp, RKVENC_L2_READ_BASE);
	}

	return 0;
}

static int rkvenc_run(struct mpp_dev *mpp,
		      struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	/* clear cache */
	mpp_write_relaxed(mpp, RKVENC_REG_CLR_CACHE_BASE, 1);
	switch (task->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		int i;
		struct mpp_request *req;
		u32 reg_en = mpp_task->hw_info->reg_en;

		/*
		 * Tips: ensure osd plt clock is 0 before setting register,
		 * otherwise, osd setting will not work
		 */
		mpp_write_relaxed(mpp, RKVENC_OSD_CFG_BASE, 0);
		/* ensure clear finish */
		wmb();
		for (i = 0; i < task->w_req_cnt; i++) {
			int s, e;

			req = &task->w_reqs[i];
			/* set register L2 */
			if (req->offset >= RKVENC_L2_OFFSET) {
				int off = req->offset - RKVENC_L2_OFFSET;

				s = off / sizeof(u32);
				e = s + req->size / sizeof(u32);
				rkvenc_write_req_l2(mpp, task->reg_l2, s, e);
			} else {
				/* set register L1 */
				s = req->offset / sizeof(u32);
				e = s + req->size / sizeof(u32);
				mpp_write_req(mpp, task->reg, s, e, reg_en);
			}
		}
		/* init current task */
		mpp->cur_task = mpp_task;
		/* Flush the register before the start the device */
		wmb();
		mpp_write(mpp, RKVENC_ENC_START_BASE, task->reg[reg_en]);
	} break;
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
	default: {
		mpp_err("link_mode %d failed.\n", task->link_mode);
	} break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_irq(struct mpp_dev *mpp)
{
	mpp_debug_enter();

	mpp->irq_status = mpp_read(mpp, RKVENC_INT_STATUS_BASE);
	if (!mpp->irq_status)
		return IRQ_NONE;

	mpp_write(mpp, RKVENC_INT_CLR_BASE, 0xffffffff);
	udelay(5);
	mpp_write(mpp, RKVENC_INT_STATUS_BASE, 0);

	mpp_debug_leave();

	return IRQ_WAKE_THREAD;
}

static int rkvenc_isr(struct mpp_dev *mpp)
{
	struct rkvenc_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvenc_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);

	if (task->irq_status & RKVENC_INT_ERROR_BITS) {
		/*
		 * according to war running, if the dummy encoding
		 * running with timeout, we enable a safe clear process,
		 * we reset the ip, and complete the war procedure.
		 */
		atomic_inc(&mpp->reset_request);
		/* time out error */
		mpp_write(mpp, RKVENC_INT_ERROR_BITS, RKVENC_INT_MSK_BASE);
	}

	mpp_task_finish(mpp_task->session, mpp_task);
	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int rkvenc_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	switch (task->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		u32 i;
		struct mpp_request *req;

		for (i = 0; i < task->r_req_cnt; i++) {
			int s, e;

			req = &task->r_reqs[i];
			if (req->offset >= RKVENC_L2_OFFSET) {
				int off = req->offset - RKVENC_L2_OFFSET;

				s = off / sizeof(u32);
				e = s + req->size / sizeof(u32);
				rkvenc_read_req_l2(mpp, task->reg_l2, s, e);
			} else {
				s = req->offset / sizeof(u32);
				e = s + req->size / sizeof(u32);
				mpp_read_req(mpp, task->reg, s, e);
			}
		}
		task->reg[RKVENC_INT_STATUS_INDEX] = task->irq_status;
	} break;
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
	default: {
		mpp_err("link_mode %d failed.\n", task->link_mode);
	} break;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	switch (task->link_mode) {
	case RKVENC_MODE_ONEFRAME: {
		u32 i;
		struct mpp_request *req;

		for (i = 0; i < task->r_req_cnt; i++) {
			req = &task->r_reqs[i];
			/* set register L2 */
			if (req->offset >= RKVENC_L2_OFFSET) {
				int off = req->offset - RKVENC_L2_OFFSET;

				if (copy_to_user(req->data,
						 (u8 *)task->reg_l2 + off,
						 req->size)) {
					mpp_err("copy_to_user reg_l2 fail\n");
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
	} break;
	case RKVENC_MODE_LINKTABLE_FIX:
	case RKVENC_MODE_LINKTABLE_UPDATE:
	default: {
		mpp_err("link_mode %d failed.\n", task->link_mode);
	} break;
	}

	return 0;
}

static int rkvenc_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

static int rkvenc_debugfs_remove(struct mpp_dev *mpp)
{
#ifdef CONFIG_DEBUG_FS
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	debugfs_remove_recursive(enc->debugfs);
#endif
	return 0;
}

static int rkvenc_debugfs_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	enc->aclk_debug = 0;
	enc->clk_core_debug = 0;
	enc->session_max_buffers_debug = 0;
#ifdef CONFIG_DEBUG_FS
	enc->debugfs = debugfs_create_dir(mpp->dev->of_node->name,
					  mpp->srv->debugfs);
	if (IS_ERR_OR_NULL(enc->debugfs)) {
		mpp_err("failed on open debugfs\n");
		enc->debugfs = NULL;
		return -EIO;
	}
	debugfs_create_u32("aclk", 0644,
			   enc->debugfs, &enc->aclk_debug);
	debugfs_create_u32("clk_core", 0644,
			   enc->debugfs, &enc->clk_core_debug);
	debugfs_create_u32("session_buffers", 0644,
			   enc->debugfs, &enc->session_max_buffers_debug);
#endif
	if (enc->session_max_buffers_debug)
		mpp->session_max_buffers = enc->session_max_buffers_debug;

	return 0;
}

static int rkvenc_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVENC];

	enc->aclk = devm_clk_get(mpp->dev, "aclk_vcodec");
	if (IS_ERR(enc->aclk)) {
		mpp_err("failed on clk_get aclk_vcodec\n");
		enc->aclk = NULL;
	}
	enc->hclk = devm_clk_get(mpp->dev, "hclk_vcodec");
	if (IS_ERR(enc->hclk)) {
		mpp_err("failed on clk_get hclk_vcodec\n");
		enc->hclk = NULL;
	}
	enc->clk_core = devm_clk_get(mpp->dev, "clk_core");
	if (IS_ERR_OR_NULL(enc->clk_core)) {
		dev_err(mpp->dev, "failed on clk_get core\n");
		enc->clk_core = NULL;
	}

	enc->rst_a = devm_reset_control_get_shared(mpp->dev, "video_a");
	if (IS_ERR_OR_NULL(enc->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		enc->rst_a = NULL;
	}
	enc->rst_h = devm_reset_control_get_shared(mpp->dev, "video_h");
	if (IS_ERR_OR_NULL(enc->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		enc->rst_h = NULL;
	}
	enc->rst_core = devm_reset_control_get_shared(mpp->dev, "video_core");
	if (IS_ERR_OR_NULL(enc->rst_core)) {
		mpp_err("No core reset resource define\n");
		enc->rst_core = NULL;
	}
	mpp_safe_unreset(enc->rst_a);
	mpp_safe_unreset(enc->rst_h);
	mpp_safe_unreset(enc->rst_core);

	return 0;
}

static int rkvenc_reset(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();

	mpp_write(mpp, RKVENC_INT_EN_BASE, RKVENC_SAFE_CLR_BIT);
	mpp_write(mpp, RKVENC_CLR_BASE, RKVENC_SAFE_CLR_BIT);
	if (enc->rst_a && enc->rst_h && enc->rst_core) {
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(enc->rst_a);
		mpp_safe_reset(enc->rst_h);
		mpp_safe_reset(enc->rst_core);
		udelay(5);
		mpp_safe_unreset(enc->rst_a);
		mpp_safe_unreset(enc->rst_h);
		mpp_safe_unreset(enc->rst_core);
		rockchip_pmu_idle_request(mpp->dev, false);
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_power_on(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->aclk)
		clk_prepare_enable(enc->aclk);
	if (enc->hclk)
		clk_prepare_enable(enc->hclk);
	if (enc->clk_core)
		clk_prepare_enable(enc->clk_core);

	return 0;
}

static int rkvenc_power_off(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->aclk)
		clk_disable_unprepare(enc->aclk);
	if (enc->hclk)
		clk_disable_unprepare(enc->hclk);
	if (enc->clk_core)
		clk_disable_unprepare(enc->clk_core);

	return 0;
}

static int rkvenc_get_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	task->aclk_freq = 300;
	task->clk_core_freq = 200;

	return 0;
}

static int rkvenc_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	task->aclk_freq = enc->aclk_debug ? enc->aclk_debug : task->aclk_freq;
	task->clk_core_freq = enc->clk_core_debug ?
	enc->clk_core_debug : task->clk_core_freq;

	clk_set_rate(enc->aclk, task->aclk_freq * MHZ);
	clk_set_rate(enc->clk_core, task->clk_core_freq * MHZ);

	return 0;
}

static int rkvenc_reduce_freq(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->aclk)
		clk_set_rate(enc->aclk, 50 * MHZ);
	if (enc->clk_core)
		clk_set_rate(enc->clk_core, 50 * MHZ);

	return 0;
}

static struct mpp_hw_ops rkvenc_hw_ops = {
	.init = rkvenc_init,
	.power_on = rkvenc_power_on,
	.power_off = rkvenc_power_off,
	.get_freq = rkvenc_get_freq,
	.set_freq = rkvenc_set_freq,
	.reduce_freq = rkvenc_reduce_freq,
	.reset = rkvenc_reset,
};

static struct mpp_dev_ops rkvenc_dev_ops = {
	.alloc_task = rkvenc_alloc_task,
	.run = rkvenc_run,
	.irq = rkvenc_irq,
	.isr = rkvenc_isr,
	.finish = rkvenc_finish,
	.result = rkvenc_result,
	.free_task = rkvenc_free_task,
};

static const struct mpp_dev_var rkvenc_v1_data = {
	.device_type = MPP_DEVICE_RKVENC,
	.hw_info = &rkvenc_hw_info,
	.trans_info = trans_rk_rkvenc,
	.hw_ops = &rkvenc_hw_ops,
	.dev_ops = &rkvenc_dev_ops,
};

static const struct of_device_id mpp_rkvenc_dt_match[] = {
	{
		.compatible = "rockchip,rkv-encoder-v1",
		.data = &rkvenc_v1_data,
	},
	{},
};

static int rkvenc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

	dev_info(dev, "probing start\n");

	enc = devm_kzalloc(dev, sizeof(*enc), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;
	mpp = &enc->mpp;
	platform_set_drvdata(pdev, enc);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvenc_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		goto failed_get_irq;
	}

	mpp->session_max_buffers = RKVENC_SESSION_MAX_BUFFERS;
	rkvenc_debugfs_init(mpp);

	dev_info(dev, "probing finish\n");

	return 0;

failed_get_irq:
	mpp_dev_remove(mpp);

	return ret;
}

static int rkvenc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&enc->mpp);
	rkvenc_debugfs_remove(&enc->mpp);

	return 0;
}

static void rkvenc_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &enc->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 1000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_rkvenc_driver = {
	.probe = rkvenc_probe,
	.remove = rkvenc_remove,
	.shutdown = rkvenc_shutdown,
	.driver = {
		.name = RKVENC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvenc_dt_match),
	},
};
