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
#include <mach/mx25.h>
#include <mach/iomux-v3.h>
#include <mach/irqs.h>

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
	iotable_init(mx25_io_desc, ARRAY_SIZE(mx25_io_desc));
}

void __init imx25_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX25);
	mxc_iomux_v3_init(MX25_IO_ADDRESS(MX25_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX25_IO_ADDRESS(MX25_WDOG_BASE_ADDR));
}

void __init mx25_init_irq(void)
{
	mxc_init_irq(MX25_IO_ADDRESS(MX25_AVIC_BASE_ADDR));
}

static struct sdma_script_start_addrs imx25_sdma_script __initdata = {
	.ap_2_ap_addr = 729,
	.uart_2_mcu_addr = 904,
	.per_2_app_addr = 1255,
	.mcu_2_app_addr = 834,
	.uartsh_2_mcu_addr = 1120,
	.per_2_shp_addr = 1329,
	.mcu_2_shp_addr = 1048,
	.ata_2_mcu_addr = 1560,
	.mcu_2_ata_addr = 1479,
	.app_2_per_addr = 1189,
	.app_2_mcu_addr = 770,
	.shp_2_per_addr = 1407,
	.shp_2_mcu_addr = 979,
};

static struct sdma_platform_data imx25_sdma_pdata __initdata = {
	.fw_name = "sdma-imx25.bin",
	.script_addrs = &imx25_sdma_script,
};

void __init imx25_soc_init(void)
{
	/* i.mx25 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX25_GPIO1_BASE_ADDR, SZ_16K, MX25_INT_GPIO1, 0);
	mxc_register_gpio("imx31-gpio", 1, MX25_GPIO2_BASE_ADDR, SZ_16K, MX25_INT_GPIO2, 0);
	mxc_register_gpio("imx31-gpio", 2, MX25_GPIO3_BASE_ADDR, SZ_16K, MX25_INT_GPIO3, 0);
	mxc_register_gpio("imx31-gpio", 3, MX25_GPIO4_BASE_ADDR, SZ_16K, MX25_INT_GPIO4, 0);

	/* i.mx25 has the i.mx35 type sdma */
	imx_add_imx_sdma("imx35-sdma", MX25_SDMA_BASE_ADDR, MX25_INT_SDMA, &imx25_sdma_pdata);
}
