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

/*
 * we have to be careful, because no indirections are allowed here, and
 * paravirt_ops is a kind of one. As it will only run in baremetal anyway,
 * we just keep it from happening
 */
#undef CONFIG_PARAVIRT
#ifdef CONFIG_X86_32
#define _ASM_X86_DESC_H 1
#endif

#ifdef CONFIG_X86_64
#define _LINUX_STRING_H_ 1
#define __LINUX_BITMAP_H 1
#endif

#include <linux/linkage.h>
#include <linux/screen_info.h>
#include <linux/elf.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/boot.h>
#include <asm/bootparam.h>

/* WARNING!!
 * This code is compiled with -fPIC and it is relocated dynamically
 * at run time, but no relocation processing is performed.
 * This means that it is not safe to place pointers in static structures.
 */

/*
 * Getting to provable safe in place decompression is hard.
 * Worst case behaviours need to be analyzed.
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
 * 1 block occurs every 32K -1 bytes or when there 50% compression
 * has been achieved. The smallest block type encoding is always used.
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
 * per 32767 bytes of data is sufficient.  To avoind problems internal to a
 * block adding an extra 32767 bytes (the worst case uncompressed block size)
 * is sufficient, to ensure that in the worst case the decompressed data for
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
 * 10K of actual data and 4K of bss.
 *
 */

/*
 * gzip declarations
 */

#define OF(args)	args
#define STATIC		static

#undef memset
#undef memcpy
#define memzero(s, n)	memset((s), 0, (n))

typedef unsigned char	uch;
typedef unsigned short	ush;
typedef unsigned long	ulg;

/*
 * Window size must be at least 32k, and a power of two.
 * We don't actually have a window just a huge output buffer,
 * so we report a 2G window size, as that should always be
 * larger than our output buffer:
 */
#define WSIZE		0x80000000

/* Input buffer: */
static unsigned char	*inbuf;

/* Sliding window buffer (and final output buffer): */
static unsigned char	*window;

/* Valid bytes in inbuf: */
static unsigned		insize;

/* Index of next byte to be processed in inbuf: */
static unsigned		inptr;

/* Bytes in output buffer: */
static unsigned		outcnt;

/* gzip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION	0x02 /* bit 1 set: continuation of multi-part gz file */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAM	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define ENCRYPTED	0x20 /* bit 5 set: file is encrypted */
#define RESERVED	0xC0 /* bit 6, 7:  reserved */

#define get_byte()	(inptr < insize ? inbuf[inptr++] : fill_inbuf())

/* Diagnostic functions */
#ifdef DEBUG
#  define Assert(cond, msg) do { if (!(cond)) error(msg); } while (0)
#  define Trace(x)	do { fprintf x; } while (0)
#  define Tracev(x)	do { if (verbose) fprintf x ; } while (0)
#  define Tracevv(x)	do { if (verbose > 1) fprintf x ; } while (0)
#  define Tracec(c, x)	do { if (verbose && (c)) fprintf x ; } while (0)
#  define Tracecv(c, x)	do { if (verbose > 1 && (c)) fprintf x ; } while (0)
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

/*
 * This is set up by the setup-routine at boot-time
 */
static struct boot_params *real_mode;		/* Pointer to real-mode data */
static int quiet;

extern unsigned char input_data[];
extern int input_len;

static long bytes_out;

static void *memset(void *s, int c, unsigned n);
static void *memcpy(void *dest, const void *src, unsigned n);

static void __putstr(int, const char *);
#define putstr(__x)  __putstr(0, __x)

#ifdef CONFIG_X86_64
#define memptr long
#else
#define memptr unsigned
#endif

static memptr free_mem_ptr;
static memptr free_mem_end_ptr;

static char *vidmem;
static int vidport;
static int lines, cols;

#include "../../../../lib/inflate.c"

static void scroll(void)
{
	int i;

	memcpy(vidmem, vidmem + cols * 2, (lines - 1) * cols * 2);
	for (i = (lines - 1) * cols * 2; i < lines * cols * 2; i += 2)
		vidmem[i] = ' ';
}

static void __putstr(int error, const char *s)
{
	int x, y, pos;
	char c;

#ifndef CONFIG_X86_VERBOSE_BOOTUP
	if (!error)
		return;
#endif

#ifdef CONFIG_X86_32
	if (real_mode->screen_info.orig_video_mode == 0 &&
	    lines == 0 && cols == 0)
		return;
#endif

	x = real_mode->screen_info.orig_x;
	y = real_mode->screen_info.orig_y;

	while ((c = *s++) != '\0') {
		if (c == '\n') {
			x = 0;
			if (++y >= lines) {
				scroll();
				y--;
			}
		} else {
			vidmem[(x + cols * y) * 2] = c;
			if (++x >= cols) {
				x = 0;
				if (++y >= lines) {
					scroll();
					y--;
				}
			}
		}
	}

	real_mode->screen_info.orig_x = x;
	real_mode->screen_info.orig_y = y;

	pos = (x + cols * y) * 2;	/* Update cursor position */
	outb(14, vidport);
	outb(0xff & (pos >> 9), vidport+1);
	outb(15, vidport);
	outb(0xff & (pos >> 1), vidport+1);
}

static void *memset(void *s, int c, unsigned n)
{
	int i;
	char *ss = s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

static void *memcpy(void *dest, const void *src, unsigned n)
{
	int i;
	const char *s = src;
	char *d = dest;

	for (i = 0; i < n; i++)
		d[i] = s[i];
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
	unsigned long c = crc;         /* temporary variable */
	unsigned n;
	unsigned char *in, ch;

	in = window;
	for (n = 0; n < outcnt; n++) {
		ch = *in++;
		c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (unsigned long)outcnt;
	outcnt = 0;
}

static void error(char *x)
{
	__putstr(1, "\n\n");
	__putstr(1, x);
	__putstr(1, "\n\n -- System halted");

	while (1)
		asm("hlt");
}

static void parse_elf(void *output)
{
#ifdef CONFIG_X86_64
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs, *phdr;
#else
	Elf32_Ehdr ehdr;
	Elf32_Phdr *phdrs, *phdr;
#endif
	void *dest;
	int i;

	memcpy(&ehdr, output, sizeof(ehdr));
	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	   ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	   ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	   ehdr.e_ident[EI_MAG3] != ELFMAG3) {
		error("Kernel is not a valid ELF file");
		return;
	}

	if (!quiet)
		putstr("Parsing ELF... ");

	phdrs = malloc(sizeof(*phdrs) * ehdr.e_phnum);
	if (!phdrs)
		error("Failed to allocate space for phdrs");

	memcpy(phdrs, output + ehdr.e_phoff, sizeof(*phdrs) * ehdr.e_phnum);

	for (i = 0; i < ehdr.e_phnum; i++) {
		phdr = &phdrs[i];

		switch (phdr->p_type) {
		case PT_LOAD:
#ifdef CONFIG_RELOCATABLE
			dest = output;
			dest += (phdr->p_paddr - LOAD_PHYSICAL_ADDR);
#else
			dest = (void *)(phdr->p_paddr);
#endif
			memcpy(dest,
			       output + phdr->p_offset,
			       phdr->p_filesz);
			break;
		default: /* Ignore other PT_* */ break;
		}
	}
}

asmlinkage void decompress_kernel(void *rmode, memptr heap,
				  unsigned char *input_data,
				  unsigned long input_len,
				  unsigned char *output)
{
	real_mode = rmode;

	if (real_mode->hdr.loadflags & QUIET_FLAG)
		quiet = 1;

	if (real_mode->screen_info.orig_video_mode == 7) {
		vidmem = (char *) 0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *) 0xb8000;
		vidport = 0x3d4;
	}

	lines = real_mode->screen_info.orig_video_lines;
	cols = real_mode->screen_info.orig_video_cols;

	window = output;		/* Output buffer (Normally at 1M) */
	free_mem_ptr     = heap;	/* Heap */
	free_mem_end_ptr = heap + BOOT_HEAP_SIZE;
	inbuf  = input_data;		/* Input buffer */
	insize = input_len;
	inptr  = 0;

#ifdef CONFIG_X86_64
	if ((unsigned long)output & (__KERNEL_ALIGN - 1))
		error("Destination address not 2M aligned");
	if ((unsigned long)output >= 0xffffffffffUL)
		error("Destination address too large");
#else
	if ((u32)output & (CONFIG_PHYSICAL_ALIGN - 1))
		error("Destination address not CONFIG_PHYSICAL_ALIGN aligned");
	if (heap > ((-__PAGE_OFFSET-(512<<20)-1) & 0x7fffffff))
		error("Destination address too large");
#ifndef CONFIG_RELOCATABLE
	if ((u32)output != LOAD_PHYSICAL_ADDR)
		error("Wrong destination address");
#endif
#endif

	makecrc();
	if (!quiet)
		putstr("\nDecompressing Linux... ");
	gunzip();
	parse_elf(output);
	if (!quiet)
		putstr("done.\nBooting the kernel.\n");
	return;
}
