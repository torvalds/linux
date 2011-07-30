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
#include <mach/devices-common.h>
#include <mach/hardware.h>
#include <mach/iomux-v3.h>
#include <mach/irqs.h>

static struct map_desc mx31_io_desc[] __initdata = {
	imx_map_entry(MX31, X_MEMC, MT_DEVICE),
	imx_map_entry(MX31, AVIC, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS1, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, AIPS2, MT_DEVICE_NONSHARED),
	imx_map_entry(MX31, SPBA0, MT_DEVICE_NONSHARED),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx31_map_io(void)
{
	iotable_init(mx31_io_desc, ARRAY_SIZE(mx31_io_desc));
}

void __init imx31_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX31);
	mxc_arch_reset_init(MX31_IO_ADDRESS(MX31_WDOG_BASE_ADDR));
}

void __init mx31_init_irq(void)
{
	mxc_init_irq(MX31_IO_ADDRESS(MX31_AVIC_BASE_ADDR));
}

static struct sdma_script_start_addrs imx31_to1_sdma_script __initdata = {
	.per_2_per_addr = 1677,
};

static struct sdma_script_start_addrs imx31_to2_sdma_script __initdata = {
	.ap_2_ap_addr = 423,
	.ap_2_bp_addr = 829,
	.bp_2_ap_addr = 1029,
};

static struct sdma_platform_data imx31_sdma_pdata __initdata = {
	.fw_name = "sdma-imx31-to2.bin",
	.script_addrs = &imx31_to2_sdma_script,
};

void __init imx31_soc_init(void)
{
	int to_version = mx31_revision() >> 4;

	mxc_register_gpio("imx31-gpio", 0, MX31_GPIO1_BASE_ADDR, SZ_16K, MX31_INT_GPIO1, 0);
	mxc_register_gpio("imx31-gpio", 1, MX31_GPIO2_BASE_ADDR, SZ_16K, MX31_INT_GPIO2, 0);
	mxc_register_gpio("imx31-gpio", 2, MX31_GPIO3_BASE_ADDR, SZ_16K, MX31_INT_GPIO3, 0);

	if (to_version == 1) {
		strncpy(imx31_sdma_pdata.fw_name, "sdma-imx31-to1.bin",
			strlen(imx31_sdma_pdata.fw_name));
		imx31_sdma_pdata.script_addrs = &imx31_to1_sdma_script;
	}

	imx_add_imx_sdma("imx31-sdma", MX31_SDMA_BASE_ADDR, MX31_INT_SDMA, &imx31_sdma_pdata);
}
