#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/udc.h>
#include <mach/pxafb.h>
#include <mach/mmc.h>
#include <mach/irda.h>
#include <plat/i2c.h>
#include <mach/ohci.h>
#include <mach/pxa27x_keypad.h>
#include <mach/pxa2xx_spi.h>
#include <mach/camera.h>
#include <mach/audio.h>
#include <mach/pxa3xx_nand.h>

#include "devices.h"
#include "generic.h"

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
	[2] = {
		.start	= 21,
		.end	= 21,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= 22,
		.end	= 22,
		.flags	= IORESOURCE_DMA,
	},
};

static u64 pxamci_dmamask = 0xffffffffUL;

struct platform_device pxa_device_mci = {
	.name		= "pxa2xx-mci",
	.id		= 0,
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


static struct pxa2xx_udc_mach_info pxa_udc_info = {
	.gpio_pullup = -1,
	.gpio_vbus   = -1,
};

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

struct platform_device pxa25x_device_udc = {
	.name		= "pxa25x-udc",
	.id		= -1,
	.resource	= pxa2xx_udc_resources,
	.num_resources	= ARRAY_SIZE(pxa2xx_udc_resources),
	.dev		=  {
		.platform_data	= &pxa_udc_info,
		.dma_mask	= &udc_dma_mask,
	}
};

struct platform_device pxa27x_device_udc = {
	.name		= "pxa27x-udc",
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
		.start	= 0x40100000,
		.end	= 0x40100023,
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
		.start	= 0x40200000,
		.end	= 0x40200023,
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
		.start	= 0x40700000,
		.end	= 0x40700023,
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
		.start	= 0x41600000,
		.end	= 0x4160002F,
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

#ifdef CONFIG_PXA27x
static struct resource pxa27x_resources_i2c_power[] = {
	{
		.start	= 0x40f00180,
		.end	= 0x40f001a3,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_PWRI2C,
		.end	= IRQ_PWRI2C,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa27x_device_i2c_power = {
	.name		= "pxa2xx-i2c",
	.id		= 1,
	.resource	= pxa27x_resources_i2c_power,
	.num_resources	= ARRAY_SIZE(pxa27x_resources_i2c_power),
};
#endif

#ifdef CONFIG_PXA3xx
static struct resource pxa3xx_resources_i2c_power[] = {
	{
		.start  = 0x40f500c0,
		.end    = 0x40f500d3,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_PWRI2C,
		.end	= IRQ_PWRI2C,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa3xx_device_i2c_power = {
	.name		= "pxa3xx-pwri2c",
	.id		= 1,
	.resource	= pxa3xx_resources_i2c_power,
	.num_resources	= ARRAY_SIZE(pxa3xx_resources_i2c_power),
};
#endif

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

static struct resource pxa_rtc_resources[] = {
	[0] = {
		.start  = 0x40900000,
		.end	= 0x40900000 + 0x3b,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_RTC1Hz,
		.end    = IRQ_RTC1Hz,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = IRQ_RTCAlrm,
		.end    = IRQ_RTCAlrm,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device sa1100_device_rtc = {
	.name		= "sa1100-rtc",
	.id		= -1,
};

struct platform_device pxa_device_rtc = {
	.name		= "pxa-rtc",
	.id		= -1,
	.num_resources  = ARRAY_SIZE(pxa_rtc_resources),
	.resource       = pxa_rtc_resources,
};

static struct resource pxa_ac97_resources[] = {
	[0] = {
		.start  = 0x40500000,
		.end	= 0x40500000 + 0xfff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_AC97,
		.end    = IRQ_AC97,
		.flags  = IORESOURCE_IRQ,
	},
};

static u64 pxa_ac97_dmamask = 0xffffffffUL;

struct platform_device pxa_device_ac97 = {
	.name           = "pxa2xx-ac97",
	.id             = -1,
	.dev            = {
		.dma_mask = &pxa_ac97_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(pxa_ac97_resources),
	.resource       = pxa_ac97_resources,
};

void __init pxa_set_ac97_info(pxa2xx_audio_ops_t *ops)
{
	pxa_register_device(&pxa_device_ac97, ops);
}

#ifdef CONFIG_PXA25x

static struct resource pxa25x_resource_pwm0[] = {
	[0] = {
		.start	= 0x40b00000,
		.end	= 0x40b0000f,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device pxa25x_device_pwm0 = {
	.name		= "pxa25x-pwm",
	.id		= 0,
	.resource	= pxa25x_resource_pwm0,
	.num_resources	= ARRAY_SIZE(pxa25x_resource_pwm0),
};

static struct resource pxa25x_resource_pwm1[] = {
	[0] = {
		.start	= 0x40c00000,
		.end	= 0x40c0000f,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device pxa25x_device_pwm1 = {
	.name		= "pxa25x-pwm",
	.id		= 1,
	.resource	= pxa25x_resource_pwm1,
	.num_resources	= ARRAY_SIZE(pxa25x_resource_pwm1),
};

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

static struct resource pxa27x_resource_keypad[] = {
	[0] = {
		.start	= 0x41500000,
		.end	= 0x4150004c,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_KEYPAD,
		.end	= IRQ_KEYPAD,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa27x_device_keypad = {
	.name		= "pxa27x-keypad",
	.id		= -1,
	.resource	= pxa27x_resource_keypad,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_keypad),
};

void __init pxa_set_keypad_info(struct pxa27x_keypad_platform_data *info)
{
	pxa_register_device(&pxa27x_device_keypad, info);
}

static u64 pxa27x_ohci_dma_mask = DMA_BIT_MASK(32);

static struct resource pxa27x_resource_ohci[] = {
	[0] = {
		.start  = 0x4C000000,
		.end    = 0x4C00ff6f,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_USBH1,
		.end    = IRQ_USBH1,
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device pxa27x_device_ohci = {
	.name		= "pxa27x-ohci",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxa27x_ohci_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources  = ARRAY_SIZE(pxa27x_resource_ohci),
	.resource       = pxa27x_resource_ohci,
};

void __init pxa_set_ohci_info(struct pxaohci_platform_data *info)
{
	pxa_register_device(&pxa27x_device_ohci, info);
}

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

static struct resource pxa27x_resource_pwm0[] = {
	[0] = {
		.start	= 0x40b00000,
		.end	= 0x40b0001f,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device pxa27x_device_pwm0 = {
	.name		= "pxa27x-pwm",
	.id		= 0,
	.resource	= pxa27x_resource_pwm0,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_pwm0),
};

static struct resource pxa27x_resource_pwm1[] = {
	[0] = {
		.start	= 0x40c00000,
		.end	= 0x40c0001f,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device pxa27x_device_pwm1 = {
	.name		= "pxa27x-pwm",
	.id		= 1,
	.resource	= pxa27x_resource_pwm1,
	.num_resources	= ARRAY_SIZE(pxa27x_resource_pwm1),
};

static struct resource pxa27x_resource_camera[] = {
	[0] = {
		.start	= 0x50000000,
		.end	= 0x50000fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_CAMERA,
		.end	= IRQ_CAMERA,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 pxa27x_dma_mask_camera = DMA_BIT_MASK(32);

static struct platform_device pxa27x_device_camera = {
	.name		= "pxa27x-camera",
	.id		= 0, /* This is used to put cameras on this interface */
	.dev		= {
		.dma_mask      		= &pxa27x_dma_mask_camera,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxa27x_resource_camera),
	.resource	= pxa27x_resource_camera,
};

void __init pxa_set_camera_info(struct pxacamera_platform_data *info)
{
	pxa_register_device(&pxa27x_device_camera, info);
}
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

static struct resource pxa3xx_resources_mci2[] = {
	[0] = {
		.start	= 0x42000000,
		.end	= 0x42000fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MMC2,
		.end	= IRQ_MMC2,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= 93,
		.end	= 93,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= 94,
		.end	= 94,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa3xx_device_mci2 = {
	.name		= "pxa2xx-mci",
	.id		= 1,
	.dev		= {
		.dma_mask = &pxamci_dmamask,
		.coherent_dma_mask =	0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxa3xx_resources_mci2),
	.resource	= pxa3xx_resources_mci2,
};

void __init pxa3xx_set_mci2_info(struct pxamci_platform_data *info)
{
	pxa_register_device(&pxa3xx_device_mci2, info);
}

static struct resource pxa3xx_resources_mci3[] = {
	[0] = {
		.start	= 0x42500000,
		.end	= 0x42500fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MMC3,
		.end	= IRQ_MMC3,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= 100,
		.end	= 100,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= 101,
		.end	= 101,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device pxa3xx_device_mci3 = {
	.name		= "pxa2xx-mci",
	.id		= 2,
	.dev		= {
		.dma_mask = &pxamci_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pxa3xx_resources_mci3),
	.resource	= pxa3xx_resources_mci3,
};

void __init pxa3xx_set_mci3_info(struct pxamci_platform_data *info)
{
	pxa_register_device(&pxa3xx_device_mci3, info);
}

static struct resource pxa3xx_resources_nand[] = {
	[0] = {
		.start	= 0x43100000,
		.end	= 0x43100053,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_NAND,
		.end	= IRQ_NAND,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* DRCMR for Data DMA */
		.start	= 97,
		.end	= 97,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		/* DRCMR for Command DMA */
		.start	= 99,
		.end	= 99,
		.flags	= IORESOURCE_DMA,
	},
};

static u64 pxa3xx_nand_dma_mask = DMA_BIT_MASK(32);

struct platform_device pxa3xx_device_nand = {
	.name		= "pxa3xx-nand",
	.id		= -1,
	.dev		= {
		.dma_mask = &pxa3xx_nand_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(pxa3xx_resources_nand),
	.resource	= pxa3xx_resources_nand,
};

void __init pxa3xx_set_nand_info(struct pxa3xx_nand_platform_data *info)
{
	pxa_register_device(&pxa3xx_device_nand, info);
}
#endif /* CONFIG_PXA3xx */

/* pxa2xx-spi platform-device ID equals respective SSP platform-device ID + 1.
 * See comment in arch/arm/mach-pxa/ssp.c::ssp_probe() */
void __init pxa2xx_set_spi_info(unsigned id, struct pxa2xx_spi_master *info)
{
	struct platform_device *pd;

	pd = platform_device_alloc("pxa2xx-spi", id);
	if (pd == NULL) {
		printk(KERN_ERR "pxa2xx-spi: failed to allocate device id %d\n",
		       id);
		return;
	}

	pd->dev.platform_data = info;
	platform_device_add(pd);
}
