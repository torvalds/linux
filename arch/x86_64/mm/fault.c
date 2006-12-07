/*
 *  linux/arch/x86-64/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2001,2002 Andi Kleen, SuSE Labs.
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
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>

#include <asm/system.h>
#include <asm/pgalloc.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/kdebug.h>
#include <asm-generic/sections.h>

/* Page fault error code bits */
#define PF_PROT	(1<<0)		/* or no page found */
#define PF_WRITE	(1<<1)
#define PF_USER	(1<<2)
#define PF_RSVD	(1<<3)
#define PF_INSTR	(1<<4)

static ATOMIC_NOTIFIER_HEAD(notify_page_fault_chain);

/* Hook to register for page fault notifications */
int register_page_fault_notifier(struct notifier_block *nb)
{
	vmalloc_sync_all();
	return atomic_notifier_chain_register(&notify_page_fault_chain, nb);
}
EXPORT_SYMBOL_GPL(register_page_fault_notifier);

int unregister_page_fault_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&notify_page_fault_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_page_fault_notifier);

static inline int notify_page_fault(enum die_val val, const char *str,
			struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.str = str,
		.err = err,
		.trapnr = trap,
		.signr = sig
	};
	return atomic_notifier_call_chain(&notify_page_fault_chain, val, &args);
}

void bust_spinlocks(int yes)
{
	int loglevel_save = console_loglevel;
	if (yes) {
		oops_in_progress = 1;
	} else {
#ifdef CONFIG_VT
		unblank_screen();
#endif
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;		/* NMI oopser may have shut the console up */
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

/* Sometimes the CPU reports invalid exceptions on prefetch.
   Check that here and ignore.
   Opcode checker based on code by Richard Brunner */
static noinline int is_prefetch(struct pt_regs *regs, unsigned long addr,
				unsigned long error_code)
{ 
	unsigned char *instr;
	int scan_more = 1;
	int prefetch = 0; 
	unsigned char *max_instr;

	/* If it was a exec fault ignore */
	if (error_code & PF_INSTR)
		return 0;
	
	instr = (unsigned char __user *)convert_rip_to_linear(current, regs);
	max_instr = instr + 15;

	if (user_mode(regs) && instr >= (unsigned char *)TASK_SIZE)
		return 0;

	while (scan_more && instr < max_instr) { 
		unsigned char opcode;
		unsigned char instr_hi;
		unsigned char instr_lo;

		if (probe_kernel_address(instr, opcode))
			break; 

		instr_hi = opcode & 0xf0; 
		instr_lo = opcode & 0x0f; 
		instr++;

		switch (instr_hi) { 
		case 0x20:
		case 0x30:
			/* Values 0x26,0x2E,0x36,0x3E are valid x86
			   prefixes.  In long mode, the CPU will signal
			   invalid opcode if some of these prefixes are
			   present so we will never get here anyway */
			scan_more = ((instr_lo & 7) == 0x6);
			break;
			
		case 0x40:
			/* In AMD64 long mode, 0x40 to 0x4F are valid REX prefixes
			   Need to figure out under what instruction mode the
			   instruction was issued ... */
			/* Could check the LDT for lm, but for now it's good
			   enough to assume that long mode only uses well known
			   segments or kernel. */
			scan_more = (!user_mode(regs)) || (regs->cs == __USER_CS);
			break;
			
		case 0x60:
			/* 0x64 thru 0x67 are valid prefixes in all modes. */
			scan_more = (instr_lo & 0xC) == 0x4;
			break;		
		case 0xF0:
			/* 0xF0, 0xF2, and 0xF3 are valid prefixes in all modes. */
			scan_more = !instr_lo || (instr_lo>>1) == 1;
			break;			
		case 0x00:
			/* Prefetch instruction is 0x0F0D or 0x0F18 */
			scan_more = 0;
			if (probe_kernel_address(instr, opcode))
				break;
			prefetch = (instr_lo == 0xF) &&
				(opcode == 0x0D || opcode == 0x18);
			break;			
		default:
			scan_more = 0;
			break;
		} 
	}
	return prefetch;
}

static int bad_address(void *p) 
{ 
	unsigned long dummy;
	return probe_kernel_address((unsigned long *)p, dummy);
} 

void dump_pagetable(unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	asm("movq %%cr3,%0" : "=r" (pgd));

	pgd = __va((unsigned long)pgd & PHYSICAL_PAGE_MASK); 
	pgd += pgd_index(address);
	if (bad_address(pgd)) goto bad;
	printk("PGD %lx ", pgd_val(*pgd));
	if (!pgd_present(*pgd)) goto ret; 

	pud = pud_offset(pgd, address);
	if (bad_address(pud)) goto bad;
	printk("PUD %lx ", pud_val(*pud));
	if (!pud_present(*pud))	goto ret;

	pmd = pmd_offset(pud, address);
	if (bad_address(pmd)) goto bad;
	printk("PMD %lx ", pmd_val(*pmd));
	if (!pmd_present(*pmd))	goto ret;	 

	pte = pte_offset_kernel(pmd, address);
	if (bad_address(pte)) goto bad;
	printk("PTE %lx", pte_val(*pte)); 
ret:
	printk("\n");
	return;
bad:
	printk("BAD\n");
}

static const char errata93_warning[] = 
KERN_ERR "******* Your BIOS seems to not contain a fix for K8 errata #93\n"
KERN_ERR "******* Working around it, but it may cause SEGVs or burn power.\n"
KERN_ERR "******* Please consider a BIOS update.\n"
KERN_ERR "******* Disabling USB legacy in the BIOS may also help.\n";

/* Workaround for K8 erratum #93 & buggy BIOS.
   BIOS SMM functions are required to use a specific workaround
   to avoid corruption of the 64bit RIP register on C stepping K8. 
   A lot of BIOS that didn't get tested properly miss this. 
   The OS sees this as a page fault with the upper 32bits of RIP cleared.
   Try to work around it here.
   Note we only handle faults in kernel here. */

static int is_errata93(struct pt_regs *regs, unsigned long address) 
{
	static int warned;
	if (address != regs->rip)
		return 0;
	if ((address >> 32) != 0) 
		return 0;
	address |= 0xffffffffUL << 32;
	if ((address >= (u64)_stext && address <= (u64)_etext) || 
	    (address >= MODULES_VADDR && address <= MODULES_END)) { 
		if (!warned) {
			printk(errata93_warning); 		
			warned = 1;
		}
		regs->rip = address;
		return 1;
	}
	return 0;
} 

int unhandled_signal(struct task_struct *tsk, int sig)
{
	if (is_init(tsk))
		return 1;
	if (tsk->ptrace & PT_PTRACED)
		return 0;
	return (tsk->sighand->action[sig-1].sa.sa_handler == SIG_IGN) ||
		(tsk->sighand->action[sig-1].sa.sa_handler == SIG_DFL);
}

static noinline void pgtable_bad(unsigned long address, struct pt_regs *regs,
				 unsigned long error_code)
{
	unsigned long flags = oops_begin();
	struct task_struct *tsk;

	printk(KERN_ALERT "%s: Corrupted page table at address %lx\n",
	       current->comm, address);
	dump_pagetable(address);
	tsk = current;
	tsk->thread.cr2 = address;
	tsk->thread.trap_no = 14;
	tsk->thread.error_code = error_code;
	__die("Bad pagetable", regs, error_code);
	oops_end(flags);
	do_exit(SIGKILL);
}

/*
 * Handle a fault on the vmalloc area
 *
 * This assumes no large pages in there.
 */
static int vmalloc_fault(unsigned long address)
{
	pgd_t *pgd, *pgd_ref;
	pud_t *pud, *pud_ref;
	pmd_t *pmd, *pmd_ref;
	pte_t *pte, *pte_ref;

	/* Copy kernel mappings over when needed. This can also
	   happen within a race in page table update. In the later
	   case just flush. */

	pgd = pgd_offset(current->mm ?: &init_mm, address);
	pgd_ref = pgd_offset_k(address);
	if (pgd_none(*pgd_ref))
		return -1;
	if (pgd_none(*pgd))
		set_pgd(pgd, *pgd_ref);
	else
		BUG_ON(pgd_page_vaddr(*pgd) != pgd_page_vaddr(*pgd_ref));

	/* Below here mismatches are bugs because these lower tables
	   are shared */

	pud = pud_offset(pgd, address);
	pud_ref = pud_offset(pgd_ref, address);
	if (pud_none(*pud_ref))
		return -1;
	if (pud_none(*pud) || pud_page_vaddr(*pud) != pud_page_vaddr(*pud_ref))
		BUG();
	pmd = pmd_offset(pud, address);
	pmd_ref = pmd_offset(pud_ref, address);
	if (pmd_none(*pmd_ref))
		return -1;
	if (pmd_none(*pmd) || pmd_page(*pmd) != pmd_page(*pmd_ref))
		BUG();
	pte_ref = pte_offset_kernel(pmd_ref, address);
	if (!pte_present(*pte_ref))
		return -1;
	pte = pte_offset_kernel(pmd, address);
	/* Don't use pte_page here, because the mappings can point
	   outside mem_map, and the NUMA hash lookup cannot handle
	   that. */
	if (!pte_present(*pte) || pte_pfn(*pte) != pte_pfn(*pte_ref))
		BUG();
	return 0;
}

int page_fault_trace = 0;
int exception_trace = 1;

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void __kprobes do_page_fault(struct pt_regs *regs,
					unsigned long error_code)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long address;
	const struct exception_table_entry *fixup;
	int write;
	unsigned long flags;
	siginfo_t info;

	tsk = current;
	mm = tsk->mm;
	prefetchw(&mm->mmap_sem);

	/* get the address */
	__asm__("movq %%cr2,%0":"=r" (address));

	info.si_code = SEGV_MAPERR;


	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * This verifies that the fault happens in kernel space
	 * (error_code & 4) == 0, and that the fault was not a
	 * protection error (error_code & 9) == 0.
	 */
	if (unlikely(address >= TASK_SIZE64)) {
		/*
		 * Don't check for the module range here: its PML4
		 * is always initialized because it's shared with the main
		 * kernel text. Only vmalloc may need PML4 syncups.
		 */
		if (!(error_code & (PF_RSVD|PF_USER|PF_PROT)) &&
		      ((address >= VMALLOC_START && address < VMALLOC_END))) {
			if (vmalloc_fault(address) >= 0)
				return;
		}
		if (notify_page_fault(DIE_PAGE_FAULT, "page fault", regs, error_code, 14,
						SIGSEGV) == NOTIFY_STOP)
			return;
		/*
		 * Don't take the mm semaphore here. If we fixup a prefetch
		 * fault we could otherwise deadlock.
		 */
		goto bad_area_nosemaphore;
	}

	if (notify_page_fault(DIE_PAGE_FAULT, "page fault", regs, error_code, 14,
					SIGSEGV) == NOTIFY_STOP)
		return;

	if (likely(regs->eflags & X86_EFLAGS_IF))
		local_irq_enable();

	if (unlikely(page_fault_trace))
		printk("pagefault rip:%lx rsp:%lx cs:%lu ss:%lu address %lx error %lx\n",
		       regs->rip,regs->rsp,regs->cs,regs->ss,address,error_code); 

	if (unlikely(error_code & PF_RSVD))
		pgtable_bad(address, regs, error_code);

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (unlikely(in_atomic() || !mm))
		goto bad_area_nosemaphore;

 again:
	/* When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunatly, in the case of an
	 * erroneous fault occurring in a code path which already holds mmap_sem
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibilty of a deadlock.
	 * Attempt to lock the address space, if we cannot we then validate the
	 * source.  If this is invalid we can skip the address space check,
	 * thus avoiding the deadlock.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if ((error_code & PF_USER) == 0 &&
		    !search_exception_tables(regs->rip))
			goto bad_area_nosemaphore;
		down_read(&mm->mmap_sem);
	}

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (likely(vma->vm_start <= address))
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (error_code & 4) {
		/* Allow userspace just enough access below the stack pointer
		 * to let the 'enter' instruction work.
		 */
		if (address + 65536 + 32 * sizeof(unsigned long) < regs->rsp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	info.si_code = SEGV_ACCERR;
	write = 0;
	switch (error_code & (PF_PROT|PF_WRITE)) {
		default:	/* 3: write, present */
			/* fall through */
		case PF_WRITE:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case PF_PROT:		/* read, present */
			goto bad_area;
		case 0:			/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
				goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, write)) {
	case VM_FAULT_MINOR:
		tsk->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto do_sigbus;
	default:
		goto out_of_memory;
	}

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
	if (error_code & PF_USER) {
		if (is_prefetch(regs, address, error_code))
			return;

		/* Work around K8 erratum #100 K8 in compat mode
		   occasionally jumps to illegal addresses >4GB.  We
		   catch this here in the page fault handler because
		   these addresses are not reachable. Just detect this
		   case and return.  Any code segment in LDT is
		   compatibility mode. */
		if ((regs->cs == __USER32_CS || (regs->cs & (1<<2))) &&
		    (address >> 32))
			return;

		if (exception_trace && unhandled_signal(tsk, SIGSEGV)) {
			printk(
		       "%s%s[%d]: segfault at %016lx rip %016lx rsp %016lx error %lx\n",
					tsk->pid > 1 ? KERN_INFO : KERN_EMERG,
					tsk->comm, tsk->pid, address, regs->rip,
					regs->rsp, error_code);
		}
       
		tsk->thread.cr2 = address;
		/* Kernel addresses are always protection faults */
		tsk->thread.error_code = error_code | (address >= TASK_SIZE);
		tsk->thread.trap_no = 14;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void __user *)address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
	
	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->rip);
	if (fixup) {
		regs->rip = fixup->fixup;
		return;
	}

	/* 
	 * Hall of shame of CPU/BIOS bugs.
	 */

 	if (is_prefetch(regs, address, error_code))
 		return;

	if (is_errata93(regs, address))
		return; 

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

	flags = oops_begin();

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at %016lx RIP: \n" KERN_ALERT,address);
	printk_address(regs->rip);
	dump_pagetable(address);
	tsk->thread.cr2 = address;
	tsk->thread.trap_no = 14;
	tsk->thread.error_code = error_code;
	__die("Oops", regs, error_code);
	/* Executive summary in case the body of the oops scrolled away */
	printk(KERN_EMERG "CR2: %016lx\n", address);
	oops_end(flags);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_init(current)) {
		yield();
		goto again;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (error_code & 4)
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Kernel mode? Handle exceptions or die */
	if (!(error_code & PF_USER))
		goto no_context;

	tsk->thread.cr2 = address;
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 14;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *)address;
	force_sig_info(SIGBUS, &info, tsk);
	return;
}

DEFINE_SPINLOCK(pgd_lock);
struct page *pgd_list;

void vmalloc_sync_all(void)
{
	/* Note that races in the updates of insync and start aren't 
	   problematic:
	   insync can only get set bits added, and updates to start are only
	   improving performance (without affecting correctness if undone). */
	static DECLARE_BITMAP(insync, PTRS_PER_PGD);
	static unsigned long start = VMALLOC_START & PGDIR_MASK;
	unsigned long address;

	for (address = start; address <= VMALLOC_END; address += PGDIR_SIZE) {
		if (!test_bit(pgd_index(address), insync)) {
			const pgd_t *pgd_ref = pgd_offset_k(address);
			struct page *page;

			if (pgd_none(*pgd_ref))
				continue;
			spin_lock(&pgd_lock);
			for (page = pgd_list; page;
			     page = (struct page *)page->index) {
				pgd_t *pgd;
				pgd = (pgd_t *)page_address(page) + pgd_index(address);
				if (pgd_none(*pgd))
					set_pgd(pgd, *pgd_ref);
				else
					BUG_ON(pgd_page_vaddr(*pgd) != pgd_page_vaddr(*pgd_ref));
			}
			spin_unlock(&pgd_lock);
			set_bit(pgd_index(address), insync);
		}
		if (address == start)
			start = address + PGDIR_SIZE;
	}
	/* Check that there is no need to do the same for the modules area. */
	BUILD_BUG_ON(!(MODULES_VADDR > __START_KERNEL));
	BUILD_BUG_ON(!(((MODULES_END - 1) & PGDIR_MASK) == 
				(__START_KERNEL & PGDIR_MASK)));
}

static int __init enable_pagefaulttrace(char *str)
{
	page_fault_trace = 1;
	return 1;
}
__setup("pagefaulttrace", enable_pagefaulttrace);
