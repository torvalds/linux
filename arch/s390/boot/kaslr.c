// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2019
 */
#include <asm/mem_detect.h>
#include <asm/cpacf.h>
#include <asm/timex.h>
#include <asm/sclp.h>
#include "compressed/decompressor.h"

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

static unsigned long get_random(unsigned long limit)
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
				  (char *) entropy, (char *) entropy,
				  sizeof(entropy));
			memcpy(prng.parm_block, entropy, sizeof(entropy));
		}
		random = seed;
		cpacf_kmc(CPACF_KMC_PRNG, prng.parm_block, (u8 *) &random,
			  (u8 *) &random, sizeof(random));
		break;
	default:
		random = 0;
	}
	return random % limit;
}

unsigned long get_random_base(unsigned long safe_addr)
{
	unsigned long base, start, end, kernel_size;
	unsigned long block_sum, offset;
	int i;

	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && INITRD_START && INITRD_SIZE) {
		if (safe_addr < INITRD_START + INITRD_SIZE)
			safe_addr = INITRD_START + INITRD_SIZE;
	}
	safe_addr = ALIGN(safe_addr, THREAD_SIZE);

	kernel_size = vmlinux.image_size + vmlinux.bss_size;
	block_sum = 0;
	for_each_mem_detect_block(i, &start, &end) {
		if (memory_end_set) {
			if (start >= memory_end)
				break;
			if (end > memory_end)
				end = memory_end;
		}
		if (end - start < kernel_size)
			continue;
		block_sum += end - start - kernel_size;
	}
	if (!block_sum) {
		sclp_early_printk("KASLR disabled: not enough memory\n");
		return 0;
	}

	base = get_random(block_sum);
	if (base == 0)
		return 0;
	if (base < safe_addr)
		base = safe_addr;
	block_sum = offset = 0;
	for_each_mem_detect_block(i, &start, &end) {
		if (memory_end_set) {
			if (start >= memory_end)
				break;
			if (end > memory_end)
				end = memory_end;
		}
		if (end - start < kernel_size)
			continue;
		block_sum += end - start - kernel_size;
		if (base <= block_sum) {
			base = start + base - offset;
			base = ALIGN_DOWN(base, THREAD_SIZE);
			break;
		}
		offset = block_sum;
	}
	return base;
}
