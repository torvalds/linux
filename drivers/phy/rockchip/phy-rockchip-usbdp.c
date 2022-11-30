// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip USBDP Combo PHY with Samsung IP block driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/ch9.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>

#include <linux/phy/phy-rockchip-usbdp.h>

#define BIT_WRITEABLE_SHIFT	16

enum {
	DP_BW_RBR,
	DP_BW_HBR,
	DP_BW_HBR2,
	DP_BW_HBR3,
};

enum {
	UDPHY_MODE_NONE		= 0,
	UDPHY_MODE_USB		= BIT(0),
	UDPHY_MODE_DP		= BIT(1),
	UDPHY_MODE_DP_USB	= BIT(1) | BIT(0),
};

struct udphy_grf_reg {
	unsigned int	offset;
	unsigned int	bitend;
	unsigned int	bitstart;
	unsigned int	disable;
	unsigned int	enable;
};

struct udphy_grf_cfg {
	/* u2phy-grf */
	struct udphy_grf_reg	bvalid_phy_con;
	struct udphy_grf_reg	bvalid_grf_con;

	/* usb-grf */
	struct udphy_grf_reg	usb3otg0_cfg;
	struct udphy_grf_reg	usb3otg1_cfg;

	/* usbdpphy-grf */
	struct udphy_grf_reg	low_pwrn;
	struct udphy_grf_reg	rx_lfps;
};

struct udphy_vogrf_cfg {
	/* vo-grf */
	struct udphy_grf_reg hpd_trigger;
};

struct dp_tx_drv_ctrl {
	u32 trsv_reg0204;
	u32 trsv_reg0205;
	u32 trsv_reg0206;
	u32 trsv_reg0207;
};

struct rockchip_udphy;

struct rockchip_udphy_cfg {
	/* resets to be requested */
	const char * const *rst_list;
	int num_rsts;

	struct udphy_grf_cfg grfcfg;
	struct udphy_vogrf_cfg vogrfcfg[2];
	const struct dp_tx_drv_ctrl (*dp_tx_ctrl_cfg[4])[4];
	const struct dp_tx_drv_ctrl (*dp_tx_ctrl_cfg_typec[4])[4];
	int (*combophy_init)(struct rockchip_udphy *udphy);
	int (*dp_phy_set_rate)(struct rockchip_udphy *udphy,
			       struct phy_configure_opts_dp *dp);
	int (*dp_phy_set_voltages)(struct rockchip_udphy *udphy,
				   struct phy_configure_opts_dp *dp);
	int (*hpd_event_trigger)(struct rockchip_udphy *udphy, bool hpd);
	int (*dplane_enable)(struct rockchip_udphy *udphy, int dp_lanes);
	int (*dplane_select)(struct rockchip_udphy *udphy);
};

struct rockchip_udphy {
	struct device *dev;
	struct regmap *pma_regmap;
	struct regmap *u2phygrf;
	struct regmap *udphygrf;
	struct regmap *usbgrf;
	struct regmap *vogrf;
	struct typec_switch *sw;
	struct typec_mux *mux;
	struct mutex mutex; /* mutex to protect access to individual PHYs */

	/* clocks and rests */
	int num_clks;
	struct clk_bulk_data *clks;
	struct clk *refclk;
	struct reset_control **rsts;

	/* PHY status management */
	bool flip;
	bool mode_change;
	u8 mode;
	u8 status;

	/* utilized for USB */
	bool hs; /* flag for high-speed */

	/* utilized for DP */
	struct gpio_desc *sbu1_dc_gpio;
	struct gpio_desc *sbu2_dc_gpio;
	u32 lane_mux_sel[4];
	u32 dp_lane_sel[4];
	u32 dp_aux_dout_sel;
	u32 dp_aux_din_sel;
	bool dp_sink_hpd_sel;
	bool dp_sink_hpd_cfg;
	u8 bw;
	int id;

	/* PHY const config */
	const struct rockchip_udphy_cfg *cfgs;
};

static const struct dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_rbr_hbr[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x20, 0x10, 0x42, 0xe5 },
		{ 0x26, 0x14, 0x42, 0xe5 },
		{ 0x29, 0x18, 0x42, 0xe5 },
		{ 0x2b, 0x1c, 0x43, 0xe7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x23, 0x10, 0x42, 0xe7 },
		{ 0x2a, 0x17, 0x43, 0xe7 },
		{ 0x2b, 0x1a, 0x43, 0xe7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x27, 0x10, 0x42, 0xe7 },
		{ 0x2b, 0x17, 0x43, 0xe7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0x29, 0x10, 0x43, 0xe7 },
	},
};

static const struct dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_rbr_hbr_typec[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x20, 0x10, 0x42, 0xe5 },
		{ 0x26, 0x14, 0x42, 0xe5 },
		{ 0x29, 0x18, 0x42, 0xe5 },
		{ 0x2b, 0x1c, 0x43, 0xe7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x23, 0x10, 0x42, 0xe7 },
		{ 0x2a, 0x17, 0x43, 0xe7 },
		{ 0x2b, 0x1a, 0x43, 0xe7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x27, 0x10, 0x43, 0x67 },
		{ 0x2b, 0x17, 0x43, 0xe7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0x29, 0x10, 0x43, 0xe7 },
	},
};

static const struct dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_hbr2[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x21, 0x10, 0x42, 0xe5 },
		{ 0x26, 0x14, 0x42, 0xe5 },
		{ 0x26, 0x16, 0x43, 0xe5 },
		{ 0x2a, 0x19, 0x43, 0xe7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x24, 0x10, 0x42, 0xe7 },
		{ 0x2a, 0x17, 0x43, 0xe7 },
		{ 0x2b, 0x1a, 0x43, 0xe7 },
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x28, 0x10, 0x42, 0xe7 },
		{ 0x2b, 0x17, 0x43, 0xe7 },
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0x28, 0x10, 0x43, 0xe7 },
	},
};

static const struct dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_hbr3[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{
		{ 0x21, 0x10, 0x42, 0xe5 },
		{ 0x26, 0x14, 0x42, 0xe5 },
		{ 0x26, 0x16, 0x43, 0xe5 },
		{ 0x29, 0x18, 0x43, 0xe7 },
	},

	/* voltage swing 1, pre-emphasis 0->2 */
	{
		{ 0x24, 0x10, 0x42, 0xe7 },
		{ 0x2a, 0x18, 0x43, 0xe7 },
		{ 0x2b, 0x1b, 0x43, 0xe7 }
	},

	/* voltage swing 2, pre-emphasis 0->1 */
	{
		{ 0x27, 0x10, 0x42, 0xe7 },
		{ 0x2b, 0x18, 0x43, 0xe7 }
	},

	/* voltage swing 3, pre-emphasis 0 */
	{
		{ 0x28, 0x10, 0x43, 0xe7 },
	},
};

static const struct reg_sequence rk3588_udphy_24m_refclk_cfg[] = {
	{0x0090, 0x68}, {0x0094, 0x68},
	{0x0128, 0x24}, {0x012c, 0x44},
	{0x0130, 0x3f}, {0x0134, 0x44},
	{0x015c, 0xa9}, {0x0160, 0x71},
	{0x0164, 0x71}, {0x0168, 0xa9},
	{0x0174, 0xa9}, {0x0178, 0x71},
	{0x017c, 0x71}, {0x0180, 0xa9},
	{0x018c, 0x41}, {0x0190, 0x00},
	{0x0194, 0x05}, {0x01ac, 0x2a},
	{0x01b0, 0x17}, {0x01b4, 0x17},
	{0x01b8, 0x2a}, {0x01c8, 0x04},
	{0x01cc, 0x08}, {0x01d0, 0x08},
	{0x01d4, 0x04}, {0x01d8, 0x20},
	{0x01dc, 0x01}, {0x01e0, 0x09},
	{0x01e4, 0x03}, {0x01f0, 0x29},
	{0x01f4, 0x02}, {0x01f8, 0x02},
	{0x01fc, 0x29}, {0x0208, 0x2a},
	{0x020c, 0x17}, {0x0210, 0x17},
	{0x0214, 0x2a}, {0x0224, 0x20},
	{0x03f0, 0x0a}, {0x03f4, 0x07},
	{0x03f8, 0x07}, {0x03fc, 0x0c},
	{0x0404, 0x12}, {0x0408, 0x1a},
	{0x040c, 0x1a}, {0x0410, 0x3f},
	{0x0ce0, 0x68}, {0x0ce8, 0xd0},
	{0x0cf0, 0x87}, {0x0cf8, 0x70},
	{0x0d00, 0x70}, {0x0d08, 0xa9},
	{0x1ce0, 0x68}, {0x1ce8, 0xd0},
	{0x1cf0, 0x87}, {0x1cf8, 0x70},
	{0x1d00, 0x70}, {0x1d08, 0xa9},
	{0x0a3c, 0xd0}, {0x0a44, 0xd0},
	{0x0a48, 0x01}, {0x0a4c, 0x0d},
	{0x0a54, 0xe0}, {0x0a5c, 0xe0},
	{0x0a64, 0xa8}, {0x1a3c, 0xd0},
	{0x1a44, 0xd0}, {0x1a48, 0x01},
	{0x1a4c, 0x0d}, {0x1a54, 0xe0},
	{0x1a5c, 0xe0}, {0x1a64, 0xa8}
};

static const struct reg_sequence rk3588_udphy_26m_refclk_cfg[] = {
	{0x0830, 0x07}, {0x085c, 0x80},
	{0x1030, 0x07}, {0x105c, 0x80},
	{0x1830, 0x07}, {0x185c, 0x80},
	{0x2030, 0x07}, {0x205c, 0x80},
	{0x0228, 0x38}, {0x0104, 0x44},
	{0x0248, 0x44}, {0x038C, 0x02},
	{0x0878, 0x04}, {0x1878, 0x04},
	{0x0898, 0x77}, {0x1898, 0x77},
	{0x0054, 0x01}, {0x00e0, 0x38},
	{0x0060, 0x24}, {0x0064, 0x77},
	{0x0070, 0x76}, {0x0234, 0xE8},
	{0x0AF4, 0x15}, {0x1AF4, 0x15},
	{0x081C, 0xE5}, {0x181C, 0xE5},
	{0x099C, 0x48}, {0x199C, 0x48},
	{0x09A4, 0x07}, {0x09A8, 0x22},
	{0x19A4, 0x07}, {0x19A8, 0x22},
	{0x09B8, 0x3E}, {0x19B8, 0x3E},
	{0x09E4, 0x02}, {0x19E4, 0x02},
	{0x0A34, 0x1E}, {0x1A34, 0x1E},
	{0x0A98, 0x2F}, {0x1A98, 0x2F},
	{0x0c30, 0x0E}, {0x0C48, 0x06},
	{0x1C30, 0x0E}, {0x1C48, 0x06},
	{0x028C, 0x18}, {0x0AF0, 0x00},
	{0x1AF0, 0x00}
};

static const struct reg_sequence rk3588_udphy_init_sequence[] = {
	{0x0104, 0x44}, {0x0234, 0xE8},
	{0x0248, 0x44}, {0x028C, 0x18},
	{0x081C, 0xE5}, {0x0878, 0x00},
	{0x0994, 0x1C}, {0x0AF0, 0x00},
	{0x181C, 0xE5}, {0x1878, 0x00},
	{0x1994, 0x1C}, {0x1AF0, 0x00},
	{0x0428, 0x60}, {0x0D58, 0x33},
	{0x1D58, 0x33}, {0x0990, 0x74},
	{0x0D64, 0x17}, {0x08C8, 0x13},
	{0x1990, 0x74}, {0x1D64, 0x17},
	{0x18C8, 0x13}, {0x0D90, 0x40},
	{0x0DA8, 0x40}, {0x0DC0, 0x40},
	{0x0DD8, 0x40}, {0x1D90, 0x40},
	{0x1DA8, 0x40}, {0x1DC0, 0x40},
	{0x1DD8, 0x40}, {0x03C0, 0x30},
	{0x03C4, 0x06}, {0x0E10, 0x00},
	{0x1E10, 0x00}, {0x043C, 0x0F},
	{0x0D2C, 0xFF}, {0x1D2C, 0xFF},
	{0x0D34, 0x0F}, {0x1D34, 0x0F},
	{0x08FC, 0x2A}, {0x0914, 0x28},
	{0x0A30, 0x03}, {0x0E38, 0x05},
	{0x0ECC, 0x27}, {0x0ED0, 0x22},
	{0x0ED4, 0x26}, {0x18FC, 0x2A},
	{0x1914, 0x28}, {0x1A30, 0x03},
	{0x1E38, 0x05}, {0x1ECC, 0x27},
	{0x1ED0, 0x22}, {0x1ED4, 0x26},
	{0x0048, 0x0F}, {0x0060, 0x3C},
	{0x0064, 0xF7}, {0x006C, 0x20},
	{0x0070, 0x7D}, {0x0074, 0x68},
	{0x0AF4, 0x1A}, {0x1AF4, 0x1A},
	{0x0440, 0x3F}, {0x10D4, 0x08},
	{0x20D4, 0x08}, {0x00D4, 0x30},
	{0x0024, 0x6e},
};

static inline int grfreg_write(struct regmap *base,
			       const struct udphy_grf_reg *reg, bool en)
{
	u32 val, mask, tmp;

	tmp = en ? reg->enable : reg->disable;
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static int udphy_clk_init(struct rockchip_udphy *udphy, struct device *dev)
{
	int i;

	udphy->num_clks = devm_clk_bulk_get_all(dev, &udphy->clks);
	if (udphy->num_clks < 1)
		return -ENODEV;

	/* used for configure phy reference clock frequency */
	for (i = 0; i < udphy->num_clks; i++) {
		if (!strncmp(udphy->clks[i].id, "refclk", 6)) {
			udphy->refclk = udphy->clks[i].clk;
			break;
		}
	}

	if (!udphy->refclk)
		dev_warn(udphy->dev, "no refclk found\n");

	return 0;
}

static int udphy_reset_init(struct rockchip_udphy *udphy, struct device *dev)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int idx;

	udphy->rsts = devm_kcalloc(dev, cfg->num_rsts,
				   sizeof(*udphy->rsts), GFP_KERNEL);
	if (!udphy->rsts)
		return -ENOMEM;

	for (idx = 0; idx < cfg->num_rsts; idx++) {
		struct reset_control *rst;
		const char *name = cfg->rst_list[idx];

		rst = devm_reset_control_get(dev, name);
		if (IS_ERR(rst)) {
			dev_err(dev, "failed to get %s reset\n", name);
			devm_kfree(dev, (void *)udphy->rsts);
			return PTR_ERR(rst);
		}

		udphy->rsts[idx] = rst;
	}

	return 0;
}

static int udphy_get_rst_idx(const char * const *list, int num, char *name)
{
	int idx;

	for (idx = 0; idx < num; idx++) {
		if (!strcmp(list[idx], name))
			return idx;
	}

	return -EINVAL;
}

static int udphy_reset_assert(struct rockchip_udphy *udphy, char *name)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int idx;

	idx = udphy_get_rst_idx(cfg->rst_list, cfg->num_rsts, name);
	if (idx < 0)
		return idx;

	return reset_control_assert(udphy->rsts[idx]);
}

static int udphy_reset_deassert(struct rockchip_udphy *udphy, char *name)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int idx;

	idx = udphy_get_rst_idx(cfg->rst_list, cfg->num_rsts, name);
	if (idx < 0)
		return idx;

	return reset_control_deassert(udphy->rsts[idx]);
}

static void udphy_u3_port_disable(struct rockchip_udphy *udphy, u8 disable)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	const struct udphy_grf_reg *preg;

	preg = udphy->id ? &cfg->grfcfg.usb3otg1_cfg : &cfg->grfcfg.usb3otg0_cfg;
	grfreg_write(udphy->usbgrf, preg, disable);
}

static void udphy_usb_bvalid_enable(struct rockchip_udphy *udphy, u8 enable)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;

	grfreg_write(udphy->u2phygrf, &cfg->grfcfg.bvalid_phy_con, enable);
	grfreg_write(udphy->u2phygrf, &cfg->grfcfg.bvalid_grf_con, enable);
}

/*
 * In usb/dp combo phy driver, here are 2 ways to mapping lanes.
 *
 * 1 Type-C Mapping table (DP_Alt_Mode V1.0b remove ABF pin mapping)
 * ---------------------------------------------------------------------------
 * Type-C Pin   B11-B10       A2-A3       A11-A10       B2-B3
 * PHY Pad      ln0(tx/rx)    ln1(tx)     ln2(tx/rx)    ln3(tx)
 * C/E(Normal)  dpln3         dpln2       dpln0         dpln1
 * C/E(Flip  )  dpln0         dpln1       dpln3         dpln2
 * D/F(Normal)  usbrx         usbtx       dpln0         dpln1
 * D/F(Flip  )  dpln0         dpln1       usbrx         usbtx
 * A(Normal  )  dpln3         dpln1       dpln2         dpln0
 * A(Flip    )  dpln2         dpln0       dpln3         dpln1
 * B(Normal  )  usbrx         usbtx       dpln1         dpln0
 * B(Flip    )  dpln1         dpln0       usbrx         usbtx
 * ---------------------------------------------------------------------------
 *
 * 2 Mapping the lanes in dtsi
 * if all 4 lane assignment for dp function, define rockchip,dp-lane-mux = <x x x x>;
 * sample as follow:
 * ---------------------------------------------------------------------------
 *                        B11-B10       A2-A3       A11-A10       B2-B3
 * rockchip,dp-lane-mux   ln0(tx/rx)    ln1(tx)     ln2(tx/rx)    ln3(tx)
 * <0 1 2 3>              dpln0         dpln1       dpln2         dpln3
 * <2 3 0 1>              dpln2         dpln3       dpln0         dpln1
 * ---------------------------------------------------------------------------
 * if 2 lane for dp function, 2 lane for usb function, define rockchip,dp-lane-mux = <x x>;
 * sample as follow:
 * ---------------------------------------------------------------------------
 *                        B11-B10       A2-A3       A11-A10       B2-B3
 * rockchip,dp-lane-mux   ln0(tx/rx)    ln1(tx)     ln2(tx/rx)    ln3(tx)
 * <0 1>                  dpln0         dpln1       usbrx         usbtx
 * <2 3>                  usbrx         usbtx       dpln0         dpln1
 * ---------------------------------------------------------------------------
 */

static int udphy_dplane_select(struct rockchip_udphy *udphy)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;

	if (cfg->dplane_select)
		return cfg->dplane_select(udphy);

	return 0;
}

static int udphy_dplane_get(struct rockchip_udphy *udphy)
{
	int dp_lanes;

	switch (udphy->mode) {
	case UDPHY_MODE_DP:
		dp_lanes = 4;
		break;
	case UDPHY_MODE_DP_USB:
		dp_lanes = 2;
		break;
	case UDPHY_MODE_USB:
		fallthrough;
	default:
		dp_lanes = 0;
		break;
	}

	return dp_lanes;
}

static int udphy_dplane_enable(struct rockchip_udphy *udphy, int dp_lanes)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int ret = 0;

	if (cfg->dplane_enable)
		ret = cfg->dplane_enable(udphy, dp_lanes);

	return ret;
}

static int upphy_set_typec_default_mapping(struct rockchip_udphy *udphy)
{
	if (udphy->flip) {
		udphy->dp_lane_sel[0] = 0;
		udphy->dp_lane_sel[1] = 1;
		udphy->dp_lane_sel[2] = 3;
		udphy->dp_lane_sel[3] = 2;
		udphy->lane_mux_sel[0] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[1] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[2] = PHY_LANE_MUX_USB;
		udphy->lane_mux_sel[3] = PHY_LANE_MUX_USB;
		udphy->dp_aux_dout_sel = PHY_AUX_DP_DATA_POL_INVERT;
		udphy->dp_aux_din_sel = PHY_AUX_DP_DATA_POL_INVERT;
		gpiod_set_value_cansleep(udphy->sbu1_dc_gpio, 1);
		gpiod_set_value_cansleep(udphy->sbu2_dc_gpio, 0);
	} else {
		udphy->dp_lane_sel[0] = 2;
		udphy->dp_lane_sel[1] = 3;
		udphy->dp_lane_sel[2] = 1;
		udphy->dp_lane_sel[3] = 0;
		udphy->lane_mux_sel[0] = PHY_LANE_MUX_USB;
		udphy->lane_mux_sel[1] = PHY_LANE_MUX_USB;
		udphy->lane_mux_sel[2] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[3] = PHY_LANE_MUX_DP;
		udphy->dp_aux_dout_sel = PHY_AUX_DP_DATA_POL_NORMAL;
		udphy->dp_aux_din_sel = PHY_AUX_DP_DATA_POL_NORMAL;
		gpiod_set_value_cansleep(udphy->sbu1_dc_gpio, 0);
		gpiod_set_value_cansleep(udphy->sbu2_dc_gpio, 1);
	}

	udphy->mode = UDPHY_MODE_DP_USB;

	return 0;
}

static int udphy_orien_sw_set(struct typec_switch *sw,
			      enum typec_orientation orien)
{
	struct rockchip_udphy *udphy = typec_switch_get_drvdata(sw);

	mutex_lock(&udphy->mutex);

	if (orien == TYPEC_ORIENTATION_NONE) {
		gpiod_set_value_cansleep(udphy->sbu1_dc_gpio, 0);
		gpiod_set_value_cansleep(udphy->sbu2_dc_gpio, 0);
		/* unattached */
		udphy_usb_bvalid_enable(udphy, false);
		goto unlock_ret;
	}

	udphy->flip = (orien == TYPEC_ORIENTATION_REVERSE) ? true : false;
	upphy_set_typec_default_mapping(udphy);
	udphy_usb_bvalid_enable(udphy, true);

unlock_ret:
	mutex_unlock(&udphy->mutex);
	return 0;
}

static int udphy_setup_orien_switch(struct rockchip_udphy *udphy)
{
	struct typec_switch_desc sw_desc = { };

	sw_desc.drvdata = udphy;
	sw_desc.fwnode = dev_fwnode(udphy->dev);
	sw_desc.set = udphy_orien_sw_set;

	udphy->sw = typec_switch_register(udphy->dev, &sw_desc);
	if (IS_ERR(udphy->sw)) {
		dev_err(udphy->dev, "Error register typec orientation switch: %ld\n",
			PTR_ERR(udphy->sw));
		return PTR_ERR(udphy->sw);
	}

	return 0;
}

static void udphy_orien_switch_unregister(void *data)
{
	struct rockchip_udphy *udphy = data;

	typec_switch_unregister(udphy->sw);
}

static int udphy_setup(struct rockchip_udphy *udphy)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int ret = 0;

	ret = clk_bulk_prepare_enable(udphy->num_clks, udphy->clks);
	if (ret) {
		dev_err(udphy->dev, "failed to enable clk\n");
		return ret;
	}

	if (cfg->combophy_init) {
		ret = cfg->combophy_init(udphy);
		if (ret) {
			dev_err(udphy->dev, "failed to init combophy\n");
			clk_bulk_disable_unprepare(udphy->num_clks, udphy->clks);
			return ret;
		}
	}

	return 0;
}

static int udphy_disable(struct rockchip_udphy *udphy)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int i;

	clk_bulk_disable_unprepare(udphy->num_clks, udphy->clks);

	for (i = 0; i < cfg->num_rsts; i++)
		reset_control_assert(udphy->rsts[i]);

	return 0;
}

static int udphy_parse_lane_mux_data(struct rockchip_udphy *udphy, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct property *prop;
	int ret, i, len, num_lanes;

	prop = of_find_property(np, "rockchip,dp-lane-mux", &len);
	if (!prop) {
		dev_dbg(dev, "failed to find dp lane mux, following dp alt mode\n");
		udphy->mode = UDPHY_MODE_USB;
		return 0;
	}

	num_lanes = len / sizeof(u32);

	if (num_lanes != 2 && num_lanes != 4) {
		dev_err(dev, "invalid number of lane mux\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "rockchip,dp-lane-mux", udphy->dp_lane_sel, num_lanes);
	if (ret) {
		dev_err(dev, "get dp lane mux failed\n");
		return -EINVAL;
	}

	for (i = 0; i < num_lanes; i++) {
		int j;

		if (udphy->dp_lane_sel[i] > 3) {
			dev_err(dev, "lane mux between 0 and 3, exceeding the range\n");
			return -EINVAL;
		}

		udphy->lane_mux_sel[udphy->dp_lane_sel[i]] = PHY_LANE_MUX_DP;

		for (j = i + 1; j < num_lanes; j++) {
			if (udphy->dp_lane_sel[i] == udphy->dp_lane_sel[j]) {
				dev_err(dev, "set repeat lane mux value\n");
				return -EINVAL;
			}
		}
	}

	udphy->mode = UDPHY_MODE_DP;
	if (num_lanes == 2) {
		udphy->mode |= UDPHY_MODE_USB;
		udphy->flip = udphy->lane_mux_sel[0] == PHY_LANE_MUX_DP ? true : false;
	}

	return 0;
}

static int udphy_get_initial_status(struct rockchip_udphy *udphy)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int ret, i;
	u32 value;

	ret = clk_bulk_prepare_enable(udphy->num_clks, udphy->clks);
	if (ret) {
		dev_err(udphy->dev, "failed to enable clk\n");
		return ret;
	}

	for (i = 0; i < cfg->num_rsts; i++)
		reset_control_deassert(udphy->rsts[i]);

	regmap_read(udphy->pma_regmap, CMN_LANE_MUX_AND_EN_OFFSET, &value);
	if (FIELD_GET(CMN_DP_LANE_MUX_ALL, value) && FIELD_GET(CMN_DP_LANE_EN_ALL, value))
		udphy->status = UDPHY_MODE_DP;
	else
		udphy_disable(udphy);

	return 0;
}

static int udphy_parse_dt(struct rockchip_udphy *udphy, struct device *dev)
{
	struct device_node *np = dev->of_node;
	enum usb_device_speed maximum_speed;
	int ret;

	udphy->u2phygrf = syscon_regmap_lookup_by_phandle(np, "rockchip,u2phy-grf");
	if (IS_ERR(udphy->u2phygrf)) {
		if (PTR_ERR(udphy->u2phygrf) == -ENODEV) {
			dev_warn(dev, "missing u2phy-grf dt node\n");
			udphy->u2phygrf = NULL;
		} else {
			return PTR_ERR(udphy->u2phygrf);
		}
	}

	udphy->udphygrf = syscon_regmap_lookup_by_phandle(np, "rockchip,usbdpphy-grf");
	if (IS_ERR(udphy->udphygrf)) {
		if (PTR_ERR(udphy->udphygrf) == -ENODEV) {
			dev_warn(dev, "missing usbdpphy-grf dt node\n");
			udphy->udphygrf = NULL;
		} else {
			return PTR_ERR(udphy->udphygrf);
		}
	}

	udphy->usbgrf = syscon_regmap_lookup_by_phandle(np, "rockchip,usb-grf");
	if (IS_ERR(udphy->usbgrf)) {
		if (PTR_ERR(udphy->usbgrf) == -ENODEV) {
			dev_warn(dev, "missing usb-grf dt node\n");
			udphy->usbgrf = NULL;
		} else {
			return PTR_ERR(udphy->usbgrf);
		}
	}

	udphy->vogrf = syscon_regmap_lookup_by_phandle(np, "rockchip,vo-grf");
	if (IS_ERR(udphy->vogrf)) {
		if (PTR_ERR(udphy->vogrf) == -ENODEV) {
			dev_warn(dev, "missing vo-grf dt node\n");
			udphy->vogrf = NULL;
		} else {
			return PTR_ERR(udphy->vogrf);
		}
	}

	ret = udphy_parse_lane_mux_data(udphy, dev);
	if (ret)
		return ret;

	udphy->sbu1_dc_gpio = devm_gpiod_get_optional(dev, "sbu1-dc", GPIOD_OUT_LOW);
	if (IS_ERR(udphy->sbu1_dc_gpio))
		return PTR_ERR(udphy->sbu1_dc_gpio);

	udphy->sbu2_dc_gpio = devm_gpiod_get_optional(dev, "sbu2-dc", GPIOD_OUT_LOW);
	if (IS_ERR(udphy->sbu2_dc_gpio))
		return PTR_ERR(udphy->sbu2_dc_gpio);

	if (device_property_present(dev, "maximum-speed")) {
		maximum_speed = usb_get_maximum_speed(dev);
		udphy->hs = maximum_speed <= USB_SPEED_HIGH ? true : false;
	}

	ret = udphy_clk_init(udphy, dev);
	if (ret)
		return ret;

	ret = udphy_reset_init(udphy, dev);
	if (ret)
		return ret;

	return 0;
}

static int udphy_power_on(struct rockchip_udphy *udphy, u8 mode)
{
	int ret;

	if (!(udphy->mode & mode)) {
		dev_info(udphy->dev, "mode 0x%02x is not support\n", mode);
		return 0;
	}

	if (udphy->status == UDPHY_MODE_NONE) {
		udphy->mode_change = false;
		ret = udphy_setup(udphy);
		if (ret)
			return ret;

		if (udphy->mode & UDPHY_MODE_USB)
			udphy_u3_port_disable(udphy, false);
	} else if (udphy->mode_change) {
		udphy->mode_change = false;
		udphy->status = UDPHY_MODE_NONE;
		if (udphy->mode == UDPHY_MODE_DP)
			udphy_u3_port_disable(udphy, true);

		ret = udphy_disable(udphy);
		if (ret)
			return ret;
		ret = udphy_setup(udphy);
		if (ret)
			return ret;
	}

	udphy->status |= mode;

	return 0;
}

static int udphy_power_off(struct rockchip_udphy *udphy, u8 mode)
{
	int ret;

	if (!(udphy->mode & mode)) {
		dev_info(udphy->dev, "mode 0x%02x is not support\n", mode);
		return 0;
	}

	if (!udphy->status)
		return 0;

	udphy->status &= ~mode;

	if (udphy->status == UDPHY_MODE_NONE) {
		ret = udphy_disable(udphy);
		if (ret)
			return ret;
	}

	return 0;
}

static int rockchip_dp_phy_power_on(struct phy *phy)
{
	struct rockchip_udphy *udphy = phy_get_drvdata(phy);
	int ret, dp_lanes;

	mutex_lock(&udphy->mutex);

	dp_lanes = udphy_dplane_get(udphy);
	phy_set_bus_width(phy, dp_lanes);

	ret = udphy_power_on(udphy, UDPHY_MODE_DP);
	if (ret)
		goto unlock;

	ret = udphy_dplane_enable(udphy, dp_lanes);
	if (ret)
		goto unlock;

	ret = udphy_dplane_select(udphy);

unlock:
	mutex_unlock(&udphy->mutex);
	/*
	 * If data send by aux channel too fast after phy power on,
	 * the aux may be not ready which will cause aux error. Adding
	 * delay to avoid this issue.
	 */
	usleep_range(10000, 11000);
	return ret;
}

static int rockchip_dp_phy_power_off(struct phy *phy)
{
	struct rockchip_udphy *udphy = phy_get_drvdata(phy);
	int ret;

	mutex_lock(&udphy->mutex);
	ret = udphy_dplane_enable(udphy, 0);
	if (ret)
		goto unlock;

	ret = udphy_power_off(udphy, UDPHY_MODE_DP);

unlock:
	mutex_unlock(&udphy->mutex);
	return ret;
}

static int rockchip_dp_phy_verify_link_rate(unsigned int link_rate)
{
	switch (link_rate) {
	case 1620:
	case 2700:
	case 5400:
	case 8100:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rockchip_dp_phy_verify_config(struct rockchip_udphy *udphy,
					 struct phy_configure_opts_dp *dp)
{
	int i, ret;

	/* If changing link rate was required, verify it's supported. */
	ret = rockchip_dp_phy_verify_link_rate(dp->link_rate);
	if (ret)
		return ret;

	/* Verify lane count. */
	switch (dp->lanes) {
	case 1:
	case 2:
	case 4:
		/* valid lane count. */
		break;
	default:
		return -EINVAL;
	}

	/*
	 * If changing voltages is required, check swing and pre-emphasis
	 * levels, per-lane.
	 */
	if (dp->set_voltages) {
		/* Lane count verified previously. */
		for (i = 0; i < dp->lanes; i++) {
			if (dp->voltage[i] > 3 || dp->pre[i] > 3)
				return -EINVAL;

			/*
			 * Sum of voltage swing and pre-emphasis levels cannot
			 * exceed 3.
			 */
			if (dp->voltage[i] + dp->pre[i] > 3)
				return -EINVAL;
		}
	}

	return 0;
}

static int rockchip_dp_phy_configure(struct phy *phy,
				     union phy_configure_opts *opts)
{
	struct rockchip_udphy *udphy = phy_get_drvdata(phy);
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int ret;

	ret = rockchip_dp_phy_verify_config(udphy, &opts->dp);
	if (ret)
		return ret;

	if (opts->dp.set_rate && cfg->dp_phy_set_rate) {
		ret = cfg->dp_phy_set_rate(udphy, &opts->dp);
		if (ret) {
			dev_err(udphy->dev,
				"rockchip_hdptx_phy_set_rate failed\n");
			return ret;
		}
	}

	if (opts->dp.set_voltages && cfg->dp_phy_set_voltages) {
		ret = cfg->dp_phy_set_voltages(udphy, &opts->dp);
		if (ret) {
			dev_err(udphy->dev,
				"rockchip_dp_phy_set_voltages failed\n");
			return ret;
		}
	}

	return 0;
}

static const struct phy_ops rockchip_dp_phy_ops = {
	.power_on	= rockchip_dp_phy_power_on,
	.power_off	= rockchip_dp_phy_power_off,
	.configure	= rockchip_dp_phy_configure,
	.owner		= THIS_MODULE,
};

static int rockchip_u3phy_init(struct phy *phy)
{
	struct rockchip_udphy *udphy = phy_get_drvdata(phy);
	int ret = 0;

	mutex_lock(&udphy->mutex);
	/* DP only or high-speed, disable U3 port */
	if (!(udphy->mode & UDPHY_MODE_USB) || udphy->hs) {
		udphy_u3_port_disable(udphy, true);
		goto unlock;
	}

	ret = udphy_power_on(udphy, UDPHY_MODE_USB);

unlock:
	mutex_unlock(&udphy->mutex);
	return ret;
}

static int rockchip_u3phy_exit(struct phy *phy)
{
	struct rockchip_udphy *udphy = phy_get_drvdata(phy);
	int ret = 0;

	mutex_lock(&udphy->mutex);
	/* DP only or high-speed */
	if (!(udphy->mode & UDPHY_MODE_USB) || udphy->hs)
		goto unlock;

	ret = udphy_power_off(udphy, UDPHY_MODE_USB);

unlock:
	mutex_unlock(&udphy->mutex);
	return ret;
}

static const struct phy_ops rockchip_u3phy_ops = {
	.init		= rockchip_u3phy_init,
	.exit		= rockchip_u3phy_exit,
	.owner		= THIS_MODULE,
};

static int usbdp_typec_mux_set(struct typec_mux *mux,
			       struct typec_mux_state *state)
{
	struct rockchip_udphy *udphy = typec_mux_get_drvdata(mux);
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	u8 mode;

	mutex_lock(&udphy->mutex);

	switch (state->mode) {
	case TYPEC_DP_STATE_C:
		fallthrough;
	case TYPEC_DP_STATE_E:
		udphy->lane_mux_sel[0] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[1] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[2] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[3] = PHY_LANE_MUX_DP;
		mode = UDPHY_MODE_DP;
		break;
	case TYPEC_DP_STATE_D:
		fallthrough;
	default:
		if (udphy->flip) {
			udphy->lane_mux_sel[0] = PHY_LANE_MUX_DP;
			udphy->lane_mux_sel[1] = PHY_LANE_MUX_DP;
			udphy->lane_mux_sel[2] = PHY_LANE_MUX_USB;
			udphy->lane_mux_sel[3] = PHY_LANE_MUX_USB;
		} else {
			udphy->lane_mux_sel[0] = PHY_LANE_MUX_USB;
			udphy->lane_mux_sel[1] = PHY_LANE_MUX_USB;
			udphy->lane_mux_sel[2] = PHY_LANE_MUX_DP;
			udphy->lane_mux_sel[3] = PHY_LANE_MUX_DP;
		}
		mode = UDPHY_MODE_DP_USB;
		break;
	}

	if (state->alt && state->alt->svid == USB_TYPEC_DP_SID) {
		struct typec_displayport_data *data = state->data;

		if (!data) {
			if (cfg->hpd_event_trigger)
				cfg->hpd_event_trigger(udphy, false);
		} else if (data->status & DP_STATUS_IRQ_HPD) {
			if (cfg->hpd_event_trigger) {
				cfg->hpd_event_trigger(udphy, false);
				usleep_range(750, 800);
				cfg->hpd_event_trigger(udphy, true);
			}
		} else if (data->status & DP_STATUS_HPD_STATE) {
			if (udphy->mode != mode) {
				udphy->mode = mode;
				udphy->mode_change = true;
			}
			if (cfg->hpd_event_trigger)
				cfg->hpd_event_trigger(udphy, true);
		} else {
			if (cfg->hpd_event_trigger)
				cfg->hpd_event_trigger(udphy, false);
		}
	}

	mutex_unlock(&udphy->mutex);
	return 0;
}

static int udphy_setup_typec_mux(struct rockchip_udphy *udphy)
{
	struct typec_mux_desc mux_desc = {};

	mux_desc.drvdata = udphy;
	mux_desc.fwnode = dev_fwnode(udphy->dev);
	mux_desc.set = usbdp_typec_mux_set;

	udphy->mux = typec_mux_register(udphy->dev, &mux_desc);
	if (IS_ERR(udphy->mux)) {
		dev_err(udphy->dev, "Error register typec mux: %ld\n",
			PTR_ERR(udphy->mux));
		return PTR_ERR(udphy->mux);
	}

	return 0;
}

static void udphy_typec_mux_unregister(void *data)
{
	struct rockchip_udphy *udphy = data;

	typec_mux_unregister(udphy->mux);
}

static u32 udphy_dp_get_max_link_rate(struct rockchip_udphy *udphy, struct device_node *np)
{
	u32 max_link_rate;
	int ret;

	ret = of_property_read_u32(np, "max-link-rate", &max_link_rate);
	if (ret)
		return 8100;

	ret = rockchip_dp_phy_verify_link_rate(max_link_rate);
	if (ret) {
		dev_warn(udphy->dev, "invalid max-link-rate value:%d\n", max_link_rate);
		max_link_rate = 8100;
	}

	return max_link_rate;
}

static const struct regmap_config rockchip_udphy_pma_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x20dc,
};

static int rockchip_udphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct phy_provider *phy_provider;
	struct resource *res;
	struct rockchip_udphy *udphy;
	const struct rockchip_udphy_cfg *phy_cfgs;
	void __iomem *base;
	int id, ret;

	udphy = devm_kzalloc(dev, sizeof(*udphy), GFP_KERNEL);
	if (!udphy)
		return -ENOMEM;

	id = of_alias_get_id(dev->of_node, "usbdp");
	if (id < 0)
		id = 0;
	udphy->id = id;

	phy_cfgs = of_device_get_match_data(dev);
	if (!phy_cfgs) {
		dev_err(dev, "no OF data can be matched with %p node\n", np);
		return -EINVAL;
	}

	udphy->cfgs = phy_cfgs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	udphy->pma_regmap = devm_regmap_init_mmio(dev, base + UDPHY_PMA,
						  &rockchip_udphy_pma_regmap_cfg);
	if (IS_ERR(udphy->pma_regmap))
		return PTR_ERR(udphy->pma_regmap);

	ret = udphy_parse_dt(udphy, dev);
	if (ret)
		return ret;

	ret = udphy_get_initial_status(udphy);
	if (ret)
		return ret;

	mutex_init(&udphy->mutex);
	udphy->dev = dev;
	platform_set_drvdata(pdev, udphy);

	if (device_property_present(dev, "orientation-switch")) {
		ret = udphy_setup_orien_switch(udphy);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, udphy_orien_switch_unregister, udphy);
		if (ret)
			return ret;
	}

	if (device_property_present(dev, "svid")) {
		ret = udphy_setup_typec_mux(udphy);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, udphy_typec_mux_unregister, udphy);
		if (ret)
			return ret;
	}

	for_each_available_child_of_node(np, child_np) {
		struct phy *phy;

		if (of_node_name_eq(child_np, "dp-port")) {
			phy = devm_phy_create(dev, child_np, &rockchip_dp_phy_ops);
			if (IS_ERR(phy)) {
				dev_err(dev, "failed to create dp phy: %pOFn\n", child_np);
				goto put_child;
			}

			phy_set_bus_width(phy, udphy_dplane_get(udphy));
			phy->attrs.max_link_rate = udphy_dp_get_max_link_rate(udphy, child_np);
		} else if (of_node_name_eq(child_np, "u3-port")) {
			phy = devm_phy_create(dev, child_np, &rockchip_u3phy_ops);
			if (IS_ERR(phy)) {
				dev_err(dev, "failed to create usb phy: %pOFn\n", child_np);
				goto put_child;
			}
		} else
			continue;

		phy_set_drvdata(phy, udphy);
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		goto put_child;
	}

	return 0;

put_child:
	of_node_put(child_np);
	return ret;
}

static int rk3588_udphy_refclk_set(struct rockchip_udphy *udphy)
{
	unsigned long rate;
	int ret;

	/* configure phy reference clock */
	rate = clk_get_rate(udphy->refclk);
	dev_dbg(udphy->dev, "refclk freq %ld\n", rate);

	switch (rate) {
	case 24000000:
		ret = regmap_multi_reg_write(udphy->pma_regmap, rk3588_udphy_24m_refclk_cfg,
					     ARRAY_SIZE(rk3588_udphy_24m_refclk_cfg));
		if (ret)
			return ret;
		break;
	case 26000000:
		/* register default is 26MHz */
		ret = regmap_multi_reg_write(udphy->pma_regmap, rk3588_udphy_26m_refclk_cfg,
					     ARRAY_SIZE(rk3588_udphy_26m_refclk_cfg));
		if (ret)
			return ret;
		break;
	default:
		dev_err(udphy->dev, "unsupported refclk freq %ld\n", rate);
		return -EINVAL;
	}

	return 0;
}

static int rk3588_udphy_status_check(struct rockchip_udphy *udphy)
{
	unsigned int val;
	int ret;

	/* LCPLL check */
	if (udphy->mode & UDPHY_MODE_USB) {
		ret = regmap_read_poll_timeout(udphy->pma_regmap, CMN_ANA_LCPLL_DONE_OFFSET,
					       val, (val & CMN_ANA_LCPLL_AFC_DONE) &&
					       (val & CMN_ANA_LCPLL_LOCK_DONE), 200, 100000);
		if (ret) {
			dev_err(udphy->dev, "cmn ana lcpll lock timeout\n");
			return ret;
		}
	}

	if (udphy->mode & UDPHY_MODE_USB) {
		if (!udphy->flip) {
			ret = regmap_read_poll_timeout(udphy->pma_regmap,
						       TRSV_LN0_MON_RX_CDR_DONE_OFFSET, val,
						       val & TRSV_LN0_MON_RX_CDR_LOCK_DONE,
						       200, 100000);
			if (ret)
				dev_notice(udphy->dev, "trsv ln0 mon rx cdr lock timeout\n");
		} else {
			ret = regmap_read_poll_timeout(udphy->pma_regmap,
						       TRSV_LN2_MON_RX_CDR_DONE_OFFSET, val,
						       val & TRSV_LN2_MON_RX_CDR_LOCK_DONE,
						       200, 100000);
			if (ret)
				dev_notice(udphy->dev, "trsv ln2 mon rx cdr lock timeout\n");
		}
	}

	return 0;
}

static int rk3588_udphy_init(struct rockchip_udphy *udphy)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	int ret;

	/* enable rx lfps for usb */
	if (udphy->mode & UDPHY_MODE_USB)
		grfreg_write(udphy->udphygrf, &cfg->grfcfg.rx_lfps, true);

	/* Step 1: power on pma and deassert apb rstn */
	grfreg_write(udphy->udphygrf, &cfg->grfcfg.low_pwrn, true);

	udphy_reset_deassert(udphy, "pma_apb");
	udphy_reset_deassert(udphy, "pcs_apb");

	/* Step 2: set init sequence and phy refclk */
	ret = regmap_multi_reg_write(udphy->pma_regmap, rk3588_udphy_init_sequence,
				     ARRAY_SIZE(rk3588_udphy_init_sequence));
	if (ret) {
		dev_err(udphy->dev, "init sequence set error %d\n", ret);
		goto assert_apb;
	}

	ret = rk3588_udphy_refclk_set(udphy);
	if (ret) {
		dev_err(udphy->dev, "refclk set error %d\n", ret);
		goto assert_apb;
	}

	/* Step 3: configure lane mux */
	regmap_update_bits(udphy->pma_regmap, CMN_LANE_MUX_AND_EN_OFFSET,
			   CMN_DP_LANE_MUX_ALL | CMN_DP_LANE_EN_ALL,
			   FIELD_PREP(CMN_DP_LANE_MUX_N(3), udphy->lane_mux_sel[3]) |
			   FIELD_PREP(CMN_DP_LANE_MUX_N(2), udphy->lane_mux_sel[2]) |
			   FIELD_PREP(CMN_DP_LANE_MUX_N(1), udphy->lane_mux_sel[1]) |
			   FIELD_PREP(CMN_DP_LANE_MUX_N(0), udphy->lane_mux_sel[0]) |
			   FIELD_PREP(CMN_DP_LANE_EN_ALL, 0));

	/* Step 4: deassert init rstn and wait for 200ns from datasheet */
	if (udphy->mode & UDPHY_MODE_USB)
		udphy_reset_deassert(udphy, "init");

	if (udphy->mode & UDPHY_MODE_DP) {
		regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET,
				   CMN_DP_INIT_RSTN,
				   FIELD_PREP(CMN_DP_INIT_RSTN, 0x1));
	}

	udelay(1);

	/*  Step 5: deassert cmn/lane rstn */
	if (udphy->mode & UDPHY_MODE_USB) {
		udphy_reset_deassert(udphy, "cmn");
		udphy_reset_deassert(udphy, "lane");
	}

	/*  Step 6: wait for lock done of pll */
	ret = rk3588_udphy_status_check(udphy);
	if (ret)
		goto assert_phy;

	return 0;

assert_phy:
	udphy_reset_assert(udphy, "init");
	udphy_reset_assert(udphy, "cmn");
	udphy_reset_assert(udphy, "lane");

assert_apb:
	udphy_reset_assert(udphy, "pma_apb");
	udphy_reset_assert(udphy, "pcs_apb");
	return ret;
}

static int rk3588_udphy_hpd_event_trigger(struct rockchip_udphy *udphy, bool hpd)
{
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;

	udphy->dp_sink_hpd_sel = true;
	udphy->dp_sink_hpd_cfg = hpd;

	grfreg_write(udphy->vogrf, &cfg->vogrfcfg[udphy->id].hpd_trigger, hpd);

	return 0;
}

static int rk3588_udphy_dplane_enable(struct rockchip_udphy *udphy, int dp_lanes)
{
	int i;
	u32 val = 0;

	for (i = 0; i < dp_lanes; i++)
		val |= BIT(udphy->dp_lane_sel[i]);

	regmap_update_bits(udphy->pma_regmap, CMN_LANE_MUX_AND_EN_OFFSET, CMN_DP_LANE_EN_ALL,
			   FIELD_PREP(CMN_DP_LANE_EN_ALL, val));

	if (!dp_lanes)
		regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET,
				   CMN_DP_CMN_RSTN, FIELD_PREP(CMN_DP_CMN_RSTN, 0x0));

	return 0;
}

static int rk3588_udphy_dplane_select(struct rockchip_udphy *udphy)
{
	u32 value = 0;

	switch (udphy->mode) {
	case UDPHY_MODE_DP:
		value |= 2 << udphy->dp_lane_sel[2] * 2;
		value |= 3 << udphy->dp_lane_sel[3] * 2;
		fallthrough;
	case UDPHY_MODE_DP_USB:
		value |= 0 << udphy->dp_lane_sel[0] * 2;
		value |= 1 << udphy->dp_lane_sel[1] * 2;
		break;
	case UDPHY_MODE_USB:
		break;
	default:
		break;
	}

	regmap_write(udphy->vogrf, udphy->id ? RK3588_GRF_VO0_CON2 : RK3588_GRF_VO0_CON0,
		     ((DP_AUX_DIN_SEL | DP_AUX_DOUT_SEL | DP_LANE_SEL_ALL) << 16) |
		     FIELD_PREP(DP_AUX_DIN_SEL, udphy->dp_aux_din_sel) |
		     FIELD_PREP(DP_AUX_DOUT_SEL, udphy->dp_aux_dout_sel) | value);

	return 0;
}

static int rk3588_dp_phy_set_rate(struct rockchip_udphy *udphy,
				  struct phy_configure_opts_dp *dp)
{
	u32 val;
	int ret;

	regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET,
			   CMN_DP_CMN_RSTN, FIELD_PREP(CMN_DP_CMN_RSTN, 0x0));

	switch (dp->link_rate) {
	case 1620:
		udphy->bw = DP_BW_RBR;
		break;
	case 2700:
		udphy->bw = DP_BW_HBR;
		break;
	case 5400:
		udphy->bw = DP_BW_HBR2;
		break;
	case 8100:
		udphy->bw = DP_BW_HBR3;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(udphy->pma_regmap, CMN_DP_LINK_OFFSET, CMN_DP_TX_LINK_BW,
			   FIELD_PREP(CMN_DP_TX_LINK_BW, udphy->bw));
	regmap_update_bits(udphy->pma_regmap, CMN_SSC_EN_OFFSET, CMN_ROPLL_SSC_EN,
			   FIELD_PREP(CMN_ROPLL_SSC_EN, dp->ssc));
	regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET, CMN_DP_CMN_RSTN,
			   FIELD_PREP(CMN_DP_CMN_RSTN, 0x1));

	ret = regmap_read_poll_timeout(udphy->pma_regmap, CMN_ANA_ROPLL_DONE_OFFSET, val,
				       FIELD_GET(CMN_ANA_ROPLL_LOCK_DONE, val) &&
				       FIELD_GET(CMN_ANA_ROPLL_AFC_DONE, val),
				       0, 1000);
	if (ret) {
		dev_err(udphy->dev, "ROPLL is not lock\n");
		return ret;
	}

	return 0;
}

static void rk3588_dp_phy_set_voltage(struct rockchip_udphy *udphy, u8 bw,
				      u32 voltage, u32 pre, u32 lane)
{
	u32 offset = 0x800 * lane;
	u32 val;
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;
	const struct dp_tx_drv_ctrl (*dp_ctrl)[4];

	dp_ctrl = udphy->mux ? cfg->dp_tx_ctrl_cfg_typec[bw] : cfg->dp_tx_ctrl_cfg[bw];
	val = dp_ctrl[voltage][pre].trsv_reg0204;
	regmap_write(udphy->pma_regmap, 0x0810 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0205;
	regmap_write(udphy->pma_regmap, 0x0814 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0206;
	regmap_write(udphy->pma_regmap, 0x0818 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0207;
	regmap_write(udphy->pma_regmap, 0x081c + offset, val);
}

static int rk3588_dp_phy_set_voltages(struct rockchip_udphy *udphy,
				      struct phy_configure_opts_dp *dp)
{
	u32 i, lane;

	for (i = 0; i < dp->lanes; i++) {
		lane = udphy->dp_lane_sel[i];
		switch (dp->link_rate) {
		case 1620:
		case 2700:
			regmap_update_bits(udphy->pma_regmap, TRSV_ANA_TX_CLK_OFFSET_N(lane),
					   LN_ANA_TX_SER_TXCLK_INV,
					   FIELD_PREP(LN_ANA_TX_SER_TXCLK_INV,
					   udphy->lane_mux_sel[lane]));
			break;
		case 5400:
		case 8100:
			regmap_update_bits(udphy->pma_regmap, TRSV_ANA_TX_CLK_OFFSET_N(lane),
					   LN_ANA_TX_SER_TXCLK_INV,
					   FIELD_PREP(LN_ANA_TX_SER_TXCLK_INV, 0x0));
			break;
		}

		rk3588_dp_phy_set_voltage(udphy, udphy->bw, dp->voltage[i], dp->pre[i], lane);
	}

	return 0;
}

static int __maybe_unused udphy_resume(struct device *dev)
{
	struct rockchip_udphy *udphy = dev_get_drvdata(dev);
	const struct rockchip_udphy_cfg *cfg = udphy->cfgs;

	if (udphy->dp_sink_hpd_sel)
		cfg->hpd_event_trigger(udphy, udphy->dp_sink_hpd_cfg);

	return 0;
}

static const struct dev_pm_ops udphy_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, udphy_resume)
};

static const char * const rk3588_udphy_rst_l[] = {
	"init", "cmn", "lane", "pcs_apb", "pma_apb"
};

static const struct rockchip_udphy_cfg rk3588_udphy_cfgs = {
	.num_rsts = ARRAY_SIZE(rk3588_udphy_rst_l),
	.rst_list = rk3588_udphy_rst_l,
	.grfcfg	= {
		/* u2phy-grf */
		.bvalid_phy_con		= { 0x0008, 1, 0, 0x2, 0x3 },
		.bvalid_grf_con		= { 0x0010, 3, 2, 0x2, 0x3 },

		/* usb-grf */
		.usb3otg0_cfg		= { 0x001c, 15, 0, 0x1100, 0x0188 },
		.usb3otg1_cfg		= { 0x0034, 15, 0, 0x1100, 0x0188 },

		/* usbdpphy-grf */
		.low_pwrn		= { 0x0004, 13, 13, 0, 1 },
		.rx_lfps		= { 0x0004, 14, 14, 0, 1 },
	},
	.vogrfcfg = {
		{
			.hpd_trigger	= { 0x0000, 11, 10, 1, 3 },
		},
		{
			.hpd_trigger	= { 0x0008, 11, 10, 1, 3 },
		},
	},
	.dp_tx_ctrl_cfg = {
		rk3588_dp_tx_drv_ctrl_rbr_hbr,
		rk3588_dp_tx_drv_ctrl_rbr_hbr,
		rk3588_dp_tx_drv_ctrl_hbr2,
		rk3588_dp_tx_drv_ctrl_hbr3,
	},
	.dp_tx_ctrl_cfg_typec = {
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_hbr2,
		rk3588_dp_tx_drv_ctrl_hbr3,
	},
	.combophy_init = rk3588_udphy_init,
	.dp_phy_set_rate = rk3588_dp_phy_set_rate,
	.dp_phy_set_voltages = rk3588_dp_phy_set_voltages,
	.hpd_event_trigger = rk3588_udphy_hpd_event_trigger,
	.dplane_enable = rk3588_udphy_dplane_enable,
	.dplane_select = rk3588_udphy_dplane_select,
};

static const struct of_device_id rockchip_udphy_dt_match[] = {
	{
		.compatible = "rockchip,rk3588-usbdp-phy",
		.data = &rk3588_udphy_cfgs
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, rockchip_udphy_dt_match);

static struct platform_driver rockchip_udphy_driver = {
	.probe		= rockchip_udphy_probe,
	.driver		= {
		.name	= "rockchip-usbdp-phy",
		.of_match_table = rockchip_udphy_dt_match,
		.pm = &udphy_pm_ops,
	},
};

module_platform_driver(rockchip_udphy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_AUTHOR("Zhang Yubing <yubing.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USBDP Combo PHY driver");
MODULE_LICENSE("GPL v2");
