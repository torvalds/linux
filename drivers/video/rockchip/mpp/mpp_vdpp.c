// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 *
 * author:
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

#define VDPP_DRIVER_NAME		"mpp_vdpp"

#define	VDPP_SESSION_MAX_BUFFERS	15
#define VDPP_REG_WORK_MODE			0x0008
#define VDPP_REG_VDPP_MODE			BIT(1)

#define to_vdpp_info(info)	\
		container_of(info, struct vdpp_hw_info, hw)
#define to_vdpp_task(task)	\
		container_of(task, struct vdpp_task, mpp_task)
#define to_vdpp_dev(dev)	\
		container_of(dev, struct vdpp_dev, mpp)

struct vdpp_hw_info {
	struct mpp_hw_info hw;

	/* register info */
	u32 start_base;
	u32 cfg_base;
	u32 work_mode_base;
	u32 gate_base;
	u32 rst_sta_base;
	u32 int_en_base;
	u32 int_clr_base;
	u32 int_sta_base; // int_sta = int_raw_sta && int_en
	u32 int_mask;
	u32 err_mask;
	/* register for zme */
	u32 zme_reg_off;
	u32 zme_reg_num;
	/* for soft reset */
	u32 bit_rst_en;
	u32 bit_rst_done;
};

struct vdpp_task {
	struct mpp_task mpp_task;
	enum MPP_CLOCK_MODE clk_mode;
	u32 *reg;
	u32 *zme_reg;

	struct reg_offset_info off_inf;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct vdpp_dev {
	struct mpp_dev mpp;
	struct vdpp_hw_info *hw_info;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info sclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_s;
	/* for zme */
	void __iomem *zme_base;
};

static struct vdpp_hw_info vdpp_v1_hw_info = {
	.hw = {
		.reg_num = 53,
		.reg_id = 21,
		.reg_en = 0,
		.reg_start = 0,
		.reg_end = 52,
	},
	.start_base = 0x0000,
	.cfg_base = 0x0004,
	.work_mode_base = 0x0008,
	.gate_base = 0x0010,
	.rst_sta_base = 0x0014,
	.int_en_base = 0x0020,
	.int_clr_base = 0x0024,
	.int_sta_base = 0x0028,
	.int_mask = 0x0073,
	.err_mask = 0x0070,
	.zme_reg_off = 0x2000,
	.zme_reg_num = 530,
	.bit_rst_en = BIT(21),
	.bit_rst_done = BIT(0),
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_vdpp[] = {
	24, 25, 26, 27,
};

#define VDPP_FMT_DEFAULT		0
static struct mpp_trans_info vdpp_v1_trans[] = {
	[VDPP_FMT_DEFAULT] = {
		.count = ARRAY_SIZE(trans_tbl_vdpp),
		.table = trans_tbl_vdpp,
	},
};

static int vdpp_process_reg_fd(struct mpp_session *session,
				 struct vdpp_task *task,
				 struct mpp_task_msgs *msgs)
{
	int ret = 0;

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					VDPP_FMT_DEFAULT, task->reg, &task->off_inf);
	if (ret)
		return ret;

	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);
	return 0;
}

static int vdpp_extract_task_msg(struct vdpp_task *task,
				   struct mpp_task_msgs *msgs)
{
	u32 i;
	int ret;
	struct mpp_request *req;
	struct vdpp_hw_info *hw_info = to_vdpp_info(task->mpp_task.hw_info);

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			int req_base;
			int max_size;
			u8 *dst = NULL;

			if (req->offset >= hw_info->zme_reg_off) {
				req_base = hw_info->zme_reg_off;
				max_size = hw_info->zme_reg_num * sizeof(u32);
				dst = (u8 *)task->zme_reg;
			} else {
				req_base = 0;
				max_size = hw_info->hw.reg_num * sizeof(u32);
				dst = (u8 *)task->reg;
			}

			ret = mpp_check_req(req, req_base, max_size, 0, max_size);
			if (ret)
				return ret;

			dst += req->offset - req_base;
			if (copy_from_user(dst, req->data, req->size)) {
				mpp_err("copy_from_user reg failed\n");
				return -EIO;
			}
			memcpy(&task->w_reqs[task->w_req_cnt++], req, sizeof(*req));
		} break;
		case MPP_CMD_SET_REG_READ: {
			int req_base;
			int max_size;

			if (req->offset >= hw_info->zme_reg_off) {
				req_base = hw_info->zme_reg_off;
				max_size = hw_info->zme_reg_num * sizeof(u32);
			} else {
				req_base = 0;
				max_size = hw_info->hw.reg_num * sizeof(u32);
			}

			ret = mpp_check_req(req, req_base, max_size, 0, max_size);
			if (ret)
				return ret;

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

static void *vdpp_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	u32 reg_num;
	struct mpp_task *mpp_task = NULL;
	struct vdpp_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;
	struct vdpp_hw_info *hw_info = to_vdpp_info(mpp->var->hw_info);

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;
	/* alloc reg buffer */
	reg_num = hw_info->hw.reg_num + hw_info->zme_reg_num;
	task->reg = kcalloc(reg_num, sizeof(u32), GFP_KERNEL);
	if (!task->reg)
		goto free_task;
	task->zme_reg = task->reg + hw_info->hw.reg_num;

	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	mpp_task->reg = task->reg;
	/* extract reqs for current task */
	ret = vdpp_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = vdpp_process_reg_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	kfree(task->reg);
free_task:
	kfree(task);
	return NULL;
}

static int vdpp_write_req_zme(void __iomem *reg_base,
			      u32 *regs,
			      u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		int reg = i * sizeof(u32);

		mpp_debug(DEBUG_SET_REG_L2, "zme_reg[%03d]: %04x: 0x%08x\n", i, reg, regs[i]);
		writel_relaxed(regs[i], reg_base + reg);
	}

	return 0;
}

static int vdpp_read_req_zme(void __iomem *reg_base,
			     u32 *regs,
			     u32 start_idx, u32 end_idx)
{
	int i;

	for (i = start_idx; i < end_idx; i++) {
		int reg = i * sizeof(u32);

		regs[i] = readl_relaxed(reg_base + reg);
		mpp_debug(DEBUG_GET_REG_L2, "zme_reg[%03d]: %04x: 0x%08x\n", i, reg, regs[i]);
	}

	return 0;
}

static int vdpp_run(struct mpp_dev *mpp,
		      struct mpp_task *mpp_task)
{
	u32 i;
	u32 reg_en;
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct vdpp_task *task = to_vdpp_task(mpp_task);
	struct vdpp_hw_info *hw_info = vdpp->hw_info;
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	reg_en = hw_info->hw.reg_en;
	for (i = 0; i < task->w_req_cnt; i++) {
		struct mpp_request *req = &task->w_reqs[i];

		if (req->offset >= hw_info->zme_reg_off) {
			/* set registers for zme */
			int off = req->offset - hw_info->zme_reg_off;
			int s = off / sizeof(u32);
			int e = s + req->size / sizeof(u32);

			if (!vdpp->zme_base)
				continue;
			vdpp_write_req_zme(vdpp->zme_base, task->zme_reg, s, e);
		} else {
			/* set registers for vdpp */
			int s = req->offset / sizeof(u32);
			int e = s + req->size / sizeof(u32);

			mpp_write_req(mpp, task->reg, s, e, reg_en);
		}
	}

	/* flush tlb before starting hardware */
	mpp_iommu_flush_tlb(mpp->iommu_info);

	/* init current task */
	mpp->cur_task = mpp_task;

	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, hw_info->start_base, task->reg[reg_en]);

	mpp_task_run_end(mpp_task, timing_en);

	mpp_debug_leave();

	return 0;
}

static int vdpp_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	u32 i;
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct vdpp_task *task = to_vdpp_task(mpp_task);
	struct vdpp_hw_info *hw_info = vdpp->hw_info;

	mpp_debug_enter();

	for (i = 0; i < task->r_req_cnt; i++) {
		struct mpp_request *req = &task->r_reqs[i];

		if (req->offset >= hw_info->zme_reg_off) {
			int off = req->offset - hw_info->zme_reg_off;
			int s = off / sizeof(u32);
			int e = s + req->size / sizeof(u32);

			if (!vdpp->zme_base)
				continue;
			vdpp_read_req_zme(vdpp->zme_base, task->zme_reg, s, e);
		} else {
			int s = req->offset / sizeof(u32);
			int e = s + req->size / sizeof(u32);

			mpp_read_req(mpp, task->reg, s, e);
		}
	}
	task->reg[hw_info->int_sta_base] = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static int vdpp_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct vdpp_task *task = to_vdpp_task(mpp_task);
	struct vdpp_hw_info *hw_info = to_vdpp_info(mpp_task->hw_info);

	for (i = 0; i < task->r_req_cnt; i++) {
		struct mpp_request *req;

		req = &task->r_reqs[i];
		/* set register L2 */
		if (req->offset >= hw_info->zme_reg_off) {
			struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
			int off = req->offset - hw_info->zme_reg_off;

			if (!vdpp->zme_base)
				continue;
			if (copy_to_user(req->data,
					 (u8 *)task->zme_reg + off,
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

	return 0;
}

static int vdpp_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct vdpp_task *task = to_vdpp_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task->reg);
	kfree(task);

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int vdpp_procfs_remove(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	if (vdpp->procfs) {
		proc_remove(vdpp->procfs);
		vdpp->procfs = NULL;
	}

	return 0;
}

static int vdpp_procfs_init(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	vdpp->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(vdpp->procfs)) {
		mpp_err("failed on open procfs\n");
		vdpp->procfs = NULL;
		return -EIO;
	}
	mpp_procfs_create_u32("aclk", 0644,
			      vdpp->procfs, &vdpp->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      vdpp->procfs, &mpp->session_max_buffers);
	return 0;
}
#else
static inline int vdpp_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int vdpp_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int vdpp_init(struct mpp_dev *mpp)
{
	int ret;
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &vdpp->aclk_info, "aclk");
	if (ret)
		mpp_err("failed on clk_get aclk\n");
	ret = mpp_get_clk_info(mpp, &vdpp->hclk_info, "hclk");
	if (ret)
		mpp_err("failed on clk_get hclk\n");
	ret = mpp_get_clk_info(mpp, &vdpp->sclk_info, "sclk");
	if (ret)
		mpp_err("failed on clk_get sclk\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&vdpp->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	vdpp->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "rst_a");
	if (!vdpp->rst_a)
		mpp_err("No aclk reset resource define\n");
	vdpp->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "rst_h");
	if (!vdpp->rst_h)
		mpp_err("No hclk reset resource define\n");
	vdpp->rst_s = mpp_reset_control_get(mpp, RST_TYPE_CORE, "rst_s");
	if (!vdpp->rst_s)
		mpp_err("No sclk reset resource define\n");

	return 0;
}

static int vdpp_clk_on(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	mpp_clk_safe_enable(vdpp->aclk_info.clk);
	mpp_clk_safe_enable(vdpp->hclk_info.clk);
	mpp_clk_safe_enable(vdpp->sclk_info.clk);

	return 0;
}

static int vdpp_clk_off(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	mpp_clk_safe_disable(vdpp->aclk_info.clk);
	mpp_clk_safe_disable(vdpp->hclk_info.clk);
	mpp_clk_safe_disable(vdpp->sclk_info.clk);

	return 0;
}

static int vdpp_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct vdpp_task *task = to_vdpp_task(mpp_task);

	mpp_clk_set_rate(&vdpp->aclk_info, task->clk_mode);

	return 0;
}

static int vdpp_reduce_freq(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);

	mpp_clk_set_rate(&vdpp->aclk_info, CLK_MODE_REDUCE);

	return 0;
}

static int vdpp_irq(struct mpp_dev *mpp)
{
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct vdpp_hw_info *hw_info = vdpp->hw_info;
	u32 work_mode = mpp_read(mpp, VDPP_REG_WORK_MODE);

	if (!(work_mode & VDPP_REG_VDPP_MODE))
		return IRQ_NONE;
	mpp->irq_status = mpp_read(mpp, hw_info->int_sta_base);
	if (!(mpp->irq_status & hw_info->int_mask))
		return IRQ_NONE;
	mpp_write(mpp, hw_info->int_en_base, 0);
	mpp_write(mpp, hw_info->int_clr_base, mpp->irq_status);

	/* ensure hardware is being off status */
	mpp_write(mpp, hw_info->start_base, 0);

	return IRQ_WAKE_THREAD;
}

static int vdpp_isr(struct mpp_dev *mpp)
{
	struct vdpp_task *task = NULL;
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_vdpp_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	if (task->irq_status & vdpp->hw_info->err_mask)
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int _vdpp_reset(struct mpp_dev *mpp, struct vdpp_dev *vdpp)
{
	if (vdpp->rst_a && vdpp->rst_h && vdpp->rst_s) {
		mpp_debug(DEBUG_RESET, "reset in\n");

		/* Don't skip this or iommu won't work after reset */
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(vdpp->rst_a);
		mpp_safe_reset(vdpp->rst_h);
		mpp_safe_reset(vdpp->rst_s);
		udelay(5);
		mpp_safe_unreset(vdpp->rst_a);
		mpp_safe_unreset(vdpp->rst_h);
		mpp_safe_unreset(vdpp->rst_s);
		mpp_pmu_idle_request(mpp, false);

		mpp_debug(DEBUG_RESET, "reset out\n");
	}

	return 0;
}

static int vdpp_reset(struct mpp_dev *mpp)
{
	int ret = 0;
	u32 rst_status = 0;
	struct vdpp_dev *vdpp = to_vdpp_dev(mpp);
	struct vdpp_hw_info *hw_info = vdpp->hw_info;

	/* soft rest first */
	mpp_write(mpp, hw_info->cfg_base, hw_info->bit_rst_en);
	ret = readl_relaxed_poll_timeout(mpp->reg_base + hw_info->rst_sta_base,
					 rst_status,
					 rst_status & hw_info->bit_rst_done,
					 0, 5);
	if (ret) {
		mpp_err("soft reset timeout, use cru reset\n");
		return _vdpp_reset(mpp, vdpp);
	}

	mpp_write(mpp, hw_info->rst_sta_base, 0);

	/* ensure hardware is being off status */
	mpp_write(mpp, hw_info->start_base, 0);

	return 0;
}

static struct mpp_hw_ops vdpp_v1_hw_ops = {
	.init = vdpp_init,
	.clk_on = vdpp_clk_on,
	.clk_off = vdpp_clk_off,
	.set_freq = vdpp_set_freq,
	.reduce_freq = vdpp_reduce_freq,
	.reset = vdpp_reset,
};

static struct mpp_dev_ops vdpp_v1_dev_ops = {
	.alloc_task = vdpp_alloc_task,
	.run = vdpp_run,
	.irq = vdpp_irq,
	.isr = vdpp_isr,
	.finish = vdpp_finish,
	.result = vdpp_result,
	.free_task = vdpp_free_task,
};

static const struct mpp_dev_var vdpp_v1_data = {
	.device_type = MPP_DEVICE_VDPP,
	.hw_info = &vdpp_v1_hw_info.hw,
	.trans_info = vdpp_v1_trans,
	.hw_ops = &vdpp_v1_hw_ops,
	.dev_ops = &vdpp_v1_dev_ops,
};

static const struct of_device_id mpp_vdpp_dt_match[] = {
	{
		.compatible = "rockchip,vdpp-v1",
		.data = &vdpp_v1_data,
	},
	{},
};

static int vdpp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vdpp_dev *vdpp = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;
	struct resource *res;

	dev_info(dev, "probe device\n");
	vdpp = devm_kzalloc(dev, sizeof(struct vdpp_dev), GFP_KERNEL);
	if (!vdpp)
		return -ENOMEM;
	platform_set_drvdata(pdev, vdpp);

	mpp = &vdpp->mpp;
	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vdpp_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
		mpp->core_id = -1;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret) {
		dev_err(dev, "probe sub driver failed\n");
		return -EINVAL;
	}
	/* map zme regs */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "zme_regs");
	if (res) {
		vdpp->zme_base = devm_ioremap(dev, res->start, resource_size(res));
		if (!vdpp->zme_base) {
			dev_err(dev, "ioremap failed for resource %pR\n", res);
			return -ENOMEM;
		}
	}
	/* get irq */
	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		return -EINVAL;
	}

	mpp->session_max_buffers = VDPP_SESSION_MAX_BUFFERS;
	vdpp->hw_info = to_vdpp_info(mpp->var->hw_info);
	vdpp_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);

	dev_info(dev, "probing finish\n");

	return 0;
}

static int vdpp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vdpp_dev *vdpp = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&vdpp->mpp);
	vdpp_procfs_remove(&vdpp->mpp);

	return 0;
}

static void vdpp_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct vdpp_dev *vdpp = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &vdpp->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_vdpp_driver = {
	.probe = vdpp_probe,
	.remove = vdpp_remove,
	.shutdown = vdpp_shutdown,
	.driver = {
		.name = VDPP_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vdpp_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vdpp_driver);
