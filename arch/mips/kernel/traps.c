/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 01 Ralf Baechle
 * Copyright (C) 1995, 1996 Paul M. Antoine
 * Copyright (C) 1998 Ulf Carlsson
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000, 01 MIPS Technologies, Inc.
 * Copyright (C) 2002, 2003, 2004, 2005  Maciej W. Rozycki
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/bootmem.h>

#include <asm/bootinfo.h>
#include <asm/branch.h>
#include <asm/break.h>
#include <asm/cpu.h>
#include <asm/dsp.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/module.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/tlbdebug.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/watch.h>
#include <asm/types.h>

extern asmlinkage void handle_tlbm(void);
extern asmlinkage void handle_tlbl(void);
extern asmlinkage void handle_tlbs(void);
extern asmlinkage void handle_adel(void);
extern asmlinkage void handle_ades(void);
extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_cpu(void);
extern asmlinkage void handle_ov(void);
extern asmlinkage void handle_tr(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_mdmx(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_mt(void);
extern asmlinkage void handle_dsp(void);
extern asmlinkage void handle_mcheck(void);
extern asmlinkage void handle_reserved(void);

extern int fpu_emulator_cop1Handler(struct pt_regs *xcp,
	struct mips_fpu_soft_struct *ctx);

void (*board_be_init)(void);
int (*board_be_handler)(struct pt_regs *regs, int is_fixup);
void (*board_nmi_handler_setup)(void);
void (*board_ejtag_handler_setup)(void);
void (*board_bind_eic_interrupt)(int irq, int regset);

/*
 * These constant is for searching for possible module text segments.
 * MODULE_RANGE is a guess of how much space is likely to be vmalloced.
 */
#define MODULE_RANGE (8*1024*1024)

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */
void show_stack(struct task_struct *task, unsigned long *sp)
{
	const int field = 2 * sizeof(unsigned long);
	long stackdata;
	int i;

	if (!sp) {
		if (task && task != current)
			sp = (unsigned long *) task->thread.reg29;
		else
			sp = (unsigned long *) &sp;
	}

	printk("Stack :");
	i = 0;
	while ((unsigned long) sp & (PAGE_SIZE - 1)) {
		if (i && ((i % (64 / field)) == 0))
			printk("\n       ");
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
}

void show_trace(struct task_struct *task, unsigned long *stack)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned long addr;

	if (!stack) {
		if (task && task != current)
			stack = (unsigned long *) task->thread.reg29;
		else
			stack = (unsigned long *) &stack;
	}

	printk("Call Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	while (!kstack_end(stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
			printk(" [<%0*lx>] ", field, addr);
			print_symbol("%s\n", addr);
		}
	}
	printk("\n");
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(current, &stack);
}

EXPORT_SYMBOL(dump_stack);

void show_code(unsigned int *pc)
{
	long i;

	printk("\nCode:");

	for(i = -3 ; i < 6 ; i++) {
		unsigned int insn;
		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in epc)\n");
			break;
		}
		printk("%c%08x%c", (i?' ':'<'), insn, (i?' ':'>'));
	}
}

void show_regs(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int cause = regs->cp0_cause;
	int i;

	printk("Cpu %d\n", smp_processor_id());

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

	printk("Hi    : %0*lx\n", field, regs->hi);
	printk("Lo    : %0*lx\n", field, regs->lo);

	/*
	 * Saved cp0 registers
	 */
	printk("epc   : %0*lx ", field, regs->cp0_epc);
	print_symbol("%s ", regs->cp0_epc);
	printk("    %s\n", print_tainted());
	printk("ra    : %0*lx ", field, regs->regs[31]);
	print_symbol("%s\n", regs->regs[31]);

	printk("Status: %08x    ", (uint32_t) regs->cp0_status);

	if (current_cpu_data.isa_level == MIPS_CPU_ISA_I) {
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
	} else {
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

	printk("Cause : %08x\n", cause);

	cause = (cause & CAUSEF_EXCCODE) >> CAUSEB_EXCCODE;
	if (1 <= cause && cause <= 5)
		printk("BadVA : %0*lx\n", field, regs->cp0_badvaddr);

	printk("PrId  : %08x\n", read_c0_prid());
}

void show_registers(struct pt_regs *regs)
{
	show_regs(regs);
	print_modules();
	printk("Process %s (pid: %d, threadinfo=%p, task=%p)\n",
	        current->comm, current->pid, current_thread_info(), current);
	show_stack(current, (long *) regs->regs[29]);
	show_trace(current, (long *) regs->regs[29]);
	show_code((unsigned int *) regs->cp0_epc);
	printk("\n");
}

static DEFINE_SPINLOCK(die_lock);

NORET_TYPE void ATTRIB_NORET die(const char * str, struct pt_regs * regs)
{
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	printk("%s[#%d]:\n", str, ++die_counter);
	show_registers(regs);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

extern const struct exception_table_entry __start___dbe_table[];
extern const struct exception_table_entry __stop___dbe_table[];

void __declare_dbe_table(void)
{
	__asm__ __volatile__(
	".section\t__dbe_table,\"a\"\n\t"
	".previous"
	);
}

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

	/* XXX For now.  Fixme, this searches the wrong table ...  */
	if (data && !user_mode(regs))
		fixup = search_dbe_tables(exception_epc(regs));

	if (fixup)
		action = MIPS_BE_FIXUP;

	if (board_be_handler)
		action = board_be_handler(regs, fixup != 0);

	switch (action) {
	case MIPS_BE_DISCARD:
		return;
	case MIPS_BE_FIXUP:
		if (fixup) {
			regs->cp0_epc = fixup->nextinsn;
			return;
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
	die_if_kernel("Oops", regs);
	force_sig(SIGBUS, current);
}

static inline int get_insn_opcode(struct pt_regs *regs, unsigned int *opcode)
{
	unsigned int __user *epc;

	epc = (unsigned int __user *) regs->cp0_epc +
	      ((regs->cp0_cause & CAUSEF_BD) != 0);
	if (!get_user(*opcode, epc))
		return 0;

	force_sig(SIGSEGV, current);
	return 1;
}

/*
 * ll/sc emulation
 */

#define OPCODE 0xfc000000
#define BASE   0x03e00000
#define RT     0x001f0000
#define OFFSET 0x0000ffff
#define LL     0xc0000000
#define SC     0xe0000000
#define SPEC3  0x7c000000
#define RD     0x0000f800
#define FUNC   0x0000003f
#define RDHWR  0x0000003b

/*
 * The ll_bit is cleared by r*_switch.S
 */

unsigned long ll_bit;

static struct task_struct *ll_task = NULL;

static inline void simulate_ll(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long value, __user *vaddr;
	long offset;
	int signal = 0;

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

	if ((unsigned long)vaddr & 3) {
		signal = SIGBUS;
		goto sig;
	}
	if (get_user(value, vaddr)) {
		signal = SIGSEGV;
		goto sig;
	}

	preempt_disable();

	if (ll_task == NULL || ll_task == current) {
		ll_bit = 1;
	} else {
		ll_bit = 0;
	}
	ll_task = current;

	preempt_enable();

	compute_return_epc(regs);

	regs->regs[(opcode & RT) >> 16] = value;

	return;

sig:
	force_sig(signal, current);
}

static inline void simulate_sc(struct pt_regs *regs, unsigned int opcode)
{
	unsigned long __user *vaddr;
	unsigned long reg;
	long offset;
	int signal = 0;

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

	if ((unsigned long)vaddr & 3) {
		signal = SIGBUS;
		goto sig;
	}

	preempt_disable();

	if (ll_bit == 0 || ll_task != current) {
		compute_return_epc(regs);
		regs->regs[reg] = 0;
		preempt_enable();
		return;
	}

	preempt_enable();

	if (put_user(regs->regs[reg], vaddr)) {
		signal = SIGSEGV;
		goto sig;
	}

	compute_return_epc(regs);
	regs->regs[reg] = 1;

	return;

sig:
	force_sig(signal, current);
}

/*
 * ll uses the opcode of lwc0 and sc uses the opcode of swc0.  That is both
 * opcodes are supposed to result in coprocessor unusable exceptions if
 * executed on ll/sc-less processors.  That's the theory.  In practice a
 * few processors such as NEC's VR4100 throw reserved instruction exceptions
 * instead, so we're doing the emulation thing in both exception handlers.
 */
static inline int simulate_llsc(struct pt_regs *regs)
{
	unsigned int opcode;

	if (unlikely(get_insn_opcode(regs, &opcode)))
		return -EFAULT;

	if ((opcode & OPCODE) == LL) {
		simulate_ll(regs, opcode);
		return 0;
	}
	if ((opcode & OPCODE) == SC) {
		simulate_sc(regs, opcode);
		return 0;
	}

	return -EFAULT;			/* Strange things going on ... */
}

/*
 * Simulate trapping 'rdhwr' instructions to provide user accessible
 * registers not implemented in hardware.  The only current use of this
 * is the thread area pointer.
 */
static inline int simulate_rdhwr(struct pt_regs *regs)
{
	struct thread_info *ti = current->thread_info;
	unsigned int opcode;

	if (unlikely(get_insn_opcode(regs, &opcode)))
		return -EFAULT;

	if (unlikely(compute_return_epc(regs)))
		return -EFAULT;

	if ((opcode & OPCODE) == SPEC3 && (opcode & FUNC) == RDHWR) {
		int rd = (opcode & RD) >> 11;
		int rt = (opcode & RT) >> 16;
		switch (rd) {
			case 29:
				regs->regs[rt] = ti->tp_value;
				break;
			default:
				return -EFAULT;
		}
	}

	return 0;
}

asmlinkage void do_ov(struct pt_regs *regs)
{
	siginfo_t info;

	info.si_code = FPE_INTOVF;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void __user *) regs->cp0_epc;
	force_sig_info(SIGFPE, &info, current);
}

/*
 * XXX Delayed fp exceptions when doing a lazy ctx switch XXX
 */
asmlinkage void do_fpe(struct pt_regs *regs, unsigned long fcr31)
{
	if (fcr31 & FPU_CSR_UNI_X) {
		int sig;

		preempt_disable();

#ifdef CONFIG_PREEMPT
		if (!is_fpu_owner()) {
			/* We might lose fpu before disabling preempt... */
			own_fpu();
			BUG_ON(!used_math());
			restore_fp(current);
		}
#endif
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
		save_fp(current);
		/* Ensure 'resume' not overwrite saved fp context again. */
		lose_fpu();

		preempt_enable();

		/* Run the emulator */
		sig = fpu_emulator_cop1Handler (regs,
			&current->thread.fpu.soft);

		preempt_disable();

		own_fpu();	/* Using the FPU again.  */
		/*
		 * We can't allow the emulated instruction to leave any of
		 * the cause bit set in $fcr31.
		 */
		current->thread.fpu.soft.fcr31 &= ~FPU_CSR_ALL_X;

		/* Restore the hardware register state */
		restore_fp(current);

		preempt_enable();

		/* If something went wrong, signal */
		if (sig)
			force_sig(sig, current);

		return;
	}

	force_sig(SIGFPE, current);
}

asmlinkage void do_bp(struct pt_regs *regs)
{
	unsigned int opcode, bcode;
	siginfo_t info;

	die_if_kernel("Break instruction in kernel code", regs);

	if (get_insn_opcode(regs, &opcode))
		return;

	/*
	 * There is the ancient bug in the MIPS assemblers that the break
	 * code starts left to bit 16 instead to bit 6 in the opcode.
	 * Gas is bug-compatible, but not always, grrr...
	 * We handle both cases with a simple heuristics.  --macro
	 */
	bcode = ((opcode >> 6) & ((1 << 20) - 1));
	if (bcode < (1 << 10))
		bcode <<= 10;

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all break
	 * insns, even for break codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (bcode) {
	case BRK_OVERFLOW << 10:
	case BRK_DIVZERO << 10:
		if (bcode == (BRK_DIVZERO << 10))
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void __user *) regs->cp0_epc;
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
}

asmlinkage void do_tr(struct pt_regs *regs)
{
	unsigned int opcode, tcode = 0;
	siginfo_t info;

	die_if_kernel("Trap instruction in kernel code", regs);

	if (get_insn_opcode(regs, &opcode))
		return;

	/* Immediate versions don't provide a code.  */
	if (!(opcode & OPCODE))
		tcode = ((opcode >> 6) & ((1 << 10) - 1));

	/*
	 * (A short test says that IRIX 5.3 sends SIGTRAP for all trap
	 * insns, even for trap codes that indicate arithmetic failures.
	 * Weird ...)
	 * But should we continue the brokenness???  --macro
	 */
	switch (tcode) {
	case BRK_OVERFLOW:
	case BRK_DIVZERO:
		if (tcode == BRK_DIVZERO)
			info.si_code = FPE_INTDIV;
		else
			info.si_code = FPE_INTOVF;
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void __user *) regs->cp0_epc;
		force_sig_info(SIGFPE, &info, current);
		break;
	default:
		force_sig(SIGTRAP, current);
	}
}

asmlinkage void do_ri(struct pt_regs *regs)
{
	die_if_kernel("Reserved instruction in kernel code", regs);

	if (!cpu_has_llsc)
		if (!simulate_llsc(regs))
			return;

	if (!simulate_rdhwr(regs))
		return;

	force_sig(SIGILL, current);
}

asmlinkage void do_cpu(struct pt_regs *regs)
{
	unsigned int cpid;

	die_if_kernel("do_cpu invoked from kernel context!", regs);

	cpid = (regs->cp0_cause >> CAUSEB_CE) & 3;

	switch (cpid) {
	case 0:
		if (!cpu_has_llsc)
			if (!simulate_llsc(regs))
				return;

		if (!simulate_rdhwr(regs))
			return;

		break;

	case 1:
		preempt_disable();

		own_fpu();
		if (used_math()) {	/* Using the FPU again.  */
			restore_fp(current);
		} else {			/* First time FPU user.  */
			init_fpu();
			set_used_math();
		}

		preempt_enable();

		if (!cpu_has_fpu) {
			int sig = fpu_emulator_cop1Handler(regs,
						&current->thread.fpu.soft);
			if (sig)
				force_sig(sig, current);
		}

		return;

	case 2:
	case 3:
		break;
	}

	force_sig(SIGILL, current);
}

asmlinkage void do_mdmx(struct pt_regs *regs)
{
	force_sig(SIGILL, current);
}

asmlinkage void do_watch(struct pt_regs *regs)
{
	/*
	 * We use the watch exception where available to detect stack
	 * overflows.
	 */
	dump_tlb_all();
	show_regs(regs);
	panic("Caught WATCH exception - probably caused by stack overflow.");
}

asmlinkage void do_mcheck(struct pt_regs *regs)
{
	show_regs(regs);
	dump_tlb_all();
	/*
	 * Some chips may have other causes of machine check (e.g. SB1
	 * graduation timer)
	 */
	panic("Caught Machine Check exception - %scaused by multiple "
	      "matching entries in the TLB.",
	      (regs->cp0_status & ST0_TS) ? "" : "not ");
}

asmlinkage void do_mt(struct pt_regs *regs)
{
	die_if_kernel("MIPS MT Thread exception in kernel", regs);

	force_sig(SIGILL, current);
}


asmlinkage void do_dsp(struct pt_regs *regs)
{
	if (cpu_has_dsp)
		panic("Unexpected DSP exception\n");

	force_sig(SIGILL, current);
}

asmlinkage void do_reserved(struct pt_regs *regs)
{
	/*
	 * Game over - no way to handle this if it ever occurs.  Most probably
	 * caused by a new unknown cpu type or after another deadly
	 * hard/software error.
	 */
	show_regs(regs);
	panic("Caught reserved exception %ld - should not happen.",
	      (regs->cp0_cause & 0x7f) >> 2);
}

asmlinkage void do_default_vi(struct pt_regs *regs)
{
	show_regs(regs);
	panic("Caught unexpected vectored interrupt.");
}

/*
 * Some MIPS CPUs can enable/disable for cache parity detection, but do
 * it different ways.
 */
static inline void parity_protection_init(void)
{
	switch (current_cpu_data.cputype) {
	case CPU_24K:
	case CPU_5KC:
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
	printk("Error bits: %s%s%s%s%s%s%s\n",
	       reg_val & (1<<29) ? "ED " : "",
	       reg_val & (1<<28) ? "ET " : "",
	       reg_val & (1<<26) ? "EE " : "",
	       reg_val & (1<<25) ? "EB " : "",
	       reg_val & (1<<24) ? "EI " : "",
	       reg_val & (1<<23) ? "E1 " : "",
	       reg_val & (1<<22) ? "E0 " : "");
	printk("IDX: 0x%08x\n", reg_val & ((1<<22)-1));

#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
	if (reg_val & (1<<22))
		printk("DErrAddr0: 0x%0*lx\n", field, read_c0_derraddr0());

	if (reg_val & (1<<23))
		printk("DErrAddr1: 0x%0*lx\n", field, read_c0_derraddr1());
#endif

	panic("Can't handle the cache error!");
}

/*
 * SDBBP EJTAG debug exception handler.
 * We skip the instruction and return to the next instruction.
 */
void ejtag_exception_handler(struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned long depc, old_epc;
	unsigned int debug;

	printk("SDBBP EJTAG debug exception - not handled yet, just ignored!\n");
	depc = read_c0_depc();
	debug = read_c0_debug();
	printk("c0_depc = %0*lx, DEBUG = %08x\n", field, depc, debug);
	if (debug & 0x80000000) {
		/*
		 * In branch delay slot.
		 * We cheat a little bit here and use EPC to calculate the
		 * debug return address (DEPC). EPC is restored after the
		 * calculation.
		 */
		old_epc = regs->cp0_epc;
		regs->cp0_epc = depc;
		__compute_return_epc(regs);
		depc = regs->cp0_epc;
		regs->cp0_epc = old_epc;
	} else
		depc += 4;
	write_c0_depc(depc);

#if 0
	printk("\n\n----- Enable EJTAG single stepping ----\n\n");
	write_c0_debug(debug | 0x100);
#endif
}

/*
 * NMI exception handler.
 */
void nmi_exception_handler(struct pt_regs *regs)
{
	printk("NMI taken!!!!\n");
	die("NMI", regs);
	while(1) ;
}

#define VECTORSPACING 0x100	/* for EI/VI mode */

unsigned long ebase;
unsigned long exception_handlers[32];
unsigned long vi_handlers[64];

/*
 * As a side effect of the way this is implemented we're limited
 * to interrupt handlers in the address range from
 * KSEG0 <= x < KSEG0 + 256mb on the Nevada.  Oh well ...
 */
void *set_except_vector(int n, void *addr)
{
	unsigned long handler = (unsigned long) addr;
	unsigned long old_handler = exception_handlers[n];

	exception_handlers[n] = handler;
	if (n == 0 && cpu_has_divec) {
		*(volatile u32 *)(ebase + 0x200) = 0x08000000 |
		                                 (0x03ffffff & (handler >> 2));
		flush_icache_range(ebase + 0x200, ebase + 0x204);
	}
	return (void *)old_handler;
}

#ifdef CONFIG_CPU_MIPSR2
/*
 * Shadow register allocation
 * FIXME: SMP...
 */

/* MIPSR2 shadow register sets */
struct shadow_registers {
	spinlock_t sr_lock;	/*  */
	int sr_supported;	/* Number of shadow register sets supported */
	int sr_allocated;	/* Bitmap of allocated shadow registers */
} shadow_registers;

void mips_srs_init(void)
{
#ifdef CONFIG_CPU_MIPSR2_SRS
	shadow_registers.sr_supported = ((read_c0_srsctl() >> 26) & 0x0f) + 1;
	printk ("%d MIPSR2 register sets available\n", shadow_registers.sr_supported);
#else
	shadow_registers.sr_supported = 1;
#endif
	shadow_registers.sr_allocated = 1;	/* Set 0 used by kernel */
	spin_lock_init(&shadow_registers.sr_lock);
}

int mips_srs_max(void)
{
	return shadow_registers.sr_supported;
}

int mips_srs_alloc (void)
{
	struct shadow_registers *sr = &shadow_registers;
	unsigned long flags;
	int set;

	spin_lock_irqsave(&sr->sr_lock, flags);

	for (set = 0; set < sr->sr_supported; set++) {
		if ((sr->sr_allocated & (1 << set)) == 0) {
			sr->sr_allocated |= 1 << set;
			spin_unlock_irqrestore(&sr->sr_lock, flags);
			return set;
		}
	}

	/* None available */
	spin_unlock_irqrestore(&sr->sr_lock, flags);
	return -1;
}

void mips_srs_free (int set)
{
	struct shadow_registers *sr = &shadow_registers;
	unsigned long flags;

	spin_lock_irqsave(&sr->sr_lock, flags);
	sr->sr_allocated &= ~(1 << set);
	spin_unlock_irqrestore(&sr->sr_lock, flags);
}

void *set_vi_srs_handler (int n, void *addr, int srs)
{
	unsigned long handler;
	unsigned long old_handler = vi_handlers[n];
	u32 *w;
	unsigned char *b;

	if (!cpu_has_veic && !cpu_has_vint)
		BUG();

	if (addr == NULL) {
		handler = (unsigned long) do_default_vi;
		srs = 0;
	}
	else
		handler = (unsigned long) addr;
	vi_handlers[n] = (unsigned long) addr;

	b = (unsigned char *)(ebase + 0x200 + n*VECTORSPACING);

	if (srs >= mips_srs_max())
		panic("Shadow register set %d not supported", srs);

	if (cpu_has_veic) {
		if (board_bind_eic_interrupt)
			board_bind_eic_interrupt (n, srs);
	}
	else if (cpu_has_vint) {
		/* SRSMap is only defined if shadow sets are implemented */
		if (mips_srs_max() > 1)
			change_c0_srsmap (0xf << n*4, srs << n*4);
	}

	if (srs == 0) {
		/*
		 * If no shadow set is selected then use the default handler
		 * that does normal register saving and a standard interrupt exit
		 */

		extern char except_vec_vi, except_vec_vi_lui;
		extern char except_vec_vi_ori, except_vec_vi_end;
		const int handler_len = &except_vec_vi_end - &except_vec_vi;
		const int lui_offset = &except_vec_vi_lui - &except_vec_vi;
		const int ori_offset = &except_vec_vi_ori - &except_vec_vi;

		if (handler_len > VECTORSPACING) {
			/*
			 * Sigh... panicing won't help as the console
			 * is probably not configured :(
			 */
			panic ("VECTORSPACING too small");
		}

		memcpy (b, &except_vec_vi, handler_len);
		w = (u32 *)(b + lui_offset);
		*w = (*w & 0xffff0000) | (((u32)handler >> 16) & 0xffff);
		w = (u32 *)(b + ori_offset);
		*w = (*w & 0xffff0000) | ((u32)handler & 0xffff);
		flush_icache_range((unsigned long)b, (unsigned long)(b+handler_len));
	}
	else {
		/*
		 * In other cases jump directly to the interrupt handler
		 *
		 * It is the handlers responsibility to save registers if required
		 * (eg hi/lo) and return from the exception using "eret"
		 */
		w = (u32 *)b;
		*w++ = 0x08000000 | (((u32)handler >> 2) & 0x03fffff); /* j handler */
		*w = 0;
		flush_icache_range((unsigned long)b, (unsigned long)(b+8));
	}

	return (void *)old_handler;
}

void *set_vi_handler (int n, void *addr)
{
	return set_vi_srs_handler (n, addr, 0);
}
#endif

/*
 * This is used by native signal handling
 */
asmlinkage int (*save_fp_context)(struct sigcontext *sc);
asmlinkage int (*restore_fp_context)(struct sigcontext *sc);

extern asmlinkage int _save_fp_context(struct sigcontext *sc);
extern asmlinkage int _restore_fp_context(struct sigcontext *sc);

extern asmlinkage int fpu_emulator_save_context(struct sigcontext *sc);
extern asmlinkage int fpu_emulator_restore_context(struct sigcontext *sc);

static inline void signal_init(void)
{
	if (cpu_has_fpu) {
		save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;
	} else {
		save_fp_context = fpu_emulator_save_context;
		restore_fp_context = fpu_emulator_restore_context;
	}
}

#ifdef CONFIG_MIPS32_COMPAT

/*
 * This is used by 32-bit signal stuff on the 64-bit kernel
 */
asmlinkage int (*save_fp_context32)(struct sigcontext32 *sc);
asmlinkage int (*restore_fp_context32)(struct sigcontext32 *sc);

extern asmlinkage int _save_fp_context32(struct sigcontext32 *sc);
extern asmlinkage int _restore_fp_context32(struct sigcontext32 *sc);

extern asmlinkage int fpu_emulator_save_context32(struct sigcontext32 *sc);
extern asmlinkage int fpu_emulator_restore_context32(struct sigcontext32 *sc);

static inline void signal32_init(void)
{
	if (cpu_has_fpu) {
		save_fp_context32 = _save_fp_context32;
		restore_fp_context32 = _restore_fp_context32;
	} else {
		save_fp_context32 = fpu_emulator_save_context32;
		restore_fp_context32 = fpu_emulator_restore_context32;
	}
}
#endif

extern void cpu_cache_init(void);
extern void tlb_init(void);
extern void flush_tlb_handlers(void);

void __init per_cpu_trap_init(void)
{
	unsigned int cpu = smp_processor_id();
	unsigned int status_set = ST0_CU0;

	/*
	 * Disable coprocessors and select 32-bit or 64-bit addressing
	 * and the 16/32 or 32/32 FPR register model.  Reset the BEV
	 * flag that some firmware may have left set and the TS bit (for
	 * IP27).  Set XX for ISA IV code to work.
	 */
#ifdef CONFIG_64BIT
	status_set |= ST0_FR|ST0_KX|ST0_SX|ST0_UX;
#endif
	if (current_cpu_data.isa_level == MIPS_CPU_ISA_IV)
		status_set |= ST0_XX;
	change_c0_status(ST0_CU|ST0_MX|ST0_FR|ST0_BEV|ST0_TS|ST0_KX|ST0_SX|ST0_UX,
			 status_set);

	if (cpu_has_dsp)
		set_c0_status(ST0_MX);

#ifdef CONFIG_CPU_MIPSR2
	write_c0_hwrena (0x0000000f); /* Allow rdhwr to all registers */
#endif

	/*
	 * Interrupt handling.
	 */
	if (cpu_has_veic || cpu_has_vint) {
		write_c0_ebase (ebase);
		/* Setting vector spacing enables EI/VI mode  */
		change_c0_intctl (0x3e0, VECTORSPACING);
	}
	if (cpu_has_divec) {
		if (cpu_has_mipsmt) {
			unsigned int vpflags = dvpe();
			set_c0_cause(CAUSEF_IV);
			evpe(vpflags);
		} else
			set_c0_cause(CAUSEF_IV);
	}

	cpu_data[cpu].asid_cache = ASID_FIRST_VERSION;
	TLBMISS_HANDLER_SETUP();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);

	cpu_cache_init();
	tlb_init();
}

/* Install CPU exception handler */
void __init set_handler (unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(ebase + offset), addr, size);
	flush_icache_range(ebase + offset, ebase + offset + size);
}

/* Install uncached CPU exception handler */
void __init set_uncached_handler (unsigned long offset, void *addr, unsigned long size)
{
#ifdef CONFIG_32BIT
	unsigned long uncached_ebase = KSEG1ADDR(ebase);
#endif
#ifdef CONFIG_64BIT
	unsigned long uncached_ebase = TO_UNCAC(ebase);
#endif

	memcpy((void *)(uncached_ebase + offset), addr, size);
}

void __init trap_init(void)
{
	extern char except_vec3_generic, except_vec3_r4000;
	extern char except_vec4;
	unsigned long i;

	if (cpu_has_veic || cpu_has_vint)
		ebase = (unsigned long) alloc_bootmem_low_pages (0x200 + VECTORSPACING*64);
	else
		ebase = CAC_BASE;

#ifdef CONFIG_CPU_MIPSR2
	mips_srs_init();
#endif

	per_cpu_trap_init();

	/*
	 * Copy the generic exception handlers to their final destination.
	 * This will be overriden later as suitable for a particular
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
		board_ejtag_handler_setup ();

	/*
	 * Only some CPUs have the watch exceptions.
	 */
	if (cpu_has_watch)
		set_except_vector(23, handle_watch);

	/*
	 * Initialise interrupt handlers
	 */
	if (cpu_has_veic || cpu_has_vint) {
		int nvec = cpu_has_veic ? 64 : 8;
		for (i = 0; i < nvec; i++)
			set_vi_handler (i, NULL);
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

	set_except_vector(1, handle_tlbm);
	set_except_vector(2, handle_tlbl);
	set_except_vector(3, handle_tlbs);

	set_except_vector(4, handle_adel);
	set_except_vector(5, handle_ades);

	set_except_vector(6, handle_ibe);
	set_except_vector(7, handle_dbe);

	set_except_vector(8, handle_sys);
	set_except_vector(9, handle_bp);
	set_except_vector(10, handle_ri);
	set_except_vector(11, handle_cpu);
	set_except_vector(12, handle_ov);
	set_except_vector(13, handle_tr);

	if (current_cpu_data.cputype == CPU_R6000 ||
	    current_cpu_data.cputype == CPU_R6000A) {
		/*
		 * The R6000 is the only R-series CPU that features a machine
		 * check exception (similar to the R4000 cache error) and
		 * unaligned ldc1/sdc1 exception.  The handlers have not been
		 * written yet.  Well, anyway there is no R6000 machine on the
		 * current list of targets for Linux/MIPS.
		 * (Duh, crap, there is someone with a triple R6k machine)
		 */
		//set_except_vector(14, handle_mc);
		//set_except_vector(15, handle_ndc);
	}


	if (board_nmi_handler_setup)
		board_nmi_handler_setup();

	if (cpu_has_fpu && !cpu_has_nofpuex)
		set_except_vector(15, handle_fpe);

	set_except_vector(22, handle_mdmx);

	if (cpu_has_mcheck)
		set_except_vector(24, handle_mcheck);

	if (cpu_has_mipsmt)
		set_except_vector(25, handle_mt);

	if (cpu_has_dsp)
		set_except_vector(26, handle_dsp);

	if (cpu_has_vce)
		/* Special exception: R4[04]00 uses also the divec space. */
		memcpy((void *)(CAC_BASE + 0x180), &except_vec3_r4000, 0x100);
	else if (cpu_has_4kex)
		memcpy((void *)(CAC_BASE + 0x180), &except_vec3_generic, 0x80);
	else
		memcpy((void *)(CAC_BASE + 0x080), &except_vec3_generic, 0x80);

	signal_init();
#ifdef CONFIG_MIPS32_COMPAT
	signal32_init();
#endif

	flush_icache_range(ebase, ebase + 0x400);
	flush_tlb_handlers();
}
