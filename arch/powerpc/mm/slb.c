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

#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
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
	VMALLOC_INDEX	= 1, /* Kernel virtual map (0xd000000000000000) */
	KSTACK_INDEX	= 2, /* Kernel stack map */
};

extern void slb_allocate(unsigned long ea);

#define slb_esid_mask(ssize)	\
	(((ssize) == MMU_SEGSIZE_256M)? ESID_MASK: ESID_MASK_1T)

static inline unsigned long mk_esid_data(unsigned long ea, int ssize,
					 enum slb_index index)
{
	return (ea & slb_esid_mask(ssize)) | SLB_ESID_V | index;
}

static inline unsigned long mk_vsid_data(unsigned long ea, int ssize,
					 unsigned long flags)
{
	return (get_kernel_vsid(ea, ssize) << slb_vsid_shift(ssize)) | flags |
		((unsigned long) ssize << SLB_VSID_SSIZE_SHIFT);
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
}

/*
 * Insert the bolted entries into an empty SLB.
 * This is not the same as rebolt because the bolted segments are not
 * changed, just loaded from the shadow area.
 */
void slb_restore_bolted_realmode(void)
{
	__slb_restore_bolted_realmode();
	get_paca()->slb_cache_ptr = 0;
}

/*
 * This flushes all SLB entries including 0, so it must be realmode.
 */
void slb_flush_all_realmode(void)
{
	/*
	 * This flushes all SLB entries including 0, so it must be realmode.
	 */
	asm volatile("slbmte %0,%0; slbia" : : "r" (0));
}

static void __slb_flush_and_rebolt(void)
{
	/* If you change this make sure you change SLB_NUM_BOLTED
	 * and PR KVM appropriately too. */
	unsigned long linear_llp, vmalloc_llp, lflags, vflags;
	unsigned long ksp_esid_data, ksp_vsid_data;

	linear_llp = mmu_psize_defs[mmu_linear_psize].sllp;
	vmalloc_llp = mmu_psize_defs[mmu_vmalloc_psize].sllp;
	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	ksp_esid_data = mk_esid_data(get_paca()->kstack, mmu_kernel_ssize, KSTACK_INDEX);
	if ((ksp_esid_data & ~0xfffffffUL) <= PAGE_OFFSET) {
		ksp_esid_data &= ~SLB_ESID_V;
		ksp_vsid_data = 0;
		slb_shadow_clear(KSTACK_INDEX);
	} else {
		/* Update stack entry; others don't change */
		slb_shadow_update(get_paca()->kstack, mmu_kernel_ssize, lflags, KSTACK_INDEX);
		ksp_vsid_data =
			be64_to_cpu(get_slb_shadow()->save_area[KSTACK_INDEX].vsid);
	}

	/* We need to do this all in asm, so we're sure we don't touch
	 * the stack between the slbia and rebolting it. */
	asm volatile("isync\n"
		     "slbia\n"
		     /* Slot 1 - first VMALLOC segment */
		     "slbmte	%0,%1\n"
		     /* Slot 2 - kernel stack */
		     "slbmte	%2,%3\n"
		     "isync"
		     :: "r"(mk_vsid_data(VMALLOC_START, mmu_kernel_ssize, vflags)),
		        "r"(mk_esid_data(VMALLOC_START, mmu_kernel_ssize, VMALLOC_INDEX)),
		        "r"(ksp_vsid_data),
		        "r"(ksp_esid_data)
		     : "memory");
}

void slb_flush_and_rebolt(void)
{

	WARN_ON(!irqs_disabled());

	/*
	 * We can't take a PMU exception in the following code, so hard
	 * disable interrupts.
	 */
	hard_irq_disable();

	__slb_flush_and_rebolt();
	get_paca()->slb_cache_ptr = 0;
}

void slb_vmalloc_update(void)
{
	unsigned long vflags;

	vflags = SLB_VSID_KERNEL | mmu_psize_defs[mmu_vmalloc_psize].sllp;
	slb_shadow_update(VMALLOC_START, mmu_kernel_ssize, vflags, VMALLOC_INDEX);
	slb_flush_and_rebolt();
}

/* Helper function to compare esids.  There are four cases to handle.
 * 1. The system is not 1T segment size capable.  Use the GET_ESID compare.
 * 2. The system is 1T capable, both addresses are < 1T, use the GET_ESID compare.
 * 3. The system is 1T capable, only one of the two addresses is > 1T.  This is not a match.
 * 4. The system is 1T capable, both addresses are > 1T, use the GET_ESID_1T macro to compare.
 */
static inline int esids_match(unsigned long addr1, unsigned long addr2)
{
	int esid_1t_count;

	/* System is not 1T segment size capable. */
	if (!mmu_has_feature(MMU_FTR_1T_SEGMENT))
		return (GET_ESID(addr1) == GET_ESID(addr2));

	esid_1t_count = (((addr1 >> SID_SHIFT_1T) != 0) +
				((addr2 >> SID_SHIFT_1T) != 0));

	/* both addresses are < 1T */
	if (esid_1t_count == 0)
		return (GET_ESID(addr1) == GET_ESID(addr2));

	/* One address < 1T, the other > 1T.  Not a match */
	if (esid_1t_count == 1)
		return 0;

	/* Both addresses are > 1T. */
	return (GET_ESID_1T(addr1) == GET_ESID_1T(addr2));
}

/* Flush all user entries from the segment table of the current processor. */
void switch_slb(struct task_struct *tsk, struct mm_struct *mm)
{
	unsigned long offset;
	unsigned long slbie_data = 0;
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long exec_base;

	/*
	 * We need interrupts hard-disabled here, not just soft-disabled,
	 * so that a PMU interrupt can't occur, which might try to access
	 * user memory (to get a stack trace) and possible cause an SLB miss
	 * which would update the slb_cache/slb_cache_ptr fields in the PACA.
	 */
	hard_irq_disable();
	offset = get_paca()->slb_cache_ptr;
	if (!mmu_has_feature(MMU_FTR_NO_SLBIE_B) &&
	    offset <= SLB_CACHE_ENTRIES) {
		int i;
		asm volatile("isync" : : : "memory");
		for (i = 0; i < offset; i++) {
			slbie_data = (unsigned long)get_paca()->slb_cache[i]
				<< SID_SHIFT; /* EA */
			slbie_data |= user_segment_size(slbie_data)
				<< SLBIE_SSIZE_SHIFT;
			slbie_data |= SLBIE_C; /* C set for user addresses */
			asm volatile("slbie %0" : : "r" (slbie_data));
		}
		asm volatile("isync" : : : "memory");
	} else {
		__slb_flush_and_rebolt();
	}

	/* Workaround POWER5 < DD2.1 issue */
	if (offset == 1 || offset > SLB_CACHE_ENTRIES)
		asm volatile("slbie %0" : : "r" (slbie_data));

	get_paca()->slb_cache_ptr = 0;
	copy_mm_to_paca(mm);

	/*
	 * preload some userspace segments into the SLB.
	 * Almost all 32 and 64bit PowerPC executables are linked at
	 * 0x10000000 so it makes sense to preload this segment.
	 */
	exec_base = 0x10000000;

	if (is_kernel_addr(pc) || is_kernel_addr(stack) ||
	    is_kernel_addr(exec_base))
		return;

	slb_allocate(pc);

	if (!esids_match(pc, stack))
		slb_allocate(stack);

	if (!esids_match(pc, exec_base) &&
	    !esids_match(stack, exec_base))
		slb_allocate(exec_base);
}

static inline void patch_slb_encoding(unsigned int *insn_addr,
				      unsigned int immed)
{

	/*
	 * This function patches either an li or a cmpldi instruction with
	 * a new immediate value. This relies on the fact that both li
	 * (which is actually addi) and cmpldi both take a 16-bit immediate
	 * value, and it is situated in the same location in the instruction,
	 * ie. bits 16-31 (Big endian bit order) or the lower 16 bits.
	 * The signedness of the immediate operand differs between the two
	 * instructions however this code is only ever patching a small value,
	 * much less than 1 << 15, so we can get away with it.
	 * To patch the value we read the existing instruction, clear the
	 * immediate value, and or in our new value, then write the instruction
	 * back.
	 */
	unsigned int insn = (*insn_addr & 0xffff0000) | immed;
	patch_instruction(insn_addr, insn);
}

extern u32 slb_miss_kernel_load_linear[];
extern u32 slb_miss_kernel_load_io[];
extern u32 slb_compare_rr_to_size[];
extern u32 slb_miss_kernel_load_vmemmap[];

void slb_set_size(u16 size)
{
	if (mmu_slb_size == size)
		return;

	mmu_slb_size = size;
	patch_slb_encoding(slb_compare_rr_to_size, mmu_slb_size);
}

void slb_initialize(void)
{
	unsigned long linear_llp, vmalloc_llp, io_llp;
	unsigned long lflags, vflags;
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
		patch_slb_encoding(slb_miss_kernel_load_linear,
				   SLB_VSID_KERNEL | linear_llp);
		patch_slb_encoding(slb_miss_kernel_load_io,
				   SLB_VSID_KERNEL | io_llp);
		patch_slb_encoding(slb_compare_rr_to_size,
				   mmu_slb_size);

		pr_devel("SLB: linear  LLP = %04lx\n", linear_llp);
		pr_devel("SLB: io      LLP = %04lx\n", io_llp);

#ifdef CONFIG_SPARSEMEM_VMEMMAP
		patch_slb_encoding(slb_miss_kernel_load_vmemmap,
				   SLB_VSID_KERNEL | vmemmap_llp);
		pr_devel("SLB: vmemmap LLP = %04lx\n", vmemmap_llp);
#endif
	}

	get_paca()->stab_rr = SLB_NUM_BOLTED - 1;

	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	/* Invalidate the entire SLB (even entry 0) & all the ERATS */
	asm volatile("isync":::"memory");
	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_shadowed_slbe(PAGE_OFFSET, mmu_kernel_ssize, lflags, LINEAR_INDEX);
	create_shadowed_slbe(VMALLOC_START, mmu_kernel_ssize, vflags, VMALLOC_INDEX);

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

static void insert_slb_entry(unsigned long vsid, unsigned long ea,
			     int bpsize, int ssize)
{
	unsigned long flags, vsid_data, esid_data;
	enum slb_index index;
	int slb_cache_index;

	/*
	 * We are irq disabled, hence should be safe to access PACA.
	 */
	VM_WARN_ON(!irqs_disabled());

	/*
	 * We can't take a PMU exception in the following code, so hard
	 * disable interrupts.
	 */
	hard_irq_disable();

	index = get_paca()->stab_rr;

	/*
	 * simple round-robin replacement of slb starting at SLB_NUM_BOLTED.
	 */
	if (index < (mmu_slb_size - 1))
		index++;
	else
		index = SLB_NUM_BOLTED;

	get_paca()->stab_rr = index;

	flags = SLB_VSID_USER | mmu_psize_defs[bpsize].sllp;
	vsid_data = (vsid << slb_vsid_shift(ssize)) | flags |
		    ((unsigned long) ssize << SLB_VSID_SSIZE_SHIFT);
	esid_data = mk_esid_data(ea, ssize, index);

	/*
	 * No need for an isync before or after this slbmte. The exception
	 * we enter with and the rfid we exit with are context synchronizing.
	 * Also we only handle user segments here.
	 */
	asm volatile("slbmte %0, %1" : : "r" (vsid_data), "r" (esid_data)
		     : "memory");

	/*
	 * Now update slb cache entries
	 */
	slb_cache_index = get_paca()->slb_cache_ptr;
	if (slb_cache_index < SLB_CACHE_ENTRIES) {
		/*
		 * We have space in slb cache for optimized switch_slb().
		 * Top 36 bits from esid_data as per ISA
		 */
		get_paca()->slb_cache[slb_cache_index++] = esid_data >> 28;
		get_paca()->slb_cache_ptr++;
	} else {
		/*
		 * Our cache is full and the current cache content strictly
		 * doesn't indicate the active SLB conents. Bump the ptr
		 * so that switch_slb() will ignore the cache.
		 */
		get_paca()->slb_cache_ptr = SLB_CACHE_ENTRIES + 1;
	}
}

static void handle_multi_context_slb_miss(int context_id, unsigned long ea)
{
	struct mm_struct *mm = current->mm;
	unsigned long vsid;
	int bpsize;

	/*
	 * We are always above 1TB, hence use high user segment size.
	 */
	vsid = get_vsid(context_id, ea, mmu_highuser_ssize);
	bpsize = get_slice_psize(mm, ea);
	insert_slb_entry(vsid, ea, bpsize, mmu_highuser_ssize);
}

void slb_miss_large_addr(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();
	unsigned long ea = regs->dar;
	int context;

	if (REGION_ID(ea) != USER_REGION_ID)
		goto slb_bad_addr;

	/*
	 * Are we beyound what the page table layout supports ?
	 */
	if ((ea & ~REGION_MASK) >= H_PGTABLE_RANGE)
		goto slb_bad_addr;

	/* Lower address should have been handled by asm code */
	if (ea < (1UL << MAX_EA_BITS_PER_CONTEXT))
		goto slb_bad_addr;

	/*
	 * consider this as bad access if we take a SLB miss
	 * on an address above addr limit.
	 */
	if (ea >= current->mm->context.slb_addr_limit)
		goto slb_bad_addr;

	context = get_ea_context(&current->mm->context, ea);
	if (!context)
		goto slb_bad_addr;

	handle_multi_context_slb_miss(context, ea);
	exception_exit(prev_state);
	return;

slb_bad_addr:
	if (user_mode(regs))
		_exception(SIGSEGV, regs, SEGV_BNDERR, ea);
	else
		bad_page_fault(regs, ea, SIGSEGV);
	exception_exit(prev_state);
}
