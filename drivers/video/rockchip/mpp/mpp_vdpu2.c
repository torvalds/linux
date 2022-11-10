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
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/proc_fs.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"
#include "hack/mpp_hack_px30.h"

#define VDPU2_DRIVER_NAME		"mpp_vdpu2"

#define	VDPU2_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define VDPU2_REG_NUM			159
#define VDPU2_REG_HW_ID_INDEX		-1 /* INVALID */
#define VDPU2_REG_START_INDEX		50
#define VDPU2_REG_END_INDEX		158

#define VDPU2_REG_SYS_CTRL			0x0d4
#define VDPU2_REG_SYS_CTRL_INDEX		(53)
#define VDPU2_GET_FORMAT(x)			((x) & 0xf)
#define VDPU2_FMT_H264D				0
#define VDPU2_FMT_MPEG4D			1
#define VDPU2_FMT_H263D				2
#define VDPU2_FMT_JPEGD				3
#define VDPU2_FMT_VC1D				4
#define VDPU2_FMT_MPEG2D			5
#define VDPU2_FMT_MPEG1D			6
#define VDPU2_FMT_VP6D				7
#define VDPU2_FMT_RESERVED			8
#define VDPU2_FMT_VP7D				9
#define VDPU2_FMT_VP8D				10
#define VDPU2_FMT_AVSD				11

#define VDPU2_REG_DEC_INT			0x0dc
#define VDPU2_REG_DEC_INT_INDEX			(55)
#define VDPU2_INT_TIMEOUT			BIT(13)
#define VDPU2_INT_STRM_ERROR			BIT(12)
#define VDPU2_INT_SLICE				BIT(9)
#define VDPU2_INT_ASO_ERROR			BIT(8)
#define VDPU2_INT_BUF_EMPTY			BIT(6)
#define VDPU2_INT_BUS_ERROR			BIT(5)
#define	VDPU2_DEC_INT				BIT(4)
#define VDPU2_DEC_IRQ_DIS			BIT(1)
#define VDPU2_DEC_INT_RAW			BIT(0)

#define VDPU2_REG_DEC_EN			0x0e4
#define VDPU2_REG_DEC_EN_INDEX			(57)
#define VDPU2_DEC_CLOCK_GATE_EN			BIT(4)
#define VDPU2_DEC_START				BIT(0)

#define VDPU2_REG_DIR_MV_BASE			0x0f8
#define VDPU2_REG_DIR_MV_BASE_INDEX		(62)

#define VDPU2_REG_STREAM_RLC_BASE		0x100
#define VDPU2_REG_STREAM_RLC_BASE_INDEX		(64)

#define VDPU2_REG_CLR_CACHE_BASE		0x810

#define to_vdpu_task(task)		\
		container_of(task, struct vdpu_task, mpp_task)
#define to_vdpu_dev(dev)		\
		container_of(dev, struct vdpu_dev, mpp)

struct vdpu_task {
	struct mpp_task mpp_task;

	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[VDPU2_REG_NUM];

	struct reg_offset_info off_inf;
	u32 strm_addr;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct vdpu_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
};

static struct mpp_hw_info vdpu_v2_hw_info = {
	.reg_num = VDPU2_REG_NUM,
	.reg_id = VDPU2_REG_HW_ID_INDEX,
	.reg_start = VDPU2_REG_START_INDEX,
	.reg_end = VDPU2_REG_END_INDEX,
	.reg_en = VDPU2_REG_DEC_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_default[] = {
	61, 62, 63, 64, 131, 134, 135, 148
};

static const u16 trans_tbl_jpegd[] = {
	21, 22, 61, 63, 64, 131
};

static const u16 trans_tbl_h264d[] = {
	61, 63, 64, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
	98, 99
};

static const u16 trans_tbl_vc1d[] = {
	62, 63, 64, 131, 134, 135, 145, 148
};

static const u16 trans_tbl_vp6d[] = {
	61, 63, 64, 131, 136, 145
};

static const u16 trans_tbl_vp8d[] = {
	61, 63, 64, 131, 136, 137, 140, 141, 142, 143, 144, 145, 146, 147, 149
};

static struct mpp_trans_info vdpu_v2_trans[] = {
	[VDPU2_FMT_H264D] = {
		.count = ARRAY_SIZE(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[VDPU2_FMT_H263D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU2_FMT_MPEG4D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU2_FMT_JPEGD] = {
		.count = ARRAY_SIZE(trans_tbl_jpegd),
		.table = trans_tbl_jpegd,
	},
	[VDPU2_FMT_VC1D] = {
		.count = ARRAY_SIZE(trans_tbl_vc1d),
		.table = trans_tbl_vc1d,
	},
	[VDPU2_FMT_MPEG2D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU2_FMT_MPEG1D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU2_FMT_VP6D] = {
		.count = ARRAY_SIZE(trans_tbl_vp6d),
		.table = trans_tbl_vp6d,
	},
	[VDPU2_FMT_RESERVED] = {
		.count = 0,
		.table = NULL,
	},
	[VDPU2_FMT_VP7D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU2_FMT_VP8D] = {
		.count = ARRAY_SIZE(trans_tbl_vp8d),
		.table = trans_tbl_vp8d,
	},
	[VDPU2_FMT_AVSD] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
};

static int vdpu_process_reg_fd(struct mpp_session *session,
			       struct vdpu_task *task,
			       struct mpp_task_msgs *msgs)
{
	int ret = 0;
	int fmt = VDPU2_GET_FORMAT(task->reg[VDPU2_REG_SYS_CTRL_INDEX]);

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					fmt, task->reg, &task->off_inf);
	if (ret)
		return ret;

	if (likely(fmt == VDPU2_FMT_H264D)) {
		int fd;
		u32 offset;
		dma_addr_t iova = 0;
		struct mpp_mem_region *mem_region = NULL;
		int idx = VDPU2_REG_DIR_MV_BASE_INDEX;

		if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
			fd = task->reg[idx];
			offset = 0;
		} else {
			fd = task->reg[idx] & 0x3ff;
			offset = task->reg[idx] >> 10 << 4;
		}
		mem_region = mpp_task_attach_fd(&task->mpp_task, fd);
		if (IS_ERR(mem_region)) {
			mpp_err("reg[%3d]: %08x fd %d attach failed\n",
				idx, task->reg[idx], fd);
			return -EFAULT;
		}

		iova = mem_region->iova;
		mpp_debug(DEBUG_IOMMU, "DMV[%3d]: %3d => %pad + offset %10d\n",
			  idx, fd, &iova, offset);
		task->reg[idx] = iova + offset;
	}
	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);
	return 0;
}

static int vdpu_extract_task_msg(struct vdpu_task *task,
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
			ret = mpp_check_req(req, 0, sizeof(task->reg),
					    off_s, off_e);
			if (ret)
				continue;
			if (copy_from_user((u8 *)task->reg + req->offset,
					   req->data, req->size)) {
				mpp_err("copy_from_user reg failed\n");
				return -EIO;
			}
			memcpy(&task->w_reqs[task->w_req_cnt++],
			       req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_READ: {
			off_s = hw_info->reg_start * sizeof(u32);
			off_e = hw_info->reg_end * sizeof(u32);
			ret = mpp_check_req(req, 0, sizeof(task->reg),
					    off_s, off_e);
			if (ret)
				continue;
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
	mpp_debug(DEBUG_TASK_INFO, "w_req_cnt %d, r_req_cnt %d\n",
		  task->w_req_cnt, task->r_req_cnt);

	return 0;
}

static void *vdpu_alloc_task(struct mpp_session *session,
			     struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct vdpu_task *task = NULL;
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
	ret = vdpu_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = vdpu_process_reg_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	task->strm_addr = task->reg[VDPU2_REG_STREAM_RLC_BASE_INDEX];
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

static int vdpu_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	u32 i;
	u32 reg_en;
	struct vdpu_task *task = to_vdpu_task(mpp_task);
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	/* clear cache */
	mpp_write_relaxed(mpp, VDPU2_REG_CLR_CACHE_BASE, 1);
	/* set registers for hardware */
	 reg_en = mpp_task->hw_info->reg_en;
	for (i = 0; i < task->w_req_cnt; i++) {
		struct mpp_request *req = &task->w_reqs[i];
		int s = req->offset / sizeof(u32);
		int e = s + req->size / sizeof(u32);

		mpp_write_req(mpp, task->reg, s, e, reg_en);
	}
	/* init current task */
	mpp->cur_task = mpp_task;

	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);

	/* Flush the registers */
	wmb();
	mpp_write(mpp, VDPU2_REG_DEC_EN,
		  task->reg[reg_en] | VDPU2_DEC_START);

	mpp_task_run_end(mpp_task, timing_en);

	mpp_debug_leave();

	return 0;
}

static int vdpu_px30_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	mpp_iommu_flush_tlb(mpp->iommu_info);
	return vdpu_run(mpp, mpp_task);
}

static int vdpu_finish(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task)
{
	u32 i;
	u32 s, e;
	u32 dec_get;
	s32 dec_length;
	struct mpp_request *req;
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	mpp_debug_enter();

	/* read register after running */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];
		s = req->offset / sizeof(u32);
		e = s + req->size / sizeof(u32);
		mpp_read_req(mpp, task->reg, s, e);
	}
	/* revert hack for irq status */
	task->reg[VDPU2_REG_DEC_INT_INDEX] = task->irq_status;
	/* revert hack for decoded length */
	dec_get = mpp_read_relaxed(mpp, VDPU2_REG_STREAM_RLC_BASE);
	dec_length = dec_get - task->strm_addr;
	task->reg[VDPU2_REG_STREAM_RLC_BASE_INDEX] = dec_length << 10;
	mpp_debug(DEBUG_REGISTER,
		  "dec_get %08x dec_length %d\n", dec_get, dec_length);

	mpp_debug_leave();

	return 0;
}

static int vdpu_result(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task,
		       struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	/* FIXME may overflow the kernel */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];

		if (copy_to_user(req->data,
				 (u8 *)task->reg + req->offset,
				 req->size)) {
			mpp_err("copy_to_user reg fail\n");
			return -EIO;
		}
	}

	return 0;
}

static int vdpu_free_task(struct mpp_session *session,
			  struct mpp_task *mpp_task)
{
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int vdpu_procfs_remove(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int vdpu_procfs_init(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	dec->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(dec->procfs)) {
		mpp_err("failed on open procfs\n");
		dec->procfs = NULL;
		return -EIO;
	}

	/* for common mpp_dev options */
	mpp_procfs_create_common(dec->procfs, mpp);

	mpp_procfs_create_u32("aclk", 0644,
			      dec->procfs, &dec->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      dec->procfs, &mpp->session_max_buffers);

	return 0;
}
#else
static inline int vdpu_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int vdpu_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int vdpu_init(struct mpp_dev *mpp)
{
	int ret;
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_VDPU2];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &dec->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&dec->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	/* Get reset control from dtsi */
	dec->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!dec->rst_a)
		mpp_err("No aclk reset resource define\n");
	dec->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!dec->rst_h)
		mpp_err("No hclk reset resource define\n");

	return 0;
}

static int vdpu_px30_init(struct mpp_dev *mpp)
{
	vdpu_init(mpp);
	return px30_workaround_combo_init(mpp);
}

static int vdpu_clk_on(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);

	return 0;
}

static int vdpu_clk_off(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp_clk_safe_disable(dec->aclk_info.clk);
	mpp_clk_safe_disable(dec->hclk_info.clk);

	return 0;
}

static int vdpu_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);

	return 0;
}

static int vdpu_reduce_freq(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_REDUCE);

	return 0;
}

static int vdpu_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, VDPU2_REG_DEC_INT);
	if (!(mpp->irq_status & VDPU2_DEC_INT_RAW))
		return IRQ_NONE;

	mpp_write(mpp, VDPU2_REG_DEC_INT, 0);
	/* set clock gating to save power */
	mpp_write(mpp, VDPU2_REG_DEC_EN, VDPU2_DEC_CLOCK_GATE_EN);

	return IRQ_WAKE_THREAD;
}

static int vdpu_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct vdpu_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_vdpu_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	err_mask = VDPU2_INT_TIMEOUT
		| VDPU2_INT_STRM_ERROR
		| VDPU2_INT_ASO_ERROR
		| VDPU2_INT_BUF_EMPTY
		| VDPU2_INT_BUS_ERROR;

	if (err_mask & task->irq_status)
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int vdpu_reset(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp_write(mpp, VDPU2_REG_DEC_EN, 0);
	mpp_write(mpp, VDPU2_REG_DEC_INT, 0);
	if (dec->rst_a && dec->rst_h) {
		/* Don't skip this or iommu won't work after reset */
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		udelay(5);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		mpp_pmu_idle_request(mpp, false);
	}

	return 0;
}

static struct mpp_hw_ops vdpu_v2_hw_ops = {
	.init = vdpu_init,
	.clk_on = vdpu_clk_on,
	.clk_off = vdpu_clk_off,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
};

static struct mpp_hw_ops vdpu_px30_hw_ops = {
	.init = vdpu_px30_init,
	.clk_on = vdpu_clk_on,
	.clk_off = vdpu_clk_off,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
	.set_grf = px30_workaround_combo_switch_grf,
};

static struct mpp_dev_ops vdpu_v2_dev_ops = {
	.alloc_task = vdpu_alloc_task,
	.run = vdpu_run,
	.irq = vdpu_irq,
	.isr = vdpu_isr,
	.finish = vdpu_finish,
	.result = vdpu_result,
	.free_task = vdpu_free_task,
};

static struct mpp_dev_ops vdpu_px30_dev_ops = {
	.alloc_task = vdpu_alloc_task,
	.run = vdpu_px30_run,
	.irq = vdpu_irq,
	.isr = vdpu_isr,
	.finish = vdpu_finish,
	.result = vdpu_result,
	.free_task = vdpu_free_task,
};

static const struct mpp_dev_var vdpu_v2_data = {
	.device_type = MPP_DEVICE_VDPU2,
	.hw_info = &vdpu_v2_hw_info,
	.trans_info = vdpu_v2_trans,
	.hw_ops = &vdpu_v2_hw_ops,
	.dev_ops = &vdpu_v2_dev_ops,
};

static const struct mpp_dev_var vdpu_px30_data = {
	.device_type = MPP_DEVICE_VDPU2,
	.hw_info = &vdpu_v2_hw_info,
	.trans_info = vdpu_v2_trans,
	.hw_ops = &vdpu_px30_hw_ops,
	.dev_ops = &vdpu_px30_dev_ops,
};

static const struct of_device_id mpp_vdpu2_dt_match[] = {
	{
		.compatible = "rockchip,vpu-decoder-v2",
		.data = &vdpu_v2_data,
	},
#ifdef CONFIG_CPU_PX30
	{
		.compatible = "rockchip,vpu-decoder-px30",
		.data = &vdpu_px30_data,
	},
#endif
	{},
};

static int vdpu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct vdpu_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

	dev_info(dev, "probe device\n");
	dec = devm_kzalloc(dev, sizeof(struct vdpu_dev), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;
	mpp = &dec->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vdpu2_dt_match,
				      pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;

		mpp->core_id = of_alias_get_id(pdev->dev.of_node, "vdpu");
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	if (mpp->var->device_type == MPP_DEVICE_VDPU2) {
		mpp->srv->sub_devices[MPP_DEVICE_VDPU2_PP] = mpp;
		set_bit(MPP_DEVICE_VDPU2_PP, &mpp->srv->hw_support);
	}

	mpp->session_max_buffers = VDPU2_SESSION_MAX_BUFFERS;
	vdpu_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int vdpu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = dev_get_drvdata(dev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(mpp);
	vdpu_procfs_remove(mpp);

	return 0;
}

struct platform_driver rockchip_vdpu2_driver = {
	.probe = vdpu_probe,
	.remove = vdpu_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = VDPU2_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vdpu2_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vdpu2_driver);
