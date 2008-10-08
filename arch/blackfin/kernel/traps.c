/*
 * File:         arch/blackfin/kernel/traps.c
 * Based on:
 * Author:       Hamish Macdonald
 *
 * Created:
 * Description:  uses S/W interrupt 15 for the system calls
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>
#include <asm/cplb.h>
#include <asm/blackfin.h>
#include <asm/irq_handler.h>
#include <linux/irq.h>
#include <asm/trace.h>
#include <asm/fixed_code.h>

#ifdef CONFIG_KGDB
# include <linux/kgdb.h>

# define CHK_DEBUGGER_TRAP() \
	do { \
		kgdb_handle_exception(trapnr, sig, info.si_code, fp); \
	} while (0)
# define CHK_DEBUGGER_TRAP_MAYBE() \
	do { \
		if (kgdb_connected) \
			CHK_DEBUGGER_TRAP(); \
	} while (0)
#else
# define CHK_DEBUGGER_TRAP() do { } while (0)
# define CHK_DEBUGGER_TRAP_MAYBE() do { } while (0)
#endif

/* Initiate the event table handler */
void __init trap_init(void)
{
	CSYNC();
	bfin_write_EVT3(trap);
	CSYNC();
}

/*
 * Used to save the RETX, SEQSTAT, I/D CPLB FAULT ADDR
 * values across the transition from exception to IRQ5.
 * We put these in L1, so they are going to be in a valid
 * location during exception context
 */
__attribute__((l1_data))
unsigned long saved_retx, saved_seqstat,
	saved_icplb_fault_addr, saved_dcplb_fault_addr;

static void decode_address(char *buf, unsigned long address)
{
	struct vm_list_struct *vml;
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long flags, offset;
	unsigned char in_atomic = (bfin_read_IPEND() & 0x10) || in_atomic();

#ifdef CONFIG_KALLSYMS
	unsigned long symsize;
	const char *symname;
	char *modname;
	char *delim = ":";
	char namebuf[128];

	/* look up the address and see if we are in kernel space */
	symname = kallsyms_lookup(address, &symsize, &offset, &modname, namebuf);

	if (symname) {
		/* yeah! kernel space! */
		if (!modname)
			modname = delim = "";
		sprintf(buf, "<0x%p> { %s%s%s%s + 0x%lx }",
		              (void *)address, delim, modname, delim, symname,
		              (unsigned long)offset);
		return;

	}
#endif

	/* Problem in fixed code section? */
	if (address >= FIXED_CODE_START && address < FIXED_CODE_END) {
		sprintf(buf, "<0x%p> /* Maybe fixed code section */", (void *)address);
		return;
	}

	/* Problem somewhere before the kernel start address */
	if (address < CONFIG_BOOT_LOAD) {
		sprintf(buf, "<0x%p> /* Maybe null pointer? */", (void *)address);
		return;
	}

	/* looks like we're off in user-land, so let's walk all the
	 * mappings of all our processes and see if we can't be a whee
	 * bit more specific
	 */
	write_lock_irqsave(&tasklist_lock, flags);
	for_each_process(p) {
		mm = (in_atomic ? p->mm : get_task_mm(p));
		if (!mm)
			continue;

		vml = mm->context.vmlist;
		while (vml) {
			struct vm_area_struct *vma = vml->vma;

			if (address >= vma->vm_start && address < vma->vm_end) {
				char _tmpbuf[256];
				char *name = p->comm;
				struct file *file = vma->vm_file;

				if (file)
					name = d_path(&file->f_path, _tmpbuf,
						      sizeof(_tmpbuf));

				/* FLAT does not have its text aligned to the start of
				 * the map while FDPIC ELF does ...
				 */

				/* before we can check flat/fdpic, we need to
				 * make sure current is valid
				 */
				if ((unsigned long)current >= FIXED_CODE_START &&
				    !((unsigned long)current & 0x3)) {
					if (current->mm &&
					    (address > current->mm->start_code) &&
					    (address < current->mm->end_code))
						offset = address - current->mm->start_code;
					else
						offset = (address - vma->vm_start) +
							 (vma->vm_pgoff << PAGE_SHIFT);

					sprintf(buf, "<0x%p> [ %s + 0x%lx ]",
						(void *)address, name, offset);
				} else
					sprintf(buf, "<0x%p> [ %s vma:0x%lx-0x%lx]",
						(void *)address, name,
						vma->vm_start, vma->vm_end);

				if (!in_atomic)
					mmput(mm);

				if (!strlen(buf))
					sprintf(buf, "<0x%p> [ %s ] dynamic memory", (void *)address, name);

				goto done;
			}

			vml = vml->next;
		}
		if (!in_atomic)
			mmput(mm);
	}

	/* we were unable to find this address anywhere */
	sprintf(buf, "<0x%p> /* kernel dynamic memory */", (void *)address);

done:
	write_unlock_irqrestore(&tasklist_lock, flags);
}

asmlinkage void double_fault_c(struct pt_regs *fp)
{
	console_verbose();
	oops_in_progress = 1;
	printk(KERN_EMERG "\n" KERN_EMERG "Double Fault\n");
#ifdef CONFIG_DEBUG_DOUBLEFAULT_PRINT
	if (((long)fp->seqstat &  SEQSTAT_EXCAUSE) == VEC_UNCOV) {
		char buf[150];
		decode_address(buf, saved_retx);
		printk(KERN_EMERG "While handling exception (EXCAUSE = 0x%x) at %s:\n",
			(int)saved_seqstat & SEQSTAT_EXCAUSE, buf);
		decode_address(buf, saved_dcplb_fault_addr);
		printk(KERN_NOTICE "   DCPLB_FAULT_ADDR: %s\n", buf);
		decode_address(buf, saved_icplb_fault_addr);
		printk(KERN_NOTICE "   ICPLB_FAULT_ADDR: %s\n", buf);

		decode_address(buf, fp->retx);
		printk(KERN_NOTICE "The instruction at %s caused a double exception\n",
			buf);
	} else
#endif
	{
		dump_bfin_process(fp);
		dump_bfin_mem(fp);
		show_regs(fp);
	}
	panic("Double Fault - unrecoverable event\n");

}

asmlinkage void trap_c(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int j;
#endif
	int sig = 0;
	siginfo_t info;
	unsigned long trapnr = fp->seqstat & SEQSTAT_EXCAUSE;

	trace_buffer_save(j);

	/* Important - be very careful dereferncing pointers - will lead to
	 * double faults if the stack has become corrupt
	 */

	/* If the fault was caused by a kernel thread, or interrupt handler
	 * we will kernel panic, so the system reboots.
	 * If KGDB is enabled, don't set this for kernel breakpoints
	*/

	/* TODO: check to see if we are in some sort of deferred HWERR
	 * that we should be able to recover from, not kernel panic
	 */
	if ((bfin_read_IPEND() & 0xFFC0) && (trapnr != VEC_STEP)
#ifdef CONFIG_KGDB
		&& (trapnr != VEC_EXCPT02)
#endif
	){
		console_verbose();
		oops_in_progress = 1;
	} else if (current) {
		if (current->mm == NULL) {
			console_verbose();
			oops_in_progress = 1;
		}
	}

	/* trap_c() will be called for exceptions. During exceptions
	 * processing, the pc value should be set with retx value.
	 * With this change we can cleanup some code in signal.c- TODO
	 */
	fp->orig_pc = fp->retx;
	/* printk("exception: 0x%x, ipend=%x, reti=%x, retx=%x\n",
		trapnr, fp->ipend, fp->pc, fp->retx); */

	/* send the appropriate signal to the user program */
	switch (trapnr) {

	/* This table works in conjuction with the one in ./mach-common/entry.S
	 * Some exceptions are handled there (in assembly, in exception space)
	 * Some are handled here, (in C, in interrupt space)
	 * Some, like CPLB, are handled in both, where the normal path is
	 * handled in assembly/exception space, and the error path is handled
	 * here
	 */

	/* 0x00 - Linux Syscall, getting here is an error */
	/* 0x01 - userspace gdb breakpoint, handled here */
	case VEC_EXCPT01:
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a breakpoint in kernel space */
		if (fp->ipend & 0xffc0)
			return;
		else
			break;
#ifdef CONFIG_KGDB
	case VEC_EXCPT02 :		 /* gdb connection */
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP();
		return;
#else
	/* 0x02 - User Defined, Caught by default */
#endif
	/* 0x03 - User Defined, userspace stack overflow */
	case VEC_EXCPT03:
		info.si_code = SEGV_STACKFLOW;
		sig = SIGSEGV;
		printk(KERN_NOTICE EXC_0x03(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x04 - User Defined */
	/* 0x05 - User Defined */
	/* 0x06 - User Defined */
	/* 0x07 - User Defined */
	/* 0x08 - User Defined */
	/* 0x09 - User Defined */
	/* 0x0A - User Defined */
	/* 0x0B - User Defined */
	/* 0x0C - User Defined */
	/* 0x0D - User Defined */
	/* 0x0E - User Defined */
	/* 0x0F - User Defined */
	/*
	 * If we got here, it is most likely that someone was trying to use a
	 * custom exception handler, and it is not actually installed properly
	 */
	case VEC_EXCPT02:
	case VEC_EXCPT04 ... VEC_EXCPT15:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x04(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x10 HW Single step, handled here */
	case VEC_STEP:
		info.si_code = TRAP_STEP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a single step in kernel space */
		if (fp->ipend & 0xffc0)
			return;
		else
			break;
	/* 0x11 - Trace Buffer Full, handled here */
	case VEC_OVFLOW:
		info.si_code = TRAP_TRACEFLOW;
		sig = SIGTRAP;
		printk(KERN_NOTICE EXC_0x11(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x12 - Reserved, Caught by default */
	/* 0x13 - Reserved, Caught by default */
	/* 0x14 - Reserved, Caught by default */
	/* 0x15 - Reserved, Caught by default */
	/* 0x16 - Reserved, Caught by default */
	/* 0x17 - Reserved, Caught by default */
	/* 0x18 - Reserved, Caught by default */
	/* 0x19 - Reserved, Caught by default */
	/* 0x1A - Reserved, Caught by default */
	/* 0x1B - Reserved, Caught by default */
	/* 0x1C - Reserved, Caught by default */
	/* 0x1D - Reserved, Caught by default */
	/* 0x1E - Reserved, Caught by default */
	/* 0x1F - Reserved, Caught by default */
	/* 0x20 - Reserved, Caught by default */
	/* 0x21 - Undefined Instruction, handled here */
	case VEC_UNDEF_I:
		info.si_code = ILL_ILLOPC;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x21(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x22 - Illegal Instruction Combination, handled here */
	case VEC_ILGAL_I:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x22(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x23 - Data CPLB protection violation, handled here */
	case VEC_CPLB_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x23(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x24 - Data access misaligned, handled here */
	case VEC_MISALI_D:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x24(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x25 - Unrecoverable Event, handled here */
	case VEC_UNCOV:
		info.si_code = ILL_ILLEXCPT;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x25(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x26 - Data CPLB Miss, normal case is handled in _cplb_hdr,
		error case is handled here */
	case VEC_CPLB_M:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x26(KERN_NOTICE));
		break;
	/* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero, handled here */
	case VEC_CPLB_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
		sig = SIGSEGV;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		if (saved_dcplb_fault_addr < FIXED_CODE_START)
			printk(KERN_NOTICE "NULL pointer access\n");
		else
#endif
			printk(KERN_NOTICE EXC_0x27(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x28 - Emulation Watchpoint, handled here */
	case VEC_WATCH:
		info.si_code = TRAP_WATCHPT;
		sig = SIGTRAP;
		pr_debug(EXC_0x28(KERN_DEBUG));
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a watchpoint in kernel space */
		if (fp->ipend & 0xffc0)
			return;
		else
			break;
#ifdef CONFIG_BF535
	/* 0x29 - Instruction fetch access error (535 only) */
	case VEC_ISTRU_VL:      /* ADSP-BF535 only (MH) */
		info.si_code = BUS_OPFETCH;
		sig = SIGBUS;
		printk(KERN_NOTICE "BF535: VEC_ISTRU_VL\n");
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
#else
	/* 0x29 - Reserved, Caught by default */
#endif
	/* 0x2A - Instruction fetch misaligned, handled here */
	case VEC_MISALI_I:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2A(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2B - Instruction CPLB protection violation, handled here */
	case VEC_CPLB_I_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2B(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2C - Instruction CPLB miss, handled in _cplb_hdr */
	case VEC_CPLB_I_M:
		info.si_code = ILL_CPLB_MISS;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2C(KERN_NOTICE));
		break;
	/* 0x2D - Instruction CPLB Multiple Hits, handled here */
	case VEC_CPLB_I_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
		sig = SIGSEGV;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		if (saved_icplb_fault_addr < FIXED_CODE_START)
			printk(KERN_NOTICE "Jump to NULL address\n");
		else
#endif
			printk(KERN_NOTICE EXC_0x2D(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2E - Illegal use of Supervisor Resource, handled here */
	case VEC_ILL_RES:
		info.si_code = ILL_PRVOPC;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x2E(KERN_NOTICE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2F - Reserved, Caught by default */
	/* 0x30 - Reserved, Caught by default */
	/* 0x31 - Reserved, Caught by default */
	/* 0x32 - Reserved, Caught by default */
	/* 0x33 - Reserved, Caught by default */
	/* 0x34 - Reserved, Caught by default */
	/* 0x35 - Reserved, Caught by default */
	/* 0x36 - Reserved, Caught by default */
	/* 0x37 - Reserved, Caught by default */
	/* 0x38 - Reserved, Caught by default */
	/* 0x39 - Reserved, Caught by default */
	/* 0x3A - Reserved, Caught by default */
	/* 0x3B - Reserved, Caught by default */
	/* 0x3C - Reserved, Caught by default */
	/* 0x3D - Reserved, Caught by default */
	/* 0x3E - Reserved, Caught by default */
	/* 0x3F - Reserved, Caught by default */
	case VEC_HWERR:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		switch (fp->seqstat & SEQSTAT_HWERRCAUSE) {
		/* System MMR Error */
		case (SEQSTAT_HWERRCAUSE_SYSTEM_MMR):
			info.si_code = BUS_ADRALN;
			sig = SIGBUS;
			printk(KERN_NOTICE HWC_x2(KERN_NOTICE));
			break;
		/* External Memory Addressing Error */
		case (SEQSTAT_HWERRCAUSE_EXTERN_ADDR):
			info.si_code = BUS_ADRERR;
			sig = SIGBUS;
			printk(KERN_NOTICE HWC_x3(KERN_NOTICE));
			break;
		/* Performance Monitor Overflow */
		case (SEQSTAT_HWERRCAUSE_PERF_FLOW):
			printk(KERN_NOTICE HWC_x12(KERN_NOTICE));
			break;
		/* RAISE 5 instruction */
		case (SEQSTAT_HWERRCAUSE_RAISE_5):
			printk(KERN_NOTICE HWC_x18(KERN_NOTICE));
			break;
		default:        /* Reserved */
			printk(KERN_NOTICE HWC_default(KERN_NOTICE));
			break;
		}
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/*
	 * We should be handling all known exception types above,
	 * if we get here we hit a reserved one, so panic
	 */
	default:
		oops_in_progress = 1;
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		printk(KERN_EMERG "Caught Unhandled Exception, code = %08lx\n",
			(fp->seqstat & SEQSTAT_EXCAUSE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	}

	BUG_ON(sig == 0);

	if (sig != SIGTRAP) {
		unsigned long *stack;
		dump_bfin_process(fp);
		dump_bfin_mem(fp);
		show_regs(fp);

		/* Print out the trace buffer if it makes sense */
#ifndef CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE
		if (trapnr == VEC_CPLB_I_M || trapnr == VEC_CPLB_M)
			printk(KERN_NOTICE "No trace since you do not have "
				"CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE enabled\n"
				KERN_NOTICE "\n");
		else
#endif
			dump_bfin_trace_buffer();

		if (oops_in_progress) {
			/* Dump the current kernel stack */
			printk(KERN_NOTICE "\n" KERN_NOTICE "Kernel Stack\n");
			show_stack(current, NULL);

			print_modules();
#ifndef CONFIG_ACCESS_CHECK
			printk(KERN_EMERG "Please turn on "
			       "CONFIG_ACCESS_CHECK\n");
#endif
			panic("Kernel exception");
		} else {
			/* Dump the user space stack */
			stack = (unsigned long *)rdusp();
			printk(KERN_NOTICE "Userspace Stack\n");
			show_stack(NULL, stack);
		}
	}

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_addr = (void __user *)fp->pc;
	force_sig_info(sig, &info, current);

	trace_buffer_restore(j);
	return;
}

/* Typical exception handling routines	*/

#define EXPAND_LEN ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 256 - 1)

/*
 * Similar to get_user, do some address checking, then dereference
 * Return true on sucess, false on bad address
 */
bool get_instruction(unsigned short *val, unsigned short *address)
{

	unsigned long addr;

	addr = (unsigned long)address;

	/* Check for odd addresses */
	if (addr & 0x1)
		return false;

	/* Check that things do not wrap around */
	if (addr > (addr + 2))
		return false;

	/*
	 * Since we are in exception context, we need to do a little address checking
	 * We need to make sure we are only accessing valid memory, and
	 * we don't read something in the async space that can hang forever
	 */
	if ((addr >= FIXED_CODE_START && (addr + 2) <= physical_mem_end) ||
#if L2_LENGTH != 0
	    (addr >= L2_START && (addr + 2) <= (L2_START + L2_LENGTH)) ||
#endif
	    (addr >= BOOT_ROM_START && (addr + 2) <= (BOOT_ROM_START + BOOT_ROM_LENGTH)) ||
#if L1_DATA_A_LENGTH != 0
	    (addr >= L1_DATA_A_START && (addr + 2) <= (L1_DATA_A_START + L1_DATA_A_LENGTH)) ||
#endif
#if L1_DATA_B_LENGTH != 0
	    (addr >= L1_DATA_B_START && (addr + 2) <= (L1_DATA_B_START + L1_DATA_B_LENGTH)) ||
#endif
	    (addr >= L1_SCRATCH_START && (addr + 2) <= (L1_SCRATCH_START + L1_SCRATCH_LENGTH)) ||
	    (!(bfin_read_EBIU_AMBCTL0() & B0RDYEN) &&
	       addr >= ASYNC_BANK0_BASE && (addr + 2) <= (ASYNC_BANK0_BASE + ASYNC_BANK0_SIZE)) ||
	    (!(bfin_read_EBIU_AMBCTL0() & B1RDYEN) &&
	       addr >= ASYNC_BANK1_BASE && (addr + 2) <= (ASYNC_BANK1_BASE + ASYNC_BANK1_SIZE)) ||
	    (!(bfin_read_EBIU_AMBCTL1() & B2RDYEN) &&
	       addr >= ASYNC_BANK2_BASE && (addr + 2) <= (ASYNC_BANK2_BASE + ASYNC_BANK1_SIZE)) ||
	    (!(bfin_read_EBIU_AMBCTL1() & B3RDYEN) &&
	      addr >= ASYNC_BANK3_BASE && (addr + 2) <= (ASYNC_BANK3_BASE + ASYNC_BANK1_SIZE))) {
		*val = *address;
		return true;
	}

#if L1_CODE_LENGTH != 0
	if (addr >= L1_CODE_START && (addr + 2) <= (L1_CODE_START + L1_CODE_LENGTH)) {
		isram_memcpy(val, address, 2);
		return true;
	}
#endif


	return false;
}

/* 
 * decode the instruction if we are printing out the trace, as it
 * makes things easier to follow, without running it through objdump
 * These are the normal instructions which cause change of flow, which
 * would be at the source of the trace buffer
 */
void decode_instruction(unsigned short *address)
{
	unsigned short opcode;

	if (get_instruction(&opcode, address)) {
		if (opcode == 0x0010)
			printk("RTS");
		else if (opcode == 0x0011)
			printk("RTI");
		else if (opcode == 0x0012)
			printk("RTX");
		else if (opcode >= 0x0050 && opcode <= 0x0057)
			printk("JUMP (P%i)", opcode & 7);
		else if (opcode >= 0x0060 && opcode <= 0x0067)
			printk("CALL (P%i)", opcode & 7);
		else if (opcode >= 0x0070 && opcode <= 0x0077)
			printk("CALL (PC+P%i)", opcode & 7);
		else if (opcode >= 0x0080 && opcode <= 0x0087)
			printk("JUMP (PC+P%i)", opcode & 7);
		else if ((opcode >= 0x1000 && opcode <= 0x13FF) || (opcode >= 0x1800 && opcode <= 0x1BFF))
			printk("IF !CC JUMP");
		else if ((opcode >= 0x1400 && opcode <= 0x17ff) || (opcode >= 0x1c00 && opcode <= 0x1fff))
			printk("IF CC JUMP");
		else if (opcode >= 0x2000 && opcode <= 0x2fff)
			printk("JUMP.S");
		else if (opcode >= 0xe080 && opcode <= 0xe0ff)
			printk("LSETUP");
		else if (opcode >= 0xe200 && opcode <= 0xe2ff)
			printk("JUMP.L");
		else if (opcode >= 0xe300 && opcode <= 0xe3ff)
			printk("CALL pcrel");
		else
			printk("0x%04x", opcode);
	}

}

void dump_bfin_trace_buffer(void)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags, i = 0;
	char buf[150];
	unsigned short *addr;
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	int j, index;
#endif

	trace_buffer_save(tflags);

	printk(KERN_NOTICE "Hardware Trace:\n");

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	printk(KERN_NOTICE "WARNING: Expanded trace turned on - can not trace exceptions\n");
#endif

	if (likely(bfin_read_TBUFSTAT() & TBUFCNT)) {
		for (; bfin_read_TBUFSTAT() & TBUFCNT; i++) {
			decode_address(buf, (unsigned long)bfin_read_TBUF());
			printk(KERN_NOTICE "%4i Target : %s\n", i, buf);
			addr = (unsigned short *)bfin_read_TBUF();
			decode_address(buf, (unsigned long)addr);
			printk(KERN_NOTICE "     Source : %s ", buf);
			decode_instruction(addr);
			printk("\n");
		}
	}

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	if (trace_buff_offset)
		index = trace_buff_offset / 4;
	else
		index = EXPAND_LEN;

	j = (1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 128;
	while (j) {
		decode_address(buf, software_trace_buff[index]);
		printk(KERN_NOTICE "%4i Target : %s\n", i, buf);
		index -= 1;
		if (index < 0 )
			index = EXPAND_LEN;
		decode_address(buf, software_trace_buff[index]);
		printk(KERN_NOTICE "     Source : %s ", buf);
		decode_instruction((unsigned short *)software_trace_buff[index]);
		printk("\n");
		index -= 1;
		if (index < 0)
			index = EXPAND_LEN;
		j--;
		i++;
	}
#endif

	trace_buffer_restore(tflags);
#endif
}
EXPORT_SYMBOL(dump_bfin_trace_buffer);

/*
 * Checks to see if the address pointed to is either a
 * 16-bit CALL instruction, or a 32-bit CALL instruction
 */
bool is_bfin_call(unsigned short *addr)
{
	unsigned short opcode = 0, *ins_addr;
	ins_addr = (unsigned short *)addr;

	if (!get_instruction(&opcode, ins_addr))
		return false;

	if ((opcode >= 0x0060 && opcode <= 0x0067) ||
	    (opcode >= 0x0070 && opcode <= 0x0077))
		return true;

	ins_addr--;
	if (!get_instruction(&opcode, ins_addr))
		return false;

	if (opcode >= 0xE300 && opcode <= 0xE3FF)
		return true;

	return false;

}
void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned int *addr, *endstack, *fp = 0, *frame;
	unsigned short *ins_addr;
	char buf[150];
	unsigned int i, j, ret_addr, frame_no = 0;

	/*
	 * If we have been passed a specific stack, use that one otherwise
	 *    if we have been passed a task structure, use that, otherwise
	 *    use the stack of where the variable "stack" exists
	 */

	if (stack == NULL) {
		if (task) {
			/* We know this is a kernel stack, so this is the start/end */
			stack = (unsigned long *)task->thread.ksp;
			endstack = (unsigned int *)(((unsigned int)(stack) & ~(THREAD_SIZE - 1)) + THREAD_SIZE);
		} else {
			/* print out the existing stack info */
			stack = (unsigned long *)&stack;
			endstack = (unsigned int *)PAGE_ALIGN((unsigned int)stack);
		}
	} else
		endstack = (unsigned int *)PAGE_ALIGN((unsigned int)stack);

	decode_address(buf, (unsigned int)stack);
	printk(KERN_NOTICE "Stack info:\n" KERN_NOTICE " SP: [0x%p] %s\n", stack, buf);
	addr = (unsigned int *)((unsigned int)stack & ~0x3F);

	/* First thing is to look for a frame pointer */
	for (addr = (unsigned int *)((unsigned int)stack & ~0xF), i = 0;
		addr < endstack; addr++, i++) {
		if (*addr & 0x1)
			continue;
		ins_addr = (unsigned short *)*addr;
		ins_addr--;
		if (is_bfin_call(ins_addr))
			fp = addr - 1;

		if (fp) {
			/* Let's check to see if it is a frame pointer */
			while (fp >= (addr - 1) && fp < endstack && fp)
				fp = (unsigned int *)*fp;
			if (fp == 0 || fp == endstack) {
				fp = addr - 1;
				break;
			}
			fp = 0;
		}
	}
	if (fp) {
		frame = fp;
		printk(" FP: (0x%p)\n", fp);
	} else
		frame = 0;

	/*
	 * Now that we think we know where things are, we
	 * walk the stack again, this time printing things out
	 * incase there is no frame pointer, we still look for
	 * valid return addresses
	 */

	/* First time print out data, next time, print out symbols */
	for (j = 0; j <= 1; j++) {
		if (j)
			printk(KERN_NOTICE "Return addresses in stack:\n");
		else
			printk(KERN_NOTICE " Memory from 0x%08lx to %p", ((long unsigned int)stack & ~0xF), endstack);

		fp = frame;
		frame_no = 0;

		for (addr = (unsigned int *)((unsigned int)stack & ~0xF), i = 0;
		     addr <= endstack; addr++, i++) {

			ret_addr = 0;
			if (!j && i % 8 == 0)
				printk("\n" KERN_NOTICE "%p:",addr);

			/* if it is an odd address, or zero, just skip it */
			if (*addr & 0x1 || !*addr)
				goto print;

			ins_addr = (unsigned short *)*addr;

			/* Go back one instruction, and see if it is a CALL */
			ins_addr--;
			ret_addr = is_bfin_call(ins_addr);
 print:
			if (!j && stack == (unsigned long *)addr)
				printk("[%08x]", *addr);
			else if (ret_addr)
				if (j) {
					decode_address(buf, (unsigned int)*addr);
					if (frame == addr) {
						printk(KERN_NOTICE "   frame %2i : %s\n", frame_no, buf);
						continue;
					}
					printk(KERN_NOTICE "    address : %s\n", buf);
				} else
					printk("<%08x>", *addr);
			else if (fp == addr) {
				if (j)
					frame = addr+1;
				else
					printk("(%08x)", *addr);

				fp = (unsigned int *)*addr;
				frame_no++;

			} else if (!j)
				printk(" %08x ", *addr);
		}
		if (!j)
			printk("\n");
	}

}

void dump_stack(void)
{
	unsigned long stack;
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags;
#endif
	trace_buffer_save(tflags);
	dump_bfin_trace_buffer();
	show_stack(current, &stack);
	trace_buffer_restore(tflags);
}
EXPORT_SYMBOL(dump_stack);

void dump_bfin_process(struct pt_regs *fp)
{
	/* We should be able to look at fp->ipend, but we don't push it on the
	 * stack all the time, so do this until we fix that */
	unsigned int context = bfin_read_IPEND();

	if (oops_in_progress)
		printk(KERN_EMERG "Kernel OOPS in progress\n");

	if (context & 0x0020 && (fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR)
		printk(KERN_NOTICE "HW Error context\n");
	else if (context & 0x0020)
		printk(KERN_NOTICE "Deferred Exception context\n");
	else if (context & 0x3FC0)
		printk(KERN_NOTICE "Interrupt context\n");
	else if (context & 0x4000)
		printk(KERN_NOTICE "Deferred Interrupt context\n");
	else if (context & 0x8000)
		printk(KERN_NOTICE "Kernel process context\n");

	/* Because we are crashing, and pointers could be bad, we check things
	 * pretty closely before we use them
	 */
	if ((unsigned long)current >= FIXED_CODE_START &&
	    !((unsigned long)current & 0x3) && current->pid) {
		printk(KERN_NOTICE "CURRENT PROCESS:\n");
		if (current->comm >= (char *)FIXED_CODE_START)
			printk(KERN_NOTICE "COMM=%s PID=%d\n",
				current->comm, current->pid);
		else
			printk(KERN_NOTICE "COMM= invalid\n");

		if (!((unsigned long)current->mm & 0x3) && (unsigned long)current->mm >= FIXED_CODE_START)
			printk(KERN_NOTICE  "TEXT = 0x%p-0x%p        DATA = 0x%p-0x%p\n"
				KERN_NOTICE " BSS = 0x%p-0x%p  USER-STACK = 0x%p\n"
				KERN_NOTICE "\n",
				(void *)current->mm->start_code,
				(void *)current->mm->end_code,
				(void *)current->mm->start_data,
				(void *)current->mm->end_data,
				(void *)current->mm->end_data,
				(void *)current->mm->brk,
				(void *)current->mm->start_stack);
		else
			printk(KERN_NOTICE "invalid mm\n");
	} else
		printk(KERN_NOTICE "\n" KERN_NOTICE
		     "No Valid process in current context\n");
}

void dump_bfin_mem(struct pt_regs *fp)
{
	unsigned short *addr, *erraddr, val = 0, err = 0;
	char sti = 0, buf[6];

	erraddr = (void *)fp->pc;

	printk(KERN_NOTICE "return address: [0x%p]; contents of:", erraddr);

	for (addr = (unsigned short *)((unsigned long)erraddr & ~0xF) - 0x10;
	     addr < (unsigned short *)((unsigned long)erraddr & ~0xF) + 0x10;
	     addr++) {
		if (!((unsigned long)addr & 0xF))
			printk("\n" KERN_NOTICE "0x%p: ", addr);

		if (!get_instruction(&val, addr)) {
				val = 0;
				sprintf(buf, "????");
		} else
			sprintf(buf, "%04x", val);

		if (addr == erraddr) {
			printk("[%s]", buf);
			err = val;
		} else
			printk(" %s ", buf);

		/* Do any previous instructions turn on interrupts? */
		if (addr <= erraddr &&				/* in the past */
		    ((val >= 0x0040 && val <= 0x0047) ||	/* STI instruction */
		      val == 0x017b))				/* [SP++] = RETI */
			sti = 1;
	}

	printk("\n");

	/* Hardware error interrupts can be deferred */
	if (unlikely(sti && (fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR &&
	    oops_in_progress)){
		printk(KERN_NOTICE "Looks like this was a deferred error - sorry\n");
#ifndef CONFIG_DEBUG_HWERR
		printk(KERN_NOTICE "The remaining message may be meaningless\n"
			KERN_NOTICE "You should enable CONFIG_DEBUG_HWERR to get a"
			 " better idea where it came from\n");
#else
		/* If we are handling only one peripheral interrupt
		 * and current mm and pid are valid, and the last error
		 * was in that user space process's text area
		 * print it out - because that is where the problem exists
		 */
		if ((!(((fp)->ipend & ~0x30) & (((fp)->ipend & ~0x30) - 1))) &&
		     (current->pid && current->mm)) {
			/* And the last RETI points to the current userspace context */
			if ((fp + 1)->pc >= current->mm->start_code &&
			    (fp + 1)->pc <= current->mm->end_code) {
				printk(KERN_NOTICE "It might be better to look around here : \n");
				printk(KERN_NOTICE "-------------------------------------------\n");
				show_regs(fp + 1);
				printk(KERN_NOTICE "-------------------------------------------\n");
			}
		}
#endif
	}
}

void show_regs(struct pt_regs *fp)
{
	char buf [150];
	struct irqaction *action;
	unsigned int i;
	unsigned long flags;

	printk(KERN_NOTICE "\n" KERN_NOTICE "SEQUENCER STATUS:\t\t%s\n", print_tainted());
	printk(KERN_NOTICE " SEQSTAT: %08lx  IPEND: %04lx  SYSCFG: %04lx\n",
		(long)fp->seqstat, fp->ipend, fp->syscfg);
	printk(KERN_NOTICE "  HWERRCAUSE: 0x%lx\n",
		(fp->seqstat & SEQSTAT_HWERRCAUSE) >> 14);
	printk(KERN_NOTICE "  EXCAUSE   : 0x%lx\n",
		fp->seqstat & SEQSTAT_EXCAUSE);
	for (i = 6; i <= 15 ; i++) {
		if (fp->ipend & (1 << i)) {
			decode_address(buf, bfin_read32(EVT0 + 4*i));
			printk(KERN_NOTICE "  physical IVG%i asserted : %s\n", i, buf);
		}
	}

	/* if no interrupts are going off, don't print this out */
	if (fp->ipend & ~0x3F) {
		for (i = 0; i < (NR_IRQS - 1); i++) {
			spin_lock_irqsave(&irq_desc[i].lock, flags);
			action = irq_desc[i].action;
			if (!action)
				goto unlock;

			decode_address(buf, (unsigned int)action->handler);
			printk(KERN_NOTICE "  logical irq %3d mapped  : %s", i, buf);
			for (action = action->next; action; action = action->next) {
				decode_address(buf, (unsigned int)action->handler);
				printk(", %s", buf);
			}
			printk("\n");
unlock:
			spin_unlock_irqrestore(&irq_desc[i].lock, flags);
		}
	}

	decode_address(buf, fp->rete);
	printk(KERN_NOTICE " RETE: %s\n", buf);
	decode_address(buf, fp->retn);
	printk(KERN_NOTICE " RETN: %s\n", buf);
	decode_address(buf, fp->retx);
	printk(KERN_NOTICE " RETX: %s\n", buf);
	decode_address(buf, fp->rets);
	printk(KERN_NOTICE " RETS: %s\n", buf);
	decode_address(buf, fp->pc);
	printk(KERN_NOTICE " PC  : %s\n", buf);

	if (((long)fp->seqstat &  SEQSTAT_EXCAUSE) &&
	    (((long)fp->seqstat & SEQSTAT_EXCAUSE) != VEC_HWERR)) {
		decode_address(buf, saved_dcplb_fault_addr);
		printk(KERN_NOTICE "DCPLB_FAULT_ADDR: %s\n", buf);
		decode_address(buf, saved_icplb_fault_addr);
		printk(KERN_NOTICE "ICPLB_FAULT_ADDR: %s\n", buf);
	}

	printk(KERN_NOTICE "\n" KERN_NOTICE "PROCESSOR STATE:\n");
	printk(KERN_NOTICE " R0 : %08lx    R1 : %08lx    R2 : %08lx    R3 : %08lx\n",
		fp->r0, fp->r1, fp->r2, fp->r3);
	printk(KERN_NOTICE " R4 : %08lx    R5 : %08lx    R6 : %08lx    R7 : %08lx\n",
		fp->r4, fp->r5, fp->r6, fp->r7);
	printk(KERN_NOTICE " P0 : %08lx    P1 : %08lx    P2 : %08lx    P3 : %08lx\n",
		fp->p0, fp->p1, fp->p2, fp->p3);
	printk(KERN_NOTICE " P4 : %08lx    P5 : %08lx    FP : %08lx    SP : %08lx\n",
		fp->p4, fp->p5, fp->fp, (long)fp);
	printk(KERN_NOTICE " LB0: %08lx    LT0: %08lx    LC0: %08lx\n",
		fp->lb0, fp->lt0, fp->lc0);
	printk(KERN_NOTICE " LB1: %08lx    LT1: %08lx    LC1: %08lx\n",
		fp->lb1, fp->lt1, fp->lc1);
	printk(KERN_NOTICE " B0 : %08lx    L0 : %08lx    M0 : %08lx    I0 : %08lx\n",
		fp->b0, fp->l0, fp->m0, fp->i0);
	printk(KERN_NOTICE " B1 : %08lx    L1 : %08lx    M1 : %08lx    I1 : %08lx\n",
		fp->b1, fp->l1, fp->m1, fp->i1);
	printk(KERN_NOTICE " B2 : %08lx    L2 : %08lx    M2 : %08lx    I2 : %08lx\n",
		fp->b2, fp->l2, fp->m2, fp->i2);
	printk(KERN_NOTICE " B3 : %08lx    L3 : %08lx    M3 : %08lx    I3 : %08lx\n",
		fp->b3, fp->l3, fp->m3, fp->i3);
	printk(KERN_NOTICE "A0.w: %08lx   A0.x: %08lx   A1.w: %08lx   A1.x: %08lx\n",
		fp->a0w, fp->a0x, fp->a1w, fp->a1x);

	printk(KERN_NOTICE "USP : %08lx  ASTAT: %08lx\n",
		rdusp(), fp->astat);

	printk(KERN_NOTICE "\n");
}

#ifdef CONFIG_SYS_BFIN_SPINLOCK_L1
asmlinkage int sys_bfin_spinlock(int *spinlock)__attribute__((l1_text));
#endif

asmlinkage int sys_bfin_spinlock(int *spinlock)
{
	int ret = 0;
	int tmp = 0;

	local_irq_disable();
	ret = get_user(tmp, spinlock);
	if (ret == 0) {
		if (tmp)
			ret = 1;
		tmp = 1;
		put_user(tmp, spinlock);
	}
	local_irq_enable();
	return ret;
}

int bfin_request_exception(unsigned int exception, void (*handler)(void))
{
	void (*curr_handler)(void);

	if (exception > 0x3F)
		return -EINVAL;

	curr_handler = ex_table[exception];

	if (curr_handler != ex_replaceable)
		return -EBUSY;

	ex_table[exception] = handler;

	return 0;
}
EXPORT_SYMBOL(bfin_request_exception);

int bfin_free_exception(unsigned int exception, void (*handler)(void))
{
	void (*curr_handler)(void);

	if (exception > 0x3F)
		return -EINVAL;

	curr_handler = ex_table[exception];

	if (curr_handler != handler)
		return -EBUSY;

	ex_table[exception] = ex_replaceable;

	return 0;
}
EXPORT_SYMBOL(bfin_free_exception);

void panic_cplb_error(int cplb_panic, struct pt_regs *fp)
{
	switch (cplb_panic) {
	case CPLB_NO_UNLOCKED:
		printk(KERN_EMERG "All CPLBs are locked\n");
		break;
	case CPLB_PROT_VIOL:
		return;
	case CPLB_NO_ADDR_MATCH:
		return;
	case CPLB_UNKNOWN_ERR:
		printk(KERN_EMERG "Unknown CPLB Exception\n");
		break;
	}

	oops_in_progress = 1;

	dump_bfin_process(fp);
	dump_bfin_mem(fp);
	show_regs(fp);
	dump_stack();
	panic("Unrecoverable event\n");
}
