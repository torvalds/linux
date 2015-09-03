/*
 * arch/arm/mm/cache-l2x0.c - L210/L220/L310 cache controller support
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
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/log2.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/hardware/cache-l2x0.h>
#include "cache-tauros3.h"
#include "cache-aurora-l2.h"

struct l2c_init_data {
	const char *type;
	unsigned way_size_0;
	unsigned num_lock;
	void (*of_parse)(const struct device_node *, u32 *, u32 *);
	void (*enable)(void __iomem *, unsigned);
	void (*fixup)(void __iomem *, u32, struct outer_cache_fns *);
	void (*save)(void __iomem *);
	void (*configure)(void __iomem *);
	void (*unlock)(void __iomem *, unsigned);
	struct outer_cache_fns outer_cache;
};

#define CACHE_LINE_SIZE		32

static void __iomem *l2x0_base;
static const struct l2c_init_data *l2x0_data;
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
 * By default, we write directly to secure registers.  Platforms must
 * override this if they are running non-secure.
 */
static void l2c_write_sec(unsigned long val, void __iomem *base, unsigned reg)
{
	if (val == readl_relaxed(base + reg))
		return;
	if (outer_cache.write_sec)
		outer_cache.write_sec(val, reg);
	else
		writel_relaxed(val, base + reg);
}

/*
 * This should only be called when we have a requirement that the
 * register be written due to a work-around, as platforms running
 * in non-secure mode may not be able to access this register.
 */
static inline void l2c_set_debug(void __iomem *base, unsigned long val)
{
	l2c_write_sec(val, base, L2X0_DEBUG_CTRL);
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

static void l2c_configure(void __iomem *base)
{
	l2c_write_sec(l2x0_saved_regs.aux_ctrl, base, L2X0_AUX_CTRL);
}

/*
 * Enable the L2 cache controller.  This function must only be
 * called when the cache controller is known to be disabled.
 */
static void l2c_enable(void __iomem *base, unsigned num_lock)
{
	unsigned long flags;

	if (outer_cache.configure)
		outer_cache.configure(&l2x0_saved_regs);
	else
		l2x0_data->configure(base);

	l2x0_data->unlock(base, num_lock);

	local_irq_save(flags);
	__l2c_op_way(base + L2X0_INV_WAY);
	writel_relaxed(0, base + sync_reg_offset);
	l2c_wait_mask(base + sync_reg_offset, 1);
	local_irq_restore(flags);

	l2c_write_sec(L2X0_CTRL_EN, base, L2X0_CTRL);
}

static void l2c_disable(void)
{
	void __iomem *base = l2x0_base;

	outer_cache.flush_all();
	l2c_write_sec(0, base, L2X0_CTRL);
	dsb(st);
}

static void l2c_save(void __iomem *base)
{
	l2x0_saved_regs.aux_ctrl = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);
}

static void l2c_resume(void)
{
	void __iomem *base = l2x0_base;

	/* Do not touch the controller if already enabled. */
	if (!(readl_relaxed(base + L2X0_CTRL) & L2X0_CTRL_EN))
		l2c_enable(base, l2x0_data->num_lock);
}

/*
 * L2C-210 specific code.
 *
 * The L2C-2x0 PA, set/way and sync operations are atomic, but we must
 * ensure that no background operation is running.  The way operations
 * are all background tasks.
 *
 * While a background operation is in progress, any new operation is
 * ignored (unspecified whether this causes an error.)  Thankfully, not
 * used on SMP.
 *
 * Never has a different sync register other than L2X0_CACHE_SYNC, but
 * we use sync_reg_offset here so we can share some of this with L2C-310.
 */
static void __l2c210_cache_sync(void __iomem *base)
{
	writel_relaxed(0, base + sync_reg_offset);
}

static void __l2c210_op_pa_range(void __iomem *reg, unsigned long start,
	unsigned long end)
{
	while (start < end) {
		writel_relaxed(start, reg);
		start += CACHE_LINE_SIZE;
	}
}

static void l2c210_inv_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;

	if (start & (CACHE_LINE_SIZE - 1)) {
		start &= ~(CACHE_LINE_SIZE - 1);
		writel_relaxed(start, base + L2X0_CLEAN_INV_LINE_PA);
		start += CACHE_LINE_SIZE;
	}

	if (end & (CACHE_LINE_SIZE - 1)) {
		end &= ~(CACHE_LINE_SIZE - 1);
		writel_relaxed(end, base + L2X0_CLEAN_INV_LINE_PA);
	}

	__l2c210_op_pa_range(base + L2X0_INV_LINE_PA, start, end);
	__l2c210_cache_sync(base);
}

static void l2c210_clean_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;

	start &= ~(CACHE_LINE_SIZE - 1);
	__l2c210_op_pa_range(base + L2X0_CLEAN_LINE_PA, start, end);
	__l2c210_cache_sync(base);
}

static void l2c210_flush_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;

	start &= ~(CACHE_LINE_SIZE - 1);
	__l2c210_op_pa_range(base + L2X0_CLEAN_INV_LINE_PA, start, end);
	__l2c210_cache_sync(base);
}

static void l2c210_flush_all(void)
{
	void __iomem *base = l2x0_base;

	BUG_ON(!irqs_disabled());

	__l2c_op_way(base + L2X0_CLEAN_INV_WAY);
	__l2c210_cache_sync(base);
}

static void l2c210_sync(void)
{
	__l2c210_cache_sync(l2x0_base);
}

static const struct l2c_init_data l2c210_data __initconst = {
	.type = "L2C-210",
	.way_size_0 = SZ_8K,
	.num_lock = 1,
	.enable = l2c_enable,
	.save = l2c_save,
	.configure = l2c_configure,
	.unlock = l2c_unlock,
	.outer_cache = {
		.inv_range = l2c210_inv_range,
		.clean_range = l2c210_clean_range,
		.flush_range = l2c210_flush_range,
		.flush_all = l2c210_flush_all,
		.disable = l2c_disable,
		.sync = l2c210_sync,
		.resume = l2c_resume,
	},
};

/*
 * L2C-220 specific code.
 *
 * All operations are background operations: they have to be waited for.
 * Conflicting requests generate a slave error (which will cause an
 * imprecise abort.)  Never uses sync_reg_offset, so we hard-code the
 * sync register here.
 *
 * However, we can re-use the l2c210_resume call.
 */
static inline void __l2c220_cache_sync(void __iomem *base)
{
	writel_relaxed(0, base + L2X0_CACHE_SYNC);
	l2c_wait_mask(base + L2X0_CACHE_SYNC, 1);
}

static void l2c220_op_way(void __iomem *base, unsigned reg)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2c_op_way(base + reg);
	__l2c220_cache_sync(base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static unsigned long l2c220_op_pa_range(void __iomem *reg, unsigned long start,
	unsigned long end, unsigned long flags)
{
	raw_spinlock_t *lock = &l2x0_lock;

	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		while (start < blk_end) {
			l2c_wait_mask(reg, 1);
			writel_relaxed(start, reg);
			start += CACHE_LINE_SIZE;
		}

		if (blk_end < end) {
			raw_spin_unlock_irqrestore(lock, flags);
			raw_spin_lock_irqsave(lock, flags);
		}
	}

	return flags;
}

static void l2c220_inv_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	if ((start | end) & (CACHE_LINE_SIZE - 1)) {
		if (start & (CACHE_LINE_SIZE - 1)) {
			start &= ~(CACHE_LINE_SIZE - 1);
			writel_relaxed(start, base + L2X0_CLEAN_INV_LINE_PA);
			start += CACHE_LINE_SIZE;
		}

		if (end & (CACHE_LINE_SIZE - 1)) {
			end &= ~(CACHE_LINE_SIZE - 1);
			l2c_wait_mask(base + L2X0_CLEAN_INV_LINE_PA, 1);
			writel_relaxed(end, base + L2X0_CLEAN_INV_LINE_PA);
		}
	}

	flags = l2c220_op_pa_range(base + L2X0_INV_LINE_PA,
				   start, end, flags);
	l2c_wait_mask(base + L2X0_INV_LINE_PA, 1);
	__l2c220_cache_sync(base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2c220_clean_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	start &= ~(CACHE_LINE_SIZE - 1);
	if ((end - start) >= l2x0_size) {
		l2c220_op_way(base, L2X0_CLEAN_WAY);
		return;
	}

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	flags = l2c220_op_pa_range(base + L2X0_CLEAN_LINE_PA,
				   start, end, flags);
	l2c_wait_mask(base + L2X0_CLEAN_INV_LINE_PA, 1);
	__l2c220_cache_sync(base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2c220_flush_range(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	start &= ~(CACHE_LINE_SIZE - 1);
	if ((end - start) >= l2x0_size) {
		l2c220_op_way(base, L2X0_CLEAN_INV_WAY);
		return;
	}

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	flags = l2c220_op_pa_range(base + L2X0_CLEAN_INV_LINE_PA,
				   start, end, flags);
	l2c_wait_mask(base + L2X0_CLEAN_INV_LINE_PA, 1);
	__l2c220_cache_sync(base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2c220_flush_all(void)
{
	l2c220_op_way(l2x0_base, L2X0_CLEAN_INV_WAY);
}

static void l2c220_sync(void)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2c220_cache_sync(l2x0_base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void l2c220_enable(void __iomem *base, unsigned num_lock)
{
	/*
	 * Always enable non-secure access to the lockdown registers -
	 * we write to them as part of the L2C enable sequence so they
	 * need to be accessible.
	 */
	l2x0_saved_regs.aux_ctrl |= L220_AUX_CTRL_NS_LOCKDOWN;

	l2c_enable(base, num_lock);
}

static void l2c220_unlock(void __iomem *base, unsigned num_lock)
{
	if (readl_relaxed(base + L2X0_AUX_CTRL) & L220_AUX_CTRL_NS_LOCKDOWN)
		l2c_unlock(base, num_lock);
}

static const struct l2c_init_data l2c220_data = {
	.type = "L2C-220",
	.way_size_0 = SZ_8K,
	.num_lock = 1,
	.enable = l2c220_enable,
	.save = l2c_save,
	.configure = l2c_configure,
	.unlock = l2c220_unlock,
	.outer_cache = {
		.inv_range = l2c220_inv_range,
		.clean_range = l2c220_clean_range,
		.flush_range = l2c220_flush_range,
		.flush_all = l2c220_flush_all,
		.disable = l2c_disable,
		.sync = l2c220_sync,
		.resume = l2c_resume,
	},
};

/*
 * L2C-310 specific code.
 *
 * Very similar to L2C-210, the PA, set/way and sync operations are atomic,
 * and the way operations are all background tasks.  However, issuing an
 * operation while a background operation is in progress results in a
 * SLVERR response.  We can reuse:
 *
 *  __l2c210_cache_sync (using sync_reg_offset)
 *  l2c210_sync
 *  l2c210_inv_range (if 588369 is not applicable)
 *  l2c210_clean_range
 *  l2c210_flush_range (if 588369 is not applicable)
 *  l2c210_flush_all (if 727915 is not applicable)
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
 * 752271: PL310 R3P0->R3P1-50REL0, fixed R3P2.
 *	Affects: 8x64-bit (double fill) line fetches
 *	double fill line fetches can fail to cause dirty data to be evicted
 *	from the cache before the new data overwrites the second line.
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
static void l2c310_inv_range_erratum(unsigned long start, unsigned long end)
{
	void __iomem *base = l2x0_base;

	if ((start | end) & (CACHE_LINE_SIZE - 1)) {
		unsigned long flags;

		/* Erratum 588369 for both clean+invalidate operations */
		raw_spin_lock_irqsave(&l2x0_lock, flags);
		l2c_set_debug(base, 0x03);

		if (start & (CACHE_LINE_SIZE - 1)) {
			start &= ~(CACHE_LINE_SIZE - 1);
			writel_relaxed(start, base + L2X0_CLEAN_LINE_PA);
			writel_relaxed(start, base + L2X0_INV_LINE_PA);
			start += CACHE_LINE_SIZE;
		}

		if (end & (CACHE_LINE_SIZE - 1)) {
			end &= ~(CACHE_LINE_SIZE - 1);
			writel_relaxed(end, base + L2X0_CLEAN_LINE_PA);
			writel_relaxed(end, base + L2X0_INV_LINE_PA);
		}

		l2c_set_debug(base, 0x00);
		raw_spin_unlock_irqrestore(&l2x0_lock, flags);
	}

	__l2c210_op_pa_range(base + L2X0_INV_LINE_PA, start, end);
	__l2c210_cache_sync(base);
}

static void l2c310_flush_range_erratum(unsigned long start, unsigned long end)
{
	raw_spinlock_t *lock = &l2x0_lock;
	unsigned long flags;
	void __iomem *base = l2x0_base;

	raw_spin_lock_irqsave(lock, flags);
	while (start < end) {
		unsigned long blk_end = start + min(end - start, 4096UL);

		l2c_set_debug(base, 0x03);
		while (start < blk_end) {
			writel_relaxed(start, base + L2X0_CLEAN_LINE_PA);
			writel_relaxed(start, base + L2X0_INV_LINE_PA);
			start += CACHE_LINE_SIZE;
		}
		l2c_set_debug(base, 0x00);

		if (blk_end < end) {
			raw_spin_unlock_irqrestore(lock, flags);
			raw_spin_lock_irqsave(lock, flags);
		}
	}
	raw_spin_unlock_irqrestore(lock, flags);
	__l2c210_cache_sync(base);
}

static void l2c310_flush_all_erratum(void)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	l2c_set_debug(base, 0x03);
	__l2c_op_way(base + L2X0_CLEAN_INV_WAY);
	l2c_set_debug(base, 0x00);
	__l2c210_cache_sync(base);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void __init l2c310_save(void __iomem *base)
{
	unsigned revision;

	l2c_save(base);

	l2x0_saved_regs.tag_latency = readl_relaxed(base +
		L310_TAG_LATENCY_CTRL);
	l2x0_saved_regs.data_latency = readl_relaxed(base +
		L310_DATA_LATENCY_CTRL);
	l2x0_saved_regs.filter_end = readl_relaxed(base +
		L310_ADDR_FILTER_END);
	l2x0_saved_regs.filter_start = readl_relaxed(base +
		L310_ADDR_FILTER_START);

	revision = readl_relaxed(base + L2X0_CACHE_ID) &
			L2X0_CACHE_ID_RTL_MASK;

	/* From r2p0, there is Prefetch offset/control register */
	if (revision >= L310_CACHE_ID_RTL_R2P0)
		l2x0_saved_regs.prefetch_ctrl = readl_relaxed(base +
							L310_PREFETCH_CTRL);

	/* From r3p0, there is Power control register */
	if (revision >= L310_CACHE_ID_RTL_R3P0)
		l2x0_saved_regs.pwr_ctrl = readl_relaxed(base +
							L310_POWER_CTRL);
}

static void l2c310_configure(void __iomem *base)
{
	unsigned revision;

	l2c_configure(base);

	/* restore pl310 setup */
	l2c_write_sec(l2x0_saved_regs.tag_latency, base,
		      L310_TAG_LATENCY_CTRL);
	l2c_write_sec(l2x0_saved_regs.data_latency, base,
		      L310_DATA_LATENCY_CTRL);
	l2c_write_sec(l2x0_saved_regs.filter_end, base,
		      L310_ADDR_FILTER_END);
	l2c_write_sec(l2x0_saved_regs.filter_start, base,
		      L310_ADDR_FILTER_START);

	revision = readl_relaxed(base + L2X0_CACHE_ID) &
				 L2X0_CACHE_ID_RTL_MASK;

	if (revision >= L310_CACHE_ID_RTL_R2P0)
		l2c_write_sec(l2x0_saved_regs.prefetch_ctrl, base,
			      L310_PREFETCH_CTRL);
	if (revision >= L310_CACHE_ID_RTL_R3P0)
		l2c_write_sec(l2x0_saved_regs.pwr_ctrl, base,
			      L310_POWER_CTRL);
}

static int l2c310_cpu_enable_flz(struct notifier_block *nb, unsigned long act, void *data)
{
	switch (act & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		set_auxcr(get_auxcr() | BIT(3) | BIT(2) | BIT(1));
		break;
	case CPU_DYING:
		set_auxcr(get_auxcr() & ~(BIT(3) | BIT(2) | BIT(1)));
		break;
	}
	return NOTIFY_OK;
}

static void __init l2c310_enable(void __iomem *base, unsigned num_lock)
{
	unsigned rev = readl_relaxed(base + L2X0_CACHE_ID) & L2X0_CACHE_ID_RTL_MASK;
	bool cortex_a9 = read_cpuid_part() == ARM_CPU_PART_CORTEX_A9;
	u32 aux = l2x0_saved_regs.aux_ctrl;

	if (rev >= L310_CACHE_ID_RTL_R2P0) {
		if (cortex_a9) {
			aux |= L310_AUX_CTRL_EARLY_BRESP;
			pr_info("L2C-310 enabling early BRESP for Cortex-A9\n");
		} else if (aux & L310_AUX_CTRL_EARLY_BRESP) {
			pr_warn("L2C-310 early BRESP only supported with Cortex-A9\n");
			aux &= ~L310_AUX_CTRL_EARLY_BRESP;
		}
	}

	if (cortex_a9) {
		u32 aux_cur = readl_relaxed(base + L2X0_AUX_CTRL);
		u32 acr = get_auxcr();

		pr_debug("Cortex-A9 ACR=0x%08x\n", acr);

		if (acr & BIT(3) && !(aux_cur & L310_AUX_CTRL_FULL_LINE_ZERO))
			pr_err("L2C-310: full line of zeros enabled in Cortex-A9 but not L2C-310 - invalid\n");

		if (aux & L310_AUX_CTRL_FULL_LINE_ZERO && !(acr & BIT(3)))
			pr_err("L2C-310: enabling full line of zeros but not enabled in Cortex-A9\n");

		if (!(aux & L310_AUX_CTRL_FULL_LINE_ZERO) && !outer_cache.write_sec) {
			aux |= L310_AUX_CTRL_FULL_LINE_ZERO;
			pr_info("L2C-310 full line of zeros enabled for Cortex-A9\n");
		}
	} else if (aux & (L310_AUX_CTRL_FULL_LINE_ZERO | L310_AUX_CTRL_EARLY_BRESP)) {
		pr_err("L2C-310: disabling Cortex-A9 specific feature bits\n");
		aux &= ~(L310_AUX_CTRL_FULL_LINE_ZERO | L310_AUX_CTRL_EARLY_BRESP);
	}

	/* r3p0 or later has power control register */
	if (rev >= L310_CACHE_ID_RTL_R3P0)
		l2x0_saved_regs.pwr_ctrl = L310_DYNAMIC_CLK_GATING_EN |
						L310_STNDBY_MODE_EN;

	/*
	 * Always enable non-secure access to the lockdown registers -
	 * we write to them as part of the L2C enable sequence so they
	 * need to be accessible.
	 */
	l2x0_saved_regs.aux_ctrl = aux | L310_AUX_CTRL_NS_LOCKDOWN;

	l2c_enable(base, num_lock);

	/* Read back resulting AUX_CTRL value as it could have been altered. */
	aux = readl_relaxed(base + L2X0_AUX_CTRL);

	if (aux & (L310_AUX_CTRL_DATA_PREFETCH | L310_AUX_CTRL_INSTR_PREFETCH)) {
		u32 prefetch = readl_relaxed(base + L310_PREFETCH_CTRL);

		pr_info("L2C-310 %s%s prefetch enabled, offset %u lines\n",
			aux & L310_AUX_CTRL_INSTR_PREFETCH ? "I" : "",
			aux & L310_AUX_CTRL_DATA_PREFETCH ? "D" : "",
			1 + (prefetch & L310_PREFETCH_CTRL_OFFSET_MASK));
	}

	/* r3p0 or later has power control register */
	if (rev >= L310_CACHE_ID_RTL_R3P0) {
		u32 power_ctrl;

		power_ctrl = readl_relaxed(base + L310_POWER_CTRL);
		pr_info("L2C-310 dynamic clock gating %sabled, standby mode %sabled\n",
			power_ctrl & L310_DYNAMIC_CLK_GATING_EN ? "en" : "dis",
			power_ctrl & L310_STNDBY_MODE_EN ? "en" : "dis");
	}

	if (aux & L310_AUX_CTRL_FULL_LINE_ZERO) {
		set_auxcr(get_auxcr() | BIT(3) | BIT(2) | BIT(1));
		cpu_notifier(l2c310_cpu_enable_flz, 0);
	}
}

static void __init l2c310_fixup(void __iomem *base, u32 cache_id,
	struct outer_cache_fns *fns)
{
	unsigned revision = cache_id & L2X0_CACHE_ID_RTL_MASK;
	const char *errata[8];
	unsigned n = 0;

	if (IS_ENABLED(CONFIG_PL310_ERRATA_588369) &&
	    revision < L310_CACHE_ID_RTL_R2P0 &&
	    /* For bcm compatibility */
	    fns->inv_range == l2c210_inv_range) {
		fns->inv_range = l2c310_inv_range_erratum;
		fns->flush_range = l2c310_flush_range_erratum;
		errata[n++] = "588369";
	}

	if (IS_ENABLED(CONFIG_PL310_ERRATA_727915) &&
	    revision >= L310_CACHE_ID_RTL_R2P0 &&
	    revision < L310_CACHE_ID_RTL_R3P1) {
		fns->flush_all = l2c310_flush_all_erratum;
		errata[n++] = "727915";
	}

	if (revision >= L310_CACHE_ID_RTL_R3P0 &&
	    revision < L310_CACHE_ID_RTL_R3P2) {
		u32 val = l2x0_saved_regs.prefetch_ctrl;
		/* I don't think bit23 is required here... but iMX6 does so */
		if (val & (BIT(30) | BIT(23))) {
			val &= ~(BIT(30) | BIT(23));
			l2x0_saved_regs.prefetch_ctrl = val;
			errata[n++] = "752271";
		}
	}

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

static void l2c310_disable(void)
{
	/*
	 * If full-line-of-zeros is enabled, we must first disable it in the
	 * Cortex-A9 auxiliary control register before disabling the L2 cache.
	 */
	if (l2x0_saved_regs.aux_ctrl & L310_AUX_CTRL_FULL_LINE_ZERO)
		set_auxcr(get_auxcr() & ~(BIT(3) | BIT(2) | BIT(1)));

	l2c_disable();
}

static void l2c310_resume(void)
{
	l2c_resume();

	/* Re-enable full-line-of-zeros for Cortex-A9 */
	if (l2x0_saved_regs.aux_ctrl & L310_AUX_CTRL_FULL_LINE_ZERO)
		set_auxcr(get_auxcr() | BIT(3) | BIT(2) | BIT(1));
}

static void l2c310_unlock(void __iomem *base, unsigned num_lock)
{
	if (readl_relaxed(base + L2X0_AUX_CTRL) & L310_AUX_CTRL_NS_LOCKDOWN)
		l2c_unlock(base, num_lock);
}

static const struct l2c_init_data l2c310_init_fns __initconst = {
	.type = "L2C-310",
	.way_size_0 = SZ_8K,
	.num_lock = 8,
	.enable = l2c310_enable,
	.fixup = l2c310_fixup,
	.save = l2c310_save,
	.configure = l2c310_configure,
	.unlock = l2c310_unlock,
	.outer_cache = {
		.inv_range = l2c210_inv_range,
		.clean_range = l2c210_clean_range,
		.flush_range = l2c210_flush_range,
		.flush_all = l2c210_flush_all,
		.disable = l2c310_disable,
		.sync = l2c210_sync,
		.resume = l2c310_resume,
	},
};

static int __init __l2c_init(const struct l2c_init_data *data,
			     u32 aux_val, u32 aux_mask, u32 cache_id)
{
	struct outer_cache_fns fns;
	unsigned way_size_bits, ways;
	u32 aux, old_aux;

	/*
	 * Save the pointer globally so that callbacks which do not receive
	 * context from callers can access the structure.
	 */
	l2x0_data = kmemdup(data, sizeof(*data), GFP_KERNEL);
	if (!l2x0_data)
		return -ENOMEM;

	/*
	 * Sanity check the aux values.  aux_mask is the bits we preserve
	 * from reading the hardware register, and aux_val is the bits we
	 * set.
	 */
	if (aux_val & aux_mask)
		pr_alert("L2C: platform provided aux values permit register corruption.\n");

	old_aux = aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);
	aux &= aux_mask;
	aux |= aux_val;

	if (old_aux != aux)
		pr_warn("L2C: DT/platform modifies aux control register: 0x%08x -> 0x%08x\n",
		        old_aux, aux);

	/* Determine the number of ways */
	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
	case L2X0_CACHE_ID_PART_L310:
		if ((aux_val | ~aux_mask) & (L2C_AUX_CTRL_WAY_SIZE_MASK | L310_AUX_CTRL_ASSOCIATIVITY_16))
			pr_warn("L2C: DT/platform tries to modify or specify cache size\n");
		if (aux & (1 << 16))
			ways = 16;
		else
			ways = 8;
		break;

	case L2X0_CACHE_ID_PART_L210:
	case L2X0_CACHE_ID_PART_L220:
		ways = (aux >> 13) & 0xf;
		break;

	case AURORA_CACHE_ID:
		ways = (aux >> 13) & 0xf;
		ways = 2 << ((ways + 1) >> 2);
		break;

	default:
		/* Assume unknown chips have 8 ways */
		ways = 8;
		break;
	}

	l2x0_way_mask = (1 << ways) - 1;

	/*
	 * way_size_0 is the size that a way_size value of zero would be
	 * given the calculation: way_size = way_size_0 << way_size_bits.
	 * So, if way_size_bits=0 is reserved, but way_size_bits=1 is 16k,
	 * then way_size_0 would be 8k.
	 *
	 * L2 cache size = number of ways * way size.
	 */
	way_size_bits = (aux & L2C_AUX_CTRL_WAY_SIZE_MASK) >>
			L2C_AUX_CTRL_WAY_SIZE_SHIFT;
	l2x0_size = ways * (data->way_size_0 << way_size_bits);

	fns = data->outer_cache;
	fns.write_sec = outer_cache.write_sec;
	fns.configure = outer_cache.configure;
	if (data->fixup)
		data->fixup(l2x0_base, cache_id, &fns);

	/*
	 * Check if l2x0 controller is already enabled.  If we are booting
	 * in non-secure mode accessing the below registers will fault.
	 */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN)) {
		l2x0_saved_regs.aux_ctrl = aux;

		data->enable(l2x0_base, data->num_lock);
	}

	outer_cache = fns;

	/*
	 * It is strange to save the register state before initialisation,
	 * but hey, this is what the DT implementations decided to do.
	 */
	if (data->save)
		data->save(l2x0_base);

	/* Re-read it in case some bits are reserved. */
	aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);

	pr_info("%s cache controller enabled, %d ways, %d kB\n",
		data->type, ways, l2x0_size >> 10);
	pr_info("%s: CACHE_ID 0x%08x, AUX_CTRL 0x%08x\n",
		data->type, cache_id, aux);

	return 0;
}

void __init l2x0_init(void __iomem *base, u32 aux_val, u32 aux_mask)
{
	const struct l2c_init_data *data;
	u32 cache_id;

	l2x0_base = base;

	cache_id = readl_relaxed(base + L2X0_CACHE_ID);

	switch (cache_id & L2X0_CACHE_ID_PART_MASK) {
	default:
	case L2X0_CACHE_ID_PART_L210:
		data = &l2c210_data;
		break;

	case L2X0_CACHE_ID_PART_L220:
		data = &l2c220_data;
		break;

	case L2X0_CACHE_ID_PART_L310:
		data = &l2c310_init_fns;
		break;
	}

	/* Read back current (default) hardware configuration */
	if (data->save)
		data->save(l2x0_base);

	__l2c_init(data, aux_val, aux_mask, cache_id);
}

#ifdef CONFIG_OF
static int l2_wt_override;

/* Aurora don't have the cache ID register available, so we have to
 * pass it though the device tree */
static u32 cache_id_part_number_from_dt;

/**
 * l2x0_cache_size_of_parse() - read cache size parameters from DT
 * @np: the device tree node for the l2 cache
 * @aux_val: pointer to machine-supplied auxilary register value, to
 * be augmented by the call (bits to be set to 1)
 * @aux_mask: pointer to machine-supplied auxilary register mask, to
 * be augmented by the call (bits to be set to 0)
 * @associativity: variable to return the calculated associativity in
 * @max_way_size: the maximum size in bytes for the cache ways
 */
static int __init l2x0_cache_size_of_parse(const struct device_node *np,
					    u32 *aux_val, u32 *aux_mask,
					    u32 *associativity,
					    u32 max_way_size)
{
	u32 mask = 0, val = 0;
	u32 cache_size = 0, sets = 0;
	u32 way_size_bits = 1;
	u32 way_size = 0;
	u32 block_size = 0;
	u32 line_size = 0;

	of_property_read_u32(np, "cache-size", &cache_size);
	of_property_read_u32(np, "cache-sets", &sets);
	of_property_read_u32(np, "cache-block-size", &block_size);
	of_property_read_u32(np, "cache-line-size", &line_size);

	if (!cache_size || !sets)
		return -ENODEV;

	/* All these l2 caches have the same line = block size actually */
	if (!line_size) {
		if (block_size) {
			/* If linesize is not given, it is equal to blocksize */
			line_size = block_size;
		} else {
			/* Fall back to known size */
			pr_warn("L2C OF: no cache block/line size given: "
				"falling back to default size %d bytes\n",
				CACHE_LINE_SIZE);
			line_size = CACHE_LINE_SIZE;
		}
	}

	if (line_size != CACHE_LINE_SIZE)
		pr_warn("L2C OF: DT supplied line size %d bytes does "
			"not match hardware line size of %d bytes\n",
			line_size,
			CACHE_LINE_SIZE);

	/*
	 * Since:
	 * set size = cache size / sets
	 * ways = cache size / (sets * line size)
	 * way size = cache size / (cache size / (sets * line size))
	 * way size = sets * line size
	 * associativity = ways = cache size / way size
	 */
	way_size = sets * line_size;
	*associativity = cache_size / way_size;

	if (way_size > max_way_size) {
		pr_err("L2C OF: set size %dKB is too large\n", way_size);
		return -EINVAL;
	}

	pr_info("L2C OF: override cache size: %d bytes (%dKB)\n",
		cache_size, cache_size >> 10);
	pr_info("L2C OF: override line size: %d bytes\n", line_size);
	pr_info("L2C OF: override way size: %d bytes (%dKB)\n",
		way_size, way_size >> 10);
	pr_info("L2C OF: override associativity: %d\n", *associativity);

	/*
	 * Calculates the bits 17:19 to set for way size:
	 * 512KB -> 6, 256KB -> 5, ... 16KB -> 1
	 */
	way_size_bits = ilog2(way_size >> 10) - 3;
	if (way_size_bits < 1 || way_size_bits > 6) {
		pr_err("L2C OF: cache way size illegal: %dKB is not mapped\n",
		       way_size);
		return -EINVAL;
	}

	mask |= L2C_AUX_CTRL_WAY_SIZE_MASK;
	val |= (way_size_bits << L2C_AUX_CTRL_WAY_SIZE_SHIFT);

	*aux_val &= ~mask;
	*aux_val |= val;
	*aux_mask &= ~mask;

	return 0;
}

static void __init l2x0_of_parse(const struct device_node *np,
				 u32 *aux_val, u32 *aux_mask)
{
	u32 data[2] = { 0, 0 };
	u32 tag = 0;
	u32 dirty = 0;
	u32 val = 0, mask = 0;
	u32 assoc;
	int ret;

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

	ret = l2x0_cache_size_of_parse(np, aux_val, aux_mask, &assoc, SZ_256K);
	if (ret)
		return;

	if (assoc > 8) {
		pr_err("l2x0 of: cache setting yield too high associativity\n");
		pr_err("l2x0 of: %d calculated, max 8\n", assoc);
	} else {
		mask |= L2X0_AUX_CTRL_ASSOC_MASK;
		val |= (assoc << L2X0_AUX_CTRL_ASSOC_SHIFT);
	}

	*aux_val &= ~mask;
	*aux_val |= val;
	*aux_mask &= ~mask;
}

static const struct l2c_init_data of_l2c210_data __initconst = {
	.type = "L2C-210",
	.way_size_0 = SZ_8K,
	.num_lock = 1,
	.of_parse = l2x0_of_parse,
	.enable = l2c_enable,
	.save = l2c_save,
	.configure = l2c_configure,
	.unlock = l2c_unlock,
	.outer_cache = {
		.inv_range   = l2c210_inv_range,
		.clean_range = l2c210_clean_range,
		.flush_range = l2c210_flush_range,
		.flush_all   = l2c210_flush_all,
		.disable     = l2c_disable,
		.sync        = l2c210_sync,
		.resume      = l2c_resume,
	},
};

static const struct l2c_init_data of_l2c220_data __initconst = {
	.type = "L2C-220",
	.way_size_0 = SZ_8K,
	.num_lock = 1,
	.of_parse = l2x0_of_parse,
	.enable = l2c220_enable,
	.save = l2c_save,
	.configure = l2c_configure,
	.unlock = l2c220_unlock,
	.outer_cache = {
		.inv_range   = l2c220_inv_range,
		.clean_range = l2c220_clean_range,
		.flush_range = l2c220_flush_range,
		.flush_all   = l2c220_flush_all,
		.disable     = l2c_disable,
		.sync        = l2c220_sync,
		.resume      = l2c_resume,
	},
};

static void __init l2c310_of_parse(const struct device_node *np,
	u32 *aux_val, u32 *aux_mask)
{
	u32 data[3] = { 0, 0, 0 };
	u32 tag[3] = { 0, 0, 0 };
	u32 filter[2] = { 0, 0 };
	u32 assoc;
	u32 prefetch;
	u32 val;
	int ret;

	of_property_read_u32_array(np, "arm,tag-latency", tag, ARRAY_SIZE(tag));
	if (tag[0] && tag[1] && tag[2])
		l2x0_saved_regs.tag_latency =
			L310_LATENCY_CTRL_RD(tag[0] - 1) |
			L310_LATENCY_CTRL_WR(tag[1] - 1) |
			L310_LATENCY_CTRL_SETUP(tag[2] - 1);

	of_property_read_u32_array(np, "arm,data-latency",
				   data, ARRAY_SIZE(data));
	if (data[0] && data[1] && data[2])
		l2x0_saved_regs.data_latency =
			L310_LATENCY_CTRL_RD(data[0] - 1) |
			L310_LATENCY_CTRL_WR(data[1] - 1) |
			L310_LATENCY_CTRL_SETUP(data[2] - 1);

	of_property_read_u32_array(np, "arm,filter-ranges",
				   filter, ARRAY_SIZE(filter));
	if (filter[1]) {
		l2x0_saved_regs.filter_end =
					ALIGN(filter[0] + filter[1], SZ_1M);
		l2x0_saved_regs.filter_start = (filter[0] & ~(SZ_1M - 1))
					| L310_ADDR_FILTER_EN;
	}

	ret = l2x0_cache_size_of_parse(np, aux_val, aux_mask, &assoc, SZ_512K);
	if (!ret) {
		switch (assoc) {
		case 16:
			*aux_val &= ~L2X0_AUX_CTRL_ASSOC_MASK;
			*aux_val |= L310_AUX_CTRL_ASSOCIATIVITY_16;
			*aux_mask &= ~L2X0_AUX_CTRL_ASSOC_MASK;
			break;
		case 8:
			*aux_val &= ~L2X0_AUX_CTRL_ASSOC_MASK;
			*aux_mask &= ~L2X0_AUX_CTRL_ASSOC_MASK;
			break;
		default:
			pr_err("L2C-310 OF cache associativity %d invalid, only 8 or 16 permitted\n",
			       assoc);
			break;
		}
	}

	if (of_property_read_bool(np, "arm,shared-override")) {
		*aux_val |= L2C_AUX_CTRL_SHARED_OVERRIDE;
		*aux_mask &= ~L2C_AUX_CTRL_SHARED_OVERRIDE;
	}

	prefetch = l2x0_saved_regs.prefetch_ctrl;

	ret = of_property_read_u32(np, "arm,double-linefill", &val);
	if (ret == 0) {
		if (val)
			prefetch |= L310_PREFETCH_CTRL_DBL_LINEFILL;
		else
			prefetch &= ~L310_PREFETCH_CTRL_DBL_LINEFILL;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF arm,double-linefill property value is missing\n");
	}

	ret = of_property_read_u32(np, "arm,double-linefill-incr", &val);
	if (ret == 0) {
		if (val)
			prefetch |= L310_PREFETCH_CTRL_DBL_LINEFILL_INCR;
		else
			prefetch &= ~L310_PREFETCH_CTRL_DBL_LINEFILL_INCR;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF arm,double-linefill-incr property value is missing\n");
	}

	ret = of_property_read_u32(np, "arm,double-linefill-wrap", &val);
	if (ret == 0) {
		if (!val)
			prefetch |= L310_PREFETCH_CTRL_DBL_LINEFILL_WRAP;
		else
			prefetch &= ~L310_PREFETCH_CTRL_DBL_LINEFILL_WRAP;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF arm,double-linefill-wrap property value is missing\n");
	}

	ret = of_property_read_u32(np, "arm,prefetch-drop", &val);
	if (ret == 0) {
		if (val)
			prefetch |= L310_PREFETCH_CTRL_PREFETCH_DROP;
		else
			prefetch &= ~L310_PREFETCH_CTRL_PREFETCH_DROP;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF arm,prefetch-drop property value is missing\n");
	}

	ret = of_property_read_u32(np, "arm,prefetch-offset", &val);
	if (ret == 0) {
		prefetch &= ~L310_PREFETCH_CTRL_OFFSET_MASK;
		prefetch |= val & L310_PREFETCH_CTRL_OFFSET_MASK;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF arm,prefetch-offset property value is missing\n");
	}

	ret = of_property_read_u32(np, "prefetch-data", &val);
	if (ret == 0) {
		if (val)
			prefetch |= L310_PREFETCH_CTRL_DATA_PREFETCH;
		else
			prefetch &= ~L310_PREFETCH_CTRL_DATA_PREFETCH;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF prefetch-data property value is missing\n");
	}

	ret = of_property_read_u32(np, "prefetch-instr", &val);
	if (ret == 0) {
		if (val)
			prefetch |= L310_PREFETCH_CTRL_INSTR_PREFETCH;
		else
			prefetch &= ~L310_PREFETCH_CTRL_INSTR_PREFETCH;
	} else if (ret != -EINVAL) {
		pr_err("L2C-310 OF prefetch-instr property value is missing\n");
	}

	l2x0_saved_regs.prefetch_ctrl = prefetch;
}

static const struct l2c_init_data of_l2c310_data __initconst = {
	.type = "L2C-310",
	.way_size_0 = SZ_8K,
	.num_lock = 8,
	.of_parse = l2c310_of_parse,
	.enable = l2c310_enable,
	.fixup = l2c310_fixup,
	.save  = l2c310_save,
	.configure = l2c310_configure,
	.unlock = l2c310_unlock,
	.outer_cache = {
		.inv_range   = l2c210_inv_range,
		.clean_range = l2c210_clean_range,
		.flush_range = l2c210_flush_range,
		.flush_all   = l2c210_flush_all,
		.disable     = l2c310_disable,
		.sync        = l2c210_sync,
		.resume      = l2c310_resume,
	},
};

/*
 * This is a variant of the of_l2c310_data with .sync set to
 * NULL. Outer sync operations are not needed when the system is I/O
 * coherent, and potentially harmful in certain situations (PCIe/PL310
 * deadlock on Armada 375/38x due to hardware I/O coherency). The
 * other operations are kept because they are infrequent (therefore do
 * not cause the deadlock in practice) and needed for secondary CPU
 * boot and other power management activities.
 */
static const struct l2c_init_data of_l2c310_coherent_data __initconst = {
	.type = "L2C-310 Coherent",
	.way_size_0 = SZ_8K,
	.num_lock = 8,
	.of_parse = l2c310_of_parse,
	.enable = l2c310_enable,
	.fixup = l2c310_fixup,
	.save  = l2c310_save,
	.configure = l2c310_configure,
	.unlock = l2c310_unlock,
	.outer_cache = {
		.inv_range   = l2c210_inv_range,
		.clean_range = l2c210_clean_range,
		.flush_range = l2c210_flush_range,
		.flush_all   = l2c210_flush_all,
		.disable     = l2c310_disable,
		.resume      = l2c310_resume,
	},
};

/*
 * Note that the end addresses passed to Linux primitives are
 * noninclusive, while the hardware cache range operations use
 * inclusive start and end addresses.
 */
static unsigned long aurora_range_end(unsigned long start, unsigned long end)
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

static void aurora_pa_range(unsigned long start, unsigned long end,
			    unsigned long offset)
{
	void __iomem *base = l2x0_base;
	unsigned long range_end;
	unsigned long flags;

	/*
	 * round start and end adresses up to cache line size
	 */
	start &= ~(CACHE_LINE_SIZE - 1);
	end = ALIGN(end, CACHE_LINE_SIZE);

	/*
	 * perform operation on all full cache lines between 'start' and 'end'
	 */
	while (start < end) {
		range_end = aurora_range_end(start, end);

		raw_spin_lock_irqsave(&l2x0_lock, flags);
		writel_relaxed(start, base + AURORA_RANGE_BASE_ADDR_REG);
		writel_relaxed(range_end - CACHE_LINE_SIZE, base + offset);
		raw_spin_unlock_irqrestore(&l2x0_lock, flags);

		writel_relaxed(0, base + AURORA_SYNC_REG);
		start = range_end;
	}
}
static void aurora_inv_range(unsigned long start, unsigned long end)
{
	aurora_pa_range(start, end, AURORA_INVAL_RANGE_REG);
}

static void aurora_clean_range(unsigned long start, unsigned long end)
{
	/*
	 * If L2 is forced to WT, the L2 will always be clean and we
	 * don't need to do anything here.
	 */
	if (!l2_wt_override)
		aurora_pa_range(start, end, AURORA_CLEAN_RANGE_REG);
}

static void aurora_flush_range(unsigned long start, unsigned long end)
{
	if (l2_wt_override)
		aurora_pa_range(start, end, AURORA_INVAL_RANGE_REG);
	else
		aurora_pa_range(start, end, AURORA_FLUSH_RANGE_REG);
}

static void aurora_flush_all(void)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	/* clean all ways */
	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2c_op_way(base + L2X0_CLEAN_INV_WAY);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);

	writel_relaxed(0, base + AURORA_SYNC_REG);
}

static void aurora_cache_sync(void)
{
	writel_relaxed(0, l2x0_base + AURORA_SYNC_REG);
}

static void aurora_disable(void)
{
	void __iomem *base = l2x0_base;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2x0_lock, flags);
	__l2c_op_way(base + L2X0_CLEAN_INV_WAY);
	writel_relaxed(0, base + AURORA_SYNC_REG);
	l2c_write_sec(0, base, L2X0_CTRL);
	dsb(st);
	raw_spin_unlock_irqrestore(&l2x0_lock, flags);
}

static void aurora_save(void __iomem *base)
{
	l2x0_saved_regs.ctrl = readl_relaxed(base + L2X0_CTRL);
	l2x0_saved_regs.aux_ctrl = readl_relaxed(base + L2X0_AUX_CTRL);
}

/*
 * For Aurora cache in no outer mode, enable via the CP15 coprocessor
 * broadcasting of cache commands to L2.
 */
static void __init aurora_enable_no_outer(void __iomem *base,
	unsigned num_lock)
{
	u32 u;

	asm volatile("mrc p15, 1, %0, c15, c2, 0" : "=r" (u));
	u |= AURORA_CTRL_FW;		/* Set the FW bit */
	asm volatile("mcr p15, 1, %0, c15, c2, 0" : : "r" (u));

	isb();

	l2c_enable(base, num_lock);
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
	.type = "Aurora",
	.way_size_0 = SZ_4K,
	.num_lock = 4,
	.of_parse = aurora_of_parse,
	.enable = l2c_enable,
	.fixup = aurora_fixup,
	.save  = aurora_save,
	.configure = l2c_configure,
	.unlock = l2c_unlock,
	.outer_cache = {
		.inv_range   = aurora_inv_range,
		.clean_range = aurora_clean_range,
		.flush_range = aurora_flush_range,
		.flush_all   = aurora_flush_all,
		.disable     = aurora_disable,
		.sync	     = aurora_cache_sync,
		.resume      = l2c_resume,
	},
};

static const struct l2c_init_data of_aurora_no_outer_data __initconst = {
	.type = "Aurora",
	.way_size_0 = SZ_4K,
	.num_lock = 4,
	.of_parse = aurora_of_parse,
	.enable = aurora_enable_no_outer,
	.fixup = aurora_fixup,
	.save  = aurora_save,
	.configure = l2c_configure,
	.unlock = l2c_unlock,
	.outer_cache = {
		.resume      = l2c_resume,
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
		l2c210_inv_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2c210_inv_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2c210_inv_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

static void bcm_clean_range(unsigned long start, unsigned long end)
{
	unsigned long new_start, new_end;

	BUG_ON(start < BCM_SYS_EMI_START_ADDR);

	if (unlikely(end <= start))
		return;

	new_start = bcm_l2_phys_addr(start);
	new_end = bcm_l2_phys_addr(end);

	/* normal case, no cross section between start and end */
	if (likely(bcm_addr_is_sys_emi(end) || !bcm_addr_is_sys_emi(start))) {
		l2c210_clean_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2c210_clean_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2c210_clean_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

static void bcm_flush_range(unsigned long start, unsigned long end)
{
	unsigned long new_start, new_end;

	BUG_ON(start < BCM_SYS_EMI_START_ADDR);

	if (unlikely(end <= start))
		return;

	if ((end - start) >= l2x0_size) {
		outer_cache.flush_all();
		return;
	}

	new_start = bcm_l2_phys_addr(start);
	new_end = bcm_l2_phys_addr(end);

	/* normal case, no cross section between start and end */
	if (likely(bcm_addr_is_sys_emi(end) || !bcm_addr_is_sys_emi(start))) {
		l2c210_flush_range(new_start, new_end);
		return;
	}

	/* They cross sections, so it can only be a cross from section
	 * 2 to section 3
	 */
	l2c210_flush_range(new_start,
		bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR-1));
	l2c210_flush_range(bcm_l2_phys_addr(BCM_VC_EMI_SEC3_START_ADDR),
		new_end);
}

/* Broadcom L2C-310 start from ARMs R3P2 or later, and require no fixups */
static const struct l2c_init_data of_bcm_l2x0_data __initconst = {
	.type = "BCM-L2C-310",
	.way_size_0 = SZ_8K,
	.num_lock = 8,
	.of_parse = l2c310_of_parse,
	.enable = l2c310_enable,
	.save  = l2c310_save,
	.configure = l2c310_configure,
	.unlock = l2c310_unlock,
	.outer_cache = {
		.inv_range   = bcm_inv_range,
		.clean_range = bcm_clean_range,
		.flush_range = bcm_flush_range,
		.flush_all   = l2c210_flush_all,
		.disable     = l2c310_disable,
		.sync        = l2c210_sync,
		.resume      = l2c310_resume,
	},
};

static void __init tauros3_save(void __iomem *base)
{
	l2c_save(base);

	l2x0_saved_regs.aux2_ctrl =
		readl_relaxed(base + TAUROS3_AUX2_CTRL);
	l2x0_saved_regs.prefetch_ctrl =
		readl_relaxed(base + L310_PREFETCH_CTRL);
}

static void tauros3_configure(void __iomem *base)
{
	l2c_configure(base);
	writel_relaxed(l2x0_saved_regs.aux2_ctrl,
		       base + TAUROS3_AUX2_CTRL);
	writel_relaxed(l2x0_saved_regs.prefetch_ctrl,
		       base + L310_PREFETCH_CTRL);
}

static const struct l2c_init_data of_tauros3_data __initconst = {
	.type = "Tauros3",
	.way_size_0 = SZ_8K,
	.num_lock = 8,
	.enable = l2c_enable,
	.save  = tauros3_save,
	.configure = tauros3_configure,
	.unlock = l2c_unlock,
	/* Tauros3 broadcasts L1 cache operations to L2 */
	.outer_cache = {
		.resume      = l2c_resume,
	},
};

#define L2C_ID(name, fns) { .compatible = name, .data = (void *)&fns }
static const struct of_device_id l2x0_ids[] __initconst = {
	L2C_ID("arm,l210-cache", of_l2c210_data),
	L2C_ID("arm,l220-cache", of_l2c220_data),
	L2C_ID("arm,pl310-cache", of_l2c310_data),
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
	u32 cache_id, old_aux;
	u32 cache_level = 2;

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

	if (of_device_is_compatible(np, "arm,pl310-cache") &&
	    of_property_read_bool(np, "arm,io-coherent"))
		data = &of_l2c310_coherent_data;

	old_aux = readl_relaxed(l2x0_base + L2X0_AUX_CTRL);
	if (old_aux != ((old_aux & aux_mask) | aux_val)) {
		pr_warn("L2C: platform modifies aux control register: 0x%08x -> 0x%08x\n",
		        old_aux, (old_aux & aux_mask) | aux_val);
	} else if (aux_mask != ~0U && aux_val != 0) {
		pr_alert("L2C: platform provided aux values match the hardware, so have no effect.  Please remove them.\n");
	}

	/* All L2 caches are unified, so this property should be specified */
	if (!of_property_read_bool(np, "cache-unified"))
		pr_err("L2C: device tree omits to specify unified cache\n");

	if (of_property_read_u32(np, "cache-level", &cache_level))
		pr_err("L2C: device tree omits to specify cache-level\n");

	if (cache_level != 2)
		pr_err("L2C: device tree specifies invalid cache level\n");

	/* Read back current (default) hardware configuration */
	if (data->save)
		data->save(l2x0_base);

	/* L2 configuration can only be changed if the cache is disabled */
	if (!(readl_relaxed(l2x0_base + L2X0_CTRL) & L2X0_CTRL_EN))
		if (data->of_parse)
			data->of_parse(np, &aux_val, &aux_mask);

	if (cache_id_part_number_from_dt)
		cache_id = cache_id_part_number_from_dt;
	else
		cache_id = readl_relaxed(l2x0_base + L2X0_CACHE_ID);

	return __l2c_init(data, aux_val, aux_mask, cache_id);
}
#endif
