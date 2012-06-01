/**************************************************************************
 *
 * Copyright (c) 2006-2007 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <linux/export.h>
#include "drmP.h"

#if defined(CONFIG_X86)
static void
drm_clflush_page(struct page *page)
{
	uint8_t *page_virtual;
	unsigned int i;
	const int size = boot_cpu_data.x86_clflush_size;

	if (unlikely(page == NULL))
		return;

	page_virtual = kmap_atomic(page);
	for (i = 0; i < PAGE_SIZE; i += size)
		clflush(page_virtual + i);
	kunmap_atomic(page_virtual);
}

static void drm_cache_flush_clflush(struct page *pages[],
				    unsigned long num_pages)
{
	unsigned long i;

	mb();
	for (i = 0; i < num_pages; i++)
		drm_clflush_page(*pages++);
	mb();
}

static void
drm_clflush_ipi_handler(void *null)
{
	wbinvd();
}
#endif

void
drm_clflush_pages(struct page *pages[], unsigned long num_pages)
{

#if defined(CONFIG_X86)
	if (cpu_has_clflush) {
		drm_cache_flush_clflush(pages, num_pages);
		return;
	}

	if (on_each_cpu(drm_clflush_ipi_handler, NULL, 1) != 0)
		printk(KERN_ERR "Timed out waiting for cache flush.\n");

#elif defined(__powerpc__)
	unsigned long i;
	for (i = 0; i < num_pages; i++) {
		struct page *page = pages[i];
		void *page_virtual;

		if (unlikely(page == NULL))
			continue;

		page_virtual = kmap_atomic(page);
		flush_dcache_range((unsigned long)page_virtual,
				   (unsigned long)page_virtual + PAGE_SIZE);
		kunmap_atomic(page_virtual);
	}
#else
	printk(KERN_ERR "Architecture has no drm_cache.c support\n");
	WARN_ON_ONCE(1);
#endif
}
EXPORT_SYMBOL(drm_clflush_pages);

void
drm_clflush_sg(struct sg_table *st)
{
#if defined(CONFIG_X86)
	if (cpu_has_clflush) {
		struct scatterlist *sg;
		int i;

		mb();
		for_each_sg(st->sgl, sg, st->nents, i)
			drm_clflush_page(sg_page(sg));
		mb();

		return;
	}

	if (on_each_cpu(drm_clflush_ipi_handler, NULL, 1) != 0)
		printk(KERN_ERR "Timed out waiting for cache flush.\n");
#else
	printk(KERN_ERR "Architecture has no drm_cache.c support\n");
	WARN_ON_ONCE(1);
#endif
}
EXPORT_SYMBOL(drm_clflush_sg);

void
drm_clflush_virt_range(char *addr, unsigned long length)
{
#if defined(CONFIG_X86)
	if (cpu_has_clflush) {
		char *end = addr + length;
		mb();
		for (; addr < end; addr += boot_cpu_data.x86_clflush_size)
			clflush(addr);
		clflush(end - 1);
		mb();
		return;
	}

	if (on_each_cpu(drm_clflush_ipi_handler, NULL, 1) != 0)
		printk(KERN_ERR "Timed out waiting for cache flush.\n");
#else
	printk(KERN_ERR "Architecture has no drm_cache.c support\n");
	WARN_ON_ONCE(1);
#endif
}
EXPORT_SYMBOL(drm_clflush_virt_range);
