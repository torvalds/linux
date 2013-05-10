#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/kprobes.h>
#include <asm/debugreg.h>
#include <asm/siginfo.h>			/* TRAP_TRACE, ... */

#ifdef CONFIG_X86_32
#define dotraplinkage
#else
#define dotraplinkage asmlinkage
#endif

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void xen_debug(void);
asmlinkage void xen_int3(void);
asmlinkage void xen_stack_segment(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
#ifdef CONFIG_X86_64
asmlinkage void double_fault(void);
#endif
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void async_page_fault(void);
asmlinkage void spurious_interrupt_bug(void);
asmlinkage void coprocessor_error(void);
asmlinkage void alignment_check(void);
#ifdef CONFIG_X86_MCE
asmlinkage void machine_check(void);
#endif /* CONFIG_X86_MCE */
asmlinkage void simd_coprocessor_error(void);

dotraplinkage void do_divide_error(struct pt_regs *, long);
dotraplinkage void do_debug(struct pt_regs *, long);
dotraplinkage void do_nmi(struct pt_regs *, long);
dotraplinkage void do_int3(struct pt_regs *, long);
dotraplinkage void do_overflow(struct pt_regs *, long);
dotraplinkage void do_bounds(struct pt_regs *, long);
dotraplinkage void do_invalid_op(struct pt_regs *, long);
dotraplinkage void do_device_not_available(struct pt_regs *, long);
dotraplinkage void do_coprocessor_segment_overrun(struct pt_regs *, long);
dotraplinkage void do_invalid_TSS(struct pt_regs *, long);
dotraplinkage void do_segment_not_present(struct pt_regs *, long);
dotraplinkage void do_stack_segment(struct pt_regs *, long);
#ifdef CONFIG_X86_64
dotraplinkage void do_double_fault(struct pt_regs *, long);
asmlinkage __kprobes struct pt_regs *sync_regs(struct pt_regs *);
#endif
dotraplinkage void do_general_protection(struct pt_regs *, long);
dotraplinkage void do_page_fault(struct pt_regs *, unsigned long);
dotraplinkage void do_spurious_interrupt_bug(struct pt_regs *, long);
dotraplinkage void do_coprocessor_error(struct pt_regs *, long);
dotraplinkage void do_alignment_check(struct pt_regs *, long);
#ifdef CONFIG_X86_MCE
dotraplinkage void do_machine_check(struct pt_regs *, long);
#endif
dotraplinkage void do_simd_coprocessor_error(struct pt_regs *, long);
#ifdef CONFIG_X86_32
dotraplinkage void do_iret_error(struct pt_regs *, long);
#endif

static inline int get_si_code(unsigned long condition)
{
	if (condition & DR_STEP)
		return TRAP_TRACE;
	else if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3))
		return TRAP_HWBKPT;
	else
		return TRAP_BRKPT;
}

extern int panic_on_unrecovered_nmi;

void math_error(struct pt_regs *, int, int);
void math_emulate(struct math_emu_info *);
#ifndef CONFIG_X86_32
asmlinkage void smp_thermal_interrupt(void);
asmlinkage void mce_threshold_interrupt(void);
#endif

/* Interrupts/Exceptions */
enum {
	X86_TRAP_DE = 0,	/*  0, Divide-by-zero */
	X86_TRAP_DB,		/*  1, Debug */
	X86_TRAP_NMI,		/*  2, Non-maskable Interrupt */
	X86_TRAP_BP,		/*  3, Breakpoint */
	X86_TRAP_OF,		/*  4, Overflow */
	X86_TRAP_BR,		/*  5, Bound Range Exceeded */
	X86_TRAP_UD,		/*  6, Invalid Opcode */
	X86_TRAP_NM,		/*  7, Device Not Available */
	X86_TRAP_DF,		/*  8, Double Fault */
	X86_TRAP_OLD_MF,	/*  9, Coprocessor Segment Overrun */
	X86_TRAP_TS,		/* 10, Invalid TSS */
	X86_TRAP_NP,		/* 11, Segment Not Present */
	X86_TRAP_SS,		/* 12, Stack Segment Fault */
	X86_TRAP_GP,		/* 13, General Protection Fault */
	X86_TRAP_PF,		/* 14, Page Fault */
	X86_TRAP_SPURIOUS,	/* 15, Spurious Interrupt */
	X86_TRAP_MF,		/* 16, x87 Floating-Point Exception */
	X86_TRAP_AC,		/* 17, Alignment Check */
	X86_TRAP_MC,		/* 18, Machine Check */
	X86_TRAP_XF,		/* 19, SIMD Floating-Point Exception */
	X86_TRAP_IRET = 32,	/* 32, IRET Exception */
};

#endif /* _ASM_X86_TRAPS_H */
