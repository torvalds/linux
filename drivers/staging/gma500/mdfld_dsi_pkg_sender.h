/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Jackie Li<yaodong.li@intel.com>
 */
#ifndef __MDFLD_DSI_PKG_SENDER_H__
#define __MDFLD_DSI_PKG_SENDER_H__

#include <linux/kthread.h>

#define MDFLD_MAX_DCS_PARAM	8
#define MDFLD_MAX_PKG_NUM	2048

enum {
	MDFLD_DSI_PKG_DCS,
	MDFLD_DSI_PKG_GEN_SHORT_WRITE_0 = 0x03,
	MDFLD_DSI_PKG_GEN_SHORT_WRITE_1 = 0x13,
	MDFLD_DSI_PKG_GEN_SHORT_WRITE_2 = 0x23,
	MDFLD_DSI_PKG_GEN_READ_0 = 0x04,
	MDFLD_DSI_PKG_GEN_READ_1 = 0x14,
	MDFLD_DSI_PKG_GEN_READ_2 = 0x24,
	MDFLD_DSI_PKG_GEN_LONG_WRITE = 0x29,
	MDFLD_DSI_PKG_MCS_SHORT_WRITE_0 = 0x05,
	MDFLD_DSI_PKG_MCS_SHORT_WRITE_1 = 0x15,
	MDFLD_DSI_PKG_MCS_READ = 0x06,
	MDFLD_DSI_PKG_MCS_LONG_WRITE = 0x39,
};

enum {
	MDFLD_DSI_LP_TRANSMISSION,
	MDFLD_DSI_HS_TRANSMISSION,
	MDFLD_DSI_DCS,
};

enum {
	MDFLD_DSI_PANEL_MODE_SLEEP = 0x1,
};

enum {
	MDFLD_DSI_PKG_SENDER_FREE = 0x0,
	MDFLD_DSI_PKG_SENDER_BUSY = 0x1,
};

enum {
	MDFLD_DSI_SEND_PACKAGE,
	MDFLD_DSI_QUEUE_PACKAGE,
};

struct mdfld_dsi_gen_short_pkg {
	u8 cmd;
	u8 param;
};

struct mdfld_dsi_gen_long_pkg {
	u32 *data;
	u32 len;
};

struct mdfld_dsi_dcs_pkg {
	u8 cmd;
	u8 param[MDFLD_MAX_DCS_PARAM];
	u32 param_num;
	u8 data_src;
};

struct mdfld_dsi_pkg {
	u8 pkg_type;
	u8 transmission_type;

	union {
		struct mdfld_dsi_gen_short_pkg short_pkg;
		struct mdfld_dsi_gen_long_pkg long_pkg;
		struct mdfld_dsi_dcs_pkg dcs_pkg;
	} pkg;

	struct list_head entry;
};

struct mdfld_dsi_pkg_sender {
	struct drm_device *dev;
	struct mdfld_dsi_connector *dsi_connector;
	u32 status;

	u32 panel_mode;

	int pipe;

	spinlock_t lock;
	struct list_head pkg_list;
	struct list_head free_list;

	u32 pkg_num;

	int dbi_pkg_support;

	u32 dbi_cb_phy;
	void *dbi_cb_addr;

	/* Registers */
	u32 dpll_reg;
	u32 dspcntr_reg;
	u32 pipeconf_reg;
	u32 pipestat_reg;
	u32 dsplinoff_reg;
	u32 dspsurf_reg;

	u32 mipi_intr_stat_reg;
	u32 mipi_lp_gen_data_reg;
	u32 mipi_hs_gen_data_reg;
	u32 mipi_lp_gen_ctrl_reg;
	u32 mipi_hs_gen_ctrl_reg;
	u32 mipi_gen_fifo_stat_reg;
	u32 mipi_data_addr_reg;
	u32 mipi_data_len_reg;
	u32 mipi_cmd_addr_reg;
	u32 mipi_cmd_len_reg;
};

/* DCS definitions */
#define DCS_SOFT_RESET			0x01
#define DCS_ENTER_SLEEP_MODE		0x10
#define DCS_EXIT_SLEEP_MODE		0x11
#define DCS_SET_DISPLAY_OFF		0x28
#define DCS_SET_DISPLAY_ON		0x29
#define DCS_SET_COLUMN_ADDRESS		0x2a
#define DCS_SET_PAGE_ADDRESS		0x2b
#define DCS_WRITE_MEM_START		0x2c
#define DCS_SET_TEAR_OFF		0x34
#define DCS_SET_TEAR_ON 		0x35

extern int mdfld_dsi_pkg_sender_init(struct mdfld_dsi_connector *dsi_connector,
			int pipe);
extern void mdfld_dsi_pkg_sender_destroy(struct mdfld_dsi_pkg_sender *sender);
extern int mdfld_dsi_send_dcs(struct mdfld_dsi_pkg_sender *sender, u8 dcs,
			u8 *param, u32 param_num, u8 data_src, int delay);
extern int mdfld_dsi_send_mcs_short_hs(struct mdfld_dsi_pkg_sender *sender,
			u8 cmd, u8 param, u8 param_num, int delay);
extern int mdfld_dsi_send_mcs_short_lp(struct mdfld_dsi_pkg_sender *sender,
			u8 cmd, u8 param, u8 param_num, int delay);
extern int mdfld_dsi_send_mcs_long_hs(struct mdfld_dsi_pkg_sender *sender,
			u32 *data, u32 len, int delay);
extern int mdfld_dsi_send_mcs_long_lp(struct mdfld_dsi_pkg_sender *sender,
			u32 *data, u32 len, int delay);
extern int mdfld_dsi_send_gen_short_hs(struct mdfld_dsi_pkg_sender *sender,
			u8 param0, u8 param1, u8 param_num, int delay);
extern int mdfld_dsi_send_gen_short_lp(struct mdfld_dsi_pkg_sender *sender,
			u8 param0, u8 param1, u8 param_num, int delay);
extern int mdfld_dsi_send_gen_long_hs(struct mdfld_dsi_pkg_sender *sender,
			u32 *data, u32 len, int delay);
extern int mdfld_dsi_send_gen_long_lp(struct mdfld_dsi_pkg_sender *sender,
			u32 *data, u32 len, int delay);

extern int mdfld_dsi_read_gen_hs(struct mdfld_dsi_pkg_sender *sender,
			u8 param0, u8 param1, u8 param_num, u32 *data, u16 len);
extern int mdfld_dsi_read_gen_lp(struct mdfld_dsi_pkg_sender *sender,
			u8 param0, u8 param1, u8 param_num, u32 *data, u16 len);
extern int mdfld_dsi_read_mcs_hs(struct mdfld_dsi_pkg_sender *sender,
			u8 cmd, u32 *data, u16 len);
extern int mdfld_dsi_read_mcs_lp(struct mdfld_dsi_pkg_sender *sender,
			u8 cmd, u32 *data, u16 len);

extern void mdfld_dsi_cmds_kick_out(struct mdfld_dsi_pkg_sender *sender);

#endif /* __MDFLD_DSI_PKG_SENDER_H__ */
