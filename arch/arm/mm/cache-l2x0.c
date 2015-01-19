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
#include "cache-aurora-l2.h"

#ifdef CONFIG_PLAT_MESON
#include <mach/io.h>
#ifdef CONFIG_MESON_TRUSTZONE
#include <mach/meson-secure.h>
#endif
#endif


#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static DEFINE_RAW_SPINLOCK(l2x0_lock);
static u32 l2x0_way_mask;	/* Bitmask of active ways */
static u32 l2x0_size;
static u32 l2x0_cache_id;
static unsigned int l2x0_sets;
static unsigned int l2x0_ways;
static unsigned long sync_reg_offset = L2X0_CACHE_SYNC;

/* Aurora don't have the cache ID register available, so we have to
 * pass it though the device tree */
static u32  cache_id_part_number_from_dt;

struct l2x0_regs l2x0_saved_regs;

struct l2x0_of_data {
	void (*setup)(const struct device_node *, u32 *, u32 *);
	void (*save)(void);
	struct outer_cache_fns outer_cache;
};

static bool of_init = false;

static inline bool is_pl310_rev(int rev)
{
	return (l2x0_cache_id &
		(L2X0_CACHE_ID_PART_MASK | L2X0_CACHE_ID_REV_MASK)) ==
			(L2X0_CACHE_ID_PART_L310 | rev);
}

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
#ifdef CONFIG_MESON_TRUSTZONE
	meson_smc1(TRUSTZONE_MON_L2X0_DEBUG_INDEX, val);
#else
	writel_relaxed(val, l2x0_base + L2X0_DEBUG_CTRL);
#endif
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

#ifdef CONFIG_PL310_ERRATA_727915
static void l2x0_for_each_set_way(void __iomem *reg)
{
	int set;
	int way;
	unsigned long flags;

	for (way = 0; way < l2x0_ways; way++) {
		raw_spin_lock_irqsave(&l2x0_lock, flags);
		for (set = 0; set < l2x0_sets; set++)
			writel_relaxed((way << 28) | (set << 5), reg);
		cache_sync();
		raw_spin_unlock_irqrestore(&l2x0_lock, flags);
	}
}
#endif

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

#ifdef CONFIG_PL310_ERRATA_727915
	if (is_pl310_rev(REV_PL310_R2P0)) {
		l2x0_for_each_set_way(l2x0_base + L2X0_CLEAN_INV_LINE_IDX);
		return;
	}
#endif

	/* clean all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2x0_flush_all();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_clean_all(void)
{
	unsigned long flags;

#ifdef CONFIG_PL310_ERRATA_727915
	if (is_pl310_rev(REV_PL310_R2P0)) {
		l2x0_for_each_set_way(l2x0_base + L2X0_CLEAN_LINE_IDX);
		return;
	}
#endif

	/* clean all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	debug_writel(0x03);
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_CLEAN_WAY);
	cache_wait_way(l2x0_base + L2X0_CLEAN_WAY, l2x0_way_mask);
	cache_sync();
	debug_writel(0x00);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_inv_all(void)
{
	unsigned long flags;

	/* invalidate all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	/* Invalidating when L2 is enabled is a nono */
	BUG_ON(readl(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN);
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

#ifdef CONFIG_MESON_TRUSTZONE
	meson_smc1(TRUSTZONE_MON_L2X0_CTRL_INDEX, 0);
#else
	writel_relaxed(0, l2x0_base + L2X0_CTRL);
#endif
	dsb();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_unlock(u32 cache_id)
{
	int lockregs;
	int i;

	switch (cache_id) {
	case L2X0_CACHE_ID_PART_L310:
		lockregs = 8;
		break;
	case AURORA_CACHE_ID:
		lockregs = 4;
		break;
	default:
		/* L210 and unknown types */
		lockregs = 1;
		break;
	}

	for (i = 0; i < lockregs; i++) {
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
	}
}

void __init l2x0_init(void __iomem *base, u32 aux_val, u32 aux_mask)
{
	u32 aux,prefetch,power,tag_lan,data_lan;
	u32 way_size = 0;
	int way_size_shift = L2X0_WAY_SIZE_SHIFT;
	const char *type;

	l2x0_base = base;
	if (cache_id_part_number_from_dt)
		l2x0_cache_id = cache_id_part_number_from_dt;
	else
		l2x0_cache_id = readl_relaxed(l2x0_base + L2X0_CACHE_ID);
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);

	aux &= aux_mask;
	aux |= aux_val;

	/* Determine the number of ways */
	switch (l2x0_cache_id & L2X0_CACHE_ID_PART_MASK) {
	case L2X0_CACHE_ID_PART_L310:
		if (aux & (1 << 16))
			l2x0_ways = 16;
		else
			l2x0_ways = 8;
		type = "L310";
#ifdef CONFIG_PL310_ERRATA_753970
		/* Unmapped register. */
		sync_reg_offset = L2X0_DUMMY_REG;
#endif
		//if ((cache_id & L2X0_CACHE_ID_RTL_MASK) <= L2X0_CACHE_ID_RTL_R3P0)
		if ((l2x0_cache_id & L2X0_CACHE_ID_RTL_MASK) <= L2X0_CACHE_ID_RTL_R3P0)
			outer_cache.set_debug = pl310_set_debug;
		break;
	case L2X0_CACHE_ID_PART_L210:
		l2x0_ways = (aux >> 13) & 0xf;
		type = "L210";
		break;

	case AURORA_CACHE_ID:
		sync_reg_offset = AURORA_SYNC_REG;
		//ways = (aux >> 13) & 0xf;
		l2x0_ways = (aux >> 13) & 0xf;
		//ways = 2 << ((ways + 1) >> 2);
		l2x0_ways = 2 << ((l2x0_ways + 1) >> 2);
		way_size_shift = AURORA_WAY_SIZE_SHIFT;
		type = "Aurora";
		break;
	default:
		/* Assume unknown chips have 8 ways */
		l2x0_ways = 8;
		type = "L2x0 series";
		break;
	}

	l2x0_way_mask = (1 << l2x0_ways) - 1;

	/*
	 * L2 cache Size =  Way size * Number of ways
	 */
	way_size = (aux & L2X0_AUX_CTRL_WAY_SIZE_MASK) >> 17;
	way_size = SZ_1K << (way_size + way_size_shift);

	l2x0_size = l2x0_ways * way_size;
	l2x0_sets = way_size / CACHE_LINE_SIZE;

	/*
	 * Check if l2x0 controller is already enabled.
	 * If you are booting from non-secure mode
	 * accessing the below registers will fault.
	 */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		/* Make sure that I&D is not locked down when starting */
		l2x0_unlock(l2x0_cache_id);

		/* l2x0 controller is disabled */
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_AUXCTRL_INDEX, aux);
#else
		writel_relaxed(aux, l2x0_base + L2X0_AUX_CTRL);
#endif

		l2x0_inv_all();

		/* enable L2X0 */

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_CTRL_INDEX, L2X0_CTRL_EN);
#else
		writel_relaxed(L2X0_CTRL_EN, l2x0_base + L2X0_CTRL);
#endif
	}

	/* Re-read it in case some bits are reserved. */
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);
	prefetch=readl_relaxed(l2x0_base+ L2X0_PREFETCH_CTRL);
	power=readl_relaxed(l2x0_base+ L2X0_POWER_CTRL);
	tag_lan = readl_relaxed(l2x0_base + L2X0_TAG_LATENCY_CTRL);
	data_lan = readl_relaxed(l2x0_base + L2X0_DATA_LATENCY_CTRL);
	
	/* Save the value for resuming. */
	l2x0_saved_regs.aux_ctrl = aux;

	if (!of_init) {
		outer_cache.inv_range = l2x0_inv_range;
		outer_cache.clean_range = l2x0_clean_range;
		outer_cache.flush_range = l2x0_flush_range;
		outer_cache.sync = l2x0_cache_sync;
		outer_cache.flush_all = l2x0_flush_all;
		outer_cache.inv_all = l2x0_inv_all;
		outer_cache.disable = l2x0_disable;
	}

	printk(KERN_INFO "%s cache controller enabled\n", type);
	printk(KERN_INFO "l2x0: %d ways, %d sets, CACHE_ID 0x%08x,  Cache size: %d B\n",
			l2x0_ways, l2x0_sets, l2x0_cache_id, l2x0_size);
	printk(KERN_INFO "      AUX_CTRL 0x%08x, PERFETCH_CTRL 0x%08x, POWER_CTRL  0x%08x\n",
		aux,prefetch,power);
	printk(KERN_INFO "      TAG_LATENCY 0x%08x, DATA_LATENCY 0x%08x\n",
		tag_lan, data_lan);
}

#ifdef CONFIG_OF
static int l2_wt_override;

/*
 * Note that the end addresses passed to Linux primitives are
 * noninclusive, while the hardware cache range operations use
 * inclusive start and end addresses.
 */
static unsigned long calc_range_end(unsigned long start, unsigned long end)
{
	/*
	 * Limit the number of cache lines processed at once,
	 * since cache range operations stall the CPU pipeline
	 * until completion.
	 */
	if (end > start + MAX_RANGE_SIZE)
		end = start + MAX_RANGE_SIZE;

	/*
	 * Cache range operations can't straddle a page boundary.
	 */
	if (end > PAGE_ALIGN(start+1))
		end = PAGE_ALIGN(start+1);

	return end;
}

/*
 * Make sure 'start' and 'end' reference the same page, as L2 is PIPT
 * and range operations only do a TLB lookup on the start address.
 */
static void aurora_pa_range(unsigned long start, unsigned long end,
			unsigned long offset)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	writel_relaxed(start, l2x0_base + AURORA_RANGE_BASE_ADDR_REG);
	writel_relaxed(end, l2x0_base + offset);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);

	cache_sync();
}

static void aurora_inv_range(unsigned long start, unsigned long end)
{
	/*
	 * round start and end adresses up to cache line size
	 */
	start &= ~(CACHE_LINE_SIZE - 1);
	end = ALIGN(end, CACHE_LINE_SIZE);

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < end) {
		unsigned long range_end = calc_range_end(start, end);
		aurora_pa_range(start, range_end - CACHE_LINE_SIZE,
				AURORA_INVAL_RANGE_REG);
		start = range_end;
	}
}

static void aurora_clean_range(unsigned long start, unsigned long end)
{
	/*
	 * If L2 is forced to WT, the L2 will always be clean and we
	 * don't need to do anything here.
	 */
	if (!l2_wt_override) {
		start &= ~(CACHE_LINE_SIZE - 1);
		end = ALIGN(end, CACHE_LINE_SIZE);
		while (start != end) {
			unsigned long range_end = calc_range_end(start, end);
			aurora_pa_range(start, range_end - CACHE_LINE_SIZE,
					AURORA_CLEAN_RANGE_REG);
			start = range_end;
		}
	}
}

static void aurora_flush_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	end = ALIGN(end, CACHE_LINE_SIZE);
	while (start != end) {
		unsigned long range_end = calc_range_end(start, end);
		/*
		 * If L2 is forced to WT, the L2 will always be clean and we
		 * just need to invalidate.
		 */
		if (l2_wt_override)
			aurora_pa_range(start, range_end - CACHE_LINE_SIZE,
							AURORA_INVAL_RANGE_REG);
		else
			aurora_pa_range(start, range_end - CACHE_LINE_SIZE,
							AURORA_FLUSH_RANGE_REG);
		start = range_end;
	}
}

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

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_TAGLATENCY_INDEX,
			((tag[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((tag[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((tag[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT));
#else
		writel_relaxed(
			((tag[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((tag[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((tag[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT),
			l2x0_base + L2X0_TAG_LATENCY_CTRL);
#endif

	of_property_read_u32_array(np, "arm,data-latency",
				   data, ARRAY_SIZE(data));
	if (data[0] && data[1] && data[2])

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_DATALATENCY_INDEX,
			((data[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((data[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((data[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT));
#else
		writel_relaxed(
			((data[0] - 1) << L2X0_LATENCY_CTRL_RD_SHIFT) |
			((data[1] - 1) << L2X0_LATENCY_CTRL_WR_SHIFT) |
			((data[2] - 1) << L2X0_LATENCY_CTRL_SETUP_SHIFT),
			l2x0_base + L2X0_DATA_LATENCY_CTRL);
#endif

	of_property_read_u32_array(np, "arm,filter-ranges",
				   filter, ARRAY_SIZE(filter));
	if (filter[1]) {
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_FILTEREND_INDEX, ALIGN(filter[0] + filter[1], SZ_1M));
#else
		writel_relaxed(ALIGN(filter[0] + filter[1], SZ_1M),
			       l2x0_base + L2X0_ADDR_FILTER_END);
#endif

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_FILTERSTART_INDEX, (filter[0] & ~(SZ_1M - 1)) | L2X0_ADDR_FILTER_EN);
#else
		writel_relaxed((filter[0] & ~(SZ_1M - 1)) | L2X0_ADDR_FILTER_EN,
			       l2x0_base + L2X0_ADDR_FILTER_START);
#endif
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

static void aurora_save(void)
{
	l2x0_saved_regs.ctrl = readl_relaxed(l2x0_base + L2X0_CTRL);
	l2x0_saved_regs.aux_ctrl = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);
}

static void l2x0_resume(void)
{
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		/* restore aux ctrl and enable l2 */
		l2x0_unlock(readl_relaxed(l2x0_base + L2X0_CACHE_ID));

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_AUXCTRL_INDEX, l2x0_saved_regs.aux_ctrl);
#else
		writel_relaxed(l2x0_saved_regs.aux_ctrl, l2x0_base +
			L2X0_AUX_CTRL);
#endif

		l2x0_inv_all();

#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_CTRL_INDEX, L2X0_CTRL_EN);
#else
		writel_relaxed(L2X0_CTRL_EN, l2x0_base + L2X0_CTRL);
#endif
	}
}

static void pl310_resume(void)
{
	u32 l2x0_revision;

	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		/* restore pl310 setup */
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_TAGLATENCY_INDEX, l2x0_saved_regs.tag_latency);
#else
		writel_relaxed(l2x0_saved_regs.tag_latency,
			l2x0_base + L2X0_TAG_LATENCY_CTRL);
#endif
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_DATALATENCY_INDEX, l2x0_saved_regs.data_latency);
#else
		writel_relaxed(l2x0_saved_regs.data_latency,
			l2x0_base + L2X0_DATA_LATENCY_CTRL);
#endif
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_FILTEREND_INDEX, l2x0_saved_regs.filter_end);
#else
		writel_relaxed(l2x0_saved_regs.filter_end,
			l2x0_base + L2X0_ADDR_FILTER_END);
#endif
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_FILTERSTART_INDEX, l2x0_saved_regs.filter_start);
#else
		writel_relaxed(l2x0_saved_regs.filter_start,
			l2x0_base + L2X0_ADDR_FILTER_START);
#endif

		l2x0_revision = readl_relaxed(l2x0_base + L2X0_CACHE_ID) &
			L2X0_CACHE_ID_RTL_MASK;

		if (l2x0_revision >= L2X0_CACHE_ID_RTL_R2P0) {
#ifdef CONFIG_MESON_TRUSTZONE
			meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, l2x0_saved_regs.prefetch_ctrl);
#else
			writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
				l2x0_base + L2X0_PREFETCH_CTRL);
#endif
			if (l2x0_revision >= L2X0_CACHE_ID_RTL_R3P0)
#ifdef CONFIG_MESON_TRUSTZONE
				meson_smc1(TRUSTZONE_MON_L2X0_POWER_INDEX, l2x0_saved_regs.pwr_ctrl);
#else
				writel_relaxed(l2x0_saved_regs.pwr_ctrl,
					l2x0_base + L2X0_POWER_CTRL);
#endif
		}
	}

	l2x0_resume();
}

static void aurora_resume(void)
{
	if (!(readl(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_AUXCTRL_INDEX, l2x0_saved_regs.aux_ctrl);
		meson_smc1(TRUSTZONE_MON_L2X0_CTRL_INDEX, l2x0_saved_regs.ctrl);
#else
		writel_relaxed(l2x0_saved_regs.aux_ctrl,
				l2x0_base + L2X0_AUX_CTRL);
		writel_relaxed(l2x0_saved_regs.ctrl, l2x0_base + L2X0_CTRL);
#endif
	}
}

static void __init aurora_broadcast_l2_commands(void)
{
	__u32 u;
	/* Enable Broadcasting of cache commands to L2*/
	__asm__ __volatile__("mrc p15, 1, %0, c15, c2, 0" : "=r"(u));
	u |= AURORA_CTRL_FW;		/* Set the FW bit */
	__asm__ __volatile__("mcr p15, 1, %0, c15, c2, 0\n" : : "r"(u));
	isb();
}

static void __init aurora_of_setup(const struct device_node *np,
				u32 *aux_val, u32 *aux_mask)
{
	u32 val = AURORA_ACR_REPLACEMENT_TYPE_SEMIPLRU;
	u32 mask =  AURORA_ACR_REPLACEMENT_MASK;

	of_property_read_u32(np, "cache-id-part",
			&cache_id_part_number_from_dt);

	/* Determine and save the write policy */
	l2_wt_override = of_property_read_bool(np, "wt-override");

	if (l2_wt_override) {
		val |= AURORA_ACR_FORCE_WRITE_THRO_POLICY;
		mask |= AURORA_ACR_FORCE_WRITE_POLICY_MASK;
	}

	*aux_val &= ~mask;
	*aux_val |= val;
	*aux_mask &= ~mask;
}


#ifdef CONFIG_PLAT_MESON
#define L2X0_AUX_CTRL_FULL_LINE_OF_ZERO_SHIFT		0
#define L2X0_AUX_CTRL_HIGH_PRIORITY_FOR_SO_DEV_SHIFT	10
#define L2X0_AUX_CTRL_STORE_BUFFER_DEV_LMT_SHIFT	11
#define L2X0_AUX_CTRL_EXCLUSIVE_CACHE_CFG_SHIFT		12
#define L2X0_AUX_CTRL_SHARED_ATTRIB_INVALID_SHIFT	13
#define L2X0_AUX_CTRL_FORCE_WRITE_ALLOCATE_SHIFT	23
#define L2X0_AUX_CTRL_FORCE_WRITE_ALLOCATE_MASK	(3 << 23)
#define L2X0_AUX_CTRL_CACHE_REPLACE_POLICY_SHIFT	25

#define L2X0_PREF_CTRL_DOUBLE_LINE_FILL_SHIFT	30
#define L2X0_PREF_CTRL_INSTR_PREFETCH_SHIFT	29
#define L2X0_PREF_CTRL_DATA_PREFETCH_SHIFT	28
#define L2X0_PREF_CTRL_DLF_ON_WRAP_DISABLE_SHIFT	27
#define L2X0_PREF_CTRL_PREFETCH_DROP_SHIFT	24
#define L2X0_PREF_CTRL_INCR_DLF_SHIFT	23
#define L2X0_PREF_CTRL_NOT_SAME_ID_EXEL_SEQ_SHIFT	21
#define L2X0_PREF_CTRL_PERFETCH_OFFSET_SHIFT		0
#define L2X0_PREF_CTRL_PERFETCH_OFFSET_MASK		(0x1f << 0)
static void __init meson_of_setup(const struct device_node *np,
				u32 *aux_val, u32 *aux_mask)
{
	u32 val = *aux_val;
	u32 mask =  *aux_mask;

	void * prefetch_reg = (void * )l2x0_base + L2X0_PREFETCH_CTRL;
	u32 prefetch_val;
	void * power_reg = (void * )l2x0_base + L2X0_POWER_CTRL;
	u32 power_val;

	int way_size = -1, ws_value;
	int associativity = -1, assoc_value;
	int prefetch_offset = -1;

	if (of_property_read_bool(np, "aux-early_write_response")) {
		val |= (1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT);
	}

	if(of_property_read_bool(np, "aux-instruction_prefetch")){
		val |= (1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT);
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_INSTR_PREFETCH_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);		
#endif
	}

	if(of_property_read_bool(np, "aux-data_prefetch")){
		val |= (1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT);		
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_DATA_PREFETCH_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);		
#endif
	}

	if (of_property_read_bool(np, "aux-ns_lockdown")) {
		val |= (1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT);
	}

	if(of_property_read_bool(np, "aux-cache_replace_policy_round_robin")){
		val |= (1 << L2X0_AUX_CTRL_CACHE_REPLACE_POLICY_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_CACHE_REPLACE_POLICY_SHIFT);		
	}else{
		val &= ~(1 << L2X0_AUX_CTRL_CACHE_REPLACE_POLICY_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_CACHE_REPLACE_POLICY_SHIFT);	
	}

	if(of_property_read_bool(np, "aux-force_no_write_alloc")){
		val |= (1 << L2X0_AUX_CTRL_FORCE_WRITE_ALLOCATE_SHIFT);
		mask &= ~L2X0_AUX_CTRL_FORCE_WRITE_ALLOCATE_MASK;
	}

	if(of_property_read_bool(np, "aux-store_buffer_device_limit")){
		val |= (1 << L2X0_AUX_CTRL_STORE_BUFFER_DEV_LMT_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_STORE_BUFFER_DEV_LMT_SHIFT);
	}

	if(of_property_read_bool(np, "aux-high_prio_for_so_dev_read")){
		val |= (1 << L2X0_AUX_CTRL_HIGH_PRIORITY_FOR_SO_DEV_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_HIGH_PRIORITY_FOR_SO_DEV_SHIFT);
	}

	if(of_property_read_bool(np, "aux-full_line_of_zero")){
		val |= (1 << L2X0_AUX_CTRL_FULL_LINE_OF_ZERO_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_FULL_LINE_OF_ZERO_SHIFT);
	}

	if(of_property_read_bool(np, "aux-ns_int_ctrl")){
		val |= (1 << L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT);
	}

	if(of_property_read_bool(np, "aux-share_override")){
		val |= (1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT);
		mask &= ~(1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT);
	}
	
	if(of_property_read_u32(np, "aux-way_size",&way_size) > 0){
		switch(way_size){
		case 16:
			ws_value = 1;
			break;
		case 32:
			ws_value = 2;
			break;
		case 64:
			ws_value = 3;
			break;
		case 128:
			ws_value = 4;
			break;
		case 256:
			ws_value = 5;
			break;
		case 512:
			ws_value = 6;
			break;
		default:
			ws_value = 0;
			break;
		}
		val |= (ws_value << L2X0_AUX_CTRL_WAY_SIZE_SHIFT);
		mask &= ~L2X0_AUX_CTRL_WAY_SIZE_MASK;
	}

	if(!of_property_read_u32(np, "aux-associativity",&associativity)){
		if(associativity == 16)
			assoc_value = 1;
		else 
			assoc_value = 0;
		val |= (assoc_value << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT);
		mask &= ~(assoc_value << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT);
	}

	if(of_property_read_bool(np, "prefetch-double_line_fill")){
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_DOUBLE_LINE_FILL_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);
#endif
	}

	if(of_property_read_bool(np, "prefetch-double_line_fill_on_wrap_read_disable")){
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_DLF_ON_WRAP_DISABLE_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);
#endif
	}

	if(of_property_read_bool(np, "prefetch-prefetch_drop")){
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_PREFETCH_DROP_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);
#endif
	}

	if(of_property_read_bool(np, "prefetch-incr_double_line_fill")){
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_INCR_DLF_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);
#endif
	}

	if(of_property_read_bool(np, "prefetch-not_same_id_excl_seq")){
		prefetch_val = readl_relaxed(prefetch_reg);
		prefetch_val |= (1 << L2X0_PREF_CTRL_NOT_SAME_ID_EXEL_SEQ_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
		writel_relaxed(prefetch_val,prefetch_reg);
#endif
	}

	if(!of_property_read_u32(np, "prefetch-prefetch_offset",&prefetch_offset)){
		switch(prefetch_offset){
		case 0 ... 7:
		case 15:
		case 23:
		case 31:
			prefetch_val = readl_relaxed(prefetch_reg);
			prefetch_val &= ~(L2X0_PREF_CTRL_PERFETCH_OFFSET_MASK);
			prefetch_val |= (prefetch_offset << L2X0_PREF_CTRL_PERFETCH_OFFSET_SHIFT);
#ifdef CONFIG_MESON_TRUSTZONE
			meson_smc1(TRUSTZONE_MON_L2X0_PREFETCH_INDEX, prefetch_val);
#else
			writel_relaxed(prefetch_val,prefetch_reg);
#endif
		default:
			break;
		}

	}

	if(of_property_read_bool(np, "power-dynamic_clk_gating")){
		power_val = readl_relaxed(power_reg);
		power_val |= L2X0_DYNAMIC_CLK_GATING_EN;
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_POWER_INDEX, power_val);
#else
		writel_relaxed(power_val,power_reg);
#endif
	}

	if(of_property_read_bool(np, "power-standby_mode")){
		power_val = readl_relaxed(power_reg);
		power_val |= L2X0_STNDBY_MODE_EN;
#ifdef CONFIG_MESON_TRUSTZONE
		meson_smc1(TRUSTZONE_MON_L2X0_POWER_INDEX, power_val);
#else
		writel_relaxed(power_val,power_reg);
#endif
	}

	*aux_val = val;
	*aux_mask = mask;
	
	pl310_of_setup(np,aux_val,aux_mask);
}

static const struct l2x0_of_data meson_pl310_data = {
	.setup = meson_of_setup,
	.save  = pl310_save,
	.outer_cache = {
		.resume      = pl310_resume,
		.inv_range   = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.sync        = l2x0_cache_sync,
		.flush_all   = l2x0_flush_all,
		.inv_all     = l2x0_inv_all,
		.disable     = l2x0_disable,
		.set_debug   = pl310_set_debug,
	},
};
#endif
static const struct l2x0_of_data pl310_data = {
	.setup = pl310_of_setup,
	.save  = pl310_save,
	.outer_cache = {
		.resume      = pl310_resume,
		.inv_range   = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.sync        = l2x0_cache_sync,
		.flush_all   = l2x0_flush_all,
		.inv_all     = l2x0_inv_all,
		.disable     = l2x0_disable,
		.set_debug   = pl310_set_debug,
	},
};

static const struct l2x0_of_data l2x0_data = {
	.setup = l2x0_of_setup,
	.save  = NULL,
	.outer_cache = {
		.resume      = l2x0_resume,
		.inv_range   = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.sync        = l2x0_cache_sync,
		.flush_all   = l2x0_flush_all,
		.inv_all     = l2x0_inv_all,
		.disable     = l2x0_disable,
	},
};

static const struct l2x0_of_data aurora_with_outer_data = {
	.setup = aurora_of_setup,
	.save  = aurora_save,
	.outer_cache = {
		.resume      = aurora_resume,
		.inv_range   = aurora_inv_range,
		.clean_range = aurora_clean_range,
		.flush_range = aurora_flush_range,
		.sync        = l2x0_cache_sync,
		.flush_all   = l2x0_flush_all,
		.inv_all     = l2x0_inv_all,
		.disable     = l2x0_disable,
	},
};

static const struct l2x0_of_data aurora_no_outer_data = {
	.setup = aurora_of_setup,
	.save  = aurora_save,
	.outer_cache = {
		.resume      = aurora_resume,
	},
};

static const struct of_device_id l2x0_ids[] __initconst = {
#ifdef CONFIG_PLAT_MESON
	{ .compatible = "arm,meson-pl310-cache", .data = (void *)&meson_pl310_data },
#endif
	{ .compatible = "arm,pl310-cache", .data = (void *)&pl310_data },
	{ .compatible = "arm,l220-cache", .data = (void *)&l2x0_data },
	{ .compatible = "arm,l210-cache", .data = (void *)&l2x0_data },
	{ .compatible = "marvell,aurora-system-cache",
	  .data = (void *)&aurora_no_outer_data},
	{ .compatible = "marvell,aurora-outer-cache",
	  .data = (void *)&aurora_with_outer_data},
	{}
};

int __init l2x0_of_init(u32 aux_val, u32 aux_mask)
{
	struct device_node *np;
	const struct l2x0_of_data *data;
	struct resource res;

	np = of_find_matching_node(NULL, l2x0_ids);
	if (!np)
		return -ENODEV;

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;
	
#ifdef CONFIG_PLAT_MESON
	l2x0_base = (void __iomem *)IO_PL310_BASE;
#else
	l2x0_base = ioremap(res.start, resource_size(&res));
	if (!l2x0_base)
		return -ENOMEM;
#endif

	l2x0_saved_regs.phy_base = res.start;

	data = of_match_node(l2x0_ids, np)->data;

	/* L2 configuration can only be changed if the cache is disabled */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		if (data->setup)
			data->setup(np, &aux_val, &aux_mask);

		/* For aurora cache in no outer mode select the
		 * correct mode using the coprocessor*/
		if (data == &aurora_no_outer_data)
			aurora_broadcast_l2_commands();
	}

	if (data->save)
		data->save();

	of_init = true;
	l2x0_init(l2x0_base, aux_val, aux_mask);

	memcpy(&outer_cache, &data->outer_cache, sizeof(outer_cache));

	return 0;
}
#endif
