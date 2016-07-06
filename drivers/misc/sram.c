/*
 * Generic on-chip SRAM allocation driver
 *
 * Copyright (C) 2012 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/list_sort.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define SRAM_GRANULARITY	32

struct sram_partition {
	void __iomem *base;

	struct gen_pool *pool;
	struct bin_attribute battr;
	struct mutex lock;
};

struct sram_dev {
	struct device *dev;
	void __iomem *virt_base;

	struct gen_pool *pool;
	struct clk *clk;

	struct sram_partition *partition;
	u32 partitions;
};

struct sram_reserve {
	struct list_head list;
	u32 start;
	u32 size;
	bool export;
	bool pool;
	const char *label;
};

static ssize_t sram_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr,
			 char *buf, loff_t pos, size_t count)
{
	struct sram_partition *part;

	part = container_of(attr, struct sram_partition, battr);

	mutex_lock(&part->lock);
	memcpy_fromio(buf, part->base + pos, count);
	mutex_unlock(&part->lock);

	return count;
}

static ssize_t sram_write(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *attr,
			  char *buf, loff_t pos, size_t count)
{
	struct sram_partition *part;

	part = container_of(attr, struct sram_partition, battr);

	mutex_lock(&part->lock);
	memcpy_toio(part->base + pos, buf, count);
	mutex_unlock(&part->lock);

	return count;
}

static int sram_add_pool(struct sram_dev *sram, struct sram_reserve *block,
			 phys_addr_t start, struct sram_partition *part)
{
	int ret;

	part->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
					  NUMA_NO_NODE, block->label);
	if (IS_ERR(part->pool))
		return PTR_ERR(part->pool);

	ret = gen_pool_add_virt(part->pool, (unsigned long)part->base, start,
				block->size, NUMA_NO_NODE);
	if (ret < 0) {
		dev_err(sram->dev, "failed to register subpool: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sram_add_export(struct sram_dev *sram, struct sram_reserve *block,
			   phys_addr_t start, struct sram_partition *part)
{
	sysfs_bin_attr_init(&part->battr);
	part->battr.attr.name = devm_kasprintf(sram->dev, GFP_KERNEL,
					       "%llx.sram",
					       (unsigned long long)start);
	if (!part->battr.attr.name)
		return -ENOMEM;

	part->battr.attr.mode = S_IRUSR | S_IWUSR;
	part->battr.read = sram_read;
	part->battr.write = sram_write;
	part->battr.size = block->size;

	return device_create_bin_file(sram->dev, &part->battr);
}

static int sram_add_partition(struct sram_dev *sram, struct sram_reserve *block,
			      phys_addr_t start)
{
	int ret;
	struct sram_partition *part = &sram->partition[sram->partitions];

	mutex_init(&part->lock);
	part->base = sram->virt_base + block->start;

	if (block->pool) {
		ret = sram_add_pool(sram, block, start, part);
		if (ret)
			return ret;
	}
	if (block->export) {
		ret = sram_add_export(sram, block, start, part);
		if (ret)
			return ret;
	}
	sram->partitions++;

	return 0;
}

static void sram_free_partitions(struct sram_dev *sram)
{
	struct sram_partition *part;

	if (!sram->partitions)
		return;

	part = &sram->partition[sram->partitions - 1];
	for (; sram->partitions; sram->partitions--, part--) {
		if (part->battr.size)
			device_remove_bin_file(sram->dev, &part->battr);

		if (part->pool &&
		    gen_pool_avail(part->pool) < gen_pool_size(part->pool))
			dev_err(sram->dev, "removed pool while SRAM allocated\n");
	}
}

static int sram_reserve_cmp(void *priv, struct list_head *a,
					struct list_head *b)
{
	struct sram_reserve *ra = list_entry(a, struct sram_reserve, list);
	struct sram_reserve *rb = list_entry(b, struct sram_reserve, list);

	return ra->start - rb->start;
}

static int sram_reserve_regions(struct sram_dev *sram, struct resource *res)
{
	struct device_node *np = sram->dev->of_node, *child;
	unsigned long size, cur_start, cur_size;
	struct sram_reserve *rblocks, *block;
	struct list_head reserve_list;
	unsigned int nblocks, exports = 0;
	const char *label;
	int ret = 0;

	INIT_LIST_HEAD(&reserve_list);

	size = resource_size(res);

	/*
	 * We need an additional block to mark the end of the memory region
	 * after the reserved blocks from the dt are processed.
	 */
	nblocks = (np) ? of_get_available_child_count(np) + 1 : 1;
	rblocks = kzalloc((nblocks) * sizeof(*rblocks), GFP_KERNEL);
	if (!rblocks)
		return -ENOMEM;

	block = &rblocks[0];
	for_each_available_child_of_node(np, child) {
		struct resource child_res;

		ret = of_address_to_resource(child, 0, &child_res);
		if (ret < 0) {
			dev_err(sram->dev,
				"could not get address for node %s\n",
				child->full_name);
			goto err_chunks;
		}

		if (child_res.start < res->start || child_res.end > res->end) {
			dev_err(sram->dev,
				"reserved block %s outside the sram area\n",
				child->full_name);
			ret = -EINVAL;
			goto err_chunks;
		}

		block->start = child_res.start - res->start;
		block->size = resource_size(&child_res);
		list_add_tail(&block->list, &reserve_list);

		if (of_find_property(child, "export", NULL))
			block->export = true;

		if (of_find_property(child, "pool", NULL))
			block->pool = true;

		if ((block->export || block->pool) && block->size) {
			exports++;

			label = NULL;
			ret = of_property_read_string(child, "label", &label);
			if (ret && ret != -EINVAL) {
				dev_err(sram->dev,
					"%s has invalid label name\n",
					child->full_name);
				goto err_chunks;
			}
			if (!label)
				label = child->name;

			block->label = devm_kstrdup(sram->dev,
						    label, GFP_KERNEL);
			if (!block->label)
				goto err_chunks;

			dev_dbg(sram->dev, "found %sblock '%s' 0x%x-0x%x\n",
				block->export ? "exported " : "", block->label,
				block->start, block->start + block->size);
		} else {
			dev_dbg(sram->dev, "found reserved block 0x%x-0x%x\n",
				block->start, block->start + block->size);
		}

		block++;
	}
	child = NULL;

	/* the last chunk marks the end of the region */
	rblocks[nblocks - 1].start = size;
	rblocks[nblocks - 1].size = 0;
	list_add_tail(&rblocks[nblocks - 1].list, &reserve_list);

	list_sort(NULL, &reserve_list, sram_reserve_cmp);

	if (exports) {
		sram->partition = devm_kzalloc(sram->dev,
				       exports * sizeof(*sram->partition),
				       GFP_KERNEL);
		if (!sram->partition) {
			ret = -ENOMEM;
			goto err_chunks;
		}
	}

	cur_start = 0;
	list_for_each_entry(block, &reserve_list, list) {
		/* can only happen if sections overlap */
		if (block->start < cur_start) {
			dev_err(sram->dev,
				"block at 0x%x starts after current offset 0x%lx\n",
				block->start, cur_start);
			ret = -EINVAL;
			sram_free_partitions(sram);
			goto err_chunks;
		}

		if ((block->export || block->pool) && block->size) {
			ret = sram_add_partition(sram, block,
						 res->start + block->start);
			if (ret) {
				sram_free_partitions(sram);
				goto err_chunks;
			}
		}

		/* current start is in a reserved block, so continue after it */
		if (block->start == cur_start) {
			cur_start = block->start + block->size;
			continue;
		}

		/*
		 * allocate the space between the current starting
		 * address and the following reserved block, or the
		 * end of the region.
		 */
		cur_size = block->start - cur_start;

		dev_dbg(sram->dev, "adding chunk 0x%lx-0x%lx\n",
			cur_start, cur_start + cur_size);

		ret = gen_pool_add_virt(sram->pool,
				(unsigned long)sram->virt_base + cur_start,
				res->start + cur_start, cur_size, -1);
		if (ret < 0) {
			sram_free_partitions(sram);
			goto err_chunks;
		}

		/* next allocation after this reserved block */
		cur_start = block->start + block->size;
	}

 err_chunks:
	if (child)
		of_node_put(child);

	kfree(rblocks);

	return ret;
}

static int sram_probe(struct platform_device *pdev)
{
	struct sram_dev *sram;
	struct resource *res;
	size_t size;
	int ret;

	sram = devm_kzalloc(&pdev->dev, sizeof(*sram), GFP_KERNEL);
	if (!sram)
		return -ENOMEM;

	sram->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(sram->dev, "found no memory resource\n");
		return -EINVAL;
	}

	size = resource_size(res);

	if (!devm_request_mem_region(sram->dev, res->start, size, pdev->name)) {
		dev_err(sram->dev, "could not request region for resource\n");
		return -EBUSY;
	}

	if (of_property_read_bool(pdev->dev.of_node, "no-memory-wc"))
		sram->virt_base = devm_ioremap(sram->dev, res->start, size);
	else
		sram->virt_base = devm_ioremap_wc(sram->dev, res->start, size);
	if (!sram->virt_base)
		return -ENOMEM;

	sram->pool = devm_gen_pool_create(sram->dev, ilog2(SRAM_GRANULARITY),
					  NUMA_NO_NODE, NULL);
	if (IS_ERR(sram->pool))
		return PTR_ERR(sram->pool);

	ret = sram_reserve_regions(sram, res);
	if (ret)
		return ret;

	sram->clk = devm_clk_get(sram->dev, NULL);
	if (IS_ERR(sram->clk))
		sram->clk = NULL;
	else
		clk_prepare_enable(sram->clk);

	platform_set_drvdata(pdev, sram);

	dev_dbg(sram->dev, "SRAM pool: %zu KiB @ 0x%p\n",
		gen_pool_size(sram->pool) / 1024, sram->virt_base);

	return 0;
}

static int sram_remove(struct platform_device *pdev)
{
	struct sram_dev *sram = platform_get_drvdata(pdev);

	sram_free_partitions(sram);

	if (gen_pool_avail(sram->pool) < gen_pool_size(sram->pool))
		dev_err(sram->dev, "removed while SRAM allocated\n");

	if (sram->clk)
		clk_disable_unprepare(sram->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sram_dt_ids[] = {
	{ .compatible = "mmio-sram" },
	{}
};
#endif

static struct platform_driver sram_driver = {
	.driver = {
		.name = "sram",
		.of_match_table = of_match_ptr(sram_dt_ids),
	},
	.probe = sram_probe,
	.remove = sram_remove,
};

static int __init sram_init(void)
{
	return platform_driver_register(&sram_driver);
}

postcore_initcall(sram_init);
