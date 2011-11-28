/* linux/arch/arm/plat-samsung/include/plat/sysmmu.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung System MMU driver for S5P platform
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_SYSMMU_H
#define __PLAT_SAMSUNG_SYSMMU_H __FILE__

enum S5P_SYSMMU_INTERRUPT_TYPE {
	SYSMMU_PAGEFAULT,
	SYSMMU_AR_MULTIHIT,
	SYSMMU_AW_MULTIHIT,
	SYSMMU_BUSERROR,
	SYSMMU_AR_SECURITY,
	SYSMMU_AR_ACCESS,
	SYSMMU_AW_SECURITY,
	SYSMMU_AW_PROTECTION, /* 7 */
	SYSMMU_FAULTS_NUM
};

#ifdef CONFIG_S5P_SYSTEM_MMU

#include <mach/sysmmu.h>

/**
 * s5p_sysmmu_enable() - enable system mmu of ip
 * @ips: The ip connected system mmu.
 * #pgd: Base physical address of the 1st level page table
 *
 * This function enable system mmu to transfer address
 * from virtual address to physical address
 */
void s5p_sysmmu_enable(sysmmu_ips ips, unsigned long pgd);

/**
 * s5p_sysmmu_disable() - disable sysmmu mmu of ip
 * @ips: The ip connected system mmu.
 *
 * This function disable system mmu to transfer address
 * from virtual address to physical address
 */
void s5p_sysmmu_disable(sysmmu_ips ips);

/**
 * s5p_sysmmu_set_tablebase_pgd() - set page table base address to refer page table
 * @ips: The ip connected system mmu.
 * @pgd: The page table base address.
 *
 * This function set page table base address
 * When system mmu transfer address from virtaul address to physical address,
 * system mmu refer address information from page table
 */
void s5p_sysmmu_set_tablebase_pgd(sysmmu_ips ips, unsigned long pgd);

/**
 * s5p_sysmmu_tlb_invalidate() - flush all TLB entry in system mmu
 * @ips: The ip connected system mmu.
 *
 * This function flush all TLB entry in system mmu
 */
void s5p_sysmmu_tlb_invalidate(sysmmu_ips ips);

/** s5p_sysmmu_set_fault_handler() - Fault handler for System MMUs
 * @itype: type of fault.
 * @pgtable_base: the physical address of page table base. This is 0 if @ips is
 *               SYSMMU_BUSERROR.
 * @fault_addr: the device (virtual) address that the System MMU tried to
 *             translated. This is 0 if @ips is SYSMMU_BUSERROR.
 * Called when interrupt occurred by the System MMUs
 * The device drivers of peripheral devices that has a System MMU can implement
 * a fault handler to resolve address translation fault by System MMU.
 * The meanings of return value and parameters are described below.

 * return value: non-zero if the fault is correctly resolved.
 *         zero if the fault is not handled.
 */
void s5p_sysmmu_set_fault_handler(sysmmu_ips ips,
			int (*handler)(enum S5P_SYSMMU_INTERRUPT_TYPE itype,
					unsigned long pgtable_base,
					unsigned long fault_addr));
#else
#define s5p_sysmmu_enable(ips, pgd) do { } while (0)
#define s5p_sysmmu_disable(ips) do { } while (0)
#define s5p_sysmmu_set_tablebase_pgd(ips, pgd) do { } while (0)
#define s5p_sysmmu_tlb_invalidate(ips) do { } while (0)
#define s5p_sysmmu_set_fault_handler(ips, handler) do { } while (0)
#endif
#endif /* __ASM_PLAT_SYSMMU_H */
