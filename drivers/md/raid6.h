/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2003 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef LINUX_RAID_RAID6_H
#define LINUX_RAID_RAID6_H

#ifdef __KERNEL__

/* Set to 1 to use kernel-wide empty_zero_page */
#define RAID6_USE_EMPTY_ZERO_PAGE 0

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mempool.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/raid/md.h>
#include <linux/raid/raid5.h>

typedef raid5_conf_t raid6_conf_t; /* Same configuration */

/* Additional compute_parity mode -- updates the parity w/o LOCKING */
#define UPDATE_PARITY	4

/* We need a pre-zeroed page... if we don't want to use the kernel-provided
   one define it here */
#if RAID6_USE_EMPTY_ZERO_PAGE
# define raid6_empty_zero_page empty_zero_page
#else
extern const char raid6_empty_zero_page[PAGE_SIZE];
#endif

#else /* ! __KERNEL__ */
/* Used for testing in user space */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>

/* Not standard, but glibc defines it */
#define BITS_PER_LONG __WORDSIZE

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifndef PAGE_SIZE
# define PAGE_SIZE 4096
#endif
extern const char raid6_empty_zero_page[PAGE_SIZE];

#define __init
#define __exit
#define __attribute_const__ __attribute__((const))
#define noinline __attribute__((noinline))

#define preempt_enable()
#define preempt_disable()
#define cpu_has_feature(x) 1
#define enable_kernel_altivec()
#define disable_kernel_altivec()

#endif /* __KERNEL__ */

/* Routine choices */
struct raid6_calls {
	void (*gen_syndrome)(int, size_t, void **);
	int  (*valid)(void);	/* Returns 1 if this routine set is usable */
	const char *name;	/* Name of this routine set */
	int prefer;		/* Has special performance attribute */
};

/* Selected algorithm */
extern struct raid6_calls raid6_call;

/* Algorithm list */
extern const struct raid6_calls * const raid6_algos[];
int raid6_select_algo(void);

/* Return values from chk_syndrome */
#define RAID6_OK	0
#define RAID6_P_BAD	1
#define RAID6_Q_BAD	2
#define RAID6_PQ_BAD	3

/* Galois field tables */
extern const u8 raid6_gfmul[256][256] __attribute__((aligned(256)));
extern const u8 raid6_gfexp[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfinv[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfexi[256]      __attribute__((aligned(256)));

/* Recovery routines */
void raid6_2data_recov(int disks, size_t bytes, int faila, int failb, void **ptrs);
void raid6_datap_recov(int disks, size_t bytes, int faila, void **ptrs);
void raid6_dual_recov(int disks, size_t bytes, int faila, int failb, void **ptrs);

/* Some definitions to allow code to be compiled for testing in userspace */
#ifndef __KERNEL__

# define jiffies	raid6_jiffies()
# define printk 	printf
# define GFP_KERNEL	0
# define __get_free_pages(x,y)	((unsigned long)mmap(NULL, PAGE_SIZE << (y), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0))
# define free_pages(x,y)	munmap((void *)(x), (y)*PAGE_SIZE)

static inline void cpu_relax(void)
{
	/* Nothing */
}

#undef  HZ
#define HZ 1000
static inline uint32_t raid6_jiffies(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}

#endif /* ! __KERNEL__ */

#endif /* LINUX_RAID_RAID6_H */
