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
#include <linux/notifier.h>
#include <linux/cpu_pm.h>

#include <asm/hardware/gic.h>

#include <mach/omap-wakeupgen.h>
#include <mach/omap-secure.h>

#include "omap4-sar-layout.h"
#include "common.h"

#define MAX_NR_REG_BANKS	5
#define MAX_IRQS		160
#define WKG_MASK_ALL		0x00000000
#define WKG_UNMASK_ALL		0xffffffff
#define CPU_ENA_OFFSET		0x400
#define CPU0_ID			0x0
#define CPU1_ID			0x1
#define OMAP4_NR_BANKS		4
#define OMAP4_NR_IRQS		128

static void __iomem *wakeupgen_base;
static void __iomem *sar_base;
static DEFINE_SPINLOCK(wakeupgen_lock);
static unsigned int irq_target_cpu[NR_IRQS];
static unsigned int irq_banks = MAX_NR_REG_BANKS;
static unsigned int max_irqs = MAX_IRQS;
static unsigned int omap_secure_apis;

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

static inline void sar_writel(u32 val, u32 offset, u8 idx)
{
	__raw_writel(val, sar_base + offset + (idx * 4));
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

#ifdef CONFIG_HOTPLUG_CPU
static DEFINE_PER_CPU(u32 [MAX_NR_REG_BANKS], irqmasks);

static void _wakeupgen_save_masks(unsigned int cpu)
{
	u8 i;

	for (i = 0; i < irq_banks; i++)
		per_cpu(irqmasks, cpu)[i] = wakeupgen_readl(i, cpu);
}

static void _wakeupgen_restore_masks(unsigned int cpu)
{
	u8 i;

	for (i = 0; i < irq_banks; i++)
		wakeupgen_writel(per_cpu(irqmasks, cpu)[i], i, cpu);
}

static void _wakeupgen_set_all(unsigned int cpu, unsigned int reg)
{
	u8 i;

	for (i = 0; i < irq_banks; i++)
		wakeupgen_writel(reg, i, cpu);
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
#endif

#ifdef CONFIG_CPU_PM
static inline void omap4_irq_save_context(void)
{
	u32 i, val;

	if (omap_rev() == OMAP4430_REV_ES1_0)
		return;

	for (i = 0; i < irq_banks; i++) {
		/* Save the CPUx interrupt mask for IRQ 0 to 127 */
		val = wakeupgen_readl(i, 0);
		sar_writel(val, WAKEUPGENENB_OFFSET_CPU0, i);
		val = wakeupgen_readl(i, 1);
		sar_writel(val, WAKEUPGENENB_OFFSET_CPU1, i);

		/*
		 * Disable the secure interrupts for CPUx. The restore
		 * code blindly restores secure and non-secure interrupt
		 * masks from SAR RAM. Secure interrupts are not suppose
		 * to be enabled from HLOS. So overwrite the SAR location
		 * so that the secure interrupt remains disabled.
		 */
		sar_writel(0x0, WAKEUPGENENB_SECURE_OFFSET_CPU0, i);
		sar_writel(0x0, WAKEUPGENENB_SECURE_OFFSET_CPU1, i);
	}

	/* Save AuxBoot* registers */
	val = __raw_readl(wakeupgen_base + OMAP_AUX_CORE_BOOT_0);
	__raw_writel(val, sar_base + AUXCOREBOOT0_OFFSET);
	val = __raw_readl(wakeupgen_base + OMAP_AUX_CORE_BOOT_1);
	__raw_writel(val, sar_base + AUXCOREBOOT1_OFFSET);

	/* Save SyncReq generation logic */
	val = __raw_readl(wakeupgen_base + OMAP_PTMSYNCREQ_MASK);
	__raw_writel(val, sar_base + PTMSYNCREQ_MASK_OFFSET);
	val = __raw_readl(wakeupgen_base + OMAP_PTMSYNCREQ_EN);
	__raw_writel(val, sar_base + PTMSYNCREQ_EN_OFFSET);

	/* Set the Backup Bit Mask status */
	val = __raw_readl(sar_base + SAR_BACKUP_STATUS_OFFSET);
	val |= SAR_BACKUP_STATUS_WAKEUPGEN;
	__raw_writel(val, sar_base + SAR_BACKUP_STATUS_OFFSET);

}

static inline void omap5_irq_save_context(void)
{
	u32 i, val;

	for (i = 0; i < irq_banks; i++) {
		/* Save the CPUx interrupt mask for IRQ 0 to 159 */
		val = wakeupgen_readl(i, 0);
		sar_writel(val, OMAP5_WAKEUPGENENB_OFFSET_CPU0, i);
		val = wakeupgen_readl(i, 1);
		sar_writel(val, OMAP5_WAKEUPGENENB_OFFSET_CPU1, i);
		sar_writel(0x0, OMAP5_WAKEUPGENENB_SECURE_OFFSET_CPU0, i);
		sar_writel(0x0, OMAP5_WAKEUPGENENB_SECURE_OFFSET_CPU1, i);
	}

	/* Save AuxBoot* registers */
	val = __raw_readl(wakeupgen_base + OMAP_AUX_CORE_BOOT_0);
	__raw_writel(val, sar_base + OMAP5_AUXCOREBOOT0_OFFSET);
	val = __raw_readl(wakeupgen_base + OMAP_AUX_CORE_BOOT_0);
	__raw_writel(val, sar_base + OMAP5_AUXCOREBOOT1_OFFSET);

	/* Set the Backup Bit Mask status */
	val = __raw_readl(sar_base + OMAP5_SAR_BACKUP_STATUS_OFFSET);
	val |= SAR_BACKUP_STATUS_WAKEUPGEN;
	__raw_writel(val, sar_base + OMAP5_SAR_BACKUP_STATUS_OFFSET);

}

/*
 * Save WakeupGen interrupt context in SAR BANK3. Restore is done by
 * ROM code. WakeupGen IP is integrated along with GIC to manage the
 * interrupt wakeups from CPU low power states. It manages
 * masking/unmasking of Shared peripheral interrupts(SPI). So the
 * interrupt enable/disable control should be in sync and consistent
 * at WakeupGen and GIC so that interrupts are not lost.
 */
static void irq_save_context(void)
{
	if (!sar_base)
		sar_base = omap4_get_sar_ram_base();

	if (soc_is_omap54xx())
		omap5_irq_save_context();
	else
		omap4_irq_save_context();
}

/*
 * Clear WakeupGen SAR backup status.
 */
static void irq_sar_clear(void)
{
	u32 val;
	u32 offset = SAR_BACKUP_STATUS_OFFSET;

	if (soc_is_omap54xx())
		offset = OMAP5_SAR_BACKUP_STATUS_OFFSET;

	val = __raw_readl(sar_base + offset);
	val &= ~SAR_BACKUP_STATUS_WAKEUPGEN;
	__raw_writel(val, sar_base + offset);
}

/*
 * Save GIC and Wakeupgen interrupt context using secure API
 * for HS/EMU devices.
 */
static void irq_save_secure_context(void)
{
	u32 ret;
	ret = omap_secure_dispatcher(OMAP4_HAL_SAVEGIC_INDEX,
				FLAG_START_CRITICAL,
				0, 0, 0, 0, 0);
	if (ret != API_HAL_RET_VALUE_OK)
		pr_err("GIC and Wakeupgen context save failed\n");
}
#endif

#ifdef CONFIG_HOTPLUG_CPU
static int __cpuinit irq_cpu_hotplug_notify(struct notifier_block *self,
					 unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)hcpu;

	switch (action) {
	case CPU_ONLINE:
		wakeupgen_irqmask_all(cpu, 0);
		break;
	case CPU_DEAD:
		wakeupgen_irqmask_all(cpu, 1);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata irq_hotplug_notifier = {
	.notifier_call = irq_cpu_hotplug_notify,
};

static void __init irq_hotplug_init(void)
{
	register_hotcpu_notifier(&irq_hotplug_notifier);
}
#else
static void __init irq_hotplug_init(void)
{}
#endif

#ifdef CONFIG_CPU_PM
static int irq_notifier(struct notifier_block *self, unsigned long cmd,	void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		if (omap_type() == OMAP2_DEVICE_TYPE_GP)
			irq_save_context();
		else
			irq_save_secure_context();
		break;
	case CPU_CLUSTER_PM_EXIT:
		if (omap_type() == OMAP2_DEVICE_TYPE_GP)
			irq_sar_clear();
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block irq_notifier_block = {
	.notifier_call = irq_notifier,
};

static void __init irq_pm_init(void)
{
	/* FIXME: Remove this when MPU OSWR support is added */
	if (!soc_is_omap54xx())
		cpu_pm_register_notifier(&irq_notifier_block);
}
#else
static void __init irq_pm_init(void)
{}
#endif

void __iomem *omap_get_wakeupgen_base(void)
{
	return wakeupgen_base;
}

int omap_secure_apis_support(void)
{
	return omap_secure_apis;
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
	wakeupgen_base = ioremap(OMAP_WKUPGEN_BASE, SZ_4K);
	if (WARN_ON(!wakeupgen_base))
		return -ENOMEM;

	if (cpu_is_omap44xx()) {
		irq_banks = OMAP4_NR_BANKS;
		max_irqs = OMAP4_NR_IRQS;
		omap_secure_apis = 1;
	}

	/* Clear all IRQ bitmasks at wakeupGen level */
	for (i = 0; i < irq_banks; i++) {
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
	for (i = 0; i < max_irqs; i++)
		irq_target_cpu[i] = boot_cpu;

	irq_hotplug_init();
	irq_pm_init();

	return 0;
}
