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
 * jim liu <jim.liu@intel.com>
 * Jackie Li<yaodong.li@intel.com>
 */

#ifndef __MDFLD_DSI_DBI_H__
#define __MDFLD_DSI_DBI_H__

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "power.h"

#include "mdfld_dsi_output.h"
#include "mdfld_output.h"

#define DRM_MODE_ENCODER_MIPI  5


/*
 * DBI encoder which inherits from mdfld_dsi_encoder
 */
struct mdfld_dsi_dbi_output {
	struct mdfld_dsi_encoder base;
	struct drm_display_mode *panel_fixed_mode;
	u8 last_cmd;
	u8 lane_count;
	u8 channel_num;
	struct drm_device *dev;

	/* Backlight operations */

	/* DSR timer */
	spinlock_t dsr_timer_lock;
	struct timer_list dsr_timer;
	void(*dsi_timer_func)(unsigned long data);
	u32 dsr_idle_count;
	bool dsr_fb_update_done;

	/* Mode setting flags */
	u32 mode_flags;

	/* Panel status */
	bool dbi_panel_on;
	bool first_boot;
	struct panel_funcs *p_funcs;
};

#define MDFLD_DSI_DBI_OUTPUT(dsi_encoder) \
	container_of(dsi_encoder, struct mdfld_dsi_dbi_output, base)

struct mdfld_dbi_dsr_info {
	int dbi_output_num;
	struct mdfld_dsi_dbi_output *dbi_outputs[2];

	spinlock_t dsr_timer_lock;
	struct timer_list dsr_timer;
	u32 dsr_idle_count;
};

#define DBI_CB_TIMEOUT_COUNT	0xffff

/* DCS commands */
#define enter_sleep_mode	0x10
#define exit_sleep_mode		0x11
#define set_display_off		0x28
#define	set_dispaly_on		0x29
#define set_column_address	0x2a
#define set_page_addr		0x2b
#define write_mem_start		0x2c

/* Offsets */
#define CMD_MEM_ADDR_OFFSET	0

#define CMD_DATA_SRC_SYSTEM_MEM	0
#define CMD_DATA_SRC_PIPE	1

static inline int mdfld_dsi_dbi_fifo_ready(struct mdfld_dsi_dbi_output *dbi_output)
{
	struct drm_device *dev = dbi_output->dev;
	u32 retry = DBI_CB_TIMEOUT_COUNT;
	int reg_offset = (dbi_output->channel_num == 1) ? MIPIC_REG_OFFSET : 0;
	int ret = 0;

	/* Query the dbi fifo status*/
	while (retry--) {
		if (REG_READ(MIPIA_GEN_FIFO_STAT_REG + reg_offset) & (1 << 27))
			break;
	}

	if (!retry) {
		DRM_ERROR("Timeout waiting for DBI FIFO empty\n");
		ret = -EAGAIN;
	}
	return ret;
}

static inline int mdfld_dsi_dbi_cmd_sent(struct mdfld_dsi_dbi_output *dbi_output)
{
	struct drm_device *dev = dbi_output->dev;
	u32 retry = DBI_CB_TIMEOUT_COUNT;
	int reg_offset = (dbi_output->channel_num == 1) ? MIPIC_REG_OFFSET : 0;
	int ret = 0;

	/* Query the command execution status */
	while (retry--)
		if (!(REG_READ(MIPIA_CMD_ADD_REG + reg_offset) & (1 << 10)))
			break;

	if (!retry) {
		DRM_ERROR("Timeout waiting for DBI command status\n");
		ret = -EAGAIN;
	}

	return ret;
}

static inline int mdfld_dsi_dbi_cb_ready(struct mdfld_dsi_dbi_output *dbi_output)
{
	int ret = 0;

	/* Query the command execution status*/
	ret = mdfld_dsi_dbi_cmd_sent(dbi_output);
	if (ret) {
		DRM_ERROR("Peripheral is busy\n");
		ret = -EAGAIN;
	}
	/* Query the dbi fifo status*/
	ret = mdfld_dsi_dbi_fifo_ready(dbi_output);
	if (ret) {
		DRM_ERROR("DBI FIFO is not empty\n");
		ret = -EAGAIN;
	}
	return ret;
}

extern void mdfld_dsi_dbi_output_init(struct drm_device *dev,
			struct psb_intel_mode_device *mode_dev, int pipe);
extern void mdfld_dsi_dbi_exit_dsr(struct drm_device *dev, u32 update_src,
			void *p_surfaceAddr, bool check_hw_on_only);
extern void mdfld_dsi_dbi_enter_dsr(struct mdfld_dsi_dbi_output *dbi_output,
			int pipe);
extern int mdfld_dbi_dsr_init(struct drm_device *dev);
extern void mdfld_dbi_dsr_exit(struct drm_device *dev);
extern void mdfld_dbi_dsr_timer_start(struct mdfld_dbi_dsr_info *dsr_info);
extern struct mdfld_dsi_encoder *mdfld_dsi_dbi_init(struct drm_device *dev,
			struct mdfld_dsi_connector *dsi_connector,
			struct panel_funcs *p_funcs);
extern int mdfld_dsi_dbi_send_dcs(struct mdfld_dsi_dbi_output *dbi_output,
			u8 dcs, u8 *param, u32 num, u8 data_src);
extern int mdfld_dsi_dbi_update_area(struct mdfld_dsi_dbi_output *dbi_output,
			u16 x1, u16 y1, u16 x2, u16 y2);
extern void mdfld_dbi_dsr_timer_start(struct mdfld_dbi_dsr_info *dsr_info);
extern int mdfld_dsi_dbi_update_power(struct mdfld_dsi_dbi_output *dbi_output,
			int mode);
extern void mdfld_dsi_controller_dbi_init(struct mdfld_dsi_config *dsi_config,
			int pipe);

#endif /*__MDFLD_DSI_DBI_H__*/
