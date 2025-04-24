// SPDX-License-Identifier: GPL-2.0
/*
 * Common Ultravisor functions and initialization
 *
 * Copyright IBM Corp. 2019, 2024
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
EXPORT_SYMBOL(uv_destroy_folio);

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
int uv_convert_from_secure(unsigned long paddr)
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
EXPORT_SYMBOL_GPL(uv_convert_from_secure);

/*
 * The caller must already hold a reference to the folio.
 */
int uv_convert_from_secure_folio(struct folio *folio)
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
EXPORT_SYMBOL_GPL(uv_convert_from_secure_folio);

/*
 * The present PTE still indirectly holds a folio reference through the mapping.
 */
int uv_convert_from_secure_pte(pte_t pte)
{
	VM_WARN_ON(!pte_present(pte));
	return uv_convert_from_secure_folio(pfn_folio(pte_pfn(pte)));
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

/**
 * __make_folio_secure() - make a folio secure
 * @folio: the folio to make secure
 * @uvcb: the uvcb that describes the UVC to be used
 *
 * The folio @folio will be made secure if possible, @uvcb will be passed
 * as-is to the UVC.
 *
 * Return: 0 on success;
 *         -EBUSY if the folio is in writeback or has too many references;
 *         -EAGAIN if the UVC needs to be attempted again;
 *         -ENXIO if the address is not mapped;
 *         -EINVAL if the UVC failed for other reasons.
 *
 * Context: The caller must hold exactly one extra reference on the folio
 *          (it's the same logic as split_folio()), and the folio must be
 *          locked.
 */
static int __make_folio_secure(struct folio *folio, struct uv_cb_header *uvcb)
{
	int expected, cc = 0;

	if (folio_test_writeback(folio))
		return -EBUSY;
	expected = expected_folio_refs(folio) + 1;
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

static int make_folio_secure(struct mm_struct *mm, struct folio *folio, struct uv_cb_header *uvcb)
{
	int rc;

	if (!folio_trylock(folio))
		return -EAGAIN;
	if (should_export_before_import(uvcb, mm))
		uv_convert_from_secure(folio_to_phys(folio));
	rc = __make_folio_secure(folio, uvcb);
	folio_unlock(folio);

	return rc;
}

/**
 * s390_wiggle_split_folio() - try to drain extra references to a folio and optionally split.
 * @mm:    the mm containing the folio to work on
 * @folio: the folio
 * @split: whether to split a large folio
 *
 * Context: Must be called while holding an extra reference to the folio;
 *          the mm lock should not be held.
 * Return: 0 if the folio was split successfully;
 *         -EAGAIN if the folio was not split successfully but another attempt
 *                 can be made, or if @split was set to false;
 *         -EINVAL in case of other errors. See split_folio().
 */
static int s390_wiggle_split_folio(struct mm_struct *mm, struct folio *folio, bool split)
{
	int rc;

	lockdep_assert_not_held(&mm->mmap_lock);
	folio_wait_writeback(folio);
	lru_add_drain_all();
	if (split) {
		folio_lock(folio);
		rc = split_folio(folio);
		folio_unlock(folio);

		if (rc != -EBUSY)
			return rc;
	}
	return -EAGAIN;
}

int make_hva_secure(struct mm_struct *mm, unsigned long hva, struct uv_cb_header *uvcb)
{
	struct vm_area_struct *vma;
	struct folio_walk fw;
	struct folio *folio;
	int rc;

	mmap_read_lock(mm);
	vma = vma_lookup(mm, hva);
	if (!vma) {
		mmap_read_unlock(mm);
		return -EFAULT;
	}
	folio = folio_walk_start(&fw, vma, hva, 0);
	if (!folio) {
		mmap_read_unlock(mm);
		return -ENXIO;
	}

	folio_get(folio);
	/*
	 * Secure pages cannot be huge and userspace should not combine both.
	 * In case userspace does it anyway this will result in an -EFAULT for
	 * the unpack. The guest is thus never reaching secure mode.
	 * If userspace plays dirty tricks and decides to map huge pages at a
	 * later point in time, it will receive a segmentation fault or
	 * KVM_RUN will return -EFAULT.
	 */
	if (folio_test_hugetlb(folio))
		rc = -EFAULT;
	else if (folio_test_large(folio))
		rc = -E2BIG;
	else if (!pte_write(fw.pte) || (pte_val(fw.pte) & _PAGE_INVALID))
		rc = -ENXIO;
	else
		rc = make_folio_secure(mm, folio, uvcb);
	folio_walk_end(&fw, vma);
	mmap_read_unlock(mm);

	if (rc == -E2BIG || rc == -EBUSY)
		rc = s390_wiggle_split_folio(mm, folio, rc == -E2BIG);
	folio_put(folio);

	return rc;
}
EXPORT_SYMBOL_GPL(make_hva_secure);

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
	return sysfs_emit(buf, "%d\n",
			  uv_info.max_assoc_secrets + uv_info.max_retr_secrets);
}

static struct kobj_attribute uv_query_max_secrets_attr =
	__ATTR(max_secrets, 0444, uv_query_max_secrets, NULL);

static ssize_t uv_query_max_retr_secrets(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", uv_info.max_retr_secrets);
}

static struct kobj_attribute uv_query_max_retr_secrets_attr =
	__ATTR(max_retr_secrets, 0444, uv_query_max_retr_secrets, NULL);

static ssize_t uv_query_max_assoc_secrets(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%d\n", uv_info.max_assoc_secrets);
}

static struct kobj_attribute uv_query_max_assoc_secrets_attr =
	__ATTR(max_assoc_secrets, 0444, uv_query_max_assoc_secrets, NULL);

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
	&uv_query_max_assoc_secrets_attr.attr,
	&uv_query_max_retr_secrets_attr.attr,
	NULL,
};

static inline struct uv_cb_query_keys uv_query_keys(void)
{
	struct uv_cb_query_keys uvcb = {
		.header.cmd = UVC_CMD_QUERY_KEYS,
		.header.len = sizeof(uvcb)
	};

	uv_call(0, (uint64_t)&uvcb);
	return uvcb;
}

static inline ssize_t emit_hash(struct uv_key_hash *hash, char *buf, int at)
{
	return sysfs_emit_at(buf, at, "%016llx%016llx%016llx%016llx\n",
			    hash->dword[0], hash->dword[1], hash->dword[2], hash->dword[3]);
}

static ssize_t uv_keys_host_key(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct uv_cb_query_keys uvcb = uv_query_keys();

	return emit_hash(&uvcb.key_hashes[UVC_QUERY_KEYS_IDX_HK], buf, 0);
}

static struct kobj_attribute uv_keys_host_key_attr =
	__ATTR(host_key, 0444, uv_keys_host_key, NULL);

static ssize_t uv_keys_backup_host_key(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct uv_cb_query_keys uvcb = uv_query_keys();

	return emit_hash(&uvcb.key_hashes[UVC_QUERY_KEYS_IDX_BACK_HK], buf, 0);
}

static struct kobj_attribute uv_keys_backup_host_key_attr =
	__ATTR(backup_host_key, 0444, uv_keys_backup_host_key, NULL);

static ssize_t uv_keys_all(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct uv_cb_query_keys uvcb = uv_query_keys();
	ssize_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(uvcb.key_hashes); i++)
		len += emit_hash(uvcb.key_hashes + i, buf, len);

	return len;
}

static struct kobj_attribute uv_keys_all_attr =
	__ATTR(all, 0444, uv_keys_all, NULL);

static struct attribute_group uv_query_attr_group = {
	.attrs = uv_query_attrs,
};

static struct attribute *uv_keys_attrs[] = {
	&uv_keys_host_key_attr.attr,
	&uv_keys_backup_host_key_attr.attr,
	&uv_keys_all_attr.attr,
	NULL,
};

static struct attribute_group uv_keys_attr_group = {
	.attrs = uv_keys_attrs,
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
static struct kset *uv_keys_kset;
static struct kobject *uv_kobj;

static int __init uv_sysfs_dir_init(const struct attribute_group *grp,
				    struct kset **uv_dir_kset, const char *name)
{
	struct kset *kset;
	int rc;

	kset = kset_create_and_add(name, NULL, uv_kobj);
	if (!kset)
		return -ENOMEM;
	*uv_dir_kset = kset;

	rc = sysfs_create_group(&kset->kobj, grp);
	if (rc)
		kset_unregister(kset);
	return rc;
}

static int __init uv_sysfs_init(void)
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

	rc = uv_sysfs_dir_init(&uv_query_attr_group, &uv_query_kset, "query");
	if (rc)
		goto out_ind_files;

	/* Get installed key hashes if available, ignore any errors */
	if (test_bit_inv(BIT_UVC_CMD_QUERY_KEYS, uv_info.inst_calls_list))
		uv_sysfs_dir_init(&uv_keys_attr_group, &uv_keys_kset, "keys");

	return 0;

out_ind_files:
	sysfs_remove_files(uv_kobj, uv_prot_virt_attrs);
out_kobj:
	kobject_del(uv_kobj);
	kobject_put(uv_kobj);
	return rc;
}
device_initcall(uv_sysfs_init);

/*
 * Find the secret with the secret_id in the provided list.
 *
 * Context: might sleep.
 */
static int find_secret_in_page(const u8 secret_id[UV_SECRET_ID_LEN],
			       const struct uv_secret_list *list,
			       struct uv_secret_list_item_hdr *secret)
{
	u16 i;

	for (i = 0; i < list->total_num_secrets; i++) {
		if (memcmp(secret_id, list->secrets[i].id, UV_SECRET_ID_LEN) == 0) {
			*secret = list->secrets[i].hdr;
			return 0;
		}
	}
	return -ENOENT;
}

/*
 * Do the actual search for `uv_get_secret_metadata`.
 *
 * Context: might sleep.
 */
static int find_secret(const u8 secret_id[UV_SECRET_ID_LEN],
		       struct uv_secret_list *list,
		       struct uv_secret_list_item_hdr *secret)
{
	u16 start_idx = 0;
	u16 list_rc;
	int ret;

	do {
		uv_list_secrets(list, start_idx, &list_rc, NULL);
		if (list_rc != UVC_RC_EXECUTED && list_rc != UVC_RC_MORE_DATA) {
			if (list_rc == UVC_RC_INV_CMD)
				return -ENODEV;
			else
				return -EIO;
		}
		ret = find_secret_in_page(secret_id, list, secret);
		if (ret == 0)
			return ret;
		start_idx = list->next_secret_idx;
	} while (list_rc == UVC_RC_MORE_DATA && start_idx < list->next_secret_idx);

	return -ENOENT;
}

/**
 * uv_get_secret_metadata() - get secret metadata for a given secret id.
 * @secret_id: search pattern.
 * @secret: output data, containing the secret's metadata.
 *
 * Search for a secret with the given secret_id in the Ultravisor secret store.
 *
 * Context: might sleep.
 *
 * Return:
 * * %0:	- Found entry; secret->idx and secret->type are valid.
 * * %ENOENT	- No entry found.
 * * %ENODEV:	- Not supported: UV not available or command not available.
 * * %EIO:	- Other unexpected UV error.
 */
int uv_get_secret_metadata(const u8 secret_id[UV_SECRET_ID_LEN],
			   struct uv_secret_list_item_hdr *secret)
{
	struct uv_secret_list *buf;
	int rc;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	rc = find_secret(secret_id, buf, secret);
	kfree(buf);
	return rc;
}
EXPORT_SYMBOL_GPL(uv_get_secret_metadata);

/**
 * uv_retrieve_secret() - get the secret value for the secret index.
 * @secret_idx: Secret index for which the secret should be retrieved.
 * @buf: Buffer to store retrieved secret.
 * @buf_size: Size of the buffer. The correct buffer size is reported as part of
 * the result from `uv_get_secret_metadata`.
 *
 * Calls the Retrieve Secret UVC and translates the UV return code into an errno.
 *
 * Context: might sleep.
 *
 * Return:
 * * %0		- Entry found; buffer contains a valid secret.
 * * %ENOENT:	- No entry found or secret at the index is non-retrievable.
 * * %ENODEV:	- Not supported: UV not available or command not available.
 * * %EINVAL:	- Buffer too small for content.
 * * %EIO:	- Other unexpected UV error.
 */
int uv_retrieve_secret(u16 secret_idx, u8 *buf, size_t buf_size)
{
	struct uv_cb_retr_secr uvcb = {
		.header.len = sizeof(uvcb),
		.header.cmd = UVC_CMD_RETR_SECRET,
		.secret_idx = secret_idx,
		.buf_addr = (u64)buf,
		.buf_size = buf_size,
	};

	uv_call_sched(0, (u64)&uvcb);

	switch (uvcb.header.rc) {
	case UVC_RC_EXECUTED:
		return 0;
	case UVC_RC_INV_CMD:
		return -ENODEV;
	case UVC_RC_RETR_SECR_STORE_EMPTY:
	case UVC_RC_RETR_SECR_INV_SECRET:
	case UVC_RC_RETR_SECR_INV_IDX:
		return -ENOENT;
	case UVC_RC_RETR_SECR_BUF_SMALL:
		return -EINVAL;
	default:
		return -EIO;
	}
}
EXPORT_SYMBOL_GPL(uv_retrieve_secret);
