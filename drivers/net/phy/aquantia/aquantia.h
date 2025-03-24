/* SPDX-License-Identifier: GPL-2.0 */
/* HWMON driver for Aquantia PHY
 *
 * Author: Nikita Yushchenko <nikita.yoush@cogentembedded.com>
 * Author: Andrew Lunn <andrew@lunn.ch>
 * Author: Heiner Kallweit <hkallweit1@gmail.com>
 */

#ifndef AQUANTIA_H
#define AQUANTIA_H

#include <linux/device.h>
#include <linux/phy.h>

/* Vendor specific 1, MDIO_MMD_VEND1 */
#define VEND1_GLOBAL_SC				0x0
#define VEND1_GLOBAL_SC_SOFT_RESET		BIT(15)
#define VEND1_GLOBAL_SC_LOW_POWER		BIT(11)

#define VEND1_GLOBAL_FW_ID			0x0020
#define VEND1_GLOBAL_FW_ID_MAJOR		GENMASK(15, 8)
#define VEND1_GLOBAL_FW_ID_MINOR		GENMASK(7, 0)

#define VEND1_GLOBAL_MAILBOX_INTERFACE1			0x0200
#define VEND1_GLOBAL_MAILBOX_INTERFACE1_EXECUTE		BIT(15)
#define VEND1_GLOBAL_MAILBOX_INTERFACE1_WRITE		BIT(14)
#define VEND1_GLOBAL_MAILBOX_INTERFACE1_CRC_RESET	BIT(12)
#define VEND1_GLOBAL_MAILBOX_INTERFACE1_BUSY		BIT(8)

#define VEND1_GLOBAL_MAILBOX_INTERFACE2			0x0201
#define VEND1_GLOBAL_MAILBOX_INTERFACE3			0x0202
#define VEND1_GLOBAL_MAILBOX_INTERFACE3_MSW_ADDR_MASK	GENMASK(15, 0)
#define VEND1_GLOBAL_MAILBOX_INTERFACE3_MSW_ADDR(x)	FIELD_PREP(VEND1_GLOBAL_MAILBOX_INTERFACE3_MSW_ADDR_MASK, (u16)((x) >> 16))
#define VEND1_GLOBAL_MAILBOX_INTERFACE4			0x0203
#define VEND1_GLOBAL_MAILBOX_INTERFACE4_LSW_ADDR_MASK	GENMASK(15, 2)
#define VEND1_GLOBAL_MAILBOX_INTERFACE4_LSW_ADDR(x)	FIELD_PREP(VEND1_GLOBAL_MAILBOX_INTERFACE4_LSW_ADDR_MASK, (u16)(x))

#define VEND1_GLOBAL_MAILBOX_INTERFACE5			0x0204
#define VEND1_GLOBAL_MAILBOX_INTERFACE5_MSW_DATA_MASK	GENMASK(15, 0)
#define VEND1_GLOBAL_MAILBOX_INTERFACE5_MSW_DATA(x)	FIELD_PREP(VEND1_GLOBAL_MAILBOX_INTERFACE5_MSW_DATA_MASK, (u16)((x) >> 16))
#define VEND1_GLOBAL_MAILBOX_INTERFACE6			0x0205
#define VEND1_GLOBAL_MAILBOX_INTERFACE6_LSW_DATA_MASK	GENMASK(15, 0)
#define VEND1_GLOBAL_MAILBOX_INTERFACE6_LSW_DATA(x)	FIELD_PREP(VEND1_GLOBAL_MAILBOX_INTERFACE6_LSW_DATA_MASK, (u16)(x))

/* The following registers all have similar layouts; first the registers... */
#define VEND1_GLOBAL_CFG_10M			0x0310
#define VEND1_GLOBAL_CFG_100M			0x031b
#define VEND1_GLOBAL_CFG_1G			0x031c
#define VEND1_GLOBAL_CFG_2_5G			0x031d
#define VEND1_GLOBAL_CFG_5G			0x031e
#define VEND1_GLOBAL_CFG_10G			0x031f
/* ...and now the fields */
#define VEND1_GLOBAL_CFG_SERDES_MODE		GENMASK(2, 0)
#define VEND1_GLOBAL_CFG_SERDES_MODE_XFI	0
#define VEND1_GLOBAL_CFG_SERDES_MODE_SGMII	3
#define VEND1_GLOBAL_CFG_SERDES_MODE_OCSGMII	4
#define VEND1_GLOBAL_CFG_SERDES_MODE_XFI5G	6
#define VEND1_GLOBAL_CFG_RATE_ADAPT		GENMASK(8, 7)
#define VEND1_GLOBAL_CFG_RATE_ADAPT_NONE	0
#define VEND1_GLOBAL_CFG_RATE_ADAPT_USX		1
#define VEND1_GLOBAL_CFG_RATE_ADAPT_PAUSE	2

/* Vendor specific 1, MDIO_MMD_VEND2 */
#define VEND1_GLOBAL_CONTROL2			0xc001
#define VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_RST	BIT(15)
#define VEND1_GLOBAL_CONTROL2_UP_RUN_STALL_OVD	BIT(6)
#define VEND1_GLOBAL_CONTROL2_UP_RUN_STALL	BIT(0)

#define VEND1_GLOBAL_LED_PROV			0xc430
#define AQR_LED_PROV(x)				(VEND1_GLOBAL_LED_PROV + (x))
#define VEND1_GLOBAL_LED_PROV_LINK2500		BIT(14)
#define VEND1_GLOBAL_LED_PROV_LINK5000		BIT(15)
#define VEND1_GLOBAL_LED_PROV_FORCE_ON		BIT(8)
#define VEND1_GLOBAL_LED_PROV_LINK10000		BIT(7)
#define VEND1_GLOBAL_LED_PROV_LINK1000		BIT(6)
#define VEND1_GLOBAL_LED_PROV_LINK100		BIT(5)
#define VEND1_GLOBAL_LED_PROV_RX_ACT		BIT(3)
#define VEND1_GLOBAL_LED_PROV_TX_ACT		BIT(2)
#define VEND1_GLOBAL_LED_PROV_ACT_STRETCH	GENMASK(0, 1)

#define VEND1_GLOBAL_LED_PROV_LINK_MASK		(VEND1_GLOBAL_LED_PROV_LINK100 | \
						 VEND1_GLOBAL_LED_PROV_LINK1000 | \
						 VEND1_GLOBAL_LED_PROV_LINK10000 | \
						 VEND1_GLOBAL_LED_PROV_LINK5000 | \
						 VEND1_GLOBAL_LED_PROV_LINK2500)

#define VEND1_GLOBAL_LED_DRIVE			0xc438
#define VEND1_GLOBAL_LED_DRIVE_VDD		BIT(1)
#define AQR_LED_DRIVE(x)			(VEND1_GLOBAL_LED_DRIVE + (x))

#define VEND1_THERMAL_PROV_HIGH_TEMP_FAIL	0xc421
#define VEND1_THERMAL_PROV_LOW_TEMP_FAIL	0xc422
#define VEND1_THERMAL_PROV_HIGH_TEMP_WARN	0xc423
#define VEND1_THERMAL_PROV_LOW_TEMP_WARN	0xc424
#define VEND1_THERMAL_STAT1			0xc820
#define VEND1_THERMAL_STAT2			0xc821
#define VEND1_THERMAL_STAT2_VALID		BIT(0)
#define VEND1_GENERAL_STAT1			0xc830
#define VEND1_GENERAL_STAT1_HIGH_TEMP_FAIL	BIT(14)
#define VEND1_GENERAL_STAT1_LOW_TEMP_FAIL	BIT(13)
#define VEND1_GENERAL_STAT1_HIGH_TEMP_WARN	BIT(12)
#define VEND1_GENERAL_STAT1_LOW_TEMP_WARN	BIT(11)

#define VEND1_GLOBAL_GEN_STAT2			0xc831
#define VEND1_GLOBAL_GEN_STAT2_OP_IN_PROG	BIT(15)

#define VEND1_GLOBAL_RSVD_STAT1			0xc885
#define VEND1_GLOBAL_RSVD_STAT1_FW_BUILD_ID	GENMASK(7, 4)
#define VEND1_GLOBAL_RSVD_STAT1_PROV_ID		GENMASK(3, 0)

#define VEND1_GLOBAL_RSVD_STAT9			0xc88d
#define VEND1_GLOBAL_RSVD_STAT9_MODE		GENMASK(7, 0)
#define VEND1_GLOBAL_RSVD_STAT9_1000BT2		0x23

/* MDIO_MMD_C22EXT */
#define MDIO_C22EXT_STAT_SGMII_RX_GOOD_FRAMES		0xd292
#define MDIO_C22EXT_STAT_SGMII_RX_BAD_FRAMES		0xd294
#define MDIO_C22EXT_STAT_SGMII_RX_FALSE_CARRIER		0xd297
#define MDIO_C22EXT_STAT_SGMII_TX_GOOD_FRAMES		0xd313
#define MDIO_C22EXT_STAT_SGMII_TX_BAD_FRAMES		0xd315
#define MDIO_C22EXT_STAT_SGMII_TX_FALSE_CARRIER		0xd317
#define MDIO_C22EXT_STAT_SGMII_TX_COLLISIONS		0xd318
#define MDIO_C22EXT_STAT_SGMII_TX_LINE_COLLISIONS	0xd319
#define MDIO_C22EXT_STAT_SGMII_TX_FRAME_ALIGN_ERR	0xd31a
#define MDIO_C22EXT_STAT_SGMII_TX_RUNT_FRAMES		0xd31b

#define VEND1_GLOBAL_INT_STD_STATUS		0xfc00
#define VEND1_GLOBAL_INT_VEND_STATUS		0xfc01

#define VEND1_GLOBAL_INT_STD_MASK		0xff00
#define VEND1_GLOBAL_INT_STD_MASK_PMA1		BIT(15)
#define VEND1_GLOBAL_INT_STD_MASK_PMA2		BIT(14)
#define VEND1_GLOBAL_INT_STD_MASK_PCS1		BIT(13)
#define VEND1_GLOBAL_INT_STD_MASK_PCS2		BIT(12)
#define VEND1_GLOBAL_INT_STD_MASK_PCS3		BIT(11)
#define VEND1_GLOBAL_INT_STD_MASK_PHY_XS1	BIT(10)
#define VEND1_GLOBAL_INT_STD_MASK_PHY_XS2	BIT(9)
#define VEND1_GLOBAL_INT_STD_MASK_AN1		BIT(8)
#define VEND1_GLOBAL_INT_STD_MASK_AN2		BIT(7)
#define VEND1_GLOBAL_INT_STD_MASK_GBE		BIT(6)
#define VEND1_GLOBAL_INT_STD_MASK_ALL		BIT(0)

#define VEND1_GLOBAL_INT_VEND_MASK		0xff01
#define VEND1_GLOBAL_INT_VEND_MASK_PMA		BIT(15)
#define VEND1_GLOBAL_INT_VEND_MASK_PCS		BIT(14)
#define VEND1_GLOBAL_INT_VEND_MASK_PHY_XS	BIT(13)
#define VEND1_GLOBAL_INT_VEND_MASK_AN		BIT(12)
#define VEND1_GLOBAL_INT_VEND_MASK_GBE		BIT(11)
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL1	BIT(2)
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL2	BIT(1)
#define VEND1_GLOBAL_INT_VEND_MASK_GLOBAL3	BIT(0)

#define AQR_MAX_LEDS				3

struct aqr107_hw_stat {
	const char *name;
	int reg;
	int size;
};

#define SGMII_STAT(n, r, s) { n, MDIO_C22EXT_STAT_SGMII_ ## r, s }
static const struct aqr107_hw_stat aqr107_hw_stats[] = {
	SGMII_STAT("sgmii_rx_good_frames",	    RX_GOOD_FRAMES,	26),
	SGMII_STAT("sgmii_rx_bad_frames",	    RX_BAD_FRAMES,	26),
	SGMII_STAT("sgmii_rx_false_carrier_events", RX_FALSE_CARRIER,	 8),
	SGMII_STAT("sgmii_tx_good_frames",	    TX_GOOD_FRAMES,	26),
	SGMII_STAT("sgmii_tx_bad_frames",	    TX_BAD_FRAMES,	26),
	SGMII_STAT("sgmii_tx_false_carrier_events", TX_FALSE_CARRIER,	 8),
	SGMII_STAT("sgmii_tx_collisions",	    TX_COLLISIONS,	 8),
	SGMII_STAT("sgmii_tx_line_collisions",	    TX_LINE_COLLISIONS,	 8),
	SGMII_STAT("sgmii_tx_frame_alignment_err",  TX_FRAME_ALIGN_ERR,	16),
	SGMII_STAT("sgmii_tx_runt_frames",	    TX_RUNT_FRAMES,	22),
};

#define AQR107_SGMII_STAT_SZ ARRAY_SIZE(aqr107_hw_stats)

struct aqr107_priv {
	u64 sgmii_stats[AQR107_SGMII_STAT_SZ];
	unsigned long leds_active_low;
	unsigned long leds_active_high;
};

#if IS_REACHABLE(CONFIG_HWMON)
int aqr_hwmon_probe(struct phy_device *phydev);
#else
static inline int aqr_hwmon_probe(struct phy_device *phydev) { return 0; }
#endif

int aqr_firmware_load(struct phy_device *phydev);

int aqr_phy_led_blink_set(struct phy_device *phydev, u8 index,
			  unsigned long *delay_on,
			  unsigned long *delay_off);
int aqr_phy_led_brightness_set(struct phy_device *phydev,
			       u8 index, enum led_brightness value);
int aqr_phy_led_hw_is_supported(struct phy_device *phydev, u8 index,
				unsigned long rules);
int aqr_phy_led_hw_control_get(struct phy_device *phydev, u8 index,
			       unsigned long *rules);
int aqr_phy_led_hw_control_set(struct phy_device *phydev, u8 index,
			       unsigned long rules);
int aqr_phy_led_active_low_set(struct phy_device *phydev, int index, bool enable);
int aqr_phy_led_polarity_set(struct phy_device *phydev, int index,
			     unsigned long modes);
int aqr_wait_reset_complete(struct phy_device *phydev);

#endif /* AQUANTIA_H */
