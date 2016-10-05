/*
 * Driver for (BCM4706)? GBit MAC core on BCMA bus.
 *
 * Copyright (C) 2012 Rafał Miłecki <zajec5@gmail.com>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/bcma/bcma.h>
#include <linux/brcmphy.h>
#include "bgmac.h"

struct bcma_mdio {
	struct bcma_device *core;
	u8 phyaddr;
};

static bool bcma_mdio_wait_value(struct bcma_device *core, u16 reg, u32 mask,
				 u32 value, int timeout)
{
	u32 val;
	int i;

	for (i = 0; i < timeout / 10; i++) {
		val = bcma_read32(core, reg);
		if ((val & mask) == value)
			return true;
		udelay(10);
	}
	dev_err(&core->dev, "Timeout waiting for reg 0x%X\n", reg);
	return false;
}

/**************************************************
 * PHY ops
 **************************************************/

static u16 bcma_mdio_phy_read(struct bcma_mdio *bcma_mdio, u8 phyaddr, u8 reg)
{
	struct bcma_device *core;
	u16 phy_access_addr;
	u16 phy_ctl_addr;
	u32 tmp;

	BUILD_BUG_ON(BGMAC_PA_DATA_MASK != BCMA_GMAC_CMN_PA_DATA_MASK);
	BUILD_BUG_ON(BGMAC_PA_ADDR_MASK != BCMA_GMAC_CMN_PA_ADDR_MASK);
	BUILD_BUG_ON(BGMAC_PA_ADDR_SHIFT != BCMA_GMAC_CMN_PA_ADDR_SHIFT);
	BUILD_BUG_ON(BGMAC_PA_REG_MASK != BCMA_GMAC_CMN_PA_REG_MASK);
	BUILD_BUG_ON(BGMAC_PA_REG_SHIFT != BCMA_GMAC_CMN_PA_REG_SHIFT);
	BUILD_BUG_ON(BGMAC_PA_WRITE != BCMA_GMAC_CMN_PA_WRITE);
	BUILD_BUG_ON(BGMAC_PA_START != BCMA_GMAC_CMN_PA_START);
	BUILD_BUG_ON(BGMAC_PC_EPA_MASK != BCMA_GMAC_CMN_PC_EPA_MASK);
	BUILD_BUG_ON(BGMAC_PC_MCT_MASK != BCMA_GMAC_CMN_PC_MCT_MASK);
	BUILD_BUG_ON(BGMAC_PC_MCT_SHIFT != BCMA_GMAC_CMN_PC_MCT_SHIFT);
	BUILD_BUG_ON(BGMAC_PC_MTE != BCMA_GMAC_CMN_PC_MTE);

	if (bcma_mdio->core->id.id == BCMA_CORE_4706_MAC_GBIT) {
		core = bcma_mdio->core->bus->drv_gmac_cmn.core;
		phy_access_addr = BCMA_GMAC_CMN_PHY_ACCESS;
		phy_ctl_addr = BCMA_GMAC_CMN_PHY_CTL;
	} else {
		core = bcma_mdio->core;
		phy_access_addr = BGMAC_PHY_ACCESS;
		phy_ctl_addr = BGMAC_PHY_CNTL;
	}

	tmp = bcma_read32(core, phy_ctl_addr);
	tmp &= ~BGMAC_PC_EPA_MASK;
	tmp |= phyaddr;
	bcma_write32(core, phy_ctl_addr, tmp);

	tmp = BGMAC_PA_START;
	tmp |= phyaddr << BGMAC_PA_ADDR_SHIFT;
	tmp |= reg << BGMAC_PA_REG_SHIFT;
	bcma_write32(core, phy_access_addr, tmp);

	if (!bcma_mdio_wait_value(core, phy_access_addr, BGMAC_PA_START, 0,
				  1000)) {
		dev_err(&core->dev, "Reading PHY %d register 0x%X failed\n",
			phyaddr, reg);
		return 0xffff;
	}

	return bcma_read32(core, phy_access_addr) & BGMAC_PA_DATA_MASK;
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphywr */
static int bcma_mdio_phy_write(struct bcma_mdio *bcma_mdio, u8 phyaddr, u8 reg,
			       u16 value)
{
	struct bcma_device *core;
	u16 phy_access_addr;
	u16 phy_ctl_addr;
	u32 tmp;

	if (bcma_mdio->core->id.id == BCMA_CORE_4706_MAC_GBIT) {
		core = bcma_mdio->core->bus->drv_gmac_cmn.core;
		phy_access_addr = BCMA_GMAC_CMN_PHY_ACCESS;
		phy_ctl_addr = BCMA_GMAC_CMN_PHY_CTL;
	} else {
		core = bcma_mdio->core;
		phy_access_addr = BGMAC_PHY_ACCESS;
		phy_ctl_addr = BGMAC_PHY_CNTL;
	}

	tmp = bcma_read32(core, phy_ctl_addr);
	tmp &= ~BGMAC_PC_EPA_MASK;
	tmp |= phyaddr;
	bcma_write32(core, phy_ctl_addr, tmp);

	bcma_write32(bcma_mdio->core, BGMAC_INT_STATUS, BGMAC_IS_MDIO);
	if (bcma_read32(bcma_mdio->core, BGMAC_INT_STATUS) & BGMAC_IS_MDIO)
		dev_warn(&core->dev, "Error setting MDIO int\n");

	tmp = BGMAC_PA_START;
	tmp |= BGMAC_PA_WRITE;
	tmp |= phyaddr << BGMAC_PA_ADDR_SHIFT;
	tmp |= reg << BGMAC_PA_REG_SHIFT;
	tmp |= value;
	bcma_write32(core, phy_access_addr, tmp);

	if (!bcma_mdio_wait_value(core, phy_access_addr, BGMAC_PA_START, 0,
				  1000)) {
		dev_err(&core->dev, "Writing to PHY %d register 0x%X failed\n",
			phyaddr, reg);
		return -ETIMEDOUT;
	}

	return 0;
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphyinit */
static void bcma_mdio_phy_init(struct bcma_mdio *bcma_mdio)
{
	struct bcma_chipinfo *ci = &bcma_mdio->core->bus->chipinfo;
	u8 i;

	if (ci->id == BCMA_CHIP_ID_BCM5356) {
		for (i = 0; i < 5; i++) {
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x008b);
			bcma_mdio_phy_write(bcma_mdio, i, 0x15, 0x0100);
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000f);
			bcma_mdio_phy_write(bcma_mdio, i, 0x12, 0x2aaa);
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000b);
		}
	}
	if ((ci->id == BCMA_CHIP_ID_BCM5357 && ci->pkg != 10) ||
	    (ci->id == BCMA_CHIP_ID_BCM4749 && ci->pkg != 10) ||
	    (ci->id == BCMA_CHIP_ID_BCM53572 && ci->pkg != 9)) {
		struct bcma_drv_cc *cc = &bcma_mdio->core->bus->drv_cc;

		bcma_chipco_chipctl_maskset(cc, 2, ~0xc0000000, 0);
		bcma_chipco_chipctl_maskset(cc, 4, ~0x80000000, 0);
		for (i = 0; i < 5; i++) {
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000f);
			bcma_mdio_phy_write(bcma_mdio, i, 0x16, 0x5284);
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000b);
			bcma_mdio_phy_write(bcma_mdio, i, 0x17, 0x0010);
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000f);
			bcma_mdio_phy_write(bcma_mdio, i, 0x16, 0x5296);
			bcma_mdio_phy_write(bcma_mdio, i, 0x17, 0x1073);
			bcma_mdio_phy_write(bcma_mdio, i, 0x17, 0x9073);
			bcma_mdio_phy_write(bcma_mdio, i, 0x16, 0x52b6);
			bcma_mdio_phy_write(bcma_mdio, i, 0x17, 0x9273);
			bcma_mdio_phy_write(bcma_mdio, i, 0x1f, 0x000b);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipphyreset */
static int bcma_mdio_phy_reset(struct mii_bus *bus)
{
	struct bcma_mdio *bcma_mdio = bus->priv;
	u8 phyaddr = bcma_mdio->phyaddr;

	if (bcma_mdio->phyaddr == BGMAC_PHY_NOREGS)
		return 0;

	bcma_mdio_phy_write(bcma_mdio, phyaddr, MII_BMCR, BMCR_RESET);
	udelay(100);
	if (bcma_mdio_phy_read(bcma_mdio, phyaddr, MII_BMCR) & BMCR_RESET)
		dev_err(&bcma_mdio->core->dev, "PHY reset failed\n");
	bcma_mdio_phy_init(bcma_mdio);

	return 0;
}

/**************************************************
 * MII
 **************************************************/

static int bcma_mdio_mii_read(struct mii_bus *bus, int mii_id, int regnum)
{
	return bcma_mdio_phy_read(bus->priv, mii_id, regnum);
}

static int bcma_mdio_mii_write(struct mii_bus *bus, int mii_id, int regnum,
			       u16 value)
{
	return bcma_mdio_phy_write(bus->priv, mii_id, regnum, value);
}

struct mii_bus *bcma_mdio_mii_register(struct bcma_device *core, u8 phyaddr)
{
	struct bcma_mdio *bcma_mdio;
	struct mii_bus *mii_bus;
	int err;

	bcma_mdio = kzalloc(sizeof(*bcma_mdio), GFP_KERNEL);
	if (!bcma_mdio)
		return ERR_PTR(-ENOMEM);

	mii_bus = mdiobus_alloc();
	if (!mii_bus) {
		err = -ENOMEM;
		goto err;
	}

	mii_bus->name = "bcma_mdio mii bus";
	sprintf(mii_bus->id, "%s-%d-%d", "bcma_mdio", core->bus->num,
		core->core_unit);
	mii_bus->priv = bcma_mdio;
	mii_bus->read = bcma_mdio_mii_read;
	mii_bus->write = bcma_mdio_mii_write;
	mii_bus->reset = bcma_mdio_phy_reset;
	mii_bus->parent = &core->dev;
	mii_bus->phy_mask = ~(1 << phyaddr);

	bcma_mdio->core = core;
	bcma_mdio->phyaddr = phyaddr;

	err = mdiobus_register(mii_bus);
	if (err) {
		dev_err(&core->dev, "Registration of mii bus failed\n");
		goto err_free_bus;
	}

	return mii_bus;

err_free_bus:
	mdiobus_free(mii_bus);
err:
	kfree(bcma_mdio);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(bcma_mdio_mii_register);

void bcma_mdio_mii_unregister(struct mii_bus *mii_bus)
{
	struct bcma_mdio *bcma_mdio;

	if (!mii_bus)
		return;

	bcma_mdio = mii_bus->priv;

	mdiobus_unregister(mii_bus);
	mdiobus_free(mii_bus);
	kfree(bcma_mdio);
}
EXPORT_SYMBOL_GPL(bcma_mdio_mii_unregister);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
