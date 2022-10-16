// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <linux/lockdep.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/suspend.h>
#include <linux/sched/mm.h>
#include <asm/sgx.h>
#include "encl.h"
#include "encls.h"
#include "sgx.h"

static int sgx_encl_lookup_backing(struct sgx_encl *encl, unsigned long page_index,
			    struct sgx_backing *backing);

#define PCMDS_PER_PAGE (PAGE_SIZE / sizeof(struct sgx_pcmd))
/*
 * 32 PCMD entries share a PCMD page. PCMD_FIRST_MASK is used to
 * determine the page index associated with the first PCMD entry
 * within a PCMD page.
 */
#define PCMD_FIRST_MASK GENMASK(4, 0)

/**
 * reclaimer_writing_to_pcmd() - Query if any enclave page associated with
 *                               a PCMD page is in process of being reclaimed.
 * @encl:        Enclave to which PCMD page belongs
 * @start_addr:  Address of enclave page using first entry within the PCMD page
 *
 * When an enclave page is reclaimed some Paging Crypto MetaData (PCMD) is
 * stored. The PCMD data of a reclaimed enclave page contains enough
 * information for the processor to verify the page at the time
 * it is loaded back into the Enclave Page Cache (EPC).
 *
 * The backing storage to which enclave pages are reclaimed is laid out as
 * follows:
 * Encrypted enclave pages:SECS page:PCMD pages
 *
 * Each PCMD page contains the PCMD metadata of
 * PAGE_SIZE/sizeof(struct sgx_pcmd) enclave pages.
 *
 * A PCMD page can only be truncated if it is (a) empty, and (b) not in the
 * process of getting data (and thus soon being non-empty). (b) is tested with
 * a check if an enclave page sharing the PCMD page is in the process of being
 * reclaimed.
 *
 * The reclaimer sets the SGX_ENCL_PAGE_BEING_RECLAIMED flag when it
 * intends to reclaim that enclave page - it means that the PCMD page
 * associated with that enclave page is about to get some data and thus
 * even if the PCMD page is empty, it should not be truncated.
 *
 * Context: Enclave mutex (&sgx_encl->lock) must be held.
 * Return: 1 if the reclaimer is about to write to the PCMD page
 *         0 if the reclaimer has no intention to write to the PCMD page
 */
static int reclaimer_writing_to_pcmd(struct sgx_encl *encl,
				     unsigned long start_addr)
{
	int reclaimed = 0;
	int i;

	/*
	 * PCMD_FIRST_MASK is based on number of PCMD entries within
	 * PCMD page being 32.
	 */
	BUILD_BUG_ON(PCMDS_PER_PAGE != 32);

	for (i = 0; i < PCMDS_PER_PAGE; i++) {
		struct sgx_encl_page *entry;
		unsigned long addr;

		addr = start_addr + i * PAGE_SIZE;

		/*
		 * Stop when reaching the SECS page - it does not
		 * have a page_array entry and its reclaim is
		 * started and completed with enclave mutex held so
		 * it does not use the SGX_ENCL_PAGE_BEING_RECLAIMED
		 * flag.
		 */
		if (addr == encl->base + encl->size)
			break;

		entry = xa_load(&encl->page_array, PFN_DOWN(addr));
		if (!entry)
			continue;

		/*
		 * VA page slot ID uses same bit as the flag so it is important
		 * to ensure that the page is not already in backing store.
		 */
		if (entry->epc_page &&
		    (entry->desc & SGX_ENCL_PAGE_BEING_RECLAIMED)) {
			reclaimed = 1;
			break;
		}
	}

	return reclaimed;
}

/*
 * Calculate byte offset of a PCMD struct associated with an enclave page. PCMD's
 * follow right after the EPC data in the backing storage. In addition to the
 * visible enclave pages, there's one extra page slot for SECS, before PCMD
 * structs.
 */
static inline pgoff_t sgx_encl_get_backing_page_pcmd_offset(struct sgx_encl *encl,
							    unsigned long page_index)
{
	pgoff_t epc_end_off = encl->size + sizeof(struct sgx_secs);

	return epc_end_off + page_index * sizeof(struct sgx_pcmd);
}

/*
 * Free a page from the backing storage in the given page index.
 */
static inline void sgx_encl_truncate_backing_page(struct sgx_encl *encl, unsigned long page_index)
{
	struct inode *inode = file_inode(encl->backing);

	shmem_truncate_range(inode, PFN_PHYS(page_index), PFN_PHYS(page_index) + PAGE_SIZE - 1);
}

/*
 * ELDU: Load an EPC page as unblocked. For more info, see "OS Management of EPC
 * Pages" in the SDM.
 */
static int __sgx_encl_eldu(struct sgx_encl_page *encl_page,
			   struct sgx_epc_page *epc_page,
			   struct sgx_epc_page *secs_page)
{
	unsigned long va_offset = encl_page->desc & SGX_ENCL_PAGE_VA_OFFSET_MASK;
	struct sgx_encl *encl = encl_page->encl;
	pgoff_t page_index, page_pcmd_off;
	unsigned long pcmd_first_page;
	struct sgx_pageinfo pginfo;
	struct sgx_backing b;
	bool pcmd_page_empty;
	u8 *pcmd_page;
	int ret;

	if (secs_page)
		page_index = PFN_DOWN(encl_page->desc - encl_page->encl->base);
	else
		page_index = PFN_DOWN(encl->size);

	/*
	 * Address of enclave page using the first entry within the PCMD page.
	 */
	pcmd_first_page = PFN_PHYS(page_index & ~PCMD_FIRST_MASK) + encl->base;

	page_pcmd_off = sgx_encl_get_backing_page_pcmd_offset(encl, page_index);

	ret = sgx_encl_lookup_backing(encl, page_index, &b);
	if (ret)
		return ret;

	pginfo.addr = encl_page->desc & PAGE_MASK;
	pginfo.contents = (unsigned long)kmap_atomic(b.contents);
	pcmd_page = kmap_atomic(b.pcmd);
	pginfo.metadata = (unsigned long)pcmd_page + b.pcmd_offset;

	if (secs_page)
		pginfo.secs = (u64)sgx_get_epc_virt_addr(secs_page);
	else
		pginfo.secs = 0;

	ret = __eldu(&pginfo, sgx_get_epc_virt_addr(epc_page),
		     sgx_get_epc_virt_addr(encl_page->va_page->epc_page) + va_offset);
	if (ret) {
		if (encls_failed(ret))
			ENCLS_WARN(ret, "ELDU");

		ret = -EFAULT;
	}

	memset(pcmd_page + b.pcmd_offset, 0, sizeof(struct sgx_pcmd));
	set_page_dirty(b.pcmd);

	/*
	 * The area for the PCMD in the page was zeroed above.  Check if the
	 * whole page is now empty meaning that all PCMD's have been zeroed:
	 */
	pcmd_page_empty = !memchr_inv(pcmd_page, 0, PAGE_SIZE);

	kunmap_atomic(pcmd_page);
	kunmap_atomic((void *)(unsigned long)pginfo.contents);

	get_page(b.pcmd);
	sgx_encl_put_backing(&b);

	sgx_encl_truncate_backing_page(encl, page_index);

	if (pcmd_page_empty && !reclaimer_writing_to_pcmd(encl, pcmd_first_page)) {
		sgx_encl_truncate_backing_page(encl, PFN_DOWN(page_pcmd_off));
		pcmd_page = kmap_atomic(b.pcmd);
		if (memchr_inv(pcmd_page, 0, PAGE_SIZE))
			pr_warn("PCMD page not empty after truncate.\n");
		kunmap_atomic(pcmd_page);
	}

	put_page(b.pcmd);

	return ret;
}

static struct sgx_epc_page *sgx_encl_eldu(struct sgx_encl_page *encl_page,
					  struct sgx_epc_page *secs_page)
{

	unsigned long va_offset = encl_page->desc & SGX_ENCL_PAGE_VA_OFFSET_MASK;
	struct sgx_encl *encl = encl_page->encl;
	struct sgx_epc_page *epc_page;
	int ret;

	epc_page = sgx_alloc_epc_page(encl_page, false);
	if (IS_ERR(epc_page))
		return epc_page;

	ret = __sgx_encl_eldu(encl_page, epc_page, secs_page);
	if (ret) {
		sgx_encl_free_epc_page(epc_page);
		return ERR_PTR(ret);
	}

	sgx_free_va_slot(encl_page->va_page, va_offset);
	list_move(&encl_page->va_page->list, &encl->va_pages);
	encl_page->desc &= ~SGX_ENCL_PAGE_VA_OFFSET_MASK;
	encl_page->epc_page = epc_page;

	return epc_page;
}

static struct sgx_encl_page *__sgx_encl_load_page(struct sgx_encl *encl,
						  struct sgx_encl_page *entry)
{
	struct sgx_epc_page *epc_page;

	/* Entry successfully located. */
	if (entry->epc_page) {
		if (entry->desc & SGX_ENCL_PAGE_BEING_RECLAIMED)
			return ERR_PTR(-EBUSY);

		return entry;
	}

	if (!(encl->secs.epc_page)) {
		epc_page = sgx_encl_eldu(&encl->secs, NULL);
		if (IS_ERR(epc_page))
			return ERR_CAST(epc_page);
	}

	epc_page = sgx_encl_eldu(entry, encl->secs.epc_page);
	if (IS_ERR(epc_page))
		return ERR_CAST(epc_page);

	encl->secs_child_cnt++;
	sgx_mark_page_reclaimable(entry->epc_page);

	return entry;
}

static struct sgx_encl_page *sgx_encl_load_page_in_vma(struct sgx_encl *encl,
						       unsigned long addr,
						       unsigned long vm_flags)
{
	unsigned long vm_prot_bits = vm_flags & (VM_READ | VM_WRITE | VM_EXEC);
	struct sgx_encl_page *entry;

	entry = xa_load(&encl->page_array, PFN_DOWN(addr));
	if (!entry)
		return ERR_PTR(-EFAULT);

	/*
	 * Verify that the page has equal or higher build time
	 * permissions than the VMA permissions (i.e. the subset of {VM_READ,
	 * VM_WRITE, VM_EXECUTE} in vma->vm_flags).
	 */
	if ((entry->vm_max_prot_bits & vm_prot_bits) != vm_prot_bits)
		return ERR_PTR(-EFAULT);

	return __sgx_encl_load_page(encl, entry);
}

struct sgx_encl_page *sgx_encl_load_page(struct sgx_encl *encl,
					 unsigned long addr)
{
	struct sgx_encl_page *entry;

	entry = xa_load(&encl->page_array, PFN_DOWN(addr));
	if (!entry)
		return ERR_PTR(-EFAULT);

	return __sgx_encl_load_page(encl, entry);
}

/**
 * sgx_encl_eaug_page() - Dynamically add page to initialized enclave
 * @vma:	VMA obtained from fault info from where page is accessed
 * @encl:	enclave accessing the page
 * @addr:	address that triggered the page fault
 *
 * When an initialized enclave accesses a page with no backing EPC page
 * on a SGX2 system then the EPC can be added dynamically via the SGX2
 * ENCLS[EAUG] instruction.
 *
 * Returns: Appropriate vm_fault_t: VM_FAULT_NOPAGE when PTE was installed
 * successfully, VM_FAULT_SIGBUS or VM_FAULT_OOM as error otherwise.
 */
static vm_fault_t sgx_encl_eaug_page(struct vm_area_struct *vma,
				     struct sgx_encl *encl, unsigned long addr)
{
	vm_fault_t vmret = VM_FAULT_SIGBUS;
	struct sgx_pageinfo pginfo = {0};
	struct sgx_encl_page *encl_page;
	struct sgx_epc_page *epc_page;
	struct sgx_va_page *va_page;
	unsigned long phys_addr;
	u64 secinfo_flags;
	int ret;

	if (!test_bit(SGX_ENCL_INITIALIZED, &encl->flags))
		return VM_FAULT_SIGBUS;

	/*
	 * Ignore internal permission checking for dynamically added pages.
	 * They matter only for data added during the pre-initialization
	 * phase. The enclave decides the permissions by the means of
	 * EACCEPT, EACCEPTCOPY and EMODPE.
	 */
	secinfo_flags = SGX_SECINFO_R | SGX_SECINFO_W | SGX_SECINFO_X;
	encl_page = sgx_encl_page_alloc(encl, addr - encl->base, secinfo_flags);
	if (IS_ERR(encl_page))
		return VM_FAULT_OOM;

	mutex_lock(&encl->lock);

	epc_page = sgx_alloc_epc_page(encl_page, false);
	if (IS_ERR(epc_page)) {
		if (PTR_ERR(epc_page) == -EBUSY)
			vmret =  VM_FAULT_NOPAGE;
		goto err_out_unlock;
	}

	va_page = sgx_encl_grow(encl, false);
	if (IS_ERR(va_page)) {
		if (PTR_ERR(va_page) == -EBUSY)
			vmret = VM_FAULT_NOPAGE;
		goto err_out_epc;
	}

	if (va_page)
		list_add(&va_page->list, &encl->va_pages);

	ret = xa_insert(&encl->page_array, PFN_DOWN(encl_page->desc),
			encl_page, GFP_KERNEL);
	/*
	 * If ret == -EBUSY then page was created in another flow while
	 * running without encl->lock
	 */
	if (ret)
		goto err_out_shrink;

	pginfo.secs = (unsigned long)sgx_get_epc_virt_addr(encl->secs.epc_page);
	pginfo.addr = encl_page->desc & PAGE_MASK;
	pginfo.metadata = 0;

	ret = __eaug(&pginfo, sgx_get_epc_virt_addr(epc_page));
	if (ret)
		goto err_out;

	encl_page->encl = encl;
	encl_page->epc_page = epc_page;
	encl_page->type = SGX_PAGE_TYPE_REG;
	encl->secs_child_cnt++;

	sgx_mark_page_reclaimable(encl_page->epc_page);

	phys_addr = sgx_get_epc_phys_addr(epc_page);
	/*
	 * Do not undo everything when creating PTE entry fails - next #PF
	 * would find page ready for a PTE.
	 */
	vmret = vmf_insert_pfn(vma, addr, PFN_DOWN(phys_addr));
	if (vmret != VM_FAULT_NOPAGE) {
		mutex_unlock(&encl->lock);
		return VM_FAULT_SIGBUS;
	}
	mutex_unlock(&encl->lock);
	return VM_FAULT_NOPAGE;

err_out:
	xa_erase(&encl->page_array, PFN_DOWN(encl_page->desc));

err_out_shrink:
	sgx_encl_shrink(encl, va_page);
err_out_epc:
	sgx_encl_free_epc_page(epc_page);
err_out_unlock:
	mutex_unlock(&encl->lock);
	kfree(encl_page);

	return vmret;
}

static vm_fault_t sgx_vma_fault(struct vm_fault *vmf)
{
	unsigned long addr = (unsigned long)vmf->address;
	struct vm_area_struct *vma = vmf->vma;
	struct sgx_encl_page *entry;
	unsigned long phys_addr;
	struct sgx_encl *encl;
	vm_fault_t ret;

	encl = vma->vm_private_data;

	/*
	 * It's very unlikely but possible that allocating memory for the
	 * mm_list entry of a forked process failed in sgx_vma_open(). When
	 * this happens, vm_private_data is set to NULL.
	 */
	if (unlikely(!encl))
		return VM_FAULT_SIGBUS;

	/*
	 * The page_array keeps track of all enclave pages, whether they
	 * are swapped out or not. If there is no entry for this page and
	 * the system supports SGX2 then it is possible to dynamically add
	 * a new enclave page. This is only possible for an initialized
	 * enclave that will be checked for right away.
	 */
	if (cpu_feature_enabled(X86_FEATURE_SGX2) &&
	    (!xa_load(&encl->page_array, PFN_DOWN(addr))))
		return sgx_encl_eaug_page(vma, encl, addr);

	mutex_lock(&encl->lock);

	entry = sgx_encl_load_page_in_vma(encl, addr, vma->vm_flags);
	if (IS_ERR(entry)) {
		mutex_unlock(&encl->lock);

		if (PTR_ERR(entry) == -EBUSY)
			return VM_FAULT_NOPAGE;

		return VM_FAULT_SIGBUS;
	}

	phys_addr = sgx_get_epc_phys_addr(entry->epc_page);

	ret = vmf_insert_pfn(vma, addr, PFN_DOWN(phys_addr));
	if (ret != VM_FAULT_NOPAGE) {
		mutex_unlock(&encl->lock);

		return VM_FAULT_SIGBUS;
	}

	sgx_encl_test_and_clear_young(vma->vm_mm, entry);
	mutex_unlock(&encl->lock);

	return VM_FAULT_NOPAGE;
}

static void sgx_vma_open(struct vm_area_struct *vma)
{
	struct sgx_encl *encl = vma->vm_private_data;

	/*
	 * It's possible but unlikely that vm_private_data is NULL. This can
	 * happen in a grandchild of a process, when sgx_encl_mm_add() had
	 * failed to allocate memory in this callback.
	 */
	if (unlikely(!encl))
		return;

	if (sgx_encl_mm_add(encl, vma->vm_mm))
		vma->vm_private_data = NULL;
}


/**
 * sgx_encl_may_map() - Check if a requested VMA mapping is allowed
 * @encl:		an enclave pointer
 * @start:		lower bound of the address range, inclusive
 * @end:		upper bound of the address range, exclusive
 * @vm_flags:		VMA flags
 *
 * Iterate through the enclave pages contained within [@start, @end) to verify
 * that the permissions requested by a subset of {VM_READ, VM_WRITE, VM_EXEC}
 * do not contain any permissions that are not contained in the build time
 * permissions of any of the enclave pages within the given address range.
 *
 * An enclave creator must declare the strongest permissions that will be
 * needed for each enclave page. This ensures that mappings have the identical
 * or weaker permissions than the earlier declared permissions.
 *
 * Return: 0 on success, -EACCES otherwise
 */
int sgx_encl_may_map(struct sgx_encl *encl, unsigned long start,
		     unsigned long end, unsigned long vm_flags)
{
	unsigned long vm_prot_bits = vm_flags & (VM_READ | VM_WRITE | VM_EXEC);
	struct sgx_encl_page *page;
	unsigned long count = 0;
	int ret = 0;

	XA_STATE(xas, &encl->page_array, PFN_DOWN(start));

	/* Disallow mapping outside enclave's address range. */
	if (test_bit(SGX_ENCL_INITIALIZED, &encl->flags) &&
	    (start < encl->base || end > encl->base + encl->size))
		return -EACCES;

	/*
	 * Disallow READ_IMPLIES_EXEC tasks as their VMA permissions might
	 * conflict with the enclave page permissions.
	 */
	if (current->personality & READ_IMPLIES_EXEC)
		return -EACCES;

	mutex_lock(&encl->lock);
	xas_lock(&xas);
	xas_for_each(&xas, page, PFN_DOWN(end - 1)) {
		if (~page->vm_max_prot_bits & vm_prot_bits) {
			ret = -EACCES;
			break;
		}

		/* Reschedule on every XA_CHECK_SCHED iteration. */
		if (!(++count % XA_CHECK_SCHED)) {
			xas_pause(&xas);
			xas_unlock(&xas);
			mutex_unlock(&encl->lock);

			cond_resched();

			mutex_lock(&encl->lock);
			xas_lock(&xas);
		}
	}
	xas_unlock(&xas);
	mutex_unlock(&encl->lock);

	return ret;
}

static int sgx_vma_mprotect(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end, unsigned long newflags)
{
	return sgx_encl_may_map(vma->vm_private_data, start, end, newflags);
}

static int sgx_encl_debug_read(struct sgx_encl *encl, struct sgx_encl_page *page,
			       unsigned long addr, void *data)
{
	unsigned long offset = addr & ~PAGE_MASK;
	int ret;


	ret = __edbgrd(sgx_get_epc_virt_addr(page->epc_page) + offset, data);
	if (ret)
		return -EIO;

	return 0;
}

static int sgx_encl_debug_write(struct sgx_encl *encl, struct sgx_encl_page *page,
				unsigned long addr, void *data)
{
	unsigned long offset = addr & ~PAGE_MASK;
	int ret;

	ret = __edbgwr(sgx_get_epc_virt_addr(page->epc_page) + offset, data);
	if (ret)
		return -EIO;

	return 0;
}

/*
 * Load an enclave page to EPC if required, and take encl->lock.
 */
static struct sgx_encl_page *sgx_encl_reserve_page(struct sgx_encl *encl,
						   unsigned long addr,
						   unsigned long vm_flags)
{
	struct sgx_encl_page *entry;

	for ( ; ; ) {
		mutex_lock(&encl->lock);

		entry = sgx_encl_load_page_in_vma(encl, addr, vm_flags);
		if (PTR_ERR(entry) != -EBUSY)
			break;

		mutex_unlock(&encl->lock);
	}

	if (IS_ERR(entry))
		mutex_unlock(&encl->lock);

	return entry;
}

static int sgx_vma_access(struct vm_area_struct *vma, unsigned long addr,
			  void *buf, int len, int write)
{
	struct sgx_encl *encl = vma->vm_private_data;
	struct sgx_encl_page *entry = NULL;
	char data[sizeof(unsigned long)];
	unsigned long align;
	int offset;
	int cnt;
	int ret = 0;
	int i;

	/*
	 * If process was forked, VMA is still there but vm_private_data is set
	 * to NULL.
	 */
	if (!encl)
		return -EFAULT;

	if (!test_bit(SGX_ENCL_DEBUG, &encl->flags))
		return -EFAULT;

	for (i = 0; i < len; i += cnt) {
		entry = sgx_encl_reserve_page(encl, (addr + i) & PAGE_MASK,
					      vma->vm_flags);
		if (IS_ERR(entry)) {
			ret = PTR_ERR(entry);
			break;
		}

		align = ALIGN_DOWN(addr + i, sizeof(unsigned long));
		offset = (addr + i) & (sizeof(unsigned long) - 1);
		cnt = sizeof(unsigned long) - offset;
		cnt = min(cnt, len - i);

		ret = sgx_encl_debug_read(encl, entry, align, data);
		if (ret)
			goto out;

		if (write) {
			memcpy(data + offset, buf + i, cnt);
			ret = sgx_encl_debug_write(encl, entry, align, data);
			if (ret)
				goto out;
		} else {
			memcpy(buf + i, data + offset, cnt);
		}

out:
		mutex_unlock(&encl->lock);

		if (ret)
			break;
	}

	return ret < 0 ? ret : i;
}

const struct vm_operations_struct sgx_vm_ops = {
	.fault = sgx_vma_fault,
	.mprotect = sgx_vma_mprotect,
	.open = sgx_vma_open,
	.access = sgx_vma_access,
};

/**
 * sgx_encl_release - Destroy an enclave instance
 * @ref:	address of a kref inside &sgx_encl
 *
 * Used together with kref_put(). Frees all the resources associated with the
 * enclave and the instance itself.
 */
void sgx_encl_release(struct kref *ref)
{
	struct sgx_encl *encl = container_of(ref, struct sgx_encl, refcount);
	struct sgx_va_page *va_page;
	struct sgx_encl_page *entry;
	unsigned long index;

	xa_for_each(&encl->page_array, index, entry) {
		if (entry->epc_page) {
			/*
			 * The page and its radix tree entry cannot be freed
			 * if the page is being held by the reclaimer.
			 */
			if (sgx_unmark_page_reclaimable(entry->epc_page))
				continue;

			sgx_encl_free_epc_page(entry->epc_page);
			encl->secs_child_cnt--;
			entry->epc_page = NULL;
		}

		kfree(entry);
		/* Invoke scheduler to prevent soft lockups. */
		cond_resched();
	}

	xa_destroy(&encl->page_array);

	if (!encl->secs_child_cnt && encl->secs.epc_page) {
		sgx_encl_free_epc_page(encl->secs.epc_page);
		encl->secs.epc_page = NULL;
	}

	while (!list_empty(&encl->va_pages)) {
		va_page = list_first_entry(&encl->va_pages, struct sgx_va_page,
					   list);
		list_del(&va_page->list);
		sgx_encl_free_epc_page(va_page->epc_page);
		kfree(va_page);
	}

	if (encl->backing)
		fput(encl->backing);

	cleanup_srcu_struct(&encl->srcu);

	WARN_ON_ONCE(!list_empty(&encl->mm_list));

	/* Detect EPC page leak's. */
	WARN_ON_ONCE(encl->secs_child_cnt);
	WARN_ON_ONCE(encl->secs.epc_page);

	kfree(encl);
}

/*
 * 'mm' is exiting and no longer needs mmu notifications.
 */
static void sgx_mmu_notifier_release(struct mmu_notifier *mn,
				     struct mm_struct *mm)
{
	struct sgx_encl_mm *encl_mm = container_of(mn, struct sgx_encl_mm, mmu_notifier);
	struct sgx_encl_mm *tmp = NULL;

	/*
	 * The enclave itself can remove encl_mm.  Note, objects can't be moved
	 * off an RCU protected list, but deletion is ok.
	 */
	spin_lock(&encl_mm->encl->mm_lock);
	list_for_each_entry(tmp, &encl_mm->encl->mm_list, list) {
		if (tmp == encl_mm) {
			list_del_rcu(&encl_mm->list);
			break;
		}
	}
	spin_unlock(&encl_mm->encl->mm_lock);

	if (tmp == encl_mm) {
		synchronize_srcu(&encl_mm->encl->srcu);
		mmu_notifier_put(mn);
	}
}

static void sgx_mmu_notifier_free(struct mmu_notifier *mn)
{
	struct sgx_encl_mm *encl_mm = container_of(mn, struct sgx_encl_mm, mmu_notifier);

	/* 'encl_mm' is going away, put encl_mm->encl reference: */
	kref_put(&encl_mm->encl->refcount, sgx_encl_release);

	kfree(encl_mm);
}

static const struct mmu_notifier_ops sgx_mmu_notifier_ops = {
	.release		= sgx_mmu_notifier_release,
	.free_notifier		= sgx_mmu_notifier_free,
};

static struct sgx_encl_mm *sgx_encl_find_mm(struct sgx_encl *encl,
					    struct mm_struct *mm)
{
	struct sgx_encl_mm *encl_mm = NULL;
	struct sgx_encl_mm *tmp;
	int idx;

	idx = srcu_read_lock(&encl->srcu);

	list_for_each_entry_rcu(tmp, &encl->mm_list, list) {
		if (tmp->mm == mm) {
			encl_mm = tmp;
			break;
		}
	}

	srcu_read_unlock(&encl->srcu, idx);

	return encl_mm;
}

int sgx_encl_mm_add(struct sgx_encl *encl, struct mm_struct *mm)
{
	struct sgx_encl_mm *encl_mm;
	int ret;

	/*
	 * Even though a single enclave may be mapped into an mm more than once,
	 * each 'mm' only appears once on encl->mm_list. This is guaranteed by
	 * holding the mm's mmap lock for write before an mm can be added or
	 * remove to an encl->mm_list.
	 */
	mmap_assert_write_locked(mm);

	/*
	 * It's possible that an entry already exists in the mm_list, because it
	 * is removed only on VFS release or process exit.
	 */
	if (sgx_encl_find_mm(encl, mm))
		return 0;

	encl_mm = kzalloc(sizeof(*encl_mm), GFP_KERNEL);
	if (!encl_mm)
		return -ENOMEM;

	/* Grab a refcount for the encl_mm->encl reference: */
	kref_get(&encl->refcount);
	encl_mm->encl = encl;
	encl_mm->mm = mm;
	encl_mm->mmu_notifier.ops = &sgx_mmu_notifier_ops;

	ret = __mmu_notifier_register(&encl_mm->mmu_notifier, mm);
	if (ret) {
		kfree(encl_mm);
		return ret;
	}

	spin_lock(&encl->mm_lock);
	list_add_rcu(&encl_mm->list, &encl->mm_list);
	/* Pairs with smp_rmb() in sgx_zap_enclave_ptes(). */
	smp_wmb();
	encl->mm_list_version++;
	spin_unlock(&encl->mm_lock);

	return 0;
}

/**
 * sgx_encl_cpumask() - Query which CPUs might be accessing the enclave
 * @encl: the enclave
 *
 * Some SGX functions require that no cached linear-to-physical address
 * mappings are present before they can succeed. For example, ENCLS[EWB]
 * copies a page from the enclave page cache to regular main memory but
 * it fails if it cannot ensure that there are no cached
 * linear-to-physical address mappings referring to the page.
 *
 * SGX hardware flushes all cached linear-to-physical mappings on a CPU
 * when an enclave is exited via ENCLU[EEXIT] or an Asynchronous Enclave
 * Exit (AEX). Exiting an enclave will thus ensure cached linear-to-physical
 * address mappings are cleared but coordination with the tracking done within
 * the SGX hardware is needed to support the SGX functions that depend on this
 * cache clearing.
 *
 * When the ENCLS[ETRACK] function is issued on an enclave the hardware
 * tracks threads operating inside the enclave at that time. The SGX
 * hardware tracking require that all the identified threads must have
 * exited the enclave in order to flush the mappings before a function such
 * as ENCLS[EWB] will be permitted
 *
 * The following flow is used to support SGX functions that require that
 * no cached linear-to-physical address mappings are present:
 * 1) Execute ENCLS[ETRACK] to initiate hardware tracking.
 * 2) Use this function (sgx_encl_cpumask()) to query which CPUs might be
 *    accessing the enclave.
 * 3) Send IPI to identified CPUs, kicking them out of the enclave and
 *    thus flushing all locally cached linear-to-physical address mappings.
 * 4) Execute SGX function.
 *
 * Context: It is required to call this function after ENCLS[ETRACK].
 *          This will ensure that if any new mm appears (racing with
 *          sgx_encl_mm_add()) then the new mm will enter into the
 *          enclave with fresh linear-to-physical address mappings.
 *
 *          It is required that all IPIs are completed before a new
 *          ENCLS[ETRACK] is issued so be sure to protect steps 1 to 3
 *          of the above flow with the enclave's mutex.
 *
 * Return: cpumask of CPUs that might be accessing @encl
 */
const cpumask_t *sgx_encl_cpumask(struct sgx_encl *encl)
{
	cpumask_t *cpumask = &encl->cpumask;
	struct sgx_encl_mm *encl_mm;
	int idx;

	cpumask_clear(cpumask);

	idx = srcu_read_lock(&encl->srcu);

	list_for_each_entry_rcu(encl_mm, &encl->mm_list, list) {
		if (!mmget_not_zero(encl_mm->mm))
			continue;

		cpumask_or(cpumask, cpumask, mm_cpumask(encl_mm->mm));

		mmput_async(encl_mm->mm);
	}

	srcu_read_unlock(&encl->srcu, idx);

	return cpumask;
}

static struct page *sgx_encl_get_backing_page(struct sgx_encl *encl,
					      pgoff_t index)
{
	struct address_space *mapping = encl->backing->f_mapping;
	gfp_t gfpmask = mapping_gfp_mask(mapping);

	return shmem_read_mapping_page_gfp(mapping, index, gfpmask);
}

/**
 * __sgx_encl_get_backing() - Pin the backing storage
 * @encl:	an enclave pointer
 * @page_index:	enclave page index
 * @backing:	data for accessing backing storage for the page
 *
 * Pin the backing storage pages for storing the encrypted contents and Paging
 * Crypto MetaData (PCMD) of an enclave page.
 *
 * Return:
 *   0 on success,
 *   -errno otherwise.
 */
static int __sgx_encl_get_backing(struct sgx_encl *encl, unsigned long page_index,
			 struct sgx_backing *backing)
{
	pgoff_t page_pcmd_off = sgx_encl_get_backing_page_pcmd_offset(encl, page_index);
	struct page *contents;
	struct page *pcmd;

	contents = sgx_encl_get_backing_page(encl, page_index);
	if (IS_ERR(contents))
		return PTR_ERR(contents);

	pcmd = sgx_encl_get_backing_page(encl, PFN_DOWN(page_pcmd_off));
	if (IS_ERR(pcmd)) {
		put_page(contents);
		return PTR_ERR(pcmd);
	}

	backing->contents = contents;
	backing->pcmd = pcmd;
	backing->pcmd_offset = page_pcmd_off & (PAGE_SIZE - 1);

	return 0;
}

/*
 * When called from ksgxd, returns the mem_cgroup of a struct mm stored
 * in the enclave's mm_list. When not called from ksgxd, just returns
 * the mem_cgroup of the current task.
 */
static struct mem_cgroup *sgx_encl_get_mem_cgroup(struct sgx_encl *encl)
{
	struct mem_cgroup *memcg = NULL;
	struct sgx_encl_mm *encl_mm;
	int idx;

	/*
	 * If called from normal task context, return the mem_cgroup
	 * of the current task's mm. The remainder of the handling is for
	 * ksgxd.
	 */
	if (!current_is_ksgxd())
		return get_mem_cgroup_from_mm(current->mm);

	/*
	 * Search the enclave's mm_list to find an mm associated with
	 * this enclave to charge the allocation to.
	 */
	idx = srcu_read_lock(&encl->srcu);

	list_for_each_entry_rcu(encl_mm, &encl->mm_list, list) {
		if (!mmget_not_zero(encl_mm->mm))
			continue;

		memcg = get_mem_cgroup_from_mm(encl_mm->mm);

		mmput_async(encl_mm->mm);

		break;
	}

	srcu_read_unlock(&encl->srcu, idx);

	/*
	 * In the rare case that there isn't an mm associated with
	 * the enclave, set memcg to the current active mem_cgroup.
	 * This will be the root mem_cgroup if there is no active
	 * mem_cgroup.
	 */
	if (!memcg)
		return get_mem_cgroup_from_mm(NULL);

	return memcg;
}

/**
 * sgx_encl_alloc_backing() - create a new backing storage page
 * @encl:	an enclave pointer
 * @page_index:	enclave page index
 * @backing:	data for accessing backing storage for the page
 *
 * When called from ksgxd, sets the active memcg from one of the
 * mms in the enclave's mm_list prior to any backing page allocation,
 * in order to ensure that shmem page allocations are charged to the
 * enclave.  Create a backing page for loading data back into an EPC page with
 * ELDU.  This function takes a reference on a new backing page which
 * must be dropped with a corresponding call to sgx_encl_put_backing().
 *
 * Return:
 *   0 on success,
 *   -errno otherwise.
 */
int sgx_encl_alloc_backing(struct sgx_encl *encl, unsigned long page_index,
			   struct sgx_backing *backing)
{
	struct mem_cgroup *encl_memcg = sgx_encl_get_mem_cgroup(encl);
	struct mem_cgroup *memcg = set_active_memcg(encl_memcg);
	int ret;

	ret = __sgx_encl_get_backing(encl, page_index, backing);

	set_active_memcg(memcg);
	mem_cgroup_put(encl_memcg);

	return ret;
}

/**
 * sgx_encl_lookup_backing() - retrieve an existing backing storage page
 * @encl:	an enclave pointer
 * @page_index:	enclave page index
 * @backing:	data for accessing backing storage for the page
 *
 * Retrieve a backing page for loading data back into an EPC page with ELDU.
 * It is the caller's responsibility to ensure that it is appropriate to use
 * sgx_encl_lookup_backing() rather than sgx_encl_alloc_backing(). If lookup is
 * not used correctly, this will cause an allocation which is not accounted for.
 * This function takes a reference on an existing backing page which must be
 * dropped with a corresponding call to sgx_encl_put_backing().
 *
 * Return:
 *   0 on success,
 *   -errno otherwise.
 */
static int sgx_encl_lookup_backing(struct sgx_encl *encl, unsigned long page_index,
			   struct sgx_backing *backing)
{
	return __sgx_encl_get_backing(encl, page_index, backing);
}

/**
 * sgx_encl_put_backing() - Unpin the backing storage
 * @backing:	data for accessing backing storage for the page
 */
void sgx_encl_put_backing(struct sgx_backing *backing)
{
	put_page(backing->pcmd);
	put_page(backing->contents);
}

static int sgx_encl_test_and_clear_young_cb(pte_t *ptep, unsigned long addr,
					    void *data)
{
	pte_t pte;
	int ret;

	ret = pte_young(*ptep);
	if (ret) {
		pte = pte_mkold(*ptep);
		set_pte_at((struct mm_struct *)data, addr, ptep, pte);
	}

	return ret;
}

/**
 * sgx_encl_test_and_clear_young() - Test and reset the accessed bit
 * @mm:		mm_struct that is checked
 * @page:	enclave page to be tested for recent access
 *
 * Checks the Access (A) bit from the PTE corresponding to the enclave page and
 * clears it.
 *
 * Return: 1 if the page has been recently accessed and 0 if not.
 */
int sgx_encl_test_and_clear_young(struct mm_struct *mm,
				  struct sgx_encl_page *page)
{
	unsigned long addr = page->desc & PAGE_MASK;
	struct sgx_encl *encl = page->encl;
	struct vm_area_struct *vma;
	int ret;

	ret = sgx_encl_find(mm, addr, &vma);
	if (ret)
		return 0;

	if (encl != vma->vm_private_data)
		return 0;

	ret = apply_to_page_range(vma->vm_mm, addr, PAGE_SIZE,
				  sgx_encl_test_and_clear_young_cb, vma->vm_mm);
	if (ret < 0)
		return 0;

	return ret;
}

struct sgx_encl_page *sgx_encl_page_alloc(struct sgx_encl *encl,
					  unsigned long offset,
					  u64 secinfo_flags)
{
	struct sgx_encl_page *encl_page;
	unsigned long prot;

	encl_page = kzalloc(sizeof(*encl_page), GFP_KERNEL);
	if (!encl_page)
		return ERR_PTR(-ENOMEM);

	encl_page->desc = encl->base + offset;
	encl_page->encl = encl;

	prot = _calc_vm_trans(secinfo_flags, SGX_SECINFO_R, PROT_READ)  |
	       _calc_vm_trans(secinfo_flags, SGX_SECINFO_W, PROT_WRITE) |
	       _calc_vm_trans(secinfo_flags, SGX_SECINFO_X, PROT_EXEC);

	/*
	 * TCS pages must always RW set for CPU access while the SECINFO
	 * permissions are *always* zero - the CPU ignores the user provided
	 * values and silently overwrites them with zero permissions.
	 */
	if ((secinfo_flags & SGX_SECINFO_PAGE_TYPE_MASK) == SGX_SECINFO_TCS)
		prot |= PROT_READ | PROT_WRITE;

	/* Calculate maximum of the VM flags for the page. */
	encl_page->vm_max_prot_bits = calc_vm_prot_bits(prot, 0);

	return encl_page;
}

/**
 * sgx_zap_enclave_ptes() - remove PTEs mapping the address from enclave
 * @encl: the enclave
 * @addr: page aligned pointer to single page for which PTEs will be removed
 *
 * Multiple VMAs may have an enclave page mapped. Remove the PTE mapping
 * @addr from each VMA. Ensure that page fault handler is ready to handle
 * new mappings of @addr before calling this function.
 */
void sgx_zap_enclave_ptes(struct sgx_encl *encl, unsigned long addr)
{
	unsigned long mm_list_version;
	struct sgx_encl_mm *encl_mm;
	struct vm_area_struct *vma;
	int idx, ret;

	do {
		mm_list_version = encl->mm_list_version;

		/* Pairs with smp_wmb() in sgx_encl_mm_add(). */
		smp_rmb();

		idx = srcu_read_lock(&encl->srcu);

		list_for_each_entry_rcu(encl_mm, &encl->mm_list, list) {
			if (!mmget_not_zero(encl_mm->mm))
				continue;

			mmap_read_lock(encl_mm->mm);

			ret = sgx_encl_find(encl_mm->mm, addr, &vma);
			if (!ret && encl == vma->vm_private_data)
				zap_vma_ptes(vma, addr, PAGE_SIZE);

			mmap_read_unlock(encl_mm->mm);

			mmput_async(encl_mm->mm);
		}

		srcu_read_unlock(&encl->srcu, idx);
	} while (unlikely(encl->mm_list_version != mm_list_version));
}

/**
 * sgx_alloc_va_page() - Allocate a Version Array (VA) page
 * @reclaim: Reclaim EPC pages directly if none available. Enclave
 *           mutex should not be held if this is set.
 *
 * Allocate a free EPC page and convert it to a Version Array (VA) page.
 *
 * Return:
 *   a VA page,
 *   -errno otherwise
 */
struct sgx_epc_page *sgx_alloc_va_page(bool reclaim)
{
	struct sgx_epc_page *epc_page;
	int ret;

	epc_page = sgx_alloc_epc_page(NULL, reclaim);
	if (IS_ERR(epc_page))
		return ERR_CAST(epc_page);

	ret = __epa(sgx_get_epc_virt_addr(epc_page));
	if (ret) {
		WARN_ONCE(1, "EPA returned %d (0x%x)", ret, ret);
		sgx_encl_free_epc_page(epc_page);
		return ERR_PTR(-EFAULT);
	}

	return epc_page;
}

/**
 * sgx_alloc_va_slot - allocate a VA slot
 * @va_page:	a &struct sgx_va_page instance
 *
 * Allocates a slot from a &struct sgx_va_page instance.
 *
 * Return: offset of the slot inside the VA page
 */
unsigned int sgx_alloc_va_slot(struct sgx_va_page *va_page)
{
	int slot = find_first_zero_bit(va_page->slots, SGX_VA_SLOT_COUNT);

	if (slot < SGX_VA_SLOT_COUNT)
		set_bit(slot, va_page->slots);

	return slot << 3;
}

/**
 * sgx_free_va_slot - free a VA slot
 * @va_page:	a &struct sgx_va_page instance
 * @offset:	offset of the slot inside the VA page
 *
 * Frees a slot from a &struct sgx_va_page instance.
 */
void sgx_free_va_slot(struct sgx_va_page *va_page, unsigned int offset)
{
	clear_bit(offset >> 3, va_page->slots);
}

/**
 * sgx_va_page_full - is the VA page full?
 * @va_page:	a &struct sgx_va_page instance
 *
 * Return: true if all slots have been taken
 */
bool sgx_va_page_full(struct sgx_va_page *va_page)
{
	int slot = find_first_zero_bit(va_page->slots, SGX_VA_SLOT_COUNT);

	return slot == SGX_VA_SLOT_COUNT;
}

/**
 * sgx_encl_free_epc_page - free an EPC page assigned to an enclave
 * @page:	EPC page to be freed
 *
 * Free an EPC page assigned to an enclave. It does EREMOVE for the page, and
 * only upon success, it puts the page back to free page list.  Otherwise, it
 * gives a WARNING to indicate page is leaked.
 */
void sgx_encl_free_epc_page(struct sgx_epc_page *page)
{
	int ret;

	WARN_ON_ONCE(page->flags & SGX_EPC_PAGE_RECLAIMER_TRACKED);

	ret = __eremove(sgx_get_epc_virt_addr(page));
	if (WARN_ONCE(ret, EREMOVE_ERROR_MESSAGE, ret, ret))
		return;

	sgx_free_epc_page(page);
}
