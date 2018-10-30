/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVDIMM_PMEM_H__
#define __NVDIMM_PMEM_H__
#include <linux/page-flags.h>
#include <linux/badblocks.h>
#include <linux/types.h>
#include <linux/pfn_t.h>
#include <linux/fs.h>

/* this definition is in it's own header for tools/testing/nvdimm to consume */
struct pmem_device {
	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	/* when non-zero this device is hosting a 'pfn' instance */
	phys_addr_t		data_offset;
	u64			pfn_flags;
	void			*virt_addr;
	/* immutable base size of the namespace */
	size_t			size;
	/* trim size when namespace capacity has been section aligned */
	u32			pfn_pad;
	struct kernfs_node	*bb_state;
	struct badblocks	bb;
	struct dax_device	*dax_dev;
	struct gendisk		*disk;
	struct dev_pagemap	pgmap;
};

long __pmem_direct_access(struct pmem_device *pmem, pgoff_t pgoff,
		long nr_pages, void **kaddr, pfn_t *pfn);

#ifdef CONFIG_MEMORY_FAILURE
static inline bool test_and_clear_pmem_poison(struct page *page)
{
	return TestClearPageHWPoison(page);
}
#else
static inline bool test_and_clear_pmem_poison(struct page *page)
{
	return false;
}
#endif
#endif /* __NVDIMM_PMEM_H__ */
