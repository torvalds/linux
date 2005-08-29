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
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/page.h>

/*
 * gzip declarations
 */

#define OF(args)  args
#define STATIC static

#undef memset
#undef memcpy

/*
 * Why do we do this? Don't ask me..
 *
 * Incomprehensible are the ways of bootloaders.
 */
static void* memset(void *, int, size_t);
static void* memcpy(void *, __const void *, size_t);
#define memzero(s, n)     memset ((s), 0, (n))

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define WSIZE 0x8000		/* Window size must be at least 32k, */
				/* and a power of two */

static uch *inbuf;	     /* input buffer */
static uch window[WSIZE];    /* Sliding window buffer */

static unsigned insize = 0;  /* valid bytes in inbuf */
static unsigned inptr = 0;   /* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;  /* bytes in output buffer */

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

extern char input_data[];
extern int input_len;

static long bytes_out = 0;
static uch *output_data;
static unsigned long output_ptr = 0;

static void *malloc(int size);
static void free(void *where);

static void putstr(const char *);

extern int end;
static long free_mem_ptr = (long)&end;
static long free_mem_end_ptr;

#define INPLACE_MOVE_ROUTINE  0x1000
#define LOW_BUFFER_START      0x2000
#define LOW_BUFFER_MAX       0x90000
#define HEAP_SIZE             0x3000
static unsigned int low_buffer_end, low_buffer_size;
static int high_loaded =0;
static uch *high_buffer_start /* = (uch *)(((ulg)&end) + HEAP_SIZE)*/;

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
	free_mem_ptr = (long) *ptr;
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

static void* memset(void* s, int c, size_t n)
{
	int i;
	char *ss = (char*)s;

	for (i=0;i<n;i++) ss[i] = c;
	return s;
}

static void* memcpy(void* __dest, __const void* __src,
			    size_t __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
	return __dest;
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf(void)
{
	if (insize != 0) {
		error("ran out of input data");
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
	if ((ulg)output_data == low_buffer_end) output_data=high_buffer_start;
	c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void flush_window(void)
{
	if (high_loaded) flush_window_high();
	else flush_window_low();
}

static void error(char *x)
{
	putstr("\n\n");
	putstr(x);
	putstr("\n\n -- System halted");

	while(1);	/* Halt */
}

#define STACK_SIZE (4096)

long user_stack [STACK_SIZE];

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [STACK_SIZE] , __BOOT_DS };

static void setup_normal_output_buffer(void)
{
#ifdef STANDARD_MEMORY_BIOS_CALL
	if (RM_EXT_MEM_K < 1024) error("Less than 2MB of memory");
#else
	if ((RM_ALT_MEM_K > RM_EXT_MEM_K ? RM_ALT_MEM_K : RM_EXT_MEM_K) < 1024) error("Less than 2MB of memory");
#endif
	output_data = (char *)__PHYSICAL_START; /* Normally Points to 1M */
	free_mem_end_ptr = (long)real_mode;
}

struct moveparams {
	uch *low_buffer_start;  int lcount;
	uch *high_buffer_start; int hcount;
};

static void setup_output_buffer_if_we_run_high(struct moveparams *mv)
{
	high_buffer_start = (uch *)(((ulg)&end) + HEAP_SIZE);
#ifdef STANDARD_MEMORY_BIOS_CALL
	if (RM_EXT_MEM_K < (3*1024)) error("Less than 4MB of memory");
#else
	if ((RM_ALT_MEM_K > RM_EXT_MEM_K ? RM_ALT_MEM_K : RM_EXT_MEM_K) <
			(3*1024))
		error("Less than 4MB of memory");
#endif	
	mv->low_buffer_start = output_data = (char *)LOW_BUFFER_START;
	low_buffer_end = ((unsigned int)real_mode > LOW_BUFFER_MAX
	  ? LOW_BUFFER_MAX : (unsigned int)real_mode) & ~0xfff;
	low_buffer_size = low_buffer_end - LOW_BUFFER_START;
	high_loaded = 1;
	free_mem_end_ptr = (long)high_buffer_start;
	if ( (__PHYSICAL_START + low_buffer_size) > ((ulg)high_buffer_start)) {
		high_buffer_start = (uch *)(__PHYSICAL_START + low_buffer_size);
		mv->hcount = 0; /* say: we need not to move high_buffer */
	}
	else mv->hcount = -1;
	mv->high_buffer_start = high_buffer_start;
}

static void close_output_buffer_if_we_run_high(struct moveparams *mv)
{
	if (bytes_out > low_buffer_size) {
		mv->lcount = low_buffer_size;
		if (mv->hcount)
			mv->hcount = bytes_out - low_buffer_size;
	} else {
		mv->lcount = bytes_out;
		mv->hcount = 0;
	}
}

asmlinkage int decompress_kernel(struct moveparams *mv, void *rmode)
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

	if (free_mem_ptr < 0x100000) setup_normal_output_buffer();
	else setup_output_buffer_if_we_run_high(mv);

	makecrc();
	putstr("Uncompressing Linux... ");
	gunzip();
	putstr("Ok, booting the kernel.\n");
	if (high_loaded) close_output_buffer_if_we_run_high(mv);
	return high_loaded;
}
