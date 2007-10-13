/*
 * Copyright (C) 2006 Chris Dearman (chris@mips.com),
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/mipsregs.h>
#include <asm/bcache.h>
#include <asm/cacheops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>

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

static inline int __init mips_sc_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int config1, config2;
	unsigned int tmp;

	/* Mark as not present until probe completed */
	c->scache.flags |= MIPS_CACHE_NOT_PRESENT;

	/* Ignore anything but MIPSxx processors */
	if (c->isa_level != MIPS_CPU_ISA_M32R1 &&
	    c->isa_level != MIPS_CPU_ISA_M32R2 &&
	    c->isa_level != MIPS_CPU_ISA_M64R1 &&
	    c->isa_level != MIPS_CPU_ISA_M64R2)
		return 0;

	/* Does this MIPS32/MIPS64 CPU have a config2 register? */
	config1 = read_c0_config1();
	if (!(config1 & MIPS_CONF_M))
		return 0;

	config2 = read_c0_config2();
	tmp = (config2 >> 4) & 0x0f;
	if (0 < tmp && tmp <= 7)
		c->scache.linesz = 2 << tmp;
	else
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

int __init mips_sc_init(void)
{
	int found = mips_sc_probe();
	if (found) {
		mips_sc_enable();
		bcops = &mips_sc_ops;
	}
	return found;
}

