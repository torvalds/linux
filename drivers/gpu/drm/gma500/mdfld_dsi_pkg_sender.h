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

struct mdfld_dsi_pkg_sender {
	struct drm_device *dev;
	struct mdfld_dsi_connector *dsi_connector;
	u32 status;
	u32 panel_mode;

	int pipe;

	spinlock_t lock;

	u32 pkg_num;

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

extern int mdfld_dsi_pkg_sender_init(struct mdfld_dsi_connector *dsi_connector,
					int pipe);
extern void mdfld_dsi_pkg_sender_destroy(struct mdfld_dsi_pkg_sender *sender);
int mdfld_dsi_send_mcs_short(struct mdfld_dsi_pkg_sender *sender, u8 cmd,
					u8 param, u8 param_num, bool hs);
int mdfld_dsi_send_mcs_long(struct mdfld_dsi_pkg_sender *sender, u8 *data,
					u32 len, bool hs);
int mdfld_dsi_send_gen_short(struct mdfld_dsi_pkg_sender *sender, u8 param0,
					u8 param1, u8 param_num, bool hs);
int mdfld_dsi_send_gen_long(struct mdfld_dsi_pkg_sender *sender, u8 *data,
					u32 len, bool hs);
/* Read interfaces */
int mdfld_dsi_read_mcs(struct mdfld_dsi_pkg_sender *sender, u8 cmd,
		u32 *data, u16 len, bool hs);

#endif
