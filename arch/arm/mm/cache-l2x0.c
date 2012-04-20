/*
 * arch/arm/mm/cache-l2x0.c - L210/L220 cache controller support
 *
 * Copyright (C) 2007 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static DEFINE_RAW_SPINLOCK(l2x0_lock);
static u32 l2x0_way_mask;	/* Bitmask of active ways */
static u32 l2x0_size;
static unsigned long sync_reg_offset = L2X0_CACHE_SYNC;

struct l2x0_regs l2x0_saved_regs;

struct l2x0_of_data {
	void (*setup)(const struct device_node *, u32 *, u32 *);
	void (*save)(void);
	void (*resume)(void);
};

static inline void cache_wait_way(void __iomem *reg, unsigned long mask)
{
	/* wait for cache operation by line or way to complete */
	while (readl_relaxed(reg) & mask)
		cpu_relax();
}

#ifdef CONFIG_CACHE_PL310
static inline void cache_wait(void __iomem *reg, unsigned long mask)
{
	/* cache operations by line are atomic on PL310 */
}
#else
#define cache_wait	cache_wait_way
#endif

static inline void cache_sync(void)
{
	void __iomem *base = l2x0_base;

	writel_relaxed(0, base + sync_reg_offset);
	cache_wait(base + L2X0_CACHE_SYNC, 1);
}

static inline void l2x0_clean_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	writel_relaxed(addr, base + L2X0_CLEAN_LINE_PA);
}

static inline void l2x0_inv_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	writel_relaxed(addr, base + L2X0_INV_LINE_PA);
}

#if defined(CONFIG_PL310_ERRATA_588369) || defined(CONFIG_PL310_ERRATA_727915)
static inline void debug_writel(unsigned long val)
{
	if (outer_cache.set_debug)
		outer_cache.set_debug(val);
}

static void pl310_set_debug(unsigned long val)
{
	writel_relaxed(val, l2x0_base + L2X0_DEBUG_CTRL);
}
#else
/* Optimised out for non-errata case */
static inline void debug_writel(unsigned long val)
{
}

#define pl310_set_debug	NULL
#endif

#ifdef CONFIG_PL310_ERRATA_588369
static inline void l2x0_flush_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;

	/* Clean by PA followed by Invalidate by PA */
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	writel_relaxed(addr, base + L2X0_CLEAN_LINE_PA);
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	writel_relaxed(addr, base + L2X0_INV_LINE_PA);
}
#else

static inline void l2x0_flush_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_CLEAN_INV_LINE_PA, 1);
	writel_relaxed(addr, base + L2X0_CLEAN_INV_LINE_PA);
}
#endif

static void l2x0_cache_sync(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void __l2x0_flush_all(void)
{
	debug_writel(0x03);
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_CLEAN_INV_WAY);
	cache_wait_way(l2x0_base + L2X0_CLEAN_INV_WAY, l2x0_way_mask);
	cache_sync();
	debug_writel(0x00);
}

static void l2x0_flush_all(void)
{
	unsigned long flags;

	/* clean all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2x0_flush_all();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_clean_all(void)
{
	unsigned long flags;

	/* clean all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_CLEAN_WAY);
	cache_wait_way(l2x0_base + L2X0_CLEAN_WAY, l2x0_way_mask);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_inv_all(void)
{
	unsigned long flags;

	/* invalidate all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	/* Invalidating when L2 is enabled is a nono */
	BUG_ON(readl(l2x0_base + L2X0_CTRL) & 1);
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_INV_WAY);
	cache_wait_way(l2x0_base + L2X0_INV_WAY, l2x0_way_mask);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_inv_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	if (start & (CACHE_LINE_SIZE - 1)) {
		start &= ~(CACHE_LINE_SIZE - 1);
		debug_writel(0x03);
		l2x0_flush_line(start);
		debug_writel(0x00);
		start += CACHE_LINE_SIZE;
	}

	if (end & (CACHE_LINE_SIZE - 1)) {
		end &= ~(CACHE_LINE_SIZE - 1);
		debug_writel(0x03);
		l2x0_flush_line(end);
		debug_writel(0x00);
	}

	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			l2x0_inv_line(start);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			raw_spin_unlock_irqrestore(&l2x0_lock, flags);
			raw_spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_clean_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	if ((end - start) >= l2x0_size) {
		l2x0_clean_all();
		return;
	}

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			l2x0_clean_line(start);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			raw_spin_unlock_irqrestore(&l2x0_lock, flags);
			raw_spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_flush_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	if ((end - start) >= l2x0_size) {
		l2x0_flush_all();
		return;
	}

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		debug_writel(0x03);
		while (start < blk_end) {
			l2x0_flush_line(start);
			start += CACHE_LINE_SIZE;
		}
		debug_writel(0x00);

		if (blk_end < end) {
			raw_spin_unlock_irqrestore(&l2x0_lock, flags);
			raw_spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_CLEAN_INV_LINE_PA, 1);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_disable(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2x0_flush_all();
	writel_relaxed(0, l2x0_base + L2X0_CTRL);
	dsb();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_unlock(u32 cache_id)
{
	int lockregs;
	int i;

	if (cache_id == L2X0_CACHE_ID_PART_L310)
		lockregs = 8;
	else
		/* L210 and unknown types */
		lockregs = 1;

	for (i = 0; i < lockregs; i++) {
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
	}
}

void __init l2x0_init(void __iomem *base, u32 aux_val, u32 aux_mask)
{
	u32 aux;
	u32 cache_id;
	u32 way_size = 0;
	int ways;
	const char *type;

	l2x0_base = base;

	cache_id = readl_relaxed(l2x0_base + L2X0_CACHE_ID);
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);

	aux &= aux_mask;
	aux |= aux_val;

	/* Determine the number of ways */
	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
	case L2X0_CACHE_ID_PART_L310:
		if (aux & (1 << 16))
			ways = 16;
		else
			ways = 8;
		type = "L310";
#ifdef CONFIG_PL310_ERRATA_753970
		/* Unmapped register. */
		sync_reg_offset = L2X0_DUMMY_REG;
#endif
		outer_cache.set_debug = pl310_set_debug;
		break;
	case L2X0_CACHE_ID_PART_L210:
		ways = (aux >> 13) & 0xf;
		type = "L210";
		break;
	default:
		/* Assume unknown chips have 8 ways */
		ways = 8;
		type = "L2x0 series";
		break;
	}

	l2x0_way_mask = (1 << ways) - 1;

	/*
	 * L2 cache Size =  Way size * Number of ways
	 */
	way_size = (aux & L2X0_AUX_CTRL_WAY_SIZE_MASK) >> 17;
	way_size = 1 << (way_size + 3);
	l2x0_size = ways * way_size * SZ_1K;

	/*
	 * Check if l2x0 controller is already enabled.
	 * If you are booting from non-secure mode
	 * accessing the below registers will fault.
	 */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & 1)) {
		/* Make sure that I&D is not locked down when starting */
		l2x0_unlock(cache_id);

		/* l2x0 controller is disabled */
		writel_relaxed(aux, l2x0_base + L2X0_AUX_CTRL);

		l2x0_saved_regs.aux_ctrl = aux;

		l2x0_inv_all();

		/* enable L2X0 */
		writel_relaxed(1, l2x0_base + L2X0_CTRL);
	}

	outer_cache.inv_range = l2x0_inv_range;
	outer_cache.clean_range = l2x0_clean_range;
	outer_cache.flush_range = l2x0_flush_range;
	outer_cache.sync = l2x0_cache_sync;
	outer_cache.flush_all = l2x0_flush_all;
	outer_cache.inv_all = l2x0_inv_all;
	outer_cache.disable = l2x0_disable;

	printk(KERN_INFO "%s cache controller enabled\n", type);
	printk(KERN_INFO "l2x0: %d ways, CACHE_ID 0x%08x, AUX_CTRL 0x%08x, Cache size: %d B\n",
			ways, cache_id, aux, l2x0_size);
}

#ifdef CONFIG_OF
static void __init l2x0_of_setup(const struct device_node *np,
				 u32 *aux_val, u32 *aux_mask)
{
	u32 data[2] = { 0, 0 };
	u32 tag = 0;
	u32 dirty = 0;
	u32 val = 0, mask = 0;

	of_property_read_u32(np, "arm,tag-latency", &tag);
	if (tag) {
		mask |= L2X0_AUX_CTRL_TAG_LATENCY_MASK;
		val |= (tag - 1) << L2X0_AUX_CTRL_TAG_LATENCY_SHIFT;
	}

	of_property_read_u32_array(np, "arm,data-latency",
				   data, ARRAY_SIZE(data));
	if (data[0] && data[1]) {
		mask |= L2X0_AUX_CTRL_DATA_RD_LATENCY_MASK |
			L2X0_AUX_CTRL_DATA_WR_LATENCY_MASK;
		val |= ((data[0] - 1) << L2X0_AUX_CTRL_DATA_RD_LATENCY_SHIFT) |
		       ((data[1] - 1) << L2X0_AUX_CTRL_DATA_WR_LATENCY_SHIFT);
	}

	of_property_read_u32(np, "arm,dirty-latency", &dirty);
	if (dirty) {
		mask |= L2X0_AUX_CTRL_DIRTY_LATENCY_MASK;
		val |= (dirty - 1) << L2X0_AUX_CTRL_DIRTY_LATENCY_SHIFT;
	}

	*aux_val &= ~mask;
	*aux_val |= val;
	*aux_mask &= ~mask;
}

static void __init pl310_of_setup(const struct device_node *np,
				  u32 *aux_val, u32 *aux_mask)
{
	u32 data[3] = { 0, 0, 0 };
	u32 tag[3] = { 0, 0, 0 };
	u32 filter[2] = { 0, 0 };

	of_property_read_u32_array(np, "arm,tag-latency", tag, ARRAY_SIZE(tag));
	if (tag[0] && tag[1] && tag[2])
		writel_relaxed(
			((tag[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((tag[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((tag[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT),
			l2x0_base + L2X0_TAG_LATENCY_CTRL);

	of_property_read_u32_array(np, "arm,data-latency",
				   data, ARRAY_SIZE(data));
	if (data[0] && data[1] && data[2])
		writel_relaxed(
			((data[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((data[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((data[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT),
			l2x0_base + L2X0_DATA_LATENCY_CTRL);

	of_property_read_u32_array(np, "arm,filter-ranges",
				   filter, ARRAY_SIZE(filter));
	if (filter[1]) {
		writel_relaxed(ALIGN(filter[0] + filter[1], SZ_1M),
			       l2x0_base + L2X0_ADDR_FILTER_END);
		writel_relaxed((filter[0] & ~(SZ_1M - 1)) | L2X0_ADDR_FILTER_EN,
			       l2x0_base + L2X0_ADDR_FILTER_START);
	}
}

static void __init pl310_save(void)
{
	u32 l2x0_revision = readl_relaxed(l2x0_base + L2X0_CACHE_ID) &
		L2X0_CACHE_ID_RTL_MASK;

	l2x0_saved_regs.tag_latency = readl_relaxed(l2x0_base +
		L2X0_TAG_LATENCY_CTRL);
	l2x0_saved_regs.data_latency = readl_relaxed(l2x0_base +
		L2X0_DATA_LATENCY_CTRL);
	l2x0_saved_regs.filter_end = readl_relaxed(l2x0_base +
		L2X0_ADDR_FILTER_END);
	l2x0_saved_regs.filter_start = readl_relaxed(l2x0_base +
		L2X0_ADDR_FILTER_START);

	if (l2x0_revision >= L2X0_CACHE_ID_RTL_R2P0) {
		/*
		 * From r2p0, there is Prefetch offset/control register
		 */
		l2x0_saved_regs.prefetch_ctrl = readl_relaxed(l2x0_base +
			L2X0_PREFETCH_CTRL);
		/*
		 * From r3p0, there is Power control register
		 */
		if (l2x0_revision >= L2X0_CACHE_ID_RTL_R3P0)
			l2x0_saved_regs.pwr_ctrl = readl_relaxed(l2x0_base +
				L2X0_POWER_CTRL);
	}
}

static void l2x0_resume(void)
{
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & 1)) {
		/* restore aux ctrl and enable l2 */
		l2x0_unlock(readl_relaxed(l2x0_base + L2X0_CACHE_ID));

		writel_relaxed(l2x0_saved_regs.aux_ctrl, l2x0_base +
			L2X0_AUX_CTRL);

		l2x0_inv_all();

		writel_relaxed(1, l2x0_base + L2X0_CTRL);
	}
}

static void pl310_resume(void)
{
	u32 l2x0_revision;

	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & 1)) {
		/* restore pl310 setup */
		writel_relaxed(l2x0_saved_regs.tag_latency,
			l2x0_base + L2X0_TAG_LATENCY_CTRL);
		writel_relaxed(l2x0_saved_regs.data_latency,
			l2x0_base + L2X0_DATA_LATENCY_CTRL);
		writel_relaxed(l2x0_saved_regs.filter_end,
			l2x0_base + L2X0_ADDR_FILTER_END);
		writel_relaxed(l2x0_saved_regs.filter_start,
			l2x0_base + L2X0_ADDR_FILTER_START);

		l2x0_revision = readl_relaxed(l2x0_base + L2X0_CACHE_ID) &
			L2X0_CACHE_ID_RTL_MASK;

		if (l2x0_revision >= L2X0_CACHE_ID_RTL_R2P0) {
			writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
				l2x0_base + L2X0_PREFETCH_CTRL);
			if (l2x0_revision >= L2X0_CACHE_ID_RTL_R3P0)
				writel_relaxed(l2x0_saved_regs.pwr_ctrl,
					l2x0_base + L2X0_POWER_CTRL);
		}
	}

	l2x0_resume();
}

static const struct l2x0_of_data pl310_data = {
	pl310_of_setup,
	pl310_save,
	pl310_resume,
};

static const struct l2x0_of_data l2x0_data = {
	l2x0_of_setup,
	NULL,
	l2x0_resume,
};

static const struct of_device_id l2x0_ids[] __initconst = {
	{ .compatible = "arm,pl310-cache", .data = (void *)&pl310_data },
	{ .compatible = "arm,l220-cache", .data = (void *)&l2x0_data },
	{ .compatible = "arm,l210-cache", .data = (void *)&l2x0_data },
	{}
};

int __init l2x0_of_init(u32 aux_val, u32 aux_mask)
{
	struct device_node *np;
	struct l2x0_of_data *data;
	struct resource res;

	np = of_find_matching_node(NULL, l2x0_ids);
	if (!np)
		return -ENODEV;

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;

	l2x0_base = ioremap(res.start, resource_size(&res));
	if (!l2x0_base)
		return -ENOMEM;

	l2x0_saved_regs.phy_base = res.start;

	data = of_match_node(l2x0_ids, np)->data;

	/* L2 configuration can only be changed if the cache is disabled */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & 1)) {
		if (data->setup)
			data->setup(np, &aux_val, &aux_mask);
	}

	if (data->save)
		data->save();

	l2x0_init(l2x0_base, aux_val, aux_mask);

	outer_cache.resume = data->resume;
	return 0;
}
#endif
