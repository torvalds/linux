/* vi: set sw=4 ts=4: */
/*
 * Gzip implementation for busybox
 *
 * Based on GNU gzip Copyright (C) 1992-1993 Jean-loup Gailly.
 *
 * Originally adjusted for busybox by Charles P. Wright <cpw@unix.asb.com>
 * "this is a stripped down version of gzip I put into busybox, it does
 * only standard in to standard out with -9 compression.  It also requires
 * the zcat module for some important functions."
 *
 * Adjusted further by Erik Andersen <andersen@codepoet.org> to support
 * files as well as stdin/stdout, and to generally behave itself wrt
 * command line handling.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* TODO: full support for -v for DESKTOP
 * "/usr/bin/gzip -v a bogus aa" should say:
a:       85.1% -- replaced with a.gz
gzip: bogus: No such file or directory
aa:      85.1% -- replaced with aa.gz
*/
//config:config GZIP
//config:	bool "gzip (19 kb)"
//config:	default y
//config:	help
//config:	gzip is used to compress files.
//config:	It's probably the most widely used UNIX compression program.
//config:
//config:config FEATURE_GZIP_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on GZIP && LONG_OPTS
//config:
//config:config GZIP_FAST
//config:	int "Trade memory for speed (0:small,slow - 2:fast,big)"
//config:	default 0
//config:	range 0 2
//config:	depends on GZIP
//config:	help
//config:	Enable big memory options for gzip.
//config:	0: small buffers, small hash-tables
//config:	1: larger buffers, larger hash-tables
//config:	2: larger buffers, largest hash-tables
//config:	Larger models may give slightly better compression
//config:
//config:config FEATURE_GZIP_LEVELS
//config:	bool "Enable compression levels"
//config:	default n
//config:	depends on GZIP
//config:	help
//config:	Enable support for compression levels 4-9. The default level
//config:	is 6. If levels 1-3 are specified, 4 is used.
//config:	If this option is not selected, -N options are ignored and -9
//config:	is used.
//config:
//config:config FEATURE_GZIP_DECOMPRESS
//config:	bool "Enable decompression"
//config:	default y
//config:	depends on GZIP || GUNZIP || ZCAT
//config:	help
//config:	Enable -d (--decompress) and -t (--test) options for gzip.
//config:	This will be automatically selected if gunzip or zcat is
//config:	enabled.

//applet:IF_GZIP(APPLET(gzip, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_GZIP) += gzip.o

//usage:#define gzip_trivial_usage
//usage:       "[-cfk" IF_FEATURE_GZIP_DECOMPRESS("dt") IF_FEATURE_GZIP_LEVELS("123456789") "] [FILE]..."
//usage:#define gzip_full_usage "\n\n"
//usage:       "Compress FILEs (or stdin)\n"
//usage:	IF_FEATURE_GZIP_LEVELS(
//usage:     "\n	-1..9	Compression level"
//usage:	)
//usage:	IF_FEATURE_GZIP_DECOMPRESS(
//usage:     "\n	-d	Decompress"
//usage:     "\n	-t	Test file integrity"
//usage:	)
//usage:     "\n	-c	Write to stdout"
//usage:     "\n	-f	Force"
//usage:     "\n	-k	Keep input files"
//usage:
//usage:#define gzip_example_usage
//usage:       "$ ls -la /tmp/busybox*\n"
//usage:       "-rw-rw-r--    1 andersen andersen  1761280 Apr 14 17:47 /tmp/busybox.tar\n"
//usage:       "$ gzip /tmp/busybox.tar\n"
//usage:       "$ ls -la /tmp/busybox*\n"
//usage:       "-rw-rw-r--    1 andersen andersen   554058 Apr 14 17:49 /tmp/busybox.tar.gz\n"

#include "libbb.h"
#include "bb_archive.h"

/* ===========================================================================
 */
//#define DEBUG 1
/* Diagnostic functions */
#ifdef DEBUG
static int verbose;
#  define Assert(cond,msg) { if (!(cond)) bb_error_msg(msg); }
#  define Trace(x) fprintf x
#  define Tracev(x) {if (verbose) fprintf x; }
#  define Tracevv(x) {if (verbose > 1) fprintf x; }
#  define Tracec(c,x) {if (verbose && (c)) fprintf x; }
#  define Tracecv(c,x) {if (verbose > 1 && (c)) fprintf x; }
#else
#  define Assert(cond,msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c,x)
#  define Tracecv(c,x)
#endif

/* ===========================================================================
 */
#if   CONFIG_GZIP_FAST == 0
# define SMALL_MEM
#elif CONFIG_GZIP_FAST == 1
# define MEDIUM_MEM
#elif CONFIG_GZIP_FAST == 2
# define BIG_MEM
#else
# error "Invalid CONFIG_GZIP_FAST value"
#endif

#ifndef INBUFSIZ
#  ifdef SMALL_MEM
#    define INBUFSIZ  0x2000	/* input buffer size */
#  else
#    define INBUFSIZ  0x8000	/* input buffer size */
#  endif
#endif

#ifndef OUTBUFSIZ
#  ifdef SMALL_MEM
#    define OUTBUFSIZ   8192	/* output buffer size */
#  else
#    define OUTBUFSIZ  16384	/* output buffer size */
#  endif
#endif

#ifndef DIST_BUFSIZE
#  ifdef SMALL_MEM
#    define DIST_BUFSIZE 0x2000	/* buffer for distances, see trees.c */
#  else
#    define DIST_BUFSIZE 0x8000	/* buffer for distances, see trees.c */
#  endif
#endif

/* gzip flag byte */
#define ASCII_FLAG   0x01	/* bit 0 set: file probably ascii text */
#define CONTINUATION 0x02	/* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04	/* bit 2 set: extra field present */
#define ORIG_NAME    0x08	/* bit 3 set: original file name present */
#define COMMENT      0x10	/* bit 4 set: file comment present */
#define RESERVED     0xC0	/* bit 6,7:   reserved */

/* internal file attribute */
#define UNKNOWN 0xffff
#define BINARY  0
#define ASCII   1

#ifndef WSIZE
#  define WSIZE 0x8000  /* window size--must be a power of two, and */
#endif                  /*  at least 32K for zip's deflate method */

#define MIN_MATCH  3
#define MAX_MATCH  258
/* The minimum and maximum match lengths */

#define MIN_LOOKAHEAD (MAX_MATCH+MIN_MATCH+1)
/* Minimum amount of lookahead, except at the end of the input file.
 * See deflate.c for comments about the MIN_MATCH+1.
 */

#define MAX_DIST  (WSIZE-MIN_LOOKAHEAD)
/* In order to simplify the code, particularly on 16 bit machines, match
 * distances are limited to MAX_DIST instead of WSIZE.
 */

#ifndef MAX_PATH_LEN
#  define MAX_PATH_LEN   1024	/* max pathname length */
#endif

#define seekable()    0	/* force sequential output */
#define translate_eol 0	/* no option -a yet */

#ifndef BITS
#  define BITS 16
#endif
#define INIT_BITS 9		/* Initial number of bits per code */

#define BIT_MASK    0x1f	/* Mask for 'number of compression bits' */
/* Mask 0x20 is reserved to mean a fourth header byte, and 0x40 is free.
 * It's a pity that old uncompress does not check bit 0x20. That makes
 * extension of the format actually undesirable because old compress
 * would just crash on the new format instead of giving a meaningful
 * error message. It does check the number of bits, but it's more
 * helpful to say "unsupported format, get a new version" than
 * "can only handle 16 bits".
 */

#ifdef MAX_EXT_CHARS
#  define MAX_SUFFIX  MAX_EXT_CHARS
#else
#  define MAX_SUFFIX  30
#endif

/* ===========================================================================
 * Compile with MEDIUM_MEM to reduce the memory requirements or
 * with SMALL_MEM to use as little memory as possible. Use BIG_MEM if the
 * entire input file can be held in memory (not possible on 16 bit systems).
 * Warning: defining these symbols affects HASH_BITS (see below) and thus
 * affects the compression ratio. The compressed output
 * is still correct, and might even be smaller in some cases.
 */
#ifdef SMALL_MEM
#  define HASH_BITS  13	/* Number of bits used to hash strings */
#endif
#ifdef MEDIUM_MEM
#  define HASH_BITS  14
#endif
#ifndef HASH_BITS
#  define HASH_BITS  15
   /* For portability to 16 bit machines, do not use values above 15. */
#endif

#define HASH_SIZE (unsigned)(1<<HASH_BITS)
#define HASH_MASK (HASH_SIZE-1)
#define WMASK     (WSIZE-1)
/* HASH_SIZE and WSIZE must be powers of two */
#ifndef TOO_FAR
#  define TOO_FAR 4096
#endif
/* Matches of length 3 are discarded if their distance exceeds TOO_FAR */

/* ===========================================================================
 * These types are not really 'char', 'short' and 'long'
 */
typedef uint8_t uch;
typedef uint16_t ush;
typedef uint32_t ulg;
typedef int32_t lng;

typedef ush Pos;
typedef unsigned IPos;
/* A Pos is an index in the character window. We use short instead of int to
 * save space in the various tables. IPos is used only for parameter passing.
 */

enum {
	WINDOW_SIZE = 2 * WSIZE,
/* window size, 2*WSIZE except for MMAP or BIG_MEM, where it is the
 * input file length plus MIN_LOOKAHEAD.
 */

#if !ENABLE_FEATURE_GZIP_LEVELS

	max_chain_length = 4096,
/* To speed up deflation, hash chains are never searched beyond this length.
 * A higher limit improves compression ratio but degrades the speed.
 */

	max_lazy_match = 258,
/* Attempt to find a better match only when the current match is strictly
 * smaller than this value. This mechanism is used only for compression
 * levels >= 4.
 */

	max_insert_length = max_lazy_match,
/* Insert new strings in the hash table only if the match length
 * is not greater than this length. This saves time but degrades compression.
 * max_insert_length is used only for compression levels <= 3.
 */

	good_match = 32,
/* Use a faster search when the previous match is longer than this */

/* Values for max_lazy_match, good_match and max_chain_length, depending on
 * the desired pack level (0..9). The values given below have been tuned to
 * exclude worst case performance for pathological files. Better values may be
 * found for specific files.
 */

	nice_match = 258,	/* Stop searching when current match exceeds this */
/* Note: the deflate() code requires max_lazy >= MIN_MATCH and max_chain >= 4
 * For deflate_fast() (levels <= 3) good is ignored and lazy has a different
 * meaning.
 */
#endif /* ENABLE_FEATURE_GZIP_LEVELS */
};

struct globals {
/* =========================================================================== */
/* global buffers, allocated once */

#define DECLARE(type, array, size) \
	type * array
#define ALLOC(type, array, size) \
	array = xzalloc((size_t)(((size)+1L)/2) * 2*sizeof(type))
#define FREE(array) \
	do { free(array); array = NULL; } while (0)

	/* buffer for literals or lengths */
	/* DECLARE(uch, l_buf, LIT_BUFSIZE); */
	DECLARE(uch, l_buf, INBUFSIZ);

	DECLARE(ush, d_buf, DIST_BUFSIZE);
	DECLARE(uch, outbuf, OUTBUFSIZ);

/* Sliding window. Input bytes are read into the second half of the window,
 * and move to the first half later to keep a dictionary of at least WSIZE
 * bytes. With this organization, matches are limited to a distance of
 * WSIZE-MAX_MATCH bytes, but this ensures that IO is always
 * performed with a length multiple of the block size. Also, it limits
 * the window size to 64K, which is quite useful on MSDOS.
 * To do: limit the window size to WSIZE+BSZ if SMALL_MEM (the code would
 * be less efficient).
 */
	DECLARE(uch, window, 2L * WSIZE);

/* Link to older string with same hash index. To limit the size of this
 * array to 64K, this link is maintained only for the last 32K strings.
 * An index in this array is thus a window index modulo 32K.
 */
	/* DECLARE(Pos, prev, WSIZE); */
	DECLARE(ush, prev, 1L << BITS);

/* Heads of the hash chains or 0. */
	/* DECLARE(Pos, head, 1<<HASH_BITS); */
#define head (G1.prev + WSIZE) /* hash head (see deflate.c) */

/* =========================================================================== */
/* all members below are zeroed out in pack_gzip() for each next file */

	uint32_t crc;	/* shift register contents */
	/*uint32_t *crc_32_tab;*/

#if ENABLE_FEATURE_GZIP_LEVELS
	unsigned max_chain_length;
	unsigned max_lazy_match;
	unsigned good_match;
	unsigned nice_match;
#define max_chain_length (G1.max_chain_length)
#define max_lazy_match   (G1.max_lazy_match)
#define good_match	 (G1.good_match)
#define nice_match	 (G1.nice_match)
#endif

/* window position at the beginning of the current output block. Gets
 * negative when the window is moved backwards.
 */
	lng block_start;

	unsigned ins_h;	/* hash index of string to be inserted */

/* Number of bits by which ins_h and del_h must be shifted at each
 * input step. It must be such that after MIN_MATCH steps, the oldest
 * byte no longer takes part in the hash key, that is:
 * H_SHIFT * MIN_MATCH >= HASH_BITS
 */
#define H_SHIFT  ((HASH_BITS+MIN_MATCH-1) / MIN_MATCH)

/* Length of the best match at previous step. Matches not greater than this
 * are discarded. This is used in the lazy match evaluation.
 */
	unsigned prev_length;

	unsigned strstart;	/* start of string to insert */
	unsigned match_start;	/* start of matching string */
	unsigned lookahead;	/* number of valid bytes ahead in window */

/* number of input bytes */
	ulg isize;		/* only 32 bits stored in .gz file */

/* bbox always use stdin/stdout */
#define ifd STDIN_FILENO	/* input file descriptor */
#define ofd STDOUT_FILENO	/* output file descriptor */

#ifdef DEBUG
	unsigned insize;	/* valid bytes in l_buf */
#endif
	unsigned outcnt;	/* bytes in output buffer */
	smallint eofile;	/* flag set at end of input file */

/* ===========================================================================
 * Local data used by the "bit string" routines.
 */

/* Output buffer. bits are inserted starting at the bottom (least significant
 * bits).
 */
	unsigned bi_buf;	/* was unsigned short */

#undef BUF_SIZE
#define BUF_SIZE (int)(8 * sizeof(G1.bi_buf))

/* Number of bits used within bi_buf. (bi_buf might be implemented on
 * more than 16 bits on some systems.)
 */
	unsigned bi_valid;

#ifdef DEBUG
	ulg bits_sent;	/* bit length of the compressed data */
# define DEBUG_bits_sent(v) (void)(G1.bits_sent v)
#else
# define DEBUG_bits_sent(v) ((void)0)
#endif
};

#define G1 (*(ptr_to_globals - 1))

/* ===========================================================================
 * Write the output buffer outbuf[0..outcnt-1] and update bytes_out.
 * (used for the compressed data only)
 */
static void flush_outbuf(void)
{
	if (G1.outcnt == 0)
		return;

	xwrite(ofd, (char *) G1.outbuf, G1.outcnt);
	G1.outcnt = 0;
}

/* ===========================================================================
 */
/* put_8bit is used for the compressed output */
#define put_8bit(c) \
do { \
	G1.outbuf[G1.outcnt++] = (c); \
	if (G1.outcnt == OUTBUFSIZ) \
		flush_outbuf(); \
} while (0)

/* Output a 16 bit value, lsb first */
static void put_16bit(ush w)
{
	/* GCC 4.2.1 won't optimize out redundant loads of G1.outcnt
	 * (probably because of fear of aliasing with G1.outbuf[]
	 * stores), do it explicitly:
	 */
	unsigned outcnt = G1.outcnt;
	uch *dst = &G1.outbuf[outcnt];

#if BB_UNALIGNED_MEMACCESS_OK && BB_LITTLE_ENDIAN
	if (outcnt < OUTBUFSIZ-2) {
		/* Common case */
		ush *dst16 = (void*) dst;
		*dst16 = w; /* unaligned LSB 16-bit store */
		G1.outcnt = outcnt + 2;
		return;
	}
	*dst = (uch)w;
	w >>= 8;
	G1.outcnt = ++outcnt;
#else
	*dst = (uch)w;
	w >>= 8;
	if (outcnt < OUTBUFSIZ-2) {
		/* Common case */
		dst[1] = w;
		G1.outcnt = outcnt + 2;
		return;
	}
	G1.outcnt = ++outcnt;
#endif

	/* Slowpath: we will need to do flush_outbuf() */
	if (outcnt == OUTBUFSIZ)
		flush_outbuf(); /* here */
	put_8bit(w); /* or here */
}

#define OPTIMIZED_PUT_32BIT (CONFIG_GZIP_FAST > 0 && BB_UNALIGNED_MEMACCESS_OK && BB_LITTLE_ENDIAN)
static void put_32bit(ulg n)
{
	if (OPTIMIZED_PUT_32BIT) {
		unsigned outcnt = G1.outcnt;
		if (outcnt < OUTBUFSIZ-4) {
			/* Common case */
			ulg *dst32 = (void*) &G1.outbuf[outcnt];
			*dst32 = n; /* unaligned LSB 32-bit store */
			//bb_error_msg("%p", dst32); // store alignment debugging
			G1.outcnt = outcnt + 4;
			return;
		}
	}
	put_16bit(n);
	put_16bit(n >> 16);
}
static ALWAYS_INLINE void flush_outbuf_if_32bit_optimized(void)
{
	/* If put_32bit() performs 32bit stores && it is used in send_bits() */
	if (OPTIMIZED_PUT_32BIT && BUF_SIZE > 16)
		flush_outbuf();
}

/* ===========================================================================
 * Run a set of bytes through the crc shift register.  If s is a NULL
 * pointer, then initialize the crc shift register contents instead.
 * Return the current crc in either case.
 */
static void updcrc(uch * s, unsigned n)
{
	G1.crc = crc32_block_endian0(G1.crc, s, n, global_crc32_table /*G1.crc_32_tab*/);
}

/* ===========================================================================
 * Read a new buffer from the current input file, perform end-of-line
 * translation, and update the crc and input file size.
 * IN assertion: size >= 2 (for end-of-line translation)
 */
static unsigned file_read(void *buf, unsigned size)
{
	unsigned len;

	Assert(G1.insize == 0, "l_buf not empty");

	len = safe_read(ifd, buf, size);
	if (len == (unsigned)(-1) || len == 0)
		return len;

	updcrc(buf, len);
	G1.isize += len;
	return len;
}

/* ===========================================================================
 * Send a value on a given number of bits.
 * IN assertion: length <= 16 and value fits in length bits.
 */
static void send_bits(unsigned value, unsigned length)
{
	unsigned new_buf;

#ifdef DEBUG
	Tracev((stderr, " l %2d v %4x ", length, value));
	Assert(length > 0 && length <= 15, "invalid length");
	DEBUG_bits_sent(+= length);
#endif
	BUILD_BUG_ON(BUF_SIZE != 32 && BUF_SIZE != 16);

	new_buf = G1.bi_buf | (value << G1.bi_valid);
	/* NB: the above may sometimes do "<< 32" shift (undefined)
	 * if check below is changed to "length > BUF_SIZE" instead of >= */
	length += G1.bi_valid;

	/* If bi_buf is full */
	if (length >= BUF_SIZE) {
		/* ...use (valid) bits from bi_buf and
		 * (BUF_SIZE - bi_valid) bits from value,
		 *  leaving (width - (BUF_SIZE-bi_valid)) unused bits in value.
		 */
		value >>= (BUF_SIZE - G1.bi_valid);
		if (BUF_SIZE == 32) {
			put_32bit(new_buf);
		} else { /* 16 */
			put_16bit(new_buf);
		}
		new_buf = value;
		length -= BUF_SIZE;
	}
	G1.bi_buf = new_buf;
	G1.bi_valid = length;
}

/* ===========================================================================
 * Reverse the first len bits of a code, using straightforward code (a faster
 * method would use a table)
 * IN assertion: 1 <= len <= 15
 */
static unsigned bi_reverse(unsigned code, int len)
{
	unsigned res = 0;

	while (1) {
		res |= code & 1;
		if (--len <= 0) return res;
		code >>= 1;
		res <<= 1;
	}
}

/* ===========================================================================
 * Write out any remaining bits in an incomplete byte.
 */
static void bi_windup(void)
{
	unsigned bits = G1.bi_buf;
	int cnt = G1.bi_valid;

	while (cnt > 0) {
		put_8bit(bits);
		bits >>= 8;
		cnt -= 8;
	}
	G1.bi_buf = 0;
	G1.bi_valid = 0;
	DEBUG_bits_sent(= (G1.bits_sent + 7) & ~7);
}

/* ===========================================================================
 * Copy a stored block to the zip file, storing first the length and its
 * one's complement if requested.
 */
static void copy_block(char *buf, unsigned len, int header)
{
	bi_windup();		/* align on byte boundary */

	if (header) {
		unsigned v = ((uint16_t)len) | ((~len) << 16);
		put_32bit(v);
		DEBUG_bits_sent(+= 2 * 16);
	}
	DEBUG_bits_sent(+= (ulg) len << 3);
	while (len--) {
		put_8bit(*buf++);
	}
	/* The above can 32-bit misalign outbuf */
	if (G1.outcnt & 3) /* syscalls are expensive, is it really misaligned? */
		flush_outbuf_if_32bit_optimized();
}

/* ===========================================================================
 * Fill the window when the lookahead becomes insufficient.
 * Updates strstart and lookahead, and sets eofile if end of input file.
 * IN assertion: lookahead < MIN_LOOKAHEAD && strstart + lookahead > 0
 * OUT assertions: at least one byte has been read, or eofile is set;
 *    file reads are performed for at least two bytes (required for the
 *    translate_eol option).
 */
static void fill_window(void)
{
	unsigned n, m;
	unsigned more =	WINDOW_SIZE - G1.lookahead - G1.strstart;
	/* Amount of free space at the end of the window. */

	/* If the window is almost full and there is insufficient lookahead,
	 * move the upper half to the lower one to make room in the upper half.
	 */
	if (more == (unsigned) -1) {
		/* Very unlikely, but possible on 16 bit machine if strstart == 0
		 * and lookahead == 1 (input done one byte at time)
		 */
		more--;
	} else if (G1.strstart >= WSIZE + MAX_DIST) {
		/* By the IN assertion, the window is not empty so we can't confuse
		 * more == 0 with more == 64K on a 16 bit machine.
		 */
		Assert(WINDOW_SIZE == 2 * WSIZE, "no sliding with BIG_MEM");

		memcpy(G1.window, G1.window + WSIZE, WSIZE);
		G1.match_start -= WSIZE;
		G1.strstart -= WSIZE;	/* we now have strstart >= MAX_DIST: */

		G1.block_start -= WSIZE;

		for (n = 0; n < HASH_SIZE; n++) {
			m = head[n];
			head[n] = (Pos) (m >= WSIZE ? m - WSIZE : 0);
		}
		for (n = 0; n < WSIZE; n++) {
			m = G1.prev[n];
			G1.prev[n] = (Pos) (m >= WSIZE ? m - WSIZE : 0);
			/* If n is not on any hash chain, prev[n] is garbage but
			 * its value will never be used.
			 */
		}
		more += WSIZE;
	}
	/* At this point, more >= 2 */
	if (!G1.eofile) {
		n = file_read(G1.window + G1.strstart + G1.lookahead, more);
		if (n == 0 || n == (unsigned) -1) {
			G1.eofile = 1;
		} else {
			G1.lookahead += n;
		}
	}
}
/* Both users fill window with the same loop: */
static void fill_window_if_needed(void)
{
	while (G1.lookahead < MIN_LOOKAHEAD && !G1.eofile)
		fill_window();
}

/* ===========================================================================
 * Set match_start to the longest match starting at the given string and
 * return its length. Matches shorter or equal to prev_length are discarded,
 * in which case the result is equal to prev_length and match_start is
 * garbage.
 * IN assertions: cur_match is the head of the hash chain for the current
 *   string (strstart) and its distance is <= MAX_DIST, and prev_length >= 1
 */

/* For MSDOS, OS/2 and 386 Unix, an optimized version is in match.asm or
 * match.s. The code is functionally equivalent, so you can use the C version
 * if desired.
 */
static int longest_match(IPos cur_match)
{
	unsigned chain_length = max_chain_length;	/* max hash chain length */
	uch *scan = G1.window + G1.strstart;	/* current string */
	uch *match;	/* matched string */
	int len;	/* length of current match */
	int best_len = G1.prev_length;	/* best match length so far */
	IPos limit = G1.strstart > (IPos) MAX_DIST ? G1.strstart - (IPos) MAX_DIST : 0;
	/* Stop when cur_match becomes <= limit. To simplify the code,
	 * we prevent matches with the string of window index 0.
	 */

/* The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
 * It is easy to get rid of this optimization if necessary.
 */
#if HASH_BITS < 8 || MAX_MATCH != 258
#  error Code too clever
#endif
	uch *strend = G1.window + G1.strstart + MAX_MATCH;
	uch scan_end1 = scan[best_len - 1];
	uch scan_end = scan[best_len];

	/* Do not waste too much time if we already have a good match: */
	if (G1.prev_length >= good_match) {
		chain_length >>= 2;
	}
	Assert(G1.strstart <= WINDOW_SIZE - MIN_LOOKAHEAD, "insufficient lookahead");

	do {
		Assert(cur_match < G1.strstart, "no future");
		match = G1.window + cur_match;

		/* Skip to next match if the match length cannot increase
		 * or if the match length is less than 2:
		 */
		if (match[best_len] != scan_end
		 || match[best_len - 1] != scan_end1
		 || *match != *scan || *++match != scan[1]
		) {
			continue;
		}

		/* The check at best_len-1 can be removed because it will be made
		 * again later. (This heuristic is not always a win.)
		 * It is not necessary to compare scan[2] and match[2] since they
		 * are always equal when the other bytes match, given that
		 * the hash keys are equal and that HASH_BITS >= 8.
		 */
		scan += 2, match++;

		/* We check for insufficient lookahead only every 8th comparison;
		 * the 256th check will be made at strstart+258.
		 */
		do {
		} while (*++scan == *++match && *++scan == *++match &&
				 *++scan == *++match && *++scan == *++match &&
				 *++scan == *++match && *++scan == *++match &&
				 *++scan == *++match && *++scan == *++match && scan < strend);

		len = MAX_MATCH - (int) (strend - scan);
		scan = strend - MAX_MATCH;

		if (len > best_len) {
			G1.match_start = cur_match;
			best_len = len;
			if (len >= nice_match)
				break;
			scan_end1 = scan[best_len - 1];
			scan_end = scan[best_len];
		}
	} while ((cur_match = G1.prev[cur_match & WMASK]) > limit
			 && --chain_length != 0);

	return best_len;
}

#ifdef DEBUG
/* ===========================================================================
 * Check that the match at match_start is indeed a match.
 */
static void check_match(IPos start, IPos match, int length)
{
	/* check that the match is indeed a match */
	if (memcmp(G1.window + match, G1.window + start, length) != 0) {
		bb_error_msg(" start %d, match %d, length %d", start, match, length);
		bb_error_msg("invalid match");
	}
	if (verbose > 1) {
		bb_error_msg("\\[%d,%d]", start - match, length);
		do {
			bb_putchar_stderr(G1.window[start++]);
		} while (--length != 0);
	}
}
#else
#  define check_match(start, match, length) ((void)0)
#endif


/* trees.c -- output deflated data using Huffman coding
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

/*  PURPOSE
 *      Encode various sets of source values using variable-length
 *      binary code trees.
 *
 *  DISCUSSION
 *      The PKZIP "deflation" process uses several Huffman trees. The more
 *      common source values are represented by shorter bit sequences.
 *
 *      Each code tree is stored in the ZIP file in a compressed form
 *      which is itself a Huffman encoding of the lengths of
 *      all the code strings (in ascending order by source values).
 *      The actual code strings are reconstructed from the lengths in
 *      the UNZIP process, as described in the "application note"
 *      (APPNOTE.TXT) distributed as part of PKWARE's PKZIP program.
 *
 *  REFERENCES
 *      Lynch, Thomas J.
 *          Data Compression:  Techniques and Applications, pp. 53-55.
 *          Lifetime Learning Publications, 1985.  ISBN 0-534-03418-7.
 *
 *      Storer, James A.
 *          Data Compression:  Methods and Theory, pp. 49-50.
 *          Computer Science Press, 1988.  ISBN 0-7167-8156-5.
 *
 *      Sedgewick, R.
 *          Algorithms, p290.
 *          Addison-Wesley, 1983. ISBN 0-201-06672-6.
 *
 *  INTERFACE
 *      void ct_init()
 *          Allocate the match buffer, initialize the various tables [and save
 *          the location of the internal file attribute (ascii/binary) and
 *          method (DEFLATE/STORE) -- deleted in bbox]
 *
 *      void ct_tally(int dist, int lc);
 *          Save the match info and tally the frequency counts.
 *
 *      ulg flush_block(char *buf, ulg stored_len, int eof)
 *          Determine the best encoding for the current block: dynamic trees,
 *          static trees or store, and output the encoded block to the zip
 *          file. Returns the total compressed length for the file so far.
 */

#define MAX_BITS 15
/* All codes must not exceed MAX_BITS bits */

#define MAX_BL_BITS 7
/* Bit length codes must not exceed MAX_BL_BITS bits */

#define LENGTH_CODES 29
/* number of length codes, not counting the special END_BLOCK code */

#define LITERALS  256
/* number of literal bytes 0..255 */

#define END_BLOCK 256
/* end of block literal code */

#define L_CODES (LITERALS+1+LENGTH_CODES)
/* number of Literal or Length codes, including the END_BLOCK code */

#define D_CODES   30
/* number of distance codes */

#define BL_CODES  19
/* number of codes used to transfer the bit lengths */

/* extra bits for each length code */
static const uint8_t extra_lbits[LENGTH_CODES] ALIGN1 = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
	4, 4, 5, 5, 5, 5, 0
};

/* extra bits for each distance code */
static const uint8_t extra_dbits[D_CODES] ALIGN1 = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
	10, 10, 11, 11, 12, 12, 13, 13
};

/* extra bits for each bit length code */
static const uint8_t extra_blbits[BL_CODES] ALIGN1 = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7 };

/* number of codes at each bit length for an optimal tree */
static const uint8_t bl_order[BL_CODES] ALIGN1 = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

#define STORED_BLOCK 0
#define STATIC_TREES 1
#define DYN_TREES    2
/* The three kinds of block type */

#ifndef LIT_BUFSIZE
#  ifdef SMALL_MEM
#    define LIT_BUFSIZE  0x2000
#  else
#  ifdef MEDIUM_MEM
#    define LIT_BUFSIZE  0x4000
#  else
#    define LIT_BUFSIZE  0x8000
#  endif
#  endif
#endif
#ifndef DIST_BUFSIZE
#  define DIST_BUFSIZE  LIT_BUFSIZE
#endif
/* Sizes of match buffers for literals/lengths and distances.  There are
 * 4 reasons for limiting LIT_BUFSIZE to 64K:
 *   - frequencies can be kept in 16 bit counters
 *   - if compression is not successful for the first block, all input data is
 *     still in the window so we can still emit a stored block even when input
 *     comes from standard input.  (This can also be done for all blocks if
 *     LIT_BUFSIZE is not greater than 32K.)
 *   - if compression is not successful for a file smaller than 64K, we can
 *     even emit a stored file instead of a stored block (saving 5 bytes).
 *   - creating new Huffman trees less frequently may not provide fast
 *     adaptation to changes in the input data statistics. (Take for
 *     example a binary file with poorly compressible code followed by
 *     a highly compressible string table.) Smaller buffer sizes give
 *     fast adaptation but have of course the overhead of transmitting trees
 *     more frequently.
 *   - I can't count above 4
 * The current code is general and allows DIST_BUFSIZE < LIT_BUFSIZE (to save
 * memory at the expense of compression). Some optimizations would be possible
 * if we rely on DIST_BUFSIZE == LIT_BUFSIZE.
 */
#define REP_3_6      16
/* repeat previous bit length 3-6 times (2 bits of repeat count) */
#define REPZ_3_10    17
/* repeat a zero length 3-10 times  (3 bits of repeat count) */
#define REPZ_11_138  18
/* repeat a zero length 11-138 times  (7 bits of repeat count) */

/* ===========================================================================
*/
/* Data structure describing a single value and its code string. */
typedef struct ct_data {
	union {
		ush freq;		/* frequency count */
		ush code;		/* bit string */
	} fc;
	union {
		ush dad;		/* father node in Huffman tree */
		ush len;		/* length of bit string */
	} dl;
} ct_data;

#define Freq fc.freq
#define Code fc.code
#define Dad  dl.dad
#define Len  dl.len

#define HEAP_SIZE (2*L_CODES + 1)
/* maximum heap size */

typedef struct tree_desc {
	ct_data *dyn_tree;	/* the dynamic tree */
	ct_data *static_tree;	/* corresponding static tree or NULL */
	const uint8_t *extra_bits;	/* extra bits for each code or NULL */
	int extra_base;		/* base index for extra_bits */
	int elems;			/* max number of elements in the tree */
	int max_length;		/* max bit length for the codes */
	int max_code;		/* largest code with non zero frequency */
} tree_desc;

struct globals2 {

	ush heap[HEAP_SIZE];     /* heap used to build the Huffman trees */
	int heap_len;            /* number of elements in the heap */
	int heap_max;            /* element of largest frequency */

/* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
 * The same heap array is used to build all trees.
 */

	ct_data dyn_ltree[HEAP_SIZE];	/* literal and length tree */
	ct_data dyn_dtree[2 * D_CODES + 1];	/* distance tree */

	ct_data static_ltree[L_CODES + 2];

/* The static literal tree. Since the bit lengths are imposed, there is no
 * need for the L_CODES extra codes used during heap construction. However
 * The codes 286 and 287 are needed to build a canonical tree (see ct_init
 * below).
 */

	ct_data static_dtree[D_CODES];

/* The static distance tree. (Actually a trivial tree since all codes use
 * 5 bits.)
 */

	ct_data bl_tree[2 * BL_CODES + 1];

/* Huffman tree for the bit lengths */

	tree_desc l_desc;
	tree_desc d_desc;
	tree_desc bl_desc;

	ush bl_count[MAX_BITS + 1];

/* The lengths of the bit length codes are sent in order of decreasing
 * probability, to avoid transmitting the lengths for unused bit length codes.
 */

	uch depth[2 * L_CODES + 1];

/* Depth of each subtree used as tie breaker for trees of equal frequency */

	uch length_code[MAX_MATCH - MIN_MATCH + 1];

/* length code for each normalized match length (0 == MIN_MATCH) */

	uch dist_code[512];

/* distance codes. The first 256 values correspond to the distances
 * 3 .. 258, the last 256 values correspond to the top 8 bits of
 * the 15 bit distances.
 */

	int base_length[LENGTH_CODES];

/* First normalized length for each code (0 = MIN_MATCH) */

	int base_dist[D_CODES];

/* First normalized distance for each code (0 = distance of 1) */

	uch flag_buf[LIT_BUFSIZE / 8];

/* flag_buf is a bit array distinguishing literals from lengths in
 * l_buf, thus indicating the presence or absence of a distance.
 */

	unsigned last_lit;       /* running index in l_buf */
	unsigned last_dist;      /* running index in d_buf */
	unsigned last_flags;     /* running index in flag_buf */
	uch flags;               /* current flags not yet saved in flag_buf */
	uch flag_bit;            /* current bit used in flags */

/* bits are filled in flags starting at bit 0 (least significant).
 * Note: these flags are overkill in the current code since we don't
 * take advantage of DIST_BUFSIZE == LIT_BUFSIZE.
 */

	ulg opt_len;             /* bit length of current block with optimal trees */
	ulg static_len;          /* bit length of current block with static trees */

//	ulg compressed_len;      /* total bit length of compressed file */
};

#define G2ptr ((struct globals2*)(ptr_to_globals))
#define G2 (*G2ptr)

/* ===========================================================================
 */
#ifndef DEBUG
/* Send a code of the given tree. c and tree must not have side effects */
#  define SEND_CODE(c, tree) send_bits(tree[c].Code, tree[c].Len)
#else
#  define SEND_CODE(c, tree) \
{ \
	if (verbose > 1) bb_error_msg("\ncd %3d ", (c)); \
	send_bits(tree[c].Code, tree[c].Len); \
}
#endif

#define D_CODE(dist) \
	((dist) < 256 ? G2.dist_code[dist] : G2.dist_code[256 + ((dist)>>7)])
/* Mapping from a distance to a distance code. dist is the distance - 1 and
 * must not have side effects. dist_code[256] and dist_code[257] are never
 * used.
 * The arguments must not have side effects.
 */

/* ===========================================================================
 * Initialize a new block.
 */
static void init_block(void)
{
	int n; /* iterates over tree elements */

	/* Initialize the trees. */
	for (n = 0; n < L_CODES; n++)
		G2.dyn_ltree[n].Freq = 0;
	for (n = 0; n < D_CODES; n++)
		G2.dyn_dtree[n].Freq = 0;
	for (n = 0; n < BL_CODES; n++)
		G2.bl_tree[n].Freq = 0;

	G2.dyn_ltree[END_BLOCK].Freq = 1;
	G2.opt_len = G2.static_len = 0;
	G2.last_lit = G2.last_dist = G2.last_flags = 0;
	G2.flags = 0;
	G2.flag_bit = 1;
}

/* ===========================================================================
 * Restore the heap property by moving down the tree starting at node k,
 * exchanging a node with the smallest of its two sons if necessary, stopping
 * when the heap property is re-established (each father smaller than its
 * two sons).
 */

/* Compares to subtrees, using the tree depth as tie breaker when
 * the subtrees have equal frequency. This minimizes the worst case length. */
#define SMALLER(tree, n, m) \
	(tree[n].Freq < tree[m].Freq \
	|| (tree[n].Freq == tree[m].Freq && G2.depth[n] <= G2.depth[m]))

static void pqdownheap(ct_data * tree, int k)
{
	int v = G2.heap[k];
	int j = k << 1;		/* left son of k */

	while (j <= G2.heap_len) {
		/* Set j to the smallest of the two sons: */
		if (j < G2.heap_len && SMALLER(tree, G2.heap[j + 1], G2.heap[j]))
			j++;

		/* Exit if v is smaller than both sons */
		if (SMALLER(tree, v, G2.heap[j]))
			break;

		/* Exchange v with the smallest son */
		G2.heap[k] = G2.heap[j];
		k = j;

		/* And continue down the tree, setting j to the left son of k */
		j <<= 1;
	}
	G2.heap[k] = v;
}

/* ===========================================================================
 * Compute the optimal bit lengths for a tree and update the total bit length
 * for the current block.
 * IN assertion: the fields freq and dad are set, heap[heap_max] and
 *    above are the tree nodes sorted by increasing frequency.
 * OUT assertions: the field len is set to the optimal bit length, the
 *     array bl_count contains the frequencies for each bit length.
 *     The length opt_len is updated; static_len is also updated if stree is
 *     not null.
 */
static void gen_bitlen(tree_desc * desc)
{
	ct_data *tree = desc->dyn_tree;
	const uint8_t *extra = desc->extra_bits;
	int base = desc->extra_base;
	int max_code = desc->max_code;
	int max_length = desc->max_length;
	ct_data *stree = desc->static_tree;
	int h;				/* heap index */
	int n, m;			/* iterate over the tree elements */
	int bits;			/* bit length */
	int xbits;			/* extra bits */
	ush f;				/* frequency */
	int overflow = 0;	/* number of elements with bit length too large */

	for (bits = 0; bits <= MAX_BITS; bits++)
		G2.bl_count[bits] = 0;

	/* In a first pass, compute the optimal bit lengths (which may
	 * overflow in the case of the bit length tree).
	 */
	tree[G2.heap[G2.heap_max]].Len = 0;	/* root of the heap */

	for (h = G2.heap_max + 1; h < HEAP_SIZE; h++) {
		n = G2.heap[h];
		bits = tree[tree[n].Dad].Len + 1;
		if (bits > max_length) {
			bits = max_length;
			overflow++;
		}
		tree[n].Len = (ush) bits;
		/* We overwrite tree[n].Dad which is no longer needed */

		if (n > max_code)
			continue;	/* not a leaf node */

		G2.bl_count[bits]++;
		xbits = 0;
		if (n >= base)
			xbits = extra[n - base];
		f = tree[n].Freq;
		G2.opt_len += (ulg) f *(bits + xbits);

		if (stree)
			G2.static_len += (ulg) f * (stree[n].Len + xbits);
	}
	if (overflow == 0)
		return;

	Trace((stderr, "\nbit length overflow\n"));
	/* This happens for example on obj2 and pic of the Calgary corpus */

	/* Find the first bit length which could increase: */
	do {
		bits = max_length - 1;
		while (G2.bl_count[bits] == 0)
			bits--;
		G2.bl_count[bits]--;	/* move one leaf down the tree */
		G2.bl_count[bits + 1] += 2;	/* move one overflow item as its brother */
		G2.bl_count[max_length]--;
		/* The brother of the overflow item also moves one step up,
		 * but this does not affect bl_count[max_length]
		 */
		overflow -= 2;
	} while (overflow > 0);

	/* Now recompute all bit lengths, scanning in increasing frequency.
	 * h is still equal to HEAP_SIZE. (It is simpler to reconstruct all
	 * lengths instead of fixing only the wrong ones. This idea is taken
	 * from 'ar' written by Haruhiko Okumura.)
	 */
	for (bits = max_length; bits != 0; bits--) {
		n = G2.bl_count[bits];
		while (n != 0) {
			m = G2.heap[--h];
			if (m > max_code)
				continue;
			if (tree[m].Len != (unsigned) bits) {
				Trace((stderr, "code %d bits %d->%d\n", m, tree[m].Len, bits));
				G2.opt_len += ((int32_t) bits - tree[m].Len) * tree[m].Freq;
				tree[m].Len = bits;
			}
			n--;
		}
	}
}

/* ===========================================================================
 * Generate the codes for a given tree and bit counts (which need not be
 * optimal).
 * IN assertion: the array bl_count contains the bit length statistics for
 * the given tree and the field len is set for all tree elements.
 * OUT assertion: the field code is set for all tree elements of non
 *     zero code length.
 */
static void gen_codes(ct_data * tree, int max_code)
{
	ush next_code[MAX_BITS + 1];	/* next code value for each bit length */
	ush code = 0;		/* running code value */
	int bits;			/* bit index */
	int n;				/* code index */

	/* The distribution counts are first used to generate the code values
	 * without bit reversal.
	 */
	for (bits = 1; bits <= MAX_BITS; bits++) {
		next_code[bits] = code = (code + G2.bl_count[bits - 1]) << 1;
	}
	/* Check that the bit counts in bl_count are consistent. The last code
	 * must be all ones.
	 */
	Assert(code + G2.bl_count[MAX_BITS] - 1 == (1 << MAX_BITS) - 1,
			"inconsistent bit counts");
	Tracev((stderr, "\ngen_codes: max_code %d ", max_code));

	for (n = 0; n <= max_code; n++) {
		int len = tree[n].Len;

		if (len == 0)
			continue;
		/* Now reverse the bits */
		tree[n].Code = bi_reverse(next_code[len]++, len);

		Tracec(tree != G2.static_ltree,
			   (stderr, "\nn %3d %c l %2d c %4x (%x) ", n,
				(n > ' ' ? n : ' '), len, tree[n].Code,
				next_code[len] - 1));
	}
}

/* ===========================================================================
 * Construct one Huffman tree and assigns the code bit strings and lengths.
 * Update the total bit length for the current block.
 * IN assertion: the field freq is set for all tree elements.
 * OUT assertions: the fields len and code are set to the optimal bit length
 *     and corresponding code. The length opt_len is updated; static_len is
 *     also updated if stree is not null. The field max_code is set.
 */

/* Remove the smallest element from the heap and recreate the heap with
 * one less element. Updates heap and heap_len. */

#define SMALLEST 1
/* Index within the heap array of least frequent node in the Huffman tree */

#define PQREMOVE(tree, top) \
do { \
	top = G2.heap[SMALLEST]; \
	G2.heap[SMALLEST] = G2.heap[G2.heap_len--]; \
	pqdownheap(tree, SMALLEST); \
} while (0)

static void build_tree(tree_desc * desc)
{
	ct_data *tree = desc->dyn_tree;
	ct_data *stree = desc->static_tree;
	int elems = desc->elems;
	int n, m;			/* iterate over heap elements */
	int max_code = -1;	/* largest code with non zero frequency */
	int node = elems;	/* next internal node of the tree */

	/* Construct the initial heap, with least frequent element in
	 * heap[SMALLEST]. The sons of heap[n] are heap[2*n] and heap[2*n+1].
	 * heap[0] is not used.
	 */
	G2.heap_len = 0;
	G2.heap_max = HEAP_SIZE;

	for (n = 0; n < elems; n++) {
		if (tree[n].Freq != 0) {
			G2.heap[++G2.heap_len] = max_code = n;
			G2.depth[n] = 0;
		} else {
			tree[n].Len = 0;
		}
	}

	/* The pkzip format requires that at least one distance code exists,
	 * and that at least one bit should be sent even if there is only one
	 * possible code. So to avoid special checks later on we force at least
	 * two codes of non zero frequency.
	 */
	while (G2.heap_len < 2) {
		int new = G2.heap[++G2.heap_len] = (max_code < 2 ? ++max_code : 0);

		tree[new].Freq = 1;
		G2.depth[new] = 0;
		G2.opt_len--;
		if (stree)
			G2.static_len -= stree[new].Len;
		/* new is 0 or 1 so it does not have extra bits */
	}
	desc->max_code = max_code;

	/* The elements heap[heap_len/2+1 .. heap_len] are leaves of the tree,
	 * establish sub-heaps of increasing lengths:
	 */
	for (n = G2.heap_len / 2; n >= 1; n--)
		pqdownheap(tree, n);

	/* Construct the Huffman tree by repeatedly combining the least two
	 * frequent nodes.
	 */
	do {
		PQREMOVE(tree, n);	/* n = node of least frequency */
		m = G2.heap[SMALLEST];	/* m = node of next least frequency */

		G2.heap[--G2.heap_max] = n;	/* keep the nodes sorted by frequency */
		G2.heap[--G2.heap_max] = m;

		/* Create a new node father of n and m */
		tree[node].Freq = tree[n].Freq + tree[m].Freq;
		G2.depth[node] = MAX(G2.depth[n], G2.depth[m]) + 1;
		tree[n].Dad = tree[m].Dad = (ush) node;
#ifdef DUMP_BL_TREE
		if (tree == G2.bl_tree) {
			bb_error_msg("\nnode %d(%d), sons %d(%d) %d(%d)",
					node, tree[node].Freq, n, tree[n].Freq, m, tree[m].Freq);
		}
#endif
		/* and insert the new node in the heap */
		G2.heap[SMALLEST] = node++;
		pqdownheap(tree, SMALLEST);
	} while (G2.heap_len >= 2);

	G2.heap[--G2.heap_max] = G2.heap[SMALLEST];

	/* At this point, the fields freq and dad are set. We can now
	 * generate the bit lengths.
	 */
	gen_bitlen((tree_desc *) desc);

	/* The field len is now set, we can generate the bit codes */
	gen_codes((ct_data *) tree, max_code);
}

/* ===========================================================================
 * Scan a literal or distance tree to determine the frequencies of the codes
 * in the bit length tree. Updates opt_len to take into account the repeat
 * counts. (The contribution of the bit length codes will be added later
 * during the construction of bl_tree.)
 */
static void scan_tree(ct_data * tree, int max_code)
{
	int n;				/* iterates over all tree elements */
	int prevlen = -1;	/* last emitted length */
	int curlen;			/* length of current code */
	int nextlen = tree[0].Len;	/* length of next code */
	int count = 0;		/* repeat count of the current code */
	int max_count = 7;	/* max repeat count */
	int min_count = 4;	/* min repeat count */

	if (nextlen == 0) {
		max_count = 138;
		min_count = 3;
	}
	tree[max_code + 1].Len = 0xffff; /* guard */

	for (n = 0; n <= max_code; n++) {
		curlen = nextlen;
		nextlen = tree[n + 1].Len;
		if (++count < max_count && curlen == nextlen)
			continue;

		if (count < min_count) {
			G2.bl_tree[curlen].Freq += count;
		} else if (curlen != 0) {
			if (curlen != prevlen)
				G2.bl_tree[curlen].Freq++;
			G2.bl_tree[REP_3_6].Freq++;
		} else if (count <= 10) {
			G2.bl_tree[REPZ_3_10].Freq++;
		} else {
			G2.bl_tree[REPZ_11_138].Freq++;
		}
		count = 0;
		prevlen = curlen;

		max_count = 7;
		min_count = 4;
		if (nextlen == 0) {
			max_count = 138;
			min_count = 3;
		} else if (curlen == nextlen) {
			max_count = 6;
			min_count = 3;
		}
	}
}

/* ===========================================================================
 * Send a literal or distance tree in compressed form, using the codes in
 * bl_tree.
 */
static void send_tree(ct_data * tree, int max_code)
{
	int n;				/* iterates over all tree elements */
	int prevlen = -1;	/* last emitted length */
	int curlen;			/* length of current code */
	int nextlen = tree[0].Len;	/* length of next code */
	int count = 0;		/* repeat count of the current code */
	int max_count = 7;	/* max repeat count */
	int min_count = 4;	/* min repeat count */

/* tree[max_code+1].Len = -1; *//* guard already set */
	if (nextlen == 0)
		max_count = 138, min_count = 3;

	for (n = 0; n <= max_code; n++) {
		curlen = nextlen;
		nextlen = tree[n + 1].Len;
		if (++count < max_count && curlen == nextlen) {
			continue;
		} else if (count < min_count) {
			do {
				SEND_CODE(curlen, G2.bl_tree);
			} while (--count);
		} else if (curlen != 0) {
			if (curlen != prevlen) {
				SEND_CODE(curlen, G2.bl_tree);
				count--;
			}
			Assert(count >= 3 && count <= 6, " 3_6?");
			SEND_CODE(REP_3_6, G2.bl_tree);
			send_bits(count - 3, 2);
		} else if (count <= 10) {
			SEND_CODE(REPZ_3_10, G2.bl_tree);
			send_bits(count - 3, 3);
		} else {
			SEND_CODE(REPZ_11_138, G2.bl_tree);
			send_bits(count - 11, 7);
		}
		count = 0;
		prevlen = curlen;
		if (nextlen == 0) {
			max_count = 138;
			min_count = 3;
		} else if (curlen == nextlen) {
			max_count = 6;
			min_count = 3;
		} else {
			max_count = 7;
			min_count = 4;
		}
	}
}

/* ===========================================================================
 * Construct the Huffman tree for the bit lengths and return the index in
 * bl_order of the last bit length code to send.
 */
static int build_bl_tree(void)
{
	int max_blindex;	/* index of last bit length code of non zero freq */

	/* Determine the bit length frequencies for literal and distance trees */
	scan_tree(G2.dyn_ltree, G2.l_desc.max_code);
	scan_tree(G2.dyn_dtree, G2.d_desc.max_code);

	/* Build the bit length tree: */
	build_tree(&G2.bl_desc);
	/* opt_len now includes the length of the tree representations, except
	 * the lengths of the bit lengths codes and the 5+5+4 bits for the counts.
	 */

	/* Determine the number of bit length codes to send. The pkzip format
	 * requires that at least 4 bit length codes be sent. (appnote.txt says
	 * 3 but the actual value used is 4.)
	 */
	for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--) {
		if (G2.bl_tree[bl_order[max_blindex]].Len != 0)
			break;
	}
	/* Update opt_len to include the bit length tree and counts */
	G2.opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
	Tracev((stderr, "\ndyn trees: dyn %ld, stat %ld", (long)G2.opt_len, (long)G2.static_len));

	return max_blindex;
}

/* ===========================================================================
 * Send the header for a block using dynamic Huffman trees: the counts, the
 * lengths of the bit length codes, the literal tree and the distance tree.
 * IN assertion: lcodes >= 257, dcodes >= 1, blcodes >= 4.
 */
static void send_all_trees(int lcodes, int dcodes, int blcodes)
{
	int rank;			/* index in bl_order */

	Assert(lcodes >= 257 && dcodes >= 1 && blcodes >= 4, "not enough codes");
	Assert(lcodes <= L_CODES && dcodes <= D_CODES
		   && blcodes <= BL_CODES, "too many codes");
	Tracev((stderr, "\nbl counts: "));
	send_bits(lcodes - 257, 5);	/* not +255 as stated in appnote.txt */
	send_bits(dcodes - 1, 5);
	send_bits(blcodes - 4, 4);	/* not -3 as stated in appnote.txt */
	for (rank = 0; rank < blcodes; rank++) {
		Tracev((stderr, "\nbl code %2d ", bl_order[rank]));
		send_bits(G2.bl_tree[bl_order[rank]].Len, 3);
	}
	Tracev((stderr, "\nbl tree: sent %ld", (long)G1.bits_sent));

	send_tree((ct_data *) G2.dyn_ltree, lcodes - 1);	/* send the literal tree */
	Tracev((stderr, "\nlit tree: sent %ld", (long)G1.bits_sent));

	send_tree((ct_data *) G2.dyn_dtree, dcodes - 1);	/* send the distance tree */
	Tracev((stderr, "\ndist tree: sent %ld", (long)G1.bits_sent));
}

/* ===========================================================================
 * Save the match info and tally the frequency counts. Return true if
 * the current block must be flushed.
 */
static int ct_tally(int dist, int lc)
{
	G1.l_buf[G2.last_lit++] = lc;
	if (dist == 0) {
		/* lc is the unmatched char */
		G2.dyn_ltree[lc].Freq++;
	} else {
		/* Here, lc is the match length - MIN_MATCH */
		dist--;			/* dist = match distance - 1 */
		Assert((ush) dist < (ush) MAX_DIST
		 && (ush) lc <= (ush) (MAX_MATCH - MIN_MATCH)
		 && (ush) D_CODE(dist) < (ush) D_CODES, "ct_tally: bad match"
		);

		G2.dyn_ltree[G2.length_code[lc] + LITERALS + 1].Freq++;
		G2.dyn_dtree[D_CODE(dist)].Freq++;

		G1.d_buf[G2.last_dist++] = dist;
		G2.flags |= G2.flag_bit;
	}
	G2.flag_bit <<= 1;

	/* Output the flags if they fill a byte: */
	if ((G2.last_lit & 7) == 0) {
		G2.flag_buf[G2.last_flags++] = G2.flags;
		G2.flags = 0;
		G2.flag_bit = 1;
	}
	/* Try to guess if it is profitable to stop the current block here */
	if ((G2.last_lit & 0xfff) == 0) {
		/* Compute an upper bound for the compressed length */
		ulg out_length = G2.last_lit * 8L;
		ulg in_length = (ulg) G1.strstart - G1.block_start;
		int dcode;

		for (dcode = 0; dcode < D_CODES; dcode++) {
			out_length += G2.dyn_dtree[dcode].Freq * (5L + extra_dbits[dcode]);
		}
		out_length >>= 3;
		Trace((stderr,
				"\nlast_lit %u, last_dist %u, in %ld, out ~%ld(%ld%%) ",
				G2.last_lit, G2.last_dist,
				(long)in_length, (long)out_length,
				100L - out_length * 100L / in_length));
		if (G2.last_dist < G2.last_lit / 2 && out_length < in_length / 2)
			return 1;
	}
	return (G2.last_lit == LIT_BUFSIZE - 1 || G2.last_dist == DIST_BUFSIZE);
	/* We avoid equality with LIT_BUFSIZE because of wraparound at 64K
	 * on 16 bit machines and because stored blocks are restricted to
	 * 64K-1 bytes.
	 */
}

/* ===========================================================================
 * Send the block data compressed using the given Huffman trees
 */
static void compress_block(ct_data * ltree, ct_data * dtree)
{
	unsigned dist;          /* distance of matched string */
	int lc;                 /* match length or unmatched char (if dist == 0) */
	unsigned lx = 0;        /* running index in l_buf */
	unsigned dx = 0;        /* running index in d_buf */
	unsigned fx = 0;        /* running index in flag_buf */
	uch flag = 0;           /* current flags */
	unsigned code;          /* the code to send */
	int extra;              /* number of extra bits to send */

	if (G2.last_lit != 0) do {
		if ((lx & 7) == 0)
			flag = G2.flag_buf[fx++];
		lc = G1.l_buf[lx++];
		if ((flag & 1) == 0) {
			SEND_CODE(lc, ltree);	/* send a literal byte */
			Tracecv(lc > ' ', (stderr, " '%c' ", lc));
		} else {
			/* Here, lc is the match length - MIN_MATCH */
			code = G2.length_code[lc];
			SEND_CODE(code + LITERALS + 1, ltree);	/* send the length code */
			extra = extra_lbits[code];
			if (extra != 0) {
				lc -= G2.base_length[code];
				send_bits(lc, extra);	/* send the extra length bits */
			}
			dist = G1.d_buf[dx++];
			/* Here, dist is the match distance - 1 */
			code = D_CODE(dist);
			Assert(code < D_CODES, "bad d_code");

			SEND_CODE(code, dtree);	/* send the distance code */
			extra = extra_dbits[code];
			if (extra != 0) {
				dist -= G2.base_dist[code];
				send_bits(dist, extra);	/* send the extra distance bits */
			}
		}			/* literal or match pair ? */
		flag >>= 1;
	} while (lx < G2.last_lit);

	SEND_CODE(END_BLOCK, ltree);
}

/* ===========================================================================
 * Determine the best encoding for the current block: dynamic trees, static
 * trees or store, and output the encoded block to the zip file. This function
 * returns the total compressed length for the file so far.
 */
static void flush_block(char *buf, ulg stored_len, int eof)
{
	ulg opt_lenb, static_lenb;      /* opt_len and static_len in bytes */
	int max_blindex;                /* index of last bit length code of non zero freq */

	G2.flag_buf[G2.last_flags] = G2.flags;   /* Save the flags for the last 8 items */

	/* Construct the literal and distance trees */
	build_tree(&G2.l_desc);
	Tracev((stderr, "\nlit data: dyn %ld, stat %ld", (long)G2.opt_len, (long)G2.static_len));

	build_tree(&G2.d_desc);
	Tracev((stderr, "\ndist data: dyn %ld, stat %ld", (long)G2.opt_len, (long)G2.static_len));
	/* At this point, opt_len and static_len are the total bit lengths of
	 * the compressed block data, excluding the tree representations.
	 */

	/* Build the bit length tree for the above two trees, and get the index
	 * in bl_order of the last bit length code to send.
	 */
	max_blindex = build_bl_tree();

	/* Determine the best encoding. Compute first the block length in bytes */
	opt_lenb = (G2.opt_len + 3 + 7) >> 3;
	static_lenb = (G2.static_len + 3 + 7) >> 3;

	Trace((stderr,
			"\nopt %lu(%lu) stat %lu(%lu) stored %lu lit %u dist %u ",
			(unsigned long)opt_lenb, (unsigned long)G2.opt_len,
			(unsigned long)static_lenb, (unsigned long)G2.static_len,
			(unsigned long)stored_len,
			G2.last_lit, G2.last_dist));

	if (static_lenb <= opt_lenb)
		opt_lenb = static_lenb;

	/* If compression failed and this is the first and last block,
	 * and if the zip file can be seeked (to rewrite the local header),
	 * the whole file is transformed into a stored file:
	 */
// seekable() is constant FALSE in busybox, and G2.compressed_len is disabled
// (this was the only user)
//	if (stored_len <= opt_lenb && eof && G2.compressed_len == 0L && seekable()) {
//		/* Since LIT_BUFSIZE <= 2*WSIZE, the input data must be there: */
//		if (buf == NULL)
//			bb_error_msg("block vanished");
//
//		G2.compressed_len = stored_len << 3;
//		copy_block(buf, (unsigned) stored_len, 0);	/* without header */
//	} else
	if (stored_len + 4 <= opt_lenb && buf != NULL) {
		/* 4: two words for the lengths */
		/* The test buf != NULL is only necessary if LIT_BUFSIZE > WSIZE.
		 * Otherwise we can't have processed more than WSIZE input bytes since
		 * the last block flush, because compression would have been
		 * successful. If LIT_BUFSIZE <= WSIZE, it is never too late to
		 * transform a block into a stored block.
		 */
		send_bits((STORED_BLOCK << 1) + eof, 3);	/* send block type */
//		G2.compressed_len = ((G2.compressed_len + 3 + 7) & ~7L)
//				+ ((stored_len + 4) << 3);
		copy_block(buf, (unsigned) stored_len, 1);	/* with header */
	} else
	if (static_lenb == opt_lenb) {
		send_bits((STATIC_TREES << 1) + eof, 3);
		compress_block((ct_data *) G2.static_ltree, (ct_data *) G2.static_dtree);
//		G2.compressed_len += 3 + G2.static_len;
	} else {
		send_bits((DYN_TREES << 1) + eof, 3);
		send_all_trees(G2.l_desc.max_code + 1, G2.d_desc.max_code + 1,
					max_blindex + 1);
		compress_block((ct_data *) G2.dyn_ltree, (ct_data *) G2.dyn_dtree);
//		G2.compressed_len += 3 + G2.opt_len;
	}
//	Assert(G2.compressed_len == G1.bits_sent, "bad compressed size");
	init_block();

	if (eof) {
		bi_windup();
//		G2.compressed_len += 7;	/* align on byte boundary */
	}
//	Tracev((stderr, "\ncomprlen %lu(%lu) ",
//			(unsigned long)G2.compressed_len >> 3,
//			(unsigned long)G2.compressed_len - 7 * eof));

	return; /* was "return G2.compressed_len >> 3;" */
}

/* ===========================================================================
 * Update a hash value with the given input byte
 * IN  assertion: all calls to UPDATE_HASH are made with consecutive
 *    input characters, so that a running hash key can be computed from the
 *    previous key instead of complete recalculation each time.
 */
#define UPDATE_HASH(h, c) (h = (((h)<<H_SHIFT) ^ (c)) & HASH_MASK)

/* ===========================================================================
 * Same as above, but achieves better compression. We use a lazy
 * evaluation for matches: a match is finally adopted only if there is
 * no better match at the next window position.
 *
 * Processes a new input file and return its compressed length. Sets
 * the compressed length, crc, deflate flags and internal file
 * attributes.
 */

/* Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match. */
#define FLUSH_BLOCK(eof) \
	flush_block( \
		G1.block_start >= 0L \
			? (char*)&G1.window[(unsigned)G1.block_start] \
			: (char*)NULL, \
		(ulg)G1.strstart - G1.block_start, \
		(eof) \
	)

/* Insert string s in the dictionary and set match_head to the previous head
 * of the hash chain (the most recent string with same hash key). Return
 * the previous length of the hash chain.
 * IN  assertion: all calls to INSERT_STRING are made with consecutive
 *    input characters and the first MIN_MATCH bytes of s are valid
 *    (except for the last MIN_MATCH-1 bytes of the input file). */
#define INSERT_STRING(s, match_head) \
do { \
	UPDATE_HASH(G1.ins_h, G1.window[(s) + MIN_MATCH-1]); \
	G1.prev[(s) & WMASK] = match_head = head[G1.ins_h]; \
	head[G1.ins_h] = (s); \
} while (0)

static NOINLINE void deflate(void)
{
	IPos hash_head;		/* head of hash chain */
	IPos prev_match;	/* previous match */
	int flush;			/* set if current block must be flushed */
	int match_available = 0;	/* set if previous match exists */
	unsigned match_length = MIN_MATCH - 1;	/* length of best match */

	/* Process the input block. */
	while (G1.lookahead != 0) {
		/* Insert the string window[strstart .. strstart+2] in the
		 * dictionary, and set hash_head to the head of the hash chain:
		 */
		INSERT_STRING(G1.strstart, hash_head);

		/* Find the longest match, discarding those <= prev_length.
		 */
		G1.prev_length = match_length;
		prev_match = G1.match_start;
		match_length = MIN_MATCH - 1;

		if (hash_head != 0 && G1.prev_length < max_lazy_match
		 && G1.strstart - hash_head <= MAX_DIST
		) {
			/* To simplify the code, we prevent matches with the string
			 * of window index 0 (in particular we have to avoid a match
			 * of the string with itself at the start of the input file).
			 */
			match_length = longest_match(hash_head);
			/* longest_match() sets match_start */
			if (match_length > G1.lookahead)
				match_length = G1.lookahead;

			/* Ignore a length 3 match if it is too distant: */
			if (match_length == MIN_MATCH && G1.strstart - G1.match_start > TOO_FAR) {
				/* If prev_match is also MIN_MATCH, G1.match_start is garbage
				 * but we will ignore the current match anyway.
				 */
				match_length--;
			}
		}
		/* If there was a match at the previous step and the current
		 * match is not better, output the previous match:
		 */
		if (G1.prev_length >= MIN_MATCH && match_length <= G1.prev_length) {
			check_match(G1.strstart - 1, prev_match, G1.prev_length);
			flush = ct_tally(G1.strstart - 1 - prev_match, G1.prev_length - MIN_MATCH);

			/* Insert in hash table all strings up to the end of the match.
			 * strstart-1 and strstart are already inserted.
			 */
			G1.lookahead -= G1.prev_length - 1;
			G1.prev_length -= 2;
			do {
				G1.strstart++;
				INSERT_STRING(G1.strstart, hash_head);
				/* strstart never exceeds WSIZE-MAX_MATCH, so there are
				 * always MIN_MATCH bytes ahead. If lookahead < MIN_MATCH
				 * these bytes are garbage, but it does not matter since the
				 * next lookahead bytes will always be emitted as literals.
				 */
			} while (--G1.prev_length != 0);
			match_available = 0;
			match_length = MIN_MATCH - 1;
			G1.strstart++;
			if (flush) {
				FLUSH_BLOCK(0);
				G1.block_start = G1.strstart;
			}
		} else if (match_available) {
			/* If there was no match at the previous position, output a
			 * single literal. If there was a match but the current match
			 * is longer, truncate the previous match to a single literal.
			 */
			Tracevv((stderr, "%c", G1.window[G1.strstart - 1]));
			if (ct_tally(0, G1.window[G1.strstart - 1])) {
				FLUSH_BLOCK(0);
				G1.block_start = G1.strstart;
			}
			G1.strstart++;
			G1.lookahead--;
		} else {
			/* There is no previous match to compare with, wait for
			 * the next step to decide.
			 */
			match_available = 1;
			G1.strstart++;
			G1.lookahead--;
		}
		Assert(G1.strstart <= G1.isize && G1.lookahead <= G1.isize, "a bit too far");

		/* Make sure that we always have enough lookahead, except
		 * at the end of the input file. We need MAX_MATCH bytes
		 * for the next match, plus MIN_MATCH bytes to insert the
		 * string following the next match.
		 */
		fill_window_if_needed();
	}
	if (match_available)
		ct_tally(0, G1.window[G1.strstart - 1]);

	FLUSH_BLOCK(1);	/* eof */
}

/* ===========================================================================
 * Initialize the bit string routines.
 */
static void bi_init(void)
{
	//G1.bi_buf = 0; // globals are zeroed in pack_gzip()
	//G1.bi_valid = 0; // globals are zeroed in pack_gzip()
	//DEBUG_bits_sent(= 0L); // globals are zeroed in pack_gzip()
}

/* ===========================================================================
 * Initialize the "longest match" routines for a new file
 */
static void lm_init(unsigned *flags16p)
{
	unsigned j;

	/* Initialize the hash table. */
	memset(head, 0, HASH_SIZE * sizeof(*head));
	/* prev will be initialized on the fly */

	/* speed options for the general purpose bit flag */
	*flags16p |= 2;	/* FAST 4, SLOW 2 */
	/* ??? reduce max_chain_length for binary files */

	//G1.strstart = 0; // globals are zeroed in pack_gzip()
	//G1.block_start = 0L; // globals are zeroed in pack_gzip()

	G1.lookahead = file_read(G1.window,
			sizeof(int) <= 2 ? (unsigned) WSIZE : 2 * WSIZE);

	if (G1.lookahead == 0 || G1.lookahead == (unsigned) -1) {
		G1.eofile = 1;
		G1.lookahead = 0;
		return;
	}
	//G1.eofile = 0; // globals are zeroed in pack_gzip()

	/* Make sure that we always have enough lookahead. This is important
	 * if input comes from a device such as a tty.
	 */
	fill_window_if_needed();

	//G1.ins_h = 0; // globals are zeroed in pack_gzip()
	for (j = 0; j < MIN_MATCH - 1; j++)
		UPDATE_HASH(G1.ins_h, G1.window[j]);
	/* If lookahead < MIN_MATCH, ins_h is garbage, but this is
	 * not important since only literal bytes will be emitted.
	 */
}

/* ===========================================================================
 * Allocate the match buffer, initialize the various tables and save the
 * location of the internal file attribute (ascii/binary) and method
 * (DEFLATE/STORE).
 * One callsite in zip()
 */
static void ct_init(void)
{
	int n;				/* iterates over tree elements */
	int length;			/* length value */
	int code;			/* code value */
	int dist;			/* distance index */

//	//G2.compressed_len = 0L; // globals are zeroed in pack_gzip()

#ifdef NOT_NEEDED
	if (G2.static_dtree[0].Len != 0)
		return;			/* ct_init already called */
#endif

	/* Initialize the mapping length (0..255) -> length code (0..28) */
	length = 0;
	for (code = 0; code < LENGTH_CODES - 1; code++) {
		G2.base_length[code] = length;
		for (n = 0; n < (1 << extra_lbits[code]); n++) {
			G2.length_code[length++] = code;
		}
	}
	Assert(length == 256, "ct_init: length != 256");
	/* Note that the length 255 (match length 258) can be represented
	 * in two different ways: code 284 + 5 bits or code 285, so we
	 * overwrite length_code[255] to use the best encoding:
	 */
	G2.length_code[length - 1] = code;

	/* Initialize the mapping dist (0..32K) -> dist code (0..29) */
	dist = 0;
	for (code = 0; code < 16; code++) {
		G2.base_dist[code] = dist;
		for (n = 0; n < (1 << extra_dbits[code]); n++) {
			G2.dist_code[dist++] = code;
		}
	}
	Assert(dist == 256, "ct_init: dist != 256");
	dist >>= 7;			/* from now on, all distances are divided by 128 */
	for (; code < D_CODES; code++) {
		G2.base_dist[code] = dist << 7;
		for (n = 0; n < (1 << (extra_dbits[code] - 7)); n++) {
			G2.dist_code[256 + dist++] = code;
		}
	}
	Assert(dist == 256, "ct_init: 256+dist != 512");

	/* Construct the codes of the static literal tree */
	//for (n = 0; n <= MAX_BITS; n++) // globals are zeroed in pack_gzip()
	//	G2.bl_count[n] = 0;

	n = 0;
	while (n <= 143) {
		G2.static_ltree[n++].Len = 8;
		//G2.bl_count[8]++;
	}
	//G2.bl_count[8] = 143 + 1;
	while (n <= 255) {
		G2.static_ltree[n++].Len = 9;
		//G2.bl_count[9]++;
	}
	//G2.bl_count[9] = 255 - 143;
	while (n <= 279) {
		G2.static_ltree[n++].Len = 7;
		//G2.bl_count[7]++;
	}
	//G2.bl_count[7] = 279 - 255;
	while (n <= 287) {
		G2.static_ltree[n++].Len = 8;
		//G2.bl_count[8]++;
	}
	//G2.bl_count[8] += 287 - 279;
	G2.bl_count[7] = 279 - 255;
	G2.bl_count[8] = (143 + 1) + (287 - 279);
	G2.bl_count[9] = 255 - 143;
	/* Codes 286 and 287 do not exist, but we must include them in the
	 * tree construction to get a canonical Huffman tree (longest code
	 * all ones)
	 */
	gen_codes((ct_data *) G2.static_ltree, L_CODES + 1);

	/* The static distance tree is trivial: */
	for (n = 0; n < D_CODES; n++) {
		G2.static_dtree[n].Len = 5;
		G2.static_dtree[n].Code = bi_reverse(n, 5);
	}

	/* Initialize the first block of the first file: */
	init_block();
}

/* ===========================================================================
 * Deflate in to out.
 * IN assertions: the input and output buffers are cleared.
 */
static void zip(void)
{
	unsigned deflate_flags;

	//G1.outcnt = 0; // globals are zeroed in pack_gzip()

	/* Write the header to the gzip file. See algorithm.doc for the format */
	/* magic header for gzip files: 1F 8B */
	/* compression method: 8 (DEFLATED) */
	/* general flags: 0 */
	put_32bit(0x00088b1f);
	put_32bit(0);		/* Unix timestamp */

	/* Write deflated file to zip file */
	G1.crc = ~0;

	bi_init();
	ct_init();
	deflate_flags = 0;  /* pkzip -es, -en or -ex equivalent */
	lm_init(&deflate_flags);

	put_16bit(deflate_flags | 0x300); /* extra flags. OS id = 3 (Unix) */

	/* The above 32-bit misaligns outbuf (10 bytes are stored), flush it */
	flush_outbuf_if_32bit_optimized();

	deflate();

	/* Write the crc and uncompressed size */
	put_32bit(~G1.crc);
	put_32bit(G1.isize);

	flush_outbuf();
}

/* ======================================================================== */
static
IF_DESKTOP(long long) int FAST_FUNC pack_gzip(transformer_state_t *xstate UNUSED_PARAM)
{
	/* Reinit G1.xxx except pointers to allocated buffers, and entire G2 */
	memset(&G1.crc, 0, (sizeof(G1) - offsetof(struct globals, crc)) + sizeof(G2));

	/* Clear input and output buffers */
	//G1.outcnt = 0;
#ifdef DEBUG
	//G1.insize = 0;
#endif
	//G1.isize = 0;

	/* Reinit G2.xxx */
	G2.l_desc.dyn_tree     = G2.dyn_ltree;
	G2.l_desc.static_tree  = G2.static_ltree;
	G2.l_desc.extra_bits   = extra_lbits;
	G2.l_desc.extra_base   = LITERALS + 1;
	G2.l_desc.elems        = L_CODES;
	G2.l_desc.max_length   = MAX_BITS;
	//G2.l_desc.max_code     = 0;
	G2.d_desc.dyn_tree     = G2.dyn_dtree;
	G2.d_desc.static_tree  = G2.static_dtree;
	G2.d_desc.extra_bits   = extra_dbits;
	//G2.d_desc.extra_base   = 0;
	G2.d_desc.elems        = D_CODES;
	G2.d_desc.max_length   = MAX_BITS;
	//G2.d_desc.max_code     = 0;
	G2.bl_desc.dyn_tree    = G2.bl_tree;
	//G2.bl_desc.static_tree = NULL;
	G2.bl_desc.extra_bits  = extra_blbits,
	//G2.bl_desc.extra_base  = 0;
	G2.bl_desc.elems       = BL_CODES;
	G2.bl_desc.max_length  = MAX_BL_BITS;
	//G2.bl_desc.max_code    = 0;

#if 0
	/* Saving of timestamp is disabled. Why?
	 * - it is not Y2038-safe.
	 * - some people want deterministic results
	 *   (normally they'd use -n, but our -n is a nop).
	 * - it's bloat.
	 * Per RFC 1952, gzfile.time=0 is "no timestamp".
	 * If users will demand this to be reinstated,
	 * implement -n "don't save timestamp".
	 */
	struct stat s;
	s.st_ctime = 0;
	fstat(STDIN_FILENO, &s);
	zip(s.st_ctime);
#else
	zip();
#endif
	return 0;
}

#if ENABLE_FEATURE_GZIP_LONG_OPTIONS
static const char gzip_longopts[] ALIGN1 =
	"stdout\0"              No_argument       "c"
	"to-stdout\0"           No_argument       "c"
	"force\0"               No_argument       "f"
	"verbose\0"             No_argument       "v"
#if ENABLE_FEATURE_GZIP_DECOMPRESS
	"decompress\0"          No_argument       "d"
	"uncompress\0"          No_argument       "d"
	"test\0"                No_argument       "t"
#endif
	"quiet\0"               No_argument       "q"
	"fast\0"                No_argument       "1"
	"best\0"                No_argument       "9"
	"no-name\0"             No_argument       "n"
	;
#endif

/*
 * Linux kernel build uses gzip -d -n. We accept and ignore -n.
 * Man page says:
 * -n --no-name
 * gzip: do not save the original file name and time stamp.
 * (The original name is always saved if the name had to be truncated.)
 * gunzip: do not restore the original file name/time even if present
 * (remove only the gzip suffix from the compressed file name).
 * This option is the default when decompressing.
 * -N --name
 * gzip: always save the original file name and time stamp (this is the default)
 * gunzip: restore the original file name and time stamp if present.
 */

int gzip_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
#if ENABLE_FEATURE_GZIP_DECOMPRESS
int gzip_main(int argc, char **argv)
#else
int gzip_main(int argc UNUSED_PARAM, char **argv)
#endif
{
	unsigned opt;
#if ENABLE_FEATURE_GZIP_LEVELS
	static const struct {
		uint8_t good;
		uint8_t chain_shift;
		uint8_t lazy2;
		uint8_t nice2;
	} gzip_level_config[6] = {
		{4,   4,   4/2,  16/2}, /* Level 4 */
		{8,   5,  16/2,  32/2}, /* Level 5 */
		{8,   7,  16/2, 128/2}, /* Level 6 */
		{8,   8,  32/2, 128/2}, /* Level 7 */
		{32, 10, 128/2, 258/2}, /* Level 8 */
		{32, 12, 258/2, 258/2}, /* Level 9 */
	};
#endif

	SET_PTR_TO_GLOBALS((char *)xzalloc(sizeof(struct globals)+sizeof(struct globals2))
			+ sizeof(struct globals));

	/* Must match bbunzip's constants OPT_STDOUT, OPT_FORCE! */
#if ENABLE_FEATURE_GZIP_LONG_OPTIONS
	opt = getopt32long(argv, BBUNPK_OPTSTR IF_FEATURE_GZIP_DECOMPRESS("dt") "n123456789", gzip_longopts);
#else
	opt = getopt32(argv, BBUNPK_OPTSTR IF_FEATURE_GZIP_DECOMPRESS("dt") "n123456789");
#endif
#if ENABLE_FEATURE_GZIP_DECOMPRESS /* gunzip_main may not be visible... */
	if (opt & (BBUNPK_OPT_DECOMPRESS|BBUNPK_OPT_TEST)) /* -d and/or -t */
		return gunzip_main(argc, argv);
#endif
#if ENABLE_FEATURE_GZIP_LEVELS
	opt >>= (BBUNPK_OPTSTRLEN IF_FEATURE_GZIP_DECOMPRESS(+ 2) + 1); /* drop cfkvq[dt]n bits */
	if (opt == 0)
		opt = 1 << 6; /* default: 6 */
	opt = ffs(opt >> 4); /* Maps -1..-4 to [0], -5 to [1] ... -9 to [5] */
	max_chain_length = 1 << gzip_level_config[opt].chain_shift;
	good_match	 = gzip_level_config[opt].good;
	max_lazy_match	 = gzip_level_config[opt].lazy2 * 2;
	nice_match	 = gzip_level_config[opt].nice2 * 2;
#endif
	option_mask32 &= BBUNPK_OPTSTRMASK; /* retain only -cfkvq */

	/* Allocate all global buffers (for DYN_ALLOC option) */
	ALLOC(uch, G1.l_buf, INBUFSIZ);
	ALLOC(uch, G1.outbuf, OUTBUFSIZ);
	ALLOC(ush, G1.d_buf, DIST_BUFSIZE);
	ALLOC(uch, G1.window, 2L * WSIZE);
	ALLOC(ush, G1.prev, 1L << BITS);

	/* Initialize the CRC32 table */
	global_crc32_new_table_le();

	argv += optind;
	return bbunpack(argv, pack_gzip, append_ext, "gz");
}
