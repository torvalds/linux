/*
 * misc.c
 * 
 * This is a collection of several routines from gzip-1.0.3 
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 * puts by Nick Holloway 1993, better puts by Martin Mares 1995
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */

#include <linux/linkage.h>
#include <linux/vmalloc.h>
#include <linux/screen_info.h>
#include <asm/io.h>
#include <asm/page.h>

/* WARNING!!
 * This code is compiled with -fPIC and it is relocated dynamically
 * at run time, but no relocation processing is performed.
 * This means that it is not safe to place pointers in static structures.
 */

/*
 * Getting to provable safe in place decompression is hard.
 * Worst case behaviours need to be analized.
 * Background information:
 *
 * The file layout is:
 *    magic[2]
 *    method[1]
 *    flags[1]
 *    timestamp[4]
 *    extraflags[1]
 *    os[1]
 *    compressed data blocks[N]
 *    crc[4] orig_len[4]
 *
 * resulting in 18 bytes of non compressed data overhead.
 *
 * Files divided into blocks
 * 1 bit (last block flag)
 * 2 bits (block type)
 *
 * 1 block occurs every 32K -1 bytes or when there 50% compression has been achieved.
 * The smallest block type encoding is always used.
 *
 * stored:
 *    32 bits length in bytes.
 *
 * fixed:
 *    magic fixed tree.
 *    symbols.
 *
 * dynamic:
 *    dynamic tree encoding.
 *    symbols.
 *
 *
 * The buffer for decompression in place is the length of the
 * uncompressed data, plus a small amount extra to keep the algorithm safe.
 * The compressed data is placed at the end of the buffer.  The output
 * pointer is placed at the start of the buffer and the input pointer
 * is placed where the compressed data starts.  Problems will occur
 * when the output pointer overruns the input pointer.
 *
 * The output pointer can only overrun the input pointer if the input
 * pointer is moving faster than the output pointer.  A condition only
 * triggered by data whose compressed form is larger than the uncompressed
 * form.
 *
 * The worst case at the block level is a growth of the compressed data
 * of 5 bytes per 32767 bytes.
 *
 * The worst case internal to a compressed block is very hard to figure.
 * The worst case can at least be boundined by having one bit that represents
 * 32764 bytes and then all of the rest of the bytes representing the very
 * very last byte.
 *
 * All of which is enough to compute an amount of extra data that is required
 * to be safe.  To avoid problems at the block level allocating 5 extra bytes
 * per 32767 bytes of data is sufficient.  To avoind problems internal to a block
 * adding an extra 32767 bytes (the worst case uncompressed block size) is
 * sufficient, to ensure that in the worst case the decompressed data for
 * block will stop the byte before the compressed data for a block begins.
 * To avoid problems with the compressed data's meta information an extra 18
 * bytes are needed.  Leading to the formula:
 *
 * extra_bytes = (uncompressed_size >> 12) + 32768 + 18 + decompressor_size.
 *
 * Adding 8 bytes per 32K is a bit excessive but much easier to calculate.
 * Adding 32768 instead of 32767 just makes for round numbers.
 * Adding the decompressor_size is necessary as it musht live after all
 * of the data as well.  Last I measured the decompressor is about 14K.
 * 10K of actuall data and 4K of bss.
 *
 */

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)     memset ((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define WSIZE 0x80000000	/* Window size must be at least 32k,
				 * and a power of two
				 * We don't actually have a window just
				 * a huge output buffer so I report
				 * a 2G windows size, as that should
				 * always be larger than our output buffer.
				 */

static uch *inbuf;	/* input buffer */
static uch *window;	/* Sliding window buffer, (and final output buffer) */

static unsigned insize;  /* valid bytes in inbuf */
static unsigned inptr;   /* index of next byte to be processed in inbuf */
static unsigned outcnt;  /* bytes in output buffer */

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

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

static int  fill_inbuf(void);
static void flush_window(void);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);
  
/*
 * This is set up by the setup-routine at boot-time
 */
static unsigned char *real_mode; /* Pointer to real-mode data */

#define RM_EXT_MEM_K   (*(unsigned short *)(real_mode + 0x2))
#ifndef STANDARD_MEMORY_BIOS_CALL
#define RM_ALT_MEM_K   (*(unsigned long *)(real_mode + 0x1e0))
#endif
#define RM_SCREEN_INFO (*(struct screen_info *)(real_mode+0))

extern unsigned char input_data[];
extern int input_len;

static long bytes_out = 0;

static void *malloc(int size);
static void free(void *where);

static void *memset(void *s, int c, unsigned n);
static void *memcpy(void *dest, const void *src, unsigned n);

static void putstr(const char *);

static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#define HEAP_SIZE             0x3000

static char *vidmem = (char *)0xb8000;
static int vidport;
static int lines, cols;

#ifdef CONFIG_X86_NUMAQ
static void * xquad_portio = NULL;
#endif

#include "../../../../lib/inflate.c"

static void *malloc(int size)
{
	void *p;

	if (size <0) error("Malloc error");
	if (free_mem_ptr <= 0) error("Memory error");

	free_mem_ptr = (free_mem_ptr + 3) & ~3;	/* Align */

	p = (void *)free_mem_ptr;
	free_mem_ptr += size;

	if (free_mem_ptr >= free_mem_end_ptr)
		error("Out of memory");

	return p;
}

static void free(void *where)
{	/* Don't care */
}

static void gzip_mark(void **ptr)
{
	*ptr = (void *) free_mem_ptr;
}

static void gzip_release(void **ptr)
{
	free_mem_ptr = (unsigned long) *ptr;
}
 
static void scroll(void)
{
	int i;

	memcpy ( vidmem, vidmem + cols * 2, ( lines - 1 ) * cols * 2 );
	for ( i = ( lines - 1 ) * cols * 2; i < lines * cols * 2; i += 2 )
		vidmem[i] = ' ';
}

static void putstr(const char *s)
{
	int x,y,pos;
	char c;

	x = RM_SCREEN_INFO.orig_x;
	y = RM_SCREEN_INFO.orig_y;

	while ( ( c = *s++ ) != '\0' ) {
		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c;
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					scroll();
					y--;
				}
			}
		}
	}

	RM_SCREEN_INFO.orig_x = x;
	RM_SCREEN_INFO.orig_y = y;

	pos = (x + cols * y) * 2;	/* Update cursor position */
	outb_p(14, vidport);
	outb_p(0xff & (pos >> 9), vidport+1);
	outb_p(15, vidport);
	outb_p(0xff & (pos >> 1), vidport+1);
}

static void* memset(void* s, int c, unsigned n)
{
	int i;
	char *ss = (char*)s;

	for (i=0;i<n;i++) ss[i] = c;
	return s;
}

static void* memcpy(void* dest, const void* src, unsigned n)
{
	int i;
	char *d = (char *)dest, *s = (char *)src;

	for (i=0;i<n;i++) d[i] = s[i];
	return dest;
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf(void)
{
	error("ran out of input data");
	return 0;
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window(void)
{
	/* With my window equal to my output buffer
	 * I only need to compute the crc here.
	 */
	ulg c = crc;         /* temporary variable */
	unsigned n;
	uch *in, ch;

	in = window;
	for (n = 0; n < outcnt; n++) {
		ch = *in++;
		c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (ulg)outcnt;
	outcnt = 0;
}

static void error(char *x)
{
	putstr("\n\n");
	putstr(x);
	putstr("\n\n -- System halted");

	while(1);	/* Halt */
}

asmlinkage void decompress_kernel(void *rmode, unsigned long end,
			uch *input_data, unsigned long input_len, uch *output)
{
	real_mode = rmode;

	if (RM_SCREEN_INFO.orig_video_mode == 7) {
		vidmem = (char *) 0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *) 0xb8000;
		vidport = 0x3d4;
	}

	lines = RM_SCREEN_INFO.orig_video_lines;
	cols = RM_SCREEN_INFO.orig_video_cols;

	window = output;  	/* Output buffer (Normally at 1M) */
	free_mem_ptr     = end;	/* Heap  */
	free_mem_end_ptr = end + HEAP_SIZE;
	inbuf  = input_data;	/* Input buffer */
	insize = input_len;
	inptr  = 0;

	if (((u32)output - CONFIG_PHYSICAL_START) & 0x3fffff)
		error("Destination address not 4M aligned");
	if (end > ((-__PAGE_OFFSET-(512 <<20)-1) & 0x7fffffff))
		error("Destination address too large");
#ifndef CONFIG_RELOCATABLE
	if ((u32)output != CONFIG_PHYSICAL_START)
		error("Wrong destination address");
#endif

	makecrc();
	putstr("Uncompressing Linux... ");
	gunzip();
	putstr("Ok, booting the kernel.\n");
	return;
}
