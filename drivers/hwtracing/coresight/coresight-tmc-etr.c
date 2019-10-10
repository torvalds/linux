// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/atomic.h>
#include <linux/coresight.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include "coresight-catu.h"
#include "coresight-etm-perf.h"
#include "coresight-priv.h"
#include "coresight-tmc.h"

struct etr_flat_buf {
	struct device	*dev;
	dma_addr_t	daddr;
	void		*vaddr;
	size_t		size;
};

/*
 * etr_perf_buffer - Perf buffer used for ETR
 * @drvdata		- The ETR drvdaga this buffer has been allocated for.
 * @etr_buf		- Actual buffer used by the ETR
 * @pid			- The PID this etr_perf_buffer belongs to.
 * @snaphost		- Perf session mode
 * @head		- handle->head at the beginning of the session.
 * @nr_pages		- Number of pages in the ring buffer.
 * @pages		- Array of Pages in the ring buffer.
 */
struct etr_perf_buffer {
	struct tmc_drvdata	*drvdata;
	struct etr_buf		*etr_buf;
	pid_t			pid;
	bool			snapshot;
	unsigned long		head;
	int			nr_pages;
	void			**pages;
};

/* Convert the perf index to an offset within the ETR buffer */
#define PERF_IDX2OFF(idx, buf)	((idx) % ((buf)->nr_pages << PAGE_SHIFT))

/* Lower limit for ETR hardware buffer */
#define TMC_ETR_PERF_MIN_BUF_SIZE	SZ_1M

/*
 * The TMC ETR SG has a page size of 4K. The SG table contains pointers
 * to 4KB buffers. However, the OS may use a PAGE_SIZE different from
 * 4K (i.e, 16KB or 64KB). This implies that a single OS page could
 * contain more than one SG buffer and tables.
 *
 * A table entry has the following format:
 *
 * ---Bit31------------Bit4-------Bit1-----Bit0--
 * |     Address[39:12]    | SBZ |  Entry Type  |
 * ----------------------------------------------
 *
 * Address: Bits [39:12] of a physical page address. Bits [11:0] are
 *	    always zero.
 *
 * Entry type:
 *	b00 - Reserved.
 *	b01 - Last entry in the tables, points to 4K page buffer.
 *	b10 - Normal entry, points to 4K page buffer.
 *	b11 - Link. The address points to the base of next table.
 */

typedef u32 sgte_t;

#define ETR_SG_PAGE_SHIFT		12
#define ETR_SG_PAGE_SIZE		(1UL << ETR_SG_PAGE_SHIFT)
#define ETR_SG_PAGES_PER_SYSPAGE	(PAGE_SIZE / ETR_SG_PAGE_SIZE)
#define ETR_SG_PTRS_PER_PAGE		(ETR_SG_PAGE_SIZE / sizeof(sgte_t))
#define ETR_SG_PTRS_PER_SYSPAGE		(PAGE_SIZE / sizeof(sgte_t))

#define ETR_SG_ET_MASK			0x3
#define ETR_SG_ET_LAST			0x1
#define ETR_SG_ET_NORMAL		0x2
#define ETR_SG_ET_LINK			0x3

#define ETR_SG_ADDR_SHIFT		4

#define ETR_SG_ENTRY(addr, type) \
	(sgte_t)((((addr) >> ETR_SG_PAGE_SHIFT) << ETR_SG_ADDR_SHIFT) | \
		 (type & ETR_SG_ET_MASK))

#define ETR_SG_ADDR(entry) \
	(((dma_addr_t)(entry) >> ETR_SG_ADDR_SHIFT) << ETR_SG_PAGE_SHIFT)
#define ETR_SG_ET(entry)		((entry) & ETR_SG_ET_MASK)

/*
 * struct etr_sg_table : ETR SG Table
 * @sg_table:		Generic SG Table holding the data/table pages.
 * @hwaddr:		hwaddress used by the TMC, which is the base
 *			address of the table.
 */
struct etr_sg_table {
	struct tmc_sg_table	*sg_table;
	dma_addr_t		hwaddr;
};

/*
 * tmc_etr_sg_table_entries: Total number of table entries required to map
 * @nr_pages system pages.
 *
 * We need to map @nr_pages * ETR_SG_PAGES_PER_SYSPAGE data pages.
 * Each TMC page can map (ETR_SG_PTRS_PER_PAGE - 1) buffer pointers,
 * with the last entry pointing to another page of table entries.
 * If we spill over to a new page for mapping 1 entry, we could as
 * well replace the link entry of the previous page with the last entry.
 */
static inline unsigned long __attribute_const__
tmc_etr_sg_table_entries(int nr_pages)
{
	unsigned long nr_sgpages = nr_pages * ETR_SG_PAGES_PER_SYSPAGE;
	unsigned long nr_sglinks = nr_sgpages / (ETR_SG_PTRS_PER_PAGE - 1);
	/*
	 * If we spill over to a new page for 1 entry, we could as well
	 * make it the LAST entry in the previous page, skipping the Link
	 * address.
	 */
	if (nr_sglinks && (nr_sgpages % (ETR_SG_PTRS_PER_PAGE - 1) < 2))
		nr_sglinks--;
	return nr_sgpages + nr_sglinks;
}

/*
 * tmc_pages_get_offset:  Go through all the pages in the tmc_pages
 * and map the device address @addr to an offset within the virtual
 * contiguous buffer.
 */
static long
tmc_pages_get_offset(struct tmc_pages *tmc_pages, dma_addr_t addr)
{
	int i;
	dma_addr_t page_start;

	for (i = 0; i < tmc_pages->nr_pages; i++) {
		page_start = tmc_pages->daddrs[i];
		if (addr >= page_start && addr < (page_start + PAGE_SIZE))
			return i * PAGE_SIZE + (addr - page_start);
	}

	return -EINVAL;
}

/*
 * tmc_pages_free : Unmap and free the pages used by tmc_pages.
 * If the pages were not allocated in tmc_pages_alloc(), we would
 * simply drop the refcount.
 */
static void tmc_pages_free(struct tmc_pages *tmc_pages,
			   struct device *dev, enum dma_data_direction dir)
{
	int i;
	struct device *real_dev = dev->parent;

	for (i = 0; i < tmc_pages->nr_pages; i++) {
		if (tmc_pages->daddrs && tmc_pages->daddrs[i])
			dma_unmap_page(real_dev, tmc_pages->daddrs[i],
					 PAGE_SIZE, dir);
		if (tmc_pages->pages && tmc_pages->pages[i])
			__free_page(tmc_pages->pages[i]);
	}

	kfree(tmc_pages->pages);
	kfree(tmc_pages->daddrs);
	tmc_pages->pages = NULL;
	tmc_pages->daddrs = NULL;
	tmc_pages->nr_pages = 0;
}

/*
 * tmc_pages_alloc : Allocate and map pages for a given @tmc_pages.
 * If @pages is not NULL, the list of page virtual addresses are
 * used as the data pages. The pages are then dma_map'ed for @dev
 * with dma_direction @dir.
 *
 * Returns 0 upon success, else the error number.
 */
static int tmc_pages_alloc(struct tmc_pages *tmc_pages,
			   struct device *dev, int node,
			   enum dma_data_direction dir, void **pages)
{
	int i, nr_pages;
	dma_addr_t paddr;
	struct page *page;
	struct device *real_dev = dev->parent;

	nr_pages = tmc_pages->nr_pages;
	tmc_pages->daddrs = kcalloc(nr_pages, sizeof(*tmc_pages->daddrs),
					 GFP_KERNEL);
	if (!tmc_pages->daddrs)
		return -ENOMEM;
	tmc_pages->pages = kcalloc(nr_pages, sizeof(*tmc_pages->pages),
					 GFP_KERNEL);
	if (!tmc_pages->pages) {
		kfree(tmc_pages->daddrs);
		tmc_pages->daddrs = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < nr_pages; i++) {
		if (pages && pages[i]) {
			page = virt_to_page(pages[i]);
			/* Hold a refcount on the page */
			get_page(page);
		} else {
			page = alloc_pages_node(node,
						GFP_KERNEL | __GFP_ZERO, 0);
		}
		paddr = dma_map_page(real_dev, page, 0, PAGE_SIZE, dir);
		if (dma_mapping_error(real_dev, paddr))
			goto err;
		tmc_pages->daddrs[i] = paddr;
		tmc_pages->pages[i] = page;
	}
	return 0;
err:
	tmc_pages_free(tmc_pages, dev, dir);
	return -ENOMEM;
}

static inline long
tmc_sg_get_data_page_offset(struct tmc_sg_table *sg_table, dma_addr_t addr)
{
	return tmc_pages_get_offset(&sg_table->data_pages, addr);
}

static inline void tmc_free_table_pages(struct tmc_sg_table *sg_table)
{
	if (sg_table->table_vaddr)
		vunmap(sg_table->table_vaddr);
	tmc_pages_free(&sg_table->table_pages, sg_table->dev, DMA_TO_DEVICE);
}

static void tmc_free_data_pages(struct tmc_sg_table *sg_table)
{
	if (sg_table->data_vaddr)
		vunmap(sg_table->data_vaddr);
	tmc_pages_free(&sg_table->data_pages, sg_table->dev, DMA_FROM_DEVICE);
}

void tmc_free_sg_table(struct tmc_sg_table *sg_table)
{
	tmc_free_table_pages(sg_table);
	tmc_free_data_pages(sg_table);
}

/*
 * Alloc pages for the table. Since this will be used by the device,
 * allocate the pages closer to the device (i.e, dev_to_node(dev)
 * rather than the CPU node).
 */
static int tmc_alloc_table_pages(struct tmc_sg_table *sg_table)
{
	int rc;
	struct tmc_pages *table_pages = &sg_table->table_pages;

	rc = tmc_pages_alloc(table_pages, sg_table->dev,
			     dev_to_node(sg_table->dev),
			     DMA_TO_DEVICE, NULL);
	if (rc)
		return rc;
	sg_table->table_vaddr = vmap(table_pages->pages,
				     table_pages->nr_pages,
				     VM_MAP,
				     PAGE_KERNEL);
	if (!sg_table->table_vaddr)
		rc = -ENOMEM;
	else
		sg_table->table_daddr = table_pages->daddrs[0];
	return rc;
}

static int tmc_alloc_data_pages(struct tmc_sg_table *sg_table, void **pages)
{
	int rc;

	/* Allocate data pages on the node requested by the caller */
	rc = tmc_pages_alloc(&sg_table->data_pages,
			     sg_table->dev, sg_table->node,
			     DMA_FROM_DEVICE, pages);
	if (!rc) {
		sg_table->data_vaddr = vmap(sg_table->data_pages.pages,
					    sg_table->data_pages.nr_pages,
					    VM_MAP,
					    PAGE_KERNEL);
		if (!sg_table->data_vaddr)
			rc = -ENOMEM;
	}
	return rc;
}

/*
 * tmc_alloc_sg_table: Allocate and setup dma pages for the TMC SG table
 * and data buffers. TMC writes to the data buffers and reads from the SG
 * Table pages.
 *
 * @dev		- Coresight device to which page should be DMA mapped.
 * @node	- Numa node for mem allocations
 * @nr_tpages	- Number of pages for the table entries.
 * @nr_dpages	- Number of pages for Data buffer.
 * @pages	- Optional list of virtual address of pages.
 */
struct tmc_sg_table *tmc_alloc_sg_table(struct device *dev,
					int node,
					int nr_tpages,
					int nr_dpages,
					void **pages)
{
	long rc;
	struct tmc_sg_table *sg_table;

	sg_table = kzalloc(sizeof(*sg_table), GFP_KERNEL);
	if (!sg_table)
		return ERR_PTR(-ENOMEM);
	sg_table->data_pages.nr_pages = nr_dpages;
	sg_table->table_pages.nr_pages = nr_tpages;
	sg_table->node = node;
	sg_table->dev = dev;

	rc  = tmc_alloc_data_pages(sg_table, pages);
	if (!rc)
		rc = tmc_alloc_table_pages(sg_table);
	if (rc) {
		tmc_free_sg_table(sg_table);
		kfree(sg_table);
		return ERR_PTR(rc);
	}

	return sg_table;
}

/*
 * tmc_sg_table_sync_data_range: Sync the data buffer written
 * by the device from @offset upto a @size bytes.
 */
void tmc_sg_table_sync_data_range(struct tmc_sg_table *table,
				  u64 offset, u64 size)
{
	int i, index, start;
	int npages = DIV_ROUND_UP(size, PAGE_SIZE);
	struct device *real_dev = table->dev->parent;
	struct tmc_pages *data = &table->data_pages;

	start = offset >> PAGE_SHIFT;
	for (i = start; i < (start + npages); i++) {
		index = i % data->nr_pages;
		dma_sync_single_for_cpu(real_dev, data->daddrs[index],
					PAGE_SIZE, DMA_FROM_DEVICE);
	}
}

/* tmc_sg_sync_table: Sync the page table */
void tmc_sg_table_sync_table(struct tmc_sg_table *sg_table)
{
	int i;
	struct device *real_dev = sg_table->dev->parent;
	struct tmc_pages *table_pages = &sg_table->table_pages;

	for (i = 0; i < table_pages->nr_pages; i++)
		dma_sync_single_for_device(real_dev, table_pages->daddrs[i],
					   PAGE_SIZE, DMA_TO_DEVICE);
}

/*
 * tmc_sg_table_get_data: Get the buffer pointer for data @offset
 * in the SG buffer. The @bufpp is updated to point to the buffer.
 * Returns :
 *	the length of linear data available at @offset.
 *	or
 *	<= 0 if no data is available.
 */
ssize_t tmc_sg_table_get_data(struct tmc_sg_table *sg_table,
			      u64 offset, size_t len, char **bufpp)
{
	size_t size;
	int pg_idx = offset >> PAGE_SHIFT;
	int pg_offset = offset & (PAGE_SIZE - 1);
	struct tmc_pages *data_pages = &sg_table->data_pages;

	size = tmc_sg_table_buf_size(sg_table);
	if (offset >= size)
		return -EINVAL;

	/* Make sure we don't go beyond the end */
	len = (len < (size - offset)) ? len : size - offset;
	/* Respect the page boundaries */
	len = (len < (PAGE_SIZE - pg_offset)) ? len : (PAGE_SIZE - pg_offset);
	if (len > 0)
		*bufpp = page_address(data_pages->pages[pg_idx]) + pg_offset;
	return len;
}

#ifdef ETR_SG_DEBUG
/* Map a dma address to virtual address */
static unsigned long
tmc_sg_daddr_to_vaddr(struct tmc_sg_table *sg_table,
		      dma_addr_t addr, bool table)
{
	long offset;
	unsigned long base;
	struct tmc_pages *tmc_pages;

	if (table) {
		tmc_pages = &sg_table->table_pages;
		base = (unsigned long)sg_table->table_vaddr;
	} else {
		tmc_pages = &sg_table->data_pages;
		base = (unsigned long)sg_table->data_vaddr;
	}

	offset = tmc_pages_get_offset(tmc_pages, addr);
	if (offset < 0)
		return 0;
	return base + offset;
}

/* Dump the given sg_table */
static void tmc_etr_sg_table_dump(struct etr_sg_table *etr_table)
{
	sgte_t *ptr;
	int i = 0;
	dma_addr_t addr;
	struct tmc_sg_table *sg_table = etr_table->sg_table;

	ptr = (sgte_t *)tmc_sg_daddr_to_vaddr(sg_table,
					      etr_table->hwaddr, true);
	while (ptr) {
		addr = ETR_SG_ADDR(*ptr);
		switch (ETR_SG_ET(*ptr)) {
		case ETR_SG_ET_NORMAL:
			dev_dbg(sg_table->dev,
				"%05d: %p\t:[N] 0x%llx\n", i, ptr, addr);
			ptr++;
			break;
		case ETR_SG_ET_LINK:
			dev_dbg(sg_table->dev,
				"%05d: *** %p\t:{L} 0x%llx ***\n",
				 i, ptr, addr);
			ptr = (sgte_t *)tmc_sg_daddr_to_vaddr(sg_table,
							      addr, true);
			break;
		case ETR_SG_ET_LAST:
			dev_dbg(sg_table->dev,
				"%05d: ### %p\t:[L] 0x%llx ###\n",
				 i, ptr, addr);
			return;
		default:
			dev_dbg(sg_table->dev,
				"%05d: xxx %p\t:[INVALID] 0x%llx xxx\n",
				 i, ptr, addr);
			return;
		}
		i++;
	}
	dev_dbg(sg_table->dev, "******* End of Table *****\n");
}
#else
static inline void tmc_etr_sg_table_dump(struct etr_sg_table *etr_table) {}
#endif

/*
 * Populate the SG Table page table entries from table/data
 * pages allocated. Each Data page has ETR_SG_PAGES_PER_SYSPAGE SG pages.
 * So does a Table page. So we keep track of indices of the tables
 * in each system page and move the pointers accordingly.
 */
#define INC_IDX_ROUND(idx, size) ((idx) = ((idx) + 1) % (size))
static void tmc_etr_sg_table_populate(struct etr_sg_table *etr_table)
{
	dma_addr_t paddr;
	int i, type, nr_entries;
	int tpidx = 0; /* index to the current system table_page */
	int sgtidx = 0;	/* index to the sg_table within the current syspage */
	int sgtentry = 0; /* the entry within the sg_table */
	int dpidx = 0; /* index to the current system data_page */
	int spidx = 0; /* index to the SG page within the current data page */
	sgte_t *ptr; /* pointer to the table entry to fill */
	struct tmc_sg_table *sg_table = etr_table->sg_table;
	dma_addr_t *table_daddrs = sg_table->table_pages.daddrs;
	dma_addr_t *data_daddrs = sg_table->data_pages.daddrs;

	nr_entries = tmc_etr_sg_table_entries(sg_table->data_pages.nr_pages);
	/*
	 * Use the contiguous virtual address of the table to update entries.
	 */
	ptr = sg_table->table_vaddr;
	/*
	 * Fill all the entries, except the last entry to avoid special
	 * checks within the loop.
	 */
	for (i = 0; i < nr_entries - 1; i++) {
		if (sgtentry == ETR_SG_PTRS_PER_PAGE - 1) {
			/*
			 * Last entry in a sg_table page is a link address to
			 * the next table page. If this sg_table is the last
			 * one in the system page, it links to the first
			 * sg_table in the next system page. Otherwise, it
			 * links to the next sg_table page within the system
			 * page.
			 */
			if (sgtidx == ETR_SG_PAGES_PER_SYSPAGE - 1) {
				paddr = table_daddrs[tpidx + 1];
			} else {
				paddr = table_daddrs[tpidx] +
					(ETR_SG_PAGE_SIZE * (sgtidx + 1));
			}
			type = ETR_SG_ET_LINK;
		} else {
			/*
			 * Update the indices to the data_pages to point to the
			 * next sg_page in the data buffer.
			 */
			type = ETR_SG_ET_NORMAL;
			paddr = data_daddrs[dpidx] + spidx * ETR_SG_PAGE_SIZE;
			if (!INC_IDX_ROUND(spidx, ETR_SG_PAGES_PER_SYSPAGE))
				dpidx++;
		}
		*ptr++ = ETR_SG_ENTRY(paddr, type);
		/*
		 * Move to the next table pointer, moving the table page index
		 * if necessary
		 */
		if (!INC_IDX_ROUND(sgtentry, ETR_SG_PTRS_PER_PAGE)) {
			if (!INC_IDX_ROUND(sgtidx, ETR_SG_PAGES_PER_SYSPAGE))
				tpidx++;
		}
	}

	/* Set up the last entry, which is always a data pointer */
	paddr = data_daddrs[dpidx] + spidx * ETR_SG_PAGE_SIZE;
	*ptr++ = ETR_SG_ENTRY(paddr, ETR_SG_ET_LAST);
}

/*
 * tmc_init_etr_sg_table: Allocate a TMC ETR SG table, data buffer of @size and
 * populate the table.
 *
 * @dev		- Device pointer for the TMC
 * @node	- NUMA node where the memory should be allocated
 * @size	- Total size of the data buffer
 * @pages	- Optional list of page virtual address
 */
static struct etr_sg_table *
tmc_init_etr_sg_table(struct device *dev, int node,
		      unsigned long size, void **pages)
{
	int nr_entries, nr_tpages;
	int nr_dpages = size >> PAGE_SHIFT;
	struct tmc_sg_table *sg_table;
	struct etr_sg_table *etr_table;

	etr_table = kzalloc(sizeof(*etr_table), GFP_KERNEL);
	if (!etr_table)
		return ERR_PTR(-ENOMEM);
	nr_entries = tmc_etr_sg_table_entries(nr_dpages);
	nr_tpages = DIV_ROUND_UP(nr_entries, ETR_SG_PTRS_PER_SYSPAGE);

	sg_table = tmc_alloc_sg_table(dev, node, nr_tpages, nr_dpages, pages);
	if (IS_ERR(sg_table)) {
		kfree(etr_table);
		return ERR_CAST(sg_table);
	}

	etr_table->sg_table = sg_table;
	/* TMC should use table base address for DBA */
	etr_table->hwaddr = sg_table->table_daddr;
	tmc_etr_sg_table_populate(etr_table);
	/* Sync the table pages for the HW */
	tmc_sg_table_sync_table(sg_table);
	tmc_etr_sg_table_dump(etr_table);

	return etr_table;
}

/*
 * tmc_etr_alloc_flat_buf: Allocate a contiguous DMA buffer.
 */
static int tmc_etr_alloc_flat_buf(struct tmc_drvdata *drvdata,
				  struct etr_buf *etr_buf, int node,
				  void **pages)
{
	struct etr_flat_buf *flat_buf;
	struct device *real_dev = drvdata->csdev->dev.parent;

	/* We cannot reuse existing pages for flat buf */
	if (pages)
		return -EINVAL;

	flat_buf = kzalloc(sizeof(*flat_buf), GFP_KERNEL);
	if (!flat_buf)
		return -ENOMEM;

	flat_buf->vaddr = dma_alloc_coherent(real_dev, etr_buf->size,
					     &flat_buf->daddr, GFP_KERNEL);
	if (!flat_buf->vaddr) {
		kfree(flat_buf);
		return -ENOMEM;
	}

	flat_buf->size = etr_buf->size;
	flat_buf->dev = &drvdata->csdev->dev;
	etr_buf->hwaddr = flat_buf->daddr;
	etr_buf->mode = ETR_MODE_FLAT;
	etr_buf->private = flat_buf;
	return 0;
}

static void tmc_etr_free_flat_buf(struct etr_buf *etr_buf)
{
	struct etr_flat_buf *flat_buf = etr_buf->private;

	if (flat_buf && flat_buf->daddr) {
		struct device *real_dev = flat_buf->dev->parent;

		dma_free_coherent(real_dev, flat_buf->size,
				  flat_buf->vaddr, flat_buf->daddr);
	}
	kfree(flat_buf);
}

static void tmc_etr_sync_flat_buf(struct etr_buf *etr_buf, u64 rrp, u64 rwp)
{
	/*
	 * Adjust the buffer to point to the beginning of the trace data
	 * and update the available trace data.
	 */
	etr_buf->offset = rrp - etr_buf->hwaddr;
	if (etr_buf->full)
		etr_buf->len = etr_buf->size;
	else
		etr_buf->len = rwp - rrp;
}

static ssize_t tmc_etr_get_data_flat_buf(struct etr_buf *etr_buf,
					 u64 offset, size_t len, char **bufpp)
{
	struct etr_flat_buf *flat_buf = etr_buf->private;

	*bufpp = (char *)flat_buf->vaddr + offset;
	/*
	 * tmc_etr_buf_get_data already adjusts the length to handle
	 * buffer wrapping around.
	 */
	return len;
}

static const struct etr_buf_operations etr_flat_buf_ops = {
	.alloc = tmc_etr_alloc_flat_buf,
	.free = tmc_etr_free_flat_buf,
	.sync = tmc_etr_sync_flat_buf,
	.get_data = tmc_etr_get_data_flat_buf,
};

/*
 * tmc_etr_alloc_sg_buf: Allocate an SG buf @etr_buf. Setup the parameters
 * appropriately.
 */
static int tmc_etr_alloc_sg_buf(struct tmc_drvdata *drvdata,
				struct etr_buf *etr_buf, int node,
				void **pages)
{
	struct etr_sg_table *etr_table;
	struct device *dev = &drvdata->csdev->dev;

	etr_table = tmc_init_etr_sg_table(dev, node,
					  etr_buf->size, pages);
	if (IS_ERR(etr_table))
		return -ENOMEM;
	etr_buf->hwaddr = etr_table->hwaddr;
	etr_buf->mode = ETR_MODE_ETR_SG;
	etr_buf->private = etr_table;
	return 0;
}

static void tmc_etr_free_sg_buf(struct etr_buf *etr_buf)
{
	struct etr_sg_table *etr_table = etr_buf->private;

	if (etr_table) {
		tmc_free_sg_table(etr_table->sg_table);
		kfree(etr_table);
	}
}

static ssize_t tmc_etr_get_data_sg_buf(struct etr_buf *etr_buf, u64 offset,
				       size_t len, char **bufpp)
{
	struct etr_sg_table *etr_table = etr_buf->private;

	return tmc_sg_table_get_data(etr_table->sg_table, offset, len, bufpp);
}

static void tmc_etr_sync_sg_buf(struct etr_buf *etr_buf, u64 rrp, u64 rwp)
{
	long r_offset, w_offset;
	struct etr_sg_table *etr_table = etr_buf->private;
	struct tmc_sg_table *table = etr_table->sg_table;

	/* Convert hw address to offset in the buffer */
	r_offset = tmc_sg_get_data_page_offset(table, rrp);
	if (r_offset < 0) {
		dev_warn(table->dev,
			 "Unable to map RRP %llx to offset\n", rrp);
		etr_buf->len = 0;
		return;
	}

	w_offset = tmc_sg_get_data_page_offset(table, rwp);
	if (w_offset < 0) {
		dev_warn(table->dev,
			 "Unable to map RWP %llx to offset\n", rwp);
		etr_buf->len = 0;
		return;
	}

	etr_buf->offset = r_offset;
	if (etr_buf->full)
		etr_buf->len = etr_buf->size;
	else
		etr_buf->len = ((w_offset < r_offset) ? etr_buf->size : 0) +
				w_offset - r_offset;
	tmc_sg_table_sync_data_range(table, r_offset, etr_buf->len);
}

static const struct etr_buf_operations etr_sg_buf_ops = {
	.alloc = tmc_etr_alloc_sg_buf,
	.free = tmc_etr_free_sg_buf,
	.sync = tmc_etr_sync_sg_buf,
	.get_data = tmc_etr_get_data_sg_buf,
};

/*
 * TMC ETR could be connected to a CATU device, which can provide address
 * translation service. This is represented by the Output port of the TMC
 * (ETR) connected to the input port of the CATU.
 *
 * Returns	: coresight_device ptr for the CATU device if a CATU is found.
 *		: NULL otherwise.
 */
struct coresight_device *
tmc_etr_get_catu_device(struct tmc_drvdata *drvdata)
{
	int i;
	struct coresight_device *tmp, *etr = drvdata->csdev;

	if (!IS_ENABLED(CONFIG_CORESIGHT_CATU))
		return NULL;

	for (i = 0; i < etr->pdata->nr_outport; i++) {
		tmp = etr->pdata->conns[i].child_dev;
		if (tmp && coresight_is_catu_device(tmp))
			return tmp;
	}

	return NULL;
}

static inline int tmc_etr_enable_catu(struct tmc_drvdata *drvdata,
				      struct etr_buf *etr_buf)
{
	struct coresight_device *catu = tmc_etr_get_catu_device(drvdata);

	if (catu && helper_ops(catu)->enable)
		return helper_ops(catu)->enable(catu, etr_buf);
	return 0;
}

static inline void tmc_etr_disable_catu(struct tmc_drvdata *drvdata)
{
	struct coresight_device *catu = tmc_etr_get_catu_device(drvdata);

	if (catu && helper_ops(catu)->disable)
		helper_ops(catu)->disable(catu, drvdata->etr_buf);
}

static const struct etr_buf_operations *etr_buf_ops[] = {
	[ETR_MODE_FLAT] = &etr_flat_buf_ops,
	[ETR_MODE_ETR_SG] = &etr_sg_buf_ops,
	[ETR_MODE_CATU] = IS_ENABLED(CONFIG_CORESIGHT_CATU)
						? &etr_catu_buf_ops : NULL,
};

static inline int tmc_etr_mode_alloc_buf(int mode,
					 struct tmc_drvdata *drvdata,
					 struct etr_buf *etr_buf, int node,
					 void **pages)
{
	int rc = -EINVAL;

	switch (mode) {
	case ETR_MODE_FLAT:
	case ETR_MODE_ETR_SG:
	case ETR_MODE_CATU:
		if (etr_buf_ops[mode] && etr_buf_ops[mode]->alloc)
			rc = etr_buf_ops[mode]->alloc(drvdata, etr_buf,
						      node, pages);
		if (!rc)
			etr_buf->ops = etr_buf_ops[mode];
		return rc;
	default:
		return -EINVAL;
	}
}

/*
 * tmc_alloc_etr_buf: Allocate a buffer use by ETR.
 * @drvdata	: ETR device details.
 * @size	: size of the requested buffer.
 * @flags	: Required properties for the buffer.
 * @node	: Node for memory allocations.
 * @pages	: An optional list of pages.
 */
static struct etr_buf *tmc_alloc_etr_buf(struct tmc_drvdata *drvdata,
					 ssize_t size, int flags,
					 int node, void **pages)
{
	int rc = -ENOMEM;
	bool has_etr_sg, has_iommu;
	bool has_sg, has_catu;
	struct etr_buf *etr_buf;
	struct device *dev = &drvdata->csdev->dev;

	has_etr_sg = tmc_etr_has_cap(drvdata, TMC_ETR_SG);
	has_iommu = iommu_get_domain_for_dev(dev->parent);
	has_catu = !!tmc_etr_get_catu_device(drvdata);

	has_sg = has_catu || has_etr_sg;

	etr_buf = kzalloc(sizeof(*etr_buf), GFP_KERNEL);
	if (!etr_buf)
		return ERR_PTR(-ENOMEM);

	etr_buf->size = size;

	/*
	 * If we have to use an existing list of pages, we cannot reliably
	 * use a contiguous DMA memory (even if we have an IOMMU). Otherwise,
	 * we use the contiguous DMA memory if at least one of the following
	 * conditions is true:
	 *  a) The ETR cannot use Scatter-Gather.
	 *  b) we have a backing IOMMU
	 *  c) The requested memory size is smaller (< 1M).
	 *
	 * Fallback to available mechanisms.
	 *
	 */
	if (!pages &&
	    (!has_sg || has_iommu || size < SZ_1M))
		rc = tmc_etr_mode_alloc_buf(ETR_MODE_FLAT, drvdata,
					    etr_buf, node, pages);
	if (rc && has_etr_sg)
		rc = tmc_etr_mode_alloc_buf(ETR_MODE_ETR_SG, drvdata,
					    etr_buf, node, pages);
	if (rc && has_catu)
		rc = tmc_etr_mode_alloc_buf(ETR_MODE_CATU, drvdata,
					    etr_buf, node, pages);
	if (rc) {
		kfree(etr_buf);
		return ERR_PTR(rc);
	}

	refcount_set(&etr_buf->refcount, 1);
	dev_dbg(dev, "allocated buffer of size %ldKB in mode %d\n",
		(unsigned long)size >> 10, etr_buf->mode);
	return etr_buf;
}

static void tmc_free_etr_buf(struct etr_buf *etr_buf)
{
	WARN_ON(!etr_buf->ops || !etr_buf->ops->free);
	etr_buf->ops->free(etr_buf);
	kfree(etr_buf);
}

/*
 * tmc_etr_buf_get_data: Get the pointer the trace data at @offset
 * with a maximum of @len bytes.
 * Returns: The size of the linear data available @pos, with *bufpp
 * updated to point to the buffer.
 */
static ssize_t tmc_etr_buf_get_data(struct etr_buf *etr_buf,
				    u64 offset, size_t len, char **bufpp)
{
	/* Adjust the length to limit this transaction to end of buffer */
	len = (len < (etr_buf->size - offset)) ? len : etr_buf->size - offset;

	return etr_buf->ops->get_data(etr_buf, (u64)offset, len, bufpp);
}

static inline s64
tmc_etr_buf_insert_barrier_packet(struct etr_buf *etr_buf, u64 offset)
{
	ssize_t len;
	char *bufp;

	len = tmc_etr_buf_get_data(etr_buf, offset,
				   CORESIGHT_BARRIER_PKT_SIZE, &bufp);
	if (WARN_ON(len < CORESIGHT_BARRIER_PKT_SIZE))
		return -EINVAL;
	coresight_insert_barrier_packet(bufp);
	return offset + CORESIGHT_BARRIER_PKT_SIZE;
}

/*
 * tmc_sync_etr_buf: Sync the trace buffer availability with drvdata.
 * Makes sure the trace data is synced to the memory for consumption.
 * @etr_buf->offset will hold the offset to the beginning of the trace data
 * within the buffer, with @etr_buf->len bytes to consume.
 */
static void tmc_sync_etr_buf(struct tmc_drvdata *drvdata)
{
	struct etr_buf *etr_buf = drvdata->etr_buf;
	u64 rrp, rwp;
	u32 status;

	rrp = tmc_read_rrp(drvdata);
	rwp = tmc_read_rwp(drvdata);
	status = readl_relaxed(drvdata->base + TMC_STS);

	/*
	 * If there were memory errors in the session, truncate the
	 * buffer.
	 */
	if (WARN_ON_ONCE(status & TMC_STS_MEMERR)) {
		dev_dbg(&drvdata->csdev->dev,
			"tmc memory error detected, truncating buffer\n");
		etr_buf->len = 0;
		etr_buf->full = 0;
		return;
	}

	etr_buf->full = status & TMC_STS_FULL;

	WARN_ON(!etr_buf->ops || !etr_buf->ops->sync);

	etr_buf->ops->sync(etr_buf, rrp, rwp);
}

static void __tmc_etr_enable_hw(struct tmc_drvdata *drvdata)
{
	u32 axictl, sts;
	struct etr_buf *etr_buf = drvdata->etr_buf;

	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(etr_buf->size / 4, drvdata->base + TMC_RSZ);
	writel_relaxed(TMC_MODE_CIRCULAR_BUFFER, drvdata->base + TMC_MODE);

	axictl = readl_relaxed(drvdata->base + TMC_AXICTL);
	axictl &= ~TMC_AXICTL_CLEAR_MASK;
	axictl |= (TMC_AXICTL_PROT_CTL_B1 | TMC_AXICTL_WR_BURST_16);
	axictl |= TMC_AXICTL_AXCACHE_OS;

	if (tmc_etr_has_cap(drvdata, TMC_ETR_AXI_ARCACHE)) {
		axictl &= ~TMC_AXICTL_ARCACHE_MASK;
		axictl |= TMC_AXICTL_ARCACHE_OS;
	}

	if (etr_buf->mode == ETR_MODE_ETR_SG)
		axictl |= TMC_AXICTL_SCT_GAT_MODE;

	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);
	tmc_write_dba(drvdata, etr_buf->hwaddr);
	/*
	 * If the TMC pointers must be programmed before the session,
	 * we have to set it properly (i.e, RRP/RWP to base address and
	 * STS to "not full").
	 */
	if (tmc_etr_has_cap(drvdata, TMC_ETR_SAVE_RESTORE)) {
		tmc_write_rrp(drvdata, etr_buf->hwaddr);
		tmc_write_rwp(drvdata, etr_buf->hwaddr);
		sts = readl_relaxed(drvdata->base + TMC_STS) & ~TMC_STS_FULL;
		writel_relaxed(sts, drvdata->base + TMC_STS);
	}

	writel_relaxed(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI |
		       TMC_FFCR_FON_FLIN | TMC_FFCR_FON_TRIG_EVT |
		       TMC_FFCR_TRIGON_TRIGIN,
		       drvdata->base + TMC_FFCR);
	writel_relaxed(drvdata->trigger_cntr, drvdata->base + TMC_TRG);
	tmc_enable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_etr_enable_hw(struct tmc_drvdata *drvdata,
			     struct etr_buf *etr_buf)
{
	int rc;

	/* Callers should provide an appropriate buffer for use */
	if (WARN_ON(!etr_buf))
		return -EINVAL;

	if ((etr_buf->mode == ETR_MODE_ETR_SG) &&
	    WARN_ON(!tmc_etr_has_cap(drvdata, TMC_ETR_SG)))
		return -EINVAL;

	if (WARN_ON(drvdata->etr_buf))
		return -EBUSY;

	/*
	 * If this ETR is connected to a CATU, enable it before we turn
	 * this on.
	 */
	rc = tmc_etr_enable_catu(drvdata, etr_buf);
	if (rc)
		return rc;
	rc = coresight_claim_device(drvdata->base);
	if (!rc) {
		drvdata->etr_buf = etr_buf;
		__tmc_etr_enable_hw(drvdata);
	}

	return rc;
}

/*
 * Return the available trace data in the buffer (starts at etr_buf->offset,
 * limited by etr_buf->len) from @pos, with a maximum limit of @len,
 * also updating the @bufpp on where to find it. Since the trace data
 * starts at anywhere in the buffer, depending on the RRP, we adjust the
 * @len returned to handle buffer wrapping around.
 *
 * We are protected here by drvdata->reading != 0, which ensures the
 * sysfs_buf stays alive.
 */
ssize_t tmc_etr_get_sysfs_trace(struct tmc_drvdata *drvdata,
				loff_t pos, size_t len, char **bufpp)
{
	s64 offset;
	ssize_t actual = len;
	struct etr_buf *etr_buf = drvdata->sysfs_buf;

	if (pos + actual > etr_buf->len)
		actual = etr_buf->len - pos;
	if (actual <= 0)
		return actual;

	/* Compute the offset from which we read the data */
	offset = etr_buf->offset + pos;
	if (offset >= etr_buf->size)
		offset -= etr_buf->size;
	return tmc_etr_buf_get_data(etr_buf, offset, actual, bufpp);
}

static struct etr_buf *
tmc_etr_setup_sysfs_buf(struct tmc_drvdata *drvdata)
{
	return tmc_alloc_etr_buf(drvdata, drvdata->size,
				 0, cpu_to_node(0), NULL);
}

static void
tmc_etr_free_sysfs_buf(struct etr_buf *buf)
{
	if (buf)
		tmc_free_etr_buf(buf);
}

static void tmc_etr_sync_sysfs_buf(struct tmc_drvdata *drvdata)
{
	struct etr_buf *etr_buf = drvdata->etr_buf;

	if (WARN_ON(drvdata->sysfs_buf != etr_buf)) {
		tmc_etr_free_sysfs_buf(drvdata->sysfs_buf);
		drvdata->sysfs_buf = NULL;
	} else {
		tmc_sync_etr_buf(drvdata);
		/*
		 * Insert barrier packets at the beginning, if there was
		 * an overflow.
		 */
		if (etr_buf->full)
			tmc_etr_buf_insert_barrier_packet(etr_buf,
							  etr_buf->offset);
	}
}

static void __tmc_etr_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	/*
	 * When operating in sysFS mode the content of the buffer needs to be
	 * read before the TMC is disabled.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etr_sync_sysfs_buf(drvdata);

	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);

}

static void tmc_etr_disable_hw(struct tmc_drvdata *drvdata)
{
	__tmc_etr_disable_hw(drvdata);
	/* Disable CATU device if this ETR is connected to one */
	tmc_etr_disable_catu(drvdata);
	coresight_disclaim_device(drvdata->base);
	/* Reset the ETR buf used by hardware */
	drvdata->etr_buf = NULL;
}

static int tmc_enable_etr_sink_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct etr_buf *sysfs_buf = NULL, *new_buf = NULL, *free_buf = NULL;

	/*
	 * If we are enabling the ETR from disabled state, we need to make
	 * sure we have a buffer with the right size. The etr_buf is not reset
	 * immediately after we stop the tracing in SYSFS mode as we wait for
	 * the user to collect the data. We may be able to reuse the existing
	 * buffer, provided the size matches. Any allocation has to be done
	 * with the lock released.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);
	sysfs_buf = READ_ONCE(drvdata->sysfs_buf);
	if (!sysfs_buf || (sysfs_buf->size != drvdata->size)) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);

		/* Allocate memory with the locks released */
		free_buf = new_buf = tmc_etr_setup_sysfs_buf(drvdata);
		if (IS_ERR(new_buf))
			return PTR_ERR(new_buf);

		/* Let's try again */
		spin_lock_irqsave(&drvdata->spinlock, flags);
	}

	if (drvdata->reading || drvdata->mode == CS_MODE_PERF) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched, even if the buffer size has changed.
	 */
	if (drvdata->mode == CS_MODE_SYSFS) {
		atomic_inc(csdev->refcnt);
		goto out;
	}

	/*
	 * If we don't have a buffer or it doesn't match the requested size,
	 * use the buffer allocated above. Otherwise reuse the existing buffer.
	 */
	sysfs_buf = READ_ONCE(drvdata->sysfs_buf);
	if (!sysfs_buf || (new_buf && sysfs_buf->size != new_buf->size)) {
		free_buf = sysfs_buf;
		drvdata->sysfs_buf = new_buf;
	}

	ret = tmc_etr_enable_hw(drvdata, drvdata->sysfs_buf);
	if (!ret) {
		drvdata->mode = CS_MODE_SYSFS;
		atomic_inc(csdev->refcnt);
	}
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (free_buf)
		tmc_etr_free_sysfs_buf(free_buf);

	if (!ret)
		dev_dbg(&csdev->dev, "TMC-ETR enabled\n");

	return ret;
}

/*
 * alloc_etr_buf: Allocate ETR buffer for use by perf.
 * The size of the hardware buffer is dependent on the size configured
 * via sysfs and the perf ring buffer size. We prefer to allocate the
 * largest possible size, scaling down the size by half until it
 * reaches a minimum limit (1M), beyond which we give up.
 */
static struct etr_buf *
alloc_etr_buf(struct tmc_drvdata *drvdata, struct perf_event *event,
	      int nr_pages, void **pages, bool snapshot)
{
	int node;
	struct etr_buf *etr_buf;
	unsigned long size;

	node = (event->cpu == -1) ? NUMA_NO_NODE : cpu_to_node(event->cpu);
	/*
	 * Try to match the perf ring buffer size if it is larger
	 * than the size requested via sysfs.
	 */
	if ((nr_pages << PAGE_SHIFT) > drvdata->size) {
		etr_buf = tmc_alloc_etr_buf(drvdata, (nr_pages << PAGE_SHIFT),
					    0, node, NULL);
		if (!IS_ERR(etr_buf))
			goto done;
	}

	/*
	 * Else switch to configured size for this ETR
	 * and scale down until we hit the minimum limit.
	 */
	size = drvdata->size;
	do {
		etr_buf = tmc_alloc_etr_buf(drvdata, size, 0, node, NULL);
		if (!IS_ERR(etr_buf))
			goto done;
		size /= 2;
	} while (size >= TMC_ETR_PERF_MIN_BUF_SIZE);

	return ERR_PTR(-ENOMEM);

done:
	return etr_buf;
}

static struct etr_buf *
get_perf_etr_buf_cpu_wide(struct tmc_drvdata *drvdata,
			  struct perf_event *event, int nr_pages,
			  void **pages, bool snapshot)
{
	int ret;
	pid_t pid = task_pid_nr(event->owner);
	struct etr_buf *etr_buf;

retry:
	/*
	 * An etr_perf_buffer is associated with an event and holds a reference
	 * to the AUX ring buffer that was created for that event.  In CPU-wide
	 * N:1 mode multiple events (one per CPU), each with its own AUX ring
	 * buffer, share a sink.  As such an etr_perf_buffer is created for each
	 * event but a single etr_buf associated with the ETR is shared between
	 * them.  The last event in a trace session will copy the content of the
	 * etr_buf to its AUX ring buffer.  Ring buffer associated to other
	 * events are simply not used an freed as events are destoyed.  We still
	 * need to allocate a ring buffer for each event since we don't know
	 * which event will be last.
	 */

	/*
	 * The first thing to do here is check if an etr_buf has already been
	 * allocated for this session.  If so it is shared with this event,
	 * otherwise it is created.
	 */
	mutex_lock(&drvdata->idr_mutex);
	etr_buf = idr_find(&drvdata->idr, pid);
	if (etr_buf) {
		refcount_inc(&etr_buf->refcount);
		mutex_unlock(&drvdata->idr_mutex);
		return etr_buf;
	}

	/* If we made it here no buffer has been allocated, do so now. */
	mutex_unlock(&drvdata->idr_mutex);

	etr_buf = alloc_etr_buf(drvdata, event, nr_pages, pages, snapshot);
	if (IS_ERR(etr_buf))
		return etr_buf;

	/* Now that we have a buffer, add it to the IDR. */
	mutex_lock(&drvdata->idr_mutex);
	ret = idr_alloc(&drvdata->idr, etr_buf, pid, pid + 1, GFP_KERNEL);
	mutex_unlock(&drvdata->idr_mutex);

	/* Another event with this session ID has allocated this buffer. */
	if (ret == -ENOSPC) {
		tmc_free_etr_buf(etr_buf);
		goto retry;
	}

	/* The IDR can't allocate room for a new session, abandon ship. */
	if (ret == -ENOMEM) {
		tmc_free_etr_buf(etr_buf);
		return ERR_PTR(ret);
	}


	return etr_buf;
}

static struct etr_buf *
get_perf_etr_buf_per_thread(struct tmc_drvdata *drvdata,
			    struct perf_event *event, int nr_pages,
			    void **pages, bool snapshot)
{
	/*
	 * In per-thread mode the etr_buf isn't shared, so just go ahead
	 * with memory allocation.
	 */
	return alloc_etr_buf(drvdata, event, nr_pages, pages, snapshot);
}

static struct etr_buf *
get_perf_etr_buf(struct tmc_drvdata *drvdata, struct perf_event *event,
		 int nr_pages, void **pages, bool snapshot)
{
	if (event->cpu == -1)
		return get_perf_etr_buf_per_thread(drvdata, event, nr_pages,
						   pages, snapshot);

	return get_perf_etr_buf_cpu_wide(drvdata, event, nr_pages,
					 pages, snapshot);
}

static struct etr_perf_buffer *
tmc_etr_setup_perf_buf(struct tmc_drvdata *drvdata, struct perf_event *event,
		       int nr_pages, void **pages, bool snapshot)
{
	int node;
	struct etr_buf *etr_buf;
	struct etr_perf_buffer *etr_perf;

	node = (event->cpu == -1) ? NUMA_NO_NODE : cpu_to_node(event->cpu);

	etr_perf = kzalloc_node(sizeof(*etr_perf), GFP_KERNEL, node);
	if (!etr_perf)
		return ERR_PTR(-ENOMEM);

	etr_buf = get_perf_etr_buf(drvdata, event, nr_pages, pages, snapshot);
	if (!IS_ERR(etr_buf))
		goto done;

	kfree(etr_perf);
	return ERR_PTR(-ENOMEM);

done:
	/*
	 * Keep a reference to the ETR this buffer has been allocated for
	 * in order to have access to the IDR in tmc_free_etr_buffer().
	 */
	etr_perf->drvdata = drvdata;
	etr_perf->etr_buf = etr_buf;

	return etr_perf;
}


static void *tmc_alloc_etr_buffer(struct coresight_device *csdev,
				  struct perf_event *event, void **pages,
				  int nr_pages, bool snapshot)
{
	struct etr_perf_buffer *etr_perf;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	etr_perf = tmc_etr_setup_perf_buf(drvdata, event,
					  nr_pages, pages, snapshot);
	if (IS_ERR(etr_perf)) {
		dev_dbg(&csdev->dev, "Unable to allocate ETR buffer\n");
		return NULL;
	}

	etr_perf->pid = task_pid_nr(event->owner);
	etr_perf->snapshot = snapshot;
	etr_perf->nr_pages = nr_pages;
	etr_perf->pages = pages;

	return etr_perf;
}

static void tmc_free_etr_buffer(void *config)
{
	struct etr_perf_buffer *etr_perf = config;
	struct tmc_drvdata *drvdata = etr_perf->drvdata;
	struct etr_buf *buf, *etr_buf = etr_perf->etr_buf;

	if (!etr_buf)
		goto free_etr_perf_buffer;

	mutex_lock(&drvdata->idr_mutex);
	/* If we are not the last one to use the buffer, don't touch it. */
	if (!refcount_dec_and_test(&etr_buf->refcount)) {
		mutex_unlock(&drvdata->idr_mutex);
		goto free_etr_perf_buffer;
	}

	/* We are the last one, remove from the IDR and free the buffer. */
	buf = idr_remove(&drvdata->idr, etr_perf->pid);
	mutex_unlock(&drvdata->idr_mutex);

	/*
	 * Something went very wrong if the buffer associated with this ID
	 * is not the same in the IDR.  Leak to avoid use after free.
	 */
	if (buf && WARN_ON(buf != etr_buf))
		goto free_etr_perf_buffer;

	tmc_free_etr_buf(etr_perf->etr_buf);

free_etr_perf_buffer:
	kfree(etr_perf);
}

/*
 * tmc_etr_sync_perf_buffer: Copy the actual trace data from the hardware
 * buffer to the perf ring buffer.
 */
static void tmc_etr_sync_perf_buffer(struct etr_perf_buffer *etr_perf,
				     unsigned long src_offset,
				     unsigned long to_copy)
{
	long bytes;
	long pg_idx, pg_offset;
	unsigned long head = etr_perf->head;
	char **dst_pages, *src_buf;
	struct etr_buf *etr_buf = etr_perf->etr_buf;

	head = etr_perf->head;
	pg_idx = head >> PAGE_SHIFT;
	pg_offset = head & (PAGE_SIZE - 1);
	dst_pages = (char **)etr_perf->pages;

	while (to_copy > 0) {
		/*
		 * In one iteration, we can copy minimum of :
		 *  1) what is available in the source buffer,
		 *  2) what is available in the source buffer, before it
		 *     wraps around.
		 *  3) what is available in the destination page.
		 * in one iteration.
		 */
		if (src_offset >= etr_buf->size)
			src_offset -= etr_buf->size;
		bytes = tmc_etr_buf_get_data(etr_buf, src_offset, to_copy,
					     &src_buf);
		if (WARN_ON_ONCE(bytes <= 0))
			break;
		bytes = min(bytes, (long)(PAGE_SIZE - pg_offset));

		memcpy(dst_pages[pg_idx] + pg_offset, src_buf, bytes);

		to_copy -= bytes;

		/* Move destination pointers */
		pg_offset += bytes;
		if (pg_offset == PAGE_SIZE) {
			pg_offset = 0;
			if (++pg_idx == etr_perf->nr_pages)
				pg_idx = 0;
		}

		/* Move source pointers */
		src_offset += bytes;
	}
}

/*
 * tmc_update_etr_buffer : Update the perf ring buffer with the
 * available trace data. We use software double buffering at the moment.
 *
 * TODO: Add support for reusing the perf ring buffer.
 */
static unsigned long
tmc_update_etr_buffer(struct coresight_device *csdev,
		      struct perf_output_handle *handle,
		      void *config)
{
	bool lost = false;
	unsigned long flags, offset, size = 0;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct etr_perf_buffer *etr_perf = config;
	struct etr_buf *etr_buf = etr_perf->etr_buf;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* Don't do anything if another tracer is using this sink */
	if (atomic_read(csdev->refcnt) != 1) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		goto out;
	}

	if (WARN_ON(drvdata->perf_buf != etr_buf)) {
		lost = true;
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		goto out;
	}

	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	tmc_sync_etr_buf(drvdata);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	lost = etr_buf->full;
	offset = etr_buf->offset;
	size = etr_buf->len;

	/*
	 * The ETR buffer may be bigger than the space available in the
	 * perf ring buffer (handle->size).  If so advance the offset so that we
	 * get the latest trace data.  In snapshot mode none of that matters
	 * since we are expected to clobber stale data in favour of the latest
	 * traces.
	 */
	if (!etr_perf->snapshot && size > handle->size) {
		u32 mask = tmc_get_memwidth_mask(drvdata);

		/*
		 * Make sure the new size is aligned in accordance with the
		 * requirement explained in function tmc_get_memwidth_mask().
		 */
		size = handle->size & mask;
		offset = etr_buf->offset + etr_buf->len - size;

		if (offset >= etr_buf->size)
			offset -= etr_buf->size;
		lost = true;
	}

	/* Insert barrier packets at the beginning, if there was an overflow */
	if (lost)
		tmc_etr_buf_insert_barrier_packet(etr_buf, etr_buf->offset);
	tmc_etr_sync_perf_buffer(etr_perf, offset, size);

	/*
	 * In snapshot mode we simply increment the head by the number of byte
	 * that were written.  User space function  cs_etm_find_snapshot() will
	 * figure out how many bytes to get from the AUX buffer based on the
	 * position of the head.
	 */
	if (etr_perf->snapshot)
		handle->head += size;
out:
	/*
	 * Don't set the TRUNCATED flag in snapshot mode because 1) the
	 * captured buffer is expected to be truncated and 2) a full buffer
	 * prevents the event from being re-enabled by the perf core,
	 * resulting in stale data being send to user space.
	 */
	if (!etr_perf->snapshot && lost)
		perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);
	return size;
}

static int tmc_enable_etr_sink_perf(struct coresight_device *csdev, void *data)
{
	int rc = 0;
	pid_t pid;
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	struct perf_output_handle *handle = data;
	struct etr_perf_buffer *etr_perf = etm_perf_sink_config(handle);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	 /* Don't use this sink if it is already claimed by sysFS */
	if (drvdata->mode == CS_MODE_SYSFS) {
		rc = -EBUSY;
		goto unlock_out;
	}

	if (WARN_ON(!etr_perf || !etr_perf->etr_buf)) {
		rc = -EINVAL;
		goto unlock_out;
	}

	/* Get a handle on the pid of the process to monitor */
	pid = etr_perf->pid;

	/* Do not proceed if this device is associated with another session */
	if (drvdata->pid != -1 && drvdata->pid != pid) {
		rc = -EBUSY;
		goto unlock_out;
	}

	etr_perf->head = PERF_IDX2OFF(handle->head, etr_perf);

	/*
	 * No HW configuration is needed if the sink is already in
	 * use for this session.
	 */
	if (drvdata->pid == pid) {
		atomic_inc(csdev->refcnt);
		goto unlock_out;
	}

	rc = tmc_etr_enable_hw(drvdata, etr_perf->etr_buf);
	if (!rc) {
		/* Associate with monitored process. */
		drvdata->pid = pid;
		drvdata->mode = CS_MODE_PERF;
		drvdata->perf_buf = etr_perf->etr_buf;
		atomic_inc(csdev->refcnt);
	}

unlock_out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
	return rc;
}

static int tmc_enable_etr_sink(struct coresight_device *csdev,
			       u32 mode, void *data)
{
	switch (mode) {
	case CS_MODE_SYSFS:
		return tmc_enable_etr_sink_sysfs(csdev);
	case CS_MODE_PERF:
		return tmc_enable_etr_sink_perf(csdev, data);
	}

	/* We shouldn't be here */
	return -EINVAL;
}

static int tmc_disable_etr_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);

	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	if (atomic_dec_return(csdev->refcnt)) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return -EBUSY;
	}

	/* Complain if we (somehow) got out of sync */
	WARN_ON_ONCE(drvdata->mode == CS_MODE_DISABLED);
	tmc_etr_disable_hw(drvdata);
	/* Dissociate from monitored process. */
	drvdata->pid = -1;
	drvdata->mode = CS_MODE_DISABLED;
	/* Reset perf specific data */
	drvdata->perf_buf = NULL;

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_dbg(&csdev->dev, "TMC-ETR disabled\n");
	return 0;
}

static const struct coresight_ops_sink tmc_etr_sink_ops = {
	.enable		= tmc_enable_etr_sink,
	.disable	= tmc_disable_etr_sink,
	.alloc_buffer	= tmc_alloc_etr_buffer,
	.update_buffer	= tmc_update_etr_buffer,
	.free_buffer	= tmc_free_etr_buffer,
};

const struct coresight_ops tmc_etr_cs_ops = {
	.sink_ops	= &tmc_etr_sink_ops,
};

int tmc_read_prepare_etr(struct tmc_drvdata *drvdata)
{
	int ret = 0;
	unsigned long flags;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * We can safely allow reads even if the ETR is operating in PERF mode,
	 * since the sysfs session is captured in mode specific data.
	 * If drvdata::sysfs_data is NULL the trace data has been read already.
	 */
	if (!drvdata->sysfs_buf) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if we are trying to read from a running session. */
	if (drvdata->mode == CS_MODE_SYSFS)
		__tmc_etr_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata)
{
	unsigned long flags;
	struct etr_buf *sysfs_buf = NULL;

	/* config types are set a boot time and never change */
	if (WARN_ON_ONCE(drvdata->config_type != TMC_CONFIG_TYPE_ETR))
		return -EINVAL;

	spin_lock_irqsave(&drvdata->spinlock, flags);

	/* RE-enable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS) {
		/*
		 * The trace run will continue with the same allocated trace
		 * buffer. Since the tracer is still enabled drvdata::buf can't
		 * be NULL.
		 */
		__tmc_etr_enable_hw(drvdata);
	} else {
		/*
		 * The ETR is not tracing and the buffer was just read.
		 * As such prepare to free the trace buffer.
		 */
		sysfs_buf = drvdata->sysfs_buf;
		drvdata->sysfs_buf = NULL;
	}

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free allocated memory out side of the spinlock */
	if (sysfs_buf)
		tmc_etr_free_sysfs_buf(sysfs_buf);

	return 0;
}
