// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 *
 * author:
 *	Ding Wei, leo.ding@rock-chips.com
 *	Alpha Lin, alpha.lin@rock-chips.com
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
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/proc_fs.h>
#include <soc/rockchip/pm_domains.h>

#include "rockchip_iep2_regs.h"
#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

#define IEP2_DRIVER_NAME		"mpp-iep2"

#define	IEP2_SESSION_MAX_BUFFERS		20

#define TILE_WIDTH		16
#define TILE_HEIGHT		4
#define MVL			28
#define MVR			27

enum rockchip_iep2_fmt {
	ROCKCHIP_IEP2_FMT_YUV422 = 2,
	ROCKCHIP_IEP2_FMT_YUV420
};

enum rockchip_iep2_yuv_swap {
	ROCKCHIP_IEP2_YUV_SWAP_SP_UV,
	ROCKCHIP_IEP2_YUV_SWAP_SP_VU,
	ROCKCHIP_IEP2_YUV_SWAP_P0,
	ROCKCHIP_IEP2_YUV_SWAP_P
};

enum rockchip_iep2_dil_ff_order {
	ROCKCHIP_IEP2_DIL_FF_ORDER_TB,
	ROCKCHIP_IEP2_DIL_FF_ORDER_BT
};

enum rockchip_iep2_dil_mode {
	ROCKCHIP_IEP2_DIL_MODE_DISABLE,
	ROCKCHIP_IEP2_DIL_MODE_I5O2,
	ROCKCHIP_IEP2_DIL_MODE_I5O1T,
	ROCKCHIP_IEP2_DIL_MODE_I5O1B,
	ROCKCHIP_IEP2_DIL_MODE_I2O2,
	ROCKCHIP_IEP2_DIL_MODE_I1O1T,
	ROCKCHIP_IEP2_DIL_MODE_I1O1B,
	ROCKCHIP_IEP2_DIL_MODE_PD,
	ROCKCHIP_IEP2_DIL_MODE_BYPASS,
	ROCKCHIP_IEP2_DIL_MODE_DECT
};

enum ROCKCHIP_IEP2_PD_COMP_FLAG {
	ROCKCHIP_IEP2_PD_COMP_FLAG_CC,
	ROCKCHIP_IEP2_PD_COMP_FLAG_CN,
	ROCKCHIP_IEP2_PD_COMP_FLAG_NC,
	ROCKCHIP_IEP2_PD_COMP_FLAG_NON
};

/* default iep2 mtn table */
static u32 iep2_mtn_tab[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x01010000, 0x06050302, 0x0f0d0a08, 0x1c191512,
	0x2b282420, 0x3634312e, 0x3d3c3a38, 0x40403f3e,
	0x40404040, 0x40404040, 0x40404040, 0x40404040
};

#define to_iep_task(task)		\
		container_of(task, struct iep_task, mpp_task)
#define to_iep2_dev(dev)		\
		container_of(dev, struct iep2_dev, mpp)

struct iep2_addr {
	u32 y;
	u32 cbcr;
	u32 cr;
};

struct iep2_params {
	u32 src_fmt;
	u32 src_yuv_swap;
	u32 dst_fmt;
	u32 dst_yuv_swap;
	u32 tile_cols;
	u32 tile_rows;
	u32 src_y_stride;
	u32 src_uv_stride;
	u32 dst_y_stride;

	/* current, previous, next. */
	struct iep2_addr src[3];
	struct iep2_addr dst[2];
	u32 mv_addr;
	u32 md_addr;

	u32 dil_mode;
	u32 dil_out_mode;
	u32 dil_field_order;

	u32 md_theta;
	u32 md_r;
	u32 md_lambda;

	u32 dect_resi_thr;
	u32 osd_area_num;
	u32 osd_gradh_thr;
	u32 osd_gradv_thr;

	u32 osd_pos_limit_en;
	u32 osd_pos_limit_num;

	u32 osd_limit_area[2];

	u32 osd_line_num;
	u32 osd_pec_thr;

	u32 osd_x_sta[8];
	u32 osd_x_end[8];
	u32 osd_y_sta[8];
	u32 osd_y_end[8];

	u32 me_pena;
	u32 mv_bonus;
	u32 mv_similar_thr;
	u32 mv_similar_num_thr0;
	s32 me_thr_offset;

	u32 mv_left_limit;
	u32 mv_right_limit;

	s8 mv_tru_list[8];
	u32 mv_tru_vld[8];

	u32 eedi_thr0;

	u32 ble_backtoma_num;

	u32 comb_cnt_thr;
	u32 comb_feature_thr;
	u32 comb_t_thr;
	u32 comb_osd_vld[8];

	u32 mtn_en;
	u32 mtn_tab[16];

	u32 pd_mode;

	u32 roi_en;
	u32 roi_layer_num;
	u32 roi_mode[8];
	u32 xsta[8];
	u32 xend[8];
	u32 ysta[8];
	u32 yend[8];
};

struct iep2_output {
	u32 mv_hist[MVL + MVR + 1];
	u32 dect_pd_tcnt;
	u32 dect_pd_bcnt;
	u32 dect_ff_cur_tcnt;
	u32 dect_ff_cur_bcnt;
	u32 dect_ff_nxt_tcnt;
	u32 dect_ff_nxt_bcnt;
	u32 dect_ff_ble_tcnt;
	u32 dect_ff_ble_bcnt;
	u32 dect_ff_nz;
	u32 dect_ff_comb_f;
	u32 dect_osd_cnt;
	u32 out_comb_cnt;
	u32 out_osd_comb_cnt;
	u32 ff_gradt_tcnt;
	u32 ff_gradt_bcnt;
	u32 x_sta[8];
	u32 x_end[8];
	u32 y_sta[8];
	u32 y_end[8];
};

struct iep_task {
	struct mpp_task mpp_task;
	struct mpp_hw_info *hw_info;

	enum MPP_CLOCK_MODE clk_mode;
	struct iep2_params params;
	struct iep2_output output;

	struct reg_offset_info off_inf;
	u32 irq_status;
	/* req for current task */
	u32 w_req_cnt;
	struct mpp_request w_reqs[MPP_MAX_MSG_NUM];
	u32 r_req_cnt;
	struct mpp_request r_reqs[MPP_MAX_MSG_NUM];
};

struct iep2_dev {
	struct mpp_dev mpp;

	struct mpp_clk_info aclk_info;
	struct mpp_clk_info hclk_info;
	struct mpp_clk_info sclk_info;
#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
	struct proc_dir_entry *procfs;
#endif
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_s;

	struct mpp_dma_buffer roi;
};

static int iep2_addr_rnum[] = {
	24, 27, 28, /* src cur */
	25, 29, 30, /* src nxt */
	26, 31, 32, /* src prv */
	44, 46, -1, /* dst top */
	45, 47, -1, /* dst bot */
	34, /* mv */
	33, /* md */
};

static int iep2_process_reg_fd(struct mpp_session *session,
			       struct iep_task *task,
			       struct mpp_task_msgs *msgs)
{
	int i;
	/* see the detail at above table iep2_addr_rnum */
	int addr_num =
		ARRAY_SIZE(task->params.src) * 3 +
		ARRAY_SIZE(task->params.dst) * 3 + 2;

	u32 *paddr = &task->params.src[0].y;

	for (i = 0; i < addr_num; ++i) {
		int usr_fd;
		u32 offset;
		struct mpp_mem_region *mem_region = NULL;

		if (session->msg_flags & MPP_FLAGS_REG_NO_OFFSET) {
			usr_fd = paddr[i];
			offset = 0;
		} else {
			usr_fd = paddr[i] & 0x3ff;
			offset = paddr[i] >> 10;
		}

		if (usr_fd == 0 || iep2_addr_rnum[i] == -1)
			continue;

		mem_region = mpp_task_attach_fd(&task->mpp_task, usr_fd);
		if (IS_ERR(mem_region)) {
			mpp_err("reg[%03d]: %08x failed\n",
				iep2_addr_rnum[i], paddr[i]);
			return PTR_ERR(mem_region);
		}

		mem_region->reg_idx = iep2_addr_rnum[i];
		mpp_debug(DEBUG_IOMMU, "reg[%3d]: %3d => %pad + offset %10d\n",
			  iep2_addr_rnum[i], usr_fd, &mem_region->iova, offset);
		paddr[i] = mem_region->iova + offset;
	}

	return 0;
}

static int iep2_extract_task_msg(struct iep_task *task,
				 struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;

	for (i = 0; i < msgs->req_cnt; i++) {
		req = &msgs->reqs[i];
		if (!req->size)
			continue;

		switch (req->cmd) {
		case MPP_CMD_SET_REG_WRITE: {
			if (copy_from_user(&task->params,
					   req->data, req->size)) {
				mpp_err("copy_from_user params failed\n");
				return -EIO;
			}
		} break;
		case MPP_CMD_SET_REG_READ: {
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

static void *iep2_alloc_task(struct mpp_session *session,
			     struct mpp_task_msgs *msgs)
{
	int ret;
	struct iep_task *task = NULL;

	mpp_debug_enter();

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	mpp_task_init(session, &task->mpp_task);
	/* extract reqs for current task */
	ret = iep2_extract_task_msg(task, msgs);
	if (ret)
		goto fail;
	/* process fd in register */
	if (!(msgs->flags & MPP_FLAGS_REG_FD_NO_TRANS)) {
		ret = iep2_process_reg_fd(session, task, msgs);
		if (ret)
			goto fail;
	}
	task->clk_mode = CLK_MODE_NORMAL;

	mpp_debug_leave();

	return &task->mpp_task;

fail:
	mpp_task_finalize(session, &task->mpp_task);
	kfree(task);
	return NULL;
}

static void iep2_config(struct mpp_dev *mpp, struct iep_task *task)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);
	struct iep2_params *cfg = &task->params;
	u32 reg;
	u32 width, height;

	width = cfg->tile_cols * TILE_WIDTH;
	height = cfg->tile_rows * TILE_HEIGHT;

	reg = IEP2_REG_SRC_FMT(cfg->src_fmt)
		| IEP2_REG_SRC_YUV_SWAP(cfg->src_yuv_swap)
		| IEP2_REG_DST_FMT(cfg->dst_fmt)
		| IEP2_REG_DST_YUV_SWAP(cfg->dst_yuv_swap)
		| IEP2_REG_DEBUG_DATA_EN;
	mpp_write_relaxed(mpp, IEP2_REG_IEP_CONFIG0, reg);

	reg = IEP2_REG_SRC_PIC_WIDTH(width - 1)
		| IEP2_REG_SRC_PIC_HEIGHT(height - 1);
	mpp_write_relaxed(mpp, IEP2_REG_SRC_IMG_SIZE, reg);

	reg = IEP2_REG_SRC_VIR_Y_STRIDE(cfg->src_y_stride)
		| IEP2_REG_SRC_VIR_UV_STRIDE(cfg->src_uv_stride);
	mpp_write_relaxed(mpp, IEP2_REG_VIR_SRC_IMG_WIDTH, reg);

	reg = IEP2_REG_DST_VIR_STRIDE(cfg->dst_y_stride);
	mpp_write_relaxed(mpp, IEP2_REG_VIR_DST_IMG_WIDTH, reg);

	reg = IEP2_REG_DIL_MV_HIST_EN
		| IEP2_REG_DIL_COMB_EN
		| IEP2_REG_DIL_BLE_EN
		| IEP2_REG_DIL_EEDI_EN
		| IEP2_REG_DIL_MEMC_EN
		| IEP2_REG_DIL_OSD_EN
		| IEP2_REG_DIL_PD_EN
		| IEP2_REG_DIL_FF_EN
		| IEP2_REG_DIL_MD_PRE_EN
		| IEP2_REG_DIL_FIELD_ORDER(cfg->dil_field_order)
		| IEP2_REG_DIL_OUT_MODE(cfg->dil_out_mode)
		| IEP2_REG_DIL_MODE(cfg->dil_mode);
	if (cfg->roi_en)
		reg |= IEP2_REG_DIL_ROI_EN;
	mpp_write_relaxed(mpp, IEP2_REG_DIL_CONFIG0, reg);

	if (cfg->dil_mode != ROCKCHIP_IEP2_DIL_MODE_PD) {
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURY,
				  cfg->src[0].y);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURUV,
				  cfg->src[0].cbcr);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURV,
				  cfg->src[0].cr);

		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTY,
				  cfg->src[1].y);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTUV,
				  cfg->src[1].cbcr);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTV,
				  cfg->src[1].cr);
	} else {
		struct iep2_addr *top, *bot;

		switch (cfg->pd_mode) {
		default:
		case ROCKCHIP_IEP2_PD_COMP_FLAG_CC:
			top = &cfg->src[0];
			bot = &cfg->src[0];
			break;
		case ROCKCHIP_IEP2_PD_COMP_FLAG_CN:
			top = &cfg->src[0];
			bot = &cfg->src[1];
			break;
		case ROCKCHIP_IEP2_PD_COMP_FLAG_NC:
			top = &cfg->src[1];
			bot = &cfg->src[0];
			break;
		}

		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURY, top->y);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURUV, top->cbcr);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_CURV, top->cr);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTY, bot->y);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTUV, bot->cbcr);
		mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_NXTV, bot->cr);
	}

	mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_PREY, cfg->src[2].y);
	mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_PREUV, cfg->src[2].cbcr);
	mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_PREV, cfg->src[2].cr);

	mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_MD, cfg->md_addr);
	mpp_write_relaxed(mpp, IEP2_REG_SRC_ADDR_MV, cfg->mv_addr);
	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_MD, cfg->md_addr);
	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_MV, cfg->mv_addr);
	mpp_write_relaxed(mpp, IEP2_REG_ROI_ADDR, (u32)iep->roi.iova);

	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_TOPY, cfg->dst[0].y);
	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_TOPC, cfg->dst[0].cbcr);
	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_BOTY, cfg->dst[1].y);
	mpp_write_relaxed(mpp, IEP2_REG_DST_ADDR_BOTC, cfg->dst[1].cbcr);

	reg = IEP2_REG_MD_THETA(cfg->md_theta)
		| IEP2_REG_MD_R(cfg->md_r)
		| IEP2_REG_MD_LAMBDA(cfg->md_lambda);
	mpp_write_relaxed(mpp, IEP2_REG_MD_CONFIG0, reg);

	reg = IEP2_REG_DECT_RESI_THR(cfg->dect_resi_thr)
		| IEP2_REG_OSD_AREA_NUM(cfg->osd_area_num)
		| IEP2_REG_OSD_GRADH_THR(cfg->osd_gradh_thr)
		| IEP2_REG_OSD_GRADV_THR(cfg->osd_gradv_thr);
	mpp_write_relaxed(mpp, IEP2_REG_DECT_CONFIG0, reg);

	reg = IEP2_REG_OSD_POS_LIMIT_NUM(cfg->osd_pos_limit_num);
	if (cfg->osd_pos_limit_en)
		reg |= IEP2_REG_OSD_POS_LIMIT_EN;
	mpp_write_relaxed(mpp, IEP2_REG_OSD_LIMIT_CONFIG, reg);

	mpp_write_relaxed(mpp, IEP2_REG_OSD_LIMIT_AREA(0),
			  cfg->osd_limit_area[0]);
	mpp_write_relaxed(mpp, IEP2_REG_OSD_LIMIT_AREA(1),
			  cfg->osd_limit_area[1]);

	reg = IEP2_REG_OSD_PEC_THR(cfg->osd_pec_thr)
		| IEP2_REG_OSD_LINE_NUM(cfg->osd_line_num);
	mpp_write_relaxed(mpp, IEP2_REG_OSD_CONFIG0, reg);

	reg = IEP2_REG_ME_PENA(cfg->me_pena)
		| IEP2_REG_MV_BONUS(cfg->mv_bonus)
		| IEP2_REG_MV_SIMILAR_THR(cfg->mv_similar_thr)
		| IEP2_REG_MV_SIMILAR_NUM_THR0(cfg->mv_similar_num_thr0)
		| IEP2_REG_ME_THR_OFFSET(cfg->me_thr_offset);
	mpp_write_relaxed(mpp, IEP2_REG_ME_CONFIG0, reg);

	reg = IEP2_REG_MV_LEFT_LIMIT((~cfg->mv_left_limit) + 1)
		| IEP2_REG_MV_RIGHT_LIMIT(cfg->mv_right_limit);
	mpp_write_relaxed(mpp, IEP2_REG_ME_LIMIT_CONFIG, reg);

	mpp_write_relaxed(mpp, IEP2_REG_EEDI_CONFIG0,
			  IEP2_REG_EEDI_THR0(cfg->eedi_thr0));
	mpp_write_relaxed(mpp, IEP2_REG_BLE_CONFIG0,
			  IEP2_REG_BLE_BACKTOMA_NUM(cfg->ble_backtoma_num));
}

static void iep2_osd_cfg(struct mpp_dev *mpp, struct iep_task *task)
{
	struct iep2_params *hw_cfg = &task->params;
	int i;
	u32 reg;

	for (i = 0; i < hw_cfg->osd_area_num; ++i) {
		reg = IEP2_REG_OSD_X_STA(hw_cfg->osd_x_sta[i])
			| IEP2_REG_OSD_X_END(hw_cfg->osd_x_end[i])
			| IEP2_REG_OSD_Y_STA(hw_cfg->osd_y_sta[i])
			| IEP2_REG_OSD_Y_END(hw_cfg->osd_y_end[i]);
		mpp_write_relaxed(mpp, IEP2_REG_OSD_AREA_CONF(i), reg);
	}

	for (; i < ARRAY_SIZE(hw_cfg->osd_x_sta); ++i)
		mpp_write_relaxed(mpp, IEP2_REG_OSD_AREA_CONF(i), 0);
}

static void iep2_mtn_tab_cfg(struct mpp_dev *mpp, struct iep_task *task)
{
	struct iep2_params *hw_cfg = &task->params;
	int i;
	u32 *mtn_tab = hw_cfg->mtn_en ? hw_cfg->mtn_tab : iep2_mtn_tab;

	for (i = 0; i < ARRAY_SIZE(hw_cfg->mtn_tab); ++i)
		mpp_write_relaxed(mpp, IEP2_REG_DIL_MTN_TAB(i), mtn_tab[i]);
}

static u32 iep2_tru_list_vld_tab[] = {
	IEP2_REG_MV_TRU_LIST0_4_VLD, IEP2_REG_MV_TRU_LIST1_5_VLD,
	IEP2_REG_MV_TRU_LIST2_6_VLD, IEP2_REG_MV_TRU_LIST3_7_VLD,
	IEP2_REG_MV_TRU_LIST0_4_VLD, IEP2_REG_MV_TRU_LIST1_5_VLD,
	IEP2_REG_MV_TRU_LIST2_6_VLD, IEP2_REG_MV_TRU_LIST3_7_VLD
};

static void iep2_tru_list_cfg(struct mpp_dev *mpp, struct iep_task *task)
{
	struct iep2_params *cfg = &task->params;
	int i;
	u32 reg;

	for (i = 0; i < ARRAY_SIZE(cfg->mv_tru_list); i += 4) {
		reg = 0;

		if (cfg->mv_tru_vld[i])
			reg |= IEP2_REG_MV_TRU_LIST0_4(cfg->mv_tru_list[i])
				| iep2_tru_list_vld_tab[i];

		if (cfg->mv_tru_vld[i + 1])
			reg |= IEP2_REG_MV_TRU_LIST1_5(cfg->mv_tru_list[i + 1])
				| iep2_tru_list_vld_tab[i + 1];

		if (cfg->mv_tru_vld[i + 2])
			reg |= IEP2_REG_MV_TRU_LIST2_6(cfg->mv_tru_list[i + 2])
				| iep2_tru_list_vld_tab[i + 2];

		if (cfg->mv_tru_vld[i + 3])
			reg |= IEP2_REG_MV_TRU_LIST3_7(cfg->mv_tru_list[i + 3])
				| iep2_tru_list_vld_tab[i + 3];

		mpp_write_relaxed(mpp, IEP2_REG_MV_TRU_LIST(i / 4), reg);
	}
}

static void iep2_comb_cfg(struct mpp_dev *mpp, struct iep_task *task)
{
	struct iep2_params *hw_cfg = &task->params;
	int i;
	u32 reg = 0;

	for (i = 0; i < ARRAY_SIZE(hw_cfg->comb_osd_vld); ++i) {
		if (hw_cfg->comb_osd_vld[i])
			reg |= IEP2_REG_COMB_OSD_VLD(i);
	}

	reg |= IEP2_REG_COMB_T_THR(hw_cfg->comb_t_thr)
		| IEP2_REG_COMB_FEATRUE_THR(hw_cfg->comb_feature_thr)
		| IEP2_REG_COMB_CNT_THR(hw_cfg->comb_cnt_thr);
	mpp_write_relaxed(mpp, IEP2_REG_COMB_CONFIG0, reg);
}

static int iep2_run(struct mpp_dev *mpp,
		    struct mpp_task *mpp_task)
{
	struct iep_task *task = NULL;
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	task = to_iep_task(mpp_task);

	/* init current task */
	mpp->cur_task = mpp_task;

	iep2_config(mpp, task);
	iep2_osd_cfg(mpp, task);
	iep2_mtn_tab_cfg(mpp, task);
	iep2_tru_list_cfg(mpp, task);
	iep2_comb_cfg(mpp, task);

	/* set interrupt enable bits */
	mpp_write_relaxed(mpp, IEP2_REG_INT_EN,
			  IEP2_REG_FRM_DONE_EN
			  | IEP2_REG_OSD_MAX_EN
			  | IEP2_REG_BUS_ERROR_EN);

	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);

	/* Last, flush the registers */
	wmb();
	/* start iep2 */
	mpp_write(mpp, IEP2_REG_FRM_START, 1);

	mpp_task_run_end(mpp_task, timing_en);

	mpp_debug_leave();

	return 0;
}

static int iep2_irq(struct mpp_dev *mpp)
{
	mpp->irq_status = mpp_read(mpp, IEP2_REG_INT_STS);
	mpp_write(mpp, IEP2_REG_INT_CLR, 0xffffffff);

	if (!IEP2_REG_RO_VALID_INT_STS(mpp->irq_status))
		return IRQ_NONE;

	return IRQ_WAKE_THREAD;
}

static int iep2_isr(struct mpp_dev *mpp)
{
	struct mpp_task *mpp_task = NULL;
	struct iep_task *task = NULL;
	struct iep2_dev *iep = to_iep2_dev(mpp);

	mpp_task = mpp->cur_task;
	task = to_iep_task(mpp_task);
	if (!task) {
		dev_err(iep->mpp.dev, "no current task\n");
		return IRQ_HANDLED;
	}

	mpp_time_diff(mpp_task);
	mpp->cur_task = NULL;
	task->irq_status = mpp->irq_status;
	mpp_debug(DEBUG_IRQ_STATUS, "irq_status: %08x\n",
		  task->irq_status);

	if (IEP2_REG_RO_BUS_ERROR_STS(task->irq_status))
		atomic_inc(&mpp->reset_request);

	mpp_task_finish(mpp_task->session, mpp_task);

	mpp_debug_leave();

	return IRQ_HANDLED;
}

static void iep2_osd_done(struct mpp_dev *mpp, struct iep_task *task)
{
	int i;
	u32 reg;

	for (i = 0; i < task->output.dect_osd_cnt; ++i) {
		reg = mpp_read(mpp, IEP2_REG_RO_OSD_AREA_X(i));
		task->output.x_sta[i] = IEP2_REG_RO_X_STA(reg) / 16;
		task->output.x_end[i] = IEP2_REG_RO_X_END(reg) / 16;

		reg = mpp_read(mpp, IEP2_REG_RO_OSD_AREA_Y(i));
		task->output.y_sta[i] = IEP2_REG_RO_Y_STA(reg) / 4;
		task->output.y_end[i] = IEP2_REG_RO_Y_END(reg) / 4;
	}

	for (; i < ARRAY_SIZE(task->output.x_sta); ++i) {
		task->output.x_sta[i] = 0;
		task->output.x_end[i] = 0;
		task->output.y_sta[i] = 0;
		task->output.y_end[i] = 0;
	}
}

static int iep2_finish(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task)
{
	struct iep_task *task = to_iep_task(mpp_task);
	struct iep2_output *output = &task->output;
	u32 i;
	u32 reg;

	mpp_debug_enter();

	output->dect_pd_tcnt = mpp_read(mpp, IEP2_REG_RO_PD_TCNT);
	output->dect_pd_bcnt = mpp_read(mpp, IEP2_REG_RO_PD_BCNT);
	output->dect_ff_cur_tcnt = mpp_read(mpp, IEP2_REG_RO_FF_CUR_TCNT);
	output->dect_ff_cur_bcnt = mpp_read(mpp, IEP2_REG_RO_FF_CUR_BCNT);
	output->dect_ff_nxt_tcnt = mpp_read(mpp, IEP2_REG_RO_FF_NXT_TCNT);
	output->dect_ff_nxt_bcnt = mpp_read(mpp, IEP2_REG_RO_FF_NXT_BCNT);
	output->dect_ff_ble_tcnt = mpp_read(mpp, IEP2_REG_RO_FF_BLE_TCNT);
	output->dect_ff_ble_bcnt = mpp_read(mpp, IEP2_REG_RO_FF_BLE_BCNT);
	output->dect_ff_nz = mpp_read(mpp, IEP2_REG_RO_FF_COMB_NZ);
	output->dect_ff_comb_f = mpp_read(mpp, IEP2_REG_RO_FF_COMB_F);
	output->dect_osd_cnt = mpp_read(mpp, IEP2_REG_RO_OSD_NUM);

	reg = mpp_read(mpp, IEP2_REG_RO_COMB_CNT);
	output->out_comb_cnt = IEP2_REG_RO_OUT_COMB_CNT(reg);
	output->out_osd_comb_cnt = IEP2_REG_RO_OUT_OSD_COMB_CNT(reg);
	output->ff_gradt_tcnt = mpp_read(mpp, IEP2_REG_RO_FF_GRADT_TCNT);
	output->ff_gradt_bcnt = mpp_read(mpp, IEP2_REG_RO_FF_GRADT_BCNT);

	iep2_osd_done(mpp, task);

	for (i = 0; i < ARRAY_SIZE(output->mv_hist); i += 2) {
		reg = mpp_read(mpp, IEP2_REG_RO_MV_HIST_BIN(i / 2));
		output->mv_hist[i] = IEP2_REG_RO_MV_HIST_EVEN(reg);
		output->mv_hist[i + 1] = IEP2_REG_RO_MV_HIST_ODD(reg);
	}

	mpp_debug_leave();

	return 0;
}

static int iep2_result(struct mpp_dev *mpp,
		       struct mpp_task *mpp_task,
		       struct mpp_task_msgs *msgs)
{
	u32 i;
	struct mpp_request *req;
	struct iep_task *task = to_iep_task(mpp_task);

	/* FIXME may overflow the kernel */
	for (i = 0; i < task->r_req_cnt; i++) {
		req = &task->r_reqs[i];

		if (copy_to_user(req->data, (u8 *)&task->output, req->size)) {
			mpp_err("copy_to_user reg fail\n");
			return -EIO;
		}
	}

	return 0;
}

static int iep2_free_task(struct mpp_session *session,
			  struct mpp_task *mpp_task)
{
	struct iep_task *task = to_iep_task(mpp_task);

	mpp_task_finalize(session, mpp_task);
	kfree(task);

	return 0;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
static int iep2_procfs_remove(struct mpp_dev *mpp)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);

	if (iep->procfs) {
		proc_remove(iep->procfs);
		iep->procfs = NULL;
	}

	return 0;
}

static int iep2_procfs_init(struct mpp_dev *mpp)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);

	iep->procfs = proc_mkdir(mpp->dev->of_node->name, mpp->srv->procfs);
	if (IS_ERR_OR_NULL(iep->procfs)) {
		mpp_err("failed on mkdir\n");
		iep->procfs = NULL;
		return -EIO;
	}

	/* for common mpp_dev options */
	mpp_procfs_create_common(iep->procfs, mpp);

	mpp_procfs_create_u32("aclk", 0644,
			      iep->procfs, &iep->aclk_info.debug_rate_hz);
	mpp_procfs_create_u32("session_buffers", 0644,
			      iep->procfs, &mpp->session_max_buffers);

	return 0;
}
#else
static inline int iep2_procfs_remove(struct mpp_dev *mpp)
{
	return 0;
}

static inline int iep2_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

#define IEP2_TILE_W_MAX		120
#define IEP2_TILE_H_MAX		272

static int iep2_init(struct mpp_dev *mpp)
{
	int ret;
	struct iep2_dev *iep = to_iep2_dev(mpp);

	mpp->grf_info = &mpp->srv->grf_infos[MPP_DRIVER_IEP2];

	/* Get clock info from dtsi */
	ret = mpp_get_clk_info(mpp, &iep->aclk_info, "aclk");
	if (ret)
		mpp_err("failed on clk_get aclk\n");
	ret = mpp_get_clk_info(mpp, &iep->hclk_info, "hclk");
	if (ret)
		mpp_err("failed on clk_get hclk\n");
	ret = mpp_get_clk_info(mpp, &iep->sclk_info, "sclk");
	if (ret)
		mpp_err("failed on clk_get sclk\n");
	/* Set default rates */
	mpp_set_clk_info_rate_hz(&iep->aclk_info, CLK_MODE_DEFAULT, 300 * MHZ);

	iep->rst_a = mpp_reset_control_get(mpp, RST_TYPE_A, "rst_a");
	if (!iep->rst_a)
		mpp_err("No aclk reset resource define\n");
	iep->rst_h = mpp_reset_control_get(mpp, RST_TYPE_H, "rst_h");
	if (!iep->rst_h)
		mpp_err("No hclk reset resource define\n");
	iep->rst_s = mpp_reset_control_get(mpp, RST_TYPE_CORE, "rst_s");
	if (!iep->rst_s)
		mpp_err("No sclk reset resource define\n");

	iep->roi.size = IEP2_TILE_W_MAX * IEP2_TILE_H_MAX;
	iep->roi.vaddr = dma_alloc_coherent(mpp->dev, iep->roi.size,
					    &iep->roi.iova,
					    GFP_KERNEL);
	if (iep->roi.vaddr) {
		dev_err(mpp->dev, "allocate roi buffer failed\n");
		//return -ENOMEM;
	}

	return 0;
}

static int iep2_clk_on(struct mpp_dev *mpp)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);

	mpp_clk_safe_enable(iep->aclk_info.clk);
	mpp_clk_safe_enable(iep->hclk_info.clk);
	mpp_clk_safe_enable(iep->sclk_info.clk);

	return 0;
}

static int iep2_clk_off(struct mpp_dev *mpp)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);

	mpp_clk_safe_disable(iep->aclk_info.clk);
	mpp_clk_safe_disable(iep->hclk_info.clk);
	mpp_clk_safe_disable(iep->sclk_info.clk);

	return 0;
}

static int iep2_set_freq(struct mpp_dev *mpp,
			 struct mpp_task *mpp_task)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);
	struct iep_task *task = to_iep_task(mpp_task);

	mpp_clk_set_rate(&iep->aclk_info, task->clk_mode);

	return 0;
}

static int iep2_reset(struct mpp_dev *mpp)
{
	struct iep2_dev *iep = to_iep2_dev(mpp);

	if (iep->rst_a && iep->rst_h && iep->rst_s) {
		/* Don't skip this or iommu won't work after reset */
		mpp_pmu_idle_request(mpp, true);
		mpp_safe_reset(iep->rst_a);
		mpp_safe_reset(iep->rst_h);
		mpp_safe_reset(iep->rst_s);
		udelay(5);
		mpp_safe_unreset(iep->rst_a);
		mpp_safe_unreset(iep->rst_h);
		mpp_safe_unreset(iep->rst_s);
		mpp_pmu_idle_request(mpp, false);
	}

	return 0;
}

static struct mpp_hw_ops iep_v2_hw_ops = {
	.init = iep2_init,
	.clk_on = iep2_clk_on,
	.clk_off = iep2_clk_off,
	.set_freq = iep2_set_freq,
	.reset = iep2_reset,
};

static struct mpp_dev_ops iep_v2_dev_ops = {
	.alloc_task = iep2_alloc_task,
	.run = iep2_run,
	.irq = iep2_irq,
	.isr = iep2_isr,
	.finish = iep2_finish,
	.result = iep2_result,
	.free_task = iep2_free_task,
};

static struct mpp_hw_info iep2_hw_info = {
	.reg_id = -1,
};

static const struct mpp_dev_var iep2_v2_data = {
	.device_type = MPP_DEVICE_IEP2,
	.hw_ops = &iep_v2_hw_ops,
	.dev_ops = &iep_v2_dev_ops,
	.hw_info = &iep2_hw_info,
};

static const struct of_device_id mpp_iep2_match[] = {
	{
		.compatible = "rockchip,iep-v2",
		.data = &iep2_v2_data,
	},
#ifdef CONFIG_CPU_RV1126
	{
		.compatible = "rockchip,rv1126-iep",
		.data = &iep2_v2_data,
	},
#endif
	{},
};

static int iep2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iep2_dev *iep = NULL;
	struct mpp_dev *mpp = NULL;
	const struct of_device_id *match = NULL;
	int ret = 0;

	dev_info(dev, "probe device\n");
	iep = devm_kzalloc(dev, sizeof(struct iep2_dev), GFP_KERNEL);
	if (!iep)
		return -ENOMEM;

	mpp = &iep->mpp;
	platform_set_drvdata(pdev, mpp);

	if (pdev->dev.of_node) {
		match = of_match_node(mpp_iep2_match, pdev->dev.of_node);
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

	mpp->session_max_buffers = IEP2_SESSION_MAX_BUFFERS;
	iep2_procfs_init(mpp);
	/* register current device to mpp service */
	mpp_dev_register_srv(mpp, mpp->srv);
	dev_info(dev, "probing finish\n");

	return 0;
}

static int iep2_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = dev_get_drvdata(dev);
	struct iep2_dev *iep = to_iep2_dev(mpp);

	dma_free_coherent(dev, iep->roi.size, iep->roi.vaddr, iep->roi.iova);

	dev_info(dev, "remove device\n");
	mpp_dev_remove(mpp);
	iep2_procfs_remove(mpp);

	return 0;
}

struct platform_driver rockchip_iep2_driver = {
	.probe = iep2_probe,
	.remove = iep2_remove,
	.shutdown = mpp_dev_shutdown,
	.driver = {
		.name = IEP2_DRIVER_NAME,
		.of_match_table = of_match_ptr(mpp_iep2_match),
	},
};
EXPORT_SYMBOL(rockchip_iep2_driver);

