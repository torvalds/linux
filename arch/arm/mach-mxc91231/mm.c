/*
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2004-2005 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MXC specific definitions
 *  Copyright 2006 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

/*
 * This structure defines the MXC memory map.
 */
static struct map_desc mxc_io_desc[] __initdata = {
	{
		.virtual	= MXC91231_L2CC_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_L2CC_BASE_ADDR),
		.length		= MXC91231_L2CC_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_X_MEMC_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_X_MEMC_BASE_ADDR),
		.length		= MXC91231_X_MEMC_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_ROMP_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_ROMP_BASE_ADDR),
		.length		= MXC91231_ROMP_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_AVIC_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_AVIC_BASE_ADDR),
		.length		= MXC91231_AVIC_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_AIPS1_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_AIPS1_BASE_ADDR),
		.length		= MXC91231_AIPS1_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_SPBA0_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_SPBA0_BASE_ADDR),
		.length		= MXC91231_SPBA0_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_SPBA1_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_SPBA1_BASE_ADDR),
		.length		= MXC91231_SPBA1_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= MXC91231_AIPS2_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MXC91231_AIPS2_BASE_ADDR),
		.length		= MXC91231_AIPS2_SIZE,
		.type		= MT_DEVICE,
	},
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory map for
 * the IO modules.
 */
void __init mxc91231_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MXC91231);

	iotable_init(mxc_io_desc, ARRAY_SIZE(mxc_io_desc));
}

int mxc91231_register_gpios(void);

void __init mxc91231_init_irq(void)
{
	mxc91231_register_gpios();
	mxc_init_irq(MXC91231_IO_ADDRESS(MXC91231_AVIC_BASE_ADDR));
}
