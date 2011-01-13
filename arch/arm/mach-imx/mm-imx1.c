/*
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX1 machines
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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/hardware.h>

static struct map_desc imx_io_desc[] __initdata = {
	imx_map_entry(MX1, IO, MT_DEVICE),
};

void __init mx1_map_io(void)
{
	mxc_set_cpu_type(MXC_CPU_MX1);
	mxc_arch_reset_init(MX1_IO_ADDRESS(MX1_WDT_BASE_ADDR));

	iotable_init(imx_io_desc, ARRAY_SIZE(imx_io_desc));
}

int imx1_register_gpios(void);

void __init mx1_init_irq(void)
{
	mxc_init_irq(MX1_IO_ADDRESS(MX1_AVIC_BASE_ADDR));
	imx1_register_gpios();
}
