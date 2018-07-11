// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2016 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/coresight.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include "coresight-priv.h"
#include "coresight-tmc.h"

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

	for (i = 0; i < tmc_pages->nr_pages; i++) {
		if (tmc_pages->daddrs && tmc_pages->daddrs[i])
			dma_unmap_page(dev, tmc_pages->daddrs[i],
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
		paddr = dma_map_page(dev, page, 0, PAGE_SIZE, dir);
		if (dma_mapping_error(dev, paddr))
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
 * @dev		- Device to which page should be DMA mapped.
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
	struct device *dev = table->dev;
	struct tmc_pages *data = &table->data_pages;

	start = offset >> PAGE_SHIFT;
	for (i = start; i < (start + npages); i++) {
		index = i % data->nr_pages;
		dma_sync_single_for_cpu(dev, data->daddrs[index],
					PAGE_SIZE, DMA_FROM_DEVICE);
	}
}

/* tmc_sg_sync_table: Sync the page table */
void tmc_sg_table_sync_table(struct tmc_sg_table *sg_table)
{
	int i;
	struct device *dev = sg_table->dev;
	struct tmc_pages *table_pages = &sg_table->table_pages;

	for (i = 0; i < table_pages->nr_pages; i++)
		dma_sync_single_for_device(dev, table_pages->daddrs[i],
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
static struct etr_sg_table __maybe_unused *
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
		return ERR_PTR(PTR_ERR(sg_table));
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

static void tmc_etr_enable_hw(struct tmc_drvdata *drvdata)
{
	u32 axictl, sts;

	CS_UNLOCK(drvdata->base);

	/* Wait for TMCSReady bit to be set */
	tmc_wait_for_tmcready(drvdata);

	writel_relaxed(drvdata->size / 4, drvdata->base + TMC_RSZ);
	writel_relaxed(TMC_MODE_CIRCULAR_BUFFER, drvdata->base + TMC_MODE);

	axictl = readl_relaxed(drvdata->base + TMC_AXICTL);
	axictl &= ~TMC_AXICTL_CLEAR_MASK;
	axictl |= (TMC_AXICTL_PROT_CTL_B1 | TMC_AXICTL_WR_BURST_16);
	axictl |= TMC_AXICTL_AXCACHE_OS;

	if (tmc_etr_has_cap(drvdata, TMC_ETR_AXI_ARCACHE)) {
		axictl &= ~TMC_AXICTL_ARCACHE_MASK;
		axictl |= TMC_AXICTL_ARCACHE_OS;
	}

	writel_relaxed(axictl, drvdata->base + TMC_AXICTL);
	tmc_write_dba(drvdata, drvdata->paddr);
	/*
	 * If the TMC pointers must be programmed before the session,
	 * we have to set it properly (i.e, RRP/RWP to base address and
	 * STS to "not full").
	 */
	if (tmc_etr_has_cap(drvdata, TMC_ETR_SAVE_RESTORE)) {
		tmc_write_rrp(drvdata, drvdata->paddr);
		tmc_write_rwp(drvdata, drvdata->paddr);
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

/*
 * Return the available trace data in the buffer @pos, with a maximum
 * limit of @len, also updating the @bufpp on where to find it.
 */
ssize_t tmc_etr_get_sysfs_trace(struct tmc_drvdata *drvdata,
				loff_t pos, size_t len, char **bufpp)
{
	ssize_t actual = len;
	char *bufp = drvdata->buf + pos;
	char *bufend = (char *)(drvdata->vaddr + drvdata->size);

	/* Adjust the len to available size @pos */
	if (pos + actual > drvdata->len)
		actual = drvdata->len - pos;

	if (actual <= 0)
		return actual;

	/*
	 * Since we use a circular buffer, with trace data starting
	 * @drvdata->buf, possibly anywhere in the buffer @drvdata->vaddr,
	 * wrap the current @pos to within the buffer.
	 */
	if (bufp >= bufend)
		bufp -= drvdata->size;
	/*
	 * For simplicity, avoid copying over a wrapped around buffer.
	 */
	if ((bufp + actual) > bufend)
		actual = bufend - bufp;
	*bufpp = bufp;
	return actual;
}

static void tmc_etr_dump_hw(struct tmc_drvdata *drvdata)
{
	u32 val;
	u64 rwp;

	rwp = tmc_read_rwp(drvdata);
	val = readl_relaxed(drvdata->base + TMC_STS);

	/*
	 * Adjust the buffer to point to the beginning of the trace data
	 * and update the available trace data.
	 */
	if (val & TMC_STS_FULL) {
		drvdata->buf = drvdata->vaddr + rwp - drvdata->paddr;
		drvdata->len = drvdata->size;
		coresight_insert_barrier_packet(drvdata->buf);
	} else {
		drvdata->buf = drvdata->vaddr;
		drvdata->len = rwp - drvdata->paddr;
	}
}

static void tmc_etr_disable_hw(struct tmc_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	tmc_flush_and_stop(drvdata);
	/*
	 * When operating in sysFS mode the content of the buffer needs to be
	 * read before the TMC is disabled.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etr_dump_hw(drvdata);
	tmc_disable_hw(drvdata);

	CS_LOCK(drvdata->base);
}

static int tmc_enable_etr_sink_sysfs(struct coresight_device *csdev)
{
	int ret = 0;
	bool used = false;
	unsigned long flags;
	void __iomem *vaddr = NULL;
	dma_addr_t paddr = 0;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * If we don't have a buffer release the lock and allocate memory.
	 * Otherwise keep the lock and move along.
	 */
	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (!drvdata->vaddr) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);

		/*
		 * Contiguous  memory can't be allocated while a spinlock is
		 * held.  As such allocate memory here and free it if a buffer
		 * has already been allocated (from a previous session).
		 */
		vaddr = dma_alloc_coherent(drvdata->dev, drvdata->size,
					   &paddr, GFP_KERNEL);
		if (!vaddr)
			return -ENOMEM;

		/* Let's try again */
		spin_lock_irqsave(&drvdata->spinlock, flags);
	}

	if (drvdata->reading) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * In sysFS mode we can have multiple writers per sink.  Since this
	 * sink is already enabled no memory is needed and the HW need not be
	 * touched.
	 */
	if (drvdata->mode == CS_MODE_SYSFS)
		goto out;

	/*
	 * If drvdata::vaddr == NULL, use the memory allocated above.
	 * Otherwise a buffer still exists from a previous session, so
	 * simply use that.
	 */
	if (drvdata->vaddr == NULL) {
		used = true;
		drvdata->vaddr = vaddr;
		drvdata->paddr = paddr;
		drvdata->buf = drvdata->vaddr;
	}

	drvdata->mode = CS_MODE_SYSFS;
	tmc_etr_enable_hw(drvdata);
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free memory outside the spinlock if need be */
	if (!used && vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	if (!ret)
		dev_info(drvdata->dev, "TMC-ETR enabled\n");

	return ret;
}

static int tmc_enable_etr_sink_perf(struct coresight_device *csdev)
{
	/* We don't support perf mode yet ! */
	return -EINVAL;
}

static int tmc_enable_etr_sink(struct coresight_device *csdev, u32 mode)
{
	switch (mode) {
	case CS_MODE_SYSFS:
		return tmc_enable_etr_sink_sysfs(csdev);
	case CS_MODE_PERF:
		return tmc_enable_etr_sink_perf(csdev);
	}

	/* We shouldn't be here */
	return -EINVAL;
}

static void tmc_disable_etr_sink(struct coresight_device *csdev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->reading) {
		spin_unlock_irqrestore(&drvdata->spinlock, flags);
		return;
	}

	/* Disable the TMC only if it needs to */
	if (drvdata->mode != CS_MODE_DISABLED) {
		tmc_etr_disable_hw(drvdata);
		drvdata->mode = CS_MODE_DISABLED;
	}

	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "TMC-ETR disabled\n");
}

static const struct coresight_ops_sink tmc_etr_sink_ops = {
	.enable		= tmc_enable_etr_sink,
	.disable	= tmc_disable_etr_sink,
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

	/* Don't interfere if operated from Perf */
	if (drvdata->mode == CS_MODE_PERF) {
		ret = -EINVAL;
		goto out;
	}

	/* If drvdata::buf is NULL the trace data has been read already */
	if (drvdata->buf == NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* Disable the TMC if need be */
	if (drvdata->mode == CS_MODE_SYSFS)
		tmc_etr_disable_hw(drvdata);

	drvdata->reading = true;
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	return ret;
}

int tmc_read_unprepare_etr(struct tmc_drvdata *drvdata)
{
	unsigned long flags;
	dma_addr_t paddr;
	void __iomem *vaddr = NULL;

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
		tmc_etr_enable_hw(drvdata);
	} else {
		/*
		 * The ETR is not tracing and the buffer was just read.
		 * As such prepare to free the trace buffer.
		 */
		vaddr = drvdata->vaddr;
		paddr = drvdata->paddr;
		drvdata->buf = drvdata->vaddr = NULL;
	}

	drvdata->reading = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	/* Free allocated memory out side of the spinlock */
	if (vaddr)
		dma_free_coherent(drvdata->dev, drvdata->size, vaddr, paddr);

	return 0;
}
