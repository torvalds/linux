/*
 * Copyright (C) 2008-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * TCM memory handling for ARM systems
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/genalloc.h>
#include <linux/string.h> /* memcpy */
#include <asm/page.h> /* PAGE_SHIFT */
#include <asm/cputype.h>
#include <asm/mach/map.h>
#include <mach/memory.h>
#include "tcm.h"

/* Scream and warn about misuse */
#if !defined(ITCM_OFFSET) || !defined(ITCM_END) || \
    !defined(DTCM_OFFSET) || !defined(DTCM_END)
#error "TCM support selected but offsets not defined!"
#endif

static struct gen_pool *tcm_pool;

/* TCM section definitions from the linker */
extern char __itcm_start, __sitcm_text, __eitcm_text;
extern char __dtcm_start, __sdtcm_data, __edtcm_data;

/*
 * TCM memory resources
 */
static struct resource dtcm_res = {
	.name = "DTCM RAM",
	.start = DTCM_OFFSET,
	.end = DTCM_END,
	.flags = IORESOURCE_MEM
};

static struct resource itcm_res = {
	.name = "ITCM RAM",
	.start = ITCM_OFFSET,
	.end = ITCM_END,
	.flags = IORESOURCE_MEM
};

static struct map_desc dtcm_iomap[] __initdata = {
	{
		.virtual	= DTCM_OFFSET,
		.pfn		= __phys_to_pfn(DTCM_OFFSET),
		.length		= (DTCM_END - DTCM_OFFSET + 1),
		.type		= MT_UNCACHED
	}
};

static struct map_desc itcm_iomap[] __initdata = {
	{
		.virtual	= ITCM_OFFSET,
		.pfn		= __phys_to_pfn(ITCM_OFFSET),
		.length		= (ITCM_END - ITCM_OFFSET + 1),
		.type		= MT_UNCACHED
	}
};

/*
 * Allocate a chunk of TCM memory
 */
void *tcm_alloc(size_t len)
{
	unsigned long vaddr;

	if (!tcm_pool)
		return NULL;

	vaddr = gen_pool_alloc(tcm_pool, len);
	if (!vaddr)
		return NULL;

	return (void *) vaddr;
}
EXPORT_SYMBOL(tcm_alloc);

/*
 * Free a chunk of TCM memory
 */
void tcm_free(void *addr, size_t len)
{
	gen_pool_free(tcm_pool, (unsigned long) addr, len);
}
EXPORT_SYMBOL(tcm_free);


static void __init setup_tcm_bank(u8 type, u32 offset, u32 expected_size)
{
	const int tcm_sizes[16] = { 0, -1, -1, 4, 8, 16, 32, 64, 128,
				    256, 512, 1024, -1, -1, -1, -1 };
	u32 tcm_region;
	int tcm_size;

	/* Read the special TCM region register c9, 0 */
	if (!type)
		asm("mrc	p15, 0, %0, c9, c1, 0"
		    : "=r" (tcm_region));
	else
		asm("mrc	p15, 0, %0, c9, c1, 1"
		    : "=r" (tcm_region));

	tcm_size = tcm_sizes[(tcm_region >> 2) & 0x0f];
	if (tcm_size < 0) {
		pr_err("CPU: %sTCM of unknown size!\n",
			type ? "I" : "D");
	} else {
		pr_info("CPU: found %sTCM %dk @ %08x, %senabled\n",
			type ? "I" : "D",
			tcm_size,
			(tcm_region & 0xfffff000U),
			(tcm_region & 1) ? "" : "not ");
	}

	if (tcm_size != expected_size) {
		pr_crit("CPU: %sTCM was detected %dk but expected %dk!\n",
		       type ? "I" : "D",
		       tcm_size,
		       expected_size);
		/* Adjust to the expected size? what can we do... */
	}

	/* Force move the TCM bank to where we want it, enable */
	tcm_region = offset | (tcm_region & 0x00000ffeU) | 1;

	if (!type)
		asm("mcr	p15, 0, %0, c9, c1, 0"
		    : /* No output operands */
		    : "r" (tcm_region));
	else
		asm("mcr	p15, 0, %0, c9, c1, 1"
		    : /* No output operands */
		    : "r" (tcm_region));

	pr_debug("CPU: moved %sTCM %dk to %08x, enabled\n",
		 type ? "I" : "D",
		 tcm_size,
		 (tcm_region & 0xfffff000U));
}

/*
 * This initializes the TCM memory
 */
void __init tcm_init(void)
{
	u32 tcm_status = read_cpuid_tcmstatus();
	char *start;
	char *end;
	char *ram;

	/* Setup DTCM if present */
	if (tcm_status & (1 << 16)) {
		setup_tcm_bank(0, DTCM_OFFSET,
			       (DTCM_END - DTCM_OFFSET + 1) >> 10);
		request_resource(&iomem_resource, &dtcm_res);
		iotable_init(dtcm_iomap, 1);
		/* Copy data from RAM to DTCM */
		start = &__sdtcm_data;
		end   = &__edtcm_data;
		ram   = &__dtcm_start;
		memcpy(start, ram, (end-start));
		pr_debug("CPU DTCM: copied data from %p - %p\n", start, end);
	}

	/* Setup ITCM if present */
	if (tcm_status & 1) {
		setup_tcm_bank(1, ITCM_OFFSET,
			       (ITCM_END - ITCM_OFFSET + 1) >> 10);
		request_resource(&iomem_resource, &itcm_res);
		iotable_init(itcm_iomap, 1);
		/* Copy code from RAM to ITCM */
		start = &__sitcm_text;
		end   = &__eitcm_text;
		ram   = &__itcm_start;
		memcpy(start, ram, (end-start));
		pr_debug("CPU ITCM: copied code from %p - %p\n", start, end);
	}
}

/*
 * This creates the TCM memory pool and has to be done later,
 * during the core_initicalls, since the allocator is not yet
 * up and running when the first initialization runs.
 */
static int __init setup_tcm_pool(void)
{
	u32 tcm_status = read_cpuid_tcmstatus();
	u32 dtcm_pool_start = (u32) &__edtcm_data;
	u32 itcm_pool_start = (u32) &__eitcm_text;
	int ret;

	/*
	 * Set up malloc pool, 2^2 = 4 bytes granularity since
	 * the TCM is sometimes just 4 KiB. NB: pages and cache
	 * line alignments does not matter in TCM!
	 */
	tcm_pool = gen_pool_create(2, -1);

	pr_debug("Setting up TCM memory pool\n");

	/* Add the rest of DTCM to the TCM pool */
	if (tcm_status & (1 << 16)) {
		if (dtcm_pool_start < DTCM_END) {
			ret = gen_pool_add(tcm_pool, dtcm_pool_start,
					   DTCM_END - dtcm_pool_start + 1, -1);
			if (ret) {
				pr_err("CPU DTCM: could not add DTCM " \
				       "remainder to pool!\n");
				return ret;
			}
			pr_debug("CPU DTCM: Added %08x bytes @ %08x to " \
				 "the TCM memory pool\n",
				 DTCM_END - dtcm_pool_start + 1,
				 dtcm_pool_start);
		}
	}

	/* Add the rest of ITCM to the TCM pool */
	if (tcm_status & 1) {
		if (itcm_pool_start < ITCM_END) {
			ret = gen_pool_add(tcm_pool, itcm_pool_start,
					   ITCM_END - itcm_pool_start + 1, -1);
			if (ret) {
				pr_err("CPU ITCM: could not add ITCM " \
				       "remainder to pool!\n");
				return ret;
			}
			pr_debug("CPU ITCM: Added %08x bytes @ %08x to " \
				 "the TCM memory pool\n",
				 ITCM_END - itcm_pool_start + 1,
				 itcm_pool_start);
		}
	}
	return 0;
}

core_initcall(setup_tcm_pool);
