// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *    Zheng Yang <zhengyang@rock-chips.com>
 *    Yakir Yang <ykk@rock-chips.com>
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

#include "rockchip_drm_drv.h"

#include "inno_hdmi.h"

#define INNO_HDMI_MIN_TMDS_CLOCK  25000000U

struct inno_hdmi_phy_config {
	unsigned long pixelclock;
	u8 pre_emphasis;
	u8 voltage_level_control;
};

struct inno_hdmi_variant {
	struct inno_hdmi_phy_config *phy_configs;
	struct inno_hdmi_phy_config *default_phy_config;
};

struct inno_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;

	struct mutex lock;
	struct completion cmp;
};

struct inno_hdmi {
	struct device *dev;

	struct clk *pclk;
	struct clk *refclk;
	void __iomem *regs;

	struct drm_connector	connector;
	struct rockchip_encoder	encoder;

	struct inno_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;

	const struct inno_hdmi_variant *variant;
};

struct inno_hdmi_connector_state {
	struct drm_connector_state	base;
	unsigned int			colorimetry;
};

static struct inno_hdmi *encoder_to_inno_hdmi(struct drm_encoder *encoder)
{
	struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

	return container_of(rkencoder, struct inno_hdmi, encoder);
}

static struct inno_hdmi *connector_to_inno_hdmi(struct drm_connector *connector)
{
	return container_of(connector, struct inno_hdmi, connector);
}

#define to_inno_hdmi_conn_state(conn_state) \
	container_of_const(conn_state, struct inno_hdmi_connector_state, base)

enum {
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,
};

static const char coeff_csc[][24] = {
	/*
	 * RGB2YUV:601 SD mode:
	 *   Cb = -0.291G - 0.148R + 0.439B + 128
	 *   Y  = 0.504G  + 0.257R + 0.098B + 16
	 *   Cr = -0.368G + 0.439R - 0.071B + 128
	 */
	{
		0x11, 0x5f, 0x01, 0x82, 0x10, 0x23, 0x00, 0x80,
		0x02, 0x1c, 0x00, 0xa1, 0x00, 0x36, 0x00, 0x1e,
		0x11, 0x29, 0x10, 0x59, 0x01, 0x82, 0x00, 0x80
	},
	/*
	 * RGB2YUV:709 HD mode:
	 *   Cb = - 0.338G - 0.101R + 0.439B + 128
	 *   Y  = 0.614G   + 0.183R + 0.062B + 16
	 *   Cr = - 0.399G + 0.439R - 0.040B + 128
	 */
	{
		0x11, 0x98, 0x01, 0xc1, 0x10, 0x28, 0x00, 0x80,
		0x02, 0x74, 0x00, 0xbb, 0x00, 0x3f, 0x00, 0x10,
		0x11, 0x5a, 0x10, 0x67, 0x01, 0xc1, 0x00, 0x80
	},
	/*
	 * RGB[0:255]2RGB[16:235]:
	 *   R' = R x (235-16)/255 + 16;
	 *   G' = G x (235-16)/255 + 16;
	 *   B' = B x (235-16)/255 + 16;
	 */
	{
		0x00, 0x00, 0x03, 0x6F, 0x00, 0x00, 0x00, 0x10,
		0x03, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x6F, 0x00, 0x10
	},
};

static struct inno_hdmi_phy_config rk3036_hdmi_phy_configs[] = {
	{  74250000, 0x3f, 0xbb },
	{ 165000000, 0x6f, 0xbb },
	{      ~0UL, 0x00, 0x00 }
};

static struct inno_hdmi_phy_config rk3128_hdmi_phy_configs[] = {
	{  74250000, 0x3f, 0xaa },
	{ 165000000, 0x5f, 0xaa },
	{      ~0UL, 0x00, 0x00 }
};

static int inno_hdmi_find_phy_config(struct inno_hdmi *hdmi,
				     unsigned long pixelclk)
{
	const struct inno_hdmi_phy_config *phy_configs =
						hdmi->variant->phy_configs;
	int i;

	for (i = 0; phy_configs[i].pixelclock != ~0UL; i++) {
		if (pixelclk <= phy_configs[i].pixelclock)
			return i;
	}

	DRM_DEV_DEBUG(hdmi->dev, "No phy configuration for pixelclock %lu\n",
		      pixelclk);

	return -EINVAL;
}

static inline u8 hdmi_readb(struct inno_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->regs + (offset) * 0x04);
}

static inline void hdmi_writeb(struct inno_hdmi *hdmi, u16 offset, u32 val)
{
	writel_relaxed(val, hdmi->regs + (offset) * 0x04);
}

static inline void hdmi_modb(struct inno_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void inno_hdmi_i2c_init(struct inno_hdmi *hdmi, unsigned long long rate)
{
	unsigned long long ddc_bus_freq = rate >> 2;

	do_div(ddc_bus_freq, HDMI_SCL_RATE);

	hdmi_writeb(hdmi, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);
}

static void inno_hdmi_sys_power(struct inno_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_OFF);
}

static void inno_hdmi_standby(struct inno_hdmi *hdmi)
{
	inno_hdmi_sys_power(hdmi, false);

	hdmi_writeb(hdmi, HDMI_PHY_DRIVER, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
};

static void inno_hdmi_power_up(struct inno_hdmi *hdmi,
			       unsigned long mpixelclock)
{
	struct inno_hdmi_phy_config *phy_config;
	int ret = inno_hdmi_find_phy_config(hdmi, mpixelclock);

	if (ret < 0) {
		phy_config = hdmi->variant->default_phy_config;
		DRM_DEV_ERROR(hdmi->dev,
			      "Using default phy configuration for TMDS rate %lu",
			      mpixelclock);
	} else {
		phy_config = &hdmi->variant->phy_configs[ret];
	}

	inno_hdmi_sys_power(hdmi, false);

	hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, phy_config->pre_emphasis);
	hdmi_writeb(hdmi, HDMI_PHY_DRIVER, phy_config->voltage_level_control);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x14);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x10);
	hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x0f);
	hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x01);

	inno_hdmi_sys_power(hdmi, true);
};

static void inno_hdmi_reset(struct inno_hdmi *hdmi)
{
	u32 val;
	u32 msk;

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	udelay(100);

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	udelay(100);

	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_modb(hdmi, HDMI_SYS_CTRL, msk, val);

	inno_hdmi_standby(hdmi);
}

static int inno_hdmi_disable_frame(struct drm_connector *connector,
				   enum hdmi_infoframe_type type)
{
	struct inno_hdmi *hdmi = connector_to_inno_hdmi(connector);

	if (type != HDMI_INFOFRAME_TYPE_AVI) {
		drm_err(connector->dev,
			"Unsupported infoframe type: %u\n", type);
		return 0;
	}

	hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);

	return 0;
}

static int inno_hdmi_upload_frame(struct drm_connector *connector,
				  enum hdmi_infoframe_type type,
				  const u8 *buffer, size_t len)
{
	struct inno_hdmi *hdmi = connector_to_inno_hdmi(connector);
	ssize_t i;

	if (type != HDMI_INFOFRAME_TYPE_AVI) {
		drm_err(connector->dev,
			"Unsupported infoframe type: %u\n", type);
		return 0;
	}

	inno_hdmi_disable_frame(connector, type);

	for (i = 0; i < len; i++)
		hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_ADDR + i, buffer[i]);

	return 0;
}

static const struct drm_connector_hdmi_funcs inno_hdmi_hdmi_connector_funcs = {
	.clear_infoframe	= inno_hdmi_disable_frame,
	.write_infoframe	= inno_hdmi_upload_frame,
};

static int inno_hdmi_config_video_csc(struct inno_hdmi *hdmi)
{
	struct drm_connector *connector = &hdmi->connector;
	struct drm_connector_state *conn_state = connector->state;
	struct inno_hdmi_connector_state *inno_conn_state =
					to_inno_hdmi_conn_state(conn_state);
	int c0_c2_change = 0;
	int csc_enable = 0;
	int csc_mode = 0;
	int auto_csc = 0;
	int value;
	int i;

	/* Input video mode is SDR RGB24bit, data enable signal from external */
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL1, v_DE_EXTERNAL |
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444));

	/* Input color hardcode to RGB, and output color hardcode to RGB888 */
	value = v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) |
		v_VIDEO_OUTPUT_COLOR(0) |
		v_VIDEO_INPUT_CSP(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL2, value);

	if (conn_state->hdmi.output_format == HDMI_COLORSPACE_RGB) {
		if (conn_state->hdmi.is_limited_range) {
			csc_mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;

		} else {
			value = v_SOF_DISABLE | v_COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);

			hdmi_modb(hdmi, HDMI_VIDEO_CONTRL,
				  m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_SWAP,
				  v_VIDEO_AUTO_CSC(AUTO_CSC_DISABLE) |
				  v_VIDEO_C0_C2_SWAP(C0_C2_CHANGE_DISABLE));
			return 0;
		}
	} else {
		if (inno_conn_state->colorimetry == HDMI_COLORIMETRY_ITU_601) {
			if (conn_state->hdmi.output_format == HDMI_COLORSPACE_YUV444) {
				csc_mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
				auto_csc = AUTO_CSC_DISABLE;
				c0_c2_change = C0_C2_CHANGE_DISABLE;
				csc_enable = v_CSC_ENABLE;
			}
		} else {
			if (conn_state->hdmi.output_format == HDMI_COLORSPACE_YUV444) {
				csc_mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
				auto_csc = AUTO_CSC_DISABLE;
				c0_c2_change = C0_C2_CHANGE_DISABLE;
				csc_enable = v_CSC_ENABLE;
			}
		}
	}

	for (i = 0; i < 24; i++)
		hdmi_writeb(hdmi, HDMI_VIDEO_CSC_COEF + i,
			    coeff_csc[csc_mode][i]);

	value = v_SOF_DISABLE | csc_enable | v_COLOR_DEPTH_NOT_INDICATED(1);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);
	hdmi_modb(hdmi, HDMI_VIDEO_CONTRL, m_VIDEO_AUTO_CSC |
		  m_VIDEO_C0_C2_SWAP, v_VIDEO_AUTO_CSC(auto_csc) |
		  v_VIDEO_C0_C2_SWAP(c0_c2_change));

	return 0;
}

static int inno_hdmi_config_video_timing(struct inno_hdmi *hdmi,
					 struct drm_display_mode *mode)
{
	int value;

	/* Set detail external video timing polarity and interlace mode */
	value = v_EXTERANL_VIDEO(1);
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		 v_HSYNC_POLARITY(1) : v_HSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		 v_VSYNC_POLARITY(1) : v_VSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		 v_INETLACE(1) : v_INETLACE(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_TIMING_CTL, value);

	/* Set detail external video timing */
	value = mode->htotal;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hdisplay;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);

	value = mode->hsync_end - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);

	value = mode->vtotal;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);

	value = mode->vtotal - mode->vdisplay;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VBLANK, value & 0xFF);

	value = mode->vtotal - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDELAY, value & 0xFF);

	value = mode->vsync_end - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDURATION, value & 0xFF);

	hdmi_writeb(hdmi, HDMI_PHY_PRE_DIV_RATIO, 0x1e);
	hdmi_writeb(hdmi, HDMI_PHY_FEEDBACK_DIV_RATIO_LOW, 0x2c);
	hdmi_writeb(hdmi, HDMI_PHY_FEEDBACK_DIV_RATIO_HIGH, 0x01);

	return 0;
}

static int inno_hdmi_setup(struct inno_hdmi *hdmi,
			   struct drm_atomic_state *state)
{
	struct drm_connector *connector = &hdmi->connector;
	struct drm_display_info *display = &connector->display_info;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc_state *new_crtc_state;

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!new_conn_state))
		return -EINVAL;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	if (WARN_ON(!new_crtc_state))
		return -EINVAL;

	/* Mute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_VIDEO_BLACK,
		  v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));

	/* Set HDMI Mode */
	hdmi_writeb(hdmi, HDMI_HDCP_CTRL,
		    v_HDMI_DVI(display->is_hdmi));

	inno_hdmi_config_video_timing(hdmi, &new_crtc_state->adjusted_mode);

	inno_hdmi_config_video_csc(hdmi);

	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);

	/*
	 * When IP controller have configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to mode pixel
	 * clock rate, and reconfigure the DDC clock.
	 */
	inno_hdmi_i2c_init(hdmi, new_conn_state->hdmi.tmds_char_rate);

	/* Unmute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_VIDEO_BLACK,
		  v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));

	inno_hdmi_power_up(hdmi, new_conn_state->hdmi.tmds_char_rate);

	return 0;
}

static enum drm_mode_status inno_hdmi_display_mode_valid(struct inno_hdmi *hdmi,
							 struct drm_display_mode *mode)
{
	unsigned long mpixelclk, max_tolerance;
	long rounded_refclk;

	/* No support for double-clock modes */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_BAD;

	mpixelclk = mode->clock * 1000;

	if (mpixelclk < INNO_HDMI_MIN_TMDS_CLOCK)
		return MODE_CLOCK_LOW;

	if (inno_hdmi_find_phy_config(hdmi, mpixelclk) < 0)
		return MODE_CLOCK_HIGH;

	if (hdmi->refclk) {
		rounded_refclk = clk_round_rate(hdmi->refclk, mpixelclk);
		if (rounded_refclk < 0)
			return MODE_BAD;

		/* Vesa DMT standard mentions +/- 0.5% max tolerance */
		max_tolerance = mpixelclk / 200;
		if (abs_diff((unsigned long)rounded_refclk, mpixelclk) > max_tolerance)
			return MODE_NOCLOCK;
	}

	return MODE_OK;
}

static void inno_hdmi_encoder_enable(struct drm_encoder *encoder,
				     struct drm_atomic_state *state)
{
	struct inno_hdmi *hdmi = encoder_to_inno_hdmi(encoder);

	inno_hdmi_setup(hdmi, state);
}

static void inno_hdmi_encoder_disable(struct drm_encoder *encoder,
				      struct drm_atomic_state *state)
{
	struct inno_hdmi *hdmi = encoder_to_inno_hdmi(encoder);

	inno_hdmi_standby(hdmi);
}

static int
inno_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	u8 vic = drm_match_cea_mode(mode);
	struct inno_hdmi_connector_state *inno_conn_state =
					to_inno_hdmi_conn_state(conn_state);

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	if (vic == 6 || vic == 7 ||
	    vic == 21 || vic == 22 ||
	    vic == 2 || vic == 3 ||
	    vic == 17 || vic == 18)
		inno_conn_state->colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		inno_conn_state->colorimetry = HDMI_COLORIMETRY_ITU_709;

	return 0;
}

static struct drm_encoder_helper_funcs inno_hdmi_encoder_helper_funcs = {
	.atomic_check	= inno_hdmi_encoder_atomic_check,
	.atomic_enable	= inno_hdmi_encoder_enable,
	.atomic_disable	= inno_hdmi_encoder_disable,
};

static enum drm_connector_status
inno_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct inno_hdmi *hdmi = connector_to_inno_hdmi(connector);

	return (hdmi_readb(hdmi, HDMI_STATUS) & m_HOTPLUG) ?
		connector_status_connected : connector_status_disconnected;
}

static int inno_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct inno_hdmi *hdmi = connector_to_inno_hdmi(connector);
	const struct drm_edid *drm_edid;
	int ret = 0;

	if (!hdmi->ddc)
		return 0;

	drm_edid = drm_edid_read_ddc(connector, hdmi->ddc);
	drm_edid_connector_update(connector, drm_edid);
	ret = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return ret;
}

static enum drm_mode_status
inno_hdmi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct inno_hdmi *hdmi = connector_to_inno_hdmi(connector);

	return  inno_hdmi_display_mode_valid(hdmi, mode);
}

static void
inno_hdmi_connector_destroy_state(struct drm_connector *connector,
				  struct drm_connector_state *state)
{
	struct inno_hdmi_connector_state *inno_conn_state =
						to_inno_hdmi_conn_state(state);

	__drm_atomic_helper_connector_destroy_state(&inno_conn_state->base);
	kfree(inno_conn_state);
}

static void inno_hdmi_connector_reset(struct drm_connector *connector)
{
	struct inno_hdmi_connector_state *inno_conn_state;

	if (connector->state) {
		inno_hdmi_connector_destroy_state(connector, connector->state);
		connector->state = NULL;
	}

	inno_conn_state = kzalloc(sizeof(*inno_conn_state), GFP_KERNEL);
	if (!inno_conn_state)
		return;

	__drm_atomic_helper_connector_reset(connector, &inno_conn_state->base);
	__drm_atomic_helper_connector_hdmi_reset(connector, connector->state);

	inno_conn_state->colorimetry = HDMI_COLORIMETRY_ITU_709;
}

static struct drm_connector_state *
inno_hdmi_connector_duplicate_state(struct drm_connector *connector)
{
	struct inno_hdmi_connector_state *inno_conn_state;

	if (WARN_ON(!connector->state))
		return NULL;

	inno_conn_state = kmemdup(to_inno_hdmi_conn_state(connector->state),
				  sizeof(*inno_conn_state), GFP_KERNEL);

	if (!inno_conn_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector,
						      &inno_conn_state->base);

	return &inno_conn_state->base;
}

static const struct drm_connector_funcs inno_hdmi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = inno_hdmi_connector_detect,
	.reset = inno_hdmi_connector_reset,
	.atomic_duplicate_state = inno_hdmi_connector_duplicate_state,
	.atomic_destroy_state = inno_hdmi_connector_destroy_state,
};

static struct drm_connector_helper_funcs inno_hdmi_connector_helper_funcs = {
	.atomic_check = drm_atomic_helper_connector_hdmi_check,
	.get_modes = inno_hdmi_connector_get_modes,
	.mode_valid = inno_hdmi_connector_mode_valid,
};

static int inno_hdmi_register(struct drm_device *drm, struct inno_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder.encoder;
	struct device *dev = hdmi->dev;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &inno_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&hdmi->connector,
				 &inno_hdmi_connector_helper_funcs);
	drmm_connector_hdmi_init(drm, &hdmi->connector,
				 "Rockchip", "Inno HDMI",
				 &inno_hdmi_connector_funcs,
				 &inno_hdmi_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA,
				 hdmi->ddc,
				 BIT(HDMI_COLORSPACE_RGB),
				 8);

	drm_connector_attach_encoder(&hdmi->connector, encoder);

	return 0;
}

static irqreturn_t inno_hdmi_i2c_irq(struct inno_hdmi *hdmi)
{
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	u8 stat;

	stat = hdmi_readb(hdmi, HDMI_INTERRUPT_STATUS1);
	if (!(stat & m_INT_EDID_READY))
		return IRQ_NONE;

	/* Clear HDMI EDID interrupt flag */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	complete(&i2c->cmp);

	return IRQ_HANDLED;
}

static irqreturn_t inno_hdmi_hardirq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u8 interrupt;

	if (hdmi->i2c)
		ret = inno_hdmi_i2c_irq(hdmi);

	interrupt = hdmi_readb(hdmi, HDMI_STATUS);
	if (interrupt & m_INT_HOTPLUG) {
		hdmi_modb(hdmi, HDMI_STATUS, m_INT_HOTPLUG, m_INT_HOTPLUG);
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t inno_hdmi_irq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static int inno_hdmi_i2c_read(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	ret = wait_for_completion_timeout(&hdmi->i2c->cmp, HZ / 10);
	if (!ret)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_EDID_FIFO_ADDR);

	return 0;
}

static int inno_hdmi_i2c_write(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	reinit_completion(&hdmi->i2c->cmp);

	if (msgs->addr == DDC_SEGMENT_ADDR)
		hdmi->i2c->segment_addr = msgs->buf[0];
	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];

	/* Set edid fifo first addr */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_OFFSET, 0x00);

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int inno_hdmi_i2c_xfer(struct i2c_adapter *adap,
			      struct i2c_msg *msgs, int num)
{
	struct inno_hdmi *hdmi = i2c_get_adapdata(adap);
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	/* Clear the EDID interrupt flag and unmute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_EDID_READY);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	for (i = 0; i < num; i++) {
		DRM_DEV_DEBUG(hdmi->dev,
			      "xfer: num: %d/%d, len: %d, flags: %#x\n",
			      i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = inno_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = inno_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 inno_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm inno_hdmi_algorithm = {
	.master_xfer	= inno_hdmi_i2c_xfer,
	.functionality	= inno_hdmi_i2c_func,
};

static struct i2c_adapter *inno_hdmi_i2c_adapter(struct inno_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct inno_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &inno_hdmi_algorithm;
	strscpy(adap->name, "Inno HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	DRM_DEV_INFO(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static int inno_hdmi_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct inno_hdmi *hdmi;
	const struct inno_hdmi_variant *variant;
	int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;

	variant = of_device_get_match_data(hdmi->dev);
	if (!variant)
		return -EINVAL;

	hdmi->variant = variant;

	hdmi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	hdmi->pclk = devm_clk_get(hdmi->dev, "pclk");
	if (IS_ERR(hdmi->pclk)) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to get HDMI pclk clk\n");
		return PTR_ERR(hdmi->pclk);
	}

	ret = clk_prepare_enable(hdmi->pclk);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev,
			      "Cannot enable HDMI pclk clock: %d\n", ret);
		return ret;
	}

	hdmi->refclk = devm_clk_get_optional(hdmi->dev, "ref");
	if (IS_ERR(hdmi->refclk)) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to get HDMI reference clock\n");
		ret = PTR_ERR(hdmi->refclk);
		goto err_disable_pclk;
	}

	ret = clk_prepare_enable(hdmi->refclk);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev,
			      "Cannot enable HDMI reference clock: %d\n", ret);
		goto err_disable_pclk;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_disable_clk;
	}

	inno_hdmi_reset(hdmi);

	hdmi->ddc = inno_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		goto err_disable_clk;
	}

	/*
	 * When the controller isn't configured to an accurate
	 * video timing and there is no reference clock available,
	 * then the TMDS clock source would be switched to PCLK_HDMI,
	 * so we need to init the TMDS rate to PCLK rate, and
	 * reconfigure the DDC clock.
	 */
	if (hdmi->refclk)
		inno_hdmi_i2c_init(hdmi, clk_get_rate(hdmi->refclk));
	else
		inno_hdmi_i2c_init(hdmi, clk_get_rate(hdmi->pclk));

	ret = inno_hdmi_register(drm, hdmi);
	if (ret)
		goto err_put_adapter;

	dev_set_drvdata(dev, hdmi);

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));

	ret = devm_request_threaded_irq(dev, irq, inno_hdmi_hardirq,
					inno_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret < 0)
		goto err_cleanup_hdmi;

	return 0;
err_cleanup_hdmi:
	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.encoder.funcs->destroy(&hdmi->encoder.encoder);
err_put_adapter:
	i2c_put_adapter(hdmi->ddc);
err_disable_clk:
	clk_disable_unprepare(hdmi->refclk);
err_disable_pclk:
	clk_disable_unprepare(hdmi->pclk);
	return ret;
}

static void inno_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.encoder.funcs->destroy(&hdmi->encoder.encoder);

	i2c_put_adapter(hdmi->ddc);
	clk_disable_unprepare(hdmi->refclk);
	clk_disable_unprepare(hdmi->pclk);
}

static const struct component_ops inno_hdmi_ops = {
	.bind	= inno_hdmi_bind,
	.unbind	= inno_hdmi_unbind,
};

static int inno_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &inno_hdmi_ops);
}

static void inno_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &inno_hdmi_ops);
}

static const struct inno_hdmi_variant rk3036_inno_hdmi_variant = {
	.phy_configs = rk3036_hdmi_phy_configs,
	.default_phy_config = &rk3036_hdmi_phy_configs[1],
};

static const struct inno_hdmi_variant rk3128_inno_hdmi_variant = {
	.phy_configs = rk3128_hdmi_phy_configs,
	.default_phy_config = &rk3128_hdmi_phy_configs[1],
};

static const struct of_device_id inno_hdmi_dt_ids[] = {
	{ .compatible = "rockchip,rk3036-inno-hdmi",
	  .data = &rk3036_inno_hdmi_variant,
	},
	{ .compatible = "rockchip,rk3128-inno-hdmi",
	  .data = &rk3128_inno_hdmi_variant,
	},
	{},
};
MODULE_DEVICE_TABLE(of, inno_hdmi_dt_ids);

struct platform_driver inno_hdmi_driver = {
	.probe  = inno_hdmi_probe,
	.remove_new = inno_hdmi_remove,
	.driver = {
		.name = "innohdmi-rockchip",
		.of_match_table = inno_hdmi_dt_ids,
	},
};
