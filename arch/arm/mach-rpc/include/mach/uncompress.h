/*
 *  arch/arm/mach-rpc/include/mach/uncompress.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define VIDMEM ((char *)SCREEN_START)
 
#include <linux/io.h>
#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/page.h>

int video_size_row;
unsigned char bytes_per_char_h;
extern unsigned long con_charconvtable[256];

struct param_struct {
	unsigned long page_size;
	unsigned long nr_pages;
	unsigned long ramdisk_size;
	unsigned long mountrootrdonly;
	unsigned long rootdev;
	unsigned long video_num_cols;
	unsigned long video_num_rows;
	unsigned long video_x;
	unsigned long video_y;
	unsigned long memc_control_reg;
	unsigned char sounddefault;
	unsigned char adfsdrives;
	unsigned char bytes_per_char_h;
	unsigned char bytes_per_char_v;
	unsigned long unused[256/4-11];
};

static const unsigned long palette_4[16] = {
	0x00000000,
	0x000000cc,
	0x0000cc00,             /* Green   */
	0x0000cccc,             /* Yellow  */
	0x00cc0000,             /* Blue    */
	0x00cc00cc,             /* Magenta */
	0x00cccc00,             /* Cyan    */
	0x00cccccc,             /* White   */
	0x00000000,
	0x000000ff,
	0x0000ff00,
	0x0000ffff,
	0x00ff0000,
	0x00ff00ff,
	0x00ffff00,
	0x00ffffff
};

#define palette_setpixel(p)	*(unsigned long *)(IO_START+0x00400000) = 0x10000000|((p) & 255)
#define palette_write(v)	*(unsigned long *)(IO_START+0x00400000) = 0x00000000|((v) & 0x00ffffff)

/*
 * params_phys is a linker defined symbol - see
 * arch/arm/boot/compressed/Makefile
 */
extern __attribute__((pure)) struct param_struct *params(void);
#define params (params())

#ifndef STANDALONE_DEBUG 
static unsigned long video_num_cols;
static unsigned long video_num_rows;
static unsigned long video_x;
static unsigned long video_y;
static unsigned char bytes_per_char_v;
static int white;

/*
 * This does not append a newline
 */
static void putc(int c)
{
	extern void ll_write_char(char *, char c, char white);
	int x,y;
	char *ptr;

	x = video_x;
	y = video_y;

	if (c == '\n') {
		if (++y >= video_num_rows)
			y--;
	} else if (c == '\r') {
		x = 0;
	} else {
		ptr = VIDMEM + ((y*video_num_cols*bytes_per_char_v+x)*bytes_per_char_h);
		ll_write_char(ptr, c, white);
		if (++x >= video_num_cols) {
			x = 0;
			if ( ++y >= video_num_rows ) {
				y--;
			}
		}
	}

	video_x = x;
	video_y = y;
}

static inline void flush(void)
{
}

static void error(char *x);

/*
 * Setup for decompression
 */
static void arch_decomp_setup(void)
{
	int i;
	struct tag *t = (struct tag *)params;
	unsigned int nr_pages = 0, page_size = PAGE_SIZE;

	if (t->hdr.tag == ATAG_CORE)
	{
		for (; t->hdr.size; t = tag_next(t))
		{
			if (t->hdr.tag == ATAG_VIDEOTEXT)
			{
				video_num_rows = t->u.videotext.video_lines;
				video_num_cols = t->u.videotext.video_cols;
				bytes_per_char_h = t->u.videotext.video_points;
				bytes_per_char_v = t->u.videotext.video_points;
				video_x = t->u.videotext.x;
				video_y = t->u.videotext.y;
			}

			if (t->hdr.tag == ATAG_MEM)
			{
				page_size = PAGE_SIZE;
				nr_pages += (t->u.mem.size / PAGE_SIZE);
			}
		}
	}
	else
	{
		nr_pages = params->nr_pages;
		page_size = params->page_size;
		video_num_rows = params->video_num_rows;
		video_num_cols = params->video_num_cols;
		video_x = params->video_x;
		video_y = params->video_y;
		bytes_per_char_h = params->bytes_per_char_h;
		bytes_per_char_v = params->bytes_per_char_v;
	}

	video_size_row = video_num_cols * bytes_per_char_h;
	
	if (bytes_per_char_h == 4)
		for (i = 0; i < 256; i++)
			con_charconvtable[i] =
				(i & 128 ? 1 << 0  : 0) |
				(i & 64  ? 1 << 4  : 0) |
				(i & 32  ? 1 << 8  : 0) |
				(i & 16  ? 1 << 12 : 0) |
				(i & 8   ? 1 << 16 : 0) |
				(i & 4   ? 1 << 20 : 0) |
				(i & 2   ? 1 << 24 : 0) |
				(i & 1   ? 1 << 28 : 0);
	else
		for (i = 0; i < 16; i++)
			con_charconvtable[i] =
				(i & 8   ? 1 << 0  : 0) |
				(i & 4   ? 1 << 8  : 0) |
				(i & 2   ? 1 << 16 : 0) |
				(i & 1   ? 1 << 24 : 0);


	palette_setpixel(0);
	if (bytes_per_char_h == 1) {
		palette_write (0);
		palette_write (0x00ffffff);
		for (i = 2; i < 256; i++)
			palette_write (0);
		white = 1;
	} else {
		for (i = 0; i < 256; i++)
			palette_write (i < 16 ? palette_4[i] : 0);
		white = 7;
	}

	if (nr_pages * page_size < 4096*1024) error("<4M of mem\n");
}
#endif

/*
 * nothing to do
 */
#define arch_decomp_wdog()
