/*
 * arch/arm/mach-spear3xx/spear300.c
 *
 * SPEAr300 machine source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr300: " fmt

#include <linux/amba/pl08x.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* DMAC platform data's slave info */
struct pl08x_channel_data spear300_dma_info[] = {
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
		.bus_id = "ras0_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras0_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras1_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras1_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras2_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras2_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras3_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras3_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras4_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 1,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ras4_tx",
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

/* Add SPEAr300 auxdata to pass platform data */
static struct of_dev_auxdata spear300_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl022", SPEAR3XX_ICM1_SSP_BASE, NULL,
			&pl022_plat_data),
	OF_DEV_AUXDATA("arm,pl080", SPEAR3XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	{}
};

static void __init spear300_dt_init(void)
{
	pl080_plat_data.slave_channels = spear300_dma_info;
	pl080_plat_data.num_slave_channels = ARRAY_SIZE(spear300_dma_info);

	of_platform_populate(NULL, of_default_bus_match_table,
			spear300_auxdata_lookup, NULL);
}

static const char * const spear300_dt_board_compat[] = {
	"st,spear300",
	"st,spear300-evb",
	NULL,
};

static void __init spear300_map_io(void)
{
	spear3xx_map_io();
}

DT_MACHINE_START(SPEAR300_DT, "ST SPEAr300 SoC with Flattened Device Tree")
	.map_io		=	spear300_map_io,
	.init_irq	=	spear3xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.init_time	=	spear3xx_timer_init,
	.init_machine	=	spear300_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear300_dt_board_compat,
MACHINE_END
