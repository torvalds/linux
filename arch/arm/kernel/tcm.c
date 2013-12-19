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
#include <asm/cputype.h>
#include <asm/mach/map.h>
#include <asm/memory.h>
#include <asm/system_info.h>

static struct gen_pool *tcm_pool;
static bool dtcm_present;
static bool itcm_present;

/* TCM section definitions from the linker */
extern char __itcm_start, __sitcm_text, __eitcm_text;
extern char __dtcm_start, __sdtcm_data, __edtcm_data;

/* These will be increased as we run */
u32 dtcm_end = DTCM_OFFSET;
u32 itcm_end = ITCM_OFFSET;

/*
 * TCM memory resources
 */
static struct resource dtcm_res = {
	.name = "DTCM RAM",
	.start = DTCM_OFFSET,
	.end = DTCM_OFFSET,
	.flags = IORESOURCE_MEM
};

static struct resource itcm_res = {
	.name = "ITCM RAM",
	.start = ITCM_OFFSET,
	.end = ITCM_OFFSET,
	.flags = IORESOURCE_MEM
};

static struct map_desc dtcm_iomap[] __initdata = {
	{
		.virtual	= DTCM_OFFSET,
		.pfn		= __phys_to_pfn(DTCM_OFFSET),
		.length		= 0,
		.type		= MT_MEMORY_DTCM
	}
};

static struct map_desc itcm_iomap[] __initdata = {
	{
		.virtual	= ITCM_OFFSET,
		.pfn		= __phys_to_pfn(ITCM_OFFSET),
		.length		= 0,
		.type		= MT_MEMORY_ITCM
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

bool tcm_dtcm_present(void)
{
	return dtcm_present;
}
EXPORT_SYMBOL(tcm_dtcm_present);

bool tcm_itcm_present(void)
{
	return itcm_present;
}
EXPORT_SYMBOL(tcm_itcm_present);

static int __init setup_tcm_bank(u8 type, u8 bank, u8 banks,
				  u32 *offset)
{
	const int tcm_sizes[16] = { 0, -1, -1, 4, 8, 16, 32, 64, 128,
				    256, 512, 1024, -1, -1, -1, -1 };
	u32 tcm_region;
	int tcm_size;

	/*
	 * If there are more than one TCM bank of this type,
	 * select the TCM bank to operate on in the TCM selection
	 * register.
	 */
	if (banks > 1)
		asm("mcr	p15, 0, %0, c9, c2, 0"
		    : /* No output operands */
		    : "r" (bank));

	/* Read the special TCM region register c9, 0 */
	if (!type)
		asm("mrc	p15, 0, %0, c9, c1, 0"
		    : "=r" (tcm_region));
	else
		asm("mrc	p15, 0, %0, c9, c1, 1"
		    : "=r" (tcm_region));

	tcm_size = tcm_sizes[(tcm_region >> 2) & 0x0f];
	if (tcm_size < 0) {
		pr_err("CPU: %sTCM%d of unknown size\n",
		       type ? "I" : "D", bank);
		return -EINVAL;
	} else if (tcm_size > 32) {
		pr_err("CPU: %sTCM%d larger than 32k found\n",
		       type ? "I" : "D", bank);
		return -EINVAL;
	} else {
		pr_info("CPU: found %sTCM%d %dk @ %08x, %senabled\n",
			type ? "I" : "D",
			bank,
			tcm_size,
			(tcm_region & 0xfffff000U),
			(tcm_region & 1) ? "" : "not ");
	}

	/* Not much fun you can do with a size 0 bank */
	if (tcm_size == 0)
		return 0;

	/* Force move the TCM bank to where we want it, enable */
	tcm_region = *offset | (tcm_region & 0x00000ffeU) | 1;

	if (!type)
		asm("mcr	p15, 0, %0, c9, c1, 0"
		    : /* No output operands */
		    : "r" (tcm_region));
	else
		asm("mcr	p15, 0, %0, c9, c1, 1"
		    : /* No output operands */
		    : "r" (tcm_region));

	/* Increase offset */
	*offset += (tcm_size << 10);

	pr_info("CPU: moved %sTCM%d %dk to %08x, enabled\n",
		type ? "I" : "D",
		bank,
		tcm_size,
		(tcm_region & 0xfffff000U));
	return 0;
}

/*
 * This initializes the TCM memory
 */
void __init tcm_init(void)
{
	u32 tcm_status;
	u8 dtcm_banks;
	u8 itcm_banks;
	size_t dtcm_code_sz = &__edtcm_data - &__sdtcm_data;
	size_t itcm_code_sz = &__eitcm_text - &__sitcm_text;
	char *start;
	char *end;
	char *ram;
	int ret;
	int i;

	/*
	 * Prior to ARMv5 there is no TCM, and trying to read the status
	 * register will hang the processor.
	 */
	if (cpu_architecture() < CPU_ARCH_ARMv5) {
		if (dtcm_code_sz || itcm_code_sz)
			pr_info("CPU TCM: %u bytes of DTCM and %u bytes of "
				"ITCM code compiled in, but no TCM present "
				"in pre-v5 CPU\n", dtcm_code_sz, itcm_code_sz);
		return;
	}

	tcm_status = read_cpuid_tcmstatus();
	dtcm_banks = (tcm_status >> 16) & 0x03;
	itcm_banks = (tcm_status & 0x03);

	/* Values greater than 2 for D/ITCM banks are "reserved" */
	if (dtcm_banks > 2)
		dtcm_banks = 0;
	if (itcm_banks > 2)
		itcm_banks = 0;

	/* Setup DTCM if present */
	if (dtcm_banks > 0) {
		for (i = 0; i < dtcm_banks; i++) {
			ret = setup_tcm_bank(0, i, dtcm_banks, &dtcm_end);
			if (ret)
				return;
		}
		/* This means you compiled more code than fits into DTCM */
		if (dtcm_code_sz > (dtcm_end - DTCM_OFFSET)) {
			pr_info("CPU DTCM: %u bytes of code compiled to "
				"DTCM but only %lu bytes of DTCM present\n",
				dtcm_code_sz, (dtcm_end - DTCM_OFFSET));
			goto no_dtcm;
		}
		dtcm_res.end = dtcm_end - 1;
		request_resource(&iomem_resource, &dtcm_res);
		dtcm_iomap[0].length = dtcm_end - DTCM_OFFSET;
		iotable_init(dtcm_iomap, 1);
		/* Copy data from RAM to DTCM */
		start = &__sdtcm_data;
		end   = &__edtcm_data;
		ram   = &__dtcm_start;
		memcpy(start, ram, dtcm_code_sz);
		pr_debug("CPU DTCM: copied data from %p - %p\n",
			 start, end);
		dtcm_present = true;
	} else if (dtcm_code_sz) {
		pr_info("CPU DTCM: %u bytes of code compiled to DTCM but no "
			"DTCM banks present in CPU\n", dtcm_code_sz);
	}

no_dtcm:
	/* Setup ITCM if present */
	if (itcm_banks > 0) {
		for (i = 0; i < itcm_banks; i++) {
			ret = setup_tcm_bank(1, i, itcm_banks, &itcm_end);
			if (ret)
				return;
		}
		/* This means you compiled more code than fits into ITCM */
		if (itcm_code_sz > (itcm_end - ITCM_OFFSET)) {
			pr_info("CPU ITCM: %u bytes of code compiled to "
				"ITCM but only %lu bytes of ITCM present\n",
				itcm_code_sz, (itcm_end - ITCM_OFFSET));
			return;
		}
		itcm_res.end = itcm_end - 1;
		request_resource(&iomem_resource, &itcm_res);
		itcm_iomap[0].length = itcm_end - ITCM_OFFSET;
		iotable_init(itcm_iomap, 1);
		/* Copy code from RAM to ITCM */
		start = &__sitcm_text;
		end   = &__eitcm_text;
		ram   = &__itcm_start;
		memcpy(start, ram, itcm_code_sz);
		pr_debug("CPU ITCM: copied code from %p - %p\n",
			 start, end);
		itcm_present = true;
	} else if (itcm_code_sz) {
		pr_info("CPU ITCM: %u bytes of code compiled to ITCM but no "
			"ITCM banks present in CPU\n", itcm_code_sz);
	}
}

/*
 * This creates the TCM memory pool and has to be done later,
 * during the core_initicalls, since the allocator is not yet
 * up and running when the first initialization runs.
 */
static int __init setup_tcm_pool(void)
{
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
	if (dtcm_present) {
		if (dtcm_pool_start < dtcm_end) {
			ret = gen_pool_add(tcm_pool, dtcm_pool_start,
					   dtcm_end - dtcm_pool_start, -1);
			if (ret) {
				pr_err("CPU DTCM: could not add DTCM " \
				       "remainder to pool!\n");
				return ret;
			}
			pr_debug("CPU DTCM: Added %08x bytes @ %08x to " \
				 "the TCM memory pool\n",
				 dtcm_end - dtcm_pool_start,
				 dtcm_pool_start);
		}
	}

	/* Add the rest of ITCM to the TCM pool */
	if (itcm_present) {
		if (itcm_pool_start < itcm_end) {
			ret = gen_pool_add(tcm_pool, itcm_pool_start,
					   itcm_end - itcm_pool_start, -1);
			if (ret) {
				pr_err("CPU ITCM: could not add ITCM " \
				       "remainder to pool!\n");
				return ret;
			}
			pr_debug("CPU ITCM: Added %08x bytes @ %08x to " \
				 "the TCM memory pool\n",
				 itcm_end - itcm_pool_start,
				 itcm_pool_start);
		}
	}
	return 0;
}

core_initcall(setup_tcm_pool);
