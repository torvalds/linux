/*
 * mach-davinci/devices.c
 *
 * DaVinci platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/i2c.h>
#include <mach/irqs.h>
#include <mach/cputype.h>
#include <mach/mux.h>
#include <mach/edma.h>
#include <mach/mmc.h>
#include <mach/time.h>

#define DAVINCI_I2C_BASE	     0x01C21000
#define DAVINCI_MMCSD0_BASE	     0x01E10000
#define DM355_MMCSD0_BASE	     0x01E11000
#define DM355_MMCSD1_BASE	     0x01E00000

static struct resource i2c_resources[] = {
	{
		.start		= DAVINCI_I2C_BASE,
		.end		= DAVINCI_I2C_BASE + 0x40,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_I2C,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device davinci_i2c_device = {
	.name           = "i2c_davinci",
	.id             = 1,
	.num_resources	= ARRAY_SIZE(i2c_resources),
	.resource	= i2c_resources,
};

void __init davinci_init_i2c(struct davinci_i2c_platform_data *pdata)
{
	if (cpu_is_davinci_dm644x())
		davinci_cfg_reg(DM644X_I2C);

	davinci_i2c_device.dev.platform_data = pdata;
	(void) platform_device_register(&davinci_i2c_device);
}

#if	defined(CONFIG_MMC_DAVINCI) || defined(CONFIG_MMC_DAVINCI_MODULE)

static u64 mmcsd0_dma_mask = DMA_BIT_MASK(32);

static struct resource mmcsd0_resources[] = {
	{
		/* different on dm355 */
		.start = DAVINCI_MMCSD0_BASE,
		.end   = DAVINCI_MMCSD0_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	/* IRQs:  MMC/SD, then SDIO */
	{
		.start = IRQ_MMCINT,
		.flags = IORESOURCE_IRQ,
	}, {
		/* different on dm355 */
		.start = IRQ_SDIOINT,
		.flags = IORESOURCE_IRQ,
	},
	/* DMA channels: RX, then TX */
	{
		.start = DAVINCI_DMA_MMCRXEVT,
		.flags = IORESOURCE_DMA,
	}, {
		.start = DAVINCI_DMA_MMCTXEVT,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device davinci_mmcsd0_device = {
	.name = "davinci_mmc",
	.id = 0,
	.dev = {
		.dma_mask = &mmcsd0_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = ARRAY_SIZE(mmcsd0_resources),
	.resource = mmcsd0_resources,
};

static u64 mmcsd1_dma_mask = DMA_BIT_MASK(32);

static struct resource mmcsd1_resources[] = {
	{
		.start = DM355_MMCSD1_BASE,
		.end   = DM355_MMCSD1_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	/* IRQs:  MMC/SD, then SDIO */
	{
		.start = IRQ_DM355_MMCINT1,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DM355_SDIOINT1,
		.flags = IORESOURCE_IRQ,
	},
	/* DMA channels: RX, then TX */
	{
		.start = 30,	/* rx */
		.flags = IORESOURCE_DMA,
	}, {
		.start = 31,	/* tx */
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device davinci_mmcsd1_device = {
	.name = "davinci_mmc",
	.id = 1,
	.dev = {
		.dma_mask = &mmcsd1_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = ARRAY_SIZE(mmcsd1_resources),
	.resource = mmcsd1_resources,
};


void __init davinci_setup_mmc(int module, struct davinci_mmc_config *config)
{
	struct platform_device	*pdev = NULL;

	if (WARN_ON(cpu_is_davinci_dm646x()))
		return;

	/* REVISIT: update PINMUX, ARM_IRQMUX, and EDMA_EVTMUX here too;
	 * for example if MMCSD1 is used for SDIO, maybe DAT2 is unused.
	 *
	 * FIXME dm6441 (no MMC/SD), dm357 (one), and dm335 (two) are
	 * not handled right here ...
	 */
	switch (module) {
	case 1:
		if (!cpu_is_davinci_dm355())
			break;

		/* REVISIT we may not need all these pins if e.g. this
		 * is a hard-wired SDIO device...
		 */
		davinci_cfg_reg(DM355_SD1_CMD);
		davinci_cfg_reg(DM355_SD1_CLK);
		davinci_cfg_reg(DM355_SD1_DATA0);
		davinci_cfg_reg(DM355_SD1_DATA1);
		davinci_cfg_reg(DM355_SD1_DATA2);
		davinci_cfg_reg(DM355_SD1_DATA3);

		pdev = &davinci_mmcsd1_device;
		break;
	case 0:
		if (cpu_is_davinci_dm355()) {
			mmcsd0_resources[0].start = DM355_MMCSD0_BASE;
			mmcsd0_resources[0].end = DM355_MMCSD0_BASE + SZ_4K - 1;
			mmcsd0_resources[2].start = IRQ_DM355_SDIOINT0;

			/* expose all 6 MMC0 signals:  CLK, CMD, DATA[0..3] */
			davinci_cfg_reg(DM355_MMCSD0);

			/* enable RX EDMA */
			davinci_cfg_reg(DM355_EVT26_MMC0_RX);
		}

		else if (cpu_is_davinci_dm644x()) {
			/* REVISIT: should this be in board-init code? */
			void __iomem *base =
				IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE);

			/* Power-on 3.3V IO cells */
			__raw_writel(0, base + DM64XX_VDD3P3V_PWDN);
			/*Set up the pull regiter for MMC */
			davinci_cfg_reg(DM644X_MSTK);
		}

		pdev = &davinci_mmcsd0_device;
		break;
	}

	if (WARN_ON(!pdev))
		return;

	pdev->dev.platform_data = config;
	platform_device_register(pdev);
}

#else

void __init davinci_setup_mmc(int module, struct davinci_mmc_config *config)
{
}

#endif

/*-------------------------------------------------------------------------*/

static struct resource wdt_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device davinci_wdt_device = {
	.name		= "watchdog",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(wdt_resources),
	.resource	= wdt_resources,
};

static void davinci_init_wdt(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	wdt_resources[0].start = (resource_size_t)soc_info->wdt_base;
	wdt_resources[0].end = (resource_size_t)soc_info->wdt_base + SZ_1K - 1;

	platform_device_register(&davinci_wdt_device);
}

/*-------------------------------------------------------------------------*/

struct davinci_timer_instance davinci_timer_instance[2] = {
	{
		.base		= IO_ADDRESS(DAVINCI_TIMER0_BASE),
		.bottom_irq	= IRQ_TINT0_TINT12,
		.top_irq	= IRQ_TINT0_TINT34,
	},
	{
		.base		= IO_ADDRESS(DAVINCI_TIMER1_BASE),
		.bottom_irq	= IRQ_TINT1_TINT12,
		.top_irq	= IRQ_TINT1_TINT34,
	},
};

/*-------------------------------------------------------------------------*/

static int __init davinci_init_devices(void)
{
	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	davinci_init_wdt();

	return 0;
}
arch_initcall(davinci_init_devices);

