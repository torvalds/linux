/* $Id: fault.c,v 1.122 2001/11/17 07:19:26 davem Exp $
 * fault.c:  Page fault handlers for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <asm/head.h>

#include <linux/string.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kdebug.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/memreg.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/smp.h>
#include <asm/traps.h>
#include <asm/uaccess.h>

extern int prom_node_root;

/* At boot time we determine these two values necessary for setting
 * up the segment maps and page table entries (pte's).
 */

int num_segmaps, num_contexts;
int invalid_segment;

/* various Virtual Address Cache parameters we find at boot time... */

int vac_size, vac_linesize, vac_do_hw_vac_flushes;
int vac_entries_per_context, vac_entries_per_segment;
int vac_entries_per_page;

/* Return how much physical memory we have.  */
unsigned long probe_memory(void)
{
	unsigned long total = 0;
	int i;

	for (i = 0; sp_banks[i].num_bytes; i++)
		total += sp_banks[i].num_bytes;

	return total;
}

extern void sun4c_complete_all_stores(void);

/* Whee, a level 15 NMI interrupt memory error.  Let's have fun... */
asmlinkage void sparc_lvl15_nmi(struct pt_regs *regs, unsigned long serr,
				unsigned long svaddr, unsigned long aerr,
				unsigned long avaddr)
{
	sun4c_complete_all_stores();
	printk("FAULT: NMI received\n");
	printk("SREGS: Synchronous Error %08lx\n", serr);
	printk("       Synchronous Vaddr %08lx\n", svaddr);
	printk("      Asynchronous Error %08lx\n", aerr);
	printk("      Asynchronous Vaddr %08lx\n", avaddr);
	if (sun4c_memerr_reg)
		printk("     Memory Parity Error %08lx\n", *sun4c_memerr_reg);
	printk("REGISTER DUMP:\n");
	show_regs(regs);
	prom_halt();
}

static void unhandled_fault(unsigned long, struct task_struct *,
		struct pt_regs *) __attribute__ ((noreturn));

static void unhandled_fault(unsigned long address, struct task_struct *tsk,
                     struct pt_regs *regs)
{
	if((unsigned long) address < PAGE_SIZE) {
		printk(KERN_ALERT
		    "Unable to handle kernel NULL pointer dereference\n");
	} else {
		printk(KERN_ALERT "Unable to handle kernel paging request "
		       "at virtual address %08lx\n", address);
	}
	printk(KERN_ALERT "tsk->{mm,active_mm}->context = %08lx\n",
		(tsk->mm ? tsk->mm->context : tsk->active_mm->context));
	printk(KERN_ALERT "tsk->{mm,active_mm}->pgd = %08lx\n",
		(tsk->mm ? (unsigned long) tsk->mm->pgd :
		 	(unsigned long) tsk->active_mm->pgd));
	die_if_kernel("Oops", regs);
}

asmlinkage int lookup_fault(unsigned long pc, unsigned long ret_pc, 
			    unsigned long address)
{
	struct pt_regs regs;
	unsigned long g2;
	unsigned int insn;
	int i;
	
	i = search_extables_range(ret_pc, &g2);
	switch (i) {
	case 3:
		/* load & store will be handled by fixup */
		return 3;

	case 1:
		/* store will be handled by fixup, load will bump out */
		/* for _to_ macros */
		insn = *((unsigned int *) pc);
		if ((insn >> 21) & 1)
			return 1;
		break;

	case 2:
		/* load will be handled by fixup, store will bump out */
		/* for _from_ macros */
		insn = *((unsigned int *) pc);
		if (!((insn >> 21) & 1) || ((insn>>19)&0x3f) == 15)
			return 2; 
		break; 

	default:
		break;
	};

	memset(&regs, 0, sizeof (regs));
	regs.pc = pc;
	regs.npc = pc + 4;
	__asm__ __volatile__(
		"rd %%psr, %0\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n" : "=r" (regs.psr));
	unhandled_fault(address, current, &regs);

	/* Not reached */
	return 0;
}

extern unsigned long safe_compute_effective_address(struct pt_regs *,
						    unsigned int);

static unsigned long compute_si_addr(struct pt_regs *regs, int text_fault)
{
	unsigned int insn;

	if (text_fault)
		return regs->pc;

	if (regs->psr & PSR_PS) {
		insn = *(unsigned int *) regs->pc;
	} else {
		__get_user(insn, (unsigned int *) regs->pc);
	}

	return safe_compute_effective_address(regs, insn);
}

asmlinkage void do_sparc_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	unsigned int fixup;
	unsigned long g2;
	siginfo_t info;
	int from_user = !(regs->psr & PSR_PS);
	int fault;

	if(text_fault)
		address = regs->pc;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (!ARCH_SUN4C_SUN4 && address >= TASK_SIZE)
		goto vmalloc_fault;

	info.si_code = SEGV_MAPERR;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
        if (in_atomic() || !mm)
                goto no_context;

	down_read(&mm->mmap_sem);

	/*
	 * The kernel referencing a bad kernel pointer can lock up
	 * a sun4c machine completely, so we must attempt recovery.
	 */
	if(!from_user && address >= PAGE_OFFSET)
		goto bad_area;

	vma = find_vma(mm, address);
	if(!vma)
		goto bad_area;
	if(vma->vm_start <= address)
		goto good_area;
	if(!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if(expand_stack(vma, address))
		goto bad_area;
	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	info.si_code = SEGV_ACCERR;
	if(write) {
		if(!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		/* Allow reads even for write-only mappings */
		if(!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, write);
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
	return;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if(from_user) {
#if 0
		printk("Fault whee %s [%d]: segfaults at %08lx pc=%08lx\n",
		       tsk->comm, tsk->pid, address, regs->pc);
#endif
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code set above to make clear whether
		   this was a SEGV_MAPERR or SEGV_ACCERR fault.  */
		info.si_addr = (void __user *)compute_si_addr(regs, text_fault);
		info.si_trapno = 0;
		force_sig_info (SIGSEGV, &info, tsk);
		return;
	}

	/* Is this in ex_table? */
no_context:
	g2 = regs->u_regs[UREG_G2];
	if (!from_user && (fixup = search_extables_range(regs->pc, &g2))) {
		if (fixup > 10) { /* Values below are reserved for other things */
			extern const unsigned __memset_start[];
			extern const unsigned __memset_end[];
			extern const unsigned __csum_partial_copy_start[];
			extern const unsigned __csum_partial_copy_end[];

#ifdef DEBUG_EXCEPTIONS
			printk("Exception: PC<%08lx> faddr<%08lx>\n", regs->pc, address);
			printk("EX_TABLE: insn<%08lx> fixup<%08x> g2<%08lx>\n",
				regs->pc, fixup, g2);
#endif
			if ((regs->pc >= (unsigned long)__memset_start &&
			     regs->pc < (unsigned long)__memset_end) ||
			    (regs->pc >= (unsigned long)__csum_partial_copy_start &&
			     regs->pc < (unsigned long)__csum_partial_copy_end)) {
			        regs->u_regs[UREG_I4] = address;
				regs->u_regs[UREG_I5] = regs->pc;
			}
			regs->u_regs[UREG_G2] = g2;
			regs->pc = fixup;
			regs->npc = regs->pc + 4;
			return;
		}
	}
	
	unhandled_fault (address, tsk, regs);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	printk("VM: killing process %s\n", tsk->comm);
	if (from_user)
		do_group_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *) compute_si_addr(regs, text_fault);
	info.si_trapno = 0;
	force_sig_info (SIGBUS, &info, tsk);
	if (!from_user)
		goto no_context;

vmalloc_fault:
	{
		/*
		 * Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 */
		int offset = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		pmd_t *pmd, *pmd_k;

		pgd = tsk->active_mm->pgd + offset;
		pgd_k = init_mm.pgd + offset;

		if (!pgd_present(*pgd)) {
			if (!pgd_present(*pgd_k))
				goto bad_area_nosemaphore;
			pgd_val(*pgd) = pgd_val(*pgd_k);
			return;
		}

		pmd = pmd_offset(pgd, address);
		pmd_k = pmd_offset(pgd_k, address);

		if (pmd_present(*pmd) || !pmd_present(*pmd_k))
			goto bad_area_nosemaphore;
		*pmd = *pmd_k;
		return;
	}
}

asmlinkage void do_sun4c_fault(struct pt_regs *regs, int text_fault, int write,
			       unsigned long address)
{
	extern void sun4c_update_mmu_cache(struct vm_area_struct *,
					   unsigned long,pte_t);
	extern pte_t *sun4c_pte_offset_kernel(pmd_t *,unsigned long);
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	pgd_t *pgdp;
	pte_t *ptep;

	if (text_fault) {
		address = regs->pc;
	} else if (!write &&
		   !(regs->psr & PSR_PS)) {
		unsigned int insn, __user *ip;

		ip = (unsigned int __user *)regs->pc;
		if (!get_user(insn, ip)) {
			if ((insn & 0xc1680000) == 0xc0680000)
				write = 1;
		}
	}

	if (!mm) {
		/* We are oopsing. */
		do_sparc_fault(regs, text_fault, write, address);
		BUG();	/* P3 Oops already, you bitch */
	}

	pgdp = pgd_offset(mm, address);
	ptep = sun4c_pte_offset_kernel((pmd_t *) pgdp, address);

	if (pgd_val(*pgdp)) {
	    if (write) {
		if ((pte_val(*ptep) & (_SUN4C_PAGE_WRITE|_SUN4C_PAGE_PRESENT))
				   == (_SUN4C_PAGE_WRITE|_SUN4C_PAGE_PRESENT)) {
			unsigned long flags;

			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_ACCESSED |
				      _SUN4C_PAGE_MODIFIED |
				      _SUN4C_PAGE_VALID |
				      _SUN4C_PAGE_DIRTY);

			local_irq_save(flags);
			if (sun4c_get_segmap(address) != invalid_segment) {
				sun4c_put_pte(address, pte_val(*ptep));
				local_irq_restore(flags);
				return;
			}
			local_irq_restore(flags);
		}
	    } else {
		if ((pte_val(*ptep) & (_SUN4C_PAGE_READ|_SUN4C_PAGE_PRESENT))
				   == (_SUN4C_PAGE_READ|_SUN4C_PAGE_PRESENT)) {
			unsigned long flags;

			*ptep = __pte(pte_val(*ptep) | _SUN4C_PAGE_ACCESSED |
				      _SUN4C_PAGE_VALID);

			local_irq_save(flags);
			if (sun4c_get_segmap(address) != invalid_segment) {
				sun4c_put_pte(address, pte_val(*ptep));
				local_irq_restore(flags);
				return;
			}
			local_irq_restore(flags);
		}
	    }
	}

	/* This conditional is 'interesting'. */
	if (pgd_val(*pgdp) && !(write && !(pte_val(*ptep) & _SUN4C_PAGE_WRITE))
	    && (pte_val(*ptep) & _SUN4C_PAGE_VALID))
		/* Note: It is safe to not grab the MMAP semaphore here because
		 *       we know that update_mmu_cache() will not sleep for
		 *       any reason (at least not in the current implementation)
		 *       and therefore there is no danger of another thread getting
		 *       on the CPU and doing a shrink_mmap() on this vma.
		 */
		sun4c_update_mmu_cache (find_vma(current->mm, address), address,
					*ptep);
	else
		do_sparc_fault(regs, text_fault, write, address);
}

/* This always deals with user addresses. */
inline void force_user_fault(unsigned long address, int write)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	siginfo_t info;

	info.si_code = SEGV_MAPERR;

#if 0
	printk("wf<pid=%d,wr=%d,addr=%08lx>\n",
	       tsk->pid, write, address);
#endif
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if(!vma)
		goto bad_area;
	if(vma->vm_start <= address)
		goto good_area;
	if(!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if(expand_stack(vma, address))
		goto bad_area;
good_area:
	info.si_code = SEGV_ACCERR;
	if(write) {
		if(!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if(!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}
	switch (handle_mm_fault(mm, vma, address, write)) {
	case VM_FAULT_SIGBUS:
	case VM_FAULT_OOM:
		goto do_sigbus;
	}
	up_read(&mm->mmap_sem);
	return;
bad_area:
	up_read(&mm->mmap_sem);
#if 0
	printk("Window whee %s [%d]: segfaults at %08lx\n",
	       tsk->comm, tsk->pid, address);
#endif
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	/* info.si_code set above to make clear whether
	   this was a SEGV_MAPERR or SEGV_ACCERR fault.  */
	info.si_addr = (void __user *) address;
	info.si_trapno = 0;
	force_sig_info (SIGSEGV, &info, tsk);
	return;

do_sigbus:
	up_read(&mm->mmap_sem);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *) address;
	info.si_trapno = 0;
	force_sig_info (SIGBUS, &info, tsk);
}

void window_overflow_fault(void)
{
	unsigned long sp;

	sp = current_thread_info()->rwbuf_stkptrs[0];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 1);
	force_user_fault(sp, 1);
}

void window_underflow_fault(unsigned long sp)
{
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
}

void window_ret_fault(struct pt_regs *regs)
{
	unsigned long sp;

	sp = regs->u_regs[UREG_FP];
	if(((sp + 0x38) & PAGE_MASK) != (sp & PAGE_MASK))
		force_user_fault(sp + 0x38, 0);
	force_user_fault(sp, 0);
}
