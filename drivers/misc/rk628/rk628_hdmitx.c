// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Chen Shunqing <csq@rock-chips.com>
 */

#include "rk628.h"
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>
#include <sound/hdmi-codec.h>

#include "../../gpu/drm/rockchip/rockchip_drm_drv.h"
#include "rk628_config.h"
#include "rk628_hdmitx.h"
#include "rk628_post_process.h"

#include <linux/extcon.h>
#include <linux/extcon-provider.h>


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

struct rk628_hdmi_i2c {
	struct i2c_adapter adap;
	u8 ddc_addr;
	u8 segment_addr;
	/* i2c lock */
	struct mutex lock;
};

struct rk628_hdmi_phy_config {
	unsigned long mpixelclock;
	u8 pre_emphasis;	/* pre-emphasis value */
	u8 vlev_ctr;		/* voltage level control */
};

struct rk628_hdmi {
	struct rk628 *rk628;
	struct device *dev;
	int irq;

	struct drm_bridge bridge;
	struct drm_connector connector;

	struct rk628_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;

	unsigned int tmds_rate;

	struct platform_device *audio_pdev;
	bool audio_enable;

	struct hdmi_data_info	hdmi_data;
	struct drm_display_mode previous_mode;

	struct rockchip_drm_sub_dev sub_dev;
	struct extcon_dev *extcon;
};

static const unsigned int rk628_hdmi_cable[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};

enum {
	CSC_ITU601_16_235_TO_RGB_0_255_8BIT,
	CSC_ITU601_0_255_TO_RGB_0_255_8BIT,
	CSC_ITU709_16_235_TO_RGB_0_255_8BIT,
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,
};

static const char coeff_csc[][24] = {
	/*
	 * YUV2RGB:601 SD mode(Y[16:235], UV[16:240], RGB[0:255]):
	 *   R = 1.164*Y + 1.596*V - 204
	 *   G = 1.164*Y - 0.391*U - 0.813*V + 154
	 *   B = 1.164*Y + 2.018*U - 258
	 */
	{
		0x04, 0xa7, 0x00, 0x00, 0x06, 0x62, 0x02, 0xcc,
		0x04, 0xa7, 0x11, 0x90, 0x13, 0x40, 0x00, 0x9a,
		0x04, 0xa7, 0x08, 0x12, 0x00, 0x00, 0x03, 0x02
	},
	/*
	 * YUV2RGB:601 SD mode(YUV[0:255],RGB[0:255]):
	 *   R = Y + 1.402*V - 248
	 *   G = Y - 0.344*U - 0.714*V + 135
	 *   B = Y + 1.772*U - 227
	 */
	{
		0x04, 0x00, 0x00, 0x00, 0x05, 0x9b, 0x02, 0xf8,
		0x04, 0x00, 0x11, 0x60, 0x12, 0xdb, 0x00, 0x87,
		0x04, 0x00, 0x07, 0x16, 0x00, 0x00, 0x02, 0xe3
	},
	/*
	 * YUV2RGB:709 HD mode(Y[16:235],UV[16:240],RGB[0:255]):
	 *   R = 1.164*Y + 1.793*V - 248
	 *   G = 1.164*Y - 0.213*U - 0.534*V + 77
	 *   B = 1.164*Y + 2.115*U - 289
	 */
	{
		0x04, 0xa7, 0x00, 0x00, 0x07, 0x2c, 0x02, 0xf8,
		0x04, 0xa7, 0x10, 0xda, 0x12, 0x22, 0x00, 0x4d,
		0x04, 0xa7, 0x08, 0x74, 0x00, 0x00, 0x03, 0x21
	},

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

static inline struct rk628_hdmi *bridge_to_hdmi(struct drm_bridge *b)
{
	return container_of(b, struct rk628_hdmi, bridge);
}

static inline struct rk628_hdmi *connector_to_hdmi(struct drm_connector *c)
{
	return container_of(c, struct rk628_hdmi, connector);
}

static u32 hdmi_readb(struct rk628_hdmi *hdmi, u32 reg)
{
	u32 val;

	rk628_i2c_read(hdmi->rk628, reg, &val);

	return val;
}

static void hdmi_writeb(struct rk628_hdmi *hdmi, u32 reg, u32 val)
{
	rk628_i2c_write(hdmi->rk628, reg, val);
}

static void hdmi_modb(struct rk628_hdmi *hdmi, u32 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void rk628_hdmi_i2c_init(struct rk628_hdmi *hdmi)
{
	int ddc_bus_freq;

	ddc_bus_freq = (hdmi->tmds_rate >> 2) / HDMI_SCL_RATE;

	hdmi_writeb(hdmi, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, INT_EDID_READY);
}

static void rk628_hdmi_sys_power(struct rk628_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi_modb(hdmi, HDMI_SYS_CTRL, POWER_MASK, PWR_OFF(0));
	else
		hdmi_modb(hdmi, HDMI_SYS_CTRL, POWER_MASK, PWR_OFF(1));
}

static struct rk628_hdmi_phy_config rk628_hdmi_phy_config[] = {
	/* pixelclk pre-emp vlev */
	{ 74250000,  0x3f, 0x88 },
	{ 165000000, 0x3f, 0x88 },
	{ ~0UL,	     0x00, 0x00 }
};

static void rk628_hdmi_set_pwr_mode(struct rk628_hdmi *hdmi, int mode)
{
	const struct rk628_hdmi_phy_config *phy_config =
						rk628_hdmi_phy_config;

	switch (mode) {
	case NORMAL:
		rk628_hdmi_sys_power(hdmi, false);
		for (; phy_config->mpixelclock != ~0UL; phy_config++)
			if (hdmi->tmds_rate <= phy_config->mpixelclock)
				break;
		if (!phy_config->mpixelclock)
			return;
		hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS,
			    phy_config->pre_emphasis);
		hdmi_writeb(hdmi, HDMI_PHY_DRIVER, phy_config->vlev_ctr);

		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x14);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x10);
		hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x0f);
		hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x01);

		rk628_hdmi_sys_power(hdmi, true);
		break;

	case LOWER_PWR:
		rk628_hdmi_sys_power(hdmi, false);
		hdmi_writeb(hdmi, HDMI_PHY_DRIVER, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
		break;

	default:
		dev_err(hdmi->dev, "Unknown power mode %d\n", mode);
	}
}

static void rk628_hdmi_reset(struct rk628_hdmi *hdmi)
{
	u32 val;
	u32 msk;

	hdmi_modb(hdmi, HDMI_SYS_CTRL, RST_DIGITAL_MASK, NOT_RST_DIGITAL(1));
	usleep_range(100, 110);

	hdmi_modb(hdmi, HDMI_SYS_CTRL, RST_ANALOG_MASK, NOT_RST_ANALOG(1));
	usleep_range(100, 110);

	msk = REG_CLK_INV_MASK | REG_CLK_SOURCE_MASK | POWER_MASK |
	      INT_POL_MASK;
	val = REG_CLK_INV(1) | REG_CLK_SOURCE(1) | PWR_OFF(0) | INT_POL(1);
	hdmi_modb(hdmi, HDMI_SYS_CTRL, msk, val);

	rk628_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static int rk628_hdmi_upload_frame(struct rk628_hdmi *hdmi, int setup_rc,
				   union hdmi_infoframe *frame, u32 frame_index,
				   u32 mask, u32 disable, u32 enable)
{
	if (mask)
		hdmi_modb(hdmi, HDMI_PACKET_SEND_AUTO, mask, disable);

	hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_BUF_INDEX, frame_index);

	if (setup_rc >= 0) {
		u8 packed_frame[HDMI_MAXIMUM_INFO_FRAME_SIZE];
		ssize_t rc, i;

		rc = hdmi_infoframe_pack(frame, packed_frame,
					 sizeof(packed_frame));
		if (rc < 0)
			return rc;

		for (i = 0; i < rc; i++)
			hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_ADDR + (i * 4),
				    packed_frame[i]);

		if (mask)
			hdmi_modb(hdmi, HDMI_PACKET_SEND_AUTO, mask, enable);
	}

	return setup_rc;
}

static int rk628_hdmi_config_video_vsi(struct rk628_hdmi *hdmi,
				       struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_vendor_infoframe_from_display_mode(&frame.vendor.hdmi,
							 &hdmi->connector,
							 mode);

	return rk628_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_VSI, PACKET_VSI_EN_MASK,
				       PACKET_VSI_EN(0), PACKET_VSI_EN(1));
}

static int rk628_hdmi_config_video_avi(struct rk628_hdmi *hdmi,
				       struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						      &hdmi->connector, mode);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV422)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV422;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	if (frame.avi.colorspace != HDMI_COLORSPACE_RGB)
		frame.avi.colorimetry = hdmi->hdmi_data.colorimetry;

	frame.avi.scan_mode = HDMI_SCAN_MODE_NONE;

	return rk628_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_AVI, 0, 0, 0);
}

static int rk628_hdmi_config_audio_aai(struct rk628_hdmi *hdmi,
				       struct audio_info *audio)
{
	struct hdmi_audio_infoframe *faudio;
	union hdmi_infoframe frame;
	int rc;

	rc = hdmi_audio_infoframe_init(&frame.audio);
	faudio = (struct hdmi_audio_infoframe *)&frame;

	faudio->channels = audio->channels;

	return rk628_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_AAI, 0, 0, 0);
}

static int rk628_hdmi_config_video_csc(struct rk628_hdmi *hdmi)
{
	struct hdmi_data_info *data = &hdmi->hdmi_data;
	int c0_c2_change = 0;
	int csc_enable = 0;
	int csc_mode = 0;
	int auto_csc = 0;
	int value;
	int i;
	int out_fmt;

	/* Input video mode is SDR RGB24bit, data enable signal from external */
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL1, DE_SOURCE(1) |
		    VIDEO_INPUT_SDR_RGB444);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		out_fmt = VIDEO_OUTPUT_YCBCR444;
	else
		out_fmt = VIDEO_OUTPUT_RRGB444;
	/* Input color hardcode to RGB, and output color hardcode to RGB888 */
	value = VIDEO_INPUT_8BITS | out_fmt | VIDEO_INPUT_CSP(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL2, value);

	if (data->enc_in_format == data->enc_out_format) {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) ||
		    (data->enc_in_format >= HDMI_COLORSPACE_YUV444)) {
			value = SOF_DISABLE(1) | COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL3, value);

			hdmi_modb(hdmi, HDMI_VIDEO_CONTROL,
				  VIDEO_AUTO_CSC_MASK | VIDEO_C0_C2_SWAP_MASK,
				  VIDEO_AUTO_CSC(AUTO_CSC_DISABLE) |
				  VIDEO_C0_C2_SWAP(C0_C2_CHANGE_DISABLE));
			return 0;
		}
	}

	if (data->colorimetry == HDMI_COLORIMETRY_ITU_601) {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) &&
		    (data->enc_out_format == HDMI_COLORSPACE_YUV444)) {
			csc_mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = CSC_ENABLE(1);
		} else if ((data->enc_in_format == HDMI_COLORSPACE_YUV444) &&
			   (data->enc_out_format == HDMI_COLORSPACE_RGB)) {
			csc_mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = CSC_ENABLE(0);
		}
	} else {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) &&
		    (data->enc_out_format == HDMI_COLORSPACE_YUV444)) {
			csc_mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = CSC_ENABLE(1);
		} else if ((data->enc_in_format == HDMI_COLORSPACE_YUV444) &&
			   (data->enc_out_format == HDMI_COLORSPACE_RGB)) {
			csc_mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = CSC_ENABLE(0);
		}
	}

	for (i = 0; i < 24; i++)
		hdmi_writeb(hdmi, HDMI_VIDEO_CSC_COEF + (i * 4),
			    coeff_csc[csc_mode][i]);

	value = SOF_DISABLE(1) | csc_enable | COLOR_DEPTH_NOT_INDICATED(1);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL3, value);
	hdmi_modb(hdmi, HDMI_VIDEO_CONTROL,
		  VIDEO_AUTO_CSC_MASK | VIDEO_C0_C2_SWAP_MASK,
		  VIDEO_AUTO_CSC(auto_csc) | VIDEO_C0_C2_SWAP(c0_c2_change));

	return 0;
}

static int rk628_hdmi_config_video_timing(struct rk628_hdmi *hdmi,
					  struct drm_display_mode *mode)
{
	int value;

	/* Set detail external video timing polarity and interlace mode */
	value = EXTERANL_VIDEO(1);
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		 HSYNC_POLARITY(1) : HSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		 VSYNC_POLARITY(1) : VSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		 INETLACE(1) : INETLACE(0);
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
	hdmi_writeb(hdmi, PHY_FEEDBACK_DIV_RATIO_LOW, 0x2c);
	hdmi_writeb(hdmi, PHY_FEEDBACK_DIV_RATIO_HIGH, 0x01);

	return 0;
}

static int rk628_hdmi_setup(struct rk628_hdmi *hdmi,
			    struct drm_display_mode *mode)
{
	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);

	hdmi->hdmi_data.enc_in_format = HDMI_COLORSPACE_RGB;
	hdmi->hdmi_data.enc_out_format = HDMI_COLORSPACE_RGB;

	if ((hdmi->hdmi_data.vic == 6) || (hdmi->hdmi_data.vic == 7) ||
	    (hdmi->hdmi_data.vic == 21) || (hdmi->hdmi_data.vic == 22) ||
	    (hdmi->hdmi_data.vic == 2) || (hdmi->hdmi_data.vic == 3) ||
	    (hdmi->hdmi_data.vic == 17) || (hdmi->hdmi_data.vic == 18))
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_709;

	/* Mute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, AUDIO_MUTE_MASK | VIDEO_BLACK_MASK,
		  AUDIO_MUTE(1) | VIDEO_MUTE(1));

	/* Set HDMI Mode */
	hdmi_writeb(hdmi, HDMI_HDCP_CTRL,
		    HDMI_DVI(hdmi->hdmi_data.sink_is_hdmi));

	rk628_hdmi_config_video_timing(hdmi, mode);

	rk628_hdmi_config_video_csc(hdmi);

	if (hdmi->hdmi_data.sink_is_hdmi) {
		rk628_hdmi_config_video_avi(hdmi, mode);
		rk628_hdmi_config_video_vsi(hdmi, mode);
	}

	/*
	 * When IP controller have configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to mode pixel
	 * clock rate, and reconfigure the DDC clock.
	 */
	hdmi->tmds_rate = mode->clock * 1000;
	rk628_hdmi_i2c_init(hdmi);

	/* Unmute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, VIDEO_BLACK_MASK, VIDEO_MUTE(0));
	if (hdmi->audio_enable)
		hdmi_modb(hdmi, HDMI_AV_MUTE, AUDIO_MUTE_MASK, AUDIO_MUTE(0));

	return 0;
}

static enum drm_connector_status
rk628_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct rk628_hdmi *hdmi = connector_to_hdmi(connector);
	int status;

	status = hdmi_readb(hdmi, HDMI_STATUS) & HOTPLUG_STATUS;

	if (status)
		extcon_set_state_sync(hdmi->extcon, EXTCON_DISP_HDMI, true);
	else
		extcon_set_state_sync(hdmi->extcon, EXTCON_DISP_HDMI, false);

	return status ? connector_status_connected :
			connector_status_disconnected;
}

static int rk628_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_hdmi *hdmi = connector_to_hdmi(connector);
	struct drm_display_info *info = &connector->display_info;
	struct edid *edid = NULL;
	int ret = 0;

	if (!hdmi->ddc)
		return 0;

	if ((hdmi_readb(hdmi, HDMI_STATUS) & HOTPLUG_STATUS))
		edid = drm_get_edid(connector, hdmi->ddc);

	if (edid) {
		hdmi->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);

		drm_connector_update_edid_property(connector, edid);

		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		hdmi->hdmi_data.sink_is_hdmi = true;
		hdmi->hdmi_data.sink_has_audio = true;

		ret = rockchip_drm_add_modes_noedid(connector);

		info->edid_hdmi_dc_modes = 0;
		info->hdmi.y420_dc_modes = 0;
		info->color_formats = 0;

		dev_info(hdmi->dev, "failed to get edid\n");
	}

	return ret;
}

static enum drm_mode_status
rk628_hdmi_connector_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	if ((mode->hdisplay == 1920 && mode->vdisplay == 1080) ||
	    (mode->hdisplay == 1280 && mode->vdisplay == 720))
		return MODE_OK;
	else
		return MODE_BAD;
}

static struct drm_encoder *
rk628_hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct rk628_hdmi *hdmi = connector_to_hdmi(connector);

	return hdmi->bridge.encoder;
}

static int
rk628_hdmi_probe_single_connector_modes(struct drm_connector *connector,
					u32 maxX, u32 maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static const struct drm_connector_funcs rk628_hdmi_connector_funcs = {
	.fill_modes = rk628_hdmi_probe_single_connector_modes,
	.detect = rk628_hdmi_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
rk628_hdmi_connector_helper_funcs = {
	.get_modes = rk628_hdmi_connector_get_modes,
	.mode_valid = rk628_hdmi_connector_mode_valid,
	.best_encoder = rk628_hdmi_connector_best_encoder,
};

static void rk628_hdmi_bridge_mode_set(struct drm_bridge *bridge,
				       const struct drm_display_mode *mode,
				       const struct drm_display_mode *adj_mode)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);
	struct rk628 *rk628 = hdmi->rk628;
	struct rk628_display_mode *src = rk628_display_get_src_mode(rk628);
	struct rk628_display_mode *dst = rk628_display_get_dst_mode(rk628);

	/* Store the display mode for plugin/DPMS poweron events */
	memcpy(&hdmi->previous_mode, mode, sizeof(hdmi->previous_mode));
	dst->clock = mode->clock;
	dst->hdisplay = mode->hdisplay;
	dst->hsync_start = mode->hsync_start;
	dst->hsync_end = mode->hsync_end;
	dst->htotal = mode->htotal;
	dst->vdisplay = mode->vdisplay;
	dst->vsync_start = mode->vsync_start;
	dst->vsync_end = mode->vsync_end;
	dst->vtotal = mode->vtotal;
	dst->flags = mode->flags;
	rk628_mode_copy(src, dst);
}

static bool
rk628_hdmi_bridge_mode_fixup(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     struct drm_display_mode *adj)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);
	struct rk628 *rk628 = hdmi->rk628;

	if (rk628->sync_pol == MODE_FLAG_NSYNC) {
		adj->flags &= ~(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
		adj->flags |= (DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);
	} else {
		adj->flags &= ~(DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);
		adj->flags |= (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
	}

	return true;
}

static void rk628_hdmi_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);

	rk628_post_process_init(hdmi->rk628);
	rk628_post_process_enable(hdmi->rk628);
	rk628_hdmi_setup(hdmi, &hdmi->previous_mode);
	rk628_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static void rk628_hdmi_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);

	rk628_hdmi_set_pwr_mode(hdmi, LOWER_PWR);
}

static int rk628_hdmi_bridge_attach(struct drm_bridge *bridge,
				    enum drm_bridge_attach_flags flags)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);
	struct drm_connector *connector = &hdmi->connector;
	struct drm_device *drm = bridge->dev;
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(drm, connector, &rk628_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(hdmi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &rk628_hdmi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	hdmi->sub_dev.connector = &hdmi->connector;
	hdmi->sub_dev.of_node = hdmi->dev->of_node;
	rockchip_drm_register_sub_dev(&hdmi->sub_dev);

	return 0;
}

static void rk628_hdmi_bridge_detach(struct drm_bridge *bridge)
{
	struct rk628_hdmi *hdmi = bridge_to_hdmi(bridge);

	rockchip_drm_unregister_sub_dev(&hdmi->sub_dev);
}

static const struct drm_bridge_funcs rk628_hdmi_bridge_funcs = {
	.attach = rk628_hdmi_bridge_attach,
	.detach = rk628_hdmi_bridge_detach,
	.mode_set = rk628_hdmi_bridge_mode_set,
	.mode_fixup = rk628_hdmi_bridge_mode_fixup,
	.enable = rk628_hdmi_bridge_enable,
	.disable = rk628_hdmi_bridge_disable,
};

static int
rk628_hdmi_audio_config_set(struct rk628_hdmi *hdmi, struct audio_info *audio)
{
	int rate, N, channel;

	if (audio->channels < 3)
		channel = I2S_CHANNEL_1_2;
	else if (audio->channels < 5)
		channel = I2S_CHANNEL_3_4;
	else if (audio->channels < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	switch (audio->sample_rate) {
	case 32000:
		rate = AUDIO_32K;
		N = N_32K;
		break;
	case 44100:
		rate = AUDIO_441K;
		N = N_441K;
		break;
	case 48000:
		rate = AUDIO_48K;
		N = N_48K;
		break;
	case 88200:
		rate = AUDIO_882K;
		N = N_882K;
		break;
	case 96000:
		rate = AUDIO_96K;
		N = N_96K;
		break;
	case 176400:
		rate = AUDIO_1764K;
		N = N_1764K;
		break;
	case 192000:
		rate = AUDIO_192K;
		N = N_192K;
		break;
	default:
		dev_err(hdmi->dev, "[%s] not support such sample rate %d\n",
			__func__, audio->sample_rate);
		return -ENOENT;
	}

	/* set_audio source I2S */
	hdmi_writeb(hdmi, HDMI_AUDIO_CTRL1, 0x01);
	hdmi_writeb(hdmi, AUDIO_SAMPLE_RATE, rate);
	hdmi_writeb(hdmi, AUDIO_I2S_MODE,
		    I2S_MODE(I2S_STANDARD) | I2S_CHANNEL(channel));

	hdmi_writeb(hdmi, AUDIO_I2S_MAP, 0x00);
	hdmi_writeb(hdmi, AUDIO_I2S_SWAPS_SPDIF, 0);

	/* Set N value */
	hdmi_writeb(hdmi, AUDIO_N_H, (N >> 16) & 0x0F);
	hdmi_writeb(hdmi, AUDIO_N_M, (N >> 8) & 0xFF);
	hdmi_writeb(hdmi, AUDIO_N_L, N & 0xFF);

	/*Set hdmi nlpcm mode to support hdmi bitstream*/
	hdmi_writeb(hdmi, HDMI_AUDIO_CHANNEL_STATUS, AUDIO_STATUS_NLPCM(0));

	return rk628_hdmi_config_audio_aai(hdmi, audio);
}

static int rk628_hdmi_audio_hw_params(struct device *dev, void *d,
				      struct hdmi_codec_daifmt *daifmt,
				      struct hdmi_codec_params *params)
{
	struct rk628_hdmi *hdmi = dev_get_drvdata(dev);
	struct audio_info audio = {
		.sample_width = params->sample_width,
		.sample_rate = params->sample_rate,
		.channels = params->channels,
	};

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	if (!hdmi->bridge.encoder->crtc)
		return -ENODEV;

	switch (daifmt->fmt) {
	case HDMI_I2S:
		break;
	default:
		dev_err(dev, "%s: Invalid format %d\n", __func__, daifmt->fmt);
		return -EINVAL;
	}

	return rk628_hdmi_audio_config_set(hdmi, &audio);
}

static void rk628_hdmi_audio_shutdown(struct device *dev, void *d)
{
	/* do nothing */
}

static int rk628_hdmi_audio_mute(struct device *dev, void *d, bool mute,
				 int direction)
{
	struct rk628_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	hdmi->audio_enable = !mute;

	if (mute)
		hdmi_modb(hdmi, HDMI_AV_MUTE, AUDIO_MUTE_MASK | AUDIO_PD_MASK,
			  AUDIO_MUTE(1) | AUDIO_PD(1));
	else
		hdmi_modb(hdmi, HDMI_AV_MUTE, AUDIO_MUTE_MASK | AUDIO_PD_MASK,
			  AUDIO_MUTE(0) | AUDIO_PD(0));

	return 0;
}

static int rk628_hdmi_audio_get_eld(struct device *dev, void *d,
				    u8 *buf, size_t len)
{
	struct rk628_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_mode_config *config = &hdmi->bridge.dev->mode_config;
	struct drm_connector *connector;
	int ret = -ENODEV;

	mutex_lock(&config->mutex);
	list_for_each_entry(connector, &config->connector_list, head) {
		if (hdmi->bridge.encoder == connector->encoder) {
			memcpy(buf, connector->eld,
			       min(sizeof(connector->eld), len));
			ret = 0;
		}
	}
	mutex_unlock(&config->mutex);

	return ret;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = rk628_hdmi_audio_hw_params,
	.audio_shutdown = rk628_hdmi_audio_shutdown,
	.mute_stream = rk628_hdmi_audio_mute,
	.no_capture_mute = 1,
	.get_eld = rk628_hdmi_audio_get_eld,
};

static int rk628_hdmi_audio_codec_init(struct rk628_hdmi *hdmi,
				       struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
	};

	hdmi->audio_enable = false;
	hdmi->audio_pdev = platform_device_register_data(dev,
				HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_NONE,
				&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(hdmi->audio_pdev);
}

static irqreturn_t rk628_hdmi_irq(int irq, void *dev_id)
{
	struct rk628_hdmi *hdmi = dev_id;
	u8 interrupt;

	/* clear interrupts */
	rk628_i2c_write(hdmi->rk628, GRF_INTR0_CLR_EN, 0x00040004);
	interrupt = hdmi_readb(hdmi, HDMI_STATUS);
	if (!(interrupt & INT_HOTPLUG))
		return IRQ_HANDLED;

	hdmi_modb(hdmi, HDMI_STATUS, INT_HOTPLUG, INT_HOTPLUG);
	if (hdmi->connector.dev)
		drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static int rk628_hdmi_i2c_read(struct rk628_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int i;
	u32 c;

	for (i = 0; i < 5; i++) {
		msleep(20);
		c = hdmi_readb(hdmi, HDMI_INTERRUPT_STATUS1);
		if (c & INT_EDID_READY)
			break;
	}
	if ((c & INT_EDID_READY) == 0)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_EDID_FIFO_ADDR);

	return 0;
}

static int rk628_hdmi_i2c_write(struct rk628_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];
	if (msgs->addr == DDC_SEGMENT_ADDR) {
		hdmi->i2c->segment_addr = msgs->buf[0];
		return 0;
	}

	/* Set edid fifo first addr */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_OFFSET, 0x00);

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int rk628_hdmi_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct rk628_hdmi *hdmi = i2c_get_adapdata(adap);
	struct rk628_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	hdmi->i2c->ddc_addr = 0;
	hdmi->i2c->segment_addr = 0;

	/* Clear the EDID interrupt flag and unmute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, INT_EDID_READY);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, INT_EDID_READY_MASK);

	for (i = 0; i < num; i++) {
		dev_dbg(hdmi->dev, "xfer: num: %d/%d, len: %d, flags: %#x\n",
			i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = rk628_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = rk628_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, INT_EDID_READY);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 rk628_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rk628_hdmi_algorithm = {
	.master_xfer	= rk628_hdmi_i2c_xfer,
	.functionality	= rk628_hdmi_i2c_func,
};

static struct i2c_adapter *rk628_hdmi_i2c_adapter(struct rk628_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct rk628_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &rk628_hdmi_algorithm;
	strscpy(adap->name, "RK628 HDMI", sizeof(adap->name));
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

int rk628_hdmitx_enable(struct rk628 *rk628)
{
	struct device *dev = rk628->dev;
	struct rk628_hdmi *hdmi;
	int irq;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;


	hdmi->dev = dev;
	hdmi->rk628 = rk628;

	irq = rk628->client->irq;
	if (irq < 0)
		return irq;
	dev_set_drvdata(dev, hdmi);

	/* selete int io function */
	rk628_i2c_write(rk628, GRF_GPIO0AB_SEL_CON, 0x70007000);
	rk628_i2c_write(rk628, GRF_GPIO0AB_SEL_CON, 0x055c055c);

	/* hdmitx vclk pllref select Pin_vclk */
	rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON,
			   SW_HDMITX_VCLK_PLLREF_SEL_MASK,
			   SW_HDMITX_VCLK_PLLREF_SEL(1));
	/* set output mode to HDMI */
	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_HDMI));

	rk628_hdmi_reset(hdmi);

	hdmi->ddc = rk628_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		goto fail;
	}

	/*
	 * When IP controller haven't configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * PCLK_HDMI, so we need to init the TMDS rate to PCLK rate,
	 * and reconfigure the DDC clock.
	 */
	hdmi->tmds_rate = 24000 * 1000;

	/* hdmitx int en */
	rk628_i2c_write(rk628, GRF_INTR0_EN, 0x00040004);
	rk628_hdmi_i2c_init(hdmi);

	rk628_hdmi_audio_codec_init(hdmi, dev);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					rk628_hdmi_irq,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					dev_name(dev), hdmi);
	if (ret) {
		dev_err(dev, "failed to request hdmi irq: %d\n", ret);
		goto fail;
	}

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, MASK_INT_HOTPLUG_MASK,
		  MASK_INT_HOTPLUG(1));
	hdmi->bridge.funcs = &rk628_hdmi_bridge_funcs;
	hdmi->bridge.of_node = dev->of_node;

	drm_bridge_add(&hdmi->bridge);

	hdmi->extcon = devm_extcon_dev_allocate(hdmi->dev, rk628_hdmi_cable);
	if (IS_ERR(hdmi->extcon)) {
		dev_err(hdmi->dev, "allocate extcon failed\n");
		ret = PTR_ERR(hdmi->extcon);
		goto fail;
	}

	ret = devm_extcon_dev_register(hdmi->dev, hdmi->extcon);
	if (ret) {
		dev_err(dev, "failed to register extcon: %d\n", ret);
		goto fail;
	}

	ret = extcon_set_property_capability(hdmi->extcon, EXTCON_DISP_HDMI,
					     EXTCON_PROP_DISP_HPD);
	if (ret) {
		dev_err(dev, "failed to set USB property capability: %d\n",
			ret);
		goto fail;
	}

	return 0;

fail:
	return ret;
}

MODULE_AUTHOR("Chen Shunqing <csq@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 HDMI driver");
MODULE_LICENSE("GPL");
