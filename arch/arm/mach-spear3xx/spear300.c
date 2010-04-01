/*
 * arch/arm/mach-spear3xx/spear300.c
 *
 * SPEAr300 machine source file
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
#include <asm/irq.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* Add spear300 specific devices here */
/* arm gpio1 device registeration */
static struct pl061_platform_data gpio1_plat_data = {
	.gpio_base	= 8,
	.irq_base	= SPEAR_GPIO1_INT_BASE,
};

struct amba_device gpio1_device = {
	.dev = {
		.init_name = "gpio1",
		.platform_data = &gpio1_plat_data,
	},
	.res = {
		.start = SPEAR300_GPIO_BASE,
		.end = SPEAR300_GPIO_BASE + SPEAR300_GPIO_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_GEN_RAS_1, NO_IRQ},
};

void __init spear300_init(void)
{
	/* call spear3xx family common init function */
	spear3xx_init();
}
