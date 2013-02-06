/* linux/arch/arm/plat-s5p/include/plat/sysmmu.h
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

#ifndef __ASM__PLAT_SYSMMU_H
#define __ASM__PLAT_SYSMMU_H __FILE__

#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

enum S5P_SYSMMU_INTERRUPT_TYPE {
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
typedef int (*s5p_sysmmu_fault_handler_t)(enum S5P_SYSMMU_INTERRUPT_TYPE itype,
			unsigned long pgtable_base, unsigned long fault_addr);

struct sysmmu_drvdata {
	struct list_head node;
	struct device *dev;
	struct device *owner;
	void __iomem *sfrbase;
	struct clk *clk;
	int activations;
	rwlock_t lock;
	s5p_sysmmu_fault_handler_t fault_handler;
	unsigned long version;
};

#ifdef CONFIG_S5P_SYSTEM_MMU

struct sysmmu_drvdata *get_sysmmu_data(struct device *owner,
						struct sysmmu_drvdata *start);

struct list_head *get_sysmmu_list(void);

/**
 * s5p_sysmmu_enable() - enable system mmu
 * @owner: The device whose System MMU is about to be enabled.
 * @pgd: Base physical address of the 1st level page table
 *
 * This function enable system mmu to transfer address
 * from virtual address to physical address.
 * Return non-zero if it fails to enable System MMU.
 */
int s5p_sysmmu_enable(struct device *owner, unsigned long pgd);

/**
 * s5p_sysmmu_disable() - disable sysmmu mmu of ip
 * @owner: The device whose System MMU is about to be disabled.
 *
 * This function disable system mmu to transfer address
 * from virtual address to physical address
 */
void s5p_sysmmu_disable(struct device *owner);

/**
 * s5p_sysmmu_set_tablebase_pgd() - set page table base to refer page table
 * @owner: The device whose System MMU.
 * @pgd: The page table base address.
 *
 * This function set page table base address
 * When system mmu transfer address from virtaul address to physical address,
 * system mmu refer address information from page table
 */
void s5p_sysmmu_set_tablebase_pgd(struct device *owner, unsigned long pgd);

/**
 * s5p_sysmmu_tlb_invalidate() - flush all TLB entry in system mmu
 * @owner: The device whose System MMU.
 *
 * This function flush all TLB entry in system mmu
 */
void s5p_sysmmu_tlb_invalidate(struct device *owner);

/**
 * s5p_sysmmu_tlb_invalidate_entry() - flush a TLB entry in system mmu
 * @owner: The device whose System MMU.
 * @iova: device address to tlb entry to be invalidated.
 * @count: entry count.
 *
 * This function flush all TLB entry in system mmu
 */
void s5p_sysmmu_tlb_invalidate_entry(struct device *owner,
					unsigned long iova,
					unsigned int count,
					unsigned long page_size);

/** s5p_sysmmu_set_fault_handler() - Fault handler for System MMUs
 * Called when interrupt occurred by the System MMUs
 * The device drivers of peripheral devices that has a System MMU can implement
 * a fault handler to resolve address translation fault by System MMU.
 * The meanings of return value and parameters are described below.

 * return value: non-zero if the fault is correctly resolved.
 *         zero if the fault is not handled.
 */
void s5p_sysmmu_set_fault_handler(struct device *sysmmu,
					s5p_sysmmu_fault_handler_t handler);

/** s5p_sysmmu_set_prefbuf() - Initialize prefetch buffers of System MMU v3
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
void s5p_sysmmu_set_prefbuf(struct device *owner,
				unsigned long base0, unsigned long size0,
				unsigned long base1, unsigned long size1);
#else /* !CONFIG_S5P_SYSTEM_MMU */
#define s5p_sysmmu_enable(owner, pgd) do { } while (0)
#define s5p_sysmmu_disable(owner) do { } while (0)
#define s5p_sysmmu_set_tablebase_pgd(owner, pgd) do { } while (0)
#define s5p_sysmmu_tlb_invalidate(owner) do { } while (0)
#define s5p_sysmmu_set_fault_handler(sysmmu, handler) do { } while (0)
#define s5p_sysmmu_set_prefbuf(owner, base, size) do { } while (0)
#endif
#endif /* __ASM_PLAT_SYSMMU_H */
