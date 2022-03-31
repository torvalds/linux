// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson GXL Internal PHY Driver
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2016 BayLibre, SAS. All rights reserved.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>

#define TSTCNTL		20
#define  TSTCNTL_READ		BIT(15)
#define  TSTCNTL_WRITE		BIT(14)
#define  TSTCNTL_REG_BANK_SEL	GENMASK(12, 11)
#define  TSTCNTL_TEST_MODE	BIT(10)
#define  TSTCNTL_READ_ADDRESS	GENMASK(9, 5)
#define  TSTCNTL_WRITE_ADDRESS	GENMASK(4, 0)
#define TSTREAD1	21
#define TSTWRITE	23
#define INTSRC_FLAG	29
#define  INTSRC_ANEG_PR		BIT(1)
#define  INTSRC_PARALLEL_FAULT	BIT(2)
#define  INTSRC_ANEG_LP_ACK	BIT(3)
#define  INTSRC_LINK_DOWN	BIT(4)
#define  INTSRC_REMOTE_FAULT	BIT(5)
#define  INTSRC_ANEG_COMPLETE	BIT(6)
#define  INTSRC_ENERGY_DETECT	BIT(7)
#define INTSRC_MASK	30

#define INT_SOURCES (INTSRC_LINK_DOWN | INTSRC_ANEG_COMPLETE | \
		     INTSRC_ENERGY_DETECT)

#define BANK_ANALOG_DSP		0
#define BANK_WOL		1
#define BANK_BIST		3

/* WOL Registers */
#define LPI_STATUS	0xc
#define  LPI_STATUS_RSV12	BIT(12)

/* BIST Registers */
#define FR_PLL_CONTROL	0x1b
#define FR_PLL_DIV0	0x1c
#define FR_PLL_DIV1	0x1d

static int meson_gxl_open_banks(struct phy_device *phydev)
{
	int ret;

	/* Enable Analog and DSP register Bank access by
	 * toggling TSTCNTL_TEST_MODE bit in the TSTCNTL register
	 */
	ret = phy_write(phydev, TSTCNTL, 0);
	if (ret)
		return ret;
	ret = phy_write(phydev, TSTCNTL, TSTCNTL_TEST_MODE);
	if (ret)
		return ret;
	ret = phy_write(phydev, TSTCNTL, 0);
	if (ret)
		return ret;
	return phy_write(phydev, TSTCNTL, TSTCNTL_TEST_MODE);
}

static void meson_gxl_close_banks(struct phy_device *phydev)
{
	phy_write(phydev, TSTCNTL, 0);
}

static int meson_gxl_read_reg(struct phy_device *phydev,
			      unsigned int bank, unsigned int reg)
{
	int ret;

	ret = meson_gxl_open_banks(phydev);
	if (ret)
		goto out;

	ret = phy_write(phydev, TSTCNTL, TSTCNTL_READ |
			FIELD_PREP(TSTCNTL_REG_BANK_SEL, bank) |
			TSTCNTL_TEST_MODE |
			FIELD_PREP(TSTCNTL_READ_ADDRESS, reg));
	if (ret)
		goto out;

	ret = phy_read(phydev, TSTREAD1);
out:
	/* Close the bank access on our way out */
	meson_gxl_close_banks(phydev);
	return ret;
}

static int meson_gxl_write_reg(struct phy_device *phydev,
			       unsigned int bank, unsigned int reg,
			       uint16_t value)
{
	int ret;

	ret = meson_gxl_open_banks(phydev);
	if (ret)
		goto out;

	ret = phy_write(phydev, TSTWRITE, value);
	if (ret)
		goto out;

	ret = phy_write(phydev, TSTCNTL, TSTCNTL_WRITE |
			FIELD_PREP(TSTCNTL_REG_BANK_SEL, bank) |
			TSTCNTL_TEST_MODE |
			FIELD_PREP(TSTCNTL_WRITE_ADDRESS, reg));

out:
	/* Close the bank access on our way out */
	meson_gxl_close_banks(phydev);
	return ret;
}

static int meson_gxl_config_init(struct phy_device *phydev)
{
	int ret;

	/* Enable fractional PLL */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_CONTROL, 0x5);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV1 */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_DIV1, 0x029a);
	if (ret)
		return ret;

	/* Program fraction FR_PLL_DIV1 */
	ret = meson_gxl_write_reg(phydev, BANK_BIST, FR_PLL_DIV0, 0xaaaa);
	if (ret)
		return ret;

	return 0;
}

/* This function is provided to cope with the possible failures of this phy
 * during aneg process. When aneg fails, the PHY reports that aneg is done
 * but the value found in MII_LPA is wrong:
 *  - Early failures: MII_LPA is just 0x0001. if MII_EXPANSION reports that
 *    the link partner (LP) supports aneg but the LP never acked our base
 *    code word, it is likely that we never sent it to begin with.
 *  - Late failures: MII_LPA is filled with a value which seems to make sense
 *    but it actually is not what the LP is advertising. It seems that we
 *    can detect this using a magic bit in the WOL bank (reg 12 - bit 12).
 *    If this particular bit is not set when aneg is reported being done,
 *    it means MII_LPA is likely to be wrong.
 *
 * In both case, forcing a restart of the aneg process solve the problem.
 * When this failure happens, the first retry is usually successful but,
 * in some cases, it may take up to 6 retries to get a decent result
 */
static int meson_gxl_read_status(struct phy_device *phydev)
{
	int ret, wol, lpa, exp;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_aneg_done(phydev);
		if (ret < 0)
			return ret;
		else if (!ret)
			goto read_status_continue;

		/* Aneg is done, let's check everything is fine */
		wol = meson_gxl_read_reg(phydev, BANK_WOL, LPI_STATUS);
		if (wol < 0)
			return wol;

		lpa = phy_read(phydev, MII_LPA);
		if (lpa < 0)
			return lpa;

		exp = phy_read(phydev, MII_EXPANSION);
		if (exp < 0)
			return exp;

		if (!(wol & LPI_STATUS_RSV12) ||
		    ((exp & EXPANSION_NWAY) && !(lpa & LPA_LPACK))) {
			/* Looks like aneg failed after all */
			phydev_dbg(phydev, "LPA corruption - aneg restart\n");
			return genphy_restart_aneg(phydev);
		}
	}

read_status_continue:
	return genphy_read_status(phydev);
}

static int meson_gxl_ack_interrupt(struct phy_device *phydev)
{
	int ret = phy_read(phydev, INTSRC_FLAG);

	return ret < 0 ? ret : 0;
}

static int meson_gxl_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* Ack any pending IRQ */
		ret = meson_gxl_ack_interrupt(phydev);
		if (ret)
			return ret;

		ret = phy_write(phydev, INTSRC_MASK, INT_SOURCES);
	} else {
		ret = phy_write(phydev, INTSRC_MASK, 0);

		/* Ack any pending IRQ */
		ret = meson_gxl_ack_interrupt(phydev);
	}

	return ret;
}

static irqreturn_t meson_gxl_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, INTSRC_FLAG);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	irq_status &= INT_SOURCES;

	if (irq_status == 0)
		return IRQ_NONE;

	/* Aneg-complete interrupt is used for link-up detection */
	if (phydev->autoneg == AUTONEG_ENABLE &&
	    irq_status == INTSRC_ENERGY_DETECT)
		return IRQ_HANDLED;

	/* Give PHY some time before MAC starts sending data. This works
	 * around an issue where network doesn't come up properly.
	 */
	if (!(irq_status & INTSRC_LINK_DOWN))
		phy_queue_state_machine(phydev, msecs_to_jiffies(100));
	else
		phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static struct phy_driver meson_gxl_phy[] = {
	{
		PHY_ID_MATCH_EXACT(0x01814400),
		.name		= "Meson GXL Internal PHY",
		/* PHY_BASIC_FEATURES */
		.flags		= PHY_IS_INTERNAL,
		.soft_reset     = genphy_soft_reset,
		.config_init	= meson_gxl_config_init,
		.read_status	= meson_gxl_read_status,
		.config_intr	= meson_gxl_config_intr,
		.handle_interrupt = meson_gxl_handle_interrupt,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
	}, {
		PHY_ID_MATCH_EXACT(0x01803301),
		.name		= "Meson G12A Internal PHY",
		/* PHY_BASIC_FEATURES */
		.flags		= PHY_IS_INTERNAL,
		.soft_reset     = genphy_soft_reset,
		.config_intr	= meson_gxl_config_intr,
		.handle_interrupt = meson_gxl_handle_interrupt,
		.suspend        = genphy_suspend,
		.resume         = genphy_resume,
	},
};

static struct mdio_device_id __maybe_unused meson_gxl_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(0x01814400) },
	{ PHY_ID_MATCH_VENDOR(0x01803301) },
	{ }
};

module_phy_driver(meson_gxl_phy);

MODULE_DEVICE_TABLE(mdio, meson_gxl_tbl);

MODULE_DESCRIPTION("Amlogic Meson GXL Internal PHY driver");
MODULE_AUTHOR("Baoqi wang");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
