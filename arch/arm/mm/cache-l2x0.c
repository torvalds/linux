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
#include "cache-tauros3.h"
#include "cache-aurora-l2.h"

struct l2c_init_data {
	unsigned num_lock;
	void (*of_parse)(const struct device_node *, u32 *, u32 *);
	void (*enable)(void __iomem *, u32, unsigned);
	void (*fixup)(void __iomem *, u32, struct outer_cache_fns *);
	void (*save)(void __iomem *);
	struct outer_cache_fns outer_cache;
};

#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static DEFINE_RAW_SPINLOCK(l2x0_lock);
static u32 l2x0_way_mask;	/* Bitmask of active ways */
static u32 l2x0_size;
static unsigned long sync_reg_offset = L2X0_CACHE_SYNC;

struct l2x0_regs l2x0_saved_regs;

/*
 * Common code for all cache controllers.
 */
static inline void l2c_wait_mask(void __iomem *reg, unsigned long mask)
{
	/* wait for cache operation by line or way to complete */
	while (readl_relaxed(reg) & mask)
		cpu_relax();
}

/*
 * This should only be called when we have a requirement that the
 * register be written due to a work-around, as platforms running
 * in non-secure mode may not be able to access this register.
 */
static inline void l2c_set_debug(void __iomem *base, unsigned long val)
{
	outer_cache.set_debug(val);
}

static void __l2c_op_way(void __iomem *reg)
{
	writel_relaxed(l2x0_way_mask, reg);
	l2c_wait_mask(reg, l2x0_way_mask);
}

static inline void l2c_unlock(void __iomem *base, unsigned num)
{
	unsigned i;

	for (i = 0; i < num; i++) {
		writel_relaxed(0, base + L2X0_LOCKDOWN_WAY_D_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
		writel_relaxed(0, base + L2X0_LOCKDOWN_WAY_I_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
	}
}

/*
 * Enable the L2 cache controller.  This function must only be
 * called when the cache controller is known to be disabled.
 */
static void l2c_enable(void __iomem *base, u32 aux, unsigned num_lock)
{
	unsigned long flags;

	/* Only write the aux register if it needs changing */
	if (readl_relaxed(base + L2X0_AUX_CTRL) != aux)
		writel_relaxed(aux, base + L2X0_AUX_CTRL);

	l2c_unlock(base, num_lock);

	local_irq_save(flags);
	__l2c_op_way(base + L2X0_INV_WAY);
	writel_relaxed(0, base + sync_reg_offset);
	l2c_wait_mask(base + sync_reg_offset, 1);
	local_irq_restore(flags);

	writel_relaxed(L2X0_CTRL_EN, base + L2X0_CTRL);
}

static void l2c_disable(void)
{
	void __iomem *base = l2x0_base;

	outer_cache.flush_all();
	writel_relaxed(0, base + L2X0_CTRL);
	dsb(st);
}

#ifdef CONFIG_CACHE_PL310
static inline void cache_wait(void __iomem *reg, unsigned long mask)
{
	/* cache operations by line are atomic on PL310 */
}
#else
#define cache_wait	l2c_wait_mask
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
		l2c_set_debug(l2x0_base, val);
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
	__l2c_op_way(l2x0_base + L2X0_CLEAN_INV_WAY);
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
	__l2c_op_way(l2x0_base + L2X0_CLEAN_WAY);
	cache_sync();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_inv_all(void)
{
	unsigned long flags;

	/* invalidate all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	/* Invalidating when L2 is enabled is a nono */
	BUG_ON(readl(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN);
	__l2c_op_way(l2x0_base + L2X0_INV_WAY);
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
	dsb(st);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_unlock(u32 cache_id)
{
	int lockregs;

	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
	case L2X0_CACHE_ID_PART_L310:
		lockregs = 8;
		break;
	default:
		/* L210 and unknown types */
		lockregs = 1;
		break;
	}

	l2c_unlock(l2x0_base, lockregs);
}

static void l2x0_enable(void __iomem *base, u32 aux, unsigned num_lock)
{
	/* l2x0 controller is disabled */
	writel_relaxed(aux, base + L2X0_AUX_CTRL);

	/* Make sure that I&D is not locked down when starting */
	l2x0_unlock(readl_relaxed(base + L2X0_CACHE_ID));

	l2x0_inv_all();

	/* enable L2X0 */
	writel_relaxed(L2X0_CTRL_EN, base + L2X0_CTRL);
}

static void l2x0_resume(void)
{
	void __iomem *base = l2x0_base;

	if (!(readl_relaxed(base + L2X0_CTRL) & L2X0_CTRL_EN))
		l2x0_enable(base, l2x0_saved_regs.aux_ctrl, 0);
}

static const struct l2c_init_data l2x0_init_fns __initconst = {
	.enable = l2x0_enable,
	.outer_cache = {
		.inv_range = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.flush_all = l2x0_flush_all,
		.disable = l2x0_disable,
		.sync = l2x0_cache_sync,
		.resume = l2x0_resume,
	},
};

/*
 * L2C-310 specific code.
 *
 * Errata:
 * 588369: PL310 R0P0->R1P0, fixed R2P0.
 *	Affects: all clean+invalidate operations
 *	clean and invalidate skips the invalidate step, so we need to issue
 *	separate operations.  We also require the above debug workaround
 *	enclosing this code fragment on affected parts.  On unaffected parts,
 *	we must not use this workaround without the debug register writes
 *	to avoid exposing a problem similar to 727915.
 *
 * 727915: PL310 R2P0->R3P0, fixed R3P1.
 *	Affects: clean+invalidate by way
 *	clean and invalidate by way runs in the background, and a store can
 *	hit the line between the clean operation and invalidate operation,
 *	resulting in the store being lost.
 *
 * 753970: PL310 R3P0, fixed R3P1.
 *	Affects: sync
 *	prevents merging writes after the sync operation, until another L2C
 *	operation is performed (or a number of other conditions.)
 *
 * 769419: PL310 R0P0->R3P1, fixed R3P2.
 *	Affects: store buffer
 *	store buffer is not automatically drained.
 */
static void __init l2c310_save(void __iomem *base)
{
	unsigned revision;

	l2x0_saved_regs.tag_latency = readl_relaxed(base +
		L2X0_TAG_LATENCY_CTRL);
	l2x0_saved_regs.data_latency = readl_relaxed(base +
		L2X0_DATA_LATENCY_CTRL);
	l2x0_saved_regs.filter_end = readl_relaxed(base +
		L2X0_ADDR_FILTER_END);
	l2x0_saved_regs.filter_start = readl_relaxed(base +
		L2X0_ADDR_FILTER_START);

	revision = readl_relaxed(base + L2X0_CACHE_ID) &
			L2X0_CACHE_ID_RTL_MASK;

	/* From r2p0, there is Prefetch offset/control register */
	if (revision >= L310_CACHE_ID_RTL_R2P0)
		l2x0_saved_regs.prefetch_ctrl = readl_relaxed(base +
							L2X0_PREFETCH_CTRL);

	/* From r3p0, there is Power control register */
	if (revision >= L310_CACHE_ID_RTL_R3P0)
		l2x0_saved_regs.pwr_ctrl = readl_relaxed(base +
							L2X0_POWER_CTRL);
}

static void l2c310_resume(void)
{
	void __iomem *base = l2x0_base;

	if (!(readl_relaxed(base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		unsigned revision;

		/* restore pl310 setup */
		writel_relaxed(l2x0_saved_regs.tag_latency,
			       base + L2X0_TAG_LATENCY_CTRL);
		writel_relaxed(l2x0_saved_regs.data_latency,
			       base + L2X0_DATA_LATENCY_CTRL);
		writel_relaxed(l2x0_saved_regs.filter_end,
			       base + L2X0_ADDR_FILTER_END);
		writel_relaxed(l2x0_saved_regs.filter_start,
			       base + L2X0_ADDR_FILTER_START);

		revision = readl_relaxed(base + L2X0_CACHE_ID) &
				L2X0_CACHE_ID_RTL_MASK;

		if (revision >= L310_CACHE_ID_RTL_R2P0)
			writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
				       base + L2X0_PREFETCH_CTRL);
		if (revision >= L310_CACHE_ID_RTL_R3P0)
			writel_relaxed(l2x0_saved_regs.pwr_ctrl,
				       base + L2X0_POWER_CTRL);

		l2c_enable(base, l2x0_saved_regs.aux_ctrl, 8);
	}
}

static void __init l2c310_fixup(void __iomem *base, u32 cache_id,
	struct outer_cache_fns *fns)
{
	unsigned revision = cache_id & L2X0_CACHE_ID_RTL_MASK;
	const char *errata[4];
	unsigned n = 0;

	if (revision <= L310_CACHE_ID_RTL_R3P0)
		fns->set_debug = pl310_set_debug;

	if (IS_ENABLED(CONFIG_PL310_ERRATA_753970) &&
	    revision == L310_CACHE_ID_RTL_R3P0) {
		sync_reg_offset = L2X0_DUMMY_REG;
		errata[n++] = "753970";
	}

	if (IS_ENABLED(CONFIG_PL310_ERRATA_769419))
		errata[n++] = "769419";

	if (n) {
		unsigned i;

		pr_info("L2C-310 errat%s", n > 1 ? "a" : "um");
		for (i = 0; i < n; i++)
			pr_cont(" %s", errata[i]);
		pr_cont(" enabled\n");
	}
}

static const struct l2c_init_data l2c310_init_fns __initconst = {
	.num_lock = 8,
	.enable = l2c_enable,
	.fixup = l2c310_fixup,
	.save = l2c310_save,
	.outer_cache = {
		.inv_range = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.flush_all = l2x0_flush_all,
		.disable = l2x0_disable,
		.sync = l2x0_cache_sync,
		.resume = l2c310_resume,
	},
};

static void __init __l2c_init(const struct l2c_init_data *data,
	u32 aux_val, u32 aux_mask, u32 cache_id)
{
	struct outer_cache_fns fns;
	u32 aux;
	u32 way_size = 0;
	int ways;
	int way_size_shift = L2X0_WAY_SIZE_SHIFT;
	const char *type;

	/*
	 * It is strange to save the register state before initialisation,
	 * but hey, this is what the DT implementations decided to do.
	 */
	if (data->save)
		data->save(l2x0_base);

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
		break;

	case L2X0_CACHE_ID_PART_L210:
		ways = (aux >> 13) & 0xf;
		type = "L210";
		break;

	case AURORA_CACHE_ID:
		ways = (aux >> 13) & 0xf;
		ways = 2 << ((ways + 1) >> 2);
		way_size_shift = AURORA_WAY_SIZE_SHIFT;
		type = "Aurora";
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
	way_size = 1 << (way_size + way_size_shift);

	l2x0_size = ways * way_size * SZ_1K;

	fns = data->outer_cache;
	if (data->fixup)
		data->fixup(l2x0_base, cache_id, &fns);

	/*
	 * Check if l2x0 controller is already enabled.  If we are booting
	 * in non-secure mode accessing the below registers will fault.
	 */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN))
		data->enable(l2x0_base, aux, data->num_lock);

	/* Re-read it in case some bits are reserved. */
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);

	/* Save the value for resuming. */
	l2x0_saved_regs.aux_ctrl = aux;

	outer_cache = fns;

	pr_info("%s cache controller enabled, %d ways, %d kB\n",
		type, ways, l2x0_size >> 10);
	pr_info("%s: CACHE_ID 0x%08x, AUX_CTRL 0x%08x\n",
		type, cache_id, aux);
}

void __init l2x0_init(void __iomem *base, u32 aux_val, u32 aux_mask)
{
	const struct l2c_init_data *data;
	u32 cache_id;

	l2x0_base = base;

	cache_id = readl_relaxed(base + L2X0_CACHE_ID);

	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
	default:
		data = &l2x0_init_fns;
		break;

	case L2X0_CACHE_ID_PART_L310:
		data = &l2c310_init_fns;
		break;
	}

	__l2c_init(data, aux_val, aux_mask, cache_id);
}

#ifdef CONFIG_OF
static int l2_wt_override;

/* Aurora don't have the cache ID register available, so we have to
 * pass it though the device tree */
static u32 cache_id_part_number_from_dt;

static void __init l2x0_of_parse(const struct device_node *np,
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

static const struct l2c_init_data of_l2x0_data __initconst = {
	.of_parse = l2x0_of_parse,
	.enable = l2x0_enable,
	.outer_cache = {
		.inv_range   = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.flush_all   = l2x0_flush_all,
		.disable     = l2x0_disable,
		.sync        = l2x0_cache_sync,
		.resume      = l2x0_resume,
	},
};

static void __init pl310_of_parse(const struct device_node *np,
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

static const struct l2c_init_data of_pl310_data __initconst = {
	.num_lock = 8,
	.of_parse = pl310_of_parse,
	.enable = l2c_enable,
	.fixup = l2c310_fixup,
	.save  = l2c310_save,
	.outer_cache = {
		.inv_range   = l2x0_inv_range,
		.clean_range = l2x0_clean_range,
		.flush_range = l2x0_flush_range,
		.flush_all   = l2x0_flush_all,
		.disable     = l2x0_disable,
		.sync        = l2x0_cache_sync,
		.resume      = l2c310_resume,
	},
};

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

static void aurora_save(void __iomem *base)
{
	l2x0_saved_regs.ctrl = readl_relaxed(base + L2X0_CTRL);
	l2x0_saved_regs.aux_ctrl = readl_relaxed(base + L2X0_AUX_CTRL);
}

static void aurora_resume(void)
{
	void __iomem *base = l2x0_base;

	if (!(readl(base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		writel_relaxed(l2x0_saved_regs.aux_ctrl, base + L2X0_AUX_CTRL);
		writel_relaxed(l2x0_saved_regs.ctrl, base + L2X0_CTRL);
	}
}

/*
 * For Aurora cache in no outer mode, enable via the CP15 coprocessor
 * broadcasting of cache commands to L2.
 */
static void __init aurora_enable_no_outer(void __iomem *base, u32 aux,
	unsigned num_lock)
{
	u32 u;

	asm volatile("mrc p15, 1, %0, c15, c2, 0" : "=r" (u));
	u |= AURORA_CTRL_FW;		/* Set the FW bit */
	asm volatile("mcr p15, 1, %0, c15, c2, 0" : : "r" (u));

	isb();

	l2c_enable(base, aux, num_lock);
}

static void __init aurora_fixup(void __iomem *base, u32 cache_id,
	struct outer_cache_fns *fns)
{
	sync_reg_offset = AURORA_SYNC_REG;
}

static void __init aurora_of_parse(const struct device_node *np,
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

static const struct l2c_init_data of_aurora_with_outer_data __initconst = {
	.num_lock = 4,
	.of_parse = aurora_of_parse,
	.enable = l2c_enable,
	.fixup = aurora_fixup,
	.save  = aurora_save,
	.outer_cache = {
		.inv_range   = aurora_inv_range,
		.clean_range = aurora_clean_range,
		.flush_range = aurora_flush_range,
		.flush_all   = l2x0_flush_all,
		.disable     = l2x0_disable,
		.sync        = l2x0_cache_sync,
		.resume      = aurora_resume,
	},
};

static const struct l2c_init_data of_aurora_no_outer_data __initconst = {
	.num_lock = 4,
	.of_parse = aurora_of_parse,
	.enable = aurora_enable_no_outer,
	.fixup = aurora_fixup,
	.save  = aurora_save,
	.outer_cache = {
		.resume      = aurora_resume,
	},
};

/*
 * For certain Broadcom SoCs, depending on the address range, different offsets
 * need to be added to the address before passing it to L2 for
 * invalidation/clean/flush
 *
 * Section Address Range              Offset        EMI
 *   1     0x00000000 - 0x3FFFFFFF    0x80000000    VC
 *   2     0x40000000 - 0xBFFFFFFF    0x40000000    SYS
 *   3     0xC0000000 - 0xFFFFFFFF    0x80000000    VC
 *
 * When the start and end addresses have crossed two different sections, we
 * need to break the L2 operation into two, each within its own section.
 * For example, if we need to invalidate addresses starts at 0xBFFF0000 and
 * ends at 0xC0001000, we need do invalidate 1) 0xBFFF0000 - 0xBFFFFFFF and 2)
 * 0xC0000000 - 0xC0001000
 *
 * Note 1:
 * By breaking a single L2 operation into two, we may potentially suffer some
 * performance hit, but keep in mind the cross section case is very rare
 *
 * Note 2:
 * We do not need to handle the case when the start address is in
 * Section 1 and the end address is in Section 3, since it is not a valid use
 * case
 *
 * Note 3:
 * Section 1 in practical terms can no longer be used on rev A2. Because of
 * that the code does not need to handle section 1 at all.
 *
 */
#define BCM_SYS_EMI_START_ADDR        0x40000000UL
#define BCM_VC_EMI_SEC3_START_ADDR    0xC0000000UL

#define BCM_SYS_EMI_OFFSET            0x40000000UL
#define BCM_VC_EMI_OFFSET             0x80000000UL

static inline int bcm_addr_is_sys_emi(unsigned long addr)
{
	return (addr >= BCM_SYS_EMI_START_ADDR) &&
		(addr < BCM_VC_EMI_SEC3_START_ADDR);
}

static inline unsigned long bcm_l2_phys_addr(unsigned long addr)
{
	if (bcm_addr_is_sys_emi(addr))
		return addr + BCM_SYS_EMI_OFFSET;
	else
		return addr + BCM_VC_EMI_OFFSET;
}

static void bcm_inv_range(unsigned long start, unsigned long end)
{
	unsigned long new_start, new_end;

	BUG_ON(start < BCM_SYS_EMI_START_ADDR);

	if (unlikely(end <= start))
		return;

	new_start = bcm_l2_phys_addr(start);
	new_end = bcm_l2_phys_addr(end);

	/* normal case, no cross section between start and end */
	if (likely(bcm_addr_is_sys_emi(end) || !bcm_addr_is_sys_emi(start))) {
		l2x0_inv_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2x0_inv_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2x0_inv_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

static void bcm_clean_range(unsigned long start, unsigned long end)
{
	unsigned long new_start, new_end;

	BUG_ON(start < BCM_SYS_EMI_START_ADDR);

	if (unlikely(end <= start))
		return;

	if ((end - start) >= l2x0_size) {
		l2x0_clean_all();
		return;
	}

	new_start = bcm_l2_phys_addr(start);
	new_end = bcm_l2_phys_addr(end);

	/* normal case, no cross section between start and end */
	if (likely(bcm_addr_is_sys_emi(end) || !bcm_addr_is_sys_emi(start))) {
		l2x0_clean_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2x0_clean_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2x0_clean_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

static void bcm_flush_range(unsigned long start, unsigned long end)
{
	unsigned long new_start, new_end;

	BUG_ON(start < BCM_SYS_EMI_START_ADDR);

	if (unlikely(end <= start))
		return;

	if ((end - start) >= l2x0_size) {
		l2x0_flush_all();
		return;
	}

	new_start = bcm_l2_phys_addr(start);
	new_end = bcm_l2_phys_addr(end);

	/* normal case, no cross section between start and end */
	if (likely(bcm_addr_is_sys_emi(end) || !bcm_addr_is_sys_emi(start))) {
		l2x0_flush_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2x0_flush_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2x0_flush_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

static const struct l2c_init_data of_bcm_l2x0_data __initconst = {
	.num_lock = 8,
	.of_parse = pl310_of_parse,
	.enable = l2c_enable,
	.fixup = l2c310_fixup,
	.save  = l2c310_save,
	.outer_cache = {
		.inv_range   = bcm_inv_range,
		.clean_range = bcm_clean_range,
		.flush_range = bcm_flush_range,
		.flush_all   = l2x0_flush_all,
		.disable     = l2x0_disable,
		.sync        = l2x0_cache_sync,
		.resume      = l2c310_resume,
	},
};

static void __init tauros3_save(void __iomem *base)
{
	l2x0_saved_regs.aux2_ctrl =
		readl_relaxed(base + TAUROS3_AUX2_CTRL);
	l2x0_saved_regs.prefetch_ctrl =
		readl_relaxed(base + L2X0_PREFETCH_CTRL);
}

static void tauros3_resume(void)
{
	void __iomem *base = l2x0_base;

	if (!(readl_relaxed(base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		writel_relaxed(l2x0_saved_regs.aux2_ctrl,
			       base + TAUROS3_AUX2_CTRL);
		writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
			       base + L2X0_PREFETCH_CTRL);

		l2c_enable(base, l2x0_saved_regs.aux_ctrl, 8);
	}
}

static const struct l2c_init_data of_tauros3_data __initconst = {
	.num_lock = 8,
	.enable = l2c_enable,
	.save  = tauros3_save,
	/* Tauros3 broadcasts L1 cache operations to L2 */
	.outer_cache = {
		.resume      = tauros3_resume,
	},
};

#define L2C_ID(name, fns) { .compatible = name, .data = (void *)&fns }
static const struct of_device_id l2x0_ids[] __initconst = {
	L2C_ID("arm,l210-cache", of_l2x0_data),
	L2C_ID("arm,l220-cache", of_l2x0_data),
	L2C_ID("arm,pl310-cache", of_pl310_data),
	L2C_ID("brcm,bcm11351-a2-pl310-cache", of_bcm_l2x0_data),
	L2C_ID("marvell,aurora-outer-cache", of_aurora_with_outer_data),
	L2C_ID("marvell,aurora-system-cache", of_aurora_no_outer_data),
	L2C_ID("marvell,tauros3-cache", of_tauros3_data),
	/* Deprecated IDs */
	L2C_ID("bcm,bcm11351-a2-pl310-cache", of_bcm_l2x0_data),
	{}
};

int __init l2x0_of_init(u32 aux_val, u32 aux_mask)
{
	const struct l2c_init_data *data;
	struct device_node *np;
	struct resource res;
	u32 cache_id;

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
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN))
		if (data->of_parse)
			data->of_parse(np, &aux_val, &aux_mask);

	if (cache_id_part_number_from_dt)
		cache_id = cache_id_part_number_from_dt;
	else
		cache_id = readl_relaxed(l2x0_base + L2X0_CACHE_ID);

	__l2c_init(data, aux_val, aux_mask, cache_id);

	return 0;
}
#endif
