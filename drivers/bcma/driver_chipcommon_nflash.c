/*
 * Broadcom specific AMBA
 * ChipCommon NAND flash interface
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"

#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>

struct platform_device bcma_nflash_dev = {
	.name		= "bcma_nflash",
	.num_resources	= 0,
};

/* Initialize NAND flash access */
int bcma_nflash_init(struct bcma_drv_cc *cc)
{
	struct bcma_bus *bus = cc->core->bus;

	if (bus->chipinfo.id != BCMA_CHIP_ID_BCM4706 &&
	    cc->core->id.rev != 0x38) {
		bcma_err(bus, "NAND flash on unsupported board!\n");
		return -ENOTSUPP;
	}

	if (!(cc->capabilities & BCMA_CC_CAP_NFLASH)) {
		bcma_err(bus, "NAND flash not present according to ChipCommon\n");
		return -ENODEV;
	}

	cc->nflash.present = true;
	if (cc->core->id.rev == 38 &&
	    (cc->status & BCMA_CC_CHIPST_5357_NAND_BOOT))
		cc->nflash.boot = true;

	/* Prepare platform device, but don't register it yet. It's too early,
	 * malloc (required by device_private_init) is not available yet. */
	bcma_nflash_dev.dev.platform_data = &cc->nflash;

	return 0;
}
