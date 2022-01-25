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

#define JPGDEC_DRIVER_NAME		"mpp_jpgdec"

#define	JPGDEC_SESSION_MAX_BUFFERS	40
/* The maximum registers number of all the version */
#define JPGDEC_REG_NUM			42
#define JPGDEC_REG_HW_ID_INDEX		0
#define JPGDEC_REG_START_INDEX		0
#define JPGDEC_REG_END_INDEX		41

#define JPGDEC_GET_PROD_NUM(x)		(((x) >> 16) & 0xffff)
#define JPGDEC_GET_SUPPORT_BIT(x)	(((x) >> 8) & 0x1)

#define JPGDEC_REG_INT_EN_BASE		0x004
#define JPGDEC_REG_INT_EN_INDEX		(1)

#define JPGDEC_CARE_STREAM_ERROR_EN	BIT(16)
#define JPGDEC_EMPTY_FORCE_END		BIT(15)
#define JPGDEC_SOFT_RSET_READY		BIT(14)
#define JPGDEC_BUF_EMPTY_STA		BIT(13)
#define JPGDEC_TIMEOUT_STA		BIT(12)
#define JPGDEC_ERROR_STA		BIT(11)
#define JPGDEC_BUS_STA			BIT(10)
#define JPGDEC_REDAY_STA		BIT(9)
#define JPGDEC_IRQ			BIT(8)
#define JPGDEC_WAIT_RESET_EN		BIT(7)
#define JPGDEC_IRQ_RAW			BIT(6)
#define JPGDEC_SOFT_REST_EN		BIT(5)
#define JPGDEC_BUF_EMPTY_RELOAD_EN	BIT(4)
#define JPGDEC_BUF_EMPTY_EN		BIT(3)
#define JPGDEC_TIMEOUT_EN		BIT(2)
#define JPGDEC_IRQ_DIS			BIT(1)
#define JPGDEC_START_EN			BIT(0)

#define JPGDEC_REG_SYS_BASE		0x008
#define JPGDEC_FORCE_SOFTRESET_VALID	BIT(17)

#define JPGDEC_REG_PIC_INFO_BASE	0x00c
#define JPGDEC_REG_PIC_INFO_INDEX	(3)
#define JPGDEC_GET_WIDTH(x)		(((x) & 0xffff) + 1)
#define JPGDEC_GET_HEIGHT(x)		((((x) >> 16) & 0xffff) + 1)

#define JPGDEC_REG_STREAM_RLC_BASE		0x030
#define JPGDEC_REG_STREAM_RLC_BASE_INDEX	(12)

#define to_jpgdec_task(task)	\
		container_of(task, struct jpgdec_task, mpp_task)
#define to_jpgdec_dev(dev)	\
		container_of(dev, struct jpgdec_dev, mpp)

struct jpgdec_task {
	struct mpp_task mpp_task;
	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[JPGDEC_REG_NUM];

	struct reg_offset_info off_inf;
	u32 strm_addr;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct jpgdec_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
};

static struct mpp_hw_info jpgdec_v1_hw_info = {
	.reg_num = JPGDEC_REG_NUM,
	.reg_id = JPGDEC_REG_HW_ID_INDEX,
	.reg_start = JPGDEC_REG_START_INDEX,
	.reg_end = JPGDEC_REG_END_INDEX,
	.reg_en = JPGDEC_REG_INT_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_jpgdec[] = {
	9, 10, 11, 12, 13,
};

#define JPEGDEC_FMT_DEFAULT		0
static struct mpp_trans_info jpgdec_v1_trans[] = {
	[JPEGDEC_FMT_DEFAULT] = {
		.count = ARRAY_SIZE(trans_tbl_jpgdec),
		.table = trans_tbl_jpgdec,
	},
};

static int jpgdec_process_reg_fd(struct mpp_session *session,
				 struct jpgdec_task *task,
				 struct mpp_task_msgs *msgs)
{
	int ret = 0;

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					JPEGDEC_FMT_DEFAULT, task->reg, &task->off_inf);
	if (ret)
		return ret;

	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);
	return 0;
}

static int jpgdec_extract_task_msg(struct jpgdec_task *task,
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

static void *jpgdec_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct jpgdec_task *task = NULL;
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
	ret = jpgdec_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = jpgdec_process_reg_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	task->strm_addr = task->reg[JPGDEC_REG_STREAM_RLC_BASE_INDEX];
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

static int jpgdec_soft_reset(struct mpp_dev *mpp)
{
	mpp_write(mpp, JPGDEC_REG_SYS_BASE, JPGDEC_FORCE_SOFTRESET_VALID);
	mpp_write(mpp, JPGDEC_REG_INT_EN_BASE, JPGDEC_SOFT_REST_EN);

	return 0;
}

static int jpgdec_run(struct mpp_dev *mpp,
		      struct mpp_task *mpp_task)
{
	u32 i;
	u32 reg_en;
	struct jpgdec_task *task = to_jpgdec_task(mpp_task);

	mpp_debug_enter();

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
	mpp_write(mpp, JPGDEC_REG_INT_EN_BASE,
		  task->reg[reg_en] | JPGDEC_START_EN);

	mpp_debug_leave();

	return 0;
}

static int jpgdec_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	u32 i;
	u32 s, e;
	u32 dec_get;
	s32 dec_length;
	struct mpp_request *req;
	struct jpgdec_task *task = to_jpgdec_task(mpp_task);

	mpp_debug_enter();

	/* read register after running */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];
		s = req->offset / sizeof(u32);
		e = s + req->size / sizeof(u32);
		mpp_read_req(mpp, task->reg, s, e);
	}
	/* revert hack for irq status */
	task->reg[JPGDEC_REG_INT_EN_INDEX] = task->irq_status;
	/* revert hack for decoded length */
	dec_get = mpp_read_relaxed(mpp, JPGDEC_REG_STREAM_RLC_BASE);
	dec_length = dec_get - task->strm_addr;
	task->reg[JPGDEC_REG_STREAM_RLC_BASE_INDEX] = dec_length << 10;
	/*
	 * If the softrest_rdy bit is low,
	 * it means that the soft-reset of the previous frame
	 * has not been completed.We have to manually trigger to do soft-reset.
	 */
	if (!(task->irq_status & JPGDEC_SOFT_RSET_READY) &&
	    !atomic_read(&mpp->reset_request))
		jpgdec_soft_reset(mpp);

	mpp_debug(DEBUG_REGISTER,
		  "dec_get %08x dec_length %d\n", dec_get, dec_length);

	mpp_debug_leave();

	return 0;
}

static int jpgdec_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct jpgdec_task *task = to_jpgdec_task(mpp_task);

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

static int jpgdec_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct jpgdec_task *task = to_jpgdec_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int jpgdec_procfs_remove(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int jpgdec_procfs_init(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

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
static inline int jpgdec_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int jpgdec_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int jpgdec_init(struct mpp_dev *mpp)
{
	int ret;
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

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

static int jpgdec_clk_on(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);

	return 0;
}

static int jpgdec_clk_off(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

	mpp_clk_safe_disable(dec->aclk_info.clk);
	mpp_clk_safe_disable(dec->hclk_info.clk);

	return 0;
}

static int jpgdec_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);
	struct jpgdec_task *task = to_jpgdec_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);

	return 0;
}

static int jpgdec_reduce_freq(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

	mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_REDUCE);

	return 0;
}

static int jpgdec_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, JPGDEC_REG_INT_EN_BASE);
	if (!(mpp->irq_status & JPGDEC_IRQ_RAW))
		return IRQ_NONE;
	mpp_write(mpp, JPGDEC_REG_INT_EN_BASE, 0);

	return IRQ_WAKE_THREAD;
}

static int jpgdec_isr(struct mpp_dev *mpp)
{
	int error_mask;
	struct jpgdec_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_jpgdec_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	error_mask = JPGDEC_BUS_STA | JPGDEC_ERROR_STA |
		     JPGDEC_TIMEOUT_STA | JPGDEC_BUF_EMPTY_STA;

	if (error_mask & task->irq_status)
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int jpgdec_reset(struct mpp_dev *mpp)
{
	struct jpgdec_dev *dec = to_jpgdec_dev(mpp);

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
	mpp_write(mpp, JPGDEC_REG_INT_EN_BASE, 0);

	return 0;
}

static struct mpp_hw_ops jpgdec_v1_hw_ops = {
	.init = jpgdec_init,
	.clk_on = jpgdec_clk_on,
	.clk_off = jpgdec_clk_off,
	.set_freq = jpgdec_set_freq,
	.reduce_freq = jpgdec_reduce_freq,
	.reset = jpgdec_reset,
};

static struct mpp_dev_ops jpgdec_v1_dev_ops = {
	.alloc_task = jpgdec_alloc_task,
	.run = jpgdec_run,
	.irq = jpgdec_irq,
	.isr = jpgdec_isr,
	.finish = jpgdec_finish,
	.result = jpgdec_result,
	.free_task = jpgdec_free_task,
};

static const struct mpp_dev_var jpgdec_v1_data = {
	.device_type = MPP_DEVICE_JPGDEC,
	.hw_info = &jpgdec_v1_hw_info,
	.trans_info = jpgdec_v1_trans,
	.hw_ops = &jpgdec_v1_hw_ops,
	.dev_ops = &jpgdec_v1_dev_ops,
};

static const struct of_device_id mpp_jpgdec_dt_match[] = {
	{
		.compatible = "rockchip,rkv-jpeg-decoder-v1",
		.data = &jpgdec_v1_data,
	},
	{},
};

static int jpgdec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jpgdec_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probe device\n");
	dec = devm_kzalloc(dev, sizeof(struct jpgdec_dev), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;
	mpp = &dec->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_jpgdec_dt_match, pdev->dev.of_node);
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

	mpp->session_max_buffers = JPGDEC_SESSION_MAX_BUFFERS;
	jpgdec_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int jpgdec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = dev_get_drvdata(dev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(mpp);
	jpgdec_procfs_remove(mpp);

	return 0;
}

struct platform_driver rockchip_jpgdec_driver = {
	.probe = jpgdec_probe,
	.remove = jpgdec_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = JPGDEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_jpgdec_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_jpgdec_driver);
