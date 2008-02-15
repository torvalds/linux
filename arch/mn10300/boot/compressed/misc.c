/* MN10300 Miscellaneous helper routines for kernel decompressor
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Modified by David Howells (dhowells@redhat.com)
 * - Derived from arch/x86/boot/compressed/misc_32.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/compiler.h>
#include <asm/serial-regs.h>
#include "misc.h"

#ifndef CONFIG_GDBSTUB_ON_TTYSx
/* display 'Uncompressing Linux... ' messages on ttyS0 or ttyS1 */
#if 1	/* ttyS0 */
#define CYG_DEV_BASE	0xA6FB0000
#else   /* ttyS1 */
#define CYG_DEV_BASE	0xA6FC0000
#endif

#define CYG_DEV_THR	(*((volatile __u8*)(CYG_DEV_BASE + 0x00)))
#define CYG_DEV_MCR	(*((volatile __u8*)(CYG_DEV_BASE + 0x10)))
#define SIO_MCR_DTR	0x01
#define SIO_MCR_RTS	0x02
#define CYG_DEV_LSR	(*((volatile __u8*)(CYG_DEV_BASE + 0x14)))
#define SIO_LSR_THRE	0x20		/* transmitter holding register empty */
#define SIO_LSR_TEMT	0x40		/* transmitter register empty */
#define CYG_DEV_MSR	(*((volatile __u8*)(CYG_DEV_BASE + 0x18)))
#define SIO_MSR_CTS	0x10		/* clear to send */
#define SIO_MSR_DSR	0x20		/* data set ready */

#define LSR_WAIT_FOR(STATE) \
	do { while (!(CYG_DEV_LSR & SIO_LSR_##STATE)) {} } while (0)
#define FLOWCTL_QUERY(LINE) \
	({ CYG_DEV_MSR & SIO_MSR_##LINE; })
#define FLOWCTL_WAIT_FOR(LINE) \
	do { while (!(CYG_DEV_MSR & SIO_MSR_##LINE)) {} } while (0)
#define FLOWCTL_CLEAR(LINE) \
	do { CYG_DEV_MCR &= ~SIO_MCR_##LINE; } while (0)
#define FLOWCTL_SET(LINE) \
	do { CYG_DEV_MCR |= SIO_MCR_##LINE; } while (0)
#endif

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static

#undef memset
#undef memcpy

static inline void *memset(const void *s, int c, size_t n)
{
	int i;
	char *ss = (char *) s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return (void *)s;
}

#define memzero(s, n) memset((s), 0, (n))

static inline void *memcpy(void *__dest, const void *__src, size_t __n)
{
	int i;
	const char *s = __src;
	char *d = __dest;

	for (i = 0; i < __n; i++)
		d[i] = s[i];
	return __dest;
}

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define WSIZE 0x8000	/* Window size must be at least 32k, and a power of
			 * two */

static uch *inbuf;	/* input buffer */
static uch window[WSIZE]; /* sliding window buffer */

static unsigned insize;	/* valid bytes in inbuf */
static unsigned inptr;	/* index of next byte to be processed in inbuf */
static unsigned outcnt;	/* bytes in output buffer */

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

/* Diagnostic functions */
#ifdef DEBUG
#  define Assert(cond, msg) { if (!(cond)) error(msg); }
#  define Trace(x)	fprintf x
#  define Tracev(x)	{ if (verbose) fprintf x ; }
#  define Tracevv(x)	{ if (verbose > 1) fprintf x ; }
#  define Tracec(c, x)	{ if (verbose && (c)) fprintf x ; }
#  define Tracecv(c, x)	{ if (verbose > 1 && (c)) fprintf x ; }
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
static void error(const char *) __attribute__((noreturn));
static void kputs(const char *);

static inline unsigned char get_byte(void)
{
	unsigned char ch = inptr < insize ? inbuf[inptr++] : fill_inbuf();

#if 0
	char hex[3];
	hex[0] = ((ch & 0x0f) > 9) ?
		((ch & 0x0f) + 'A' - 0xa) : ((ch & 0x0f) + '0');
	hex[1] = ((ch >> 4) > 9) ?
		((ch >> 4) + 'A' - 0xa) : ((ch >> 4) + '0');
	hex[2] = 0;
	kputs(hex);
#endif
	return ch;
}

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#ifndef STANDARD_MEMORY_BIOS_CALL
#define ALT_MEM_K (*(unsigned long *) 0x901e0)
#endif
#define SCREEN_INFO (*(struct screen_info *)0x90000)

static long bytes_out;
static uch *output_data;
static unsigned long output_ptr;


static void *malloc(int size);

static inline void free(void *where)
{	/* Don't care */
}

static unsigned long free_mem_ptr = (unsigned long) &end;
static unsigned long free_mem_end_ptr = (unsigned long) &end + 0x90000;

static inline void gzip_mark(void **ptr)
{
	kputs(".");
	*ptr = (void *) free_mem_ptr;
}

static inline void gzip_release(void **ptr)
{
	free_mem_ptr = (unsigned long) *ptr;
}

#define INPLACE_MOVE_ROUTINE	0x1000
#define LOW_BUFFER_START	0x2000
#define LOW_BUFFER_END		0x90000
#define LOW_BUFFER_SIZE		(LOW_BUFFER_END - LOW_BUFFER_START)
#define HEAP_SIZE		0x3000
static int high_loaded;
static uch *high_buffer_start /* = (uch *)(((ulg)&end) + HEAP_SIZE)*/;

static char *vidmem = (char *)0xb8000;
static int lines, cols;

#include "../../../../lib/inflate.c"

static void *malloc(int size)
{
	void *p;

	if (size < 0)
		error("Malloc error\n");
	if (!free_mem_ptr)
		error("Memory error\n");

	free_mem_ptr = (free_mem_ptr + 3) & ~3;	/* Align */

	p = (void *) free_mem_ptr;
	free_mem_ptr += size;

	if (free_mem_ptr >= free_mem_end_ptr)
		error("\nOut of memory\n");

	return p;
}

static inline void scroll(void)
{
	int i;

	memcpy(vidmem, vidmem + cols * 2, (lines - 1) * cols * 2);
	for (i = (lines - 1) * cols * 2; i < lines * cols * 2; i += 2)
		vidmem[i] = ' ';
}

static inline void kputchar(unsigned char ch)
{
#ifdef CONFIG_MN10300_UNIT_ASB2305
	while (SC0STR & SC01STR_TBF)
		continue;

	if (ch == 0x0a) {
		SC0TXB = 0x0d;
		while (SC0STR & SC01STR_TBF)
			continue;
	}

	SC0TXB = ch;

#else
	while (SC1STR & SC01STR_TBF)
		continue;

	if (ch == 0x0a) {
		SC1TXB = 0x0d;
		while (SC1STR & SC01STR_TBF)
			continue;
	}

	SC1TXB = ch;

#endif
}

static void kputs(const char *s)
{
#ifdef CONFIG_DEBUG_DECOMPRESS_KERNEL
#ifndef CONFIG_GDBSTUB_ON_TTYSx
	char ch;

	FLOWCTL_SET(DTR);

	while (*s) {
		LSR_WAIT_FOR(THRE);

		ch = *s++;
		if (ch == 0x0a) {
			CYG_DEV_THR = 0x0d;
			LSR_WAIT_FOR(THRE);
		}
		CYG_DEV_THR = ch;
	}

	FLOWCTL_CLEAR(DTR);
#else

	for (; *s; s++)
		kputchar(*s);

#endif
#endif /* CONFIG_DEBUG_DECOMPRESS_KERNEL */
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf()
{
	if (insize != 0)
		error("ran out of input data\n");

	inbuf = input_data;
	insize = input_len;
	inptr = 1;
	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window_low(void)
{
    ulg c = crc;         /* temporary variable */
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

static void flush_window_high(void)
{
    ulg c = crc;         /* temporary variable */
    unsigned n;
    uch *in,  ch;
    in = window;
    for (n = 0; n < outcnt; n++) {
	ch = *output_data++ = *in++;
	if ((ulg) output_data == LOW_BUFFER_END)
		output_data = high_buffer_start;
	c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void flush_window(void)
{
	if (high_loaded)
		flush_window_high();
	else
		flush_window_low();
}

static void error(const char *x)
{
	kputs("\n\n");
	kputs(x);
	kputs("\n\n -- System halted");

	while (1)
		/* Halt */;
}

#define STACK_SIZE (4096)

long user_stack[STACK_SIZE];

struct {
	long *a;
	short b;
} stack_start = { &user_stack[STACK_SIZE], 0 };

void setup_normal_output_buffer(void)
{
#ifdef STANDARD_MEMORY_BIOS_CALL
	if (EXT_MEM_K < 1024)
		error("Less than 2MB of memory.\n");
#else
	if ((ALT_MEM_K > EXT_MEM_K ? ALT_MEM_K : EXT_MEM_K) < 1024)
		error("Less than 2MB of memory.\n");
#endif
	output_data = (char *) 0x100000; /* Points to 1M */
}

struct moveparams {
	uch *low_buffer_start;
	int lcount;
	uch *high_buffer_start;
	int hcount;
};

void setup_output_buffer_if_we_run_high(struct moveparams *mv)
{
	high_buffer_start = (uch *)(((ulg) &end) + HEAP_SIZE);
#ifdef STANDARD_MEMORY_BIOS_CALL
	if (EXT_MEM_K < (3 * 1024))
		error("Less than 4MB of memory.\n");
#else
	if ((ALT_MEM_K > EXT_MEM_K ? ALT_MEM_K : EXT_MEM_K) < (3 * 1024))
		error("Less than 4MB of memory.\n");
#endif
	mv->low_buffer_start = output_data = (char *) LOW_BUFFER_START;
	high_loaded = 1;
	free_mem_end_ptr = (long) high_buffer_start;
	if (0x100000 + LOW_BUFFER_SIZE > (ulg) high_buffer_start) {
		high_buffer_start = (uch *)(0x100000 + LOW_BUFFER_SIZE);
		mv->hcount = 0; /* say: we need not to move high_buffer */
	} else {
		mv->hcount = -1;
	}
	mv->high_buffer_start = high_buffer_start;
}

void close_output_buffer_if_we_run_high(struct moveparams *mv)
{
	mv->lcount = bytes_out;
	if (bytes_out > LOW_BUFFER_SIZE) {
		mv->lcount = LOW_BUFFER_SIZE;
		if (mv->hcount)
			mv->hcount = bytes_out - LOW_BUFFER_SIZE;
	} else {
		mv->hcount = 0;
	}
}

#undef DEBUGFLAG
#ifdef DEBUGFLAG
int debugflag;
#endif

int decompress_kernel(struct moveparams *mv)
{
#ifdef DEBUGFLAG
	while (!debugflag)
		barrier();
#endif

	output_data = (char *) CONFIG_KERNEL_TEXT_ADDRESS;

	makecrc();
	kputs("Uncompressing Linux... ");
	gunzip();
	kputs("Ok, booting the kernel.\n");
	return 0;
}
