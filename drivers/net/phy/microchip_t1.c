// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Microchip Technology

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/phy.h>

/* External Register Control Register */
#define LAN87XX_EXT_REG_CTL                     (0x14)
#define LAN87XX_EXT_REG_CTL_RD_CTL              (0x1000)
#define LAN87XX_EXT_REG_CTL_WR_CTL              (0x0800)

/* External Register Read Data Register */
#define LAN87XX_EXT_REG_RD_DATA                 (0x15)

/* External Register Write Data Register */
#define LAN87XX_EXT_REG_WR_DATA                 (0x16)

/* Interrupt Source Register */
#define LAN87XX_INTERRUPT_SOURCE                (0x18)

/* Interrupt Mask Register */
#define LAN87XX_INTERRUPT_MASK                  (0x19)
#define LAN87XX_MASK_LINK_UP                    (0x0004)
#define LAN87XX_MASK_LINK_DOWN                  (0x0002)

/* phyaccess nested types */
#define	PHYACC_ATTR_MODE_READ		0
#define	PHYACC_ATTR_MODE_WRITE		1
#define	PHYACC_ATTR_MODE_MODIFY		2

#define	PHYACC_ATTR_BANK_SMI		0
#define	PHYACC_ATTR_BANK_MISC		1
#define	PHYACC_ATTR_BANK_PCS		2
#define	PHYACC_ATTR_BANK_AFE		3
#define	PHYACC_ATTR_BANK_MAX		7

#define DRIVER_AUTHOR	"Nisar Sayed <nisar.sayed@microchip.com>"
#define DRIVER_DESC	"Microchip LAN87XX T1 PHY driver"

struct access_ereg_val {
	u8  mode;
	u8  bank;
	u8  offset;
	u16 val;
	u16 mask;
};

static int access_ereg(struct phy_device *phydev, u8 mode, u8 bank,
		       u8 offset, u16 val)
{
	u16 ereg = 0;
	int rc = 0;

	if (mode > PHYACC_ATTR_MODE_WRITE || bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	if (bank == PHYACC_ATTR_BANK_SMI) {
		if (mode == PHYACC_ATTR_MODE_WRITE)
			rc = phy_write(phydev, offset, val);
		else
			rc = phy_read(phydev, offset);
		return rc;
	}

	if (mode == PHYACC_ATTR_MODE_WRITE) {
		ereg = LAN87XX_EXT_REG_CTL_WR_CTL;
		rc = phy_write(phydev, LAN87XX_EXT_REG_WR_DATA, val);
		if (rc < 0)
			return rc;
	} else {
		ereg = LAN87XX_EXT_REG_CTL_RD_CTL;
	}

	ereg |= (bank << 8) | offset;

	rc = phy_write(phydev, LAN87XX_EXT_REG_CTL, ereg);
	if (rc < 0)
		return rc;

	if (mode == PHYACC_ATTR_MODE_READ)
		rc = phy_read(phydev, LAN87XX_EXT_REG_RD_DATA);

	return rc;
}

static int access_ereg_modify_changed(struct phy_device *phydev,
				      u8 bank, u8 offset, u16 val, u16 mask)
{
	int new = 0, rc = 0;

	if (bank > PHYACC_ATTR_BANK_MAX)
		return -EINVAL;

	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ, bank, offset, val);
	if (rc < 0)
		return rc;

	new = val | (rc & (mask ^ 0xFFFF));
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_WRITE, bank, offset, new);

	return rc;
}

static int lan87xx_phy_init(struct phy_device *phydev)
{
	static const struct access_ereg_val init[] = {
		/* TX Amplitude = 5 */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_AFE, 0x0B,
		 0x000A, 0x001E},
		/* Clear SMI interrupts */
		{PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_SMI, 0x18,
		 0, 0},
		/* Clear MISC interrupts */
		{PHYACC_ATTR_MODE_READ, PHYACC_ATTR_BANK_MISC, 0x08,
		 0, 0},
		/* Turn on TC10 Ring Oscillator (ROSC) */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_MISC, 0x20,
		 0x0020, 0x0020},
		/* WUR Detect Length to 1.2uS, LPC Detect Length to 1.09uS */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_PCS, 0x20,
		 0x283C, 0},
		/* Wake_In Debounce Length to 39uS, Wake_Out Length to 79uS */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x21,
		 0x274F, 0},
		/* Enable Auto Wake Forward to Wake_Out, ROSC on, Sleep,
		 * and Wake_In to wake PHY
		 */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x20,
		 0x80A7, 0},
		/* Enable WUP Auto Fwd, Enable Wake on MDI, Wakeup Debouncer
		 * to 128 uS
		 */
		{PHYACC_ATTR_MODE_WRITE, PHYACC_ATTR_BANK_MISC, 0x24,
		 0xF110, 0},
		/* Enable HW Init */
		{PHYACC_ATTR_MODE_MODIFY, PHYACC_ATTR_BANK_SMI, 0x1A,
		 0x0100, 0x0100},
	};
	int rc, i;

	/* Start manual initialization procedures in Managed Mode */
	rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
					0x1a, 0x0000, 0x0100);
	if (rc < 0)
		return rc;

	/* Soft Reset the SMI block */
	rc = access_ereg_modify_changed(phydev, PHYACC_ATTR_BANK_SMI,
					0x00, 0x8000, 0x8000);
	if (rc < 0)
		return rc;

	/* Check to see if the self-clearing bit is cleared */
	usleep_range(1000, 2000);
	rc = access_ereg(phydev, PHYACC_ATTR_MODE_READ,
			 PHYACC_ATTR_BANK_SMI, 0x00, 0);
	if (rc < 0)
		return rc;
	if ((rc & 0x8000) != 0)
		return -ETIMEDOUT;

	/* PHY Initialization */
	for (i = 0; i < ARRAY_SIZE(init); i++) {
		if (init[i].mode == PHYACC_ATTR_MODE_MODIFY) {
			rc = access_ereg_modify_changed(phydev, init[i].bank,
							init[i].offset,
							init[i].val,
							init[i].mask);
		} else {
			rc = access_ereg(phydev, init[i].mode, init[i].bank,
					 init[i].offset, init[i].val);
		}
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int lan87xx_phy_config_intr(struct phy_device *phydev)
{
	int rc, val = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* unmask all source and clear them before enable */
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, 0x7FFF);
		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
		val = LAN87XX_MASK_LINK_UP | LAN87XX_MASK_LINK_DOWN;
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
	} else {
		rc = phy_write(phydev, LAN87XX_INTERRUPT_MASK, val);
		if (rc)
			return rc;

		rc = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
	}

	return rc < 0 ? rc : 0;
}

static irqreturn_t lan87xx_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, LAN87XX_INTERRUPT_SOURCE);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (irq_status == 0)
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int lan87xx_config_init(struct phy_device *phydev)
{
	int rc = lan87xx_phy_init(phydev);

	return rc < 0 ? rc : 0;
}

static struct phy_driver microchip_t1_phy_driver[] = {
	{
		.phy_id         = 0x0007c150,
		.phy_id_mask    = 0xfffffff0,
		.name           = "Microchip LAN87xx T1",

		.features       = PHY_BASIC_T1_FEATURES,

		.config_init	= lan87xx_config_init,

		.config_intr    = lan87xx_phy_config_intr,
		.handle_interrupt = lan87xx_handle_interrupt,

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
