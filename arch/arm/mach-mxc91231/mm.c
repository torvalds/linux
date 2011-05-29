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
static struct map_desc mxc91231_io_desc[] __initdata = {
	imx_map_entry(MXC91231, L2CC, MT_DEVICE),
	imx_map_entry(MXC91231, X_MEMC, MT_DEVICE),
	imx_map_entry(MXC91231, ROMP, MT_DEVICE),
	imx_map_entry(MXC91231, AVIC, MT_DEVICE),
	imx_map_entry(MXC91231, AIPS1, MT_DEVICE),
	imx_map_entry(MXC91231, SPBA0, MT_DEVICE),
	imx_map_entry(MXC91231, SPBA1, MT_DEVICE),
	imx_map_entry(MXC91231, AIPS2, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory map for
 * the IO modules.
 */
void __init mxc91231_map_io(void)
{
	iotable_init(mxc91231_io_desc, ARRAY_SIZE(mxc91231_io_desc));
}

void __init mxc91231_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MXC91231);
}

int mxc91231_register_gpios(void);

void __init mxc91231_init_irq(void)
{
	mxc91231_register_gpios();
	mxc_init_irq(MXC91231_IO_ADDRESS(MXC91231_AVIC_BASE_ADDR));
}
