/*
-*- linux-c -*-
   drbd_receiver.c
   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _DRBD_VLI_H
#define _DRBD_VLI_H

/*
 * At a granularity of 4KiB storage represented per bit,
 * and stroage sizes of several TiB,
 * and possibly small-bandwidth replication,
 * the bitmap transfer time can take much too long,
 * if transmitted in plain text.
 *
 * We try to reduce the transfered bitmap information
 * by encoding runlengths of bit polarity.
 *
 * We never actually need to encode a "zero" (runlengths are positive).
 * But then we have to store the value of the first bit.
 * The first bit of information thus shall encode if the first runlength
 * gives the number of set or unset bits.
 *
 * We assume that large areas are either completely set or unset,
 * which gives good compression with any runlength method,
 * even when encoding the runlength as fixed size 32bit/64bit integers.
 *
 * Still, there may be areas where the polarity flips every few bits,
 * and encoding the runlength sequence of those areas with fix size
 * integers would be much worse than plaintext.
 *
 * We want to encode small runlength values with minimum code length,
 * while still being able to encode a Huge run of all zeros.
 *
 * Thus we need a Variable Length Integer encoding, VLI.
 *
 * For some cases, we produce more code bits than plaintext input.
 * We need to send incompressible chunks as plaintext, skip over them
 * and then see if the next chunk compresses better.
 *
 * We don't care too much about "excellent" compression ratio for large
 * runlengths (all set/all clear): whether we achieve a factor of 100
 * or 1000 is not that much of an issue.
 * We do not want to waste too much on short runlengths in the "noisy"
 * parts of the bitmap, though.
 *
 * There are endless variants of VLI, we experimented with:
 *  * simple byte-based
 *  * various bit based with different code word length.
 *
 * To avoid yet an other configuration parameter (choice of bitmap compression
 * algorithm) which was difficult to explain and tune, we just chose the one
 * variant that turned out best in all test cases.
 * Based on real world usage patterns, with device sizes ranging from a few GiB
 * to several TiB, file server/mailserver/webserver/mysql/postgress,
 * mostly idle to really busy, the all time winner (though sometimes only
 * marginally better) is:
 */

/*
 * encoding is "visualised" as
 * __little endian__ bitstream, least significant bit first (left most)
 *
 * this particular encoding is chosen so that the prefix code
 * starts as unary encoding the level, then modified so that
 * 10 levels can be described in 8bit, with minimal overhead
 * for the smaller levels.
 *
 * Number of data bits follow fibonacci sequence, with the exception of the
 * last level (+1 data bit, so it makes 64bit total).  The only worse code when
 * encoding bit polarity runlength is 1 plain bits => 2 code bits.
prefix    data bits                                    max val  Nº data bits
0 x                                                         0x2            1
10 x                                                        0x4            1
110 xx                                                      0x8            2
1110 xxx                                                   0x10            3
11110 xxx xx                                               0x30            5
111110 xx xxxxxx                                          0x130            8
11111100  xxxxxxxx xxxxx                                 0x2130           13
11111110  xxxxxxxx xxxxxxxx xxxxx                      0x202130           21
11111101  xxxxxxxx xxxxxxxx xxxxxxxx  xxxxxxxx xx   0x400202130           34
11111111  xxxxxxxx xxxxxxxx xxxxxxxx  xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx 56
 * maximum encodable value: 0x100000400202130 == 2**56 + some */

/* compression "table":
 transmitted   x                                0.29
 as plaintext x                                  ........................
             x                                   ........................
            x                                    ........................
           x    0.59                         0.21........................
          x      ........................................................
         x       .. c ...................................................
        x    0.44.. o ...................................................
       x .......... d ...................................................
      x  .......... e ...................................................
     X.............   ...................................................
    x.............. b ...................................................
2.0x............... i ...................................................
 #X................ t ...................................................
 #................. s ...........................  plain bits  ..........
-+-----------------------------------------------------------------------
 1             16              32                              64
*/

/* LEVEL: (total bits, prefix bits, prefix value),
 * sorted ascending by number of total bits.
 * The rest of the code table is calculated at compiletime from this. */

/* fibonacci data 1, 1, ... */
#define VLI_L_1_1() do { \
	LEVEL( 2, 1, 0x00); \
	LEVEL( 3, 2, 0x01); \
	LEVEL( 5, 3, 0x03); \
	LEVEL( 7, 4, 0x07); \
	LEVEL(10, 5, 0x0f); \
	LEVEL(14, 6, 0x1f); \
	LEVEL(21, 8, 0x3f); \
	LEVEL(29, 8, 0x7f); \
	LEVEL(42, 8, 0xbf); \
	LEVEL(64, 8, 0xff); \
	} while (0)

/* finds a suitable level to decode the least significant part of in.
 * returns number of bits consumed.
 *
 * BUG() for bad input, as that would mean a buggy code table. */
static inline int vli_decode_bits(u64 *out, const u64 in)
{
	u64 adj = 1;

#define LEVEL(t,b,v)					\
	do {						\
		if ((in & ((1 << b) -1)) == v) {	\
			*out = ((in & ((~0ULL) >> (64-t))) >> b) + adj;	\
			return t;			\
		}					\
		adj += 1ULL << (t - b);			\
	} while (0)

	VLI_L_1_1();

	/* NOT REACHED, if VLI_LEVELS code table is defined properly */
	BUG();
#undef LEVEL
}

/* return number of code bits needed,
 * or negative error number */
static inline int __vli_encode_bits(u64 *out, const u64 in)
{
	u64 max = 0;
	u64 adj = 1;

	if (in == 0)
		return -EINVAL;

#define LEVEL(t,b,v) do {		\
		max += 1ULL << (t - b);	\
		if (in <= max) {	\
			if (out)	\
				*out = ((in - adj) << b) | v;	\
			return t;	\
		}			\
		adj = max + 1;		\
	} while (0)

	VLI_L_1_1();

	return -EOVERFLOW;
#undef LEVEL
}

#undef VLI_L_1_1

/* code from here down is independend of actually used bit code */

/*
 * Code length is determined by some unique (e.g. unary) prefix.
 * This encodes arbitrary bit length, not whole bytes: we have a bit-stream,
 * not a byte stream.
 */

/* for the bitstream, we need a cursor */
struct bitstream_cursor {
	/* the current byte */
	u8 *b;
	/* the current bit within *b, nomalized: 0..7 */
	unsigned int bit;
};

/* initialize cursor to point to first bit of stream */
static inline void bitstream_cursor_reset(struct bitstream_cursor *cur, void *s)
{
	cur->b = s;
	cur->bit = 0;
}

/* advance cursor by that many bits; maximum expected input value: 64,
 * but depending on VLI implementation, it may be more. */
static inline void bitstream_cursor_advance(struct bitstream_cursor *cur, unsigned int bits)
{
	bits += cur->bit;
	cur->b = cur->b + (bits >> 3);
	cur->bit = bits & 7;
}

/* the bitstream itself knows its length */
struct bitstream {
	struct bitstream_cursor cur;
	unsigned char *buf;
	size_t buf_len;		/* in bytes */

	/* for input stream:
	 * number of trailing 0 bits for padding
	 * total number of valid bits in stream: buf_len * 8 - pad_bits */
	unsigned int pad_bits;
};

static inline void bitstream_init(struct bitstream *bs, void *s, size_t len, unsigned int pad_bits)
{
	bs->buf = s;
	bs->buf_len = len;
	bs->pad_bits = pad_bits;
	bitstream_cursor_reset(&bs->cur, bs->buf);
}

static inline void bitstream_rewind(struct bitstream *bs)
{
	bitstream_cursor_reset(&bs->cur, bs->buf);
	memset(bs->buf, 0, bs->buf_len);
}

/* Put (at most 64) least significant bits of val into bitstream, and advance cursor.
 * Ignores "pad_bits".
 * Returns zero if bits == 0 (nothing to do).
 * Returns number of bits used if successful.
 *
 * If there is not enough room left in bitstream,
 * leaves bitstream unchanged and returns -ENOBUFS.
 */
static inline int bitstream_put_bits(struct bitstream *bs, u64 val, const unsigned int bits)
{
	unsigned char *b = bs->cur.b;
	unsigned int tmp;

	if (bits == 0)
		return 0;

	if ((bs->cur.b + ((bs->cur.bit + bits -1) >> 3)) - bs->buf >= bs->buf_len)
		return -ENOBUFS;

	/* paranoia: strip off hi bits; they should not be set anyways. */
	if (bits < 64)
		val &= ~0ULL >> (64 - bits);

	*b++ |= (val & 0xff) << bs->cur.bit;

	for (tmp = 8 - bs->cur.bit; tmp < bits; tmp += 8)
		*b++ |= (val >> tmp) & 0xff;

	bitstream_cursor_advance(&bs->cur, bits);
	return bits;
}

/* Fetch (at most 64) bits from bitstream into *out, and advance cursor.
 *
 * If more than 64 bits are requested, returns -EINVAL and leave *out unchanged.
 *
 * If there are less than the requested number of valid bits left in the
 * bitstream, still fetches all available bits.
 *
 * Returns number of actually fetched bits.
 */
static inline int bitstream_get_bits(struct bitstream *bs, u64 *out, int bits)
{
	u64 val;
	unsigned int n;

	if (bits > 64)
		return -EINVAL;

	if (bs->cur.b + ((bs->cur.bit + bs->pad_bits + bits -1) >> 3) - bs->buf >= bs->buf_len)
		bits = ((bs->buf_len - (bs->cur.b - bs->buf)) << 3)
			- bs->cur.bit - bs->pad_bits;

	if (bits == 0) {
		*out = 0;
		return 0;
	}

	/* get the high bits */
	val = 0;
	n = (bs->cur.bit + bits + 7) >> 3;
	/* n may be at most 9, if cur.bit + bits > 64 */
	/* which means this copies at most 8 byte */
	if (n) {
		memcpy(&val, bs->cur.b+1, n - 1);
		val = le64_to_cpu(val) << (8 - bs->cur.bit);
	}

	/* we still need the low bits */
	val |= bs->cur.b[0] >> bs->cur.bit;

	/* and mask out bits we don't want */
	val &= ~0ULL >> (64 - bits);

	bitstream_cursor_advance(&bs->cur, bits);
	*out = val;

	return bits;
}

/* encodes @in as vli into @bs;

 * return values
 *  > 0: number of bits successfully stored in bitstream
 * -ENOBUFS @bs is full
 * -EINVAL input zero (invalid)
 * -EOVERFLOW input too large for this vli code (invalid)
 */
static inline int vli_encode_bits(struct bitstream *bs, u64 in)
{
	u64 code = code;
	int bits = __vli_encode_bits(&code, in);

	if (bits <= 0)
		return bits;

	return bitstream_put_bits(bs, code, bits);
}

#endif
