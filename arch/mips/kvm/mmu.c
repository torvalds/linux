/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS MMU handling in the KVM module.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/highmem.h>
#include <linux/kvm_host.h>
#include <asm/mmu_context.h>

static u32 kvm_mips_get_kernel_asid(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();

	return vcpu->arch.guest_kernel_asid[cpu] &
			cpu_asid_mask(&cpu_data[cpu]);
}

static u32 kvm_mips_get_user_asid(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();

	return vcpu->arch.guest_user_asid[cpu] &
			cpu_asid_mask(&cpu_data[cpu]);
}

static int kvm_mips_map_page(struct kvm *kvm, gfn_t gfn)
{
	int srcu_idx, err = 0;
	kvm_pfn_t pfn;

	if (kvm->arch.guest_pmap[gfn] != KVM_INVALID_PAGE)
		return 0;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	pfn = gfn_to_pfn(kvm, gfn);

	if (is_error_pfn(pfn)) {
		kvm_err("Couldn't get pfn for gfn %#llx!\n", gfn);
		err = -EFAULT;
		goto out;
	}

	kvm->arch.guest_pmap[gfn] = pfn;
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return err;
}

/* Translate guest KSEG0 addresses to Host PA */
unsigned long kvm_mips_translate_guest_kseg0_to_hpa(struct kvm_vcpu *vcpu,
						    unsigned long gva)
{
	gfn_t gfn;
	unsigned long offset = gva & ~PAGE_MASK;
	struct kvm *kvm = vcpu->kvm;

	if (KVM_GUEST_KSEGX(gva) != KVM_GUEST_KSEG0) {
		kvm_err("%s/%p: Invalid gva: %#lx\n", __func__,
			__builtin_return_address(0), gva);
		return KVM_INVALID_PAGE;
	}

	gfn = (KVM_GUEST_CPHYSADDR(gva) >> PAGE_SHIFT);

	if (gfn >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: %#llx, GVA: %#lx\n", __func__, gfn,
			gva);
		return KVM_INVALID_PAGE;
	}

	if (kvm_mips_map_page(vcpu->kvm, gfn) < 0)
		return KVM_INVALID_ADDR;

	return (kvm->arch.guest_pmap[gfn] << PAGE_SHIFT) + offset;
}

/* XXXKYMA: Must be called with interrupts disabled */
int kvm_mips_handle_kseg0_tlb_fault(unsigned long badvaddr,
				    struct kvm_vcpu *vcpu)
{
	gfn_t gfn;
	kvm_pfn_t pfn0, pfn1;
	unsigned long vaddr = 0;
	unsigned long entryhi = 0, entrylo0 = 0, entrylo1 = 0;
	struct kvm *kvm = vcpu->kvm;
	const int flush_dcache_mask = 0;
	int ret;

	if (KVM_GUEST_KSEGX(badvaddr) != KVM_GUEST_KSEG0) {
		kvm_err("%s: Invalid BadVaddr: %#lx\n", __func__, badvaddr);
		kvm_mips_dump_host_tlbs();
		return -1;
	}

	gfn = (KVM_GUEST_CPHYSADDR(badvaddr) >> PAGE_SHIFT);
	if ((gfn | 1) >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: %#llx, BadVaddr: %#lx\n", __func__,
			gfn, badvaddr);
		kvm_mips_dump_host_tlbs();
		return -1;
	}
	vaddr = badvaddr & (PAGE_MASK << 1);

	if (kvm_mips_map_page(vcpu->kvm, gfn) < 0)
		return -1;

	if (kvm_mips_map_page(vcpu->kvm, gfn ^ 0x1) < 0)
		return -1;

	pfn0 = kvm->arch.guest_pmap[gfn & ~0x1];
	pfn1 = kvm->arch.guest_pmap[gfn | 0x1];

	entrylo0 = mips3_paddr_to_tlbpfn(pfn0 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		ENTRYLO_D | ENTRYLO_V;
	entrylo1 = mips3_paddr_to_tlbpfn(pfn1 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		ENTRYLO_D | ENTRYLO_V;

	preempt_disable();
	entryhi = (vaddr | kvm_mips_get_kernel_asid(vcpu));
	ret = kvm_mips_host_tlb_write(vcpu, entryhi, entrylo0, entrylo1,
				      flush_dcache_mask);
	preempt_enable();

	return ret;
}

int kvm_mips_handle_mapped_seg_tlb_fault(struct kvm_vcpu *vcpu,
					 struct kvm_mips_tlb *tlb)
{
	unsigned long entryhi = 0, entrylo0 = 0, entrylo1 = 0;
	struct kvm *kvm = vcpu->kvm;
	kvm_pfn_t pfn0, pfn1;
	gfn_t gfn0, gfn1;
	long tlb_lo[2];
	int ret;

	tlb_lo[0] = tlb->tlb_lo[0];
	tlb_lo[1] = tlb->tlb_lo[1];

	/*
	 * The commpage address must not be mapped to anything else if the guest
	 * TLB contains entries nearby, or commpage accesses will break.
	 */
	if (!((tlb->tlb_hi ^ KVM_GUEST_COMMPAGE_ADDR) &
			VPN2_MASK & (PAGE_MASK << 1)))
		tlb_lo[(KVM_GUEST_COMMPAGE_ADDR >> PAGE_SHIFT) & 1] = 0;

	gfn0 = mips3_tlbpfn_to_paddr(tlb_lo[0]) >> PAGE_SHIFT;
	gfn1 = mips3_tlbpfn_to_paddr(tlb_lo[1]) >> PAGE_SHIFT;
	if (gfn0 >= kvm->arch.guest_pmap_npages ||
	    gfn1 >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: [%#llx, %#llx], EHi: %#lx\n",
			__func__, gfn0, gfn1, tlb->tlb_hi);
		kvm_mips_dump_guest_tlbs(vcpu);
		return -1;
	}

	if (kvm_mips_map_page(kvm, gfn0) < 0)
		return -1;

	if (kvm_mips_map_page(kvm, gfn1) < 0)
		return -1;

	pfn0 = kvm->arch.guest_pmap[gfn0];
	pfn1 = kvm->arch.guest_pmap[gfn1];

	/* Get attributes from the Guest TLB */
	entrylo0 = mips3_paddr_to_tlbpfn(pfn0 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		(tlb_lo[0] & ENTRYLO_D) |
		(tlb_lo[0] & ENTRYLO_V);
	entrylo1 = mips3_paddr_to_tlbpfn(pfn1 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		(tlb_lo[1] & ENTRYLO_D) |
		(tlb_lo[1] & ENTRYLO_V);

	kvm_debug("@ %#lx tlb_lo0: 0x%08lx tlb_lo1: 0x%08lx\n", vcpu->arch.pc,
		  tlb->tlb_lo[0], tlb->tlb_lo[1]);

	preempt_disable();
	entryhi = (tlb->tlb_hi & VPN2_MASK) | (KVM_GUEST_KERNEL_MODE(vcpu) ?
					       kvm_mips_get_kernel_asid(vcpu) :
					       kvm_mips_get_user_asid(vcpu));
	ret = kvm_mips_host_tlb_write(vcpu, entryhi, entrylo0, entrylo1,
				      tlb->tlb_mask);
	preempt_enable();

	return ret;
}

void kvm_get_new_mmu_context(struct mm_struct *mm, unsigned long cpu,
			     struct kvm_vcpu *vcpu)
{
	unsigned long asid = asid_cache(cpu);

	asid += cpu_asid_inc();
	if (!(asid & cpu_asid_mask(&cpu_data[cpu]))) {
		if (cpu_has_vtag_icache)
			flush_icache_all();

		kvm_local_flush_tlb_all();      /* start new asid cycle */

		if (!asid)      /* fix version if needed */
			asid = asid_first_version(cpu);
	}

	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}

/**
 * kvm_mips_migrate_count() - Migrate timer.
 * @vcpu:	Virtual CPU.
 *
 * Migrate CP0_Count hrtimer to the current CPU by cancelling and restarting it
 * if it was running prior to being cancelled.
 *
 * Must be called when the VCPU is migrated to a different CPU to ensure that
 * timer expiry during guest execution interrupts the guest and causes the
 * interrupt to be delivered in a timely manner.
 */
static void kvm_mips_migrate_count(struct kvm_vcpu *vcpu)
{
	if (hrtimer_cancel(&vcpu->arch.comparecount_timer))
		hrtimer_restart(&vcpu->arch.comparecount_timer);
}

/* Restore ASID once we are scheduled back after preemption */
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	unsigned long asid_mask = cpu_asid_mask(&cpu_data[cpu]);
	unsigned long flags;
	int newasid = 0;

	kvm_debug("%s: vcpu %p, cpu: %d\n", __func__, vcpu, cpu);

	/* Allocate new kernel and user ASIDs if needed */

	local_irq_save(flags);

	if ((vcpu->arch.guest_kernel_asid[cpu] ^ asid_cache(cpu)) &
						asid_version_mask(cpu)) {
		kvm_get_new_mmu_context(&vcpu->arch.guest_kernel_mm, cpu, vcpu);
		vcpu->arch.guest_kernel_asid[cpu] =
		    vcpu->arch.guest_kernel_mm.context.asid[cpu];
		newasid++;

		kvm_debug("[%d]: cpu_context: %#lx\n", cpu,
			  cpu_context(cpu, current->mm));
		kvm_debug("[%d]: Allocated new ASID for Guest Kernel: %#x\n",
			  cpu, vcpu->arch.guest_kernel_asid[cpu]);
	}

	if ((vcpu->arch.guest_user_asid[cpu] ^ asid_cache(cpu)) &
						asid_version_mask(cpu)) {
		kvm_get_new_mmu_context(&vcpu->arch.guest_user_mm, cpu, vcpu);
		vcpu->arch.guest_user_asid[cpu] =
		    vcpu->arch.guest_user_mm.context.asid[cpu];
		newasid++;

		kvm_debug("[%d]: cpu_context: %#lx\n", cpu,
			  cpu_context(cpu, current->mm));
		kvm_debug("[%d]: Allocated new ASID for Guest User: %#x\n", cpu,
			  vcpu->arch.guest_user_asid[cpu]);
	}

	if (vcpu->arch.last_sched_cpu != cpu) {
		kvm_debug("[%d->%d]KVM VCPU[%d] switch\n",
			  vcpu->arch.last_sched_cpu, cpu, vcpu->vcpu_id);
		/*
		 * Migrate the timer interrupt to the current CPU so that it
		 * always interrupts the guest and synchronously triggers a
		 * guest timer interrupt.
		 */
		kvm_mips_migrate_count(vcpu);
	}

	if (!newasid) {
		/*
		 * If we preempted while the guest was executing, then reload
		 * the pre-empted ASID
		 */
		if (current->flags & PF_VCPU) {
			write_c0_entryhi(vcpu->arch.
					 preempt_entryhi & asid_mask);
			ehb();
		}
	} else {
		/* New ASIDs were allocated for the VM */

		/*
		 * Were we in guest context? If so then the pre-empted ASID is
		 * no longer valid, we need to set it to what it should be based
		 * on the mode of the Guest (Kernel/User)
		 */
		if (current->flags & PF_VCPU) {
			if (KVM_GUEST_KERNEL_MODE(vcpu))
				write_c0_entryhi(vcpu->arch.
						 guest_kernel_asid[cpu] &
						 asid_mask);
			else
				write_c0_entryhi(vcpu->arch.
						 guest_user_asid[cpu] &
						 asid_mask);
			ehb();
		}
	}

	/* restore guest state to registers */
	kvm_mips_callbacks->vcpu_set_regs(vcpu);

	local_irq_restore(flags);

}

/* ASID can change if another task is scheduled during preemption */
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();

	vcpu->arch.preempt_entryhi = read_c0_entryhi();
	vcpu->arch.last_sched_cpu = cpu;

	/* save guest state in registers */
	kvm_mips_callbacks->vcpu_get_regs(vcpu);

	if (((cpu_context(cpu, current->mm) ^ asid_cache(cpu)) &
	     asid_version_mask(cpu))) {
		kvm_debug("%s: Dropping MMU Context:  %#lx\n", __func__,
			  cpu_context(cpu, current->mm));
		drop_mmu_context(current->mm, cpu);
	}
	write_c0_entryhi(cpu_asid(cpu, current->mm));
	ehb();

	local_irq_restore(flags);
}

u32 kvm_get_inst(u32 *opc, struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long paddr, flags, vpn2, asid;
	unsigned long va = (unsigned long)opc;
	void *vaddr;
	u32 inst;
	int index;

	if (KVM_GUEST_KSEGX(va) < KVM_GUEST_KSEG0 ||
	    KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG23) {
		local_irq_save(flags);
		index = kvm_mips_host_tlb_lookup(vcpu, va);
		if (index >= 0) {
			inst = *(opc);
		} else {
			vpn2 = va & VPN2_MASK;
			asid = kvm_read_c0_guest_entryhi(cop0) &
						KVM_ENTRYHI_ASID;
			index = kvm_mips_guest_tlb_lookup(vcpu, vpn2 | asid);
			if (index < 0) {
				kvm_err("%s: get_user_failed for %p, vcpu: %p, ASID: %#lx\n",
					__func__, opc, vcpu, read_c0_entryhi());
				kvm_mips_dump_host_tlbs();
				kvm_mips_dump_guest_tlbs(vcpu);
				local_irq_restore(flags);
				return KVM_INVALID_INST;
			}
			if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu,
						&vcpu->arch.guest_tlb[index])) {
				kvm_err("%s: handling mapped seg tlb fault failed for %p, index: %u, vcpu: %p, ASID: %#lx\n",
					__func__, opc, index, vcpu,
					read_c0_entryhi());
				kvm_mips_dump_guest_tlbs(vcpu);
				local_irq_restore(flags);
				return KVM_INVALID_INST;
			}
			inst = *(opc);
		}
		local_irq_restore(flags);
	} else if (KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG0) {
		paddr = kvm_mips_translate_guest_kseg0_to_hpa(vcpu, va);
		vaddr = kmap_atomic(pfn_to_page(PHYS_PFN(paddr)));
		vaddr += paddr & ~PAGE_MASK;
		inst = *(u32 *)vaddr;
		kunmap_atomic(vaddr);
	} else {
		kvm_err("%s: illegal address: %p\n", __func__, opc);
		return KVM_INVALID_INST;
	}

	return inst;
}
