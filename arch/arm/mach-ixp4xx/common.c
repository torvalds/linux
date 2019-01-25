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
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/cpu.h>
#include <linux/pci.h>
#include <linux/sched_clock.h>
#include <linux/bitops.h>
#include <linux/irqchip/irq-ixp4xx.h>
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

/*
 * The timer register doesn't allow to specify the two least significant bits of
 * the timeout value and assumes them being zero. So make sure IXP4XX_LATCH is
 * the best value with the two least significant bits unset.
 */
#define IXP4XX_LATCH DIV_ROUND_CLOSEST(IXP4XX_TIMER_FREQ, \
				       (IXP4XX_OST_RELOAD_MASK + 1) * HZ) * \
			(IXP4XX_OST_RELOAD_MASK + 1)

static void __init ixp4xx_clocksource_init(void);
static void __init ixp4xx_clockevent_init(void);
static struct clock_event_device clockevent_ixp4xx;

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
	}, {	/* Queue Manager */
		.virtual	= (unsigned long)IXP4XX_QMGR_BASE_VIRT,
		.pfn		= __phys_to_pfn(IXP4XX_QMGR_BASE_PHYS),
		.length		= IXP4XX_QMGR_REGION_SIZE,
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

/*************************************************************************
 * IXP4xx timer tick
 * We use OS timer1 on the CPU for the timer tick and the timestamp 
 * counter as a source of real clock ticks to account for missed jiffies.
 *************************************************************************/

static irqreturn_t ixp4xx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	/* Clear Pending Interrupt by writing '1' to it */
	*IXP4XX_OSST = IXP4XX_OSST_TIMER_1_PEND;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

void __init ixp4xx_timer_init(void)
{
	/* Reset/disable counter */
	*IXP4XX_OSRT1 = 0;

	/* Clear Pending Interrupt by writing '1' to it */
	*IXP4XX_OSST = IXP4XX_OSST_TIMER_1_PEND;

	/* Reset time-stamp counter */
	*IXP4XX_OSTS = 0;

	ixp4xx_clocksource_init();
	ixp4xx_clockevent_init();
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

static struct platform_device *ixp4xx_devices[] __initdata = {
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

/*
 * sched_clock()
 */
static u64 notrace ixp4xx_read_sched_clock(void)
{
	return *IXP4XX_OSTS;
}

/*
 * clocksource
 */

static u64 ixp4xx_clocksource_read(struct clocksource *c)
{
	return *IXP4XX_OSTS;
}

unsigned long ixp4xx_timer_freq = IXP4XX_TIMER_FREQ;
EXPORT_SYMBOL(ixp4xx_timer_freq);
static void __init ixp4xx_clocksource_init(void)
{
	sched_clock_register(ixp4xx_read_sched_clock, 32, ixp4xx_timer_freq);

	clocksource_mmio_init(NULL, "OSTS", ixp4xx_timer_freq, 200, 32,
			ixp4xx_clocksource_read);
}

/*
 * clockevents
 */
static int ixp4xx_set_next_event(unsigned long evt,
				 struct clock_event_device *unused)
{
	unsigned long opts = *IXP4XX_OSRT1 & IXP4XX_OST_RELOAD_MASK;

	*IXP4XX_OSRT1 = (evt & ~IXP4XX_OST_RELOAD_MASK) | opts;

	return 0;
}

static int ixp4xx_shutdown(struct clock_event_device *evt)
{
	unsigned long opts = *IXP4XX_OSRT1 & IXP4XX_OST_RELOAD_MASK;
	unsigned long osrt = *IXP4XX_OSRT1 & ~IXP4XX_OST_RELOAD_MASK;

	opts &= ~IXP4XX_OST_ENABLE;
	*IXP4XX_OSRT1 = osrt | opts;
	return 0;
}

static int ixp4xx_set_oneshot(struct clock_event_device *evt)
{
	unsigned long opts = IXP4XX_OST_ENABLE | IXP4XX_OST_ONE_SHOT;
	unsigned long osrt = 0;

	/* period set by 'set next_event' */
	*IXP4XX_OSRT1 = osrt | opts;
	return 0;
}

static int ixp4xx_set_periodic(struct clock_event_device *evt)
{
	unsigned long opts = IXP4XX_OST_ENABLE;
	unsigned long osrt = IXP4XX_LATCH & ~IXP4XX_OST_RELOAD_MASK;

	*IXP4XX_OSRT1 = osrt | opts;
	return 0;
}

static int ixp4xx_resume(struct clock_event_device *evt)
{
	unsigned long opts = *IXP4XX_OSRT1 & IXP4XX_OST_RELOAD_MASK;
	unsigned long osrt = *IXP4XX_OSRT1 & ~IXP4XX_OST_RELOAD_MASK;

	opts |= IXP4XX_OST_ENABLE;
	*IXP4XX_OSRT1 = osrt | opts;
	return 0;
}

static struct clock_event_device clockevent_ixp4xx = {
	.name			= "ixp4xx timer1",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 200,
	.set_state_shutdown	= ixp4xx_shutdown,
	.set_state_periodic	= ixp4xx_set_periodic,
	.set_state_oneshot	= ixp4xx_set_oneshot,
	.tick_resume		= ixp4xx_resume,
	.set_next_event		= ixp4xx_set_next_event,
};

static void __init ixp4xx_clockevent_init(void)
{
	int ret;

	clockevent_ixp4xx.cpumask = cpumask_of(0);
	clockevent_ixp4xx.irq = IRQ_IXP4XX_TIMER1;
	ret = request_irq(IRQ_IXP4XX_TIMER1, ixp4xx_timer_interrupt,
			  IRQF_TIMER, "IXP4XX-TIMER1", &clockevent_ixp4xx);
	if (ret) {
		pr_crit("no timer IRQ\n");
		return;
	}
	clockevents_config_and_register(&clockevent_ixp4xx, IXP4XX_TIMER_FREQ,
					0xf, 0xfffffffe);
}

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
