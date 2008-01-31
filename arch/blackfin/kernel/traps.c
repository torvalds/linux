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
#include <linux/irq.h>
#include <asm/trace.h>
#include <asm/fixed_code.h>
#include <asm/dma.h>

#ifdef CONFIG_KGDB
# include <linux/debugger.h>
# include <linux/kgdb.h>

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

/* Initiate the event table handler */
void __init trap_init(void)
{
	CSYNC();
	bfin_write_EVT3(trap);
	CSYNC();
}

int kstack_depth_to_print = 48;

static void decode_address(char *buf, unsigned long address)
{
	struct vm_list_struct *vml;
	struct task_struct *p;
	struct mm_struct *mm;
	unsigned long flags, offset;
	unsigned int in_exception = bfin_read_IPEND() & 0x10;

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
		mm = (in_exception ? p->mm : get_task_mm(p));
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

				sprintf(buf, "<0x%p> [ %s + 0x%lx ]",
					(void *)address, name, offset);
				if (!in_exception)
					mmput(mm);
				goto done;
			}

			vml = vml->next;
		}
		if (!in_exception)
			mmput(mm);
	}

	/* we were unable to find this address anywhere */
	sprintf(buf, "<0x%p> /* unknown address */", (void *)address);

done:
	write_unlock_irqrestore(&tasklist_lock, flags);
}

asmlinkage void double_fault_c(struct pt_regs *fp)
{
	console_verbose();
	oops_in_progress = 1;
	printk(KERN_EMERG "\n" KERN_EMERG "Double Fault\n");
	dump_bfin_process(fp);
	dump_bfin_mem(fp);
	show_regs(fp);
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
		printk(KERN_NOTICE EXC_0x11(KERN_NOTICE));
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
		printk(KERN_NOTICE EXC_0x21(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x22 - Illegal Instruction Combination, handled here */
	case VEC_ILGAL_I:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x22(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x23 - Data CPLB protection violation, handled here */
	case VEC_CPLB_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x23(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x24 - Data access misaligned, handled here */
	case VEC_MISALI_D:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x24(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x25 - Unrecoverable Event, handled here */
	case VEC_UNCOV:
		info.si_code = ILL_ILLEXCPT;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x25(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x26 - Data CPLB Miss, normal case is handled in _cplb_hdr,
		error case is handled here */
	case VEC_CPLB_M:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x26(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero, handled here */
	case VEC_CPLB_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		sig = SIGSEGV;
		printk(KERN_NOTICE "NULL pointer access (probably)\n");
#else
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x27(KERN_NOTICE));
#endif
		CHK_DEBUGGER_TRAP();
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
		CHK_DEBUGGER_TRAP();
		break;
#else
	/* 0x29 - Reserved, Caught by default */
#endif
	/* 0x2A - Instruction fetch misaligned, handled here */
	case VEC_MISALI_I:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2A(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2B - Instruction CPLB protection violation, handled here */
	case VEC_CPLB_I_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2B(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2C - Instruction CPLB miss, handled in _cplb_hdr */
	case VEC_CPLB_I_M:
		info.si_code = ILL_CPLB_MISS;
		sig = SIGBUS;
		printk(KERN_NOTICE EXC_0x2C(KERN_NOTICE));
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2D - Instruction CPLB Multiple Hits, handled here */
	case VEC_CPLB_I_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		sig = SIGSEGV;
		printk(KERN_NOTICE "Jump to address 0 - 0x0fff\n");
#else
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x2D(KERN_NOTICE));
#endif
		CHK_DEBUGGER_TRAP();
		break;
	/* 0x2E - Illegal use of Supervisor Resource, handled here */
	case VEC_ILL_RES:
		info.si_code = ILL_PRVOPC;
		sig = SIGILL;
		printk(KERN_NOTICE EXC_0x2E(KERN_NOTICE));
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
		CHK_DEBUGGER_TRAP();
		break;
	default:
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		printk(KERN_EMERG "Caught Unhandled Exception, code = %08lx\n",
			(fp->seqstat & SEQSTAT_EXCAUSE));
		CHK_DEBUGGER_TRAP();
		break;
	}

	BUG_ON(sig == 0);

	if (sig != SIGTRAP) {
		unsigned long stack;
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
		show_stack(current, &stack);
		if (oops_in_progress) {
			print_modules();
#ifndef CONFIG_ACCESS_CHECK
			printk(KERN_EMERG "Please turn on "
			       "CONFIG_ACCESS_CHECK\n");
#endif
			panic("Kernel exception");
		}
	}

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_addr = (void *)fp->pc;
	force_sig_info(sig, &info, current);

	trace_buffer_restore(j);
	return;
}

/* Typical exception handling routines	*/

#define EXPAND_LEN ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN) * 256 - 1)

void dump_bfin_trace_buffer(void)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int tflags, i = 0;
	char buf[150];
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	int j, index;
#endif

	trace_buffer_save(tflags);

	printk(KERN_NOTICE "Hardware Trace:\n");

	if (likely(bfin_read_TBUFSTAT() & TBUFCNT)) {
		for (; bfin_read_TBUFSTAT() & TBUFCNT; i++) {
			decode_address(buf, (unsigned long)bfin_read_TBUF());
			printk(KERN_NOTICE "%4i Target : %s\n", i, buf);
			decode_address(buf, (unsigned long)bfin_read_TBUF());
			printk(KERN_NOTICE "     Source : %s\n", buf);
		}
	}

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	if (trace_buff_offset)
		index = trace_buff_offset/4 - 1;
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
		printk(KERN_NOTICE "     Source : %s\n", buf);
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

static void show_trace(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long addr;

	printk(KERN_NOTICE "\n" KERN_NOTICE "Call Trace:\n");

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

	printk(KERN_NOTICE "\n");
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

	printk(KERN_NOTICE "Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i % 8 == 0)
			printk("\n" KERN_NOTICE "       ");
		printk(" %08lx", *stack++);
	}
	printk("\n");

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
		printk(KERN_NOTICE "Defered Exception context\n");
	else if (context & 0x3FC0)
		printk(KERN_NOTICE "Interrupt context\n");
	else if (context & 0x4000)
		printk(KERN_NOTICE "Deferred Interrupt context\n");
	else if (context & 0x8000)
		printk(KERN_NOTICE "Kernel process context\n");

	if (current->pid && current->mm) {
		printk(KERN_NOTICE "CURRENT PROCESS:\n");
		printk(KERN_NOTICE "COMM=%s PID=%d\n",
			current->comm, current->pid);

		printk(KERN_NOTICE "TEXT = 0x%p-0x%p  DATA = 0x%p-0x%p\n"
			KERN_NOTICE "BSS = 0x%p-0x%p   USER-STACK = 0x%p\n"
			KERN_NOTICE "\n",
			(void *)current->mm->start_code,
			(void *)current->mm->end_code,
			(void *)current->mm->start_data,
			(void *)current->mm->end_data,
			(void *)current->mm->end_data,
			(void *)current->mm->brk,
			(void *)current->mm->start_stack);
	} else
		printk(KERN_NOTICE "\n" KERN_NOTICE
		     "No Valid process in current context\n");
}

void dump_bfin_mem(struct pt_regs *fp)
{
	unsigned short *addr, *erraddr, val = 0, err = 0;
	char sti = 0, buf[6];

	if (unlikely((fp->seqstat & SEQSTAT_EXCAUSE) == VEC_HWERR))
		erraddr = (void *)fp->pc;
	else
		erraddr = (void *)fp->retx;

	printk(KERN_NOTICE "return address: [0x%p]; contents of:", erraddr);

	for (addr = (unsigned short *)((unsigned long)erraddr & ~0xF) - 0x10;
	     addr < (unsigned short *)((unsigned long)erraddr & ~0xF) + 0x10;
	     addr++) {
		if (!((unsigned long)addr & 0xF))
			printk("\n" KERN_NOTICE "0x%p: ", addr);

		if (get_user(val, addr)) {
			if (addr >= (unsigned short *)L1_CODE_START &&
			    addr < (unsigned short *)(L1_CODE_START + L1_CODE_LENGTH)) {
				dma_memcpy(&val, addr, sizeof(val));
				sprintf(buf, "%04x", val);
			} else if (addr >= (unsigned short *)FIXED_CODE_START &&
				addr <= (unsigned short *)memory_start) {
				val = bfin_read16(addr);
				sprintf(buf, "%04x", val);
			} else {
				val = 0;
				sprintf(buf, "????");
			}
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
		decode_address(buf, bfin_read_DCPLB_FAULT_ADDR());
		printk(KERN_NOTICE "DCPLB_FAULT_ADDR: %s\n", buf);
		decode_address(buf, bfin_read_ICPLB_FAULT_ADDR());
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

	printk(KERN_EMERG "DCPLB_FAULT_ADDR=%p\n", (void *)bfin_read_DCPLB_FAULT_ADDR());
	printk(KERN_EMERG "ICPLB_FAULT_ADDR=%p\n", (void *)bfin_read_ICPLB_FAULT_ADDR());
	dump_bfin_process(fp);
	dump_bfin_mem(fp);
	show_regs(fp);
	dump_stack();
	panic("Unrecoverable event\n");
}
