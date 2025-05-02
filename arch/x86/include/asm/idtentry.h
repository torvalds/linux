/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IDTENTRY_H
#define _ASM_X86_IDTENTRY_H

/* Interrupts/Exceptions */
#include <asm/trapnr.h>

#define IDT_ALIGN	(8 * (1 + HAS_KERNEL_IBT))

#ifndef __ASSEMBLER__
#include <linux/entry-common.h>
#include <linux/hardirq.h>

#include <asm/irq_stack.h>

typedef void (*idtentry_t)(struct pt_regs *regs);

/**
 * DECLARE_IDTENTRY - Declare functions for simple IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares four functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the FRED event dispatcher (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Note: This is the C variant of DECLARE_IDTENTRY(). As the name says it
 * declares the entry points for usage in C code. There is an ASM variant
 * as well which is used to emit the entry stubs in entry_32/64.S.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	void fred_##func(struct pt_regs *regs);				\
	__visible void func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY - Emit code for simple IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * irqentry_enter() contains common code which has to be invoked before
 * arbitrary code in the body. irqentry_exit() contains common code
 * which has to run before returning to the low level assembly code.
 */
#define DEFINE_IDTENTRY(func)						\
static __always_inline void __##func(struct pt_regs *regs);		\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs);						\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs)

/* Special case for 32bit IRET 'trap' */
#define DECLARE_IDTENTRY_SW	DECLARE_IDTENTRY
#define DEFINE_IDTENTRY_SW	DEFINE_IDTENTRY

/**
 * DECLARE_IDTENTRY_ERRORCODE - Declare functions for simple IDT entry points
 *				Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Same as DECLARE_IDTENTRY, but has an extra error_code argument for the
 * C-handler.
 */
#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	__visible void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DEFINE_IDTENTRY_ERRORCODE - Emit code for simple IDT entry points
 *			       Error code pushed by hardware
 * @func:	Function name of the entry point
 *
 * Same as DEFINE_IDTENTRY, but has an extra error_code argument
 */
#define DEFINE_IDTENTRY_ERRORCODE(func)					\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code);		\
									\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs, error_code);					\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code)

/**
 * DECLARE_IDTENTRY_RAW - Declare functions for raw IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY().
 */
#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/**
 * DEFINE_IDTENTRY_RAW - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY() this does not invoke the
 * idtentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW(func)					\
__visible noinstr void func(struct pt_regs *regs)

/**
 * DEFINE_FREDENTRY_RAW - Emit code for raw FRED entry points
 * @func:	Function name of the entry point
 *
 * @func is called from the FRED event dispatcher with interrupts disabled.
 *
 * See @DEFINE_IDTENTRY_RAW for further details.
 */
#define DEFINE_FREDENTRY_RAW(func)					\
noinstr void fred_##func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_RAW_ERRORCODE - Declare functions for raw IDT entry points
 *				    Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_ERRORCODE()
 */
#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_RAW_ERRORCODE - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY_ERRORCODE() this does not invoke the
 * irqentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW_ERRORCODE(func)				\
__visible noinstr void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DECLARE_IDTENTRY_IRQ - Declare functions for device interrupt IDT entry
 *			  points (common/spurious)
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_ERRORCODE()
 */
#define DECLARE_IDTENTRY_IRQ(vector, func)				\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_IRQ - Emit code for device interrupt IDT entry points
 * @func:	Function name of the entry point
 *
 * The vector number is pushed by the low level entry stub and handed
 * to the function as error_code argument which needs to be truncated
 * to an u8 because the push is sign extending.
 *
 * irq_enter/exit_rcu() are invoked before the function body and the
 * KVM L1D flush request is set. Stack switching to the interrupt stack
 * has to be done in the function body if necessary.
 */
#define DEFINE_IDTENTRY_IRQ(func)					\
static void __##func(struct pt_regs *regs, u32 vector);			\
									\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
	u32 vector = (u32)(u8)error_code;				\
									\
	kvm_set_cpu_l1tf_flush_l1d();                                   \
	instrumentation_begin();					\
	run_irq_on_irqstack_cond(__##func, regs, vector);		\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
static noinline void __##func(struct pt_regs *regs, u32 vector)

/**
 * DECLARE_IDTENTRY_SYSVEC - Declare functions for system vector entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Maps to DECLARE_IDTENTRY().
 */
#define DECLARE_IDTENTRY_SYSVEC(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/**
 * DEFINE_IDTENTRY_SYSVEC - Emit code for system vector IDT entry points
 * @func:	Function name of the entry point
 *
 * irqentry_enter/exit() and irq_enter/exit_rcu() are invoked before the
 * function body. KVM L1D flush request is set.
 *
 * Runs the function on the interrupt stack if the entry hit kernel mode
 */
#define DEFINE_IDTENTRY_SYSVEC(func)					\
static void __##func(struct pt_regs *regs);				\
									\
static __always_inline void instr_##func(struct pt_regs *regs)		\
{									\
	run_sysvec_on_irqstack_cond(__##func, regs);			\
}									\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	kvm_set_cpu_l1tf_flush_l1d();                                   \
	instrumentation_begin();					\
	instr_##func (regs);						\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
void fred_##func(struct pt_regs *regs)					\
{									\
	instr_##func (regs);						\
}									\
									\
static noinline void __##func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY_SYSVEC_SIMPLE - Emit code for simple system vector IDT
 *				   entry points
 * @func:	Function name of the entry point
 *
 * Runs the function on the interrupted stack. No switch to IRQ stack and
 * only the minimal __irq_enter/exit() handling.
 *
 * Only use for 'empty' vectors like reschedule IPI and KVM posted
 * interrupt vectors.
 */
#define DEFINE_IDTENTRY_SYSVEC_SIMPLE(func)				\
static __always_inline void __##func(struct pt_regs *regs);		\
									\
static __always_inline void instr_##func(struct pt_regs *regs)		\
{									\
	__irq_enter_raw();						\
	__##func (regs);						\
	__irq_exit_raw();						\
}									\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state = irqentry_enter(regs);			\
									\
	kvm_set_cpu_l1tf_flush_l1d();                                   \
	instrumentation_begin();					\
	instr_##func (regs);						\
	instrumentation_end();						\
	irqentry_exit(regs, state);					\
}									\
									\
void fred_##func(struct pt_regs *regs)					\
{									\
	instr_##func (regs);						\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_XENCB - Declare functions for XEN HV callback entry point
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Maps to DECLARE_IDTENTRY(). Distinct entry point to handle the 32/64-bit
 * difference
 */
#define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#ifdef CONFIG_X86_64
/**
 * DECLARE_IDTENTRY_IST - Declare functions for IST handling IDT entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW, but declares also the NOIST C handler
 * which is called from the ASM entry point on user mode entry
 */
#define DECLARE_IDTENTRY_IST(vector, func)				\
	DECLARE_IDTENTRY_RAW(vector, func);				\
	__visible void noist_##func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_VC - Declare functions for the VC entry point
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW_ERRORCODE, but declares also the
 * safe_stack C handler.
 */
#define DECLARE_IDTENTRY_VC(vector, func)				\
	DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func);			\
	__visible noinstr void kernel_##func(struct pt_regs *regs, unsigned long error_code);	\
	__visible noinstr void   user_##func(struct pt_regs *regs, unsigned long error_code)

/**
 * DEFINE_IDTENTRY_IST - Emit code for IST entry points
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW
 */
#define DEFINE_IDTENTRY_IST(func)					\
	DEFINE_IDTENTRY_RAW(func)

/**
 * DEFINE_IDTENTRY_NOIST - Emit code for NOIST entry points which
 *			   belong to a IST entry point (MCE, DB)
 * @func:	Function name of the entry point. Must be the same as
 *		the function name of the corresponding IST variant
 *
 * Maps to DEFINE_IDTENTRY_RAW().
 */
#define DEFINE_IDTENTRY_NOIST(func)					\
	DEFINE_IDTENTRY_RAW(noist_##func)

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW_ERRORCODE
 */
#define DECLARE_IDTENTRY_DF(vector, func)				\
	DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_DF(func)					\
	DEFINE_IDTENTRY_RAW_ERRORCODE(func)

/**
 * DEFINE_IDTENTRY_VC_KERNEL - Emit code for VMM communication handler
			       when raised from kernel mode
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_VC_KERNEL(func)				\
	DEFINE_IDTENTRY_RAW_ERRORCODE(kernel_##func)

/**
 * DEFINE_IDTENTRY_VC_USER - Emit code for VMM communication handler
			     when raised from user mode
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_VC_USER(func)				\
	DEFINE_IDTENTRY_RAW_ERRORCODE(user_##func)

#else	/* CONFIG_X86_64 */

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault 32bit variant
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares two functions:
 * - The ASM entry point: asm_##func
 * - The C handler called from the C shim
 */
#define DECLARE_IDTENTRY_DF(vector, func)				\
	asmlinkage void asm_##func(void);				\
	__visible void func(struct pt_regs *regs,			\
			    unsigned long error_code,			\
			    unsigned long address)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault on 32bit
 * @func:	Function name of the entry point
 *
 * This is called through the doublefault shim which already provides
 * cr2 in the address argument.
 */
#define DEFINE_IDTENTRY_DF(func)					\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code,			\
			    unsigned long address)

#endif	/* !CONFIG_X86_64 */

/* C-Code mapping */
#define DECLARE_IDTENTRY_NMI		DECLARE_IDTENTRY_RAW
#define DEFINE_IDTENTRY_NMI		DEFINE_IDTENTRY_RAW
#define DEFINE_FREDENTRY_NMI		DEFINE_FREDENTRY_RAW

#ifdef CONFIG_X86_64
#define DECLARE_IDTENTRY_MCE		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE_USER	DEFINE_IDTENTRY_NOIST
#define DEFINE_FREDENTRY_MCE		DEFINE_FREDENTRY_RAW

#define DECLARE_IDTENTRY_DEBUG		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG_USER	DEFINE_IDTENTRY_NOIST
#define DEFINE_FREDENTRY_DEBUG		DEFINE_FREDENTRY_RAW
#endif

void idt_install_sysvec(unsigned int n, const void *function);

#ifdef CONFIG_X86_FRED
void fred_install_sysvec(unsigned int vector, const idtentry_t function);
#else
static inline void fred_install_sysvec(unsigned int vector, const idtentry_t function) { }
#endif

#define sysvec_install(vector, function) {				\
	if (cpu_feature_enabled(X86_FEATURE_FRED))			\
		fred_install_sysvec(vector, function);			\
	else								\
		idt_install_sysvec(vector, asm_##function);		\
}

#else /* !__ASSEMBLER__ */

/*
 * The ASM variants for DECLARE_IDTENTRY*() which emit the ASM entry stubs.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	idtentry vector asm_##func func has_error_code=0

#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	idtentry vector asm_##func func has_error_code=1

/* Special case for 32bit IRET 'trap'. Do not emit ASM code */
#define DECLARE_IDTENTRY_SW(vector, func)

#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/* Entries for common/spurious (device) interrupts */
#define DECLARE_IDTENTRY_IRQ(vector, func)				\
	idtentry_irq vector func

/* System vector entries */
#define DECLARE_IDTENTRY_SYSVEC(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#ifdef CONFIG_X86_64
# define DECLARE_IDTENTRY_MCE(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DEBUG(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DF(vector, func)				\
	idtentry_df vector asm_##func func

# define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

# define DECLARE_IDTENTRY_VC(vector, func)				\
	idtentry_vc vector asm_##func func

#else
# define DECLARE_IDTENTRY_MCE(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/* No ASM emitted for DF as this goes through a C shim */
# define DECLARE_IDTENTRY_DF(vector, func)

/* No ASM emitted for XEN hypervisor callback */
# define DECLARE_IDTENTRY_XENCB(vector, func)

#endif

/* No ASM code emitted for NMI */
#define DECLARE_IDTENTRY_NMI(vector, func)

/*
 * ASM code to emit the common vector entry stubs where each stub is
 * packed into IDT_ALIGN bytes.
 *
 * Note, that the 'pushq imm8' is emitted via '.byte 0x6a, vector' because
 * GCC treats the local vector variable as unsigned int and would expand
 * all vectors above 0x7F to a 5 byte push. The original code did an
 * adjustment of the vector number to be in the signed byte range to avoid
 * this. While clever it's mindboggling counterintuitive and requires the
 * odd conversion back to a real vector number in the C entry points. Using
 * .byte achieves the same thing and the only fixup needed in the C entry
 * point is to mask off the bits above bit 7 because the push is sign
 * extending.
 */
	.align IDT_ALIGN
SYM_CODE_START(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept NR_EXTERNAL_VECTORS
	UNWIND_HINT_IRET_REGS
0 :
	ENDBR
	.byte	0x6a, vector
	jmp	asm_common_interrupt
	/* Ensure that the above is IDT_ALIGN bytes max */
	.fill 0b + IDT_ALIGN - ., 1, 0xcc
	vector = vector+1
    .endr
SYM_CODE_END(irq_entries_start)

#ifdef CONFIG_X86_LOCAL_APIC
	.align IDT_ALIGN
SYM_CODE_START(spurious_entries_start)
    vector=FIRST_SYSTEM_VECTOR
    .rept NR_SYSTEM_VECTORS
	UNWIND_HINT_IRET_REGS
0 :
	ENDBR
	.byte	0x6a, vector
	jmp	asm_spurious_interrupt
	/* Ensure that the above is IDT_ALIGN bytes max */
	.fill 0b + IDT_ALIGN - ., 1, 0xcc
	vector = vector+1
    .endr
SYM_CODE_END(spurious_entries_start)
#endif

#endif /* __ASSEMBLER__ */

/*
 * The actual entry points. Note that DECLARE_IDTENTRY*() serves two
 * purposes:
 *  - provide the function declarations when included from C-Code
 *  - emit the ASM stubs when included from entry_32/64.S
 *
 * This avoids duplicate defines and ensures that everything is consistent.
 */

/*
 * Dummy trap number so the low level ASM macro vector number checks do not
 * match which results in emitting plain IDTENTRY stubs without bells and
 * whistles.
 */
#define X86_TRAP_OTHER		0xFFFF

/* Simple exception entry points. No hardware error code */
DECLARE_IDTENTRY(X86_TRAP_DE,		exc_divide_error);
DECLARE_IDTENTRY(X86_TRAP_OF,		exc_overflow);
DECLARE_IDTENTRY(X86_TRAP_BR,		exc_bounds);
DECLARE_IDTENTRY(X86_TRAP_NM,		exc_device_not_available);
DECLARE_IDTENTRY(X86_TRAP_OLD_MF,	exc_coproc_segment_overrun);
DECLARE_IDTENTRY(X86_TRAP_SPURIOUS,	exc_spurious_interrupt_bug);
DECLARE_IDTENTRY(X86_TRAP_MF,		exc_coprocessor_error);
DECLARE_IDTENTRY(X86_TRAP_XF,		exc_simd_coprocessor_error);

/* 32bit software IRET trap. Do not emit ASM code */
DECLARE_IDTENTRY_SW(X86_TRAP_IRET,	iret_error);

/* Simple exception entries with error code pushed by hardware */
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_TS,	exc_invalid_tss);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_NP,	exc_segment_not_present);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_SS,	exc_stack_segment);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_GP,	exc_general_protection);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_AC,	exc_alignment_check);

/* Raw exception entries which need extra work */
DECLARE_IDTENTRY_RAW(X86_TRAP_UD,		exc_invalid_op);
DECLARE_IDTENTRY_RAW(X86_TRAP_BP,		exc_int3);
DECLARE_IDTENTRY_RAW_ERRORCODE(X86_TRAP_PF,	exc_page_fault);

#if defined(CONFIG_IA32_EMULATION)
DECLARE_IDTENTRY_RAW(IA32_SYSCALL_VECTOR,	int80_emulation);
#endif

#ifdef CONFIG_X86_MCE
#ifdef CONFIG_X86_64
DECLARE_IDTENTRY_MCE(X86_TRAP_MC,	exc_machine_check);
#else
DECLARE_IDTENTRY_RAW(X86_TRAP_MC,	exc_machine_check);
#endif
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW(X86_TRAP_MC,	xenpv_exc_machine_check);
#endif
#endif

/* NMI */

#if IS_ENABLED(CONFIG_KVM_INTEL)
/*
 * Special entry point for VMX which invokes this on the kernel stack, even for
 * 64-bit, i.e. without using an IST.  asm_exc_nmi() requires an IST to work
 * correctly vs. the NMI 'executing' marker.  Used for 32-bit kernels as well
 * to avoid more ifdeffery.
 */
DECLARE_IDTENTRY(X86_TRAP_NMI,		exc_nmi_kvm_vmx);
#endif

DECLARE_IDTENTRY_NMI(X86_TRAP_NMI,	exc_nmi);
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW(X86_TRAP_NMI,	xenpv_exc_nmi);
#endif

/* #DB */
#ifdef CONFIG_X86_64
DECLARE_IDTENTRY_DEBUG(X86_TRAP_DB,	exc_debug);
#else
DECLARE_IDTENTRY_RAW(X86_TRAP_DB,	exc_debug);
#endif
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW(X86_TRAP_DB,	xenpv_exc_debug);
#endif

/* #DF */
DECLARE_IDTENTRY_DF(X86_TRAP_DF,	exc_double_fault);
#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_RAW_ERRORCODE(X86_TRAP_DF,	xenpv_exc_double_fault);
#endif

/* #CP */
#ifdef CONFIG_X86_CET
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_CP,	exc_control_protection);
#endif

/* #VC */
#ifdef CONFIG_AMD_MEM_ENCRYPT
DECLARE_IDTENTRY_VC(X86_TRAP_VC,	exc_vmm_communication);
#endif

#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_XENCB(X86_TRAP_OTHER,	exc_xen_hypervisor_callback);
DECLARE_IDTENTRY_RAW(X86_TRAP_OTHER,	exc_xen_unknown_trap);
#endif

#ifdef CONFIG_INTEL_TDX_GUEST
DECLARE_IDTENTRY(X86_TRAP_VE,		exc_virtualization_exception);
#endif

/* Device interrupts common/spurious */
DECLARE_IDTENTRY_IRQ(X86_TRAP_OTHER,	common_interrupt);
#ifdef CONFIG_X86_LOCAL_APIC
DECLARE_IDTENTRY_IRQ(X86_TRAP_OTHER,	spurious_interrupt);
#endif

/* System vector entry points */
#ifdef CONFIG_X86_LOCAL_APIC
DECLARE_IDTENTRY_SYSVEC(ERROR_APIC_VECTOR,		sysvec_error_interrupt);
DECLARE_IDTENTRY_SYSVEC(SPURIOUS_APIC_VECTOR,		sysvec_spurious_apic_interrupt);
DECLARE_IDTENTRY_SYSVEC(LOCAL_TIMER_VECTOR,		sysvec_apic_timer_interrupt);
DECLARE_IDTENTRY_SYSVEC(X86_PLATFORM_IPI_VECTOR,	sysvec_x86_platform_ipi);
#endif

#ifdef CONFIG_SMP
DECLARE_IDTENTRY(RESCHEDULE_VECTOR,			sysvec_reschedule_ipi);
DECLARE_IDTENTRY_SYSVEC(REBOOT_VECTOR,			sysvec_reboot);
DECLARE_IDTENTRY_SYSVEC(CALL_FUNCTION_SINGLE_VECTOR,	sysvec_call_function_single);
DECLARE_IDTENTRY_SYSVEC(CALL_FUNCTION_VECTOR,		sysvec_call_function);
#else
# define fred_sysvec_reschedule_ipi			NULL
# define fred_sysvec_reboot				NULL
# define fred_sysvec_call_function_single		NULL
# define fred_sysvec_call_function			NULL
#endif

#ifdef CONFIG_X86_LOCAL_APIC
# ifdef CONFIG_X86_MCE_THRESHOLD
DECLARE_IDTENTRY_SYSVEC(THRESHOLD_APIC_VECTOR,		sysvec_threshold);
# else
# define fred_sysvec_threshold				NULL
# endif

# ifdef CONFIG_X86_MCE_AMD
DECLARE_IDTENTRY_SYSVEC(DEFERRED_ERROR_VECTOR,		sysvec_deferred_error);
# else
# define fred_sysvec_deferred_error			NULL
# endif

# ifdef CONFIG_X86_THERMAL_VECTOR
DECLARE_IDTENTRY_SYSVEC(THERMAL_APIC_VECTOR,		sysvec_thermal);
# else
# define fred_sysvec_thermal				NULL
# endif

# ifdef CONFIG_IRQ_WORK
DECLARE_IDTENTRY_SYSVEC(IRQ_WORK_VECTOR,		sysvec_irq_work);
# else
# define fred_sysvec_irq_work				NULL
# endif
#endif

#if IS_ENABLED(CONFIG_KVM)
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_VECTOR,		sysvec_kvm_posted_intr_ipi);
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_WAKEUP_VECTOR,	sysvec_kvm_posted_intr_wakeup_ipi);
DECLARE_IDTENTRY_SYSVEC(POSTED_INTR_NESTED_VECTOR,	sysvec_kvm_posted_intr_nested_ipi);
#else
# define fred_sysvec_kvm_posted_intr_ipi		NULL
# define fred_sysvec_kvm_posted_intr_wakeup_ipi		NULL
# define fred_sysvec_kvm_posted_intr_nested_ipi		NULL
#endif

# ifdef CONFIG_X86_POSTED_MSI
DECLARE_IDTENTRY_SYSVEC(POSTED_MSI_NOTIFICATION_VECTOR,	sysvec_posted_msi_notification);
#else
# define fred_sysvec_posted_msi_notification		NULL
# endif

#if IS_ENABLED(CONFIG_HYPERV)
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_hyperv_callback);
DECLARE_IDTENTRY_SYSVEC(HYPERV_REENLIGHTENMENT_VECTOR,	sysvec_hyperv_reenlightenment);
DECLARE_IDTENTRY_SYSVEC(HYPERV_STIMER0_VECTOR,		sysvec_hyperv_stimer0);
#endif

#if IS_ENABLED(CONFIG_ACRN_GUEST)
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_acrn_hv_callback);
#endif

#ifdef CONFIG_XEN_PVHVM
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_xen_hvm_callback);
#endif

#ifdef CONFIG_KVM_GUEST
DECLARE_IDTENTRY_SYSVEC(HYPERVISOR_CALLBACK_VECTOR,	sysvec_kvm_asyncpf_interrupt);
#endif

#undef X86_TRAP_OTHER

#endif
