/* linux/arch/arm/plat-samsung/devs.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Base SAMSUNG platform device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/gfp.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/ioport.h>

#include <asm/irq.h>
#include <asm/pmu.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/dma.h>
#include <mach/irqs.h>
#include <mach/map.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/ehci.h>
#include <plat/fb.h>
#include <plat/fb-s3c2410.h>
#include <plat/iic.h>
#include <plat/mci.h>
#include <plat/ts.h>
#include <plat/udc.h>
#include <plat/usb-phy.h>
#include <plat/regs-iic.h>
#include <plat/regs-serial.h>
#include <plat/regs-spi.h>

static u64 samsung_device_dma_mask = DMA_BIT_MASK(32);

/* AC97 */
#ifdef CONFIG_CPU_S3C2440
static struct resource s3c_ac97_resource[] = {
	[0] = {
		.start	= S3C2440_PA_AC97,
		.end	= S3C2440_PA_AC97 + S3C2440_SZ_AC97 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_S3C244x_AC97,
		.end	= IRQ_S3C244x_AC97,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= "PCM out",
		.start	= DMACH_PCM_OUT,
		.end	= DMACH_PCM_OUT,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.name	= "PCM in",
		.start	= DMACH_PCM_IN,
		.end	= DMACH_PCM_IN,
		.flags	= IORESOURCE_DMA,
	},
	[4] = {
		.name	= "Mic in",
		.start	= DMACH_MIC_IN,
		.end	= DMACH_MIC_IN,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device s3c_device_ac97 = {
	.name		= "samsung-ac97",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_ac97_resource),
	.resource	= s3c_ac97_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_CPU_S3C2440 */

/* ADC */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_adc_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_ADC,
		.end	= S3C24XX_PA_ADC + S3C24XX_SZ_ADC - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_TC,
		.end	= IRQ_TC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_ADC,
		.end	= IRQ_ADC,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_adc = {
	.name		= "s3c24xx-adc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_adc_resource),
	.resource	= s3c_adc_resource,
};
#endif /* CONFIG_PLAT_S3C24XX */

/* Camif Controller */

#ifdef CONFIG_CPU_S3C2440
static struct resource s3c_camif_resource[] = {
	[0] = {
		.start	= S3C2440_PA_CAMIF,
		.end	= S3C2440_PA_CAMIF + S3C2440_SZ_CAMIF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CAM,
		.end	= IRQ_CAM,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_camif = {
	.name		= "s3c2440-camif",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_camif_resource),
	.resource	= s3c_camif_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_CPU_S3C2440 */

/* FIMC */

#ifdef CONFIG_S5P_DEV_FIMC0
static struct resource s5p_fimc0_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC0,
		.end	= S5P_PA_FIMC0 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC0,
		.end	= IRQ_FIMC0,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimc0 = {
	.name		= "s5p-fimc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p_fimc0_resource),
	.resource	= s5p_fimc0_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
#endif /* CONFIG_S5P_DEV_FIMC0 */

#ifdef CONFIG_S5P_DEV_FIMC1
static struct resource s5p_fimc1_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC1,
		.end	= S5P_PA_FIMC1 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC1,
		.end	= IRQ_FIMC1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimc1 = {
	.name		= "s5p-fimc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s5p_fimc1_resource),
	.resource	= s5p_fimc1_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
#endif /* CONFIG_S5P_DEV_FIMC1 */

#ifdef CONFIG_S5P_DEV_FIMC2
static struct resource s5p_fimc2_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC2,
		.end	= S5P_PA_FIMC2 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC2,
		.end	= IRQ_FIMC2,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimc2 = {
	.name		= "s5p-fimc",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(s5p_fimc2_resource),
	.resource	= s5p_fimc2_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
#endif /* CONFIG_S5P_DEV_FIMC2 */

#ifdef CONFIG_S5P_DEV_FIMC3
static struct resource s5p_fimc3_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMC3,
		.end	= S5P_PA_FIMC3 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMC3,
		.end	= IRQ_FIMC3,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimc3 = {
	.name		= "s5p-fimc",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(s5p_fimc3_resource),
	.resource	= s5p_fimc3_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
#endif /* CONFIG_S5P_DEV_FIMC3 */

/* FIMD0 */

#ifdef CONFIG_S5P_DEV_FIMD0
static struct resource s5p_fimd0_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMD0,
		.end	= S5P_PA_FIMD0 + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMD0_VSYNC,
		.end	= IRQ_FIMD0_VSYNC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_FIMD0_FIFO,
		.end	= IRQ_FIMD0_FIFO,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.start	= IRQ_FIMD0_SYSTEM,
		.end	= IRQ_FIMD0_SYSTEM,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_fimd0 = {
	.name		= "s5p-fb",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p_fimd0_resource),
	.resource	= s5p_fimd0_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s5p_fimd0_set_platdata(struct s3c_fb_platdata *pd)
{
	s3c_set_platdata(pd, sizeof(struct s3c_fb_platdata),
			 &s5p_device_fimd0);
}
#endif /* CONFIG_S5P_DEV_FIMD0 */

/* I2C HDMIPHY */

#ifdef CONFIG_S5P_DEV_I2C_HDMIPHY
static struct resource s5p_i2c_resource[] = {
	[0] = {
		.start	= S5P_PA_IIC_HDMIPHY,
		.end	= S5P_PA_IIC_HDMIPHY + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IIC_HDMIPHY,
		.end	= IRQ_IIC_HDMIPHY,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_i2c_hdmiphy = {
	.name		= "s3c2440-hdmiphy-i2c",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_i2c_resource),
	.resource	= s5p_i2c_resource,
};

void __init s5p_i2c_hdmiphy_set_platdata(struct s3c2410_platform_i2c *pd)
{
	struct s3c2410_platform_i2c *npd;

	if (!pd) {
		pd = &default_i2c_data;

		if (soc_is_exynos4210())
			pd->bus_num = 8;
		else if (soc_is_s5pv210())
			pd->bus_num = 3;
		else
			pd->bus_num = 0;
	}

	npd = s3c_set_platdata(pd, sizeof(struct s3c2410_platform_i2c),
			       &s5p_device_i2c_hdmiphy);
}
#endif /* CONFIG_S5P_DEV_I2C_HDMIPHY */

/* I2S */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_iis_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_IIS,
		.end	= S3C24XX_PA_IIS + S3C24XX_SZ_IIS - 1,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device s3c_device_iis = {
	.name		= "s3c24xx-iis",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_iis_resource),
	.resource	= s3c_iis_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_PLAT_S3C24XX */

#ifdef CONFIG_CPU_S3C2440
struct platform_device s3c2412_device_iis = {
	.name		= "s3c2412-iis",
	.id		= -1,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_CPU_S3C2440 */

/* LCD Controller */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_lcd_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_LCD,
		.end	= S3C24XX_PA_LCD + S3C24XX_SZ_LCD - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_LCD,
		.end	= IRQ_LCD,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_lcd = {
	.name		= "s3c2410-lcd",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_lcd_resource),
	.resource	= s3c_lcd_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};

void __init s3c24xx_fb_set_platdata(struct s3c2410fb_mach_info *pd)
{
	struct s3c2410fb_mach_info *npd;

	npd = s3c_set_platdata(pd, sizeof(*npd), &s3c_device_lcd);
	if (npd) {
		npd->displays = kmemdup(pd->displays,
			sizeof(struct s3c2410fb_display) * npd->num_displays,
			GFP_KERNEL);
		if (!npd->displays)
			printk(KERN_ERR "no memory for LCD display data\n");
	} else {
		printk(KERN_ERR "no memory for LCD platform data\n");
	}
}
#endif /* CONFIG_PLAT_S3C24XX */

/* MFC */

#ifdef CONFIG_S5P_DEV_MFC
static struct resource s5p_mfc_resource[] = {
	[0] = {
		.start	= S5P_PA_MFC,
		.end	= S5P_PA_MFC + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MFC,
		.end	= IRQ_MFC,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_mfc = {
	.name		= "s5p-mfc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_mfc_resource),
	.resource	= s5p_mfc_resource,
};

/*
 * MFC hardware has 2 memory interfaces which are modelled as two separate
 * platform devices to let dma-mapping distinguish between them.
 *
 * MFC parent device (s5p_device_mfc) must be registered before memory
 * interface specific devices (s5p_device_mfc_l and s5p_device_mfc_r).
 */

struct platform_device s5p_device_mfc_l = {
	.name		= "s5p-mfc-l",
	.id		= -1,
	.dev		= {
		.parent			= &s5p_device_mfc.dev,
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

struct platform_device s5p_device_mfc_r = {
	.name		= "s5p-mfc-r",
	.id		= -1,
	.dev		= {
		.parent			= &s5p_device_mfc.dev,
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};
#endif /* CONFIG_S5P_DEV_MFC */

/* MIPI CSIS */

#ifdef CONFIG_S5P_DEV_CSIS0
static struct resource s5p_mipi_csis0_resource[] = {
	[0] = {
		.start	= S5P_PA_MIPI_CSIS0,
		.end	= S5P_PA_MIPI_CSIS0 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MIPI_CSIS0,
		.end	= IRQ_MIPI_CSIS0,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_mipi_csis0 = {
	.name		= "s5p-mipi-csis",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p_mipi_csis0_resource),
	.resource	= s5p_mipi_csis0_resource,
};
#endif /* CONFIG_S5P_DEV_CSIS0 */

#ifdef CONFIG_S5P_DEV_CSIS1
static struct resource s5p_mipi_csis1_resource[] = {
	[0] = {
		.start	= S5P_PA_MIPI_CSIS1,
		.end	= S5P_PA_MIPI_CSIS1 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MIPI_CSIS1,
		.end	= IRQ_MIPI_CSIS1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_mipi_csis1 = {
	.name		= "s5p-mipi-csis",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s5p_mipi_csis1_resource),
	.resource	= s5p_mipi_csis1_resource,
};
#endif

#ifdef CONFIG_S3C64XX_DEV_ONENAND1
static struct resource s3c64xx_onenand1_resources[] = {
	[0] = {
		.start	= S3C64XX_PA_ONENAND1,
		.end	= S3C64XX_PA_ONENAND1 + 0x400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S3C64XX_PA_ONENAND1_BUF,
		.end	= S3C64XX_PA_ONENAND1_BUF + S3C64XX_SZ_ONENAND1_BUF - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_ONENAND1,
		.end	= IRQ_ONENAND1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s3c64xx_device_onenand1 = {
	.name		= "samsung-onenand",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c64xx_onenand1_resources),
	.resource	= s3c64xx_onenand1_resources,
};

void s3c64xx_onenand1_set_platdata(struct onenand_platform_data *pdata)
{
	s3c_set_platdata(pdata, sizeof(struct onenand_platform_data),
			 &s3c64xx_device_onenand1);
}
#endif /* CONFIG_S3C64XX_DEV_ONENAND1 */

#ifdef CONFIG_S5P_DEV_ONENAND
static struct resource s5p_onenand_resources[] = {
	[0] = {
		.start	= S5P_PA_ONENAND,
		.end	= S5P_PA_ONENAND + SZ_128K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= S5P_PA_ONENAND_DMA,
		.end	= S5P_PA_ONENAND_DMA + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_ONENAND_AUDI,
		.end	= IRQ_ONENAND_AUDI,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_onenand = {
	.name		= "s5pc110-onenand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_onenand_resources),
	.resource	= s5p_onenand_resources,
};
#endif /* CONFIG_S5P_DEV_ONENAND */

/* PMU */

#ifdef CONFIG_PLAT_S5P
static struct resource s5p_pmu_resource = {
	.start	= IRQ_PMU,
	.end	= IRQ_PMU,
	.flags	= IORESOURCE_IRQ,
};

struct platform_device s5p_device_pmu = {
	.name		= "arm-pmu",
	.id		= ARM_PMU_DEVICE_CPU,
	.num_resources	= 1,
	.resource	= &s5p_pmu_resource,
};

static int __init s5p_pmu_init(void)
{
	platform_device_register(&s5p_device_pmu);
	return 0;
}
arch_initcall(s5p_pmu_init);
#endif /* CONFIG_PLAT_S5P */

/* RTC */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_rtc_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_RTC,
		.end	= S3C24XX_PA_RTC + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_RTC,
		.end	= IRQ_RTC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_TICK,
		.end	= IRQ_TICK,
		.flags	= IORESOURCE_IRQ
	}
};

struct platform_device s3c_device_rtc = {
	.name		= "s3c2410-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_rtc_resource),
	.resource	= s3c_rtc_resource,
};
#endif /* CONFIG_PLAT_S3C24XX */

/* SDI */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_sdi_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_SDI,
		.end	= S3C24XX_PA_SDI + S3C24XX_SZ_SDI - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SDI,
		.end	= IRQ_SDI,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_sdi = {
	.name		= "s3c2410-sdi",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_sdi_resource),
	.resource	= s3c_sdi_resource,
};

void __init s3c24xx_mci_set_platdata(struct s3c24xx_mci_pdata *pdata)
{
	s3c_set_platdata(pdata, sizeof(struct s3c24xx_mci_pdata),
			 &s3c_device_sdi);
}
#endif /* CONFIG_PLAT_S3C24XX */

/* SPI */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_spi0_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_SPI,
		.end	= S3C24XX_PA_SPI + 0x1f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SPI0,
		.end	= IRQ_SPI0,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_spi0 = {
	.name		= "s3c2410-spi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s3c_spi0_resource),
	.resource	= s3c_spi0_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};

static struct resource s3c_spi1_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_SPI + S3C2410_SPI1,
		.end	= S3C24XX_PA_SPI + S3C2410_SPI1 + 0x1f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SPI1,
		.end	= IRQ_SPI1,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_spi1 = {
	.name		= "s3c2410-spi",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s3c_spi1_resource),
	.resource	= s3c_spi1_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_PLAT_S3C24XX */

/* Touchscreen */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_ts_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_ADC,
		.end	= S3C24XX_PA_ADC + S3C24XX_SZ_ADC - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_TC,
		.end	= IRQ_TC,
		.flags	= IORESOURCE_IRQ,
	},

};

struct platform_device s3c_device_ts = {
	.name		= "s3c2410-ts",
	.id		= -1,
	.dev.parent	= &s3c_device_adc.dev,
	.num_resources	= ARRAY_SIZE(s3c_ts_resource),
	.resource	= s3c_ts_resource,
};

void __init s3c24xx_ts_set_platdata(struct s3c2410_ts_mach_info *hard_s3c2410ts_info)
{
	s3c_set_platdata(hard_s3c2410ts_info,
			 sizeof(struct s3c2410_ts_mach_info), &s3c_device_ts);
}
#endif /* CONFIG_PLAT_S3C24XX */

/* TV */

#ifdef CONFIG_S5P_DEV_TV

static struct resource s5p_hdmi_resources[] = {
	[0] = {
		.start	= S5P_PA_HDMI,
		.end	= S5P_PA_HDMI + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_HDMI,
		.end	= IRQ_HDMI,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_hdmi = {
	.name		= "s5p-hdmi",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_hdmi_resources),
	.resource	= s5p_hdmi_resources,
};

static struct resource s5p_sdo_resources[] = {
	[0] = {
		.start	= S5P_PA_SDO,
		.end	= S5P_PA_SDO + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SDO,
		.end	= IRQ_SDO,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_sdo = {
	.name		= "s5p-sdo",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_sdo_resources),
	.resource	= s5p_sdo_resources,
};

static struct resource s5p_mixer_resources[] = {
	[0] = {
		.start	= S5P_PA_MIXER,
		.end	= S5P_PA_MIXER + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
		.name	= "mxr"
	},
	[1] = {
		.start	= S5P_PA_VP,
		.end	= S5P_PA_VP + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
		.name	= "vp"
	},
	[2] = {
		.start	= IRQ_MIXER,
		.end	= IRQ_MIXER,
		.flags	= IORESOURCE_IRQ,
		.name	= "irq"
	}
};

struct platform_device s5p_device_mixer = {
	.name		= "s5p-mixer",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_mixer_resources),
	.resource	= s5p_mixer_resources,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};
#endif /* CONFIG_S5P_DEV_TV */

/* USB Device (Gadget) */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_usbgadget_resource[] = {
	[0] = {
		.start	= S3C24XX_PA_USBDEV,
		.end	= S3C24XX_PA_USBDEV + S3C24XX_SZ_USBDEV - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBD,
		.end	= IRQ_USBD,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_usbgadget = {
	.name		= "s3c2410-usbgadget",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_usbgadget_resource),
	.resource	= s3c_usbgadget_resource,
};

void __init s3c24xx_udc_set_platdata(struct s3c2410_udc_mach_info *pd)
{
	s3c_set_platdata(pd, sizeof(*pd), &s3c_device_usbgadget);
}
#endif /* CONFIG_PLAT_S3C24XX */

/* USB EHCI Host Controller */

#ifdef CONFIG_S5P_DEV_USB_EHCI
static struct resource s5p_ehci_resource[] = {
	[0] = {
		.start	= S5P_PA_EHCI,
		.end	= S5P_PA_EHCI + SZ_256 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USB_HOST,
		.end	= IRQ_USB_HOST,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s5p_device_ehci = {
	.name		= "s5p-ehci",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_ehci_resource),
	.resource	= s5p_ehci_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	}
};

void __init s5p_ehci_set_platdata(struct s5p_ehci_platdata *pd)
{
	struct s5p_ehci_platdata *npd;

	npd = s3c_set_platdata(pd, sizeof(struct s5p_ehci_platdata),
			&s5p_device_ehci);

	if (!npd->phy_init)
		npd->phy_init = s5p_usb_phy_init;
	if (!npd->phy_exit)
		npd->phy_exit = s5p_usb_phy_exit;
}
#endif /* CONFIG_S5P_DEV_USB_EHCI */

/* USB High Spped 2.0 Device (Gadget) */

#ifdef CONFIG_PLAT_S3C24XX
static struct resource s3c_hsudc_resource[] = {
	[0] = {
		.start	= S3C2416_PA_HSUDC,
		.end	= S3C2416_PA_HSUDC + S3C2416_SZ_HSUDC - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBD,
		.end	= IRQ_USBD,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device s3c_device_usb_hsudc = {
	.name		= "s3c-hsudc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s3c_hsudc_resource),
	.resource	= s3c_hsudc_resource,
	.dev		= {
		.dma_mask		= &samsung_device_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

void __init s3c24xx_hsudc_set_platdata(struct s3c24xx_hsudc_platdata *pd)
{
	s3c_set_platdata(pd, sizeof(*pd), &s3c_device_usb_hsudc);
}
#endif /* CONFIG_PLAT_S3C24XX */
