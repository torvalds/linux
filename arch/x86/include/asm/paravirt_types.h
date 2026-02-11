/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PARAVIRT_TYPES_H
#define _ASM_X86_PARAVIRT_TYPES_H

#ifdef CONFIG_PARAVIRT

#ifndef __ASSEMBLER__
#include <linux/types.h>

#include <asm/paravirt-base.h>
#include <asm/desc_defs.h>
#include <asm/pgtable_types.h>
#include <asm/nospec-branch.h>

struct thread_struct;
struct mm_struct;
struct task_struct;
struct cpumask;
struct flush_tlb_info;
struct vm_area_struct;

#ifdef CONFIG_PARAVIRT_XXL
struct pv_lazy_ops {
	/* Set deferred update mode, used for batching operations. */
	void (*enter)(void);
	void (*leave)(void);
	void (*flush)(void);
} __no_randomize_layout;
#endif

struct pv_cpu_ops {
	/* hooks for various privileged instructions */
	void (*io_delay)(void);

#ifdef CONFIG_PARAVIRT_XXL
	unsigned long (*get_debugreg)(int regno);
	void (*set_debugreg)(int regno, unsigned long value);

	unsigned long (*read_cr0)(void);
	void (*write_cr0)(unsigned long);

	void (*write_cr4)(unsigned long);

	/* Segment descriptor handling */
	void (*load_tr_desc)(void);
	void (*load_gdt)(const struct desc_ptr *);
	void (*load_idt)(const struct desc_ptr *);
	void (*set_ldt)(const void *desc, unsigned entries);
	unsigned long (*store_tr)(void);
	void (*load_tls)(struct thread_struct *t, unsigned int cpu);
	void (*load_gs_index)(unsigned int idx);
	void (*write_ldt_entry)(struct desc_struct *ldt, int entrynum,
				const void *desc);
	void (*write_gdt_entry)(struct desc_struct *,
				int entrynum, const void *desc, int size);
	void (*write_idt_entry)(gate_desc *,
				int entrynum, const gate_desc *gate);
	void (*alloc_ldt)(struct desc_struct *ldt, unsigned entries);
	void (*free_ldt)(struct desc_struct *ldt, unsigned entries);

	void (*load_sp0)(unsigned long sp0);

#ifdef CONFIG_X86_IOPL_IOPERM
	void (*invalidate_io_bitmap)(void);
	void (*update_io_bitmap)(void);
#endif

	/* cpuid emulation, mostly so that caps bits can be disabled */
	void (*cpuid)(unsigned int *eax, unsigned int *ebx,
		      unsigned int *ecx, unsigned int *edx);

	/* Unsafe MSR operations.  These will warn or panic on failure. */
	u64 (*read_msr)(u32 msr);
	void (*write_msr)(u32 msr, u64 val);

	/*
	 * Safe MSR operations.
	 * Returns 0 or -EIO.
	 */
	int (*read_msr_safe)(u32 msr, u64 *val);
	int (*write_msr_safe)(u32 msr, u64 val);

	u64 (*read_pmc)(int counter);

	void (*start_context_switch)(struct task_struct *prev);
	void (*end_context_switch)(struct task_struct *next);
#endif
} __no_randomize_layout;

struct pv_irq_ops {
#ifdef CONFIG_PARAVIRT_XXL
	/*
	 * Get/set interrupt state.  save_fl is expected to use X86_EFLAGS_IF;
	 * all other bits returned from save_fl are undefined.
	 *
	 * NOTE: These functions callers expect the callee to preserve
	 * more registers than the standard C calling convention.
	 */
	struct paravirt_callee_save save_fl;
	struct paravirt_callee_save irq_disable;
	struct paravirt_callee_save irq_enable;
#endif
	void (*safe_halt)(void);
	void (*halt)(void);
} __no_randomize_layout;

struct pv_mmu_ops {
	/* TLB operations */
	void (*flush_tlb_user)(void);
	void (*flush_tlb_kernel)(void);
	void (*flush_tlb_one_user)(unsigned long addr);
	void (*flush_tlb_multi)(const struct cpumask *cpus,
				const struct flush_tlb_info *info);

	/* Hook for intercepting the destruction of an mm_struct. */
	void (*exit_mmap)(struct mm_struct *mm);
	void (*notify_page_enc_status_changed)(unsigned long pfn, int npages, bool enc);

#ifdef CONFIG_PARAVIRT_XXL
	struct paravirt_callee_save read_cr2;
	void (*write_cr2)(unsigned long);

	unsigned long (*read_cr3)(void);
	void (*write_cr3)(unsigned long);

	/* Hook for intercepting the creation/use of an mm_struct. */
	void (*enter_mmap)(struct mm_struct *mm);

	/* Hooks for allocating and freeing a pagetable top-level */
	int  (*pgd_alloc)(struct mm_struct *mm);
	void (*pgd_free)(struct mm_struct *mm, pgd_t *pgd);

	/*
	 * Hooks for allocating/releasing pagetable pages when they're
	 * attached to a pagetable
	 */
	void (*alloc_pte)(struct mm_struct *mm, unsigned long pfn);
	void (*alloc_pmd)(struct mm_struct *mm, unsigned long pfn);
	void (*alloc_pud)(struct mm_struct *mm, unsigned long pfn);
	void (*alloc_p4d)(struct mm_struct *mm, unsigned long pfn);
	void (*release_pte)(unsigned long pfn);
	void (*release_pmd)(unsigned long pfn);
	void (*release_pud)(unsigned long pfn);
	void (*release_p4d)(unsigned long pfn);

	/* Pagetable manipulation functions */
	void (*set_pte)(pte_t *ptep, pte_t pteval);
	void (*set_pmd)(pmd_t *pmdp, pmd_t pmdval);

	pte_t (*ptep_modify_prot_start)(struct vm_area_struct *vma, unsigned long addr,
					pte_t *ptep);
	void (*ptep_modify_prot_commit)(struct vm_area_struct *vma, unsigned long addr,
					pte_t *ptep, pte_t pte);

	struct paravirt_callee_save pte_val;
	struct paravirt_callee_save make_pte;

	struct paravirt_callee_save pgd_val;
	struct paravirt_callee_save make_pgd;

	void (*set_pud)(pud_t *pudp, pud_t pudval);

	struct paravirt_callee_save pmd_val;
	struct paravirt_callee_save make_pmd;

	struct paravirt_callee_save pud_val;
	struct paravirt_callee_save make_pud;

	void (*set_p4d)(p4d_t *p4dp, p4d_t p4dval);

	struct paravirt_callee_save p4d_val;
	struct paravirt_callee_save make_p4d;

	void (*set_pgd)(pgd_t *pgdp, pgd_t pgdval);

	struct pv_lazy_ops lazy_mode;

	/* dom0 ops */

	/* Sometimes the physical address is a pfn, and sometimes its
	   an mfn.  We can tell which is which from the index. */
	void (*set_fixmap)(unsigned /* enum fixed_addresses */ idx,
			   phys_addr_t phys, pgprot_t flags);
#endif
} __no_randomize_layout;

/* This contains all the paravirt structures: we get a convenient
 * number for each function using the offset which we use to indicate
 * what to patch. */
struct paravirt_patch_template {
	struct pv_cpu_ops	cpu;
	struct pv_irq_ops	irq;
	struct pv_mmu_ops	mmu;
} __no_randomize_layout;

extern struct paravirt_patch_template pv_ops;

#define paravirt_ptr(array, op)	[paravirt_opptr] "m" (array.op)

/*
 * This generates an indirect call based on the operation type number.
 *
 * Since alternatives run after enabling CET/IBT -- the latter setting/clearing
 * capabilities and the former requiring all capabilities being finalized --
 * these indirect calls are subject to IBT and the paravirt stubs should have
 * ENDBR on.
 *
 * OTOH since this is effectively a __nocfi indirect call, the paravirt stubs
 * don't need to bother with CFI prefixes.
 */
#define PARAVIRT_CALL					\
	ANNOTATE_RETPOLINE_SAFE "\n\t"			\
	"call *%[paravirt_opptr]"

/*
 * These macros are intended to wrap calls through one of the paravirt
 * ops structs, so that they can be later identified and patched at
 * runtime.
 *
 * Normally, a call to a pv_op function is a simple indirect call:
 * (pv_op_struct.operations)(args...).
 *
 * Unfortunately, this is a relatively slow operation for modern CPUs,
 * because it cannot necessarily determine what the destination
 * address is.  In this case, the address is a runtime constant, so at
 * the very least we can patch the call to a simple direct call, or,
 * ideally, patch an inline implementation into the callsite.  (Direct
 * calls are essentially free, because the call and return addresses
 * are completely predictable.)
 *
 * For i386, these macros rely on the standard gcc "regparm(3)" calling
 * convention, in which the first three arguments are placed in %eax,
 * %edx, %ecx (in that order), and the remaining arguments are placed
 * on the stack.  All caller-save registers (eax,edx,ecx) are expected
 * to be modified (either clobbered or used for return values).
 * X86_64, on the other hand, already specifies a register-based calling
 * conventions, returning at %rax, with parameters going in %rdi, %rsi,
 * %rdx, and %rcx. Note that for this reason, x86_64 does not need any
 * special handling for dealing with 4 arguments, unlike i386.
 * However, x86_64 also has to clobber all caller saved registers, which
 * unfortunately, are quite a bit (r8 - r11)
 *
 * Unfortunately there's no way to get gcc to generate the args setup
 * for the call, and then allow the call itself to be generated by an
 * inline asm.  Because of this, we must do the complete arg setup and
 * return value handling from within these macros.  This is fairly
 * cumbersome.
 *
 * There are 5 sets of PVOP_* macros for dealing with 0-4 arguments.
 * It could be extended to more arguments, but there would be little
 * to be gained from that.  For each number of arguments, there are
 * two VCALL and CALL variants for void and non-void functions.
 *
 * When there is a return value, the invoker of the macro must specify
 * the return type.  The macro then uses sizeof() on that type to
 * determine whether it's a 32 or 64 bit value and places the return
 * in the right register(s) (just %eax for 32-bit, and %edx:%eax for
 * 64-bit). For x86_64 machines, it just returns in %rax regardless of
 * the return value size.
 *
 * 64-bit arguments are passed as a pair of adjacent 32-bit arguments;
 * i386 also passes 64-bit arguments as a pair of adjacent 32-bit arguments
 * in low,high order
 *
 * Small structures are passed and returned in registers.  The macro
 * calling convention can't directly deal with this, so the wrapper
 * functions must do it.
 *
 * These PVOP_* macros are only defined within this header.  This
 * means that all uses must be wrapped in inline functions.  This also
 * makes sure the incoming and outgoing types are always correct.
 */
#ifdef CONFIG_X86_32
#define PVOP_CALL_ARGS							\
	unsigned long __eax = __eax, __edx = __edx, __ecx = __ecx;

#define PVOP_CALL_ARG1(x)		"a" ((unsigned long)(x))
#define PVOP_CALL_ARG2(x)		"d" ((unsigned long)(x))
#define PVOP_CALL_ARG3(x)		"c" ((unsigned long)(x))

#define PVOP_VCALL_CLOBBERS		"=a" (__eax), "=d" (__edx),	\
					"=c" (__ecx)
#define PVOP_CALL_CLOBBERS		PVOP_VCALL_CLOBBERS

#define PVOP_VCALLEE_CLOBBERS		"=a" (__eax), "=d" (__edx)
#define PVOP_CALLEE_CLOBBERS		PVOP_VCALLEE_CLOBBERS

#define EXTRA_CLOBBERS
#define VEXTRA_CLOBBERS
#else  /* CONFIG_X86_64 */
/* [re]ax isn't an arg, but the return val */
#define PVOP_CALL_ARGS						\
	unsigned long __edi = __edi, __esi = __esi,		\
		__edx = __edx, __ecx = __ecx, __eax = __eax;

#define PVOP_CALL_ARG1(x)		"D" ((unsigned long)(x))
#define PVOP_CALL_ARG2(x)		"S" ((unsigned long)(x))
#define PVOP_CALL_ARG3(x)		"d" ((unsigned long)(x))
#define PVOP_CALL_ARG4(x)		"c" ((unsigned long)(x))

#define PVOP_VCALL_CLOBBERS	"=D" (__edi),				\
				"=S" (__esi), "=d" (__edx),		\
				"=c" (__ecx)
#define PVOP_CALL_CLOBBERS	PVOP_VCALL_CLOBBERS, "=a" (__eax)

/*
 * void functions are still allowed [re]ax for scratch.
 *
 * The ZERO_CALL_USED REGS feature may end up zeroing out callee-saved
 * registers. Make sure we model this with the appropriate clobbers.
 */
#ifdef CONFIG_ZERO_CALL_USED_REGS
#define PVOP_VCALLEE_CLOBBERS	"=a" (__eax), PVOP_VCALL_CLOBBERS
#else
#define PVOP_VCALLEE_CLOBBERS	"=a" (__eax)
#endif
#define PVOP_CALLEE_CLOBBERS	PVOP_VCALLEE_CLOBBERS

#define EXTRA_CLOBBERS	 , "r8", "r9", "r10", "r11"
#define VEXTRA_CLOBBERS	 , "rax", "r8", "r9", "r10", "r11"
#endif	/* CONFIG_X86_32 */

#define PVOP_RETVAL(rettype)						\
	({	unsigned long __mask = ~0UL;				\
		BUILD_BUG_ON(sizeof(rettype) > sizeof(unsigned long));	\
		switch (sizeof(rettype)) {				\
		case 1: __mask =       0xffUL; break;			\
		case 2: __mask =     0xffffUL; break;			\
		case 4: __mask = 0xffffffffUL; break;			\
		default: break;						\
		}							\
		__mask & __eax;						\
	})

/*
 * Use alternative patching for paravirt calls:
 * - For replacing an indirect call with a direct one, use the "normal"
 *   ALTERNATIVE() macro with the indirect call as the initial code sequence,
 *   which will be replaced with the related direct call by using the
 *   ALT_FLAG_DIRECT_CALL special case and the "always on" feature.
 * - In case the replacement is either a direct call or a short code sequence
 *   depending on a feature bit, the ALTERNATIVE_2() macro is being used.
 *   The indirect call is the initial code sequence again, while the special
 *   code sequence is selected with the specified feature bit. In case the
 *   feature is not active, the direct call is used as above via the
 *   ALT_FLAG_DIRECT_CALL special case and the "always on" feature.
 */
#define ____PVOP_CALL(ret, array, op, call_clbr, extra_clbr, ...)	\
	({								\
		PVOP_CALL_ARGS;						\
		asm volatile(ALTERNATIVE(PARAVIRT_CALL, ALT_CALL_INSTR,	\
				ALT_CALL_ALWAYS)			\
			     : call_clbr, ASM_CALL_CONSTRAINT		\
			     : paravirt_ptr(array, op),			\
			       ##__VA_ARGS__				\
			     : "memory", "cc" extra_clbr);		\
		ret;							\
	})

#define ____PVOP_ALT_CALL(ret, array, op, alt, cond, call_clbr,		\
			  extra_clbr, ...)				\
	({								\
		PVOP_CALL_ARGS;						\
		asm volatile(ALTERNATIVE_2(PARAVIRT_CALL,		\
				 ALT_CALL_INSTR, ALT_CALL_ALWAYS,	\
				 alt, cond)				\
			     : call_clbr, ASM_CALL_CONSTRAINT		\
			     : paravirt_ptr(array, op),			\
			       ##__VA_ARGS__				\
			     : "memory", "cc" extra_clbr);		\
		ret;							\
	})

#define __PVOP_CALL(rettype, array, op, ...)				\
	____PVOP_CALL(PVOP_RETVAL(rettype), array, op,			\
		      PVOP_CALL_CLOBBERS, EXTRA_CLOBBERS, ##__VA_ARGS__)

#define __PVOP_ALT_CALL(rettype, array, op, alt, cond, ...)		\
	____PVOP_ALT_CALL(PVOP_RETVAL(rettype), array, op, alt, cond,	\
			  PVOP_CALL_CLOBBERS, EXTRA_CLOBBERS,		\
			  ##__VA_ARGS__)

#define __PVOP_CALLEESAVE(rettype, array, op, ...)			\
	____PVOP_CALL(PVOP_RETVAL(rettype), array, op.func,		\
		      PVOP_CALLEE_CLOBBERS, , ##__VA_ARGS__)

#define __PVOP_ALT_CALLEESAVE(rettype, array, op, alt, cond, ...)	\
	____PVOP_ALT_CALL(PVOP_RETVAL(rettype), array, op.func, alt, cond, \
			  PVOP_CALLEE_CLOBBERS, , ##__VA_ARGS__)


#define __PVOP_VCALL(array, op, ...)					\
	(void)____PVOP_CALL(, array, op, PVOP_VCALL_CLOBBERS,		\
		       VEXTRA_CLOBBERS, ##__VA_ARGS__)

#define __PVOP_ALT_VCALL(array, op, alt, cond, ...)			\
	(void)____PVOP_ALT_CALL(, array, op, alt, cond,			\
				PVOP_VCALL_CLOBBERS, VEXTRA_CLOBBERS,	\
				##__VA_ARGS__)

#define __PVOP_VCALLEESAVE(array, op, ...)				\
	(void)____PVOP_CALL(, array, op.func,				\
			    PVOP_VCALLEE_CLOBBERS, , ##__VA_ARGS__)

#define __PVOP_ALT_VCALLEESAVE(array, op, alt, cond, ...)		\
	(void)____PVOP_ALT_CALL(, array, op.func, alt, cond,		\
				PVOP_VCALLEE_CLOBBERS, , ##__VA_ARGS__)


#define PVOP_CALL0(rettype, array, op)					\
	__PVOP_CALL(rettype, array, op)
#define PVOP_VCALL0(array, op)						\
	__PVOP_VCALL(array, op)
#define PVOP_ALT_CALL0(rettype, array, op, alt, cond)			\
	__PVOP_ALT_CALL(rettype, array, op, alt, cond)
#define PVOP_ALT_VCALL0(array, op, alt, cond)				\
	__PVOP_ALT_VCALL(array, op, alt, cond)

#define PVOP_CALLEE0(rettype, array, op)				\
	__PVOP_CALLEESAVE(rettype, array, op)
#define PVOP_VCALLEE0(array, op)					\
	__PVOP_VCALLEESAVE(array, op)
#define PVOP_ALT_CALLEE0(rettype, array, op, alt, cond)			\
	__PVOP_ALT_CALLEESAVE(rettype, array, op, alt, cond)
#define PVOP_ALT_VCALLEE0(array, op, alt, cond)				\
	__PVOP_ALT_VCALLEESAVE(array, op, alt, cond)


#define PVOP_CALL1(rettype, array, op, arg1)				\
	__PVOP_CALL(rettype, array, op, PVOP_CALL_ARG1(arg1))
#define PVOP_VCALL1(array, op, arg1)					\
	__PVOP_VCALL(array, op, PVOP_CALL_ARG1(arg1))
#define PVOP_ALT_VCALL1(array, op, arg1, alt, cond)			\
	__PVOP_ALT_VCALL(array, op, alt, cond, PVOP_CALL_ARG1(arg1))

#define PVOP_CALLEE1(rettype, array, op, arg1)				\
	__PVOP_CALLEESAVE(rettype, array, op, PVOP_CALL_ARG1(arg1))
#define PVOP_VCALLEE1(array, op, arg1)					\
	__PVOP_VCALLEESAVE(array, op, PVOP_CALL_ARG1(arg1))
#define PVOP_ALT_CALLEE1(rettype, array, op, arg1, alt, cond)		\
	__PVOP_ALT_CALLEESAVE(rettype, array, op, alt, cond, PVOP_CALL_ARG1(arg1))
#define PVOP_ALT_VCALLEE1(array, op, arg1, alt, cond)			\
	__PVOP_ALT_VCALLEESAVE(array, op, alt, cond, PVOP_CALL_ARG1(arg1))


#define PVOP_CALL2(rettype, array, op, arg1, arg2)			\
	__PVOP_CALL(rettype, array, op, PVOP_CALL_ARG1(arg1), PVOP_CALL_ARG2(arg2))
#define PVOP_VCALL2(array, op, arg1, arg2)				\
	__PVOP_VCALL(array, op, PVOP_CALL_ARG1(arg1), PVOP_CALL_ARG2(arg2))

#define PVOP_CALL3(rettype, array, op, arg1, arg2, arg3)		\
	__PVOP_CALL(rettype, array, op, PVOP_CALL_ARG1(arg1),		\
		    PVOP_CALL_ARG2(arg2), PVOP_CALL_ARG3(arg3))
#define PVOP_VCALL3(array, op, arg1, arg2, arg3)			\
	__PVOP_VCALL(array, op, PVOP_CALL_ARG1(arg1),			\
		     PVOP_CALL_ARG2(arg2), PVOP_CALL_ARG3(arg3))

#define PVOP_CALL4(rettype, array, op, arg1, arg2, arg3, arg4)		\
	__PVOP_CALL(rettype, array, op,					\
		    PVOP_CALL_ARG1(arg1), PVOP_CALL_ARG2(arg2),		\
		    PVOP_CALL_ARG3(arg3), PVOP_CALL_ARG4(arg4))
#define PVOP_VCALL4(array, op, arg1, arg2, arg3, arg4)			\
	__PVOP_VCALL(array, op, PVOP_CALL_ARG1(arg1), PVOP_CALL_ARG2(arg2), \
		     PVOP_CALL_ARG3(arg3), PVOP_CALL_ARG4(arg4))

#endif	/* __ASSEMBLER__ */

#define ALT_NOT_XEN	ALT_NOT(X86_FEATURE_XENPV)

#ifdef CONFIG_X86_32
/* save and restore all caller-save registers, except return value */
#define PV_SAVE_ALL_CALLER_REGS		"pushl %ecx;"
#define PV_RESTORE_ALL_CALLER_REGS	"popl  %ecx;"
#else
/* save and restore all caller-save registers, except return value */
#define PV_SAVE_ALL_CALLER_REGS						\
	"push %rcx;"							\
	"push %rdx;"							\
	"push %rsi;"							\
	"push %rdi;"							\
	"push %r8;"							\
	"push %r9;"							\
	"push %r10;"							\
	"push %r11;"
#define PV_RESTORE_ALL_CALLER_REGS					\
	"pop %r11;"							\
	"pop %r10;"							\
	"pop %r9;"							\
	"pop %r8;"							\
	"pop %rdi;"							\
	"pop %rsi;"							\
	"pop %rdx;"							\
	"pop %rcx;"
#endif

/*
 * Generate a thunk around a function which saves all caller-save
 * registers except for the return value.  This allows C functions to
 * be called from assembler code where fewer than normal registers are
 * available.  It may also help code generation around calls from C
 * code if the common case doesn't use many registers.
 *
 * When a callee is wrapped in a thunk, the caller can assume that all
 * arg regs and all scratch registers are preserved across the
 * call. The return value in rax/eax will not be saved, even for void
 * functions.
 */
#define PV_THUNK_NAME(func) "__raw_callee_save_" #func
#define __PV_CALLEE_SAVE_REGS_THUNK(func, section)			\
	extern typeof(func) __raw_callee_save_##func;			\
									\
	asm(".pushsection " section ", \"ax\";"				\
	    ".globl " PV_THUNK_NAME(func) ";"				\
	    ".type " PV_THUNK_NAME(func) ", @function;"			\
	    ASM_FUNC_ALIGN						\
	    PV_THUNK_NAME(func) ":"					\
	    ASM_ENDBR							\
	    FRAME_BEGIN							\
	    PV_SAVE_ALL_CALLER_REGS					\
	    "call " #func ";"						\
	    PV_RESTORE_ALL_CALLER_REGS					\
	    FRAME_END							\
	    ASM_RET							\
	    ".size " PV_THUNK_NAME(func) ", .-" PV_THUNK_NAME(func) ";"	\
	    ".popsection")

#define PV_CALLEE_SAVE_REGS_THUNK(func)			\
	__PV_CALLEE_SAVE_REGS_THUNK(func, ".text")

/* Get a reference to a callee-save function */
#define PV_CALLEE_SAVE(func)						\
	((struct paravirt_callee_save) { __raw_callee_save_##func })

/* Promise that "func" already uses the right calling convention */
#define __PV_IS_CALLEE_SAVE(func)			\
	((struct paravirt_callee_save) { func })

#endif  /* CONFIG_PARAVIRT */
#endif	/* _ASM_X86_PARAVIRT_TYPES_H */
