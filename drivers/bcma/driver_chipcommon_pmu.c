/*
 * Broadcom specific AMBA
 * ChipCommon Power Management Unit driver
 *
 * Copyright 2009, Michael Buesch <m@bues.ch>
 * Copyright 2007, Broadcom Corporation
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/bcma/bcma.h>

static void bcma_chipco_chipctl_maskset(struct bcma_drv_cc *cc,
					u32 offset, u32 mask, u32 set)
{
	u32 value;

	bcma_cc_read32(cc, BCMA_CC_CHIPCTL_ADDR);
	bcma_cc_write32(cc, BCMA_CC_CHIPCTL_ADDR, offset);
	bcma_cc_read32(cc, BCMA_CC_CHIPCTL_ADDR);
	value = bcma_cc_read32(cc, BCMA_CC_CHIPCTL_DATA);
	value &= mask;
	value |= set;
	bcma_cc_write32(cc, BCMA_CC_CHIPCTL_DATA, value);
	bcma_cc_read32(cc, BCMA_CC_CHIPCTL_DATA);
}

static void bcma_pmu_pll_init(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case 0x4313:
	case 0x4331:
	case 43224:
	case 43225:
		break;
	default:
		pr_err("PLL init unknown for device 0x%04X\n",
			bus->chipinfo.id);
	}
}

static void bcma_pmu_resources_init(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;
	u32 min_msk = 0, max_msk = 0;

	switch (bus->chipinfo.id) {
	case 0x4313:
		min_msk = 0x200D;
		max_msk = 0xFFFF;
		break;
	case 43224:
	case 43225:
		break;
	default:
		pr_err("PMU resource config unknown for device 0x%04X\n",
			bus->chipinfo.id);
	}

	/* Set the resource masks. */
	if (min_msk)
		bcma_cc_write32(cc, BCMA_CC_PMU_MINRES_MSK, min_msk);
	if (max_msk)
		bcma_cc_write32(cc, BCMA_CC_PMU_MAXRES_MSK, max_msk);
}

void bcma_pmu_swreg_init(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case 0x4313:
	case 0x4331:
	case 43224:
	case 43225:
		break;
	default:
		pr_err("PMU switch/regulators init unknown for device "
			"0x%04X\n", bus->chipinfo.id);
	}
}

void bcma_pmu_workarounds(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case 0x4313:
		bcma_chipco_chipctl_maskset(cc, 0, ~0, 0x7);
		break;
	case 0x4331:
		pr_err("Enabling Ext PA lines not implemented\n");
		break;
	case 43224:
		if (bus->chipinfo.rev == 0) {
			pr_err("Workarounds for 43224 rev 0 not fully "
				"implemented\n");
			bcma_chipco_chipctl_maskset(cc, 0, ~0, 0x00F000F0);
		} else {
			bcma_chipco_chipctl_maskset(cc, 0, ~0, 0xF0);
		}
		break;
	case 43225:
		break;
	default:
		pr_err("Workarounds unknown for device 0x%04X\n",
			bus->chipinfo.id);
	}
}

void bcma_pmu_init(struct bcma_drv_cc *cc)
{
	u32 pmucap;

	pmucap = bcma_cc_read32(cc, BCMA_CC_PMU_CAP);
	cc->pmu.rev = (pmucap & BCMA_CC_PMU_CAP_REVISION);

	pr_debug("Found rev %u PMU (capabilities 0x%08X)\n", cc->pmu.rev,
		 pmucap);

	if (cc->pmu.rev == 1)
		bcma_cc_mask32(cc, BCMA_CC_PMU_CTL,
			      ~BCMA_CC_PMU_CTL_NOILPONW);
	else
		bcma_cc_set32(cc, BCMA_CC_PMU_CTL,
			     BCMA_CC_PMU_CTL_NOILPONW);

	if (cc->core->id.id == 0x4329 && cc->core->id.rev == 2)
		pr_err("Fix for 4329b0 bad LPOM state not implemented!\n");

	bcma_pmu_pll_init(cc);
	bcma_pmu_resources_init(cc);
	bcma_pmu_swreg_init(cc);
	bcma_pmu_workarounds(cc);
}
