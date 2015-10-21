/*
 * Copyright (C) 2006 Chris Dearman (chris@mips.com),
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/cpu-type.h>
#include <asm/mipsregs.h>
#include <asm/bcache.h>
#include <asm/cacheops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>
#include <asm/mips-cm.h>

/*
 * MIPS32/MIPS64 L2 cache handling
 */

/*
 * Writeback and invalidate the secondary cache before DMA.
 */
static void mips_sc_wback_inv(unsigned long addr, unsigned long size)
{
	blast_scache_range(addr, addr + size);
}

/*
 * Invalidate the secondary cache before DMA.
 */
static void mips_sc_inv(unsigned long addr, unsigned long size)
{
	unsigned long lsize = cpu_scache_line_size();
	unsigned long almask = ~(lsize - 1);

	cache_op(Hit_Writeback_Inv_SD, addr & almask);
	cache_op(Hit_Writeback_Inv_SD, (addr + size - 1) & almask);
	blast_inv_scache_range(addr, addr + size);
}

static void mips_sc_enable(void)
{
	/* L2 cache is permanently enabled */
}

static void mips_sc_disable(void)
{
	/* L2 cache is permanently enabled */
}

static struct bcache_ops mips_sc_ops = {
	.bc_enable = mips_sc_enable,
	.bc_disable = mips_sc_disable,
	.bc_wback_inv = mips_sc_wback_inv,
	.bc_inv = mips_sc_inv
};

/*
 * Check if the L2 cache controller is activated on a particular platform.
 * MTI's L2 controller and the L2 cache controller of Broadcom's BMIPS
 * cores both use c0_config2's bit 12 as "L2 Bypass" bit, that is the
 * cache being disabled.  However there is no guarantee for this to be
 * true on all platforms.  In an act of stupidity the spec defined bits
 * 12..15 as implementation defined so below function will eventually have
 * to be replaced by a platform specific probe.
 */
static inline int mips_sc_is_activated(struct cpuinfo_mips *c)
{
	unsigned int config2 = read_c0_config2();
	unsigned int tmp;

	/* Check the bypass bit (L2B) */
	switch (current_cpu_type()) {
	case CPU_34K:
	case CPU_74K:
	case CPU_1004K:
	case CPU_1074K:
	case CPU_INTERAPTIV:
	case CPU_PROAPTIV:
	case CPU_P5600:
	case CPU_BMIPS5000:
	case CPU_QEMU_GENERIC:
		if (config2 & (1 << 12))
			return 0;
	}

	tmp = (config2 >> 4) & 0x0f;
	if (0 < tmp && tmp <= 7)
		c->scache.linesz = 2 << tmp;
	else
		return 0;
	return 1;
}

static int __init mips_sc_probe_cm3(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned long cfg = read_gcr_l2_config();
	unsigned long sets, line_sz, assoc;

	if (cfg & CM_GCR_L2_CONFIG_BYPASS_MSK)
		return 0;

	sets = cfg & CM_GCR_L2_CONFIG_SET_SIZE_MSK;
	sets >>= CM_GCR_L2_CONFIG_SET_SIZE_SHF;
	c->scache.sets = 64 << sets;

	line_sz = cfg & CM_GCR_L2_CONFIG_LINE_SIZE_MSK;
	line_sz >>= CM_GCR_L2_CONFIG_LINE_SIZE_SHF;
	c->scache.linesz = 2 << line_sz;

	assoc = cfg & CM_GCR_L2_CONFIG_ASSOC_MSK;
	assoc >>= CM_GCR_L2_CONFIG_ASSOC_SHF;
	c->scache.ways = assoc + 1;
	c->scache.waysize = c->scache.sets * c->scache.linesz;
	c->scache.waybit = __ffs(c->scache.waysize);

	c->scache.flags &= ~MIPS_CACHE_NOT_PRESENT;

	return 1;
}

void __weak platform_early_l2_init(void)
{
}

static inline int __init mips_sc_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config1, config2;
	unsigned int tmp;

	/* Mark as not present until probe completed */
	c->scache.flags |= MIPS_CACHE_NOT_PRESENT;

	/*
	 * Do we need some platform specific probing before
	 * we configure L2?
	 */
	platform_early_l2_init();

	if (mips_cm_revision() >= CM_REV_CM3)
		return mips_sc_probe_cm3();

	/* Ignore anything but MIPSxx processors */
	if (!(c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M32R2 |
			      MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R1 |
			      MIPS_CPU_ISA_M64R2 | MIPS_CPU_ISA_M64R6)))
		return 0;

	/* Does this MIPS32/MIPS64 CPU have a config2 register? */
	config1 = read_c0_config1();
	if (!(config1 & MIPS_CONF_M))
		return 0;

	config2 = read_c0_config2();

	if (!mips_sc_is_activated(c))
		return 0;

	tmp = (config2 >> 8) & 0x0f;
	if (0 <= tmp && tmp <= 7)
		c->scache.sets = 64 << tmp;
	else
		return 0;

	tmp = (config2 >> 0) & 0x0f;
	if (0 <= tmp && tmp <= 7)
		c->scache.ways = tmp + 1;
	else
		return 0;

	c->scache.waysize = c->scache.sets * c->scache.linesz;
	c->scache.waybit = __ffs(c->scache.waysize);

	c->scache.flags &= ~MIPS_CACHE_NOT_PRESENT;

	return 1;
}

int mips_sc_init(void)
{
	int found = mips_sc_probe();
	if (found) {
		mips_sc_enable();
		bcops = &mips_sc_ops;
	}
	return found;
}
