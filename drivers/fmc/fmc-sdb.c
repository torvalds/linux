/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fmc.h>
#include <linux/sdb.h>
#include <linux/err.h>
#include <linux/fmc-sdb.h>
#include <asm/byteorder.h>

static uint32_t __sdb_rd(struct fmc_device *fmc, unsigned long address,
			int convert)
{
	uint32_t res = fmc_readl(fmc, address);
	if (convert)
		return __be32_to_cpu(res);
	return res;
}

static struct sdb_array *__fmc_scan_sdb_tree(struct fmc_device *fmc,
					     unsigned long sdb_addr,
					     unsigned long reg_base, int level)
{
	uint32_t onew;
	int i, j, n, convert = 0;
	struct sdb_array *arr, *sub;

	onew = fmc_readl(fmc, sdb_addr);
	if (onew == SDB_MAGIC) {
		/* Uh! If we are little-endian, we must convert */
		if (SDB_MAGIC != __be32_to_cpu(SDB_MAGIC))
			convert = 1;
	} else if (onew == __be32_to_cpu(SDB_MAGIC)) {
		/* ok, don't convert */
	} else {
		return ERR_PTR(-ENOENT);
	}
	/* So, the magic was there: get the count from offset 4*/
	onew = __sdb_rd(fmc, sdb_addr + 4, convert);
	n = __be16_to_cpu(*(uint16_t *)&onew);
	arr = kzalloc(sizeof(*arr), GFP_KERNEL);
	if (!arr)
		return ERR_PTR(-ENOMEM);
	arr->record = kcalloc(n, sizeof(arr->record[0]), GFP_KERNEL);
	arr->subtree = kcalloc(n, sizeof(arr->subtree[0]), GFP_KERNEL);
	if (!arr->record || !arr->subtree) {
		kfree(arr->record);
		kfree(arr->subtree);
		kfree(arr);
		return ERR_PTR(-ENOMEM);
	}

	arr->len = n;
	arr->level = level;
	arr->fmc = fmc;
	for (i = 0; i < n; i++) {
		union  sdb_record *r;

		for (j = 0; j < sizeof(arr->record[0]); j += 4) {
			*(uint32_t *)((void *)(arr->record + i) + j) =
				__sdb_rd(fmc, sdb_addr + (i * 64) + j, convert);
		}
		r = &arr->record[i];
		arr->subtree[i] = ERR_PTR(-ENODEV);
		if (r->empty.record_type == sdb_type_bridge) {
			struct sdb_component *c = &r->bridge.sdb_component;
			uint64_t subaddr = __be64_to_cpu(r->bridge.sdb_child);
			uint64_t newbase = __be64_to_cpu(c->addr_first);

			subaddr += reg_base;
			newbase += reg_base;
			sub = __fmc_scan_sdb_tree(fmc, subaddr, newbase,
						  level + 1);
			arr->subtree[i] = sub; /* may be error */
			if (IS_ERR(sub))
				continue;
			sub->parent = arr;
			sub->baseaddr = newbase;
		}
	}
	return arr;
}

int fmc_scan_sdb_tree(struct fmc_device *fmc, unsigned long address)
{
	struct sdb_array *ret;
	if (fmc->sdb)
		return -EBUSY;
	ret = __fmc_scan_sdb_tree(fmc, address, 0 /* regs */, 0);
	if (IS_ERR(ret))
		return PTR_ERR(ret);
	fmc->sdb = ret;
	return 0;
}
EXPORT_SYMBOL(fmc_scan_sdb_tree);

static void __fmc_sdb_free(struct sdb_array *arr)
{
	int i, n;

	if (!arr)
		return;
	n = arr->len;
	for (i = 0; i < n; i++) {
		if (IS_ERR(arr->subtree[i]))
			continue;
		__fmc_sdb_free(arr->subtree[i]);
	}
	kfree(arr->record);
	kfree(arr->subtree);
	kfree(arr);
}

int fmc_free_sdb_tree(struct fmc_device *fmc)
{
	__fmc_sdb_free(fmc->sdb);
	fmc->sdb = NULL;
	return 0;
}
EXPORT_SYMBOL(fmc_free_sdb_tree);

/* This helper calls reprogram and inizialized sdb as well */
int fmc_reprogram_raw(struct fmc_device *fmc, struct fmc_driver *d,
		      void *gw, unsigned long len, int sdb_entry)
{
	int ret;

	ret = fmc->op->reprogram_raw(fmc, d, gw, len);
	if (ret < 0)
		return ret;
	if (sdb_entry < 0)
		return ret;

	/* We are required to find SDB at a given offset */
	ret = fmc_scan_sdb_tree(fmc, sdb_entry);
	if (ret < 0) {
		dev_err(&fmc->dev, "Can't find SDB at address 0x%x\n",
			sdb_entry);
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(fmc_reprogram_raw);

/* This helper calls reprogram and inizialized sdb as well */
int fmc_reprogram(struct fmc_device *fmc, struct fmc_driver *d, char *gw,
			 int sdb_entry)
{
	int ret;

	ret = fmc->op->reprogram(fmc, d, gw);
	if (ret < 0)
		return ret;
	if (sdb_entry < 0)
		return ret;

	/* We are required to find SDB at a given offset */
	ret = fmc_scan_sdb_tree(fmc, sdb_entry);
	if (ret < 0) {
		dev_err(&fmc->dev, "Can't find SDB at address 0x%x\n",
			sdb_entry);
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(fmc_reprogram);

void fmc_show_sdb_tree(const struct fmc_device *fmc)
{
	pr_err("%s: not supported anymore, use debugfs to dump SDB\n",
		__func__);
}
EXPORT_SYMBOL(fmc_show_sdb_tree);

signed long fmc_find_sdb_device(struct sdb_array *tree,
				uint64_t vid, uint32_t did, unsigned long *sz)
{
	signed long res = -ENODEV;
	union  sdb_record *r;
	struct sdb_product *p;
	struct sdb_component *c;
	int i, n = tree->len;
	uint64_t last, first;

	/* FIXME: what if the first interconnect is not at zero? */
	for (i = 0; i < n; i++) {
		r = &tree->record[i];
		c = &r->dev.sdb_component;
		p = &c->product;

		if (!IS_ERR(tree->subtree[i]))
			res = fmc_find_sdb_device(tree->subtree[i],
						  vid, did, sz);
		if (res >= 0)
			return res + tree->baseaddr;
		if (r->empty.record_type != sdb_type_device)
			continue;
		if (__be64_to_cpu(p->vendor_id) != vid)
			continue;
		if (__be32_to_cpu(p->device_id) != did)
			continue;
		/* found */
		last = __be64_to_cpu(c->addr_last);
		first = __be64_to_cpu(c->addr_first);
		if (sz)
			*sz = (typeof(*sz))(last + 1 - first);
		return first + tree->baseaddr;
	}
	return res;
}
EXPORT_SYMBOL(fmc_find_sdb_device);
