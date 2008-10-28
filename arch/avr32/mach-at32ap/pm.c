/*
 * AVR32 AP Power Management
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/sysreg.h>

#include <mach/chip.h>
#include <mach/pm.h>
#include <mach/sram.h>

#include "sdramc.h"

#define SRAM_PAGE_FLAGS	(SYSREG_BIT(TLBELO_D) | SYSREG_BF(SZ, 1)	\
				| SYSREG_BF(AP, 3) | SYSREG_BIT(G))


static unsigned long	pm_sram_start;
static size_t		pm_sram_size;
static struct vm_struct	*pm_sram_area;

static void (*avr32_pm_enter_standby)(unsigned long sdramc_base);
static void (*avr32_pm_enter_str)(unsigned long sdramc_base);

/*
 * Must be called with interrupts disabled. Exceptions will be masked
 * on return (i.e. all exceptions will be "unrecoverable".)
 */
static void *avr32_pm_map_sram(void)
{
	unsigned long	vaddr;
	unsigned long	page_addr;
	u32		tlbehi;
	u32		mmucr;

	vaddr = (unsigned long)pm_sram_area->addr;
	page_addr = pm_sram_start & PAGE_MASK;

	/*
	 * Mask exceptions and grab the first TLB entry. We won't be
	 * needing it while sleeping.
	 */
	asm volatile("ssrf	%0" : : "i"(SYSREG_EM_OFFSET) : "memory");

	mmucr = sysreg_read(MMUCR);
	tlbehi = sysreg_read(TLBEHI);
	sysreg_write(MMUCR, SYSREG_BFINS(DRP, 0, mmucr));

	tlbehi = SYSREG_BF(ASID, SYSREG_BFEXT(ASID, tlbehi));
	tlbehi |= vaddr & PAGE_MASK;
	tlbehi |= SYSREG_BIT(TLBEHI_V);

	sysreg_write(TLBELO, page_addr | SRAM_PAGE_FLAGS);
	sysreg_write(TLBEHI, tlbehi);
	__builtin_tlbw();

	return (void *)(vaddr + pm_sram_start - page_addr);
}

/*
 * Must be called with interrupts disabled. Exceptions will be
 * unmasked on return.
 */
static void avr32_pm_unmap_sram(void)
{
	u32	mmucr;
	u32	tlbehi;
	u32	tlbarlo;

	/* Going to update TLB entry at index 0 */
	mmucr = sysreg_read(MMUCR);
	tlbehi = sysreg_read(TLBEHI);
	sysreg_write(MMUCR, SYSREG_BFINS(DRP, 0, mmucr));

	/* Clear the "valid" bit */
	tlbehi = SYSREG_BF(ASID, SYSREG_BFEXT(ASID, tlbehi));
	sysreg_write(TLBEHI, tlbehi);

	/* Mark it as "not accessed" */
	tlbarlo = sysreg_read(TLBARLO);
	sysreg_write(TLBARLO, tlbarlo | 0x80000000U);

	/* Update the TLB */
	__builtin_tlbw();

	/* Unmask exceptions */
	asm volatile("csrf	%0" : : "i"(SYSREG_EM_OFFSET) : "memory");
}

static int avr32_pm_valid_state(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_ON:
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;

	default:
		return 0;
	}
}

static int avr32_pm_enter(suspend_state_t state)
{
	u32		lpr_saved;
	u32		evba_saved;
	void		*sram;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		sram = avr32_pm_map_sram();

		/* Switch to in-sram exception handlers */
		evba_saved = sysreg_read(EVBA);
		sysreg_write(EVBA, (unsigned long)sram);

		/*
		 * Save the LPR register so that we can re-enable
		 * SDRAM Low Power mode on resume.
		 */
		lpr_saved = sdramc_readl(LPR);
		pr_debug("%s: Entering standby...\n", __func__);
		avr32_pm_enter_standby(SDRAMC_BASE);
		sdramc_writel(LPR, lpr_saved);

		/* Switch back to regular exception handlers */
		sysreg_write(EVBA, evba_saved);

		avr32_pm_unmap_sram();
		break;

	case PM_SUSPEND_MEM:
		sram = avr32_pm_map_sram();

		/* Switch to in-sram exception handlers */
		evba_saved = sysreg_read(EVBA);
		sysreg_write(EVBA, (unsigned long)sram);

		/*
		 * Save the LPR register so that we can re-enable
		 * SDRAM Low Power mode on resume.
		 */
		lpr_saved = sdramc_readl(LPR);
		pr_debug("%s: Entering suspend-to-ram...\n", __func__);
		avr32_pm_enter_str(SDRAMC_BASE);
		sdramc_writel(LPR, lpr_saved);

		/* Switch back to regular exception handlers */
		sysreg_write(EVBA, evba_saved);

		avr32_pm_unmap_sram();
		break;

	case PM_SUSPEND_ON:
		pr_debug("%s: Entering idle...\n", __func__);
		cpu_enter_idle();
		break;

	default:
		pr_debug("%s: Invalid suspend state %d\n", __func__, state);
		goto out;
	}

	pr_debug("%s: wakeup\n", __func__);

out:
	return 0;
}

static struct platform_suspend_ops avr32_pm_ops = {
	.valid	= avr32_pm_valid_state,
	.enter	= avr32_pm_enter,
};

static unsigned long avr32_pm_offset(void *symbol)
{
	extern u8 pm_exception[];

	return (unsigned long)symbol - (unsigned long)pm_exception;
}

static int __init avr32_pm_init(void)
{
	extern u8 pm_exception[];
	extern u8 pm_irq0[];
	extern u8 pm_standby[];
	extern u8 pm_suspend_to_ram[];
	extern u8 pm_sram_end[];
	void *dst;

	/*
	 * To keep things simple, we depend on not needing more than a
	 * single page.
	 */
	pm_sram_size = avr32_pm_offset(pm_sram_end);
	if (pm_sram_size > PAGE_SIZE)
		goto err;

	pm_sram_start = sram_alloc(pm_sram_size);
	if (!pm_sram_start)
		goto err_alloc_sram;

	/* Grab a virtual area we can use later on. */
	pm_sram_area = get_vm_area(pm_sram_size, VM_IOREMAP);
	if (!pm_sram_area)
		goto err_vm_area;
	pm_sram_area->phys_addr = pm_sram_start;

	local_irq_disable();
	dst = avr32_pm_map_sram();
	memcpy(dst, pm_exception, pm_sram_size);
	flush_dcache_region(dst, pm_sram_size);
	invalidate_icache_region(dst, pm_sram_size);
	avr32_pm_unmap_sram();
	local_irq_enable();

	avr32_pm_enter_standby = dst + avr32_pm_offset(pm_standby);
	avr32_pm_enter_str = dst + avr32_pm_offset(pm_suspend_to_ram);
	intc_set_suspend_handler(avr32_pm_offset(pm_irq0));

	suspend_set_ops(&avr32_pm_ops);

	printk("AVR32 AP Power Management enabled\n");

	return 0;

err_vm_area:
	sram_free(pm_sram_start, pm_sram_size);
err_alloc_sram:
err:
	pr_err("AVR32 Power Management initialization failed\n");
	return -ENOMEM;
}
arch_initcall(avr32_pm_init);
