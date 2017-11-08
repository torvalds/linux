/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *    Zheng Yang <zhengyang@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include <sound/hdmi-codec.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#include "rk3066_hdmi.h"

#define DEFAULT_PLLA_RATE	30000000

struct audio_info {
	int sample_rate;
	int channels;
	int sample_width;
};

struct hdmi_data_info {
	int vic;
	bool sink_is_hdmi;
	bool sink_has_audio;
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int colorimetry;
};

struct rk3066_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;
	u8 stat;

	struct mutex lock;	/*for i2c operation*/
	struct completion cmp;
};

struct rk3066_hdmi {
	struct device *dev;
	struct drm_device *drm_dev;
	struct regmap *regmap;
	int irq;
	struct clk *hclk;
	void __iomem *regs;

	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct rk3066_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;

	unsigned int tmdsclk;
	unsigned int powermode;

	struct platform_device *audio_pdev;
	bool audio_enable;

	struct hdmi_data_info	hdmi_data;
	struct audio_info	audio;
	struct drm_display_mode previous_mode;
};

#define to_rk3066_hdmi(x)	container_of(x, struct rk3066_hdmi, x)

static int
rk3066_hdmi_config_audio(struct rk3066_hdmi *hdmi, struct audio_info *audio);

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

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_modb(hdmi, HDMI_INTR_MASK1, HDMI_INTR_EDID_MASK, 0);
	hdmi_writeb(hdmi, HDMI_INTR_STATUS1, HDMI_INTR_EDID_MASK);
}

static inline u8 rk3066_hdmi_get_power_mode(struct rk3066_hdmi *hdmi)
{
	return hdmi_readb(hdmi, HDMI_SYS_CTRL) & HDMI_SYS_POWER_MODE_MASK;
}

static void rk3066_hdmi_set_power_mode(struct rk3066_hdmi *hdmi, int mode)
{
	u8 previous_mode, next_mode;

	previous_mode = rk3066_hdmi_get_power_mode(hdmi);

	if (previous_mode == mode)
		return;

	do {
		if (previous_mode > mode)
			next_mode = previous_mode / 2;
		else
			next_mode = previous_mode * 2;
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
		previous_mode = next_mode;
	} while (next_mode != mode);

	/*
	 * When IP controller haven't configured to an accurate video
	 * timing, DDC_CLK is equal to PLLA freq which is 30MHz,so we
	 * need to init the TMDS rate to PCLK rate, and reconfigure
	 * the DDC clock.
	 */
	if (mode < HDMI_SYS_POWER_MODE_D)
		hdmi->tmdsclk = DEFAULT_PLLA_RATE;
}

static int
rk3066_hdmi_upload_frame(struct rk3066_hdmi *hdmi, int setup_rc,
			 union hdmi_infoframe *frame, u32 frame_index,
			 u32 mask, u32 disable, u32 enable)
{
	if (mask)
		hdmi_modb(hdmi, HDMI_CP_AUTO_SEND_CTRL, mask, disable);

	hdmi_writeb(hdmi, HDMI_CP_BUF_INDEX, frame_index);

	if (setup_rc >= 0) {
		u8 packed_frame[HDMI_MAXIMUM_INFO_FRAME_SIZE];
		ssize_t rc, i;

		rc = hdmi_infoframe_pack(frame, packed_frame,
					 sizeof(packed_frame));
		if (rc < 0)
			return rc;

		for (i = 0; i < rc; i++)
			hdmi_writeb(hdmi, HDMI_CP_BUF_ACC_HB0 + i * 4,
				    packed_frame[i]);

		if (mask)
			hdmi_modb(hdmi, HDMI_CP_AUTO_SEND_CTRL, mask, enable);
	}

	return setup_rc;
}

static int rk3066_hdmi_config_avi(struct rk3066_hdmi *hdmi,
				  struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi, mode, false);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV422)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV422;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	frame.avi.colorimetry = hdmi->hdmi_data.colorimetry;
	frame.avi.scan_mode = HDMI_SCAN_MODE_NONE;

	return rk3066_hdmi_upload_frame(hdmi, rc, &frame,
					HDMI_INFOFRAME_AVI, 0, 0, 0);
}

static int rk3066_hdmi_config_aai(struct rk3066_hdmi *hdmi,
				  struct audio_info *audio)
{
	struct hdmi_audio_infoframe *faudio;
	union hdmi_infoframe frame;
	int rc;

	rc = hdmi_audio_infoframe_init(&frame.audio);
	faudio = (struct hdmi_audio_infoframe *)&frame;

	return rk3066_hdmi_upload_frame(hdmi, rc, &frame,
					HDMI_INFOFRAME_AAI, 0, 0, 0);
}

static int rk3066_hdmi_config_video_timing(struct rk3066_hdmi *hdmi,
					   struct drm_display_mode *mode)
{
	int value, vsync_offset;

	/* Set detail external video timing polarity and interlace mode */
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
	value |= vsync_offset << 4;
	hdmi_writeb(hdmi, HDMI_EXT_VIDEO_PARA, value);

	/* Set detail external video timing */
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
	/* tmds frequency same as input dclk */
	hdmi_writeb(hdmi, HDMI_DEEP_COLOR_MODE, 0x22);
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
			     struct drm_display_mode *mode)
{
	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);

	hdmi->hdmi_data.enc_in_format = HDMI_COLORSPACE_RGB;
	hdmi->hdmi_data.enc_out_format = HDMI_COLORSPACE_RGB;

	if (hdmi->hdmi_data.vic == 6 || hdmi->hdmi_data.vic == 7 ||
	    hdmi->hdmi_data.vic == 21 || hdmi->hdmi_data.vic == 22 ||
	    hdmi->hdmi_data.vic == 2 || hdmi->hdmi_data.vic == 3 ||
	    hdmi->hdmi_data.vic == 17 || hdmi->hdmi_data.vic == 18)
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_709;

	hdmi->tmdsclk = mode->clock * 1000;

	/* Mute video and audio output */
	hdmi_modb(hdmi, HDMI_VIDEO_CTRL2, HDMI_VIDEO_AUDIO_DISABLE_MASK,
		  HDMI_AUDIO_DISABLE | HDMI_VIDEO_DISABLE);

	/* Set power state to mode b */
	if (rk3066_hdmi_get_power_mode(hdmi) != HDMI_SYS_POWER_MODE_B)
		rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_B);

	/* Input video mode is RGB24bit, Data enable signal from external */
	hdmi_modb(hdmi, HDMI_AV_CTRL1,
		  HDMI_VIDEO_DE_MASK, HDMI_VIDEO_EXTERNAL_DE);
	hdmi_writeb(hdmi, HDMI_VIDEO_CTRL1,
		    HDMI_VIDEO_OUTPUT_RGB444 |
		    HDMI_VIDEO_INPUT_DATA_DEPTH_8BIT |
		    HDMI_VIDEO_INPUT_COLOR_RGB);
	hdmi_writeb(hdmi, HDMI_DEEP_COLOR_MODE, 0x20);

	rk3066_hdmi_config_video_timing(hdmi, mode);

	if (hdmi->hdmi_data.sink_is_hdmi) {
		hdmi_modb(hdmi, HDMI_HDCP_CTRL, HDMI_VIDEO_MODE_MASK,
			  HDMI_VIDEO_MODE_HDMI);
		rk3066_hdmi_config_avi(hdmi, mode);
		rk3066_hdmi_config_audio(hdmi, &hdmi->audio);
	} else {
		hdmi_modb(hdmi, HDMI_HDCP_CTRL, HDMI_VIDEO_MODE_MASK, 0);
	}

	rk3066_hdmi_config_phy(hdmi);

	rk3066_hdmi_set_power_mode(hdmi, HDMI_SYS_POWER_MODE_E);

	/*
	 * When IP controller have configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to mode pixel
	 * clock rate, and reconfigure the DDC clock.
	 */
	rk3066_hdmi_i2c_init(hdmi);

	/* Unmute video and audio output */
	hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
		  HDMI_VIDEO_AUDIO_DISABLE_MASK, HDMI_AUDIO_DISABLE);
	if (hdmi->audio_enable) {
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2, HDMI_AUDIO_DISABLE, 0);
		/* Reset Audio cature logic */
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_CP_LOGIC_RESET_MASK,
			  HDMI_AUDIO_CP_LOGIC_RESET);
		usleep_range(900, 1000);
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_CP_LOGIC_RESET_MASK, 0);
	}
	return 0;
}

static void
rk3066_hdmi_encoder_mode_set(struct drm_encoder *encoder,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adj_mode)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(encoder);

	/* Store the display mode for plugin/DPMS poweron events */
	memcpy(&hdmi->previous_mode, adj_mode, sizeof(hdmi->previous_mode));
}

static void rk3066_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(encoder);

	rk3066_hdmi_setup(hdmi, &hdmi->previous_mode);
}

static void rk3066_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(encoder);

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

static bool
rk3066_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adj_mode)
{
	return true;
}

static int
rk3066_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;
	s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	return 0;
}

static const
struct drm_encoder_helper_funcs rk3066_hdmi_encoder_helper_funcs = {
	.enable     = rk3066_hdmi_encoder_enable,
	.disable    = rk3066_hdmi_encoder_disable,
	.mode_fixup = rk3066_hdmi_encoder_mode_fixup,
	.mode_set   = rk3066_hdmi_encoder_mode_set,
	.atomic_check = rk3066_hdmi_encoder_atomic_check,
};

static const struct drm_encoder_funcs rk3066_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_connector_status
rk3066_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(connector);

	return (hdmi_readb(hdmi, HDMI_HPG_MENS_STA) & HDMI_HPG_IN_STATUS_HIGH) ?
		connector_status_connected : connector_status_disconnected;
}

static int rk3066_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(connector);
	struct edid *edid;
	const u8 def_modes[6] = {4, 16, 31, 19, 17, 2};
	struct drm_display_mode *mode;
	int i, ret = 0;

	if (!hdmi->ddc)
		return 0;

	edid = drm_get_edid(connector, hdmi->ddc);
	if (edid) {
		hdmi->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		hdmi->hdmi_data.sink_is_hdmi = true;
		hdmi->hdmi_data.sink_has_audio = true;
		for (i = 0; i < sizeof(def_modes); i++) {
			mode = drm_display_mode_from_vic_index(connector,
							       def_modes,
							       31, i);
			if (mode) {
				if (!i)
					mode->type = DRM_MODE_TYPE_PREFERRED;
				drm_mode_probed_add(connector, mode);
				ret++;
			}
		}
		dev_info(hdmi->dev, "failed to get edid\n");
	}

	return ret;
}

static enum drm_mode_status
rk3066_hdmi_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	u32 vic = drm_match_cea_mode(mode);

	if (vic > 1)
		return MODE_OK;
	else
		return MODE_BAD;
}

static struct drm_encoder *
rk3066_hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct rk3066_hdmi *hdmi = to_rk3066_hdmi(connector);

	return &hdmi->encoder;
}

static int
rk3066_hdmi_probe_single_connector_modes(struct drm_connector *connector,
					 uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static void rk3066_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk3066_hdmi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = rk3066_hdmi_probe_single_connector_modes,
	.detect = rk3066_hdmi_connector_detect,
	.destroy = rk3066_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const
struct drm_connector_helper_funcs rk3066_hdmi_connector_helper_funcs = {
	.get_modes = rk3066_hdmi_connector_get_modes,
	.mode_valid = rk3066_hdmi_connector_mode_valid,
	.best_encoder = rk3066_hdmi_connector_best_encoder,
};

static int
rk3066_hdmi_config_audio(struct rk3066_hdmi *hdmi, struct audio_info *audio)
{
	u32 rate, channel, word_length, N, CTS;
	u64 tmp;

	if (audio->channels < 3)
		channel = HDMI_AUDIO_I2S_CHANNEL_1_2;
	else if (audio->channels < 5)
		channel = HDMI_AUDIO_I2S_CHANNEL_3_4;
	else if (audio->channels < 7)
		channel = HDMI_AUDIO_I2S_CHANNEL_5_6;
	else
		channel = HDMI_AUDIO_I2S_CHANNEL_7_8;

	switch (audio->sample_rate) {
	case 32000:
		rate = HDMI_AUDIO_SAMPLE_FRE_32000;
		N = N_32K;
		break;
	case 44100:
		rate = HDMI_AUDIO_SAMPLE_FRE_44100;
		N = N_441K;
		break;
	case 48000:
		rate = HDMI_AUDIO_SAMPLE_FRE_48000;
		N = N_48K;
		break;
	case 88200:
		rate = HDMI_AUDIO_SAMPLE_FRE_88200;
		N = N_882K;
		break;
	case 96000:
		rate = HDMI_AUDIO_SAMPLE_FRE_96000;
		N = N_96K;
		break;
	case 176400:
		rate = HDMI_AUDIO_SAMPLE_FRE_176400;
		N = N_1764K;
		break;
	case 192000:
		rate = HDMI_AUDIO_SAMPLE_FRE_192000;
		N = N_192K;
		break;
	default:
		dev_err(hdmi->dev, "[%s] not support such sample rate %d\n",
			__func__, audio->sample_rate);
		return -ENOENT;
	}

	switch (audio->sample_width) {
	case 16:
		word_length = 0x02;
		break;
	case 20:
		word_length = 0x0a;
		break;
	case 24:
		word_length = 0x0b;
		break;
	default:
		dev_err(hdmi->dev, "[%s] not support such word length %d\n",
			__func__, audio->sample_width);
		return -ENOENT;
	}

	tmp = (u64)hdmi->tmdsclk * N;
	do_div(tmp, 128 * audio->sample_rate);
	CTS = tmp;

	/* set_audio source I2S */
	hdmi_writeb(hdmi, HDMI_AUDIO_CTRL1, 0x00);
	hdmi_writeb(hdmi, HDMI_AUDIO_CTRL2, 0x40);
	hdmi_writeb(hdmi, HDMI_I2S_AUDIO_CTRL,
		    HDMI_AUDIO_I2S_FORMAT_STANDARD | channel);
	hdmi_writeb(hdmi, HDMI_I2S_SWAP, 0x00);
	hdmi_modb(hdmi, HDMI_AV_CTRL1, HDMI_AUDIO_SAMPLE_FRE_MASK, rate);
	hdmi_writeb(hdmi, HDMI_AUDIO_SRC_NUM_AND_LENGTH, word_length);

	/* Set N value */
	hdmi_modb(hdmi, HDMI_LR_SWAP_N3,
		  HDMI_AUDIO_N_19_16_MASK, (N >> 16) & 0x0F);
	hdmi_writeb(hdmi, HDMI_N2, (N >> 8) & 0xFF);
	hdmi_writeb(hdmi, HDMI_N1, N & 0xFF);

	/* Set CTS value */
	hdmi_writeb(hdmi, HDMI_CTS_EXT1, CTS & 0xff);
	hdmi_writeb(hdmi, HDMI_CTS_EXT2, (CTS >> 8) & 0xff);
	hdmi_writeb(hdmi, HDMI_CTS_EXT3, (CTS >> 16) & 0xff);

	if (audio->channels > 2)
		hdmi_modb(hdmi, HDMI_LR_SWAP_N3,
			  HDMI_AUDIO_LR_SWAP_MASK,
			  HDMI_AUDIO_LR_SWAP_SUBPACKET1);
	rate = (~(rate >> 4)) & 0x0f;
	hdmi_writeb(hdmi, HDMI_AUDIO_STA_BIT_CTRL1, rate);
	hdmi_writeb(hdmi, HDMI_AUDIO_STA_BIT_CTRL2, 0);

	return rk3066_hdmi_config_aai(hdmi, audio);
}

static int rk3066_hdmi_audio_hw_params(struct device *dev, void *d,
				       struct hdmi_codec_daifmt *daifmt,
				       struct hdmi_codec_params *params)
{
	struct rk3066_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	if (!hdmi->encoder.crtc)
		return -ENODEV;

	switch (daifmt->fmt) {
	case HDMI_I2S:
		break;
	default:
		dev_err(dev, "%s: Invalid format %d\n", __func__, daifmt->fmt);
		return -EINVAL;
	}

	hdmi->audio.sample_width = params->sample_width;
	hdmi->audio.sample_rate = params->sample_rate;
	hdmi->audio.channels = params->channels;

	return rk3066_hdmi_config_audio(hdmi, &hdmi->audio);
}

static void rk3066_hdmi_audio_shutdown(struct device *dev, void *d)
{
	/* do nothing */
}

static int
rk3066_hdmi_audio_digital_mute(struct device *dev, void *d, bool mute)
{
	struct rk3066_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	hdmi->audio_enable = !mute;

	if (mute)
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_DISABLE, HDMI_AUDIO_DISABLE);
	else
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2, HDMI_AUDIO_DISABLE, 0);

	/*
	 * Under power mode e, we need to reset audio capture logic to
	 * make audio setting update.
	 */
	if (rk3066_hdmi_get_power_mode(hdmi) == HDMI_SYS_POWER_MODE_E) {
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_CP_LOGIC_RESET_MASK,
			  HDMI_AUDIO_CP_LOGIC_RESET);
		usleep_range(900, 1000);
		hdmi_modb(hdmi, HDMI_VIDEO_CTRL2,
			  HDMI_AUDIO_CP_LOGIC_RESET_MASK, 0);
	}

	return 0;
}

static int rk3066_hdmi_audio_get_eld(struct device *dev, void *d,
				     uint8_t *buf, size_t len)
{
	struct rk3066_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_mode_config *config = &hdmi->encoder.dev->mode_config;
	struct drm_connector *connector;
	int ret = -ENODEV;

	mutex_lock(&config->mutex);
	list_for_each_entry(connector, &config->connector_list, head) {
		if (&hdmi->encoder == connector->encoder) {
			memcpy(buf, connector->eld,
			       min(sizeof(connector->eld), len));
			ret = 0;
		}
	}
	mutex_unlock(&config->mutex);

	return ret;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = rk3066_hdmi_audio_hw_params,
	.audio_shutdown = rk3066_hdmi_audio_shutdown,
	.digital_mute = rk3066_hdmi_audio_digital_mute,
	.get_eld = rk3066_hdmi_audio_get_eld,
};

static int rk3066_hdmi_audio_codec_init(struct rk3066_hdmi *hdmi,
					struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
	};
	hdmi->audio.channels = 2;
	hdmi->audio.sample_rate = 48000;
	hdmi->audio_enable = false;
	hdmi->audio_pdev =
		platform_device_register_data(dev,
					      HDMI_CODEC_DRV_NAME,
					      PLATFORM_DEVID_NONE,
					      &codec_data,
					      sizeof(codec_data));

	return PTR_ERR_OR_ZERO(hdmi->audio_pdev);
}

static int
rk3066_hdmi_register(struct drm_device *drm, struct rk3066_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct device *dev = hdmi->dev;

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
	drm_encoder_init(drm, encoder, &rk3066_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	hdmi->connector.port = dev->of_node;

	drm_connector_helper_add(&hdmi->connector,
				 &rk3066_hdmi_connector_helper_funcs);
	drm_connector_init(drm, &hdmi->connector,
			   &rk3066_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);

	drm_mode_connector_attach_encoder(&hdmi->connector, encoder);

	rk3066_hdmi_audio_codec_init(hdmi, dev);

	return 0;
}

static irqreturn_t rk3066_hdmi_i2c_irq(struct rk3066_hdmi *hdmi, u8 stat)
{
	struct rk3066_hdmi_i2c *i2c = hdmi->i2c;

	if (!(stat & HDMI_INTR_EDID_MASK))
		return IRQ_NONE;

	i2c->stat = stat;

	complete(&i2c->cmp);

	return IRQ_HANDLED;
}

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

	if (hdmi->i2c)
		ret = rk3066_hdmi_i2c_irq(hdmi, interrupt);

	if (interrupt & (HDMI_INTR_HOTPLUG | HDMI_INTR_MSENS))
		ret = IRQ_WAKE_THREAD;

	return ret;
}

static irqreturn_t rk3066_hdmi_irq(int irq, void *dev_id)
{
	struct rk3066_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static int rk3066_hdmi_i2c_read(struct rk3066_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	ret = wait_for_completion_timeout(&hdmi->i2c->cmp, HZ / 10);
	if (!ret || hdmi->i2c->stat & HDMI_INTR_EDID_ERR)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_DDC_READ_FIFO_ADDR);

	return 0;
}

static int rk3066_hdmi_i2c_write(struct rk3066_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if (msgs->len != 1 ||
	    (msgs->addr != DDC_ADDR && msgs->addr != DDC_SEGMENT_ADDR))
		return -EINVAL;

	reinit_completion(&hdmi->i2c->cmp);

	if (msgs->addr == DDC_SEGMENT_ADDR)
		hdmi->i2c->segment_addr = msgs->buf[0];
	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int rk3066_hdmi_i2c_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	struct rk3066_hdmi *hdmi = i2c_get_adapdata(adap);
	struct rk3066_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	rk3066_hdmi_i2c_init(hdmi);

	/* Unmute the interrupt */
	hdmi_modb(hdmi, HDMI_INTR_MASK1,
		  HDMI_INTR_EDID_MASK, HDMI_INTR_EDID_MASK);
	i2c->stat = 0;

	for (i = 0; i < num; i++) {
		dev_dbg(hdmi->dev, "xfer: num: %d/%d, len: %d, flags: %#x\n",
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

	/* Mute HDMI EDID interrupt */
	hdmi_modb(hdmi, HDMI_INTR_MASK1, HDMI_INTR_EDID_MASK, 0);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 rk3066_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rk3066_hdmi_algorithm = {
	.master_xfer	= rk3066_hdmi_i2c_xfer,
	.functionality	= rk3066_hdmi_i2c_func,
};

static struct i2c_adapter *rk3066_hdmi_i2c_adapter(struct rk3066_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct rk3066_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &rk3066_hdmi_algorithm;
	strlcpy(adap->name, "RK3066 HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	dev_info(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static int rk3066_hdmi_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct rk3066_hdmi *hdmi;
	struct resource *iores;
	int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm_dev = drm;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -ENXIO;

	hdmi->regs = devm_ioremap_resource(dev, iores);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	hdmi->hclk = devm_clk_get(hdmi->dev, "hclk");
	if (IS_ERR(hdmi->hclk)) {
		dev_err(hdmi->dev, "Unable to get HDMI hclk clk\n");
		return PTR_ERR(hdmi->hclk);
	}

	ret = clk_prepare_enable(hdmi->hclk);
	if (ret) {
		dev_err(hdmi->dev, "Cannot enable HDMI hclk clock: %d\n", ret);
		return ret;
	}

	hdmi->regmap =
		syscon_regmap_lookup_by_phandle(hdmi->dev->of_node,
						"rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		ret = PTR_ERR(hdmi->regmap);
		goto err_disable_hclk;
	}

	/* internal hclk = hdmi_hclk / 25 */
	hdmi_writeb(hdmi, HDMI_INTERNAL_CLK_DIVIDER, 25);

	hdmi->ddc = rk3066_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		goto err_disable_hclk;
	}

	hdmi->powermode = HDMI_SYS_POWER_MODE_A;
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
		dev_err(hdmi->dev,
			"failed to request hdmi irq: %d\n", ret);
		goto err_disable_hclk;
	}

	return 0;

err_disable_hclk:
	clk_disable_unprepare(hdmi->hclk);

	return ret;
}

static void rk3066_hdmi_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct rk3066_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);

	clk_disable_unprepare(hdmi->hclk);
	i2c_put_adapter(hdmi->ddc);
}

static const struct component_ops rk3066_hdmi_ops = {
	.bind	= rk3066_hdmi_bind,
	.unbind	= rk3066_hdmi_unbind,
};

static int rk3066_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &rk3066_hdmi_ops);
}

static int rk3066_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rk3066_hdmi_ops);

	return 0;
}

static const struct of_device_id rk3066_hdmi_dt_ids[] = {
	{ .compatible = "rockchip,rk3066-hdmi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, rk3066_hdmi_dt_ids);

static struct platform_driver rk3066_hdmi_driver = {
	.probe  = rk3066_hdmi_probe,
	.remove = rk3066_hdmi_remove,
	.driver = {
		.name = "rk3066hdmi-rockchip",
		.of_match_table = rk3066_hdmi_dt_ids,
	},
};

module_platform_driver(rk3066_hdmi_driver);

MODULE_AUTHOR("Zheng Yang <zhengyang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Specific RK3066-HDMI Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rk3066hdmi-rockchip");
