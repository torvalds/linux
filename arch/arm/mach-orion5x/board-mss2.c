// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Maxtor Shared Storage II Board Setup
 *
 * Maintainer: Sylver Bruneau <sylver.bruneau@googlemail.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/pci.h>
#include "orion5x.h"
#include "bridge-regs.h"
#include "common.h"

/*****************************************************************************
 * Maxtor Shared Storage II Info
 ****************************************************************************/

/****************************************************************************
 * PCI setup
 ****************************************************************************/
static int __init mss2_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	/*
	 * Check for devices with hard-wired IRQs.
	 */
	irq = orion5x_pci_map_irq(dev, slot, pin);
	if (irq != -1)
		return irq;

	return -1;
}

static struct hw_pci mss2_pci __initdata = {
	.nr_controllers = 2,
	.setup		= orion5x_pci_sys_setup,
	.scan		= orion5x_pci_sys_scan_bus,
	.map_irq	= mss2_pci_map_irq,
};

static int __init mss2_pci_init(void)
{
	if (machine_is_mss2())
		pci_common_init(&mss2_pci);

	return 0;
}
subsys_initcall(mss2_pci_init);

/*****************************************************************************
 * MSS2 power off method
 ****************************************************************************/
/*
 * On the Maxtor Shared Storage II, the shutdown process is the following :
 * - Userland modifies U-boot env to tell U-boot to go idle at next boot
 * - The board reboots
 * - U-boot starts and go into an idle mode until the user press "power"
 */
static void mss2_power_off(void)
{
	u32 reg;

	/*
	 * Enable and issue soft reset
	 */
	reg = readl(RSTOUTn_MASK);
	reg |= 1 << 2;
	writel(reg, RSTOUTn_MASK);

	reg = readl(CPU_SOFT_RESET);
	reg |= 1;
	writel(reg, CPU_SOFT_RESET);
}

void __init mss2_init(void)
{
	/* register mss2 specific power-off method */
	pm_power_off = mss2_power_off;
}
