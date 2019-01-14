/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/smp.h>
#include <asm/invpcid.h>
#include <asm/pti.h>
#include <asm/processor-flags.h>

/*
 * The x86 feature is called PCID (Process Context IDentifier). It is similar
 * to what is traditionally called ASID on the RISC processors.
 *
 * We don't use the traditional ASID implementation, where each process/mm gets
 * its own ASID and flush/restart when we run out of ASID space.
 *
 * Instead we have a small per-cpu array of ASIDs and cache the last few mm's
 * that came by on this CPU, allowing cheaper switch_mm between processes on
 * this CPU.
 *
 * We end up with different spaces for different things. To avoid confusion we
 * use different names for each of them:
 *
 * ASID  - [0, TLB_NR_DYN_ASIDS-1]
 *         the canonical identifier for an mm
 *
 * kPCID - [1, TLB_NR_DYN_ASIDS]
 *         the value we write into the PCID part of CR3; corresponds to the
 *         ASID+1, because PCID 0 is special.
 *
 * uPCID - [2048 + 1, 2048 + TLB_NR_DYN_ASIDS]
 *         for KPTI each mm has two address spaces and thus needs two
 *         PCID values, but we can still do with a single ASID denomination
 *         for each mm. Corresponds to kPCID + 2048.
 *
 */

/* There are 12 bits of space for ASIDS in CR3 */
#define CR3_HW_ASID_BITS		12

/*
 * When enabled, PAGE_TABLE_ISOLATION consumes a single bit for
 * user/kernel switches
 */
#ifdef CONFIG_PAGE_TABLE_ISOLATION
# define PTI_CONSUMED_PCID_BITS	1
#else
# define PTI_CONSUMED_PCID_BITS	0
#endif

#define CR3_AVAIL_PCID_BITS (X86_CR3_PCID_BITS - PTI_CONSUMED_PCID_BITS)

/*
 * ASIDs are zero-based: 0->MAX_AVAIL_ASID are valid.  -1 below to account
 * for them being zero-based.  Another -1 is because PCID 0 is reserved for
 * use by non-PCID-aware users.
 */
#define MAX_ASID_AVAILABLE ((1 << CR3_AVAIL_PCID_BITS) - 2)

/*
 * 6 because 6 should be plenty and struct tlb_state will fit in two cache
 * lines.
 */
#define TLB_NR_DYN_ASIDS	6

/*
 * Given @asid, compute kPCID
 */
static inline u16 kern_pcid(u16 asid)
{
	VM_WARN_ON_ONCE(asid > MAX_ASID_AVAILABLE);

#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/*
	 * Make sure that the dynamic ASID space does not confict with the
	 * bit we are using to switch between user and kernel ASIDs.
	 */
	BUILD_BUG_ON(TLB_NR_DYN_ASIDS >= (1 << X86_CR3_PTI_PCID_USER_BIT));

	/*
	 * The ASID being passed in here should have respected the
	 * MAX_ASID_AVAILABLE and thus never have the switch bit set.
	 */
	VM_WARN_ON_ONCE(asid & (1 << X86_CR3_PTI_PCID_USER_BIT));
#endif
	/*
	 * The dynamically-assigned ASIDs that get passed in are small
	 * (<TLB_NR_DYN_ASIDS).  They never have the high switch bit set,
	 * so do not bother to clear it.
	 *
	 * If PCID is on, ASID-aware code paths put the ASID+1 into the
	 * PCID bits.  This serves two purposes.  It prevents a nasty
	 * situation in which PCID-unaware code saves CR3, loads some other
	 * value (with PCID == 0), and then restores CR3, thus corrupting
	 * the TLB for ASID 0 if the saved ASID was nonzero.  It also means
	 * that any bugs involving loading a PCID-enabled CR3 with
	 * CR4.PCIDE off will trigger deterministically.
	 */
	return asid + 1;
}

/*
 * Given @asid, compute uPCID
 */
static inline u16 user_pcid(u16 asid)
{
	u16 ret = kern_pcid(asid);
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	ret |= 1 << X86_CR3_PTI_PCID_USER_BIT;
#endif
	return ret;
}

struct pgd_t;
static inline unsigned long build_cr3(pgd_t *pgd, u16 asid)
{
	if (static_cpu_has(X86_FEATURE_PCID)) {
		return __sme_pa(pgd) | kern_pcid(asid);
	} else {
		VM_WARN_ON_ONCE(asid != 0);
		return __sme_pa(pgd);
	}
}

static inline unsigned long build_cr3_noflush(pgd_t *pgd, u16 asid)
{
	VM_WARN_ON_ONCE(asid > MAX_ASID_AVAILABLE);
	/*
	 * Use boot_cpu_has() instead of this_cpu_has() as this function
	 * might be called during early boot. This should work even after
	 * boot because all CPU's the have same capabilities:
	 */
	VM_WARN_ON_ONCE(!boot_cpu_has(X86_FEATURE_PCID));
	return __sme_pa(pgd) | kern_pcid(asid) | CR3_NOFLUSH;
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define __flush_tlb() __native_flush_tlb()
#define __flush_tlb_global() __native_flush_tlb_global()
#define __flush_tlb_one_user(addr) __native_flush_tlb_one_user(addr)
#endif

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

#define LOADED_MM_SWITCHING ((struct mm_struct *)1)

	/* Last user mm for optimizing IBPB */
	union {
		struct mm_struct	*last_user_mm;
		unsigned long		last_user_mm_ibpb;
	};

	u16 loaded_mm_asid;
	u16 next_asid;

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
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state, cpu_tlbstate);

/*
 * Blindly accessing user memory from NMI context can be dangerous
 * if we're in the middle of switching the current user task or
 * switching the loaded mm.  It can also be dangerous if we
 * interrupted some kernel code that was temporarily using a
 * different mm.
 */
static inline bool nmi_uaccess_okay(void)
{
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	struct mm_struct *current_mm = current->mm;

	VM_WARN_ON_ONCE(!loaded_mm);

	/*
	 * The condition we want to check is
	 * current_mm->pgd == __va(read_cr3_pa()).  This may be slow, though,
	 * if we're running in a VM with shadow paging, and nmi_uaccess_okay()
	 * is supposed to be reasonably fast.
	 *
	 * Instead, we check the almost equivalent but somewhat conservative
	 * condition below, and we rely on the fact that switch_mm_irqs_off()
	 * sets loaded_mm to LOADED_MM_SWITCHING before writing to CR3.
	 */
	if (loaded_mm != current_mm)
		return false;

	VM_WARN_ON_ONCE(current_mm->pgd != __va(read_cr3_pa()));

	return true;
}

/* Initialize cr4 shadow for this CPU. */
static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

static inline void __cr4_set(unsigned long cr4)
{
	lockdep_assert_irqs_disabled();
	this_cpu_write(cpu_tlbstate.cr4, cr4);
	__write_cr4(cr4);
}

/* Set in this cpu's CR4. */
static inline void cr4_set_bits(unsigned long mask)
{
	unsigned long cr4, flags;

	local_irq_save(flags);
	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	if ((cr4 | mask) != cr4)
		__cr4_set(cr4 | mask);
	local_irq_restore(flags);
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits(unsigned long mask)
{
	unsigned long cr4, flags;

	local_irq_save(flags);
	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	if ((cr4 & ~mask) != cr4)
		__cr4_set(cr4 & ~mask);
	local_irq_restore(flags);
}

static inline void cr4_toggle_bits_irqsoff(unsigned long mask)
{
	unsigned long cr4;

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	__cr4_set(cr4 ^ mask);
}

/* Read the CR4 shadow. */
static inline unsigned long cr4_read_shadow(void)
{
	return this_cpu_read(cpu_tlbstate.cr4);
}

/*
 * Mark all other ASIDs as invalid, preserves the current.
 */
static inline void invalidate_other_asid(void)
{
	this_cpu_write(cpu_tlbstate.invalidate_other, true);
}

/*
 * Save some of cr4 feature set we're using (e.g.  Pentium 4MB
 * enable and PPro Global page enable), so that any CPU's that boot
 * up after us can get the correct flags.  This should only be used
 * during boot on the boot cpu.
 */
extern unsigned long mmu_cr4_features;
extern u32 *trampoline_cr4_features;

static inline void cr4_set_bits_and_update_boot(unsigned long mask)
{
	mmu_cr4_features |= mask;
	if (trampoline_cr4_features)
		*trampoline_cr4_features = mmu_cr4_features;
	cr4_set_bits(mask);
}

extern void initialize_tlbstate_and_flush(void);

/*
 * Given an ASID, flush the corresponding user ASID.  We can delay this
 * until the next time we switch to it.
 *
 * See SWITCH_TO_USER_CR3.
 */
static inline void invalidate_user_asid(u16 asid)
{
	/* There is no user ASID if address space separation is off */
	if (!IS_ENABLED(CONFIG_PAGE_TABLE_ISOLATION))
		return;

	/*
	 * We only have a single ASID if PCID is off and the CR3
	 * write will have flushed it.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_PCID))
		return;

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	__set_bit(kern_pcid(asid),
		  (unsigned long *)this_cpu_ptr(&cpu_tlbstate.user_pcid_flush_mask));
}

/*
 * flush the entire current user mapping
 */
static inline void __native_flush_tlb(void)
{
	/*
	 * Preemption or interrupts must be disabled to protect the access
	 * to the per CPU variable and to prevent being preempted between
	 * read_cr3() and write_cr3().
	 */
	WARN_ON_ONCE(preemptible());

	invalidate_user_asid(this_cpu_read(cpu_tlbstate.loaded_mm_asid));

	/* If current->mm == NULL then the read_cr3() "borrows" an mm */
	native_write_cr3(__native_read_cr3());
}

/*
 * flush everything
 */
static inline void __native_flush_tlb_global(void)
{
	unsigned long cr4, flags;

	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		/*
		 * Using INVPCID is considerably faster than a pair of writes
		 * to CR4 sandwiched inside an IRQ flag save/restore.
		 *
		 * Note, this works with CR4.PCIDE=0 or 1.
		 */
		invpcid_flush_all();
		return;
	}

	/*
	 * Read-modify-write to CR4 - protect it from preemption and
	 * from interrupts. (Use the raw variant because this code can
	 * be called from deep inside debugging code.)
	 */
	raw_local_irq_save(flags);

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	/* toggle PGE */
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	/* write old PGE again and flush TLBs */
	native_write_cr4(cr4);

	raw_local_irq_restore(flags);
}

/*
 * flush one page in the user mapping
 */
static inline void __native_flush_tlb_one_user(unsigned long addr)
{
	u32 loaded_mm_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);

	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	/*
	 * Some platforms #GP if we call invpcid(type=1/2) before CR4.PCIDE=1.
	 * Just use invalidate_user_asid() in case we are called early.
	 */
	if (!this_cpu_has(X86_FEATURE_INVPCID_SINGLE))
		invalidate_user_asid(loaded_mm_asid);
	else
		invpcid_flush_one(user_pcid(loaded_mm_asid), addr);
}

/*
 * flush everything
 */
static inline void __flush_tlb_all(void)
{
	/*
	 * This is to catch users with enabled preemption and the PGE feature
	 * and don't trigger the warning in __native_flush_tlb().
	 */
	VM_WARN_ON_ONCE(preemptible());

	if (boot_cpu_has(X86_FEATURE_PGE)) {
		__flush_tlb_global();
	} else {
		/*
		 * !PGE -> !PCID (setup_pcid()), thus every flush is total.
		 */
		__flush_tlb();
	}
}

/*
 * flush one page in the kernel mapping
 */
static inline void __flush_tlb_one_kernel(unsigned long addr)
{
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ONE);

	/*
	 * If PTI is off, then __flush_tlb_one_user() is just INVLPG or its
	 * paravirt equivalent.  Even with PCID, this is sufficient: we only
	 * use PCID if we also use global PTEs for the kernel mapping, and
	 * INVLPG flushes global translations across all address spaces.
	 *
	 * If PTI is on, then the kernel is mapped with non-global PTEs, and
	 * __flush_tlb_one_user() will flush the given address for the current
	 * kernel address space and for its usermode counterpart, but it does
	 * not flush it for other address spaces.
	 */
	__flush_tlb_one_user(addr);

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	/*
	 * See above.  We need to propagate the flush to all other address
	 * spaces.  In principle, we only need to propagate it to kernelmode
	 * address spaces, but the extra bookkeeping we would need is not
	 * worth it.
	 */
	invalidate_other_asid();
}

#define TLB_FLUSH_ALL	-1UL

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_others(cpumask, info) flushes TLBs on other cpus
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
	unsigned int		stride_shift;
	bool			freed_tables;
};

#define local_flush_tlb() __flush_tlb()

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

void native_flush_tlb_others(const struct cpumask *cpumask,
			     const struct flush_tlb_info *info);

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

#ifndef CONFIG_PARAVIRT
#define flush_tlb_others(mask, info)	\
	native_flush_tlb_others(mask, info)

#define paravirt_tlb_remove_table(tlb, page) \
	tlb_remove_page(tlb, (void *)(page))
#endif

#endif /* _ASM_X86_TLBFLUSH_H */
