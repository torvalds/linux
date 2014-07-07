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
#include <linux/rockchip/sysmmu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#include "rockchip-iommu.h"

/* We does not consider super section mapping (16MB) */
#define SPAGE_ORDER 12
#define SPAGE_SIZE (1 << SPAGE_ORDER)
#define SPAGE_MASK (~(SPAGE_SIZE - 1))
typedef enum sysmmu_entry_flags 
{
	SYSMMU_FLAGS_PRESENT = 0x01,
	SYSMMU_FLAGS_READ_PERMISSION = 0x02,
	SYSMMU_FLAGS_WRITE_PERMISSION = 0x04,
	SYSMMU_FLAGS_OVERRIDE_CACHE  = 0x8,
	SYSMMU_FLAGS_WRITE_CACHEABLE  = 0x10,
	SYSMMU_FLAGS_WRITE_ALLOCATE  = 0x20,
	SYSMMU_FLAGS_WRITE_BUFFERABLE  = 0x40,
	SYSMMU_FLAGS_READ_CACHEABLE  = 0x80,
	SYSMMU_FLAGS_READ_ALLOCATE  = 0x100,
	SYSMMU_FLAGS_MASK = 0x1FF,
} sysmmu_entry_flags;

#define lv1ent_fault(sent) ((*(sent) & SYSMMU_FLAGS_PRESENT) == 0)
#define lv1ent_page(sent) ((*(sent) & SYSMMU_FLAGS_PRESENT) == 1)
#define lv2ent_fault(pent) ((*(pent) & SYSMMU_FLAGS_PRESENT) == 0)
#define spage_phys(pent) (*(pent) & SPAGE_MASK)
#define spage_offs(iova) ((iova) & 0x0FFF)

#define lv1ent_offset(iova) (((iova)>>22) & 0x03FF)
#define lv2ent_offset(iova) (((iova)>>12) & 0x03FF)

#define NUM_LV1ENTRIES 1024
#define NUM_LV2ENTRIES 1024

#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(long))

#define lv2table_base(sent) (*(sent) & 0xFFFFFFFE)

#define mk_lv1ent_page(pa) ((pa) | SYSMMU_FLAGS_PRESENT)
/*write and read permission for level2 page default*/
#define mk_lv2ent_spage(pa) ((pa) | SYSMMU_FLAGS_PRESENT |SYSMMU_FLAGS_READ_PERMISSION |SYSMMU_FLAGS_WRITE_PERMISSION)

#define SYSMMU_REG_POLL_COUNT_FAST 1000

/*rk3036:vpu and hevc share ahb interface*/
#define BIT_VCODEC_SEL (1<<3)


/**
 * MMU register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum sysmmu_register 
{
	SYSMMU_REGISTER_DTE_ADDR = 0x0000, /**< Current Page Directory Pointer */
	SYSMMU_REGISTER_STATUS = 0x0004, /**< Status of the MMU */
	SYSMMU_REGISTER_COMMAND = 0x0008, /**< Command register, used to control the MMU */
	SYSMMU_REGISTER_PAGE_FAULT_ADDR = 0x000C, /**< Logical address of the last page fault */
	SYSMMU_REGISTER_ZAP_ONE_LINE = 0x010, /**< Used to invalidate the mapping of a single page from the MMU */
	SYSMMU_REGISTER_INT_RAWSTAT = 0x0014, /**< Raw interrupt status, all interrupts visible */
	SYSMMU_REGISTER_INT_CLEAR = 0x0018, /**< Indicate to the MMU that the interrupt has been received */
	SYSMMU_REGISTER_INT_MASK = 0x001C, /**< Enable/disable types of interrupts */
	SYSMMU_REGISTER_INT_STATUS = 0x0020, /**< Interrupt status based on the mask */
	SYSMMU_REGISTER_AUTO_GATING	= 0x0024
} sysmmu_register;

typedef enum sysmmu_command 
{
	SYSMMU_COMMAND_ENABLE_PAGING = 0x00, /**< Enable paging (memory translation) */
	SYSMMU_COMMAND_DISABLE_PAGING = 0x01, /**< Disable paging (memory translation) */
	SYSMMU_COMMAND_ENABLE_STALL = 0x02, /**<  Enable stall on page fault */
	SYSMMU_COMMAND_DISABLE_STALL = 0x03, /**< Disable stall on page fault */
	SYSMMU_COMMAND_ZAP_CACHE = 0x04, /**< Zap the entire page table cache */
	SYSMMU_COMMAND_PAGE_FAULT_DONE = 0x05, /**< Page fault processed */
	SYSMMU_COMMAND_HARD_RESET = 0x06 /**< Reset the MMU back to power-on settings */
} sysmmu_command;

/**
 * MMU interrupt register bits
 * Each cause of the interrupt is reported
 * through the (raw) interrupt status registers.
 * Multiple interrupts can be pending, so multiple bits
 * can be set at once.
 */
typedef enum sysmmu_interrupt 
{
	SYSMMU_INTERRUPT_PAGE_FAULT = 0x01, /**< A page fault occured */
	SYSMMU_INTERRUPT_READ_BUS_ERROR = 0x02 /**< A bus read error occured */
} sysmmu_interrupt;

typedef enum sysmmu_status_bits 
{
	SYSMMU_STATUS_BIT_PAGING_ENABLED      = 1 << 0,
	SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE   = 1 << 1,
	SYSMMU_STATUS_BIT_STALL_ACTIVE        = 1 << 2,
	SYSMMU_STATUS_BIT_IDLE                = 1 << 3,
	SYSMMU_STATUS_BIT_REPLAY_BUFFER_EMPTY = 1 << 4,
	SYSMMU_STATUS_BIT_PAGE_FAULT_IS_WRITE = 1 << 5,
	SYSMMU_STATUS_BIT_STALL_NOT_ACTIVE    = 1 << 31,
} sys_mmu_status_bits;

/**
 * Size of an MMU page in bytes
 */
#define SYSMMU_PAGE_SIZE 0x1000

/*
 * Size of the address space referenced by a page table page
 */
#define SYSMMU_VIRTUAL_PAGE_SIZE 0x400000 /* 4 MiB */

/**
 * Page directory index from address
 * Calculates the page directory index from the given address
 */
#define SYSMMU_PDE_ENTRY(address) (((address)>>22) & 0x03FF)

/**
 * Page table index from address
 * Calculates the page table index from the given address
 */
#define SYSMMU_PTE_ENTRY(address) (((address)>>12) & 0x03FF)

/**
 * Extract the memory address from an PDE/PTE entry
 */
#define SYSMMU_ENTRY_ADDRESS(value) ((value) & 0xFFFFFC00)

#define INVALID_PAGE ((u32)(~0))

static struct kmem_cache *lv2table_kmem_cache;

static void rockchip_vcodec_select(const char *string)
{
	if(strstr(string,"hevc"))
	{
		writel_relaxed(readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_CON1) |
	       (BIT_VCODEC_SEL) | (BIT_VCODEC_SEL << 16),
	       RK_GRF_VIRT + RK3036_GRF_SOC_CON1);
	}
	else if(strstr(string,"vpu"))
	{
		writel_relaxed(readl_relaxed(RK_GRF_VIRT + RK3036_GRF_SOC_CON1) |
	      (BIT_VCODEC_SEL << 16),
	       RK_GRF_VIRT + RK3036_GRF_SOC_CON1);
	}
}
static unsigned long *section_entry(unsigned long *pgtable, unsigned long iova)
{
	return pgtable + lv1ent_offset(iova);
}

static unsigned long *page_entry(unsigned long *sent, unsigned long iova)
{
	return (unsigned long *)__va(lv2table_base(sent)) + lv2ent_offset(iova);
}

static char *sysmmu_fault_name[SYSMMU_FAULTS_NUM] = {
	"PAGE FAULT",
	"BUS ERROR",
	"UNKNOWN FAULT"
};

struct rk_iommu_domain {
	struct list_head clients; /* list of sysmmu_drvdata.node */
	unsigned long *pgtable; /* lv1 page table, 4KB */
	short *lv2entcnt; /* free lv2 entry counter for each section */
	spinlock_t lock; /* lock for this structure */
	spinlock_t pgtablelock; /* lock for modifying page table @ pgtable */
};

static bool set_sysmmu_active(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU was not active previously
	   and it needs to be initialized */
	return ++data->activations == 1;
}

static bool set_sysmmu_inactive(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU is needed to be disabled */
	BUG_ON(data->activations < 1);
	return --data->activations == 0;
}

static bool is_sysmmu_active(struct sysmmu_drvdata *data)
{
	return data->activations > 0;
}
static void sysmmu_disable_stall(void __iomem *base)
{
	int i;
	u32 mmu_status = __raw_readl(base+SYSMMU_REGISTER_STATUS);
	if ( 0 == (mmu_status & SYSMMU_STATUS_BIT_PAGING_ENABLED )) 
	{
		return;
	}
	if (mmu_status & SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE) 
	{
		pr_err("Aborting MMU disable stall request since it is in pagefault state.\n");
		return;
	}
	
	__raw_writel(SYSMMU_COMMAND_DISABLE_STALL, base + SYSMMU_REGISTER_COMMAND);
	
	for (i = 0; i < SYSMMU_REG_POLL_COUNT_FAST; ++i) 
	{
		u32 status = __raw_readl(base + SYSMMU_REGISTER_STATUS);
		if ( 0 == (status & SYSMMU_STATUS_BIT_STALL_ACTIVE) ) 
		{
			break;
		}
		if ( status &  SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE ) 
		{
			break;
		}
		if ( 0 == (mmu_status & SYSMMU_STATUS_BIT_PAGING_ENABLED )) 
		{
			break;
		}
	}
	if (SYSMMU_REG_POLL_COUNT_FAST == i) 
		pr_err("Disable stall request failed, MMU status is 0x%08X\n", __raw_readl(base + SYSMMU_REGISTER_STATUS));
}
static bool sysmmu_enable_stall(void __iomem *base)
{
	int i;
	u32 mmu_status = __raw_readl(base + SYSMMU_REGISTER_STATUS);

	if ( 0 == (mmu_status & SYSMMU_STATUS_BIT_PAGING_ENABLED) ) 
	{
		/*pr_info("MMU stall is implicit when Paging is not enabled.\n");*/
		return true;
	}
	if ( mmu_status & SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE ) 
	{
		pr_err("Aborting MMU stall request since it is in pagefault state.\n");
		return false;
	}
	
	__raw_writel(SYSMMU_COMMAND_ENABLE_STALL, base + SYSMMU_REGISTER_COMMAND);

	for (i = 0; i < SYSMMU_REG_POLL_COUNT_FAST; ++i) 
	{
		mmu_status = __raw_readl(base + SYSMMU_REGISTER_STATUS);
		if (mmu_status & SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE) 
		{
			break;
		}
		if ((mmu_status & SYSMMU_STATUS_BIT_STALL_ACTIVE)&&(0==(mmu_status & SYSMMU_STATUS_BIT_STALL_NOT_ACTIVE))) 
		{
			break;
		}
		if (0 == (mmu_status & ( SYSMMU_STATUS_BIT_PAGING_ENABLED ))) 
		{
			break;
		}
	}
	if (SYSMMU_REG_POLL_COUNT_FAST == i) 
	{
		pr_err("Enable stall request failed, MMU status is 0x%08X\n", __raw_readl(base + SYSMMU_REGISTER_STATUS));
		return false;
	}
	if ( mmu_status & SYSMMU_STATUS_BIT_PAGE_FAULT_ACTIVE ) 
	{
		pr_err("Aborting MMU stall request since it has a pagefault.\n");
		return false;
	}
	return true;
}

static bool sysmmu_enable_paging(void __iomem *base)
{
	int i;
	__raw_writel(SYSMMU_COMMAND_ENABLE_PAGING, base + SYSMMU_REGISTER_COMMAND);

	for (i = 0; i < SYSMMU_REG_POLL_COUNT_FAST; ++i) 
	{
		if (__raw_readl(base + SYSMMU_REGISTER_STATUS) & SYSMMU_STATUS_BIT_PAGING_ENABLED) 
		{
			/*pr_info("Enable paging request success.\n");*/
			break;
		}
	}
	if (SYSMMU_REG_POLL_COUNT_FAST == i)
	{
		pr_err("Enable paging request failed, MMU status is 0x%08X\n", __raw_readl(base + SYSMMU_REGISTER_STATUS));
		return false;
	}
	return true;
}
static bool sysmmu_disable_paging(void __iomem *base)
{
	int i;
	__raw_writel(SYSMMU_COMMAND_DISABLE_PAGING, base + SYSMMU_REGISTER_COMMAND);

	for (i = 0; i < SYSMMU_REG_POLL_COUNT_FAST; ++i) 
	{
		if (!(__raw_readl(base + SYSMMU_REGISTER_STATUS) & SYSMMU_STATUS_BIT_PAGING_ENABLED)) 
		{
			/*pr_info("Disable paging request success.\n");*/
			break;
		}
	}
	if (SYSMMU_REG_POLL_COUNT_FAST == i)
	{
		pr_err("Disable paging request failed, MMU status is 0x%08X\n", __raw_readl(base + SYSMMU_REGISTER_STATUS));
		return false;
	}
	return true;
}

static void sysmmu_page_fault_done(void __iomem *base,const char *dbgname)
{
	pr_info("MMU: %s: Leaving page fault mode\n", dbgname);
	__raw_writel(SYSMMU_COMMAND_PAGE_FAULT_DONE, base + SYSMMU_REGISTER_COMMAND);
}
static bool sysmmu_zap_tlb(void __iomem *base)
{
	bool stall_success = sysmmu_enable_stall(base);
	
	__raw_writel(SYSMMU_COMMAND_ZAP_CACHE, base + SYSMMU_REGISTER_COMMAND);
	if (false == stall_success) 
	{
		/* False means that it is in Pagefault state. Not possible to disable_stall then */
		return false;
	}
	sysmmu_disable_stall(base);
	return true;
}
static inline bool sysmmu_raw_reset(void __iomem *base)
{
	int i;
	__raw_writel(0xCAFEBABE, base + SYSMMU_REGISTER_DTE_ADDR);

	if(!(0xCAFEB000 == __raw_readl(base+SYSMMU_REGISTER_DTE_ADDR)))
	{
		pr_err("error when %s.\n",__func__);
		return false;
	}
	__raw_writel(SYSMMU_COMMAND_HARD_RESET, base + SYSMMU_REGISTER_COMMAND);

	for (i = 0; i < SYSMMU_REG_POLL_COUNT_FAST; ++i) 
	{
		if(__raw_readl(base + SYSMMU_REGISTER_DTE_ADDR) == 0)
		{
			break;
		}
	}
	if (SYSMMU_REG_POLL_COUNT_FAST == i) {
		pr_err("%s,Reset request failed, MMU status is 0x%08X\n", __func__,__raw_readl(base + SYSMMU_REGISTER_DTE_ADDR));
		return false;
	}
	return true;
}

static void __sysmmu_set_ptbase(void __iomem *base,unsigned long pgd)
{
	__raw_writel(pgd, base + SYSMMU_REGISTER_DTE_ADDR);

}

static bool sysmmu_reset(void __iomem *base,const char *dbgname)
{
	bool err = true;
	
	err = sysmmu_enable_stall(base);
	if(!err)
	{
		pr_err("%s:stall failed: %s\n",__func__,dbgname);
		return err;
	}
	err = sysmmu_raw_reset(base);
	if(err)
	{
		__raw_writel(SYSMMU_INTERRUPT_PAGE_FAULT|SYSMMU_INTERRUPT_READ_BUS_ERROR, base+SYSMMU_REGISTER_INT_MASK);
	}
	sysmmu_disable_stall(base);
	if(!err)
		pr_err("%s: failed: %s\n", __func__,dbgname);
	return err;
}

static inline void pgtable_flush(void *vastart, void *vaend)
{
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart),virt_to_phys(vaend));
}
static void __set_fault_handler(struct sysmmu_drvdata *data,
					sysmmu_fault_handler_t handler)
{
	unsigned long flags;

	write_lock_irqsave(&data->lock, flags);
	data->fault_handler = handler;
	write_unlock_irqrestore(&data->lock, flags);
}

void rockchip_sysmmu_set_fault_handler(struct device *dev,sysmmu_fault_handler_t handler)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	__set_fault_handler(data, handler);
}

static int default_fault_handler(struct device *dev,
					enum rk_sysmmu_inttype itype,
					unsigned long pgtable_base,
					unsigned long fault_addr,
					unsigned int status
					)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	if(!data)
	{
		pr_err("%s,iommu device not assigned yet\n",__func__);
		return 0;
	}
	if ((itype >= SYSMMU_FAULTS_NUM) || (itype < SYSMMU_PAGEFAULT))
		itype = SYSMMU_FAULT_UNKNOWN;

	if(itype == SYSMMU_BUSERROR)
		pr_err("%s occured at 0x%lx(Page table base: 0x%lx)\n",sysmmu_fault_name[itype], fault_addr, pgtable_base);

	if(itype == SYSMMU_PAGEFAULT)
		pr_err("SYSMMU:Page fault detected at 0x%lx from bus id %d of type %s on %s\n",
				fault_addr,
				(status >> 6) & 0x1F,
				(status & 32) ? "write" : "read",
				data->dbgname
				);

	pr_err("Generating Kernel OOPS... because it is unrecoverable.\n");

	BUG();

	return 0;
}
static void dump_pagetbl(u32 fault_address,u32 addr_dte)
{
	u32 lv1_offset;
	u32 lv2_offset;
	
	u32 *lv1_entry_pa;
	u32 *lv1_entry_va;
	u32 *lv1_entry_value;
	
	u32 *lv2_base;
	u32 *lv2_entry_pa;
	u32 *lv2_entry_va;
	u32 *lv2_entry_value;

	
	lv1_offset = lv1ent_offset(fault_address);
	lv2_offset = lv2ent_offset(fault_address);
	
	lv1_entry_pa = (u32 *)addr_dte + lv1_offset;
	lv1_entry_va = (u32 *)(__va(addr_dte)) + lv1_offset;
	lv1_entry_value = (u32 *)(*lv1_entry_va);
	
	lv2_base = (u32 *)((*lv1_entry_va) & 0xfffffffe);
	lv2_entry_pa = (u32 * )lv2_base + lv2_offset;
	lv2_entry_va = (u32 * )(__va(lv2_base)) + lv2_offset;
	lv2_entry_value = (u32 *)(*lv2_entry_va);
	
	pr_info("fault address = 0x%08x,dte addr pa = 0x%08x,va = 0x%08x\n",fault_address,addr_dte,(u32)__va(addr_dte));
	pr_info("lv1_offset = 0x%x,lv1_entry_pa = 0x%08x,lv1_entry_va = 0x%08x\n",lv1_offset,(u32)lv1_entry_pa,(u32)lv1_entry_va);
	pr_info("lv1_entry_value(*lv1_entry_va) = 0x%08x,lv2_base = 0x%08x\n",(u32)lv1_entry_value,(u32)lv2_base);
	pr_info("lv2_offset = 0x%x,lv2_entry_pa = 0x%08x,lv2_entry_va = 0x%08x\n",lv2_offset,(u32)lv2_entry_pa,(u32)lv2_entry_va);
	pr_info("lv2_entry value(*lv2_entry_va) = 0x%08x\n",(u32)lv2_entry_value);
}
static irqreturn_t rockchip_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	struct sysmmu_drvdata *data = dev_id;
	struct resource *irqres;
	struct platform_device *pdev;
	enum rk_sysmmu_inttype itype = SYSMMU_FAULT_UNKNOWN;
	u32 status;
	u32 rawstat;
	u32 int_status;
	u32 fault_address;
	int i, ret = 0;

	read_lock(&data->lock);
	
#if 0
	WARN_ON(!is_sysmmu_active(data));
#else
	if(!is_sysmmu_active(data))
	{
		read_unlock(&data->lock);
		return IRQ_HANDLED;
	}
#endif	
	rockchip_vcodec_select(data->dbgname);

	pdev = to_platform_device(data->sysmmu);

	for (i = 0; i < data->num_res_irq; i++) 
	{
		irqres = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (irqres && ((int)irqres->start == irq))
			break;
	}

	if (i == data->num_res_irq) 
	{
		itype = SYSMMU_FAULT_UNKNOWN;
	} 
	else 
	{
		int_status = __raw_readl(data->res_bases[i] + SYSMMU_REGISTER_INT_STATUS);
		
		if(int_status != 0)
		{
			/*mask status*/
			__raw_writel(0x00,data->res_bases[i] + SYSMMU_REGISTER_INT_MASK);
			
			rawstat = __raw_readl(data->res_bases[i] + SYSMMU_REGISTER_INT_RAWSTAT);

			if(rawstat & SYSMMU_INTERRUPT_PAGE_FAULT)
			{
				fault_address = __raw_readl(data->res_bases[i] + SYSMMU_REGISTER_PAGE_FAULT_ADDR);
				itype = SYSMMU_PAGEFAULT;
			}
			else if(rawstat & SYSMMU_INTERRUPT_READ_BUS_ERROR)
			{
				itype = SYSMMU_BUSERROR;
			}
			else
			{
				goto out;
			}
			dump_pagetbl(fault_address,__raw_readl(data->res_bases[i] + SYSMMU_REGISTER_DTE_ADDR));
		}
		else
			goto out;
	}
	
	if (data->fault_handler) 
	{
		unsigned long base = __raw_readl(data->res_bases[i] + SYSMMU_REGISTER_DTE_ADDR);
		status = __raw_readl(data->res_bases[i] + SYSMMU_REGISTER_STATUS);
		ret = data->fault_handler(data->dev, itype, base, fault_address,status);
	}

	if (!ret && (itype != SYSMMU_FAULT_UNKNOWN))
	{
		if(SYSMMU_PAGEFAULT == itype)
		{
			sysmmu_zap_tlb(data->res_bases[i]);
			sysmmu_page_fault_done(data->res_bases[i],data->dbgname);
			__raw_writel(SYSMMU_INTERRUPT_PAGE_FAULT|SYSMMU_INTERRUPT_READ_BUS_ERROR, data->res_bases[i]+SYSMMU_REGISTER_INT_MASK);
		}
	}
	else
		pr_err("(%s) %s is not handled.\n",data->dbgname, sysmmu_fault_name[itype]);

out :
	read_unlock(&data->lock);

	return IRQ_HANDLED;
}

static bool __rockchip_sysmmu_disable(struct sysmmu_drvdata *data)
{
	unsigned long flags;
	bool disabled = false;
	int i;
	write_lock_irqsave(&data->lock, flags);

	if (!set_sysmmu_inactive(data))
		goto finish;

	for(i=0;i<data->num_res_mem;i++)
	{
		sysmmu_disable_paging(data->res_bases[i]);
	}

	disabled = true;
	data->pgtable = 0;
	data->domain = NULL;
finish:
	write_unlock_irqrestore(&data->lock, flags);

	if (disabled)
		pr_info("(%s) Disabled\n", data->dbgname);
	else
		pr_info("(%s) %d times left to be disabled\n",data->dbgname, data->activations);

	return disabled;
}

/* __rk_sysmmu_enable: Enables System MMU
 *
 * returns -error if an error occurred and System MMU is not enabled,
 * 0 if the System MMU has been just enabled and 1 if System MMU was already
 * enabled before.
 */
static int __rockchip_sysmmu_enable(struct sysmmu_drvdata *data,unsigned long pgtable, struct iommu_domain *domain)
{
	int i, ret = 0;
	unsigned long flags;

	write_lock_irqsave(&data->lock, flags);

	if (!set_sysmmu_active(data)) 
	{
		if (WARN_ON(pgtable != data->pgtable)) 
		{
			ret = -EBUSY;
			set_sysmmu_inactive(data);
		} 
		else 
			ret = 1;

		pr_info("(%s) Already enabled\n", data->dbgname);
		goto finish;
	}
	
	data->pgtable = pgtable;

	for (i = 0; i < data->num_res_mem; i++) 
	{
		bool status;
		status = sysmmu_enable_stall(data->res_bases[i]);
		if(status)
		{
			__sysmmu_set_ptbase(data->res_bases[i], pgtable);
			__raw_writel(SYSMMU_COMMAND_ZAP_CACHE, data->res_bases[i] + SYSMMU_REGISTER_COMMAND);
		}
		__raw_writel(SYSMMU_INTERRUPT_PAGE_FAULT|SYSMMU_INTERRUPT_READ_BUS_ERROR, data->res_bases[i]+SYSMMU_REGISTER_INT_MASK);
		sysmmu_enable_paging(data->res_bases[i]);
		sysmmu_disable_stall(data->res_bases[i]);
	}

	data->domain = domain;

	pr_info("(%s) Enabled\n", data->dbgname);
finish:
	write_unlock_irqrestore(&data->lock, flags);

	return ret;
}
bool rockchip_sysmmu_disable(struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	bool disabled;

	disabled = __rockchip_sysmmu_disable(data);

	return disabled;
}
void rockchip_sysmmu_tlb_invalidate(struct device *dev)
{
	unsigned long flags;
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	read_lock_irqsave(&data->lock, flags);
	
	rockchip_vcodec_select(data->dbgname);

	if (is_sysmmu_active(data)) 
	{
		int i;
		for (i = 0; i < data->num_res_mem; i++) 
		{
			if(!sysmmu_zap_tlb(data->res_bases[i]))
				pr_err("%s,invalidating TLB failed\n",data->dbgname);
		}
	} 
	else 
		pr_info("(%s) Disabled. Skipping invalidating TLB.\n",data->dbgname);

	read_unlock_irqrestore(&data->lock, flags);
}
static phys_addr_t rockchip_iommu_iova_to_phys(struct iommu_domain *domain,dma_addr_t iova)
{
	struct rk_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	phys_addr_t phys = 0;

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);
	entry = page_entry(entry, iova);
	phys = spage_phys(entry) + spage_offs(iova);
	
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return phys;
}
static int lv2set_page(unsigned long *pent, phys_addr_t paddr, size_t size,
								short *pgcnt)
{
	if (!lv2ent_fault(pent))
		return -EADDRINUSE;

	*pent = mk_lv2ent_spage(paddr);
	pgtable_flush(pent, pent + 1);
	*pgcnt -= 1;
	return 0;
}

static unsigned long *alloc_lv2entry(unsigned long *sent, unsigned long iova,short *pgcounter)
{
	if (lv1ent_fault(sent)) 
	{
		unsigned long *pent;

		pent = kmem_cache_zalloc(lv2table_kmem_cache, GFP_ATOMIC);
		BUG_ON((unsigned long)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return NULL;

		*sent = mk_lv1ent_page(__pa(pent));
		kmemleak_ignore(pent);
		*pgcounter = NUM_LV2ENTRIES;
		pgtable_flush(pent, pent + NUM_LV2ENTRIES);
		pgtable_flush(sent, sent + 1);
	}
	return page_entry(sent, iova);
}

static size_t rockchip_iommu_unmap(struct iommu_domain *domain,unsigned long iova, size_t size)
{
	struct rk_iommu_domain *priv = domain->priv;
	unsigned long flags;
	unsigned long *ent;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	ent = section_entry(priv->pgtable, iova);

	if (unlikely(lv1ent_fault(ent))) 
	{
		if (size > SPAGE_SIZE)
			size = SPAGE_SIZE;
		goto done;
	}

	/* lv1ent_page(sent) == true here */

	ent = page_entry(ent, iova);

	if (unlikely(lv2ent_fault(ent))) 
	{
		size = SPAGE_SIZE;
		goto done;
	}
	
	*ent = 0;
	size = SPAGE_SIZE;
	priv->lv2entcnt[lv1ent_offset(iova)] += 1;
	goto done;

done:
	//pr_info("%s:unmap iova 0x%lx/0x%x bytes\n",__func__, iova,size);
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return size;
}
static int rockchip_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot)
{
	struct rk_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	int ret = -ENOMEM;
	unsigned long *pent;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);
	
	pent = alloc_lv2entry(entry, iova,&priv->lv2entcnt[lv1ent_offset(iova)]);
	if (!pent)
		ret = -ENOMEM;
	else
		ret = lv2set_page(pent, paddr, size,&priv->lv2entcnt[lv1ent_offset(iova)]);
	
	if (ret)
	{
		pr_err("%s: Failed to map iova 0x%lx/0x%x bytes\n",__func__, iova, size);
	}
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return ret;
}

static void rockchip_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct rk_iommu_domain *priv = domain->priv;
	struct list_head *pos;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each(pos, &priv->clients) 
	{
		if (list_entry(pos, struct sysmmu_drvdata, node) == data) 
		{
			found = true;
			break;
		}
	}
	if (!found)
		goto finish;
	
	rockchip_vcodec_select(data->dbgname);

	if (__rockchip_sysmmu_disable(data)) 
	{
		pr_info("%s: Detached IOMMU with pgtable %#lx\n",__func__, __pa(priv->pgtable));
		list_del(&data->node);
		INIT_LIST_HEAD(&data->node);

	} 
	else 
		pr_info("%s: Detaching IOMMU with pgtable %#lx delayed",__func__, __pa(priv->pgtable));
	
finish:
	spin_unlock_irqrestore(&priv->lock, flags);
}
static int rockchip_iommu_attach_device(struct iommu_domain *domain,struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct rk_iommu_domain *priv = domain->priv;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);
	
	rockchip_vcodec_select(data->dbgname);

	ret = __rockchip_sysmmu_enable(data, __pa(priv->pgtable), domain);

	if (ret == 0) 
	{
		/* 'data->node' must not be appeared in priv->clients */
		BUG_ON(!list_empty(&data->node));
		data->dev = dev;
		list_add_tail(&data->node, &priv->clients);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret < 0) 
	{
		pr_err("%s: Failed to attach IOMMU with pgtable %#lx\n",__func__, __pa(priv->pgtable));
	} 
	else if (ret > 0) 
	{
		pr_info("%s: IOMMU with pgtable 0x%lx already attached\n",__func__, __pa(priv->pgtable));
	} 
	else 
	{
		pr_info("%s: Attached new IOMMU with pgtable 0x%lx\n",__func__, __pa(priv->pgtable));
	}

	return ret;
}
static void rockchip_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct rk_iommu_domain *priv = domain->priv;
	struct sysmmu_drvdata *data;
	unsigned long flags;
	int i;

	WARN_ON(!list_empty(&priv->clients));

	spin_lock_irqsave(&priv->lock, flags);
	
	list_for_each_entry(data, &priv->clients, node) 
	{
		rockchip_vcodec_select(data->dbgname);
		while (!rockchip_sysmmu_disable(data->dev))
			; /* until System MMU is actually disabled */
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (lv1ent_page(priv->pgtable + i))
			kmem_cache_free(lv2table_kmem_cache,__va(lv2table_base(priv->pgtable + i)));

	free_pages((unsigned long)priv->pgtable, 0);
	free_pages((unsigned long)priv->lv2entcnt, 0);
	kfree(domain->priv);
	domain->priv = NULL;
}

static int rockchip_iommu_domain_init(struct iommu_domain *domain)
{
	struct rk_iommu_domain *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
/*rk32xx sysmmu use 2 level pagetable,
   level1 and leve2 both have 1024 entries,each entry  occupy 4 bytes,
   so alloc a page size for each page table 
*/
	priv->pgtable = (unsigned long *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!priv->pgtable)
		goto err_pgtable;

	priv->lv2entcnt = (short *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!priv->lv2entcnt)
		goto err_counter;

	pgtable_flush(priv->pgtable, priv->pgtable + NUM_LV1ENTRIES);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->pgtablelock);
	INIT_LIST_HEAD(&priv->clients);

	domain->priv = priv;
	return 0;

err_counter:
	free_pages((unsigned long)priv->pgtable, 0);	
err_pgtable:
	kfree(priv);
	return -ENOMEM;
}

static struct iommu_ops rk_iommu_ops = 
{
	.domain_init = &rockchip_iommu_domain_init,
	.domain_destroy = &rockchip_iommu_domain_destroy,
	.attach_dev = &rockchip_iommu_attach_device,
	.detach_dev = &rockchip_iommu_detach_device,
	.map = &rockchip_iommu_map,
	.unmap = &rockchip_iommu_unmap,
	.iova_to_phys = &rockchip_iommu_iova_to_phys,
	.pgsize_bitmap = SPAGE_SIZE,
};

static int rockchip_sysmmu_prepare(void)
{
	int ret = 0;
	static int registed = 0;
	
	if(registed)
		return 0;
	
	lv2table_kmem_cache = kmem_cache_create("rk-iommu-lv2table",LV2TABLE_SIZE, LV2TABLE_SIZE, 0, NULL);
	if (!lv2table_kmem_cache) 
	{
		pr_err("%s: failed to create kmem cache\n", __func__);
		return -ENOMEM;
	}
	ret = bus_set_iommu(&platform_bus_type, &rk_iommu_ops);
	if(!ret)
		registed = 1;
	else
		pr_err("%s:failed to set iommu to bus\r\n",__func__);
	return ret;
}
static int  rockchip_get_sysmmu_resource_num(struct platform_device *pdev,unsigned int type)
{
	struct resource *info = NULL;
	int num_resources = 0;
	
	/*get resouce info*/
again:
	info = platform_get_resource(pdev, type, num_resources);
	while(info)
	{
		num_resources++;
		goto again;
	}
	return num_resources;
}

static struct kobject *dump_mmu_object;

static int dump_mmu_pagetbl(struct device *dev,struct device_attribute *attr, const char *buf,u32 count)
{
	u32 fault_address;
	u32 iommu_dte ;
	u32 mmu_base;
	void __iomem *base;
	u32 ret;
	ret = kstrtouint(buf,0,&mmu_base);
	if (ret)
		printk("%s is not in hexdecimal form.\n", buf);
	base = ioremap(mmu_base, 0x100);
	iommu_dte = __raw_readl(base + SYSMMU_REGISTER_DTE_ADDR);
	fault_address = __raw_readl(base + SYSMMU_REGISTER_PAGE_FAULT_ADDR);
	dump_pagetbl(fault_address,iommu_dte);
	return count;
}
static DEVICE_ATTR(dump_mmu_pgtable, 0644, NULL, dump_mmu_pagetbl);

void dump_iommu_sysfs_init(void )
{
	u32 ret;
	dump_mmu_object = kobject_create_and_add("rk_iommu", NULL);
	if (dump_mmu_object == NULL)
		return;
	ret = sysfs_create_file(dump_mmu_object, &dev_attr_dump_mmu_pgtable.attr);
	return;
}
	


static int rockchip_sysmmu_probe(struct platform_device *pdev)
{
	int i, ret;
	struct device *dev;
	struct sysmmu_drvdata *data;
	
	dev = &pdev->dev;
	
	ret = rockchip_sysmmu_prepare();
	if(ret)
	{
		pr_err("%s,failed\r\n",__func__);
		goto err_alloc;
	}

	data = devm_kzalloc(dev,sizeof(*data), GFP_KERNEL);
	if (!data) 
	{
		dev_dbg(dev, "Not enough memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	
	ret = dev_set_drvdata(dev, data);
	if (ret) 
	{
		dev_dbg(dev, "Unabled to initialize driver data\n");
		goto err_init;
	}
	
	if(pdev->dev.of_node)
	{
		of_property_read_string(pdev->dev.of_node,"dbgname",&(data->dbgname));
	}
	else
	{
		pr_info("dbgname not assigned in device tree or device node not exist\r\n");
	}

	pr_info("(%s) Enter\n", data->dbgname);

	/*rk32xx sysmmu need both irq and memory */
	data->num_res_mem = rockchip_get_sysmmu_resource_num(pdev,IORESOURCE_MEM);
	if(0 == data->num_res_mem)
	{
		pr_err("can't find sysmmu memory resource \r\n");
		goto err_init;
	}
	pr_info("data->num_res_mem=%d\n",data->num_res_mem);
	data->num_res_irq = rockchip_get_sysmmu_resource_num(pdev,IORESOURCE_IRQ);
	if(0 == data->num_res_irq)
	{
		pr_err("can't find sysmmu irq resource \r\n");
		goto err_init;
	}
	
	data->res_bases = kmalloc(sizeof(*data->res_bases) * data->num_res_mem,GFP_KERNEL);
	if (data->res_bases == NULL)
	{
		dev_dbg(dev, "Not enough memory\n");
		ret = -ENOMEM;
		goto err_init;
	}

	for (i = 0; i < data->num_res_mem; i++) 
	{
		struct resource *res;
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) 
		{
			pr_err("Unable to find IOMEM region\n");
			ret = -ENOENT;
			goto err_res;
		}
		data->res_bases[i] = ioremap(res->start, resource_size(res));
		pr_info("res->start = 0x%08x  ioremap to  data->res_bases[%d] = 0x%08x\n",res->start,i,(unsigned int)data->res_bases[i]);
		if (!data->res_bases[i]) 
		{
			pr_err("Unable to map IOMEM @ PA:%#x\n",res->start);
			ret = -ENOENT;
			goto err_res;
		}
		
		rockchip_vcodec_select(data->dbgname);
		
		if(!strstr(data->dbgname,"isp"))
		{
			/*reset sysmmu*/
			if(!sysmmu_reset(data->res_bases[i],data->dbgname))
			{
				ret = -ENOENT;
				goto err_res;
			}
		}
	}

	for (i = 0; i < data->num_res_irq; i++) 
	{
		ret = platform_get_irq(pdev, i);
		if (ret <= 0) 
		{
			pr_err("Unable to find IRQ resource\n");
			goto err_irq;
		}
		ret = request_irq(ret, rockchip_sysmmu_irq, IRQF_SHARED ,dev_name(dev), data);
		if (ret) 
		{
			pr_err("Unabled to register interrupt handler\n");
			goto err_irq;
		}
	}
	ret = rockchip_init_iovmm(dev, &data->vmm);
	if (ret)
		goto err_irq;
	
	
	data->sysmmu = dev;
	rwlock_init(&data->lock);
	INIT_LIST_HEAD(&data->node);

	__set_fault_handler(data, &default_fault_handler);

	pr_info("(%s) Initialized\n", data->dbgname);
	return 0;

err_irq:
	while (i-- > 0) 
	{
		int irq;

		irq = platform_get_irq(pdev, i);
		free_irq(irq, data);
	}
err_res:
	while (data->num_res_mem-- > 0)
		iounmap(data->res_bases[data->num_res_mem]);
	kfree(data->res_bases);
err_init:
	kfree(data);
err_alloc:
	dev_err(dev, "Failed to initialize\n");
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id sysmmu_dt_ids[] = 
{
	{ .compatible = IEP_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = VIP_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = VOPB_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = VOPL_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = HEVC_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = VPU_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = ISP_SYSMMU_COMPATIBLE_NAME},
	{ .compatible = VOP_SYSMMU_COMPATIBLE_NAME},
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, sysmmu_dt_ids);
#endif

static struct platform_driver rk_sysmmu_driver = 
{
	.probe = rockchip_sysmmu_probe,
	.remove = NULL,
	.driver = 
	{
		   .name = "rk_sysmmu",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(sysmmu_dt_ids),
	},
};

static int __init rockchip_sysmmu_init_driver(void)
{
	dump_iommu_sysfs_init();

	return platform_driver_register(&rk_sysmmu_driver);
}

core_initcall(rockchip_sysmmu_init_driver);

