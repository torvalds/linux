// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Microchip Technology

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

/* Interrupt Source Register */
#define LAN87XX_INTERRUPT_SOURCE                (0x18)

/* Interrupt Mask Register */
#define LAN87XX_INTERRUPT_MASK                  (0x19)
#define LAN87XX_MASK_LINK_UP                    (0x0004)
#define LAN87XX_MASK_LINK_DOWN                  (0x0002)

#define DRIVER_AUTHOR	"Nisar Sayed <nisar.sayed@microchip.com>"
#define DRIVER_DESC	"Microchip LAN87XX T1 PHY driver"

static int lan87xx_phy_config_intr(struct phy_device *phydev)
{
	int rc, val = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* unmask all source and clear them before enable */
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, 0x7FFF);
		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
		val = LAN87XX_MASK_LINK_UP | LAN87XX_MASK_LINK_DOWN;
	}

	rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);

	return rc < 0 ? rc : 0;
}

static int lan87xx_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);

	return rc < 0 ? rc : 0;
}

static struct phy_driver microchip_t1_phy_driver[] = {
	{
		.phy_id         = 0x0007c150,
		.phy_id_mask    = 0xfffffff0,
		.name           = "Microchip LAN87xx T1",

		.features       = PHY_BASIC_T1_FEATURES,

		.config_init    = genphy_config_init,
		.config_aneg    = genphy_config_aneg,

		.ack_interrupt  = lan87xx_phy_ack_interrupt,
		.config_intr    = lan87xx_phy_config_intr,

		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
	}
};

module_phy_driver(microchip_t1_phy_driver);

static struct mdio_device_id __maybe_unused microchip_t1_tbl[] = {
	{ 0x0007c150, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_t1_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
