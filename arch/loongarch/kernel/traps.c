// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/module.h>
#include <linux/extable.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/debug.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/memblock.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/notifier.h>
#include <linux/irq.h>
#include <linux/perf_event.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/branch.h>
#include <asm/break.h>
#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/inst.h>
#include <asm/loongarch.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/siginfo.h>
#include <asm/stacktrace.h>
#include <asm/tlb.h>
#include <asm/types.h>
#include <asm/unwind.h>
#include <asm/uprobes.h>

#include "access-helper.h"

extern asmlinkage void handle_ade(void);
extern asmlinkage void handle_ale(void);
extern asmlinkage void handle_bce(void);
extern asmlinkage void handle_sys(void);
extern asmlinkage void handle_bp(void);
extern asmlinkage void handle_ri(void);
extern asmlinkage void handle_fpu(void);
extern asmlinkage void handle_fpe(void);
extern asmlinkage void handle_lbt(void);
extern asmlinkage void handle_lsx(void);
extern asmlinkage void handle_lasx(void);
extern asmlinkage void handle_reserved(void);
extern asmlinkage void handle_watch(void);
extern asmlinkage void handle_vint(void);

static void show_backtrace(struct task_struct *task, const struct pt_regs *regs,
			   const char *loglvl, bool user)
{
	unsigned long addr;
	struct unwind_state state;
	struct pt_regs *pregs = (struct pt_regs *)regs;

	if (!task)
		task = current;

	printk("%sCall Trace:", loglvl);
	for (unwind_start(&state, task, pregs);
	      !unwind_done(&state); unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		print_ip_sym(loglvl, addr);
	}
	printk("%s\n", loglvl);
}

static void show_stacktrace(struct task_struct *task,
	const struct pt_regs *regs, const char *loglvl, bool user)
{
	int i;
	const int field = 2 * sizeof(unsigned long);
	unsigned long stackdata;
	unsigned long *sp = (unsigned long *)regs->regs[3];

	printk("%sStack :", loglvl);
	i = 0;
	while ((unsigned long) sp & (PAGE_SIZE - 1)) {
		if (i && ((i % (64 / field)) == 0)) {
			pr_cont("\n");
			printk("%s       ", loglvl);
		}
		if (i > 39) {
			pr_cont(" ...");
			break;
		}

		if (__get_addr(&stackdata, sp++, user)) {
			pr_cont(" (Bad stack address)");
			break;
		}

		pr_cont(" %0*lx", field, stackdata);
		i++;
	}
	pr_cont("\n");
	show_backtrace(task, regs, loglvl, user);
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	struct pt_regs regs;

	regs.csr_crmd = 0;
	if (sp) {
		regs.csr_era = 0;
		regs.regs[1] = 0;
		regs.regs[3] = (unsigned long)sp;
	} else {
		if (!task || task == current)
			prepare_frametrace(&regs);
		else {
			regs.csr_era = task->thread.reg01;
			regs.regs[1] = 0;
			regs.regs[3] = task->thread.reg03;
			regs.regs[22] = task->thread.reg22;
		}
	}

	show_stacktrace(task, &regs, loglvl, false);
}

static void show_code(unsigned int *pc, bool user)
{
	long i;
	unsigned int insn;

	printk("Code:");

	for(i = -3 ; i < 6 ; i++) {
		if (__get_inst(&insn, pc + i, user)) {
			pr_cont(" (Bad address in era)\n");
			break;
		}
		pr_cont("%c%08x%c", (i?' ':'<'), insn, (i?' ':'>'));
	}
	pr_cont("\n");
}

static void print_bool_fragment(const char *key, unsigned long val, bool first)
{
	/* e.g. "+PG", "-DA" */
	pr_cont("%s%c%s", first ? "" : " ", val ? '+' : '-', key);
}

static void print_plv_fragment(const char *key, int val)
{
	/* e.g. "PLV0", "PPLV3" */
	pr_cont("%s%d", key, val);
}

static void print_memory_type_fragment(const char *key, unsigned long val)
{
	const char *humanized_type;

	switch (val) {
	case 0:
		humanized_type = "SUC";
		break;
	case 1:
		humanized_type = "CC";
		break;
	case 2:
		humanized_type = "WUC";
		break;
	default:
		pr_cont(" %s=Reserved(%lu)", key, val);
		return;
	}

	/* e.g. " DATM=WUC" */
	pr_cont(" %s=%s", key, humanized_type);
}

static void print_intr_fragment(const char *key, unsigned long val)
{
	/* e.g. "LIE=0-1,3,5-7" */
	pr_cont("%s=%*pbl", key, EXCCODE_INT_NUM, &val);
}

static void print_crmd(unsigned long x)
{
	printk(" CRMD: %08lx (", x);
	print_plv_fragment("PLV", (int) FIELD_GET(CSR_CRMD_PLV, x));
	print_bool_fragment("IE", FIELD_GET(CSR_CRMD_IE, x), false);
	print_bool_fragment("DA", FIELD_GET(CSR_CRMD_DA, x), false);
	print_bool_fragment("PG", FIELD_GET(CSR_CRMD_PG, x), false);
	print_memory_type_fragment("DACF", FIELD_GET(CSR_CRMD_DACF, x));
	print_memory_type_fragment("DACM", FIELD_GET(CSR_CRMD_DACM, x));
	print_bool_fragment("WE", FIELD_GET(CSR_CRMD_WE, x), false);
	pr_cont(")\n");
}

static void print_prmd(unsigned long x)
{
	printk(" PRMD: %08lx (", x);
	print_plv_fragment("PPLV", (int) FIELD_GET(CSR_PRMD_PPLV, x));
	print_bool_fragment("PIE", FIELD_GET(CSR_PRMD_PIE, x), false);
	print_bool_fragment("PWE", FIELD_GET(CSR_PRMD_PWE, x), false);
	pr_cont(")\n");
}

static void print_euen(unsigned long x)
{
	printk(" EUEN: %08lx (", x);
	print_bool_fragment("FPE", FIELD_GET(CSR_EUEN_FPEN, x), true);
	print_bool_fragment("SXE", FIELD_GET(CSR_EUEN_LSXEN, x), false);
	print_bool_fragment("ASXE", FIELD_GET(CSR_EUEN_LASXEN, x), false);
	print_bool_fragment("BTE", FIELD_GET(CSR_EUEN_LBTEN, x), false);
	pr_cont(")\n");
}

static void print_ecfg(unsigned long x)
{
	printk(" ECFG: %08lx (", x);
	print_intr_fragment("LIE", FIELD_GET(CSR_ECFG_IM, x));
	pr_cont(" VS=%d)\n", (int) FIELD_GET(CSR_ECFG_VS, x));
}

static const char *humanize_exc_name(unsigned int ecode, unsigned int esubcode)
{
	/*
	 * LoongArch users and developers are probably more familiar with
	 * those names found in the ISA manual, so we are going to print out
	 * the latter. This will require some mapping.
	 */
	switch (ecode) {
	case EXCCODE_RSV: return "INT";
	case EXCCODE_TLBL: return "PIL";
	case EXCCODE_TLBS: return "PIS";
	case EXCCODE_TLBI: return "PIF";
	case EXCCODE_TLBM: return "PME";
	case EXCCODE_TLBNR: return "PNR";
	case EXCCODE_TLBNX: return "PNX";
	case EXCCODE_TLBPE: return "PPI";
	case EXCCODE_ADE:
		switch (esubcode) {
		case EXSUBCODE_ADEF: return "ADEF";
		case EXSUBCODE_ADEM: return "ADEM";
		}
		break;
	case EXCCODE_ALE: return "ALE";
	case EXCCODE_BCE: return "BCE";
	case EXCCODE_SYS: return "SYS";
	case EXCCODE_BP: return "BRK";
	case EXCCODE_INE: return "INE";
	case EXCCODE_IPE: return "IPE";
	case EXCCODE_FPDIS: return "FPD";
	case EXCCODE_LSXDIS: return "SXD";
	case EXCCODE_LASXDIS: return "ASXD";
	case EXCCODE_FPE:
		switch (esubcode) {
		case EXCSUBCODE_FPE: return "FPE";
		case EXCSUBCODE_VFPE: return "VFPE";
		}
		break;
	case EXCCODE_WATCH:
		switch (esubcode) {
		case EXCSUBCODE_WPEF: return "WPEF";
		case EXCSUBCODE_WPEM: return "WPEM";
		}
		break;
	case EXCCODE_BTDIS: return "BTD";
	case EXCCODE_BTE: return "BTE";
	case EXCCODE_GSPR: return "GSPR";
	case EXCCODE_HVC: return "HVC";
	case EXCCODE_GCM:
		switch (esubcode) {
		case EXCSUBCODE_GCSC: return "GCSC";
		case EXCSUBCODE_GCHC: return "GCHC";
		}
		break;
	/*
	 * The manual did not mention the EXCCODE_SE case, but print out it
	 * nevertheless.
	 */
	case EXCCODE_SE: return "SE";
	}

	return "???";
}

static void print_estat(unsigned long x)
{
	unsigned int ecode = FIELD_GET(CSR_ESTAT_EXC, x);
	unsigned int esubcode = FIELD_GET(CSR_ESTAT_ESUBCODE, x);

	printk("ESTAT: %08lx [%s] (", x, humanize_exc_name(ecode, esubcode));
	print_intr_fragment("IS", FIELD_GET(CSR_ESTAT_IS, x));
	pr_cont(" ECode=%d EsubCode=%d)\n", (int) ecode, (int) esubcode);
}

static void __show_regs(const struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	unsigned int exccode = FIELD_GET(CSR_ESTAT_EXC, regs->csr_estat);

	show_regs_print_info(KERN_DEFAULT);

	/* Print saved GPRs except $zero (substituting with PC/ERA) */
#define GPR_FIELD(x) field, regs->regs[x]
	printk("pc %0*lx ra %0*lx tp %0*lx sp %0*lx\n",
	       field, regs->csr_era, GPR_FIELD(1), GPR_FIELD(2), GPR_FIELD(3));
	printk("a0 %0*lx a1 %0*lx a2 %0*lx a3 %0*lx\n",
	       GPR_FIELD(4), GPR_FIELD(5), GPR_FIELD(6), GPR_FIELD(7));
	printk("a4 %0*lx a5 %0*lx a6 %0*lx a7 %0*lx\n",
	       GPR_FIELD(8), GPR_FIELD(9), GPR_FIELD(10), GPR_FIELD(11));
	printk("t0 %0*lx t1 %0*lx t2 %0*lx t3 %0*lx\n",
	       GPR_FIELD(12), GPR_FIELD(13), GPR_FIELD(14), GPR_FIELD(15));
	printk("t4 %0*lx t5 %0*lx t6 %0*lx t7 %0*lx\n",
	       GPR_FIELD(16), GPR_FIELD(17), GPR_FIELD(18), GPR_FIELD(19));
	printk("t8 %0*lx u0 %0*lx s9 %0*lx s0 %0*lx\n",
	       GPR_FIELD(20), GPR_FIELD(21), GPR_FIELD(22), GPR_FIELD(23));
	printk("s1 %0*lx s2 %0*lx s3 %0*lx s4 %0*lx\n",
	       GPR_FIELD(24), GPR_FIELD(25), GPR_FIELD(26), GPR_FIELD(27));
	printk("s5 %0*lx s6 %0*lx s7 %0*lx s8 %0*lx\n",
	       GPR_FIELD(28), GPR_FIELD(29), GPR_FIELD(30), GPR_FIELD(31));

	/* The slot for $zero is reused as the syscall restart flag */
	if (regs->regs[0])
		printk("syscall restart flag: %0*lx\n", GPR_FIELD(0));

	if (user_mode(regs)) {
		printk("   ra: %0*lx\n", GPR_FIELD(1));
		printk("  ERA: %0*lx\n", field, regs->csr_era);
	} else {
		printk("   ra: %0*lx %pS\n", GPR_FIELD(1), (void *) regs->regs[1]);
		printk("  ERA: %0*lx %pS\n", field, regs->csr_era, (void *) regs->csr_era);
	}
#undef GPR_FIELD

	/* Print saved important CSRs */
	print_crmd(regs->csr_crmd);
	print_prmd(regs->csr_prmd);
	print_euen(regs->csr_euen);
	print_ecfg(regs->csr_ecfg);
	print_estat(regs->csr_estat);

	if (exccode >= EXCCODE_TLBL && exccode <= EXCCODE_ALE)
		printk(" BADV: %0*lx\n", field, regs->csr_badvaddr);

	printk(" PRID: %08x (%s, %s)\n", read_cpucfg(LOONGARCH_CPUCFG0),
	       cpu_family_string(), cpu_full_name_string());
}

void show_regs(struct pt_regs *regs)
{
	__show_regs((struct pt_regs *)regs);
	dump_stack();
}

void show_registers(struct pt_regs *regs)
{
	__show_regs(regs);
	print_modules();
	printk("Process %s (pid: %d, threadinfo=%p, task=%p)\n",
	       current->comm, current->pid, current_thread_info(), current);

	show_stacktrace(current, regs, KERN_DEFAULT, user_mode(regs));
	show_code((void *)regs->csr_era, user_mode(regs));
	printk("\n");
}

static DEFINE_RAW_SPINLOCK(die_lock);

void die(const char *str, struct pt_regs *regs)
{
	int ret;
	static int die_counter;

	oops_enter();

	ret = notify_die(DIE_OOPS, str, regs, 0,
			 current->thread.trap_nr, SIGSEGV);

	console_verbose();
	raw_spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	printk("%s[#%d]:\n", str, ++die_counter);
	show_registers(regs);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	raw_spin_unlock_irq(&die_lock);

	oops_exit();

	if (ret == NOTIFY_STOP)
		return;

	if (regs && kexec_should_crash(current))
		crash_kexec(regs);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	make_task_dead(SIGSEGV);
}

static inline void setup_vint_size(unsigned int size)
{
	unsigned int vs;

	vs = ilog2(size/4);

	if (vs == 0 || vs > 7)
		panic("vint_size %d Not support yet", vs);

	csr_xchg32(vs<<CSR_ECFG_VS_SHIFT, CSR_ECFG_VS, LOONGARCH_CSR_ECFG);
}

/*
 * Send SIGFPE according to FCSR Cause bits, which must have already
 * been masked against Enable bits.  This is impotant as Inexact can
 * happen together with Overflow or Underflow, and `ptrace' can set
 * any bits.
 */
void force_fcsr_sig(unsigned long fcsr, void __user *fault_addr,
		     struct task_struct *tsk)
{
	int si_code = FPE_FLTUNK;

	if (fcsr & FPU_CSR_INV_X)
		si_code = FPE_FLTINV;
	else if (fcsr & FPU_CSR_DIV_X)
		si_code = FPE_FLTDIV;
	else if (fcsr & FPU_CSR_OVF_X)
		si_code = FPE_FLTOVF;
	else if (fcsr & FPU_CSR_UDF_X)
		si_code = FPE_FLTUND;
	else if (fcsr & FPU_CSR_INE_X)
		si_code = FPE_FLTRES;

	force_sig_fault(SIGFPE, si_code, fault_addr);
}

int process_fpemu_return(int sig, void __user *fault_addr, unsigned long fcsr)
{
	int si_code;

	switch (sig) {
	case 0:
		return 0;

	case SIGFPE:
		force_fcsr_sig(fcsr, fault_addr, current);
		return 1;

	case SIGBUS:
		force_sig_fault(SIGBUS, BUS_ADRERR, fault_addr);
		return 1;

	case SIGSEGV:
		mmap_read_lock(current->mm);
		if (vma_lookup(current->mm, (unsigned long)fault_addr))
			si_code = SEGV_ACCERR;
		else
			si_code = SEGV_MAPERR;
		mmap_read_unlock(current->mm);
		force_sig_fault(SIGSEGV, si_code, fault_addr);
		return 1;

	default:
		force_sig(sig);
		return 1;
	}
}

/*
 * Delayed fp exceptions when doing a lazy ctx switch
 */
asmlinkage void noinstr do_fpe(struct pt_regs *regs, unsigned long fcsr)
{
	int sig;
	void __user *fault_addr;
	irqentry_state_t state = irqentry_enter(regs);

	if (notify_die(DIE_FP, "FP exception", regs, 0, current->thread.trap_nr,
		       SIGFPE) == NOTIFY_STOP)
		goto out;

	/* Clear FCSR.Cause before enabling interrupts */
	write_fcsr(LOONGARCH_FCSR0, fcsr & ~mask_fcsr_x(fcsr));
	local_irq_enable();

	die_if_kernel("FP exception in kernel code", regs);

	sig = SIGFPE;
	fault_addr = (void __user *) regs->csr_era;

	/* Send a signal if required.  */
	process_fpemu_return(sig, fault_addr, fcsr);

out:
	local_irq_disable();
	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_ade(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	die_if_kernel("Kernel ade access", regs);
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)regs->csr_badvaddr);

	irqentry_exit(regs, state);
}

/* sysctl hooks */
int unaligned_enabled __read_mostly = 1;	/* Enabled by default */
int no_unaligned_warning __read_mostly = 1;	/* Only 1 warning by default */

asmlinkage void noinstr do_ale(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

#ifndef CONFIG_ARCH_STRICT_ALIGN
	die_if_kernel("Kernel ale access", regs);
	force_sig_fault(SIGBUS, BUS_ADRALN, (void __user *)regs->csr_badvaddr);
#else
	unsigned int *pc;

	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, regs->csr_badvaddr);

	/*
	 * Did we catch a fault trying to load an instruction?
	 */
	if (regs->csr_badvaddr == regs->csr_era)
		goto sigbus;
	if (user_mode(regs) && !test_thread_flag(TIF_FIXADE))
		goto sigbus;
	if (!unaligned_enabled)
		goto sigbus;
	if (!no_unaligned_warning)
		show_registers(regs);

	pc = (unsigned int *)exception_era(regs);

	emulate_load_store_insn(regs, (void __user *)regs->csr_badvaddr, pc);

	goto out;

sigbus:
	die_if_kernel("Kernel ale access", regs);
	force_sig_fault(SIGBUS, BUS_ADRALN, (void __user *)regs->csr_badvaddr);
out:
#endif
	irqentry_exit(regs, state);
}

#ifdef CONFIG_GENERIC_BUG
int is_valid_bugaddr(unsigned long addr)
{
	return 1;
}
#endif /* CONFIG_GENERIC_BUG */

static void bug_handler(struct pt_regs *regs)
{
	switch (report_bug(regs->csr_era, regs)) {
	case BUG_TRAP_TYPE_BUG:
	case BUG_TRAP_TYPE_NONE:
		die_if_kernel("Oops - BUG", regs);
		force_sig(SIGTRAP);
		break;

	case BUG_TRAP_TYPE_WARN:
		/* Skip the BUG instruction and continue */
		regs->csr_era += LOONGARCH_INSN_SIZE;
		break;
	}
}

asmlinkage void noinstr do_bce(struct pt_regs *regs)
{
	bool user = user_mode(regs);
	unsigned long era = exception_era(regs);
	u64 badv = 0, lower = 0, upper = ULONG_MAX;
	union loongarch_instruction insn;
	irqentry_state_t state = irqentry_enter(regs);

	if (regs->csr_prmd & CSR_PRMD_PIE)
		local_irq_enable();

	current->thread.trap_nr = read_csr_excode();

	die_if_kernel("Bounds check error in kernel code", regs);

	/*
	 * Pull out the address that failed bounds checking, and the lower /
	 * upper bound, by minimally looking at the faulting instruction word
	 * and reading from the correct register.
	 */
	if (__get_inst(&insn.word, (u32 *)era, user))
		goto bad_era;

	switch (insn.reg3_format.opcode) {
	case asrtle_op:
		if (insn.reg3_format.rd != 0)
			break;	/* not asrtle */
		badv = regs->regs[insn.reg3_format.rj];
		upper = regs->regs[insn.reg3_format.rk];
		break;

	case asrtgt_op:
		if (insn.reg3_format.rd != 0)
			break;	/* not asrtgt */
		badv = regs->regs[insn.reg3_format.rj];
		lower = regs->regs[insn.reg3_format.rk];
		break;

	case ldleb_op:
	case ldleh_op:
	case ldlew_op:
	case ldled_op:
	case stleb_op:
	case stleh_op:
	case stlew_op:
	case stled_op:
	case fldles_op:
	case fldled_op:
	case fstles_op:
	case fstled_op:
		badv = regs->regs[insn.reg3_format.rj];
		upper = regs->regs[insn.reg3_format.rk];
		break;

	case ldgtb_op:
	case ldgth_op:
	case ldgtw_op:
	case ldgtd_op:
	case stgtb_op:
	case stgth_op:
	case stgtw_op:
	case stgtd_op:
	case fldgts_op:
	case fldgtd_op:
	case fstgts_op:
	case fstgtd_op:
		badv = regs->regs[insn.reg3_format.rj];
		lower = regs->regs[insn.reg3_format.rk];
		break;
	}

	force_sig_bnderr((void __user *)badv, (void __user *)lower, (void __user *)upper);

out:
	if (regs->csr_prmd & CSR_PRMD_PIE)
		local_irq_disable();

	irqentry_exit(regs, state);
	return;

bad_era:
	/*
	 * Cannot pull out the instruction word, hence cannot provide more
	 * info than a regular SIGSEGV in this case.
	 */
	force_sig(SIGSEGV);
	goto out;
}

asmlinkage void noinstr do_bp(struct pt_regs *regs)
{
	bool user = user_mode(regs);
	unsigned int opcode, bcode;
	unsigned long era = exception_era(regs);
	irqentry_state_t state = irqentry_enter(regs);

	if (regs->csr_prmd & CSR_PRMD_PIE)
		local_irq_enable();

	if (__get_inst(&opcode, (u32 *)era, user))
		goto out_sigsegv;

	bcode = (opcode & 0x7fff);

	/*
	 * notify the kprobe handlers, if instruction is likely to
	 * pertain to them.
	 */
	switch (bcode) {
	case BRK_KPROBE_BP:
		if (kprobe_breakpoint_handler(regs))
			goto out;
		else
			break;
	case BRK_KPROBE_SSTEPBP:
		if (kprobe_singlestep_handler(regs))
			goto out;
		else
			break;
	case BRK_UPROBE_BP:
		if (uprobe_breakpoint_handler(regs))
			goto out;
		else
			break;
	case BRK_UPROBE_XOLBP:
		if (uprobe_singlestep_handler(regs))
			goto out;
		else
			break;
	default:
		current->thread.trap_nr = read_csr_excode();
		if (notify_die(DIE_TRAP, "Break", regs, bcode,
			       current->thread.trap_nr, SIGTRAP) == NOTIFY_STOP)
			goto out;
		else
			break;
	}

	switch (bcode) {
	case BRK_BUG:
		bug_handler(regs);
		break;
	case BRK_DIVZERO:
		die_if_kernel("Break instruction in kernel code", regs);
		force_sig_fault(SIGFPE, FPE_INTDIV, (void __user *)regs->csr_era);
		break;
	case BRK_OVERFLOW:
		die_if_kernel("Break instruction in kernel code", regs);
		force_sig_fault(SIGFPE, FPE_INTOVF, (void __user *)regs->csr_era);
		break;
	default:
		die_if_kernel("Break instruction in kernel code", regs);
		force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *)regs->csr_era);
		break;
	}

out:
	if (regs->csr_prmd & CSR_PRMD_PIE)
		local_irq_disable();

	irqentry_exit(regs, state);
	return;

out_sigsegv:
	force_sig(SIGSEGV);
	goto out;
}

asmlinkage void noinstr do_watch(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

#ifndef CONFIG_HAVE_HW_BREAKPOINT
	pr_warn("Hardware watch point handler not implemented!\n");
#else
	if (test_tsk_thread_flag(current, TIF_SINGLESTEP)) {
		int llbit = (csr_read32(LOONGARCH_CSR_LLBCTL) & 0x1);
		unsigned long pc = instruction_pointer(regs);
		union loongarch_instruction *ip = (union loongarch_instruction *)pc;

		if (llbit) {
			/*
			 * When the ll-sc combo is encountered, it is regarded as an single
			 * instruction. So don't clear llbit and reset CSR.FWPS.Skip until
			 * the llsc execution is completed.
			 */
			csr_write32(CSR_FWPC_SKIP, LOONGARCH_CSR_FWPS);
			csr_write32(CSR_LLBCTL_KLO, LOONGARCH_CSR_LLBCTL);
			goto out;
		}

		if (pc == current->thread.single_step) {
			/*
			 * Certain insns are occasionally not skipped when CSR.FWPS.Skip is
			 * set, such as fld.d/fst.d. So singlestep needs to compare whether
			 * the csr_era is equal to the value of singlestep which last time set.
			 */
			if (!is_self_loop_ins(ip, regs)) {
				/*
				 * Check if the given instruction the target pc is equal to the
				 * current pc, If yes, then we should not set the CSR.FWPS.SKIP
				 * bit to break the original instruction stream.
				 */
				csr_write32(CSR_FWPC_SKIP, LOONGARCH_CSR_FWPS);
				goto out;
			}
		}
	} else {
		breakpoint_handler(regs);
		watchpoint_handler(regs);
	}

	force_sig(SIGTRAP);
out:
#endif
	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_ri(struct pt_regs *regs)
{
	int status = SIGILL;
	unsigned int opcode = 0;
	unsigned int __user *era = (unsigned int __user *)exception_era(regs);
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	current->thread.trap_nr = read_csr_excode();

	if (notify_die(DIE_RI, "RI Fault", regs, 0, current->thread.trap_nr,
		       SIGILL) == NOTIFY_STOP)
		goto out;

	die_if_kernel("Reserved instruction in kernel code", regs);

	if (unlikely(get_user(opcode, era) < 0)) {
		status = SIGSEGV;
		current->thread.error_code = 1;
	}

	force_sig(status);

out:
	local_irq_disable();
	irqentry_exit(regs, state);
}

static void init_restore_fp(void)
{
	if (!used_math()) {
		/* First time FP context user. */
		init_fpu();
	} else {
		/* This task has formerly used the FP context */
		if (!is_fpu_owner())
			own_fpu_inatomic(1);
	}

	BUG_ON(!is_fp_enabled());
}

static void init_restore_lsx(void)
{
	enable_lsx();

	if (!thread_lsx_context_live()) {
		/* First time LSX context user */
		init_restore_fp();
		init_lsx_upper();
		set_thread_flag(TIF_LSX_CTX_LIVE);
	} else {
		if (!is_simd_owner()) {
			if (is_fpu_owner()) {
				restore_lsx_upper(current);
			} else {
				__own_fpu();
				restore_lsx(current);
			}
		}
	}

	set_thread_flag(TIF_USEDSIMD);

	BUG_ON(!is_fp_enabled());
	BUG_ON(!is_lsx_enabled());
}

static void init_restore_lasx(void)
{
	enable_lasx();

	if (!thread_lasx_context_live()) {
		/* First time LASX context user */
		init_restore_lsx();
		init_lasx_upper();
		set_thread_flag(TIF_LASX_CTX_LIVE);
	} else {
		if (is_fpu_owner() || is_simd_owner()) {
			init_restore_lsx();
			restore_lasx_upper(current);
		} else {
			__own_fpu();
			enable_lsx();
			restore_lasx(current);
		}
	}

	set_thread_flag(TIF_USEDSIMD);

	BUG_ON(!is_fp_enabled());
	BUG_ON(!is_lsx_enabled());
	BUG_ON(!is_lasx_enabled());
}

asmlinkage void noinstr do_fpu(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	die_if_kernel("do_fpu invoked from kernel context!", regs);
	BUG_ON(is_lsx_enabled());
	BUG_ON(is_lasx_enabled());

	preempt_disable();
	init_restore_fp();
	preempt_enable();

	local_irq_disable();
	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_lsx(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	if (!cpu_has_lsx) {
		force_sig(SIGILL);
		goto out;
	}

	die_if_kernel("do_lsx invoked from kernel context!", regs);
	BUG_ON(is_lasx_enabled());

	preempt_disable();
	init_restore_lsx();
	preempt_enable();

out:
	local_irq_disable();
	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_lasx(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	if (!cpu_has_lasx) {
		force_sig(SIGILL);
		goto out;
	}

	die_if_kernel("do_lasx invoked from kernel context!", regs);

	preempt_disable();
	init_restore_lasx();
	preempt_enable();

out:
	local_irq_disable();
	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_lbt(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	force_sig(SIGILL);
	local_irq_disable();

	irqentry_exit(regs, state);
}

asmlinkage void noinstr do_reserved(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);

	local_irq_enable();
	/*
	 * Game over - no way to handle this if it ever occurs.	Most probably
	 * caused by a fatal error after another hardware/software error.
	 */
	pr_err("Caught reserved exception %u on pid:%d [%s] - should not happen\n",
		read_csr_excode(), current->pid, current->comm);
	die_if_kernel("do_reserved exception", regs);
	force_sig(SIGUNUSED);

	local_irq_disable();

	irqentry_exit(regs, state);
}

asmlinkage void cache_parity_error(void)
{
	/* For the moment, report the problem and hang. */
	pr_err("Cache error exception:\n");
	pr_err("csr_merrctl == %08x\n", csr_read32(LOONGARCH_CSR_MERRCTL));
	pr_err("csr_merrera == %016lx\n", csr_read64(LOONGARCH_CSR_MERRERA));
	panic("Can't handle the cache error!");
}

asmlinkage void noinstr handle_loongarch_irq(struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	irq_enter_rcu();
	old_regs = set_irq_regs(regs);
	handle_arch_irq(regs);
	set_irq_regs(old_regs);
	irq_exit_rcu();
}

asmlinkage void noinstr do_vint(struct pt_regs *regs, unsigned long sp)
{
	register int cpu;
	register unsigned long stack;
	irqentry_state_t state = irqentry_enter(regs);

	cpu = smp_processor_id();

	if (on_irq_stack(cpu, sp))
		handle_loongarch_irq(regs);
	else {
		stack = per_cpu(irq_stack, cpu) + IRQ_STACK_START;

		/* Save task's sp on IRQ stack for unwinding */
		*(unsigned long *)stack = sp;

		__asm__ __volatile__(
		"move	$s0, $sp		\n" /* Preserve sp */
		"move	$sp, %[stk]		\n" /* Switch stack */
		"move	$a0, %[regs]		\n"
		"bl	handle_loongarch_irq	\n"
		"move	$sp, $s0		\n" /* Restore sp */
		: /* No outputs */
		: [stk] "r" (stack), [regs] "r" (regs)
		: "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7", "$s0",
		  "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",
		  "memory");
	}

	irqentry_exit(regs, state);
}

unsigned long eentry;
unsigned long tlbrentry;

long exception_handlers[VECSIZE * 128 / sizeof(long)] __aligned(SZ_64K);

static void configure_exception_vector(void)
{
	eentry    = (unsigned long)exception_handlers;
	tlbrentry = (unsigned long)exception_handlers + 80*VECSIZE;

	csr_write64(eentry, LOONGARCH_CSR_EENTRY);
	csr_write64(eentry, LOONGARCH_CSR_MERRENTRY);
	csr_write64(tlbrentry, LOONGARCH_CSR_TLBRENTRY);
}

void per_cpu_trap_init(int cpu)
{
	unsigned int i;

	setup_vint_size(VECSIZE);

	configure_exception_vector();

	if (!cpu_data[cpu].asid_cache)
		cpu_data[cpu].asid_cache = asid_first_version(cpu);

	mmgrab(&init_mm);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);

	/* Initialise exception handlers */
	if (cpu == 0)
		for (i = 0; i < 64; i++)
			set_handler(i * VECSIZE, handle_reserved, VECSIZE);

	tlb_init(cpu);
	cpu_cache_init();
}

/* Install CPU exception handler */
void set_handler(unsigned long offset, void *addr, unsigned long size)
{
	memcpy((void *)(eentry + offset), addr, size);
	local_flush_icache_range(eentry + offset, eentry + offset + size);
}

static const char panic_null_cerr[] =
	"Trying to set NULL cache error exception handler\n";

/*
 * Install uncached CPU exception handler.
 * This is suitable only for the cache error exception which is the only
 * exception handler that is being run uncached.
 */
void set_merr_handler(unsigned long offset, void *addr, unsigned long size)
{
	unsigned long uncached_eentry = TO_UNCACHE(__pa(eentry));

	if (!addr)
		panic(panic_null_cerr);

	memcpy((void *)(uncached_eentry + offset), addr, size);
}

void __init trap_init(void)
{
	long i;

	/* Set interrupt vector handler */
	for (i = EXCCODE_INT_START; i <= EXCCODE_INT_END; i++)
		set_handler(i * VECSIZE, handle_vint, VECSIZE);

	set_handler(EXCCODE_ADE * VECSIZE, handle_ade, VECSIZE);
	set_handler(EXCCODE_ALE * VECSIZE, handle_ale, VECSIZE);
	set_handler(EXCCODE_BCE * VECSIZE, handle_bce, VECSIZE);
	set_handler(EXCCODE_SYS * VECSIZE, handle_sys, VECSIZE);
	set_handler(EXCCODE_BP * VECSIZE, handle_bp, VECSIZE);
	set_handler(EXCCODE_INE * VECSIZE, handle_ri, VECSIZE);
	set_handler(EXCCODE_IPE * VECSIZE, handle_ri, VECSIZE);
	set_handler(EXCCODE_FPDIS * VECSIZE, handle_fpu, VECSIZE);
	set_handler(EXCCODE_LSXDIS * VECSIZE, handle_lsx, VECSIZE);
	set_handler(EXCCODE_LASXDIS * VECSIZE, handle_lasx, VECSIZE);
	set_handler(EXCCODE_FPE * VECSIZE, handle_fpe, VECSIZE);
	set_handler(EXCCODE_BTDIS * VECSIZE, handle_lbt, VECSIZE);
	set_handler(EXCCODE_WATCH * VECSIZE, handle_watch, VECSIZE);

	cache_error_setup();

	local_flush_icache_range(eentry, eentry + 0x400);
}
