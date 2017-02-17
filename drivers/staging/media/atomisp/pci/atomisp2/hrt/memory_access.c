
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

#ifndef SH_CSS_MEMORY_GUARDING
/* Choose default in case not defined */
#ifdef HRT_CSIM
#define SH_CSS_MEMORY_GUARDING (1)
#else
#define SH_CSS_MEMORY_GUARDING (0)
#endif
#endif

#if SH_CSS_MEMORY_GUARDING
#define CEIL_DIV(a, b)	((b) ? ((a)+(b)-1)/(b) : 0)
#define CEIL_MUL(a, b)	(CEIL_DIV(a, b) * (b))
#define DDR_ALIGN(a)	(CEIL_MUL((a), (HIVE_ISP_DDR_WORD_BYTES)))

#define MEM_GUARD_START		0xABBAABBA
#define MEM_GUARD_END		0xBEEFBEEF
#define GUARD_SIZE		sizeof(unsigned long)
#define GUARD_SIZE_ALIGNED	DDR_ALIGN(GUARD_SIZE)

#define MAX_ALLOC_ENTRIES (256)
#define INVALID_VBASE ((ia_css_ptr)-1)
#define INVALID_SIZE ((unsigned long)-1)

struct alloc_info {
	ia_css_ptr  vbase;
	unsigned long size;
};

static struct alloc_info alloc_admin[MAX_ALLOC_ENTRIES];

static struct alloc_info const alloc_info_invalid
					= { INVALID_VBASE, INVALID_SIZE };

static void alloc_admin_init(void)
{
	int i;

	for (i = 0; i < MAX_ALLOC_ENTRIES; i++)
		alloc_admin[i] = alloc_info_invalid;
}

static struct alloc_info const *alloc_admin_find(ia_css_ptr vaddr)
{
	int i;
	/**
	 * Note that we use <= instead of < because we like to accept
	 * zero-sized operations at the last allocated address
	 * e.g. mmgr_set(vbase+alloc_size, data, 0)
	 */
	for (i = 0; i < MAX_ALLOC_ENTRIES; i++) {
		if (alloc_admin[i].vbase != INVALID_VBASE &&
					vaddr >= alloc_admin[i].vbase &&
					vaddr <= alloc_admin[i].vbase +
							alloc_admin[i].size) {
			return &alloc_admin[i];
		}
	}
	return &alloc_info_invalid;
}

static bool mem_guard_valid(ia_css_ptr vaddr, unsigned long size)
{
	unsigned long mem_guard;
	struct alloc_info const *info;

	info = alloc_admin_find(vaddr);
	if (info->vbase == INVALID_VBASE) {
		assert(false);
		return false;
	}

	/* Check if end is in alloc range*/
	if ((vaddr + size) > (info->vbase + info->size)) {
		assert(false);
		return false;
	}

	hrt_isp_css_mm_load((info->vbase - sizeof(mem_guard)),
			&mem_guard, sizeof(mem_guard));
	if (mem_guard != MEM_GUARD_START) {
		assert(false);
		return false;
	}

	hrt_isp_css_mm_load((info->vbase + info->size),
				&mem_guard, sizeof(mem_guard));
	if (mem_guard != MEM_GUARD_END) {
		assert(false);
		return false;
	}

	return true;

}

static void alloc_admin_add(ia_css_ptr vbase, unsigned long size)
{
	int i;
	unsigned long mem_guard;

	assert(alloc_admin_find(vbase)->vbase == INVALID_VBASE);

	mem_guard = MEM_GUARD_START;
	hrt_isp_css_mm_store((vbase - sizeof(mem_guard)),
				&mem_guard, sizeof(mem_guard));

	mem_guard = MEM_GUARD_END;
	hrt_isp_css_mm_store((vbase + size),
				&mem_guard, sizeof(mem_guard));

	for (i = 0; i < MAX_ALLOC_ENTRIES; i++) {
		if (alloc_admin[i].vbase == INVALID_VBASE) {
			alloc_admin[i].vbase = vbase;
			alloc_admin[i].size = size;
			return;
		}
	}
	assert(false);
}

static void alloc_admin_remove(ia_css_ptr vbase)
{
	int i;
	assert(mem_guard_valid(vbase, 0));
	for (i = 0; i < MAX_ALLOC_ENTRIES; i++) {
		if (alloc_admin[i].vbase == vbase) {
			alloc_admin[i] = alloc_info_invalid;
			return;
		}
	}
	assert(false);
}

#endif

void mmgr_set_base_address(
	const sys_address		base_addr)
{
	page_table_base_address = base_addr;

#if SH_CSS_MEMORY_GUARDING
	alloc_admin_init();
#endif
/*
 * This is part of "device_access.h", but it may be
 * that "hive_isp_css_mm_hrt.h" requires it
 */
/* hrt_isp_css_mm_set_ddr_address_offset(offset); */
/*	mmu_set_page_table_base_index(MMU0_ID, page_table_base_address); */
return;
}

sys_address mmgr_get_base_address(void)
{
return page_table_base_address;
}

void mmgr_set_base_index(
	const hrt_data		base_index)
{
/* This system only defines the MMU base address */
assert(0);
(void)base_index;
return;
}

hrt_data mmgr_get_base_index(void)
{
/* This system only defines the MMU base address */
assert(0);
return 0;
}

ia_css_ptr mmgr_malloc(
	const size_t			size)
{
return mmgr_alloc_attr(size, MMGR_ATTRIBUTE_CACHED);
}

ia_css_ptr mmgr_calloc(
	const size_t			N,
	const size_t			size)
{
return mmgr_alloc_attr(N * size, MMGR_ATTRIBUTE_CLEARED|MMGR_ATTRIBUTE_CACHED);
}

ia_css_ptr mmgr_realloc(
	ia_css_ptr			vaddr,
	const size_t			size)
{
return mmgr_realloc_attr(vaddr, size, MMGR_ATTRIBUTE_DEFAULT);
}

void mmgr_free(
	ia_css_ptr			vaddr)
{
/* "free()" should accept NULL, "hrt_isp_css_mm_free()" may not */
	if (vaddr) {
#if SH_CSS_MEMORY_GUARDING
		alloc_admin_remove(vaddr);
		/* Reconstruct the "original" address used with the alloc */
		vaddr -= GUARD_SIZE_ALIGNED;
#endif
		hrt_isp_css_mm_free(vaddr);
	}
return;
}

ia_css_ptr mmgr_alloc_attr(
	const size_t			size,
	const uint16_t			attribute)
{
	ia_css_ptr	ptr;
	size_t	extra_space = 0;
	size_t	aligned_size = size;

assert(page_table_base_address != (sys_address)-1);
assert((attribute & MMGR_ATTRIBUTE_UNUSED) == 0);

#if SH_CSS_MEMORY_GUARDING
	/* Add DDR aligned space for a guard at begin and end */
	/* Begin guard must be DDR aligned, "end" guard not */
	extra_space = GUARD_SIZE_ALIGNED + GUARD_SIZE;
	/* SP DMA operates on multiple of 32 bytes, also with writes.
	 * To prevent that the guard is being overwritten by SP DMA,
	 * the "end" guard must start DDR aligned.
	 */
	aligned_size = DDR_ALIGN(aligned_size);
#endif

	if (attribute & MMGR_ATTRIBUTE_CLEARED) {
		if (attribute & MMGR_ATTRIBUTE_CACHED) {
			if (attribute & MMGR_ATTRIBUTE_CONTIGUOUS) /* { */
				ptr = hrt_isp_css_mm_calloc_contiguous(
						aligned_size + extra_space);
			/* } */ else /* { */
				ptr = hrt_isp_css_mm_calloc_cached(
						aligned_size + extra_space);
			/* } */
		} else { /* !MMGR_ATTRIBUTE_CACHED */
			if (attribute & MMGR_ATTRIBUTE_CONTIGUOUS) /* { */
				ptr = hrt_isp_css_mm_calloc_contiguous(
						aligned_size + extra_space);
			/* } */ else /* { */
				ptr = hrt_isp_css_mm_calloc(
						aligned_size + extra_space);
			/* } */
		}
	} else { /* MMGR_ATTRIBUTE_CLEARED */
		if (attribute & MMGR_ATTRIBUTE_CACHED) {
			if (attribute & MMGR_ATTRIBUTE_CONTIGUOUS) /* { */
				ptr = hrt_isp_css_mm_alloc_contiguous(
						aligned_size + extra_space);
			/* } */ else /* { */
				ptr = hrt_isp_css_mm_alloc_cached(
						aligned_size + extra_space);
			/* } */
		} else { /* !MMGR_ATTRIBUTE_CACHED */
			if (attribute & MMGR_ATTRIBUTE_CONTIGUOUS) /* { */
				ptr = hrt_isp_css_mm_alloc_contiguous(
						aligned_size + extra_space);
			/* } */ else /* { */
				ptr = hrt_isp_css_mm_alloc(
						aligned_size + extra_space);
			/* } */
		}
	}

#if SH_CSS_MEMORY_GUARDING
	/* ptr is the user pointer, so we need to skip the "begin" guard */
	ptr += GUARD_SIZE_ALIGNED;
	alloc_admin_add(ptr, aligned_size);
#endif

	return ptr;
}

ia_css_ptr mmgr_realloc_attr(
	ia_css_ptr			vaddr,
	const size_t			size,
	const uint16_t			attribute)
{
assert(page_table_base_address != (sys_address)-1);
assert((attribute & MMGR_ATTRIBUTE_UNUSED) == 0);
/* assert(attribute == MMGR_ATTRIBUTE_DEFAULT); */
/* Apparently we don't have this one */
assert(0);
(void)vaddr;
(void)size;
(void)attribute;
return 0;
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
	mmgr_set(vaddr, (uint8_t)0, size);
}

void mmgr_set(
	ia_css_ptr			vaddr,
	const uint8_t			data,
	const size_t			size)
{
#if SH_CSS_MEMORY_GUARDING
	assert(mem_guard_valid(vaddr, size));
#endif
	hrt_isp_css_mm_set(vaddr, (int)data, size);
return;
}

void mmgr_load(
	const ia_css_ptr		vaddr,
	void				*data,
	const size_t			size)
{
#if SH_CSS_MEMORY_GUARDING
	assert(mem_guard_valid(vaddr, size));
#endif
	hrt_isp_css_mm_load(vaddr, data, size);
return;
}

void mmgr_store(
	const ia_css_ptr		vaddr,
	const void				*data,
	const size_t			size)
{
#if SH_CSS_MEMORY_GUARDING
	assert(mem_guard_valid(vaddr, size));
#endif
	hrt_isp_css_mm_store(vaddr, data, size);
return;
}
