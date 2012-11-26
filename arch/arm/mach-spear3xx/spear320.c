/*
 * arch/arm/mach-spear3xx/spear320.c
 *
 * SPEAr320 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr320: " fmt

#include <linux/amba/pl022.h>
#include <linux/amba/pl08x.h>
#include <linux/amba/serial.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/spear.h>

#define SPEAR320_UART1_BASE		UL(0xA3000000)
#define SPEAR320_UART2_BASE		UL(0xA4000000)
#define SPEAR320_SSP0_BASE		UL(0xA5000000)
#define SPEAR320_SSP1_BASE		UL(0xA6000000)

/* DMAC platform data's slave info */
struct pl08x_channel_data spear320_dma_info[] = {
	{
		.bus_id = "uart0_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart0_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp0_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp0_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c0_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c0_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "irda",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "adc",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "to_jpeg",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "from_jpeg",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp1_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp1_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp2_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp2_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c1_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c1_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c2_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2c2_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s_rx",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "rs485_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "rs485_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 1,
		.periph_buses = PL08X_AHB2,
	},
};

static struct pl022_ssp_controller spear320_ssp_data[] = {
	{
		.bus_id = 1,
		.enable_dma = 1,
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "ssp1_tx",
		.dma_rx_param = "ssp1_rx",
		.num_chipselect = 2,
	}, {
		.bus_id = 2,
		.enable_dma = 1,
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "ssp2_tx",
		.dma_rx_param = "ssp2_rx",
		.num_chipselect = 2,
	}
};

static struct amba_pl011_data spear320_uart_data[] = {
	{
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart1_tx",
		.dma_rx_param = "uart1_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart2_tx",
		.dma_rx_param = "uart2_rx",
	},
};

/* Add SPEAr310 auxdata to pass platform data */
static struct of_dev_auxdata spear320_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR3XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	OF_DEV_AUXDATA("arm,pl022", SPEAR320_SSP0_BASE, NULL,
			&spear320_ssp_data[0]),
	OF_DEV_AUXDATA("arm,pl022", SPEAR320_SSP1_BASE, NULL,
			&spear320_ssp_data[1]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR320_UART1_BASE, NULL,
			&spear320_uart_data[0]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR320_UART2_BASE, NULL,
			&spear320_uart_data[1]),
	{}
};

static void __init spear320_dt_init(void)
{
	pl080_plat_data.slave_channels = spear320_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear320_dma_info);

	of_platform_populate(NULL, of_default_bus_match_table,
			spear320_auxdata_lookup, NULL);
}

static const char * const spear320_dt_board_compat[] = {
	"st,spear320",
	"st,spear320-evb",
	"st,spear320-hmi",
	NULL,
};

struct map_desc spear320_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR320_SOC_CONFIG_BASE,
		.pfn		= __phys_to_pfn(SPEAR320_SOC_CONFIG_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
};

static void __init spear320_map_io(void)
{
	iotable_init(spear320_io_desc, ARRAY_SIZE(spear320_io_desc));
	spear3xx_map_io();
}

DT_MACHINE_START(SPEAR320_DT, "ST SPEAr320 SoC with Flattened Device Tree")
	.map_io		=	spear320_map_io,
	.init_irq	=	spear3xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.timer		=	&spear3xx_timer,
	.init_machine	=	spear320_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear320_dt_board_compat,
MACHINE_END
