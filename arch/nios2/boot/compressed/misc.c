/*
 * Copyright (C) 2009 Thomas Chou <thomas@wytron.com.tw>
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SH by Stuart Menefy, Aug 1999
 *
 * Modified to use standard LinuxSH BIOS by Greg Banks 7Jul2000
 *
 * Based on arch/sh/boot/compressed/misc.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/string.h>

/*
 * gzip declarations
 */
#define OF(args)  args
#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)		memset((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;
#define WSIZE 0x8000		/* Window size must be at least 32k, */
				/* and a power of two */

static uch *inbuf;		/* input buffer */
static uch window[WSIZE];	/* Sliding window buffer */

static unsigned insize;	/* valid bytes in inbuf */
static unsigned inptr;	/* index of next byte to be processed in inbuf */
static unsigned outcnt;	/* bytes in output buffer */

/* gzip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION	0x02 /* bit 1 set: continuation of multi-part gzip
				file */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAME	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define ENCRYPTED	0x20 /* bit 5 set: file is encrypted */
#define RESERVED	0xC0 /* bit 6,7:   reserved */

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())

#ifdef DEBUG
#  define Assert(cond, msg) {if (!(cond)) error(msg); }
#  define Trace(x) fprintf x
#  define Tracev(x) {if (verbose) fprintf x ; }
#  define Tracevv(x) {if (verbose > 1) fprintf x ; }
#  define Tracec(c, x) {if (verbose && (c)) fprintf x ; }
#  define Tracecv(c, x) {if (verbose > 1 && (c)) fprintf x ; }
#else
#  define Assert(cond, msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c, x)
#  define Tracecv(c, x)
#endif
static int  fill_inbuf(void);
static void flush_window(void);
static void error(char *m);

extern char input_data[];
extern int input_len;

static long bytes_out;
static uch *output_data;
static unsigned long output_ptr;

#include "console.c"

static void error(char *m);

int puts(const char *);

extern int _end;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#define HEAP_SIZE			0x10000

#include "../../../../lib/inflate.c"

void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = (char *)s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

void *memcpy(void *__dest, __const void *__src, size_t __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i = 0; i < __n; i++)
		d[i] = s[i];
	return __dest;
}

/*
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf(void)
{
	if (insize != 0)
		error("ran out of input data");

	inbuf = input_data;
	insize = input_len;
	inptr = 1;
	return inbuf[0];
}

/*
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window(void)
{
	ulg c = crc;	/* temporary variable */
	unsigned n;
	uch *in, *out, ch;

	in = window;
	out = &output_data[output_ptr];
	for (n = 0; n < outcnt; n++) {
		ch = *out++ = *in++;
		c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (ulg)outcnt;
	output_ptr += (ulg)outcnt;
	outcnt = 0;
}

static void error(char *x)
{
	puts("\nERROR\n");
	puts(x);
	puts("\n\n -- System halted");

	while (1)	/* Halt */
		;
}

void decompress_kernel(void)
{
	output_data = (void *) (CONFIG_NIOS2_MEM_BASE |
				CONFIG_NIOS2_KERNEL_REGION_BASE);
	output_ptr = 0;
	free_mem_ptr = (unsigned long)&_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	console_init();
	makecrc();
	puts("Uncompressing Linux... ");
	gunzip();
	puts("Ok, booting the kernel.\n");
}
