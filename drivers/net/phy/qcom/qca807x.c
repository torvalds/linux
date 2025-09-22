// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023 Sartura Ltd.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 *         Christian Marangi <ansuelsmth@gmail.com>
 *
 * Qualcomm QCA8072 and QCA8075 PHY driver
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/bitfield.h>
#include <linux/gpio/driver.h>
#include <linux/sfp.h>

#include "../phylib.h"
#include "qcom.h"

#define QCA807X_CHIP_CONFIGURATION				0x1f
#define QCA807X_BT_BX_REG_SEL					BIT(15)
#define QCA807X_BT_BX_REG_SEL_FIBER				0
#define QCA807X_BT_BX_REG_SEL_COPPER				1
#define QCA807X_CHIP_CONFIGURATION_MODE_CFG_MASK		GENMASK(3, 0)
#define QCA807X_CHIP_CONFIGURATION_MODE_QSGMII_SGMII		4
#define QCA807X_CHIP_CONFIGURATION_MODE_PSGMII_FIBER		3
#define QCA807X_CHIP_CONFIGURATION_MODE_PSGMII_ALL_COPPER	0

#define QCA807X_MEDIA_SELECT_STATUS				0x1a
#define QCA807X_MEDIA_DETECTED_COPPER				BIT(5)
#define QCA807X_MEDIA_DETECTED_1000_BASE_X			BIT(4)
#define QCA807X_MEDIA_DETECTED_100_BASE_FX			BIT(3)

#define QCA807X_MMD7_FIBER_MODE_AUTO_DETECTION			0x807e
#define QCA807X_MMD7_FIBER_MODE_AUTO_DETECTION_EN		BIT(0)

#define QCA807X_MMD7_1000BASE_T_POWER_SAVE_PER_CABLE_LENGTH	0x801a
#define QCA807X_CONTROL_DAC_MASK				GENMASK(2, 0)
/* List of tweaks enabled by this bit:
 * - With both FULL amplitude and FULL bias current: bias current
 *   is set to half.
 * - With only DSP amplitude: bias current is set to half and
 *   is set to 1/4 with cable < 10m.
 * - With DSP bias current (included both DSP amplitude and
 *   DSP bias current): bias current is half the detected current
 *   with cable < 10m.
 */
#define QCA807X_CONTROL_DAC_BIAS_CURRENT_TWEAK			BIT(2)
#define QCA807X_CONTROL_DAC_DSP_BIAS_CURRENT			BIT(1)
#define QCA807X_CONTROL_DAC_DSP_AMPLITUDE			BIT(0)

#define QCA807X_MMD7_LED_100N_1				0x8074
#define QCA807X_MMD7_LED_100N_2				0x8075
#define QCA807X_MMD7_LED_1000N_1			0x8076
#define QCA807X_MMD7_LED_1000N_2			0x8077

#define QCA807X_MMD7_LED_CTRL(x)			(0x8074 + ((x) * 2))
#define QCA807X_MMD7_LED_FORCE_CTRL(x)			(0x8075 + ((x) * 2))

/* LED hw control pattern for fiber port */
#define QCA807X_LED_FIBER_PATTERN_MASK			GENMASK(11, 1)
#define QCA807X_LED_FIBER_TXACT_BLK_EN			BIT(10)
#define QCA807X_LED_FIBER_RXACT_BLK_EN			BIT(9)
#define QCA807X_LED_FIBER_FDX_ON_EN			BIT(6)
#define QCA807X_LED_FIBER_HDX_ON_EN			BIT(5)
#define QCA807X_LED_FIBER_1000BX_ON_EN			BIT(2)
#define QCA807X_LED_FIBER_100FX_ON_EN			BIT(1)

/* Some device repurpose the LED as GPIO out */
#define QCA807X_GPIO_FORCE_EN				QCA808X_LED_FORCE_EN
#define QCA807X_GPIO_FORCE_MODE_MASK			QCA808X_LED_FORCE_MODE_MASK

#define QCA807X_FUNCTION_CONTROL			0x10
#define QCA807X_FC_MDI_CROSSOVER_MODE_MASK		GENMASK(6, 5)
#define QCA807X_FC_MDI_CROSSOVER_AUTO			3
#define QCA807X_FC_MDI_CROSSOVER_MANUAL_MDIX		1
#define QCA807X_FC_MDI_CROSSOVER_MANUAL_MDI		0

/* PQSGMII Analog PHY specific */
#define PQSGMII_CTRL_REG				0x0
#define PQSGMII_ANALOG_SW_RESET				BIT(6)
#define PQSGMII_DRIVE_CONTROL_1				0xb
#define PQSGMII_TX_DRIVER_MASK				GENMASK(7, 4)
#define PQSGMII_TX_DRIVER_140MV				0x0
#define PQSGMII_TX_DRIVER_160MV				0x1
#define PQSGMII_TX_DRIVER_180MV				0x2
#define PQSGMII_TX_DRIVER_200MV				0x3
#define PQSGMII_TX_DRIVER_220MV				0x4
#define PQSGMII_TX_DRIVER_240MV				0x5
#define PQSGMII_TX_DRIVER_260MV				0x6
#define PQSGMII_TX_DRIVER_280MV				0x7
#define PQSGMII_TX_DRIVER_300MV				0x8
#define PQSGMII_TX_DRIVER_320MV				0x9
#define PQSGMII_TX_DRIVER_400MV				0xa
#define PQSGMII_TX_DRIVER_500MV				0xb
#define PQSGMII_TX_DRIVER_600MV				0xc
#define PQSGMII_MODE_CTRL				0x6d
#define PQSGMII_MODE_CTRL_AZ_WORKAROUND_MASK		BIT(0)
#define PQSGMII_MMD3_SERDES_CONTROL			0x805a

#define PHY_ID_QCA8072		0x004dd0b2
#define PHY_ID_QCA8075		0x004dd0b1

#define QCA807X_COMBO_ADDR_OFFSET			4
#define QCA807X_PQSGMII_ADDR_OFFSET			5
#define SERDES_RESET_SLEEP				100

enum qca807x_global_phy {
	QCA807X_COMBO_ADDR = 4,
	QCA807X_PQSGMII_ADDR = 5,
};

struct qca807x_shared_priv {
	unsigned int package_mode;
	u32 tx_drive_strength;
};

struct qca807x_gpio_priv {
	struct phy_device *phy;
};

struct qca807x_priv {
	bool dac_full_amplitude;
	bool dac_full_bias_current;
	bool dac_disable_bias_current_tweak;
	struct qcom_phy_hw_stats hw_stats;
};

static int qca807x_cable_test_start(struct phy_device *phydev)
{
	/* we do all the (time consuming) work later */
	return 0;
}

static int qca807x_led_parse_netdev(struct phy_device *phydev, unsigned long rules,
				    u16 *offload_trigger)
{
	/* Parsing specific to netdev trigger */
	switch (phydev->port) {
	case PORT_TP:
		if (test_bit(TRIGGER_NETDEV_TX, &rules))
			*offload_trigger |= QCA808X_LED_TX_BLINK;
		if (test_bit(TRIGGER_NETDEV_RX, &rules))
			*offload_trigger |= QCA808X_LED_RX_BLINK;
		if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
			*offload_trigger |= QCA808X_LED_SPEED10_ON;
		if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
			*offload_trigger |= QCA808X_LED_SPEED100_ON;
		if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
			*offload_trigger |= QCA808X_LED_SPEED1000_ON;
		if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &rules))
			*offload_trigger |= QCA808X_LED_HALF_DUPLEX_ON;
		if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &rules))
			*offload_trigger |= QCA808X_LED_FULL_DUPLEX_ON;
		break;
	case PORT_FIBRE:
		if (test_bit(TRIGGER_NETDEV_TX, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_TXACT_BLK_EN;
		if (test_bit(TRIGGER_NETDEV_RX, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_RXACT_BLK_EN;
		if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_100FX_ON_EN;
		if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_1000BX_ON_EN;
		if (test_bit(TRIGGER_NETDEV_HALF_DUPLEX, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_HDX_ON_EN;
		if (test_bit(TRIGGER_NETDEV_FULL_DUPLEX, &rules))
			*offload_trigger |= QCA807X_LED_FIBER_FDX_ON_EN;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (rules && !*offload_trigger)
		return -EOPNOTSUPP;

	return 0;
}

static int qca807x_led_hw_control_enable(struct phy_device *phydev, u8 index)
{
	u16 reg;

	if (index > 1)
		return -EINVAL;

	reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
	return qca808x_led_reg_hw_control_enable(phydev, reg);
}

static int qca807x_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	u16 offload_trigger = 0;

	if (index > 1)
		return -EINVAL;

	return qca807x_led_parse_netdev(phydev, rules, &offload_trigger);
}

static int qca807x_led_hw_control_set(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	u16 reg, mask, offload_trigger = 0;
	int ret;

	if (index > 1)
		return -EINVAL;

	ret = qca807x_led_parse_netdev(phydev, rules, &offload_trigger);
	if (ret)
		return ret;

	ret = qca807x_led_hw_control_enable(phydev, index);
	if (ret)
		return ret;

	switch (phydev->port) {
	case PORT_TP:
		reg = QCA807X_MMD7_LED_CTRL(index);
		mask = QCA808X_LED_PATTERN_MASK;
		break;
	case PORT_FIBRE:
		/* HW control pattern bits are in LED FORCE reg */
		reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
		mask = QCA807X_LED_FIBER_PATTERN_MASK;
		break;
	default:
		return -EINVAL;
	}

	return phy_modify_mmd(phydev, MDIO_MMD_AN, reg, mask,
			      offload_trigger);
}

static bool qca807x_led_hw_control_status(struct phy_device *phydev, u8 index)
{
	u16 reg;

	if (index > 1)
		return false;

	reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
	return qca808x_led_reg_hw_control_status(phydev, reg);
}

static int qca807x_led_hw_control_get(struct phy_device *phydev, u8 index,
				      unsigned long *rules)
{
	u16 reg;
	int val;

	if (index > 1)
		return -EINVAL;

	/* Check if we have hw control enabled */
	if (qca807x_led_hw_control_status(phydev, index))
		return -EINVAL;

	/* Parsing specific to netdev trigger */
	switch (phydev->port) {
	case PORT_TP:
		reg = QCA807X_MMD7_LED_CTRL(index);
		val = phy_read_mmd(phydev, MDIO_MMD_AN, reg);
		if (val & QCA808X_LED_TX_BLINK)
			set_bit(TRIGGER_NETDEV_TX, rules);
		if (val & QCA808X_LED_RX_BLINK)
			set_bit(TRIGGER_NETDEV_RX, rules);
		if (val & QCA808X_LED_SPEED10_ON)
			set_bit(TRIGGER_NETDEV_LINK_10, rules);
		if (val & QCA808X_LED_SPEED100_ON)
			set_bit(TRIGGER_NETDEV_LINK_100, rules);
		if (val & QCA808X_LED_SPEED1000_ON)
			set_bit(TRIGGER_NETDEV_LINK_1000, rules);
		if (val & QCA808X_LED_HALF_DUPLEX_ON)
			set_bit(TRIGGER_NETDEV_HALF_DUPLEX, rules);
		if (val & QCA808X_LED_FULL_DUPLEX_ON)
			set_bit(TRIGGER_NETDEV_FULL_DUPLEX, rules);
		break;
	case PORT_FIBRE:
		/* HW control pattern bits are in LED FORCE reg */
		reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
		val = phy_read_mmd(phydev, MDIO_MMD_AN, reg);
		if (val & QCA807X_LED_FIBER_TXACT_BLK_EN)
			set_bit(TRIGGER_NETDEV_TX, rules);
		if (val & QCA807X_LED_FIBER_RXACT_BLK_EN)
			set_bit(TRIGGER_NETDEV_RX, rules);
		if (val & QCA807X_LED_FIBER_100FX_ON_EN)
			set_bit(TRIGGER_NETDEV_LINK_100, rules);
		if (val & QCA807X_LED_FIBER_1000BX_ON_EN)
			set_bit(TRIGGER_NETDEV_LINK_1000, rules);
		if (val & QCA807X_LED_FIBER_HDX_ON_EN)
			set_bit(TRIGGER_NETDEV_HALF_DUPLEX, rules);
		if (val & QCA807X_LED_FIBER_FDX_ON_EN)
			set_bit(TRIGGER_NETDEV_FULL_DUPLEX, rules);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int qca807x_led_hw_control_reset(struct phy_device *phydev, u8 index)
{
	u16 reg, mask;

	if (index > 1)
		return -EINVAL;

	switch (phydev->port) {
	case PORT_TP:
		reg = QCA807X_MMD7_LED_CTRL(index);
		mask = QCA808X_LED_PATTERN_MASK;
		break;
	case PORT_FIBRE:
		/* HW control pattern bits are in LED FORCE reg */
		reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
		mask = QCA807X_LED_FIBER_PATTERN_MASK;
		break;
	default:
		return -EINVAL;
	}

	return phy_clear_bits_mmd(phydev, MDIO_MMD_AN, reg, mask);
}

static int qca807x_led_brightness_set(struct phy_device *phydev,
				      u8 index, enum led_brightness value)
{
	u16 reg;
	int ret;

	if (index > 1)
		return -EINVAL;

	/* If we are setting off the LED reset any hw control rule */
	if (!value) {
		ret = qca807x_led_hw_control_reset(phydev, index);
		if (ret)
			return ret;
	}

	reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
	return qca808x_led_reg_brightness_set(phydev, reg, value);
}

static int qca807x_led_blink_set(struct phy_device *phydev, u8 index,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
	u16 reg;

	if (index > 1)
		return -EINVAL;

	reg = QCA807X_MMD7_LED_FORCE_CTRL(index);
	return qca808x_led_reg_blink_set(phydev, reg, delay_on, delay_off);
}

#ifdef CONFIG_GPIOLIB
static int qca807x_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int qca807x_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct qca807x_gpio_priv *priv = gpiochip_get_data(gc);
	u16 reg;
	int val;

	reg = QCA807X_MMD7_LED_FORCE_CTRL(offset);
	val = phy_read_mmd(priv->phy, MDIO_MMD_AN, reg);

	return FIELD_GET(QCA807X_GPIO_FORCE_MODE_MASK, val);
}

static int qca807x_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct qca807x_gpio_priv *priv = gpiochip_get_data(gc);
	u16 reg;
	int val;

	reg = QCA807X_MMD7_LED_FORCE_CTRL(offset);

	val = phy_read_mmd(priv->phy, MDIO_MMD_AN, reg);
	if (val < 0)
		return val;

	val &= ~QCA807X_GPIO_FORCE_MODE_MASK;
	val |= QCA807X_GPIO_FORCE_EN;
	val |= FIELD_PREP(QCA807X_GPIO_FORCE_MODE_MASK, value);

	return phy_write_mmd(priv->phy, MDIO_MMD_AN, reg, val);
}

static int qca807x_gpio_dir_out(struct gpio_chip *gc, unsigned int offset, int value)
{
	return qca807x_gpio_set(gc, offset, value);
}

static int qca807x_gpio(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct qca807x_gpio_priv *priv;
	struct gpio_chip *gc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phy = phydev;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gc->label = dev_name(dev);
	gc->base = -1;
	gc->ngpio = 2;
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->can_sleep = true;
	gc->get_direction = qca807x_gpio_get_direction;
	gc->direction_output = qca807x_gpio_dir_out;
	gc->get = qca807x_gpio_get;
	gc->set = qca807x_gpio_set;

	return devm_gpiochip_add_data(dev, gc, priv);
}
#endif

static int qca807x_read_fiber_status(struct phy_device *phydev)
{
	bool changed;
	int ss, err;

	err = genphy_c37_read_status(phydev, &changed);
	if (err || !changed)
		return err;

	/* Read the QCA807x PHY-Specific Status register fiber page,
	 * which indicates the speed and duplex that the PHY is actually
	 * using, irrespective of whether we are in autoneg mode or not.
	 */
	ss = phy_read(phydev, AT803X_SPECIFIC_STATUS);
	if (ss < 0)
		return ss;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	if (ss & AT803X_SS_SPEED_DUPLEX_RESOLVED) {
		switch (FIELD_GET(AT803X_SS_SPEED_MASK, ss)) {
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
	}

	return 0;
}

static int qca807x_read_status(struct phy_device *phydev)
{
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, phydev->supported)) {
		switch (phydev->port) {
		case PORT_FIBRE:
			return qca807x_read_fiber_status(phydev);
		case PORT_TP:
			return at803x_read_status(phydev);
		default:
			return -EINVAL;
		}
	}

	return at803x_read_status(phydev);
}

static int qca807x_phy_package_probe_once(struct phy_device *phydev)
{
	struct qca807x_shared_priv *priv = phy_package_get_priv(phydev);
	struct device_node *np = phy_package_get_node(phydev);
	unsigned int tx_drive_strength;
	const char *package_mode_name;

	/* Default to 600mw if not defined */
	if (of_property_read_u32(np, "qcom,tx-drive-strength-milliwatt",
				 &tx_drive_strength))
		tx_drive_strength = 600;

	switch (tx_drive_strength) {
	case 140:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_140MV;
		break;
	case 160:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_160MV;
		break;
	case 180:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_180MV;
		break;
	case 200:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_200MV;
		break;
	case 220:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_220MV;
		break;
	case 240:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_240MV;
		break;
	case 260:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_260MV;
		break;
	case 280:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_280MV;
		break;
	case 300:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_300MV;
		break;
	case 320:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_320MV;
		break;
	case 400:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_400MV;
		break;
	case 500:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_500MV;
		break;
	case 600:
		priv->tx_drive_strength = PQSGMII_TX_DRIVER_600MV;
		break;
	default:
		return -EINVAL;
	}

	priv->package_mode = PHY_INTERFACE_MODE_NA;
	if (!of_property_read_string(np, "qcom,package-mode",
				     &package_mode_name)) {
		if (!strcasecmp(package_mode_name,
				phy_modes(PHY_INTERFACE_MODE_PSGMII)))
			priv->package_mode = PHY_INTERFACE_MODE_PSGMII;
		else if (!strcasecmp(package_mode_name,
				     phy_modes(PHY_INTERFACE_MODE_QSGMII)))
			priv->package_mode = PHY_INTERFACE_MODE_QSGMII;
		else
			return -EINVAL;
	}

	return 0;
}

static int qca807x_phy_package_config_init_once(struct phy_device *phydev)
{
	struct qca807x_shared_priv *priv = phy_package_get_priv(phydev);
	int val, ret;

	/* Make sure PHY follow PHY package mode if enforced */
	if (priv->package_mode != PHY_INTERFACE_MODE_NA &&
	    phydev->interface != priv->package_mode)
		return -EINVAL;

	phy_lock_mdio_bus(phydev);

	/* Set correct PHY package mode */
	val = __phy_package_read(phydev, QCA807X_COMBO_ADDR,
				 QCA807X_CHIP_CONFIGURATION);
	val &= ~QCA807X_CHIP_CONFIGURATION_MODE_CFG_MASK;
	/* package_mode can be QSGMII or PSGMII and we validate
	 * this in probe_once.
	 * With package_mode to NA, we default to PSGMII.
	 */
	switch (priv->package_mode) {
	case PHY_INTERFACE_MODE_QSGMII:
		val |= QCA807X_CHIP_CONFIGURATION_MODE_QSGMII_SGMII;
		break;
	case PHY_INTERFACE_MODE_PSGMII:
	default:
		val |= QCA807X_CHIP_CONFIGURATION_MODE_PSGMII_ALL_COPPER;
	}
	ret = __phy_package_write(phydev, QCA807X_COMBO_ADDR,
				  QCA807X_CHIP_CONFIGURATION, val);
	if (ret)
		goto exit;

	/* After mode change Serdes reset is required */
	val = __phy_package_read(phydev, QCA807X_PQSGMII_ADDR,
				 PQSGMII_CTRL_REG);
	val &= ~PQSGMII_ANALOG_SW_RESET;
	ret = __phy_package_write(phydev, QCA807X_PQSGMII_ADDR,
				  PQSGMII_CTRL_REG, val);
	if (ret)
		goto exit;

	msleep(SERDES_RESET_SLEEP);

	val = __phy_package_read(phydev, QCA807X_PQSGMII_ADDR,
				 PQSGMII_CTRL_REG);
	val |= PQSGMII_ANALOG_SW_RESET;
	ret = __phy_package_write(phydev, QCA807X_PQSGMII_ADDR,
				  PQSGMII_CTRL_REG, val);
	if (ret)
		goto exit;

	/* Workaround to enable AZ transmitting ability */
	val = __phy_package_read_mmd(phydev, QCA807X_PQSGMII_ADDR,
				     MDIO_MMD_PMAPMD, PQSGMII_MODE_CTRL);
	val &= ~PQSGMII_MODE_CTRL_AZ_WORKAROUND_MASK;
	ret = __phy_package_write_mmd(phydev, QCA807X_PQSGMII_ADDR,
				      MDIO_MMD_PMAPMD, PQSGMII_MODE_CTRL, val);
	if (ret)
		goto exit;

	/* Set PQSGMII TX AMP strength */
	val = __phy_package_read(phydev, QCA807X_PQSGMII_ADDR,
				 PQSGMII_DRIVE_CONTROL_1);
	val &= ~PQSGMII_TX_DRIVER_MASK;
	val |= FIELD_PREP(PQSGMII_TX_DRIVER_MASK, priv->tx_drive_strength);
	ret = __phy_package_write(phydev, QCA807X_PQSGMII_ADDR,
				  PQSGMII_DRIVE_CONTROL_1, val);
	if (ret)
		goto exit;

	/* Prevent PSGMII going into hibernation via PSGMII self test */
	val = __phy_package_read_mmd(phydev, QCA807X_COMBO_ADDR,
				     MDIO_MMD_PCS, PQSGMII_MMD3_SERDES_CONTROL);
	val &= ~BIT(1);
	ret = __phy_package_write_mmd(phydev, QCA807X_COMBO_ADDR,
				      MDIO_MMD_PCS, PQSGMII_MMD3_SERDES_CONTROL, val);

exit:
	phy_unlock_mdio_bus(phydev);

	return ret;
}

static int qca807x_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
{
	struct phy_device *phydev = upstream;
	const struct sfp_module_caps *caps;
	phy_interface_t iface;
	int ret;

	caps = sfp_get_module_caps(phydev->sfp_bus);
	iface = sfp_select_interface(phydev->sfp_bus, caps->link_modes);

	dev_info(&phydev->mdio.dev, "%s SFP module inserted\n", phy_modes(iface));

	switch (iface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_100BASEX:
		/* Set PHY mode to PSGMII combo (1/4 copper + combo ports) mode */
		ret = phy_modify(phydev,
				 QCA807X_CHIP_CONFIGURATION,
				 QCA807X_CHIP_CONFIGURATION_MODE_CFG_MASK,
				 QCA807X_CHIP_CONFIGURATION_MODE_PSGMII_FIBER);
		/* Enable fiber mode autodection (1000Base-X or 100Base-FX) */
		ret = phy_set_bits_mmd(phydev,
				       MDIO_MMD_AN,
				       QCA807X_MMD7_FIBER_MODE_AUTO_DETECTION,
				       QCA807X_MMD7_FIBER_MODE_AUTO_DETECTION_EN);
		/* Select fiber page */
		ret = phy_clear_bits(phydev,
				     QCA807X_CHIP_CONFIGURATION,
				     QCA807X_BT_BX_REG_SEL);

		phydev->port = PORT_FIBRE;
		break;
	default:
		dev_err(&phydev->mdio.dev, "Incompatible SFP module inserted\n");
		return -EINVAL;
	}

	return ret;
}

static void qca807x_sfp_remove(void *upstream)
{
	struct phy_device *phydev = upstream;

	/* Select copper page */
	phy_set_bits(phydev,
		     QCA807X_CHIP_CONFIGURATION,
		     QCA807X_BT_BX_REG_SEL);

	phydev->port = PORT_TP;
}

static const struct sfp_upstream_ops qca807x_sfp_ops = {
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
	.module_insert = qca807x_sfp_insert,
	.module_remove = qca807x_sfp_remove,
	.connect_phy = phy_sfp_connect_phy,
	.disconnect_phy = phy_sfp_disconnect_phy,
};

static int qca807x_probe(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	struct qca807x_shared_priv *shared_priv;
	struct device *dev = &phydev->mdio.dev;
	struct qca807x_priv *priv;
	int ret;

	ret = devm_of_phy_package_join(dev, phydev, sizeof(*shared_priv));
	if (ret)
		return ret;

	if (phy_package_probe_once(phydev)) {
		ret = qca807x_phy_package_probe_once(phydev);
		if (ret)
			return ret;
	}

	shared_priv = phy_package_get_priv(phydev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dac_full_amplitude = of_property_read_bool(node, "qcom,dac-full-amplitude");
	priv->dac_full_bias_current = of_property_read_bool(node, "qcom,dac-full-bias-current");
	priv->dac_disable_bias_current_tweak = of_property_read_bool(node,
								     "qcom,dac-disable-bias-current-tweak");

#if IS_ENABLED(CONFIG_GPIOLIB)
	/* Do not register a GPIO controller unless flagged for it */
	if (of_property_read_bool(node, "gpio-controller")) {
		ret = qca807x_gpio(phydev);
		if (ret)
			return ret;
	}
#endif

	/* Attach SFP bus on combo port*/
	if (phy_read(phydev, QCA807X_CHIP_CONFIGURATION)) {
		ret = phy_sfp_probe(phydev, &qca807x_sfp_ops);
		if (ret)
			return ret;
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, phydev->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, phydev->advertising);
	}

	phydev->priv = priv;

	return 0;
}

static int qca807x_config_init(struct phy_device *phydev)
{
	struct qca807x_priv *priv = phydev->priv;
	u16 control_dac;
	int ret;

	if (phy_package_init_once(phydev)) {
		ret = qca807x_phy_package_config_init_once(phydev);
		if (ret)
			return ret;
	}

	ret = qcom_phy_counter_config(phydev);
	if (ret)
		return ret;

	control_dac = phy_read_mmd(phydev, MDIO_MMD_AN,
				   QCA807X_MMD7_1000BASE_T_POWER_SAVE_PER_CABLE_LENGTH);
	control_dac &= ~QCA807X_CONTROL_DAC_MASK;
	if (!priv->dac_full_amplitude)
		control_dac |= QCA807X_CONTROL_DAC_DSP_AMPLITUDE;
	if (!priv->dac_full_bias_current)
		control_dac |= QCA807X_CONTROL_DAC_DSP_BIAS_CURRENT;
	if (!priv->dac_disable_bias_current_tweak)
		control_dac |= QCA807X_CONTROL_DAC_BIAS_CURRENT_TWEAK;
	return phy_write_mmd(phydev, MDIO_MMD_AN,
			     QCA807X_MMD7_1000BASE_T_POWER_SAVE_PER_CABLE_LENGTH,
			     control_dac);
}

static int qca807x_update_stats(struct phy_device *phydev)
{
	struct qca807x_priv *priv = phydev->priv;

	return qcom_phy_update_stats(phydev, &priv->hw_stats);
}

static void qca807x_get_phy_stats(struct phy_device *phydev,
				  struct ethtool_eth_phy_stats *eth_stats,
				  struct ethtool_phy_stats *stats)
{
	struct qca807x_priv *priv = phydev->priv;

	qcom_phy_get_stats(stats, priv->hw_stats);
}

static struct phy_driver qca807x_drivers[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_QCA8072),
		.name           = "Qualcomm QCA8072",
		.flags		= PHY_POLL_CABLE_TEST,
		/* PHY_GBIT_FEATURES */
		.probe		= qca807x_probe,
		.config_init	= qca807x_config_init,
		.read_status	= qca807x_read_status,
		.config_intr	= at803x_config_intr,
		.handle_interrupt = at803x_handle_interrupt,
		.soft_reset	= genphy_soft_reset,
		.get_tunable	= at803x_get_tunable,
		.set_tunable	= at803x_set_tunable,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
		.cable_test_start	= qca807x_cable_test_start,
		.cable_test_get_status	= qca808x_cable_test_get_status,
		.update_stats		= qca807x_update_stats,
		.get_phy_stats		= qca807x_get_phy_stats,
		.set_wol		= at8031_set_wol,
		.get_wol		= at803x_get_wol,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_QCA8075),
		.name           = "Qualcomm QCA8075",
		.flags		= PHY_POLL_CABLE_TEST,
		/* PHY_GBIT_FEATURES */
		.probe		= qca807x_probe,
		.config_init	= qca807x_config_init,
		.read_status	= qca807x_read_status,
		.config_intr	= at803x_config_intr,
		.handle_interrupt = at803x_handle_interrupt,
		.soft_reset	= genphy_soft_reset,
		.get_tunable	= at803x_get_tunable,
		.set_tunable	= at803x_set_tunable,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
		.cable_test_start	= qca807x_cable_test_start,
		.cable_test_get_status	= qca808x_cable_test_get_status,
		.led_brightness_set = qca807x_led_brightness_set,
		.led_blink_set = qca807x_led_blink_set,
		.led_hw_is_supported = qca807x_led_hw_is_supported,
		.led_hw_control_set = qca807x_led_hw_control_set,
		.led_hw_control_get = qca807x_led_hw_control_get,
		.update_stats		= qca807x_update_stats,
		.get_phy_stats		= qca807x_get_phy_stats,
		.set_wol		= at8031_set_wol,
		.get_wol		= at803x_get_wol,
	},
};
module_phy_driver(qca807x_drivers);

static const struct mdio_device_id __maybe_unused qca807x_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_QCA8072) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_QCA8075) },
	{ }
};

MODULE_AUTHOR("Robert Marko <robert.marko@sartura.hr>");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("Qualcomm QCA807x PHY driver");
MODULE_DEVICE_TABLE(mdio, qca807x_tbl);
MODULE_LICENSE("GPL");
