/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/string.h>
#include "nonstdio.h"
#include "of1275.h"
#include <asm/processor.h>
#include <asm/page.h>

/* Passed from the linker */
extern char __image_begin, __image_end;
extern char __ramdisk_begin, __ramdisk_end;
extern char _start, _end;

extern unsigned int heap_max;
extern void flush_cache(void *, unsigned long);
extern void gunzip(void *, int, unsigned char *, int *);
extern void make_bi_recs(unsigned long addr, char *name, unsigned int mach,
		unsigned int progend);

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;

#define RAM_START	0x00000000
#define RAM_END		(64<<20)

#define BOOT_START	((unsigned long)_start)
#define BOOT_END	((unsigned long)(_end + 0xFFF) & ~0xFFF)

#define RAM_FREE	((unsigned long)(_end+0x1000)&~0xFFF)
#define PROG_START	0x00010000
#define PROG_SIZE	0x007f0000 /* 8MB */

#define SCRATCH_SIZE	(128 << 10)

static char scratch[SCRATCH_SIZE];	/* 1MB of scratch space for gunzip */

typedef void (*kernel_start_t)(int, int, void *, unsigned int, unsigned int);

void
boot(int a1, int a2, void *prom)
{
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned int initrd_size, initrd_start;

    printf("chrpboot starting: loaded at 0x%p\n\r", &_start);

    initrd_size = &__ramdisk_end - &__ramdisk_begin;
    if (initrd_size) {
	initrd_start = (RAM_END - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	claim(initrd_start, RAM_END - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%p (%x bytes)\n\r",
	       initrd_start, &__ramdisk_begin, initrd_size);
	memcpy((char *)initrd_start, &__ramdisk_begin, initrd_size);
    } else {
	initrd_start = 0;
	initrd_size = 0;
	a2 = 0xdeadbeef;
    }

    im = &__image_begin;
    len = &__image_end - &__image_begin;
    /* claim 4MB starting at PROG_START */
    claim(PROG_START, PROG_SIZE - PROG_START, 0);
    dst = (void *) PROG_START;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	avail_ram = scratch;
	begin_avail = avail_high = avail_ram;
	end_avail = scratch + sizeof(scratch);
	printf("gunzipping (0x%p <- 0x%p:0x%p)...", dst, im, im+len);
	gunzip(dst, 0x400000, im, &len);
	printf("done %u bytes\n\r", len);
	printf("%u bytes of heap consumed, max in use %u\n\r",
	       avail_high - begin_avail, heap_max);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    make_bi_recs(((unsigned long) dst + len), "chrpboot", _MACH_chrp,
		    (PROG_START + PROG_SIZE));

    sa = PROG_START;
    printf("start address = 0x%x\n\r", sa);

    (*(kernel_start_t)sa)(a1, a2, prom, initrd_start, initrd_size);

    printf("returned?\n\r");

    pause();
}
