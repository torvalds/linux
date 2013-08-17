/*
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, yu.liu@freescale.com
 *
 * Description:
 * This file is based on arch/powerpc/kvm/44x_tlb.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <linux/hugetlb.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_e500.h>

#include "../mm/mmu_decl.h"
#include "e500_tlb.h"
#include "trace.h"
#include "timing.h"

#define to_htlb1_esel(esel) (host_tlb_params[1].entries - (esel) - 1)

struct id {
	unsigned long val;
	struct id **pentry;
};

#define NUM_TIDS 256

/*
 * This table provide mappings from:
 * (guestAS,guestTID,guestPR) --> ID of physical cpu
 * guestAS	[0..1]
 * guestTID	[0..255]
 * guestPR	[0..1]
 * ID		[1..255]
 * Each vcpu keeps one vcpu_id_table.
 */
struct vcpu_id_table {
	struct id id[2][NUM_TIDS][2];
};

/*
 * This table provide reversed mappings of vcpu_id_table:
 * ID --> address of vcpu_id_table item.
 * Each physical core has one pcpu_id_table.
 */
struct pcpu_id_table {
	struct id *entry[NUM_TIDS];
};

static DEFINE_PER_CPU(struct pcpu_id_table, pcpu_sids);

/* This variable keeps last used shadow ID on local core.
 * The valid range of shadow ID is [1..255] */
static DEFINE_PER_CPU(unsigned long, pcpu_last_used_sid);

static struct kvmppc_e500_tlb_params host_tlb_params[E500_TLB_NUM];

static struct kvm_book3e_206_tlb_entry *get_entry(
	struct kvmppc_vcpu_e500 *vcpu_e500, int tlbsel, int entry)
{
	int offset = vcpu_e500->gtlb_offset[tlbsel];
	return &vcpu_e500->gtlb_arch[offset + entry];
}

/*
 * Allocate a free shadow id and setup a valid sid mapping in given entry.
 * A mapping is only valid when vcpu_id_table and pcpu_id_table are match.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
static inline int local_sid_setup_one(struct id *entry)
{
	unsigned long sid;
	int ret = -1;

	sid = ++(__get_cpu_var(pcpu_last_used_sid));
	if (sid < NUM_TIDS) {
		__get_cpu_var(pcpu_sids).entry[sid] = entry;
		entry->val = sid;
		entry->pentry = &__get_cpu_var(pcpu_sids).entry[sid];
		ret = sid;
	}

	/*
	 * If sid == NUM_TIDS, we've run out of sids.  We return -1, and
	 * the caller will invalidate everything and start over.
	 *
	 * sid > NUM_TIDS indicates a race, which we disable preemption to
	 * avoid.
	 */
	WARN_ON(sid > NUM_TIDS);

	return ret;
}

/*
 * Check if given entry contain a valid shadow id mapping.
 * An ID mapping is considered valid only if
 * both vcpu and pcpu know this mapping.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
static inline int local_sid_lookup(struct id *entry)
{
	if (entry && entry->val != 0 &&
	    __get_cpu_var(pcpu_sids).entry[entry->val] == entry &&
	    entry->pentry == &__get_cpu_var(pcpu_sids).entry[entry->val])
		return entry->val;
	return -1;
}

/* Invalidate all id mappings on local core -- call with preempt disabled */
static inline void local_sid_destroy_all(void)
{
	__get_cpu_var(pcpu_last_used_sid) = 0;
	memset(&__get_cpu_var(pcpu_sids), 0, sizeof(__get_cpu_var(pcpu_sids)));
}

static void *kvmppc_e500_id_table_alloc(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	vcpu_e500->idt = kzalloc(sizeof(struct vcpu_id_table), GFP_KERNEL);
	return vcpu_e500->idt;
}

static void kvmppc_e500_id_table_free(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	kfree(vcpu_e500->idt);
}

/* Invalidate all mappings on vcpu */
static void kvmppc_e500_id_table_reset_all(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	memset(vcpu_e500->idt, 0, sizeof(struct vcpu_id_table));

	/* Update shadow pid when mappings are changed */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

/* Invalidate one ID mapping on vcpu */
static inline void kvmppc_e500_id_table_reset_one(
			       struct kvmppc_vcpu_e500 *vcpu_e500,
			       int as, int pid, int pr)
{
	struct vcpu_id_table *idt = vcpu_e500->idt;

	BUG_ON(as >= 2);
	BUG_ON(pid >= NUM_TIDS);
	BUG_ON(pr >= 2);

	idt->id[as][pid][pr].val = 0;
	idt->id[as][pid][pr].pentry = NULL;

	/* Update shadow pid when mappings are changed */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

/*
 * Map guest (vcpu,AS,ID,PR) to physical core shadow id.
 * This function first lookup if a valid mapping exists,
 * if not, then creates a new one.
 *
 * The caller must have preemption disabled, and keep it that way until
 * it has finished with the returned shadow id (either written into the
 * TLB or arch.shadow_pid, or discarded).
 */
static unsigned int kvmppc_e500_get_sid(struct kvmppc_vcpu_e500 *vcpu_e500,
					unsigned int as, unsigned int gid,
					unsigned int pr, int avoid_recursion)
{
	struct vcpu_id_table *idt = vcpu_e500->idt;
	int sid;

	BUG_ON(as >= 2);
	BUG_ON(gid >= NUM_TIDS);
	BUG_ON(pr >= 2);

	sid = local_sid_lookup(&idt->id[as][gid][pr]);

	while (sid <= 0) {
		/* No mapping yet */
		sid = local_sid_setup_one(&idt->id[as][gid][pr]);
		if (sid <= 0) {
			_tlbil_all();
			local_sid_destroy_all();
		}

		/* Update shadow pid when mappings are changed */
		if (!avoid_recursion)
			kvmppc_e500_recalc_shadow_pid(vcpu_e500);
	}

	return sid;
}

/* Map guest pid to shadow.
 * We use PID to keep shadow of current guest non-zero PID,
 * and use PID1 to keep shadow of guest zero PID.
 * So that guest tlbe with TID=0 can be accessed at any time */
void kvmppc_e500_recalc_shadow_pid(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	preempt_disable();
	vcpu_e500->vcpu.arch.shadow_pid = kvmppc_e500_get_sid(vcpu_e500,
			get_cur_as(&vcpu_e500->vcpu),
			get_cur_pid(&vcpu_e500->vcpu),
			get_cur_pr(&vcpu_e500->vcpu), 1);
	vcpu_e500->vcpu.arch.shadow_pid1 = kvmppc_e500_get_sid(vcpu_e500,
			get_cur_as(&vcpu_e500->vcpu), 0,
			get_cur_pr(&vcpu_e500->vcpu), 1);
	preempt_enable();
}

static inline unsigned int gtlb0_get_next_victim(
		struct kvmppc_vcpu_e500 *vcpu_e500)
{
	unsigned int victim;

	victim = vcpu_e500->gtlb_nv[0]++;
	if (unlikely(vcpu_e500->gtlb_nv[0] >= vcpu_e500->gtlb_params[0].ways))
		vcpu_e500->gtlb_nv[0] = 0;

	return victim;
}

static inline unsigned int tlb1_max_shadow_size(void)
{
	/* reserve one entry for magic page */
	return host_tlb_params[1].entries - tlbcam_index - 1;
}

static inline int tlbe_is_writable(struct kvm_book3e_206_tlb_entry *tlbe)
{
	return tlbe->mas7_3 & (MAS3_SW|MAS3_UW);
}

static inline u32 e500_shadow_mas3_attrib(u32 mas3, int usermode)
{
	/* Mask off reserved bits. */
	mas3 &= MAS3_ATTRIB_MASK;

	if (!usermode) {
		/* Guest is in supervisor mode,
		 * so we need to translate guest
		 * supervisor permissions into user permissions. */
		mas3 &= ~E500_TLB_USER_PERM_MASK;
		mas3 |= (mas3 & E500_TLB_SUPER_PERM_MASK) << 1;
	}

	return mas3 | E500_TLB_SUPER_PERM_MASK;
}

static inline u32 e500_shadow_mas2_attrib(u32 mas2, int usermode)
{
#ifdef CONFIG_SMP
	return (mas2 & MAS2_ATTRIB_MASK) | MAS2_M;
#else
	return mas2 & MAS2_ATTRIB_MASK;
#endif
}

/*
 * writing shadow tlb entry to host TLB
 */
static inline void __write_host_tlbe(struct kvm_book3e_206_tlb_entry *stlbe,
				     uint32_t mas0)
{
	unsigned long flags;

	local_irq_save(flags);
	mtspr(SPRN_MAS0, mas0);
	mtspr(SPRN_MAS1, stlbe->mas1);
	mtspr(SPRN_MAS2, (unsigned long)stlbe->mas2);
	mtspr(SPRN_MAS3, (u32)stlbe->mas7_3);
	mtspr(SPRN_MAS7, (u32)(stlbe->mas7_3 >> 32));
	asm volatile("isync; tlbwe" : : : "memory");
	local_irq_restore(flags);

	trace_kvm_booke206_stlb_write(mas0, stlbe->mas8, stlbe->mas1,
	                              stlbe->mas2, stlbe->mas7_3);
}

/*
 * Acquire a mas0 with victim hint, as if we just took a TLB miss.
 *
 * We don't care about the address we're searching for, other than that it's
 * in the right set and is not present in the TLB.  Using a zero PID and a
 * userspace address means we don't have to set and then restore MAS5, or
 * calculate a proper MAS6 value.
 */
static u32 get_host_mas0(unsigned long eaddr)
{
	unsigned long flags;
	u32 mas0;

	local_irq_save(flags);
	mtspr(SPRN_MAS6, 0);
	asm volatile("tlbsx 0, %0" : : "b" (eaddr & ~CONFIG_PAGE_OFFSET));
	mas0 = mfspr(SPRN_MAS0);
	local_irq_restore(flags);

	return mas0;
}

/* sesel is for tlb1 only */
static inline void write_host_tlbe(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int sesel, struct kvm_book3e_206_tlb_entry *stlbe)
{
	u32 mas0;

	if (tlbsel == 0) {
		mas0 = get_host_mas0(stlbe->mas2);
		__write_host_tlbe(stlbe, mas0);
	} else {
		__write_host_tlbe(stlbe,
				  MAS0_TLBSEL(1) |
				  MAS0_ESEL(to_htlb1_esel(sesel)));
	}
}

void kvmppc_map_magic(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry magic;
	ulong shared_page = ((ulong)vcpu->arch.shared) & PAGE_MASK;
	unsigned int stid;
	pfn_t pfn;

	pfn = (pfn_t)virt_to_phys((void *)shared_page) >> PAGE_SHIFT;
	get_page(pfn_to_page(pfn));

	preempt_disable();
	stid = kvmppc_e500_get_sid(vcpu_e500, 0, 0, 0, 0);

	magic.mas1 = MAS1_VALID | MAS1_TS | MAS1_TID(stid) |
		     MAS1_TSIZE(BOOK3E_PAGESZ_4K);
	magic.mas2 = vcpu->arch.magic_page_ea | MAS2_M;
	magic.mas7_3 = ((u64)pfn << PAGE_SHIFT) |
		       MAS3_SW | MAS3_SR | MAS3_UW | MAS3_UR;
	magic.mas8 = 0;

	__write_host_tlbe(&magic, MAS0_TLBSEL(1) | MAS0_ESEL(tlbcam_index));
	preempt_enable();
}

void kvmppc_e500_tlb_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	/* Shadow PID may be expired on local core */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

void kvmppc_e500_tlb_put(struct kvm_vcpu *vcpu)
{
}

static void inval_gtlbe_on_host(struct kvmppc_vcpu_e500 *vcpu_e500,
				int tlbsel, int esel)
{
	struct kvm_book3e_206_tlb_entry *gtlbe =
		get_entry(vcpu_e500, tlbsel, esel);
	struct vcpu_id_table *idt = vcpu_e500->idt;
	unsigned int pr, tid, ts, pid;
	u32 val, eaddr;
	unsigned long flags;

	ts = get_tlb_ts(gtlbe);
	tid = get_tlb_tid(gtlbe);

	preempt_disable();

	/* One guest ID may be mapped to two shadow IDs */
	for (pr = 0; pr < 2; pr++) {
		/*
		 * The shadow PID can have a valid mapping on at most one
		 * host CPU.  In the common case, it will be valid on this
		 * CPU, in which case (for TLB0) we do a local invalidation
		 * of the specific address.
		 *
		 * If the shadow PID is not valid on the current host CPU, or
		 * if we're invalidating a TLB1 entry, we invalidate the
		 * entire shadow PID.
		 */
		if (tlbsel == 1 ||
		    (pid = local_sid_lookup(&idt->id[ts][tid][pr])) <= 0) {
			kvmppc_e500_id_table_reset_one(vcpu_e500, ts, tid, pr);
			continue;
		}

		/*
		 * The guest is invalidating a TLB0 entry which is in a PID
		 * that has a valid shadow mapping on this host CPU.  We
		 * search host TLB0 to invalidate it's shadow TLB entry,
		 * similar to __tlbil_va except that we need to look in AS1.
		 */
		val = (pid << MAS6_SPID_SHIFT) | MAS6_SAS;
		eaddr = get_tlb_eaddr(gtlbe);

		local_irq_save(flags);

		mtspr(SPRN_MAS6, val);
		asm volatile("tlbsx 0, %[eaddr]" : : [eaddr] "r" (eaddr));
		val = mfspr(SPRN_MAS1);
		if (val & MAS1_VALID) {
			mtspr(SPRN_MAS1, val & ~MAS1_VALID);
			asm volatile("tlbwe");
		}

		local_irq_restore(flags);
	}

	preempt_enable();
}

static int tlb0_set_base(gva_t addr, int sets, int ways)
{
	int set_base;

	set_base = (addr >> PAGE_SHIFT) & (sets - 1);
	set_base *= ways;

	return set_base;
}

static int gtlb0_set_base(struct kvmppc_vcpu_e500 *vcpu_e500, gva_t addr)
{
	return tlb0_set_base(addr, vcpu_e500->gtlb_params[0].sets,
			     vcpu_e500->gtlb_params[0].ways);
}

static unsigned int get_tlb_esel(struct kvm_vcpu *vcpu, int tlbsel)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int esel = get_tlb_esel_bit(vcpu);

	if (tlbsel == 0) {
		esel &= vcpu_e500->gtlb_params[0].ways - 1;
		esel += gtlb0_set_base(vcpu_e500, vcpu->arch.shared->mas2);
	} else {
		esel &= vcpu_e500->gtlb_params[tlbsel].entries - 1;
	}

	return esel;
}

/* Search the guest TLB for a matching entry. */
static int kvmppc_e500_tlb_index(struct kvmppc_vcpu_e500 *vcpu_e500,
		gva_t eaddr, int tlbsel, unsigned int pid, int as)
{
	int size = vcpu_e500->gtlb_params[tlbsel].entries;
	unsigned int set_base, offset;
	int i;

	if (tlbsel == 0) {
		set_base = gtlb0_set_base(vcpu_e500, eaddr);
		size = vcpu_e500->gtlb_params[0].ways;
	} else {
		set_base = 0;
	}

	offset = vcpu_e500->gtlb_offset[tlbsel];

	for (i = 0; i < size; i++) {
		struct kvm_book3e_206_tlb_entry *tlbe =
			&vcpu_e500->gtlb_arch[offset + set_base + i];
		unsigned int tid;

		if (eaddr < get_tlb_eaddr(tlbe))
			continue;

		if (eaddr > get_tlb_end(tlbe))
			continue;

		tid = get_tlb_tid(tlbe);
		if (tid && (tid != pid))
			continue;

		if (!get_tlb_v(tlbe))
			continue;

		if (get_tlb_ts(tlbe) != as && as != -1)
			continue;

		return set_base + i;
	}

	return -1;
}

static inline void kvmppc_e500_ref_setup(struct tlbe_ref *ref,
					 struct kvm_book3e_206_tlb_entry *gtlbe,
					 pfn_t pfn)
{
	ref->pfn = pfn;
	ref->flags = E500_TLB_VALID;

	if (tlbe_is_writable(gtlbe))
		ref->flags |= E500_TLB_DIRTY;
}

static inline void kvmppc_e500_ref_release(struct tlbe_ref *ref)
{
	if (ref->flags & E500_TLB_VALID) {
		if (ref->flags & E500_TLB_DIRTY)
			kvm_release_pfn_dirty(ref->pfn);
		else
			kvm_release_pfn_clean(ref->pfn);

		ref->flags = 0;
	}
}

static void clear_tlb_privs(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int tlbsel = 0;
	int i;

	for (i = 0; i < vcpu_e500->gtlb_params[tlbsel].entries; i++) {
		struct tlbe_ref *ref =
			&vcpu_e500->gtlb_priv[tlbsel][i].ref;
		kvmppc_e500_ref_release(ref);
	}
}

static void clear_tlb_refs(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int stlbsel = 1;
	int i;

	kvmppc_e500_id_table_reset_all(vcpu_e500);

	for (i = 0; i < host_tlb_params[stlbsel].entries; i++) {
		struct tlbe_ref *ref =
			&vcpu_e500->tlb_refs[stlbsel][i];
		kvmppc_e500_ref_release(ref);
	}

	clear_tlb_privs(vcpu_e500);
}

static inline void kvmppc_e500_deliver_tlb_miss(struct kvm_vcpu *vcpu,
		unsigned int eaddr, int as)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int victim, pidsel, tsized;
	int tlbsel;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (vcpu->arch.shared->mas4 >> 28) & 0x1;
	victim = (tlbsel == 0) ? gtlb0_get_next_victim(vcpu_e500) : 0;
	pidsel = (vcpu->arch.shared->mas4 >> 16) & 0xf;
	tsized = (vcpu->arch.shared->mas4 >> 7) & 0x1f;

	vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(victim)
		| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
	vcpu->arch.shared->mas1 = MAS1_VALID | (as ? MAS1_TS : 0)
		| MAS1_TID(vcpu_e500->pid[pidsel])
		| MAS1_TSIZE(tsized);
	vcpu->arch.shared->mas2 = (eaddr & MAS2_EPN)
		| (vcpu->arch.shared->mas4 & MAS2_ATTRIB_MASK);
	vcpu->arch.shared->mas7_3 &= MAS3_U0 | MAS3_U1 | MAS3_U2 | MAS3_U3;
	vcpu->arch.shared->mas6 = (vcpu->arch.shared->mas6 & MAS6_SPID1)
		| (get_cur_pid(vcpu) << 16)
		| (as ? MAS6_SAS : 0);
}

/* TID must be supplied by the caller */
static inline void kvmppc_e500_setup_stlbe(
	struct kvmppc_vcpu_e500 *vcpu_e500,
	struct kvm_book3e_206_tlb_entry *gtlbe,
	int tsize, struct tlbe_ref *ref, u64 gvaddr,
	struct kvm_book3e_206_tlb_entry *stlbe)
{
	pfn_t pfn = ref->pfn;

	BUG_ON(!(ref->flags & E500_TLB_VALID));

	/* Force TS=1 IPROT=0 for all guest mappings. */
	stlbe->mas1 = MAS1_TSIZE(tsize) | MAS1_TS | MAS1_VALID;
	stlbe->mas2 = (gvaddr & MAS2_EPN)
		| e500_shadow_mas2_attrib(gtlbe->mas2,
				vcpu_e500->vcpu.arch.shared->msr & MSR_PR);
	stlbe->mas7_3 = ((u64)pfn << PAGE_SHIFT)
		| e500_shadow_mas3_attrib(gtlbe->mas7_3,
				vcpu_e500->vcpu.arch.shared->msr & MSR_PR);
}

static inline void kvmppc_e500_shadow_map(struct kvmppc_vcpu_e500 *vcpu_e500,
	u64 gvaddr, gfn_t gfn, struct kvm_book3e_206_tlb_entry *gtlbe,
	int tlbsel, struct kvm_book3e_206_tlb_entry *stlbe,
	struct tlbe_ref *ref)
{
	struct kvm_memory_slot *slot;
	unsigned long pfn, hva;
	int pfnmap = 0;
	int tsize = BOOK3E_PAGESZ_4K;

	/*
	 * Translate guest physical to true physical, acquiring
	 * a page reference if it is normal, non-reserved memory.
	 *
	 * gfn_to_memslot() must succeed because otherwise we wouldn't
	 * have gotten this far.  Eventually we should just pass the slot
	 * pointer through from the first lookup.
	 */
	slot = gfn_to_memslot(vcpu_e500->vcpu.kvm, gfn);
	hva = gfn_to_hva_memslot(slot, gfn);

	if (tlbsel == 1) {
		struct vm_area_struct *vma;
		down_read(&current->mm->mmap_sem);

		vma = find_vma(current->mm, hva);
		if (vma && hva >= vma->vm_start &&
		    (vma->vm_flags & VM_PFNMAP)) {
			/*
			 * This VMA is a physically contiguous region (e.g.
			 * /dev/mem) that bypasses normal Linux page
			 * management.  Find the overlap between the
			 * vma and the memslot.
			 */

			unsigned long start, end;
			unsigned long slot_start, slot_end;

			pfnmap = 1;

			start = vma->vm_pgoff;
			end = start +
			      ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT);

			pfn = start + ((hva - vma->vm_start) >> PAGE_SHIFT);

			slot_start = pfn - (gfn - slot->base_gfn);
			slot_end = slot_start + slot->npages;

			if (start < slot_start)
				start = slot_start;
			if (end > slot_end)
				end = slot_end;

			tsize = (gtlbe->mas1 & MAS1_TSIZE_MASK) >>
				MAS1_TSIZE_SHIFT;

			/*
			 * e500 doesn't implement the lowest tsize bit,
			 * or 1K pages.
			 */
			tsize = max(BOOK3E_PAGESZ_4K, tsize & ~1);

			/*
			 * Now find the largest tsize (up to what the guest
			 * requested) that will cover gfn, stay within the
			 * range, and for which gfn and pfn are mutually
			 * aligned.
			 */

			for (; tsize > BOOK3E_PAGESZ_4K; tsize -= 2) {
				unsigned long gfn_start, gfn_end, tsize_pages;
				tsize_pages = 1 << (tsize - 2);

				gfn_start = gfn & ~(tsize_pages - 1);
				gfn_end = gfn_start + tsize_pages;

				if (gfn_start + pfn - gfn < start)
					continue;
				if (gfn_end + pfn - gfn > end)
					continue;
				if ((gfn & (tsize_pages - 1)) !=
				    (pfn & (tsize_pages - 1)))
					continue;

				gvaddr &= ~((tsize_pages << PAGE_SHIFT) - 1);
				pfn &= ~(tsize_pages - 1);
				break;
			}
		} else if (vma && hva >= vma->vm_start &&
			   (vma->vm_flags & VM_HUGETLB)) {
			unsigned long psize = vma_kernel_pagesize(vma);

			tsize = (gtlbe->mas1 & MAS1_TSIZE_MASK) >>
				MAS1_TSIZE_SHIFT;

			/*
			 * Take the largest page size that satisfies both host
			 * and guest mapping
			 */
			tsize = min(__ilog2(psize) - 10, tsize);

			/*
			 * e500 doesn't implement the lowest tsize bit,
			 * or 1K pages.
			 */
			tsize = max(BOOK3E_PAGESZ_4K, tsize & ~1);
		}

		up_read(&current->mm->mmap_sem);
	}

	if (likely(!pfnmap)) {
		unsigned long tsize_pages = 1 << (tsize + 10 - PAGE_SHIFT);
		pfn = gfn_to_pfn_memslot(vcpu_e500->vcpu.kvm, slot, gfn);
		if (is_error_pfn(pfn)) {
			printk(KERN_ERR "Couldn't get real page for gfn %lx!\n",
					(long)gfn);
			kvm_release_pfn_clean(pfn);
			return;
		}

		/* Align guest and physical address to page map boundaries */
		pfn &= ~(tsize_pages - 1);
		gvaddr &= ~((tsize_pages << PAGE_SHIFT) - 1);
	}

	/* Drop old ref and setup new one. */
	kvmppc_e500_ref_release(ref);
	kvmppc_e500_ref_setup(ref, gtlbe, pfn);

	kvmppc_e500_setup_stlbe(vcpu_e500, gtlbe, tsize, ref, gvaddr, stlbe);
}

/* XXX only map the one-one case, for now use TLB0 */
static void kvmppc_e500_tlb0_map(struct kvmppc_vcpu_e500 *vcpu_e500,
				 int esel,
				 struct kvm_book3e_206_tlb_entry *stlbe)
{
	struct kvm_book3e_206_tlb_entry *gtlbe;
	struct tlbe_ref *ref;

	gtlbe = get_entry(vcpu_e500, 0, esel);
	ref = &vcpu_e500->gtlb_priv[0][esel].ref;

	kvmppc_e500_shadow_map(vcpu_e500, get_tlb_eaddr(gtlbe),
			get_tlb_raddr(gtlbe) >> PAGE_SHIFT,
			gtlbe, 0, stlbe, ref);
}

/* Caller must ensure that the specified guest TLB entry is safe to insert into
 * the shadow TLB. */
/* XXX for both one-one and one-to-many , for now use TLB1 */
static int kvmppc_e500_tlb1_map(struct kvmppc_vcpu_e500 *vcpu_e500,
		u64 gvaddr, gfn_t gfn, struct kvm_book3e_206_tlb_entry *gtlbe,
		struct kvm_book3e_206_tlb_entry *stlbe)
{
	struct tlbe_ref *ref;
	unsigned int victim;

	victim = vcpu_e500->host_tlb1_nv++;

	if (unlikely(vcpu_e500->host_tlb1_nv >= tlb1_max_shadow_size()))
		vcpu_e500->host_tlb1_nv = 0;

	ref = &vcpu_e500->tlb_refs[1][victim];
	kvmppc_e500_shadow_map(vcpu_e500, gvaddr, gfn, gtlbe, 1, stlbe, ref);

	return victim;
}

void kvmppc_mmu_msr_notify(struct kvm_vcpu *vcpu, u32 old_msr)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	/* Recalc shadow pid since MSR changes */
	kvmppc_e500_recalc_shadow_pid(vcpu_e500);
}

static inline int kvmppc_e500_gtlbe_invalidate(
				struct kvmppc_vcpu_e500 *vcpu_e500,
				int tlbsel, int esel)
{
	struct kvm_book3e_206_tlb_entry *gtlbe =
		get_entry(vcpu_e500, tlbsel, esel);

	if (unlikely(get_tlb_iprot(gtlbe)))
		return -1;

	gtlbe->mas1 = 0;

	return 0;
}

int kvmppc_e500_emul_mt_mmucsr0(struct kvmppc_vcpu_e500 *vcpu_e500, ulong value)
{
	int esel;

	if (value & MMUCSR0_TLB0FI)
		for (esel = 0; esel < vcpu_e500->gtlb_params[0].entries; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 0, esel);
	if (value & MMUCSR0_TLB1FI)
		for (esel = 0; esel < vcpu_e500->gtlb_params[1].entries; esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, 1, esel);

	/* Invalidate all vcpu id mappings */
	kvmppc_e500_id_table_reset_all(vcpu_e500);

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbivax(struct kvm_vcpu *vcpu, int ra, int rb)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	unsigned int ia;
	int esel, tlbsel;
	gva_t ea;

	ea = ((ra) ? kvmppc_get_gpr(vcpu, ra) : 0) + kvmppc_get_gpr(vcpu, rb);

	ia = (ea >> 2) & 0x1;

	/* since we only have two TLBs, only lower bit is used. */
	tlbsel = (ea >> 3) & 0x1;

	if (ia) {
		/* invalidate all entries */
		for (esel = 0; esel < vcpu_e500->gtlb_params[tlbsel].entries;
		     esel++)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	} else {
		ea &= 0xfffff000;
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel,
				get_cur_pid(vcpu), -1);
		if (esel >= 0)
			kvmppc_e500_gtlbe_invalidate(vcpu_e500, tlbsel, esel);
	}

	/* Invalidate all vcpu id mappings */
	kvmppc_e500_id_table_reset_all(vcpu_e500);

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbre(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int tlbsel, esel;
	struct kvm_book3e_206_tlb_entry *gtlbe;

	tlbsel = get_tlb_tlbsel(vcpu);
	esel = get_tlb_esel(vcpu, tlbsel);

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);
	vcpu->arch.shared->mas0 &= ~MAS0_NV(~0);
	vcpu->arch.shared->mas0 |= MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
	vcpu->arch.shared->mas1 = gtlbe->mas1;
	vcpu->arch.shared->mas2 = gtlbe->mas2;
	vcpu->arch.shared->mas7_3 = gtlbe->mas7_3;

	return EMULATE_DONE;
}

int kvmppc_e500_emul_tlbsx(struct kvm_vcpu *vcpu, int rb)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int as = !!get_cur_sas(vcpu);
	unsigned int pid = get_cur_spid(vcpu);
	int esel, tlbsel;
	struct kvm_book3e_206_tlb_entry *gtlbe = NULL;
	gva_t ea;

	ea = kvmppc_get_gpr(vcpu, rb);

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, ea, tlbsel, pid, as);
		if (esel >= 0) {
			gtlbe = get_entry(vcpu_e500, tlbsel, esel);
			break;
		}
	}

	if (gtlbe) {
		esel &= vcpu_e500->gtlb_params[tlbsel].ways - 1;

		vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel) | MAS0_ESEL(esel)
			| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
		vcpu->arch.shared->mas1 = gtlbe->mas1;
		vcpu->arch.shared->mas2 = gtlbe->mas2;
		vcpu->arch.shared->mas7_3 = gtlbe->mas7_3;
	} else {
		int victim;

		/* since we only have two TLBs, only lower bit is used. */
		tlbsel = vcpu->arch.shared->mas4 >> 28 & 0x1;
		victim = (tlbsel == 0) ? gtlb0_get_next_victim(vcpu_e500) : 0;

		vcpu->arch.shared->mas0 = MAS0_TLBSEL(tlbsel)
			| MAS0_ESEL(victim)
			| MAS0_NV(vcpu_e500->gtlb_nv[tlbsel]);
		vcpu->arch.shared->mas1 =
			  (vcpu->arch.shared->mas6 & MAS6_SPID0)
			| (vcpu->arch.shared->mas6 & (MAS6_SAS ? MAS1_TS : 0))
			| (vcpu->arch.shared->mas4 & MAS4_TSIZED(~0));
		vcpu->arch.shared->mas2 &= MAS2_EPN;
		vcpu->arch.shared->mas2 |= vcpu->arch.shared->mas4 &
					   MAS2_ATTRIB_MASK;
		vcpu->arch.shared->mas7_3 &= MAS3_U0 | MAS3_U1 |
					     MAS3_U2 | MAS3_U3;
	}

	kvmppc_set_exit_type(vcpu, EMULATED_TLBSX_EXITS);
	return EMULATE_DONE;
}

/* sesel is for tlb1 only */
static void write_stlbe(struct kvmppc_vcpu_e500 *vcpu_e500,
			struct kvm_book3e_206_tlb_entry *gtlbe,
			struct kvm_book3e_206_tlb_entry *stlbe,
			int stlbsel, int sesel)
{
	int stid;

	preempt_disable();
	stid = kvmppc_e500_get_sid(vcpu_e500, get_tlb_ts(gtlbe),
				   get_tlb_tid(gtlbe),
				   get_cur_pr(&vcpu_e500->vcpu), 0);

	stlbe->mas1 |= MAS1_TID(stid);
	write_host_tlbe(vcpu_e500, stlbsel, sesel, stlbe);
	preempt_enable();
}

int kvmppc_e500_emul_tlbwe(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry *gtlbe;
	int tlbsel, esel;

	tlbsel = get_tlb_tlbsel(vcpu);
	esel = get_tlb_esel(vcpu, tlbsel);

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);

	if (get_tlb_v(gtlbe))
		inval_gtlbe_on_host(vcpu_e500, tlbsel, esel);

	gtlbe->mas1 = vcpu->arch.shared->mas1;
	gtlbe->mas2 = vcpu->arch.shared->mas2;
	gtlbe->mas7_3 = vcpu->arch.shared->mas7_3;

	trace_kvm_booke206_gtlb_write(vcpu->arch.shared->mas0, gtlbe->mas1,
	                              gtlbe->mas2, gtlbe->mas7_3);

	/* Invalidate shadow mappings for the about-to-be-clobbered TLBE. */
	if (tlbe_is_host_safe(vcpu, gtlbe)) {
		struct kvm_book3e_206_tlb_entry stlbe;
		int stlbsel, sesel;
		u64 eaddr;
		u64 raddr;

		switch (tlbsel) {
		case 0:
			/* TLB0 */
			gtlbe->mas1 &= ~MAS1_TSIZE(~0);
			gtlbe->mas1 |= MAS1_TSIZE(BOOK3E_PAGESZ_4K);

			stlbsel = 0;
			kvmppc_e500_tlb0_map(vcpu_e500, esel, &stlbe);
			sesel = 0; /* unused */

			break;

		case 1:
			/* TLB1 */
			eaddr = get_tlb_eaddr(gtlbe);
			raddr = get_tlb_raddr(gtlbe);

			/* Create a 4KB mapping on the host.
			 * If the guest wanted a large page,
			 * only the first 4KB is mapped here and the rest
			 * are mapped on the fly. */
			stlbsel = 1;
			sesel = kvmppc_e500_tlb1_map(vcpu_e500, eaddr,
					raddr >> PAGE_SHIFT, gtlbe, &stlbe);
			break;

		default:
			BUG();
		}

		write_stlbe(vcpu_e500, gtlbe, &stlbe, stlbsel, sesel);
	}

	kvmppc_set_exit_type(vcpu, EMULATED_TLBWE_EXITS);
	return EMULATE_DONE;
}

int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_IS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_DS);

	return kvmppc_e500_tlb_search(vcpu, eaddr, get_cur_pid(vcpu), as);
}

void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_IS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.pc, as);
}

void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu)
{
	unsigned int as = !!(vcpu->arch.shared->msr & MSR_DS);

	kvmppc_e500_deliver_tlb_miss(vcpu, vcpu->arch.fault_dear, as);
}

gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int index,
			gva_t eaddr)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry *gtlbe;
	u64 pgmask;

	gtlbe = get_entry(vcpu_e500, tlbsel_of(index), esel_of(index));
	pgmask = get_tlb_bytes(gtlbe) - 1;

	return get_tlb_raddr(gtlbe) | (eaddr & pgmask);
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
}

void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 eaddr, gpa_t gpaddr,
			unsigned int index)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct tlbe_priv *priv;
	struct kvm_book3e_206_tlb_entry *gtlbe, stlbe;
	int tlbsel = tlbsel_of(index);
	int esel = esel_of(index);
	int stlbsel, sesel;

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);

	switch (tlbsel) {
	case 0:
		stlbsel = 0;
		sesel = 0; /* unused */
		priv = &vcpu_e500->gtlb_priv[tlbsel][esel];

		kvmppc_e500_setup_stlbe(vcpu_e500, gtlbe, BOOK3E_PAGESZ_4K,
					&priv->ref, eaddr, &stlbe);
		break;

	case 1: {
		gfn_t gfn = gpaddr >> PAGE_SHIFT;

		stlbsel = 1;
		sesel = kvmppc_e500_tlb1_map(vcpu_e500, eaddr, gfn,
					     gtlbe, &stlbe);
		break;
	}

	default:
		BUG();
		break;
	}

	write_stlbe(vcpu_e500, gtlbe, &stlbe, stlbsel, sesel);
}

int kvmppc_e500_tlb_search(struct kvm_vcpu *vcpu,
				gva_t eaddr, unsigned int pid, int as)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	int esel, tlbsel;

	for (tlbsel = 0; tlbsel < 2; tlbsel++) {
		esel = kvmppc_e500_tlb_index(vcpu_e500, eaddr, tlbsel, pid, as);
		if (esel >= 0)
			return index_of(tlbsel, esel);
	}

	return -1;
}

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	if (vcpu->arch.pid != pid) {
		vcpu_e500->pid[0] = vcpu->arch.pid = pid;
		kvmppc_e500_recalc_shadow_pid(vcpu_e500);
	}
}

void kvmppc_e500_tlb_setup(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	struct kvm_book3e_206_tlb_entry *tlbe;

	/* Insert large initial mapping for guest. */
	tlbe = get_entry(vcpu_e500, 1, 0);
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_256M);
	tlbe->mas2 = 0;
	tlbe->mas7_3 = E500_TLB_SUPER_PERM_MASK;

	/* 4K map for serial output. Used by kernel wrapper. */
	tlbe = get_entry(vcpu_e500, 1, 1);
	tlbe->mas1 = MAS1_VALID | MAS1_TSIZE(BOOK3E_PAGESZ_4K);
	tlbe->mas2 = (0xe0004500 & 0xFFFFF000) | MAS2_I | MAS2_G;
	tlbe->mas7_3 = (0xe0004500 & 0xFFFFF000) | E500_TLB_SUPER_PERM_MASK;
}

static void free_gtlb(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int i;

	clear_tlb_refs(vcpu_e500);
	kfree(vcpu_e500->gtlb_priv[0]);
	kfree(vcpu_e500->gtlb_priv[1]);

	if (vcpu_e500->shared_tlb_pages) {
		vfree((void *)(round_down((uintptr_t)vcpu_e500->gtlb_arch,
					  PAGE_SIZE)));

		for (i = 0; i < vcpu_e500->num_shared_tlb_pages; i++) {
			set_page_dirty_lock(vcpu_e500->shared_tlb_pages[i]);
			put_page(vcpu_e500->shared_tlb_pages[i]);
		}

		vcpu_e500->num_shared_tlb_pages = 0;
		vcpu_e500->shared_tlb_pages = NULL;
	} else {
		kfree(vcpu_e500->gtlb_arch);
	}

	vcpu_e500->gtlb_arch = NULL;
}

int kvm_vcpu_ioctl_config_tlb(struct kvm_vcpu *vcpu,
			      struct kvm_config_tlb *cfg)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_params params;
	char *virt;
	struct page **pages;
	struct tlbe_priv *privs[2] = {};
	size_t array_len;
	u32 sets;
	int num_pages, ret, i;

	if (cfg->mmu_type != KVM_MMU_FSL_BOOKE_NOHV)
		return -EINVAL;

	if (copy_from_user(&params, (void __user *)(uintptr_t)cfg->params,
			   sizeof(params)))
		return -EFAULT;

	if (params.tlb_sizes[1] > 64)
		return -EINVAL;
	if (params.tlb_ways[1] != params.tlb_sizes[1])
		return -EINVAL;
	if (params.tlb_sizes[2] != 0 || params.tlb_sizes[3] != 0)
		return -EINVAL;
	if (params.tlb_ways[2] != 0 || params.tlb_ways[3] != 0)
		return -EINVAL;

	if (!is_power_of_2(params.tlb_ways[0]))
		return -EINVAL;

	sets = params.tlb_sizes[0] >> ilog2(params.tlb_ways[0]);
	if (!is_power_of_2(sets))
		return -EINVAL;

	array_len = params.tlb_sizes[0] + params.tlb_sizes[1];
	array_len *= sizeof(struct kvm_book3e_206_tlb_entry);

	if (cfg->array_len < array_len)
		return -EINVAL;

	num_pages = DIV_ROUND_UP(cfg->array + array_len - 1, PAGE_SIZE) -
		    cfg->array / PAGE_SIZE;
	pages = kmalloc(sizeof(struct page *) * num_pages, GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = get_user_pages_fast(cfg->array, num_pages, 1, pages);
	if (ret < 0)
		goto err_pages;

	if (ret != num_pages) {
		num_pages = ret;
		ret = -EFAULT;
		goto err_put_page;
	}

	virt = vmap(pages, num_pages, VM_MAP, PAGE_KERNEL);
	if (!virt)
		goto err_put_page;

	privs[0] = kzalloc(sizeof(struct tlbe_priv) * params.tlb_sizes[0],
			   GFP_KERNEL);
	privs[1] = kzalloc(sizeof(struct tlbe_priv) * params.tlb_sizes[1],
			   GFP_KERNEL);

	if (!privs[0] || !privs[1])
		goto err_put_page;

	free_gtlb(vcpu_e500);

	vcpu_e500->gtlb_priv[0] = privs[0];
	vcpu_e500->gtlb_priv[1] = privs[1];

	vcpu_e500->gtlb_arch = (struct kvm_book3e_206_tlb_entry *)
		(virt + (cfg->array & (PAGE_SIZE - 1)));

	vcpu_e500->gtlb_params[0].entries = params.tlb_sizes[0];
	vcpu_e500->gtlb_params[1].entries = params.tlb_sizes[1];

	vcpu_e500->gtlb_offset[0] = 0;
	vcpu_e500->gtlb_offset[1] = params.tlb_sizes[0];

	vcpu_e500->tlb0cfg &= ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	if (params.tlb_sizes[0] <= 2048)
		vcpu_e500->tlb0cfg |= params.tlb_sizes[0];
	vcpu_e500->tlb0cfg |= params.tlb_ways[0] << TLBnCFG_ASSOC_SHIFT;

	vcpu_e500->tlb1cfg &= ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu_e500->tlb1cfg |= params.tlb_sizes[1];
	vcpu_e500->tlb1cfg |= params.tlb_ways[1] << TLBnCFG_ASSOC_SHIFT;

	vcpu_e500->shared_tlb_pages = pages;
	vcpu_e500->num_shared_tlb_pages = num_pages;

	vcpu_e500->gtlb_params[0].ways = params.tlb_ways[0];
	vcpu_e500->gtlb_params[0].sets = sets;

	vcpu_e500->gtlb_params[1].ways = params.tlb_sizes[1];
	vcpu_e500->gtlb_params[1].sets = 1;

	return 0;

err_put_page:
	kfree(privs[0]);
	kfree(privs[1]);

	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);

err_pages:
	kfree(pages);
	return ret;
}

int kvm_vcpu_ioctl_dirty_tlb(struct kvm_vcpu *vcpu,
			     struct kvm_dirty_tlb *dirty)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	clear_tlb_refs(vcpu_e500);
	return 0;
}

int kvmppc_e500_tlb_init(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int entry_size = sizeof(struct kvm_book3e_206_tlb_entry);
	int entries = KVM_E500_TLB0_SIZE + KVM_E500_TLB1_SIZE;

	host_tlb_params[0].entries = mfspr(SPRN_TLB0CFG) & TLBnCFG_N_ENTRY;
	host_tlb_params[1].entries = mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY;

	/*
	 * This should never happen on real e500 hardware, but is
	 * architecturally possible -- e.g. in some weird nested
	 * virtualization case.
	 */
	if (host_tlb_params[0].entries == 0 ||
	    host_tlb_params[1].entries == 0) {
		pr_err("%s: need to know host tlb size\n", __func__);
		return -ENODEV;
	}

	host_tlb_params[0].ways = (mfspr(SPRN_TLB0CFG) & TLBnCFG_ASSOC) >>
				  TLBnCFG_ASSOC_SHIFT;
	host_tlb_params[1].ways = host_tlb_params[1].entries;

	if (!is_power_of_2(host_tlb_params[0].entries) ||
	    !is_power_of_2(host_tlb_params[0].ways) ||
	    host_tlb_params[0].entries < host_tlb_params[0].ways ||
	    host_tlb_params[0].ways == 0) {
		pr_err("%s: bad tlb0 host config: %u entries %u ways\n",
		       __func__, host_tlb_params[0].entries,
		       host_tlb_params[0].ways);
		return -ENODEV;
	}

	host_tlb_params[0].sets =
		host_tlb_params[0].entries / host_tlb_params[0].ways;
	host_tlb_params[1].sets = 1;

	vcpu_e500->gtlb_params[0].entries = KVM_E500_TLB0_SIZE;
	vcpu_e500->gtlb_params[1].entries = KVM_E500_TLB1_SIZE;

	vcpu_e500->gtlb_params[0].ways = KVM_E500_TLB0_WAY_NUM;
	vcpu_e500->gtlb_params[0].sets =
		KVM_E500_TLB0_SIZE / KVM_E500_TLB0_WAY_NUM;

	vcpu_e500->gtlb_params[1].ways = KVM_E500_TLB1_SIZE;
	vcpu_e500->gtlb_params[1].sets = 1;

	vcpu_e500->gtlb_arch = kmalloc(entries * entry_size, GFP_KERNEL);
	if (!vcpu_e500->gtlb_arch)
		return -ENOMEM;

	vcpu_e500->gtlb_offset[0] = 0;
	vcpu_e500->gtlb_offset[1] = KVM_E500_TLB0_SIZE;

	vcpu_e500->tlb_refs[0] =
		kzalloc(sizeof(struct tlbe_ref) * host_tlb_params[0].entries,
			GFP_KERNEL);
	if (!vcpu_e500->tlb_refs[0])
		goto err;

	vcpu_e500->tlb_refs[1] =
		kzalloc(sizeof(struct tlbe_ref) * host_tlb_params[1].entries,
			GFP_KERNEL);
	if (!vcpu_e500->tlb_refs[1])
		goto err;

	vcpu_e500->gtlb_priv[0] = kzalloc(sizeof(struct tlbe_ref) *
					  vcpu_e500->gtlb_params[0].entries,
					  GFP_KERNEL);
	if (!vcpu_e500->gtlb_priv[0])
		goto err;

	vcpu_e500->gtlb_priv[1] = kzalloc(sizeof(struct tlbe_ref) *
					  vcpu_e500->gtlb_params[1].entries,
					  GFP_KERNEL);
	if (!vcpu_e500->gtlb_priv[1])
		goto err;

	if (kvmppc_e500_id_table_alloc(vcpu_e500) == NULL)
		goto err;

	/* Init TLB configuration register */
	vcpu_e500->tlb0cfg = mfspr(SPRN_TLB0CFG) &
			     ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu_e500->tlb0cfg |= vcpu_e500->gtlb_params[0].entries;
	vcpu_e500->tlb0cfg |=
		vcpu_e500->gtlb_params[0].ways << TLBnCFG_ASSOC_SHIFT;

	vcpu_e500->tlb1cfg = mfspr(SPRN_TLB1CFG) &
			     ~(TLBnCFG_N_ENTRY | TLBnCFG_ASSOC);
	vcpu_e500->tlb0cfg |= vcpu_e500->gtlb_params[1].entries;
	vcpu_e500->tlb0cfg |=
		vcpu_e500->gtlb_params[1].ways << TLBnCFG_ASSOC_SHIFT;

	return 0;

err:
	free_gtlb(vcpu_e500);
	kfree(vcpu_e500->tlb_refs[0]);
	kfree(vcpu_e500->tlb_refs[1]);
	return -1;
}

void kvmppc_e500_tlb_uninit(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	free_gtlb(vcpu_e500);
	kvmppc_e500_id_table_free(vcpu_e500);

	kfree(vcpu_e500->tlb_refs[0]);
	kfree(vcpu_e500->tlb_refs[1]);
}
