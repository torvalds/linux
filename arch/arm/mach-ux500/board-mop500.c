/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/spi/spi.h>

#include <asm/localtimer.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/mtu.h>

#include <mach/hardware.h>
#include <mach/setup.h>

#define __MEM_4K_RESOURCE(x) \
	.res = {.start = (x), .end = (x) + SZ_4K - 1, .flags = IORESOURCE_MEM}

/* These are active devices on this board */
static struct amba_device uart0_device = {
	.dev = { .init_name = "uart0" },
	__MEM_4K_RESOURCE(U8500_UART0_BASE),
	.irq = {IRQ_UART0, NO_IRQ},
};

static struct amba_device uart1_device = {
	.dev = { .init_name = "uart1" },
	__MEM_4K_RESOURCE(U8500_UART1_BASE),
	.irq = {IRQ_UART1, NO_IRQ},
};

static struct amba_device uart2_device = {
	.dev = { .init_name = "uart2" },
	__MEM_4K_RESOURCE(U8500_UART2_BASE),
	.irq = {IRQ_UART2, NO_IRQ},
};

static void ab4500_spi_cs_control(u32 command)
{
	/* set the FRM signal, which is CS  - TODO */
}

struct pl022_config_chip ab4500_chip_info = {
	.lbm = LOOPBACK_DISABLED,
	.com_mode = INTERRUPT_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	/* we can act as master only */
	.hierarchy = SSP_MASTER,
	.slave_tx_disable = 0,
	.endian_rx = SSP_RX_MSB,
	.endian_tx = SSP_TX_MSB,
	.data_size = SSP_DATA_BITS_24,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_1_OR_MORE_EMPTY_LOC,
	.clk_phase = SSP_CLK_SECOND_EDGE,
	.clk_pol = SSP_CLK_POL_IDLE_HIGH,
	.cs_control = ab4500_spi_cs_control,
};

static struct spi_board_info u8500_spi_devices[] = {
	{
		.modalias = "ab4500",
		.controller_data = &ab4500_chip_info,
		.max_speed_hz = 12000000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.irq = IRQ_AB4500,
	},
};

static struct pl022_ssp_controller ssp0_platform_data = {
	.bus_id = 0,
	/* pl022 not yet supports dma */
	.enable_dma = 0,
	/* on this platform, gpio 31,142,144,214 &
	 * 224 are connected as chip selects
	 */
	.num_chipselect = 5,
};

static struct amba_device pl022_device = {
	.dev = {
		.coherent_dma_mask = ~0,
		.init_name = "pl022",
		.platform_data = &ssp0_platform_data,
	},
	.res = {
		.start = U8500_SSP0_BASE,
		.end   = U8500_SSP0_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_SSP0, NO_IRQ },
	/* ST-Ericsson modified id */
	.periphid = SSP_PER_ID,
};

static struct amba_device *amba_devs[] __initdata = {
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&pl022_device,
};

static void __init u8500_timer_init(void)
{
#ifdef CONFIG_LOCAL_TIMERS
	/* Setup the local timer base */
	twd_base = __io_address(U8500_TWD_BASE);
#endif
	/* Setup the MTU base */
	mtu_base = __io_address(U8500_MTU0_BASE);

	nmdk_timer_init();
}

static struct sys_timer u8500_timer = {
	.init	= u8500_timer_init,
};

static void __init u8500_init_machine(void)
{
	int i;

	/* Register the active AMBA devices on this board */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);

	spi_register_board_info(u8500_spi_devices,
			ARRAY_SIZE(u8500_spi_devices));

	u8500_init_devices();
}

MACHINE_START(U8500, "ST-Ericsson MOP500 platform")
	/* Maintainer: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com> */
	.phys_io	= U8500_UART2_BASE,
	.io_pg_offst	= (IO_ADDRESS(U8500_UART2_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x100,
	.map_io		= u8500_map_io,
	.init_irq	= u8500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &u8500_timer,
	.init_machine	= u8500_init_machine,
MACHINE_END
