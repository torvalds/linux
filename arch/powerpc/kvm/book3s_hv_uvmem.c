// SPDX-License-Identifier: GPL-2.0
/*
 * Secure pages management: Migration of pages between normal and secure
 * memory of KVM guests.
 *
 * Copyright 2018 Bharata B Rao, IBM Corp. <bharata@linux.ibm.com>
 */

/*
 * A pseries guest can be run as secure guest on Ultravisor-enabled
 * POWER platforms. On such platforms, this driver will be used to manage
 * the movement of guest pages between the normal memory managed by
 * hypervisor (HV) and secure memory managed by Ultravisor (UV).
 *
 * The page-in or page-out requests from UV will come to HV as hcalls and
 * HV will call back into UV via ultracalls to satisfy these page requests.
 *
 * Private ZONE_DEVICE memory equal to the amount of secure memory
 * available in the platform for running secure guests is hotplugged.
 * Whenever a page belonging to the guest becomes secure, a page from this
 * private device memory is used to represent and track that secure page
 * on the HV side. Some pages (like virtio buffers, VPA pages etc) are
 * shared between UV and HV. However such pages aren't represented by
 * device private memory and mappings to shared memory exist in both
 * UV and HV page tables.
 */

/*
 * Notes on locking
 *
 * kvm->arch.uvmem_lock is a per-guest lock that prevents concurrent
 * page-in and page-out requests for the same GPA. Concurrent accesses
 * can either come via UV (guest vCPUs requesting for same page)
 * or when HV and guest simultaneously access the same page.
 * This mutex serializes the migration of page from HV(normal) to
 * UV(secure) and vice versa. So the serialization points are around
 * migrate_vma routines and page-in/out routines.
 *
 * Per-guest mutex comes with a cost though. Mainly it serializes the
 * fault path as page-out can occur when HV faults on accessing secure
 * guest pages. Currently UV issues page-in requests for all the guest
 * PFNs one at a time during early boot (UV_ESM uvcall), so this is
 * not a cause for concern. Also currently the number of page-outs caused
 * by HV touching secure pages is very very low. If an when UV supports
 * overcommitting, then we might see concurrent guest driven page-outs.
 *
 * Locking order
 *
 * 1. kvm->srcu - Protects KVM memslots
 * 2. kvm->mm->mmap_lock - find_vma, migrate_vma_pages and helpers, ksm_madvise
 * 3. kvm->arch.uvmem_lock - protects read/writes to uvmem slots thus acting
 *			     as sync-points for page-in/out
 */

/*
 * Notes on page size
 *
 * Currently UV uses 2MB mappings internally, but will issue H_SVM_PAGE_IN
 * and H_SVM_PAGE_OUT hcalls in PAGE_SIZE(64K) granularity. HV tracks
 * secure GPAs at 64K page size and maintains one device PFN for each
 * 64K secure GPA. UV_PAGE_IN and UV_PAGE_OUT calls by HV are also issued
 * for 64K page at a time.
 *
 * HV faulting on secure pages: When HV touches any secure page, it
 * faults and issues a UV_PAGE_OUT request with 64K page size. Currently
 * UV splits and remaps the 2MB page if necessary and copies out the
 * required 64K page contents.
 *
 * Shared pages: Whenever guest shares a secure page, UV will split and
 * remap the 2MB page if required and issue H_SVM_PAGE_IN with 64K page size.
 *
 * HV invalidating a page: When a regular page belonging to secure
 * guest gets unmapped, HV informs UV with UV_PAGE_INVAL of 64K
 * page size. Using 64K page size is correct here because any non-secure
 * page will essentially be of 64K page size. Splitting by UV during sharing
 * and page-out ensures this.
 *
 * Page fault handling: When HV handles page fault of a page belonging
 * to secure guest, it sends that to UV with a 64K UV_PAGE_IN request.
 * Using 64K size is correct here too as UV would have split the 2MB page
 * into 64k mappings and would have done page-outs earlier.
 *
 * In summary, the current secure pages handling code in HV assumes
 * 64K page size and in fact fails any page-in/page-out requests of
 * non-64K size upfront. If and when UV starts supporting multiple
 * page-sizes, we need to break this assumption.
 */

#include <linux/pagemap.h>
#include <linux/migrate.h>
#include <linux/kvm_host.h>
#include <linux/ksm.h>
#include <linux/of.h>
#include <asm/ultravisor.h>
#include <asm/mman.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s_uvmem.h>

static struct dev_pagemap kvmppc_uvmem_pgmap;
static unsigned long *kvmppc_uvmem_bitmap;
static DEFINE_SPINLOCK(kvmppc_uvmem_bitmap_lock);

/*
 * States of a GFN
 * ---------------
 * The GFN can be in one of the following states.
 *
 * (a) Secure - The GFN is secure. The GFN is associated with
 *	a Secure VM, the contents of the GFN is not accessible
 *	to the Hypervisor.  This GFN can be backed by a secure-PFN,
 *	or can be backed by a normal-PFN with contents encrypted.
 *	The former is true when the GFN is paged-in into the
 *	ultravisor. The latter is true when the GFN is paged-out
 *	of the ultravisor.
 *
 * (b) Shared - The GFN is shared. The GFN is associated with a
 *	a secure VM. The contents of the GFN is accessible to
 *	Hypervisor. This GFN is backed by a normal-PFN and its
 *	content is un-encrypted.
 *
 * (c) Normal - The GFN is a normal. The GFN is associated with
 *	a normal VM. The contents of the GFN is accesible to
 *	the Hypervisor. Its content is never encrypted.
 *
 * States of a VM.
 * ---------------
 *
 * Normal VM:  A VM whose contents are always accessible to
 *	the hypervisor.  All its GFNs are normal-GFNs.
 *
 * Secure VM: A VM whose contents are not accessible to the
 *	hypervisor without the VM's consent.  Its GFNs are
 *	either Shared-GFN or Secure-GFNs.
 *
 * Transient VM: A Normal VM that is transitioning to secure VM.
 *	The transition starts on successful return of
 *	H_SVM_INIT_START, and ends on successful return
 *	of H_SVM_INIT_DONE. This transient VM, can have GFNs
 *	in any of the three states; i.e Secure-GFN, Shared-GFN,
 *	and Normal-GFN.	The VM never executes in this state
 *	in supervisor-mode.
 *
 * Memory slot State.
 * -----------------------------
 *	The state of a memory slot mirrors the state of the
 *	VM the memory slot is associated with.
 *
 * VM State transition.
 * --------------------
 *
 *  A VM always starts in Normal Mode.
 *
 *  H_SVM_INIT_START moves the VM into transient state. During this
 *  time the Ultravisor may request some of its GFNs to be shared or
 *  secured. So its GFNs can be in one of the three GFN states.
 *
 *  H_SVM_INIT_DONE moves the VM entirely from transient state to
 *  secure-state. At this point any left-over normal-GFNs are
 *  transitioned to Secure-GFN.
 *
 *  H_SVM_INIT_ABORT moves the transient VM back to normal VM.
 *  All its GFNs are moved to Normal-GFNs.
 *
 *  UV_TERMINATE transitions the secure-VM back to normal-VM. All
 *  the secure-GFN and shared-GFNs are tranistioned to normal-GFN
 *  Note: The contents of the normal-GFN is undefined at this point.
 *
 * GFN state implementation:
 * -------------------------
 *
 * Secure GFN is associated with a secure-PFN; also called uvmem_pfn,
 * when the GFN is paged-in. Its pfn[] has KVMPPC_GFN_UVMEM_PFN flag
 * set, and contains the value of the secure-PFN.
 * It is associated with a normal-PFN; also called mem_pfn, when
 * the GFN is pagedout. Its pfn[] has KVMPPC_GFN_MEM_PFN flag set.
 * The value of the normal-PFN is not tracked.
 *
 * Shared GFN is associated with a normal-PFN. Its pfn[] has
 * KVMPPC_UVMEM_SHARED_PFN flag set. The value of the normal-PFN
 * is not tracked.
 *
 * Normal GFN is associated with normal-PFN. Its pfn[] has
 * no flag set. The value of the normal-PFN is not tracked.
 *
 * Life cycle of a GFN
 * --------------------
 *
 * --------------------------------------------------------------
 * |        |     Share  |  Unshare | SVM       |H_SVM_INIT_DONE|
 * |        |operation   |operation | abort/    |               |
 * |        |            |          | terminate |               |
 * -------------------------------------------------------------
 * |        |            |          |           |               |
 * | Secure |     Shared | Secure   |Normal     |Secure         |
 * |        |            |          |           |               |
 * | Shared |     Shared | Secure   |Normal     |Shared         |
 * |        |            |          |           |               |
 * | Normal |     Shared | Secure   |Normal     |Secure         |
 * --------------------------------------------------------------
 *
 * Life cycle of a VM
 * --------------------
 *
 * --------------------------------------------------------------------
 * |         |  start    |  H_SVM_  |H_SVM_   |H_SVM_     |UV_SVM_    |
 * |         |  VM       |INIT_START|INIT_DONE|INIT_ABORT |TERMINATE  |
 * |         |           |          |         |           |           |
 * --------- ----------------------------------------------------------
 * |         |           |          |         |           |           |
 * | Normal  | Normal    | Transient|Error    |Error      |Normal     |
 * |         |           |          |         |           |           |
 * | Secure  |   Error   | Error    |Error    |Error      |Normal     |
 * |         |           |          |         |           |           |
 * |Transient|   N/A     | Error    |Secure   |Normal     |Normal     |
 * --------------------------------------------------------------------
 */

#define KVMPPC_GFN_UVMEM_PFN	(1UL << 63)
#define KVMPPC_GFN_MEM_PFN	(1UL << 62)
#define KVMPPC_GFN_SHARED	(1UL << 61)
#define KVMPPC_GFN_SECURE	(KVMPPC_GFN_UVMEM_PFN | KVMPPC_GFN_MEM_PFN)
#define KVMPPC_GFN_FLAG_MASK	(KVMPPC_GFN_SECURE | KVMPPC_GFN_SHARED)
#define KVMPPC_GFN_PFN_MASK	(~KVMPPC_GFN_FLAG_MASK)

struct kvmppc_uvmem_slot {
	struct list_head list;
	unsigned long nr_pfns;
	unsigned long base_pfn;
	unsigned long *pfns;
};
struct kvmppc_uvmem_page_pvt {
	struct kvm *kvm;
	unsigned long gpa;
	bool skip_page_out;
	bool remove_gfn;
};

bool kvmppc_uvmem_available(void)
{
	/*
	 * If kvmppc_uvmem_bitmap != NULL, then there is an ultravisor
	 * and our data structures have been initialized successfully.
	 */
	return !!kvmppc_uvmem_bitmap;
}

int kvmppc_uvmem_slot_init(struct kvm *kvm, const struct kvm_memory_slot *slot)
{
	struct kvmppc_uvmem_slot *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->pfns = vzalloc(array_size(slot->npages, sizeof(*p->pfns)));
	if (!p->pfns) {
		kfree(p);
		return -ENOMEM;
	}
	p->nr_pfns = slot->npages;
	p->base_pfn = slot->base_gfn;

	mutex_lock(&kvm->arch.uvmem_lock);
	list_add(&p->list, &kvm->arch.uvmem_pfns);
	mutex_unlock(&kvm->arch.uvmem_lock);

	return 0;
}

/*
 * All device PFNs are already released by the time we come here.
 */
void kvmppc_uvmem_slot_free(struct kvm *kvm, const struct kvm_memory_slot *slot)
{
	struct kvmppc_uvmem_slot *p, *next;

	mutex_lock(&kvm->arch.uvmem_lock);
	list_for_each_entry_safe(p, next, &kvm->arch.uvmem_pfns, list) {
		if (p->base_pfn == slot->base_gfn) {
			vfree(p->pfns);
			list_del(&p->list);
			kfree(p);
			break;
		}
	}
	mutex_unlock(&kvm->arch.uvmem_lock);
}

static void kvmppc_mark_gfn(unsigned long gfn, struct kvm *kvm,
			unsigned long flag, unsigned long uvmem_pfn)
{
	struct kvmppc_uvmem_slot *p;

	list_for_each_entry(p, &kvm->arch.uvmem_pfns, list) {
		if (gfn >= p->base_pfn && gfn < p->base_pfn + p->nr_pfns) {
			unsigned long index = gfn - p->base_pfn;

			if (flag == KVMPPC_GFN_UVMEM_PFN)
				p->pfns[index] = uvmem_pfn | flag;
			else
				p->pfns[index] = flag;
			return;
		}
	}
}

/* mark the GFN as secure-GFN associated with @uvmem pfn device-PFN. */
static void kvmppc_gfn_secure_uvmem_pfn(unsigned long gfn,
			unsigned long uvmem_pfn, struct kvm *kvm)
{
	kvmppc_mark_gfn(gfn, kvm, KVMPPC_GFN_UVMEM_PFN, uvmem_pfn);
}

/* mark the GFN as secure-GFN associated with a memory-PFN. */
static void kvmppc_gfn_secure_mem_pfn(unsigned long gfn, struct kvm *kvm)
{
	kvmppc_mark_gfn(gfn, kvm, KVMPPC_GFN_MEM_PFN, 0);
}

/* mark the GFN as a shared GFN. */
static void kvmppc_gfn_shared(unsigned long gfn, struct kvm *kvm)
{
	kvmppc_mark_gfn(gfn, kvm, KVMPPC_GFN_SHARED, 0);
}

/* mark the GFN as a non-existent GFN. */
static void kvmppc_gfn_remove(unsigned long gfn, struct kvm *kvm)
{
	kvmppc_mark_gfn(gfn, kvm, 0, 0);
}

/* return true, if the GFN is a secure-GFN backed by a secure-PFN */
static bool kvmppc_gfn_is_uvmem_pfn(unsigned long gfn, struct kvm *kvm,
				    unsigned long *uvmem_pfn)
{
	struct kvmppc_uvmem_slot *p;

	list_for_each_entry(p, &kvm->arch.uvmem_pfns, list) {
		if (gfn >= p->base_pfn && gfn < p->base_pfn + p->nr_pfns) {
			unsigned long index = gfn - p->base_pfn;

			if (p->pfns[index] & KVMPPC_GFN_UVMEM_PFN) {
				if (uvmem_pfn)
					*uvmem_pfn = p->pfns[index] &
						     KVMPPC_GFN_PFN_MASK;
				return true;
			} else
				return false;
		}
	}
	return false;
}

/*
 * starting from *gfn search for the next available GFN that is not yet
 * transitioned to a secure GFN.  return the value of that GFN in *gfn.  If a
 * GFN is found, return true, else return false
 *
 * Must be called with kvm->arch.uvmem_lock  held.
 */
static bool kvmppc_next_nontransitioned_gfn(const struct kvm_memory_slot *memslot,
		struct kvm *kvm, unsigned long *gfn)
{
	struct kvmppc_uvmem_slot *p;
	bool ret = false;
	unsigned long i;

	list_for_each_entry(p, &kvm->arch.uvmem_pfns, list)
		if (*gfn >= p->base_pfn && *gfn < p->base_pfn + p->nr_pfns)
			break;
	if (!p)
		return ret;
	/*
	 * The code below assumes, one to one correspondence between
	 * kvmppc_uvmem_slot and memslot.
	 */
	for (i = *gfn; i < p->base_pfn + p->nr_pfns; i++) {
		unsigned long index = i - p->base_pfn;

		if (!(p->pfns[index] & KVMPPC_GFN_FLAG_MASK)) {
			*gfn = i;
			ret = true;
			break;
		}
	}
	return ret;
}

static int kvmppc_memslot_page_merge(struct kvm *kvm,
		const struct kvm_memory_slot *memslot, bool merge)
{
	unsigned long gfn = memslot->base_gfn;
	unsigned long end, start = gfn_to_hva(kvm, gfn);
	int ret = 0;
	struct vm_area_struct *vma;
	int merge_flag = (merge) ? MADV_MERGEABLE : MADV_UNMERGEABLE;

	if (kvm_is_error_hva(start))
		return H_STATE;

	end = start + (memslot->npages << PAGE_SHIFT);

	mmap_write_lock(kvm->mm);
	do {
		vma = find_vma_intersection(kvm->mm, start, end);
		if (!vma) {
			ret = H_STATE;
			break;
		}
		ret = ksm_madvise(vma, vma->vm_start, vma->vm_end,
			  merge_flag, &vma->vm_flags);
		if (ret) {
			ret = H_STATE;
			break;
		}
		start = vma->vm_end;
	} while (end > vma->vm_end);

	mmap_write_unlock(kvm->mm);
	return ret;
}

static void __kvmppc_uvmem_memslot_delete(struct kvm *kvm,
		const struct kvm_memory_slot *memslot)
{
	uv_unregister_mem_slot(kvm->arch.lpid, memslot->id);
	kvmppc_uvmem_slot_free(kvm, memslot);
	kvmppc_memslot_page_merge(kvm, memslot, true);
}

static int __kvmppc_uvmem_memslot_create(struct kvm *kvm,
		const struct kvm_memory_slot *memslot)
{
	int ret = H_PARAMETER;

	if (kvmppc_memslot_page_merge(kvm, memslot, false))
		return ret;

	if (kvmppc_uvmem_slot_init(kvm, memslot))
		goto out1;

	ret = uv_register_mem_slot(kvm->arch.lpid,
				   memslot->base_gfn << PAGE_SHIFT,
				   memslot->npages * PAGE_SIZE,
				   0, memslot->id);
	if (ret < 0) {
		ret = H_PARAMETER;
		goto out;
	}
	return 0;
out:
	kvmppc_uvmem_slot_free(kvm, memslot);
out1:
	kvmppc_memslot_page_merge(kvm, memslot, true);
	return ret;
}

unsigned long kvmppc_h_svm_init_start(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot, *m;
	int ret = H_SUCCESS;
	int srcu_idx;

	kvm->arch.secure_guest = KVMPPC_SECURE_INIT_START;

	if (!kvmppc_uvmem_bitmap)
		return H_UNSUPPORTED;

	/* Only radix guests can be secure guests */
	if (!kvm_is_radix(kvm))
		return H_UNSUPPORTED;

	/* NAK the transition to secure if not enabled */
	if (!kvm->arch.svm_enabled)
		return H_AUTHORITY;

	srcu_idx = srcu_read_lock(&kvm->srcu);

	/* register the memslot */
	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots) {
		ret = __kvmppc_uvmem_memslot_create(kvm, memslot);
		if (ret)
			break;
	}

	if (ret) {
		slots = kvm_memslots(kvm);
		kvm_for_each_memslot(m, slots) {
			if (m == memslot)
				break;
			__kvmppc_uvmem_memslot_delete(kvm, memslot);
		}
	}

	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return ret;
}

/*
 * Provision a new page on HV side and copy over the contents
 * from secure memory using UV_PAGE_OUT uvcall.
 * Caller must held kvm->arch.uvmem_lock.
 */
static int __kvmppc_svm_page_out(struct vm_area_struct *vma,
		unsigned long start,
		unsigned long end, unsigned long page_shift,
		struct kvm *kvm, unsigned long gpa)
{
	unsigned long src_pfn, dst_pfn = 0;
	struct migrate_vma mig;
	struct page *dpage, *spage;
	struct kvmppc_uvmem_page_pvt *pvt;
	unsigned long pfn;
	int ret = U_SUCCESS;

	memset(&mig, 0, sizeof(mig));
	mig.vma = vma;
	mig.start = start;
	mig.end = end;
	mig.src = &src_pfn;
	mig.dst = &dst_pfn;
	mig.pgmap_owner = &kvmppc_uvmem_pgmap;
	mig.flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;

	/* The requested page is already paged-out, nothing to do */
	if (!kvmppc_gfn_is_uvmem_pfn(gpa >> page_shift, kvm, NULL))
		return ret;

	ret = migrate_vma_setup(&mig);
	if (ret)
		return -1;

	spage = migrate_pfn_to_page(*mig.src);
	if (!spage || !(*mig.src & MIGRATE_PFN_MIGRATE))
		goto out_finalize;

	if (!is_zone_device_page(spage))
		goto out_finalize;

	dpage = alloc_page_vma(GFP_HIGHUSER, vma, start);
	if (!dpage) {
		ret = -1;
		goto out_finalize;
	}

	lock_page(dpage);
	pvt = spage->zone_device_data;
	pfn = page_to_pfn(dpage);

	/*
	 * This function is used in two cases:
	 * - When HV touches a secure page, for which we do UV_PAGE_OUT
	 * - When a secure page is converted to shared page, we *get*
	 *   the page to essentially unmap the device page. In this
	 *   case we skip page-out.
	 */
	if (!pvt->skip_page_out)
		ret = uv_page_out(kvm->arch.lpid, pfn << page_shift,
				  gpa, 0, page_shift);

	if (ret == U_SUCCESS)
		*mig.dst = migrate_pfn(pfn);
	else {
		unlock_page(dpage);
		__free_page(dpage);
		goto out_finalize;
	}

	migrate_vma_pages(&mig);

out_finalize:
	migrate_vma_finalize(&mig);
	return ret;
}

static inline int kvmppc_svm_page_out(struct vm_area_struct *vma,
				      unsigned long start, unsigned long end,
				      unsigned long page_shift,
				      struct kvm *kvm, unsigned long gpa)
{
	int ret;

	mutex_lock(&kvm->arch.uvmem_lock);
	ret = __kvmppc_svm_page_out(vma, start, end, page_shift, kvm, gpa);
	mutex_unlock(&kvm->arch.uvmem_lock);

	return ret;
}

/*
 * Drop device pages that we maintain for the secure guest
 *
 * We first mark the pages to be skipped from UV_PAGE_OUT when there
 * is HV side fault on these pages. Next we *get* these pages, forcing
 * fault on them, do fault time migration to replace the device PTEs in
 * QEMU page table with normal PTEs from newly allocated pages.
 */
void kvmppc_uvmem_drop_pages(const struct kvm_memory_slot *slot,
			     struct kvm *kvm, bool skip_page_out)
{
	int i;
	struct kvmppc_uvmem_page_pvt *pvt;
	struct page *uvmem_page;
	struct vm_area_struct *vma = NULL;
	unsigned long uvmem_pfn, gfn;
	unsigned long addr;

	mmap_read_lock(kvm->mm);

	addr = slot->userspace_addr;

	gfn = slot->base_gfn;
	for (i = slot->npages; i; --i, ++gfn, addr += PAGE_SIZE) {

		/* Fetch the VMA if addr is not in the latest fetched one */
		if (!vma || addr >= vma->vm_end) {
			vma = vma_lookup(kvm->mm, addr);
			if (!vma) {
				pr_err("Can't find VMA for gfn:0x%lx\n", gfn);
				break;
			}
		}

		mutex_lock(&kvm->arch.uvmem_lock);

		if (kvmppc_gfn_is_uvmem_pfn(gfn, kvm, &uvmem_pfn)) {
			uvmem_page = pfn_to_page(uvmem_pfn);
			pvt = uvmem_page->zone_device_data;
			pvt->skip_page_out = skip_page_out;
			pvt->remove_gfn = true;

			if (__kvmppc_svm_page_out(vma, addr, addr + PAGE_SIZE,
						  PAGE_SHIFT, kvm, pvt->gpa))
				pr_err("Can't page out gpa:0x%lx addr:0x%lx\n",
				       pvt->gpa, addr);
		} else {
			/* Remove the shared flag if any */
			kvmppc_gfn_remove(gfn, kvm);
		}

		mutex_unlock(&kvm->arch.uvmem_lock);
	}

	mmap_read_unlock(kvm->mm);
}

unsigned long kvmppc_h_svm_init_abort(struct kvm *kvm)
{
	int srcu_idx;
	struct kvm_memory_slot *memslot;

	/*
	 * Expect to be called only after INIT_START and before INIT_DONE.
	 * If INIT_DONE was completed, use normal VM termination sequence.
	 */
	if (!(kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START))
		return H_UNSUPPORTED;

	if (kvm->arch.secure_guest & KVMPPC_SECURE_INIT_DONE)
		return H_STATE;

	srcu_idx = srcu_read_lock(&kvm->srcu);

	kvm_for_each_memslot(memslot, kvm_memslots(kvm))
		kvmppc_uvmem_drop_pages(memslot, kvm, false);

	srcu_read_unlock(&kvm->srcu, srcu_idx);

	kvm->arch.secure_guest = 0;
	uv_svm_terminate(kvm->arch.lpid);

	return H_PARAMETER;
}

/*
 * Get a free device PFN from the pool
 *
 * Called when a normal page is moved to secure memory (UV_PAGE_IN). Device
 * PFN will be used to keep track of the secure page on HV side.
 *
 * Called with kvm->arch.uvmem_lock held
 */
static struct page *kvmppc_uvmem_get_page(unsigned long gpa, struct kvm *kvm)
{
	struct page *dpage = NULL;
	unsigned long bit, uvmem_pfn;
	struct kvmppc_uvmem_page_pvt *pvt;
	unsigned long pfn_last, pfn_first;

	pfn_first = kvmppc_uvmem_pgmap.range.start >> PAGE_SHIFT;
	pfn_last = pfn_first +
		   (range_len(&kvmppc_uvmem_pgmap.range) >> PAGE_SHIFT);

	spin_lock(&kvmppc_uvmem_bitmap_lock);
	bit = find_first_zero_bit(kvmppc_uvmem_bitmap,
				  pfn_last - pfn_first);
	if (bit >= (pfn_last - pfn_first))
		goto out;
	bitmap_set(kvmppc_uvmem_bitmap, bit, 1);
	spin_unlock(&kvmppc_uvmem_bitmap_lock);

	pvt = kzalloc(sizeof(*pvt), GFP_KERNEL);
	if (!pvt)
		goto out_clear;

	uvmem_pfn = bit + pfn_first;
	kvmppc_gfn_secure_uvmem_pfn(gpa >> PAGE_SHIFT, uvmem_pfn, kvm);

	pvt->gpa = gpa;
	pvt->kvm = kvm;

	dpage = pfn_to_page(uvmem_pfn);
	dpage->zone_device_data = pvt;
	get_page(dpage);
	lock_page(dpage);
	return dpage;
out_clear:
	spin_lock(&kvmppc_uvmem_bitmap_lock);
	bitmap_clear(kvmppc_uvmem_bitmap, bit, 1);
out:
	spin_unlock(&kvmppc_uvmem_bitmap_lock);
	return NULL;
}

/*
 * Alloc a PFN from private device memory pool. If @pagein is true,
 * copy page from normal memory to secure memory using UV_PAGE_IN uvcall.
 */
static int kvmppc_svm_page_in(struct vm_area_struct *vma,
		unsigned long start,
		unsigned long end, unsigned long gpa, struct kvm *kvm,
		unsigned long page_shift,
		bool pagein)
{
	unsigned long src_pfn, dst_pfn = 0;
	struct migrate_vma mig;
	struct page *spage;
	unsigned long pfn;
	struct page *dpage;
	int ret = 0;

	memset(&mig, 0, sizeof(mig));
	mig.vma = vma;
	mig.start = start;
	mig.end = end;
	mig.src = &src_pfn;
	mig.dst = &dst_pfn;
	mig.flags = MIGRATE_VMA_SELECT_SYSTEM;

	ret = migrate_vma_setup(&mig);
	if (ret)
		return ret;

	if (!(*mig.src & MIGRATE_PFN_MIGRATE)) {
		ret = -1;
		goto out_finalize;
	}

	dpage = kvmppc_uvmem_get_page(gpa, kvm);
	if (!dpage) {
		ret = -1;
		goto out_finalize;
	}

	if (pagein) {
		pfn = *mig.src >> MIGRATE_PFN_SHIFT;
		spage = migrate_pfn_to_page(*mig.src);
		if (spage) {
			ret = uv_page_in(kvm->arch.lpid, pfn << page_shift,
					gpa, 0, page_shift);
			if (ret)
				goto out_finalize;
		}
	}

	*mig.dst = migrate_pfn(page_to_pfn(dpage));
	migrate_vma_pages(&mig);
out_finalize:
	migrate_vma_finalize(&mig);
	return ret;
}

static int kvmppc_uv_migrate_mem_slot(struct kvm *kvm,
		const struct kvm_memory_slot *memslot)
{
	unsigned long gfn = memslot->base_gfn;
	struct vm_area_struct *vma;
	unsigned long start, end;
	int ret = 0;

	mmap_read_lock(kvm->mm);
	mutex_lock(&kvm->arch.uvmem_lock);
	while (kvmppc_next_nontransitioned_gfn(memslot, kvm, &gfn)) {
		ret = H_STATE;
		start = gfn_to_hva(kvm, gfn);
		if (kvm_is_error_hva(start))
			break;

		end = start + (1UL << PAGE_SHIFT);
		vma = find_vma_intersection(kvm->mm, start, end);
		if (!vma || vma->vm_start > start || vma->vm_end < end)
			break;

		ret = kvmppc_svm_page_in(vma, start, end,
				(gfn << PAGE_SHIFT), kvm, PAGE_SHIFT, false);
		if (ret) {
			ret = H_STATE;
			break;
		}

		/* relinquish the cpu if needed */
		cond_resched();
	}
	mutex_unlock(&kvm->arch.uvmem_lock);
	mmap_read_unlock(kvm->mm);
	return ret;
}

unsigned long kvmppc_h_svm_init_done(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int srcu_idx;
	long ret = H_SUCCESS;

	if (!(kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START))
		return H_UNSUPPORTED;

	/* migrate any unmoved normal pfn to device pfns*/
	srcu_idx = srcu_read_lock(&kvm->srcu);
	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots) {
		ret = kvmppc_uv_migrate_mem_slot(kvm, memslot);
		if (ret) {
			/*
			 * The pages will remain transitioned.
			 * Its the callers responsibility to
			 * terminate the VM, which will undo
			 * all state of the VM. Till then
			 * this VM is in a erroneous state.
			 * Its KVMPPC_SECURE_INIT_DONE will
			 * remain unset.
			 */
			ret = H_STATE;
			goto out;
		}
	}

	kvm->arch.secure_guest |= KVMPPC_SECURE_INIT_DONE;
	pr_info("LPID %d went secure\n", kvm->arch.lpid);

out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return ret;
}

/*
 * Shares the page with HV, thus making it a normal page.
 *
 * - If the page is already secure, then provision a new page and share
 * - If the page is a normal page, share the existing page
 *
 * In the former case, uses dev_pagemap_ops.migrate_to_ram handler
 * to unmap the device page from QEMU's page tables.
 */
static unsigned long kvmppc_share_page(struct kvm *kvm, unsigned long gpa,
		unsigned long page_shift)
{

	int ret = H_PARAMETER;
	struct page *uvmem_page;
	struct kvmppc_uvmem_page_pvt *pvt;
	unsigned long pfn;
	unsigned long gfn = gpa >> page_shift;
	int srcu_idx;
	unsigned long uvmem_pfn;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	mutex_lock(&kvm->arch.uvmem_lock);
	if (kvmppc_gfn_is_uvmem_pfn(gfn, kvm, &uvmem_pfn)) {
		uvmem_page = pfn_to_page(uvmem_pfn);
		pvt = uvmem_page->zone_device_data;
		pvt->skip_page_out = true;
		/*
		 * do not drop the GFN. It is a valid GFN
		 * that is transitioned to a shared GFN.
		 */
		pvt->remove_gfn = false;
	}

retry:
	mutex_unlock(&kvm->arch.uvmem_lock);
	pfn = gfn_to_pfn(kvm, gfn);
	if (is_error_noslot_pfn(pfn))
		goto out;

	mutex_lock(&kvm->arch.uvmem_lock);
	if (kvmppc_gfn_is_uvmem_pfn(gfn, kvm, &uvmem_pfn)) {
		uvmem_page = pfn_to_page(uvmem_pfn);
		pvt = uvmem_page->zone_device_data;
		pvt->skip_page_out = true;
		pvt->remove_gfn = false; /* it continues to be a valid GFN */
		kvm_release_pfn_clean(pfn);
		goto retry;
	}

	if (!uv_page_in(kvm->arch.lpid, pfn << page_shift, gpa, 0,
				page_shift)) {
		kvmppc_gfn_shared(gfn, kvm);
		ret = H_SUCCESS;
	}
	kvm_release_pfn_clean(pfn);
	mutex_unlock(&kvm->arch.uvmem_lock);
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return ret;
}

/*
 * H_SVM_PAGE_IN: Move page from normal memory to secure memory.
 *
 * H_PAGE_IN_SHARED flag makes the page shared which means that the same
 * memory in is visible from both UV and HV.
 */
unsigned long kvmppc_h_svm_page_in(struct kvm *kvm, unsigned long gpa,
		unsigned long flags,
		unsigned long page_shift)
{
	unsigned long start, end;
	struct vm_area_struct *vma;
	int srcu_idx;
	unsigned long gfn = gpa >> page_shift;
	int ret;

	if (!(kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START))
		return H_UNSUPPORTED;

	if (page_shift != PAGE_SHIFT)
		return H_P3;

	if (flags & ~H_PAGE_IN_SHARED)
		return H_P2;

	if (flags & H_PAGE_IN_SHARED)
		return kvmppc_share_page(kvm, gpa, page_shift);

	ret = H_PARAMETER;
	srcu_idx = srcu_read_lock(&kvm->srcu);
	mmap_read_lock(kvm->mm);

	start = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(start))
		goto out;

	mutex_lock(&kvm->arch.uvmem_lock);
	/* Fail the page-in request of an already paged-in page */
	if (kvmppc_gfn_is_uvmem_pfn(gfn, kvm, NULL))
		goto out_unlock;

	end = start + (1UL << page_shift);
	vma = find_vma_intersection(kvm->mm, start, end);
	if (!vma || vma->vm_start > start || vma->vm_end < end)
		goto out_unlock;

	if (kvmppc_svm_page_in(vma, start, end, gpa, kvm, page_shift,
				true))
		goto out_unlock;

	ret = H_SUCCESS;

out_unlock:
	mutex_unlock(&kvm->arch.uvmem_lock);
out:
	mmap_read_unlock(kvm->mm);
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return ret;
}


/*
 * Fault handler callback that gets called when HV touches any page that
 * has been moved to secure memory, we ask UV to give back the page by
 * issuing UV_PAGE_OUT uvcall.
 *
 * This eventually results in dropping of device PFN and the newly
 * provisioned page/PFN gets populated in QEMU page tables.
 */
static vm_fault_t kvmppc_uvmem_migrate_to_ram(struct vm_fault *vmf)
{
	struct kvmppc_uvmem_page_pvt *pvt = vmf->page->zone_device_data;

	if (kvmppc_svm_page_out(vmf->vma, vmf->address,
				vmf->address + PAGE_SIZE, PAGE_SHIFT,
				pvt->kvm, pvt->gpa))
		return VM_FAULT_SIGBUS;
	else
		return 0;
}

/*
 * Release the device PFN back to the pool
 *
 * Gets called when secure GFN tranistions from a secure-PFN
 * to a normal PFN during H_SVM_PAGE_OUT.
 * Gets called with kvm->arch.uvmem_lock held.
 */
static void kvmppc_uvmem_page_free(struct page *page)
{
	unsigned long pfn = page_to_pfn(page) -
			(kvmppc_uvmem_pgmap.range.start >> PAGE_SHIFT);
	struct kvmppc_uvmem_page_pvt *pvt;

	spin_lock(&kvmppc_uvmem_bitmap_lock);
	bitmap_clear(kvmppc_uvmem_bitmap, pfn, 1);
	spin_unlock(&kvmppc_uvmem_bitmap_lock);

	pvt = page->zone_device_data;
	page->zone_device_data = NULL;
	if (pvt->remove_gfn)
		kvmppc_gfn_remove(pvt->gpa >> PAGE_SHIFT, pvt->kvm);
	else
		kvmppc_gfn_secure_mem_pfn(pvt->gpa >> PAGE_SHIFT, pvt->kvm);
	kfree(pvt);
}

static const struct dev_pagemap_ops kvmppc_uvmem_ops = {
	.page_free = kvmppc_uvmem_page_free,
	.migrate_to_ram	= kvmppc_uvmem_migrate_to_ram,
};

/*
 * H_SVM_PAGE_OUT: Move page from secure memory to normal memory.
 */
unsigned long
kvmppc_h_svm_page_out(struct kvm *kvm, unsigned long gpa,
		      unsigned long flags, unsigned long page_shift)
{
	unsigned long gfn = gpa >> page_shift;
	unsigned long start, end;
	struct vm_area_struct *vma;
	int srcu_idx;
	int ret;

	if (!(kvm->arch.secure_guest & KVMPPC_SECURE_INIT_START))
		return H_UNSUPPORTED;

	if (page_shift != PAGE_SHIFT)
		return H_P3;

	if (flags)
		return H_P2;

	ret = H_PARAMETER;
	srcu_idx = srcu_read_lock(&kvm->srcu);
	mmap_read_lock(kvm->mm);
	start = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(start))
		goto out;

	end = start + (1UL << page_shift);
	vma = find_vma_intersection(kvm->mm, start, end);
	if (!vma || vma->vm_start > start || vma->vm_end < end)
		goto out;

	if (!kvmppc_svm_page_out(vma, start, end, page_shift, kvm, gpa))
		ret = H_SUCCESS;
out:
	mmap_read_unlock(kvm->mm);
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return ret;
}

int kvmppc_send_page_to_uv(struct kvm *kvm, unsigned long gfn)
{
	unsigned long pfn;
	int ret = U_SUCCESS;

	pfn = gfn_to_pfn(kvm, gfn);
	if (is_error_noslot_pfn(pfn))
		return -EFAULT;

	mutex_lock(&kvm->arch.uvmem_lock);
	if (kvmppc_gfn_is_uvmem_pfn(gfn, kvm, NULL))
		goto out;

	ret = uv_page_in(kvm->arch.lpid, pfn << PAGE_SHIFT, gfn << PAGE_SHIFT,
			 0, PAGE_SHIFT);
out:
	kvm_release_pfn_clean(pfn);
	mutex_unlock(&kvm->arch.uvmem_lock);
	return (ret == U_SUCCESS) ? RESUME_GUEST : -EFAULT;
}

int kvmppc_uvmem_memslot_create(struct kvm *kvm, const struct kvm_memory_slot *new)
{
	int ret = __kvmppc_uvmem_memslot_create(kvm, new);

	if (!ret)
		ret = kvmppc_uv_migrate_mem_slot(kvm, new);

	return ret;
}

void kvmppc_uvmem_memslot_delete(struct kvm *kvm, const struct kvm_memory_slot *old)
{
	__kvmppc_uvmem_memslot_delete(kvm, old);
}

static u64 kvmppc_get_secmem_size(void)
{
	struct device_node *np;
	int i, len;
	const __be32 *prop;
	u64 size = 0;

	/*
	 * First try the new ibm,secure-memory nodes which supersede the
	 * secure-memory-ranges property.
	 * If we found some, no need to read the deprecated ones.
	 */
	for_each_compatible_node(np, NULL, "ibm,secure-memory") {
		prop = of_get_property(np, "reg", &len);
		if (!prop)
			continue;
		size += of_read_number(prop + 2, 2);
	}
	if (size)
		return size;

	np = of_find_compatible_node(NULL, NULL, "ibm,uv-firmware");
	if (!np)
		goto out;

	prop = of_get_property(np, "secure-memory-ranges", &len);
	if (!prop)
		goto out_put;

	for (i = 0; i < len / (sizeof(*prop) * 4); i++)
		size += of_read_number(prop + (i * 4) + 2, 2);

out_put:
	of_node_put(np);
out:
	return size;
}

int kvmppc_uvmem_init(void)
{
	int ret = 0;
	unsigned long size;
	struct resource *res;
	void *addr;
	unsigned long pfn_last, pfn_first;

	size = kvmppc_get_secmem_size();
	if (!size) {
		/*
		 * Don't fail the initialization of kvm-hv module if
		 * the platform doesn't export ibm,uv-firmware node.
		 * Let normal guests run on such PEF-disabled platform.
		 */
		pr_info("KVMPPC-UVMEM: No support for secure guests\n");
		goto out;
	}

	res = request_free_mem_region(&iomem_resource, size, "kvmppc_uvmem");
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		goto out;
	}

	kvmppc_uvmem_pgmap.type = MEMORY_DEVICE_PRIVATE;
	kvmppc_uvmem_pgmap.range.start = res->start;
	kvmppc_uvmem_pgmap.range.end = res->end;
	kvmppc_uvmem_pgmap.nr_range = 1;
	kvmppc_uvmem_pgmap.ops = &kvmppc_uvmem_ops;
	/* just one global instance: */
	kvmppc_uvmem_pgmap.owner = &kvmppc_uvmem_pgmap;
	addr = memremap_pages(&kvmppc_uvmem_pgmap, NUMA_NO_NODE);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto out_free_region;
	}

	pfn_first = res->start >> PAGE_SHIFT;
	pfn_last = pfn_first + (resource_size(res) >> PAGE_SHIFT);
	kvmppc_uvmem_bitmap = kcalloc(BITS_TO_LONGS(pfn_last - pfn_first),
				      sizeof(unsigned long), GFP_KERNEL);
	if (!kvmppc_uvmem_bitmap) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	pr_info("KVMPPC-UVMEM: Secure Memory size 0x%lx\n", size);
	return ret;
out_unmap:
	memunmap_pages(&kvmppc_uvmem_pgmap);
out_free_region:
	release_mem_region(res->start, size);
out:
	return ret;
}

void kvmppc_uvmem_free(void)
{
	if (!kvmppc_uvmem_bitmap)
		return;

	memunmap_pages(&kvmppc_uvmem_pgmap);
	release_mem_region(kvmppc_uvmem_pgmap.range.start,
			   range_len(&kvmppc_uvmem_pgmap.range));
	kfree(kvmppc_uvmem_bitmap);
}
