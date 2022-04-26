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

#define VDPU1_DRIVER_NAME		"mpp_vdpu1"

#define	VDPU1_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define VDPU1_REG_NUM			60
#define VDPU1_REG_HW_ID_INDEX		0
#define VDPU1_REG_START_INDEX		0
#define VDPU1_REG_END_INDEX		59

#define VDPU1_REG_PP_NUM		101
#define VDPU1_REG_PP_START_INDEX	0
#define VDPU1_REG_PP_END_INDEX		100

#define VDPU1_REG_DEC_INT_EN		0x004
#define VDPU1_REG_DEC_INT_EN_INDEX	(1)
/* B slice detected, used in 8190 decoder and later */
#define	VDPU1_INT_PIC_INF		BIT(24)
#define	VDPU1_INT_TIMEOUT		BIT(18)
#define	VDPU1_INT_SLICE			BIT(17)
#define	VDPU1_INT_STRM_ERROR		BIT(16)
#define	VDPU1_INT_ASO_ERROR		BIT(15)
#define	VDPU1_INT_BUF_EMPTY		BIT(14)
#define	VDPU1_INT_BUS_ERROR		BIT(13)
#define	VDPU1_DEC_INT			BIT(12)
#define	VDPU1_DEC_INT_RAW		BIT(8)
#define	VDPU1_DEC_IRQ_DIS		BIT(4)
#define	VDPU1_DEC_START			BIT(0)

/* NOTE: Don't enable it or decoding AVC would meet problem at rk3288 */
#define VDPU1_REG_DEC_EN		0x008
#define	VDPU1_CLOCK_GATE_EN		BIT(10)

#define VDPU1_REG_SYS_CTRL		0x00c
#define VDPU1_REG_SYS_CTRL_INDEX	(3)
#define VDPU1_RGE_WIDTH_INDEX		(4)
#define	VDPU1_GET_FORMAT(x)		(((x) >> 28) & 0xf)
#define VDPU1_GET_PROD_NUM(x)		(((x) >> 16) & 0xffff)
#define VDPU1_GET_WIDTH(x)		(((x) & 0xff800000) >> 19)
#define	VDPU1_FMT_H264D			0
#define	VDPU1_FMT_MPEG4D		1
#define	VDPU1_FMT_H263D			2
#define	VDPU1_FMT_JPEGD			3
#define	VDPU1_FMT_VC1D			4
#define	VDPU1_FMT_MPEG2D		5
#define	VDPU1_FMT_MPEG1D		6
#define	VDPU1_FMT_VP6D			7
#define	VDPU1_FMT_RESERVED		8
#define	VDPU1_FMT_VP7D			9
#define	VDPU1_FMT_VP8D			10
#define	VDPU1_FMT_AVSD			11

#define VDPU1_REG_STREAM_RLC_BASE	0x030
#define VDPU1_REG_STREAM_RLC_BASE_INDEX	(12)

#define VDPU1_REG_DIR_MV_BASE		0x0a4
#define VDPU1_REG_DIR_MV_BASE_INDEX	(41)

#define VDPU1_REG_CLR_CACHE_BASE	0x810

#define to_vdpu_task(task)		\
		container_of(task, struct vdpu_task, mpp_task)
#define to_vdpu_dev(dev)		\
		container_of(dev, struct vdpu_dev, mpp)

enum VPUD1_HW_ID {
	VDPU1_ID_0102 = 0x0102,
	VDPU1_ID_9190 = 0x6731,
};

struct vdpu_task {
	struct mpp_task mpp_task;
	/* enable of post process */
	bool pp_enable;

	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[VDPU1_REG_PP_NUM];

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

static struct mpp_hw_info vdpu_v1_hw_info = {
	.reg_num = VDPU1_REG_NUM,
	.reg_id = VDPU1_REG_HW_ID_INDEX,
	.reg_start = VDPU1_REG_START_INDEX,
	.reg_end = VDPU1_REG_END_INDEX,
	.reg_en = VDPU1_REG_DEC_INT_EN_INDEX,
};

static struct mpp_hw_info vdpu_pp_v1_hw_info = {
	.reg_num = VDPU1_REG_PP_NUM,
	.reg_id = VDPU1_REG_HW_ID_INDEX,
	.reg_start = VDPU1_REG_PP_START_INDEX,
	.reg_end = VDPU1_REG_PP_END_INDEX,
	.reg_en = VDPU1_REG_DEC_INT_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_avsd[] = {
	12, 13, 14, 15, 16, 17, 40, 41, 45
};

static const u16 trans_tbl_default[] = {
	12, 13, 14, 15, 16, 17, 40, 41
};

static const u16 trans_tbl_jpegd[] = {
	12, 13, 14, 40, 66, 67
};

static const u16 trans_tbl_h264d[] = {
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	28, 29, 40
};

static const u16 trans_tbl_vc1d[] = {
	12, 13, 14, 15, 16, 17, 27, 41
};

static const u16 trans_tbl_vp6d[] = {
	12, 13, 14, 18, 27, 40
};

static const u16 trans_tbl_vp8d[] = {
	10, 12, 13, 14, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 40
};

static struct mpp_trans_info vdpu_v1_trans[] = {
	[VDPU1_FMT_H264D] = {
		.count = ARRAY_SIZE(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[VDPU1_FMT_H263D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_MPEG4D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_JPEGD] = {
		.count = ARRAY_SIZE(trans_tbl_jpegd),
		.table = trans_tbl_jpegd,
	},
	[VDPU1_FMT_VC1D] = {
		.count = ARRAY_SIZE(trans_tbl_vc1d),
		.table = trans_tbl_vc1d,
	},
	[VDPU1_FMT_MPEG2D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_MPEG1D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_VP6D] = {
		.count = ARRAY_SIZE(trans_tbl_vp6d),
		.table = trans_tbl_vp6d,
	},
	[VDPU1_FMT_RESERVED] = {
		.count = 0,
		.table = NULL,
	},
	[VDPU1_FMT_VP7D] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_VP8D] = {
		.count = ARRAY_SIZE(trans_tbl_vp8d),
		.table = trans_tbl_vp8d,
	},
	[VDPU1_FMT_AVSD] = {
		.count = ARRAY_SIZE(trans_tbl_avsd),
		.table = trans_tbl_avsd,
	},
};

static int vdpu_process_reg_fd(struct mpp_session *session,
			       struct vdpu_task *task,
			       struct mpp_task_msgs *msgs)
{
	int ret = 0;
	int fmt = VDPU1_GET_FORMAT(task->reg[VDPU1_REG_SYS_CTRL_INDEX]);

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					fmt, task->reg, &task->off_inf);
	if (ret)
		return ret;
	/*
	 * special offset scale case
	 *
	 * This translation is for fd + offset translation.
	 * One register has 32bits. We need to transfer both buffer file
	 * handle and the start address offset so we packet file handle
	 * and offset together using below format.
	 *
	 *  0~9  bit for buffer file handle range 0 ~ 1023
	 * 10~31 bit for offset range 0 ~ 4M
	 *
	 * But on 4K case the offset can be larger the 4M
	 */
	if (likely(fmt == VDPU1_FMT_H264D)) {
		int fd;
		u32 offset;
		dma_addr_t iova = 0;
		u32 idx = VDPU1_REG_DIR_MV_BASE_INDEX;
		struct mpp_mem_region *mem_region = NULL;

		if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
			fd = task->reg[idx];
			offset = 0;
		} else {
			fd = task->reg[idx] & 0x3ff;
			offset = task->reg[idx] >> 10 << 4;
		}
		mem_region = mpp_task_attach_fd(&task->mpp_task, fd);
		if (IS_ERR(mem_region)) {
			mpp_err("reg[%03d]: %08x fd %d attach failed\n",
				idx, task->reg[idx], fd);
			goto fail;
		}

		iova = mem_region->iova;
		mpp_debug(DEBUG_IOMMU, "DMV[%3d]: %3d => %pad + offset %10d\n",
			  idx, fd, &iova, offset);
		task->reg[idx] = iova + offset;
	}

	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);
	return 0;
fail:
	return -EFAULT;
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
	if (session->device_type == MPP_DEVICE_VDPU1_PP) {
		task->pp_enable = true;
		mpp_task->hw_info = &vdpu_pp_v1_hw_info;
	} else {
		mpp_task->hw_info = mpp->var->hw_info;
	}
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
	task->strm_addr = task->reg[VDPU1_REG_STREAM_RLC_BASE_INDEX];
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

	mpp_debug_enter();

	/* clear cache */
	mpp_write_relaxed(mpp, VDPU1_REG_CLR_CACHE_BASE, 1);
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
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, VDPU1_REG_DEC_INT_EN,
		  task->reg[reg_en] | VDPU1_DEC_START);

	mpp_debug_leave();

	return 0;
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
	task->reg[VDPU1_REG_DEC_INT_EN_INDEX] = task->irq_status;
	/* revert hack for decoded length */
	dec_get = mpp_read_relaxed(mpp, VDPU1_REG_STREAM_RLC_BASE);
	dec_length = dec_get - task->strm_addr;
	task->reg[VDPU1_REG_STREAM_RLC_BASE_INDEX] = dec_length << 10;
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

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_VDPU1];

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

static int vdpu_3288_get_freq(struct mpp_dev *mpp,
			      struct mpp_task *mpp_task)
{
	u32 width;
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	width = VDPU1_GET_WIDTH(task->reg[VDPU1_RGE_WIDTH_INDEX]);
	if (width > 2560)
		task->clk_mode = CLK_MODE_ADVANCED;

	return 0;
}

static int vdpu_3368_get_freq(struct mpp_dev *mpp,
			      struct mpp_task *mpp_task)
{
	u32 width;
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	width = VDPU1_GET_WIDTH(task->reg[VDPU1_RGE_WIDTH_INDEX]);
	if (width > 2560)
		task->clk_mode = CLK_MODE_ADVANCED;

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
	mpp->irq_status = mpp_read(mpp, VDPU1_REG_DEC_INT_EN);
	if (!(mpp->irq_status & VDPU1_DEC_INT_RAW))
		return IRQ_NONE;

	mpp_write(mpp, VDPU1_REG_DEC_INT_EN, 0);
	/* set clock gating to save power */
	mpp_write(mpp, VDPU1_REG_DEC_EN, VDPU1_CLOCK_GATE_EN);

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

	err_mask = VDPU1_INT_TIMEOUT
		| VDPU1_INT_STRM_ERROR
		| VDPU1_INT_ASO_ERROR
		| VDPU1_INT_BUF_EMPTY
		| VDPU1_INT_BUS_ERROR;

	if (err_mask & task->irq_status)
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int vdpu_reset(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	if (dec->rst_a && dec->rst_h) {
		mpp_debug(DEBUG_RESET, "reset in\n");

		/* Don't skip this or iommu won't work after reset */
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		udelay(5);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		mpp_pmu_idle_request(mpp, false);

		mpp_debug(DEBUG_RESET, "reset out\n");
	}
	mpp_write(mpp, VDPU1_REG_DEC_INT_EN, 0);

	return 0;
}

static struct mpp_hw_ops vdpu_v1_hw_ops = {
	.init = vdpu_init,
	.clk_on = vdpu_clk_on,
	.clk_off = vdpu_clk_off,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
};

static struct mpp_hw_ops vdpu_3288_hw_ops = {
	.init = vdpu_init,
	.clk_on = vdpu_clk_on,
	.clk_off = vdpu_clk_off,
	.get_freq = vdpu_3288_get_freq,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
};

static struct mpp_hw_ops vdpu_3368_hw_ops = {
	.init = vdpu_init,
	.clk_on = vdpu_clk_on,
	.clk_off = vdpu_clk_off,
	.get_freq = vdpu_3368_get_freq,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
};

static struct mpp_dev_ops vdpu_v1_dev_ops = {
	.alloc_task = vdpu_alloc_task,
	.run = vdpu_run,
	.irq = vdpu_irq,
	.isr = vdpu_isr,
	.finish = vdpu_finish,
	.result = vdpu_result,
	.free_task = vdpu_free_task,
};

static const struct mpp_dev_var vdpu_v1_data = {
	.device_type = MPP_DEVICE_VDPU1,
	.hw_info = &vdpu_v1_hw_info,
	.trans_info = vdpu_v1_trans,
	.hw_ops = &vdpu_v1_hw_ops,
	.dev_ops = &vdpu_v1_dev_ops,
};

static const struct mpp_dev_var vdpu_3288_data = {
	.device_type = MPP_DEVICE_VDPU1,
	.hw_info = &vdpu_v1_hw_info,
	.trans_info = vdpu_v1_trans,
	.hw_ops = &vdpu_3288_hw_ops,
	.dev_ops = &vdpu_v1_dev_ops,
};

static const struct mpp_dev_var vdpu_3368_data = {
	.device_type = MPP_DEVICE_VDPU1,
	.hw_info = &vdpu_v1_hw_info,
	.trans_info = vdpu_v1_trans,
	.hw_ops = &vdpu_3368_hw_ops,
	.dev_ops = &vdpu_v1_dev_ops,
};

static const struct mpp_dev_var avsd_plus_data = {
	.device_type = MPP_DEVICE_AVSPLUS_DEC,
	.hw_info = &vdpu_v1_hw_info,
	.trans_info = vdpu_v1_trans,
	.hw_ops = &vdpu_v1_hw_ops,
	.dev_ops = &vdpu_v1_dev_ops,
};

static const struct of_device_id mpp_vdpu1_dt_match[] = {
	{
		.compatible = "rockchip,vpu-decoder-v1",
		.data = &vdpu_v1_data,
	},
#ifdef CONFIG_CPU_RK3288
	{
		.compatible = "rockchip,vpu-decoder-rk3288",
		.data = &vdpu_3288_data,
	},
#endif
#ifdef CONFIG_CPU_RK3368
	{
		.compatible = "rockchip,vpu-decoder-rk3368",
		.data = &vdpu_3368_data,
	},
#endif
#ifdef CONFIG_CPU_RK3328
	{
		.compatible = "rockchip,avs-plus-decoder",
		.data = &avsd_plus_data,
	},
#endif
	{},
};

static int vdpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vdpu_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probe device\n");
	dec = devm_kzalloc(dev, sizeof(struct vdpu_dev), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;
	mpp = &dec->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vdpu1_dt_match, pdev->dev.of_node);
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

	if (mpp->var->device_type == MPP_DEVICE_VDPU1) {
		mpp->srv->sub_devices[MPP_DEVICE_VDPU1_PP] = mpp;
		set_bit(MPP_DEVICE_VDPU1_PP, &mpp->srv->hw_support);
	}

	mpp->session_max_buffers = VDPU1_SESSION_MAX_BUFFERS;
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

struct platform_driver rockchip_vdpu1_driver = {
	.probe = vdpu_probe,
	.remove = vdpu_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = VDPU1_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vdpu1_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vdpu1_driver);
