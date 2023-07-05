// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Guochun Huang <hero.huang@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/gpio.h>
#include <linux/iopoll.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dsc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <video/mipi_display.h>
#include <video/videomode.h>
#include <asm/unaligned.h>
#include <uapi/linux/videodev2.h>
#include <drm/drm_panel.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define UPDATE(v, h, l)			(((v) << (l)) & GENMASK((h), (l)))

#define DSI2_PWR_UP			0x000c
#define RESET				0
#define POWER_UP			BIT(0)
#define CMD_TX_MODE(x)			UPDATE(x,  24,  24)
#define DSI2_SOFT_RESET			0x0010
#define SYS_RSTN			BIT(2)
#define PHY_RSTN			BIT(1)
#define IPI_RSTN			BIT(0)
#define INT_ST_MAIN			0x0014
#define DSI2_MODE_CTRL			0x0018
#define DSI2_MODE_STATUS		0x001c
#define DSI2_CORE_STATUS		0x0020
#define PRI_RD_DATA_AVAIL		BIT(26)
#define PRI_FIFOS_NOT_EMPTY		BIT(25)
#define PRI_BUSY			BIT(24)
#define CRI_RD_DATA_AVAIL		BIT(18)
#define CRT_FIFOS_NOT_EMPTY		BIT(17)
#define CRI_BUSY			BIT(16)
#define IPI_FIFOS_NOT_EMPTY		BIT(9)
#define IPI_BUSY			BIT(8)
#define CORE_FIFOS_NOT_EMPTY		BIT(1)
#define CORE_BUSY			BIT(0)
#define MANUAL_MODE_CFG			0x0024
#define MANUAL_MODE_EN			BIT(0)
#define DSI2_TIMEOUT_HSTX_CFG		0x0048
#define TO_HSTX(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_HSTXRDY_CFG	0x004c
#define TO_HSTXRDY(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_LPRX_CFG		0x0050
#define TO_LPRXRDY(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_LPTXRDY_CFG	0x0054
#define TO_LPTXRDY(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_LPTXTRIG_CFG	0x0058
#define TO_LPTXTRIG(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_LPTXULPS_CFG	0x005c
#define TO_LPTXULPS(x)			UPDATE(x, 15, 0)
#define DSI2_TIMEOUT_BTA_CFG		0x60
#define TO_BTA(x)			UPDATE(x, 15, 0)

#define DSI2_PHY_MODE_CFG		0x0100
#define PPI_WIDTH(x)			UPDATE(x, 9, 8)
#define PHY_LANES(x)			UPDATE(x - 1, 5, 4)
#define PHY_TYPE(x)			UPDATE(x, 0, 0)
#define DSI2_PHY_CLK_CFG		0X0104
#define PHY_LPTX_CLK_DIV(x)		UPDATE(x, 12, 8)
#define CLK_TYPE_MASK			BIT(0)
#define NON_CONTINUOUS_CLK		BIT(0)
#define CONTIUOUS_CLK			0
#define DSI2_PHY_LP2HS_MAN_CFG		0x010c
#define PHY_LP2HS_TIME(x)		UPDATE(x, 28, 0)
#define DSI2_PHY_HS2LP_MAN_CFG		0x0114
#define PHY_HS2LP_TIME(x)		UPDATE(x, 28, 0)
#define DSI2_PHY_MAX_RD_T_MAN_CFG	0x011c
#define PHY_MAX_RD_TIME(x)		UPDATE(x, 26, 0)
#define DSI2_PHY_ESC_CMD_T_MAN_CFG	0x0124
#define PHY_ESC_CMD_TIME(x)		UPDATE(x, 28, 0)
#define DSI2_PHY_ESC_BYTE_T_MAN_CFG	0x012c
#define PHY_ESC_BYTE_TIME(x)		UPDATE(x, 28, 0)

#define DSI2_PHY_IPI_RATIO_MAN_CFG	0x0134
#define PHY_IPI_RATIO(x)		UPDATE(x, 21, 0)
#define DSI2_PHY_SYS_RATIO_MAN_CFG	0x013C
#define PHY_SYS_RATIO(x)		UPDATE(x, 16, 0)

#define DSI2_DSI_GENERAL_CFG		0x0200
#define BTA_EN				BIT(1)
#define EOTP_TX_EN			BIT(0)
#define DSI2_DSI_VCID_CFG		0x0204
#define TX_VCID(x)			UPDATE(x, 1, 0)
#define DSI2_DSI_SCRAMBLING_CFG		0x0208
#define SCRAMBLING_SEED(x)		UPDATE(x, 31, 16)
#define SCRAMBLING_EN			BIT(0)
#define DSI2_DSI_VID_TX_CFG		0x020c
#define LPDT_DISPLAY_CMD_EN		BIT(20)
#define BLK_VFP_HS_EN			BIT(14)
#define BLK_VBP_HS_EN			BIT(13)
#define BLK_VSA_HS_EN			BIT(12)
#define BLK_HFP_HS_EN			BIT(6)
#define BLK_HBP_HS_EN			BIT(5)
#define BLK_HSA_HS_EN			BIT(4)
#define VID_MODE_TYPE(x)		UPDATE(x, 1, 0)
#define DSI2_CRI_TX_HDR			0x02c0
#define CMD_TX_MODE(x)			UPDATE(x, 24, 24)
#define DSI2_CRI_TX_PLD			0x02c4
#define DSI2_CRI_RX_HDR			0x02c8
#define DSI2_CRI_RX_PLD			0x02cc

#define DSI2_IPI_COLOR_MAN_CFG		0x0300
#define IPI_DEPTH(x)			UPDATE(x, 7, 4)
#define IPI_DEPTH_5_6_5_BITS		0x02
#define IPI_DEPTH_6_BITS		0x03
#define IPI_DEPTH_8_BITS		0x05
#define IPI_DEPTH_10_BITS		0x06
#define IPI_FORMAT(x)			UPDATE(x, 3, 0)
#define IPI_FORMAT_RGB			0x0
#define IPI_FORMAT_DSC			0x0b
#define DSI2_IPI_VID_HSA_MAN_CFG	0x0304
#define VID_HSA_TIME(x)			UPDATE(x, 29, 0)
#define DSI2_IPI_VID_HBP_MAN_CFG	0x030c
#define VID_HBP_TIME(x)			UPDATE(x, 29, 0)
#define DSI2_IPI_VID_HACT_MAN_CFG	0x0314
#define VID_HACT_TIME(x)		UPDATE(x, 29, 0)
#define DSI2_IPI_VID_HLINE_MAN_CFG	0x031c
#define VID_HLINE_TIME(x)		UPDATE(x, 29, 0)
#define DSI2_IPI_VID_VSA_MAN_CFG	0x0324
#define VID_VSA_LINES(x)		UPDATE(x, 9, 0)
#define DSI2_IPI_VID_VBP_MAN_CFG	0X032C
#define VID_VBP_LINES(x)		UPDATE(x, 9, 0)
#define DSI2_IPI_VID_VACT_MAN_CFG	0X0334
#define VID_VACT_LINES(x)		UPDATE(x, 13, 0)
#define DSI2_IPI_VID_VFP_MAN_CFG	0X033C
#define VID_VFP_LINES(x)		UPDATE(x, 9, 0)
#define DSI2_IPI_PIX_PKT_CFG		0x0344
#define MAX_PIX_PKT(x)			UPDATE(x, 15, 0)

#define DSI2_INT_ST_PHY			0x0400
#define DSI2_INT_MASK_PHY		0x0404
#define DSI2_INT_ST_TO			0x0410
#define DSI2_INT_MASK_TO		0x0414
#define DSI2_INT_ST_ACK			0x0420
#define DSI2_INT_MASK_ACK		0x0424
#define DSI2_INT_ST_IPI			0x0430
#define DSI2_INT_MASK_IPI		0x0434
#define DSI2_INT_ST_FIFO		0x0440
#define DSI2_INT_MASK_FIFO		0x0444
#define DSI2_INT_ST_PRI			0x0450
#define DSI2_INT_MASK_PRI		0x0454
#define DSI2_INT_ST_CRI			0x0460
#define DSI2_INT_MASK_CRI		0x0464
#define DSI2_INT_FORCE_CRI		0x0468
#define DSI2_MAX_REGISGER		DSI2_INT_FORCE_CRI

#define MODE_STATUS_TIMEOUT_US		10000
#define CMD_PKT_STATUS_TIMEOUT_US	20000
#define PSEC_PER_SEC			1000000000000LL

#define GRF_REG_FIELD(reg, lsb, msb)	(((reg) << 16) | ((lsb) << 8) | (msb))

enum vid_mode_type {
	VID_MODE_TYPE_NON_BURST_SYNC_PULSES,
	VID_MODE_TYPE_NON_BURST_SYNC_EVENTS,
	VID_MODE_TYPE_BURST,
};

enum mode_ctrl {
	IDLE_MODE,
	AUTOCALC_MODE,
	COMMAND_MODE,
	VIDEO_MODE,
	DATA_STREAM_MODE,
	VIDE_TEST_MODE,
	DATA_STREAM_TEST_MODE,
};

enum grf_reg_fields {
	TXREQCLKHS_EN,
	GATING_EN,
	IPI_SHUTDN,
	IPI_COLORM,
	IPI_COLOR_DEPTH,
	IPI_FORMAT,
	MAX_FIELDS,
};

enum phy_type {
	DPHY,
	CPHY,
};

enum ppi_width {
	PPI_WIDTH_8_BITS,
	PPI_WIDTH_16_BITS,
	PPI_WIDTH_32_BITS,
};

struct cmd_header {
	u8 cmd_type;
	u8 delay;
	u8 payload_length;
};

struct dw_mipi_dsi2_plat_data {
	const u32 *dsi0_grf_reg_fields;
	const u32 *dsi1_grf_reg_fields;
	unsigned long long dphy_max_bit_rate_per_lane;
	unsigned long long cphy_max_symbol_rate_per_lane;

};

struct dw_mipi_dsi2 {
	struct drm_device *drm_dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_bridge *bridge;
	struct mipi_dsi_host host;
	struct drm_panel *panel;
	struct drm_display_mode mode;
	struct device *dev;
	struct device_node *client;
	struct regmap *grf;
	struct clk *pclk;
	struct clk *sys_clk;
	bool phy_enabled;
	struct phy *dcphy;
	union phy_configure_opts phy_opts;

	bool c_option;
	bool scrambling_en;
	unsigned int slice_width;
	unsigned int slice_height;
	bool dsc_enable;
	u8 version_major;
	u8 version_minor;

	struct drm_dsc_picture_parameter_set *pps;
	struct regmap *regmap;
	struct reset_control *apb_rst;
	int irq;
	int id;

	/* dual-channel */
	struct dw_mipi_dsi2 *master;
	struct dw_mipi_dsi2 *slave;
	bool data_swap;

	unsigned int lane_hs_rate; /* Mbps or Msps per lane */
	u32 channel;
	u32 lanes;
	u32 format;
	unsigned long mode_flags;

	const struct dw_mipi_dsi2_plat_data *pdata;
	struct rockchip_drm_sub_dev sub_dev;

	struct gpio_desc *te_gpio;

	/* split with other display interface */
	bool dual_connector_split;
	bool left_display;
	u32 split_area;
};

static inline struct dw_mipi_dsi2 *host_to_dsi2(struct mipi_dsi_host *host)
{
	return container_of(host, struct dw_mipi_dsi2, host);
}

static inline struct dw_mipi_dsi2 *con_to_dsi2(struct drm_connector *con)
{
	return container_of(con, struct dw_mipi_dsi2, connector);
}

static inline struct dw_mipi_dsi2 *encoder_to_dsi2(struct drm_encoder *encoder)
{
	return container_of(encoder, struct dw_mipi_dsi2, encoder);
}

static void grf_field_write(struct dw_mipi_dsi2 *dsi2, enum grf_reg_fields index,
			    unsigned int val)
{
	const u32 field = dsi2->id ?
			  dsi2->pdata->dsi1_grf_reg_fields[index] :
			  dsi2->pdata->dsi0_grf_reg_fields[index];
	u16 reg;
	u8 msb, lsb;

	if (!field)
		return;

	reg = (field >> 16) & 0xffff;
	lsb = (field >>  8) & 0xff;
	msb = (field >>  0) & 0xff;

	regmap_write(dsi2->grf, reg, (val << lsb) | (GENMASK(msb, lsb) << 16));
}

static int cri_fifos_wait_avail(struct dw_mipi_dsi2 *dsi2)
{
	u32 sts, mask;
	int ret;

	mask = CRI_BUSY | CRT_FIFOS_NOT_EMPTY;
	ret = regmap_read_poll_timeout(dsi2->regmap, DSI2_CORE_STATUS, sts,
				       !(sts & mask), 0,
				       CMD_PKT_STATUS_TIMEOUT_US);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi2->dev, "command interface is busy\n");
		return ret;
	}

	return 0;
}

static void dw_mipi_dsi2_irq_enable(struct dw_mipi_dsi2 *dsi2, bool enable)
{
	if (enable) {
		regmap_write(dsi2->regmap, DSI2_INT_MASK_PHY, 0x1);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_TO, 0xf);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_ACK, 0x1);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_IPI, 0x1);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_FIFO, 0x1);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_PRI, 0x1);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_CRI, 0x1);
	} else {
		regmap_write(dsi2->regmap, DSI2_INT_MASK_PHY, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_TO, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_ACK, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_IPI, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_FIFO, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_PRI, 0x0);
		regmap_write(dsi2->regmap, DSI2_INT_MASK_CRI, 0x0);
	};
}

static void mipi_dcphy_power_on(struct dw_mipi_dsi2 *dsi2)
{
	if (dsi2->phy_enabled)
		return;

	if (dsi2->dcphy)
		phy_power_on(dsi2->dcphy);

	dsi2->phy_enabled = true;
}

static void mipi_dcphy_power_off(struct dw_mipi_dsi2 *dsi2)
{
	if (!dsi2->phy_enabled)
		return;

	if (dsi2->dcphy)
		phy_power_off(dsi2->dcphy);

	dsi2->phy_enabled = false;
}

static void dw_mipi_dsi2_set_vid_mode(struct dw_mipi_dsi2 *dsi2)
{
	u32 val = 0, mode;
	int ret;

	if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_HFP)
		val |= BLK_HFP_HS_EN;

	if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_HBP)
		val |= BLK_HBP_HS_EN;

	if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_HSA)
		val |= BLK_HSA_HS_EN;

	if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	regmap_write(dsi2->regmap, DSI2_DSI_VID_TX_CFG, val);

	regmap_write(dsi2->regmap, DSI2_MODE_CTRL, VIDEO_MODE);
	ret = regmap_read_poll_timeout(dsi2->regmap, DSI2_MODE_STATUS,
				       mode, mode & VIDEO_MODE,
				       1000, MODE_STATUS_TIMEOUT_US);
	if (ret < 0)
		dev_err(dsi2->dev, "failed to enter video mode\n");
}

static void dw_mipi_dsi2_set_data_stream_mode(struct dw_mipi_dsi2 *dsi2)
{
	u32 mode;
	int ret;

	regmap_write(dsi2->regmap, DSI2_MODE_CTRL, DATA_STREAM_MODE);
	ret = regmap_read_poll_timeout(dsi2->regmap, DSI2_MODE_STATUS,
				       mode, mode & DATA_STREAM_MODE,
				       1000, MODE_STATUS_TIMEOUT_US);
	if (ret < 0)
		dev_err(dsi2->dev, "failed to enter data stream mode\n");
}

static void dw_mipi_dsi2_set_cmd_mode(struct dw_mipi_dsi2 *dsi2)
{
	u32 mode;
	int ret;

	regmap_write(dsi2->regmap, DSI2_MODE_CTRL, COMMAND_MODE);
	ret = regmap_read_poll_timeout(dsi2->regmap, DSI2_MODE_STATUS,
				       mode, mode & COMMAND_MODE,
				       1000, MODE_STATUS_TIMEOUT_US);
	if (ret < 0)
		dev_err(dsi2->dev, "failed to enter data stream mode\n");
}

static void dw_mipi_dsi2_disable(struct dw_mipi_dsi2 *dsi2)
{
	regmap_write(dsi2->regmap, DSI2_IPI_PIX_PKT_CFG, 0);
	dw_mipi_dsi2_set_cmd_mode(dsi2);

	if (dsi2->slave)
		dw_mipi_dsi2_disable(dsi2->slave);
}

static void dw_mipi_dsi2_post_disable(struct dw_mipi_dsi2 *dsi2)
{
	dw_mipi_dsi2_irq_enable(dsi2, 0);
	regmap_write(dsi2->regmap, DSI2_PWR_UP, RESET);
	mipi_dcphy_power_off(dsi2);
	pm_runtime_put(dsi2->dev);

	if (dsi2->slave)
		dw_mipi_dsi2_post_disable(dsi2->slave);
}

static void dw_mipi_dsi2_encoder_atomic_disable(struct drm_encoder *encoder,
						struct drm_atomic_state *state)
{
	struct dw_mipi_dsi2 *dsi2 = encoder_to_dsi2(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc->state);

	if (dsi2->panel)
		drm_panel_disable(dsi2->panel);

	if (!(dsi2->mode_flags & MIPI_DSI_MODE_VIDEO))
		rockchip_drm_crtc_standby(encoder->crtc, 1);

	dw_mipi_dsi2_disable(dsi2);

	if (!(dsi2->mode_flags & MIPI_DSI_MODE_VIDEO))
		rockchip_drm_crtc_standby(encoder->crtc, 0);

	if (dsi2->panel)
		drm_panel_unprepare(dsi2->panel);

	dw_mipi_dsi2_post_disable(dsi2);

	if (!crtc->state->active_changed)
		return;

	if (dsi2->slave)
		s->output_if &= ~(VOP_OUTPUT_IF_MIPI1 | VOP_OUTPUT_IF_MIPI0);
	else
		s->output_if &= ~(dsi2->id ? VOP_OUTPUT_IF_MIPI1 : VOP_OUTPUT_IF_MIPI0);
}

static void dw_mipi_dsi2_get_lane_rate(struct dw_mipi_dsi2 *dsi2)
{
	struct device *dev = dsi2->dev;
	const struct drm_display_mode *mode = &dsi2->mode;
	u64 max_lane_rate;
	u64 lane_rate, target_pclk;
	u32 value;
	int bpp, lanes;
	u64 tmp;

	max_lane_rate = (dsi2->c_option) ?
			 dsi2->pdata->cphy_max_symbol_rate_per_lane :
			 dsi2->pdata->dphy_max_bit_rate_per_lane;

	lanes = (dsi2->slave || dsi2->master) ? dsi2->lanes * 2 : dsi2->lanes;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi2->format);
	if (bpp < 0)
		bpp = 24;

	/*
	 * optional override of the desired bandwidth
	 * High-Speed mode: Differential and terminated: 80Mbps ~ 4500 Mbps.
	 */
	if (!of_property_read_u32(dev->of_node, "rockchip,lane-rate", &value)) {
		if (value >= 80000 && value <= 4500000)
			lane_rate = value * MSEC_PER_SEC;
		else if (value >= 80 && value <= 4500)
			lane_rate = value * USEC_PER_SEC;
		else
			lane_rate = 80 * USEC_PER_SEC;
	} else {
		tmp = (u64)mode->crtc_clock * 1000 * bpp;
		do_div(tmp, lanes);

		/*
		 * Multiple bits are encoded into each symbol epoch,
		 * the data rate is ~2.28x the symbol rate.
		 */
		if (dsi2->c_option)
			tmp = DIV_ROUND_CLOSEST_ULL(tmp * 100, 228);

		/* set BW a little larger only in video burst mode in
		 * consideration of the protocol overhead and HS mode
		 * switching to BLLP mode, take 1 / 0.9, since Mbps must
		 * big than bandwidth of RGB
		 */
		if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
			tmp *= 10;
			do_div(tmp, 9);
		}

		if (tmp > max_lane_rate)
			lane_rate = max_lane_rate;
		else
			lane_rate = tmp;
	}

	target_pclk = DIV_ROUND_CLOSEST_ULL(lane_rate * lanes, bpp);
	phy_mipi_dphy_get_default_config(target_pclk, bpp, lanes,
					 &dsi2->phy_opts.mipi_dphy);
	if (dsi2->slave)
		phy_mipi_dphy_get_default_config(target_pclk, bpp, lanes,
						 &dsi2->slave->phy_opts.mipi_dphy);
}

static void dw_mipi_dsi2_set_lane_rate(struct dw_mipi_dsi2 *dsi2)
{
	unsigned long hs_clk_rate;

	if (dsi2->dcphy)
		if (!dsi2->c_option)
			phy_set_mode(dsi2->dcphy, PHY_MODE_MIPI_DPHY);

	phy_configure(dsi2->dcphy, &dsi2->phy_opts);
	hs_clk_rate = dsi2->phy_opts.mipi_dphy.hs_clk_rate;
	dsi2->lane_hs_rate = DIV_ROUND_UP(hs_clk_rate, MSEC_PER_SEC);
}

static void dw_mipi_dsi2_host_softrst(struct dw_mipi_dsi2 *dsi2)
{
	if (dsi2->apb_rst) {
		reset_control_assert(dsi2->apb_rst);
		usleep_range(10, 20);
		reset_control_deassert(dsi2->apb_rst);
	}

	regmap_write(dsi2->regmap, DSI2_SOFT_RESET, 0x0);
	udelay(100);
	regmap_write(dsi2->regmap, DSI2_SOFT_RESET,
		     SYS_RSTN | PHY_RSTN | IPI_RSTN);

}

static void dw_mipi_dsi2_phy_mode_cfg(struct dw_mipi_dsi2 *dsi2)
{
	u32 val = 0;

	/* PPI width is fixed to 16 bits in DCPHY */
	val |= PPI_WIDTH(PPI_WIDTH_16_BITS) | PHY_LANES(dsi2->lanes);
	val |= PHY_TYPE(dsi2->c_option ? CPHY : DPHY);
	regmap_write(dsi2->regmap, DSI2_PHY_MODE_CFG, val);
}

static void dw_mipi_dsi2_phy_clk_mode_cfg(struct dw_mipi_dsi2 *dsi2)
{
	u32 sys_clk, esc_clk_div;
	u32 val = 0;

	/*
	 * clk_type should be NON_CONTINUOUS_CLK before
	 * initial deskew calibration be sent.
	 */
	val |= NON_CONTINUOUS_CLK;

	/* The maximum value of the escape clock frequency is 20MHz */
	sys_clk = clk_get_rate(dsi2->sys_clk) / USEC_PER_SEC;
	esc_clk_div = DIV_ROUND_UP(sys_clk, 20 * 2);
	val |= PHY_LPTX_CLK_DIV(esc_clk_div);

	regmap_write(dsi2->regmap, DSI2_PHY_CLK_CFG, val);
}

static void dw_mipi_dsi2_phy_ratio_cfg(struct dw_mipi_dsi2 *dsi2)
{
	struct drm_display_mode *mode = &dsi2->mode;
	u64 sys_clk = clk_get_rate(dsi2->sys_clk);
	u64 pixel_clk, ipi_clk, phy_hsclk;
	u64 tmp;

	/*
	 * in DPHY mode, the phy_hstx_clk is exactly 1/16 the Lane high-speed
	 * data rate; In CPHY mode, the phy_hstx_clk is exactly 1/7 the trio
	 * high speed symbol rate.
	 */
	if (dsi2->c_option)
		phy_hsclk = DIV_ROUND_CLOSEST_ULL(dsi2->lane_hs_rate * MSEC_PER_SEC, 7);
	else
		phy_hsclk = DIV_ROUND_CLOSEST_ULL(dsi2->lane_hs_rate * MSEC_PER_SEC, 16);

	/* IPI_RATIO_MAN_CFG = PHY_HSTX_CLK / IPI_CLK */
	pixel_clk = mode->crtc_clock * MSEC_PER_SEC;
	ipi_clk = pixel_clk / 4;

	tmp = DIV_ROUND_CLOSEST_ULL(phy_hsclk << 16, ipi_clk);
	regmap_write(dsi2->regmap, DSI2_PHY_IPI_RATIO_MAN_CFG,
		     PHY_IPI_RATIO(tmp));

	/*
	 * SYS_RATIO_MAN_CFG = MIPI_DCPHY_HSCLK_Freq / MIPI_DCPHY_HSCLK_Freq
	 */
	tmp = DIV_ROUND_CLOSEST_ULL(phy_hsclk << 16, sys_clk);
	regmap_write(dsi2->regmap, DSI2_PHY_SYS_RATIO_MAN_CFG,
		     PHY_SYS_RATIO(tmp));
}

static void dw_mipi_dsi2_lp2hs_or_hs2lp_cfg(struct dw_mipi_dsi2 *dsi2)
{
	struct phy_configure_opts_mipi_dphy *cfg = &dsi2->phy_opts.mipi_dphy;
	unsigned long long tmp, ui;
	unsigned long long hstx_clk;

	hstx_clk = DIV_ROUND_CLOSEST_ULL(dsi2->lane_hs_rate * MSEC_PER_SEC, 16);

	ui = ALIGN(PSEC_PER_SEC, hstx_clk);
	do_div(ui, hstx_clk);

	/* PHY_LP2HS_TIME = (TLPX + THS-PREPARE + THS-ZERO) / Tphy_hstx_clk */
	tmp = cfg->lpx + cfg->hs_prepare + cfg->hs_zero;
	tmp = DIV_ROUND_CLOSEST_ULL(tmp << 16, ui);
	regmap_write(dsi2->regmap, DSI2_PHY_LP2HS_MAN_CFG, PHY_LP2HS_TIME(tmp));

	/* PHY_HS2LP_TIME = (THS-TRAIL + THS-EXIT) / Tphy_hstx_clk */
	tmp = cfg->hs_trail + cfg->hs_exit;
	tmp = DIV_ROUND_CLOSEST_ULL(tmp << 16, ui);
	regmap_write(dsi2->regmap, DSI2_PHY_HS2LP_MAN_CFG, PHY_HS2LP_TIME(tmp));
}

static void dw_mipi_dsi2_phy_init(struct dw_mipi_dsi2 *dsi2)
{
	dw_mipi_dsi2_phy_mode_cfg(dsi2);
	dw_mipi_dsi2_phy_clk_mode_cfg(dsi2);
	dw_mipi_dsi2_phy_ratio_cfg(dsi2);
	dw_mipi_dsi2_lp2hs_or_hs2lp_cfg(dsi2);

	/* phy configuration 8 - 10 */
}

static void dw_mipi_dsi2_tx_option_set(struct dw_mipi_dsi2 *dsi2)
{
	u32 val;

	val = BTA_EN | EOTP_TX_EN;

	if (dsi2->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	regmap_write(dsi2->regmap, DSI2_DSI_GENERAL_CFG, val);
	regmap_write(dsi2->regmap, DSI2_DSI_VCID_CFG, TX_VCID(dsi2->channel));

	if (dsi2->scrambling_en)
		regmap_write(dsi2->regmap, DSI2_DSI_SCRAMBLING_CFG,
			     SCRAMBLING_EN);
}

static void dw_mipi_dsi2_ipi_color_coding_cfg(struct dw_mipi_dsi2 *dsi2)
{
	u32 val, color_depth;

	switch (dsi2->format) {
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		color_depth = IPI_DEPTH_6_BITS;
		break;
	case MIPI_DSI_FMT_RGB565:
		color_depth = IPI_DEPTH_5_6_5_BITS;
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		color_depth = IPI_DEPTH_8_BITS;
		break;
	}

	val = IPI_DEPTH(color_depth) |
	      IPI_FORMAT(dsi2->dsc_enable ? IPI_FORMAT_DSC : IPI_FORMAT_RGB);
	regmap_write(dsi2->regmap, DSI2_IPI_COLOR_MAN_CFG, val);
	grf_field_write(dsi2, IPI_COLOR_DEPTH, color_depth);

	if (dsi2->dsc_enable)
		grf_field_write(dsi2, IPI_FORMAT, IPI_FORMAT_DSC);
}

static void dw_mipi_dsi2_ipi_set(struct dw_mipi_dsi2 *dsi2)
{
	struct drm_display_mode *mode = &dsi2->mode;
	u32 hline, hsa, hbp, hact;
	u64 hline_time, hsa_time, hbp_time, hact_time, tmp;
	u64 pixel_clk, phy_hs_clk;
	u32 vact, vsa, vfp, vbp;
	u16 val;

	if (dsi2->slave || dsi2->master)
		val = mode->hdisplay / 2;
	else
		val = mode->hdisplay;

	regmap_write(dsi2->regmap, DSI2_IPI_PIX_PKT_CFG, MAX_PIX_PKT(val));

	dw_mipi_dsi2_ipi_color_coding_cfg(dsi2);

	/*
	 * if the controller is intended to operate in data stream mode,
	 * no more steps are required.
	 */
	if (!(dsi2->mode_flags & MIPI_DSI_MODE_VIDEO))
		return;

	vact = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hact = mode->hdisplay;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	hline = mode->htotal;

	pixel_clk = mode->crtc_clock * MSEC_PER_SEC;

	if (dsi2->c_option)
		phy_hs_clk = DIV_ROUND_CLOSEST_ULL(dsi2->lane_hs_rate * MSEC_PER_SEC, 7);
	else
		phy_hs_clk = DIV_ROUND_CLOSEST_ULL(dsi2->lane_hs_rate * MSEC_PER_SEC, 16);

	tmp = hsa * phy_hs_clk;
	hsa_time = DIV_ROUND_CLOSEST_ULL(tmp << 16, pixel_clk);
	regmap_write(dsi2->regmap, DSI2_IPI_VID_HSA_MAN_CFG,
		     VID_HSA_TIME(hsa_time));

	tmp = hbp * phy_hs_clk;
	hbp_time = DIV_ROUND_CLOSEST_ULL(tmp << 16, pixel_clk);
	regmap_write(dsi2->regmap, DSI2_IPI_VID_HBP_MAN_CFG,
		     VID_HBP_TIME(hbp_time));

	tmp = hact * phy_hs_clk;
	hact_time = DIV_ROUND_CLOSEST_ULL(tmp << 16, pixel_clk);
	regmap_write(dsi2->regmap, DSI2_IPI_VID_HACT_MAN_CFG,
		     VID_HACT_TIME(hact_time));

	tmp = hline * phy_hs_clk;
	hline_time = DIV_ROUND_CLOSEST_ULL(tmp << 16, pixel_clk);
	regmap_write(dsi2->regmap, DSI2_IPI_VID_HLINE_MAN_CFG,
		     VID_HLINE_TIME(hline_time));

	regmap_write(dsi2->regmap, DSI2_IPI_VID_VSA_MAN_CFG,
		     VID_VSA_LINES(vsa));
	regmap_write(dsi2->regmap, DSI2_IPI_VID_VBP_MAN_CFG,
		     VID_VBP_LINES(vbp));
	regmap_write(dsi2->regmap, DSI2_IPI_VID_VACT_MAN_CFG,
		     VID_VACT_LINES(vact));
	regmap_write(dsi2->regmap, DSI2_IPI_VID_VFP_MAN_CFG,
		     VID_VFP_LINES(vfp));
}

static void
dw_mipi_dsi2_work_mode(struct dw_mipi_dsi2 *dsi2, u32 mode)
{
	/*
	 * select controller work in Manual mode
	 * Manual: MANUAL_MODE_EN
	 * Automatic: 0
	 */
	regmap_write(dsi2->regmap, MANUAL_MODE_CFG, mode);
}

static void dw_mipi_dsi2_pre_enable(struct dw_mipi_dsi2 *dsi2)
{
	pm_runtime_get_sync(dsi2->dev);

	dw_mipi_dsi2_host_softrst(dsi2);
	regmap_write(dsi2->regmap, DSI2_PWR_UP, RESET);

	/* there may be some timeout registers may be configured if desired */

	dw_mipi_dsi2_work_mode(dsi2, MANUAL_MODE_EN);
	dw_mipi_dsi2_phy_init(dsi2);
	dw_mipi_dsi2_tx_option_set(dsi2);
	dw_mipi_dsi2_irq_enable(dsi2, 1);
	mipi_dcphy_power_on(dsi2);

	/*
	 * initial deskew calibration is send after phy_power_on,
	 * then we can configure clk_type.
	 */
	if (!(dsi2->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		regmap_update_bits(dsi2->regmap, DSI2_PHY_CLK_CFG,
				   CLK_TYPE_MASK, CONTIUOUS_CLK);

	regmap_write(dsi2->regmap, DSI2_PWR_UP, POWER_UP);
	dw_mipi_dsi2_set_cmd_mode(dsi2);

	if (dsi2->slave)
		dw_mipi_dsi2_pre_enable(dsi2->slave);
}

static void dw_mipi_dsi2_enable(struct dw_mipi_dsi2 *dsi2)
{
	dw_mipi_dsi2_ipi_set(dsi2);

	if (dsi2->mode_flags & MIPI_DSI_MODE_VIDEO)
		dw_mipi_dsi2_set_vid_mode(dsi2);
	else
		dw_mipi_dsi2_set_data_stream_mode(dsi2);

	if (dsi2->slave)
		dw_mipi_dsi2_enable(dsi2->slave);
}

static int dw_mipi_dsi2_encoder_mode_set(struct dw_mipi_dsi2 *dsi2,
					 struct drm_atomic_state *state)
{
	struct drm_encoder *encoder = &dsi2->encoder;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *adjusted_mode;
	struct drm_display_mode *mode = &dsi2->mode;

	connector = drm_atomic_get_new_connector_for_encoder(state, encoder);
	if (!connector)
		return -ENODEV;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state)
		return -ENODEV;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (!crtc_state) {
		dev_err(dsi2->dev, "failed to get crtc state\n");
		return -ENODEV;
	}

	adjusted_mode = &crtc_state->adjusted_mode;
	drm_mode_copy(mode, adjusted_mode);

	if (dsi2->dual_connector_split)
		drm_mode_convert_to_origin_mode(mode);

	if (dsi2->slave)
		drm_mode_copy(&dsi2->slave->mode, mode);

	return 0;
}

static void dw_mipi_dsi2_encoder_atomic_enable(struct drm_encoder *encoder,
					       struct drm_atomic_state *state)
{
	struct dw_mipi_dsi2 *dsi2 = encoder_to_dsi2(encoder);
	int ret;

	ret = dw_mipi_dsi2_encoder_mode_set(dsi2, state);
	if (ret) {
		dev_err(dsi2->dev, "failed to set dsi2 mode\n");
		return;
	}

	dw_mipi_dsi2_get_lane_rate(dsi2);

	if (dsi2->dcphy)
		dw_mipi_dsi2_set_lane_rate(dsi2);

	if (dsi2->slave && dsi2->slave->dcphy)
		dw_mipi_dsi2_set_lane_rate(dsi2->slave);

	dw_mipi_dsi2_pre_enable(dsi2);

	if (dsi2->panel)
		drm_panel_prepare(dsi2->panel);

	dw_mipi_dsi2_enable(dsi2);

	if (dsi2->panel)
		drm_panel_enable(dsi2->panel);

	DRM_DEV_INFO(dsi2->dev, "final DSI-Link bandwidth: %u x %d %s\n",
		     dsi2->lane_hs_rate,
		     dsi2->slave ? dsi2->lanes * 2 : dsi2->lanes,
		     dsi2->c_option ? "Ksps" : "Kbps");
}

static int
dw_mipi_dsi2_encoder_atomic_check(struct drm_encoder *encoder,
				  struct drm_crtc_state *crtc_state,
				  struct drm_connector_state *conn_state)
{

	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct dw_mipi_dsi2 *dsi2 = encoder_to_dsi2(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	switch (dsi2->format) {
	case MIPI_DSI_FMT_RGB888:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	case MIPI_DSI_FMT_RGB666:
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
		break;
	case MIPI_DSI_FMT_RGB565:
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	s->output_type = DRM_MODE_CONNECTOR_DSI;
	s->output_if |= dsi2->id ? VOP_OUTPUT_IF_MIPI1 : VOP_OUTPUT_IF_MIPI0;
	s->bus_flags = info->bus_flags;

	s->tv_state = &conn_state->tv;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	if (!(dsi2->mode_flags & MIPI_DSI_MODE_VIDEO)) {
		s->output_flags |= ROCKCHIP_OUTPUT_MIPI_DS_MODE;
		s->soft_te = dsi2->te_gpio ? true : false;
		s->hold_mode = true;
	}

	if (dsi2->slave) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		if (dsi2->data_swap)
			s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;

		s->output_if |= VOP_OUTPUT_IF_MIPI1;
	}

	if (dsi2->dual_connector_split) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CONNECTOR_SPLIT_MODE;

		if (dsi2->left_display)
			s->output_if_left_panel |= dsi2->id ?
						   VOP_OUTPUT_IF_MIPI1 :
						   VOP_OUTPUT_IF_MIPI0;
	}

	if (dsi2->dsc_enable) {
		s->dsc_enable = 1;
		s->dsc_sink_cap.version_major = dsi2->version_major;
		s->dsc_sink_cap.version_minor = dsi2->version_minor;
		s->dsc_sink_cap.slice_width = dsi2->slice_width;
		s->dsc_sink_cap.slice_height = dsi2->slice_height;
		/* only can support rgb888 panel now */
		s->dsc_sink_cap.target_bits_per_pixel_x16 = 8 << 4;
		s->dsc_sink_cap.native_420 = 0;

		memcpy(&s->pps, dsi2->pps, sizeof(struct drm_dsc_picture_parameter_set));
	}

	return 0;
}

static void dw_mipi_dsi2_loader_protect(struct dw_mipi_dsi2 *dsi2, bool on)
{
	if (on) {
		pm_runtime_get_sync(dsi2->dev);
		phy_init(dsi2->dcphy);
		dsi2->phy_enabled = true;
		if (dsi2->dcphy)
			dsi2->dcphy->power_count++;
	} else {
		pm_runtime_put(dsi2->dev);
		phy_exit(dsi2->dcphy);
		dsi2->phy_enabled = false;
		if (dsi2->dcphy)
			dsi2->dcphy->power_count--;
	}

	if (dsi2->slave)
		dw_mipi_dsi2_loader_protect(dsi2->slave, on);
}

static int dw_mipi_dsi2_encoder_loader_protect(struct drm_encoder *encoder,
					      bool on)
{
	struct dw_mipi_dsi2 *dsi2 = encoder_to_dsi2(encoder);

	if (dsi2->panel)
		panel_simple_loader_protect(dsi2->panel);

	dw_mipi_dsi2_loader_protect(dsi2, on);

	return 0;
}

static const struct drm_encoder_helper_funcs
dw_mipi_dsi2_encoder_helper_funcs = {
	.atomic_enable = dw_mipi_dsi2_encoder_atomic_enable,
	.atomic_disable = dw_mipi_dsi2_encoder_atomic_disable,
	.atomic_check = dw_mipi_dsi2_encoder_atomic_check,
};

static int dw_mipi_dsi2_connector_get_modes(struct drm_connector *connector)
{
	struct dw_mipi_dsi2 *dsi2 = con_to_dsi2(connector);

	if (dsi2->bridge && (dsi2->bridge->ops & DRM_BRIDGE_OP_MODES))
		return drm_bridge_get_modes(dsi2->bridge, connector);

	if (dsi2->panel)
		return drm_panel_get_modes(dsi2->panel, connector);

	return -EINVAL;
}

static enum drm_mode_status
dw_mipi_dsi2_connector_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	struct dw_mipi_dsi2 *dsi2 = con_to_dsi2(connector);
	struct videomode vm;
	u8 min_pixels = dsi2->slave ? 8 : 4;

	drm_display_mode_to_videomode(mode, &vm);

	if (vm.vactive > 16383)
		return MODE_VIRTUAL_Y;

	if (vm.vsync_len > 1023)
		return MODE_VSYNC_WIDE;

	if (vm.vback_porch > 1023 || vm.vfront_porch > 1023)
		return MODE_VBLANK_WIDE;

	/*
	 * the minimum region size (HSA,HBP,HACT,HFP) is 4 pixels
	 * which is the ip known issues and limitations.
	 */
	if (!(vm.hsync_len < min_pixels || vm.hback_porch < min_pixels ||
	    vm.hfront_porch < min_pixels || vm.hactive < min_pixels))
		return MODE_OK;

	if (vm.hsync_len < min_pixels)
		vm.hsync_len = min_pixels;

	if (vm.hback_porch < min_pixels)
		vm.hback_porch = min_pixels;

	if (vm.hfront_porch < min_pixels)
		vm.hfront_porch = min_pixels;

	if (vm.hactive < min_pixels)
		vm.hactive = min_pixels;

	drm_display_mode_from_videomode(&vm, mode);

	return MODE_OK;
}

static struct drm_connector_helper_funcs dw_mipi_dsi2_connector_helper_funcs = {
	.get_modes = dw_mipi_dsi2_connector_get_modes,
	.mode_valid = dw_mipi_dsi2_connector_mode_valid,
};

static enum drm_connector_status
dw_mipi_dsi2_connector_detect(struct drm_connector *connector, bool force)
{
	struct dw_mipi_dsi2 *dsi2 = con_to_dsi2(connector);

	if (dsi2->bridge && (dsi2->bridge->ops & DRM_BRIDGE_OP_DETECT))
		return drm_bridge_detect(dsi2->bridge);

	return connector_status_connected;
}

static void dw_mipi_dsi2_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int
dw_mipi_dsi2_atomic_connector_get_property(struct drm_connector *connector,
					   const struct drm_connector_state *state,
					   struct drm_property *property,
					   uint64_t *val)
{
	struct rockchip_drm_private *private = connector->dev->dev_private;
	struct dw_mipi_dsi2 *dsi2 = con_to_dsi2(connector);

	if (property == private->split_area_prop) {
		switch (dsi2->split_area) {
		case 1:
			*val = ROCKCHIP_DRM_SPLIT_LEFT_SIDE;
			break;
		case 2:
			*val = ROCKCHIP_DRM_SPLIT_RIGHT_SIDE;
			break;
		default:
			*val = ROCKCHIP_DRM_SPLIT_UNSET;
			break;
		}
	}

	return 0;
}

static const struct drm_connector_funcs dw_mipi_dsi2_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = dw_mipi_dsi2_connector_detect,
	.destroy = dw_mipi_dsi2_drm_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = dw_mipi_dsi2_atomic_connector_get_property,
};

static int dw_mipi_dsi2_dual_channel_probe(struct dw_mipi_dsi2 *dsi2)
{
	struct device_node *np;
	struct platform_device *secondary;

	np = of_parse_phandle(dsi2->dev->of_node, "rockchip,dual-channel", 0);
	if (np) {
		dsi2->data_swap = of_property_read_bool(dsi2->dev->of_node,
						       "rockchip,data-swap");
		secondary = of_find_device_by_node(np);
		dsi2->slave = platform_get_drvdata(secondary);
		of_node_put(np);

		if (!dsi2->slave)
			return -EPROBE_DEFER;

		dsi2->slave->master = dsi2;
		dsi2->lanes /= 2;

		dsi2->slave->lanes = dsi2->lanes;
		dsi2->slave->channel = dsi2->channel;
		dsi2->slave->format = dsi2->format;
		dsi2->slave->mode_flags = dsi2->mode_flags;
	}

	return 0;
}

static irqreturn_t dw_mipi_dsi2_te_irq_handler(int irq, void *dev_id)
{
	struct dw_mipi_dsi2 *dsi2 = (struct dw_mipi_dsi2 *)dev_id;
	struct drm_encoder *encoder = &dsi2->encoder;

	if (encoder->crtc)
		rockchip_drm_te_handle(encoder->crtc);

	return IRQ_HANDLED;
}

static int dw_mipi_dsi2_get_dsc_params_from_sink(struct dw_mipi_dsi2 *dsi2,
						 struct drm_panel *panel,
						 struct drm_bridge *bridge)
{
	struct drm_dsc_picture_parameter_set *pps = NULL;
	struct device_node *np = NULL;
	struct cmd_header *header;
	const void *data;
	char *d;
	uint8_t *dsc_packed_pps;
	int len;

	if (!panel && !bridge)
		return -ENODEV;

	if (panel)
		np = panel->dev->of_node;
	else
		np = bridge->of_node;

	dsi2->c_option = of_property_read_bool(np, "phy-c-option");
	dsi2->scrambling_en = of_property_read_bool(np, "scrambling-enable");
	dsi2->dsc_enable = of_property_read_bool(np, "compressed-data");

	if (dsi2->slave) {
		dsi2->slave->c_option = dsi2->c_option;
		dsi2->slave->scrambling_en = dsi2->scrambling_en;
		dsi2->slave->dsc_enable = dsi2->dsc_enable;
	}

	of_property_read_u32(np, "slice-width", &dsi2->slice_width);
	of_property_read_u32(np, "slice-height", &dsi2->slice_height);
	of_property_read_u8(np, "version-major", &dsi2->version_major);
	of_property_read_u8(np, "version-minor", &dsi2->version_minor);

	data = of_get_property(np, "panel-init-sequence", &len);
	if (!data)
		return -EINVAL;

	d = devm_kmemdup(dsi2->dev, data, len, GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	while (len > sizeof(*header)) {
		header = (struct cmd_header *)d;
		d += sizeof(*header);
		len -= sizeof(*header);

		if (header->payload_length > len)
			return -EINVAL;

		if (header->cmd_type == MIPI_DSI_PICTURE_PARAMETER_SET) {
			dsc_packed_pps = devm_kmemdup(dsi2->dev, d,
						      header->payload_length, GFP_KERNEL);
			if (!dsc_packed_pps)
				return -ENOMEM;

			pps = (struct drm_dsc_picture_parameter_set *)dsc_packed_pps;
			break;
		}

		d += header->payload_length;
		len -= header->payload_length;
	}

	dsi2->pps = pps;

	return 0;
}

static int dw_mipi_dsi2_connector_init(struct dw_mipi_dsi2 *dsi2)
{
	struct drm_encoder *encoder = &dsi2->encoder;
	struct drm_connector *connector = &dsi2->connector;
	struct drm_device *drm_dev = dsi2->drm_dev;
	struct device *dev = dsi2->dev;
	int ret;

	ret = drm_connector_init(drm_dev, connector,
				 &dw_mipi_dsi2_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &dw_mipi_dsi2_connector_helper_funcs);
	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to attach encoder: %d\n", ret);
		goto connector_cleanup;
	}

	return 0;

connector_cleanup:
	connector->funcs->destroy(connector);

	return ret;
}

static int dw_mipi_dsi2_register_sub_dev(struct dw_mipi_dsi2 *dsi2,
					 struct drm_connector *connector)
{
	struct rockchip_drm_private *private;
	struct device *dev = dsi2->dev;

	private = connector->dev->dev_private;

	if (dsi2->split_area)
		drm_object_attach_property(&connector->base,
					   private->split_area_prop,
					   dsi2->split_area);

	dsi2->sub_dev.connector = connector;
	dsi2->sub_dev.of_node = dev->of_node;
	dsi2->sub_dev.loader_protect = dw_mipi_dsi2_encoder_loader_protect;
	rockchip_drm_register_sub_dev(&dsi2->sub_dev);

	return 0;
}

static int dw_mipi_dsi2_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct dw_mipi_dsi2 *dsi2 = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder = &dsi2->encoder;
	struct device_node *of_node = dsi2->dev->of_node;
	struct drm_connector *connector = NULL;
	enum drm_bridge_attach_flags flags;
	int ret;

	dsi2->drm_dev = drm_dev;
	ret = dw_mipi_dsi2_dual_channel_probe(dsi2);
	if (ret)
		return ret;

	if (dsi2->master)
		return 0;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &dsi2->panel, &dsi2->bridge);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to find panel or bridge: %d\n", ret);
		return ret;
	}

	dw_mipi_dsi2_get_dsc_params_from_sink(dsi2, dsi2->panel, dsi2->bridge);
	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      of_node);

	ret = drm_simple_encoder_init(drm_dev, encoder, DRM_MODE_ENCODER_DSI);
	if (ret) {
		DRM_ERROR("Failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &dw_mipi_dsi2_encoder_helper_funcs);

	if (dsi2->bridge) {
		struct list_head *connector_list =
			&drm_dev->mode_config.connector_list;

		dsi2->bridge->driver_private = &dsi2->host;
		dsi2->bridge->encoder = encoder;

		flags = dsi2->bridge->ops & DRM_BRIDGE_OP_MODES ?
			DRM_BRIDGE_ATTACH_NO_CONNECTOR : 0;
		ret = drm_bridge_attach(encoder, dsi2->bridge, NULL, flags);
		if (ret) {
			DRM_DEV_ERROR(dev,
				      "Failed to attach bridge: %d\n", ret);
			goto encoder_cleanup;
		}

		if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
			list_for_each_entry(connector, connector_list, head)
				if (drm_connector_has_possible_encoder(connector,
								       encoder))
					break;
	}

	if (dsi2->panel || (dsi2->bridge && (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))) {
		ret = dw_mipi_dsi2_connector_init(dsi2);
		if (ret)
			goto encoder_cleanup;

		connector = &dsi2->connector;
	}

	if (connector) {
		ret = dw_mipi_dsi2_register_sub_dev(dsi2, connector);
		if (ret)
			goto encoder_cleanup;
	}

	pm_runtime_enable(dsi2->dev);
	if (dsi2->slave)
		pm_runtime_enable(dsi2->slave->dev);

	return 0;

encoder_cleanup:
	encoder->funcs->destroy(encoder);

	return ret;
}

static void dw_mipi_dsi2_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct dw_mipi_dsi2 *dsi2 = dev_get_drvdata(dev);

	if (dsi2->sub_dev.connector) {
		rockchip_drm_unregister_sub_dev(&dsi2->sub_dev);

		if (dsi2->connector.funcs)
			dsi2->connector.funcs->destroy(&dsi2->connector);
	}

	pm_runtime_disable(dsi2->dev);
	if (dsi2->slave)
		pm_runtime_disable(dsi2->slave->dev);

	dsi2->encoder.funcs->destroy(&dsi2->encoder);
}

static const struct component_ops dw_mipi_dsi2_ops = {
	.bind	= dw_mipi_dsi2_bind,
	.unbind	= dw_mipi_dsi2_unbind,
};

struct dsi2_irq_data {
	u32 offeset;
	char *irq_src;
};

static const struct dsi2_irq_data dw_mipi_dsi2_irq_data[] = {
	{DSI2_INT_ST_PHY, "int_st_phy"},
	{DSI2_INT_ST_TO, "int_st_to"},
	{DSI2_INT_ST_ACK, "int_st_ack"},
	{DSI2_INT_ST_IPI, "int_st_ipi"},
	{DSI2_INT_ST_FIFO, "int_st_fifo"},
	{DSI2_INT_ST_PRI, "int_st_pri"},
	{DSI2_INT_ST_CRI, "int_st_cri"},
};

static irqreturn_t dw_mipi_dsi2_irq_handler(int irq, void *dev_id)
{

	struct dw_mipi_dsi2 *dsi2 = dev_id;
	u32 int_st;
	unsigned int i;

	regmap_read(dsi2->regmap, INT_ST_MAIN, &int_st);

	for (i = 0; i < ARRAY_SIZE(dw_mipi_dsi2_irq_data); i++)
		if (int_st & BIT(i))
			DRM_DEV_DEBUG(dsi2->dev, "%s\n",
				      dw_mipi_dsi2_irq_data[i].irq_src);

	return IRQ_HANDLED;
}

static const struct regmap_config dw_mipi_dsi2_regmap_config = {
	.name = "host",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
	.max_register = DSI2_MAX_REGISGER,
};

static int dw_mipi_dsi2_host_attach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi2 *dsi2 = host_to_dsi2(host);

	if (dsi2->master)
		return 0;

	if (device->lanes < 1 || device->lanes > 8)
		return -EINVAL;

	dsi2->client = device->dev.of_node;
	dsi2->lanes = device->lanes;
	dsi2->channel = device->channel;
	dsi2->format = device->format;
	dsi2->mode_flags = device->mode_flags;

	return 0;
}

static int dw_mipi_dsi2_host_detach(struct mipi_dsi_host *host,
				   struct mipi_dsi_device *device)
{
	return 0;
}

static int dw_mipi_dsi2_read_from_fifo(struct dw_mipi_dsi2 *dsi2,
				      const struct mipi_dsi_msg *msg)
{
	u8 *payload = msg->rx_buf;
	u8 data_type;
	u16 wc;
	int i, j, ret, len = msg->rx_len;
	unsigned int vrefresh = drm_mode_vrefresh(&dsi2->mode);
	u32 val;

	ret = regmap_read_poll_timeout(dsi2->regmap, DSI2_CORE_STATUS,
				       val, val & CRI_RD_DATA_AVAIL,
				       0, DIV_ROUND_UP(1000000, vrefresh));
	if (ret) {
		DRM_DEV_ERROR(dsi2->dev, "CRI has no available read data\n");
		return ret;
	}

	regmap_read(dsi2->regmap, DSI2_CRI_RX_HDR, &val);
	data_type = val & 0x3f;

	if (mipi_dsi_packet_format_is_short(data_type)) {
		for (i = 0; i < len && i < 2; i++)
			payload[i] = (val >> (8 * (i + 1))) & 0xff;

		return 0;
	}

	wc = (val >> 8) & 0xffff;
	/* Receive payload */
	for (i = 0; i < len && i < wc; i += 4) {
		regmap_read(dsi2->regmap, DSI2_CRI_RX_PLD, &val);
		for (j = 0; j < 4 && j + i < len && j + i < wc; j++)
			payload[i + j] = val >> (8 * j);
	}

	return 0;
}

static ssize_t dw_mipi_dsi2_transfer(struct dw_mipi_dsi2 *dsi2,
				    const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;
	u32 val;
	u32 mode;

	regmap_update_bits(dsi2->regmap, DSI2_DSI_VID_TX_CFG,
			   LPDT_DISPLAY_CMD_EN,
			   msg->flags & MIPI_DSI_MSG_USE_LPM ?
			   LPDT_DISPLAY_CMD_EN : 0);

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		DRM_DEV_ERROR(dsi2->dev, "failed to create packet: %d\n", ret);
		return ret;
	}

	ret = cri_fifos_wait_avail(dsi2);
	if (ret)
		return ret;

	/* Send payload */
	while (DIV_ROUND_UP(packet.payload_length, 4)) {
		/* check cri interface is not busy */
		if (packet.payload_length < 4) {
			/* send residu payload */
			val = 0;
			memcpy(&val, packet.payload, packet.payload_length);
			regmap_write(dsi2->regmap, DSI2_CRI_TX_PLD, val);
			packet.payload_length = 0;
		} else {
			val = get_unaligned_le32(packet.payload);
			regmap_write(dsi2->regmap, DSI2_CRI_TX_PLD, val);
			packet.payload += 4;
			packet.payload_length -= 4;
		}
	}

	/* Send packet header */
	mode = CMD_TX_MODE(msg->flags & MIPI_DSI_MSG_USE_LPM ? 1 : 0);
	val = get_unaligned_le32(packet.header);

	regmap_write(dsi2->regmap, DSI2_CRI_TX_HDR, mode | val);

	ret = cri_fifos_wait_avail(dsi2);
	if (ret)
		return ret;

	if (msg->rx_len) {
		ret = dw_mipi_dsi2_read_from_fifo(dsi2, msg);
		if (ret < 0)
			return ret;
	}

	if (dsi2->slave)
		dw_mipi_dsi2_transfer(dsi2->slave, msg);

	return msg->tx_len;
}

static ssize_t dw_mipi_dsi2_host_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi2 *dsi2 = host_to_dsi2(host);

	return dw_mipi_dsi2_transfer(dsi2, msg);
}

static const struct mipi_dsi_host_ops dw_mipi_dsi2_host_ops = {
	.attach = dw_mipi_dsi2_host_attach,
	.detach = dw_mipi_dsi2_host_detach,
	.transfer = dw_mipi_dsi2_host_transfer,
};

static int dw_mipi_dsi2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi2 *dsi2;
	struct resource *res;
	void __iomem *regs;
	int id;
	int ret;

	dsi2 = devm_kzalloc(dev, sizeof(*dsi2), GFP_KERNEL);
	if (!dsi2)
		return -ENOMEM;

	id = of_alias_get_id(dev->of_node, "dsi");
	if (id < 0)
		id = 0;

	dsi2->dev = dev;
	dsi2->id = id;
	dsi2->pdata = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, dsi2);

	if (device_property_read_bool(dev, "dual-connector-split")) {
		dsi2->dual_connector_split = true;

		if (device_property_read_bool(dev, "left-display"))
			dsi2->left_display = true;
	}

	if (device_property_read_u32(dev, "split-area", &dsi2->split_area))
		dsi2->split_area = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	dsi2->irq = platform_get_irq(pdev, 0);
	if (dsi2->irq < 0)
		return dsi2->irq;

	dsi2->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi2->pclk)) {
		ret = PTR_ERR(dsi2->pclk);
		DRM_DEV_ERROR(dev, "Unable to get pclk: %d\n", ret);
		return ret;
	}

	dsi2->sys_clk = devm_clk_get(dev, "sys_clk");
	if (IS_ERR(dsi2->sys_clk)) {
		ret = PTR_ERR(dsi2->sys_clk);
		DRM_DEV_ERROR(dev, "Unable to get sys_clk: %d\n", ret);
		return ret;
	}

	dsi2->regmap = devm_regmap_init_mmio(dev, regs,
					    &dw_mipi_dsi2_regmap_config);
	if (IS_ERR(dsi2->regmap)) {
		ret = PTR_ERR(dsi2->regmap);
		DRM_DEV_ERROR(dev, "failed to init register map: %d\n", ret);
		return ret;
	}

	dsi2->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						    "rockchip,grf");
	if (IS_ERR(dsi2->grf)) {
		ret = PTR_ERR(dsi2->grf);
		DRM_DEV_ERROR(dsi2->dev, "Unable to get grf: %d\n", ret);
		return ret;
	}

	dsi2->apb_rst = devm_reset_control_get(dev, "apb");
	if (IS_ERR(dsi2->apb_rst)) {
		ret = PTR_ERR(dsi2->apb_rst);
		DRM_DEV_ERROR(dev,
			      "Unable to get reset control: %d\n", ret);
		return ret;
	}

	dsi2->dcphy = devm_phy_optional_get(dev, "dcphy");
	if (IS_ERR(dsi2->dcphy)) {
		ret = PTR_ERR(dsi2->dcphy);
		DRM_DEV_ERROR(dev, "failed to get mipi dcphy: %d\n", ret);
		return ret;
	}

	dsi2->te_gpio = devm_gpiod_get_optional(dsi2->dev, "te", GPIOD_IN);
	if (IS_ERR(dsi2->te_gpio))
		dsi2->te_gpio = NULL;

	if (dsi2->te_gpio) {
		ret = devm_request_threaded_irq(dsi2->dev, gpiod_to_irq(dsi2->te_gpio),
						dw_mipi_dsi2_te_irq_handler, NULL,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						"PANEL-TE", dsi2);
		if (ret) {
			dev_err(dsi2->dev, "failed to request TE IRQ: %d\n", ret);
			return ret;
		}
	}

	ret = devm_request_irq(dev, dsi2->irq, dw_mipi_dsi2_irq_handler,
			       IRQF_SHARED, dev_name(dev), dsi2);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	dsi2->host.ops = &dw_mipi_dsi2_host_ops;
	dsi2->host.dev = dev;
	ret = mipi_dsi_host_register(&dsi2->host);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to register MIPI host: %d\n", ret);
		return ret;
	}

	return component_add(&pdev->dev, &dw_mipi_dsi2_ops);
}

static int dw_mipi_dsi2_remove(struct platform_device *pdev)
{
	return 0;
}

static __maybe_unused int dw_mipi_dsi2_runtime_suspend(struct device *dev)
{
	struct dw_mipi_dsi2 *dsi2 = dev_get_drvdata(dev);

	clk_disable_unprepare(dsi2->pclk);
	clk_disable_unprepare(dsi2->sys_clk);

	return 0;
}

static __maybe_unused int dw_mipi_dsi2_runtime_resume(struct device *dev)
{
	struct dw_mipi_dsi2 *dsi2 = dev_get_drvdata(dev);

	clk_prepare_enable(dsi2->pclk);
	clk_prepare_enable(dsi2->sys_clk);

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi2_rockchip_pm_ops = {
	SET_RUNTIME_PM_OPS(dw_mipi_dsi2_runtime_suspend,
			   dw_mipi_dsi2_runtime_resume, NULL)
};

static const u32 rk3588_dsi0_grf_reg_fields[MAX_FIELDS] = {
	[TXREQCLKHS_EN]		= GRF_REG_FIELD(0x0000, 11, 11),
	[GATING_EN]		= GRF_REG_FIELD(0x0000, 10, 10),
	[IPI_SHUTDN]		= GRF_REG_FIELD(0x0000,  9,  9),
	[IPI_COLORM]		= GRF_REG_FIELD(0x0000,  8,  8),
	[IPI_COLOR_DEPTH]	= GRF_REG_FIELD(0x0000,  4,  7),
	[IPI_FORMAT]		= GRF_REG_FIELD(0x0000,  0,  3),
};

static const u32 rk3588_dsi1_grf_reg_fields[MAX_FIELDS] = {
	[TXREQCLKHS_EN]		= GRF_REG_FIELD(0x0004, 11, 11),
	[GATING_EN]		= GRF_REG_FIELD(0x0004, 10, 10),
	[IPI_SHUTDN]		= GRF_REG_FIELD(0x0004,  9,  9),
	[IPI_COLORM]		= GRF_REG_FIELD(0x0004,  8,  8),
	[IPI_COLOR_DEPTH]	= GRF_REG_FIELD(0x0004,  4,  7),
	[IPI_FORMAT]		= GRF_REG_FIELD(0x0004,  0,  3),
};

static const struct dw_mipi_dsi2_plat_data rk3588_mipi_dsi2_plat_data = {
	.dsi0_grf_reg_fields = rk3588_dsi0_grf_reg_fields,
	.dsi1_grf_reg_fields = rk3588_dsi1_grf_reg_fields,
	.dphy_max_bit_rate_per_lane = 4500000000ULL,
	.cphy_max_symbol_rate_per_lane = 2000000000ULL,
};

static const struct of_device_id dw_mipi_dsi2_dt_ids[] = {
	{
		.compatible = "rockchip,rk3588-mipi-dsi2",
		.data = &rk3588_mipi_dsi2_plat_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi2_dt_ids);

struct platform_driver dw_mipi_dsi2_rockchip_driver = {
	.probe	= dw_mipi_dsi2_probe,
	.remove = dw_mipi_dsi2_remove,
	.driver = {
		.of_match_table = dw_mipi_dsi2_dt_ids,
		.pm = &dw_mipi_dsi2_rockchip_pm_ops,
		.name = "dw-mipi-dsi2",
	},
};
