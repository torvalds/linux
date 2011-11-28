/*
 * Broadcom specific AMBA
 * PCI Core in hostmode
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/bcma/bcma.h>

void bcma_core_pci_hostmode_init(struct bcma_drv_pci *pc)
{
	pr_err("No support for PCI core in hostmode yet\n");
}
