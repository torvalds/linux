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
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static DEFINE_SPINLOCK(l2x0_lock);
static uint32_t l2x0_way_mask;	/* Bitmask of active ways */

static inline void cache_wait(void __iomem *reg, unsigned long mask)
{
	/* wait for the operation to complete */
	while (readl(reg) & mask)
		;
}

static inline void cache_sync(void)
{
	void __iomem *base = l2x0_base;
	writel(0, base + L2X0_CACHE_SYNC);
	cache_wait(base + L2X0_CACHE_SYNC, 1);
}

static inline void l2x0_clean_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	writel(addr, base + L2X0_CLEAN_LINE_PA);
}

static inline void l2x0_inv_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	writel(addr, base + L2X0_INV_LINE_PA);
}

#ifdef CONFIG_PL310_ERRATA_588369
static void debug_writel(unsigned long val)
{
	extern void omap_smc1(u32 fn, u32 arg);

	/*
	 * Texas Instrument secure monitor api to modify the
	 * PL310 Debug Control Register.
	 */
	omap_smc1(0x100, val);
}

static inline void l2x0_flush_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;

	/* Clean by PA followed by Invalidate by PA */
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	writel(addr, base + L2X0_CLEAN_LINE_PA);
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	writel(addr, base + L2X0_INV_LINE_PA);
}
#else

/* Optimised out for non-errata case */
static inline void debug_writel(unsigned long val)
{
}

static inline void l2x0_flush_line(unsigned long addr)
{
	void __iomem *base = l2x0_base;
	cache_wait(base + L2X0_CLEAN_INV_LINE_PA, 1);
	writel(addr, base + L2X0_CLEAN_INV_LINE_PA);
}
#endif

static void l2x0_cache_sync(void)
{
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static inline void l2x0_inv_all(void)
{
	unsigned long flags;

	/* invalidate all ways */
	spin_lock_irqsave(&l2x0_lock, flags);
	writel(l2x0_way_mask, l2x0_base + L2X0_INV_WAY);
	cache_wait(l2x0_base + L2X0_INV_WAY, l2x0_way_mask);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_inv_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
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
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_INV_LINE_PA, 1);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_clean_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			l2x0_clean_line(start);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_CLEAN_LINE_PA, 1);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2x0_flush_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	spin_lock_irqsave(&l2x0_lock, flags);
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
			spin_unlock_irqrestore(&l2x0_lock, flags);
			spin_lock_irqsave(&l2x0_lock, flags);
		}
	}
	cache_wait(base + L2X0_CLEAN_INV_LINE_PA, 1);
	cache_sync();
	spin_unlock_irqrestore(&l2x0_lock, flags);
}

void __init l2x0_init(void __iomem *base, __u32 aux_val, __u32 aux_mask)
{
	__u32 aux;
	__u32 cache_id;
	int ways;
	const char *type;

	l2x0_base = base;

	cache_id = readl(l2x0_base + L2X0_CACHE_ID);
	aux = readl(l2x0_base + L2X0_AUX_CTRL);

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
	default:
		/* Assume unknown chips have 8 ways */
		ways = 8;
		type = "L2x0 series";
		break;
	}

	l2x0_way_mask = (1 << ways) - 1;

	/*
	 * Check if l2x0 controller is already enabled.
	 * If you are booting from non-secure mode
	 * accessing the below registers will fault.
	 */
	if (!(readl(l2x0_base + L2X0_CTRL) & 1)) {

		/* l2x0 controller is disabled */
		aux &= aux_mask;
		aux |= aux_val;
		writel(aux, l2x0_base + L2X0_AUX_CTRL);

		l2x0_inv_all();

		/* enable L2X0 */
		writel(1, l2x0_base + L2X0_CTRL);
	}

	outer_cache.inv_range = l2x0_inv_range;
	outer_cache.clean_range = l2x0_clean_range;
	outer_cache.flush_range = l2x0_flush_range;
	outer_cache.sync = l2x0_cache_sync;

	printk(KERN_INFO "%s cache controller enabled\n", type);
	printk(KERN_INFO "l2x0: %d ways, CACHE_ID 0x%08x, AUX_CTRL 0x%08x\n",
			 ways, cache_id, aux);
}
