/*
 *  linux/arch/arm/mach-pxa/generic.c
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * Code common to all PXA machines.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Since this file should be linked before any other machine specific file,
 * the __initcall() here will be executed first.  This serves as default
 * initialization stuff for PXA machines which can be overridden later if
 * need be.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/gpio.h>
#include <asm/arch/udc.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>
#include <asm/arch/irda.h>
#include <asm/arch/i2c.h>

#include "devices.h"
#include "generic.h"

/*
 * Get the clock frequency as reflected by CCCR and the turbo flag.
 * We assume these values have been applied via a fcs.
 * If info is not 0 we also display the current settings.
 */
unsigned int get_clk_frequency_khz(int info)
{
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		return pxa25x_get_clk_frequency_khz(info);
	else if (cpu_is_pxa27x())
		return pxa27x_get_clk_frequency_khz(info);
	else
		return pxa3xx_get_clk_frequency_khz(info);
}
EXPORT_SYMBOL(get_clk_frequency_khz);

/*
 * Return the current memory clock frequency in units of 10kHz
 */
unsigned int get_memclk_frequency_10khz(void)
{
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		return pxa25x_get_memclk_frequency_10khz();
	else if (cpu_is_pxa27x())
		return pxa27x_get_memclk_frequency_10khz();
	else
		return pxa3xx_get_memclk_frequency_10khz();
}
EXPORT_SYMBOL(get_memclk_frequency_10khz);

/*
 * Handy function to set GPIO alternate functions
 */
int pxa_last_gpio;

int pxa_gpio_mode(int gpio_mode)
{
	unsigned long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	local_irq_save(flags);
	if (gpio_mode & GPIO_DFLT_LOW)
		GPCR(gpio) = GPIO_bit(gpio);
	else if (gpio_mode & GPIO_DFLT_HIGH)
		GPSR(gpio) = GPIO_bit(gpio);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(pxa_gpio_mode);

int gpio_direction_input(unsigned gpio)
{
	unsigned long flags;
	u32 mask;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	mask = GPIO_bit(gpio);
	local_irq_save(flags);
	GPDR(gpio) &= ~mask;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;
	u32 mask;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	mask = GPIO_bit(gpio);
	local_irq_save(flags);
	if (value)
		GPSR(gpio) = mask;
	else
		GPCR(gpio) = mask;
	GPDR(gpio) |= mask;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

/*
 * Return GPIO level
 */
int pxa_gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

EXPORT_SYMBOL(pxa_gpio_get_value);

/*
 * Set output GPIO level
 */
void pxa_gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

EXPORT_SYMBOL(pxa_gpio_set_value);

/*
 * Routine to safely enable or disable a clock in the CKEN
 */
void __pxa_set_cken(int clock, int enable)
{
	unsigned long flags;
	local_irq_save(flags);

	if (enable)
		CKEN |= (1 << clock);
	else
		CKEN &= ~(1 << clock);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(__pxa_set_cken);

/*
 * Intel PXA2xx internal register mapping.
 *
 * Note 1: not all PXA2xx variants implement all those addresses.
 *
 * Note 2: virtual 0xfffe0000-0xffffffff is reserved for the vector table
 *         and cache flush area.
 */
static struct map_desc standard_io_desc[] __initdata = {
  	{	/* Devs */
		.virtual	=  0xf2000000,
		.pfn		= __phys_to_pfn(0x40000000),
		.length		= 0x02000000,
		.type		= MT_DEVICE
	}, {	/* LCD */
		.virtual	=  0xf4000000,
		.pfn		= __phys_to_pfn(0x44000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Mem Ctl */
		.virtual	=  0xf6000000,
		.pfn		= __phys_to_pfn(0x48000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* USB host */
		.virtual	=  0xf8000000,
		.pfn		= __phys_to_pfn(0x4c000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* Camera */
		.virtual	=  0xfa000000,
		.pfn		= __phys_to_pfn(0x50000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* IMem ctl */
		.virtual	=  0xfe000000,
		.pfn		= __phys_to_pfn(0x58000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* UNCACHED_PHYS_0 */
		.virtual	= 0xff000000,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}
};

void __init pxa_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
	get_clk_frequency_khz(1);
}


void __init pxa_register_device(struct platform_device *dev, void *data)
{
	int ret;

	dev->dev.platform_data = data;

	ret = platform_device_register(dev);
	if (ret)
		dev_err(&dev->dev, "unable to register device: %d\n", ret);
}


static struct resource pxamci_resources[] = {
	[0] = {
		.start	= 0x41100000,
		.end	= 0x41100fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MMC,
		.end	= IRQ_MMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 pxamci_dmamask = 0xffffffffUL;

struct platform_device pxa_device_mci = {
	.name		= "pxa2xx-mci",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxamci_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxamci_resources),
	.resource	= pxamci_resources,
};

void __init pxa_set_mci_info(struct pxamci_platform_data *info)
{
	pxa_register_device(&pxa_device_mci, info);
}


static struct pxa2xx_udc_mach_info pxa_udc_info;

void __init pxa_set_udc_info(struct pxa2xx_udc_mach_info *info)
{
	memcpy(&pxa_udc_info, info, sizeof *info);
}

static struct resource pxa2xx_udc_resources[] = {
	[0] = {
		.start	= 0x40600000,
		.end	= 0x4060ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB,
		.end	= IRQ_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 udc_dma_mask = ~(u32)0;

struct platform_device pxa_device_udc = {
	.name		= "pxa2xx-udc",
	.id		= -1,
	.resource	= pxa2xx_udc_resources,
	.num_resources	= ARRAY_SIZE(pxa2xx_udc_resources),
	.dev		=  {
		.platform_data	= &pxa_udc_info,
		.dma_mask	= &udc_dma_mask,
	}
};

static struct resource pxafb_resources[] = {
	[0] = {
		.start	= 0x44000000,
		.end	= 0x4400ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_LCD,
		.end	= IRQ_LCD,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 fb_dma_mask = ~(u64)0;

struct platform_device pxa_device_fb = {
	.name		= "pxa2xx-fb",
	.id		= -1,
	.dev		= {
		.dma_mask	= &fb_dma_mask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxafb_resources),
	.resource	= pxafb_resources,
};

void __init set_pxa_fb_info(struct pxafb_mach_info *info)
{
	pxa_register_device(&pxa_device_fb, info);
}

void __init set_pxa_fb_parent(struct device *parent_dev)
{
	pxa_device_fb.dev.parent = parent_dev;
}

static struct resource pxa_resource_ffuart[] = {
	{
		.start	= __PREG(FFUART),
		.end	= __PREG(FFUART) + 35,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_FFUART,
		.end	= IRQ_FFUART,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device pxa_device_ffuart= {
	.name		= "pxa2xx-uart",
	.id		= 0,
	.resource	= pxa_resource_ffuart,
	.num_resources	= ARRAY_SIZE(pxa_resource_ffuart),
};

static struct resource pxa_resource_btuart[] = {
	{
		.start	= __PREG(BTUART),
		.end	= __PREG(BTUART) + 35,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_BTUART,
		.end	= IRQ_BTUART,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device pxa_device_btuart = {
	.name		= "pxa2xx-uart",
	.id		= 1,
	.resource	= pxa_resource_btuart,
	.num_resources	= ARRAY_SIZE(pxa_resource_btuart),
};

static struct resource pxa_resource_stuart[] = {
	{
		.start	= __PREG(STUART),
		.end	= __PREG(STUART) + 35,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_STUART,
		.end	= IRQ_STUART,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device pxa_device_stuart = {
	.name		= "pxa2xx-uart",
	.id		= 2,
	.resource	= pxa_resource_stuart,
	.num_resources	= ARRAY_SIZE(pxa_resource_stuart),
};

static struct resource pxa_resource_hwuart[] = {
	{
		.start	= __PREG(HWUART),
		.end	= __PREG(HWUART) + 47,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_HWUART,
		.end	= IRQ_HWUART,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device pxa_device_hwuart = {
	.name		= "pxa2xx-uart",
	.id		= 3,
	.resource	= pxa_resource_hwuart,
	.num_resources	= ARRAY_SIZE(pxa_resource_hwuart),
};

static struct resource pxai2c_resources[] = {
	{
		.start	= 0x40301680,
		.end	= 0x403016a3,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_I2C,
		.end	= IRQ_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa_device_i2c = {
	.name		= "pxa2xx-i2c",
	.id		= 0,
	.resource	= pxai2c_resources,
	.num_resources	= ARRAY_SIZE(pxai2c_resources),
};

void __init pxa_set_i2c_info(struct i2c_pxa_platform_data *info)
{
	pxa_register_device(&pxa_device_i2c, info);
}

static struct resource pxai2s_resources[] = {
	{
		.start	= 0x40400000,
		.end	= 0x40400083,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_I2S,
		.end	= IRQ_I2S,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa_device_i2s = {
	.name		= "pxa2xx-i2s",
	.id		= -1,
	.resource	= pxai2s_resources,
	.num_resources	= ARRAY_SIZE(pxai2s_resources),
};

static u64 pxaficp_dmamask = ~(u32)0;

struct platform_device pxa_device_ficp = {
	.name		= "pxa2xx-ir",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxaficp_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
};

void __init pxa_set_ficp_info(struct pxaficp_platform_data *info)
{
	pxa_register_device(&pxa_device_ficp, info);
}

struct platform_device pxa_device_rtc = {
	.name		= "sa1100-rtc",
	.id		= -1,
};

#ifdef CONFIG_PXA25x

static u64 pxa25x_ssp_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa25x_resource_ssp[] = {
	[0] = {
		.start	= 0x41000000,
		.end	= 0x4100001f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP,
		.end	= IRQ_SSP,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 13,
		.end	= 13,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 14,
		.end	= 14,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa25x_device_ssp = {
	.name		= "pxa25x-ssp",
	.id		= 0,
	.dev		= {
		.dma_mask = &pxa25x_ssp_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa25x_resource_ssp,
	.num_resources	= ARRAY_SIZE(pxa25x_resource_ssp),
};

static u64 pxa25x_nssp_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa25x_resource_nssp[] = {
	[0] = {
		.start	= 0x41400000,
		.end	= 0x4140002f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_NSSP,
		.end	= IRQ_NSSP,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 15,
		.end	= 15,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 16,
		.end	= 16,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa25x_device_nssp = {
	.name		= "pxa25x-nssp",
	.id		= 1,
	.dev		= {
		.dma_mask = &pxa25x_nssp_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa25x_resource_nssp,
	.num_resources	= ARRAY_SIZE(pxa25x_resource_nssp),
};

static u64 pxa25x_assp_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa25x_resource_assp[] = {
	[0] = {
		.start	= 0x41500000,
		.end	= 0x4150002f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_ASSP,
		.end	= IRQ_ASSP,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 23,
		.end	= 23,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 24,
		.end	= 24,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa25x_device_assp = {
	/* ASSP is basically equivalent to NSSP */
	.name		= "pxa25x-nssp",
	.id		= 2,
	.dev		= {
		.dma_mask = &pxa25x_assp_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa25x_resource_assp,
	.num_resources	= ARRAY_SIZE(pxa25x_resource_assp),
};
#endif /* CONFIG_PXA25x */

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)

static u64 pxa27x_ssp1_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa27x_resource_ssp1[] = {
	[0] = {
		.start	= 0x41000000,
		.end	= 0x4100003f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP,
		.end	= IRQ_SSP,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 13,
		.end	= 13,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 14,
		.end	= 14,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa27x_device_ssp1 = {
	.name		= "pxa27x-ssp",
	.id		= 0,
	.dev		= {
		.dma_mask = &pxa27x_ssp1_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa27x_resource_ssp1,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_ssp1),
};

static u64 pxa27x_ssp2_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa27x_resource_ssp2[] = {
	[0] = {
		.start	= 0x41700000,
		.end	= 0x4170003f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP2,
		.end	= IRQ_SSP2,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 15,
		.end	= 15,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 16,
		.end	= 16,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa27x_device_ssp2 = {
	.name		= "pxa27x-ssp",
	.id		= 1,
	.dev		= {
		.dma_mask = &pxa27x_ssp2_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa27x_resource_ssp2,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_ssp2),
};

static u64 pxa27x_ssp3_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa27x_resource_ssp3[] = {
	[0] = {
		.start	= 0x41900000,
		.end	= 0x4190003f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP3,
		.end	= IRQ_SSP3,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 66,
		.end	= 66,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 67,
		.end	= 67,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa27x_device_ssp3 = {
	.name		= "pxa27x-ssp",
	.id		= 2,
	.dev		= {
		.dma_mask = &pxa27x_ssp3_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa27x_resource_ssp3,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_ssp3),
};
#endif /* CONFIG_PXA27x || CONFIG_PXA3xx */

#ifdef CONFIG_PXA3xx
static u64 pxa3xx_ssp4_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa3xx_resource_ssp4[] = {
	[0] = {
		.start	= 0x41a00000,
		.end	= 0x41a0003f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SSP4,
		.end	= IRQ_SSP4,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for RX */
		.start	= 2,
		.end	= 2,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for TX */
		.start	= 3,
		.end	= 3,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa3xx_device_ssp4 = {
	/* PXA3xx SSP is basically equivalent to PXA27x */
	.name		= "pxa27x-ssp",
	.id		= 3,
	.dev		= {
		.dma_mask = &pxa3xx_ssp4_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= pxa3xx_resource_ssp4,
	.num_resources	= ARRAY_SIZE(pxa3xx_resource_ssp4),
};
#endif /* CONFIG_PXA3xx */
