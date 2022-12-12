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
#include <linux/phylink.h>
#include <linux/sfp.h>
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
#define AT803X_SS_SPEED_MASK			GENMASK(15, 14)
#define AT803X_SS_SPEED_1000			2
#define AT803X_SS_SPEED_100			1
#define AT803X_SS_SPEED_10			0
#define AT803X_SS_DUPLEX			BIT(13)
#define AT803X_SS_SPEED_DUPLEX_RESOLVED		BIT(11)
#define AT803X_SS_MDIX				BIT(6)

#define QCA808X_SS_SPEED_MASK			GENMASK(9, 7)
#define QCA808X_SS_SPEED_2500			4

#define AT803X_INTR_ENABLE			0x12
#define AT803X_INTR_ENABLE_AUTONEG_ERR		BIT(15)
#define AT803X_INTR_ENABLE_SPEED_CHANGED	BIT(14)
#define AT803X_INTR_ENABLE_DUPLEX_CHANGED	BIT(13)
#define AT803X_INTR_ENABLE_PAGE_RECEIVED	BIT(12)
#define AT803X_INTR_ENABLE_LINK_FAIL		BIT(11)
#define AT803X_INTR_ENABLE_LINK_SUCCESS		BIT(10)
#define AT803X_INTR_ENABLE_LINK_FAIL_BX		BIT(8)
#define AT803X_INTR_ENABLE_LINK_SUCCESS_BX	BIT(7)
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

#define AT803X_PHY_MMD3_WOL_CTRL		0x8012
#define AT803X_WOL_EN				BIT(5)
#define AT803X_LOC_MAC_ADDR_0_15_OFFSET		0x804C
#define AT803X_LOC_MAC_ADDR_16_31_OFFSET	0x804B
#define AT803X_LOC_MAC_ADDR_32_47_OFFSET	0x804A
#define AT803X_REG_CHIP_CONFIG			0x1f
#define AT803X_BT_BX_REG_SEL			0x8000

#define AT803X_DEBUG_ADDR			0x1D
#define AT803X_DEBUG_DATA			0x1E

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

#define AT803X_DEBUG_ANALOG_TEST_CTRL		0x00
#define QCA8327_DEBUG_MANU_CTRL_EN		BIT(2)
#define QCA8337_DEBUG_MANU_CTRL_EN		GENMASK(3, 2)
#define AT803X_DEBUG_RX_CLK_DLY_EN		BIT(15)

#define AT803X_DEBUG_SYSTEM_CTRL_MODE		0x05
#define AT803X_DEBUG_TX_CLK_DLY_EN		BIT(8)

#define AT803X_DEBUG_REG_HIB_CTRL		0x0b
#define   AT803X_DEBUG_HIB_CTRL_SEL_RST_80U	BIT(10)
#define   AT803X_DEBUG_HIB_CTRL_EN_ANY_CHANGE	BIT(13)
#define   AT803X_DEBUG_HIB_CTRL_PS_HIB_EN	BIT(15)

#define AT803X_DEBUG_REG_3C			0x3C

#define AT803X_DEBUG_REG_GREEN			0x3D
#define   AT803X_DEBUG_GATE_CLK_IN1000		BIT(6)

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

#define QCA8081_PHY_ID				0x004dd101

#define QCA8327_A_PHY_ID			0x004dd033
#define QCA8327_B_PHY_ID			0x004dd034
#define QCA8337_PHY_ID				0x004dd036
#define QCA9561_PHY_ID				0x004dd042
#define QCA8K_PHY_ID_MASK			0xffffffff

#define QCA8K_DEVFLAGS_REVISION_MASK		GENMASK(2, 0)

#define AT803X_PAGE_FIBER			0
#define AT803X_PAGE_COPPER			1

/* don't turn off internal PLL */
#define AT803X_KEEP_PLL_ENABLED			BIT(0)
#define AT803X_DISABLE_SMARTEEE			BIT(1)

/* disable hibernation mode */
#define AT803X_DISABLE_HIBERNATION_MODE		BIT(2)

/* ADC threshold */
#define QCA808X_PHY_DEBUG_ADC_THRESHOLD		0x2c80
#define QCA808X_ADC_THRESHOLD_MASK		GENMASK(7, 0)
#define QCA808X_ADC_THRESHOLD_80MV		0
#define QCA808X_ADC_THRESHOLD_100MV		0xf0
#define QCA808X_ADC_THRESHOLD_200MV		0x0f
#define QCA808X_ADC_THRESHOLD_300MV		0xff

/* CLD control */
#define QCA808X_PHY_MMD3_ADDR_CLD_CTRL7		0x8007
#define QCA808X_8023AZ_AFE_CTRL_MASK		GENMASK(8, 4)
#define QCA808X_8023AZ_AFE_EN			0x90

/* AZ control */
#define QCA808X_PHY_MMD3_AZ_TRAINING_CTRL	0x8008
#define QCA808X_MMD3_AZ_TRAINING_VAL		0x1c32

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_20DB	0x8014
#define QCA808X_MSE_THRESHOLD_20DB_VALUE	0x529

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_17DB	0x800E
#define QCA808X_MSE_THRESHOLD_17DB_VALUE	0x341

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_27DB	0x801E
#define QCA808X_MSE_THRESHOLD_27DB_VALUE	0x419

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_28DB	0x8020
#define QCA808X_MSE_THRESHOLD_28DB_VALUE	0x341

#define QCA808X_PHY_MMD7_TOP_OPTION1		0x901c
#define QCA808X_TOP_OPTION1_DATA		0x0

#define QCA808X_PHY_MMD3_DEBUG_1		0xa100
#define QCA808X_MMD3_DEBUG_1_VALUE		0x9203
#define QCA808X_PHY_MMD3_DEBUG_2		0xa101
#define QCA808X_MMD3_DEBUG_2_VALUE		0x48ad
#define QCA808X_PHY_MMD3_DEBUG_3		0xa103
#define QCA808X_MMD3_DEBUG_3_VALUE		0x1698
#define QCA808X_PHY_MMD3_DEBUG_4		0xa105
#define QCA808X_MMD3_DEBUG_4_VALUE		0x8001
#define QCA808X_PHY_MMD3_DEBUG_5		0xa106
#define QCA808X_MMD3_DEBUG_5_VALUE		0x1111
#define QCA808X_PHY_MMD3_DEBUG_6		0xa011
#define QCA808X_MMD3_DEBUG_6_VALUE		0x5f85

/* master/slave seed config */
#define QCA808X_PHY_DEBUG_LOCAL_SEED		9
#define QCA808X_MASTER_SLAVE_SEED_ENABLE	BIT(1)
#define QCA808X_MASTER_SLAVE_SEED_CFG		GENMASK(12, 2)
#define QCA808X_MASTER_SLAVE_SEED_RANGE		0x32

/* Hibernation yields lower power consumpiton in contrast with normal operation mode.
 * when the copper cable is unplugged, the PHY enters into hibernation mode in about 10s.
 */
#define QCA808X_DBG_AN_TEST			0xb
#define QCA808X_HIBERNATION_EN			BIT(15)

#define QCA808X_CDT_ENABLE_TEST			BIT(15)
#define QCA808X_CDT_INTER_CHECK_DIS		BIT(13)
#define QCA808X_CDT_LENGTH_UNIT			BIT(10)

#define QCA808X_MMD3_CDT_STATUS			0x8064
#define QCA808X_MMD3_CDT_DIAG_PAIR_A		0x8065
#define QCA808X_MMD3_CDT_DIAG_PAIR_B		0x8066
#define QCA808X_MMD3_CDT_DIAG_PAIR_C		0x8067
#define QCA808X_MMD3_CDT_DIAG_PAIR_D		0x8068
#define QCA808X_CDT_DIAG_LENGTH			GENMASK(7, 0)

#define QCA808X_CDT_CODE_PAIR_A			GENMASK(15, 12)
#define QCA808X_CDT_CODE_PAIR_B			GENMASK(11, 8)
#define QCA808X_CDT_CODE_PAIR_C			GENMASK(7, 4)
#define QCA808X_CDT_CODE_PAIR_D			GENMASK(3, 0)
#define QCA808X_CDT_STATUS_STAT_FAIL		0
#define QCA808X_CDT_STATUS_STAT_NORMAL		1
#define QCA808X_CDT_STATUS_STAT_OPEN		2
#define QCA808X_CDT_STATUS_STAT_SHORT		3

MODULE_DESCRIPTION("Qualcomm Atheros AR803x and QCA808X PHY driver");
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
	bool is_fiber;
	bool is_1000basex;
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

static int at803x_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int ret, irq_enabled;

	if (wol->wolopts & WAKE_MAGIC) {
		struct net_device *ndev = phydev->attached_dev;
		const u8 *mac;
		unsigned int i;
		static const unsigned int offsets[] = {
			AT803X_LOC_MAC_ADDR_32_47_OFFSET,
			AT803X_LOC_MAC_ADDR_16_31_OFFSET,
			AT803X_LOC_MAC_ADDR_0_15_OFFSET,
		};

		if (!ndev)
			return -ENODEV;

		mac = (const u8 *) ndev->dev_addr;

		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		for (i = 0; i < 3; i++)
			phy_write_mmd(phydev, MDIO_MMD_PCS, offsets[i],
				      mac[(i * 2) + 1] | (mac[(i * 2)] << 8));

		/* Enable WOL function */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, AT803X_PHY_MMD3_WOL_CTRL,
				0, AT803X_WOL_EN);
		if (ret)
			return ret;
		/* Enable WOL interrupt */
		ret = phy_modify(phydev, AT803X_INTR_ENABLE, 0, AT803X_INTR_ENABLE_WOL);
		if (ret)
			return ret;
	} else {
		/* Disable WoL function */
		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, AT803X_PHY_MMD3_WOL_CTRL,
				AT803X_WOL_EN, 0);
		if (ret)
			return ret;
		/* Disable WOL interrupt */
		ret = phy_modify(phydev, AT803X_INTR_ENABLE, AT803X_INTR_ENABLE_WOL, 0);
		if (ret)
			return ret;
	}

	/* Clear WOL status */
	ret = phy_read(phydev, AT803X_INTR_STATUS);
	if (ret < 0)
		return ret;

	/* Check if there are other interrupts except for WOL triggered when PHY is
	 * in interrupt mode, only the interrupts enabled by AT803X_INTR_ENABLE can
	 * be passed up to the interrupt PIN.
	 */
	irq_enabled = phy_read(phydev, AT803X_INTR_ENABLE);
	if (irq_enabled < 0)
		return irq_enabled;

	irq_enabled &= ~AT803X_INTR_ENABLE_WOL;
	if (ret & irq_enabled && !phy_polling_mode(phydev))
		phy_trigger_machine(phydev);

	return 0;
}

static void at803x_get_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	int value;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	value = phy_read_mmd(phydev, MDIO_MMD_PCS, AT803X_PHY_MMD3_WOL_CTRL);
	if (value < 0)
		return;

	if (value & AT803X_WOL_EN)
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

static int at803x_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
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

static const struct sfp_upstream_ops at803x_sfp_ops = {
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
	.module_insert = at803x_sfp_insert,
};

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

		/* Only AR8031/8033 support 1000Base-X for SFP modules */
		ret = phy_sfp_probe(phydev, &at803x_sfp_ops);
		if (ret < 0)
			return ret;
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

	if (phydev->drv->phy_id == ATH8031_PHY_ID) {
		int ccr = phy_read(phydev, AT803X_REG_CHIP_CONFIG);
		int mode_cfg;
		struct ethtool_wolinfo wol = {
			.wolopts = 0,
		};

		if (ccr < 0)
			goto err;
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

		/* Disable WOL by default */
		ret = at803x_set_wol(phydev, &wol);
		if (ret < 0) {
			phydev_err(phydev, "failed to disable WOL on probe: %d\n", ret);
			goto err;
		}
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
	struct at803x_priv *priv = phydev->priv;
	int err;

	err = genphy_read_abilities(phydev);
	if (err)
		return err;

	if (phydev->drv->phy_id == QCA8081_PHY_ID) {
		err = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_NG_EXTABLE);
		if (err < 0)
			return err;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->supported,
				err & MDIO_PMA_NG_EXTABLE_2_5GBT);
	}

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
	struct at803x_priv *priv = phydev->priv;
	int ret;

	if (phydev->drv->phy_id == ATH8031_PHY_ID) {
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
	}

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

static int at803x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, AT803X_INTR_STATUS);

	return (err < 0) ? err : 0;
}

static int at803x_config_intr(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
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
		if (priv->is_fiber) {
			value |= AT803X_INTR_ENABLE_LINK_FAIL_BX;
			value |= AT803X_INTR_ENABLE_LINK_SUCCESS_BX;
		}

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

static int at803x_read_specific_status(struct phy_device *phydev)
{
	int ss;

	/* Read the AT8035 PHY-Specific Status register, which indicates the
	 * speed and duplex that the PHY is actually using, irrespective of
	 * whether we are in autoneg mode or not.
	 */
	ss = phy_read(phydev, AT803X_SPECIFIC_STATUS);
	if (ss < 0)
		return ss;

	if (ss & AT803X_SS_SPEED_DUPLEX_RESOLVED) {
		int sfc, speed;

		sfc = phy_read(phydev, AT803X_SPECIFIC_FUNCTION_CONTROL);
		if (sfc < 0)
			return sfc;

		/* qca8081 takes the different bits for speed value from at803x */
		if (phydev->drv->phy_id == QCA8081_PHY_ID)
			speed = FIELD_GET(QCA808X_SS_SPEED_MASK, ss);
		else
			speed = FIELD_GET(AT803X_SS_SPEED_MASK, ss);

		switch (speed) {
		case AT803X_SS_SPEED_10:
			phydev->speed = SPEED_10;
			break;
		case AT803X_SS_SPEED_100:
			phydev->speed = SPEED_100;
			break;
		case AT803X_SS_SPEED_1000:
			phydev->speed = SPEED_1000;
			break;
		case QCA808X_SS_SPEED_2500:
			phydev->speed = SPEED_2500;
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

	return 0;
}

static int at803x_read_status(struct phy_device *phydev)
{
	struct at803x_priv *priv = phydev->priv;
	int err, old_link = phydev->link;

	if (priv->is_1000basex)
		return genphy_c37_read_status(phydev);

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

	err = at803x_read_specific_status(phydev);
	if (err < 0)
		return err;

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
	struct at803x_priv *priv = phydev->priv;
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

	if (priv->is_1000basex)
		return genphy_c37_config_aneg(phydev);

	/* Do not restart auto-negotiation by setting ret to 0 defautly,
	 * when calling __genphy_config_aneg later.
	 */
	ret = 0;

	if (phydev->drv->phy_id == QCA8081_PHY_ID) {
		int phy_ctrl = 0;

		/* The reg MII_BMCR also needs to be configured for force mode, the
		 * genphy_config_aneg is also needed.
		 */
		if (phydev->autoneg == AUTONEG_DISABLE)
			genphy_c45_pma_setup_forced(phydev);

		if (linkmode_test_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->advertising))
			phy_ctrl = MDIO_AN_10GBT_CTRL_ADV2_5G;

		ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				MDIO_AN_10GBT_CTRL_ADV2_5G, phy_ctrl);
		if (ret < 0)
			return ret;
	}

	return __genphy_config_aneg(phydev, ret);
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

	/* qca8081 takes the different bit 15 to enable CDT test */
	if (phydev->drv->phy_id == QCA8081_PHY_ID)
		cdt = QCA808X_CDT_ENABLE_TEST |
			QCA808X_CDT_LENGTH_UNIT |
			QCA808X_CDT_INTER_CHECK_DIS;
	else
		cdt = FIELD_PREP(AT803X_CDT_MDI_PAIR_MASK, pair) |
			AT803X_CDT_ENABLE_TEST;

	return phy_write(phydev, AT803X_CDT, cdt);
}

static int at803x_cdt_wait_for_completion(struct phy_device *phydev)
{
	int val, ret;
	u16 cdt_en;

	if (phydev->drv->phy_id == QCA8081_PHY_ID)
		cdt_en = QCA808X_CDT_ENABLE_TEST;
	else
		cdt_en = AT803X_CDT_ENABLE_TEST;

	/* One test run takes about 25ms */
	ret = phy_read_poll_timeout(phydev, AT803X_CDT, val,
				    !(val & cdt_en),
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
	    phydev->phy_id == ATH8032_PHY_ID ||
	    phydev->phy_id == QCA9561_PHY_ID)
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
	    phydev->phy_id != ATH8032_PHY_ID &&
	    phydev->phy_id != QCA9561_PHY_ID)
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
		at803x_debug_reg_write(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL, 0x02ea);
		/* Turn on Gigabit clock */
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_GREEN, 0x68a0);
		break;

	case 2:
		phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0x0);
		fallthrough;
	case 4:
		phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_AZ_DEBUG, 0x803f);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_GREEN, 0x6860);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_SYSTEM_CTRL_MODE, 0x2c46);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_3C, 0x6000);
		break;
	}

	/* QCA8327 require DAC amplitude adjustment for 100m set to +6%.
	 * Disable on init and enable only with 100m speed following
	 * qca original source code.
	 */
	if (phydev->drv->phy_id == QCA8327_A_PHY_ID ||
	    phydev->drv->phy_id == QCA8327_B_PHY_ID)
		at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
				      QCA8327_DEBUG_MANU_CTRL_EN, 0);

	/* Following original QCA sourcecode set port to prefer master */
	phy_set_bits(phydev, MII_CTRL1000, CTL1000_PREFER_MASTER);

	return 0;
}

static void qca83xx_link_change_notify(struct phy_device *phydev)
{
	/* QCA8337 doesn't require DAC Amplitude adjustement */
	if (phydev->drv->phy_id == QCA8337_PHY_ID)
		return;

	/* Set DAC Amplitude adjustment to +6% for 100m on link running */
	if (phydev->state == PHY_RUNNING) {
		if (phydev->speed == SPEED_100)
			at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
					      QCA8327_DEBUG_MANU_CTRL_EN,
					      QCA8327_DEBUG_MANU_CTRL_EN);
	} else {
		/* Reset DAC Amplitude adjustment */
		at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
				      QCA8327_DEBUG_MANU_CTRL_EN, 0);
	}
}

static int qca83xx_resume(struct phy_device *phydev)
{
	int ret, val;

	/* Skip reset if not suspended */
	if (!phydev->suspended)
		return 0;

	/* Reinit the port, reset values set by suspend */
	qca83xx_config_init(phydev);

	/* Reset the port on port resume */
	phy_set_bits(phydev, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);

	/* On resume from suspend the switch execute a reset and
	 * restart auto-negotiation. Wait for reset to complete.
	 */
	ret = phy_read_poll_timeout(phydev, MII_BMCR, val, !(val & BMCR_RESET),
				    50000, 600000, true);
	if (ret)
		return ret;

	msleep(1);

	return 0;
}

static int qca83xx_suspend(struct phy_device *phydev)
{
	u16 mask = 0;

	/* Only QCA8337 support actual suspend.
	 * QCA8327 cause port unreliability when phy suspend
	 * is set.
	 */
	if (phydev->drv->phy_id == QCA8337_PHY_ID) {
		genphy_suspend(phydev);
	} else {
		mask |= ~(BMCR_SPEED1000 | BMCR_FULLDPLX);
		phy_modify(phydev, MII_BMCR, mask, 0);
	}

	at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_GREEN,
			      AT803X_DEBUG_GATE_CLK_IN1000, 0);

	at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_HIB_CTRL,
			      AT803X_DEBUG_HIB_CTRL_EN_ANY_CHANGE |
			      AT803X_DEBUG_HIB_CTRL_SEL_RST_80U, 0);

	return 0;
}

static int qca808x_phy_fast_retrain_config(struct phy_device *phydev)
{
	int ret;

	/* Enable fast retrain */
	ret = genphy_c45_fast_retrain(phydev, true);
	if (ret)
		return ret;

	phy_write_mmd(phydev, MDIO_MMD_AN, QCA808X_PHY_MMD7_TOP_OPTION1,
			QCA808X_TOP_OPTION1_DATA);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_20DB,
			QCA808X_MSE_THRESHOLD_20DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_17DB,
			QCA808X_MSE_THRESHOLD_17DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_27DB,
			QCA808X_MSE_THRESHOLD_27DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_28DB,
			QCA808X_MSE_THRESHOLD_28DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_1,
			QCA808X_MMD3_DEBUG_1_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_4,
			QCA808X_MMD3_DEBUG_4_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_5,
			QCA808X_MMD3_DEBUG_5_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_3,
			QCA808X_MMD3_DEBUG_3_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_6,
			QCA808X_MMD3_DEBUG_6_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_2,
			QCA808X_MMD3_DEBUG_2_VALUE);

	return 0;
}

static int qca808x_phy_ms_random_seed_set(struct phy_device *phydev)
{
	u16 seed_value = prandom_u32_max(QCA808X_MASTER_SLAVE_SEED_RANGE);

	return at803x_debug_reg_mask(phydev, QCA808X_PHY_DEBUG_LOCAL_SEED,
			QCA808X_MASTER_SLAVE_SEED_CFG,
			FIELD_PREP(QCA808X_MASTER_SLAVE_SEED_CFG, seed_value));
}

static int qca808x_phy_ms_seed_enable(struct phy_device *phydev, bool enable)
{
	u16 seed_enable = 0;

	if (enable)
		seed_enable = QCA808X_MASTER_SLAVE_SEED_ENABLE;

	return at803x_debug_reg_mask(phydev, QCA808X_PHY_DEBUG_LOCAL_SEED,
			QCA808X_MASTER_SLAVE_SEED_ENABLE, seed_enable);
}

static int qca808x_config_init(struct phy_device *phydev)
{
	int ret;

	/* Active adc&vga on 802.3az for the link 1000M and 100M */
	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_ADDR_CLD_CTRL7,
			QCA808X_8023AZ_AFE_CTRL_MASK, QCA808X_8023AZ_AFE_EN);
	if (ret)
		return ret;

	/* Adjust the threshold on 802.3az for the link 1000M */
	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			QCA808X_PHY_MMD3_AZ_TRAINING_CTRL, QCA808X_MMD3_AZ_TRAINING_VAL);
	if (ret)
		return ret;

	/* Config the fast retrain for the link 2500M */
	ret = qca808x_phy_fast_retrain_config(phydev);
	if (ret)
		return ret;

	/* Configure lower ramdom seed to make phy linked as slave mode */
	ret = qca808x_phy_ms_random_seed_set(phydev);
	if (ret)
		return ret;

	/* Enable seed */
	ret = qca808x_phy_ms_seed_enable(phydev, true);
	if (ret)
		return ret;

	/* Configure adc threshold as 100mv for the link 10M */
	return at803x_debug_reg_mask(phydev, QCA808X_PHY_DEBUG_ADC_THRESHOLD,
			QCA808X_ADC_THRESHOLD_MASK, QCA808X_ADC_THRESHOLD_100MV);
}

static int qca808x_read_status(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (ret < 0)
		return ret;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->lp_advertising,
			ret & MDIO_AN_10GBT_STAT_LP2_5G);

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	ret = at803x_read_specific_status(phydev);
	if (ret < 0)
		return ret;

	if (phydev->link) {
		if (phydev->speed == SPEED_2500)
			phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
		else
			phydev->interface = PHY_INTERFACE_MODE_SGMII;
	} else {
		/* generate seed as a lower random value to make PHY linked as SLAVE easily,
		 * except for master/slave configuration fault detected.
		 * the reason for not putting this code into the function link_change_notify is
		 * the corner case where the link partner is also the qca8081 PHY and the seed
		 * value is configured as the same value, the link can't be up and no link change
		 * occurs.
		 */
		if (phydev->master_slave_state == MASTER_SLAVE_STATE_ERR) {
			qca808x_phy_ms_seed_enable(phydev, false);
		} else {
			qca808x_phy_ms_random_seed_set(phydev);
			qca808x_phy_ms_seed_enable(phydev, true);
		}
	}

	return 0;
}

static int qca808x_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	return qca808x_phy_ms_seed_enable(phydev, true);
}

static bool qca808x_cdt_fault_length_valid(int cdt_code)
{
	switch (cdt_code) {
	case QCA808X_CDT_STATUS_STAT_SHORT:
	case QCA808X_CDT_STATUS_STAT_OPEN:
		return true;
	default:
		return false;
	}
}

static int qca808x_cable_test_result_trans(int cdt_code)
{
	switch (cdt_code) {
	case QCA808X_CDT_STATUS_STAT_NORMAL:
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	case QCA808X_CDT_STATUS_STAT_SHORT:
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;
	case QCA808X_CDT_STATUS_STAT_OPEN:
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
	case QCA808X_CDT_STATUS_STAT_FAIL:
	default:
		return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}
}

static int qca808x_cdt_fault_length(struct phy_device *phydev, int pair)
{
	int val;
	u32 cdt_length_reg = 0;

	switch (pair) {
	case ETHTOOL_A_CABLE_PAIR_A:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_A;
		break;
	case ETHTOOL_A_CABLE_PAIR_B:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_B;
		break;
	case ETHTOOL_A_CABLE_PAIR_C:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_C;
		break;
	case ETHTOOL_A_CABLE_PAIR_D:
		cdt_length_reg = QCA808X_MMD3_CDT_DIAG_PAIR_D;
		break;
	default:
		return -EINVAL;
	}

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, cdt_length_reg);
	if (val < 0)
		return val;

	return (FIELD_GET(QCA808X_CDT_DIAG_LENGTH, val) * 824) / 10;
}

static int qca808x_cable_test_start(struct phy_device *phydev)
{
	int ret;

	/* perform CDT with the following configs:
	 * 1. disable hibernation.
	 * 2. force PHY working in MDI mode.
	 * 3. for PHY working in 1000BaseT.
	 * 4. configure the threshold.
	 */

	ret = at803x_debug_reg_mask(phydev, QCA808X_DBG_AN_TEST, QCA808X_HIBERNATION_EN, 0);
	if (ret < 0)
		return ret;

	ret = at803x_config_mdix(phydev, ETH_TP_MDI);
	if (ret < 0)
		return ret;

	/* Force 1000base-T needs to configure PMA/PMD and MII_BMCR */
	phydev->duplex = DUPLEX_FULL;
	phydev->speed = SPEED_1000;
	ret = genphy_c45_pma_setup_forced(phydev);
	if (ret < 0)
		return ret;

	ret = genphy_setup_forced(phydev);
	if (ret < 0)
		return ret;

	/* configure the thresholds for open, short, pair ok test */
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x8074, 0xc040);
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x8076, 0xc040);
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x8077, 0xa060);
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x8078, 0xc050);
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x807a, 0xc060);
	phy_write_mmd(phydev, MDIO_MMD_PCS, 0x807e, 0xb060);

	return 0;
}

static int qca808x_cable_test_get_status(struct phy_device *phydev, bool *finished)
{
	int ret, val;
	int pair_a, pair_b, pair_c, pair_d;

	*finished = false;

	ret = at803x_cdt_start(phydev, 0);
	if (ret)
		return ret;

	ret = at803x_cdt_wait_for_completion(phydev);
	if (ret)
		return ret;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, QCA808X_MMD3_CDT_STATUS);
	if (val < 0)
		return val;

	pair_a = FIELD_GET(QCA808X_CDT_CODE_PAIR_A, val);
	pair_b = FIELD_GET(QCA808X_CDT_CODE_PAIR_B, val);
	pair_c = FIELD_GET(QCA808X_CDT_CODE_PAIR_C, val);
	pair_d = FIELD_GET(QCA808X_CDT_CODE_PAIR_D, val);

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
				qca808x_cable_test_result_trans(pair_a));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_B,
				qca808x_cable_test_result_trans(pair_b));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_C,
				qca808x_cable_test_result_trans(pair_c));
	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_D,
				qca808x_cable_test_result_trans(pair_d));

	if (qca808x_cdt_fault_length_valid(pair_a))
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_A,
				qca808x_cdt_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_A));
	if (qca808x_cdt_fault_length_valid(pair_b))
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_B,
				qca808x_cdt_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_B));
	if (qca808x_cdt_fault_length_valid(pair_c))
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_C,
				qca808x_cdt_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_C));
	if (qca808x_cdt_fault_length_valid(pair_d))
		ethnl_cable_test_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_D,
				qca808x_cdt_fault_length(phydev, ETHTOOL_A_CABLE_PAIR_D));

	*finished = true;

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
	.probe			= at803x_probe,
	.remove			= at803x_remove,
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
	/* Qualcomm Atheros QCA9561 */
	PHY_ID_MATCH_EXACT(QCA9561_PHY_ID),
	.name			= "Qualcomm Atheros QCA9561 built-in PHY",
	.probe			= at803x_probe,
	.remove			= at803x_remove,
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
	.link_change_notify	= qca83xx_link_change_notify,
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= qca83xx_suspend,
	.resume			= qca83xx_resume,
}, {
	/* QCA8327-A from switch QCA8327-AL1A */
	.phy_id			= QCA8327_A_PHY_ID,
	.phy_id_mask		= QCA8K_PHY_ID_MASK,
	.name			= "Qualcomm Atheros 8327-A internal PHY",
	/* PHY_GBIT_FEATURES */
	.link_change_notify	= qca83xx_link_change_notify,
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= qca83xx_suspend,
	.resume			= qca83xx_resume,
}, {
	/* QCA8327-B from switch QCA8327-BL1A */
	.phy_id			= QCA8327_B_PHY_ID,
	.phy_id_mask		= QCA8K_PHY_ID_MASK,
	.name			= "Qualcomm Atheros 8327-B internal PHY",
	/* PHY_GBIT_FEATURES */
	.link_change_notify	= qca83xx_link_change_notify,
	.probe			= at803x_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= at803x_get_sset_count,
	.get_strings		= at803x_get_strings,
	.get_stats		= at803x_get_stats,
	.suspend		= qca83xx_suspend,
	.resume			= qca83xx_resume,
}, {
	/* Qualcomm QCA8081 */
	PHY_ID_MATCH_EXACT(QCA8081_PHY_ID),
	.name			= "Qualcomm QCA8081",
	.flags			= PHY_POLL_CABLE_TEST,
	.probe			= at803x_probe,
	.remove			= at803x_remove,
	.config_intr		= at803x_config_intr,
	.handle_interrupt	= at803x_handle_interrupt,
	.get_tunable		= at803x_get_tunable,
	.set_tunable		= at803x_set_tunable,
	.set_wol		= at803x_set_wol,
	.get_wol		= at803x_get_wol,
	.get_features		= at803x_get_features,
	.config_aneg		= at803x_config_aneg,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.read_status		= qca808x_read_status,
	.config_init		= qca808x_config_init,
	.soft_reset		= qca808x_soft_reset,
	.cable_test_start	= qca808x_cable_test_start,
	.cable_test_get_status	= qca808x_cable_test_get_status,
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
	{ PHY_ID_MATCH_EXACT(QCA9561_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8081_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, atheros_tbl);
