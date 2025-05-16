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
 * gmap_make_secure() - make one guest page secure
 * @gmap: the guest gmap
 * @gaddr: the guest address that needs to be made secure
 * @uvcb: the UVCB specifying which operation needs to be performed
 *
 * Context: needs to be called with kvm->srcu held.
 * Return: 0 on success, < 0 in case of error.
 */
int gmap_make_secure(struct gmap *gmap, unsigned long gaddr, void *uvcb)
{
	struct kvm *kvm = gmap->private;
	unsigned long vmaddr;

	lockdep_assert_held(&kvm->srcu);

	vmaddr = gfn_to_hva(kvm, gpa_to_gfn(gaddr));
	if (kvm_is_error_hva(vmaddr))
		return -EFAULT;
	return make_hva_secure(gmap->mm, vmaddr, uvcb);
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
