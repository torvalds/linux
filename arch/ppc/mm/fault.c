/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
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
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>

#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
extern void (*debugger)(struct pt_regs *);
extern void (*debugger_fault_handler)(struct pt_regs *);
extern int (*debugger_dabr_match)(struct pt_regs *);
int debugger_kernel_faults = 1;
#endif

unsigned long htab_reloads;	/* updated by hashtable.S:hash_page() */
unsigned long htab_evicts; 	/* updated by hashtable.S:hash_page() */
unsigned long htab_preloads;	/* updated by hashtable.S:add_hash_page() */
unsigned long pte_misses;	/* updated by do_page_fault() */
unsigned long pte_errors;	/* updated by do_page_fault() */
unsigned int probingmem;

/*
 * Check whether the instruction at regs->nip is a store using
 * an update addressing form which will update r1.
 */
static int store_updates_sp(struct pt_regs *regs)
{
	unsigned int inst;

	if (get_user(inst, (unsigned int __user *)regs->nip))
		return 0;
	/* check for 1 in the rA field */
	if (((inst >> 16) & 0x1f) != 1)
		return 0;
	/* check major opcode */
	switch (inst >> 26) {
	case 37:	/* stwu */
	case 39:	/* stbu */
	case 45:	/* sthu */
	case 53:	/* stfsu */
	case 55:	/* stfdu */
		return 1;
	case 31:
		/* check minor opcode */
		switch ((inst >> 1) & 0x3ff) {
		case 183:	/* stwux */
		case 247:	/* stbux */
		case 439:	/* sthux */
		case 695:	/* stfsux */
		case 759:	/* stfdux */
			return 1;
		}
	}
	return 0;
}

/*
 * For 600- and 800-family processors, the error_code parameter is DSISR
 * for a data fault, SRR1 for an instruction fault. For 400-family processors
 * the error_code parameter is ESR for a data fault, 0 for an instruction
 * fault.
 */
int do_page_fault(struct pt_regs *regs, unsigned long address,
		  unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	siginfo_t info;
	int code = SEGV_MAPERR;
	int fault;
#if defined(CONFIG_4xx) || defined (CONFIG_BOOKE)
	int is_write = error_code & ESR_DST;
#else
	int is_write = 0;

	/*
	 * Fortunately the bit assignments in SRR1 for an instruction
	 * fault and DSISR for a data fault are mostly the same for the
	 * bits we are interested in.  But there are some bits which
	 * indicate errors in DSISR but can validly be set in SRR1.
	 */
	if (TRAP(regs) == 0x400)
		error_code &= 0x48200000;
	else
		is_write = error_code & 0x02000000;
#endif /* CONFIG_4xx || CONFIG_BOOKE */

#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_fault_handler && TRAP(regs) == 0x300) {
		debugger_fault_handler(regs);
		return 0;
	}
#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
	if (error_code & 0x00400000) {
		/* DABR match */
		if (debugger_dabr_match(regs))
			return 0;
	}
#endif /* !(CONFIG_4xx || CONFIG_BOOKE)*/
#endif /* CONFIG_XMON || CONFIG_KGDB */

	if (in_atomic() || mm == NULL)
		return SIGSEGV;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (!is_write)
                goto bad_area;

	/*
	 * N.B. The rs6000/xcoff ABI allows programs to access up to
	 * a few hundred bytes below the stack pointer.
	 * The kernel signal delivery code writes up to about 1.5kB
	 * below the stack pointer (r1) before decrementing it.
	 * The exec code can write slightly over 640kB to the stack
	 * before setting the user r1.  Thus we allow the stack to
	 * expand to 1MB without further checks.
	 */
	if (address + 0x100000 < vma->vm_end) {
		/* get user regs even if this fault is in kernel mode */
		struct pt_regs *uregs = current->thread.regs;
		if (uregs == NULL)
			goto bad_area;

		/*
		 * A user-mode access to an address a long way below
		 * the stack pointer is only valid if the instruction
		 * is one which would update the stack pointer to the
		 * address accessed if the instruction completed,
		 * i.e. either stwu rs,n(r1) or stwux rs,r1,rb
		 * (or the byte, halfword, float or double forms).
		 *
		 * If we don't check this then any write to the area
		 * between the last mapped region and the stack will
		 * expand the stack rather than segfaulting.
		 */
		if (address + 2048 < uregs->gpr[1]
		    && (!user_mode(regs) || !store_updates_sp(regs)))
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	code = SEGV_ACCERR;
#if defined(CONFIG_6xx)
	if (error_code & 0x95700000)
		/* an error such as lwarx to I/O controller space,
		   address matching DABR, eciwx, etc. */
		goto bad_area;
#endif /* CONFIG_6xx */
#if defined(CONFIG_8xx)
        /* The MPC8xx seems to always set 0x80000000, which is
         * "undefined".  Of those that can be set, this is the only
         * one which seems bad.
         */
	if (error_code & 0x10000000)
                /* Guarded storage error. */
		goto bad_area;
#endif /* CONFIG_8xx */

	/* a write */
	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
	/* an exec  - 4xx/Book-E allows for per-page execute permission */
	} else if (TRAP(regs) == 0x400) {
		pte_t *ptep;
		pmd_t *pmdp;

#if 0
		/* It would be nice to actually enforce the VM execute
		   permission on CPUs which can do so, but far too
		   much stuff in userspace doesn't get the permissions
		   right, so we let any page be executed for now. */
		if (! (vma->vm_flags & VM_EXEC))
			goto bad_area;
#endif

		/* Since 4xx/Book-E supports per-page execute permission,
		 * we lazily flush dcache to icache. */
		ptep = NULL;
		if (get_pteptr(mm, address, &ptep, &pmdp)) {
			spinlock_t *ptl = pte_lockptr(mm, pmdp);
			spin_lock(ptl);
			if (pte_present(*ptep)) {
				struct page *page = pte_page(*ptep);

				if (!test_bit(PG_arch_1, &page->flags)) {
					flush_dcache_icache_page(page);
					set_bit(PG_arch_1, &page->flags);
				}
				pte_update(ptep, 0, _PAGE_HWEXEC);
				_tlbie(address);
				pte_unmap_unlock(ptep, ptl);
				up_read(&mm->mmap_sem);
				return 0;
			}
			pte_unmap_unlock(ptep, ptl);
		}
#endif
	/* a read */
	} else {
		/* protection fault */
		if (error_code & 0x08000000)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
 survive:
	fault = handle_mm_fault(mm, vma, address, is_write);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		current->maj_flt++;
	else
		current->min_flt++;

	up_read(&mm->mmap_sem);
	/*
	 * keep track of tlb+htab misses that are good addrs but
	 * just need pte's created via handle_mm_fault()
	 * -- Cort
	 */
	pte_misses++;
	return 0;

bad_area:
	up_read(&mm->mmap_sem);
	pte_errors++;

	/* User mode accesses cause a SIGSEGV */
	if (user_mode(regs)) {
		_exception(SIGSEGV, regs, code, address);
		return 0;
	}

	return SIGSEGV;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_global_init(current)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_group_exit(SIGKILL);
	return SIGKILL;

do_sigbus:
	up_read(&mm->mmap_sem);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *)address;
	force_sig_info (SIGBUS, &info, current);
	if (!user_mode(regs))
		return SIGBUS;
	return 0;
}

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from the DSI and ISI handlers in head.S and from some
 * of the procedures in traps.c.
 */
void
bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault?  */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		regs->nip = entry->fixup;
		return;
	}

	/* kernel has accessed a bad area */
#if defined(CONFIG_XMON) || defined(CONFIG_KGDB)
	if (debugger_kernel_faults)
		debugger(regs);
#endif
	die("kernel access of bad area", regs, sig);
}

#ifdef CONFIG_8xx

/* The pgtable.h claims some functions generically exist, but I
 * can't find them......
 */
pte_t *va_to_pte(unsigned long address)
{
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;

	if (address < TASK_SIZE)
		return NULL;

	dir = pgd_offset(&init_mm, address);
	if (dir) {
		pmd = pmd_offset(dir, address & PAGE_MASK);
		if (pmd && pmd_present(*pmd)) {
			pte = pte_offset_kernel(pmd, address & PAGE_MASK);
			if (pte && pte_present(*pte))
				return(pte);
		}
	}
	return NULL;
}

unsigned long va_to_phys(unsigned long address)
{
	pte_t *pte;

	pte = va_to_pte(address);
	if (pte)
		return(((unsigned long)(pte_val(*pte)) & PAGE_MASK) | (address & ~(PAGE_MASK)));
	return (0);
}

void
print_8xx_pte(struct mm_struct *mm, unsigned long addr)
{
        pgd_t * pgd;
        pmd_t * pmd;
        pte_t * pte;

        printk(" pte @ 0x%8lx: ", addr);
        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
                pmd = pmd_offset(pgd, addr & PAGE_MASK);
                if (pmd && pmd_present(*pmd)) {
                        pte = pte_offset_kernel(pmd, addr & PAGE_MASK);
                        if (pte) {
                                printk(" (0x%08lx)->(0x%08lx)->0x%08lx\n",
                                        (long)pgd, (long)pte, (long)pte_val(*pte));
#define pp ((long)pte_val(*pte))			
				printk(" RPN: %05lx PP: %lx SPS: %lx SH: %lx "
				       "CI: %lx v: %lx\n",
				       pp>>12,    /* rpn */
				       (pp>>10)&3, /* pp */
				       (pp>>3)&1, /* small */
				       (pp>>2)&1, /* shared */
				       (pp>>1)&1, /* cache inhibit */
				       pp&1       /* valid */
				       );
#undef pp			
                        }
                        else {
                                printk("no pte\n");
                        }
                }
                else {
                        printk("no pmd\n");
                }
        }
        else {
                printk("no pgd\n");
        }
}

int
get_8xx_pte(struct mm_struct *mm, unsigned long addr)
{
        pgd_t * pgd;
        pmd_t * pmd;
        pte_t * pte;
        int     retval = 0;

        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
                pmd = pmd_offset(pgd, addr & PAGE_MASK);
                if (pmd && pmd_present(*pmd)) {
                        pte = pte_offset_kernel(pmd, addr & PAGE_MASK);
                        if (pte) {
				retval = (int)pte_val(*pte);
                        }
                }
        }
        return(retval);
}
#endif /* CONFIG_8xx */
