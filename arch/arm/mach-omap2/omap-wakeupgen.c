/*
 * OMAP WakeupGen Source file
 *
 * OMAP WakeupGen is the interrupt controller extension used along
 * with ARM GIC to wake the CPU out from low power states on
 * external interrupts. It is responsible for generating wakeup
 * event from the incoming interrupts and enable bits. It is
 * implemented in MPU always ON power domain. During normal operation,
 * WakeupGen delivers external interrupts directly to the GIC.
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>

#include <asm/hardware/gic.h>

#include <mach/omap-wakeupgen.h>

#define NR_REG_BANKS		4
#define MAX_IRQS		128
#define WKG_MASK_ALL		0x00000000
#define WKG_UNMASK_ALL		0xffffffff
#define CPU_ENA_OFFSET		0x400
#define CPU0_ID			0x0
#define CPU1_ID			0x1

static void __iomem *wakeupgen_base;
static DEFINE_PER_CPU(u32 [NR_REG_BANKS], irqmasks);
static DEFINE_SPINLOCK(wakeupgen_lock);
static unsigned int irq_target_cpu[NR_IRQS];

/*
 * Static helper functions.
 */
static inline u32 wakeupgen_readl(u8 idx, u32 cpu)
{
	return __raw_readl(wakeupgen_base + OMAP_WKG_ENB_A_0 +
				(cpu * CPU_ENA_OFFSET) + (idx * 4));
}

static inline void wakeupgen_writel(u32 val, u8 idx, u32 cpu)
{
	__raw_writel(val, wakeupgen_base + OMAP_WKG_ENB_A_0 +
				(cpu * CPU_ENA_OFFSET) + (idx * 4));
}

static void _wakeupgen_set_all(unsigned int cpu, unsigned int reg)
{
	u8 i;

	for (i = 0; i < NR_REG_BANKS; i++)
		wakeupgen_writel(reg, i, cpu);
}

static inline int _wakeupgen_get_irq_info(u32 irq, u32 *bit_posn, u8 *reg_index)
{
	unsigned int spi_irq;

	/*
	 * PPIs and SGIs are not supported.
	 */
	if (irq < OMAP44XX_IRQ_GIC_START)
		return -EINVAL;

	/*
	 * Subtract the GIC offset.
	 */
	spi_irq = irq - OMAP44XX_IRQ_GIC_START;
	if (spi_irq > MAX_IRQS) {
		pr_err("omap wakeupGen: Invalid IRQ%d\n", irq);
		return -EINVAL;
	}

	/*
	 * Each WakeupGen register controls 32 interrupt.
	 * i.e. 1 bit per SPI IRQ
	 */
	*reg_index = spi_irq >> 5;
	*bit_posn = spi_irq %= 32;

	return 0;
}

static void _wakeupgen_clear(unsigned int irq, unsigned int cpu)
{
	u32 val, bit_number;
	u8 i;

	if (_wakeupgen_get_irq_info(irq, &bit_number, &i))
		return;

	val = wakeupgen_readl(i, cpu);
	val &= ~BIT(bit_number);
	wakeupgen_writel(val, i, cpu);
}

static void _wakeupgen_set(unsigned int irq, unsigned int cpu)
{
	u32 val, bit_number;
	u8 i;

	if (_wakeupgen_get_irq_info(irq, &bit_number, &i))
		return;

	val = wakeupgen_readl(i, cpu);
	val |= BIT(bit_number);
	wakeupgen_writel(val, i, cpu);
}

static void _wakeupgen_save_masks(unsigned int cpu)
{
	u8 i;

	for (i = 0; i < NR_REG_BANKS; i++)
		per_cpu(irqmasks, cpu)[i] = wakeupgen_readl(i, cpu);
}

static void _wakeupgen_restore_masks(unsigned int cpu)
{
	u8 i;

	for (i = 0; i < NR_REG_BANKS; i++)
		wakeupgen_writel(per_cpu(irqmasks, cpu)[i], i, cpu);
}

/*
 * Architecture specific Mask extension
 */
static void wakeupgen_mask(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeupgen_lock, flags);
	_wakeupgen_clear(d->irq, irq_target_cpu[d->irq]);
	spin_unlock_irqrestore(&wakeupgen_lock, flags);
}

/*
 * Architecture specific Unmask extension
 */
static void wakeupgen_unmask(struct irq_data *d)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeupgen_lock, flags);
	_wakeupgen_set(d->irq, irq_target_cpu[d->irq]);
	spin_unlock_irqrestore(&wakeupgen_lock, flags);
}

/*
 * Mask or unmask all interrupts on given CPU.
 *	0 = Mask all interrupts on the 'cpu'
 *	1 = Unmask all interrupts on the 'cpu'
 * Ensure that the initial mask is maintained. This is faster than
 * iterating through GIC registers to arrive at the correct masks.
 */
static void wakeupgen_irqmask_all(unsigned int cpu, unsigned int set)
{
	unsigned long flags;

	spin_lock_irqsave(&wakeupgen_lock, flags);
	if (set) {
		_wakeupgen_save_masks(cpu);
		_wakeupgen_set_all(cpu, WKG_MASK_ALL);
	} else {
		_wakeupgen_set_all(cpu, WKG_UNMASK_ALL);
		_wakeupgen_restore_masks(cpu);
	}
	spin_unlock_irqrestore(&wakeupgen_lock, flags);
}

/*
 * Initialise the wakeupgen module.
 */
int __init omap_wakeupgen_init(void)
{
	int i;
	unsigned int boot_cpu = smp_processor_id();

	/* Not supported on OMAP4 ES1.0 silicon */
	if (omap_rev() == OMAP4430_REV_ES1_0) {
		WARN(1, "WakeupGen: Not supported on OMAP4430 ES1.0\n");
		return -EPERM;
	}

	/* Static mapping, never released */
	wakeupgen_base = ioremap(OMAP44XX_WKUPGEN_BASE, SZ_4K);
	if (WARN_ON(!wakeupgen_base))
		return -ENOMEM;

	/* Clear all IRQ bitmasks at wakeupGen level */
	for (i = 0; i < NR_REG_BANKS; i++) {
		wakeupgen_writel(0, i, CPU0_ID);
		wakeupgen_writel(0, i, CPU1_ID);
	}

	/*
	 * Override GIC architecture specific functions to add
	 * OMAP WakeupGen interrupt controller along with GIC
	 */
	gic_arch_extn.irq_mask = wakeupgen_mask;
	gic_arch_extn.irq_unmask = wakeupgen_unmask;
	gic_arch_extn.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE;

	/*
	 * FIXME: Add support to set_smp_affinity() once the core
	 * GIC code has necessary hooks in place.
	 */

	/* Associate all the IRQs to boot CPU like GIC init does. */
	for (i = 0; i < NR_IRQS; i++)
		irq_target_cpu[i] = boot_cpu;

	return 0;
}
