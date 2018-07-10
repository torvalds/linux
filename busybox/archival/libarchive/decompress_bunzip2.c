/* vi: set sw=4 ts=4: */
/*
 * Small bzip2 deflate implementation, by Rob Landley (rob@landley.net).
 *
 * Based on bzip2 decompression code by Julian R Seward (jseward@acm.org),
 * which also acknowledges contributions by Mike Burrows, David Wheeler,
 * Peter Fenwick, Alistair Moffat, Radford Neal, Ian H. Witten,
 * Robert Sedgewick, and Jon L. Bentley.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
	Size and speed optimizations by Manuel Novoa III  (mjn3@codepoet.org).

	More efficient reading of Huffman codes, a streamlined read_bunzip()
	function, and various other tweaks.  In (limited) tests, approximately
	20% faster than bzcat on x86 and about 10% faster on arm.

	Note that about 2/3 of the time is spent in read_bunzip() reversing
	the Burrows-Wheeler transformation.  Much of that time is delay
	resulting from cache misses.

	(2010 update by vda: profiled "bzcat <84mbyte.bz2 >/dev/null"
	on x86-64 CPU with L2 > 1M: get_next_block is hotter than read_bunzip:
	%time seconds   calls function
	71.01   12.69     444 get_next_block
	28.65    5.12   93065 read_bunzip
	00.22    0.04 7736490 get_bits
	00.11    0.02      47 dealloc_bunzip
	00.00    0.00   93018 full_write
	...)


	I would ask that anyone benefiting from this work, especially those
	using it in commercial products, consider making a donation to my local
	non-profit hospice organization (www.hospiceacadiana.com) in the name of
	the woman I loved, Toni W. Hagan, who passed away Feb. 12, 2003.

	Manuel
 */
#include "libbb.h"
#include "bb_archive.h"

#if 0
# define dbg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

/* Constants for Huffman coding */
#define MAX_GROUPS          6
#define GROUP_SIZE          50      /* 64 would have been more efficient */
#define MAX_HUFCODE_BITS    20      /* Longest Huffman code allowed */
#define MAX_SYMBOLS         258     /* 256 literals + RUNA + RUNB */
#define SYMBOL_RUNA         0
#define SYMBOL_RUNB         1

/* Status return values */
#define RETVAL_OK                       0
#define RETVAL_LAST_BLOCK               (dbg("%d", __LINE__), -1)
#define RETVAL_NOT_BZIP_DATA            (dbg("%d", __LINE__), -2)
#define RETVAL_UNEXPECTED_INPUT_EOF     (dbg("%d", __LINE__), -3)
#define RETVAL_SHORT_WRITE              (dbg("%d", __LINE__), -4)
#define RETVAL_DATA_ERROR               (dbg("%d", __LINE__), -5)
#define RETVAL_OUT_OF_MEMORY            (dbg("%d", __LINE__), -6)
#define RETVAL_OBSOLETE_INPUT           (dbg("%d", __LINE__), -7)

/* Other housekeeping constants */
#define IOBUF_SIZE          4096

/* This is what we know about each Huffman coding group */
struct group_data {
	/* We have an extra slot at the end of limit[] for a sentinel value. */
	int limit[MAX_HUFCODE_BITS+1], base[MAX_HUFCODE_BITS], permute[MAX_SYMBOLS];
	int minLen, maxLen;
};

/* Structure holding all the housekeeping data, including IO buffers and
 * memory that persists between calls to bunzip
 * Found the most used member:
 *  cat this_file.c | sed -e 's/"/ /g' -e "s/'/ /g" | xargs -n1 \
 *  | grep 'bd->' | sed 's/^.*bd->/bd->/' | sort | $PAGER
 * and moved it (inbufBitCount) to offset 0.
 */
struct bunzip_data {
	/* I/O tracking data (file handles, buffers, positions, etc.) */
	unsigned inbufBitCount, inbufBits;
	int in_fd, out_fd, inbufCount, inbufPos /*, outbufPos*/;
	uint8_t *inbuf /*,*outbuf*/;

	/* State for interrupting output loop */
	int writeCopies, writePos, writeRunCountdown, writeCount;
	int writeCurrent; /* actually a uint8_t */

	/* The CRC values stored in the block header and calculated from the data */
	uint32_t headerCRC, totalCRC, writeCRC;

	/* Intermediate buffer and its size (in bytes) */
	uint32_t *dbuf;
	unsigned dbufSize;

	/* For I/O error handling */
	jmp_buf *jmpbuf;

	/* Big things go last (register-relative addressing can be larger for big offsets) */
	uint32_t crc32Table[256];
	uint8_t selectors[32768];  /* nSelectors=15 bits */
	struct group_data groups[MAX_GROUPS];  /* Huffman coding tables */
};
/* typedef struct bunzip_data bunzip_data; -- done in .h file */


/* Return the next nnn bits of input.  All reads from the compressed input
   are done through this function.  All reads are big endian */
static unsigned get_bits(bunzip_data *bd, int bits_wanted)
{
	unsigned bits = 0;
	/* Cache bd->inbufBitCount in a CPU register (hopefully): */
	int bit_count = bd->inbufBitCount;

	/* If we need to get more data from the byte buffer, do so.  (Loop getting
	   one byte at a time to enforce endianness and avoid unaligned access.) */
	while (bit_count < bits_wanted) {

		/* If we need to read more data from file into byte buffer, do so */
		if (bd->inbufPos == bd->inbufCount) {
			/* if "no input fd" case: in_fd == -1, read fails, we jump */
			bd->inbufCount = read(bd->in_fd, bd->inbuf, IOBUF_SIZE);
			if (bd->inbufCount <= 0)
				longjmp(*bd->jmpbuf, RETVAL_UNEXPECTED_INPUT_EOF);
			bd->inbufPos = 0;
		}

		/* Avoid 32-bit overflow (dump bit buffer to top of output) */
		if (bit_count >= 24) {
			bits = bd->inbufBits & ((1U << bit_count) - 1);
			bits_wanted -= bit_count;
			bits <<= bits_wanted;
			bit_count = 0;
		}

		/* Grab next 8 bits of input from buffer. */
		bd->inbufBits = (bd->inbufBits << 8) | bd->inbuf[bd->inbufPos++];
		bit_count += 8;
	}

	/* Calculate result */
	bit_count -= bits_wanted;
	bd->inbufBitCount = bit_count;
	bits |= (bd->inbufBits >> bit_count) & ((1 << bits_wanted) - 1);

	return bits;
}
//#define get_bits(bd, n) (dbg("%d:get_bits()", __LINE__), get_bits(bd, n))

/* Unpacks the next block and sets up for the inverse Burrows-Wheeler step. */
static int get_next_block(bunzip_data *bd)
{
	int groupCount, selector,
		i, j, symCount, symTotal, nSelectors, byteCount[256];
	uint8_t uc, symToByte[256], mtfSymbol[256], *selectors;
	uint32_t *dbuf;
	unsigned origPtr, t;
	unsigned dbufCount, runPos;
	unsigned runCnt = runCnt; /* for compiler */

	dbuf = bd->dbuf;
	selectors = bd->selectors;

/* In bbox, we are ok with aborting through setjmp which is set up in start_bunzip */
#if 0
	/* Reset longjmp I/O error handling */
	i = setjmp(bd->jmpbuf);
	if (i) return i;
#endif

	/* Read in header signature and CRC, then validate signature.
	   (last block signature means CRC is for whole file, return now) */
	i = get_bits(bd, 24);
	j = get_bits(bd, 24);
	bd->headerCRC = get_bits(bd, 32);
	if ((i == 0x177245) && (j == 0x385090))
		return RETVAL_LAST_BLOCK;
	if ((i != 0x314159) || (j != 0x265359))
		return RETVAL_NOT_BZIP_DATA;

	/* We can add support for blockRandomised if anybody complains.  There was
	   some code for this in busybox 1.0.0-pre3, but nobody ever noticed that
	   it didn't actually work. */
	if (get_bits(bd, 1))
		return RETVAL_OBSOLETE_INPUT;
	origPtr = get_bits(bd, 24);
	if (origPtr > bd->dbufSize)
		return RETVAL_DATA_ERROR;

	/* mapping table: if some byte values are never used (encoding things
	   like ascii text), the compression code removes the gaps to have fewer
	   symbols to deal with, and writes a sparse bitfield indicating which
	   values were present.  We make a translation table to convert the symbols
	   back to the corresponding bytes. */
	symTotal = 0;
	i = 0;
	t = get_bits(bd, 16);
	do {
		if (t & (1 << 15)) {
			unsigned inner_map = get_bits(bd, 16);
			do {
				if (inner_map & (1 << 15))
					symToByte[symTotal++] = i;
				inner_map <<= 1;
				i++;
			} while (i & 15);
			i -= 16;
		}
		t <<= 1;
		i += 16;
	} while (i < 256);

	/* How many different Huffman coding groups does this block use? */
	groupCount = get_bits(bd, 3);
	if (groupCount < 2 || groupCount > MAX_GROUPS)
		return RETVAL_DATA_ERROR;

	/* nSelectors: Every GROUP_SIZE many symbols we select a new Huffman coding
	   group.  Read in the group selector list, which is stored as MTF encoded
	   bit runs.  (MTF=Move To Front, as each value is used it's moved to the
	   start of the list.) */
	for (i = 0; i < groupCount; i++)
		mtfSymbol[i] = i;
	nSelectors = get_bits(bd, 15);
	if (!nSelectors)
		return RETVAL_DATA_ERROR;
	for (i = 0; i < nSelectors; i++) {
		uint8_t tmp_byte;
		/* Get next value */
		int n = 0;
		while (get_bits(bd, 1)) {
			if (n >= groupCount)
				return RETVAL_DATA_ERROR;
			n++;
		}
		/* Decode MTF to get the next selector */
		tmp_byte = mtfSymbol[n];
		while (--n >= 0)
			mtfSymbol[n + 1] = mtfSymbol[n];
//We catch it later, in the second loop where we use selectors[i].
//Maybe this is a better place, though?
//		if (tmp_byte >= groupCount) {
//			dbg("%d: selectors[%d]:%d groupCount:%d",
//					__LINE__, i, tmp_byte, groupCount);
//			return RETVAL_DATA_ERROR;
//		}
		mtfSymbol[0] = selectors[i] = tmp_byte;
	}

	/* Read the Huffman coding tables for each group, which code for symTotal
	   literal symbols, plus two run symbols (RUNA, RUNB) */
	symCount = symTotal + 2;
	for (j = 0; j < groupCount; j++) {
		uint8_t length[MAX_SYMBOLS];
		/* 8 bits is ALMOST enough for temp[], see below */
		unsigned temp[MAX_HUFCODE_BITS+1];
		struct group_data *hufGroup;
		int *base, *limit;
		int minLen, maxLen, pp, len_m1;

		/* Read Huffman code lengths for each symbol.  They're stored in
		   a way similar to mtf; record a starting value for the first symbol,
		   and an offset from the previous value for every symbol after that.
		   (Subtracting 1 before the loop and then adding it back at the end is
		   an optimization that makes the test inside the loop simpler: symbol
		   length 0 becomes negative, so an unsigned inequality catches it.) */
		len_m1 = get_bits(bd, 5) - 1;
		for (i = 0; i < symCount; i++) {
			for (;;) {
				int two_bits;
				if ((unsigned)len_m1 > (MAX_HUFCODE_BITS-1))
					return RETVAL_DATA_ERROR;

				/* If first bit is 0, stop.  Else second bit indicates whether
				   to increment or decrement the value.  Optimization: grab 2
				   bits and unget the second if the first was 0. */
				two_bits = get_bits(bd, 2);
				if (two_bits < 2) {
					bd->inbufBitCount++;
					break;
				}

				/* Add one if second bit 1, else subtract 1.  Avoids if/else */
				len_m1 += (((two_bits+1) & 2) - 1);
			}

			/* Correct for the initial -1, to get the final symbol length */
			length[i] = len_m1 + 1;
		}

		/* Find largest and smallest lengths in this group */
		minLen = maxLen = length[0];
		for (i = 1; i < symCount; i++) {
			if (length[i] > maxLen)
				maxLen = length[i];
			else if (length[i] < minLen)
				minLen = length[i];
		}

		/* Calculate permute[], base[], and limit[] tables from length[].
		 *
		 * permute[] is the lookup table for converting Huffman coded symbols
		 * into decoded symbols.  base[] is the amount to subtract from the
		 * value of a Huffman symbol of a given length when using permute[].
		 *
		 * limit[] indicates the largest numerical value a symbol with a given
		 * number of bits can have.  This is how the Huffman codes can vary in
		 * length: each code with a value>limit[length] needs another bit.
		 */
		hufGroup = bd->groups + j;
		hufGroup->minLen = minLen;
		hufGroup->maxLen = maxLen;

		/* Note that minLen can't be smaller than 1, so we adjust the base
		   and limit array pointers so we're not always wasting the first
		   entry.  We do this again when using them (during symbol decoding). */
		base = hufGroup->base - 1;
		limit = hufGroup->limit - 1;

		/* Calculate permute[].  Concurrently, initialize temp[] and limit[]. */
		pp = 0;
		for (i = minLen; i <= maxLen; i++) {
			int k;
			temp[i] = limit[i] = 0;
			for (k = 0; k < symCount; k++)
				if (length[k] == i)
					hufGroup->permute[pp++] = k;
		}

		/* Count symbols coded for at each bit length */
		/* NB: in pathological cases, temp[8] can end ip being 256.
		 * That's why uint8_t is too small for temp[]. */
		for (i = 0; i < symCount; i++)
			temp[length[i]]++;

		/* Calculate limit[] (the largest symbol-coding value at each bit
		 * length, which is (previous limit<<1)+symbols at this level), and
		 * base[] (number of symbols to ignore at each bit length, which is
		 * limit minus the cumulative count of symbols coded for already). */
		pp = t = 0;
		for (i = minLen; i < maxLen;) {
			unsigned temp_i = temp[i];

			pp += temp_i;

			/* We read the largest possible symbol size and then unget bits
			   after determining how many we need, and those extra bits could
			   be set to anything.  (They're noise from future symbols.)  At
			   each level we're really only interested in the first few bits,
			   so here we set all the trailing to-be-ignored bits to 1 so they
			   don't affect the value>limit[length] comparison. */
			limit[i] = (pp << (maxLen - i)) - 1;
			pp <<= 1;
			t += temp_i;
			base[++i] = pp - t;
		}
		limit[maxLen] = pp + temp[maxLen] - 1;
		limit[maxLen+1] = INT_MAX; /* Sentinel value for reading next sym. */
		base[minLen] = 0;
	}

	/* We've finished reading and digesting the block header.  Now read this
	   block's Huffman coded symbols from the file and undo the Huffman coding
	   and run length encoding, saving the result into dbuf[dbufCount++] = uc */

	/* Initialize symbol occurrence counters and symbol Move To Front table */
	/*memset(byteCount, 0, sizeof(byteCount)); - smaller, but slower */
	for (i = 0; i < 256; i++) {
		byteCount[i] = 0;
		mtfSymbol[i] = (uint8_t)i;
	}

	/* Loop through compressed symbols. */

	runPos = dbufCount = selector = 0;
	for (;;) {
		struct group_data *hufGroup;
		int *base, *limit;
		int nextSym;
		uint8_t ngrp;

		/* Fetch next Huffman coding group from list. */
		symCount = GROUP_SIZE - 1;
		if (selector >= nSelectors)
			return RETVAL_DATA_ERROR;
		ngrp = selectors[selector++];
		if (ngrp >= groupCount) {
			dbg("%d selectors[%d]:%d groupCount:%d",
				__LINE__, selector-1, ngrp, groupCount);
			return RETVAL_DATA_ERROR;
		}
		hufGroup = bd->groups + ngrp;
		base = hufGroup->base - 1;
		limit = hufGroup->limit - 1;

 continue_this_group:
		/* Read next Huffman-coded symbol. */

		/* Note: It is far cheaper to read maxLen bits and back up than it is
		   to read minLen bits and then add additional bit at a time, testing
		   as we go.  Because there is a trailing last block (with file CRC),
		   there is no danger of the overread causing an unexpected EOF for a
		   valid compressed file.
		 */
		if (1) {
			/* As a further optimization, we do the read inline
			   (falling back to a call to get_bits if the buffer runs dry).
			 */
			int new_cnt;
			while ((new_cnt = bd->inbufBitCount - hufGroup->maxLen) < 0) {
				/* bd->inbufBitCount < hufGroup->maxLen */
				if (bd->inbufPos == bd->inbufCount) {
					nextSym = get_bits(bd, hufGroup->maxLen);
					goto got_huff_bits;
				}
				bd->inbufBits = (bd->inbufBits << 8) | bd->inbuf[bd->inbufPos++];
				bd->inbufBitCount += 8;
			};
			bd->inbufBitCount = new_cnt; /* "bd->inbufBitCount -= hufGroup->maxLen;" */
			nextSym = (bd->inbufBits >> new_cnt) & ((1 << hufGroup->maxLen) - 1);
 got_huff_bits: ;
		} else { /* unoptimized equivalent */
			nextSym = get_bits(bd, hufGroup->maxLen);
		}
		/* Figure how many bits are in next symbol and unget extras */
		i = hufGroup->minLen;
		while (nextSym > limit[i])
			++i;
		j = hufGroup->maxLen - i;
		if (j < 0)
			return RETVAL_DATA_ERROR;
		bd->inbufBitCount += j;

		/* Huffman decode value to get nextSym (with bounds checking) */
		nextSym = (nextSym >> j) - base[i];
		if ((unsigned)nextSym >= MAX_SYMBOLS)
			return RETVAL_DATA_ERROR;
		nextSym = hufGroup->permute[nextSym];

		/* We have now decoded the symbol, which indicates either a new literal
		   byte, or a repeated run of the most recent literal byte.  First,
		   check if nextSym indicates a repeated run, and if so loop collecting
		   how many times to repeat the last literal. */
		if ((unsigned)nextSym <= SYMBOL_RUNB) { /* RUNA or RUNB */

			/* If this is the start of a new run, zero out counter */
			if (runPos == 0) {
				runPos = 1;
				runCnt = 0;
			}

			/* Neat trick that saves 1 symbol: instead of or-ing 0 or 1 at
			   each bit position, add 1 or 2 instead.  For example,
			   1011 is 1<<0 + 1<<1 + 2<<2.  1010 is 2<<0 + 2<<1 + 1<<2.
			   You can make any bit pattern that way using 1 less symbol than
			   the basic or 0/1 method (except all bits 0, which would use no
			   symbols, but a run of length 0 doesn't mean anything in this
			   context).  Thus space is saved. */
			runCnt += (runPos << nextSym); /* +runPos if RUNA; +2*runPos if RUNB */
//The 32-bit overflow of runCnt wasn't yet seen, but probably can happen.
//This would be the fix (catches too large count way before it can overflow):
//			if (runCnt > bd->dbufSize) {
//				dbg("runCnt:%u > dbufSize:%u RETVAL_DATA_ERROR",
//						runCnt, bd->dbufSize);
//				return RETVAL_DATA_ERROR;
//			}
			if (runPos < bd->dbufSize) runPos <<= 1;
			goto end_of_huffman_loop;
		}

		/* When we hit the first non-run symbol after a run, we now know
		   how many times to repeat the last literal, so append that many
		   copies to our buffer of decoded symbols (dbuf) now.  (The last
		   literal used is the one at the head of the mtfSymbol array.) */
		if (runPos != 0) {
			uint8_t tmp_byte;
			if (dbufCount + runCnt > bd->dbufSize) {
				dbg("dbufCount:%u+runCnt:%u %u > dbufSize:%u RETVAL_DATA_ERROR",
						dbufCount, runCnt, dbufCount + runCnt, bd->dbufSize);
				return RETVAL_DATA_ERROR;
			}
			tmp_byte = symToByte[mtfSymbol[0]];
			byteCount[tmp_byte] += runCnt;
			while ((int)--runCnt >= 0)
				dbuf[dbufCount++] = (uint32_t)tmp_byte;
			runPos = 0;
		}

		/* Is this the terminating symbol? */
		if (nextSym > symTotal) break;

		/* At this point, nextSym indicates a new literal character.  Subtract
		   one to get the position in the MTF array at which this literal is
		   currently to be found.  (Note that the result can't be -1 or 0,
		   because 0 and 1 are RUNA and RUNB.  But another instance of the
		   first symbol in the mtf array, position 0, would have been handled
		   as part of a run above.  Therefore 1 unused mtf position minus
		   2 non-literal nextSym values equals -1.) */
		if (dbufCount >= bd->dbufSize) return RETVAL_DATA_ERROR;
		i = nextSym - 1;
		uc = mtfSymbol[i];

		/* Adjust the MTF array.  Since we typically expect to move only a
		 * small number of symbols, and are bound by 256 in any case, using
		 * memmove here would typically be bigger and slower due to function
		 * call overhead and other assorted setup costs. */
		do {
			mtfSymbol[i] = mtfSymbol[i-1];
		} while (--i);
		mtfSymbol[0] = uc;
		uc = symToByte[uc];

		/* We have our literal byte.  Save it into dbuf. */
		byteCount[uc]++;
		dbuf[dbufCount++] = (uint32_t)uc;

		/* Skip group initialization if we're not done with this group.  Done
		 * this way to avoid compiler warning. */
 end_of_huffman_loop:
		if (--symCount >= 0) goto continue_this_group;
	}

	/* At this point, we've read all the Huffman-coded symbols (and repeated
	   runs) for this block from the input stream, and decoded them into the
	   intermediate buffer.  There are dbufCount many decoded bytes in dbuf[].
	   Now undo the Burrows-Wheeler transform on dbuf.
	   See http://dogma.net/markn/articles/bwt/bwt.htm
	 */

	/* Turn byteCount into cumulative occurrence counts of 0 to n-1. */
	j = 0;
	for (i = 0; i < 256; i++) {
		int tmp_count = j + byteCount[i];
		byteCount[i] = j;
		j = tmp_count;
	}

	/* Figure out what order dbuf would be in if we sorted it. */
	for (i = 0; i < dbufCount; i++) {
		uint8_t tmp_byte = (uint8_t)dbuf[i];
		int tmp_count = byteCount[tmp_byte];
		dbuf[tmp_count] |= (i << 8);
		byteCount[tmp_byte] = tmp_count + 1;
	}

	/* Decode first byte by hand to initialize "previous" byte.  Note that it
	   doesn't get output, and if the first three characters are identical
	   it doesn't qualify as a run (hence writeRunCountdown=5). */
	if (dbufCount) {
		uint32_t tmp;
		if ((int)origPtr >= dbufCount) return RETVAL_DATA_ERROR;
		tmp = dbuf[origPtr];
		bd->writeCurrent = (uint8_t)tmp;
		bd->writePos = (tmp >> 8);
		bd->writeRunCountdown = 5;
	}
	bd->writeCount = dbufCount;

	return RETVAL_OK;
}

/* Undo Burrows-Wheeler transform on intermediate buffer to produce output.
   If start_bunzip was initialized with out_fd=-1, then up to len bytes of
   data are written to outbuf.  Return value is number of bytes written or
   error (all errors are negative numbers).  If out_fd!=-1, outbuf and len
   are ignored, data is written to out_fd and return is RETVAL_OK or error.

   NB: read_bunzip returns < 0 on error, or the number of *unfilled* bytes
   in outbuf. IOW: on EOF returns len ("all bytes are not filled"), not 0.
   (Why? This allows to get rid of one local variable)
*/
int FAST_FUNC read_bunzip(bunzip_data *bd, char *outbuf, int len)
{
	const uint32_t *dbuf;
	int pos, current, previous;
	uint32_t CRC;

	/* If we already have error/end indicator, return it */
	if (bd->writeCount < 0)
		return bd->writeCount;

	dbuf = bd->dbuf;

	/* Register-cached state (hopefully): */
	pos = bd->writePos;
	current = bd->writeCurrent;
	CRC = bd->writeCRC; /* small loss on x86-32 (not enough regs), win on x86-64 */

	/* We will always have pending decoded data to write into the output
	   buffer unless this is the very first call (in which case we haven't
	   Huffman-decoded a block into the intermediate buffer yet). */
	if (bd->writeCopies) {

 dec_writeCopies:
		/* Inside the loop, writeCopies means extra copies (beyond 1) */
		--bd->writeCopies;

		/* Loop outputting bytes */
		for (;;) {

			/* If the output buffer is full, save cached state and return */
			if (--len < 0) {
				/* Unlikely branch.
				 * Use of "goto" instead of keeping code here
				 * helps compiler to realize this. */
				goto outbuf_full;
			}

			/* Write next byte into output buffer, updating CRC */
			*outbuf++ = current;
			CRC = (CRC << 8) ^ bd->crc32Table[(CRC >> 24) ^ current];

			/* Loop now if we're outputting multiple copies of this byte */
			if (bd->writeCopies) {
				/* Unlikely branch */
				/*--bd->writeCopies;*/
				/*continue;*/
				/* Same, but (ab)using other existing --writeCopies operation
				 * (and this if() compiles into just test+branch pair): */
				goto dec_writeCopies;
			}
 decode_next_byte:
			if (--bd->writeCount < 0)
				break; /* input block is fully consumed, need next one */

			/* Follow sequence vector to undo Burrows-Wheeler transform */
			previous = current;
			pos = dbuf[pos];
			current = (uint8_t)pos;
			pos >>= 8;

			/* After 3 consecutive copies of the same byte, the 4th
			 * is a repeat count.  We count down from 4 instead
			 * of counting up because testing for non-zero is faster */
			if (--bd->writeRunCountdown != 0) {
				if (current != previous)
					bd->writeRunCountdown = 4;
			} else {
				/* Unlikely branch */
				/* We have a repeated run, this byte indicates the count */
				bd->writeCopies = current;
				current = previous;
				bd->writeRunCountdown = 5;

				/* Sometimes there are just 3 bytes (run length 0) */
				if (!bd->writeCopies) goto decode_next_byte;

				/* Subtract the 1 copy we'd output anyway to get extras */
				--bd->writeCopies;
			}
		} /* for(;;) */

		/* Decompression of this input block completed successfully */
		bd->writeCRC = CRC = ~CRC;
		bd->totalCRC = ((bd->totalCRC << 1) | (bd->totalCRC >> 31)) ^ CRC;

		/* If this block had a CRC error, force file level CRC error */
		if (CRC != bd->headerCRC) {
			bd->totalCRC = bd->headerCRC + 1;
			return RETVAL_LAST_BLOCK;
		}
	}

	/* Refill the intermediate buffer by Huffman-decoding next block of input */
	{
		int r = get_next_block(bd);
		if (r) { /* error/end */
			bd->writeCount = r;
			return (r != RETVAL_LAST_BLOCK) ? r : len;
		}
	}

	CRC = ~0;
	pos = bd->writePos;
	current = bd->writeCurrent;
	goto decode_next_byte;

 outbuf_full:
	/* Output buffer is full, save cached state and return */
	bd->writePos = pos;
	bd->writeCurrent = current;
	bd->writeCRC = CRC;

	bd->writeCopies++;

	return 0;
}

/* Allocate the structure, read file header.  If in_fd==-1, inbuf must contain
   a complete bunzip file (len bytes long).  If in_fd!=-1, inbuf and len are
   ignored, and data is read from file handle into temporary buffer. */

/* Because bunzip2 is used for help text unpacking, and because bb_show_usage()
   should work for NOFORK applets too, we must be extremely careful to not leak
   any allocations! */
int FAST_FUNC start_bunzip(
		void *jmpbuf,
		bunzip_data **bdp,
		int in_fd,
		const void *inbuf, int len)
{
	bunzip_data *bd;
	unsigned i;
	enum {
		BZh0 = ('B' << 24) + ('Z' << 16) + ('h' << 8) + '0',
		h0 = ('h' << 8) + '0',
	};

	/* Figure out how much data to allocate */
	i = sizeof(bunzip_data);
	if (in_fd != -1)
		i += IOBUF_SIZE;

	/* Allocate bunzip_data.  Most fields initialize to zero. */
	bd = *bdp = xzalloc(i);

	bd->jmpbuf = jmpbuf;

	/* Setup input buffer */
	bd->in_fd = in_fd;
	if (-1 == in_fd) {
		/* in this case, bd->inbuf is read-only */
		bd->inbuf = (void*)inbuf; /* cast away const-ness */
	} else {
		bd->inbuf = (uint8_t*)(bd + 1);
		memcpy(bd->inbuf, inbuf, len);
	}
	bd->inbufCount = len;

	/* Init the CRC32 table (big endian) */
	crc32_filltable(bd->crc32Table, 1);

	/* Ensure that file starts with "BZh['1'-'9']." */
	/* Update: now caller verifies 1st two bytes, makes .gz/.bz2
	 * integration easier */
	/* was: */
	/* i = get_bits(bd, 32); */
	/* if ((unsigned)(i - BZh0 - 1) >= 9) return RETVAL_NOT_BZIP_DATA; */
	i = get_bits(bd, 16);
	if ((unsigned)(i - h0 - 1) >= 9) return RETVAL_NOT_BZIP_DATA;

	/* Fourth byte (ascii '1'-'9') indicates block size in units of 100k of
	   uncompressed data.  Allocate intermediate buffer for block. */
	/* bd->dbufSize = 100000 * (i - BZh0); */
	bd->dbufSize = 100000 * (i - h0);

	/* Cannot use xmalloc - may leak bd in NOFORK case! */
	bd->dbuf = malloc_or_warn(bd->dbufSize * sizeof(bd->dbuf[0]));
	if (!bd->dbuf) {
		free(bd);
		xfunc_die();
	}
	return RETVAL_OK;
}

void FAST_FUNC dealloc_bunzip(bunzip_data *bd)
{
	free(bd->dbuf);
	free(bd);
}


/* Decompress src_fd to dst_fd.  Stops at end of bzip data, not end of file. */
IF_DESKTOP(long long) int FAST_FUNC
unpack_bz2_stream(transformer_state_t *xstate)
{
	IF_DESKTOP(long long total_written = 0;)
	bunzip_data *bd;
	char *outbuf;
	int i;
	unsigned len;

	if (check_signature16(xstate, BZIP2_MAGIC))
		return -1;

	outbuf = xmalloc(IOBUF_SIZE);
	len = 0;
	while (1) { /* "Process one BZ... stream" loop */
		jmp_buf jmpbuf;

		/* Setup for I/O error handling via longjmp */
		i = setjmp(jmpbuf);
		if (i == 0)
			i = start_bunzip(&jmpbuf, &bd, xstate->src_fd, outbuf + 2, len);

		if (i == 0) {
			while (1) { /* "Produce some output bytes" loop */
				i = read_bunzip(bd, outbuf, IOBUF_SIZE);
				if (i < 0) /* error? */
					break;
				i = IOBUF_SIZE - i; /* number of bytes produced */
				if (i == 0) /* EOF? */
					break;
				if (i != transformer_write(xstate, outbuf, i)) {
					i = RETVAL_SHORT_WRITE;
					goto release_mem;
				}
				IF_DESKTOP(total_written += i;)
			}
		}

		if (i != RETVAL_LAST_BLOCK
		/* Observed case when i == RETVAL_OK:
		 * "bzcat z.bz2", where "z.bz2" is a bzipped zero-length file
		 * (to be exact, z.bz2 is exactly these 14 bytes:
		 * 42 5a 68 39 17 72 45 38  50 90 00 00 00 00).
		 */
		 && i != RETVAL_OK
		) {
			bb_error_msg("bunzip error %d", i);
			break;
		}
		if (bd->headerCRC != bd->totalCRC) {
			bb_error_msg("CRC error");
			break;
		}

		/* Successfully unpacked one BZ stream */
		i = RETVAL_OK;

		/* Do we have "BZ..." after last processed byte?
		 * pbzip2 (parallelized bzip2) produces such files.
		 */
		len = bd->inbufCount - bd->inbufPos;
		memcpy(outbuf, &bd->inbuf[bd->inbufPos], len);
		if (len < 2) {
			if (safe_read(xstate->src_fd, outbuf + len, 2 - len) != 2 - len)
				break;
			len = 2;
		}
		if (*(uint16_t*)outbuf != BZIP2_MAGIC) /* "BZ"? */
			break;
		dealloc_bunzip(bd);
		len -= 2;
	}

 release_mem:
	dealloc_bunzip(bd);
	free(outbuf);

	return i ? i : IF_DESKTOP(total_written) + 0;
}

#ifdef TESTING

static char *const bunzip_errors[] = {
	NULL, "Bad file checksum", "Not bzip data",
	"Unexpected input EOF", "Unexpected output EOF", "Data error",
	"Out of memory", "Obsolete (pre 0.9.5) bzip format not supported"
};

/* Dumb little test thing, decompress stdin to stdout */
int main(int argc, char **argv)
{
	char c;

	int i = unpack_bz2_stream(0, 1);
	if (i < 0)
		fprintf(stderr, "%s\n", bunzip_errors[-i]);
	else if (read(STDIN_FILENO, &c, 1))
		fprintf(stderr, "Trailing garbage ignored\n");
	return -i;
}
#endif
