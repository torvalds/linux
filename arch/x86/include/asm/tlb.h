/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TLB_H
#define _ASM_X86_TLB_H

#define tlb_flush tlb_flush
static inline void tlb_flush(struct mmu_gather *tlb);

#include <asm-generic/tlb.h>
#include <linux/kernel.h>
#include <vdso/bits.h>
#include <vdso/page.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	unsigned long start = 0UL, end = TLB_FLUSH_ALL;
	unsigned int stride_shift = tlb_get_unmap_shift(tlb);

	if (!tlb->fullmm && !tlb->need_flush_all) {
		start = tlb->start;
		end = tlb->end;
	}

	flush_tlb_mm_range(tlb->mm, start, end, stride_shift, tlb->freed_tables);
}

static inline void invlpg(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

enum addr_stride {
	PTE_STRIDE = 0,
	PMD_STRIDE = 1
};

/*
 * INVLPGB can be targeted by virtual address, PCID, ASID, or any combination
 * of the three. For example:
 * - FLAG_VA | FLAG_INCLUDE_GLOBAL: invalidate all TLB entries at the address
 * - FLAG_PCID:			    invalidate all TLB entries matching the PCID
 *
 * The first is used to invalidate (kernel) mappings at a particular
 * address across all processes.
 *
 * The latter invalidates all TLB entries matching a PCID.
 */
#define INVLPGB_FLAG_VA			BIT(0)
#define INVLPGB_FLAG_PCID		BIT(1)
#define INVLPGB_FLAG_ASID		BIT(2)
#define INVLPGB_FLAG_INCLUDE_GLOBAL	BIT(3)
#define INVLPGB_FLAG_FINAL_ONLY		BIT(4)
#define INVLPGB_FLAG_INCLUDE_NESTED	BIT(5)

/* The implied mode when all bits are clear: */
#define INVLPGB_MODE_ALL_NONGLOBALS	0UL

#ifdef CONFIG_BROADCAST_TLB_FLUSH
/*
 * INVLPGB does broadcast TLB invalidation across all the CPUs in the system.
 *
 * The INVLPGB instruction is weakly ordered, and a batch of invalidations can
 * be done in a parallel fashion.
 *
 * The instruction takes the number of extra pages to invalidate, beyond the
 * first page, while __invlpgb gets the more human readable number of pages to
 * invalidate.
 *
 * The bits in rax[0:2] determine respectively which components of the address
 * (VA, PCID, ASID) get compared when flushing. If neither bits are set, *any*
 * address in the specified range matches.
 *
 * Since it is desired to only flush TLB entries for the ASID that is executing
 * the instruction (a host/hypervisor or a guest), the ASID valid bit should
 * always be set. On a host/hypervisor, the hardware will use the ASID value
 * specified in EDX[15:0] (which should be 0). On a guest, the hardware will
 * use the actual ASID value of the guest.
 *
 * TLBSYNC is used to ensure that pending INVLPGB invalidations initiated from
 * this CPU have completed.
 */
static inline void __invlpgb(unsigned long asid, unsigned long pcid,
			     unsigned long addr, u16 nr_pages,
			     enum addr_stride stride, u8 flags)
{
	u64 rax = addr | flags | INVLPGB_FLAG_ASID;
	u32 ecx = (stride << 31) | (nr_pages - 1);
	u32 edx = (pcid << 16) | asid;

	/* The low bits in rax are for flags. Verify addr is clean. */
	VM_WARN_ON_ONCE(addr & ~PAGE_MASK);

	/* INVLPGB; supported in binutils >= 2.36. */
	asm volatile(".byte 0x0f, 0x01, 0xfe" :: "a" (rax), "c" (ecx), "d" (edx));
}

static inline void __invlpgb_all(unsigned long asid, unsigned long pcid, u8 flags)
{
	__invlpgb(asid, pcid, 0, 1, 0, flags);
}

static inline void __tlbsync(void)
{
	/*
	 * TLBSYNC waits for INVLPGB instructions originating on the same CPU
	 * to have completed. Print a warning if the task has been migrated,
	 * and might not be waiting on all the INVLPGBs issued during this TLB
	 * invalidation sequence.
	 */
	cant_migrate();

	/* TLBSYNC: supported in binutils >= 0.36. */
	asm volatile(".byte 0x0f, 0x01, 0xff" ::: "memory");
}
#else
/* Some compilers (I'm looking at you clang!) simply can't do DCE */
static inline void __invlpgb(unsigned long asid, unsigned long pcid,
			     unsigned long addr, u16 nr_pages,
			     enum addr_stride s, u8 flags) { }
static inline void __invlpgb_all(unsigned long asid, unsigned long pcid, u8 flags) { }
static inline void __tlbsync(void) { }
#endif

static inline void invlpgb_flush_user_nr_nosync(unsigned long pcid,
						unsigned long addr,
						u16 nr, bool stride)
{
	enum addr_stride str = stride ? PMD_STRIDE : PTE_STRIDE;
	u8 flags = INVLPGB_FLAG_PCID | INVLPGB_FLAG_VA;

	__invlpgb(0, pcid, addr, nr, str, flags);
}

/* Flush all mappings for a given PCID, not including globals. */
static inline void invlpgb_flush_single_pcid_nosync(unsigned long pcid)
{
	__invlpgb_all(0, pcid, INVLPGB_FLAG_PCID);
}

/* Flush all mappings, including globals, for all PCIDs. */
static inline void invlpgb_flush_all(void)
{
	/*
	 * TLBSYNC at the end needs to make sure all flushes done on the
	 * current CPU have been executed system-wide. Therefore, make
	 * sure nothing gets migrated in-between but disable preemption
	 * as it is cheaper.
	 */
	guard(preempt)();
	__invlpgb_all(0, 0, INVLPGB_FLAG_INCLUDE_GLOBAL);
	__tlbsync();
}

/* Flush addr, including globals, for all PCIDs. */
static inline void invlpgb_flush_addr_nosync(unsigned long addr, u16 nr)
{
	__invlpgb(0, 0, addr, nr, PTE_STRIDE, INVLPGB_FLAG_INCLUDE_GLOBAL);
}

/* Flush all mappings for all PCIDs except globals. */
static inline void invlpgb_flush_all_nonglobals(void)
{
	guard(preempt)();
	__invlpgb_all(0, 0, INVLPGB_MODE_ALL_NONGLOBALS);
	__tlbsync();
}
#endif /* _ASM_X86_TLB_H */
