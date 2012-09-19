/*
 * Broadcom specific AMBA
 * ChipCommon serial flash interface
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/delay.h>

#include "bcma_private.h"

/* Initialize serial flash access */
int bcma_sflash_init(struct bcma_drv_cc *cc)
{
	bcma_err(cc->core->bus, "Serial flash support is broken\n");
	return 0;
}
