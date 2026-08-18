// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the Airoha AN8801 Gigabit PHY.
 *
 * Copyright (C) 2025 Airoha Technology Corp.
 * Copyright (C) 2025 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/minmax.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/pm_wakeirq.h>

#include "air_phy_lib.h"

#define AN8801R_PHY_ID			0xc0ff0421

/* MII Registers */

/* MII Registers - Airoha Page 1 */
#define AN8801_EXT_REG_PHY		0x14
#define   AN8801_EXT_PHY_STATUS0	GENMASK(1, 0)
#define   AN8801_EXT_PHY_DOWNSHIFT_CTL	GENMASK(3, 2) /* 2 to 5 1G auto-neg attempts (0..3) */
#define   AN8801_EXT_PHY_DOWNSHIFT_EN	BIT(4)
#define   AN8801_EXT_PHY_CTRL0		BIT(5)
#define   AN8801_EXT_PHY_STATUS1	GENMASK(8, 6)
#define   AN8801_EXT_PHY_CTRL1		GENMASK(14, 9)

/* MII Registers - Airoha Page 4 */
#define AN8801_PBUS_ACCESS		BIT(28)
#define AN8801_PBUS_EPHY_ACCESS		BIT(24)
#define AN8801_PBUS_CL22_ACCESS		BIT(23)

/* BPBUS Registers */
#define AN8801_BPBUS_REG_LED_GPIO	0x54
#define AN8801_BPBUS_REG_LED_ID_SEL	0x58
#define   LED_ID_GPIO_SEL(led, gpio)	((led) << ((gpio) * 3))
#define   LED_ID_GPIO_SEL_MASK(gpio)	(0x7 << ((gpio) * 3))
#define AN8801_BPBUS_REG_GPIO_MODE	0x70
#define AN8801_BPBUS_REG_PHY_IRQ_GPIO	0x7c
#define   AN8801_PHY_IRQ_GPIO_NUM_MASK	GENMASK(19, 16)
#define   AN8801_PHY_IRQ_GPIO_NUM	1

#define AN8801_BPBUS_REG_CKO		0x1a4
#define AN8801_CKO_OUTPUT_MODE_AUTO	3

#define AN8801_BPBUS_REG_LINK_MODE	0x5054
#define  AN8801_BPBUS_LINK_MODE_1000	BIT(0)

#define AN8801_BPBUS_REG_BYPASS_PTP	0x21c004
#define   AN8801_BYP_PTP_SGMII_TO_GPHY	BIT(8)
#define   AN8801_BYP_PTP_RGMII_TO_GPHY	BIT(0)

#define AN8801_BPBUS_REG_TXDLY_STEP	0x21c024
#define   RGMII_DELAY_STEP_MASK		GENMASK(2, 0)
#define   RGMII_DELAY_NO_STEP		0
#define   RGMII_DELAY_STEP_1		1
#define   RGMII_DELAY_STEP_2		2
#define   RGMII_DELAY_STEP_3		3
#define   RGMII_DELAY_STEP_4		4
#define   RGMII_DELAY_STEP_5		5
#define   RGMII_DELAY_STEP_6		6
#define   RGMII_DELAY_STEP_7		7
#define   RGMII_TXDELAY_FORCE_MODE	BIT(24)

/* Default RGMII TX delay setting, corresponding to a 1.883ns delay */
#define AN8801_RGMII_TXDELAY_DEFAULT	RGMII_DELAY_STEP_4

#define AN8801_BPBUS_REG_RXDLY_STEP	0x21c02c
#define   RGMII_RXDELAY_ALIGN		BIT(4)
#define   RGMII_RXDELAY_FORCE_MODE	BIT(24)

/* Default RGMII RX delay setting, corresponding to a 1.992ns delay,
 * when align bit is set or -0.008ns otherwise.
 */
#define AN8801_RGMII_RXDELAY_DEFAULT	RGMII_DELAY_NO_STEP

#define AN8801_BPBUS_REG_EFIFO_CTL(x)	(0x270004 + (0x100 * (x))) /* 0..2 */
#define   AN8801_EFIFO_ALL_EN		GENMASK(7, 0)
#define   AN8801_EFIFO_RX_EN		BIT(0)
#define   AN8801_EFIFO_TX_EN		BIT(1)
#define   AN8801_EFIFO_RX_CLK_EN	BIT(2)
#define   AN8801_EFIFO_TX_CLK_EN	BIT(3)
#define   AN8801_EFIFO_RX_EEE_EN	BIT(4)
#define   AN8801_EFIFO_TX_EEE_EN	BIT(5)
#define   AN8801_EFIFO_RX_ODD_NIBBLE_EN	BIT(6)
#define   AN8801_EFIFO_TX_ODD_NIBBLE_EN	BIT(7)

#define AN8801_BPBUS_REG_WOL_MAC_16_47	0x285114
#define AN8801_BPBUS_REG_WOL_MAC_0_15	0x285118

#define AN8801_BPBUS_REG_WAKEUP_CTL1	0x285400
#define   AN8801_WOL_WAKE_MAGIC_EN	GENMASK(3, 1)
#define   AN8801_WOL_WAKE_LNKCHG_EN	BIT(4)

#define AN8801_BPBUS_REG_WAKEUP_CTL2	0x285404
#define   AN8801_WAKE_OUT_TYPE_PULSE	BIT(0) /* Set/Unset: Pulse/Static */
#define   AN8801_WAKE_OUT_POLARITY_NEG	BIT(1) /* Set/Unset: Negative/Positive */
#define   AN8801_WAKE_OUT_WIDTH		GENMASK(3, 2)
#define    AN8801_WAKE_OUT_84MS		0
#define    AN8801_WAKE_OUT_168MS	1
#define    AN8801_WAKE_OUT_336MS	2
#define    AN8801_WAKE_OUT_672MS	3
#define   AN8801_WAKE_OUT_EN		BIT(4)
#define   AN8801_PME_WAKEUP_CLR		BIT(8)

#define AN8801_BPBUS_REG_WAKE_IRQ_EN	0x285700
#define AN8801_BPBUS_REG_WAKE_IRQ_STS	0x285704
#define   AN8801_IRQ_WAKE_LNKCHG	BIT(0) /* Wake on link change */
#define   AN8801_IRQ_WAKE_UNIPKT	BIT(1) /* Wake on unicast packet */
#define   AN8801_IRQ_WAKE_MULPKT	BIT(2) /* Wake on multicast packet */
#define   AN8801_IRQ_WAKE_BCPKT		BIT(3) /* Wake on broadcast packet */
#define   AN8801_IRQ_WAKE_MAGICPKT	BIT(4) /* Wake on magic packet */
#define   AN8801_IRQ_WAKE_ALL		GENMASK(4, 0)

/* MDIO_MMD_VEND1 Registers */
#define AN8801_PHY_TX_PAIR_DLY_SEL_GBE	0x13
#define   AN8801_PHY_PAIR_DLY_SEL_A_GBE	GENMASK(14, 12)
#define   AN8801_PHY_PAIR_DLY_SEL_B_GBE	GENMASK(10, 8)
#define   AN8801_PHY_PAIR_DLY_SEL_C_GBE	GENMASK(6, 4)
#define   AN8801_PHY_PAIR_DLY_SEL_D_GBE	GENMASK(2, 0)
#define AN8801_PHY_RXADC_CTRL		0xd8
#define   AN8801_PHY_RXADC_SAMP_PHSEL_A	BIT(12)
#define   AN8801_PHY_RXADC_SAMP_PHSEL_B	BIT(8)
#define   AN8801_PHY_RXADC_SAMP_PHSEL_C	BIT(4)
#define   AN8801_PHY_RXADC_SAMP_PHSEL_D	BIT(0)
#define AN8801_PHY_RXADC_REV_0		0xd9
#define   AN8801_PHY_RXADC_REV_MASK_A	GENMASK(15, 8)
#define   AN8801_PHY_RXADC_REV_MASK_B	GENMASK(7, 0)
#define AN8801_PHY_RXADC_REV_1		0xda
#define   AN8801_PHY_RXADC_REV_MASK_C	GENMASK(15, 8)
#define   AN8801_PHY_RXADC_REV_MASK_D	GENMASK(7, 0)

/* MDIO_MMD_VEND2 Registers */
#define LED_BCR				0x21
#define   LED_BCR_MODE_MASK		GENMASK(1, 0)
#define   LED_BCR_TIME_TEST		BIT(2)
#define   LED_BCR_CLK_EN		BIT(3)
#define   LED_BCR_EVT_ALL		BIT(4)
#define   LED_BCR_EXT_CTRL		BIT(15)
#define   LED_BCR_MODE_DISABLE		0
#define   LED_BCR_MODE_2LED		1
#define   LED_BCR_MODE_3LED_1		2
#define   LED_BCR_MODE_3LED_2		3

#define LED_ON_DUR			0x22
#define   LED_ON_DUR_MASK		GENMASK(15, 0)

#define LED_BLINK_DUR			0x23
#define   LED_BLINK_DUR_MASK		GENMASK(15, 0)

#define LED_ON_CTRL(i)			(0x24 + ((i) * 2))
#define   LED_ON_EVT_MASK		GENMASK(6, 0)
#define   LED_ON_EVT_LINK_1000M		BIT(0)
#define   LED_ON_EVT_LINK_100M		BIT(1)
#define   LED_ON_EVT_LINK_10M		BIT(2)
#define   LED_ON_EVT_LINK_DN		BIT(3)
#define   LED_ON_EVT_FDX		BIT(4)
#define   LED_ON_EVT_HDX		BIT(5)
#define   LED_ON_EVT_FORCE		BIT(6)
#define   LED_ON_POL			BIT(14)
#define   LED_ON_EN			BIT(15)

#define LED_BLINK_CTRL(i)		(0x25 + ((i) * 2))
#define LED_BLINK_EVT_MASK		GENMASK(9, 0)
#define LED_BLINK_EVT_1000M_TX		BIT(0)
#define LED_BLINK_EVT_1000M_RX		BIT(1)
#define LED_BLINK_EVT_100M_TX		BIT(2)
#define LED_BLINK_EVT_100M_RX		BIT(3)
#define LED_BLINK_EVT_10M_TX		BIT(4)
#define LED_BLINK_EVT_10M_RX		BIT(5)
#define LED_BLINK_EVT_COLLISION		BIT(6)
#define LED_BLINK_EVT_RX_CRC_ERR	BIT(7)
#define LED_BLINK_EVT_RX_IDLE_ERR	BIT(8)
#define LED_BLINK_EVT_FORCE		BIT(9)

#define AN8801R_NUM_LEDS		3
#define AN8801_PERIOD_SHIFT		15
#define AN8801_PERIOD_UNIT		32768 /* (1 << AN8801_PERIOD_SHIFT) */
#define AN8801_MAX_PERIOD_MS		2147

#define AN8801_REG_PHY_INTERNAL0	0x600
#define AN8801_REG_PHY_INTERNAL1	0x601
#define   AN8801_PHY_INTFUNC_MASK	GENMASK(15, 0) /* PHY internal functions */

enum an8801r_led_fn {
	AN8801R_LED_FN_NONE,
	AN8801R_LED_FN_LINK,
	AN8801R_LED_FN_ACTIVITY,
	AN8801R_LED_FN_MAX,
};

struct an8801r_priv {
	bool wake_magic_enabled;
	bool wake_lnkchg_enabled;
};

static const unsigned long an8801r_led_trig = BIT(TRIGGER_NETDEV_LINK) |
					      BIT(TRIGGER_NETDEV_LINK_10) |
					      BIT(TRIGGER_NETDEV_LINK_100) |
					      BIT(TRIGGER_NETDEV_LINK_1000) |
					      BIT(TRIGGER_NETDEV_RX) |
					      BIT(TRIGGER_NETDEV_RX_ERR) |
					      BIT(TRIGGER_NETDEV_TX);

static int an8801_buckpbus_reg_rmw(struct phy_device *phydev,
				   u32 addr, u32 mask, u32 set)
{
	return air_phy_buckpbus_reg_modify(phydev,
					   addr | AN8801_PBUS_ACCESS,
					   mask, set);
}

static int an8801_buckpbus_reg_set_bits(struct phy_device *phydev,
					u32 addr, u32 mask)
{
	return air_phy_buckpbus_reg_modify(phydev,
					   addr | AN8801_PBUS_ACCESS,
					   mask, mask);
}

static int an8801_buckpbus_reg_clear_bits(struct phy_device *phydev,
					  u32 addr, u32 mask)
{
	return air_phy_buckpbus_reg_modify(phydev,
					   addr | AN8801_PBUS_ACCESS,
					   mask, 0);
}

static int an8801_buckpbus_reg_write(struct phy_device *phydev, u32 addr,
				     u32 data)
{
	return air_phy_buckpbus_reg_write(phydev,
					  addr | AN8801_PBUS_ACCESS,
					  data);
}

static int an8801_buckpbus_reg_read(struct phy_device *phydev, u32 addr,
				    u32 *data)
{
	return air_phy_buckpbus_reg_read(phydev,
					 addr | AN8801_PBUS_ACCESS,
					 data);
}

static u32 an8801r_led_blink_ms_to_hw(unsigned long req_ms)
{
	u32 req_ns, regval;

	if (req_ms > AN8801_MAX_PERIOD_MS)
		req_ms = AN8801_MAX_PERIOD_MS;

	req_ns = req_ms * NSEC_PER_MSEC;

	/* Round to the nearest period unit... */
	regval = req_ns + (AN8801_PERIOD_UNIT / 2);

	/* ...and now divide by the full period */
	regval >>= AN8801_PERIOD_SHIFT;

	return regval;
}

static int an8801r_led_blink_set(struct phy_device *phydev, u8 index,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
	u32 hw_delay_on, hw_delay_off;
	u16 blink_dur;
	bool blink;
	int ret;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	if (delay_on && delay_off) {
		blink = true;

		if (*delay_on == 0 || *delay_off == 0) {
			*delay_on = 64;
			*delay_off = 64;
		}

		hw_delay_on = an8801r_led_blink_ms_to_hw(*delay_on);
		hw_delay_off = an8801r_led_blink_ms_to_hw(*delay_off);
	} else {
		blink = false;
	}

	if (blink) {
		blink_dur = (u16)min(hw_delay_on + hw_delay_off,
				     LED_BLINK_DUR_MASK);
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LED_BLINK_DUR,
				    blink_dur);
		if (ret)
			goto error;

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LED_ON_DUR,
				    hw_delay_on);
		if (ret)
			goto error;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_BLINK_CTRL(index),
			     LED_BLINK_EVT_MASK,
			     blink ? LED_BLINK_EVT_FORCE : 0);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			     LED_ON_EVT_MASK,
			     0);
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			      LED_ON_EN, blink ? LED_ON_EN : 0);

error:
	phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
		       LED_ON_EN, 0);
	return ret;
}

static int an8801r_led_brightness_set(struct phy_device *phydev, u8 index,
				      enum led_brightness value)
{
	int ret;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	/* Disable blink first if previously enabled */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_BLINK_CTRL(index),
			     LED_BLINK_EVT_MASK, 0);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			     LED_ON_EVT_MASK,
			     (value == LED_OFF) ? 0 : LED_ON_EVT_FORCE);
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			      LED_ON_EN, (value == LED_OFF) ? 0 : LED_ON_EN);
}

static int an8801r_led_hw_control_get(struct phy_device *phydev, u8 index,
				      unsigned long *rules)
{
	int on, blink;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	on = phy_read_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index));
	if (on < 0)
		return on;

	blink = phy_read_mmd(phydev, MDIO_MMD_VEND2, LED_BLINK_CTRL(index));
	if (blink < 0)
		return blink;

	if (FIELD_GET(LED_ON_EVT_LINK_10M, on))
		__set_bit(TRIGGER_NETDEV_LINK_10, rules);

	if (FIELD_GET(LED_ON_EVT_LINK_100M, on))
		__set_bit(TRIGGER_NETDEV_LINK_100, rules);

	if (FIELD_GET(LED_ON_EVT_LINK_1000M, on))
		__set_bit(TRIGGER_NETDEV_LINK_1000, rules);

	if (FIELD_GET(LED_ON_EVT_LINK_10M, on) &&
	    FIELD_GET(LED_ON_EVT_LINK_100M, on) &&
	    FIELD_GET(LED_ON_EVT_LINK_1000M, on))
		__set_bit(TRIGGER_NETDEV_LINK, rules);

	if (FIELD_GET(LED_BLINK_EVT_10M_RX, blink) ||
	    FIELD_GET(LED_BLINK_EVT_100M_RX, blink) ||
	    FIELD_GET(LED_BLINK_EVT_1000M_RX, blink))
		__set_bit(TRIGGER_NETDEV_RX, rules);

	if (FIELD_GET(LED_BLINK_EVT_10M_TX, blink) ||
	    FIELD_GET(LED_BLINK_EVT_100M_TX, blink) ||
	    FIELD_GET(LED_BLINK_EVT_1000M_TX, blink))
		__set_bit(TRIGGER_NETDEV_TX, rules);

	if (FIELD_GET(LED_BLINK_EVT_RX_CRC_ERR, blink))
		__set_bit(TRIGGER_NETDEV_RX_ERR, rules);

	return 0;
}

static int an8801r_led_trig_to_hw(unsigned long rules, u16 *on, u16 *blink)
{
	/* All combinations of the supported triggers are allowed */
	if (rules & ~an8801r_led_trig)
		return -EOPNOTSUPP;

	if (test_bit(TRIGGER_NETDEV_LINK_10, &rules))
		*on |= LED_ON_EVT_LINK_10M;

	if (test_bit(TRIGGER_NETDEV_LINK_100, &rules))
		*on |= LED_ON_EVT_LINK_100M;

	if (test_bit(TRIGGER_NETDEV_LINK_1000, &rules))
		*on |= LED_ON_EVT_LINK_1000M;

	if (test_bit(TRIGGER_NETDEV_LINK, &rules)) {
		*on |= LED_ON_EVT_LINK_10M;
		*on |= LED_ON_EVT_LINK_100M;
		*on |= LED_ON_EVT_LINK_1000M;
	}

	if (test_bit(TRIGGER_NETDEV_RX, &rules)) {
		*blink |= LED_BLINK_EVT_10M_RX;
		*blink |= LED_BLINK_EVT_100M_RX;
		*blink |= LED_BLINK_EVT_1000M_RX;
	}

	if (test_bit(TRIGGER_NETDEV_TX, &rules)) {
		*blink |= LED_BLINK_EVT_10M_TX;
		*blink |= LED_BLINK_EVT_100M_TX;
		*blink |= LED_BLINK_EVT_1000M_TX;
	}

	if (test_bit(TRIGGER_NETDEV_RX_ERR, &rules))
		*blink |= LED_BLINK_EVT_RX_CRC_ERR;

	return 0;
}

static int an8801r_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	u16 on = 0, blink = 0;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	return an8801r_led_trig_to_hw(rules, &on, &blink);
}

static int an8801r_led_hw_control_set(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	u16 on = 0, blink = 0;
	int ret;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	ret = an8801r_led_trig_to_hw(rules, &on, &blink);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			     LED_ON_EVT_MASK, on);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_BLINK_CTRL(index),
			     LED_BLINK_EVT_MASK, blink);
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			      LED_ON_EN, (on | blink) ? LED_ON_EN : 0);
}

static int an8801r_led_polarity_set(struct phy_device *phydev, int index,
				    unsigned long modes)
{
	bool active_high = true;
	unsigned long mode;

	if (index >= AN8801R_NUM_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_HIGH:
			break;
		case PHY_LED_ACTIVE_LOW:
			active_high = false;
			break;
		default:
			return -EINVAL;
		}
	}

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, LED_ON_CTRL(index),
			      LED_ON_POL, active_high ? LED_ON_POL : 0);
}

static int an8801r_led_init(struct phy_device *phydev, u8 *led_cfg)
{
	int led_id, ret;

	/* Set LED BCR Enable */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, LED_BCR,
			       LED_BCR_EXT_CTRL | LED_BCR_CLK_EN);
	if (ret)
		return ret;

	for (led_id = 0; led_id < AN8801R_NUM_LEDS; led_id++) {
		unsigned long led_trigger = 0;
		u32 led_gpio = led_id + 1;

		switch (led_cfg[led_id]) {
		case AN8801R_LED_FN_LINK:
			led_trigger = BIT(TRIGGER_NETDEV_LINK);
			break;
		case AN8801R_LED_FN_ACTIVITY:
			led_trigger = BIT(TRIGGER_NETDEV_RX) |
				    BIT(TRIGGER_NETDEV_TX);
			break;
		default:
			led_trigger = 0;
			break;
		}

		ret = an8801_buckpbus_reg_set_bits(phydev,
						   AN8801_BPBUS_REG_LED_GPIO,
						   BIT(led_gpio));
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_rmw(phydev,
					      AN8801_BPBUS_REG_LED_ID_SEL,
					      LED_ID_GPIO_SEL_MASK(led_gpio),
					      LED_ID_GPIO_SEL(led_id,
							      led_gpio));
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_clear_bits(phydev,
						     AN8801_BPBUS_REG_GPIO_MODE,
						     BIT(led_gpio));
		if (ret)
			return ret;

		if (!led_trigger)
			continue;

		ret = an8801r_led_hw_control_set(phydev, led_id, led_trigger);
		if (ret)
			return ret;
	}

	return ret;
}

static int an8801r_reset_wake(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev->priv;
	u32 reg_val = 0;
	int ret;

	/* Enable wakeup clear and disable wake up output */
	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_WAKEUP_CTL2,
					AN8801_PME_WAKEUP_CLR |
					AN8801_WAKE_OUT_POLARITY_NEG);
	if (ret)
		return ret;

	/* Clear WAKEUP_CTL1 register before enabling the wakeup events
	 * again
	 */
	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_WAKEUP_CTL1,
					0);
	if (ret)
		return ret;

	if (priv->wake_magic_enabled)
		reg_val |= AN8801_WOL_WAKE_MAGIC_EN;

	if (priv->wake_lnkchg_enabled)
		reg_val |= AN8801_WOL_WAKE_LNKCHG_EN;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_WAKEUP_CTL1,
					reg_val);
	if (ret)
		return ret;

	/* Disable wake up clear and re-enable wake up output */
	return an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_WAKEUP_CTL2,
					 AN8801_WAKE_OUT_POLARITY_NEG |
					 AN8801_WAKE_OUT_EN);
}

static int an8801r_ack_interrupt(struct phy_device *phydev)
{
	int ret;

	/* Reset wake status */
	ret = an8801r_reset_wake(phydev);
	if (ret)
		return ret;

	/* Clear the interrupts by writing the reg */
	return an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_WAKE_IRQ_STS,
					 AN8801_IRQ_WAKE_ALL);
}

static int an8801r_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		u32 val = FIELD_PREP(AN8801_PHY_IRQ_GPIO_NUM_MASK,
				     AN8801_PHY_IRQ_GPIO_NUM);

		ret = an8801_buckpbus_reg_write(phydev,
						AN8801_BPBUS_REG_PHY_IRQ_GPIO,
						val);
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_set_bits(phydev,
						   AN8801_BPBUS_REG_WAKE_IRQ_EN,
						   AN8801_IRQ_WAKE_LNKCHG);
		if (ret)
			return ret;

	} else {
		ret = an8801_buckpbus_reg_write(phydev,
						AN8801_BPBUS_REG_PHY_IRQ_GPIO,
						0);
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_clear_bits(phydev,
						     AN8801_BPBUS_REG_WAKE_IRQ_EN,
						     AN8801_IRQ_WAKE_LNKCHG);
		if (ret)
			return ret;
	}

	return an8801r_ack_interrupt(phydev);
}

static irqreturn_t an8801r_handle_interrupt(struct phy_device *phydev)
{
	u32 irq_status = 0;
	bool irq_handled = false;
	int ret;

	ret = an8801_buckpbus_reg_read(phydev, AN8801_BPBUS_REG_WAKE_IRQ_STS,
				       &irq_status);
	if (ret)
		return IRQ_NONE;

	ret = an8801r_ack_interrupt(phydev);
	if (ret)
		return IRQ_NONE;

	if (irq_status & AN8801_IRQ_WAKE_MAGICPKT) {
		pm_wakeup_event(&phydev->mdio.dev, 0);
		irq_handled = true;
	}

	if (irq_status & AN8801_IRQ_WAKE_LNKCHG) {
		phy_trigger_machine(phydev);
		irq_handled = true;
	}

	return irq_handled ? IRQ_HANDLED : IRQ_NONE;
}

static void an8801r_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	u32 reg_val;
	int ret;

	/* If the PHY is not capable of waking the system, then WoL can not
	 * be supported.
	 */
	if (!device_can_wakeup(&phydev->mdio.dev)) {
		wol->supported = 0;
		return;
	}

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	ret = an8801_buckpbus_reg_read(phydev, AN8801_BPBUS_REG_WAKEUP_CTL1,
				       &reg_val);
	if (ret)
		return;

	if (reg_val & AN8801_WOL_WAKE_MAGIC_EN)
		wol->wolopts |= WAKE_MAGIC;
	else
		wol->wolopts &= ~WAKE_MAGIC;
}

static int an8801r_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct an8801r_priv *priv = phydev->priv;
	struct net_device *attach_dev;
	const u8 *macaddr;
	u32 reg_val;
	int ret;

	if (!device_can_wakeup(&phydev->mdio.dev))
		return -EOPNOTSUPP;

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		attach_dev = phydev->attached_dev;
		if (!attach_dev)
			return -ENODEV;

		macaddr = (const u8 *)attach_dev->dev_addr;
		if (!is_valid_ether_addr(macaddr))
			return -EINVAL;

		/* MAC bits 16..47 */
		reg_val = (macaddr[2] << 24) | (macaddr[3] << 16);
		reg_val |= (macaddr[4] << 8) | (macaddr[5]);

		ret = an8801_buckpbus_reg_write(phydev,
						AN8801_BPBUS_REG_WOL_MAC_16_47,
						reg_val);
		if (ret)
			return ret;

		/* MAC bits 0..15 */
		reg_val = (macaddr[0] << 8) | (macaddr[1]);

		ret = an8801_buckpbus_reg_write(phydev,
						AN8801_BPBUS_REG_WOL_MAC_0_15,
						reg_val);
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_set_bits(phydev,
						   AN8801_BPBUS_REG_WAKEUP_CTL1,
						   AN8801_WOL_WAKE_MAGIC_EN);
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_set_bits(phydev,
						   AN8801_BPBUS_REG_WAKE_IRQ_EN,
						   AN8801_IRQ_WAKE_MAGICPKT);
		if (ret)
			return ret;

	} else {
		ret = an8801_buckpbus_reg_clear_bits(phydev,
						     AN8801_BPBUS_REG_WAKEUP_CTL1,
						     AN8801_WOL_WAKE_MAGIC_EN);
		if (ret)
			return ret;

		ret = an8801_buckpbus_reg_clear_bits(phydev,
						     AN8801_BPBUS_REG_WAKE_IRQ_EN,
						     AN8801_IRQ_WAKE_MAGICPKT);
		if (ret)
			return ret;
	}

	priv->wake_magic_enabled = !!(wol->wolopts & WAKE_MAGIC);

	return device_set_wakeup_enable(&phydev->mdio.dev,
					priv->wake_magic_enabled);
}

static int an8801r_of_init_leds(struct phy_device *phydev, u8 *led_cfg)
{
	struct device *dev = &phydev->mdio.dev;
	struct device_node *np = dev->of_node;
	struct device_node *leds;
	u32 function_enum_idx;
	int ret = 0;

	if (!np)
		return 0;

	/* If devicetree is present, leds configuration is required */
	leds = of_get_child_by_name(np, "leds");
	if (!leds)
		return 0;

	for_each_available_child_of_node_scoped(leds, led) {
		u32 led_idx;

		ret = of_property_read_u32(led, "reg", &led_idx);
		if (ret)
			goto out;

		if (led_idx >= AN8801R_NUM_LEDS) {
			ret = -EINVAL;
			goto out;
		}

		ret = of_property_read_u32(led, "function-enumerator",
					   &function_enum_idx);
		if (ret) {
			function_enum_idx = AN8801R_LED_FN_NONE;
			ret = 0;
		}

		if (function_enum_idx >= AN8801R_LED_FN_MAX) {
			ret = -EINVAL;
			goto out;
		}

		led_cfg[led_idx] = function_enum_idx;
	}
out:
	of_node_put(leds);
	return ret;
}

static int an8801r_rgmii_rxdelay(struct phy_device *phydev, bool enable,
				 u16 delay_steps)
{
	u32 reg_val;

	if (delay_steps > RGMII_DELAY_STEP_MASK)
		return -EINVAL;

	if (enable) {
		/* Set force mode bit to enable RX delay insertion */
		reg_val = delay_steps | RGMII_RXDELAY_FORCE_MODE;

		/* Set align bit to add extra offset for RX delay */
		reg_val |= RGMII_RXDELAY_ALIGN;
	} else {
		reg_val = 0;
	}

	return an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_RXDLY_STEP,
					 reg_val);
}

static int an8801r_rgmii_txdelay(struct phy_device *phydev, bool enable,
				 u16 delay_steps)
{
	u32 reg_val;

	if (delay_steps > RGMII_DELAY_STEP_MASK)
		return -EINVAL;

	if (enable) {
		/* Set force mode bit to enable TX delay insertion */
		reg_val = delay_steps | RGMII_TXDELAY_FORCE_MODE;
	} else {
		reg_val = 0;
	}

	return an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_TXDLY_STEP,
					 reg_val);
}

static int an8801r_rgmii_delay_config(struct phy_device *phydev)
{
	bool enable_delay;
	u16 delay_step;
	int ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		enable_delay = true;
		delay_step = AN8801_RGMII_TXDELAY_DEFAULT;
	} else {
		enable_delay = false;
		delay_step = RGMII_DELAY_NO_STEP;
	}

	ret = an8801r_rgmii_txdelay(phydev, enable_delay, delay_step);
	if (ret)
		return ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		enable_delay = true;
		delay_step = AN8801_RGMII_RXDELAY_DEFAULT;
	} else {
		enable_delay = false;
		delay_step = RGMII_DELAY_NO_STEP;
	}

	return an8801r_rgmii_rxdelay(phydev, enable_delay, delay_step);
}

static int an8801r_config_init(struct phy_device *phydev)
{
	u8 led_default_function[AN8801R_NUM_LEDS] = { 0 };
	int ret;

	ret = an8801r_of_init_leds(phydev, led_default_function);
	if (ret)
		return ret;

	/* Disable Low Power Mode (LPM) */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AN8801_REG_PHY_INTERNAL0,
			    FIELD_PREP(AN8801_PHY_INTFUNC_MASK, 0x1e));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, AN8801_REG_PHY_INTERNAL1,
			    FIELD_PREP(AN8801_PHY_INTFUNC_MASK, 0x2));
	if (ret)
		return ret;

	/* Set the PHY to perform auto-downshift after 3 auto-negotiation
	 * attempts
	 */
	ret = phy_write_paged(phydev, AIR_PHY_PAGE_EXTENDED_1,
			      AN8801_EXT_REG_PHY,
			      FIELD_PREP(AN8801_EXT_PHY_CTRL1, 0x1d) |
			      FIELD_PREP(AN8801_EXT_PHY_DOWNSHIFT_CTL, 1) |
			      AN8801_EXT_PHY_DOWNSHIFT_EN);
	if (ret < 0)
		return ret;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_BYPASS_PTP,
					AN8801_BYP_PTP_RGMII_TO_GPHY);
	if (ret)
		return ret;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_EFIFO_CTL(0),
					AN8801_EFIFO_RX_EN |
					AN8801_EFIFO_TX_EN |
					AN8801_EFIFO_RX_CLK_EN |
					AN8801_EFIFO_TX_CLK_EN |
					AN8801_EFIFO_RX_EEE_EN |
					AN8801_EFIFO_TX_EEE_EN);
	if (ret)
		return ret;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_EFIFO_CTL(1),
					AN8801_EFIFO_ALL_EN);
	if (ret)
		return ret;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_EFIFO_CTL(2),
					AN8801_EFIFO_ALL_EN);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    AN8801_PHY_TX_PAIR_DLY_SEL_GBE,
			    FIELD_PREP(AN8801_PHY_PAIR_DLY_SEL_A_GBE, 4) |
			    FIELD_PREP(AN8801_PHY_PAIR_DLY_SEL_C_GBE, 4));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AN8801_PHY_RXADC_CTRL,
			    AN8801_PHY_RXADC_SAMP_PHSEL_A |
			    AN8801_PHY_RXADC_SAMP_PHSEL_C);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AN8801_PHY_RXADC_REV_0,
			    FIELD_PREP(AN8801_PHY_RXADC_REV_MASK_A, 1));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, AN8801_PHY_RXADC_REV_1,
			    FIELD_PREP(AN8801_PHY_RXADC_REV_MASK_C, 1));
	if (ret)
		return ret;

	ret = an8801r_rgmii_delay_config(phydev);
	if (ret)
		return ret;

	ret = an8801_buckpbus_reg_write(phydev, AN8801_BPBUS_REG_CKO,
					AN8801_CKO_OUTPUT_MODE_AUTO);
	if (ret)
		return ret;

	ret = an8801r_led_init(phydev, led_default_function);
	if (ret) {
		phydev_err(phydev, "Cannot initialize LEDs: %d\n", ret);
		return ret;
	}

	return 0;
}

static int an8801r_read_status(struct phy_device *phydev)
{
	int prev_speed, ret;
	u32 val;

	prev_speed = phydev->speed;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	if (!phydev->link) {
		phydev->speed = SPEED_UNKNOWN;
		return 0;
	}

	if (prev_speed != phydev->speed) {
		/* Ensure that PHY switches to 1G speed when available,
		 * by configuring the function mode for either 1G or 100M/10M
		 * operation.
		 * Therefore, set the link mode register, after read_status
		 * determines the link speed.
		 */
		val = phydev->speed == SPEED_1000 ?
		      AN8801_BPBUS_LINK_MODE_1000 : 0;

		return an8801_buckpbus_reg_rmw(phydev,
					       AN8801_BPBUS_REG_LINK_MODE,
					       AN8801_BPBUS_LINK_MODE_1000,
					       val);
	}

	return 0;
}

static int an8801r_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct an8801r_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wake_lnkchg_enabled = true;

	phydev->priv = priv;

	/* Mark this PHY as wakeup capable and register the interrupt as a
	 * wakeup IRQ if the PHY is marked as a wakeup source in devicetree,
	 * and the interrupt is valid.
	 */
	if (of_property_read_bool(dev->of_node, "wakeup-source") &&
	    phy_interrupt_is_valid(phydev)) {
		device_set_wakeup_capable(dev, true);
		devm_pm_set_wake_irq(dev, phydev->irq);
	}

	return 0;
}

static int an8801r_suspend(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev->priv;
	int ret;

	/* If the PHY may wake up by a Wake-on-LAN (WOL) event, disable the link
	 * interrupt to only keep the WOL magic interrupt enabled
	 */
	if (device_may_wakeup(&phydev->mdio.dev)) {
		priv->wake_lnkchg_enabled = false;

		if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
			ret = an8801_buckpbus_reg_clear_bits(phydev,
							     AN8801_BPBUS_REG_WAKE_IRQ_EN,
							     AN8801_IRQ_WAKE_LNKCHG);
			if (ret)
				return ret;
		}

		/* Reset WOL status */
		ret = an8801r_reset_wake(phydev);
		if (ret)
			return ret;
	}

	if (!phydev->wol_enabled)
		return genphy_suspend(phydev);

	return 0;
}

static int an8801r_resume(struct phy_device *phydev)
{
	struct an8801r_priv *priv = phydev->priv;
	int ret;

	ret = genphy_resume(phydev);
	if (ret)
		return ret;

	/* Restore the interrupt enable so phylib can receive link
	 * state interrupts.
	 */
	if (device_may_wakeup(&phydev->mdio.dev)) {
		priv->wake_lnkchg_enabled = true;

		ret = an8801_buckpbus_reg_set_bits(phydev,
						   AN8801_BPBUS_REG_WAKEUP_CTL1,
						   AN8801_WOL_WAKE_LNKCHG_EN);
		if (ret)
			return ret;

		if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
			ret = an8801_buckpbus_reg_set_bits(phydev,
							   AN8801_BPBUS_REG_WAKE_IRQ_EN,
							   AN8801_IRQ_WAKE_LNKCHG);
		}
	}

	return ret;
}

static struct phy_driver airoha_driver[] = {
{
	PHY_ID_MATCH_MODEL(AN8801R_PHY_ID),
	.name			= "Airoha AN8801R",
	.probe			= an8801r_probe,
	.config_init		= an8801r_config_init,
	.suspend		= an8801r_suspend,
	.resume			= an8801r_resume,
	.config_aneg		= genphy_config_aneg,
	.read_status		= an8801r_read_status,
	.config_intr		= an8801r_config_intr,
	.handle_interrupt	= an8801r_handle_interrupt,
	.set_wol		= an8801r_set_wol,
	.get_wol		= an8801r_get_wol,
	.read_page		= air_phy_read_page,
	.write_page		= air_phy_write_page,
	.flags			= PHY_ALWAYS_CALL_SUSPEND,
	.led_brightness_set	= an8801r_led_brightness_set,
	.led_blink_set		= an8801r_led_blink_set,
	.led_hw_is_supported	= an8801r_led_hw_is_supported,
	.led_hw_control_set	= an8801r_led_hw_control_set,
	.led_hw_control_get	= an8801r_led_hw_control_get,
	.led_polarity_set	= an8801r_led_polarity_set,
} };
module_phy_driver(airoha_driver);

static struct mdio_device_id __maybe_unused an8801_tbl[] = {
	{ PHY_ID_MATCH_MODEL(AN8801R_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, an8801_tbl);

MODULE_DESCRIPTION("Airoha AN8801 PHY driver");
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_LICENSE("GPL");
