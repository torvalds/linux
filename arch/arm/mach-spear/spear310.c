/*
 * arch/arm/mach-spear3xx/spear310.c
 *
 * SPEAr310 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr310: " fmt

#include <linux/amba/pl08x.h>
#include <linux/amba/serial.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include "generic.h"
#include "spear.h"

#define SPEAR310_UART1_BASE		UL(0xB2000000)
#define SPEAR310_UART2_BASE		UL(0xB2080000)
#define SPEAR310_UART3_BASE		UL(0xB2100000)
#define SPEAR310_UART4_BASE		UL(0xB2180000)
#define SPEAR310_UART5_BASE		UL(0xB2200000)

/* DMAC platform data's slave info */
struct pl08x_channel_data spear310_dma_info[] = {
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
		.bus_id = "i2c_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "i2c_tx",
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
		.bus_id = "uart1_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart2_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart2_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart3_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart3_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart4_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart4_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart5_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart5_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras5_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras5_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras6_rx",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras6_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras7_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras7_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	},
};

/* uart devices plat data */
static struct amba_pl011_data spear310_uart_data[] = {
	{
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart1_tx",
		.dma_rx_param = "uart1_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart2_tx",
		.dma_rx_param = "uart2_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart3_tx",
		.dma_rx_param = "uart3_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart4_tx",
		.dma_rx_param = "uart4_rx",
	}, {
		.dma_filter = pl08x_filter_id,
		.dma_tx_param = "uart5_tx",
		.dma_rx_param = "uart5_rx",
	},
};

/* Add SPEAr310 auxdata to pass platform data */
static struct of_dev_auxdata spear310_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART1_BASE, NULL,
			&spear310_uart_data[0]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART2_BASE, NULL,
			&spear310_uart_data[1]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART3_BASE, NULL,
			&spear310_uart_data[2]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART4_BASE, NULL,
			&spear310_uart_data[3]),
	OF_DEV_AUXDATA("arm,pl011", SPEAR310_UART5_BASE, NULL,
			&spear310_uart_data[4]),
	{}
};

static void __init spear310_dt_init(void)
{
	pl080_plat_data.slave_channels = spear310_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear310_dma_info);

	of_platform_default_populate(NULL, spear310_auxdata_lookup, NULL);
}

static const char * const spear310_dt_board_compat[] = {
	"st,spear310",
	"st,spear310-evb",
	NULL,
};

static void __init spear310_map_io(void)
{
	spear3xx_map_io();
}

DT_MACHINE_START(SPEAR310_DT, "ST SPEAr310 SoC with Flattened Device Tree")
	.map_io		=	spear310_map_io,
	.init_time	=	spear3xx_timer_init,
	.init_machine	=	spear310_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear310_dt_board_compat,
MACHINE_END
