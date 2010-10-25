/*
 *  Copyright (C) 1999,2000 Arm Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *    - add MX31 specific definitions
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
#include <linux/err.h>

#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/mx25.h>
#include <mach/iomux-v3.h>

/*
 * This table defines static virtual address mappings for I/O regions.
 * These are the mappings common across all MX25 boards.
 */
static struct map_desc mx25_io_desc[] __initdata = {
	imx_map_entry(MX25, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX25, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX25, AIPS2, MT_DEVICE_NONSHARED),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx25_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX25);
	mxc_iomux_v3_init(MX25_IO_ADDRESS(MX25_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX25_IO_ADDRESS(MX25_WDOG_BASE_ADDR));

	iotable_init(mx25_io_desc, ARRAY_SIZE(mx25_io_desc));
}

int imx25_register_gpios(void);

void __init mx25_init_irq(void)
{
	mxc_init_irq(MX25_IO_ADDRESS(MX25_AVIC_BASE_ADDR));
	imx25_register_gpios();
}

