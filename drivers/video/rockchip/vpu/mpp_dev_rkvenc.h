/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ROCKCHIP_MPP_DEV_RKVENC_H
#define __ROCKCHIP_MPP_DEV_RKVENC_H

union rkvenc_osd_palette_elem {
	struct  {
		u8 y;
		u8 u;
		u8 v;
		u8 alpha;
	};
	u32 elem;
};

#define	RKVENC_OSD_PLT_LEN		256
struct rkvenc_osd_palette {
	union rkvenc_osd_palette_elem plalette[RKVENC_OSD_PLT_LEN];
};

#define MPP_DEV_RKVENC_SET_COLOR_PALETTE	\
			_IOW(MPP_IOC_MAGIC, MPP_IOC_CUSTOM_BASE + 1,	\
			struct rkvenc_osd_palette)

struct rkvenc_config_elem {
	u32 reg_num;
	u32 reg[140];
	struct extra_info_for_iommu ext_inf;
};

struct rkvenc_config {
	u32 mode;
	u32 tbl_num;
	struct rkvenc_config_elem elem[10];
};

struct rkvenc_result_elem {
	u32 status;
	u32 result[11];
};

struct rkvenc_result {
	u32 tbl_num;
	struct rkvenc_result_elem elem[10];
};

struct rkvenc_ctx {
	struct mpp_ctx ictx;
	enum RKVENC_MODE mode;

	struct rkvenc_config cfg;

	/* store status read from hw, oneframe mode used only */
	struct rkvenc_result result;
};

struct rkvenc_session {
	struct mpp_session isession;

	struct rkvenc_osd_palette palette;
	bool palette_valid;
};

struct mpp_dev_rkvenc_reg {
	u32 unused_00;
	u32 enc_strt;
	u32 enc_clr;
	u32 lkt_addr;
	u32 int_en;
	u32 int_msk;
	u32 int_clr;
	u32 unused_20[4];
	u32 int_stus;
	/* 12 */
	u32 enc_rsl;
	u32 enc_pic;
	u32 enc_wdg;
	u32 dtrns_map;
	u32 dtrns_cfg;
	u32 src_fmt;
	u32 src_udfy;
	u32 src_udfu;
	u32 src_udfv;
	u32 src_udfo;
	u32 src_proc;
	u32 src_tthrd;
	u32 src_stbl[5];
	u32 h3d_tbl[40];
	u32 src_strd;
	u32 adr_srcy;
	u32 adr_srcu;
	u32 adr_srcv;
	u32 adr_fltw;
	u32 adr_fltr;
	u32 adr_ctuc;
	u32 adr_rfpw;
	u32 adr_rfpr;
	u32 adr_cmvw;
	u32 adr_cmvr;
	u32 adr_dspw;
	u32 adr_dspr;
	u32 adr_meiw;
	u32 adr_bsbt;
	u32 adr_bsbb;
	u32 adr_bsbr;
	u32 adr_bsbw;
	u32 sli_spl;
	u32 sli_spl_byte;
	u32 me_rnge;
	u32 me_cnst;
	u32 me_ram;
	u32 rc_cfg;
	u32 rc_erp[5];
	u32 rc_adj[2];
	u32 rc_qp;
	u32 rc_tgt;
	u32 rdo_cfg;
	u32 synt_nal;
	u32 synt_sps;
	u32 synt_pps;
	u32 synt_sli0;
	u32 synt_sli1;
	u32 synt_sli2_rodr;
	u32 synt_ref_mark0;
	u32 synt_ref_mark1;
	u32 osd_cfg;
	u32 osd_inv;
	u32 unused_1c8[2];
	u32 osd_pos[8];
	u32 osd_addr[8];
	u32 unused_210[9];
};

struct rockchip_rkvenc_dev {
	struct rockchip_mpp_dev idev;
	unsigned long lkt_dma_addr;
	int lkt_hdl;
	void *lkt_cpu_addr;
	u32 irq_status;
	unsigned long war_dma_addr;
	int war_hdl;
	struct mpp_dev_rkvenc_reg *war_reg;
	struct rkvenc_ctx *dummy_ctx;
	atomic_t dummy_ctx_in_used;

	struct clk *aclk;
	struct clk *hclk;
	struct clk *core;

	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_v;
};

struct link_table_elem {
	unsigned long lkt_dma_addr;
	int lkt_hdl;
	void *lkt_cpu_addr;
	u32 lkt_index;
	struct list_head list;
};

#endif
