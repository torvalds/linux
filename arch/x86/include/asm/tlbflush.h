/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm_types.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/smp.h>
#include <asm/invpcid.h>
#include <asm/pti.h>
#include <asm/processor-flags.h>
#include <asm/pgtable.h>

DECLARE_PER_CPU(u64, tlbstate_untag_mask);

void __flush_tlb_all(void);

#define TLB_FLUSH_ALL	-1UL
#define TLB_GENERATION_INVALID	0

void cr4_update_irqsoff(unsigned long set, unsigned long clear);
unsigned long cr4_read_shadow(void);

/* Set in this cpu's CR4. */
static inline void cr4_set_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(mask, 0);
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(0, mask);
}

/* Set in this cpu's CR4. */
static inline void cr4_set_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_set_bits_irqsoff(mask);
	local_irq_restore(flags);
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_clear_bits_irqsoff(mask);
	local_irq_restore(flags);
}

#ifndef MODULE
/*
 * 6 because 6 should be plenty and struct tlb_state will fit in two cache
 * lines.
 */
#define TLB_NR_DYN_ASIDS	6

struct tlb_context {
	u64 ctx_id;
	u64 tlb_gen;
};

struct tlb_state {
	/*
	 * cpu_tlbstate.loaded_mm should match CR3 whenever interrupts
	 * are on.  This means that it may not match current->active_mm,
	 * which will contain the previous user mm when we're in lazy TLB
	 * mode even if we've already switched back to swapper_pg_dir.
	 *
	 * During switch_mm_irqs_off(), loaded_mm will be set to
	 * LOADED_MM_SWITCHING during the brief interrupts-off window
	 * when CR3 and loaded_mm would otherwise be inconsistent.  This
	 * is for nmi_uaccess_okay()'s benefit.
	 */
	struct mm_struct *loaded_mm;

#define LOADED_MM_SWITCHING ((struct mm_struct *)1UL)

	/* Last user mm for optimizing IBPB */
	union {
		struct mm_struct	*last_user_mm;
		unsigned long		last_user_mm_spec;
	};

	u16 loaded_mm_asid;
	u16 next_asid;

	/*
	 * If set we changed the page tables in such a way that we
	 * needed an invalidation of all contexts (aka. PCIDs / ASIDs).
	 * This tells us to go invalidate all the non-loaded ctxs[]
	 * on the next context switch.
	 *
	 * The current ctx was kept up-to-date as it ran and does not
	 * need to be invalidated.
	 */
	bool invalidate_other;

#ifdef CONFIG_ADDRESS_MASKING
	/*
	 * Active LAM mode.
	 *
	 * X86_CR3_LAM_U57/U48 shifted right by X86_CR3_LAM_U57_BIT or 0 if LAM
	 * disabled.
	 */
	u8 lam;
#endif

	/*
	 * Mask that contains TLB_NR_DYN_ASIDS+1 bits to indicate
	 * the corresponding user PCID needs a flush next time we
	 * switch to it; see SWITCH_TO_USER_CR3.
	 */
	unsigned short user_pcid_flush_mask;

	/*
	 * Access to this CR4 shadow and to H/W CR4 is protected by
	 * disabling interrupts when modifying either one.
	 */
	unsigned long cr4;

	/*
	 * This is a list of all contexts that might exist in the TLB.
	 * There is one per ASID that we use, and the ASID (what the
	 * CPU calls PCID) is the index into ctxts.
	 *
	 * For each context, ctx_id indicates which mm the TLB's user
	 * entries came from.  As an invariant, the TLB will never
	 * contain entries that are out-of-date as when that mm reached
	 * the tlb_gen in the list.
	 *
	 * To be clear, this means that it's legal for the TLB code to
	 * flush the TLB without updating tlb_gen.  This can happen
	 * (for now, at least) due to paravirt remote flushes.
	 *
	 * NB: context 0 is a bit special, since it's also used by
	 * various bits of init code.  This is fine -- code that
	 * isn't aware of PCID will end up harmlessly flushing
	 * context 0.
	 */
	struct tlb_context ctxs[TLB_NR_DYN_ASIDS];
};
DECLARE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate);

struct tlb_state_shared {
	/*
	 * We can be in one of several states:
	 *
	 *  - Actively using an mm.  Our CPU's bit will be set in
	 *    mm_cpumask(loaded_mm) and is_lazy == false;
	 *
	 *  - Not using a real mm.  loaded_mm == &init_mm.  Our CPU's bit
	 *    will not be set in mm_cpumask(&init_mm) and is_lazy == false.
	 *
	 *  - Lazily using a real mm.  loaded_mm != &init_mm, our bit
	 *    is set in mm_cpumask(loaded_mm), but is_lazy == true.
	 *    We're heuristically guessing that the CR3 load we
	 *    skipped more than makes up for the overhead added by
	 *    lazy mode.
	 */
	bool is_lazy;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state_shared, cpu_tlbstate_shared);

bool nmi_uaccess_okay(void);
#define nmi_uaccess_okay nmi_uaccess_okay

/* Initialize cr4 shadow for this CPU. */
static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

extern unsigned long mmu_cr4_features;
extern u32 *trampoline_cr4_features;

extern void initialize_tlbstate_and_flush(void);

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_multi(cpumask, info) flushes TLBs on multiple cpus
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */
struct flush_tlb_info {
	/*
	 * We support several kinds of flushes.
	 *
	 * - Fully flush a single mm.  .mm will be set, .end will be
	 *   TLB_FLUSH_ALL, and .new_tlb_gen will be the tlb_gen to
	 *   which the IPI sender is trying to catch us up.
	 *
	 * - Partially flush a single mm.  .mm will be set, .start and
	 *   .end will indicate the range, and .new_tlb_gen will be set
	 *   such that the changes between generation .new_tlb_gen-1 and
	 *   .new_tlb_gen are entirely contained in the indicated range.
	 *
	 * - Fully flush all mms whose tlb_gens have been updated.  .mm
	 *   will be NULL, .end will be TLB_FLUSH_ALL, and .new_tlb_gen
	 *   will be zero.
	 */
	struct mm_struct	*mm;
	unsigned long		start;
	unsigned long		end;
	u64			new_tlb_gen;
	unsigned int		initiating_cpu;
	u8			stride_shift;
	u8			freed_tables;
};

void flush_tlb_local(void);
void flush_tlb_one_user(unsigned long addr);
void flush_tlb_one_kernel(unsigned long addr);
void flush_tlb_multi(const struct cpumask *cpumask,
		      const struct flush_tlb_info *info);

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#endif

#define flush_tlb_mm(mm)						\
		flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL, true)

#define flush_tlb_range(vma, start, end)				\
	flush_tlb_mm_range((vma)->vm_mm, start, end,			\
			   ((vma)->vm_flags & VM_HUGETLB)		\
				? huge_page_shift(hstate_vma(vma))	\
				: PAGE_SHIFT, false)

extern void flush_tlb_all(void);
extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

static inline void flush_tlb_page(struct vm_area_struct *vma, unsigned long a)
{
	flush_tlb_mm_range(vma->vm_mm, a, a + PAGE_SIZE, PAGE_SHIFT, false);
}

static inline u64 inc_mm_tlb_gen(struct mm_struct *mm)
{
	/*
	 * Bump the generation count.  This also serves as a full barrier
	 * that synchronizes with switch_mm(): callers are required to order
	 * their read of mm_cpumask after their writes to the paging
	 * structures.
	 */
	return atomic64_inc_return(&mm->context.tlb_gen);
}

static inline void arch_tlbbatch_add_mm(struct arch_tlbflush_unmap_batch *batch,
					struct mm_struct *mm)
{
	inc_mm_tlb_gen(mm);
	cpumask_or(&batch->cpumask, &batch->cpumask, mm_cpumask(mm));
}

extern void arch_tlbbatch_flush(struct arch_tlbflush_unmap_batch *batch);

static inline bool pte_flags_need_flush(unsigned long oldflags,
					unsigned long newflags,
					bool ignore_access)
{
	/*
	 * Flags that require a flush when cleared but not when they are set.
	 * Only include flags that would not trigger spurious page-faults.
	 * Non-present entries are not cached. Hardware would set the
	 * dirty/access bit if needed without a fault.
	 */
	const pteval_t flush_on_clear = _PAGE_DIRTY | _PAGE_PRESENT |
					_PAGE_ACCESSED;
	const pteval_t software_flags = _PAGE_SOFTW1 | _PAGE_SOFTW2 |
					_PAGE_SOFTW3 | _PAGE_SOFTW4 |
					_PAGE_SAVED_DIRTY;
	const pteval_t flush_on_change = _PAGE_RW | _PAGE_USER | _PAGE_PWT |
			  _PAGE_PCD | _PAGE_PSE | _PAGE_GLOBAL | _PAGE_PAT |
			  _PAGE_PAT_LARGE | _PAGE_PKEY_BIT0 | _PAGE_PKEY_BIT1 |
			  _PAGE_PKEY_BIT2 | _PAGE_PKEY_BIT3 | _PAGE_NX;
	unsigned long diff = oldflags ^ newflags;

	BUILD_BUG_ON(flush_on_clear & software_flags);
	BUILD_BUG_ON(flush_on_clear & flush_on_change);
	BUILD_BUG_ON(flush_on_change & software_flags);

	/* Ignore software flags */
	diff &= ~software_flags;

	if (ignore_access)
		diff &= ~_PAGE_ACCESSED;

	/*
	 * Did any of the 'flush_on_clear' flags was clleared set from between
	 * 'oldflags' and 'newflags'?
	 */
	if (diff & oldflags & flush_on_clear)
		return true;

	/* Flush on modified flags. */
	if (diff & flush_on_change)
		return true;

	/* Ensure there are no flags that were left behind */
	if (IS_ENABLED(CONFIG_DEBUG_VM) &&
	    (diff & ~(flush_on_clear | software_flags | flush_on_change))) {
		VM_WARN_ON_ONCE(1);
		return true;
	}

	return false;
}

/*
 * pte_needs_flush() checks whether permissions were demoted and require a
 * flush. It should only be used for userspace PTEs.
 */
static inline bool pte_needs_flush(pte_t oldpte, pte_t newpte)
{
	/* !PRESENT -> * ; no need for flush */
	if (!(pte_flags(oldpte) & _PAGE_PRESENT))
		return false;

	/* PFN changed ; needs flush */
	if (pte_pfn(oldpte) != pte_pfn(newpte))
		return true;

	/*
	 * check PTE flags; ignore access-bit; see comment in
	 * ptep_clear_flush_young().
	 */
	return pte_flags_need_flush(pte_flags(oldpte), pte_flags(newpte),
				    true);
}
#define pte_needs_flush pte_needs_flush

/*
 * huge_pmd_needs_flush() checks whether permissions were demoted and require a
 * flush. It should only be used for userspace huge PMDs.
 */
static inline bool huge_pmd_needs_flush(pmd_t oldpmd, pmd_t newpmd)
{
	/* !PRESENT -> * ; no need for flush */
	if (!(pmd_flags(oldpmd) & _PAGE_PRESENT))
		return false;

	/* PFN changed ; needs flush */
	if (pmd_pfn(oldpmd) != pmd_pfn(newpmd))
		return true;

	/*
	 * check PMD flags; do not ignore access-bit; see
	 * pmdp_clear_flush_young().
	 */
	return pte_flags_need_flush(pmd_flags(oldpmd), pmd_flags(newpmd),
				    false);
}
#define huge_pmd_needs_flush huge_pmd_needs_flush

#ifdef CONFIG_ADDRESS_MASKING
static inline  u64 tlbstate_lam_cr3_mask(void)
{
	u64 lam = this_cpu_read(cpu_tlbstate.lam);

	return lam << X86_CR3_LAM_U57_BIT;
}

static inline void set_tlbstate_lam_mode(struct mm_struct *mm)
{
	this_cpu_write(cpu_tlbstate.lam,
		       mm->context.lam_cr3_mask >> X86_CR3_LAM_U57_BIT);
	this_cpu_write(tlbstate_untag_mask, mm->context.untag_mask);
}

#else

static inline u64 tlbstate_lam_cr3_mask(void)
{
	return 0;
}

static inline void set_tlbstate_lam_mode(struct mm_struct *mm)
{
}
#endif
#endif /* !MODULE */

static inline void __native_tlb_flush_global(unsigned long cr4)
{
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	native_write_cr4(cr4);
}
#endif /* _ASM_X86_TLBFLUSH_H */
