/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Create static mapping between physical to virtual memory.
 */

#include <linux/mm.h>
#include <linux/init.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-v3.h>
#include <mach/irqs.h>

/*
 * Define the MX50 memory map.
 */
static struct map_desc mx50_io_desc[] __initdata = {
	imx_map_entry(MX50, TZIC, MT_DEVICE),
	imx_map_entry(MX50, SPBA0, MT_DEVICE),
	imx_map_entry(MX50, AIPS1, MT_DEVICE),
	imx_map_entry(MX50, AIPS2, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx50_map_io(void)
{
	iotable_init(mx50_io_desc, ARRAY_SIZE(mx50_io_desc));
}

void __init imx50_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX50);
	mxc_iomux_v3_init(MX50_IO_ADDRESS(MX50_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX50_IO_ADDRESS(MX50_WDOG_BASE_ADDR));
}

void __init mx50_init_irq(void)
{
	tzic_init_irq(MX50_IO_ADDRESS(MX50_TZIC_BASE_ADDR));
}

void __init imx50_soc_init(void)
{
	/* i.mx50 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX50_GPIO1_BASE_ADDR, SZ_16K, MX50_INT_GPIO1_LOW, MX50_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX50_GPIO2_BASE_ADDR, SZ_16K, MX50_INT_GPIO2_LOW, MX50_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX50_GPIO3_BASE_ADDR, SZ_16K, MX50_INT_GPIO3_LOW, MX50_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX50_GPIO4_BASE_ADDR, SZ_16K, MX50_INT_GPIO4_LOW, MX50_INT_GPIO4_HIGH);
	mxc_register_gpio("imx31-gpio", 4, MX50_GPIO5_BASE_ADDR, SZ_16K, MX50_INT_GPIO5_LOW, MX50_INT_GPIO5_HIGH);
	mxc_register_gpio("imx31-gpio", 5, MX50_GPIO6_BASE_ADDR, SZ_16K, MX50_INT_GPIO6_LOW, MX50_INT_GPIO6_HIGH);
}
