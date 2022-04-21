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
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>
#include <linux/nospec.h>
#include <linux/workqueue.h>
#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_iommu.h>
#include <soc/rockchip/rockchip_ipa.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

#ifdef CONFIG_PM_DEVFREQ
#include "../../../devfreq/governor.h"
#endif

#include "mpp_debug.h"
#include "mpp_iommu.h"
#include "mpp_common.h"

#define RKVENC_DRIVER_NAME			"mpp_rkvenc"

#define IOMMU_GET_BUS_ID(x)			(((x) >> 6) & 0x1f)
#define IOMMU_PAGE_SIZE				SZ_4K

#define	RKVENC_SESSION_MAX_BUFFERS		40
/* The maximum registers number of all the version */
#define RKVENC_REG_L1_NUM			780
#define RKVENC_REG_L2_NUM			320
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
#define RKVENC_ENC_RSL_INDEX			12
#define RKVENC_ENC_PIC_INDEX			13
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

#define RKVENC_GET_WIDTH(x)			(((x & 0x1ff) + 1) << 3)
#define RKVENC_GET_HEIGHT(x)			((((x >> 16) & 0x1ff) + 1) << 3)

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
	u32 width;
	u32 height;
	u32 pixels;
	/* level 2 register setting */
	u32 reg_l2_offset;
	u32 reg_l2_num;
	u32 reg_l2[RKVENC_REG_L2_NUM];
	/* register offset info */
	struct reg_offset_info off_inf;

	enum MPP_CLOCK_MODE clk_mode;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct rkvenc_session_priv {
	struct rw_semaphore rw_sem;
	/* codec info from user */
	struct {
		/* show mode */
		u32 flag;
		/* item data */
		u64 val;
	} codec_info[ENC_INFO_BUTT];
};

struct rkvenc_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info core_clk_info;
	u32 default_max_load;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_core;

#ifdef CONFIG_PM_DEVFREQ
	struct regulator *vdd;
	struct devfreq *devfreq;
	unsigned long volt;
	unsigned long core_rate_hz;
	unsigned long core_last_rate_hz;
	struct ipa_power_model_data *model_data;
	struct thermal_cooling_device *devfreq_cooling;
	struct monitor_dev_info *mdev_info;
#endif
	/* for iommu pagefault handle */
	struct work_struct iommu_work;
	struct workqueue_struct *iommu_wq;
	struct page *aux_page;
	unsigned long aux_iova;
	unsigned long fault_iova;
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
	126, 127, 128, 129, 130, 131, 95, 96
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
			mpp_extract_reg_offset_info(&task->off_inf, req);
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
	task->fmt = RKVENC_GET_FORMAT(task->reg[RKVENC_ENC_PIC_INDEX]);
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
	task->clk_mode = CLK_MODE_NORMAL;
	/* get resolution info */
	task->width = RKVENC_GET_WIDTH(task->reg[RKVENC_ENC_RSL_INDEX]);
	task->height = RKVENC_GET_HEIGHT(task->reg[RKVENC_ENC_RSL_INDEX]);
	task->pixels = task->width * task->height;
	mpp_debug(DEBUG_TASK_INFO, "width=%d, height=%d\n", task->width, task->height);

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
		int reg = i * sizeof(u32);

		mpp_debug(DEBUG_SET_REG_L2, "reg[%03d]: %04x: 0x%08x\n", i, reg, regs[i]);
		writel_relaxed(reg, mpp->reg_base + RKVENC_L2_ADDR_BASE);
		writel_relaxed(regs[i], mpp->reg_base + RKVENC_L2_WRITE_BASE);
	}

	return 0;
}

static int rkvenc_read_req_l2(struct mpp_dev *mpp,
			      u32 *regs,
			      u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		int reg = i * sizeof(u32);

		writel_relaxed(reg, mpp->reg_base + RKVENC_L2_ADDR_BASE);
		regs[i] = readl_relaxed(mpp->reg_base + RKVENC_L2_READ_BASE);
		mpp_debug(DEBUG_GET_REG_L2, "reg[%03d]: %04x: 0x%08x\n", i, reg, regs[i]);
	}

	return 0;
}

static int rkvenc_write_req_backward(struct mpp_dev *mpp, u32 *regs,
				     s32 start_idx, s32 end_idx, s32 en_idx)
{
	int i;

	for (i = end_idx - 1; i >= start_idx; i--) {
		if (i == en_idx)
			continue;
		mpp_write_relaxed(mpp, i * sizeof(u32), regs[i]);
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
		u32 timing_en = mpp->srv->timing_en;

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
				/* NOTE: for rkvenc, register should set backward */
				rkvenc_write_req_backward(mpp, task->reg, s, e, reg_en);
			}
		}

		/* flush tlb before starting hardware */
		mpp_iommu_flush_tlb(mpp->iommu_info);

		/* init current task */
		mpp->cur_task = mpp_task;

		mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);

		/* Flush the register before the start the device */
		wmb();
		mpp_write(mpp, RKVENC_ENC_START_BASE, task->reg[reg_en]);

		mpp_task_run_end(mpp_task, timing_en);
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

	mpp_write(mpp, RKVENC_INT_MSK_BASE, 0x100);
	mpp_write(mpp, RKVENC_INT_CLR_BASE, 0xffffffff);
	mpp_write(mpp, RKVENC_INT_STATUS_BASE, 0);

	mpp_debug_leave();

	return IRQ_WAKE_THREAD;
}

static int rkvenc_isr(struct mpp_dev *mpp)
{
	struct rkvenc_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();

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
		atomic_inc(&mpp->reset_request);
		if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
			/* dump error register */
			mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n", task->irq_status);
			mpp_task_dump_hw_reg(mpp);
		}
	}

	/* unmap reserve buffer */
	if (enc->aux_iova != -1) {
		iommu_unmap(mpp->iommu_info->domain, enc->aux_iova, IOMMU_PAGE_SIZE);
		enc->aux_iova = -1;
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

static int rkvenc_control(struct mpp_session *session, struct mpp_request *req)
{
	switch (req->cmd) {
	case MPP_CMD_SEND_CODEC_INFO: {
		int i;
		int cnt;
		struct codec_info_elem elem;
		struct rkvenc_session_priv *priv;

		if (!session || !session->priv) {
			mpp_err("session info null\n");
			return -EINVAL;
		}
		priv = session->priv;

		cnt = req->size / sizeof(elem);
		cnt = (cnt > ENC_INFO_BUTT) ? ENC_INFO_BUTT : cnt;
		mpp_debug(DEBUG_IOCTL, "codec info count %d\n", cnt);
		for (i = 0; i < cnt; i++) {
			if (copy_from_user(&elem, req->data + i * sizeof(elem), sizeof(elem))) {
				mpp_err("copy_from_user failed\n");
				continue;
			}
			if (elem.type > ENC_INFO_BASE && elem.type < ENC_INFO_BUTT &&
			    elem.flag > CODEC_INFO_FLAG_NULL && elem.flag < CODEC_INFO_FLAG_BUTT) {
				elem.type = array_index_nospec(elem.type, ENC_INFO_BUTT);
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

static int rkvenc_free_session(struct mpp_session *session)
{
	if (session && session->priv) {
		kfree(session->priv);
		session->priv = NULL;
	}

	return 0;
}

static int rkvenc_init_session(struct mpp_session *session)
{
	struct rkvenc_session_priv *priv;

	if (!session) {
		mpp_err("session is null\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_rwsem(&priv->rw_sem);
	session->priv = priv;

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int rkvenc_procfs_remove(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->procfs) {
		proc_remove(enc->procfs);
		enc->procfs = NULL;
	}

	return 0;
}

static int rkvenc_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	int i;
	struct rkvenc_session_priv *priv = session->priv;

	down_read(&priv->rw_sem);
	/* item name */
	seq_puts(seq, "------------------------------------------------------");
	seq_puts(seq, "------------------------------------------------------\n");
	seq_printf(seq, "|%8s|", (const char *)"session");
	seq_printf(seq, "%8s|", (const char *)"device");
	for (i = ENC_INFO_BASE; i < ENC_INFO_BUTT; i++) {
		bool show = priv->codec_info[i].flag;

		if (show)
			seq_printf(seq, "%8s|", enc_info_item_name[i]);
	}
	seq_puts(seq, "\n");
	/* item data*/
	seq_printf(seq, "|%8d|", session->index);
	seq_printf(seq, "%8s|", mpp_device_name[session->device_type]);
	for (i = ENC_INFO_BASE; i < ENC_INFO_BUTT; i++) {
		u32 flag = priv->codec_info[i].flag;

		if (!flag)
			continue;
		if (flag == CODEC_INFO_FLAG_NUMBER) {
			u32 data = priv->codec_info[i].val;

			seq_printf(seq, "%8d|", data);
		} else if (flag == CODEC_INFO_FLAG_STRING) {
			const char *name = (const char *)&priv->codec_info[i].val;

			seq_printf(seq, "%8s|", name);
		} else {
			seq_printf(seq, "%8s|", (const char *)"null");
		}
	}
	seq_puts(seq, "\n");
	up_read(&priv->rw_sem);

	return 0;
}

static int rkvenc_show_session_info(struct seq_file *seq, void *offset)
{
	struct mpp_session *session = NULL, *n;
	struct mpp_dev *mpp = seq->private;

	mutex_lock(&mpp->srv->session_lock);
	list_for_each_entry_safe(session, n,
				 &mpp->srv->session_list,
				 service_link) {
		if (session->device_type != MPP_DEVICE_RKVENC)
			continue;
		if (!session->priv)
			continue;
		if (mpp->dev_ops->dump_session)
			mpp->dev_ops->dump_session(session, seq);
	}
	mutex_unlock(&mpp->srv->session_lock);

	return 0;
}

static int rkvenc_procfs_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	enc->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(enc->procfs)) {
		mpp_err("failed on open procfs\n");
		enc->procfs = NULL;
		return -EIO;
	}

	/* for common mpp_dev options */
	mpp_procfs_create_common(enc->procfs, mpp);

	/* for debug */
	mpp_procfs_create_u32("aclk", 0644,
			      enc->procfs, &enc->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_core", 0644,
			      enc->procfs, &enc->core_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      enc->procfs, &mpp->session_max_buffers);
	/* for show session info */
	proc_create_single_data("sessions-info", 0444,
				enc->procfs, rkvenc_show_session_info, mpp);

	return 0;
}
#else
static inline int rkvenc_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvenc_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}

static inline int rkvenc_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	return 0;
}
#endif

#ifdef CONFIG_PM_DEVFREQ
static int rkvenc_devfreq_target(struct device *dev,
				 unsigned long *freq, u32 flags)
{
	struct dev_pm_opp *opp;
	unsigned long target_volt, target_freq;
	int ret = 0;

	struct rkvenc_dev *enc = dev_get_drvdata(dev);
	struct devfreq *devfreq = enc->devfreq;
	struct devfreq_dev_status *stat = &devfreq->last_status;
	unsigned long old_clk_rate = stat->current_frequency;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu Hz\n", *freq);
		return PTR_ERR(opp);
	}
	target_freq = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (old_clk_rate == target_freq) {
		enc->core_last_rate_hz = target_freq;
		if (enc->volt == target_volt)
			return ret;
		ret = regulator_set_voltage(enc->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return ret;
		}
		enc->volt = target_volt;
		return 0;
	}

	if (old_clk_rate < target_freq) {
		ret = regulator_set_voltage(enc->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "set voltage %lu uV\n", target_volt);
			return ret;
		}
	}

	dev_dbg(dev, "%lu-->%lu\n", old_clk_rate, target_freq);
	clk_set_rate(enc->core_clk_info.clk, target_freq);
	stat->current_frequency = target_freq;
	enc->core_last_rate_hz = target_freq;

	if (old_clk_rate > target_freq) {
		ret = regulator_set_voltage(enc->vdd, target_volt, INT_MAX);
		if (ret) {
			dev_err(dev, "set vol %lu uV\n", target_volt);
			return ret;
		}
	}
	enc->volt = target_volt;

	return ret;
}

static int rkvenc_devfreq_get_dev_status(struct device *dev,
					 struct devfreq_dev_status *stat)
{
	return 0;
}

static int rkvenc_devfreq_get_cur_freq(struct device *dev,
				       unsigned long *freq)
{
	struct rkvenc_dev *enc = dev_get_drvdata(dev);

	*freq = enc->core_last_rate_hz;

	return 0;
}

static struct devfreq_dev_profile rkvenc_devfreq_profile = {
	.target	= rkvenc_devfreq_target,
	.get_dev_status	= rkvenc_devfreq_get_dev_status,
	.get_cur_freq = rkvenc_devfreq_get_cur_freq,
};

static int devfreq_venc_ondemand_func(struct devfreq *df, unsigned long *freq)
{
	struct rkvenc_dev *enc = df->data;

	if (enc)
		*freq = enc->core_rate_hz;
	else
		*freq = df->previous_freq;

	return 0;
}

static int devfreq_venc_ondemand_handler(struct devfreq *devfreq,
					 unsigned int event, void *data)
{
	return 0;
}

static struct devfreq_governor devfreq_venc_ondemand = {
	.name = "venc_ondemand",
	.get_target_freq = devfreq_venc_ondemand_func,
	.event_handler = devfreq_venc_ondemand_handler,
};

static unsigned long rkvenc_get_static_power(struct devfreq *devfreq,
					     unsigned long voltage)
{
	struct rkvenc_dev *enc = devfreq->data;

	if (!enc->model_data)
		return 0;
	else
		return rockchip_ipa_get_static_power(enc->model_data,
						     voltage);
}

static struct devfreq_cooling_power venc_cooling_power_data = {
	.get_static_power = rkvenc_get_static_power,
};

static struct monitor_dev_profile enc_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
};

static int __maybe_unused rv1126_get_soc_info(struct device *dev,
					      struct device_node *np,
					      int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;

	if (of_property_match_string(np, "nvmem-cell-names", "performance") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np, "performance", &value);
		if (ret) {
			dev_err(dev, "Failed to get soc performance value\n");
			return ret;
		}
		if (value == 0x1)
			*bin = 1;
		else
			*bin = 0;
	}
	if (*bin >= 0)
		dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static const struct rockchip_opp_data __maybe_unused rv1126_rkvenc_opp_data = {
	.get_soc_info = rv1126_get_soc_info,
};

static const struct of_device_id rockchip_rkvenc_of_match[] = {
#ifdef CONFIG_CPU_RV1126
	{
		.compatible = "rockchip,rv1109",
		.data = (void *)&rv1126_rkvenc_opp_data,
	},
	{
		.compatible = "rockchip,rv1126",
		.data = (void *)&rv1126_rkvenc_opp_data,
	},
#endif
	{},
};

static int rkvenc_devfreq_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct clk *clk_core = enc->core_clk_info.clk;
	struct devfreq_cooling_power *venc_dcp = &venc_cooling_power_data;
	struct rockchip_opp_info opp_info = {0};
	int ret = 0;

	if (!clk_core)
		return 0;

	enc->vdd = devm_regulator_get_optional(mpp->dev, "venc");
	if (IS_ERR_OR_NULL(enc->vdd)) {
		if (PTR_ERR(enc->vdd) == -EPROBE_DEFER) {
			dev_warn(mpp->dev, "venc regulator not ready, retry\n");

			return -EPROBE_DEFER;
		}
		dev_info(mpp->dev, "no regulator, devfreq is disabled\n");

		return 0;
	}

	rockchip_get_opp_data(rockchip_rkvenc_of_match, &opp_info);
	ret = rockchip_init_opp_table(mpp->dev, &opp_info, "leakage", "venc");
	if (ret) {
		dev_err(mpp->dev, "failed to init_opp_table\n");
		return ret;
	}

	ret = devfreq_add_governor(&devfreq_venc_ondemand);
	if (ret) {
		dev_err(mpp->dev, "failed to add venc_ondemand governor\n");
		goto governor_err;
	}

	rkvenc_devfreq_profile.initial_freq = clk_get_rate(clk_core);

	enc->devfreq = devm_devfreq_add_device(mpp->dev,
					       &rkvenc_devfreq_profile,
					       "venc_ondemand", (void *)enc);
	if (IS_ERR(enc->devfreq)) {
		ret = PTR_ERR(enc->devfreq);
		enc->devfreq = NULL;
		goto devfreq_err;
	}
	enc->devfreq->last_status.total_time = 1;
	enc->devfreq->last_status.busy_time = 1;

	devfreq_register_opp_notifier(mpp->dev, enc->devfreq);

	of_property_read_u32(mpp->dev->of_node, "dynamic-power-coefficient",
			     (u32 *)&venc_dcp->dyn_power_coeff);
	enc->model_data = rockchip_ipa_power_model_init(mpp->dev,
							"venc_leakage");
	if (IS_ERR_OR_NULL(enc->model_data)) {
		enc->model_data = NULL;
		dev_err(mpp->dev, "failed to initialize power model\n");
	} else if (enc->model_data->dynamic_coefficient) {
		venc_dcp->dyn_power_coeff =
			enc->model_data->dynamic_coefficient;
	}
	if (!venc_dcp->dyn_power_coeff) {
		dev_err(mpp->dev, "failed to get dynamic-coefficient\n");
		goto out;
	}

	enc->devfreq_cooling =
		of_devfreq_cooling_register_power(mpp->dev->of_node,
						  enc->devfreq, venc_dcp);
	if (IS_ERR_OR_NULL(enc->devfreq_cooling))
		dev_err(mpp->dev, "failed to register cooling device\n");

	enc_mdevp.data = enc->devfreq;
	enc->mdev_info = rockchip_system_monitor_register(mpp->dev, &enc_mdevp);
	if (IS_ERR(enc->mdev_info)) {
		dev_dbg(mpp->dev, "without system monitor\n");
		enc->mdev_info = NULL;
	}

out:

	return 0;

devfreq_err:
	devfreq_remove_governor(&devfreq_venc_ondemand);
governor_err:
	dev_pm_opp_of_remove_table(mpp->dev);

	return ret;
}

static int rkvenc_devfreq_remove(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (enc->mdev_info)
		rockchip_system_monitor_unregister(enc->mdev_info);
	if (enc->devfreq) {
		devfreq_unregister_opp_notifier(mpp->dev, enc->devfreq);
		dev_pm_opp_of_remove_table(mpp->dev);
		devfreq_remove_governor(&devfreq_venc_ondemand);
	}

	return 0;
}
#endif

static void rkvenc_iommu_handle_work(struct work_struct *work_s)
{
	int ret = 0;
	struct rkvenc_dev *enc = container_of(work_s, struct rkvenc_dev, iommu_work);
	struct mpp_dev *mpp = &enc->mpp;
	unsigned long page_iova = 0;

	mpp_debug_enter();

	/* avoid another page fault occur after page fault */
	mpp_iommu_down_write(mpp->iommu_info);

	if (enc->aux_iova != -1) {
		iommu_unmap(mpp->iommu_info->domain, enc->aux_iova, IOMMU_PAGE_SIZE);
		enc->aux_iova = -1;
	}

	page_iova = round_down(enc->fault_iova, SZ_4K);
	ret = iommu_map(mpp->iommu_info->domain, page_iova,
			page_to_phys(enc->aux_page), IOMMU_PAGE_SIZE,
			IOMMU_READ | IOMMU_WRITE);
	if (ret)
		mpp_err("iommu_map iova %lx error.\n", page_iova);
	else
		enc->aux_iova = page_iova;

	rockchip_iommu_unmask_irq(mpp->dev);
	mpp_iommu_up_write(mpp->iommu_info);

	mpp_debug_leave();
}

static int rkvenc_iommu_fault_handle(struct iommu_domain *iommu,
				     struct device *iommu_dev,
				     unsigned long iova, int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();
	mpp_debug(DEBUG_IOMMU, "IOMMU_GET_BUS_ID(status)=%d\n", IOMMU_GET_BUS_ID(status));
	if (IOMMU_GET_BUS_ID(status)) {
		enc->fault_iova = iova;
		rockchip_iommu_mask_irq(mpp->dev);
		queue_work(enc->iommu_wq, &enc->iommu_work);
	}
	mpp_debug_leave();

	return 0;
}

static int rkvenc_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	int ret = 0;

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_RKVENC];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &enc->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &enc->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &enc->core_clk_info, "clk_core");
	if (ret)
		mpp_err("failed on clk_get clk_core\n");
	/* Get normal max workload from dtsi */
	of_property_read_u32(mpp->dev->of_node,
			     "rockchip,default-max-load",
			     &enc->default_max_load);
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&enc->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);
	mpp_set_clk_info_rate_hz(&enc->core_clk_info, CLK_MODE_DEFAULT, 600 * MHZ);

	/* Get reset control from dtsi */
	enc->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!enc->rst_a)
		mpp_err("No aclk reset resource define\n");
	enc->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!enc->rst_h)
		mpp_err("No hclk reset resource define\n");
	enc->rst_core = mpp_reset_control_get(mpp, RST_TYPE_CORE, "video_core");
	if (!enc->rst_core)
		mpp_err("No core reset resource define\n");

#ifdef CONFIG_PM_DEVFREQ
	ret = rkvenc_devfreq_init(mpp);
	if (ret)
		mpp_err("failed to add venc devfreq\n");
#endif

	/* for mmu pagefault */
	enc->aux_page = alloc_page(GFP_KERNEL);
	if (!enc->aux_page) {
		dev_err(mpp->dev, "allocate a page for auxiliary usage\n");
		return -ENOMEM;
	}
	enc->aux_iova = -1;

	enc->iommu_wq = create_singlethread_workqueue("iommu_wq");
	if (!enc->iommu_wq) {
		mpp_err("failed to create workqueue\n");
		return -ENOMEM;
	}
	INIT_WORK(&enc->iommu_work, rkvenc_iommu_handle_work);

	mpp->iommu_info->hdl = rkvenc_iommu_fault_handle;

	return ret;
}

static int rkvenc_exit(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

#ifdef CONFIG_PM_DEVFREQ
	rkvenc_devfreq_remove(mpp);
#endif

	if (enc->aux_page)
		__free_page(enc->aux_page);

	if (enc->aux_iova != -1) {
		iommu_unmap(mpp->iommu_info->domain, enc->aux_iova, IOMMU_PAGE_SIZE);
		enc->aux_iova = -1;
	}

	if (enc->iommu_wq) {
		destroy_workqueue(enc->iommu_wq);
		enc->iommu_wq = NULL;
	}

	return 0;
}

static int rkvenc_reset(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();

#ifdef CONFIG_PM_DEVFREQ
	if (enc->devfreq)
		mutex_lock(&enc->devfreq->lock);
#endif
	mpp_clk_set_rate(&enc->aclk_info, CLK_MODE_REDUCE);
	mpp_clk_set_rate(&enc->core_clk_info, CLK_MODE_REDUCE);
	/* safe reset */
	mpp_write(mpp, RKVENC_INT_MSK_BASE, 0x1FF);
	mpp_write(mpp, RKVENC_CLR_BASE, RKVENC_SAFE_CLR_BIT);
	udelay(5);
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", mpp_read(mpp, RKVENC_INT_STATUS_BASE));
	mpp_write(mpp, RKVENC_INT_CLR_BASE, 0xffffffff);
	mpp_write(mpp, RKVENC_INT_STATUS_BASE, 0);
	/* cru reset */
	if (enc->rst_a && enc->rst_h && enc->rst_core) {
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(enc->rst_a);
		mpp_safe_reset(enc->rst_h);
		mpp_safe_reset(enc->rst_core);
		udelay(5);
		mpp_safe_unreset(enc->rst_a);
		mpp_safe_unreset(enc->rst_h);
		mpp_safe_unreset(enc->rst_core);
		mpp_pmu_idle_request(mpp, false);
	}
#ifdef CONFIG_PM_DEVFREQ
	if (enc->devfreq)
		mutex_unlock(&enc->devfreq->lock);
#endif

	mpp_debug_leave();

	return 0;
}

static int rkvenc_clk_on(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_clk_safe_enable(enc->aclk_info.clk);
	mpp_clk_safe_enable(enc->hclk_info.clk);
	mpp_clk_safe_enable(enc->core_clk_info.clk);

	return 0;
}

static int rkvenc_clk_off(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	clk_disable_unprepare(enc->aclk_info.clk);
	clk_disable_unprepare(enc->hclk_info.clk);
	clk_disable_unprepare(enc->core_clk_info.clk);

	return 0;
}

static int rkvenc_get_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	u32 task_cnt;
	u32 workload;
	struct mpp_task *loop = NULL, *n;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	/* if not set max load, consider not have advanced mode */
	if (!enc->default_max_load)
		return 0;

	task_cnt = 1;
	workload = task->pixels;
	/* calc workload in pending list */
	mutex_lock(&mpp->queue->pending_lock);
	list_for_each_entry_safe(loop, n,
				 &mpp->queue->pending_list,
				 queue_link) {
		struct rkvenc_task *loop_task = to_rkvenc_task(loop);

		task_cnt++;
		workload += loop_task->pixels;
	}
	mutex_unlock(&mpp->queue->pending_lock);

	if (workload > enc->default_max_load)
		task->clk_mode = CLK_MODE_ADVANCED;

	mpp_debug(DEBUG_TASK_INFO, "pending task %d, workload %d, clk_mode=%d\n",
		  task_cnt, workload, task->clk_mode);

	return 0;
}

static int rkvenc_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_clk_set_rate(&enc->aclk_info, task->clk_mode);

#ifdef CONFIG_PM_DEVFREQ
	if (enc->devfreq) {
		unsigned long core_rate_hz;

		mutex_lock(&enc->devfreq->lock);
		core_rate_hz = mpp_get_clk_info_rate_hz(&enc->core_clk_info, task->clk_mode);
		if (enc->core_rate_hz != core_rate_hz) {
			enc->core_rate_hz = core_rate_hz;
			update_devfreq(enc->devfreq);
		} else {
			/*
			 * Restore frequency when frequency is changed by
			 * rkvenc_reduce_freq()
			 */
			clk_set_rate(enc->core_clk_info.clk, enc->core_last_rate_hz);
		}
		mutex_unlock(&enc->devfreq->lock);
		return 0;
	}
#endif
	mpp_clk_set_rate(&enc->core_clk_info, task->clk_mode);

	return 0;
}

static struct mpp_hw_ops rkvenc_hw_ops = {
	.init = rkvenc_init,
	.exit = rkvenc_exit,
	.clk_on = rkvenc_clk_on,
	.clk_off = rkvenc_clk_off,
	.get_freq = rkvenc_get_freq,
	.set_freq = rkvenc_set_freq,
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
	.ioctl = rkvenc_control,
	.init_session = rkvenc_init_session,
	.free_session = rkvenc_free_session,
	.dump_session = rkvenc_dump_session,
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
	platform_set_drvdata(pdev, mpp);

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
	rkvenc_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;

failed_get_irq:
	mpp_dev_remove(mpp);

	return ret;
}

static int rkvenc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = dev_get_drvdata(dev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(mpp);
	rkvenc_procfs_remove(mpp);

	return 0;
}

struct platform_driver rockchip_rkvenc_driver = {
	.probe = rkvenc_probe,
	.remove = rkvenc_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = RKVENC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvenc_dt_match),
	},
};
