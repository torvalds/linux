// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Daniel Schwierzeck <daniel.schwierzeck@googlemail.com>
 * Copyright (C) 2016 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/bitfield.h>

#define XWAY_MDIO_MIICTRL		0x17	/* mii control */
#define XWAY_MDIO_IMASK			0x19	/* interrupt mask */
#define XWAY_MDIO_ISTAT			0x1A	/* interrupt status */
#define XWAY_MDIO_LED			0x1B	/* led control */

#define XWAY_MDIO_MIICTRL_RXSKEW_MASK	GENMASK(14, 12)
#define XWAY_MDIO_MIICTRL_TXSKEW_MASK	GENMASK(10, 8)

/* bit 15:12 are reserved */
#define XWAY_MDIO_LED_LED3_EN		BIT(11)	/* Enable the integrated function of LED3 */
#define XWAY_MDIO_LED_LED2_EN		BIT(10)	/* Enable the integrated function of LED2 */
#define XWAY_MDIO_LED_LED1_EN		BIT(9)	/* Enable the integrated function of LED1 */
#define XWAY_MDIO_LED_LED0_EN		BIT(8)	/* Enable the integrated function of LED0 */
/* bit 7:4 are reserved */
#define XWAY_MDIO_LED_LED3_DA		BIT(3)	/* Direct Access to LED3 */
#define XWAY_MDIO_LED_LED2_DA		BIT(2)	/* Direct Access to LED2 */
#define XWAY_MDIO_LED_LED1_DA		BIT(1)	/* Direct Access to LED1 */
#define XWAY_MDIO_LED_LED0_DA		BIT(0)	/* Direct Access to LED0 */

#define XWAY_MDIO_INIT_WOL		BIT(15)	/* Wake-On-LAN */
#define XWAY_MDIO_INIT_MSRE		BIT(14)
#define XWAY_MDIO_INIT_NPRX		BIT(13)
#define XWAY_MDIO_INIT_NPTX		BIT(12)
#define XWAY_MDIO_INIT_ANE		BIT(11)	/* Auto-Neg error */
#define XWAY_MDIO_INIT_ANC		BIT(10)	/* Auto-Neg complete */
#define XWAY_MDIO_INIT_ADSC		BIT(5)	/* Link auto-downspeed detect */
#define XWAY_MDIO_INIT_MPIPC		BIT(4)
#define XWAY_MDIO_INIT_MDIXC		BIT(3)
#define XWAY_MDIO_INIT_DXMC		BIT(2)	/* Duplex mode change */
#define XWAY_MDIO_INIT_LSPC		BIT(1)	/* Link speed change */
#define XWAY_MDIO_INIT_LSTC		BIT(0)	/* Link state change */
#define XWAY_MDIO_INIT_MASK		(XWAY_MDIO_INIT_LSTC | \
					 XWAY_MDIO_INIT_ADSC)

#define ADVERTISED_MPD			BIT(10)	/* Multi-port device */

/* LED Configuration */
#define XWAY_MMD_LEDCH			0x01E0
/* Inverse of SCAN Function */
#define  XWAY_MMD_LEDCH_NACS_NONE	0x0000
#define  XWAY_MMD_LEDCH_NACS_LINK	0x0001
#define  XWAY_MMD_LEDCH_NACS_PDOWN	0x0002
#define  XWAY_MMD_LEDCH_NACS_EEE	0x0003
#define  XWAY_MMD_LEDCH_NACS_ANEG	0x0004
#define  XWAY_MMD_LEDCH_NACS_ABIST	0x0005
#define  XWAY_MMD_LEDCH_NACS_CDIAG	0x0006
#define  XWAY_MMD_LEDCH_NACS_TEST	0x0007
/* Slow Blink Frequency */
#define  XWAY_MMD_LEDCH_SBF_F02HZ	0x0000
#define  XWAY_MMD_LEDCH_SBF_F04HZ	0x0010
#define  XWAY_MMD_LEDCH_SBF_F08HZ	0x0020
#define  XWAY_MMD_LEDCH_SBF_F16HZ	0x0030
/* Fast Blink Frequency */
#define  XWAY_MMD_LEDCH_FBF_F02HZ	0x0000
#define  XWAY_MMD_LEDCH_FBF_F04HZ	0x0040
#define  XWAY_MMD_LEDCH_FBF_F08HZ	0x0080
#define  XWAY_MMD_LEDCH_FBF_F16HZ	0x00C0
/* LED Configuration */
#define XWAY_MMD_LEDCL			0x01E1
/* Complex Blinking Configuration */
#define  XWAY_MMD_LEDCH_CBLINK_NONE	0x0000
#define  XWAY_MMD_LEDCH_CBLINK_LINK	0x0001
#define  XWAY_MMD_LEDCH_CBLINK_PDOWN	0x0002
#define  XWAY_MMD_LEDCH_CBLINK_EEE	0x0003
#define  XWAY_MMD_LEDCH_CBLINK_ANEG	0x0004
#define  XWAY_MMD_LEDCH_CBLINK_ABIST	0x0005
#define  XWAY_MMD_LEDCH_CBLINK_CDIAG	0x0006
#define  XWAY_MMD_LEDCH_CBLINK_TEST	0x0007
/* Complex SCAN Configuration */
#define  XWAY_MMD_LEDCH_SCAN_NONE	0x0000
#define  XWAY_MMD_LEDCH_SCAN_LINK	0x0010
#define  XWAY_MMD_LEDCH_SCAN_PDOWN	0x0020
#define  XWAY_MMD_LEDCH_SCAN_EEE	0x0030
#define  XWAY_MMD_LEDCH_SCAN_ANEG	0x0040
#define  XWAY_MMD_LEDCH_SCAN_ABIST	0x0050
#define  XWAY_MMD_LEDCH_SCAN_CDIAG	0x0060
#define  XWAY_MMD_LEDCH_SCAN_TEST	0x0070
/* Configuration for LED Pin x */
#define XWAY_MMD_LED0H			0x01E2
/* Fast Blinking Configuration */
#define  XWAY_MMD_LEDxH_BLINKF_MASK	0x000F
#define  XWAY_MMD_LEDxH_BLINKF_NONE	0x0000
#define  XWAY_MMD_LEDxH_BLINKF_LINK10	0x0001
#define  XWAY_MMD_LEDxH_BLINKF_LINK100	0x0002
#define  XWAY_MMD_LEDxH_BLINKF_LINK10X	0x0003
#define  XWAY_MMD_LEDxH_BLINKF_LINK1000	0x0004
#define  XWAY_MMD_LEDxH_BLINKF_LINK10_0	0x0005
#define  XWAY_MMD_LEDxH_BLINKF_LINK100X	0x0006
#define  XWAY_MMD_LEDxH_BLINKF_LINK10XX	0x0007
#define  XWAY_MMD_LEDxH_BLINKF_PDOWN	0x0008
#define  XWAY_MMD_LEDxH_BLINKF_EEE	0x0009
#define  XWAY_MMD_LEDxH_BLINKF_ANEG	0x000A
#define  XWAY_MMD_LEDxH_BLINKF_ABIST	0x000B
#define  XWAY_MMD_LEDxH_BLINKF_CDIAG	0x000C
/* Constant On Configuration */
#define  XWAY_MMD_LEDxH_CON_MASK	0x00F0
#define  XWAY_MMD_LEDxH_CON_NONE	0x0000
#define  XWAY_MMD_LEDxH_CON_LINK10	0x0010
#define  XWAY_MMD_LEDxH_CON_LINK100	0x0020
#define  XWAY_MMD_LEDxH_CON_LINK10X	0x0030
#define  XWAY_MMD_LEDxH_CON_LINK1000	0x0040
#define  XWAY_MMD_LEDxH_CON_LINK10_0	0x0050
#define  XWAY_MMD_LEDxH_CON_LINK100X	0x0060
#define  XWAY_MMD_LEDxH_CON_LINK10XX	0x0070
#define  XWAY_MMD_LEDxH_CON_PDOWN	0x0080
#define  XWAY_MMD_LEDxH_CON_EEE		0x0090
#define  XWAY_MMD_LEDxH_CON_ANEG	0x00A0
#define  XWAY_MMD_LEDxH_CON_ABIST	0x00B0
#define  XWAY_MMD_LEDxH_CON_CDIAG	0x00C0
#define  XWAY_MMD_LEDxH_CON_COPPER	0x00D0
#define  XWAY_MMD_LEDxH_CON_FIBER	0x00E0
/* Configuration for LED Pin x */
#define XWAY_MMD_LED0L			0x01E3
/* Pulsing Configuration */
#define  XWAY_MMD_LEDxL_PULSE_MASK	0x000F
#define  XWAY_MMD_LEDxL_PULSE_NONE	0x0000
#define  XWAY_MMD_LEDxL_PULSE_TXACT	0x0001
#define  XWAY_MMD_LEDxL_PULSE_RXACT	0x0002
#define  XWAY_MMD_LEDxL_PULSE_COL	0x0004
/* Slow Blinking Configuration */
#define  XWAY_MMD_LEDxL_BLINKS_MASK	0x00F0
#define  XWAY_MMD_LEDxL_BLINKS_NONE	0x0000
#define  XWAY_MMD_LEDxL_BLINKS_LINK10	0x0010
#define  XWAY_MMD_LEDxL_BLINKS_LINK100	0x0020
#define  XWAY_MMD_LEDxL_BLINKS_LINK10X	0x0030
#define  XWAY_MMD_LEDxL_BLINKS_LINK1000	0x0040
#define  XWAY_MMD_LEDxL_BLINKS_LINK10_0	0x0050
#define  XWAY_MMD_LEDxL_BLINKS_LINK100X	0x0060
#define  XWAY_MMD_LEDxL_BLINKS_LINK10XX	0x0070
#define  XWAY_MMD_LEDxL_BLINKS_PDOWN	0x0080
#define  XWAY_MMD_LEDxL_BLINKS_EEE	0x0090
#define  XWAY_MMD_LEDxL_BLINKS_ANEG	0x00A0
#define  XWAY_MMD_LEDxL_BLINKS_ABIST	0x00B0
#define  XWAY_MMD_LEDxL_BLINKS_CDIAG	0x00C0
#define XWAY_MMD_LED1H			0x01E4
#define XWAY_MMD_LED1L			0x01E5
#define XWAY_MMD_LED2H			0x01E6
#define XWAY_MMD_LED2L			0x01E7
#define XWAY_MMD_LED3H			0x01E8
#define XWAY_MMD_LED3L			0x01E9

#define XWAY_GPHY_MAX_LEDS		3
#define XWAY_GPHY_LED_INV(idx)		BIT(12 + (idx))
#define XWAY_GPHY_LED_EN(idx)		BIT(8 + (idx))
#define XWAY_GPHY_LED_DA(idx)		BIT(idx)
#define XWAY_MMD_LEDxH(idx)		(XWAY_MMD_LED0H + 2 * (idx))
#define XWAY_MMD_LEDxL(idx)		(XWAY_MMD_LED0L + 2 * (idx))

#define PHY_ID_PHY11G_1_3		0x030260D1
#define PHY_ID_PHY22F_1_3		0x030260E1
#define PHY_ID_PHY11G_1_4		0xD565A400
#define PHY_ID_PHY22F_1_4		0xD565A410
#define PHY_ID_PHY11G_1_5		0xD565A401
#define PHY_ID_PHY22F_1_5		0xD565A411
#define PHY_ID_PHY11G_VR9_1_1		0xD565A408
#define PHY_ID_PHY22F_VR9_1_1		0xD565A418
#define PHY_ID_PHY11G_VR9_1_2		0xD565A409
#define PHY_ID_PHY22F_VR9_1_2		0xD565A419

static const int xway_internal_delay[] = {0, 500, 1000, 1500, 2000, 2500,
					 3000, 3500};

static int xway_gphy_rgmii_init(struct phy_device *phydev)
{
	unsigned int delay_size = ARRAY_SIZE(xway_internal_delay);
	s32 int_delay;
	int val = 0;

	if (!phy_interface_is_rgmii(phydev))
		return 0;

	/* Existing behavior was to use default pin strapping delay in rgmii
	 * mode, but rgmii should have meant no delay.  Warn existing users,
	 * but do not change anything at the moment.
	 */
	if (phydev->interface == PHY_INTERFACE_MODE_RGMII) {
		u16 txskew, rxskew;

		val = phy_read(phydev, XWAY_MDIO_MIICTRL);
		if (val < 0)
			return val;

		txskew = FIELD_GET(XWAY_MDIO_MIICTRL_TXSKEW_MASK, val);
		rxskew = FIELD_GET(XWAY_MDIO_MIICTRL_RXSKEW_MASK, val);

		if (txskew > 0 || rxskew > 0)
			phydev_warn(phydev,
				    "PHY has delays (e.g. via pin strapping), but phy-mode = 'rgmii'\n"
				    "Should be 'rgmii-id' to use internal delays txskew:%d ps rxskew:%d ps\n",
				    xway_internal_delay[txskew],
				    xway_internal_delay[rxskew]);
		return 0;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		int_delay = phy_get_internal_delay(phydev, xway_internal_delay,
						   delay_size, true);

		/* if rx-internal-delay-ps is missing, use default of 2.0 ns */
		if (int_delay < 0)
			int_delay = 4; /* 2000 ps */

		val |= FIELD_PREP(XWAY_MDIO_MIICTRL_RXSKEW_MASK, int_delay);
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		int_delay = phy_get_internal_delay(phydev, xway_internal_delay,
						   delay_size, false);

		/* if tx-internal-delay-ps is missing, use default of 2.0 ns */
		if (int_delay < 0)
			int_delay = 4; /* 2000 ps */

		val |= FIELD_PREP(XWAY_MDIO_MIICTRL_TXSKEW_MASK, int_delay);
	}

	return phy_modify(phydev, XWAY_MDIO_MIICTRL,
			  XWAY_MDIO_MIICTRL_RXSKEW_MASK |
			  XWAY_MDIO_MIICTRL_TXSKEW_MASK, val);
}

static int xway_gphy_init_leds(struct phy_device *phydev)
{
	int err;
	u32 ledxh;
	u32 ledxl;

	/* Ensure that integrated led function is enabled for all leds */
	err = phy_write(phydev, XWAY_MDIO_LED,
			XWAY_MDIO_LED_LED0_EN |
			XWAY_MDIO_LED_LED1_EN |
			XWAY_MDIO_LED_LED2_EN |
			XWAY_MDIO_LED_LED3_EN);
	if (err)
		return err;

	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDCH,
		      XWAY_MMD_LEDCH_NACS_NONE |
		      XWAY_MMD_LEDCH_SBF_F02HZ |
		      XWAY_MMD_LEDCH_FBF_F16HZ);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDCL,
		      XWAY_MMD_LEDCH_CBLINK_NONE |
		      XWAY_MMD_LEDCH_SCAN_NONE);

	/**
	 * In most cases only one LED is connected to this phy, so
	 * configure them all to constant on and pulse mode. LED3 is
	 * only available in some packages, leave it in its reset
	 * configuration.
	 */
	ledxh = XWAY_MMD_LEDxH_BLINKF_NONE | XWAY_MMD_LEDxH_CON_LINK10XX;
	ledxl = XWAY_MMD_LEDxL_PULSE_TXACT | XWAY_MMD_LEDxL_PULSE_RXACT |
		XWAY_MMD_LEDxL_BLINKS_NONE;
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED0H, ledxh);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED0L, ledxl);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED1H, ledxh);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED1L, ledxl);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED2H, ledxh);
	phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LED2L, ledxl);

	return 0;
}

static int xway_gphy_config_init(struct phy_device *phydev)
{
	struct device_node *np = phydev->mdio.dev.of_node;
	int err;

	/* Mask all interrupts */
	err = phy_write(phydev, XWAY_MDIO_IMASK, 0);
	if (err)
		return err;

	/* Use default LED configuration if 'leds' node isn't defined */
	if (!of_get_child_by_name(np, "leds"))
		xway_gphy_init_leds(phydev);

	/* Clear all pending interrupts */
	phy_read(phydev, XWAY_MDIO_ISTAT);

	err = xway_gphy_rgmii_init(phydev);
	if (err)
		return err;

	return 0;
}

static int xway_gphy14_config_aneg(struct phy_device *phydev)
{
	int reg, err;

	/* Advertise as multi-port device, see IEEE802.3-2002 40.5.1.1 */
	/* This is a workaround for an errata in rev < 1.5 devices */
	reg = phy_read(phydev, MII_CTRL1000);
	reg |= ADVERTISED_MPD;
	err = phy_write(phydev, MII_CTRL1000, reg);
	if (err)
		return err;

	return genphy_config_aneg(phydev);
}

static int xway_gphy_ack_interrupt(struct phy_device *phydev)
{
	int reg;

	reg = phy_read(phydev, XWAY_MDIO_ISTAT);
	return (reg < 0) ? reg : 0;
}

static int xway_gphy_config_intr(struct phy_device *phydev)
{
	u16 mask = 0;
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = xway_gphy_ack_interrupt(phydev);
		if (err)
			return err;

		mask = XWAY_MDIO_INIT_MASK;
		err = phy_write(phydev, XWAY_MDIO_IMASK, mask);
	} else {
		err = phy_write(phydev, XWAY_MDIO_IMASK, mask);
		if (err)
			return err;

		err = xway_gphy_ack_interrupt(phydev);
	}

	return err;
}

static irqreturn_t xway_gphy_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, XWAY_MDIO_ISTAT);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & XWAY_MDIO_INIT_MASK))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int xway_gphy_led_brightness_set(struct phy_device *phydev,
					u8 index, enum led_brightness value)
{
	int ret;

	if (index >= XWAY_GPHY_MAX_LEDS)
		return -EINVAL;

	/* clear EN and set manual LED state */
	ret = phy_modify(phydev, XWAY_MDIO_LED,
			 ((value == LED_OFF) ? XWAY_GPHY_LED_EN(index) : 0) |
			 XWAY_GPHY_LED_DA(index),
			 (value == LED_OFF) ? 0 : XWAY_GPHY_LED_DA(index));
	if (ret)
		return ret;

	/* clear HW LED setup */
	if (value == LED_OFF) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxH(index), 0);
		if (ret)
			return ret;

		return phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxL(index), 0);
	} else {
		return 0;
	}
}

static const unsigned long supported_triggers = (BIT(TRIGGER_NETDEV_LINK) |
						 BIT(TRIGGER_NETDEV_LINK_10) |
						 BIT(TRIGGER_NETDEV_LINK_100) |
						 BIT(TRIGGER_NETDEV_LINK_1000) |
						 BIT(TRIGGER_NETDEV_RX) |
						 BIT(TRIGGER_NETDEV_TX));

static int xway_gphy_led_hw_is_supported(struct phy_device *phydev, u8 index,
					 unsigned long rules)
{
	if (index >= XWAY_GPHY_MAX_LEDS)
		return -EINVAL;

	/* activity triggers are not possible without combination with a link
	 * trigger.
	 */
	if (rules & (BIT(TRIGGER_NETDEV_RX) | BIT(TRIGGER_NETDEV_TX)) &&
	    !(rules & (BIT(TRIGGER_NETDEV_LINK) |
		       BIT(TRIGGER_NETDEV_LINK_10) |
		       BIT(TRIGGER_NETDEV_LINK_100) |
		       BIT(TRIGGER_NETDEV_LINK_1000))))
		return -EOPNOTSUPP;

	/* All other combinations of the supported triggers are allowed */
	if (rules & ~supported_triggers)
		return -EOPNOTSUPP;

	return 0;
}

static int xway_gphy_led_hw_control_get(struct phy_device *phydev, u8 index,
					unsigned long *rules)
{
	int lval, hval;

	if (index >= XWAY_GPHY_MAX_LEDS)
		return -EINVAL;

	hval = phy_read_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxH(index));
	if (hval < 0)
		return hval;

	lval = phy_read_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxL(index));
	if (lval < 0)
		return lval;

	if (hval & XWAY_MMD_LEDxH_CON_LINK10)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10);

	if (hval & XWAY_MMD_LEDxH_CON_LINK100)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);

	if (hval & XWAY_MMD_LEDxH_CON_LINK1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);

	if ((hval & XWAY_MMD_LEDxH_CON_LINK10) &&
	    (hval & XWAY_MMD_LEDxH_CON_LINK100) &&
	    (hval & XWAY_MMD_LEDxH_CON_LINK1000))
		*rules |= BIT(TRIGGER_NETDEV_LINK);

	if (lval & XWAY_MMD_LEDxL_PULSE_TXACT)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	if (lval & XWAY_MMD_LEDxL_PULSE_RXACT)
		*rules |= BIT(TRIGGER_NETDEV_RX);

	return 0;
}

static int xway_gphy_led_hw_control_set(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	u16 hval = 0, lval = 0;
	int ret;

	if (index >= XWAY_GPHY_MAX_LEDS)
		return -EINVAL;

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_10))
		hval |= XWAY_MMD_LEDxH_CON_LINK10;

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_100))
		hval |= XWAY_MMD_LEDxH_CON_LINK100;

	if (rules & BIT(TRIGGER_NETDEV_LINK) ||
	    rules & BIT(TRIGGER_NETDEV_LINK_1000))
		hval |= XWAY_MMD_LEDxH_CON_LINK1000;

	if (rules & BIT(TRIGGER_NETDEV_TX))
		lval |= XWAY_MMD_LEDxL_PULSE_TXACT;

	if (rules & BIT(TRIGGER_NETDEV_RX))
		lval |= XWAY_MMD_LEDxL_PULSE_RXACT;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxH(index), hval);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, XWAY_MMD_LEDxL(index), lval);
	if (ret)
		return ret;

	return phy_set_bits(phydev, XWAY_MDIO_LED, XWAY_GPHY_LED_EN(index));
}

static int xway_gphy_led_polarity_set(struct phy_device *phydev, int index,
				      unsigned long modes)
{
	bool force_active_low = false, force_active_high = false;
	u32 mode;

	if (index >= XWAY_GPHY_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			force_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH:
			force_active_high = true;
			break;
		default:
			return -EINVAL;
		}
	}

	if (force_active_low)
		return phy_set_bits(phydev, XWAY_MDIO_LED, XWAY_GPHY_LED_INV(index));

	if (force_active_high)
		return phy_clear_bits(phydev, XWAY_MDIO_LED, XWAY_GPHY_LED_INV(index));

	return -EINVAL;
}

static struct phy_driver xway_gphy[] = {
	{
		.phy_id		= PHY_ID_PHY11G_1_3,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY11G (PEF 7071/PEF 7072) v1.3",
		/* PHY_GBIT_FEATURES */
		.config_init	= xway_gphy_config_init,
		.config_aneg	= xway_gphy14_config_aneg,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY22F_1_3,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY22F (PEF 7061) v1.3",
		/* PHY_BASIC_FEATURES */
		.config_init	= xway_gphy_config_init,
		.config_aneg	= xway_gphy14_config_aneg,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY11G_1_4,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY11G (PEF 7071/PEF 7072) v1.4",
		/* PHY_GBIT_FEATURES */
		.config_init	= xway_gphy_config_init,
		.config_aneg	= xway_gphy14_config_aneg,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY22F_1_4,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY22F (PEF 7061) v1.4",
		/* PHY_BASIC_FEATURES */
		.config_init	= xway_gphy_config_init,
		.config_aneg	= xway_gphy14_config_aneg,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY11G_1_5,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY11G (PEF 7071/PEF 7072) v1.5 / v1.6",
		/* PHY_GBIT_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY22F_1_5,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY22F (PEF 7061) v1.5 / v1.6",
		/* PHY_BASIC_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY11G_VR9_1_1,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY11G (xRX v1.1 integrated)",
		/* PHY_GBIT_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY22F_VR9_1_1,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY22F (xRX v1.1 integrated)",
		/* PHY_BASIC_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY11G_VR9_1_2,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY11G (xRX v1.2 integrated)",
		/* PHY_GBIT_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	}, {
		.phy_id		= PHY_ID_PHY22F_VR9_1_2,
		.phy_id_mask	= 0xffffffff,
		.name		= "Intel XWAY PHY22F (xRX v1.2 integrated)",
		/* PHY_BASIC_FEATURES */
		.config_init	= xway_gphy_config_init,
		.handle_interrupt = xway_gphy_handle_interrupt,
		.config_intr	= xway_gphy_config_intr,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.led_brightness_set = xway_gphy_led_brightness_set,
		.led_hw_is_supported = xway_gphy_led_hw_is_supported,
		.led_hw_control_get = xway_gphy_led_hw_control_get,
		.led_hw_control_set = xway_gphy_led_hw_control_set,
		.led_polarity_set = xway_gphy_led_polarity_set,
	},
};
module_phy_driver(xway_gphy);

static const struct mdio_device_id __maybe_unused xway_gphy_tbl[] = {
	{ PHY_ID_PHY11G_1_3, 0xffffffff },
	{ PHY_ID_PHY22F_1_3, 0xffffffff },
	{ PHY_ID_PHY11G_1_4, 0xffffffff },
	{ PHY_ID_PHY22F_1_4, 0xffffffff },
	{ PHY_ID_PHY11G_1_5, 0xffffffff },
	{ PHY_ID_PHY22F_1_5, 0xffffffff },
	{ PHY_ID_PHY11G_VR9_1_1, 0xffffffff },
	{ PHY_ID_PHY22F_VR9_1_1, 0xffffffff },
	{ PHY_ID_PHY11G_VR9_1_2, 0xffffffff },
	{ PHY_ID_PHY22F_VR9_1_2, 0xffffffff },
	{ }
};
MODULE_DEVICE_TABLE(mdio, xway_gphy_tbl);

MODULE_DESCRIPTION("Intel XWAY PHY driver");
MODULE_LICENSE("GPL");
