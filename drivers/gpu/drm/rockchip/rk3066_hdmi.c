// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *    Zheng Yang <zhengyang@rock-chips.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_bridge_connector.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "rk3066_hdmi.h"

#include "rockchip_drm_drv.h"

#define DEFAULT_PLLA_RATE 30000000

struct hdmi_data_info {
	int vic; /* The CEA Video ID (VIC) of the current drm display mode. */
	unsigned int enc_out_format;
	unsigned int colorimetry;
};

struct rk3066_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;
	u8 stat;

	struct mutex i2c_lock; /* For i2c operation. */
	struct completion cmpltn;
};

struct rk3066_hdmi {
	struct device *dev;
	struct drm_device *drm_dev;
	struct regmap *grf_regmap;
	int irq;
	struct clk *hclk;
	void __iomem *regs;

	struct drm_bridge bridge;
	struct drm_connector *connector;
	struct rockchip_encoder encoder;

	struct rk3066_hdmi_i2c *i2c;

	unsigned int tmdsclk;

	struct hdmi_data_info hdmi_data;
};

static struct rk3066_hdmi *bridge_to_rk3066_hdmi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk3066_hdmi, bridge);
}

static inline u8 hdmi_readb(struct rk3066_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->regs + offset);
}

static inline void hdmi_writeb(struct rk3066_hdmi *hdmi, u16 offset, u32 val)
{
	writel_relaxed(val, hdmi->regs + offset);
}

static inline void hdmi_modb(struct rk3066_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void rk3066_hdmi_i2c_init(struct rk3066_hdmi *hdmi)
{
	int ddc_bus_freq;

	ddc_bus_freq = (hdmi->tmdsclk >> 2) / HDMI_SCL_RATE;

	hdmi_writeb(hdmi, HDMI_DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, HDMI_DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt. */
	hdmi_modb(hdmi, HDMI_INTR_MASK1, HDMI_INTR_EDID_MASK, 0);
	hdmi_writeb(hdmi, HDMI_INTR_STATUS1, HDMI_INTR_EDID_MASK);
}

static inline u8 rk3066_hdmi_get_power_mode(struct rk3066_hdmi *hdmi)
{
	return hdmi_readb(hdmi, HDMI_SYS_CTRL) & HDMI_SYS_POWER_MODE_MASK;
}

static void rk3066_hdmi_set_power_mode(struct rk3066_hdmi *hdmi, int mode)
{
	u8 current_mode, next_mode;
	u8 i = 0;

	current_mode = rk3066_hdmi_get_power_mode(hdmi);

	DRM_DEV_DEBUG(hdmi->dev, "mode         :%d\n", mode);
	DRM_DEV_DEBUG(hdmi->dev, "current_mode :%d\n", current_mode);

	if (current_mode == mode)
		return;

	do {
		if (current_mode > mode) {
			next_mode = current_mode / 2;
		} else {
			if (current_mode < HDMI_SYS_POWER_MODE_A)
				next_mode = HDMI_SYS_POWER_MODE_A;
			else
				next_mode = current_mode * 2;
		}

		DRM_DEV_DEBUG(hdmi->dev, "%d: next_mode :%d\n", i, next_mode);

		if (next_mode != HDMI_SYS_POWER_MODE_D) {
			hdmi_modb(hdmi, HDMI_SYS_CTRL,
				  HDMI_SYS_POWER_MODE_MASK, next_mode);
		} else {
			hdmi_writeb(hdmi, HDMI_SYS_CTRL,
				    HDMI_SYS_POWER_MODE_D |
				    HDMI_SYS_PLL_RESET_MASK);
			usleep_range(90, 100);
			hdmi_writeb(hdmi, HDMI_SYS_CTRL,
				    HDMI_SYS_POWER_MODE_D |
				    HDMI_SYS_PLLB_RESET);
			usleep_range(90, 100);
			hdmi_writeb(hdmi, HDMI_SYS_CTRL,
				    HDMI_SYS_POWER_MODE_D);
		}
		current_mode = next_mode;
		i = i + 1;
	} while ((next_mode != mode) && (i < 5));

	/*
	 * When the IP controller isn't configured with accurate video timing,
	 * DDC_CLK should be equal to the PLLA frequency, which is 30MHz,
	 * so we need to init the TMDS rate to the PCLK rate and reconfigure
	 * the DDC clock.
	 */
	if (mode < HDMI_SYS_POWER_MODE_D)
		hdmi->tmdsclk = DEFAULT_PLLA_RATE;
}

static int rk3066_hdmi_bridge_clear_infoframe(struct drm_bridge *bridge,
					      enum hdmi_infoframe_type type)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);

	if (type != HDMI_INFOFRAME_TYPE_AVI) {
		drm_err(bridge->dev, "Unsupported infoframe type: %u\n", type);
		return 0;
	}

	hdmi_writeb(hdmi, HDMI_CP_BUF_INDEX, HDMI_INFOFRAME_AVI);

	return 0;
}

static int
rk3066_hdmi_bridge_write_infoframe(struct drm_bridge *bridge,
				   enum hdmi_infoframe_type type,
				   const u8 *buffer, size_t len)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);
	ssize_t i;

	if (type != HDMI_INFOFRAME_TYPE_AVI) {
		drm_err(bridge->dev, "Unsupported infoframe type: %u\n", type);
		return 0;
	}

	rk3066_hdmi_bridge_clear_infoframe(bridge, type);

	for (i = 0; i < len; i++)
		hdmi_writeb(hdmi, HDMI_CP_BUF_ACC_HB0 + i * 4, buffer[i]);

	return 0;
}

static int rk3066_hdmi_config_video_timing(struct rk3066_hdmi *hdmi,
					   struct drm_display_mode *mode)
{
	int value, vsync_offset;

	/* Set the details for the external polarity and interlace mode. */
	value = HDMI_EXT_VIDEO_SET_EN;
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		 HDMI_VIDEO_HSYNC_ACTIVE_HIGH : HDMI_VIDEO_HSYNC_ACTIVE_LOW;
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		 HDMI_VIDEO_VSYNC_ACTIVE_HIGH : HDMI_VIDEO_VSYNC_ACTIVE_LOW;
	value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		 HDMI_VIDEO_MODE_INTERLACE : HDMI_VIDEO_MODE_PROGRESSIVE;

	if (hdmi->hdmi_data.vic == 2 || hdmi->hdmi_data.vic == 3)
		vsync_offset = 6;
	else
		vsync_offset = 0;

	value |= vsync_offset << HDMI_VIDEO_VSYNC_OFFSET_SHIFT;
	hdmi_writeb(hdmi, HDMI_EXT_VIDEO_PARA, value);

	/* Set the details for the external video timing. */
	value = mode->htotal;
	hdmi_writeb(hdmi, HDMI_EXT_HTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_EXT_HTOTAL_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hdisplay;
	hdmi_writeb(hdmi, HDMI_EXT_HBLANK_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_EXT_HBLANK_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_EXT_HDELAY_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_EXT_HDELAY_H, (value >> 8) & 0xFF);

	value = mode->hsync_end - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_EXT_HDURATION_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_EXT_HDURATION_H, (value >> 8) & 0xFF);

	value = mode->vtotal;
	hdmi_writeb(hdmi, HDMI_EXT_VTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_EXT_VTOTAL_H, (value >> 8) & 0xFF);

	value = mode->vtotal - mode->vdisplay;
	hdmi_writeb(hdmi, HDMI_EXT_VBLANK_L, value & 0xFF);

	value = mode->vtotal - mode->vsync_start + vsync_offset;
	hdmi_writeb(hdmi, HDMI_EXT_VDELAY, value & 0xFF);

	value = mode->vsync_end - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_EXT_VDURATION, value & 0xFF);

	return 0;
}

static void
rk3066_hdmi_phy_write(struct rk3066_hdmi *hdmi, u16 offset, u8 value)
{
	hdmi_writeb(hdmi, offset, value);
	hdmi_modb(hdmi, HDMI_SYS_CTRL,
		  HDMI_SYS_PLL_RESET_MASK, HDMI_SYS_PLL_RESET);
	usleep_range(90, 100);
	hdmi_modb(hdmi, HDMI_SYS_CTRL, HDMI_SYS_PLL_RESET_MASK, 0);
	usleep_range(900, 1000);
}

static void rk3066_hdmi_config_phy(struct rk3066_hdmi *hdmi)
{
	/* TMDS uses the same frequency as dclk. */
	hdmi_writeb(hdmi, HDMI_DEEP_COLOR_MODE, 0x22);

	/*
	 * The semi-public documentation does not describe the hdmi registers
	 * used by the function rk3066_hdmi_phy_write(), so we keep using
	 * these magic values for now.
	 */
	if (hdmi->tmdsclk > 100000000) {
		rk3066_hdmi_phy_write(hdmi, 0x158, 0x0E);
		rk3066_hdmi_phy_write(hdmi, 0x15c, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x160, 0x60);
		rk3066_hdmi_phy_write(hdmi, 0x164, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x168, 0xDA);
		rk3066_hdmi_phy_write(hdmi, 0x16c, 0xA1);
		rk3066_hdmi_phy_write(hdmi, 0x170, 0x0e);
		rk3066_hdmi_phy_write(hdmi, 0x174, 0x22);
		rk3066_hdmi_phy_write(hdmi, 0x178, 0x00);
	} else if (hdmi->tmdsclk > 50000000) {
		rk3066_hdmi_phy_write(hdmi, 0x158, 0x06);
		rk3066_hdmi_phy_write(hdmi, 0x15c, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x160, 0x60);
		rk3066_hdmi_phy_write(hdmi, 0x164, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x168, 0xCA);
		rk3066_hdmi_phy_write(hdmi, 0x16c, 0xA3);
		rk3066_hdmi_phy_write(hdmi, 0x170, 0x0e);
		rk3066_hdmi_phy_write(hdmi, 0x174, 0x20);
		rk3066_hdmi_phy_write(hdmi, 0x178, 0x00);
	} else {
		rk3066_hdmi_phy_write(hdmi, 0x158, 0x02);
		rk3066_hdmi_phy_write(hdmi, 0x15c, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x160, 0x60);
		rk3066_hdmi_phy_write(hdmi, 0x164, 0x00);
		rk3066_hdmi_phy_write(hdmi, 0x168, 0xC2);
		rk3066_hdmi_phy_write(hdmi, 0x16c, 0xA2);
		rk3066_hdmi_phy_write(hdmi, 0x170, 0x0e);
		rk3066_hdmi_phy_write(hdmi, 0x174, 0x20);
		rk3066_hdmi_phy_write(hdmi, 0x178, 0x00);
	}
}

static int rk3066_hdmi_setup(struct rk3066_hdmi *hdmi,
			     struct drm_atomic_state *state)
{
	struct drm_bridge *bridge = &hdmi->bridge;
	struct drm_connector *connector;
	struct drm_display_info *display;
	struct drm_display_mode *mode;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc_state *new_crtc_state;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!new_conn_state))
		return -EINVAL;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	if (WARN_ON(!new_crtc_state))
		return -EINVAL;

	display = &connector->display_info;
	mode = &new_crtc_state->adjusted_mode;

	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);
	hdmi->hdmi_data.enc_out_format = HDMI_COLORSPACE_RGB;

	if (hdmi->hdmi_data.vic == 6 || hdmi->hdmi_data.vic == 7 ||
	    hdmi->hdmi_data.vic == 21 || hdmi->hdmi_data.vic == 22 ||
	    hdmi->hdmi_data.vic == 2 || hdmi->hdmi_data.vic == 3 ||
	    hdmi->hdmi_data.vic == 17 || hdmi->hdmi_data.vic == 18)
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_709;

	hdmi->tmdsclk = mode->clock * 1000;

	/* Mute video and audio output. */
	hdmi_modb(hdmi, HDMI_VIDEO_CTRL2, HDMI_VIDEO_AUDIO_DISABLE_MASK,
		  HDMI_AUDIO_DISABLE | HDMI_VIDEO_DISABLE);

	/* Set power state to mode B. */
	if (rk3066_hdmi_get_power_mode(hdmi) != HDMI_SYS_POWER_MODE_B)
		rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_B);

	/* Input video mode is RGB 24 bit. Use external data enable signal. */
	hdmi_modb(hdmi, HDMI_AV_CTRL1,
		  HDMI_VIDEO_DE_MASK, HDMI_VIDEO_EXTERNAL_DE);
	hdmi_writeb(hdmi, HDMI_VIDEO_CTRL1,
		    HDMI_VIDEO_OUTPUT_RGB444 |
		    HDMI_VIDEO_INPUT_DATA_DEPTH_8BIT |
		    HDMI_VIDEO_INPUT_COLOR_RGB);
	hdmi_writeb(hdmi, HDMI_DEEP_COLOR_MODE, 0x20);

	rk3066_hdmi_config_video_timing(hdmi, mode);

	if (display->is_hdmi) {
		hdmi_modb(hdmi, HDMI_HDCP_CTRL, HDMI_VIDEO_MODE_MASK,
			  HDMI_VIDEO_MODE_HDMI);
		drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);
	} else {
		hdmi_modb(hdmi, HDMI_HDCP_CTRL, HDMI_VIDEO_MODE_MASK, 0);
	}

	rk3066_hdmi_config_phy(hdmi);

	rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_E);

	/*
	 * When the IP controller is configured with accurate video
	 * timing, the TMDS clock source should be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to the pixel mode
	 * clock rate and reconfigure the DDC clock.
	 */
	rk3066_hdmi_i2c_init(hdmi);

	/* Unmute video output. */
	hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
		  HDMI_VIDEO_AUDIO_DISABLE_MASK, HDMI_AUDIO_DISABLE);
	return 0;
}

static void rk3066_hdmi_bridge_atomic_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	int mux, val;

	conn_state = drm_atomic_get_new_connector_state(state, hdmi->connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mux = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, &hdmi->encoder.encoder);
	if (mux)
		val = (HDMI_VIDEO_SEL << 16) | HDMI_VIDEO_SEL;
	else
		val = HDMI_VIDEO_SEL << 16;

	regmap_write(hdmi->grf_regmap, GRF_SOC_CON0, val);

	DRM_DEV_DEBUG(hdmi->dev, "hdmi encoder enable select: vop%s\n",
		      (mux) ? "1" : "0");

	rk3066_hdmi_setup(hdmi, state);
}

static void rk3066_hdmi_bridge_atomic_disable(struct drm_bridge *bridge,
					      struct drm_atomic_state *state)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);

	DRM_DEV_DEBUG(hdmi->dev, "hdmi encoder disable\n");

	if (rk3066_hdmi_get_power_mode(hdmi) == HDMI_SYS_POWER_MODE_E) {
		hdmi_writeb(hdmi, HDMI_VIDEO_CTRL2,
			    HDMI_VIDEO_AUDIO_DISABLE_MASK);
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_CP_LOGIC_RESET_MASK,
			  HDMI_AUDIO_CP_LOGIC_RESET);
		usleep_range(500, 510);
	}
	rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_A);
}

static int
rk3066_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const
struct drm_encoder_helper_funcs rk3066_hdmi_encoder_helper_funcs = {
	.atomic_check   = rk3066_hdmi_encoder_atomic_check,
};

static enum drm_connector_status
rk3066_hdmi_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);

	return (hdmi_readb(hdmi, HDMI_HPG_MENS_STA) & HDMI_HPG_IN_STATUS_HIGH) ?
		connector_status_connected : connector_status_disconnected;
}

static const struct drm_edid *
rk3066_hdmi_bridge_edid_read(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct rk3066_hdmi *hdmi = bridge_to_rk3066_hdmi(bridge);
	const struct drm_edid *drm_edid;

	drm_edid = drm_edid_read_ddc(connector, bridge->ddc);
	if (!drm_edid)
		dev_dbg(hdmi->dev, "failed to get edid\n");

	return drm_edid;
}

static enum drm_mode_status
rk3066_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
			      const struct drm_display_info *info,
			      const struct drm_display_mode *mode)
{
	u32 vic = drm_match_cea_mode(mode);

	if (vic > 1)
		return MODE_OK;
	else
		return MODE_BAD;
}

static const struct drm_bridge_funcs rk3066_hdmi_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = rk3066_hdmi_bridge_atomic_enable,
	.atomic_disable = rk3066_hdmi_bridge_atomic_disable,
	.detect = rk3066_hdmi_bridge_detect,
	.edid_read = rk3066_hdmi_bridge_edid_read,
	.hdmi_clear_infoframe = rk3066_hdmi_bridge_clear_infoframe,
	.hdmi_write_infoframe = rk3066_hdmi_bridge_write_infoframe,
	.mode_valid = rk3066_hdmi_bridge_mode_valid,
};


static irqreturn_t rk3066_hdmi_hardirq(int irq, void *dev_id)
{
	struct rk3066_hdmi *hdmi = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u8 interrupt;

	if (rk3066_hdmi_get_power_mode(hdmi) == HDMI_SYS_POWER_MODE_A)
		hdmi_writeb(hdmi, HDMI_SYS_CTRL, HDMI_SYS_POWER_MODE_B);

	interrupt = hdmi_readb(hdmi, HDMI_INTR_STATUS1);
	if (interrupt)
		hdmi_writeb(hdmi, HDMI_INTR_STATUS1, interrupt);

	if (interrupt & HDMI_INTR_EDID_MASK) {
		hdmi->i2c->stat = interrupt;
		complete(&hdmi->i2c->cmpltn);
	}

	if (interrupt & (HDMI_INTR_HOTPLUG | HDMI_INTR_MSENS))
		ret = IRQ_WAKE_THREAD;

	return ret;
}

static irqreturn_t rk3066_hdmi_irq(int irq, void *dev_id)
{
	struct rk3066_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->connector->dev);

	return IRQ_HANDLED;
}

static int rk3066_hdmi_i2c_read(struct rk3066_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	ret = wait_for_completion_timeout(&hdmi->i2c->cmpltn, HZ / 10);
	if (!ret || hdmi->i2c->stat & HDMI_INTR_EDID_ERR)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_DDC_READ_FIFO_ADDR);

	return 0;
}

static int rk3066_hdmi_i2c_write(struct rk3066_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only supports read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of the EDID word address.
	 */
	if (msgs->len != 1 ||
	    (msgs->addr != DDC_ADDR && msgs->addr != DDC_SEGMENT_ADDR))
		return -EINVAL;

	reinit_completion(&hdmi->i2c->cmpltn);

	if (msgs->addr == DDC_SEGMENT_ADDR)
		hdmi->i2c->segment_addr = msgs->buf[0];
	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];

	/* Set edid fifo first address. */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_ADDR, 0x00);

	/* Set edid word address 0x00/0x80. */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer. */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int rk3066_hdmi_i2c_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	struct rk3066_hdmi *hdmi = i2c_get_adapdata(adap);
	struct rk3066_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->i2c_lock);

	rk3066_hdmi_i2c_init(hdmi);

	/* Unmute HDMI EDID interrupt. */
	hdmi_modb(hdmi, HDMI_INTR_MASK1,
		  HDMI_INTR_EDID_MASK, HDMI_INTR_EDID_MASK);
	i2c->stat = 0;

	for (i = 0; i < num; i++) {
		DRM_DEV_DEBUG(hdmi->dev,
			      "xfer: num: %d/%d, len: %d, flags: %#x\n",
			      i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = rk3066_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = rk3066_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt. */
	hdmi_modb(hdmi, HDMI_INTR_MASK1, HDMI_INTR_EDID_MASK, 0);

	mutex_unlock(&i2c->i2c_lock);

	return ret;
}

static u32 rk3066_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rk3066_hdmi_algorithm = {
	.master_xfer   = rk3066_hdmi_i2c_xfer,
	.functionality = rk3066_hdmi_i2c_func,
};

static struct i2c_adapter *rk3066_hdmi_i2c_adapter(struct rk3066_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct rk3066_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->i2c_lock);
	init_completion(&i2c->cmpltn);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &rk3066_hdmi_algorithm;
	strscpy(adap->name, "RK3066 HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = devm_i2c_add_adapter(hdmi->dev, adap);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "cannot add %s I2C adapter\n",
			      adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	DRM_DEV_DEBUG(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static int
rk3066_hdmi_register(struct drm_device *drm, struct rk3066_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder.encoder;
	struct device *dev = hdmi->dev;
	int ret;

	encoder->possible_crtcs =
		drm_of_find_possible_crtcs(drm, dev->of_node);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &rk3066_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->bridge.driver_private = hdmi;
	hdmi->bridge.funcs = &rk3066_hdmi_bridge_funcs;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT |
			   DRM_BRIDGE_OP_EDID |
			   DRM_BRIDGE_OP_HDMI |
			   DRM_BRIDGE_OP_HPD;
	hdmi->bridge.of_node = hdmi->dev->of_node;
	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->bridge.vendor = "Rockchip";
	hdmi->bridge.product = "RK3066 HDMI";

	hdmi->bridge.ddc = rk3066_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->bridge.ddc))
		return PTR_ERR(hdmi->bridge.ddc);

	if (IS_ERR(hdmi->bridge.ddc))
		return PTR_ERR(hdmi->bridge.ddc);

	ret = devm_drm_bridge_add(dev, &hdmi->bridge);
	if (ret)
		return ret;

	ret = drm_bridge_attach(encoder, &hdmi->bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ret;

	hdmi->connector = drm_bridge_connector_init(drm, encoder);
	if (IS_ERR(hdmi->connector)) {
		ret = PTR_ERR(hdmi->connector);
		dev_err(hdmi->dev, "failed to init bridge connector: %d\n", ret);
		return ret;
	}

	drm_connector_attach_encoder(hdmi->connector, encoder);

	return 0;
}

static int rk3066_hdmi_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct rk3066_hdmi *hdmi;
	int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm_dev = drm;
	hdmi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	hdmi->hclk = devm_clk_get(dev, "hclk");
	if (IS_ERR(hdmi->hclk)) {
		DRM_DEV_ERROR(dev, "unable to get HDMI hclk clock\n");
		return PTR_ERR(hdmi->hclk);
	}

	ret = clk_prepare_enable(hdmi->hclk);
	if (ret) {
		DRM_DEV_ERROR(dev, "cannot enable HDMI hclk clock: %d\n", ret);
		return ret;
	}

	hdmi->grf_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
							   "rockchip,grf");
	if (IS_ERR(hdmi->grf_regmap)) {
		DRM_DEV_ERROR(dev, "unable to get rockchip,grf\n");
		ret = PTR_ERR(hdmi->grf_regmap);
		goto err_disable_hclk;
	}

	/* internal hclk = hdmi_hclk / 25 */
	hdmi_writeb(hdmi, HDMI_INTERNAL_CLK_DIVIDER, 25);

	rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_B);
	usleep_range(999, 1000);
	hdmi_writeb(hdmi, HDMI_INTR_MASK1, HDMI_INTR_HOTPLUG);
	hdmi_writeb(hdmi, HDMI_INTR_MASK2, 0);
	hdmi_writeb(hdmi, HDMI_INTR_MASK3, 0);
	hdmi_writeb(hdmi, HDMI_INTR_MASK4, 0);
	rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_A);

	ret = rk3066_hdmi_register(drm, hdmi);
	if (ret)
		goto err_disable_hclk;

	dev_set_drvdata(dev, hdmi);

	ret = devm_request_threaded_irq(dev, irq, rk3066_hdmi_hardirq,
					rk3066_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to request hdmi irq: %d\n", ret);
		goto err_cleanup_hdmi;
	}

	return 0;

err_cleanup_hdmi:
	hdmi->encoder.encoder.funcs->destroy(&hdmi->encoder.encoder);
err_disable_hclk:
	clk_disable_unprepare(hdmi->hclk);

	return ret;
}

static void rk3066_hdmi_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct rk3066_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->encoder.encoder.funcs->destroy(&hdmi->encoder.encoder);

	clk_disable_unprepare(hdmi->hclk);
}

static const struct component_ops rk3066_hdmi_ops = {
	.bind   = rk3066_hdmi_bind,
	.unbind = rk3066_hdmi_unbind,
};

static int rk3066_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &rk3066_hdmi_ops);
}

static void rk3066_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rk3066_hdmi_ops);
}

static const struct of_device_id rk3066_hdmi_dt_ids[] = {
	{ .compatible = "rockchip,rk3066-hdmi" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rk3066_hdmi_dt_ids);

struct platform_driver rk3066_hdmi_driver = {
	.probe  = rk3066_hdmi_probe,
	.remove = rk3066_hdmi_remove,
	.driver = {
		.name = "rockchip-rk3066-hdmi",
		.of_match_table = rk3066_hdmi_dt_ids,
	},
};
