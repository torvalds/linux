/*
 * Broadcom specific AMBA
 * ChipCommon parallel flash
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"

#include <linux/bcma/bcma.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>

static const char * const part_probes[] = { "bcm47xxpart", NULL };

static struct physmap_flash_data bcma_pflash_data = {
	.part_probe_types	= part_probes,
};

static struct resource bcma_pflash_resource = {
	.name	= "bcma_pflash",
	.flags  = IORESOURCE_MEM,
};

struct platform_device bcma_pflash_dev = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data  = &bcma_pflash_data,
	},
	.resource	= &bcma_pflash_resource,
	.num_resources	= 1,
};

int bcma_pflash_init(struct bcma_drv_cc *cc)
{
	struct bcma_pflash *pflash = &cc->pflash;

	pflash->present = true;

	if (!(bcma_read32(cc->core, BCMA_CC_FLASH_CFG) & BCMA_CC_FLASH_CFG_DS))
		bcma_pflash_data.width = 1;
	else
		bcma_pflash_data.width = 2;

	bcma_pflash_resource.start = BCMA_SOC_FLASH2;
	bcma_pflash_resource.end = BCMA_SOC_FLASH2 + BCMA_SOC_FLASH2_SZ;

	return 0;
}
