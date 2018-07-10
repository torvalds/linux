/* vi: set sw=4 ts=4: */
/*
 * gunzip implementation for busybox
 *
 * Based on GNU gzip v1.2.4 Copyright (C) 1992-1993 Jean-loup Gailly.
 *
 * Originally adjusted for busybox by Sven Rudolph <sr1@inf.tu-dresden.de>
 * based on gzip sources
 *
 * Adjusted further by Erik Andersen <andersen@codepoet.org> to support
 * files as well as stdin/stdout, and to generally behave itself wrt
 * command line handling.
 *
 * General cleanup to better adhere to the style guide and make use of standard
 * busybox functions by Glenn McGrath
 *
 * read_gz interface + associated hacking by Laurence Anderson
 *
 * Fixed huft_build() so decoding end-of-block code does not grab more bits
 * than necessary (this is required by unzip applet), added inflate_cleanup()
 * to free leaked bytebuffer memory (used in unzip.c), and some minor style
 * guide cleanups by Ed Clark
 *
 * gzip (GNU zip) -- compress files with zip algorithm and 'compress' interface
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * The unzip code was written and put in the public domain by Mark Adler.
 * Portions of the lzw code are derived from the public domain 'compress'
 * written by Spencer Thomas, Joe Orost, James Woods, Jim McKie, Steve Davies,
 * Ken Turkowski, Dave Mack and Peter Jannesen.
 *
 * See the file algorithm.doc for the compression algorithms and file formats.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

typedef struct huft_t {
	unsigned char e;	/* number of extra bits or operation */
	unsigned char b;	/* number of bits in this code or subcode */
	union {
		unsigned short n;	/* literal, length base, or distance base */
		struct huft_t *t;	/* pointer to next level of table */
	} v;
} huft_t;

enum {
	/* gunzip_window size--must be a power of two, and
	 * at least 32K for zip's deflate method */
	GUNZIP_WSIZE = 0x8000,
	/* If BMAX needs to be larger than 16, then h and x[] should be ulg. */
	BMAX = 16,	/* maximum bit length of any code (16 for explode) */
	N_MAX = 288,	/* maximum number of codes in any set */
};


/* This is somewhat complex-looking arrangement, but it allows
 * to place decompressor state either in bss or in
 * malloc'ed space simply by changing #defines below.
 * Sizes on i386:
 * text    data     bss     dec     hex
 * 5256       0     108    5364    14f4 - bss
 * 4915       0       0    4915    1333 - malloc
 */
#define STATE_IN_BSS 0
#define STATE_IN_MALLOC 1


typedef struct state_t {
	off_t gunzip_bytes_out; /* number of output bytes */
	uint32_t gunzip_crc;

	int gunzip_src_fd;
	unsigned gunzip_outbuf_count; /* bytes in output buffer */

	unsigned char *gunzip_window;

	uint32_t *gunzip_crc_table;

	/* bitbuffer */
	unsigned gunzip_bb; /* bit buffer */
	unsigned char gunzip_bk; /* bits in bit buffer */

	/* input (compressed) data */
	unsigned char *bytebuffer;      /* buffer itself */
	off_t to_read;			/* compressed bytes to read (unzip only, -1 for gunzip) */
//	unsigned bytebuffer_max;        /* buffer size */
	unsigned bytebuffer_offset;     /* buffer position */
	unsigned bytebuffer_size;       /* how much data is there (size <= max) */

	/* private data of inflate_codes() */
	unsigned inflate_codes_ml; /* masks for bl and bd bits */
	unsigned inflate_codes_md; /* masks for bl and bd bits */
	unsigned inflate_codes_bb; /* bit buffer */
	unsigned inflate_codes_k; /* number of bits in bit buffer */
	unsigned inflate_codes_w; /* current gunzip_window position */
	huft_t *inflate_codes_tl;
	huft_t *inflate_codes_td;
	unsigned inflate_codes_bl;
	unsigned inflate_codes_bd;
	unsigned inflate_codes_nn; /* length and index for copy */
	unsigned inflate_codes_dd;

	smallint resume_copy;

	/* private data of inflate_get_next_window() */
	smallint method; /* method == -1 for stored, -2 for codes */
	smallint need_another_block;
	smallint end_reached;

	/* private data of inflate_stored() */
	unsigned inflate_stored_n;
	unsigned inflate_stored_b;
	unsigned inflate_stored_k;
	unsigned inflate_stored_w;

	const char *error_msg;
	jmp_buf error_jmp;
} state_t;
#define gunzip_bytes_out    (S()gunzip_bytes_out   )
#define gunzip_crc          (S()gunzip_crc         )
#define gunzip_src_fd       (S()gunzip_src_fd      )
#define gunzip_outbuf_count (S()gunzip_outbuf_count)
#define gunzip_window       (S()gunzip_window      )
#define gunzip_crc_table    (S()gunzip_crc_table   )
#define gunzip_bb           (S()gunzip_bb          )
#define gunzip_bk           (S()gunzip_bk          )
#define to_read             (S()to_read            )
// #define bytebuffer_max   (S()bytebuffer_max     )
// Both gunzip and unzip can use constant buffer size now (16k):
#define bytebuffer_max      0x4000
#define bytebuffer          (S()bytebuffer         )
#define bytebuffer_offset   (S()bytebuffer_offset  )
#define bytebuffer_size     (S()bytebuffer_size    )
#define inflate_codes_ml    (S()inflate_codes_ml   )
#define inflate_codes_md    (S()inflate_codes_md   )
#define inflate_codes_bb    (S()inflate_codes_bb   )
#define inflate_codes_k     (S()inflate_codes_k    )
#define inflate_codes_w     (S()inflate_codes_w    )
#define inflate_codes_tl    (S()inflate_codes_tl   )
#define inflate_codes_td    (S()inflate_codes_td   )
#define inflate_codes_bl    (S()inflate_codes_bl   )
#define inflate_codes_bd    (S()inflate_codes_bd   )
#define inflate_codes_nn    (S()inflate_codes_nn   )
#define inflate_codes_dd    (S()inflate_codes_dd   )
#define resume_copy         (S()resume_copy        )
#define method              (S()method             )
#define need_another_block  (S()need_another_block )
#define end_reached         (S()end_reached        )
#define inflate_stored_n    (S()inflate_stored_n   )
#define inflate_stored_b    (S()inflate_stored_b   )
#define inflate_stored_k    (S()inflate_stored_k   )
#define inflate_stored_w    (S()inflate_stored_w   )
#define error_msg           (S()error_msg          )
#define error_jmp           (S()error_jmp          )

/* This is a generic part */
#if STATE_IN_BSS /* Use global data segment */
#define DECLARE_STATE /*nothing*/
#define ALLOC_STATE /*nothing*/
#define DEALLOC_STATE ((void)0)
#define S() state.
#define PASS_STATE /*nothing*/
#define PASS_STATE_ONLY /*nothing*/
#define STATE_PARAM /*nothing*/
#define STATE_PARAM_ONLY void
static state_t state;
#endif

#if STATE_IN_MALLOC /* Use malloc space */
#define DECLARE_STATE state_t *state
#define ALLOC_STATE (state = xzalloc(sizeof(*state)))
#define DEALLOC_STATE free(state)
#define S() state->
#define PASS_STATE state,
#define PASS_STATE_ONLY state
#define STATE_PARAM state_t *state,
#define STATE_PARAM_ONLY state_t *state
#endif


static const uint16_t mask_bits[] ALIGN2 = {
	0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
	0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

/* Copy lengths for literal codes 257..285 */
static const uint16_t cplens[] ALIGN2 = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};

/* note: see note #13 above about the 258 in this list. */
/* Extra bits for literal codes 257..285 */
static const uint8_t cplext[] ALIGN1 = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5,
	5, 5, 5, 0, 99, 99
}; /* 99 == invalid */

/* Copy offsets for distance codes 0..29 */
static const uint16_t cpdist[] ALIGN2 = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
	769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};

/* Extra bits for distance codes */
static const uint8_t cpdext[] ALIGN1 = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
	11, 11, 12, 12, 13, 13
};

/* Tables for deflate from PKZIP's appnote.txt. */
/* Order of the bit length code lengths */
static const uint8_t border[] ALIGN1 = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};


/*
 * Free the malloc'ed tables built by huft_build(), which makes a linked
 * list of the tables it made, with the links in a dummy first entry of
 * each table.
 * t: table to free
 */
static void huft_free(huft_t *p)
{
	huft_t *q;

	/* Go through linked list, freeing from the malloced (t[-1]) address. */
	while (p) {
		q = (--p)->v.t;
		free(p);
		p = q;
	}
}

static void huft_free_all(STATE_PARAM_ONLY)
{
	huft_free(inflate_codes_tl);
	huft_free(inflate_codes_td);
	inflate_codes_tl = NULL;
	inflate_codes_td = NULL;
}

static void abort_unzip(STATE_PARAM_ONLY) NORETURN;
static void abort_unzip(STATE_PARAM_ONLY)
{
	huft_free_all(PASS_STATE_ONLY);
	longjmp(error_jmp, 1);
}

static unsigned fill_bitbuffer(STATE_PARAM unsigned bitbuffer, unsigned *current, const unsigned required)
{
	while (*current < required) {
		if (bytebuffer_offset >= bytebuffer_size) {
			unsigned sz = bytebuffer_max - 4;
			if (to_read >= 0 && to_read < sz) /* unzip only */
				sz = to_read;
			/* Leave the first 4 bytes empty so we can always unwind the bitbuffer
			 * to the front of the bytebuffer */
			bytebuffer_size = safe_read(gunzip_src_fd, &bytebuffer[4], sz);
			if ((int)bytebuffer_size < 1) {
				error_msg = "unexpected end of file";
				abort_unzip(PASS_STATE_ONLY);
			}
			if (to_read >= 0) /* unzip only */
				to_read -= bytebuffer_size;
			bytebuffer_size += 4;
			bytebuffer_offset = 4;
		}
		bitbuffer |= ((unsigned) bytebuffer[bytebuffer_offset]) << *current;
		bytebuffer_offset++;
		*current += 8;
	}
	return bitbuffer;
}


/* Given a list of code lengths and a maximum table size, make a set of
 * tables to decode that set of codes.  Return zero on success, one if
 * the given code set is incomplete (the tables are still built in this
 * case), two if the input is invalid (an oversubscribed set of lengths)
 * - in this case stores NULL in *t.
 *
 * b:	code lengths in bits (all assumed <= BMAX)
 * n:	number of codes (assumed <= N_MAX)
 * s:	number of simple-valued codes (0..s-1)
 * d:	list of base values for non-simple codes
 * e:	list of extra bits for non-simple codes
 * t:	result: starting table
 * m:	maximum lookup bits, returns actual
 */
static int huft_build(const unsigned *b, const unsigned n,
			const unsigned s, const unsigned short *d,
			const unsigned char *e, huft_t **t, unsigned *m)
{
	unsigned a;             /* counter for codes of length k */
	unsigned c[BMAX + 1];   /* bit length count table */
	unsigned eob_len;       /* length of end-of-block code (value 256) */
	unsigned f;             /* i repeats in table every f entries */
	int g;                  /* maximum code length */
	int htl;                /* table level */
	unsigned i;             /* counter, current code */
	unsigned j;             /* counter */
	int k;                  /* number of bits in current code */
	const unsigned *p;      /* pointer into c[], b[], or v[] */
	huft_t *q;              /* points to current table */
	huft_t r;               /* table entry for structure assignment */
	huft_t *u[BMAX];        /* table stack */
	unsigned v[N_MAX + 1];  /* values in order of bit length. last v[] is never used */
	int ws[BMAX + 1];       /* bits decoded stack */
	int w;                  /* bits decoded */
	unsigned x[BMAX + 1];   /* bit offsets, then code stack */
	unsigned *xp;           /* pointer into x */
	int y;                  /* number of dummy codes added */
	unsigned z;             /* number of entries in current table */

	/* Length of EOB code, if any */
	eob_len = n > 256 ? b[256] : BMAX;

	*t = NULL;

	/* Generate counts for each bit length */
	memset(c, 0, sizeof(c));
	p = b;
	i = n;
	do {
		c[*p]++; /* assume all entries <= BMAX */
		p++;     /* can't combine with above line (Solaris bug) */
	} while (--i);
	if (c[0] == n) {  /* null input - all zero length codes */
		q = xzalloc(3 * sizeof(*q));
		//q[0].v.t = NULL;
		q[1].e = 99;    /* invalid code marker */
		q[1].b = 1;
		q[2].e = 99;    /* invalid code marker */
		q[2].b = 1;
		*t = q + 1;
		*m = 1;
		return 0;
	}

	/* Find minimum and maximum length, bound *m by those */
	for (j = 1; (j <= BMAX) && (c[j] == 0); j++)
		continue;
	k = j; /* minimum code length */
	for (i = BMAX; (c[i] == 0) && i; i--)
		continue;
	g = i; /* maximum code length */
	*m = (*m < j) ? j : ((*m > i) ? i : *m);

	/* Adjust last length count to fill out codes, if needed */
	for (y = 1 << j; j < i; j++, y <<= 1) {
		y -= c[j];
		if (y < 0)
			return 2; /* bad input: more codes than bits */
	}
	y -= c[i];
	if (y < 0)
		return 2;
	c[i] += y;

	/* Generate starting offsets into the value table for each length */
	x[1] = j = 0;
	p = c + 1;
	xp = x + 2;
	while (--i) { /* note that i == g from above */
		j += *p++;
		*xp++ = j;
	}

	/* Make a table of values in order of bit lengths.
	 * To detect bad input, unused v[i]'s are set to invalid value UINT_MAX.
	 * In particular, last v[i] is never filled and must not be accessed.
	 */
	memset(v, 0xff, sizeof(v));
	p = b;
	i = 0;
	do {
		j = *p++;
		if (j != 0) {
			v[x[j]++] = i;
		}
	} while (++i < n);

	/* Generate the Huffman codes and for each, make the table entries */
	x[0] = i = 0;   /* first Huffman code is zero */
	p = v;          /* grab values in bit order */
	htl = -1;       /* no tables yet--level -1 */
	w = ws[0] = 0;  /* bits decoded */
	u[0] = NULL;    /* just to keep compilers happy */
	q = NULL;       /* ditto */
	z = 0;          /* ditto */

	/* go through the bit lengths (k already is bits in shortest code) */
	for (; k <= g; k++) {
		a = c[k];
		while (a--) {
			/* here i is the Huffman code of length k bits for value *p */
			/* make tables up to required level */
			while (k > ws[htl + 1]) {
				w = ws[++htl];

				/* compute minimum size table less than or equal to *m bits */
				z = g - w;
				z = z > *m ? *m : z; /* upper limit on table size */
				j = k - w;
				f = 1 << j;
				if (f > a + 1) { /* try a k-w bit table */
					/* too few codes for k-w bit table */
					f -= a + 1; /* deduct codes from patterns left */
					xp = c + k;
					while (++j < z) { /* try smaller tables up to z bits */
						f <<= 1;
						if (f <= *++xp) {
							break; /* enough codes to use up j bits */
						}
						f -= *xp; /* else deduct codes from patterns */
					}
				}
				j = (w + j > eob_len && w < eob_len) ? eob_len - w : j;	/* make EOB code end at table */
				z = 1 << j;	/* table entries for j-bit table */
				ws[htl+1] = w + j;	/* set bits decoded in stack */

				/* allocate and link in new table */
				q = xzalloc((z + 1) * sizeof(huft_t));
				*t = q + 1;	/* link to list for huft_free() */
				t = &(q->v.t);
				u[htl] = ++q;	/* table starts after link */

				/* connect to last table, if there is one */
				if (htl) {
					x[htl] = i; /* save pattern for backing up */
					r.b = (unsigned char) (w - ws[htl - 1]); /* bits to dump before this table */
					r.e = (unsigned char) (16 + j); /* bits in this table */
					r.v.t = q; /* pointer to this table */
					j = (i & ((1 << w) - 1)) >> ws[htl - 1];
					u[htl - 1][j] = r; /* connect to last table */
				}
			}

			/* set up table entry in r */
			r.b = (unsigned char) (k - w);
			if (/*p >= v + n || -- redundant, caught by the second check: */
			    *p == UINT_MAX /* do we access uninited v[i]? (see memset(v))*/
			) {
				r.e = 99; /* out of values--invalid code */
			} else if (*p < s) {
				r.e = (unsigned char) (*p < 256 ? 16 : 15);	/* 256 is EOB code */
				r.v.n = (unsigned short) (*p++); /* simple code is just the value */
			} else {
				r.e = (unsigned char) e[*p - s]; /* non-simple--look up in lists */
				r.v.n = d[*p++ - s];
			}

			/* fill code-like entries with r */
			f = 1 << (k - w);
			for (j = i >> w; j < z; j += f) {
				q[j] = r;
			}

			/* backwards increment the k-bit code i */
			for (j = 1 << (k - 1); i & j; j >>= 1) {
				i ^= j;
			}
			i ^= j;

			/* backup over finished tables */
			while ((i & ((1 << w) - 1)) != x[htl]) {
				w = ws[--htl];
			}
		}
	}

	/* return actual size of base table */
	*m = ws[1];

	/* Return 1 if we were given an incomplete table */
	return y != 0 && g != 1;
}


/*
 * inflate (decompress) the codes in a deflated (compressed) block.
 * Return an error code or zero if it all goes ok.
 *
 * tl, td: literal/length and distance decoder tables
 * bl, bd: number of bits decoded by tl[] and td[]
 */
/* called once from inflate_block */

/* map formerly local static variables to globals */
#define ml inflate_codes_ml
#define md inflate_codes_md
#define bb inflate_codes_bb
#define k  inflate_codes_k
#define w  inflate_codes_w
#define tl inflate_codes_tl
#define td inflate_codes_td
#define bl inflate_codes_bl
#define bd inflate_codes_bd
#define nn inflate_codes_nn
#define dd inflate_codes_dd
static void inflate_codes_setup(STATE_PARAM unsigned my_bl, unsigned my_bd)
{
	bl = my_bl;
	bd = my_bd;
	/* make local copies of globals */
	bb = gunzip_bb;			/* initialize bit buffer */
	k = gunzip_bk;
	w = gunzip_outbuf_count;	/* initialize gunzip_window position */
	/* inflate the coded data */
	ml = mask_bits[bl];		/* precompute masks for speed */
	md = mask_bits[bd];
}
/* called once from inflate_get_next_window */
static NOINLINE int inflate_codes(STATE_PARAM_ONLY)
{
	unsigned e;	/* table entry flag/number of extra bits */
	huft_t *t;	/* pointer to table entry */

	if (resume_copy)
		goto do_copy;

	while (1) {			/* do until end of block */
		bb = fill_bitbuffer(PASS_STATE bb, &k, bl);
		t = tl + ((unsigned) bb & ml);
		e = t->e;
		if (e > 16)
			do {
				if (e == 99) {
					abort_unzip(PASS_STATE_ONLY);
				}
				bb >>= t->b;
				k -= t->b;
				e -= 16;
				bb = fill_bitbuffer(PASS_STATE bb, &k, e);
				t = t->v.t + ((unsigned) bb & mask_bits[e]);
				e = t->e;
			} while (e > 16);
		bb >>= t->b;
		k -= t->b;
		if (e == 16) {	/* then it's a literal */
			gunzip_window[w++] = (unsigned char) t->v.n;
			if (w == GUNZIP_WSIZE) {
				gunzip_outbuf_count = w;
				//flush_gunzip_window();
				w = 0;
				return 1; // We have a block to read
			}
		} else {		/* it's an EOB or a length */
			/* exit if end of block */
			if (e == 15) {
				break;
			}

			/* get length of block to copy */
			bb = fill_bitbuffer(PASS_STATE bb, &k, e);
			nn = t->v.n + ((unsigned) bb & mask_bits[e]);
			bb >>= e;
			k -= e;

			/* decode distance of block to copy */
			bb = fill_bitbuffer(PASS_STATE bb, &k, bd);
			t = td + ((unsigned) bb & md);
			e = t->e;
			if (e > 16)
				do {
					if (e == 99) {
						abort_unzip(PASS_STATE_ONLY);
					}
					bb >>= t->b;
					k -= t->b;
					e -= 16;
					bb = fill_bitbuffer(PASS_STATE bb, &k, e);
					t = t->v.t + ((unsigned) bb & mask_bits[e]);
					e = t->e;
				} while (e > 16);
			bb >>= t->b;
			k -= t->b;
			bb = fill_bitbuffer(PASS_STATE bb, &k, e);
			dd = w - t->v.n - ((unsigned) bb & mask_bits[e]);
			bb >>= e;
			k -= e;

			/* do the copy */
 do_copy:
			do {
				/* Was: nn -= (e = (e = GUNZIP_WSIZE - ((dd &= GUNZIP_WSIZE - 1) > w ? dd : w)) > nn ? nn : e); */
				/* Who wrote THAT?? rewritten as: */
				unsigned delta;

				dd &= GUNZIP_WSIZE - 1;
				e = GUNZIP_WSIZE - (dd > w ? dd : w);
				delta = w > dd ? w - dd : dd - w;
				if (e > nn) e = nn;
				nn -= e;

				/* copy to new buffer to prevent possible overwrite */
				if (delta >= e) {
					memcpy(gunzip_window + w, gunzip_window + dd, e);
					w += e;
					dd += e;
				} else {
					/* do it slow to avoid memcpy() overlap */
					/* !NOMEMCPY */
					do {
						gunzip_window[w++] = gunzip_window[dd++];
					} while (--e);
				}
				if (w == GUNZIP_WSIZE) {
					gunzip_outbuf_count = w;
					resume_copy = (nn != 0);
					//flush_gunzip_window();
					w = 0;
					return 1;
				}
			} while (nn);
			resume_copy = 0;
		}
	}

	/* restore the globals from the locals */
	gunzip_outbuf_count = w;	/* restore global gunzip_window pointer */
	gunzip_bb = bb;			/* restore global bit buffer */
	gunzip_bk = k;

	/* normally just after call to inflate_codes, but save code by putting it here */
	/* free the decoding tables (tl and td), return */
	huft_free_all(PASS_STATE_ONLY);

	/* done */
	return 0;
}
#undef ml
#undef md
#undef bb
#undef k
#undef w
#undef tl
#undef td
#undef bl
#undef bd
#undef nn
#undef dd


/* called once from inflate_block */
static void inflate_stored_setup(STATE_PARAM int my_n, int my_b, int my_k)
{
	inflate_stored_n = my_n;
	inflate_stored_b = my_b;
	inflate_stored_k = my_k;
	/* initialize gunzip_window position */
	inflate_stored_w = gunzip_outbuf_count;
}
/* called once from inflate_get_next_window */
static int inflate_stored(STATE_PARAM_ONLY)
{
	/* read and output the compressed data */
	while (inflate_stored_n--) {
		inflate_stored_b = fill_bitbuffer(PASS_STATE inflate_stored_b, &inflate_stored_k, 8);
		gunzip_window[inflate_stored_w++] = (unsigned char) inflate_stored_b;
		if (inflate_stored_w == GUNZIP_WSIZE) {
			gunzip_outbuf_count = inflate_stored_w;
			//flush_gunzip_window();
			inflate_stored_w = 0;
			inflate_stored_b >>= 8;
			inflate_stored_k -= 8;
			return 1; /* We have a block */
		}
		inflate_stored_b >>= 8;
		inflate_stored_k -= 8;
	}

	/* restore the globals from the locals */
	gunzip_outbuf_count = inflate_stored_w;		/* restore global gunzip_window pointer */
	gunzip_bb = inflate_stored_b;	/* restore global bit buffer */
	gunzip_bk = inflate_stored_k;
	return 0; /* Finished */
}


/*
 * decompress an inflated block
 * e: last block flag
 *
 * GLOBAL VARIABLES: bb, kk,
 */
/* Return values: -1 = inflate_stored, -2 = inflate_codes */
/* One callsite in inflate_get_next_window */
static int inflate_block(STATE_PARAM smallint *e)
{
	unsigned ll[286 + 30];  /* literal/length and distance code lengths */
	unsigned t;     /* block type */
	unsigned b;     /* bit buffer */
	unsigned k;     /* number of bits in bit buffer */

	/* make local bit buffer */

	b = gunzip_bb;
	k = gunzip_bk;

	/* read in last block bit */
	b = fill_bitbuffer(PASS_STATE b, &k, 1);
	*e = b & 1;
	b >>= 1;
	k -= 1;

	/* read in block type */
	b = fill_bitbuffer(PASS_STATE b, &k, 2);
	t = (unsigned) b & 3;
	b >>= 2;
	k -= 2;

	/* restore the global bit buffer */
	gunzip_bb = b;
	gunzip_bk = k;

	/* Do we see block type 1 often? Yes!
	 * TODO: fix performance problem (see below) */
	//bb_error_msg("blktype %d", t);

	/* inflate that block type */
	switch (t) {
	case 0: /* Inflate stored */
	{
		unsigned n;	/* number of bytes in block */
		unsigned b_stored;	/* bit buffer */
		unsigned k_stored;	/* number of bits in bit buffer */

		/* make local copies of globals */
		b_stored = gunzip_bb;	/* initialize bit buffer */
		k_stored = gunzip_bk;

		/* go to byte boundary */
		n = k_stored & 7;
		b_stored >>= n;
		k_stored -= n;

		/* get the length and its complement */
		b_stored = fill_bitbuffer(PASS_STATE b_stored, &k_stored, 16);
		n = ((unsigned) b_stored & 0xffff);
		b_stored >>= 16;
		k_stored -= 16;

		b_stored = fill_bitbuffer(PASS_STATE b_stored, &k_stored, 16);
		if (n != (unsigned) ((~b_stored) & 0xffff)) {
			abort_unzip(PASS_STATE_ONLY);	/* error in compressed data */
		}
		b_stored >>= 16;
		k_stored -= 16;

		inflate_stored_setup(PASS_STATE n, b_stored, k_stored);

		return -1;
	}
	case 1:
	/* Inflate fixed
	 * decompress an inflated type 1 (fixed Huffman codes) block. We should
	 * either replace this with a custom decoder, or at least precompute the
	 * Huffman tables. TODO */
	{
		int i;                  /* temporary variable */
		unsigned bl;            /* lookup bits for tl */
		unsigned bd;            /* lookup bits for td */
		/* gcc 4.2.1 is too dumb to reuse stackspace. Moved up... */
		//unsigned ll[288];     /* length list for huft_build */

		/* set up literal table */
		for (i = 0; i < 144; i++)
			ll[i] = 8;
		for (; i < 256; i++)
			ll[i] = 9;
		for (; i < 280; i++)
			ll[i] = 7;
		for (; i < 288; i++) /* make a complete, but wrong code set */
			ll[i] = 8;
		bl = 7;
		huft_build(ll, 288, 257, cplens, cplext, &inflate_codes_tl, &bl);
		/* huft_build() never return nonzero - we use known data */

		/* set up distance table */
		for (i = 0; i < 30; i++) /* make an incomplete code set */
			ll[i] = 5;
		bd = 5;
		huft_build(ll, 30, 0, cpdist, cpdext, &inflate_codes_td, &bd);

		/* set up data for inflate_codes() */
		inflate_codes_setup(PASS_STATE bl, bd);

		/* huft_free code moved into inflate_codes */

		return -2;
	}
	case 2: /* Inflate dynamic */
	{
		enum { dbits = 6 };     /* bits in base distance lookup table */
		enum { lbits = 9 };     /* bits in base literal/length lookup table */

		huft_t *td;             /* distance code table */
		unsigned i;             /* temporary variables */
		unsigned j;
		unsigned l;             /* last length */
		unsigned m;             /* mask for bit lengths table */
		unsigned n;             /* number of lengths to get */
		unsigned bl;            /* lookup bits for tl */
		unsigned bd;            /* lookup bits for td */
		unsigned nb;            /* number of bit length codes */
		unsigned nl;            /* number of literal/length codes */
		unsigned nd;            /* number of distance codes */

		//unsigned ll[286 + 30];/* literal/length and distance code lengths */
		unsigned b_dynamic;     /* bit buffer */
		unsigned k_dynamic;     /* number of bits in bit buffer */

		/* make local bit buffer */
		b_dynamic = gunzip_bb;
		k_dynamic = gunzip_bk;

		/* read in table lengths */
		b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 5);
		nl = 257 + ((unsigned) b_dynamic & 0x1f);	/* number of literal/length codes */

		b_dynamic >>= 5;
		k_dynamic -= 5;
		b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 5);
		nd = 1 + ((unsigned) b_dynamic & 0x1f);	/* number of distance codes */

		b_dynamic >>= 5;
		k_dynamic -= 5;
		b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 4);
		nb = 4 + ((unsigned) b_dynamic & 0xf);	/* number of bit length codes */

		b_dynamic >>= 4;
		k_dynamic -= 4;
		if (nl > 286 || nd > 30) {
			abort_unzip(PASS_STATE_ONLY);	/* bad lengths */
		}

		/* read in bit-length-code lengths */
		for (j = 0; j < nb; j++) {
			b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 3);
			ll[border[j]] = (unsigned) b_dynamic & 7;
			b_dynamic >>= 3;
			k_dynamic -= 3;
		}
		for (; j < 19; j++)
			ll[border[j]] = 0;

		/* build decoding table for trees - single level, 7 bit lookup */
		bl = 7;
		i = huft_build(ll, 19, 19, NULL, NULL, &inflate_codes_tl, &bl);
		if (i != 0) {
			abort_unzip(PASS_STATE_ONLY); //return i;	/* incomplete code set */
		}

		/* read in literal and distance code lengths */
		n = nl + nd;
		m = mask_bits[bl];
		i = l = 0;
		while ((unsigned) i < n) {
			b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, (unsigned)bl);
			td = inflate_codes_tl + ((unsigned) b_dynamic & m);
			j = td->b;
			b_dynamic >>= j;
			k_dynamic -= j;
			j = td->v.n;
			if (j < 16) {	/* length of code in bits (0..15) */
				ll[i++] = l = j;	/* save last length in l */
			} else if (j == 16) {	/* repeat last length 3 to 6 times */
				b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 2);
				j = 3 + ((unsigned) b_dynamic & 3);
				b_dynamic >>= 2;
				k_dynamic -= 2;
				if ((unsigned) i + j > n) {
					abort_unzip(PASS_STATE_ONLY); //return 1;
				}
				while (j--) {
					ll[i++] = l;
				}
			} else if (j == 17) {	/* 3 to 10 zero length codes */
				b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 3);
				j = 3 + ((unsigned) b_dynamic & 7);
				b_dynamic >>= 3;
				k_dynamic -= 3;
				if ((unsigned) i + j > n) {
					abort_unzip(PASS_STATE_ONLY); //return 1;
				}
				while (j--) {
					ll[i++] = 0;
				}
				l = 0;
			} else {	/* j == 18: 11 to 138 zero length codes */
				b_dynamic = fill_bitbuffer(PASS_STATE b_dynamic, &k_dynamic, 7);
				j = 11 + ((unsigned) b_dynamic & 0x7f);
				b_dynamic >>= 7;
				k_dynamic -= 7;
				if ((unsigned) i + j > n) {
					abort_unzip(PASS_STATE_ONLY); //return 1;
				}
				while (j--) {
					ll[i++] = 0;
				}
				l = 0;
			}
		}

		/* free decoding table for trees */
		huft_free(inflate_codes_tl);

		/* restore the global bit buffer */
		gunzip_bb = b_dynamic;
		gunzip_bk = k_dynamic;

		/* build the decoding tables for literal/length and distance codes */
		bl = lbits;

		i = huft_build(ll, nl, 257, cplens, cplext, &inflate_codes_tl, &bl);
		if (i != 0) {
			abort_unzip(PASS_STATE_ONLY);
		}
		bd = dbits;
		i = huft_build(ll + nl, nd, 0, cpdist, cpdext, &inflate_codes_td, &bd);
		if (i != 0) {
			abort_unzip(PASS_STATE_ONLY);
		}

		/* set up data for inflate_codes() */
		inflate_codes_setup(PASS_STATE bl, bd);

		/* huft_free code moved into inflate_codes */

		return -2;
	}
	default:
		abort_unzip(PASS_STATE_ONLY);
	}
}

/* Two callsites, both in inflate_get_next_window */
static void calculate_gunzip_crc(STATE_PARAM_ONLY)
{
	gunzip_crc = crc32_block_endian0(gunzip_crc, gunzip_window, gunzip_outbuf_count, gunzip_crc_table);
	gunzip_bytes_out += gunzip_outbuf_count;
}

/* One callsite in inflate_unzip_internal */
static int inflate_get_next_window(STATE_PARAM_ONLY)
{
	gunzip_outbuf_count = 0;

	while (1) {
		int ret;

		if (need_another_block) {
			if (end_reached) {
				calculate_gunzip_crc(PASS_STATE_ONLY);
				end_reached = 0;
				/* NB: need_another_block is still set */
				return 0; /* Last block */
			}
			method = inflate_block(PASS_STATE &end_reached);
			need_another_block = 0;
		}

		switch (method) {
		case -1:
			ret = inflate_stored(PASS_STATE_ONLY);
			break;
		case -2:
			ret = inflate_codes(PASS_STATE_ONLY);
			break;
		default: /* cannot happen */
			abort_unzip(PASS_STATE_ONLY);
		}

		if (ret == 1) {
			calculate_gunzip_crc(PASS_STATE_ONLY);
			return 1; /* more data left */
		}
		need_another_block = 1; /* end of that block */
	}
	/* Doesnt get here */
}


/* Called from unpack_gz_stream() and inflate_unzip() */
static IF_DESKTOP(long long) int
inflate_unzip_internal(STATE_PARAM transformer_state_t *xstate)
{
	IF_DESKTOP(long long) int n = 0;
	ssize_t nwrote;

	/* Allocate all global buffers (for DYN_ALLOC option) */
	gunzip_window = xmalloc(GUNZIP_WSIZE);
	gunzip_outbuf_count = 0;
	gunzip_bytes_out = 0;
	gunzip_src_fd = xstate->src_fd;

	/* (re) initialize state */
	method = -1;
	need_another_block = 1;
	resume_copy = 0;
	gunzip_bk = 0;
	gunzip_bb = 0;

	/* Create the crc table */
	gunzip_crc_table = crc32_new_table_le();
	gunzip_crc = ~0;

	error_msg = "corrupted data";
	if (setjmp(error_jmp)) {
		/* Error from deep inside zip machinery */
		bb_error_msg(error_msg);
		n = -1;
		goto ret;
	}

	while (1) {
		int r = inflate_get_next_window(PASS_STATE_ONLY);
		nwrote = transformer_write(xstate, gunzip_window, gunzip_outbuf_count);
		if (nwrote == (ssize_t)-1) {
			n = -1;
			goto ret;
		}
		IF_DESKTOP(n += nwrote;)
		if (r == 0) break;
	}

	/* Store unused bytes in a global buffer so calling applets can access it */
	if (gunzip_bk >= 8) {
		/* Undo too much lookahead. The next read will be byte aligned
		 * so we can discard unused bits in the last meaningful byte. */
		bytebuffer_offset--;
		bytebuffer[bytebuffer_offset] = gunzip_bb & 0xff;
		gunzip_bb >>= 8;
		gunzip_bk -= 8;
	}
 ret:
	/* Cleanup */
	free(gunzip_window);
	free(gunzip_crc_table);
	return n;
}


/* External entry points */

/* For unzip */

IF_DESKTOP(long long) int FAST_FUNC
inflate_unzip(transformer_state_t *xstate)
{
	IF_DESKTOP(long long) int n;
	DECLARE_STATE;

	ALLOC_STATE;

	to_read = xstate->bytes_in;
//	bytebuffer_max = 0x8000;
	bytebuffer_offset = 4;
	bytebuffer = xmalloc(bytebuffer_max);
	n = inflate_unzip_internal(PASS_STATE xstate);
	free(bytebuffer);

	xstate->crc32 = gunzip_crc;
	xstate->bytes_out = gunzip_bytes_out;
	DEALLOC_STATE;
	return n;
}


/* For gunzip */

/* helpers first */

/* Top up the input buffer with at least n bytes. */
static int top_up(STATE_PARAM unsigned n)
{
	int count = bytebuffer_size - bytebuffer_offset;

	if (count < (int)n) {
		memmove(bytebuffer, &bytebuffer[bytebuffer_offset], count);
		bytebuffer_offset = 0;
		bytebuffer_size = full_read(gunzip_src_fd, &bytebuffer[count], bytebuffer_max - count);
		if ((int)bytebuffer_size < 0) {
			bb_error_msg(bb_msg_read_error);
			return 0;
		}
		bytebuffer_size += count;
		if (bytebuffer_size < n)
			return 0;
	}
	return 1;
}

static uint16_t buffer_read_le_u16(STATE_PARAM_ONLY)
{
	uint16_t res;
#if BB_LITTLE_ENDIAN
	move_from_unaligned16(res, &bytebuffer[bytebuffer_offset]);
#else
	res = bytebuffer[bytebuffer_offset];
	res |= bytebuffer[bytebuffer_offset + 1] << 8;
#endif
	bytebuffer_offset += 2;
	return res;
}

static uint32_t buffer_read_le_u32(STATE_PARAM_ONLY)
{
	uint32_t res;
#if BB_LITTLE_ENDIAN
	move_from_unaligned32(res, &bytebuffer[bytebuffer_offset]);
#else
	res = bytebuffer[bytebuffer_offset];
	res |= bytebuffer[bytebuffer_offset + 1] << 8;
	res |= bytebuffer[bytebuffer_offset + 2] << 16;
	res |= bytebuffer[bytebuffer_offset + 3] << 24;
#endif
	bytebuffer_offset += 4;
	return res;
}

static int check_header_gzip(STATE_PARAM transformer_state_t *xstate)
{
	union {
		unsigned char raw[8];
		struct {
			uint8_t gz_method;
			uint8_t flags;
			uint32_t mtime;
			uint8_t xtra_flags_UNUSED;
			uint8_t os_flags_UNUSED;
		} PACKED formatted;
	} header;

	BUILD_BUG_ON(sizeof(header) != 8);

	/*
	 * Rewind bytebuffer. We use the beginning because the header has 8
	 * bytes, leaving enough for unwinding afterwards.
	 */
	bytebuffer_size -= bytebuffer_offset;
	memmove(bytebuffer, &bytebuffer[bytebuffer_offset], bytebuffer_size);
	bytebuffer_offset = 0;

	if (!top_up(PASS_STATE 8))
		return 0;
	memcpy(header.raw, &bytebuffer[bytebuffer_offset], 8);
	bytebuffer_offset += 8;

	/* Check the compression method */
	if (header.formatted.gz_method != 8) {
		return 0;
	}

	if (header.formatted.flags & 0x04) {
		/* bit 2 set: extra field present */
		unsigned extra_short;

		if (!top_up(PASS_STATE 2))
			return 0;
		extra_short = buffer_read_le_u16(PASS_STATE_ONLY);
		if (!top_up(PASS_STATE extra_short))
			return 0;
		/* Ignore extra field */
		bytebuffer_offset += extra_short;
	}

	/* Discard original name and file comment if any */
	/* bit 3 set: original file name present */
	/* bit 4 set: file comment present */
	if (header.formatted.flags & 0x18) {
		while (1) {
			do {
				if (!top_up(PASS_STATE 1))
					return 0;
			} while (bytebuffer[bytebuffer_offset++] != 0);
			if ((header.formatted.flags & 0x18) != 0x18)
				break;
			header.formatted.flags &= ~0x18;
		}
	}

	xstate->mtime = SWAP_LE32(header.formatted.mtime);

	/* Read the header checksum */
	if (header.formatted.flags & 0x02) {
		if (!top_up(PASS_STATE 2))
			return 0;
		bytebuffer_offset += 2;
	}
	return 1;
}

IF_DESKTOP(long long) int FAST_FUNC
unpack_gz_stream(transformer_state_t *xstate)
{
	uint32_t v32;
	IF_DESKTOP(long long) int total, n;
	DECLARE_STATE;

#if !ENABLE_FEATURE_SEAMLESS_Z
	if (check_signature16(xstate, GZIP_MAGIC))
		return -1;
#else
	if (!xstate->signature_skipped) {
		uint16_t magic2;

		if (full_read(xstate->src_fd, &magic2, 2) != 2) {
 bad_magic:
			bb_error_msg("invalid magic");
			return -1;
		}
		if (magic2 == COMPRESS_MAGIC) {
			xstate->signature_skipped = 2;
			return unpack_Z_stream(xstate);
		}
		if (magic2 != GZIP_MAGIC)
			goto bad_magic;
	}
#endif

	total = 0;

	ALLOC_STATE;
	to_read = -1;
//	bytebuffer_max = 0x8000;
	bytebuffer = xmalloc(bytebuffer_max);
	gunzip_src_fd = xstate->src_fd;

 again:
	if (!check_header_gzip(PASS_STATE xstate)) {
		bb_error_msg("corrupted data");
		total = -1;
		goto ret;
	}

	n = inflate_unzip_internal(PASS_STATE xstate);
	if (n < 0) {
		total = -1;
		goto ret;
	}
	total += n;

	if (!top_up(PASS_STATE 8)) {
		bb_error_msg("corrupted data");
		total = -1;
		goto ret;
	}

	/* Validate decompression - crc */
	v32 = buffer_read_le_u32(PASS_STATE_ONLY);
	if ((~gunzip_crc) != v32) {
		bb_error_msg("crc error");
		total = -1;
		goto ret;
	}

	/* Validate decompression - size */
	v32 = buffer_read_le_u32(PASS_STATE_ONLY);
	if ((uint32_t)gunzip_bytes_out != v32) {
		bb_error_msg("incorrect length");
		total = -1;
	}

	if (!top_up(PASS_STATE 2))
		goto ret; /* EOF */

	if (bytebuffer[bytebuffer_offset] == 0x1f
	 && bytebuffer[bytebuffer_offset + 1] == 0x8b
	) {
		bytebuffer_offset += 2;
		goto again;
	}
	/* GNU gzip says: */
	/*bb_error_msg("decompression OK, trailing garbage ignored");*/

 ret:
	free(bytebuffer);
	DEALLOC_STATE;
	return total;
}
