/*
 * omap_hwmod_2xxx_3xxx_ipblock_data.c - common IP block data for OMAP2/3
 *
 * Copyright (C) 2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <plat/omap_hwmod.h>
#include <plat/serial.h>

#include <mach/irqs.h>

#include "omap_hwmod_common_data.h"

struct omap_hwmod_irq_info omap2_timer1_mpu_irqs[] = {
	{ .irq = 37, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer2_mpu_irqs[] = {
	{ .irq = 38, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer3_mpu_irqs[] = {
	{ .irq = 39, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer4_mpu_irqs[] = {
	{ .irq = 40, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer5_mpu_irqs[] = {
	{ .irq = 41, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer6_mpu_irqs[] = {
	{ .irq = 42, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer7_mpu_irqs[] = {
	{ .irq = 43, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer8_mpu_irqs[] = {
	{ .irq = 44, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer9_mpu_irqs[] = {
	{ .irq = 45, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer10_mpu_irqs[] = {
	{ .irq = 46, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_timer11_mpu_irqs[] = {
	{ .irq = 47, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_uart1_mpu_irqs[] = {
	{ .irq = INT_24XX_UART1_IRQ, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_uart2_mpu_irqs[] = {
	{ .irq = INT_24XX_UART2_IRQ, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_uart3_mpu_irqs[] = {
	{ .irq = INT_24XX_UART3_IRQ, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_dispc_irqs[] = {
	{ .irq = 25 },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_i2c1_mpu_irqs[] = {
	{ .irq = INT_24XX_I2C1_IRQ, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_i2c2_mpu_irqs[] = {
	{ .irq = INT_24XX_I2C2_IRQ, },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_gpio1_irqs[] = {
	{ .irq = 29 }, /* INT_24XX_GPIO_BANK1 */
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_gpio2_irqs[] = {
	{ .irq = 30 }, /* INT_24XX_GPIO_BANK2 */
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_gpio3_irqs[] = {
	{ .irq = 31 }, /* INT_24XX_GPIO_BANK3 */
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_gpio4_irqs[] = {
	{ .irq = 32 }, /* INT_24XX_GPIO_BANK4 */
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_dma_system_irqs[] = {
	{ .name = "0", .irq = 12 }, /* INT_24XX_SDMA_IRQ0 */
	{ .name = "1", .irq = 13 }, /* INT_24XX_SDMA_IRQ1 */
	{ .name = "2", .irq = 14 }, /* INT_24XX_SDMA_IRQ2 */
	{ .name = "3", .irq = 15 }, /* INT_24XX_SDMA_IRQ3 */
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_mcspi1_mpu_irqs[] = {
	{ .irq = 65 },
	{ .irq = -1 }
};

struct omap_hwmod_irq_info omap2_mcspi2_mpu_irqs[] = {
	{ .irq = 66 },
	{ .irq = -1 }
};



