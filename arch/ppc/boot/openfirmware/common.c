/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "nonstdio.h"
#include "of1275.h"
#include <linux/string.h>
#include <linux/zlib.h>
#include <asm/bootinfo.h>
#include <asm/page.h>

/* Information from the linker */
extern char __sysmap_begin, __sysmap_end;

extern int strcmp(const char *s1, const char *s2);
extern char *avail_ram, *avail_high;
extern char *end_avail;

unsigned int heap_use, heap_max;

struct memchunk {
    unsigned int size;
    struct memchunk *next;
};

static struct memchunk *freechunks;

static void *zalloc(unsigned size)
{
    void *p;
    struct memchunk **mpp, *mp;

    size = (size + 7) & -8;
    heap_use += size;
    if (heap_use > heap_max)
	heap_max = heap_use;
    for (mpp = &freechunks; (mp = *mpp) != 0; mpp = &mp->next) {
	if (mp->size == size) {
	    *mpp = mp->next;
	    return mp;
	}
    }
    p = avail_ram;
    avail_ram += size;
    if (avail_ram > avail_high)
	avail_high = avail_ram;
    if (avail_ram > end_avail) {
	printf("oops... out of memory\n\r");
	pause();
    }
    return p;
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;

	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != Z_DEFLATED || (flags & RESERVED) != 0) {
		printf("bad gzipped data\n\r");
		exit();
	}
	if ((flags & EXTRA_FIELD) != 0)
		i = 12 + src[10] + (src[11] << 8);
	if ((flags & ORIG_NAME) != 0)
		while (src[i++] != 0)
			;
	if ((flags & COMMENT) != 0)
		while (src[i++] != 0)
			;
	if ((flags & HEAD_CRC) != 0)
		i += 2;
	if (i >= *lenp) {
		printf("gunzip: ran out of data in header\n\r");
		exit();
	}

	/* Initialize ourself. */
	s.workspace = zalloc(zlib_inflate_workspacesize());
	r = zlib_inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		printf("zlib_inflateInit2 returned %d\n\r", r);
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = zlib_inflate(&s, Z_FINISH);
	if (r != Z_OK && r != Z_STREAM_END) {
		printf("inflate returned %d msg: %s\n\r", r, s.msg);
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	zlib_inflateEnd(&s);
}

/* Make a bi_rec in OF.  We need to be passed a name for BI_BOOTLOADER_ID,
 * a machine type for BI_MACHTYPE, and the location where the end of the
 * bootloader is (PROG_START + PROG_SIZE)
 */
void make_bi_recs(unsigned long addr, char *name, unsigned int mach,
		unsigned long progend)
{
	unsigned long sysmap_size;
	struct bi_record *rec;

	/* Figure out the size of a possible System.map we're going to
	 * pass along.
	 * */
	sysmap_size = (unsigned long)(&__sysmap_end) -
		(unsigned long)(&__sysmap_begin);

	/* leave a 1MB gap then align to the next 1MB boundary */
	addr = _ALIGN(addr+ (1<<20) - 1, (1<<20));
	/* oldworld machine seem very unhappy about this. -- Tom */
	if (addr >= progend)
		claim(addr, 0x1000, 0);

	rec = (struct bi_record *)addr;
	rec->tag = BI_FIRST;
	rec->size = sizeof(struct bi_record);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	rec->tag = BI_BOOTLOADER_ID;
	sprintf( (char *)rec->data, name);
	rec->size = sizeof(struct bi_record) + strlen(name) + 1;
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	rec->tag = BI_MACHTYPE;
	rec->data[0] = mach;
	rec->data[1] = 1;
	rec->size = sizeof(struct bi_record) + 2 * sizeof(unsigned long);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);

	if (sysmap_size) {
		rec->tag = BI_SYSMAP;
		rec->data[0] = (unsigned long)(&__sysmap_begin);
		rec->data[1] = sysmap_size;
		rec->size = sizeof(struct bi_record) + 2 *
			sizeof(unsigned long);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	}

	rec->tag = BI_LAST;
	rec->size = sizeof(struct bi_record);
	rec = (struct bi_record *)((unsigned long)rec + rec->size);
}
