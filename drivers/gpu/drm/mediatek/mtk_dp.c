// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019-2022 MediaTek Inc.
 * Copyright (c) 2022 BayLibre
 */

#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>
#include <sound/hdmi-codec.h>
#include <video/videomode.h>

#include "mtk_dp_reg.h"

#define MTK_DP_SIP_CONTROL_AARCH32	MTK_SIP_SMC_CMD(0x523)
#define MTK_DP_SIP_ATF_EDP_VIDEO_UNMUTE	(BIT(0) | BIT(5))
#define MTK_DP_SIP_ATF_VIDEO_UNMUTE	BIT(5)

#define MTK_DP_THREAD_CABLE_STATE_CHG	BIT(0)
#define MTK_DP_THREAD_HPD_EVENT		BIT(1)

#define MTK_DP_4P1T 4
#define MTK_DP_HDE 2
#define MTK_DP_PIX_PER_ADDR 2
#define MTK_DP_AUX_WAIT_REPLY_COUNT 20
#define MTK_DP_TBC_BUF_READ_START_ADDR 0x8
#define MTK_DP_TRAIN_VOLTAGE_LEVEL_RETRY 5
#define MTK_DP_TRAIN_DOWNSCALE_RETRY 10
#define MTK_DP_VERSION 0x11
#define MTK_DP_SDP_AUI 0x4

enum {
	MTK_DP_CAL_GLB_BIAS_TRIM = 0,
	MTK_DP_CAL_CLKTX_IMPSE,
	MTK_DP_CAL_LN_TX_IMPSEL_PMOS_0,
	MTK_DP_CAL_LN_TX_IMPSEL_PMOS_1,
	MTK_DP_CAL_LN_TX_IMPSEL_PMOS_2,
	MTK_DP_CAL_LN_TX_IMPSEL_PMOS_3,
	MTK_DP_CAL_LN_TX_IMPSEL_NMOS_0,
	MTK_DP_CAL_LN_TX_IMPSEL_NMOS_1,
	MTK_DP_CAL_LN_TX_IMPSEL_NMOS_2,
	MTK_DP_CAL_LN_TX_IMPSEL_NMOS_3,
	MTK_DP_CAL_MAX,
};

struct mtk_dp_train_info {
	bool sink_ssc;
	bool cable_plugged_in;
	/* link_rate is in multiple of 0.27Gbps */
	int link_rate;
	int lane_count;
	unsigned int channel_eq_pattern;
};

struct mtk_dp_audio_cfg {
	bool detect_monitor;
	int sad_count;
	int sample_rate;
	int word_length_bits;
	int channels;
};

struct mtk_dp_info {
	enum dp_pixelformat format;
	struct videomode vm;
	struct mtk_dp_audio_cfg audio_cur_cfg;
};

struct mtk_dp_efuse_fmt {
	unsigned short idx;
	unsigned short shift;
	unsigned short mask;
	unsigned short min_val;
	unsigned short max_val;
	unsigned short default_val;
};

struct mtk_dp {
	bool enabled;
	bool need_debounce;
	u8 max_lanes;
	u8 max_linkrate;
	u8 rx_cap[DP_RECEIVER_CAP_SIZE];
	u32 cal_data[MTK_DP_CAL_MAX];
	u32 irq_thread_handle;
	/* irq_thread_lock is used to protect irq_thread_handle */
	spinlock_t irq_thread_lock;

	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *conn;
	struct drm_device *drm_dev;
	struct drm_dp_aux aux;

	const struct mtk_dp_data *data;
	struct mtk_dp_info info;
	struct mtk_dp_train_info train_info;

	struct platform_device *phy_dev;
	struct phy *phy;
	struct regmap *regs;
	struct timer_list debounce_timer;

	/* For audio */
	bool audio_enable;
	hdmi_codec_plugged_cb plugged_cb;
	struct platform_device *audio_pdev;

	struct device *codec_dev;
	/* protect the plugged_cb as it's used in both bridge ops and audio */
	struct mutex update_plugged_status_lock;
};

struct mtk_dp_data {
	int bridge_type;
	unsigned int smc_cmd;
	const struct mtk_dp_efuse_fmt *efuse_fmt;
	bool audio_supported;
};

static const struct mtk_dp_efuse_fmt mt8195_edp_efuse_fmt[MTK_DP_CAL_MAX] = {
	[MTK_DP_CAL_GLB_BIAS_TRIM] = {
		.idx = 3,
		.shift = 27,
		.mask = 0x1f,
		.min_val = 1,
		.max_val = 0x1e,
		.default_val = 0xf,
	},
	[MTK_DP_CAL_CLKTX_IMPSE] = {
		.idx = 0,
		.shift = 9,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_0] = {
		.idx = 2,
		.shift = 28,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_1] = {
		.idx = 2,
		.shift = 20,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_2] = {
		.idx = 2,
		.shift = 12,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_3] = {
		.idx = 2,
		.shift = 4,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_0] = {
		.idx = 2,
		.shift = 24,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_1] = {
		.idx = 2,
		.shift = 16,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_2] = {
		.idx = 2,
		.shift = 8,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_3] = {
		.idx = 2,
		.shift = 0,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
};

static const struct mtk_dp_efuse_fmt mt8195_dp_efuse_fmt[MTK_DP_CAL_MAX] = {
	[MTK_DP_CAL_GLB_BIAS_TRIM] = {
		.idx = 0,
		.shift = 27,
		.mask = 0x1f,
		.min_val = 1,
		.max_val = 0x1e,
		.default_val = 0xf,
	},
	[MTK_DP_CAL_CLKTX_IMPSE] = {
		.idx = 0,
		.shift = 13,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_0] = {
		.idx = 1,
		.shift = 28,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_1] = {
		.idx = 1,
		.shift = 20,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_2] = {
		.idx = 1,
		.shift = 12,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_3] = {
		.idx = 1,
		.shift = 4,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_0] = {
		.idx = 1,
		.shift = 24,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_1] = {
		.idx = 1,
		.shift = 16,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_2] = {
		.idx = 1,
		.shift = 8,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
	[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_3] = {
		.idx = 1,
		.shift = 0,
		.mask = 0xf,
		.min_val = 1,
		.max_val = 0xe,
		.default_val = 0x8,
	},
};

static struct regmap_config mtk_dp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = SEC_OFFSET + 0x90,
	.name = "mtk-dp-registers",
};

static struct mtk_dp *mtk_dp_from_bridge(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dp, bridge);
}

static u32 mtk_dp_read(struct mtk_dp *mtk_dp, u32 offset)
{
	u32 read_val;
	int ret;

	ret = regmap_read(mtk_dp->regs, offset, &read_val);
	if (ret) {
		dev_err(mtk_dp->dev, "Failed to read register 0x%x: %d\n",
			offset, ret);
		return 0;
	}

	return read_val;
}

static int mtk_dp_write(struct mtk_dp *mtk_dp, u32 offset, u32 val)
{
	int ret = regmap_write(mtk_dp->regs, offset, val);

	if (ret)
		dev_err(mtk_dp->dev,
			"Failed to write register 0x%x with value 0x%x\n",
			offset, val);
	return ret;
}

static int mtk_dp_update_bits(struct mtk_dp *mtk_dp, u32 offset,
			      u32 val, u32 mask)
{
	int ret = regmap_update_bits(mtk_dp->regs, offset, mask, val);

	if (ret)
		dev_err(mtk_dp->dev,
			"Failed to update register 0x%x with value 0x%x, mask 0x%x\n",
			offset, val, mask);
	return ret;
}

static void mtk_dp_bulk_16bit_write(struct mtk_dp *mtk_dp, u32 offset, u8 *buf,
				    size_t length)
{
	int i;

	/* 2 bytes per register */
	for (i = 0; i < length; i += 2) {
		u32 val = buf[i] | (i + 1 < length ? buf[i + 1] << 8 : 0);

		if (mtk_dp_write(mtk_dp, offset + i * 2, val))
			return;
	}
}

static void mtk_dp_msa_bypass_enable(struct mtk_dp *mtk_dp, bool enable)
{
	u32 mask = HTOTAL_SEL_DP_ENC0_P0 | VTOTAL_SEL_DP_ENC0_P0 |
		   HSTART_SEL_DP_ENC0_P0 | VSTART_SEL_DP_ENC0_P0 |
		   HWIDTH_SEL_DP_ENC0_P0 | VHEIGHT_SEL_DP_ENC0_P0 |
		   HSP_SEL_DP_ENC0_P0 | HSW_SEL_DP_ENC0_P0 |
		   VSP_SEL_DP_ENC0_P0 | VSW_SEL_DP_ENC0_P0;

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3030, enable ? 0 : mask, mask);
}

static void mtk_dp_set_msa(struct mtk_dp *mtk_dp)
{
	struct drm_display_mode mode;
	struct videomode *vm = &mtk_dp->info.vm;

	drm_display_mode_from_videomode(vm, &mode);

	/* horizontal */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3010,
			   mode.htotal, HTOTAL_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3018,
			   vm->hsync_len + vm->hback_porch,
			   HSTART_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3028,
			   vm->hsync_len, HSW_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3028,
			   0, HSP_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3020,
			   vm->hactive, HWIDTH_SW_DP_ENC0_P0_MASK);

	/* vertical */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3014,
			   mode.vtotal, VTOTAL_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_301C,
			   vm->vsync_len + vm->vback_porch,
			   VSTART_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_302C,
			   vm->vsync_len, VSW_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_302C,
			   0, VSP_SW_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3024,
			   vm->vactive, VHEIGHT_SW_DP_ENC0_P0_MASK);

	/* horizontal */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3064,
			   vm->hactive, HDE_NUM_LAST_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3154,
			   mode.htotal, PGEN_HTOTAL_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3158,
			   vm->hfront_porch,
			   PGEN_HSYNC_RISING_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_315C,
			   vm->hsync_len,
			   PGEN_HSYNC_PULSE_WIDTH_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3160,
			   vm->hback_porch + vm->hsync_len,
			   PGEN_HFDE_START_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3164,
			   vm->hactive,
			   PGEN_HFDE_ACTIVE_WIDTH_DP_ENC0_P0_MASK);

	/* vertical */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3168,
			   mode.vtotal,
			   PGEN_VTOTAL_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_316C,
			   vm->vfront_porch,
			   PGEN_VSYNC_RISING_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3170,
			   vm->vsync_len,
			   PGEN_VSYNC_PULSE_WIDTH_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3174,
			   vm->vback_porch + vm->vsync_len,
			   PGEN_VFDE_START_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3178,
			   vm->vactive,
			   PGEN_VFDE_ACTIVE_WIDTH_DP_ENC0_P0_MASK);
}

static int mtk_dp_set_color_format(struct mtk_dp *mtk_dp,
				   enum dp_pixelformat color_format)
{
	u32 val;

	/* update MISC0 */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3034,
			   color_format << DP_TEST_COLOR_FORMAT_SHIFT,
			   DP_TEST_COLOR_FORMAT_MASK);

	switch (color_format) {
	case DP_PIXELFORMAT_YUV422:
		val = PIXEL_ENCODE_FORMAT_DP_ENC0_P0_YCBCR422;
		break;
	case DP_PIXELFORMAT_RGB:
		val = PIXEL_ENCODE_FORMAT_DP_ENC0_P0_RGB;
		break;
	default:
		drm_warn(mtk_dp->drm_dev, "Unsupported color format: %d\n",
			 color_format);
		return -EINVAL;
	}

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_303C,
			   val, PIXEL_ENCODE_FORMAT_DP_ENC0_P0_MASK);
	return 0;
}

static void mtk_dp_set_color_depth(struct mtk_dp *mtk_dp)
{
	/* Only support 8 bits currently */
	/* Update MISC0 */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3034,
			   DP_MSA_MISC_8_BPC, DP_TEST_BIT_DEPTH_MASK);

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_303C,
			   VIDEO_COLOR_DEPTH_DP_ENC0_P0_8BIT,
			   VIDEO_COLOR_DEPTH_DP_ENC0_P0_MASK);
}

static void mtk_dp_config_mn_mode(struct mtk_dp *mtk_dp)
{
	/* 0: hw mode, 1: sw mode */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3004,
			   0, VIDEO_M_CODE_SEL_DP_ENC0_P0_MASK);
}

static void mtk_dp_set_sram_read_start(struct mtk_dp *mtk_dp, u32 val)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_303C,
			   val, SRAM_START_READ_THRD_DP_ENC0_P0_MASK);
}

static void mtk_dp_setup_encoder(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_303C,
			   VIDEO_MN_GEN_EN_DP_ENC0_P0,
			   VIDEO_MN_GEN_EN_DP_ENC0_P0);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3040,
			   SDP_DOWN_CNT_DP_ENC0_P0_VAL,
			   SDP_DOWN_CNT_INIT_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3364,
			   SDP_DOWN_CNT_IN_HBLANK_DP_ENC1_P0_VAL,
			   SDP_DOWN_CNT_INIT_IN_HBLANK_DP_ENC1_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3300,
			   VIDEO_AFIFO_RDY_SEL_DP_ENC1_P0_VAL << 8,
			   VIDEO_AFIFO_RDY_SEL_DP_ENC1_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3364,
			   FIFO_READ_START_POINT_DP_ENC1_P0_VAL << 12,
			   FIFO_READ_START_POINT_DP_ENC1_P0_MASK);
	mtk_dp_write(mtk_dp, MTK_DP_ENC1_P0_3368, DP_ENC1_P0_3368_VAL);
}

static void mtk_dp_pg_enable(struct mtk_dp *mtk_dp, bool enable)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3038,
			   enable ? VIDEO_SOURCE_SEL_DP_ENC0_P0_MASK : 0,
			   VIDEO_SOURCE_SEL_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_31B0,
			   PGEN_PATTERN_SEL_VAL << 4, PGEN_PATTERN_SEL_MASK);
}

static void mtk_dp_audio_setup_channels(struct mtk_dp *mtk_dp,
					struct mtk_dp_audio_cfg *cfg)
{
	u32 channel_enable_bits;

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3324,
			   AUDIO_SOURCE_MUX_DP_ENC1_P0_DPRX,
			   AUDIO_SOURCE_MUX_DP_ENC1_P0_MASK);

	/* audio channel count change reset */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_33F4,
			   DP_ENC_DUMMY_RW_1, DP_ENC_DUMMY_RW_1);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3304,
			   AU_PRTY_REGEN_DP_ENC1_P0_MASK |
			   AU_CH_STS_REGEN_DP_ENC1_P0_MASK |
			   AUDIO_SAMPLE_PRSENT_REGEN_DP_ENC1_P0_MASK,
			   AU_PRTY_REGEN_DP_ENC1_P0_MASK |
			   AU_CH_STS_REGEN_DP_ENC1_P0_MASK |
			   AUDIO_SAMPLE_PRSENT_REGEN_DP_ENC1_P0_MASK);

	switch (cfg->channels) {
	case 2:
		channel_enable_bits = AUDIO_2CH_SEL_DP_ENC0_P0_MASK |
				      AUDIO_2CH_EN_DP_ENC0_P0_MASK;
		break;
	case 8:
	default:
		channel_enable_bits = AUDIO_8CH_SEL_DP_ENC0_P0_MASK |
				      AUDIO_8CH_EN_DP_ENC0_P0_MASK;
		break;
	}
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3088,
			   channel_enable_bits | AU_EN_DP_ENC0_P0,
			   AUDIO_2CH_SEL_DP_ENC0_P0_MASK |
			   AUDIO_2CH_EN_DP_ENC0_P0_MASK |
			   AUDIO_8CH_SEL_DP_ENC0_P0_MASK |
			   AUDIO_8CH_EN_DP_ENC0_P0_MASK |
			   AU_EN_DP_ENC0_P0);

	/* audio channel count change reset */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_33F4, 0, DP_ENC_DUMMY_RW_1);

	/* enable audio reset */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_33F4,
			   DP_ENC_DUMMY_RW_1_AUDIO_RST_EN,
			   DP_ENC_DUMMY_RW_1_AUDIO_RST_EN);
}

static void mtk_dp_audio_channel_status_set(struct mtk_dp *mtk_dp,
					    struct mtk_dp_audio_cfg *cfg)
{
	struct snd_aes_iec958 iec = { 0 };

	switch (cfg->sample_rate) {
	case 32000:
		iec.status[3] = IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		iec.status[3] = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		iec.status[3] = IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		iec.status[3] = IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		iec.status[3] = IEC958_AES3_CON_FS_96000;
		break;
	case 192000:
		iec.status[3] = IEC958_AES3_CON_FS_192000;
		break;
	default:
		iec.status[3] = IEC958_AES3_CON_FS_NOTID;
		break;
	}

	switch (cfg->word_length_bits) {
	case 16:
		iec.status[4] = IEC958_AES4_CON_WORDLEN_20_16;
		break;
	case 20:
		iec.status[4] = IEC958_AES4_CON_WORDLEN_20_16 |
				IEC958_AES4_CON_MAX_WORDLEN_24;
		break;
	case 24:
		iec.status[4] = IEC958_AES4_CON_WORDLEN_24_20 |
				IEC958_AES4_CON_MAX_WORDLEN_24;
		break;
	default:
		iec.status[4] = IEC958_AES4_CON_WORDLEN_NOTID;
	}

	/* IEC 60958 consumer channel status bits */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_308C,
			   0, CH_STATUS_0_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3090,
			   iec.status[3] << 8, CH_STATUS_1_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3094,
			   iec.status[4], CH_STATUS_2_DP_ENC0_P0_MASK);
}

static void mtk_dp_audio_sdp_asp_set_channels(struct mtk_dp *mtk_dp,
					      int channels)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_312C,
			   (min(8, channels) - 1) << 8,
			   ASP_HB2_DP_ENC0_P0_MASK | ASP_HB3_DP_ENC0_P0_MASK);
}

static void mtk_dp_audio_set_divider(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_30BC,
			   AUDIO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_DIV_2,
			   AUDIO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_MASK);
}

static void mtk_dp_sdp_trigger_aui(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3280,
			   MTK_DP_SDP_AUI, SDP_PACKET_TYPE_DP_ENC1_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3280,
			   SDP_PACKET_W_DP_ENC1_P0, SDP_PACKET_W_DP_ENC1_P0);
}

static void mtk_dp_sdp_set_data(struct mtk_dp *mtk_dp, u8 *data_bytes)
{
	mtk_dp_bulk_16bit_write(mtk_dp, MTK_DP_ENC1_P0_3200,
				data_bytes, 0x10);
}

static void mtk_dp_sdp_set_header_aui(struct mtk_dp *mtk_dp,
				      struct dp_sdp_header *header)
{
	u32 db_addr = MTK_DP_ENC0_P0_30D8 + (MTK_DP_SDP_AUI - 1) * 8;

	mtk_dp_bulk_16bit_write(mtk_dp, db_addr, (u8 *)header, 4);
}

static void mtk_dp_disable_sdp_aui(struct mtk_dp *mtk_dp)
{
	/* Disable periodic send */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_30A8 & 0xfffc, 0,
			   0xff << ((MTK_DP_ENC0_P0_30A8 & 3) * 8));
}

static void mtk_dp_setup_sdp_aui(struct mtk_dp *mtk_dp,
				 struct dp_sdp *sdp)
{
	u32 shift;

	mtk_dp_sdp_set_data(mtk_dp, sdp->db);
	mtk_dp_sdp_set_header_aui(mtk_dp, &sdp->sdp_header);
	mtk_dp_disable_sdp_aui(mtk_dp);

	shift = (MTK_DP_ENC0_P0_30A8 & 3) * 8;

	mtk_dp_sdp_trigger_aui(mtk_dp);
	/* Enable periodic sending */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_30A8 & 0xfffc,
			   0x05 << shift, 0xff << shift);
}

static void mtk_dp_aux_irq_clear(struct mtk_dp *mtk_dp)
{
	mtk_dp_write(mtk_dp, MTK_DP_AUX_P0_3640, DP_AUX_P0_3640_VAL);
}

static void mtk_dp_aux_set_cmd(struct mtk_dp *mtk_dp, u8 cmd, u32 addr)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3644,
			   cmd, MCU_REQUEST_COMMAND_AUX_TX_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3648,
			   addr, MCU_REQUEST_ADDRESS_LSB_AUX_TX_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_364C,
			   addr >> 16, MCU_REQUEST_ADDRESS_MSB_AUX_TX_P0_MASK);
}

static void mtk_dp_aux_clear_fifo(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3650,
			   MCU_ACK_TRAN_COMPLETE_AUX_TX_P0,
			   MCU_ACK_TRAN_COMPLETE_AUX_TX_P0 |
			   PHY_FIFO_RST_AUX_TX_P0_MASK |
			   MCU_REQ_DATA_NUM_AUX_TX_P0_MASK);
}

static void mtk_dp_aux_request_ready(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3630,
			   AUX_TX_REQUEST_READY_AUX_TX_P0,
			   AUX_TX_REQUEST_READY_AUX_TX_P0);
}

static void mtk_dp_aux_fill_write_fifo(struct mtk_dp *mtk_dp, u8 *buf,
				       size_t length)
{
	mtk_dp_bulk_16bit_write(mtk_dp, MTK_DP_AUX_P0_3708, buf, length);
}

static void mtk_dp_aux_read_rx_fifo(struct mtk_dp *mtk_dp, u8 *buf,
				    size_t length, int read_delay)
{
	int read_pos;

	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3620,
			   0, AUX_RD_MODE_AUX_TX_P0_MASK);

	for (read_pos = 0; read_pos < length; read_pos++) {
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3620,
				   AUX_RX_FIFO_READ_PULSE_TX_P0,
				   AUX_RX_FIFO_READ_PULSE_TX_P0);

		/* Hardware needs time to update the data */
		usleep_range(read_delay, read_delay * 2);
		buf[read_pos] = (u8)(mtk_dp_read(mtk_dp, MTK_DP_AUX_P0_3620) &
				     AUX_RX_FIFO_READ_DATA_AUX_TX_P0_MASK);
	}
}

static void mtk_dp_aux_set_length(struct mtk_dp *mtk_dp, size_t length)
{
	if (length > 0) {
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3650,
				   (length - 1) << 12,
				   MCU_REQ_DATA_NUM_AUX_TX_P0_MASK);
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_362C,
				   0,
				   AUX_NO_LENGTH_AUX_TX_P0 |
				   AUX_TX_AUXTX_OV_EN_AUX_TX_P0_MASK |
				   AUX_RESERVED_RW_0_AUX_TX_P0_MASK);
	} else {
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_362C,
				   AUX_NO_LENGTH_AUX_TX_P0,
				   AUX_NO_LENGTH_AUX_TX_P0 |
				   AUX_TX_AUXTX_OV_EN_AUX_TX_P0_MASK |
				   AUX_RESERVED_RW_0_AUX_TX_P0_MASK);
	}
}

static int mtk_dp_aux_wait_for_completion(struct mtk_dp *mtk_dp, bool is_read)
{
	int wait_reply = MTK_DP_AUX_WAIT_REPLY_COUNT;

	while (--wait_reply) {
		u32 aux_irq_status;

		if (is_read) {
			u32 fifo_status = mtk_dp_read(mtk_dp, MTK_DP_AUX_P0_3618);

			if (fifo_status &
			    (AUX_RX_FIFO_WRITE_POINTER_AUX_TX_P0_MASK |
			     AUX_RX_FIFO_FULL_AUX_TX_P0_MASK)) {
				return 0;
			}
		}

		aux_irq_status = mtk_dp_read(mtk_dp, MTK_DP_AUX_P0_3640);
		if (aux_irq_status & AUX_RX_AUX_RECV_COMPLETE_IRQ_AUX_TX_P0)
			return 0;

		if (aux_irq_status & AUX_400US_TIMEOUT_IRQ_AUX_TX_P0)
			return -ETIMEDOUT;

		/* Give the hardware a chance to reach completion before retrying */
		usleep_range(100, 500);
	}

	return -ETIMEDOUT;
}

static int mtk_dp_aux_do_transfer(struct mtk_dp *mtk_dp, bool is_read, u8 cmd,
				  u32 addr, u8 *buf, size_t length, u8 *reply_cmd)
{
	int ret;

	if (is_read && (length > DP_AUX_MAX_PAYLOAD_BYTES ||
			(cmd == DP_AUX_NATIVE_READ && !length)))
		return -EINVAL;

	if (!is_read)
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3704,
				   AUX_TX_FIFO_NEW_MODE_EN_AUX_TX_P0,
				   AUX_TX_FIFO_NEW_MODE_EN_AUX_TX_P0);

	/* We need to clear fifo and irq before sending commands to the sink device. */
	mtk_dp_aux_clear_fifo(mtk_dp);
	mtk_dp_aux_irq_clear(mtk_dp);

	mtk_dp_aux_set_cmd(mtk_dp, cmd, addr);
	mtk_dp_aux_set_length(mtk_dp, length);

	if (!is_read) {
		if (length)
			mtk_dp_aux_fill_write_fifo(mtk_dp, buf, length);

		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3704,
				   AUX_TX_FIFO_WDATA_NEW_MODE_T_AUX_TX_P0_MASK,
				   AUX_TX_FIFO_WDATA_NEW_MODE_T_AUX_TX_P0_MASK);
	}

	mtk_dp_aux_request_ready(mtk_dp);

	/* Wait for feedback from sink device. */
	ret = mtk_dp_aux_wait_for_completion(mtk_dp, is_read);

	*reply_cmd = mtk_dp_read(mtk_dp, MTK_DP_AUX_P0_3624) &
		     AUX_RX_REPLY_COMMAND_AUX_TX_P0_MASK;

	if (ret) {
		u32 phy_status = mtk_dp_read(mtk_dp, MTK_DP_AUX_P0_3628) &
				 AUX_RX_PHY_STATE_AUX_TX_P0_MASK;
		if (phy_status != AUX_RX_PHY_STATE_AUX_TX_P0_RX_IDLE) {
			dev_err(mtk_dp->dev,
				"AUX Rx Aux hang, need SW reset\n");
			return -EIO;
		}

		return -ETIMEDOUT;
	}

	if (!length) {
		mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_362C,
				   0,
				   AUX_NO_LENGTH_AUX_TX_P0 |
				   AUX_TX_AUXTX_OV_EN_AUX_TX_P0_MASK |
				   AUX_RESERVED_RW_0_AUX_TX_P0_MASK);
	} else if (is_read) {
		int read_delay;

		if (cmd == (DP_AUX_I2C_READ | DP_AUX_I2C_MOT) ||
		    cmd == DP_AUX_I2C_READ)
			read_delay = 500;
		else
			read_delay = 100;

		mtk_dp_aux_read_rx_fifo(mtk_dp, buf, length, read_delay);
	}

	return 0;
}

static void mtk_dp_set_swing_pre_emphasis(struct mtk_dp *mtk_dp, int lane_num,
					  int swing_val, int preemphasis)
{
	u32 lane_shift = lane_num * DP_TX1_VOLT_SWING_SHIFT;

	dev_dbg(mtk_dp->dev,
		"link training: swing_val = 0x%x, pre-emphasis = 0x%x\n",
		swing_val, preemphasis);

	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_SWING_EMP,
			   swing_val << (DP_TX0_VOLT_SWING_SHIFT + lane_shift),
			   DP_TX0_VOLT_SWING_MASK << lane_shift);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_SWING_EMP,
			   preemphasis << (DP_TX0_PRE_EMPH_SHIFT + lane_shift),
			   DP_TX0_PRE_EMPH_MASK << lane_shift);
}

static void mtk_dp_reset_swing_pre_emphasis(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_SWING_EMP,
			   0,
			   DP_TX0_VOLT_SWING_MASK |
			   DP_TX1_VOLT_SWING_MASK |
			   DP_TX2_VOLT_SWING_MASK |
			   DP_TX3_VOLT_SWING_MASK |
			   DP_TX0_PRE_EMPH_MASK |
			   DP_TX1_PRE_EMPH_MASK |
			   DP_TX2_PRE_EMPH_MASK |
			   DP_TX3_PRE_EMPH_MASK);
}

static u32 mtk_dp_swirq_get_clear(struct mtk_dp *mtk_dp)
{
	u32 irq_status = mtk_dp_read(mtk_dp, MTK_DP_TRANS_P0_35D0) &
			 SW_IRQ_FINAL_STATUS_DP_TRANS_P0_MASK;

	if (irq_status) {
		mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_35C8,
				   irq_status, SW_IRQ_CLR_DP_TRANS_P0_MASK);
		mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_35C8,
				   0, SW_IRQ_CLR_DP_TRANS_P0_MASK);
	}

	return irq_status;
}

static u32 mtk_dp_hwirq_get_clear(struct mtk_dp *mtk_dp)
{
	u32 irq_status = (mtk_dp_read(mtk_dp, MTK_DP_TRANS_P0_3418) &
			  IRQ_STATUS_DP_TRANS_P0_MASK) >> 12;

	if (irq_status) {
		mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3418,
				   irq_status, IRQ_CLR_DP_TRANS_P0_MASK);
		mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3418,
				   0, IRQ_CLR_DP_TRANS_P0_MASK);
	}

	return irq_status;
}

static void mtk_dp_hwirq_enable(struct mtk_dp *mtk_dp, bool enable)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3418,
			   enable ? 0 :
			   IRQ_MASK_DP_TRANS_P0_DISC_IRQ |
			   IRQ_MASK_DP_TRANS_P0_CONN_IRQ |
			   IRQ_MASK_DP_TRANS_P0_INT_IRQ,
			   IRQ_MASK_DP_TRANS_P0_MASK);
}

static void mtk_dp_initialize_settings(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_342C,
			   XTAL_FREQ_DP_TRANS_P0_DEFAULT,
			   XTAL_FREQ_DP_TRANS_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3540,
			   FEC_CLOCK_EN_MODE_DP_TRANS_P0,
			   FEC_CLOCK_EN_MODE_DP_TRANS_P0);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_31EC,
			   AUDIO_CH_SRC_SEL_DP_ENC0_P0,
			   AUDIO_CH_SRC_SEL_DP_ENC0_P0);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_304C,
			   0, SDP_VSYNC_RISING_MASK_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_IRQ_MASK,
			   IRQ_MASK_AUX_TOP_IRQ, IRQ_MASK_AUX_TOP_IRQ);
}

static void mtk_dp_initialize_hpd_detect_settings(struct mtk_dp *mtk_dp)
{
	u32 val;
	/* Debounce threshold */
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3410,
			   8, HPD_DEB_THD_DP_TRANS_P0_MASK);

	val = (HPD_INT_THD_DP_TRANS_P0_LOWER_500US |
	       HPD_INT_THD_DP_TRANS_P0_UPPER_1100US) << 4;
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3410,
			   val, HPD_INT_THD_DP_TRANS_P0_MASK);

	/*
	 * Connect threshold 1.5ms + 5 x 0.1ms = 2ms
	 * Disconnect threshold 1.5ms + 5 x 0.1ms = 2ms
	 */
	val = (5 << 8) | (5 << 12);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3410,
			   val,
			   HPD_DISC_THD_DP_TRANS_P0_MASK |
			   HPD_CONN_THD_DP_TRANS_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3430,
			   HPD_INT_THD_ECO_DP_TRANS_P0_HIGH_BOUND_EXT,
			   HPD_INT_THD_ECO_DP_TRANS_P0_MASK);
}

static void mtk_dp_initialize_aux_settings(struct mtk_dp *mtk_dp)
{
	/* modify timeout threshold = 0x1595 */
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_360C,
			   AUX_TIMEOUT_THR_AUX_TX_P0_VAL,
			   AUX_TIMEOUT_THR_AUX_TX_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3658,
			   0, AUX_TX_OV_EN_AUX_TX_P0_MASK);
	/* 25 for 26M */
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3634,
			   AUX_TX_OVER_SAMPLE_RATE_FOR_26M << 8,
			   AUX_TX_OVER_SAMPLE_RATE_AUX_TX_P0_MASK);
	/* 13 for 26M */
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3614,
			   AUX_RX_UI_CNT_THR_AUX_FOR_26M,
			   AUX_RX_UI_CNT_THR_AUX_TX_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_37C8,
			   MTK_ATOP_EN_AUX_TX_P0,
			   MTK_ATOP_EN_AUX_TX_P0);
}

static void mtk_dp_initialize_digital_settings(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_304C,
			   0, VBID_VIDEO_MUTE_DP_ENC0_P0_MASK);

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3368,
			   BS2BS_MODE_DP_ENC1_P0_VAL << 12,
			   BS2BS_MODE_DP_ENC1_P0_MASK);

	/* dp tx encoder reset all sw */
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3004,
			   DP_TX_ENCODER_4P_RESET_SW_DP_ENC0_P0,
			   DP_TX_ENCODER_4P_RESET_SW_DP_ENC0_P0);

	/* Wait for sw reset to complete */
	usleep_range(1000, 5000);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3004,
			   0, DP_TX_ENCODER_4P_RESET_SW_DP_ENC0_P0);
}

static void mtk_dp_digital_sw_reset(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_340C,
			   DP_TX_TRANSMITTER_4P_RESET_SW_DP_TRANS_P0,
			   DP_TX_TRANSMITTER_4P_RESET_SW_DP_TRANS_P0);

	/* Wait for sw reset to complete */
	usleep_range(1000, 5000);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_340C,
			   0, DP_TX_TRANSMITTER_4P_RESET_SW_DP_TRANS_P0);
}

static void mtk_dp_set_lanes(struct mtk_dp *mtk_dp, int lanes)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_35F0,
			   lanes == 0 ? 0 : DP_TRANS_DUMMY_RW_0,
			   DP_TRANS_DUMMY_RW_0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3000,
			   lanes, LANE_NUM_DP_ENC0_P0_MASK);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_34A4,
			   lanes << 2, LANE_NUM_DP_TRANS_P0_MASK);
}

static void mtk_dp_get_calibration_data(struct mtk_dp *mtk_dp)
{
	const struct mtk_dp_efuse_fmt *fmt;
	struct device *dev = mtk_dp->dev;
	struct nvmem_cell *cell;
	u32 *cal_data = mtk_dp->cal_data;
	u32 *buf;
	int i;
	size_t len;

	cell = nvmem_cell_get(dev, "dp_calibration_data");
	if (IS_ERR(cell)) {
		dev_warn(dev, "Failed to get nvmem cell dp_calibration_data\n");
		goto use_default_val;
	}

	buf = (u32 *)nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(buf) || ((len / sizeof(u32)) != 4)) {
		dev_warn(dev, "Failed to read nvmem_cell_read\n");

		if (!IS_ERR(buf))
			kfree(buf);

		goto use_default_val;
	}

	for (i = 0; i < MTK_DP_CAL_MAX; i++) {
		fmt = &mtk_dp->data->efuse_fmt[i];
		cal_data[i] = (buf[fmt->idx] >> fmt->shift) & fmt->mask;

		if (cal_data[i] < fmt->min_val || cal_data[i] > fmt->max_val) {
			dev_warn(mtk_dp->dev, "Invalid efuse data, idx = %d\n", i);
			kfree(buf);
			goto use_default_val;
		}
	}
	kfree(buf);

	return;

use_default_val:
	dev_warn(mtk_dp->dev, "Use default calibration data\n");
	for (i = 0; i < MTK_DP_CAL_MAX; i++)
		cal_data[i] = mtk_dp->data->efuse_fmt[i].default_val;
}

static void mtk_dp_set_calibration_data(struct mtk_dp *mtk_dp)
{
	u32 *cal_data = mtk_dp->cal_data;

	mtk_dp_update_bits(mtk_dp, DP_PHY_GLB_DPAUX_TX,
			   cal_data[MTK_DP_CAL_CLKTX_IMPSE] << 20,
			   RG_CKM_PT0_CKTX_IMPSEL);
	mtk_dp_update_bits(mtk_dp, DP_PHY_GLB_BIAS_GEN_00,
			   cal_data[MTK_DP_CAL_GLB_BIAS_TRIM] << 16,
			   RG_XTP_GLB_BIAS_INTR_CTRL);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_0,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_0] << 12,
			   RG_XTP_LN0_TX_IMPSEL_PMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_0,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_0] << 16,
			   RG_XTP_LN0_TX_IMPSEL_NMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_1,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_1] << 12,
			   RG_XTP_LN1_TX_IMPSEL_PMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_1,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_1] << 16,
			   RG_XTP_LN1_TX_IMPSEL_NMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_2,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_2] << 12,
			   RG_XTP_LN2_TX_IMPSEL_PMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_2,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_2] << 16,
			   RG_XTP_LN2_TX_IMPSEL_NMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_3,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_PMOS_3] << 12,
			   RG_XTP_LN3_TX_IMPSEL_PMOS);
	mtk_dp_update_bits(mtk_dp, DP_PHY_LANE_TX_3,
			   cal_data[MTK_DP_CAL_LN_TX_IMPSEL_NMOS_3] << 16,
			   RG_XTP_LN3_TX_IMPSEL_NMOS);
}

static int mtk_dp_phy_configure(struct mtk_dp *mtk_dp,
				u32 link_rate, int lane_count)
{
	int ret;
	union phy_configure_opts phy_opts = {
		.dp = {
			.link_rate = drm_dp_bw_code_to_link_rate(link_rate) / 100,
			.set_rate = 1,
			.lanes = lane_count,
			.set_lanes = 1,
			.ssc = mtk_dp->train_info.sink_ssc,
		}
	};

	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE, DP_PWR_STATE_BANDGAP,
			   DP_PWR_STATE_MASK);

	ret = phy_configure(mtk_dp->phy, &phy_opts);
	if (ret)
		return ret;

	mtk_dp_set_calibration_data(mtk_dp);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
			   DP_PWR_STATE_BANDGAP_TPLL_LANE, DP_PWR_STATE_MASK);

	return 0;
}

static void mtk_dp_set_idle_pattern(struct mtk_dp *mtk_dp, bool enable)
{
	u32 val = POST_MISC_DATA_LANE0_OV_DP_TRANS_P0_MASK |
		  POST_MISC_DATA_LANE1_OV_DP_TRANS_P0_MASK |
		  POST_MISC_DATA_LANE2_OV_DP_TRANS_P0_MASK |
		  POST_MISC_DATA_LANE3_OV_DP_TRANS_P0_MASK;

	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3580,
			   enable ? val : 0, val);
}

static void mtk_dp_train_set_pattern(struct mtk_dp *mtk_dp, int pattern)
{
	/* TPS1 */
	if (pattern == 1)
		mtk_dp_set_idle_pattern(mtk_dp, false);

	mtk_dp_update_bits(mtk_dp,
			   MTK_DP_TRANS_P0_3400,
			   pattern ? BIT(pattern - 1) << 12 : 0,
			   PATTERN1_EN_DP_TRANS_P0_MASK |
			   PATTERN2_EN_DP_TRANS_P0_MASK |
			   PATTERN3_EN_DP_TRANS_P0_MASK |
			   PATTERN4_EN_DP_TRANS_P0_MASK);
}

static void mtk_dp_set_enhanced_frame_mode(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3000,
			   ENHANCED_FRAME_EN_DP_ENC0_P0,
			   ENHANCED_FRAME_EN_DP_ENC0_P0);
}

static void mtk_dp_training_set_scramble(struct mtk_dp *mtk_dp, bool enable)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TRANS_P0_3404,
			   enable ? DP_SCR_EN_DP_TRANS_P0_MASK : 0,
			   DP_SCR_EN_DP_TRANS_P0_MASK);
}

static void mtk_dp_video_mute(struct mtk_dp *mtk_dp, bool enable)
{
	struct arm_smccc_res res;
	u32 val = VIDEO_MUTE_SEL_DP_ENC0_P0 |
		  (enable ? VIDEO_MUTE_SW_DP_ENC0_P0 : 0);

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3000,
			   val,
			   VIDEO_MUTE_SEL_DP_ENC0_P0 |
			   VIDEO_MUTE_SW_DP_ENC0_P0);

	arm_smccc_smc(MTK_DP_SIP_CONTROL_AARCH32,
		      mtk_dp->data->smc_cmd, enable,
		      0, 0, 0, 0, 0, &res);

	dev_dbg(mtk_dp->dev, "smc cmd: 0x%x, p1: %s, ret: 0x%lx-0x%lx\n",
		mtk_dp->data->smc_cmd, enable ? "enable" : "disable", res.a0, res.a1);
}

static void mtk_dp_audio_mute(struct mtk_dp *mtk_dp, bool mute)
{
	u32 val[3];

	if (mute) {
		val[0] = VBID_AUDIO_MUTE_FLAG_SW_DP_ENC0_P0 |
			 VBID_AUDIO_MUTE_FLAG_SEL_DP_ENC0_P0;
		val[1] = 0;
		val[2] = 0;
	} else {
		val[0] = 0;
		val[1] = AU_EN_DP_ENC0_P0;
		/* Send one every two frames */
		val[2] = 0x0F;
	}

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3030,
			   val[0],
			   VBID_AUDIO_MUTE_FLAG_SW_DP_ENC0_P0 |
			   VBID_AUDIO_MUTE_FLAG_SEL_DP_ENC0_P0);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3088,
			   val[1], AU_EN_DP_ENC0_P0);
	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_30A4,
			   val[2], AU_TS_CFG_DP_ENC0_P0_MASK);
}

static void mtk_dp_power_enable(struct mtk_dp *mtk_dp)
{
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_RESET_AND_PROBE,
			   0, SW_RST_B_PHYD);

	/* Wait for power enable */
	usleep_range(10, 200);

	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_RESET_AND_PROBE,
			   SW_RST_B_PHYD, SW_RST_B_PHYD);
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
			   DP_PWR_STATE_BANDGAP_TPLL, DP_PWR_STATE_MASK);
	mtk_dp_write(mtk_dp, MTK_DP_1040,
		     RG_DPAUX_RX_VALID_DEGLITCH_EN | RG_XTP_GLB_CKDET_EN |
		     RG_DPAUX_RX_EN);
	mtk_dp_update_bits(mtk_dp, MTK_DP_0034, 0, DA_CKM_CKTX0_EN_FORCE_EN);
}

static void mtk_dp_power_disable(struct mtk_dp *mtk_dp)
{
	mtk_dp_write(mtk_dp, MTK_DP_TOP_PWR_STATE, 0);

	mtk_dp_update_bits(mtk_dp, MTK_DP_0034,
			   DA_CKM_CKTX0_EN_FORCE_EN, DA_CKM_CKTX0_EN_FORCE_EN);

	/* Disable RX */
	mtk_dp_write(mtk_dp, MTK_DP_1040, 0);
	mtk_dp_write(mtk_dp, MTK_DP_TOP_MEM_PD,
		     0x550 | FUSE_SEL | MEM_ISO_EN);
}

static void mtk_dp_initialize_priv_data(struct mtk_dp *mtk_dp)
{
	mtk_dp->train_info.link_rate = DP_LINK_BW_5_4;
	mtk_dp->train_info.lane_count = mtk_dp->max_lanes;
	mtk_dp->train_info.cable_plugged_in = false;

	mtk_dp->info.format = DP_PIXELFORMAT_RGB;
	memset(&mtk_dp->info.vm, 0, sizeof(struct videomode));
	mtk_dp->audio_enable = false;
}

static void mtk_dp_sdp_set_down_cnt_init(struct mtk_dp *mtk_dp,
					 u32 sram_read_start)
{
	u32 sdp_down_cnt_init = 0;
	struct drm_display_mode mode;
	struct videomode *vm = &mtk_dp->info.vm;

	drm_display_mode_from_videomode(vm, &mode);

	if (mode.clock > 0)
		sdp_down_cnt_init = sram_read_start *
				    mtk_dp->train_info.link_rate * 2700 * 8 /
				    (mode.clock * 4);

	switch (mtk_dp->train_info.lane_count) {
	case 1:
		sdp_down_cnt_init = max_t(u32, sdp_down_cnt_init, 0x1A);
		break;
	case 2:
		/* case for LowResolution && High Audio Sample Rate */
		sdp_down_cnt_init = max_t(u32, sdp_down_cnt_init, 0x10);
		sdp_down_cnt_init += mode.vtotal <= 525 ? 4 : 0;
		break;
	case 4:
	default:
		sdp_down_cnt_init = max_t(u32, sdp_down_cnt_init, 6);
		break;
	}

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC0_P0_3040,
			   sdp_down_cnt_init,
			   SDP_DOWN_CNT_INIT_DP_ENC0_P0_MASK);
}

static void mtk_dp_sdp_set_down_cnt_init_in_hblank(struct mtk_dp *mtk_dp)
{
	int pix_clk_mhz;
	u32 dc_offset;
	u32 spd_down_cnt_init = 0;
	struct drm_display_mode mode;
	struct videomode *vm = &mtk_dp->info.vm;

	drm_display_mode_from_videomode(vm, &mode);

	pix_clk_mhz = mtk_dp->info.format == DP_PIXELFORMAT_YUV420 ?
		      mode.clock / 2000 : mode.clock / 1000;

	switch (mtk_dp->train_info.lane_count) {
	case 1:
		spd_down_cnt_init = 0x20;
		break;
	case 2:
		dc_offset = (mode.vtotal <= 525) ? 0x14 : 0x00;
		spd_down_cnt_init = 0x18 + dc_offset;
		break;
	case 4:
	default:
		dc_offset = (mode.vtotal <= 525) ? 0x08 : 0x00;
		if (pix_clk_mhz > mtk_dp->train_info.link_rate * 27)
			spd_down_cnt_init = 0x8;
		else
			spd_down_cnt_init = 0x10 + dc_offset;
		break;
	}

	mtk_dp_update_bits(mtk_dp, MTK_DP_ENC1_P0_3364, spd_down_cnt_init,
			   SDP_DOWN_CNT_INIT_IN_HBLANK_DP_ENC1_P0_MASK);
}

static void mtk_dp_setup_tu(struct mtk_dp *mtk_dp)
{
	u32 sram_read_start = min_t(u32, MTK_DP_TBC_BUF_READ_START_ADDR,
				    mtk_dp->info.vm.hactive /
				    mtk_dp->train_info.lane_count /
				    MTK_DP_4P1T / MTK_DP_HDE /
				    MTK_DP_PIX_PER_ADDR);
	mtk_dp_set_sram_read_start(mtk_dp, sram_read_start);
	mtk_dp_setup_encoder(mtk_dp);
	mtk_dp_sdp_set_down_cnt_init_in_hblank(mtk_dp);
	mtk_dp_sdp_set_down_cnt_init(mtk_dp, sram_read_start);
}

static void mtk_dp_set_tx_out(struct mtk_dp *mtk_dp)
{
	mtk_dp_setup_tu(mtk_dp);
}

static void mtk_dp_train_update_swing_pre(struct mtk_dp *mtk_dp, int lanes,
					  u8 dpcd_adjust_req[2])
{
	int lane;

	for (lane = 0; lane < lanes; ++lane) {
		u8 val;
		u8 swing;
		u8 preemphasis;
		int index = lane / 2;
		int shift = lane % 2 ? DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT : 0;

		swing = (dpcd_adjust_req[index] >> shift) &
			DP_ADJUST_VOLTAGE_SWING_LANE0_MASK;
		preemphasis = ((dpcd_adjust_req[index] >> shift) &
			       DP_ADJUST_PRE_EMPHASIS_LANE0_MASK) >>
			      DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
		val = swing << DP_TRAIN_VOLTAGE_SWING_SHIFT |
		      preemphasis << DP_TRAIN_PRE_EMPHASIS_SHIFT;

		if (swing == DP_TRAIN_VOLTAGE_SWING_LEVEL_3)
			val |= DP_TRAIN_MAX_SWING_REACHED;
		if (preemphasis == 3)
			val |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		mtk_dp_set_swing_pre_emphasis(mtk_dp, lane, swing, preemphasis);
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_TRAINING_LANE0_SET + lane,
				   val);
	}
}

static void mtk_dp_pattern(struct mtk_dp *mtk_dp, bool is_tps1)
{
	int pattern;
	unsigned int aux_offset;

	if (is_tps1) {
		pattern = 1;
		aux_offset = DP_LINK_SCRAMBLING_DISABLE | DP_TRAINING_PATTERN_1;
	} else {
		aux_offset = mtk_dp->train_info.channel_eq_pattern;

		switch (mtk_dp->train_info.channel_eq_pattern) {
		case DP_TRAINING_PATTERN_4:
			pattern = 4;
			break;
		case DP_TRAINING_PATTERN_3:
			pattern = 3;
			aux_offset |= DP_LINK_SCRAMBLING_DISABLE;
			break;
		case DP_TRAINING_PATTERN_2:
		default:
			pattern = 2;
			aux_offset |= DP_LINK_SCRAMBLING_DISABLE;
			break;
		}
	}

	mtk_dp_train_set_pattern(mtk_dp, pattern);
	drm_dp_dpcd_writeb(&mtk_dp->aux, DP_TRAINING_PATTERN_SET, aux_offset);
}

static int mtk_dp_train_setting(struct mtk_dp *mtk_dp, u8 target_link_rate,
				u8 target_lane_count)
{
	int ret;

	drm_dp_dpcd_writeb(&mtk_dp->aux, DP_LINK_BW_SET, target_link_rate);
	drm_dp_dpcd_writeb(&mtk_dp->aux, DP_LANE_COUNT_SET,
			   target_lane_count | DP_LANE_COUNT_ENHANCED_FRAME_EN);

	if (mtk_dp->train_info.sink_ssc)
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_DOWNSPREAD_CTRL,
				   DP_SPREAD_AMP_0_5);

	mtk_dp_set_lanes(mtk_dp, target_lane_count / 2);
	ret = mtk_dp_phy_configure(mtk_dp, target_link_rate, target_lane_count);
	if (ret)
		return ret;

	dev_dbg(mtk_dp->dev,
		"Link train target_link_rate = 0x%x, target_lane_count = 0x%x\n",
		target_link_rate, target_lane_count);

	return 0;
}

static int mtk_dp_train_cr(struct mtk_dp *mtk_dp, u8 target_lane_count)
{
	u8 lane_adjust[2] = {};
	u8 link_status[DP_LINK_STATUS_SIZE] = {};
	u8 prev_lane_adjust = 0xff;
	int train_retries = 0;
	int voltage_retries = 0;

	mtk_dp_pattern(mtk_dp, true);

	/* In DP spec 1.4, the retry count of CR is defined as 10. */
	do {
		train_retries++;
		if (!mtk_dp->train_info.cable_plugged_in) {
			mtk_dp_train_set_pattern(mtk_dp, 0);
			return -ENODEV;
		}

		drm_dp_dpcd_read(&mtk_dp->aux, DP_ADJUST_REQUEST_LANE0_1,
				 lane_adjust, sizeof(lane_adjust));
		mtk_dp_train_update_swing_pre(mtk_dp, target_lane_count,
					      lane_adjust);

		drm_dp_link_train_clock_recovery_delay(&mtk_dp->aux,
						       mtk_dp->rx_cap);

		/* check link status from sink device */
		drm_dp_dpcd_read_link_status(&mtk_dp->aux, link_status);
		if (drm_dp_clock_recovery_ok(link_status,
					     target_lane_count)) {
			dev_dbg(mtk_dp->dev, "Link train CR pass\n");
			return 0;
		}

		/*
		 * In DP spec 1.4, if current voltage level is the same
		 * with previous voltage level, we need to retry 5 times.
		 */
		if (prev_lane_adjust == link_status[4]) {
			voltage_retries++;
			/*
			 * Condition of CR fail:
			 * 1. Failed to pass CR using the same voltage
			 *    level over five times.
			 * 2. Failed to pass CR when the current voltage
			 *    level is the same with previous voltage
			 *    level and reach max voltage level (3).
			 */
			if (voltage_retries > MTK_DP_TRAIN_VOLTAGE_LEVEL_RETRY ||
			    (prev_lane_adjust & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK) == 3) {
				dev_dbg(mtk_dp->dev, "Link train CR fail\n");
				break;
			}
		} else {
			/*
			 * If the voltage level is changed, we need to
			 * re-calculate this retry count.
			 */
			voltage_retries = 0;
		}
		prev_lane_adjust = link_status[4];
	} while (train_retries < MTK_DP_TRAIN_DOWNSCALE_RETRY);

	/* Failed to train CR, and disable pattern. */
	drm_dp_dpcd_writeb(&mtk_dp->aux, DP_TRAINING_PATTERN_SET,
			   DP_TRAINING_PATTERN_DISABLE);
	mtk_dp_train_set_pattern(mtk_dp, 0);

	return -ETIMEDOUT;
}

static int mtk_dp_train_eq(struct mtk_dp *mtk_dp, u8 target_lane_count)
{
	u8 lane_adjust[2] = {};
	u8 link_status[DP_LINK_STATUS_SIZE] = {};
	int train_retries = 0;

	mtk_dp_pattern(mtk_dp, false);

	do {
		train_retries++;
		if (!mtk_dp->train_info.cable_plugged_in) {
			mtk_dp_train_set_pattern(mtk_dp, 0);
			return -ENODEV;
		}

		drm_dp_dpcd_read(&mtk_dp->aux, DP_ADJUST_REQUEST_LANE0_1,
				 lane_adjust, sizeof(lane_adjust));
		mtk_dp_train_update_swing_pre(mtk_dp, target_lane_count,
					      lane_adjust);

		drm_dp_link_train_channel_eq_delay(&mtk_dp->aux,
						   mtk_dp->rx_cap);

		/* check link status from sink device */
		drm_dp_dpcd_read_link_status(&mtk_dp->aux, link_status);
		if (drm_dp_channel_eq_ok(link_status, target_lane_count)) {
			dev_dbg(mtk_dp->dev, "Link train EQ pass\n");

			/* Training done, and disable pattern. */
			drm_dp_dpcd_writeb(&mtk_dp->aux, DP_TRAINING_PATTERN_SET,
					   DP_TRAINING_PATTERN_DISABLE);
			mtk_dp_train_set_pattern(mtk_dp, 0);
			return 0;
		}
		dev_dbg(mtk_dp->dev, "Link train EQ fail\n");
	} while (train_retries < MTK_DP_TRAIN_DOWNSCALE_RETRY);

	/* Failed to train EQ, and disable pattern. */
	drm_dp_dpcd_writeb(&mtk_dp->aux, DP_TRAINING_PATTERN_SET,
			   DP_TRAINING_PATTERN_DISABLE);
	mtk_dp_train_set_pattern(mtk_dp, 0);

	return -ETIMEDOUT;
}

static int mtk_dp_parse_capabilities(struct mtk_dp *mtk_dp)
{
	u8 val;
	ssize_t ret;

	ret = drm_dp_read_dpcd_caps(&mtk_dp->aux, mtk_dp->rx_cap);
	if (ret < 0)
		return ret;

	if (drm_dp_tps4_supported(mtk_dp->rx_cap))
		mtk_dp->train_info.channel_eq_pattern = DP_TRAINING_PATTERN_4;
	else if (drm_dp_tps3_supported(mtk_dp->rx_cap))
		mtk_dp->train_info.channel_eq_pattern = DP_TRAINING_PATTERN_3;
	else
		mtk_dp->train_info.channel_eq_pattern = DP_TRAINING_PATTERN_2;

	mtk_dp->train_info.sink_ssc = drm_dp_max_downspread(mtk_dp->rx_cap);

	ret = drm_dp_dpcd_readb(&mtk_dp->aux, DP_MSTM_CAP, &val);
	if (ret < 1) {
		drm_err(mtk_dp->drm_dev, "Read mstm cap failed\n");
		return ret == 0 ? -EIO : ret;
	}

	if (val & DP_MST_CAP) {
		/* Clear DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0 */
		ret = drm_dp_dpcd_readb(&mtk_dp->aux,
					DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0,
					&val);
		if (ret < 1) {
			drm_err(mtk_dp->drm_dev, "Read irq vector failed\n");
			return ret == 0 ? -EIO : ret;
		}

		if (val) {
			ret = drm_dp_dpcd_writeb(&mtk_dp->aux,
						 DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0,
						 val);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static bool mtk_dp_edid_parse_audio_capabilities(struct mtk_dp *mtk_dp,
						 struct mtk_dp_audio_cfg *cfg)
{
	if (!mtk_dp->data->audio_supported)
		return false;

	if (mtk_dp->info.audio_cur_cfg.sad_count <= 0) {
		drm_info(mtk_dp->drm_dev, "The SADs is NULL\n");
		return false;
	}

	return true;
}

static void mtk_dp_train_change_mode(struct mtk_dp *mtk_dp)
{
	phy_reset(mtk_dp->phy);
	mtk_dp_reset_swing_pre_emphasis(mtk_dp);
}

static int mtk_dp_training(struct mtk_dp *mtk_dp)
{
	int ret;
	u8 lane_count, link_rate, train_limit, max_link_rate;

	link_rate = min_t(u8, mtk_dp->max_linkrate,
			  mtk_dp->rx_cap[DP_MAX_LINK_RATE]);
	max_link_rate = link_rate;
	lane_count = min_t(u8, mtk_dp->max_lanes,
			   drm_dp_max_lane_count(mtk_dp->rx_cap));

	/*
	 * TPS are generated by the hardware pattern generator. From the
	 * hardware setting we need to disable this scramble setting before
	 * use the TPS pattern generator.
	 */
	mtk_dp_training_set_scramble(mtk_dp, false);

	for (train_limit = 6; train_limit > 0; train_limit--) {
		mtk_dp_train_change_mode(mtk_dp);

		ret = mtk_dp_train_setting(mtk_dp, link_rate, lane_count);
		if (ret)
			return ret;

		ret = mtk_dp_train_cr(mtk_dp, lane_count);
		if (ret == -ENODEV) {
			return ret;
		} else if (ret) {
			/* reduce link rate */
			switch (link_rate) {
			case DP_LINK_BW_1_62:
				lane_count = lane_count / 2;
				link_rate = max_link_rate;
				if (lane_count == 0)
					return -EIO;
				break;
			case DP_LINK_BW_2_7:
				link_rate = DP_LINK_BW_1_62;
				break;
			case DP_LINK_BW_5_4:
				link_rate = DP_LINK_BW_2_7;
				break;
			case DP_LINK_BW_8_1:
				link_rate = DP_LINK_BW_5_4;
				break;
			default:
				return -EINVAL;
			};
			continue;
		}

		ret = mtk_dp_train_eq(mtk_dp, lane_count);
		if (ret == -ENODEV) {
			return ret;
		} else if (ret) {
			/* reduce lane count */
			if (lane_count == 0)
				return -EIO;
			lane_count /= 2;
			continue;
		}

		/* if we can run to this, training is done. */
		break;
	}

	if (train_limit == 0)
		return -ETIMEDOUT;

	mtk_dp->train_info.link_rate = link_rate;
	mtk_dp->train_info.lane_count = lane_count;

	/*
	 * After training done, we need to output normal stream instead of TPS,
	 * so we need to enable scramble.
	 */
	mtk_dp_training_set_scramble(mtk_dp, true);
	mtk_dp_set_enhanced_frame_mode(mtk_dp);

	return 0;
}

static void mtk_dp_video_enable(struct mtk_dp *mtk_dp, bool enable)
{
	/* the mute sequence is different between enable and disable */
	if (enable) {
		mtk_dp_msa_bypass_enable(mtk_dp, false);
		mtk_dp_pg_enable(mtk_dp, false);
		mtk_dp_set_tx_out(mtk_dp);
		mtk_dp_video_mute(mtk_dp, false);
	} else {
		mtk_dp_video_mute(mtk_dp, true);
		mtk_dp_pg_enable(mtk_dp, true);
		mtk_dp_msa_bypass_enable(mtk_dp, true);
	}
}

static void mtk_dp_audio_sdp_setup(struct mtk_dp *mtk_dp,
				   struct mtk_dp_audio_cfg *cfg)
{
	struct dp_sdp sdp;
	struct hdmi_audio_infoframe frame;

	hdmi_audio_infoframe_init(&frame);
	frame.coding_type = HDMI_AUDIO_CODING_TYPE_PCM;
	frame.channels = cfg->channels;
	frame.sample_frequency = cfg->sample_rate;

	switch (cfg->word_length_bits) {
	case 16:
		frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_16;
		break;
	case 20:
		frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_20;
		break;
	case 24:
	default:
		frame.sample_size = HDMI_AUDIO_SAMPLE_SIZE_24;
		break;
	}

	hdmi_audio_infoframe_pack_for_dp(&frame, &sdp, MTK_DP_VERSION);

	mtk_dp_audio_sdp_asp_set_channels(mtk_dp, cfg->channels);
	mtk_dp_setup_sdp_aui(mtk_dp, &sdp);
}

static void mtk_dp_audio_setup(struct mtk_dp *mtk_dp,
			       struct mtk_dp_audio_cfg *cfg)
{
	mtk_dp_audio_sdp_setup(mtk_dp, cfg);
	mtk_dp_audio_channel_status_set(mtk_dp, cfg);

	mtk_dp_audio_setup_channels(mtk_dp, cfg);
	mtk_dp_audio_set_divider(mtk_dp);
}

static int mtk_dp_video_config(struct mtk_dp *mtk_dp)
{
	mtk_dp_config_mn_mode(mtk_dp);
	mtk_dp_set_msa(mtk_dp);
	mtk_dp_set_color_depth(mtk_dp);
	return mtk_dp_set_color_format(mtk_dp, mtk_dp->info.format);
}

static void mtk_dp_init_port(struct mtk_dp *mtk_dp)
{
	mtk_dp_set_idle_pattern(mtk_dp, true);
	mtk_dp_initialize_priv_data(mtk_dp);

	mtk_dp_initialize_settings(mtk_dp);
	mtk_dp_initialize_aux_settings(mtk_dp);
	mtk_dp_initialize_digital_settings(mtk_dp);

	mtk_dp_update_bits(mtk_dp, MTK_DP_AUX_P0_3690,
			   RX_REPLY_COMPLETE_MODE_AUX_TX_P0,
			   RX_REPLY_COMPLETE_MODE_AUX_TX_P0);
	mtk_dp_initialize_hpd_detect_settings(mtk_dp);

	mtk_dp_digital_sw_reset(mtk_dp);
}

static irqreturn_t mtk_dp_hpd_event_thread(int hpd, void *dev)
{
	struct mtk_dp *mtk_dp = dev;
	unsigned long flags;
	u32 status;

	if (mtk_dp->need_debounce && mtk_dp->train_info.cable_plugged_in)
		msleep(100);

	spin_lock_irqsave(&mtk_dp->irq_thread_lock, flags);
	status = mtk_dp->irq_thread_handle;
	mtk_dp->irq_thread_handle = 0;
	spin_unlock_irqrestore(&mtk_dp->irq_thread_lock, flags);

	if (status & MTK_DP_THREAD_CABLE_STATE_CHG) {
		if (mtk_dp->bridge.dev)
			drm_helper_hpd_irq_event(mtk_dp->bridge.dev);

		if (!mtk_dp->train_info.cable_plugged_in) {
			mtk_dp_disable_sdp_aui(mtk_dp);
			memset(&mtk_dp->info.audio_cur_cfg, 0,
			       sizeof(mtk_dp->info.audio_cur_cfg));

			mtk_dp->need_debounce = false;
			mod_timer(&mtk_dp->debounce_timer,
				  jiffies + msecs_to_jiffies(100) - 1);
		}
	}

	if (status & MTK_DP_THREAD_HPD_EVENT)
		dev_dbg(mtk_dp->dev, "Receive IRQ from sink devices\n");

	return IRQ_HANDLED;
}

static irqreturn_t mtk_dp_hpd_event(int hpd, void *dev)
{
	struct mtk_dp *mtk_dp = dev;
	bool cable_sta_chg = false;
	unsigned long flags;
	u32 irq_status = mtk_dp_swirq_get_clear(mtk_dp) |
			 mtk_dp_hwirq_get_clear(mtk_dp);

	if (!irq_status)
		return IRQ_HANDLED;

	spin_lock_irqsave(&mtk_dp->irq_thread_lock, flags);

	if (irq_status & MTK_DP_HPD_INTERRUPT)
		mtk_dp->irq_thread_handle |= MTK_DP_THREAD_HPD_EVENT;

	/* Cable state is changed. */
	if (irq_status != MTK_DP_HPD_INTERRUPT) {
		mtk_dp->irq_thread_handle |= MTK_DP_THREAD_CABLE_STATE_CHG;
		cable_sta_chg = true;
	}

	spin_unlock_irqrestore(&mtk_dp->irq_thread_lock, flags);

	if (cable_sta_chg) {
		if (!!(mtk_dp_read(mtk_dp, MTK_DP_TRANS_P0_3414) &
		       HPD_DB_DP_TRANS_P0_MASK))
			mtk_dp->train_info.cable_plugged_in = true;
		else
			mtk_dp->train_info.cable_plugged_in = false;
	}

	return IRQ_WAKE_THREAD;
}

static int mtk_dp_dt_parse(struct mtk_dp *mtk_dp,
			   struct platform_device *pdev)
{
	struct device_node *endpoint;
	struct device *dev = &pdev->dev;
	int ret;
	void __iomem *base;
	u32 linkrate;
	int len;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	mtk_dp->regs = devm_regmap_init_mmio(dev, base, &mtk_dp_regmap_config);
	if (IS_ERR(mtk_dp->regs))
		return PTR_ERR(mtk_dp->regs);

	endpoint = of_graph_get_endpoint_by_regs(pdev->dev.of_node, 1, -1);
	len = of_property_count_elems_of_size(endpoint,
					      "data-lanes", sizeof(u32));
	if (len < 0 || len > 4 || len == 3) {
		dev_err(dev, "invalid data lane size: %d\n", len);
		return -EINVAL;
	}

	mtk_dp->max_lanes = len;

	ret = device_property_read_u32(dev, "max-linkrate-mhz", &linkrate);
	if (ret) {
		dev_err(dev, "failed to read max linkrate: %d\n", ret);
		return ret;
	}

	mtk_dp->max_linkrate = drm_dp_link_rate_to_bw_code(linkrate * 100);

	return 0;
}

static void mtk_dp_update_plugged_status(struct mtk_dp *mtk_dp)
{
	mutex_lock(&mtk_dp->update_plugged_status_lock);
	if (mtk_dp->plugged_cb && mtk_dp->codec_dev)
		mtk_dp->plugged_cb(mtk_dp->codec_dev,
				   mtk_dp->enabled &
				   mtk_dp->info.audio_cur_cfg.detect_monitor);
	mutex_unlock(&mtk_dp->update_plugged_status_lock);
}

static enum drm_connector_status mtk_dp_bdg_detect(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	enum drm_connector_status ret = connector_status_disconnected;
	bool enabled = mtk_dp->enabled;
	u8 sink_count = 0;

	if (!mtk_dp->train_info.cable_plugged_in)
		return ret;

	if (!enabled) {
		/* power on aux */
		mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
				   DP_PWR_STATE_BANDGAP_TPLL_LANE,
				   DP_PWR_STATE_MASK);

		/* power on panel */
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
		usleep_range(2000, 5000);
	}
	/*
	 * Some dongles still source HPD when they do not connect to any
	 * sink device. To avoid this, we need to read the sink count
	 * to make sure we do connect to sink devices. After this detect
	 * function, we just need to check the HPD connection to check
	 * whether we connect to a sink device.
	 */
	drm_dp_dpcd_readb(&mtk_dp->aux, DP_SINK_COUNT, &sink_count);
	if (DP_GET_SINK_COUNT(sink_count))
		ret = connector_status_connected;

	if (!enabled) {
		/* power off panel */
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D3);
		usleep_range(2000, 3000);

		/* power off aux */
		mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
				   DP_PWR_STATE_BANDGAP_TPLL,
				   DP_PWR_STATE_MASK);
	}

	return ret;
}

static struct edid *mtk_dp_get_edid(struct drm_bridge *bridge,
				    struct drm_connector *connector)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	bool enabled = mtk_dp->enabled;
	struct edid *new_edid = NULL;
	struct mtk_dp_audio_cfg *audio_caps = &mtk_dp->info.audio_cur_cfg;

	if (!enabled) {
		drm_bridge_chain_pre_enable(bridge);

		/* power on aux */
		mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
				   DP_PWR_STATE_BANDGAP_TPLL_LANE,
				   DP_PWR_STATE_MASK);

		/* power on panel */
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
		usleep_range(2000, 5000);
	}

	new_edid = drm_get_edid(connector, &mtk_dp->aux.ddc);

	/*
	 * Parse capability here to let atomic_get_input_bus_fmts and
	 * mode_valid use the capability to calculate sink bitrates.
	 */
	if (mtk_dp_parse_capabilities(mtk_dp)) {
		drm_err(mtk_dp->drm_dev, "Can't parse capabilities\n");
		new_edid = NULL;
	}

	if (new_edid) {
		struct cea_sad *sads;

		audio_caps->sad_count = drm_edid_to_sad(new_edid, &sads);
		kfree(sads);

		audio_caps->detect_monitor = drm_detect_monitor_audio(new_edid);
	}

	if (!enabled) {
		/* power off panel */
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D3);
		usleep_range(2000, 3000);

		/* power off aux */
		mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
				   DP_PWR_STATE_BANDGAP_TPLL,
				   DP_PWR_STATE_MASK);

		drm_bridge_chain_post_disable(bridge);
	}

	return new_edid;
}

static ssize_t mtk_dp_aux_transfer(struct drm_dp_aux *mtk_aux,
				   struct drm_dp_aux_msg *msg)
{
	struct mtk_dp *mtk_dp;
	bool is_read;
	u8 request;
	size_t accessed_bytes = 0;
	int ret;

	mtk_dp = container_of(mtk_aux, struct mtk_dp, aux);

	if (!mtk_dp->train_info.cable_plugged_in) {
		ret = -EAGAIN;
		goto err;
	}

	switch (msg->request) {
	case DP_AUX_I2C_MOT:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE | DP_AUX_I2C_MOT:
		request = msg->request & ~DP_AUX_I2C_WRITE_STATUS_UPDATE;
		is_read = false;
		break;
	case DP_AUX_I2C_READ:
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ | DP_AUX_I2C_MOT:
		request = msg->request;
		is_read = true;
		break;
	default:
		dev_err(mtk_dp->dev, "invalid aux cmd = %d\n",
			msg->request);
		ret = -EINVAL;
		goto err;
	}

	do {
		size_t to_access = min_t(size_t, DP_AUX_MAX_PAYLOAD_BYTES,
					 msg->size - accessed_bytes);

		ret = mtk_dp_aux_do_transfer(mtk_dp, is_read, request,
					     msg->address + accessed_bytes,
					     msg->buffer + accessed_bytes,
					     to_access, &msg->reply);

		if (ret) {
			dev_info(mtk_dp->dev,
				 "Failed to do AUX transfer: %d\n", ret);
			goto err;
		}
		accessed_bytes += to_access;
	} while (accessed_bytes < msg->size);

	return msg->size;
err:
	msg->reply = DP_AUX_NATIVE_REPLY_NACK | DP_AUX_I2C_REPLY_NACK;
	return ret;
}

static int mtk_dp_poweron(struct mtk_dp *mtk_dp)
{
	int ret;

	ret = phy_init(mtk_dp->phy);
	if (ret) {
		dev_err(mtk_dp->dev, "Failed to initialize phy: %d\n", ret);
		return ret;
	}

	mtk_dp_init_port(mtk_dp);
	mtk_dp_power_enable(mtk_dp);

	return 0;
}

static void mtk_dp_poweroff(struct mtk_dp *mtk_dp)
{
	mtk_dp_power_disable(mtk_dp);
	phy_exit(mtk_dp->phy);
}

static int mtk_dp_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		dev_err(mtk_dp->dev, "Driver does not provide a connector!");
		return -EINVAL;
	}

	mtk_dp->aux.drm_dev = bridge->dev;
	ret = drm_dp_aux_register(&mtk_dp->aux);
	if (ret) {
		dev_err(mtk_dp->dev,
			"failed to register DP AUX channel: %d\n", ret);
		return ret;
	}

	ret = mtk_dp_poweron(mtk_dp);
	if (ret)
		goto err_aux_register;

	if (mtk_dp->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, mtk_dp->next_bridge,
					&mtk_dp->bridge, flags);
		if (ret) {
			drm_warn(mtk_dp->drm_dev,
				 "Failed to attach external bridge: %d\n", ret);
			goto err_bridge_attach;
		}
	}

	mtk_dp->drm_dev = bridge->dev;

	mtk_dp_hwirq_enable(mtk_dp, true);

	return 0;

err_bridge_attach:
	mtk_dp_poweroff(mtk_dp);
err_aux_register:
	drm_dp_aux_unregister(&mtk_dp->aux);
	return ret;
}

static void mtk_dp_bridge_detach(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);

	mtk_dp_hwirq_enable(mtk_dp, false);
	mtk_dp->drm_dev = NULL;
	mtk_dp_poweroff(mtk_dp);
	drm_dp_aux_unregister(&mtk_dp->aux);
}

static void mtk_dp_bridge_atomic_enable(struct drm_bridge *bridge,
					struct drm_bridge_state *old_state)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	int ret;

	mtk_dp->conn = drm_atomic_get_new_connector_for_encoder(old_state->base.state,
								bridge->encoder);
	if (!mtk_dp->conn) {
		drm_err(mtk_dp->drm_dev,
			"Can't enable bridge as connector is missing\n");
		return;
	}

	/* power on aux */
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
			   DP_PWR_STATE_BANDGAP_TPLL_LANE,
			   DP_PWR_STATE_MASK);

	if (mtk_dp->train_info.cable_plugged_in) {
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D0);
		usleep_range(2000, 5000);
	}

	/* Training */
	ret = mtk_dp_training(mtk_dp);
	if (ret) {
		drm_err(mtk_dp->drm_dev, "Training failed, %d\n", ret);
		goto power_off_aux;
	}

	ret = mtk_dp_video_config(mtk_dp);
	if (ret)
		goto power_off_aux;

	mtk_dp_video_enable(mtk_dp, true);

	mtk_dp->audio_enable =
		mtk_dp_edid_parse_audio_capabilities(mtk_dp,
						     &mtk_dp->info.audio_cur_cfg);
	if (mtk_dp->audio_enable) {
		mtk_dp_audio_setup(mtk_dp, &mtk_dp->info.audio_cur_cfg);
		mtk_dp_audio_mute(mtk_dp, false);
	} else {
		memset(&mtk_dp->info.audio_cur_cfg, 0,
		       sizeof(mtk_dp->info.audio_cur_cfg));
	}

	mtk_dp->enabled = true;
	mtk_dp_update_plugged_status(mtk_dp);

	return;
power_off_aux:
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
			   DP_PWR_STATE_BANDGAP_TPLL,
			   DP_PWR_STATE_MASK);
}

static void mtk_dp_bridge_atomic_disable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_state)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);

	mtk_dp->enabled = false;
	mtk_dp_update_plugged_status(mtk_dp);
	mtk_dp_video_enable(mtk_dp, false);
	mtk_dp_audio_mute(mtk_dp, true);

	if (mtk_dp->train_info.cable_plugged_in) {
		drm_dp_dpcd_writeb(&mtk_dp->aux, DP_SET_POWER, DP_SET_POWER_D3);
		usleep_range(2000, 3000);
	}

	/* power off aux */
	mtk_dp_update_bits(mtk_dp, MTK_DP_TOP_PWR_STATE,
			   DP_PWR_STATE_BANDGAP_TPLL,
			   DP_PWR_STATE_MASK);

	/* Ensure the sink is muted */
	msleep(20);
}

static enum drm_mode_status
mtk_dp_bridge_mode_valid(struct drm_bridge *bridge,
			 const struct drm_display_info *info,
			 const struct drm_display_mode *mode)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	u32 bpp = info->color_formats & DRM_COLOR_FORMAT_YCBCR422 ? 16 : 24;
	u32 rate = min_t(u32, drm_dp_max_link_rate(mtk_dp->rx_cap) *
			      drm_dp_max_lane_count(mtk_dp->rx_cap),
			 drm_dp_bw_code_to_link_rate(mtk_dp->max_linkrate) *
			 mtk_dp->max_lanes);

	if (rate < mode->clock * bpp / 8)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static u32 *mtk_dp_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
						     struct drm_bridge_state *bridge_state,
						     struct drm_crtc_state *crtc_state,
						     struct drm_connector_state *conn_state,
						     unsigned int *num_output_fmts)
{
	u32 *output_fmts;

	*num_output_fmts = 0;
	output_fmts = kmalloc(sizeof(*output_fmts), GFP_KERNEL);
	if (!output_fmts)
		return NULL;
	*num_output_fmts = 1;
	output_fmts[0] = MEDIA_BUS_FMT_FIXED;
	return output_fmts;
}

static const u32 mt8195_input_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_YUYV8_1X16,
};

static u32 *mtk_dp_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						    struct drm_bridge_state *bridge_state,
						    struct drm_crtc_state *crtc_state,
						    struct drm_connector_state *conn_state,
						    u32 output_fmt,
						    unsigned int *num_input_fmts)
{
	u32 *input_fmts;
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct drm_display_info *display_info =
		&conn_state->connector->display_info;
	u32 rate = min_t(u32, drm_dp_max_link_rate(mtk_dp->rx_cap) *
			      drm_dp_max_lane_count(mtk_dp->rx_cap),
			 drm_dp_bw_code_to_link_rate(mtk_dp->max_linkrate) *
			 mtk_dp->max_lanes);

	*num_input_fmts = 0;

	/*
	 * If the linkrate is smaller than datarate of RGB888, larger than
	 * datarate of YUV422 and sink device supports YUV422, we output YUV422
	 * format. Use this condition, we can support more resolution.
	 */
	if ((rate < (mode->clock * 24 / 8)) &&
	    (rate > (mode->clock * 16 / 8)) &&
	    (display_info->color_formats & DRM_COLOR_FORMAT_YCBCR422)) {
		input_fmts = kcalloc(1, sizeof(*input_fmts), GFP_KERNEL);
		if (!input_fmts)
			return NULL;
		*num_input_fmts = 1;
		input_fmts[0] = MEDIA_BUS_FMT_YUYV8_1X16;
	} else {
		input_fmts = kcalloc(ARRAY_SIZE(mt8195_input_fmts),
				     sizeof(*input_fmts),
				     GFP_KERNEL);
		if (!input_fmts)
			return NULL;

		*num_input_fmts = ARRAY_SIZE(mt8195_input_fmts);
		memcpy(input_fmts, mt8195_input_fmts, sizeof(mt8195_input_fmts));
	}

	return input_fmts;
}

static int mtk_dp_bridge_atomic_check(struct drm_bridge *bridge,
				      struct drm_bridge_state *bridge_state,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct mtk_dp *mtk_dp = mtk_dp_from_bridge(bridge);
	struct drm_crtc *crtc = conn_state->crtc;
	unsigned int input_bus_format;

	input_bus_format = bridge_state->input_bus_cfg.format;

	dev_dbg(mtk_dp->dev, "input format 0x%04x, output format 0x%04x\n",
		bridge_state->input_bus_cfg.format,
		 bridge_state->output_bus_cfg.format);

	if (input_bus_format == MEDIA_BUS_FMT_YUYV8_1X16)
		mtk_dp->info.format = DP_PIXELFORMAT_YUV422;
	else
		mtk_dp->info.format = DP_PIXELFORMAT_RGB;

	if (!crtc) {
		drm_err(mtk_dp->drm_dev,
			"Can't enable bridge as connector state doesn't have a crtc\n");
		return -EINVAL;
	}

	drm_display_mode_to_videomode(&crtc_state->adjusted_mode, &mtk_dp->info.vm);

	return 0;
}

static const struct drm_bridge_funcs mtk_dp_bridge_funcs = {
	.atomic_check = mtk_dp_bridge_atomic_check,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_get_output_bus_fmts = mtk_dp_bridge_atomic_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = mtk_dp_bridge_atomic_get_input_bus_fmts,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.attach = mtk_dp_bridge_attach,
	.detach = mtk_dp_bridge_detach,
	.atomic_enable = mtk_dp_bridge_atomic_enable,
	.atomic_disable = mtk_dp_bridge_atomic_disable,
	.mode_valid = mtk_dp_bridge_mode_valid,
	.get_edid = mtk_dp_get_edid,
	.detect = mtk_dp_bdg_detect,
};

static void mtk_dp_debounce_timer(struct timer_list *t)
{
	struct mtk_dp *mtk_dp = from_timer(mtk_dp, t, debounce_timer);

	mtk_dp->need_debounce = true;
}

/*
 * HDMI audio codec callbacks
 */
static int mtk_dp_audio_hw_params(struct device *dev, void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	if (!mtk_dp->enabled) {
		dev_err(mtk_dp->dev, "%s, DP is not ready!\n", __func__);
		return -ENODEV;
	}

	mtk_dp->info.audio_cur_cfg.channels = params->cea.channels;
	mtk_dp->info.audio_cur_cfg.sample_rate = params->sample_rate;

	mtk_dp_audio_setup(mtk_dp, &mtk_dp->info.audio_cur_cfg);

	return 0;
}

static int mtk_dp_audio_startup(struct device *dev, void *data)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	mtk_dp_audio_mute(mtk_dp, false);

	return 0;
}

static void mtk_dp_audio_shutdown(struct device *dev, void *data)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	mtk_dp_audio_mute(mtk_dp, true);
}

static int mtk_dp_audio_get_eld(struct device *dev, void *data, uint8_t *buf,
				size_t len)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	if (mtk_dp->enabled)
		memcpy(buf, mtk_dp->conn->eld, len);
	else
		memset(buf, 0, len);

	return 0;
}

static int mtk_dp_audio_hook_plugged_cb(struct device *dev, void *data,
					hdmi_codec_plugged_cb fn,
					struct device *codec_dev)
{
	struct mtk_dp *mtk_dp = data;

	mutex_lock(&mtk_dp->update_plugged_status_lock);
	mtk_dp->plugged_cb = fn;
	mtk_dp->codec_dev = codec_dev;
	mutex_unlock(&mtk_dp->update_plugged_status_lock);

	mtk_dp_update_plugged_status(mtk_dp);

	return 0;
}

static const struct hdmi_codec_ops mtk_dp_audio_codec_ops = {
	.hw_params = mtk_dp_audio_hw_params,
	.audio_startup = mtk_dp_audio_startup,
	.audio_shutdown = mtk_dp_audio_shutdown,
	.get_eld = mtk_dp_audio_get_eld,
	.hook_plugged_cb = mtk_dp_audio_hook_plugged_cb,
	.no_capture_mute = 1,
};

static int mtk_dp_register_audio_driver(struct device *dev)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);
	struct hdmi_codec_pdata codec_data = {
		.ops = &mtk_dp_audio_codec_ops,
		.max_i2s_channels = 8,
		.i2s = 1,
		.data = mtk_dp,
	};

	mtk_dp->audio_pdev = platform_device_register_data(dev,
							   HDMI_CODEC_DRV_NAME,
							   PLATFORM_DEVID_AUTO,
							   &codec_data,
							   sizeof(codec_data));
	return PTR_ERR_OR_ZERO(mtk_dp->audio_pdev);
}

static int mtk_dp_probe(struct platform_device *pdev)
{
	struct mtk_dp *mtk_dp;
	struct device *dev = &pdev->dev;
	int ret, irq_num;

	mtk_dp = devm_kzalloc(dev, sizeof(*mtk_dp), GFP_KERNEL);
	if (!mtk_dp)
		return -ENOMEM;

	mtk_dp->dev = dev;
	mtk_dp->data = (struct mtk_dp_data *)of_device_get_match_data(dev);

	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0)
		return dev_err_probe(dev, irq_num,
				     "failed to request dp irq resource\n");

	mtk_dp->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(mtk_dp->next_bridge) &&
	    PTR_ERR(mtk_dp->next_bridge) == -ENODEV)
		mtk_dp->next_bridge = NULL;
	else if (IS_ERR(mtk_dp->next_bridge))
		return dev_err_probe(dev, PTR_ERR(mtk_dp->next_bridge),
				     "Failed to get bridge\n");

	ret = mtk_dp_dt_parse(mtk_dp, pdev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to parse dt\n");

	drm_dp_aux_init(&mtk_dp->aux);
	mtk_dp->aux.name = "aux_mtk_dp";
	mtk_dp->aux.transfer = mtk_dp_aux_transfer;

	spin_lock_init(&mtk_dp->irq_thread_lock);

	ret = devm_request_threaded_irq(dev, irq_num, mtk_dp_hpd_event,
					mtk_dp_hpd_event_thread,
					IRQ_TYPE_LEVEL_HIGH, dev_name(dev),
					mtk_dp);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to request mediatek dptx irq\n");

	mutex_init(&mtk_dp->update_plugged_status_lock);

	platform_set_drvdata(pdev, mtk_dp);

	if (mtk_dp->data->audio_supported) {
		ret = mtk_dp_register_audio_driver(dev);
		if (ret) {
			dev_err(dev, "Failed to register audio driver: %d\n",
				ret);
			return ret;
		}
	}

	mtk_dp->phy_dev = platform_device_register_data(dev, "mediatek-dp-phy",
							PLATFORM_DEVID_AUTO,
							&mtk_dp->regs,
							sizeof(struct regmap *));
	if (IS_ERR(mtk_dp->phy_dev))
		return dev_err_probe(dev, PTR_ERR(mtk_dp->phy_dev),
				     "Failed to create device mediatek-dp-phy\n");

	mtk_dp_get_calibration_data(mtk_dp);

	mtk_dp->phy = devm_phy_get(&mtk_dp->phy_dev->dev, "dp");

	if (IS_ERR(mtk_dp->phy)) {
		platform_device_unregister(mtk_dp->phy_dev);
		return dev_err_probe(dev, PTR_ERR(mtk_dp->phy),
				     "Failed to get phy\n");
	}

	mtk_dp->bridge.funcs = &mtk_dp_bridge_funcs;
	mtk_dp->bridge.of_node = dev->of_node;

	mtk_dp->bridge.ops =
		DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID | DRM_BRIDGE_OP_HPD;
	mtk_dp->bridge.type = mtk_dp->data->bridge_type;

	drm_bridge_add(&mtk_dp->bridge);

	mtk_dp->need_debounce = true;
	timer_setup(&mtk_dp->debounce_timer, mtk_dp_debounce_timer, 0);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;
}

static int mtk_dp_remove(struct platform_device *pdev)
{
	struct mtk_dp *mtk_dp = platform_get_drvdata(pdev);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	del_timer_sync(&mtk_dp->debounce_timer);
	drm_bridge_remove(&mtk_dp->bridge);
	platform_device_unregister(mtk_dp->phy_dev);
	if (mtk_dp->audio_pdev)
		platform_device_unregister(mtk_dp->audio_pdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_dp_suspend(struct device *dev)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	mtk_dp_power_disable(mtk_dp);
	mtk_dp_hwirq_enable(mtk_dp, false);
	pm_runtime_put_sync(dev);

	return 0;
}

static int mtk_dp_resume(struct device *dev)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	mtk_dp_init_port(mtk_dp);
	mtk_dp_hwirq_enable(mtk_dp, true);
	mtk_dp_power_enable(mtk_dp);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mtk_dp_pm_ops, mtk_dp_suspend, mtk_dp_resume);

static const struct mtk_dp_data mt8195_edp_data = {
	.bridge_type = DRM_MODE_CONNECTOR_eDP,
	.smc_cmd = MTK_DP_SIP_ATF_EDP_VIDEO_UNMUTE,
	.efuse_fmt = mt8195_edp_efuse_fmt,
	.audio_supported = false,
};

static const struct mtk_dp_data mt8195_dp_data = {
	.bridge_type = DRM_MODE_CONNECTOR_DisplayPort,
	.smc_cmd = MTK_DP_SIP_ATF_VIDEO_UNMUTE,
	.efuse_fmt = mt8195_dp_efuse_fmt,
	.audio_supported = true,
};

static const struct of_device_id mtk_dp_of_match[] = {
	{
		.compatible = "mediatek,mt8195-edp-tx",
		.data = &mt8195_edp_data,
	},
	{
		.compatible = "mediatek,mt8195-dp-tx",
		.data = &mt8195_dp_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_dp_of_match);

static struct platform_driver mtk_dp_driver = {
	.probe = mtk_dp_probe,
	.remove = mtk_dp_remove,
	.driver = {
		.name = "mediatek-drm-dp",
		.of_match_table = mtk_dp_of_match,
		.pm = &mtk_dp_pm_ops,
	},
};

module_platform_driver(mtk_dp_driver);

MODULE_AUTHOR("Jitao Shi <jitao.shi@mediatek.com>");
MODULE_AUTHOR("Markus Schneider-Pargmann <msp@baylibre.com>");
MODULE_AUTHOR("Bo-Chen Chen <rex-bc.chen@mediatek.com>");
MODULE_DESCRIPTION("MediaTek DisplayPort Driver");
MODULE_LICENSE("GPL");
