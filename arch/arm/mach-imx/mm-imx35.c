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
#include <asm/hardware/cache-l2x0.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-v3.h>
#include <mach/irqs.h>

static struct map_desc mx35_io_desc[] __initdata = {
	imx_map_entry(MX35, X_MEMC, MT_DEVICE),
	imx_map_entry(MX35, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX35, SPBA0, MT_DEVICE_NONSHARED),
};

void __init mx35_map_io(void)
{
	iotable_init(mx35_io_desc, ARRAY_SIZE(mx35_io_desc));
}

void __init imx35_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX35);
	mxc_iomux_v3_init(MX35_IO_ADDRESS(MX35_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX35_IO_ADDRESS(MX35_WDOG_BASE_ADDR));
}

void __init mx35_init_irq(void)
{
	mxc_init_irq(MX35_IO_ADDRESS(MX35_AVIC_BASE_ADDR));
}

void __init imx35_soc_init(void)
{
	/* i.mx35 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX35_GPIO1_BASE_ADDR, SZ_16K, MX35_INT_GPIO1, 0);
	mxc_register_gpio("imx31-gpio", 1, MX35_GPIO2_BASE_ADDR, SZ_16K, MX35_INT_GPIO2, 0);
	mxc_register_gpio("imx31-gpio", 2, MX35_GPIO3_BASE_ADDR, SZ_16K, MX35_INT_GPIO3, 0);
}
