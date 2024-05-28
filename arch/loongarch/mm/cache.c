// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1994 - 2003, 06, 07 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2007 MIPS Technologies, Inc.
 */
#include <linux/cacheinfo.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/syscalls.h>

#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/loongarch.h>
#include <asm/numa.h>
#include <asm/processor.h>
#include <asm/setup.h>

void cache_error_setup(void)
{
	extern char __weak except_vec_cex;
	set_merr_handler(0x0, &except_vec_cex, 0x80);
}

/*
 * LoongArch maintains ICache/DCache coherency by hardware,
 * we just need "ibar" to avoid instruction hazard here.
 */
void local_flush_icache_range(unsigned long start, unsigned long end)
{
	asm volatile ("\tibar 0\n"::);
}
EXPORT_SYMBOL(local_flush_icache_range);

static void flush_cache_leaf(unsigned int leaf)
{
	int i, j, nr_nodes;
	uint64_t addr = CSR_DMW0_BASE;
	struct cache_desc *cdesc = current_cpu_data.cache_leaves + leaf;

	nr_nodes = cache_private(cdesc) ? 1 : loongson_sysconf.nr_nodes;

	do {
		for (i = 0; i < cdesc->sets; i++) {
			for (j = 0; j < cdesc->ways; j++) {
				flush_cache_line(leaf, addr);
				addr++;
			}

			addr -= cdesc->ways;
			addr += cdesc->linesz;
		}
		addr += (1ULL << NODE_ADDRSPACE_SHIFT);
	} while (--nr_nodes > 0);
}

asmlinkage __visible void __flush_cache_all(void)
{
	int leaf;
	struct cache_desc *cdesc = current_cpu_data.cache_leaves;
	unsigned int cache_present = current_cpu_data.cache_leaves_present;

	leaf = cache_present - 1;
	if (cache_inclusive(cdesc + leaf)) {
		flush_cache_leaf(leaf);
		return;
	}

	for (leaf = 0; leaf < cache_present; leaf++)
		flush_cache_leaf(leaf);
}

#define L1IUPRE		(1 << 0)
#define L1IUUNIFY	(1 << 1)
#define L1DPRE		(1 << 2)

#define LXIUPRE		(1 << 0)
#define LXIUUNIFY	(1 << 1)
#define LXIUPRIV	(1 << 2)
#define LXIUINCL	(1 << 3)
#define LXDPRE		(1 << 4)
#define LXDPRIV		(1 << 5)
#define LXDINCL		(1 << 6)

#define populate_cache_properties(cfg0, cdesc, level, leaf)				\
do {											\
	unsigned int cfg1;								\
											\
	cfg1 = read_cpucfg(LOONGARCH_CPUCFG17 + leaf);					\
	if (level == 1)	{								\
		cdesc->flags |= CACHE_PRIVATE;						\
	} else {									\
		if (cfg0 & LXIUPRIV)							\
			cdesc->flags |= CACHE_PRIVATE;					\
		if (cfg0 & LXIUINCL)							\
			cdesc->flags |= CACHE_INCLUSIVE;				\
	}										\
	cdesc->level = level;								\
	cdesc->flags |= CACHE_PRESENT;							\
	cdesc->ways = ((cfg1 & CPUCFG_CACHE_WAYS_M) >> CPUCFG_CACHE_WAYS) + 1;		\
	cdesc->sets = 1 << ((cfg1 & CPUCFG_CACHE_SETS_M) >> CPUCFG_CACHE_SETS);		\
	cdesc->linesz = 1 << ((cfg1 & CPUCFG_CACHE_LSIZE_M) >> CPUCFG_CACHE_LSIZE);	\
	cdesc++; leaf++;								\
} while (0)

void cpu_cache_init(void)
{
	unsigned int leaf = 0, level = 1;
	unsigned int config = read_cpucfg(LOONGARCH_CPUCFG16);
	struct cache_desc *cdesc = current_cpu_data.cache_leaves;

	if (config & L1IUPRE) {
		if (config & L1IUUNIFY)
			cdesc->type = CACHE_TYPE_UNIFIED;
		else
			cdesc->type = CACHE_TYPE_INST;
		populate_cache_properties(config, cdesc, level, leaf);
	}

	if (config & L1DPRE) {
		cdesc->type = CACHE_TYPE_DATA;
		populate_cache_properties(config, cdesc, level, leaf);
	}

	config = config >> 3;
	for (level = 2; level <= CACHE_LEVEL_MAX; level++) {
		if (!config)
			break;

		if (config & LXIUPRE) {
			if (config & LXIUUNIFY)
				cdesc->type = CACHE_TYPE_UNIFIED;
			else
				cdesc->type = CACHE_TYPE_INST;
			populate_cache_properties(config, cdesc, level, leaf);
		}

		if (config & LXDPRE) {
			cdesc->type = CACHE_TYPE_DATA;
			populate_cache_properties(config, cdesc, level, leaf);
		}

		config = config >> 7;
	}

	BUG_ON(leaf > CACHE_LEAVES_MAX);

	current_cpu_data.cache_leaves_present = leaf;
	current_cpu_data.options |= LOONGARCH_CPU_PREFETCH;
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= __pgprot(_CACHE_CC | _PAGE_USER |
								   _PAGE_PROTNONE | _PAGE_NO_EXEC |
								   _PAGE_NO_READ),
	[VM_READ]					= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC),
	[VM_WRITE]					= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC),
	[VM_WRITE | VM_READ]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC),
	[VM_EXEC]					= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_EXEC | VM_READ]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_EXEC | VM_WRITE]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_EXEC | VM_WRITE | VM_READ]			= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_SHARED]					= __pgprot(_CACHE_CC | _PAGE_USER |
								   _PAGE_PROTNONE | _PAGE_NO_EXEC |
								   _PAGE_NO_READ),
	[VM_SHARED | VM_READ]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC),
	[VM_SHARED | VM_WRITE]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC | _PAGE_WRITE),
	[VM_SHARED | VM_WRITE | VM_READ]		= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_NO_EXEC | _PAGE_WRITE),
	[VM_SHARED | VM_EXEC]				= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_SHARED | VM_EXEC | VM_READ]			= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT),
	[VM_SHARED | VM_EXEC | VM_WRITE]		= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_WRITE),
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= __pgprot(_CACHE_CC | _PAGE_VALID |
								   _PAGE_USER | _PAGE_PRESENT |
								   _PAGE_WRITE)
};
DECLARE_VM_GET_PAGE_PROT
