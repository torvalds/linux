/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

#include <asm/paravirt_types.h>

#ifndef __ASSEMBLER__
struct mm_struct;
#endif

#ifdef CONFIG_PARAVIRT
#include <asm/pgtable_types.h>
#include <asm/asm.h>
#include <asm/nospec-branch.h>

#ifndef __ASSEMBLER__
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/static_call_types.h>
#include <asm/frame.h>

u64 dummy_steal_clock(int cpu);
u64 dummy_sched_clock(void);

DECLARE_STATIC_CALL(pv_steal_clock, dummy_steal_clock);
DECLARE_STATIC_CALL(pv_sched_clock, dummy_sched_clock);

void paravirt_set_sched_clock(u64 (*func)(void));

static __always_inline u64 paravirt_sched_clock(void)
{
	return static_call(pv_sched_clock)();
}

struct static_key;
extern struct static_key paravirt_steal_enabled;
extern struct static_key paravirt_steal_rq_enabled;

__visible void __native_queued_spin_unlock(struct qspinlock *lock);
bool pv_is_native_spin_unlock(void);
__visible bool __native_vcpu_is_preempted(long cpu);
bool pv_is_native_vcpu_is_preempted(void);

static inline u64 paravirt_steal_clock(int cpu)
{
	return static_call(pv_steal_clock)(cpu);
}

#ifdef CONFIG_PARAVIRT_SPINLOCKS
void __init paravirt_set_cap(void);
#endif

/* The paravirtualized I/O functions */
static inline void slow_down_io(void)
{
	PVOP_VCALL0(cpu.io_delay);
#ifdef REALLY_SLOW_IO
	PVOP_VCALL0(cpu.io_delay);
	PVOP_VCALL0(cpu.io_delay);
	PVOP_VCALL0(cpu.io_delay);
#endif
}

void native_flush_tlb_local(void);
void native_flush_tlb_global(void);
void native_flush_tlb_one_user(unsigned long addr);
void native_flush_tlb_multi(const struct cpumask *cpumask,
			     const struct flush_tlb_info *info);

static inline void __flush_tlb_local(void)
{
	PVOP_VCALL0(mmu.flush_tlb_user);
}

static inline void __flush_tlb_global(void)
{
	PVOP_VCALL0(mmu.flush_tlb_kernel);
}

static inline void __flush_tlb_one_user(unsigned long addr)
{
	PVOP_VCALL1(mmu.flush_tlb_one_user, addr);
}

static inline void __flush_tlb_multi(const struct cpumask *cpumask,
				      const struct flush_tlb_info *info)
{
	PVOP_VCALL2(mmu.flush_tlb_multi, cpumask, info);
}

static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
	PVOP_VCALL1(mmu.exit_mmap, mm);
}

static inline void notify_page_enc_status_changed(unsigned long pfn,
						  int npages, bool enc)
{
	PVOP_VCALL3(mmu.notify_page_enc_status_changed, pfn, npages, enc);
}

static __always_inline void arch_safe_halt(void)
{
	PVOP_VCALL0(irq.safe_halt);
}

static inline void halt(void)
{
	PVOP_VCALL0(irq.halt);
}

#ifdef CONFIG_PARAVIRT_XXL
static inline void load_sp0(unsigned long sp0)
{
	PVOP_VCALL1(cpu.load_sp0, sp0);
}

/* The paravirtualized CPUID instruction. */
static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
			   unsigned int *ecx, unsigned int *edx)
{
	PVOP_VCALL4(cpu.cpuid, eax, ebx, ecx, edx);
}

/*
 * These special macros can be used to get or set a debugging register
 */
static __always_inline unsigned long paravirt_get_debugreg(int reg)
{
	return PVOP_CALL1(unsigned long, cpu.get_debugreg, reg);
}
#define get_debugreg(var, reg) var = paravirt_get_debugreg(reg)
static __always_inline void set_debugreg(unsigned long val, int reg)
{
	PVOP_VCALL2(cpu.set_debugreg, reg, val);
}

static inline unsigned long read_cr0(void)
{
	return PVOP_CALL0(unsigned long, cpu.read_cr0);
}

static inline void write_cr0(unsigned long x)
{
	PVOP_VCALL1(cpu.write_cr0, x);
}

static __always_inline unsigned long read_cr2(void)
{
	return PVOP_ALT_CALLEE0(unsigned long, mmu.read_cr2,
				"mov %%cr2, %%rax;", ALT_NOT_XEN);
}

static __always_inline void write_cr2(unsigned long x)
{
	PVOP_VCALL1(mmu.write_cr2, x);
}

static inline unsigned long __read_cr3(void)
{
	return PVOP_ALT_CALL0(unsigned long, mmu.read_cr3,
			      "mov %%cr3, %%rax;", ALT_NOT_XEN);
}

static inline void write_cr3(unsigned long x)
{
	PVOP_ALT_VCALL1(mmu.write_cr3, x, "mov %%rdi, %%cr3", ALT_NOT_XEN);
}

static inline void __write_cr4(unsigned long x)
{
	PVOP_VCALL1(cpu.write_cr4, x);
}

static inline u64 paravirt_read_msr(u32 msr)
{
	return PVOP_CALL1(u64, cpu.read_msr, msr);
}

static inline void paravirt_write_msr(u32 msr, u64 val)
{
	PVOP_VCALL2(cpu.write_msr, msr, val);
}

static inline int paravirt_read_msr_safe(u32 msr, u64 *val)
{
	return PVOP_CALL2(int, cpu.read_msr_safe, msr, val);
}

static inline int paravirt_write_msr_safe(u32 msr, u64 val)
{
	return PVOP_CALL2(int, cpu.write_msr_safe, msr, val);
}

#define rdmsr(msr, val1, val2)			\
do {						\
	u64 _l = paravirt_read_msr(msr);	\
	val1 = (u32)_l;				\
	val2 = _l >> 32;			\
} while (0)

static __always_inline void wrmsr(u32 msr, u32 low, u32 high)
{
	paravirt_write_msr(msr, (u64)high << 32 | low);
}

#define rdmsrq(msr, val)			\
do {						\
	val = paravirt_read_msr(msr);		\
} while (0)

static inline void wrmsrq(u32 msr, u64 val)
{
	paravirt_write_msr(msr, val);
}

static inline int wrmsrq_safe(u32 msr, u64 val)
{
	return paravirt_write_msr_safe(msr, val);
}

/* rdmsr with exception handling */
#define rdmsr_safe(msr, a, b)				\
({							\
	u64 _l;						\
	int _err = paravirt_read_msr_safe((msr), &_l);	\
	(*a) = (u32)_l;					\
	(*b) = (u32)(_l >> 32);				\
	_err;						\
})

static __always_inline int rdmsrq_safe(u32 msr, u64 *p)
{
	return paravirt_read_msr_safe(msr, p);
}

static __always_inline u64 rdpmc(int counter)
{
	return PVOP_CALL1(u64, cpu.read_pmc, counter);
}

static inline void paravirt_alloc_ldt(struct desc_struct *ldt, unsigned entries)
{
	PVOP_VCALL2(cpu.alloc_ldt, ldt, entries);
}

static inline void paravirt_free_ldt(struct desc_struct *ldt, unsigned entries)
{
	PVOP_VCALL2(cpu.free_ldt, ldt, entries);
}

static inline void load_TR_desc(void)
{
	PVOP_VCALL0(cpu.load_tr_desc);
}
static inline void load_gdt(const struct desc_ptr *dtr)
{
	PVOP_VCALL1(cpu.load_gdt, dtr);
}
static inline void load_idt(const struct desc_ptr *dtr)
{
	PVOP_VCALL1(cpu.load_idt, dtr);
}
static inline void set_ldt(const void *addr, unsigned entries)
{
	PVOP_VCALL2(cpu.set_ldt, addr, entries);
}
static inline unsigned long paravirt_store_tr(void)
{
	return PVOP_CALL0(unsigned long, cpu.store_tr);
}

#define store_tr(tr)	((tr) = paravirt_store_tr())
static inline void load_TLS(struct thread_struct *t, unsigned cpu)
{
	PVOP_VCALL2(cpu.load_tls, t, cpu);
}

static inline void load_gs_index(unsigned int gs)
{
	PVOP_VCALL1(cpu.load_gs_index, gs);
}

static inline void write_ldt_entry(struct desc_struct *dt, int entry,
				   const void *desc)
{
	PVOP_VCALL3(cpu.write_ldt_entry, dt, entry, desc);
}

static inline void write_gdt_entry(struct desc_struct *dt, int entry,
				   void *desc, int type)
{
	PVOP_VCALL4(cpu.write_gdt_entry, dt, entry, desc, type);
}

static inline void write_idt_entry(gate_desc *dt, int entry, const gate_desc *g)
{
	PVOP_VCALL3(cpu.write_idt_entry, dt, entry, g);
}

#ifdef CONFIG_X86_IOPL_IOPERM
static inline void tss_invalidate_io_bitmap(void)
{
	PVOP_VCALL0(cpu.invalidate_io_bitmap);
}

static inline void tss_update_io_bitmap(void)
{
	PVOP_VCALL0(cpu.update_io_bitmap);
}
#endif

static inline void paravirt_enter_mmap(struct mm_struct *next)
{
	PVOP_VCALL1(mmu.enter_mmap, next);
}

static inline int paravirt_pgd_alloc(struct mm_struct *mm)
{
	return PVOP_CALL1(int, mmu.pgd_alloc, mm);
}

static inline void paravirt_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	PVOP_VCALL2(mmu.pgd_free, mm, pgd);
}

static inline void paravirt_alloc_pte(struct mm_struct *mm, unsigned long pfn)
{
	PVOP_VCALL2(mmu.alloc_pte, mm, pfn);
}
static inline void paravirt_release_pte(unsigned long pfn)
{
	PVOP_VCALL1(mmu.release_pte, pfn);
}

static inline void paravirt_alloc_pmd(struct mm_struct *mm, unsigned long pfn)
{
	PVOP_VCALL2(mmu.alloc_pmd, mm, pfn);
}

static inline void paravirt_release_pmd(unsigned long pfn)
{
	PVOP_VCALL1(mmu.release_pmd, pfn);
}

static inline void paravirt_alloc_pud(struct mm_struct *mm, unsigned long pfn)
{
	PVOP_VCALL2(mmu.alloc_pud, mm, pfn);
}
static inline void paravirt_release_pud(unsigned long pfn)
{
	PVOP_VCALL1(mmu.release_pud, pfn);
}

static inline void paravirt_alloc_p4d(struct mm_struct *mm, unsigned long pfn)
{
	PVOP_VCALL2(mmu.alloc_p4d, mm, pfn);
}

static inline void paravirt_release_p4d(unsigned long pfn)
{
	PVOP_VCALL1(mmu.release_p4d, pfn);
}

static inline pte_t __pte(pteval_t val)
{
	return (pte_t) { PVOP_ALT_CALLEE1(pteval_t, mmu.make_pte, val,
					  "mov %%rdi, %%rax", ALT_NOT_XEN) };
}

static inline pteval_t pte_val(pte_t pte)
{
	return PVOP_ALT_CALLEE1(pteval_t, mmu.pte_val, pte.pte,
				"mov %%rdi, %%rax", ALT_NOT_XEN);
}

static inline pgd_t __pgd(pgdval_t val)
{
	return (pgd_t) { PVOP_ALT_CALLEE1(pgdval_t, mmu.make_pgd, val,
					  "mov %%rdi, %%rax", ALT_NOT_XEN) };
}

static inline pgdval_t pgd_val(pgd_t pgd)
{
	return PVOP_ALT_CALLEE1(pgdval_t, mmu.pgd_val, pgd.pgd,
				"mov %%rdi, %%rax", ALT_NOT_XEN);
}

#define  __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
static inline pte_t ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr,
					   pte_t *ptep)
{
	pteval_t ret;

	ret = PVOP_CALL3(pteval_t, mmu.ptep_modify_prot_start, vma, addr, ptep);

	return (pte_t) { .pte = ret };
}

static inline void ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr,
					   pte_t *ptep, pte_t old_pte, pte_t pte)
{

	PVOP_VCALL4(mmu.ptep_modify_prot_commit, vma, addr, ptep, pte.pte);
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	PVOP_VCALL2(mmu.set_pte, ptep, pte.pte);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	PVOP_VCALL2(mmu.set_pmd, pmdp, native_pmd_val(pmd));
}

static inline pmd_t __pmd(pmdval_t val)
{
	return (pmd_t) { PVOP_ALT_CALLEE1(pmdval_t, mmu.make_pmd, val,
					  "mov %%rdi, %%rax", ALT_NOT_XEN) };
}

static inline pmdval_t pmd_val(pmd_t pmd)
{
	return PVOP_ALT_CALLEE1(pmdval_t, mmu.pmd_val, pmd.pmd,
				"mov %%rdi, %%rax", ALT_NOT_XEN);
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	PVOP_VCALL2(mmu.set_pud, pudp, native_pud_val(pud));
}

static inline pud_t __pud(pudval_t val)
{
	pudval_t ret;

	ret = PVOP_ALT_CALLEE1(pudval_t, mmu.make_pud, val,
			       "mov %%rdi, %%rax", ALT_NOT_XEN);

	return (pud_t) { ret };
}

static inline pudval_t pud_val(pud_t pud)
{
	return PVOP_ALT_CALLEE1(pudval_t, mmu.pud_val, pud.pud,
				"mov %%rdi, %%rax", ALT_NOT_XEN);
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, native_make_pud(0));
}

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	p4dval_t val = native_p4d_val(p4d);

	PVOP_VCALL2(mmu.set_p4d, p4dp, val);
}

static inline p4d_t __p4d(p4dval_t val)
{
	p4dval_t ret = PVOP_ALT_CALLEE1(p4dval_t, mmu.make_p4d, val,
					"mov %%rdi, %%rax", ALT_NOT_XEN);

	return (p4d_t) { ret };
}

static inline p4dval_t p4d_val(p4d_t p4d)
{
	return PVOP_ALT_CALLEE1(p4dval_t, mmu.p4d_val, p4d.p4d,
				"mov %%rdi, %%rax", ALT_NOT_XEN);
}

static inline void __set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	PVOP_VCALL2(mmu.set_pgd, pgdp, native_pgd_val(pgd));
}

#define set_pgd(pgdp, pgdval) do {					\
	if (pgtable_l5_enabled())						\
		__set_pgd(pgdp, pgdval);				\
	else								\
		set_p4d((p4d_t *)(pgdp), (p4d_t) { (pgdval).pgd });	\
} while (0)

#define pgd_clear(pgdp) do {						\
	if (pgtable_l5_enabled())					\
		set_pgd(pgdp, native_make_pgd(0));			\
} while (0)

static inline void p4d_clear(p4d_t *p4dp)
{
	set_p4d(p4dp, native_make_p4d(0));
}

static inline void set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_pte(ptep, pte);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	set_pte(ptep, native_make_pte(0));
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, native_make_pmd(0));
}

#define  __HAVE_ARCH_START_CONTEXT_SWITCH
static inline void arch_start_context_switch(struct task_struct *prev)
{
	PVOP_VCALL1(cpu.start_context_switch, prev);
}

static inline void arch_end_context_switch(struct task_struct *next)
{
	PVOP_VCALL1(cpu.end_context_switch, next);
}

#define  __HAVE_ARCH_ENTER_LAZY_MMU_MODE
static inline void arch_enter_lazy_mmu_mode(void)
{
	PVOP_VCALL0(mmu.lazy_mode.enter);
}

static inline void arch_leave_lazy_mmu_mode(void)
{
	PVOP_VCALL0(mmu.lazy_mode.leave);
}

static inline void arch_flush_lazy_mmu_mode(void)
{
	PVOP_VCALL0(mmu.lazy_mode.flush);
}

static inline void __set_fixmap(unsigned /* enum fixed_addresses */ idx,
				phys_addr_t phys, pgprot_t flags)
{
	pv_ops.mmu.set_fixmap(idx, phys, flags);
}
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_PARAVIRT_SPINLOCKS)

static __always_inline void pv_queued_spin_lock_slowpath(struct qspinlock *lock,
							u32 val)
{
	PVOP_VCALL2(lock.queued_spin_lock_slowpath, lock, val);
}

static __always_inline void pv_queued_spin_unlock(struct qspinlock *lock)
{
	PVOP_ALT_VCALLEE1(lock.queued_spin_unlock, lock,
			  "movb $0, (%%" _ASM_ARG1 ");",
			  ALT_NOT(X86_FEATURE_PVUNLOCK));
}

static __always_inline void pv_wait(u8 *ptr, u8 val)
{
	PVOP_VCALL2(lock.wait, ptr, val);
}

static __always_inline void pv_kick(int cpu)
{
	PVOP_VCALL1(lock.kick, cpu);
}

static __always_inline bool pv_vcpu_is_preempted(long cpu)
{
	return PVOP_ALT_CALLEE1(bool, lock.vcpu_is_preempted, cpu,
				"xor %%" _ASM_AX ", %%" _ASM_AX ";",
				ALT_NOT(X86_FEATURE_VCPUPREEMPT));
}

void __raw_callee_save___native_queued_spin_unlock(struct qspinlock *lock);
bool __raw_callee_save___native_vcpu_is_preempted(long cpu);

#endif /* SMP && PARAVIRT_SPINLOCKS */

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

#ifdef CONFIG_PARAVIRT_XXL
static __always_inline unsigned long arch_local_save_flags(void)
{
	return PVOP_ALT_CALLEE0(unsigned long, irq.save_fl, "pushf; pop %%rax;",
				ALT_NOT_XEN);
}

static __always_inline void arch_local_irq_disable(void)
{
	PVOP_ALT_VCALLEE0(irq.irq_disable, "cli;", ALT_NOT_XEN);
}

static __always_inline void arch_local_irq_enable(void)
{
	PVOP_ALT_VCALLEE0(irq.irq_enable, "sti;", ALT_NOT_XEN);
}

static __always_inline unsigned long arch_local_irq_save(void)
{
	unsigned long f;

	f = arch_local_save_flags();
	arch_local_irq_disable();
	return f;
}
#endif


/* Make sure as little as possible of this mess escapes. */
#undef PARAVIRT_CALL
#undef __PVOP_CALL
#undef __PVOP_VCALL
#undef PVOP_VCALL0
#undef PVOP_CALL0
#undef PVOP_VCALL1
#undef PVOP_CALL1
#undef PVOP_VCALL2
#undef PVOP_CALL2
#undef PVOP_VCALL3
#undef PVOP_CALL3
#undef PVOP_VCALL4
#undef PVOP_CALL4

extern void default_banner(void);
void native_pv_lock_init(void) __init;

#else  /* __ASSEMBLER__ */

#ifdef CONFIG_X86_64
#ifdef CONFIG_PARAVIRT_XXL
#ifdef CONFIG_DEBUG_ENTRY

#define PARA_INDIRECT(addr)	*addr(%rip)

.macro PARA_IRQ_save_fl
	ANNOTATE_RETPOLINE_SAFE;
	call PARA_INDIRECT(pv_ops+PV_IRQ_save_fl);
.endm

#define SAVE_FLAGS ALTERNATIVE_2 "PARA_IRQ_save_fl;",			\
				 "ALT_CALL_INSTR;", ALT_CALL_ALWAYS,	\
				 "pushf; pop %rax;", ALT_NOT_XEN
#endif
#endif /* CONFIG_PARAVIRT_XXL */
#endif	/* CONFIG_X86_64 */

#endif /* __ASSEMBLER__ */
#else  /* CONFIG_PARAVIRT */
# define default_banner x86_init_noop

#ifndef __ASSEMBLER__
static inline void native_pv_lock_init(void)
{
}
#endif
#endif /* !CONFIG_PARAVIRT */

#ifndef __ASSEMBLER__
#ifndef CONFIG_PARAVIRT_XXL
static inline void paravirt_enter_mmap(struct mm_struct *mm)
{
}
#endif

#ifndef CONFIG_PARAVIRT
static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
}
#endif

#ifndef CONFIG_PARAVIRT_SPINLOCKS
static inline void paravirt_set_cap(void)
{
}
#endif
#endif /* __ASSEMBLER__ */
#endif /* _ASM_X86_PARAVIRT_H */
