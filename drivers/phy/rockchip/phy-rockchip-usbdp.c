// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip USBDP Combo PHY with Samsung IP block driver
 *
 * Copyright (C) 2021-2024 Rockchip Electronics Co., Ltd
 * Copyright (C) 2024 Collabora Ltd
 */

#include <dt-bindings/phy/phy.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/ch9.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>

/* USBDP PHY Register Definitions */
#define UDPHY_PCS				0x4000
#define UDPHY_PMA				0x8000

/* VO0 GRF Registers */
#define DP_SINK_HPD_CFG				BIT(11)
#define DP_SINK_HPD_SEL				BIT(10)
#define DP_AUX_DIN_SEL				BIT(9)
#define DP_AUX_DOUT_SEL				BIT(8)
#define DP_LANE_SEL_N(n)			GENMASK(2 * (n) + 1, 2 * (n))
#define DP_LANE_SEL_ALL				GENMASK(7, 0)

/* PMA CMN Registers */
#define CMN_LANE_MUX_AND_EN_OFFSET		0x0288	/* cmn_reg00A2 */
#define CMN_DP_LANE_MUX_N(n)			BIT((n) + 4)
#define CMN_DP_LANE_EN_N(n)			BIT(n)
#define CMN_DP_LANE_MUX_ALL			GENMASK(7, 4)
#define CMN_DP_LANE_EN_ALL			GENMASK(3, 0)

#define CMN_DP_LINK_OFFSET			0x28c	/* cmn_reg00A3 */
#define CMN_DP_TX_LINK_BW			GENMASK(6, 5)
#define CMN_DP_TX_LANE_SWAP_EN			BIT(2)

#define CMN_SSC_EN_OFFSET			0x2d0	/* cmn_reg00B4 */
#define CMN_ROPLL_SSC_EN			BIT(1)
#define CMN_LCPLL_SSC_EN			BIT(0)

#define CMN_ANA_LCPLL_DONE_OFFSET		0x0350	/* cmn_reg00D4 */
#define CMN_ANA_LCPLL_LOCK_DONE			BIT(7)
#define CMN_ANA_LCPLL_AFC_DONE			BIT(6)

#define CMN_ANA_ROPLL_DONE_OFFSET		0x0354	/* cmn_reg00D5 */
#define CMN_ANA_ROPLL_LOCK_DONE			BIT(1)
#define CMN_ANA_ROPLL_AFC_DONE			BIT(0)

#define CMN_DP_RSTN_OFFSET			0x038c	/* cmn_reg00E3 */
#define CMN_DP_INIT_RSTN			BIT(3)
#define CMN_DP_CMN_RSTN				BIT(2)
#define CMN_CDR_WTCHDG_EN			BIT(1)
#define CMN_CDR_WTCHDG_MSK_CDR_EN		BIT(0)

#define TRSV_ANA_TX_CLK_OFFSET_N(n)		(0x854 + (n) * 0x800)	/* trsv_reg0215 */
#define LN_ANA_TX_SER_TXCLK_INV			BIT(1)

#define TRSV_LN0_MON_RX_CDR_DONE_OFFSET		0x0b84	/* trsv_reg02E1 */
#define TRSV_LN0_MON_RX_CDR_LOCK_DONE		BIT(0)

#define TRSV_LN2_MON_RX_CDR_DONE_OFFSET		0x1b84	/* trsv_reg06E1 */
#define TRSV_LN2_MON_RX_CDR_LOCK_DONE		BIT(0)

#define BIT_WRITEABLE_SHIFT			16
#define PHY_AUX_DP_DATA_POL_NORMAL		0
#define PHY_AUX_DP_DATA_POL_INVERT		1
#define PHY_LANE_MUX_USB			0
#define PHY_LANE_MUX_DP				1

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

struct rk_udphy_grf_reg {
	unsigned int	offset;
	unsigned int	disable;
	unsigned int	enable;
};

#define _RK_UDPHY_GEN_GRF_REG(offset, mask, disable, enable) \
{\
	offset, \
	FIELD_PREP_CONST(mask, disable) | (mask << BIT_WRITEABLE_SHIFT), \
	FIELD_PREP_CONST(mask, enable) | (mask << BIT_WRITEABLE_SHIFT), \
}

#define RK_UDPHY_GEN_GRF_REG(offset, bitend, bitstart, disable, enable) \
	_RK_UDPHY_GEN_GRF_REG(offset, GENMASK(bitend, bitstart), disable, enable)

struct rk_udphy_grf_cfg {
	/* u2phy-grf */
	struct rk_udphy_grf_reg	bvalid_phy_con;
	struct rk_udphy_grf_reg	bvalid_grf_con;

	/* usb-grf */
	struct rk_udphy_grf_reg	usb3otg0_cfg;
	struct rk_udphy_grf_reg	usb3otg1_cfg;

	/* usbdpphy-grf */
	struct rk_udphy_grf_reg	low_pwrn;
	struct rk_udphy_grf_reg	rx_lfps;
};

struct rk_udphy_vogrf_cfg {
	/* vo-grf */
	struct rk_udphy_grf_reg hpd_trigger;
	u32 dp_lane_reg;
};

struct rk_udphy_dp_tx_drv_ctrl {
	u32 trsv_reg0204;
	u32 trsv_reg0205;
	u32 trsv_reg0206;
	u32 trsv_reg0207;
};

struct rk_udphy_cfg {
	unsigned int num_phys;
	unsigned int phy_ids[2];
	/* resets to be requested */
	const char * const *rst_list;
	int num_rsts;

	struct rk_udphy_grf_cfg grfcfg;
	struct rk_udphy_vogrf_cfg vogrfcfg[2];
	const struct rk_udphy_dp_tx_drv_ctrl (*dp_tx_ctrl_cfg[4])[4];
	const struct rk_udphy_dp_tx_drv_ctrl (*dp_tx_ctrl_cfg_typec[4])[4];
};

struct rk_udphy {
	struct device *dev;
	struct regmap *pma_regmap;
	struct regmap *u2phygrf;
	struct regmap *udphygrf;
	struct regmap *usbgrf;
	struct regmap *vogrf;
	struct typec_switch_dev *sw;
	struct typec_mux_dev *mux;
	struct mutex mutex; /* mutex to protect access to individual PHYs */

	/* clocks and rests */
	int num_clks;
	struct clk_bulk_data *clks;
	struct clk *refclk;
	int num_rsts;
	struct reset_control_bulk_data *rsts;

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
	unsigned int link_rate;
	unsigned int lanes;
	u8 bw;
	int id;

	bool dp_in_use;

	/* PHY const config */
	const struct rk_udphy_cfg *cfgs;

	/* PHY devices */
	struct phy *phy_dp;
	struct phy *phy_u3;
};

static const struct rk_udphy_dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_rbr_hbr[4][4] = {
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

static const struct rk_udphy_dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_rbr_hbr_typec[4][4] = {
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

static const struct rk_udphy_dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_hbr2[4][4] = {
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

static const struct rk_udphy_dp_tx_drv_ctrl rk3588_dp_tx_drv_ctrl_hbr3[4][4] = {
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

static const struct reg_sequence rk_udphy_24m_refclk_cfg[] = {
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

static const struct reg_sequence rk_udphy_26m_refclk_cfg[] = {
	{0x0830, 0x07}, {0x085c, 0x80},
	{0x1030, 0x07}, {0x105c, 0x80},
	{0x1830, 0x07}, {0x185c, 0x80},
	{0x2030, 0x07}, {0x205c, 0x80},
	{0x0228, 0x38}, {0x0104, 0x44},
	{0x0248, 0x44}, {0x038c, 0x02},
	{0x0878, 0x04}, {0x1878, 0x04},
	{0x0898, 0x77}, {0x1898, 0x77},
	{0x0054, 0x01}, {0x00e0, 0x38},
	{0x0060, 0x24}, {0x0064, 0x77},
	{0x0070, 0x76}, {0x0234, 0xe8},
	{0x0af4, 0x15}, {0x1af4, 0x15},
	{0x081c, 0xe5}, {0x181c, 0xe5},
	{0x099c, 0x48}, {0x199c, 0x48},
	{0x09a4, 0x07}, {0x09a8, 0x22},
	{0x19a4, 0x07}, {0x19a8, 0x22},
	{0x09b8, 0x3e}, {0x19b8, 0x3e},
	{0x09e4, 0x02}, {0x19e4, 0x02},
	{0x0a34, 0x1e}, {0x1a34, 0x1e},
	{0x0a98, 0x2f}, {0x1a98, 0x2f},
	{0x0c30, 0x0e}, {0x0c48, 0x06},
	{0x1c30, 0x0e}, {0x1c48, 0x06},
	{0x028c, 0x18}, {0x0af0, 0x00},
	{0x1af0, 0x00}
};

static const struct reg_sequence rk_udphy_init_sequence[] = {
	{0x0104, 0x44}, {0x0234, 0xe8},
	{0x0248, 0x44}, {0x028c, 0x18},
	{0x081c, 0xe5}, {0x0878, 0x00},
	{0x0994, 0x1c}, {0x0af0, 0x00},
	{0x181c, 0xe5}, {0x1878, 0x00},
	{0x1994, 0x1c}, {0x1af0, 0x00},
	{0x0428, 0x60}, {0x0d58, 0x33},
	{0x1d58, 0x33}, {0x0990, 0x74},
	{0x0d64, 0x17}, {0x08c8, 0x13},
	{0x1990, 0x74}, {0x1d64, 0x17},
	{0x18c8, 0x13}, {0x0d90, 0x40},
	{0x0da8, 0x40}, {0x0dc0, 0x40},
	{0x0dd8, 0x40}, {0x1d90, 0x40},
	{0x1da8, 0x40}, {0x1dc0, 0x40},
	{0x1dd8, 0x40}, {0x03c0, 0x30},
	{0x03c4, 0x06}, {0x0e10, 0x00},
	{0x1e10, 0x00}, {0x043c, 0x0f},
	{0x0d2c, 0xff}, {0x1d2c, 0xff},
	{0x0d34, 0x0f}, {0x1d34, 0x0f},
	{0x08fc, 0x2a}, {0x0914, 0x28},
	{0x0a30, 0x03}, {0x0e38, 0x03},
	{0x0ecc, 0x27}, {0x0ed0, 0x22},
	{0x0ed4, 0x26}, {0x18fc, 0x2a},
	{0x1914, 0x28}, {0x1a30, 0x03},
	{0x1e38, 0x03}, {0x1ecc, 0x27},
	{0x1ed0, 0x22}, {0x1ed4, 0x26},
	{0x0048, 0x0f}, {0x0060, 0x3c},
	{0x0064, 0xf7}, {0x006c, 0x20},
	{0x0070, 0x7d}, {0x0074, 0x68},
	{0x0af4, 0x1a}, {0x1af4, 0x1a},
	{0x0440, 0x3f}, {0x10d4, 0x08},
	{0x20d4, 0x08}, {0x00d4, 0x30},
	{0x0024, 0x6e},
};

static inline int rk_udphy_grfreg_write(struct regmap *base,
					const struct rk_udphy_grf_reg *reg, bool en)
{
	return regmap_write(base, reg->offset, en ? reg->enable : reg->disable);
}

static int rk_udphy_clk_init(struct rk_udphy *udphy, struct device *dev)
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
		return dev_err_probe(udphy->dev, -EINVAL, "no refclk found\n");

	return 0;
}

static int rk_udphy_reset_assert_all(struct rk_udphy *udphy)
{
	return reset_control_bulk_assert(udphy->num_rsts, udphy->rsts);
}

static int rk_udphy_reset_deassert_all(struct rk_udphy *udphy)
{
	return reset_control_bulk_deassert(udphy->num_rsts, udphy->rsts);
}

static int rk_udphy_reset_deassert(struct rk_udphy *udphy, char *name)
{
	struct reset_control_bulk_data *list = udphy->rsts;
	int idx;

	for (idx = 0; idx < udphy->num_rsts; idx++) {
		if (!strcmp(list[idx].id, name))
			return reset_control_deassert(list[idx].rstc);
	}

	return -EINVAL;
}

static int rk_udphy_reset_init(struct rk_udphy *udphy, struct device *dev)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;
	int idx;

	udphy->num_rsts = cfg->num_rsts;
	udphy->rsts = devm_kcalloc(dev, udphy->num_rsts,
				   sizeof(*udphy->rsts), GFP_KERNEL);
	if (!udphy->rsts)
		return -ENOMEM;

	for (idx = 0; idx < cfg->num_rsts; idx++)
		udphy->rsts[idx].id = cfg->rst_list[idx];

	return devm_reset_control_bulk_get_exclusive(dev, cfg->num_rsts,
						     udphy->rsts);
}

static void rk_udphy_u3_port_disable(struct rk_udphy *udphy, u8 disable)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;
	const struct rk_udphy_grf_reg *preg;

	preg = udphy->id ? &cfg->grfcfg.usb3otg1_cfg : &cfg->grfcfg.usb3otg0_cfg;
	rk_udphy_grfreg_write(udphy->usbgrf, preg, disable);
}

static void rk_udphy_usb_bvalid_enable(struct rk_udphy *udphy, u8 enable)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;

	rk_udphy_grfreg_write(udphy->u2phygrf, &cfg->grfcfg.bvalid_phy_con, enable);
	rk_udphy_grfreg_write(udphy->u2phygrf, &cfg->grfcfg.bvalid_grf_con, enable);
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

static void rk_udphy_dplane_select(struct rk_udphy *udphy)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;
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

	regmap_write(udphy->vogrf, cfg->vogrfcfg[udphy->id].dp_lane_reg,
		     ((DP_AUX_DIN_SEL | DP_AUX_DOUT_SEL | DP_LANE_SEL_ALL) << 16) |
		     FIELD_PREP(DP_AUX_DIN_SEL, udphy->dp_aux_din_sel) |
		     FIELD_PREP(DP_AUX_DOUT_SEL, udphy->dp_aux_dout_sel) | value);
}

static int rk_udphy_dplane_get(struct rk_udphy *udphy)
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
	default:
		dp_lanes = 0;
		break;
	}

	return dp_lanes;
}

static void rk_udphy_dplane_enable(struct rk_udphy *udphy, int dp_lanes)
{
	u32 val = 0;
	int i;

	for (i = 0; i < dp_lanes; i++)
		val |= BIT(udphy->dp_lane_sel[i]);

	regmap_update_bits(udphy->pma_regmap, CMN_LANE_MUX_AND_EN_OFFSET, CMN_DP_LANE_EN_ALL,
			   FIELD_PREP(CMN_DP_LANE_EN_ALL, val));

	if (!dp_lanes)
		regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET,
				   CMN_DP_CMN_RSTN, FIELD_PREP(CMN_DP_CMN_RSTN, 0x0));
}

static void rk_udphy_dp_hpd_event_trigger(struct rk_udphy *udphy, bool hpd)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;

	udphy->dp_sink_hpd_sel = true;
	udphy->dp_sink_hpd_cfg = hpd;

	if (!udphy->dp_in_use)
		return;

	rk_udphy_grfreg_write(udphy->vogrf, &cfg->vogrfcfg[udphy->id].hpd_trigger, hpd);
}

static void rk_udphy_set_typec_default_mapping(struct rk_udphy *udphy)
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
}

static int rk_udphy_orien_sw_set(struct typec_switch_dev *sw,
				 enum typec_orientation orien)
{
	struct rk_udphy *udphy = typec_switch_get_drvdata(sw);

	mutex_lock(&udphy->mutex);

	if (orien == TYPEC_ORIENTATION_NONE) {
		gpiod_set_value_cansleep(udphy->sbu1_dc_gpio, 0);
		gpiod_set_value_cansleep(udphy->sbu2_dc_gpio, 0);
		/* unattached */
		rk_udphy_usb_bvalid_enable(udphy, false);
		goto unlock_ret;
	}

	udphy->flip = (orien == TYPEC_ORIENTATION_REVERSE) ? true : false;
	rk_udphy_set_typec_default_mapping(udphy);
	rk_udphy_usb_bvalid_enable(udphy, true);

unlock_ret:
	mutex_unlock(&udphy->mutex);
	return 0;
}

static void rk_udphy_orien_switch_unregister(void *data)
{
	struct rk_udphy *udphy = data;

	typec_switch_unregister(udphy->sw);
}

static int rk_udphy_setup_orien_switch(struct rk_udphy *udphy)
{
	struct typec_switch_desc sw_desc = { };

	sw_desc.drvdata = udphy;
	sw_desc.fwnode = dev_fwnode(udphy->dev);
	sw_desc.set = rk_udphy_orien_sw_set;

	udphy->sw = typec_switch_register(udphy->dev, &sw_desc);
	if (IS_ERR(udphy->sw)) {
		dev_err(udphy->dev, "Error register typec orientation switch: %ld\n",
			PTR_ERR(udphy->sw));
		return PTR_ERR(udphy->sw);
	}

	return devm_add_action_or_reset(udphy->dev,
					rk_udphy_orien_switch_unregister, udphy);
}

static int rk_udphy_refclk_set(struct rk_udphy *udphy)
{
	unsigned long rate;
	int ret;

	/* configure phy reference clock */
	rate = clk_get_rate(udphy->refclk);
	dev_dbg(udphy->dev, "refclk freq %ld\n", rate);

	switch (rate) {
	case 24000000:
		ret = regmap_multi_reg_write(udphy->pma_regmap, rk_udphy_24m_refclk_cfg,
					     ARRAY_SIZE(rk_udphy_24m_refclk_cfg));
		if (ret)
			return ret;
		break;

	case 26000000:
		/* register default is 26MHz */
		ret = regmap_multi_reg_write(udphy->pma_regmap, rk_udphy_26m_refclk_cfg,
					     ARRAY_SIZE(rk_udphy_26m_refclk_cfg));
		if (ret)
			return ret;
		break;

	default:
		dev_err(udphy->dev, "unsupported refclk freq %ld\n", rate);
		return -EINVAL;
	}

	return 0;
}

static int rk_udphy_status_check(struct rk_udphy *udphy)
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
			/*
			 * If earlier software (U-Boot) enabled USB once already
			 * the PLL may have problems locking on the first try.
			 * It will be successful on the second try, so for the
			 * time being a -EPROBE_DEFER will solve the issue.
			 *
			 * This requires further investigation to understand the
			 * root cause, especially considering that the driver is
			 * asserting all reset lines at probe time.
			 */
			return -EPROBE_DEFER;
		}

		if (!udphy->flip) {
			ret = regmap_read_poll_timeout(udphy->pma_regmap,
						       TRSV_LN0_MON_RX_CDR_DONE_OFFSET, val,
						       val & TRSV_LN0_MON_RX_CDR_LOCK_DONE,
						       200, 100000);
			if (ret)
				dev_err(udphy->dev, "trsv ln0 mon rx cdr lock timeout\n");
		} else {
			ret = regmap_read_poll_timeout(udphy->pma_regmap,
						       TRSV_LN2_MON_RX_CDR_DONE_OFFSET, val,
						       val & TRSV_LN2_MON_RX_CDR_LOCK_DONE,
						       200, 100000);
			if (ret)
				dev_err(udphy->dev, "trsv ln2 mon rx cdr lock timeout\n");
		}
	}

	return 0;
}

static int rk_udphy_init(struct rk_udphy *udphy)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;
	int ret;

	rk_udphy_reset_assert_all(udphy);
	usleep_range(10000, 11000);

	/* enable rx lfps for usb */
	if (udphy->mode & UDPHY_MODE_USB)
		rk_udphy_grfreg_write(udphy->udphygrf, &cfg->grfcfg.rx_lfps, true);

	/* Step 1: power on pma and deassert apb rstn */
	rk_udphy_grfreg_write(udphy->udphygrf, &cfg->grfcfg.low_pwrn, true);

	rk_udphy_reset_deassert(udphy, "pma_apb");
	rk_udphy_reset_deassert(udphy, "pcs_apb");

	/* Step 2: set init sequence and phy refclk */
	ret = regmap_multi_reg_write(udphy->pma_regmap, rk_udphy_init_sequence,
				     ARRAY_SIZE(rk_udphy_init_sequence));
	if (ret) {
		dev_err(udphy->dev, "init sequence set error %d\n", ret);
		goto assert_resets;
	}

	ret = rk_udphy_refclk_set(udphy);
	if (ret) {
		dev_err(udphy->dev, "refclk set error %d\n", ret);
		goto assert_resets;
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
		rk_udphy_reset_deassert(udphy, "init");

	if (udphy->mode & UDPHY_MODE_DP) {
		regmap_update_bits(udphy->pma_regmap, CMN_DP_RSTN_OFFSET,
				   CMN_DP_INIT_RSTN,
				   FIELD_PREP(CMN_DP_INIT_RSTN, 0x1));
	}

	udelay(1);

	/*  Step 5: deassert cmn/lane rstn */
	if (udphy->mode & UDPHY_MODE_USB) {
		rk_udphy_reset_deassert(udphy, "cmn");
		rk_udphy_reset_deassert(udphy, "lane");
	}

	/*  Step 6: wait for lock done of pll */
	ret = rk_udphy_status_check(udphy);
	if (ret)
		goto assert_resets;

	return 0;

assert_resets:
	rk_udphy_reset_assert_all(udphy);
	return ret;
}

static int rk_udphy_setup(struct rk_udphy *udphy)
{
	int ret;

	ret = clk_bulk_prepare_enable(udphy->num_clks, udphy->clks);
	if (ret) {
		dev_err(udphy->dev, "failed to enable clk\n");
		return ret;
	}

	ret = rk_udphy_init(udphy);
	if (ret) {
		dev_err(udphy->dev, "failed to init combophy\n");
		clk_bulk_disable_unprepare(udphy->num_clks, udphy->clks);
		return ret;
	}

	return 0;
}

static void rk_udphy_disable(struct rk_udphy *udphy)
{
	clk_bulk_disable_unprepare(udphy->num_clks, udphy->clks);
	rk_udphy_reset_assert_all(udphy);
}

static int rk_udphy_parse_lane_mux_data(struct rk_udphy *udphy)
{
	int ret, i, num_lanes;

	num_lanes = device_property_count_u32(udphy->dev, "rockchip,dp-lane-mux");
	if (num_lanes < 0) {
		dev_dbg(udphy->dev, "no dp-lane-mux, following dp alt mode\n");
		udphy->mode = UDPHY_MODE_USB;
		return 0;
	}

	if (num_lanes != 2 && num_lanes != 4)
		return dev_err_probe(udphy->dev, -EINVAL,
				     "invalid number of lane mux\n");

	ret = device_property_read_u32_array(udphy->dev, "rockchip,dp-lane-mux",
					     udphy->dp_lane_sel, num_lanes);
	if (ret)
		return dev_err_probe(udphy->dev, ret, "get dp lane mux failed\n");

	for (i = 0; i < num_lanes; i++) {
		int j;

		if (udphy->dp_lane_sel[i] > 3)
			return dev_err_probe(udphy->dev, -EINVAL,
					     "lane mux between 0 and 3, exceeding the range\n");

		udphy->lane_mux_sel[udphy->dp_lane_sel[i]] = PHY_LANE_MUX_DP;

		for (j = i + 1; j < num_lanes; j++) {
			if (udphy->dp_lane_sel[i] == udphy->dp_lane_sel[j])
				return dev_err_probe(udphy->dev, -EINVAL,
						"set repeat lane mux value\n");
		}
	}

	udphy->mode = UDPHY_MODE_DP;
	if (num_lanes == 2) {
		udphy->mode |= UDPHY_MODE_USB;
		udphy->flip = (udphy->lane_mux_sel[0] == PHY_LANE_MUX_DP);
	}

	return 0;
}

static int rk_udphy_get_initial_status(struct rk_udphy *udphy)
{
	int ret;
	u32 value;

	ret = clk_bulk_prepare_enable(udphy->num_clks, udphy->clks);
	if (ret) {
		dev_err(udphy->dev, "failed to enable clk\n");
		return ret;
	}

	rk_udphy_reset_deassert_all(udphy);

	regmap_read(udphy->pma_regmap, CMN_LANE_MUX_AND_EN_OFFSET, &value);
	if (FIELD_GET(CMN_DP_LANE_MUX_ALL, value) && FIELD_GET(CMN_DP_LANE_EN_ALL, value))
		udphy->status = UDPHY_MODE_DP;
	else
		rk_udphy_disable(udphy);

	return 0;
}

static int rk_udphy_parse_dt(struct rk_udphy *udphy)
{
	struct device *dev = udphy->dev;
	struct device_node *np = dev_of_node(dev);
	enum usb_device_speed maximum_speed;
	int ret;

	udphy->u2phygrf = syscon_regmap_lookup_by_phandle(np, "rockchip,u2phy-grf");
	if (IS_ERR(udphy->u2phygrf))
		return dev_err_probe(dev, PTR_ERR(udphy->u2phygrf), "failed to get u2phy-grf\n");

	udphy->udphygrf = syscon_regmap_lookup_by_phandle(np, "rockchip,usbdpphy-grf");
	if (IS_ERR(udphy->udphygrf))
		return dev_err_probe(dev, PTR_ERR(udphy->udphygrf), "failed to get usbdpphy-grf\n");

	udphy->usbgrf = syscon_regmap_lookup_by_phandle(np, "rockchip,usb-grf");
	if (IS_ERR(udphy->usbgrf))
		return dev_err_probe(dev, PTR_ERR(udphy->usbgrf), "failed to get usb-grf\n");

	udphy->vogrf = syscon_regmap_lookup_by_phandle(np, "rockchip,vo-grf");
	if (IS_ERR(udphy->vogrf))
		return dev_err_probe(dev, PTR_ERR(udphy->vogrf), "failed to get vo-grf\n");

	ret = rk_udphy_parse_lane_mux_data(udphy);
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
		udphy->hs = maximum_speed <= USB_SPEED_HIGH;
	}

	ret = rk_udphy_clk_init(udphy, dev);
	if (ret)
		return ret;

	return rk_udphy_reset_init(udphy, dev);
}

static int rk_udphy_power_on(struct rk_udphy *udphy, u8 mode)
{
	int ret;

	if (!(udphy->mode & mode)) {
		dev_info(udphy->dev, "mode 0x%02x is not support\n", mode);
		return 0;
	}

	if (udphy->status == UDPHY_MODE_NONE) {
		udphy->mode_change = false;
		ret = rk_udphy_setup(udphy);
		if (ret)
			return ret;

		if (udphy->mode & UDPHY_MODE_USB)
			rk_udphy_u3_port_disable(udphy, false);
	} else if (udphy->mode_change) {
		udphy->mode_change = false;
		udphy->status = UDPHY_MODE_NONE;
		if (udphy->mode == UDPHY_MODE_DP)
			rk_udphy_u3_port_disable(udphy, true);

		rk_udphy_disable(udphy);
		ret = rk_udphy_setup(udphy);
		if (ret)
			return ret;
	}

	udphy->status |= mode;

	return 0;
}

static void rk_udphy_power_off(struct rk_udphy *udphy, u8 mode)
{
	if (!(udphy->mode & mode)) {
		dev_info(udphy->dev, "mode 0x%02x is not support\n", mode);
		return;
	}

	if (!udphy->status)
		return;

	udphy->status &= ~mode;

	if (udphy->status == UDPHY_MODE_NONE)
		rk_udphy_disable(udphy);
}

static int rk_udphy_dp_phy_init(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);

	mutex_lock(&udphy->mutex);

	udphy->dp_in_use = true;

	mutex_unlock(&udphy->mutex);

	return 0;
}

static int rk_udphy_dp_phy_exit(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);

	mutex_lock(&udphy->mutex);
	udphy->dp_in_use = false;
	mutex_unlock(&udphy->mutex);
	return 0;
}

static int rk_udphy_dp_phy_power_on(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);
	int ret, dp_lanes;

	mutex_lock(&udphy->mutex);

	dp_lanes = rk_udphy_dplane_get(udphy);
	phy_set_bus_width(phy, dp_lanes);

	ret = rk_udphy_power_on(udphy, UDPHY_MODE_DP);
	if (ret)
		goto unlock;

	rk_udphy_dplane_enable(udphy, dp_lanes);

	rk_udphy_dplane_select(udphy);

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

static int rk_udphy_dp_phy_power_off(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);

	mutex_lock(&udphy->mutex);
	rk_udphy_dplane_enable(udphy, 0);
	rk_udphy_power_off(udphy, UDPHY_MODE_DP);
	mutex_unlock(&udphy->mutex);

	return 0;
}

/*
 * Verify link rate
 */
static int rk_udphy_dp_phy_verify_link_rate(struct rk_udphy *udphy,
					    struct phy_configure_opts_dp *dp)
{
	switch (dp->link_rate) {
	case 1620:
	case 2700:
	case 5400:
	case 8100:
		udphy->link_rate = dp->link_rate;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk_udphy_dp_phy_verify_lanes(struct rk_udphy *udphy,
					struct phy_configure_opts_dp *dp)
{
	switch (dp->lanes) {
	case 1:
	case 2:
	case 4:
		/* valid lane count. */
		udphy->lanes = dp->lanes;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * If changing voltages is required, check swing and pre-emphasis
 * levels, per-lane.
 */
static int rk_udphy_dp_phy_verify_voltages(struct rk_udphy *udphy,
					   struct phy_configure_opts_dp *dp)
{
	int i;

	/* Lane count verified previously. */
	for (i = 0; i < udphy->lanes; i++) {
		if (dp->voltage[i] > 3 || dp->pre[i] > 3)
			return -EINVAL;

		/*
		 * Sum of voltage swing and pre-emphasis levels cannot
		 * exceed 3.
		 */
		if (dp->voltage[i] + dp->pre[i] > 3)
			return -EINVAL;
	}

	return 0;
}

static void rk_udphy_dp_set_voltage(struct rk_udphy *udphy, u8 bw,
				    u32 voltage, u32 pre, u32 lane)
{
	const struct rk_udphy_cfg *cfg = udphy->cfgs;
	const struct rk_udphy_dp_tx_drv_ctrl (*dp_ctrl)[4];
	u32 offset = 0x800 * lane;
	u32 val;

	if (udphy->mux)
		dp_ctrl = cfg->dp_tx_ctrl_cfg_typec[bw];
	else
		dp_ctrl = cfg->dp_tx_ctrl_cfg[bw];

	val = dp_ctrl[voltage][pre].trsv_reg0204;
	regmap_write(udphy->pma_regmap, 0x0810 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0205;
	regmap_write(udphy->pma_regmap, 0x0814 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0206;
	regmap_write(udphy->pma_regmap, 0x0818 + offset, val);

	val = dp_ctrl[voltage][pre].trsv_reg0207;
	regmap_write(udphy->pma_regmap, 0x081c + offset, val);
}

static int rk_udphy_dp_phy_configure(struct phy *phy,
				     union phy_configure_opts *opts)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);
	struct phy_configure_opts_dp *dp = &opts->dp;
	u32 i, val, lane;
	int ret;

	if (dp->set_rate) {
		ret = rk_udphy_dp_phy_verify_link_rate(udphy, dp);
		if (ret)
			return ret;
	}

	if (dp->set_lanes) {
		ret = rk_udphy_dp_phy_verify_lanes(udphy, dp);
		if (ret)
			return ret;
	}

	if (dp->set_voltages) {
		ret = rk_udphy_dp_phy_verify_voltages(udphy, dp);
		if (ret)
			return ret;
	}

	if (dp->set_rate) {
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
			dev_err(udphy->dev, "ROPLL is not lock, set_rate failed\n");
			return ret;
		}
	}

	if (dp->set_voltages) {
		for (i = 0; i < udphy->lanes; i++) {
			lane = udphy->dp_lane_sel[i];
			switch (udphy->link_rate) {
			case 1620:
			case 2700:
				regmap_update_bits(udphy->pma_regmap,
						   TRSV_ANA_TX_CLK_OFFSET_N(lane),
						   LN_ANA_TX_SER_TXCLK_INV,
						   FIELD_PREP(LN_ANA_TX_SER_TXCLK_INV,
						   udphy->lane_mux_sel[lane]));
				break;

			case 5400:
			case 8100:
				regmap_update_bits(udphy->pma_regmap,
						   TRSV_ANA_TX_CLK_OFFSET_N(lane),
						   LN_ANA_TX_SER_TXCLK_INV,
						   FIELD_PREP(LN_ANA_TX_SER_TXCLK_INV, 0x0));
				break;
			}

			rk_udphy_dp_set_voltage(udphy, udphy->bw, dp->voltage[i],
						dp->pre[i], lane);
		}
	}

	return 0;
}

static const struct phy_ops rk_udphy_dp_phy_ops = {
	.init		= rk_udphy_dp_phy_init,
	.exit		= rk_udphy_dp_phy_exit,
	.power_on	= rk_udphy_dp_phy_power_on,
	.power_off	= rk_udphy_dp_phy_power_off,
	.configure	= rk_udphy_dp_phy_configure,
	.owner		= THIS_MODULE,
};

static int rk_udphy_usb3_phy_init(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);
	int ret = 0;

	mutex_lock(&udphy->mutex);
	/* DP only or high-speed, disable U3 port */
	if (!(udphy->mode & UDPHY_MODE_USB) || udphy->hs) {
		rk_udphy_u3_port_disable(udphy, true);
		goto unlock;
	}

	ret = rk_udphy_power_on(udphy, UDPHY_MODE_USB);

unlock:
	mutex_unlock(&udphy->mutex);
	return ret;
}

static int rk_udphy_usb3_phy_exit(struct phy *phy)
{
	struct rk_udphy *udphy = phy_get_drvdata(phy);

	mutex_lock(&udphy->mutex);
	/* DP only or high-speed */
	if (!(udphy->mode & UDPHY_MODE_USB) || udphy->hs)
		goto unlock;

	rk_udphy_power_off(udphy, UDPHY_MODE_USB);

unlock:
	mutex_unlock(&udphy->mutex);
	return 0;
}

static const struct phy_ops rk_udphy_usb3_phy_ops = {
	.init		= rk_udphy_usb3_phy_init,
	.exit		= rk_udphy_usb3_phy_exit,
	.owner		= THIS_MODULE,
};

static int rk_udphy_typec_mux_set(struct typec_mux_dev *mux,
				  struct typec_mux_state *state)
{
	struct rk_udphy *udphy = typec_mux_get_drvdata(mux);
	u8 mode;

	mutex_lock(&udphy->mutex);

	switch (state->mode) {
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_E:
		udphy->lane_mux_sel[0] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[1] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[2] = PHY_LANE_MUX_DP;
		udphy->lane_mux_sel[3] = PHY_LANE_MUX_DP;
		mode = UDPHY_MODE_DP;
		break;

	case TYPEC_DP_STATE_D:
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
			rk_udphy_dp_hpd_event_trigger(udphy, false);
		} else if (data->status & DP_STATUS_IRQ_HPD) {
			rk_udphy_dp_hpd_event_trigger(udphy, false);
			usleep_range(750, 800);
			rk_udphy_dp_hpd_event_trigger(udphy, true);
		} else if (data->status & DP_STATUS_HPD_STATE) {
			if (udphy->mode != mode) {
				udphy->mode = mode;
				udphy->mode_change = true;
			}
			rk_udphy_dp_hpd_event_trigger(udphy, true);
		} else {
			rk_udphy_dp_hpd_event_trigger(udphy, false);
		}
	}

	mutex_unlock(&udphy->mutex);
	return 0;
}

static void rk_udphy_typec_mux_unregister(void *data)
{
	struct rk_udphy *udphy = data;

	typec_mux_unregister(udphy->mux);
}

static int rk_udphy_setup_typec_mux(struct rk_udphy *udphy)
{
	struct typec_mux_desc mux_desc = {};

	mux_desc.drvdata = udphy;
	mux_desc.fwnode = dev_fwnode(udphy->dev);
	mux_desc.set = rk_udphy_typec_mux_set;

	udphy->mux = typec_mux_register(udphy->dev, &mux_desc);
	if (IS_ERR(udphy->mux)) {
		dev_err(udphy->dev, "Error register typec mux: %ld\n",
			PTR_ERR(udphy->mux));
		return PTR_ERR(udphy->mux);
	}

	return devm_add_action_or_reset(udphy->dev, rk_udphy_typec_mux_unregister,
					udphy);
}

static const struct regmap_config rk_udphy_pma_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = 0x20dc,
};

static struct phy *rk_udphy_phy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct rk_udphy *udphy = dev_get_drvdata(dev);

	if (args->args_count == 0)
		return ERR_PTR(-EINVAL);

	switch (args->args[0]) {
	case PHY_TYPE_USB3:
		return udphy->phy_u3;
	case PHY_TYPE_DP:
		return udphy->phy_dp;
	}

	return ERR_PTR(-EINVAL);
}

static int rk_udphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct resource *res;
	struct rk_udphy *udphy;
	void __iomem *base;
	int id, ret;

	udphy = devm_kzalloc(dev, sizeof(*udphy), GFP_KERNEL);
	if (!udphy)
		return -ENOMEM;

	udphy->cfgs = device_get_match_data(dev);
	if (!udphy->cfgs)
		return dev_err_probe(dev, -EINVAL, "missing match data\n");

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* find the phy-id from the io address */
	udphy->id = -ENODEV;
	for (id = 0; id < udphy->cfgs->num_phys; id++) {
		if (res->start == udphy->cfgs->phy_ids[id]) {
			udphy->id = id;
			break;
		}
	}

	if (udphy->id < 0)
		return dev_err_probe(dev, -ENODEV, "no matching device found\n");

	udphy->pma_regmap = devm_regmap_init_mmio(dev, base + UDPHY_PMA,
						  &rk_udphy_pma_regmap_cfg);
	if (IS_ERR(udphy->pma_regmap))
		return PTR_ERR(udphy->pma_regmap);

	udphy->dev = dev;
	ret = rk_udphy_parse_dt(udphy);
	if (ret)
		return ret;

	ret = rk_udphy_get_initial_status(udphy);
	if (ret)
		return ret;

	mutex_init(&udphy->mutex);
	platform_set_drvdata(pdev, udphy);

	if (device_property_present(dev, "orientation-switch")) {
		ret = rk_udphy_setup_orien_switch(udphy);
		if (ret)
			return ret;
	}

	if (device_property_present(dev, "mode-switch")) {
		ret = rk_udphy_setup_typec_mux(udphy);
		if (ret)
			return ret;
	}

	udphy->phy_u3 = devm_phy_create(dev, dev->of_node, &rk_udphy_usb3_phy_ops);
	if (IS_ERR(udphy->phy_u3)) {
		ret = PTR_ERR(udphy->phy_u3);
		return dev_err_probe(dev, ret, "failed to create USB3 phy\n");
	}
	phy_set_drvdata(udphy->phy_u3, udphy);

	udphy->phy_dp = devm_phy_create(dev, dev->of_node, &rk_udphy_dp_phy_ops);
	if (IS_ERR(udphy->phy_dp)) {
		ret = PTR_ERR(udphy->phy_dp);
		return dev_err_probe(dev, ret, "failed to create DP phy\n");
	}
	phy_set_bus_width(udphy->phy_dp, rk_udphy_dplane_get(udphy));
	udphy->phy_dp->attrs.max_link_rate = 8100;
	phy_set_drvdata(udphy->phy_dp, udphy);

	phy_provider = devm_of_phy_provider_register(dev, rk_udphy_phy_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		return dev_err_probe(dev, ret, "failed to register phy provider\n");
	}

	return 0;
}

static int __maybe_unused rk_udphy_resume(struct device *dev)
{
	struct rk_udphy *udphy = dev_get_drvdata(dev);

	if (udphy->dp_sink_hpd_sel)
		rk_udphy_dp_hpd_event_trigger(udphy, udphy->dp_sink_hpd_cfg);

	return 0;
}

static const struct dev_pm_ops rk_udphy_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, rk_udphy_resume)
};

static const char * const rk_udphy_rst_list[] = {
	"init", "cmn", "lane", "pcs_apb", "pma_apb"
};

static const struct rk_udphy_cfg rk3576_udphy_cfgs = {
	.num_phys = 1,
	.phy_ids = { 0x2b010000 },
	.num_rsts = ARRAY_SIZE(rk_udphy_rst_list),
	.rst_list = rk_udphy_rst_list,
	.grfcfg	= {
		/* u2phy-grf */
		.bvalid_phy_con		= RK_UDPHY_GEN_GRF_REG(0x0010, 1, 0, 0x2, 0x3),
		.bvalid_grf_con		= RK_UDPHY_GEN_GRF_REG(0x0000, 15, 14, 0x1, 0x3),

		/* usb-grf */
		.usb3otg0_cfg		= RK_UDPHY_GEN_GRF_REG(0x0030, 15, 0, 0x1100, 0x0188),

		/* usbdpphy-grf */
		.low_pwrn		= RK_UDPHY_GEN_GRF_REG(0x0004, 13, 13, 0, 1),
		.rx_lfps		= RK_UDPHY_GEN_GRF_REG(0x0004, 14, 14, 0, 1),
	},
	.vogrfcfg = {
		{
			.hpd_trigger	= RK_UDPHY_GEN_GRF_REG(0x0000, 11, 10, 1, 3),
			.dp_lane_reg    = 0x0000,
		},
	},
	.dp_tx_ctrl_cfg = {
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_hbr2,
		rk3588_dp_tx_drv_ctrl_hbr3,
	},
	.dp_tx_ctrl_cfg_typec = {
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_rbr_hbr_typec,
		rk3588_dp_tx_drv_ctrl_hbr2,
		rk3588_dp_tx_drv_ctrl_hbr3,
	},
};

static const struct rk_udphy_cfg rk3588_udphy_cfgs = {
	.num_phys = 2,
	.phy_ids = {
		0xfed80000,
		0xfed90000,
	},
	.num_rsts = ARRAY_SIZE(rk_udphy_rst_list),
	.rst_list = rk_udphy_rst_list,
	.grfcfg	= {
		/* u2phy-grf */
		.bvalid_phy_con		= RK_UDPHY_GEN_GRF_REG(0x0008, 1, 0, 0x2, 0x3),
		.bvalid_grf_con		= RK_UDPHY_GEN_GRF_REG(0x0010, 3, 2, 0x2, 0x3),

		/* usb-grf */
		.usb3otg0_cfg		= RK_UDPHY_GEN_GRF_REG(0x001c, 15, 0, 0x1100, 0x0188),
		.usb3otg1_cfg		= RK_UDPHY_GEN_GRF_REG(0x0034, 15, 0, 0x1100, 0x0188),

		/* usbdpphy-grf */
		.low_pwrn		= RK_UDPHY_GEN_GRF_REG(0x0004, 13, 13, 0, 1),
		.rx_lfps		= RK_UDPHY_GEN_GRF_REG(0x0004, 14, 14, 0, 1),
	},
	.vogrfcfg = {
		{
			.hpd_trigger	= RK_UDPHY_GEN_GRF_REG(0x0000, 11, 10, 1, 3),
			.dp_lane_reg	= 0x0000,
		},
		{
			.hpd_trigger	= RK_UDPHY_GEN_GRF_REG(0x0008, 11, 10, 1, 3),
			.dp_lane_reg	= 0x0008,
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
};

static const struct of_device_id rk_udphy_dt_match[] = {
	{
		.compatible = "rockchip,rk3576-usbdp-phy",
		.data = &rk3576_udphy_cfgs
	},
	{
		.compatible = "rockchip,rk3588-usbdp-phy",
		.data = &rk3588_udphy_cfgs
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rk_udphy_dt_match);

static struct platform_driver rk_udphy_driver = {
	.probe		= rk_udphy_probe,
	.driver		= {
		.name	= "rockchip-usbdp-phy",
		.of_match_table = rk_udphy_dt_match,
		.pm = &rk_udphy_pm_ops,
	},
};
module_platform_driver(rk_udphy_driver);

MODULE_AUTHOR("Frank Wang <frank.wang@rock-chips.com>");
MODULE_AUTHOR("Zhang Yubing <yubing.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip USBDP Combo PHY driver");
MODULE_LICENSE("GPL");
