/*
 * arch/arm/mach-orion/common.c
 *
 * Core functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/mach/map.h>
#include <asm/arch/orion.h>
#include "common.h"

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc orion_io_desc[] __initdata = {
	{
		.virtual	= ORION_REGS_BASE,
		.pfn		= __phys_to_pfn(ORION_REGS_BASE),
		.length		= ORION_REGS_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= ORION_PCIE_IO_BASE,
		.pfn		= __phys_to_pfn(ORION_PCIE_IO_BASE),
		.length		= ORION_PCIE_IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= ORION_PCI_IO_BASE,
		.pfn		= __phys_to_pfn(ORION_PCI_IO_BASE),
		.length		= ORION_PCI_IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= ORION_PCIE_WA_BASE,
		.pfn		= __phys_to_pfn(ORION_PCIE_WA_BASE),
		.length		= ORION_PCIE_WA_SIZE,
		.type		= MT_DEVICE
	},
};

void __init orion_map_io(void)
{
	iotable_init(orion_io_desc, ARRAY_SIZE(orion_io_desc));
}

/*****************************************************************************
 * General
 ****************************************************************************/

/*
 * Identify device ID and rev from PCIE configuration header space '0'.
 */
static void orion_id(u32 *dev, u32 *rev, char **dev_name)
{
	orion_pcie_id(dev, rev);

	if (*dev == MV88F5281_DEV_ID) {
		if (*rev == MV88F5281_REV_D2) {
			*dev_name = "MV88F5281-D2";
		} else if (*rev == MV88F5281_REV_D1) {
			*dev_name = "MV88F5281-D1";
		} else {
			*dev_name = "MV88F5281-Rev-Unsupported";
		}
	} else if (*dev == MV88F5182_DEV_ID) {
		if (*rev == MV88F5182_REV_A2) {
			*dev_name = "MV88F5182-A2";
		} else {
			*dev_name = "MV88F5182-Rev-Unsupported";
		}
	} else {
		*dev_name = "Device-Unknown";
	}
}

void __init orion_init(void)
{
	char *dev_name;
	u32 dev, rev;

	orion_id(&dev, &rev, &dev_name);
	printk(KERN_INFO "Orion ID: %s. TCLK=%d.\n", dev_name, ORION_TCLK);

	/*
	 * Setup Orion address map
	 */
	orion_setup_cpu_wins();
	orion_setup_usb_wins();
	orion_setup_eth_wins();
	orion_setup_pci_wins();
	orion_setup_pcie_wins();
	if (dev == MV88F5182_DEV_ID)
		orion_setup_sata_wins();
}
