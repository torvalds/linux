/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/string.h>
#include <asm/processor.h>
#include <asm/page.h>

#include "nonstdio.h"
#include "of1275.h"

/* Passed from the linker */
extern char __image_begin, __image_end;
extern char __ramdisk_begin[], __ramdisk_end;
extern char _start, _end;

extern char image_data[], initrd_data[];
extern int initrd_len, image_len;
extern unsigned int heap_max;
extern void flush_cache(void *start, unsigned int len);
extern void gunzip(void *, int, unsigned char *, int *);
extern void make_bi_recs(unsigned long addr, char *name, unsigned int mach,
		unsigned int progend);
extern void setup_bats(unsigned long start);

char *avail_ram;
char *begin_avail, *end_avail;
char *avail_high;

#define SCRATCH_SIZE	(128 << 10)

static char heap[SCRATCH_SIZE];

static unsigned long ram_start = 0;
static unsigned long ram_end = 0x1000000;

static unsigned long prog_start = 0x900000;
static unsigned long prog_size = 0x700000;

typedef void (*kernel_start_t)(int, int, void *);

void boot(int a1, int a2, void *prom)
{
    unsigned sa, len;
    void *dst;
    unsigned char *im;
    unsigned initrd_start, initrd_size;

    printf("coffboot starting: loaded at 0x%p\n", &_start);
    setup_bats(ram_start);

    initrd_size = (char *)(&__ramdisk_end) - (char *)(&__ramdisk_begin);
    if (initrd_size) {
	initrd_start = (ram_end - initrd_size) & ~0xFFF;
	a1 = initrd_start;
	a2 = initrd_size;
	claim(initrd_start, ram_end - initrd_start, 0);
	printf("initial ramdisk moving 0x%x <- 0x%p (%x bytes)\n\r",
	       initrd_start, (char *)(&__ramdisk_begin), initrd_size);
	memcpy((char *)initrd_start, (char *)(&__ramdisk_begin), initrd_size);
	prog_size = initrd_start - prog_start;
    } else
	a2 = 0xdeadbeef;

    im = (char *)(&__image_begin);
    len = (char *)(&__image_end) - (char *)(&__image_begin);
    /* claim 4MB starting at PROG_START */
    claim(prog_start, prog_size, 0);
    map(prog_start, prog_start, prog_size);
    dst = (void *) prog_start;
    if (im[0] == 0x1f && im[1] == 0x8b) {
	/* set up scratch space */
	begin_avail = avail_high = avail_ram = heap;
	end_avail = heap + sizeof(heap);
	printf("heap at 0x%p\n", avail_ram);
	printf("gunzipping (0x%p <- 0x%p:0x%p)...", dst, im, im+len);
	gunzip(dst, prog_size, im, &len);
	printf("done %u bytes\n", len);
	printf("%u bytes of heap consumed, max in use %u\n",
	       avail_high - begin_avail, heap_max);
    } else {
	memmove(dst, im, len);
    }

    flush_cache(dst, len);
    make_bi_recs(((unsigned long) dst + len), "coffboot", _MACH_Pmac,
		    (prog_start + prog_size));

    sa = (unsigned long)prog_start;
    printf("start address = 0x%x\n", sa);

    (*(kernel_start_t)sa)(a1, a2, prom);

    printf("returned?\n");

    pause();
}
