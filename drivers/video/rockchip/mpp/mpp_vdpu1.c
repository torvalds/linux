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
#include <linux/debugfs.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define VDPU1_DRIVER_NAME		"mpp_vdpu1"

#define	VDPU1_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define VDPU1_REG_NUM			60
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
#define	VDPU1_GET_FORMAT(x)		(((x) >> 28) & 0xf)
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

struct vdpu_task {
	struct mpp_task mpp_task;
	struct mpp_hw_info *hw_info;
	/* enable of post process */
	bool pp_enable;

	unsigned long aclk_freq;
	u32 reg[VDPU1_REG_PP_NUM];
	u32 idx;
	struct extra_info_for_iommu ext_inf;
	u32 strm_addr;
	u32 irq_status;
};

struct vdpu_dev {
	struct mpp_dev mpp;

	struct clk *aclk;
	struct clk *hclk;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
	u32 aclk_debug;
	u32 session_max_buffers_debug;

	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct vdpu_task *current_task;
};

static struct mpp_hw_info vdpu_v1_hw_info = {
	.reg_num = VDPU1_REG_NUM,
	.regidx_start = VDPU1_REG_START_INDEX,
	.regidx_end = VDPU1_REG_END_INDEX,
	.regidx_en = VDPU1_REG_DEC_INT_EN_INDEX,
};

static struct mpp_hw_info vdpu_pp_v1_hw_info = {
	.reg_num = VDPU1_REG_PP_NUM,
	.regidx_start = VDPU1_REG_PP_START_INDEX,
	.regidx_end = VDPU1_REG_PP_END_INDEX,
	.regidx_en = VDPU1_REG_DEC_INT_EN_INDEX,
};

/*
 * file handle translate information
 */
static const char trans_tbl_avsd[] = {
	12, 13, 14, 15, 16, 17, 40, 41, 45
};

static const char trans_tbl_default[] = {
	12, 13, 14, 15, 16, 17, 40, 41
};

static const char trans_tbl_jpegd[] = {
	12, 13, 14, 40, 66, 67
};

static const char trans_tbl_h264d[] = {
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	28, 29, 40
};

static const char trans_tbl_vc1d[] = {
	12, 13, 14, 15, 16, 17, 27, 41
};

static const char trans_tbl_vp6d[] = {
	12, 13, 14, 18, 27, 40
};

static const char trans_tbl_vp8d[] = {
	10, 12, 13, 14, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 40
};

static struct mpp_trans_info vdpu_v1_trans[] = {
	[VDPU1_FMT_H264D] = {
		.count = sizeof(trans_tbl_h264d),
		.table = trans_tbl_h264d,
	},
	[VDPU1_FMT_H263D] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_MPEG4D] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_JPEGD] = {
		.count = sizeof(trans_tbl_jpegd),
		.table = trans_tbl_jpegd,
	},
	[VDPU1_FMT_VC1D] = {
		.count = sizeof(trans_tbl_vc1d),
		.table = trans_tbl_vc1d,
	},
	[VDPU1_FMT_MPEG2D] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_MPEG1D] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_VP6D] = {
		.count = sizeof(trans_tbl_vp6d),
		.table = trans_tbl_vp6d,
	},
	[VDPU1_FMT_RESERVED] = {
		.count = 0,
		.table = NULL,
	},
	[VDPU1_FMT_VP7D] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VDPU1_FMT_VP8D] = {
		.count = sizeof(trans_tbl_vp8d),
		.table = trans_tbl_vp8d,
	},
	[VDPU1_FMT_AVSD] = {
		.count = sizeof(trans_tbl_avsd),
		.table = trans_tbl_avsd,
	},
};

static void *vdpu_alloc_task(struct mpp_session *session,
			     void __user *src, u32 size)
{
	u32 fmt;
	int err;
	u32 reg_len;
	u32 extinf_len;
	struct vdpu_task *task = NULL;
	u32 dwsize = size / sizeof(u32);
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task_init(session, &task->mpp_task);

	if (session->device_type == MPP_DEVICE_DEC_PP) {
		task->pp_enable = true;
		task->hw_info = &vdpu_pp_v1_hw_info;
	} else {
		task->hw_info = mpp->var->hw_info;
	}
	reg_len = min(task->hw_info->reg_num, dwsize);
	extinf_len = dwsize > reg_len ? (dwsize - reg_len) * 4 : 0;

	if (copy_from_user(task->reg, src, reg_len * 4)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		goto fail;
	}

	fmt = VDPU1_GET_FORMAT(task->reg[VDPU1_REG_SYS_CTRL_INDEX]);
	if (extinf_len > 0) {
		if (likely(fmt == VDPU1_FMT_JPEGD)) {
			err = copy_from_user(&task->ext_inf,
					     (u8 *)src + size
					     - JPEG_IOC_EXTRA_SIZE,
					     JPEG_IOC_EXTRA_SIZE);
		} else {
			u32 ext_cpy = min_t(size_t, extinf_len,
					    sizeof(task->ext_inf));
			err = copy_from_user(&task->ext_inf,
					     (u32 *)src + reg_len,
					     ext_cpy);
		}

		if (err) {
			mpp_err("copy_from_user failed when extra info\n");
			goto fail;
		}
	}

	err = mpp_translate_reg_address(session->mpp,
					&task->mpp_task,
					fmt,
					task->reg);
	if (err) {
		mpp_err("error: translate reg address failed.\n");
		mpp_dump_reg(task->reg,
			     task->hw_info->regidx_start,
			     task->hw_info->regidx_end);
		goto fail;
	}
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
		struct mpp_mem_region *mem_region = NULL;
		dma_addr_t iova = 0;
		u32 offset = task->reg[VDPU1_REG_DIR_MV_BASE_INDEX];
		int fd = task->reg[VDPU1_REG_DIR_MV_BASE_INDEX] & 0x3ff;

		offset = offset >> 10 << 4;
		mem_region = mpp_task_attach_fd(&task->mpp_task, fd);
		if (IS_ERR(mem_region))
			goto fail;

		iova = mem_region->iova;
		mpp_debug(DEBUG_IOMMU, "DMV[%3d]: %3d => %pad + offset %10d\n",
			  VDPU1_REG_DIR_MV_BASE_INDEX, fd, &iova, offset);
		task->reg[VDPU1_REG_DIR_MV_BASE_INDEX] = iova + offset;
	}

	task->strm_addr = task->reg[VDPU1_REG_STREAM_RLC_BASE_INDEX];

	mpp_debug(DEBUG_SET_REG, "extra info cnt %u, magic %08x",
		  task->ext_inf.cnt, task->ext_inf.magic);
	mpp_translate_extra_info(&task->mpp_task, &task->ext_inf, task->reg);

	mpp_debug_leave();

	return &task->mpp_task;

fail:
	mpp_task_finalize(session, &task->mpp_task);
	kfree(task);
	return NULL;
}

static int vdpu_prepare(struct mpp_dev *mpp,
			struct mpp_task *task)
{
	return -EINVAL;
}

static int vdpu_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	u32 regidx_en;
	struct vdpu_task *task = NULL;
	struct vdpu_dev *dec = NULL;

	mpp_debug_enter();

	task = to_vdpu_task(mpp_task);
	dec = to_vdpu_dev(mpp);

	/* FIXME: spin lock here */
	dec->current_task = task;
	/* clear cache */
	mpp_write_relaxed(mpp, VDPU1_REG_CLR_CACHE_BASE, 1);
	/* set registers for hardware */
	regidx_start = task->hw_info->regidx_start;
	regidx_end = task->hw_info->regidx_end;
	regidx_en = task->hw_info->regidx_en;
	for (i = regidx_start; i <= regidx_end; i++) {
		if (i == regidx_en)
			continue;
		mpp_write_relaxed(mpp, i * sizeof(u32), task->reg[i]);
	}
	/* Flush the registers */
	wmb();
	mpp_write(mpp, VDPU1_REG_DEC_INT_EN,
		  task->reg[regidx_en] | VDPU1_DEC_START);

	mpp_debug_leave();

	return 0;
}

static int vdpu_finish(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	u32 dec_get;
	s32 dec_length;
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	mpp_debug_enter();

	/* read register after running */
	regidx_start = task->hw_info->regidx_start;
	regidx_end = task->hw_info->regidx_end;
	for (i = regidx_start; i <= regidx_end; i++)
		task->reg[i] = mpp_read_relaxed(mpp, i * sizeof(u32));
	task->reg[VDPU1_REG_DEC_INT_EN_INDEX] = task->irq_status;
	/* revert hack for decoded length */
	dec_get = task->reg[VDPU1_REG_STREAM_RLC_BASE_INDEX];
	dec_length = dec_get - task->strm_addr;
	task->reg[VDPU1_REG_STREAM_RLC_BASE_INDEX] = dec_length << 10;
	mpp_debug(DEBUG_REGISTER,
		  "dec_get %08x dec_length %d\n",
		  dec_get, dec_length);

	mpp_debug_leave();

	return 0;
}

static int vdpu_result(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task,
		       u32 __user *dst, u32 size)
{
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	/* FIXME may overflow the kernel */
	if (copy_to_user(dst, task->reg, size)) {
		mpp_err("copy_to_user failed\n");
		return -EIO;
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

static int vdpu_debugfs_remove(struct mpp_dev *mpp)
{
#ifdef CONFIG_DEBUG_FS
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	debugfs_remove_recursive(dec->debugfs);
#endif
	return 0;
}

static int vdpu_debugfs_init(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);
	struct device_node *np = mpp->dev->of_node;

	dec->aclk_debug = 0;
	dec->session_max_buffers_debug = 0;

#ifdef CONFIG_DEBUG_FS
	dec->debugfs = debugfs_create_dir(np->name, mpp->srv->debugfs);
	if (IS_ERR_OR_NULL(dec->debugfs)) {
		mpp_err("failed on open debugfs\n");
		dec->debugfs = NULL;
		return -EIO;
	}
	debugfs_create_u32("aclk", 0644,
			   dec->debugfs, &dec->aclk_debug);
	debugfs_create_u32("session_buffers", 0644,
			   dec->debugfs, &dec->session_max_buffers_debug);
#endif
	if (dec->session_max_buffers_debug)
		mpp->session_max_buffers = dec->session_max_buffers_debug;

	return 0;
}

static int vdpu_init(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_VDPU1];

	dec->aclk = devm_clk_get(mpp->dev, "aclk_vcodec");
	if (IS_ERR_OR_NULL(dec->aclk)) {
		mpp_err("failed on clk_get aclk_vcodec\n");
		dec->aclk = NULL;
	}
	dec->hclk = devm_clk_get(mpp->dev, "hclk_vcodec");
	if (IS_ERR_OR_NULL(dec->hclk)) {
		mpp_err("failed on clk_get hclk_vcodec\n");
		dec->hclk = NULL;
	}

	dec->rst_a = devm_reset_control_get(mpp->dev, "video_a");
	if (IS_ERR_OR_NULL(dec->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		dec->rst_a = NULL;
	}
	dec->rst_h = devm_reset_control_get(mpp->dev, "video_h");
	if (IS_ERR_OR_NULL(dec->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		dec->rst_h = NULL;
	}

	return 0;
}

static int vdpu_power_on(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	if (dec->aclk)
		clk_prepare_enable(dec->aclk);
	if (dec->hclk)
		clk_prepare_enable(dec->hclk);

	return 0;
}

static int vdpu_power_off(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	if (dec->aclk)
		clk_disable_unprepare(dec->aclk);
	if (dec->hclk)
		clk_disable_unprepare(dec->hclk);

	return 0;
}

static int vdpu_get_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	task->aclk_freq = 300;

	return 0;
}

static int vdpu_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);
	struct vdpu_task *task = to_vdpu_task(mpp_task);

	/* check whether use debug freq */
	task->aclk_freq = dec->aclk_debug ?
			  dec->aclk_debug : task->aclk_freq;

	clk_set_rate(dec->aclk, task->aclk_freq * MHZ);

	return 0;
}

static int vdpu_reduce_freq(struct mpp_dev *mpp)
{
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	if (dec->aclk)
		clk_set_rate(dec->aclk, 50 * MHZ);

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
	struct mpp_task *mpp_task = NULL;
	struct vdpu_dev *dec = to_vdpu_dev(mpp);

	/* FIXME use a spin lock here */
	task = dec->current_task;
	if (!task) {
		dev_err(dec->mpp.dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_task = &task->mpp_task;
	mpp_time_diff(mpp_task);
	dec->current_task = NULL;
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
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		udelay(5);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		rockchip_pmu_idle_request(mpp->dev, false);

		mpp_debug(DEBUG_RESET, "reset out\n");
	}
	mpp_write(mpp, VDPU1_REG_DEC_INT_EN, 0);

	return 0;
}

static struct mpp_hw_ops vdpu_v1_hw_ops = {
	.init = vdpu_init,
	.power_on = vdpu_power_on,
	.power_off = vdpu_power_off,
	.get_freq = vdpu_get_freq,
	.set_freq = vdpu_set_freq,
	.reduce_freq = vdpu_reduce_freq,
	.reset = vdpu_reset,
};

static struct mpp_dev_ops vdpu_v1_dev_ops = {
	.alloc_task = vdpu_alloc_task,
	.prepare = vdpu_prepare,
	.run = vdpu_run,
	.irq = vdpu_irq,
	.isr = vdpu_isr,
	.finish = vdpu_finish,
	.result = vdpu_result,
	.free_task = vdpu_free_task,
};

static const struct mpp_dev_var vdpu_v1_data = {
	.device_type = MPP_DEVICE_DEC,
	.hw_info = &vdpu_v1_hw_info,
	.trans_info = vdpu_v1_trans,
	.hw_ops = &vdpu_v1_hw_ops,
	.dev_ops = &vdpu_v1_dev_ops,
};

static const struct mpp_dev_var avsd_plus_data = {
	.device_type = MPP_DEVICE_DEC_AVSPLUS,
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
	{
		.compatible = "rockchip,avs-plus-decoder",
		.data = &avsd_plus_data,
	},
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
	platform_set_drvdata(pdev, dec);

	mpp = &dec->mpp;
	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vdpu1_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
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

	if (mpp->var->device_type == MPP_DEVICE_DEC) {
		mpp->srv->sub_devices[MPP_DEVICE_PP] = mpp;
		mpp->srv->sub_devices[MPP_DEVICE_DEC_PP] = mpp;
	}
	mpp->session_max_buffers = VDPU1_SESSION_MAX_BUFFERS;
	vdpu_debugfs_init(mpp);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int vdpu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vdpu_dev *dec = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&dec->mpp);
	vdpu_debugfs_remove(&dec->mpp);

	return 0;
}

static void vdpu_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct vdpu_dev *dec = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &dec->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->total_running,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_vdpu1_driver = {
	.probe = vdpu_probe,
	.remove = vdpu_remove,
	.shutdown = vdpu_shutdown,
	.driver = {
		.name = VDPU1_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vdpu1_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vdpu1_driver);
