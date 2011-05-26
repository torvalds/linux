/*
 * Broadcom specific AMBA
 * Core ops
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
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

static void bcma_core_disable(struct bcma_device *core, u32 flags)
{
	if (bcma_aread32(core, BCMA_RESET_CTL) & BCMA_RESET_CTL_RESET)
		return;

	bcma_awrite32(core, BCMA_IOCTL, flags);
	bcma_aread32(core, BCMA_IOCTL);
	udelay(10);

	bcma_awrite32(core, BCMA_RESET_CTL, BCMA_RESET_CTL_RESET);
	udelay(1);
}

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
