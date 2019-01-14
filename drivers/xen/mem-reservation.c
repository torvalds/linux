// SPDX-License-Identifier: GPL-2.0

/******************************************************************************
 * Xen memory reservation utilities.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
 * Copyright (c) 2010 Daniel Kiper
 * Copyright (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#include <asm/xen/hypercall.h>

#include <xen/interface/memory.h>
#include <xen/mem-reservation.h>
#include <linux/moduleparam.h>

bool __read_mostly xen_scrub_pages = IS_ENABLED(CONFIG_XEN_SCRUB_PAGES_DEFAULT);
core_param(xen_scrub_pages, xen_scrub_pages, bool, 0);

/*
 * Use one extent per PAGE_SIZE to avoid to break down the page into
 * multiple frame.
 */
#define EXTENT_ORDER (fls(XEN_PFN_PER_PAGE) - 1)

#ifdef CONFIG_XEN_HAVE_PVMMU
void __xenmem_reservation_va_mapping_update(unsigned long count,
					    struct page **pages,
					    xen_pfn_t *frames)
{
	int i;

	for (i = 0; i < count; i++) {
		struct page *page = pages[i];
		unsigned long pfn = page_to_pfn(page);

		BUG_ON(!page);

		/*
		 * We don't support PV MMU when Linux and Xen is using
		 * different page granularity.
		 */
		BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

		set_phys_to_machine(pfn, frames[i]);

		/* Link back into the page tables if not highmem. */
		if (!PageHighMem(page)) {
			int ret;

			ret = HYPERVISOR_update_va_mapping(
					(unsigned long)__va(pfn << PAGE_SHIFT),
					mfn_pte(frames[i], PAGE_KERNEL),
					0);
			BUG_ON(ret);
		}
	}
}
EXPORT_SYMBOL_GPL(__xenmem_reservation_va_mapping_update);

void __xenmem_reservation_va_mapping_reset(unsigned long count,
					   struct page **pages)
{
	int i;

	for (i = 0; i < count; i++) {
		struct page *page = pages[i];
		unsigned long pfn = page_to_pfn(page);

		/*
		 * We don't support PV MMU when Linux and Xen are using
		 * different page granularity.
		 */
		BUILD_BUG_ON(XEN_PAGE_SIZE != PAGE_SIZE);

		if (!PageHighMem(page)) {
			int ret;

			ret = HYPERVISOR_update_va_mapping(
					(unsigned long)__va(pfn << PAGE_SHIFT),
					__pte_ma(0), 0);
			BUG_ON(ret);
		}
		__set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
	}
}
EXPORT_SYMBOL_GPL(__xenmem_reservation_va_mapping_reset);
#endif /* CONFIG_XEN_HAVE_PVMMU */

/* @frames is an array of PFNs */
int xenmem_reservation_increase(int count, xen_pfn_t *frames)
{
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = EXTENT_ORDER,
		.domid        = DOMID_SELF
	};

	/* XENMEM_populate_physmap requires a PFN based on Xen granularity. */
	set_xen_guest_handle(reservation.extent_start, frames);
	reservation.nr_extents = count;
	return HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
}
EXPORT_SYMBOL_GPL(xenmem_reservation_increase);

/* @frames is an array of GFNs */
int xenmem_reservation_decrease(int count, xen_pfn_t *frames)
{
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = EXTENT_ORDER,
		.domid        = DOMID_SELF
	};

	/* XENMEM_decrease_reservation requires a GFN */
	set_xen_guest_handle(reservation.extent_start, frames);
	reservation.nr_extents = count;
	return HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
}
EXPORT_SYMBOL_GPL(xenmem_reservation_decrease);
