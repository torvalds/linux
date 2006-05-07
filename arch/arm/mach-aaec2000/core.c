/*
 *  linux/arch/arm/mach-aaec2000/core.c
 *
 *  Code common to all AAEC-2000 machines
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/signal.h>
#include <linux/amba/bus.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include "core.h"
#include "clock.h"

/*
 * Common I/O mapping:
 *
 * Static virtual address mappings are as follow:
 *
 * 0xf8000000-0xf8001ffff: Devices connected to APB bus
 * 0xf8002000-0xf8003ffff: Devices connected to AHB bus
 *
 * Below 0xe8000000 is reserved for vm allocation.
 *
 * The machine specific code must provide the extra mapping beside the
 * default mapping provided here.
 */
static struct map_desc standard_io_desc[] __initdata = {
	{
		.virtual	= VIO_APB_BASE,
		.pfn		= __phys_to_pfn(PIO_APB_BASE),
		.length		= IO_APB_LENGTH,
		.type		= MT_DEVICE
	}, {
		.virtual	= VIO_AHB_BASE,
		.pfn		= __phys_to_pfn(PIO_AHB_BASE),
		.length		= IO_AHB_LENGTH,
		.type		= MT_DEVICE
	}
};

void __init aaec2000_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}

/*
 * Interrupt handling routines
 */
static void aaec2000_int_ack(unsigned int irq)
{
	IRQ_INTSR = 1 << irq;
}

static void aaec2000_int_mask(unsigned int irq)
{
	IRQ_INTENC |= (1 << irq);
}

static void aaec2000_int_unmask(unsigned int irq)
{
	IRQ_INTENS |= (1 << irq);
}

static struct irqchip aaec2000_irq_chip = {
	.ack	= aaec2000_int_ack,
	.mask	= aaec2000_int_mask,
	.unmask	= aaec2000_int_unmask,
};

void __init aaec2000_init_irq(void)
{
	unsigned int i;

	for (i = 0; i < NR_IRQS; i++) {
		set_irq_handler(i, do_level_IRQ);
		set_irq_chip(i, &aaec2000_irq_chip);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Disable all interrupts */
	IRQ_INTENC = 0xffffffff;

	/* Clear any pending interrupts */
	IRQ_INTSR = IRQ_INTSR;
}

/*
 * Time keeping
 */
/* IRQs are disabled before entering here from do_gettimeofday() */
static unsigned long aaec2000_gettimeoffset(void)
{
	unsigned long ticks_to_match, elapsed, usec;

	/* Get ticks before next timer match */
	ticks_to_match = TIMER1_LOAD - TIMER1_VAL;

	/* We need elapsed ticks since last match */
	elapsed = LATCH - ticks_to_match;

	/* Now, convert them to usec */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000))/LATCH;

	return usec;
}

/* We enter here with IRQs enabled */
static irqreturn_t
aaec2000_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/* TODO: Check timer accuracy */
	write_seqlock(&xtime_lock);

	timer_tick(regs);
	TIMER1_CLEAR = 1;

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction aaec2000_timer_irq = {
	.name		= "AAEC-2000 Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= aaec2000_timer_interrupt,
};

static void __init aaec2000_timer_init(void)
{
	/* Disable timer 1 */
	TIMER1_CTRL = 0;

	/* We have somehow to generate a 100Hz clock.
	 * We then use the 508KHz timer in periodic mode.
	 */
	TIMER1_LOAD = LATCH;
	TIMER1_CLEAR = 1; /* Clear interrupt */

	setup_irq(INT_TMR1_OFL, &aaec2000_timer_irq);

	TIMER1_CTRL = TIMER_CTRL_ENABLE |
	                TIMER_CTRL_PERIODIC |
	                TIMER_CTRL_CLKSEL_508K;
}

struct sys_timer aaec2000_timer = {
	.init		= aaec2000_timer_init,
	.offset		= aaec2000_gettimeoffset,
};

static struct clcd_panel mach_clcd_panel;

static int aaec2000_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->panel = &mach_clcd_panel;

	fb->fb.screen_base = dma_alloc_writecombine(&fb->dev->dev, SZ_1M,
			&dma, GFP_KERNEL);

	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = SZ_1M;

	return 0;
}

static int aaec2000_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
			fb->fb.screen_base,
			fb->fb.fix.smem_start,
			fb->fb.fix.smem_len);
}

static void aaec2000_clcd_remove(struct clcd_fb *fb)
{
	dma_free_writecombine(&fb->dev->dev, fb->fb.fix.smem_len,
			fb->fb.screen_base, fb->fb.fix.smem_start);
}

static struct clcd_board clcd_plat_data = {
	.name	= "AAEC-2000",
	.check	= clcdfb_check,
	.decode	= clcdfb_decode,
	.setup	= aaec2000_clcd_setup,
	.mmap	= aaec2000_clcd_mmap,
	.remove	= aaec2000_clcd_remove,
};

static struct amba_device clcd_device = {
	.dev		= {
		.bus_id			= "mb:16",
		.coherent_dma_mask	= ~0,
		.platform_data		= &clcd_plat_data,
	},
	.res		= {
		.start			= AAEC_CLCD_PHYS,
		.end			= AAEC_CLCD_PHYS + SZ_4K - 1,
		.flags			= IORESOURCE_MEM,
	},
	.irq		= { INT_LCD, NO_IRQ },
	.periphid	= 0x41110,
};

static struct amba_device *amba_devs[] __initdata = {
	&clcd_device,
};

static struct clk aaec2000_clcd_clk = {
	.name = "CLCDCLK",
};

void __init aaec2000_set_clcd_plat_data(struct aaec2000_clcd_info *clcd)
{
	clcd_plat_data.enable = clcd->enable;
	clcd_plat_data.disable = clcd->disable;
	memcpy(&mach_clcd_panel, &clcd->panel, sizeof(struct clcd_panel));
}

static struct flash_platform_data aaec2000_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 4,
};

static struct resource aaec2000_flash_resource = {
	.start		= AAEC_FLASH_BASE,
	.end		= AAEC_FLASH_BASE + AAEC_FLASH_SIZE,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device aaec2000_flash_device = {
	.name		= "armflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &aaec2000_flash_data,
	},
	.num_resources	= 1,
	.resource	= &aaec2000_flash_resource,
};

static int __init aaec2000_init(void)
{
	int i;

	clk_register(&aaec2000_clcd_clk);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	platform_device_register(&aaec2000_flash_device);

	return 0;
};
arch_initcall(aaec2000_init);

