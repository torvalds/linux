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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/i2c.h>

#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>

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
		.modalias = "ab8500",
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

#define U8500_I2C_CONTROLLER(id, _slsu, _tft, _rft, clk, _sm) \
static struct nmk_i2c_controller u8500_i2c##id##_data = { \
	/*				\
	 * slave data setup time, which is	\
	 * 250 ns,100ns,10ns which is 14,6,2	\
	 * respectively for a 48 Mhz	\
	 * i2c clock			\
	 */				\
	.slsu		= _slsu,	\
	/* Tx FIFO threshold */		\
	.tft		= _tft,		\
	/* Rx FIFO threshold */		\
	.rft		= _rft,		\
	/* std. mode operation */	\
	.clk_freq	= clk,		\
	.sm		= _sm,		\
}

/*
 * The board uses 4 i2c controllers, initialize all of
 * them with slave data setup time of 250 ns,
 * Tx & Rx FIFO threshold values as 1 and standard
 * mode of operation
 */
U8500_I2C_CONTROLLER(0, 0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(1, 0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(2,	0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);
U8500_I2C_CONTROLLER(3,	0xe, 1, 1, 100000, I2C_FREQ_MODE_STANDARD);

static struct amba_device *amba_devs[] __initdata = {
	&ux500_uart0_device,
	&ux500_uart1_device,
	&ux500_uart2_device,
	&u8500_ssp0_device,
};

/* add any platform devices here - TODO */
static struct platform_device *platform_devs[] __initdata = {
	&u8500_i2c0_device,
	&ux500_i2c1_device,
	&ux500_i2c2_device,
	&ux500_i2c3_device,
};

static void __init u8500_init_machine(void)
{
	int i;

	u8500_i2c0_device.dev.platform_data = &u8500_i2c0_data;
	ux500_i2c1_device.dev.platform_data = &u8500_i2c1_data;
	ux500_i2c2_device.dev.platform_data = &u8500_i2c2_data;
	ux500_i2c3_device.dev.platform_data = &u8500_i2c3_data;

	u8500_ssp0_device.dev.platform_data = &ssp0_platform_data;

	/* Register the active AMBA devices on this board */
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);

	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

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
	.init_irq	= ux500_init_irq,
	/* we re-use nomadik timer here */
	.timer		= &ux500_timer,
	.init_machine	= u8500_init_machine,
MACHINE_END
