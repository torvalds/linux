/*
 * arch/arm/mach-kirkwood/nsa-310-setup.c
 *
 * ZyXEL NSA-310 Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <mach/kirkwood.h>
#include <linux/of.h>
#include "common.h"

static int __init nsa310_pci_init(void)
{
	if (of_machine_is_compatible("zyxel,nsa310"))
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}

subsys_initcall(nsa310_pci_init);
