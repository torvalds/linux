/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 * Copyright 1999 SuSE GmbH (Philipp Rumpf, prumpf@tux.org)
 * Copyright 1999 Hewlett Packard Co.
 *
 */

#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/interrupt.h>
#include <linux/extable.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <linux/perf_event.h>

#include <asm/traps.h>

/* Various important other fields */
#define bit22set(x)		(x & 0x00000200)
#define bits23_25set(x)		(x & 0x000001c0)
#define isGraphicsFlushRead(x)	((x & 0xfc003fdf) == 0x04001a80)
				/* extended opcode is 0x6a */

#define BITSSET		0x1c0	/* for identifying LDCW */


int show_unhandled_signals = 1;

/*
 * parisc_acctyp(unsigned int inst) --
 *    Given a PA-RISC memory access instruction, determine if the
 *    the instruction would perform a memory read or memory write
 *    operation.
 *
 *    This function assumes that the given instruction is a memory access
 *    instruction (i.e. you should really only call it if you know that
 *    the instruction has generated some sort of a memory access fault).
 *
 * Returns:
 *   VM_READ  if read operation
 *   VM_WRITE if write operation
 *   VM_EXEC  if execute operation
 */
unsigned long
parisc_acctyp(unsigned long code, unsigned int inst)
{
	if (code == 6 || code == 16)
	    return VM_EXEC;

	switch (inst & 0xf0000000) {
	case 0x40000000: /* load */
	case 0x50000000: /* new load */
		return VM_READ;

	case 0x60000000: /* store */
	case 0x70000000: /* new store */
		return VM_WRITE;

	case 0x20000000: /* coproc */
	case 0x30000000: /* coproc2 */
		if (bit22set(inst))
			return VM_WRITE;
		fallthrough;

	case 0x0: /* indexed/memory management */
		if (bit22set(inst)) {
			/*
			 * Check for the 'Graphics Flush Read' instruction.
			 * It resembles an FDC instruction, except for bits
			 * 20 and 21. Any combination other than zero will
			 * utilize the block mover functionality on some
			 * older PA-RISC platforms.  The case where a block
			 * move is performed from VM to graphics IO space
			 * should be treated as a READ.
			 *
			 * The significance of bits 20,21 in the FDC
			 * instruction is:
			 *
			 *   00  Flush data cache (normal instruction behavior)
			 *   01  Graphics flush write  (IO space -> VM)
			 *   10  Graphics flush read   (VM -> IO space)
			 *   11  Graphics flush read/write (VM <-> IO space)
			 */
			if (isGraphicsFlushRead(inst))
				return VM_READ;
			return VM_WRITE;
		} else {
			/*
			 * Check for LDCWX and LDCWS (semaphore instructions).
			 * If bits 23 through 25 are all 1's it is one of
			 * the above two instructions and is a write.
			 *
			 * Note: With the limited bits we are looking at,
			 * this will also catch PROBEW and PROBEWI. However,
			 * these should never get in here because they don't
			 * generate exceptions of the type:
			 *   Data TLB miss fault/data page fault
			 *   Data memory protection trap
			 */
			if (bits23_25set(inst) == BITSSET)
				return VM_WRITE;
		}
		return VM_READ; /* Default */
	}
	return VM_READ; /* Default */
}

#undef bit22set
#undef bits23_25set
#undef isGraphicsFlushRead
#undef BITSSET


#if 0
/* This is the treewalk to find a vma which is the highest that has
 * a start < addr.  We're using find_vma_prev instead right now, but
 * we might want to use this at some point in the future.  Probably
 * not, but I want it committed to CVS so I don't lose it :-)
 */
			while (tree != vm_avl_empty) {
				if (tree->vm_start > addr) {
					tree = tree->vm_avl_left;
				} else {
					prev = tree;
					if (prev->vm_next == NULL)
						break;
					if (prev->vm_next->vm_start > addr)
						break;
					tree = tree->vm_avl_right;
				}
			}
#endif

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fix;

	fix = search_exception_tables(regs->iaoq[0]);
	if (fix) {
		/*
		 * Fix up get_user() and put_user().
		 * ASM_EXCEPTIONTABLE_ENTRY_EFAULT() sets the least-significant
		 * bit in the relative address of the fixup routine to indicate
		 * that gr[ASM_EXCEPTIONTABLE_REG] should be loaded with
		 * -EFAULT to report a userspace access error.
		 */
		if (fix->fixup & 1) {
			regs->gr[ASM_EXCEPTIONTABLE_REG] = -EFAULT;

			/* zero target register for get_user() */
			if (parisc_acctyp(0, regs->iir) == VM_READ) {
				int treg = regs->iir & 0x1f;
				BUG_ON(treg == 0);
				regs->gr[treg] = 0;
			}
		}

		regs->iaoq[0] = (unsigned long)&fix->fixup + fix->fixup;
		regs->iaoq[0] &= ~3;
		/*
		 * NOTE: In some cases the faulting instruction
		 * may be in the delay slot of a branch. We
		 * don't want to take the branch, so we don't
		 * increment iaoq[1], instead we set it to be
		 * iaoq[0]+4, and clear the B bit in the PSW
		 */
		regs->iaoq[1] = regs->iaoq[0] + 4;
		regs->gr[0] &= ~PSW_B; /* IPSW in gr[0] */

		return 1;
	}

	return 0;
}

/*
 * parisc hardware trap list
 *
 * Documented in section 3 "Addressing and Access Control" of the
 * "PA-RISC 1.1 Architecture and Instruction Set Reference Manual"
 * https://parisc.wiki.kernel.org/index.php/File:Pa11_acd.pdf
 *
 * For implementation see handle_interruption() in traps.c
 */
static const char * const trap_description[] = {
	[1] "High-priority machine check (HPMC)",
	[2] "Power failure interrupt",
	[3] "Recovery counter trap",
	[5] "Low-priority machine check",
	[6] "Instruction TLB miss fault",
	[7] "Instruction access rights / protection trap",
	[8] "Illegal instruction trap",
	[9] "Break instruction trap",
	[10] "Privileged operation trap",
	[11] "Privileged register trap",
	[12] "Overflow trap",
	[13] "Conditional trap",
	[14] "FP Assist Exception trap",
	[15] "Data TLB miss fault",
	[16] "Non-access ITLB miss fault",
	[17] "Non-access DTLB miss fault",
	[18] "Data memory protection/unaligned access trap",
	[19] "Data memory break trap",
	[20] "TLB dirty bit trap",
	[21] "Page reference trap",
	[22] "Assist emulation trap",
	[25] "Taken branch trap",
	[26] "Data memory access rights trap",
	[27] "Data memory protection ID trap",
	[28] "Unaligned data reference trap",
};

const char *trap_name(unsigned long code)
{
	const char *t = NULL;

	if (code < ARRAY_SIZE(trap_description))
		t = trap_description[code];

	return t ? t : "Unknown trap";
}

/*
 * Print out info about fatal segfaults, if the show_unhandled_signals
 * sysctl is set:
 */
static inline void
show_signal_msg(struct pt_regs *regs, unsigned long code,
		unsigned long address, struct task_struct *tsk,
		struct vm_area_struct *vma)
{
	if (!unhandled_signal(tsk, SIGSEGV))
		return;

	if (!printk_ratelimit())
		return;

	pr_warn("\n");
	pr_warn("do_page_fault() command='%s' type=%lu address=0x%08lx",
	    tsk->comm, code, address);
	print_vma_addr(KERN_CONT " in ", regs->iaoq[0]);

	pr_cont("\ntrap #%lu: %s%c", code, trap_name(code),
		vma ? ',':'\n');

	if (vma)
		pr_cont(" vm_start = 0x%08lx, vm_end = 0x%08lx\n",
			vma->vm_start, vma->vm_end);

	show_regs(regs);
}

void do_page_fault(struct pt_regs *regs, unsigned long code,
			      unsigned long address)
{
	struct vm_area_struct *vma, *prev_vma;
	struct task_struct *tsk;
	struct mm_struct *mm;
	unsigned long acc_type;
	vm_fault_t fault = 0;
	unsigned int flags;
	char *msg;

	tsk = current;
	mm = tsk->mm;
	if (!mm) {
		msg = "Page fault: no context";
		goto no_context;
	}

	flags = FAULT_FLAG_DEFAULT;
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	acc_type = parisc_acctyp(code, regs->iir);
	if (acc_type & VM_WRITE)
		flags |= FAULT_FLAG_WRITE;
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
retry:
	mmap_read_lock(mm);
	vma = find_vma_prev(mm, address, &prev_vma);
	if (!vma || address < vma->vm_start)
		goto check_expansion;
/*
 * Ok, we have a good vm_area for this memory access. We still need to
 * check the access permissions.
 */

good_area:

	if ((vma->vm_flags & acc_type) != acc_type)
		goto bad_area;

	/*
	 * If for any reason at all we couldn't handle the fault, make
	 * sure we exit gracefully rather than endlessly redo the
	 * fault.
	 */

	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs))
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		/*
		 * We hit a shared mapping outside of the file, or some
		 * other thing happened to us that made us unable to
		 * handle the page fault gracefully.
		 */
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|
				  VM_FAULT_HWPOISON_LARGE))
			goto bad_area;
		BUG();
	}
	if (fault & VM_FAULT_RETRY) {
		/*
		 * No need to mmap_read_unlock(mm) as we would
		 * have already released it in __lock_page_or_retry
		 * in mm/filemap.c.
		 */
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}
	mmap_read_unlock(mm);
	return;

check_expansion:
	vma = prev_vma;
	if (vma && (expand_stack(vma, address) == 0))
		goto good_area;

/*
 * Something tried to access memory that isn't in our memory map..
 */
bad_area:
	mmap_read_unlock(mm);

	if (user_mode(regs)) {
		int signo, si_code;

		switch (code) {
		case 15:	/* Data TLB miss fault/Data page fault */
			/* send SIGSEGV when outside of vma */
			if (!vma ||
			    address < vma->vm_start || address >= vma->vm_end) {
				signo = SIGSEGV;
				si_code = SEGV_MAPERR;
				break;
			}

			/* send SIGSEGV for wrong permissions */
			if ((vma->vm_flags & acc_type) != acc_type) {
				signo = SIGSEGV;
				si_code = SEGV_ACCERR;
				break;
			}

			/* probably address is outside of mapped file */
			fallthrough;
		case 17:	/* NA data TLB miss / page fault */
		case 18:	/* Unaligned access - PCXS only */
			signo = SIGBUS;
			si_code = (code == 18) ? BUS_ADRALN : BUS_ADRERR;
			break;
		case 16:	/* Non-access instruction TLB miss fault */
		case 26:	/* PCXL: Data memory access rights trap */
		default:
			signo = SIGSEGV;
			si_code = (code == 26) ? SEGV_ACCERR : SEGV_MAPERR;
			break;
		}
#ifdef CONFIG_MEMORY_FAILURE
		if (fault & (VM_FAULT_HWPOISON|VM_FAULT_HWPOISON_LARGE)) {
			unsigned int lsb = 0;
			printk(KERN_ERR
	"MCE: Killing %s:%d due to hardware memory corruption fault at %08lx\n",
			tsk->comm, tsk->pid, address);
			/*
			 * Either small page or large page may be poisoned.
			 * In other words, VM_FAULT_HWPOISON_LARGE and
			 * VM_FAULT_HWPOISON are mutually exclusive.
			 */
			if (fault & VM_FAULT_HWPOISON_LARGE)
				lsb = hstate_index_to_shift(VM_FAULT_GET_HINDEX(fault));
			else if (fault & VM_FAULT_HWPOISON)
				lsb = PAGE_SHIFT;

			force_sig_mceerr(BUS_MCEERR_AR, (void __user *) address,
					 lsb);
			return;
		}
#endif
		show_signal_msg(regs, code, address, tsk, vma);

		force_sig_fault(signo, si_code, (void __user *) address);
		return;
	}
	msg = "Page fault: bad address";

no_context:

	if (!user_mode(regs) && fixup_exception(regs)) {
		return;
	}

	parisc_terminate(msg, regs, code, address);

out_of_memory:
	mmap_read_unlock(mm);
	if (!user_mode(regs)) {
		msg = "Page fault: out of memory";
		goto no_context;
	}
	pagefault_out_of_memory();
}

/* Handle non-access data TLB miss faults.
 *
 * For probe instructions, accesses to userspace are considered allowed
 * if they lie in a valid VMA and the access type matches. We are not
 * allowed to handle MM faults here so there may be situations where an
 * actual access would fail even though a probe was successful.
 */
int
handle_nadtlb_fault(struct pt_regs *regs)
{
	unsigned long insn = regs->iir;
	int breg, treg, xreg, val = 0;
	struct vm_area_struct *vma, *prev_vma;
	struct task_struct *tsk;
	struct mm_struct *mm;
	unsigned long address;
	unsigned long acc_type;

	switch (insn & 0x380) {
	case 0x280:
		/* FDC instruction */
		fallthrough;
	case 0x380:
		/* PDC and FIC instructions */
		if (printk_ratelimit()) {
			pr_warn("BUG: nullifying cache flush/purge instruction\n");
			show_regs(regs);
		}
		if (insn & 0x20) {
			/* Base modification */
			breg = (insn >> 21) & 0x1f;
			xreg = (insn >> 16) & 0x1f;
			if (breg && xreg)
				regs->gr[breg] += regs->gr[xreg];
		}
		regs->gr[0] |= PSW_N;
		return 1;

	case 0x180:
		/* PROBE instruction */
		treg = insn & 0x1f;
		if (regs->isr) {
			tsk = current;
			mm = tsk->mm;
			if (mm) {
				/* Search for VMA */
				address = regs->ior;
				mmap_read_lock(mm);
				vma = find_vma_prev(mm, address, &prev_vma);
				mmap_read_unlock(mm);

				/*
				 * Check if access to the VMA is okay.
				 * We don't allow for stack expansion.
				 */
				acc_type = (insn & 0x40) ? VM_WRITE : VM_READ;
				if (vma
				    && address >= vma->vm_start
				    && (vma->vm_flags & acc_type) == acc_type)
					val = 1;
			}
		}
		if (treg)
			regs->gr[treg] = val;
		regs->gr[0] |= PSW_N;
		return 1;

	case 0x300:
		/* LPA instruction */
		if (insn & 0x20) {
			/* Base modification */
			breg = (insn >> 21) & 0x1f;
			xreg = (insn >> 16) & 0x1f;
			if (breg && xreg)
				regs->gr[breg] += regs->gr[xreg];
		}
		treg = insn & 0x1f;
		if (treg)
			regs->gr[treg] = 0;
		regs->gr[0] |= PSW_N;
		return 1;

	default:
		break;
	}

	return 0;
}
