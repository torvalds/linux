
#include "memory_access.h"

#include <stddef.h>		/* NULL */
#include <stdbool.h>

#include "device_access.h"

#include "mmu_device.h"

#include "assert_support.h"

/* Presently system specific */
#include <hmm/hmm.h>
/* Presently system specific */
#include "hive_isp_css_mm_hrt.h"

/*
 * This is an HRT backend implementation for CSIM
 * 31 July 2012, rvanimme: this implementation is also used in Android context
 */

static sys_address	page_table_base_address = (sys_address)-1;

void mmgr_set_base_address(const sys_address base_addr)
{
	page_table_base_address = base_addr;

/*
 * This is part of "device_access.h", but it may be
 * that "hive_isp_css_mm_hrt.h" requires it
 */
/* hrt_isp_css_mm_set_ddr_address_offset(offset); */
/*	mmu_set_page_table_base_index(MMU0_ID, page_table_base_address); */
}

ia_css_ptr mmgr_malloc(const size_t size)
{
	return mmgr_alloc_attr(size, MMGR_ATTRIBUTE_CACHED);
}

ia_css_ptr mmgr_calloc(const size_t N, const size_t size)
{
	return mmgr_alloc_attr(N * size,
		MMGR_ATTRIBUTE_CLEARED|MMGR_ATTRIBUTE_CACHED);
}

void mmgr_free(ia_css_ptr vaddr)
{
/* "free()" should accept NULL, "hrt_isp_css_mm_free()" may not */
	if (vaddr)
		hrt_isp_css_mm_free(vaddr);
}

ia_css_ptr mmgr_alloc_attr(const size_t	size, const uint16_t attribute)
{
	ia_css_ptr	ptr;
	size_t	extra_space = 0;
	size_t	aligned_size = size;

	assert(page_table_base_address != (sys_address)-1);
	assert((attribute & MMGR_ATTRIBUTE_UNUSED) == 0);
	WARN_ON(attribute & MMGR_ATTRIBUTE_CONTIGUOUS);

	if (attribute & MMGR_ATTRIBUTE_CLEARED) {
		if (attribute & MMGR_ATTRIBUTE_CACHED) {
			ptr = hrt_isp_css_mm_calloc_cached(
						aligned_size + extra_space);
		} else { /* !MMGR_ATTRIBUTE_CACHED */
			ptr = hrt_isp_css_mm_calloc(
						aligned_size + extra_space);
		}
	} else { /* MMGR_ATTRIBUTE_CLEARED */
		if (attribute & MMGR_ATTRIBUTE_CACHED) {
			ptr = hrt_isp_css_mm_alloc_cached(
						aligned_size + extra_space);
		} else { /* !MMGR_ATTRIBUTE_CACHED */
			ptr = hrt_isp_css_mm_alloc(
					aligned_size + extra_space);
		}
	}
	return ptr;
}

ia_css_ptr mmgr_mmap(const void *ptr, const size_t size, uint16_t attribute,
		void *context)
{
	struct hrt_userbuffer_attr *userbuffer_attr = context;
	return hrt_isp_css_mm_alloc_user_ptr(size, (void *)ptr,
					userbuffer_attr->pgnr,
					userbuffer_attr->type,
					attribute & HRT_BUF_FLAG_CACHED);
}

void mmgr_clear(
	ia_css_ptr			vaddr,
	const size_t			size)
{
	hrt_isp_css_mm_set(vaddr, 0, size);
}

void mmgr_load(const ia_css_ptr	vaddr, void *data, const size_t size)
{
	hrt_isp_css_mm_load(vaddr, data, size);
}

void mmgr_store(const ia_css_ptr vaddr,	const void *data, const size_t size)
{
	hrt_isp_css_mm_store(vaddr, data, size);
}
