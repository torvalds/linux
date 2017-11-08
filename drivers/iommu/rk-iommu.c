/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_ROCKCHIP_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/module.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/of.h>
#include <linux/rockchip-iovmm.h>
#include <linux/rockchip/cpu.h>
#include <linux/device.h>
#include "rk-iommu.h"

/* We does not consider super section mapping (16MB) */
#define SPAGE_ORDER 12
#define SPAGE_SIZE (1 << SPAGE_ORDER)
#define SPAGE_MASK (~(SPAGE_SIZE - 1))

static void __iomem *rk312x_vop_mmu_base;

enum iommu_entry_flags {
	IOMMU_FLAGS_PRESENT = 0x01,
	IOMMU_FLAGS_READ_PERMISSION = 0x02,
	IOMMU_FLAGS_WRITE_PERMISSION = 0x04,
	IOMMU_FLAGS_OVERRIDE_CACHE = 0x8,
	IOMMU_FLAGS_WRITE_CACHEABLE = 0x10,
	IOMMU_FLAGS_WRITE_ALLOCATE = 0x20,
	IOMMU_FLAGS_WRITE_BUFFERABLE = 0x40,
	IOMMU_FLAGS_READ_CACHEABLE = 0x80,
	IOMMU_FLAGS_READ_ALLOCATE = 0x100,
	IOMMU_FLAGS_MASK = 0x1FF,
};

#define rockchip_lv1ent_fault(sent) ((*(sent) & IOMMU_FLAGS_PRESENT) == 0)
#define rockchip_lv1ent_page(sent) ((*(sent) & IOMMU_FLAGS_PRESENT) == 1)
#define rockchip_lv2ent_fault(pent) ((*(pent) & IOMMU_FLAGS_PRESENT) == 0)
#define rockchip_spage_phys(pent) (*(pent) & SPAGE_MASK)
#define rockchip_spage_offs(iova) ((iova) & 0x0FFF)

#define rockchip_lv1ent_offset(iova) (((iova)>>22) & 0x03FF)
#define rockchip_lv2ent_offset(iova) (((iova)>>12) & 0x03FF)

#define NUM_LV1ENTRIES 1024
#define NUM_LV2ENTRIES 1024

#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(long))

#define rockchip_lv2table_base(sent) (*(sent) & 0xFFFFFFFE)

#define rockchip_mk_lv1ent_page(pa) ((pa) | IOMMU_FLAGS_PRESENT)
/*write and read permission for level2 page default*/
#define rockchip_mk_lv2ent_spage(pa) ((pa) | IOMMU_FLAGS_PRESENT | \
			     IOMMU_FLAGS_READ_PERMISSION | \
			     IOMMU_FLAGS_WRITE_PERMISSION)

#define IOMMU_REG_POLL_COUNT_FAST 1000

/**
 * MMU register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
enum iommu_register {
	/**< Current Page Directory Pointer */
	IOMMU_REGISTER_DTE_ADDR = 0x0000,
	/**< Status of the MMU */
	IOMMU_REGISTER_STATUS = 0x0004,
	/**< Command register, used to control the MMU */
	IOMMU_REGISTER_COMMAND = 0x0008,
	/**< Logical address of the last page fault */
	IOMMU_REGISTER_PAGE_FAULT_ADDR = 0x000C,
	/**< Used to invalidate the mapping of a single page from the MMU */
	IOMMU_REGISTER_ZAP_ONE_LINE = 0x010,
	/**< Raw interrupt status, all interrupts visible */
	IOMMU_REGISTER_INT_RAWSTAT = 0x0014,
	/**< Indicate to the MMU that the interrupt has been received */
	IOMMU_REGISTER_INT_CLEAR = 0x0018,
	/**< Enable/disable types of interrupts */
	IOMMU_REGISTER_INT_MASK = 0x001C,
	/**< Interrupt status based on the mask */
	IOMMU_REGISTER_INT_STATUS = 0x0020,
	IOMMU_REGISTER_AUTO_GATING = 0x0024
};

enum iommu_command {
	/**< Enable paging (memory translation) */
	IOMMU_COMMAND_ENABLE_PAGING = 0x00,
	/**< Disable paging (memory translation) */
	IOMMU_COMMAND_DISABLE_PAGING = 0x01,
	/**<  Enable stall on page fault */
	IOMMU_COMMAND_ENABLE_STALL = 0x02,
	/**< Disable stall on page fault */
	IOMMU_COMMAND_DISABLE_STALL = 0x03,
	/**< Zap the entire page table cache */
	IOMMU_COMMAND_ZAP_CACHE = 0x04,
	/**< Page fault processed */
	IOMMU_COMMAND_PAGE_FAULT_DONE = 0x05,
	/**< Reset the MMU back to power-on settings */
	IOMMU_COMMAND_HARD_RESET = 0x06
};

/**
 * MMU interrupt register bits
 * Each cause of the interrupt is reported
 * through the (raw) interrupt status registers.
 * Multiple interrupts can be pending, so multiple bits
 * can be set at once.
 */
enum iommu_interrupt {
	IOMMU_INTERRUPT_PAGE_FAULT = 0x01, /**< A page fault occured */
	IOMMU_INTERRUPT_READ_BUS_ERROR = 0x02 /**< A bus read error occured */
};

enum iommu_status_bits {
	IOMMU_STATUS_BIT_PAGING_ENABLED      = 1 << 0,
	IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE   = 1 << 1,
	IOMMU_STATUS_BIT_STALL_ACTIVE        = 1 << 2,
	IOMMU_STATUS_BIT_IDLE                = 1 << 3,
	IOMMU_STATUS_BIT_REPLAY_BUFFER_EMPTY = 1 << 4,
	IOMMU_STATUS_BIT_PAGE_FAULT_IS_WRITE = 1 << 5,
	IOMMU_STATUS_BIT_STALL_NOT_ACTIVE    = 1 << 31,
};

/**
 * Size of an MMU page in bytes
 */
#define IOMMU_PAGE_SIZE 0x1000

/*
 * Size of the address space referenced by a page table page
 */
#define IOMMU_VIRTUAL_PAGE_SIZE 0x400000 /* 4 MiB */

/**
 * Page directory index from address
 * Calculates the page directory index from the given address
 */
#define IOMMU_PDE_ENTRY(address) (((address)>>22) & 0x03FF)

/**
 * Page table index from address
 * Calculates the page table index from the given address
 */
#define IOMMU_PTE_ENTRY(address) (((address)>>12) & 0x03FF)

/**
 * Extract the memory address from an PDE/PTE entry
 */
#define IOMMU_ENTRY_ADDRESS(value) ((value) & 0xFFFFFC00)

#define INVALID_PAGE ((u32)(~0))

static struct kmem_cache *lv2table_kmem_cache;

static unsigned int *rockchip_section_entry(unsigned int *pgtable, unsigned long iova)
{
	return pgtable + rockchip_lv1ent_offset(iova);
}

static unsigned int *rockchip_page_entry(unsigned int *sent, unsigned long iova)
{
	return (unsigned int *)phys_to_virt(rockchip_lv2table_base(sent)) +
		rockchip_lv2ent_offset(iova);
}

struct rk_iommu_domain {
	struct list_head clients; /* list of iommu_drvdata.node */
	unsigned int *pgtable; /* lv1 page table, 4KB */
	short *lv2entcnt; /* free lv2 entry counter for each section */
	spinlock_t lock; /* lock for this structure */
	spinlock_t pgtablelock; /* lock for modifying page table @ pgtable */
	struct iommu_domain domain;
};

static struct rk_iommu_domain *to_rk_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct rk_iommu_domain, domain);
}

static bool rockchip_set_iommu_active(struct iommu_drvdata *data)
{
	/* return true if the IOMMU was not active previously
	   and it needs to be initialized */
	return ++data->activations == 1;
}

static bool rockchip_set_iommu_inactive(struct iommu_drvdata *data)
{
	/* return true if the IOMMU is needed to be disabled */
	BUG_ON(data->activations < 1);
	return --data->activations == 0;
}

static bool rockchip_is_iommu_active(struct iommu_drvdata *data)
{
	return data->activations > 0;
}

static void rockchip_iommu_disable_stall(void __iomem *base)
{
	int i;
	u32 mmu_status;

	if (base != rk312x_vop_mmu_base) {
		mmu_status = __raw_readl(base + IOMMU_REGISTER_STATUS);
	} else {
		goto skip_vop_mmu_disable;
	}

	if (0 == (mmu_status & IOMMU_STATUS_BIT_PAGING_ENABLED)) {
		return;
	}

	if (mmu_status & IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		pr_info("Aborting MMU disable stall request since it is in pagefault state.\n");
		return;
	}

	if (!(mmu_status & IOMMU_STATUS_BIT_STALL_ACTIVE)) {
		return;
	}

	__raw_writel(IOMMU_COMMAND_DISABLE_STALL, base + IOMMU_REGISTER_COMMAND);

	skip_vop_mmu_disable:

	for (i = 0; i < IOMMU_REG_POLL_COUNT_FAST; ++i) {
		u32 status;
		
		if (base != rk312x_vop_mmu_base) {
			status = __raw_readl(base + IOMMU_REGISTER_STATUS);
		} else {
			int j;
			while (j < 5)
				j++;
			return;	
		}

		if (0 == (status & IOMMU_STATUS_BIT_STALL_ACTIVE))
			break;

		if (status & IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE)
			break;

		if (0 == (mmu_status & IOMMU_STATUS_BIT_PAGING_ENABLED))
			break;
	}

	if (IOMMU_REG_POLL_COUNT_FAST == i) {
		pr_info("Disable stall request failed, MMU status is 0x%08X\n",
		      __raw_readl(base + IOMMU_REGISTER_STATUS));
	}
}

static bool rockchip_iommu_enable_stall(void __iomem *base)
{
	int i;

	u32 mmu_status;
	
	if (base != rk312x_vop_mmu_base) {
		mmu_status = __raw_readl(base + IOMMU_REGISTER_STATUS);
	} else {
		goto skip_vop_mmu_enable;
	}

	if (0 == (mmu_status & IOMMU_STATUS_BIT_PAGING_ENABLED)) {
		return true;
	}

	if (mmu_status & IOMMU_STATUS_BIT_STALL_ACTIVE){
		pr_info("MMU stall already enabled\n");
		return true;
	}

	if (mmu_status & IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		pr_info("Aborting MMU stall request since it is in pagefault state. mmu status is 0x%08x\n",
			mmu_status);
		return false;
	}

	__raw_writel(IOMMU_COMMAND_ENABLE_STALL, base + IOMMU_REGISTER_COMMAND);

	skip_vop_mmu_enable:

	for (i = 0; i < IOMMU_REG_POLL_COUNT_FAST; ++i) {
		if (base != rk312x_vop_mmu_base) {
			mmu_status = __raw_readl(base + IOMMU_REGISTER_STATUS);
		} else {
			int j;
			while (j < 5)
				j++;
			return true;
		}

		if (mmu_status & IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE)
			break;

		if ((mmu_status & IOMMU_STATUS_BIT_STALL_ACTIVE) &&
		    (0 == (mmu_status & IOMMU_STATUS_BIT_STALL_NOT_ACTIVE)))
			break;

		if (0 == (mmu_status & (IOMMU_STATUS_BIT_PAGING_ENABLED)))
			break;
	}

	if (IOMMU_REG_POLL_COUNT_FAST == i) {
		pr_info("Enable stall request failed, MMU status is 0x%08X\n",
		       __raw_readl(base + IOMMU_REGISTER_STATUS));
		return false;
	}

	if (mmu_status & IOMMU_STATUS_BIT_PAGE_FAULT_ACTIVE) {
		pr_info("Aborting MMU stall request since it has a pagefault.\n");
		return false;
	}

	return true;
}

static bool rockchip_iommu_enable_paging(void __iomem *base)
{
	int i;

	__raw_writel(IOMMU_COMMAND_ENABLE_PAGING,
		     base + IOMMU_REGISTER_COMMAND);

	for (i = 0; i < IOMMU_REG_POLL_COUNT_FAST; ++i) {
		if (base != rk312x_vop_mmu_base) {
			if (__raw_readl(base + IOMMU_REGISTER_STATUS) &
				IOMMU_STATUS_BIT_PAGING_ENABLED)
			break;
		} else {
			int j;
			while (j < 5)
				j++;
			return true;
		}
	}

	if (IOMMU_REG_POLL_COUNT_FAST == i) {
		pr_info("Enable paging request failed, MMU status is 0x%08X\n",
		       __raw_readl(base + IOMMU_REGISTER_STATUS));
		return false;
	}

	return true;
}

static bool rockchip_iommu_disable_paging(void __iomem *base)
{
	int i;

	__raw_writel(IOMMU_COMMAND_DISABLE_PAGING,
		     base + IOMMU_REGISTER_COMMAND);

	for (i = 0; i < IOMMU_REG_POLL_COUNT_FAST; ++i) {
		if (base != rk312x_vop_mmu_base) {
			if (!(__raw_readl(base + IOMMU_REGISTER_STATUS) &
				  IOMMU_STATUS_BIT_PAGING_ENABLED))
				break;
		} else {
			int j;
			while (j < 5)
				j++;
			return true;
		}
	}

	if (IOMMU_REG_POLL_COUNT_FAST == i) {
		pr_info("Disable paging request failed, MMU status is 0x%08X\n",
		       __raw_readl(base + IOMMU_REGISTER_STATUS));
		return false;
	}

	return true;
}

static void rockchip_iommu_page_fault_done(void __iomem *base, const char *dbgname)
{
	pr_info("MMU: %s: Leaving page fault mode\n",
		dbgname);
	__raw_writel(IOMMU_COMMAND_PAGE_FAULT_DONE,
		     base + IOMMU_REGISTER_COMMAND);
}

static int rockchip_iommu_zap_tlb_without_stall (void __iomem *base)
{
	__raw_writel(IOMMU_COMMAND_ZAP_CACHE, base + IOMMU_REGISTER_COMMAND);

	return 0;
}

static int rockchip_iommu_zap_tlb(void __iomem *base)
{
	if (!rockchip_iommu_enable_stall(base)) {
		pr_err("%s failed\n", __func__);
		return -1;
	}

	__raw_writel(IOMMU_COMMAND_ZAP_CACHE, base + IOMMU_REGISTER_COMMAND);

	rockchip_iommu_disable_stall(base);

	return 0;
}

static inline bool rockchip_iommu_raw_reset(void __iomem *base)
{
	int i;
	unsigned int ret;

	__raw_writel(0xCAFEBABE, base + IOMMU_REGISTER_DTE_ADDR);

	if (base != rk312x_vop_mmu_base) {
		ret = __raw_readl(base + IOMMU_REGISTER_DTE_ADDR);
		if (ret != 0xCAFEB000)
			return false;
	}
	__raw_writel(IOMMU_COMMAND_HARD_RESET,
		     base + IOMMU_REGISTER_COMMAND);

	for (i = 0; i < IOMMU_REG_POLL_COUNT_FAST; ++i) {
		if (base != rk312x_vop_mmu_base) {
			if (__raw_readl(base + IOMMU_REGISTER_DTE_ADDR) == 0)
				break;
		} else {
			int j;
			while (j < 5)
				j++;
			return true;
		}
	}

	if (IOMMU_REG_POLL_COUNT_FAST == i) {
		pr_info("%s,Reset request failed, MMU status is 0x%08X\n",
		       __func__, __raw_readl(base + IOMMU_REGISTER_DTE_ADDR));
		return false;
	}
	return true;
}

static void rockchip_iommu_set_ptbase(void __iomem *base, unsigned int pgd)
{
	__raw_writel(pgd, base + IOMMU_REGISTER_DTE_ADDR);
}

static bool rockchip_iommu_reset(void __iomem *base, const char *dbgname)
{
	bool ret = true;

	ret = rockchip_iommu_raw_reset(base);
	if (!ret) {
		pr_info("(%s), %s failed\n", dbgname, __func__);
		return ret;
	}

	if (base != rk312x_vop_mmu_base)
		__raw_writel(IOMMU_INTERRUPT_PAGE_FAULT |
			     IOMMU_INTERRUPT_READ_BUS_ERROR,
			     base + IOMMU_REGISTER_INT_MASK);
	else
		__raw_writel(0x00, base + IOMMU_REGISTER_INT_MASK);

	return ret;
}

static inline void rockchip_pgtable_flush(void *vastart, void *vaend)
{
#ifdef CONFIG_ARM
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart), virt_to_phys(vaend));
#elif defined(CONFIG_ARM64)
	__dma_flush_range(vastart, vaend);
	//flush_cache_all();
#endif
}

static void dump_pagetbl(dma_addr_t fault_address, u32 addr_dte)
{
	u32 dte_index, pte_index, page_offset;
	u32 mmu_dte_addr;
	phys_addr_t mmu_dte_addr_phys, dte_addr_phys;
	u32 *dte_addr;
	u32 dte;
	phys_addr_t pte_addr_phys = 0;
	u32 *pte_addr = NULL;
	u32 pte = 0;
	phys_addr_t page_addr_phys = 0;
	u32 page_flags = 0;

	dte_index = rockchip_lv1ent_offset(fault_address);
	pte_index = rockchip_lv2ent_offset(fault_address);
	page_offset = (u32)(fault_address & 0x00000fff);

	mmu_dte_addr = addr_dte;
	mmu_dte_addr_phys = (phys_addr_t)mmu_dte_addr;

	dte_addr_phys = mmu_dte_addr_phys + (4 * dte_index);
	dte_addr = phys_to_virt(dte_addr_phys);
	dte = *dte_addr;

	if (!(IOMMU_FLAGS_PRESENT & dte))
		goto print_it;

	pte_addr_phys = ((phys_addr_t)dte & 0xfffff000) + (pte_index * 4);
	pte_addr = phys_to_virt(pte_addr_phys);
	pte = *pte_addr;

	if (!(IOMMU_FLAGS_PRESENT & pte))
		goto print_it;

	page_addr_phys = ((phys_addr_t)pte & 0xfffff000) + page_offset;
	page_flags = pte & 0x000001fe;

print_it:
	pr_err("iova = %pad: dte_index: 0x%03x pte_index: 0x%03x page_offset: 0x%03x\n",
		&fault_address, dte_index, pte_index, page_offset);
	pr_err("mmu_dte_addr: %pa dte@%pa: %#08x valid: %u pte@%pa: %#08x valid: %u page@%pa flags: %#03x\n",
		&mmu_dte_addr_phys, &dte_addr_phys, dte,
		(dte & IOMMU_FLAGS_PRESENT), &pte_addr_phys, pte,
		(pte & IOMMU_FLAGS_PRESENT), &page_addr_phys, page_flags);
}

static irqreturn_t rockchip_iommu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	struct iommu_drvdata *data = dev_id;
	u32 status;
	u32 rawstat;
	dma_addr_t fault_address;
	int i;
	unsigned long flags;
	int ret;
	u32 reg_status;

	spin_lock_irqsave(&data->data_lock, flags);

	if (!rockchip_is_iommu_active(data)) {
		spin_unlock_irqrestore(&data->data_lock, flags);
		return IRQ_HANDLED;
	}

	for (i = 0; i < data->num_res_mem; i++) {
		status = __raw_readl(data->res_bases[i] +
				     IOMMU_REGISTER_INT_STATUS);
		if (status == 0)
			continue;

		rawstat = __raw_readl(data->res_bases[i] +
				      IOMMU_REGISTER_INT_RAWSTAT);

		reg_status = __raw_readl(data->res_bases[i] +
				      	 IOMMU_REGISTER_STATUS);

		dev_info(data->iommu, "1.rawstat = 0x%08x,status = 0x%08x,reg_status = 0x%08x\n",
			 rawstat, status, reg_status);

		if (rawstat & IOMMU_INTERRUPT_PAGE_FAULT) {
			u32 dte;
			int flags;

			fault_address = __raw_readl(data->res_bases[i] +
				   	    IOMMU_REGISTER_PAGE_FAULT_ADDR);

			dte = __raw_readl(data->res_bases[i] +
					  IOMMU_REGISTER_DTE_ADDR);

			flags = (status & 32) ? 1 : 0;

			dev_err(data->iommu, "Page fault detected at %pad from bus id %d of type %s on %s\n",
				&fault_address, (status >> 6) & 0x1F,
				(flags == 1) ? "write" : "read", data->dbgname);

			dump_pagetbl(fault_address, dte);

			if (data->domain)
				report_iommu_fault(data->domain, data->iommu,
						   fault_address, flags);
			if (data->fault_handler)
				data->fault_handler(data->master, IOMMU_PAGEFAULT, dte, fault_address, 1);

			rockchip_iommu_page_fault_done(data->res_bases[i],
					               data->dbgname);
		}

		if (rawstat & IOMMU_INTERRUPT_READ_BUS_ERROR) {
			dev_err(data->iommu, "bus error occured at %pad\n",
				&fault_address);
		}

		if (rawstat & ~(IOMMU_INTERRUPT_READ_BUS_ERROR |
		    IOMMU_INTERRUPT_PAGE_FAULT)) {
	    		dev_err(data->iommu, "unexpected int_status: %#08x\n\n",
	    			rawstat);
		}

		__raw_writel(rawstat, data->res_bases[i] +
			     IOMMU_REGISTER_INT_CLEAR);

		status = __raw_readl(data->res_bases[i] +
				     IOMMU_REGISTER_INT_STATUS);

		rawstat = __raw_readl(data->res_bases[i] +
				      IOMMU_REGISTER_INT_RAWSTAT);

		reg_status = __raw_readl(data->res_bases[i] +
				      	 IOMMU_REGISTER_STATUS);

		dev_info(data->iommu, "2.rawstat = 0x%08x,status = 0x%08x,reg_status = 0x%08x\n",
			 rawstat, status, reg_status);

		ret = rockchip_iommu_zap_tlb_without_stall(data->res_bases[i]);
		if (ret)
			dev_err(data->iommu, "(%s) %s failed\n", data->dbgname,
				__func__);
	}

	spin_unlock_irqrestore(&data->data_lock, flags);
	return IRQ_HANDLED;
}

static bool rockchip_iommu_disable(struct iommu_drvdata *data)
{
	unsigned long flags;
	int i;
	bool ret = false;

	spin_lock_irqsave(&data->data_lock, flags);

	if (!rockchip_set_iommu_inactive(data)) {
		spin_unlock_irqrestore(&data->data_lock, flags);
		dev_info(data->iommu,"(%s) %d times left to be disabled\n",
			 data->dbgname, data->activations);
		return ret;
	}

	for (i = 0; i < data->num_res_mem; i++) {
		ret = rockchip_iommu_enable_stall(data->res_bases[i]);
		if (!ret) {
			dev_info(data->iommu, "(%s), %s failed\n",
				 data->dbgname, __func__);
			spin_unlock_irqrestore(&data->data_lock, flags);
			return false;
		}

		__raw_writel(0, data->res_bases[i] + IOMMU_REGISTER_INT_MASK);

		ret = rockchip_iommu_disable_paging(data->res_bases[i]);
		if (!ret) {
			rockchip_iommu_disable_stall(data->res_bases[i]);
			spin_unlock_irqrestore(&data->data_lock, flags);
			dev_info(data->iommu, "%s error\n", __func__);
			return ret;
		}
		rockchip_iommu_disable_stall(data->res_bases[i]);
	}

	data->pgtable = 0;

	spin_unlock_irqrestore(&data->data_lock, flags);

	dev_dbg(data->iommu,"(%s) Disabled\n", data->dbgname);

	return ret;
}

/* __rk_sysmmu_enable: Enables System MMU
 *
 * returns -error if an error occurred and System MMU is not enabled,
 * 0 if the System MMU has been just enabled and 1 if System MMU was already
 * enabled before.
 */
static int rockchip_iommu_enable(struct iommu_drvdata *data, unsigned int pgtable)
{
	int i, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&data->data_lock, flags);

	if (!rockchip_set_iommu_active(data)) {
		if (WARN_ON(pgtable != data->pgtable))
			ret = -EBUSY;
		else
			ret = 1;

		dev_info(data->iommu, "(%s) Already enabled\n", data->dbgname);

		goto enable_out;
	}

	for (i = 0; i < data->num_res_mem; i++) {
		ret = rockchip_iommu_enable_stall(data->res_bases[i]);
		if (!ret) {
			dev_info(data->iommu, "(%s), %s failed\n",
				 data->dbgname, __func__);
			ret = -EBUSY;
			goto enable_out;
		}

		if (!strstr(data->dbgname, "isp")) {
			if (!rockchip_iommu_reset(data->res_bases[i],
			     data->dbgname)) {
				rockchip_iommu_disable_stall(data->res_bases[i]);
				ret = -ENOENT;
				goto enable_out;
			}
		}

		rockchip_iommu_set_ptbase(data->res_bases[i], pgtable);

		__raw_writel(IOMMU_COMMAND_ZAP_CACHE, data->res_bases[i] +
			     IOMMU_REGISTER_COMMAND);

		if (strstr(data->dbgname, "isp")) {
			__raw_writel(IOMMU_INTERRUPT_PAGE_FAULT |
				IOMMU_INTERRUPT_READ_BUS_ERROR,
			     data->res_bases[i] + IOMMU_REGISTER_INT_MASK);
		}

		ret = rockchip_iommu_enable_paging(data->res_bases[i]);
		if (!ret) {
			dev_info(data->iommu, "(%s), %s failed\n",
				 data->dbgname, __func__);
			rockchip_iommu_disable_stall(data->res_bases[i]);
			ret = -EBUSY;
			goto enable_out;
		}

		rockchip_iommu_disable_stall(data->res_bases[i]);
	}

	data->pgtable = pgtable;
	spin_unlock_irqrestore(&data->data_lock, flags);

	dev_dbg(data->iommu,"(%s) Enabled\n", data->dbgname);

	return 0;

enable_out:
	rockchip_set_iommu_inactive(data);
	spin_unlock_irqrestore(&data->data_lock, flags);

	return ret;
}

int rockchip_iommu_tlb_invalidate_global(struct device *dev)
{
	unsigned long flags;
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	int ret = 0;

	spin_lock_irqsave(&data->data_lock, flags);

	if (rockchip_is_iommu_active(data)) {
		int i;

		for (i = 0; i < data->num_res_mem; i++) {
			ret = rockchip_iommu_zap_tlb(data->res_bases[i]);
			if (ret)
				dev_err(dev->archdata.iommu, "(%s) %s failed\n",
					data->dbgname, __func__);
		}
	} else {
		dev_dbg(dev->archdata.iommu, "(%s) Disabled. Skipping invalidating TLB.\n",
			data->dbgname);
		ret = -1;
	}

	spin_unlock_irqrestore(&data->data_lock, flags);

	return ret;
}

int rockchip_iommu_tlb_invalidate(struct device *dev)
{
	unsigned long flags;
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	if (strstr(data->dbgname, "vpu") || strstr(data->dbgname, "hevc"))
			return 0;

	spin_lock_irqsave(&data->data_lock, flags);

	if (rockchip_is_iommu_active(data)) {
		int i;
		int ret;

		for (i = 0; i < data->num_res_mem; i++) {
			ret = rockchip_iommu_zap_tlb(data->res_bases[i]);
			if (ret) {
				dev_err(dev->archdata.iommu, "(%s) %s failed\n",
					data->dbgname, __func__);
				spin_unlock_irqrestore(&data->data_lock, flags);
				return ret;
			}
				
		}
	} else {
		dev_dbg(dev->archdata.iommu, "(%s) Disabled. Skipping invalidating TLB.\n",
			data->dbgname);
	}

	spin_unlock_irqrestore(&data->data_lock, flags);

	return 0;
}

static phys_addr_t rockchip_iommu_iova_to_phys(struct iommu_domain *domain,
					       dma_addr_t iova)
{
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	unsigned int *entry;
	unsigned long flags;
	phys_addr_t phys = 0;

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = rockchip_section_entry(priv->pgtable, iova);
	entry = rockchip_page_entry(entry, iova);
	phys = rockchip_spage_phys(entry) + rockchip_spage_offs(iova);

	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return phys;
}

static int rockchip_lv2set_page(unsigned int *pent, phys_addr_t paddr,
		       size_t size, short *pgcnt)
{
	if (!rockchip_lv2ent_fault(pent))
		return -EADDRINUSE;

	*pent = rockchip_mk_lv2ent_spage(paddr);
	rockchip_pgtable_flush(pent, pent + 1);
	*pgcnt -= 1;
	return 0;
}

static unsigned int *rockchip_alloc_lv2entry(unsigned int *sent,
				     unsigned long iova, short *pgcounter)
{
	if (rockchip_lv1ent_fault(sent)) {
		unsigned int *pent;

		pent = kmem_cache_zalloc(lv2table_kmem_cache, GFP_ATOMIC);
		BUG_ON((unsigned long)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return NULL;

		*sent = rockchip_mk_lv1ent_page(virt_to_phys(pent));
		kmemleak_ignore(pent);
		*pgcounter = NUM_LV2ENTRIES;
		rockchip_pgtable_flush(pent, pent + NUM_LV2ENTRIES);
		rockchip_pgtable_flush(sent, sent + 1);
	}
	return rockchip_page_entry(sent, iova);
}

static size_t rockchip_iommu_unmap(struct iommu_domain *domain,
				   unsigned long iova, size_t size)
{
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	unsigned long flags;
	unsigned int *ent;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	ent = rockchip_section_entry(priv->pgtable, iova);

	if (unlikely(rockchip_lv1ent_fault(ent))) {
		if (size > SPAGE_SIZE)
			size = SPAGE_SIZE;
		goto done;
	}

	/* lv1ent_page(sent) == true here */

	ent = rockchip_page_entry(ent, iova);

	if (unlikely(rockchip_lv2ent_fault(ent))) {
		size = SPAGE_SIZE;
		goto done;
	}

	*ent = 0;
	size = SPAGE_SIZE;
	priv->lv2entcnt[rockchip_lv1ent_offset(iova)] += 1;
	goto done;

done:
	pr_debug("%s:unmap iova 0x%lx/%zx bytes\n",
		  __func__, iova,size);
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return size;
}

static int rockchip_iommu_map(struct iommu_domain *domain, unsigned long iova,
			      phys_addr_t paddr, size_t size, int prot)
{
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	unsigned int *entry;
	unsigned long flags;
	int ret = -ENOMEM;
	unsigned int *pent;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = rockchip_section_entry(priv->pgtable, iova);

	pent = rockchip_alloc_lv2entry(entry, iova,
			      &priv->lv2entcnt[rockchip_lv1ent_offset(iova)]);
	if (!pent)
		ret = -ENOMEM;
	else
		ret = rockchip_lv2set_page(pent, paddr, size,
				&priv->lv2entcnt[rockchip_lv1ent_offset(iova)]);

	if (ret) {
		pr_info("%s: Failed to map iova 0x%lx/%zx bytes\n", __func__,
		       iova, size);
	}
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return ret;
}

static void rockchip_iommu_detach_device(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	struct list_head *pos;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each(pos, &priv->clients) {
		if (list_entry(pos, struct iommu_drvdata, node) == data) {
			found = true;
			break;
		}
	}

	if (!found) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}

	if (rockchip_iommu_disable(data)) {
		if (!(strstr(data->dbgname, "vpu") || strstr(data->dbgname, "hevc")))
			dev_dbg(dev->archdata.iommu,"%s: Detached IOMMU with pgtable %08lx\n",
				__func__, (unsigned long)virt_to_phys(priv->pgtable));
		data->domain = NULL;
		list_del_init(&data->node);

	} else
		dev_err(dev->archdata.iommu,"%s: Detaching IOMMU with pgtable %08lx delayed",
			__func__, (unsigned long)virt_to_phys(priv->pgtable));

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int rockchip_iommu_attach_device(struct iommu_domain *domain, struct device *dev)
{
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);

	ret = rockchip_iommu_enable(data, virt_to_phys(priv->pgtable));

	if (ret == 0) {
		/* 'data->node' must not be appeared in priv->clients */
		BUG_ON(!list_empty(&data->node));
		list_add_tail(&data->node, &priv->clients);
		data->domain = domain;
		data->master = dev;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret < 0) {
		dev_err(dev->archdata.iommu,"%s: Failed to attach IOMMU with pgtable %x\n",
		       __func__, (unsigned int)virt_to_phys(priv->pgtable));
	} else if (ret > 0) {
		dev_dbg(dev->archdata.iommu,"%s: IOMMU with pgtable 0x%x already attached\n",
			__func__, (unsigned int)virt_to_phys(priv->pgtable));
	} else {
		if (!(strstr(data->dbgname, "vpu") ||
		      strstr(data->dbgname, "hevc") ||
		      strstr(data->dbgname, "vdec")))
			dev_info(dev->archdata.iommu,"%s: Attached new IOMMU with pgtable 0x%x\n",
				__func__, (unsigned int)virt_to_phys(priv->pgtable));
	}

	return ret;
}

static void rockchip_iommu_domain_free(struct iommu_domain *domain)
{
	struct rk_iommu_domain *priv = to_rk_domain(domain);
	int i;

	WARN_ON(!list_empty(&priv->clients));

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (rockchip_lv1ent_page(priv->pgtable + i))
			kmem_cache_free(lv2table_kmem_cache,
					phys_to_virt(rockchip_lv2table_base(priv->pgtable + i)));

	free_pages((unsigned long)priv->pgtable, 0);
	free_pages((unsigned long)priv->lv2entcnt, 0);
	kfree(priv);
}

static struct iommu_domain *rockchip_iommu_domain_alloc(unsigned type)
{
	struct rk_iommu_domain *priv;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

/*rk32xx iommu use 2 level pagetable,
   level1 and leve2 both have 1024 entries,each entry  occupy 4 bytes,
   so alloc a page size for each page table
*/
	priv->pgtable = (unsigned int *)__get_free_pages(GFP_KERNEL |
							  __GFP_ZERO, 0);
	if (!priv->pgtable)
		goto err_pgtable;

	priv->lv2entcnt = (short *)__get_free_pages(GFP_KERNEL |
						    __GFP_ZERO, 0);
	if (!priv->lv2entcnt)
		goto err_counter;

	rockchip_pgtable_flush(priv->pgtable, priv->pgtable + NUM_LV1ENTRIES);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->pgtablelock);
	INIT_LIST_HEAD(&priv->clients);

	return &priv->domain;

err_counter:
	free_pages((unsigned long)priv->pgtable, 0);
err_pgtable:
	kfree(priv);
	return NULL;
}

static struct iommu_ops rk_iommu_ops = {
	.domain_alloc = rockchip_iommu_domain_alloc,
	.domain_free = rockchip_iommu_domain_free,
	.attach_dev = rockchip_iommu_attach_device,
	.detach_dev = rockchip_iommu_detach_device,
	.map = rockchip_iommu_map,
	.unmap = rockchip_iommu_unmap,
	.iova_to_phys = rockchip_iommu_iova_to_phys,
	.pgsize_bitmap = SPAGE_SIZE,
};

static int  rockchip_get_iommu_resource_num(struct platform_device *pdev,
					     unsigned int type)
{
	int num = 0;
	int i;

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *r = &pdev->resource[i];
		if (type == resource_type(r))
			num++;
	}

	return num;
}

static int rockchip_iommu_probe(struct platform_device *pdev)
{
	int i, ret;
	struct device *dev;
	struct iommu_drvdata *data;
	
	dev = &pdev->dev;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_dbg(dev, "Not enough memory\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, data);

	if (pdev->dev.of_node)
		of_property_read_string(pdev->dev.of_node, "dbgname",
					&(data->dbgname));
	else
		dev_dbg(dev, "dbgname not assigned in device tree or device node not exist\r\n");

	dev_info(dev,"(%s) Enter\n", data->dbgname);
	
	data->num_res_mem = rockchip_get_iommu_resource_num(pdev,
				IORESOURCE_MEM);
	if (0 == data->num_res_mem) {
		dev_err(dev,"can't find iommu memory resource \r\n");
		return -ENOMEM;
	}
	dev_dbg(dev,"data->num_res_mem=%d\n", data->num_res_mem);

	data->num_res_irq = rockchip_get_iommu_resource_num(pdev,
				IORESOURCE_IRQ);
	if (0 == data->num_res_irq) {
		dev_err(dev,"can't find iommu irq resource \r\n");
		return -ENOMEM;
	}
	dev_dbg(dev,"data->num_res_irq=%d\n", data->num_res_irq);

	data->res_bases = devm_kmalloc_array(dev, data->num_res_mem,
				sizeof(*data->res_bases), GFP_KERNEL);
	if (data->res_bases == NULL) {
		dev_err(dev, "Not enough memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < data->num_res_mem; i++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev,"Unable to find IOMEM region\n");
			return -ENOENT;
		}

		data->res_bases[i] = devm_ioremap(dev,res->start,
						  resource_size(res));
		if (!data->res_bases[i]) {
			dev_err(dev, "Unable to map IOMEM @ PA:%pa\n",
				&res->start);
			return -ENOMEM;
		}

		dev_dbg(dev,"res->start = 0x%pa ioremap to data->res_bases[%d] = %p\n",
			&res->start, i, data->res_bases[i]);

		if (strstr(data->dbgname, "vop") &&
		    (soc_is_rk3128() || soc_is_rk3126())) {
			rk312x_vop_mmu_base = data->res_bases[0];
			dev_dbg(dev, "rk312x_vop_mmu_base = %p\n",
				rk312x_vop_mmu_base);
		}
	}

	for (i = 0; i < data->num_res_irq; i++) {
		if ((soc_is_rk3128() || soc_is_rk3126()) &&
		    strstr(data->dbgname, "vop")) {
			dev_info(dev, "skip request vop mmu irq\n");
			continue;
		}

		ret = platform_get_irq(pdev, i);
		if (ret <= 0) {
			dev_err(dev,"Unable to find IRQ resource\n");
			return -ENOENT;
		}

		ret = devm_request_irq(dev, ret, rockchip_iommu_irq,
				  IRQF_SHARED, dev_name(dev), data);
		if (ret) {
			dev_err(dev, "Unabled to register interrupt handler\n");
			return -ENOENT;
		}
	}

	ret = rockchip_init_iovmm(dev, &data->vmm);
	if (ret)
		return ret;

	data->iommu = dev;
	spin_lock_init(&data->data_lock);
	INIT_LIST_HEAD(&data->node);

	dev_info(dev,"(%s) Initialized\n", data->dbgname);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id iommu_dt_ids[] = {
	{ .compatible = IEP_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VIP_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VOPB_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VOPL_IOMMU_COMPATIBLE_NAME},
	{ .compatible = HEVC_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VPU_IOMMU_COMPATIBLE_NAME},
	{ .compatible = ISP_IOMMU_COMPATIBLE_NAME},
	{ .compatible = ISP0_IOMMU_COMPATIBLE_NAME},
	{ .compatible = ISP1_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VOP_IOMMU_COMPATIBLE_NAME},
	{ .compatible = VDEC_IOMMU_COMPATIBLE_NAME},
	{ /* end */ }
};

MODULE_DEVICE_TABLE(of, iommu_dt_ids);
#endif

static struct platform_driver rk_iommu_driver = {
	.probe = rockchip_iommu_probe,
	.remove = NULL,
	.driver = {
		   .name = "rk_iommu",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(iommu_dt_ids),
	},
};

static int __init rockchip_iommu_init_driver(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, iommu_dt_ids);
	if (!np) {
		pr_err("Failed to find legacy iommu devices\n");
		return -ENODEV;
	}

	lv2table_kmem_cache = kmem_cache_create("rk-iommu-lv2table",
						LV2TABLE_SIZE, LV2TABLE_SIZE,
						0, NULL);
	if (!lv2table_kmem_cache) {
		pr_info("%s: failed to create kmem cache\n", __func__);
		return -ENOMEM;
	}

	ret = bus_set_iommu(&platform_bus_type, &rk_iommu_ops);
	if (ret)
		return ret;

	return platform_driver_register(&rk_iommu_driver);
}

core_initcall(rockchip_iommu_init_driver);
