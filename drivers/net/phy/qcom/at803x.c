// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/at803x.c
 *
 * Driver for Qualcomm Atheros AR803x PHY
 *
 * Author: Matus Ujhelyi <ujhelyi.m@gmail.com>
 */

#include <linux/phy.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool_netlink.h>
#include <linux/bitfield.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/phylink.h>
#include <linux/sfp.h>
#include <dt-bindings/net/qca-ar803x.h>

#include "qcom.h"

#define AT803X_LED_CONTROL			0x18

#define AT803X_PHY_MMD3_WOL_CTRL		0x8012
#define AT803X_WOL_EN				BIT(5)

#define AT803X_REG_CHIP_CONFIG			0x1f
#define AT803X_BT_BX_REG_SEL			0x8000

#define AT803X_MODE_CFG_MASK			0x0F
#define AT803X_MODE_CFG_BASET_RGMII		0x00
#define AT803X_MODE_CFG_BASET_SGMII		0x01
#define AT803X_MODE_CFG_BX1000_RGMII_50OHM	0x02
#define AT803X_MODE_CFG_BX1000_RGMII_75OHM	0x03
#define AT803X_MODE_CFG_BX1000_CONV_50OHM	0x04
#define AT803X_MODE_CFG_BX1000_CONV_75OHM	0x05
#define AT803X_MODE_CFG_FX100_RGMII_50OHM	0x06
#define AT803X_MODE_CFG_FX100_CONV_50OHM	0x07
#define AT803X_MODE_CFG_RGMII_AUTO_MDET		0x0B
#define AT803X_MODE_CFG_FX100_RGMII_75OHM	0x0E
#define AT803X_MODE_CFG_FX100_CONV_75OHM	0x0F

#define AT803X_PSSR				0x11	/*PHY-Specific Status Register*/
#define AT803X_PSSR_MR_AN_COMPLETE		0x0200

#define AT803X_DEBUG_REG_1F			0x1F
#define AT803X_DEBUG_PLL_ON			BIT(2)
#define AT803X_DEBUG_RGMII_1V8			BIT(3)

/* AT803x supports either the XTAL input pad, an internal PLL or the
 * DSP as clock reference for the clock output pad. The XTAL reference
 * is only used for 25 MHz output, all other frequencies need the PLL.
 * The DSP as a clock reference is used in synchronous ethernet
 * applications.
 *
 * By default the PLL is only enabled if there is a link. Otherwise
 * the PHY will go into low power state and disabled the PLL. You can
 * set the PLL_ON bit (see debug register 0x1f) to keep the PLL always
 * enabled.
 */
#define AT803X_MMD7_CLK25M			0x8016
#define AT803X_CLK_OUT_MASK			GENMASK(4, 2)
#define AT803X_CLK_OUT_25MHZ_XTAL		0
#define AT803X_CLK_OUT_25MHZ_DSP		1
#define AT803X_CLK_OUT_50MHZ_PLL		2
#define AT803X_CLK_OUT_50MHZ_DSP		3
#define AT803X_CLK_OUT_62_5MHZ_PLL		4
#define AT803X_CLK_OUT_62_5MHZ_DSP		5
#define AT803X_CLK_OUT_125MHZ_PLL		6
#define AT803X_CLK_OUT_125MHZ_DSP		7

/* The AR8035 has another mask which is compatible with the AR8031/AR8033 mask
 * but doesn't support choosing between XTAL/PLL and DSP.
 */
#define AT8035_CLK_OUT_MASK			GENMASK(4, 3)

#define AT803X_CLK_OUT_STRENGTH_MASK		GENMASK(8, 7)
#define AT803X_CLK_OUT_STRENGTH_FULL		0
#define AT803X_CLK_OUT_STRENGTH_HALF		1
#define AT803X_CLK_OUT_STRENGTH_QUARTER		2

#define AT803X_MMD3_SMARTEEE_CTL1		0x805b
#define AT803X_MMD3_SMARTEEE_CTL2		0x805c
#define AT803X_MMD3_SMARTEEE_CTL3		0x805d
#define AT803X_MMD3_SMARTEEE_CTL3_LPI_EN	BIT(8)

#define ATH9331_PHY_ID				0x004dd041
#define ATH8030_PHY_ID				0x004dd076
#define ATH8031_PHY_ID				0x004dd074
#define ATH8032_PHY_ID				0x004dd023
#define ATH8035_PHY_ID				0x004dd072
#define AT8030_PHY_ID_MASK			0xffffffef

#define QCA9561_PHY_ID				0x004dd042

#define AT803X_PAGE_FIBER			0
#define AT803X_PAGE_COPPER			1

/* don't turn off internal PLL */
#define AT803X_KEEP_PLL_ENABLED			BIT(0)
#define AT803X_DISABLE_SMARTEEE			BIT(1)

/* disable hibernation mode */
#define AT803X_DISABLE_HIBERNATION_MODE		BIT(2)

MODULE_DESCRIPTION("Qualcomm Atheros AR803x PHY driver");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_LICENSE("GPL");

struct at803x_priv {
	int flags;
	u16 clk_25m_reg;
	u16 clk_25m_mask;
	u8 smarteee_lpi_tw_1g;
	u8 smarteee_lpi_tw_100m;
	bool is_fiber;
	bool is_1000basex;
	struct regulator_dev *vddio_rdev;
	struct regulator_dev *vddh_rdev;
};

struct at803x_context {
	u16 bmcr;
	u16 advertise;
	u16 control1000;
	u16 int_enable;
	u16 smart_speed;
	u16 led_control;
};

static int at803x_write_page(struct phy_device *phydev, int page)
{
	int mask;
	int set;

	if (page == AT803X_PAGE_COPPER) {
		set = AT803X_BT_BX_REG_SEL;
		mask = 0;
	} else {
		set = 0;
		mask = AT803X_BT_BX_REG_SEL;
	}

	return __phy_modify(phydev, AT803X_REG_CHIP_CONFIG, mask, set);
}

static int at803x_read_page(struct phy_device *phydev)
{
	int ccr = __phy_read(phydev, AT803X_REG_CHIP_CONFIG);

	if (ccr < 0)
		return ccr;

	if (ccr & AT803X_BT_BX_REG_SEL)
		return AT803X_PAGE_COPPER;

	return AT803X_PAGE_FIBER;
}

static int at803x_enable_rx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL, 0,
				     AT803X_DEBUG_RX_CLK_DLY_EN);
}

static int at803x_enable_tx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_SYSTEM_CTRL_MODE, 0,
				     AT803X_DEBUG_TX_CLK_DLY_EN);
}

static int at803x_disable_rx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
				     AT803X_DEBUG_RX_CLK_DLY_EN, 0);
}

static int at803x_disable_tx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_SYSTEM_CTRL_MODE,
				     AT803X_DEBUG_TX_CLK_DLY_EN, 0);
}

/* save relevant PHY registers to private copy */
static void at803x_context_save(struct phy_device *phydev,
				struct at803x_context *context)
{
	context->bmcr = phy_read(phydev, MII_BMCR);
	context->advertise = phy_read(phydev, MII_ADVERTISE);
	context->control1000 = phy_read(phydev, MII_CTRL1000);
	context->int_enable = phy_read(phydev, AT803X_INTR_ENABLE);
	context->smart_speed = phy_read(phydev, AT803X_SMART_SPEED);
	context->led_control = phy_read(phydev, AT803X_LED_CONTROL);
}

/* restore relevant PHY registers from private copy */
static void at803x_context_restore(struct phy_device *phydev,
				   const struct at803x_context *context)
{
	phy_write(phydev, MII_BMCR, context->bmcr);
	phy_write(phydev, MII_ADVERTISE, context->advertise);
	phy_write(phydev, MII_CTRL1000, context->control1000);
	phy_write(phydev, AT803X_INTR_ENABLE, context->int_enable);
	phy_write(phydev, AT803X_SMART_SPEED, context->smart_speed);
	phy_write(phydev, AT803X_LED_CONTROL, context->led_control);
}

static int at803x_suspend(struct phy_device *phydev)
{
	int value;
	int wol_enabled;

	value = phy_read(phydev, AT803X_INTR_ENABLE);
	wol_enabled = value & AT803X_INTR_ENABLE_WOL;

	if (wol_enabled)
		value = BMCR_ISOLATE;
	else
		value = BMCR_PDOWN;

	phy_modify(phydev, MII_BMCR, 0, value);

	return 0;
}

static int at803x_resume(struct phy_device *phydev)
{
	return phy_modify(phydev, MII_BMCR, BMCR_PDOWN | BMCR_ISOLATE, 0);
}

static int at803x_parse_dt(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct at803x_priv *priv = phydev->priv;
	u32 freq, strength, tw;
	unsigned int sel;
	int ret;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return 0;

	if (of_property_read_bool(node, "qca,disable-smarteee"))
		priv->flags |= AT803X_DISABLE_SMARTEEE;

	if (of_property_read_bool(node, "qca,disable-hibernation-mode"))
		priv->flags |= AT803X_DISABLE_HIBERNATION_MODE;

	if (!of_property_read_u32(node, "qca,smarteee-tw-us-1g", &tw)) {
		if (!tw || tw > 255) {
			phydev_err(phydev, "invalid qca,smarteee-tw-us-1g\n");
			return -EINVAL;
		}
		priv->smarteee_lpi_tw_1g = tw;
	}

	if (!of_property_read_u32(node, "qca,smarteee-tw-us-100m", &tw)) {
		if (!tw || tw > 255) {
			phydev_err(phydev, "invalid qca,smarteee-tw-us-100m\n");
			return -EINVAL;
		}
		priv->smarteee_lpi_tw_100m = tw;
	}

	ret = of_property_read_u32(node, "qca,clk-out-frequency", &freq);
	if (!ret) {
		switch (freq) {
		case 25000000:
			sel = AT803X_CLK_OUT_25MHZ_XTAL;
			break;
		case 50000000:
			sel = AT803X_CLK_OUT_50MHZ_PLL;
			break;
		case 62500000:
			sel = AT803X_CLK_OUT_62_5MHZ_PLL;
			break;
		case 125000000:
			sel = AT803X_CLK_OUT_125MHZ_PLL;
			break;
		default:
			phydev_err(phydev, "invalid qca,clk-out-frequency\n");
			return -EINVAL;
		}

		priv->clk_25m_reg |= FIELD_PREP(AT803X_CLK_OUT_MASK, sel);
		priv->clk_25m_mask |= AT803X_CLK_OUT_MASK;
	}

	ret = of_property_read_u32(node, "qca,clk-out-strength", &strength);
	if (!ret) {
		priv->clk_25m_mask |= AT803X_CLK_OUT_STRENGTH_MASK;
		switch (strength) {
		case AR803X_STRENGTH_FULL:
			priv->clk_25m_reg |= AT803X_CLK_OUT_STRENGTH_FULL;
			break;
		case AR803X_STRENGTH_HALF:
			priv->clk_25m_reg |= AT803X_CLK_OUT_STRENGTH_HALF;
			break;
		case AR803X_STRENGTH_QUARTER:
			priv->clk_25m_reg |= AT803X_CLK_OUT_STRENGTH_QUARTER;
			break;
		default:
			phydev_err(phydev, "invalid qca,clk-out-strength\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int at803x_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct at803x_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	ret = at803x_parse_dt(phydev);
	if (ret)
		return ret;

	return 0;
}

static int at803x_get_features(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int err;

	err = genphy_read_abilities(phydev);
	if (err)
		return err;

	if (phydev->drv->phy_id != ATH8031_PHY_ID)
		return 0;

	/* AR8031/AR8033 have different status registers
	 * for copper and fiber operation. However, the
	 * extended status register is the same for both
	 * operation modes.
	 *
	 * As a result of that, ESTATUS_1000_XFULL is set
	 * to 1 even when operating in copper TP mode.
	 *
	 * Remove this mode from the supported link modes
	 * when not operating in 1000BaseX mode.
	 */
	if (!priv->is_1000basex)
		linkmode_clear_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				   phydev->supported);

	return 0;
}

static int at803x_smarteee_config(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	u16 mask = 0, val = 0;
	int ret;

	if (priv->flags & AT803X_DISABLE_SMARTEEE)
		return phy_modify_mmd(phydev, MDIO_MMD_PCS,
				      AT803X_MMD3_SMARTEEE_CTL3,
				      AT803X_MMD3_SMARTEEE_CTL3_LPI_EN, 0);

	if (priv->smarteee_lpi_tw_1g) {
		mask |= 0xff00;
		val |= priv->smarteee_lpi_tw_1g << 8;
	}
	if (priv->smarteee_lpi_tw_100m) {
		mask |= 0x00ff;
		val |= priv->smarteee_lpi_tw_100m;
	}
	if (!mask)
		return 0;

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, AT803X_MMD3_SMARTEEE_CTL1,
			     mask, val);
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_PCS, AT803X_MMD3_SMARTEEE_CTL3,
			      AT803X_MMD3_SMARTEEE_CTL3_LPI_EN,
			      AT803X_MMD3_SMARTEEE_CTL3_LPI_EN);
}

static int at803x_clk_out_config(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	if (!priv->clk_25m_mask)
		return 0;

	return phy_modify_mmd(phydev, MDIO_MMD_AN, AT803X_MMD7_CLK25M,
			      priv->clk_25m_mask, priv->clk_25m_reg);
}

static int at8031_pll_config(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	/* The default after hardware reset is PLL OFF. After a soft reset, the
	 * values are retained.
	 */
	if (priv->flags & AT803X_KEEP_PLL_ENABLED)
		return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_1F,
					     0, AT803X_DEBUG_PLL_ON);
	else
		return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_1F,
					     AT803X_DEBUG_PLL_ON, 0);
}

static int at803x_hibernation_mode_config(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	/* The default after hardware reset is hibernation mode enabled. After
	 * software reset, the value is retained.
	 */
	if (!(priv->flags & AT803X_DISABLE_HIBERNATION_MODE))
		return 0;

	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_HIB_CTRL,
					 AT803X_DEBUG_HIB_CTRL_PS_HIB_EN, 0);
}

static int at803x_config_init(struct phy_device *phydev)
{
	int ret;

	/* The RX and TX delay default is:
	 *   after HW reset: RX delay enabled and TX delay disabled
	 *   after SW reset: RX delay enabled, while TX delay retains the
	 *   value before reset.
	 */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
		ret = at803x_enable_rx_delay(phydev);
	else
		ret = at803x_disable_rx_delay(phydev);
	if (ret < 0)
		return ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
		ret = at803x_enable_tx_delay(phydev);
	else
		ret = at803x_disable_tx_delay(phydev);
	if (ret < 0)
		return ret;

	ret = at803x_smarteee_config(phydev);
	if (ret < 0)
		return ret;

	ret = at803x_clk_out_config(phydev);
	if (ret < 0)
		return ret;

	ret = at803x_hibernation_mode_config(phydev);
	if (ret < 0)
		return ret;

	/* Ar803x extended next page bit is enabled by default. Cisco
	 * multigig switches read this bit and attempt to negotiate 10Gbps
	 * rates even if the next page bit is disabled. This is incorrect
	 * behaviour but we still need to accommodate it. XNP is only needed
	 * for 10Gbps support, so disable XNP.
	 */
	return phy_modify(phydev, MII_ADVERTISE, MDIO_AN_CTRL1_XNP, 0);
}

static void at803x_link_change_notify(struct phy_device *phydev)
{
	/*
	 * Conduct a hardware reset for AT8030 every time a link loss is
	 * signalled. This is necessary to circumvent a hardware bug that
	 * occurs when the cable is unplugged while TX packets are pending
	 * in the FIFO. In such cases, the FIFO enters an error mode it
	 * cannot recover from by software.
	 */
	if (phydev->state == PHY_NOLINK && phydev->mdio.reset_gpio) {
		struct at803x_context context;

		at803x_context_save(phydev, &context);

		phy_device_reset(phydev, 1);
		usleep_range(1000, 2000);
		phy_device_reset(phydev, 0);
		usleep_range(1000, 2000);

		at803x_context_restore(phydev, &context);

		phydev_dbg(phydev, "%s(): phy was reset\n", __func__);
	}
}

static int at803x_config_aneg(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int ret;

	ret = at803x_prepare_config_aneg(phydev);
	if (ret)
		return ret;

	if (priv->is_1000basex)
		return genphy_c37_config_aneg(phydev);

	return genphy_config_aneg(phydev);
}

static int at803x_cable_test_result_trans(u16 status)
{
	switch (FIELD_GET(AT803X_CDT_STATUS_STAT_MASK, status)) {
	case AT803X_CDT_STATUS_STAT_NORMAL:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case AT803X_CDT_STATUS_STAT_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case AT803X_CDT_STATUS_STAT_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case AT803X_CDT_STATUS_STAT_FAIL:
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static bool at803x_cdt_test_failed(u16 status)
{
	return FIELD_GET(AT803X_CDT_STATUS_STAT_MASK, status) ==
		AT803X_CDT_STATUS_STAT_FAIL;
}

static bool at803x_cdt_fault_length_valid(u16 status)
{
	switch (FIELD_GET(AT803X_CDT_STATUS_STAT_MASK, status)) {
	case AT803X_CDT_STATUS_STAT_OPEN:
	case AT803X_CDT_STATUS_STAT_SHORT:
		return true;
	}
	return false;
}

static int at803x_cable_test_one_pair(struct phy_device *phydev, int pair)
{
	static const int ethtool_pair[] = {
		ETHTOOL_A_CABLE_PAIR_A,
		ETHTOOL_A_CABLE_PAIR_B,
		ETHTOOL_A_CABLE_PAIR_C,
		ETHTOOL_A_CABLE_PAIR_D,
	};
	int ret, val;

	val = FIELD_PREP(AT803X_CDT_MDI_PAIR_MASK, pair) |
	      AT803X_CDT_ENABLE_TEST;
	ret = at803x_cdt_start(phydev, val);
	if (ret)
		return ret;

	ret = at803x_cdt_wait_for_completion(phydev, AT803X_CDT_ENABLE_TEST);
	if (ret)
		return ret;

	val = phy_read(phydev, AT803X_CDT_STATUS);
	if (val < 0)
		return val;

	if (at803x_cdt_test_failed(val))
		return 0;

	ethnl_cable_test_result(phydev, ethtool_pair[pair],
				at803x_cable_test_result_trans(val));

	if (at803x_cdt_fault_length_valid(val)) {
		val = FIELD_GET(AT803X_CDT_STATUS_DELTA_TIME_MASK, val);
		ethnl_cable_test_fault_length(phydev, ethtool_pair[pair],
					      at803x_cdt_fault_length(val));
	}

	return 1;
}

static int at803x_cable_test_get_status(struct phy_device *phydev,
					bool *finished, unsigned long pair_mask)
{
	int retries = 20;
	int pair, ret;

	*finished = false;

	/* According to the datasheet the CDT can be performed when
	 * there is no link partner or when the link partner is
	 * auto-negotiating. Starting the test will restart the AN
	 * automatically. It seems that doing this repeatedly we will
	 * get a slot where our link partner won't disturb our
	 * measurement.
	 */
	while (pair_mask && retries--) {
		for_each_set_bit(pair, &pair_mask, 4) {
			ret = at803x_cable_test_one_pair(phydev, pair);
			if (ret < 0)
				return ret;
			if (ret)
				clear_bit(pair, &pair_mask);
		}
		if (pair_mask)
			msleep(250);
	}

	*finished = true;

	return 0;
}

static void at803x_cable_test_autoneg(struct phy_device *phydev)
{
	/* Enable auto-negotiation, but advertise no capabilities, no link
	 * will be established. A restart of the auto-negotiation is not
	 * required, because the cable test will automatically break the link.
	 */
	phy_write(phydev, MII_BMCR, BMCR_ANENABLE);
	phy_write(phydev, MII_ADVERTISE, ADVERTISE_CSMA);
}

static int at803x_cable_test_start(struct phy_device *phydev)
{
	at803x_cable_test_autoneg(phydev);
	/* we do all the (time consuming) work later */
	return 0;
}

static int at8031_rgmii_reg_set_voltage_sel(struct regulator_dev *rdev,
					    unsigned int selector)
{
	struct phy_device *phydev = rdev_get_drvdata(rdev);

	if (selector)
		return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_1F,
					     0, AT803X_DEBUG_RGMII_1V8);
	else
		return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_1F,
					     AT803X_DEBUG_RGMII_1V8, 0);
}

static int at8031_rgmii_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	struct phy_device *phydev = rdev_get_drvdata(rdev);
	int val;

	val = at803x_debug_reg_read(phydev, AT803X_DEBUG_REG_1F);
	if (val < 0)
		return val;

	return (val & AT803X_DEBUG_RGMII_1V8) ? 1 : 0;
}

static const struct regulator_ops vddio_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = at8031_rgmii_reg_set_voltage_sel,
	.get_voltage_sel = at8031_rgmii_reg_get_voltage_sel,
};

static const unsigned int vddio_voltage_table[] = {
	1500000,
	1800000,
};

static const struct regulator_desc vddio_desc = {
	.name = "vddio",
	.of_match = of_match_ptr("vddio-regulator"),
	.n_voltages = ARRAY_SIZE(vddio_voltage_table),
	.volt_table = vddio_voltage_table,
	.ops = &vddio_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};

static const struct regulator_ops vddh_regulator_ops = {
};

static const struct regulator_desc vddh_desc = {
	.name = "vddh",
	.of_match = of_match_ptr("vddh-regulator"),
	.n_voltages = 1,
	.fixed_uV = 2500000,
	.ops = &vddh_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};

static int at8031_register_regulators(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct regulator_config config = { };

	config.dev = dev;
	config.driver_data = phydev;

	priv->vddio_rdev = devm_regulator_register(dev, &vddio_desc, &config);
	if (IS_ERR(priv->vddio_rdev)) {
		phydev_err(phydev, "failed to register VDDIO regulator\n");
		return PTR_ERR(priv->vddio_rdev);
	}

	priv->vddh_rdev = devm_regulator_register(dev, &vddh_desc, &config);
	if (IS_ERR(priv->vddh_rdev)) {
		phydev_err(phydev, "failed to register VDDH regulator\n");
		return PTR_ERR(priv->vddh_rdev);
	}

	return 0;
}

static int at8031_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
{
	struct phy_device *phydev = upstream;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(phy_support);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(sfp_support);
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	phy_interface_t iface;

	linkmode_zero(phy_support);
	phylink_set(phy_support, 1000baseX_Full);
	phylink_set(phy_support, 1000baseT_Full);
	phylink_set(phy_support, Autoneg);
	phylink_set(phy_support, Pause);
	phylink_set(phy_support, Asym_Pause);

	linkmode_zero(sfp_support);
	sfp_parse_support(phydev->sfp_bus, id, sfp_support, interfaces);
	/* Some modules support 10G modes as well as others we support.
	 * Mask out non-supported modes so the correct interface is picked.
	 */
	linkmode_and(sfp_support, phy_support, sfp_support);

	if (linkmode_empty(sfp_support)) {
		dev_err(&phydev->mdio.dev, "incompatible SFP module inserted\n");
		return -EINVAL;
	}

	iface = sfp_select_interface(phydev->sfp_bus, sfp_support);

	/* Only 1000Base-X is supported by AR8031/8033 as the downstream SerDes
	 * interface for use with SFP modules.
	 * However, some copper modules detected as having a preferred SGMII
	 * interface do default to and function in 1000Base-X mode, so just
	 * print a warning and allow such modules, as they may have some chance
	 * of working.
	 */
	if (iface == PHY_INTERFACE_MODE_SGMII)
		dev_warn(&phydev->mdio.dev, "module may not function if 1000Base-X not supported\n");
	else if (iface != PHY_INTERFACE_MODE_1000BASEX)
		return -EINVAL;

	return 0;
}

static const struct sfp_upstream_ops at8031_sfp_ops = {
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
	.module_insert = at8031_sfp_insert,
};

static int at8031_parse_dt(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct at803x_priv *priv = phydev->priv;
	int ret;

	if (of_property_read_bool(node, "qca,keep-pll-enabled"))
		priv->flags |= AT803X_KEEP_PLL_ENABLED;

	ret = at8031_register_regulators(phydev);
	if (ret < 0)
		return ret;

	ret = devm_regulator_get_enable_optional(&phydev->mdio.dev,
						 "vddio");
	if (ret) {
		phydev_err(phydev, "failed to get VDDIO regulator\n");
		return ret;
	}

	/* Only AR8031/8033 support 1000Base-X for SFP modules */
	return phy_sfp_probe(phydev, &at8031_sfp_ops);
}

static int at8031_probe(struct phy_device *phydev)
{
	struct at803x_priv *priv;
	int mode_cfg;
	int ccr;
	int ret;

	ret = at803x_probe(phydev);
	if (ret)
		return ret;

	priv = phydev->priv;

	/* Only supported on AR8031/AR8033, the AR8030/AR8035 use strapping
	 * options.
	 */
	ret = at8031_parse_dt(phydev);
	if (ret)
		return ret;

	ccr = phy_read(phydev, AT803X_REG_CHIP_CONFIG);
	if (ccr < 0)
		return ccr;
	mode_cfg = ccr & AT803X_MODE_CFG_MASK;

	switch (mode_cfg) {
	case AT803X_MODE_CFG_BX1000_RGMII_50OHM:
	case AT803X_MODE_CFG_BX1000_RGMII_75OHM:
		priv->is_1000basex = true;
		fallthrough;
	case AT803X_MODE_CFG_FX100_RGMII_50OHM:
	case AT803X_MODE_CFG_FX100_RGMII_75OHM:
		priv->is_fiber = true;
		break;
	}

	/* Disable WoL in 1588 register which is enabled
	 * by default
	 */
	return phy_modify_mmd(phydev, MDIO_MMD_PCS,
			      AT803X_PHY_MMD3_WOL_CTRL,
			      AT803X_WOL_EN, 0);
}

static int at8031_config_init(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int ret;

	/* Some bootloaders leave the fiber page selected.
	 * Switch to the appropriate page (fiber or copper), as otherwise we
	 * read the PHY capabilities from the wrong page.
	 */
	phy_lock_mdio_bus(phydev);
	ret = at803x_write_page(phydev,
				priv->is_fiber ? AT803X_PAGE_FIBER :
						 AT803X_PAGE_COPPER);
	phy_unlock_mdio_bus(phydev);
	if (ret)
		return ret;

	ret = at8031_pll_config(phydev);
	if (ret < 0)
		return ret;

	return at803x_config_init(phydev);
}

static int at8031_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int ret;

	/* First setup MAC address and enable WOL interrupt */
	ret = at803x_set_wol(phydev, wol);
	if (ret)
		return ret;

	if (wol->wolopts & WAKE_MAGIC)
		/* Enable WOL function for 1588 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
				     AT803X_PHY_MMD3_WOL_CTRL,
				     0, AT803X_WOL_EN);
	else
		/* Disable WoL function for 1588 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS,
				     AT803X_PHY_MMD3_WOL_CTRL,
				     AT803X_WOL_EN, 0);

	return ret;
}

static int at8031_config_intr(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int err, value = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED &&
	    priv->is_fiber) {
		/* Clear any pending interrupts */
		err = at803x_ack_interrupt(phydev);
		if (err)
			return err;

		value |= AT803X_INTR_ENABLE_LINK_FAIL_BX;
		value |= AT803X_INTR_ENABLE_LINK_SUCCESS_BX;

		err = phy_set_bits(phydev, AT803X_INTR_ENABLE, value);
		if (err)
			return err;
	}

	return at803x_config_intr(phydev);
}

/* AR8031 and AR8033 share the same read status logic */
static int at8031_read_status(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	bool changed;

	if (priv->is_1000basex)
		return genphy_c37_read_status(phydev, &changed);

	return at803x_read_status(phydev);
}

/* AR8031 and AR8035 share the same cable test get status reg */
static int at8031_cable_test_get_status(struct phy_device *phydev,
					bool *finished)
{
	return at803x_cable_test_get_status(phydev, finished, 0xf);
}

/* AR8031 and AR8035 share the same cable test start logic */
static int at8031_cable_test_start(struct phy_device *phydev)
{
	at803x_cable_test_autoneg(phydev);
	phy_write(phydev, MII_CTRL1000, 0);
	/* we do all the (time consuming) work later */
	return 0;
}

/* AR8032, AR9331 and QCA9561 share the same cable test get status reg */
static int at8032_cable_test_get_status(struct phy_device *phydev,
					bool *finished)
{
	return at803x_cable_test_get_status(phydev, finished, 0x3);
}

static int at8035_parse_dt(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	/* Mask is set by the generic at803x_parse_dt
	 * if property is set. Assume property is set
	 * with the mask not zero.
	 */
	if (priv->clk_25m_mask) {
		/* Fixup for the AR8030/AR8035. This chip has another mask and
		 * doesn't support the DSP reference. Eg. the lowest bit of the
		 * mask. The upper two bits select the same frequencies. Mask
		 * the lowest bit here.
		 *
		 * Warning:
		 *   There was no datasheet for the AR8030 available so this is
		 *   just a guess. But the AR8035 is listed as pin compatible
		 *   to the AR8030 so there might be a good chance it works on
		 *   the AR8030 too.
		 */
		priv->clk_25m_reg &= AT8035_CLK_OUT_MASK;
		priv->clk_25m_mask &= AT8035_CLK_OUT_MASK;
	}

	return 0;
}

/* AR8030 and AR8035 shared the same special mask for clk_25m */
static int at8035_probe(struct phy_device *phydev)
{
	int ret;

	ret = at803x_probe(phydev);
	if (ret)
		return ret;

	return at8035_parse_dt(phydev);
}

static struct phy_driver at803x_driver[] = {
{
	/* Qualcomm Atheros AR8035 */
	PHY_ID_MATCH_EXACT(ATH8035_PHY_ID),
	.name			= "Qualcomm Atheros AR8035",
	.flags			= PHY_POLL_CABLE_TEST,
	.probe			= at8035_probe,
	.config_aneg		= at803x_config_aneg,
	.config_init		= at803x_config_init,
	.soft_reset		= genphy_soft_reset,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_GBIT_FEATURES */
	.read_status		= at803x_read_status,
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.get_tunable		= at803x_get_tunable,
	.set_tunable		= at803x_set_tunable,
	.cable_test_start	= at8031_cable_test_start,
	.cable_test_get_status	= at8031_cable_test_get_status,
}, {
	/* Qualcomm Atheros AR8030 */
	.phy_id			= ATH8030_PHY_ID,
	.name			= "Qualcomm Atheros AR8030",
	.phy_id_mask		= AT8030_PHY_ID_MASK,
	.probe			= at8035_probe,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_BASIC_FEATURES */
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
}, {
	/* Qualcomm Atheros AR8031/AR8033 */
	PHY_ID_MATCH_EXACT(ATH8031_PHY_ID),
	.name			= "Qualcomm Atheros AR8031/AR8033",
	.flags			= PHY_POLL_CABLE_TEST,
	.probe			= at8031_probe,
	.config_init		= at8031_config_init,
	.config_aneg		= at803x_config_aneg,
	.soft_reset		= genphy_soft_reset,
	.set_wol		= at8031_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.read_page		= at803x_read_page,
	.write_page		= at803x_write_page,
	.get_features		= at803x_get_features,
	.read_status		= at8031_read_status,
	.config_intr		= at8031_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.get_tunable		= at803x_get_tunable,
	.set_tunable		= at803x_set_tunable,
	.cable_test_start	= at8031_cable_test_start,
	.cable_test_get_status	= at8031_cable_test_get_status,
}, {
	/* Qualcomm Atheros AR8032 */
	PHY_ID_MATCH_EXACT(ATH8032_PHY_ID),
	.name			= "Qualcomm Atheros AR8032",
	.probe			= at803x_probe,
	.flags			= PHY_POLL_CABLE_TEST,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_BASIC_FEATURES */
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at8032_cable_test_get_status,
}, {
	/* ATHEROS AR9331 */
	PHY_ID_MATCH_EXACT(ATH9331_PHY_ID),
	.name			= "Qualcomm Atheros AR9331 built-in PHY",
	.probe			= at803x_probe,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.flags			= PHY_POLL_CABLE_TEST,
	/* PHY_BASIC_FEATURES */
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at8032_cable_test_get_status,
	.read_status		= at803x_read_status,
	.soft_reset		= genphy_soft_reset,
	.config_aneg		= at803x_config_aneg,
}, {
	/* Qualcomm Atheros QCA9561 */
	PHY_ID_MATCH_EXACT(QCA9561_PHY_ID),
	.name			= "Qualcomm Atheros QCA9561 built-in PHY",
	.probe			= at803x_probe,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.flags			= PHY_POLL_CABLE_TEST,
	/* PHY_BASIC_FEATURES */
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at8032_cable_test_get_status,
	.read_status		= at803x_read_status,
	.soft_reset		= genphy_soft_reset,
	.config_aneg		= at803x_config_aneg,
}, };

module_phy_driver(at803x_driver);

static struct mdio_device_id __maybe_unused atheros_tbl[] = {
	{ ATH8030_PHY_ID, AT8030_PHY_ID_MASK },
	{ PHY_ID_MATCH_EXACT(ATH8031_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH8032_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH8035_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH9331_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA9561_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, atheros_tbl);
