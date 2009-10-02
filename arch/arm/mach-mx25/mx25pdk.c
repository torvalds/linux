#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/mx25.h>
#include <mach/mxc_nand.h>
#include "devices.h"
#include <mach/iomux-v3.h>

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static void __init mx25pdk_init(void)
{
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_usbh2, NULL);
}

static void __init mx25pdk_timer_init(void)
{
	mx25_clocks_init(26000000);
}

static struct sys_timer mx25pdk_timer = {
	.init   = mx25pdk_timer_init,
};

MACHINE_START(MX25_3DS, "Freescale MX25PDK (3DS)")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.phys_io	= MX25_AIPS1_BASE_ADDR,
	.io_pg_offst	= ((MX25_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx25_map_io,
	.init_irq       = mx25_init_irq,
	.init_machine   = mx25pdk_init,
	.timer          = &mx25pdk_timer,
MACHINE_END

