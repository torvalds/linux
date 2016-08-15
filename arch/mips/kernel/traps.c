/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 01, 06 Ralf Baechle
 * Copyright (C) 1995, 1996 Paul M. Antoine
 * Copyright (C) 1998 Ulf Carlsson
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2002, 2003, 2004, 2005, 2007  Maciej W. Rozycki
 * Copyright (C) 2000, 2001, 2012 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/cpu_pm.h>
#include <linux/kexec.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/extable.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/bootmem.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/kdb.h>
#include <linux/irq.h>
#include <linux/perf_event.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/branch.h>
#include <asm/break.h>
#include <asm/cop2.h>
#include <asm/cpu.h>
#include <asm/cpu-type.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/fpu_emulator.h>
#include <asm/idle.h>
#include <asm/mips-r2-to-r6-emul.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/module.h>
#include <asm/msa.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/siginfo.h>
#include <asm/tlbdebug.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/watch.h>
#include <asm/mmu_context.h>
#include <asm/types.h>
#include <asm/stacktrace.h>
#include <asm/uasm.h>

extern void check_wait(void);
extern asmlinkage void rollback_handle_int(void);
extern asmlinkage void handle_int(void);
extern u32 handle_tlbl[];
extern u32 handle_tlbs[];
extern u32 handle_tlbm[];
extern asmlinkage void handle_adel(void);
extern asmlinkage void handle_ades(void);
extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_ri_rdhwr_vivt(void);
extern asmlinkage void handle_ri_rdhwr(void);
extern asmlinkage void handle_cpu(void);
extern asmlinkage void handle_ov(void);
extern asmlinkage void handle_tr(void);
extern asmlinkage void handle_msa_fpe(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_ftlb(void);
extern asmlinkage void handle_msa(void);
extern asmlinkage void handle_mdmx(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_mt(void);
extern asmlinkage void handle_dsp(void);
extern asmlinkage void handle_mcheck(void);
extern asmlinkage void handle_reserved(void);
extern void tlb_do_page_fault_0(void);

void (*board_be_init)(void);
int (*board_be_handler)(struct pt_regs *regs, int is_fixup);
void (*board_nmi_handler_setup)(void);
void (*board_ejtag_handler_setup)(void);
void (*board_bind_eic_interrupt)(int irq, int regset);
void (*board_ebase_setup)(void);
void(*board_cache_error_setup)(void);

static void show_raw_backtrace(unsigned long reg29)
{
	unsigned long *sp = (unsigned long *)(reg29 & ~3);
	unsigned long addr;

	printk("Call Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	while (!kstack_end(sp)) {
		unsigned long __user *p =
			(unsigned long __user *)(unsigned long)sp++;
		if (__get_user(addr, p)) {
			printk(" (Bad stack address)");
			break;
		}
		if (__kernel_text_address(addr))
			print_ip_sym(addr);
	}
	printk("\n");
}

#ifdef CONFIG_KALLSYMS
int raw_show_trace;
static int __init set_raw_show_trace(char *str)
{
	raw_show_trace = 1;
	return 1;
}
__setup("raw_show_trace", set_raw_show_trace);
#endif

static void show_backtrace(struct task_struct *task, const struct pt_regs *regs)
{
	unsigned long sp = regs->regs[29];
	unsigned long ra = regs->regs[31];
	unsigned long pc = regs->cp0_epc;

	if (!task)
		task = current;

	if (raw_show_trace || user_mode(regs) || !__kernel_text_address(pc)) {
		show_raw_backtrace(sp);
		return;
	}
	printk("Call Trace:\n");
	do {
		print_ip_sym(pc);
		pc = unwind_stack(task, &sp, pc, &ra);
	} while (pc);
	printk("\n");
}

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */
static void show_stacktrace(struct task_struct *task,
	const struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	long stackdata;
	int i;
	unsigned long __user *sp = (unsigned long __user *)regs->regs[29];

	printk("Stack :");
	i = 0;
	while ((unsigned long) sp & (PAGE_SIZE - 1)) {
		if (i && ((i % (64 / field)) == 0))
			printk("\n	 ");
		if (i > 39) {
			printk(" ...");
			break;
		}

		if (__get_user(stackdata, sp++)) {
			printk(" (Bad stack address)");
			break;
		}

		printk(" %0*lx", field, stackdata);
		i++;
	}
	printk("\n");
	show_backtrace(task, regs);
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	struct pt_regs regs;
	mm_segment_t old_fs = get_fs();
	if (sp) {
		regs.regs[29] = (unsigned long)sp;
		regs.regs[31] = 0;
		regs.cp0_epc = 0;
	} else {
		if (task && task != current) {
			regs.regs[29] = task->thread.reg29;
			regs.regs[31] = 0;
			regs.cp0_epc = task->thread.reg31;
#ifdef CONFIG_KGDB_KDB
		} else if (atomic_read(&kgdb_active) != -1 &&
			   kdb_current_regs) {
			memcpy(&regs, kdb_current_regs, sizeof(regs));
#endif /* CONFIG_KGDB_KDB */
		} else {
			prepare_frametrace(&regs);
		}
	}
	/*
	 * show_stack() deals exclusively with kernel mode, so be sure to access
	 * the stack in the kernel (not user) address space.
	 */
	set_fs(KERNEL_DS);
	show_stacktrace(task, &regs);
	set_fs(old_fs);
}

static void show_code(unsigned int __user *pc)
{
	long i;
	unsigned short __user *pc16 = NULL;

	printk("\nCode:");

	if ((unsigned long)pc & 1)
		pc16 = (unsigned short __user *)((unsigned long)pc & ~1);
	for(i = -3 ; i < 6 ; i++) {
		unsigned int insn;
		if (pc16 ? __get_user(insn, pc16 + i) : __get_user(insn, pc + i)) {
			printk(" (Bad address in epc)\n");
			break;
		}
		printk("%c%0*x%c", (i?' ':'<'), pc16 ? 4 : 8, insn, (i?' ':'>'));
	}
}

static void __show_regs(const struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int cause = regs->cp0_cause;
	unsigned int exccode;
	int i;

	show_regs_print_info(KERN_DEFAULT);

	/*
	 * Saved main processor registers
	 */
	for (i = 0; i < 32; ) {
		if ((i % 4) == 0)
			printk("$%2d   :", i);
		if (i == 0)
			printk(" %0*lx", field, 0UL);
		else if (i == 26 || i == 27)
			printk(" %*s", field, "");
		else
			printk(" %0*lx", field, regs->regs[i]);

		i++;
		if ((i % 4) == 0)
			printk("\n");
	}

#ifdef CONFIG_CPU_HAS_SMARTMIPS
	printk("Acx    : %0*lx\n", field, regs->acx);
#endif
	printk("Hi    : %0*lx\n", field, regs->hi);
	printk("Lo    : %0*lx\n", field, regs->lo);

	/*
	 * Saved cp0 registers
	 */
	printk("epc   : %0*lx %pS\n", field, regs->cp0_epc,
	       (void *) regs->cp0_epc);
	printk("ra    : %0*lx %pS\n", field, regs->regs[31],
	       (void *) regs->regs[31]);

	printk("Status: %08x	", (uint32_t) regs->cp0_status);

	if (cpu_has_3kex) {
		if (regs->cp0_status & ST0_KUO)
			printk("KUo ");
		if (regs->cp0_status & ST0_IEO)
			printk("IEo ");
		if (regs->cp0_status & ST0_KUP)
			printk("KUp ");
		if (regs->cp0_status & ST0_IEP)
			printk("IEp ");
		if (regs->cp0_status & ST0_KUC)
			printk("KUc ");
		if (regs->cp0_status & ST0_IEC)
			printk("IEc ");
	} else if (cpu_has_4kex) {
		if (regs->cp0_status & ST0_KX)
			printk("KX ");
		if (regs->cp0_status & ST0_SX)
			printk("SX ");
		if (regs->cp0_status & ST0_UX)
			printk("UX ");
		switch (regs->cp0_status & ST0_KSU) {
		case KSU_USER:
			printk("USER ");
			break;
		case KSU_SUPERVISOR:
			printk("SUPERVISOR ");
			break;
		case KSU_KERNEL:
			printk("KERNEL ");
			break;
		default:
			printk("BAD_MODE ");
			break;
		}
		if (regs->cp0_status & ST0_ERL)
			printk("ERL ");
		if (regs->cp0_status & ST0_EXL)
			printk("EXL ");
		if (regs->cp0_status & ST0_IE)
			printk("IE ");
	}
	printk("\n");

	exccode = (cause & CAUSEF_EXCCODE) >> CAUSEB_EXCCODE;
	printk("Cause : %08x (ExcCode %02x)\n", cause, exccode);

	if (1 <= exccode && exccode <= 5)
		printk("BadVA : %0*lx\n", field, regs->cp0_badvaddr);

	printk("PrId  : %08x (%s)\n", read_c0_prid(),
	       cpu_name_string());
}

/*
 * FIXME: really the generic show_regs should take a const pointer argument.
 */
void show_regs(struct pt_regs *regs)
{
	__show_regs((struct pt_regs *)regs);
}

void show_registers(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	mm_segment_t old_fs = get_fs();

	__show_regs(regs);
	print_modules();
	printk("Process %s (pid: %d, threadinfo=%p, task=%p, tls=%0*lx)\n",
	       current->comm, current->pid, current_thread_info(), current,
	      field, current_thread_info()->tp_value);
	if (cpu_has_userlocal) {
		unsigned long tls;

		tls = read_c0_userlocal();
		if (tls != current_thread_info()->tp_value)
			printk("*HwTLS: %0*lx\n", field, tls);
	}

	if (!user_mode(regs))
		/* Necessary for getting the correct stack content */
		set_fs(KERNEL_DS);
	show_stacktrace(current, regs);
	show_code((unsigned int __user *) regs->cp0_epc);
	printk("\n");
	set_fs(old_fs);
}

static DEFINE_RAW_SPINLOCK(die_lock);

void __noreturn die(const char *str, struct pt_regs *regs)
{
	static int die_counter;
	int sig = SIGSEGV;

	oops_enter();

	if (notify_die(DIE_OOPS, str, regs, 0, current->thread.trap_nr,
		       SIGSEGV) == NOTIFY_STOP)
		sig = 0;

	console_verbose();
	raw_spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	printk("%s[#%d]:\n", str, ++die_counter);
	show_registers(regs);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	raw_spin_unlock_irq(&die_lock);

	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	if (regs && kexec_should_crash(current))
		crash_kexec(regs);

	do_exit(sig);
}

extern struct exception_table_entry __start___dbe_table[];
extern struct exception_table_entry __stop___dbe_table[];

__asm__(
"	.section	__dbe_table, \"a\"\n"
"	.previous			\n");

/* Given an address, look for it in the exception tables. */
static const struct exception_table_entry *search_dbe_tables(unsigned long addr)
{
	const struct exception_table_entry *e;

	e = search_extable(__start___dbe_table, __stop___dbe_table - 1, addr);
	if (!e)
		e = search_module_dbetables(addr);
	return e;
}

asmlinkage void do_be(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	const struct exception_table_entry *fixup = NULL;
	int data = regs->cp0_cause & 4;
	int action = MIPS_BE_FATAL;
	enum ctx_state prev_state;

	prev_state = exception_enter();
	/* XXX For now.	 Fixme, this searches the wrong table ...  */
	if (data && !user_mode(regs))
		fixup = search_dbe_tables(exception_epc(regs));

	if (fixup)
		action = MIPS_BE_FIXUP;

	if (board_be_handler)
		action = board_be_handler(regs, fixup != NULL);

	switch (action) {
	case MIPS_BE_DISCARD:
		goto out;
	case MIPS_BE_FIXUP:
		if (fixup) {
			regs->cp0_epc = fixup->nextinsn;
			goto out;
		}
		break;
	default:
		break;
	}

	/*
	 * Assume it would be too dangerous to continue ...
	 */
	printk(KERN_ALERT "%s bus error, epc == %0*lx, ra == %0*lx\n",
	       data ? "Data" : "Instruction",
	       field, regs->cp0_epc, field, regs->regs[31]);
	if (notify_die(DIE_OOPS, "bus error", regs, 0, current->thread.trap_nr,
		       SIGBUS) == NOTIFY_STOP)
		goto out;

	die_if_kernel("Oops", regs);
	force_sig(SIGBUS, current);

out:
	exception_exit(prev_state);
}

/*
 * ll/sc, rdhwr, sync emulation
 */

#define OPCODE 0xfc000000
#define BASE   0x03e00000
#define RT     0x001f0000
#define OFFSET 0x0000ffff
#define LL     0xc0000000
#define SC     0xe0000000
#define SPEC0  0x00000000
#define SPEC3  0x7c000000
#define RD     0x0000f800
#define FUNC   0x0000003f
#define SYNC   0x0000000f
#define RDHWR  0x0000003b

/*  microMIPS definitions   */
#define MM_POOL32A_FUNC 0xfc00ffff
#define MM_RDHWR        0x00006b3c
#define MM_RS           0x001f0000
#define MM_RT           0x03e00000

/*
 * The ll_bit is cleared by r*_switch.S
 */

unsigned int ll_bit;
struct task_struct *ll_task;

static inline int simulate_ll(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long value, __user *vaddr;
	long offset;

	/*
	 * analyse the ll instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long __user *)
		((unsigned long)(regs->regs[(opcode & BASE) >> 21]) + offset);

	if ((unsigned long)vaddr & 3)
		return SIGBUS;
	if (get_user(value, vaddr))
		return SIGSEGV;

	preempt_disable();

	if (ll_task == NULL || ll_task == current) {
		ll_bit = 1;
	} else {
		ll_bit = 0;
	}
	ll_task = current;

	preempt_enable();

	regs->regs[(opcode & RT) >> 16] = value;

	return 0;
}

static inline int simulate_sc(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long __user *vaddr;
	unsigned long reg;
	long offset;

	/*
	 * analyse the sc instruction that just caused a ri exception
	 * and put the referenced address to addr.
	 */

	/* sign extend offset */
	offset = opcode & OFFSET;
	offset <<= 16;
	offset >>= 16;

	vaddr = (unsigned long __user *)
		((unsigned long)(regs->regs[(opcode & BASE) >> 21]) + offset);
	reg = (opcode & RT) >> 16;

	if ((unsigned long)vaddr & 3)
		return SIGBUS;

	preempt_disable();

	if (ll_bit == 0 || ll_task != current) {
		regs->regs[reg] = 0;
		preempt_enable();
		return 0;
	}

	preempt_enable();

	if (put_user(regs->regs[reg], vaddr))
		return SIGSEGV;

	regs->regs[reg] = 1;

	return 0;
}

/*
 * ll uses the opcode of lwc0 and sc uses the opcode of swc0.  That is both
 * opcodes are supposed to result in coprocessor unusable exceptions if
 * executed on ll/sc-less processors.  That's the theory.  In practice a
 * few processors such as NEC's VR4100 throw reserved instruction exceptions
 * instead, so we're doing the emulation thing in both exception handlers.
 */
static int simulate_llsc(struct pt_regs *regs, unsigned int opcode)
{
	if ((opcode & OPCODE) == LL) {
		perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS,
				1, regs, 0);
		return simulate_ll(regs, opcode);
	}
	if ((opcode & OPCODE) == SC) {
		perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS,
				1, regs, 0);
		return simulate_sc(regs, opcode);
	}

	return -1;			/* Must be something else ... */
}

/*
 * Simulate trapping 'rdhwr' instructions to provide user accessible
 * registers not implemented in hardware.
 */
static int simulate_rdhwr(struct pt_regs *regs, int rd, int rt)
{
	struct thread_info *ti = task_thread_info(current);

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS,
			1, regs, 0);
	switch (rd) {
	case MIPS_HWR_CPUNUM:		/* CPU number */
		regs->regs[rt] = smp_processor_id();
		return 0;
	case MIPS_HWR_SYNCISTEP:	/* SYNCI length */
		regs->regs[rt] = min(current_cpu_data.dcache.linesz,
				     current_cpu_data.icache.linesz);
		return 0;
	case MIPS_HWR_CC:		/* Read count register */
		regs->regs[rt] = read_c0_count();
		return 0;
	case MIPS_HWR_CCRES:		/* Count register resolution */
		switch (current_cpu_type()) {
		case CPU_20KC:
		case CPU_25KF:
			regs->regs[rt] = 1;
			break;
		default:
			regs->regs[rt] = 2;
		}
		return 0;
	case MIPS_HWR_ULR:		/* Read UserLocal register */
		regs->regs[rt] = ti->tp_value;
		return 0;
	default:
		return -1;
	}
}

static int simulate_rdhwr_normal(struct pt_regs *regs, unsigned int opcode)
{
	if ((opcode & OPCODE) == SPEC3 && (opcode & FUNC) == RDHWR) {
		int rd = (opcode & RD) >> 11;
		int rt = (opcode & RT) >> 16;

		simulate_rdhwr(regs, rd, rt);
		return 0;
	}

	/* Not ours.  */
	return -1;
}

static int simulate_rdhwr_mm(struct pt_regs *regs, unsigned int opcode)
{
	if ((opcode & MM_POOL32A_FUNC) == MM_RDHWR) {
		int rd = (opcode & MM_RS) >> 16;
		int rt = (opcode & MM_RT) >> 21;
		simulate_rdhwr(regs, rd, rt);
		return 0;
	}

	/* Not ours.  */
	return -1;
}

static int simulate_sync(struct pt_regs *regs, unsigned int opcode)
{
	if ((opcode & OPCODE) == SPEC0 && (opcode & FUNC) == SYNC) {
		perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS,
				1, regs, 0);
		return 0;
	}

	return -1;			/* Must be something else ... */
}

asmlinkage void do_ov(struct pt_regs *regs)
{
	enum ctx_state prev_state;
	siginfo_t info = {
		.si_signo = SIGFPE,
		.si_code = FPE_INTOVF,
		.si_addr = (void __user *)regs->cp0_epc,
	};

	prev_state = exception_enter();
	die_if_kernel("Integer overflow", regs);

	force_sig_info(SIGFPE, &info, current);
	exception_exit(prev_state);
}

int process_fpemu_return(int sig, void __user *fault_addr, unsigned long fcr31)
{
	struct siginfo si = { 0 };
	struct vm_area_struct *vma;

	switch (sig) {
	case 0:
		return 0;

	case SIGFPE:
		si.si_addr = fault_addr;
		si.si_signo = sig;
		/*
		 * Inexact can happen together with Overflow or Underflow.
		 * Respect the mask to deliver the correct exception.
		 */
		fcr31 &= (fcr31 & FPU_CSR_ALL_E) <<
			 (ffs(FPU_CSR_ALL_X) - ffs(FPU_CSR_ALL_E));
		if (fcr31 & FPU_CSR_INV_X)
			si.si_code = FPE_FLTINV;
		else if (fcr31 & FPU_CSR_DIV_X)
			si.si_code = FPE_FLTDIV;
		else if (fcr31 & FPU_CSR_OVF_X)
			si.si_code = FPE_FLTOVF;
		else if (fcr31 & FPU_CSR_UDF_X)
			si.si_code = FPE_FLTUND;
		else if (fcr31 & FPU_CSR_INE_X)
			si.si_code = FPE_FLTRES;
		else
			si.si_code = __SI_FAULT;
		force_sig_info(sig, &si, current);
		return 1;

	case SIGBUS:
		si.si_addr = fault_addr;
		si.si_signo = sig;
		si.si_code = BUS_ADRERR;
		force_sig_info(sig, &si, current);
		return 1;

	case SIGSEGV:
		si.si_addr = fault_addr;
		si.si_signo = sig;
		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, (unsigned long)fault_addr);
		if (vma && (vma->vm_start <= (unsigned long)fault_addr))
			si.si_code = SEGV_ACCERR;
		else
			si.si_code = SEGV_MAPERR;
		up_read(&current->mm->mmap_sem);
		force_sig_info(sig, &si, current);
		return 1;

	default:
		force_sig(sig, current);
		return 1;
	}
}

static int simulate_fp(struct pt_regs *regs, unsigned int opcode,
		       unsigned long old_epc, unsigned long old_ra)
{
	union mips_instruction inst = { .word = opcode };
	void __user *fault_addr;
	unsigned long fcr31;
	int sig;

	/* If it's obviously not an FP instruction, skip it */
	switch (inst.i_format.opcode) {
	case cop1_op:
	case cop1x_op:
	case lwc1_op:
	case ldc1_op:
	case swc1_op:
	case sdc1_op:
		break;

	default:
		return -1;
	}

	/*
	 * do_ri skipped over the instruction via compute_return_epc, undo
	 * that for the FPU emulator.
	 */
	regs->cp0_epc = old_epc;
	regs->regs[31] = old_ra;

	/* Save the FP context to struct thread_struct */
	lose_fpu(1);

	/* Run the emulator */
	sig = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 1,
				       &fault_addr);
	fcr31 = current->thread.fpu.fcr31;

	/*
	 * We can't allow the emulated instruction to leave any of
	 * the cause bits set in $fcr31.
	 */
	current->thread.fpu.fcr31 &= ~FPU_CSR_ALL_X;

	/* Restore the hardware register state */
	own_fpu(1);

	/* Send a signal if required.  */
	process_fpemu_return(sig, fault_addr, fcr31);

	return 0;
}

/*
 * XXX Delayed fp exceptions when doing a lazy ctx switch XXX
 */
asmlinkage void do_fpe(struct pt_regs *regs, unsigned long fcr31)
{
	enum ctx_state prev_state;
	void __user *fault_addr;
	int sig;

	prev_state = exception_enter();
	if (notify_die(DIE_FP, "FP exception", regs, 0, current->thread.trap_nr,
		       SIGFPE) == NOTIFY_STOP)
		goto out;

	/* Clear FCSR.Cause before enabling interrupts */
	write_32bit_cp1_register(CP1_STATUS, fcr31 & ~FPU_CSR_ALL_X);
	local_irq_enable();

	die_if_kernel("FP exception in kernel code", regs);

	if (fcr31 & FPU_CSR_UNI_X) {
		/*
		 * Unimplemented operation exception.  If we've got the full
		 * software emulator on-board, let's use it...
		 *
		 * Force FPU to dump state into task/thread context.  We're
		 * moving a lot of data here for what is probably a single
		 * instruction, but the alternative is to pre-decode the FP
		 * register operands before invoking the emulator, which seems
		 * a bit extreme for what should be an infrequent event.
		 */
		/* Ensure 'resume' not overwrite saved fp context again. */
		lose_fpu(1);

		/* Run the emulator */
		sig = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 1,
					       &fault_addr);
		fcr31 = current->thread.fpu.fcr31;

		/*
		 * We can't allow the emulated instruction to leave any of
		 * the cause bits set in $fcr31.
		 */
		current->thread.fpu.fcr31 &= ~FPU_CSR_ALL_X;

		/* Restore the hardware register state */
		own_fpu(1);	/* Using the FPU again.	 */
	} else {
		sig = SIGFPE;
		fault_addr = (void __user *) regs->cp0_epc;
	}

	/* Send a signal if required.  */
	process_fpemu_return(sig, fault_addr, fcr31);

out:
	exception_exit(prev_state);
}

void do_trap_or_bp(struct pt_regs *regs, unsigned int code, int si_code,
	const char *str)
{
	siginfo_t info = { 0 };
	char b[40];

#ifdef CONFIG_KGDB_LOW_LEVEL_TRAP
	if (kgdb_ll_trap(DIE_TRAP, str, regs, code, current->thread.trap_nr,
			 SIGTRAP) == NOTIFY_STOP)
		return;
#endif /* CONFIG_KGDB_LOW_LEVEL_TRAP */

	if (notify_die(DIE_TRAP, str, regs, code, current->thread.trap_nr,
		       SIGTRAP) == NOTIFY_STOP)
		return;

	/*
	 * A short test says that IRIX 5.3 sends SIGTRAP for all trap
	 * insns, even for trap and break codes that indicate arithmetic
	 * failures.  Weird ...
	 * But should we continue the brokenness???  --macro
	 */
	switch (code) {
	case BRK_OVERFLOW:
	case BRK_DIVZERO:
		scnprintf(b, sizeof(b), "%s instruction in kernel code", str);
		die_if_kernel(b, regs);
		if (code == BRK_DIVZERO)
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_addr = (void __user *) regs->cp0_epc;
		force_sig_info(SIGFPE, &info, current);
		break;
	case BRK_BUG:
		die_if_kernel("Kernel bug detected", regs);
		force_sig(SIGTRAP, current);
		break;
	case BRK_MEMU:
		/*
		 * This breakpoint code is used by the FPU emulator to retake
		 * control of the CPU after executing the instruction from the
		 * delay slot of an emulated branch.
		 *
		 * Terminate if exception was recognized as a delay slot return
		 * otherwise handle as normal.
		 */
		if (do_dsemulret(regs))
			return;

		die_if_kernel("Math emu break/trap", regs);
		force_sig(SIGTRAP, current);
		break;
	default:
		scnprintf(b, sizeof(b), "%s instruction in kernel code", str);
		die_if_kernel(b, regs);
		if (si_code) {
			info.si_signo = SIGTRAP;
			info.si_code = si_code;
			force_sig_info(SIGTRAP, &info, current);
		} else {
			force_sig(SIGTRAP, current);
		}
	}
}

asmlinkage void do_bp(struct pt_regs *regs)
{
	unsigned long epc = msk_isa16_mode(exception_epc(regs));
	unsigned int opcode, bcode;
	enum ctx_state prev_state;
	mm_segment_t seg;

	seg = get_fs();
	if (!user_mode(regs))
		set_fs(KERNEL_DS);

	prev_state = exception_enter();
	current->thread.trap_nr = (regs->cp0_cause >> 2) & 0x1f;
	if (get_isa16_mode(regs->cp0_epc)) {
		u16 instr[2];

		if (__get_user(instr[0], (u16 __user *)epc))
			goto out_sigsegv;

		if (!cpu_has_mmips) {
			/* MIPS16e mode */
			bcode = (instr[0] >> 5) & 0x3f;
		} else if (mm_insn_16bit(instr[0])) {
			/* 16-bit microMIPS BREAK */
			bcode = instr[0] & 0xf;
		} else {
			/* 32-bit microMIPS BREAK */
			if (__get_user(instr[1], (u16 __user *)(epc + 2)))
				goto out_sigsegv;
			opcode = (instr[0] << 16) | instr[1];
			bcode = (opcode >> 6) & ((1 << 20) - 1);
		}
	} else {
		if (__get_user(opcode, (unsigned int __user *)epc))
			goto out_sigsegv;
		bcode = (opcode >> 6) & ((1 << 20) - 1);
	}

	/*
	 * There is the ancient bug in the MIPS assemblers that the break
	 * code starts left to bit 16 instead to bit 6 in the opcode.
	 * Gas is bug-compatible, but not always, grrr...
	 * We handle both cases with a simple heuristics.  --macro
	 */
	if (bcode >= (1 << 10))
		bcode = ((bcode & ((1 << 10) - 1)) << 10) | (bcode >> 10);

	/*
	 * notify the kprobe handlers, if instruction is likely to
	 * pertain to them.
	 */
	switch (bcode) {
	case BRK_UPROBE:
		if (notify_die(DIE_UPROBE, "uprobe", regs, bcode,
			       current->thread.trap_nr, SIGTRAP) == NOTIFY_STOP)
			goto out;
		else
			break;
	case BRK_UPROBE_XOL:
		if (notify_die(DIE_UPROBE_XOL, "uprobe_xol", regs, bcode,
			       current->thread.trap_nr, SIGTRAP) == NOTIFY_STOP)
			goto out;
		else
			break;
	case BRK_KPROBE_BP:
		if (notify_die(DIE_BREAK, "debug", regs, bcode,
			       current->thread.trap_nr, SIGTRAP) == NOTIFY_STOP)
			goto out;
		else
			break;
	case BRK_KPROBE_SSTEPBP:
		if (notify_die(DIE_SSTEPBP, "single_step", regs, bcode,
			       current->thread.trap_nr, SIGTRAP) == NOTIFY_STOP)
			goto out;
		else
			break;
	default:
		break;
	}

	do_trap_or_bp(regs, bcode, TRAP_BRKPT, "Break");

out:
	set_fs(seg);
	exception_exit(prev_state);
	return;

out_sigsegv:
	force_sig(SIGSEGV, current);
	goto out;
}

asmlinkage void do_tr(struct pt_regs *regs)
{
	u32 opcode, tcode = 0;
	enum ctx_state prev_state;
	u16 instr[2];
	mm_segment_t seg;
	unsigned long epc = msk_isa16_mode(exception_epc(regs));

	seg = get_fs();
	if (!user_mode(regs))
		set_fs(get_ds());

	prev_state = exception_enter();
	current->thread.trap_nr = (regs->cp0_cause >> 2) & 0x1f;
	if (get_isa16_mode(regs->cp0_epc)) {
		if (__get_user(instr[0], (u16 __user *)(epc + 0)) ||
		    __get_user(instr[1], (u16 __user *)(epc + 2)))
			goto out_sigsegv;
		opcode = (instr[0] << 16) | instr[1];
		/* Immediate versions don't provide a code.  */
		if (!(opcode & OPCODE))
			tcode = (opcode >> 12) & ((1 << 4) - 1);
	} else {
		if (__get_user(opcode, (u32 __user *)epc))
			goto out_sigsegv;
		/* Immediate versions don't provide a code.  */
		if (!(opcode & OPCODE))
			tcode = (opcode >> 6) & ((1 << 10) - 1);
	}

	do_trap_or_bp(regs, tcode, 0, "Trap");

out:
	set_fs(seg);
	exception_exit(prev_state);
	return;

out_sigsegv:
	force_sig(SIGSEGV, current);
	goto out;
}

asmlinkage void do_ri(struct pt_regs *regs)
{
	unsigned int __user *epc = (unsigned int __user *)exception_epc(regs);
	unsigned long old_epc = regs->cp0_epc;
	unsigned long old31 = regs->regs[31];
	enum ctx_state prev_state;
	unsigned int opcode = 0;
	int status = -1;

	/*
	 * Avoid any kernel code. Just emulate the R2 instruction
	 * as quickly as possible.
	 */
	if (mipsr2_emulation && cpu_has_mips_r6 &&
	    likely(user_mode(regs)) &&
	    likely(get_user(opcode, epc) >= 0)) {
		unsigned long fcr31 = 0;

		status = mipsr2_decoder(regs, opcode, &fcr31);
		switch (status) {
		case 0:
		case SIGEMT:
			task_thread_info(current)->r2_emul_return = 1;
			return;
		case SIGILL:
			goto no_r2_instr;
		default:
			process_fpemu_return(status,
					     &current->thread.cp0_baduaddr,
					     fcr31);
			task_thread_info(current)->r2_emul_return = 1;
			return;
		}
	}

no_r2_instr:

	prev_state = exception_enter();
	current->thread.trap_nr = (regs->cp0_cause >> 2) & 0x1f;

	if (notify_die(DIE_RI, "RI Fault", regs, 0, current->thread.trap_nr,
		       SIGILL) == NOTIFY_STOP)
		goto out;

	die_if_kernel("Reserved instruction in kernel code", regs);

	if (unlikely(compute_return_epc(regs) < 0))
		goto out;

	if (!get_isa16_mode(regs->cp0_epc)) {
		if (unlikely(get_user(opcode, epc) < 0))
			status = SIGSEGV;

		if (!cpu_has_llsc && status < 0)
			status = simulate_llsc(regs, opcode);

		if (status < 0)
			status = simulate_rdhwr_normal(regs, opcode);

		if (status < 0)
			status = simulate_sync(regs, opcode);

		if (status < 0)
			status = simulate_fp(regs, opcode, old_epc, old31);
	} else if (cpu_has_mmips) {
		unsigned short mmop[2] = { 0 };

		if (unlikely(get_user(mmop[0], (u16 __user *)epc + 0) < 0))
			status = SIGSEGV;
		if (unlikely(get_user(mmop[1], (u16 __user *)epc + 1) < 0))
			status = SIGSEGV;
		opcode = mmop[0];
		opcode = (opcode << 16) | mmop[1];

		if (status < 0)
			status = simulate_rdhwr_mm(regs, opcode);
	}

	if (status < 0)
		status = SIGILL;

	if (unlikely(status > 0)) {
		regs->cp0_epc = old_epc;		/* Undo skip-over.  */
		regs->regs[31] = old31;
		force_sig(status, current);
	}

out:
	exception_exit(prev_state);
}

/*
 * MIPS MT processors may have fewer FPU contexts than CPU threads. If we've
 * emulated more than some threshold number of instructions, force migration to
 * a "CPU" that has FP support.
 */
static void mt_ase_fp_affinity(void)
{
#ifdef CONFIG_MIPS_MT_FPAFF
	if (mt_fpemul_threshold > 0 &&
	     ((current->thread.emulated_fp++ > mt_fpemul_threshold))) {
		/*
		 * If there's no FPU present, or if the application has already
		 * restricted the allowed set to exclude any CPUs with FPUs,
		 * we'll skip the procedure.
		 */
		if (cpumask_intersects(&current->cpus_allowed, &mt_fpu_cpumask)) {
			cpumask_t tmask;

			current->thread.user_cpus_allowed
				= current->cpus_allowed;
			cpumask_and(&tmask, &current->cpus_allowed,
				    &mt_fpu_cpumask);
			set_cpus_allowed_ptr(current, &tmask);
			set_thread_flag(TIF_FPUBOUND);
		}
	}
#endif /* CONFIG_MIPS_MT_FPAFF */
}

/*
 * No lock; only written during early bootup by CPU 0.
 */
static RAW_NOTIFIER_HEAD(cu2_chain);

int __ref register_cu2_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&cu2_chain, nb);
}

int cu2_notifier_call_chain(unsigned long val, void *v)
{
	return raw_notifier_call_chain(&cu2_chain, val, v);
}

static int default_cu2_call(struct notifier_block *nfb, unsigned long action,
	void *data)
{
	struct pt_regs *regs = data;

	die_if_kernel("COP2: Unhandled kernel unaligned access or invalid "
			      "instruction", regs);
	force_sig(SIGILL, current);

	return NOTIFY_OK;
}

static int wait_on_fp_mode_switch(atomic_t *p)
{
	/*
	 * The FP mode for this task is currently being switched. That may
	 * involve modifications to the format of this tasks FP context which
	 * make it unsafe to proceed with execution for the moment. Instead,
	 * schedule some other task.
	 */
	schedule();
	return 0;
}

static int enable_restore_fp_context(int msa)
{
	int err, was_fpu_owner, prior_msa;

	/*
	 * If an FP mode switch is currently underway, wait for it to
	 * complete before proceeding.
	 */
	wait_on_atomic_t(&current->mm->context.fp_mode_switching,
			 wait_on_fp_mode_switch, TASK_KILLABLE);

	if (!used_math()) {
		/* First time FP context user. */
		preempt_disable();
		err = init_fpu();
		if (msa && !err) {
			enable_msa();
			init_msa_upper();
			set_thread_flag(TIF_USEDMSA);
			set_thread_flag(TIF_MSA_CTX_LIVE);
		}
		preempt_enable();
		if (!err)
			set_used_math();
		return err;
	}

	/*
	 * This task has formerly used the FP context.
	 *
	 * If this thread has no live MSA vector context then we can simply
	 * restore the scalar FP context. If it has live MSA vector context
	 * (that is, it has or may have used MSA since last performing a
	 * function call) then we'll need to restore the vector context. This
	 * applies even if we're currently only executing a scalar FP
	 * instruction. This is because if we were to later execute an MSA
	 * instruction then we'd either have to:
	 *
	 *  - Restore the vector context & clobber any registers modified by
	 *    scalar FP instructions between now & then.
	 *
	 * or
	 *
	 *  - Not restore the vector context & lose the most significant bits
	 *    of all vector registers.
	 *
	 * Neither of those options is acceptable. We cannot restore the least
	 * significant bits of the registers now & only restore the most
	 * significant bits later because the most significant bits of any
	 * vector registers whose aliased FP register is modified now will have
	 * been zeroed. We'd have no way to know that when restoring the vector
	 * context & thus may load an outdated value for the most significant
	 * bits of a vector register.
	 */
	if (!msa && !thread_msa_context_live())
		return own_fpu(1);

	/*
	 * This task is using or has previously used MSA. Thus we require
	 * that Status.FR == 1.
	 */
	preempt_disable();
	was_fpu_owner = is_fpu_owner();
	err = own_fpu_inatomic(0);
	if (err)
		goto out;

	enable_msa();
	write_msa_csr(current->thread.fpu.msacsr);
	set_thread_flag(TIF_USEDMSA);

	/*
	 * If this is the first time that the task is using MSA and it has
	 * previously used scalar FP in this time slice then we already nave
	 * FP context which we shouldn't clobber. We do however need to clear
	 * the upper 64b of each vector register so that this task has no
	 * opportunity to see data left behind by another.
	 */
	prior_msa = test_and_set_thread_flag(TIF_MSA_CTX_LIVE);
	if (!prior_msa && was_fpu_owner) {
		init_msa_upper();

		goto out;
	}

	if (!prior_msa) {
		/*
		 * Restore the least significant 64b of each vector register
		 * from the existing scalar FP context.
		 */
		_restore_fp(current);

		/*
		 * The task has not formerly used MSA, so clear the upper 64b
		 * of each vector register such that it cannot see data left
		 * behind by another task.
		 */
		init_msa_upper();
	} else {
		/* We need to restore the vector context. */
		restore_msa(current);

		/* Restore the scalar FP control & status register */
		if (!was_fpu_owner)
			write_32bit_cp1_register(CP1_STATUS,
						 current->thread.fpu.fcr31);
	}

out:
	preempt_enable();

	return 0;
}

asmlinkage void do_cpu(struct pt_regs *regs)
{
	enum ctx_state prev_state;
	unsigned int __user *epc;
	unsigned long old_epc, old31;
	void __user *fault_addr;
	unsigned int opcode;
	unsigned long fcr31;
	unsigned int cpid;
	int status, err;
	int sig;

	prev_state = exception_enter();
	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;

	if (cpid != 2)
		die_if_kernel("do_cpu invoked from kernel context!", regs);

	switch (cpid) {
	case 0:
		epc = (unsigned int __user *)exception_epc(regs);
		old_epc = regs->cp0_epc;
		old31 = regs->regs[31];
		opcode = 0;
		status = -1;

		if (unlikely(compute_return_epc(regs) < 0))
			break;

		if (!get_isa16_mode(regs->cp0_epc)) {
			if (unlikely(get_user(opcode, epc) < 0))
				status = SIGSEGV;

			if (!cpu_has_llsc && status < 0)
				status = simulate_llsc(regs, opcode);
		}

		if (status < 0)
			status = SIGILL;

		if (unlikely(status > 0)) {
			regs->cp0_epc = old_epc;	/* Undo skip-over.  */
			regs->regs[31] = old31;
			force_sig(status, current);
		}

		break;

	case 3:
		/*
		 * The COP3 opcode space and consequently the CP0.Status.CU3
		 * bit and the CP0.Cause.CE=3 encoding have been removed as
		 * of the MIPS III ISA.  From the MIPS IV and MIPS32r2 ISAs
		 * up the space has been reused for COP1X instructions, that
		 * are enabled by the CP0.Status.CU1 bit and consequently
		 * use the CP0.Cause.CE=1 encoding for Coprocessor Unusable
		 * exceptions.  Some FPU-less processors that implement one
		 * of these ISAs however use this code erroneously for COP1X
		 * instructions.  Therefore we redirect this trap to the FP
		 * emulator too.
		 */
		if (raw_cpu_has_fpu || !cpu_has_mips_4_5_64_r2_r6) {
			force_sig(SIGILL, current);
			break;
		}
		/* Fall through.  */

	case 1:
		err = enable_restore_fp_context(0);

		if (raw_cpu_has_fpu && !err)
			break;

		sig = fpu_emulator_cop1Handler(regs, &current->thread.fpu, 0,
					       &fault_addr);
		fcr31 = current->thread.fpu.fcr31;

		/*
		 * We can't allow the emulated instruction to leave
		 * any of the cause bits set in $fcr31.
		 */
		current->thread.fpu.fcr31 &= ~FPU_CSR_ALL_X;

		/* Send a signal if required.  */
		if (!process_fpemu_return(sig, fault_addr, fcr31) && !err)
			mt_ase_fp_affinity();

		break;

	case 2:
		raw_notifier_call_chain(&cu2_chain, CU2_EXCEPTION, regs);
		break;
	}

	exception_exit(prev_state);
}

asmlinkage void do_msa_fpe(struct pt_regs *regs, unsigned int msacsr)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	current->thread.trap_nr = (regs->cp0_cause >> 2) & 0x1f;
	if (notify_die(DIE_MSAFP, "MSA FP exception", regs, 0,
		       current->thread.trap_nr, SIGFPE) == NOTIFY_STOP)
		goto out;

	/* Clear MSACSR.Cause before enabling interrupts */
	write_msa_csr(msacsr & ~MSA_CSR_CAUSEF);
	local_irq_enable();

	die_if_kernel("do_msa_fpe invoked from kernel context!", regs);
	force_sig(SIGFPE, current);
out:
	exception_exit(prev_state);
}

asmlinkage void do_msa(struct pt_regs *regs)
{
	enum ctx_state prev_state;
	int err;

	prev_state = exception_enter();

	if (!cpu_has_msa || test_thread_flag(TIF_32BIT_FPREGS)) {
		force_sig(SIGILL, current);
		goto out;
	}

	die_if_kernel("do_msa invoked from kernel context!", regs);

	err = enable_restore_fp_context(1);
	if (err)
		force_sig(SIGILL, current);
out:
	exception_exit(prev_state);
}

asmlinkage void do_mdmx(struct pt_regs *regs)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	force_sig(SIGILL, current);
	exception_exit(prev_state);
}

/*
 * Called with interrupts disabled.
 */
asmlinkage void do_watch(struct pt_regs *regs)
{
	siginfo_t info = { .si_signo = SIGTRAP, .si_code = TRAP_HWBKPT };
	enum ctx_state prev_state;

	prev_state = exception_enter();
	/*
	 * Clear WP (bit 22) bit of cause register so we don't loop
	 * forever.
	 */
	clear_c0_cause(CAUSEF_WP);

	/*
	 * If the current thread has the watch registers loaded, save
	 * their values and send SIGTRAP.  Otherwise another thread
	 * left the registers set, clear them and continue.
	 */
	if (test_tsk_thread_flag(current, TIF_LOAD_WATCH)) {
		mips_read_watch_registers();
		local_irq_enable();
		force_sig_info(SIGTRAP, &info, current);
	} else {
		mips_clear_watch_registers();
		local_irq_enable();
	}
	exception_exit(prev_state);
}

asmlinkage void do_mcheck(struct pt_regs *regs)
{
	int multi_match = regs->cp0_status & ST0_TS;
	enum ctx_state prev_state;
	mm_segment_t old_fs = get_fs();

	prev_state = exception_enter();
	show_regs(regs);

	if (multi_match) {
		dump_tlb_regs();
		pr_info("\n");
		dump_tlb_all();
	}

	if (!user_mode(regs))
		set_fs(KERNEL_DS);

	show_code((unsigned int __user *) regs->cp0_epc);

	set_fs(old_fs);

	/*
	 * Some chips may have other causes of machine check (e.g. SB1
	 * graduation timer)
	 */
	panic("Caught Machine Check exception - %scaused by multiple "
	      "matching entries in the TLB.",
	      (multi_match) ? "" : "not ");
}

asmlinkage void do_mt(struct pt_regs *regs)
{
	int subcode;

	subcode = (read_vpe_c0_vpecontrol() & VPECONTROL_EXCPT)
			>> VPECONTROL_EXCPT_SHIFT;
	switch (subcode) {
	case 0:
		printk(KERN_DEBUG "Thread Underflow\n");
		break;
	case 1:
		printk(KERN_DEBUG "Thread Overflow\n");
		break;
	case 2:
		printk(KERN_DEBUG "Invalid YIELD Qualifier\n");
		break;
	case 3:
		printk(KERN_DEBUG "Gating Storage Exception\n");
		break;
	case 4:
		printk(KERN_DEBUG "YIELD Scheduler Exception\n");
		break;
	case 5:
		printk(KERN_DEBUG "Gating Storage Scheduler Exception\n");
		break;
	default:
		printk(KERN_DEBUG "*** UNKNOWN THREAD EXCEPTION %d ***\n",
			subcode);
		break;
	}
	die_if_kernel("MIPS MT Thread exception in kernel", regs);

	force_sig(SIGILL, current);
}


asmlinkage void do_dsp(struct pt_regs *regs)
{
	if (cpu_has_dsp)
		panic("Unexpected DSP exception");

	force_sig(SIGILL, current);
}

asmlinkage void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.	 Most probably
	 * caused by a new unknown cpu type or after another deadly
	 * hard/software error.
	 */
	show_regs(regs);
	panic("Caught reserved exception %ld - should not happen.",
	      (regs->cp0_cause & 0x7f) >> 2);
}

static int __initdata l1parity = 1;
static int __init nol1parity(char *s)
{
	l1parity = 0;
	return 1;
}
__setup("nol1par", nol1parity);
static int __initdata l2parity = 1;
static int __init nol2parity(char *s)
{
	l2parity = 0;
	return 1;
}
__setup("nol2par", nol2parity);

/*
 * Some MIPS CPUs can enable/disable for cache parity detection, but do
 * it different ways.
 */
static inline void parity_protection_init(void)
{
	switch (current_cpu_type()) {
	case CPU_24K:
	case CPU_34K:
	case CPU_74K:
	case CPU_1004K:
	case CPU_1074K:
	case CPU_INTERAPTIV:
	case CPU_PROAPTIV:
	case CPU_P5600:
	case CPU_QEMU_GENERIC:
	case CPU_I6400:
	case CPU_P6600:
		{
#define ERRCTL_PE	0x80000000
#define ERRCTL_L2P	0x00800000
			unsigned long errctl;
			unsigned int l1parity_present, l2parity_present;

			errctl = read_c0_ecc();
			errctl &= ~(ERRCTL_PE|ERRCTL_L2P);

			/* probe L1 parity support */
			write_c0_ecc(errctl | ERRCTL_PE);
			back_to_back_c0_hazard();
			l1parity_present = (read_c0_ecc() & ERRCTL_PE);

			/* probe L2 parity support */
			write_c0_ecc(errctl|ERRCTL_L2P);
			back_to_back_c0_hazard();
			l2parity_present = (read_c0_ecc() & ERRCTL_L2P);

			if (l1parity_present && l2parity_present) {
				if (l1parity)
					errctl |= ERRCTL_PE;
				if (l1parity ^ l2parity)
					errctl |= ERRCTL_L2P;
			} else if (l1parity_present) {
				if (l1parity)
					errctl |= ERRCTL_PE;
			} else if (l2parity_present) {
				if (l2parity)
					errctl |= ERRCTL_L2P;
			} else {
				/* No parity available */
			}

			printk(KERN_INFO "Writing ErrCtl register=%08lx\n", errctl);

			write_c0_ecc(errctl);
			back_to_back_c0_hazard();
			errctl = read_c0_ecc();
			printk(KERN_INFO "Readback ErrCtl register=%08lx\n", errctl);

			if (l1parity_present)
				printk(KERN_INFO "Cache parity protection %sabled\n",
				       (errctl & ERRCTL_PE) ? "en" : "dis");

			if (l2parity_present) {
				if (l1parity_present && l1parity)
					errctl ^= ERRCTL_L2P;
				printk(KERN_INFO "L2 cache parity protection %sabled\n",
				       (errctl & ERRCTL_L2P) ? "en" : "dis");
			}
		}
		break;

	case CPU_5KC:
	case CPU_5KE:
	case CPU_LOONGSON1:
		write_c0_ecc(0x80000000);
		back_to_back_c0_hazard();
		/* Set the PE bit (bit 31) in the c0_errctl register. */
		printk(KERN_INFO "Cache parity protection %sabled\n",
		       (read_c0_ecc() & 0x80000000) ? "en" : "dis");
		break;
	case CPU_20KC:
	case CPU_25KF:
		/* Clear the DE bit (bit 16) in the c0_status register. */
		printk(KERN_INFO "Enable cache parity protection for "
		       "MIPS 20KC/25KF CPUs.\n");
		clear_c0_status(ST0_DE);
		break;
	default:
		break;
	}
}

asmlinkage void cache_parity_error(void)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int reg_val;

	/* For the moment, report the problem and hang. */
	printk("Cache error exception:\n");
	printk("cp0_errorepc == %0*lx\n", field, read_c0_errorepc());
	reg_val = read_c0_cacheerr();
	printk("c0_cacheerr == %08x\n", reg_val);

	printk("Decoded c0_cacheerr: %s cache fault in %s reference.\n",
	       reg_val & (1<<30) ? "secondary" : "primary",
	       reg_val & (1<<31) ? "data" : "insn");
	if ((cpu_has_mips_r2_r6) &&
	    ((current_cpu_data.processor_id & 0xff0000) == PRID_COMP_MIPS)) {
		pr_err("Error bits: %s%s%s%s%s%s%s%s\n",
			reg_val & (1<<29) ? "ED " : "",
			reg_val & (1<<28) ? "ET " : "",
			reg_val & (1<<27) ? "ES " : "",
			reg_val & (1<<26) ? "EE " : "",
			reg_val & (1<<25) ? "EB " : "",
			reg_val & (1<<24) ? "EI " : "",
			reg_val & (1<<23) ? "E1 " : "",
			reg_val & (1<<22) ? "E0 " : "");
	} else {
		pr_err("Error bits: %s%s%s%s%s%s%s\n",
			reg_val & (1<<29) ? "ED " : "",
			reg_val & (1<<28) ? "ET " : "",
			reg_val & (1<<26) ? "EE " : "",
			reg_val & (1<<25) ? "EB " : "",
			reg_val & (1<<24) ? "EI " : "",
			reg_val & (1<<23) ? "E1 " : "",
			reg_val & (1<<22) ? "E0 " : "");
	}
	printk("IDX: 0x%08x\n", reg_val & ((1<<22)-1));

#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
	if (reg_val & (1<<22))
		printk("DErrAddr0: 0x%0*lx\n", field, read_c0_derraddr0());

	if (reg_val & (1<<23))
		printk("DErrAddr1: 0x%0*lx\n", field, read_c0_derraddr1());
#endif

	panic("Can't handle the cache error!");
}

asmlinkage void do_ftlb(void)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int reg_val;

	/* For the moment, report the problem and hang. */
	if ((cpu_has_mips_r2_r6) &&
	    (((current_cpu_data.processor_id & 0xff0000) == PRID_COMP_MIPS) ||
	    ((current_cpu_data.processor_id & 0xff0000) == PRID_COMP_LOONGSON))) {
		pr_err("FTLB error exception, cp0_ecc=0x%08x:\n",
		       read_c0_ecc());
		pr_err("cp0_errorepc == %0*lx\n", field, read_c0_errorepc());
		reg_val = read_c0_cacheerr();
		pr_err("c0_cacheerr == %08x\n", reg_val);

		if ((reg_val & 0xc0000000) == 0xc0000000) {
			pr_err("Decoded c0_cacheerr: FTLB parity error\n");
		} else {
			pr_err("Decoded c0_cacheerr: %s cache fault in %s reference.\n",
			       reg_val & (1<<30) ? "secondary" : "primary",
			       reg_val & (1<<31) ? "data" : "insn");
		}
	} else {
		pr_err("FTLB error exception\n");
	}
	/* Just print the cacheerr bits for now */
	cache_parity_error();
}

/*
 * SDBBP EJTAG debug exception handler.
 * We skip the instruction and return to the next instruction.
 */
void ejtag_exception_handler(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned long depc, old_epc, old_ra;
	unsigned int debug;

	printk(KERN_DEBUG "SDBBP EJTAG debug exception - not handled yet, just ignored!\n");
	depc = read_c0_depc();
	debug = read_c0_debug();
	printk(KERN_DEBUG "c0_depc = %0*lx, DEBUG = %08x\n", field, depc, debug);
	if (debug & 0x80000000) {
		/*
		 * In branch delay slot.
		 * We cheat a little bit here and use EPC to calculate the
		 * debug return address (DEPC). EPC is restored after the
		 * calculation.
		 */
		old_epc = regs->cp0_epc;
		old_ra = regs->regs[31];
		regs->cp0_epc = depc;
		compute_return_epc(regs);
		depc = regs->cp0_epc;
		regs->cp0_epc = old_epc;
		regs->regs[31] = old_ra;
	} else
		depc += 4;
	write_c0_depc(depc);

#if 0
	printk(KERN_DEBUG "\n\n----- Enable EJTAG single stepping ----\n\n");
	write_c0_debug(debug | 0x100);
#endif
}

/*
 * NMI exception handler.
 * No lock; only written during early bootup by CPU 0.
 */
static RAW_NOTIFIER_HEAD(nmi_chain);

int register_nmi_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&nmi_chain, nb);
}

void __noreturn nmi_exception_handler(struct pt_regs *regs)
{
	char str[100];

	nmi_enter();
	raw_notifier_call_chain(&nmi_chain, 0, regs);
	bust_spinlocks(1);
	snprintf(str, 100, "CPU%d NMI taken, CP0_EPC=%lx\n",
		 smp_processor_id(), regs->cp0_epc);
	regs->cp0_epc = read_c0_errorepc();
	die(str, regs);
	nmi_exit();
}

#define VECTORSPACING 0x100	/* for EI/VI mode */

unsigned long ebase;
EXPORT_SYMBOL_GPL(ebase);
unsigned long exception_handlers[32];
unsigned long vi_handlers[64];

void __init *set_except_vector(int n, void *addr)
{
	unsigned long handler = (unsigned long) addr;
	unsigned long old_handler;

#ifdef CONFIG_CPU_MICROMIPS
	/*
	 * Only the TLB handlers are cache aligned with an even
	 * address. All other handlers are on an odd address and
	 * require no modification. Otherwise, MIPS32 mode will
	 * be entered when handling any TLB exceptions. That
	 * would be bad...since we must stay in microMIPS mode.
	 */
	if (!(handler & 0x1))
		handler |= 1;
#endif
	old_handler = xchg(&exception_handlers[n], handler);

	if (n == 0 && cpu_has_divec) {
#ifdef CONFIG_CPU_MICROMIPS
		unsigned long jump_mask = ~((1 << 27) - 1);
#else
		unsigned long jump_mask = ~((1 << 28) - 1);
#endif
		u32 *buf = (u32 *)(ebase + 0x200);
		unsigned int k0 = 26;
		if ((handler & jump_mask) == ((ebase + 0x200) & jump_mask)) {
			uasm_i_j(&buf, handler & ~jump_mask);
			uasm_i_nop(&buf);
		} else {
			UASM_i_LA(&buf, k0, handler);
			uasm_i_jr(&buf, k0);
			uasm_i_nop(&buf);
		}
		local_flush_icache_range(ebase + 0x200, (unsigned long)buf);
	}
	return (void *)old_handler;
}

static void do_default_vi(void)
{
	show_regs(get_irq_regs());
	panic("Caught unexpected vectored interrupt.");
}

static void *set_vi_srs_handler(int n, vi_handler_t addr, int srs)
{
	unsigned long handler;
	unsigned long old_handler = vi_handlers[n];
	int srssets = current_cpu_data.srsets;
	u16 *h;
	unsigned char *b;

	BUG_ON(!cpu_has_veic && !cpu_has_vint);

	if (addr == NULL) {
		handler = (unsigned long) do_default_vi;
		srs = 0;
	} else
		handler = (unsigned long) addr;
	vi_handlers[n] = handler;

	b = (unsigned char *)(ebase + 0x200 + n*VECTORSPACING);

	if (srs >= srssets)
		panic("Shadow register set %d not supported", srs);

	if (cpu_has_veic) {
		if (board_bind_eic_interrupt)
			board_bind_eic_interrupt(n, srs);
	} else if (cpu_has_vint) {
		/* SRSMap is only defined if shadow sets are implemented */
		if (srssets > 1)
			change_c0_srsmap(0xf << n*4, srs << n*4);
	}

	if (srs == 0) {
		/*
		 * If no shadow set is selected then use the default handler
		 * that does normal register saving and standard interrupt exit
		 */
		extern char except_vec_vi, except_vec_vi_lui;
		extern char except_vec_vi_ori, except_vec_vi_end;
		extern char rollback_except_vec_vi;
		char *vec_start = using_rollback_handler() ?
			&rollback_except_vec_vi : &except_vec_vi;
#if defined(CONFIG_CPU_MICROMIPS) || defined(CONFIG_CPU_BIG_ENDIAN)
		const int lui_offset = &except_vec_vi_lui - vec_start + 2;
		const int ori_offset = &except_vec_vi_ori - vec_start + 2;
#else
		const int lui_offset = &except_vec_vi_lui - vec_start;
		const int ori_offset = &except_vec_vi_ori - vec_start;
#endif
		const int handler_len = &except_vec_vi_end - vec_start;

		if (handler_len > VECTORSPACING) {
			/*
			 * Sigh... panicing won't help as the console
			 * is probably not configured :(
			 */
			panic("VECTORSPACING too small");
		}

		set_handler(((unsigned long)b - ebase), vec_start,
#ifdef CONFIG_CPU_MICROMIPS
				(handler_len - 1));
#else
				handler_len);
#endif
		h = (u16 *)(b + lui_offset);
		*h = (handler >> 16) & 0xffff;
		h = (u16 *)(b + ori_offset);
		*h = (handler & 0xffff);
		local_flush_icache_range((unsigned long)b,
					 (unsigned long)(b+handler_len));
	}
	else {
		/*
		 * In other cases jump directly to the interrupt handler. It
		 * is the handler's responsibility to save registers if required
		 * (eg hi/lo) and return from the exception using "eret".
		 */
		u32 insn;

		h = (u16 *)b;
		/* j handler */
#ifdef CONFIG_CPU_MICROMIPS
		insn = 0xd4000000 | (((u32)handler & 0x07ffffff) >> 1);
#else
		insn = 0x08000000 | (((u32)handler & 0x0fffffff) >> 2);
#endif
		h[0] = (insn >> 16) & 0xffff;
		h[1] = insn & 0xffff;
		h[2] = 0;
		h[3] = 0;
		local_flush_icache_range((unsigned long)b,
					 (unsigned long)(b+8));
	}

	return (void *)old_handler;
}

void *set_vi_handler(int n, vi_handler_t addr)
{
	return set_vi_srs_handler(n, addr, 0);
}

extern void tlb_init(void);

/*
 * Timer interrupt
 */
int cp0_compare_irq;
EXPORT_SYMBOL_GPL(cp0_compare_irq);
int cp0_compare_irq_shift;

/*
 * Performance counter IRQ or -1 if shared with timer
 */
int cp0_perfcount_irq;
EXPORT_SYMBOL_GPL(cp0_perfcount_irq);

/*
 * Fast debug channel IRQ or -1 if not present
 */
int cp0_fdc_irq;
EXPORT_SYMBOL_GPL(cp0_fdc_irq);

static int noulri;

static int __init ulri_disable(char *s)
{
	pr_info("Disabling ulri\n");
	noulri = 1;

	return 1;
}
__setup("noulri", ulri_disable);

/* configure STATUS register */
static void configure_status(void)
{
	/*
	 * Disable coprocessors and select 32-bit or 64-bit addressing
	 * and the 16/32 or 32/32 FPR register model.  Reset the BEV
	 * flag that some firmware may have left set and the TS bit (for
	 * IP27).  Set XX for ISA IV code to work.
	 */
	unsigned int status_set = ST0_CU0;
#ifdef CONFIG_64BIT
	status_set |= ST0_FR|ST0_KX|ST0_SX|ST0_UX;
#endif
	if (current_cpu_data.isa_level & MIPS_CPU_ISA_IV)
		status_set |= ST0_XX;
	if (cpu_has_dsp)
		status_set |= ST0_MX;

	change_c0_status(ST0_CU|ST0_MX|ST0_RE|ST0_FR|ST0_BEV|ST0_TS|ST0_KX|ST0_SX|ST0_UX,
			 status_set);
}

unsigned int hwrena;
EXPORT_SYMBOL_GPL(hwrena);

/* configure HWRENA register */
static void configure_hwrena(void)
{
	hwrena = cpu_hwrena_impl_bits;

	if (cpu_has_mips_r2_r6)
		hwrena |= MIPS_HWRENA_CPUNUM |
			  MIPS_HWRENA_SYNCISTEP |
			  MIPS_HWRENA_CC |
			  MIPS_HWRENA_CCRES;

	if (!noulri && cpu_has_userlocal)
		hwrena |= MIPS_HWRENA_ULR;

	if (hwrena)
		write_c0_hwrena(hwrena);
}

static void configure_exception_vector(void)
{
	if (cpu_has_veic || cpu_has_vint) {
		unsigned long sr = set_c0_status(ST0_BEV);
		write_c0_ebase(ebase);
		write_c0_status(sr);
		/* Setting vector spacing enables EI/VI mode  */
		change_c0_intctl(0x3e0, VECTORSPACING);
	}
	if (cpu_has_divec) {
		if (cpu_has_mipsmt) {
			unsigned int vpflags = dvpe();
			set_c0_cause(CAUSEF_IV);
			evpe(vpflags);
		} else
			set_c0_cause(CAUSEF_IV);
	}
}

void per_cpu_trap_init(bool is_boot_cpu)
{
	unsigned int cpu = smp_processor_id();

	configure_status();
	configure_hwrena();

	configure_exception_vector();

	/*
	 * Before R2 both interrupt numbers were fixed to 7, so on R2 only:
	 *
	 *  o read IntCtl.IPTI to determine the timer interrupt
	 *  o read IntCtl.IPPCI to determine the performance counter interrupt
	 *  o read IntCtl.IPFDC to determine the fast debug channel interrupt
	 */
	if (cpu_has_mips_r2_r6) {
		/*
		 * We shouldn't trust a secondary core has a sane EBASE register
		 * so use the one calculated by the boot CPU.
		 */
		if (!is_boot_cpu)
			write_c0_ebase(ebase);

		cp0_compare_irq_shift = CAUSEB_TI - CAUSEB_IP;
		cp0_compare_irq = (read_c0_intctl() >> INTCTLB_IPTI) & 7;
		cp0_perfcount_irq = (read_c0_intctl() >> INTCTLB_IPPCI) & 7;
		cp0_fdc_irq = (read_c0_intctl() >> INTCTLB_IPFDC) & 7;
		if (!cp0_fdc_irq)
			cp0_fdc_irq = -1;

	} else {
		cp0_compare_irq = CP0_LEGACY_COMPARE_IRQ;
		cp0_compare_irq_shift = CP0_LEGACY_PERFCNT_IRQ;
		cp0_perfcount_irq = -1;
		cp0_fdc_irq = -1;
	}

	if (!cpu_data[cpu].asid_cache)
		cpu_data[cpu].asid_cache = asid_first_version(cpu);

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);

	/* Boot CPU's cache setup in setup_arch(). */
	if (!is_boot_cpu)
		cpu_cache_init();
	tlb_init();
	TLBMISS_HANDLER_SETUP();
}

/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
#ifdef CONFIG_CPU_MICROMIPS
	memcpy((void *)(ebase + offset), ((unsigned char *)addr - 1), size);
#else
	memcpy((void *)(ebase + offset), addr, size);
#endif
	local_flush_icache_range(ebase + offset, ebase + offset + size);
}

static char panic_null_cerr[] =
	"Trying to set NULL cache error exception handler";

/*
 * Install uncached CPU exception handler.
 * This is suitable only for the cache error exception which is the only
 * exception handler that is being run uncached.
 */
void set_uncached_handler(unsigned long offset, void *addr,
	unsigned long size)
{
	unsigned long uncached_ebase = CKSEG1ADDR(ebase);

	if (!addr)
		panic(panic_null_cerr);

	memcpy((void *)(uncached_ebase + offset), addr, size);
}

static int __initdata rdhwr_noopt;
static int __init set_rdhwr_noopt(char *str)
{
	rdhwr_noopt = 1;
	return 1;
}

__setup("rdhwr_noopt", set_rdhwr_noopt);

void __init trap_init(void)
{
	extern char except_vec3_generic;
	extern char except_vec4;
	extern char except_vec3_r4000;
	unsigned long i;

	check_wait();

	if (cpu_has_veic || cpu_has_vint) {
		unsigned long size = 0x200 + VECTORSPACING*64;
		ebase = (unsigned long)
			__alloc_bootmem(size, 1 << fls(size), 0);
	} else {
		ebase = CAC_BASE;

		if (cpu_has_mips_r2_r6)
			ebase += (read_c0_ebase() & 0x3ffff000);
	}

	if (cpu_has_mmips) {
		unsigned int config3 = read_c0_config3();

		if (IS_ENABLED(CONFIG_CPU_MICROMIPS))
			write_c0_config3(config3 | MIPS_CONF3_ISA_OE);
		else
			write_c0_config3(config3 & ~MIPS_CONF3_ISA_OE);
	}

	if (board_ebase_setup)
		board_ebase_setup();
	per_cpu_trap_init(true);

	/*
	 * Copy the generic exception handlers to their final destination.
	 * This will be overridden later as suitable for a particular
	 * configuration.
	 */
	set_handler(0x180, &except_vec3_generic, 0x80);

	/*
	 * Setup default vectors
	 */
	for (i = 0; i <= 31; i++)
		set_except_vector(i, handle_reserved);

	/*
	 * Copy the EJTAG debug exception vector handler code to it's final
	 * destination.
	 */
	if (cpu_has_ejtag && board_ejtag_handler_setup)
		board_ejtag_handler_setup();

	/*
	 * Only some CPUs have the watch exceptions.
	 */
	if (cpu_has_watch)
		set_except_vector(EXCCODE_WATCH, handle_watch);

	/*
	 * Initialise interrupt handlers
	 */
	if (cpu_has_veic || cpu_has_vint) {
		int nvec = cpu_has_veic ? 64 : 8;
		for (i = 0; i < nvec; i++)
			set_vi_handler(i, NULL);
	}
	else if (cpu_has_divec)
		set_handler(0x200, &except_vec4, 0x8);

	/*
	 * Some CPUs can enable/disable for cache parity detection, but does
	 * it different ways.
	 */
	parity_protection_init();

	/*
	 * The Data Bus Errors / Instruction Bus Errors are signaled
	 * by external hardware.  Therefore these two exceptions
	 * may have board specific handlers.
	 */
	if (board_be_init)
		board_be_init();

	set_except_vector(EXCCODE_INT, using_rollback_handler() ?
					rollback_handle_int : handle_int);
	set_except_vector(EXCCODE_MOD, handle_tlbm);
	set_except_vector(EXCCODE_TLBL, handle_tlbl);
	set_except_vector(EXCCODE_TLBS, handle_tlbs);

	set_except_vector(EXCCODE_ADEL, handle_adel);
	set_except_vector(EXCCODE_ADES, handle_ades);

	set_except_vector(EXCCODE_IBE, handle_ibe);
	set_except_vector(EXCCODE_DBE, handle_dbe);

	set_except_vector(EXCCODE_SYS, handle_sys);
	set_except_vector(EXCCODE_BP, handle_bp);
	set_except_vector(EXCCODE_RI, rdhwr_noopt ? handle_ri :
			  (cpu_has_vtag_icache ?
			   handle_ri_rdhwr_vivt : handle_ri_rdhwr));
	set_except_vector(EXCCODE_CPU, handle_cpu);
	set_except_vector(EXCCODE_OV, handle_ov);
	set_except_vector(EXCCODE_TR, handle_tr);
	set_except_vector(EXCCODE_MSAFPE, handle_msa_fpe);

	if (current_cpu_type() == CPU_R6000 ||
	    current_cpu_type() == CPU_R6000A) {
		/*
		 * The R6000 is the only R-series CPU that features a machine
		 * check exception (similar to the R4000 cache error) and
		 * unaligned ldc1/sdc1 exception.  The handlers have not been
		 * written yet.	 Well, anyway there is no R6000 machine on the
		 * current list of targets for Linux/MIPS.
		 * (Duh, crap, there is someone with a triple R6k machine)
		 */
		//set_except_vector(14, handle_mc);
		//set_except_vector(15, handle_ndc);
	}


	if (board_nmi_handler_setup)
		board_nmi_handler_setup();

	if (cpu_has_fpu && !cpu_has_nofpuex)
		set_except_vector(EXCCODE_FPE, handle_fpe);

	set_except_vector(MIPS_EXCCODE_TLBPAR, handle_ftlb);

	if (cpu_has_rixiex) {
		set_except_vector(EXCCODE_TLBRI, tlb_do_page_fault_0);
		set_except_vector(EXCCODE_TLBXI, tlb_do_page_fault_0);
	}

	set_except_vector(EXCCODE_MSADIS, handle_msa);
	set_except_vector(EXCCODE_MDMX, handle_mdmx);

	if (cpu_has_mcheck)
		set_except_vector(EXCCODE_MCHECK, handle_mcheck);

	if (cpu_has_mipsmt)
		set_except_vector(EXCCODE_THREAD, handle_mt);

	set_except_vector(EXCCODE_DSPDIS, handle_dsp);

	if (board_cache_error_setup)
		board_cache_error_setup();

	if (cpu_has_vce)
		/* Special exception: R4[04]00 uses also the divec space. */
		set_handler(0x180, &except_vec3_r4000, 0x100);
	else if (cpu_has_4kex)
		set_handler(0x180, &except_vec3_generic, 0x80);
	else
		set_handler(0x080, &except_vec3_generic, 0x80);

	local_flush_icache_range(ebase, ebase + 0x400);

	sort_extable(__start___dbe_table, __stop___dbe_table);

	cu2_notifier(default_cu2_call, 0x80000000);	/* Run last  */
}

static int trap_pm_notifier(struct notifier_block *self, unsigned long cmd,
			    void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		configure_status();
		configure_hwrena();
		configure_exception_vector();

		/* Restore register with CPU number for TLB handlers */
		TLBMISS_HANDLER_RESTORE();

		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block trap_pm_notifier_block = {
	.notifier_call = trap_pm_notifier,
};

static int __init trap_pm_init(void)
{
	return cpu_pm_register_notifier(&trap_pm_notifier_block);
}
arch_initcall(trap_pm_init);
