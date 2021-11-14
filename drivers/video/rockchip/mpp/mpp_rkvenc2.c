// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 *
 * author:
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
#include <linux/pm_runtime.h>
#include <linux/nospec.h>
#include <linux/workqueue.h>
#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_ipa.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

#include "mpp_debug.h"
#include "mpp_iommu.h"
#include "mpp_common.h"

#define RKVENC_DRIVER_NAME			"mpp_rkvenc2"

#define	RKVENC_SESSION_MAX_BUFFERS		40
#define RKVENC_MAX_CORE_NUM			4

#define to_rkvenc_info(info)		\
		container_of(info, struct rkvenc_hw_info, hw)
#define to_rkvenc_task(ctx)		\
		container_of(ctx, struct rkvenc_task, mpp_task)
#define to_rkvenc_dev(dev)		\
		container_of(dev, struct rkvenc_dev, mpp)


enum RKVENC_FORMAT_TYPE {
	RKVENC_FMT_BASE		= 0x0000,
	RKVENC_FMT_H264E	= RKVENC_FMT_BASE + 0,
	RKVENC_FMT_H265E	= RKVENC_FMT_BASE + 1,

	RKVENC_FMT_OSD_BASE	= 0x1000,
	RKVENC_FMT_H264E_OSD	= RKVENC_FMT_OSD_BASE + 0,
	RKVENC_FMT_H265E_OSD	= RKVENC_FMT_OSD_BASE + 1,
	RKVENC_FMT_BUTT,
};

enum RKVENC_CLASS_TYPE {
	RKVENC_CLASS_BASE	= 0,	/* base */
	RKVENC_CLASS_PIC	= 1,	/* picture configure */
	RKVENC_CLASS_RC		= 2,	/* rate control */
	RKVENC_CLASS_PAR	= 3,	/* parameter */
	RKVENC_CLASS_SQI	= 4,	/* subjective Adjust */
	RKVENC_CLASS_SCL	= 5,	/* scaling list */
	RKVENC_CLASS_OSD	= 6,	/* osd */
	RKVENC_CLASS_ST		= 7,	/* status */
	RKVENC_CLASS_DEBUG	= 8,	/* debug */
	RKVENC_CLASS_BUTT,
};

enum RKVENC_CLASS_FD_TYPE {
	RKVENC_CLASS_FD_BASE	= 0,	/* base */
	RKVENC_CLASS_FD_OSD	= 1,	/* osd */
	RKVENC_CLASS_FD_BUTT,
};

struct rkvenc_reg_msg {
	u32 base_s;
	u32 base_e;
};

struct rkvenc_hw_info {
	struct mpp_hw_info hw;
	/* for register range check */
	u32 reg_class;
	struct rkvenc_reg_msg reg_msg[RKVENC_CLASS_BUTT];
	/* for fd translate */
	u32 fd_class;
	struct {
		u32 class;
		u32 base_fmt;
	} fd_reg[RKVENC_CLASS_FD_BUTT];
	/* for get format */
	struct {
		u32 class;
		u32 base;
		u32 bitpos;
		u32 bitlen;
	} fmt_reg;
	/* register info */
	u32 enc_start_base;
	u32 enc_clr_base;
	u32 int_en_base;
	u32 int_mask_base;
	u32 int_clr_base;
	u32 int_sta_base;
	u32 enc_wdg_base;
	u32 err_mask;
};

struct rkvenc_task {
	struct mpp_task mpp_task;
	int fmt;
	struct rkvenc_hw_info *hw_info;

	/* class register */
	struct {
		u32 valid;
		u32 *data;
		u32 size;
	} reg[RKVENC_CLASS_BUTT];
	/* register offset info */
	struct reg_offset_info off_inf;

	enum MPP_CLOCK_MODE clk_mode;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
	struct mpp_dma_buffer *table;
	u32 task_no;
};

struct rkvenc_dev {
	struct mpp_dev mpp;
	struct rkvenc_hw_info *hw_info;

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
	/* for ccu */
	struct rkvenc_ccu *ccu;
	struct list_head core_link;
	u32 disable_work;
};

struct rkvenc_ccu {
	u32 core_num;
	/* lock for core attach */
	struct mutex lock;
	struct list_head core_list;
	struct mpp_dev *main_core;
};

static struct rkvenc_hw_info rkvenc_v2_hw_info = {
	.hw = {
		.reg_num = 254,
		.reg_id = 0,
		.reg_en = 4,
		.reg_start = 160,
		.reg_end = 253,
	},
	.reg_class = RKVENC_CLASS_BUTT,
	.reg_msg[RKVENC_CLASS_BASE] = {
		.base_s = 0x0000,
		.base_e = 0x0058,
	},
	.reg_msg[RKVENC_CLASS_PIC] = {
		.base_s = 0x0280,
		.base_e = 0x03f4,
	},
	.reg_msg[RKVENC_CLASS_RC] = {
		.base_s = 0x1000,
		.base_e = 0x10e0,
	},
	.reg_msg[RKVENC_CLASS_PAR] = {
		.base_s = 0x1700,
		.base_e = 0x1cd4,
	},
	.reg_msg[RKVENC_CLASS_SQI] = {
		.base_s = 0x2000,
		.base_e = 0x2198,
	},
	.reg_msg[RKVENC_CLASS_SCL] = {
		.base_s = 0x2200,
		.base_e = 0x2c98,
	},
	.reg_msg[RKVENC_CLASS_OSD] = {
		.base_s = 0x3000,
		.base_e = 0x347c,
	},
	.reg_msg[RKVENC_CLASS_ST] = {
		.base_s = 0x4000,
		.base_e = 0x42cc,
	},
	.reg_msg[RKVENC_CLASS_DEBUG] = {
		.base_s = 0x5000,
		.base_e = 0x5354,
	},
	.fd_class = RKVENC_CLASS_FD_BUTT,
	.fd_reg[RKVENC_CLASS_FD_BASE] = {
		.class = RKVENC_CLASS_PIC,
		.base_fmt = RKVENC_FMT_BASE,
	},
	.fd_reg[RKVENC_CLASS_FD_OSD] = {
		.class = RKVENC_CLASS_OSD,
		.base_fmt = RKVENC_FMT_OSD_BASE,
	},
	.fmt_reg = {
		.class = RKVENC_CLASS_PIC,
		.base = 0x0300,
		.bitpos = 0,
		.bitlen = 1,
	},
	.enc_start_base = 0x0010,
	.enc_clr_base = 0x0014,
	.int_en_base = 0x0020,
	.int_mask_base = 0x0024,
	.int_clr_base = 0x0028,
	.int_sta_base = 0x002c,
	.enc_wdg_base = 0x0038,
	.err_mask = 0x03f0,
};

/*
 * file handle translate information for v2
 */
static const u16 trans_tbl_h264e_v2[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23,
};

static const u16 trans_tbl_h264e_v2_osd[] = {
	20, 21, 22, 23, 24, 25, 26, 27,
};

static const u16 trans_tbl_h265e_v2[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23,
};

static const u16 trans_tbl_h265e_v2_osd[] = {
	20, 21, 22, 23, 24, 25, 26, 27,
};

static struct mpp_trans_info trans_rkvenc_v2[] = {
	[RKVENC_FMT_H264E] = {
		.count = ARRAY_SIZE(trans_tbl_h264e_v2),
		.table = trans_tbl_h264e_v2,
	},
	[RKVENC_FMT_H264E_OSD] = {
		.count = ARRAY_SIZE(trans_tbl_h264e_v2_osd),
		.table = trans_tbl_h264e_v2_osd,
	},
	[RKVENC_FMT_H265E] = {
		.count = ARRAY_SIZE(trans_tbl_h265e_v2),
		.table = trans_tbl_h265e_v2,
	},
	[RKVENC_FMT_H265E_OSD] = {
		.count = ARRAY_SIZE(trans_tbl_h265e_v2_osd),
		.table = trans_tbl_h265e_v2_osd,
	},
};

static bool req_over_class(struct mpp_request *req,
			   struct rkvenc_task *task, int class)
{
	bool ret;
	u32 base_s, base_e, req_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	base_s = hw->reg_msg[class].base_s;
	base_e = hw->reg_msg[class].base_e;
	req_e = req->offset + req->size - sizeof(u32);

	ret = (req->offset <= base_e && req_e >= base_s) ? true : false;

	return ret;
}

static int rkvenc_free_class_msg(struct rkvenc_task *task)
{
	u32 i;
	u32 reg_class = task->hw_info->reg_class;

	for (i = 0; i < reg_class; i++) {
		kfree(task->reg[i].data);
		task->reg[i].size = 0;
	}

	return 0;
}

static int rkvenc_alloc_class_msg(struct rkvenc_task *task, int class)
{
	u32 *data;
	struct rkvenc_hw_info *hw = task->hw_info;

	if (!task->reg[class].data) {
		u32 base_s = hw->reg_msg[class].base_s;
		u32 base_e = hw->reg_msg[class].base_e;
		u32 class_size = base_e - base_s + sizeof(u32);

		data = kzalloc(class_size, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		task->reg[class].data = data;
		task->reg[class].size = class_size;
	}

	return 0;
}

static int rkvenc_update_req(struct rkvenc_task *task, int class,
			     struct mpp_request *req_in,
			     struct mpp_request *req_out)
{
	u32 base_s, base_e, req_e, s, e;
	struct rkvenc_hw_info *hw = task->hw_info;

	base_s = hw->reg_msg[class].base_s;
	base_e = hw->reg_msg[class].base_e;
	req_e = req_in->offset + req_in->size - sizeof(u32);
	s = max(req_in->offset, base_s);
	e = min(req_e, base_e);

	req_out->offset = s;
	req_out->size = e - s + sizeof(u32);
	req_out->data = (u8 *)req_in->data + (s - req_in->offset);

	return 0;
}

static int rkvenc_get_class_msg(struct rkvenc_task *task,
				u32 addr, struct mpp_request *msg)
{
	int i;
	bool found = false;
	u32 base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	if (!msg)
		return -EINVAL;

	memset(msg, 0, sizeof(*msg));
	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			found = true;
			msg->offset = base_s;
			msg->size = task->reg[i].size;
			msg->data = task->reg[i].data;
			break;
		}
	}

	return (found ? 0 : (-EINVAL));
}

static u32 *rkvenc_get_class_reg(struct rkvenc_task *task, u32 addr)
{
	int i;
	u8 *reg = NULL;
	u32 base_s, base_e;
	struct rkvenc_hw_info *hw = task->hw_info;

	for (i = 0; i < hw->reg_class; i++) {
		base_s = hw->reg_msg[i].base_s;
		base_e = hw->reg_msg[i].base_e;
		if (addr >= base_s && addr < base_e) {
			reg = (u8 *)task->reg[i].data + (addr - base_s);
			break;
		}
	}

	return (u32 *)reg;
}

static int rkvenc_extract_task_msg(struct rkvenc_task *task,
				   struct mpp_task_msgs *msgs)
{
	int ret;
	u32 i, j;
	struct mpp_request *req;
	struct rkvenc_hw_info *hw = task->hw_info;

	mpp_debug_enter();

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			void *data;
			struct mpp_request *wreq;

			for (j = 0; j < hw->reg_class; j++) {
				if (!req_over_class(req, task, j))
					continue;

				ret = rkvenc_alloc_class_msg(task, j);
				if (ret) {
					mpp_err("alloc class msg %d fail.\n", j);
					goto fail;
				}
				wreq = &task->w_reqs[task->w_req_cnt];
				rkvenc_update_req(task, j, req, wreq);
				data = rkvenc_get_class_reg(task, wreq->offset);
				if (!data)
					goto fail;
				if (copy_from_user(data, wreq->data, wreq->size)) {
					mpp_err("copy_from_user fail, offset %08x\n", wreq->offset);
					ret = -EIO;
					goto fail;
				}
				task->reg[j].valid = 1;
				task->w_req_cnt++;
			}
		} break;
		case MPP_CMD_SET_REG_READ: {
			struct mpp_request *rreq;

			for (j = 0; j < hw->reg_class; j++) {
				if (!req_over_class(req, task, j))
					continue;

				ret = rkvenc_alloc_class_msg(task, j);
				if (ret) {
					mpp_err("alloc class msg reg %d fail.\n", j);
					goto fail;
				}
				rreq = &task->r_reqs[task->r_req_cnt];
				rkvenc_update_req(task, j, req, rreq);
				task->reg[j].valid = 1;
				task->r_req_cnt++;
			}
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

	mpp_debug_enter();
	return 0;

fail:
	rkvenc_free_class_msg(task);

	mpp_debug_enter();
	return ret;
}

static int rkvenc_task_get_format(struct mpp_dev *mpp,
				  struct rkvenc_task *task)
{
	u32 offset, val;

	struct rkvenc_hw_info *hw = task->hw_info;
	u32 class = hw->fmt_reg.class;
	u32 *class_reg = task->reg[class].data;
	u32 class_size = task->reg[class].size;
	u32 class_base = hw->reg_msg[class].base_s;
	u32 bitpos = hw->fmt_reg.bitpos;
	u32 bitlen = hw->fmt_reg.bitlen;

	if (!class_reg || !class_size)
		return -EINVAL;

	offset = hw->fmt_reg.base - class_base;
	val = class_reg[offset/sizeof(u32)];
	task->fmt = (val >> bitpos) & ((1 << bitlen) - 1);

	return 0;
}

static struct rkvenc_dev *rkvenc_core_balance(struct rkvenc_ccu *ccu)
{
	struct rkvenc_dev *enc;
	struct rkvenc_dev *core = NULL, *n;

	mpp_debug_enter();

	mutex_lock(&ccu->lock);
	enc = list_first_entry(&ccu->core_list, struct rkvenc_dev, core_link);
	list_for_each_entry_safe(core, n, &ccu->core_list, core_link) {
		mpp_debug(DEBUG_DEVICE, "%s, disable_work=%d, task_count=%d, task_index=%d\n",
			  dev_name(core->mpp.dev), core->disable_work,
			  atomic_read(&core->mpp.task_count), atomic_read(&core->mpp.task_index));
		/* if core (except main-core) disabled, skip it */
		if (core->disable_work)
			continue;
		/* choose core with less task in queue */
		if (atomic_read(&core->mpp.task_count) < atomic_read(&enc->mpp.task_count)) {
			enc = core;
			break;
		}
		/* choose core with less task which done */
		if (atomic_read(&core->mpp.task_index) < atomic_read(&enc->mpp.task_index))
			enc = core;
	}
	mutex_unlock(&ccu->lock);

	mpp_debug_leave();

	return enc;
}

static void *rkvenc_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	struct rkvenc_task *task;
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	task->hw_info = to_rkvenc_info(mpp_task->hw_info);
	/* extract reqs for current task */
	ret = rkvenc_extract_task_msg(task, msgs);
	if (ret)
		goto free_task;
	mpp_task->reg = task->reg[0].data;
	/* get format */
	ret = rkvenc_task_get_format(mpp, task);
	if (ret)
		goto free_task;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		u32 i, j;
		int cnt;
		u32 off;
		const u16 *tbl;
		struct rkvenc_hw_info *hw = task->hw_info;

		for (i = 0; i < hw->fd_class; i++) {
			u32 class = hw->fd_reg[i].class;
			u32 fmt = hw->fd_reg[i].base_fmt + task->fmt;
			u32 *reg = task->reg[class].data;
			u32 ss = hw->reg_msg[class].base_s / sizeof(u32);

			if (!reg)
				continue;

			ret = mpp_translate_reg_address(session, mpp_task, fmt, reg, NULL);
			if (ret)
				goto fail;

			cnt = mpp->var->trans_info[fmt].count;
			tbl = mpp->var->trans_info[fmt].table;
			for (j = 0; j < cnt; j++) {
				off = mpp_query_reg_offset_info(&task->off_inf, tbl[j] + ss);
				mpp_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n", tbl[j] + ss, off);
				reg[tbl[j]] += off;
			}
		}
	}
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return mpp_task;

fail:
	mpp_task_dump_mem_region(mpp, mpp_task);
	mpp_task_dump_reg(mpp, mpp_task);
	mpp_task_finalize(session, mpp_task);
	/* free class register buffer */
	rkvenc_free_class_msg(task);
free_task:
	kfree(task);

	return NULL;
}

static void *rkvenc_ccu_alloc_task(struct mpp_session *session,
				   struct mpp_task_msgs *msgs)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(session->mpp);

	/* if multi-cores, choose one for current task */
	if (enc->ccu) {
		enc = rkvenc_core_balance(enc->ccu);
		session->mpp = &enc->mpp;
	}

	return rkvenc_alloc_task(session, msgs);
}

static int rkvenc_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i, j;
	u32 start_val = 0;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	for (i = 0; i < task->w_req_cnt; i++) {
		int ret;
		u32 s, e, off;
		u32 *regs;

		struct mpp_request msg;
		struct mpp_request *req = &task->w_reqs[i];

		ret = rkvenc_get_class_msg(task, req->offset, &msg);
		if (ret)
			return -EINVAL;

		s = (req->offset - msg.offset) / sizeof(u32);
		e = s + req->size / sizeof(u32);
		regs = (u32 *)msg.data;
		for (j = s; j < e; j++) {
			off = msg.offset + j * sizeof(u32);
			if (off == enc->hw_info->enc_start_base) {
				start_val = regs[j];
				continue;
			}
			mpp_write_relaxed(mpp, off, regs[j]);
		}
	}

	/* init current task */
	mpp->cur_task = mpp_task;
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, enc->hw_info->enc_start_base, start_val);

	mpp_debug_leave();

	return 0;
}

static int rkvenc_irq(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;

	mpp_debug_enter();

	mpp->irq_status = mpp_read(mpp, hw->int_sta_base);
	if (!mpp->irq_status)
		return IRQ_NONE;
	mpp_write(mpp, hw->int_mask_base, 0x100);
	mpp_write(mpp, hw->int_clr_base, 0xffffffff);
	udelay(5);
	mpp_write(mpp, hw->int_sta_base, 0);

	mpp_debug_leave();

	return IRQ_WAKE_THREAD;
}

static int rkvenc_isr(struct mpp_dev *mpp)
{
	struct rkvenc_task *task;
	struct mpp_task *mpp_task;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	mpp_debug_enter();

	/* FIXME use a spin lock here */
	if (!mpp->cur_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_task = mpp->cur_task;
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task = to_rkvenc_task(mpp_task);
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);

	if (task->irq_status & enc->hw_info->err_mask) {
		atomic_inc(&mpp->reset_request);
		/* dump register */
		if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG))
			mpp_task_dump_hw_reg(mpp, mpp_task);
	}
	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int rkvenc_finish(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i, j;
	u32 *reg;
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	for (i = 0; i < task->r_req_cnt; i++) {
		int ret;
		int s, e;
		struct mpp_request msg;
		struct mpp_request *req = &task->r_reqs[i];

		ret = rkvenc_get_class_msg(task, req->offset, &msg);
		if (ret)
			return -EINVAL;
		s = (req->offset - msg.offset) / sizeof(u32);
		e = s + req->size / sizeof(u32);
		reg = (u32 *)msg.data;
		for (j = s; j < e; j++)
			reg[j] = mpp_read_relaxed(mpp, msg.offset + j * sizeof(u32));

	}
	/* revert hack for irq status */
	reg = rkvenc_get_class_reg(task, task->hw_info->int_sta_base);
	if (reg)
		*reg = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static int rkvenc_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_debug_enter();

	for (i = 0; i < task->r_req_cnt; i++) {
		struct mpp_request *req = &task->r_reqs[i];
		u32 *reg = rkvenc_get_class_reg(task, req->offset);

		if (!reg)
			return -EINVAL;
		if (copy_to_user(req->data, reg, req->size)) {
			mpp_err("copy_to_user reg fail\n");
			return -EIO;
		}
	}

	mpp_debug_leave();

	return 0;
}

static int rkvenc_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	rkvenc_free_class_msg(task);
	kfree(task);

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

static int rkvenc_procfs_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	enc->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(enc->procfs)) {
		mpp_err("failed on open procfs\n");
		enc->procfs = NULL;
		return -EIO;
	}
	/* for debug */
	mpp_procfs_create_u32("aclk", 0644,
			      enc->procfs, &enc->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("clk_core", 0644,
			      enc->procfs, &enc->core_clk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      enc->procfs, &mpp->session_max_buffers);

	return 0;
}

static int rkvenc_procfs_ccu_init(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

	if (!enc->procfs)
		goto done;

	mpp_procfs_create_u32("disable_work", 0644,
			      enc->procfs, &enc->disable_work);
done:
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

static inline int rkvenc_procfs_ccu_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

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

	return 0;
}

static int rkvenc_reset(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;

	mpp_debug_enter();

	/* safe reset */
	mpp_write(mpp, hw->int_mask_base, 0x3FF);
	mpp_write(mpp, hw->enc_clr_base, 0x1);
	udelay(5);
	mpp_write(mpp, hw->int_clr_base, 0xffffffff);
	mpp_write(mpp, hw->int_sta_base, 0);

	/* cru reset */
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

static int rkvenc_set_freq(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);

	mpp_clk_set_rate(&enc->aclk_info, task->clk_mode);
	mpp_clk_set_rate(&enc->core_clk_info, task->clk_mode);

	return 0;
}

static struct mpp_hw_ops rkvenc_hw_ops = {
	.init = rkvenc_init,
	.clk_on = rkvenc_clk_on,
	.clk_off = rkvenc_clk_off,
	.set_freq = rkvenc_set_freq,
	.reset = rkvenc_reset,
};

static struct mpp_dev_ops rkvenc_dev_ops_v2 = {
	.alloc_task = rkvenc_alloc_task,
	.run = rkvenc_run,
	.irq = rkvenc_irq,
	.isr = rkvenc_isr,
	.finish = rkvenc_finish,
	.result = rkvenc_result,
	.free_task = rkvenc_free_task,
};

static struct mpp_dev_ops rkvenc_ccu_dev_ops = {
	.alloc_task = rkvenc_ccu_alloc_task,
	.run = rkvenc_run,
	.irq = rkvenc_irq,
	.isr = rkvenc_isr,
	.finish = rkvenc_finish,
	.result = rkvenc_result,
	.free_task = rkvenc_free_task,
};


static const struct mpp_dev_var rkvenc_v2_data = {
	.device_type = MPP_DEVICE_RKVENC,
	.hw_info = &rkvenc_v2_hw_info.hw,
	.trans_info = trans_rkvenc_v2,
	.hw_ops = &rkvenc_hw_ops,
	.dev_ops = &rkvenc_dev_ops_v2,
};

static const struct mpp_dev_var rkvenc_ccu_data = {
	.device_type = MPP_DEVICE_RKVENC,
	.hw_info = &rkvenc_v2_hw_info.hw,
	.trans_info = trans_rkvenc_v2,
	.hw_ops = &rkvenc_hw_ops,
	.dev_ops = &rkvenc_ccu_dev_ops,
};

static const struct of_device_id mpp_rkvenc_dt_match[] = {
	{
		.compatible = "rockchip,rkv-encoder-v2",
		.data = &rkvenc_v2_data,
	},
#ifdef CONFIG_CPU_RK3588
	{
		.compatible = "rockchip,rkv-encoder-v2-core",
		.data = &rkvenc_ccu_data,
	},
	{
		.compatible = "rockchip,rkv-encoder-v2-ccu",
	},
#endif
	{},
};

static int rkvenc_ccu_probe(struct platform_device *pdev)
{
	struct rkvenc_ccu *ccu;
	struct device *dev = &pdev->dev;

	ccu = devm_kzalloc(dev, sizeof(*ccu), GFP_KERNEL);
	if (!ccu)
		return -ENOMEM;

	platform_set_drvdata(pdev, ccu);

	mutex_init(&ccu->lock);
	INIT_LIST_HEAD(&ccu->core_list);

	return 0;
}

static int rkvenc_attach_ccu(struct device *dev, struct rkvenc_dev *enc)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkvenc_ccu *ccu;

	mpp_debug_enter();

	np = of_parse_phandle(dev->of_node, "rockchip,ccu", 0);
	if (!np || !of_device_is_available(np))
		return -ENODEV;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENODEV;

	ccu = platform_get_drvdata(pdev);
	if (!ccu)
		return -ENOMEM;

	INIT_LIST_HEAD(&enc->core_link);
	mutex_lock(&ccu->lock);
	ccu->core_num++;
	list_add_tail(&enc->core_link, &ccu->core_list);
	mutex_unlock(&ccu->lock);

	/* attach the ccu-domain to current core */
	if (!ccu->main_core) {
		/**
		 * set the first device for the main-core,
		 * then the domain of the main-core named ccu-domain
		 */
		ccu->main_core = &enc->mpp;
	} else {
		struct mpp_iommu_info *ccu_info, *cur_info;

		/* set the ccu-domain for current device */
		ccu_info = ccu->main_core->iommu_info;
		cur_info = enc->mpp.iommu_info;

		cur_info->domain = ccu_info->domain;
		mpp_iommu_attach(cur_info);
	}
	enc->ccu = ccu;

	dev_info(dev, "attach ccu success\n");
	mpp_debug_enter();

	return 0;
}

static int rkvenc_core_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;


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

	/* attach core to ccu */
	ret = rkvenc_attach_ccu(dev, enc);
	if (ret) {
		dev_err(dev, "attach ccu failed\n");
		return ret;
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
	mpp->session_max_buffers = RKVENC_SESSION_MAX_BUFFERS;
	enc->hw_info = to_rkvenc_info(mpp->var->hw_info);
	rkvenc_procfs_init(mpp);
	rkvenc_procfs_ccu_init(mpp);

	/* if current is main-core, register current device to mpp service */
	if (mpp == enc->ccu->main_core)
		mpp_dev_register_srv(mpp, mpp->srv);

	return 0;
}

static int rkvenc_probe_default(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

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
	enc->hw_info = to_rkvenc_info(mpp->var->hw_info);
	rkvenc_procfs_init(mpp);
	mpp_dev_register_srv(mpp, mpp->srv);

	return 0;

failed_get_irq:
	mpp_dev_remove(mpp);

	return ret;
}

static int rkvenc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	dev_info(dev, "probing start\n");

	if (strstr(np->name, "ccu"))
		ret = rkvenc_ccu_probe(pdev);
	else if (strstr(np->name, "core"))
		ret = rkvenc_core_probe(pdev);
	else
		ret = rkvenc_probe_default(pdev);

	dev_info(dev, "probing finish\n");

	return ret;
}

static int rkvenc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (strstr(np->name, "ccu")) {
		dev_info(dev, "remove ccu\n");
	} else if (strstr(np->name, "core")) {
		struct rkvenc_dev *enc = platform_get_drvdata(pdev);

		dev_info(dev, "remove core\n");
		if (enc->ccu) {
			mutex_lock(&enc->ccu->lock);
			list_del_init(&enc->core_link);
			enc->ccu->core_num--;
			mutex_unlock(&enc->ccu->lock);
		}
		mpp_dev_remove(&enc->mpp);
		rkvenc_procfs_remove(&enc->mpp);
	} else {
		struct rkvenc_dev *enc = platform_get_drvdata(pdev);

		dev_info(dev, "remove device\n");
		mpp_dev_remove(&enc->mpp);
		rkvenc_procfs_remove(&enc->mpp);
	}

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

	dev_info(dev, "shutdown success\n");
}

struct platform_driver rockchip_rkvenc2_driver = {
	.probe = rkvenc_probe,
	.remove = rkvenc_remove,
	.shutdown = rkvenc_shutdown,
	.driver = {
		.name = RKVENC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_rkvenc_dt_match),
	},
};
