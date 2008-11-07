/*
 * misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 * puts by Nick Holloway 1993, better puts by Martin Mares 1995
 * adaptation for Linux/CRIS Axis Communications AB, 1999
 *
 */

/* where the piggybacked kernel image expects itself to live.
 * it is the same address we use when we network load an uncompressed
 * image into DRAM, and it is the address the kernel is linked to live
 * at by vmlinux.lds.S
 */

#define KERNEL_LOAD_ADR 0x40004000


#include <linux/types.h>
#include <arch/svinto.h>

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static

void *memset(void *s, int c, size_t n);
void *memcpy(void *__dest, __const void *__src, size_t __n);

#define memzero(s, n)     memset((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define WSIZE 0x8000		/* Window size must be at least 32k, */
				/* and a power of two */

static uch *inbuf;	     /* input buffer */
static uch window[WSIZE];    /* Sliding window buffer */

unsigned inptr = 0;	/* index of next byte to be processed in inbuf
			 * After decompression it will contain the
			 * compressed size, and head.S will read it.
			 */

static unsigned outcnt = 0;  /* bytes in output buffer */

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ascii text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

#define get_byte() (inbuf[inptr++])

/* Diagnostic functions */
#ifdef DEBUG
#  define Assert(cond, msg) do { \
		if (!(cond)) \
			error(msg); \
	} while (0)
#  define Trace(x) fprintf x
#  define Tracev(x) do { \
		if (verbose) \
			fprintf x; \
	} while (0)
#  define Tracevv(x) do { \
		if (verbose > 1) \
			fprintf x; \
	} while (0)
#  define Tracec(c, x) do { \
		if (verbose && (c)) \
			fprintf x; \
	} while (0)
#  define Tracecv(c, x) do { \
		if (verbose > 1 && (c)) \
			fprintf x; \
	} while (0)
#else
#  define Assert(cond, msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c, x)
#  define Tracecv(c, x)
#endif

static void flush_window(void);
static void error(char *m);

extern char *input_data;  /* lives in head.S */

static long bytes_out = 0;
static uch *output_data;
static unsigned long output_ptr = 0;
static void puts(const char *);

/* the "heap" is put directly after the BSS ends, at end */

extern int _end;
static long free_mem_ptr = (long)&_end;
static long free_mem_end_ptr;

#include "../../../../../lib/inflate.c"

/* decompressor info and error messages to serial console */

static void
puts(const char *s)
{
#ifndef CONFIG_ETRAX_DEBUG_PORT_NULL
	while (*s) {
#ifdef CONFIG_ETRAX_DEBUG_PORT0
		while (!(*R_SERIAL0_STATUS & (1 << 5))) ;
		*R_SERIAL0_TR_DATA = *s++;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT1
		while (!(*R_SERIAL1_STATUS & (1 << 5))) ;
		*R_SERIAL1_TR_DATA = *s++;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT2
		while (!(*R_SERIAL2_STATUS & (1 << 5))) ;
		*R_SERIAL2_TR_DATA = *s++;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT3
		while (!(*R_SERIAL3_STATUS & (1 << 5))) ;
		*R_SERIAL3_TR_DATA = *s++;
#endif
	}
#endif
}

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

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */

static void flush_window(void)
{
	ulg c = crc;         /* temporary variable */
	unsigned n;
	uch *in, *out, ch;

	in = window;
	out = &output_data[output_ptr];
	for (n = 0; n < outcnt; n++) {
		ch = *out = *in;
		out++;
		in++;
		c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (ulg)outcnt;
	output_ptr += (ulg)outcnt;
	outcnt = 0;
}

static void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted\n");

	while (1);	/* Halt */
}

void setup_normal_output_buffer(void)
{
	output_data = (char *)KERNEL_LOAD_ADR;
}

void decompress_kernel(void)
{
	char revision;

	/* input_data is set in head.S */
	inbuf = input_data;

#ifdef CONFIG_ETRAX_DEBUG_PORT0
	*R_SERIAL0_XOFF = 0;
	*R_SERIAL0_BAUD = 0x99;
	*R_SERIAL0_TR_CTRL = 0x40;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT1
	*R_SERIAL1_XOFF = 0;
	*R_SERIAL1_BAUD = 0x99;
	*R_SERIAL1_TR_CTRL = 0x40;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT2
	*R_GEN_CONFIG = 0x08;
	*R_SERIAL2_XOFF = 0;
	*R_SERIAL2_BAUD = 0x99;
	*R_SERIAL2_TR_CTRL = 0x40;
#endif
#ifdef CONFIG_ETRAX_DEBUG_PORT3
	*R_GEN_CONFIG = 0x100;
	*R_SERIAL3_XOFF = 0;
	*R_SERIAL3_BAUD = 0x99;
	*R_SERIAL3_TR_CTRL = 0x40;
#endif

	setup_normal_output_buffer();

	makecrc();

	__asm__ volatile ("move $vr,%0" : "=rm" (revision));
	if (revision < 10) {
		puts("You need an ETRAX 100LX to run linux 2.6\n");
		while (1);
	}

	puts("Uncompressing Linux...\n");
	gunzip();
	puts("Done. Now booting the kernel.\n");
}
