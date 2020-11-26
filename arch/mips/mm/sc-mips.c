// SPDX-License-Identifier: GPL-2.0
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
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>
#include <asm/mips-cps.h>
#include <asm/bootinfo.h>

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

static void mips_sc_prefetch_enable(void)
{
	unsigned long pftctl;

	if (mips_cm_revision() < CM_REV_CM2_5)
		return;

	/*
	 * If there is one or more L2 prefetch unit present then enable
	 * prefetching for both code & data, for all ports.
	 */
	pftctl = read_gcr_l2_pft_control();
	if (pftctl & CM_GCR_L2_PFT_CONTROL_NPFT) {
		pftctl &= ~CM_GCR_L2_PFT_CONTROL_PAGEMASK;
		pftctl |= PAGE_MASK & CM_GCR_L2_PFT_CONTROL_PAGEMASK;
		pftctl |= CM_GCR_L2_PFT_CONTROL_PFTEN;
		write_gcr_l2_pft_control(pftctl);

		set_gcr_l2_pft_control_b(CM_GCR_L2_PFT_CONTROL_B_PORTID |
					 CM_GCR_L2_PFT_CONTROL_B_CEN);
	}
}

static void mips_sc_prefetch_disable(void)
{
	if (mips_cm_revision() < CM_REV_CM2_5)
		return;

	clear_gcr_l2_pft_control(CM_GCR_L2_PFT_CONTROL_PFTEN);
	clear_gcr_l2_pft_control_b(CM_GCR_L2_PFT_CONTROL_B_PORTID |
				   CM_GCR_L2_PFT_CONTROL_B_CEN);
}

static bool mips_sc_prefetch_is_enabled(void)
{
	unsigned long pftctl;

	if (mips_cm_revision() < CM_REV_CM2_5)
		return false;

	pftctl = read_gcr_l2_pft_control();
	if (!(pftctl & CM_GCR_L2_PFT_CONTROL_NPFT))
		return false;
	return !!(pftctl & CM_GCR_L2_PFT_CONTROL_PFTEN);
}

static struct bcache_ops mips_sc_ops = {
	.bc_enable = mips_sc_enable,
	.bc_disable = mips_sc_disable,
	.bc_wback_inv = mips_sc_wback_inv,
	.bc_inv = mips_sc_inv,
	.bc_prefetch_enable = mips_sc_prefetch_enable,
	.bc_prefetch_disable = mips_sc_prefetch_disable,
	.bc_prefetch_is_enabled = mips_sc_prefetch_is_enabled,
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
	case CPU_P6600:
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

	if (cfg & CM_GCR_L2_CONFIG_BYPASS)
		return 0;

	sets = cfg & CM_GCR_L2_CONFIG_SET_SIZE;
	sets >>= __ffs(CM_GCR_L2_CONFIG_SET_SIZE);
	if (sets)
		c->scache.sets = 64 << sets;

	line_sz = cfg & CM_GCR_L2_CONFIG_LINE_SIZE;
	line_sz >>= __ffs(CM_GCR_L2_CONFIG_LINE_SIZE);
	if (line_sz)
		c->scache.linesz = 2 << line_sz;

	assoc = cfg & CM_GCR_L2_CONFIG_ASSOC;
	assoc >>= __ffs(CM_GCR_L2_CONFIG_ASSOC);
	c->scache.ways = assoc + 1;
	c->scache.waysize = c->scache.sets * c->scache.linesz;
	c->scache.waybit = __ffs(c->scache.waysize);

	if (c->scache.linesz) {
		c->scache.flags &= ~MIPS_CACHE_NOT_PRESENT;
		c->options |= MIPS_CPU_INCLUSIVE_CACHES;
		return 1;
	}

	return 0;
}

static inline int __init mips_sc_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config1, config2;
	unsigned int tmp;

	/* Mark as not present until probe completed */
	c->scache.flags |= MIPS_CACHE_NOT_PRESENT;

	if (mips_cm_revision() >= CM_REV_CM3)
		return mips_sc_probe_cm3();

	/* Ignore anything but MIPSxx processors */
	if (!(c->isa_level & (MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1 |
			      MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2 |
			      MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5 |
			      MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6)))
		return 0;

	/* Does this MIPS32/MIPS64 CPU have a config2 register? */
	config1 = read_c0_config1();
	if (!(config1 & MIPS_CONF_M))
		return 0;

	config2 = read_c0_config2();

	if (!mips_sc_is_activated(c))
		return 0;

	tmp = (config2 >> 8) & 0x0f;
	if (tmp <= 7)
		c->scache.sets = 64 << tmp;
	else
		return 0;

	tmp = (config2 >> 0) & 0x0f;
	if (tmp <= 7)
		c->scache.ways = tmp + 1;
	else
		return 0;

	if (current_cpu_type() == CPU_XBURST) {
		switch (mips_machtype) {
		/*
		 * According to config2 it would be 5-ways, but that is
		 * contradicted by all documentation.
		 */
		case MACH_INGENIC_JZ4770:
		case MACH_INGENIC_JZ4775:
			c->scache.ways = 4;
			break;

		/*
		 * According to config2 it would be 5-ways and 512-sets,
		 * but that is contradicted by all documentation.
		 */
		case MACH_INGENIC_X1000:
		case MACH_INGENIC_X1000E:
			c->scache.sets = 256;
			c->scache.ways = 4;
			break;
		}
	}

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
		mips_sc_prefetch_enable();
		bcops = &mips_sc_ops;
	}
	return found;
}
