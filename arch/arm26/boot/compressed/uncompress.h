/*
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define VIDMEM ((char *)0x02000000)
 
int video_num_columns, video_num_lines, video_size_row;
int white, bytes_per_char_h;
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

static struct param_struct *params = (struct param_struct *)0x0207c000;
 
/*
 * This does not append a newline
 */
static void puts(const char *s)
{
	extern void ll_write_char(char *, unsigned long);
	int x,y;
	unsigned char c;
	char *ptr;

	x = params->video_x;
	y = params->video_y;

	while ( ( c = *(unsigned char *)s++ ) != '\0' ) {
		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= video_num_lines ) {
				y--;
			}
		} else {
			ptr = VIDMEM + ((y*video_num_columns*params->bytes_per_char_v+x)*bytes_per_char_h);
			ll_write_char(ptr, c|(white<<16));
			if ( ++x >= video_num_columns ) {
				x = 0;
				if ( ++y >= video_num_lines ) {
					y--;
				}
			}
		}
	}

	params->video_x = x;
	params->video_y = y;
}

static void error(char *x);

/*
 * Setup for decompression
 */
static void arch_decomp_setup(void)
{
	int i;
	
	video_num_lines = params->video_num_rows;
	video_num_columns = params->video_num_cols;
	bytes_per_char_h = params->bytes_per_char_h;
	video_size_row = video_num_columns * bytes_per_char_h;
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

	white = bytes_per_char_h == 8 ? 0xfc : 7;

	if (params->nr_pages * params->page_size < 4096*1024) error("<4M of mem\n");
}

/*
 * nothing to do
 */
#define arch_decomp_wdog()
