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
#include <linux/of_gpio.h>
#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <dt-bindings/net/qca-ar803x.h>

#define AT803X_SPECIFIC_STATUS			0x11
#define AT803X_SS_SPEED_MASK			(3 << 14)
#define AT803X_SS_SPEED_1000			(2 << 14)
#define AT803X_SS_SPEED_100			(1 << 14)
#define AT803X_SS_SPEED_10			(0 << 14)
#define AT803X_SS_DUPLEX			BIT(13)
#define AT803X_SS_SPEED_DUPLEX_RESOLVED		BIT(11)
#define AT803X_SS_MDIX				BIT(6)

#define AT803X_INTR_ENABLE			0x12
#define AT803X_INTR_ENABLE_AUTONEG_ERR		BIT(15)
#define AT803X_INTR_ENABLE_SPEED_CHANGED	BIT(14)
#define AT803X_INTR_ENABLE_DUPLEX_CHANGED	BIT(13)
#define AT803X_INTR_ENABLE_PAGE_RECEIVED	BIT(12)
#define AT803X_INTR_ENABLE_LINK_FAIL		BIT(11)
#define AT803X_INTR_ENABLE_LINK_SUCCESS		BIT(10)
#define AT803X_INTR_ENABLE_WIRESPEED_DOWNGRADE	BIT(5)
#define AT803X_INTR_ENABLE_POLARITY_CHANGED	BIT(1)
#define AT803X_INTR_ENABLE_WOL			BIT(0)

#define AT803X_INTR_STATUS			0x13

#define AT803X_SMART_SPEED			0x14
#define AT803X_LED_CONTROL			0x18

#define AT803X_DEVICE_ADDR			0x03
#define AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A
#define AT803X_REG_CHIP_CONFIG			0x1f
#define AT803X_BT_BX_REG_SEL			0x8000

#define AT803X_DEBUG_ADDR			0x1D
#define AT803X_DEBUG_DATA			0x1E

#define AT803X_MODE_CFG_MASK			0x0F
#define AT803X_MODE_CFG_SGMII			0x01

#define AT803X_PSSR			0x11	/*PHY-Specific Status Register*/
#define AT803X_PSSR_MR_AN_COMPLETE	0x0200

#define AT803X_DEBUG_REG_0			0x00
#define AT803X_DEBUG_RX_CLK_DLY_EN		BIT(15)

#define AT803X_DEBUG_REG_5			0x05
#define AT803X_DEBUG_TX_CLK_DLY_EN		BIT(8)

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

#define ATH9331_PHY_ID 0x004dd041
#define ATH8030_PHY_ID 0x004dd076
#define ATH8031_PHY_ID 0x004dd074
#define ATH8035_PHY_ID 0x004dd072
#define AT803X_PHY_ID_MASK			0xffffffef

MODULE_DESCRIPTION("Qualcomm Atheros AR803x PHY driver");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_LICENSE("GPL");

struct at803x_priv {
	int flags;
#define AT803X_KEEP_PLL_ENABLED	BIT(0)	/* don't turn off internal PLL */
	u16 clk_25m_reg;
	u16 clk_25m_mask;
	struct regulator_dev *vddio_rdev;
	struct regulator_dev *vddh_rdev;
	struct regulator *vddio;
};

struct at803x_context {
	u16 bmcr;
	u16 advertise;
	u16 control1000;
	u16 int_enable;
	u16 smart_speed;
	u16 led_control;
};

static int at803x_debug_reg_read(struct phy_device *phydev, u16 reg)
{
	int ret;

	ret = phy_write(phydev, AT803X_DEBUG_ADDR, reg);
	if (ret < 0)
		return ret;

	return phy_read(phydev, AT803X_DEBUG_DATA);
}

static int at803x_debug_reg_mask(struct phy_device *phydev, u16 reg,
				 u16 clear, u16 set)
{
	u16 val;
	int ret;

	ret = at803x_debug_reg_read(phydev, reg);
	if (ret < 0)
		return ret;

	val = ret & 0xffff;
	val &= ~clear;
	val |= set;

	return phy_write(phydev, AT803X_DEBUG_DATA, val);
}

static int at803x_enable_rx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_0, 0,
				     AT803X_DEBUG_RX_CLK_DLY_EN);
}

static int at803x_enable_tx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_5, 0,
				     AT803X_DEBUG_TX_CLK_DLY_EN);
}

static int at803x_disable_rx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_0,
				     AT803X_DEBUG_RX_CLK_DLY_EN, 0);
}

static int at803x_disable_tx_delay(struct phy_device *phydev)
{
	return at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_5,
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

static int at803x_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	const u8 *mac;
	int ret;
	u32 value;
	unsigned int i, offsets[] = {
		AT803X_LOC_MAC_ADDR_32_47_OFFSET,
		AT803X_LOC_MAC_ADDR_16_31_OFFSET,
		AT803X_LOC_MAC_ADDR_0_15_OFFSET,
	};

	if (!ndev)
		return -ENODEV;

	if (wol->wolopts & WAKE_MAGIC) {
		mac = (const u8 *) ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		for (i = 0; i < 3; i++)
			phy_write_mmd(phydev, AT803X_DEVICE_ADDR, offsets[i],
				      mac[(i * 2) + 1] | (mac[(i * 2)] << 8));

		value = phy_read(phydev, AT803X_INTR_ENABLE);
		value |= AT803X_INTR_ENABLE_WOL;
		ret = phy_write(phydev, AT803X_INTR_ENABLE, value);
		if (ret)
			return ret;
		value = phy_read(phydev, AT803X_INTR_STATUS);
	} else {
		value = phy_read(phydev, AT803X_INTR_ENABLE);
		value &= (~AT803X_INTR_ENABLE_WOL);
		ret = phy_write(phydev, AT803X_INTR_ENABLE, value);
		if (ret)
			return ret;
		value = phy_read(phydev, AT803X_INTR_STATUS);
	}

	return ret;
}

static void at803x_get_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	u32 value;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	value = phy_read(phydev, AT803X_INTR_ENABLE);
	if (value & AT803X_INTR_ENABLE_WOL)
		wol->wolopts |= WAKE_MAGIC;
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

static int at803x_rgmii_reg_set_voltage_sel(struct regulator_dev *rdev,
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

static int at803x_rgmii_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	struct phy_device *phydev = rdev_get_drvdata(rdev);
	int val;

	val = at803x_debug_reg_read(phydev, AT803X_DEBUG_REG_1F);
	if (val < 0)
		return val;

	return (val & AT803X_DEBUG_RGMII_1V8) ? 1 : 0;
}

static struct regulator_ops vddio_regulator_ops = {
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = at803x_rgmii_reg_set_voltage_sel,
	.get_voltage_sel = at803x_rgmii_reg_get_voltage_sel,
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

static struct regulator_ops vddh_regulator_ops = {
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

static bool at803x_match_phy_id(struct phy_device *phydev, u32 phy_id)
{
	return (phydev->phy_id & phydev->drv->phy_id_mask)
		== (phy_id & phydev->drv->phy_id_mask);
}

static int at803x_parse_dt(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct at803x_priv *priv = phydev->priv;
	unsigned int sel, mask;
	u32 freq, strength;
	int ret;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return 0;

	ret = of_property_read_u32(node, "qca,clk-out-frequency", &freq);
	if (!ret) {
		mask = AT803X_CLK_OUT_MASK;
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

		priv->clk_25m_reg |= FIELD_PREP(mask, sel);
		priv->clk_25m_mask |= mask;

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
		if (at803x_match_phy_id(phydev, ATH8030_PHY_ID) ||
		    at803x_match_phy_id(phydev, ATH8035_PHY_ID)) {
			priv->clk_25m_reg &= ~AT8035_CLK_OUT_MASK;
			priv->clk_25m_mask &= ~AT8035_CLK_OUT_MASK;
		}
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

	/* Only supported on AR8031/AR8033, the AR8030/AR8035 use strapping
	 * options.
	 */
	if (at803x_match_phy_id(phydev, ATH8031_PHY_ID)) {
		if (of_property_read_bool(node, "qca,keep-pll-enabled"))
			priv->flags |= AT803X_KEEP_PLL_ENABLED;

		ret = at8031_register_regulators(phydev);
		if (ret < 0)
			return ret;

		priv->vddio = devm_regulator_get_optional(&phydev->mdio.dev,
							  "vddio");
		if (IS_ERR(priv->vddio)) {
			phydev_err(phydev, "failed to get VDDIO regulator\n");
			return PTR_ERR(priv->vddio);
		}

		ret = regulator_enable(priv->vddio);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int at803x_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct at803x_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return at803x_parse_dt(phydev);
}

static int at803x_clk_out_config(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int val;

	if (!priv->clk_25m_mask)
		return 0;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, AT803X_MMD7_CLK25M);
	if (val < 0)
		return val;

	val &= ~priv->clk_25m_mask;
	val |= priv->clk_25m_reg;

	return phy_write_mmd(phydev, MDIO_MMD_AN, AT803X_MMD7_CLK25M, val);
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

	ret = at803x_clk_out_config(phydev);
	if (ret < 0)
		return ret;

	if (at803x_match_phy_id(phydev, ATH8031_PHY_ID)) {
		ret = at8031_pll_config(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int at803x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, AT803X_INTR_STATUS);

	return (err < 0) ? err : 0;
}

static int at803x_config_intr(struct phy_device *phydev)
{
	int err;
	int value;

	value = phy_read(phydev, AT803X_INTR_ENABLE);

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		value |= AT803X_INTR_ENABLE_AUTONEG_ERR;
		value |= AT803X_INTR_ENABLE_SPEED_CHANGED;
		value |= AT803X_INTR_ENABLE_DUPLEX_CHANGED;
		value |= AT803X_INTR_ENABLE_LINK_FAIL;
		value |= AT803X_INTR_ENABLE_LINK_SUCCESS;

		err = phy_write(phydev, AT803X_INTR_ENABLE, value);
	}
	else
		err = phy_write(phydev, AT803X_INTR_ENABLE, 0);

	return err;
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
		msleep(1);
		phy_device_reset(phydev, 0);
		msleep(1);

		at803x_context_restore(phydev, &context);

		phydev_dbg(phydev, "%s(): phy was reset\n", __func__);
	}
}

static int at803x_aneg_done(struct phy_device *phydev)
{
	int ccr;

	int aneg_done = genphy_aneg_done(phydev);
	if (aneg_done != BMSR_ANEGCOMPLETE)
		return aneg_done;

	/*
	 * in SGMII mode, if copper side autoneg is successful,
	 * also check SGMII side autoneg result
	 */
	ccr = phy_read(phydev, AT803X_REG_CHIP_CONFIG);
	if ((ccr & AT803X_MODE_CFG_MASK) != AT803X_MODE_CFG_SGMII)
		return aneg_done;

	/* switch to SGMII/fiber page */
	phy_write(phydev, AT803X_REG_CHIP_CONFIG, ccr & ~AT803X_BT_BX_REG_SEL);

	/* check if the SGMII link is OK. */
	if (!(phy_read(phydev, AT803X_PSSR) & AT803X_PSSR_MR_AN_COMPLETE)) {
		phydev_warn(phydev, "803x_aneg_done: SGMII link is not ok\n");
		aneg_done = 0;
	}
	/* switch back to copper page */
	phy_write(phydev, AT803X_REG_CHIP_CONFIG, ccr | AT803X_BT_BX_REG_SEL);

	return aneg_done;
}

static int at803x_read_status(struct phy_device *phydev)
{
	int ss, err, old_link = phydev->link;

	/* Update the link, but return if there was an error */
	err = genphy_update_link(phydev);
	if (err)
		return err;

	/* why bother the PHY if nothing can have changed */
	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	err = genphy_read_lpa(phydev);
	if (err < 0)
		return err;

	/* Read the AT8035 PHY-Specific Status register, which indicates the
	 * speed and duplex that the PHY is actually using, irrespective of
	 * whether we are in autoneg mode or not.
	 */
	ss = phy_read(phydev, AT803X_SPECIFIC_STATUS);
	if (ss < 0)
		return ss;

	if (ss & AT803X_SS_SPEED_DUPLEX_RESOLVED) {
		switch (ss & AT803X_SS_SPEED_MASK) {
		case AT803X_SS_SPEED_10:
			phydev->speed = SPEED_10;
			break;
		case AT803X_SS_SPEED_100:
			phydev->speed = SPEED_100;
			break;
		case AT803X_SS_SPEED_1000:
			phydev->speed = SPEED_1000;
			break;
		}
		if (ss & AT803X_SS_DUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
		if (ss & AT803X_SS_MDIX)
			phydev->mdix = ETH_TP_MDI_X;
		else
			phydev->mdix = ETH_TP_MDI;
	}

	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete)
		phy_resolve_aneg_pause(phydev);

	return 0;
}

static struct phy_driver at803x_driver[] = {
{
	/* Qualcomm Atheros AR8035 */
	.phy_id			= ATH8035_PHY_ID,
	.name			= "Qualcomm Atheros AR8035",
	.phy_id_mask		= AT803X_PHY_ID_MASK,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_GBIT_FEATURES */
	.read_status		= at803x_read_status,
	.ack_interrupt		= at803x_ack_interrupt,
	.config_intr		= at803x_config_intr,
}, {
	/* Qualcomm Atheros AR8030 */
	.phy_id			= ATH8030_PHY_ID,
	.name			= "Qualcomm Atheros AR8030",
	.phy_id_mask		= AT803X_PHY_ID_MASK,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_BASIC_FEATURES */
	.ack_interrupt		= at803x_ack_interrupt,
	.config_intr		= at803x_config_intr,
}, {
	/* Qualcomm Atheros AR8031/AR8033 */
	.phy_id			= ATH8031_PHY_ID,
	.name			= "Qualcomm Atheros AR8031/AR8033",
	.phy_id_mask		= AT803X_PHY_ID_MASK,
	.probe			= at803x_probe,
	.config_init		= at803x_config_init,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_GBIT_FEATURES */
	.read_status		= at803x_read_status,
	.aneg_done		= at803x_aneg_done,
	.ack_interrupt		= &at803x_ack_interrupt,
	.config_intr		= &at803x_config_intr,
}, {
	/* ATHEROS AR9331 */
	PHY_ID_MATCH_EXACT(ATH9331_PHY_ID),
	.name			= "Qualcomm Atheros AR9331 built-in PHY",
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_BASIC_FEATURES */
	.ack_interrupt		= &at803x_ack_interrupt,
	.config_intr		= &at803x_config_intr,
} };

module_phy_driver(at803x_driver);

static struct mdio_device_id __maybe_unused atheros_tbl[] = {
	{ ATH8030_PHY_ID, AT803X_PHY_ID_MASK },
	{ ATH8031_PHY_ID, AT803X_PHY_ID_MASK },
	{ ATH8035_PHY_ID, AT803X_PHY_ID_MASK },
	{ PHY_ID_MATCH_EXACT(ATH9331_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, atheros_tbl);
