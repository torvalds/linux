/*
 * Copyright © 2011 Intel Corporation
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
 */

/* Medfield DSI controller registers */

#define MIPIA_DEVICE_READY_REG				0xb000
#define MIPIA_INTR_STAT_REG				0xb004
#define MIPIA_INTR_EN_REG				0xb008
#define MIPIA_DSI_FUNC_PRG_REG				0xb00c
#define MIPIA_HS_TX_TIMEOUT_REG				0xb010
#define MIPIA_LP_RX_TIMEOUT_REG				0xb014
#define MIPIA_TURN_AROUND_TIMEOUT_REG			0xb018
#define MIPIA_DEVICE_RESET_TIMER_REG			0xb01c
#define MIPIA_DPI_RESOLUTION_REG			0xb020
#define MIPIA_DBI_FIFO_THROTTLE_REG			0xb024
#define MIPIA_HSYNC_COUNT_REG				0xb028
#define MIPIA_HBP_COUNT_REG				0xb02c
#define MIPIA_HFP_COUNT_REG				0xb030
#define MIPIA_HACTIVE_COUNT_REG				0xb034
#define MIPIA_VSYNC_COUNT_REG				0xb038
#define MIPIA_VBP_COUNT_REG				0xb03c
#define MIPIA_VFP_COUNT_REG				0xb040
#define MIPIA_HIGH_LOW_SWITCH_COUNT_REG			0xb044
#define MIPIA_DPI_CONTROL_REG				0xb048
#define MIPIA_DPI_DATA_REG				0xb04c
#define MIPIA_INIT_COUNT_REG				0xb050
#define MIPIA_MAX_RETURN_PACK_SIZE_REG			0xb054
#define MIPIA_VIDEO_MODE_FORMAT_REG			0xb058
#define MIPIA_EOT_DISABLE_REG				0xb05c
#define MIPIA_LP_BYTECLK_REG				0xb060
#define MIPIA_LP_GEN_DATA_REG				0xb064
#define MIPIA_HS_GEN_DATA_REG				0xb068
#define MIPIA_LP_GEN_CTRL_REG				0xb06c
#define MIPIA_HS_GEN_CTRL_REG				0xb070
#define MIPIA_GEN_FIFO_STAT_REG				0xb074
#define MIPIA_HS_LS_DBI_ENABLE_REG			0xb078
#define MIPIA_DPHY_PARAM_REG				0xb080
#define MIPIA_DBI_BW_CTRL_REG				0xb084
#define MIPIA_CLK_LANE_SWITCH_TIME_CNT_REG		0xb088

#define DSI_DEVICE_READY				(0x1)
#define DSI_POWER_STATE_ULPS_ENTER			(0x2 << 1)
#define DSI_POWER_STATE_ULPS_EXIT			(0x1 << 1)
#define DSI_POWER_STATE_ULPS_OFFSET			(0x1)


#define DSI_ONE_DATA_LANE				(0x1)
#define DSI_TWO_DATA_LANE				(0x2)
#define DSI_THREE_DATA_LANE				(0X3)
#define DSI_FOUR_DATA_LANE				(0x4)
#define DSI_DPI_VIRT_CHANNEL_OFFSET			(0x3)
#define DSI_DBI_VIRT_CHANNEL_OFFSET			(0x5)
#define DSI_DPI_COLOR_FORMAT_RGB565			(0x01 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB666			(0x02 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB666_UNPACK		(0x03 << 7)
#define DSI_DPI_COLOR_FORMAT_RGB888			(0x04 << 7)
#define DSI_DBI_COLOR_FORMAT_OPTION2			(0x05 << 13)

#define DSI_INTR_STATE_RXSOTERROR			1

#define DSI_INTR_STATE_SPL_PKG_SENT			(1 << 30)
#define DSI_INTR_STATE_TE				(1 << 31)

#define DSI_HS_TX_TIMEOUT_MASK				(0xffffff)

#define DSI_LP_RX_TIMEOUT_MASK				(0xffffff)

#define DSI_TURN_AROUND_TIMEOUT_MASK			(0x3f)

#define DSI_RESET_TIMER_MASK				(0xffff)

#define DSI_DBI_FIFO_WM_HALF				(0x0)
#define DSI_DBI_FIFO_WM_QUARTER				(0x1)
#define DSI_DBI_FIFO_WM_LOW				(0x2)

#define DSI_DPI_TIMING_MASK				(0xffff)

#define DSI_INIT_TIMER_MASK				(0xffff)

#define DSI_DBI_RETURN_PACK_SIZE_MASK			(0x3ff)

#define DSI_LP_BYTECLK_MASK				(0x0ffff)

#define DSI_HS_CTRL_GEN_SHORT_W0			(0x03)
#define DSI_HS_CTRL_GEN_SHORT_W1			(0x13)
#define DSI_HS_CTRL_GEN_SHORT_W2			(0x23)
#define DSI_HS_CTRL_GEN_R0				(0x04)
#define DSI_HS_CTRL_GEN_R1				(0x14)
#define DSI_HS_CTRL_GEN_R2				(0x24)
#define DSI_HS_CTRL_GEN_LONG_W				(0x29)
#define DSI_HS_CTRL_MCS_SHORT_W0			(0x05)
#define DSI_HS_CTRL_MCS_SHORT_W1			(0x15)
#define DSI_HS_CTRL_MCS_R0				(0x06)
#define DSI_HS_CTRL_MCS_LONG_W				(0x39)
#define DSI_HS_CTRL_VC_OFFSET				(0x06)
#define DSI_HS_CTRL_WC_OFFSET				(0x08)

#define	DSI_FIFO_GEN_HS_DATA_FULL			(1 << 0)
#define DSI_FIFO_GEN_HS_DATA_HALF_EMPTY			(1 << 1)
#define DSI_FIFO_GEN_HS_DATA_EMPTY			(1 << 2)
#define DSI_FIFO_GEN_LP_DATA_FULL			(1 << 8)
#define DSI_FIFO_GEN_LP_DATA_HALF_EMPTY			(1 << 9)
#define DSI_FIFO_GEN_LP_DATA_EMPTY			(1 << 10)
#define DSI_FIFO_GEN_HS_CTRL_FULL			(1 << 16)
#define DSI_FIFO_GEN_HS_CTRL_HALF_EMPTY			(1 << 17)
#define DSI_FIFO_GEN_HS_CTRL_EMPTY			(1 << 18)
#define DSI_FIFO_GEN_LP_CTRL_FULL			(1 << 24)
#define DSI_FIFO_GEN_LP_CTRL_HALF_EMPTY			(1 << 25)
#define DSI_FIFO_GEN_LP_CTRL_EMPTY			(1 << 26)
#define DSI_FIFO_DBI_EMPTY				(1 << 27)
#define DSI_FIFO_DPI_EMPTY				(1 << 28)

#define DSI_DBI_HS_LP_SWITCH_MASK			(0x1)

#define DSI_HS_LP_SWITCH_COUNTER_OFFSET			(0x0)
#define DSI_LP_HS_SWITCH_COUNTER_OFFSET			(0x16)

#define DSI_DPI_CTRL_HS_SHUTDOWN			(0x00000001)
#define DSI_DPI_CTRL_HS_TURN_ON				(0x00000002)

/* Medfield DSI adapter registers */
#define MIPIA_CONTROL_REG				0xb104
#define MIPIA_DATA_ADD_REG				0xb108
#define MIPIA_DATA_LEN_REG				0xb10c
#define MIPIA_CMD_ADD_REG				0xb110
#define MIPIA_CMD_LEN_REG				0xb114

/*dsi power modes*/
#define DSI_POWER_MODE_DISPLAY_ON	(1 << 2)
#define DSI_POWER_MODE_NORMAL_ON	(1 << 3)
#define DSI_POWER_MODE_SLEEP_OUT	(1 << 4)
#define DSI_POWER_MODE_PARTIAL_ON	(1 << 5)
#define DSI_POWER_MODE_IDLE_ON		(1 << 6)

enum {
	MDFLD_DSI_ENCODER_DBI = 0,
	MDFLD_DSI_ENCODER_DPI,
};

enum {
	MDFLD_DSI_VIDEO_NON_BURST_MODE_SYNC_PULSE = 1,
	MDFLD_DSI_VIDEO_NON_BURST_MODE_SYNC_EVENTS = 2,
	MDFLD_DSI_VIDEO_BURST_MODE = 3,
};

#define DSI_DPI_COMPLETE_LAST_LINE			(1 << 2)
#define DSI_DPI_DISABLE_BTA				(1 << 3)
/* Panel types */
enum {
	TPO_CMD,
	TPO_VID,
	TMD_CMD,
	TMD_VID,
	PYR_CMD,
	PYR_VID,
	TPO,
	TMD,
	PYR,
	HDMI,
	GCT_DETECT
};

/* Junk that belongs elsewhere */
#define TPO_PANEL_WIDTH		84
#define TPO_PANEL_HEIGHT	46
#define TMD_PANEL_WIDTH		39
#define TMD_PANEL_HEIGHT	71
#define PYR_PANEL_WIDTH		53
#define PYR_PANEL_HEIGHT	95

/* Panel interface */
struct panel_info {
	u32 width_mm;
	u32 height_mm;
};

struct mdfld_dsi_dbi_output;

struct mdfld_dsi_connector_state {
	u32 mipi_ctrl_reg;
};

struct mdfld_dsi_encoder_state {

};

struct mdfld_dsi_connector {
	/*
	 * This is ugly, but I have to use connector in it! :-(
	 * FIXME: use drm_connector instead.
	 */
	struct psb_intel_output base;

	int pipe;
	void *private;
	void *pkg_sender;

	/* Connection status */
	enum drm_connector_status status;
};

struct mdfld_dsi_encoder {
	struct drm_encoder base;
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
	struct mdfld_dsi_encoder *encoders[DRM_CONNECTOR_MAX_ENCODER];
	struct mdfld_dsi_encoder *encoder;

	int changed;

	int bpp;
	int type;
	int lane_count;
	/*Virtual channel number for this encoder*/
	int channel_num;
	/*video mode configure*/
	int video_mode;

	int dvr_ic_inited;
};

#define MDFLD_DSI_CONNECTOR(psb_output) \
		(container_of(psb_output, struct mdfld_dsi_connector, base))

#define MDFLD_DSI_ENCODER(encoder) \
		(container_of(encoder, struct mdfld_dsi_encoder, base))

struct panel_funcs {
	const struct drm_encoder_funcs *encoder_funcs;
	const struct drm_encoder_helper_funcs *encoder_helper_funcs;
	struct drm_display_mode *(*get_config_mode) (struct drm_device *);
	void (*update_fb) (struct mdfld_dsi_dbi_output *, int);
	int (*get_panel_info) (struct drm_device *, int, struct panel_info *);
	int (*reset)(int pipe);
	void (*drv_ic_init)(struct mdfld_dsi_config *dsi_config, int pipe);
};

