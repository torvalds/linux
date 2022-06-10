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
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/nospec.h>
#include <linux/workqueue.h>
#include <linux/dma-iommu.h>
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
#define RKVENC_MAX_DCHS_ID			4

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

#define INT_STA_ENC_DONE_STA	BIT(0)
#define INT_STA_SCLR_DONE_STA	BIT(2)
#define INT_STA_SLC_DONE_STA	BIT(3)
#define INT_STA_BSF_OFLW_STA	BIT(4)
#define INT_STA_BRSP_OTSD_STA	BIT(5)
#define INT_STA_WBUS_ERR_STA	BIT(6)
#define INT_STA_RBUS_ERR_STA	BIT(7)
#define INT_STA_WDG_STA		BIT(8)

#define DCHS_REG_OFFSET		(0x304)
#define DCHS_CLASS_OFFSET	(33)
#define DCHS_TXE		(0x10)
#define DCHS_RXE		(0x20)

/* dual core hand-shake info */
union rkvenc2_dual_core_handshake_id {
	u64 val;
	struct {
		u32 txid	: 2;
		u32 rxid	: 2;
		u32 txe		: 1;
		u32 rxe		: 1;
		u32 working	: 1;
		u32 reserve0	: 1;
		u32 txid_orig	: 2;
		u32 rxid_orig	: 2;
		u32 txid_map	: 2;
		u32 rxid_map	: 2;
		u32 offset	: 11;
		u32 reserve1	: 1;
		u32 txe_orig	: 1;
		u32 rxe_orig	: 1;
		u32 txe_map	: 1;
		u32 rxe_map	: 1;
		u32 session_id;
	};
};

#define RKVENC2_REG_INT_EN		(8)
#define RKVENC2_BIT_SLICE_DONE_EN	BIT(3)

#define RKVENC2_REG_INT_MASK		(9)
#define RKVENC2_BIT_SLICE_DONE_MASK	BIT(3)

#define RKVENC2_REG_ENC_PIC		(32)
#define RKVENC2_BIT_SLEN_FIFO		BIT(30)

#define RKVENC2_REG_SLI_SPLIT		(56)
#define RKVENC2_BIT_SLI_SPLIT		BIT(0)
#define RKVENC2_BIT_SLI_FLUSH		BIT(15)

#define RKVENC2_REG_SLICE_NUM_BASE	(0x4034)
#define RKVENC2_REG_SLICE_LEN_BASE	(0x4038)

union rkvenc2_slice_len_info {
	u32 val;

	struct {
		u32 slice_len	: 31;
		u32 last	: 1;
	};
};

struct rkvenc_poll_slice_cfg {
	s32 poll_type;
	s32 poll_ret;
	s32 count_max;
	s32 count_ret;
	union rkvenc2_slice_len_info slice_info[];
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

	union rkvenc2_dual_core_handshake_id dchs_id;

	/* split output / slice mode info */
	u32 task_split;
	u32 task_split_done;
	u32 last_slice_found;
	u32 slice_wr_cnt;
	u32 slice_rd_cnt;
	DECLARE_KFIFO(slice_info, union rkvenc2_slice_len_info, 64);
};

#define RKVENC_MAX_RCB_NUM		(4)

struct rcb_info_elem {
	u32 index;
	u32 size;
};

struct rkvenc2_rcb_info {
	u32 cnt;
	struct rcb_info_elem elem[RKVENC_MAX_RCB_NUM];
};

struct rkvenc2_session_priv {
	struct rw_semaphore rw_sem;
	/* codec info from user */
	struct {
		/* show mode */
		u32 flag;
		/* item data */
		u64 val;
	} codec_info[ENC_INFO_BUTT];
	/* rcb_info for sram */
	struct rkvenc2_rcb_info rcb_inf;
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

	/* internal rcb-memory */
	u32 sram_size;
	u32 sram_used;
	dma_addr_t sram_iova;
	u32 sram_enabled;
	struct page *rcb_page;
};

struct rkvenc_ccu {
	u32 core_num;
	/* lock for core attach */
	struct mutex lock;
	struct list_head core_list;
	struct mpp_dev *main_core;

	spinlock_t lock_dchs;
	union rkvenc2_dual_core_handshake_id dchs[RKVENC_MAX_CORE_NUM];
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
		.base_e = 0x21e4,
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

static int rkvenc2_extract_rcb_info(struct rkvenc2_rcb_info *rcb_inf,
				    struct mpp_request *req)
{
	int max_size = ARRAY_SIZE(rcb_inf->elem);
	int cnt = req->size / sizeof(rcb_inf->elem[0]);

	if (req->size > sizeof(rcb_inf->elem)) {
		mpp_err("count %d,max_size %d\n", cnt, max_size);
		return -EINVAL;
	}
	if (copy_from_user(rcb_inf->elem, req->data, req->size)) {
		mpp_err("copy_from_user failed\n");
		return -EINVAL;
	}
	rcb_inf->cnt = cnt;

	return 0;
}

static int rkvenc_extract_task_msg(struct mpp_session *session,
				   struct rkvenc_task *task,
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
		case MPP_CMD_SET_RCB_INFO: {
			struct rkvenc2_session_priv *priv = session->priv;

			if (priv)
				rkvenc2_extract_rcb_info(&priv->rcb_inf, req);
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

static int rkvenc2_set_rcbbuf(struct mpp_dev *mpp, struct mpp_session *session,
			      struct rkvenc_task *task)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc2_session_priv *priv = session->priv;
	u32 sram_enabled = 0;

	mpp_debug_enter();

	if (priv && enc->sram_iova) {
		int i;
		u32 *reg;
		u32 reg_idx, rcb_size, rcb_offset;
		struct rkvenc2_rcb_info *rcb_inf = &priv->rcb_inf;

		rcb_offset = 0;
		for (i = 0; i < rcb_inf->cnt; i++) {
			reg_idx = rcb_inf->elem[i].index;
			rcb_size = rcb_inf->elem[i].size;

			if (rcb_offset > enc->sram_size ||
			    (rcb_offset + rcb_size) > enc->sram_used)
				continue;

			mpp_debug(DEBUG_SRAM_INFO, "rcb: reg %d offset %d, size %d\n",
				  reg_idx, rcb_offset, rcb_size);

			reg = rkvenc_get_class_reg(task, reg_idx * sizeof(u32));
			if (reg)
				*reg = enc->sram_iova + rcb_offset;

			rcb_offset += rcb_size;
			sram_enabled = 1;
		}
	}
	if (enc->sram_enabled != sram_enabled) {
		mpp_debug(DEBUG_SRAM_INFO, "sram %s\n", sram_enabled ? "enabled" : "disabled");
		enc->sram_enabled = sram_enabled;
	}

	mpp_debug_leave();

	return 0;
}

static void rkvenc2_setup_task_id(u32 session_id, struct rkvenc_task *task)
{
	u32 val = task->reg[RKVENC_CLASS_PIC].data[DCHS_CLASS_OFFSET];

	/* always enable tx */
	val |= DCHS_TXE;

	task->reg[RKVENC_CLASS_PIC].data[DCHS_CLASS_OFFSET] = val;
	task->dchs_id.val = (((u64)session_id << 32) | val);

	task->dchs_id.txid_orig = task->dchs_id.txid;
	task->dchs_id.rxid_orig = task->dchs_id.rxid;
	task->dchs_id.txid_map = task->dchs_id.txid;
	task->dchs_id.rxid_map = task->dchs_id.rxid;

	task->dchs_id.txe_orig = task->dchs_id.txe;
	task->dchs_id.rxe_orig = task->dchs_id.rxe;
	task->dchs_id.txe_map = task->dchs_id.txe;
	task->dchs_id.rxe_map = task->dchs_id.rxe;
}

static int rkvenc2_is_split_task(struct rkvenc_task *task)
{
	u32 slc_done_en;
	u32 slc_done_msk;
	u32 slen_fifo_en;
	u32 sli_split_en;
	u32 sli_flsh_en;

	if (task->reg[RKVENC_CLASS_BASE].valid) {
		u32 *reg = task->reg[RKVENC_CLASS_BASE].data;

		slc_done_en  = (reg[RKVENC2_REG_INT_EN] & RKVENC2_BIT_SLICE_DONE_EN) ? 1 : 0;
		slc_done_msk = (reg[RKVENC2_REG_INT_MASK] & RKVENC2_BIT_SLICE_DONE_MASK) ? 1 : 0;
	} else {
		slc_done_en  = 0;
		slc_done_msk = 0;
	}

	if (task->reg[RKVENC_CLASS_PIC].valid) {
		u32 *reg = task->reg[RKVENC_CLASS_PIC].data;

		slen_fifo_en = (reg[RKVENC2_REG_ENC_PIC] & RKVENC2_BIT_SLEN_FIFO) ? 1 : 0;
		sli_split_en = (reg[RKVENC2_REG_SLI_SPLIT] & RKVENC2_BIT_SLI_SPLIT) ? 1 : 0;
		sli_flsh_en  = (reg[RKVENC2_REG_SLI_SPLIT] & RKVENC2_BIT_SLI_FLUSH) ? 1 : 0;
	} else {
		slen_fifo_en = 0;
		sli_split_en = 0;
		sli_flsh_en  = 0;
	}

	if (sli_split_en && slen_fifo_en && sli_flsh_en) {
		if (!slc_done_en || slc_done_msk)
			mpp_dbg_slice("task %d slice output enabled but irq disabled!\n",
				      task->mpp_task.task_id);

		return 1;
	}

	return 0;
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
	ret = rkvenc_extract_task_msg(session, task, msgs);
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
	rkvenc2_set_rcbbuf(mpp, session, task);
	rkvenc2_setup_task_id(session->index, task);
	task->clk_mode = CLK_MODE_NORMAL;
	task->task_split = rkvenc2_is_split_task(task);
	if (task->task_split)
		INIT_KFIFO(task->slice_info);

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

static void *rkvenc2_prepare(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct mpp_taskqueue *queue = mpp->queue;
	unsigned long flags;
	s32 core_id;

	spin_lock_irqsave(&queue->running_lock, flags);

	core_id = find_first_bit(&queue->core_idle, queue->core_count);

	if (core_id >= queue->core_count) {
		mpp_task = NULL;
		mpp_dbg_core("core %d all busy %lx\n", core_id, queue->core_idle);
	} else {
		unsigned long core_idle = queue->core_idle;

		clear_bit(core_id, &queue->core_idle);
		mpp_task->mpp = queue->cores[core_id];
		mpp_task->core_id = core_id;

		mpp_dbg_core("core %d set idle %lx -> %lx\n", core_id,
			     core_idle, queue->core_idle);
	}

	spin_unlock_irqrestore(&queue->running_lock, flags);

	return mpp_task;
}

static void rkvenc2_patch_dchs(struct rkvenc_dev *enc, struct rkvenc_task *task)
{
	struct rkvenc_ccu *ccu;
	union rkvenc2_dual_core_handshake_id *dchs;
	union rkvenc2_dual_core_handshake_id *task_dchs = &task->dchs_id;
	int core_num;
	int core_id = enc->mpp.core_id;
	unsigned long flags;
	int i;

	if (!enc->ccu)
		return;

	if (core_id >= RKVENC_MAX_CORE_NUM) {
		dev_err(enc->mpp.dev, "invalid core id %d max %d\n",
			core_id, RKVENC_MAX_CORE_NUM);
		return;
	}

	ccu = enc->ccu;
	dchs = ccu->dchs;
	core_num = ccu->core_num;

	spin_lock_irqsave(&ccu->lock_dchs, flags);

	if (dchs[core_id].working) {
		spin_unlock_irqrestore(&ccu->lock_dchs, flags);

		mpp_err("can not config when core %d is still working\n", core_id);
		return;
	}

	if (mpp_debug_unlikely(DEBUG_CORE))
		pr_info("core tx:rx 0 %s %d:%d %d:%d -- 1 %s %d:%d %d:%d -- task %d %d:%d %d:%d\n",
			dchs[0].working ? "work" : "idle",
			dchs[0].txid, dchs[0].txe, dchs[0].rxid, dchs[0].rxe,
			dchs[1].working ? "work" : "idle",
			dchs[1].txid, dchs[1].txe, dchs[1].rxid, dchs[1].rxe,
			core_id, task_dchs->txid, task_dchs->txe,
			task_dchs->rxid, task_dchs->rxe);

	/* always use new id as  */
	{
		struct mpp_task *mpp_task = &task->mpp_task;
		unsigned long id_valid = (unsigned long)-1;
		int txid_map = -1;
		int rxid_map = -1;

		/* scan all used id */
		for (i = 0; i < core_num; i++) {
			if (!dchs[i].working)
				continue;

			clear_bit(dchs[i].txid_map, &id_valid);
			clear_bit(dchs[i].rxid_map, &id_valid);
		}

		if (task_dchs->rxe) {
			for (i = 0; i < core_num; i++) {
				if (i == core_id)
					continue;

				if (!dchs[i].working)
					continue;

				if (task_dchs->session_id != dchs[i].session_id)
					continue;

				if (task_dchs->rxid_orig != dchs[i].txid_orig)
					continue;

				rxid_map = dchs[i].txid_map;
				break;
			}
		}

		txid_map = find_first_bit(&id_valid, RKVENC_MAX_DCHS_ID);
		if (txid_map == RKVENC_MAX_DCHS_ID) {
			spin_unlock_irqrestore(&ccu->lock_dchs, flags);

			mpp_err("task %d:%d on core %d failed to find a txid\n",
				mpp_task->session->pid, mpp_task->task_id,
				mpp_task->core_id);
			return;
		}

		clear_bit(txid_map, &id_valid);
		task_dchs->txid_map = txid_map;

		if (rxid_map < 0) {
			rxid_map = find_first_bit(&id_valid, RKVENC_MAX_DCHS_ID);
			if (rxid_map == RKVENC_MAX_DCHS_ID) {
				spin_unlock_irqrestore(&ccu->lock_dchs, flags);

				mpp_err("task %d:%d on core %d failed to find a rxid\n",
					mpp_task->session->pid, mpp_task->task_id,
					mpp_task->core_id);
				return;
			}

			task_dchs->rxe_map = 0;
		}

		task_dchs->rxid_map = rxid_map;
	}

	task_dchs->txid = task_dchs->txid_map;
	task_dchs->rxid = task_dchs->rxid_map;
	task_dchs->rxe = task_dchs->rxe_map;

	dchs[core_id].val = task_dchs->val;
	task->reg[RKVENC_CLASS_PIC].data[DCHS_CLASS_OFFSET] = task_dchs->val;

	dchs[core_id].working = 1;

	spin_unlock_irqrestore(&ccu->lock_dchs, flags);
}

static void rkvenc2_update_dchs(struct rkvenc_dev *enc, struct rkvenc_task *task)
{
	struct rkvenc_ccu *ccu = enc->ccu;
	int core_id = enc->mpp.core_id;
	unsigned long flags;

	if (!ccu)
		return;

	if (core_id >= RKVENC_MAX_CORE_NUM) {
		dev_err(enc->mpp.dev, "invalid core id %d max %d\n",
			core_id, RKVENC_MAX_CORE_NUM);
		return;
	}

	spin_lock_irqsave(&ccu->lock_dchs, flags);
	ccu->dchs[core_id].val = 0;

	if (mpp_debug_unlikely(DEBUG_CORE)) {
		union rkvenc2_dual_core_handshake_id *dchs = ccu->dchs;
		union rkvenc2_dual_core_handshake_id *task_dchs = &task->dchs_id;

		pr_info("core %d task done\n", core_id);
		pr_info("core tx:rx 0 %s %d:%d %d:%d -- 1 %s %d:%d %d:%d -- task %d %d:%d %d:%d\n",
			dchs[0].working ? "work" : "idle",
			dchs[0].txid, dchs[0].txe, dchs[0].rxid, dchs[0].rxe,
			dchs[1].working ? "work" : "idle",
			dchs[1].txid, dchs[1].txe, dchs[1].rxid, dchs[1].rxe,
			core_id, task_dchs->txid, task_dchs->txe,
			task_dchs->rxid, task_dchs->rxe);
	}

	spin_unlock_irqrestore(&ccu->lock_dchs, flags);
}

static int rkvenc_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i, j;
	u32 start_val = 0;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_task *task = to_rkvenc_task(mpp_task);
	struct rkvenc_hw_info *hw = enc->hw_info;

	mpp_debug_enter();

	/* Add force clear to avoid pagefault */
	mpp_write(mpp, hw->enc_clr_base, 0x2);
	udelay(5);
	mpp_write(mpp, hw->enc_clr_base, 0x0);

	/* clear hardware counter */
	mpp_write_relaxed(mpp, 0x5300, 0x2);

	rkvenc2_patch_dchs(enc, task);

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

	if (mpp_debug_unlikely(DEBUG_CORE))
		dev_info(mpp->dev, "core %d dchs %08x\n", mpp->core_id,
			 mpp_read_relaxed(&enc->mpp, DCHS_REG_OFFSET));

	/* flush tlb before starting hardware */
	mpp_iommu_flush_tlb(mpp->iommu_info);

	/* init current task */
	mpp->cur_task = mpp_task;

	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, enc->hw_info->enc_start_base, start_val);

	mpp_debug_leave();

	return 0;
}

static void rkvenc2_read_slice_len(struct mpp_dev *mpp, struct rkvenc_task *task)
{
	u32 last = mpp_read_relaxed(mpp, 0x002c) & INT_STA_ENC_DONE_STA;
	u32 sli_num = mpp_read_relaxed(mpp, RKVENC2_REG_SLICE_NUM_BASE);
	union rkvenc2_slice_len_info slice_info;
	u32 task_id = task->mpp_task.task_id;
	u32 i;

	mpp_dbg_slice("task %d wr %3d len start %s\n", task_id,
		      sli_num, last ? "last" : "");

	for (i = 0; i < sli_num; i++) {
		slice_info.val = mpp_read_relaxed(mpp, RKVENC2_REG_SLICE_LEN_BASE);

		if (last && i == sli_num - 1) {
			task->last_slice_found = 1;
			slice_info.last = 1;
		}

		mpp_dbg_slice("task %d wr %3d len %d %s\n", task_id,
			      task->slice_wr_cnt, slice_info.slice_len,
			      slice_info.last ? "last" : "");

		kfifo_in(&task->slice_info, &slice_info, 1);
		task->slice_wr_cnt++;
	}

	/* Fixup for async between last flag and slice number register */
	if (last && !task->last_slice_found) {
		mpp_dbg_slice("task %d mark last slice\n", task_id);
		slice_info.last = 1;
		slice_info.slice_len = 0;
		kfifo_in(&task->slice_info, &slice_info, 1);
	}
}

static int rkvenc_irq(struct mpp_dev *mpp)
{
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct rkvenc_hw_info *hw = enc->hw_info;
	struct mpp_task *mpp_task = NULL;
	struct rkvenc_task *task = NULL;
	int ret = IRQ_NONE;

	mpp_debug_enter();

	mpp->irq_status = mpp_read(mpp, hw->int_sta_base);
	if (!mpp->irq_status)
		return ret;

	if (mpp->cur_task) {
		mpp_task = mpp->cur_task;
		task = to_rkvenc_task(mpp_task);
	}

	if (mpp->irq_status & INT_STA_ENC_DONE_STA) {
		if (task) {
			if (task->task_split)
				rkvenc2_read_slice_len(mpp, task);

			wake_up(&mpp_task->wait);
		}

		mpp_write(mpp, hw->int_mask_base, 0x100);
		mpp_write(mpp, hw->int_clr_base, 0xffffffff);
		udelay(5);
		mpp_write(mpp, hw->int_sta_base, 0);

		ret = IRQ_WAKE_THREAD;
	} else if (mpp->irq_status & INT_STA_SLC_DONE_STA) {
		if (task && task->task_split) {
			mpp_time_part_diff(mpp_task);

			rkvenc2_read_slice_len(mpp, task);
			wake_up(&mpp_task->wait);
		}

		mpp_write(mpp, hw->int_clr_base, INT_STA_SLC_DONE_STA);
	}

	mpp_debug_leave();

	return ret;
}

static int rkvenc_isr(struct mpp_dev *mpp)
{
	struct rkvenc_task *task;
	struct mpp_task *mpp_task;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct mpp_taskqueue *queue = mpp->queue;
	unsigned long core_idle;

	mpp_debug_enter();

	/* FIXME use a spin lock here */
	if (!mpp->cur_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_task = mpp->cur_task;
	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;

	if (mpp_task->mpp && mpp_task->mpp != mpp)
		dev_err(mpp->dev, "mismatch core dev %p:%p\n", mpp_task->mpp, mpp);

	task = to_rkvenc_task(mpp_task);
	task->irq_status = mpp->irq_status;

	rkvenc2_update_dchs(enc, task);

	mpp_debug(DEBUG_IRQ_STATUS, "%s irq_status: %08x\n",
		  dev_name(mpp->dev), task->irq_status);

	if (task->irq_status & enc->hw_info->err_mask) {
		atomic_inc(&mpp->reset_request);
		/* dump register */

		mpp_task_dump_hw_reg(mpp);
	}
	mpp_task_finish(mpp_task->session, mpp_task);

	core_idle = queue->core_idle;
	set_bit(mpp->core_id, &queue->core_idle);

	mpp_dbg_core("core %d isr idle %lx -> %lx\n", mpp->core_id, core_idle,
		     queue->core_idle);

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

static int rkvenc_control(struct mpp_session *session, struct mpp_request *req)
{
	switch (req->cmd) {
	case MPP_CMD_SEND_CODEC_INFO: {
		int i;
		int cnt;
		struct codec_info_elem elem;
		struct rkvenc2_session_priv *priv;

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
	struct rkvenc2_session_priv *priv;

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
	struct rkvenc2_session_priv *priv = session->priv;

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

static int rkvenc_show_session_info(struct seq_file *seq, void *offset)
{
	struct mpp_session *session = NULL, *n;
	struct mpp_dev *mpp = seq->private;

	mutex_lock(&mpp->srv->session_lock);
	list_for_each_entry_safe(session, n,
				 &mpp->srv->session_list,
				 session_link) {
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
	char name[32];

	if (!mpp->dev || !mpp->dev->of_node || !mpp->dev->of_node->name ||
	    !mpp->srv || !mpp->srv->procfs)
		return -EINVAL;

	snprintf(name, sizeof(name) - 1, "%s%d",
		 mpp->dev->of_node->name, mpp->core_id);

	enc->procfs = proc_mkdir(name, mpp->srv->procfs);
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
	/* for show session info */
	proc_create_single_data("sessions-info", 0444,
				enc->procfs, rkvenc_show_session_info, mpp);

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
	struct mpp_taskqueue *queue = mpp->queue;

	mpp_debug_enter();

	/* safe reset */
	mpp_write(mpp, hw->int_mask_base, 0x3FF);
	mpp_write(mpp, hw->enc_clr_base, 0x1);
	udelay(5);
	mpp_write(mpp, hw->int_clr_base, 0xffffffff);
	mpp_write(mpp, hw->int_sta_base, 0);

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

	set_bit(mpp->core_id, &queue->core_idle);
	if (enc->ccu)
		enc->ccu->dchs[mpp->core_id].val = 0;

	mpp_dbg_core("core %d reset idle %lx\n", mpp->core_id, queue->core_idle);

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

#define RKVENC2_WORK_TIMEOUT_DELAY		(200)
#define RKVENC2_WAIT_TIMEOUT_DELAY		(2000)

static void rkvenc2_task_pop_pending(struct mpp_task *task)
{
	struct mpp_session *session = task->session;

	mutex_lock(&session->pending_lock);
	list_del_init(&task->pending_link);
	mutex_unlock(&session->pending_lock);

	kref_put(&task->ref, mpp_free_task);
}

static int rkvenc2_task_default_process(struct mpp_dev *mpp,
					struct mpp_task *task)
{
	int ret = 0;

	if (mpp->dev_ops && mpp->dev_ops->result)
		ret = mpp->dev_ops->result(mpp, task, NULL);

	mpp_debug_func(DEBUG_TASK_INFO, "kref_read %d, ret %d\n",
			kref_read(&task->ref), ret);

	rkvenc2_task_pop_pending(task);

	return ret;
}

#define RKVENC2_TIMEOUT_DUMP_REG_START	(0x5100)
#define RKVENC2_TIMEOUT_DUMP_REG_END	(0x5160)

static void rkvenc2_task_timeout_process(struct mpp_session *session,
					 struct mpp_task *task)
{
	atomic_inc(&task->abort_request);
	set_bit(TASK_STATE_ABORT, &task->state);

	mpp_err("session %d:%d count %d task %d ref %d timeout\n",
		session->pid, session->index, atomic_read(&session->task_count),
		task->task_id, kref_read(&task->ref));

	if (task->mpp) {
		struct mpp_dev *mpp = task->mpp;
		u32 start = RKVENC2_TIMEOUT_DUMP_REG_START;
		u32 end = RKVENC2_TIMEOUT_DUMP_REG_END;
		u32 offset;

		dev_err(mpp->dev, "core %d dump timeout status:\n", mpp->core_id);

		for (offset = start; offset < end; offset += sizeof(u32))
			mpp_reg_show(mpp, offset);
	}

	rkvenc2_task_pop_pending(task);
}

static int rkvenc2_wait_result(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	struct rkvenc_poll_slice_cfg cfg;
	struct rkvenc_task *enc_task;
	struct mpp_request *req;
	struct mpp_task *task;
	struct mpp_dev *mpp;
	union rkvenc2_slice_len_info slice_info;
	u32 task_id;
	int ret = 0;

	mutex_lock(&session->pending_lock);
	task = list_first_entry_or_null(&session->pending_list,
					struct mpp_task,
					pending_link);
	mutex_unlock(&session->pending_lock);
	if (!task) {
		mpp_err("session %p pending list is empty!\n", session);
		return -EIO;
	}

	mpp = mpp_get_task_used_device(task, session);
	enc_task = to_rkvenc_task(task);
	task_id = task->task_id;

	req = cmpxchg(&msgs->poll_req, msgs->poll_req, NULL);

	if (!enc_task->task_split || enc_task->task_split_done) {
task_done_ret:
		ret = wait_event_timeout(task->wait,
					 test_bit(TASK_STATE_DONE, &task->state),
					 msecs_to_jiffies(RKVENC2_WAIT_TIMEOUT_DELAY));

		if (ret > 0)
			return rkvenc2_task_default_process(mpp, task);

		rkvenc2_task_timeout_process(session, task);
		return ret;
	}

	/* not slice return just wait all slice length */
	if (!req) {
		do {
			ret = wait_event_timeout(task->wait,
						 kfifo_out(&enc_task->slice_info, &slice_info, 1),
						 msecs_to_jiffies(RKVENC2_WORK_TIMEOUT_DELAY));
			if (ret > 0) {
				mpp_dbg_slice("task %d rd %3d len %d %s\n",
					      task_id, enc_task->slice_rd_cnt, slice_info.slice_len,
					      slice_info.last ? "last" : "");

				enc_task->slice_rd_cnt++;

				if (slice_info.last)
					goto task_done_ret;

				continue;
			}

			rkvenc2_task_timeout_process(session, task);
			return ret;
		} while (1);
	}

	if (copy_from_user(&cfg, req->data, sizeof(cfg))) {
		mpp_err("copy_from_user failed\n");
		return -EINVAL;
	}

	mpp_dbg_slice("task %d poll irq %d:%d\n", task->task_id,
		      cfg.count_max, cfg.count_ret);
	cfg.count_ret = 0;

	/* handle slice mode poll return */
	ret = wait_event_timeout(task->wait,
				 kfifo_out(&enc_task->slice_info, &slice_info, 1),
				 msecs_to_jiffies(RKVENC2_WORK_TIMEOUT_DELAY));
	if (ret > 0) {
		mpp_dbg_slice("task %d rd %3d len %d %s\n", task_id,
			      enc_task->slice_rd_cnt, slice_info.slice_len,
			      slice_info.last ? "last" : "");

		enc_task->slice_rd_cnt++;

		if (cfg.count_ret < cfg.count_max) {
			struct rkvenc_poll_slice_cfg __user *ucfg =
				(struct rkvenc_poll_slice_cfg __user *)(req->data);
			u32 __user *dst = (u32 __user *)(ucfg + 1);

			/* Do NOT return here when put_user error. Just continue */
			if (put_user(slice_info.val, dst + cfg.count_ret))
				ret = -EFAULT;

			cfg.count_ret++;
			if (put_user(cfg.count_ret, &ucfg->count_ret))
				ret = -EFAULT;
		}

		if (slice_info.last) {
			enc_task->task_split_done = 1;
			goto task_done_ret;
		}

		return ret < 0 ? ret : 0;
	}

	rkvenc2_task_timeout_process(session, task);

	return ret;
}

static struct mpp_hw_ops rkvenc_hw_ops = {
	.init = rkvenc_init,
	.clk_on = rkvenc_clk_on,
	.clk_off = rkvenc_clk_off,
	.set_freq = rkvenc_set_freq,
	.reset = rkvenc_reset,
};

static struct mpp_dev_ops rkvenc_dev_ops_v2 = {
	.wait_result = rkvenc2_wait_result,
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

static struct mpp_dev_ops rkvenc_ccu_dev_ops = {
	.wait_result = rkvenc2_wait_result,
	.alloc_task = rkvenc_alloc_task,
	.prepare = rkvenc2_prepare,
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
	spin_lock_init(&ccu->lock_dchs);

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
		cur_info->rw_sem = ccu_info->rw_sem;
		mpp_iommu_attach(cur_info);

		/* increase main core message capacity */
		ccu->main_core->msgs_cap++;
		enc->mpp.msgs_cap = 0;
	}
	enc->ccu = ccu;

	dev_info(dev, "attach ccu as core %d\n", enc->mpp.core_id);
	mpp_debug_enter();

	return 0;
}

static int rkvenc2_alloc_rcbbuf(struct platform_device *pdev, struct rkvenc_dev *enc)
{
	int ret;
	u32 vals[2];
	dma_addr_t iova;
	u32 sram_used, sram_size;
	struct device_node *sram_np;
	struct resource sram_res;
	resource_size_t sram_start, sram_end;
	struct iommu_domain *domain;
	struct device *dev = &pdev->dev;

	/* get rcb iova start and size */
	ret = device_property_read_u32_array(dev, "rockchip,rcb-iova", vals, 2);
	if (ret)
		return ret;

	iova = PAGE_ALIGN(vals[0]);
	sram_used = PAGE_ALIGN(vals[1]);
	if (!sram_used) {
		dev_err(dev, "sram rcb invalid.\n");
		return -EINVAL;
	}
	/* alloc reserve iova for rcb */
	ret = iommu_dma_reserve_iova(dev, iova, sram_used);
	if (ret) {
		dev_err(dev, "alloc rcb iova error.\n");
		return ret;
	}
	/* get sram device node */
	sram_np = of_parse_phandle(dev->of_node, "rockchip,sram", 0);
	if (!sram_np) {
		dev_err(dev, "could not find phandle sram\n");
		return -ENODEV;
	}
	/* get sram start and size */
	ret = of_address_to_resource(sram_np, 0, &sram_res);
	of_node_put(sram_np);
	if (ret) {
		dev_err(dev, "find sram res error\n");
		return ret;
	}
	/* check sram start and size is PAGE_SIZE align */
	sram_start = round_up(sram_res.start, PAGE_SIZE);
	sram_end = round_down(sram_res.start + resource_size(&sram_res), PAGE_SIZE);
	if (sram_end <= sram_start) {
		dev_err(dev, "no available sram, phy_start %pa, phy_end %pa\n",
			&sram_start, &sram_end);
		return -ENOMEM;
	}
	sram_size = sram_end - sram_start;
	sram_size = sram_used < sram_size ? sram_used : sram_size;
	/* iova map to sram */
	domain = enc->mpp.iommu_info->domain;
	ret = iommu_map(domain, iova, sram_start, sram_size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "sram iommu_map error.\n");
		return ret;
	}
	/* alloc dma for the remaining buffer, sram + dma */
	if (sram_size < sram_used) {
		struct page *page;
		size_t page_size = PAGE_ALIGN(sram_used - sram_size);

		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(page_size));
		if (!page) {
			dev_err(dev, "unable to allocate pages\n");
			ret = -ENOMEM;
			goto err_sram_map;
		}
		/* iova map to dma */
		ret = iommu_map(domain, iova + sram_size, page_to_phys(page),
				page_size, IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			dev_err(dev, "page iommu_map error.\n");
			__free_pages(page, get_order(page_size));
			goto err_sram_map;
		}
		enc->rcb_page = page;
	}

	enc->sram_size = sram_size;
	enc->sram_used = sram_used;
	enc->sram_iova = iova;
	enc->sram_enabled = -1;
	dev_info(dev, "sram_start %pa\n", &sram_start);
	dev_info(dev, "sram_iova %pad\n", &enc->sram_iova);
	dev_info(dev, "sram_size %u\n", enc->sram_size);
	dev_info(dev, "sram_used %u\n", enc->sram_used);

	return 0;

err_sram_map:
	iommu_unmap(domain, iova, sram_size);

	return ret;
}

static int rkvenc2_iommu_fault_handle(struct iommu_domain *iommu,
				      struct device *iommu_dev,
				      unsigned long iova, int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct rkvenc_dev *enc = to_rkvenc_dev(mpp);
	struct mpp_task *mpp_task = mpp->cur_task;

	dev_info(mpp->dev, "core %d page fault found dchs %08x\n",
		 mpp->core_id, mpp_read_relaxed(&enc->mpp, DCHS_REG_OFFSET));

	if (mpp_task)
		mpp_task_dump_mem_region(mpp, mpp_task);

	return 0;
}

static int rkvenc_core_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rkvenc_dev *enc = NULL;
	struct mpp_dev *mpp = NULL;

	enc = devm_kzalloc(dev, sizeof(*enc), GFP_KERNEL);
	if (!enc)
		return -ENOMEM;

	mpp = &enc->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		struct device_node *np = pdev->dev.of_node;
		const struct of_device_id *match = NULL;

		match = of_match_node(mpp_rkvenc_dt_match, np);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;

		mpp->core_id = of_alias_get_id(np, "rkvenc");
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	rkvenc2_alloc_rcbbuf(pdev, enc);

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
	mpp->iommu_info->hdl = rkvenc2_iommu_fault_handle;
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
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_rkvenc_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}

	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	rkvenc2_alloc_rcbbuf(pdev, enc);

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

static int rkvenc2_free_rcbbuf(struct platform_device *pdev, struct rkvenc_dev *enc)
{
	struct iommu_domain *domain;

	if (enc->rcb_page) {
		size_t page_size = PAGE_ALIGN(enc->sram_used - enc->sram_size);

		__free_pages(enc->rcb_page, get_order(page_size));
	}
	if (enc->sram_iova) {
		domain = enc->mpp.iommu_info->domain;
		iommu_unmap(domain, enc->sram_iova, enc->sram_used);
	}

	return 0;
}

static int rkvenc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (strstr(np->name, "ccu")) {
		dev_info(dev, "remove ccu\n");
	} else if (strstr(np->name, "core")) {
		struct mpp_dev *mpp = dev_get_drvdata(dev);
		struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

		dev_info(dev, "remove core\n");
		if (enc->ccu) {
			mutex_lock(&enc->ccu->lock);
			list_del_init(&enc->core_link);
			enc->ccu->core_num--;
			mutex_unlock(&enc->ccu->lock);
		}
		rkvenc2_free_rcbbuf(pdev, enc);
		mpp_dev_remove(&enc->mpp);
		rkvenc_procfs_remove(&enc->mpp);
	} else {
		struct mpp_dev *mpp = dev_get_drvdata(dev);
		struct rkvenc_dev *enc = to_rkvenc_dev(mpp);

		dev_info(dev, "remove device\n");
		rkvenc2_free_rcbbuf(pdev, enc);
		mpp_dev_remove(mpp);
		rkvenc_procfs_remove(mpp);
	}

	return 0;
}

static void rkvenc_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!strstr(dev_name(dev), "ccu"))
		mpp_dev_shutdown(pdev);
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
