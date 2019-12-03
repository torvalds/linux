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
#include "mpp_common.h"
#include "mpp_iommu.h"

#define VEPU2_DRIVER_NAME		"mpp_vepu2"

#define	VEPU2_SESSION_MAX_BUFFERS		20
/* The maximum registers number of all the version */
#define VEPU2_REG_NUM				184
#define VEPU2_REG_START_INDEX			0
#define VEPU2_REG_END_INDEX			183

#define VEPU2_REG_ENC_EN			0x19c
#define VEPU2_REG_ENC_EN_INDEX			(103)
#define VEPU2_ENC_START				BIT(0)

#define VEPU2_GET_FORMAT(x)			(((x) >> 4) & 0x3)
#define VEPU2_FORMAT_MASK			(0x30)

#define VEPU2_FMT_RESERVED			(0)
#define VEPU2_FMT_VP8E				(1)
#define VEPU2_FMT_JPEGE				(2)
#define VEPU2_FMT_H264E				(3)

#define VEPU2_REG_MB_CTRL			0x1a0
#define VEPU2_REG_MB_CTRL_INDEX			(104)

#define VEPU2_REG_INT				0x1b4
#define VEPU2_REG_INT_INDEX			(109)
#define VEPU2_MV_SAD_WR_EN			BIT(24)
#define VEPU2_ROCON_WRITE_DIS			BIT(20)
#define VEPU2_INT_SLICE_EN			BIT(16)
#define VEPU2_CLOCK_GATE_EN			BIT(12)
#define VEPU2_INT_TIMEOUT_EN			BIT(10)
#define VEPU2_INT_CLEAR				BIT(9)
#define VEPU2_IRQ_DIS				BIT(8)
#define VEPU2_INT_TIMEOUT			BIT(6)
#define VEPU2_INT_BUF_FULL			BIT(5)
#define VEPU2_INT_BUS_ERROR			BIT(4)
#define VEPU2_INT_SLICE				BIT(2)
#define VEPU2_INT_RDY				BIT(1)
#define VEPU2_INT_RAW				BIT(0)

#define RKVPUE2_REG_DMV_4P_1P(i)		(0x1e0 + ((i) << 4))
#define RKVPUE2_REG_DMV_4P_1P_INDEX(i)		(120 + (i))

#define VEPU2_REG_CLR_CACHE_BASE		0xc10

#define to_vepu_task(task)		\
		container_of(task, struct vepu_task, mpp_task)
#define to_vepu_dev(dev)		\
		container_of(dev, struct vepu_dev, mpp)

struct vepu_task {
	struct mpp_task mpp_task;
	struct mpp_hw_info *hw_info;
	unsigned long aclk_freq;
	u32 reg[VEPU2_REG_NUM];
	u32 idx;
	struct extra_info_for_iommu ext_inf;
	u32 irq_status;
};

struct vepu_dev {
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
	struct vepu_task *current_task;
};

static struct mpp_hw_info vepu_v2_hw_info = {
	.reg_num = VEPU2_REG_NUM,
	.regidx_start = VEPU2_REG_START_INDEX,
	.regidx_end = VEPU2_REG_END_INDEX,
	.regidx_en = VEPU2_REG_ENC_EN_INDEX,
};

/*
 * file handle translate information
 */
static const char trans_tbl_default[] = {
	48, 49, 50, 56, 57, 63, 64, 77, 78, 81
};

static const char trans_tbl_vp8e[] = {
	27, 44, 45, 48, 49, 50, 56, 57, 63, 64,
	76, 77, 78, 80, 81, 106, 108,
};

static struct mpp_trans_info trans_rk_vepu2[] = {
	[VEPU2_FMT_RESERVED] = {
		.count = 0,
		.table = NULL,
	},
	[VEPU2_FMT_VP8E] = {
		.count = sizeof(trans_tbl_vp8e),
		.table = trans_tbl_vp8e,
	},
	[VEPU2_FMT_JPEGE] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VEPU2_FMT_H264E] = {
		.count = sizeof(trans_tbl_default),
		.table = trans_tbl_default,
	},
};

static void *vepu_alloc_task(struct mpp_session *session,
			     void __user *src, u32 size)
{
	u32 fmt;
	int err;
	u32 reg_len;
	u32 extinf_len;

	struct vepu_task *task = NULL;
	u32 dwsize = size / sizeof(u32);
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task_init(session, &task->mpp_task);
	task->hw_info = mpp->var->hw_info;
	reg_len = min(task->hw_info->reg_num, dwsize);
	extinf_len = dwsize > reg_len ? (dwsize - reg_len) * 4 : 0;

	if (copy_from_user(task->reg, src, reg_len * 4)) {
		mpp_err("error: copy_from_user failed in reg_init\n");
		goto fail;
	}

	fmt = VEPU2_GET_FORMAT(task->reg[VEPU2_REG_ENC_EN_INDEX]);
	if (extinf_len > 0) {
		if (likely(fmt == VEPU2_FMT_JPEGE)) {
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
					fmt, task->reg);
	if (err) {
		mpp_err("error: translate reg address failed.\n");
		mpp_dump_reg(task->reg,
			     task->hw_info->regidx_start,
			     task->hw_info->regidx_end);
		goto fail;
	}

	mpp_debug(DEBUG_SET_REG, "extra info cnt %u, magic %08x",
		  task->ext_inf.cnt, task->ext_inf.magic);
	mpp_translate_extra_info(&task->mpp_task,
				 &task->ext_inf, task->reg);

	mpp_debug_leave();

	return &task->mpp_task;

fail:
	mpp_task_finalize(session, &task->mpp_task);
	kfree(task);
	return NULL;
}

static int vepu_prepare(struct mpp_dev *mpp,
			struct mpp_task *task)
{
	return -EINVAL;
}

static int vepu_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	u32 regidx_en;
	struct vepu_task *task = NULL;
	struct vepu_dev *enc = NULL;

	mpp_debug_enter();

	task = to_vepu_task(mpp_task);
	enc = to_vepu_dev(mpp);

	/* FIXME: spin lock here */
	enc->current_task = task;
	/* clear cache */
	mpp_write_relaxed(mpp, VEPU2_REG_CLR_CACHE_BASE, 1);

	regidx_start = task->hw_info->regidx_start;
	regidx_end = task->hw_info->regidx_end;
	regidx_en = task->hw_info->regidx_en;
	/* First, flush correct encoder format */
	mpp_write_relaxed(mpp, VEPU2_REG_ENC_EN,
			  task->reg[regidx_en] & VEPU2_FORMAT_MASK);
	/* Second, flush others register */
	for (i = regidx_start; i <= regidx_end; i++) {
		if (i == regidx_en)
			continue;
		mpp_write_relaxed(mpp, i * sizeof(u32), task->reg[i]);
	}
	/* Last, flush the registers */
	wmb();
	mpp_write(mpp, VEPU2_REG_ENC_EN,
		  task->reg[regidx_en] | VEPU2_ENC_START);

	mpp_debug_leave();

	return 0;
}

static int vepu_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, VEPU2_REG_INT);
	if (!(mpp->irq_status & VEPU2_INT_RAW))
		return IRQ_NONE;

	mpp_write(mpp, VEPU2_REG_INT, 0);

	return IRQ_WAKE_THREAD;
}

static int vepu_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct vepu_task *task = NULL;
	struct mpp_task *mpp_task = NULL;
	struct vepu_dev *enc = to_vepu_dev(mpp);

	/* FIXME use a spin lock here */
	task = enc->current_task;
	if (!task) {
		dev_err(enc->mpp.dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_task = &task->mpp_task;
	mpp_time_diff(mpp_task);
	enc->current_task = NULL;
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	err_mask = VEPU2_INT_TIMEOUT
		| VEPU2_INT_BUF_FULL
		| VEPU2_INT_BUS_ERROR;

	if (err_mask & task->irq_status)
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int vepu_finish(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task)
{
	u32 i;
	u32 regidx_start;
	u32 regidx_end;
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_debug_enter();
	/* read register after running */
	regidx_start = task->hw_info->regidx_start;
	regidx_end = task->hw_info->regidx_end;
	for (i = regidx_start; i <= regidx_end; i++)
		task->reg[i] = mpp_read_relaxed(mpp, i * sizeof(u32));
	task->reg[VEPU2_REG_INT_INDEX] = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static int vepu_result(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task,
		       u32 __user *dst, u32 size)
{
	struct vepu_task *task = to_vepu_task(mpp_task);

	/* FIXME may overflow the kernel */
	if (copy_to_user(dst, task->reg, size)) {
		mpp_err("copy_to_user failed\n");
		return -EIO;
	}

	return 0;
}

static int vepu_free_task(struct mpp_session *session,
			  struct mpp_task *mpp_task)
{
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

static int vepu_debugfs_remove(struct mpp_dev *mpp)
{
#ifdef CONFIG_DEBUG_FS
	struct vepu_dev *enc = to_vepu_dev(mpp);

	debugfs_remove_recursive(enc->debugfs);
#endif
	return 0;
}

static int vepu_debugfs_init(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);
	struct device_node *np = mpp->dev->of_node;

	enc->aclk_debug = 0;
	enc->session_max_buffers_debug = 0;
#ifdef CONFIG_DEBUG_FS
	enc->debugfs = debugfs_create_dir(np->name, mpp->srv->debugfs);
	if (IS_ERR_OR_NULL(enc->debugfs)) {
		mpp_err("failed on open debugfs\n");
		enc->debugfs = NULL;
		return -EIO;
	}
	debugfs_create_u32("aclk", 0644,
			   enc->debugfs, &enc->aclk_debug);
	debugfs_create_u32("session_buffers", 0644,
			   enc->debugfs, &enc->session_max_buffers_debug);
#endif
	if (enc->session_max_buffers_debug)
		mpp->session_max_buffers = enc->session_max_buffers_debug;
	return 0;
}

static int vepu_init(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_VEPU2];

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

	enc->rst_a = devm_reset_control_get(mpp->dev, "video_a");
	if (IS_ERR_OR_NULL(enc->rst_a)) {
		mpp_err("No aclk reset resource define\n");
		enc->rst_a = NULL;
	}
	enc->rst_h = devm_reset_control_get(mpp->dev, "video_h");
	if (IS_ERR_OR_NULL(enc->rst_h)) {
		mpp_err("No hclk reset resource define\n");
		enc->rst_h = NULL;
	}

	return 0;
}

static int vepu_power_on(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->aclk)
		clk_prepare_enable(enc->aclk);
	if (enc->hclk)
		clk_prepare_enable(enc->hclk);

	return 0;
}

static int vepu_power_off(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->aclk)
		clk_disable_unprepare(enc->aclk);
	if (enc->hclk)
		clk_disable_unprepare(enc->hclk);

	return 0;
}

static int vepu_get_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vepu_task *task = to_vepu_task(mpp_task);

	task->aclk_freq = 300;

	return 0;
}

static int vepu_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);
	struct vepu_task *task = to_vepu_task(mpp_task);

	/* check whether use debug freq */
	task->aclk_freq = enc->aclk_debug ?
			enc->aclk_debug : task->aclk_freq;

	clk_set_rate(enc->aclk, task->aclk_freq * MHZ);

	return 0;
}

static int vepu_reduce_freq(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->aclk)
		clk_set_rate(enc->aclk, 50 * MHZ);

	return 0;
}

static int vepu_reset(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->rst_a && enc->rst_h) {
		/* Don't skip this or iommu won't work after reset */
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(enc->rst_a);
		mpp_safe_reset(enc->rst_h);
		udelay(5);
		mpp_safe_unreset(enc->rst_a);
		mpp_safe_unreset(enc->rst_h);
		rockchip_pmu_idle_request(mpp->dev, false);
	}
	mpp_write(mpp, VEPU2_REG_INT, VEPU2_INT_CLEAR);

	return 0;
}

static struct mpp_hw_ops vepu_v2_hw_ops = {
	.init = vepu_init,
	.power_on = vepu_power_on,
	.power_off = vepu_power_off,
	.get_freq = vepu_get_freq,
	.set_freq = vepu_set_freq,
	.reduce_freq = vepu_reduce_freq,
	.reset = vepu_reset,
};

static struct mpp_dev_ops vepu_v2_dev_ops = {
	.alloc_task = vepu_alloc_task,
	.prepare = vepu_prepare,
	.run = vepu_run,
	.irq = vepu_irq,
	.isr = vepu_isr,
	.finish = vepu_finish,
	.result = vepu_result,
	.free_task = vepu_free_task,
};

static const struct mpp_dev_var vepu_v2_data = {
	.device_type = MPP_DEVICE_ENC,
	.hw_info = &vepu_v2_hw_info,
	.trans_info = trans_rk_vepu2,
	.hw_ops = &vepu_v2_hw_ops,
	.dev_ops = &vepu_v2_dev_ops,
};

static const struct of_device_id mpp_vepu2_dt_match[] = {
	{
		.compatible = "rockchip,vpu-encoder-v2",
		.data = &vepu_v2_data,
	},
	{},
};

static int vepu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vepu_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probe device\n");
	enc = devm_kzalloc(dev, sizeof(struct vepu_dev), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;

	mpp = &enc->mpp;
	platform_set_drvdata(pdev, enc);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vepu2_dt_match, pdev->dev.of_node);
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

	mpp->session_max_buffers = VEPU2_SESSION_MAX_BUFFERS;
	vepu_debugfs_init(mpp);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int vepu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vepu_dev *enc = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&enc->mpp);
	vepu_debugfs_remove(&enc->mpp);

	return 0;
}

static void vepu_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct vepu_dev *enc = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &enc->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->total_running,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_vepu2_driver = {
	.probe = vepu_probe,
	.remove = vepu_remove,
	.shutdown = vepu_shutdown,
	.driver = {
		.name = VEPU2_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vepu2_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vepu2_driver);
