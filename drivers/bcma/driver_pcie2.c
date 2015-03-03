/*
 * Broadcom specific AMBA
 * PCIe Gen 2 Core
 *
 * Copyright 2014, Broadcom Corporation
 * Copyright 2014, Rafał Miłecki <zajec5@gmail.com>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/bcma/bcma.h>
#include <linux/pci.h>

/**************************************************
 * R/W ops.
 **************************************************/

#if 0
static u32 bcma_core_pcie2_cfg_read(struct bcma_drv_pcie2 *pcie2, u32 addr)
{
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, addr);
	pcie2_read32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR);
	return pcie2_read32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA);
}
#endif

static void bcma_core_pcie2_cfg_write(struct bcma_drv_pcie2 *pcie2, u32 addr,
				      u32 val)
{
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, addr);
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, val);
}

/**************************************************
 * Init.
 **************************************************/

static u32 bcma_core_pcie2_war_delay_perst_enab(struct bcma_drv_pcie2 *pcie2,
						bool enable)
{
	u32 val;

	/* restore back to default */
	val = pcie2_read32(pcie2, BCMA_CORE_PCIE2_CLK_CONTROL);
	val |= PCIE2_CLKC_DLYPERST;
	val &= ~PCIE2_CLKC_DISSPROMLD;
	if (enable) {
		val &= ~PCIE2_CLKC_DLYPERST;
		val |= PCIE2_CLKC_DISSPROMLD;
	}
	pcie2_write32(pcie2, (BCMA_CORE_PCIE2_CLK_CONTROL), val);
	/* flush */
	return pcie2_read32(pcie2, BCMA_CORE_PCIE2_CLK_CONTROL);
}

static void bcma_core_pcie2_set_ltr_vals(struct bcma_drv_pcie2 *pcie2)
{
	/* LTR0 */
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, 0x844);
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, 0x883c883c);
	/* LTR1 */
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, 0x848);
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, 0x88648864);
	/* LTR2 */
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, 0x84C);
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, 0x90039003);
}

static void bcma_core_pcie2_hw_ltr_war(struct bcma_drv_pcie2 *pcie2)
{
	u8 core_rev = pcie2->core->id.rev;
	u32 devstsctr2;

	if (core_rev < 2 || core_rev == 10 || core_rev > 13)
		return;

	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR,
		      PCIE2_CAP_DEVSTSCTRL2_OFFSET);
	devstsctr2 = pcie2_read32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA);
	if (devstsctr2 & PCIE2_CAP_DEVSTSCTRL2_LTRENAB) {
		/* force the right LTR values */
		bcma_core_pcie2_set_ltr_vals(pcie2);

		/* TODO:
		si_core_wrapperreg(pcie2, 3, 0x60, 0x8080, 0); */

		/* enable the LTR */
		devstsctr2 |= PCIE2_CAP_DEVSTSCTRL2_LTRENAB;
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR,
			      PCIE2_CAP_DEVSTSCTRL2_OFFSET);
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, devstsctr2);

		/* set the LTR state to be active */
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_LTR_STATE,
			      PCIE2_LTR_ACTIVE);
		usleep_range(1000, 2000);

		/* set the LTR state to be sleep */
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_LTR_STATE,
			      PCIE2_LTR_SLEEP);
		usleep_range(1000, 2000);
	}
}

static void pciedev_crwlpciegen2(struct bcma_drv_pcie2 *pcie2)
{
	u8 core_rev = pcie2->core->id.rev;
	bool pciewar160, pciewar162;

	pciewar160 = core_rev == 7 || core_rev == 9 || core_rev == 11;
	pciewar162 = core_rev == 5 || core_rev == 7 || core_rev == 8 ||
		     core_rev == 9 || core_rev == 11;

	if (!pciewar160 && !pciewar162)
		return;

/* TODO */
#if 0
	pcie2_set32(pcie2, BCMA_CORE_PCIE2_CLK_CONTROL,
		    PCIE_DISABLE_L1CLK_GATING);
#if 0
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR,
		      PCIEGEN2_COE_PVT_TL_CTRL_0);
	pcie2_mask32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA,
		     ~(1 << COE_PVT_TL_CTRL_0_PM_DIS_L1_REENTRY_BIT));
#endif
#endif
}

static void pciedev_crwlpciegen2_180(struct bcma_drv_pcie2 *pcie2)
{
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, PCIE2_PMCR_REFUP);
	pcie2_set32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, 0x1f);
}

static void pciedev_crwlpciegen2_182(struct bcma_drv_pcie2 *pcie2)
{
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR, PCIE2_SBMBX);
	pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, 1 << 0);
}

static void pciedev_reg_pm_clk_period(struct bcma_drv_pcie2 *pcie2)
{
	struct bcma_drv_cc *drv_cc = &pcie2->core->bus->drv_cc;
	u8 core_rev = pcie2->core->id.rev;
	u32 alp_khz, pm_value;

	if (core_rev <= 13) {
		alp_khz = bcma_pmu_get_alp_clock(drv_cc) / 1000;
		pm_value = (1000000 * 2) / alp_khz;
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDADDR,
			      PCIE2_PVT_REG_PM_CLK_PERIOD);
		pcie2_write32(pcie2, BCMA_CORE_PCIE2_CONFIGINDDATA, pm_value);
	}
}

void bcma_core_pcie2_init(struct bcma_drv_pcie2 *pcie2)
{
	struct bcma_bus *bus = pcie2->core->bus;
	struct bcma_chipinfo *ci = &bus->chipinfo;
	u32 tmp;

	tmp = pcie2_read32(pcie2, BCMA_CORE_PCIE2_SPROM(54));
	if ((tmp & 0xe) >> 1 == 2)
		bcma_core_pcie2_cfg_write(pcie2, 0x4e0, 0x17);

	switch (bus->chipinfo.id) {
	case BCMA_CHIP_ID_BCM4360:
	case BCMA_CHIP_ID_BCM4352:
		pcie2->reqsize = 1024;
		break;
	default:
		pcie2->reqsize = 128;
		break;
	}

	if (ci->id == BCMA_CHIP_ID_BCM4360 && ci->rev > 3)
		bcma_core_pcie2_war_delay_perst_enab(pcie2, true);
	bcma_core_pcie2_hw_ltr_war(pcie2);
	pciedev_crwlpciegen2(pcie2);
	pciedev_reg_pm_clk_period(pcie2);
	pciedev_crwlpciegen2_180(pcie2);
	pciedev_crwlpciegen2_182(pcie2);
}

/**************************************************
 * Runtime ops.
 **************************************************/

void bcma_core_pcie2_up(struct bcma_drv_pcie2 *pcie2)
{
	struct bcma_bus *bus = pcie2->core->bus;
	struct pci_dev *dev = bus->host_pci;
	int err;

	err = pcie_set_readrq(dev, pcie2->reqsize);
	if (err)
		bcma_err(bus, "Error setting PCI_EXP_DEVCTL_READRQ: %d\n", err);
}
