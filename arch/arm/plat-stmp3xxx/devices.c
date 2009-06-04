/*
* Freescale STMP37XX/STMP378X platform devices
*
* Embedded Alley Solutions, Inc <source@embeddedalley.com>
*
* Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
* Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
*/

/*
* The code contained herein is licensed under the GNU General Public
* License. You may obtain a copy of the GNU General Public License
* Version 2 or later at the following locations:
*
* http://www.opensource.org/licenses/gpl-license.html
* http://www.gnu.org/copyleft/gpl.html
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/dma.h>
#include <mach/platform.h>
#include <mach/stmp3xxx.h>
#include <mach/regs-lcdif.h>
#include <mach/regs-uartapp.h>
#include <mach/regs-gpmi.h>
#include <mach/regs-usbctrl.h>
#include <mach/regs-ssp.h>
#include <mach/regs-rtc.h>

static u64 common_dmamask = DMA_BIT_MASK(32);

static struct resource appuart_resources[] = {
	{
		.start = IRQ_UARTAPP_INTERNAL,
		.end = IRQ_UARTAPP_INTERNAL,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_UARTAPP_RX_DMA,
		.end = IRQ_UARTAPP_RX_DMA,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_UARTAPP_TX_DMA,
		.end = IRQ_UARTAPP_TX_DMA,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = REGS_UARTAPP1_PHYS,
		.end = REGS_UARTAPP1_PHYS + REGS_UARTAPP_SIZE,
		.flags = IORESOURCE_MEM,
	}, {
		/* Rx DMA channel */
		.start = STMP3XXX_DMA(6, STMP3XXX_BUS_APBX),
		.end = STMP3XXX_DMA(6, STMP3XXX_BUS_APBX),
		.flags = IORESOURCE_DMA,
	}, {
		/* Tx DMA channel */
		.start = STMP3XXX_DMA(7, STMP3XXX_BUS_APBX),
		.end = STMP3XXX_DMA(7, STMP3XXX_BUS_APBX),
		.flags = IORESOURCE_DMA,
	},
};

struct platform_device stmp3xxx_appuart = {
	.name = "stmp3xxx-appuart",
	.id = 0,
	.resource = appuart_resources,
	.num_resources = ARRAY_SIZE(appuart_resources),
	.dev = {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

struct platform_device stmp3xxx_watchdog = {
      .name   = "stmp3xxx_wdt",
      .id     = -1,
};

static struct resource ts_resource[] = {
	{
		.flags  = IORESOURCE_IRQ,
		.start  = IRQ_TOUCH_DETECT,
		.end    = IRQ_TOUCH_DETECT,
	}, {
		.flags  = IORESOURCE_IRQ,
		.start  = IRQ_LRADC_CH5,
		.end    = IRQ_LRADC_CH5,
	},
};

struct platform_device stmp3xxx_touchscreen = {
	.name		= "stmp3xxx_ts",
	.id		= -1,
	.resource	= ts_resource,
	.num_resources	= ARRAY_SIZE(ts_resource),
};

/*
* Keypad device
*/
struct platform_device stmp3xxx_keyboard = {
	.name		= "stmp3xxx-keyboard",
	.id		= -1,
};

static struct resource gpmi_resources[] = {
	{
		.flags = IORESOURCE_MEM,
		.start = REGS_GPMI_PHYS,
		.end = REGS_GPMI_PHYS + REGS_GPMI_SIZE,
	}, {
		.flags = IORESOURCE_IRQ,
		.start = IRQ_GPMI_DMA,
		.end = IRQ_GPMI_DMA,
	}, {
		.flags = IORESOURCE_DMA,
		.start = STMP3XXX_DMA(4, STMP3XXX_BUS_APBH),
		.end = STMP3XXX_DMA(8, STMP3XXX_BUS_APBH),
	},
};

struct platform_device stmp3xxx_gpmi = {
	.name = "gpmi",
	.id = -1,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = gpmi_resources,
	.num_resources = ARRAY_SIZE(gpmi_resources),
};

static struct resource mmc1_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
		.start	= REGS_SSP1_PHYS,
		.end	= REGS_SSP1_PHYS + REGS_SSP_SIZE,
	}, {
		.flags	= IORESOURCE_DMA,
		.start	= STMP3XXX_DMA(1, STMP3XXX_BUS_APBH),
		.end	= STMP3XXX_DMA(1, STMP3XXX_BUS_APBH),
	}, {
		.flags	= IORESOURCE_IRQ,
		.start	= IRQ_SSP1_DMA,
		.end	= IRQ_SSP1_DMA,
	}, {
		.flags	= IORESOURCE_IRQ,
		.start	= IRQ_SSP_ERROR,
		.end	= IRQ_SSP_ERROR,
	},
};

struct platform_device stmp3xxx_mmc = {
	.name	= "stmp3xxx-mmc",
	.id	= 1,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = mmc1_resource,
	.num_resources = ARRAY_SIZE(mmc1_resource),
};

static struct resource usb_resources[] = {
	{
		.start	= REGS_USBCTRL_PHYS,
		.end	= REGS_USBCTRL_PHYS + SZ_4K,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_USB_CTRL,
		.end	= IRQ_USB_CTRL,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device stmp3xxx_udc = {
	.name		= "fsl-usb2-udc",
	.id		= -1,
	.dev		= {
		.dma_mask		= &common_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource = usb_resources,
	.num_resources = ARRAY_SIZE(usb_resources),
};

struct platform_device stmp3xxx_ehci = {
	.name		= "fsl-ehci",
	.id		= -1,
	.dev		= {
		.dma_mask		= &common_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= usb_resources,
	.num_resources	= ARRAY_SIZE(usb_resources),
};

static struct resource rtc_resources[] = {
	{
		.start	= REGS_RTC_PHYS,
		.end	= REGS_RTC_PHYS + REGS_RTC_SIZE,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_RTC_ALARM,
		.end	= IRQ_RTC_ALARM,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= IRQ_RTC_1MSEC,
		.end	= IRQ_RTC_1MSEC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device stmp3xxx_rtc = {
	.name		= "stmp3xxx-rtc",
	.id		= -1,
	.resource	= rtc_resources,
	.num_resources	= ARRAY_SIZE(rtc_resources),
};

static struct resource ssp1_resources[] = {
	{
		.start	= REGS_SSP1_PHYS,
		.end	= REGS_SSP1_PHYS + REGS_SSP_SIZE,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_SSP1_DMA,
		.end	= IRQ_SSP1_DMA,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= STMP3XXX_DMA(1, STMP3XXX_BUS_APBH),
		.end	= STMP3XXX_DMA(1, STMP3XXX_BUS_APBH),
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource ssp2_resources[] = {
	{
		.start	= REGS_SSP2_PHYS,
		.end	= REGS_SSP2_PHYS + REGS_SSP_SIZE,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_SSP2_DMA,
		.end	= IRQ_SSP2_DMA,
		.flags	= IORESOURCE_IRQ,
	}, {
		.start	= STMP3XXX_DMA(2, STMP3XXX_BUS_APBH),
		.end	= STMP3XXX_DMA(2, STMP3XXX_BUS_APBH),
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device stmp3xxx_spi1 = {
	.name	= "stmp3xxx_ssp",
	.id	= 1,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = ssp1_resources,
	.num_resources = ARRAY_SIZE(ssp1_resources),
};

struct platform_device stmp3xxx_spi2 = {
	.name	= "stmp3xxx_ssp",
	.id	= 2,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource = ssp2_resources,
	.num_resources = ARRAY_SIZE(ssp2_resources),
};

static struct resource fb_resource[] = {
	{
		.flags	= IORESOURCE_IRQ,
		.start	= IRQ_LCDIF_DMA,
		.end	= IRQ_LCDIF_DMA,
	}, {
		.flags	= IORESOURCE_IRQ,
		.start	= IRQ_LCDIF_ERROR,
		.end	= IRQ_LCDIF_ERROR,
	}, {
		.flags	= IORESOURCE_MEM,
		.start	= REGS_LCDIF_PHYS,
		.end	= REGS_LCDIF_PHYS + REGS_LCDIF_SIZE,
	},
};

struct platform_device stmp3xxx_framebuffer = {
	.name		= "stmp3xxx-fb",
	.id		= -1,
	.dev		= {
		.dma_mask		= &common_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(fb_resource),
	.resource	= fb_resource,
};

#define CMDLINE_DEVICE_CHOOSE(name, dev1, dev2)			\
	static char *cmdline_device_##name;			\
	static int cmdline_device_##name##_setup(char *dev)	\
	{							\
		cmdline_device_##name = dev + 1;		\
		return 0;					\
	}							\
	__setup(#name, cmdline_device_##name##_setup);		\
	int stmp3xxx_##name##_device_register(void)		\
	{							\
		struct platform_device *d = NULL;		\
		if (!cmdline_device_##name ||			\
			!strcmp(cmdline_device_##name, #dev1))	\
				d = &stmp3xxx_##dev1;		\
		else if (!strcmp(cmdline_device_##name, #dev2))	\
				d = &stmp3xxx_##dev2;		\
		else						\
			printk(KERN_ERR"Unknown %s assignment '%s'.\n",	\
				#name, cmdline_device_##name);	\
		return d ? platform_device_register(d) : -ENOENT;	\
	}

CMDLINE_DEVICE_CHOOSE(ssp1, mmc, spi1)
CMDLINE_DEVICE_CHOOSE(ssp2, gpmi, spi2)

struct platform_device stmp3xxx_backlight = {
	.name		= "stmp3xxx-bl",
	.id		= -1,
};

struct platform_device stmp3xxx_rotdec = {
	.name	= "stmp3xxx-rotdec",
	.id	= -1,
};

struct platform_device stmp3xxx_persistent = {
	.name			= "stmp3xxx-persistent",
	.id			= -1,
};

struct platform_device stmp3xxx_dcp_bootstream = {
	.name			= "stmp3xxx-dcpboot",
	.id			= -1,
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource dcp_resources[] = {
	{
		.start = IRQ_DCP_VMI,
		.end = IRQ_DCP_VMI,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = IRQ_DCP,
		.end = IRQ_DCP,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device stmp3xxx_dcp = {
	.name			= "stmp3xxx-dcp",
	.id			= -1,
	.resource		= dcp_resources,
	.num_resources		= ARRAY_SIZE(dcp_resources),
	.dev	= {
		.dma_mask	= &common_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource battery_resource[] = {
	{
		.flags  = IORESOURCE_IRQ,
		.start  = IRQ_VDD5V,
		.end    = IRQ_VDD5V,
	},
};

struct platform_device stmp3xxx_battery = {
	.name   = "stmp3xxx-battery",
	.resource = battery_resource,
	.num_resources = ARRAY_SIZE(battery_resource),
};
