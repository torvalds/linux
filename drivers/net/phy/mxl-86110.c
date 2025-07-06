// SPDX-License-Identifier: GPL-2.0+
/*
 * PHY driver for Maxlinear MXL86110
 *
 * Copyright 2023 MaxLinear Inc.
 *
 */

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>

/* PHY ID */
#define PHY_ID_MXL86110		0xc1335580

/* required to access extended registers */
#define MXL86110_EXTD_REG_ADDR_OFFSET			0x1E
#define MXL86110_EXTD_REG_ADDR_DATA			0x1F
#define PHY_IRQ_ENABLE_REG				0x12
#define PHY_IRQ_ENABLE_REG_WOL				BIT(6)

/* SyncE Configuration Register - COM_EXT SYNCE_CFG */
#define MXL86110_EXT_SYNCE_CFG_REG			0xA012
#define MXL86110_EXT_SYNCE_CFG_CLK_FRE_SEL		BIT(4)
#define MXL86110_EXT_SYNCE_CFG_EN_SYNC_E_DURING_LNKDN	BIT(5)
#define MXL86110_EXT_SYNCE_CFG_EN_SYNC_E		BIT(6)
#define MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_MASK		GENMASK(3, 1)
#define MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_125M_PLL	0
#define MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_25M		4

/* MAC Address registers */
#define MXL86110_EXT_MAC_ADDR_CFG1			0xA007
#define MXL86110_EXT_MAC_ADDR_CFG2			0xA008
#define MXL86110_EXT_MAC_ADDR_CFG3			0xA009

#define MXL86110_EXT_WOL_CFG_REG			0xA00A
#define MXL86110_WOL_CFG_WOL_MASK			BIT(3)

/* RGMII register */
#define MXL86110_EXT_RGMII_CFG1_REG			0xA003
/* delay can be adjusted in steps of about 150ps */
#define MXL86110_EXT_RGMII_CFG1_RX_NO_DELAY		(0x0 << 10)
/* Closest value to 2000 ps */
#define MXL86110_EXT_RGMII_CFG1_RX_DELAY_1950PS		(0xD << 10)
#define MXL86110_EXT_RGMII_CFG1_RX_DELAY_MASK		GENMASK(13, 10)

#define MXL86110_EXT_RGMII_CFG1_TX_1G_DELAY_1950PS	(0xD << 0)
#define MXL86110_EXT_RGMII_CFG1_TX_1G_DELAY_MASK	GENMASK(3, 0)

#define MXL86110_EXT_RGMII_CFG1_TX_10MB_100MB_DELAY_1950PS	(0xD << 4)
#define MXL86110_EXT_RGMII_CFG1_TX_10MB_100MB_DELAY_MASK	GENMASK(7, 4)

#define MXL86110_EXT_RGMII_CFG1_FULL_MASK \
			((MXL86110_EXT_RGMII_CFG1_RX_DELAY_MASK) | \
			(MXL86110_EXT_RGMII_CFG1_TX_1G_DELAY_MASK) | \
			(MXL86110_EXT_RGMII_CFG1_TX_10MB_100MB_DELAY_MASK))

/* EXT Sleep Control register */
#define MXL86110_UTP_EXT_SLEEP_CTRL_REG			0x27
#define MXL86110_UTP_EXT_SLEEP_CTRL_EN_SLEEP_SW_OFF	0
#define MXL86110_UTP_EXT_SLEEP_CTRL_EN_SLEEP_SW_MASK	BIT(15)

/* RGMII In-Band Status and MDIO Configuration Register */
#define MXL86110_EXT_RGMII_MDIO_CFG			0xA005
#define MXL86110_RGMII_MDIO_CFG_EPA0_MASK		GENMASK(6, 6)
#define MXL86110_EXT_RGMII_MDIO_CFG_EBA_MASK		GENMASK(5, 5)
#define MXL86110_EXT_RGMII_MDIO_CFG_BA_MASK		GENMASK(4, 0)

#define MXL86110_MAX_LEDS	3
/* LED registers and defines */
#define MXL86110_LED0_CFG_REG 0xA00C
#define MXL86110_LED1_CFG_REG 0xA00D
#define MXL86110_LED2_CFG_REG 0xA00E

#define MXL86110_LEDX_CFG_BLINK				BIT(13)
#define MXL86110_LEDX_CFG_LINK_UP_FULL_DUPLEX_ON	BIT(12)
#define MXL86110_LEDX_CFG_LINK_UP_HALF_DUPLEX_ON	BIT(11)
#define MXL86110_LEDX_CFG_LINK_UP_TX_ACT_ON		BIT(10)
#define MXL86110_LEDX_CFG_LINK_UP_RX_ACT_ON		BIT(9)
#define MXL86110_LEDX_CFG_LINK_UP_TX_ON			BIT(8)
#define MXL86110_LEDX_CFG_LINK_UP_RX_ON			BIT(7)
#define MXL86110_LEDX_CFG_LINK_UP_1GB_ON		BIT(6)
#define MXL86110_LEDX_CFG_LINK_UP_100MB_ON		BIT(5)
#define MXL86110_LEDX_CFG_LINK_UP_10MB_ON		BIT(4)
#define MXL86110_LEDX_CFG_LINK_UP_COLLISION		BIT(3)
#define MXL86110_LEDX_CFG_LINK_UP_1GB_BLINK		BIT(2)
#define MXL86110_LEDX_CFG_LINK_UP_100MB_BLINK		BIT(1)
#define MXL86110_LEDX_CFG_LINK_UP_10MB_BLINK		BIT(0)

#define MXL86110_LED_BLINK_CFG_REG			0xA00F
#define MXL86110_LED_BLINK_CFG_FREQ_MODE1_2HZ		0
#define MXL86110_LED_BLINK_CFG_FREQ_MODE1_4HZ		BIT(0)
#define MXL86110_LED_BLINK_CFG_FREQ_MODE1_8HZ		BIT(1)
#define MXL86110_LED_BLINK_CFG_FREQ_MODE1_16HZ		(BIT(1) | BIT(0))
#define MXL86110_LED_BLINK_CFG_FREQ_MODE2_2HZ		0
#define MXL86110_LED_BLINK_CFG_FREQ_MODE2_4HZ		BIT(2)
#define MXL86110_LED_BLINK_CFG_FREQ_MODE2_8HZ		BIT(3)
#define MXL86110_LED_BLINK_CFG_FREQ_MODE2_16HZ		(BIT(3) | BIT(2))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_50_ON		0
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_67_ON		(BIT(4))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_75_ON		(BIT(5))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_83_ON		(BIT(5) | BIT(4))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_50_OFF	(BIT(6))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_33_ON		(BIT(6) | BIT(4))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_25_ON		(BIT(6) | BIT(5))
#define MXL86110_LED_BLINK_CFG_DUTY_CYCLE_17_ON	(BIT(6) | BIT(5) | BIT(4))

/* Chip Configuration Register - COM_EXT_CHIP_CFG */
#define MXL86110_EXT_CHIP_CFG_REG			0xA001
#define MXL86110_EXT_CHIP_CFG_RXDLY_ENABLE		BIT(8)
#define MXL86110_EXT_CHIP_CFG_SW_RST_N_MODE		BIT(15)

/**
 * __mxl86110_write_extended_reg() - write to a PHY's extended register
 * @phydev: pointer to the PHY device structure
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * Unlocked version of mxl86110_write_extended_reg
 *
 * Note: This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 0 or negative error code
 */
static int __mxl86110_write_extended_reg(struct phy_device *phydev,
					 u16 regnum, u16 val)
{
	int ret;

	ret = __phy_write(phydev, MXL86110_EXTD_REG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	return __phy_write(phydev, MXL86110_EXTD_REG_ADDR_DATA, val);
}

/**
 * __mxl86110_read_extended_reg - Read a PHY's extended register
 * @phydev: pointer to the PHY device structure
 * @regnum: extended register number to read (address written to reg 30)
 *
 * Unlocked version of mxl86110_read_extended_reg
 *
 * Reads the content of a PHY extended register using the MaxLinear
 * 2-step access mechanism: write the register address to reg 30 (0x1E),
 * then read the value from reg 31 (0x1F).
 *
 * Note: This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 16-bit register value on success, or negative errno code on failure.
 */
static int __mxl86110_read_extended_reg(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = __phy_write(phydev, MXL86110_EXTD_REG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;
	return __phy_read(phydev, MXL86110_EXTD_REG_ADDR_DATA);
}

/**
 * __mxl86110_modify_extended_reg() - modify bits of a PHY's extended register
 * @phydev: pointer to the PHY device structure
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Note: register value = (old register value & ~mask) | set.
 * This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 0 or negative error code
 */
static int __mxl86110_modify_extended_reg(struct phy_device *phydev,
					  u16 regnum, u16 mask, u16 set)
{
	int ret;

	ret = __phy_write(phydev, MXL86110_EXTD_REG_ADDR_OFFSET, regnum);
	if (ret < 0)
		return ret;

	return __phy_modify(phydev, MXL86110_EXTD_REG_ADDR_DATA, mask, set);
}

/**
 * mxl86110_write_extended_reg() - Write to a PHY's extended register
 * @phydev: pointer to the PHY device structure
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * This function writes to an extended register of the PHY using the
 * MaxLinear two-step access method (reg 0x1E/0x1F). It handles acquiring
 * and releasing the MDIO bus lock internally.
 *
 * Return: 0 or negative error code
 */
static int mxl86110_write_extended_reg(struct phy_device *phydev,
				       u16 regnum, u16 val)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __mxl86110_write_extended_reg(phydev, regnum, val);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * mxl86110_read_extended_reg() - Read a PHY's extended register
 * @phydev: pointer to the PHY device structure
 * @regnum: extended register number to read
 *
 * This function reads from an extended register of the PHY using the
 * MaxLinear two-step access method (reg 0x1E/0x1F). It handles acquiring
 * and releasing the MDIO bus lock internally.
 *
 * Return: 16-bit register value on success, or negative errno code on failure
 */
static int mxl86110_read_extended_reg(struct phy_device *phydev, u16 regnum)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __mxl86110_read_extended_reg(phydev, regnum);
	phy_unlock_mdio_bus(phydev);

	return ret;
}

/**
 * mxl86110_get_wol() - report if wake-on-lan is enabled
 * @phydev: pointer to the phy_device
 * @wol: a pointer to a &struct ethtool_wolinfo
 */
static void mxl86110_get_wol(struct phy_device *phydev,
			     struct ethtool_wolinfo *wol)
{
	int val;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;
	val = mxl86110_read_extended_reg(phydev, MXL86110_EXT_WOL_CFG_REG);
	if (val >= 0 && (val & MXL86110_WOL_CFG_WOL_MASK))
		wol->wolopts |= WAKE_MAGIC;
}

/**
 * mxl86110_set_wol() - enable/disable wake-on-lan
 * @phydev: pointer to the phy_device
 * @wol: a pointer to a &struct ethtool_wolinfo
 *
 * Configures the WOL Magic Packet MAC
 *
 * Return: 0 or negative errno code
 */
static int mxl86110_set_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	struct net_device *netdev;
	const unsigned char *mac;
	int ret = 0;

	phy_lock_mdio_bus(phydev);

	if (wol->wolopts & WAKE_MAGIC) {
		netdev = phydev->attached_dev;
		if (!netdev) {
			ret = -ENODEV;
			goto out;
		}

		/* Configure the MAC address of the WOL magic packet */
		mac = netdev->dev_addr;
		ret = __mxl86110_write_extended_reg(phydev,
						    MXL86110_EXT_MAC_ADDR_CFG1,
						    ((mac[0] << 8) | mac[1]));
		if (ret < 0)
			goto out;

		ret = __mxl86110_write_extended_reg(phydev,
						    MXL86110_EXT_MAC_ADDR_CFG2,
						    ((mac[2] << 8) | mac[3]));
		if (ret < 0)
			goto out;

		ret = __mxl86110_write_extended_reg(phydev,
						    MXL86110_EXT_MAC_ADDR_CFG3,
						    ((mac[4] << 8) | mac[5]));
		if (ret < 0)
			goto out;

		ret = __mxl86110_modify_extended_reg(phydev,
						     MXL86110_EXT_WOL_CFG_REG,
						     MXL86110_WOL_CFG_WOL_MASK,
						     MXL86110_WOL_CFG_WOL_MASK);
		if (ret < 0)
			goto out;

		/* Enables Wake-on-LAN interrupt in the PHY. */
		ret = __phy_modify(phydev, PHY_IRQ_ENABLE_REG, 0,
				   PHY_IRQ_ENABLE_REG_WOL);
		if (ret < 0)
			goto out;

		phydev_dbg(phydev,
			   "%s, MAC Addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
			   __func__,
			   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		ret = __mxl86110_modify_extended_reg(phydev,
						     MXL86110_EXT_WOL_CFG_REG,
						     MXL86110_WOL_CFG_WOL_MASK,
						     0);
		if (ret < 0)
			goto out;

		/* Disables Wake-on-LAN interrupt in the PHY. */
		ret = __phy_modify(phydev, PHY_IRQ_ENABLE_REG,
				   PHY_IRQ_ENABLE_REG_WOL, 0);
	}

out:
	phy_unlock_mdio_bus(phydev);
	return ret;
}

static const unsigned long supported_trgs = (BIT(TRIGGER_NETDEV_LINK_10) |
					     BIT(TRIGGER_NETDEV_LINK_100) |
					     BIT(TRIGGER_NETDEV_LINK_1000) |
					     BIT(TRIGGER_NETDEV_HALF_DUPLEX) |
					     BIT(TRIGGER_NETDEV_FULL_DUPLEX) |
					     BIT(TRIGGER_NETDEV_TX) |
					     BIT(TRIGGER_NETDEV_RX));

static int mxl86110_led_hw_is_supported(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	if (index >= MXL86110_MAX_LEDS)
		return -EINVAL;

	/* All combinations of the supported triggers are allowed */
	if (rules & ~supported_trgs)
		return -EOPNOTSUPP;

	return 0;
}

static int mxl86110_led_hw_control_get(struct phy_device *phydev, u8 index,
				       unsigned long *rules)
{
	int val;

	if (index >= MXL86110_MAX_LEDS)
		return -EINVAL;

	val = mxl86110_read_extended_reg(phydev,
					 MXL86110_LED0_CFG_REG + index);
	if (val < 0)
		return val;

	if (val & MXL86110_LEDX_CFG_LINK_UP_TX_ACT_ON)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	if (val & MXL86110_LEDX_CFG_LINK_UP_RX_ACT_ON)
		*rules |= BIT(TRIGGER_NETDEV_RX);

	if (val & MXL86110_LEDX_CFG_LINK_UP_HALF_DUPLEX_ON)
		*rules |= BIT(TRIGGER_NETDEV_HALF_DUPLEX);

	if (val & MXL86110_LEDX_CFG_LINK_UP_FULL_DUPLEX_ON)
		*rules |= BIT(TRIGGER_NETDEV_FULL_DUPLEX);

	if (val & MXL86110_LEDX_CFG_LINK_UP_10MB_ON)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10);

	if (val & MXL86110_LEDX_CFG_LINK_UP_100MB_ON)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);

	if (val & MXL86110_LEDX_CFG_LINK_UP_1GB_ON)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);

	return 0;
}

static int mxl86110_led_hw_control_set(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	u16 val = 0;

	if (index >= MXL86110_MAX_LEDS)
		return -EINVAL;

	if (rules & BIT(TRIGGER_NETDEV_LINK_10))
		val |= MXL86110_LEDX_CFG_LINK_UP_10MB_ON;

	if (rules & BIT(TRIGGER_NETDEV_LINK_100))
		val |= MXL86110_LEDX_CFG_LINK_UP_100MB_ON;

	if (rules & BIT(TRIGGER_NETDEV_LINK_1000))
		val |= MXL86110_LEDX_CFG_LINK_UP_1GB_ON;

	if (rules & BIT(TRIGGER_NETDEV_TX))
		val |= MXL86110_LEDX_CFG_LINK_UP_TX_ACT_ON;

	if (rules & BIT(TRIGGER_NETDEV_RX))
		val |= MXL86110_LEDX_CFG_LINK_UP_RX_ACT_ON;

	if (rules & BIT(TRIGGER_NETDEV_HALF_DUPLEX))
		val |= MXL86110_LEDX_CFG_LINK_UP_HALF_DUPLEX_ON;

	if (rules & BIT(TRIGGER_NETDEV_FULL_DUPLEX))
		val |= MXL86110_LEDX_CFG_LINK_UP_FULL_DUPLEX_ON;

	if (rules & BIT(TRIGGER_NETDEV_TX) ||
	    rules & BIT(TRIGGER_NETDEV_RX))
		val |= MXL86110_LEDX_CFG_BLINK;

	return mxl86110_write_extended_reg(phydev,
					  MXL86110_LED0_CFG_REG + index, val);
}

/**
 * mxl86110_synce_clk_cfg() - applies syncE/clk output configuration
 * @phydev: pointer to the phy_device
 *
 * Note: This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 0 or negative errno code
 */
static int mxl86110_synce_clk_cfg(struct phy_device *phydev)
{
	u16 mask = 0, val = 0;

	/*
	 * Configures the clock output to its default
	 * setting as per the datasheet.
	 * This results in a 25MHz clock output being selected in the
	 * COM_EXT_SYNCE_CFG register for SyncE configuration.
	 */
	val = MXL86110_EXT_SYNCE_CFG_EN_SYNC_E |
			FIELD_PREP(MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_MASK,
				   MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_25M);
	mask = MXL86110_EXT_SYNCE_CFG_EN_SYNC_E |
	       MXL86110_EXT_SYNCE_CFG_CLK_SRC_SEL_MASK |
	       MXL86110_EXT_SYNCE_CFG_CLK_FRE_SEL;

	/* Write clock output configuration */
	return __mxl86110_modify_extended_reg(phydev,
					      MXL86110_EXT_SYNCE_CFG_REG,
					      mask, val);
}

/**
 * mxl86110_broadcast_cfg - Configure MDIO broadcast setting for PHY
 * @phydev: Pointer to the PHY device structure
 *
 * This function configures the MDIO broadcast behavior of the MxL86110 PHY.
 * Currently, broadcast mode is explicitly disabled by clearing the EPA0 bit
 * in the RGMII_MDIO_CFG extended register.
 *
 * Note: This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 0 on success or a negative errno code on failure.
 */
static int mxl86110_broadcast_cfg(struct phy_device *phydev)
{
	return __mxl86110_modify_extended_reg(phydev,
					      MXL86110_EXT_RGMII_MDIO_CFG,
					      MXL86110_RGMII_MDIO_CFG_EPA0_MASK,
					      0);
}

/**
 * mxl86110_enable_led_activity_blink - Enable LEDs activity blink on PHY
 * @phydev: Pointer to the PHY device structure
 *
 * Configure all PHY LEDs to blink on traffic activity regardless of whether
 * they are ON or OFF. This behavior allows each LED to serve as a pure activity
 * indicator, independently of its use as a link status indicator.
 *
 * By default, each LED blinks only when it is also in the ON state.
 * This function modifies the appropriate registers (LABx fields)
 * to enable blinking even when the LEDs are OFF, to allow the LED to be used
 * as a traffic indicator without requiring it to also serve
 * as a link status LED.
 *
 * Note: Any further LED customization can be performed via the
 * /sys/class/leds interface; the functions led_hw_is_supported,
 * led_hw_control_get, and led_hw_control_set are used
 * to support this mechanism.
 *
 * This function assumes the caller already holds the MDIO bus lock
 * or otherwise has exclusive access to the PHY.
 *
 * Return: 0 on success or a negative errno code on failure.
 */
static int mxl86110_enable_led_activity_blink(struct phy_device *phydev)
{
	int i, ret = 0;

	for (i = 0; i < MXL86110_MAX_LEDS; i++) {
		ret = __mxl86110_modify_extended_reg(phydev,
						     MXL86110_LED0_CFG_REG + i,
						     0,
						     MXL86110_LEDX_CFG_BLINK);
		if (ret < 0)
			break;
	}

	return ret;
}

/**
 * mxl86110_config_init() - initialize the PHY
 * @phydev: pointer to the phy_device
 *
 * Return: 0 or negative errno code
 */
static int mxl86110_config_init(struct phy_device *phydev)
{
	u16 val = 0;
	int ret;

	phy_lock_mdio_bus(phydev);

	/* configure syncE / clk output */
	ret = mxl86110_synce_clk_cfg(phydev);
	if (ret < 0)
		goto out;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		val = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		val = MXL86110_EXT_RGMII_CFG1_RX_DELAY_1950PS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = MXL86110_EXT_RGMII_CFG1_TX_1G_DELAY_1950PS |
			MXL86110_EXT_RGMII_CFG1_TX_10MB_100MB_DELAY_1950PS;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		val = MXL86110_EXT_RGMII_CFG1_TX_1G_DELAY_1950PS |
			MXL86110_EXT_RGMII_CFG1_TX_10MB_100MB_DELAY_1950PS |
			MXL86110_EXT_RGMII_CFG1_RX_DELAY_1950PS;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = __mxl86110_modify_extended_reg(phydev,
					     MXL86110_EXT_RGMII_CFG1_REG,
					     MXL86110_EXT_RGMII_CFG1_FULL_MASK,
					     val);
	if (ret < 0)
		goto out;

	/* Configure RXDLY (RGMII Rx Clock Delay) to disable
	 * the default additional delay value on RX_CLK
	 * (2 ns for 125 MHz, 8 ns for 25 MHz/2.5 MHz)
	 * and use just the digital one selected before
	 */
	ret = __mxl86110_modify_extended_reg(phydev,
					     MXL86110_EXT_CHIP_CFG_REG,
					     MXL86110_EXT_CHIP_CFG_RXDLY_ENABLE,
					     0);
	if (ret < 0)
		goto out;

	ret = mxl86110_enable_led_activity_blink(phydev);
	if (ret < 0)
		goto out;

	ret = mxl86110_broadcast_cfg(phydev);

out:
	phy_unlock_mdio_bus(phydev);
	return ret;
}

static struct phy_driver mxl_phy_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_MXL86110),
		.name			= "MXL86110 Gigabit Ethernet",
		.config_init		= mxl86110_config_init,
		.get_wol		= mxl86110_get_wol,
		.set_wol		= mxl86110_set_wol,
		.led_hw_is_supported	= mxl86110_led_hw_is_supported,
		.led_hw_control_get     = mxl86110_led_hw_control_get,
		.led_hw_control_set     = mxl86110_led_hw_control_set,
	},
};

module_phy_driver(mxl_phy_drvs);

static const struct mdio_device_id __maybe_unused mxl_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_MXL86110) },
	{  }
};

MODULE_DEVICE_TABLE(mdio, mxl_tbl);

MODULE_DESCRIPTION("MaxLinear MXL86110 PHY driver");
MODULE_AUTHOR("Stefano Radaelli");
MODULE_LICENSE("GPL");
