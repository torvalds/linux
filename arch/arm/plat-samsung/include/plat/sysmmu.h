/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung System MMU driver for Exynos platforms
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM__PLAT_SYSMMU_H
#define __ASM__PLAT_SYSMMU_H __FILE__

#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

enum exynos_sysmmu_inttype {
	SYSMMU_PAGEFAULT,
	SYSMMU_AR_MULTIHIT,
	SYSMMU_AW_MULTIHIT,
	SYSMMU_BUSERROR,
	SYSMMU_AR_SECURITY,
	SYSMMU_AR_ACCESS,
	SYSMMU_AW_SECURITY,
	SYSMMU_AW_PROTECTION, /* 7 */
	SYSMMU_FAULT_UNKNOWN,
	SYSMMU_FAULTS_NUM
};

/*
 * @itype: type of fault.
 * @pgtable_base: the physical address of page table base. This is 0 if @itype
 *                is SYSMMU_BUSERROR.
 * @fault_addr: the device (virtual) address that the System MMU tried to
 *             translated. This is 0 if @itype is SYSMMU_BUSERROR.
 */
typedef int (*sysmmu_fault_handler_t)(enum exynos_sysmmu_inttype itype,
			unsigned long pgtable_base, unsigned long fault_addr);

#ifdef CONFIG_EXYNOS_IOMMU
/**
 * exynos_sysmmu_enable() - enable system mmu
 * @owner: The device whose System MMU is about to be enabled.
 * @pgd: Base physical address of the 1st level page table
 *
 * This function enable system mmu to transfer address
 * from virtual address to physical address.
 * Return non-zero if it fails to enable System MMU.
 */
int exynos_sysmmu_enable(struct device *owner, unsigned long pgd);

/**
 * exynos_sysmmu_disable() - disable sysmmu mmu of ip
 * @owner: The device whose System MMU is about to be disabled.
 *
 * This function disable system mmu to transfer address
 * from virtual address to physical address
 */
bool exynos_sysmmu_disable(struct device *owner);

/**
 * exynos_sysmmu_tlb_invalidate() - flush all TLB entry in system mmu
 * @owner: The device whose System MMU.
 *
 * This function flush all TLB entry in system mmu
 */
void exynos_sysmmu_tlb_invalidate(struct device *owner);

/** exynos_sysmmu_set_fault_handler() - Fault handler for System MMUs
 * Called when interrupt occurred by the System MMUs
 * The device drivers of peripheral devices that has a System MMU can implement
 * a fault handler to resolve address translation fault by System MMU.
 * The meanings of return value and parameters are described below.
 *
 * return value: non-zero if the fault is correctly resolved.
 *         zero if the fault is not handled.
 */
void exynos_sysmmu_set_fault_handler(struct device *sysmmu,
					sysmmu_fault_handler_t handler);

/** exynos_sysmmu_set_prefbuf() - Initialize prefetch buffers of System MMU v3
 *  @owner: The device which need to set the prefetch buffers
 *  @base0: The start virtual address of the area of the @owner device that the
 *          first prefetch buffer loads translation descriptors
 *  @size0: The last virtual address of the area of the @owner device that the
 *          first prefetch buffer loads translation descriptors.
 *  @base1: The start virtual address of the area of the @owner device that the
 *          second prefetch buffer loads translation descriptors. This will be
 *          ignored if @size1 is 0 and this function assigns the 2 prefetch
 *          buffers with each half of the area specified by @base0 and @size0
 *  @size1: The last virtual address of the area of the @owner device that the
 *          prefetch buffer loads translation descriptors. This can be 0. See
 *          the description of @base1 for more information with @size1 = 0
 */
void exynos_sysmmu_set_prefbuf(struct device *owner,
				unsigned long base0, unsigned long size0,
				unsigned long base1, unsigned long size1);
#else /* CONFIG_EXYNOS_IOMMU */
#define exynos_sysmmu_enable(owner, pgd) do { } while (0)
#define exynos_sysmmu_disable(owner) do { } while (0)
#define exynos_sysmmu_tlb_invalidate(owner) do { } while (0)
#define exynos_sysmmu_set_fault_handler(sysmmu, handler) do { } while (0)
#define exynos_sysmmu_set_prefbuf(owner, b0, s0, b1, s1) do { } while (0)
#endif
#endif /* __ASM_PLAT_SYSMMU_H */
