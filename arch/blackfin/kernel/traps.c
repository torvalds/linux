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
#include <asm/blackfin.h>
#include <asm/irq_handler.h>
#include <asm/trace.h>

#ifdef CONFIG_KGDB
# include <linux/debugger.h>
# include <linux/kgdb.h>
#endif

/* Initiate the event table handler */
void __init trap_init(void)
{
	CSYNC();
	bfin_write_EVT3(trap);
	CSYNC();
}

int kstack_depth_to_print = 48;

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
static int printk_address(unsigned long address)
{
	struct vm_list_struct *vml;
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long offset;

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
		return printk("<0x%p> { %s%s%s%s + 0x%lx }",
		              (void *)address, delim, modname, delim, symname,
		              (unsigned long)offset);

	}
#endif

	/* looks like we're off in user-land, so let's walk all the
	 * mappings of all our processes and see if we can't be a whee
	 * bit more specific
	 */
	write_lock_irq(&tasklist_lock);
	for_each_process(p) {
		mm = get_task_mm(p);
		if (!mm)
			continue;

		vml = mm->context.vmlist;
		while (vml) {
			struct vm_area_struct *vma = vml->vma;

			if (address >= vma->vm_start && address < vma->vm_end) {
				char *name = p->comm;
				struct file *file = vma->vm_file;
				if (file) {
					char _tmpbuf[256];
					name = d_path(file->f_dentry,
					              file->f_vfsmnt,
					              _tmpbuf,
					              sizeof(_tmpbuf));
				}

				/* FLAT does not have its text aligned to the start of
				 * the map while FDPIC ELF does ...
				 */
				if (current->mm &&
				    (address > current->mm->start_code) &&
				    (address < current->mm->end_code))
					offset = address - current->mm->start_code;
				else
					offset = (address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT);

				write_unlock_irq(&tasklist_lock);
				return printk("<0x%p> [ %s + 0x%lx ]",
				              (void *)address, name, offset);
			}

			vml = vml->next;
		}
	}
	write_unlock_irq(&tasklist_lock);

	/* we were unable to find this address anywhere */
	return printk("[<0x%p>]", (void *)address);
}
#endif

asmlinkage void double_fault_c(struct pt_regs *fp)
{
	printk(KERN_EMERG "\n" KERN_EMERG "Double Fault\n");
	dump_bfin_regs(fp, (void *)fp->retx);
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

#ifdef CONFIG_KGDB
# define CHK_DEBUGGER_TRAP() \
	do { \
		CHK_DEBUGGER(trapnr, sig, info.si_code, fp, ); \
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

	trace_buffer_save(j);

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
		printk(KERN_EMERG EXC_0x03);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x04 - User Defined, Caught by default */
	/* 0x05 - User Defined, Caught by default */
	/* 0x06 - User Defined, Caught by default */
	/* 0x07 - User Defined, Caught by default */
	/* 0x08 - User Defined, Caught by default */
	/* 0x09 - User Defined, Caught by default */
	/* 0x0A - User Defined, Caught by default */
	/* 0x0B - User Defined, Caught by default */
	/* 0x0C - User Defined, Caught by default */
	/* 0x0D - User Defined, Caught by default */
	/* 0x0E - User Defined, Caught by default */
	/* 0x0F - User Defined, Caught by default */
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
		printk(KERN_EMERG EXC_0x11);
		CHK_DEBUGGER_TRAP();
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
		printk(KERN_EMERG EXC_0x21);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x22 - Illegal Instruction Combination, handled here */
	case VEC_ILGAL_I:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x22);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x23 - Data CPLB Protection Violation,
		 normal case is handled in _cplb_hdr */
	case VEC_CPLB_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x23);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x24 - Data access misaligned, handled here */
	case VEC_MISALI_D:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_EMERG EXC_0x24);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x25 - Unrecoverable Event, handled here */
	case VEC_UNCOV:
		info.si_code = ILL_ILLEXCPT;
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x25);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x26 - Data CPLB Miss, normal case is handled in _cplb_hdr,
		error case is handled here */
	case VEC_CPLB_M:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_EMERG EXC_0x26);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero, handled here */
	case VEC_CPLB_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		sig = SIGSEGV;
		printk(KERN_EMERG "\n"
			KERN_EMERG "NULL pointer access (probably)\n");
#else
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x27);
#endif
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x28 - Emulation Watchpoint, handled here */
	case VEC_WATCH:
		info.si_code = TRAP_WATCHPT;
		sig = SIGTRAP;
		pr_debug(EXC_0x28);
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
		printk(KERN_EMERG "BF535: VEC_ISTRU_VL\n");
		CHK_DEBUGGER_TRAP();
		break;
#else
	/* 0x29 - Reserved, Caught by default */
#endif
	/* 0x2A - Instruction fetch misaligned, handled here */
	case VEC_MISALI_I:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_EMERG EXC_0x2A);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2B - Instruction CPLB protection Violation,
		handled in _cplb_hdr */
	case VEC_CPLB_I_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x2B);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2C - Instruction CPLB miss, handled in _cplb_hdr */
	case VEC_CPLB_I_M:
		info.si_code = ILL_CPLB_MISS;
		sig = SIGBUS;
		printk(KERN_EMERG EXC_0x2C);
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2D - Instruction CPLB Multiple Hits, handled here */
	case VEC_CPLB_I_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		sig = SIGSEGV;
		printk(KERN_EMERG "\n\nJump to address 0 - 0x0fff\n");
#else
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x2D);
#endif
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2E - Illegal use of Supervisor Resource, handled here */
	case VEC_ILL_RES:
		info.si_code = ILL_PRVOPC;
		sig = SIGILL;
		printk(KERN_EMERG EXC_0x2E);
		CHK_DEBUGGER_TRAP();
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
	default:
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		printk(KERN_EMERG "Caught Unhandled Exception, code = %08lx\n",
			(fp->seqstat & SEQSTAT_EXCAUSE));
		CHK_DEBUGGER_TRAP();
		break;
	}

	if (sig != 0 && sig != SIGTRAP) {
		unsigned long stack;
		dump_bfin_regs(fp, (void *)fp->retx);
		dump_bfin_trace_buffer();
		show_stack(current, &stack);
		if (current->mm == NULL)
			panic("Kernel exception");
	}
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_addr = (void *)fp->pc;
	force_sig_info(sig, &info, current);

	/* if the address that we are about to return to is not valid, set it
	 * to a valid address, if we have a current application or panic
	 */
	if (!(fp->pc <= physical_mem_end
#if L1_CODE_LENGTH != 0
	    || (fp->pc >= L1_CODE_START &&
	        fp->pc <= (L1_CODE_START + L1_CODE_LENGTH))
#endif
	)) {
		if (current->mm) {
			fp->pc = current->mm->start_code;
		} else {
			printk(KERN_EMERG
				"I can't return to memory that doesn't exist"
				" - bad things happen\n");
			panic("Help - I've fallen and can't get up\n");
		}
	}

	trace_buffer_restore(j);
	return;
}

/* Typical exception handling routines	*/

#define EXPAND_LEN ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 256 - 1)

void dump_bfin_trace_buffer(void)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags, i = 0;
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	int j, index;
#endif

	trace_buffer_save(tflags);

	printk(KERN_EMERG "Hardware Trace:\n");

	if (likely(bfin_read_TBUFSTAT() & TBUFCNT)) {
		for (; bfin_read_TBUFSTAT() & TBUFCNT; i++) {
			printk(KERN_EMERG "%4i Target : ", i);
			printk_address((unsigned long)bfin_read_TBUF());
			printk("\n" KERN_EMERG "     Source : ");
			printk_address((unsigned long)bfin_read_TBUF());
			printk("\n");
		}
	}

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	if (trace_buff_offset)
		index = trace_buff_offset/4 - 1;
	else
		index = EXPAND_LEN;

	j = (1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 128;
	while (j) {
		printk(KERN_EMERG "%4i Target : ", i);
		printk_address(software_trace_buff[index]);
		index -= 1;
		if (index < 0 )
			index = EXPAND_LEN;
		printk("\n" KERN_EMERG "     Source : ");
		printk_address(software_trace_buff[index]);
		index -= 1;
		if (index < 0)
			index = EXPAND_LEN;
		printk("\n");
		j--;
		i++;
	}
#endif

	trace_buffer_restore(tflags);
#endif
}
EXPORT_SYMBOL(dump_bfin_trace_buffer);

static void show_trace(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long addr;

	printk("\nCall Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif

	while (!kstack_end(sp)) {
		addr = *sp++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (kernel_text_address(addr))
			print_ip_sym(addr);
	}

	printk("\n");
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *endstack, addr;
	int i;

	/* Cannot call dump_bfin_trace_buffer() here as show_stack() is
	 * called externally in some places in the kernel.
	 */

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.ksp;
		else
			stack = (unsigned long *)&stack;
	}

	addr = (unsigned long)stack;
	endstack = (unsigned long *)PAGE_ALIGN(addr);

	printk(KERN_EMERG "Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i % 8 == 0)
			printk("\n" KERN_EMERG "       ");
		printk(" %08lx", *stack++);
	}

	show_trace(task, stack);
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

void dump_bfin_regs(struct pt_regs *fp, void *retaddr)
{
	if (current->pid) {
		printk(KERN_EMERG "\n" KERN_EMERG "CURRENT PROCESS:\n"
			KERN_EMERG "\n");
		printk(KERN_EMERG "COMM=%s PID=%d\n",
			current->comm, current->pid);
	} else {
		printk
		    (KERN_EMERG "\n" KERN_EMERG
		     "No Valid pid - Either things are really messed up,"
		     " or you are in the kernel\n");
	}

	if (current->mm) {
		printk(KERN_EMERG "TEXT = 0x%p-0x%p  DATA = 0x%p-0x%p\n"
		       KERN_EMERG "BSS = 0x%p-0x%p   USER-STACK = 0x%p\n"
		       KERN_EMERG "\n",
		       (void *)current->mm->start_code,
		       (void *)current->mm->end_code,
		       (void *)current->mm->start_data,
		       (void *)current->mm->end_data,
		       (void *)current->mm->end_data,
		       (void *)current->mm->brk,
		       (void *)current->mm->start_stack);
	}

	printk(KERN_EMERG "return address: [0x%p]; contents of:", retaddr);
	if (retaddr != 0 && retaddr <= (void *)physical_mem_end
#if L1_CODE_LENGTH != 0
	    /* FIXME: Copy the code out of L1 Instruction SRAM through dma
	       memcpy.  */
	    && !(retaddr >= (void *)L1_CODE_START
	         && retaddr < (void *)(L1_CODE_START + L1_CODE_LENGTH))
#endif
	) {
		int i = ((unsigned int)retaddr & 0xFFFFFFF0) - 32;
		unsigned short x = 0;
		for (; i < ((unsigned int)retaddr & 0xFFFFFFF0) + 32; i += 2) {
			if (!(i & 0xF))
				printk("\n" KERN_EMERG "0x%08x: ", i);

			if (get_user(x, (unsigned short *)i))
				break;
#ifndef CONFIG_DEBUG_HWERR
			/* If one of the last few instructions was a STI
			 * it is likely that the error occured awhile ago
			 * and we just noticed
			 */
			if (x >= 0x0040 && x <= 0x0047 && i <= 0)
				panic("\n\nWARNING : You should reconfigure"
					" the kernel to turn on\n"
					" 'Hardware error interrupt"
					" debugging'\n"
					" The rest of this error"
					" is meanless\n");
#endif
			if (i == (unsigned int)retaddr)
				printk("[%04x]", x);
			else
				printk(" %04x ", x);
		}
		printk("\n" KERN_EMERG "\n");
	} else
		printk(KERN_EMERG
			"Cannot look at the [PC] for it is"
			"in unreadable L1 SRAM - sorry\n");


	printk(KERN_EMERG
		"RETE:  %08lx  RETN: %08lx  RETX: %08lx  RETS: %08lx\n",
		fp->rete, fp->retn, fp->retx, fp->rets);
	printk(KERN_EMERG "IPEND: %04lx  SYSCFG: %04lx\n",
		fp->ipend, fp->syscfg);
	printk(KERN_EMERG "SEQSTAT: %08lx    SP: %08lx\n",
		(long)fp->seqstat, (long)fp);
	printk(KERN_EMERG "R0: %08lx    R1: %08lx    R2: %08lx    R3: %08lx\n",
		fp->r0, fp->r1, fp->r2, fp->r3);
	printk(KERN_EMERG "R4: %08lx    R5: %08lx    R6: %08lx    R7: %08lx\n",
		fp->r4, fp->r5, fp->r6, fp->r7);
	printk(KERN_EMERG "P0: %08lx    P1: %08lx    P2: %08lx    P3: %08lx\n",
		fp->p0, fp->p1, fp->p2, fp->p3);
	printk(KERN_EMERG
		"P4: %08lx    P5: %08lx    FP: %08lx\n",
		fp->p4, fp->p5, fp->fp);
	printk(KERN_EMERG
		"A0.w: %08lx    A0.x: %08lx    A1.w: %08lx    A1.x: %08lx\n",
		fp->a0w, fp->a0x, fp->a1w, fp->a1x);

	printk(KERN_EMERG "LB0: %08lx  LT0: %08lx  LC0: %08lx\n",
		fp->lb0, fp->lt0, fp->lc0);
	printk(KERN_EMERG "LB1: %08lx  LT1: %08lx  LC1: %08lx\n",
		fp->lb1, fp->lt1, fp->lc1);
	printk(KERN_EMERG "B0: %08lx  L0: %08lx  M0: %08lx  I0: %08lx\n",
		fp->b0, fp->l0, fp->m0, fp->i0);
	printk(KERN_EMERG "B1: %08lx  L1: %08lx  M1: %08lx  I1: %08lx\n",
		fp->b1, fp->l1, fp->m1, fp->i1);
	printk(KERN_EMERG "B2: %08lx  L2: %08lx  M2: %08lx  I2: %08lx\n",
		fp->b2, fp->l2, fp->m2, fp->i2);
	printk(KERN_EMERG "B3: %08lx  L3: %08lx  M3: %08lx  I3: %08lx\n",
		fp->b3, fp->l3, fp->m3, fp->i3);

	printk(KERN_EMERG "\n" KERN_EMERG "USP: %08lx   ASTAT: %08lx\n",
		rdusp(), fp->astat);
	if ((long)fp->seqstat & SEQSTAT_EXCAUSE) {
		printk(KERN_EMERG "DCPLB_FAULT_ADDR=%p\n",
			(void *)bfin_read_DCPLB_FAULT_ADDR());
		printk(KERN_EMERG "ICPLB_FAULT_ADDR=%p\n",
			(void *)bfin_read_ICPLB_FAULT_ADDR());
	}

	printk("\n\n");
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

	printk(KERN_EMERG "DCPLB_FAULT_ADDR=%p\n", (void *)bfin_read_DCPLB_FAULT_ADDR());
	printk(KERN_EMERG "ICPLB_FAULT_ADDR=%p\n", (void *)bfin_read_ICPLB_FAULT_ADDR());
	dump_bfin_regs(fp, (void *)fp->retx);
	dump_stack();
	panic("Unrecoverable event\n");
}
