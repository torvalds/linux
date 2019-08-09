/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PARAVIRT_H
#define _ASM_X86_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

#ifdef CONFIG_PARAVIRT
#include <asm/pgtable_types.h>
#include <asm/asm.h>
#include <asm/nospec-branch.h>

#include <asm/paravirt_types.h>

#ifndef __ASSEMBLY__
#include <linux/bug.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <asm/frame.h>

static inline unsigned long long paravirt_sched_clock(void)
{
	return PVOP_CALL0(unsigned long long, time.sched_clock);
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
	return PVOP_CALL1(u64, time.steal_clock, cpu);
}

/* The paravirtualized I/O functions */
static inline void slow_down_io(void)
{
	pv_ops.cpu.io_delay();
#ifdef REALLY_SLOW_IO
	pv_ops.cpu.io_delay();
	pv_ops.cpu.io_delay();
	pv_ops.cpu.io_delay();
#endif
}

static inline void __flush_tlb(void)
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

static inline void flush_tlb_others(const struct cpumask *cpumask,
				    const struct flush_tlb_info *info)
{
	PVOP_VCALL2(mmu.flush_tlb_others, cpumask, info);
}

static inline void paravirt_tlb_remove_table(struct mmu_gather *tlb, void *table)
{
	PVOP_VCALL2(mmu.tlb_remove_table, tlb, table);
}

static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
	PVOP_VCALL1(mmu.exit_mmap, mm);
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
static inline unsigned long paravirt_get_debugreg(int reg)
{
	return PVOP_CALL1(unsigned long, cpu.get_debugreg, reg);
}
#define get_debugreg(var, reg) var = paravirt_get_debugreg(reg)
static inline void set_debugreg(unsigned long val, int reg)
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

static inline unsigned long read_cr2(void)
{
	return PVOP_CALLEE0(unsigned long, mmu.read_cr2);
}

static inline void write_cr2(unsigned long x)
{
	PVOP_VCALL1(mmu.write_cr2, x);
}

static inline unsigned long __read_cr3(void)
{
	return PVOP_CALL0(unsigned long, mmu.read_cr3);
}

static inline void write_cr3(unsigned long x)
{
	PVOP_VCALL1(mmu.write_cr3, x);
}

static inline void __write_cr4(unsigned long x)
{
	PVOP_VCALL1(cpu.write_cr4, x);
}

#ifdef CONFIG_X86_64
static inline unsigned long read_cr8(void)
{
	return PVOP_CALL0(unsigned long, cpu.read_cr8);
}

static inline void write_cr8(unsigned long x)
{
	PVOP_VCALL1(cpu.write_cr8, x);
}
#endif

static inline void arch_safe_halt(void)
{
	PVOP_VCALL0(irq.safe_halt);
}

static inline void halt(void)
{
	PVOP_VCALL0(irq.halt);
}

static inline void wbinvd(void)
{
	PVOP_VCALL0(cpu.wbinvd);
}

#define get_kernel_rpl()  (pv_info.kernel_rpl)

static inline u64 paravirt_read_msr(unsigned msr)
{
	return PVOP_CALL1(u64, cpu.read_msr, msr);
}

static inline void paravirt_write_msr(unsigned msr,
				      unsigned low, unsigned high)
{
	PVOP_VCALL3(cpu.write_msr, msr, low, high);
}

static inline u64 paravirt_read_msr_safe(unsigned msr, int *err)
{
	return PVOP_CALL2(u64, cpu.read_msr_safe, msr, err);
}

static inline int paravirt_write_msr_safe(unsigned msr,
					  unsigned low, unsigned high)
{
	return PVOP_CALL3(int, cpu.write_msr_safe, msr, low, high);
}

#define rdmsr(msr, val1, val2)			\
do {						\
	u64 _l = paravirt_read_msr(msr);	\
	val1 = (u32)_l;				\
	val2 = _l >> 32;			\
} while (0)

#define wrmsr(msr, val1, val2)			\
do {						\
	paravirt_write_msr(msr, val1, val2);	\
} while (0)

#define rdmsrl(msr, val)			\
do {						\
	val = paravirt_read_msr(msr);		\
} while (0)

static inline void wrmsrl(unsigned msr, u64 val)
{
	wrmsr(msr, (u32)val, (u32)(val>>32));
}

#define wrmsr_safe(msr, a, b)	paravirt_write_msr_safe(msr, a, b)

/* rdmsr with exception handling */
#define rdmsr_safe(msr, a, b)				\
({							\
	int _err;					\
	u64 _l = paravirt_read_msr_safe(msr, &_err);	\
	(*a) = (u32)_l;					\
	(*b) = _l >> 32;				\
	_err;						\
})

static inline int rdmsrl_safe(unsigned msr, unsigned long long *p)
{
	int err;

	*p = paravirt_read_msr_safe(msr, &err);
	return err;
}

static inline unsigned long long paravirt_read_pmc(int counter)
{
	return PVOP_CALL1(u64, cpu.read_pmc, counter);
}

#define rdpmc(counter, low, high)		\
do {						\
	u64 _l = paravirt_read_pmc(counter);	\
	low = (u32)_l;				\
	high = _l >> 32;			\
} while (0)

#define rdpmcl(counter, val) ((val) = paravirt_read_pmc(counter))

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

#ifdef CONFIG_X86_64
static inline void load_gs_index(unsigned int gs)
{
	PVOP_VCALL1(cpu.load_gs_index, gs);
}
#endif

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
static inline void set_iopl_mask(unsigned mask)
{
	PVOP_VCALL1(cpu.set_iopl_mask, mask);
}

static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
	PVOP_VCALL2(mmu.activate_mm, prev, next);
}

static inline void paravirt_arch_dup_mmap(struct mm_struct *oldmm,
					  struct mm_struct *mm)
{
	PVOP_VCALL2(mmu.dup_mmap, oldmm, mm);
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
	pteval_t ret;

	if (sizeof(pteval_t) > sizeof(long))
		ret = PVOP_CALLEE2(pteval_t, mmu.make_pte, val, (u64)val >> 32);
	else
		ret = PVOP_CALLEE1(pteval_t, mmu.make_pte, val);

	return (pte_t) { .pte = ret };
}

static inline pteval_t pte_val(pte_t pte)
{
	pteval_t ret;

	if (sizeof(pteval_t) > sizeof(long))
		ret = PVOP_CALLEE2(pteval_t, mmu.pte_val,
				   pte.pte, (u64)pte.pte >> 32);
	else
		ret = PVOP_CALLEE1(pteval_t, mmu.pte_val, pte.pte);

	return ret;
}

static inline pgd_t __pgd(pgdval_t val)
{
	pgdval_t ret;

	if (sizeof(pgdval_t) > sizeof(long))
		ret = PVOP_CALLEE2(pgdval_t, mmu.make_pgd, val, (u64)val >> 32);
	else
		ret = PVOP_CALLEE1(pgdval_t, mmu.make_pgd, val);

	return (pgd_t) { ret };
}

static inline pgdval_t pgd_val(pgd_t pgd)
{
	pgdval_t ret;

	if (sizeof(pgdval_t) > sizeof(long))
		ret =  PVOP_CALLEE2(pgdval_t, mmu.pgd_val,
				    pgd.pgd, (u64)pgd.pgd >> 32);
	else
		ret =  PVOP_CALLEE1(pgdval_t, mmu.pgd_val, pgd.pgd);

	return ret;
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

	if (sizeof(pteval_t) > sizeof(long))
		/* 5 arg words */
		pv_ops.mmu.ptep_modify_prot_commit(vma, addr, ptep, pte);
	else
		PVOP_VCALL4(mmu.ptep_modify_prot_commit,
			    vma, addr, ptep, pte.pte);
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	if (sizeof(pteval_t) > sizeof(long))
		PVOP_VCALL3(mmu.set_pte, ptep, pte.pte, (u64)pte.pte >> 32);
	else
		PVOP_VCALL2(mmu.set_pte, ptep, pte.pte);
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (sizeof(pteval_t) > sizeof(long))
		/* 5 arg words */
		pv_ops.mmu.set_pte_at(mm, addr, ptep, pte);
	else
		PVOP_VCALL4(mmu.set_pte_at, mm, addr, ptep, pte.pte);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	pmdval_t val = native_pmd_val(pmd);

	if (sizeof(pmdval_t) > sizeof(long))
		PVOP_VCALL3(mmu.set_pmd, pmdp, val, (u64)val >> 32);
	else
		PVOP_VCALL2(mmu.set_pmd, pmdp, val);
}

#if CONFIG_PGTABLE_LEVELS >= 3
static inline pmd_t __pmd(pmdval_t val)
{
	pmdval_t ret;

	if (sizeof(pmdval_t) > sizeof(long))
		ret = PVOP_CALLEE2(pmdval_t, mmu.make_pmd, val, (u64)val >> 32);
	else
		ret = PVOP_CALLEE1(pmdval_t, mmu.make_pmd, val);

	return (pmd_t) { ret };
}

static inline pmdval_t pmd_val(pmd_t pmd)
{
	pmdval_t ret;

	if (sizeof(pmdval_t) > sizeof(long))
		ret =  PVOP_CALLEE2(pmdval_t, mmu.pmd_val,
				    pmd.pmd, (u64)pmd.pmd >> 32);
	else
		ret =  PVOP_CALLEE1(pmdval_t, mmu.pmd_val, pmd.pmd);

	return ret;
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	pudval_t val = native_pud_val(pud);

	if (sizeof(pudval_t) > sizeof(long))
		PVOP_VCALL3(mmu.set_pud, pudp, val, (u64)val >> 32);
	else
		PVOP_VCALL2(mmu.set_pud, pudp, val);
}
#if CONFIG_PGTABLE_LEVELS >= 4
static inline pud_t __pud(pudval_t val)
{
	pudval_t ret;

	ret = PVOP_CALLEE1(pudval_t, mmu.make_pud, val);

	return (pud_t) { ret };
}

static inline pudval_t pud_val(pud_t pud)
{
	return PVOP_CALLEE1(pudval_t, mmu.pud_val, pud.pud);
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

static inline void set_p4d(p4d_t *p4dp, p4d_t p4d)
{
	p4dval_t val = native_p4d_val(p4d);

	PVOP_VCALL2(mmu.set_p4d, p4dp, val);
}

#if CONFIG_PGTABLE_LEVELS >= 5

static inline p4d_t __p4d(p4dval_t val)
{
	p4dval_t ret = PVOP_CALLEE1(p4dval_t, mmu.make_p4d, val);

	return (p4d_t) { ret };
}

static inline p4dval_t p4d_val(p4d_t p4d)
{
	return PVOP_CALLEE1(p4dval_t, mmu.p4d_val, p4d.p4d);
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
	if (pgtable_l5_enabled())						\
		set_pgd(pgdp, __pgd(0));				\
} while (0)

#endif  /* CONFIG_PGTABLE_LEVELS == 5 */

static inline void p4d_clear(p4d_t *p4dp)
{
	set_p4d(p4dp, __p4d(0));
}

#endif	/* CONFIG_PGTABLE_LEVELS == 4 */

#endif	/* CONFIG_PGTABLE_LEVELS >= 3 */

#ifdef CONFIG_X86_PAE
/* Special-case pte-setting operations for PAE, which can't update a
   64-bit pte atomically */
static inline void set_pte_atomic(pte_t *ptep, pte_t pte)
{
	PVOP_VCALL3(mmu.set_pte_atomic, ptep, pte.pte, pte.pte >> 32);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	PVOP_VCALL3(mmu.pte_clear, mm, addr, ptep);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	PVOP_VCALL1(mmu.pmd_clear, pmdp);
}
#else  /* !CONFIG_X86_PAE */
static inline void set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_pte(ptep, pte);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	set_pte_at(mm, addr, ptep, __pte(0));
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}
#endif	/* CONFIG_X86_PAE */

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
	PVOP_VCALLEE1(lock.queued_spin_unlock, lock);
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
	return PVOP_CALLEE1(bool, lock.vcpu_is_preempted, cpu);
}

void __raw_callee_save___native_queued_spin_unlock(struct qspinlock *lock);
bool __raw_callee_save___native_vcpu_is_preempted(long cpu);

#endif /* SMP && PARAVIRT_SPINLOCKS */

#ifdef CONFIG_X86_32
#define PV_SAVE_REGS "pushl %ecx; pushl %edx;"
#define PV_RESTORE_REGS "popl %edx; popl %ecx;"

/* save and restore all caller-save registers, except return value */
#define PV_SAVE_ALL_CALLER_REGS		"pushl %ecx;"
#define PV_RESTORE_ALL_CALLER_REGS	"popl  %ecx;"

#define PV_FLAGS_ARG "0"
#define PV_EXTRA_CLOBBERS
#define PV_VEXTRA_CLOBBERS
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

/* We save some registers, but all of them, that's too much. We clobber all
 * caller saved registers but the argument parameter */
#define PV_SAVE_REGS "pushq %%rdi;"
#define PV_RESTORE_REGS "popq %%rdi;"
#define PV_EXTRA_CLOBBERS EXTRA_CLOBBERS, "rcx" , "rdx", "rsi"
#define PV_VEXTRA_CLOBBERS EXTRA_CLOBBERS, "rdi", "rcx" , "rdx", "rsi"
#define PV_FLAGS_ARG "D"
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
#define PV_CALLEE_SAVE_REGS_THUNK(func)					\
	extern typeof(func) __raw_callee_save_##func;			\
									\
	asm(".pushsection .text;"					\
	    ".globl " PV_THUNK_NAME(func) ";"				\
	    ".type " PV_THUNK_NAME(func) ", @function;"			\
	    PV_THUNK_NAME(func) ":"					\
	    FRAME_BEGIN							\
	    PV_SAVE_ALL_CALLER_REGS					\
	    "call " #func ";"						\
	    PV_RESTORE_ALL_CALLER_REGS					\
	    FRAME_END							\
	    "ret;"							\
	    ".size " PV_THUNK_NAME(func) ", .-" PV_THUNK_NAME(func) ";"	\
	    ".popsection")

/* Get a reference to a callee-save function */
#define PV_CALLEE_SAVE(func)						\
	((struct paravirt_callee_save) { __raw_callee_save_##func })

/* Promise that "func" already uses the right calling convention */
#define __PV_IS_CALLEE_SAVE(func)			\
	((struct paravirt_callee_save) { func })

#ifdef CONFIG_PARAVIRT_XXL
static inline notrace unsigned long arch_local_save_flags(void)
{
	return PVOP_CALLEE0(unsigned long, irq.save_fl);
}

static inline notrace void arch_local_irq_restore(unsigned long f)
{
	PVOP_VCALLEE1(irq.restore_fl, f);
}

static inline notrace void arch_local_irq_disable(void)
{
	PVOP_VCALLEE0(irq.irq_disable);
}

static inline notrace void arch_local_irq_enable(void)
{
	PVOP_VCALLEE0(irq.irq_enable);
}

static inline notrace unsigned long arch_local_irq_save(void)
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

#else  /* __ASSEMBLY__ */

#define _PVSITE(ptype, ops, word, algn)		\
771:;						\
	ops;					\
772:;						\
	.pushsection .parainstructions,"a";	\
	 .align	algn;				\
	 word 771b;				\
	 .byte ptype;				\
	 .byte 772b-771b;			\
	.popsection


#define COND_PUSH(set, mask, reg)			\
	.if ((~(set)) & mask); push %reg; .endif
#define COND_POP(set, mask, reg)			\
	.if ((~(set)) & mask); pop %reg; .endif

#ifdef CONFIG_X86_64

#define PV_SAVE_REGS(set)			\
	COND_PUSH(set, CLBR_RAX, rax);		\
	COND_PUSH(set, CLBR_RCX, rcx);		\
	COND_PUSH(set, CLBR_RDX, rdx);		\
	COND_PUSH(set, CLBR_RSI, rsi);		\
	COND_PUSH(set, CLBR_RDI, rdi);		\
	COND_PUSH(set, CLBR_R8, r8);		\
	COND_PUSH(set, CLBR_R9, r9);		\
	COND_PUSH(set, CLBR_R10, r10);		\
	COND_PUSH(set, CLBR_R11, r11)
#define PV_RESTORE_REGS(set)			\
	COND_POP(set, CLBR_R11, r11);		\
	COND_POP(set, CLBR_R10, r10);		\
	COND_POP(set, CLBR_R9, r9);		\
	COND_POP(set, CLBR_R8, r8);		\
	COND_POP(set, CLBR_RDI, rdi);		\
	COND_POP(set, CLBR_RSI, rsi);		\
	COND_POP(set, CLBR_RDX, rdx);		\
	COND_POP(set, CLBR_RCX, rcx);		\
	COND_POP(set, CLBR_RAX, rax)

#define PARA_PATCH(off)		((off) / 8)
#define PARA_SITE(ptype, ops)	_PVSITE(ptype, ops, .quad, 8)
#define PARA_INDIRECT(addr)	*addr(%rip)
#else
#define PV_SAVE_REGS(set)			\
	COND_PUSH(set, CLBR_EAX, eax);		\
	COND_PUSH(set, CLBR_EDI, edi);		\
	COND_PUSH(set, CLBR_ECX, ecx);		\
	COND_PUSH(set, CLBR_EDX, edx)
#define PV_RESTORE_REGS(set)			\
	COND_POP(set, CLBR_EDX, edx);		\
	COND_POP(set, CLBR_ECX, ecx);		\
	COND_POP(set, CLBR_EDI, edi);		\
	COND_POP(set, CLBR_EAX, eax)

#define PARA_PATCH(off)		((off) / 4)
#define PARA_SITE(ptype, ops)	_PVSITE(ptype, ops, .long, 4)
#define PARA_INDIRECT(addr)	*%cs:addr
#endif

#ifdef CONFIG_PARAVIRT_XXL
#define INTERRUPT_RETURN						\
	PARA_SITE(PARA_PATCH(PV_CPU_iret),				\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  jmp PARA_INDIRECT(pv_ops+PV_CPU_iret);)

#define DISABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(PV_IRQ_irq_disable),			\
		  PV_SAVE_REGS(clobbers | CLBR_CALLEE_SAVE);		\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  call PARA_INDIRECT(pv_ops+PV_IRQ_irq_disable);	\
		  PV_RESTORE_REGS(clobbers | CLBR_CALLEE_SAVE);)

#define ENABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(PV_IRQ_irq_enable),			\
		  PV_SAVE_REGS(clobbers | CLBR_CALLEE_SAVE);		\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  call PARA_INDIRECT(pv_ops+PV_IRQ_irq_enable);		\
		  PV_RESTORE_REGS(clobbers | CLBR_CALLEE_SAVE);)
#endif

#ifdef CONFIG_X86_64
#ifdef CONFIG_PARAVIRT_XXL
/*
 * If swapgs is used while the userspace stack is still current,
 * there's no way to call a pvop.  The PV replacement *must* be
 * inlined, or the swapgs instruction must be trapped and emulated.
 */
#define SWAPGS_UNSAFE_STACK						\
	PARA_SITE(PARA_PATCH(PV_CPU_swapgs), swapgs)

/*
 * Note: swapgs is very special, and in practise is either going to be
 * implemented with a single "swapgs" instruction or something very
 * special.  Either way, we don't need to save any registers for
 * it.
 */
#define SWAPGS								\
	PARA_SITE(PARA_PATCH(PV_CPU_swapgs),				\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  call PARA_INDIRECT(pv_ops+PV_CPU_swapgs);		\
		 )

#define USERGS_SYSRET64							\
	PARA_SITE(PARA_PATCH(PV_CPU_usergs_sysret64),			\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  jmp PARA_INDIRECT(pv_ops+PV_CPU_usergs_sysret64);)

#ifdef CONFIG_DEBUG_ENTRY
#define SAVE_FLAGS(clobbers)                                        \
	PARA_SITE(PARA_PATCH(PV_IRQ_save_fl),			    \
		  PV_SAVE_REGS(clobbers | CLBR_CALLEE_SAVE);        \
		  ANNOTATE_RETPOLINE_SAFE;			    \
		  call PARA_INDIRECT(pv_ops+PV_IRQ_save_fl);	    \
		  PV_RESTORE_REGS(clobbers | CLBR_CALLEE_SAVE);)
#endif
#endif /* CONFIG_PARAVIRT_XXL */
#endif	/* CONFIG_X86_64 */

#ifdef CONFIG_PARAVIRT_XXL

#define GET_CR2_INTO_AX							\
	PARA_SITE(PARA_PATCH(PV_MMU_read_cr2),				\
		  ANNOTATE_RETPOLINE_SAFE;				\
		  call PARA_INDIRECT(pv_ops+PV_MMU_read_cr2);		\
		 )

#endif /* CONFIG_PARAVIRT_XXL */


#endif /* __ASSEMBLY__ */
#else  /* CONFIG_PARAVIRT */
# define default_banner x86_init_noop
#endif /* !CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
#ifndef CONFIG_PARAVIRT_XXL
static inline void paravirt_arch_dup_mmap(struct mm_struct *oldmm,
					  struct mm_struct *mm)
{
}
#endif

#ifndef CONFIG_PARAVIRT
static inline void paravirt_arch_exit_mmap(struct mm_struct *mm)
{
}
#endif
#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_PARAVIRT_H */
