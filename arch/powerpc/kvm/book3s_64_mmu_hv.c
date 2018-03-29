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
#include <linux/debugfs.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/cputable.h>
#include <asm/pte-walk.h>

#include "trace_hv.h"

//#define DEBUG_RESIZE_HPT	1

#ifdef DEBUG_RESIZE_HPT
#define resize_hpt_debug(resize, ...)				\
	do {							\
		printk(KERN_DEBUG "RESIZE HPT %p: ", resize);	\
		printk(__VA_ARGS__);				\
	} while (0)
#else
#define resize_hpt_debug(resize, ...)				\
	do { } while (0)
#endif

static long kvmppc_virtmode_do_h_enter(struct kvm *kvm, unsigned long flags,
				long pte_index, unsigned long pteh,
				unsigned long ptel, unsigned long *pte_idx_ret);

struct kvm_resize_hpt {
	/* These fields read-only after init */
	struct kvm *kvm;
	struct work_struct work;
	u32 order;

	/* These fields protected by kvm->lock */

	/* Possible values and their usage:
	 *  <0     an error occurred during allocation,
	 *  -EBUSY allocation is in the progress,
	 *  0      allocation made successfuly.
	 */
	int error;

	/* Private to the work thread, until error != -EBUSY,
	 * then protected by kvm->lock.
	 */
	struct kvm_hpt_info hpt;
};

int kvmppc_allocate_hpt(struct kvm_hpt_info *info, u32 order)
{
	unsigned long hpt = 0;
	int cma = 0;
	struct page *page = NULL;
	struct revmap_entry *rev;
	unsigned long npte;

	if ((order < PPC_MIN_HPT_ORDER) || (order > PPC_MAX_HPT_ORDER))
		return -EINVAL;

	page = kvm_alloc_hpt_cma(1ul << (order - PAGE_SHIFT));
	if (page) {
		hpt = (unsigned long)pfn_to_kaddr(page_to_pfn(page));
		memset((void *)hpt, 0, (1ul << order));
		cma = 1;
	}

	if (!hpt)
		hpt = __get_free_pages(GFP_KERNEL|__GFP_ZERO|__GFP_RETRY_MAYFAIL
				       |__GFP_NOWARN, order - PAGE_SHIFT);

	if (!hpt)
		return -ENOMEM;

	/* HPTEs are 2**4 bytes long */
	npte = 1ul << (order - 4);

	/* Allocate reverse map array */
	rev = vmalloc(sizeof(struct revmap_entry) * npte);
	if (!rev) {
		if (cma)
			kvm_free_hpt_cma(page, 1 << (order - PAGE_SHIFT));
		else
			free_pages(hpt, order - PAGE_SHIFT);
		return -ENOMEM;
	}

	info->order = order;
	info->virt = hpt;
	info->cma = cma;
	info->rev = rev;

	return 0;
}

void kvmppc_set_hpt(struct kvm *kvm, struct kvm_hpt_info *info)
{
	atomic64_set(&kvm->arch.mmio_update, 0);
	kvm->arch.hpt = *info;
	kvm->arch.sdr1 = __pa(info->virt) | (info->order - 18);

	pr_debug("KVM guest htab at %lx (order %ld), LPID %x\n",
		 info->virt, (long)info->order, kvm->arch.lpid);
}

long kvmppc_alloc_reset_hpt(struct kvm *kvm, int order)
{
	long err = -EBUSY;
	struct kvm_hpt_info info;

	mutex_lock(&kvm->lock);
	if (kvm->arch.mmu_ready) {
		kvm->arch.mmu_ready = 0;
		/* order mmu_ready vs. vcpus_running */
		smp_mb();
		if (atomic_read(&kvm->arch.vcpus_running)) {
			kvm->arch.mmu_ready = 1;
			goto out;
		}
	}
	if (kvm_is_radix(kvm)) {
		err = kvmppc_switch_mmu_to_hpt(kvm);
		if (err)
			goto out;
	}

	if (kvm->arch.hpt.order == order) {
		/* We already have a suitable HPT */

		/* Set the entire HPT to 0, i.e. invalid HPTEs */
		memset((void *)kvm->arch.hpt.virt, 0, 1ul << order);
		/*
		 * Reset all the reverse-mapping chains for all memslots
		 */
		kvmppc_rmap_reset(kvm);
		err = 0;
		goto out;
	}

	if (kvm->arch.hpt.virt) {
		kvmppc_free_hpt(&kvm->arch.hpt);
		kvmppc_rmap_reset(kvm);
	}

	err = kvmppc_allocate_hpt(&info, order);
	if (err < 0)
		goto out;
	kvmppc_set_hpt(kvm, &info);

out:
	if (err == 0)
		/* Ensure that each vcpu will flush its TLB on next entry. */
		cpumask_setall(&kvm->arch.need_tlb_flush);

	mutex_unlock(&kvm->lock);
	return err;
}

void kvmppc_free_hpt(struct kvm_hpt_info *info)
{
	vfree(info->rev);
	info->rev = NULL;
	if (info->cma)
		kvm_free_hpt_cma(virt_to_page(info->virt),
				 1 << (info->order - PAGE_SHIFT));
	else if (info->virt)
		free_pages(info->virt, info->order - PAGE_SHIFT);
	info->virt = 0;
	info->order = 0;
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
	if (npages > kvmppc_hpt_mask(&kvm->arch.hpt) + 1)
		npages = kvmppc_hpt_mask(&kvm->arch.hpt) + 1;

	hp0 = HPTE_V_1TB_SEG | (VRMA_VSID << (40 - 16)) |
		HPTE_V_BOLTED | hpte0_pgsize_encoding(psize);
	hp1 = hpte1_pgsize_encoding(psize) |
		HPTE_R_R | HPTE_R_C | HPTE_R_M | PP_RWXX;

	for (i = 0; i < npages; ++i) {
		addr = i << porder;
		/* can't use hpt_hash since va > 64 bits */
		hash = (i ^ (VRMA_VSID ^ (VRMA_VSID << 25)))
			& kvmppc_hpt_mask(&kvm->arch.hpt);
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

	/* POWER7 has 10-bit LPIDs (12-bit in POWER8) */
	host_lpid = mfspr(SPRN_LPID);
	rsvd_lpid = LPID_RSVD;

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

static long kvmppc_virtmode_do_h_enter(struct kvm *kvm, unsigned long flags,
				long pte_index, unsigned long pteh,
				unsigned long ptel, unsigned long *pte_idx_ret)
{
	long ret;

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

	ra_mask = kvmppc_actual_pgsz(v, r) - 1;
	return (r & HPTE_R_RPN & ~ra_mask) | (ea & ra_mask);
}

static int kvmppc_mmu_book3s_64_hv_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
			struct kvmppc_pte *gpte, bool data, bool iswrite)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_slb *slbe;
	unsigned long slb_v;
	unsigned long pp, key;
	unsigned long v, orig_v, gr;
	__be64 *hptep;
	int index;
	int virtmode = vcpu->arch.shregs.msr & (data ? MSR_DR : MSR_IR);

	if (kvm_is_radix(vcpu->kvm))
		return kvmppc_mmu_radix_xlate(vcpu, eaddr, gpte, data, iswrite);

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
	hptep = (__be64 *)(kvm->arch.hpt.virt + (index << 4));
	v = orig_v = be64_to_cpu(hptep[0]) & ~HPTE_V_HVLOCK;
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		v = hpte_new_to_old_v(v, be64_to_cpu(hptep[1]));
	gr = kvm->arch.hpt.rev[index].guest_rpte;

	unlock_hpte(hptep, orig_v);
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
	if (data && virtmode) {
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

int kvmppc_hv_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu,
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
	unsigned long hnow_v, hnow_r;
	__be64 *hptep;
	unsigned long mmu_seq, psize, pte_size;
	unsigned long gpa_base, gfn_base;
	unsigned long gpa, gfn, hva, pfn;
	struct kvm_memory_slot *memslot;
	unsigned long *rmap;
	struct revmap_entry *rev;
	struct page *page, *pages[1];
	long index, ret, npages;
	bool is_ci;
	unsigned int writing, write_ok;
	struct vm_area_struct *vma;
	unsigned long rcbits;
	long mmio_update;

	if (kvm_is_radix(kvm))
		return kvmppc_book3s_radix_page_fault(run, vcpu, ea, dsisr);

	/*
	 * Real-mode code has already searched the HPT and found the
	 * entry we're interested in.  Lock the entry and check that
	 * it hasn't changed.  If it has, just return and re-execute the
	 * instruction.
	 */
	if (ea != vcpu->arch.pgfault_addr)
		return RESUME_GUEST;

	if (vcpu->arch.pgfault_cache) {
		mmio_update = atomic64_read(&kvm->arch.mmio_update);
		if (mmio_update == vcpu->arch.pgfault_cache->mmio_update) {
			r = vcpu->arch.pgfault_cache->rpte;
			psize = kvmppc_actual_pgsz(vcpu->arch.pgfault_hpte[0],
						   r);
			gpa_base = r & HPTE_R_RPN & ~(psize - 1);
			gfn_base = gpa_base >> PAGE_SHIFT;
			gpa = gpa_base | (ea & (psize - 1));
			return kvmppc_hv_emulate_mmio(run, vcpu, gpa, ea,
						dsisr & DSISR_ISSTORE);
		}
	}
	index = vcpu->arch.pgfault_index;
	hptep = (__be64 *)(kvm->arch.hpt.virt + (index << 4));
	rev = &kvm->arch.hpt.rev[index];
	preempt_disable();
	while (!try_lock_hpte(hptep, HPTE_V_HVLOCK))
		cpu_relax();
	hpte[0] = be64_to_cpu(hptep[0]) & ~HPTE_V_HVLOCK;
	hpte[1] = be64_to_cpu(hptep[1]);
	hpte[2] = r = rev->guest_rpte;
	unlock_hpte(hptep, hpte[0]);
	preempt_enable();

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		hpte[0] = hpte_new_to_old_v(hpte[0], hpte[1]);
		hpte[1] = hpte_new_to_old_r(hpte[1]);
	}
	if (hpte[0] != vcpu->arch.pgfault_hpte[0] ||
	    hpte[1] != vcpu->arch.pgfault_hpte[1])
		return RESUME_GUEST;

	/* Translate the logical address and get the page */
	psize = kvmppc_actual_pgsz(hpte[0], r);
	gpa_base = r & HPTE_R_RPN & ~(psize - 1);
	gfn_base = gpa_base >> PAGE_SHIFT;
	gpa = gpa_base | (ea & (psize - 1));
	gfn = gpa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(kvm, gfn);

	trace_kvm_page_fault_enter(vcpu, hpte, memslot, ea, dsisr);

	/* No memslot means it's an emulated MMIO region */
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		return kvmppc_hv_emulate_mmio(run, vcpu, gpa, ea,
					      dsisr & DSISR_ISSTORE);

	/*
	 * This should never happen, because of the slot_is_aligned()
	 * check in kvmppc_do_h_enter().
	 */
	if (gfn_base < memslot->base_gfn)
		return -EFAULT;

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	ret = -EFAULT;
	is_ci = false;
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
			is_ci = pte_ci(__pte((pgprot_val(vma->vm_page_prot))));
			write_ok = vma->vm_flags & VM_WRITE;
		}
		up_read(&current->mm->mmap_sem);
		if (!pfn)
			goto out_put;
	} else {
		page = pages[0];
		pfn = page_to_pfn(page);
		if (PageHuge(page)) {
			page = compound_head(page);
			pte_size <<= compound_order(page);
		}
		/* if the guest wants write access, see if that is OK */
		if (!writing && hpte_is_writable(r)) {
			pte_t *ptep, pte;
			unsigned long flags;
			/*
			 * We need to protect against page table destruction
			 * hugepage split and collapse.
			 */
			local_irq_save(flags);
			ptep = find_current_mm_pte(current->mm->pgd,
						   hva, NULL, NULL);
			if (ptep) {
				pte = kvmppc_read_update_linux_pte(ptep, 1);
				if (__pte_write(pte))
					write_ok = 1;
			}
			local_irq_restore(flags);
		}
	}

	if (psize > pte_size)
		goto out_put;

	/* Check WIMG vs. the actual page we're accessing */
	if (!hpte_cache_flags_ok(r, is_ci)) {
		if (is_ci)
			goto out_put;
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
	r = (r & HPTE_R_KEY_HI) | (r & ~(HPTE_R_PP0 - psize)) |
					((pfn << PAGE_SHIFT) & ~(psize - 1));
	if (hpte_is_writable(r) && !write_ok)
		r = hpte_make_readonly(r);
	ret = RESUME_GUEST;
	preempt_disable();
	while (!try_lock_hpte(hptep, HPTE_V_HVLOCK))
		cpu_relax();
	hnow_v = be64_to_cpu(hptep[0]);
	hnow_r = be64_to_cpu(hptep[1]);
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		hnow_v = hpte_new_to_old_v(hnow_v, hnow_r);
		hnow_r = hpte_new_to_old_r(hnow_r);
	}

	/*
	 * If the HPT is being resized, don't update the HPTE,
	 * instead let the guest retry after the resize operation is complete.
	 * The synchronization for mmu_ready test vs. set is provided
	 * by the HPTE lock.
	 */
	if (!kvm->arch.mmu_ready)
		goto out_unlock;

	if ((hnow_v & ~HPTE_V_HVLOCK) != hpte[0] || hnow_r != hpte[1] ||
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

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		r = hpte_old_to_new_r(hpte[0], r);
		hpte[0] = hpte_old_to_new_v(hpte[0]);
	}
	hptep[1] = cpu_to_be64(r);
	eieio();
	__unlock_hpte(hptep, hpte[0]);
	asm volatile("ptesync" : : : "memory");
	preempt_enable();
	if (page && hpte_is_writable(r))
		SetPageDirty(page);

 out_put:
	trace_kvm_page_fault_exit(vcpu, hpte, ret);

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
	__unlock_hpte(hptep, be64_to_cpu(hptep[0]));
	preempt_enable();
	goto out_put;
}

void kvmppc_rmap_reset(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);
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

typedef int (*hva_handler_fn)(struct kvm *kvm, struct kvm_memory_slot *memslot,
			      unsigned long gfn);

static int kvm_handle_hva_range(struct kvm *kvm,
				unsigned long start,
				unsigned long end,
				hva_handler_fn handler)
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
			ret = handler(kvm, memslot, gfn);
			retval |= ret;
		}
	}

	return retval;
}

static int kvm_handle_hva(struct kvm *kvm, unsigned long hva,
			  hva_handler_fn handler)
{
	return kvm_handle_hva_range(kvm, hva, hva + 1, handler);
}

/* Must be called with both HPTE and rmap locked */
static void kvmppc_unmap_hpte(struct kvm *kvm, unsigned long i,
			      struct kvm_memory_slot *memslot,
			      unsigned long *rmapp, unsigned long gfn)
{
	__be64 *hptep = (__be64 *) (kvm->arch.hpt.virt + (i << 4));
	struct revmap_entry *rev = kvm->arch.hpt.rev;
	unsigned long j, h;
	unsigned long ptel, psize, rcbits;

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
	psize = kvmppc_actual_pgsz(be64_to_cpu(hptep[0]), ptel);
	if ((be64_to_cpu(hptep[0]) & HPTE_V_VALID) &&
	    hpte_rpn(ptel, psize) == gfn) {
		hptep[0] |= cpu_to_be64(HPTE_V_ABSENT);
		kvmppc_invalidate_hpte(kvm, hptep, i);
		hptep[1] &= ~cpu_to_be64(HPTE_R_KEY_HI | HPTE_R_KEY_LO);
		/* Harvest R and C */
		rcbits = be64_to_cpu(hptep[1]) & (HPTE_R_R | HPTE_R_C);
		*rmapp |= rcbits << KVMPPC_RMAP_RC_SHIFT;
		if ((rcbits & HPTE_R_C) && memslot->dirty_bitmap)
			kvmppc_update_dirty_map(memslot, gfn, psize);
		if (rcbits & ~rev[i].guest_rpte) {
			rev[i].guest_rpte = ptel | rcbits;
			note_hpte_modification(kvm, &rev[i]);
		}
	}
}

static int kvm_unmap_rmapp(struct kvm *kvm, struct kvm_memory_slot *memslot,
			   unsigned long gfn)
{
	unsigned long i;
	__be64 *hptep;
	unsigned long *rmapp;

	rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
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
		hptep = (__be64 *) (kvm->arch.hpt.virt + (i << 4));
		if (!try_lock_hpte(hptep, HPTE_V_HVLOCK)) {
			/* unlock rmap before spinning on the HPTE lock */
			unlock_rmap(rmapp);
			while (be64_to_cpu(hptep[0]) & HPTE_V_HVLOCK)
				cpu_relax();
			continue;
		}

		kvmppc_unmap_hpte(kvm, i, memslot, rmapp, gfn);
		unlock_rmap(rmapp);
		__unlock_hpte(hptep, be64_to_cpu(hptep[0]));
	}
	return 0;
}

int kvm_unmap_hva_range_hv(struct kvm *kvm, unsigned long start, unsigned long end)
{
	hva_handler_fn handler;

	handler = kvm_is_radix(kvm) ? kvm_unmap_radix : kvm_unmap_rmapp;
	kvm_handle_hva_range(kvm, start, end, handler);
	return 0;
}

void kvmppc_core_flush_memslot_hv(struct kvm *kvm,
				  struct kvm_memory_slot *memslot)
{
	unsigned long gfn;
	unsigned long n;
	unsigned long *rmapp;

	gfn = memslot->base_gfn;
	rmapp = memslot->arch.rmap;
	for (n = memslot->npages; n; --n, ++gfn) {
		if (kvm_is_radix(kvm)) {
			kvm_unmap_radix(kvm, memslot, gfn);
			continue;
		}
		/*
		 * Testing the present bit without locking is OK because
		 * the memslot has been marked invalid already, and hence
		 * no new HPTEs referencing this page can be created,
		 * thus the present bit can't go from 0 to 1.
		 */
		if (*rmapp & KVMPPC_RMAP_PRESENT)
			kvm_unmap_rmapp(kvm, memslot, gfn);
		++rmapp;
	}
}

static int kvm_age_rmapp(struct kvm *kvm, struct kvm_memory_slot *memslot,
			 unsigned long gfn)
{
	struct revmap_entry *rev = kvm->arch.hpt.rev;
	unsigned long head, i, j;
	__be64 *hptep;
	int ret = 0;
	unsigned long *rmapp;

	rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
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
		hptep = (__be64 *) (kvm->arch.hpt.virt + (i << 4));
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
		__unlock_hpte(hptep, be64_to_cpu(hptep[0]));
	} while ((i = j) != head);

	unlock_rmap(rmapp);
	return ret;
}

int kvm_age_hva_hv(struct kvm *kvm, unsigned long start, unsigned long end)
{
	hva_handler_fn handler;

	handler = kvm_is_radix(kvm) ? kvm_age_radix : kvm_age_rmapp;
	return kvm_handle_hva_range(kvm, start, end, handler);
}

static int kvm_test_age_rmapp(struct kvm *kvm, struct kvm_memory_slot *memslot,
			      unsigned long gfn)
{
	struct revmap_entry *rev = kvm->arch.hpt.rev;
	unsigned long head, i, j;
	unsigned long *hp;
	int ret = 1;
	unsigned long *rmapp;

	rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
	if (*rmapp & KVMPPC_RMAP_REFERENCED)
		return 1;

	lock_rmap(rmapp);
	if (*rmapp & KVMPPC_RMAP_REFERENCED)
		goto out;

	if (*rmapp & KVMPPC_RMAP_PRESENT) {
		i = head = *rmapp & KVMPPC_RMAP_INDEX;
		do {
			hp = (unsigned long *)(kvm->arch.hpt.virt + (i << 4));
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
	hva_handler_fn handler;

	handler = kvm_is_radix(kvm) ? kvm_test_age_radix : kvm_test_age_rmapp;
	return kvm_handle_hva(kvm, hva, handler);
}

void kvm_set_spte_hva_hv(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	hva_handler_fn handler;

	handler = kvm_is_radix(kvm) ? kvm_unmap_radix : kvm_unmap_rmapp;
	kvm_handle_hva(kvm, hva, handler);
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
	struct revmap_entry *rev = kvm->arch.hpt.rev;
	unsigned long head, i, j;
	unsigned long n;
	unsigned long v, r;
	__be64 *hptep;
	int npages_dirty = 0;

 retry:
	lock_rmap(rmapp);
	if (!(*rmapp & KVMPPC_RMAP_PRESENT)) {
		unlock_rmap(rmapp);
		return npages_dirty;
	}

	i = head = *rmapp & KVMPPC_RMAP_INDEX;
	do {
		unsigned long hptep1;
		hptep = (__be64 *) (kvm->arch.hpt.virt + (i << 4));
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
			__unlock_hpte(hptep, be64_to_cpu(hptep[0]));
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
			n = kvmppc_actual_pgsz(v, r);
			n = (n + PAGE_SIZE - 1) >> PAGE_SHIFT;
			if (n > npages_dirty)
				npages_dirty = n;
			eieio();
		}
		v &= ~HPTE_V_ABSENT;
		v |= HPTE_V_VALID;
		__unlock_hpte(hptep, v);
	} while ((i = j) != head);

	unlock_rmap(rmapp);
	return npages_dirty;
}

void kvmppc_harvest_vpa_dirty(struct kvmppc_vpa *vpa,
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

long kvmppc_hv_get_dirty_log_hpt(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map)
{
	unsigned long i;
	unsigned long *rmapp;

	preempt_disable();
	rmapp = memslot->arch.rmap;
	for (i = 0; i < memslot->npages; ++i) {
		int npages = kvm_test_clear_dirty_npages(kvm, rmapp);
		/*
		 * Note that if npages > 0 then i must be a multiple of npages,
		 * since we always put huge-page HPTEs in the rmap chain
		 * corresponding to their page base address.
		 */
		if (npages)
			set_dirty_bits(map, i, npages);
		++rmapp;
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
	int srcu_idx;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, gfn);
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		goto err;
	hva = gfn_to_hva_memslot(memslot, gfn);
	npages = get_user_pages_fast(hva, 1, 1, pages);
	if (npages < 1)
		goto err;
	page = pages[0];
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
	int srcu_idx;

	put_page(page);

	if (!dirty)
		return;

	/* We need to mark this page dirty in the memslot dirty_bitmap, if any */
	gfn = gpa >> PAGE_SHIFT;
	srcu_idx = srcu_read_lock(&kvm->srcu);
	memslot = gfn_to_memslot(kvm, gfn);
	if (memslot && memslot->dirty_bitmap)
		set_bit_le(gfn - memslot->base_gfn, memslot->dirty_bitmap);
	srcu_read_unlock(&kvm->srcu, srcu_idx);
}

/*
 * HPT resizing
 */
static int resize_hpt_allocate(struct kvm_resize_hpt *resize)
{
	int rc;

	rc = kvmppc_allocate_hpt(&resize->hpt, resize->order);
	if (rc < 0)
		return rc;

	resize_hpt_debug(resize, "resize_hpt_allocate(): HPT @ 0x%lx\n",
			 resize->hpt.virt);

	return 0;
}

static unsigned long resize_hpt_rehash_hpte(struct kvm_resize_hpt *resize,
					    unsigned long idx)
{
	struct kvm *kvm = resize->kvm;
	struct kvm_hpt_info *old = &kvm->arch.hpt;
	struct kvm_hpt_info *new = &resize->hpt;
	unsigned long old_hash_mask = (1ULL << (old->order - 7)) - 1;
	unsigned long new_hash_mask = (1ULL << (new->order - 7)) - 1;
	__be64 *hptep, *new_hptep;
	unsigned long vpte, rpte, guest_rpte;
	int ret;
	struct revmap_entry *rev;
	unsigned long apsize, avpn, pteg, hash;
	unsigned long new_idx, new_pteg, replace_vpte;
	int pshift;

	hptep = (__be64 *)(old->virt + (idx << 4));

	/* Guest is stopped, so new HPTEs can't be added or faulted
	 * in, only unmapped or altered by host actions.  So, it's
	 * safe to check this before we take the HPTE lock */
	vpte = be64_to_cpu(hptep[0]);
	if (!(vpte & HPTE_V_VALID) && !(vpte & HPTE_V_ABSENT))
		return 0; /* nothing to do */

	while (!try_lock_hpte(hptep, HPTE_V_HVLOCK))
		cpu_relax();

	vpte = be64_to_cpu(hptep[0]);

	ret = 0;
	if (!(vpte & HPTE_V_VALID) && !(vpte & HPTE_V_ABSENT))
		/* Nothing to do */
		goto out;

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		rpte = be64_to_cpu(hptep[1]);
		vpte = hpte_new_to_old_v(vpte, rpte);
	}

	/* Unmap */
	rev = &old->rev[idx];
	guest_rpte = rev->guest_rpte;

	ret = -EIO;
	apsize = kvmppc_actual_pgsz(vpte, guest_rpte);
	if (!apsize)
		goto out;

	if (vpte & HPTE_V_VALID) {
		unsigned long gfn = hpte_rpn(guest_rpte, apsize);
		int srcu_idx = srcu_read_lock(&kvm->srcu);
		struct kvm_memory_slot *memslot =
			__gfn_to_memslot(kvm_memslots(kvm), gfn);

		if (memslot) {
			unsigned long *rmapp;
			rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];

			lock_rmap(rmapp);
			kvmppc_unmap_hpte(kvm, idx, memslot, rmapp, gfn);
			unlock_rmap(rmapp);
		}

		srcu_read_unlock(&kvm->srcu, srcu_idx);
	}

	/* Reload PTE after unmap */
	vpte = be64_to_cpu(hptep[0]);
	BUG_ON(vpte & HPTE_V_VALID);
	BUG_ON(!(vpte & HPTE_V_ABSENT));

	ret = 0;
	if (!(vpte & HPTE_V_BOLTED))
		goto out;

	rpte = be64_to_cpu(hptep[1]);

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		vpte = hpte_new_to_old_v(vpte, rpte);
		rpte = hpte_new_to_old_r(rpte);
	}

	pshift = kvmppc_hpte_base_page_shift(vpte, rpte);
	avpn = HPTE_V_AVPN_VAL(vpte) & ~(((1ul << pshift) - 1) >> 23);
	pteg = idx / HPTES_PER_GROUP;
	if (vpte & HPTE_V_SECONDARY)
		pteg = ~pteg;

	if (!(vpte & HPTE_V_1TB_SEG)) {
		unsigned long offset, vsid;

		/* We only have 28 - 23 bits of offset in avpn */
		offset = (avpn & 0x1f) << 23;
		vsid = avpn >> 5;
		/* We can find more bits from the pteg value */
		if (pshift < 23)
			offset |= ((vsid ^ pteg) & old_hash_mask) << pshift;

		hash = vsid ^ (offset >> pshift);
	} else {
		unsigned long offset, vsid;

		/* We only have 40 - 23 bits of seg_off in avpn */
		offset = (avpn & 0x1ffff) << 23;
		vsid = avpn >> 17;
		if (pshift < 23)
			offset |= ((vsid ^ (vsid << 25) ^ pteg) & old_hash_mask) << pshift;

		hash = vsid ^ (vsid << 25) ^ (offset >> pshift);
	}

	new_pteg = hash & new_hash_mask;
	if (vpte & HPTE_V_SECONDARY)
		new_pteg = ~hash & new_hash_mask;

	new_idx = new_pteg * HPTES_PER_GROUP + (idx % HPTES_PER_GROUP);
	new_hptep = (__be64 *)(new->virt + (new_idx << 4));

	replace_vpte = be64_to_cpu(new_hptep[0]);
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		unsigned long replace_rpte = be64_to_cpu(new_hptep[1]);
		replace_vpte = hpte_new_to_old_v(replace_vpte, replace_rpte);
	}

	if (replace_vpte & (HPTE_V_VALID | HPTE_V_ABSENT)) {
		BUG_ON(new->order >= old->order);

		if (replace_vpte & HPTE_V_BOLTED) {
			if (vpte & HPTE_V_BOLTED)
				/* Bolted collision, nothing we can do */
				ret = -ENOSPC;
			/* Discard the new HPTE */
			goto out;
		}

		/* Discard the previous HPTE */
	}

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		rpte = hpte_old_to_new_r(vpte, rpte);
		vpte = hpte_old_to_new_v(vpte);
	}

	new_hptep[1] = cpu_to_be64(rpte);
	new->rev[new_idx].guest_rpte = guest_rpte;
	/* No need for a barrier, since new HPT isn't active */
	new_hptep[0] = cpu_to_be64(vpte);
	unlock_hpte(new_hptep, vpte);

out:
	unlock_hpte(hptep, vpte);
	return ret;
}

static int resize_hpt_rehash(struct kvm_resize_hpt *resize)
{
	struct kvm *kvm = resize->kvm;
	unsigned  long i;
	int rc;

	for (i = 0; i < kvmppc_hpt_npte(&kvm->arch.hpt); i++) {
		rc = resize_hpt_rehash_hpte(resize, i);
		if (rc != 0)
			return rc;
	}

	return 0;
}

static void resize_hpt_pivot(struct kvm_resize_hpt *resize)
{
	struct kvm *kvm = resize->kvm;
	struct kvm_hpt_info hpt_tmp;

	/* Exchange the pending tables in the resize structure with
	 * the active tables */

	resize_hpt_debug(resize, "resize_hpt_pivot()\n");

	spin_lock(&kvm->mmu_lock);
	asm volatile("ptesync" : : : "memory");

	hpt_tmp = kvm->arch.hpt;
	kvmppc_set_hpt(kvm, &resize->hpt);
	resize->hpt = hpt_tmp;

	spin_unlock(&kvm->mmu_lock);

	synchronize_srcu_expedited(&kvm->srcu);

	if (cpu_has_feature(CPU_FTR_ARCH_300))
		kvmppc_setup_partition_table(kvm);

	resize_hpt_debug(resize, "resize_hpt_pivot() done\n");
}

static void resize_hpt_release(struct kvm *kvm, struct kvm_resize_hpt *resize)
{
	if (WARN_ON(!mutex_is_locked(&kvm->lock)))
		return;

	if (!resize)
		return;

	if (resize->error != -EBUSY) {
		if (resize->hpt.virt)
			kvmppc_free_hpt(&resize->hpt);
		kfree(resize);
	}

	if (kvm->arch.resize_hpt == resize)
		kvm->arch.resize_hpt = NULL;
}

static void resize_hpt_prepare_work(struct work_struct *work)
{
	struct kvm_resize_hpt *resize = container_of(work,
						     struct kvm_resize_hpt,
						     work);
	struct kvm *kvm = resize->kvm;
	int err = 0;

	if (WARN_ON(resize->error != -EBUSY))
		return;

	mutex_lock(&kvm->lock);

	/* Request is still current? */
	if (kvm->arch.resize_hpt == resize) {
		/* We may request large allocations here:
		 * do not sleep with kvm->lock held for a while.
		 */
		mutex_unlock(&kvm->lock);

		resize_hpt_debug(resize, "resize_hpt_prepare_work(): order = %d\n",
				 resize->order);

		err = resize_hpt_allocate(resize);

		/* We have strict assumption about -EBUSY
		 * when preparing for HPT resize.
		 */
		if (WARN_ON(err == -EBUSY))
			err = -EINPROGRESS;

		mutex_lock(&kvm->lock);
		/* It is possible that kvm->arch.resize_hpt != resize
		 * after we grab kvm->lock again.
		 */
	}

	resize->error = err;

	if (kvm->arch.resize_hpt != resize)
		resize_hpt_release(kvm, resize);

	mutex_unlock(&kvm->lock);
}

long kvm_vm_ioctl_resize_hpt_prepare(struct kvm *kvm,
				     struct kvm_ppc_resize_hpt *rhpt)
{
	unsigned long flags = rhpt->flags;
	unsigned long shift = rhpt->shift;
	struct kvm_resize_hpt *resize;
	int ret;

	if (flags != 0 || kvm_is_radix(kvm))
		return -EINVAL;

	if (shift && ((shift < 18) || (shift > 46)))
		return -EINVAL;

	mutex_lock(&kvm->lock);

	resize = kvm->arch.resize_hpt;

	if (resize) {
		if (resize->order == shift) {
			/* Suitable resize in progress? */
			ret = resize->error;
			if (ret == -EBUSY)
				ret = 100; /* estimated time in ms */
			else if (ret)
				resize_hpt_release(kvm, resize);

			goto out;
		}

		/* not suitable, cancel it */
		resize_hpt_release(kvm, resize);
	}

	ret = 0;
	if (!shift)
		goto out; /* nothing to do */

	/* start new resize */

	resize = kzalloc(sizeof(*resize), GFP_KERNEL);
	if (!resize) {
		ret = -ENOMEM;
		goto out;
	}

	resize->error = -EBUSY;
	resize->order = shift;
	resize->kvm = kvm;
	INIT_WORK(&resize->work, resize_hpt_prepare_work);
	kvm->arch.resize_hpt = resize;

	schedule_work(&resize->work);

	ret = 100; /* estimated time in ms */

out:
	mutex_unlock(&kvm->lock);
	return ret;
}

static void resize_hpt_boot_vcpu(void *opaque)
{
	/* Nothing to do, just force a KVM exit */
}

long kvm_vm_ioctl_resize_hpt_commit(struct kvm *kvm,
				    struct kvm_ppc_resize_hpt *rhpt)
{
	unsigned long flags = rhpt->flags;
	unsigned long shift = rhpt->shift;
	struct kvm_resize_hpt *resize;
	long ret;

	if (flags != 0 || kvm_is_radix(kvm))
		return -EINVAL;

	if (shift && ((shift < 18) || (shift > 46)))
		return -EINVAL;

	mutex_lock(&kvm->lock);

	resize = kvm->arch.resize_hpt;

	/* This shouldn't be possible */
	ret = -EIO;
	if (WARN_ON(!kvm->arch.mmu_ready))
		goto out_no_hpt;

	/* Stop VCPUs from running while we mess with the HPT */
	kvm->arch.mmu_ready = 0;
	smp_mb();

	/* Boot all CPUs out of the guest so they re-read
	 * mmu_ready */
	on_each_cpu(resize_hpt_boot_vcpu, NULL, 1);

	ret = -ENXIO;
	if (!resize || (resize->order != shift))
		goto out;

	ret = resize->error;
	if (ret)
		goto out;

	ret = resize_hpt_rehash(resize);
	if (ret)
		goto out;

	resize_hpt_pivot(resize);

out:
	/* Let VCPUs run again */
	kvm->arch.mmu_ready = 1;
	smp_mb();
out_no_hpt:
	resize_hpt_release(kvm, resize);
	mutex_unlock(&kvm->lock);
	return ret;
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
	unsigned long v, r, hr;
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
		hr = be64_to_cpu(hptp[1]);
		if (cpu_has_feature(CPU_FTR_ARCH_300)) {
			v = hpte_new_to_old_v(v, hr);
			hr = hpte_new_to_old_r(hr);
		}

		/* re-evaluate valid and dirty from synchronized HPTE value */
		valid = !!(v & HPTE_V_VALID);
		dirty = !!(revp->guest_rpte & HPTE_GR_MODIFIED);

		/* Harvest R and C into guest view if necessary */
		rcbits_unset = ~revp->guest_rpte & (HPTE_R_R | HPTE_R_C);
		if (valid && (rcbits_unset & hr)) {
			revp->guest_rpte |= (hr &
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
		unlock_hpte(hptp, be64_to_cpu(hptp[0]));
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
	if (kvm_is_radix(kvm))
		return 0;

	first_pass = ctx->first_pass;
	flags = ctx->flags;

	i = ctx->index;
	hptp = (__be64 *)(kvm->arch.hpt.virt + (i * HPTE_SIZE));
	revp = kvm->arch.hpt.rev + i;
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
			while (i < kvmppc_hpt_npte(&kvm->arch.hpt) &&
			       !hpte_dirty(revp, hptp)) {
				++i;
				hptp += 2;
				++revp;
			}
		}
		hdr.index = i;

		/* Grab a series of valid entries */
		while (i < kvmppc_hpt_npte(&kvm->arch.hpt) &&
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
		while (i < kvmppc_hpt_npte(&kvm->arch.hpt) &&
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
		if (i >= kvmppc_hpt_npte(&kvm->arch.hpt)) {
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
	int mmu_ready;
	int pshift;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	if (kvm_is_radix(kvm))
		return -EINVAL;

	/* lock out vcpus from running while we're doing this */
	mutex_lock(&kvm->lock);
	mmu_ready = kvm->arch.mmu_ready;
	if (mmu_ready) {
		kvm->arch.mmu_ready = 0;	/* temporarily */
		/* order mmu_ready vs. vcpus_running */
		smp_mb();
		if (atomic_read(&kvm->arch.vcpus_running)) {
			kvm->arch.mmu_ready = 1;
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
		if (i >= kvmppc_hpt_npte(&kvm->arch.hpt) ||
		    i + hdr.n_valid + hdr.n_invalid > kvmppc_hpt_npte(&kvm->arch.hpt))
			break;

		hptp = (__be64 *)(kvm->arch.hpt.virt + (i * HPTE_SIZE));
		lbuf = (unsigned long __user *)buf;
		for (j = 0; j < hdr.n_valid; ++j) {
			__be64 hpte_v;
			__be64 hpte_r;

			err = -EFAULT;
			if (__get_user(hpte_v, lbuf) ||
			    __get_user(hpte_r, lbuf + 1))
				goto out;
			v = be64_to_cpu(hpte_v);
			r = be64_to_cpu(hpte_r);
			err = -EINVAL;
			if (!(v & HPTE_V_VALID))
				goto out;
			pshift = kvmppc_hpte_base_page_shift(v, r);
			if (pshift <= 0)
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
			if (!mmu_ready && is_vrma_hpte(v)) {
				unsigned long senc, lpcr;

				senc = slb_pgsize_encoding(1ul << pshift);
				kvm->arch.vrma_slb_v = senc | SLB_VSID_B_1T |
					(VRMA_VSID << SLB_VSID_SHIFT_1T);
				if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
					lpcr = senc << (LPCR_VRMASD_SH - 4);
					kvmppc_update_lpcr(kvm, lpcr,
							   LPCR_VRMASD);
				} else {
					kvmppc_setup_partition_table(kvm);
				}
				mmu_ready = 1;
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
	/* Order HPTE updates vs. mmu_ready */
	smp_wmb();
	kvm->arch.mmu_ready = mmu_ready;
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
		kfree(ctx);
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

struct debugfs_htab_state {
	struct kvm	*kvm;
	struct mutex	mutex;
	unsigned long	hpt_index;
	int		chars_left;
	int		buf_index;
	char		buf[64];
};

static int debugfs_htab_open(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	struct debugfs_htab_state *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	kvm_get_kvm(kvm);
	p->kvm = kvm;
	mutex_init(&p->mutex);
	file->private_data = p;

	return nonseekable_open(inode, file);
}

static int debugfs_htab_release(struct inode *inode, struct file *file)
{
	struct debugfs_htab_state *p = file->private_data;

	kvm_put_kvm(p->kvm);
	kfree(p);
	return 0;
}

static ssize_t debugfs_htab_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct debugfs_htab_state *p = file->private_data;
	ssize_t ret, r;
	unsigned long i, n;
	unsigned long v, hr, gr;
	struct kvm *kvm;
	__be64 *hptp;

	kvm = p->kvm;
	if (kvm_is_radix(kvm))
		return 0;

	ret = mutex_lock_interruptible(&p->mutex);
	if (ret)
		return ret;

	if (p->chars_left) {
		n = p->chars_left;
		if (n > len)
			n = len;
		r = copy_to_user(buf, p->buf + p->buf_index, n);
		n -= r;
		p->chars_left -= n;
		p->buf_index += n;
		buf += n;
		len -= n;
		ret = n;
		if (r) {
			if (!n)
				ret = -EFAULT;
			goto out;
		}
	}

	i = p->hpt_index;
	hptp = (__be64 *)(kvm->arch.hpt.virt + (i * HPTE_SIZE));
	for (; len != 0 && i < kvmppc_hpt_npte(&kvm->arch.hpt);
	     ++i, hptp += 2) {
		if (!(be64_to_cpu(hptp[0]) & (HPTE_V_VALID | HPTE_V_ABSENT)))
			continue;

		/* lock the HPTE so it's stable and read it */
		preempt_disable();
		while (!try_lock_hpte(hptp, HPTE_V_HVLOCK))
			cpu_relax();
		v = be64_to_cpu(hptp[0]) & ~HPTE_V_HVLOCK;
		hr = be64_to_cpu(hptp[1]);
		gr = kvm->arch.hpt.rev[i].guest_rpte;
		unlock_hpte(hptp, v);
		preempt_enable();

		if (!(v & (HPTE_V_VALID | HPTE_V_ABSENT)))
			continue;

		n = scnprintf(p->buf, sizeof(p->buf),
			      "%6lx %.16lx %.16lx %.16lx\n",
			      i, v, hr, gr);
		p->chars_left = n;
		if (n > len)
			n = len;
		r = copy_to_user(buf, p->buf, n);
		n -= r;
		p->chars_left -= n;
		p->buf_index = n;
		buf += n;
		len -= n;
		ret += n;
		if (r) {
			if (!ret)
				ret = -EFAULT;
			goto out;
		}
	}
	p->hpt_index = i;

 out:
	mutex_unlock(&p->mutex);
	return ret;
}

static ssize_t debugfs_htab_write(struct file *file, const char __user *buf,
			   size_t len, loff_t *ppos)
{
	return -EACCES;
}

static const struct file_operations debugfs_htab_fops = {
	.owner	 = THIS_MODULE,
	.open	 = debugfs_htab_open,
	.release = debugfs_htab_release,
	.read	 = debugfs_htab_read,
	.write	 = debugfs_htab_write,
	.llseek	 = generic_file_llseek,
};

void kvmppc_mmu_debugfs_init(struct kvm *kvm)
{
	kvm->arch.htab_dentry = debugfs_create_file("htab", 0400,
						    kvm->arch.debugfs_dir, kvm,
						    &debugfs_htab_fops);
}

void kvmppc_mmu_book3s_hv_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_mmu *mmu = &vcpu->arch.mmu;

	vcpu->arch.slb_nr = 32;		/* POWER7/POWER8 */

	mmu->xlate = kvmppc_mmu_book3s_64_hv_xlate;
	mmu->reset_msr = kvmppc_mmu_book3s_64_hv_reset_msr;

	vcpu->arch.hflags |= BOOK3S_HFLAG_SLB;
}
