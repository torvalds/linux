/*
 * Broadcom specific AMBA
 * GBIT MAC COMMON Core
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/bcma/bcma.h>

void __devinit bcma_core_gmac_cmn_init(struct bcma_drv_gmac_cmn *gc)
{
	mutex_init(&gc->phy_mutex);
}
