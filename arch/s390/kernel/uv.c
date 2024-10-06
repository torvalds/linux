// SPDX-License-Identifier: GPL-2.0
/*
 * Common Ultravisor functions and initialization
 *
 * Copyright IBM Corp. 2019, 2020
 */
#define KMSG_COMPONENT "prot_virt"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sizes.h>
#include <linux/bitmap.h>
#include <linux/memblock.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/pagewalk.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/uv.h>

#if !IS_ENABLED(CONFIG_KVM)
unsigned long __gmap_translate(struct gmap *gmap, unsigned long gaddr)
{
	return 0;
}

int gmap_fault(struct gmap *gmap, unsigned long gaddr,
	       unsigned int fault_flags)
{
	return 0;
}
#endif

/* the bootdata_preserved fields come from ones in arch/s390/boot/uv.c */
int __bootdata_preserved(prot_virt_guest);
EXPORT_SYMBOL(prot_virt_guest);

/*
 * uv_info contains both host and guest information but it's currently only
 * expected to be used within modules if it's the KVM module or for
 * any PV guest module.
 *
 * The kernel itself will write these values once in uv_query_info()
 * and then make some of them readable via a sysfs interface.
 */
struct uv_info __bootdata_preserved(uv_info);
EXPORT_SYMBOL(uv_info);

int __bootdata_preserved(prot_virt_host);
EXPORT_SYMBOL(prot_virt_host);

static int __init uv_init(phys_addr_t stor_base, unsigned long stor_len)
{
	struct uv_cb_init uvcb = {
		.header.cmd = UVC_CMD_INIT_UV,
		.header.len = sizeof(uvcb),
		.stor_origin = stor_base,
		.stor_len = stor_len,
	};

	if (uv_call(0, (uint64_t)&uvcb)) {
		pr_err("Ultravisor init failed with rc: 0x%x rrc: 0%x\n",
		       uvcb.header.rc, uvcb.header.rrc);
		return -1;
	}
	return 0;
}

void __init setup_uv(void)
{
	void *uv_stor_base;

	if (!is_prot_virt_host())
		return;

	uv_stor_base = memblock_alloc_try_nid(
		uv_info.uv_base_stor_len, SZ_1M, SZ_2G,
		MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
	if (!uv_stor_base) {
		pr_warn("Failed to reserve %lu bytes for ultravisor base storage\n",
			uv_info.uv_base_stor_len);
		goto fail;
	}

	if (uv_init(__pa(uv_stor_base), uv_info.uv_base_stor_len)) {
		memblock_free(uv_stor_base, uv_info.uv_base_stor_len);
		goto fail;
	}

	pr_info("Reserving %luMB as ultravisor base storage\n",
		uv_info.uv_base_stor_len >> 20);
	return;
fail:
	pr_info("Disabling support for protected virtualization");
	prot_virt_host = 0;
}

/*
 * Requests the Ultravisor to pin the page in the shared state. This will
 * cause an intercept when the guest attempts to unshare the pinned page.
 */
int uv_pin_shared(unsigned long paddr)
{
	struct uv_cb_cfs uvcb = {
		.header.cmd = UVC_CMD_PIN_PAGE_SHARED,
		.header.len = sizeof(uvcb),
		.paddr = paddr,
	};

	if (uv_call(0, (u64)&uvcb))
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(uv_pin_shared);

/*
 * Requests the Ultravisor to destroy a guest page and make it
 * accessible to the host. The destroy clears the page instead of
 * exporting.
 *
 * @paddr: Absolute host address of page to be destroyed
 */
static int uv_destroy(unsigned long paddr)
{
	struct uv_cb_cfs uvcb = {
		.header.cmd = UVC_CMD_DESTR_SEC_STOR,
		.header.len = sizeof(uvcb),
		.paddr = paddr
	};

	if (uv_call(0, (u64)&uvcb)) {
		/*
		 * Older firmware uses 107/d as an indication of a non secure
		 * page. Let us emulate the newer variant (no-op).
		 */
		if (uvcb.header.rc == 0x107 && uvcb.header.rrc == 0xd)
			return 0;
		return -EINVAL;
	}
	return 0;
}

/*
 * The caller must already hold a reference to the folio
 */
int uv_destroy_folio(struct folio *folio)
{
	int rc;

	/* See gmap_make_secure(): large folios cannot be secure */
	if (unlikely(folio_test_large(folio)))
		return 0;

	folio_get(folio);
	rc = uv_destroy(folio_to_phys(folio));
	if (!rc)
		clear_bit(PG_arch_1, &folio->flags);
	folio_put(folio);
	return rc;
}

/*
 * The present PTE still indirectly holds a folio reference through the mapping.
 */
int uv_destroy_pte(pte_t pte)
{
	VM_WARN_ON(!pte_present(pte));
	return uv_destroy_folio(pfn_folio(pte_pfn(pte)));
}

/*
 * Requests the Ultravisor to encrypt a guest page and make it
 * accessible to the host for paging (export).
 *
 * @paddr: Absolute host address of page to be exported
 */
static int uv_convert_from_secure(unsigned long paddr)
{
	struct uv_cb_cfs uvcb = {
		.header.cmd = UVC_CMD_CONV_FROM_SEC_STOR,
		.header.len = sizeof(uvcb),
		.paddr = paddr
	};

	if (uv_call(0, (u64)&uvcb))
		return -EINVAL;
	return 0;
}

/*
 * The caller must already hold a reference to the folio.
 */
static int uv_convert_from_secure_folio(struct folio *folio)
{
	int rc;

	/* See gmap_make_secure(): large folios cannot be secure */
	if (unlikely(folio_test_large(folio)))
		return 0;

	folio_get(folio);
	rc = uv_convert_from_secure(folio_to_phys(folio));
	if (!rc)
		clear_bit(PG_arch_1, &folio->flags);
	folio_put(folio);
	return rc;
}

/*
 * The present PTE still indirectly holds a folio reference through the mapping.
 */
int uv_convert_from_secure_pte(pte_t pte)
{
	VM_WARN_ON(!pte_present(pte));
	return uv_convert_from_secure_folio(pfn_folio(pte_pfn(pte)));
}

/*
 * Calculate the expected ref_count for a folio that would otherwise have no
 * further pins. This was cribbed from similar functions in other places in
 * the kernel, but with some slight modifications. We know that a secure
 * folio can not be a large folio, for example.
 */
static int expected_folio_refs(struct folio *folio)
{
	int res;

	res = folio_mapcount(folio);
	if (folio_test_swapcache(folio)) {
		res++;
	} else if (folio_mapping(folio)) {
		res++;
		if (folio->private)
			res++;
	}
	return res;
}

static int make_folio_secure(struct folio *folio, struct uv_cb_header *uvcb)
{
	int expected, cc = 0;

	if (folio_test_writeback(folio))
		return -EAGAIN;
	expected = expected_folio_refs(folio);
	if (!folio_ref_freeze(folio, expected))
		return -EBUSY;
	set_bit(PG_arch_1, &folio->flags);
	/*
	 * If the UVC does not succeed or fail immediately, we don't want to
	 * loop for long, or we might get stall notifications.
	 * On the other hand, this is a complex scenario and we are holding a lot of
	 * locks, so we can't easily sleep and reschedule. We try only once,
	 * and if the UVC returned busy or partial completion, we return
	 * -EAGAIN and we let the callers deal with it.
	 */
	cc = __uv_call(0, (u64)uvcb);
	folio_ref_unfreeze(folio, expected);
	/*
	 * Return -ENXIO if the folio was not mapped, -EINVAL for other errors.
	 * If busy or partially completed, return -EAGAIN.
	 */
	if (cc == UVC_CC_OK)
		return 0;
	else if (cc == UVC_CC_BUSY || cc == UVC_CC_PARTIAL)
		return -EAGAIN;
	return uvcb->rc == 0x10a ? -ENXIO : -EINVAL;
}

/**
 * should_export_before_import - Determine whether an export is needed
 * before an import-like operation
 * @uvcb: the Ultravisor control block of the UVC to be performed
 * @mm: the mm of the process
 *
 * Returns whether an export is needed before every import-like operation.
 * This is needed for shared pages, which don't trigger a secure storage
 * exception when accessed from a different guest.
 *
 * Although considered as one, the Unpin Page UVC is not an actual import,
 * so it is not affected.
 *
 * No export is needed also when there is only one protected VM, because the
 * page cannot belong to the wrong VM in that case (there is no "other VM"
 * it can belong to).
 *
 * Return: true if an export is needed before every import, otherwise false.
 */
static bool should_export_before_import(struct uv_cb_header *uvcb, struct mm_struct *mm)
{
	/*
	 * The misc feature indicates, among other things, that importing a
	 * shared page from a different protected VM will automatically also
	 * transfer its ownership.
	 */
	if (uv_has_feature(BIT_UV_FEAT_MISC))
		return false;
	if (uvcb->cmd == UVC_CMD_UNPIN_PAGE_SHARED)
		return false;
	return atomic_read(&mm->context.protected_count) > 1;
}

/*
 * Drain LRU caches: the local one on first invocation and the ones of all
 * CPUs on successive invocations. Returns "true" on the first invocation.
 */
static bool drain_lru(bool *drain_lru_called)
{
	/*
	 * If we have tried a local drain and the folio refcount
	 * still does not match our expected safe value, try with a
	 * system wide drain. This is needed if the pagevecs holding
	 * the page are on a different CPU.
	 */
	if (*drain_lru_called) {
		lru_add_drain_all();
		/* We give up here, don't retry immediately. */
		return false;
	}
	/*
	 * We are here if the folio refcount does not match the
	 * expected safe value. The main culprits are usually
	 * pagevecs. With lru_add_drain() we drain the pagevecs
	 * on the local CPU so that hopefully the refcount will
	 * reach the expected safe value.
	 */
	lru_add_drain();
	*drain_lru_called = true;
	/* The caller should try again immediately */
	return true;
}

/*
 * Requests the Ultravisor to make a page accessible to a guest.
 * If it's brought in the first time, it will be cleared. If
 * it has been exported before, it will be decrypted and integrity
 * checked.
 */
int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb)
{
	struct vm_area_struct *vma;
	bool drain_lru_called = false;
	spinlock_t *ptelock;
	unsigned long uaddr;
	struct folio *folio;
	pte_t *ptep;
	int rc;

again:
	rc = -EFAULT;
	mmap_read_lock(gmap->mm);

	uaddr = __gmap_translate(gmap, gaddr);
	if (IS_ERR_VALUE(uaddr))
		goto out;
	vma = vma_lookup(gmap->mm, uaddr);
	if (!vma)
		goto out;
	/*
	 * Secure pages cannot be huge and userspace should not combine both.
	 * In case userspace does it anyway this will result in an -EFAULT for
	 * the unpack. The guest is thus never reaching secure mode. If
	 * userspace is playing dirty tricky with mapping huge pages later
	 * on this will result in a segmentation fault.
	 */
	if (is_vm_hugetlb_page(vma))
		goto out;

	rc = -ENXIO;
	ptep = get_locked_pte(gmap->mm, uaddr, &ptelock);
	if (!ptep)
		goto out;
	if (pte_present(*ptep) && !(pte_val(*ptep) & _PAGE_INVALID) && pte_write(*ptep)) {
		folio = page_folio(pte_page(*ptep));
		rc = -EAGAIN;
		if (folio_test_large(folio)) {
			rc = -E2BIG;
		} else if (folio_trylock(folio)) {
			if (should_export_before_import(uvcb, gmap->mm))
				uv_convert_from_secure(PFN_PHYS(folio_pfn(folio)));
			rc = make_folio_secure(folio, uvcb);
			folio_unlock(folio);
		}

		/*
		 * Once we drop the PTL, the folio may get unmapped and
		 * freed immediately. We need a temporary reference.
		 */
		if (rc == -EAGAIN || rc == -E2BIG)
			folio_get(folio);
	}
	pte_unmap_unlock(ptep, ptelock);
out:
	mmap_read_unlock(gmap->mm);

	switch (rc) {
	case -E2BIG:
		folio_lock(folio);
		rc = split_folio(folio);
		folio_unlock(folio);
		folio_put(folio);

		switch (rc) {
		case 0:
			/* Splitting succeeded, try again immediately. */
			goto again;
		case -EAGAIN:
			/* Additional folio references. */
			if (drain_lru(&drain_lru_called))
				goto again;
			return -EAGAIN;
		case -EBUSY:
			/* Unexpected race. */
			return -EAGAIN;
		}
		WARN_ON_ONCE(1);
		return -ENXIO;
	case -EAGAIN:
		/*
		 * If we are here because the UVC returned busy or partial
		 * completion, this is just a useless check, but it is safe.
		 */
		folio_wait_writeback(folio);
		folio_put(folio);
		return -EAGAIN;
	case -EBUSY:
		/* Additional folio references. */
		if (drain_lru(&drain_lru_called))
			goto again;
		return -EAGAIN;
	case -ENXIO:
		if (gmap_fault(gmap, gaddr, FAULT_FLAG_WRITE))
			return -EFAULT;
		return -EAGAIN;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_make_secure);

int gmap_convert_to_secure(struct gmap *gmap, unsigned long gaddr)
{
	struct uv_cb_cts uvcb = {
		.header.cmd = UVC_CMD_CONV_TO_SEC_STOR,
		.header.len = sizeof(uvcb),
		.guest_handle = gmap->guest_handle,
		.gaddr = gaddr,
	};

	return gmap_make_secure(gmap, gaddr, &uvcb);
}
EXPORT_SYMBOL_GPL(gmap_convert_to_secure);

/**
 * gmap_destroy_page - Destroy a guest page.
 * @gmap: the gmap of the guest
 * @gaddr: the guest address to destroy
 *
 * An attempt will be made to destroy the given guest page. If the attempt
 * fails, an attempt is made to export the page. If both attempts fail, an
 * appropriate error is returned.
 */
int gmap_destroy_page(struct gmap *gmap, unsigned long gaddr)
{
	struct vm_area_struct *vma;
	struct folio_walk fw;
	unsigned long uaddr;
	struct folio *folio;
	int rc;

	rc = -EFAULT;
	mmap_read_lock(gmap->mm);

	uaddr = __gmap_translate(gmap, gaddr);
	if (IS_ERR_VALUE(uaddr))
		goto out;
	vma = vma_lookup(gmap->mm, uaddr);
	if (!vma)
		goto out;
	/*
	 * Huge pages should not be able to become secure
	 */
	if (is_vm_hugetlb_page(vma))
		goto out;

	rc = 0;
	folio = folio_walk_start(&fw, vma, uaddr, 0);
	if (!folio)
		goto out;
	/*
	 * See gmap_make_secure(): large folios cannot be secure. Small
	 * folio implies FW_LEVEL_PTE.
	 */
	if (folio_test_large(folio) || !pte_write(fw.pte))
		goto out_walk_end;
	rc = uv_destroy_folio(folio);
	/*
	 * Fault handlers can race; it is possible that two CPUs will fault
	 * on the same secure page. One CPU can destroy the page, reboot,
	 * re-enter secure mode and import it, while the second CPU was
	 * stuck at the beginning of the handler. At some point the second
	 * CPU will be able to progress, and it will not be able to destroy
	 * the page. In that case we do not want to terminate the process,
	 * we instead try to export the page.
	 */
	if (rc)
		rc = uv_convert_from_secure_folio(folio);
out_walk_end:
	folio_walk_end(&fw, vma);
out:
	mmap_read_unlock(gmap->mm);
	return rc;
}
EXPORT_SYMBOL_GPL(gmap_destroy_page);

/*
 * To be called with the folio locked or with an extra reference! This will
 * prevent gmap_make_secure from touching the folio concurrently. Having 2
 * parallel arch_make_folio_accessible is fine, as the UV calls will become a
 * no-op if the folio is already exported.
 */
int arch_make_folio_accessible(struct folio *folio)
{
	int rc = 0;

	/* See gmap_make_secure(): large folios cannot be secure */
	if (unlikely(folio_test_large(folio)))
		return 0;

	/*
	 * PG_arch_1 is used in 2 places:
	 * 1. for storage keys of hugetlb folios and KVM
	 * 2. As an indication that this small folio might be secure. This can
	 *    overindicate, e.g. we set the bit before calling
	 *    convert_to_secure.
	 * As secure pages are never large folios, both variants can co-exists.
	 */
	if (!test_bit(PG_arch_1, &folio->flags))
		return 0;

	rc = uv_pin_shared(folio_to_phys(folio));
	if (!rc) {
		clear_bit(PG_arch_1, &folio->flags);
		return 0;
	}

	rc = uv_convert_from_secure(folio_to_phys(folio));
	if (!rc) {
		clear_bit(PG_arch_1, &folio->flags);
		return 0;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(arch_make_folio_accessible);

static ssize_t uv_query_facilities(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n%lx\n%lx\n%lx\n",
			  uv_info.inst_calls_list[0],
			  uv_info.inst_calls_list[1],
			  uv_info.inst_calls_list[2],
			  uv_info.inst_calls_list[3]);
}

static struct kobj_attribute uv_query_facilities_attr =
	__ATTR(facilities, 0444, uv_query_facilities, NULL);

static ssize_t uv_query_supp_se_hdr_ver(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_se_hdr_ver);
}

static struct kobj_attribute uv_query_supp_se_hdr_ver_attr =
	__ATTR(supp_se_hdr_ver, 0444, uv_query_supp_se_hdr_ver, NULL);

static ssize_t uv_query_supp_se_hdr_pcf(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_se_hdr_pcf);
}

static struct kobj_attribute uv_query_supp_se_hdr_pcf_attr =
	__ATTR(supp_se_hdr_pcf, 0444, uv_query_supp_se_hdr_pcf, NULL);

static ssize_t uv_query_dump_cpu_len(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.guest_cpu_stor_len);
}

static struct kobj_attribute uv_query_dump_cpu_len_attr =
	__ATTR(uv_query_dump_cpu_len, 0444, uv_query_dump_cpu_len, NULL);

static ssize_t uv_query_dump_storage_state_len(struct kobject *kobj,
					       struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.conf_dump_storage_state_len);
}

static struct kobj_attribute uv_query_dump_storage_state_len_attr =
	__ATTR(dump_storage_state_len, 0444, uv_query_dump_storage_state_len, NULL);

static ssize_t uv_query_dump_finalize_len(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.conf_dump_finalize_len);
}

static struct kobj_attribute uv_query_dump_finalize_len_attr =
	__ATTR(dump_finalize_len, 0444, uv_query_dump_finalize_len, NULL);

static ssize_t uv_query_feature_indications(struct kobject *kobj,
					    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.uv_feature_indications);
}

static struct kobj_attribute uv_query_feature_indications_attr =
	__ATTR(feature_indications, 0444, uv_query_feature_indications, NULL);

static ssize_t uv_query_max_guest_cpus(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", uv_info.max_guest_cpu_id + 1);
}

static struct kobj_attribute uv_query_max_guest_cpus_attr =
	__ATTR(max_cpus, 0444, uv_query_max_guest_cpus, NULL);

static ssize_t uv_query_max_guest_vms(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", uv_info.max_num_sec_conf);
}

static struct kobj_attribute uv_query_max_guest_vms_attr =
	__ATTR(max_guests, 0444, uv_query_max_guest_vms, NULL);

static ssize_t uv_query_max_guest_addr(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.max_sec_stor_addr);
}

static struct kobj_attribute uv_query_max_guest_addr_attr =
	__ATTR(max_address, 0444, uv_query_max_guest_addr, NULL);

static ssize_t uv_query_supp_att_req_hdr_ver(struct kobject *kobj,
					     struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_att_req_hdr_ver);
}

static struct kobj_attribute uv_query_supp_att_req_hdr_ver_attr =
	__ATTR(supp_att_req_hdr_ver, 0444, uv_query_supp_att_req_hdr_ver, NULL);

static ssize_t uv_query_supp_att_pflags(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_att_pflags);
}

static struct kobj_attribute uv_query_supp_att_pflags_attr =
	__ATTR(supp_att_pflags, 0444, uv_query_supp_att_pflags, NULL);

static ssize_t uv_query_supp_add_secret_req_ver(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_add_secret_req_ver);
}

static struct kobj_attribute uv_query_supp_add_secret_req_ver_attr =
	__ATTR(supp_add_secret_req_ver, 0444, uv_query_supp_add_secret_req_ver, NULL);

static ssize_t uv_query_supp_add_secret_pcf(struct kobject *kobj,
					    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_add_secret_pcf);
}

static struct kobj_attribute uv_query_supp_add_secret_pcf_attr =
	__ATTR(supp_add_secret_pcf, 0444, uv_query_supp_add_secret_pcf, NULL);

static ssize_t uv_query_supp_secret_types(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.supp_secret_types);
}

static struct kobj_attribute uv_query_supp_secret_types_attr =
	__ATTR(supp_secret_types, 0444, uv_query_supp_secret_types, NULL);

static ssize_t uv_query_max_secrets(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", uv_info.max_secrets);
}

static struct kobj_attribute uv_query_max_secrets_attr =
	__ATTR(max_secrets, 0444, uv_query_max_secrets, NULL);

static struct attribute *uv_query_attrs[] = {
	&uv_query_facilities_attr.attr,
	&uv_query_feature_indications_attr.attr,
	&uv_query_max_guest_cpus_attr.attr,
	&uv_query_max_guest_vms_attr.attr,
	&uv_query_max_guest_addr_attr.attr,
	&uv_query_supp_se_hdr_ver_attr.attr,
	&uv_query_supp_se_hdr_pcf_attr.attr,
	&uv_query_dump_storage_state_len_attr.attr,
	&uv_query_dump_finalize_len_attr.attr,
	&uv_query_dump_cpu_len_attr.attr,
	&uv_query_supp_att_req_hdr_ver_attr.attr,
	&uv_query_supp_att_pflags_attr.attr,
	&uv_query_supp_add_secret_req_ver_attr.attr,
	&uv_query_supp_add_secret_pcf_attr.attr,
	&uv_query_supp_secret_types_attr.attr,
	&uv_query_max_secrets_attr.attr,
	NULL,
};

static struct attribute_group uv_query_attr_group = {
	.attrs = uv_query_attrs,
};

static ssize_t uv_is_prot_virt_guest(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", prot_virt_guest);
}

static ssize_t uv_is_prot_virt_host(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", prot_virt_host);
}

static struct kobj_attribute uv_prot_virt_guest =
	__ATTR(prot_virt_guest, 0444, uv_is_prot_virt_guest, NULL);

static struct kobj_attribute uv_prot_virt_host =
	__ATTR(prot_virt_host, 0444, uv_is_prot_virt_host, NULL);

static const struct attribute *uv_prot_virt_attrs[] = {
	&uv_prot_virt_guest.attr,
	&uv_prot_virt_host.attr,
	NULL,
};

static struct kset *uv_query_kset;
static struct kobject *uv_kobj;

static int __init uv_info_init(void)
{
	int rc = -ENOMEM;

	if (!test_facility(158))
		return 0;

	uv_kobj = kobject_create_and_add("uv", firmware_kobj);
	if (!uv_kobj)
		return -ENOMEM;

	rc = sysfs_create_files(uv_kobj, uv_prot_virt_attrs);
	if (rc)
		goto out_kobj;

	uv_query_kset = kset_create_and_add("query", NULL, uv_kobj);
	if (!uv_query_kset) {
		rc = -ENOMEM;
		goto out_ind_files;
	}

	rc = sysfs_create_group(&uv_query_kset->kobj, &uv_query_attr_group);
	if (!rc)
		return 0;

	kset_unregister(uv_query_kset);
out_ind_files:
	sysfs_remove_files(uv_kobj, uv_prot_virt_attrs);
out_kobj:
	kobject_del(uv_kobj);
	kobject_put(uv_kobj);
	return rc;
}
device_initcall(uv_info_init);
