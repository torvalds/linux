/*
 *  linux/arch/m32r/kernel/traps.c
 *
 *  Copyright (C) 2001, 2002  Hirokazu Takata, Hiroyuki Kondo,
 *                            Hitoshi Yamamoto
 */

/*
 * 'traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/processor.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <linux/atomic.h>

#include <asm/smp.h>

#include <linux/module.h>

asmlinkage void alignment_check(void);
asmlinkage void ei_handler(void);
asmlinkage void rie_handler(void);
asmlinkage void debug_trap(void);
asmlinkage void cache_flushing_handler(void);
asmlinkage void ill_trap(void);

#ifdef CONFIG_SMP
extern void smp_reschedule_interrupt(void);
extern void smp_invalidate_interrupt(void);
extern void smp_call_function_interrupt(void);
extern void smp_ipi_timer_interrupt(void);
extern void smp_flush_cache_all_interrupt(void);
extern void smp_call_function_single_interrupt(void);

/*
 * for Boot AP function
 */
asm (
	"	.section .eit_vector4,\"ax\"	\n"
	"	.global _AP_RE			\n"
	"	.global startup_AP		\n"
	"_AP_RE:				\n"
	"	.fill 32, 4, 0			\n"
	"_AP_EI: bra	startup_AP		\n"
	"	.previous			\n"
);
#endif  /* CONFIG_SMP */

extern unsigned long	eit_vector[];
#define BRA_INSN(func, entry)	\
	((unsigned long)func - (unsigned long)eit_vector - entry*4)/4 \
	+ 0xff000000UL

static void set_eit_vector_entries(void)
{
	extern void default_eit_handler(void);
	extern void system_call(void);
	extern void pie_handler(void);
	extern void ace_handler(void);
	extern void tme_handler(void);
	extern void _flush_cache_copyback_all(void);

	eit_vector[0] = 0xd0c00001; /* seth r0, 0x01 */
	eit_vector[1] = BRA_INSN(default_eit_handler, 1);
	eit_vector[4] = 0xd0c00010; /* seth r0, 0x10 */
	eit_vector[5] = BRA_INSN(default_eit_handler, 5);
	eit_vector[8] = BRA_INSN(rie_handler, 8);
	eit_vector[12] = BRA_INSN(alignment_check, 12);
	eit_vector[16] = BRA_INSN(ill_trap, 16);
	eit_vector[17] = BRA_INSN(debug_trap, 17);
	eit_vector[18] = BRA_INSN(system_call, 18);
	eit_vector[19] = BRA_INSN(ill_trap, 19);
	eit_vector[20] = BRA_INSN(ill_trap, 20);
	eit_vector[21] = BRA_INSN(ill_trap, 21);
	eit_vector[22] = BRA_INSN(ill_trap, 22);
	eit_vector[23] = BRA_INSN(ill_trap, 23);
	eit_vector[24] = BRA_INSN(ill_trap, 24);
	eit_vector[25] = BRA_INSN(ill_trap, 25);
	eit_vector[26] = BRA_INSN(ill_trap, 26);
	eit_vector[27] = BRA_INSN(ill_trap, 27);
	eit_vector[28] = BRA_INSN(cache_flushing_handler, 28);
	eit_vector[29] = BRA_INSN(ill_trap, 29);
	eit_vector[30] = BRA_INSN(ill_trap, 30);
	eit_vector[31] = BRA_INSN(ill_trap, 31);
	eit_vector[32] = BRA_INSN(ei_handler, 32);
	eit_vector[64] = BRA_INSN(pie_handler, 64);
#ifdef CONFIG_MMU
	eit_vector[68] = BRA_INSN(ace_handler, 68);
	eit_vector[72] = BRA_INSN(tme_handler, 72);
#endif /* CONFIG_MMU */
#ifdef CONFIG_SMP
	eit_vector[184] = (unsigned long)smp_reschedule_interrupt;
	eit_vector[185] = (unsigned long)smp_invalidate_interrupt;
	eit_vector[186] = (unsigned long)smp_call_function_interrupt;
	eit_vector[187] = (unsigned long)smp_ipi_timer_interrupt;
	eit_vector[188] = (unsigned long)smp_flush_cache_all_interrupt;
	eit_vector[189] = 0;	/* CPU_BOOT_IPI */
	eit_vector[190] = (unsigned long)smp_call_function_single_interrupt;
	eit_vector[191] = 0;
#endif
	_flush_cache_copyback_all();
}

void __init trap_init(void)
{
	set_eit_vector_entries();

	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();
}

static int kstack_depth_to_print = 24;

static void show_trace(struct task_struct *task, unsigned long *stack)
{
	unsigned long addr;

	if (!stack)
		stack = (unsigned long*)&stack;

	printk("Call Trace: ");
	while (!kstack_end(stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr))
			printk("[<%08lx>] %pSR\n", addr, (void *)addr);
	}
	printk("\n");
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long  *stack;
	int  i;

	/*
	 * debugging aid: "show_stack(NULL);" prints the
	 * back trace for this cpu.
	 */

	if(sp==NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp=(unsigned long*)&sp;
	}

	stack = sp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % 4) == 0))
			printk("\n       ");
		printk("%08lx ", *stack++);
	}
	printk("\n");
	show_trace(task, sp);
}

static void show_registers(struct pt_regs *regs)
{
	int i = 0;
	int in_kernel = 1;
	unsigned long sp;

	printk("CPU:    %d\n", smp_processor_id());
	show_regs(regs);

	sp = (unsigned long) (1+regs);
	if (user_mode(regs)) {
		in_kernel = 0;
		sp = regs->spu;
		printk("SPU: %08lx\n", sp);
	} else {
		printk("SPI: %08lx\n", sp);
	}
	printk("Process %s (pid: %d, process nr: %d, stackpage=%08lx)",
		current->comm, task_pid_nr(current), 0xffff & i, 4096+(unsigned long)current);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {
		printk("\nStack: ");
		show_stack(current, (unsigned long*) sp);

		printk("\nCode: ");
		if (regs->bpc < PAGE_OFFSET)
			goto bad;

		for(i=0;i<20;i++) {
			unsigned char c;
			if (__get_user(c, &((unsigned char*)regs->bpc)[i])) {
bad:
				printk(" Bad PC value.");
				break;
			}
			printk("%02x ", c);
		}
	}
	printk("\n");
}

static DEFINE_SPINLOCK(die_lock);

void die(const char * str, struct pt_regs * regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04lx\n", str, err & 0xffff);
	show_registers(regs);
	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

static __inline__ void die_if_kernel(const char * str,
	struct pt_regs * regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

static __inline__ void do_trap(int trapnr, int signr, const char * str,
	struct pt_regs * regs, long error_code, siginfo_t *info)
{
	if (user_mode(regs)) {
		/* trap_signal */
		struct task_struct *tsk = current;
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;
		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	} else {
		/* kernel_trap */
		if (!fixup_exception(regs))
			die(str, regs, error_code);
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	do_trap(trapnr, signr, NULL, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
asmlinkage void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void __user *)siaddr; \
	do_trap(trapnr, signr, str, regs, error_code, &info); \
}

DO_ERROR( 1, SIGTRAP, "debug trap", debug_trap)
DO_ERROR_INFO(0x20, SIGILL,  "reserved instruction ", rie_handler, ILL_ILLOPC, regs->bpc)
DO_ERROR_INFO(0x100, SIGILL,  "privileged instruction", pie_handler, ILL_PRVOPC, regs->bpc)
DO_ERROR_INFO(-1, SIGILL,  "illegal trap", ill_trap, ILL_ILLTRP, regs->bpc)

extern int handle_unaligned_access(unsigned long, struct pt_regs *);

/* This code taken from arch/sh/kernel/traps.c */
asmlinkage void do_alignment_check(struct pt_regs *regs, long error_code)
{
	mm_segment_t oldfs;
	unsigned long insn;
	int tmp;

	oldfs = get_fs();

	if (user_mode(regs)) {
		local_irq_enable();
		current->thread.error_code = error_code;
		current->thread.trap_no = 0x17;

		set_fs(USER_DS);
		if (copy_from_user(&insn, (void *)regs->bpc, 4)) {
			set_fs(oldfs);
			goto uspace_segv;
		}
		tmp = handle_unaligned_access(insn, regs);
		set_fs(oldfs);

		if (!tmp)
			return;

	uspace_segv:
		printk(KERN_NOTICE "Killing process \"%s\" due to unaligned "
			"access\n", current->comm);
		force_sig(SIGSEGV, current);
	} else {
		set_fs(KERNEL_DS);
		if (copy_from_user(&insn, (void *)regs->bpc, 4)) {
			set_fs(oldfs);
			die("insn faulting in do_address_error", regs, 0);
		}
		handle_unaligned_access(insn, regs);
		set_fs(oldfs);
	}
}
