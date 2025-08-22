// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare Cores DisplayPort Transmitter Controller
 *
 * Copyright (c) 2025 Rockchip Electronics Co., Ltd.
 *
 * Author: Andy Yan <andy.yan@rock-chips.com>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/media-bus-format.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/unaligned.h>

#include <drm/bridge/dw_dp.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define DW_DP_VERSION_NUMBER			0x0000
#define DW_DP_VERSION_TYPE			0x0004
#define DW_DP_ID				0x0008

#define DW_DP_CONFIG_REG1			0x0100
#define DW_DP_CONFIG_REG2			0x0104
#define DW_DP_CONFIG_REG3			0x0108

#define DW_DP_CCTL				0x0200
#define FORCE_HPD				BIT(4)
#define DEFAULT_FAST_LINK_TRAIN_EN		BIT(2)
#define ENHANCE_FRAMING_EN			BIT(1)
#define SCRAMBLE_DIS				BIT(0)
#define DW_DP_SOFT_RESET_CTRL			0x0204
#define VIDEO_RESET				BIT(5)
#define AUX_RESET				BIT(4)
#define AUDIO_SAMPLER_RESET			BIT(3)
#define HDCP_MODULE_RESET			BIT(2)
#define PHY_SOFT_RESET				BIT(1)
#define CONTROLLER_RESET			BIT(0)

#define DW_DP_VSAMPLE_CTRL			0x0300
#define PIXEL_MODE_SELECT			GENMASK(22, 21)
#define VIDEO_MAPPING				GENMASK(20, 16)
#define VIDEO_STREAM_ENABLE			BIT(5)

#define DW_DP_VSAMPLE_STUFF_CTRL1		0x0304

#define DW_DP_VSAMPLE_STUFF_CTRL2		0x0308

#define DW_DP_VINPUT_POLARITY_CTRL		0x030c
#define DE_IN_POLARITY				BIT(2)
#define HSYNC_IN_POLARITY			BIT(1)
#define VSYNC_IN_POLARITY			BIT(0)

#define DW_DP_VIDEO_CONFIG1			0x0310
#define HACTIVE					GENMASK(31, 16)
#define HBLANK					GENMASK(15, 2)
#define I_P					BIT(1)
#define R_V_BLANK_IN_OSC			BIT(0)

#define DW_DP_VIDEO_CONFIG2			0x0314
#define VBLANK					GENMASK(31, 16)
#define VACTIVE					GENMASK(15, 0)

#define DW_DP_VIDEO_CONFIG3			0x0318
#define H_SYNC_WIDTH				GENMASK(31, 16)
#define H_FRONT_PORCH				GENMASK(15, 0)

#define DW_DP_VIDEO_CONFIG4			0x031c
#define V_SYNC_WIDTH				GENMASK(31, 16)
#define V_FRONT_PORCH				GENMASK(15, 0)

#define DW_DP_VIDEO_CONFIG5			0x0320
#define INIT_THRESHOLD_HI			GENMASK(22, 21)
#define AVERAGE_BYTES_PER_TU_FRAC		GENMASK(19, 16)
#define INIT_THRESHOLD				GENMASK(13, 7)
#define AVERAGE_BYTES_PER_TU			GENMASK(6, 0)

#define DW_DP_VIDEO_MSA1			0x0324
#define VSTART					GENMASK(31, 16)
#define HSTART					GENMASK(15, 0)

#define DW_DP_VIDEO_MSA2			0x0328
#define MISC0					GENMASK(31, 24)

#define DW_DP_VIDEO_MSA3			0x032c
#define MISC1					GENMASK(31, 24)

#define DW_DP_VIDEO_HBLANK_INTERVAL		0x0330
#define HBLANK_INTERVAL_EN			BIT(16)
#define HBLANK_INTERVAL				GENMASK(15, 0)

#define DW_DP_AUD_CONFIG1			0x0400
#define AUDIO_TIMESTAMP_VERSION_NUM		GENMASK(29, 24)
#define AUDIO_PACKET_ID				GENMASK(23, 16)
#define AUDIO_MUTE				BIT(15)
#define NUM_CHANNELS				GENMASK(14, 12)
#define HBR_MODE_ENABLE				BIT(10)
#define AUDIO_DATA_WIDTH			GENMASK(9, 5)
#define AUDIO_DATA_IN_EN			GENMASK(4, 1)
#define AUDIO_INF_SELECT			BIT(0)

#define DW_DP_SDP_VERTICAL_CTRL			0x0500
#define EN_VERTICAL_SDP				BIT(2)
#define EN_AUDIO_STREAM_SDP			BIT(1)
#define EN_AUDIO_TIMESTAMP_SDP			BIT(0)
#define DW_DP_SDP_HORIZONTAL_CTRL		0x0504
#define EN_HORIZONTAL_SDP			BIT(2)
#define DW_DP_SDP_STATUS_REGISTER		0x0508
#define DW_DP_SDP_MANUAL_CTRL			0x050c
#define DW_DP_SDP_STATUS_EN			0x0510

#define DW_DP_SDP_REGISTER_BANK			0x0600
#define SDP_REGS				GENMASK(31, 0)

#define DW_DP_PHYIF_CTRL			0x0a00
#define PHY_WIDTH				BIT(25)
#define PHY_POWERDOWN				GENMASK(20, 17)
#define PHY_BUSY				GENMASK(15, 12)
#define SSC_DIS					BIT(16)
#define XMIT_ENABLE				GENMASK(11, 8)
#define PHY_LANES				GENMASK(7, 6)
#define PHY_RATE				GENMASK(5, 4)
#define TPS_SEL					GENMASK(3, 0)

#define DW_DP_PHY_TX_EQ				0x0a04
#define DW_DP_CUSTOMPAT0			0x0a08
#define DW_DP_CUSTOMPAT1			0x0a0c
#define DW_DP_CUSTOMPAT2			0x0a10
#define DW_DP_HBR2_COMPLIANCE_SCRAMBLER_RESET	0x0a14
#define DW_DP_PHYIF_PWRDOWN_CTRL		0x0a18

#define DW_DP_AUX_CMD				0x0b00
#define AUX_CMD_TYPE				GENMASK(31, 28)
#define AUX_ADDR				GENMASK(27, 8)
#define I2C_ADDR_ONLY				BIT(4)
#define AUX_LEN_REQ				GENMASK(3, 0)

#define DW_DP_AUX_STATUS			0x0b04
#define AUX_TIMEOUT				BIT(17)
#define AUX_BYTES_READ				GENMASK(23, 19)
#define AUX_STATUS				GENMASK(7, 4)

#define DW_DP_AUX_DATA0				0x0b08
#define DW_DP_AUX_DATA1				0x0b0c
#define DW_DP_AUX_DATA2				0x0b10
#define DW_DP_AUX_DATA3				0x0b14

#define DW_DP_GENERAL_INTERRUPT			0x0d00
#define VIDEO_FIFO_OVERFLOW_STREAM0		BIT(6)
#define AUDIO_FIFO_OVERFLOW_STREAM0		BIT(5)
#define SDP_EVENT_STREAM0			BIT(4)
#define AUX_CMD_INVALID				BIT(3)
#define HDCP_EVENT				BIT(2)
#define AUX_REPLY_EVENT				BIT(1)
#define HPD_EVENT				BIT(0)

#define DW_DP_GENERAL_INTERRUPT_ENABLE		0x0d04
#define HDCP_EVENT_EN				BIT(2)
#define AUX_REPLY_EVENT_EN			BIT(1)
#define HPD_EVENT_EN				BIT(0)

#define DW_DP_HPD_STATUS			0x0d08
#define HPD_STATE				GENMASK(11, 9)
#define HPD_STATUS				BIT(8)
#define HPD_HOT_UNPLUG				BIT(2)
#define HPD_HOT_PLUG				BIT(1)
#define HPD_IRQ					BIT(0)

#define DW_DP_HPD_INTERRUPT_ENABLE		0x0d0c
#define HPD_UNPLUG_ERR_EN			BIT(3)
#define HPD_UNPLUG_EN				BIT(2)
#define HPD_PLUG_EN				BIT(1)
#define HPD_IRQ_EN				BIT(0)

#define DW_DP_HDCP_CFG				0x0e00
#define DPCD12PLUS				BIT(7)
#define CP_IRQ					BIT(6)
#define BYPENCRYPTION				BIT(5)
#define HDCP_LOCK				BIT(4)
#define ENCRYPTIONDISABLE			BIT(3)
#define ENABLE_HDCP_13				BIT(2)
#define ENABLE_HDCP				BIT(1)

#define DW_DP_HDCP_OBS				0x0e04
#define HDCP22_RE_AUTHENTICATION_REQ		BIT(31)
#define HDCP22_AUTHENTICATION_FAILED		BIT(30)
#define HDCP22_AUTHENTICATION_SUCCESS		BIT(29)
#define HDCP22_CAPABLE_SINK			BIT(28)
#define HDCP22_SINK_CAP_CHECK_COMPLETE		BIT(27)
#define HDCP22_STATE				GENMASK(26, 24)
#define HDCP22_BOOTED				BIT(23)
#define HDCP13_BSTATUS				GENMASK(22, 19)
#define REPEATER				BIT(18)
#define HDCP_CAPABLE				BIT(17)
#define STATEE					GENMASK(16, 14)
#define STATEOEG				GENMASK(13, 11)
#define STATER					GENMASK(10, 8)
#define STATEA					GENMASK(7, 4)
#define SUBSTATEA				GENMASK(3, 1)
#define HDCPENGAGED				BIT(0)

#define DW_DP_HDCP_APIINTCLR			0x0e08
#define DW_DP_HDCP_APIINTSTAT			0x0e0c
#define DW_DP_HDCP_APIINTMSK			0x0e10
#define HDCP22_GPIOINT				BIT(8)
#define HDCP_ENGAGED				BIT(7)
#define HDCP_FAILED				BIT(6)
#define KSVSHA1CALCDONEINT			BIT(5)
#define AUXRESPNACK7TIMES			BIT(4)
#define AUXRESPTIMEOUT				BIT(3)
#define AUXRESPDEFER7TIMES			BIT(2)
#define KSVACCESSINT				BIT(0)

#define DW_DP_HDCP_KSVMEMCTRL			0x0e18
#define KSVSHA1STATUS				BIT(4)
#define KSVMEMACCESS				BIT(1)
#define KSVMEMREQUEST				BIT(0)

#define DW_DP_HDCP_REG_BKSV0			0x3600
#define DW_DP_HDCP_REG_BKSV1			0x3604
#define DW_DP_HDCP_REG_ANCONF			0x3608
#define AN_BYPASS				BIT(0)

#define DW_DP_HDCP_REG_AN0			0x360c
#define DW_DP_HDCP_REG_AN1			0x3610
#define DW_DP_HDCP_REG_RMLCTL			0x3614
#define ODPK_DECRYPT_ENABLE			BIT(0)

#define DW_DP_HDCP_REG_RMLSTS			0x3618
#define IDPK_WR_OK_STS				BIT(6)
#define	IDPK_DATA_INDEX				GENMASK(5, 0)
#define DW_DP_HDCP_REG_SEED			0x361c
#define DW_DP_HDCP_REG_DPK0			0x3620
#define DW_DP_HDCP_REG_DPK1			0x3624
#define DW_DP_HDCP22_GPIOSTS			0x3628
#define DW_DP_HDCP22_GPIOCHNGSTS		0x362c
#define DW_DP_HDCP_REG_DPK_CRC			0x3630

#define DW_DP_MAX_REGISTER			DW_DP_HDCP_REG_DPK_CRC

#define SDP_REG_BANK_SIZE			16

struct dw_dp_link_caps {
	bool enhanced_framing;
	bool tps3_supported;
	bool tps4_supported;
	bool fast_training;
	bool channel_coding;
	bool ssc;
};

struct dw_dp_link_train_set {
	unsigned int voltage_swing[4];
	unsigned int pre_emphasis[4];
	bool voltage_max_reached[4];
	bool pre_max_reached[4];
};

struct dw_dp_link_train {
	struct dw_dp_link_train_set adjust;
	bool clock_recovered;
	bool channel_equalized;
};

struct dw_dp_link {
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	unsigned char revision;
	unsigned int rate;
	unsigned int lanes;
	u8 sink_count;
	u8 vsc_sdp_supported;
	struct dw_dp_link_caps caps;
	struct dw_dp_link_train train;
	struct drm_dp_desc desc;
};

struct dw_dp_bridge_state {
	struct drm_bridge_state base;
	struct drm_display_mode mode;
	u8 video_mapping;
	u8 color_format;
	u8 bpc;
	u8 bpp;
};

struct dw_dp_sdp {
	struct dp_sdp base;
	unsigned long flags;
};

struct dw_dp_hotplug {
	bool long_hpd;
};

struct dw_dp {
	struct drm_bridge bridge;
	struct device *dev;
	struct regmap *regmap;
	struct phy *phy;
	struct clk *apb_clk;
	struct clk *aux_clk;
	struct clk *i2s_clk;
	struct clk *spdif_clk;
	struct clk *hdcp_clk;
	struct reset_control *rstc;
	struct completion complete;
	int irq;
	struct work_struct hpd_work;
	struct dw_dp_hotplug hotplug;
	/* Serialize hpd status access */
	struct mutex irq_lock;

	struct drm_dp_aux aux;

	struct dw_dp_link link;
	struct dw_dp_plat_data plat_data;
	u8 pixel_mode;

	DECLARE_BITMAP(sdp_reg_bank, SDP_REG_BANK_SIZE);
};

enum {
	DW_DP_RGB_6BIT,
	DW_DP_RGB_8BIT,
	DW_DP_RGB_10BIT,
	DW_DP_RGB_12BIT,
	DW_DP_RGB_16BIT,
	DW_DP_YCBCR444_8BIT,
	DW_DP_YCBCR444_10BIT,
	DW_DP_YCBCR444_12BIT,
	DW_DP_YCBCR444_16BIT,
	DW_DP_YCBCR422_8BIT,
	DW_DP_YCBCR422_10BIT,
	DW_DP_YCBCR422_12BIT,
	DW_DP_YCBCR422_16BIT,
	DW_DP_YCBCR420_8BIT,
	DW_DP_YCBCR420_10BIT,
	DW_DP_YCBCR420_12BIT,
	DW_DP_YCBCR420_16BIT,
};

enum {
	DW_DP_MP_SINGLE_PIXEL,
	DW_DP_MP_DUAL_PIXEL,
	DW_DP_MP_QUAD_PIXEL,
};

enum {
	DW_DP_SDP_VERTICAL_INTERVAL = BIT(0),
	DW_DP_SDP_HORIZONTAL_INTERVAL = BIT(1),
};

enum {
	DW_DP_HPD_STATE_IDLE,
	DW_DP_HPD_STATE_UNPLUG,
	DP_DP_HPD_STATE_TIMEOUT = 4,
	DW_DP_HPD_STATE_PLUG = 7
};

enum {
	DW_DP_PHY_PATTERN_NONE,
	DW_DP_PHY_PATTERN_TPS_1,
	DW_DP_PHY_PATTERN_TPS_2,
	DW_DP_PHY_PATTERN_TPS_3,
	DW_DP_PHY_PATTERN_TPS_4,
	DW_DP_PHY_PATTERN_SERM,
	DW_DP_PHY_PATTERN_PBRS7,
	DW_DP_PHY_PATTERN_CUSTOM_80BIT,
	DW_DP_PHY_PATTERN_CP2520_1,
	DW_DP_PHY_PATTERN_CP2520_2,
};

struct dw_dp_output_format {
	u32 bus_format;
	u32 color_format;
	u8 video_mapping;
	u8 bpc;
	u8 bpp;
};

#define to_dw_dp_bridge_state(s) container_of(s, struct dw_dp_bridge_state, base)

static const struct dw_dp_output_format dw_dp_output_formats[] = {
	{ MEDIA_BUS_FMT_RGB101010_1X30, DRM_COLOR_FORMAT_RGB444, DW_DP_RGB_10BIT, 10, 30 },
	{ MEDIA_BUS_FMT_RGB888_1X24, DRM_COLOR_FORMAT_RGB444, DW_DP_RGB_8BIT, 8, 24 },
	{ MEDIA_BUS_FMT_YUV10_1X30, DRM_COLOR_FORMAT_YCBCR444, DW_DP_YCBCR444_10BIT, 10, 30 },
	{ MEDIA_BUS_FMT_YUV8_1X24, DRM_COLOR_FORMAT_YCBCR444, DW_DP_YCBCR444_8BIT, 8, 24},
	{ MEDIA_BUS_FMT_YUYV10_1X20, DRM_COLOR_FORMAT_YCBCR422, DW_DP_YCBCR422_10BIT, 10, 20 },
	{ MEDIA_BUS_FMT_YUYV8_1X16, DRM_COLOR_FORMAT_YCBCR422, DW_DP_YCBCR422_8BIT, 8, 16 },
	{ MEDIA_BUS_FMT_UYYVYY10_0_5X30, DRM_COLOR_FORMAT_YCBCR420, DW_DP_YCBCR420_10BIT, 10, 15 },
	{ MEDIA_BUS_FMT_UYYVYY8_0_5X24, DRM_COLOR_FORMAT_YCBCR420, DW_DP_YCBCR420_8BIT, 8, 12 },
	{ MEDIA_BUS_FMT_RGB666_1X24_CPADHI, DRM_COLOR_FORMAT_RGB444, DW_DP_RGB_6BIT, 6, 18 },
};

static const struct dw_dp_output_format *dw_dp_get_output_format(u32 bus_format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dw_dp_output_formats); i++)
		if (dw_dp_output_formats[i].bus_format == bus_format)
			return &dw_dp_output_formats[i];

	return NULL;
}

static inline struct dw_dp *bridge_to_dp(struct drm_bridge *b)
{
	return container_of(b, struct dw_dp, bridge);
}

static struct dw_dp_bridge_state *dw_dp_get_bridge_state(struct dw_dp *dp)
{
	struct dw_dp_bridge_state *dw_bridge_state;
	struct drm_bridge_state *state;

	state = drm_priv_to_bridge_state(dp->bridge.base.state);
	if (!state)
		return  NULL;

	dw_bridge_state = to_dw_dp_bridge_state(state);
	if (!dw_bridge_state)
		return NULL;

	return dw_bridge_state;
}

static inline void dw_dp_phy_set_pattern(struct dw_dp *dp, u32 pattern)
{
	regmap_update_bits(dp->regmap, DW_DP_PHYIF_CTRL, TPS_SEL,
			   FIELD_PREP(TPS_SEL, pattern));
}

static void dw_dp_phy_xmit_enable(struct dw_dp *dp, u32 lanes)
{
	u32 xmit_enable;

	switch (lanes) {
	case 4:
	case 2:
	case 1:
		xmit_enable = GENMASK(lanes - 1, 0);
		break;
	case 0:
	default:
		xmit_enable = 0;
		break;
	}

	regmap_update_bits(dp->regmap, DW_DP_PHYIF_CTRL, XMIT_ENABLE,
			   FIELD_PREP(XMIT_ENABLE, xmit_enable));
}

static bool dw_dp_bandwidth_ok(struct dw_dp *dp,
			       const struct drm_display_mode *mode, u32 bpp,
			       unsigned int lanes, unsigned int rate)
{
	u32 max_bw, req_bw;

	req_bw = mode->clock * bpp / 8;
	max_bw = lanes * rate;
	if (req_bw > max_bw)
		return false;

	return true;
}

static bool dw_dp_hpd_detect(struct dw_dp *dp)
{
	u32 value;

	regmap_read(dp->regmap, DW_DP_HPD_STATUS, &value);

	return FIELD_GET(HPD_STATE, value) == DW_DP_HPD_STATE_PLUG;
}

static void dw_dp_link_caps_reset(struct dw_dp_link_caps *caps)
{
	caps->enhanced_framing = false;
	caps->tps3_supported = false;
	caps->tps4_supported = false;
	caps->fast_training = false;
	caps->channel_coding = false;
}

static void dw_dp_link_reset(struct dw_dp_link *link)
{
	link->vsc_sdp_supported = 0;
	link->sink_count = 0;
	link->revision = 0;
	link->rate = 0;
	link->lanes = 0;

	dw_dp_link_caps_reset(&link->caps);
	memset(link->dpcd, 0, sizeof(link->dpcd));
}

static int dw_dp_link_parse(struct dw_dp *dp, struct drm_connector *connector)
{
	struct dw_dp_link *link = &dp->link;
	int ret;

	dw_dp_link_reset(link);

	ret = drm_dp_read_dpcd_caps(&dp->aux, link->dpcd);
	if (ret < 0)
		return ret;

	drm_dp_read_desc(&dp->aux, &link->desc, drm_dp_is_branch(link->dpcd));

	if (drm_dp_read_sink_count_cap(connector, link->dpcd, &link->desc)) {
		ret = drm_dp_read_sink_count(&dp->aux);
		if (ret < 0)
			return ret;

		link->sink_count = ret;

		/* Dongle connected, but no display */
		if (!link->sink_count)
			return -ENODEV;
	}

	link->vsc_sdp_supported = drm_dp_vsc_sdp_supported(&dp->aux, link->dpcd);

	link->revision = link->dpcd[DP_DPCD_REV];
	link->rate = min_t(u32, min(dp->plat_data.max_link_rate,
				    dp->phy->attrs.max_link_rate * 100),
			   drm_dp_max_link_rate(link->dpcd));
	link->lanes = min_t(u8, phy_get_bus_width(dp->phy),
			    drm_dp_max_lane_count(link->dpcd));

	link->caps.enhanced_framing = drm_dp_enhanced_frame_cap(link->dpcd);
	link->caps.tps3_supported = drm_dp_tps3_supported(link->dpcd);
	link->caps.tps4_supported = drm_dp_tps4_supported(link->dpcd);
	link->caps.fast_training = drm_dp_fast_training_cap(link->dpcd);
	link->caps.channel_coding = drm_dp_channel_coding_supported(link->dpcd);
	link->caps.ssc = !!(link->dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5);

	return 0;
}

static int dw_dp_link_train_update_vs_emph(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct dw_dp_link_train_set *train_set = &link->train.adjust;
	unsigned int lanes = dp->link.lanes;
	union phy_configure_opts phy_cfg;
	unsigned int *vs, *pe;
	int i, ret;
	u8 buf[4];

	vs = train_set->voltage_swing;
	pe = train_set->pre_emphasis;

	for (i = 0; i < lanes; i++) {
		phy_cfg.dp.voltage[i] = vs[i];
		phy_cfg.dp.pre[i] = pe[i];
	}

	phy_cfg.dp.set_lanes = false;
	phy_cfg.dp.set_rate = false;
	phy_cfg.dp.set_voltages = true;

	ret = phy_configure(dp->phy, &phy_cfg);
	if (ret)
		return ret;

	for (i = 0; i < lanes; i++) {
		buf[i] = (vs[i] << DP_TRAIN_VOLTAGE_SWING_SHIFT) |
			 (pe[i] << DP_TRAIN_PRE_EMPHASIS_SHIFT);
		if (train_set->voltage_max_reached[i])
			buf[i] |= DP_TRAIN_MAX_SWING_REACHED;
		if (train_set->pre_max_reached[i])
			buf[i] |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	}

	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET, buf, lanes);
	if (ret < 0)
		return ret;

	return 0;
}

static int dw_dp_phy_configure(struct dw_dp *dp, unsigned int rate,
			       unsigned int lanes, bool ssc)
{
	union phy_configure_opts phy_cfg;
	int ret;

	/* Move PHY to P3 */
	regmap_update_bits(dp->regmap, DW_DP_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x3));

	phy_cfg.dp.lanes = lanes;
	phy_cfg.dp.link_rate = rate / 100;
	phy_cfg.dp.ssc = ssc;
	phy_cfg.dp.set_lanes = true;
	phy_cfg.dp.set_rate = true;
	phy_cfg.dp.set_voltages = false;
	ret = phy_configure(dp->phy, &phy_cfg);
	if (ret)
		return ret;

	regmap_update_bits(dp->regmap, DW_DP_PHYIF_CTRL, PHY_LANES,
			   FIELD_PREP(PHY_LANES, lanes / 2));

	/* Move PHY to P0 */
	regmap_update_bits(dp->regmap, DW_DP_PHYIF_CTRL, PHY_POWERDOWN,
			   FIELD_PREP(PHY_POWERDOWN, 0x0));

	dw_dp_phy_xmit_enable(dp, lanes);

	return 0;
}

static int dw_dp_link_configure(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 buf[2];
	int ret;

	ret = dw_dp_phy_configure(dp, link->rate, link->lanes, link->caps.ssc);
	if (ret)
		return ret;

	buf[0] = drm_dp_link_rate_to_bw_code(link->rate);
	buf[1] = link->lanes;

	if (link->caps.enhanced_framing) {
		buf[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
		regmap_update_bits(dp->regmap, DW_DP_CCTL, ENHANCE_FRAMING_EN,
				   FIELD_PREP(ENHANCE_FRAMING_EN, 1));
	} else {
		regmap_update_bits(dp->regmap, DW_DP_CCTL, ENHANCE_FRAMING_EN,
				   FIELD_PREP(ENHANCE_FRAMING_EN, 0));
	}

	ret = drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	buf[0] = link->caps.ssc ? DP_SPREAD_AMP_0_5 : 0;
	buf[1] = link->caps.channel_coding ? DP_SET_ANSI_8B10B : 0;

	ret = drm_dp_dpcd_write(&dp->aux, DP_DOWNSPREAD_CTRL, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static void dw_dp_link_train_init(struct dw_dp_link_train *train)
{
	struct dw_dp_link_train_set *adj = &train->adjust;
	unsigned int i;

	for (i = 0; i < 4; i++) {
		adj->voltage_swing[i] = 0;
		adj->pre_emphasis[i] = 0;
		adj->voltage_max_reached[i] = false;
		adj->pre_max_reached[i] = false;
	}

	train->clock_recovered = false;
	train->channel_equalized = false;
}

static bool dw_dp_link_train_valid(const struct dw_dp_link_train *train)
{
	return train->clock_recovered && train->channel_equalized;
}

static int dw_dp_link_train_set_pattern(struct dw_dp *dp, u32 pattern)
{
	u8 buf = 0;
	int ret;

	if (pattern && pattern != DP_TRAINING_PATTERN_4) {
		buf |= DP_LINK_SCRAMBLING_DISABLE;

		regmap_update_bits(dp->regmap, DW_DP_CCTL, SCRAMBLE_DIS,
				   FIELD_PREP(SCRAMBLE_DIS, 1));
	} else {
		regmap_update_bits(dp->regmap, DW_DP_CCTL, SCRAMBLE_DIS,
				   FIELD_PREP(SCRAMBLE_DIS, 0));
	}

	switch (pattern) {
	case DP_TRAINING_PATTERN_DISABLE:
		dw_dp_phy_set_pattern(dp, DW_DP_PHY_PATTERN_NONE);
		break;
	case DP_TRAINING_PATTERN_1:
		dw_dp_phy_set_pattern(dp, DW_DP_PHY_PATTERN_TPS_1);
		break;
	case DP_TRAINING_PATTERN_2:
		dw_dp_phy_set_pattern(dp, DW_DP_PHY_PATTERN_TPS_2);
		break;
	case DP_TRAINING_PATTERN_3:
		dw_dp_phy_set_pattern(dp, DW_DP_PHY_PATTERN_TPS_3);
		break;
	case DP_TRAINING_PATTERN_4:
		dw_dp_phy_set_pattern(dp, DW_DP_PHY_PATTERN_TPS_4);
		break;
	default:
		return -EINVAL;
	}

	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET,
				 buf | pattern);
	if (ret < 0)
		return ret;

	return 0;
}

static u8 dw_dp_voltage_max(u8 preemph)
{
	switch (preemph & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_1;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
	default:
		return DP_TRAIN_VOLTAGE_SWING_LEVEL_0;
	}
}

static bool dw_dp_link_get_adjustments(struct dw_dp_link *link,
				       u8 status[DP_LINK_STATUS_SIZE])
{
	struct dw_dp_link_train_set *adj = &link->train.adjust;
	unsigned int i;
	bool changed = false;
	u8 v = 0;
	u8 p = 0;

	for (i = 0; i < link->lanes; i++) {
		v = drm_dp_get_adjust_request_voltage(status, i);
		v >>= DP_TRAIN_VOLTAGE_SWING_SHIFT;
		p = drm_dp_get_adjust_request_pre_emphasis(status, i);
		p >>= DP_TRAIN_PRE_EMPHASIS_SHIFT;

		if (v != adj->voltage_swing[i] || p != adj->pre_emphasis[i])
			changed = true;

		if (p >=  (DP_TRAIN_PRE_EMPH_LEVEL_3 >> DP_TRAIN_PRE_EMPHASIS_SHIFT)) {
			adj->pre_emphasis[i] = DP_TRAIN_PRE_EMPH_LEVEL_3 >>
					       DP_TRAIN_PRE_EMPHASIS_SHIFT;
			adj->pre_max_reached[i] = true;
		} else {
			adj->pre_emphasis[i] = p;
			adj->pre_max_reached[i] = false;
		}

		v = min(v, dw_dp_voltage_max(p));
		if (v >= (DP_TRAIN_VOLTAGE_SWING_LEVEL_3 >> DP_TRAIN_VOLTAGE_SWING_SHIFT)) {
			adj->voltage_swing[i] = DP_TRAIN_VOLTAGE_SWING_LEVEL_3 >>
						DP_TRAIN_VOLTAGE_SWING_SHIFT;
			adj->voltage_max_reached[i] = true;
		} else {
			adj->voltage_swing[i] = v;
			adj->voltage_max_reached[i] = false;
		}
	}

	return changed;
}

static int dw_dp_link_clock_recovery(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 status[DP_LINK_STATUS_SIZE];
	unsigned int tries = 0;
	int ret;
	bool adj_changed;

	ret = dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret)
		return ret;

	for (;;) {
		ret = dw_dp_link_train_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_clock_recovery_delay(&dp->aux, link->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, status);
		if (ret < 0) {
			dev_err(dp->dev, "failed to read link status: %d\n", ret);
			return ret;
		}

		if (drm_dp_clock_recovery_ok(status, link->lanes)) {
			link->train.clock_recovered = true;
			break;
		}

		/*
		 * According to DP spec 1.4, if current ADJ is the same
		 * with previous REQ, we need to retry 5 times.
		 */
		adj_changed = dw_dp_link_get_adjustments(link, status);
		if (!adj_changed)
			tries++;
		else
			tries = 0;

		if (tries == 5)
			break;
	}

	return 0;
}

static int dw_dp_link_channel_equalization(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 status[DP_LINK_STATUS_SIZE], pattern;
	unsigned int tries;
	int ret;

	if (link->caps.tps4_supported)
		pattern = DP_TRAINING_PATTERN_4;
	else if (link->caps.tps3_supported)
		pattern = DP_TRAINING_PATTERN_3;
	else
		pattern = DP_TRAINING_PATTERN_2;
	ret = dw_dp_link_train_set_pattern(dp, pattern);
	if (ret)
		return ret;

	for (tries = 1; tries < 5; tries++) {
		ret = dw_dp_link_train_update_vs_emph(dp);
		if (ret)
			return ret;

		drm_dp_link_train_channel_eq_delay(&dp->aux, link->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, status);
		if (ret < 0)
			return ret;

		if (!drm_dp_clock_recovery_ok(status, link->lanes)) {
			dev_err(dp->dev, "clock recovery lost while equalizing channel\n");
			link->train.clock_recovered = false;
			break;
		}

		if (drm_dp_channel_eq_ok(status, link->lanes)) {
			link->train.channel_equalized = true;
			break;
		}

		dw_dp_link_get_adjustments(link, status);
	}

	return 0;
}

static int dw_dp_link_downgrade(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct dw_dp_bridge_state *state;

	state = dw_dp_get_bridge_state(dp);

	switch (link->rate) {
	case 162000:
		return -EINVAL;
	case 270000:
		link->rate = 162000;
		break;
	case 540000:
		link->rate = 270000;
		break;
	case 810000:
		link->rate = 540000;
		break;
	}

	if (!dw_dp_bandwidth_ok(dp, &state->mode, state->bpp, link->lanes,
				link->rate))
		return -E2BIG;

	return 0;
}

static int dw_dp_link_train_full(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	int ret;

retry:
	dw_dp_link_train_init(&link->train);

	dev_dbg(dp->dev, "full-training link: %u lane%s at %u MHz\n",
		link->lanes, (link->lanes > 1) ? "s" : "", link->rate / 100);

	ret = dw_dp_link_configure(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to configure DP link: %d\n", ret);
		return ret;
	}

	ret = dw_dp_link_clock_recovery(dp);
	if (ret < 0) {
		dev_err(dp->dev, "clock recovery failed: %d\n", ret);
		goto out;
	}

	if (!link->train.clock_recovered) {
		dev_err(dp->dev, "clock recovery failed, downgrading link\n");

		ret = dw_dp_link_downgrade(dp);
		if (ret < 0)
			goto out;
		else
			goto retry;
	}

	dev_dbg(dp->dev, "clock recovery succeeded\n");

	ret = dw_dp_link_channel_equalization(dp);
	if (ret < 0) {
		dev_err(dp->dev, "channel equalization failed: %d\n", ret);
		goto out;
	}

	if (!link->train.channel_equalized) {
		dev_err(dp->dev, "channel equalization failed, downgrading link\n");

		ret = dw_dp_link_downgrade(dp);
		if (ret < 0)
			goto out;
		else
			goto retry;
	}

	dev_dbg(dp->dev, "channel equalization succeeded\n");

out:
	dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
	return ret;
}

static int dw_dp_link_train_fast(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	int ret;
	u8 status[DP_LINK_STATUS_SIZE];
	u8 pattern;

	dw_dp_link_train_init(&link->train);

	dev_dbg(dp->dev, "fast-training link: %u lane%s at %u MHz\n",
		link->lanes, (link->lanes > 1) ? "s" : "", link->rate / 100);

	ret = dw_dp_link_configure(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to configure DP link: %d\n", ret);
		return ret;
	}

	ret = dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret)
		goto out;

	usleep_range(500, 1000);

	if (link->caps.tps4_supported)
		pattern = DP_TRAINING_PATTERN_4;
	else if (link->caps.tps3_supported)
		pattern = DP_TRAINING_PATTERN_3;
	else
		pattern = DP_TRAINING_PATTERN_2;
	ret = dw_dp_link_train_set_pattern(dp, pattern);
	if (ret)
		goto out;

	usleep_range(500, 1000);

	ret = drm_dp_dpcd_read_link_status(&dp->aux, status);
	if (ret < 0) {
		dev_err(dp->dev, "failed to read link status: %d\n", ret);
		goto out;
	}

	if (!drm_dp_clock_recovery_ok(status, link->lanes)) {
		dev_err(dp->dev, "clock recovery failed\n");
		ret = -EIO;
		goto out;
	}

	if (!drm_dp_channel_eq_ok(status, link->lanes)) {
		dev_err(dp->dev, "channel equalization failed\n");
		ret = -EIO;
		goto out;
	}

out:
	dw_dp_link_train_set_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
	return ret;
}

static int dw_dp_link_train(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	int ret;

	if (link->caps.fast_training) {
		if (dw_dp_link_train_valid(&link->train)) {
			ret = dw_dp_link_train_fast(dp);
			if (ret < 0)
				dev_err(dp->dev, "fast link training failed: %d\n", ret);
			else
				return 0;
		}
	}

	ret = dw_dp_link_train_full(dp);
	if (ret < 0) {
		dev_err(dp->dev, "full link training failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int dw_dp_send_sdp(struct dw_dp *dp, struct dw_dp_sdp *sdp)
{
	const u8 *payload = sdp->base.db;
	u32 reg;
	int i, nr;

	nr = find_first_zero_bit(dp->sdp_reg_bank, SDP_REG_BANK_SIZE);
	if (nr < SDP_REG_BANK_SIZE)
		set_bit(nr, dp->sdp_reg_bank);
	else
		return -EBUSY;

	reg = DW_DP_SDP_REGISTER_BANK + nr * 9 * 4;

	/* SDP header */
	regmap_write(dp->regmap, reg, get_unaligned_le32(&sdp->base.sdp_header));

	/* SDP data payload */
	for (i = 1; i < 9; i++, payload += 4)
		regmap_write(dp->regmap, reg + i * 4,
			     FIELD_PREP(SDP_REGS, get_unaligned_le32(payload)));

	if (sdp->flags & DW_DP_SDP_VERTICAL_INTERVAL)
		regmap_update_bits(dp->regmap, DW_DP_SDP_VERTICAL_CTRL,
				   EN_VERTICAL_SDP << nr,
				   EN_VERTICAL_SDP << nr);

	if (sdp->flags & DW_DP_SDP_HORIZONTAL_INTERVAL)
		regmap_update_bits(dp->regmap, DW_DP_SDP_HORIZONTAL_CTRL,
				   EN_HORIZONTAL_SDP << nr,
				   EN_HORIZONTAL_SDP << nr);

	return 0;
}

static int dw_dp_send_vsc_sdp(struct dw_dp *dp)
{
	struct dw_dp_bridge_state *state;
	struct dw_dp_sdp sdp = {};
	struct drm_dp_vsc_sdp vsc = {};

	state = dw_dp_get_bridge_state(dp);
	if (!state)
		return -EINVAL;

	vsc.bpc = state->bpc;

	vsc.sdp_type = DP_SDP_VSC;
	vsc.revision = 0x5;
	vsc.length = 0x13;
	vsc.content_type = DP_CONTENT_TYPE_NOT_DEFINED;

	sdp.flags = DW_DP_SDP_VERTICAL_INTERVAL;

	switch (state->color_format) {
	case DRM_COLOR_FORMAT_YCBCR444:
		vsc.pixelformat = DP_PIXELFORMAT_YUV444;
		break;
	case DRM_COLOR_FORMAT_YCBCR420:
		vsc.pixelformat = DP_PIXELFORMAT_YUV420;
		break;
	case DRM_COLOR_FORMAT_YCBCR422:
		vsc.pixelformat = DP_PIXELFORMAT_YUV422;
		break;
	case DRM_COLOR_FORMAT_RGB444:
	default:
		vsc.pixelformat = DP_PIXELFORMAT_RGB;
		break;
	}

	if (state->color_format == DRM_COLOR_FORMAT_RGB444) {
		vsc.colorimetry = DP_COLORIMETRY_DEFAULT;
		vsc.dynamic_range = DP_DYNAMIC_RANGE_VESA;
	} else {
		vsc.colorimetry = DP_COLORIMETRY_BT709_YCC;
		vsc.dynamic_range = DP_DYNAMIC_RANGE_CTA;
	}

	drm_dp_vsc_sdp_pack(&vsc, &sdp.base);

	return dw_dp_send_sdp(dp, &sdp);
}

static int dw_dp_video_set_pixel_mode(struct dw_dp *dp)
{
	switch (dp->pixel_mode) {
	case DW_DP_MP_SINGLE_PIXEL:
	case DW_DP_MP_DUAL_PIXEL:
	case DW_DP_MP_QUAD_PIXEL:
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(dp->regmap, DW_DP_VSAMPLE_CTRL, PIXEL_MODE_SELECT,
			   FIELD_PREP(PIXEL_MODE_SELECT, dp->pixel_mode));

	return 0;
}

static bool dw_dp_video_need_vsc_sdp(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct dw_dp_bridge_state *state;

	state = dw_dp_get_bridge_state(dp);
	if (!state)
		return -EINVAL;

	if (!link->vsc_sdp_supported)
		return false;

	if (state->color_format == DRM_COLOR_FORMAT_YCBCR420)
		return true;

	return false;
}

static int dw_dp_video_set_msa(struct dw_dp *dp, u8 color_format, u8 bpc,
			       u16 vstart, u16 hstart)
{
	u16 misc = 0;

	if (dw_dp_video_need_vsc_sdp(dp))
		misc |= DP_MSA_MISC_COLOR_VSC_SDP;

	switch (color_format) {
	case DRM_COLOR_FORMAT_RGB444:
		misc |= DP_MSA_MISC_COLOR_RGB;
		break;
	case DRM_COLOR_FORMAT_YCBCR444:
		misc |= DP_MSA_MISC_COLOR_YCBCR_444_BT709;
		break;
	case DRM_COLOR_FORMAT_YCBCR422:
		misc |= DP_MSA_MISC_COLOR_YCBCR_422_BT709;
		break;
	case DRM_COLOR_FORMAT_YCBCR420:
		break;
	default:
		return -EINVAL;
	}

	switch (bpc) {
	case 6:
		misc |= DP_MSA_MISC_6_BPC;
		break;
	case 8:
		misc |= DP_MSA_MISC_8_BPC;
		break;
	case 10:
		misc |= DP_MSA_MISC_10_BPC;
		break;
	case 12:
		misc |= DP_MSA_MISC_12_BPC;
		break;
	case 16:
		misc |= DP_MSA_MISC_16_BPC;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(dp->regmap, DW_DP_VIDEO_MSA1,
		     FIELD_PREP(VSTART, vstart) | FIELD_PREP(HSTART, hstart));
	regmap_write(dp->regmap, DW_DP_VIDEO_MSA2, FIELD_PREP(MISC0, misc));
	regmap_write(dp->regmap, DW_DP_VIDEO_MSA3, FIELD_PREP(MISC1, misc >> 8));

	return 0;
}

static void dw_dp_video_disable(struct dw_dp *dp)
{
	regmap_update_bits(dp->regmap, DW_DP_VSAMPLE_CTRL, VIDEO_STREAM_ENABLE,
			   FIELD_PREP(VIDEO_STREAM_ENABLE, 0));
}

static int dw_dp_video_enable(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	struct dw_dp_bridge_state *state;
	struct drm_display_mode *mode;
	u8 color_format, bpc, bpp;
	u8 init_threshold, vic;
	u32 hstart, hactive, hblank, h_sync_width, h_front_porch;
	u32 vstart, vactive, vblank, v_sync_width, v_front_porch;
	u32 peak_stream_bandwidth, link_bandwidth;
	u32 average_bytes_per_tu, average_bytes_per_tu_frac;
	u32 ts, hblank_interval;
	u32 value;
	int ret;

	state = dw_dp_get_bridge_state(dp);
	if (!state)
		return -EINVAL;

	bpc = state->bpc;
	bpp = state->bpp;
	color_format = state->color_format;
	mode = &state->mode;

	vstart = mode->vtotal - mode->vsync_start;
	hstart = mode->htotal - mode->hsync_start;

	ret = dw_dp_video_set_pixel_mode(dp);
	if (ret)
		return ret;

	ret = dw_dp_video_set_msa(dp, color_format, bpc, vstart, hstart);
	if (ret)
		return ret;

	regmap_update_bits(dp->regmap, DW_DP_VSAMPLE_CTRL, VIDEO_MAPPING,
			   FIELD_PREP(VIDEO_MAPPING, state->video_mapping));

	/* Configure DW_DP_VINPUT_POLARITY_CTRL register */
	value = 0;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		value |= FIELD_PREP(HSYNC_IN_POLARITY, 1);
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		value |= FIELD_PREP(VSYNC_IN_POLARITY, 1);
	regmap_write(dp->regmap, DW_DP_VINPUT_POLARITY_CTRL, value);

	/* Configure DW_DP_VIDEO_CONFIG1 register */
	hactive = mode->hdisplay;
	hblank = mode->htotal - mode->hdisplay;
	value = FIELD_PREP(HACTIVE, hactive) | FIELD_PREP(HBLANK, hblank);
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		value |= FIELD_PREP(I_P, 1);
	vic = drm_match_cea_mode(mode);
	if (vic == 5 || vic == 6 || vic == 7 ||
	    vic == 10 || vic == 11 || vic == 20 ||
	    vic == 21 || vic == 22 || vic == 39 ||
	    vic == 25 || vic == 26 || vic == 40 ||
	    vic == 44 || vic == 45 || vic == 46 ||
	    vic == 50 || vic == 51 || vic == 54 ||
	    vic == 55 || vic == 58 || vic  == 59)
		value |= R_V_BLANK_IN_OSC;
	regmap_write(dp->regmap, DW_DP_VIDEO_CONFIG1, value);

	/* Configure DW_DP_VIDEO_CONFIG2 register */
	vblank = mode->vtotal - mode->vdisplay;
	vactive = mode->vdisplay;
	regmap_write(dp->regmap, DW_DP_VIDEO_CONFIG2,
		     FIELD_PREP(VBLANK, vblank) | FIELD_PREP(VACTIVE, vactive));

	/* Configure DW_DP_VIDEO_CONFIG3 register */
	h_sync_width = mode->hsync_end - mode->hsync_start;
	h_front_porch = mode->hsync_start - mode->hdisplay;
	regmap_write(dp->regmap, DW_DP_VIDEO_CONFIG3,
		     FIELD_PREP(H_SYNC_WIDTH, h_sync_width) |
		     FIELD_PREP(H_FRONT_PORCH, h_front_porch));

	/* Configure DW_DP_VIDEO_CONFIG4 register */
	v_sync_width = mode->vsync_end - mode->vsync_start;
	v_front_porch = mode->vsync_start - mode->vdisplay;
	regmap_write(dp->regmap, DW_DP_VIDEO_CONFIG4,
		     FIELD_PREP(V_SYNC_WIDTH, v_sync_width) |
		     FIELD_PREP(V_FRONT_PORCH, v_front_porch));

	/* Configure DW_DP_VIDEO_CONFIG5 register */
	peak_stream_bandwidth = mode->clock * bpp / 8;
	link_bandwidth = (link->rate / 1000) * link->lanes;
	ts = peak_stream_bandwidth * 64 / link_bandwidth;
	average_bytes_per_tu = ts / 1000;
	average_bytes_per_tu_frac = ts / 100 - average_bytes_per_tu * 10;
	if (dp->pixel_mode == DW_DP_MP_SINGLE_PIXEL) {
		if (average_bytes_per_tu < 6)
			init_threshold = 32;
		else if (hblank <= 80 && color_format != DRM_COLOR_FORMAT_YCBCR420)
			init_threshold = 12;
		else if (hblank <= 40 && color_format == DRM_COLOR_FORMAT_YCBCR420)
			init_threshold = 3;
		else
			init_threshold = 16;
	} else {
		u32 t1 = 0, t2 = 0, t3 = 0;

		switch (bpc) {
		case 6:
			t1 = (4 * 1000 / 9) * link->lanes;
			break;
		case 8:
			if (color_format == DRM_COLOR_FORMAT_YCBCR422) {
				t1 = (1000 / 2) * link->lanes;
			} else {
				if (dp->pixel_mode == DW_DP_MP_DUAL_PIXEL)
					t1 = (1000 / 3) * link->lanes;
				else
					t1 = (3000 / 16) * link->lanes;
			}
			break;
		case 10:
			if (color_format == DRM_COLOR_FORMAT_YCBCR422)
				t1 = (2000 / 5) * link->lanes;
			else
				t1 = (4000 / 15) * link->lanes;
			break;
		case 12:
			if (color_format == DRM_COLOR_FORMAT_YCBCR422) {
				if (dp->pixel_mode == DW_DP_MP_DUAL_PIXEL)
					t1 = (1000 / 6) * link->lanes;
				else
					t1 = (1000 / 3) * link->lanes;
			} else {
				t1 = (2000 / 9) * link->lanes;
			}
			break;
		case 16:
			if (color_format != DRM_COLOR_FORMAT_YCBCR422 &&
			    dp->pixel_mode == DW_DP_MP_DUAL_PIXEL)
				t1 = (1000 / 6) * link->lanes;
			else
				t1 = (1000 / 4) * link->lanes;
			break;
		default:
			return -EINVAL;
		}

		if (color_format == DRM_COLOR_FORMAT_YCBCR420)
			t2 = (link->rate / 4) * 1000 / (mode->clock / 2);
		else
			t2 = (link->rate / 4) * 1000 / mode->clock;

		if (average_bytes_per_tu_frac)
			t3 = average_bytes_per_tu + 1;
		else
			t3 = average_bytes_per_tu;
		init_threshold = t1 * t2 * t3 / (1000 * 1000);
		if (init_threshold <= 16 || average_bytes_per_tu < 10)
			init_threshold = 40;
	}

	regmap_write(dp->regmap, DW_DP_VIDEO_CONFIG5,
		     FIELD_PREP(INIT_THRESHOLD_HI, init_threshold >> 6) |
		     FIELD_PREP(AVERAGE_BYTES_PER_TU_FRAC, average_bytes_per_tu_frac) |
		     FIELD_PREP(INIT_THRESHOLD, init_threshold) |
		     FIELD_PREP(AVERAGE_BYTES_PER_TU, average_bytes_per_tu));

	/* Configure DW_DP_VIDEO_HBLANK_INTERVAL register */
	hblank_interval = hblank * (link->rate / 4) / mode->clock;
	regmap_write(dp->regmap, DW_DP_VIDEO_HBLANK_INTERVAL,
		     FIELD_PREP(HBLANK_INTERVAL_EN, 1) |
		     FIELD_PREP(HBLANK_INTERVAL, hblank_interval));

	/* Video stream enable */
	regmap_update_bits(dp->regmap, DW_DP_VSAMPLE_CTRL, VIDEO_STREAM_ENABLE,
			   FIELD_PREP(VIDEO_STREAM_ENABLE, 1));

	if (dw_dp_video_need_vsc_sdp(dp))
		dw_dp_send_vsc_sdp(dp);

	return 0;
}

static void dw_dp_hpd_init(struct dw_dp *dp)
{
	/* Enable all HPD interrupts */
	regmap_update_bits(dp->regmap, DW_DP_HPD_INTERRUPT_ENABLE,
			   HPD_UNPLUG_EN | HPD_PLUG_EN | HPD_IRQ_EN,
			   FIELD_PREP(HPD_UNPLUG_EN, 1) |
			   FIELD_PREP(HPD_PLUG_EN, 1) |
			   FIELD_PREP(HPD_IRQ_EN, 1));

	/* Enable all top-level interrupts */
	regmap_update_bits(dp->regmap, DW_DP_GENERAL_INTERRUPT_ENABLE,
			   HPD_EVENT_EN, FIELD_PREP(HPD_EVENT_EN, 1));
}

static void dw_dp_aux_init(struct dw_dp *dp)
{
	regmap_update_bits(dp->regmap, DW_DP_GENERAL_INTERRUPT_ENABLE,
			   AUX_REPLY_EVENT_EN, FIELD_PREP(AUX_REPLY_EVENT_EN, 1));
}

static void dw_dp_init_hw(struct dw_dp *dp)
{
	regmap_update_bits(dp->regmap, DW_DP_CCTL, DEFAULT_FAST_LINK_TRAIN_EN,
			   FIELD_PREP(DEFAULT_FAST_LINK_TRAIN_EN, 0));

	dw_dp_hpd_init(dp);
	dw_dp_aux_init(dp);
}

static int dw_dp_aux_write_data(struct dw_dp *dp, const u8 *buffer, size_t size)
{
	size_t i, j;

	for (i = 0; i < DIV_ROUND_UP(size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4, 4);
		u32 value = 0;

		for (j = 0; j < num; j++)
			value |= buffer[i * 4 + j] << (j * 8);

		regmap_write(dp->regmap, DW_DP_AUX_DATA0 + i * 4, value);
	}

	return size;
}

static int dw_dp_aux_read_data(struct dw_dp *dp, u8 *buffer, size_t size)
{
	size_t i, j;

	for (i = 0; i < DIV_ROUND_UP(size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4, 4);
		u32 value;

		regmap_read(dp->regmap, DW_DP_AUX_DATA0 + i * 4, &value);

		for (j = 0; j < num; j++)
			buffer[i * 4 + j] = value >> (j * 8);
	}

	return size;
}

static ssize_t dw_dp_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	struct dw_dp *dp = container_of(aux, struct dw_dp, aux);
	unsigned long timeout = msecs_to_jiffies(10);
	u32 status, value;
	ssize_t ret = 0;

	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		ret = dw_dp_aux_write_data(dp, msg->buffer, msg->size);
		if (ret < 0)
			return ret;
		break;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		break;
	default:
		return -EINVAL;
	}

	if (msg->size > 0)
		value = FIELD_PREP(AUX_LEN_REQ, msg->size - 1);
	else
		value = FIELD_PREP(I2C_ADDR_ONLY, 1);
	value |= FIELD_PREP(AUX_CMD_TYPE, msg->request);
	value |= FIELD_PREP(AUX_ADDR, msg->address);
	regmap_write(dp->regmap, DW_DP_AUX_CMD, value);

	status = wait_for_completion_timeout(&dp->complete, timeout);
	if (!status) {
		dev_err(dp->dev, "timeout waiting for AUX reply\n");
		return -ETIMEDOUT;
	}

	regmap_read(dp->regmap, DW_DP_AUX_STATUS, &value);
	if (value & AUX_TIMEOUT)
		return -ETIMEDOUT;

	msg->reply = FIELD_GET(AUX_STATUS, value);

	if (msg->size > 0 && msg->reply == DP_AUX_NATIVE_REPLY_ACK) {
		if (msg->request & DP_AUX_I2C_READ) {
			size_t count = FIELD_GET(AUX_BYTES_READ, value) - 1;

			if (count != msg->size)
				return -EBUSY;

			ret = dw_dp_aux_read_data(dp, msg->buffer, count);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

/*
 * Limits for the video timing for DP:
 * 1. the hfp should be 2 pixels aligned;
 * 2. the minimum hsync should be 9 pixel;
 * 3. the minimum hbp should be 16 pixel;
 */
static int dw_dp_bridge_atomic_check(struct drm_bridge *bridge,
				     struct drm_bridge_state *bridge_state,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct dw_dp *dp = bridge_to_dp(bridge);
	struct dw_dp_bridge_state *state;
	const struct dw_dp_output_format *fmt;
	struct drm_display_mode *mode;
	int min_hbp = 16;
	int min_hsync = 9;

	state = to_dw_dp_bridge_state(bridge_state);
	mode = &state->mode;

	fmt = dw_dp_get_output_format(bridge_state->output_bus_cfg.format);
	if (!fmt)
		return -EINVAL;

	state->video_mapping = fmt->video_mapping;
	state->color_format = fmt->color_format;
	state->bpc = fmt->bpc;
	state->bpp = fmt->bpp;

	if ((adjusted_mode->hsync_start - adjusted_mode->hdisplay) & 0x1) {
		adjusted_mode->hsync_start += 1;
		dev_warn(dp->dev, "hfp is not 2 pixeel aligned, fixup to aligned hfp\n");
	}

	if (adjusted_mode->hsync_end - adjusted_mode->hsync_start < min_hsync) {
		adjusted_mode->hsync_end = adjusted_mode->hsync_start + min_hsync;
		dev_warn(dp->dev, "hsync is too narrow, fixup to min hsync:%d\n", min_hsync);
	}

	if (adjusted_mode->htotal - adjusted_mode->hsync_end < min_hbp) {
		adjusted_mode->htotal = adjusted_mode->hsync_end + min_hbp;
		dev_warn(dp->dev, "hbp is too narrow, fixup to min hbp:%d\n", min_hbp);
	}

	drm_mode_copy(mode, adjusted_mode);

	return 0;
}

static enum drm_mode_status dw_dp_bridge_mode_valid(struct drm_bridge *bridge,
						    const struct drm_display_info *info,
						    const struct drm_display_mode *mode)
{
	struct dw_dp *dp = bridge_to_dp(bridge);
	struct dw_dp_link *link = &dp->link;
	u32 min_bpp;

	if (info->color_formats & DRM_COLOR_FORMAT_YCBCR420 &&
	    link->vsc_sdp_supported &&
	    (drm_mode_is_420_only(info, mode) || drm_mode_is_420_also(info, mode)))
		min_bpp = 12;
	else if (info->color_formats & DRM_COLOR_FORMAT_YCBCR422)
		min_bpp = 16;
	else if (info->color_formats & DRM_COLOR_FORMAT_RGB444)
		min_bpp = 18;
	else
		min_bpp = 24;

	if (!link->vsc_sdp_supported &&
	    drm_mode_is_420_only(info, mode))
		return MODE_NO_420;

	if (!dw_dp_bandwidth_ok(dp, mode, min_bpp, link->lanes, link->rate))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool dw_dp_needs_link_retrain(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!dw_dp_link_train_valid(&link->train))
		return false;

	if (drm_dp_dpcd_read_link_status(&dp->aux, link_status) < 0)
		return false;

	/* Retrain if Channel EQ or CR not ok */
	return !drm_dp_channel_eq_ok(link_status, dp->link.lanes);
}

static void dw_dp_link_disable(struct dw_dp *dp)
{
	struct dw_dp_link *link = &dp->link;

	if (dw_dp_hpd_detect(dp))
		drm_dp_link_power_down(&dp->aux, dp->link.revision);

	dw_dp_phy_xmit_enable(dp, 0);

	phy_power_off(dp->phy);

	link->train.clock_recovered = false;
	link->train.channel_equalized = false;
}

static int dw_dp_link_enable(struct dw_dp *dp)
{
	int ret;

	ret = phy_power_on(dp->phy);
	if (ret)
		return ret;

	ret = drm_dp_link_power_up(&dp->aux, dp->link.revision);
	if (ret < 0)
		return ret;

	ret = dw_dp_link_train(dp);

	return ret;
}

static void dw_dp_bridge_atomic_enable(struct drm_bridge *bridge,
				       struct drm_atomic_state *state)
{
	struct dw_dp *dp = bridge_to_dp(bridge);
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	int ret;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (!connector) {
		dev_err(dp->dev, "failed to get connector\n");
		return;
	}

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state) {
		dev_err(dp->dev, "failed to get connector state\n");
		return;
	}

	set_bit(0, dp->sdp_reg_bank);

	ret = dw_dp_link_enable(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable link: %d\n", ret);
		return;
	}

	ret = dw_dp_video_enable(dp);
	if (ret < 0) {
		dev_err(dp->dev, "failed to enable video: %d\n", ret);
		return;
	}
}

static void dw_dp_reset(struct dw_dp *dp)
{
	int val;

	disable_irq(dp->irq);
	regmap_update_bits(dp->regmap, DW_DP_SOFT_RESET_CTRL, CONTROLLER_RESET,
			   FIELD_PREP(CONTROLLER_RESET, 1));
	usleep_range(10, 20);
	regmap_update_bits(dp->regmap, DW_DP_SOFT_RESET_CTRL, CONTROLLER_RESET,
			   FIELD_PREP(CONTROLLER_RESET, 0));

	dw_dp_init_hw(dp);
	regmap_read_poll_timeout(dp->regmap, DW_DP_HPD_STATUS, val,
				 FIELD_GET(HPD_HOT_PLUG, val), 200, 200000);
	regmap_write(dp->regmap, DW_DP_HPD_STATUS, HPD_HOT_PLUG);
	enable_irq(dp->irq);
}

static void dw_dp_bridge_atomic_disable(struct drm_bridge *bridge,
					struct drm_atomic_state *state)
{
	struct dw_dp *dp = bridge_to_dp(bridge);

	dw_dp_video_disable(dp);
	dw_dp_link_disable(dp);
	bitmap_zero(dp->sdp_reg_bank, SDP_REG_BANK_SIZE);
	dw_dp_reset(dp);
}

static bool dw_dp_hpd_detect_link(struct dw_dp *dp, struct drm_connector *connector)
{
	int ret;

	ret = phy_power_on(dp->phy);
	if (ret < 0)
		return false;
	ret = dw_dp_link_parse(dp, connector);
	phy_power_off(dp->phy);

	return !ret;
}

static enum drm_connector_status dw_dp_bridge_detect(struct drm_bridge *bridge,
						     struct drm_connector *connector)
{
	struct dw_dp *dp = bridge_to_dp(bridge);

	if (!dw_dp_hpd_detect(dp))
		return connector_status_disconnected;

	if (!dw_dp_hpd_detect_link(dp, connector))
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_edid *dw_dp_bridge_edid_read(struct drm_bridge *bridge,
						     struct drm_connector *connector)
{
	struct dw_dp *dp = bridge_to_dp(bridge);
	const struct drm_edid *edid;
	int ret;

	ret = phy_power_on(dp->phy);
	if (ret)
		return NULL;

	edid = drm_edid_read_ddc(connector, &dp->aux.ddc);

	phy_power_off(dp->phy);

	return edid;
}

static u32 *dw_dp_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
						    struct drm_bridge_state *bridge_state,
						    struct drm_crtc_state *crtc_state,
						    struct drm_connector_state *conn_state,
						    unsigned int *num_output_fmts)
{
	struct dw_dp *dp = bridge_to_dp(bridge);
	struct dw_dp_link *link = &dp->link;
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct drm_display_mode mode = crtc_state->mode;
	const struct dw_dp_output_format *fmt;
	u32 i, j = 0;
	u32 *output_fmts;

	*num_output_fmts = 0;

	output_fmts = kcalloc(ARRAY_SIZE(dw_dp_output_formats), sizeof(*output_fmts), GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(dw_dp_output_formats); i++) {
		fmt = &dw_dp_output_formats[i];

		if (fmt->bpc > conn_state->max_bpc)
			continue;

		if (!(fmt->color_format & di->color_formats))
			continue;

		if (fmt->color_format == DRM_COLOR_FORMAT_YCBCR420 &&
		    !link->vsc_sdp_supported)
			continue;

		if (fmt->color_format != DRM_COLOR_FORMAT_YCBCR420 &&
		    drm_mode_is_420_only(di, &mode))
			continue;

		if (!dw_dp_bandwidth_ok(dp, &mode, fmt->bpp, link->lanes, link->rate))
			continue;

		output_fmts[j++] = fmt->bus_format;
	}

	*num_output_fmts = j;

	return output_fmts;
}

static struct drm_bridge_state *dw_dp_bridge_atomic_duplicate_state(struct drm_bridge *bridge)
{
	struct dw_dp_bridge_state *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_bridge_duplicate_state(bridge, &state->base);

	return &state->base;
}

static const struct drm_bridge_funcs dw_dp_bridge_funcs = {
	.atomic_duplicate_state = dw_dp_bridge_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_get_output_bus_fmts = dw_dp_bridge_atomic_get_output_bus_fmts,
	.atomic_check = dw_dp_bridge_atomic_check,
	.mode_valid = dw_dp_bridge_mode_valid,
	.atomic_enable = dw_dp_bridge_atomic_enable,
	.atomic_disable = dw_dp_bridge_atomic_disable,
	.detect = dw_dp_bridge_detect,
	.edid_read = dw_dp_bridge_edid_read,
};

static int dw_dp_link_retrain(struct dw_dp *dp)
{
	struct drm_device *dev = dp->bridge.dev;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	if (!dw_dp_needs_link_retrain(dp))
		return 0;

	dev_dbg(dp->dev, "Retraining link\n");

	drm_modeset_acquire_init(&ctx, 0);
	for (;;) {
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(&ctx);
	}

	if (!ret)
		ret = dw_dp_link_train(dp);

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

static void dw_dp_hpd_work(struct work_struct *work)
{
	struct dw_dp *dp = container_of(work, struct dw_dp, hpd_work);
	bool long_hpd;
	int ret;

	mutex_lock(&dp->irq_lock);
	long_hpd = dp->hotplug.long_hpd;
	mutex_unlock(&dp->irq_lock);

	dev_dbg(dp->dev, "[drm] Get hpd irq - %s\n", long_hpd ? "long" : "short");

	if (!long_hpd) {
		if (dw_dp_needs_link_retrain(dp)) {
			ret = dw_dp_link_retrain(dp);
			if (ret)
				dev_warn(dp->dev, "Retrain link failed\n");
		}
	} else {
		drm_helper_hpd_irq_event(dp->bridge.dev);
	}
}

static void dw_dp_handle_hpd_event(struct dw_dp *dp)
{
	u32 value;

	mutex_lock(&dp->irq_lock);
	regmap_read(dp->regmap, DW_DP_HPD_STATUS, &value);

	if (value & HPD_IRQ) {
		dev_dbg(dp->dev, "IRQ from the HPD\n");
		dp->hotplug.long_hpd = false;
		regmap_write(dp->regmap, DW_DP_HPD_STATUS, HPD_IRQ);
	}

	if (value & HPD_HOT_PLUG) {
		dev_dbg(dp->dev, "Hot plug detected\n");
		dp->hotplug.long_hpd = true;
		regmap_write(dp->regmap, DW_DP_HPD_STATUS, HPD_HOT_PLUG);
	}

	if (value & HPD_HOT_UNPLUG) {
		dev_dbg(dp->dev, "Unplug detected\n");
		dp->hotplug.long_hpd = true;
		regmap_write(dp->regmap, DW_DP_HPD_STATUS, HPD_HOT_UNPLUG);
	}
	mutex_unlock(&dp->irq_lock);

	schedule_work(&dp->hpd_work);
}

static irqreturn_t dw_dp_irq(int irq, void *data)
{
	struct dw_dp *dp = data;
	u32 value;

	regmap_read(dp->regmap, DW_DP_GENERAL_INTERRUPT, &value);
	if (!value)
		return IRQ_NONE;

	if (value & HPD_EVENT)
		dw_dp_handle_hpd_event(dp);

	if (value & AUX_REPLY_EVENT) {
		regmap_write(dp->regmap, DW_DP_GENERAL_INTERRUPT, AUX_REPLY_EVENT);
		complete(&dp->complete);
	}

	return IRQ_HANDLED;
}

static const struct regmap_range dw_dp_readable_ranges[] = {
	regmap_reg_range(DW_DP_VERSION_NUMBER, DW_DP_ID),
	regmap_reg_range(DW_DP_CONFIG_REG1, DW_DP_CONFIG_REG3),
	regmap_reg_range(DW_DP_CCTL, DW_DP_SOFT_RESET_CTRL),
	regmap_reg_range(DW_DP_VSAMPLE_CTRL, DW_DP_VIDEO_HBLANK_INTERVAL),
	regmap_reg_range(DW_DP_AUD_CONFIG1, DW_DP_AUD_CONFIG1),
	regmap_reg_range(DW_DP_SDP_VERTICAL_CTRL, DW_DP_SDP_STATUS_EN),
	regmap_reg_range(DW_DP_PHYIF_CTRL, DW_DP_PHYIF_PWRDOWN_CTRL),
	regmap_reg_range(DW_DP_AUX_CMD, DW_DP_AUX_DATA3),
	regmap_reg_range(DW_DP_GENERAL_INTERRUPT, DW_DP_HPD_INTERRUPT_ENABLE),
};

static const struct regmap_access_table dw_dp_readable_table = {
	.yes_ranges     = dw_dp_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(dw_dp_readable_ranges),
};

static const struct regmap_config dw_dp_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = DW_DP_MAX_REGISTER,
	.rd_table = &dw_dp_readable_table,
};

static void dw_dp_phy_exit(void *data)
{
	struct dw_dp *dp = data;

	phy_exit(dp->phy);
}

struct dw_dp *dw_dp_bind(struct device *dev, struct drm_encoder *encoder,
			 const struct dw_dp_plat_data *plat_data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_dp *dp;
	struct drm_bridge *bridge;
	void __iomem *res;
	int ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return ERR_PTR(-ENOMEM);

	dp = devm_drm_bridge_alloc(dev, struct dw_dp, bridge, &dw_dp_bridge_funcs);
	if (IS_ERR(dp))
		return ERR_CAST(dp);

	dp->dev = dev;
	dp->pixel_mode = DW_DP_MP_QUAD_PIXEL;

	dp->plat_data.max_link_rate = plat_data->max_link_rate;
	bridge = &dp->bridge;
	mutex_init(&dp->irq_lock);
	INIT_WORK(&dp->hpd_work, dw_dp_hpd_work);
	init_completion(&dp->complete);

	res = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(res))
		return ERR_CAST(res);

	dp->regmap = devm_regmap_init_mmio(dev, res, &dw_dp_regmap_config);
	if (IS_ERR(dp->regmap)) {
		dev_err_probe(dev, PTR_ERR(dp->regmap), "failed to create regmap\n");
		return ERR_CAST(dp->regmap);
	}

	dp->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(dp->phy)) {
		dev_err_probe(dev, PTR_ERR(dp->phy), "failed to get phy\n");
		return ERR_CAST(dp->phy);
	}

	dp->apb_clk = devm_clk_get_enabled(dev, "apb");
	if (IS_ERR(dp->apb_clk)) {
		dev_err_probe(dev, PTR_ERR(dp->apb_clk), "failed to get apb clock\n");
		return ERR_CAST(dp->apb_clk);
	}

	dp->aux_clk = devm_clk_get_enabled(dev, "aux");
	if (IS_ERR(dp->aux_clk)) {
		dev_err_probe(dev, PTR_ERR(dp->aux_clk), "failed to get aux clock\n");
		return ERR_CAST(dp->aux_clk);
	}

	dp->i2s_clk = devm_clk_get(dev, "i2s");
	if (IS_ERR(dp->i2s_clk)) {
		dev_err_probe(dev, PTR_ERR(dp->i2s_clk), "failed to get i2s clock\n");
		return ERR_CAST(dp->i2s_clk);
	}

	dp->spdif_clk = devm_clk_get(dev, "spdif");
	if (IS_ERR(dp->spdif_clk)) {
		dev_err_probe(dev, PTR_ERR(dp->spdif_clk), "failed to get spdif clock\n");
		return ERR_CAST(dp->spdif_clk);
	}

	dp->hdcp_clk = devm_clk_get(dev, "hdcp");
	if (IS_ERR(dp->hdcp_clk)) {
		dev_err_probe(dev, PTR_ERR(dp->hdcp_clk), "failed to get hdcp clock\n");
		return ERR_CAST(dp->hdcp_clk);
	}

	dp->rstc = devm_reset_control_get(dev, NULL);
	if (IS_ERR(dp->rstc)) {
		dev_err_probe(dev, PTR_ERR(dp->rstc), "failed to get reset control\n");
		return ERR_CAST(dp->rstc);
	}

	bridge->of_node = dev->of_node;
	bridge->ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID | DRM_BRIDGE_OP_HPD;
	bridge->type = DRM_MODE_CONNECTOR_DisplayPort;
	bridge->ycbcr_420_allowed = true;

	dp->aux.dev = dev;
	dp->aux.drm_dev = encoder->dev;
	dp->aux.name = dev_name(dev);
	dp->aux.transfer = dw_dp_aux_transfer;
	ret = drm_dp_aux_register(&dp->aux);
	if (ret) {
		dev_err_probe(dev, ret, "Aux register failed\n");
		return ERR_PTR(ret);
	}

	ret = drm_bridge_attach(encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		dev_err_probe(dev, ret, "Failed to attach bridge\n");

	dw_dp_init_hw(dp);

	ret = phy_init(dp->phy);
	if (ret) {
		dev_err_probe(dev, ret, "phy init failed\n");
		return ERR_PTR(ret);
	}

	ret = devm_add_action_or_reset(dev, dw_dp_phy_exit, dp);
	if (ret)
		return ERR_PTR(ret);

	dp->irq = platform_get_irq(pdev, 0);
	if (dp->irq < 0)
		return ERR_PTR(ret);

	ret = devm_request_threaded_irq(dev, dp->irq, NULL, dw_dp_irq,
					IRQF_ONESHOT, dev_name(dev), dp);
	if (ret) {
		dev_err_probe(dev, ret, "failed to request irq\n");
		return ERR_PTR(ret);
	}

	return dp;
}
EXPORT_SYMBOL_GPL(dw_dp_bind);

MODULE_AUTHOR("Andy Yan <andyshrk@163.com>");
MODULE_DESCRIPTION("DW DP Core Library");
MODULE_LICENSE("GPL");
