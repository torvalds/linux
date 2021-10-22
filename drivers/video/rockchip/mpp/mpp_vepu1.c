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
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/proc_fs.h>
#include <linux/nospec.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define VEPU1_DRIVER_NAME		"mpp_vepu1"

#define	VEPU1_SESSION_MAX_BUFFERS	20
/* The maximum registers number of all the version */
#define VEPU1_REG_NUM			164
#define VEPU1_REG_HW_ID_INDEX		0
#define VEPU1_REG_START_INDEX		0
#define VEPU1_REG_END_INDEX		163

#define VEPU1_REG_INT			0x004
#define VEPU1_REG_INT_INDEX		(1)
#define VEPU1_INT_SLICE			BIT(8)
#define VEPU1_INT_TIMEOUT		BIT(6)
#define VEPU1_INT_BUF_FULL		BIT(5)
#define VEPU1_INT_RESET			BIT(4)
#define VEPU1_INT_BUS_ERROR		BIT(3)
#define VEPU1_INT_RDY			BIT(2)
#define VEPU1_IRQ_DIS			BIT(1)
#define VEPU1_INT_RAW			BIT(0)

#define VEPU1_REG_ENC_EN		0x038
#define VEPU1_REG_ENC_EN_INDEX		(14)
#define VEPU1_INT_TIMEOUT_EN		BIT(31)
#define VEPU1_INT_SLICE_EN		BIT(28)
#define VEPU1_ENC_START			BIT(0)

#define VEPU1_GET_FORMAT(x)		(((x) >> 1) & 0x3)
#define VEPU1_FORMAT_MASK		(0x06)

#define VEPU1_FMT_RESERVED		(0)
#define VEPU1_FMT_VP8E			(1)
#define VEPU1_FMT_JPEGE			(2)
#define VEPU1_FMT_H264E			(3)

#define VEPU1_REG_CLR_CACHE_BASE	0xc10

#define to_vepu_task(task)		\
		container_of(task, struct vepu_task, mpp_task)
#define to_vepu_dev(dev)		\
		container_of(dev, struct vepu_dev, mpp)

struct vepu_task {
	struct mpp_task mpp_task;

	enum MPP_CLOCK_MODE clk_mode;
	u32 reg[VEPU1_REG_NUM];

	struct reg_offset_info off_inf;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct vepu_session_priv {
	struct rw_semaphore rw_sem;
	/* codec info from user */
	struct {
		/* show mode */
		u32 flag;
		/* item data */
		u64 val;
	} codec_info[ENC_INFO_BUTT];
};

struct vepu_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
};

static struct mpp_hw_info vepu_v1_hw_info = {
	.reg_num = VEPU1_REG_NUM,
	.reg_id = VEPU1_REG_HW_ID_INDEX,
	.reg_start = VEPU1_REG_START_INDEX,
	.reg_end = VEPU1_REG_END_INDEX,
	.reg_en = VEPU1_REG_ENC_EN_INDEX,
};

/*
 * file handle translate information
 */
static const u16 trans_tbl_default[] = {
	5, 6, 7, 8, 9, 10, 11, 12, 13, 51
};

static const u16 trans_tbl_vp8e[] = {
	5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 26, 51, 52, 58, 59, 71
};

static struct mpp_trans_info trans_rk_vepu1[] = {
	[VEPU1_FMT_RESERVED] = {
		.count = 0,
		.table = NULL,
	},
	[VEPU1_FMT_VP8E] = {
		.count = ARRAY_SIZE(trans_tbl_vp8e),
		.table = trans_tbl_vp8e,
	},
	[VEPU1_FMT_JPEGE] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
	[VEPU1_FMT_H264E] = {
		.count = ARRAY_SIZE(trans_tbl_default),
		.table = trans_tbl_default,
	},
};

static int vepu_process_reg_fd(struct mpp_session *session,
			       struct vepu_task *task,
			       struct mpp_task_msgs *msgs)
{
	int ret = 0;
	int fmt = VEPU1_GET_FORMAT(task->reg[VEPU1_REG_ENC_EN_INDEX]);

	ret = mpp_translate_reg_address(session, &task->mpp_task,
					fmt, task->reg, &task->off_inf);
	if (ret)
		return ret;

	mpp_translate_reg_offset_info(&task->mpp_task,
				      &task->off_inf, task->reg);

	return 0;
}

static int vepu_extract_task_msg(struct vepu_task *task,
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

static void *vepu_alloc_task(struct mpp_session *session,
			     struct mpp_task_msgs *msgs)
{
	int ret;
	struct mpp_task *mpp_task = NULL;
	struct vepu_task *task = NULL;
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
	ret = vepu_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = vepu_process_reg_fd(session, task, msgs);
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
	kfree(task);
	return NULL;
}

static int vepu_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	u32 i;
	u32 reg_en;
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_debug_enter();

	/* clear cache */
	mpp_write_relaxed(mpp, VEPU1_REG_CLR_CACHE_BASE, 1);
	/* set registers for hardware */
	reg_en = mpp_task->hw_info->reg_en;
	/* First, flush correct encoder format */
	mpp_write_relaxed(mpp, VEPU1_REG_ENC_EN,
			  task->reg[reg_en] & VEPU1_FORMAT_MASK);
	/* Second, flush others register */
	for (i = 0; i < task->w_req_cnt; i++) {
		struct mpp_request *req = &task->w_reqs[i];
		int s = req->offset / sizeof(u32);
		int e = s + req->size / sizeof(u32);

		mpp_write_req(mpp, task->reg, s, e, reg_en);
	}
	/* init current task */
	mpp->cur_task = mpp_task;
	/* Last, flush start registers */
	wmb();
	mpp_write(mpp, VEPU1_REG_ENC_EN,
		  task->reg[reg_en] | VEPU1_ENC_START);

	mpp_debug_leave();

	return 0;
}

static int vepu_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, VEPU1_REG_INT);
	if (!(mpp->irq_status & VEPU1_INT_RAW))
		return IRQ_NONE;

	mpp_write(mpp, VEPU1_REG_INT, 0);

	return IRQ_WAKE_THREAD;
}

static int vepu_isr(struct mpp_dev *mpp)
{
	u32 err_mask;
	struct vepu_task *task = NULL;
	struct mpp_task *mpp_task = mpp->cur_task;

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_vepu_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	err_mask = VEPU1_INT_TIMEOUT
		| VEPU1_INT_BUF_FULL
		| VEPU1_INT_BUS_ERROR;

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
	u32 s, e;
	struct mpp_request *req;
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_debug_enter();

	/* read register after running */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];
		s = req->offset / sizeof(u32);
		e = s + req->size / sizeof(u32);
		mpp_read_req(mpp, task->reg, s, e);
	}
	/* revert hack for irq status */
	task->reg[VEPU1_REG_INT_INDEX] = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static int vepu_result(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task,
		       struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct vepu_task *task = to_vepu_task(mpp_task);

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

static int vepu_free_task(struct mpp_session *session,
			  struct mpp_task *mpp_task)
{
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

static int vepu_control(struct mpp_session *session, struct mpp_request *req)
{
	switch (req->cmd) {
	case MPP_CMD_SEND_CODEC_INFO: {
		int i;
		int cnt;
		struct codec_info_elem elem;
		struct vepu_session_priv *priv;

		if (!session || !session->priv) {
			mpp_err("session info null\n");
			return -EINVAL;
		}
		priv = session->priv;

		cnt = req->size / sizeof(elem);
		cnt = (cnt > ENC_INFO_BUTT) ? ENC_INFO_BUTT : cnt;
		mpp_debug(DEBUG_IOCTL, "codec info count %d\n", cnt);
		down_write(&priv->rw_sem);
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
		up_write(&priv->rw_sem);
	} break;
	default: {
		mpp_err("unknown mpp ioctl cmd %x\n", req->cmd);
	} break;
	}

	return 0;
}

static int vepu_free_session(struct mpp_session *session)
{
	if (session && session->priv) {
		kfree(session->priv);
		session->priv = NULL;
	}

	return 0;
}

static int vepu_init_session(struct mpp_session *session)
{
	struct vepu_session_priv *priv;

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
static int vepu_procfs_remove(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	if (enc->procfs) {
		proc_remove(enc->procfs);
		enc->procfs = NULL;
	}

	return 0;
}

static int vepu_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	int i;
	struct vepu_session_priv *priv = session->priv;

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
	seq_printf(seq, "|%8p|", session);
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

static int vepu_show_session_info(struct seq_file *seq, void *offset)
{
	struct mpp_session *session = NULL, *n;
	struct mpp_dev *mpp = seq->private;

	mutex_lock(&mpp->srv->session_lock);
	list_for_each_entry_safe(session, n,
				 &mpp->srv->session_list,
				 session_link) {
		if (session->device_type != MPP_DEVICE_VEPU1)
			continue;
		if (!session->priv)
			continue;
		if (mpp->dev_ops->dump_session)
			mpp->dev_ops->dump_session(session, seq);
	}
	mutex_unlock(&mpp->srv->session_lock);

	return 0;
}

static int vepu_procfs_init(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	enc->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(enc->procfs)) {
		mpp_err("failed on open procfs\n");
		enc->procfs = NULL;
		return -EIO;
	}
	mpp_procfs_create_u32("aclk", 0644,
			      enc->procfs, &enc->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      enc->procfs, &mpp->session_max_buffers);
	/* for show session info */
	proc_create_single_data("sessions-info", 0444,
				enc->procfs, vepu_show_session_info, mpp);

	return 0;
}
#else
static inline int vepu_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int vepu_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}

static inline int vepu_dump_session(struct mpp_session *session, struct seq_file *seq)
{
	return 0;
}
#endif

static int vepu_init(struct mpp_dev *mpp)
{
	int ret;
	struct vepu_dev *enc = to_vepu_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_VEPU1];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &enc->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &enc->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&enc->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	/* Get reset control from dtsi */
	enc->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "video_a");
	if (!enc->rst_a)
		mpp_err("No aclk reset resource define\n");
	enc->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "video_h");
	if (!enc->rst_h)
		mpp_err("No hclk reset resource define\n");

	return 0;
}

static int vepu_clk_on(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	mpp_clk_safe_enable(enc->aclk_info.clk);
	mpp_clk_safe_enable(enc->hclk_info.clk);

	return 0;
}

static int vepu_clk_off(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	mpp_clk_safe_disable(enc->aclk_info.clk);
	mpp_clk_safe_disable(enc->hclk_info.clk);

	return 0;
}

static int vepu_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);
	struct vepu_task *task = to_vepu_task(mpp_task);

	mpp_clk_set_rate(&enc->aclk_info, task->clk_mode);

	return 0;
}

static int vepu_reduce_freq(struct mpp_dev *mpp)
{
	struct vepu_dev *enc = to_vepu_dev(mpp);

	mpp_clk_set_rate(&enc->aclk_info, CLK_MODE_REDUCE);

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
	mpp_write(mpp, VEPU1_REG_ENC_EN, 0);

	return 0;
}

static struct mpp_hw_ops vepu_v1_hw_ops = {
	.init = vepu_init,
	.clk_on = vepu_clk_on,
	.clk_off = vepu_clk_off,
	.set_freq = vepu_set_freq,
	.reduce_freq = vepu_reduce_freq,
	.reset = vepu_reset,
};

static struct mpp_dev_ops vepu_v1_dev_ops = {
	.alloc_task = vepu_alloc_task,
	.run = vepu_run,
	.irq = vepu_irq,
	.isr = vepu_isr,
	.finish = vepu_finish,
	.result = vepu_result,
	.free_task = vepu_free_task,
	.ioctl = vepu_control,
	.init_session = vepu_init_session,
	.free_session = vepu_free_session,
	.dump_session = vepu_dump_session,
};

static const struct mpp_dev_var vepu_v1_data = {
	.device_type = MPP_DEVICE_VEPU1,
	.hw_info = &vepu_v1_hw_info,
	.trans_info = trans_rk_vepu1,
	.hw_ops = &vepu_v1_hw_ops,
	.dev_ops = &vepu_v1_dev_ops,
};

static const struct of_device_id mpp_vepu1_dt_match[] = {
	{
		.compatible = "rockchip,vpu-encoder-v1",
		.data = &vepu_v1_data,
	},
	{},
};

static int vepu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct vepu_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

	dev_info(dev, "probe device\n");
	enc = devm_kzalloc(dev, sizeof(struct vepu_dev), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;

	mpp = &enc->mpp;
	platform_set_drvdata(pdev, enc);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_vepu1_dt_match, pdev->dev.of_node);
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

	mpp->session_max_buffers = VEPU1_SESSION_MAX_BUFFERS;
	vepu_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int vepu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vepu_dev *enc = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&enc->mpp);
	vepu_procfs_remove(&enc->mpp);

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
				 &mpp->task_count,
				 val, val == 0, 20000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");
}

struct platform_driver rockchip_vepu1_driver = {
	.probe = vepu_probe,
	.remove = vepu_remove,
	.shutdown = vepu_shutdown,
	.driver = {
		.name = VEPU1_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_vepu1_dt_match),
	},
};
EXPORT_SYMBOL(rockchip_vepu1_driver);
