// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright 2016 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/debugfs.h>
#include <linux/pgtable.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>
#include <asm/pte-walk.h>
#include <asm/ultravisor.h>
#include <asm/kvm_book3s_uvmem.h>

/*
 * Supported radix tree geometry.
 * Like p9, we support either 5 or 9 bits at the first (lowest) level,
 * for a page size of 64k or 4k.
 */
static int p9_supported_radix_bits[4] = { 5, 9, 9, 13 };

unsigned long __kvmhv_copy_tofrom_guest_radix(int lpid, int pid,
					      gva_t eaddr, void *to, void *from,
					      unsigned long n)
{
	int old_pid, old_lpid;
	unsigned long quadrant, ret = n;
	bool is_load = !!to;

	/* Can't access quadrants 1 or 2 in non-HV mode, call the HV to do it */
	if (kvmhv_on_pseries())
		return plpar_hcall_norets(H_COPY_TOFROM_GUEST, lpid, pid, eaddr,
					  (to != NULL) ? __pa(to): 0,
					  (from != NULL) ? __pa(from): 0, n);

	quadrant = 1;
	if (!pid)
		quadrant = 2;
	if (is_load)
		from = (void *) (eaddr | (quadrant << 62));
	else
		to = (void *) (eaddr | (quadrant << 62));

	preempt_disable();

	/* switch the lpid first to avoid running host with unallocated pid */
	old_lpid = mfspr(SPRN_LPID);
	if (old_lpid != lpid)
		mtspr(SPRN_LPID, lpid);
	if (quadrant == 1) {
		old_pid = mfspr(SPRN_PID);
		if (old_pid != pid)
			mtspr(SPRN_PID, pid);
	}
	isync();

	if (is_load)
		ret = copy_from_user_nofault(to, (const void __user *)from, n);
	else
		ret = copy_to_user_nofault((void __user *)to, from, n);

	/* switch the pid first to avoid running host with unallocated pid */
	if (quadrant == 1 && pid != old_pid)
		mtspr(SPRN_PID, old_pid);
	if (lpid != old_lpid)
		mtspr(SPRN_LPID, old_lpid);
	isync();

	preempt_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(__kvmhv_copy_tofrom_guest_radix);

static long kvmhv_copy_tofrom_guest_radix(struct kvm_vcpu *vcpu, gva_t eaddr,
					  void *to, void *from, unsigned long n)
{
	int lpid = vcpu->kvm->arch.lpid;
	int pid = vcpu->arch.pid;

	/* This would cause a data segment intr so don't allow the access */
	if (eaddr & (0x3FFUL << 52))
		return -EINVAL;

	/* Should we be using the nested lpid */
	if (vcpu->arch.nested)
		lpid = vcpu->arch.nested->shadow_lpid;

	/* If accessing quadrant 3 then pid is expected to be 0 */
	if (((eaddr >> 62) & 0x3) == 0x3)
		pid = 0;

	eaddr &= ~(0xFFFUL << 52);

	return __kvmhv_copy_tofrom_guest_radix(lpid, pid, eaddr, to, from, n);
}

long kvmhv_copy_from_guest_radix(struct kvm_vcpu *vcpu, gva_t eaddr, void *to,
				 unsigned long n)
{
	long ret;

	ret = kvmhv_copy_tofrom_guest_radix(vcpu, eaddr, to, NULL, n);
	if (ret > 0)
		memset(to + (n - ret), 0, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(kvmhv_copy_from_guest_radix);

long kvmhv_copy_to_guest_radix(struct kvm_vcpu *vcpu, gva_t eaddr, void *from,
			       unsigned long n)
{
	return kvmhv_copy_tofrom_guest_radix(vcpu, eaddr, NULL, from, n);
}
EXPORT_SYMBOL_GPL(kvmhv_copy_to_guest_radix);

int kvmppc_mmu_walk_radix_tree(struct kvm_vcpu *vcpu, gva_t eaddr,
			       struct kvmppc_pte *gpte, u64 root,
			       u64 *pte_ret_p)
{
	struct kvm *kvm = vcpu->kvm;
	int ret, level, ps;
	unsigned long rts, bits, offset, index;
	u64 pte, base, gpa;
	__be64 rpte;

	rts = ((root & RTS1_MASK) >> (RTS1_SHIFT - 3)) |
		((root & RTS2_MASK) >> RTS2_SHIFT);
	bits = root & RPDS_MASK;
	base = root & RPDB_MASK;

	offset = rts + 31;

	/* Current implementations only support 52-bit space */
	if (offset != 52)
		return -EINVAL;

	/* Walk each level of the radix tree */
	for (level = 3; level >= 0; --level) {
		u64 addr;
		/* Check a valid size */
		if (level && bits != p9_supported_radix_bits[level])
			return -EINVAL;
		if (level == 0 && !(bits == 5 || bits == 9))
			return -EINVAL;
		offset -= bits;
		index = (eaddr >> offset) & ((1UL << bits) - 1);
		/* Check that low bits of page table base are zero */
		if (base & ((1UL << (bits + 3)) - 1))
			return -EINVAL;
		/* Read the entry from guest memory */
		addr = base + (index * sizeof(rpte));
		vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
		ret = kvm_read_guest(kvm, addr, &rpte, sizeof(rpte));
		srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
		if (ret) {
			if (pte_ret_p)
				*pte_ret_p = addr;
			return ret;
		}
		pte = __be64_to_cpu(rpte);
		if (!(pte & _PAGE_PRESENT))
			return -ENOENT;
		/* Check if a leaf entry */
		if (pte & _PAGE_PTE)
			break;
		/* Get ready to walk the next level */
		base = pte & RPDB_MASK;
		bits = pte & RPDS_MASK;
	}

	/* Need a leaf at lowest level; 512GB pages not supported */
	if (level < 0 || level == 3)
		return -EINVAL;

	/* We found a valid leaf PTE */
	/* Offset is now log base 2 of the page size */
	gpa = pte & 0x01fffffffffff000ul;
	if (gpa & ((1ul << offset) - 1))
		return -EINVAL;
	gpa |= eaddr & ((1ul << offset) - 1);
	for (ps = MMU_PAGE_4K; ps < MMU_PAGE_COUNT; ++ps)
		if (offset == mmu_psize_defs[ps].shift)
			break;
	gpte->page_size = ps;
	gpte->page_shift = offset;

	gpte->eaddr = eaddr;
	gpte->raddr = gpa;

	/* Work out permissions */
	gpte->may_read = !!(pte & _PAGE_READ);
	gpte->may_write = !!(pte & _PAGE_WRITE);
	gpte->may_execute = !!(pte & _PAGE_EXEC);

	gpte->rc = pte & (_PAGE_ACCESSED | _PAGE_DIRTY);

	if (pte_ret_p)
		*pte_ret_p = pte;

	return 0;
}

/*
 * Used to walk a partition or process table radix tree in guest memory
 * Note: We exploit the fact that a partition table and a process
 * table have the same layout, a partition-scoped page table and a
 * process-scoped page table have the same layout, and the 2nd
 * doubleword of a partition table entry has the same layout as
 * the PTCR register.
 */
int kvmppc_mmu_radix_translate_table(struct kvm_vcpu *vcpu, gva_t eaddr,
				     struct kvmppc_pte *gpte, u64 table,
				     int table_index, u64 *pte_ret_p)
{
	struct kvm *kvm = vcpu->kvm;
	int ret;
	unsigned long size, ptbl, root;
	struct prtb_entry entry;

	if ((table & PRTS_MASK) > 24)
		return -EINVAL;
	size = 1ul << ((table & PRTS_MASK) + 12);

	/* Is the table big enough to contain this entry? */
	if ((table_index * sizeof(entry)) >= size)
		return -EINVAL;

	/* Read the table to find the root of the radix tree */
	ptbl = (table & PRTB_MASK) + (table_index * sizeof(entry));
	vcpu->srcu_idx = srcu_read_lock(&kvm->srcu);
	ret = kvm_read_guest(kvm, ptbl, &entry, sizeof(entry));
	srcu_read_unlock(&kvm->srcu, vcpu->srcu_idx);
	if (ret)
		return ret;

	/* Root is stored in the first double word */
	root = be64_to_cpu(entry.prtb0);

	return kvmppc_mmu_walk_radix_tree(vcpu, eaddr, gpte, root, pte_ret_p);
}

int kvmppc_mmu_radix_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
			   struct kvmppc_pte *gpte, bool data, bool iswrite)
{
	u32 pid;
	u64 pte;
	int ret;

	/* Work out effective PID */
	switch (eaddr >> 62) {
	case 0:
		pid = vcpu->arch.pid;
		break;
	case 3:
		pid = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = kvmppc_mmu_radix_translate_table(vcpu, eaddr, gpte,
				vcpu->kvm->arch.process_table, pid, &pte);
	if (ret)
		return ret;

	/* Check privilege (applies only to process scoped translations) */
	if (kvmppc_get_msr(vcpu) & MSR_PR) {
		if (pte & _PAGE_PRIVILEGED) {
			gpte->may_read = 0;
			gpte->may_write = 0;
			gpte->may_execute = 0;
		}
	} else {
		if (!(pte & _PAGE_PRIVILEGED)) {
			/* Check AMR/IAMR to see if strict mode is in force */
			if (vcpu->arch.amr & (1ul << 62))
				gpte->may_read = 0;
			if (vcpu->arch.amr & (1ul << 63))
				gpte->may_write = 0;
			if (vcpu->arch.iamr & (1ul << 62))
				gpte->may_execute = 0;
		}
	}

	return 0;
}

void kvmppc_radix_tlbie_page(struct kvm *kvm, unsigned long addr,
			     unsigned int pshift, unsigned int lpid)
{
	unsigned long psize = PAGE_SIZE;
	int psi;
	long rc;
	unsigned long rb;

	if (pshift)
		psize = 1UL << pshift;
	else
		pshift = PAGE_SHIFT;

	addr &= ~(psize - 1);

	if (!kvmhv_on_pseries()) {
		radix__flush_tlb_lpid_page(lpid, addr, psize);
		return;
	}

	psi = shift_to_mmu_psize(pshift);
	rb = addr | (mmu_get_ap(psi) << PPC_BITLSHIFT(58));
	rc = plpar_hcall_norets(H_TLB_INVALIDATE, H_TLBIE_P1_ENC(0, 0, 1),
				lpid, rb);
	if (rc)
		pr_err("KVM: TLB page invalidation hcall failed, rc=%ld\n", rc);
}

static void kvmppc_radix_flush_pwc(struct kvm *kvm, unsigned int lpid)
{
	long rc;

	if (!kvmhv_on_pseries()) {
		radix__flush_pwc_lpid(lpid);
		return;
	}

	rc = plpar_hcall_norets(H_TLB_INVALIDATE, H_TLBIE_P1_ENC(1, 0, 1),
				lpid, TLBIEL_INVAL_SET_LPID);
	if (rc)
		pr_err("KVM: TLB PWC invalidation hcall failed, rc=%ld\n", rc);
}

static unsigned long kvmppc_radix_update_pte(struct kvm *kvm, pte_t *ptep,
				      unsigned long clr, unsigned long set,
				      unsigned long addr, unsigned int shift)
{
	return __radix_pte_update(ptep, clr, set);
}

static void kvmppc_radix_set_pte_at(struct kvm *kvm, unsigned long addr,
			     pte_t *ptep, pte_t pte)
{
	radix__set_pte_at(kvm->mm, addr, ptep, pte, 0);
}

static struct kmem_cache *kvm_pte_cache;
static struct kmem_cache *kvm_pmd_cache;

static pte_t *kvmppc_pte_alloc(void)
{
	pte_t *pte;

	pte = kmem_cache_alloc(kvm_pte_cache, GFP_KERNEL);
	/* pmd_populate() will only reference _pa(pte). */
	kmemleak_ignore(pte);

	return pte;
}

static void kvmppc_pte_free(pte_t *ptep)
{
	kmem_cache_free(kvm_pte_cache, ptep);
}

static pmd_t *kvmppc_pmd_alloc(void)
{
	pmd_t *pmd;

	pmd = kmem_cache_alloc(kvm_pmd_cache, GFP_KERNEL);
	/* pud_populate() will only reference _pa(pmd). */
	kmemleak_ignore(pmd);

	return pmd;
}

static void kvmppc_pmd_free(pmd_t *pmdp)
{
	kmem_cache_free(kvm_pmd_cache, pmdp);
}

/* Called with kvm->mmu_lock held */
void kvmppc_unmap_pte(struct kvm *kvm, pte_t *pte, unsigned long gpa,
		      unsigned int shift,
		      const struct kvm_memory_slot *memslot,
		      unsigned int lpid)

{
	unsigned long old;
	unsigned long gfn = gpa >> PAGE_SHIFT;
	unsigned long page_size = PAGE_SIZE;
	unsigned long hpa;

	old = kvmppc_radix_update_pte(kvm, pte, ~0UL, 0, gpa, shift);
	kvmppc_radix_tlbie_page(kvm, gpa, shift, lpid);

	/* The following only applies to L1 entries */
	if (lpid != kvm->arch.lpid)
		return;

	if (!memslot) {
		memslot = gfn_to_memslot(kvm, gfn);
		if (!memslot)
			return;
	}
	if (shift) { /* 1GB or 2MB page */
		page_size = 1ul << shift;
		if (shift == PMD_SHIFT)
			kvm->stat.num_2M_pages--;
		else if (shift == PUD_SHIFT)
			kvm->stat.num_1G_pages--;
	}

	gpa &= ~(page_size - 1);
	hpa = old & PTE_RPN_MASK;
	kvmhv_remove_nest_rmap_range(kvm, memslot, gpa, hpa, page_size);

	if ((old & _PAGE_DIRTY) && memslot->dirty_bitmap)
		kvmppc_update_dirty_map(memslot, gfn, page_size);
}

/*
 * kvmppc_free_p?d are used to free existing page tables, and recursively
 * descend and clear and free children.
 * Callers are responsible for flushing the PWC.
 *
 * When page tables are being unmapped/freed as part of page fault path
 * (full == false), valid ptes are generally not expected; however, there
 * is one situation where they arise, which is when dirty page logging is
 * turned off for a memslot while the VM is running.  The new memslot
 * becomes visible to page faults before the memslot commit function
 * gets to flush the memslot, which can lead to a 2MB page mapping being
 * installed for a guest physical address where there are already 64kB
 * (or 4kB) mappings (of sub-pages of the same 2MB page).
 */
static void kvmppc_unmap_free_pte(struct kvm *kvm, pte_t *pte, bool full,
				  unsigned int lpid)
{
	if (full) {
		memset(pte, 0, sizeof(long) << RADIX_PTE_INDEX_SIZE);
	} else {
		pte_t *p = pte;
		unsigned long it;

		for (it = 0; it < PTRS_PER_PTE; ++it, ++p) {
			if (pte_val(*p) == 0)
				continue;
			kvmppc_unmap_pte(kvm, p,
					 pte_pfn(*p) << PAGE_SHIFT,
					 PAGE_SHIFT, NULL, lpid);
		}
	}

	kvmppc_pte_free(pte);
}

static void kvmppc_unmap_free_pmd(struct kvm *kvm, pmd_t *pmd, bool full,
				  unsigned int lpid)
{
	unsigned long im;
	pmd_t *p = pmd;

	for (im = 0; im < PTRS_PER_PMD; ++im, ++p) {
		if (!pmd_present(*p))
			continue;
		if (pmd_is_leaf(*p)) {
			if (full) {
				pmd_clear(p);
			} else {
				WARN_ON_ONCE(1);
				kvmppc_unmap_pte(kvm, (pte_t *)p,
					 pte_pfn(*(pte_t *)p) << PAGE_SHIFT,
					 PMD_SHIFT, NULL, lpid);
			}
		} else {
			pte_t *pte;

			pte = pte_offset_map(p, 0);
			kvmppc_unmap_free_pte(kvm, pte, full, lpid);
			pmd_clear(p);
		}
	}
	kvmppc_pmd_free(pmd);
}

static void kvmppc_unmap_free_pud(struct kvm *kvm, pud_t *pud,
				  unsigned int lpid)
{
	unsigned long iu;
	pud_t *p = pud;

	for (iu = 0; iu < PTRS_PER_PUD; ++iu, ++p) {
		if (!pud_present(*p))
			continue;
		if (pud_is_leaf(*p)) {
			pud_clear(p);
		} else {
			pmd_t *pmd;

			pmd = pmd_offset(p, 0);
			kvmppc_unmap_free_pmd(kvm, pmd, true, lpid);
			pud_clear(p);
		}
	}
	pud_free(kvm->mm, pud);
}

void kvmppc_free_pgtable_radix(struct kvm *kvm, pgd_t *pgd, unsigned int lpid)
{
	unsigned long ig;

	for (ig = 0; ig < PTRS_PER_PGD; ++ig, ++pgd) {
		p4d_t *p4d = p4d_offset(pgd, 0);
		pud_t *pud;

		if (!p4d_present(*p4d))
			continue;
		pud = pud_offset(p4d, 0);
		kvmppc_unmap_free_pud(kvm, pud, lpid);
		p4d_clear(p4d);
	}
}

void kvmppc_free_radix(struct kvm *kvm)
{
	if (kvm->arch.pgtable) {
		kvmppc_free_pgtable_radix(kvm, kvm->arch.pgtable,
					  kvm->arch.lpid);
		pgd_free(kvm->mm, kvm->arch.pgtable);
		kvm->arch.pgtable = NULL;
	}
}

static void kvmppc_unmap_free_pmd_entry_table(struct kvm *kvm, pmd_t *pmd,
					unsigned long gpa, unsigned int lpid)
{
	pte_t *pte = pte_offset_kernel(pmd, 0);

	/*
	 * Clearing the pmd entry then flushing the PWC ensures that the pte
	 * page no longer be cached by the MMU, so can be freed without
	 * flushing the PWC again.
	 */
	pmd_clear(pmd);
	kvmppc_radix_flush_pwc(kvm, lpid);

	kvmppc_unmap_free_pte(kvm, pte, false, lpid);
}

static void kvmppc_unmap_free_pud_entry_table(struct kvm *kvm, pud_t *pud,
					unsigned long gpa, unsigned int lpid)
{
	pmd_t *pmd = pmd_offset(pud, 0);

	/*
	 * Clearing the pud entry then flushing the PWC ensures that the pmd
	 * page and any children pte pages will no longer be cached by the MMU,
	 * so can be freed without flushing the PWC again.
	 */
	pud_clear(pud);
	kvmppc_radix_flush_pwc(kvm, lpid);

	kvmppc_unmap_free_pmd(kvm, pmd, false, lpid);
}

/*
 * There are a number of bits which may differ between different faults to
 * the same partition scope entry. RC bits, in the course of cleaning and
 * aging. And the write bit can change, either the access could have been
 * upgraded, or a read fault could happen concurrently with a write fault
 * that sets those bits first.
 */
#define PTE_BITS_MUST_MATCH (~(_PAGE_WRITE | _PAGE_DIRTY | _PAGE_ACCESSED))

int kvmppc_create_pte(struct kvm *kvm, pgd_t *pgtable, pte_t pte,
		      unsigned long gpa, unsigned int level,
		      unsigned long mmu_seq, unsigned int lpid,
		      unsigned long *rmapp, struct rmap_nested **n_rmap)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud, *new_pud = NULL;
	pmd_t *pmd, *new_pmd = NULL;
	pte_t *ptep, *new_ptep = NULL;
	int ret;

	/* Traverse the guest's 2nd-level tree, allocate new levels needed */
	pgd = pgtable + pgd_index(gpa);
	p4d = p4d_offset(pgd, gpa);

	pud = NULL;
	if (p4d_present(*p4d))
		pud = pud_offset(p4d, gpa);
	else
		new_pud = pud_alloc_one(kvm->mm, gpa);

	pmd = NULL;
	if (pud && pud_present(*pud) && !pud_is_leaf(*pud))
		pmd = pmd_offset(pud, gpa);
	else if (level <= 1)
		new_pmd = kvmppc_pmd_alloc();

	if (level == 0 && !(pmd && pmd_present(*pmd) && !pmd_is_leaf(*pmd)))
		new_ptep = kvmppc_pte_alloc();

	/* Check if we might have been invalidated; let the guest retry if so */
	spin_lock(&kvm->mmu_lock);
	ret = -EAGAIN;
	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;

	/* Now traverse again under the lock and change the tree */
	ret = -ENOMEM;
	if (p4d_none(*p4d)) {
		if (!new_pud)
			goto out_unlock;
		p4d_populate(kvm->mm, p4d, new_pud);
		new_pud = NULL;
	}
	pud = pud_offset(p4d, gpa);
	if (pud_is_leaf(*pud)) {
		unsigned long hgpa = gpa & PUD_MASK;

		/* Check if we raced and someone else has set the same thing */
		if (level == 2) {
			if (pud_raw(*pud) == pte_raw(pte)) {
				ret = 0;
				goto out_unlock;
			}
			/* Valid 1GB page here already, add our extra bits */
			WARN_ON_ONCE((pud_val(*pud) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
			kvmppc_radix_update_pte(kvm, (pte_t *)pud,
					      0, pte_val(pte), hgpa, PUD_SHIFT);
			ret = 0;
			goto out_unlock;
		}
		/*
		 * If we raced with another CPU which has just put
		 * a 1GB pte in after we saw a pmd page, try again.
		 */
		if (!new_pmd) {
			ret = -EAGAIN;
			goto out_unlock;
		}
		/* Valid 1GB page here already, remove it */
		kvmppc_unmap_pte(kvm, (pte_t *)pud, hgpa, PUD_SHIFT, NULL,
				 lpid);
	}
	if (level == 2) {
		if (!pud_none(*pud)) {
			/*
			 * There's a page table page here, but we wanted to
			 * install a large page, so remove and free the page
			 * table page.
			 */
			kvmppc_unmap_free_pud_entry_table(kvm, pud, gpa, lpid);
		}
		kvmppc_radix_set_pte_at(kvm, gpa, (pte_t *)pud, pte);
		if (rmapp && n_rmap)
			kvmhv_insert_nest_rmap(kvm, rmapp, n_rmap);
		ret = 0;
		goto out_unlock;
	}
	if (pud_none(*pud)) {
		if (!new_pmd)
			goto out_unlock;
		pud_populate(kvm->mm, pud, new_pmd);
		new_pmd = NULL;
	}
	pmd = pmd_offset(pud, gpa);
	if (pmd_is_leaf(*pmd)) {
		unsigned long lgpa = gpa & PMD_MASK;

		/* Check if we raced and someone else has set the same thing */
		if (level == 1) {
			if (pmd_raw(*pmd) == pte_raw(pte)) {
				ret = 0;
				goto out_unlock;
			}
			/* Valid 2MB page here already, add our extra bits */
			WARN_ON_ONCE((pmd_val(*pmd) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
			kvmppc_radix_update_pte(kvm, pmdp_ptep(pmd),
					0, pte_val(pte), lgpa, PMD_SHIFT);
			ret = 0;
			goto out_unlock;
		}

		/*
		 * If we raced with another CPU which has just put
		 * a 2MB pte in after we saw a pte page, try again.
		 */
		if (!new_ptep) {
			ret = -EAGAIN;
			goto out_unlock;
		}
		/* Valid 2MB page here already, remove it */
		kvmppc_unmap_pte(kvm, pmdp_ptep(pmd), lgpa, PMD_SHIFT, NULL,
				 lpid);
	}
	if (level == 1) {
		if (!pmd_none(*pmd)) {
			/*
			 * There's a page table page here, but we wanted to
			 * install a large page, so remove and free the page
			 * table page.
			 */
			kvmppc_unmap_free_pmd_entry_table(kvm, pmd, gpa, lpid);
		}
		kvmppc_radix_set_pte_at(kvm, gpa, pmdp_ptep(pmd), pte);
		if (rmapp && n_rmap)
			kvmhv_insert_nest_rmap(kvm, rmapp, n_rmap);
		ret = 0;
		goto out_unlock;
	}
	if (pmd_none(*pmd)) {
		if (!new_ptep)
			goto out_unlock;
		pmd_populate(kvm->mm, pmd, new_ptep);
		new_ptep = NULL;
	}
	ptep = pte_offset_kernel(pmd, gpa);
	if (pte_present(*ptep)) {
		/* Check if someone else set the same thing */
		if (pte_raw(*ptep) == pte_raw(pte)) {
			ret = 0;
			goto out_unlock;
		}
		/* Valid page here already, add our extra bits */
		WARN_ON_ONCE((pte_val(*ptep) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
		kvmppc_radix_update_pte(kvm, ptep, 0, pte_val(pte), gpa, 0);
		ret = 0;
		goto out_unlock;
	}
	kvmppc_radix_set_pte_at(kvm, gpa, ptep, pte);
	if (rmapp && n_rmap)
		kvmhv_insert_nest_rmap(kvm, rmapp, n_rmap);
	ret = 0;

 out_unlock:
	spin_unlock(&kvm->mmu_lock);
	if (new_pud)
		pud_free(kvm->mm, new_pud);
	if (new_pmd)
		kvmppc_pmd_free(new_pmd);
	if (new_ptep)
		kvmppc_pte_free(new_ptep);
	return ret;
}

bool kvmppc_hv_handle_set_rc(struct kvm *kvm, bool nested, bool writing,
			     unsigned long gpa, unsigned int lpid)
{
	unsigned long pgflags;
	unsigned int shift;
	pte_t *ptep;

	/*
	 * Need to set an R or C bit in the 2nd-level tables;
	 * since we are just helping out the hardware here,
	 * it is sufficient to do what the hardware does.
	 */
	pgflags = _PAGE_ACCESSED;
	if (writing)
		pgflags |= _PAGE_DIRTY;

	if (nested)
		ptep = find_kvm_nested_guest_pte(kvm, lpid, gpa, &shift);
	else
		ptep = find_kvm_secondary_pte(kvm, gpa, &shift);

	if (ptep && pte_present(*ptep) && (!writing || pte_write(*ptep))) {
		kvmppc_radix_update_pte(kvm, ptep, 0, pgflags, gpa, shift);
		return true;
	}
	return false;
}

int kvmppc_book3s_instantiate_page(struct kvm_vcpu *vcpu,
				   unsigned long gpa,
				   struct kvm_memory_slot *memslot,
				   bool writing, bool kvm_ro,
				   pte_t *inserted_pte, unsigned int *levelp)
{
	struct kvm *kvm = vcpu->kvm;
	struct page *page = NULL;
	unsigned long mmu_seq;
	unsigned long hva, gfn = gpa >> PAGE_SHIFT;
	bool upgrade_write = false;
	bool *upgrade_p = &upgrade_write;
	pte_t pte, *ptep;
	unsigned int shift, level;
	int ret;
	bool large_enable;

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	/*
	 * Do a fast check first, since __gfn_to_pfn_memslot doesn't
	 * do it with !atomic && !async, which is how we call it.
	 * We always ask for write permission since the common case
	 * is that the page is writable.
	 */
	hva = gfn_to_hva_memslot(memslot, gfn);
	if (!kvm_ro && get_user_page_fast_only(hva, FOLL_WRITE, &page)) {
		upgrade_write = true;
	} else {
		unsigned long pfn;

		/* Call KVM generic code to do the slow-path check */
		pfn = __gfn_to_pfn_memslot(memslot, gfn, false, NULL,
					   writing, upgrade_p, NULL);
		if (is_error_noslot_pfn(pfn))
			return -EFAULT;
		page = NULL;
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);
			if (PageReserved(page))
				page = NULL;
		}
	}

	/*
	 * Read the PTE from the process' radix tree and use that
	 * so we get the shift and attribute bits.
	 */
	spin_lock(&kvm->mmu_lock);
	ptep = find_kvm_host_pte(kvm, mmu_seq, hva, &shift);
	pte = __pte(0);
	if (ptep)
		pte = READ_ONCE(*ptep);
	spin_unlock(&kvm->mmu_lock);
	/*
	 * If the PTE disappeared temporarily due to a THP
	 * collapse, just return and let the guest try again.
	 */
	if (!pte_present(pte)) {
		if (page)
			put_page(page);
		return RESUME_GUEST;
	}

	/* If we're logging dirty pages, always map single pages */
	large_enable = !(memslot->flags & KVM_MEM_LOG_DIRTY_PAGES);

	/* Get pte level from shift/size */
	if (large_enable && shift == PUD_SHIFT &&
	    (gpa & (PUD_SIZE - PAGE_SIZE)) ==
	    (hva & (PUD_SIZE - PAGE_SIZE))) {
		level = 2;
	} else if (large_enable && shift == PMD_SHIFT &&
		   (gpa & (PMD_SIZE - PAGE_SIZE)) ==
		   (hva & (PMD_SIZE - PAGE_SIZE))) {
		level = 1;
	} else {
		level = 0;
		if (shift > PAGE_SHIFT) {
			/*
			 * If the pte maps more than one page, bring over
			 * bits from the virtual address to get the real
			 * address of the specific single page we want.
			 */
			unsigned long rpnmask = (1ul << shift) - PAGE_SIZE;
			pte = __pte(pte_val(pte) | (hva & rpnmask));
		}
	}

	pte = __pte(pte_val(pte) | _PAGE_EXEC | _PAGE_ACCESSED);
	if (writing || upgrade_write) {
		if (pte_val(pte) & _PAGE_WRITE)
			pte = __pte(pte_val(pte) | _PAGE_DIRTY);
	} else {
		pte = __pte(pte_val(pte) & ~(_PAGE_WRITE | _PAGE_DIRTY));
	}

	/* Allocate space in the tree and write the PTE */
	ret = kvmppc_create_pte(kvm, kvm->arch.pgtable, pte, gpa, level,
				mmu_seq, kvm->arch.lpid, NULL, NULL);
	if (inserted_pte)
		*inserted_pte = pte;
	if (levelp)
		*levelp = level;

	if (page) {
		if (!ret && (pte_val(pte) & _PAGE_WRITE))
			set_page_dirty_lock(page);
		put_page(page);
	}

	/* Increment number of large pages if we (successfully) inserted one */
	if (!ret) {
		if (level == 1)
			kvm->stat.num_2M_pages++;
		else if (level == 2)
			kvm->stat.num_1G_pages++;
	}

	return ret;
}

int kvmppc_book3s_radix_page_fault(struct kvm_vcpu *vcpu,
				   unsigned long ea, unsigned long dsisr)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long gpa, gfn;
	struct kvm_memory_slot *memslot;
	long ret;
	bool writing = !!(dsisr & DSISR_ISSTORE);
	bool kvm_ro = false;

	/* Check for unusual errors */
	if (dsisr & DSISR_UNSUPP_MMU) {
		pr_err("KVM: Got unsupported MMU fault\n");
		return -EFAULT;
	}
	if (dsisr & DSISR_BADACCESS) {
		/* Reflect to the guest as DSI */
		pr_err("KVM: Got radix HV page fault with DSISR=%lx\n", dsisr);
		kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
		return RESUME_GUEST;
	}

	/* Translate the logical address */
	gpa = vcpu->arch.fault_gpa & ~0xfffUL;
	gpa &= ~0xF000000000000000ul;
	gfn = gpa >> PAGE_SHIFT;
	if (!(dsisr & DSISR_PRTABLE_FAULT))
		gpa |= ea & 0xfff;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return kvmppc_send_page_to_uv(kvm, gfn);

	/* Get the corresponding memslot */
	memslot = gfn_to_memslot(kvm, gfn);

	/* No memslot means it's an emulated MMIO region */
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID)) {
		if (dsisr & (DSISR_PRTABLE_FAULT | DSISR_BADACCESS |
			     DSISR_SET_RC)) {
			/*
			 * Bad address in guest page table tree, or other
			 * unusual error - reflect it to the guest as DSI.
			 */
			kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
			return RESUME_GUEST;
		}
		return kvmppc_hv_emulate_mmio(vcpu, gpa, ea, writing);
	}

	if (memslot->flags & KVM_MEM_READONLY) {
		if (writing) {
			/* give the guest a DSI */
			kvmppc_core_queue_data_storage(vcpu, ea, DSISR_ISSTORE |
						       DSISR_PROTFAULT);
			return RESUME_GUEST;
		}
		kvm_ro = true;
	}

	/* Failed to set the reference/change bits */
	if (dsisr & DSISR_SET_RC) {
		spin_lock(&kvm->mmu_lock);
		if (kvmppc_hv_handle_set_rc(kvm, false, writing,
					    gpa, kvm->arch.lpid))
			dsisr &= ~DSISR_SET_RC;
		spin_unlock(&kvm->mmu_lock);

		if (!(dsisr & (DSISR_BAD_FAULT_64S | DSISR_NOHPTE |
			       DSISR_PROTFAULT | DSISR_SET_RC)))
			return RESUME_GUEST;
	}

	/* Try to insert a pte */
	ret = kvmppc_book3s_instantiate_page(vcpu, gpa, memslot, writing,
					     kvm_ro, NULL, NULL);

	if (ret == 0 || ret == -EAGAIN)
		ret = RESUME_GUEST;
	return ret;
}

/* Called with kvm->mmu_lock held */
void kvm_unmap_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
		     unsigned long gfn)
{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE) {
		uv_page_inval(kvm->arch.lpid, gpa, PAGE_SHIFT);
		return;
	}

	ptep = find_kvm_secondary_pte(kvm, gpa, &shift);
	if (ptep && pte_present(*ptep))
		kvmppc_unmap_pte(kvm, ptep, gpa, shift, memslot,
				 kvm->arch.lpid);
}

/* Called with kvm->mmu_lock held */
bool kvm_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
		   unsigned long gfn)
{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;
	bool ref = false;
	unsigned long old, *rmapp;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return ref;

	ptep = find_kvm_secondary_pte(kvm, gpa, &shift);
	if (ptep && pte_present(*ptep) && pte_young(*ptep)) {
		old = kvmppc_radix_update_pte(kvm, ptep, _PAGE_ACCESSED, 0,
					      gpa, shift);
		/* XXX need to flush tlb here? */
		/* Also clear bit in ptes in shadow pgtable for nested guests */
		rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
		kvmhv_update_nest_rmap_rc_list(kvm, rmapp, _PAGE_ACCESSED, 0,
					       old & PTE_RPN_MASK,
					       1UL << shift);
		ref = true;
	}
	return ref;
}

/* Called with kvm->mmu_lock held */
bool kvm_test_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
			unsigned long gfn)

{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;
	bool ref = false;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return ref;

	ptep = find_kvm_secondary_pte(kvm, gpa, &shift);
	if (ptep && pte_present(*ptep) && pte_young(*ptep))
		ref = true;
	return ref;
}

/* Returns the number of PAGE_SIZE pages that are dirty */
static int kvm_radix_test_clear_dirty(struct kvm *kvm,
				struct kvm_memory_slot *memslot, int pagenum)
{
	unsigned long gfn = memslot->base_gfn + pagenum;
	unsigned long gpa = gfn << PAGE_SHIFT;
	pte_t *ptep, pte;
	unsigned int shift;
	int ret = 0;
	unsigned long old, *rmapp;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return ret;

	/*
	 * For performance reasons we don't hold kvm->mmu_lock while walking the
	 * partition scoped table.
	 */
	ptep = find_kvm_secondary_pte_unlocked(kvm, gpa, &shift);
	if (!ptep)
		return 0;

	pte = READ_ONCE(*ptep);
	if (pte_present(pte) && pte_dirty(pte)) {
		spin_lock(&kvm->mmu_lock);
		/*
		 * Recheck the pte again
		 */
		if (pte_val(pte) != pte_val(*ptep)) {
			/*
			 * We have KVM_MEM_LOG_DIRTY_PAGES enabled. Hence we can
			 * only find PAGE_SIZE pte entries here. We can continue
			 * to use the pte addr returned by above page table
			 * walk.
			 */
			if (!pte_present(*ptep) || !pte_dirty(*ptep)) {
				spin_unlock(&kvm->mmu_lock);
				return 0;
			}
		}

		ret = 1;
		VM_BUG_ON(shift);
		old = kvmppc_radix_update_pte(kvm, ptep, _PAGE_DIRTY, 0,
					      gpa, shift);
		kvmppc_radix_tlbie_page(kvm, gpa, shift, kvm->arch.lpid);
		/* Also clear bit in ptes in shadow pgtable for nested guests */
		rmapp = &memslot->arch.rmap[gfn - memslot->base_gfn];
		kvmhv_update_nest_rmap_rc_list(kvm, rmapp, _PAGE_DIRTY, 0,
					       old & PTE_RPN_MASK,
					       1UL << shift);
		spin_unlock(&kvm->mmu_lock);
	}
	return ret;
}

long kvmppc_hv_get_dirty_log_radix(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map)
{
	unsigned long i, j;
	int npages;

	for (i = 0; i < memslot->npages; i = j) {
		npages = kvm_radix_test_clear_dirty(kvm, memslot, i);

		/*
		 * Note that if npages > 0 then i must be a multiple of npages,
		 * since huge pages are only used to back the guest at guest
		 * real addresses that are a multiple of their size.
		 * Since we have at most one PTE covering any given guest
		 * real address, if npages > 1 we can skip to i + npages.
		 */
		j = i + 1;
		if (npages) {
			set_dirty_bits(map, i, npages);
			j = i + npages;
		}
	}
	return 0;
}

void kvmppc_radix_flush_memslot(struct kvm *kvm,
				const struct kvm_memory_slot *memslot)
{
	unsigned long n;
	pte_t *ptep;
	unsigned long gpa;
	unsigned int shift;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START)
		kvmppc_uvmem_drop_pages(memslot, kvm, true);

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return;

	gpa = memslot->base_gfn << PAGE_SHIFT;
	spin_lock(&kvm->mmu_lock);
	for (n = memslot->npages; n; --n) {
		ptep = find_kvm_secondary_pte(kvm, gpa, &shift);
		if (ptep && pte_present(*ptep))
			kvmppc_unmap_pte(kvm, ptep, gpa, shift, memslot,
					 kvm->arch.lpid);
		gpa += PAGE_SIZE;
	}
	/*
	 * Increase the mmu notifier sequence number to prevent any page
	 * fault that read the memslot earlier from writing a PTE.
	 */
	kvm->mmu_notifier_seq++;
	spin_unlock(&kvm->mmu_lock);
}

static void add_rmmu_ap_encoding(struct kvm_ppc_rmmu_info *info,
				 int psize, int *indexp)
{
	if (!mmu_psize_defs[psize].shift)
		return;
	info->ap_encodings[*indexp] = mmu_psize_defs[psize].shift |
		(mmu_psize_defs[psize].ap << 29);
	++(*indexp);
}

int kvmhv_get_rmmu_info(struct kvm *kvm, struct kvm_ppc_rmmu_info *info)
{
	int i;

	if (!radix_enabled())
		return -EINVAL;
	memset(info, 0, sizeof(*info));

	/* 4k page size */
	info->geometries[0].page_shift = 12;
	info->geometries[0].level_bits[0] = 9;
	for (i = 1; i < 4; ++i)
		info->geometries[0].level_bits[i] = p9_supported_radix_bits[i];
	/* 64k page size */
	info->geometries[1].page_shift = 16;
	for (i = 0; i < 4; ++i)
		info->geometries[1].level_bits[i] = p9_supported_radix_bits[i];

	i = 0;
	add_rmmu_ap_encoding(info, MMU_PAGE_4K, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_64K, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_2M, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_1G, &i);

	return 0;
}

int kvmppc_init_vm_radix(struct kvm *kvm)
{
	kvm->arch.pgtable = pgd_alloc(kvm->mm);
	if (!kvm->arch.pgtable)
		return -ENOMEM;
	return 0;
}

static void pte_ctor(void *addr)
{
	memset(addr, 0, RADIX_PTE_TABLE_SIZE);
}

static void pmd_ctor(void *addr)
{
	memset(addr, 0, RADIX_PMD_TABLE_SIZE);
}

struct debugfs_radix_state {
	struct kvm	*kvm;
	struct mutex	mutex;
	unsigned long	gpa;
	int		lpid;
	int		chars_left;
	int		buf_index;
	char		buf[128];
	u8		hdr;
};

static int debugfs_radix_open(struct inode *inode, struct file *file)
{
	struct kvm *kvm = inode->i_private;
	struct debugfs_radix_state *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	kvm_get_kvm(kvm);
	p->kvm = kvm;
	mutex_init(&p->mutex);
	file->private_data = p;

	return nonseekable_open(inode, file);
}

static int debugfs_radix_release(struct inode *inode, struct file *file)
{
	struct debugfs_radix_state *p = file->private_data;

	kvm_put_kvm(p->kvm);
	kfree(p);
	return 0;
}

static ssize_t debugfs_radix_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct debugfs_radix_state *p = file->private_data;
	ssize_t ret, r;
	unsigned long n;
	struct kvm *kvm;
	unsigned long gpa;
	pgd_t *pgt;
	struct kvm_nested_guest *nested;
	pgd_t *pgdp;
	p4d_t p4d, *p4dp;
	pud_t pud, *pudp;
	pmd_t pmd, *pmdp;
	pte_t *ptep;
	int shift;
	unsigned long pte;

	kvm = p->kvm;
	if (!kvm_is_radix(kvm))
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

	gpa = p->gpa;
	nested = NULL;
	pgt = NULL;
	while (len != 0 && p->lpid >= 0) {
		if (gpa >= RADIX_PGTABLE_RANGE) {
			gpa = 0;
			pgt = NULL;
			if (nested) {
				kvmhv_put_nested(nested);
				nested = NULL;
			}
			p->lpid = kvmhv_nested_next_lpid(kvm, p->lpid);
			p->hdr = 0;
			if (p->lpid < 0)
				break;
		}
		if (!pgt) {
			if (p->lpid == 0) {
				pgt = kvm->arch.pgtable;
			} else {
				nested = kvmhv_get_nested(kvm, p->lpid, false);
				if (!nested) {
					gpa = RADIX_PGTABLE_RANGE;
					continue;
				}
				pgt = nested->shadow_pgtable;
			}
		}
		n = 0;
		if (!p->hdr) {
			if (p->lpid > 0)
				n = scnprintf(p->buf, sizeof(p->buf),
					      "\nNested LPID %d: ", p->lpid);
			n += scnprintf(p->buf + n, sizeof(p->buf) - n,
				      "pgdir: %lx\n", (unsigned long)pgt);
			p->hdr = 1;
			goto copy;
		}

		pgdp = pgt + pgd_index(gpa);
		p4dp = p4d_offset(pgdp, gpa);
		p4d = READ_ONCE(*p4dp);
		if (!(p4d_val(p4d) & _PAGE_PRESENT)) {
			gpa = (gpa & P4D_MASK) + P4D_SIZE;
			continue;
		}

		pudp = pud_offset(&p4d, gpa);
		pud = READ_ONCE(*pudp);
		if (!(pud_val(pud) & _PAGE_PRESENT)) {
			gpa = (gpa & PUD_MASK) + PUD_SIZE;
			continue;
		}
		if (pud_val(pud) & _PAGE_PTE) {
			pte = pud_val(pud);
			shift = PUD_SHIFT;
			goto leaf;
		}

		pmdp = pmd_offset(&pud, gpa);
		pmd = READ_ONCE(*pmdp);
		if (!(pmd_val(pmd) & _PAGE_PRESENT)) {
			gpa = (gpa & PMD_MASK) + PMD_SIZE;
			continue;
		}
		if (pmd_val(pmd) & _PAGE_PTE) {
			pte = pmd_val(pmd);
			shift = PMD_SHIFT;
			goto leaf;
		}

		ptep = pte_offset_kernel(&pmd, gpa);
		pte = pte_val(READ_ONCE(*ptep));
		if (!(pte & _PAGE_PRESENT)) {
			gpa += PAGE_SIZE;
			continue;
		}
		shift = PAGE_SHIFT;
	leaf:
		n = scnprintf(p->buf, sizeof(p->buf),
			      " %lx: %lx %d\n", gpa, pte, shift);
		gpa += 1ul << shift;
	copy:
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
			break;
		}
	}
	p->gpa = gpa;
	if (nested)
		kvmhv_put_nested(nested);

 out:
	mutex_unlock(&p->mutex);
	return ret;
}

static ssize_t debugfs_radix_write(struct file *file, const char __user *buf,
			   size_t len, loff_t *ppos)
{
	return -EACCES;
}

static const struct file_operations debugfs_radix_fops = {
	.owner	 = THIS_MODULE,
	.open	 = debugfs_radix_open,
	.release = debugfs_radix_release,
	.read	 = debugfs_radix_read,
	.write	 = debugfs_radix_write,
	.llseek	 = generic_file_llseek,
};

void kvmhv_radix_debugfs_init(struct kvm *kvm)
{
	debugfs_create_file("radix", 0400, kvm->arch.debugfs_dir, kvm,
			    &debugfs_radix_fops);
}

int kvmppc_radix_init(void)
{
	unsigned long size = sizeof(void *) << RADIX_PTE_INDEX_SIZE;

	kvm_pte_cache = kmem_cache_create("kvm-pte", size, size, 0, pte_ctor);
	if (!kvm_pte_cache)
		return -ENOMEM;

	size = sizeof(void *) << RADIX_PMD_INDEX_SIZE;

	kvm_pmd_cache = kmem_cache_create("kvm-pmd", size, size, 0, pmd_ctor);
	if (!kvm_pmd_cache) {
		kmem_cache_destroy(kvm_pte_cache);
		return -ENOMEM;
	}

	return 0;
}

void kvmppc_radix_exit(void)
{
	kmem_cache_destroy(kvm_pte_cache);
	kmem_cache_destroy(kvm_pmd_cache);
}
