// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek HDMI v2 IP driver
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Copyright (c) 2022 BayLibre, SAS
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/units.h>
#include <linux/phy/phy.h>

#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>
#include <drm/display/drm_scdc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "mtk_hdmi_common.h"
#include "mtk_hdmi_regs_v2.h"

#define MTK_HDMI_V2_CLOCK_MIN	27000
#define MTK_HDMI_V2_CLOCK_MAX	594000

#define HPD_PORD_HWIRQS		(HTPLG_R_INT | HTPLG_F_INT | PORD_F_INT | PORD_R_INT)

enum mtk_hdmi_v2_clk_id {
	MTK_HDMI_V2_CLK_HDCP_SEL,
	MTK_HDMI_V2_CLK_HDCP_24M_SEL,
	MTK_HDMI_V2_CLK_VPP_SPLIT_HDMI,
	MTK_HDMI_V2_CLK_HDMI_APB_SEL,
	MTK_HDMI_V2_CLK_COUNT,
};

const char *const mtk_hdmi_v2_clk_names[MTK_HDMI_V2_CLK_COUNT] = {
	[MTK_HDMI_V2_CLK_HDMI_APB_SEL] = "bus",
	[MTK_HDMI_V2_CLK_HDCP_SEL] = "hdcp",
	[MTK_HDMI_V2_CLK_HDCP_24M_SEL] = "hdcp24m",
	[MTK_HDMI_V2_CLK_VPP_SPLIT_HDMI] = "hdmi-split",
};

static inline void mtk_hdmi_v2_hwirq_disable(struct mtk_hdmi *hdmi)
{
	regmap_write(hdmi->regs, TOP_INT_ENABLE00, 0);
	regmap_write(hdmi->regs, TOP_INT_ENABLE01, 0);
}

static inline void mtk_hdmi_v2_enable_hpd_pord_irq(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable)
		regmap_set_bits(hdmi->regs, TOP_INT_ENABLE00, HPD_PORD_HWIRQS);
	else
		regmap_clear_bits(hdmi->regs, TOP_INT_ENABLE00, HPD_PORD_HWIRQS);
}

static inline void mtk_hdmi_v2_set_sw_hpd(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable) {
		regmap_set_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMITX_SW_HPD);
		regmap_set_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_OVR);
		regmap_set_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_SW);
	} else {
		regmap_clear_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_OVR);
		regmap_clear_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_SW);
		regmap_clear_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMITX_SW_HPD);
	}
}

static inline void mtk_hdmi_v2_enable_scrambling(struct mtk_hdmi *hdmi, bool enable)
{
	struct drm_scdc *scdc = &hdmi->curr_conn->display_info.hdmi.scdc;

	if (enable)
		regmap_set_bits(hdmi->regs, TOP_CFG00, SCR_ON | HDMI2_ON);
	else
		regmap_clear_bits(hdmi->regs, TOP_CFG00, SCR_ON | HDMI2_ON);

	if (scdc->supported) {
		if (scdc->scrambling.supported)
			drm_scdc_set_scrambling(hdmi->curr_conn, enable);
		drm_scdc_set_high_tmds_clock_ratio(hdmi->curr_conn, enable);
	}
}

static void mtk_hdmi_v2_hw_vid_mute(struct mtk_hdmi *hdmi, bool enable)
{
	/* If enabled, sends a black image */
	if (enable)
		regmap_set_bits(hdmi->regs, TOP_VMUTE_CFG1, REG_VMUTE_EN);
	else
		regmap_clear_bits(hdmi->regs, TOP_VMUTE_CFG1, REG_VMUTE_EN);
}

static void mtk_hdmi_v2_hw_aud_mute(struct mtk_hdmi *hdmi, bool enable)
{
	u32 aip, val;

	if (!enable) {
		regmap_clear_bits(hdmi->regs, AIP_TXCTRL, AUD_MUTE_FIFO_EN);
		return;
	}

	regmap_read(hdmi->regs, AIP_CTRL, &aip);

	val = AUD_MUTE_FIFO_EN;
	if (aip & DSD_EN)
		val |= DSD_MUTE_EN;

	regmap_update_bits(hdmi->regs, AIP_TXCTRL, val, val);
}

static void mtk_hdmi_v2_hw_reset(struct mtk_hdmi *hdmi)
{
	regmap_clear_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMITX_SW_RSTB);
	udelay(5);
	regmap_set_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMITX_SW_RSTB);
}

static inline u32 mtk_hdmi_v2_format_hw_packet(const u8 *buffer, u8 len)
{
	unsigned short i;
	u32 val = 0;

	for (i = 0; i < len; i++)
		val |= buffer[i] << (i * 8);

	return val;
}

static int mtk_hdmi_v2_hdmi_write_audio_infoframe(struct drm_bridge *bridge,
						  const u8 *buffer, size_t len)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, AUD_EN | AUD_EN_WR);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, AUD_RPT_EN);

	regmap_write(hdmi->regs, TOP_AIF_HEADER, mtk_hdmi_v2_format_hw_packet(&buffer[0], 3));
	regmap_write(hdmi->regs, TOP_AIF_PKT00, mtk_hdmi_v2_format_hw_packet(&buffer[3], 3));
	regmap_write(hdmi->regs, TOP_AIF_PKT01, mtk_hdmi_v2_format_hw_packet(&buffer[7], 2));
	regmap_write(hdmi->regs, TOP_AIF_PKT02, 0);
	regmap_write(hdmi->regs, TOP_AIF_PKT03, 0);

	regmap_set_bits(hdmi->regs, TOP_INFO_RPT, AUD_RPT_EN);
	regmap_set_bits(hdmi->regs, TOP_INFO_EN, AUD_EN | AUD_EN_WR);

	return 0;
}

static int mtk_hdmi_v2_hdmi_write_avi_infoframe(struct drm_bridge *bridge,
						const u8 *buffer, size_t len)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, AVI_EN_WR | AVI_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, AVI_RPT_EN);

	regmap_write(hdmi->regs, TOP_AVI_HEADER, mtk_hdmi_v2_format_hw_packet(&buffer[0], 3));
	regmap_write(hdmi->regs, TOP_AVI_PKT00, mtk_hdmi_v2_format_hw_packet(&buffer[3], 4));
	regmap_write(hdmi->regs, TOP_AVI_PKT01, mtk_hdmi_v2_format_hw_packet(&buffer[7], 3));
	regmap_write(hdmi->regs, TOP_AVI_PKT02, mtk_hdmi_v2_format_hw_packet(&buffer[10], 4));
	regmap_write(hdmi->regs, TOP_AVI_PKT03, mtk_hdmi_v2_format_hw_packet(&buffer[14], 3));
	regmap_write(hdmi->regs, TOP_AVI_PKT04, 0);
	regmap_write(hdmi->regs, TOP_AVI_PKT05, 0);

	regmap_set_bits(hdmi->regs, TOP_INFO_RPT, AVI_RPT_EN);
	regmap_set_bits(hdmi->regs, TOP_INFO_EN, AVI_EN_WR | AVI_EN);

	return 0;
}

static int mtk_hdmi_v2_hdmi_write_spd_infoframe(struct drm_bridge *bridge,
						const u8 *buffer, size_t len)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, SPD_EN_WR | SPD_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, SPD_RPT_EN);

	regmap_write(hdmi->regs, TOP_SPDIF_HEADER, mtk_hdmi_v2_format_hw_packet(&buffer[0], 3));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT00, mtk_hdmi_v2_format_hw_packet(&buffer[3], 4));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT01, mtk_hdmi_v2_format_hw_packet(&buffer[7], 3));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT02, mtk_hdmi_v2_format_hw_packet(&buffer[10], 4));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT03, mtk_hdmi_v2_format_hw_packet(&buffer[14], 3));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT04, mtk_hdmi_v2_format_hw_packet(&buffer[17], 4));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT05, mtk_hdmi_v2_format_hw_packet(&buffer[21], 3));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT06, mtk_hdmi_v2_format_hw_packet(&buffer[24], 4));
	regmap_write(hdmi->regs, TOP_SPDIF_PKT07, buffer[28]);

	regmap_set_bits(hdmi->regs, TOP_INFO_EN, SPD_EN_WR | SPD_EN);
	regmap_set_bits(hdmi->regs, TOP_INFO_RPT, SPD_RPT_EN);

	return 0;
}

static int mtk_hdmi_v2_hdmi_write_hdmi_infoframe(struct drm_bridge *bridge,
						 const u8 *buffer, size_t len)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, VSIF_EN_WR | VSIF_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, VSIF_RPT_EN);

	regmap_write(hdmi->regs, TOP_VSIF_HEADER, mtk_hdmi_v2_format_hw_packet(&buffer[0], 3));
	regmap_write(hdmi->regs, TOP_VSIF_PKT00, mtk_hdmi_v2_format_hw_packet(&buffer[3], 4));
	regmap_write(hdmi->regs, TOP_VSIF_PKT01, mtk_hdmi_v2_format_hw_packet(&buffer[7], 2));
	regmap_write(hdmi->regs, TOP_VSIF_PKT02, 0);
	regmap_write(hdmi->regs, TOP_VSIF_PKT03, 0);
	regmap_write(hdmi->regs, TOP_VSIF_PKT04, 0);
	regmap_write(hdmi->regs, TOP_VSIF_PKT05, 0);
	regmap_write(hdmi->regs, TOP_VSIF_PKT06, 0);
	regmap_write(hdmi->regs, TOP_VSIF_PKT07, 0);

	regmap_set_bits(hdmi->regs, TOP_INFO_EN, VSIF_EN_WR | VSIF_EN);
	regmap_set_bits(hdmi->regs, TOP_INFO_RPT, VSIF_RPT_EN);

	return 0;
}

static void mtk_hdmi_yuv420_downsampling(struct mtk_hdmi *hdmi, bool enable)
{
	u32 val;

	regmap_read(hdmi->regs, VID_DOWNSAMPLE_CONFIG, &val);

	if (enable) {
		regmap_set_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMI_YUV420_MODE);

		val |= C444_C422_CONFIG_ENABLE | C422_C420_CONFIG_ENABLE;
		val |= C422_C420_CONFIG_OUT_CB_OR_CR;
		val &= ~C422_C420_CONFIG_BYPASS;
		regmap_write(hdmi->regs, VID_DOWNSAMPLE_CONFIG, val);

		regmap_set_bits(hdmi->regs, VID_OUT_FORMAT, OUTPUT_FORMAT_DEMUX_420_ENABLE);
	} else {
		regmap_clear_bits(hdmi->regs, hdmi->conf->reg_hdmi_tx_cfg, HDMI_YUV420_MODE);

		val &= ~(C444_C422_CONFIG_ENABLE | C422_C420_CONFIG_ENABLE);
		val &= ~C422_C420_CONFIG_OUT_CB_OR_CR;
		val |= C422_C420_CONFIG_BYPASS;
		regmap_write(hdmi->regs, VID_DOWNSAMPLE_CONFIG, val);

		regmap_clear_bits(hdmi->regs, VID_OUT_FORMAT, OUTPUT_FORMAT_DEMUX_420_ENABLE);
	}
}

static int mtk_hdmi_v2_setup_audio_infoframe(struct mtk_hdmi *hdmi)
{
	struct hdmi_codec_params *params = &hdmi->aud_param.codec_params;
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t ret;

	memcpy(&frame, &params->cea, sizeof(frame));

	ret = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0)
		return ret;

	mtk_hdmi_v2_hdmi_write_audio_infoframe(&hdmi->bridge, buffer, sizeof(buffer));

	return 0;
}

static inline void mtk_hdmi_v2_hw_gcp_avmute(struct mtk_hdmi *hdmi, bool mute)
{
	u32 val;

	regmap_read(hdmi->regs, TOP_CFG01, &val);
	val &= ~(CP_CLR_MUTE_EN | CP_SET_MUTE_EN);

	if (mute) {
		val |= CP_SET_MUTE_EN;
		val &= ~CP_CLR_MUTE_EN;
	} else {
		val |= CP_CLR_MUTE_EN;
		val &= ~CP_SET_MUTE_EN;
	}
	regmap_write(hdmi->regs, TOP_CFG01, val);

	regmap_set_bits(hdmi->regs, TOP_INFO_RPT, CP_RPT_EN);
	regmap_set_bits(hdmi->regs, TOP_INFO_EN, CP_EN | CP_EN_WR);
}

static void mtk_hdmi_v2_hw_ncts_enable(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable)
		regmap_set_bits(hdmi->regs, AIP_CTRL, CTS_SW_SEL);
	else
		regmap_clear_bits(hdmi->regs, AIP_CTRL, CTS_SW_SEL);
}

static void mtk_hdmi_v2_hw_aud_set_channel_status(struct mtk_hdmi *hdmi)
{
	u8 *ch_status = hdmi->aud_param.codec_params.iec.status;

	/* Only the first 5 to 7 bytes of Channel Status contain useful information */
	regmap_write(hdmi->regs, AIP_I2S_CHST0, mtk_hdmi_v2_format_hw_packet(&ch_status[0], 4));
	regmap_write(hdmi->regs, AIP_I2S_CHST1, mtk_hdmi_v2_format_hw_packet(&ch_status[4], 3));
}

static void mtk_hdmi_v2_hw_aud_set_ncts(struct mtk_hdmi *hdmi,
				     unsigned int sample_rate,
				     unsigned int clock)
{
	unsigned int n, cts;

	mtk_hdmi_get_ncts(sample_rate, clock, &n, &cts);

	regmap_write(hdmi->regs, AIP_N_VAL, n);
	regmap_write(hdmi->regs, AIP_CTS_SVAL, cts);
}

static void mtk_hdmi_v2_hw_aud_enable(struct mtk_hdmi *hdmi, bool enable)
{
	if (enable)
		regmap_clear_bits(hdmi->regs, AIP_TXCTRL, AUD_PACKET_DROP);
	else
		regmap_set_bits(hdmi->regs, AIP_TXCTRL, AUD_PACKET_DROP);
}

static u32 mtk_hdmi_v2_aud_output_channel_map(u8 sd0, u8 sd1, u8 sd2, u8 sd3,
					      u8 sd4, u8 sd5, u8 sd6, u8 sd7)
{
	u32 val;

	/*
	 * Each of the Output Channels (0-7) can be mapped to get their input
	 * from any of the available Input Channels (0-7): this function
	 * takes input channel numbers and formats a value that must then
	 * be written to the TOP_AUD_MAP hardware register by the caller.
	 */
	val = FIELD_PREP(SD0_MAP, sd0) | FIELD_PREP(SD1_MAP, sd1);
	val |= FIELD_PREP(SD2_MAP, sd2) | FIELD_PREP(SD3_MAP, sd3);
	val |= FIELD_PREP(SD4_MAP, sd4) | FIELD_PREP(SD5_MAP, sd5);
	val |= FIELD_PREP(SD6_MAP, sd6) | FIELD_PREP(SD7_MAP, sd7);

	return val;
}

static void mtk_hdmi_audio_dsd_config(struct mtk_hdmi *hdmi,
				      unsigned char chnum, bool dsd_bypass)
{
	u32 channel_map;

	regmap_update_bits(hdmi->regs, AIP_CTRL, SPDIF_EN | DSD_EN | HBRA_ON, DSD_EN);
	regmap_set_bits(hdmi->regs, AIP_TXCTRL, DSD_MUTE_EN);

	if (dsd_bypass)
		channel_map = mtk_hdmi_v2_aud_output_channel_map(0, 2, 4, 6, 1, 3, 5, 7);
	else
		channel_map = mtk_hdmi_v2_aud_output_channel_map(0, 5, 1, 0, 3, 2, 4, 0);

	regmap_write(hdmi->regs, TOP_AUD_MAP, channel_map);
	regmap_clear_bits(hdmi->regs, AIP_SPDIF_CTRL, I2S2DSD_EN);
}

static inline void mtk_hdmi_v2_hw_i2s_fifo_map(struct mtk_hdmi *hdmi, u32 fifo_mapping)
{
	regmap_update_bits(hdmi->regs, AIP_I2S_CTRL,
			   FIFO0_MAP | FIFO1_MAP | FIFO2_MAP | FIFO3_MAP, fifo_mapping);
}

static inline void mtk_hdmi_v2_hw_i2s_ch_number(struct mtk_hdmi *hdmi, u8 chnum)
{
	regmap_update_bits(hdmi->regs, AIP_CTRL, I2S_EN, FIELD_PREP(I2S_EN, chnum));
}

static void mtk_hdmi_v2_hw_i2s_ch_mapping(struct mtk_hdmi *hdmi, u8 chnum, u8 mapping)
{
	u32 fifo_map;
	u8 bdata;

	switch (chnum) {
	default:
	case 2:
		bdata = 0x1;
		break;
	case 3:
		bdata = 0x3;
		break;
	case 6:
		if (mapping == 0x0e) {
			bdata = 0xf;
			break;
		}
		fallthrough;
	case 5:
		bdata = 0x7;
		break;
	case 7:
	case 8:
		bdata = 0xf;
		break;
	}

	/* Assign default FIFO mapping: SD0 to FIFO0, SD1 to FIFO1, etc. */
	fifo_map = FIELD_PREP(FIFO0_MAP, 0) | FIELD_PREP(FIFO1_MAP, 1);
	fifo_map |= FIELD_PREP(FIFO2_MAP, 2) | FIELD_PREP(FIFO3_MAP, 3);
	mtk_hdmi_v2_hw_i2s_fifo_map(hdmi, fifo_map);
	mtk_hdmi_v2_hw_i2s_ch_number(hdmi, bdata);

	/*
	 * Set HDMI Audio packet layout indicator:
	 * Layout 0 is for two channels
	 * Layout 1 is for up to eight channels
	 */
	if (chnum == 2)
		regmap_set_bits(hdmi->regs, AIP_TXCTRL, AUD_LAYOUT_1);
	else
		regmap_clear_bits(hdmi->regs, AIP_TXCTRL, AUD_LAYOUT_1);
}

static void mtk_hdmi_i2s_data_fmt(struct mtk_hdmi *hdmi, unsigned char fmt)
{
	u32 val;

	regmap_read(hdmi->regs, AIP_I2S_CTRL, &val);
	val &= ~(WS_HIGH | I2S_1ST_BIT_NOSHIFT | JUSTIFY_RIGHT);

	switch (fmt) {
	case HDMI_I2S_MODE_RJT_24BIT:
	case HDMI_I2S_MODE_RJT_16BIT:
		val |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT | JUSTIFY_RIGHT);
		break;
	case HDMI_I2S_MODE_LJT_24BIT:
	case HDMI_I2S_MODE_LJT_16BIT:
		val |= (WS_HIGH | I2S_1ST_BIT_NOSHIFT);
		break;
	case HDMI_I2S_MODE_I2S_24BIT:
	case HDMI_I2S_MODE_I2S_16BIT:
	default:
		break;
	}

	regmap_write(hdmi->regs, AIP_I2S_CTRL, val);
}

static inline void mtk_hdmi_i2s_sck_edge_rise(struct mtk_hdmi *hdmi, bool rise)
{
	if (rise)
		regmap_set_bits(hdmi->regs, AIP_I2S_CTRL, SCK_EDGE_RISE);
	else
		regmap_clear_bits(hdmi->regs, AIP_I2S_CTRL, SCK_EDGE_RISE);
}

static inline void mtk_hdmi_i2s_cbit_order(struct mtk_hdmi *hdmi, unsigned int cbit)
{
	regmap_update_bits(hdmi->regs, AIP_I2S_CTRL, CBIT_ORDER_SAME, cbit);
}

static inline void mtk_hdmi_i2s_vbit(struct mtk_hdmi *hdmi, unsigned int vbit)
{
	/* V bit: 0 for PCM, 1 for Compressed data */
	regmap_update_bits(hdmi->regs, AIP_I2S_CTRL, VBIT_COMPRESSED, vbit);
}

static inline void mtk_hdmi_i2s_data_direction(struct mtk_hdmi *hdmi, unsigned int is_lsb)
{
	regmap_update_bits(hdmi->regs, AIP_I2S_CTRL, I2S_DATA_DIR_LSB, is_lsb);
}

static inline void mtk_hdmi_v2_hw_audio_type(struct mtk_hdmi *hdmi, unsigned int spdif_i2s)
{
	regmap_update_bits(hdmi->regs, AIP_CTRL, SPDIF_EN, FIELD_PREP(SPDIF_EN, spdif_i2s));
}

static u8 mtk_hdmi_v2_get_i2s_ch_mapping(struct mtk_hdmi *hdmi, u8 channel_type)
{
	switch (channel_type) {
	case HDMI_AUD_CHAN_TYPE_1_1:
	case HDMI_AUD_CHAN_TYPE_2_1:
		return 0x01;
	case HDMI_AUD_CHAN_TYPE_3_0:
		return 0x02;
	case HDMI_AUD_CHAN_TYPE_3_1:
		return 0x03;
	case HDMI_AUD_CHAN_TYPE_3_0_LRS:
	case HDMI_AUD_CHAN_TYPE_4_0:
		return 0x08;
	case HDMI_AUD_CHAN_TYPE_5_1:
		return 0x0b;
	case HDMI_AUD_CHAN_TYPE_4_1_CLRS:
	case HDMI_AUD_CHAN_TYPE_6_0:
	case HDMI_AUD_CHAN_TYPE_6_0_CS:
	case HDMI_AUD_CHAN_TYPE_6_0_CH:
	case HDMI_AUD_CHAN_TYPE_6_0_OH:
	case HDMI_AUD_CHAN_TYPE_6_0_CHR:
		return 0x0e;
	case HDMI_AUD_CHAN_TYPE_1_0:
	case HDMI_AUD_CHAN_TYPE_2_0:
	case HDMI_AUD_CHAN_TYPE_3_1_LRS:
	case HDMI_AUD_CHAN_TYPE_4_1:
	case HDMI_AUD_CHAN_TYPE_5_0:
	case HDMI_AUD_CHAN_TYPE_4_0_CLRS:
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
	default:
		return 0;
	}

	return 0;
}

static inline void mtk_hdmi_v2_hw_i2s_ch_swap(struct mtk_hdmi *hdmi)
{
	regmap_update_bits(hdmi->regs, AIP_SPDIF_CTRL, MAX_2UI_I2S_HI_WRITE,
			   FIELD_PREP(MAX_2UI_I2S_HI_WRITE, MAX_2UI_I2S_LFE_CC_SWAP));
}

static void mtk_hdmi_hbr_config(struct mtk_hdmi *hdmi, bool dsd_bypass)
{
	const u32 hbr_mask = SPDIF_EN | DSD_EN | HBRA_ON;

	if (dsd_bypass) {
		regmap_update_bits(hdmi->regs, AIP_CTRL, hbr_mask, HBRA_ON);
		regmap_set_bits(hdmi->regs, AIP_CTRL, I2S_EN);
	} else {
		regmap_update_bits(hdmi->regs, AIP_CTRL, hbr_mask, SPDIF_EN);
		regmap_set_bits(hdmi->regs, AIP_CTRL, SPDIF_INTERNAL_MODULE);
		regmap_set_bits(hdmi->regs, AIP_CTRL, HBR_FROM_SPDIF);
		regmap_set_bits(hdmi->regs, AIP_CTRL, CTS_CAL_N4);
	}
}

static inline void mtk_hdmi_v2_hw_spdif_config(struct mtk_hdmi *hdmi)
{
	regmap_clear_bits(hdmi->regs, AIP_SPDIF_CTRL, WR_1UI_LOCK);
	regmap_clear_bits(hdmi->regs, AIP_SPDIF_CTRL, FS_OVERRIDE_WRITE);
	regmap_clear_bits(hdmi->regs, AIP_SPDIF_CTRL, WR_2UI_LOCK);

	regmap_update_bits(hdmi->regs, AIP_SPDIF_CTRL, MAX_1UI_WRITE,
			   FIELD_PREP(MAX_1UI_WRITE, 4));
	regmap_update_bits(hdmi->regs, AIP_SPDIF_CTRL, MAX_2UI_SPDIF_WRITE,
			   FIELD_PREP(MAX_2UI_SPDIF_WRITE, 9));
	regmap_update_bits(hdmi->regs, AIP_SPDIF_CTRL, AUD_ERR_THRESH,
			   FIELD_PREP(AUD_ERR_THRESH, 4));

	regmap_set_bits(hdmi->regs, AIP_SPDIF_CTRL, I2S2DSD_EN);
}

static void mtk_hdmi_v2_aud_set_input(struct mtk_hdmi *hdmi)
{
	struct hdmi_audio_param *aud_param = &hdmi->aud_param;
	struct hdmi_codec_params *codec_params = &aud_param->codec_params;
	u8 i2s_ch_map;
	u32 out_ch_map;

	/* Write the default output channel map. CH0 maps to SD0, CH1 maps to SD1, etc */
	out_ch_map = mtk_hdmi_v2_aud_output_channel_map(0, 1, 2, 3, 4, 5, 6, 7);
	regmap_write(hdmi->regs, TOP_AUD_MAP, out_ch_map);

	regmap_update_bits(hdmi->regs, AIP_SPDIF_CTRL, MAX_2UI_I2S_HI_WRITE, 0);
	regmap_clear_bits(hdmi->regs, AIP_CTRL,
			  SPDIF_EN | DSD_EN | HBRA_ON | CTS_CAL_N4 |
			  HBR_FROM_SPDIF | SPDIF_INTERNAL_MODULE);
	regmap_clear_bits(hdmi->regs, AIP_TXCTRL, DSD_MUTE_EN | AUD_LAYOUT_1);

	if (aud_param->aud_input_type == HDMI_AUD_INPUT_I2S) {
		switch (aud_param->aud_codec) {
		case HDMI_AUDIO_CODING_TYPE_DTS_HD:
		case HDMI_AUDIO_CODING_TYPE_MLP:
			mtk_hdmi_i2s_data_fmt(hdmi, aud_param->aud_i2s_fmt);
			mtk_hdmi_hbr_config(hdmi, true);
			break;
		case HDMI_AUDIO_CODING_TYPE_DSD:
			mtk_hdmi_audio_dsd_config(hdmi, codec_params->channels, 0);
			mtk_hdmi_v2_hw_i2s_ch_mapping(hdmi, codec_params->channels, 1);
			break;
		default:
			mtk_hdmi_i2s_data_fmt(hdmi, aud_param->aud_i2s_fmt);
			mtk_hdmi_i2s_sck_edge_rise(hdmi, true);
			mtk_hdmi_i2s_cbit_order(hdmi, CBIT_ORDER_SAME);
			mtk_hdmi_i2s_vbit(hdmi, 0); /* PCM data */
			mtk_hdmi_i2s_data_direction(hdmi, 0); /* MSB first */
			mtk_hdmi_v2_hw_audio_type(hdmi, HDMI_AUD_INPUT_I2S);
			i2s_ch_map = mtk_hdmi_v2_get_i2s_ch_mapping(hdmi,
						aud_param->aud_input_chan_type);
			mtk_hdmi_v2_hw_i2s_ch_mapping(hdmi, codec_params->channels, i2s_ch_map);
			mtk_hdmi_v2_hw_i2s_ch_swap(hdmi);
		}
	} else {
		if (codec_params->sample_rate == 768000 &&
		    (aud_param->aud_codec == HDMI_AUDIO_CODING_TYPE_DTS_HD ||
		     aud_param->aud_codec == HDMI_AUDIO_CODING_TYPE_MLP)) {
			mtk_hdmi_hbr_config(hdmi, false);
		} else {
			mtk_hdmi_v2_hw_spdif_config(hdmi);
			mtk_hdmi_v2_hw_i2s_ch_mapping(hdmi, 2, 0);
		}
	}
}

static inline void mtk_hdmi_v2_hw_audio_input_enable(struct mtk_hdmi *hdmi, bool ena)
{
	if (ena)
		regmap_set_bits(hdmi->regs, AIP_CTRL, AUD_IN_EN);
	else
		regmap_clear_bits(hdmi->regs, AIP_CTRL, AUD_IN_EN);
}

static void mtk_hdmi_v2_aip_ctrl_init(struct mtk_hdmi *hdmi)
{
	regmap_set_bits(hdmi->regs, AIP_CTRL,
			AUD_SEL_OWRT | NO_MCLK_CTSGEN_SEL | MCLK_EN | CTS_REQ_EN);
	regmap_clear_bits(hdmi->regs, AIP_TPI_CTRL, TPI_AUDIO_LOOKUP_EN);
}

static void mtk_hdmi_v2_audio_reset(struct mtk_hdmi *hdmi, bool reset)
{
	const u32 arst_bits = RST4AUDIO | RST4AUDIO_FIFO | RST4AUDIO_ACR;

	if (reset)
		regmap_set_bits(hdmi->regs, AIP_TXCTRL, arst_bits);
	else
		regmap_clear_bits(hdmi->regs, AIP_TXCTRL, arst_bits);
}

static void mtk_hdmi_v2_aud_output_config(struct mtk_hdmi *hdmi,
					  struct drm_display_mode *display_mode)
{
	/* Shut down and reset the HDMI Audio HW to avoid glitching */
	mtk_hdmi_v2_hw_aud_mute(hdmi, true);
	mtk_hdmi_v2_hw_aud_enable(hdmi, false);
	mtk_hdmi_v2_audio_reset(hdmi, true);

	/* Configure the main hardware params and get out of reset */
	mtk_hdmi_v2_aip_ctrl_init(hdmi);
	mtk_hdmi_v2_aud_set_input(hdmi);
	mtk_hdmi_v2_hw_aud_set_channel_status(hdmi);
	mtk_hdmi_v2_setup_audio_infoframe(hdmi);
	mtk_hdmi_v2_hw_audio_input_enable(hdmi, true);
	mtk_hdmi_v2_audio_reset(hdmi, false);

	/* Ignore N/CTS packet transmission requests and configure */
	mtk_hdmi_v2_hw_ncts_enable(hdmi, false);
	mtk_hdmi_v2_hw_aud_set_ncts(hdmi, hdmi->aud_param.codec_params.sample_rate,
				    display_mode->clock);

	/* Wait for the HW to apply settings */
	usleep_range(25, 50);

	/* Hardware is fully configured: enable TX of N/CTS pkts and unmute */
	mtk_hdmi_v2_hw_ncts_enable(hdmi, true);
	mtk_hdmi_v2_hw_aud_enable(hdmi, true);
	mtk_hdmi_v2_hw_aud_mute(hdmi, false);
}

static void mtk_hdmi_v2_change_video_resolution(struct mtk_hdmi *hdmi,
						struct drm_connector_state *conn_state)
{
	mtk_hdmi_v2_hw_reset(hdmi);
	mtk_hdmi_v2_set_sw_hpd(hdmi, true);
	udelay(2);

	regmap_write(hdmi->regs, HDCP_TOP_CTRL, 0);

	/*
	 * Enable HDCP reauthentication interrupt: the HW uses this internally
	 * for the HPD state machine even if HDCP encryption is not enabled.
	 */
	regmap_set_bits(hdmi->regs, TOP_INT_ENABLE00, HDCP2X_RX_REAUTH_REQ_DDCM_INT);

	/* Enable hotplug and pord interrupts */
	mtk_hdmi_v2_enable_hpd_pord_irq(hdmi, true);

	/* Force enabling HDCP HPD */
	regmap_set_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_OVR);
	regmap_set_bits(hdmi->regs, HDCP2X_CTRL_0, HDCP2X_HPD_SW);

	/* Set 8 bits per pixel */
	regmap_update_bits(hdmi->regs, TOP_CFG00, TMDS_PACK_MODE,
			   FIELD_PREP(TMDS_PACK_MODE, TMDS_PACK_MODE_8BPP));
	/* Disable generating deepcolor packets */
	regmap_clear_bits(hdmi->regs, TOP_CFG00, DEEPCOLOR_PKT_EN);
	/* Disable adding deepcolor information to the general packet */
	regmap_clear_bits(hdmi->regs, TOP_MISC_CTLR, DEEP_COLOR_ADD);

	if (hdmi->curr_conn->display_info.is_hdmi)
		regmap_set_bits(hdmi->regs, TOP_CFG00, HDMI_MODE_HDMI);
	else
		regmap_clear_bits(hdmi->regs, TOP_CFG00, HDMI_MODE_HDMI);

	udelay(5);
	mtk_hdmi_v2_hw_vid_mute(hdmi, true);
	mtk_hdmi_v2_hw_aud_mute(hdmi, true);
	mtk_hdmi_v2_hw_gcp_avmute(hdmi, false);

	regmap_update_bits(hdmi->regs, TOP_CFG01,
			   NULL_PKT_VSYNC_HIGH_EN | NULL_PKT_EN, NULL_PKT_VSYNC_HIGH_EN);
	usleep_range(100, 150);

	/* Enable scrambling if tmds clock is 340MHz or more */
	mtk_hdmi_v2_enable_scrambling(hdmi, hdmi->mode.clock >= 340 * KILO);

	switch (conn_state->hdmi.output_format) {
	default:
	case HDMI_COLORSPACE_RGB:
	case HDMI_COLORSPACE_YUV444:
		/* Disable YUV420 downsampling for RGB and YUV444 */
		mtk_hdmi_yuv420_downsampling(hdmi, false);
		break;
	case HDMI_COLORSPACE_YUV422:
		/*
		 * YUV420 downsampling is special and needs a bit of setup
		 * so we disable everything there before doing anything else.
		 *
		 * YUV422 downsampling instead just needs one bit to be set.
		 */
		mtk_hdmi_yuv420_downsampling(hdmi, false);
		regmap_set_bits(hdmi->regs, VID_DOWNSAMPLE_CONFIG,
				C444_C422_CONFIG_ENABLE);
		break;
	case HDMI_COLORSPACE_YUV420:
		mtk_hdmi_yuv420_downsampling(hdmi, true);
		break;
	}
}

static void mtk_hdmi_v2_output_set_display_mode(struct mtk_hdmi *hdmi,
						struct drm_connector_state *conn_state,
						struct drm_display_mode *mode)
{
	union phy_configure_opts opts = {
		.dp = { .link_rate = hdmi->mode.clock * KILO }
	};
	int ret;

	ret = phy_configure(hdmi->phy, &opts);
	if (ret)
		dev_err(hdmi->dev, "Setting clock=%d failed: %d", mode->clock, ret);

	mtk_hdmi_v2_change_video_resolution(hdmi, conn_state);
	mtk_hdmi_v2_aud_output_config(hdmi, mode);
}

static int mtk_hdmi_v2_clk_enable(struct mtk_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_SEL]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_24M_SEL]);
	if (ret)
		goto disable_hdcp_clk;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_V2_CLK_HDMI_APB_SEL]);
	if (ret)
		goto disable_hdcp_24m_clk;

	ret = clk_prepare_enable(hdmi->clk[MTK_HDMI_V2_CLK_VPP_SPLIT_HDMI]);
	if (ret)
		goto disable_bus_clk;

	return 0;

disable_bus_clk:
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDMI_APB_SEL]);
disable_hdcp_24m_clk:
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_24M_SEL]);
disable_hdcp_clk:
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_SEL]);

	return ret;
}

static void mtk_hdmi_v2_clk_disable(struct mtk_hdmi *hdmi)
{
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_VPP_SPLIT_HDMI]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDMI_APB_SEL]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_24M_SEL]);
	clk_disable_unprepare(hdmi->clk[MTK_HDMI_V2_CLK_HDCP_SEL]);
}

static enum hdmi_hpd_state mtk_hdmi_v2_hpd_pord_status(struct mtk_hdmi *hdmi)
{
	u8 hpd_pin_sta, pord_pin_sta;
	u32 hpd_status;

	regmap_read(hdmi->regs, HPD_DDC_STATUS, &hpd_status);
	hpd_pin_sta = FIELD_GET(HPD_PIN_STA, hpd_status);
	pord_pin_sta = FIELD_GET(PORD_PIN_STA, hpd_status);

	/*
	 * Inform that the cable is plugged in (hpd_pin_sta) so that the
	 * sink can be powered on by switching the 5V VBUS as required by
	 * the HDMI spec for reading EDID and for HDMI Audio registers to
	 * be accessible.
	 *
	 * PORD detection succeeds only when the cable is plugged in and
	 * the sink is powered on: reaching that state means that the
	 * communication with the sink can be started.
	 *
	 * Please note that when the cable is plugged out the HPD pin will
	 * be the first one to fall, while PORD may still be in rise state
	 * for a few more milliseconds, so we decide HDMI_PLUG_OUT without
	 * checking PORD at all (we check only HPD falling for that).
	 */
	if (hpd_pin_sta && pord_pin_sta)
		return HDMI_PLUG_IN_AND_SINK_POWER_ON;
	else if (hpd_pin_sta)
		return HDMI_PLUG_IN_ONLY;
	else
		return HDMI_PLUG_OUT;
}

static irqreturn_t mtk_hdmi_v2_isr(int irq, void *arg)
{
	struct mtk_hdmi *hdmi = arg;
	unsigned int irq_sta;
	int ret = IRQ_HANDLED;

	regmap_read(hdmi->regs, TOP_INT_STA00, &irq_sta);

	/* Handle Hotplug Detection interrupts */
	if (irq_sta & HPD_PORD_HWIRQS) {
		/*
		 * Disable the HPD/PORD IRQs now and until thread done to
		 * avoid interrupt storm that could happen with bad cables
		 */
		mtk_hdmi_v2_enable_hpd_pord_irq(hdmi, false);
		ret = IRQ_WAKE_THREAD;

		/* Clear HPD/PORD irqs to avoid unwanted retriggering */
		regmap_write(hdmi->regs, TOP_INT_CLR00, HPD_PORD_HWIRQS);
		regmap_write(hdmi->regs, TOP_INT_CLR00, 0);
	}

	return ret;
}

static irqreturn_t __mtk_hdmi_v2_isr_thread(struct mtk_hdmi *hdmi)
{
	enum hdmi_hpd_state hpd;

	hpd = mtk_hdmi_v2_hpd_pord_status(hdmi);
	if (hpd != hdmi->hpd) {
		struct drm_encoder *encoder = hdmi->bridge.encoder;

		hdmi->hpd = hpd;

		if (encoder && encoder->dev)
			drm_helper_hpd_irq_event(hdmi->bridge.encoder->dev);
	}

	mtk_hdmi_v2_enable_hpd_pord_irq(hdmi, true);
	return IRQ_HANDLED;
}

static irqreturn_t mtk_hdmi_v2_isr_thread(int irq, void *arg)
{
	struct mtk_hdmi *hdmi = arg;

	/*
	 * Debounce HDMI monitor HPD status.
	 * Empirical testing shows that 30ms is enough wait
	 */
	msleep(30);

	return __mtk_hdmi_v2_isr_thread(hdmi);
}

static int mtk_hdmi_v2_enable(struct mtk_hdmi *hdmi)
{
	bool was_active = pm_runtime_active(hdmi->dev);
	int ret;

	ret = pm_runtime_resume_and_get(hdmi->dev);
	if (ret) {
		dev_err(hdmi->dev, "Cannot resume HDMI\n");
		return ret;
	}

	ret = mtk_hdmi_v2_clk_enable(hdmi);
	if (ret) {
		pm_runtime_put(hdmi->dev);
		return ret;
	}

	if (!was_active) {
		mtk_hdmi_v2_hw_reset(hdmi);
		mtk_hdmi_v2_set_sw_hpd(hdmi, true);
	}

	return 0;
}

static void mtk_hdmi_v2_disable(struct mtk_hdmi *hdmi)
{
	mtk_hdmi_v2_clk_disable(hdmi);
	pm_runtime_put_sync(hdmi->dev);
}

/*
 * Bridge callbacks
 */

static int mtk_hdmi_v2_bridge_attach(struct drm_bridge *bridge,
				     struct drm_encoder *encoder,
				     enum drm_bridge_attach_flags flags)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_ERROR("The flag DRM_BRIDGE_ATTACH_NO_CONNECTOR must be supplied\n");
		return -EINVAL;
	}
	if (hdmi->bridge.next_bridge) {
		ret = drm_bridge_attach(encoder, hdmi->bridge.next_bridge, bridge, flags);
		if (ret)
			return ret;
	}

	ret = mtk_hdmi_v2_enable(hdmi);
	if (ret)
		return ret;

	/* Enable Hotplug and Pord pins internal debouncing */
	regmap_set_bits(hdmi->regs, HPD_DDC_CTRL,
			HPD_DDC_HPD_DBNC_EN | HPD_DDC_PORD_DBNC_EN);

	irq_clear_status_flags(hdmi->irq, IRQ_NOAUTOEN);
	enable_irq(hdmi->irq);

	/*
	 * Check if any HDMI monitor was connected before probing this driver
	 * and/or attaching the bridge, without debouncing: if so, we want to
	 * notify the DRM so that we start outputting an image ASAP.
	 * Note that calling the ISR thread function will also perform a HW
	 * registers write that enables both the HPD and Pord interrupts.
	 */
	__mtk_hdmi_v2_isr_thread(hdmi);

	mtk_hdmi_v2_disable(hdmi);

	return 0;
}

static void mtk_hdmi_v2_bridge_detach(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	WARN_ON(pm_runtime_active(hdmi->dev));

	/* The controller is already powered off, just disable irq here */
	disable_irq(hdmi->irq);
}

static void mtk_hdmi_v2_handle_plugged_change(struct mtk_hdmi *hdmi, bool plugged)
{
	mutex_lock(&hdmi->update_plugged_status_lock);
	if (hdmi->plugged_cb && hdmi->codec_dev)
		hdmi->plugged_cb(hdmi->codec_dev, plugged);
	mutex_unlock(&hdmi->update_plugged_status_lock);
}

static void mtk_hdmi_v2_bridge_pre_enable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	struct drm_connector_state *conn_state;
	union phy_configure_opts opts = {
		.dp = { .link_rate = hdmi->mode.clock * KILO }
	};
	int ret;

	/* Power on the controller before trying to write to registers */
	ret = mtk_hdmi_v2_enable(hdmi);
	if (WARN_ON(ret))
		return;

	/* Retrieve the connector through the atomic state */
	hdmi->curr_conn = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);

	conn_state = drm_atomic_get_new_connector_state(state, hdmi->curr_conn);
	if (WARN_ON(!conn_state))
		return;

	/*
	 * Preconfigure the HDMI controller and the HDMI PHY at pre_enable
	 * stage to make sure that this IP is ready and clocked before the
	 * mtk_dpi gets powered on and before it enables the output.
	 */
	mtk_hdmi_v2_output_set_display_mode(hdmi, conn_state, &hdmi->mode);

	/* Reconfigure phy clock link with appropriate rate */
	phy_configure(hdmi->phy, &opts);

	/* Power on the PHY here to make sure that DPI_HDMI is clocked */
	phy_power_on(hdmi->phy);

	hdmi->powered = true;
}

static void mtk_hdmi_v2_bridge_enable(struct drm_bridge *bridge,
				      struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	if (WARN_ON(!hdmi->powered))
		return;

	ret = drm_atomic_helper_connector_hdmi_update_infoframes(hdmi->curr_conn, state);
	if (ret)
		dev_err(hdmi->dev, "Could not update infoframes: %d\n", ret);

	mtk_hdmi_v2_hw_vid_mute(hdmi, false);

	/* signal the connect event to audio codec */
	mtk_hdmi_v2_handle_plugged_change(hdmi, true);

	hdmi->enabled = true;
}

static void mtk_hdmi_v2_bridge_disable(struct drm_bridge *bridge,
				       struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->enabled)
		return;

	mtk_hdmi_v2_hw_gcp_avmute(hdmi, true);
	msleep(50);
	mtk_hdmi_v2_hw_vid_mute(hdmi, true);
	mtk_hdmi_v2_hw_aud_mute(hdmi, true);
	msleep(50);

	hdmi->enabled = false;
}

static void mtk_hdmi_v2_bridge_post_disable(struct drm_bridge *bridge,
					    struct drm_atomic_state *state)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	if (!hdmi->powered)
		return;

	phy_power_off(hdmi->phy);
	hdmi->powered = false;

	/* signal the disconnect event to audio codec */
	mtk_hdmi_v2_handle_plugged_change(hdmi, false);

	/* Power off */
	mtk_hdmi_v2_disable(hdmi);
}

static enum drm_connector_status mtk_hdmi_v2_bridge_detect(struct drm_bridge *bridge,
							   struct drm_connector *connector)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	return hdmi->hpd != HDMI_PLUG_OUT ?
	       connector_status_connected : connector_status_disconnected;
}

static const struct drm_edid *mtk_hdmi_v2_bridge_edid_read(struct drm_bridge *bridge,
							   struct drm_connector *connector)
{
	return drm_edid_read(connector);
}

static void mtk_hdmi_v2_hpd_enable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);
	int ret;

	ret = mtk_hdmi_v2_enable(hdmi);
	if (ret) {
		dev_err(hdmi->dev, "Cannot power on controller for HPD: %d\n", ret);
		return;
	}

	mtk_hdmi_v2_enable_hpd_pord_irq(hdmi, true);
}

static void mtk_hdmi_v2_hpd_disable(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	mtk_hdmi_v2_enable_hpd_pord_irq(hdmi, false);
	mtk_hdmi_v2_disable(hdmi);
}

static enum drm_mode_status
mtk_hdmi_v2_hdmi_tmds_char_rate_valid(const struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      unsigned long long tmds_rate)
{
	if (mode->clock < MTK_HDMI_V2_CLOCK_MIN)
		return MODE_CLOCK_LOW;
	else if (mode->clock > MTK_HDMI_V2_CLOCK_MAX)
		return MODE_CLOCK_HIGH;
	else
		return MODE_OK;
}

static int mtk_hdmi_v2_hdmi_clear_audio_infoframe(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, AUD_EN_WR | AUD_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, AUD_RPT_EN);

	return 0;
}

static int mtk_hdmi_v2_hdmi_clear_avi_infoframe(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, AVI_EN_WR | AVI_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, AVI_RPT_EN);

	return 0;
}

static int mtk_hdmi_v2_hdmi_clear_spd_infoframe(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, SPD_EN_WR | SPD_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, SPD_RPT_EN);

	return 0;
}

static int mtk_hdmi_v2_hdmi_clear_hdmi_infoframe(struct drm_bridge *bridge)
{
	struct mtk_hdmi *hdmi = hdmi_ctx_from_bridge(bridge);

	regmap_clear_bits(hdmi->regs, TOP_INFO_EN, VSIF_EN_WR | VSIF_EN);
	regmap_clear_bits(hdmi->regs, TOP_INFO_RPT, VSIF_RPT_EN);

	return 0;
}

static int mtk_hdmi_v2_set_abist(struct mtk_hdmi *hdmi, bool enable)
{
	struct drm_display_mode *mode = &hdmi->mode;
	int abist_format = -EINVAL;
	bool interlaced;

	if (!enable) {
		regmap_clear_bits(hdmi->regs, TOP_CFG00, HDMI_ABIST_ENABLE);
		return 0;
	}

	if (!mode->hdisplay || !mode->vdisplay)
		return -EINVAL;

	interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;

	switch (mode->hdisplay) {
	case 720:
		if (mode->vdisplay == 480)
			abist_format = 2;
		else if (mode->vdisplay == 576)
			abist_format = 11;
		break;
	case 1280:
		if (mode->vdisplay == 720)
			abist_format = 3;
		break;
	case 1440:
		if (mode->vdisplay == 480)
			abist_format = interlaced ? 5 : 9;
		else if (mode->vdisplay == 576)
			abist_format = interlaced ? 14 : 18;
		break;
	case 1920:
		if (mode->vdisplay == 1080)
			abist_format = interlaced ? 4 : 10;
		break;
	case 3840:
		if (mode->vdisplay == 2160)
			abist_format = 25;
		break;
	case 4096:
		if (mode->vdisplay == 2160)
			abist_format = 26;
		break;
	default:
		break;
	}
	if (abist_format < 0)
		return abist_format;

	regmap_update_bits(hdmi->regs, TOP_CFG00, HDMI_ABIST_VIDEO_FORMAT,
			   FIELD_PREP(HDMI_ABIST_VIDEO_FORMAT, abist_format));
	regmap_set_bits(hdmi->regs, TOP_CFG00, HDMI_ABIST_ENABLE);
	return 0;
}

static int mtk_hdmi_v2_debug_abist_show(struct seq_file *m, void *arg)
{
	struct mtk_hdmi *hdmi = m->private;
	bool en;
	u32 val;
	int ret;

	if (!hdmi)
		return -EINVAL;

	ret = regmap_read(hdmi->regs, TOP_CFG00, &val);
	if (ret)
		return ret;

	en = FIELD_GET(HDMI_ABIST_ENABLE, val);

	seq_printf(m, "HDMI Automated Built-In Self Test: %s\n",
		   en ? "Enabled" : "Disabled");

	return 0;
}

static ssize_t mtk_hdmi_v2_debug_abist_write(struct file *file,
					     const char __user *ubuf,
					     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	int ret;
	u32 en;

	if (!m || !m->private || *offp)
		return -EINVAL;

	ret = kstrtouint_from_user(ubuf, len, 0, &en);
	if (ret)
		return ret;

	if (en < 0 || en > 1)
		return -EINVAL;

	mtk_hdmi_v2_set_abist((struct mtk_hdmi *)m->private, en);
	return len;
}

static int mtk_hdmi_v2_debug_abist_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtk_hdmi_v2_debug_abist_show, inode->i_private);
}

static const struct file_operations mtk_hdmi_debug_abist_fops = {
	.owner = THIS_MODULE,
	.open = mtk_hdmi_v2_debug_abist_open,
	.read = seq_read,
	.write = mtk_hdmi_v2_debug_abist_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void mtk_hdmi_v2_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct mtk_hdmi *dpi = hdmi_ctx_from_bridge(bridge);

	debugfs_create_file("hdmi_abist", 0640, root, dpi, &mtk_hdmi_debug_abist_fops);
}

static const struct drm_bridge_funcs mtk_v2_hdmi_bridge_funcs = {
	.attach = mtk_hdmi_v2_bridge_attach,
	.detach = mtk_hdmi_v2_bridge_detach,
	.mode_fixup = mtk_hdmi_bridge_mode_fixup,
	.mode_set = mtk_hdmi_bridge_mode_set,
	.atomic_pre_enable = mtk_hdmi_v2_bridge_pre_enable,
	.atomic_enable = mtk_hdmi_v2_bridge_enable,
	.atomic_disable = mtk_hdmi_v2_bridge_disable,
	.atomic_post_disable = mtk_hdmi_v2_bridge_post_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.detect = mtk_hdmi_v2_bridge_detect,
	.edid_read = mtk_hdmi_v2_bridge_edid_read,
	.hpd_enable = mtk_hdmi_v2_hpd_enable,
	.hpd_disable = mtk_hdmi_v2_hpd_disable,
	.hdmi_tmds_char_rate_valid = mtk_hdmi_v2_hdmi_tmds_char_rate_valid,
	.hdmi_clear_audio_infoframe = mtk_hdmi_v2_hdmi_clear_audio_infoframe,
	.hdmi_write_audio_infoframe = mtk_hdmi_v2_hdmi_write_audio_infoframe,
	.hdmi_clear_avi_infoframe = mtk_hdmi_v2_hdmi_clear_avi_infoframe,
	.hdmi_write_avi_infoframe = mtk_hdmi_v2_hdmi_write_avi_infoframe,
	.hdmi_clear_spd_infoframe = mtk_hdmi_v2_hdmi_clear_spd_infoframe,
	.hdmi_write_spd_infoframe = mtk_hdmi_v2_hdmi_write_spd_infoframe,
	.hdmi_clear_hdmi_infoframe = mtk_hdmi_v2_hdmi_clear_hdmi_infoframe,
	.hdmi_write_hdmi_infoframe = mtk_hdmi_v2_hdmi_write_hdmi_infoframe,
	.debugfs_init = mtk_hdmi_v2_debugfs_init,
};

/*
 * HDMI audio codec callbacks
 */
static int mtk_hdmi_v2_audio_hook_plugged_cb(struct device *dev, void *data,
					     hdmi_codec_plugged_cb fn,
					     struct device *codec_dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);
	bool plugged;

	if (!hdmi)
		return -ENODEV;

	mtk_hdmi_audio_set_plugged_cb(hdmi, fn, codec_dev);
	plugged = (hdmi->hpd == HDMI_PLUG_IN_AND_SINK_POWER_ON);
	mtk_hdmi_v2_handle_plugged_change(hdmi, plugged);

	return 0;
}

static int mtk_hdmi_v2_audio_hw_params(struct device *dev, void *data,
				       struct hdmi_codec_daifmt *daifmt,
				       struct hdmi_codec_params *params)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi->audio_enable) {
		mtk_hdmi_audio_params(hdmi, daifmt, params);
		mtk_hdmi_v2_aud_output_config(hdmi, &hdmi->mode);
	}
	return 0;
}

static int mtk_hdmi_v2_audio_startup(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_v2_hw_aud_enable(hdmi, true);
	hdmi->audio_enable = true;

	return 0;
}

static void mtk_hdmi_v2_audio_shutdown(struct device *dev, void *data)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->audio_enable = false;
	mtk_hdmi_v2_hw_aud_enable(hdmi, false);
}

static int mtk_hdmi_v2_audio_mute(struct device *dev, void *data, bool enable, int dir)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_v2_hw_aud_mute(hdmi, enable);

	return 0;
}

static const struct hdmi_codec_ops mtk_hdmi_v2_audio_codec_ops = {
	.hw_params = mtk_hdmi_v2_audio_hw_params,
	.audio_startup = mtk_hdmi_v2_audio_startup,
	.audio_shutdown = mtk_hdmi_v2_audio_shutdown,
	.mute_stream = mtk_hdmi_v2_audio_mute,
	.get_eld = mtk_hdmi_audio_get_eld,
	.hook_plugged_cb = mtk_hdmi_v2_audio_hook_plugged_cb,
};

static __maybe_unused int mtk_hdmi_v2_suspend(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	mtk_hdmi_v2_disable(hdmi);

	return 0;
}

static __maybe_unused int mtk_hdmi_v2_resume(struct device *dev)
{
	struct mtk_hdmi *hdmi = dev_get_drvdata(dev);

	return mtk_hdmi_v2_enable(hdmi);
}

static SIMPLE_DEV_PM_OPS(mtk_hdmi_v2_pm_ops, mtk_hdmi_v2_suspend, mtk_hdmi_v2_resume);

static const struct mtk_hdmi_ver_conf mtk_hdmi_conf_v2 = {
	.bridge_funcs = &mtk_v2_hdmi_bridge_funcs,
	.codec_ops = &mtk_hdmi_v2_audio_codec_ops,
	.mtk_hdmi_clock_names = mtk_hdmi_v2_clk_names,
	.num_clocks = MTK_HDMI_V2_CLK_COUNT,
	.interlace_allowed = true,
};

static const struct mtk_hdmi_conf mtk_hdmi_conf_mt8188 = {
	.ver_conf = &mtk_hdmi_conf_v2,
	.reg_hdmi_tx_cfg = HDMITX_CONFIG_MT8188
};

static const struct mtk_hdmi_conf mtk_hdmi_conf_mt8195 = {
	.ver_conf = &mtk_hdmi_conf_v2,
	.reg_hdmi_tx_cfg = HDMITX_CONFIG_MT8195
};

static int mtk_hdmi_v2_probe(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi;
	int ret;

	/* Populate HDMI sub-devices if present */
	ret = devm_of_platform_populate(&pdev->dev);
	if (ret)
		return ret;

	hdmi = mtk_hdmi_common_probe(pdev);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	hdmi->hpd = HDMI_PLUG_OUT;

	/* Disable all HW interrupts at probe stage */
	mtk_hdmi_v2_hwirq_disable(hdmi);

	/*
	 * In case bootloader leaves HDMI enabled before booting, make
	 * sure that any interrupt that was left is cleared by setting
	 * all bits in the INT_CLR registers for all 32+19 interrupts.
	 */
	regmap_write(hdmi->regs, TOP_INT_CLR00, GENMASK(31, 0));
	regmap_write(hdmi->regs, TOP_INT_CLR01, GENMASK(18, 0));

	/* Restore interrupt clearing registers to zero */
	regmap_write(hdmi->regs, TOP_INT_CLR00, 0);
	regmap_write(hdmi->regs, TOP_INT_CLR01, 0);

	/*
	 * Install the ISR but keep it disabled: as the interrupts are
	 * being set up in the .bridge_attach() callback which will
	 * enable both the right HW IRQs and the ISR.
	 */
	irq_set_status_flags(hdmi->irq, IRQ_NOAUTOEN);
	ret = devm_request_threaded_irq(&pdev->dev, hdmi->irq, mtk_hdmi_v2_isr,
					mtk_hdmi_v2_isr_thread,
					IRQ_TYPE_LEVEL_HIGH,
					dev_name(&pdev->dev), hdmi);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Cannot request IRQ\n");

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Cannot enable Runtime PM\n");

	return 0;
}

static void mtk_hdmi_v2_remove(struct platform_device *pdev)
{
	struct mtk_hdmi *hdmi = platform_get_drvdata(pdev);

	i2c_put_adapter(hdmi->ddc_adpt);
}

static const struct of_device_id mtk_drm_hdmi_v2_of_ids[] = {
	{ .compatible = "mediatek,mt8188-hdmi-tx", .data = &mtk_hdmi_conf_mt8188 },
	{ .compatible = "mediatek,mt8195-hdmi-tx", .data = &mtk_hdmi_conf_mt8195 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_drm_hdmi_v2_of_ids);

static struct platform_driver mtk_hdmi_v2_driver = {
	.probe = mtk_hdmi_v2_probe,
	.remove = mtk_hdmi_v2_remove,
	.driver = {
		.name = "mediatek-drm-hdmi-v2",
		.of_match_table = mtk_drm_hdmi_v2_of_ids,
		.pm = &mtk_hdmi_v2_pm_ops,
	},
};
module_platform_driver(mtk_hdmi_v2_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>>");
MODULE_DESCRIPTION("MediaTek HDMIv2 Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DRM_MTK_HDMI");
