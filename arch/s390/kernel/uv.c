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
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/uv.h>

/* the bootdata_preserved fields come from ones in arch/s390/boot/uv.c */
#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
int __bootdata_preserved(prot_virt_guest);
#endif

struct uv_info __bootdata_preserved(uv_info);

#if IS_ENABLED(CONFIG_KVM)
int __bootdata_preserved(prot_virt_host);
EXPORT_SYMBOL(prot_virt_host);
EXPORT_SYMBOL(uv_info);

static int __init uv_init(unsigned long stor_base, unsigned long stor_len)
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
	unsigned long uv_stor_base;

	if (!is_prot_virt_host())
		return;

	uv_stor_base = (unsigned long)memblock_alloc_try_nid(
		uv_info.uv_base_stor_len, SZ_1M, SZ_2G,
		MEMBLOCK_ALLOC_ACCESSIBLE, NUMA_NO_NODE);
	if (!uv_stor_base) {
		pr_warn("Failed to reserve %lu bytes for ultravisor base storage\n",
			uv_info.uv_base_stor_len);
		goto fail;
	}

	if (uv_init(uv_stor_base, uv_info.uv_base_stor_len)) {
		memblock_phys_free(uv_stor_base, uv_info.uv_base_stor_len);
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
static int uv_pin_shared(unsigned long paddr)
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

/*
 * Requests the Ultravisor to destroy a guest page and make it
 * accessible to the host. The destroy clears the page instead of
 * exporting.
 *
 * @paddr: Absolute host address of page to be destroyed
 */
static int uv_destroy_page(unsigned long paddr)
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
 * The caller must already hold a reference to the page
 */
int uv_destroy_owned_page(unsigned long paddr)
{
	struct page *page = phys_to_page(paddr);
	int rc;

	get_page(page);
	rc = uv_destroy_page(paddr);
	if (!rc)
		clear_bit(PG_arch_1, &page->flags);
	put_page(page);
	return rc;
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

/*
 * The caller must already hold a reference to the page
 */
int uv_convert_owned_from_secure(unsigned long paddr)
{
	struct page *page = phys_to_page(paddr);
	int rc;

	get_page(page);
	rc = uv_convert_from_secure(paddr);
	if (!rc)
		clear_bit(PG_arch_1, &page->flags);
	put_page(page);
	return rc;
}

/*
 * Calculate the expected ref_count for a page that would otherwise have no
 * further pins. This was cribbed from similar functions in other places in
 * the kernel, but with some slight modifications. We know that a secure
 * page can not be a huge page for example.
 */
static int expected_page_refs(struct page *page)
{
	int res;

	res = page_mapcount(page);
	if (PageSwapCache(page)) {
		res++;
	} else if (page_mapping(page)) {
		res++;
		if (page_has_private(page))
			res++;
	}
	return res;
}

static int make_secure_pte(pte_t *ptep, unsigned long addr,
			   struct page *exp_page, struct uv_cb_header *uvcb)
{
	pte_t entry = READ_ONCE(*ptep);
	struct page *page;
	int expected, cc = 0;

	if (!pte_present(entry))
		return -ENXIO;
	if (pte_val(entry) & _PAGE_INVALID)
		return -ENXIO;

	page = pte_page(entry);
	if (page != exp_page)
		return -ENXIO;
	if (PageWriteback(page))
		return -EAGAIN;
	expected = expected_page_refs(page);
	if (!page_ref_freeze(page, expected))
		return -EBUSY;
	set_bit(PG_arch_1, &page->flags);
	/*
	 * If the UVC does not succeed or fail immediately, we don't want to
	 * loop for long, or we might get stall notifications.
	 * On the other hand, this is a complex scenario and we are holding a lot of
	 * locks, so we can't easily sleep and reschedule. We try only once,
	 * and if the UVC returned busy or partial completion, we return
	 * -EAGAIN and we let the callers deal with it.
	 */
	cc = __uv_call(0, (u64)uvcb);
	page_ref_unfreeze(page, expected);
	/*
	 * Return -ENXIO if the page was not mapped, -EINVAL for other errors.
	 * If busy or partially completed, return -EAGAIN.
	 */
	if (cc == UVC_CC_OK)
		return 0;
	else if (cc == UVC_CC_BUSY || cc == UVC_CC_PARTIAL)
		return -EAGAIN;
	return uvcb->rc == 0x10a ? -ENXIO : -EINVAL;
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
	bool local_drain = false;
	spinlock_t *ptelock;
	unsigned long uaddr;
	struct page *page;
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
	page = follow_page(vma, uaddr, FOLL_WRITE);
	if (IS_ERR_OR_NULL(page))
		goto out;

	lock_page(page);
	ptep = get_locked_pte(gmap->mm, uaddr, &ptelock);
	rc = make_secure_pte(ptep, uaddr, page, uvcb);
	pte_unmap_unlock(ptep, ptelock);
	unlock_page(page);
out:
	mmap_read_unlock(gmap->mm);

	if (rc == -EAGAIN) {
		/*
		 * If we are here because the UVC returned busy or partial
		 * completion, this is just a useless check, but it is safe.
		 */
		wait_on_page_writeback(page);
	} else if (rc == -EBUSY) {
		/*
		 * If we have tried a local drain and the page refcount
		 * still does not match our expected safe value, try with a
		 * system wide drain. This is needed if the pagevecs holding
		 * the page are on a different CPU.
		 */
		if (local_drain) {
			lru_add_drain_all();
			/* We give up here, and let the caller try again */
			return -EAGAIN;
		}
		/*
		 * We are here if the page refcount does not match the
		 * expected safe value. The main culprits are usually
		 * pagevecs. With lru_add_drain() we drain the pagevecs
		 * on the local CPU so that hopefully the refcount will
		 * reach the expected safe value.
		 */
		lru_add_drain();
		local_drain = true;
		/* And now we try again immediately after draining */
		goto again;
	} else if (rc == -ENXIO) {
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

/*
 * To be called with the page locked or with an extra reference! This will
 * prevent gmap_make_secure from touching the page concurrently. Having 2
 * parallel make_page_accessible is fine, as the UV calls will become a
 * no-op if the page is already exported.
 */
int arch_make_page_accessible(struct page *page)
{
	int rc = 0;

	/* Hugepage cannot be protected, so nothing to do */
	if (PageHuge(page))
		return 0;

	/*
	 * PG_arch_1 is used in 3 places:
	 * 1. for kernel page tables during early boot
	 * 2. for storage keys of huge pages and KVM
	 * 3. As an indication that this page might be secure. This can
	 *    overindicate, e.g. we set the bit before calling
	 *    convert_to_secure.
	 * As secure pages are never huge, all 3 variants can co-exists.
	 */
	if (!test_bit(PG_arch_1, &page->flags))
		return 0;

	rc = uv_pin_shared(page_to_phys(page));
	if (!rc) {
		clear_bit(PG_arch_1, &page->flags);
		return 0;
	}

	rc = uv_convert_from_secure(page_to_phys(page));
	if (!rc) {
		clear_bit(PG_arch_1, &page->flags);
		return 0;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(arch_make_page_accessible);

#endif

#if defined(CONFIG_PROTECTED_VIRTUALIZATION_GUEST) || IS_ENABLED(CONFIG_KVM)
static ssize_t uv_query_facilities(struct kobject *kobj,
				   struct kobj_attribute *attr, char *page)
{
	return scnprintf(page, PAGE_SIZE, "%lx\n%lx\n%lx\n%lx\n",
			uv_info.inst_calls_list[0],
			uv_info.inst_calls_list[1],
			uv_info.inst_calls_list[2],
			uv_info.inst_calls_list[3]);
}

static struct kobj_attribute uv_query_facilities_attr =
	__ATTR(facilities, 0444, uv_query_facilities, NULL);

static ssize_t uv_query_feature_indications(struct kobject *kobj,
					    struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lx\n", uv_info.uv_feature_indications);
}

static struct kobj_attribute uv_query_feature_indications_attr =
	__ATTR(feature_indications, 0444, uv_query_feature_indications, NULL);

static ssize_t uv_query_max_guest_cpus(struct kobject *kobj,
				       struct kobj_attribute *attr, char *page)
{
	return scnprintf(page, PAGE_SIZE, "%d\n",
			uv_info.max_guest_cpu_id + 1);
}

static struct kobj_attribute uv_query_max_guest_cpus_attr =
	__ATTR(max_cpus, 0444, uv_query_max_guest_cpus, NULL);

static ssize_t uv_query_max_guest_vms(struct kobject *kobj,
				      struct kobj_attribute *attr, char *page)
{
	return scnprintf(page, PAGE_SIZE, "%d\n",
			uv_info.max_num_sec_conf);
}

static struct kobj_attribute uv_query_max_guest_vms_attr =
	__ATTR(max_guests, 0444, uv_query_max_guest_vms, NULL);

static ssize_t uv_query_max_guest_addr(struct kobject *kobj,
				       struct kobj_attribute *attr, char *page)
{
	return scnprintf(page, PAGE_SIZE, "%lx\n",
			uv_info.max_sec_stor_addr);
}

static struct kobj_attribute uv_query_max_guest_addr_attr =
	__ATTR(max_address, 0444, uv_query_max_guest_addr, NULL);

static struct attribute *uv_query_attrs[] = {
	&uv_query_facilities_attr.attr,
	&uv_query_feature_indications_attr.attr,
	&uv_query_max_guest_cpus_attr.attr,
	&uv_query_max_guest_vms_attr.attr,
	&uv_query_max_guest_addr_attr.attr,
	NULL,
};

static struct attribute_group uv_query_attr_group = {
	.attrs = uv_query_attrs,
};

static ssize_t uv_is_prot_virt_guest(struct kobject *kobj,
				     struct kobj_attribute *attr, char *page)
{
	int val = 0;

#ifdef CONFIG_PROTECTED_VIRTUALIZATION_GUEST
	val = prot_virt_guest;
#endif
	return scnprintf(page, PAGE_SIZE, "%d\n", val);
}

static ssize_t uv_is_prot_virt_host(struct kobject *kobj,
				    struct kobj_attribute *attr, char *page)
{
	int val = 0;

#if IS_ENABLED(CONFIG_KVM)
	val = prot_virt_host;
#endif

	return scnprintf(page, PAGE_SIZE, "%d\n", val);
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
#endif
