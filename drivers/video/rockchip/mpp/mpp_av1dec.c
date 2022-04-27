// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */

#define pr_fmt(fmt) "mpp_av1dec: " fmt

#include <asm/cacheflush.h>
#include <linux/clk.h>
#include <linux/clk/clk-conf.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_platform.h>
#include <linux/clk/clk-conf.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/proc_fs.h>
#include <soc/rockchip/pm_domains.h>

#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define AV1DEC_DRIVER_NAME		"mpp_av1dec"

#define	AV1DEC_SESSION_MAX_BUFFERS		40

/* REG_DEC_INT, bits for interrupt */
#define	AV1DEC_INT_PIC_INF		BIT(24)
#define	AV1DEC_INT_TIMEOUT		BIT(18)
#define	AV1DEC_INT_SLICE		BIT(17)
#define	AV1DEC_INT_STRM_ERROR		BIT(16)
#define	AV1DEC_INT_ASO_ERROR		BIT(15)
#define	AV1DEC_INT_BUF_EMPTY		BIT(14)
#define	AV1DEC_INT_BUS_ERROR		BIT(13)
#define	AV1DEC_DEC_INT			BIT(12)
#define	AV1DEC_DEC_INT_RAW		BIT(8)
#define	AV1DEC_DEC_IRQ_DIS		BIT(4)
#define	AV1DEC_DEC_START		BIT(0)

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))
/* REG_DEC_EN, bit for gate */
#define	AV1DEC_CLOCK_GATE_EN		BIT(10)

#define to_av1dec_info(info)		\
		container_of(info, struct av1dec_hw_info, hw)
#define to_av1dec_task(ctx)		\
		container_of(ctx, struct av1dec_task, mpp_task)
#define to_av1dec_dev(dev)		\
		container_of(dev, struct av1dec_dev, mpp)

/* define functions */
#define MPP_GET_BITS(v, p, b)	(((v) >> (p)) & ((1 << (b)) - 1))
#define MPP_BASE_TO_IDX(a)	((a) / sizeof(u32))

enum AV1DEC_CLASS_TYPE {
	AV1DEC_CLASS_VCD	= 0,
	AV1DEC_CLASS_CACHE	= 1,
	AV1DEC_CLASS_AFBC	= 2,
	AV1DEC_CLASS_BUTT,
};

enum av1dec_trans_type {
	AV1DEC_TRANS_BASE	= 0x0000,

	AV1DEC_TRANS_VCD	= AV1DEC_TRANS_BASE + 0,
	AV1DEC_TRANS_CACHE	= AV1DEC_TRANS_BASE + 1,
	AV1DEC_TRANS_AFBC	= AV1DEC_TRANS_BASE + 2,
	AV1DEC_TRANS_BUTT,
};

struct av1dec_hw_info {
	struct mpp_hw_info hw;
	/* register range by class */
	u32 reg_class_num;
	struct {
		u32 base_s;
		u32 base_e;
	} reg_class[AV1DEC_CLASS_BUTT];
	/* fd translate for class */
	u32 trans_class_num;
	struct {
		u32 class;
		u32 trans_fmt;
	} trans_class[AV1DEC_TRANS_BUTT];

	/* interrupt config register */
	int int_base;
	/* enable hardware register */
	int en_base;
	/* status register */
	int sta_base;
	/* clear irq register */
	int clr_base;
	/* stream register */
	int strm_base;

	u32 err_mask;
};

struct av1dec_task {
	struct mpp_task mpp_task;

	struct av1dec_hw_info *hw_info;
	/* for malloc register data buffer */
	u32 *reg_data;
	/* class register */
	struct {
		u32 valid;
		u32 base;
		u32 *data;
		/* offset base reg_data */
		u32 off;
		/* length for class */
		u32 len;
	} reg_class[AV1DEC_CLASS_BUTT];
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

struct av1dec_dev {
	struct mpp_dev mpp;
	struct av1dec_hw_info *hw_info;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	u32 default_max_load;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;

	void __iomem *reg_base[AV1DEC_CLASS_BUTT];
	int irq[AV1DEC_CLASS_BUTT];
};

static struct av1dec_hw_info av1dec_hw_info = {
	.hw = {
		.reg_num = 512,
		.reg_id = 0,
		.reg_en = 1,
		.reg_start = 1,
		.reg_end = 319,
	},
	.reg_class_num = 3,
	.reg_class[AV1DEC_CLASS_VCD] = {
		.base_s = 0x0000,
		.base_e = 0x07fc,
	},
	.reg_class[AV1DEC_CLASS_CACHE] = {
		.base_s = 0x10000,
		.base_e = 0x10294,
	},
	.reg_class[AV1DEC_CLASS_AFBC] = {
		.base_s = 0x20000,
		.base_e = 0x2034c,
	},
	.trans_class_num = AV1DEC_TRANS_BUTT,
	.trans_class[AV1DEC_CLASS_VCD] = {
		.class = AV1DEC_CLASS_VCD,
		.trans_fmt = AV1DEC_TRANS_VCD,
	},
	.trans_class[AV1DEC_CLASS_CACHE] = {
		.class = AV1DEC_CLASS_CACHE,
		.trans_fmt = AV1DEC_TRANS_CACHE,
	},
	.trans_class[AV1DEC_CLASS_AFBC] = {
		.class = AV1DEC_CLASS_AFBC,
		.trans_fmt = AV1DEC_TRANS_AFBC,
	},
	.int_base = 0x0004,
	.en_base = 0x0004,
	.sta_base = 0x0004,
	.clr_base = 0x0004,
	.strm_base = 0x02a4,
	.err_mask = 0x7e000,
};

/*
 * file handle translate information for v2
 */
static const u16 trans_tbl_av1_vcd[] = {
	65, 67, 69, 71, 73, 75, 77, 79, 81, 83, 85, 87, 89, 91,
	93, 95, 97, 99, 101, 103, 105, 107, 109, 111, 113, 115,
	117, 133, 135, 137, 139, 141, 143, 145, 147,
	167, 169, 171, 173, 175, 177, 179, 183, 190, 192, 194,
	196, 198, 200, 202, 204, 224, 226, 228, 230, 232, 234,
	236, 238, 326, 328, 339, 341, 348, 350, 505, 507
};

static const u16 trans_tbl_av1_cache[] = {
	13, 18, 23, 28, 33, 38, 43, 48, 53, 58, 63, 68, 73, 78, 83, 88,
	134, 135, 138, 139, 142, 143, 146, 147,
};

static const u16 trans_tbl_av1_afbc[] = {
	32, 33, 34, 35, 48, 49, 50, 51, 96, 97, 98, 99
};

static struct mpp_trans_info trans_av1dec[] = {
	[AV1DEC_TRANS_VCD] = {
		.count = ARRAY_SIZE(trans_tbl_av1_vcd),
		.table = trans_tbl_av1_vcd,
	},
	[AV1DEC_TRANS_CACHE] = {
		.count = ARRAY_SIZE(trans_tbl_av1_cache),
		.table = trans_tbl_av1_cache,
	},
	[AV1DEC_TRANS_AFBC] = {
		.count = ARRAY_SIZE(trans_tbl_av1_afbc),
		.table = trans_tbl_av1_afbc,
	},
};

static bool req_over_class(struct mpp_request *req,
			   struct av1dec_task *task, int class)
{
	bool ret;
	u32 base_s, base_e, req_e;
	struct av1dec_hw_info *hw = task->hw_info;

	if (class > hw->reg_class_num)
		return false;

	base_s = hw->reg_class[class].base_s;
	base_e = hw->reg_class[class].base_e;
	req_e = req->offset + req->size - sizeof(u32);

	ret = (req->offset <= base_e && req_e >= base_s) ? true : false;

	return ret;
}

static int av1dec_alloc_reg_class(struct av1dec_task *task)
{
	int i;
	u32 data_size;
	struct av1dec_hw_info *hw = task->hw_info;

	data_size = 0;
	for (i = 0; i < hw->reg_class_num; i++) {
		u32 base_s = hw->reg_class[i].base_s;
		u32 base_e = hw->reg_class[i].base_e;

		task->reg_class[i].base = base_s;
		task->reg_class[i].off = data_size;
		task->reg_class[i].len = base_e - base_s + sizeof(u32);
		data_size += task->reg_class[i].len;
	}

	task->reg_data = kzalloc(data_size, GFP_KERNEL);
	if (!task->reg_data)
		return -ENOMEM;

	for (i = 0; i < hw->reg_class_num; i++)
		task->reg_class[i].data = task->reg_data + (task->reg_class[i].off / sizeof(u32));

	return 0;
}

static int av1dec_update_req(struct av1dec_task *task, int class,
			     struct mpp_request *req_in,
			     struct mpp_request *req_out)
{
	u32 base_s, base_e, req_e, s, e;
	struct av1dec_hw_info *hw = task->hw_info;

	if (class > hw->reg_class_num)
		return -EINVAL;

	base_s = hw->reg_class[class].base_s;
	base_e = hw->reg_class[class].base_e;
	req_e = req_in->offset + req_in->size - sizeof(u32);
	s = max(req_in->offset, base_s);
	e = min(req_e, base_e);

	req_out->offset = s;
	req_out->size = e - s + sizeof(u32);
	req_out->data = (u8 *)req_in->data + (s - req_in->offset);
	mpp_debug(DEBUG_TASK_INFO, "req_out->offset=%08x, req_out->size=%d\n",
		  req_out->offset, req_out->size);

	return 0;
}

static int av1dec_extract_task_msg(struct av1dec_task *task,
				   struct mpp_task_msgs *msgs)
{
	int ret;
	u32 i;
	struct mpp_request *req;
	struct av1dec_hw_info *hw = task->hw_info;

	mpp_debug_enter();

	mpp_debug(DEBUG_TASK_INFO, "req_cnt=%d, set_cnt=%d, poll_cnt=%d, reg_class=%d\n",
		msgs->req_cnt, msgs->set_cnt, msgs->poll_cnt, hw->reg_class_num);

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		mpp_debug(DEBUG_TASK_INFO, "msg: cmd %08x, offset %08x, size %d\n",
			req->cmd, req->offset, req->size);
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			u32 class;
			u32 base, *regs;
			struct mpp_request *wreq;

			for (class = 0; class < hw->reg_class_num; class++) {
				if (!req_over_class(req, task, class))
					continue;
				mpp_debug(DEBUG_TASK_INFO, "found write_calss %d\n", class);
				wreq = &task->w_reqs[task->w_req_cnt];
				av1dec_update_req(task, class, req, wreq);

				base = task->reg_class[class].base;
				regs = (u32 *)task->reg_class[class].data;
				regs += MPP_BASE_TO_IDX(req->offset - base);
				if (copy_from_user(regs, wreq->data, wreq->size)) {
					mpp_err("copy_from_user fail, offset %08x\n", wreq->offset);
					ret = -EIO;
					goto fail;
				}
				task->w_req_cnt++;
			}
		} break;
		case MPP_CMD_SET_REG_READ: {
			u32 class;
			struct mpp_request *rreq;

			for (class = 0; class < hw->reg_class_num; class++) {
				if (!req_over_class(req, task, class))
					continue;
				mpp_debug(DEBUG_TASK_INFO, "found read_calss %d\n", class);
				rreq = &task->r_reqs[task->r_req_cnt];
				av1dec_update_req(task, class, req, rreq);
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

	mpp_debug_leave();
	return 0;

fail:
	mpp_debug_leave();
	return ret;
}

static void *av1dec_alloc_task(struct mpp_session *session,
			       struct mpp_task_msgs *msgs)
{
	int ret;
	u32 i, j;
	struct mpp_task *mpp_task = NULL;
	struct av1dec_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task = &task->mpp_task;
	mpp_task_init(session, mpp_task);
	mpp_task->hw_info = mpp->var->hw_info;
	task->hw_info = to_av1dec_info(mpp_task->hw_info);

	/* alloc reg data for task */
	ret = av1dec_alloc_reg_class(task);
	if (ret)
		goto free_task;
	mpp_task->reg = task->reg_class[0].data;
	/* extract reqs for current task */
	ret = av1dec_extract_task_msg(task, msgs);
	if (ret)
		goto free_reg_class;

	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		int cnt;
		const u16 *tbl;
		u32 offset;
		struct av1dec_hw_info *hw = task->hw_info;

		for (i = 0; i < task->w_req_cnt; i++) {
			struct mpp_request *req = &task->w_reqs[i];

			for (i = 0; i < hw->trans_class_num; i++) {
				u32 class = hw->trans_class[i].class;
				u32 fmt = hw->trans_class[i].trans_fmt;
				u32 *reg = task->reg_class[class].data;
				u32 base_idx = MPP_BASE_TO_IDX(task->reg_class[class].base);

				if (!req_over_class(req, task, i))
					continue;
				mpp_debug(DEBUG_TASK_INFO, "class=%d, base_idx=%d\n",
					  class, base_idx);
				if (!reg)
					continue;

				ret = mpp_translate_reg_address(session, mpp_task, fmt, reg, NULL);
				if (ret)
					goto fail;

				cnt = mpp->var->trans_info[fmt].count;
				tbl = mpp->var->trans_info[fmt].table;
				for (j = 0; j < cnt; j++) {
					offset = mpp_query_reg_offset_info(&task->off_inf,
									tbl[j] + base_idx);
					mpp_debug(DEBUG_IOMMU,
						"reg[%d] + offset %d\n", tbl[j] + base_idx, offset);
					reg[tbl[j]] += offset;
				}
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
free_reg_class:
	kfree(task->reg_data);
free_task:
	kfree(task);

	return NULL;
}
#define AV1_PP_CONFIG_INDEX	321
#define AV1_PP_TILE_SIZE	GENMASK_ULL(10, 9)
#define AV1_PP_TILE_16X16	BIT(10)

#define AV1_PP_OUT_LUMA_ADR_INDEX	326
#define AV1_PP_OUT_CHROMA_ADR_INDEX	328

#define AV1_L2_CACHE_SHAPER_CTRL	0x20
#define AV1_L2_CACHE_SHAPER_EN		BIT(0)
#define AV1_L2_CACHE_INT_MASK		0x30
#define AV1_L2_CACHE_PP0_Y_CONFIG0	0x84
#define AV1_L2_CACHE_PP0_Y_CONFIG2	0x8c
#define AV1_L2_CACHE_PP0_Y_CONFIG3	0x90
#define AV1_L2_CACHE_PP0_U_CONFIG0	0x98
#define AV1_L2_CACHE_PP0_U_CONFIG2	0xa0
#define AV1_L2_CACHE_PP0_U_CONFIG3	0xa4

#define AV1_L2_CACHE_RD_ONLY_CTRL	0x204
#define AV1_L2_CACHE_RD_ONLY_CONFIG	0x208

static int av1dec_set_l2_cache(struct av1dec_dev *dec, struct av1dec_task *task)
{
	int val;
	u32 *regs = (u32 *)task->reg_class[0].data;
	u32 width = (regs[4] >> 19) * 8;
	u32 height = ((regs[4] >> 6) & 0x1fff) * 8;
	u32 pixel_width = (((regs[322]) >> 27) & 0x1F) == 1 ? 8 : 16;
	u32 pre_fetch_height = 136;
	u32 max_h;
	u32 line_cnt;
	u32 line_size;
	u32 line_stride;

	/* channel 4, PPU0_Y Configuration */
	/* afbc sharper can't use open cache.
	 * afbc out must be tile 16x16.
	 */
	if ((regs[AV1_PP_CONFIG_INDEX] & AV1_PP_TILE_SIZE) != AV1_PP_TILE_16X16) {
		line_size = MPP_ALIGN(MPP_ALIGN(width * pixel_width, 8) / 8, 16);
		line_stride = MPP_ALIGN(MPP_ALIGN(width * pixel_width, 8) / 8, 16) >> 4;
		line_cnt = height;
		max_h = pre_fetch_height;

		writel_relaxed(regs[AV1_PP_OUT_LUMA_ADR_INDEX] + 0x1,
			       dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_Y_CONFIG0);
		val = line_size | (line_stride << 16);
		writel_relaxed(val, dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_Y_CONFIG2);

		val = line_cnt | (max_h << 16);
		writel_relaxed(val, dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_Y_CONFIG3);

		/* channel 5, PPU0_U Configuration */
		line_size = MPP_ALIGN(MPP_ALIGN(width * pixel_width, 8) / 8, 16);
		line_stride = MPP_ALIGN(MPP_ALIGN(width * pixel_width, 8) / 8, 16) >> 4;
		line_cnt = height >> 1;
		max_h = pre_fetch_height >> 1;

		writel_relaxed(regs[AV1_PP_OUT_CHROMA_ADR_INDEX] + 0x1,
			       dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_U_CONFIG0);
		val = line_size | (line_stride << 16);
		writel_relaxed(val, dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_U_CONFIG2);

		val = line_cnt | (max_h << 16);
		writel_relaxed(val, dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_PP0_U_CONFIG3);
		/* mask cache irq */
		writel_relaxed(0xf, dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_INT_MASK);

		/* shaper enable */
		writel_relaxed(AV1_L2_CACHE_SHAPER_EN,
			       dec->reg_base[AV1DEC_CLASS_CACHE] + AV1_L2_CACHE_SHAPER_CTRL);

		/* TODO: set exception list */

		/* multi id enable bit */
		writel_relaxed(0x00000001, dec->reg_base[AV1DEC_CLASS_CACHE] +
			       AV1_L2_CACHE_RD_ONLY_CONFIG);
		/* reorder_e and cache_e */
		writel_relaxed(0x00000081, dec->reg_base[AV1DEC_CLASS_CACHE] +
			       AV1_L2_CACHE_RD_ONLY_CTRL);
		/* wmb */
		wmb();
	}

	return 0;
}
#define REG_CONTROL		0x20
#define REG_INTRENBL		0x34
#define REG_ACKNOWLEDGE		0x38
#define REG_FORMAT		0x100
#define REG_COMPRESSENABLE	0x340
#define REG_HEADERBASE		0x80
#define REG_PAYLOADBASE		0xC0
#define REG_INPUTBUFBASE	0x180
#define REG_INPUTBUFSTRIDE	0x200
#define REG_INPUTBUFSIZE	0x140

static int av1dec_set_afbc(struct av1dec_dev *dec, struct av1dec_task *task)
{
	u32 *regs = (u32 *)task->reg_class[0].data;
	u32 width = (regs[4] >> 19) * 8;
	u32 height = ((regs[4] >> 6) & 0x1fff) * 8;
	u32 pixel_width_y, pixel_width_c, pixel_width = 8;
	u32 vir_top  =  (((regs[503]) >> 16) & 0xf);
	u32 vir_left  =  (((regs[503]) >> 20) & 0xf);
	u32 vir_bottom = (((regs[503]) >> 24) & 0xf);
	u32 vir_right  =  (((regs[503]) >> 28) & 0xf);
	u32 fbc_format = 0;
	u32 fbc_stream_number = 0;
	u32 fbc_comp_en[2] = {0, 0};
	u32 pp_width_final[2] = {0, 0};
	u32 pp_height_final[2] = {0, 0};
	u32 pp_hdr_base[2] = {0, 0};
	u32 pp_payload_base[2] = {0, 0};
	u32 pp_input_base[2] = {0, 0};
	u32 pp_input_stride[2] = {0, 0};
	u32 bus_address;
	u32 i = 0;

	pixel_width_y = ((regs[8] >> 6) & 0x3) + 8;
	pixel_width_c = ((regs[8] >> 4) & 0x3) + 8;
	pixel_width = (pixel_width_y == 8 && pixel_width_c == 8) ? 8 : 10;

	if ((regs[AV1_PP_CONFIG_INDEX] & AV1_PP_TILE_SIZE) == AV1_PP_TILE_16X16) {
		u32 offset = MPP_ALIGN((vir_left + width + vir_right) *
			     (height + 28) / 16, 64);

		bus_address = regs[505];
		fbc_stream_number++;
		if (pixel_width == 10)
			fbc_format = 3;
		else
			fbc_format = 9;
		fbc_comp_en[0] = 1;
		fbc_comp_en[1] = 1;

		pp_width_final[0] = pp_width_final[1] = vir_left + width + vir_right;
		pp_height_final[0] = pp_height_final[1] = vir_top + height + vir_bottom;

		if (pixel_width == 10)
			pp_input_stride[0] = pp_input_stride[1] = 2 * pp_width_final[0];
		else
			pp_input_stride[0] = pp_input_stride[1] = pp_width_final[0];

		pp_hdr_base[0] = pp_hdr_base[1] = bus_address;
		pp_payload_base[0] = pp_payload_base[1] = bus_address + offset;
		pp_input_base[0] = pp_input_base[1] = bus_address;

		writel_relaxed((fbc_stream_number << 9),
			       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_CONTROL);
		writel_relaxed(0x1, dec->reg_base[AV1DEC_CLASS_AFBC] + REG_INTRENBL);

		for (i = 0; i < 2; i++) {
			writel_relaxed(fbc_format,
				       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_FORMAT + i * 4);
			writel_relaxed(fbc_comp_en[i], dec->reg_base[AV1DEC_CLASS_AFBC] +
				       REG_COMPRESSENABLE + i * 4);
			/* hdr base */
			writel_relaxed(pp_hdr_base[i],
				       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_HEADERBASE + i * 4);
			/* payload */
			writel_relaxed(pp_payload_base[i],
				       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_PAYLOADBASE + i * 4);
			/* bufsize */
			writel_relaxed(((pp_height_final[i] << 15) | pp_width_final[i]),
				       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_INPUTBUFSIZE + i * 4);
			/* buf */
			writel_relaxed(pp_input_base[i],
				       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_INPUTBUFBASE + i * 4);
			/* stride */
			writel_relaxed(pp_input_stride[i], dec->reg_base[AV1DEC_CLASS_AFBC] +
				       REG_INPUTBUFSTRIDE + i * 4);
		}
		/* wmb */
		wmb();
		writel(((fbc_stream_number << 9) | (1 << 7)),
		       dec->reg_base[AV1DEC_CLASS_AFBC] + REG_CONTROL); /* update */
		writel((fbc_stream_number << 9), dec->reg_base[AV1DEC_CLASS_AFBC] + REG_CONTROL);

	}
	return 0;
}

static int av1dec_run(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	int i;
	u32 en_val = 0;
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_hw_info *hw = dec->hw_info;
	struct av1dec_task *task = to_av1dec_task(mpp_task);

	mpp_debug_enter();
	mpp_iommu_flush_tlb(mpp->iommu_info);
	av1dec_set_l2_cache(dec, task);
	av1dec_set_afbc(dec, task);

	for (i = 0; i < task->w_req_cnt; i++) {
		int class;
		struct mpp_request *req = &task->w_reqs[i];

		for (class = 0; class < hw->reg_class_num; class++) {
			int j, s, e;
			u32 base, *regs;

			if (!req_over_class(req, task, class))
				continue;
			base = task->reg_class[class].base;
			s = MPP_BASE_TO_IDX(req->offset - base);
			e = s + req->size / sizeof(u32);
			regs = (u32 *)task->reg_class[class].data;

			mpp_debug(DEBUG_TASK_INFO, "found rd_class %d, base=%08x, s=%d, e=%d\n",
				  class, base, s, e);
			for (j = s; j < e; j++) {
				if (class == 0 && j == hw->hw.reg_en) {
					en_val = regs[j];
					continue;
				}
				writel_relaxed(regs[j], dec->reg_base[class] + j * sizeof(u32));
			}
		}
	}

	/* init current task */
	mpp->cur_task = mpp_task;
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, hw->en_base, en_val);

	mpp_debug_leave();

	return 0;
}

static int av1dec_vcd_irq(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_hw_info *hw = dec->hw_info;

	mpp_debug_enter();

	mpp->irq_status = mpp_read(mpp, hw->sta_base);
	if (!mpp->irq_status)
		return IRQ_NONE;

	mpp_write(mpp, hw->clr_base, 0);

	mpp_debug_leave();

	return IRQ_WAKE_THREAD;
}

static int av1dec_isr(struct mpp_dev *mpp)
{
	struct mpp_task *mpp_task = mpp->cur_task;
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_task *task = to_av1dec_task(mpp_task);
	u32 *regs = (u32 *)task->reg_class[0].data;

	mpp_debug_enter();

	/* FIXME use a spin lock here */
	if (!mpp_task) {
		dev_err(mpp->dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;

	/* clear l2 cache status */
	writel_relaxed(0x0, dec->reg_base[AV1DEC_CLASS_CACHE] + 0x020);
	writel_relaxed(0x0, dec->reg_base[AV1DEC_CLASS_CACHE] + 0x204);
	/* multi id enable bit */
	writel_relaxed(0x00000000, dec->reg_base[AV1DEC_CLASS_CACHE] + 0x208);

	if (((regs[321] >> 9) & 0x3) == 0x2) {
		u32 ack_status = readl(dec->reg_base[AV1DEC_CLASS_AFBC] + REG_ACKNOWLEDGE);

		if ((ack_status & 0x1) == 0x1) {
			u32 ctl_val = readl(dec->reg_base[AV1DEC_CLASS_AFBC] + REG_CONTROL);

			ctl_val |= 1;
			writel_relaxed(ctl_val, dec->reg_base[AV1DEC_CLASS_AFBC] + REG_CONTROL);
		}
	}
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n", task->irq_status);
	if (task->irq_status & dec->hw_info->err_mask) {
		atomic_inc(&mpp->reset_request);
		/* dump register */
		if (mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
			mpp_debug(DEBUG_DUMP_ERR_REG, "irq_status: %08x\n",
				  task->irq_status);
			mpp_task_dump_hw_reg(mpp);
		}
	}
	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static int av1dec_finish(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	u32 i;
	struct av1dec_task *task = to_av1dec_task(mpp_task);
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_hw_info *hw = dec->hw_info;

	mpp_debug_enter();

	for (i = 0; i < task->r_req_cnt; i++) {
		int class;
		struct mpp_request *req = &task->r_reqs[i];

		for (class = 0; class < hw->reg_class_num; class++) {
			int j, s, e;
			u32 base, *regs;

			if (!req_over_class(req, task, class))
				continue;
			base = task->reg_class[class].base;
			s = MPP_BASE_TO_IDX(req->offset - base);
			e = s + req->size / sizeof(u32);
			regs = (u32 *)task->reg_class[class].data;

			mpp_debug(DEBUG_TASK_INFO, "found rd_class %d, base=%08x, s=%d, e=%d\n",
				  class, base, s, e);
			for (j = s; j < e; j++) {
				/* revert hack for irq status */
				if (class == 0 && j == MPP_BASE_TO_IDX(hw->sta_base)) {
					regs[j] = task->irq_status;
					continue;
				}
				regs[j] = readl_relaxed(dec->reg_base[class] + j * sizeof(u32));
			}
		}
	}

	mpp_debug_leave();

	return 0;
}

static int av1dec_result(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task,
			 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct av1dec_task *task = to_av1dec_task(mpp_task);
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_hw_info *hw = dec->hw_info;

	mpp_debug_enter();

	for (i = 0; i < task->r_req_cnt; i++) {
		int class;
		struct mpp_request *req = &task->r_reqs[i];

		for (class = 0; class < hw->reg_class_num; class++) {
			u32 base, *regs;

			if (!req_over_class(req, task, class))
				continue;
			base = task->reg_class[class].base;
			regs = (u32 *)task->reg_class[class].data;
			regs += MPP_BASE_TO_IDX(req->offset - base);

			if (copy_to_user(req->data, regs, req->size)) {
				mpp_err("copy_to_user reg fail\n");
				return -EIO;
			}
		}
	}
	mpp_debug_leave();

	return 0;
}

static int av1dec_free_task(struct mpp_session *session,
			    struct mpp_task *mpp_task)
{
	struct av1dec_task *task = to_av1dec_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task->reg_data);
	kfree(task);

	return 0;
}

#ifdef CONFIG_PROC_FS
static int av1dec_procfs_remove(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);

	if (dec->procfs) {
		proc_remove(dec->procfs);
		dec->procfs = NULL;
	}

	return 0;
}

static int av1dec_procfs_init(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);

	dec->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(dec->procfs)) {
		mpp_err("failed on open procfs\n");
		dec->procfs = NULL;
		return -EIO;
	}
	/* for debug */
	mpp_procfs_create_u32("aclk", 0644,
			      dec->procfs, &dec->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      dec->procfs, &mpp->session_max_buffers);

	return 0;
}
#else
static inline int av1dec_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int av1dec_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

static int av1dec_init(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	int ret = 0;

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &dec->aclk_info, "aclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get aclk_vcodec\n");
	ret = mpp_get_clk_info(mpp, &dec->hclk_info, "hclk_vcodec");
	if (ret)
		mpp_err("failed on clk_get hclk_vcodec\n");

	/* Get normal max workload from dtsi */
	of_property_read_u32(mpp->dev->of_node,
			     "rockchip,default-max-load",
			     &dec->default_max_load);
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

static int av1dec_reset(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);

	mpp_debug_enter();

	if (dec->rst_a && dec->rst_h) {
		rockchip_pmu_idle_request(mpp->dev, true);
		mpp_safe_reset(dec->rst_a);
		mpp_safe_reset(dec->rst_h);
		udelay(5);
		mpp_safe_unreset(dec->rst_a);
		mpp_safe_unreset(dec->rst_h);
		rockchip_pmu_idle_request(mpp->dev, false);
	}

	mpp_debug_leave();

	return 0;
}

static int av1dec_clk_on(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);

	mpp_clk_safe_enable(dec->aclk_info.clk);
	mpp_clk_safe_enable(dec->hclk_info.clk);

	return 0;
}

static int av1dec_clk_off(struct mpp_dev *mpp)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);

	clk_disable_unprepare(dec->aclk_info.clk);
	clk_disable_unprepare(dec->hclk_info.clk);

	return 0;
}

static int av1dec_set_freq(struct mpp_dev *mpp,
			   struct mpp_task *mpp_task)
{
	struct av1dec_dev *dec = to_av1dec_dev(mpp);
	struct av1dec_task *task = to_av1dec_task(mpp_task);

	mpp_clk_set_rate(&dec->aclk_info, task->clk_mode);

	return 0;
}

static struct mpp_hw_ops av1dec_hw_ops = {
	.init = av1dec_init,
	.clk_on = av1dec_clk_on,
	.clk_off = av1dec_clk_off,
	.set_freq = av1dec_set_freq,
	.reset = av1dec_reset,
};

static struct mpp_dev_ops av1dec_dev_ops = {
	.alloc_task = av1dec_alloc_task,
	.run = av1dec_run,
	.irq = av1dec_vcd_irq,
	.isr = av1dec_isr,
	.finish = av1dec_finish,
	.result = av1dec_result,
	.free_task = av1dec_free_task,
};
static const struct mpp_dev_var av1dec_data = {
	.device_type = MPP_DEVICE_AV1DEC,
	.hw_info = &av1dec_hw_info.hw,
	.trans_info = trans_av1dec,
	.hw_ops = &av1dec_hw_ops,
	.dev_ops = &av1dec_dev_ops,
};

static const struct of_device_id mpp_av1dec_dt_match[] = {
	{
		.compatible = "rockchip,av1-decoder",
		.data = &av1dec_data,
	},
	{},
};

static int av1dec_device_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int av1dec_device_probe(struct device *dev)
{
	int ret;
	const struct platform_driver *drv;
	struct platform_device *pdev = to_platform_device(dev);

	ret = of_clk_set_defaults(dev->of_node, false);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_attach(dev, true);
	if (ret)
		return ret;

	drv = to_platform_driver(dev->driver);
	if (drv->probe) {
		ret = drv->probe(pdev);
		if (ret)
			dev_pm_domain_detach(dev, true);
	}

	return ret;
}

static int av1dec_device_remove(struct device *dev)
{

	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);

	if (dev->driver && drv->remove)
		drv->remove(pdev);

	dev_pm_domain_detach(dev, true);

	return 0;
}

static void av1dec_device_shutdown(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *drv = to_platform_driver(dev->driver);

	if (dev->driver && drv->shutdown)
		drv->shutdown(pdev);
}

static int av1dec_dma_configure(struct device *dev)
{
	return of_dma_configure(dev, dev->of_node, true);
}

static const struct dev_pm_ops platform_dev_pm_ops = {
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
};

struct bus_type av1dec_bus = {
	.name		= "av1dec_bus",
	.match		= av1dec_device_match,
	.probe		= av1dec_device_probe,
	.remove		= av1dec_device_remove,
	.shutdown	= av1dec_device_shutdown,
	.dma_configure  = av1dec_dma_configure,
	.pm		= &platform_dev_pm_ops,
};

static int av1_of_device_add(struct platform_device *ofdev)
{
	WARN_ON(ofdev->dev.of_node == NULL);

	/* name and id have to be set so that the platform bus doesn't get
	 * confused on matching
	 */
	ofdev->name = dev_name(&ofdev->dev);
	ofdev->id = PLATFORM_DEVID_NONE;

	/*
	 * If this device has not binding numa node in devicetree, that is
	 * of_node_to_nid returns NUMA_NO_NODE. device_add will assume that this
	 * device is on the same node as the parent.
	 */
	set_dev_node(&ofdev->dev, of_node_to_nid(ofdev->dev.of_node));

	return device_add(&ofdev->dev);
}

static struct platform_device *av1dec_device_create(void)
{
	int ret = -ENODEV;
	struct device_node *root, *child;
	struct platform_device *pdev;

	root = of_find_node_by_path("/");

	for_each_child_of_node(root, child) {
		if (!of_match_node(mpp_av1dec_dt_match, child))
			continue;

		pr_info("Adding child %pOF\n", child);

		pdev = of_device_alloc(child, "av1d-master", NULL);
		if (!pdev)
			return ERR_PTR(-ENOMEM);

		pdev->dev.bus = &av1dec_bus;

		dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));

		ret = av1_of_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			return ERR_PTR(-EINVAL);
		}

		pr_info("register device %s\n", dev_name(&pdev->dev));

		return  pdev;
	}

	return ERR_PTR(ret);
}

static void av1dec_device_destory(void)
{
	struct platform_device *pdev;
	struct device *dev;

	dev = bus_find_device_by_name(&av1dec_bus, NULL, "av1d-master");
	pdev = dev ? to_platform_device(dev) : NULL;
	if (!pdev) {
		pr_err("cannot find platform device\n");
		return;
	}

	pr_info("destroy device %s\n", dev_name(&pdev->dev));
	platform_device_del(pdev);
	platform_device_put(pdev);
}

void av1dec_driver_unregister(struct platform_driver *drv)
{
	/* 1. unregister av1 driver */
	driver_unregister(&drv->driver);
	/* 2. release device */
	av1dec_device_destory();
	/* 3. unregister iommu driver */
	platform_driver_unregister(&rockchip_av1_iommu_driver);
	/* 4. unregister bus */
	bus_unregister(&av1dec_bus);
}

int av1dec_driver_register(struct platform_driver *drv)
{
	int ret;
	/* 1. register bus */
	ret = bus_register(&av1dec_bus);
	if (ret) {
		pr_err("failed to register av1 bus: %d\n", ret);
		return ret;
	}
	/* 2. register iommu driver */
	platform_driver_register(&rockchip_av1_iommu_driver);
	/* 3. create device */
	av1dec_device_create();
	/* 4. register av1 driver */
	return driver_register(&drv->driver);
}

static irqreturn_t av1dec_cache_irq(int irq, void *dev_id)
{
	struct av1dec_dev *dec = dev_id;
	u32 shaper_st, rd_st;

	shaper_st = readl(dec->reg_base[AV1DEC_CLASS_CACHE] + 0x2c);
	rd_st = readl(dec->reg_base[AV1DEC_CLASS_CACHE] + 0x204);

	mpp_debug(DEBUG_IRQ_STATUS, "cache irq st shaper 0x%x read 0x%x\n", shaper_st, rd_st);

	writel(shaper_st, dec->reg_base[AV1DEC_CLASS_CACHE] + 0x2c);
	writel(rd_st, dec->reg_base[AV1DEC_CLASS_CACHE] + 0x204);

	return IRQ_HANDLED;
}

static int av1dec_cache_init(struct platform_device *pdev, struct av1dec_dev *dec)
{
	int ret;
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cache");
	if (!res)
		return -ENOMEM;

	dec->reg_base[AV1DEC_CLASS_CACHE] = devm_ioremap(dev, res->start, resource_size(res));
	if (!dec->reg_base[AV1DEC_CLASS_CACHE]) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		return -EINVAL;
	}

	dec->irq[AV1DEC_CLASS_CACHE] = platform_get_irq(pdev, 1);

	ret = devm_request_irq(dev, dec->irq[AV1DEC_CLASS_CACHE],
			       av1dec_cache_irq, IRQF_SHARED, "irq_cache", dec);
	if (ret)
		mpp_err("ret=%d\n", ret);
	return ret;
}

static int av1dec_afbc_init(struct platform_device *pdev, struct av1dec_dev *dec)
{
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "afbc");
	if (!res)
		return -ENOMEM;

	dec->reg_base[AV1DEC_CLASS_AFBC] = devm_ioremap(dev, res->start, resource_size(res));
	if (!dec->reg_base[AV1DEC_CLASS_AFBC]) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		return -EINVAL;
	}
	dec->irq[AV1DEC_CLASS_AFBC] = platform_get_irq(pdev, 2);

	return 0;
}

static int av1dec_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct av1dec_dev *dec = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;

	dev_info(dev, "probing start\n");

	dec = devm_kzalloc(dev, sizeof(*dec), GFP_KERNEL);
	if (!dec)
		return -ENOMEM;

	mpp = &dec->mpp;
	platform_set_drvdata(pdev, dec);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_av1dec_dt_match, pdev->dev.of_node);
		if (match)
			mpp->var = (struct mpp_dev_var *)match->data;
	}
	/* get vcd resource */
	ret = mpp_dev_probe(mpp, pdev);
	if (ret)
		return ret;

	/* iommu may disabled */
	if (mpp->iommu_info)
		mpp->iommu_info->av1d_iommu = 1;

	dec->reg_base[AV1DEC_CLASS_VCD] = mpp->reg_base;
	ret = devm_request_threaded_irq(dev, mpp->irq,
					mpp_dev_irq,
					mpp_dev_isr_sched,
					IRQF_SHARED,
					dev_name(dev), mpp);
	if (ret) {
		dev_err(dev, "register interrupter runtime failed\n");
		goto failed_get_irq;
	}
	dec->irq[AV1DEC_CLASS_VCD] = mpp->irq;
	/* get cache resource */
	ret = av1dec_cache_init(pdev, dec);
	if (ret)
		goto failed_get_irq;
	/* get afbc resource */
	ret = av1dec_afbc_init(pdev, dec);
	if (ret)
		goto failed_get_irq;
	mpp->session_max_buffers = AV1DEC_SESSION_MAX_BUFFERS;
	dec->hw_info = to_av1dec_info(mpp->var->hw_info);
	av1dec_procfs_init(mpp);
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;

failed_get_irq:
	mpp_dev_remove(mpp);

	return ret;
}

static int av1dec_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct av1dec_dev *dec = platform_get_drvdata(pdev);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(&dec->mpp);
	av1dec_procfs_remove(&dec->mpp);

	return 0;
}

static void av1dec_shutdown(struct platform_device *pdev)
{
	int ret;
	int val;
	struct device *dev = &pdev->dev;
	struct av1dec_dev *dec = platform_get_drvdata(pdev);
	struct mpp_dev *mpp = &dec->mpp;

	dev_info(dev, "shutdown device\n");

	atomic_inc(&mpp->srv->shutdown_request);
	ret = readx_poll_timeout(atomic_read,
				 &mpp->task_count,
				 val, val == 0, 1000, 200000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "wait total running time out\n");

	dev_info(dev, "shutdown success\n");
}

struct platform_driver rockchip_av1dec_driver = {
	.probe = av1dec_probe,
	.remove = av1dec_remove,
	.shutdown = av1dec_shutdown,
	.driver = {
		.name = AV1DEC_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_av1dec_dt_match),
		.bus = &av1dec_bus,
	},
};
