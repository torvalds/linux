/*
 * linux/arch/arm/plat-omap/devices.c
 *
 * Common platform device setup/initialization for OMAP1 and OMAP2
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

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/tc.h>
#include <mach/control.h>
#include <mach/board.h>
#include <mach/mux.h>
#include <mach/gpio.h>
#include <mach/menelaus.h>
#include <mach/mcbsp.h>
#include <mach/dsp_common.h>

#if	defined(CONFIG_OMAP_DSP) || defined(CONFIG_OMAP_DSP_MODULE)

static struct dsp_platform_data dsp_pdata = {
	.kdev_list = LIST_HEAD_INIT(dsp_pdata.kdev_list),
};

static struct resource omap_dsp_resources[] = {
	{
		.name	= "dsp_mmu",
		.start	= -1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device omap_dsp_device = {
	.name		= "dsp",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(omap_dsp_resources),
	.resource	= omap_dsp_resources,
	.dev = {
		.platform_data = &dsp_pdata,
	},
};

static inline void omap_init_dsp(void)
{
	struct resource *res;
	int irq;

	if (cpu_is_omap15xx())
		irq = INT_1510_DSP_MMU;
	else if (cpu_is_omap16xx())
		irq = INT_1610_DSP_MMU;
	else if (cpu_is_omap24xx())
		irq = INT_24XX_DSP_MMU;

	res = platform_get_resource_byname(&omap_dsp_device,
					   IORESOURCE_IRQ, "dsp_mmu");
	res->start = irq;

	platform_device_register(&omap_dsp_device);
}

int dsp_kfunc_device_register(struct dsp_kfunc_device *kdev)
{
	static DEFINE_MUTEX(dsp_pdata_lock);

	spin_lock_init(&kdev->lock);

	mutex_lock(&dsp_pdata_lock);
	list_add_tail(&kdev->entry, &dsp_pdata.kdev_list);
	mutex_unlock(&dsp_pdata_lock);

	return 0;
}
EXPORT_SYMBOL(dsp_kfunc_device_register);

#else
static inline void omap_init_dsp(void) { }
#endif	/* CONFIG_OMAP_DSP */

/*-------------------------------------------------------------------------*/
#if	defined(CONFIG_KEYBOARD_OMAP) || defined(CONFIG_KEYBOARD_OMAP_MODULE)

static void omap_init_kp(void)
{
	if (machine_is_omap_h2() || machine_is_omap_h3()) {
		omap_cfg_reg(F18_1610_KBC0);
		omap_cfg_reg(D20_1610_KBC1);
		omap_cfg_reg(D19_1610_KBC2);
		omap_cfg_reg(E18_1610_KBC3);
		omap_cfg_reg(C21_1610_KBC4);

		omap_cfg_reg(G18_1610_KBR0);
		omap_cfg_reg(F19_1610_KBR1);
		omap_cfg_reg(H14_1610_KBR2);
		omap_cfg_reg(E20_1610_KBR3);
		omap_cfg_reg(E19_1610_KBR4);
		omap_cfg_reg(N19_1610_KBR5);
	} else if (machine_is_omap_perseus2() || machine_is_omap_fsample()) {
		omap_cfg_reg(E2_730_KBR0);
		omap_cfg_reg(J7_730_KBR1);
		omap_cfg_reg(E1_730_KBR2);
		omap_cfg_reg(F3_730_KBR3);
		omap_cfg_reg(D2_730_KBR4);

		omap_cfg_reg(C2_730_KBC0);
		omap_cfg_reg(D3_730_KBC1);
		omap_cfg_reg(E4_730_KBC2);
		omap_cfg_reg(F4_730_KBC3);
		omap_cfg_reg(E3_730_KBC4);
	} else if (machine_is_omap_h4()) {
		omap_cfg_reg(T19_24XX_KBR0);
		omap_cfg_reg(R19_24XX_KBR1);
		omap_cfg_reg(V18_24XX_KBR2);
		omap_cfg_reg(M21_24XX_KBR3);
		omap_cfg_reg(E5__24XX_KBR4);
		if (omap_has_menelaus()) {
			omap_cfg_reg(B3__24XX_KBR5);
			omap_cfg_reg(AA4_24XX_KBC2);
			omap_cfg_reg(B13_24XX_KBC6);
		} else {
			omap_cfg_reg(M18_24XX_KBR5);
			omap_cfg_reg(H19_24XX_KBC2);
			omap_cfg_reg(N19_24XX_KBC6);
		}
		omap_cfg_reg(R20_24XX_KBC0);
		omap_cfg_reg(M14_24XX_KBC1);
		omap_cfg_reg(V17_24XX_KBC3);
		omap_cfg_reg(P21_24XX_KBC4);
		omap_cfg_reg(L14_24XX_KBC5);
	}
}
#else
static inline void omap_init_kp(void) {}
#endif

/*-------------------------------------------------------------------------*/
#if defined(CONFIG_OMAP_MCBSP) || defined(CONFIG_OMAP_MCBSP_MODULE)

static struct platform_device **omap_mcbsp_devices;

void omap_mcbsp_register_board_cfg(struct omap_mcbsp_platform_data *config,
					int size)
{
	int i;

	if (size > OMAP_MAX_MCBSP_COUNT) {
		printk(KERN_WARNING "Registered too many McBSPs platform_data."
			" Using maximum (%d) available.\n",
			OMAP_MAX_MCBSP_COUNT);
		size = OMAP_MAX_MCBSP_COUNT;
	}

	omap_mcbsp_devices = kzalloc(size * sizeof(struct platform_device *),
				     GFP_KERNEL);
	if (!omap_mcbsp_devices) {
		printk(KERN_ERR "Could not register McBSP devices\n");
		return;
	}

	for (i = 0; i < size; i++) {
		struct platform_device *new_mcbsp;
		int ret;

		new_mcbsp = platform_device_alloc("omap-mcbsp", i + 1);
		if (!new_mcbsp)
			continue;
		new_mcbsp->dev.platform_data = &config[i];
		ret = platform_device_add(new_mcbsp);
		if (ret) {
			platform_device_put(new_mcbsp);
			continue;
		}
		omap_mcbsp_devices[i] = new_mcbsp;
	}
}

#else
void omap_mcbsp_register_board_cfg(struct omap_mcbsp_platform_data *config,
					int size)
{  }
#endif

/*-------------------------------------------------------------------------*/

#if	defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE)

#ifdef CONFIG_ARCH_OMAP24XX
#define	OMAP_MMC1_BASE		0x4809c000
#define OMAP_MMC1_INT		INT_24XX_MMC_IRQ
#else
#define	OMAP_MMC1_BASE		0xfffb7800
#define OMAP_MMC1_INT		INT_MMC
#endif
#define	OMAP_MMC2_BASE		0xfffb7c00	/* omap16xx only */

static struct omap_mmc_conf mmc1_conf;

static u64 mmc1_dmamask = 0xffffffff;

static struct resource mmc1_resources[] = {
	{
		.start		= OMAP_MMC1_BASE,
		.end		= OMAP_MMC1_BASE + 0x7f,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP_MMC1_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mmc_omap_device1 = {
	.name		= "mmci-omap",
	.id		= 1,
	.dev = {
		.dma_mask	= &mmc1_dmamask,
		.platform_data	= &mmc1_conf,
	},
	.num_resources	= ARRAY_SIZE(mmc1_resources),
	.resource	= mmc1_resources,
};

#ifdef	CONFIG_ARCH_OMAP16XX

static struct omap_mmc_conf mmc2_conf;

static u64 mmc2_dmamask = 0xffffffff;

static struct resource mmc2_resources[] = {
	{
		.start		= OMAP_MMC2_BASE,
		.end		= OMAP_MMC2_BASE + 0x7f,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= INT_1610_MMC2,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mmc_omap_device2 = {
	.name		= "mmci-omap",
	.id		= 2,
	.dev = {
		.dma_mask	= &mmc2_dmamask,
		.platform_data	= &mmc2_conf,
	},
	.num_resources	= ARRAY_SIZE(mmc2_resources),
	.resource	= mmc2_resources,
};
#endif

static void __init omap_init_mmc(void)
{
	const struct omap_mmc_config	*mmc_conf;
	const struct omap_mmc_conf	*mmc;

	/* NOTE:  assumes MMC was never (wrongly) enabled */
	mmc_conf = omap_get_config(OMAP_TAG_MMC, struct omap_mmc_config);
	if (!mmc_conf)
		return;

	/* block 1 is always available and has just one pinout option */
	mmc = &mmc_conf->mmc[0];
	if (mmc->enabled) {
		if (cpu_is_omap24xx()) {
			omap_cfg_reg(H18_24XX_MMC_CMD);
			omap_cfg_reg(H15_24XX_MMC_CLKI);
			omap_cfg_reg(G19_24XX_MMC_CLKO);
			omap_cfg_reg(F20_24XX_MMC_DAT0);
			omap_cfg_reg(F19_24XX_MMC_DAT_DIR0);
			omap_cfg_reg(G18_24XX_MMC_CMD_DIR);
		} else {
			omap_cfg_reg(MMC_CMD);
			omap_cfg_reg(MMC_CLK);
			omap_cfg_reg(MMC_DAT0);
			if (cpu_is_omap1710()) {
				omap_cfg_reg(M15_1710_MMC_CLKI);
				omap_cfg_reg(P19_1710_MMC_CMDDIR);
				omap_cfg_reg(P20_1710_MMC_DATDIR0);
			}
		}
		if (mmc->wire4) {
			if (cpu_is_omap24xx()) {
				omap_cfg_reg(H14_24XX_MMC_DAT1);
				omap_cfg_reg(E19_24XX_MMC_DAT2);
				omap_cfg_reg(D19_24XX_MMC_DAT3);
				omap_cfg_reg(E20_24XX_MMC_DAT_DIR1);
				omap_cfg_reg(F18_24XX_MMC_DAT_DIR2);
				omap_cfg_reg(E18_24XX_MMC_DAT_DIR3);
			} else {
				omap_cfg_reg(MMC_DAT1);
				/* NOTE:  DAT2 can be on W10 (here) or M15 */
				if (!mmc->nomux)
					omap_cfg_reg(MMC_DAT2);
				omap_cfg_reg(MMC_DAT3);
			}
		}
		mmc1_conf = *mmc;
		(void) platform_device_register(&mmc_omap_device1);
	}

#ifdef	CONFIG_ARCH_OMAP16XX
	/* block 2 is on newer chips, and has many pinout options */
	mmc = &mmc_conf->mmc[1];
	if (mmc->enabled) {
		if (!mmc->nomux) {
			omap_cfg_reg(Y8_1610_MMC2_CMD);
			omap_cfg_reg(Y10_1610_MMC2_CLK);
			omap_cfg_reg(R18_1610_MMC2_CLKIN);
			omap_cfg_reg(W8_1610_MMC2_DAT0);
			if (mmc->wire4) {
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
		mmc2_conf = *mmc;
		(void) platform_device_register(&mmc_omap_device2);
	}
#endif
	return;
}
#else
static inline void omap_init_mmc(void) {}
#endif

/*-------------------------------------------------------------------------*/

/* Numbering for the SPI-capable controllers when used for SPI:
 * spi		= 1
 * uwire	= 2
 * mmc1..2	= 3..4
 * mcbsp1..3	= 5..7
 */

#if defined(CONFIG_SPI_OMAP_UWIRE) || defined(CONFIG_SPI_OMAP_UWIRE_MODULE)

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

/*-------------------------------------------------------------------------*/

#if	defined(CONFIG_OMAP_WATCHDOG) || defined(CONFIG_OMAP_WATCHDOG_MODULE)

#ifdef CONFIG_ARCH_OMAP24XX
#define	OMAP_WDT_BASE		0x48022000
#else
#define	OMAP_WDT_BASE		0xfffeb000
#endif

static struct resource wdt_resources[] = {
	{
		.start		= OMAP_WDT_BASE,
		.end		= OMAP_WDT_BASE + 0x4f,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap_wdt_device = {
	.name	   = "omap_wdt",
	.id	     = -1,
	.num_resources	= ARRAY_SIZE(wdt_resources),
	.resource	= wdt_resources,
};

static void omap_init_wdt(void)
{
	(void) platform_device_register(&omap_wdt_device);
}
#else
static inline void omap_init_wdt(void) {}
#endif

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_HW_RANDOM_OMAP) || defined(CONFIG_HW_RANDOM_OMAP_MODULE)

#ifdef CONFIG_ARCH_OMAP24XX
#define	OMAP_RNG_BASE		0x480A0000
#else
#define	OMAP_RNG_BASE		0xfffe5000
#endif

static struct resource rng_resources[] = {
	{
		.start		= OMAP_RNG_BASE,
		.end		= OMAP_RNG_BASE + 0x4f,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap_rng_device = {
	.name	   = "omap_rng",
	.id	     = -1,
	.num_resources	= ARRAY_SIZE(rng_resources),
	.resource	= rng_resources,
};

static void omap_init_rng(void)
{
	(void) platform_device_register(&omap_rng_device);
}
#else
static inline void omap_init_rng(void) {}
#endif

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
 * Board-specific knowlege like creating devices or pin setup is to be
 * kept out of drivers as much as possible.  In particular, pin setup
 * may be handled by the boot loader, and drivers should expect it will
 * normally have been done by the time they're probed.
 */
static int __init omap_init_devices(void)
{
	/* please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	omap_init_dsp();
	omap_init_kp();
	omap_init_mmc();
	omap_init_uwire();
	omap_init_wdt();
	omap_init_rng();
	return 0;
}
arch_initcall(omap_init_devices);
