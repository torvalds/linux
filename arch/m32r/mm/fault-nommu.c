/*
 *  linux/arch/m32r/mm/fault.c
 *
 *  Copyright (c) 2001, 2002  Hitoshi Yamamoto, and H. Kondo
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/vt_kern.h>              /* For unblank_screen() */

#include <asm/m32r.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/mmu_context.h>

extern void die(const char *, struct pt_regs *, long);

#ifndef CONFIG_SMP
asmlinkage unsigned int tlb_entry_i_dat;
asmlinkage unsigned int tlb_entry_d_dat;
#define tlb_entry_i tlb_entry_i_dat
#define tlb_entry_d tlb_entry_d_dat
#else
unsigned int tlb_entry_i_dat[NR_CPUS];
unsigned int tlb_entry_d_dat[NR_CPUS];
#define tlb_entry_i tlb_entry_i_dat[smp_processor_id()]
#define tlb_entry_d tlb_entry_d_dat[smp_processor_id()]
#endif

void do_BUG(const char *file, int line)
{
	bust_spinlocks(1);
	printk("kernel BUG at %s:%d!\n", file, line);
}

/*======================================================================*
 * do_page_fault()
 *======================================================================*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * ARGUMENT:
 *  regs       : M32R SP reg.
 *  error_code : See below
 *  address    : M32R MMU MDEVA reg. (Operand ACE)
 *             : M32R BPC reg. (Instruction ACE)
 *
 * error_code :
 *  bit 0 == 0 means no page found, 1 means protection fault
 *  bit 1 == 0 means read, 1 means write
 *  bit 2 == 0 means kernel, 1 means user-mode
 *======================================================================*/
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code,
  unsigned long address)
{

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

	bust_spinlocks(1);

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	printk(" printing bpc:\n");
	printk(KERN_ALERT "bpc = %08lx\n", regs->bpc);

	die("Oops", regs, error_code);
	bust_spinlocks(0);
	do_exit(SIGKILL);
}

/*======================================================================*
 * update_mmu_cache()
 *======================================================================*/
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr,
	pte_t *ptep)
{
	BUG();
}

/*======================================================================*
 * flush_tlb_page() : flushes one page
 *======================================================================*/
void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	BUG();
}

/*======================================================================*
 * flush_tlb_range() : flushes a range of pages
 *======================================================================*/
void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	BUG();
}

/*======================================================================*
 * flush_tlb_mm() : flushes the specified mm context TLB's
 *======================================================================*/
void local_flush_tlb_mm(struct mm_struct *mm)
{
	BUG();
}

/*======================================================================*
 * flush_tlb_all() : flushes all processes TLBs
 *======================================================================*/
void local_flush_tlb_all(void)
{
	BUG();
}
