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

#ifndef __MDFLD_DSI_OUTPUT_H__
#define __MDFLD_DSI_OUTPUT_H__

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>

#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "mdfld_output.h"

#include <asm/mrst.h>

#define FLD_MASK(start, end)	(((1 << ((start) - (end) + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << (end)) & FLD_MASK(start, end))
#define FLD_GET(val, start, end) (((val) & FLD_MASK(start, end)) >> (end))
#define FLD_MOD(orig, val, start, end) \
	(((orig) & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end))

#define REG_FLD_MOD(reg, val, start, end) \
	REG_WRITE(reg, FLD_MOD(REG_READ(reg), val, start, end))

static inline int REGISTER_FLD_WAIT(struct drm_device *dev, u32 reg,
		u32 val, int start, int end)
{
	int t = 100000;

	while (FLD_GET(REG_READ(reg), start, end) != val) {
		if (--t == 0)
			return 1;
	}

	return 0;
}

#define REG_FLD_WAIT(reg, val, start, end) \
	REGISTER_FLD_WAIT(dev, reg, val, start, end)

#define REG_BIT_WAIT(reg, val, bitnum) \
	REGISTER_FLD_WAIT(dev, reg, val, bitnum, bitnum)

#define MDFLD_DSI_BRIGHTNESS_MAX_LEVEL 100

#ifdef DEBUG
#define CHECK_PIPE(pipe) ({			\
	const typeof(pipe) __pipe = (pipe);	\
	BUG_ON(__pipe != 0 && __pipe != 2);	\
	__pipe;	})
#else
#define CHECK_PIPE(pipe) (pipe)
#endif

/*
 * Actual MIPIA->MIPIC reg offset is 0x800, value 0x400 is valid for 0 and 2
 */
#define REG_OFFSET(pipe) (CHECK_PIPE(pipe) * 0x400)

/* mdfld DSI controller registers */
#define MIPI_DEVICE_READY_REG(pipe)		(0xb000 + REG_OFFSET(pipe))
#define MIPI_INTR_STAT_REG(pipe)		(0xb004 + REG_OFFSET(pipe))
#define MIPI_INTR_EN_REG(pipe)			(0xb008 + REG_OFFSET(pipe))
#define MIPI_DSI_FUNC_PRG_REG(pipe)		(0xb00c + REG_OFFSET(pipe))
#define MIPI_HS_TX_TIMEOUT_REG(pipe)		(0xb010 + REG_OFFSET(pipe))
#define MIPI_LP_RX_TIMEOUT_REG(pipe)		(0xb014 + REG_OFFSET(pipe))
#define MIPI_TURN_AROUND_TIMEOUT_REG(pipe)	(0xb018 + REG_OFFSET(pipe))
#define MIPI_DEVICE_RESET_TIMER_REG(pipe)	(0xb01c + REG_OFFSET(pipe))
#define MIPI_DPI_RESOLUTION_REG(pipe)		(0xb020 + REG_OFFSET(pipe))
#define MIPI_DBI_FIFO_THROTTLE_REG(pipe)	(0xb024 + REG_OFFSET(pipe))
#define MIPI_HSYNC_COUNT_REG(pipe)		(0xb028 + REG_OFFSET(pipe))
#define MIPI_HBP_COUNT_REG(pipe)		(0xb02c + REG_OFFSET(pipe))
#define MIPI_HFP_COUNT_REG(pipe)		(0xb030 + REG_OFFSET(pipe))
#define MIPI_HACTIVE_COUNT_REG(pipe)		(0xb034 + REG_OFFSET(pipe))
#define MIPI_VSYNC_COUNT_REG(pipe)		(0xb038 + REG_OFFSET(pipe))
#define MIPI_VBP_COUNT_REG(pipe)		(0xb03c + REG_OFFSET(pipe))
#define MIPI_VFP_COUNT_REG(pipe)		(0xb040 + REG_OFFSET(pipe))
#define MIPI_HIGH_LOW_SWITCH_COUNT_REG(pipe)	(0xb044 + REG_OFFSET(pipe))
#define MIPI_DPI_CONTROL_REG(pipe)		(0xb048 + REG_OFFSET(pipe))
#define MIPI_DPI_DATA_REG(pipe)			(0xb04c + REG_OFFSET(pipe))
#define MIPI_INIT_COUNT_REG(pipe)		(0xb050 + REG_OFFSET(pipe))
#define MIPI_MAX_RETURN_PACK_SIZE_REG(pipe)	(0xb054 + REG_OFFSET(pipe))
#define MIPI_VIDEO_MODE_FORMAT_REG(pipe)	(0xb058 + REG_OFFSET(pipe))
#define MIPI_EOT_DISABLE_REG(pipe)		(0xb05c + REG_OFFSET(pipe))
#define MIPI_LP_BYTECLK_REG(pipe)		(0xb060 + REG_OFFSET(pipe))
#define MIPI_LP_GEN_DATA_REG(pipe)		(0xb064 + REG_OFFSET(pipe))
#define MIPI_HS_GEN_DATA_REG(pipe)		(0xb068 + REG_OFFSET(pipe))
#define MIPI_LP_GEN_CTRL_REG(pipe)		(0xb06c + REG_OFFSET(pipe))
#define MIPI_HS_GEN_CTRL_REG(pipe)		(0xb070 + REG_OFFSET(pipe))
#define MIPI_GEN_FIFO_STAT_REG(pipe)		(0xb074 + REG_OFFSET(pipe))
#define MIPI_HS_LS_DBI_ENABLE_REG(pipe)		(0xb078 + REG_OFFSET(pipe))
#define MIPI_DPHY_PARAM_REG(pipe)		(0xb080 + REG_OFFSET(pipe))
#define MIPI_DBI_BW_CTRL_REG(pipe)		(0xb084 + REG_OFFSET(pipe))
#define MIPI_CLK_LANE_SWITCH_TIME_CNT_REG(pipe)	(0xb088 + REG_OFFSET(pipe))

#define MIPI_CTRL_REG(pipe)			(0xb104 + REG_OFFSET(pipe))
#define MIPI_DATA_ADD_REG(pipe)			(0xb108 + REG_OFFSET(pipe))
#define MIPI_DATA_LEN_REG(pipe)			(0xb10c + REG_OFFSET(pipe))
#define MIPI_CMD_ADD_REG(pipe)			(0xb110 + REG_OFFSET(pipe))
#define MIPI_CMD_LEN_REG(pipe)			(0xb114 + REG_OFFSET(pipe))

/* non-uniform reg offset */
#define MIPI_PORT_CONTROL(pipe)		(CHECK_PIPE(pipe) ? MIPI_C : MIPI)

#define DSI_DEVICE_READY				(0x1)
#define DSI_POWER_STATE_ULPS_ENTER			(0x2 << 1)
#define DSI_POWER_STATE_ULPS_EXIT			(0x1 << 1)
#define DSI_POWER_STATE_ULPS_OFFSET			(0x1)


#define DSI_ONE_DATA_LANE					(0x1)
#define DSI_TWO_DATA_LANE					(0x2)
#define DSI_THREE_DATA_LANE					(0X3)
#define DSI_FOUR_DATA_LANE					(0x4)
#define DSI_DPI_VIRT_CHANNEL_OFFSET			(0x3)
#define DSI_DBI_VIRT_CHANNEL_OFFSET			(0x5)
#define DSI_DPI_COLOR_FORMAT_RGB565			(0x01 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB666			(0x02 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB666_UNPACK		(0x03 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB888			(0x04 << 7)
#define DSI_DBI_COLOR_FORMAT_OPTION2			(0x05 << 13)

#define DSI_INTR_STATE_RXSOTERROR			BIT(0)

#define DSI_INTR_STATE_SPL_PKG_SENT			BIT(30)
#define DSI_INTR_STATE_TE				BIT(31)

#define DSI_HS_TX_TIMEOUT_MASK				(0xffffff)

#define DSI_LP_RX_TIMEOUT_MASK				(0xffffff)

#define DSI_TURN_AROUND_TIMEOUT_MASK		(0x3f)

#define DSI_RESET_TIMER_MASK				(0xffff)

#define DSI_DBI_FIFO_WM_HALF				(0x0)
#define DSI_DBI_FIFO_WM_QUARTER				(0x1)
#define DSI_DBI_FIFO_WM_LOW					(0x2)

#define DSI_DPI_TIMING_MASK					(0xffff)

#define DSI_INIT_TIMER_MASK					(0xffff)

#define DSI_DBI_RETURN_PACK_SIZE_MASK		(0x3ff)

#define DSI_LP_BYTECLK_MASK					(0x0ffff)

#define DSI_HS_CTRL_GEN_SHORT_W0			(0x03)
#define DSI_HS_CTRL_GEN_SHORT_W1			(0x13)
#define DSI_HS_CTRL_GEN_SHORT_W2			(0x23)
#define DSI_HS_CTRL_GEN_R0					(0x04)
#define DSI_HS_CTRL_GEN_R1					(0x14)
#define DSI_HS_CTRL_GEN_R2					(0x24)
#define DSI_HS_CTRL_GEN_LONG_W				(0x29)
#define DSI_HS_CTRL_MCS_SHORT_W0			(0x05)
#define DSI_HS_CTRL_MCS_SHORT_W1			(0x15)
#define DSI_HS_CTRL_MCS_R0					(0x06)
#define DSI_HS_CTRL_MCS_LONG_W				(0x39)
#define DSI_HS_CTRL_VC_OFFSET				(0x06)
#define DSI_HS_CTRL_WC_OFFSET				(0x08)

#define	DSI_FIFO_GEN_HS_DATA_FULL			BIT(0)
#define DSI_FIFO_GEN_HS_DATA_HALF_EMPTY		BIT(1)
#define DSI_FIFO_GEN_HS_DATA_EMPTY			BIT(2)
#define DSI_FIFO_GEN_LP_DATA_FULL			BIT(8)
#define DSI_FIFO_GEN_LP_DATA_HALF_EMPTY		BIT(9)
#define DSI_FIFO_GEN_LP_DATA_EMPTY			BIT(10)
#define DSI_FIFO_GEN_HS_CTRL_FULL			BIT(16)
#define DSI_FIFO_GEN_HS_CTRL_HALF_EMPTY		BIT(17)
#define DSI_FIFO_GEN_HS_CTRL_EMPTY			BIT(18)
#define DSI_FIFO_GEN_LP_CTRL_FULL			BIT(24)
#define DSI_FIFO_GEN_LP_CTRL_HALF_EMPTY		BIT(25)
#define DSI_FIFO_GEN_LP_CTRL_EMPTY			BIT(26)
#define DSI_FIFO_DBI_EMPTY					BIT(27)
#define DSI_FIFO_DPI_EMPTY					BIT(28)

#define DSI_DBI_HS_LP_SWITCH_MASK			(0x1)

#define DSI_HS_LP_SWITCH_COUNTER_OFFSET		(0x0)
#define DSI_LP_HS_SWITCH_COUNTER_OFFSET		(0x16)

#define DSI_DPI_CTRL_HS_SHUTDOWN			(0x00000001)
#define DSI_DPI_CTRL_HS_TURN_ON				(0x00000002)

/*dsi power modes*/
#define DSI_POWER_MODE_DISPLAY_ON	BIT(2)
#define DSI_POWER_MODE_NORMAL_ON	BIT(3)
#define DSI_POWER_MODE_SLEEP_OUT	BIT(4)
#define DSI_POWER_MODE_PARTIAL_ON	BIT(5)
#define DSI_POWER_MODE_IDLE_ON		BIT(6)

enum {
	MDFLD_DSI_VIDEO_NON_BURST_MODE_SYNC_PULSE = 1,
	MDFLD_DSI_VIDEO_NON_BURST_MODE_SYNC_EVENTS = 2,
	MDFLD_DSI_VIDEO_BURST_MODE = 3,
};

#define DSI_DPI_COMPLETE_LAST_LINE			BIT(2)
#define DSI_DPI_DISABLE_BTA					BIT(3)

struct mdfld_dsi_connector {
	struct gma_connector base;

	int pipe;
	void *private;
	void *pkg_sender;

	/* Connection status */
	enum drm_connector_status status;
};

struct mdfld_dsi_encoder {
	struct gma_encoder base;
	void *private;
};

/*
 * DSI config, consists of one DSI connector, two DSI encoders.
 * DRM will pick up on DSI encoder basing on differents configs.
 */
struct mdfld_dsi_config {
	struct drm_device *dev;
	struct drm_display_mode *fixed_mode;
	struct drm_display_mode *mode;

	struct mdfld_dsi_connector *connector;
	struct mdfld_dsi_encoder *encoder;

	int changed;

	int bpp;
	int lane_count;
	/*Virtual channel number for this encoder*/
	int channel_num;
	/*video mode configure*/
	int video_mode;

	int dvr_ic_inited;
};

static inline struct mdfld_dsi_connector *mdfld_dsi_connector(
		struct drm_connector *connector)
{
	struct gma_connector *gma_connector;

	gma_connector = to_gma_connector(connector);

	return container_of(gma_connector, struct mdfld_dsi_connector, base);
}

static inline struct mdfld_dsi_encoder *mdfld_dsi_encoder(
		struct drm_encoder *encoder)
{
	struct gma_encoder *gma_encoder;

	gma_encoder = to_gma_encoder(encoder);

	return container_of(gma_encoder, struct mdfld_dsi_encoder, base);
}

static inline struct mdfld_dsi_config *
	mdfld_dsi_get_config(struct mdfld_dsi_connector *connector)
{
	if (!connector)
		return NULL;
	return (struct mdfld_dsi_config *)connector->private;
}

static inline void *mdfld_dsi_get_pkg_sender(struct mdfld_dsi_config *config)
{
	struct mdfld_dsi_connector *dsi_connector;

	if (!config)
		return NULL;

	dsi_connector = config->connector;

	if (!dsi_connector)
		return NULL;

	return dsi_connector->pkg_sender;
}

static inline struct mdfld_dsi_config *
	mdfld_dsi_encoder_get_config(struct mdfld_dsi_encoder *encoder)
{
	if (!encoder)
		return NULL;
	return (struct mdfld_dsi_config *)encoder->private;
}

static inline struct mdfld_dsi_connector *
	mdfld_dsi_encoder_get_connector(struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_config *config;

	if (!encoder)
		return NULL;

	config = mdfld_dsi_encoder_get_config(encoder);
	if (!config)
		return NULL;

	return config->connector;
}

static inline void *mdfld_dsi_encoder_get_pkg_sender(
				struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_config *dsi_config;

	dsi_config = mdfld_dsi_encoder_get_config(encoder);
	if (!dsi_config)
		return NULL;

	return mdfld_dsi_get_pkg_sender(dsi_config);
}

static inline int mdfld_dsi_encoder_get_pipe(struct mdfld_dsi_encoder *encoder)
{
	struct mdfld_dsi_connector *connector;

	if (!encoder)
		return -1;

	connector = mdfld_dsi_encoder_get_connector(encoder);
	if (!connector)
		return -1;
	return connector->pipe;
}

/* Export functions */
extern void mdfld_dsi_gen_fifo_ready(struct drm_device *dev,
					u32 gen_fifo_stat_reg, u32 fifo_stat);
extern void mdfld_dsi_brightness_init(struct mdfld_dsi_config *dsi_config,
					int pipe);
extern void mdfld_dsi_brightness_control(struct drm_device *dev, int pipe,
					int level);
extern void mdfld_dsi_output_init(struct drm_device *dev,
					int pipe,
					const struct panel_funcs *p_vid_funcs);
extern void mdfld_dsi_controller_init(struct mdfld_dsi_config *dsi_config,
					int pipe);

extern int mdfld_dsi_get_power_mode(struct mdfld_dsi_config *dsi_config,
					u32 *mode, bool hs);
extern int mdfld_dsi_panel_reset(int pipe);

#endif /*__MDFLD_DSI_OUTPUT_H__*/
