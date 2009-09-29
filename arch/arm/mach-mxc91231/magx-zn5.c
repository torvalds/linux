/*
 * Copyright 2009 Dmitriy Taychenachev <dimichxp@gmail.com>
 *
 * This file is released under the GPLv2 or later.
 */

#include <linux/irq.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mach-types.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mxc91231.h>
#include <mach/mmc.h>
#include <mach/imx-uart.h>

#include "devices.h"

static struct imxuart_platform_data uart_pdata = {
};

static struct imxmmc_platform_data sdhc_pdata = {
};

static void __init zn5_init(void)
{
	pm_power_off = mxc91231_power_off;

	mxc_iomux_alloc_pin(MXC91231_PIN_SP_USB_DAT_VP__RXD2, "uart2-rx");
	mxc_iomux_alloc_pin(MXC91231_PIN_SP_USB_SE0_VM__TXD2, "uart2-tx");

	mxc_register_device(&mxc_uart_device1, &uart_pdata);
	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	mxc_register_device(&mxc_sdhc_device0, &sdhc_pdata);

	mxc_register_device(&mxc_wdog_device0, NULL);

	return;
}

static void __init zn5_timer_init(void)
{
	mxc91231_clocks_init(26000000); /* 26mhz ckih */
}

struct sys_timer zn5_timer = {
	.init = zn5_timer_init,
};

MACHINE_START(MAGX_ZN5, "Motorola Zn5")
	.phys_io	= MXC91231_AIPS1_BASE_ADDR,
	.io_pg_offst	= ((MXC91231_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io		= mxc91231_map_io,
	.init_irq	= mxc91231_init_irq,
	.timer		= &zn5_timer,
	.init_machine	= zn5_init,
MACHINE_END
