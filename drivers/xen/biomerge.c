#include <linux/bio.h>
#include <linux/io.h>
#include <linux/export.h>
#include <xen/page.h>

bool xen_biovec_phys_mergeable(const struct bio_vec *vec1,
			       const struct bio_vec *vec2)
{
	unsigned long bfn1 = pfn_to_bfn(page_to_pfn(vec1->bv_page));
	unsigned long bfn2 = pfn_to_bfn(page_to_pfn(vec2->bv_page));

	return __BIOVEC_PHYS_MERGEABLE(vec1, vec2) &&
		((bfn1 == bfn2) || ((bfn1+1) == bfn2));
}
EXPORT_SYMBOL(xen_biovec_phys_mergeable);
