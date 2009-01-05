/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx1-mx2.h>
#include <mach/board-mx27ads.h>

#include "devices.h"

/* ADS's NOR flash */
static struct physmap_flash_data mx27ads_flash_data = {
	.width = 2,
};

static struct resource mx27ads_flash_resource = {
	.start = 0xc0000000,
	.end = 0xc0000000 + 0x02000000 - 1,
	.flags = IORESOURCE_MEM,

};

static struct platform_device mx27ads_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &mx27ads_flash_data,
	},
	.num_resources = 1,
	.resource = &mx27ads_flash_resource,
};

static int mxc_uart0_pins[] = {
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS
};

static int uart_mxc_port0_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart0_pins,
			ARRAY_SIZE(mxc_uart0_pins), "UART0");
}

static int uart_mxc_port0_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart0_pins,
			ARRAY_SIZE(mxc_uart0_pins));
	return 0;
}

static int mxc_uart1_pins[] = {
	PE3_PF_UART2_CTS,
	PE4_PF_UART2_RTS,
	PE6_PF_UART2_TXD,
	PE7_PF_UART2_RXD
};

static int uart_mxc_port1_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart1_pins,
			ARRAY_SIZE(mxc_uart1_pins), "UART1");
}

static int uart_mxc_port1_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart1_pins,
			ARRAY_SIZE(mxc_uart1_pins));
	return 0;
}

static int mxc_uart2_pins[] = {
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,
	PE10_PF_UART3_CTS,
	PE11_PF_UART3_RTS
};

static int uart_mxc_port2_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart2_pins,
			ARRAY_SIZE(mxc_uart2_pins), "UART2");
}

static int uart_mxc_port2_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart2_pins,
			ARRAY_SIZE(mxc_uart2_pins));
	return 0;
}

static int mxc_uart3_pins[] = {
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD
};

static int uart_mxc_port3_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart3_pins,
			ARRAY_SIZE(mxc_uart3_pins), "UART3");
}

static int uart_mxc_port3_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart3_pins,
			ARRAY_SIZE(mxc_uart3_pins));
}

static int mxc_uart4_pins[] = {
	PB18_AF_UART5_TXD,
	PB19_AF_UART5_RXD,
	PB20_AF_UART5_CTS,
	PB21_AF_UART5_RTS
};

static int uart_mxc_port4_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart4_pins,
			ARRAY_SIZE(mxc_uart4_pins), "UART4");
}

static int uart_mxc_port4_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart4_pins,
			ARRAY_SIZE(mxc_uart4_pins));
	return 0;
}

static int mxc_uart5_pins[] = {
	PB10_AF_UART6_TXD,
	PB12_AF_UART6_CTS,
	PB11_AF_UART6_RXD,
	PB13_AF_UART6_RTS
};

static int uart_mxc_port5_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_uart5_pins,
			ARRAY_SIZE(mxc_uart5_pins), "UART5");
}

static int uart_mxc_port5_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_uart5_pins,
			ARRAY_SIZE(mxc_uart5_pins));
	return 0;
}

static struct platform_device *platform_devices[] __initdata = {
	&mx27ads_nor_mtd_device,
};

static int mxc_fec_pins[] = {
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_CLR,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN
};

static void gpio_fec_active(void)
{
	mxc_gpio_setup_multiple_pins(mxc_fec_pins,
			ARRAY_SIZE(mxc_fec_pins), "FEC");
}

static void gpio_fec_inactive(void)
{
	mxc_gpio_release_multiple_pins(mxc_fec_pins,
			ARRAY_SIZE(mxc_fec_pins));
}

static struct imxuart_platform_data uart_pdata[] = {
	{
		.init = uart_mxc_port0_init,
		.exit = uart_mxc_port0_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart_mxc_port1_init,
		.exit = uart_mxc_port1_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart_mxc_port2_init,
		.exit = uart_mxc_port2_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart_mxc_port3_init,
		.exit = uart_mxc_port3_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart_mxc_port4_init,
		.exit = uart_mxc_port4_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	}, {
		.init = uart_mxc_port5_init,
		.exit = uart_mxc_port5_exit,
		.flags = IMXUART_HAVE_RTSCTS,
	},
};

static void __init mx27ads_board_init(void)
{
	gpio_fec_active();

	mxc_register_device(&mxc_uart_device0, &uart_pdata[0]);
	mxc_register_device(&mxc_uart_device1, &uart_pdata[1]);
	mxc_register_device(&mxc_uart_device2, &uart_pdata[2]);
	mxc_register_device(&mxc_uart_device3, &uart_pdata[3]);
	mxc_register_device(&mxc_uart_device4, &uart_pdata[4]);
	mxc_register_device(&mxc_uart_device5, &uart_pdata[5]);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}

static void __init mx27ads_timer_init(void)
{
	unsigned long fref = 26000000;

	if ((__raw_readw(PBC_VERSION_REG) & CKIH_27MHZ_BIT_SET) == 0)
		fref = 27000000;

	mxc_clocks_init(fref);
	mxc_timer_init("gpt_clk.0");
}

struct sys_timer mx27ads_timer = {
	.init	= mx27ads_timer_init,
};

static struct map_desc mx27ads_io_desc[] __initdata = {
	{
		.virtual = PBC_BASE_ADDRESS,
		.pfn = __phys_to_pfn(CS4_BASE_ADDR),
		.length = SZ_1M,
		.type = MT_DEVICE,
	},
};

void __init mx27ads_map_io(void)
{
	mxc_map_io();
	iotable_init(mx27ads_io_desc, ARRAY_SIZE(mx27ads_io_desc));
}

MACHINE_START(MX27ADS, "Freescale i.MX27ADS")
	/* maintainer: Freescale Semiconductor, Inc. */
	.phys_io        = AIPI_BASE_ADDR,
	.io_pg_offst    = ((AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx27ads_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mx27ads_board_init,
	.timer          = &mx27ads_timer,
MACHINE_END

