/*
 *  linux/arch/m68k/kernel/traps.c
 *
 *  Copyright (C) 1993, 1994 by Hamish Macdonald
 *
 *  68040 fixes by Michael Rausch
 *  68040 fixes by Martin Apel
 *  68040 fixes and writeback by Richard Zidlicky
 *  68060 fixes by Roman Hodek
 *  68060 fixes by Jesper Skov
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Sets up all exception vectors
 */

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>

#include <asm/setup.h>
#include <asm/fpu.h>
#include <linux/uaccess.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/machdep.h>
#include <asm/siginfo.h>


static const char *vec_names[] = {
	[VEC_RESETSP]	= "RESET SP",
	[VEC_RESETPC]	= "RESET PC",
	[VEC_BUSERR]	= "BUS ERROR",
	[VEC_ADDRERR]	= "ADDRESS ERROR",
	[VEC_ILLEGAL]	= "ILLEGAL INSTRUCTION",
	[VEC_ZERODIV]	= "ZERO DIVIDE",
	[VEC_CHK]	= "CHK",
	[VEC_TRAP]	= "TRAPcc",
	[VEC_PRIV]	= "PRIVILEGE VIOLATION",
	[VEC_TRACE]	= "TRACE",
	[VEC_LINE10]	= "LINE 1010",
	[VEC_LINE11]	= "LINE 1111",
	[VEC_RESV12]	= "UNASSIGNED RESERVED 12",
	[VEC_COPROC]	= "COPROCESSOR PROTOCOL VIOLATION",
	[VEC_FORMAT]	= "FORMAT ERROR",
	[VEC_UNINT]	= "UNINITIALIZED INTERRUPT",
	[VEC_RESV16]	= "UNASSIGNED RESERVED 16",
	[VEC_RESV17]	= "UNASSIGNED RESERVED 17",
	[VEC_RESV18]	= "UNASSIGNED RESERVED 18",
	[VEC_RESV19]	= "UNASSIGNED RESERVED 19",
	[VEC_RESV20]	= "UNASSIGNED RESERVED 20",
	[VEC_RESV21]	= "UNASSIGNED RESERVED 21",
	[VEC_RESV22]	= "UNASSIGNED RESERVED 22",
	[VEC_RESV23]	= "UNASSIGNED RESERVED 23",
	[VEC_SPUR]	= "SPURIOUS INTERRUPT",
	[VEC_INT1]	= "LEVEL 1 INT",
	[VEC_INT2]	= "LEVEL 2 INT",
	[VEC_INT3]	= "LEVEL 3 INT",
	[VEC_INT4]	= "LEVEL 4 INT",
	[VEC_INT5]	= "LEVEL 5 INT",
	[VEC_INT6]	= "LEVEL 6 INT",
	[VEC_INT7]	= "LEVEL 7 INT",
	[VEC_SYS]	= "SYSCALL",
	[VEC_TRAP1]	= "TRAP #1",
	[VEC_TRAP2]	= "TRAP #2",
	[VEC_TRAP3]	= "TRAP #3",
	[VEC_TRAP4]	= "TRAP #4",
	[VEC_TRAP5]	= "TRAP #5",
	[VEC_TRAP6]	= "TRAP #6",
	[VEC_TRAP7]	= "TRAP #7",
	[VEC_TRAP8]	= "TRAP #8",
	[VEC_TRAP9]	= "TRAP #9",
	[VEC_TRAP10]	= "TRAP #10",
	[VEC_TRAP11]	= "TRAP #11",
	[VEC_TRAP12]	= "TRAP #12",
	[VEC_TRAP13]	= "TRAP #13",
	[VEC_TRAP14]	= "TRAP #14",
	[VEC_TRAP15]	= "TRAP #15",
	[VEC_FPBRUC]	= "FPCP BSUN",
	[VEC_FPIR]	= "FPCP INEXACT",
	[VEC_FPDIVZ]	= "FPCP DIV BY 0",
	[VEC_FPUNDER]	= "FPCP UNDERFLOW",
	[VEC_FPOE]	= "FPCP OPERAND ERROR",
	[VEC_FPOVER]	= "FPCP OVERFLOW",
	[VEC_FPNAN]	= "FPCP SNAN",
	[VEC_FPUNSUP]	= "FPCP UNSUPPORTED OPERATION",
	[VEC_MMUCFG]	= "MMU CONFIGURATION ERROR",
	[VEC_MMUILL]	= "MMU ILLEGAL OPERATION ERROR",
	[VEC_MMUACC]	= "MMU ACCESS LEVEL VIOLATION ERROR",
	[VEC_RESV59]	= "UNASSIGNED RESERVED 59",
	[VEC_UNIMPEA]	= "UNASSIGNED RESERVED 60",
	[VEC_UNIMPII]	= "UNASSIGNED RESERVED 61",
	[VEC_RESV62]	= "UNASSIGNED RESERVED 62",
	[VEC_RESV63]	= "UNASSIGNED RESERVED 63",
};

static const char *space_names[] = {
	[0]		= "Space 0",
	[USER_DATA]	= "User Data",
	[USER_PROGRAM]	= "User Program",
#ifndef CONFIG_SUN3
	[3]		= "Space 3",
#else
	[FC_CONTROL]	= "Control",
#endif
	[4]		= "Space 4",
	[SUPER_DATA]	= "Super Data",
	[SUPER_PROGRAM]	= "Super Program",
	[CPU_SPACE]	= "CPU"
};

void die_if_kernel(char *,struct pt_regs *,int);
asmlinkage int do_page_fault(struct pt_regs *regs, unsigned long address,
                             unsigned long error_code);
int send_fault_sig(struct pt_regs *regs);

asmlinkage void trap_c(struct frame *fp);

#if defined (CONFIG_M68060)
static inline void access_error060 (struct frame *fp)
{
	unsigned long fslw = fp->un.fmt4.pc; /* is really FSLW for access error */

	pr_debug("fslw=%#lx, fa=%#lx\n", fslw, fp->un.fmt4.effaddr);

	if (fslw & MMU060_BPE) {
		/* branch prediction error -> clear branch cache */
		__asm__ __volatile__ ("movec %/cacr,%/d0\n\t"
				      "orl   #0x00400000,%/d0\n\t"
				      "movec %/d0,%/cacr"
				      : : : "d0" );
		/* return if there's no other error */
		if (!(fslw & MMU060_ERR_BITS) && !(fslw & MMU060_SEE))
			return;
	}

	if (fslw & (MMU060_DESC_ERR | MMU060_WP | MMU060_SP)) {
		unsigned long errorcode;
		unsigned long addr = fp->un.fmt4.effaddr;

		if (fslw & MMU060_MA)
			addr = (addr + PAGE_SIZE - 1) & PAGE_MASK;

		errorcode = 1;
		if (fslw & MMU060_DESC_ERR) {
			__flush_tlb040_one(addr);
			errorcode = 0;
		}
		if (fslw & MMU060_W)
			errorcode |= 2;
		pr_debug("errorcode = %ld\n", errorcode);
		do_page_fault(&fp->ptregs, addr, errorcode);
	} else if (fslw & (MMU060_SEE)){
		/* Software Emulation Error.
		 * fault during mem_read/mem_write in ifpsp060/os.S
		 */
		send_fault_sig(&fp->ptregs);
	} else if (!(fslw & (MMU060_RE|MMU060_WE)) ||
		   send_fault_sig(&fp->ptregs) > 0) {
		pr_err("pc=%#lx, fa=%#lx\n", fp->ptregs.pc,
		       fp->un.fmt4.effaddr);
		pr_err("68060 access error, fslw=%lx\n", fslw);
		trap_c( fp );
	}
}
#endif /* CONFIG_M68060 */

#if defined (CONFIG_M68040)
static inline unsigned long probe040(int iswrite, unsigned long addr, int wbs)
{
	unsigned long mmusr;
	mm_segment_t old_fs = get_fs();

	set_fs(MAKE_MM_SEG(wbs));

	if (iswrite)
		asm volatile (".chip 68040; ptestw (%0); .chip 68k" : : "a" (addr));
	else
		asm volatile (".chip 68040; ptestr (%0); .chip 68k" : : "a" (addr));

	asm volatile (".chip 68040; movec %%mmusr,%0; .chip 68k" : "=r" (mmusr));

	set_fs(old_fs);

	return mmusr;
}

static inline int do_040writeback1(unsigned short wbs, unsigned long wba,
				   unsigned long wbd)
{
	int res = 0;
	mm_segment_t old_fs = get_fs();

	/* set_fs can not be moved, otherwise put_user() may oops */
	set_fs(MAKE_MM_SEG(wbs));

	switch (wbs & WBSIZ_040) {
	case BA_SIZE_BYTE:
		res = put_user(wbd & 0xff, (char __user *)wba);
		break;
	case BA_SIZE_WORD:
		res = put_user(wbd & 0xffff, (short __user *)wba);
		break;
	case BA_SIZE_LONG:
		res = put_user(wbd, (int __user *)wba);
		break;
	}

	/* set_fs can not be moved, otherwise put_user() may oops */
	set_fs(old_fs);


	pr_debug("do_040writeback1, res=%d\n", res);

	return res;
}

/* after an exception in a writeback the stack frame corresponding
 * to that exception is discarded, set a few bits in the old frame
 * to simulate what it should look like
 */
static inline void fix_xframe040(struct frame *fp, unsigned long wba, unsigned short wbs)
{
	fp->un.fmt7.faddr = wba;
	fp->un.fmt7.ssw = wbs & 0xff;
	if (wba != current->thread.faddr)
	    fp->un.fmt7.ssw |= MA_040;
}

static inline void do_040writebacks(struct frame *fp)
{
	int res = 0;
#if 0
	if (fp->un.fmt7.wb1s & WBV_040)
		pr_err("access_error040: cannot handle 1st writeback. oops.\n");
#endif

	if ((fp->un.fmt7.wb2s & WBV_040) &&
	    !(fp->un.fmt7.wb2s & WBTT_040)) {
		res = do_040writeback1(fp->un.fmt7.wb2s, fp->un.fmt7.wb2a,
				       fp->un.fmt7.wb2d);
		if (res)
			fix_xframe040(fp, fp->un.fmt7.wb2a, fp->un.fmt7.wb2s);
		else
			fp->un.fmt7.wb2s = 0;
	}

	/* do the 2nd wb only if the first one was successful (except for a kernel wb) */
	if (fp->un.fmt7.wb3s & WBV_040 && (!res || fp->un.fmt7.wb3s & 4)) {
		res = do_040writeback1(fp->un.fmt7.wb3s, fp->un.fmt7.wb3a,
				       fp->un.fmt7.wb3d);
		if (res)
		    {
			fix_xframe040(fp, fp->un.fmt7.wb3a, fp->un.fmt7.wb3s);

			fp->un.fmt7.wb2s = fp->un.fmt7.wb3s;
			fp->un.fmt7.wb3s &= (~WBV_040);
			fp->un.fmt7.wb2a = fp->un.fmt7.wb3a;
			fp->un.fmt7.wb2d = fp->un.fmt7.wb3d;
		    }
		else
			fp->un.fmt7.wb3s = 0;
	}

	if (res)
		send_fault_sig(&fp->ptregs);
}

/*
 * called from sigreturn(), must ensure userspace code didn't
 * manipulate exception frame to circumvent protection, then complete
 * pending writebacks
 * we just clear TM2 to turn it into a userspace access
 */
asmlinkage void berr_040cleanup(struct frame *fp)
{
	fp->un.fmt7.wb2s &= ~4;
	fp->un.fmt7.wb3s &= ~4;

	do_040writebacks(fp);
}

static inline void access_error040(struct frame *fp)
{
	unsigned short ssw = fp->un.fmt7.ssw;
	unsigned long mmusr;

	pr_debug("ssw=%#x, fa=%#lx\n", ssw, fp->un.fmt7.faddr);
	pr_debug("wb1s=%#x, wb2s=%#x, wb3s=%#x\n", fp->un.fmt7.wb1s,
		fp->un.fmt7.wb2s, fp->un.fmt7.wb3s);
	pr_debug("wb2a=%lx, wb3a=%lx, wb2d=%lx, wb3d=%lx\n",
		fp->un.fmt7.wb2a, fp->un.fmt7.wb3a,
		fp->un.fmt7.wb2d, fp->un.fmt7.wb3d);

	if (ssw & ATC_040) {
		unsigned long addr = fp->un.fmt7.faddr;
		unsigned long errorcode;

		/*
		 * The MMU status has to be determined AFTER the address
		 * has been corrected if there was a misaligned access (MA).
		 */
		if (ssw & MA_040)
			addr = (addr + 7) & -8;

		/* MMU error, get the MMUSR info for this access */
		mmusr = probe040(!(ssw & RW_040), addr, ssw);
		pr_debug("mmusr = %lx\n", mmusr);
		errorcode = 1;
		if (!(mmusr & MMU_R_040)) {
			/* clear the invalid atc entry */
			__flush_tlb040_one(addr);
			errorcode = 0;
		}

		/* despite what documentation seems to say, RMW
		 * accesses have always both the LK and RW bits set */
		if (!(ssw & RW_040) || (ssw & LK_040))
			errorcode |= 2;

		if (do_page_fault(&fp->ptregs, addr, errorcode)) {
			pr_debug("do_page_fault() !=0\n");
			if (user_mode(&fp->ptregs)){
				/* delay writebacks after signal delivery */
				pr_debug(".. was usermode - return\n");
				return;
			}
			/* disable writeback into user space from kernel
			 * (if do_page_fault didn't fix the mapping,
                         * the writeback won't do good)
			 */
disable_wb:
			pr_debug(".. disabling wb2\n");
			if (fp->un.fmt7.wb2a == fp->un.fmt7.faddr)
				fp->un.fmt7.wb2s &= ~WBV_040;
			if (fp->un.fmt7.wb3a == fp->un.fmt7.faddr)
				fp->un.fmt7.wb3s &= ~WBV_040;
		}
	} else {
		/* In case of a bus error we either kill the process or expect
		 * the kernel to catch the fault, which then is also responsible
		 * for cleaning up the mess.
		 */
		current->thread.signo = SIGBUS;
		current->thread.faddr = fp->un.fmt7.faddr;
		if (send_fault_sig(&fp->ptregs) >= 0)
			pr_err("68040 bus error (ssw=%x, faddr=%lx)\n", ssw,
			       fp->un.fmt7.faddr);
		goto disable_wb;
	}

	do_040writebacks(fp);
}
#endif /* CONFIG_M68040 */

#if defined(CONFIG_SUN3)
#include <asm/sun3mmu.h>

extern int mmu_emu_handle_fault (unsigned long, int, int);

/* sun3 version of bus_error030 */

static inline void bus_error030 (struct frame *fp)
{
	unsigned char buserr_type = sun3_get_buserr ();
	unsigned long addr, errorcode;
	unsigned short ssw = fp->un.fmtb.ssw;
	extern unsigned long _sun3_map_test_start, _sun3_map_test_end;

	if (ssw & (FC | FB))
		pr_debug("Instruction fault at %#010lx\n",
			ssw & FC ?
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 2 : fp->un.fmtb.baddr - 2
			:
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 4 : fp->un.fmtb.baddr);
	if (ssw & DF)
		pr_debug("Data %s fault at %#010lx in %s (pc=%#lx)\n",
			ssw & RW ? "read" : "write",
			fp->un.fmtb.daddr,
			space_names[ssw & DFC], fp->ptregs.pc);

	/*
	 * Check if this page should be demand-mapped. This needs to go before
	 * the testing for a bad kernel-space access (demand-mapping applies
	 * to kernel accesses too).
	 */

	if ((ssw & DF)
	    && (buserr_type & (SUN3_BUSERR_PROTERR | SUN3_BUSERR_INVALID))) {
		if (mmu_emu_handle_fault (fp->un.fmtb.daddr, ssw & RW, 0))
			return;
	}

	/* Check for kernel-space pagefault (BAD). */
	if (fp->ptregs.sr & PS_S) {
		/* kernel fault must be a data fault to user space */
		if (! ((ssw & DF) && ((ssw & DFC) == USER_DATA))) {
		     // try checking the kernel mappings before surrender
		     if (mmu_emu_handle_fault (fp->un.fmtb.daddr, ssw & RW, 1))
			  return;
			/* instruction fault or kernel data fault! */
			if (ssw & (FC | FB))
				pr_err("Instruction fault at %#010lx\n",
					fp->ptregs.pc);
			if (ssw & DF) {
				/* was this fault incurred testing bus mappings? */
				if((fp->ptregs.pc >= (unsigned long)&_sun3_map_test_start) &&
				   (fp->ptregs.pc <= (unsigned long)&_sun3_map_test_end)) {
					send_fault_sig(&fp->ptregs);
					return;
				}

				pr_err("Data %s fault at %#010lx in %s (pc=%#lx)\n",
					ssw & RW ? "read" : "write",
					fp->un.fmtb.daddr,
					space_names[ssw & DFC], fp->ptregs.pc);
			}
			pr_err("BAD KERNEL BUSERR\n");

			die_if_kernel("Oops", &fp->ptregs,0);
			force_sig(SIGKILL);
			return;
		}
	} else {
		/* user fault */
		if (!(ssw & (FC | FB)) && !(ssw & DF))
			/* not an instruction fault or data fault! BAD */
			panic ("USER BUSERR w/o instruction or data fault");
	}


	/* First handle the data fault, if any.  */
	if (ssw & DF) {
		addr = fp->un.fmtb.daddr;

// errorcode bit 0:	0 -> no page		1 -> protection fault
// errorcode bit 1:	0 -> read fault		1 -> write fault

// (buserr_type & SUN3_BUSERR_PROTERR)	-> protection fault
// (buserr_type & SUN3_BUSERR_INVALID)	-> invalid page fault

		if (buserr_type & SUN3_BUSERR_PROTERR)
			errorcode = 0x01;
		else if (buserr_type & SUN3_BUSERR_INVALID)
			errorcode = 0x00;
		else {
			pr_debug("*** unexpected busfault type=%#04x\n",
				 buserr_type);
			pr_debug("invalid %s access at %#lx from pc %#lx\n",
				 !(ssw & RW) ? "write" : "read", addr,
				 fp->ptregs.pc);
			die_if_kernel ("Oops", &fp->ptregs, buserr_type);
			force_sig (SIGBUS);
			return;
		}

//todo: wtf is RM bit? --m
		if (!(ssw & RW) || ssw & RM)
			errorcode |= 0x02;

		/* Handle page fault. */
		do_page_fault (&fp->ptregs, addr, errorcode);

		/* Retry the data fault now. */
		return;
	}

	/* Now handle the instruction fault. */

	/* Get the fault address. */
	if (fp->ptregs.format == 0xA)
		addr = fp->ptregs.pc + 4;
	else
		addr = fp->un.fmtb.baddr;
	if (ssw & FC)
		addr -= 2;

	if (buserr_type & SUN3_BUSERR_INVALID) {
		if (!mmu_emu_handle_fault(addr, 1, 0))
			do_page_fault (&fp->ptregs, addr, 0);
       } else {
		pr_debug("protection fault on insn access (segv).\n");
		force_sig (SIGSEGV);
       }
}
#else
#if defined(CPU_M68020_OR_M68030)
static inline void bus_error030 (struct frame *fp)
{
	volatile unsigned short temp;
	unsigned short mmusr;
	unsigned long addr, errorcode;
	unsigned short ssw = fp->un.fmtb.ssw;
#ifdef DEBUG
	unsigned long desc;
#endif

	pr_debug("pid = %x  ", current->pid);
	pr_debug("SSW=%#06x  ", ssw);

	if (ssw & (FC | FB))
		pr_debug("Instruction fault at %#010lx\n",
			ssw & FC ?
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 2 : fp->un.fmtb.baddr - 2
			:
			fp->ptregs.format == 0xa ? fp->ptregs.pc + 4 : fp->un.fmtb.baddr);
	if (ssw & DF)
		pr_debug("Data %s fault at %#010lx in %s (pc=%#lx)\n",
			ssw & RW ? "read" : "write",
			fp->un.fmtb.daddr,
			space_names[ssw & DFC], fp->ptregs.pc);

	/* ++andreas: If a data fault and an instruction fault happen
	   at the same time map in both pages.  */

	/* First handle the data fault, if any.  */
	if (ssw & DF) {
		addr = fp->un.fmtb.daddr;

#ifdef DEBUG
		asm volatile ("ptestr %3,%2@,#7,%0\n\t"
			      "pmove %%psr,%1"
			      : "=a&" (desc), "=m" (temp)
			      : "a" (addr), "d" (ssw));
		pr_debug("mmusr is %#x for addr %#lx in task %p\n",
			 temp, addr, current);
		pr_debug("descriptor address is 0x%p, contents %#lx\n",
			 __va(desc), *(unsigned long *)__va(desc));
#else
		asm volatile ("ptestr %2,%1@,#7\n\t"
			      "pmove %%psr,%0"
			      : "=m" (temp) : "a" (addr), "d" (ssw));
#endif
		mmusr = temp;
		errorcode = (mmusr & MMU_I) ? 0 : 1;
		if (!(ssw & RW) || (ssw & RM))
			errorcode |= 2;

		if (mmusr & (MMU_I | MMU_WP)) {
			if (ssw & 4) {
				pr_err("Data %s fault at %#010lx in %s (pc=%#lx)\n",
				       ssw & RW ? "read" : "write",
				       fp->un.fmtb.daddr,
				       space_names[ssw & DFC], fp->ptregs.pc);
				goto buserr;
			}
			/* Don't try to do anything further if an exception was
			   handled. */
			if (do_page_fault (&fp->ptregs, addr, errorcode) < 0)
				return;
		} else if (!(mmusr & MMU_I)) {
			/* probably a 020 cas fault */
			if (!(ssw & RM) && send_fault_sig(&fp->ptregs) > 0)
				pr_err("unexpected bus error (%#x,%#x)\n", ssw,
				       mmusr);
		} else if (mmusr & (MMU_B|MMU_L|MMU_S)) {
			pr_err("invalid %s access at %#lx from pc %#lx\n",
			       !(ssw & RW) ? "write" : "read", addr,
			       fp->ptregs.pc);
			die_if_kernel("Oops",&fp->ptregs,mmusr);
			force_sig(SIGSEGV);
			return;
		} else {
#if 0
			static volatile long tlong;
#endif

			pr_err("weird %s access at %#lx from pc %#lx (ssw is %#x)\n",
			       !(ssw & RW) ? "write" : "read", addr,
			       fp->ptregs.pc, ssw);
			asm volatile ("ptestr #1,%1@,#0\n\t"
				      "pmove %%psr,%0"
				      : "=m" (temp)
				      : "a" (addr));
			mmusr = temp;

			pr_err("level 0 mmusr is %#x\n", mmusr);
#if 0
			asm volatile ("pmove %%tt0,%0"
				      : "=m" (tlong));
			pr_debug("tt0 is %#lx, ", tlong);
			asm volatile ("pmove %%tt1,%0"
				      : "=m" (tlong));
			pr_debug("tt1 is %#lx\n", tlong);
#endif
			pr_debug("Unknown SIGSEGV - 1\n");
			die_if_kernel("Oops",&fp->ptregs,mmusr);
			force_sig(SIGSEGV);
			return;
		}

		/* setup an ATC entry for the access about to be retried */
		if (!(ssw & RW) || (ssw & RM))
			asm volatile ("ploadw %1,%0@" : /* no outputs */
				      : "a" (addr), "d" (ssw));
		else
			asm volatile ("ploadr %1,%0@" : /* no outputs */
				      : "a" (addr), "d" (ssw));
	}

	/* Now handle the instruction fault. */

	if (!(ssw & (FC|FB)))
		return;

	if (fp->ptregs.sr & PS_S) {
		pr_err("Instruction fault at %#010lx\n", fp->ptregs.pc);
	buserr:
		pr_err("BAD KERNEL BUSERR\n");
		die_if_kernel("Oops",&fp->ptregs,0);
		force_sig(SIGKILL);
		return;
	}

	/* get the fault address */
	if (fp->ptregs.format == 10)
		addr = fp->ptregs.pc + 4;
	else
		addr = fp->un.fmtb.baddr;
	if (ssw & FC)
		addr -= 2;

	if ((ssw & DF) && ((addr ^ fp->un.fmtb.daddr) & PAGE_MASK) == 0)
		/* Insn fault on same page as data fault.  But we
		   should still create the ATC entry.  */
		goto create_atc_entry;

#ifdef DEBUG
	asm volatile ("ptestr #1,%2@,#7,%0\n\t"
		      "pmove %%psr,%1"
		      : "=a&" (desc), "=m" (temp)
		      : "a" (addr));
	pr_debug("mmusr is %#x for addr %#lx in task %p\n",
		temp, addr, current);
	pr_debug("descriptor address is 0x%p, contents %#lx\n",
		__va(desc), *(unsigned long *)__va(desc));
#else
	asm volatile ("ptestr #1,%1@,#7\n\t"
		      "pmove %%psr,%0"
		      : "=m" (temp) : "a" (addr));
#endif
	mmusr = temp;
	if (mmusr & MMU_I)
		do_page_fault (&fp->ptregs, addr, 0);
	else if (mmusr & (MMU_B|MMU_L|MMU_S)) {
		pr_err("invalid insn access at %#lx from pc %#lx\n",
			addr, fp->ptregs.pc);
		pr_debug("Unknown SIGSEGV - 2\n");
		die_if_kernel("Oops",&fp->ptregs,mmusr);
		force_sig(SIGSEGV);
		return;
	}

create_atc_entry:
	/* setup an ATC entry for the access about to be retried */
	asm volatile ("ploadr #2,%0@" : /* no outputs */
		      : "a" (addr));
}
#endif /* CPU_M68020_OR_M68030 */
#endif /* !CONFIG_SUN3 */

#if defined(CONFIG_COLDFIRE) && defined(CONFIG_MMU)
#include <asm/mcfmmu.h>

/*
 *	The following table converts the FS encoding of a ColdFire
 *	exception stack frame into the error_code value needed by
 *	do_fault.
*/
static const unsigned char fs_err_code[] = {
	0,  /* 0000 */
	0,  /* 0001 */
	0,  /* 0010 */
	0,  /* 0011 */
	1,  /* 0100 */
	0,  /* 0101 */
	0,  /* 0110 */
	0,  /* 0111 */
	2,  /* 1000 */
	3,  /* 1001 */
	2,  /* 1010 */
	0,  /* 1011 */
	1,  /* 1100 */
	1,  /* 1101 */
	0,  /* 1110 */
	0   /* 1111 */
};

static inline void access_errorcf(unsigned int fs, struct frame *fp)
{
	unsigned long mmusr, addr;
	unsigned int err_code;
	int need_page_fault;

	mmusr = mmu_read(MMUSR);
	addr = mmu_read(MMUAR);

	/*
	 * error_code:
	 *	bit 0 == 0 means no page found, 1 means protection fault
	 *	bit 1 == 0 means read, 1 means write
	 */
	switch (fs) {
	case  5:  /* 0101 TLB opword X miss */
		need_page_fault = cf_tlb_miss(&fp->ptregs, 0, 0, 0);
		addr = fp->ptregs.pc;
		break;
	case  6:  /* 0110 TLB extension word X miss */
		need_page_fault = cf_tlb_miss(&fp->ptregs, 0, 0, 1);
		addr = fp->ptregs.pc + sizeof(long);
		break;
	case 10:  /* 1010 TLB W miss */
		need_page_fault = cf_tlb_miss(&fp->ptregs, 1, 1, 0);
		break;
	case 14: /* 1110 TLB R miss */
		need_page_fault = cf_tlb_miss(&fp->ptregs, 0, 1, 0);
		break;
	default:
		/* 0000 Normal  */
		/* 0001 Reserved */
		/* 0010 Interrupt during debug service routine */
		/* 0011 Reserved */
		/* 0100 X Protection */
		/* 0111 IFP in emulator mode */
		/* 1000 W Protection*/
		/* 1001 Write error*/
		/* 1011 Reserved*/
		/* 1100 R Protection*/
		/* 1101 R Protection*/
		/* 1111 OEP in emulator mode*/
		need_page_fault = 1;
		break;
	}

	if (need_page_fault) {
		err_code = fs_err_code[fs];
		if ((fs == 13) && (mmusr & MMUSR_WF)) /* rd-mod-wr access */
			err_code |= 2; /* bit1 - write, bit0 - protection */
		do_page_fault(&fp->ptregs, addr, err_code);
	}
}
#endif /* CONFIG_COLDFIRE CONFIG_MMU */

asmlinkage void buserr_c(struct frame *fp)
{
	/* Only set esp0 if coming from user mode */
	if (user_mode(&fp->ptregs))
		current->thread.esp0 = (unsigned long) fp;

	pr_debug("*** Bus Error *** Format is %x\n", fp->ptregs.format);

#if defined(CONFIG_COLDFIRE) && defined(CONFIG_MMU)
	if (CPU_IS_COLDFIRE) {
		unsigned int fs;
		fs = (fp->ptregs.vector & 0x3) |
			((fp->ptregs.vector & 0xc00) >> 8);
		switch (fs) {
		case 0x5:
		case 0x6:
		case 0x7:
		case 0x9:
		case 0xa:
		case 0xd:
		case 0xe:
		case 0xf:
			access_errorcf(fs, fp);
			return;
		default:
			break;
		}
	}
#endif /* CONFIG_COLDFIRE && CONFIG_MMU */

	switch (fp->ptregs.format) {
#if defined (CONFIG_M68060)
	case 4:				/* 68060 access error */
	  access_error060 (fp);
	  break;
#endif
#if defined (CONFIG_M68040)
	case 0x7:			/* 68040 access error */
	  access_error040 (fp);
	  break;
#endif
#if defined (CPU_M68020_OR_M68030)
	case 0xa:
	case 0xb:
	  bus_error030 (fp);
	  break;
#endif
	default:
	  die_if_kernel("bad frame format",&fp->ptregs,0);
	  pr_debug("Unknown SIGSEGV - 4\n");
	  force_sig(SIGSEGV);
	}
}


static int kstack_depth_to_print = 48;

void show_trace(unsigned long *stack)
{
	unsigned long *endstack;
	unsigned long addr;
	int i;

	pr_info("Call Trace:");
	addr = (unsigned long)stack + THREAD_SIZE - 1;
	endstack = (unsigned long *)(addr & -THREAD_SIZE);
	i = 0;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (__kernel_text_address(addr)) {
#ifndef CONFIG_KALLSYMS
			if (i % 5 == 0)
				pr_cont("\n       ");
#endif
			pr_cont(" [<%08lx>] %pS\n", addr, (void *)addr);
			i++;
		}
	}
	pr_cont("\n");
}

void show_registers(struct pt_regs *regs)
{
	struct frame *fp = (struct frame *)regs;
	mm_segment_t old_fs = get_fs();
	u16 c, *cp;
	unsigned long addr;
	int i;

	print_modules();
	pr_info("PC: [<%08lx>] %pS\n", regs->pc, (void *)regs->pc);
	pr_info("SR: %04x  SP: %p  a2: %08lx\n", regs->sr, regs, regs->a2);
	pr_info("d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
	       regs->d0, regs->d1, regs->d2, regs->d3);
	pr_info("d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
	       regs->d4, regs->d5, regs->a0, regs->a1);

	pr_info("Process %s (pid: %d, task=%p)\n",
		current->comm, task_pid_nr(current), current);
	addr = (unsigned long)&fp->un;
	pr_info("Frame format=%X ", regs->format);
	switch (regs->format) {
	case 0x2:
		pr_cont("instr addr=%08lx\n", fp->un.fmt2.iaddr);
		addr += sizeof(fp->un.fmt2);
		break;
	case 0x3:
		pr_cont("eff addr=%08lx\n", fp->un.fmt3.effaddr);
		addr += sizeof(fp->un.fmt3);
		break;
	case 0x4:
		if (CPU_IS_060)
			pr_cont("fault addr=%08lx fslw=%08lx\n",
				fp->un.fmt4.effaddr, fp->un.fmt4.pc);
		else
			pr_cont("eff addr=%08lx pc=%08lx\n",
				fp->un.fmt4.effaddr, fp->un.fmt4.pc);
		addr += sizeof(fp->un.fmt4);
		break;
	case 0x7:
		pr_cont("eff addr=%08lx ssw=%04x faddr=%08lx\n",
			fp->un.fmt7.effaddr, fp->un.fmt7.ssw, fp->un.fmt7.faddr);
		pr_info("wb 1 stat/addr/data: %04x %08lx %08lx\n",
			fp->un.fmt7.wb1s, fp->un.fmt7.wb1a, fp->un.fmt7.wb1dpd0);
		pr_info("wb 2 stat/addr/data: %04x %08lx %08lx\n",
			fp->un.fmt7.wb2s, fp->un.fmt7.wb2a, fp->un.fmt7.wb2d);
		pr_info("wb 3 stat/addr/data: %04x %08lx %08lx\n",
			fp->un.fmt7.wb3s, fp->un.fmt7.wb3a, fp->un.fmt7.wb3d);
		pr_info("push data: %08lx %08lx %08lx %08lx\n",
			fp->un.fmt7.wb1dpd0, fp->un.fmt7.pd1, fp->un.fmt7.pd2,
			fp->un.fmt7.pd3);
		addr += sizeof(fp->un.fmt7);
		break;
	case 0x9:
		pr_cont("instr addr=%08lx\n", fp->un.fmt9.iaddr);
		addr += sizeof(fp->un.fmt9);
		break;
	case 0xa:
		pr_cont("ssw=%04x isc=%04x isb=%04x daddr=%08lx dobuf=%08lx\n",
			fp->un.fmta.ssw, fp->un.fmta.isc, fp->un.fmta.isb,
			fp->un.fmta.daddr, fp->un.fmta.dobuf);
		addr += sizeof(fp->un.fmta);
		break;
	case 0xb:
		pr_cont("ssw=%04x isc=%04x isb=%04x daddr=%08lx dobuf=%08lx\n",
			fp->un.fmtb.ssw, fp->un.fmtb.isc, fp->un.fmtb.isb,
			fp->un.fmtb.daddr, fp->un.fmtb.dobuf);
		pr_info("baddr=%08lx dibuf=%08lx ver=%x\n",
			fp->un.fmtb.baddr, fp->un.fmtb.dibuf, fp->un.fmtb.ver);
		addr += sizeof(fp->un.fmtb);
		break;
	default:
		pr_cont("\n");
	}
	show_stack(NULL, (unsigned long *)addr);

	pr_info("Code:");
	set_fs(KERNEL_DS);
	cp = (u16 *)regs->pc;
	for (i = -8; i < 16; i++) {
		if (get_user(c, cp + i) && i >= 0) {
			pr_cont(" Bad PC value.");
			break;
		}
		if (i)
			pr_cont(" %04x", c);
		else
			pr_cont(" <%04x>", c);
	}
	set_fs(old_fs);
	pr_cont("\n");
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *p;
	unsigned long *endstack;
	int i;

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.esp0;
		else
			stack = (unsigned long *)&stack;
	}
	endstack = (unsigned long *)(((unsigned long)stack + THREAD_SIZE - 1) & -THREAD_SIZE);

	pr_info("Stack from %08lx:", (unsigned long)stack);
	p = stack;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (p + 1 > endstack)
			break;
		if (i % 8 == 0)
			pr_cont("\n       ");
		pr_cont(" %08lx", *p++);
	}
	pr_cont("\n");
	show_trace(stack);
}

/*
 * The vector number returned in the frame pointer may also contain
 * the "fs" (Fault Status) bits on ColdFire. These are in the bottom
 * 2 bits, and upper 2 bits. So we need to mask out the real vector
 * number before using it in comparisons. You don't need to do this on
 * real 68k parts, but it won't hurt either.
 */

void bad_super_trap (struct frame *fp)
{
	int vector = (fp->ptregs.vector >> 2) & 0xff;

	console_verbose();
	if (vector < ARRAY_SIZE(vec_names))
		pr_err("*** %s ***   FORMAT=%X\n",
			vec_names[vector],
			fp->ptregs.format);
	else
		pr_err("*** Exception %d ***   FORMAT=%X\n",
			vector, fp->ptregs.format);
	if (vector == VEC_ADDRERR && CPU_IS_020_OR_030) {
		unsigned short ssw = fp->un.fmtb.ssw;

		pr_err("SSW=%#06x  ", ssw);

		if (ssw & RC)
			pr_err("Pipe stage C instruction fault at %#010lx\n",
				(fp->ptregs.format) == 0xA ?
				fp->ptregs.pc + 2 : fp->un.fmtb.baddr - 2);
		if (ssw & RB)
			pr_err("Pipe stage B instruction fault at %#010lx\n",
				(fp->ptregs.format) == 0xA ?
				fp->ptregs.pc + 4 : fp->un.fmtb.baddr);
		if (ssw & DF)
			pr_err("Data %s fault at %#010lx in %s (pc=%#lx)\n",
				ssw & RW ? "read" : "write",
				fp->un.fmtb.daddr, space_names[ssw & DFC],
				fp->ptregs.pc);
	}
	pr_err("Current process id is %d\n", task_pid_nr(current));
	die_if_kernel("BAD KERNEL TRAP", &fp->ptregs, 0);
}

asmlinkage void trap_c(struct frame *fp)
{
	int sig, si_code;
	void __user *addr;
	int vector = (fp->ptregs.vector >> 2) & 0xff;

	if (fp->ptregs.sr & PS_S) {
		if (vector == VEC_TRACE) {
			/* traced a trapping instruction on a 68020/30,
			 * real exception will be executed afterwards.
			 */
			return;
		}
#ifdef CONFIG_MMU
		if (fixup_exception(&fp->ptregs))
			return;
#endif
		bad_super_trap(fp);
		return;
	}

	/* send the appropriate signal to the user program */
	switch (vector) {
	    case VEC_ADDRERR:
		si_code = BUS_ADRALN;
		sig = SIGBUS;
		break;
	    case VEC_ILLEGAL:
	    case VEC_LINE10:
	    case VEC_LINE11:
		si_code = ILL_ILLOPC;
		sig = SIGILL;
		break;
	    case VEC_PRIV:
		si_code = ILL_PRVOPC;
		sig = SIGILL;
		break;
	    case VEC_COPROC:
		si_code = ILL_COPROC;
		sig = SIGILL;
		break;
	    case VEC_TRAP1:
	    case VEC_TRAP2:
	    case VEC_TRAP3:
	    case VEC_TRAP4:
	    case VEC_TRAP5:
	    case VEC_TRAP6:
	    case VEC_TRAP7:
	    case VEC_TRAP8:
	    case VEC_TRAP9:
	    case VEC_TRAP10:
	    case VEC_TRAP11:
	    case VEC_TRAP12:
	    case VEC_TRAP13:
	    case VEC_TRAP14:
		si_code = ILL_ILLTRP;
		sig = SIGILL;
		break;
	    case VEC_FPBRUC:
	    case VEC_FPOE:
	    case VEC_FPNAN:
		si_code = FPE_FLTINV;
		sig = SIGFPE;
		break;
	    case VEC_FPIR:
		si_code = FPE_FLTRES;
		sig = SIGFPE;
		break;
	    case VEC_FPDIVZ:
		si_code = FPE_FLTDIV;
		sig = SIGFPE;
		break;
	    case VEC_FPUNDER:
		si_code = FPE_FLTUND;
		sig = SIGFPE;
		break;
	    case VEC_FPOVER:
		si_code = FPE_FLTOVF;
		sig = SIGFPE;
		break;
	    case VEC_ZERODIV:
		si_code = FPE_INTDIV;
		sig = SIGFPE;
		break;
	    case VEC_CHK:
	    case VEC_TRAP:
		si_code = FPE_INTOVF;
		sig = SIGFPE;
		break;
	    case VEC_TRACE:		/* ptrace single step */
		si_code = TRAP_TRACE;
		sig = SIGTRAP;
		break;
	    case VEC_TRAP15:		/* breakpoint */
		si_code = TRAP_BRKPT;
		sig = SIGTRAP;
		break;
	    default:
		si_code = ILL_ILLOPC;
		sig = SIGILL;
		break;
	}
	switch (fp->ptregs.format) {
	    default:
		addr = (void __user *) fp->ptregs.pc;
		break;
	    case 2:
		addr = (void __user *) fp->un.fmt2.iaddr;
		break;
	    case 7:
		addr = (void __user *) fp->un.fmt7.effaddr;
		break;
	    case 9:
		addr = (void __user *) fp->un.fmt9.iaddr;
		break;
	    case 10:
		addr = (void __user *) fp->un.fmta.daddr;
		break;
	    case 11:
		addr = (void __user*) fp->un.fmtb.daddr;
		break;
	}
	force_sig_fault(sig, si_code, addr);
}

void die_if_kernel (char *str, struct pt_regs *fp, int nr)
{
	if (!(fp->sr & PS_S))
		return;

	console_verbose();
	pr_crit("%s: %08x\n", str, nr);
	show_registers(fp);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	do_exit(SIGSEGV);
}

asmlinkage void set_esp0(unsigned long ssp)
{
	current->thread.esp0 = ssp;
}

/*
 * This function is called if an error occur while accessing
 * user-space from the fpsp040 code.
 */
asmlinkage void fpsp040_die(void)
{
	do_exit(SIGSEGV);
}

#ifdef CONFIG_M68KFPU_EMU
asmlinkage void fpemu_signal(int signal, int code, void *addr)
{
	force_sig_fault(signal, code, addr);
}
#endif
