/*
 * Based on arch/arm/mm/fault.c
 *
 * Copyright (C) 1995  Linus Torvalds
 * Copyright (C) 1995-2004 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/extable.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/page-flags.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/highmem.h>
#include <linux/perf_event.h>
#include <linux/preempt.h>
#include <linux/hugetlb.h>

#include <asm/bug.h>
#include <asm/cmpxchg.h>
#include <asm/cpufeature.h>
#include <asm/exception.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/sysreg.h>
#include <asm/system_misc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>

#include <acpi/ghes.h>

struct fault_info {
	int	(*fn)(unsigned long addr, unsigned int esr,
		      struct pt_regs *regs);
	int	sig;
	int	code;
	const char *name;
};

static const struct fault_info fault_info[];

static inline const struct fault_info *esr_to_fault_info(unsigned int esr)
{
	return fault_info + (esr & 63);
}

#ifdef CONFIG_KPROBES
static inline int notify_page_fault(struct pt_regs *regs, unsigned int esr)
{
	int ret = 0;

	/* kprobe_running() needs smp_processor_id() */
	if (!user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, esr))
			ret = 1;
		preempt_enable();
	}

	return ret;
}
#else
static inline int notify_page_fault(struct pt_regs *regs, unsigned int esr)
{
	return 0;
}
#endif

static void data_abort_decode(unsigned int esr)
{
	pr_alert("Data abort info:\n");

	if (esr & ESR_ELx_ISV) {
		pr_alert("  Access size = %u byte(s)\n",
			 1U << ((esr & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT));
		pr_alert("  SSE = %lu, SRT = %lu\n",
			 (esr & ESR_ELx_SSE) >> ESR_ELx_SSE_SHIFT,
			 (esr & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT);
		pr_alert("  SF = %lu, AR = %lu\n",
			 (esr & ESR_ELx_SF) >> ESR_ELx_SF_SHIFT,
			 (esr & ESR_ELx_AR) >> ESR_ELx_AR_SHIFT);
	} else {
		pr_alert("  ISV = 0, ISS = 0x%08lx\n", esr & ESR_ELx_ISS_MASK);
	}

	pr_alert("  CM = %lu, WnR = %lu\n",
		 (esr & ESR_ELx_CM) >> ESR_ELx_CM_SHIFT,
		 (esr & ESR_ELx_WNR) >> ESR_ELx_WNR_SHIFT);
}

static void mem_abort_decode(unsigned int esr)
{
	pr_alert("Mem abort info:\n");

	pr_alert("  ESR = 0x%08x\n", esr);
	pr_alert("  Exception class = %s, IL = %u bits\n",
		 esr_get_class_string(esr),
		 (esr & ESR_ELx_IL) ? 32 : 16);
	pr_alert("  SET = %lu, FnV = %lu\n",
		 (esr & ESR_ELx_SET_MASK) >> ESR_ELx_SET_SHIFT,
		 (esr & ESR_ELx_FnV) >> ESR_ELx_FnV_SHIFT);
	pr_alert("  EA = %lu, S1PTW = %lu\n",
		 (esr & ESR_ELx_EA) >> ESR_ELx_EA_SHIFT,
		 (esr & ESR_ELx_S1PTW) >> ESR_ELx_S1PTW_SHIFT);

	if (esr_is_data_abort(esr))
		data_abort_decode(esr);
}

/*
 * Dump out the page tables associated with 'addr' in the currently active mm.
 */
void show_pte(unsigned long addr)
{
	struct mm_struct *mm;
	pgd_t *pgdp;
	pgd_t pgd;

	if (addr < TASK_SIZE) {
		/* TTBR0 */
		mm = current->active_mm;
		if (mm == &init_mm) {
			pr_alert("[%016lx] user address but active_mm is swapper\n",
				 addr);
			return;
		}
	} else if (addr >= VA_START) {
		/* TTBR1 */
		mm = &init_mm;
	} else {
		pr_alert("[%016lx] address between user and kernel address ranges\n",
			 addr);
		return;
	}

	pr_alert("%s pgtable: %luk pages, %u-bit VAs, pgdp = %p\n",
		 mm == &init_mm ? "swapper" : "user", PAGE_SIZE / SZ_1K,
		 VA_BITS, mm->pgd);
	pgdp = pgd_offset(mm, addr);
	pgd = READ_ONCE(*pgdp);
	pr_alert("[%016lx] pgd=%016llx", addr, pgd_val(pgd));

	do {
		pud_t *pudp, pud;
		pmd_t *pmdp, pmd;
		pte_t *ptep, pte;

		if (pgd_none(pgd) || pgd_bad(pgd))
			break;

		pudp = pud_offset(pgdp, addr);
		pud = READ_ONCE(*pudp);
		pr_cont(", pud=%016llx", pud_val(pud));
		if (pud_none(pud) || pud_bad(pud))
			break;

		pmdp = pmd_offset(pudp, addr);
		pmd = READ_ONCE(*pmdp);
		pr_cont(", pmd=%016llx", pmd_val(pmd));
		if (pmd_none(pmd) || pmd_bad(pmd))
			break;

		ptep = pte_offset_map(pmdp, addr);
		pte = READ_ONCE(*ptep);
		pr_cont(", pte=%016llx", pte_val(pte));
		pte_unmap(ptep);
	} while(0);

	pr_cont("\n");
}

/*
 * This function sets the access flags (dirty, accessed), as well as write
 * permission, and only to a more permissive setting.
 *
 * It needs to cope with hardware update of the accessed/dirty state by other
 * agents in the system and can safely skip the __sync_icache_dcache() call as,
 * like set_pte_at(), the PTE is never changed from no-exec to exec here.
 *
 * Returns whether or not the PTE actually changed.
 */
int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	pteval_t old_pteval, pteval;
	pte_t pte = READ_ONCE(*ptep);

	if (pte_same(pte, entry))
		return 0;

	/* only preserve the access flags and write permission */
	pte_val(entry) &= PTE_RDONLY | PTE_AF | PTE_WRITE | PTE_DIRTY;

	/*
	 * Setting the flags must be done atomically to avoid racing with the
	 * hardware update of the access/dirty state. The PTE_RDONLY bit must
	 * be set to the most permissive (lowest value) of *ptep and entry
	 * (calculated as: a & b == ~(~a | ~b)).
	 */
	pte_val(entry) ^= PTE_RDONLY;
	pteval = pte_val(pte);
	do {
		old_pteval = pteval;
		pteval ^= PTE_RDONLY;
		pteval |= pte_val(entry);
		pteval ^= PTE_RDONLY;
		pteval = cmpxchg_relaxed(&pte_val(*ptep), old_pteval, pteval);
	} while (pteval != old_pteval);

	flush_tlb_fix_spurious_fault(vma, address);
	return 1;
}

static bool is_el1_instruction_abort(unsigned int esr)
{
	return ESR_ELx_EC(esr) == ESR_ELx_EC_IABT_CUR;
}

static inline bool is_permission_fault(unsigned int esr, struct pt_regs *regs,
				       unsigned long addr)
{
	unsigned int ec       = ESR_ELx_EC(esr);
	unsigned int fsc_type = esr & ESR_ELx_FSC_TYPE;

	if (ec != ESR_ELx_EC_DABT_CUR && ec != ESR_ELx_EC_IABT_CUR)
		return false;

	if (fsc_type == ESR_ELx_FSC_PERM)
		return true;

	if (addr < TASK_SIZE && system_uses_ttbr0_pan())
		return fsc_type == ESR_ELx_FSC_FAULT &&
			(regs->pstate & PSR_PAN_BIT);

	return false;
}

static void __do_kernel_fault(unsigned long addr, unsigned int esr,
			      struct pt_regs *regs)
{
	const char *msg;

	/*
	 * Are we prepared to handle this kernel fault?
	 * We are almost certainly not prepared to handle instruction faults.
	 */
	if (!is_el1_instruction_abort(esr) && fixup_exception(regs))
		return;

	bust_spinlocks(1);

	if (is_permission_fault(esr, regs, addr)) {
		if (esr & ESR_ELx_WNR)
			msg = "write to read-only memory";
		else
			msg = "read from unreadable memory";
	} else if (addr < PAGE_SIZE) {
		msg = "NULL pointer dereference";
	} else {
		msg = "paging request";
	}

	pr_alert("Unable to handle kernel %s at virtual address %08lx\n", msg,
		 addr);

	mem_abort_decode(esr);

	show_pte(addr);
	die("Oops", regs, esr);
	bust_spinlocks(0);
	do_exit(SIGKILL);
}

static void __do_user_fault(struct siginfo *info, unsigned int esr)
{
	current->thread.fault_address = (unsigned long)info->si_addr;

	/*
	 * If the faulting address is in the kernel, we must sanitize the ESR.
	 * From userspace's point of view, kernel-only mappings don't exist
	 * at all, so we report them as level 0 translation faults.
	 * (This is not quite the way that "no mapping there at all" behaves:
	 * an alignment fault not caused by the memory type would take
	 * precedence over translation fault for a real access to empty
	 * space. Unfortunately we can't easily distinguish "alignment fault
	 * not caused by memory type" from "alignment fault caused by memory
	 * type", so we ignore this wrinkle and just return the translation
	 * fault.)
	 */
	if (current->thread.fault_address >= TASK_SIZE) {
		switch (ESR_ELx_EC(esr)) {
		case ESR_ELx_EC_DABT_LOW:
			/*
			 * These bits provide only information about the
			 * faulting instruction, which userspace knows already.
			 * We explicitly clear bits which are architecturally
			 * RES0 in case they are given meanings in future.
			 * We always report the ESR as if the fault was taken
			 * to EL1 and so ISV and the bits in ISS[23:14] are
			 * clear. (In fact it always will be a fault to EL1.)
			 */
			esr &= ESR_ELx_EC_MASK | ESR_ELx_IL |
				ESR_ELx_CM | ESR_ELx_WNR;
			esr |= ESR_ELx_FSC_FAULT;
			break;
		case ESR_ELx_EC_IABT_LOW:
			/*
			 * Claim a level 0 translation fault.
			 * All other bits are architecturally RES0 for faults
			 * reported with that DFSC value, so we clear them.
			 */
			esr &= ESR_ELx_EC_MASK | ESR_ELx_IL;
			esr |= ESR_ELx_FSC_FAULT;
			break;
		default:
			/*
			 * This should never happen (entry.S only brings us
			 * into this code for insn and data aborts from a lower
			 * exception level). Fail safe by not providing an ESR
			 * context record at all.
			 */
			WARN(1, "ESR 0x%x is not DABT or IABT from EL0\n", esr);
			esr = 0;
			break;
		}
	}

	current->thread.fault_code = esr;
	arm64_force_sig_info(info, esr_to_fault_info(esr)->name, current);
}

static void do_bad_area(unsigned long addr, unsigned int esr, struct pt_regs *regs)
{
	/*
	 * If we are in kernel mode at this point, we have no context to
	 * handle this fault with.
	 */
	if (user_mode(regs)) {
		const struct fault_info *inf = esr_to_fault_info(esr);
		struct siginfo si = {
			.si_signo	= inf->sig,
			.si_code	= inf->code,
			.si_addr	= (void __user *)addr,
		};

		__do_user_fault(&si, esr);
	} else {
		__do_kernel_fault(addr, esr, regs);
	}
}

#define VM_FAULT_BADMAP		0x010000
#define VM_FAULT_BADACCESS	0x020000

static int __do_page_fault(struct mm_struct *mm, unsigned long addr,
			   unsigned int mm_flags, unsigned long vm_flags,
			   struct task_struct *tsk)
{
	struct vm_area_struct *vma;
	int fault;

	vma = find_vma(mm, addr);
	fault = VM_FAULT_BADMAP;
	if (unlikely(!vma))
		goto out;
	if (unlikely(vma->vm_start > addr))
		goto check_stack;

	/*
	 * Ok, we have a good vm_area for this memory access, so we can handle
	 * it.
	 */
good_area:
	/*
	 * Check that the permissions on the VMA allow for the fault which
	 * occurred.
	 */
	if (!(vma->vm_flags & vm_flags)) {
		fault = VM_FAULT_BADACCESS;
		goto out;
	}

	return handle_mm_fault(vma, addr & PAGE_MASK, mm_flags);

check_stack:
	if (vma->vm_flags & VM_GROWSDOWN && !expand_stack(vma, addr))
		goto good_area;
out:
	return fault;
}

static bool is_el0_instruction_abort(unsigned int esr)
{
	return ESR_ELx_EC(esr) == ESR_ELx_EC_IABT_LOW;
}

static int __kprobes do_page_fault(unsigned long addr, unsigned int esr,
				   struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct siginfo si;
	int fault, major = 0;
	unsigned long vm_flags = VM_READ | VM_WRITE;
	unsigned int mm_flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	if (notify_page_fault(regs, esr))
		return 0;

	tsk = current;
	mm  = tsk->mm;

	/*
	 * If we're in an interrupt or have no user context, we must not take
	 * the fault.
	 */
	if (faulthandler_disabled() || !mm)
		goto no_context;

	if (user_mode(regs))
		mm_flags |= FAULT_FLAG_USER;

	if (is_el0_instruction_abort(esr)) {
		vm_flags = VM_EXEC;
	} else if ((esr & ESR_ELx_WNR) && !(esr & ESR_ELx_CM)) {
		vm_flags = VM_WRITE;
		mm_flags |= FAULT_FLAG_WRITE;
	}

	if (addr < TASK_SIZE && is_permission_fault(esr, regs, addr)) {
		/* regs->orig_addr_limit may be 0 if we entered from EL0 */
		if (regs->orig_addr_limit == KERNEL_DS)
			die("Accessing user space memory with fs=KERNEL_DS", regs, esr);

		if (is_el1_instruction_abort(esr))
			die("Attempting to execute userspace memory", regs, esr);

		if (!search_exception_tables(regs->pc))
			die("Accessing user space memory outside uaccess.h routines", regs, esr);
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, addr);

	/*
	 * As per x86, we may deadlock here. However, since the kernel only
	 * validly references user space from well defined areas of the code,
	 * we can bug out early if this is from code which shouldn't.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if (!user_mode(regs) && !search_exception_tables(regs->pc))
			goto no_context;
retry:
		down_read(&mm->mmap_sem);
	} else {
		/*
		 * The above down_read_trylock() might have succeeded in which
		 * case, we'll have missed the might_sleep() from down_read().
		 */
		might_sleep();
#ifdef CONFIG_DEBUG_VM
		if (!user_mode(regs) && !search_exception_tables(regs->pc))
			goto no_context;
#endif
	}

	fault = __do_page_fault(mm, addr, mm_flags, vm_flags, tsk);
	major |= fault & VM_FAULT_MAJOR;

	if (fault & VM_FAULT_RETRY) {
		/*
		 * If we need to retry but a fatal signal is pending,
		 * handle the signal first. We do not need to release
		 * the mmap_sem because it would already be released
		 * in __lock_page_or_retry in mm/filemap.c.
		 */
		if (fatal_signal_pending(current)) {
			if (!user_mode(regs))
				goto no_context;
			return 0;
		}

		/*
		 * Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk of
		 * starvation.
		 */
		if (mm_flags & FAULT_FLAG_ALLOW_RETRY) {
			mm_flags &= ~FAULT_FLAG_ALLOW_RETRY;
			mm_flags |= FAULT_FLAG_TRIED;
			goto retry;
		}
	}
	up_read(&mm->mmap_sem);

	/*
	 * Handle the "normal" (no error) case first.
	 */
	if (likely(!(fault & (VM_FAULT_ERROR | VM_FAULT_BADMAP |
			      VM_FAULT_BADACCESS)))) {
		/*
		 * Major/minor page fault accounting is only done
		 * once. If we go through a retry, it is extremely
		 * likely that the page will be found in page cache at
		 * that point.
		 */
		if (major) {
			tsk->maj_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, regs,
				      addr);
		} else {
			tsk->min_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, regs,
				      addr);
		}

		return 0;
	}

	/*
	 * If we are in kernel mode at this point, we have no context to
	 * handle this fault with.
	 */
	if (!user_mode(regs))
		goto no_context;

	if (fault & VM_FAULT_OOM) {
		/*
		 * We ran out of memory, call the OOM killer, and return to
		 * userspace (which will retry the fault, or kill us if we got
		 * oom-killed).
		 */
		pagefault_out_of_memory();
		return 0;
	}

	clear_siginfo(&si);
	si.si_addr = (void __user *)addr;

	if (fault & VM_FAULT_SIGBUS) {
		/*
		 * We had some memory, but were unable to successfully fix up
		 * this page fault.
		 */
		si.si_signo	= SIGBUS;
		si.si_code	= BUS_ADRERR;
	} else if (fault & VM_FAULT_HWPOISON_LARGE) {
		unsigned int hindex = VM_FAULT_GET_HINDEX(fault);

		si.si_signo	= SIGBUS;
		si.si_code	= BUS_MCEERR_AR;
		si.si_addr_lsb	= hstate_index_to_shift(hindex);
	} else if (fault & VM_FAULT_HWPOISON) {
		si.si_signo	= SIGBUS;
		si.si_code	= BUS_MCEERR_AR;
		si.si_addr_lsb	= PAGE_SHIFT;
	} else {
		/*
		 * Something tried to access memory that isn't in our memory
		 * map.
		 */
		si.si_signo	= SIGSEGV;
		si.si_code	= fault == VM_FAULT_BADACCESS ?
				  SEGV_ACCERR : SEGV_MAPERR;
	}

	__do_user_fault(&si, esr);
	return 0;

no_context:
	__do_kernel_fault(addr, esr, regs);
	return 0;
}

static int __kprobes do_translation_fault(unsigned long addr,
					  unsigned int esr,
					  struct pt_regs *regs)
{
	if (addr < TASK_SIZE)
		return do_page_fault(addr, esr, regs);

	do_bad_area(addr, esr, regs);
	return 0;
}

static int do_alignment_fault(unsigned long addr, unsigned int esr,
			      struct pt_regs *regs)
{
	do_bad_area(addr, esr, regs);
	return 0;
}

static int do_bad(unsigned long addr, unsigned int esr, struct pt_regs *regs)
{
	return 1; /* "fault" */
}

static int do_sea(unsigned long addr, unsigned int esr, struct pt_regs *regs)
{
	struct siginfo info;
	const struct fault_info *inf;

	inf = esr_to_fault_info(esr);

	/*
	 * Synchronous aborts may interrupt code which had interrupts masked.
	 * Before calling out into the wider kernel tell the interested
	 * subsystems.
	 */
	if (IS_ENABLED(CONFIG_ACPI_APEI_SEA)) {
		if (interrupts_enabled(regs))
			nmi_enter();

		ghes_notify_sea();

		if (interrupts_enabled(regs))
			nmi_exit();
	}

	info.si_signo = inf->sig;
	info.si_errno = 0;
	info.si_code  = inf->code;
	if (esr & ESR_ELx_FnV)
		info.si_addr = NULL;
	else
		info.si_addr  = (void __user *)addr;
	arm64_notify_die(inf->name, regs, &info, esr);

	return 0;
}

static const struct fault_info fault_info[] = {
	{ do_bad,		SIGKILL, SI_KERNEL,	"ttbr address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 1 address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 2 address size fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"level 3 address size fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 0 translation fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 1 translation fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 2 translation fault"	},
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"level 3 translation fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 8"			},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 1 access flag fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 2 access flag fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 3 access flag fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 12"			},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 1 permission fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 2 permission fault"	},
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"level 3 permission fault"	},
	{ do_sea,		SIGBUS,  BUS_OBJERR,	"synchronous external abort"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 17"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 18"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 19"			},
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 0 (translation table walk)"	},
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 1 (translation table walk)"	},
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 2 (translation table walk)"	},
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 3 (translation table walk)"	},
	{ do_sea,		SIGBUS,  BUS_OBJERR,	"synchronous parity or ECC error" },	// Reserved when RAS is implemented
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 25"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 26"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 27"			},
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 0 synchronous parity error (translation table walk)"	},	// Reserved when RAS is implemented
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 1 synchronous parity error (translation table walk)"	},	// Reserved when RAS is implemented
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 2 synchronous parity error (translation table walk)"	},	// Reserved when RAS is implemented
	{ do_sea,		SIGKILL, SI_KERNEL,	"level 3 synchronous parity error (translation table walk)"	},	// Reserved when RAS is implemented
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 32"			},
	{ do_alignment_fault,	SIGBUS,  BUS_ADRALN,	"alignment fault"		},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 34"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 35"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 36"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 37"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 38"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 39"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 40"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 41"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 42"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 43"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 44"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 45"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 46"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 47"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"TLB conflict abort"		},
	{ do_bad,		SIGKILL, SI_KERNEL,	"Unsupported atomic hardware update fault"	},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 50"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 51"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"implementation fault (lockdown abort)" },
	{ do_bad,		SIGBUS,  BUS_OBJERR,	"implementation fault (unsupported exclusive)" },
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 54"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 55"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 56"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 57"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 58" 			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 59"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 60"			},
	{ do_bad,		SIGKILL, SI_KERNEL,	"section domain fault"		},
	{ do_bad,		SIGKILL, SI_KERNEL,	"page domain fault"		},
	{ do_bad,		SIGKILL, SI_KERNEL,	"unknown 63"			},
};

int handle_guest_sea(phys_addr_t addr, unsigned int esr)
{
	int ret = -ENOENT;

	if (IS_ENABLED(CONFIG_ACPI_APEI_SEA))
		ret = ghes_notify_sea();

	return ret;
}

asmlinkage void __exception do_mem_abort(unsigned long addr, unsigned int esr,
					 struct pt_regs *regs)
{
	const struct fault_info *inf = esr_to_fault_info(esr);
	struct siginfo info;

	if (!inf->fn(addr, esr, regs))
		return;

	if (!user_mode(regs)) {
		pr_alert("Unhandled fault at 0x%016lx\n", addr);
		mem_abort_decode(esr);
		show_pte(addr);
	}

	info.si_signo = inf->sig;
	info.si_errno = 0;
	info.si_code  = inf->code;
	info.si_addr  = (void __user *)addr;
	arm64_notify_die(inf->name, regs, &info, esr);
}

asmlinkage void __exception do_el0_irq_bp_hardening(void)
{
	/* PC has already been checked in entry.S */
	arm64_apply_bp_hardening();
}

asmlinkage void __exception do_el0_ia_bp_hardening(unsigned long addr,
						   unsigned int esr,
						   struct pt_regs *regs)
{
	/*
	 * We've taken an instruction abort from userspace and not yet
	 * re-enabled IRQs. If the address is a kernel address, apply
	 * BP hardening prior to enabling IRQs and pre-emption.
	 */
	if (addr > TASK_SIZE)
		arm64_apply_bp_hardening();

	local_irq_enable();
	do_mem_abort(addr, esr, regs);
}


asmlinkage void __exception do_sp_pc_abort(unsigned long addr,
					   unsigned int esr,
					   struct pt_regs *regs)
{
	struct siginfo info;

	if (user_mode(regs)) {
		if (instruction_pointer(regs) > TASK_SIZE)
			arm64_apply_bp_hardening();
		local_irq_enable();
	}

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code  = BUS_ADRALN;
	info.si_addr  = (void __user *)addr;
	arm64_notify_die("SP/PC alignment exception", regs, &info, esr);
}

int __init early_brk64(unsigned long addr, unsigned int esr,
		       struct pt_regs *regs);

/*
 * __refdata because early_brk64 is __init, but the reference to it is
 * clobbered at arch_initcall time.
 * See traps.c and debug-monitors.c:debug_traps_init().
 */
static struct fault_info __refdata debug_fault_info[] = {
	{ do_bad,	SIGTRAP,	TRAP_HWBKPT,	"hardware breakpoint"	},
	{ do_bad,	SIGTRAP,	TRAP_HWBKPT,	"hardware single-step"	},
	{ do_bad,	SIGTRAP,	TRAP_HWBKPT,	"hardware watchpoint"	},
	{ do_bad,	SIGKILL,	SI_KERNEL,	"unknown 3"		},
	{ do_bad,	SIGTRAP,	TRAP_BRKPT,	"aarch32 BKPT"		},
	{ do_bad,	SIGKILL,	SI_KERNEL,	"aarch32 vector catch"	},
	{ early_brk64,	SIGTRAP,	TRAP_BRKPT,	"aarch64 BRK"		},
	{ do_bad,	SIGKILL,	SI_KERNEL,	"unknown 7"		},
};

void __init hook_debug_fault_code(int nr,
				  int (*fn)(unsigned long, unsigned int, struct pt_regs *),
				  int sig, int code, const char *name)
{
	BUG_ON(nr < 0 || nr >= ARRAY_SIZE(debug_fault_info));

	debug_fault_info[nr].fn		= fn;
	debug_fault_info[nr].sig	= sig;
	debug_fault_info[nr].code	= code;
	debug_fault_info[nr].name	= name;
}

asmlinkage int __exception do_debug_exception(unsigned long addr,
					      unsigned int esr,
					      struct pt_regs *regs)
{
	const struct fault_info *inf = debug_fault_info + DBG_ESR_EVT(esr);
	struct siginfo info;
	int rv;

	/*
	 * Tell lockdep we disabled irqs in entry.S. Do nothing if they were
	 * already disabled to preserve the last enabled/disabled addresses.
	 */
	if (interrupts_enabled(regs))
		trace_hardirqs_off();

	if (user_mode(regs) && instruction_pointer(regs) > TASK_SIZE)
		arm64_apply_bp_hardening();

	if (!inf->fn(addr, esr, regs)) {
		rv = 1;
	} else {
		info.si_signo = inf->sig;
		info.si_errno = 0;
		info.si_code  = inf->code;
		info.si_addr  = (void __user *)addr;
		arm64_notify_die(inf->name, regs, &info, esr);
		rv = 0;
	}

	if (interrupts_enabled(regs))
		trace_hardirqs_on();

	return rv;
}
NOKPROBE_SYMBOL(do_debug_exception);

#ifdef CONFIG_ARM64_PAN
void cpu_enable_pan(const struct arm64_cpu_capabilities *__unused)
{
	/*
	 * We modify PSTATE. This won't work from irq context as the PSTATE
	 * is discarded once we return from the exception.
	 */
	WARN_ON_ONCE(in_interrupt());

	config_sctlr_el1(SCTLR_EL1_SPAN, 0);
	asm(SET_PSTATE_PAN(1));
}
#endif /* CONFIG_ARM64_PAN */
