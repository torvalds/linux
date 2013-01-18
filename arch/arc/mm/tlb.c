/*
 * TLB Management (flush/create/diagnostics) for ARC700
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <asm/arcregs.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

/* A copy of the ASID from the PID reg is kept in asid_cache */
int asid_cache = FIRST_ASID;

/* ASID to mm struct mapping. We have one extra entry corresponding to
 * NO_ASID to save us a compare when clearing the mm entry for old asid
 * see get_new_mmu_context (asm-arc/mmu_context.h)
 */
struct mm_struct *asid_mm_map[NUM_ASID + 1];


/*
 * Routine to create a TLB entry
 */
void create_tlb(struct vm_area_struct *vma, unsigned long address, pte_t *ptep)
{
	unsigned long flags;
	unsigned int idx, asid_or_sasid;
	unsigned long pd0_flags;

	/*
	 * create_tlb() assumes that current->mm == vma->mm, since
	 * -it ASID for TLB entry is fetched from MMU ASID reg (valid for curr)
	 * -completes the lazy write to SASID reg (again valid for curr tsk)
	 *
	 * Removing the assumption involves
	 * -Using vma->mm->context{ASID,SASID}, as opposed to MMU reg.
	 * -Fix the TLB paranoid debug code to not trigger false negatives.
	 * -More importantly it makes this handler inconsistent with fast-path
	 *  TLB Refill handler which always deals with "current"
	 *
	 * Lets see the use cases when current->mm != vma->mm and we land here
	 *  1. execve->copy_strings()->__get_user_pages->handle_mm_fault
	 *     Here VM wants to pre-install a TLB entry for user stack while
	 *     current->mm still points to pre-execve mm (hence the condition).
	 *     However the stack vaddr is soon relocated (randomization) and
	 *     move_page_tables() tries to undo that TLB entry.
	 *     Thus not creating TLB entry is not any worse.
	 *
	 *  2. ptrace(POKETEXT) causes a CoW - debugger(current) inserting a
	 *     breakpoint in debugged task. Not creating a TLB now is not
	 *     performance critical.
	 *
	 * Both the cases above are not good enough for code churn.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	tlb_paranoid_check(vma->vm_mm->context.asid, address);

	address &= PAGE_MASK;

	/* update this PTE credentials */
	pte_val(*ptep) |= (_PAGE_PRESENT | _PAGE_ACCESSED);

	/* Create HW TLB entry Flags (in PD0) from PTE Flags */
#if (CONFIG_ARC_MMU_VER <= 2)
	pd0_flags = ((pte_val(*ptep) & PTE_BITS_IN_PD0) >> 1);
#else
	pd0_flags = ((pte_val(*ptep) & PTE_BITS_IN_PD0));
#endif

	/* ASID for this task */
	asid_or_sasid = read_aux_reg(ARC_REG_PID) & 0xff;

	write_aux_reg(ARC_REG_TLBPD0, address | pd0_flags | asid_or_sasid);

	/* Load remaining info in PD1 (Page Frame Addr and Kx/Kw/Kr Flags) */
	write_aux_reg(ARC_REG_TLBPD1, (pte_val(*ptep) & PTE_BITS_IN_PD1));

	/* First verify if entry for this vaddr+ASID already exists */
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBProbe);
	idx = read_aux_reg(ARC_REG_TLBINDEX);

	/*
	 * If Not already present get a free slot from MMU.
	 * Otherwise, Probe would have located the entry and set INDEX Reg
	 * with existing location. This will cause Write CMD to over-write
	 * existing entry with new PD0 and PD1
	 */
	if (likely(idx & TLB_LKUP_ERR))
		write_aux_reg(ARC_REG_TLBCOMMAND, TLBGetIndex);

	/*
	 * Commit the Entry to MMU
	 * It doesnt sound safe to use the TLBWriteNI cmd here
	 * which doesn't flush uTLBs. I'd rather be safe than sorry.
	 */
	write_aux_reg(ARC_REG_TLBCOMMAND, TLBWrite);

	local_irq_restore(flags);
}

/* arch hook called by core VM at the end of handle_mm_fault( ),
 * when a new PTE is entered in Page Tables or an existing one
 * is modified. We aggresively pre-install a TLB entry
 */

void update_mmu_cache(struct vm_area_struct *vma, unsigned long vaddress,
		      pte_t *ptep)
{

	create_tlb(vma, vaddress, ptep);
}

/* Read the Cache Build Confuration Registers, Decode them and save into
 * the cpuinfo structure for later use.
 * No Validation is done here, simply read/convert the BCRs
 */
void __init read_decode_mmu_bcr(void)
{
	unsigned int tmp;
	struct bcr_mmu_1_2 *mmu2;	/* encoded MMU2 attr */
	struct bcr_mmu_3 *mmu3;		/* encoded MMU3 attr */
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;

	tmp = read_aux_reg(ARC_REG_MMU_BCR);
	mmu->ver = (tmp >> 24);

	if (mmu->ver <= 2) {
		mmu2 = (struct bcr_mmu_1_2 *)&tmp;
		mmu->pg_sz = PAGE_SIZE;
		mmu->sets = 1 << mmu2->sets;
		mmu->ways = 1 << mmu2->ways;
		mmu->u_dtlb = mmu2->u_dtlb;
		mmu->u_itlb = mmu2->u_itlb;
	} else {
		mmu3 = (struct bcr_mmu_3 *)&tmp;
		mmu->pg_sz = 512 << mmu3->pg_sz;
		mmu->sets = 1 << mmu3->sets;
		mmu->ways = 1 << mmu3->ways;
		mmu->u_dtlb = mmu3->u_dtlb;
		mmu->u_itlb = mmu3->u_itlb;
	}

	mmu->num_tlb = mmu->sets * mmu->ways;
}

void __init arc_mmu_init(void)
{
	/*
	 * ASID mgmt data structures are compile time init
	 *  asid_cache = FIRST_ASID and asid_mm_map[] all zeroes
	 */

	local_flush_tlb_all();

	/* Enable the MMU */
	write_aux_reg(ARC_REG_PID, MMU_ENABLE);
}

/*
 * TLB Programmer's Model uses Linear Indexes: 0 to {255, 511} for 128 x {2,4}
 * The mapping is Column-first.
 *		---------------------	-----------
 *		|way0|way1|way2|way3|	|way0|way1|
 *		---------------------	-----------
 * [set0]	|  0 |  1 |  2 |  3 |	|  0 |  1 |
 * [set1]	|  4 |  5 |  6 |  7 |	|  2 |  3 |
 *		~		    ~	~	  ~
 * [set127]	| 508| 509| 510| 511|	| 254| 255|
 *		---------------------	-----------
 * For normal operations we don't(must not) care how above works since
 * MMU cmd getIndex(vaddr) abstracts that out.
 * However for walking WAYS of a SET, we need to know this
 */
#define SET_WAY_TO_IDX(mmu, set, way)  ((set) * mmu->ways + (way))

/* Handling of Duplicate PD (TLB entry) in MMU.
 * -Could be due to buggy customer tapeouts or obscure kernel bugs
 * -MMU complaints not at the time of duplicate PD installation, but at the
 *      time of lookup matching multiple ways.
 * -Ideally these should never happen - but if they do - workaround by deleting
 *      the duplicate one.
 * -Knob to be verbose abt it.(TODO: hook them up to debugfs)
 */
volatile int dup_pd_verbose = 1;/* Be slient abt it or complain (default) */

void do_tlb_overlap_fault(unsigned long cause, unsigned long address,
			  struct pt_regs *regs)
{
	int set, way, n;
	unsigned int pd0[4], pd1[4];	/* assume max 4 ways */
	unsigned long flags, is_valid;
	struct cpuinfo_arc_mmu *mmu = &cpuinfo_arc700[smp_processor_id()].mmu;

	local_irq_save(flags);

	/* re-enable the MMU */
	write_aux_reg(ARC_REG_PID, MMU_ENABLE | read_aux_reg(ARC_REG_PID));

	/* loop thru all sets of TLB */
	for (set = 0; set < mmu->sets; set++) {

		/* read out all the ways of current set */
		for (way = 0, is_valid = 0; way < mmu->ways; way++) {
			write_aux_reg(ARC_REG_TLBINDEX,
					  SET_WAY_TO_IDX(mmu, set, way));
			write_aux_reg(ARC_REG_TLBCOMMAND, TLBRead);
			pd0[way] = read_aux_reg(ARC_REG_TLBPD0);
			pd1[way] = read_aux_reg(ARC_REG_TLBPD1);
			is_valid |= pd0[way] & _PAGE_PRESENT;
		}

		/* If all the WAYS in SET are empty, skip to next SET */
		if (!is_valid)
			continue;

		/* Scan the set for duplicate ways: needs a nested loop */
		for (way = 0; way < mmu->ways; way++) {
			if (!pd0[way])
				continue;

			for (n = way + 1; n < mmu->ways; n++) {
				if ((pd0[way] & PAGE_MASK) ==
				    (pd0[n] & PAGE_MASK)) {

					if (dup_pd_verbose) {
						pr_info("Duplicate PD's @"
							"[%d:%d]/[%d:%d]\n",
						     set, way, set, n);
						pr_info("TLBPD0[%u]: %08x\n",
						     way, pd0[way]);
					}

					/*
					 * clear entry @way and not @n. This is
					 * critical to our optimised loop
					 */
					pd0[way] = pd1[way] = 0;
					write_aux_reg(ARC_REG_TLBINDEX,
						SET_WAY_TO_IDX(mmu, set, way));
					__tlb_entry_erase();
				}
			}
		}
	}

	local_irq_restore(flags);
}

/***********************************************************************
 * Diagnostic Routines
 *  -Called from Low Level TLB Hanlders if things don;t look good
 **********************************************************************/

#ifdef CONFIG_ARC_DBG_TLB_PARANOIA

/*
 * Low Level ASM TLB handler calls this if it finds that HW and SW ASIDS
 * don't match
 */
void print_asid_mismatch(int is_fast_path)
{
	int pid_sw, pid_hw;
	pid_sw = current->active_mm->context.asid;
	pid_hw = read_aux_reg(ARC_REG_PID) & 0xff;

	pr_emerg("ASID Mismatch in %s Path Handler: sw-pid=0x%x hw-pid=0x%x\n",
	       is_fast_path ? "Fast" : "Slow", pid_sw, pid_hw);

	__asm__ __volatile__("flag 1");
}

void tlb_paranoid_check(unsigned int pid_sw, unsigned long addr)
{
	unsigned int pid_hw;

	pid_hw = read_aux_reg(ARC_REG_PID) & 0xff;

	if (addr < 0x70000000 && ((pid_hw != pid_sw) || (pid_sw == NO_ASID)))
		print_asid_mismatch(0);
}
#endif
