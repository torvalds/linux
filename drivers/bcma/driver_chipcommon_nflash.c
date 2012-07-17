/*
 * Broadcom specific AMBA
 * ChipCommon NAND flash interface
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/delay.h>

#include "bcma_private.h"

/* Initialize NAND flash access */
int bcma_nflash_init(struct bcma_drv_cc *cc)
{
	bcma_err(cc->core->bus, "NAND flash support is broken\n");
	return 0;
}
