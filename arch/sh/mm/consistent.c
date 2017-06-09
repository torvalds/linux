/*
 * arch/sh/mm/consistent.c
 *
 * Copyright (C) 2004 - 2007  Paul Mundt
 *
 * Declared coherent memory functions based on arch/x86/kernel/pci-dma_32.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <asm/cacheflush.h>
#include <asm/addrspace.h>

#define PREALLOC_DMA_DEBUG_ENTRIES	4096

const struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

static int __init dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_init);

void *dma_generic_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t gfp,
				 unsigned long attrs)
{
	void *ret, *ret_nocache;
	int order = get_order(size);

	gfp |= __GFP_ZERO;

	ret = (void *)__get_free_pages(gfp, order);
	if (!ret)
		return NULL;

	/*
	 * Pages from the page allocator may have data present in
	 * cache. So flush the cache before using uncached memory.
	 */
	dma_cache_sync(dev, ret, size, DMA_BIDIRECTIONAL);

	ret_nocache = (void __force *)ioremap_nocache(virt_to_phys(ret), size);
	if (!ret_nocache) {
		free_pages((unsigned long)ret, order);
		return NULL;
	}

	split_page(pfn_to_page(virt_to_phys(ret) >> PAGE_SHIFT), order);

	*dma_handle = virt_to_phys(ret);

	return ret_nocache;
}

void dma_generic_free_coherent(struct device *dev, size_t size,
			       void *vaddr, dma_addr_t dma_handle,
			       unsigned long attrs)
{
	int order = get_order(size);
	unsigned long pfn = dma_handle >> PAGE_SHIFT;
	int k;

	for (k = 0; k < (1 << order); k++)
		__free_pages(pfn_to_page(pfn + k), 0);

	iounmap(vaddr);
}

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
	void *addr;

	addr = __in_29bit_mode() ?
	       (void *)CAC_ADDR((unsigned long)vaddr) : vaddr;

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		__flush_invalidate_region(addr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		__flush_wback_region(addr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		__flush_purge_region(addr, size);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(dma_cache_sync);

static int __init memchunk_setup(char *str)
{
	return 1; /* accept anything that begins with "memchunk." */
}
__setup("memchunk.", memchunk_setup);

static void __init memchunk_cmdline_override(char *name, unsigned long *sizep)
{
	char *p = boot_command_line;
	int k = strlen(name);

	while ((p = strstr(p, "memchunk."))) {
		p += 9; /* strlen("memchunk.") */
		if (!strncmp(name, p, k) && p[k] == '=') {
			p += k + 1;
			*sizep = memparse(p, NULL);
			pr_info("%s: forcing memory chunk size to 0x%08lx\n",
				name, *sizep);
			break;
		}
	}
}

int __init platform_resource_setup_memory(struct platform_device *pdev,
					  char *name, unsigned long memsize)
{
	struct resource *r;
	dma_addr_t dma_handle;
	void *buf;

	r = pdev->resource + pdev->num_resources - 1;
	if (r->flags) {
		pr_warning("%s: unable to find empty space for resource\n",
			name);
		return -EINVAL;
	}

	memchunk_cmdline_override(name, &memsize);
	if (!memsize)
		return 0;

	buf = dma_alloc_coherent(NULL, memsize, &dma_handle, GFP_KERNEL);
	if (!buf) {
		pr_warning("%s: unable to allocate memory\n", name);
		return -ENOMEM;
	}

	memset(buf, 0, memsize);

	r->flags = IORESOURCE_MEM;
	r->start = dma_handle;
	r->end = r->start + memsize - 1;
	r->name = name;
	return 0;
}
