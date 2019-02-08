/*
 * PowerPC 4xx OCM memory allocation support
 *
 * (C) Copyright 2009, Applied Micro Circuits Corporation
 * Victor Gallardo (vgallardo@amcc.com)
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/rheap.h>
#include <asm/ppc4xx_ocm.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#define OCM_DISABLED	0
#define OCM_ENABLED		1

struct ocm_block {
	struct list_head	list;
	void __iomem		*addr;
	int					size;
	const char			*owner;
};

/* non-cached or cached region */
struct ocm_region {
	phys_addr_t			phys;
	void __iomem		*virt;

	int					memtotal;
	int					memfree;

	rh_info_t			*rh;
	struct list_head	list;
};

struct ocm_info {
	int					index;
	int					status;
	int					ready;

	phys_addr_t			phys;

	int					alignment;
	int					memtotal;
	int					cache_size;

	struct ocm_region	nc;	/* non-cached region */
	struct ocm_region	c;	/* cached region */
};

static struct ocm_info *ocm_nodes;
static int ocm_count;

static struct ocm_info *ocm_get_node(unsigned int index)
{
	if (index >= ocm_count) {
		printk(KERN_ERR "PPC4XX OCM: invalid index");
		return NULL;
	}

	return &ocm_nodes[index];
}

static int ocm_free_region(struct ocm_region *ocm_reg, const void *addr)
{
	struct ocm_block *blk, *tmp;
	unsigned long offset;

	if (!ocm_reg->virt)
		return 0;

	list_for_each_entry_safe(blk, tmp, &ocm_reg->list, list) {
		if (blk->addr == addr) {
			offset = addr - ocm_reg->virt;
			ocm_reg->memfree += blk->size;
			rh_free(ocm_reg->rh, offset);
			list_del(&blk->list);
			kfree(blk);
			return 1;
		}
	}

	return 0;
}

static void __init ocm_init_node(int count, struct device_node *node)
{
	struct ocm_info *ocm;

	const unsigned int *cell_index;
	const unsigned int *cache_size;
	int len;

	struct resource rsrc;

	ocm = ocm_get_node(count);

	cell_index = of_get_property(node, "cell-index", &len);
	if (!cell_index) {
		printk(KERN_ERR "PPC4XX OCM: missing cell-index property");
		return;
	}
	ocm->index = *cell_index;

	if (of_device_is_available(node))
		ocm->status = OCM_ENABLED;

	cache_size = of_get_property(node, "cached-region-size", &len);
	if (cache_size)
		ocm->cache_size = *cache_size;

	if (of_address_to_resource(node, 0, &rsrc)) {
		printk(KERN_ERR "PPC4XX OCM%d: could not get resource address\n",
			ocm->index);
		return;
	}

	ocm->phys = rsrc.start;
	ocm->memtotal = (rsrc.end - rsrc.start + 1);

	printk(KERN_INFO "PPC4XX OCM%d: %d Bytes (%s)\n",
		ocm->index, ocm->memtotal,
		(ocm->status == OCM_DISABLED) ? "disabled" : "enabled");

	if (ocm->status == OCM_DISABLED)
		return;

	/* request region */

	if (!request_mem_region(ocm->phys, ocm->memtotal, "ppc4xx_ocm")) {
		printk(KERN_ERR "PPC4XX OCM%d: could not request region\n",
			ocm->index);
		return;
	}

	/* Configure non-cached and cached regions */

	ocm->nc.phys = ocm->phys;
	ocm->nc.memtotal = ocm->memtotal - ocm->cache_size;
	ocm->nc.memfree = ocm->nc.memtotal;

	ocm->c.phys = ocm->phys + ocm->nc.memtotal;
	ocm->c.memtotal = ocm->cache_size;
	ocm->c.memfree = ocm->c.memtotal;

	if (ocm->nc.memtotal == 0)
		ocm->nc.phys = 0;

	if (ocm->c.memtotal == 0)
		ocm->c.phys = 0;

	printk(KERN_INFO "PPC4XX OCM%d: %d Bytes (non-cached)\n",
		ocm->index, ocm->nc.memtotal);

	printk(KERN_INFO "PPC4XX OCM%d: %d Bytes (cached)\n",
		ocm->index, ocm->c.memtotal);

	/* ioremap the non-cached region */
	if (ocm->nc.memtotal) {
		ocm->nc.virt = __ioremap(ocm->nc.phys, ocm->nc.memtotal,
			_PAGE_EXEC | pgprot_val(PAGE_KERNEL_NCG));

		if (!ocm->nc.virt) {
			printk(KERN_ERR
			       "PPC4XX OCM%d: failed to ioremap non-cached memory\n",
			       ocm->index);
			ocm->nc.memfree = 0;
			return;
		}
	}

	/* ioremap the cached region */

	if (ocm->c.memtotal) {
		ocm->c.virt = __ioremap(ocm->c.phys, ocm->c.memtotal,
					_PAGE_EXEC | pgprot_val(PAGE_KERNEL));

		if (!ocm->c.virt) {
			printk(KERN_ERR
			       "PPC4XX OCM%d: failed to ioremap cached memory\n",
			       ocm->index);
			ocm->c.memfree = 0;
			return;
		}
	}

	/* Create Remote Heaps */

	ocm->alignment = 4; /* default 4 byte alignment */

	if (ocm->nc.virt) {
		ocm->nc.rh = rh_create(ocm->alignment);
		rh_attach_region(ocm->nc.rh, 0, ocm->nc.memtotal);
	}

	if (ocm->c.virt) {
		ocm->c.rh = rh_create(ocm->alignment);
		rh_attach_region(ocm->c.rh, 0, ocm->c.memtotal);
	}

	INIT_LIST_HEAD(&ocm->nc.list);
	INIT_LIST_HEAD(&ocm->c.list);

	ocm->ready = 1;
}

static int ocm_debugfs_show(struct seq_file *m, void *v)
{
	struct ocm_block *blk, *tmp;
	unsigned int i;

	for (i = 0; i < ocm_count; i++) {
		struct ocm_info *ocm = ocm_get_node(i);

		if (!ocm || !ocm->ready)
			continue;

		seq_printf(m, "PPC4XX OCM   : %d\n", ocm->index);
		seq_printf(m, "PhysAddr     : %pa\n", &(ocm->phys));
		seq_printf(m, "MemTotal     : %d Bytes\n", ocm->memtotal);
		seq_printf(m, "MemTotal(NC) : %d Bytes\n", ocm->nc.memtotal);
		seq_printf(m, "MemTotal(C)  : %d Bytes\n\n", ocm->c.memtotal);

		seq_printf(m, "NC.PhysAddr  : %pa\n", &(ocm->nc.phys));
		seq_printf(m, "NC.VirtAddr  : 0x%p\n", ocm->nc.virt);
		seq_printf(m, "NC.MemTotal  : %d Bytes\n", ocm->nc.memtotal);
		seq_printf(m, "NC.MemFree   : %d Bytes\n", ocm->nc.memfree);

		list_for_each_entry_safe(blk, tmp, &ocm->nc.list, list) {
			seq_printf(m, "NC.MemUsed   : %d Bytes (%s)\n",
							blk->size, blk->owner);
		}

		seq_printf(m, "\nC.PhysAddr   : %pa\n", &(ocm->c.phys));
		seq_printf(m, "C.VirtAddr   : 0x%p\n", ocm->c.virt);
		seq_printf(m, "C.MemTotal   : %d Bytes\n", ocm->c.memtotal);
		seq_printf(m, "C.MemFree    : %d Bytes\n", ocm->c.memfree);

		list_for_each_entry_safe(blk, tmp, &ocm->c.list, list) {
			seq_printf(m, "C.MemUsed    : %d Bytes (%s)\n",
						blk->size, blk->owner);
		}

		seq_putc(m, '\n');
	}

	return 0;
}

static int ocm_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, ocm_debugfs_show, NULL);
}

static const struct file_operations ocm_debugfs_fops = {
	.open = ocm_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ocm_debugfs_init(void)
{
	struct dentry *junk;

	junk = debugfs_create_dir("ppc4xx_ocm", 0);
	if (!junk) {
		printk(KERN_ALERT "debugfs ppc4xx ocm: failed to create dir\n");
		return -1;
	}

	if (debugfs_create_file("info", 0644, junk, NULL, &ocm_debugfs_fops)) {
		printk(KERN_ALERT "debugfs ppc4xx ocm: failed to create file\n");
		return -1;
	}

	return 0;
}

void *ppc4xx_ocm_alloc(phys_addr_t *phys, int size, int align,
			int flags, const char *owner)
{
	void __iomem *addr = NULL;
	unsigned long offset;
	struct ocm_info *ocm;
	struct ocm_region *ocm_reg;
	struct ocm_block *ocm_blk;
	int i;

	for (i = 0; i < ocm_count; i++) {
		ocm = ocm_get_node(i);

		if (!ocm || !ocm->ready)
			continue;

		if (flags == PPC4XX_OCM_NON_CACHED)
			ocm_reg = &ocm->nc;
		else
			ocm_reg = &ocm->c;

		if (!ocm_reg->virt)
			continue;

		if (align < ocm->alignment)
			align = ocm->alignment;

		offset = rh_alloc_align(ocm_reg->rh, size, align, NULL);

		if (IS_ERR_VALUE(offset))
			continue;

		ocm_blk = kzalloc(sizeof(*ocm_blk), GFP_KERNEL);
		if (!ocm_blk) {
			rh_free(ocm_reg->rh, offset);
			break;
		}

		*phys = ocm_reg->phys + offset;
		addr = ocm_reg->virt + offset;
		size = ALIGN(size, align);

		ocm_blk->addr = addr;
		ocm_blk->size = size;
		ocm_blk->owner = owner;
		list_add_tail(&ocm_blk->list, &ocm_reg->list);

		ocm_reg->memfree -= size;

		break;
	}

	return addr;
}

void ppc4xx_ocm_free(const void *addr)
{
	int i;

	if (!addr)
		return;

	for (i = 0; i < ocm_count; i++) {
		struct ocm_info *ocm = ocm_get_node(i);

		if (!ocm || !ocm->ready)
			continue;

		if (ocm_free_region(&ocm->nc, addr) ||
			ocm_free_region(&ocm->c, addr))
			return;
	}
}

static int __init ppc4xx_ocm_init(void)
{
	struct device_node *np;
	int count;

	count = 0;
	for_each_compatible_node(np, NULL, "ibm,ocm")
		count++;

	if (!count)
		return 0;

	ocm_nodes = kzalloc((count * sizeof(struct ocm_info)), GFP_KERNEL);
	if (!ocm_nodes)
		return -ENOMEM;

	ocm_count = count;
	count = 0;

	for_each_compatible_node(np, NULL, "ibm,ocm") {
		ocm_init_node(count, np);
		count++;
	}

	ocm_debugfs_init();

	return 0;
}

arch_initcall(ppc4xx_ocm_init);
