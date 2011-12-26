/*
 * arch/arch/mach-sun5i/core.c
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * SUN5I machine core implementations
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl061.h>
#include <linux/amba/mmci.h>
#include <linux/amba/pl022.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/clockchips.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>

#include <asm/clkdev.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/hardware/arm_timer.h>
#include <asm/hardware/icst.h>
#include <asm/hardware/vic.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/system.h>
#include <mach/timex.h>
#include <mach/sys_config.h>

/**
 * Machine Implementations
 *
 */

static struct map_desc sw_io_desc[] __initdata = {
	{ SW_VA_SRAM_BASE, __phys_to_pfn(SW_PA_SRAM_BASE),  (SZ_128K + SZ_64K), MT_MEMORY_ITCM  },
	{ SW_VA_IO_BASE,   __phys_to_pfn(SW_PA_IO_BASE),    (SZ_1M + SZ_2M),    MT_DEVICE    },
};

void __init sw_core_map_io(void)
{
	iotable_init(sw_io_desc, ARRAY_SIZE(sw_io_desc));
}

static u32 DRAMC_get_dram_size(void)
{
	u32 reg_val;
	u32 dram_size;
	u32 chip_den;

	reg_val = readl(SW_DRAM_SDR_DCR);
	chip_den = (reg_val >> 3) & 0x7;
	if(chip_den == 0)
		dram_size = 32;
	else if(chip_den == 1)
		dram_size = 64;
	else if(chip_den == 2)
		dram_size = 128;
	else if(chip_den == 3)
		dram_size = 256;
	else if(chip_den == 4)
		dram_size = 512;
	else
		dram_size = 1024;

	if( ((reg_val>>1)&0x3) == 0x1)
		dram_size<<=1;
	if( ((reg_val>>6)&0x7) == 0x3)
		dram_size<<=1;
	if( ((reg_val>>10)&0x3) == 0x1)
		dram_size<<=1;

	return dram_size;
}

static void __init sw_core_fixup(struct machine_desc *desc,
                  struct tag *tags, char **cmdline,
                  struct meminfo *mi)
{
	u32 size;

#ifdef CONFIG_SUN5I_FPGA
	size = 256;
	mi->nr_banks = 1;
	mi->bank[0].start = 0x40000000;
	mi->bank[0].size = SZ_1M * size;
#else
	size = DRAMC_get_dram_size();
	early_printk("DRAM: %d", size);

	if (size <= 512) {
		mi->nr_banks = 1;
		mi->bank[0].start = 0x40000000;
		mi->bank[0].size = SZ_1M * (size - 64);
	} else {
		mi->nr_banks = 2;
		mi->bank[0].start = 0x40000000;
		mi->bank[0].size = SZ_1M * (512 - 64);
		mi->bank[1].start = 0x60000000;
		mi->bank[1].size = SZ_1M * (size - 512);
	}
#endif

	pr_info("Total Detected Memory: %uMB with %d banks\n", size, mi->nr_banks);
}

unsigned long fb_start = (PLAT_PHYS_OFFSET + SZ_512M - SZ_64M - SZ_32M);
unsigned long fb_size = SZ_32M;
EXPORT_SYMBOL(fb_start);
EXPORT_SYMBOL(fb_size);

unsigned long g2d_start = (PLAT_PHYS_OFFSET + SZ_512M - SZ_128M);
unsigned long g2d_size = SZ_1M * 16;
EXPORT_SYMBOL(g2d_start);
EXPORT_SYMBOL(g2d_size);

unsigned long ve_start = (PLAT_PHYS_OFFSET + SZ_64M);
unsigned long ve_size = (SZ_64M + SZ_16M);
EXPORT_SYMBOL(ve_start);
EXPORT_SYMBOL(ve_size);

static void __init sw_core_reserve(void)
{
	memblock_reserve(SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE);
#ifdef CONFIG_SUN5I_FPGA
#else
	memblock_reserve(fb_start, fb_size);
	memblock_reserve(ve_start, SZ_64M);
	memblock_reserve(ve_start + SZ_64M, SZ_16M);

#if 0
        int g2d_used = 0;
        char *script_base = (char *)(PAGE_OFFSET + 0x3000000);

        g2d_used = sw_cfg_get_int(script_base, "g2d_para", "g2d_used");

	memblock_reserve(fb_start, fb_size);
	memblock_reserve(SYS_CONFIG_MEMBASE, SYS_CONFIG_MEMSIZE);
	memblock_reserve(ve_start, ve_start);

        if (g2d_used) {
                g2d_size = sw_cfg_get_int(script_base, "g2d_para", "g2d_size");
                if (g2d_size < 0 || g2d_size > SW_G2D_MEM_MAX) {
                        g2d_size = SW_G2D_MEM_MAX;
                }
                g2d_start = SW_G2D_MEM_BASE;
                g2d_size = g2d_size;
                memblock_reserve(g2d_start, g2d_size);
        }

#endif
	pr_info("Memory Reserved(in bytes):\n");
	pr_info("\tLCD: 0x%08x, 0x%08x\n", (unsigned int)fb_start, (unsigned int)fb_size);
	pr_info("\tSYS: 0x%08x, 0x%08x\n", (unsigned int)SYS_CONFIG_MEMBASE, (unsigned int)SYS_CONFIG_MEMSIZE);
	pr_info("\tG2D: 0x%08x, 0x%08x\n", (unsigned int)g2d_start, (unsigned int)g2d_size);
	pr_info("\tVE : 0x%08x, 0x%08x\n", (unsigned int)ve_start, (unsigned int)ve_size);
#endif
}

void sw_irq_ack(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if (irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) & ~(1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) | (1 << irq), SW_INT_MASK_REG0);
		writel(readl(SW_INT_IRQ_PENDING_REG0) | (1<<irq), SW_INT_IRQ_PENDING_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) & ~(1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) | (1 << irq), SW_INT_MASK_REG1);
		writel(readl(SW_INT_IRQ_PENDING_REG1) | (1<<irq), SW_INT_IRQ_PENDING_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) & ~(1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) | (1 << irq), SW_INT_MASK_REG2);
		writel(readl(SW_INT_IRQ_PENDING_REG2) | (1<<irq), SW_INT_IRQ_PENDING_REG2);
	}
}

/* Mask an IRQ line, which means disabling the IRQ line */
static void sw_irq_mask(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if(irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) & ~(1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) | (1 << irq), SW_INT_MASK_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) & ~(1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) | (1 << irq), SW_INT_MASK_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) & ~(1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) | (1 << irq), SW_INT_MASK_REG2);
	}
}

static void sw_irq_unmask(struct irq_data *irqd)
{
	unsigned int irq = irqd->irq;

	if(irq < 32){
		writel(readl(SW_INT_ENABLE_REG0) | (1<<irq), SW_INT_ENABLE_REG0);
		writel(readl(SW_INT_MASK_REG0) & ~(1 << irq), SW_INT_MASK_REG0);
		if(irq == SW_INT_IRQNO_ENMI) /* must clear pending bit when enabled */
			writel((1 << SW_INT_IRQNO_ENMI), SW_INT_IRQ_PENDING_REG0);
	} else if(irq < 64){
		irq -= 32;
		writel(readl(SW_INT_ENABLE_REG1) | (1<<irq), SW_INT_ENABLE_REG1);
		writel(readl(SW_INT_MASK_REG1) & ~(1 << irq), SW_INT_MASK_REG1);
	} else if(irq < 96){
		irq -= 64;
		writel(readl(SW_INT_ENABLE_REG2) | (1<<irq), SW_INT_ENABLE_REG2);
		writel(readl(SW_INT_MASK_REG2) & ~(1 << irq), SW_INT_MASK_REG2);
	}
}

static struct irq_chip sw_vic_chip = {
	.name       = "sw_vic",
	.irq_ack    = sw_irq_ack,
	.irq_mask   = sw_irq_mask,
	.irq_unmask = sw_irq_unmask,
};

void __init sw_core_init_irq(void)
{
	u32 i = 0;

	/* Disable & clear all interrupts */
	writel(0, SW_INT_ENABLE_REG0);
	writel(0, SW_INT_ENABLE_REG1);
	writel(0, SW_INT_ENABLE_REG2);

	writel(0xffffffff, SW_INT_MASK_REG0);
	writel(0xffffffff, SW_INT_MASK_REG1);
	writel(0xffffffff, SW_INT_MASK_REG2);

	writel(0xffffffff, SW_INT_IRQ_PENDING_REG0);
	writel(0xffffffff, SW_INT_IRQ_PENDING_REG1);
	writel(0xffffffff, SW_INT_IRQ_PENDING_REG2);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG0);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG1);
	writel(0xffffffff, SW_INT_FIQ_PENDING_REG2);
	
	/*enable protection mode*/
	writel(0x01, SW_INT_PROTECTION_REG);
	/*config the external interrupt source type*/
	writel(0x00, SW_INT_NMI_CTRL_REG);

	for (i = SW_INT_START; i < SW_INT_END; i++) {
		irq_set_chip(i, &sw_vic_chip);
		irq_set_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}



/**
 * Global vars definitions
 *
 */
static void timer_set_mode(enum clock_event_mode mode, struct clock_event_device *clk)
{
	volatile u32 ctrl;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_info("timer0: Periodic Mode\n");
		writel(TMR_INTER_VAL, SW_TIMER0_INTVAL_REG); /* interval (999+1) */
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl &= ~(1<<7);    /* Continuous mode */
		ctrl |= 1;  /* Enable this timer */
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		pr_info("timer0: Oneshot Mode\n");
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl |= (1<<7);     /* Single mode */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = readl(SW_TIMER0_CTL_REG);
		ctrl &= ~(1<<0);    /* Disable timer0 */
		break;
	}

	writel(ctrl, SW_TIMER0_CTL_REG);
}

/* Useless when periodic mode */
static int timer_set_next_event(unsigned long evt, struct clock_event_device *unused)
{
	volatile u32 ctrl;

	/* clear any pending before continue */
	ctrl = readl(SW_TIMER0_CTL_REG);
	writel(evt, SW_TIMER0_CNTVAL_REG);
	ctrl |= (1<<1);
	writel(ctrl, SW_TIMER0_CTL_REG);
	writel(ctrl | 0x1, SW_TIMER0_CTL_REG);

	return 0;
}

static struct clock_event_device timer0_clockevent = {
	.name = "timer0",
	.shift = 32,
	.rating = 300,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = timer_set_mode,
	.set_next_event = timer_set_next_event,
};


static irqreturn_t sw_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;

	writel(0x1, SW_TIMER_INT_STA_REG);

	/*
 	 * timer_set_next_event will be called only in ONESHOT mode
 	 */
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction sw_timer_irq = {
	.name = "timer0",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = sw_timer_interrupt,
	.dev_id = &timer0_clockevent,
	.irq = SW_INT_IRQNO_TIMER0,
};


static void __init sw_timer_init(void)
{
	int ret;
	volatile u32  val = 0;

	writel(TMR_INTER_VAL, SW_TIMER0_INTVAL_REG);
	/* set clock sourch to HOSC, 16 pre-division */
	val = readl(SW_TIMER0_CTL_REG);
	val &= ~(0x07<<4);
	val &= ~(0x03<<2);
	val |= (4<<4) | (1<<2);
	writel(val, SW_TIMER0_CTL_REG);
	/* set mode to auto reload */
	val = readl(SW_TIMER0_CTL_REG);
	val |= (1<<1);
	writel(val, SW_TIMER0_CTL_REG);

	ret = setup_irq(SW_INT_IRQNO_TIMER0, &sw_timer_irq);
	if (ret) {
		pr_warning("failed to setup irq %d\n", SW_INT_IRQNO_TIMER0);
	}

	/* Enable time0 interrupt */
	val = readl(SW_TIMER_INT_CTL_REG);
	val |= (1<<0);
	writel(val, SW_TIMER_INT_CTL_REG);

	timer0_clockevent.mult = div_sc(SYS_TIMER_CLKSRC/SYS_TIMER_SCAL, NSEC_PER_SEC, timer0_clockevent.shift);
	timer0_clockevent.max_delta_ns = clockevent_delta2ns(0xff, &timer0_clockevent);
	timer0_clockevent.min_delta_ns = clockevent_delta2ns(0x1, &timer0_clockevent);
	timer0_clockevent.cpumask = cpumask_of(0);
	timer0_clockevent.irq = sw_timer_irq.irq;
	clockevents_register_device(&timer0_clockevent);
}

struct sys_timer sw_sys_timer = {
	.init = sw_timer_init,
};

extern void __init sw_pdev_init(void);
void __init sw_core_init(void)
{
	sw_pdev_init();
}
enum sw_ic_ver sw_get_ic_ver(void)
{
	volatile u32 val = readl(SW_VA_TIMERC_IO_BASE + 0x13c);

	val = (val >> 6) & 0x3;

	if (val == 0x3) {
		return MAGIC_VER_B;
	}

	return MAGIC_VER_A;
}
EXPORT_SYMBOL(sw_get_ic_ver);
/**
 * Arch Required Implementations
 *
 */
//void arch_idle(void)
//{

//}

//void arch_reset(char mode, const char *cmd)
//{


//}


MACHINE_START(SUN5I, "sun5i")
	.boot_params    = PLAT_PHYS_OFFSET + 0x100,
	.timer          = &sw_sys_timer,
	.fixup          = sw_core_fixup,
	.map_io         = sw_core_map_io,
	.init_early     = NULL,
	.init_irq       = sw_core_init_irq,
	.init_machine   = sw_core_init,
	.reserve        = sw_core_reserve,
MACHINE_END

