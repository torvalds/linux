// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dsc.h>
#include <drm/drm_edid.h>
#include <drm/drm_hdcp.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_HDMI_SDAIN_MSK		BIT(14)
#define RK3228_HDMI_SCLIN_MSK		BIT(13)
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_HDMI_HPD_VSEL		BIT(6)
#define RK3228_HDMI_SDA_VSEL		BIT(5)
#define RK3228_HDMI_SCL_VSEL		BIT(4)

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3288_GRF_SOC_CON16		0x03a8
#define RK3288_HDMI_LCDC0_YUV420	BIT(2)
#define RK3288_HDMI_LCDC1_YUV420	BIT(3)

#define RK3328_GRF_SOC_CON2		0x0408
#define RK3328_HDMI_SDAIN_MSK		BIT(11)
#define RK3328_HDMI_SCLIN_MSK		BIT(10)
#define RK3328_HDMI_HPD_IOE		BIT(2)
#define RK3328_GRF_SOC_CON3		0x040c
/* need to be unset if hdmi or i2c should control voltage */
#define RK3328_HDMI_SDA5V_GRF		BIT(15)
#define RK3328_HDMI_SCL5V_GRF		BIT(14)
#define RK3328_HDMI_HPD5V_GRF		BIT(13)
#define RK3328_HDMI_CEC5V_GRF		BIT(12)
#define RK3328_GRF_SOC_CON4		0x0410
#define RK3328_HDMI_HPD_SARADC		BIT(13)
#define RK3328_HDMI_CEC_5V		BIT(11)
#define RK3328_HDMI_SDA_5V		BIT(10)
#define RK3328_HDMI_SCL_5V		BIT(9)
#define RK3328_HDMI_HPD_5V		BIT(8)

#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define RK3528_VO_GRF_HDMI_MASK		0x60014
#define RK3528_HDMI_SNKDET_SEL		BIT(6)
#define RK3528_HDMI_SNKDET		BIT(5)
#define RK3528_HDMI_CECIN_MSK		BIT(2)
#define RK3528_HDMI_SDAIN_MSK		BIT(1)
#define RK3528_HDMI_SCLIN_MSK		BIT(0)

#define RK3528PMU_GRF_SOC_CON6		0x70018
#define RK3528_HDMI_SDA5V_GRF		BIT(6)
#define RK3528_HDMI_SCL5V_GRF		BIT(5)
#define RK3528_HDMI_CEC5V_GRF		BIT(4)
#define RK3528_HDMI_HPD5V_GRF		BIT(3)

#define RK3528_GPIO_SWPORT_DR_L		0x0000
#define RK3528_GPIO0_A2_DR		BIT(2)

#define RK3568_GRF_VO_CON1		0x0364
#define RK3568_HDMI_SDAIN_MSK		BIT(15)
#define RK3568_HDMI_SCLIN_MSK		BIT(14)

#define RK3588_GRF_SOC_CON2		0x0308
#define RK3588_HDMI1_HPD_INT_MSK	BIT(15)
#define RK3588_HDMI1_HPD_INT_CLR	BIT(14)
#define RK3588_HDMI0_HPD_INT_MSK	BIT(13)
#define RK3588_HDMI0_HPD_INT_CLR	BIT(12)
#define RK3588_GRF_SOC_CON7		0x031c
#define RK3588_SET_HPD_PATH_MASK	(0x3 << 12)
#define RK3588_GRF_SOC_STATUS1		0x0384
#define RK3588_HDMI0_LOW_MORETHAN100MS	BIT(20)
#define RK3588_HDMI0_HPD_PORT_LEVEL	BIT(19)
#define RK3588_HDMI0_IHPD_PORT		BIT(18)
#define RK3588_HDMI0_OHPD_INT		BIT(17)
#define RK3588_HDMI0_LEVEL_INT		BIT(16)
#define RK3588_HDMI0_INTR_CHANGE_CNT	(0x7 << 13)
#define RK3588_HDMI1_LOW_MORETHAN100MS	BIT(28)
#define RK3588_HDMI1_HPD_PORT_LEVEL	BIT(27)
#define RK3588_HDMI1_IHPD_PORT		BIT(26)
#define RK3588_HDMI1_OHPD_INT		BIT(25)
#define RK3588_HDMI1_LEVEL_INT		BIT(24)
#define RK3588_HDMI1_INTR_CHANGE_CNT	(0x7 << 21)

#define RK3588_GRF_VO1_CON1		0x0004
#define HDCP1_P1_GPIO_IN		BIT(9)
#define RK3588_GRF_VO1_CON3		0x000c
#define RK3588_COLOR_FORMAT_MASK	0xf
#define RK3588_RGB			0
#define RK3588_YUV422			0x1
#define RK3588_YUV444			0x2
#define RK3588_YUV420			0x3
#define RK3588_COMPRESSED_DATA		0xb
#define RK3588_COLOR_DEPTH_MASK		(0xf << 4)
#define RK3588_8BPC			0
#define RK3588_10BPC			(0x6 << 4)
#define RK3588_CECIN_MASK		BIT(8)
#define RK3588_SCLIN_MASK		BIT(9)
#define RK3588_SDAIN_MASK		BIT(10)
#define RK3588_MODE_MASK		BIT(11)
#define RK3588_COMPRESS_MODE_MASK	BIT(12)
#define RK3588_I2S_SEL_MASK		BIT(13)
#define RK3588_SPDIF_SEL_MASK		BIT(14)
#define RK3588_GRF_VO1_CON4		0x0010
#define RK3588_HDMI21_MASK		BIT(0)
#define RK3588_GRF_VO1_CON9		0x0024
#define RK3588_HDMI0_GRANT_SEL		BIT(10)
#define RK3588_HDMI0_GRANT_SW		BIT(11)
#define RK3588_HDMI1_GRANT_SEL		BIT(12)
#define RK3588_HDMI1_GRANT_SW		BIT(13)
#define RK3588_GRF_VO1_CON4		0x0010
#define RK3588_HDMI_HDCP14_MEM_EN	BIT(15)
#define RK3588_GRF_VO1_CON6		0x0018
#define RK3588_GRF_VO1_CON7		0x001c

#define COLOR_DEPTH_10BIT		BIT(31)
#define HDMI_FRL_MODE			BIT(30)
#define HDMI_EARC_MODE			BIT(29)
#define DATA_RATE_MASK			0xFFFFFFF

#define HDMI20_MAX_RATE			600000
#define HDMI_8K60_RATE			2376000

/**
 * struct rockchip_hdmi_chip_data - splite the grf setting of kind of chips
 * @lcdsel_grf_reg: grf register offset of lcdc select
 * @ddc_en_reg: grf register offset of hdmi ddc enable
 * @lcdsel_big: reg value of selecting vop big for HDMI
 * @lcdsel_lit: reg value of selecting vop little for HDMI
 */
struct rockchip_hdmi_chip_data {
	int	lcdsel_grf_reg;
	int	ddc_en_reg;
	u32	lcdsel_big;
	u32	lcdsel_lit;
	bool	split_mode;
};

enum hdmi_frl_rate_per_lane {
	FRL_12G_PER_LANE = 12,
	FRL_10G_PER_LANE = 10,
	FRL_8G_PER_LANE = 8,
	FRL_6G_PER_LANE = 6,
	FRL_3G_PER_LANE = 3,
};

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	struct regmap *vo1_regmap;
	void __iomem *gpio_base;
	struct drm_encoder encoder;
	struct drm_device *drm_dev;
	const struct rockchip_hdmi_chip_data *chip_data;
	struct dw_hdmi_plat_data *plat_data;
	struct clk *aud_clk;
	struct clk *phyref_clk;
	struct clk *grf_clk;
	struct clk *hclk_vio;
	struct clk *hclk_vo1;
	struct clk *hclk_vop;
	struct clk *hpd_clk;
	struct clk *pclk;
	struct clk *earc_clk;
	struct clk *hdmitx_ref;
	struct clk *link_clk;
	struct dw_hdmi *hdmi;
	struct dw_hdmi_qp *hdmi_qp;

	struct phy *phy;

	u32 max_tmdsclk;
	bool unsupported_yuv_input;
	bool unsupported_deep_color;
	bool skip_check_420_mode;
	bool hpd_wake_en;
	u8 force_output;
	u8 id;
	bool hpd_stat;
	bool is_hdmi_qp;

	unsigned long bus_format;
	unsigned long output_bus_format;
	unsigned long enc_out_encoding;
	unsigned long prev_bus_format;
	int color_changed;
	int hpd_irq;

	struct drm_property *color_depth_property;
	struct drm_property *hdmi_output_property;
	struct drm_property *colordepth_capacity;
	struct drm_property *outputmode_capacity;
	struct drm_property *quant_range;
	struct drm_property *hdr_panel_metadata_property;
	struct drm_property *next_hdr_sink_data_property;
	struct drm_property *output_hdmi_dvi;
	struct drm_property *output_type_capacity;
	struct drm_property *allm_capacity;
	struct drm_property *allm_enable;
	struct drm_property *hdcp_state_property;

	struct drm_property_blob *hdr_panel_blob_ptr;
	struct drm_property_blob *next_hdr_data_ptr;

	unsigned int colordepth;
	unsigned int colorimetry;
	unsigned int hdmi_quant_range;
	unsigned int phy_bus_width;
	unsigned int enable_allm;
	enum rk_if_color_format hdmi_output;
	struct rockchip_drm_sub_dev sub_dev;

	u8 max_frl_rate_per_lane;
	u8 max_lanes;
	u8 add_func;
	u8 edid_colorimetry;
	u8 hdcp_status;
	struct rockchip_drm_dsc_cap dsc_cap;
	struct next_hdr_sink_data next_hdr_data;
	struct dw_hdmi_link_config link_cfg;
	struct gpio_desc *enable_gpio;

	struct delayed_work work;
	struct workqueue_struct *workqueue;
	struct gpio_desc *hpd_gpiod;
	struct pinctrl *p;
	struct pinctrl_state *idle_state;
	struct pinctrl_state *default_state;
};

#define to_rockchip_hdmi(x)	container_of(x, struct rockchip_hdmi, x)

/*
 * There are some rates that would be ranged for better clock jitter at
 * Chrome OS tree, like 25.175Mhz would range to 25.170732Mhz. But due
 * to the clock is aglined to KHz in struct drm_display_mode, this would
 * bring some inaccurate error if we still run the compute_n math, so
 * let's just code an const table for it until we can actually get the
 * right clock rate.
 */
static const struct dw_hdmi_audio_tmds_n rockchip_werid_tmds_n_table[] = {
	/* 25176471 for 25.175 MHz = 428000000 / 17. */
	{ .tmds = 25177000, .n_32k = 4352, .n_44k1 = 14994, .n_48k = 6528, },
	/* 57290323 for 57.284 MHz */
	{ .tmds = 57291000, .n_32k = 3968, .n_44k1 = 4557, .n_48k = 5952, },
	/* 74437500 for 74.44 MHz = 297750000 / 4 */
	{ .tmds = 74438000, .n_32k = 8192, .n_44k1 = 18816, .n_48k = 4096, },
	/* 118666667 for 118.68 MHz */
	{ .tmds = 118667000, .n_32k = 4224, .n_44k1 = 5292, .n_48k = 6336, },
	/* 121714286 for 121.75 MHz */
	{ .tmds = 121715000, .n_32k = 4480, .n_44k1 = 6174, .n_48k = 6272, },
	/* 136800000 for 136.75 MHz */
	{ .tmds = 136800000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg_420[] = {
	{
		30666000, {
			{ 0x00b7, 0x0000 },
			{ 0x2157, 0x0000 },
			{ 0x40f7, 0x0000 },
		},
	},  {
		92000000, {
			{ 0x00b7, 0x0000 },
			{ 0x2143, 0x0001 },
			{ 0x40a3, 0x0001 },
		},
	},  {
		184000000, {
			{ 0x0073, 0x0001 },
			{ 0x2146, 0x0002 },
			{ 0x4062, 0x0002 },
		},
	},  {
		340000000, {
			{ 0x0052, 0x0003 },
			{ 0x214d, 0x0003 },
			{ 0x4065, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x0041, 0x0003 },
			{ 0x3b4d, 0x0003 },
			{ 0x5a65, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_mpll_config rockchip_rk3288w_mpll_cfg_420[] = {
	{
		30666000, {
			{ 0x00b7, 0x0000 },
			{ 0x2157, 0x0000 },
			{ 0x40f7, 0x0000 },
		},
	},  {
		92000000, {
			{ 0x00b7, 0x0000 },
			{ 0x2143, 0x0001 },
			{ 0x40a3, 0x0001 },
		},
	},  {
		184000000, {
			{ 0x0073, 0x0001 },
			{ 0x2146, 0x0002 },
			{ 0x4062, 0x0002 },
		},
	},  {
		340000000, {
			{ 0x0052, 0x0003 },
			{ 0x214d, 0x0003 },
			{ 0x4065, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a65, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		600000000, { 0x0000, 0x0000, 0x0000 },
	},  {
		~0UL,      { 0x0000, 0x0000, 0x0000},
	}
};

static struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 165000000, 0x802b, 0x0004, 0x0209},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000},
	{ ~0UL,      0x0000, 0x0000, 0x0000},
};

enum ROW_INDEX_BPP {
	ROW_INDEX_6BPP = 0,
	ROW_INDEX_8BPP,
	ROW_INDEX_10BPP,
	ROW_INDEX_12BPP,
	ROW_INDEX_23BPP,
	MAX_ROW_INDEX
};

enum COLUMN_INDEX_BPC {
	COLUMN_INDEX_8BPC = 0,
	COLUMN_INDEX_10BPC,
	COLUMN_INDEX_12BPC,
	COLUMN_INDEX_14BPC,
	COLUMN_INDEX_16BPC,
	MAX_COLUMN_INDEX
};

#define PPS_TABLE_LEN 8
#define PPS_BPP_LEN 4
#define PPS_BPC_LEN 2

struct pps_data {
	u32 pic_width;
	u32 pic_height;
	u32 slice_width;
	u32 slice_height;
	bool convert_rgb;
	u8 bpc;
	u8 bpp;
	u8 raw_pps[128];
};

/*
 * Selected Rate Control Related Parameter Recommended Values
 * from DSC_v1.11 spec & C Model release: DSC_model_20161212
 */
static struct pps_data pps_datas[PPS_TABLE_LEN] = {
	{
		/* 7680x4320/960X96 rgb 8bpc 12bpp */
		7680, 4320, 960, 96, 1, 8, 192,
		{
			0x12, 0x00, 0x00, 0x8d, 0x30, 0xc0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x05, 0xa0,
			0x01, 0x55, 0x03, 0x90, 0x00, 0x0a, 0x05, 0xc9,
			0x00, 0xa0, 0x00, 0x0f, 0x01, 0x44, 0x01, 0xaa,
			0x08, 0x00, 0x10, 0xf4, 0x03, 0x0c, 0x20, 0x00,
			0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x00, 0x82, 0x00, 0xc0, 0x09, 0x00,
			0x09, 0x7e, 0x19, 0xbc, 0x19, 0xba, 0x19, 0xf8,
			0x1a, 0x38, 0x1a, 0x38, 0x1a, 0x76, 0x2a, 0x76,
			0x2a, 0x76, 0x2a, 0x74, 0x3a, 0xb4, 0x52, 0xf4,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 8bpc 11bpp */
		7680, 4320, 960, 96, 1, 8, 176,
		{
			0x12, 0x00, 0x00, 0x8d, 0x30, 0xb0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x05, 0x28,
			0x01, 0x74, 0x03, 0x40, 0x00, 0x0f, 0x06, 0xe0,
			0x00, 0x2d, 0x00, 0x0f, 0x01, 0x44, 0x01, 0x33,
			0x0f, 0x00, 0x10, 0xf4, 0x03, 0x0c, 0x20, 0x00,
			0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x00, 0x82, 0x01, 0x00, 0x09, 0x40,
			0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa, 0x19, 0xf8,
			0x1a, 0x38, 0x1a, 0x38, 0x1a, 0x76, 0x2a, 0x76,
			0x2a, 0x76, 0x2a, 0xb4, 0x3a, 0xb4, 0x52, 0xf4,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 8bpc 10bpp */
		7680, 4320, 960, 96, 1, 8, 160,
		{
			0x12, 0x00, 0x00, 0x8d, 0x30, 0xa0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x04, 0xb0,
			0x01, 0x9a, 0x02, 0xe0, 0x00, 0x19, 0x09, 0xb0,
			0x00, 0x12, 0x00, 0x0f, 0x01, 0x44, 0x00, 0xbb,
			0x16, 0x00, 0x10, 0xec, 0x03, 0x0c, 0x20, 0x00,
			0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x00, 0xc2, 0x01, 0x00, 0x09, 0x40,
			0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa, 0x19, 0xf8,
			0x1a, 0x38, 0x1a, 0x78, 0x1a, 0x76, 0x2a, 0xb6,
			0x2a, 0xb6, 0x2a, 0xf4, 0x3a, 0xf4, 0x5b, 0x34,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 8bpc 9bpp */
		7680, 4320, 960, 96, 1, 8, 144,
		{
			0x12, 0x00, 0x00, 0x8d, 0x30, 0x90, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x04, 0x38,
			0x01, 0xc7, 0x03, 0x16, 0x00, 0x1c, 0x08, 0xc7,
			0x00, 0x10, 0x00, 0x0f, 0x01, 0x44, 0x00, 0xaa,
			0x17, 0x00, 0x10, 0xf1, 0x03, 0x0c, 0x20, 0x00,
			0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x00, 0xc2, 0x01, 0x00, 0x09, 0x40,
			0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa, 0x19, 0xf8,
			0x1a, 0x38, 0x1a, 0x78, 0x1a, 0x76, 0x2a, 0xb6,
			0x2a, 0xb6, 0x2a, 0xf4, 0x3a, 0xf4, 0x63, 0x74,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 10bpc 12bpp */
		7680, 4320, 960, 96, 1, 10, 192,
		{
			0x12, 0x00, 0x00, 0xad, 0x30, 0xc0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x05, 0xa0,
			0x01, 0x55, 0x03, 0x90, 0x00, 0x0a, 0x05, 0xc9,
			0x00, 0xa0, 0x00, 0x0f, 0x01, 0x44, 0x01, 0xaa,
			0x08, 0x00, 0x10, 0xf4, 0x07, 0x10, 0x20, 0x00,
			0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x01, 0x02, 0x11, 0x80, 0x22, 0x00,
			0x22, 0x7e, 0x32, 0xbc, 0x32, 0xba, 0x3a, 0xf8,
			0x3b, 0x38, 0x3b, 0x38, 0x3b, 0x76, 0x4b, 0x76,
			0x4b, 0x76, 0x4b, 0x74, 0x5b, 0xb4, 0x73, 0xf4,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 10bpc 11bpp */
		7680, 4320, 960, 96, 1, 10, 176,
		{
			0x12, 0x00, 0x00, 0xad, 0x30, 0xb0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x05, 0x28,
			0x01, 0x74, 0x03, 0x40, 0x00, 0x0f, 0x06, 0xe0,
			0x00, 0x2d, 0x00, 0x0f, 0x01, 0x44, 0x01, 0x33,
			0x0f, 0x00, 0x10, 0xf4, 0x07, 0x10, 0x20, 0x00,
			0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x01, 0x42, 0x19, 0xc0, 0x2a, 0x40,
			0x2a, 0xbe, 0x3a, 0xfc, 0x3a, 0xfa, 0x3a, 0xf8,
			0x3b, 0x38, 0x3b, 0x38, 0x3b, 0x76, 0x4b, 0x76,
			0x4b, 0x76, 0x4b, 0xb4, 0x5b, 0xb4, 0x73, 0xf4,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 10bpc 10bpp */
		7680, 4320, 960, 96, 1, 10, 160,
		{
			0x12, 0x00, 0x00, 0xad, 0x30, 0xa0, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x04, 0xb0,
			0x01, 0x9a, 0x02, 0xe0, 0x00, 0x19, 0x09, 0xb0,
			0x00, 0x12, 0x00, 0x0f, 0x01, 0x44, 0x00, 0xbb,
			0x16, 0x00, 0x10, 0xec, 0x07, 0x10, 0x20, 0x00,
			0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x01, 0xc2, 0x22, 0x00, 0x2a, 0x40,
			0x2a, 0xbe, 0x3a, 0xfc, 0x3a, 0xfa, 0x3a, 0xf8,
			0x3b, 0x38, 0x3b, 0x78, 0x3b, 0x76, 0x4b, 0xb6,
			0x4b, 0xb6, 0x4b, 0xf4, 0x63, 0xf4, 0x7c, 0x34,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
	{
		/* 7680x4320/960X96 rgb 10bpc 9bpp */
		7680, 4320, 960, 96, 1, 10, 144,
		{
			0x12, 0x00, 0x00, 0xad, 0x30, 0x90, 0x10, 0xe0,
			0x1e, 0x00, 0x00, 0x60, 0x03, 0xc0, 0x04, 0x38,
			0x01, 0xc7, 0x03, 0x16, 0x00, 0x1c, 0x08, 0xc7,
			0x00, 0x10, 0x00, 0x0f, 0x01, 0x44, 0x00, 0xaa,
			0x17, 0x00, 0x10, 0xf1, 0x07, 0x10, 0x20, 0x00,
			0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38,
			0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
			0x7d, 0x7e, 0x01, 0xc2, 0x22, 0x00, 0x2a, 0x40,
			0x2a, 0xbe, 0x3a, 0xfc, 0x3a, 0xfa, 0x3a, 0xf8,
			0x3b, 0x38, 0x3b, 0x78, 0x3b, 0x76, 0x4b, 0xb6,
			0x4b, 0xb6, 0x4b, 0xf4, 0x63, 0xf4, 0x84, 0x74,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		},
	},
};

static bool hdmi_bus_fmt_is_rgb(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB161616_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv444(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV16_1X48:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv422(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return true;

	default:
		return false;
	}
}

static bool hdmi_bus_fmt_is_yuv420(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return true;

	default:
	return false;
	}
}

static int hdmi_bus_fmt_color_depth(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		return 8;

	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		return 10;

	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_YUYV12_1X24:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
		return 12;

	case MEDIA_BUS_FMT_RGB161616_1X48:
	case MEDIA_BUS_FMT_YUV16_1X48:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return 16;

	default:
		return 0;
	}
}

static int hdmi_bus_fmt_to_color_format(unsigned int bus_format)
{
	switch (bus_format) {
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYYVYY12_0_5X36:
	case MEDIA_BUS_FMT_UYYVYY16_0_5X48:
		return RK_IF_FORMAT_YCBCR420;

	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_YUV12_1X36:
	case MEDIA_BUS_FMT_YUV16_1X48:
		return RK_IF_FORMAT_YCBCR444;

	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_UYVY12_1X24:
	case MEDIA_BUS_FMT_YVYU12_1X24:
		return RK_IF_FORMAT_YCBCR422;

	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_RGB161616_1X48:
	default:
		return RK_IF_FORMAT_RGB;
	}
}

static unsigned int
hdmi_get_tmdsclock(struct rockchip_hdmi *hdmi, unsigned long pixelclock)
{
	unsigned int tmdsclock = pixelclock;
	unsigned int depth =
		hdmi_bus_fmt_color_depth(hdmi->output_bus_format);

	if (!hdmi_bus_fmt_is_yuv422(hdmi->output_bus_format)) {
		switch (depth) {
		case 16:
			tmdsclock = pixelclock * 2;
			break;
		case 12:
			tmdsclock = pixelclock * 3 / 2;
			break;
		case 10:
			tmdsclock = pixelclock * 5 / 4;
			break;
		default:
			break;
		}
	}

	return tmdsclock;
}

static int rockchip_hdmi_match_by_id(struct device *dev, const void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);
	const unsigned int *id = data;

	return hdmi->id == *id;
}

static struct rockchip_hdmi *
rockchip_hdmi_find_by_id(struct device_driver *drv, unsigned int id)
{
	struct device *dev;

	dev = driver_find_device(drv, NULL, &id, rockchip_hdmi_match_by_id);
	if (!dev)
		return NULL;

	return dev_get_drvdata(dev);
}

static void hdmi_select_link_config(struct rockchip_hdmi *hdmi,
				    struct drm_crtc_state *crtc_state,
				    unsigned int tmdsclk)
{
	struct drm_display_mode mode;
	int max_lanes, max_rate_per_lane;
	int max_dsc_lanes, max_dsc_rate_per_lane;
	unsigned long max_frl_rate;

	drm_mode_copy(&mode, &crtc_state->mode);
	if (hdmi->plat_data->split_mode)
		drm_mode_convert_to_origin_mode(&mode);

	max_lanes = hdmi->max_lanes;
	max_rate_per_lane = hdmi->max_frl_rate_per_lane;
	max_frl_rate = max_lanes * max_rate_per_lane * 1000000;

	hdmi->link_cfg.dsc_mode = false;
	hdmi->link_cfg.frl_lanes = max_lanes;
	hdmi->link_cfg.rate_per_lane = max_rate_per_lane;
	hdmi->link_cfg.add_func = hdmi->add_func;

	if (!max_frl_rate || (tmdsclk < HDMI20_MAX_RATE && mode.clock < HDMI20_MAX_RATE)) {
		dev_info(hdmi->dev, "use tmds mode\n");
		hdmi->link_cfg.frl_mode = false;
		return;
	}

	hdmi->link_cfg.frl_mode = true;

	if (!hdmi->dsc_cap.v_1p2)
		return;

	max_dsc_lanes = hdmi->dsc_cap.max_lanes;
	max_dsc_rate_per_lane =
		hdmi->dsc_cap.max_frl_rate_per_lane;

	if (mode.clock >= HDMI_8K60_RATE &&
	    !hdmi_bus_fmt_is_yuv420(hdmi->bus_format) &&
	    !hdmi_bus_fmt_is_yuv422(hdmi->bus_format)) {
		hdmi->link_cfg.dsc_mode = true;
		hdmi->link_cfg.frl_lanes = max_dsc_lanes;
		hdmi->link_cfg.rate_per_lane = max_dsc_rate_per_lane;
	} else {
		hdmi->link_cfg.dsc_mode = false;
		hdmi->link_cfg.frl_lanes = max_lanes;
		hdmi->link_cfg.rate_per_lane = max_rate_per_lane;
	}
}

/////////////////////////////////////////////////////////////////////////////////////

static int hdmi_dsc_get_slice_height(int vactive)
{
	int slice_height;

	/*
	 * Slice Height determination : HDMI2.1 Section 7.7.5.2
	 * Select smallest slice height >=96, that results in a valid PPS and
	 * requires minimum padding lines required for final slice.
	 *
	 * Assumption : Vactive is even.
	 */
	for (slice_height = 96; slice_height <= vactive; slice_height += 2)
		if (vactive % slice_height == 0)
			return slice_height;

	return 0;
}

static int hdmi_dsc_get_num_slices(struct rockchip_hdmi *hdmi,
				   struct drm_crtc_state *crtc_state,
				   int src_max_slices, int src_max_slice_width,
				   int hdmi_max_slices, int hdmi_throughput)
{
/* Pixel rates in KPixels/sec */
#define HDMI_DSC_PEAK_PIXEL_RATE		2720000
/*
 * Rates at which the source and sink are required to process pixels in each
 * slice, can be two levels: either at least 340000KHz or at least 40000KHz.
 */
#define HDMI_DSC_MAX_ENC_THROUGHPUT_0		340000
#define HDMI_DSC_MAX_ENC_THROUGHPUT_1		400000

/* Spec limits the slice width to 2720 pixels */
#define MAX_HDMI_SLICE_WIDTH			2720
	int kslice_adjust;
	int adjusted_clk_khz;
	int min_slices;
	int target_slices;
	int max_throughput; /* max clock freq. in khz per slice */
	int max_slice_width;
	int slice_width;
	int pixel_clock = crtc_state->mode.clock;

	if (!hdmi_throughput)
		return 0;

	/*
	 * Slice Width determination : HDMI2.1 Section 7.7.5.1
	 * kslice_adjust factor for 4:2:0, and 4:2:2 formats is 0.5, where as
	 * for 4:4:4 is 1.0. Multiplying these factors by 10 and later
	 * dividing adjusted clock value by 10.
	 */
	if (hdmi_bus_fmt_is_yuv444(hdmi->output_bus_format) ||
	    hdmi_bus_fmt_is_rgb(hdmi->output_bus_format))
		kslice_adjust = 10;
	else
		kslice_adjust = 5;

	/*
	 * As per spec, the rate at which the source and the sink process
	 * the pixels per slice are at two levels: at least 340Mhz or 400Mhz.
	 * This depends upon the pixel clock rate and output formats
	 * (kslice adjust).
	 * If pixel clock * kslice adjust >= 2720MHz slices can be processed
	 * at max 340MHz, otherwise they can be processed at max 400MHz.
	 */

	adjusted_clk_khz = DIV_ROUND_UP(kslice_adjust * pixel_clock, 10);

	if (adjusted_clk_khz <= HDMI_DSC_PEAK_PIXEL_RATE)
		max_throughput = HDMI_DSC_MAX_ENC_THROUGHPUT_0;
	else
		max_throughput = HDMI_DSC_MAX_ENC_THROUGHPUT_1;

	/*
	 * Taking into account the sink's capability for maximum
	 * clock per slice (in MHz) as read from HF-VSDB.
	 */
	max_throughput = min(max_throughput, hdmi_throughput * 1000);

	min_slices = DIV_ROUND_UP(adjusted_clk_khz, max_throughput);
	max_slice_width = min(MAX_HDMI_SLICE_WIDTH, src_max_slice_width);

	/*
	 * Keep on increasing the num of slices/line, starting from min_slices
	 * per line till we get such a number, for which the slice_width is
	 * just less than max_slice_width. The slices/line selected should be
	 * less than or equal to the max horizontal slices that the combination
	 * of PCON encoder and HDMI decoder can support.
	 */
	do {
		if (min_slices <= 1 && src_max_slices >= 1 && hdmi_max_slices >= 1)
			target_slices = 1;
		else if (min_slices <= 2 && src_max_slices >= 2 && hdmi_max_slices >= 2)
			target_slices = 2;
		else if (min_slices <= 4 && src_max_slices >= 4 && hdmi_max_slices >= 4)
			target_slices = 4;
		else if (min_slices <= 8 && src_max_slices >= 8 && hdmi_max_slices >= 8)
			target_slices = 8;
		else if (min_slices <= 12 && src_max_slices >= 12 && hdmi_max_slices >= 12)
			target_slices = 12;
		else if (min_slices <= 16 && src_max_slices >= 16 && hdmi_max_slices >= 16)
			target_slices = 16;
		else
			return 0;

		slice_width = DIV_ROUND_UP(crtc_state->mode.hdisplay, target_slices);
		if (slice_width > max_slice_width)
			min_slices = target_slices + 1;
	} while (slice_width > max_slice_width);

	return target_slices;
}

static int hdmi_dsc_slices(struct rockchip_hdmi *hdmi,
			   struct drm_crtc_state *crtc_state)
{
	int hdmi_throughput = hdmi->dsc_cap.clk_per_slice;
	int hdmi_max_slices = hdmi->dsc_cap.max_slices;
	int rk_max_slices = 8;
	int rk_max_slice_width = 2048;

	return hdmi_dsc_get_num_slices(hdmi, crtc_state, rk_max_slices,
				       rk_max_slice_width,
				       hdmi_max_slices, hdmi_throughput);
}

static int
hdmi_dsc_get_bpp(struct rockchip_hdmi *hdmi, int src_fractional_bpp,
		 int slice_width, int num_slices, bool hdmi_all_bpp,
		 int hdmi_max_chunk_bytes)
{
	int max_dsc_bpp, min_dsc_bpp;
	int target_bytes;
	bool bpp_found = false;
	int bpp_decrement_x16;
	int bpp_target;
	int bpp_target_x16;

	/*
	 * Get min bpp and max bpp as per Table 7.23, in HDMI2.1 spec
	 * Start with the max bpp and keep on decrementing with
	 * fractional bpp, if supported by PCON DSC encoder
	 *
	 * for each bpp we check if no of bytes can be supported by HDMI sink
	 */

	/* only 9\10\12 bpp was tested */
	min_dsc_bpp = 9;
	max_dsc_bpp = 12;

	/*
	 * Taking into account if all dsc_all_bpp supported by HDMI2.1 sink
	 * Section 7.7.34 : Source shall not enable compressed Video
	 * Transport with bpp_target settings above 12 bpp unless
	 * DSC_all_bpp is set to 1.
	 */
	if (!hdmi_all_bpp)
		max_dsc_bpp = min(max_dsc_bpp, 12);

	/*
	 * The Sink has a limit of compressed data in bytes for a scanline,
	 * as described in max_chunk_bytes field in HFVSDB block of edid.
	 * The no. of bytes depend on the target bits per pixel that the
	 * source configures. So we start with the max_bpp and calculate
	 * the target_chunk_bytes. We keep on decrementing the target_bpp,
	 * till we get the target_chunk_bytes just less than what the sink's
	 * max_chunk_bytes, or else till we reach the min_dsc_bpp.
	 *
	 * The decrement is according to the fractional support from PCON DSC
	 * encoder. For fractional BPP we use bpp_target as a multiple of 16.
	 *
	 * bpp_target_x16 = bpp_target * 16
	 * So we need to decrement by {1, 2, 4, 8, 16} for fractional bpps
	 * {1/16, 1/8, 1/4, 1/2, 1} respectively.
	 */

	bpp_target = max_dsc_bpp;

	/* src does not support fractional bpp implies decrement by 16 for bppx16 */
	if (!src_fractional_bpp)
		src_fractional_bpp = 1;
	bpp_decrement_x16 = DIV_ROUND_UP(16, src_fractional_bpp);
	bpp_target_x16 = bpp_target * 16;

	while (bpp_target_x16 > (min_dsc_bpp * 16)) {
		int bpp;

		bpp = DIV_ROUND_UP(bpp_target_x16, 16);
		target_bytes = DIV_ROUND_UP((num_slices * slice_width * bpp), 8);
		if (target_bytes <= hdmi_max_chunk_bytes) {
			bpp_found = true;
			break;
		}
		bpp_target_x16 -= bpp_decrement_x16;
	}
	if (bpp_found)
		return bpp_target_x16;

	return 0;
}

static int
dw_hdmi_dsc_bpp(struct rockchip_hdmi *hdmi,
		int num_slices, int slice_width)
{
	bool hdmi_all_bpp = hdmi->dsc_cap.all_bpp;
	int fractional_bpp = 0;
	int hdmi_max_chunk_bytes = hdmi->dsc_cap.total_chunk_kbytes * 1024;

	return hdmi_dsc_get_bpp(hdmi, fractional_bpp, slice_width,
				num_slices, hdmi_all_bpp,
				hdmi_max_chunk_bytes);
}

static int dw_hdmi_qp_set_link_cfg(struct rockchip_hdmi *hdmi,
				   u16 pic_width, u16 pic_height,
				   u16 slice_width, u16 slice_height,
				   u16 bits_per_pixel, u8 bits_per_component)
{
	int i;

	for (i = 0; i < PPS_TABLE_LEN; i++)
		if (pic_width == pps_datas[i].pic_width &&
		    pic_height == pps_datas[i].pic_height &&
		    slice_width == pps_datas[i].slice_width &&
		    slice_height == pps_datas[i].slice_height &&
		    bits_per_component == pps_datas[i].bpc &&
		    bits_per_pixel == pps_datas[i].bpp &&
		    hdmi_bus_fmt_is_rgb(hdmi->output_bus_format) == pps_datas[i].convert_rgb)
			break;

	if (i == PPS_TABLE_LEN) {
		dev_err(hdmi->dev, "can't find pps cfg!\n");
		return -EINVAL;
	}

	memcpy(hdmi->link_cfg.pps_payload, pps_datas[i].raw_pps, 128);
	hdmi->link_cfg.hcactive = DIV_ROUND_UP(slice_width * (bits_per_pixel / 16), 8) *
		(pic_width / slice_width);

	return 0;
}

static void dw_hdmi_qp_dsc_configure(struct rockchip_hdmi *hdmi,
				     struct rockchip_crtc_state *s,
				     struct drm_crtc_state *crtc_state)
{
	int ret;
	int slice_height;
	int slice_width;
	int bits_per_pixel;
	int slice_count;
	bool hdmi_is_dsc_1_2;
	unsigned int depth = hdmi_bus_fmt_color_depth(hdmi->output_bus_format);

	if (!crtc_state)
		return;

	hdmi_is_dsc_1_2 = hdmi->dsc_cap.v_1p2;

	if (!hdmi_is_dsc_1_2)
		return;

	slice_height = hdmi_dsc_get_slice_height(crtc_state->mode.vdisplay);
	if (!slice_height)
		return;

	slice_count = hdmi_dsc_slices(hdmi, crtc_state);
	if (!slice_count)
		return;

	slice_width = DIV_ROUND_UP(crtc_state->mode.hdisplay, slice_count);

	bits_per_pixel = dw_hdmi_dsc_bpp(hdmi, slice_count, slice_width);
	if (!bits_per_pixel)
		return;

	ret = dw_hdmi_qp_set_link_cfg(hdmi, crtc_state->mode.hdisplay,
				      crtc_state->mode.vdisplay, slice_width,
				      slice_height, bits_per_pixel, depth);

	if (ret) {
		dev_err(hdmi->dev, "set vdsc cfg failed\n");
		return;
	}
	dev_info(hdmi->dev, "dsc_enable\n");
	s->dsc_enable = 1;
	s->dsc_sink_cap.version_major = 1;
	s->dsc_sink_cap.version_minor = 2;
	s->dsc_sink_cap.slice_width = slice_width;
	s->dsc_sink_cap.slice_height = slice_height;
	s->dsc_sink_cap.target_bits_per_pixel_x16 = bits_per_pixel;
	s->dsc_sink_cap.block_pred = 1;
	s->dsc_sink_cap.native_420 = 0;

	memcpy(&s->pps, hdmi->link_cfg.pps_payload, 128);
}
/////////////////////////////////////////////////////////////////////////////////////////

static int rockchip_hdmi_update_phy_table(struct rockchip_hdmi *hdmi,
					  u32 *config,
					  int phy_table_size)
{
	int i;

	if (phy_table_size > ARRAY_SIZE(rockchip_phy_config)) {
		dev_err(hdmi->dev, "phy table array number is out of range\n");
		return -E2BIG;
	}

	for (i = 0; i < phy_table_size; i++) {
		if (config[i * 4] != 0)
			rockchip_phy_config[i].mpixelclock = (u64)config[i * 4];
		else
			rockchip_phy_config[i].mpixelclock = ~0UL;
		rockchip_phy_config[i].sym_ctr = (u16)config[i * 4 + 1];
		rockchip_phy_config[i].term = (u16)config[i * 4 + 2];
		rockchip_phy_config[i].vlev_ctr = (u16)config[i * 4 + 3];
	}

	return 0;
}

static void repo_hpd_event(struct work_struct *p_work)
{
	struct rockchip_hdmi *hdmi = container_of(p_work, struct rockchip_hdmi, work.work);
	bool change;

	change = drm_helper_hpd_irq_event(hdmi->drm_dev);
	if (change) {
		dev_dbg(hdmi->dev, "hpd stat changed:%d\n", hdmi->hpd_stat);
		dw_hdmi_qp_cec_set_hpd(hdmi->hdmi_qp, hdmi->hpd_stat, change);
	}
}

static irqreturn_t rockchip_hdmi_hardirq(int irq, void *dev_id)
{
	struct rockchip_hdmi *hdmi = dev_id;
	u32 intr_stat, val;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &intr_stat);

	if (intr_stat) {
		dev_dbg(hdmi->dev, "hpd irq %#x\n", intr_stat);

		if (!hdmi->id)
			val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_MSK,
					    RK3588_HDMI0_HPD_INT_MSK);
		else
			val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_MSK,
					    RK3588_HDMI1_HPD_INT_MSK);
		regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t rockchip_hdmi_irq(int irq, void *dev_id)
{
	struct rockchip_hdmi *hdmi = dev_id;
	u32 intr_stat, val;
	int msecs;
	bool stat;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &intr_stat);

	if (!intr_stat)
		return IRQ_NONE;

	if (!hdmi->id) {
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_CLR,
				    RK3588_HDMI0_HPD_INT_CLR);
		if (intr_stat & RK3588_HDMI0_LEVEL_INT)
			stat = true;
		else
			stat = false;
	} else {
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_CLR,
				    RK3588_HDMI1_HPD_INT_CLR);
		if (intr_stat & RK3588_HDMI1_LEVEL_INT)
			stat = true;
		else
			stat = false;
	}

	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	if (stat) {
		hdmi->hpd_stat = true;
		msecs = 150;
	} else {
		hdmi->hpd_stat = false;
		msecs = 20;
	}
	mod_delayed_work(hdmi->workqueue, &hdmi->work, msecs_to_jiffies(msecs));

	if (!hdmi->id) {
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_CLR,
				    RK3588_HDMI0_HPD_INT_CLR) |
		      HIWORD_UPDATE(0, RK3588_HDMI0_HPD_INT_MSK);
	} else {
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_CLR,
				    RK3588_HDMI1_HPD_INT_CLR) |
		      HIWORD_UPDATE(0, RK3588_HDMI1_HPD_INT_MSK);
	}

	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

	return IRQ_HANDLED;
}

static void init_hpd_work(struct rockchip_hdmi *hdmi)
{
	hdmi->workqueue = create_workqueue("hpd_queue");
	INIT_DELAYED_WORK(&hdmi->work, repo_hpd_event);
}

static irqreturn_t rockchip_hdmi_hpd_irq_handler(int irq, void *arg)
{
	u32 val;
	struct rockchip_hdmi *hdmi = arg;

	val = gpiod_get_value(hdmi->hpd_gpiod);
	if (val) {
		val = HIWORD_UPDATE(RK3528_HDMI_SNKDET, RK3528_HDMI_SNKDET);
		if (hdmi->hdmi && hdmi->hpd_wake_en && hdmi->hpd_gpiod)
			dw_hdmi_set_hpd_wake(hdmi->hdmi);
	} else {
		val = HIWORD_UPDATE(0, RK3528_HDMI_SNKDET);
	}
	regmap_write(hdmi->regmap, RK3528_VO_GRF_HDMI_MASK, val);

	return IRQ_HANDLED;
}

static void dw_hdmi_rk3528_gpio_hpd_init(struct rockchip_hdmi *hdmi)
{
	u32 val;

	if (hdmi->hpd_gpiod) {
		/* gpio0_a2's input enable is controlled by gpio output data bit */
		val = HIWORD_UPDATE(RK3528_GPIO0_A2_DR, RK3528_GPIO0_A2_DR);
		writel(val, hdmi->gpio_base + RK3528_GPIO_SWPORT_DR_L);

		val = HIWORD_UPDATE(RK3528_HDMI_SNKDET_SEL | RK3528_HDMI_SDAIN_MSK |
				    RK3528_HDMI_SCLIN_MSK,
				    RK3528_HDMI_SNKDET_SEL | RK3528_HDMI_SDAIN_MSK |
				    RK3528_HDMI_SCLIN_MSK);
	} else {
		val = HIWORD_UPDATE(RK3528_HDMI_SDAIN_MSK | RK3528_HDMI_SCLIN_MSK,
				    RK3528_HDMI_SDAIN_MSK | RK3528_HDMI_SCLIN_MSK);
	}

	regmap_write(hdmi->regmap, RK3528_VO_GRF_HDMI_MASK, val);

	val = gpiod_get_value(hdmi->hpd_gpiod);
	if (val) {
		val = HIWORD_UPDATE(RK3528_HDMI_SNKDET, RK3528_HDMI_SNKDET);
		if (hdmi->hdmi && hdmi->hpd_wake_en && hdmi->hpd_gpiod)
			dw_hdmi_set_hpd_wake(hdmi->hdmi);
	} else {
		val = HIWORD_UPDATE(0, RK3528_HDMI_SNKDET);
	}
	regmap_write(hdmi->regmap, RK3528_VO_GRF_HDMI_MASK, val);
}

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	int ret, val, phy_table_size;
	u32 *phy_config;
	struct device_node *np = hdmi->dev->of_node;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	if (hdmi->is_hdmi_qp) {
		hdmi->vo1_regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,vo1_grf");
		if (IS_ERR(hdmi->vo1_regmap)) {
			DRM_DEV_ERROR(hdmi->dev, "Unable to get rockchip,vo1_grf\n");
			return PTR_ERR(hdmi->vo1_regmap);
		}
	}

	hdmi->phyref_clk = devm_clk_get(hdmi->dev, "vpll");
	if (PTR_ERR(hdmi->phyref_clk) == -ENOENT)
		hdmi->phyref_clk = devm_clk_get(hdmi->dev, "ref");

	if (PTR_ERR(hdmi->phyref_clk) == -ENOENT) {
		hdmi->phyref_clk = NULL;
	} else if (PTR_ERR(hdmi->phyref_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->phyref_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->phyref_clk);
	}

	hdmi->grf_clk = devm_clk_get(hdmi->dev, "grf");
	if (PTR_ERR(hdmi->grf_clk) == -ENOENT) {
		hdmi->grf_clk = NULL;
	} else if (PTR_ERR(hdmi->grf_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->grf_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->grf_clk);
	}

	hdmi->hclk_vio = devm_clk_get(hdmi->dev, "hclk_vio");
	if (PTR_ERR(hdmi->hclk_vio) == -ENOENT) {
		hdmi->hclk_vio = NULL;
	} else if (PTR_ERR(hdmi->hclk_vio) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vio)) {
		dev_err(hdmi->dev, "failed to get hclk_vio clock\n");
		return PTR_ERR(hdmi->hclk_vio);
	}

	hdmi->hclk_vop = devm_clk_get(hdmi->dev, "hclk");
	if (PTR_ERR(hdmi->hclk_vop) == -ENOENT) {
		hdmi->hclk_vop = NULL;
	} else if (PTR_ERR(hdmi->hclk_vop) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vop)) {
		dev_err(hdmi->dev, "failed to get hclk_vop clock\n");
		return PTR_ERR(hdmi->hclk_vop);
	}

	hdmi->aud_clk = devm_clk_get_optional(hdmi->dev, "aud");
	if (IS_ERR(hdmi->aud_clk)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->aud_clk),
			      "failed to get aud_clk clock\n");
		return PTR_ERR(hdmi->aud_clk);
	}

	hdmi->hpd_clk = devm_clk_get_optional(hdmi->dev, "hpd");
	if (IS_ERR(hdmi->hpd_clk)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->hpd_clk),
			      "failed to get hpd_clk clock\n");
		return PTR_ERR(hdmi->hpd_clk);
	}

	hdmi->hclk_vo1 = devm_clk_get_optional(hdmi->dev, "hclk_vo1");
	if (IS_ERR(hdmi->hclk_vo1)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->hclk_vo1),
			      "failed to get hclk_vo1 clock\n");
		return PTR_ERR(hdmi->hclk_vo1);
	}

	hdmi->earc_clk = devm_clk_get_optional(hdmi->dev, "earc");
	if (IS_ERR(hdmi->earc_clk)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->earc_clk),
			      "failed to get earc_clk clock\n");
		return PTR_ERR(hdmi->earc_clk);
	}

	hdmi->hdmitx_ref = devm_clk_get_optional(hdmi->dev, "hdmitx_ref");
	if (IS_ERR(hdmi->hdmitx_ref)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->hdmitx_ref),
			      "failed to get hdmitx_ref clock\n");
		return PTR_ERR(hdmi->hdmitx_ref);
	}

	hdmi->pclk = devm_clk_get_optional(hdmi->dev, "pclk");
	if (IS_ERR(hdmi->pclk)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->pclk),
			      "failed to get pclk clock\n");
		return PTR_ERR(hdmi->pclk);
	}

	hdmi->link_clk = devm_clk_get_optional(hdmi->dev, "link_clk");
	if (IS_ERR(hdmi->link_clk)) {
		dev_err_probe(hdmi->dev, PTR_ERR(hdmi->link_clk),
			      "failed to get link_clk clock\n");
		return PTR_ERR(hdmi->link_clk);
	}

	hdmi->enable_gpio = devm_gpiod_get_optional(hdmi->dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hdmi->enable_gpio)) {
		ret = PTR_ERR(hdmi->enable_gpio);
		dev_err(hdmi->dev, "failed to request enable GPIO: %d\n", ret);
		return ret;
	}

	hdmi->skip_check_420_mode =
		of_property_read_bool(np, "skip-check-420-mode");

	if (of_get_property(np, "rockchip,phy-table", &val)) {
		phy_config = kmalloc(val, GFP_KERNEL);
		if (!phy_config) {
			/* use default table when kmalloc failed. */
			dev_err(hdmi->dev, "kmalloc phy table failed\n");

			return -ENOMEM;
		}
		phy_table_size = val / 16;
		of_property_read_u32_array(np, "rockchip,phy-table",
					   phy_config, val / sizeof(u32));
		ret = rockchip_hdmi_update_phy_table(hdmi, phy_config,
						     phy_table_size);
		if (ret) {
			kfree(phy_config);
			return ret;
		}
		kfree(phy_config);
	} else {
		dev_dbg(hdmi->dev, "use default hdmi phy table\n");
	}

	hdmi->hpd_gpiod = devm_gpiod_get_optional(hdmi->dev, "hpd", GPIOD_IN);

	if (IS_ERR(hdmi->hpd_gpiod)) {
		dev_err(hdmi->dev, "error getting HDP GPIO: %ld\n",
			PTR_ERR(hdmi->hpd_gpiod));
		return PTR_ERR(hdmi->hpd_gpiod);
	}

	if (hdmi->hpd_gpiod) {
		struct resource *res;
		struct platform_device *pdev = to_platform_device(hdmi->dev);

		/* gpio interrupt reflects hpd status */
		hdmi->hpd_irq = gpiod_to_irq(hdmi->hpd_gpiod);
		if (hdmi->hpd_irq < 0)
			return -EINVAL;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (!res) {
			DRM_DEV_ERROR(hdmi->dev, "failed to get gpio regs\n");
			return -EINVAL;
		}

		hdmi->gpio_base = devm_ioremap(hdmi->dev, res->start, resource_size(res));
		if (IS_ERR(hdmi->gpio_base)) {
			DRM_DEV_ERROR(hdmi->dev, "Unable to get gpio ioregmap\n");
			return PTR_ERR(hdmi->gpio_base);
		}

		dw_hdmi_rk3528_gpio_hpd_init(hdmi);
		ret = devm_request_threaded_irq(hdmi->dev, hdmi->hpd_irq, NULL,
						rockchip_hdmi_hpd_irq_handler,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"hdmi-hpd", hdmi);
		if (ret) {
			dev_err(hdmi->dev, "failed to request hpd IRQ: %d\n", ret);
			return ret;
		}

		hdmi->hpd_wake_en = device_property_read_bool(hdmi->dev, "hpd-wake-up");
		if (hdmi->hpd_wake_en)
			enable_irq_wake(hdmi->hpd_irq);
	}

	hdmi->p = devm_pinctrl_get(hdmi->dev);
	if (IS_ERR(hdmi->p)) {
		dev_err(hdmi->dev, "could not get pinctrl\n");
		return PTR_ERR(hdmi->p);
	}

	hdmi->idle_state = pinctrl_lookup_state(hdmi->p, "idle");
	if (IS_ERR(hdmi->idle_state)) {
		dev_dbg(hdmi->dev, "idle state is not defined\n");
		return 0;
	}

	hdmi->default_state = pinctrl_lookup_state(hdmi->p, "default");
	if (IS_ERR(hdmi->default_state)) {
		dev_err(hdmi->dev, "could not find default state\n");
		return PTR_ERR(hdmi->default_state);
	}

	return 0;
}

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct dw_hdmi *dw_hdmi, void *data,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct drm_connector *connector = container_of(info, struct drm_connector, display_info);
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct rockchip_hdmi *hdmi;

	if (!encoder) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (funcs->atomic_best_encoder)
			encoder = funcs->atomic_best_encoder(connector,
							     connector->state);
		else
			encoder = funcs->best_encoder(connector);
	}

	if (!encoder || !encoder->possible_crtcs)
		return MODE_BAD;

	hdmi = to_rockchip_hdmi(encoder);

	if (hdmi->is_hdmi_qp) {
		if (!hdmi->enable_gpio && mode->clock > 600000)
			return MODE_BAD;

		return MODE_OK;
	}

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
		return MODE_BAD;

	/*
	 * If sink max TMDS clock < 340MHz, we should check the mode pixel
	 * clock > 340MHz is YCbCr420 or not and whether the platform supports
	 * YCbCr420.
	 */
	if (!hdmi->skip_check_420_mode) {
		if (mode->clock > 340000 &&
		    connector->display_info.max_tmds_clock < 340000 &&
		    (!drm_mode_is_420(&connector->display_info, mode) ||
		     !connector->ycbcr_420_allowed))
			return MODE_BAD;

		if (hdmi->max_tmdsclk <= 340000 && mode->clock > 340000 &&
		    !drm_mode_is_420(&connector->display_info, mode))
			return MODE_BAD;
	};

	if (hdmi->phy) {
		if (hdmi->is_hdmi_qp)
			phy_set_bus_width(hdmi->phy, mode->clock * 10);
		else
			phy_set_bus_width(hdmi->phy, 8);
	}

	/*
	 * ensure all drm display mode can work, if someone want support more
	 * resolutions, please limit the possible_crtc, only connect to
	 * needed crtc.
	 */
	drm_for_each_crtc(crtc, connector->dev) {
		int pipe = drm_crtc_index(crtc);
		const struct rockchip_crtc_funcs *funcs =
						priv->crtc_funcs[pipe];

		if (!(encoder->possible_crtcs & drm_crtc_mask(crtc)))
			continue;
		if (!funcs || !funcs->mode_valid)
			continue;

		status = funcs->mode_valid(crtc, mode,
					   DRM_MODE_CONNECTOR_HDMIA);
		if (status != MODE_OK)
			return status;
	}

	return status;
}

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct rockchip_crtc_state *s;

	if (!crtc || !crtc->state) {
		dev_info(hdmi->dev, "%s old crtc state is null\n", __func__);
		return;
	}

	s = to_rockchip_crtc_state(crtc->state);

	if (crtc->state->active_changed) {
		if (hdmi->plat_data->split_mode) {
			s->output_if &= ~(VOP_OUTPUT_IF_HDMI0 | VOP_OUTPUT_IF_HDMI1);
		} else {
			if (!hdmi->id)
				s->output_if &= ~VOP_OUTPUT_IF_HDMI0;
			else
				s->output_if &= ~VOP_OUTPUT_IF_HDMI1;
		}
	}
	/*
	 * when plug out hdmi it will be switch cvbs and then phy bus width
	 * must be set as 8
	 */
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, 8);
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	u32 val;
	int mux;
	int ret;

	if (!crtc || !crtc->state) {
		dev_info(hdmi->dev, "%s old crtc state is null\n", __func__);
		return;
	}

	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);

	clk_set_rate(hdmi->phyref_clk,
		     crtc->state->adjusted_mode.crtc_clock * 1000);

	if (hdmi->is_hdmi_qp) {
		if (hdmi->link_cfg.frl_mode)
			gpiod_set_value(hdmi->enable_gpio, 0);
		else
			gpiod_set_value(hdmi->enable_gpio, 1);
	}

	if (hdmi->chip_data->lcdsel_grf_reg < 0)
		return;

	mux = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = hdmi->chip_data->lcdsel_lit;
	else
		val = hdmi->chip_data->lcdsel_big;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	ret = regmap_write(hdmi->regmap, hdmi->chip_data->lcdsel_grf_reg, val);
	if (ret != 0)
		DRM_DEV_ERROR(hdmi->dev, "Could not write to GRF: %d\n", ret);

	if (hdmi->chip_data->lcdsel_grf_reg == RK3288_GRF_SOC_CON6) {
		struct rockchip_crtc_state *s =
				to_rockchip_crtc_state(crtc->state);
		u32 mode_mask = mux ? RK3288_HDMI_LCDC1_YUV420 :
					RK3288_HDMI_LCDC0_YUV420;

		if (s->output_mode == ROCKCHIP_OUT_MODE_YUV420)
			val = HIWORD_UPDATE(mode_mask, mode_mask);
		else
			val = HIWORD_UPDATE(0, mode_mask);

		regmap_write(hdmi->regmap, RK3288_GRF_SOC_CON16, val);
	}

	clk_disable_unprepare(hdmi->grf_clk);
	DRM_DEV_DEBUG(hdmi->dev, "vop %s output to hdmi\n",
		      ret ? "LIT" : "BIG");
}

static int _dw_hdmi_rockchip_encoder_loader_protect(struct rockchip_hdmi *hdmi, bool on)
{
	int ret;

	if (on) {
		if (hdmi->is_hdmi_qp) {
			ret = clk_prepare_enable(hdmi->link_clk);
			if (ret < 0) {
				DRM_DEV_ERROR(hdmi->dev, "failed to enable link_clk %d\n", ret);
				return ret;
			}
		}

		hdmi->phy->power_count++;
	} else {
		clk_disable_unprepare(hdmi->link_clk);
		hdmi->phy->power_count--;
	}

	return 0;
}

static int dw_hdmi_rockchip_encoder_loader_protect(struct drm_encoder *encoder, bool on)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct rockchip_hdmi *secondary;

	_dw_hdmi_rockchip_encoder_loader_protect(hdmi, on);
	if (hdmi->plat_data->right) {
		secondary = rockchip_hdmi_find_by_id(hdmi->dev->driver, !hdmi->id);
		_dw_hdmi_rockchip_encoder_loader_protect(secondary, on);
	}

	return 0;
}

static void rk3588_set_link_mode(struct rockchip_hdmi *hdmi)
{
	int val;
	bool is_hdmi0;

	if (!hdmi->id)
		is_hdmi0 = true;
	else
		is_hdmi0 = false;

	if (!hdmi->link_cfg.frl_mode) {
		val = HIWORD_UPDATE(0, RK3588_HDMI21_MASK);
		if (is_hdmi0)
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON4, val);
		else
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON7, val);

		val = HIWORD_UPDATE(0, RK3588_COMPRESS_MODE_MASK | RK3588_COLOR_FORMAT_MASK);
		if (is_hdmi0)
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);
		else
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);

		return;
	}

	val = HIWORD_UPDATE(RK3588_HDMI21_MASK, RK3588_HDMI21_MASK);
	if (is_hdmi0)
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON4, val);
	else
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON7, val);

	if (hdmi->link_cfg.dsc_mode) {
		val = HIWORD_UPDATE(RK3588_COMPRESS_MODE_MASK | RK3588_COMPRESSED_DATA,
				    RK3588_COMPRESS_MODE_MASK | RK3588_COLOR_FORMAT_MASK);
		if (is_hdmi0)
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);
		else
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);
	} else {
		val = HIWORD_UPDATE(0, RK3588_COMPRESS_MODE_MASK | RK3588_COLOR_FORMAT_MASK);
		if (is_hdmi0)
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);
		else
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);
	}
}

static void rk3588_set_color_format(struct rockchip_hdmi *hdmi, u64 bus_format,
				    u32 depth)
{
	u32 val = 0;

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
		val = HIWORD_UPDATE(0, RK3588_COLOR_FORMAT_MASK);
		break;
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		val = HIWORD_UPDATE(RK3588_YUV420, RK3588_COLOR_FORMAT_MASK);
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		val = HIWORD_UPDATE(RK3588_YUV444, RK3588_COLOR_FORMAT_MASK);
		break;
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		val = HIWORD_UPDATE(RK3588_YUV422, RK3588_COLOR_FORMAT_MASK);
		break;
	default:
		dev_err(hdmi->dev, "can't set correct color format\n");
		return;
	}

	if (hdmi->link_cfg.dsc_mode)
		val = HIWORD_UPDATE(RK3588_COMPRESSED_DATA, RK3588_COLOR_FORMAT_MASK);

	if (depth == 8 || bus_format == MEDIA_BUS_FMT_YUYV10_1X20)
		val |= HIWORD_UPDATE(RK3588_8BPC, RK3588_COLOR_DEPTH_MASK);
	else
		val |= HIWORD_UPDATE(RK3588_10BPC, RK3588_COLOR_DEPTH_MASK);

	if (!hdmi->id)
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);
	else
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);
}

static void rk3588_set_hdcp_status(void *data, u8 status)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	hdmi->hdcp_status = status;
}

static void rk3588_set_hdcp2_enable(void *data, bool enable)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	u32 val;

	if (enable)
		val = HIWORD_UPDATE(HDCP1_P1_GPIO_IN, HDCP1_P1_GPIO_IN);
	else
		val = HIWORD_UPDATE(0, HDCP1_P1_GPIO_IN);

	regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON1, val);
}

static void rk3588_set_grf_cfg(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	int color_depth;

	rk3588_set_link_mode(hdmi);
	color_depth = hdmi_bus_fmt_color_depth(hdmi->bus_format);
	rk3588_set_color_format(hdmi, hdmi->bus_format, color_depth);
}

static u64 rk3588_get_grf_color_fmt(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	u32 val, depth;
	u64 bus_format;

	if (!hdmi->id)
		regmap_read(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, &val);
	else
		regmap_read(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, &val);

	depth = (val & RK3588_COLOR_DEPTH_MASK) >> 4;

	switch (val & RK3588_COLOR_FORMAT_MASK) {
	case RK3588_YUV444:
		if (!depth)
			bus_format = MEDIA_BUS_FMT_YUV8_1X24;
		else
			bus_format = MEDIA_BUS_FMT_YUV10_1X30;
		break;
	case RK3588_YUV422:
		bus_format = MEDIA_BUS_FMT_YUYV10_1X20;
		break;
	case RK3588_YUV420:
		if (!depth)
			bus_format = MEDIA_BUS_FMT_UYYVYY8_0_5X24;
		else
			bus_format = MEDIA_BUS_FMT_UYYVYY10_0_5X30;
		break;
	case RK3588_RGB:
		if (!depth)
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		else
			bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
		break;
	default:
		dev_err(hdmi->dev, "can't get correct color format\n");
		bus_format = MEDIA_BUS_FMT_YUV8_1X24;
		break;
	}

	return bus_format;
}

static void
dw_hdmi_rockchip_select_output(struct drm_connector_state *conn_state,
			       struct drm_crtc_state *crtc_state,
			       struct rockchip_hdmi *hdmi,
			       unsigned int *color_format,
			       unsigned int *output_mode,
			       unsigned long *bus_format,
			       unsigned int *bus_width,
			       unsigned long *enc_out_encoding,
			       unsigned int *eotf)
{
	struct drm_display_info *info = &conn_state->connector->display_info;
	struct drm_display_mode mode;
	struct hdr_output_metadata *hdr_metadata;
	u32 vic;
	unsigned long tmdsclock, pixclock;
	unsigned int color_depth;
	bool support_dc = false;
	bool sink_is_hdmi = true;
	bool yuv422_out = false;
	u32 max_tmds_clock = info->max_tmds_clock;
	int output_eotf;

	drm_mode_copy(&mode, &crtc_state->mode);
	pixclock = mode.crtc_clock;
	if (hdmi->plat_data->split_mode) {
		drm_mode_convert_to_origin_mode(&mode);
		pixclock /= 2;
	}

	vic = drm_match_cea_mode(&mode);

	if (!hdmi->is_hdmi_qp)
		sink_is_hdmi = dw_hdmi_get_output_whether_hdmi(hdmi->hdmi);
	else
		sink_is_hdmi = dw_hdmi_qp_get_output_whether_hdmi(hdmi->hdmi_qp);

	*color_format = RK_IF_FORMAT_RGB;

	switch (hdmi->hdmi_output) {
	case RK_IF_FORMAT_YCBCR_HQ:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = RK_IF_FORMAT_YCBCR444;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = RK_IF_FORMAT_YCBCR422;
		else if (conn_state->connector->ycbcr_420_allowed &&
			 drm_mode_is_420(info, &mode) &&
			 (pixclock >= 594000 && !hdmi->is_hdmi_qp))
			*color_format = RK_IF_FORMAT_YCBCR420;
		break;
	case RK_IF_FORMAT_YCBCR_LQ:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, &mode) && pixclock >= 594000)
			*color_format = RK_IF_FORMAT_YCBCR420;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = RK_IF_FORMAT_YCBCR422;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = RK_IF_FORMAT_YCBCR444;
		break;
	case RK_IF_FORMAT_YCBCR420:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, &mode) && pixclock >= 594000)
			*color_format = RK_IF_FORMAT_YCBCR420;
		break;
	case RK_IF_FORMAT_YCBCR422:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = RK_IF_FORMAT_YCBCR422;
		break;
	case RK_IF_FORMAT_YCBCR444:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = RK_IF_FORMAT_YCBCR444;
		break;
	case RK_IF_FORMAT_RGB:
	default:
		break;
	}

	if (*color_format == RK_IF_FORMAT_RGB &&
	    info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_30)
		support_dc = true;
	if (*color_format == RK_IF_FORMAT_YCBCR444 &&
	    info->edid_hdmi_dc_modes &
	    (DRM_EDID_HDMI_DC_Y444 | DRM_EDID_HDMI_DC_30))
		support_dc = true;
	if (*color_format == RK_IF_FORMAT_YCBCR422)
		support_dc = true;
	if (*color_format == RK_IF_FORMAT_YCBCR420 &&
	    info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
		support_dc = true;

	if (hdmi->colordepth > 8 && support_dc)
		color_depth = 10;
	else
		color_depth = 8;

	*eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
	if (conn_state->hdr_output_metadata) {
		hdr_metadata = (struct hdr_output_metadata *)
			conn_state->hdr_output_metadata->data;
		output_eotf = hdr_metadata->hdmi_metadata_type1.eotf;
		if (output_eotf > HDMI_EOTF_TRADITIONAL_GAMMA_SDR &&
		    output_eotf <= HDMI_EOTF_BT_2100_HLG)
			*eotf = output_eotf;
	}

	hdmi->colorimetry = conn_state->colorspace;

	/* bt2020 sdr/hdr output */
	if ((hdmi->colorimetry >= DRM_MODE_COLORIMETRY_BT2020_CYCC) &&
	    (hdmi->colorimetry <= DRM_MODE_COLORIMETRY_BT2020_YCC) &&
	    hdmi->edid_colorimetry & (BIT(6) | BIT(7))) {
		*enc_out_encoding = V4L2_YCBCR_ENC_BT2020;
		yuv422_out = true;
	/* bt709 hdr output */
	} else if ((hdmi->colorimetry <= DRM_MODE_COLORIMETRY_BT2020_CYCC) &&
		   (hdmi->colorimetry >= DRM_MODE_COLORIMETRY_BT2020_YCC) &&
		   (conn_state->connector->hdr_sink_metadata.hdmi_type1.eotf & BIT(*eotf) &&
		    *eotf > HDMI_EOTF_TRADITIONAL_GAMMA_SDR)) {
		*enc_out_encoding = V4L2_YCBCR_ENC_709;
		yuv422_out = true;
	} else if ((vic == 6) || (vic == 7) || (vic == 21) || (vic == 22) ||
		   (vic == 2) || (vic == 3) || (vic == 17) || (vic == 18)) {
		*enc_out_encoding = V4L2_YCBCR_ENC_601;
	} else {
		*enc_out_encoding = V4L2_YCBCR_ENC_709;
	}

	if ((yuv422_out || hdmi->hdmi_output == RK_IF_FORMAT_YCBCR_HQ) && color_depth == 10 &&
	    (hdmi_bus_fmt_color_depth(hdmi->prev_bus_format) == 8 ||
	     hdmi_bus_fmt_to_color_format(hdmi->prev_bus_format) == RK_IF_FORMAT_YCBCR422)) {
		/* We prefer use YCbCr422 to send hdr 10bit */
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = RK_IF_FORMAT_YCBCR422;
	}

	if (mode.flags & DRM_MODE_FLAG_DBLCLK)
		pixclock *= 2;
	if ((mode.flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING)
		pixclock *= 2;

	if (hdmi->is_hdmi_qp && mode.clock >= 600000)
		*color_format = RK_IF_FORMAT_YCBCR420;

	if (!sink_is_hdmi) {
		*color_format = RK_IF_FORMAT_RGB;
		color_depth = 8;
	}

	if (*color_format == RK_IF_FORMAT_YCBCR422 || color_depth == 8)
		tmdsclock = pixclock;
	else
		tmdsclock = pixclock * (color_depth) / 8;

	if (*color_format == RK_IF_FORMAT_YCBCR420)
		tmdsclock /= 2;

	/* XXX: max_tmds_clock of some sink is 0, we think it is 340MHz. */
	if (!max_tmds_clock)
		max_tmds_clock = 340000;

	max_tmds_clock = min(max_tmds_clock, hdmi->max_tmdsclk);

	if (hdmi->is_hdmi_qp && hdmi->link_cfg.rate_per_lane && mode.clock > 600000)
		max_tmds_clock =
			hdmi->link_cfg.frl_lanes * hdmi->link_cfg.rate_per_lane * 1000000;

	if (tmdsclock > max_tmds_clock) {
		if (max_tmds_clock >= 594000) {
			color_depth = 8;
		} else if (max_tmds_clock > 340000) {
			if (drm_mode_is_420(info, &mode) || tmdsclock >= 594000)
				*color_format = RK_IF_FORMAT_YCBCR420;
		} else {
			color_depth = 8;
			if (drm_mode_is_420(info, &mode) || tmdsclock >= 594000)
				*color_format = RK_IF_FORMAT_YCBCR420;
		}
	}

	if (*color_format == RK_IF_FORMAT_YCBCR420) {
		*output_mode = ROCKCHIP_OUT_MODE_YUV420;
		if (color_depth > 8)
			*bus_format = MEDIA_BUS_FMT_UYYVYY10_0_5X30;
		else
			*bus_format = MEDIA_BUS_FMT_UYYVYY8_0_5X24;
		*bus_width = color_depth / 2;
	} else {
		*output_mode = ROCKCHIP_OUT_MODE_AAAA;
		if (color_depth > 8) {
			if (*color_format != RK_IF_FORMAT_RGB &&
			    !hdmi->unsupported_yuv_input)
				*bus_format = MEDIA_BUS_FMT_YUV10_1X30;
			else
				*bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
		} else {
			if (*color_format != RK_IF_FORMAT_RGB &&
			    !hdmi->unsupported_yuv_input)
				*bus_format = MEDIA_BUS_FMT_YUV8_1X24;
			else
				*bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		}
		if (*color_format == RK_IF_FORMAT_YCBCR422)
			*bus_width = 8;
		else
			*bus_width = color_depth;
	}

	hdmi->bus_format = *bus_format;

	if (*color_format == RK_IF_FORMAT_YCBCR422) {
		if (hdmi->is_hdmi_qp) {
			if (color_depth == 12)
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV12_1X24;
			else if (color_depth == 10)
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV10_1X20;
			else
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV8_1X16;

			*bus_format = hdmi->output_bus_format;
			hdmi->bus_format = *bus_format;
			*output_mode = ROCKCHIP_OUT_MODE_YUV422;
		} else {
			if (color_depth == 12)
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY12_1X24;
			else if (color_depth == 10)
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY10_1X20;
			else
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
		}
	} else {
		hdmi->output_bus_format = *bus_format;
	}
}

static bool
dw_hdmi_rockchip_check_color(struct drm_connector_state *conn_state,
			     struct rockchip_hdmi *hdmi)
{
	struct drm_crtc_state *crtc_state = conn_state->crtc->state;
	unsigned int colorformat;
	unsigned long bus_format;
	unsigned long output_bus_format = hdmi->output_bus_format;
	unsigned long enc_out_encoding = hdmi->enc_out_encoding;
	unsigned int eotf, bus_width;
	unsigned int output_mode;

	dw_hdmi_rockchip_select_output(conn_state, crtc_state, hdmi,
				       &colorformat,
				       &output_mode, &bus_format, &bus_width,
				       &hdmi->enc_out_encoding, &eotf);

	if (output_bus_format != hdmi->output_bus_format ||
	    enc_out_encoding != hdmi->enc_out_encoding)
		return true;
	else
		return false;
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	unsigned int colorformat, bus_width, tmdsclk;
	struct drm_display_mode mode;
	unsigned int output_mode;
	unsigned long bus_format;
	int color_depth;
	bool secondary = false;

	/*
	 * There are two hdmi but only one encoder in split mode,
	 * so we need to check twice.
	 */
secondary:
	drm_mode_copy(&mode, &crtc_state->mode);

	if (hdmi->plat_data->split_mode)
		drm_mode_convert_to_origin_mode(&mode);

	dw_hdmi_rockchip_select_output(conn_state, crtc_state, hdmi,
				       &colorformat,
				       &output_mode, &bus_format, &bus_width,
				       &hdmi->enc_out_encoding, &s->eotf);

	s->bus_format = bus_format;
	if (hdmi->is_hdmi_qp) {
		color_depth = hdmi_bus_fmt_color_depth(bus_format);
		tmdsclk = hdmi_get_tmdsclock(hdmi, crtc_state->mode.clock);
		if (hdmi_bus_fmt_is_yuv420(hdmi->output_bus_format))
			tmdsclk /= 2;
		hdmi_select_link_config(hdmi, crtc_state, tmdsclk);

		if (hdmi->link_cfg.frl_mode) {
			/* in the current version, support max 40G frl */
			if (hdmi->link_cfg.rate_per_lane >= 10) {
				hdmi->link_cfg.frl_lanes = 4;
				hdmi->link_cfg.rate_per_lane = 10;
			}
			bus_width = hdmi->link_cfg.frl_lanes *
				hdmi->link_cfg.rate_per_lane * 1000000;
			/* 10 bit color depth and frl mode */
			if (color_depth == 10)
				bus_width |=
					COLOR_DEPTH_10BIT | HDMI_FRL_MODE;
			else
				bus_width |= HDMI_FRL_MODE;
		} else {
			bus_width = hdmi_get_tmdsclock(hdmi, mode.clock * 10);
			if (hdmi_bus_fmt_is_yuv420(hdmi->output_bus_format))
				bus_width /= 2;

			if (color_depth == 10 && !hdmi_bus_fmt_is_yuv422(hdmi->output_bus_format))
				bus_width |= COLOR_DEPTH_10BIT;
		}
	}

	hdmi->phy_bus_width = bus_width;

	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, bus_width);

	s->output_type = DRM_MODE_CONNECTOR_HDMIA;
	s->tv_state = &conn_state->tv;

	if (hdmi->plat_data->split_mode) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		if (hdmi->plat_data->right && hdmi->id)
			s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;
		s->output_if |= VOP_OUTPUT_IF_HDMI0 | VOP_OUTPUT_IF_HDMI1;
	} else {
		if (!hdmi->id)
			s->output_if |= VOP_OUTPUT_IF_HDMI0;
		else
			s->output_if |= VOP_OUTPUT_IF_HDMI1;
	}

	s->output_mode = output_mode;
	hdmi->bus_format = s->bus_format;

	if (hdmi->enc_out_encoding == V4L2_YCBCR_ENC_BT2020)
		s->color_space = V4L2_COLORSPACE_BT2020;
	else if (colorformat == RK_IF_FORMAT_RGB)
		s->color_space = V4L2_COLORSPACE_DEFAULT;
	else if (hdmi->enc_out_encoding == V4L2_YCBCR_ENC_709)
		s->color_space = V4L2_COLORSPACE_REC709;
	else
		s->color_space = V4L2_COLORSPACE_SMPTE170M;

	if (hdmi->plat_data->split_mode && !secondary) {
		hdmi = rockchip_hdmi_find_by_id(hdmi->dev->driver, !hdmi->id);
		secondary = true;
		goto secondary;
	}

	return 0;
}


static unsigned long
dw_hdmi_rockchip_get_input_bus_format(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->bus_format;
}

static unsigned long
dw_hdmi_rockchip_get_output_bus_format(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->output_bus_format;
}

static unsigned long
dw_hdmi_rockchip_get_enc_in_encoding(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static unsigned long
dw_hdmi_rockchip_get_enc_out_encoding(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static unsigned long
dw_hdmi_rockchip_get_quant_range(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdmi_quant_range;
}

static struct drm_property *
dw_hdmi_rockchip_get_hdr_property(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdr_panel_metadata_property;
}

static struct drm_property_blob *
dw_hdmi_rockchip_get_hdr_blob(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdr_panel_blob_ptr;
}

static void dw_hdmi_rockchip_update_color_format(struct drm_connector_state *conn_state,
						 void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_rockchip_check_color(conn_state, hdmi);
}

static bool
dw_hdmi_rockchip_get_color_changed(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	bool ret = false;

	if (hdmi->color_changed)
		ret = true;
	hdmi->color_changed = 0;

	return ret;
}

static int
dw_hdmi_rockchip_get_yuv422_format(struct drm_connector *connector,
				   struct edid *edid)
{
	if (!connector || !edid)
		return -EINVAL;

	return rockchip_drm_get_yuv422_format(connector, edid);
}

static int
dw_hdmi_rockchip_get_edid_dsc_info(void *data, struct edid *edid)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (!edid)
		return -EINVAL;

	memset(&hdmi->dsc_cap, 0, sizeof(hdmi->dsc_cap));
	hdmi->max_frl_rate_per_lane = 0;
	hdmi->max_lanes = 0;
	hdmi->add_func = 0;

	return rockchip_drm_parse_cea_ext(&hdmi->dsc_cap,
					  &hdmi->max_frl_rate_per_lane,
					  &hdmi->max_lanes, &hdmi->add_func, edid);
}

static int
dw_hdmi_rockchip_get_next_hdr_data(void *data, struct edid *edid,
				   struct drm_connector *connector)
{
	int ret;
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct next_hdr_sink_data *sink_data = &hdmi->next_hdr_data;
	size_t size = sizeof(*sink_data);
	struct drm_property *property = hdmi->next_hdr_sink_data_property;
	struct drm_property_blob *blob = hdmi->hdr_panel_blob_ptr;

	if (!edid)
		return -EINVAL;

	rockchip_drm_parse_next_hdr(sink_data, edid);

	ret = drm_property_replace_global_blob(connector->dev, &blob, size, sink_data,
					       &connector->base, property);

	return ret;
};

static int dw_hdmi_rockchip_get_colorimetry(void *data, struct edid *edid)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return rockchip_drm_parse_colorimetry_data_block(&hdmi->edid_colorimetry, edid);
}

static
struct dw_hdmi_link_config *dw_hdmi_rockchip_get_link_cfg(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return &hdmi->link_cfg;
}

static int dw_hdmi_rockchip_get_vp_id(struct drm_crtc_state *crtc_state)
{
	struct rockchip_crtc_state *s;

	s = to_rockchip_crtc_state(crtc_state);

	return s->vp_id;
}

static int dw_hdmi_dclk_set(void *data, bool enable, int vp_id)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	char clk_name[16];
	struct clk *dclk;
	int ret;

	snprintf(clk_name, sizeof(clk_name), "dclk_vp%d", vp_id);

	dclk = devm_clk_get_optional(hdmi->dev, clk_name);
	if (IS_ERR(dclk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get %s\n", clk_name);
		return PTR_ERR(dclk);
	} else if (!dclk) {
		if (hdmi->is_hdmi_qp) {
			DRM_DEV_ERROR(hdmi->dev, "failed to get %s\n", clk_name);
			return -ENOENT;
		}

		return 0;
	}

	if (enable) {
		ret = clk_prepare_enable(dclk);
		if (ret < 0)
			DRM_DEV_ERROR(hdmi->dev, "failed to enable dclk for video port%d - %d\n",
				      vp_id, ret);
	} else {
		clk_disable_unprepare(dclk);
	}

	return 0;
}

static int dw_hdmi_link_clk_set(void *data, bool enable)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	u64 phy_clk = hdmi->phy_bus_width;
	int ret;

	if (enable) {
		ret = clk_prepare_enable(hdmi->link_clk);
		if (ret < 0) {
			DRM_DEV_ERROR(hdmi->dev, "failed to enable link_clk %d\n", ret);
			return ret;
		}

		if (((phy_clk & DATA_RATE_MASK) <= 6000000) &&
		    (phy_clk & COLOR_DEPTH_10BIT))
			phy_clk = (phy_clk & DATA_RATE_MASK) * 10 * 8;
		else
			phy_clk = (phy_clk & DATA_RATE_MASK) * 100;

		/*
		 * To be compatible with vop dclk usage scenarios, hdmi phy pll clk
		 * is set according to dclk rate.
		 * But phy pll actual frequency will varies according to the color depth.
		 * So we should get the actual frequency or clk_set_rate may not change
		 * pll frequency when 8/10 bit switch.
		 */
		clk_get_rate(hdmi->link_clk);
		clk_set_rate(hdmi->link_clk, phy_clk);
	} else {
		clk_disable_unprepare(hdmi->link_clk);
	}
	return 0;
}

static bool
dw_hdmi_rockchip_check_hdr_color_change(struct drm_connector_state *conn_state,
					void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (!conn_state || !data)
		return false;

	if (dw_hdmi_rockchip_check_color(conn_state, hdmi))
		return true;

	return false;
}

static void dw_hdmi_rockchip_set_prev_bus_format(void *data, unsigned long bus_format)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	hdmi->prev_bus_format = bus_format;
}

static void dw_hdmi_rockchip_set_ddc_io(void *data, bool enable)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (!hdmi->p || !hdmi->idle_state || !hdmi->default_state)
		return;

	if (!enable) {
		if (pinctrl_select_state(hdmi->p, hdmi->idle_state))
			dev_err(hdmi->dev, "could not select idle state\n");
	} else {
		if (pinctrl_select_state(hdmi->p, hdmi->default_state))
			dev_err(hdmi->dev, "could not select default state\n");
	}
}

static void dw_hdmi_rockchip_set_hdcp14_mem(void *data, bool enable)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	u32 val;

	val = HIWORD_UPDATE(enable << 15, RK3588_HDMI_HDCP14_MEM_EN);
	if (!hdmi->id)
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON4, val);
	else
		regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON7, val);
}

static const struct drm_prop_enum_list color_depth_enum_list[] = {
	{ 0, "Automatic" }, /* Prefer highest color depth */
	{ 8, "24bit" },
	{ 10, "30bit" },
};

static const struct drm_prop_enum_list drm_hdmi_output_enum_list[] = {
	{ RK_IF_FORMAT_RGB, "rgb" },
	{ RK_IF_FORMAT_YCBCR444, "ycbcr444" },
	{ RK_IF_FORMAT_YCBCR422, "ycbcr422" },
	{ RK_IF_FORMAT_YCBCR420, "ycbcr420" },
	{ RK_IF_FORMAT_YCBCR_HQ, "ycbcr_high_subsampling" },
	{ RK_IF_FORMAT_YCBCR_LQ, "ycbcr_low_subsampling" },
	{ RK_IF_FORMAT_MAX, "invalid_output" },
};

static const struct drm_prop_enum_list quant_range_enum_list[] = {
	{ HDMI_QUANTIZATION_RANGE_DEFAULT, "default" },
	{ HDMI_QUANTIZATION_RANGE_LIMITED, "limit" },
	{ HDMI_QUANTIZATION_RANGE_FULL, "full" },
};

static const struct drm_prop_enum_list output_hdmi_dvi_enum_list[] = {
	{ 0, "auto" },
	{ 1, "force_hdmi" },
	{ 2, "force_dvi" },
};

static const struct drm_prop_enum_list output_type_cap_list[] = {
	{ 0, "DVI" },
	{ 1, "HDMI" },
};

static const struct drm_prop_enum_list allm_enable_list[] = {
	{ 0, "disable" },
	{ 1, "enable" },
};

static void
dw_hdmi_rockchip_attach_properties(struct drm_connector *connector,
				   unsigned int color, int version,
				   void *data, bool allm_en)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_property *prop;
	struct rockchip_drm_private *private = connector->dev->dev_private;
	int ret;

	switch (color) {
	case MEDIA_BUS_FMT_RGB101010_1X30:
		hdmi->hdmi_output = RK_IF_FORMAT_RGB;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR444;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR444;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_YUYV10_1X20:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR422;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR422;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR420;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		hdmi->hdmi_output = RK_IF_FORMAT_YCBCR420;
		hdmi->colordepth = 10;
		break;
	default:
		hdmi->hdmi_output = RK_IF_FORMAT_RGB;
		hdmi->colordepth = 8;
	}

	hdmi->bus_format = color;
	hdmi->prev_bus_format = color;

	if (hdmi->hdmi_output == RK_IF_FORMAT_YCBCR422) {
		if (hdmi->is_hdmi_qp) {
			if (hdmi->colordepth == 12)
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV12_1X24;
			else if (hdmi->colordepth == 10)
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV10_1X20;
			else
				hdmi->output_bus_format = MEDIA_BUS_FMT_YUYV8_1X16;
		} else {
			if (hdmi->colordepth == 12)
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY12_1X24;
			else if (hdmi->colordepth == 10)
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY10_1X20;
			else
				hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
		}
	} else {
		hdmi->output_bus_format = hdmi->bus_format;
	}

	/* RK3368 does not support deep color mode */
	if (!hdmi->color_depth_property && !hdmi->unsupported_deep_color) {
		prop = drm_property_create_enum(connector->dev, 0,
						RK_IF_PROP_COLOR_DEPTH,
						color_depth_enum_list,
						ARRAY_SIZE(color_depth_enum_list));
		if (prop) {
			hdmi->color_depth_property = prop;
			drm_object_attach_property(&connector->base, prop, 0);
		}
	}

	prop = drm_property_create_enum(connector->dev, 0, RK_IF_PROP_COLOR_FORMAT,
					drm_hdmi_output_enum_list,
					ARRAY_SIZE(drm_hdmi_output_enum_list));
	if (prop) {
		hdmi->hdmi_output_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_range(connector->dev, 0,
					 RK_IF_PROP_COLOR_DEPTH_CAPS,
					 0, 0xff);
	if (prop) {
		hdmi->colordepth_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_range(connector->dev, 0,
					 RK_IF_PROP_COLOR_FORMAT_CAPS,
					 0, 0xf);
	if (prop) {
		hdmi->outputmode_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create(connector->dev,
				   DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "HDR_PANEL_METADATA", 0);
	if (prop) {
		hdmi->hdr_panel_metadata_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create(connector->dev,
				   DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "NEXT_HDR_SINK_DATA", 0);
	if (prop) {
		hdmi->next_hdr_sink_data_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_bool(connector->dev, 0, "allm_capacity");
	if (prop) {
		hdmi->allm_capacity = prop;
		drm_object_attach_property(&connector->base, prop,
					   !!(hdmi->add_func & SUPPORT_HDMI_ALLM));
	}

	prop = drm_property_create_enum(connector->dev, 0,
					"allm_enable",
					allm_enable_list,
					ARRAY_SIZE(allm_enable_list));
	if (prop) {
		hdmi->allm_enable = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}
	hdmi->enable_allm = allm_en;

	prop = drm_property_create_enum(connector->dev, 0,
					"output_hdmi_dvi",
					output_hdmi_dvi_enum_list,
					ARRAY_SIZE(output_hdmi_dvi_enum_list));
	if (prop) {
		hdmi->output_hdmi_dvi = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_enum(connector->dev, 0,
					"output_type_capacity",
					output_type_cap_list,
					ARRAY_SIZE(output_type_cap_list));
	if (prop) {
		hdmi->output_type_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	if (!hdmi->is_hdmi_qp) {
		prop = drm_property_create_enum(connector->dev, 0,
						"hdmi_quant_range",
						quant_range_enum_list,
						ARRAY_SIZE(quant_range_enum_list));
		if (prop) {
			hdmi->quant_range = prop;
			drm_object_attach_property(&connector->base, prop, 0);
		}
	}

	prop = connector->dev->mode_config.hdr_output_metadata_property;
	if (hdmi->is_hdmi_qp)
		drm_object_attach_property(&connector->base, prop, 0);

	if (!drm_mode_create_hdmi_colorspace_property(connector))
		drm_object_attach_property(&connector->base,
					   connector->colorspace_property, 0);
	drm_object_attach_property(&connector->base, private->connector_id_prop, hdmi->id);

	ret = drm_connector_attach_content_protection_property(connector, true);
	if (ret) {
		dev_err(hdmi->dev, "failed to attach content protection: %d\n", ret);
		return;
	}

	prop = drm_property_create_range(connector->dev, 0, RK_IF_PROP_ENCRYPTED,
					 RK_IF_HDCP_ENCRYPTED_NONE, RK_IF_HDCP_ENCRYPTED_LEVEL2);
	if (!prop) {
		dev_err(hdmi->dev, "create hdcp encrypted prop for hdmi%d failed\n", hdmi->id);
		return;
	}
	hdmi->hdcp_state_property = prop;
	drm_object_attach_property(&connector->base, prop, RK_IF_HDCP_ENCRYPTED_NONE);
}

static void
dw_hdmi_rockchip_destroy_properties(struct drm_connector *connector,
				    void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (hdmi->color_depth_property) {
		drm_property_destroy(connector->dev,
				     hdmi->color_depth_property);
		hdmi->color_depth_property = NULL;
	}

	if (hdmi->hdmi_output_property) {
		drm_property_destroy(connector->dev,
				     hdmi->hdmi_output_property);
		hdmi->hdmi_output_property = NULL;
	}

	if (hdmi->colordepth_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->colordepth_capacity);
		hdmi->colordepth_capacity = NULL;
	}

	if (hdmi->outputmode_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->outputmode_capacity);
		hdmi->outputmode_capacity = NULL;
	}

	if (hdmi->quant_range) {
		drm_property_destroy(connector->dev,
				     hdmi->quant_range);
		hdmi->quant_range = NULL;
	}

	if (hdmi->hdr_panel_metadata_property) {
		drm_property_destroy(connector->dev,
				     hdmi->hdr_panel_metadata_property);
		hdmi->hdr_panel_metadata_property = NULL;
	}

	if (hdmi->next_hdr_sink_data_property) {
		drm_property_destroy(connector->dev,
				     hdmi->next_hdr_sink_data_property);
		hdmi->next_hdr_sink_data_property = NULL;
	}

	if (hdmi->output_hdmi_dvi) {
		drm_property_destroy(connector->dev,
				     hdmi->output_hdmi_dvi);
		hdmi->output_hdmi_dvi = NULL;
	}

	if (hdmi->output_type_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->output_type_capacity);
		hdmi->output_type_capacity = NULL;
	}

	if (hdmi->allm_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->allm_capacity);
		hdmi->allm_capacity = NULL;
	}

	if (hdmi->allm_enable) {
		drm_property_destroy(connector->dev, hdmi->allm_enable);
		hdmi->allm_enable = NULL;
	}
}

static int
dw_hdmi_rockchip_set_property(struct drm_connector *connector,
			      struct drm_connector_state *state,
			      struct drm_property *property,
			      u64 val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->color_depth_property) {
		hdmi->colordepth = val;
		/* If hdmi is disconnected, state->crtc is null */
		if (!state->crtc)
			return 0;
		if (dw_hdmi_rockchip_check_color(state, hdmi))
			hdmi->color_changed++;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		hdmi->hdmi_output = val;
		if (!state->crtc)
			return 0;
		if (dw_hdmi_rockchip_check_color(state, hdmi))
			hdmi->color_changed++;
		return 0;
	} else if (property == hdmi->quant_range) {
		u64 quant_range = hdmi->hdmi_quant_range;

		hdmi->hdmi_quant_range = val;
		if (quant_range != hdmi->hdmi_quant_range)
			dw_hdmi_set_quant_range(hdmi->hdmi);
		return 0;
	} else if (property == config->hdr_output_metadata_property) {
		return 0;
	} else if (property == hdmi->output_hdmi_dvi) {
		if (!hdmi->is_hdmi_qp) {
			if (hdmi->force_output != val)
				hdmi->color_changed++;
			hdmi->force_output = val;
			dw_hdmi_set_output_type(hdmi->hdmi, val);
		} else {
			hdmi->force_output = val;
			dw_hdmi_qp_set_output_type(hdmi->hdmi_qp, val);
		}
		return 0;
	} else if (property == hdmi->colordepth_capacity) {
		return 0;
	} else if (property == hdmi->outputmode_capacity) {
		return 0;
	} else if (property == hdmi->output_type_capacity) {
		return 0;
	} else if (property == hdmi->allm_capacity) {
		return 0;
	} else if (property == hdmi->allm_enable) {
		u64 allm_enable = hdmi->enable_allm;

		hdmi->enable_allm = val;
		if (allm_enable != hdmi->enable_allm)
			dw_hdmi_qp_set_allm_enable(hdmi->hdmi_qp, hdmi->enable_allm);
	} else if (property == hdmi->hdcp_state_property) {
		return 0;
	}

	DRM_ERROR("Unknown property [PROP:%d:%s]\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static int
dw_hdmi_rockchip_get_property(struct drm_connector *connector,
			      const struct drm_connector_state *state,
			      struct drm_property *property,
			      u64 *val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_display_info *info = &connector->display_info;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->color_depth_property) {
		*val = hdmi->colordepth;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		*val = hdmi->hdmi_output;
		return 0;
	} else if (property == hdmi->colordepth_capacity) {
		*val = BIT(RK_IF_DEPTH_8);
		/* RK3368 only support 8bit */
		if (hdmi->unsupported_deep_color)
			return 0;
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_30)
			*val |= BIT(RK_IF_DEPTH_10);
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_36)
			*val |= BIT(RK_IF_DEPTH_12);
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_48)
			*val |= BIT(RK_IF_DEPTH_16);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
			*val |= BIT(RK_IF_DEPTH_420_10);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36)
			*val |= BIT(RK_IF_DEPTH_420_12);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48)
			*val |= BIT(RK_IF_DEPTH_420_16);
		return 0;
	} else if (property == hdmi->outputmode_capacity) {
		*val = BIT(RK_IF_FORMAT_RGB);
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*val |= BIT(RK_IF_FORMAT_YCBCR444);
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*val |= BIT(RK_IF_FORMAT_YCBCR422);
		if (connector->ycbcr_420_allowed &&
		    info->color_formats & DRM_COLOR_FORMAT_YCRCB420)
			*val |= BIT(RK_IF_FORMAT_YCBCR420);
		return 0;
	} else if (property == hdmi->quant_range) {
		*val = hdmi->hdmi_quant_range;
		return 0;
	} else if (property == config->hdr_output_metadata_property) {
		*val = state->hdr_output_metadata ?
			state->hdr_output_metadata->base.id : 0;
		return 0;
	} else if (property == hdmi->output_hdmi_dvi) {
		*val = hdmi->force_output;
		return 0;
	} else if (property == hdmi->output_type_capacity) {
		if (!hdmi->is_hdmi_qp)
			*val = dw_hdmi_get_output_type_cap(hdmi->hdmi);
		else
			*val = dw_hdmi_qp_get_output_type_cap(hdmi->hdmi_qp);
		return 0;
	} else if (property == hdmi->allm_capacity) {
		*val = !!(hdmi->add_func & SUPPORT_HDMI_ALLM);
		return 0;
	} else if (property == hdmi->allm_enable) {
		*val = hdmi->enable_allm;
		return 0;
	} else if (property == hdmi->hdcp_state_property) {
		if (hdmi->hdcp_status & BIT(1))
			*val = RK_IF_HDCP_ENCRYPTED_LEVEL2;
		else if (hdmi->hdcp_status & BIT(0))
			*val = RK_IF_HDCP_ENCRYPTED_LEVEL1;
		else
			*val = RK_IF_HDCP_ENCRYPTED_NONE;
		return 0;
	}

	DRM_ERROR("Unknown property [PROP:%d:%s]\n",
		  property->base.id, property->name);

	return -EINVAL;
}

static const struct dw_hdmi_property_ops dw_hdmi_rockchip_property_ops = {
	.attach_properties	= dw_hdmi_rockchip_attach_properties,
	.destroy_properties	= dw_hdmi_rockchip_destroy_properties,
	.set_property		= dw_hdmi_rockchip_set_property,
	.get_property		= dw_hdmi_rockchip_get_property,
};

static void dw_hdmi_rockchip_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc;
	struct rockchip_crtc_state *s;

	if (!encoder->crtc)
		return;
	crtc = encoder->crtc;

	if (!crtc->state)
		return;
	s = to_rockchip_crtc_state(crtc->state);

	if (!s)
		return;

	if (hdmi->is_hdmi_qp) {
		s->dsc_enable = 0;
		if (hdmi->link_cfg.dsc_mode)
			dw_hdmi_qp_dsc_configure(hdmi, s, crtc->state);

		phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);
	}

	clk_set_rate(hdmi->phyref_clk, adj->crtc_clock * 1000);
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
	.mode_set = dw_hdmi_rockchip_encoder_mode_set,
};

static void
dw_hdmi_rockchip_genphy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	while (hdmi->phy->power_count > 0)
		phy_power_off(hdmi->phy);
}

static int
dw_hdmi_rockchip_genphy_init(struct dw_hdmi *dw_hdmi, void *data,
			     const struct drm_display_info *display,
			     const struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_rockchip_genphy_disable(dw_hdmi, data);
	dw_hdmi_set_high_tmds_clock_ratio(dw_hdmi, display);
	return phy_power_on(hdmi->phy);
}

static void dw_hdmi_rk3228_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON6,
		HIWORD_UPDATE(RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL,
			      RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL));

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK,
			      RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK));
}

static enum drm_connector_status
dw_hdmi_rk3328_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V,
				      RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V));
	else
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(0, RK3328_HDMI_SDA_5V |
					 RK3328_HDMI_SCL_5V));
	return status;
}

static void dw_hdmi_rk3328_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	/* Enable and map pins to 3V grf-controlled io-voltage */
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON4,
		HIWORD_UPDATE(0, RK3328_HDMI_HPD_SARADC | RK3328_HDMI_CEC_5V |
				 RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V |
				 RK3328_HDMI_HPD_5V));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON3,
		HIWORD_UPDATE(0, RK3328_HDMI_SDA5V_GRF | RK3328_HDMI_SCL5V_GRF |
				 RK3328_HDMI_HPD5V_GRF |
				 RK3328_HDMI_CEC5V_GRF));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK,
			      RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK |
			      RK3328_HDMI_HPD_IOE));
}

static void dw_hdmi_qp_rockchip_phy_disable(struct dw_hdmi_qp *dw_hdmi,
					    void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	while (hdmi->phy->power_count > 0)
		phy_power_off(hdmi->phy);
}

static int dw_hdmi_qp_rockchip_genphy_init(struct dw_hdmi_qp *dw_hdmi, void *data,
					   struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_qp_rockchip_phy_disable(dw_hdmi, data);

	return phy_power_on(hdmi->phy);
}

static enum drm_connector_status
dw_hdmi_rk3588_read_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	u32 val;
	int ret;
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	regmap_read(hdmi->regmap, RK3588_GRF_SOC_STATUS1, &val);

	if (!hdmi->id) {
		if (val & RK3588_HDMI0_LEVEL_INT) {
			hdmi->hpd_stat = true;
			ret = connector_status_connected;
		} else {
			hdmi->hpd_stat = false;
			ret = connector_status_disconnected;
		}
	} else {
		if (val & RK3588_HDMI1_LEVEL_INT) {
			hdmi->hpd_stat = true;
			ret = connector_status_connected;
		} else {
			hdmi->hpd_stat = false;
			ret = connector_status_disconnected;
		}
	}

	return ret;
}

static void dw_hdmi_rk3588_setup_hpd(struct dw_hdmi_qp *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	u32 val;

	if (!hdmi->id) {
		val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_CLR,
				    RK3588_HDMI0_HPD_INT_CLR) |
		      HIWORD_UPDATE(0, RK3588_HDMI0_HPD_INT_MSK);
	} else {
		val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_CLR,
				    RK3588_HDMI1_HPD_INT_CLR) |
		      HIWORD_UPDATE(0, RK3588_HDMI1_HPD_INT_MSK);
	}

	regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);
}

static void dw_hdmi_rk3588_phy_set_mode(struct dw_hdmi_qp *dw_hdmi, void *data,
					u32 mode_mask, bool enable)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (!hdmi->phy)
		return;

	/* set phy earc/frl mode */
	if (enable)
		hdmi->phy_bus_width |= mode_mask;
	else
		hdmi->phy_bus_width &= ~mode_mask;

	phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);
}

static const struct dw_hdmi_phy_ops rk3228_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3228_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3228_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3228_chip_data,
	.phy_ops = &rk3228_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.max_tmdsclk = 371250,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3288_chip_data = {
	.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
	.lcdsel_big = HIWORD_UPDATE(0, RK3288_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3288_HDMI_LCDC_SEL, RK3288_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_rk3288w_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3288_chip_data,
	.tmds_n_table = rockchip_werid_tmds_n_table,
	.unsupported_yuv_input = true,
	.ycbcr_420_allowed = true,
};

static const struct dw_hdmi_phy_ops rk3328_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_rk3328_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3328_setup_hpd,
};

static enum drm_connector_status
dw_hdmi_rk3528_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	return dw_hdmi_phy_read_hpd(dw_hdmi, data);
}

static const struct dw_hdmi_phy_ops rk3528_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_rk3528_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3328_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3328_chip_data,
	.phy_ops = &rk3328_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
	.max_tmdsclk = 371250,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3368_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3368_chip_data,
	.unsupported_deep_color = true,
	.max_tmdsclk = 340000,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3399_chip_data = {
	.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
	.lcdsel_big = HIWORD_UPDATE(0, RK3399_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3399_HDMI_LCDC_SEL, RK3399_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3399_chip_data,
	.use_drm_infoframe = true,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3528_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3528_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3528_chip_data,
	.phy_ops = &rk3528_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3568_chip_data = {
	.lcdsel_grf_reg = -1,
	.ddc_en_reg = RK3568_GRF_VO_CON1,
};

static const struct dw_hdmi_plat_data rk3568_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3568_chip_data,
	.ycbcr_420_allowed = true,
	.use_drm_infoframe = true,
};

static const struct dw_hdmi_qp_phy_ops rk3588_hdmi_phy_ops = {
	.init		= dw_hdmi_qp_rockchip_genphy_init,
	.disable	= dw_hdmi_qp_rockchip_phy_disable,
	.read_hpd	= dw_hdmi_rk3588_read_hpd,
	.setup_hpd	= dw_hdmi_rk3588_setup_hpd,
	.set_mode       = dw_hdmi_rk3588_phy_set_mode,
};

struct rockchip_hdmi_chip_data rk3588_hdmi_chip_data = {
	.lcdsel_grf_reg = -1,
	.ddc_en_reg = RK3588_GRF_VO1_CON3,
	.split_mode = true,
};

static const struct dw_hdmi_plat_data rk3588_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_data = &rk3588_hdmi_chip_data,
	.qp_phy_ops = &rk3588_hdmi_phy_ops,
	.phy_name = "samsung_hdptx_phy",
	.phy_force_vendor = true,
	.ycbcr_420_allowed = true,
	.is_hdmi_qp = true,
	.use_drm_infoframe = true,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3228-dw-hdmi",
	  .data = &rk3228_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3328-dw-hdmi",
	  .data = &rk3328_hdmi_drv_data
	},
	{
	 .compatible = "rockchip,rk3368-dw-hdmi",
	 .data = &rk3368_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3399-dw-hdmi",
	  .data = &rk3399_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3528-dw-hdmi",
	  .data = &rk3528_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3568-dw-hdmi",
	  .data = &rk3568_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3588-dw-hdmi",
	  .data = &rk3588_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct rockchip_hdmi *hdmi;
	struct dw_hdmi_plat_data *plat_data;
	struct rockchip_hdmi *secondary;
	int ret;
	u32 val;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = platform_get_drvdata(pdev);
	if (!hdmi)
		return -ENOMEM;

	plat_data = hdmi->plat_data;
	hdmi->drm_dev = drm;

	plat_data->phy_data = hdmi;
	plat_data->get_input_bus_format =
		dw_hdmi_rockchip_get_input_bus_format;
	plat_data->get_output_bus_format =
		dw_hdmi_rockchip_get_output_bus_format;
	plat_data->get_enc_in_encoding =
		dw_hdmi_rockchip_get_enc_in_encoding;
	plat_data->get_enc_out_encoding =
		dw_hdmi_rockchip_get_enc_out_encoding;
	plat_data->get_quant_range =
		dw_hdmi_rockchip_get_quant_range;
	plat_data->get_hdr_property =
		dw_hdmi_rockchip_get_hdr_property;
	plat_data->get_hdr_blob =
		dw_hdmi_rockchip_get_hdr_blob;
	plat_data->get_color_changed =
		dw_hdmi_rockchip_get_color_changed;
	plat_data->get_yuv422_format =
		dw_hdmi_rockchip_get_yuv422_format;
	plat_data->get_edid_dsc_info =
		dw_hdmi_rockchip_get_edid_dsc_info;
	plat_data->get_next_hdr_data =
		dw_hdmi_rockchip_get_next_hdr_data;
	plat_data->get_colorimetry =
		dw_hdmi_rockchip_get_colorimetry;
	plat_data->get_link_cfg = dw_hdmi_rockchip_get_link_cfg;
	plat_data->set_hdcp2_enable = rk3588_set_hdcp2_enable;
	plat_data->set_hdcp_status = rk3588_set_hdcp_status;
	plat_data->set_grf_cfg = rk3588_set_grf_cfg;
	plat_data->get_grf_color_fmt = rk3588_get_grf_color_fmt;
	plat_data->convert_to_split_mode = drm_mode_convert_to_split_mode;
	plat_data->convert_to_origin_mode = drm_mode_convert_to_origin_mode;
	plat_data->dclk_set = dw_hdmi_dclk_set;
	plat_data->link_clk_set = dw_hdmi_link_clk_set;
	plat_data->get_vp_id = dw_hdmi_rockchip_get_vp_id;
	plat_data->update_color_format =
		dw_hdmi_rockchip_update_color_format;
	plat_data->check_hdr_color_change =
		dw_hdmi_rockchip_check_hdr_color_change;
	plat_data->set_prev_bus_format =
		dw_hdmi_rockchip_set_prev_bus_format;
	plat_data->set_ddc_io =
		dw_hdmi_rockchip_set_ddc_io;
	plat_data->set_hdcp14_mem =
		dw_hdmi_rockchip_set_hdcp14_mem;
	plat_data->property_ops = &dw_hdmi_rockchip_property_ops;

	secondary = rockchip_hdmi_find_by_id(dev->driver, !hdmi->id);
	/* If don't enable hdmi0 and hdmi1, we don't enable split mode */
	if (hdmi->chip_data->split_mode && secondary) {

		/*
		 * hdmi can only attach bridge and init encoder/connector in the
		 * last bind hdmi in split mode, or hdmi->hdmi_qp will not be initialized
		 * and plat_data->left/right will be null pointer. we must check if split
		 * mode is on and determine the sequence of hdmi bind.
		 */
		if (device_property_read_bool(dev, "split-mode") ||
		    device_property_read_bool(secondary->dev, "split-mode")) {
			plat_data->split_mode = true;
			secondary->plat_data->split_mode = true;
			if (!secondary->plat_data->first_screen)
				plat_data->first_screen = true;
		}
	}

	if (!plat_data->first_screen) {
		encoder = &hdmi->encoder;
		encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm, dev->of_node);
		/*
		 * If we failed to find the CRTC(s) which this encoder is
		 * supposed to be connected to, it's because the CRTC has
		 * not been registered yet.  Defer probing, and hope that
		 * the required CRTC is added later.
		 */
		if (encoder->possible_crtcs == 0)
			return -EPROBE_DEFER;

		drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);
		drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);
	}

	if (!plat_data->max_tmdsclk)
		hdmi->max_tmdsclk = 594000;
	else
		hdmi->max_tmdsclk = plat_data->max_tmdsclk;

	hdmi->is_hdmi_qp = plat_data->is_hdmi_qp;

	hdmi->unsupported_yuv_input = plat_data->unsupported_yuv_input;
	hdmi->unsupported_deep_color = plat_data->unsupported_deep_color;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to parse OF data\n");
		return ret;
	}

	ret = clk_prepare_enable(hdmi->aud_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI aud_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hpd_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hpd_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vo1);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hclk_vo1: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->earc_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI earc_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hdmitx_ref);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hdmitx_ref: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->pclk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI pclk: %d\n", ret);
		return ret;
	}

	if (hdmi->chip_data->ddc_en_reg == RK3568_GRF_VO_CON1) {
		regmap_write(hdmi->regmap, RK3568_GRF_VO_CON1,
			     HIWORD_UPDATE(RK3568_HDMI_SDAIN_MSK |
					   RK3568_HDMI_SCLIN_MSK,
					   RK3568_HDMI_SDAIN_MSK |
					   RK3568_HDMI_SCLIN_MSK));
	}

	if (hdmi->is_hdmi_qp) {
		if (!hdmi->id) {
			val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
			      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
			      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
			      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);

			val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
					    RK3588_SET_HPD_PATH_MASK);
			regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

			val = HIWORD_UPDATE(RK3588_HDMI0_GRANT_SEL,
					    RK3588_HDMI0_GRANT_SEL);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON9, val);
		} else {
			val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
			      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
			      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
			      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);

			val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
					    RK3588_SET_HPD_PATH_MASK);
			regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

			val = HIWORD_UPDATE(RK3588_HDMI1_GRANT_SEL,
					    RK3588_HDMI1_GRANT_SEL);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON9, val);
		}
		init_hpd_work(hdmi);
	}

	ret = clk_prepare_enable(hdmi->phyref_clk);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Failed to enable HDMI vpll: %d\n",
			      ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vio);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hclk_vio: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vop);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hclk_vop: %d\n",
			ret);
		return ret;
	}

	if (hdmi->is_hdmi_qp) {
		if (!hdmi->id)
			val = HIWORD_UPDATE(RK3588_HDMI0_HPD_INT_MSK, RK3588_HDMI0_HPD_INT_MSK);
		else
			val = HIWORD_UPDATE(RK3588_HDMI1_HPD_INT_MSK, RK3588_HDMI1_HPD_INT_MSK);
		regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON2, val);

		hdmi->hpd_irq = platform_get_irq(pdev, 4);
		if (hdmi->hpd_irq < 0)
			return hdmi->hpd_irq;

		ret = devm_request_threaded_irq(hdmi->dev, hdmi->hpd_irq,
						rockchip_hdmi_hardirq,
						rockchip_hdmi_irq,
						IRQF_SHARED, "dw-hdmi-qp-hpd",
						hdmi);
		if (ret)
			return ret;
	}

	hdmi->phy = devm_phy_optional_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		hdmi->phy = devm_phy_optional_get(dev, "hdmi_phy");
		if (IS_ERR(hdmi->phy)) {
			ret = PTR_ERR(hdmi->phy);
			if (ret != -EPROBE_DEFER)
				DRM_DEV_ERROR(hdmi->dev, "failed to get phy\n");
			return ret;
		}
	}

	if (hdmi->is_hdmi_qp) {
		hdmi->hdmi_qp = dw_hdmi_qp_bind(pdev, &hdmi->encoder, plat_data);

		if (IS_ERR(hdmi->hdmi_qp)) {
			ret = PTR_ERR(hdmi->hdmi_qp);
			drm_encoder_cleanup(&hdmi->encoder);
		}

		if (plat_data->bridge) {
			struct drm_connector *connector = NULL;
			struct list_head *connector_list =
				&plat_data->bridge->dev->mode_config.connector_list;

			list_for_each_entry(connector, connector_list, head)
				if (drm_connector_has_possible_encoder(connector,
							&hdmi->encoder))
					break;

			hdmi->sub_dev.connector = connector;
			hdmi->sub_dev.of_node = dev->of_node;
			rockchip_drm_register_sub_dev(&hdmi->sub_dev);
		} else if (plat_data->connector) {
			hdmi->sub_dev.connector = plat_data->connector;
			hdmi->sub_dev.loader_protect = dw_hdmi_rockchip_encoder_loader_protect;
			if (secondary && device_property_read_bool(secondary->dev, "split-mode"))
				hdmi->sub_dev.of_node = secondary->dev->of_node;
			else
				hdmi->sub_dev.of_node = hdmi->dev->of_node;

			rockchip_drm_register_sub_dev(&hdmi->sub_dev);
		}

		if (plat_data->split_mode && secondary) {
			if (device_property_read_bool(dev, "split-mode")) {
				plat_data->right = secondary->hdmi_qp;
				secondary->plat_data->left = hdmi->hdmi_qp;
			} else {
				plat_data->left = secondary->hdmi_qp;
				secondary->plat_data->right = hdmi->hdmi_qp;
			}
		}

		return ret;
	}

	hdmi->hdmi = dw_hdmi_bind(pdev, &hdmi->encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		drm_encoder_cleanup(&hdmi->encoder);
		clk_disable_unprepare(hdmi->aud_clk);
		clk_disable_unprepare(hdmi->phyref_clk);
		clk_disable_unprepare(hdmi->hclk_vop);
		clk_disable_unprepare(hdmi->hpd_clk);
		clk_disable_unprepare(hdmi->hclk_vo1);
		clk_disable_unprepare(hdmi->earc_clk);
		clk_disable_unprepare(hdmi->hdmitx_ref);
		clk_disable_unprepare(hdmi->pclk);
	}

	if (plat_data->connector) {
		hdmi->sub_dev.connector = plat_data->connector;
		hdmi->sub_dev.of_node = dev->of_node;
		rockchip_drm_register_sub_dev(&hdmi->sub_dev);
	}

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi->is_hdmi_qp) {
		cancel_delayed_work(&hdmi->work);
		flush_workqueue(hdmi->workqueue);
		destroy_workqueue(hdmi->workqueue);
	}

	if (hdmi->sub_dev.connector)
		rockchip_drm_unregister_sub_dev(&hdmi->sub_dev);

	if (hdmi->is_hdmi_qp)
		dw_hdmi_qp_unbind(hdmi->hdmi_qp);
	else
		dw_hdmi_unbind(hdmi->hdmi);
	clk_disable_unprepare(hdmi->aud_clk);
	clk_disable_unprepare(hdmi->phyref_clk);
	clk_disable_unprepare(hdmi->hclk_vop);
	clk_disable_unprepare(hdmi->hpd_clk);
	clk_disable_unprepare(hdmi->hclk_vo1);
	clk_disable_unprepare(hdmi->earc_clk);
	clk_disable_unprepare(hdmi->hdmitx_ref);
	clk_disable_unprepare(hdmi->pclk);
}

static const struct component_ops dw_hdmi_rockchip_ops = {
	.bind	= dw_hdmi_rockchip_bind,
	.unbind	= dw_hdmi_rockchip_unbind,
};

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	struct rockchip_hdmi *hdmi;
	const struct of_device_id *match;
	struct dw_hdmi_plat_data *plat_data;
	int id;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	id = of_alias_get_id(pdev->dev.of_node, "hdmi");
	if (id < 0)
		id = 0;

	hdmi->id = id;
	hdmi->dev = &pdev->dev;

	match = of_match_node(dw_hdmi_rockchip_dt_ids, pdev->dev.of_node);
	plat_data = devm_kmemdup(&pdev->dev, match->data,
				 sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	plat_data->id = hdmi->id;
	hdmi->plat_data = plat_data;
	hdmi->chip_data = plat_data->phy_data;

	platform_set_drvdata(pdev, hdmi);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static void dw_hdmi_rockchip_shutdown(struct platform_device *pdev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(&pdev->dev);

	if (!hdmi)
		return;

	if (hdmi->is_hdmi_qp) {
		if (hdmi->hpd_irq)
			disable_irq(hdmi->hpd_irq);
		cancel_delayed_work(&hdmi->work);
		flush_workqueue(hdmi->workqueue);
		dw_hdmi_qp_suspend(hdmi->dev, hdmi->hdmi_qp);
	} else {
		if (hdmi->hpd_gpiod) {
			disable_irq(hdmi->hpd_irq);
			if (hdmi->hpd_wake_en)
				disable_irq_wake(hdmi->hpd_irq);
		}
		dw_hdmi_suspend(hdmi->hdmi);
	}
	pm_runtime_put_sync(&pdev->dev);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int dw_hdmi_rockchip_suspend(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi->is_hdmi_qp) {
		if (hdmi->hpd_irq)
			disable_irq(hdmi->hpd_irq);
		dw_hdmi_qp_suspend(dev, hdmi->hdmi_qp);
	} else {
		if (hdmi->hpd_gpiod)
			disable_irq(hdmi->hpd_irq);
		dw_hdmi_suspend(hdmi->hdmi);
	}
	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_hdmi_rockchip_resume(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);
	u32 val;

	if (hdmi->is_hdmi_qp) {
		if (!hdmi->id) {
			val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
			      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
			      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
			      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON3, val);

			val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
					    RK3588_SET_HPD_PATH_MASK);
			regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

			val = HIWORD_UPDATE(RK3588_HDMI0_GRANT_SEL,
					    RK3588_HDMI0_GRANT_SEL);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON9, val);
		} else {
			val = HIWORD_UPDATE(RK3588_SCLIN_MASK, RK3588_SCLIN_MASK) |
			      HIWORD_UPDATE(RK3588_SDAIN_MASK, RK3588_SDAIN_MASK) |
			      HIWORD_UPDATE(RK3588_MODE_MASK, RK3588_MODE_MASK) |
			      HIWORD_UPDATE(RK3588_I2S_SEL_MASK, RK3588_I2S_SEL_MASK);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON6, val);

			val = HIWORD_UPDATE(RK3588_SET_HPD_PATH_MASK,
					    RK3588_SET_HPD_PATH_MASK);
			regmap_write(hdmi->regmap, RK3588_GRF_SOC_CON7, val);

			val = HIWORD_UPDATE(RK3588_HDMI1_GRANT_SEL,
					    RK3588_HDMI1_GRANT_SEL);
			regmap_write(hdmi->vo1_regmap, RK3588_GRF_VO1_CON9, val);
		}

		dw_hdmi_qp_resume(dev, hdmi->hdmi_qp);
		if (hdmi->hpd_irq)
			enable_irq(hdmi->hpd_irq);
		drm_helper_hpd_irq_event(hdmi->drm_dev);
	} else {
		if (hdmi->hpd_gpiod) {
			dw_hdmi_rk3528_gpio_hpd_init(hdmi);
			enable_irq(hdmi->hpd_irq);
		}
		dw_hdmi_resume(hdmi->hdmi);
	}
	pm_runtime_get_sync(dev);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_rockchip_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_rockchip_suspend,
				dw_hdmi_rockchip_resume)
};

struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.shutdown = dw_hdmi_rockchip_shutdown,
	.driver = {
		.name = "dwhdmi-rockchip",
		.pm = &dw_hdmi_rockchip_pm,
		.of_match_table = dw_hdmi_rockchip_dt_ids,
	},
};
