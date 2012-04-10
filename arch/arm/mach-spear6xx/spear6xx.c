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

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/hardware.h>

/* Following will create static virtual/physical mappings */
static struct map_desc spear6xx_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR6XX_ICM1_UART0_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ICM1_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR6XX_CPU_VIC_PRI_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_CPU_VIC_PRI_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR6XX_CPU_VIC_SEC_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_CPU_VIC_SEC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR6XX_ICM3_SYS_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ICM3_SYS_CTRL_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR6XX_ICM3_MISC_REG_BASE,
		.pfn		= __phys_to_pfn(SPEAR6XX_ICM3_MISC_REG_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear6xx_map_io(void)
{
	iotable_init(spear6xx_io_desc, ARRAY_SIZE(spear6xx_io_desc));
}

static void __init spear6xx_timer_init(void)
{
	char pclk_name[] = "pll3_48m_clk";
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

	spear_setup_timer();
}

struct sys_timer spear6xx_timer = {
	.init = spear6xx_timer_init,
};

static void __init spear600_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *spear600_dt_board_compat[] = {
	"st,spear600",
	NULL
};

static const struct of_device_id vic_of_match[] __initconst = {
	{ .compatible = "arm,pl190-vic", .data = vic_of_init, },
	{ /* Sentinel */ }
};

static void __init spear6xx_dt_init_irq(void)
{
	of_irq_init(vic_of_match);
}

DT_MACHINE_START(SPEAR600_DT, "ST SPEAr600 (Flattened Device Tree)")
	.map_io		=	spear6xx_map_io,
	.init_irq	=	spear6xx_dt_init_irq,
	.handle_irq	=	vic_handle_irq,
	.timer		=	&spear6xx_timer,
	.init_machine	=	spear600_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear600_dt_board_compat,
MACHINE_END
