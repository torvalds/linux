// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <sound/hdmi-codec.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "mtk_cec.h"
#include "mtk_hdmi_common.h"
#include "mtk_hdmi_regs.h"

#define NCTS_BYTES	7

enum mtk_hdmi_clk_id {
	MTK_HDMI_CLK_HDMI_PIXEL,
	MTK_HDMI_CLK_HDMI_PLL,
	MTK_HDMI_CLK_AUD_BCLK,
	MTK_HDMI_CLK_AUD_SPDIF,
	MTK_HDMI_CLK_COUNT
};

static void mtk_hdmi_hw_vid_black(struct mtk_hdmi *hdmi, bool black)
{
	regmap_update_bits(hdmi->regs, VIDEO_CFG_4,
			   VIDEO_SOURCE_SEL, black ? GEN_RGB : NORMAL_PATH);
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
	if (hdmi->conf && hdmi->conf->tz_disabled)
		regmap_update_bits(hdmi->sys_regmap,
				   hdmi->sys_offset + HDMI_SYS_CFG20,
				   0x80008005, enable ? 0x80000005 : 0x8000);
	else
		arm_smccc_smc(MTK_SIP_SET_AUTHORIZED_SECURE_REG, 0x14000904,
			      0x80000000, 0, 0, 0, 0, 0, &res);

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
	regmap_set_bits(hdmi->regs, GRL_AUDIO_CFG, AUDIO_ZERO);
}

static void mtk_hdmi_hw_aud_unmute(struct mtk_hdmi *hdmi)
{
	regmap_clear_bits(hdmi->regs, GRL_AUDIO_CFG, AUDIO_ZERO);
}

static void mtk_hdmi_hw_reset(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   HDMI_RST, HDMI_RST);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   HDMI_RST, 0);
	regmap_clear_bits(hdmi->regs, GRL_CFG3, CFG3_CONTROL_PACKET_DELAY);
	regmap_update_bits(hdmi->sys_regmap, hdmi->sys_offset + HDMI_SYS_CFG1C,
			   ANLG_ON, ANLG_ON);
}

static void mtk_hdmi_hw_enable_notice(struct mtk_hdmi *hdmi, bool enable_notice)
{
	regmap_update_bits(hdmi->regs, GRL_CFG2, CFG2_NOTICE_EN,
			   enable_notice ? CFG2_NOTICE_EN : 0);
}

static void mtk_hdmi_hw_write_int_mask(struct mtk_hdmi *hdmi, u32 int_mask)
{
	regmap_write(hdmi->regs, GRL_INT_MASK, int_mask);
}

static void mtk_hdmi_hw_enable_dvi_mode(struct mtk_hdmi *hdmi, bool enable)
{
	regmap_update_bits(hdmi->regs, GRL_CFG1, CFG1_DVI, enable ? CFG1_DVI : 0);
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

	frame_type = *buffer++;
	frame_ver = *buffer++;
	frame_len = *buffer++;
	checksum = *buffer++;
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
	default:
		dev_err(hdmi->dev, "Unknown infoframe type %d\n", frame_type);
		return;
	}
	regmap_clear_bits(hdmi->regs, ctrl_reg, ctrl_frame_en);
	regmap_write(hdmi->regs, GRL_INFOFRM_TYPE, frame_type);
	regmap_write(hdmi->regs, GRL_INFOFRM_VER, frame_ver);
	regmap_write(hdmi->regs, GRL_INFOFRM_LNG, frame_len);

	regmap_write(hdmi->regs, GRL_IFM_PORT, checksum);
	for (i = 0; i < frame_len; i++)
		regmap_write(hdmi->regs, GRL_IFM_PORT, frame_data[i]);

	regmap_set_bits(hdmi->regs, ctrl_reg, ctrl_frame_en);
}

static void mtk_hdmi_hw_send_aud_packet(struct mtk_hdmi *hdmi, bool enable)
{
	regmap_update_bits(hdmi->regs, GRL_SHIFT_R2,
			   AUDIO_PACKET_OFF, enable ? 0 : AUDIO_PACKET_OFF);
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
	regmap_clear_bits(hdmi->regs, GRL_CFG4, CTRL_AVMUTE);
	usleep_range(2000, 4000);
	regmap_set_bits(hdmi->regs, GRL_CFG4, CTRL_AVMUTE);
}

static void mtk_hdmi_hw_send_av_unmute(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->regs, GRL_CFG4, CFG4_AV_UNMUTE_EN | CFG4_AV_UNMUTE_SET,
			   CFG4_AV_UNMUTE_EN);
	usleep_range(2000, 4000);
	regmap_update_bits(hdmi->regs, GRL_CFG4, CFG4_AV_UNMUTE_EN | CFG4_AV_UNMUTE_SET,
			   CFG4_AV_UNMUTE_SET);
}

static void mtk_hdmi_hw_ncts_enable(struct mtk_hdmi *hdmi, bool on)
{
	regmap_update_bits(hdmi->regs, GRL_CTS_CTRL, CTS_CTRL_SOFT,
			   on ? 0 : CTS_CTRL_SOFT);
}

static void mtk_hdmi_hw_ncts_auto_write_enable(struct mtk_hdmi *hdmi,
					       bool enable)
{
	regmap_update_bits(hdmi->regs, GRL_CTS_CTRL, NCTS_WRI_ANYTIME,
			   enable ? NCTS_WRI_ANYTIME : 0);
}

static void mtk_hdmi_hw_msic_setting(struct mtk_hdmi *hdmi,
				     struct drm_display_mode *mode)
{
	regmap_clear_bits(hdmi->regs, GRL_CFG4, CFG4_MHL_MODE);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE &&
	    mode->clock == 74250 &&
	    mode->vdisplay == 1080)
		regmap_clear_bits(hdmi->regs, GRL_CFG2, CFG2_MHL_DE_SEL);
	else
		regmap_set_bits(hdmi->regs, GRL_CFG2, CFG2_MHL_DE_SEL);
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
	regmap_update_bits(hdmi->regs, GRL_CH_SWAP, 0xff, swap_bit);
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

	regmap_update_bits(hdmi->regs, GRL_AOUT_CFG, AOUT_BNUM_SEL_MASK, val);
}

static void mtk_hdmi_hw_aud_set_i2s_fmt(struct mtk_hdmi *hdmi,
					enum hdmi_aud_i2s_fmt i2s_fmt)
{
	u32 val;

	regmap_read(hdmi->regs, GRL_CFG0, &val);
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
	regmap_write(hdmi->regs, GRL_CFG0, val);
}

static void mtk_hdmi_hw_audio_config(struct mtk_hdmi *hdmi, bool dst)
{
	const u8 mask = HIGH_BIT_RATE | DST_NORMAL_DOUBLE | SACD_DST | DSD_SEL;
	u8 val;

	/* Disable high bitrate, set DST packet normal/double */
	regmap_clear_bits(hdmi->regs, GRL_AOUT_CFG, HIGH_BIT_RATE_PACKET_ALIGN);

	if (dst)
		val = DST_NORMAL_DOUBLE | SACD_DST;
	else
		val = 0;

	regmap_update_bits(hdmi->regs, GRL_AUDIO_CFG, mask, val);
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

	regmap_write(hdmi->regs, GRL_CH_SW0, ch_switch & 0xff);
	regmap_write(hdmi->regs, GRL_CH_SW1, (ch_switch >> 8) & 0xff);
	regmap_write(hdmi->regs, GRL_CH_SW2, (ch_switch >> 16) & 0xff);
	regmap_write(hdmi->regs, GRL_I2S_UV, i2s_uv);
}

static void mtk_hdmi_hw_aud_set_input_type(struct mtk_hdmi *hdmi,
					   enum hdmi_aud_input_type input_type)
{
	u32 val;

	regmap_read(hdmi->regs, GRL_CFG1, &val);
	if (input_type == HDMI_AUD_INPUT_I2S &&
	    (val & CFG1_SPDIF) == CFG1_SPDIF) {
		val &= ~CFG1_SPDIF;
	} else if (input_type == HDMI_AUD_INPUT_SPDIF &&
		(val & CFG1_SPDIF) == 0) {
		val |= CFG1_SPDIF;
	}
	regmap_write(hdmi->regs, GRL_CFG1, val);
}

static void mtk_hdmi_hw_aud_set_channel_status(struct mtk_hdmi *hdmi,
					       u8 *channel_status)
{
	int i;

	for (i = 0; i < 5; i++) {
		regmap_write(hdmi->regs, GRL_I2S_C_STA0 + i * 4, channel_status[i]);
		regmap_write(hdmi->regs, GRL_L_STATUS_0 + i * 4, channel_status[i]);
		regmap_write(hdmi->regs, GRL_R_STATUS_0 + i * 4, channel_status[i]);
	}
	for (; i < 24; i++) {
		regmap_write(hdmi->regs, GRL_L_STATUS_0 + i * 4, 0);
		regmap_write(hdmi->regs, GRL_R_STATUS_0 + i * 4, 0);
	}
}

static void mtk_hdmi_hw_aud_src_reenable(struct mtk_hdmi *hdmi)
{
	u32 val;

	regmap_read(hdmi->regs, GRL_MIX_CTRL, &val);
	if (val & MIX_CTRL_SRC_EN) {
		val &= ~MIX_CTRL_SRC_EN;
		regmap_write(hdmi->regs, GRL_MIX_CTRL, val);
		usleep_range(255, 512);
		val |= MIX_CTRL_SRC_EN;
		regmap_write(hdmi->regs, GRL_MIX_CTRL, val);
	}
}

static void mtk_hdmi_hw_aud_src_disable(struct mtk_hdmi *hdmi)
{
	u32 val;

	regmap_read(hdmi->regs, GRL_MIX_CTRL, &val);
	val &= ~MIX_CTRL_SRC_EN;
	regmap_write(hdmi->regs, GRL_MIX_CTRL, val);
	regmap_write(hdmi->regs, GRL_SHIFT_L1, 0x00);
}

static void mtk_hdmi_hw_aud_set_mclk(struct mtk_hdmi *hdmi,
				     enum hdmi_aud_mclk mclk)
{
	u32 val;

	regmap_read(hdmi->regs, GRL_CFG5, &val);
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
	regmap_write(hdmi->regs, GRL_CFG5, val);
}

static void do_hdmi_hw_aud_set_ncts(struct mtk_hdmi *hdmi, unsigned int n,
				    unsigned int cts)
{
	unsigned char val[NCTS_BYTES];
	int i;

	regmap_write(hdmi->regs, GRL_NCTS, 0);
	regmap_write(hdmi->regs, GRL_NCTS, 0);
	regmap_write(hdmi->regs, GRL_NCTS, 0);
	memset(val, 0, sizeof(val));

	val[0] = (cts >> 24) & 0xff;
	val[1] = (cts >> 16) & 0xff;
	val[2] = (cts >> 8) & 0xff;
	val[3] = cts & 0xff;

	val[4] = (n >> 16) & 0xff;
	val[5] = (n >> 8) & 0xff;
	val[6] = n & 0xff;

	for (i = 0; i < NCTS_BYTES; i++)
		regmap_write(hdmi->regs, GRL_NCTS, val[i]);
}

static void mtk_hdmi_hw_aud_set_ncts(struct mtk_hdmi *hdmi,
				     unsigned int sample_rate,
				     unsigned int clock)
{
	unsigned int n, cts;

	mtk_hdmi_get_ncts(sample_rate, clock, &n, &cts);

	dev_dbg(hdmi->dev, "%s: sample_rate=%u, clock=%d, cts=%u, n=%u\n",
		__func__, sample_rate, clock, n, cts);

	regmap_update_bits(hdmi->regs, DUMMY_304, AUDIO_I2S_NCTS_SEL, AUDIO_I2S_NCTS_SEL_64);
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


static void mtk_hdmi_aud_set_input(struct mtk_hdmi *hdmi)
{
	enum hdmi_aud_channel_type chan_type;
	u8 chan_count;
	bool dst;

	mtk_hdmi_hw_aud_set_channel_swap(hdmi, HDMI_AUD_SWAP_LFE_CC);
	regmap_set_bits(hdmi->regs, GRL_MIX_CTRL, MIX_CTRL_FLAT);

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
}

static int mtk_hdmi_aud_set_src(struct mtk_hdmi *hdmi,
				struct drm_display_mode *display_mode)
{
	unsigned int sample_rate = hdmi->aud_param.codec_params.sample_rate;

	mtk_hdmi_hw_ncts_enable(hdmi, false);
	mtk_hdmi_hw_aud_src_disable(hdmi);
	regmap_clear_bits(hdmi->regs, GRL_CFG2, CFG2_ACLK_INV);

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
	mtk_hdmi_hw_send_aud_packet(hdmi, false);

	mtk_hdmi_aud_set_input(hdmi);
	mtk_hdmi_aud_set_src(hdmi, display_mode);
	mtk_hdmi_hw_aud_set_channel_status(hdmi,
			hdmi->aud_param.codec_params.iec.status);

	usleep_range(50, 100);

	mtk_hdmi_hw_ncts_enable(hdmi, true);
	mtk_hdmi_hw_send_aud_packet(hdmi, true);
	mtk_hdmi_hw_aud_unmute(hdmi);
	return 0;
}

static int mtk_hdmi_setup_avi_infoframe(struct mtk_hdmi *hdmi,
					struct drm_display_mode *mode)
{
	struct hdmi_avi_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	ssize_t err;

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame,
						       hdmi->curr_conn, mode);
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

static int mtk_hdmi_setup_spd_infoframe(struct mtk_hdmi *hdmi)
{
	struct drm_bridge *bridge = &hdmi->bridge;
	struct hdmi_spd_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_SPD_INFOFRAME_SIZE];
	ssize_t err;

	err = hdmi_spd_infoframe_init(&frame, bridge->vendor, bridge->product);
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
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AUDIO_INFOFRAME_SIZE];
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
							  hdmi->curr_conn, mode);
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

static void mtk_hdmi_audio_enable(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_hw_send_aud_packet(hdmi, true);
	hdmi->audio_enable = true;
}

static void mtk_hdmi_audio_disable(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_hw_send_aud_packet(hdmi, false);
	hdmi->audio_enable = false;
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

static int mtk_hdmi_clk_enable_audio(struct mtk_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_AUD_SPDIF]);
	if (ret) {
		clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
		return ret;
	}

	return 0;
}

static void mtk_hdmi_clk_disable_audio(struct mtk_hdmi *hdmi)
{
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_BCLK]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_AUD_SPDIF]);
}

static enum drm_connector_status
mtk_hdmi_update_plugged_status(struct mtk_hdmi *hdmi)
{
	bool connected;

	mutex_lock(&hdmi->update_plugged_status_lock);
	connected = mtk_cec_hpd_high(hdmi->cec_dev);
	if (hdmi->plugged_cb && hdmi->codec_dev)
		hdmi->plugged_cb(hdmi->codec_dev, connected);
	mutex_unlock(&hdmi->update_plugged_status_lock);

	return connected ?
	       connector_status_connected : connector_status_disconnected;
}

static enum drm_connector_status mtk_hdmi_detect(struct mtk_hdmi *hdmi)
{
	return mtk_hdmi_update_plugged_status(hdmi);
}

static enum drm_mode_status
mtk_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	dev_dbg(hdmi->dev, "xres=%d, yres=%d, refresh=%d, intl=%d clock=%d\n",
		mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE), mode->clock * 1000);

	if (hdmi->conf) {
		if (hdmi->conf->cea_modes_only && !drm_match_cea_mode(mode))
			return MODE_BAD;

		if (hdmi->conf->max_mode_clock &&
		    mode->clock > hdmi->conf->max_mode_clock)
			return MODE_CLOCK_HIGH;
	}

	if (mode->clock < 27000)
		return MODE_CLOCK_LOW;
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return drm_mode_validate_size(mode, 0x1fff, 0x1fff);
}

static void mtk_hdmi_hpd_event(bool hpd, struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi && hdmi->bridge.encoder && hdmi->bridge.encoder->dev) {
		static enum drm_connector_status status;

		status = mtk_hdmi_detect(hdmi);
		drm_helper_hpd_irq_event(hdmi->bridge.encoder->dev);
		drm_bridge_hpd_notify(&hdmi->bridge, status);
	}
}

/*
 * Bridge callbacks
 */

static enum drm_connector_status
mtk_hdmi_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	return mtk_hdmi_detect(hdmi);
}

static const struct drm_edid *mtk_hdmi_bridge_edid_read(struct drm_bridge *bridge,
							struct drm_connector *connector)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	const struct drm_edid *drm_edid;

	if (!hdmi->ddc_adpt)
		return NULL;
	drm_edid = drm_edid_read_ddc(connector, hdmi->ddc_adpt);
	if (drm_edid) {
		/*
		 * FIXME: This should use !connector->display_info.has_audio (or
		 * !connector->display_info.is_hdmi) from a path that has read
		 * the EDID and called drm_edid_connector_update().
		 */
		const struct edid *edid = drm_edid_raw(drm_edid);

		hdmi->dvi_mode = !drm_detect_monitor_audio(edid);
	}

	return drm_edid;
}

static int mtk_hdmi_bridge_attach(struct drm_bridge *bridge,
				  struct drm_encoder *encoder,
				  enum drm_bridge_attach_flags flags)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_ERROR("%s: The flag DRM_BRIDGE_ATTACH_NO_CONNECTOR must be supplied\n",
			  __func__);
		return -EINVAL;
	}

	if (hdmi->bridge.next_bridge) {
		ret = drm_bridge_attach(encoder, hdmi->bridge.next_bridge,
					bridge, flags);
		if (ret)
			return ret;
	}

	mtk_cec_set_hpd_event(hdmi->cec_dev, mtk_hdmi_hpd_event, hdmi->dev);

	return 0;
}

static void mtk_hdmi_bridge_atomic_disable(struct drm_bridge *bridge,
					   struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->enabled)
		return;

	phy_power_off(hdmi->phy);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL]);

	hdmi->curr_conn = NULL;

	hdmi->enabled = false;
}

static void mtk_hdmi_bridge_atomic_post_disable(struct drm_bridge *bridge,
						struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->powered)
		return;

	mtk_hdmi_hw_1p4_version_enable(hdmi, true);
	mtk_hdmi_hw_make_reg_writable(hdmi, false);

	hdmi->powered = false;
}

static void mtk_hdmi_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					      struct drm_atomic_state *state)
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
	mtk_hdmi_setup_spd_infoframe(hdmi);
	if (mode->flags & DRM_MODE_FLAG_3D_MASK)
		mtk_hdmi_setup_vendor_specific_infoframe(hdmi, mode);
}

static void mtk_hdmi_bridge_atomic_enable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	/* Retrieve the connector through the atomic state. */
	hdmi->curr_conn = drm_atomic_get_new_connector_for_encoder(state,
								   bridge->encoder);

	mtk_hdmi_output_set_display_mode(hdmi, &hdmi->mode);
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_PLL]);
	clk_prepare_enable(hdmi->clk[MTK_HDMI_CLK_HDMI_PIXEL]);
	phy_power_on(hdmi->phy);
	mtk_hdmi_send_infoframe(hdmi, &hdmi->mode);

	hdmi->enabled = true;
}

static const struct drm_bridge_funcs mtk_hdmi_bridge_funcs = {
	.mode_valid = mtk_hdmi_bridge_mode_valid,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.attach = mtk_hdmi_bridge_attach,
	.mode_fixup = mtk_hdmi_bridge_mode_fixup,
	.atomic_disable = mtk_hdmi_bridge_atomic_disable,
	.atomic_post_disable = mtk_hdmi_bridge_atomic_post_disable,
	.mode_set = mtk_hdmi_bridge_mode_set,
	.atomic_pre_enable = mtk_hdmi_bridge_atomic_pre_enable,
	.atomic_enable = mtk_hdmi_bridge_atomic_enable,
	.detect = mtk_hdmi_bridge_detect,
	.edid_read = mtk_hdmi_bridge_edid_read,
};

/*
 * HDMI audio codec callbacks
 */

static int mtk_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi->audio_enable) {
		dev_err(hdmi->dev, "hdmi audio is in disable state!\n");
		return -EINVAL;
	}

	mtk_hdmi_audio_params(hdmi, daifmt, params);
	mtk_hdmi_aud_output_config(hdmi, &hdmi->mode);

	return 0;
}

static int mtk_hdmi_audio_startup(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_audio_enable(hdmi);

	return 0;
}

static void mtk_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_audio_disable(hdmi);
}

static int
mtk_hdmi_audio_mute(struct device *dev, void *data,
		    bool enable, int direction)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (enable)
		mtk_hdmi_hw_aud_mute(hdmi);
	else
		mtk_hdmi_hw_aud_unmute(hdmi);

	return 0;
}

static int mtk_hdmi_audio_hook_plugged_cb(struct device *dev, void *data,
					  hdmi_codec_plugged_cb fn,
					  struct device *codec_dev)
{
	struct mtk_hdmi *hdmi = data;

	mtk_hdmi_audio_set_plugged_cb(hdmi, fn, codec_dev);
	mtk_hdmi_update_plugged_status(hdmi);

	return 0;
}

static const struct hdmi_codec_ops mtk_hdmi_audio_codec_ops = {
	.hw_params = mtk_hdmi_audio_hw_params,
	.audio_startup = mtk_hdmi_audio_startup,
	.audio_shutdown = mtk_hdmi_audio_shutdown,
	.mute_stream = mtk_hdmi_audio_mute,
	.get_eld = mtk_hdmi_audio_get_eld,
	.hook_plugged_cb = mtk_hdmi_audio_hook_plugged_cb,
};

static int mtk_hdmi_probe(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi;
	int ret;

	hdmi = mtk_hdmi_common_probe(pdev);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	if (!hdmi->cec_dev)
		return dev_err_probe(hdmi->dev, -ENODEV, "CEC is required by HDMIv1\n");

	ret = mtk_hdmi_clk_enable_audio(hdmi);
	if (ret)
		return dev_err_probe(hdmi->dev, ret,
				     "Failed to enable audio clocks\n");

	return 0;
}

static void mtk_hdmi_remove(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi = platform_get_drvdata(pdev);

	mtk_hdmi_clk_disable_audio(hdmi);
}

static __maybe_unused int mtk_hdmi_suspend(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_clk_disable_audio(hdmi);

	return 0;
}

static __maybe_unused int mtk_hdmi_resume(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	return mtk_hdmi_clk_enable_audio(hdmi);
}

static SIMPLE_DEV_PM_OPS(mtk_hdmi_pm_ops, mtk_hdmi_suspend, mtk_hdmi_resume);

static const struct mtk_hdmi_ver_conf mtk_hdmi_v1_ver_conf = {
	.bridge_funcs = &mtk_hdmi_bridge_funcs,
	.codec_ops = &mtk_hdmi_audio_codec_ops,
	.mtk_hdmi_clock_names = mtk_hdmi_clk_names,
	.num_clocks = ARRAY_SIZE(mtk_hdmi_clk_names)
};

static const struct mtk_hdmi_conf mtk_hdmi_conf_mt2701 = {
	.tz_disabled = true,
	.ver_conf = &mtk_hdmi_v1_ver_conf
};

static const struct mtk_hdmi_conf mtk_hdmi_conf_mt8167 = {
	.cea_modes_only = true,
	.max_mode_clock = 148500,
	.ver_conf = &mtk_hdmi_v1_ver_conf
};

static const struct mtk_hdmi_conf mtk_hdmi_conf_mt8173 = {
	.ver_conf = &mtk_hdmi_v1_ver_conf
};

static const struct of_device_id mtk_hdmi_of_ids[] = {
	{ .compatible = "mediatek,mt2701-hdmi", .data = &mtk_hdmi_conf_mt2701 },
	{ .compatible = "mediatek,mt8167-hdmi", .data = &mtk_hdmi_conf_mt8167 },
	{ .compatible = "mediatek,mt8173-hdmi", .data = &mtk_hdmi_conf_mt8173 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_hdmi_of_ids);

static struct platform_driver mtk_hdmi_driver = {
	.probe = mtk_hdmi_probe,
	.remove = mtk_hdmi_remove,
	.driver = {
		.name = "mediatek-drm-hdmi",
		.of_match_table = mtk_hdmi_of_ids,
		.pm = &mtk_hdmi_pm_ops,
	},
};
module_platform_driver(mtk_hdmi_driver);

MODULE_AUTHOR("Jie Qiu <jie.qiu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek HDMI Driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("DRM_MTK_HDMI_V1");
MODULE_IMPORT_NS("DRM_MTK_HDMI");
