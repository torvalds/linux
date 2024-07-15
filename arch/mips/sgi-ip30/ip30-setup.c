// SPDX-License-Identifier: GPL-2.0
/*
 * SGI IP30 miscellaneous setup bits.
 *
 * Copyright (C) 2004-2007 Stanislaw Skowronek <skylark@unaligned.org>
 *               2007 Joshua Kinard <kumba@gentoo.org>
 *               2009 Johannes Dickgreber <tanzy@gmx.de>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/memblock.h>

#include <asm/bootinfo.h>
#include <asm/smp-ops.h>
#include <asm/sgialib.h>
#include <asm/time.h>
#include <asm/sgi/heart.h>

#include "ip30-common.h"

/* Structure of accessible HEART registers located in XKPHYS space. */
struct ip30_heart_regs __iomem *heart_regs = HEART_XKPHYS_BASE;

/*
 * ARCS will report up to the first 1GB of
 * memory if queried.  Anything beyond that
 * is marked as reserved.
 */
#define IP30_MAX_PROM_MEMORY	_AC(0x40000000, UL)

/*
 * Memory in the Octane starts at 512MB
 */
#define IP30_MEMORY_BASE	_AC(0x20000000, UL)

/*
 * If using ARCS to probe for memory, then
 * remaining memory will start at this offset.
 */
#define IP30_REAL_MEMORY_START  (IP30_MEMORY_BASE + IP30_MAX_PROM_MEMORY)

#define MEM_SHIFT(x) ((x) >> 20)

static void __init ip30_mem_init(void)
{
	unsigned long total_mem;
	phys_addr_t addr;
	phys_addr_t size;
	u32 memcfg;
	int i;

	total_mem = 0;
	for (i = 0; i < HEART_MEMORY_BANKS; i++) {
		memcfg = __raw_readl(&heart_regs->mem_cfg.l[i]);
		if (!(memcfg & HEART_MEMCFG_VALID))
			continue;

		addr = memcfg & HEART_MEMCFG_ADDR_MASK;
		addr <<= HEART_MEMCFG_UNIT_SHIFT;
		addr += IP30_MEMORY_BASE;
		size = memcfg & HEART_MEMCFG_SIZE_MASK;
		size >>= HEART_MEMCFG_SIZE_SHIFT;
		size += 1;
		size <<= HEART_MEMCFG_UNIT_SHIFT;

		total_mem += size;

		if (addr >= IP30_REAL_MEMORY_START)
			memblock_phys_free(addr, size);
		else if ((addr + size) > IP30_REAL_MEMORY_START)
			memblock_phys_free(IP30_REAL_MEMORY_START,
					   size - IP30_MAX_PROM_MEMORY);
	}
	pr_info("Detected %luMB of physical memory.\n", MEM_SHIFT(total_mem));
}

/**
 * ip30_cpu_time_init - platform time initialization.
 */
static void __init ip30_cpu_time_init(void)
{
	int cpu = smp_processor_id();
	u64 heart_compare;
	unsigned int start, end;
	int time_diff;

	heart_compare = (heart_read(&heart_regs->count) +
			 (HEART_CYCLES_PER_SEC / 10));
	start = read_c0_count();
	while ((heart_read(&heart_regs->count) - heart_compare) & 0x800000)
		cpu_relax();

	end = read_c0_count();
	time_diff = (int)end - (int)start;
	mips_hpt_frequency = time_diff * 10;
	pr_info("IP30: CPU%d: %d MHz CPU detected.\n", cpu,
		(mips_hpt_frequency * 2) / 1000000);
}

void __init ip30_per_cpu_init(void)
{
	/* Disable all interrupts. */
	clear_c0_status(ST0_IM);

	ip30_cpu_time_init();
#ifdef CONFIG_SMP
	ip30_install_ipi();
#endif

	enable_percpu_irq(IP30_HEART_L0_IRQ, IRQ_TYPE_NONE);
	enable_percpu_irq(IP30_HEART_L1_IRQ, IRQ_TYPE_NONE);
	enable_percpu_irq(IP30_HEART_L2_IRQ, IRQ_TYPE_NONE);
	enable_percpu_irq(IP30_HEART_ERR_IRQ, IRQ_TYPE_NONE);
}

/**
 * plat_mem_setup - despite the name, misc setup happens here.
 */
void __init plat_mem_setup(void)
{
	ip30_mem_init();

	/* XXX: Hard lock on /sbin/init if this flag isn't specified. */
	prom_flags |= PROM_FLAG_DONT_FREE_TEMP;

#ifdef CONFIG_SMP
	register_smp_ops(&ip30_smp_ops);
#else
	ip30_per_cpu_init();
#endif

	ioport_resource.start = 0;
	ioport_resource.end = ~0UL;
	set_io_port_base(IO_BASE);
}
