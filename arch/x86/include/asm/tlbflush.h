#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>

static inline void __invpcid(unsigned long pcid, unsigned long addr,
			     unsigned long type)
{
	struct { u64 d[2]; } desc = { { pcid, addr } };

	/*
	 * The memory clobber is because the whole point is to invalidate
	 * stale TLB entries and, especially if we're flushing global
	 * mappings, we don't want the compiler to reorder any subsequent
	 * memory accesses before the TLB flush.
	 *
	 * The hex opcode is invpcid (%ecx), %eax in 32-bit mode and
	 * invpcid (%rcx), %rax in long mode.
	 */
	asm volatile (".byte 0x66, 0x0f, 0x38, 0x82, 0x01"
		      : : "m" (desc), "a" (type), "c" (&desc) : "memory");
}

#define INVPCID_TYPE_INDIV_ADDR		0
#define INVPCID_TYPE_SINGLE_CTXT	1
#define INVPCID_TYPE_ALL_INCL_GLOBAL	2
#define INVPCID_TYPE_ALL_NON_GLOBAL	3

/* Flush all mappings for a given pcid and addr, not including globals. */
static inline void invpcid_flush_one(unsigned long pcid,
				     unsigned long addr)
{
	__invpcid(pcid, addr, INVPCID_TYPE_INDIV_ADDR);
}

/* Flush all mappings for a given PCID, not including globals. */
static inline void invpcid_flush_single_context(unsigned long pcid)
{
	__invpcid(pcid, 0, INVPCID_TYPE_SINGLE_CTXT);
}

/* Flush all mappings, including globals, for all PCIDs. */
static inline void invpcid_flush_all(void)
{
	__invpcid(0, 0, INVPCID_TYPE_ALL_INCL_GLOBAL);
}

/* Flush all mappings for all PCIDs except globals. */
static inline void invpcid_flush_all_nonglobals(void)
{
	__invpcid(0, 0, INVPCID_TYPE_ALL_NON_GLOBAL);
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define __flush_tlb() __native_flush_tlb()
#define __flush_tlb_global() __native_flush_tlb_global()
#define __flush_tlb_single(addr) __native_flush_tlb_single(addr)
#endif

struct tlb_state {
#ifdef CONFIG_SMP
	struct mm_struct *active_mm;
	int state;
#endif

	/*
	 * Access to this CR4 shadow and to H/W CR4 is protected by
	 * disabling interrupts when modifying either one.
	 */
	unsigned long cr4;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state, cpu_tlbstate);

/* Initialize cr4 shadow for this CPU. */
static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4_safe());
}

/* Set in this cpu's CR4. */
static inline void cr4_set_bits(unsigned long mask)
{
	unsigned long cr4;

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	if ((cr4 | mask) != cr4) {
		cr4 |= mask;
		this_cpu_write(cpu_tlbstate.cr4, cr4);
		__write_cr4(cr4);
	}
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits(unsigned long mask)
{
	unsigned long cr4;

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	if ((cr4 & ~mask) != cr4) {
		cr4 &= ~mask;
		this_cpu_write(cpu_tlbstate.cr4, cr4);
		__write_cr4(cr4);
	}
}

/* Read the CR4 shadow. */
static inline unsigned long cr4_read_shadow(void)
{
	return this_cpu_read(cpu_tlbstate.cr4);
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

static inline void __native_flush_tlb(void)
{
	/*
	 * If current->mm == NULL then we borrow a mm which may change during a
	 * task switch and therefore we must not be preempted while we write CR3
	 * back:
	 */
	preempt_disable();
	native_write_cr3(native_read_cr3());
	preempt_enable();
}

static inline void __native_flush_tlb_global_irq_disabled(void)
{
	unsigned long cr4;

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	/* clear PGE */
	native_write_cr4(cr4 & ~X86_CR4_PGE);
	/* write old PGE again and flush TLBs */
	native_write_cr4(cr4);
}

static inline void __native_flush_tlb_global(void)
{
	unsigned long flags;

	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		/*
		 * Using INVPCID is considerably faster than a pair of writes
		 * to CR4 sandwiched inside an IRQ flag save/restore.
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

	__native_flush_tlb_global_irq_disabled();

	raw_local_irq_restore(flags);
}

static inline void __native_flush_tlb_single(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

static inline void __flush_tlb_all(void)
{
	if (static_cpu_has(X86_FEATURE_PGE))
		__flush_tlb_global();
	else
		__flush_tlb();
}

static inline void __flush_tlb_one(unsigned long addr)
{
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ONE);
	__flush_tlb_single(addr);
}

#define TLB_FLUSH_ALL	-1UL

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_others(cpumask, mm, start, end) flushes TLBs on other cpus
 *
 * ..but the i386 has somewhat limited tlb flushing capabilities,
 * and page-granular flushes are available only on i486 and up.
 */

#ifndef CONFIG_SMP

/* "_up" is for UniProcessor.
 *
 * This is a helper for other header functions.  *Not* intended to be called
 * directly.  All global TLB flushes need to either call this, or to bump the
 * vm statistics themselves.
 */
static inline void __flush_tlb_up(void)
{
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
	__flush_tlb();
}

static inline void flush_tlb_all(void)
{
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
	__flush_tlb_all();
}

static inline void flush_tlb(void)
{
	__flush_tlb_up();
}

static inline void local_flush_tlb(void)
{
	__flush_tlb_up();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb_up();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_up();
}

static inline void flush_tlb_mm_range(struct mm_struct *mm,
	   unsigned long start, unsigned long end, unsigned long vmflag)
{
	if (mm == current->active_mm)
		__flush_tlb_up();
}

static inline void native_flush_tlb_others(const struct cpumask *cpumask,
					   struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
}

static inline void reset_lazy_tlbstate(void)
{
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

#else  /* SMP */

#include <asm/smp.h>

#define local_flush_tlb() __flush_tlb()

#define flush_tlb_mm(mm)	flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL)

#define flush_tlb_range(vma, start, end)	\
		flush_tlb_mm_range(vma->vm_mm, start, end, vma->vm_flags)

extern void flush_tlb_all(void);
extern void flush_tlb_current_task(void);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);
extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define flush_tlb()	flush_tlb_current_task()

void native_flush_tlb_others(const struct cpumask *cpumask,
				struct mm_struct *mm,
				unsigned long start, unsigned long end);

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

static inline void reset_lazy_tlbstate(void)
{
	this_cpu_write(cpu_tlbstate.state, 0);
	this_cpu_write(cpu_tlbstate.active_mm, &init_mm);
}

#endif	/* SMP */

#ifndef CONFIG_PARAVIRT
#define flush_tlb_others(mask, mm, start, end)	\
	native_flush_tlb_others(mask, mm, start, end)
#endif

#endif /* _ASM_X86_TLBFLUSH_H */
