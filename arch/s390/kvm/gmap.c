// SPDX-License-Identifier: GPL-2.0
/*
 * Guest memory management for KVM/s390
 *
 * Copyright IBM Corp. 2008, 2020, 2024
 *
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *               Martin Schwidefsky <schwidefsky@de.ibm.com>
 *               David Hildenbrand <david@redhat.com>
 *               Janosch Frank <frankja@linux.vnet.ibm.com>
 */

#include <linux/compiler.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/pgtable.h>
#include <linux/pagemap.h>

#include <asm/lowcore.h>
#include <asm/gmap.h>
#include <asm/uv.h>

#include "gmap.h"

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

static int __gmap_make_secure(struct gmap *gmap, struct page *page, void *uvcb)
{
	struct folio *folio = page_folio(page);
	int rc;

	/*
	 * Secure pages cannot be huge and userspace should not combine both.
	 * In case userspace does it anyway this will result in an -EFAULT for
	 * the unpack. The guest is thus never reaching secure mode.
	 * If userspace plays dirty tricks and decides to map huge pages at a
	 * later point in time, it will receive a segmentation fault or
	 * KVM_RUN will return -EFAULT.
	 */
	if (folio_test_hugetlb(folio))
		return -EFAULT;
	if (folio_test_large(folio)) {
		mmap_read_unlock(gmap->mm);
		rc = kvm_s390_wiggle_split_folio(gmap->mm, folio, true);
		mmap_read_lock(gmap->mm);
		if (rc)
			return rc;
		folio = page_folio(page);
	}

	if (!folio_trylock(folio))
		return -EAGAIN;
	if (should_export_before_import(uvcb, gmap->mm))
		uv_convert_from_secure(folio_to_phys(folio));
	rc = make_folio_secure(folio, uvcb);
	folio_unlock(folio);

	/*
	 * In theory a race is possible and the folio might have become
	 * large again before the folio_trylock() above. In that case, no
	 * action is performed and -EAGAIN is returned; the callers will
	 * have to try again later.
	 * In most cases this implies running the VM again, getting the same
	 * exception again, and make another attempt in this function.
	 * This is expected to happen extremely rarely.
	 */
	if (rc == -E2BIG)
		return -EAGAIN;
	/* The folio has too many references, try to shake some off */
	if (rc == -EBUSY) {
		mmap_read_unlock(gmap->mm);
		kvm_s390_wiggle_split_folio(gmap->mm, folio, false);
		mmap_read_lock(gmap->mm);
		return -EAGAIN;
	}

	return rc;
}

/**
 * gmap_make_secure() - make one guest page secure
 * @gmap: the guest gmap
 * @gaddr: the guest address that needs to be made secure
 * @uvcb: the UVCB specifying which operation needs to be performed
 *
 * Context: needs to be called with kvm->srcu held.
 * Return: 0 on success, < 0 in case of error (see __gmap_make_secure()).
 */
int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb)
{
	struct kvm *kvm = gmap->private;
	struct page *page;
	int rc = 0;

	lockdep_assert_held(&kvm->srcu);

	page = gfn_to_page(kvm, gpa_to_gfn(gaddr));
	mmap_read_lock(gmap->mm);
	if (page)
		rc = __gmap_make_secure(gmap, page, uvcb);
	kvm_release_page_clean(page);
	mmap_read_unlock(gmap->mm);

	return rc;
}

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

/**
 * __gmap_destroy_page() - Destroy a guest page.
 * @gmap: the gmap of the guest
 * @page: the page to destroy
 *
 * An attempt will be made to destroy the given guest page. If the attempt
 * fails, an attempt is made to export the page. If both attempts fail, an
 * appropriate error is returned.
 *
 * Context: must be called holding the mm lock for gmap->mm
 */
static int __gmap_destroy_page(struct gmap *gmap, struct page *page)
{
	struct folio *folio = page_folio(page);
	int rc;

	/*
	 * See gmap_make_secure(): large folios cannot be secure. Small
	 * folio implies FW_LEVEL_PTE.
	 */
	if (folio_test_large(folio))
		return -EFAULT;

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

	return rc;
}

/**
 * gmap_destroy_page() - Destroy a guest page.
 * @gmap: the gmap of the guest
 * @gaddr: the guest address to destroy
 *
 * An attempt will be made to destroy the given guest page. If the attempt
 * fails, an attempt is made to export the page. If both attempts fail, an
 * appropriate error is returned.
 *
 * Context: may sleep.
 */
int gmap_destroy_page(struct gmap *gmap, unsigned long gaddr)
{
	struct page *page;
	int rc = 0;

	mmap_read_lock(gmap->mm);
	page = gfn_to_page(gmap->private, gpa_to_gfn(gaddr));
	if (page)
		rc = __gmap_destroy_page(gmap, page);
	kvm_release_page_clean(page);
	mmap_read_unlock(gmap->mm);
	return rc;
}
