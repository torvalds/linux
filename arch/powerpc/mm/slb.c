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
#include <asm/firmware.h>
#include <linux/compiler.h>
#include <asm/udbg.h>


extern void slb_allocate_realmode(unsigned long ea);
extern void slb_allocate_user(unsigned long ea);

static void slb_allocate(unsigned long ea)
{
	/* Currently, we do real mode for all SLBs including user, but
	 * that will change if we bring back dynamic VSIDs
	 */
	slb_allocate_realmode(ea);
}

#define slb_esid_mask(ssize)	\
	(((ssize) == MMU_SEGSIZE_256M)? ESID_MASK: ESID_MASK_1T)

static inline unsigned long mk_esid_data(unsigned long ea, int ssize,
					 unsigned long slot)
{
	return (ea & slb_esid_mask(ssize)) | SLB_ESID_V | slot;
}

#define slb_vsid_shift(ssize)	\
	((ssize) == MMU_SEGSIZE_256M? SLB_VSID_SHIFT: SLB_VSID_SHIFT_1T)

static inline unsigned long mk_vsid_data(unsigned long ea, int ssize,
					 unsigned long flags)
{
	return (get_kernel_vsid(ea, ssize) << slb_vsid_shift(ssize)) | flags |
		((unsigned long) ssize << SLB_VSID_SSIZE_SHIFT);
}

static inline void slb_shadow_update(unsigned long ea, int ssize,
				     unsigned long flags,
				     unsigned long entry)
{
	/*
	 * Clear the ESID first so the entry is not valid while we are
	 * updating it.  No write barriers are needed here, provided
	 * we only update the current CPU's SLB shadow buffer.
	 */
	get_slb_shadow()->save_area[entry].esid = 0;
	get_slb_shadow()->save_area[entry].vsid = mk_vsid_data(ea, ssize, flags);
	get_slb_shadow()->save_area[entry].esid = mk_esid_data(ea, ssize, entry);
}

static inline void slb_shadow_clear(unsigned long entry)
{
	get_slb_shadow()->save_area[entry].esid = 0;
}

static inline void create_shadowed_slbe(unsigned long ea, int ssize,
					unsigned long flags,
					unsigned long entry)
{
	/*
	 * Updating the shadow buffer before writing the SLB ensures
	 * we don't get a stale entry here if we get preempted by PHYP
	 * between these two statements.
	 */
	slb_shadow_update(ea, ssize, flags, entry);

	asm volatile("slbmte  %0,%1" :
		     : "r" (mk_vsid_data(ea, ssize, flags)),
		       "r" (mk_esid_data(ea, ssize, entry))
		     : "memory" );
}

void slb_flush_and_rebolt(void)
{
	/* If you change this make sure you change SLB_NUM_BOLTED
	 * appropriately too. */
	unsigned long linear_llp, vmalloc_llp, lflags, vflags;
	unsigned long ksp_esid_data, ksp_vsid_data;

	WARN_ON(!irqs_disabled());

	linear_llp = mmu_psize_defs[mmu_linear_psize].sllp;
	vmalloc_llp = mmu_psize_defs[mmu_vmalloc_psize].sllp;
	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	ksp_esid_data = mk_esid_data(get_paca()->kstack, mmu_kernel_ssize, 2);
	if ((ksp_esid_data & ~0xfffffffUL) <= PAGE_OFFSET) {
		ksp_esid_data &= ~SLB_ESID_V;
		ksp_vsid_data = 0;
		slb_shadow_clear(2);
	} else {
		/* Update stack entry; others don't change */
		slb_shadow_update(get_paca()->kstack, mmu_kernel_ssize, lflags, 2);
		ksp_vsid_data = get_slb_shadow()->save_area[2].vsid;
	}

	/*
	 * We can't take a PMU exception in the following code, so hard
	 * disable interrupts.
	 */
	hard_irq_disable();

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
		        "r"(mk_esid_data(VMALLOC_START, mmu_kernel_ssize, 1)),
		        "r"(ksp_vsid_data),
		        "r"(ksp_esid_data)
		     : "memory");
}

void slb_vmalloc_update(void)
{
	unsigned long vflags;

	vflags = SLB_VSID_KERNEL | mmu_psize_defs[mmu_vmalloc_psize].sllp;
	slb_shadow_update(VMALLOC_START, mmu_kernel_ssize, vflags, 1);
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
	if (!cpu_has_feature(CPU_FTR_1T_SEGMENT))
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
	unsigned long offset = get_paca()->slb_cache_ptr;
	unsigned long slbie_data = 0;
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;

	if (!cpu_has_feature(CPU_FTR_NO_SLBIE_B) &&
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
		slb_flush_and_rebolt();
	}

	/* Workaround POWER5 < DD2.1 issue */
	if (offset == 1 || offset > SLB_CACHE_ENTRIES)
		asm volatile("slbie %0" : : "r" (slbie_data));

	get_paca()->slb_cache_ptr = 0;
	get_paca()->context = mm->context;

	/*
	 * preload some userspace segments into the SLB.
	 */
	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	if (is_kernel_addr(pc))
		return;
	slb_allocate(pc);

	if (esids_match(pc,stack))
		return;

	if (is_kernel_addr(stack))
		return;
	slb_allocate(stack);

	if (esids_match(pc,unmapped_base) || esids_match(stack,unmapped_base))
		return;

	if (is_kernel_addr(unmapped_base))
		return;
	slb_allocate(unmapped_base);
}

static inline void patch_slb_encoding(unsigned int *insn_addr,
				      unsigned int immed)
{
	/* Assume the instruction had a "0" immediate value, just
	 * "or" in the new value
	 */
	*insn_addr |= immed;
	flush_icache_range((unsigned long)insn_addr, 4+
			   (unsigned long)insn_addr);
}

void slb_initialize(void)
{
	unsigned long linear_llp, vmalloc_llp, io_llp;
	unsigned long lflags, vflags;
	static int slb_encoding_inited;
	extern unsigned int *slb_miss_kernel_load_linear;
	extern unsigned int *slb_miss_kernel_load_io;
	extern unsigned int *slb_compare_rr_to_size;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	extern unsigned int *slb_miss_kernel_load_vmemmap;
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

	get_paca()->stab_rr = SLB_NUM_BOLTED;

	/* On iSeries the bolted entries have already been set up by
	 * the hypervisor from the lparMap data in head.S */
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return;

	lflags = SLB_VSID_KERNEL | linear_llp;
	vflags = SLB_VSID_KERNEL | vmalloc_llp;

	/* Invalidate the entire SLB (even slot 0) & all the ERATS */
	asm volatile("isync":::"memory");
	asm volatile("slbmte  %0,%0"::"r" (0) : "memory");
	asm volatile("isync; slbia; isync":::"memory");
	create_shadowed_slbe(PAGE_OFFSET, mmu_kernel_ssize, lflags, 0);

	create_shadowed_slbe(VMALLOC_START, mmu_kernel_ssize, vflags, 1);

	/* For the boot cpu, we're running on the stack in init_thread_union,
	 * which is in the first segment of the linear mapping, and also
	 * get_paca()->kstack hasn't been initialized yet.
	 * For secondary cpus, we need to bolt the kernel stack entry now.
	 */
	slb_shadow_clear(2);
	if (raw_smp_processor_id() != boot_cpuid &&
	    (get_paca()->kstack & slb_esid_mask(mmu_kernel_ssize)) > PAGE_OFFSET)
		create_shadowed_slbe(get_paca()->kstack,
				     mmu_kernel_ssize, lflags, 2);

	asm volatile("isync":::"memory");
}
