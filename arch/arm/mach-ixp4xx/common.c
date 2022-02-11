/*
 * arch/arm/mach-ixp4xx/common.c
 *
 * Generic code shared across all IXP4XX platforms
 *
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2002 (c) Intel Corporation
 * Copyright 2003-2004 (c) MontaVista, Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/cpu.h>
#include <linux/pci.h>
#include <linux/sched_clock.h>
#include <linux/soc/ixp4xx/cpu.h>
#include <linux/irqchip/irq-ixp4xx.h>
#include <linux/platform_data/timer-ixp4xx.h>
#include <mach/hardware.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/exception.h>
#include <asm/irq.h>
#include <asm/system_misc.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include "irqs.h"

#define IXP4XX_TIMER_FREQ 66666000

/*************************************************************************
 * IXP4xx chipset I/O mapping
 *************************************************************************/
static struct map_desc ixp4xx_io_desc[] __initdata = {
	{	/* UART, Interrupt ctrl, GPIO, timers, NPEs, MACs, USB .... */
		.virtual	= (unsigned long)IXP4XX_PERIPHERAL_BASE_VIRT,
		.pfn		= __phys_to_pfn(IXP4XX_PERIPHERAL_BASE_PHYS),
		.length		= IXP4XX_PERIPHERAL_REGION_SIZE,
		.type		= MT_DEVICE
	}, {	/* Expansion Bus Config Registers */
		.virtual	= (unsigned long)IXP4XX_EXP_CFG_BASE_VIRT,
		.pfn		= __phys_to_pfn(IXP4XX_EXP_CFG_BASE_PHYS),
		.length		= IXP4XX_EXP_CFG_REGION_SIZE,
		.type		= MT_DEVICE
	}, {	/* PCI Registers */
		.virtual	= (unsigned long)IXP4XX_PCI_CFG_BASE_VIRT,
		.pfn		= __phys_to_pfn(IXP4XX_PCI_CFG_BASE_PHYS),
		.length		= IXP4XX_PCI_CFG_REGION_SIZE,
		.type		= MT_DEVICE
	},
};

void __init ixp4xx_map_io(void)
{
  	iotable_init(ixp4xx_io_desc, ARRAY_SIZE(ixp4xx_io_desc));
}

void __init ixp4xx_init_irq(void)
{
	/*
	 * ixp4xx does not implement the XScale PWRMODE register
	 * so it must not call cpu_do_idle().
	 */
	cpu_idle_poll_ctrl(true);

	ixp4xx_irq_init(IXP4XX_INTC_BASE_PHYS,
			(cpu_is_ixp46x() || cpu_is_ixp43x()));
}

void __init ixp4xx_timer_init(void)
{
	return ixp4xx_timer_setup(IXP4XX_TIMER_BASE_PHYS,
				  IRQ_IXP4XX_TIMER1,
				  IXP4XX_TIMER_FREQ);
}

static struct resource ixp4xx_udc_resources[] = {
	[0] = {
		.start  = 0xc800b000,
		.end    = 0xc800bfff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_IXP4XX_USB,
		.end    = IRQ_IXP4XX_USB,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource ixp4xx_gpio_resource[] = {
	{
		.start = IXP4XX_GPIO_BASE_PHYS,
		.end = IXP4XX_GPIO_BASE_PHYS + 0xfff,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device ixp4xx_gpio_device = {
	.name           = "ixp4xx-gpio",
	.id             = -1,
	.dev = {
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.resource = ixp4xx_gpio_resource,
	.num_resources  = ARRAY_SIZE(ixp4xx_gpio_resource),
};

/*
 * USB device controller. The IXP4xx uses the same controller as PXA25X,
 * so we just use the same device.
 */
static struct platform_device ixp4xx_udc_device = {
	.name           = "pxa25x-udc",
	.id             = -1,
	.num_resources  = 2,
	.resource       = ixp4xx_udc_resources,
};

static struct resource ixp4xx_npe_resources[] = {
	{
		.start = IXP4XX_NPEA_BASE_PHYS,
		.end = IXP4XX_NPEA_BASE_PHYS + 0xfff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IXP4XX_NPEB_BASE_PHYS,
		.end = IXP4XX_NPEB_BASE_PHYS + 0xfff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IXP4XX_NPEC_BASE_PHYS,
		.end = IXP4XX_NPEC_BASE_PHYS + 0xfff,
		.flags = IORESOURCE_MEM,
	},

};

static struct platform_device ixp4xx_npe_device = {
	.name           = "ixp4xx-npe",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ixp4xx_npe_resources),
	.resource       = ixp4xx_npe_resources,
};

static struct resource ixp4xx_qmgr_resources[] = {
	{
		.start = IXP4XX_QMGR_BASE_PHYS,
		.end = IXP4XX_QMGR_BASE_PHYS + 0x3fff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_IXP4XX_QM1,
		.end = IRQ_IXP4XX_QM1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_IXP4XX_QM2,
		.end = IRQ_IXP4XX_QM2,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ixp4xx_qmgr_device = {
	.name           = "ixp4xx-qmgr",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ixp4xx_qmgr_resources),
	.resource       = ixp4xx_qmgr_resources,
};

static struct platform_device *ixp4xx_devices[] __initdata = {
	&ixp4xx_npe_device,
	&ixp4xx_qmgr_device,
	&ixp4xx_gpio_device,
	&ixp4xx_udc_device,
};

static struct resource ixp46x_i2c_resources[] = {
	[0] = {
		.start 	= 0xc8011000,
		.end	= 0xc801101c,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start 	= IRQ_IXP4XX_I2C,
		.end	= IRQ_IXP4XX_I2C,
		.flags	= IORESOURCE_IRQ
	}
};

/* A single 32-bit register on IXP46x */
#define IXP4XX_HWRANDOM_BASE_PHYS	0x70002100

static struct resource ixp46x_hwrandom_resource[] = {
	{
		.start = IXP4XX_HWRANDOM_BASE_PHYS,
		.end = IXP4XX_HWRANDOM_BASE_PHYS + 0x3,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device ixp46x_hwrandom_device = {
	.name           = "ixp4xx-hwrandom",
	.id             = -1,
	.dev = {
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.resource = ixp46x_hwrandom_resource,
	.num_resources  = ARRAY_SIZE(ixp46x_hwrandom_resource),
};

/*
 * I2C controller. The IXP46x uses the same block as the IOP3xx, so
 * we just use the same device name.
 */
static struct platform_device ixp46x_i2c_controller = {
	.name		= "IOP3xx-I2C",
	.id		= 0,
	.num_resources	= 2,
	.resource	= ixp46x_i2c_resources
};

static struct resource ixp46x_ptp_resources[] = {
	DEFINE_RES_MEM(IXP4XX_TIMESYNC_BASE_PHYS, SZ_4K),
	DEFINE_RES_IRQ_NAMED(IRQ_IXP4XX_GPIO8, "master"),
	DEFINE_RES_IRQ_NAMED(IRQ_IXP4XX_GPIO7, "slave"),
};

static struct platform_device ixp46x_ptp = {
	.name		= "ptp-ixp46x",
	.id		= -1,
	.resource	= ixp46x_ptp_resources,
	.num_resources	= ARRAY_SIZE(ixp46x_ptp_resources),
};

static struct platform_device *ixp46x_devices[] __initdata = {
	&ixp46x_hwrandom_device,
	&ixp46x_i2c_controller,
	&ixp46x_ptp,
};

unsigned long ixp4xx_exp_bus_size;
EXPORT_SYMBOL(ixp4xx_exp_bus_size);

static struct platform_device_info ixp_dev_info __initdata = {
	.name		= "ixp4xx_crypto",
	.id		= 0,
	.dma_mask	= DMA_BIT_MASK(32),
};

static int __init ixp_crypto_register(void)
{
	struct platform_device *pdev;

	if (!(~(*IXP4XX_EXP_CFG2) & (IXP4XX_FEATURE_HASH |
				IXP4XX_FEATURE_AES | IXP4XX_FEATURE_DES))) {
		printk(KERN_ERR "ixp_crypto: No HW crypto available\n");
		return -ENODEV;
	}

	pdev = platform_device_register_full(&ixp_dev_info);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return 0;
}

void __init ixp4xx_sys_init(void)
{
	ixp4xx_exp_bus_size = SZ_16M;

	platform_add_devices(ixp4xx_devices, ARRAY_SIZE(ixp4xx_devices));

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_IXP4XX))
		ixp_crypto_register();

	if (cpu_is_ixp46x()) {
		int region;

		platform_add_devices(ixp46x_devices,
				ARRAY_SIZE(ixp46x_devices));

		for (region = 0; region < 7; region++) {
			if((*(IXP4XX_EXP_REG(0x4 * region)) & 0x200)) {
				ixp4xx_exp_bus_size = SZ_32M;
				break;
			}
		}
	}

	printk("IXP4xx: Using %luMiB expansion bus window size\n",
			ixp4xx_exp_bus_size >> 20);
}

unsigned long ixp4xx_timer_freq = IXP4XX_TIMER_FREQ;
EXPORT_SYMBOL(ixp4xx_timer_freq);

void ixp4xx_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode == REBOOT_SOFT) {
		/* Jump into ROM at address 0 */
		soft_restart(0);
	} else {
		/* Use on-chip reset capability */

		/* set the "key" register to enable access to
		 * "timer" and "enable" registers
		 */
		*IXP4XX_OSWK = IXP4XX_WDT_KEY;

		/* write 0 to the timer register for an immediate reset */
		*IXP4XX_OSWT = 0;

		*IXP4XX_OSWE = IXP4XX_WDT_RESET_ENABLE | IXP4XX_WDT_COUNT_ENABLE;
	}
}
