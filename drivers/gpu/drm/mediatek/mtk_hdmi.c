/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/hdmi-codec.h>
#include "mtk_cec.h"
#include "mtk_hdmi.h"
#include "mtk_hdmi_regs.h"

#define NCTS_BYTES	7

enum mtk_hdmi_clk_id {
	MTK_HDMI_CLK_HDMI_PIXEL,
	MTK_HDMI_CLK_HDMI_PLL,
	MTK_HDMI_CLK_AUD_BCLK,
	MTK_HDMI_CLK_AUD_SPDIF,
	MTK_HDMI_CLK_COUNT
};

enum hdmi_aud_input_type {
	HDMI_AUD_INPUT_I2S = 0,
	HDMI_AUD_INPUT_SPDIF,
};

enum hdmi_aud_i2s_fmt {
	HDMI_I2S_MODE_RJT_24BIT = 0,
	HDMI_I2S_MODE_RJT_16BIT,
	HDMI_I2S_MODE_LJT_24BIT,
	HDMI_I2S_MODE_LJT_16BIT,
	HDMI_I2S_MODE_I2S_24BIT,
	HDMI_I2S_MODE_I2S_16BIT
};

enum hdmi_aud_mclk {
	HDMI_AUD_MCLK_128FS,
	HDMI_AUD_MCLK_192FS,
	HDMI_AUD_MCLK_256FS,
	HDMI_AUD_MCLK_384FS,
	HDMI_AUD_MCLK_512FS,
	HDMI_AUD_MCLK_768FS,
	HDMI_AUD_MCLK_1152FS,
};

enum hdmi_aud_channel_type {
	HDMI_AUD_CHAN_TYPE_1_0 = 0,
	HDMI_AUD_CHAN_TYPE_1_1,
	HDMI_AUD_CHAN_TYPE_2_0,
	HDMI_AUD_CHAN_TYPE_2_1,
	HDMI_AUD_CHAN_TYPE_3_0,
	HDMI_AUD_CHAN_TYPE_3_1,
	HDMI_AUD_CHAN_TYPE_4_0,
	HDMI_AUD_CHAN_TYPE_4_1,
	HDMI_AUD_CHAN_TYPE_5_0,
	HDMI_AUD_CHAN_TYPE_5_1,
	HDMI_AUD_CHAN_TYPE_6_0,
	HDMI_AUD_CHAN_TYPE_6_1,
	HDMI_AUD_CHAN_TYPE_7_0,
	HDMI_AUD_CHAN_TYPE_7_1,
	HDMI_AUD_CHAN_TYPE_3_0_LRS,
	HDMI_AUD_CHAN_TYPE_3_1_LRS,
	HDMI_AUD_CHAN_TYPE_4_0_CLRS,
	HDMI_AUD_CHAN_TYPE_4_1_CLRS,
	HDMI_AUD_CHAN_TYPE_6_1_CS,
	HDMI_AUD_CHAN_TYPE_6_1_CH,
	HDMI_AUD_CHAN_TYPE_6_1_OH,
	HDMI_AUD_CHAN_TYPE_6_1_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_LH_RH,
	HDMI_AUD_CHAN_TYPE_7_1_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_7_1_LC_RC,
	HDMI_AUD_CHAN_TYPE_7_1_LW_RW,
	HDMI_AUD_CHAN_TYPE_7_1_LSD_RSD,
	HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS,
	HDMI_AUD_CHAN_TYPE_7_1_LHS_RHS,
	HDMI_AUD_CHAN_TYPE_7_1_CS_CH,
	HDMI_AUD_CHAN_TYPE_7_1_CS_OH,
	HDMI_AUD_CHAN_TYPE_7_1_CS_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_CH_OH,
	HDMI_AUD_CHAN_TYPE_7_1_CH_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_OH_CHR,
	HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_6_0_CS,
	HDMI_AUD_CHAN_TYPE_6_0_CH,
	HDMI_AUD_CHAN_TYPE_6_0_OH,
	HDMI_AUD_CHAN_TYPE_6_0_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_LH_RH,
	HDMI_AUD_CHAN_TYPE_7_0_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_7_0_LC_RC,
	HDMI_AUD_CHAN_TYPE_7_0_LW_RW,
	HDMI_AUD_CHAN_TYPE_7_0_LSD_RSD,
	HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS,
	HDMI_AUD_CHAN_TYPE_7_0_LHS_RHS,
	HDMI_AUD_CHAN_TYPE_7_0_CS_CH,
	HDMI_AUD_CHAN_TYPE_7_0_CS_OH,
	HDMI_AUD_CHAN_TYPE_7_0_CS_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_CH_OH,
	HDMI_AUD_CHAN_TYPE_7_0_CH_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_OH_CHR,
	HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS_LSR_RSR,
	HDMI_AUD_CHAN_TYPE_8_0_LH_RH_CS,
	HDMI_AUD_CHAN_TYPE_UNKNOWN = 0xFF
};

enum hdmi_aud_channel_swap_type {
	HDMI_AUD_SWAP_LR,
	HDMI_AUD_SWAP_LFE_CC,
	HDMI_AUD_SWAP_LSRS,
	HDMI_AUD_SWAP_RLS_RRS,
	HDMI_AUD_SWAP_LR_STATUS,
};

struct hdmi_audio_param {
	enum hdmi_audio_coding_type aud_codec;
	enum hdmi_audio_sample_size aud_sampe_size;
	enum hdmi_aud_input_type aud_input_type;
	enum hdmi_aud_i2s_fmt aud_i2s_fmt;
	enum hdmi_aud_mclk aud_mclk;
	enum hdmi_aud_channel_type aud_input_chan_type;
	struct hdmi_codec_params codec_params;
};

struct mtk_hdmi {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector conn;
	struct device *dev;
	struct phy *phy;
	struct device *cec_dev;
	struct i2c_adapter *ddc_adpt;
	struct clk *clk[MTK_HDMI_CLK_COUNT];
	struct drm_display_mode mode;
	bool dvi_mode;
	u32 min_clock;
	u32 max_clock;
	u32 max_hdisplay;
	u32 max_vdisplay;
	u32 ibias;
	u32 ibias_up;
	struct regmap *sys_regmap;
	unsigned int sys_offset;
	void __iomem *regs;
	enum hdmi_colorspace csp;
	struct hdmi_audio_param aud_param;
	bool audio_enable;
	bool powered;
	bool enabled;
};

static inline struct mtk_hdmi *hdmi_ctx_from_bridge(struct drm_bridge *b)
{
	return container_of(b, struct mtk_hdmi, bridge);
}

static inline struct mtk_hdmi *hdmi_ctx_from_conn(struct drm_connector *c)
{
	return container_of(c, struct mtk_hdmi, conn);
}

static u32 mtk_hdmi_read(struct mtk_hdmi *hdmi, u32 offset)
{
	return readl(hdmi->regs + offset);
}

static void mtk_hdmi_write(struct mtk_hdmi *hdmi, u32 offset, u32 val)
{
	writel(val, hdmi->regs + offset);
}

static void mtk_hdmi_clear_bits(struct mtk_hdmi *hdmi, u32 offset, u32 bits)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp &= ~bits;
	writel(tmp, reg);
}

static void mtk_hdmi_set_bits(struct mtk_hdmi *hdmi, u32 offset, u32 bits)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp |= bits;
	writel(tmp, reg);
}

static void mtk_hdmi_mask(struct mtk_hdmi *hdmi, u32 offset, u32 val, u32 mask)
{
	void __iomem *reg = hdmi->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, reg);
}

static void mtk_hdmi_hw_vid_black(struct mtk_hdmi *hdmi, bool black)
{
	mtk_hdmi_mask(hdmi, VIDEO_CFG_4, black ? GEN_RGB : NORMAL_PATH,
		      VIDEO_SOURCE_SEL);
}

static void mtk_hdmi_hw_make_reg_writable(struct mtk_hdmi *hdmi, bool enable)
{
	struct arm_smccc_res res;

	/*
	 * MT8173 HDMI hardware has an output control bit to enable/disable HDMI
	 * output. This bit can only be controlled in ARM supervisor mode.
	 * The ARM trusted firmware provides an API for the HDMI driver to set
	 * this control bit to enable HDMI output in supervisor mode.
	 */
	arm_smccc_smc(MTK_SIP_SET_AUTHORIZED_SECURE_REG, 0x14000904, 0x80000000,
		      0, 0, 0, 0, 0, &res);

	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG20,
			   HDMI_PCLK_FREE_RUN, enable ? HDMI_PCLK_FREE_RUN : 0);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   HDMI_ON | ANLG_ON, enable ? (HDMI_ON | ANLG_ON) : 0);
}

static void mtk_hdmi_hw_1p4_version_enable(struct mtk_hdmi *hdmi, bool enable)
{
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG20,
			   HDMI2P0_EN, enable ? 0 : HDMI2P0_EN);
}

static void mtk_hdmi_hw_aud_mute(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_set_bits(hdmi, GRL_AUDIO_CFG, AUDIO_ZERO);
}

static void mtk_hdmi_hw_aud_unmute(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_clear_bits(hdmi, GRL_AUDIO_CFG, AUDIO_ZERO);
}

static void mtk_hdmi_hw_reset(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   HDMI_RST, HDMI_RST);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   HDMI_RST, 0);
	mtk_hdmi_clear_bits(hdmi, GRL_CFG3, CFG3_CONTROL_PACKET_DELAY);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   ANLG_ON, ANLG_ON);
}

static void mtk_hdmi_hw_enable_notice(struct mtk_hdmi *hdmi, bool enable_notice)
{
	mtk_hdmi_mask(hdmi, GRL_CFG2, enable_notice ? CFG2_NOTICE_EN : 0,
		      CFG2_NOTICE_EN);
}

static void mtk_hdmi_hw_write_int_mask(struct mtk_hdmi *hdmi, u32 int_mask)
{
	mtk_hdmi_write(hdmi, GRL_INT_MASK, int_mask);
}

static void mtk_hdmi_hw_enable_dvi_mode(struct mtk_hdmi *hdmi, bool enable)
{
	mtk_hdmi_mask(hdmi, GRL_CFG1, enable ? CFG1_DVI : 0, CFG1_DVI);
}

static void mtk_hdmi_hw_send_info_frame(struct mtk_hdmi *hdmi, u8 *buffer,
					u8 len)
{
	u32 ctrl_reg = GRL_CTRL;
	int i;
	u8 *frame_data;
	enum hdmi_infoframe_type frame_type;
	u8 frame_ver;
	u8 frame_len;
	u8 checksum;
	int ctrl_frame_en = 0;

	frame_type = *buffer;
	buffer += 1;
	frame_ver = *buffer;
	buffer += 1;
	frame_len = *buffer;
	buffer += 1;
	checksum = *buffer;
	buffer += 1;
	frame_data = buffer;

	dev_dbg(hdmi->dev,
		"frame_type:0x%x,frame_ver:0x%x,frame_len:0x%x,checksum:0x%x\n",
		frame_type, frame_ver, frame_len, checksum);

	switch (frame_type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		ctrl_frame_en = CTRL_AVI_EN;
		ctrl_reg = GRL_CTRL;
		break;
	case HDMI_INFOFRAME_TYPE_SPD:
		ctrl_frame_en = CTRL_SPD_EN;
		ctrl_reg = GRL_CTRL;
		break;
	case HDMI_INFOFRAME_TYPE_AUDIO:
		ctrl_frame_en = CTRL_AUDIO_EN;
		ctrl_reg = GRL_CTRL;
		break;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		ctrl_frame_en = VS_EN;
		ctrl_reg = GRL_ACP_ISRC_CTRL;
		break;
	}
	mtk_hdmi_clear_bits(hdmi, ctrl_reg, ctrl_frame_en);
	mtk_hdmi_write(hdmi, GRL_INFOFRM_TYPE, frame_type);
	mtk_hdmi_write(hdmi, GRL_INFOFRM_VER, frame_ver);
	mtk_hdmi_write(hdmi, GRL_INFOFRM_LNG, frame_len);

	mtk_hdmi_write(hdmi, GRL_IFM_PORT, checksum);
	for (i = 0; i < frame_len; i++)
		mtk_hdmi_write(hdmi, GRL_IFM_PORT, frame_data[i]);

	mtk_hdmi_set_bits(hdmi, ctrl_reg, ctrl_frame_en);
}

static void mtk_hdmi_hw_send_aud_packet(struct mtk_hdmi *hdmi, bool enable)
{
	mtk_hdmi_mask(hdmi, GRL_SHIFT_R2, enable ? 0 : AUDIO_PACKET_OFF,
		      AUDIO_PACKET_OFF);
}

static void mtk_hdmi_hw_config_sys(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG20,
			   HDMI_OUT_FIFO_EN | MHL_MODE_ON, 0);
	usleep_range(2000, 4000);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG20,
			   HDMI_OUT_FIFO_EN | MHL_MODE_ON, HDMI_OUT_FIFO_EN);
}

static void mtk_hdmi_hw_set_deep_color_mode(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG20,
			   DEEP_COLOR_MODE_MASK | DEEP_COLOR_EN,
			   COLOR_8BIT_MODE);
}

static void mtk_hdmi_hw_send_av_mute(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_clear_bits(hdmi, GRL_CFG4, CTRL_AVMUTE);
	usleep_range(2000, 4000);
	mtk_hdmi_set_bits(hdmi, GRL_CFG4, CTRL_AVMUTE);
}

static void mtk_hdmi_hw_send_av_unmute(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_mask(hdmi, GRL_CFG4, CFG4_AV_UNMUTE_EN,
		      CFG4_AV_UNMUTE_EN | CFG4_AV_UNMUTE_SET);
	usleep_range(2000, 4000);
	mtk_hdmi_mask(hdmi, GRL_CFG4, CFG4_AV_UNMUTE_SET,
		      CFG4_AV_UNMUTE_EN | CFG4_AV_UNMUTE_SET);
}

static void mtk_hdmi_hw_ncts_enable(struct mtk_hdmi *hdmi, bool on)
{
	mtk_hdmi_mask(hdmi, GRL_CTS_CTRL, on ? 0 : CTS_CTRL_SOFT,
		      CTS_CTRL_SOFT);
}

static void mtk_hdmi_hw_ncts_auto_write_enable(struct mtk_hdmi *hdmi,
					       bool enable)
{
	mtk_hdmi_mask(hdmi, GRL_CTS_CTRL, enable ? NCTS_WRI_ANYTIME : 0,
		      NCTS_WRI_ANYTIME);
}

static void mtk_hdmi_hw_msic_setting(struct mtk_hdmi *hdmi,
				     struct drm_display_mode *mode)
{
	mtk_hdmi_clear_bits(hdmi, GRL_CFG4, CFG4_MHL_MODE);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE &&
	    mode->clock == 74250 &&
	    mode->vdisplay == 1080)
		mtk_hdmi_clear_bits(hdmi, GRL_CFG2, CFG2_MHL_DE_SEL);
	else
		mtk_hdmi_set_bits(hdmi, GRL_CFG2, CFG2_MHL_DE_SEL);
}

static void mtk_hdmi_hw_aud_set_channel_swap(struct mtk_hdmi *hdmi,
					enum hdmi_aud_channel_swap_type swap)
{
	u8 swap_bit;

	switch (swap) {
	case HDMI_AUD_SWAP_LR:
		swap_bit = LR_SWAP;
		break;
	case HDMI_AUD_SWAP_LFE_CC:
		swap_bit = LFE_CC_SWAP;
		break;
	case HDMI_AUD_SWAP_LSRS:
		swap_bit = LSRS_SWAP;
		break;
	case HDMI_AUD_SWAP_RLS_RRS:
		swap_bit = RLS_RRS_SWAP;
		break;
	case HDMI_AUD_SWAP_LR_STATUS:
		swap_bit = LR_STATUS_SWAP;
		break;
	default:
		swap_bit = LFE_CC_SWAP;
		break;
	}
	mtk_hdmi_mask(hdmi, GRL_CH_SWAP, swap_bit, 0xff);
}

static void mtk_hdmi_hw_aud_set_bit_num(struct mtk_hdmi *hdmi,
					enum hdmi_audio_sample_size bit_num)
{
	u32 val;

	switch (bit_num) {
	case HDMI_AUDIO_SAMPLE_SIZE_16:
		val = AOUT_16BIT;
		break;
	case HDMI_AUDIO_SAMPLE_SIZE_20:
		val = AOUT_20BIT;
		break;
	case HDMI_AUDIO_SAMPLE_SIZE_24:
	case HDMI_AUDIO_SAMPLE_SIZE_STREAM:
		val = AOUT_24BIT;
		break;
	}

	mtk_hdmi_mask(hdmi, GRL_AOUT_CFG, val, AOUT_BNUM_SEL_MASK);
}

static void mtk_hdmi_hw_aud_set_i2s_fmt(struct mtk_hdmi *hdmi,
					enum hdmi_aud_i2s_fmt i2s_fmt)
{
	u32 val;

	val = mtk_hdmi_read(hdmi, GRL_CFG0);
	val &= ~(CFG0_W_LENGTH_MASK | CFG0_I2S_MODE_MASK);

	switch (i2s_fmt) {
	case HDMI_I2S_MODE_RJT_24BIT:
		val |= CFG0_I2S_MODE_RTJ | CFG0_W_LENGTH_24BIT;
		break;
	case HDMI_I2S_MODE_RJT_16BIT:
		val |= CFG0_I2S_MODE_RTJ | CFG0_W_LENGTH_16BIT;
		break;
	case HDMI_I2S_MODE_LJT_24BIT:
	default:
		val |= CFG0_I2S_MODE_LTJ | CFG0_W_LENGTH_24BIT;
		break;
	case HDMI_I2S_MODE_LJT_16BIT:
		val |= CFG0_I2S_MODE_LTJ | CFG0_W_LENGTH_16BIT;
		break;
	case HDMI_I2S_MODE_I2S_24BIT:
		val |= CFG0_I2S_MODE_I2S | CFG0_W_LENGTH_24BIT;
		break;
	case HDMI_I2S_MODE_I2S_16BIT:
		val |= CFG0_I2S_MODE_I2S | CFG0_W_LENGTH_16BIT;
		break;
	}
	mtk_hdmi_write(hdmi, GRL_CFG0, val);
}

static void mtk_hdmi_hw_audio_config(struct mtk_hdmi *hdmi, bool dst)
{
	const u8 mask = HIGH_BIT_RATE | DST_NORMAL_DOUBLE | SACD_DST | DSD_SEL;
	u8 val;

	/* Disable high bitrate, set DST packet normal/double */
	mtk_hdmi_clear_bits(hdmi, GRL_AOUT_CFG, HIGH_BIT_RATE_PACKET_ALIGN);

	if (dst)
		val = DST_NORMAL_DOUBLE | SACD_DST;
	else
		val = 0;

	mtk_hdmi_mask(hdmi, GRL_AUDIO_CFG, val, mask);
}

static void mtk_hdmi_hw_aud_set_i2s_chan_num(struct mtk_hdmi *hdmi,
					enum hdmi_aud_channel_type channel_type,
					u8 channel_count)
{
	unsigned int ch_switch;
	u8 i2s_uv;

	ch_switch = CH_SWITCH(7, 7) | CH_SWITCH(6, 6) |
		    CH_SWITCH(5, 5) | CH_SWITCH(4, 4) |
		    CH_SWITCH(3, 3) | CH_SWITCH(1, 2) |
		    CH_SWITCH(2, 1) | CH_SWITCH(0, 0);

	if (channel_count == 2) {
		i2s_uv = I2S_UV_CH_EN(0);
	} else if (channel_count == 3 || channel_count == 4) {
		if (channel_count == 4 &&
		    (channel_type == HDMI_AUD_CHAN_TYPE_3_0_LRS ||
		    channel_type == HDMI_AUD_CHAN_TYPE_4_0))
			i2s_uv = I2S_UV_CH_EN(2) | I2S_UV_CH_EN(0);
		else
			i2s_uv = I2S_UV_CH_EN(3) | I2S_UV_CH_EN(2);
	} else if (channel_count == 6 || channel_count == 5) {
		if (channel_count == 6 &&
		    channel_type != HDMI_AUD_CHAN_TYPE_5_1 &&
		    channel_type != HDMI_AUD_CHAN_TYPE_4_1_CLRS) {
			i2s_uv = I2S_UV_CH_EN(3) | I2S_UV_CH_EN(2) |
				 I2S_UV_CH_EN(1) | I2S_UV_CH_EN(0);
		} else {
			i2s_uv = I2S_UV_CH_EN(2) | I2S_UV_CH_EN(1) |
				 I2S_UV_CH_EN(0);
		}
	} else if (channel_count == 8 || channel_count == 7) {
		i2s_uv = I2S_UV_CH_EN(3) | I2S_UV_CH_EN(2) |
			 I2S_UV_CH_EN(1) | I2S_UV_CH_EN(0);
	} else {
		i2s_uv = I2S_UV_CH_EN(0);
	}

	mtk_hdmi_write(hdmi, GRL_CH_SW0, ch_switch & 0xff);
	mtk_hdmi_write(hdmi, GRL_CH_SW1, (ch_switch >> 8) & 0xff);
	mtk_hdmi_write(hdmi, GRL_CH_SW2, (ch_switch >> 16) & 0xff);
	mtk_hdmi_write(hdmi, GRL_I2S_UV, i2s_uv);
}

static void mtk_hdmi_hw_aud_set_input_type(struct mtk_hdmi *hdmi,
					   enum hdmi_aud_input_type input_type)
{
	u32 val;

	val = mtk_hdmi_read(hdmi, GRL_CFG1);
	if (input_type == HDMI_AUD_INPUT_I2S &&
	    (val & CFG1_SPDIF) == CFG1_SPDIF) {
		val &= ~CFG1_SPDIF;
	} else if (input_type == HDMI_AUD_INPUT_SPDIF &&
		(val & CFG1_SPDIF) == 0) {
		val |= CFG1_SPDIF;
	}
	mtk_hdmi_write(hdmi, GRL_CFG1, val);
}

static void mtk_hdmi_hw_aud_set_channel_status(struct mtk_hdmi *hdmi,
					       u8 *channel_status)
{
	int i;

	for (i = 0; i < 5; i++) {
		mtk_hdmi_write(hdmi, GRL_I2S_C_STA0 + i * 4, channel_status[i]);
		mtk_hdmi_write(hdmi, GRL_L_STATUS_0 + i * 4, channel_status[i]);
		mtk_hdmi_write(hdmi, GRL_R_STATUS_0 + i * 4, channel_status[i]);
	}
	for (; i < 24; i++) {
		mtk_hdmi_write(hdmi, GRL_L_STATUS_0 + i * 4, 0);
		mtk_hdmi_write(hdmi, GRL_R_STATUS_0 + i * 4, 0);
	}
}

static void mtk_hdmi_hw_aud_src_reenable(struct mtk_hdmi *hdmi)
{
	u32 val;

	val = mtk_hdmi_read(hdmi, GRL_MIX_CTRL);
	if (val & MIX_CTRL_SRC_EN) {
		val &= ~MIX_CTRL_SRC_EN;
		mtk_hdmi_write(hdmi, GRL_MIX_CTRL, val);
		usleep_range(255, 512);
		val |= MIX_CTRL_SRC_EN;
		mtk_hdmi_write(hdmi, GRL_MIX_CTRL, val);
	}
}

static void mtk_hdmi_hw_aud_src_disable(struct mtk_hdmi *hdmi)
{
	u32 val;

	val = mtk_hdmi_read(hdmi, GRL_MIX_CTRL);
	val &= ~MIX_CTRL_SRC_EN;
	mtk_hdmi_write(hdmi, GRL_MIX_CTRL, val);
	mtk_hdmi_write(hdmi, GRL_SHIFT_L1, 0x00);
}

static void mtk_hdmi_hw_aud_set_mclk(struct mtk_hdmi *hdmi,
				     enum hdmi_aud_mclk mclk)
{
	u32 val;

	val = mtk_hdmi_read(hdmi, GRL_CFG5);
	val &= CFG5_CD_RATIO_MASK;

	switch (mclk) {
	case HDMI_AUD_MCLK_128FS:
		val |= CFG5_FS128;
		break;
	case HDMI_AUD_MCLK_256FS:
		val |= CFG5_FS256;
		break;
	case HDMI_AUD_MCLK_384FS:
		val |= CFG5_FS384;
		break;
	case HDMI_AUD_MCLK_512FS:
		val |= CFG5_FS512;
		break;
	case HDMI_AUD_MCLK_768FS:
		val |= CFG5_FS768;
		break;
	default:
		val |= CFG5_FS256;
		break;
	}
	mtk_hdmi_write(hdmi, GRL_CFG5, val);
}

struct hdmi_acr_n {
	unsigned int clock;
	unsigned int n[3];
};

/* Recommended N values from HDMI specification, tables 7-1 to 7-3 */
static const struct hdmi_acr_n hdmi_rec_n_table[] = {
	/* Clock, N: 32kHz 44.1kHz 48kHz */
	{  25175, {  4576,  7007,  6864 } },
	{  74176, { 11648, 17836, 11648 } },
	{ 148352, { 11648,  8918,  5824 } },
	{ 296703, {  5824,  4459,  5824 } },
	{ 297000, {  3072,  4704,  5120 } },
	{      0, {  4096,  6272,  6144 } }, /* all other TMDS clocks */
};

/**
 * hdmi_recommended_n() - Return N value recommended by HDMI specification
 * @freq: audio sample rate in Hz
 * @clock: rounded TMDS clock in kHz
 */
static unsigned int hdmi_recommended_n(unsigned int freq, unsigned int clock)
{
	const struct hdmi_acr_n *recommended;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_rec_n_table) - 1; i++) {
		if (clock == hdmi_rec_n_table[i].clock)
			break;
	}
	recommended = hdmi_rec_n_table + i;

	switch (freq) {
	case 32000:
		return recommended->n[0];
	case 44100:
		return recommended->n[1];
	case 48000:
		return recommended->n[2];
	case 88200:
		return recommended->n[1] * 2;
	case 96000:
		return recommended->n[2] * 2;
	case 176400:
		return recommended->n[1] * 4;
	case 192000:
		return recommended->n[2] * 4;
	default:
		return (128 * freq) / 1000;
	}
}

static unsigned int hdmi_mode_clock_to_hz(unsigned int clock)
{
	switch (clock) {
	case 25175:
		return 25174825;	/* 25.2/1.001 MHz */
	case 74176:
		return 74175824;	/* 74.25/1.001 MHz */
	case 148352:
		return 148351648;	/* 148.5/1.001 MHz */
	case 296703:
		return 296703297;	/* 297/1.001 MHz */
	default:
		return clock * 1000;
	}
}

static unsigned int hdmi_expected_cts(unsigned int audio_sample_rate,
				      unsigned int tmds_clock, unsigned int n)
{
	return DIV_ROUND_CLOSEST_ULL((u64)hdmi_mode_clock_to_hz(tmds_clock) * n,
				     128 * audio_sample_rate);
}

static void do_hdmi_hw_aud_set_ncts(struct mtk_hdmi *hdmi, unsigned int n,
				    unsigned int cts)
{
	unsigned char val[NCTS_BYTES];
	int i;

	mtk_hdmi_write(hdmi, GRL_NCTS, 0);
	mtk_hdmi_write(hdmi, GRL_NCTS, 0);
	mtk_hdmi_write(hdmi, GRL_NCTS, 0);
	memset(val, 0, sizeof(val));

	val[0] = (cts >> 24) & 0xff;
	val[1] = (cts >> 16) & 0xff;
	val[2] = (cts >> 8) & 0xff;
	val[3] = cts & 0xff;

	val[4] = (n >> 16) & 0xff;
	val[5] = (n >> 8) & 0xff;
	val[6] = n & 0xff;

	for (i = 0; i < NCTS_BYTES; i++)
		mtk_hdmi_write(hdmi, GRL_NCTS, val[i]);
}

static void mtk_hdmi_hw_aud_set_ncts(struct mtk_hdmi *hdmi,
				     unsigned int sample_rate,
				     unsigned int clock)
{
	unsigned int n, cts;

	n = hdmi_recommended_n(sample_rate, clock);
	cts = hdmi_expected_cts(sample_rate, clock, n);

	dev_dbg(hdmi->dev, "%s: sample_rate=%u, clock=%d, cts=%u, n=%u\n",
		__func__, sample_rate, clock, n, cts);

	mtk_hdmi_mask(hdmi, DUMMY_304, AUDIO_I2S_NCTS_SEL_64,
		      AUDIO_I2S_NCTS_SEL);
	do_hdmi_hw_aud_set_ncts(hdmi, n, cts);
}

static u8 mtk_hdmi_aud_get_chnl_count(enum hdmi_aud_channel_type channel_type)
{
	switch (channel_type) {
	case HDMI_AUD_CHAN_TYPE_1_0:
	case HDMI_AUD_CHAN_TYPE_1_1:
	case HDMI_AUD_CHAN_TYPE_2_0:
		return 2;
	case HDMI_AUD_CHAN_TYPE_2_1:
	case HDMI_AUD_CHAN_TYPE_3_0:
		return 3;
	case HDMI_AUD_CHAN_TYPE_3_1:
	case HDMI_AUD_CHAN_TYPE_4_0:
	case HDMI_AUD_CHAN_TYPE_3_0_LRS:
		return 4;
	case HDMI_AUD_CHAN_TYPE_4_1:
	case HDMI_AUD_CHAN_TYPE_5_0:
	case HDMI_AUD_CHAN_TYPE_3_1_LRS:
	case HDMI_AUD_CHAN_TYPE_4_0_CLRS:
		return 5;
	case HDMI_AUD_CHAN_TYPE_5_1:
	case HDMI_AUD_CHAN_TYPE_6_0:
	case HDMI_AUD_CHAN_TYPE_4_1_CLRS:
	case HDMI_AUD_CHAN_TYPE_6_0_CS:
	case HDMI_AUD_CHAN_TYPE_6_0_CH:
	case HDMI_AUD_CHAN_TYPE_6_0_OH:
	case HDMI_AUD_CHAN_TYPE_6_0_CHR:
		return 6;
	case HDMI_AUD_CHAN_TYPE_6_1:
	case HDMI_AUD_CHAN_TYPE_6_1_CS:
	case HDMI_AUD_CHAN_TYPE_6_1_CH:
	case HDMI_AUD_CHAN_TYPE_6_1_OH:
	case HDMI_AUD_CHAN_TYPE_6_1_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0:
	case HDMI_AUD_CHAN_TYPE_7_0_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_0_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_0_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_0_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_0_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_0_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_0_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_0_LSS_RSS_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_8_0_LH_RH_CS:
		return 7;
	case HDMI_AUD_CHAN_TYPE_7_1:
	case HDMI_AUD_CHAN_TYPE_7_1_LH_RH:
	case HDMI_AUD_CHAN_TYPE_7_1_LSR_RSR:
	case HDMI_AUD_CHAN_TYPE_7_1_LC_RC:
	case HDMI_AUD_CHAN_TYPE_7_1_LW_RW:
	case HDMI_AUD_CHAN_TYPE_7_1_LSD_RSD:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS:
	case HDMI_AUD_CHAN_TYPE_7_1_LHS_RHS:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CS_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_OH:
	case HDMI_AUD_CHAN_TYPE_7_1_CH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_OH_CHR:
	case HDMI_AUD_CHAN_TYPE_7_1_LSS_RSS_LSR_RSR:
		return 8;
	default:
		return 2;
	}
}

static int mtk_hdmi_video_change_vpll(struct mtk_hdmi *hdmi, u32 clock)
{
	unsigned long rate;
	int ret;

	/* The DPI driver already should have set TVDPLL to the correct rate */
	ret = clk_set_rate(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL], clock);
	if (ret) {
		dev_err(hdmi->dev, "Failed to set PLL to %u Hz: %d\n", clock,
			ret);
		return ret;
	}

	rate = clk_get_rate(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL]);

	if (DIV_ROUND_CLOSEST(rate, 1000) != DIV_ROUND_CLOSEST(clock, 1000))
		dev_warn(hdmi->dev, "Want PLL %u Hz, got %lu Hz\n", clock,
			 rate);
	else
		dev_dbg(hdmi->dev, "Want PLL %u Hz, got %lu Hz\n", clock, rate);

	mtk_hdmi_hw_config_sys(hdmi);
	mtk_hdmi_hw_set_deep_color_mode(hdmi);
	return 0;
}

static void mtk_hdmi_video_set_display_mode(struct mtk_hdmi *hdmi,
					    struct drm_display_mode *mode)
{
	mtk_hdmi_hw_reset(hdmi);
	mtk_hdmi_hw_enable_notice(hdmi, true);
	mtk_hdmi_hw_write_int_mask(hdmi, 0xff);
	mtk_hdmi_hw_enable_dvi_mode(hdmi, hdmi->dvi_mode);
	mtk_hdmi_hw_ncts_auto_write_enable(hdmi, true);

	mtk_hdmi_hw_msic_setting(hdmi, mode);
}

static int mtk_hdmi_aud_enable_packet(struct mtk_hdmi *hdmi, bool enable)
{
	mtk_hdmi_hw_send_aud_packet(hdmi, enable);
	return 0;
}

static int mtk_hdmi_aud_on_off_hw_ncts(struct mtk_hdmi *hdmi, bool on)
{
	mtk_hdmi_hw_ncts_enable(hdmi, on);
	return 0;
}

static int mtk_hdmi_aud_set_input(struct mtk_hdmi *hdmi)
{
	enum hdmi_aud_channel_type chan_type;
	u8 chan_count;
	bool dst;

	mtk_hdmi_hw_aud_set_channel_swap(hdmi, HDMI_AUD_SWAP_LFE_CC);
	mtk_hdmi_set_bits(hdmi, GRL_MIX_CTRL, MIX_CTRL_FLAT);

	if (hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_SPDIF &&
	    hdmi->aud_param.aud_codec == HDMI_AUDIO_CODING_TYPE_DST) {
		mtk_hdmi_hw_aud_set_bit_num(hdmi, HDMI_AUDIO_SAMPLE_SIZE_24);
	} else if (hdmi->aud_param.aud_i2s_fmt == HDMI_I2S_MODE_LJT_24BIT) {
		hdmi->aud_param.aud_i2s_fmt = HDMI_I2S_MODE_LJT_16BIT;
	}

	mtk_hdmi_hw_aud_set_i2s_fmt(hdmi, hdmi->aud_param.aud_i2s_fmt);
	mtk_hdmi_hw_aud_set_bit_num(hdmi, HDMI_AUDIO_SAMPLE_SIZE_24);

	dst = ((hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_SPDIF) &&
	       (hdmi->aud_param.aud_codec == HDMI_AUDIO_CODING_TYPE_DST));
	mtk_hdmi_hw_audio_config(hdmi, dst);

	if (hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_SPDIF)
		chan_type = HDMI_AUD_CHAN_TYPE_2_0;
	else
		chan_type = hdmi->aud_param.aud_input_chan_type;
	chan_count = mtk_hdmi_aud_get_chnl_count(chan_type);
	mtk_hdmi_hw_aud_set_i2s_chan_num(hdmi, chan_type, chan_count);
	mtk_hdmi_hw_aud_set_input_type(hdmi, hdmi->aud_param.aud_input_type);

	return 0;
}

static int mtk_hdmi_aud_set_src(struct mtk_hdmi *hdmi,
				struct drm_display_mode *display_mode)
{
	unsigned int sample_rate = hdmi->aud_param.codec_params.sample_rate;

	mtk_hdmi_aud_on_off_hw_ncts(hdmi, false);
	mtk_hdmi_hw_aud_src_disable(hdmi);
	mtk_hdmi_clear_bits(hdmi, GRL_CFG2, CFG2_ACLK_INV);

	if (hdmi->aud_param.aud_input_type == HDMI_AUD_INPUT_I2S) {
		switch (sample_rate) {
		case 32000:
		case 44100:
		case 48000:
		case 88200:
		case 96000:
			break;
		default:
			return -EINVAL;
		}
		mtk_hdmi_hw_aud_set_mclk(hdmi, hdmi->aud_param.aud_mclk);
	} else {
		switch (sample_rate) {
		case 32000:
		case 44100:
		case 48000:
			break;
		default:
			return -EINVAL;
		}
		mtk_hdmi_hw_aud_set_mclk(hdmi, HDMI_AUD_MCLK_128FS);
	}

	mtk_hdmi_hw_aud_set_ncts(hdmi, sample_rate, display_mode->clock);

	mtk_hdmi_hw_aud_src_reenable(hdmi);
	return 0;
}

static int mtk_hdmi_aud_output_config(struct mtk_hdmi *hdmi,
				      struct drm_display_mode *display_mode)
{
	mtk_hdmi_hw_aud_mute(hdmi);
	mtk_hdmi_aud_enable_packet(hdmi, false);

	mtk_hdmi_aud_set_input(hdmi);
	mtk_hdmi_aud_set_src(hdmi, display_mode);
	mtk_hdmi_hw_aud_set_channel_status(hdmi,
			hdmi->aud_param.codec_params.iec.status);

	usleep_range(50, 100);

	mtk_hdmi_aud_on_off_hw_ncts(hdmi, true);
	mtk_hdmi_aud_enable_packet(hdmi, true);
	mtk_hdmi_hw_aud_unmute(hdmi);
	return 0;
}

static int mtk_hdmi_setup_avi_infoframe(struct mtk_hdmi *hdmi,
					struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 buffer[17];
	ssize_t err;

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, mode, false);
	if (err < 0) {
		dev_err(hdmi->dev,
			"Failed to get AVI infoframe from mode: %zd\n", err);
		return err;
	}

	err = hdmi_avi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack AVI infoframe: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_send_info_frame(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_setup_spd_infoframe(struct mtk_hdmi *hdmi,
					const char *vendor,
					const char *product)
{
	struct hdmi_spd_infoframe frame;
	u8 buffer[29];
	ssize_t err;

	err = hdmi_spd_infoframe_init(&frame, vendor, product);
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to initialize SPD infoframe: %zd\n",
			err);
		return err;
	}

	err = hdmi_spd_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack SDP infoframe: %zd\n", err);
		return err;
	}

	mtk_hdmi_hw_send_info_frame(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_setup_audio_infoframe(struct mtk_hdmi *hdmi)
{
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err;

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to setup audio infoframe: %zd\n",
			err);
		return err;
	}

	frame.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	frame.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	frame.channels = mtk_hdmi_aud_get_chnl_count(
					hdmi->aud_param.aud_input_chan_type);

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack audio infoframe: %zd\n",
			err);
		return err;
	}

	mtk_hdmi_hw_send_info_frame(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_setup_vendor_specific_infoframe(struct mtk_hdmi *hdmi,
						struct drm_display_mode *mode)
{
	struct hdmi_vendor_infoframe frame;
	u8 buffer[10];
	ssize_t err;

	err = drm_hdmi_vendor_infoframe_from_display_mode(&frame,
							  &hdmi->conn, mode);
	if (err) {
		dev_err(hdmi->dev,
			"Failed to get vendor infoframe from mode: %zd\n", err);
		return err;
	}

	err = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(hdmi->dev, "Failed to pack vendor infoframe: %zd\n",
			err);
		return err;
	}

	mtk_hdmi_hw_send_info_frame(hdmi, buffer, sizeof(buffer));
	return 0;
}

static int mtk_hdmi_output_init(struct mtk_hdmi *hdmi)
{
	struct hdmi_audio_param *aud_param = &hdmi->aud_param;

	hdmi->csp = HDMI_COLORSPACE_RGB;
	aud_param->aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
	aud_param->aud_sampe_size = HDMI_AUDIO_SAMPLE_SIZE_16;
	aud_param->aud_input_type = HDMI_AUD_INPUT_I2S;
	aud_param->aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
	aud_param->aud_mclk = HDMI_AUD_MCLK_128FS;
	aud_param->aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;

	return 0;
}

static void mtk_hdmi_audio_enable(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_aud_enable_packet(hdmi, true);
	hdmi->audio_enable = true;
}

static void mtk_hdmi_audio_disable(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_aud_enable_packet(hdmi, false);
	hdmi->audio_enable = false;
}

static int mtk_hdmi_audio_set_param(struct mtk_hdmi *hdmi,
				    struct hdmi_audio_param *param)
{
	if (!hdmi->audio_enable) {
		dev_err(hdmi->dev, "hdmi audio is in disable state!\n");
		return -EINVAL;
	}
	dev_dbg(hdmi->dev, "codec:%d, input:%d, channel:%d, fs:%d\n",
		param->aud_codec, param->aud_input_type,
		param->aud_input_chan_type, param->codec_params.sample_rate);
	memcpy(&hdmi->aud_param, param, sizeof(*param));
	return mtk_hdmi_aud_output_config(hdmi, &hdmi->mode);
}

static int mtk_hdmi_output_set_display_mode(struct mtk_hdmi *hdmi,
					    struct drm_display_mode *mode)
{
	int ret;

	mtk_hdmi_hw_vid_black(hdmi, true);
	mtk_hdmi_hw_aud_mute(hdmi);
	mtk_hdmi_hw_send_av_mute(hdmi);
	phy_power_off(hdmi->phy);

	ret = mtk_hdmi_video_change_vpll(hdmi,
					 mode->clock * 1000);
	if (ret) {
		dev_err(hdmi->dev, "Failed to set vpll: %d\n", ret);
		return ret;
	}
	mtk_hdmi_video_set_display_mode(hdmi, mode);

	phy_power_on(hdmi->phy);
	mtk_hdmi_aud_output_config(hdmi, mode);

	mtk_hdmi_hw_vid_black(hdmi, false);
	mtk_hdmi_hw_aud_unmute(hdmi);
	mtk_hdmi_hw_send_av_unmute(hdmi);

	return 0;
}

static const char * const mtk_hdmi_clk_names[MTK_HDMI_CLK_COUNT] = {
	[MTK_HDMI_CLK_HDMI_PIXEL] = "pixel",
	[MTK_HDMI_CLK_HDMI_PLL] = "pll",
	[MTK_HDMI_CLK_AUD_BCLK] = "bclk",
	[MTK_HDMI_CLK_AUD_SPDIF] = "spdif",
};

static int mtk_hdmi_get_all_clk(struct mtk_hdmi *hdmi,
				struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_hdmi_clk_names); i++) {
		hdmi->clk[i] = of_clk_get_by_name(np,
						  mtk_hdmi_clk_names[i]);
		if (IS_ERR(hdmi->clk[i]))
			return PTR_ERR(hdmi->clk[i]);
	}
	return 0;
}

static int mtk_hdmi_clk_enable_audio(struct mtk_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_AUD_SPDIF]);
	if (ret)
		goto err;

	return 0;
err:
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
	return ret;
}

static void mtk_hdmi_clk_disable_audio(struct mtk_hdmi *hdmi)
{
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_SPDIF]);
}

static enum drm_connector_status hdmi_conn_detect(struct drm_connector *conn,
						  bool force)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

	return mtk_cec_hpd_high(hdmi->cec_dev) ?
	       connector_status_connected : connector_status_disconnected;
}

static void hdmi_conn_destroy(struct drm_connector *conn)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

	mtk_cec_set_hpd_event(hdmi->cec_dev, NULL, NULL);

	drm_connector_cleanup(conn);
}

static int mtk_hdmi_conn_get_modes(struct drm_connector *conn)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);
	struct edid *edid;
	int ret;

	if (!hdmi->ddc_adpt)
		return -ENODEV;

	edid = drm_get_edid(conn, hdmi->ddc_adpt);
	if (!edid)
		return -ENODEV;

	hdmi->dvi_mode = !drm_detect_monitor_audio(edid);

	drm_connector_update_edid_property(conn, edid);

	ret = drm_add_edid_modes(conn, edid);
	kfree(edid);
	return ret;
}

static int mtk_hdmi_conn_mode_valid(struct drm_connector *conn,
				    struct drm_display_mode *mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

	dev_dbg(hdmi->dev, "xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		mode->hdisplay, mode->vdisplay, mode->vrefresh,
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE), mode->clock * 1000);

	if (hdmi->bridge.next) {
		struct drm_display_mode adjusted_mode;

		drm_mode_copy(&adjusted_mode, mode);
		if (!drm_bridge_mode_fixup(hdmi->bridge.next, mode,
					   &adjusted_mode))
			return MODE_BAD;
	}

	if (mode->clock < 27000)
		return MODE_CLOCK_LOW;
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return drm_mode_validate_size(mode, 0x1fff, 0x1fff);
}

static struct drm_encoder *mtk_hdmi_conn_best_enc(struct drm_connector *conn)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_conn(conn);

	return hdmi->bridge.encoder;
}

static const struct drm_connector_funcs mtk_hdmi_connector_funcs = {
	.detect = hdmi_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = hdmi_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
		mtk_hdmi_connector_helper_funcs = {
	.get_modes = mtk_hdmi_conn_get_modes,
	.mode_valid = mtk_hdmi_conn_mode_valid,
	.best_encoder = mtk_hdmi_conn_best_enc,
};

static void mtk_hdmi_hpd_event(bool hpd, struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi && hdmi->bridge.encoder && hdmi->bridge.encoder->dev)
		drm_helper_hpd_irq_event(hdmi->bridge.encoder->dev);
}

/*
 * Bridge callbacks
 */

static int mtk_hdmi_bridge_attach(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	ret = drm_connector_init(bridge->encoder->dev, &hdmi->conn,
				 &mtk_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(hdmi->dev, "Failed to initialize connector: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(&hdmi->conn, &mtk_hdmi_connector_helper_funcs);

	hdmi->conn.polled = DRM_CONNECTOR_POLL_HPD;
	hdmi->conn.interlace_allowed = true;
	hdmi->conn.doublescan_allowed = false;

	ret = drm_connector_attach_encoder(&hdmi->conn,
						bridge->encoder);
	if (ret) {
		dev_err(hdmi->dev,
			"Failed to attach connector to encoder: %d\n", ret);
		return ret;
	}

	if (hdmi->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, hdmi->next_bridge,
					bridge);
		if (ret) {
			dev_err(hdmi->dev,
				"Failed to attach external bridge: %d\n", ret);
			return ret;
		}
	}

	mtk_cec_set_hpd_event(hdmi->cec_dev, mtk_hdmi_hpd_event, hdmi->dev);

	return 0;
}

static bool mtk_hdmi_bridge_mode_fixup(struct drm_bridge *bridge,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mtk_hdmi_bridge_disable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->enabled)
		return;

	phy_power_off(hdmi->phy);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL]);

	hdmi->enabled = false;
}

static void mtk_hdmi_bridge_post_disable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->powered)
		return;

	mtk_hdmi_hw_1p4_version_enable(hdmi, true);
	mtk_hdmi_hw_make_reg_writable(hdmi, false);

	hdmi->powered = false;
}

static void mtk_hdmi_bridge_mode_set(struct drm_bridge *bridge,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	dev_dbg(hdmi->dev, "cur info: name:%s, hdisplay:%d\n",
		adjusted_mode->name, adjusted_mode->hdisplay);
	dev_dbg(hdmi->dev, "hsync_start:%d,hsync_end:%d, htotal:%d",
		adjusted_mode->hsync_start, adjusted_mode->hsync_end,
		adjusted_mode->htotal);
	dev_dbg(hdmi->dev, "hskew:%d, vdisplay:%d\n",
		adjusted_mode->hskew, adjusted_mode->vdisplay);
	dev_dbg(hdmi->dev, "vsync_start:%d, vsync_end:%d, vtotal:%d",
		adjusted_mode->vsync_start, adjusted_mode->vsync_end,
		adjusted_mode->vtotal);
	dev_dbg(hdmi->dev, "vscan:%d, flag:%d\n",
		adjusted_mode->vscan, adjusted_mode->flags);

	drm_mode_copy(&hdmi->mode, adjusted_mode);
}

static void mtk_hdmi_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	mtk_hdmi_hw_make_reg_writable(hdmi, true);
	mtk_hdmi_hw_1p4_version_enable(hdmi, true);

	hdmi->powered = true;
}

static void mtk_hdmi_send_infoframe(struct mtk_hdmi *hdmi,
				    struct drm_display_mode *mode)
{
	mtk_hdmi_setup_audio_infoframe(hdmi);
	mtk_hdmi_setup_avi_infoframe(hdmi, mode);
	mtk_hdmi_setup_spd_infoframe(hdmi, "mediatek", "On-chip HDMI");
	if (mode->flags & DRM_MODE_FLAG_3D_MASK)
		mtk_hdmi_setup_vendor_specific_infoframe(hdmi, mode);
}

static void mtk_hdmi_bridge_enable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	mtk_hdmi_output_set_display_mode(hdmi, &hdmi->mode);
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL]);
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);
	phy_power_on(hdmi->phy);
	mtk_hdmi_send_infoframe(hdmi, &hdmi->mode);

	hdmi->enabled = true;
}

static const struct drm_bridge_funcs mtk_hdmi_bridge_funcs = {
	.attach = mtk_hdmi_bridge_attach,
	.mode_fixup = mtk_hdmi_bridge_mode_fixup,
	.disable = mtk_hdmi_bridge_disable,
	.post_disable = mtk_hdmi_bridge_post_disable,
	.mode_set = mtk_hdmi_bridge_mode_set,
	.pre_enable = mtk_hdmi_bridge_pre_enable,
	.enable = mtk_hdmi_bridge_enable,
};

static int mtk_hdmi_dt_parse_pdata(struct mtk_hdmi *hdmi,
				   struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *cec_np, *remote, *i2c_np;
	struct platform_device *cec_pdev;
	struct regmap *regmap;
	struct resource *mem;
	int ret;

	ret = mtk_hdmi_get_all_clk(hdmi, np);
	if (ret) {
		dev_err(dev, "Failed to get clocks: %d\n", ret);
		return ret;
	}

	/* The CEC module handles HDMI hotplug detection */
	cec_np = of_find_compatible_node(np->parent, NULL,
					 "mediatek,mt8173-cec");
	if (!cec_np) {
		dev_err(dev, "Failed to find CEC node\n");
		return -EINVAL;
	}

	cec_pdev = of_find_device_by_node(cec_np);
	if (!cec_pdev) {
		dev_err(hdmi->dev, "Waiting for CEC device %pOF\n",
			cec_np);
		return -EPROBE_DEFER;
	}
	hdmi->cec_dev = &cec_pdev->dev;

	/*
	 * The mediatek,syscon-hdmi property contains a phandle link to the
	 * MMSYS_CONFIG device and the register offset of the HDMI_SYS_CFG
	 * registers it contains.
	 */
	regmap = syscon_regmap_lookup_by_phandle(np, "mediatek,syscon-hdmi");
	ret = of_property_read_u32_index(np, "mediatek,syscon-hdmi", 1,
					 &hdmi->sys_offset);
	if (IS_ERR(regmap))
		ret = PTR_ERR(regmap);
	if (ret) {
		ret = PTR_ERR(regmap);
		dev_err(dev,
			"Failed to get system configuration registers: %d\n",
			ret);
		return ret;
	}
	hdmi->sys_regmap = regmap;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	remote = of_graph_get_remote_node(np, 1, 0);
	if (!remote)
		return -EINVAL;

	if (!of_device_is_compatible(remote, "hdmi-connector")) {
		hdmi->next_bridge = of_drm_find_bridge(remote);
		if (!hdmi->next_bridge) {
			dev_err(dev, "Waiting for external bridge\n");
			of_node_put(remote);
			return -EPROBE_DEFER;
		}
	}

	i2c_np = of_parse_phandle(remote, "ddc-i2c-bus", 0);
	if (!i2c_np) {
		dev_err(dev, "Failed to find ddc-i2c-bus node in %pOF\n",
			remote);
		of_node_put(remote);
		return -EINVAL;
	}
	of_node_put(remote);

	hdmi->ddc_adpt = of_find_i2c_adapter_by_node(i2c_np);
	if (!hdmi->ddc_adpt) {
		dev_err(dev, "Failed to get ddc i2c adapter by node\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * HDMI audio codec callbacks
 */

static int mtk_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	struct hdmi_audio_param hdmi_params;
	unsigned int chan = params->cea.channels;

	dev_dbg(hdmi->dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
		params->sample_rate, params->sample_width, chan);

	if (!hdmi->bridge.encoder)
		return -ENODEV;

	switch (chan) {
	case 2:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_2_0;
		break;
	case 4:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_4_0;
		break;
	case 6:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_5_1;
		break;
	case 8:
		hdmi_params.aud_input_chan_type = HDMI_AUD_CHAN_TYPE_7_1;
		break;
	default:
		dev_err(hdmi->dev, "channel[%d] not supported!\n", chan);
		return -EINVAL;
	}

	switch (params->sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		break;
	default:
		dev_err(hdmi->dev, "rate[%d] not supported!\n",
			params->sample_rate);
		return -EINVAL;
	}

	switch (daifmt->fmt) {
	case HDMI_I2S:
		hdmi_params.aud_codec = HDMI_AUDIO_CODING_TYPE_PCM;
		hdmi_params.aud_sampe_size = HDMI_AUDIO_SAMPLE_SIZE_16;
		hdmi_params.aud_input_type = HDMI_AUD_INPUT_I2S;
		hdmi_params.aud_i2s_fmt = HDMI_I2S_MODE_I2S_24BIT;
		hdmi_params.aud_mclk = HDMI_AUD_MCLK_128FS;
		break;
	default:
		dev_err(hdmi->dev, "%s: Invalid DAI format %d\n", __func__,
			daifmt->fmt);
		return -EINVAL;
	}

	memcpy(&hdmi_params.codec_params, params,
	       sizeof(hdmi_params.codec_params));

	mtk_hdmi_audio_set_param(hdmi, &hdmi_params);

	return 0;
}

static int mtk_hdmi_audio_startup(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	mtk_hdmi_audio_enable(hdmi);

	return 0;
}

static void mtk_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	mtk_hdmi_audio_disable(hdmi);
}

static int
mtk_hdmi_audio_digital_mute(struct device *dev, void *data, bool enable)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s(%d)\n", __func__, enable);

	if (enable)
		mtk_hdmi_hw_aud_mute(hdmi);
	else
		mtk_hdmi_hw_aud_unmute(hdmi);

	return 0;
}

static int mtk_hdmi_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	memcpy(buf, hdmi->conn.eld, min(sizeof(hdmi->conn.eld), len));

	return 0;
}

static const struct hdmi_codec_ops mtk_hdmi_audio_codec_ops = {
	.hw_params = mtk_hdmi_audio_hw_params,
	.audio_startup = mtk_hdmi_audio_startup,
	.audio_shutdown = mtk_hdmi_audio_shutdown,
	.digital_mute = mtk_hdmi_audio_digital_mute,
	.get_eld = mtk_hdmi_audio_get_eld,
};

static void mtk_hdmi_register_audio_driver(struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &mtk_hdmi_audio_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
	};
	struct platform_device *pdev;

	pdev = platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					     PLATFORM_DEVID_AUTO, &codec_data,
					     sizeof(codec_data));
	if (IS_ERR(pdev))
		return;

	DRM_INFO("%s driver bound to HDMI\n", HDMI_CODEC_DRV_NAME);
}

static int mtk_drm_hdmi_probe(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi;
	struct device *dev = &pdev->dev;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;

	ret = mtk_hdmi_dt_parse_pdata(hdmi, pdev);
	if (ret)
		return ret;

	hdmi->phy = devm_phy_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		dev_err(dev, "Failed to get HDMI PHY: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, hdmi);

	ret = mtk_hdmi_output_init(hdmi);
	if (ret) {
		dev_err(dev, "Failed to initialize hdmi output\n");
		return ret;
	}

	mtk_hdmi_register_audio_driver(dev);

	hdmi->bridge.funcs = &mtk_hdmi_bridge_funcs;
	hdmi->bridge.of_node = pdev->dev.of_node;
	drm_bridge_add(&hdmi->bridge);

	ret = mtk_hdmi_clk_enable_audio(hdmi);
	if (ret) {
		dev_err(dev, "Failed to enable audio clocks: %d\n", ret);
		goto err_bridge_remove;
	}

	dev_dbg(dev, "mediatek hdmi probe success\n");
	return 0;

err_bridge_remove:
	drm_bridge_remove(&hdmi->bridge);
	return ret;
}

static int mtk_drm_hdmi_remove(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi = platform_get_drvdata(pdev);

	drm_bridge_remove(&hdmi->bridge);
	mtk_hdmi_clk_disable_audio(hdmi);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_hdmi_suspend(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_clk_disable_audio(hdmi);
	dev_dbg(dev, "hdmi suspend success!\n");
	return 0;
}

static int mtk_hdmi_resume(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	int ret = 0;

	ret = mtk_hdmi_clk_enable_audio(hdmi);
	if (ret) {
		dev_err(dev, "hdmi resume failed!\n");
		return ret;
	}

	dev_dbg(dev, "hdmi resume success!\n");
	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(mtk_hdmi_pm_ops,
			 mtk_hdmi_suspend, mtk_hdmi_resume);

static const struct of_device_id mtk_drm_hdmi_of_ids[] = {
	{ .compatible = "mediatek,mt8173-hdmi", },
	{}
};

static struct platform_driver mtk_hdmi_driver = {
	.probe = mtk_drm_hdmi_probe,
	.remove = mtk_drm_hdmi_remove,
	.driver = {
		.name = "mediatek-drm-hdmi",
		.of_match_table = mtk_drm_hdmi_of_ids,
		.pm = &mtk_hdmi_pm_ops,
	},
};

static struct platform_driver * const mtk_hdmi_drivers[] = {
	&mtk_hdmi_phy_driver,
	&mtk_hdmi_ddc_driver,
	&mtk_cec_driver,
	&mtk_hdmi_driver,
};

static int __init mtk_hdmitx_init(void)
{
	return platform_register_drivers(mtk_hdmi_drivers,
					 ARRAY_SIZE(mtk_hdmi_drivers));
}

static void __exit mtk_hdmitx_exit(void)
{
	platform_unregister_drivers(mtk_hdmi_drivers,
				    ARRAY_SIZE(mtk_hdmi_drivers));
}

module_init(mtk_hdmitx_init);
module_exit(mtk_hdmitx_exit);

MODULE_AUTHOR("Jie Qiu <jie.qiu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI Driver");
MODULE_LICENSE("GPL v2");
