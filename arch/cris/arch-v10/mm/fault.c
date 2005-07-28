/*
 *  linux/arch/cris/mm/fault.c
 *
 *  Low level bus fault handler
 *
 *
 *  Copyright (C) 2000, 2001  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen 
 * 
 */

#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/arch/svinto.h>
#include <asm/mmu_context.h>

/* debug of low-level TLB reload */
#undef DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

extern const struct exception_table_entry
	*search_exception_tables(unsigned long addr);

asmlinkage void do_page_fault(unsigned long address, struct pt_regs *regs,
                              int protection, int writeaccess);

/* fast TLB-fill fault handler
 * this is called from entry.S with interrupts disabled
 */

void
handle_mmu_bus_fault(struct pt_regs *regs)
{
	int cause;
	int select;
#ifdef DEBUG
	int index;
	int page_id;
	int acc, inv;
#endif
	pgd_t* pgd = (pgd_t*)per_cpu(current_pgd, smp_processor_id());
	pmd_t *pmd;
	pte_t pte;
	int miss, we, writeac;
	unsigned long address;
	unsigned long flags;

	cause = *R_MMU_CAUSE;

	address = cause & PAGE_MASK; /* get faulting address */
	select = *R_TLB_SELECT;

#ifdef DEBUG
	page_id = IO_EXTRACT(R_MMU_CAUSE,  page_id,   cause);
	acc     = IO_EXTRACT(R_MMU_CAUSE,  acc_excp,  cause);
	inv     = IO_EXTRACT(R_MMU_CAUSE,  inv_excp,  cause);  
	index   = IO_EXTRACT(R_TLB_SELECT, index,     select);
#endif
	miss    = IO_EXTRACT(R_MMU_CAUSE,  miss_excp, cause);
	we      = IO_EXTRACT(R_MMU_CAUSE,  we_excp,   cause);
	writeac = IO_EXTRACT(R_MMU_CAUSE,  wr_rd,     cause);

	D(printk("bus_fault from IRP 0x%lx: addr 0x%lx, miss %d, inv %d, we %d, acc %d, dx %d pid %d\n",
		 regs->irp, address, miss, inv, we, acc, index, page_id));

	/* leave it to the MM system fault handler */
	if (miss)
		do_page_fault(address, regs, 0, writeac);
        else
		do_page_fault(address, regs, 1, we);

        /* Reload TLB with new entry to avoid an extra miss exception.
	 * do_page_fault may have flushed the TLB so we have to restore
	 * the MMU registers.
	 */
	local_save_flags(flags);
	local_irq_disable();
	pmd = (pmd_t *)(pgd + pgd_index(address));
	if (pmd_none(*pmd))
		return;
	pte = *pte_offset_kernel(pmd, address);
	if (!pte_present(pte))
		return;
	*R_TLB_SELECT = select;
	*R_TLB_HI = cause;
	*R_TLB_LO = pte_val(pte);
	local_irq_restore(flags);
}
