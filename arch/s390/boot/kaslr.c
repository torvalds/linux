// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2019
 */
#include <linux/pgtable.h>
#include <asm/mem_detect.h>
#include <asm/cpacf.h>
#include <asm/timex.h>
#include <asm/sclp.h>
#include <asm/kasan.h>
#include "decompressor.h"
#include "boot.h"

#define PRNG_MODE_TDES	 1
#define PRNG_MODE_SHA512 2
#define PRNG_MODE_TRNG	 3

struct prno_parm {
	u32 res;
	u32 reseed_counter;
	u64 stream_bytes;
	u8  V[112];
	u8  C[112];
};

struct prng_parm {
	u8  parm_block[32];
	u32 reseed_counter;
	u64 byte_counter;
};

static int check_prng(void)
{
	if (!cpacf_query_func(CPACF_KMC, CPACF_KMC_PRNG)) {
		sclp_early_printk("KASLR disabled: CPU has no PRNG\n");
		return 0;
	}
	if (cpacf_query_func(CPACF_PRNO, CPACF_PRNO_TRNG))
		return PRNG_MODE_TRNG;
	if (cpacf_query_func(CPACF_PRNO, CPACF_PRNO_SHA512_DRNG_GEN))
		return PRNG_MODE_SHA512;
	else
		return PRNG_MODE_TDES;
}

static int get_random(unsigned long limit, unsigned long *value)
{
	struct prng_parm prng = {
		/* initial parameter block for tdes mode, copied from libica */
		.parm_block = {
			0x0F, 0x2B, 0x8E, 0x63, 0x8C, 0x8E, 0xD2, 0x52,
			0x64, 0xB7, 0xA0, 0x7B, 0x75, 0x28, 0xB8, 0xF4,
			0x75, 0x5F, 0xD2, 0xA6, 0x8D, 0x97, 0x11, 0xFF,
			0x49, 0xD8, 0x23, 0xF3, 0x7E, 0x21, 0xEC, 0xA0
		},
	};
	unsigned long seed, random;
	struct prno_parm prno;
	__u64 entropy[4];
	int mode, i;

	mode = check_prng();
	seed = get_tod_clock_fast();
	switch (mode) {
	case PRNG_MODE_TRNG:
		cpacf_trng(NULL, 0, (u8 *) &random, sizeof(random));
		break;
	case PRNG_MODE_SHA512:
		cpacf_prno(CPACF_PRNO_SHA512_DRNG_SEED, &prno, NULL, 0,
			   (u8 *) &seed, sizeof(seed));
		cpacf_prno(CPACF_PRNO_SHA512_DRNG_GEN, &prno, (u8 *) &random,
			   sizeof(random), NULL, 0);
		break;
	case PRNG_MODE_TDES:
		/* add entropy */
		*(unsigned long *) prng.parm_block ^= seed;
		for (i = 0; i < 16; i++) {
			cpacf_kmc(CPACF_KMC_PRNG, prng.parm_block,
				  (u8 *) entropy, (u8 *) entropy,
				  sizeof(entropy));
			memcpy(prng.parm_block, entropy, sizeof(entropy));
		}
		random = seed;
		cpacf_kmc(CPACF_KMC_PRNG, prng.parm_block, (u8 *) &random,
			  (u8 *) &random, sizeof(random));
		break;
	default:
		return -1;
	}
	*value = random % limit;
	return 0;
}

/*
 * To randomize kernel base address we have to consider several facts:
 * 1. physical online memory might not be continuous and have holes. mem_detect
 *    info contains list of online memory ranges we should consider.
 * 2. we have several memory regions which are occupied and we should not
 *    overlap and destroy them. Currently safe_addr tells us the border below
 *    which all those occupied regions are. We are safe to use anything above
 *    safe_addr.
 * 3. the upper limit might apply as well, even if memory above that limit is
 *    online. Currently those limitations are:
 *    3.1. Limit set by "mem=" kernel command line option
 *    3.2. memory reserved at the end for kasan initialization.
 * 4. kernel base address must be aligned to THREAD_SIZE (kernel stack size).
 *    Which is required for CONFIG_CHECK_STACK. Currently THREAD_SIZE is 4 pages
 *    (16 pages when the kernel is built with kasan enabled)
 * Assumptions:
 * 1. kernel size (including .bss size) and upper memory limit are page aligned.
 * 2. mem_detect memory region start is THREAD_SIZE aligned / end is PAGE_SIZE
 *    aligned (in practice memory configurations granularity on z/VM and LPAR
 *    is 1mb).
 *
 * To guarantee uniform distribution of kernel base address among all suitable
 * addresses we generate random value just once. For that we need to build a
 * continuous range in which every value would be suitable. We can build this
 * range by simply counting all suitable addresses (let's call them positions)
 * which would be valid as kernel base address. To count positions we iterate
 * over online memory ranges. For each range which is big enough for the
 * kernel image we count all suitable addresses we can put the kernel image at
 * that is
 * (end - start - kernel_size) / THREAD_SIZE + 1
 * Two functions count_valid_kernel_positions and position_to_address help
 * to count positions in memory range given and then convert position back
 * to address.
 */
static unsigned long count_valid_kernel_positions(unsigned long kernel_size,
						  unsigned long _min,
						  unsigned long _max)
{
	unsigned long start, end, pos = 0;
	int i;

	for_each_mem_detect_usable_block(i, &start, &end) {
		if (_min >= end)
			continue;
		if (start >= _max)
			break;
		start = max(_min, start);
		end = min(_max, end);
		if (end - start < kernel_size)
			continue;
		pos += (end - start - kernel_size) / THREAD_SIZE + 1;
	}

	return pos;
}

static unsigned long position_to_address(unsigned long pos, unsigned long kernel_size,
				 unsigned long _min, unsigned long _max)
{
	unsigned long start, end;
	int i;

	for_each_mem_detect_usable_block(i, &start, &end) {
		if (_min >= end)
			continue;
		if (start >= _max)
			break;
		start = max(_min, start);
		end = min(_max, end);
		if (end - start < kernel_size)
			continue;
		if ((end - start - kernel_size) / THREAD_SIZE + 1 >= pos)
			return start + (pos - 1) * THREAD_SIZE;
		pos -= (end - start - kernel_size) / THREAD_SIZE + 1;
	}

	return 0;
}

unsigned long get_random_base(unsigned long safe_addr)
{
	unsigned long usable_total = get_mem_detect_usable_total();
	unsigned long memory_limit = get_mem_detect_end();
	unsigned long base_pos, max_pos, kernel_size;
	int i;

	/*
	 * Avoid putting kernel in the end of physical memory
	 * which vmem and kasan code will use for shadow memory and
	 * pgtable mapping allocations.
	 */
	memory_limit -= kasan_estimate_memory_needs(usable_total);
	memory_limit -= vmem_estimate_memory_needs(usable_total);

	safe_addr = ALIGN(safe_addr, THREAD_SIZE);
	kernel_size = vmlinux.image_size + vmlinux.bss_size;
	if (safe_addr + kernel_size > memory_limit)
		return 0;

	max_pos = count_valid_kernel_positions(kernel_size, safe_addr, memory_limit);
	if (!max_pos) {
		sclp_early_printk("KASLR disabled: not enough memory\n");
		return 0;
	}

	/* we need a value in the range [1, base_pos] inclusive */
	if (get_random(max_pos, &base_pos))
		return 0;
	return position_to_address(base_pos + 1, kernel_size, safe_addr, memory_limit);
}
