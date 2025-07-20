// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/net/phy/qsemi.c
 *
 * Driver for Quality Semiconductor PHYs
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>

/* ------------------------------------------------------------------------- */
/* The Quality Semiconductor QS6612 is used on the RPX CLLF                  */

/* register definitions */

#define MII_QS6612_MCR		17  /* Mode Control Register      */
#define MII_QS6612_FTR		27  /* Factory Test Register      */
#define MII_QS6612_MCO		28  /* Misc. Control Register     */
#define MII_QS6612_ISR		29  /* Interrupt Source Register  */
#define MII_QS6612_IMR		30  /* Interrupt Mask Register    */
#define MII_QS6612_IMR_INIT	0x003a
#define MII_QS6612_PCR		31  /* 100BaseTx PHY Control Reg. */

#define QS6612_PCR_AN_COMPLETE	0x1000
#define QS6612_PCR_RLBEN	0x0200
#define QS6612_PCR_DCREN	0x0100
#define QS6612_PCR_4B5BEN	0x0040
#define QS6612_PCR_TX_ISOLATE	0x0020
#define QS6612_PCR_MLT3_DIS	0x0002
#define QS6612_PCR_SCRM_DESCRM	0x0001

MODULE_DESCRIPTION("Quality Semiconductor PHY driver");
MODULE_AUTHOR("Andy Fleming");
MODULE_LICENSE("GPL");

/* Returns 0, unless there's a write error */
static int qs6612_config_init(struct phy_device *phydev)
{
	/* The PHY powers up isolated on the RPX,
	 * so send a command to allow operation.
	 * XXX - My docs indicate this should be 0x0940
	 * ...or something.  The current value sets three
	 * reserved bits, bit 11, which specifies it should be
	 * set to one, bit 10, which specifies it should be set
	 * to 0, and bit 7, which doesn't specify.  However, my
	 * docs are preliminary, and I will leave it like this
	 * until someone more knowledgable corrects me or it.
	 * -- Andy Fleming
	 */
	return phy_write(phydev, MII_QS6612_PCR, 0x0dc0);
}

static int qs6612_ack_interrupt(struct phy_device *phydev)
{
	int err;

	/* The Interrupt Source register is not self-clearing, bits 4 and 5 are
	 * cleared when MII_BMSR is read and bits 1 and 3 are cleared when
	 * MII_EXPANSION is read
	 */
	err = phy_read(phydev, MII_QS6612_ISR);

	if (err < 0)
		return err;

	err = phy_read(phydev, MII_BMSR);

	if (err < 0)
		return err;

	err = phy_read(phydev, MII_EXPANSION);

	if (err < 0)
		return err;

	return 0;
}

static int qs6612_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* clear any interrupts before enabling them */
		err = qs6612_ack_interrupt(phydev);
		if (err)
			return err;

		err = phy_write(phydev, MII_QS6612_IMR,
				MII_QS6612_IMR_INIT);
	} else {
		err = phy_write(phydev, MII_QS6612_IMR, 0);
		if (err)
			return err;

		/* clear any leftover interrupts */
		err = qs6612_ack_interrupt(phydev);
	}

	return err;

}

static irqreturn_t qs6612_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, MII_QS6612_ISR);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & MII_QS6612_IMR_INIT))
		return IRQ_NONE;

	/* the interrupt source register is not self-clearing */
	qs6612_ack_interrupt(phydev);

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static struct phy_driver qs6612_driver[] = { {
	.phy_id		= 0x00181440,
	.name		= "QS6612",
	.phy_id_mask	= 0xfffffff0,
	/* PHY_BASIC_FEATURES */
	.config_init	= qs6612_config_init,
	.config_intr	= qs6612_config_intr,
	.handle_interrupt = qs6612_handle_interrupt,
} };

module_phy_driver(qs6612_driver);

static const struct mdio_device_id __maybe_unused qs6612_tbl[] = {
	{ 0x00181440, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, qs6612_tbl);
