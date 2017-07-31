/*
 * linux/arch/arm/mach-omap1/devices.c
 *
 * OMAP1 platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <linux/platform_data/omap-wd-timer.h>

#include <asm/mach/map.h>

#include <mach/tc.h>
#include <mach/mux.h>

#include <mach/omap7xx.h>
#include "camera.h"
#include <mach/hardware.h>

#include "common.h"
#include "clock.h"
#include "mmc.h"
#include "sram.h"

#if IS_ENABLED(CONFIG_RTC_DRV_OMAP)

#define	OMAP_RTC_BASE		0xfffb4800

static struct resource rtc_resources[] = {
	{
		.start		= OMAP_RTC_BASE,
		.end		= OMAP_RTC_BASE + 0x5f,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= INT_RTC_TIMER,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= INT_RTC_ALARM,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device omap_rtc_device = {
	.name           = "omap_rtc",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static void omap_init_rtc(void)
{
	(void) platform_device_register(&omap_rtc_device);
}
#else
static inline void omap_init_rtc(void) {}
#endif

static inline void omap_init_mbox(void) { }

/*-------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_MMC_OMAP)

static inline void omap1_mmc_mux(struct omap_mmc_platform_data *mmc_controller,
			int controller_nr)
{
	if (controller_nr == 0) {
		if (cpu_is_omap7xx()) {
			omap_cfg_reg(MMC_7XX_CMD);
			omap_cfg_reg(MMC_7XX_CLK);
			omap_cfg_reg(MMC_7XX_DAT0);
		} else {
			omap_cfg_reg(MMC_CMD);
			omap_cfg_reg(MMC_CLK);
			omap_cfg_reg(MMC_DAT0);
		}

		if (cpu_is_omap1710()) {
			omap_cfg_reg(M15_1710_MMC_CLKI);
			omap_cfg_reg(P19_1710_MMC_CMDDIR);
			omap_cfg_reg(P20_1710_MMC_DATDIR0);
		}
		if (mmc_controller->slots[0].wires == 4 && !cpu_is_omap7xx()) {
			omap_cfg_reg(MMC_DAT1);
			/* NOTE: DAT2 can be on W10 (here) or M15 */
			if (!mmc_controller->slots[0].nomux)
				omap_cfg_reg(MMC_DAT2);
			omap_cfg_reg(MMC_DAT3);
		}
	}

	/* Block 2 is on newer chips, and has many pinout options */
	if (cpu_is_omap16xx() && controller_nr == 1) {
		if (!mmc_controller->slots[1].nomux) {
			omap_cfg_reg(Y8_1610_MMC2_CMD);
			omap_cfg_reg(Y10_1610_MMC2_CLK);
			omap_cfg_reg(R18_1610_MMC2_CLKIN);
			omap_cfg_reg(W8_1610_MMC2_DAT0);
			if (mmc_controller->slots[1].wires == 4) {
				omap_cfg_reg(V8_1610_MMC2_DAT1);
				omap_cfg_reg(W15_1610_MMC2_DAT2);
				omap_cfg_reg(R10_1610_MMC2_DAT3);
			}

			/* These are needed for the level shifter */
			omap_cfg_reg(V9_1610_MMC2_CMDDIR);
			omap_cfg_reg(V5_1610_MMC2_DATDIR0);
			omap_cfg_reg(W19_1610_MMC2_DATDIR1);
		}

		/* Feedback clock must be set on OMAP-1710 MMC2 */
		if (cpu_is_omap1710())
			omap_writel(omap_readl(MOD_CONF_CTRL_1) | (1 << 24),
					MOD_CONF_CTRL_1);
	}
}

#define OMAP_MMC_NR_RES		4

/*
 * Register MMC devices.
 */
static int __init omap_mmc_add(const char *name, int id, unsigned long base,
				unsigned long size, unsigned int irq,
				unsigned rx_req, unsigned tx_req,
				struct omap_mmc_platform_data *data)
{
	struct platform_device *pdev;
	struct resource res[OMAP_MMC_NR_RES];
	int ret;

	pdev = platform_device_alloc(name, id);
	if (!pdev)
		return -ENOMEM;

	memset(res, 0, OMAP_MMC_NR_RES * sizeof(struct resource));
	res[0].start = base;
	res[0].end = base + size - 1;
	res[0].flags = IORESOURCE_MEM;
	res[1].start = res[1].end = irq;
	res[1].flags = IORESOURCE_IRQ;
	res[2].start = rx_req;
	res[2].name = "rx";
	res[2].flags = IORESOURCE_DMA;
	res[3].start = tx_req;
	res[3].name = "tx";
	res[3].flags = IORESOURCE_DMA;

	if (cpu_is_omap7xx())
		data->slots[0].features = MMC_OMAP7XX;
	if (cpu_is_omap15xx())
		data->slots[0].features = MMC_OMAP15XX;
	if (cpu_is_omap16xx())
		data->slots[0].features = MMC_OMAP16XX;

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret == 0)
		ret = platform_device_add_data(pdev, data, sizeof(*data));
	if (ret)
		goto fail;

	ret = platform_device_add(pdev);
	if (ret)
		goto fail;

	/* return device handle to board setup code */
	data->dev = &pdev->dev;
	return 0;

fail:
	platform_device_put(pdev);
	return ret;
}

void __init omap1_init_mmc(struct omap_mmc_platform_data **mmc_data,
			int nr_controllers)
{
	int i;

	for (i = 0; i < nr_controllers; i++) {
		unsigned long base, size;
		unsigned rx_req, tx_req;
		unsigned int irq = 0;

		if (!mmc_data[i])
			continue;

		omap1_mmc_mux(mmc_data[i], i);

		switch (i) {
		case 0:
			base = OMAP1_MMC1_BASE;
			irq = INT_MMC;
			rx_req = 22;
			tx_req = 21;
			break;
		case 1:
			if (!cpu_is_omap16xx())
				return;
			base = OMAP1_MMC2_BASE;
			irq = INT_1610_MMC2;
			rx_req = 55;
			tx_req = 54;
			break;
		default:
			continue;
		}
		size = OMAP1_MMC_SIZE;

		omap_mmc_add("mmci-omap", i, base, size, irq,
				rx_req, tx_req, mmc_data[i]);
	}
}

#endif

/*-------------------------------------------------------------------------*/

/* OMAP7xx SPI support */
#if IS_ENABLED(CONFIG_SPI_OMAP_100K)

struct platform_device omap_spi1 = {
	.name           = "omap1_spi100k",
	.id             = 1,
};

struct platform_device omap_spi2 = {
	.name           = "omap1_spi100k",
	.id             = 2,
};

static void omap_init_spi100k(void)
{
	omap_spi1.dev.platform_data = ioremap(OMAP7XX_SPI1_BASE, 0x7ff);
	if (omap_spi1.dev.platform_data)
		platform_device_register(&omap_spi1);

	omap_spi2.dev.platform_data = ioremap(OMAP7XX_SPI2_BASE, 0x7ff);
	if (omap_spi2.dev.platform_data)
		platform_device_register(&omap_spi2);
}

#else
static inline void omap_init_spi100k(void)
{
}
#endif


#define OMAP1_CAMERA_BASE	0xfffb6800
#define OMAP1_CAMERA_IOSIZE	0x1c

static struct resource omap1_camera_resources[] = {
	[0] = {
		.start	= OMAP1_CAMERA_BASE,
		.end	= OMAP1_CAMERA_BASE + OMAP1_CAMERA_IOSIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_CAMERA,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 omap1_camera_dma_mask = DMA_BIT_MASK(32);

static struct platform_device omap1_camera_device = {
	.name		= "omap1-camera",
	.id		= 0, /* This is used to put cameras on this interface */
	.dev		= {
		.dma_mask		= &omap1_camera_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(omap1_camera_resources),
	.resource	= omap1_camera_resources,
};

void __init omap1_camera_init(void *info)
{
	struct platform_device *dev = &omap1_camera_device;
	int ret;

	dev->dev.platform_data = info;

	ret = platform_device_register(dev);
	if (ret)
		dev_err(&dev->dev, "unable to register device: %d\n", ret);
}


/*-------------------------------------------------------------------------*/

static inline void omap_init_sti(void) {}

/* Numbering for the SPI-capable controllers when used for SPI:
 * spi		= 1
 * uwire	= 2
 * mmc1..2	= 3..4
 * mcbsp1..3	= 5..7
 */

#if IS_ENABLED(CONFIG_SPI_OMAP_UWIRE)

#define	OMAP_UWIRE_BASE		0xfffb3000

static struct resource uwire_resources[] = {
	{
		.start		= OMAP_UWIRE_BASE,
		.end		= OMAP_UWIRE_BASE + 0x20,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap_uwire_device = {
	.name	   = "omap_uwire",
	.id	     = -1,
	.num_resources	= ARRAY_SIZE(uwire_resources),
	.resource	= uwire_resources,
};

static void omap_init_uwire(void)
{
	/* FIXME define and use a boot tag; not all boards will be hooking
	 * up devices to the microwire controller, and multi-board configs
	 * mean that CONFIG_SPI_OMAP_UWIRE may be configured anyway...
	 */

	/* board-specific code must configure chipselects (only a few
	 * are normally used) and SCLK/SDI/SDO (each has two choices).
	 */
	(void) platform_device_register(&omap_uwire_device);
}
#else
static inline void omap_init_uwire(void) {}
#endif


#define OMAP1_RNG_BASE		0xfffe5000

static struct resource omap1_rng_resources[] = {
	{
		.start		= OMAP1_RNG_BASE,
		.end		= OMAP1_RNG_BASE + 0x4f,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap1_rng_device = {
	.name		= "omap_rng",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(omap1_rng_resources),
	.resource	= omap1_rng_resources,
};

static void omap1_init_rng(void)
{
	if (!cpu_is_omap16xx())
		return;

	(void) platform_device_register(&omap1_rng_device);
}

/*-------------------------------------------------------------------------*/

/*
 * This gets called after board-specific INIT_MACHINE, and initializes most
 * on-chip peripherals accessible on this board (except for few like USB):
 *
 *  (a) Does any "standard config" pin muxing needed.  Board-specific
 *	code will have muxed GPIO pins and done "nonstandard" setup;
 *	that code could live in the boot loader.
 *  (b) Populating board-specific platform_data with the data drivers
 *	rely on to handle wiring variations.
 *  (c) Creating platform devices as meaningful on this board and
 *	with this kernel configuration.
 *
 * Claiming GPIOs, and setting their direction and initial values, is the
 * responsibility of the device drivers.  So is responding to probe().
 *
 * Board-specific knowledge like creating devices or pin setup is to be
 * kept out of drivers as much as possible.  In particular, pin setup
 * may be handled by the boot loader, and drivers should expect it will
 * normally have been done by the time they're probed.
 */
static int __init omap1_init_devices(void)
{
	if (!cpu_class_is_omap1())
		return -ENODEV;

	omap_sram_init();
	omap1_clk_late_init();

	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */

	omap_init_mbox();
	omap_init_rtc();
	omap_init_spi100k();
	omap_init_sti();
	omap_init_uwire();
	omap1_init_rng();

	return 0;
}
arch_initcall(omap1_init_devices);

#if IS_ENABLED(CONFIG_OMAP_WATCHDOG)

static struct resource wdt_resources[] = {
	{
		.start		= 0xfffeb000,
		.end		= 0xfffeb07F,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap_wdt_device = {
	.name		= "omap_wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(wdt_resources),
	.resource	= wdt_resources,
};

static int __init omap_init_wdt(void)
{
	struct omap_wd_timer_platform_data pdata;
	int ret;

	if (!cpu_is_omap16xx())
		return -ENODEV;

	pdata.read_reset_sources = omap1_get_reset_sources;

	ret = platform_device_register(&omap_wdt_device);
	if (!ret) {
		ret = platform_device_add_data(&omap_wdt_device, &pdata,
					       sizeof(pdata));
		if (ret)
			platform_device_del(&omap_wdt_device);
	}

	return ret;
}
subsys_initcall(omap_init_wdt);
#endif
