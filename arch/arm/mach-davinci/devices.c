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

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/i2c.h>
#include <mach/irqs.h>
#include <mach/cputype.h>
#include <mach/mux.h>
#include <mach/edma.h>
#include <mach/mmc.h>
#include <mach/time.h>

#include "davinci.h"
#include "clock.h"

#define DAVINCI_I2C_BASE	     0x01C21000
#define DAVINCI_ATA_BASE	     0x01C66000
#define DAVINCI_MMCSD0_BASE	     0x01E10000
#define DM355_MMCSD0_BASE	     0x01E11000
#define DM355_MMCSD1_BASE	     0x01E00000
#define DM365_MMCSD0_BASE	     0x01D11000
#define DM365_MMCSD1_BASE	     0x01D00000

void __iomem  *davinci_sysmod_base;

void davinci_map_sysmod(void)
{
	davinci_sysmod_base = ioremap_nocache(DAVINCI_SYSTEM_MODULE_BASE,
					      0x800);
	/*
	 * Throw a bug since a lot of board initialization code depends
	 * on system module availability. ioremap() failing this early
	 * need careful looking into anyway.
	 */
	BUG_ON(!davinci_sysmod_base);
}

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

static struct resource ide_resources[] = {
	{
		.start		= DAVINCI_ATA_BASE,
		.end		= DAVINCI_ATA_BASE + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_IDE,
		.end		= IRQ_IDE,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 ide_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ide_device = {
	.name           = "palm_bk3710",
	.id             = -1,
	.resource       = ide_resources,
	.num_resources  = ARRAY_SIZE(ide_resources),
	.dev = {
		.dma_mask		= &ide_dma_mask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
};

void __init davinci_init_ide(void)
{
	if (cpu_is_davinci_dm644x()) {
		davinci_cfg_reg(DM644X_HPIEN_DISABLE);
		davinci_cfg_reg(DM644X_ATAEN);
		davinci_cfg_reg(DM644X_HDIREN);
	} else if (cpu_is_davinci_dm646x()) {
		/* IRQ_DM646X_IDE is the same as IRQ_IDE */
		davinci_cfg_reg(DM646X_ATAEN);
	} else {
		WARN_ON(1);
		return;
	}

	platform_device_register(&ide_device);
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
		.start = EDMA_CTLR_CHAN(0, DAVINCI_DMA_MMCRXEVT),
		.flags = IORESOURCE_DMA,
	}, {
		.start = EDMA_CTLR_CHAN(0, DAVINCI_DMA_MMCTXEVT),
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
		.start = EDMA_CTLR_CHAN(0, 30),	/* rx */
		.flags = IORESOURCE_DMA,
	}, {
		.start = EDMA_CTLR_CHAN(0, 31),	/* tx */
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
		if (cpu_is_davinci_dm355()) {
			/* REVISIT we may not need all these pins if e.g. this
			 * is a hard-wired SDIO device...
			 */
			davinci_cfg_reg(DM355_SD1_CMD);
			davinci_cfg_reg(DM355_SD1_CLK);
			davinci_cfg_reg(DM355_SD1_DATA0);
			davinci_cfg_reg(DM355_SD1_DATA1);
			davinci_cfg_reg(DM355_SD1_DATA2);
			davinci_cfg_reg(DM355_SD1_DATA3);
		} else if (cpu_is_davinci_dm365()) {
			/* Configure pull down control */
			unsigned v;

			v = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_PUPDCTL1));
			__raw_writel(v & ~0xfc0,
					DAVINCI_SYSMOD_VIRT(SYSMOD_PUPDCTL1));

			mmcsd1_resources[0].start = DM365_MMCSD1_BASE;
			mmcsd1_resources[0].end = DM365_MMCSD1_BASE +
							SZ_4K - 1;
			mmcsd1_resources[2].start = IRQ_DM365_SDIOINT1;
		} else
			break;

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
		} else if (cpu_is_davinci_dm365()) {
			mmcsd0_resources[0].start = DM365_MMCSD0_BASE;
			mmcsd0_resources[0].end = DM365_MMCSD0_BASE +
							SZ_4K - 1;
			mmcsd0_resources[2].start = IRQ_DM365_SDIOINT0;
		} else if (cpu_is_davinci_dm644x()) {
			/* REVISIT: should this be in board-init code? */
			/* Power-on 3.3V IO cells */
			__raw_writel(0,
				DAVINCI_SYSMOD_VIRT(SYSMOD_VDD3P3VPWDN));
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
		.start	= DAVINCI_WDOG_BASE,
		.end	= DAVINCI_WDOG_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device davinci_wdt_device = {
	.name		= "watchdog",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(wdt_resources),
	.resource	= wdt_resources,
};

void davinci_restart(char mode, const char *cmd)
{
	davinci_watchdog_reset(&davinci_wdt_device);
}

static void davinci_init_wdt(void)
{
	platform_device_register(&davinci_wdt_device);
}

/*-------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

struct davinci_timer_instance davinci_timer_instance[2] = {
	{
		.base		= DAVINCI_TIMER0_BASE,
		.bottom_irq	= IRQ_TINT0_TINT12,
		.top_irq	= IRQ_TINT0_TINT34,
	},
	{
		.base		= DAVINCI_TIMER1_BASE,
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

