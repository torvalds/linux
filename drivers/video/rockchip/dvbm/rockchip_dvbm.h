/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 */
#ifndef __ROCKCHIP_DVBM_H__
#define __ROCKCHIP_DVBM_H__

#include <linux/clk.h>
#include <linux/reset.h>

struct rk_dvbm_base {
	/* 0x2c */
	u32 ybuf_bot;
	/* 0x30 */
	u32 ybuf_top;
	/* 0x34 */
	u32 ybuf_sadr;
	/* 0x38 */
	u32 ybuf_lstd;
	/* 0x3c */
	u32 ybuf_fstd;
	/* 0x40 */
	u32 cbuf_bot;
	/* 0x44 */
	u32 cbuf_top;
	/* 0x48 */
	u32 cbuf_sadr;
	/* 0x4c */
	u32 cbuf_lstd;
	/* 0x50 */
	u32 cbuf_fstd;
	/* 0x54 */
	u32 aful_thdy;
	/* 0x58 */
	u32 aful_thdc;
	/* 0x5c */
	u32 oful_thdy;
	/* 0x60 */
	u32 oful_thdc;
};

struct rk_dvbm_regs {
	/* 0x0 */
	u32 version;

	/* 0x4 */
	struct {
		u32 isp_cnct : 1;
		u32 reserved : 31;
	} isp_cnct;

	/* 0x8 */
	struct {
		u32 vepu_cnct : 1;
		u32 reserved : 31;
	} vepu_cnct;

	/* 0xc */
	struct {
		u32 auto_resyn                  : 1;
		u32 ignore_vepu_cnct_ack        : 1;
		/*
		 * 1’b0                            : the current ISP frame
		 * 1’b1                            : the next ISP frame
		 */
		u32 start_point_after_vepu_cnct : 1;
		u32 reserved0                   : 5;
		/* only support yuv420sp 4'h0 */
		u32 fmt                         : 4;
		u32 reserved1                   : 20;
	} dvbm_cfg;

	/* 0x10 */
	struct {
		u32 wdg_isp_cnct_timeout        : 22;
		u32 reserved                    : 10;
	} wdg_cfg0;

	/* 0x14 */
	struct {
		u32 wdg_vepu_cnct_timeout       : 22;
		u32 reserved                    : 10;
	} wdg_cfg1;

	/* 0x18 */
	struct {
		u32 wdg_vepu_handshake_timeout  : 22;
		u32 reserved                    : 10;
	} wdg_cfg2;

	/* 0x1c */
	struct {
		u32 buf_ovfl               : 1;
		u32 resync_finish          : 1;
		u32 isp_cnct_timeout       : 1;
		u32 vepu_cnct_timeout      : 1;

		u32 vepu_handshake_timeout : 1;
		u32 isp_cnct               : 1;
		u32 isp_discnct            : 1;
		u32 vepu_cnct              : 1;

		u32 vepu_discnct           : 1;
		u32 reserved               : 23;
	} int_en;

	/* 0x20 */
	struct {
		u32 buf_ovfl               : 1;
		u32 resync_finish          : 1;
		u32 isp_cnct_timeout       : 1;
		u32 vepu_cnct_timeout      : 1;

		u32 vepu_handshake_timeout : 1;
		u32 isp_cnct               : 1;
		u32 isp_discnct            : 1;
		u32 vepu_cnct              : 1;

		u32 vepu_discnct           : 1;
		u32 reserved               : 23;
	} int_msk;

	/* 0x24 */
	struct {
		u32 buf_ovfl               : 1;
		u32 resync_finish          : 1;
		u32 isp_cnct_timeout       : 1;
		u32 vepu_cnct_timeout      : 1;

		u32 vepu_handshake_timeout : 1;
		u32 isp_cnct               : 1;
		u32 isp_discnct            : 1;
		u32 vepu_cnct              : 1;

		u32 vepu_discnct           : 1;
		u32 reserved               : 23;
	} int_clr;

	/* 0x28 */
	struct {
		u32 buf_ovfl               : 1;
		u32 resync_finish          : 1;
		u32 isp_cnct_timeout       : 1;
		u32 vepu_cnct_timeout      : 1;

		u32 vepu_handshake_timeout : 1;
		u32 isp_cnct               : 1;
		u32 isp_discnct            : 1;
		u32 vepu_cnct              : 1;

		u32 vepu_discnct           : 1;
		u32 reserved               : 23;
	} int_st;
	struct rk_dvbm_base addr_base;
	/* 0x64 - 0x7c */
	u32 reserved[7];

	/* 0x80 */
	struct {
		u32 isp_connection       : 1;
		u32 vepu_connection      : 1;
		u32 resynchronization    : 1;
		u32 y_buf_ovfl           : 1;

		u32 c_buf_ovfl           : 1;
		u32 reserved             : 27;
	} dvbm_st;

	/* 0x84 */
	u32 ovfl_st;
};

struct dvbm_ctx {
	struct clk *clk;
	struct device *dev;
	void __iomem *reg_base;
	struct rk_dvbm_regs regs;
	struct reset_control *rst;

	u32 isp_connet;
	u32 vepu_connet;
	u32 buf_overflow;
	u32 irq_status;
	u32 dvbm_status;
	int irq;

	/* vepu infos */
	struct dvbm_port port_vepu;
	atomic_t vepu_ref;
	atomic_t vepu_link;
	struct dvbm_cb	vepu_cb;
	struct dvbm_addr_cfg vepu_cfg;

	/* isp infos */
	struct dvbm_port port_isp;
	struct dvbm_cb	isp_cb;
	struct dvbm_isp_cfg_t isp_cfg;
	struct dvbm_isp_frm_info isp_frm_info;
	atomic_t isp_link;
	atomic_t isp_ref;
	u32 isp_max_lcnt;
	u32 isp_frm_start;
	u32 isp_frm_end;
	ktime_t isp_frm_time;
	u32 wrap_line;

	/* debug infos */
	u32 dump_s;
	u32 dump_e;
	u32 ignore_ovfl;
	u32 loopcnt;
};

#endif
