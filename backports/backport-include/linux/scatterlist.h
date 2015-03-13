#ifndef __BACKPORT_SCATTERLIST_H
#define __BACKPORT_SCATTERLIST_H
#include_next <linux/scatterlist.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
/* backports efc42bc9 */
#define sg_alloc_table_from_pages LINUX_BACKPORT(sg_alloc_table_from_pages)
int sg_alloc_table_from_pages(struct sg_table *sgt,
			      struct page **pages, unsigned int n_pages,
			      unsigned long offset, unsigned long size,
			      gfp_t gfp_mask);
#endif /* < 3.6 */

#endif /* __BACKPORT_SCATTERLIST_H */
