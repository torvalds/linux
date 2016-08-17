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
#include <linux/etherdevice.h>
#include "bgmac.h"

static inline bool bgmac_is_bcm4707_family(struct bcma_device *core)
{
	switch (core->bus->chipinfo.id) {
	case BCMA_CHIP_ID_BCM4707:
	case BCMA_CHIP_ID_BCM47094:
	case BCMA_CHIP_ID_BCM53018:
		return true;
	default:
		return false;
	}
}

/**************************************************
 * BCMA bus ops
 **************************************************/

static u32 bcma_bgmac_read(struct bgmac *bgmac, u16 offset)
{
	return bcma_read32(bgmac->bcma.core, offset);
}

static void bcma_bgmac_write(struct bgmac *bgmac, u16 offset, u32 value)
{
	bcma_write32(bgmac->bcma.core, offset, value);
}

static u32 bcma_bgmac_idm_read(struct bgmac *bgmac, u16 offset)
{
	return bcma_aread32(bgmac->bcma.core, offset);
}

static void bcma_bgmac_idm_write(struct bgmac *bgmac, u16 offset, u32 value)
{
	return bcma_awrite32(bgmac->bcma.core, offset, value);
}

static bool bcma_bgmac_clk_enabled(struct bgmac *bgmac)
{
	return bcma_core_is_enabled(bgmac->bcma.core);
}

static void bcma_bgmac_clk_enable(struct bgmac *bgmac, u32 flags)
{
	bcma_core_enable(bgmac->bcma.core, flags);
}

static void bcma_bgmac_cco_ctl_maskset(struct bgmac *bgmac, u32 offset,
				       u32 mask, u32 set)
{
	struct bcma_drv_cc *cc = &bgmac->bcma.core->bus->drv_cc;

	bcma_chipco_chipctl_maskset(cc, offset, mask, set);
}

static u32 bcma_bgmac_get_bus_clock(struct bgmac *bgmac)
{
	struct bcma_drv_cc *cc = &bgmac->bcma.core->bus->drv_cc;

	return bcma_pmu_get_bus_clock(cc);
}

static void bcma_bgmac_cmn_maskset32(struct bgmac *bgmac, u16 offset, u32 mask,
				     u32 set)
{
	bcma_maskset32(bgmac->bcma.cmn, offset, mask, set);
}

static const struct bcma_device_id bgmac_bcma_tbl[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_4706_MAC_GBIT,
		  BCMA_ANY_REV, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_MAC_GBIT, BCMA_ANY_REV,
		  BCMA_ANY_CLASS),
	{},
};
MODULE_DEVICE_TABLE(bcma, bgmac_bcma_tbl);

/* http://bcm-v4.sipsolutions.net/mac-gbit/gmac/chipattach */
static int bgmac_probe(struct bcma_device *core)
{
	struct bcma_chipinfo *ci = &core->bus->chipinfo;
	struct ssb_sprom *sprom = &core->bus->sprom;
	struct mii_bus *mii_bus;
	struct bgmac *bgmac;
	u8 *mac;
	int err;

	bgmac = kzalloc(sizeof(*bgmac), GFP_KERNEL);
	if (!bgmac)
		return -ENOMEM;

	bgmac->bcma.core = core;
	bgmac->dev = &core->dev;
	bgmac->dma_dev = core->dma_dev;
	bgmac->irq = core->irq;

	bcma_set_drvdata(core, bgmac);

	switch (core->core_unit) {
	case 0:
		mac = sprom->et0mac;
		break;
	case 1:
		mac = sprom->et1mac;
		break;
	case 2:
		mac = sprom->et2mac;
		break;
	default:
		dev_err(bgmac->dev, "Unsupported core_unit %d\n",
			core->core_unit);
		err = -ENOTSUPP;
		goto err;
	}

	ether_addr_copy(bgmac->mac_addr, mac);

	/* On BCM4706 we need common core to access PHY */
	if (core->id.id == BCMA_CORE_4706_MAC_GBIT &&
	    !core->bus->drv_gmac_cmn.core) {
		dev_err(bgmac->dev, "GMAC CMN core not found (required for BCM4706)\n");
		err = -ENODEV;
		goto err;
	}
	bgmac->bcma.cmn = core->bus->drv_gmac_cmn.core;

	switch (core->core_unit) {
	case 0:
		bgmac->phyaddr = sprom->et0phyaddr;
		break;
	case 1:
		bgmac->phyaddr = sprom->et1phyaddr;
		break;
	case 2:
		bgmac->phyaddr = sprom->et2phyaddr;
		break;
	}
	bgmac->phyaddr &= BGMAC_PHY_MASK;
	if (bgmac->phyaddr == BGMAC_PHY_MASK) {
		dev_err(bgmac->dev, "No PHY found\n");
		err = -ENODEV;
		goto err;
	}
	dev_info(bgmac->dev, "Found PHY addr: %d%s\n", bgmac->phyaddr,
		 bgmac->phyaddr == BGMAC_PHY_NOREGS ? " (NOREGS)" : "");

	if (!bgmac_is_bcm4707_family(core) &&
	    !(ci->id == BCMA_CHIP_ID_BCM53573 && core->core_unit == 1)) {
		mii_bus = bcma_mdio_mii_register(core, bgmac->phyaddr);
		if (!IS_ERR(mii_bus)) {
			err = PTR_ERR(mii_bus);
			goto err;
		}

		bgmac->mii_bus = mii_bus;
	}

	if (core->bus->hosttype == BCMA_HOSTTYPE_PCI) {
		dev_err(bgmac->dev, "PCI setup not implemented\n");
		err = -ENOTSUPP;
		goto err1;
	}

	bgmac->has_robosw = !!(core->bus->sprom.boardflags_lo &
			       BGMAC_BFL_ENETROBO);
	if (bgmac->has_robosw)
		dev_warn(bgmac->dev, "Support for Roboswitch not implemented\n");

	if (core->bus->sprom.boardflags_lo & BGMAC_BFL_ENETADM)
		dev_warn(bgmac->dev, "Support for ADMtek ethernet switch not implemented\n");

	/* Feature Flags */
	switch (core->bus->chipinfo.id) {
	case BCMA_CHIP_ID_BCM5357:
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_FLW_CTRL1;
		bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_PHY;
		if (core->bus->chipinfo.pkg == BCMA_PKG_ID_BCM47186) {
			bgmac->feature_flags |= BGMAC_FEAT_IOST_ATTACHED;
			bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_RGMII;
		}
		if (core->bus->chipinfo.pkg == BCMA_PKG_ID_BCM5358)
			bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_EPHYRMII;
		break;
	case BCMA_CHIP_ID_BCM53572:
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_FLW_CTRL1;
		bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_PHY;
		if (core->bus->chipinfo.pkg == BCMA_PKG_ID_BCM47188) {
			bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_RGMII;
			bgmac->feature_flags |= BGMAC_FEAT_IOST_ATTACHED;
		}
		break;
	case BCMA_CHIP_ID_BCM4749:
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_FLW_CTRL1;
		bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_PHY;
		if (core->bus->chipinfo.pkg == 10) {
			bgmac->feature_flags |= BGMAC_FEAT_SW_TYPE_RGMII;
			bgmac->feature_flags |= BGMAC_FEAT_IOST_ATTACHED;
		}
		break;
	case BCMA_CHIP_ID_BCM4716:
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		/* fallthrough */
	case BCMA_CHIP_ID_BCM47162:
		bgmac->feature_flags |= BGMAC_FEAT_FLW_CTRL2;
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
		break;
	/* bcm4707_family */
	case BCMA_CHIP_ID_BCM4707:
	case BCMA_CHIP_ID_BCM47094:
	case BCMA_CHIP_ID_BCM53018:
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_NO_RESET;
		bgmac->feature_flags |= BGMAC_FEAT_FORCE_SPEED_2500;
		break;
	case BCMA_CHIP_ID_BCM53573:
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
		if (ci->pkg == BCMA_PKG_ID_BCM47189)
			bgmac->feature_flags |= BGMAC_FEAT_IOST_ATTACHED;
		if (core->core_unit == 0) {
			bgmac->feature_flags |= BGMAC_FEAT_CC4_IF_SW_TYPE;
			if (ci->pkg == BCMA_PKG_ID_BCM47189)
				bgmac->feature_flags |=
					BGMAC_FEAT_CC4_IF_SW_TYPE_RGMII;
		} else if (core->core_unit == 1) {
			bgmac->feature_flags |= BGMAC_FEAT_IRQ_ID_OOB_6;
			bgmac->feature_flags |= BGMAC_FEAT_CC7_IF_TYPE_RGMII;
		}
		break;
	default:
		bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
		bgmac->feature_flags |= BGMAC_FEAT_SET_RXQ_CLK;
	}

	if (!bgmac_is_bcm4707_family(core) && core->id.rev > 2)
		bgmac->feature_flags |= BGMAC_FEAT_MISC_PLL_REQ;

	if (core->id.id == BCMA_CORE_4706_MAC_GBIT) {
		bgmac->feature_flags |= BGMAC_FEAT_CMN_PHY_CTL;
		bgmac->feature_flags |= BGMAC_FEAT_NO_CLR_MIB;
	}

	if (core->id.rev >= 4) {
		bgmac->feature_flags |= BGMAC_FEAT_CMDCFG_SR_REV4;
		bgmac->feature_flags |= BGMAC_FEAT_TX_MASK_SETUP;
		bgmac->feature_flags |= BGMAC_FEAT_RX_MASK_SETUP;
	}

	bgmac->read = bcma_bgmac_read;
	bgmac->write = bcma_bgmac_write;
	bgmac->idm_read = bcma_bgmac_idm_read;
	bgmac->idm_write = bcma_bgmac_idm_write;
	bgmac->clk_enabled = bcma_bgmac_clk_enabled;
	bgmac->clk_enable = bcma_bgmac_clk_enable;
	bgmac->cco_ctl_maskset = bcma_bgmac_cco_ctl_maskset;
	bgmac->get_bus_clock = bcma_bgmac_get_bus_clock;
	bgmac->cmn_maskset32 = bcma_bgmac_cmn_maskset32;

	err = bgmac_enet_probe(bgmac);
	if (err)
		goto err1;

	return 0;

err1:
	bcma_mdio_mii_unregister(bgmac->mii_bus);
err:
	kfree(bgmac);
	bcma_set_drvdata(core, NULL);

	return err;
}

static void bgmac_remove(struct bcma_device *core)
{
	struct bgmac *bgmac = bcma_get_drvdata(core);

	bcma_mdio_mii_unregister(bgmac->mii_bus);
	bgmac_enet_remove(bgmac);
	bcma_set_drvdata(core, NULL);
	kfree(bgmac);
}

static struct bcma_driver bgmac_bcma_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= bgmac_bcma_tbl,
	.probe		= bgmac_probe,
	.remove		= bgmac_remove,
};

static int __init bgmac_init(void)
{
	int err;

	err = bcma_driver_register(&bgmac_bcma_driver);
	if (err)
		return err;
	pr_info("Broadcom 47xx GBit MAC driver loaded\n");

	return 0;
}

static void __exit bgmac_exit(void)
{
	bcma_driver_unregister(&bgmac_bcma_driver);
}

module_init(bgmac_init)
module_exit(bgmac_exit)

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
