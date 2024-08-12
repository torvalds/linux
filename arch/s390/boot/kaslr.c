// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2019
 */
#include <linux/pgtable.h>
#include <asm/physmem_info.h>
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

int get_random(unsigned long limit, unsigned long *value)
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

static void sort_reserved_ranges(struct reserved_range *res, unsigned long size)
{
	struct reserved_range tmp;
	int i, j;

	for (i = 1; i < size; i++) {
		tmp = res[i];
		for (j = i - 1; j >= 0 && res[j].start > tmp.start; j--)
			res[j + 1] = res[j];
		res[j + 1] = tmp;
	}
}

static unsigned long iterate_valid_positions(unsigned long size, unsigned long align,
					     unsigned long _min, unsigned long _max,
					     struct reserved_range *res, size_t res_count,
					     bool pos_count, unsigned long find_pos)
{
	unsigned long start, end, tmp_end, range_pos, pos = 0;
	struct reserved_range *res_end = res + res_count;
	struct reserved_range *skip_res;
	int i;

	align = max(align, 8UL);
	_min = round_up(_min, align);
	for_each_physmem_usable_range(i, &start, &end) {
		if (_min >= end)
			continue;
		start = round_up(start, align);
		if (start >= _max)
			break;
		start = max(_min, start);
		end = min(_max, end);

		while (start + size <= end) {
			/* skip reserved ranges below the start */
			while (res && res->end <= start) {
				res++;
				if (res >= res_end)
					res = NULL;
			}
			skip_res = NULL;
			tmp_end = end;
			/* has intersecting reserved range */
			if (res && res->start < end) {
				skip_res = res;
				tmp_end = res->start;
			}
			if (start + size <= tmp_end) {
				range_pos = (tmp_end - start - size) / align + 1;
				if (pos_count) {
					pos += range_pos;
				} else {
					if (range_pos >= find_pos)
						return start + (find_pos - 1) * align;
					find_pos -= range_pos;
				}
			}
			if (!skip_res)
				break;
			start = round_up(skip_res->end, align);
		}
	}

	return pos_count ? pos : 0;
}

/*
 * Two types of decompressor memory allocations/reserves are considered
 * differently.
 *
 * "Static" or "single" allocations are done via physmem_alloc_range() and
 * physmem_reserve(), and they are listed in physmem_info.reserved[]. Each
 * type of "static" allocation can only have one allocation per type and
 * cannot have chains.
 *
 * On the other hand, "dynamic" or "repetitive" allocations are done via
 * physmem_alloc_top_down(). These allocations are tightly packed together
 * top down from the end of online memory. physmem_alloc_pos represents
 * current position where those allocations start.
 *
 * Functions randomize_within_range() and iterate_valid_positions()
 * only consider "dynamic" allocations by never looking above
 * physmem_alloc_pos. "Static" allocations, however, are explicitly
 * considered by checking the "res" (reserves) array. The first
 * reserved_range of a "dynamic" allocation may also be checked along the
 * way, but it will always be above the maximum value anyway.
 */
unsigned long randomize_within_range(unsigned long size, unsigned long align,
				     unsigned long min, unsigned long max)
{
	struct reserved_range res[RR_MAX];
	unsigned long max_pos, pos;

	memcpy(res, physmem_info.reserved, sizeof(res));
	sort_reserved_ranges(res, ARRAY_SIZE(res));
	max = min(max, get_physmem_alloc_pos());

	max_pos = iterate_valid_positions(size, align, min, max, res, ARRAY_SIZE(res), true, 0);
	if (!max_pos)
		return 0;
	if (get_random(max_pos, &pos))
		return 0;
	return iterate_valid_positions(size, align, min, max, res, ARRAY_SIZE(res), false, pos + 1);
}
