/*
 * arch/arm/mach-spear3xx/spear3xx.c
 *
 * SPEAr3XX machines common source file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr3xx: " fmt

#include <linux/amba/pl022.h>
#include <linux/amba/pl08x.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <asm/hardware/pl080.h>
#include <asm/hardware/vic.h>
#include <plat/pl080.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* ssp device registration */
struct pl022_ssp_controller pl022_plat_data = {
	.bus_id = 0,
	.enable_dma = 1,
	.dma_filter = pl08x_filter_id,
	.dma_tx_param = "ssp0_tx",
	.dma_rx_param = "ssp0_rx",
	/*
	 * This is number of spi devices that can be connected to spi. There are
	 * two type of chipselects on which slave devices can work. One is chip
	 * select provided by spi masters other is controlled through external
	 * gpio's. We can't use chipselect provided from spi master (because as
	 * soon as FIFO becomes empty, CS is disabled and transfer ends). So
	 * this number now depends on number of gpios available for spi. each
	 * slave on each master requires a separate gpio pin.
	 */
	.num_chipselect = 2,
};

/* dmac device registration */
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
};

/*
 * Following will create 16MB static virtual/physical mappings
 * PHYSICAL		VIRTUAL
 * 0xD0000000		0xFD000000
 * 0xFC000000		0xFC000000
 */
struct map_desc spear3xx_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR3XX_ICM1_2_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM1_2_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ICM3_SMI_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM3_SMI_CTRL_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear3xx_map_io(void)
{
	iotable_init(spear3xx_io_desc, ARRAY_SIZE(spear3xx_io_desc));
}

static void __init spear3xx_timer_init(void)
{
	char pclk_name[] = "pll3_48m_clk";
	struct clk *gpt_clk, *pclk;

	spear3xx_clk_init();

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

struct sys_timer spear3xx_timer = {
	.init = spear3xx_timer_init,
};

static const struct of_device_id vic_of_match[] __initconst = {
	{ .compatible = "arm,pl190-vic", .data = vic_of_init, },
	{ /* Sentinel */ }
};

void __init spear3xx_dt_init_irq(void)
{
	of_irq_init(vic_of_match);
}
