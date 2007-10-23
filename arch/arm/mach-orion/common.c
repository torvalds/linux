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
