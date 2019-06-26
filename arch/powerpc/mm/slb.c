/*
 * PowerPC64 SLB support.
 *
 * Copyright (C) 2004 David Gibson <dwg@au.ibm.com>, IBM
 * Based on earlier code written by:
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/asm-prototypes.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
#include <asm/ppc-opcode.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <asm/smp.h>
#include <linux/compiler.h>
#include <linux/context_tracking.h>
#include <linux/mm_types.h>

#include <asm/udbg.h>
#include <asm/code-patching.h>

enum slb_index {
	LINEAR_INDEX	= 0, /* Kernel linear map  (0xc000000000000000) */
	KSTACK_INDEX	= 1, /* Kernel stack map */
};

static long slb_allocate_user(struct mm_struct *mm, unsigned long ea);

#define slb_esid_mask(ssize)	\
	(((ssize) == MMU_SEGSIZE_256M)? ESID_MASK: ESID_MASK_1T)

static inline unsigned long mk_esid_data(unsigned long ea, int ssize,
					 enum slb_index index)
{
	return (ea & slb_esid_mask(ssize)) | SLB_ESID_V | index;
}

static inline unsigned long __mk_vsid_data(unsigned long vsid, int ssize,
					 unsigned long flags)
{
	return (vsid << slb_vsid_shift(ssize)) | flags |
		((unsigned long) ssize << SLB_VSID_SSIZE_SHIFT);
}

static inline unsigned long mk_vsid_data(unsigned long ea, int ssize,
					 unsigned long flags)
{
	return __mk_vsid_data(get_kernel_vsid(ea, ssize), ssize, flags);
}

static void assert_slb_presence(bool present, unsigned long ea)
{
#ifdef CONFIG_DEBUG_VM
	unsigned long tmp;

	WARN_ON_ONCE(mfmsr() & MSR_EE);

	if (!cpu_has_feature(CPU_FTR_ARCH_206))
		return;

	/*
	 * slbfee. requires bit 24 (PPC bit 39) be clear in RB. Hardware
	 * ignores all other bits from 0-27, so just clear them all.
	 */
	ea &= ~((1UL << 28) - 1);
	asm volatile(__PPC_SLBFEE_DOT(%0, %1) : "=r"(tmp) : "r"(ea) : "cr0");

	WARN_ON(present == (tmp == 0));
#endif
}

static inline void slb_shadow_update(unsigned long ea, int ssize,
				     unsigned long flags,
				     enum slb_index index)
{
	struct slb_shadow *p = get_slb_shadow();

	/*
	 * Clear the ESID first so the entry is not valid while we are
	 * updating it.  No write barriers are needed here, provided
	 * we only update the current CPU's SLB shadow buffer.
	 */
	WRITE_ONCE(p->save_area[index].esid, 0);
	WRITE_ONCE(p->save_area[index].vsid, cpu_to_be64(mk_vsid_data(ea, ssize, flags)));
	WRITE_ONCE(p->save_area[index].esid, cpu_to_be64(mk_esid_data(ea, ssize, index)));
}

static inline void slb_shadow_clear(enum slb_index index)
{
	WRITE_ONCE(get_slb_shadow()->save_area[index].esid, cpu_to_be64(index));
}

static inline void create_shadowed_slbe(unsigned long ea, int ssize,
					unsigned long flags,
					enum slb_index index)
{
	/*
	 * Updating the shadow buffer before writing the SLB ensures
	 * we don't get a stale entry here if we get preempted by PHYP
	 * between these two statements.
	 */
	slb_shadow_update(ea, ssize, flags, index);

	assert_slb_presence(false, ea);
	asm volatile("slbmte  %0,%1" :
		     : "r" (mk_vsid_data(ea, ssize, flags)),
		       "r" (mk_esid_data(ea, ssize, index))
		     : "memory" );
}

/*
 * Insert bolted entries into SLB (which may not be empty, so don't clear
 * slb_cache_ptr).
 */
void __slb_restore_bolted_realmode(void)
{
	struct slb_shadow *p = get_slb_shadow();
	enum slb_index index;

	 /* No isync needed because realmode. */
	for (index = 0; index < SLB_NUM_BOLTED; index++) {
		asm volatile("slbmte  %0,%1" :
		     : "r" (be64_to_cpu(p->save_area[index].vsid)),
		       "r" (be64_to_cpu(p->save_area[index].esid)));
	}

	assert_slb_presence(true, local_paca->kstack);
}

/*
 * Insert the bolted entries into an empty SLB.
 */
void slb_restore_bolted_realmode(void)
{
	__slb_restore_bolted_realmode();
	get_paca()->slb_cache_ptr = 0;

	get_paca()->slb_kern_bitmap = (1U << SLB_NUM_BOLTED) - 1;
	get_paca()->slb_used_bitmap = get_paca()->slb_kern_bitmap;
}

/*
 * This flushes all SLB entries including 0, so it must be realmode.
 */
void slb_flush_all_realmode(void)
{
	asm volatile("slbmte %0,%0; slbia" : : "r" (0));
}

/*
 * This flushes non-bolted entries, it can be run in virtual mode. Must
 * be called with interrupts disabled.
 */
void slb_flush_and_restore_bolted(void)
{
	struct slb_shadow *p = get_slb_shadow();

	BUILD_BUG_ON(SLB_NUM_BOLTED != 2);

	WARN_ON(!irqs_disabled());

	/*
	 * We can't take a PMU exception in the following code, so hard
	 * disable interrupts.
	 */
	hard_irq_disable();

	asm volatile("isync\n"
		     "slbia\n"
		     "slbmte  %0, %1\n"
		     "isync\n"
		     :: "r" (be64_to_cpu(p->save_area[KSTACK_INDEX].vsid)),
			"r" (be64_to_cpu(p->save_area[KSTACK_INDEX].esid))
		     : "memory");
	assert_slb_presence(true, get_paca()->kstack);

	get_paca()->slb_cache_ptr = 0;

	get_paca()->slb_kern_bitmap = (1U << SLB_NUM_BOLTED) - 1;
	get_paca()->slb_used_bitmap = get_paca()->slb_kern_bitmap;
}

void slb_save_contents(struct slb_entry *slb_ptr)
{
	int i;
	unsigned long e, v;

	/* Save slb_cache_ptr value. */
	get_paca()->slb_save_cache_ptr = get_paca()->slb_cache_ptr;

	if (!slb_ptr)
		return;

	for (i = 0; i < mmu_slb_size; i++) {
		asm volatile("slbmfee  %0,%1" : "=r" (e) : "r" (i));
		asm volatile("slbmfev  %0,%1" : "=r" (v) : "r" (i));
		slb_ptr->esid = e;
		slb_ptr->vsid = v;
		slb_ptr++;
	}
}

void slb_dump_contents(struct slb_entry *slb_ptr)
{
	int i, n;
	unsigned long e, v;
	unsigned long llp;

	if (!slb_ptr)
		return;

	pr_err("SLB contents of cpu 0x%x\n", smp_processor_id());
	pr_err("Last SLB entry inserted at slot %d\n", get_paca()->stab_rr);

	for (i = 0; i < mmu_slb_size; i++) {
		e = slb_ptr->esid;
		v = slb_ptr->vsid;
		slb_ptr++;

		if (!e && !v)
			continue;

		pr_err("%02d %016lx %016lx\n", i, e, v);

		if (!(e & SLB_ESID_V)) {
			pr_err("\n");
			continue;
		}
		llp = v & SLB_VSID_LLP;
		if (v & SLB_VSID_B_1T) {
			pr_err("  1T  ESID=%9lx  VSID=%13lx LLP:%3lx\n",
			       GET_ESID_1T(e),
			       (v & ~SLB_VSID_B) >> SLB_VSID_SHIFT_1T, llp);
		} else {
			pr_err(" 256M ESID=%9lx  VSID=%13lx LLP:%3lx\n",
			       GET_ESID(e),
			       (v & ~SLB_VSID_B) >> SLB_VSID_SHIFT, llp);
		}
	}
	pr_err("----------------------------------\n");

	/* Dump slb cache entires as well. */
	pr_err("SLB cache ptr value = %d\n", get_paca()->slb_save_cache_ptr);
	pr_err("Valid SLB cache entries:\n");
	n = min_t(int, get_paca()->slb_save_cache_ptr, SLB_CACHE_ENTRIES);
	for (i = 0; i < n; i++)
		pr_err("%02d EA[0-35]=%9x\n", i, get_paca()->slb_cache[i]);
	pr_err("Rest of SLB cache entries:\n");
	for (i = n; i < SLB_CACHE_ENTRIES; i++)
		pr_err("%02d EA[0-35]=%9x\n", i, get_paca()->slb_cache[i]);
}

void slb_vmalloc_update(void)
{
	/*
	 * vmalloc is not bolted, so just have to flush non-bolted.
	 */
	slb_flush_and_restore_bolted();
}

static bool preload_hit(struct thread_info *ti, unsigned long esid)
{
	unsigned char i;

	for (i = 0; i < ti->slb_preload_nr; i++) {
		unsigned char idx;

		idx = (ti->slb_preload_tail + i) % SLB_PRELOAD_NR;
		if (esid == ti->slb_preload_esid[idx])
			return true;
	}
	return false;
}

static bool preload_add(struct thread_info *ti, unsigned long ea)
{
	unsigned char idx;
	unsigned long esid;

	if (mmu_has_feature(MMU_FTR_1T_SEGMENT)) {
		/* EAs are stored >> 28 so 256MB segments don't need clearing */
		if (ea & ESID_MASK_1T)
			ea &= ESID_MASK_1T;
	}

	esid = ea >> SID_SHIFT;

	if (preload_hit(ti, esid))
		return false;

	idx = (ti->slb_preload_tail + ti->slb_preload_nr) % SLB_PRELOAD_NR;
	ti->slb_preload_esid[idx] = esid;
	if (ti->slb_preload_nr == SLB_PRELOAD_NR)
		ti->slb_preload_tail = (ti->slb_preload_tail + 1) % SLB_PRELOAD_NR;
	else
		ti->slb_preload_nr++;

	return true;
}

static void preload_age(struct thread_info *ti)
{
	if (!ti->slb_preload_nr)
		return;
	ti->slb_preload_nr--;
	ti->slb_preload_tail = (ti->slb_preload_tail + 1) % SLB_PRELOAD_NR;
}

void slb_setup_new_exec(void)
{
	struct thread_info *ti = current_thread_info();
	struct mm_struct *mm = current->mm;
	unsigned long exec = 0x10000000;

	WARN_ON(irqs_disabled());

	/*
	 * preload cache can only be used to determine whether a SLB
	 * entry exists if it does not start to overflow.
	 */
	if (ti->slb_preload_nr + 2 > SLB_PRELOAD_NR)
		return;

	hard_irq_disable();

	/*
	 * We have no good place to clear the slb preload cache on exec,
	 * flush_thread is about the earliest arch hook but that happens
	 * after we switch to the mm and have aleady preloaded the SLBEs.
	 *
	 * For the most part that's probably okay to use entries from the
	 * previous exec, they will age out if unused. It may turn out to
	 * be an advantage to clear the cache before switching to it,
	 * however.
	 */

	/*
	 * preload some userspace segments into the SLB.
	 * Almost all 32 and 64bit PowerPC executables are linked at
	 * 0x10000000 so it makes sense to preload this segment.
	 */
	if (!is_kernel_addr(exec)) {
		if (preload_add(ti, exec))
			slb_allocate_user(mm, exec);
	}

	/* Libraries and mmaps. */
	if (!is_kernel_addr(mm->mmap_base)) {
		if (preload_add(ti, mm->mmap_base))
			slb_allocate_user(mm, mm->mmap_base);
	}

	/* see switch_slb */
	asm volatile("isync" : : : "memory");

	local_irq_enable();
}

void preload_new_slb_context(unsigned long start, unsigned long sp)
{
	struct thread_info *ti = current_thread_info();
	struct mm_struct *mm = current->mm;
	unsigned long heap = mm->start_brk;

	WARN_ON(irqs_disabled());

	/* see above */
	if (ti->slb_preload_nr + 3 > SLB_PRELOAD_NR)
		return;

	hard_irq_disable();

	/* Userspace entry address. */
	if (!is_kernel_addr(start)) {
		if (preload_add(ti, start))
			slb_allocate_user(mm, start);
	}

	/* Top of stack, grows down. */
	if (!is_kernel_addr(sp)) {
		if (preload_add(ti, sp))
			slb_allocate_user(mm, sp);
	}

	/* Bottom of heap, grows up. */
	if (heap && !is_kernel_addr(heap)) {
		if (preload_add(ti, heap))
			slb_allocate_user(mm, heap);
	}

	/* see switch_slb */
	asm volatile("isync" : : : "memory");

	local_irq_enable();
}


/* Flush all user entries from the segment table of the current processor. */
void switch_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	struct thread_info *ti = task_thread_info(tsk);
	unsigned char i;

	/*
	 * We need interrupts hard-disabled here, not just soft-disabled,
	 * so that a PMU interrupt can't occur, which might try to access
	 * user memory (to get a stack trace) and possible cause an SLB miss
	 * which would update the slb_cache/slb_cache_ptr fields in the PACA.
	 */
	hard_irq_disable();
	asm volatile("isync" : : : "memory");
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		/*
		 * SLBIA IH=3 invalidates all Class=1 SLBEs and their
		 * associated lookaside structures, which matches what
		 * switch_slb wants. So ARCH_300 does not use the slb
		 * cache.
		 */
		asm volatile(PPC_SLBIA(3));
	} else {
		unsigned long offset = get_paca()->slb_cache_ptr;

		if (!mmu_has_feature(MMU_FTR_NO_SLBIE_B) &&
		    offset <= SLB_CACHE_ENTRIES) {
			unsigned long slbie_data = 0;

			for (i = 0; i < offset; i++) {
				unsigned long ea;

				ea = (unsigned long)
					get_paca()->slb_cache[i] << SID_SHIFT;
				/*
				 * Could assert_slb_presence(true) here, but
				 * hypervisor or machine check could have come
				 * in and removed the entry at this point.
				 */

				slbie_data = ea;
				slbie_data |= user_segment_size(slbie_data)
						<< SLBIE_SSIZE_SHIFT;
				slbie_data |= SLBIE_C; /* user slbs have C=1 */
				asm volatile("slbie %0" : : "r" (slbie_data));
			}

			/* Workaround POWER5 < DD2.1 issue */
			if (!cpu_has_feature(CPU_FTR_ARCH_207S) && offset == 1)
				asm volatile("slbie %0" : : "r" (slbie_data));

		} else {
			struct slb_shadow *p = get_slb_shadow();
			unsigned long ksp_esid_data =
				be64_to_cpu(p->save_area[KSTACK_INDEX].esid);
			unsigned long ksp_vsid_data =
				be64_to_cpu(p->save_area[KSTACK_INDEX].vsid);

			asm volatile(PPC_SLBIA(1) "\n"
				     "slbmte	%0,%1\n"
				     "isync"
				     :: "r"(ksp_vsid_data),
					"r"(ksp_esid_data));

			get_paca()->slb_kern_bitmap = (1U << SLB_NUM_BOLTED) - 1;
		}

		get_paca()->slb_cache_ptr = 0;
	}
	get_paca()->slb_used_bitmap = get_paca()->slb_kern_bitmap;

	copy_mm_to_paca(mm);

	/*
	 * We gradually age out SLBs after a number of context switches to
	 * reduce reload overhead of unused entries (like we do with FP/VEC
	 * reload). Each time we wrap 256 switches, take an entry out of the
	 * SLB preload cache.
	 */
	tsk->thread.load_slb++;
	if (!tsk->thread.load_slb) {
		unsigned long pc = KSTK_EIP(tsk);

		preload_age(ti);
		preload_add(ti, pc);
	}

	for (i = 0; i < ti->slb_preload_nr; i++) {
		unsigned char idx;
		unsigned long ea;

		idx = (ti->slb_preload_tail + i) % SLB_PRELOAD_NR;
		ea = (unsigned long)ti->slb_preload_esid[idx] << SID_SHIFT;

		slb_allocate_user(mm, ea);
	}

	/*
	 * Synchronize slbmte preloads with possible subsequent user memory
	 * address accesses by the kernel (user mode won't happen until
	 * rfid, which is safe).
	 */
	asm volatile("isync" : : : "memory");
}

void slb_set_size(u16 size)
{
	mmu_slb_size = size;
}

void slb_initialize(void)
{
	unsigned long linear_llp, vmalloc_llp, io_llp;
	unsigned long lflags;
	static int slb_encoding_inited;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	unsigned long vmemmap_llp;
#endif

	/* Prepare our SLB miss handler based on our page size */
	linear_llp = mmu_psize_defs[mmu_linear_psize].sllp;
	io_llp = mmu_psize_defs[mmu_io_psize].sllp;
	vmalloc_llp = mmu_psize_defs[mmu_vmalloc_psize].sllp;
	get_paca()->vmalloc_sllp = SLB_VSID_KERNEL | vmalloc_llp;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	vmemmap_llp = mmu_psize_defs[mmu_vmemmap_psize].sllp;
#endif
	if (!slb_encoding_inited) {
		slb_encoding_inited = 1;
		pr_devel("SLB: linear  LLP = %04lx\n", linear_llp);
		pr_devel("SLB: io      LLP = %04lx\n", io_llp);
#ifdef CONFIG_SPARSEMEM_VMEMMAP
		pr_devel("SLB: vmemmap LLP = %04lx\n", vmemmap_llp);
#endif
	}

	get_paca()->stab_rr = SLB_NUM_BOLTED - 1;
	get_paca()->slb_kern_bitmap = (1U << SLB_NUM_BOLTED) - 1;
	get_paca()->slb_used_bitmap = get_paca()->slb_kern_bitmap;

	lflags = SLB_VSID_KERNEL | linear_llp;

	/* Invalidate the entire SLB (even entry 0) & all the ERATS */
	asm volatile("isync":::"memory");
	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_shadowed_slbe(PAGE_OFFSET, mmu_kernel_ssize, lflags, LINEAR_INDEX);

	/* For the boot cpu, we're running on the stack in init_thread_union,
	 * which is in the first segment of the linear mapping, and also
	 * get_paca()->kstack hasn't been initialized yet.
	 * For secondary cpus, we need to bolt the kernel stack entry now.
	 */
	slb_shadow_clear(KSTACK_INDEX);
	if (raw_smp_processor_id() != boot_cpuid &&
	    (get_paca()->kstack & slb_esid_mask(mmu_kernel_ssize)) > PAGE_OFFSET)
		create_shadowed_slbe(get_paca()->kstack,
				     mmu_kernel_ssize, lflags, KSTACK_INDEX);

	asm volatile("isync":::"memory");
}

static void slb_cache_update(unsigned long esid_data)
{
	int slb_cache_index;

	if (cpu_has_feature(CPU_FTR_ARCH_300))
		return; /* ISAv3.0B and later does not use slb_cache */

	/*
	 * Now update slb cache entries
	 */
	slb_cache_index = local_paca->slb_cache_ptr;
	if (slb_cache_index < SLB_CACHE_ENTRIES) {
		/*
		 * We have space in slb cache for optimized switch_slb().
		 * Top 36 bits from esid_data as per ISA
		 */
		local_paca->slb_cache[slb_cache_index++] = esid_data >> 28;
		local_paca->slb_cache_ptr++;
	} else {
		/*
		 * Our cache is full and the current cache content strictly
		 * doesn't indicate the active SLB conents. Bump the ptr
		 * so that switch_slb() will ignore the cache.
		 */
		local_paca->slb_cache_ptr = SLB_CACHE_ENTRIES + 1;
	}
}

static enum slb_index alloc_slb_index(bool kernel)
{
	enum slb_index index;

	/*
	 * The allocation bitmaps can become out of synch with the SLB
	 * when the _switch code does slbie when bolting a new stack
	 * segment and it must not be anywhere else in the SLB. This leaves
	 * a kernel allocated entry that is unused in the SLB. With very
	 * large systems or small segment sizes, the bitmaps could slowly
	 * fill with these entries. They will eventually be cleared out
	 * by the round robin allocator in that case, so it's probably not
	 * worth accounting for.
	 */

	/*
	 * SLBs beyond 32 entries are allocated with stab_rr only
	 * POWER7/8/9 have 32 SLB entries, this could be expanded if a
	 * future CPU has more.
	 */
	if (local_paca->slb_used_bitmap != U32_MAX) {
		index = ffz(local_paca->slb_used_bitmap);
		local_paca->slb_used_bitmap |= 1U << index;
		if (kernel)
			local_paca->slb_kern_bitmap |= 1U << index;
	} else {
		/* round-robin replacement of slb starting at SLB_NUM_BOLTED. */
		index = local_paca->stab_rr;
		if (index < (mmu_slb_size - 1))
			index++;
		else
			index = SLB_NUM_BOLTED;
		local_paca->stab_rr = index;
		if (index < 32) {
			if (kernel)
				local_paca->slb_kern_bitmap |= 1U << index;
			else
				local_paca->slb_kern_bitmap &= ~(1U << index);
		}
	}
	BUG_ON(index < SLB_NUM_BOLTED);

	return index;
}

static long slb_insert_entry(unsigned long ea, unsigned long context,
				unsigned long flags, int ssize, bool kernel)
{
	unsigned long vsid;
	unsigned long vsid_data, esid_data;
	enum slb_index index;

	vsid = get_vsid(context, ea, ssize);
	if (!vsid)
		return -EFAULT;

	/*
	 * There must not be a kernel SLB fault in alloc_slb_index or before
	 * slbmte here or the allocation bitmaps could get out of whack with
	 * the SLB.
	 *
	 * User SLB faults or preloads take this path which might get inlined
	 * into the caller, so add compiler barriers here to ensure unsafe
	 * memory accesses do not come between.
	 */
	barrier();

	index = alloc_slb_index(kernel);

	vsid_data = __mk_vsid_data(vsid, ssize, flags);
	esid_data = mk_esid_data(ea, ssize, index);

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 * User preloads should add isync afterwards in case the kernel
	 * accesses user memory before it returns to userspace with rfid.
	 */
	assert_slb_presence(false, ea);
	asm volatile("slbmte %0, %1" : : "r" (vsid_data), "r" (esid_data));

	barrier();

	if (!kernel)
		slb_cache_update(esid_data);

	return 0;
}

static long slb_allocate_kernel(unsigned long ea, unsigned long id)
{
	unsigned long context;
	unsigned long flags;
	int ssize;

	if (id == KERNEL_REGION_ID) {

		/* We only support upto MAX_PHYSMEM_BITS */
		if ((ea & ~REGION_MASK) > (1UL << MAX_PHYSMEM_BITS))
			return -EFAULT;

		flags = SLB_VSID_KERNEL | mmu_psize_defs[mmu_linear_psize].sllp;

#ifdef CONFIG_SPARSEMEM_VMEMMAP
	} else if (id == VMEMMAP_REGION_ID) {

		if ((ea & ~REGION_MASK) >= (1ULL << MAX_EA_BITS_PER_CONTEXT))
			return -EFAULT;

		flags = SLB_VSID_KERNEL | mmu_psize_defs[mmu_vmemmap_psize].sllp;
#endif
	} else if (id == VMALLOC_REGION_ID) {

		if ((ea & ~REGION_MASK) >= (1ULL << MAX_EA_BITS_PER_CONTEXT))
			return -EFAULT;

		if (ea < H_VMALLOC_END)
			flags = local_paca->vmalloc_sllp;
		else
			flags = SLB_VSID_KERNEL | mmu_psize_defs[mmu_io_psize].sllp;
	} else {
		return -EFAULT;
	}

	ssize = MMU_SEGSIZE_1T;
	if (!mmu_has_feature(MMU_FTR_1T_SEGMENT))
		ssize = MMU_SEGSIZE_256M;

	context = get_kernel_context(ea);
	return slb_insert_entry(ea, context, flags, ssize, true);
}

static long slb_allocate_user(struct mm_struct *mm, unsigned long ea)
{
	unsigned long context;
	unsigned long flags;
	int bpsize;
	int ssize;

	/*
	 * consider this as bad access if we take a SLB miss
	 * on an address above addr limit.
	 */
	if (ea >= mm->context.slb_addr_limit)
		return -EFAULT;

	context = get_user_context(&mm->context, ea);
	if (!context)
		return -EFAULT;

	if (unlikely(ea >= H_PGTABLE_RANGE)) {
		WARN_ON(1);
		return -EFAULT;
	}

	ssize = user_segment_size(ea);

	bpsize = get_slice_psize(mm, ea);
	flags = SLB_VSID_USER | mmu_psize_defs[bpsize].sllp;

	return slb_insert_entry(ea, context, flags, ssize, false);
}

long do_slb_fault(struct pt_regs *regs, unsigned long ea)
{
	unsigned long id = REGION_ID(ea);

	/* IRQs are not reconciled here, so can't check irqs_disabled */
	VM_WARN_ON(mfmsr() & MSR_EE);

	if (unlikely(!(regs->msr & MSR_RI)))
		return -EINVAL;

	/*
	 * SLB kernel faults must be very careful not to touch anything
	 * that is not bolted. E.g., PACA and global variables are okay,
	 * mm->context stuff is not.
	 *
	 * SLB user faults can access all of kernel memory, but must be
	 * careful not to touch things like IRQ state because it is not
	 * "reconciled" here. The difficulty is that we must use
	 * fast_exception_return to return from kernel SLB faults without
	 * looking at possible non-bolted memory. We could test user vs
	 * kernel faults in the interrupt handler asm and do a full fault,
	 * reconcile, ret_from_except for user faults which would make them
	 * first class kernel code. But for performance it's probably nicer
	 * if they go via fast_exception_return too.
	 */
	if (id >= KERNEL_REGION_ID) {
		long err;
#ifdef CONFIG_DEBUG_VM
		/* Catch recursive kernel SLB faults. */
		BUG_ON(local_paca->in_kernel_slb_handler);
		local_paca->in_kernel_slb_handler = 1;
#endif
		err = slb_allocate_kernel(ea, id);
#ifdef CONFIG_DEBUG_VM
		local_paca->in_kernel_slb_handler = 0;
#endif
		return err;
	} else {
		struct mm_struct *mm = current->mm;
		long err;

		if (unlikely(!mm))
			return -EFAULT;

		err = slb_allocate_user(mm, ea);
		if (!err)
			preload_add(current_thread_info(), ea);

		return err;
	}
}

void do_bad_slb_fault(struct pt_regs *regs, unsigned long ea, long err)
{
	if (err == -EFAULT) {
		if (user_mode(regs))
			_exception(SIGSEGV, regs, SEGV_BNDERR, ea);
		else
			bad_page_fault(regs, ea, SIGSEGV);
	} else if (err == -EINVAL) {
		unrecoverable_exception(regs);
	} else {
		BUG();
	}
}
