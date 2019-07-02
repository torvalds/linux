/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/memblock.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */

#ifndef __UNICORE_MEMBLOCK_H__
#define __UNICORE_MEMBLOCK_H__

/*
 * Memory map description
 */
# define NR_BANKS 8

struct membank {
	unsigned long start;
	unsigned long size;
	unsigned int highmem;
};

struct meminfo {
	int nr_banks;
	struct membank bank[NR_BANKS];
};

extern struct meminfo meminfo;

#define for_each_bank(iter, mi)				\
	for (iter = 0; iter < (mi)->nr_banks; iter++)

#define bank_pfn_start(bank)	__phys_to_pfn((bank)->start)
#define bank_pfn_end(bank)	__phys_to_pfn((bank)->start + (bank)->size)
#define bank_pfn_size(bank)	((bank)->size >> PAGE_SHIFT)
#define bank_phys_start(bank)	((bank)->start)
#define bank_phys_end(bank)	((bank)->start + (bank)->size)
#define bank_phys_size(bank)	((bank)->size)

extern void uc32_memblock_init(struct meminfo *);

#endif
