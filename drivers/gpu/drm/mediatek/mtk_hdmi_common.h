/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 */

#ifndef _MTK_HDMI_COMMON_H
#define _MTK_HDMI_COMMON_H

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include <sound/hdmi-codec.h>

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
	enum hdmi_audio_sample_size aud_sample_size;
	enum hdmi_aud_input_type aud_input_type;
	enum hdmi_aud_i2s_fmt aud_i2s_fmt;
	enum hdmi_aud_mclk aud_mclk;
	enum hdmi_aud_channel_type aud_input_chan_type;
	struct hdmi_codec_params codec_params;
};

enum hdmi_hpd_state {
	HDMI_PLUG_OUT = 0,
	HDMI_PLUG_IN_AND_SINK_POWER_ON,
	HDMI_PLUG_IN_ONLY,
};

struct mtk_hdmi_ver_conf {
	const struct drm_bridge_funcs *bridge_funcs;
	const struct hdmi_codec_ops *codec_ops;
	const char * const *mtk_hdmi_clock_names;
	int num_clocks;
	bool interlace_allowed;
};

struct mtk_hdmi_conf {
	const struct mtk_hdmi_ver_conf *ver_conf;
	bool tz_disabled;
	bool cea_modes_only;
	unsigned long max_mode_clock;
	u32 reg_hdmi_tx_cfg;
};

struct mtk_hdmi {
	struct drm_bridge bridge;
	struct drm_connector *curr_conn;/* current connector (only valid when 'enabled') */
	struct device *dev;
	const struct mtk_hdmi_conf *conf;
	struct phy *phy;
	struct device *cec_dev;
	struct i2c_adapter *ddc_adpt;
	struct clk **clk;
	struct drm_display_mode mode;
	bool dvi_mode;
	struct regmap *sys_regmap;
	unsigned int sys_offset;
	struct regmap *regs;
	struct platform_device *audio_pdev;
	struct hdmi_audio_param aud_param;
	bool audio_enable;
	bool powered;
	bool enabled;
	int irq;
	enum hdmi_hpd_state hpd;
	hdmi_codec_plugged_cb plugged_cb;
	struct device *codec_dev;
	struct mutex update_plugged_status_lock;
};

static inline struct mtk_hdmi *hdmi_ctx_from_bridge(struct drm_bridge *b)
{
	return container_of(b, struct mtk_hdmi, bridge);
}


int mtk_hdmi_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len);
void mtk_hdmi_audio_set_plugged_cb(struct mtk_hdmi *hdmi, hdmi_codec_plugged_cb fn,
				   struct device *codec_dev);
int mtk_hdmi_audio_params(struct mtk_hdmi *hdmi, struct hdmi_codec_daifmt *daifmt,
			  struct hdmi_codec_params *params);
void mtk_hdmi_get_ncts(unsigned int sample_rate, unsigned int clock,
		       unsigned int *n, unsigned int *cts);
bool mtk_hdmi_bridge_mode_fixup(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode);
void mtk_hdmi_bridge_mode_set(struct drm_bridge *bridge,
			      const struct drm_display_mode *mode,
			      const struct drm_display_mode *adjusted_mode);
struct mtk_hdmi *mtk_hdmi_common_probe(struct platform_device *pdev);
#endif /* _MTK_HDMI_COMMON_H */
