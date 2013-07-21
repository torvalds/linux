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

#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static DEFINE_RAW_SPINLOCK(l2x0_lock);
static u32 l2x0_way_mask;	/* Bitmask of active ways */
static u32 l2x0_size;
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
	writel_relaxed(0, l2x0_base + L2X0_CTRL);
	dsb();
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_unlock(u32 cache_id)
{
	int lockregs;
	int i;

	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
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
	u32 aux;
	u32 cache_id;
	u32 way_size = 0;
	int ways;
	int way_size_shift = L2X0_WAY_SIZE_SHIFT;
	const char *type;

	l2x0_base = base;
	if (cache_id_part_number_from_dt)
		cache_id = cache_id_part_number_from_dt;
	else
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
		if ((cache_id & L2X0_CACHE_ID_RTL_MASK) <= L2X0_CACHE_ID_RTL_R3P0)
			outer_cache.set_debug = pl310_set_debug;
		break;
	case L2X0_CACHE_ID_PART_L210:
		ways = (aux >> 13) & 0xf;
		type = "L210";
		break;

	case AURORA_CACHE_ID:
		sync_reg_offset = AURORA_SYNC_REG;
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

	/*
	 * Check if l2x0 controller is already enabled.
	 * If you are booting from non-secure mode
	 * accessing the below registers will fault.
	 */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		/* Make sure that I&D is not locked down when starting */
		l2x0_unlock(cache_id);

		/* l2x0 controller is disabled */
		writel_relaxed(aux, l2x0_base + L2X0_AUX_CTRL);

		l2x0_inv_all();

		/* enable L2X0 */
		writel_relaxed(L2X0_CTRL_EN, l2x0_base + L2X0_CTRL);
	}

	/* Re-read it in case some bits are reserved. */
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);

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
	printk(KERN_INFO "l2x0: %d ways, CACHE_ID 0x%08x, AUX_CTRL 0x%08x, Cache size: %d B\n",
			ways, cache_id, aux, l2x0_size);
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

		writel_relaxed(l2x0_saved_regs.aux_ctrl, l2x0_base +
			L2X0_AUX_CTRL);

		l2x0_inv_all();

		writel_relaxed(L2X0_CTRL_EN, l2x0_base + L2X0_CTRL);
	}
}

static void pl310_resume(void)
{
	u32 l2x0_revision;

	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
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

static void aurora_resume(void)
{
	if (!(readl(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		writel_relaxed(l2x0_saved_regs.aux_ctrl,
				l2x0_base + L2X0_AUX_CTRL);
		writel_relaxed(l2x0_saved_regs.ctrl, l2x0_base + L2X0_CTRL);
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

static const struct l2x0_of_data bcm_l2x0_data = {
	.setup = pl310_of_setup,
	.save  = pl310_save,
	.outer_cache = {
		.resume      = pl310_resume,
		.inv_range   = bcm_inv_range,
		.clean_range = bcm_clean_range,
		.flush_range = bcm_flush_range,
		.sync        = l2x0_cache_sync,
		.flush_all   = l2x0_flush_all,
		.inv_all     = l2x0_inv_all,
		.disable     = l2x0_disable,
	},
};

static const struct of_device_id l2x0_ids[] __initconst = {
	{ .compatible = "arm,pl310-cache", .data = (void *)&pl310_data },
	{ .compatible = "arm,l220-cache", .data = (void *)&l2x0_data },
	{ .compatible = "arm,l210-cache", .data = (void *)&l2x0_data },
	{ .compatible = "marvell,aurora-system-cache",
	  .data = (void *)&aurora_no_outer_data},
	{ .compatible = "marvell,aurora-outer-cache",
	  .data = (void *)&aurora_with_outer_data},
	{ .compatible = "bcm,bcm11351-a2-pl310-cache",
	  .data = (void *)&bcm_l2x0_data},
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

	l2x0_base = ioremap(res.start, resource_size(&res));
	if (!l2x0_base)
		return -ENOMEM;

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
	memcpy(&outer_cache, &data->outer_cache, sizeof(outer_cache));
	l2x0_init(l2x0_base, aux_val, aux_mask);

	return 0;
}
#endif
