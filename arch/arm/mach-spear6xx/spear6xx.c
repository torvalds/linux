/*
 * arch/arm/mach-spear6xx/spear6xx.c
 *
 * SPEAr6XX machines common source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * Copyright 2012 Stefan Roese <sr@denx.de>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/amba/pl08x.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/hardware/pl080.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <plat/pl080.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* dmac device registration */
static struct pl08x_channel_data spear600_dma_info[] = {
	{
		.bus_id = "ssp1_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp1_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
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
		.bus_id = "uart1_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	}, {
		.bus_id = "ssp2_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ssp2_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 0,
		.periph_buses = PL08X_AHB2,
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
		.periph_buses = PL08X_AHB2,
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
	}, {
		.bus_id = "ext0_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext0_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext1_rx",
		.min_signal = 2,
		.max_signal = 2,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext1_tx",
		.min_signal = 3,
		.max_signal = 3,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext2_rx",
		.min_signal = 4,
		.max_signal = 4,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext2_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext3_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext3_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext4_rx",
		.min_signal = 8,
		.max_signal = 8,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext4_tx",
		.min_signal = 9,
		.max_signal = 9,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext5_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext5_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext6_rx",
		.min_signal = 12,
		.max_signal = 12,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext6_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext7_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ext7_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 2,
		.periph_buses = PL08X_AHB2,
	},
};

struct pl08x_platform_data pl080_plat_data = {
	.memcpy_channel = {
		.bus_id = "memcpy",
		.cctl_memcpy =
			(PL080_BSIZE_16 << PL080_CONTROL_SB_SIZE_SHIFT | \
			PL080_BSIZE_16 << PL080_CONTROL_DB_SIZE_SHIFT | \
			PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT | \
			PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT | \
			PL080_CONTROL_PROT_BUFF | PL080_CONTROL_PROT_CACHE | \
			PL080_CONTROL_PROT_SYS),
	},
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
	.get_signal = pl080_get_signal,
	.put_signal = pl080_put_signal,
	.slave_channels = spear600_dma_info,
	.num_slave_channels = ARRAY_SIZE(spear600_dma_info),
};

/*
 * Following will create 16MB static virtual/physical mappings
 * PHYSICAL		VIRTUAL
 * 0xF0000000		0xF0000000
 * 0xF1000000		0xF1000000
 * 0xD0000000		0xFD000000
 * 0xFC000000		0xFC000000
 */
struct map_desc spear6xx_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR6XX_ML_CPU_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ML_CPU_BASE),
		.length		= 2 * SZ_16M,
		.type		= MT_DEVICE
	},	{
		.virtual	= VA_SPEAR6XX_ICM1_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ICM1_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR6XX_ICM3_SMI_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ICM3_SMI_CTRL_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear6xx_map_io(void)
{
	iotable_init(spear6xx_io_desc, ARRAY_SIZE(spear6xx_io_desc));
}

void __init spear6xx_timer_init(void)
{
	char pclk_name[] = "pll3_clk";
	struct clk *gpt_clk, *pclk;

	spear6xx_clk_init();

	/* get the system timer clock */
	gpt_clk = clk_get_sys("gpt0", NULL);
	if (IS_ERR(gpt_clk)) {
		pr_err("%s:couldn't get clk for gpt\n", __func__);
		BUG();
	}

	/* get the suitable parent clock for timer*/
	pclk = clk_get(NULL, pclk_name);
	if (IS_ERR(pclk)) {
		pr_err("%s:couldn't get %s as parent for gpt\n",
				__func__, pclk_name);
		BUG();
	}

	clk_set_parent(gpt_clk, pclk);
	clk_put(gpt_clk);
	clk_put(pclk);

	spear_setup_of_timer();
}

/* Add auxdata to pass platform data */
struct of_dev_auxdata spear6xx_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,pl080", SPEAR6XX_ICM3_DMA_BASE, NULL,
			&pl080_plat_data),
	{}
};

static void __init spear600_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			spear6xx_auxdata_lookup, NULL);
}

static const char *spear600_dt_board_compat[] = {
	"st,spear600",
	NULL
};

DT_MACHINE_START(SPEAR600_DT, "ST SPEAr600 (Flattened Device Tree)")
	.map_io		=	spear6xx_map_io,
	.init_irq	=	irqchip_init,
	.init_time	=	spear6xx_timer_init,
	.init_machine	=	spear600_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear600_dt_board_compat,
MACHINE_END
