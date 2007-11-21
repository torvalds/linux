/*
 * arch/sh64/boot/compressed/misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SHmedia from sh by Stuart Menefy, May 2002
 */

#include <asm/uaccess.h>

/* cache.c */
#define CACHE_ENABLE      0
#define CACHE_DISABLE     1
int cache_control(unsigned int command);

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)     memset ((s), 0, (n))

typedef unsigned char uch;
typedef unsigned short ush;
typedef unsigned long ulg;

#define WSIZE 0x8000		/* Window size must be at least 32k, */
				/* and a power of two */

static uch *inbuf;		/* input buffer */
static uch window[WSIZE];	/* Sliding window buffer */

static unsigned insize = 0;	/* valid bytes in inbuf */
static unsigned inptr = 0;	/* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;	/* bytes in output buffer */

/* gzip flag byte */
#define ASCII_FLAG   0x01	/* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02	/* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04	/* bit 2 set: extra field present */
#define ORIG_NAME    0x08	/* bit 3 set: original file name present */
#define COMMENT      0x10	/* bit 4 set: file comment present */
#define ENCRYPTED    0x20	/* bit 5 set: file is encrypted */
#define RESERVED     0xC0	/* bit 6,7:   reserved */

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())

/* Diagnostic functions */
#ifdef DEBUG
#  define Assert(cond,msg) {if(!(cond)) error(msg);}
#  define Trace(x) fprintf x
#  define Tracev(x) {if (verbose) fprintf x ;}
#  define Tracevv(x) {if (verbose>1) fprintf x ;}
#  define Tracec(c,x) {if (verbose && (c)) fprintf x ;}
#  define Tracecv(c,x) {if (verbose>1 && (c)) fprintf x ;}
#else
#  define Assert(cond,msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c,x)
#  define Tracecv(c,x)
#endif

static int fill_inbuf(void);
static void flush_window(void);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

extern char input_data[];
extern int input_len;

static long bytes_out = 0;
static uch *output_data;
static unsigned long output_ptr = 0;

static void *malloc(int size);
static void free(void *where);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

static void puts(const char *);

extern int _text;		/* Defined in vmlinux.lds.S */
extern int _end;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#define HEAP_SIZE             0x10000

#include "../../../../lib/inflate.c"

static void *malloc(int size)
{
	void *p;

	if (size < 0)
		error("Malloc error\n");
	if (free_mem_ptr == 0)
		error("Memory error\n");

	free_mem_ptr = (free_mem_ptr + 3) & ~3;	/* Align */

	p = (void *) free_mem_ptr;
	free_mem_ptr += size;

	if (free_mem_ptr >= free_mem_end_ptr)
		error("\nOut of memory\n");

	return p;
}

static void free(void *where)
{				/* Don't care */
}

static void gzip_mark(void **ptr)
{
	*ptr = (void *) free_mem_ptr;
}

static void gzip_release(void **ptr)
{
	free_mem_ptr = (long) *ptr;
}

void puts(const char *s)
{
}

void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = (char *) s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

void *memcpy(void *__dest, __const void *__src, size_t __n)
{
	int i;
	char *d = (char *) __dest, *s = (char *) __src;

	for (i = 0; i < __n; i++)
		d[i] = s[i];
	return __dest;
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf(void)
{
	if (insize != 0) {
		error("ran out of input data\n");
	}

	inbuf = input_data;
	insize = input_len;
	inptr = 1;
	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window(void)
{
	ulg c = crc;		/* temporary variable */
	unsigned n;
	uch *in, *out, ch;

	in = window;
	out = &output_data[output_ptr];
	for (n = 0; n < outcnt; n++) {
		ch = *out++ = *in++;
		c = crc_32_tab[((int) c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (ulg) outcnt;
	output_ptr += (ulg) outcnt;
	outcnt = 0;
	puts(".");
}

static void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while (1) ;		/* Halt */
}

#define STACK_SIZE (4096)
long __attribute__ ((aligned(8))) user_stack[STACK_SIZE];
long *stack_start = &user_stack[STACK_SIZE];

void decompress_kernel(void)
{
	output_data = (uch *) (CONFIG_MEMORY_START + 0x2000);
	free_mem_ptr = (unsigned long) &_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	makecrc();
	puts("Uncompressing Linux... ");
	cache_control(CACHE_ENABLE);
	gunzip();
	puts("\n");

#if 0
	/* When booting from ROM may want to do something like this if the
	 * boot loader doesn't.
	 */

	/* Set up the parameters and command line */
	{
		volatile unsigned int *parambase =
		    (int *) (CONFIG_MEMORY_START + 0x1000);

		parambase[0] = 0x1;	/* MOUNT_ROOT_RDONLY */
		parambase[1] = 0x0;	/* RAMDISK_FLAGS */
		parambase[2] = 0x0200;	/* ORIG_ROOT_DEV */
		parambase[3] = 0x0;	/* LOADER_TYPE */
		parambase[4] = 0x0;	/* INITRD_START */
		parambase[5] = 0x0;	/* INITRD_SIZE */
		parambase[6] = 0;

		strcpy((char *) ((int) parambase + 0x100),
		       "console=ttySC0,38400");
	}
#endif

	puts("Ok, booting the kernel.\n");

	cache_control(CACHE_DISABLE);
}
