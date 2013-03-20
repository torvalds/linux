/*
 * Broadcom specific AMBA
 * Core ops
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/export.h>
#include <linux/bcma/bcma.h>

bool bcma_core_is_enabled(struct bcma_device *core)
{
	if ((bcma_aread32(core, BCMA_IOCTL) & (BCMA_IOCTL_CLK | BCMA_IOCTL_FGC))
	    != BCMA_IOCTL_CLK)
		return false;
	if (bcma_aread32(core, BCMA_RESET_CTL) & BCMA_RESET_CTL_RESET)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(bcma_core_is_enabled);

void bcma_core_disable(struct bcma_device *core, u32 flags)
{
	if (bcma_aread32(core, BCMA_RESET_CTL) & BCMA_RESET_CTL_RESET)
		return;

	bcma_awrite32(core, BCMA_IOCTL, flags);
	bcma_aread32(core, BCMA_IOCTL);
	udelay(10);

	bcma_awrite32(core, BCMA_RESET_CTL, BCMA_RESET_CTL_RESET);
	bcma_aread32(core, BCMA_RESET_CTL);
	udelay(1);
}
EXPORT_SYMBOL_GPL(bcma_core_disable);

int bcma_core_enable(struct bcma_device *core, u32 flags)
{
	bcma_core_disable(core, flags);

	bcma_awrite32(core, BCMA_IOCTL, (BCMA_IOCTL_CLK | BCMA_IOCTL_FGC | flags));
	bcma_aread32(core, BCMA_IOCTL);

	bcma_awrite32(core, BCMA_RESET_CTL, 0);
	udelay(1);

	bcma_awrite32(core, BCMA_IOCTL, (BCMA_IOCTL_CLK | flags));
	bcma_aread32(core, BCMA_IOCTL);
	udelay(1);

	return 0;
}
EXPORT_SYMBOL_GPL(bcma_core_enable);

void bcma_core_set_clockmode(struct bcma_device *core,
			     enum bcma_clkmode clkmode)
{
	u16 i;

	WARN_ON(core->id.id != BCMA_CORE_CHIPCOMMON &&
		core->id.id != BCMA_CORE_PCIE &&
		core->id.id != BCMA_CORE_80211);

	switch (clkmode) {
	case BCMA_CLKMODE_FAST:
		bcma_set32(core, BCMA_CLKCTLST, BCMA_CLKCTLST_FORCEHT);
		usleep_range(64, 300);
		for (i = 0; i < 1500; i++) {
			if (bcma_read32(core, BCMA_CLKCTLST) &
			    BCMA_CLKCTLST_HAVEHT) {
				i = 0;
				break;
			}
			udelay(10);
		}
		if (i)
			bcma_err(core->bus, "HT force timeout\n");
		break;
	case BCMA_CLKMODE_DYNAMIC:
		bcma_set32(core, BCMA_CLKCTLST, ~BCMA_CLKCTLST_FORCEHT);
		break;
	}
}
EXPORT_SYMBOL_GPL(bcma_core_set_clockmode);

void bcma_core_pll_ctl(struct bcma_device *core, u32 req, u32 status, bool on)
{
	u16 i;

	WARN_ON(req & ~BCMA_CLKCTLST_EXTRESREQ);
	WARN_ON(status & ~BCMA_CLKCTLST_EXTRESST);

	if (on) {
		bcma_set32(core, BCMA_CLKCTLST, req);
		for (i = 0; i < 10000; i++) {
			if ((bcma_read32(core, BCMA_CLKCTLST) & status) ==
			    status) {
				i = 0;
				break;
			}
			udelay(10);
		}
		if (i)
			bcma_err(core->bus, "PLL enable timeout\n");
	} else {
		/*
		 * Mask the PLL but don't wait for it to be disabled. PLL may be
		 * shared between cores and will be still up if there is another
		 * core using it.
		 */
		bcma_mask32(core, BCMA_CLKCTLST, ~req);
		bcma_read32(core, BCMA_CLKCTLST);
	}
}
EXPORT_SYMBOL_GPL(bcma_core_pll_ctl);

u32 bcma_core_dma_translation(struct bcma_device *core)
{
	switch (core->bus->hosttype) {
	case BCMA_HOSTTYPE_SOC:
		return 0;
	case BCMA_HOSTTYPE_PCI:
		if (bcma_aread32(core, BCMA_IOST) & BCMA_IOST_DMA64)
			return BCMA_DMA_TRANSLATION_DMA64_CMT;
		else
			return BCMA_DMA_TRANSLATION_DMA32_CMT;
	default:
		bcma_err(core->bus, "DMA translation unknown for host %d\n",
			 core->bus->hosttype);
	}
	return BCMA_DMA_TRANSLATION_NONE;
}
EXPORT_SYMBOL(bcma_core_dma_translation);
