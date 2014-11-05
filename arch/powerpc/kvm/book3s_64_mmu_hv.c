/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright 2010 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>
#include <linux/srcu.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/cputable.h>

/* POWER7 has 10-bit LPIDs, PPC970 has 6-bit LPIDs */
#define MAX_LPID_970	63

/* Power architecture requires HPT is at least 256kB */
#define PPC_MIN_HPT_ORDER	18

static long kvmppc_virtmode_do_h_enter(struct kvm *kvm, unsigned long flags,
				long pte_index, unsigned long pteh,
				unsigned long ptel, unsigned long *pte_idx_ret);
static void kvmppc_rmap_reset(struct kvm *kvm);

long kvmppc_alloc_hpt(struct kvm *kvm, u32 *htab_orderp)
{
	unsigned long hpt = 0;
	struct revmap_entry *rev;
	struct page *page = NULL;
	long order = KVM_DEFAULT_HPT_ORDER;

	if (htab_orderp) {
		order = *htab_orderp;
		if (order < PPC_MIN_HPT_ORDER)
			order = PPC_MIN_HPT_ORDER;
	}

	kvm->arch.hpt_cma_alloc = 0;
	page = kvm_alloc_hpt(1ul << (order - PAGE_SHIFT));
	if (page) {
		hpt = (unsigned long)pfn_to_kaddr(page_to_pfn(page));
		memset((void *)hpt, 0, (1ul << order));
		kvm->arch.hpt_cma_alloc = 1;
	}

	/* Lastly try successively smaller sizes from the page allocator */
	while (!hpt && order > PPC_MIN_HPT_ORDER) {
		hpt = __get_free_pages(GFP_KERNEL|__GFP_ZERO|__GFP_REPEAT|
				       __GFP_NOWARN, order - PAGE_SHIFT);
		if (!hpt)
			--order;
	}

	if (!hpt)
		return -ENOMEM;

	kvm->arch.hpt_virt = hpt;
	kvm->arch.hpt_order = order;
	/* HPTEs are 2**4 bytes long */
	kvm->arch.hpt_npte = 1ul << (order - 4);
	/* 128 (2**7) bytes in each HPTEG */
	kvm->arch.hpt_mask = (1ul << (order - 7)) - 1;

	/* Allocate reverse map array */
	rev = vmalloc(sizeof(struct revmap_entry) * kvm->arch.hpt_npte);
	if (!rev) {
		pr_err("kvmppc_alloc_hpt: Couldn't alloc reverse map array\n");
		goto out_freehpt;
	}
	kvm->arch.revmap = rev;
	kvm->arch.sdr1 = __pa(hpt) | (order - 18);

	pr_info("KVM guest htab at %lx (order %ld), LPID %x\n",
		hpt, order, kvm->arch.lpid);

	if (htab_orderp)
		*htab_orderp = order;
	return 0;

 out_freehpt:
	if (kvm->arch.hpt_cma_alloc)
		kvm_release_hpt(page, 1 << (order - PAGE_SHIFT));
	else
		free_pages(hpt, order - PAGE_SHIFT);
	return -ENOMEM;
}

long kvmppc_alloc_reset_hpt(struct kvm *kvm, u32 *htab_orderp)
{
	long err = -EBUSY;
	long order;

	mutex_lock(&kvm->lock);
	if (kvm->arch.rma_setup_done) {
		kvm->arch.rma_setup_done = 0;
		/* order rma_setup_done vs. vcpus_running */
		smp_mb();
		if (atomic_read(&kvm->arch.vcpus_running)) {
			kvm->arch.rma_setup_done = 1;
			goto out;
		}
	}
	if (kvm->arch.hpt_virt) {
		order = kvm->arch.hpt_order;
		/* Set the entire HPT to 0, i.e. invalid HPTEs */
		memset((void *)kvm->arch.hpt_virt, 0, 1ul << order);
		/*
		 * Reset all the reverse-mapping chains for all memslots
		 */
		kvmppc_rmap_reset(kvm);
		/* Ensure that each vcpu will flush its TLB on next entry. */
		cpumask_setall(&kvm->arch.need_tlb_flush);
		*htab_orderp = order;
		err = 0;
	} else {
		err = kvmppc_alloc_hpt(kvm, htab_orderp);
		order = *htab_orderp;
	}
 out:
	mutex_unlock(&kvm->lock);
	return err;
}

void kvmppc_free_hpt(struct kvm *kvm)
{
	kvmppc_free_lpid(kvm->arch.lpid);
	vfree(kvm->arch.revmap);
	if (kvm->arch.hpt_cma_alloc)
		kvm_release_hpt(virt_to_page(kvm->arch.hpt_virt),
				1 << (kvm->arch.hpt_order - PAGE_SHIFT));
	else
		free_pages(kvm->arch.hpt_virt,
			   kvm->arch.hpt_order - PAGE_SHIFT);
}

/* Bits in first HPTE dword for pagesize 4k, 64k or 16M */
static inline unsigned long hpte0_pgsize_encoding(unsigned long pgsize)
{
	return (pgsize > 0x1000) ? HPTE_V_LARGE : 0;
}

/* Bits in second HPTE dword for pagesize 4k, 64k or 16M */
static inline unsigned long hpte1_pgsize_encoding(unsigned long pgsize)
{
	return (pgsize == 0x10000) ? 0x1000 : 0;
}

void kvmppc_map_vrma(struct kvm_vcpu *vcpu, struct kvm_memory_slot *memslot,
		     unsigned long porder)
{
	unsigned long i;
	unsigned long npages;
	unsigned long hp_v, hp_r;
	unsigned long addr, hash;
	unsigned long psize;
	unsigned long hp0, hp1;
	unsigned long idx_ret;
	long ret;
	struct kvm *kvm = vcpu->kvm;

	psize = 1ul << porder;
	npages = memslot->npages >> (porder - PAGE_SHIFT);

	/* VRMA can't be > 1TB */
	if (npages > 1ul << (40 - porder))
		npages = 1ul << (40 - porder);
	/* Can't use more than 1 HPTE per HPTEG */
	if (npages > kvm->arch.hpt_mask + 1)
		npages = kvm->arch.hpt_mask + 1;

	hp0 = HPTE_V_1TB_SEG | (VRMA_VSID << (40 - 16)) |
		HPTE_V_BOLTED | hpte0_pgsize_encoding(psize);
	hp1 = hpte1_pgsize_encoding(psize) |
		HPTE_R_R | HPTE_R_C | HPTE_R_M | PP_RWXX;

	for (i = 0; i < npages; ++i) {
		addr = i << porder;
		/* can't use hpt_hash since va > 64 bits */
		hash = (i ^ (VRMA_VSID ^ (VRMA_VSID << 25))) & kvm->arch.hpt_mask;
		/*
		 * We assume that the hash table is empty and no
		 * vcpus are using it at this stage.  Since we create
		 * at most one HPTE per HPTEG, we just assume entry 7
		 * is available and use it.
		 */
		hash = (hash << 3) + 7;
		hp_v = hp0 | ((addr >> 16) & ~0x7fUL);
		hp_r = hp1 | addr;
		ret = kvmppc_virtmode_do_h_enter(kvm, H_EXACT, hash, hp_v, hp_r,
						 &idx_ret);
		if (ret != H_SUCCESS) {
			pr_err("KVM: map_vrma at %lx failed, ret=%ld\n",
			       addr, ret);
			break;
		}
	}
}

int kvmppc_mmu_hv_init(void)
{
	unsigned long host_lpid, rsvd_lpid;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return -EINVAL;

	/* POWER7 has 10-bit LPIDs, PPC970 and e500mc have 6-bit LPIDs */
	if (cpu_has_feature(CPU_FTR_ARCH_206)) {
		host_lpid = mfspr(SPRN_LPID);	/* POWER7 */
		rsvd_lpid = LPID_RSVD;
	} else {
		host_lpid = 0;			/* PPC970 */
		rsvd_lpid = MAX_LPID_970;
	}

	kvmppc_init_lpid(rsvd_lpid + 1);

	kvmppc_claim_lpid(host_lpid);
	/* rsvd_lpid is reserved for use in partition switching */
	kvmppc_claim_lpid(rsvd_lpid);

	return 0;
}

static void kvmppc_mmu_book3s_64_hv_reset_msr(struct kvm_vcpu *vcpu)
{
	unsigned long msr = vcpu->arch.intr_msr;

	/* If transactional, change to suspend mode on IRQ delivery */
	if (MSR_TM_TRANSACTIONAL(vcpu->arch.shregs.msr))
		msr |= MSR_TS_S;
	else
		msr |= vcpu->arch.shregs.msr & MSR_TS_MASK;
	kvmppc_set_msr(vcpu, msr);
}

/*
 * This is called to get a reference to a guest page if there isn't
 * one already in the memslot->arch.slot_phys[] array.
 */
static long kvmppc_get_guest_page(struct kvm *kvm, unsigned long gfn,
				  struct kvm_memory_slot *memslot,
				  unsigned long psize)
{
	unsigned long start;
	long np, err;
	struct page *page, *hpage, *pages[1];
	unsigned long s, pgsize;
	unsigned long *physp;
	unsigned int is_io, got, pgorder;
	struct vm_area_struct *vma;
	unsigned long pfn, i, npages;

	physp = memslot->arch.slot_phys;
	if (!physp)
		return -EINVAL;
	if (physp[gfn - memslot->base_gfn])
		return 0;

	is_io = 0;
	got = 0;
	page = NULL;
	pgsize = psize;
	err = -EINVAL;
	start = gfn_to_hva_memslot(memslot, gfn);

	/* Instantiate and get the page we want access to */
	np = get_user_pages_fast(start, 1, 1, pages);
	if (np != 1) {
		/* Look up the vma for the page */
		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, start);
		if (!vma || vma->vm_start > start ||
		    start + psize > vma->vm_end ||
		    !(vma->vm_flags & VM_PFNMAP))
			goto up_err;
		is_io = hpte_cache_bits(pgprot_val(vma->vm_page_prot));
		pfn = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
		/* check alignment of pfn vs. requested page size */
		if (psize > PAGE_SIZE && (pfn & ((psize >> PAGE_SHIFT) - 1)))
			goto up_err;
		up_read(&current->mm->mmap_sem);

	} else {
		page = pages[0];
		got = KVMPPC_GOT_PAGE;

		/* See if this is a large page */
		s = PAGE_SIZE;
		if (PageHuge(page)) {
			hpage = compound_head(page);
			s <<= compound_order(hpage);
			/* Get the whole large page if slot alignment is ok */
			if (s > psize && slot_is_aligned(memslot, s) &&
			    !(memslot->userspace_addr & (s - 1))) {
				start &= ~(s - 1);
				pgsize = s;
				get_page(hpage);
				put_page(page);
				page = hpage;
			}
		}
		if (s < psize)
			goto out;
		pfn = page_to_pfn(page);
	}

	npages = pgsize >> PAGE_SHIFT;
	pgorder = __ilog2(npages);
	physp += (gfn - memslot->base_gfn) & ~(npages - 1);
	spin_lock(&kvm->arch.slot_phys_lock);
	for (i = 0; i < npages; ++i) {
		if (!physp[i]) {
			physp[i] = ((pfn + i) << PAGE_SHIFT) +
				got + is_io + pgorder;
			got = 0;
		}
	}
	spin_unlock(&kvm->arch.slot_phys_lock);
	err = 0;

 out:
	if (got)
		put_page(page);
	return err;

 up_err:
	up_read(&current->mm->mmap_sem);
	return err;
}

long kvmppc_virtmode_do_h_enter(struct kvm *kvm, unsigned long flags,
				long pte_index, unsigned long pteh,
				unsigned long ptel, unsigned long *pte_idx_ret)
{
	unsigned long psize, gpa, gfn;
	struct kvm_memory_slot *memslot;
	long ret;

	if (kvm->arch.using_mmu_notifiers)
		goto do_insert;

	psize = hpte_page_size(pteh, ptel);
	if (!psize)
		return H_PARAMETER;

	pteh &= ~(HPTE_V_HVLOCK | HPTE_V_ABSENT | HPTE_V_VALID);

	/* Find the memslot (if any) for this address */
	gpa = (ptel & HPTE_R_RPN) & ~(psize - 1);
	gfn = gpa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(kvm, gfn);
	if (memslot && !(memslot->flags & KVM_MEMSLOT_INVALID)) {
		if (!slot_is_aligned(memslot, psize))
			return H_PARAMETER;
		if (kvmppc_get_guest_page(kvm, gfn, memslot, psize) < 0)
			return H_PARAMETER;
	}

 do_insert:
	/* Protect linux PTE lookup from page table destruction */
	rcu_read_lock_sched();	/* this disables preemption too */
	ret = kvmppc_do_h_enter(kvm, flags, pte_index, pteh, ptel,
				current->mm->pgd, false, pte_idx_ret);
	rcu_read_unlock_sched();
	if (ret == H_TOO_HARD) {
		/* this can't happen */
		pr_err("KVM: Oops, kvmppc_h_enter returned too hard!\n");
		ret = H_RESOURCE;	/* or something */
	}
	return ret;

}

/*
 * We come here on a H_ENTER call from the guest when we are not
 * using mmu notifiers and we don't have the requested page pinned
 * already.
 */
long kvmppc_virtmode_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
			     long pte_index, unsigned long pteh,
			     unsigned long ptel)
{
	return kvmppc_virtmode_do_h_enter(vcpu->kvm, flags, pte_index,
					  pteh, ptel, &vcpu->arch.gpr[4]);
}

static struct kvmppc_slb *kvmppc_mmu_book3s_hv_find_slbe(struct kvm_vcpu *vcpu,
							 gva_t eaddr)
{
	u64 mask;
	int i;

	for (i = 0; i < vcpu->arch.slb_nr; i++) {
		if (!(vcpu->arch.slb[i].orige & SLB_ESID_V))
			continue;

		if (vcpu->arch.slb[i].origv & SLB_VSID_B_1T)
			mask = ESID_MASK_1T;
		else
			mask = ESID_MASK;

		if (((vcpu->arch.slb[i].orige ^ eaddr) & mask) == 0)
			return &vcpu->arch.slb[i];
	}
	return NULL;
}

static unsigned long kvmppc_mmu_get_real_addr(unsigned long v, unsigned long r,
			unsigned long ea)
{
	unsigned long ra_mask;

	ra_mask = hpte_page_size(v, r) - 1;
	return (r & HPTE_R_RPN & ~ra_mask) | (ea & ra_mask);
}

static int kvmppc_mmu_book3s_64_hv_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
			struct kvmppc_pte *gpte, bool data, bool iswrite)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_slb *slbe;
	unsigned long slb_v;
	unsigned long pp, key;
	unsigned long v, gr;
	__be64 *hptep;
	int index;
	int virtmode = vcpu->arch.shregs.msr & (data ? MSR_DR : MSR_IR);

	/* Get SLB entry */
	if (virtmode) {
		slbe = kvmppc_mmu_book3s_hv_find_slbe(vcpu, eaddr);
		if (!slbe)
			return -EINVAL;
		slb_v = slbe->origv;
	} else {
		/* real mode access */
		slb_v = vcpu->kvm->arch.vrma_slb_v;
	}

	preempt_disable();
	/* Find the HPTE in the hash table */
	index = kvmppc_hv_find_lock_hpte(kvm, eaddr, slb_v,
					 HPTE_V_VALID | HPTE_V_ABSENT);
	if (index < 0) {
		preempt_enable();
		return -ENOENT;
	}
	hptep = (__be64 *)(kvm->arch.hpt_virt + (index << 4));
	v = be64_to_cpu(hptep[0]) & ~HPTE_V_HVLOCK;
	gr = kvm->arch.revmap[index].guest_rpte;

	/* Unlock the HPTE */
	asm volatile("lwsync" : : : "memory");
	hptep[0] = cpu_to_be64(v);
	preempt_enable();

	gpte->eaddr = eaddr;
	gpte->vpage = ((v & HPTE_V_AVPN) << 4) | ((eaddr >> 12) & 0xfff);

	/* Get PP bits and key for permission check */
	pp = gr & (HPTE_R_PP0 | HPTE_R_PP);
	key = (vcpu->arch.shregs.msr & MSR_PR) ? SLB_VSID_KP : SLB_VSID_KS;
	key &= slb_v;

	/* Calculate permissions */
	gpte->may_read = hpte_read_permission(pp, key);
	gpte->may_write = hpte_write_permission(pp, key);
	gpte->may_execute = gpte->may_read && !(gr & (HPTE_R_N | HPTE_R_G));

	/* Storage key permission check for POWER7 */
	if (data && virtmode && cpu_has_feature(CPU_FTR_ARCH_206)) {
		int amrfield = hpte_get_skey_perm(gr, vcpu->arch.amr);
		if (amrfield & 1)
			gpte->may_read = 0;
		if (amrfield & 2)
			gpte->may_write = 0;
	}

	/* Get the guest physical address */
	gpte->raddr = kvmppc_mmu_get_real_addr(v, gr, eaddr);
	return 0;
}

/*
 * Quick test for whether an instruction is a load or a store.
 * If the instruction is a load or a store, then this will indicate
 * which it is, at least on server processors.  (Embedded processors
 * have some external PID instructions that don't follow the rule
 * embodied here.)  If the instruction isn't a load or store, then
 * this doesn't return anything useful.
 */
static int instruction_is_store(unsigned int instr)
{
	unsigned int mask;

	mask = 0x10000000;
	if ((instr & 0xfc000000) == 0x7c000000)
		mask = 0x100;		/* major opcode 31 */
	return (instr & mask) != 0;
}

static int kvmppc_hv_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu,
				  unsigned long gpa, gva_t ea, int is_store)
{
	u32 last_inst;

	/*
	 * If we fail, we just return to the guest and try executing it again.
	 */
	if (kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst) !=
		EMULATE_DONE)
		return RESUME_GUEST;

	/*
	 * WARNING: We do not know for sure whether the instruction we just
	 * read from memory is the same that caused the fault in the first
	 * place.  If the instruction we read is neither an load or a store,
	 * then it can't access memory, so we don't need to worry about
	 * enforcing access permissions.  So, assuming it is a load or
	 * store, we just check that its direction (load or store) is
	 * consistent with the original fault, since that's what we
	 * checked the access permissions against.  If there is a mismatch
	 * we just return and retry the instruction.
	 */

	if (instruction_is_store(last_inst) != !!is_store)
		return RESUME_GUEST;

	/*
	 * Emulated accesses are emulated by looking at the hash for
	 * translation once, then performing the access later. The
	 * translation could be invalidated in the meantime in which
	 * point performing the subsequent memory access on the old
	 * physical address could possibly be a security hole for the
	 * guest (but not the host).
	 *
	 * This is less of an issue for MMIO stores since they aren't
	 * globally visible. It could be an issue for MMIO loads to
	 * a certain extent but we'll ignore it for now.
	 */

	vcpu->arch.paddr_accessed = gpa;
	vcpu->arch.vaddr_accessed = ea;
	return kvmppc_emulate_mmio(run, vcpu);
}

int kvmppc_book3s_hv_page_fault(struct kvm_run *run, struct kvm_vcpu *vcpu,
				unsigned long ea, unsigned long dsisr)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long hpte[3], r;
	__be64 *hptep;
	unsigned long mmu_seq, psize, pte_size;
	unsigned long gpa_base, gfn_base;
	unsigned long gpa, gfn, hva, pfn;
	struct kvm_memory_slot *memslot;
	unsigned long *rmap;
	struct revmap_entry *rev;
	struct page *page, *pages[1];
	long index, ret, npages;
	unsigned long is_io;
	unsigned int writing, write_ok;
	struct vm_area_struct *vma;
	unsigned long rcbits;

	/*
	 * Real-mode code has already searched the HPT and found the
	 * entry we're interested in.  Lock the entry and check that
	 * it hasn't changed.  If it has, just return and re-execute the
	 * instruction.
	 */
	if (ea != vcpu->arch.pgfault_addr)
		return RESUME_GUEST;
	index = vcpu->arch.pgfault_index;
	hptep = (__be64 *)(kvm->arch.hpt_virt + (index << 4));
	rev = &kvm->arch.revmap[index];
	preempt_disable();
	while (!try_lock_hpte(hptep, HPTE_V_HVLOCK))
		cpu_relax();
	hpte[0] = be64_to_cpu(hptep[0]) & ~HPTE_V_HVLOCK;
	hpte[1] = be64_to_cpu(hptep[1]);
	hpte[2] = r = rev->guest_rpte;
	asm volatile("lwsync" : : : "memory");
	hptep[0] = cpu_to_be64(hpte[0]);
	preempt_enable();

	if (hpte[0] != vcpu->arch.pgfault_hpte[0] ||
	    hpte[1] != vcpu->arch.pgfault_hpte[1])
		return RESUME_GUEST;

	/* Translate the logical address and get the page */
	psize = hpte_page_size(hpte[0], r);
	gpa_base = r & HPTE_R_RPN & ~(psize - 1);
	gfn_base = gpa_base >> PAGE_SHIFT;
	gpa = gpa_base | (ea & (psize - 1));
	gfn = gpa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(kvm, gfn);

	/* No memslot means it's an emulated MMIO region */
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		return kvmppc_hv_emulate_mmio(run, vcpu, gpa, ea,
					      dsisr & DSISR_ISSTORE);

	if (!kvm->arch.using_mmu_notifiers)
		return -EFAULT;		/* should never get here */

	/*
	 * This should never happen, because of the slot_is_aligned()
	 * check in kvmppc_do_h_enter().
	 */
	if (gfn_base < memslot->base_gfn)
		return -EFAULT;

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	is_io = 0;
	pfn = 0;
	page = NULL;
	pte_size = PAGE_SIZE;
	writing = (dsisr & DSISR_ISSTORE) != 0;
	/* If writing != 0, then the HPTE must allow writing, if we get here */
	write_ok = writing;
	hva = gfn_to_hva_memslot(memslot, gfn);
	npages = get_user_pages_fast(hva, 1, writing, pages);
	if (npages < 1) {
		/* Check if it's an I/O mapping */
		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, hva);
		if (vma && vma->vm_start <= hva && hva + psize <= vma->vm_end &&
		    (vma->vm_flags & VM_PFNMAP)) {
			pfn = vma->vm_pgoff +
				((hva - vma->vm_start) >> PAGE_SHIFT);
			pte_size = psize;
			is_io = hpte_cache_bits(pgprot_val(vma->vm_page_prot));
			write_ok = vma->vm_flags & VM_WRITE;
		}
		up_read(&current->mm->mmap_sem);
		if (!pfn)
			return -EFAULT;
	} else {
		page = pages[0];
		pfn = page_to_pfn(page);
		if (PageHuge(page)) {
			page = compound_head(page);
			pte_size <<= compound_order(page);
		}
		/* if the guest wants write access, see if that is OK */
		if (!writing && hpte_is_writable(r)) {
			unsigned int hugepage_shift;
			pte_t *ptep, pte;

			/*
			 * We need to protect against page table destruction
			 * while looking up and updating the pte.
			 */
			rcu_read_lock_sched();
			ptep = find_linux_pte_or_hugepte(current->mm->pgd,
							 hva, &hugepage_shift);
			if (ptep) {
				pte = kvmppc_read_update_linux_pte(ptep, 1,
							   hugepage_shift);
				if (pte_write(pte))
					write_ok = 1;
			}
			rcu_read_unlock_sched();
		}
	}

	ret = -EFAULT;
	if (psize > pte_size)
		goto out_put;

	/* Check WIMG vs. the actual page we're accessing */
	if (!hpte_cache_flags_ok(r, is_io)) {
		if (is_io)
			return -EFAULT;
		/*
		 * Allow guest to map emulated device memory as
		 * uncacheable, but actually make it cacheable.
		 */
		r = (r & ~(HPTE_R_W|HPTE_R_I|HPTE_R_G)) | HPTE_R_M;
	}

	/*
	 * Set the HPTE to point to pfn.
	 * Since the pfn is at PAGE_SIZE granularity, make sure we
	 * don't mask out lower-order bits if psize < PAGE_SIZE.
	 */
	if (psize < PAGE_SIZE)
		psize = PAGE_SIZE;
	r = (r & ~(HPTE_R_PP0 - psize)) | ((pfn << PAGE_SHIFT) & ~(psize - 1));
	if (hpte_is_writable(r) && !write_ok)
		r = hpte_make_readonly(r);
	ret = RESUME_GUEST;
	preempt_disable();
	while (!try_lock_hpte(hptep, HPTE_V_HVLOCK))
		cpu_relax();
	if ((be64_to_cpu(hptep[0]) & ~HPTE_V_HVLOCK) != hpte[0] ||
		be64_to_cpu(hptep[1]) != hpte[1] ||
		rev->guest_rpte != hpte[2])
		/* HPTE has been changed under us; let the guest retry */
		goto out_unlock;
	hpte[0] = (hpte[0] & ~HPTE_V_ABSENT) | HPTE_V_VALID;

	/* Always put the HPTE in the rmap chain for the page base address */
	rmap = &memslot->arch.rmap[gfn_base - memslot->base_gfn];
	lock_rmap(rmap);

	/* Check if we might have been invalidated; let the guest retry if so */
	ret = RESUME_GUEST;
	if (mmu_notifier_retry(vcpu->kvm, mmu_seq)) {
		unlock_rmap(rmap);
		goto out_unlock;
	}

	/* Only set R/C in real HPTE if set in both *rmap and guest_rpte */
	rcbits = *rmap >> KVMPPC_RMAP_RC_SHIFT;
	r &= rcbits | ~(HPTE_R_R | HPTE_R_C);

	if (be64_to_cpu(hptep[0]) & HPTE_V_VALID) {
		/* HPTE was previously valid, so we need to invalidate it */
		unlock_rmap(rmap);
		hptep[0] |= cpu_to_be64(HPTE_V_ABSENT);
		kvmppc_invalidate_hpte(kvm, hptep, index);
		/* don't lose previous R and C bits */
		r |= be64_to_cpu(hptep[1]) & (HPTE_R_R | HPTE_R_C);
	} else {
		kvmppc_add_revmap_chain(kvm, rev, rmap, index, 0);
	}

	hptep[1] = cpu_to_be64(r);
	eieio();
	hptep[0] = cpu_to_be64(hpte[0]);
	asm volatile("ptesync" : : : "memory");
	preempt_enable();
	if (page && hpte_is_writable(r))
		SetPageDirty(page);

 out_put:
	if (page) {
		/*
		 * We drop pages[0] here, not page because page might
		 * have been set to the head page of a compound, but
		 * we have to drop the reference on the correct tail
		 * page to match the get inside gup()
		 */
		put_page(pages[0]);
	}
	return ret;

 out_unlock:
	hptep[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
	preempt_enable();
	goto out_put;
}

static void kvmppc_rmap_reset(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	slots = kvm->memslots;
	kvm_for_each_memslot(memslot, slots) {
		/*
		 * This assumes it is acceptable to lose reference and
		 * change bits across a reset.
		 */
		memset(memslot->arch.rmap, 0,
		       memslot->npages * sizeof(*memslot->arch.rmap));
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
}

static int kvm_handle_hva_range(struct kvm *kvm,
				unsigned long start,
				unsigned long end,
				int (*handler)(struct kvm *kvm,
					       unsigned long *rmapp,
					       unsigned long gfn))
{
	int ret;
	int retval = 0;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gfn, gfn_end;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;
		/*
		 * {gfn(page) | page intersects with [hva_start, hva_end)} =
		 * {gfn, gfn+1, ..., gfn_end-1}.
		 */
		gfn = hva_to_gfn_memslot(hva_start, memslot);
		gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);

		for (; gfn < gfn_end; ++gfn) {
			gfn_t gfn_offset = gfn - memslot->base_gfn;

			ret = handler(kvm, &memslot->arch.rmap[gfn_offset], gfn);
			retval |= ret;
		}
	}

	return retval;
}

static int kvm_handle_hva(struct kvm *kvm, unsigned long hva,
			  int (*handler)(struct kvm *kvm, unsigned long *rmapp,
					 unsigned long gfn))
{
	return kvm_handle_hva_range(kvm, hva, hva + 1, handler);
}

static int kvm_unmap_rmapp(struct kvm *kvm, unsigned long *rmapp,
			   unsigned long gfn)
{
	struct revmap_entry *rev = kvm->arch.revmap;
	unsigned long h, i, j;
	__be64 *hptep;
	unsigned long ptel, psize, rcbits;

	for (;;) {
		lock_rmap(rmapp);
		if (!(*rmapp & KVMPPC_RMAP_PRESENT)) {
			unlock_rmap(rmapp);
			break;
		}

		/*
		 * To avoid an ABBA deadlock with the HPTE lock bit,
		 * we can't spin on the HPTE lock while holding the
		 * rmap chain lock.
		 */
		i = *rmapp & KVMPPC_RMAP_INDEX;
		hptep = (__be64 *) (kvm->arch.hpt_virt + (i << 4));
		if (!try_lock_hpte(hptep, HPTE_V_HVLOCK)) {
			/* unlock rmap before spinning on the HPTE lock */
			unlock_rmap(rmapp);
			while (be64_to_cpu(hptep[0]) & HPTE_V_HVLOCK)
				cpu_relax();
			continue;
		}
		j = rev[i].forw;
		if (j == i) {
			/* chain is now empty */
			*rmapp &= ~(KVMPPC_RMAP_PRESENT | KVMPPC_RMAP_INDEX);
		} else {
			/* remove i from chain */
			h = rev[i].back;
			rev[h].forw = j;
			rev[j].back = h;
			rev[i].forw = rev[i].back = i;
			*rmapp = (*rmapp & ~KVMPPC_RMAP_INDEX) | j;
		}

		/* Now check and modify the HPTE */
		ptel = rev[i].guest_rpte;
		psize = hpte_page_size(be64_to_cpu(hptep[0]), ptel);
		if ((be64_to_cpu(hptep[0]) & HPTE_V_VALID) &&
		    hpte_rpn(ptel, psize) == gfn) {
			if (kvm->arch.using_mmu_notifiers)
				hptep[0] |= cpu_to_be64(HPTE_V_ABSENT);
			kvmppc_invalidate_hpte(kvm, hptep, i);
			/* Harvest R and C */
			rcbits = be64_to_cpu(hptep[1]) & (HPTE_R_R | HPTE_R_C);
			*rmapp |= rcbits << KVMPPC_RMAP_RC_SHIFT;
			if (rcbits & ~rev[i].guest_rpte) {
				rev[i].guest_rpte = ptel | rcbits;
				note_hpte_modification(kvm, &rev[i]);
			}
		}
		unlock_rmap(rmapp);
		hptep[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
	}
	return 0;
}

int kvm_unmap_hva_hv(struct kvm *kvm, unsigned long hva)
{
	if (kvm->arch.using_mmu_notifiers)
		kvm_handle_hva(kvm, hva, kvm_unmap_rmapp);
	return 0;
}

int kvm_unmap_hva_range_hv(struct kvm *kvm, unsigned long start, unsigned long end)
{
	if (kvm->arch.using_mmu_notifiers)
		kvm_handle_hva_range(kvm, start, end, kvm_unmap_rmapp);
	return 0;
}

void kvmppc_core_flush_memslot_hv(struct kvm *kvm,
				  struct kvm_memory_slot *memslot)
{
	unsigned long *rmapp;
	unsigned long gfn;
	unsigned long n;

	rmapp = memslot->arch.rmap;
	gfn = memslot->base_gfn;
	for (n = memslot->npages; n; --n) {
		/*
		 * Testing the present bit without locking is OK because
		 * the memslot has been marked invalid already, and hence
		 * no new HPTEs referencing this page can be created,
		 * thus the present bit can't go from 0 to 1.
		 */
		if (*rmapp & KVMPPC_RMAP_PRESENT)
			kvm_unmap_rmapp(kvm, rmapp, gfn);
		++rmapp;
		++gfn;
	}
}

static int kvm_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			 unsigned long gfn)
{
	struct revmap_entry *rev = kvm->arch.revmap;
	unsigned long head, i, j;
	__be64 *hptep;
	int ret = 0;

 retry:
	lock_rmap(rmapp);
	if (*rmapp & KVMPPC_RMAP_REFERENCED) {
		*rmapp &= ~KVMPPC_RMAP_REFERENCED;
		ret = 1;
	}
	if (!(*rmapp & KVMPPC_RMAP_PRESENT)) {
		unlock_rmap(rmapp);
		return ret;
	}

	i = head = *rmapp & KVMPPC_RMAP_INDEX;
	do {
		hptep = (__be64 *) (kvm->arch.hpt_virt + (i << 4));
		j = rev[i].forw;

		/* If this HPTE isn't referenced, ignore it */
		if (!(be64_to_cpu(hptep[1]) & HPTE_R_R))
			continue;

		if (!try_lock_hpte(hptep, HPTE_V_HVLOCK)) {
			/* unlock rmap before spinning on the HPTE lock */
			unlock_rmap(rmapp);
			while (be64_to_cpu(hptep[0]) & HPTE_V_HVLOCK)
				cpu_relax();
			goto retry;
		}

		/* Now check and modify the HPTE */
		if ((be64_to_cpu(hptep[0]) & HPTE_V_VALID) &&
		    (be64_to_cpu(hptep[1]) & HPTE_R_R)) {
			kvmppc_clear_ref_hpte(kvm, hptep, i);
			if (!(rev[i].guest_rpte & HPTE_R_R)) {
				rev[i].guest_rpte |= HPTE_R_R;
				note_hpte_modification(kvm, &rev[i]);
			}
			ret = 1;
		}
		hptep[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
	} while ((i = j) != head);

	unlock_rmap(rmapp);
	return ret;
}

int kvm_age_hva_hv(struct kvm *kvm, unsigned long start, unsigned long end)
{
	if (!kvm->arch.using_mmu_notifiers)
		return 0;
	return kvm_handle_hva_range(kvm, start, end, kvm_age_rmapp);
}

static int kvm_test_age_rmapp(struct kvm *kvm, unsigned long *rmapp,
			      unsigned long gfn)
{
	struct revmap_entry *rev = kvm->arch.revmap;
	unsigned long head, i, j;
	unsigned long *hp;
	int ret = 1;

	if (*rmapp & KVMPPC_RMAP_REFERENCED)
		return 1;

	lock_rmap(rmapp);
	if (*rmapp & KVMPPC_RMAP_REFERENCED)
		goto out;

	if (*rmapp & KVMPPC_RMAP_PRESENT) {
		i = head = *rmapp & KVMPPC_RMAP_INDEX;
		do {
			hp = (unsigned long *)(kvm->arch.hpt_virt + (i << 4));
			j = rev[i].forw;
			if (be64_to_cpu(hp[1]) & HPTE_R_R)
				goto out;
		} while ((i = j) != head);
	}
	ret = 0;

 out:
	unlock_rmap(rmapp);
	return ret;
}

int kvm_test_age_hva_hv(struct kvm *kvm, unsigned long hva)
{
	if (!kvm->arch.using_mmu_notifiers)
		return 0;
	return kvm_handle_hva(kvm, hva, kvm_test_age_rmapp);
}

void kvm_set_spte_hva_hv(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	if (!kvm->arch.using_mmu_notifiers)
		return;
	kvm_handle_hva(kvm, hva, kvm_unmap_rmapp);
}

static int vcpus_running(struct kvm *kvm)
{
	return atomic_read(&kvm->arch.vcpus_running) != 0;
}

/*
 * Returns the number of system pages that are dirty.
 * This can be more than 1 if we find a huge-page HPTE.
 */
static int kvm_test_clear_dirty_npages(struct kvm *kvm, unsigned long *rmapp)
{
	struct revmap_entry *rev = kvm->arch.revmap;
	unsigned long head, i, j;
	unsigned long n;
	unsigned long v, r;
	__be64 *hptep;
	int npages_dirty = 0;

 retry:
	lock_rmap(rmapp);
	if (*rmapp & KVMPPC_RMAP_CHANGED) {
		*rmapp &= ~KVMPPC_RMAP_CHANGED;
		npages_dirty = 1;
	}
	if (!(*rmapp & KVMPPC_RMAP_PRESENT)) {
		unlock_rmap(rmapp);
		return npages_dirty;
	}

	i = head = *rmapp & KVMPPC_RMAP_INDEX;
	do {
		unsigned long hptep1;
		hptep = (__be64 *) (kvm->arch.hpt_virt + (i << 4));
		j = rev[i].forw;

		/*
		 * Checking the C (changed) bit here is racy since there
		 * is no guarantee about when the hardware writes it back.
		 * If the HPTE is not writable then it is stable since the
		 * page can't be written to, and we would have done a tlbie
		 * (which forces the hardware to complete any writeback)
		 * when making the HPTE read-only.
		 * If vcpus are running then this call is racy anyway
		 * since the page could get dirtied subsequently, so we
		 * expect there to be a further call which would pick up
		 * any delayed C bit writeback.
		 * Otherwise we need to do the tlbie even if C==0 in
		 * order to pick up any delayed writeback of C.
		 */
		hptep1 = be64_to_cpu(hptep[1]);
		if (!(hptep1 & HPTE_R_C) &&
		    (!hpte_is_writable(hptep1) || vcpus_running(kvm)))
			continue;

		if (!try_lock_hpte(hptep, HPTE_V_HVLOCK)) {
			/* unlock rmap before spinning on the HPTE lock */
			unlock_rmap(rmapp);
			while (hptep[0] & cpu_to_be64(HPTE_V_HVLOCK))
				cpu_relax();
			goto retry;
		}

		/* Now check and modify the HPTE */
		if (!(hptep[0] & cpu_to_be64(HPTE_V_VALID))) {
			/* unlock and continue */
			hptep[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
			continue;
		}

		/* need to make it temporarily absent so C is stable */
		hptep[0] |= cpu_to_be64(HPTE_V_ABSENT);
		kvmppc_invalidate_hpte(kvm, hptep, i);
		v = be64_to_cpu(hptep[0]);
		r = be64_to_cpu(hptep[1]);
		if (r & HPTE_R_C) {
			hptep[1] = cpu_to_be64(r & ~HPTE_R_C);
			if (!(rev[i].guest_rpte & HPTE_R_C)) {
				rev[i].guest_rpte |= HPTE_R_C;
				note_hpte_modification(kvm, &rev[i]);
			}
			n = hpte_page_size(v, r);
			n = (n + PAGE_SIZE - 1) >> PAGE_SHIFT;
			if (n > npages_dirty)
				npages_dirty = n;
			eieio();
		}
		v &= ~(HPTE_V_ABSENT | HPTE_V_HVLOCK);
		v |= HPTE_V_VALID;
		hptep[0] = cpu_to_be64(v);
	} while ((i = j) != head);

	unlock_rmap(rmapp);
	return npages_dirty;
}

static void harvest_vpa_dirty(struct kvmppc_vpa *vpa,
			      struct kvm_memory_slot *memslot,
			      unsigned long *map)
{
	unsigned long gfn;

	if (!vpa->dirty || !vpa->pinned_addr)
		return;
	gfn = vpa->gpa >> PAGE_SHIFT;
	if (gfn < memslot->base_gfn ||
	    gfn >= memslot->base_gfn + memslot->npages)
		return;

	vpa->dirty = false;
	if (map)
		__set_bit_le(gfn - memslot->base_gfn, map);
}

long kvmppc_hv_get_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot,
			     unsigned long *map)
{
	unsigned long i, j;
	unsigned long *rmapp;
	struct kvm_vcpu *vcpu;

	preempt_disable();
	rmapp = memslot->arch.rmap;
	for (i = 0; i < memslot->npages; ++i) {
		int npages = kvm_test_clear_dirty_npages(kvm, rmapp);
		/*
		 * Note that if npages > 0 then i must be a multiple of npages,
		 * since we always put huge-page HPTEs in the rmap chain
		 * corresponding to their page base address.
		 */
		if (npages && map)
			for (j = i; npages; ++j, --npages)
				__set_bit_le(j, map);
		++rmapp;
	}

	/* Harvest dirty bits from VPA and DTL updates */
	/* Note: we never modify the SLB shadow buffer areas */
	kvm_for_each_vcpu(i, vcpu, kvm) {
		spin_lock(&vcpu->arch.vpa_update_lock);
		harvest_vpa_dirty(&vcpu->arch.vpa, memslot, map);
		harvest_vpa_dirty(&vcpu->arch.dtl, memslot, map);
		spin_unlock(&vcpu->arch.vpa_update_lock);
	}
	preempt_enable();
	return 0;
}

void *kvmppc_pin_guest_page(struct kvm *kvm, unsigned long gpa,
			    unsigned long *nb_ret)
{
	struct kvm_memory_slot *memslot;
	unsigned long gfn = gpa >> PAGE_SHIFT;
	struct page *page, *pages[1];
	int npages;
	unsigned long hva, offset;
	unsigned long pa;
	unsigned long *physp;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, gfn);
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		goto err;
	if (!kvm->arch.using_mmu_notifiers) {
		physp = memslot->arch.slot_phys;
		if (!physp)
			goto err;
		physp += gfn - memslot->base_gfn;
		pa = *physp;
		if (!pa) {
			if (kvmppc_get_guest_page(kvm, gfn, memslot,
						  PAGE_SIZE) < 0)
				goto err;
			pa = *physp;
		}
		page = pfn_to_page(pa >> PAGE_SHIFT);
		get_page(page);
	} else {
		hva = gfn_to_hva_memslot(memslot, gfn);
		npages = get_user_pages_fast(hva, 1, 1, pages);
		if (npages < 1)
			goto err;
		page = pages[0];
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);

	offset = gpa & (PAGE_SIZE - 1);
	if (nb_ret)
		*nb_ret = PAGE_SIZE - offset;
	return page_address(page) + offset;

 err:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return NULL;
}

void kvmppc_unpin_guest_page(struct kvm *kvm, void *va, unsigned long gpa,
			     bool dirty)
{
	struct page *page = virt_to_page(va);
	struct kvm_memory_slot *memslot;
	unsigned long gfn;
	unsigned long *rmap;
	int srcu_idx;

	put_page(page);

	if (!dirty || !kvm->arch.using_mmu_notifiers)
		return;

	/* We need to mark this page dirty in the rmap chain */
	gfn = gpa >> PAGE_SHIFT;
	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, gfn);
	if (memslot) {
		rmap = &memslot->arch.rmap[gfn - memslot->base_gfn];
		lock_rmap(rmap);
		*rmap |= KVMPPC_RMAP_CHANGED;
		unlock_rmap(rmap);
	}
	srcu_read_unlock(&kvm->srcu, srcu_idx);
}

/*
 * Functions for reading and writing the hash table via reads and
 * writes on a file descriptor.
 *
 * Reads return the guest view of the hash table, which has to be
 * pieced together from the real hash table and the guest_rpte
 * values in the revmap array.
 *
 * On writes, each HPTE written is considered in turn, and if it
 * is valid, it is written to the HPT as if an H_ENTER with the
 * exact flag set was done.  When the invalid count is non-zero
 * in the header written to the stream, the kernel will make
 * sure that that many HPTEs are invalid, and invalidate them
 * if not.
 */

struct kvm_htab_ctx {
	unsigned long	index;
	unsigned long	flags;
	struct kvm	*kvm;
	int		first_pass;
};

#define HPTE_SIZE	(2 * sizeof(unsigned long))

/*
 * Returns 1 if this HPT entry has been modified or has pending
 * R/C bit changes.
 */
static int hpte_dirty(struct revmap_entry *revp, __be64 *hptp)
{
	unsigned long rcbits_unset;

	if (revp->guest_rpte & HPTE_GR_MODIFIED)
		return 1;

	/* Also need to consider changes in reference and changed bits */
	rcbits_unset = ~revp->guest_rpte & (HPTE_R_R | HPTE_R_C);
	if ((be64_to_cpu(hptp[0]) & HPTE_V_VALID) &&
	    (be64_to_cpu(hptp[1]) & rcbits_unset))
		return 1;

	return 0;
}

static long record_hpte(unsigned long flags, __be64 *hptp,
			unsigned long *hpte, struct revmap_entry *revp,
			int want_valid, int first_pass)
{
	unsigned long v, r;
	unsigned long rcbits_unset;
	int ok = 1;
	int valid, dirty;

	/* Unmodified entries are uninteresting except on the first pass */
	dirty = hpte_dirty(revp, hptp);
	if (!first_pass && !dirty)
		return 0;

	valid = 0;
	if (be64_to_cpu(hptp[0]) & (HPTE_V_VALID | HPTE_V_ABSENT)) {
		valid = 1;
		if ((flags & KVM_GET_HTAB_BOLTED_ONLY) &&
		    !(be64_to_cpu(hptp[0]) & HPTE_V_BOLTED))
			valid = 0;
	}
	if (valid != want_valid)
		return 0;

	v = r = 0;
	if (valid || dirty) {
		/* lock the HPTE so it's stable and read it */
		preempt_disable();
		while (!try_lock_hpte(hptp, HPTE_V_HVLOCK))
			cpu_relax();
		v = be64_to_cpu(hptp[0]);

		/* re-evaluate valid and dirty from synchronized HPTE value */
		valid = !!(v & HPTE_V_VALID);
		dirty = !!(revp->guest_rpte & HPTE_GR_MODIFIED);

		/* Harvest R and C into guest view if necessary */
		rcbits_unset = ~revp->guest_rpte & (HPTE_R_R | HPTE_R_C);
		if (valid && (rcbits_unset & be64_to_cpu(hptp[1]))) {
			revp->guest_rpte |= (be64_to_cpu(hptp[1]) &
				(HPTE_R_R | HPTE_R_C)) | HPTE_GR_MODIFIED;
			dirty = 1;
		}

		if (v & HPTE_V_ABSENT) {
			v &= ~HPTE_V_ABSENT;
			v |= HPTE_V_VALID;
			valid = 1;
		}
		if ((flags & KVM_GET_HTAB_BOLTED_ONLY) && !(v & HPTE_V_BOLTED))
			valid = 0;

		r = revp->guest_rpte;
		/* only clear modified if this is the right sort of entry */
		if (valid == want_valid && dirty) {
			r &= ~HPTE_GR_MODIFIED;
			revp->guest_rpte = r;
		}
		asm volatile(PPC_RELEASE_BARRIER "" : : : "memory");
		hptp[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
		preempt_enable();
		if (!(valid == want_valid && (first_pass || dirty)))
			ok = 0;
	}
	hpte[0] = cpu_to_be64(v);
	hpte[1] = cpu_to_be64(r);
	return ok;
}

static ssize_t kvm_htab_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct kvm_htab_ctx *ctx = file->private_data;
	struct kvm *kvm = ctx->kvm;
	struct kvm_get_htab_header hdr;
	__be64 *hptp;
	struct revmap_entry *revp;
	unsigned long i, nb, nw;
	unsigned long __user *lbuf;
	struct kvm_get_htab_header __user *hptr;
	unsigned long flags;
	int first_pass;
	unsigned long hpte[2];

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	first_pass = ctx->first_pass;
	flags = ctx->flags;

	i = ctx->index;
	hptp = (__be64 *)(kvm->arch.hpt_virt + (i * HPTE_SIZE));
	revp = kvm->arch.revmap + i;
	lbuf = (unsigned long __user *)buf;

	nb = 0;
	while (nb + sizeof(hdr) + HPTE_SIZE < count) {
		/* Initialize header */
		hptr = (struct kvm_get_htab_header __user *)buf;
		hdr.n_valid = 0;
		hdr.n_invalid = 0;
		nw = nb;
		nb += sizeof(hdr);
		lbuf = (unsigned long __user *)(buf + sizeof(hdr));

		/* Skip uninteresting entries, i.e. clean on not-first pass */
		if (!first_pass) {
			while (i < kvm->arch.hpt_npte &&
			       !hpte_dirty(revp, hptp)) {
				++i;
				hptp += 2;
				++revp;
			}
		}
		hdr.index = i;

		/* Grab a series of valid entries */
		while (i < kvm->arch.hpt_npte &&
		       hdr.n_valid < 0xffff &&
		       nb + HPTE_SIZE < count &&
		       record_hpte(flags, hptp, hpte, revp, 1, first_pass)) {
			/* valid entry, write it out */
			++hdr.n_valid;
			if (__put_user(hpte[0], lbuf) ||
			    __put_user(hpte[1], lbuf + 1))
				return -EFAULT;
			nb += HPTE_SIZE;
			lbuf += 2;
			++i;
			hptp += 2;
			++revp;
		}
		/* Now skip invalid entries while we can */
		while (i < kvm->arch.hpt_npte &&
		       hdr.n_invalid < 0xffff &&
		       record_hpte(flags, hptp, hpte, revp, 0, first_pass)) {
			/* found an invalid entry */
			++hdr.n_invalid;
			++i;
			hptp += 2;
			++revp;
		}

		if (hdr.n_valid || hdr.n_invalid) {
			/* write back the header */
			if (__copy_to_user(hptr, &hdr, sizeof(hdr)))
				return -EFAULT;
			nw = nb;
			buf = (char __user *)lbuf;
		} else {
			nb = nw;
		}

		/* Check if we've wrapped around the hash table */
		if (i >= kvm->arch.hpt_npte) {
			i = 0;
			ctx->first_pass = 0;
			break;
		}
	}

	ctx->index = i;

	return nb;
}

static ssize_t kvm_htab_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct kvm_htab_ctx *ctx = file->private_data;
	struct kvm *kvm = ctx->kvm;
	struct kvm_get_htab_header hdr;
	unsigned long i, j;
	unsigned long v, r;
	unsigned long __user *lbuf;
	__be64 *hptp;
	unsigned long tmp[2];
	ssize_t nb;
	long int err, ret;
	int rma_setup;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	/* lock out vcpus from running while we're doing this */
	mutex_lock(&kvm->lock);
	rma_setup = kvm->arch.rma_setup_done;
	if (rma_setup) {
		kvm->arch.rma_setup_done = 0;	/* temporarily */
		/* order rma_setup_done vs. vcpus_running */
		smp_mb();
		if (atomic_read(&kvm->arch.vcpus_running)) {
			kvm->arch.rma_setup_done = 1;
			mutex_unlock(&kvm->lock);
			return -EBUSY;
		}
	}

	err = 0;
	for (nb = 0; nb + sizeof(hdr) <= count; ) {
		err = -EFAULT;
		if (__copy_from_user(&hdr, buf, sizeof(hdr)))
			break;

		err = 0;
		if (nb + hdr.n_valid * HPTE_SIZE > count)
			break;

		nb += sizeof(hdr);
		buf += sizeof(hdr);

		err = -EINVAL;
		i = hdr.index;
		if (i >= kvm->arch.hpt_npte ||
		    i + hdr.n_valid + hdr.n_invalid > kvm->arch.hpt_npte)
			break;

		hptp = (__be64 *)(kvm->arch.hpt_virt + (i * HPTE_SIZE));
		lbuf = (unsigned long __user *)buf;
		for (j = 0; j < hdr.n_valid; ++j) {
			err = -EFAULT;
			if (__get_user(v, lbuf) || __get_user(r, lbuf + 1))
				goto out;
			err = -EINVAL;
			if (!(v & HPTE_V_VALID))
				goto out;
			lbuf += 2;
			nb += HPTE_SIZE;

			if (be64_to_cpu(hptp[0]) & (HPTE_V_VALID | HPTE_V_ABSENT))
				kvmppc_do_h_remove(kvm, 0, i, 0, tmp);
			err = -EIO;
			ret = kvmppc_virtmode_do_h_enter(kvm, H_EXACT, i, v, r,
							 tmp);
			if (ret != H_SUCCESS) {
				pr_err("kvm_htab_write ret %ld i=%ld v=%lx "
				       "r=%lx\n", ret, i, v, r);
				goto out;
			}
			if (!rma_setup && is_vrma_hpte(v)) {
				unsigned long psize = hpte_base_page_size(v, r);
				unsigned long senc = slb_pgsize_encoding(psize);
				unsigned long lpcr;

				kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
					(VRMA_VSID << SLB_VSID_SHIFT_1T);
				lpcr = senc << (LPCR_VRMASD_SH - 4);
				kvmppc_update_lpcr(kvm, lpcr, LPCR_VRMASD);
				rma_setup = 1;
			}
			++i;
			hptp += 2;
		}

		for (j = 0; j < hdr.n_invalid; ++j) {
			if (be64_to_cpu(hptp[0]) & (HPTE_V_VALID | HPTE_V_ABSENT))
				kvmppc_do_h_remove(kvm, 0, i, 0, tmp);
			++i;
			hptp += 2;
		}
		err = 0;
	}

 out:
	/* Order HPTE updates vs. rma_setup_done */
	smp_wmb();
	kvm->arch.rma_setup_done = rma_setup;
	mutex_unlock(&kvm->lock);

	if (err)
		return err;
	return nb;
}

static int kvm_htab_release(struct inode *inode, struct file *filp)
{
	struct kvm_htab_ctx *ctx = filp->private_data;

	filp->private_data = NULL;
	if (!(ctx->flags & KVM_GET_HTAB_WRITE))
		atomic_dec(&ctx->kvm->arch.hpte_mod_interest);
	kvm_put_kvm(ctx->kvm);
	kfree(ctx);
	return 0;
}

static const struct file_operations kvm_htab_fops = {
	.read		= kvm_htab_read,
	.write		= kvm_htab_write,
	.llseek		= default_llseek,
	.release	= kvm_htab_release,
};

int kvm_vm_ioctl_get_htab_fd(struct kvm *kvm, struct kvm_get_htab_fd *ghf)
{
	int ret;
	struct kvm_htab_ctx *ctx;
	int rwflag;

	/* reject flags we don't recognize */
	if (ghf->flags & ~(KVM_GET_HTAB_BOLTED_ONLY | KVM_GET_HTAB_WRITE))
		return -EINVAL;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	kvm_get_kvm(kvm);
	ctx->kvm = kvm;
	ctx->index = ghf->start_index;
	ctx->flags = ghf->flags;
	ctx->first_pass = 1;

	rwflag = (ghf->flags & KVM_GET_HTAB_WRITE) ? O_WRONLY : O_RDONLY;
	ret = anon_inode_getfd("kvm-htab", &kvm_htab_fops, ctx, rwflag | O_CLOEXEC);
	if (ret < 0) {
		kvm_put_kvm(kvm);
		return ret;
	}

	if (rwflag == O_RDONLY) {
		mutex_lock(&kvm->slots_lock);
		atomic_inc(&kvm->arch.hpte_mod_interest);
		/* make sure kvmppc_do_h_enter etc. see the increment */
		synchronize_srcu_expedited(&kvm->srcu);
		mutex_unlock(&kvm->slots_lock);
	}

	return ret;
}

void kvmppc_mmu_book3s_hv_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_mmu *mmu = &vcpu->arch.mmu;

	if (cpu_has_feature(CPU_FTR_ARCH_206))
		vcpu->arch.slb_nr = 32;		/* POWER7 */
	else
		vcpu->arch.slb_nr = 64;

	mmu->xlate = kvmppc_mmu_book3s_64_hv_xlate;
	mmu->reset_msr = kvmppc_mmu_book3s_64_hv_reset_msr;

	vcpu->arch.hflags |= BOOK3S_HFLAG_SLB;
}
