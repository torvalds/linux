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
#include <linux/of_gpio.h>
#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <dt-bindings/net/qca-ar803x.h>

#define AT803X_SPECIFIC_FUNCTION_CONTROL	0x10
#define AT803X_SFC_ASSERT_CRS			BIT(11)
#define AT803X_SFC_FORCE_LINK			BIT(10)
#define AT803X_SFC_MDI_CROSSOVER_MODE_M		GENMASK(6, 5)
#define AT803X_SFC_AUTOMATIC_CROSSOVER		0x3
#define AT803X_SFC_MANUAL_MDIX			0x1
#define AT803X_SFC_MANUAL_MDI			0x0
#define AT803X_SFC_SQE_TEST			BIT(2)
#define AT803X_SFC_POLARITY_REVERSAL		BIT(1)
#define AT803X_SFC_DISABLE_JABBER		BIT(0)

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
#define AT803X_SMART_SPEED_ENABLE		BIT(5)
#define AT803X_SMART_SPEED_RETRY_LIMIT_MASK	GENMASK(4, 2)
#define AT803X_SMART_SPEED_BYPASS_TIMER		BIT(1)
#define AT803X_CDT				0x16
#define AT803X_CDT_MDI_PAIR_MASK		GENMASK(9, 8)
#define AT803X_CDT_ENABLE_TEST			BIT(0)
#define AT803X_CDT_STATUS			0x1c
#define AT803X_CDT_STATUS_STAT_NORMAL		0
#define AT803X_CDT_STATUS_STAT_SHORT		1
#define AT803X_CDT_STATUS_STAT_OPEN		2
#define AT803X_CDT_STATUS_STAT_FAIL		3
#define AT803X_CDT_STATUS_STAT_MASK		GENMASK(9, 8)
#define AT803X_CDT_STATUS_DELTA_TIME_MASK	GENMASK(7, 0)
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

#define AT803X_PSSR				0x11	/*PHY-Specific Status Register*/
#define AT803X_PSSR_MR_AN_COMPLETE		0x0200

#define AT803X_DEBUG_REG_0			0x00
#define AT803X_DEBUG_RX_CLK_DLY_EN		BIT(15)

#define AT803X_DEBUG_REG_5			0x05
#define AT803X_DEBUG_TX_CLK_DLY_EN		BIT(8)

#define AT803X_DEBUG_REG_3C			0x3C

#define AT803X_DEBUG_REG_3D			0x3D

#define AT803X_DEBUG_REG_1F			0x1F
#define AT803X_DEBUG_PLL_ON			BIT(2)
#define AT803X_DEBUG_RGMII_1V8			BIT(3)

#define MDIO_AZ_DEBUG				0x800D

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

#define AT803X_DEFAULT_DOWNSHIFT		5
#define AT803X_MIN_DOWNSHIFT			2
#define AT803X_MAX_DOWNSHIFT			9

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

#define QCA8327_A_PHY_ID			0x004dd033
#define QCA8327_B_PHY_ID			0x004dd034
#define QCA8337_PHY_ID				0x004dd036
#define QCA8K_PHY_ID_MASK			0xffffffff

#define QCA8K_DEVFLAGS_REVISION_MASK		GENMASK(2, 0)

#define AT803X_PAGE_FIBER			0
#define AT803X_PAGE_COPPER			1

/* don't turn off internal PLL */
#define AT803X_KEEP_PLL_ENABLED			BIT(0)
#define AT803X_DISABLE_SMARTEEE			BIT(1)

MODULE_DESCRIPTION("Qualcomm Atheros AR803x PHY driver");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_LICENSE("GPL");

enum stat_access_type {
	PHY,
	MMD
};

struct at803x_hw_stat {
	const char *string;
	u8 reg;
	u32 mask;
	enum stat_access_type access_type;
};

static struct at803x_hw_stat at803x_hw_stats[] = {
	{ "phy_idle_errors", 0xa, GENMASK(7, 0), PHY},
	{ "phy_receive_errors", 0x15, GENMASK(15, 0), PHY},
	{ "eee_wake_errors", 0x16, GENMASK(15, 0), MMD},
};

struct at803x_priv {
	int flags;
	u16 clk_25m_reg;
	u16 clk_25m_mask;
	u8 smarteee_lpi_tw_1g;
	u8 smarteee_lpi_tw_100m;
	struct regulator_dev *vddio_rdev;
	struct regulator_dev *vddh_rdev;
	struct regulator *vddio;
	u64 stats[ARRAY_SIZE(at803x_hw_stats)];
};

struct at803x_context {
	u16 bmcr;
	u16 advertise;
	u16 control1000;
	u16 int_enable;
	u16 smart_speed;
	u16 led_control;
};

static int at803x_debug_reg_write(struct phy_device *phydev, u16 reg, u16 data)
{
	int ret;

	ret = phy_write(phydev, AT803X_DEBUG_ADDR, reg);
	if (ret < 0)
		return ret;

	return phy_write(phydev, AT803X_DEBUG_DATA, data);
}

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

static int at803x_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(at803x_hw_stats);
}

static void at803x_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(at803x_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			at803x_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 at803x_get_stat(struct phy_device *phydev, int i)
{
	struct at803x_hw_stat stat = at803x_hw_stats[i];
	struct at803x_priv *priv = phydev->priv;
	int val;
	u64 ret;

	if (stat.access_type == MMD)
		val = phy_read_mmd(phydev, MDIO_MMD_PCS, stat.reg);
	else
		val = phy_read(phydev, stat.reg);

	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & stat.mask;
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void at803x_get_stats(struct phy_device *phydev,
			     struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(at803x_hw_stats); i++)
		data[i] = at803x_get_stat(phydev, i);
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

static const struct regulator_ops vddio_regulator_ops = {
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
		if (phydev->drv->phy_id == ATH8030_PHY_ID ||
		    phydev->drv->phy_id == ATH8035_PHY_ID) {
			priv->clk_25m_reg &= AT8035_CLK_OUT_MASK;
			priv->clk_25m_mask &= AT8035_CLK_OUT_MASK;
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
	if (phydev->drv->phy_id == ATH8031_PHY_ID) {
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

	if (priv->vddio) {
		ret = regulator_enable(priv->vddio);
		if (ret < 0)
			return ret;
	}

	/* Some bootloaders leave the fiber page selected.
	 * Switch to the copper page, as otherwise we read
	 * the PHY capabilities from the fiber side.
	 */
	if (phydev->drv->phy_id == ATH8031_PHY_ID) {
		phy_lock_mdio_bus(phydev);
		ret = at803x_write_page(phydev, AT803X_PAGE_COPPER);
		phy_unlock_mdio_bus(phydev);
		if (ret)
			goto err;
	}

	return 0;

err:
	if (priv->vddio)
		regulator_disable(priv->vddio);

	return ret;
}

static void at803x_remove(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;

	if (priv->vddio)
		regulator_disable(priv->vddio);
}

static int at803x_get_features(struct phy_device *phydev)
{
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
	 * Remove this mode from the supported link modes,
	 * as this driver currently only supports copper
	 * operation.
	 */
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

	if (phydev->drv->phy_id == ATH8031_PHY_ID) {
		ret = at8031_pll_config(phydev);
		if (ret < 0)
			return ret;
	}

	/* Ar803x extended next page bit is enabled by default. Cisco
	 * multigig switches read this bit and attempt to negotiate 10Gbps
	 * rates even if the next page bit is disabled. This is incorrect
	 * behaviour but we still need to accommodate it. XNP is only needed
	 * for 10Gbps support, so disable XNP.
	 */
	return phy_modify(phydev, MII_ADVERTISE, MDIO_AN_CTRL1_XNP, 0);
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
		/* Clear any pending interrupts */
		err = at803x_ack_interrupt(phydev);
		if (err)
			return err;

		value |= AT803X_INTR_ENABLE_AUTONEG_ERR;
		value |= AT803X_INTR_ENABLE_SPEED_CHANGED;
		value |= AT803X_INTR_ENABLE_DUPLEX_CHANGED;
		value |= AT803X_INTR_ENABLE_LINK_FAIL;
		value |= AT803X_INTR_ENABLE_LINK_SUCCESS;

		err = phy_write(phydev, AT803X_INTR_ENABLE, value);
	} else {
		err = phy_write(phydev, AT803X_INTR_ENABLE, 0);
		if (err)
			return err;

		/* Clear any pending interrupts */
		err = at803x_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t at803x_handle_interrupt(struct phy_device *phydev)
{
	int irq_status, int_enabled;

	irq_status = phy_read(phydev, AT803X_INTR_STATUS);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* Read the current enabled interrupts */
	int_enabled = phy_read(phydev, AT803X_INTR_ENABLE);
	if (int_enabled < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	/* See if this was one of our enabled interrupts */
	if (!(irq_status & int_enabled))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
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
		int sfc;

		sfc = phy_read(phydev, AT803X_SPECIFIC_FUNCTION_CONTROL);
		if (sfc < 0)
			return sfc;

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

		switch (FIELD_GET(AT803X_SFC_MDI_CROSSOVER_MODE_M, sfc)) {
		case AT803X_SFC_MANUAL_MDI:
			phydev->mdix_ctrl = ETH_TP_MDI;
			break;
		case AT803X_SFC_MANUAL_MDIX:
			phydev->mdix_ctrl = ETH_TP_MDI_X;
			break;
		case AT803X_SFC_AUTOMATIC_CROSSOVER:
			phydev->mdix_ctrl = ETH_TP_MDI_AUTO;
			break;
		}
	}

	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete)
		phy_resolve_aneg_pause(phydev);

	return 0;
}

static int at803x_config_mdix(struct phy_device *phydev, u8 ctrl)
{
	u16 val;

	switch (ctrl) {
	case ETH_TP_MDI:
		val = AT803X_SFC_MANUAL_MDI;
		break;
	case ETH_TP_MDI_X:
		val = AT803X_SFC_MANUAL_MDIX;
		break;
	case ETH_TP_MDI_AUTO:
		val = AT803X_SFC_AUTOMATIC_CROSSOVER;
		break;
	default:
		return 0;
	}

	return phy_modify_changed(phydev, AT803X_SPECIFIC_FUNCTION_CONTROL,
			  AT803X_SFC_MDI_CROSSOVER_MODE_M,
			  FIELD_PREP(AT803X_SFC_MDI_CROSSOVER_MODE_M, val));
}

static int at803x_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = at803x_config_mdix(phydev, phydev->mdix_ctrl);
	if (ret < 0)
		return ret;

	/* Changes of the midx bits are disruptive to the normal operation;
	 * therefore any changes to these registers must be followed by a
	 * software reset to take effect.
	 */
	if (ret == 1) {
		ret = genphy_soft_reset(phydev);
		if (ret < 0)
			return ret;
	}

	return genphy_config_aneg(phydev);
}

static int at803x_get_downshift(struct phy_device *phydev, u8 *d)
{
	int val;

	val = phy_read(phydev, AT803X_SMART_SPEED);
	if (val < 0)
		return val;

	if (val & AT803X_SMART_SPEED_ENABLE)
		*d = FIELD_GET(AT803X_SMART_SPEED_RETRY_LIMIT_MASK, val) + 2;
	else
		*d = DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int at803x_set_downshift(struct phy_device *phydev, u8 cnt)
{
	u16 mask, set;
	int ret;

	switch (cnt) {
	case DOWNSHIFT_DEV_DEFAULT_COUNT:
		cnt = AT803X_DEFAULT_DOWNSHIFT;
		fallthrough;
	case AT803X_MIN_DOWNSHIFT ... AT803X_MAX_DOWNSHIFT:
		set = AT803X_SMART_SPEED_ENABLE |
		      AT803X_SMART_SPEED_BYPASS_TIMER |
		      FIELD_PREP(AT803X_SMART_SPEED_RETRY_LIMIT_MASK, cnt - 2);
		mask = AT803X_SMART_SPEED_RETRY_LIMIT_MASK;
		break;
	case DOWNSHIFT_DEV_DISABLE:
		set = 0;
		mask = AT803X_SMART_SPEED_ENABLE |
		       AT803X_SMART_SPEED_BYPASS_TIMER;
		break;
	default:
		return -EINVAL;
	}

	ret = phy_modify_changed(phydev, AT803X_SMART_SPEED, mask, set);

	/* After changing the smart speed settings, we need to perform a
	 * software reset, use phy_init_hw() to make sure we set the
	 * reapply any values which might got lost during software reset.
	 */
	if (ret == 1)
		ret = phy_init_hw(phydev);

	return ret;
}

static int at803x_get_tunable(struct phy_device *phydev,
			      struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return at803x_get_downshift(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int at803x_set_tunable(struct phy_device *phydev,
			      struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return at803x_set_downshift(phydev, *(const u8 *)data);
	default:
		return -EOPNOTSUPP;
	}
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

static int at803x_cdt_fault_length(u16 status)
{
	int dt;

	/* According to the datasheet the distance to the fault is
	 * DELTA_TIME * 0.824 meters.
	 *
	 * The author suspect the correct formula is:
	 *
	 *   fault_distance = DELTA_TIME * (c * VF) / 125MHz / 2
	 *
	 * where c is the speed of light, VF is the velocity factor of
	 * the twisted pair cable, 125MHz the counter frequency and
	 * we need to divide by 2 because the hardware will measure the
	 * round trip time to the fault and back to the PHY.
	 *
	 * With a VF of 0.69 we get the factor 0.824 mentioned in the
	 * datasheet.
	 */
	dt = FIELD_GET(AT803X_CDT_STATUS_DELTA_TIME_MASK, status);

	return (dt * 824) / 10;
}

static int at803x_cdt_start(struct phy_device *phydev, int pair)
{
	u16 cdt;

	cdt = FIELD_PREP(AT803X_CDT_MDI_PAIR_MASK, pair) |
	      AT803X_CDT_ENABLE_TEST;

	return phy_write(phydev, AT803X_CDT, cdt);
}

static int at803x_cdt_wait_for_completion(struct phy_device *phydev)
{
	int val, ret;

	/* One test run takes about 25ms */
	ret = phy_read_poll_timeout(phydev, AT803X_CDT, val,
				    !(val & AT803X_CDT_ENABLE_TEST),
				    30000, 100000, true);

	return ret < 0 ? ret : 0;
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

	ret = at803x_cdt_start(phydev, pair);
	if (ret)
		return ret;

	ret = at803x_cdt_wait_for_completion(phydev);
	if (ret)
		return ret;

	val = phy_read(phydev, AT803X_CDT_STATUS);
	if (val < 0)
		return val;

	if (at803x_cdt_test_failed(val))
		return 0;

	ethnl_cable_test_result(phydev, ethtool_pair[pair],
				at803x_cable_test_result_trans(val));

	if (at803x_cdt_fault_length_valid(val))
		ethnl_cable_test_fault_length(phydev, ethtool_pair[pair],
					      at803x_cdt_fault_length(val));

	return 1;
}

static int at803x_cable_test_get_status(struct phy_device *phydev,
					bool *finished)
{
	unsigned long pair_mask;
	int retries = 20;
	int pair, ret;

	if (phydev->phy_id == ATH9331_PHY_ID ||
	    phydev->phy_id == ATH8032_PHY_ID)
		pair_mask = 0x3;
	else
		pair_mask = 0xf;

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

static int at803x_cable_test_start(struct phy_device *phydev)
{
	/* Enable auto-negotiation, but advertise no capabilities, no link
	 * will be established. A restart of the auto-negotiation is not
	 * required, because the cable test will automatically break the link.
	 */
	phy_write(phydev, MII_BMCR, BMCR_ANENABLE);
	phy_write(phydev, MII_ADVERTISE, ADVERTISE_CSMA);
	if (phydev->phy_id != ATH9331_PHY_ID &&
	    phydev->phy_id != ATH8032_PHY_ID)
		phy_write(phydev, MII_CTRL1000, 0);

	/* we do all the (time consuming) work later */
	return 0;
}

static int qca83xx_config_init(struct phy_device *phydev)
{
	u8 switch_revision;

	switch_revision = phydev->dev_flags & QCA8K_DEVFLAGS_REVISION_MASK;

	switch (switch_revision) {
	case 1:
		/* For 100M waveform */
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_0, 0x02ea);
		/* Turn on Gigabit clock */
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_3D, 0x68a0);
		break;

	case 2:
		phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0x0);
		fallthrough;
	case 4:
		phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_AZ_DEBUG, 0x803f);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_3D, 0x6860);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_5, 0x2c46);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_3C, 0x6000);
		break;
	}

	return 0;
}

static struct phy_driver at803x_driver[] = {
{
	/* Qualcomm Atheros AR8035 */
	PHY_ID_MATCH_EXACT(ATH8035_PHY_ID),
	.name			= "Qualcomm Atheros AR8035",
	.flags			= PHY_POLL_CABLE_TEST,
	.probe			= at803x_probe,
	.remove			= at803x_remove,
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
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at803x_cable_test_get_status,
}, {
	/* Qualcomm Atheros AR8030 */
	.phy_id			= ATH8030_PHY_ID,
	.name			= "Qualcomm Atheros AR8030",
	.phy_id_mask		= AT8030_PHY_ID_MASK,
	.probe			= at803x_probe,
	.remove			= at803x_remove,
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
	.probe			= at803x_probe,
	.remove			= at803x_remove,
	.config_init		= at803x_config_init,
	.config_aneg		= at803x_config_aneg,
	.soft_reset		= genphy_soft_reset,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.read_page		= at803x_read_page,
	.write_page		= at803x_write_page,
	.get_features		= at803x_get_features,
	.read_status		= at803x_read_status,
	.config_intr		= &at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.get_tunable		= at803x_get_tunable,
	.set_tunable		= at803x_set_tunable,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at803x_cable_test_get_status,
}, {
	/* Qualcomm Atheros AR8032 */
	PHY_ID_MATCH_EXACT(ATH8032_PHY_ID),
	.name			= "Qualcomm Atheros AR8032",
	.probe			= at803x_probe,
	.remove			= at803x_remove,
	.flags			= PHY_POLL_CABLE_TEST,
	.config_init		= at803x_config_init,
	.link_change_notify	= at803x_link_change_notify,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	/* PHY_BASIC_FEATURES */
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at803x_cable_test_get_status,
}, {
	/* ATHEROS AR9331 */
	PHY_ID_MATCH_EXACT(ATH9331_PHY_ID),
	.name			= "Qualcomm Atheros AR9331 built-in PHY",
	.suspend		= at803x_suspend,
	.resume			= at803x_resume,
	.flags			= PHY_POLL_CABLE_TEST,
	/* PHY_BASIC_FEATURES */
	.config_intr		= &at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.cable_test_start	= at803x_cable_test_start,
	.cable_test_get_status	= at803x_cable_test_get_status,
	.read_status		= at803x_read_status,
	.soft_reset		= genphy_soft_reset,
	.config_aneg		= at803x_config_aneg,
}, {
	/* QCA8337 */
	.phy_id			= QCA8337_PHY_ID,
	.phy_id_mask		= QCA8K_PHY_ID_MASK,
	.name			= "Qualcomm Atheros 8337 internal PHY",
	/* PHY_GBIT_FEATURES */
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
}, {
	/* QCA8327-A from switch QCA8327-AL1A */
	.phy_id			= QCA8327_A_PHY_ID,
	.phy_id_mask		= QCA8K_PHY_ID_MASK,
	.name			= "Qualcomm Atheros 8327-A internal PHY",
	/* PHY_GBIT_FEATURES */
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
}, {
	/* QCA8327-B from switch QCA8327-BL1A */
	.phy_id			= QCA8327_B_PHY_ID,
	.phy_id_mask		= QCA8K_PHY_ID_MASK,
	.name			= "Qualcomm Atheros 8327-B internal PHY",
	/* PHY_GBIT_FEATURES */
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
}, };

module_phy_driver(at803x_driver);

static struct mdio_device_id __maybe_unused atheros_tbl[] = {
	{ ATH8030_PHY_ID, AT8030_PHY_ID_MASK },
	{ PHY_ID_MATCH_EXACT(ATH8031_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH8032_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH8035_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(ATH9331_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8337_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8327_A_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8327_B_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, atheros_tbl);
