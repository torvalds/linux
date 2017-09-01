#include <linux/bio.h>
#include <linux/io.h>
#include <linux/export.h>
#include <xen/page.h>

bool xen_biovec_phys_mergeable(const struct bio_vec *vec1,
			       const struct bio_vec *vec2)
{
#if XEN_PAGE_SIZE == PAGE_SIZE
	unsigned long bfn1 = pfn_to_bfn(page_to_pfn(vec1->bv_page));
	unsigned long bfn2 = pfn_to_bfn(page_to_pfn(vec2->bv_page));

	return bfn1 + PFN_DOWN(vec1->bv_offset + vec1->bv_len) == bfn2;
#else
	/*
	 * XXX: Add support for merging bio_vec when using different page
	 * size in Xen and Linux.
	 */
	return 0;
#endif
}
EXPORT_SYMBOL(xen_biovec_phys_mergeable);
