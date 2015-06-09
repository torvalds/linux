/*
 * linux/arch/arm/mach-omap2/devices.c
 *
 * OMAP2 platform device setup/initialization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/mailbox-omap.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <linux/omap-dma.h>

#include "iomap.h"
#include "omap_hwmod.h"
#include "omap_device.h"

#include "soc.h"
#include "common.h"
#include "mux.h"
#include "control.h"
#include "devices.h"
#include "display.h"

#define L3_MODULES_MAX_LEN 12
#define L3_MODULES 3

static int __init omap3_l3_init(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	char oh_name[L3_MODULES_MAX_LEN];

	/*
	 * To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!(cpu_is_omap34xx()) || of_have_populated_dt())
		return -ENODEV;

	snprintf(oh_name, L3_MODULES_MAX_LEN, "l3_main");

	oh = omap_hwmod_lookup(oh_name);

	if (!oh)
		pr_err("could not look up %s\n", oh_name);

	pdev = omap_device_build("omap_l3_smx", 0, oh, NULL, 0);

	WARN(IS_ERR(pdev), "could not build omap_device for %s\n", oh_name);

	return PTR_RET(pdev);
}
omap_postcore_initcall(omap3_l3_init);

#if defined(CONFIG_IOMMU_API)

#include <linux/platform_data/iommu-omap.h>

static struct resource omap3isp_resources[] = {
	{
		.start		= OMAP3430_ISP_BASE,
		.end		= OMAP3430_ISP_BASE + 0x12fc,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= OMAP3430_ISP_BASE2,
		.end		= OMAP3430_ISP_BASE2 + 0x0600,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 24 + OMAP_INTC_START,
		.flags		= IORESOURCE_IRQ,
	}
};

static struct platform_device omap3isp_device = {
	.name		= "omap3isp",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(omap3isp_resources),
	.resource	= omap3isp_resources,
};

static struct omap_iommu_arch_data omap3_isp_iommu = {
	.name = "mmu_isp",
};

int omap3_init_camera(struct isp_platform_data *pdata)
{
	if (of_have_populated_dt())
		omap3_isp_iommu.name = "480bd400.mmu";

	omap3isp_device.dev.platform_data = pdata;
	omap3isp_device.dev.archdata.iommu = &omap3_isp_iommu;

	return platform_device_register(&omap3isp_device);
}

#else /* !CONFIG_IOMMU_API */

int omap3_init_camera(struct isp_platform_data *pdata)
{
	return 0;
}

#endif

#if defined(CONFIG_OMAP2PLUS_MBOX) || defined(CONFIG_OMAP2PLUS_MBOX_MODULE)
static inline void __init omap_init_mbox(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	struct omap_mbox_pdata *pdata;

	oh = omap_hwmod_lookup("mailbox");
	if (!oh) {
		pr_err("%s: unable to find hwmod\n", __func__);
		return;
	}
	if (!oh->dev_attr) {
		pr_err("%s: hwmod doesn't have valid attrs\n", __func__);
		return;
	}

	pdata = (struct omap_mbox_pdata *)oh->dev_attr;
	pdev = omap_device_build("omap-mailbox", -1, oh, pdata, sizeof(*pdata));
	WARN(IS_ERR(pdev), "%s: could not build device, err %ld\n",
						__func__, PTR_ERR(pdev));
}
#else
static inline void omap_init_mbox(void) { }
#endif /* CONFIG_OMAP2PLUS_MBOX */

static inline void omap_init_sti(void) {}

#if defined(CONFIG_SND_SOC) || defined(CONFIG_SND_SOC_MODULE)

static struct platform_device omap_pcm = {
	.name	= "omap-pcm-audio",
	.id	= -1,
};

static void omap_init_audio(void)
{
	platform_device_register(&omap_pcm);
}

#else
static inline void omap_init_audio(void) {}
#endif

#if defined(CONFIG_SPI_OMAP24XX) || defined(CONFIG_SPI_OMAP24XX_MODULE)

#include <linux/platform_data/spi-omap2-mcspi.h>

static int __init omap_mcspi_init(struct omap_hwmod *oh, void *unused)
{
	struct platform_device *pdev;
	char *name = "omap2_mcspi";
	struct omap2_mcspi_platform_config *pdata;
	static int spi_num;
	struct omap2_mcspi_dev_attr *mcspi_attrib = oh->dev_attr;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("Memory allocation for McSPI device failed\n");
		return -ENOMEM;
	}

	pdata->num_cs = mcspi_attrib->num_chipselect;
	switch (oh->class->rev) {
	case OMAP2_MCSPI_REV:
	case OMAP3_MCSPI_REV:
			pdata->regs_offset = 0;
			break;
	case OMAP4_MCSPI_REV:
			pdata->regs_offset = OMAP4_MCSPI_REG_OFFSET;
			break;
	default:
			pr_err("Invalid McSPI Revision value\n");
			kfree(pdata);
			return -EINVAL;
	}

	spi_num++;
	pdev = omap_device_build(name, spi_num, oh, pdata, sizeof(*pdata));
	WARN(IS_ERR(pdev), "Can't build omap_device for %s:%s\n",
				name, oh->name);
	kfree(pdata);
	return 0;
}

static void omap_init_mcspi(void)
{
	omap_hwmod_for_each_by_class("mcspi", omap_mcspi_init, NULL);
}

#else
static inline void omap_init_mcspi(void) {}
#endif

/**
 * omap_init_rng - bind the RNG hwmod to the RNG omap_device
 *
 * Bind the RNG hwmod to the RNG omap_device.  No return value.
 */
static void omap_init_rng(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;

	oh = omap_hwmod_lookup("rng");
	if (!oh)
		return;

	pdev = omap_device_build("omap_rng", -1, oh, NULL, 0);
	WARN(IS_ERR(pdev), "Can't build omap_device for omap_rng\n");
}

static void __init omap_init_sham(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;

	oh = omap_hwmod_lookup("sham");
	if (!oh)
		return;

	pdev = omap_device_build("omap-sham", -1, oh, NULL, 0);
	WARN(IS_ERR(pdev), "Can't build omap_device for omap-sham\n");
}

static void __init omap_init_aes(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;

	oh = omap_hwmod_lookup("aes");
	if (!oh)
		return;

	pdev = omap_device_build("omap-aes", -1, oh, NULL, 0);
	WARN(IS_ERR(pdev), "Can't build omap_device for omap-aes\n");
}

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_VIDEO_OMAP2_VOUT) || \
	defined(CONFIG_VIDEO_OMAP2_VOUT_MODULE)
#if defined(CONFIG_FB_OMAP2) || defined(CONFIG_FB_OMAP2_MODULE)
static struct resource omap_vout_resource[3 - CONFIG_FB_OMAP2_NUM_FBS] = {
};
#else
static struct resource omap_vout_resource[2] = {
};
#endif

static struct platform_device omap_vout_device = {
	.name		= "omap_vout",
	.num_resources	= ARRAY_SIZE(omap_vout_resource),
	.resource 	= &omap_vout_resource[0],
	.id		= -1,
};

int __init omap_init_vout(void)
{
	return platform_device_register(&omap_vout_device);
}
#else
int __init omap_init_vout(void) { return 0; }
#endif

/*-------------------------------------------------------------------------*/

static int __init omap2_init_devices(void)
{
	/* Enable dummy states for those platforms without pinctrl support */
	if (!of_have_populated_dt())
		pinctrl_provide_dummies();

	/*
	 * please keep these calls, and their implementations above,
	 * in alphabetical order so they're easier to sort through.
	 */
	omap_init_audio();
	/* If dtb is there, the devices will be created dynamically */
	if (!of_have_populated_dt()) {
		omap_init_mbox();
		omap_init_mcspi();
		omap_init_sham();
		omap_init_aes();
		omap_init_rng();
	}
	omap_init_sti();

	return 0;
}
omap_arch_initcall(omap2_init_devices);

static int __init omap_gpmc_init(void)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	char *oh_name = "gpmc";

	/*
	 * if the board boots up with a populated DT, do not
	 * manually add the device from this initcall
	 */
	if (of_have_populated_dt())
		return -ENODEV;

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		return -ENODEV;
	}

	pdev = omap_device_build("omap-gpmc", -1, oh, NULL, 0);
	WARN(IS_ERR(pdev), "could not build omap_device for %s\n", oh_name);

	return PTR_RET(pdev);
}
omap_postcore_initcall(omap_gpmc_init);
