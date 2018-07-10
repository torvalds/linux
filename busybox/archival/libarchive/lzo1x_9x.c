/* lzo1x_9x.c -- implementation of the LZO1X-999 compression algorithm

   This file is part of the LZO real-time data compression library.

   Copyright (C) 2008 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2007 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2006 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2005 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2004 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2003 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2002 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2001 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 2000 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1999 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1998 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1997 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1996 Markus Franz Xaver Johannes Oberhumer
   All Rights Reserved.

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the LZO library; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

   Markus F.X.J. Oberhumer
   <markus@oberhumer.com>
   http://www.oberhumer.com/opensource/lzo/
*/
#include "libbb.h"

/* The following is probably only safe on Intel-compatible processors ... */
#define LZO_UNALIGNED_OK_2
#define LZO_UNALIGNED_OK_4

#include "liblzo.h"

#define LZO_MAX(a,b)        ((a) >= (b) ? (a) : (b))
#define LZO_MIN(a,b)        ((a) <= (b) ? (a) : (b))
#define LZO_MAX3(a,b,c)     ((a) >= (b) ? LZO_MAX(a,c) : LZO_MAX(b,c))

/***********************************************************************
//
************************************************************************/
#define SWD_N           M4_MAX_OFFSET   /* size of ring buffer */
#define SWD_F           2048           /* upper limit for match length */

#define SWD_BEST_OFF    (LZO_MAX3(M2_MAX_LEN, M3_MAX_LEN, M4_MAX_LEN) + 1)

typedef struct {
	int init;

	unsigned look;          /* bytes in lookahead buffer */

	unsigned m_len;
	unsigned m_off;

	const uint8_t *bp;
	const uint8_t *ip;
	const uint8_t *in;
	const uint8_t *in_end;
	uint8_t *out;

	unsigned r1_lit;
} lzo1x_999_t;

#define getbyte(c)  ((c).ip < (c).in_end ? *((c).ip)++ : (-1))

/* lzo_swd.c -- sliding window dictionary */

/***********************************************************************
//
************************************************************************/
#define SWD_UINT_MAX      USHRT_MAX

#ifndef SWD_HSIZE
#  define SWD_HSIZE         16384
#endif
#ifndef SWD_MAX_CHAIN
#  define SWD_MAX_CHAIN     2048
#endif

#define HEAD3(b, p) \
	( ((0x9f5f * ((((b[p]<<5)^b[p+1])<<5) ^ b[p+2])) >> 5) & (SWD_HSIZE-1) )

#if defined(LZO_UNALIGNED_OK_2)
#  define HEAD2(b,p)      (* (bb__aliased_uint16_t *) &(b[p]))
#else
#  define HEAD2(b,p)      (b[p] ^ ((unsigned)b[p+1]<<8))
#endif
#define NIL2              SWD_UINT_MAX

typedef struct lzo_swd {
	/* public - "built-in" */

	/* public - configuration */
	unsigned max_chain;
	int use_best_off;

	/* public - output */
	unsigned m_len;
	unsigned m_off;
	unsigned look;
	int b_char;
#if defined(SWD_BEST_OFF)
	unsigned best_off[SWD_BEST_OFF];
#endif

	/* semi public */
	lzo1x_999_t *c;
	unsigned m_pos;
#if defined(SWD_BEST_OFF)
	unsigned best_pos[SWD_BEST_OFF];
#endif

	/* private */
	unsigned ip;                /* input pointer (lookahead) */
	unsigned bp;                /* buffer pointer */
	unsigned rp;                /* remove pointer */

	unsigned node_count;
	unsigned first_rp;

	uint8_t b[SWD_N + SWD_F];
	uint8_t b_wrap[SWD_F]; /* must follow b */
	uint16_t head3[SWD_HSIZE];
	uint16_t succ3[SWD_N + SWD_F];
	uint16_t best3[SWD_N + SWD_F];
	uint16_t llen3[SWD_HSIZE];
#ifdef HEAD2
	uint16_t head2[65536L];
#endif
} lzo_swd_t, *lzo_swd_p;

#define SIZEOF_LZO_SWD_T    (sizeof(lzo_swd_t))


/* Access macro for head3.
 * head3[key] may be uninitialized, but then its value will never be used.
 */
#define s_get_head3(s,key)    s->head3[key]


/***********************************************************************
//
************************************************************************/
#define B_SIZE (SWD_N + SWD_F)

static int swd_init(lzo_swd_p s)
{
	/* defaults */
	s->node_count = SWD_N;

	memset(s->llen3, 0, sizeof(s->llen3[0]) * (unsigned)SWD_HSIZE);
#ifdef HEAD2
	memset(s->head2, 0xff, sizeof(s->head2[0]) * 65536L);
	assert(s->head2[0] == NIL2);
#endif

	s->ip = 0;
	s->bp = s->ip;
	s->first_rp = s->ip;

	assert(s->ip + SWD_F <= B_SIZE);
	s->look = (unsigned) (s->c->in_end - s->c->ip);
	if (s->look > 0) {
		if (s->look > SWD_F)
			s->look = SWD_F;
		memcpy(&s->b[s->ip], s->c->ip, s->look);
		s->c->ip += s->look;
		s->ip += s->look;
	}
	if (s->ip == B_SIZE)
		s->ip = 0;

	s->rp = s->first_rp;
	if (s->rp >= s->node_count)
		s->rp -= s->node_count;
	else
		s->rp += B_SIZE - s->node_count;

	return LZO_E_OK;
}

#define swd_pos2off(s,pos) \
	(s->bp > (pos) ? s->bp - (pos) : B_SIZE - ((pos) - s->bp))


/***********************************************************************
//
************************************************************************/
static void swd_getbyte(lzo_swd_p s)
{
	int c;

	if ((c = getbyte(*(s->c))) < 0) {
		if (s->look > 0)
			--s->look;
	} else {
		s->b[s->ip] = c;
		if (s->ip < SWD_F)
			s->b_wrap[s->ip] = c;
	}
	if (++s->ip == B_SIZE)
		s->ip = 0;
	if (++s->bp == B_SIZE)
		s->bp = 0;
	if (++s->rp == B_SIZE)
		s->rp = 0;
}


/***********************************************************************
// remove node from lists
************************************************************************/
static void swd_remove_node(lzo_swd_p s, unsigned node)
{
	if (s->node_count == 0) {
		unsigned key;

		key = HEAD3(s->b,node);
		assert(s->llen3[key] > 0);
		--s->llen3[key];

#ifdef HEAD2
		key = HEAD2(s->b,node);
		assert(s->head2[key] != NIL2);
		if ((unsigned) s->head2[key] == node)
			s->head2[key] = NIL2;
#endif
	} else
		--s->node_count;
}


/***********************************************************************
//
************************************************************************/
static void swd_accept(lzo_swd_p s, unsigned n)
{
	assert(n <= s->look);

	while (n--) {
		unsigned key;

		swd_remove_node(s,s->rp);

		/* add bp into HEAD3 */
		key = HEAD3(s->b, s->bp);
		s->succ3[s->bp] = s_get_head3(s, key);
		s->head3[key] = s->bp;
		s->best3[s->bp] = SWD_F + 1;
		s->llen3[key]++;
		assert(s->llen3[key] <= SWD_N);

#ifdef HEAD2
		/* add bp into HEAD2 */
		key = HEAD2(s->b, s->bp);
		s->head2[key] = s->bp;
#endif

		swd_getbyte(s);
	}
}


/***********************************************************************
//
************************************************************************/
static void swd_search(lzo_swd_p s, unsigned node, unsigned cnt)
{
	const uint8_t *p1;
	const uint8_t *p2;
	const uint8_t *px;
	unsigned m_len = s->m_len;
	const uint8_t *b  = s->b;
	const uint8_t *bp = s->b + s->bp;
	const uint8_t *bx = s->b + s->bp + s->look;
	unsigned char scan_end1;

	assert(s->m_len > 0);

	scan_end1 = bp[m_len - 1];
	for ( ; cnt-- > 0; node = s->succ3[node]) {
		p1 = bp;
		p2 = b + node;
		px = bx;

		assert(m_len < s->look);

		if (p2[m_len - 1] == scan_end1
		 && p2[m_len] == p1[m_len]
		 && p2[0] == p1[0]
		 && p2[1] == p1[1]
		) {
			unsigned i;
			assert(lzo_memcmp(bp, &b[node], 3) == 0);

			p1 += 2; p2 += 2;
			do {} while (++p1 < px && *p1 == *++p2);
			i = p1-bp;

			assert(lzo_memcmp(bp, &b[node], i) == 0);

#if defined(SWD_BEST_OFF)
			if (i < SWD_BEST_OFF) {
				if (s->best_pos[i] == 0)
					s->best_pos[i] = node + 1;
			}
#endif
			if (i > m_len) {
				s->m_len = m_len = i;
				s->m_pos = node;
				if (m_len == s->look)
					return;
				if (m_len >= SWD_F)
					return;
				if (m_len > (unsigned) s->best3[node])
					return;
				scan_end1 = bp[m_len - 1];
			}
		}
	}
}


/***********************************************************************
//
************************************************************************/
#ifdef HEAD2

static int swd_search2(lzo_swd_p s)
{
	unsigned key;

	assert(s->look >= 2);
	assert(s->m_len > 0);

	key = s->head2[HEAD2(s->b, s->bp)];
	if (key == NIL2)
		return 0;
	assert(lzo_memcmp(&s->b[s->bp], &s->b[key], 2) == 0);
#if defined(SWD_BEST_OFF)
	if (s->best_pos[2] == 0)
		s->best_pos[2] = key + 1;
#endif

	if (s->m_len < 2) {
		s->m_len = 2;
		s->m_pos = key;
	}
	return 1;
}

#endif


/***********************************************************************
//
************************************************************************/
static void swd_findbest(lzo_swd_p s)
{
	unsigned key;
	unsigned cnt, node;
	unsigned len;

	assert(s->m_len > 0);

	/* get current head, add bp into HEAD3 */
	key = HEAD3(s->b,s->bp);
	node = s->succ3[s->bp] = s_get_head3(s, key);
	cnt = s->llen3[key]++;
	assert(s->llen3[key] <= SWD_N + SWD_F);
	if (cnt > s->max_chain)
		cnt = s->max_chain;
	s->head3[key] = s->bp;

	s->b_char = s->b[s->bp];
	len = s->m_len;
	if (s->m_len >= s->look) {
		if (s->look == 0)
			s->b_char = -1;
		s->m_off = 0;
		s->best3[s->bp] = SWD_F + 1;
	} else {
#ifdef HEAD2
		if (swd_search2(s))
#endif
			if (s->look >= 3)
				swd_search(s, node, cnt);
		if (s->m_len > len)
			s->m_off = swd_pos2off(s,s->m_pos);
		s->best3[s->bp] = s->m_len;

#if defined(SWD_BEST_OFF)
		if (s->use_best_off) {
			int i;
			for (i = 2; i < SWD_BEST_OFF; i++) {
				if (s->best_pos[i] > 0)
					s->best_off[i] = swd_pos2off(s, s->best_pos[i]-1);
				else
					s->best_off[i] = 0;
			}
		}
#endif
	}

	swd_remove_node(s,s->rp);

#ifdef HEAD2
	/* add bp into HEAD2 */
	key = HEAD2(s->b, s->bp);
	s->head2[key] = s->bp;
#endif
}

#undef HEAD3
#undef HEAD2
#undef s_get_head3


/***********************************************************************
//
************************************************************************/
static int init_match(lzo1x_999_t *c, lzo_swd_p s, uint32_t use_best_off)
{
	int r;

	assert(!c->init);
	c->init = 1;

	s->c = c;

	r = swd_init(s);
	if (r != 0)
		return r;

	s->use_best_off = use_best_off;
	return r;
}


/***********************************************************************
//
************************************************************************/
static int find_match(lzo1x_999_t *c, lzo_swd_p s,
		unsigned this_len, unsigned skip)
{
	assert(c->init);

	if (skip > 0) {
		assert(this_len >= skip);
		swd_accept(s, this_len - skip);
	} else {
		assert(this_len <= 1);
	}

	s->m_len = 1;
#ifdef SWD_BEST_OFF
	if (s->use_best_off)
		memset(s->best_pos, 0, sizeof(s->best_pos));
#endif
	swd_findbest(s);
	c->m_len = s->m_len;
	c->m_off = s->m_off;

	swd_getbyte(s);

	if (s->b_char < 0) {
		c->look = 0;
		c->m_len = 0;
	} else {
		c->look = s->look + 1;
	}
	c->bp = c->ip - c->look;

	return LZO_E_OK;
}

/* this is a public functions, but there is no prototype in a header file */
static int lzo1x_999_compress_internal(const uint8_t *in , unsigned in_len,
		uint8_t *out, unsigned *out_len,
		void *wrkmem,
		unsigned good_length,
		unsigned max_lazy,
		unsigned max_chain,
		uint32_t use_best_off);


/***********************************************************************
//
************************************************************************/
static uint8_t *code_match(lzo1x_999_t *c,
		uint8_t *op, unsigned m_len, unsigned m_off)
{
	assert(op > c->out);
	if (m_len == 2) {
		assert(m_off <= M1_MAX_OFFSET);
		assert(c->r1_lit > 0);
		assert(c->r1_lit < 4);
		m_off -= 1;
		*op++ = M1_MARKER | ((m_off & 3) << 2);
		*op++ = m_off >> 2;
	} else if (m_len <= M2_MAX_LEN && m_off <= M2_MAX_OFFSET) {
		assert(m_len >= 3);
		m_off -= 1;
		*op++ = ((m_len - 1) << 5) | ((m_off & 7) << 2);
		*op++ = m_off >> 3;
		assert(op[-2] >= M2_MARKER);
	} else if (m_len == M2_MIN_LEN && m_off <= MX_MAX_OFFSET && c->r1_lit >= 4) {
		assert(m_len == 3);
		assert(m_off > M2_MAX_OFFSET);
		m_off -= 1 + M2_MAX_OFFSET;
		*op++ = M1_MARKER | ((m_off & 3) << 2);
		*op++ = m_off >> 2;
	} else if (m_off <= M3_MAX_OFFSET) {
		assert(m_len >= 3);
		m_off -= 1;
		if (m_len <= M3_MAX_LEN)
			*op++ = M3_MARKER | (m_len - 2);
		else {
			m_len -= M3_MAX_LEN;
			*op++ = M3_MARKER | 0;
			while (m_len > 255) {
				m_len -= 255;
				*op++ = 0;
			}
			assert(m_len > 0);
			*op++ = m_len;
		}
		*op++ = m_off << 2;
		*op++ = m_off >> 6;
	} else {
		unsigned k;

		assert(m_len >= 3);
		assert(m_off > 0x4000);
		assert(m_off <= 0xbfff);
		m_off -= 0x4000;
		k = (m_off & 0x4000) >> 11;
		if (m_len <= M4_MAX_LEN)
			*op++ = M4_MARKER | k | (m_len - 2);
		else {
			m_len -= M4_MAX_LEN;
			*op++ = M4_MARKER | k | 0;
			while (m_len > 255) {
				m_len -= 255;
				*op++ = 0;
			}
			assert(m_len > 0);
			*op++ = m_len;
		}
		*op++ = m_off << 2;
		*op++ = m_off >> 6;
	}

	return op;
}


static uint8_t *STORE_RUN(lzo1x_999_t *c, uint8_t *op,
		const uint8_t *ii, unsigned t)
{
	if (op == c->out && t <= 238) {
		*op++ = 17 + t;
	} else if (t <= 3) {
		op[-2] |= t;
	} else if (t <= 18) {
		*op++ = t - 3;
	} else {
		unsigned tt = t - 18;

		*op++ = 0;
		while (tt > 255) {
			tt -= 255;
			*op++ = 0;
		}
		assert(tt > 0);
		*op++ = tt;
	}
	do *op++ = *ii++; while (--t > 0);

	return op;
}


static uint8_t *code_run(lzo1x_999_t *c, uint8_t *op, const uint8_t *ii,
		unsigned lit)
{
	if (lit > 0) {
		assert(m_len >= 2);
		op = STORE_RUN(c, op, ii, lit);
	} else {
		assert(m_len >= 3);
	}
	c->r1_lit = lit;

	return op;
}


/***********************************************************************
//
************************************************************************/
static int len_of_coded_match(unsigned m_len, unsigned m_off, unsigned lit)
{
	int n = 4;

	if (m_len < 2)
		return -1;
	if (m_len == 2)
		return (m_off <= M1_MAX_OFFSET && lit > 0 && lit < 4) ? 2 : -1;
	if (m_len <= M2_MAX_LEN && m_off <= M2_MAX_OFFSET)
		return 2;
	if (m_len == M2_MIN_LEN && m_off <= MX_MAX_OFFSET && lit >= 4)
		return 2;
	if (m_off <= M3_MAX_OFFSET) {
		if (m_len <= M3_MAX_LEN)
			return 3;
		m_len -= M3_MAX_LEN;
	} else if (m_off <= M4_MAX_OFFSET) {
		if (m_len <= M4_MAX_LEN)
			return 3;
		m_len -= M4_MAX_LEN;
	} else
		return -1;
	while (m_len > 255) {
		m_len -= 255;
		n++;
	}
	return n;
}


static int min_gain(unsigned ahead, unsigned lit1,
			unsigned lit2, int l1, int l2, int l3)
{
	int lazy_match_min_gain = 0;

	assert (ahead >= 1);
	lazy_match_min_gain += ahead;

	if (lit1 <= 3)
		lazy_match_min_gain += (lit2 <= 3) ? 0 : 2;
	else if (lit1 <= 18)
		lazy_match_min_gain += (lit2 <= 18) ? 0 : 1;

	lazy_match_min_gain += (l2 - l1) * 2;
	if (l3 > 0)
		lazy_match_min_gain -= (ahead - l3) * 2;

	if (lazy_match_min_gain < 0)
		lazy_match_min_gain = 0;

	return lazy_match_min_gain;
}


/***********************************************************************
//
************************************************************************/
#if defined(SWD_BEST_OFF)

static void better_match(const lzo_swd_p swd,
			unsigned *m_len, unsigned *m_off)
{
	if (*m_len <= M2_MIN_LEN)
		return;

	if (*m_off <= M2_MAX_OFFSET)
		return;

	/* M3/M4 -> M2 */
	if (*m_off > M2_MAX_OFFSET
	 && *m_len >= M2_MIN_LEN + 1 && *m_len <= M2_MAX_LEN + 1
	 && swd->best_off[*m_len-1] && swd->best_off[*m_len-1] <= M2_MAX_OFFSET
	) {
		*m_len = *m_len - 1;
		*m_off = swd->best_off[*m_len];
		return;
	}

	/* M4 -> M2 */
	if (*m_off > M3_MAX_OFFSET
	 && *m_len >= M4_MAX_LEN + 1 && *m_len <= M2_MAX_LEN + 2
	 && swd->best_off[*m_len-2] && swd->best_off[*m_len-2] <= M2_MAX_OFFSET
	) {
		*m_len = *m_len - 2;
		*m_off = swd->best_off[*m_len];
		return;
	}
	/* M4 -> M3 */
	if (*m_off > M3_MAX_OFFSET
	 && *m_len >= M4_MAX_LEN + 1 && *m_len <= M3_MAX_LEN + 1
	 && swd->best_off[*m_len-1] && swd->best_off[*m_len-1] <= M3_MAX_OFFSET
	) {
		*m_len = *m_len - 1;
		*m_off = swd->best_off[*m_len];
	}
}

#endif


/***********************************************************************
//
************************************************************************/
static int lzo1x_999_compress_internal(const uint8_t *in, unsigned in_len,
		uint8_t *out, unsigned *out_len,
		void *wrkmem,
		unsigned good_length,
		unsigned max_lazy,
		unsigned max_chain,
		uint32_t use_best_off)
{
	uint8_t *op;
	const uint8_t *ii;
	unsigned lit;
	unsigned m_len, m_off;
	lzo1x_999_t cc;
	lzo1x_999_t *const c = &cc;
	const lzo_swd_p swd = (lzo_swd_p) wrkmem;
	int r;

	c->init = 0;
	c->ip = c->in = in;
	c->in_end = in + in_len;
	c->out = out;

	op = out;
	ii = c->ip;             /* point to start of literal run */
	lit = 0;
	c->r1_lit = 0;

	r = init_match(c, swd, use_best_off);
	if (r != 0)
		return r;
	swd->max_chain = max_chain;

	r = find_match(c, swd, 0, 0);
	if (r != 0)
		return r;

	while (c->look > 0) {
		unsigned ahead;
		unsigned max_ahead;
		int l1, l2, l3;

		m_len = c->m_len;
		m_off = c->m_off;

		assert(c->bp == c->ip - c->look);
		assert(c->bp >= in);
		if (lit == 0)
			ii = c->bp;
		assert(ii + lit == c->bp);
		assert(swd->b_char == *(c->bp));

		if (m_len < 2
		 || (m_len == 2 && (m_off > M1_MAX_OFFSET || lit == 0 || lit >= 4))
		    /* Do not accept this match for compressed-data compatibility
		     * with LZO v1.01 and before
		     * [ might be a problem for decompress() and optimize() ]
		     */
		 || (m_len == 2 && op == out)
		 || (op == out && lit == 0)
		) {
			/* a literal */
			m_len = 0;
		}
		else if (m_len == M2_MIN_LEN) {
			/* compression ratio improves if we code a literal in some cases */
			if (m_off > MX_MAX_OFFSET && lit >= 4)
				m_len = 0;
		}

		if (m_len == 0) {
			/* a literal */
			lit++;
			swd->max_chain = max_chain;
			r = find_match(c, swd, 1, 0);
			assert(r == 0);
			continue;
		}

		/* a match */
#if defined(SWD_BEST_OFF)
		if (swd->use_best_off)
			better_match(swd, &m_len, &m_off);
#endif

		/* shall we try a lazy match ? */
		ahead = 0;
		if (m_len >= max_lazy) {
			/* no */
			l1 = 0;
			max_ahead = 0;
		} else {
			/* yes, try a lazy match */
			l1 = len_of_coded_match(m_len, m_off, lit);
			assert(l1 > 0);
			max_ahead = LZO_MIN(2, (unsigned)l1 - 1);
		}


		while (ahead < max_ahead && c->look > m_len) {
			int lazy_match_min_gain;

			if (m_len >= good_length)
				swd->max_chain = max_chain >> 2;
			else
				swd->max_chain = max_chain;
			r = find_match(c, swd, 1, 0);
			ahead++;

			assert(r == 0);
			assert(c->look > 0);
			assert(ii + lit + ahead == c->bp);

			if (c->m_len < m_len)
				continue;
			if (c->m_len == m_len && c->m_off >= m_off)
				continue;
#if defined(SWD_BEST_OFF)
			if (swd->use_best_off)
				better_match(swd, &c->m_len, &c->m_off);
#endif
			l2 = len_of_coded_match(c->m_len, c->m_off, lit+ahead);
			if (l2 < 0)
				continue;

			/* compressed-data compatibility [see above] */
			l3 = (op == out) ? -1 : len_of_coded_match(ahead, m_off, lit);

			lazy_match_min_gain = min_gain(ahead, lit, lit+ahead, l1, l2, l3);
			if (c->m_len >= m_len + lazy_match_min_gain) {
				if (l3 > 0) {
					/* code previous run */
					op = code_run(c, op, ii, lit);
					lit = 0;
					/* code shortened match */
					op = code_match(c, op, ahead, m_off);
				} else {
					lit += ahead;
					assert(ii + lit == c->bp);
				}
				goto lazy_match_done;
			}
		}

		assert(ii + lit + ahead == c->bp);

		/* 1 - code run */
		op = code_run(c, op, ii, lit);
		lit = 0;

		/* 2 - code match */
		op = code_match(c, op, m_len, m_off);
		swd->max_chain = max_chain;
		r = find_match(c, swd, m_len, 1+ahead);
		assert(r == 0);

 lazy_match_done: ;
	}

	/* store final run */
	if (lit > 0)
		op = STORE_RUN(c, op, ii, lit);

#if defined(LZO_EOF_CODE)
	*op++ = M4_MARKER | 1;
	*op++ = 0;
	*op++ = 0;
#endif

	*out_len = op - out;

	return LZO_E_OK;
}


/***********************************************************************
//
************************************************************************/
int lzo1x_999_compress_level(const uint8_t *in, unsigned in_len,
		uint8_t *out, unsigned *out_len,
		void *wrkmem,
		int compression_level)
{
	static const struct {
		uint16_t good_length;
		uint16_t max_lazy;
		uint16_t max_chain;
		uint16_t use_best_off;
	} c[3] = {
		{     8,    32,  256,   0 },
		{    32,   128, 2048,   1 },
		{ SWD_F, SWD_F, 4096,   1 }       /* max. compression */
	};

	if (compression_level < 7 || compression_level > 9)
		return LZO_E_ERROR;

	compression_level -= 7;
	return lzo1x_999_compress_internal(in, in_len, out, out_len, wrkmem,
					c[compression_level].good_length,
					c[compression_level].max_lazy,
					c[compression_level].max_chain,
					c[compression_level].use_best_off);
}
