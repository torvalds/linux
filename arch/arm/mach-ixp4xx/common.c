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
#include <linux/irqchip/irq-ixp4xx.h>
#include <linux/platform_data/timer-ixp4xx.h>
#include <mach/udc.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
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

static struct pxa2xx_udc_mach_info ixp4xx_udc_info;

void __init ixp4xx_set_udc_info(struct pxa2xx_udc_mach_info *info)
{
	memcpy(&ixp4xx_udc_info, info, sizeof *info);
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
	.dev            = {
		.platform_data = &ixp4xx_udc_info,
	},
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

static struct platform_device *ixp46x_devices[] __initdata = {
	&ixp46x_i2c_controller
};

unsigned long ixp4xx_exp_bus_size;
EXPORT_SYMBOL(ixp4xx_exp_bus_size);

void __init ixp4xx_sys_init(void)
{
	ixp4xx_exp_bus_size = SZ_16M;

	platform_add_devices(ixp4xx_devices, ARRAY_SIZE(ixp4xx_devices));

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

#ifdef CONFIG_PCI
static int ixp4xx_needs_bounce(struct device *dev, dma_addr_t dma_addr, size_t size)
{
	return (dma_addr + size) > SZ_64M;
}

static int ixp4xx_platform_notify_remove(struct device *dev)
{
	if (dev_is_pci(dev))
		dmabounce_unregister_dev(dev);

	return 0;
}
#endif

/*
 * Setup DMA mask to 64MB on PCI devices and 4 GB on all other things.
 */
static int ixp4xx_platform_notify(struct device *dev)
{
	dev->dma_mask = &dev->coherent_dma_mask;

#ifdef CONFIG_PCI
	if (dev_is_pci(dev)) {
		dev->coherent_dma_mask = DMA_BIT_MASK(28); /* 64 MB */
		dmabounce_register_dev(dev, 2048, 4096, ixp4xx_needs_bounce);
		return 0;
	}
#endif

	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	return 0;
}

int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (dev_is_pci(dev))
		mask &= DMA_BIT_MASK(28); /* 64 MB */

	if ((mask & DMA_BIT_MASK(28)) == DMA_BIT_MASK(28)) {
		dev->coherent_dma_mask = mask;
		return 0;
	}

	return -EIO;		/* device wanted sub-64MB mask */
}
EXPORT_SYMBOL(dma_set_coherent_mask);

#ifdef CONFIG_IXP4XX_INDIRECT_PCI
/*
 * In the case of using indirect PCI, we simply return the actual PCI
 * address and our read/write implementation use that to drive the
 * access registers. If something outside of PCI is ioremap'd, we
 * fallback to the default.
 */

static void __iomem *ixp4xx_ioremap_caller(phys_addr_t addr, size_t size,
					   unsigned int mtype, void *caller)
{
	if (!is_pci_memory(addr))
		return __arm_ioremap_caller(addr, size, mtype, caller);

	return (void __iomem *)addr;
}

static void ixp4xx_iounmap(volatile void __iomem *addr)
{
	if (!is_pci_memory((__force u32)addr))
		__iounmap(addr);
}
#endif

void __init ixp4xx_init_early(void)
{
	platform_notify = ixp4xx_platform_notify;
#ifdef CONFIG_PCI
	platform_notify_remove = ixp4xx_platform_notify_remove;
#endif
#ifdef CONFIG_IXP4XX_INDIRECT_PCI
	arch_ioremap_caller = ixp4xx_ioremap_caller;
	arch_iounmap = ixp4xx_iounmap;
#endif
}
