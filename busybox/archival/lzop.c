/*
   This file is part of the lzop file compressor.

   Copyright (C) 1996..2003 Markus Franz Xaver Johannes Oberhumer
   All Rights Reserved.

   Markus F.X.J. Oberhumer <markus@oberhumer.com>
   http://www.oberhumer.com/opensource/lzop/

   lzop and the LZO library are free software; you can redistribute them
   and/or modify them under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   "Minimalized" for busybox by Alain Knaff
*/
//config:config LZOP
//config:	bool "lzop (13 kb)"
//config:	default y
//config:	help
//config:	Lzop compression/decompresion.
//config:
//config:config UNLZOP
//config:	bool "unlzop (13 kb)"
//config:	default n  # INCOMPAT: upstream lzop does not provide such tool
//config:	help
//config:	Lzop decompresion.
//config:
//config:config LZOPCAT
//config:	bool "lzopcat (13 kb)"
//config:	default n  # INCOMPAT: upstream lzop does not provide such tool
//config:	help
//config:	Alias to "lzop -dc".
//config:
//config:config LZOP_COMPR_HIGH
//config:	bool "lzop compression levels 7,8,9 (not very useful)"
//config:	default n
//config:	depends on LZOP || UNLZOP || LZOPCAT
//config:	help
//config:	High levels (7,8,9) of lzop compression. These levels
//config:	are actually slower than gzip at equivalent compression ratios
//config:	and take up 3.2K of code.

//applet:IF_LZOP(APPLET(lzop, BB_DIR_BIN, BB_SUID_DROP))
//                  APPLET_ODDNAME:name     main  location        suid_type     help
//applet:IF_UNLZOP( APPLET_ODDNAME(unlzop,  lzop, BB_DIR_USR_BIN, BB_SUID_DROP, unlzop))
//applet:IF_LZOPCAT(APPLET_ODDNAME(lzopcat, lzop, BB_DIR_USR_BIN, BB_SUID_DROP, lzopcat))

//kbuild:lib-$(CONFIG_LZOP) += lzop.o
//kbuild:lib-$(CONFIG_UNLZOP) += lzop.o
//kbuild:lib-$(CONFIG_LZOPCAT) += lzop.o

//usage:#define lzop_trivial_usage
//usage:       "[-cfUvd123456789CF] [FILE]..."
//usage:#define lzop_full_usage "\n\n"
//usage:       "	-1..9	Compression level"
//usage:     "\n	-d	Decompress"
//usage:     "\n	-c	Write to stdout"
//usage:     "\n	-f	Force"
//usage:     "\n	-U	Delete input files"
///////:     "\n	-k	Keep input files" (default, so why bother documenting?)
//usage:     "\n	-v	Verbose"
//usage:     "\n	-F	Don't store or verify checksum"
//usage:     "\n	-C	Also write checksum of compressed block"
//usage:
//usage:#define lzopcat_trivial_usage
//usage:       "[-vF] [FILE]..."
//usage:#define lzopcat_full_usage "\n\n"
//usage:       "	-v	Verbose"
//usage:     "\n	-F	Don't verify checksum"
//usage:
//usage:#define unlzop_trivial_usage
//usage:       "[-cfUvF] [FILE]..."
//usage:#define unlzop_full_usage "\n\n"
//usage:       "	-c	Write to stdout"
//usage:     "\n	-f	Force"
//usage:     "\n	-U	Delete input files"
///////:     "\n	-k	Keep input files" (default, so why bother documenting?)
//usage:     "\n	-v	Verbose"
//usage:     "\n	-F	Don't verify checksum"

#include "libbb.h"
#include "common_bufsiz.h"
#include "bb_archive.h"
#include "liblzo_interface.h"

/* lzo-2.03/src/lzo_ptr.h */
#define pd(a,b)	 ((unsigned)((a)-(b)))

#define lzo_version()			LZO_VERSION
#define lzo_sizeof_dict_t		(sizeof(uint8_t*))

/* lzo-2.03/include/lzo/lzo1x.h */
#define LZO1X_1_MEM_COMPRESS	(16384 * lzo_sizeof_dict_t)
#define LZO1X_1_15_MEM_COMPRESS (32768 * lzo_sizeof_dict_t)
#define LZO1X_999_MEM_COMPRESS	(14 * 16384 * sizeof(short))

/* lzo-2.03/src/lzo1x_oo.c */
#define NO_LIT UINT_MAX

/**********************************************************************/
static void copy2(uint8_t* ip, const uint8_t* m_pos, unsigned off)
{
	ip[0] = m_pos[0];
	if (off == 1)
		ip[1] = m_pos[0];
	else
		ip[1] = m_pos[1];
}

static void copy3(uint8_t* ip, const uint8_t* m_pos, unsigned off)
{
	ip[0] = m_pos[0];
	if (off == 1) {
		ip[2] = ip[1] = m_pos[0];
	}
	else if (off == 2) {
		ip[1] = m_pos[1];
		ip[2] = m_pos[0];
	}
	else {
		ip[1] = m_pos[1];
		ip[2] = m_pos[2];
	}
}

/**********************************************************************/
// optimize a block of data.
/**********************************************************************/
#define TEST_IP		(ip < ip_end)
#define TEST_OP		(op <= op_end)

static NOINLINE int lzo1x_optimize(uint8_t *in, unsigned in_len,
		uint8_t *out, unsigned *out_len /*, void* wrkmem */)
{
	uint8_t* op;
	uint8_t* ip;
	unsigned t;
	uint8_t* m_pos;
	uint8_t* const ip_end = in + in_len;
	uint8_t* const op_end = out + *out_len;
	uint8_t* litp = NULL;
	unsigned lit = 0;
	unsigned next_lit = NO_LIT;
	unsigned nl;
	unsigned long o_m1_a = 0, o_m1_b = 0, o_m2 = 0, o_m3_a = 0, o_m3_b = 0;

//	LZO_UNUSED(wrkmem);

	*out_len = 0;

	op = out;
	ip = in;

	if (*ip > 17) {
		t = *ip++ - 17;
		if (t < 4)
			goto match_next;
		goto first_literal_run;
	}

	while (TEST_IP && TEST_OP) {
		t = *ip++;
		if (t >= 16)
			goto match;
		/* a literal run */
		litp = ip - 1;
		if (t == 0) {
			t = 15;
			while (*ip == 0)
				t += 255, ip++;
			t += *ip++;
		}
		lit = t + 3;
		/* copy literals */
 copy_literal_run:
		*op++ = *ip++;
		*op++ = *ip++;
		*op++ = *ip++;
 first_literal_run:
		do *op++ = *ip++; while (--t > 0);

		t = *ip++;

		if (t >= 16)
			goto match;
#if defined(LZO1X)
		m_pos = op - 1 - 0x800;
#elif defined(LZO1Y)
		m_pos = op - 1 - 0x400;
#endif
		m_pos -= t >> 2;
		m_pos -= *ip++ << 2;
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		lit = 0;
		goto match_done;


		/* handle matches */
		do {
			if (t < 16) { /* a M1 match */
				m_pos = op - 1;
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;

				if (litp == NULL)
					goto copy_m1;

				nl = ip[-2] & 3;
				/* test if a match follows */
				if (nl == 0 && lit == 1 && ip[0] >= 16) {
					next_lit = nl;
					/* adjust length of previous short run */
					lit += 2;
					*litp = (unsigned char)((*litp & ~3) | lit);
					/* copy over the 2 literals that replace the match */
					copy2(ip-2, m_pos, pd(op, m_pos));
					o_m1_a++;
				}
				/* test if a literal run follows */
				else
				if (nl == 0
				 && ip[0] < 16
				 && ip[0] != 0
				 && (lit + 2 + ip[0] < 16)
				) {
					t = *ip++;
					/* remove short run */
					*litp &= ~3;
					/* copy over the 2 literals that replace the match */
					copy2(ip-3+1, m_pos, pd(op, m_pos));
					/* move literals 1 byte ahead */
					litp += 2;
					if (lit > 0)
						memmove(litp+1, litp, lit);
					/* insert new length of long literal run */
					lit += 2 + t + 3;
					*litp = (unsigned char)(lit - 3);

					o_m1_b++;
					*op++ = *m_pos++;
					*op++ = *m_pos++;
					goto copy_literal_run;
				}
 copy_m1:
				*op++ = *m_pos++;
				*op++ = *m_pos++;
			} else {
 match:
				if (t >= 64) {				/* a M2 match */
					m_pos = op - 1;
#if defined(LZO1X)
					m_pos -= (t >> 2) & 7;
					m_pos -= *ip++ << 3;
					t = (t >> 5) - 1;
#elif defined(LZO1Y)
					m_pos -= (t >> 2) & 3;
					m_pos -= *ip++ << 2;
					t = (t >> 4) - 3;
#endif
					if (litp == NULL)
						goto copy_m;

					nl = ip[-2] & 3;
					/* test if in beetween two long literal runs */
					if (t == 1 && lit > 3 && nl == 0
					 && ip[0] < 16 && ip[0] != 0 && (lit + 3 + ip[0] < 16)
					) {
						t = *ip++;
						/* copy over the 3 literals that replace the match */
						copy3(ip-1-2, m_pos, pd(op, m_pos));
						/* set new length of previous literal run */
						lit += 3 + t + 3;
						*litp = (unsigned char)(lit - 3);
						o_m2++;
						*op++ = *m_pos++;
						*op++ = *m_pos++;
						*op++ = *m_pos++;
						goto copy_literal_run;
					}
				} else {
					if (t >= 32) {			/* a M3 match */
						t &= 31;
						if (t == 0) {
							t = 31;
							while (*ip == 0)
								t += 255, ip++;
							t += *ip++;
						}
						m_pos = op - 1;
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
					} else {					/* a M4 match */
						m_pos = op;
						m_pos -= (t & 8) << 11;
						t &= 7;
						if (t == 0) {
							t = 7;
							while (*ip == 0)
								t += 255, ip++;
							t += *ip++;
						}
						m_pos -= *ip++ >> 2;
						m_pos -= *ip++ << 6;
						if (m_pos == op)
							goto eof_found;
						m_pos -= 0x4000;
					}
					if (litp == NULL)
						goto copy_m;

					nl = ip[-2] & 3;
					/* test if in beetween two matches */
					if (t == 1 && lit == 0 && nl == 0 && ip[0] >= 16) {
						next_lit = nl;
						/* make a previous short run */
						lit += 3;
						*litp = (unsigned char)((*litp & ~3) | lit);
						/* copy over the 3 literals that replace the match */
						copy3(ip-3, m_pos, pd(op, m_pos));
						o_m3_a++;
					}
					/* test if a literal run follows */
					else if (t == 1 && lit <= 3 && nl == 0
					 && ip[0] < 16 && ip[0] != 0 && (lit + 3 + ip[0] < 16)
					) {
						t = *ip++;
						/* remove short run */
						*litp &= ~3;
						/* copy over the 3 literals that replace the match */
						copy3(ip-4+1, m_pos, pd(op, m_pos));
						/* move literals 1 byte ahead */
						litp += 2;
						if (lit > 0)
							memmove(litp+1,litp,lit);
						/* insert new length of long literal run */
						lit += 3 + t + 3;
						*litp = (unsigned char)(lit - 3);

						o_m3_b++;
						*op++ = *m_pos++;
						*op++ = *m_pos++;
						*op++ = *m_pos++;
						goto copy_literal_run;
					}
				}
 copy_m:
				*op++ = *m_pos++;
				*op++ = *m_pos++;
				do *op++ = *m_pos++; while (--t > 0);
			}

 match_done:
			if (next_lit == NO_LIT) {
				t = ip[-2] & 3;
				lit = t;
				litp = ip - 2;
			}
			else
				t = next_lit;
			next_lit = NO_LIT;
			if (t == 0)
				break;
			/* copy literals */
 match_next:
			do *op++ = *ip++; while (--t > 0);
			t = *ip++;
		} while (TEST_IP && TEST_OP);
	}

	/* no EOF code was found */
	*out_len = pd(op, out);
	return LZO_E_EOF_NOT_FOUND;

 eof_found:
//	LZO_UNUSED(o_m1_a); LZO_UNUSED(o_m1_b); LZO_UNUSED(o_m2);
//	LZO_UNUSED(o_m3_a); LZO_UNUSED(o_m3_b);
	*out_len = pd(op, out);
	return (ip == ip_end ? LZO_E_OK :
		(ip < ip_end ? LZO_E_INPUT_NOT_CONSUMED : LZO_E_INPUT_OVERRUN));
}

/**********************************************************************/
#define F_OS F_OS_UNIX
#define F_CS F_CS_NATIVE

/**********************************************************************/
#define ADLER32_INIT_VALUE 1
#define CRC32_INIT_VALUE   0

/**********************************************************************/
enum {
	M_LZO1X_1    = 1,
	M_LZO1X_1_15 = 2,
	M_LZO1X_999  = 3,
};

/**********************************************************************/
/* header flags */
#define F_ADLER32_D     0x00000001L
#define F_ADLER32_C     0x00000002L
#define F_H_EXTRA_FIELD 0x00000040L
#define F_H_GMTDIFF     0x00000080L
#define F_CRC32_D       0x00000100L
#define F_CRC32_C       0x00000200L
#define F_H_FILTER      0x00000800L
#define F_H_CRC32       0x00001000L
#define F_MASK          0x00003FFFL

/* operating system & file system that created the file [mostly unused] */
#define F_OS_UNIX       0x03000000L
#define F_OS_SHIFT      24
#define F_OS_MASK       0xff000000L

/* character set for file name encoding [mostly unused] */
#define F_CS_NATIVE     0x00000000L
#define F_CS_SHIFT      20
#define F_CS_MASK       0x00f00000L

/* these bits must be zero */
#define F_RESERVED      ((F_MASK | F_OS_MASK | F_CS_MASK) ^ 0xffffffffL)

typedef struct chksum_t {
	uint32_t f_adler32;
	uint32_t f_crc32;
} chksum_t;

typedef struct header_t {
	/* used to have auxiliary fields here */

	/* Starting from here, the layout and endianness
	 * are exactly in on-disk format.
	 */
	uint16_t version_be16;
	uint16_t lib_version_be16;
	uint16_t version_needed_to_extract_be16;
	uint8_t  method;
	uint8_t	 level;
	uint32_t flags32; /* be32 on disk, but we keep this field in native order */
	uint32_t mode_be32;
	uint32_t mtime_be32;
	uint32_t gmtdiff_be32;
	char     len_and_name[1+255+1];
} header_t;

struct globals {
	/*const uint32_t *lzo_crc32_table;*/
	chksum_t chksum;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
//#define G (*ptr_to_globals)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	/*SET_PTR_TO_GLOBALS(xzalloc(sizeof(G)));*/ \
} while (0)


/**********************************************************************/
#define LZOP_VERSION            0x1010
//#define LZOP_VERSION_STRING     "1.01"
//#define LZOP_VERSION_DATE       "Apr 27th 2003"

// lzop wants to be weird:
// unlike all other compressosrs, its -k "keep" option is the default,
// and -U is used to delete the source. We will invert the bit after getopt().
#define OPTION_STRING "cfUvqdt123456789CFk"

/* Note: must be kept in sync with archival/bbunzip.c */
enum {
	OPT_STDOUT      = (1 << 0),
	OPT_FORCE       = (1 << 1),
	OPT_KEEP        = (1 << 2),
	OPT_VERBOSE     = (1 << 3),
	OPT_QUIET       = (1 << 4),
	OPT_DECOMPRESS  = (1 << 5),
	OPT_TEST        = (1 << 6),
	OPT_1           = (1 << 7),
	OPT_2           = (1 << 8),
	OPT_3           = (1 << 9),
	OPT_4           = (1 << 10),
	OPT_5           = (1 << 11),
	OPT_6           = (1 << 12),
	OPT_7           = (1 << 13),
	OPT_8           = (1 << 14),
	OPT_9           = (1 << 15),
	OPT_C           = (1 << 16),
	OPT_F           = (1 << 17),
	OPT_k           = (1 << 18),
	OPT_789         = OPT_7 | OPT_8 | OPT_9
};

/**********************************************************************/
// adler32 checksum
// adapted from free code by Mark Adler <madler@alumni.caltech.edu>
// see http://www.zlib.org/
/**********************************************************************/
static FAST_FUNC uint32_t
lzo_adler32(uint32_t adler, const uint8_t* buf, unsigned len)
{
	enum {
		LZO_BASE = 65521, /* largest prime smaller than 65536 */
		/* NMAX is the largest n such that
		 * 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */
		LZO_NMAX = 5552,
	};
	uint32_t s1 = adler & 0xffff;
	uint32_t s2 = (adler >> 16) & 0xffff;
	unsigned k;

	if (buf == NULL)
		return 1;

	while (len > 0) {
		k = len < LZO_NMAX ? (unsigned) len : LZO_NMAX;
		len -= k;
		if (k != 0) do {
			s1 += *buf++;
			s2 += s1;
		} while (--k > 0);
		s1 %= LZO_BASE;
		s2 %= LZO_BASE;
	}
	return (s2 << 16) | s1;
}

static FAST_FUNC uint32_t
lzo_crc32(uint32_t c, const uint8_t* buf, unsigned len)
{
	//if (buf == NULL) - impossible
	//	return 0;

	return ~crc32_block_endian0(~c, buf, len, global_crc32_table);
}

/**********************************************************************/
static void init_chksum(void)
{
	G.chksum.f_adler32 = ADLER32_INIT_VALUE;
	G.chksum.f_crc32 = CRC32_INIT_VALUE;
}

static void add_bytes_to_chksum(const void* buf, int cnt)
{
	/* We need to handle the two checksums at once, because at the
	 * beginning of the header, we don't know yet which one we'll
	 * eventually need */
	G.chksum.f_adler32 = lzo_adler32(G.chksum.f_adler32, (const uint8_t*)buf, cnt);
	G.chksum.f_crc32 = lzo_crc32(G.chksum.f_crc32, (const uint8_t*)buf, cnt);
}

static uint32_t chksum_getresult(uint32_t h_flags32)
{
	return (h_flags32 & F_H_CRC32) ? G.chksum.f_crc32 : G.chksum.f_adler32;
}

/**********************************************************************/
static uint32_t read32(void)
{
	uint32_t v;
	xread(0, &v, 4);
	return ntohl(v);
}
static void f_read(void* buf, int cnt)
{
	xread(0, buf, cnt);
	add_bytes_to_chksum(buf, cnt);
}
//static int f_read8(void)
//{
//	uint8_t v;
//	f_read(&v, 1);
//	return v;
//}
//static unsigned f_read16(void)
//{
//	uint16_t v;
//	f_read(&v, 2);
//	return ntohs(v);
//}
static uint32_t f_read32(void)
{
	uint32_t v;
	f_read(&v, 4);
	return ntohl(v);
}

static void write32(uint32_t v)
{
	v = htonl(v);
	xwrite(1, &v, 4);
}
static void f_write(const void* buf, int cnt)
{
	xwrite(1, buf, cnt);
	add_bytes_to_chksum(buf, cnt);
}
//static void f_write8(uint8_t v)
//{
//	f_write(&v, 1);
//}
//static void f_write16(uint16_t v)
//{
//	v = htons(v);
//	f_write(&v, 2);
//}
//static void f_write32(uint32_t v)
//{
//	v = htonl(v);
//	f_write(&v, 4);
//}

/**********************************************************************/
#define LZO_BLOCK_SIZE	(256 * 1024l)
#define MAX_BLOCK_SIZE	(64 * 1024l * 1024l)	/* DO NOT CHANGE */

/* LZO may expand uncompressible data by a small amount */
#define MAX_COMPRESSED_SIZE(x)	((x) + (x) / 16 + 64 + 3)

/**********************************************************************/
// compress a file
/**********************************************************************/
static NOINLINE int lzo_compress(const header_t *h)
{
	unsigned block_size = LZO_BLOCK_SIZE;
	int r = 0; /* LZO_E_OK */
	uint8_t *const b1 = xzalloc(block_size);
	uint8_t *const b2 = xzalloc(MAX_COMPRESSED_SIZE(block_size));
	uint32_t d_adler32 = ADLER32_INIT_VALUE;
	uint32_t d_crc32 = CRC32_INIT_VALUE;
	uint8_t *wrk_mem = NULL;

	/* Only these methods are possible, see lzo_set_method():
	 * -1:    M_LZO1X_1_15
	 * -2..6: M_LZO1X_1
	 * -7..9: M_LZO1X_999 if ENABLE_LZOP_COMPR_HIGH
	 */
	if (h->method == M_LZO1X_1)
		wrk_mem = xzalloc(LZO1X_1_MEM_COMPRESS);
	else /* check only if it's not the only possibility */
		IF_LZOP_COMPR_HIGH(if (h->method == M_LZO1X_1_15))
			wrk_mem = xzalloc(LZO1X_1_15_MEM_COMPRESS);
#if ENABLE_LZOP_COMPR_HIGH
	else /* must be h->method == M_LZO1X_999 */
		wrk_mem = xzalloc(LZO1X_999_MEM_COMPRESS);
#endif

	for (;;) {
		unsigned src_len, dst_len;
		int l;
		uint32_t wordbuf[6];
		uint32_t *wordptr = wordbuf;

		/* read a block */
		l = full_read(0, b1, block_size);
		src_len = (l > 0 ? l : 0);

		/* write uncompressed block size */
		/* exit if last block */
		if (src_len == 0) {
			write32(0);
			break;
		}
		*wordptr++ = htonl(src_len);

		/* compute checksum of uncompressed block */
		if (h->flags32 & F_ADLER32_D)
			d_adler32 = lzo_adler32(ADLER32_INIT_VALUE, b1, src_len);
		if (h->flags32 & F_CRC32_D)
			d_crc32 = lzo_crc32(CRC32_INIT_VALUE, b1, src_len);

		/* compress */
		if (h->method == M_LZO1X_1)
			r = lzo1x_1_compress(b1, src_len, b2, &dst_len, wrk_mem);
		else IF_LZOP_COMPR_HIGH(if (h->method == M_LZO1X_1_15))
			r = lzo1x_1_15_compress(b1, src_len, b2, &dst_len, wrk_mem);
#if ENABLE_LZOP_COMPR_HIGH
		else /* must be h->method == M_LZO1X_999 */
			r = lzo1x_999_compress_level(b1, src_len, b2, &dst_len,
						wrk_mem, h->level);
#endif
		if (r != 0) /* not LZO_E_OK */
			bb_error_msg_and_die("%s: %s", "internal error", "compression");

		/* write compressed block size */
		if (dst_len < src_len) {
			/* optimize */
			if (h->method == M_LZO1X_999) {
				unsigned new_len = src_len;
				r = lzo1x_optimize(b2, dst_len, b1, &new_len /*, NULL*/);
				if (r != 0 /*LZO_E_OK*/ || new_len != src_len)
					bb_error_msg_and_die("%s: %s", "internal error", "optimization");
			}
			*wordptr++ = htonl(dst_len);
		} else {
			/* data actually expanded => store data uncompressed */
			*wordptr++ = htonl(src_len);
		}

		/* write checksum of uncompressed block */
		if (h->flags32 & F_ADLER32_D)
			*wordptr++ = htonl(d_adler32);
		if (h->flags32 & F_CRC32_D)
			*wordptr++ = htonl(d_crc32);

		if (dst_len < src_len) {
			/* write checksum of compressed block */
			if (h->flags32 & F_ADLER32_C)
				*wordptr++ = htonl(lzo_adler32(ADLER32_INIT_VALUE, b2, dst_len));
			if (h->flags32 & F_CRC32_C)
				*wordptr++ = htonl(lzo_crc32(CRC32_INIT_VALUE, b2, dst_len));
		}
		xwrite(1, wordbuf, ((char*)wordptr) - ((char*)wordbuf));
		if (dst_len < src_len) {
			/* write compressed block data */
			xwrite(1, b2, dst_len);
		} else {
			/* write uncompressed block data */
			xwrite(1, b1, src_len);
		}
		// /* if full_read() was nevertheless "short", it was EOF */
		// if (src_len < block_size)
		// 	break;
	}

	free(wrk_mem);
	free(b1);
	free(b2);
	return 1;
}

static FAST_FUNC void lzo_check(
		uint32_t init,
		uint8_t* buf, unsigned len,
		uint32_t FAST_FUNC (*fn)(uint32_t, const uint8_t*, unsigned),
		uint32_t ref)
{
	/* This function, by having the same order of parameters
	 * as fn, and by being marked FAST_FUNC (same as fn),
	 * saves a dozen bytes of code.
	 */
	uint32_t c = fn(init, buf, len);
	if (c != ref)
		bb_error_msg_and_die("checksum error");
}

/**********************************************************************/
// decompress a file
/**********************************************************************/
// used to have "const header_t *h" parameter, but since it uses
// only flags32 field, changed to receive only that.
static NOINLINE int lzo_decompress(uint32_t h_flags32)
{
	unsigned block_size = LZO_BLOCK_SIZE;
	int r;
	uint32_t src_len, dst_len;
	uint32_t c_adler32 = ADLER32_INIT_VALUE;
	uint32_t d_adler32 = ADLER32_INIT_VALUE;
	uint32_t c_crc32 = CRC32_INIT_VALUE, d_crc32 = CRC32_INIT_VALUE;
	uint8_t *b1;
	uint32_t mcs_block_size = MAX_COMPRESSED_SIZE(block_size);
	uint8_t *b2 = NULL;

	for (;;) {
		uint8_t *dst;

		/* read uncompressed block size */
		dst_len = read32();

		/* exit if last block */
		if (dst_len == 0)
			break;

		/* error if split file */
		if (dst_len == 0xffffffffL)
			/* should not happen - not yet implemented */
			bb_error_msg_and_die("this file is a split lzop file");

		if (dst_len > MAX_BLOCK_SIZE)
			bb_error_msg_and_die("corrupted data");

		/* read compressed block size */
		src_len = read32();
		if (src_len <= 0 || src_len > dst_len)
			bb_error_msg_and_die("corrupted data");

		if (dst_len > block_size) {
			if (b2) {
				free(b2);
				b2 = NULL;
			}
			block_size = dst_len;
			mcs_block_size = MAX_COMPRESSED_SIZE(block_size);
		}

		/* read checksum of uncompressed block */
		if (h_flags32 & F_ADLER32_D)
			d_adler32 = read32();
		if (h_flags32 & F_CRC32_D)
			d_crc32 = read32();

		/* read checksum of compressed block */
		if (src_len < dst_len) {
			if (h_flags32 & F_ADLER32_C)
				c_adler32 = read32();
			if (h_flags32 & F_CRC32_C)
				c_crc32 = read32();
		}

		if (b2 == NULL)
			b2 = xzalloc(mcs_block_size);
		/* read the block into the end of our buffer */
		b1 = b2 + mcs_block_size - src_len;
		xread(0, b1, src_len);

		if (src_len < dst_len) {
			unsigned d = dst_len;

			if (!(option_mask32 & OPT_F)) {
				/* verify checksum of compressed block */
				if (h_flags32 & F_ADLER32_C)
					lzo_check(ADLER32_INIT_VALUE,
							b1, src_len,
							lzo_adler32, c_adler32);
				if (h_flags32 & F_CRC32_C)
					lzo_check(CRC32_INIT_VALUE,
							b1, src_len,
							lzo_crc32, c_crc32);
			}

			/* decompress */
//			if (option_mask32 & OPT_F)
//				r = lzo1x_decompress(b1, src_len, b2, &d /*, NULL*/);
//			else
				r = lzo1x_decompress_safe(b1, src_len, b2, &d /*, NULL*/);

			if (r != 0 /*LZO_E_OK*/ || dst_len != d) {
				bb_error_msg_and_die("corrupted data");
			}
			dst = b2;
		} else {
			/* "stored" block => no decompression */
			dst = b1;
		}

		if (!(option_mask32 & OPT_F)) {
			/* verify checksum of uncompressed block */
			if (h_flags32 & F_ADLER32_D)
				lzo_check(ADLER32_INIT_VALUE,
					dst, dst_len,
					lzo_adler32, d_adler32);
			if (h_flags32 & F_CRC32_D)
				lzo_check(CRC32_INIT_VALUE,
					dst, dst_len,
					lzo_crc32, d_crc32);
		}

		/* write uncompressed block data */
		xwrite(1, dst, dst_len);
	}

	free(b2);
	return 1;
}

/**********************************************************************/
// lzop file signature (shamelessly borrowed from PNG)
/**********************************************************************/
/*
 * The first nine bytes of a lzop file always contain the following values:
 *
 *                                 0   1   2   3   4   5   6   7   8
 *                               --- --- --- --- --- --- --- --- ---
 * (hex)                          89  4c  5a  4f  00  0d  0a  1a  0a
 * (decimal)                     137  76  90  79   0  13  10  26  10
 * (C notation - ASCII)         \211   L   Z   O  \0  \r  \n \032 \n
 */

/* (vda) comparison with lzop v1.02rc1 ("lzop -1 <FILE" cmd):
 * Only slight differences in header:
 * -00000000  89 4c 5a 4f 00 0d 0a 1a 0a 10 20 20 20 09 40 02
 * +00000000  89 4c 5a 4f 00 0d 0a 1a 0a 10 10 20 30 09 40 02
 *                                       ^^^^^ ^^^^^
 *                                     version lib_version
 * -00000010  01 03 00 00 0d 00 00 81 a4 49 f7 a6 3f 00 00 00
 * +00000010  01 03 00 00 01 00 00 00 00 00 00 00 00 00 00 00
 *               ^^^^^^^^^^^ ^^^^^^^^^^^ ^^^^^^^^^^^
 *               flags       mode        mtime
 * -00000020  00 00 2d 67 04 17 00 04 00 00 00 03 ed ec 9d 6d
 * +00000020  00 00 10 5f 00 c1 00 04 00 00 00 03 ed ec 9d 6d
 *                  ^^^^^^^^^^^
 *                  chksum
 * The rest is identical.
*/
static const unsigned char lzop_magic[9] ALIGN1 = {
	0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a
};

/* This coding is derived from Alexander Lehmann's pngcheck code. */
static void check_magic(void)
{
	unsigned char magic[sizeof(lzop_magic)];
	xread(0, magic, sizeof(magic));
	if (memcmp(magic, lzop_magic, sizeof(lzop_magic)) != 0)
		bb_error_msg_and_die("bad magic number");
}

/**********************************************************************/
// lzop file header
/**********************************************************************/
static void write_header(header_t *h)
{
	char *end;

	xwrite(1, lzop_magic, sizeof(lzop_magic));

	init_chksum();

	/* Our caller leaves name zero-filled, so len == 0 */
	end = h->len_and_name+1 + 0; /* 0 is strlen(h->len_and_name+1) */
	/* Store length byte */
	/*h->len_and_name[0] = end - (h->len_and_name+1); - zero already */

	f_write(&h->version_be16, end - (char*)&h->version_be16);

	h->flags32 = htonl(h->flags32); /* native endianness for lzo_compress() */

	write32(chksum_getresult(h->flags32));
}

static int read_header(header_t *h)
{
	int l;
	uint32_t checksum;
	/* As it stands now, only h->flags32 is used by our caller.
	 * Therefore we don't store many fields in h->FIELD.
	 */
	unsigned h_version;
	unsigned h_version_needed_to_extract;

	init_chksum();

	/* We don't support versions < 0.94, since 0.94
	 * came only 2 months after 0.90:
	 * 0.90 (10 Aug 1997): First public release of lzop
	 * 0.94 (15 Oct 1997): Header format change
	 */

	/* Read up to and including name length byte */
	f_read(&h->version_be16, ((char*)&h->len_and_name[1]) - ((char*)&h->version_be16));

	h_version = htons(h->version_be16);
	if (h_version < 0x0940)
		return 3;
	h_version_needed_to_extract = htons(h->version_needed_to_extract_be16);
	if (h_version_needed_to_extract > LZOP_VERSION)
		return 16;
	if (h_version_needed_to_extract < 0x0940)
		return 3;

	if (h->method <= 0)
		return 14;

	/* former lzo_get_method(h): */
	if (h->method == M_LZO1X_1) {
		if (h->level == 0)
			h->level = 3;
	} else if (h->method == M_LZO1X_1_15) {
		if (h->level == 0)
			h->level = 1;
	} else if (h->method == M_LZO1X_999) {
		if (h->level == 0)
			h->level = 9;
	} else
		return -1; /* not a LZO method */
	/* check compression level */
	if (h->level < 1 || h->level > 9)
		return 15;

	h->flags32 = ntohl(h->flags32);
	if (h->flags32 & F_H_FILTER)
		return 16; /* filter not supported */
	/* check reserved flags */
	if (h->flags32 & F_RESERVED)
		return -13;

	l = h->len_and_name[0];
	if (l > 0)
		/* UNUSED */ f_read(h->len_and_name+1, l);
	/* UNUSED h->len_and_name[1+l] = 0; */

	checksum = chksum_getresult(h->flags32);
	if (read32() != checksum)
		return 2;

	/* skip extra field [not used yet] */
	if (h->flags32 & F_H_EXTRA_FIELD) {
		uint32_t extra_field_len;
		uint32_t extra_field_checksum;
		uint32_t k;
		char dummy;

		/* note: the checksum also covers the length */
		init_chksum();
		extra_field_len = f_read32();
		for (k = 0; k < extra_field_len; k++)
			f_read(&dummy, 1);
		checksum = chksum_getresult(h->flags32);
		extra_field_checksum = read32();
		if (extra_field_checksum != checksum)
			return 3;
	}

	return 0;
}

/**********************************************************************/
// compress
/**********************************************************************/
static void lzo_set_method(header_t *h)
{
	smallint level;

	/* levels 2..6 or none (defaults to level 3) */
	h->method = M_LZO1X_1;
	level = 5; /* levels 2-6 are actually the same */

	if (option_mask32 & OPT_1) {
		h->method = M_LZO1X_1_15;
		level = 1;
	}
	if (option_mask32 & OPT_789) {
#if ENABLE_LZOP_COMPR_HIGH
		h->method = M_LZO1X_999;
		level = 9;
		if (option_mask32 & OPT_7)
			level = 7;
		else if (option_mask32 & OPT_8)
			level = 8;
#else
		bb_error_msg_and_die("high compression not compiled in");
#endif
	}

	h->level = level;
}

static int do_lzo_compress(void)
{
	header_t header;

#define h (&header)
	memset(h, 0, sizeof(*h));

	lzo_set_method(h);

	h->version_be16 = htons(LZOP_VERSION & 0xffff);
	h->version_needed_to_extract_be16 = htons(0x0940);
	h->lib_version_be16 = htons(lzo_version() & 0xffff);

	h->flags32 = htonl((F_OS & F_OS_MASK) | (F_CS & F_CS_MASK));

	if (!(option_mask32 & OPT_F) || h->method == M_LZO1X_999) {
		h->flags32 |= htonl(F_ADLER32_D);
		if (option_mask32 & OPT_C)
			h->flags32 |= htonl(F_ADLER32_C);
	}

	/* write_header() also converts h->flags32 to native endianness */
	write_header(h);

	return lzo_compress(h);
#undef h
}

/**********************************************************************/
// decompress
/**********************************************************************/
static int do_lzo_decompress(void)
{
	int r;
	header_t header;

	check_magic();
	r = read_header(&header);
	if (r != 0)
		bb_error_msg_and_die("header_error %d", r);
	return lzo_decompress(header.flags32);
}

static char* FAST_FUNC make_new_name_lzop(char *filename, const char *expected_ext UNUSED_PARAM)
{
	if (option_mask32 & OPT_DECOMPRESS) {
		char *extension = strrchr(filename, '.');
		if (!extension || strcmp(extension + 1, "lzo") != 0)
			return xasprintf("%s.out", filename);
		*extension = '\0';
		return filename;
	}
	return xasprintf("%s.lzo", filename);
}

static IF_DESKTOP(long long) int FAST_FUNC pack_lzop(transformer_state_t *xstate UNUSED_PARAM)
{
	if (option_mask32 & OPT_DECOMPRESS)
		return do_lzo_decompress();
	return do_lzo_compress();
}

int lzop_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lzop_main(int argc UNUSED_PARAM, char **argv)
{
	INIT_G();

	getopt32(argv, OPTION_STRING);
	argv += optind;
	/* -U is "anti -k", invert bit for bbunpack(): */
	option_mask32 ^= OPT_KEEP;
	/* -k disables -U (if any): */
	/* opt_complementary "k-U"? - nope, only handles -Uk, not -kU */
	if (option_mask32 & OPT_k)
		option_mask32 |= OPT_KEEP;

	/* lzopcat? */
	if (ENABLE_LZOPCAT && applet_name[4] == 'c')
		option_mask32 |= (OPT_STDOUT | OPT_DECOMPRESS);
	/* unlzop? */
	if (ENABLE_UNLZOP && applet_name[4] == 'o')
		option_mask32 |= OPT_DECOMPRESS;

	global_crc32_new_table_le();
	return bbunpack(argv, pack_lzop, make_new_name_lzop, /*unused:*/ NULL);
}
