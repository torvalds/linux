/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)		"uniphier: " fmt

#include <linux/init.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/hardware/cache-uniphier.h>
#include <asm/outercache.h>

/* control registers */
#define UNIPHIER_SSCC		0x0	/* Control Register */
#define    UNIPHIER_SSCC_BST			BIT(20)	/* UCWG burst read */
#define    UNIPHIER_SSCC_ACT			BIT(19)	/* Inst-Data separate */
#define    UNIPHIER_SSCC_WTG			BIT(18)	/* WT gathering on */
#define    UNIPHIER_SSCC_PRD			BIT(17)	/* enable pre-fetch */
#define    UNIPHIER_SSCC_ON			BIT(0)	/* enable cache */
#define UNIPHIER_SSCLPDAWCR	0x30	/* Unified/Data Active Way Control */
#define UNIPHIER_SSCLPIAWCR	0x34	/* Instruction Active Way Control */

/* revision registers */
#define UNIPHIER_SSCID		0x0	/* ID Register */

/* operation registers */
#define UNIPHIER_SSCOPE		0x244	/* Cache Operation Primitive Entry */
#define    UNIPHIER_SSCOPE_CM_INV		0x0	/* invalidate */
#define    UNIPHIER_SSCOPE_CM_CLEAN		0x1	/* clean */
#define    UNIPHIER_SSCOPE_CM_FLUSH		0x2	/* flush */
#define    UNIPHIER_SSCOPE_CM_SYNC		0x8	/* sync (drain bufs) */
#define    UNIPHIER_SSCOPE_CM_FLUSH_PREFETCH	0x9	/* flush p-fetch buf */
#define UNIPHIER_SSCOQM		0x248	/* Cache Operation Queue Mode */
#define    UNIPHIER_SSCOQM_TID_MASK		(0x3 << 21)
#define    UNIPHIER_SSCOQM_TID_LRU_DATA		(0x0 << 21)
#define    UNIPHIER_SSCOQM_TID_LRU_INST		(0x1 << 21)
#define    UNIPHIER_SSCOQM_TID_WAY		(0x2 << 21)
#define    UNIPHIER_SSCOQM_S_MASK		(0x3 << 17)
#define    UNIPHIER_SSCOQM_S_RANGE		(0x0 << 17)
#define    UNIPHIER_SSCOQM_S_ALL		(0x1 << 17)
#define    UNIPHIER_SSCOQM_S_WAY		(0x2 << 17)
#define    UNIPHIER_SSCOQM_CE			BIT(15)	/* notify completion */
#define    UNIPHIER_SSCOQM_CM_INV		0x0	/* invalidate */
#define    UNIPHIER_SSCOQM_CM_CLEAN		0x1	/* clean */
#define    UNIPHIER_SSCOQM_CM_FLUSH		0x2	/* flush */
#define    UNIPHIER_SSCOQM_CM_PREFETCH		0x3	/* prefetch to cache */
#define    UNIPHIER_SSCOQM_CM_PREFETCH_BUF	0x4	/* prefetch to pf-buf */
#define    UNIPHIER_SSCOQM_CM_TOUCH		0x5	/* touch */
#define    UNIPHIER_SSCOQM_CM_TOUCH_ZERO	0x6	/* touch to zero */
#define    UNIPHIER_SSCOQM_CM_TOUCH_DIRTY	0x7	/* touch with dirty */
#define UNIPHIER_SSCOQAD	0x24c	/* Cache Operation Queue Address */
#define UNIPHIER_SSCOQSZ	0x250	/* Cache Operation Queue Size */
#define UNIPHIER_SSCOQMASK	0x254	/* Cache Operation Queue Address Mask */
#define UNIPHIER_SSCOQWN	0x258	/* Cache Operation Queue Way Number */
#define UNIPHIER_SSCOPPQSEF	0x25c	/* Cache Operation Queue Set Complete*/
#define    UNIPHIER_SSCOPPQSEF_FE		BIT(1)
#define    UNIPHIER_SSCOPPQSEF_OE		BIT(0)
#define UNIPHIER_SSCOLPQS	0x260	/* Cache Operation Queue Status */
#define    UNIPHIER_SSCOLPQS_EF			BIT(2)
#define    UNIPHIER_SSCOLPQS_EST		BIT(1)
#define    UNIPHIER_SSCOLPQS_QST		BIT(0)

/* Is the touch/pre-fetch destination specified by ways? */
#define UNIPHIER_SSCOQM_TID_IS_WAY(op) \
		((op & UNIPHIER_SSCOQM_TID_MASK) == UNIPHIER_SSCOQM_TID_WAY)
/* Is the operation region specified by address range? */
#define UNIPHIER_SSCOQM_S_IS_RANGE(op) \
		((op & UNIPHIER_SSCOQM_S_MASK) == UNIPHIER_SSCOQM_S_RANGE)

/**
 * uniphier_cache_data - UniPhier outer cache specific data
 *
 * @ctrl_base: virtual base address of control registers
 * @rev_base: virtual base address of revision registers
 * @op_base: virtual base address of operation registers
 * @way_present_mask: each bit specifies if the way is present
 * @way_locked_mask: each bit specifies if the way is locked
 * @nsets: number of associativity sets
 * @line_size: line size in bytes
 * @range_op_max_size: max size that can be handled by a single range operation
 * @list: list node to include this level in the whole cache hierarchy
 */
struct uniphier_cache_data {
	void __iomem *ctrl_base;
	void __iomem *rev_base;
	void __iomem *op_base;
	u32 way_present_mask;
	u32 way_locked_mask;
	u32 nsets;
	u32 line_size;
	u32 range_op_max_size;
	struct list_head list;
};

/*
 * List of the whole outer cache hierarchy.  This list is only modified during
 * the early boot stage, so no mutex is taken for the access to the list.
 */
static LIST_HEAD(uniphier_cache_list);

/**
 * __uniphier_cache_sync - perform a sync point for a particular cache level
 *
 * @data: cache controller specific data
 */
static void __uniphier_cache_sync(struct uniphier_cache_data *data)
{
	/* This sequence need not be atomic.  Do not disable IRQ. */
	writel_relaxed(UNIPHIER_SSCOPE_CM_SYNC,
		       data->op_base + UNIPHIER_SSCOPE);
	/* need a read back to confirm */
	readl_relaxed(data->op_base + UNIPHIER_SSCOPE);
}

/**
 * __uniphier_cache_maint_common - run a queue operation for a particular level
 *
 * @data: cache controller specific data
 * @start: start address of range operation (don't care for "all" operation)
 * @size: data size of range operation (don't care for "all" operation)
 * @operation: flags to specify the desired cache operation
 */
static void __uniphier_cache_maint_common(struct uniphier_cache_data *data,
					  unsigned long start,
					  unsigned long size,
					  u32 operation)
{
	unsigned long flags;

	/*
	 * No spin lock is necessary here because:
	 *
	 * [1] This outer cache controller is able to accept maintenance
	 * operations from multiple CPUs at a time in an SMP system; if a
	 * maintenance operation is under way and another operation is issued,
	 * the new one is stored in the queue.  The controller performs one
	 * operation after another.  If the queue is full, the status register,
	 * UNIPHIER_SSCOPPQSEF, indicates that the queue registration has
	 * failed.  The status registers, UNIPHIER_{SSCOPPQSEF, SSCOLPQS}, have
	 * different instances for each CPU, i.e. each CPU can track the status
	 * of the maintenance operations triggered by itself.
	 *
	 * [2] The cache command registers, UNIPHIER_{SSCOQM, SSCOQAD, SSCOQSZ,
	 * SSCOQWN}, are shared between multiple CPUs, but the hardware still
	 * guarantees the registration sequence is atomic; the write access to
	 * them are arbitrated by the hardware.  The first accessor to the
	 * register, UNIPHIER_SSCOQM, holds the access right and it is released
	 * by reading the status register, UNIPHIER_SSCOPPQSEF.  While one CPU
	 * is holding the access right, other CPUs fail to register operations.
	 * One CPU should not hold the access right for a long time, so local
	 * IRQs should be disabled while the following sequence.
	 */
	local_irq_save(flags);

	/* clear the complete notification flag */
	writel_relaxed(UNIPHIER_SSCOLPQS_EF, data->op_base + UNIPHIER_SSCOLPQS);

	do {
		/* set cache operation */
		writel_relaxed(UNIPHIER_SSCOQM_CE | operation,
			       data->op_base + UNIPHIER_SSCOQM);

		/* set address range if needed */
		if (likely(UNIPHIER_SSCOQM_S_IS_RANGE(operation))) {
			writel_relaxed(start, data->op_base + UNIPHIER_SSCOQAD);
			writel_relaxed(size, data->op_base + UNIPHIER_SSCOQSZ);
		}

		/* set target ways if needed */
		if (unlikely(UNIPHIER_SSCOQM_TID_IS_WAY(operation)))
			writel_relaxed(data->way_locked_mask,
				       data->op_base + UNIPHIER_SSCOQWN);
	} while (unlikely(readl_relaxed(data->op_base + UNIPHIER_SSCOPPQSEF) &
			  (UNIPHIER_SSCOPPQSEF_FE | UNIPHIER_SSCOPPQSEF_OE)));

	/* wait until the operation is completed */
	while (likely(readl_relaxed(data->op_base + UNIPHIER_SSCOLPQS) !=
		      UNIPHIER_SSCOLPQS_EF))
		cpu_relax();

	local_irq_restore(flags);
}

static void __uniphier_cache_maint_all(struct uniphier_cache_data *data,
				       u32 operation)
{
	__uniphier_cache_maint_common(data, 0, 0,
				      UNIPHIER_SSCOQM_S_ALL | operation);

	__uniphier_cache_sync(data);
}

static void __uniphier_cache_maint_range(struct uniphier_cache_data *data,
					 unsigned long start, unsigned long end,
					 u32 operation)
{
	unsigned long size;

	/*
	 * If the start address is not aligned,
	 * perform a cache operation for the first cache-line
	 */
	start = start & ~(data->line_size - 1);

	size = end - start;

	if (unlikely(size >= (unsigned long)(-data->line_size))) {
		/* this means cache operation for all range */
		__uniphier_cache_maint_all(data, operation);
		return;
	}

	/*
	 * If the end address is not aligned,
	 * perform a cache operation for the last cache-line
	 */
	size = ALIGN(size, data->line_size);

	while (size) {
		unsigned long chunk_size = min_t(unsigned long, size,
						 data->range_op_max_size);

		__uniphier_cache_maint_common(data, start, chunk_size,
					UNIPHIER_SSCOQM_S_RANGE | operation);

		start += chunk_size;
		size -= chunk_size;
	}

	__uniphier_cache_sync(data);
}

static void __uniphier_cache_enable(struct uniphier_cache_data *data, bool on)
{
	u32 val = 0;

	if (on)
		val = UNIPHIER_SSCC_WTG | UNIPHIER_SSCC_PRD | UNIPHIER_SSCC_ON;

	writel_relaxed(val, data->ctrl_base + UNIPHIER_SSCC);
}

static void __init __uniphier_cache_set_locked_ways(
					struct uniphier_cache_data *data,
					u32 way_mask)
{
	data->way_locked_mask = way_mask & data->way_present_mask;

	writel_relaxed(~data->way_locked_mask & data->way_present_mask,
		       data->ctrl_base + UNIPHIER_SSCLPDAWCR);
}

static void uniphier_cache_maint_range(unsigned long start, unsigned long end,
				       u32 operation)
{
	struct uniphier_cache_data *data;

	list_for_each_entry(data, &uniphier_cache_list, list)
		__uniphier_cache_maint_range(data, start, end, operation);
}

static void uniphier_cache_maint_all(u32 operation)
{
	struct uniphier_cache_data *data;

	list_for_each_entry(data, &uniphier_cache_list, list)
		__uniphier_cache_maint_all(data, operation);
}

static void uniphier_cache_inv_range(unsigned long start, unsigned long end)
{
	uniphier_cache_maint_range(start, end, UNIPHIER_SSCOQM_CM_INV);
}

static void uniphier_cache_clean_range(unsigned long start, unsigned long end)
{
	uniphier_cache_maint_range(start, end, UNIPHIER_SSCOQM_CM_CLEAN);
}

static void uniphier_cache_flush_range(unsigned long start, unsigned long end)
{
	uniphier_cache_maint_range(start, end, UNIPHIER_SSCOQM_CM_FLUSH);
}

static void __init uniphier_cache_inv_all(void)
{
	uniphier_cache_maint_all(UNIPHIER_SSCOQM_CM_INV);
}

static void uniphier_cache_flush_all(void)
{
	uniphier_cache_maint_all(UNIPHIER_SSCOQM_CM_FLUSH);
}

static void uniphier_cache_disable(void)
{
	struct uniphier_cache_data *data;

	list_for_each_entry_reverse(data, &uniphier_cache_list, list)
		__uniphier_cache_enable(data, false);

	uniphier_cache_flush_all();
}

static void __init uniphier_cache_enable(void)
{
	struct uniphier_cache_data *data;

	uniphier_cache_inv_all();

	list_for_each_entry(data, &uniphier_cache_list, list) {
		__uniphier_cache_enable(data, true);
		__uniphier_cache_set_locked_ways(data, 0);
	}
}

static void uniphier_cache_sync(void)
{
	struct uniphier_cache_data *data;

	list_for_each_entry(data, &uniphier_cache_list, list)
		__uniphier_cache_sync(data);
}

int __init uniphier_cache_l2_is_enabled(void)
{
	struct uniphier_cache_data *data;

	data = list_first_entry_or_null(&uniphier_cache_list,
					struct uniphier_cache_data, list);
	if (!data)
		return 0;

	return !!(readl_relaxed(data->ctrl_base + UNIPHIER_SSCC) &
		  UNIPHIER_SSCC_ON);
}

void __init uniphier_cache_l2_touch_range(unsigned long start,
					  unsigned long end)
{
	struct uniphier_cache_data *data;

	data = list_first_entry_or_null(&uniphier_cache_list,
					struct uniphier_cache_data, list);
	if (data)
		__uniphier_cache_maint_range(data, start, end,
					     UNIPHIER_SSCOQM_TID_WAY |
					     UNIPHIER_SSCOQM_CM_TOUCH);
}

void __init uniphier_cache_l2_set_locked_ways(u32 way_mask)
{
	struct uniphier_cache_data *data;

	data = list_first_entry_or_null(&uniphier_cache_list,
					struct uniphier_cache_data, list);
	if (data)
		__uniphier_cache_set_locked_ways(data, way_mask);
}

static const struct of_device_id uniphier_cache_match[] __initconst = {
	{
		.compatible = "socionext,uniphier-system-cache",
	},
	{ /* sentinel */ }
};

static int __init __uniphier_cache_init(struct device_node *np,
					unsigned int *cache_level)
{
	struct uniphier_cache_data *data;
	u32 level, cache_size;
	struct device_node *next_np;
	int ret = 0;

	if (!of_match_node(uniphier_cache_match, np)) {
		pr_err("L%d: not compatible with uniphier cache\n",
		       *cache_level);
		return -EINVAL;
	}

	if (of_property_read_u32(np, "cache-level", &level)) {
		pr_err("L%d: cache-level is not specified\n", *cache_level);
		return -EINVAL;
	}

	if (level != *cache_level) {
		pr_err("L%d: cache-level is unexpected value %d\n",
		       *cache_level, level);
		return -EINVAL;
	}

	if (!of_property_read_bool(np, "cache-unified")) {
		pr_err("L%d: cache-unified is not specified\n", *cache_level);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (of_property_read_u32(np, "cache-line-size", &data->line_size) ||
	    !is_power_of_2(data->line_size)) {
		pr_err("L%d: cache-line-size is unspecified or invalid\n",
		       *cache_level);
		ret = -EINVAL;
		goto err;
	}

	if (of_property_read_u32(np, "cache-sets", &data->nsets) ||
	    !is_power_of_2(data->nsets)) {
		pr_err("L%d: cache-sets is unspecified or invalid\n",
		       *cache_level);
		ret = -EINVAL;
		goto err;
	}

	if (of_property_read_u32(np, "cache-size", &cache_size) ||
	    cache_size == 0 || cache_size % (data->nsets * data->line_size)) {
		pr_err("L%d: cache-size is unspecified or invalid\n",
		       *cache_level);
		ret = -EINVAL;
		goto err;
	}

	data->way_present_mask =
		((u32)1 << cache_size / data->nsets / data->line_size) - 1;

	data->ctrl_base = of_iomap(np, 0);
	if (!data->ctrl_base) {
		pr_err("L%d: failed to map control register\n", *cache_level);
		ret = -ENOMEM;
		goto err;
	}

	data->rev_base = of_iomap(np, 1);
	if (!data->rev_base) {
		pr_err("L%d: failed to map revision register\n", *cache_level);
		ret = -ENOMEM;
		goto err;
	}

	data->op_base = of_iomap(np, 2);
	if (!data->op_base) {
		pr_err("L%d: failed to map operation register\n", *cache_level);
		ret = -ENOMEM;
		goto err;
	}

	if (*cache_level == 2) {
		u32 revision = readl(data->rev_base + UNIPHIER_SSCID);
		/*
		 * The size of range operation is limited to (1 << 22) or less
		 * for PH-sLD8 or older SoCs.
		 */
		if (revision <= 0x16)
			data->range_op_max_size = (u32)1 << 22;
	}

	data->range_op_max_size -= data->line_size;

	INIT_LIST_HEAD(&data->list);
	list_add_tail(&data->list, &uniphier_cache_list); /* no mutex */

	/*
	 * OK, this level has been successfully initialized.  Look for the next
	 * level cache.  Do not roll back even if the initialization of the
	 * next level cache fails because we want to continue with available
	 * cache levels.
	 */
	next_np = of_find_next_cache_node(np);
	if (next_np) {
		(*cache_level)++;
		ret = __uniphier_cache_init(next_np, cache_level);
	}
	of_node_put(next_np);

	return ret;
err:
	iounmap(data->op_base);
	iounmap(data->rev_base);
	iounmap(data->ctrl_base);
	kfree(data);

	return ret;
}

int __init uniphier_cache_init(void)
{
	struct device_node *np = NULL;
	unsigned int cache_level;
	int ret = 0;

	/* look for level 2 cache */
	while ((np = of_find_matching_node(np, uniphier_cache_match)))
		if (!of_property_read_u32(np, "cache-level", &cache_level) &&
		    cache_level == 2)
			break;

	if (!np)
		return -ENODEV;

	ret = __uniphier_cache_init(np, &cache_level);
	of_node_put(np);

	if (ret) {
		/*
		 * Error out iif L2 initialization fails.  Continue with any
		 * error on L3 or outer because they are optional.
		 */
		if (cache_level == 2) {
			pr_err("failed to initialize L2 cache\n");
			return ret;
		}

		cache_level--;
		ret = 0;
	}

	outer_cache.inv_range = uniphier_cache_inv_range;
	outer_cache.clean_range = uniphier_cache_clean_range;
	outer_cache.flush_range = uniphier_cache_flush_range;
	outer_cache.flush_all = uniphier_cache_flush_all;
	outer_cache.disable = uniphier_cache_disable;
	outer_cache.sync = uniphier_cache_sync;

	uniphier_cache_enable();

	pr_info("enabled outer cache (cache level: %d)\n", cache_level);

	return ret;
}
