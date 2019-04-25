// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Arm Limited. All rights reserved.
 *
 * Coresight Address Translation Unit support
 *
 * Author: Suzuki K Poulose <suzuki.poulose@arm.com>
 */

#include <linux/amba/bus.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "coresight-catu.h"
#include "coresight-priv.h"
#include "coresight-tmc.h"

#define csdev_to_catu_drvdata(csdev)	\
	dev_get_drvdata(csdev->dev.parent)

/* Verbose output for CATU table contents */
#ifdef CATU_DEBUG
#define catu_dbg(x, ...) dev_dbg(x, __VA_ARGS__)
#else
#define catu_dbg(x, ...) do {} while (0)
#endif

struct catu_etr_buf {
	struct tmc_sg_table *catu_table;
	dma_addr_t sladdr;
};

/*
 * CATU uses a page size of 4KB for page tables as well as data pages.
 * Each 64bit entry in the table has the following format.
 *
 *	63			12	1  0
 *	------------------------------------
 *	|	 Address [63-12] | SBZ	| V|
 *	------------------------------------
 *
 * Where bit[0] V indicates if the address is valid or not.
 * Each 4K table pages have upto 256 data page pointers, taking upto 2K
 * size. There are two Link pointers, pointing to the previous and next
 * table pages respectively at the end of the 4K page. (i.e, entry 510
 * and 511).
 *  E.g, a table of two pages could look like :
 *
 *                 Table Page 0               Table Page 1
 * SLADDR ===> x------------------x  x--> x-----------------x
 * INADDR    ->|  Page 0      | V |  |    | Page 256    | V | <- INADDR+1M
 *             |------------------|  |    |-----------------|
 * INADDR+4K ->|  Page 1      | V |  |    |                 |
 *             |------------------|  |    |-----------------|
 *             |  Page 2      | V |  |    |                 |
 *             |------------------|  |    |-----------------|
 *             |   ...        | V |  |    |    ...          |
 *             |------------------|  |    |-----------------|
 * INADDR+1020K|  Page 255    | V |  |    |   Page 511  | V |
 * SLADDR+2K==>|------------------|  |    |-----------------|
 *             |  UNUSED      |   |  |    |                 |
 *             |------------------|  |    |                 |
 *             |  UNUSED      |   |  |    |                 |
 *             |------------------|  |    |                 |
 *             |    ...       |   |  |    |                 |
 *             |------------------|  |    |-----------------|
 *             |   IGNORED    | 0 |  |    | Table Page 0| 1 |
 *             |------------------|  |    |-----------------|
 *             |  Table Page 1| 1 |--x    | IGNORED     | 0 |
 *             x------------------x       x-----------------x
 * SLADDR+4K==>
 *
 * The base input address (used by the ETR, programmed in INADDR_{LO,HI})
 * must be aligned to 1MB (the size addressable by a single page table).
 * The CATU maps INADDR{LO:HI} to the first page in the table pointed
 * to by SLADDR{LO:HI} and so on.
 *
 */
typedef u64 cate_t;

#define CATU_PAGE_SHIFT		12
#define CATU_PAGE_SIZE		(1UL << CATU_PAGE_SHIFT)
#define CATU_PAGES_PER_SYSPAGE	(PAGE_SIZE / CATU_PAGE_SIZE)

/* Page pointers are only allocated in the first 2K half */
#define CATU_PTRS_PER_PAGE	((CATU_PAGE_SIZE >> 1) / sizeof(cate_t))
#define CATU_PTRS_PER_SYSPAGE	(CATU_PAGES_PER_SYSPAGE * CATU_PTRS_PER_PAGE)
#define CATU_LINK_PREV		((CATU_PAGE_SIZE / sizeof(cate_t)) - 2)
#define CATU_LINK_NEXT		((CATU_PAGE_SIZE / sizeof(cate_t)) - 1)

#define CATU_ADDR_SHIFT		12
#define CATU_ADDR_MASK		~(((cate_t)1 << CATU_ADDR_SHIFT) - 1)
#define CATU_ENTRY_VALID	((cate_t)0x1)
#define CATU_VALID_ENTRY(addr) \
	(((cate_t)(addr) & CATU_ADDR_MASK) | CATU_ENTRY_VALID)
#define CATU_ENTRY_ADDR(entry)	((cate_t)(entry) & ~((cate_t)CATU_ENTRY_VALID))

/* CATU expects the INADDR to be aligned to 1M. */
#define CATU_DEFAULT_INADDR	(1ULL << 20)

/*
 * catu_get_table : Retrieve the table pointers for the given @offset
 * within the buffer. The buffer is wrapped around to a valid offset.
 *
 * Returns : The CPU virtual address for the beginning of the table
 * containing the data page pointer for @offset. If @daddrp is not NULL,
 * @daddrp points the DMA address of the beginning of the table.
 */
static inline cate_t *catu_get_table(struct tmc_sg_table *catu_table,
				     unsigned long offset,
				     dma_addr_t *daddrp)
{
	unsigned long buf_size = tmc_sg_table_buf_size(catu_table);
	unsigned int table_nr, pg_idx, pg_offset;
	struct tmc_pages *table_pages = &catu_table->table_pages;
	void *ptr;

	/* Make sure offset is within the range */
	offset %= buf_size;

	/*
	 * Each table can address 1MB and a single kernel page can
	 * contain "CATU_PAGES_PER_SYSPAGE" CATU tables.
	 */
	table_nr = offset >> 20;
	/* Find the table page where the table_nr lies in */
	pg_idx = table_nr / CATU_PAGES_PER_SYSPAGE;
	pg_offset = (table_nr % CATU_PAGES_PER_SYSPAGE) * CATU_PAGE_SIZE;
	if (daddrp)
		*daddrp = table_pages->daddrs[pg_idx] + pg_offset;
	ptr = page_address(table_pages->pages[pg_idx]);
	return (cate_t *)((unsigned long)ptr + pg_offset);
}

#ifdef CATU_DEBUG
static void catu_dump_table(struct tmc_sg_table *catu_table)
{
	int i;
	cate_t *table;
	unsigned long table_end, buf_size, offset = 0;

	buf_size = tmc_sg_table_buf_size(catu_table);
	dev_dbg(catu_table->dev,
		"Dump table %p, tdaddr: %llx\n",
		catu_table, catu_table->table_daddr);

	while (offset < buf_size) {
		table_end = offset + SZ_1M < buf_size ?
			    offset + SZ_1M : buf_size;
		table = catu_get_table(catu_table, offset, NULL);
		for (i = 0; offset < table_end; i++, offset += CATU_PAGE_SIZE)
			dev_dbg(catu_table->dev, "%d: %llx\n", i, table[i]);
		dev_dbg(catu_table->dev, "Prev : %llx, Next: %llx\n",
			table[CATU_LINK_PREV], table[CATU_LINK_NEXT]);
		dev_dbg(catu_table->dev, "== End of sub-table ===");
	}
	dev_dbg(catu_table->dev, "== End of Table ===");
}

#else
static inline void catu_dump_table(struct tmc_sg_table *catu_table)
{
}
#endif

static inline cate_t catu_make_entry(dma_addr_t addr)
{
	return addr ? CATU_VALID_ENTRY(addr) : 0;
}

/*
 * catu_populate_table : Populate the given CATU table.
 * The table is always populated as a circular table.
 * i.e, the "prev" link of the "first" table points to the "last"
 * table and the "next" link of the "last" table points to the
 * "first" table. The buffer should be made linear by calling
 * catu_set_table().
 */
static void
catu_populate_table(struct tmc_sg_table *catu_table)
{
	int i;
	int sys_pidx;	/* Index to current system data page */
	int catu_pidx;	/* Index of CATU page within the system data page */
	unsigned long offset, buf_size, table_end;
	dma_addr_t data_daddr;
	dma_addr_t prev_taddr, next_taddr, cur_taddr;
	cate_t *table_ptr, *next_table;

	buf_size = tmc_sg_table_buf_size(catu_table);
	sys_pidx = catu_pidx = 0;
	offset = 0;

	table_ptr = catu_get_table(catu_table, 0, &cur_taddr);
	prev_taddr = 0;	/* Prev link for the first table */

	while (offset < buf_size) {
		/*
		 * The @offset is always 1M aligned here and we have an
		 * empty table @table_ptr to fill. Each table can address
		 * upto 1MB data buffer. The last table may have fewer
		 * entries if the buffer size is not aligned.
		 */
		table_end = (offset + SZ_1M) < buf_size ?
			    (offset + SZ_1M) : buf_size;
		for (i = 0; offset < table_end;
		     i++, offset += CATU_PAGE_SIZE) {

			data_daddr = catu_table->data_pages.daddrs[sys_pidx] +
				     catu_pidx * CATU_PAGE_SIZE;
			catu_dbg(catu_table->dev,
				"[table %5ld:%03d] 0x%llx\n",
				(offset >> 20), i, data_daddr);
			table_ptr[i] = catu_make_entry(data_daddr);
			/* Move the pointers for data pages */
			catu_pidx = (catu_pidx + 1) % CATU_PAGES_PER_SYSPAGE;
			if (catu_pidx == 0)
				sys_pidx++;
		}

		/*
		 * If we have finished all the valid entries, fill the rest of
		 * the table (i.e, last table page) with invalid entries,
		 * to fail the lookups.
		 */
		if (offset == buf_size) {
			memset(&table_ptr[i], 0,
			       sizeof(cate_t) * (CATU_PTRS_PER_PAGE - i));
			next_taddr = 0;
		} else {
			next_table = catu_get_table(catu_table,
						    offset, &next_taddr);
		}

		table_ptr[CATU_LINK_PREV] = catu_make_entry(prev_taddr);
		table_ptr[CATU_LINK_NEXT] = catu_make_entry(next_taddr);

		catu_dbg(catu_table->dev,
			"[table%5ld]: Cur: 0x%llx Prev: 0x%llx, Next: 0x%llx\n",
			(offset >> 20) - 1,  cur_taddr, prev_taddr, next_taddr);

		/* Update the prev/next addresses */
		if (next_taddr) {
			prev_taddr = cur_taddr;
			cur_taddr = next_taddr;
			table_ptr = next_table;
		}
	}

	/* Sync the table for device */
	tmc_sg_table_sync_table(catu_table);
}

static struct tmc_sg_table *
catu_init_sg_table(struct device *catu_dev, int node,
		   ssize_t size, void **pages)
{
	int nr_tpages;
	struct tmc_sg_table *catu_table;

	/*
	 * Each table can address upto 1MB and we can have
	 * CATU_PAGES_PER_SYSPAGE tables in a system page.
	 */
	nr_tpages = DIV_ROUND_UP(size, SZ_1M) / CATU_PAGES_PER_SYSPAGE;
	catu_table = tmc_alloc_sg_table(catu_dev, node, nr_tpages,
					size >> PAGE_SHIFT, pages);
	if (IS_ERR(catu_table))
		return catu_table;

	catu_populate_table(catu_table);
	dev_dbg(catu_dev,
		"Setup table %p, size %ldKB, %d table pages\n",
		catu_table, (unsigned long)size >> 10,  nr_tpages);
	catu_dump_table(catu_table);
	return catu_table;
}

static void catu_free_etr_buf(struct etr_buf *etr_buf)
{
	struct catu_etr_buf *catu_buf;

	if (!etr_buf || etr_buf->mode != ETR_MODE_CATU || !etr_buf->private)
		return;

	catu_buf = etr_buf->private;
	tmc_free_sg_table(catu_buf->catu_table);
	kfree(catu_buf);
}

static ssize_t catu_get_data_etr_buf(struct etr_buf *etr_buf, u64 offset,
				     size_t len, char **bufpp)
{
	struct catu_etr_buf *catu_buf = etr_buf->private;

	return tmc_sg_table_get_data(catu_buf->catu_table, offset, len, bufpp);
}

static void catu_sync_etr_buf(struct etr_buf *etr_buf, u64 rrp, u64 rwp)
{
	struct catu_etr_buf *catu_buf = etr_buf->private;
	struct tmc_sg_table *catu_table = catu_buf->catu_table;
	u64 r_offset, w_offset;

	/*
	 * ETR started off at etr_buf->hwaddr. Convert the RRP/RWP to
	 * offsets within the trace buffer.
	 */
	r_offset = rrp - etr_buf->hwaddr;
	w_offset = rwp - etr_buf->hwaddr;

	if (!etr_buf->full) {
		etr_buf->len = w_offset - r_offset;
		if (w_offset < r_offset)
			etr_buf->len += etr_buf->size;
	} else {
		etr_buf->len = etr_buf->size;
	}

	etr_buf->offset = r_offset;
	tmc_sg_table_sync_data_range(catu_table, r_offset, etr_buf->len);
}

static int catu_alloc_etr_buf(struct tmc_drvdata *tmc_drvdata,
			      struct etr_buf *etr_buf, int node, void **pages)
{
	struct coresight_device *csdev;
	struct device *catu_dev;
	struct tmc_sg_table *catu_table;
	struct catu_etr_buf *catu_buf;

	csdev = tmc_etr_get_catu_device(tmc_drvdata);
	if (!csdev)
		return -ENODEV;
	catu_dev = csdev->dev.parent;
	catu_buf = kzalloc(sizeof(*catu_buf), GFP_KERNEL);
	if (!catu_buf)
		return -ENOMEM;

	catu_table = catu_init_sg_table(catu_dev, node, etr_buf->size, pages);
	if (IS_ERR(catu_table)) {
		kfree(catu_buf);
		return PTR_ERR(catu_table);
	}

	etr_buf->mode = ETR_MODE_CATU;
	etr_buf->private = catu_buf;
	etr_buf->hwaddr = CATU_DEFAULT_INADDR;

	catu_buf->catu_table = catu_table;
	/* Get the table base address */
	catu_buf->sladdr = catu_table->table_daddr;

	return 0;
}

const struct etr_buf_operations etr_catu_buf_ops = {
	.alloc = catu_alloc_etr_buf,
	.free = catu_free_etr_buf,
	.sync = catu_sync_etr_buf,
	.get_data = catu_get_data_etr_buf,
};

coresight_simple_reg32(struct catu_drvdata, devid, CORESIGHT_DEVID);
coresight_simple_reg32(struct catu_drvdata, control, CATU_CONTROL);
coresight_simple_reg32(struct catu_drvdata, status, CATU_STATUS);
coresight_simple_reg32(struct catu_drvdata, mode, CATU_MODE);
coresight_simple_reg32(struct catu_drvdata, axictrl, CATU_AXICTRL);
coresight_simple_reg32(struct catu_drvdata, irqen, CATU_IRQEN);
coresight_simple_reg64(struct catu_drvdata, sladdr,
		       CATU_SLADDRLO, CATU_SLADDRHI);
coresight_simple_reg64(struct catu_drvdata, inaddr,
		       CATU_INADDRLO, CATU_INADDRHI);

static struct attribute *catu_mgmt_attrs[] = {
	&dev_attr_devid.attr,
	&dev_attr_control.attr,
	&dev_attr_status.attr,
	&dev_attr_mode.attr,
	&dev_attr_axictrl.attr,
	&dev_attr_irqen.attr,
	&dev_attr_sladdr.attr,
	&dev_attr_inaddr.attr,
	NULL,
};

static const struct attribute_group catu_mgmt_group = {
	.attrs = catu_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *catu_groups[] = {
	&catu_mgmt_group,
	NULL,
};


static inline int catu_wait_for_ready(struct catu_drvdata *drvdata)
{
	return coresight_timeout(drvdata->base,
				 CATU_STATUS, CATU_STATUS_READY, 1);
}

static int catu_enable_hw(struct catu_drvdata *drvdata, void *data)
{
	int rc;
	u32 control, mode;
	struct etr_buf *etr_buf = data;

	if (catu_wait_for_ready(drvdata))
		dev_warn(drvdata->dev, "Timeout while waiting for READY\n");

	control = catu_read_control(drvdata);
	if (control & BIT(CATU_CONTROL_ENABLE)) {
		dev_warn(drvdata->dev, "CATU is already enabled\n");
		return -EBUSY;
	}

	rc = coresight_claim_device_unlocked(drvdata->base);
	if (rc)
		return rc;

	control |= BIT(CATU_CONTROL_ENABLE);

	if (etr_buf && etr_buf->mode == ETR_MODE_CATU) {
		struct catu_etr_buf *catu_buf = etr_buf->private;

		mode = CATU_MODE_TRANSLATE;
		catu_write_axictrl(drvdata, CATU_OS_AXICTRL);
		catu_write_sladdr(drvdata, catu_buf->sladdr);
		catu_write_inaddr(drvdata, CATU_DEFAULT_INADDR);
	} else {
		mode = CATU_MODE_PASS_THROUGH;
		catu_write_sladdr(drvdata, 0);
		catu_write_inaddr(drvdata, 0);
	}

	catu_write_irqen(drvdata, 0);
	catu_write_mode(drvdata, mode);
	catu_write_control(drvdata, control);
	dev_dbg(drvdata->dev, "Enabled in %s mode\n",
		(mode == CATU_MODE_PASS_THROUGH) ?
		"Pass through" :
		"Translate");
	return 0;
}

static int catu_enable(struct coresight_device *csdev, void *data)
{
	int rc;
	struct catu_drvdata *catu_drvdata = csdev_to_catu_drvdata(csdev);

	CS_UNLOCK(catu_drvdata->base);
	rc = catu_enable_hw(catu_drvdata, data);
	CS_LOCK(catu_drvdata->base);
	return rc;
}

static int catu_disable_hw(struct catu_drvdata *drvdata)
{
	int rc = 0;

	catu_write_control(drvdata, 0);
	coresight_disclaim_device_unlocked(drvdata->base);
	if (catu_wait_for_ready(drvdata)) {
		dev_info(drvdata->dev, "Timeout while waiting for READY\n");
		rc = -EAGAIN;
	}

	dev_dbg(drvdata->dev, "Disabled\n");
	return rc;
}

static int catu_disable(struct coresight_device *csdev, void *__unused)
{
	int rc;
	struct catu_drvdata *catu_drvdata = csdev_to_catu_drvdata(csdev);

	CS_UNLOCK(catu_drvdata->base);
	rc = catu_disable_hw(catu_drvdata);
	CS_LOCK(catu_drvdata->base);
	return rc;
}

static const struct coresight_ops_helper catu_helper_ops = {
	.enable = catu_enable,
	.disable = catu_disable,
};

static const struct coresight_ops catu_ops = {
	.helper_ops = &catu_helper_ops,
};

static int catu_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;
	u32 dma_mask;
	struct catu_drvdata *drvdata;
	struct coresight_desc catu_desc;
	struct coresight_platform_data *pdata = NULL;
	struct device *dev = &adev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata)) {
			ret = PTR_ERR(pdata);
			goto out;
		}
		dev->platform_data = pdata;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		ret = -ENOMEM;
		goto out;
	}

	drvdata->dev = dev;
	dev_set_drvdata(dev, drvdata);
	base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out;
	}

	/* Setup dma mask for the device */
	dma_mask = readl_relaxed(base + CORESIGHT_DEVID) & 0x3f;
	switch (dma_mask) {
	case 32:
	case 40:
	case 44:
	case 48:
	case 52:
	case 56:
	case 64:
		break;
	default:
		/* Default to the 40bits as supported by TMC-ETR */
		dma_mask = 40;
	}
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_mask));
	if (ret)
		goto out;

	drvdata->base = base;
	catu_desc.pdata = pdata;
	catu_desc.dev = dev;
	catu_desc.groups = catu_groups;
	catu_desc.type = CORESIGHT_DEV_TYPE_HELPER;
	catu_desc.subtype.helper_subtype = CORESIGHT_DEV_SUBTYPE_HELPER_CATU;
	catu_desc.ops = &catu_ops;
	drvdata->csdev = coresight_register(&catu_desc);
	if (IS_ERR(drvdata->csdev))
		ret = PTR_ERR(drvdata->csdev);
out:
	pm_runtime_put(&adev->dev);
	return ret;
}

static struct amba_id catu_ids[] = {
	{
		.id	= 0x000bb9ee,
		.mask	= 0x000fffff,
	},
	{},
};

static struct amba_driver catu_driver = {
	.drv = {
		.name			= "coresight-catu",
		.owner			= THIS_MODULE,
		.suppress_bind_attrs	= true,
	},
	.probe				= catu_probe,
	.id_table			= catu_ids,
};

builtin_amba_driver(catu_driver);
