/*
 * arch/arm/mach-spear3xx/spear3xx.c
 *
 * SPEAr3XX machines common source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/types.h>
#include <linux/amba/pl061.h>
#include <linux/ptrace.h>
#include <linux/io.h>
#include <asm/hardware/vic.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* Add spear3xx machines common devices here */
/* gpio device registeration */
static struct pl061_platform_data gpio_plat_data = {
	.gpio_base	= 0,
	.irq_base	= SPEAR_GPIO_INT_BASE,
};

struct amba_device gpio_device = {
	.dev = {
		.init_name = "gpio",
		.platform_data = &gpio_plat_data,
	},
	.res = {
		.start = SPEAR3XX_ICM3_GPIO_BASE,
		.end = SPEAR3XX_ICM3_GPIO_BASE + SPEAR3XX_ICM3_GPIO_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_BASIC_GPIO, NO_IRQ},
};

/* uart device registeration */
struct amba_device uart_device = {
	.dev = {
		.init_name = "uart",
	},
	.res = {
		.start = SPEAR3XX_ICM1_UART_BASE,
		.end = SPEAR3XX_ICM1_UART_BASE + SPEAR3XX_ICM1_UART_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_UART, NO_IRQ},
};

/* Do spear3xx familiy common initialization part here */
void __init spear3xx_init(void)
{
	/* nothing to do for now */
}

/* This will initialize vic */
void __init spear3xx_init_irq(void)
{
	vic_init((void __iomem *)VA_SPEAR3XX_ML1_VIC_BASE, 0, ~0, 0);
}

/* Following will create static virtual/physical mappings */
struct map_desc spear3xx_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR3XX_ICM1_UART_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM1_UART_BASE),
		.length		= SPEAR3XX_ICM1_UART_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ML1_VIC_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ML1_VIC_BASE),
		.length		= SPEAR3XX_ML1_VIC_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ICM3_SYS_CTRL_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM3_SYS_CTRL_BASE),
		.length		= SPEAR3XX_ICM3_SYS_CTRL_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= VA_SPEAR3XX_ICM3_MISC_REG_BASE,
		.pfn		= __phys_to_pfn(SPEAR3XX_ICM3_MISC_REG_BASE),
		.length		= SPEAR3XX_ICM3_MISC_REG_SIZE,
		.type		= MT_DEVICE
	},
};

/* This will create static memory mapping for selected devices */
void __init spear3xx_map_io(void)
{
	iotable_init(spear3xx_io_desc, ARRAY_SIZE(spear3xx_io_desc));

	/* This will initialize clock framework */
	clk_init();
}
