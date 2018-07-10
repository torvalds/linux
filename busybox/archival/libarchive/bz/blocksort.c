/*
 * bzip2 is written by Julian Seward <jseward@bzip.org>.
 * Adapted for busybox by Denys Vlasenko <vda.linux@googlemail.com>.
 * See README and LICENSE files in this directory for more information.
 */

/*-------------------------------------------------------------*/
/*--- Block sorting machinery                               ---*/
/*---                                           blocksort.c ---*/
/*-------------------------------------------------------------*/

/* ------------------------------------------------------------------
This file is part of bzip2/libbzip2, a program and library for
lossless, block-sorting data compression.

bzip2/libbzip2 version 1.0.4 of 20 December 2006
Copyright (C) 1996-2006 Julian Seward <jseward@bzip.org>

Please read the WARNING, DISCLAIMER and PATENTS sections in the
README file.

This program is released under the terms of the license contained
in the file LICENSE.
------------------------------------------------------------------ */

/* #include "bzlib_private.h" */

#define mswap(zz1, zz2) \
{ \
	int32_t zztmp = zz1; \
	zz1 = zz2; \
	zz2 = zztmp; \
}

static
/* No measurable speed gain with inlining */
/* ALWAYS_INLINE */
void mvswap(uint32_t* ptr, int32_t zzp1, int32_t zzp2, int32_t zzn)
{
	while (zzn > 0) {
		mswap(ptr[zzp1], ptr[zzp2]);
		zzp1++;
		zzp2++;
		zzn--;
	}
}

static
ALWAYS_INLINE
int32_t mmin(int32_t a, int32_t b)
{
	return (a < b) ? a : b;
}


/*---------------------------------------------*/
/*--- Fallback O(N log(N)^2) sorting        ---*/
/*--- algorithm, for repetitive blocks      ---*/
/*---------------------------------------------*/

/*---------------------------------------------*/
static
inline
void fallbackSimpleSort(uint32_t* fmap,
		uint32_t* eclass,
		int32_t   lo,
		int32_t   hi)
{
	int32_t i, j, tmp;
	uint32_t ec_tmp;

	if (lo == hi) return;

	if (hi - lo > 3) {
		for (i = hi-4; i >= lo; i--) {
			tmp = fmap[i];
			ec_tmp = eclass[tmp];
			for (j = i+4; j <= hi && ec_tmp > eclass[fmap[j]]; j += 4)
				fmap[j-4] = fmap[j];
			fmap[j-4] = tmp;
		}
	}

	for (i = hi-1; i >= lo; i--) {
		tmp = fmap[i];
		ec_tmp = eclass[tmp];
		for (j = i+1; j <= hi && ec_tmp > eclass[fmap[j]]; j++)
			fmap[j-1] = fmap[j];
		fmap[j-1] = tmp;
	}
}


/*---------------------------------------------*/
#define fpush(lz,hz) { \
	stackLo[sp] = lz; \
	stackHi[sp] = hz; \
	sp++; \
}

#define fpop(lz,hz) { \
	sp--; \
	lz = stackLo[sp]; \
	hz = stackHi[sp]; \
}

#define FALLBACK_QSORT_SMALL_THRESH 10
#define FALLBACK_QSORT_STACK_SIZE   100

static
void fallbackQSort3(uint32_t* fmap,
		uint32_t* eclass,
		int32_t   loSt,
		int32_t   hiSt)
{
	int32_t sp;
	uint32_t r;
	int32_t stackLo[FALLBACK_QSORT_STACK_SIZE];
	int32_t stackHi[FALLBACK_QSORT_STACK_SIZE];

	r = 0;

	sp = 0;
	fpush(loSt, hiSt);

	while (sp > 0) {
		int32_t unLo, unHi, ltLo, gtHi, n, m;
		int32_t lo, hi;
		uint32_t med;
		uint32_t r3;

		AssertH(sp < FALLBACK_QSORT_STACK_SIZE - 1, 1004);

		fpop(lo, hi);
		if (hi - lo < FALLBACK_QSORT_SMALL_THRESH) {
			fallbackSimpleSort(fmap, eclass, lo, hi);
			continue;
		}

		/* Random partitioning.  Median of 3 sometimes fails to
		 * avoid bad cases.  Median of 9 seems to help but
		 * looks rather expensive.  This too seems to work but
		 * is cheaper.  Guidance for the magic constants
		 * 7621 and 32768 is taken from Sedgewick's algorithms
		 * book, chapter 35.
		 */
		r = ((r * 7621) + 1) % 32768;
		r3 = r % 3;
		if (r3 == 0)
			med = eclass[fmap[lo]];
		else if (r3 == 1)
			med = eclass[fmap[(lo+hi)>>1]];
		else
			med = eclass[fmap[hi]];

		unLo = ltLo = lo;
		unHi = gtHi = hi;

		while (1) {
			while (1) {
				if (unLo > unHi) break;
				n = (int32_t)eclass[fmap[unLo]] - (int32_t)med;
				if (n == 0) {
					mswap(fmap[unLo], fmap[ltLo]);
					ltLo++;
					unLo++;
					continue;
				}
				if (n > 0) break;
				unLo++;
			}
			while (1) {
				if (unLo > unHi) break;
				n = (int32_t)eclass[fmap[unHi]] - (int32_t)med;
				if (n == 0) {
					mswap(fmap[unHi], fmap[gtHi]);
					gtHi--; unHi--;
					continue;
				}
				if (n < 0) break;
				unHi--;
			}
			if (unLo > unHi) break;
			mswap(fmap[unLo], fmap[unHi]); unLo++; unHi--;
		}

		AssertD(unHi == unLo-1, "fallbackQSort3(2)");

		if (gtHi < ltLo) continue;

		n = mmin(ltLo-lo, unLo-ltLo); mvswap(fmap, lo, unLo-n, n);
		m = mmin(hi-gtHi, gtHi-unHi); mvswap(fmap, unLo, hi-m+1, m);

		n = lo + unLo - ltLo - 1;
		m = hi - (gtHi - unHi) + 1;

		if (n - lo > hi - m) {
			fpush(lo, n);
			fpush(m, hi);
		} else {
			fpush(m, hi);
			fpush(lo, n);
		}
	}
}

#undef fpush
#undef fpop
#undef FALLBACK_QSORT_SMALL_THRESH
#undef FALLBACK_QSORT_STACK_SIZE


/*---------------------------------------------*/
/* Pre:
 *	nblock > 0
 *	eclass exists for [0 .. nblock-1]
 *	((uint8_t*)eclass) [0 .. nblock-1] holds block
 *	ptr exists for [0 .. nblock-1]
 *
 * Post:
 *	((uint8_t*)eclass) [0 .. nblock-1] holds block
 *	All other areas of eclass destroyed
 *	fmap [0 .. nblock-1] holds sorted order
 *	bhtab[0 .. 2+(nblock/32)] destroyed
*/

#define       SET_BH(zz)  bhtab[(zz) >> 5] |= (1 << ((zz) & 31))
#define     CLEAR_BH(zz)  bhtab[(zz) >> 5] &= ~(1 << ((zz) & 31))
#define     ISSET_BH(zz)  (bhtab[(zz) >> 5] & (1 << ((zz) & 31)))
#define      WORD_BH(zz)  bhtab[(zz) >> 5]
#define UNALIGNED_BH(zz)  ((zz) & 0x01f)

static
void fallbackSort(EState* state)
{
	int32_t ftab[257];
	int32_t ftabCopy[256];
	int32_t H, i, j, k, l, r, cc, cc1;
	int32_t nNotDone;
	int32_t nBhtab;
	/* params */
	uint32_t *const fmap    = state->arr1;
	uint32_t *const eclass  = state->arr2;
#define eclass8 ((uint8_t*)eclass)
	uint32_t *const bhtab   = state->ftab;
	const int32_t   nblock  = state->nblock;

	/*
	 * Initial 1-char radix sort to generate
	 * initial fmap and initial BH bits.
	 */
	for (i = 0; i < 257;    i++) ftab[i] = 0;
	for (i = 0; i < nblock; i++) ftab[eclass8[i]]++;
	for (i = 0; i < 256;    i++) ftabCopy[i] = ftab[i];

	j = ftab[0];  /* bbox: optimized */
	for (i = 1; i < 257;    i++) {
		j += ftab[i];
		ftab[i] = j;
	}

	for (i = 0; i < nblock; i++) {
		j = eclass8[i];
		k = ftab[j] - 1;
		ftab[j] = k;
		fmap[k] = i;
	}

	nBhtab = 2 + ((uint32_t)nblock / 32); /* bbox: unsigned div is easier */
	for (i = 0; i < nBhtab; i++) bhtab[i] = 0;
	for (i = 0; i < 256; i++) SET_BH(ftab[i]);

	/*
	 * Inductively refine the buckets.  Kind-of an
	 * "exponential radix sort" (!), inspired by the
	 * Manber-Myers suffix array construction algorithm.
	 */

	/*-- set sentinel bits for block-end detection --*/
	for (i = 0; i < 32; i++) {
		SET_BH(nblock + 2*i);
		CLEAR_BH(nblock + 2*i + 1);
	}

	/*-- the log(N) loop --*/
	H = 1;
	while (1) {
		j = 0;
		for (i = 0; i < nblock; i++) {
			if (ISSET_BH(i))
				j = i;
			k = fmap[i] - H;
			if (k < 0)
				k += nblock;
			eclass[k] = j;
		}

		nNotDone = 0;
		r = -1;
		while (1) {

		/*-- find the next non-singleton bucket --*/
			k = r + 1;
			while (ISSET_BH(k) && UNALIGNED_BH(k))
				k++;
			if (ISSET_BH(k)) {
				while (WORD_BH(k) == 0xffffffff) k += 32;
				while (ISSET_BH(k)) k++;
			}
			l = k - 1;
			if (l >= nblock)
				break;
			while (!ISSET_BH(k) && UNALIGNED_BH(k))
				k++;
			if (!ISSET_BH(k)) {
				while (WORD_BH(k) == 0x00000000) k += 32;
				while (!ISSET_BH(k)) k++;
			}
			r = k - 1;
			if (r >= nblock)
				break;

			/*-- now [l, r] bracket current bucket --*/
			if (r > l) {
				nNotDone += (r - l + 1);
				fallbackQSort3(fmap, eclass, l, r);

				/*-- scan bucket and generate header bits-- */
				cc = -1;
				for (i = l; i <= r; i++) {
					cc1 = eclass[fmap[i]];
					if (cc != cc1) {
						SET_BH(i);
						cc = cc1;
					}
				}
			}
		}

		H *= 2;
		if (H > nblock || nNotDone == 0)
			break;
	}

	/*
	 * Reconstruct the original block in
	 * eclass8 [0 .. nblock-1], since the
	 * previous phase destroyed it.
	 */
	j = 0;
	for (i = 0; i < nblock; i++) {
		while (ftabCopy[j] == 0)
			j++;
		ftabCopy[j]--;
		eclass8[fmap[i]] = (uint8_t)j;
	}
	AssertH(j < 256, 1005);
#undef eclass8
}

#undef       SET_BH
#undef     CLEAR_BH
#undef     ISSET_BH
#undef      WORD_BH
#undef UNALIGNED_BH


/*---------------------------------------------*/
/*--- The main, O(N^2 log(N)) sorting       ---*/
/*--- algorithm.  Faster for "normal"       ---*/
/*--- non-repetitive blocks.                ---*/
/*---------------------------------------------*/

/*---------------------------------------------*/
static
NOINLINE
int mainGtU(EState* state,
		uint32_t  i1,
		uint32_t  i2)
{
	int32_t  k;
	uint8_t  c1, c2;
	uint16_t s1, s2;

	uint8_t  *const block    = state->block;
	uint16_t *const quadrant = state->quadrant;
	const int32_t   nblock   = state->nblock;

/* Loop unrolling here is actually very useful
 * (generated code is much simpler),
 * code size increase is only 270 bytes (i386)
 * but speeds up compression 10% overall
 */

#if BZIP2_SPEED >= 1

#define TIMES_8(code) \
	code; code; code; code; \
	code; code; code; code;
#define TIMES_12(code) \
	code; code; code; code; \
	code; code; code; code; \
	code; code; code; code;

#else

#define TIMES_8(code) \
{ \
	int nn = 8; \
	do { \
		code; \
	} while (--nn); \
}
#define TIMES_12(code) \
{ \
	int nn = 12; \
	do { \
		code; \
	} while (--nn); \
}

#endif

	AssertD(i1 != i2, "mainGtU");
	TIMES_12(
		c1 = block[i1]; c2 = block[i2];
		if (c1 != c2) return (c1 > c2);
		i1++; i2++;
	)

	k = nblock + 8;

	do {
		TIMES_8(
			c1 = block[i1]; c2 = block[i2];
			if (c1 != c2) return (c1 > c2);
			s1 = quadrant[i1]; s2 = quadrant[i2];
			if (s1 != s2) return (s1 > s2);
			i1++; i2++;
		)

		if (i1 >= nblock) i1 -= nblock;
		if (i2 >= nblock) i2 -= nblock;

		state->budget--;
		k -= 8;
	} while (k >= 0);

	return False;
}
#undef TIMES_8
#undef TIMES_12

/*---------------------------------------------*/
/*
 * Knuth's increments seem to work better
 * than Incerpi-Sedgewick here.  Possibly
 * because the number of elems to sort is
 * usually small, typically <= 20.
 */
static
const uint32_t incs[14] = {
	1, 4, 13, 40, 121, 364, 1093, 3280,
	9841, 29524, 88573, 265720,
	797161, 2391484
};

static
void mainSimpleSort(EState* state,
		int32_t   lo,
		int32_t   hi,
		int32_t   d)
{
	uint32_t *const ptr = state->ptr;

	/* At which increment to start? */
	int hp = 0;
	{
		int bigN = hi - lo;
		if (bigN <= 0)
			return;
		while (incs[hp] <= bigN)
			hp++;
		hp--;
	}

	for (; hp >= 0; hp--) {
		int32_t i;
		unsigned h;

		h = incs[hp];
		i = lo + h;
		while (1) {
			unsigned j;
			unsigned v;

			if (i > hi) break;
			v = ptr[i];
			j = i;
			while (mainGtU(state, ptr[j-h]+d, v+d)) {
				ptr[j] = ptr[j-h];
				j = j - h;
				if (j <= (lo + h - 1)) break;
			}
			ptr[j] = v;
			i++;

/* 1.5% overall speedup, +290 bytes */
#if BZIP2_SPEED >= 3
			/*-- copy 2 --*/
			if (i > hi) break;
			v = ptr[i];
			j = i;
			while (mainGtU(state, ptr[j-h]+d, v+d)) {
				ptr[j] = ptr[j-h];
				j = j - h;
				if (j <= (lo + h - 1)) break;
			}
			ptr[j] = v;
			i++;
			/*-- copy 3 --*/
			if (i > hi) break;
			v = ptr[i];
			j = i;
			while (mainGtU(state, ptr[j-h]+d, v+d)) {
				ptr[j] = ptr[j-h];
				j = j - h;
				if (j <= (lo + h - 1)) break;
			}
			ptr[j] = v;
			i++;
#endif
			if (state->budget < 0) return;
		}
	}
}


/*---------------------------------------------*/
/*
 * The following is an implementation of
 * an elegant 3-way quicksort for strings,
 * described in a paper "Fast Algorithms for
 * Sorting and Searching Strings", by Robert
 * Sedgewick and Jon L. Bentley.
 */

static
ALWAYS_INLINE
uint8_t mmed3(uint8_t a, uint8_t b, uint8_t c)
{
	uint8_t t;
	if (a > b) {
		t = a;
		a = b;
		b = t;
	}
	/* here b >= a */
	if (b > c) {
		b = c;
		if (a > b)
			b = a;
	}
	return b;
}

#define mpush(lz,hz,dz) \
{ \
	stackLo[sp] = lz; \
	stackHi[sp] = hz; \
	stackD [sp] = dz; \
	sp++; \
}

#define mpop(lz,hz,dz) \
{ \
	sp--; \
	lz = stackLo[sp]; \
	hz = stackHi[sp]; \
	dz = stackD [sp]; \
}

#define mnextsize(az) (nextHi[az] - nextLo[az])

#define mnextswap(az,bz) \
{ \
	int32_t tz; \
	tz = nextLo[az]; nextLo[az] = nextLo[bz]; nextLo[bz] = tz; \
	tz = nextHi[az]; nextHi[az] = nextHi[bz]; nextHi[bz] = tz; \
	tz = nextD [az]; nextD [az] = nextD [bz]; nextD [bz] = tz; \
}

#define MAIN_QSORT_SMALL_THRESH 20
#define MAIN_QSORT_DEPTH_THRESH (BZ_N_RADIX + BZ_N_QSORT)
#define MAIN_QSORT_STACK_SIZE   100

static NOINLINE
void mainQSort3(EState* state,
		int32_t   loSt,
		int32_t   hiSt
		/*int32_t dSt*/)
{
	enum { dSt = BZ_N_RADIX };
	int32_t unLo, unHi, ltLo, gtHi, n, m, med;
	int32_t sp, lo, hi, d;

	int32_t stackLo[MAIN_QSORT_STACK_SIZE];
	int32_t stackHi[MAIN_QSORT_STACK_SIZE];
	int32_t stackD [MAIN_QSORT_STACK_SIZE];

	int32_t nextLo[3];
	int32_t nextHi[3];
	int32_t nextD [3];

	uint32_t *const ptr   = state->ptr;
	uint8_t  *const block = state->block;

	sp = 0;
	mpush(loSt, hiSt, dSt);

	while (sp > 0) {
		AssertH(sp < MAIN_QSORT_STACK_SIZE - 2, 1001);

		mpop(lo, hi, d);
		if (hi - lo < MAIN_QSORT_SMALL_THRESH
		 || d > MAIN_QSORT_DEPTH_THRESH
		) {
			mainSimpleSort(state, lo, hi, d);
			if (state->budget < 0)
				return;
			continue;
		}
		med = (int32_t)	mmed3(block[ptr[lo          ] + d],
		                      block[ptr[hi          ] + d],
		                      block[ptr[(lo+hi) >> 1] + d]);

		unLo = ltLo = lo;
		unHi = gtHi = hi;

		while (1) {
			while (1) {
				if (unLo > unHi)
					break;
				n = ((int32_t)block[ptr[unLo]+d]) - med;
				if (n == 0) {
					mswap(ptr[unLo], ptr[ltLo]);
					ltLo++;
					unLo++;
					continue;
				}
				if (n > 0) break;
				unLo++;
			}
			while (1) {
				if (unLo > unHi)
					break;
				n = ((int32_t)block[ptr[unHi]+d]) - med;
				if (n == 0) {
					mswap(ptr[unHi], ptr[gtHi]);
					gtHi--;
					unHi--;
					continue;
				}
				if (n < 0) break;
				unHi--;
			}
			if (unLo > unHi)
				break;
			mswap(ptr[unLo], ptr[unHi]);
			unLo++;
			unHi--;
		}

		AssertD(unHi == unLo-1, "mainQSort3(2)");

		if (gtHi < ltLo) {
			mpush(lo, hi, d + 1);
			continue;
		}

		n = mmin(ltLo-lo, unLo-ltLo); mvswap(ptr, lo, unLo-n, n);
		m = mmin(hi-gtHi, gtHi-unHi); mvswap(ptr, unLo, hi-m+1, m);

		n = lo + unLo - ltLo - 1;
		m = hi - (gtHi - unHi) + 1;

		nextLo[0] = lo;  nextHi[0] = n;   nextD[0] = d;
		nextLo[1] = m;   nextHi[1] = hi;  nextD[1] = d;
		nextLo[2] = n+1; nextHi[2] = m-1; nextD[2] = d+1;

		if (mnextsize(0) < mnextsize(1)) mnextswap(0, 1);
		if (mnextsize(1) < mnextsize(2)) mnextswap(1, 2);
		if (mnextsize(0) < mnextsize(1)) mnextswap(0, 1);

		AssertD (mnextsize(0) >= mnextsize(1), "mainQSort3(8)");
		AssertD (mnextsize(1) >= mnextsize(2), "mainQSort3(9)");

		mpush(nextLo[0], nextHi[0], nextD[0]);
		mpush(nextLo[1], nextHi[1], nextD[1]);
		mpush(nextLo[2], nextHi[2], nextD[2]);
	}
}

#undef mpush
#undef mpop
#undef mnextsize
#undef mnextswap
#undef MAIN_QSORT_SMALL_THRESH
#undef MAIN_QSORT_DEPTH_THRESH
#undef MAIN_QSORT_STACK_SIZE


/*---------------------------------------------*/
/* Pre:
 *	nblock > N_OVERSHOOT
 *	block32 exists for [0 .. nblock-1 +N_OVERSHOOT]
 *	((uint8_t*)block32) [0 .. nblock-1] holds block
 *	ptr exists for [0 .. nblock-1]
 *
 * Post:
 *	((uint8_t*)block32) [0 .. nblock-1] holds block
 *	All other areas of block32 destroyed
 *	ftab[0 .. 65536] destroyed
 *	ptr [0 .. nblock-1] holds sorted order
 *	if (*budget < 0), sorting was abandoned
 */

#define BIGFREQ(b) (ftab[((b)+1) << 8] - ftab[(b) << 8])
#define SETMASK (1 << 21)
#define CLEARMASK (~(SETMASK))

static NOINLINE
void mainSort(EState* state)
{
	int32_t  i, j;
	Bool     bigDone[256];
	uint8_t  runningOrder[256];
	/* bbox: moved to EState to save stack
	int32_t  copyStart[256];
	int32_t  copyEnd  [256];
	*/
#define copyStart    (state->mainSort__copyStart)
#define copyEnd      (state->mainSort__copyEnd)

	uint32_t *const ptr      = state->ptr;
	uint8_t  *const block    = state->block;
	uint32_t *const ftab     = state->ftab;
	const int32_t   nblock   = state->nblock;
	uint16_t *const quadrant = state->quadrant;

	/*-- set up the 2-byte frequency table --*/
	/* was: for (i = 65536; i >= 0; i--) ftab[i] = 0; */
	memset(ftab, 0, 65537 * sizeof(ftab[0]));

	j = block[0] << 8;
	i = nblock - 1;
/* 3%, +300 bytes */
#if BZIP2_SPEED >= 2
	for (; i >= 3; i -= 4) {
		quadrant[i] = 0;
		j = (j >> 8) | (((unsigned)block[i]) << 8);
		ftab[j]++;
		quadrant[i-1] = 0;
		j = (j >> 8) | (((unsigned)block[i-1]) << 8);
		ftab[j]++;
		quadrant[i-2] = 0;
		j = (j >> 8) | (((unsigned)block[i-2]) << 8);
		ftab[j]++;
		quadrant[i-3] = 0;
		j = (j >> 8) | (((unsigned)block[i-3]) << 8);
		ftab[j]++;
	}
#endif
	for (; i >= 0; i--) {
		quadrant[i] = 0;
		j = (j >> 8) | (((unsigned)block[i]) << 8);
		ftab[j]++;
	}

	/*-- (emphasises close relationship of block & quadrant) --*/
	for (i = 0; i < BZ_N_OVERSHOOT; i++) {
		block   [nblock+i] = block[i];
		quadrant[nblock+i] = 0;
	}

	/*-- Complete the initial radix sort --*/
	j = ftab[0]; /* bbox: optimized */
	for (i = 1; i <= 65536; i++) {
		j += ftab[i];
		ftab[i] = j;
	}

	{
		unsigned s;
		s = block[0] << 8;
		i = nblock - 1;
#if BZIP2_SPEED >= 2
		for (; i >= 3; i -= 4) {
			s = (s >> 8) | (block[i] << 8);
			j = ftab[s] - 1;
			ftab[s] = j;
			ptr[j] = i;
			s = (s >> 8) | (block[i-1] << 8);
			j = ftab[s] - 1;
			ftab[s] = j;
			ptr[j] = i-1;
			s = (s >> 8) | (block[i-2] << 8);
			j = ftab[s] - 1;
			ftab[s] = j;
			ptr[j] = i-2;
			s = (s >> 8) | (block[i-3] << 8);
			j = ftab[s] - 1;
			ftab[s] = j;
			ptr[j] = i-3;
		}
#endif
		for (; i >= 0; i--) {
			s = (s >> 8) | (block[i] << 8);
			j = ftab[s] - 1;
			ftab[s] = j;
			ptr[j] = i;
		}
	}

	/*
	 * Now ftab contains the first loc of every small bucket.
	 * Calculate the running order, from smallest to largest
	 * big bucket.
	 */
	for (i = 0; i <= 255; i++) {
		bigDone     [i] = False;
		runningOrder[i] = i;
	}

	{
		/* bbox: was: int32_t h = 1; */
		/* do h = 3 * h + 1; while (h <= 256); */
		unsigned h = 364;

		do {
			/*h = h / 3;*/
			h = (h * 171) >> 9; /* bbox: fast h/3 */
			for (i = h; i <= 255; i++) {
				unsigned vv, jh;
				vv = runningOrder[i]; /* uint8[] */
				j = i;
				while (jh = j - h, BIGFREQ(runningOrder[jh]) > BIGFREQ(vv)) {
					runningOrder[j] = runningOrder[jh];
					j = jh;
					if (j < h)
						break;
				}
				runningOrder[j] = vv;
			}
		} while (h != 1);
	}

	/*
	 * The main sorting loop.
	 */

	for (i = 0; /*i <= 255*/; i++) {
		unsigned ss;

		/*
		 * Process big buckets, starting with the least full.
		 * Basically this is a 3-step process in which we call
		 * mainQSort3 to sort the small buckets [ss, j], but
		 * also make a big effort to avoid the calls if we can.
		 */
		ss = runningOrder[i];

		/*
		 * Step 1:
		 * Complete the big bucket [ss] by quicksorting
		 * any unsorted small buckets [ss, j], for j != ss.
		 * Hopefully previous pointer-scanning phases have already
		 * completed many of the small buckets [ss, j], so
		 * we don't have to sort them at all.
		 */
		for (j = 0; j <= 255; j++) {
			if (j != ss) {
				unsigned sb;
				sb = (ss << 8) + j;
				if (!(ftab[sb] & SETMASK)) {
					int32_t lo =  ftab[sb] /*& CLEARMASK (redundant)*/;
					int32_t hi = (ftab[sb+1] & CLEARMASK) - 1;
					if (hi > lo) {
						mainQSort3(state, lo, hi /*,BZ_N_RADIX*/);
						if (state->budget < 0) return;
					}
				}
				ftab[sb] |= SETMASK;
			}
		}

		AssertH(!bigDone[ss], 1006);

		/*
		 * Step 2:
		 * Now scan this big bucket [ss] so as to synthesise the
		 * sorted order for small buckets [t, ss] for all t,
		 * including, magically, the bucket [ss,ss] too.
		 * This will avoid doing Real Work in subsequent Step 1's.
		 */
		{
			for (j = 0; j <= 255; j++) {
				copyStart[j] =  ftab[(j << 8) + ss]     & CLEARMASK;
				copyEnd  [j] = (ftab[(j << 8) + ss + 1] & CLEARMASK) - 1;
			}
			for (j = ftab[ss << 8] & CLEARMASK; j < copyStart[ss]; j++) {
				unsigned c1;
				int32_t k;
				k = ptr[j] - 1;
				if (k < 0)
					k += nblock;
				c1 = block[k];
				if (!bigDone[c1])
					ptr[copyStart[c1]++] = k;
			}
			for (j = (ftab[(ss+1) << 8] & CLEARMASK) - 1; j > copyEnd[ss]; j--) {
				unsigned c1;
				int32_t k;
				k = ptr[j]-1;
				if (k < 0)
					k += nblock;
				c1 = block[k];
				if (!bigDone[c1])
					ptr[copyEnd[c1]--] = k;
			}
		}

		/* Extremely rare case missing in bzip2-1.0.0 and 1.0.1.
		 * Necessity for this case is demonstrated by compressing
		 * a sequence of approximately 48.5 million of character
		 * 251; 1.0.0/1.0.1 will then die here. */
		AssertH((copyStart[ss]-1 == copyEnd[ss]) \
		     || (copyStart[ss] == 0 && copyEnd[ss] == nblock-1), 1007);

		for (j = 0; j <= 255; j++)
			ftab[(j << 8) + ss] |= SETMASK;

		if (i == 255)
			break;

		/*
		 * Step 3:
		 * The [ss] big bucket is now done.  Record this fact,
		 * and update the quadrant descriptors.  Remember to
		 * update quadrants in the overshoot area too, if
		 * necessary.  The "if (i < 255)" test merely skips
		 * this updating for the last bucket processed, since
		 * updating for the last bucket is pointless.
		 *
		 * The quadrant array provides a way to incrementally
		 * cache sort orderings, as they appear, so as to
		 * make subsequent comparisons in fullGtU() complete
		 * faster.  For repetitive blocks this makes a big
		 * difference (but not big enough to be able to avoid
		 * the fallback sorting mechanism, exponential radix sort).
		 *
		 * The precise meaning is: at all times:
		 *
		 *	for 0 <= i < nblock and 0 <= j <= nblock
		 *
		 *	if block[i] != block[j],
		 *
		 *		then the relative values of quadrant[i] and
		 *			  quadrant[j] are meaningless.
		 *
		 *		else {
		 *			if quadrant[i] < quadrant[j]
		 *				then the string starting at i lexicographically
		 *				precedes the string starting at j
		 *
		 *			else if quadrant[i] > quadrant[j]
		 *				then the string starting at j lexicographically
		 *				precedes the string starting at i
		 *
		 *			else
		 *				the relative ordering of the strings starting
		 *				at i and j has not yet been determined.
		 *		}
		 */
		bigDone[ss] = True;

		{
			unsigned bbStart = ftab[ss << 8] & CLEARMASK;
			unsigned bbSize  = (ftab[(ss+1) << 8] & CLEARMASK) - bbStart;
			unsigned shifts  = 0;

			while ((bbSize >> shifts) > 65534) shifts++;

			for (j = bbSize-1; j >= 0; j--) {
				unsigned a2update  = ptr[bbStart + j]; /* uint32[] */
				uint16_t qVal      = (uint16_t)(j >> shifts);
				quadrant[a2update] = qVal;
				if (a2update < BZ_N_OVERSHOOT)
					quadrant[a2update + nblock] = qVal;
			}
			AssertH(((bbSize-1) >> shifts) <= 65535, 1002);
		}
	}
#undef runningOrder
#undef copyStart
#undef copyEnd
}

#undef BIGFREQ
#undef SETMASK
#undef CLEARMASK


/*---------------------------------------------*/
/* Pre:
 *	nblock > 0
 *	arr2 exists for [0 .. nblock-1 +N_OVERSHOOT]
 *	  ((uint8_t*)arr2)[0 .. nblock-1] holds block
 *	arr1 exists for [0 .. nblock-1]
 *
 * Post:
 *	((uint8_t*)arr2) [0 .. nblock-1] holds block
 *	All other areas of block destroyed
 *	ftab[0 .. 65536] destroyed
 *	arr1[0 .. nblock-1] holds sorted order
 */
static NOINLINE
int32_t BZ2_blockSort(EState* state)
{
	/* In original bzip2 1.0.4, it's a parameter, but 30
	 * (which was the default) should work ok. */
	enum { wfact = 30 };
	unsigned i;
	int32_t origPtr = origPtr;

	if (state->nblock >= 10000) {
		/* Calculate the location for quadrant, remembering to get
		 * the alignment right.  Assumes that &(block[0]) is at least
		 * 2-byte aligned -- this should be ok since block is really
		 * the first section of arr2.
		 */
		i = state->nblock + BZ_N_OVERSHOOT;
		if (i & 1)
			i++;
		state->quadrant = (uint16_t*) &(state->block[i]);

		/* (wfact-1) / 3 puts the default-factor-30
		 * transition point at very roughly the same place as
		 * with v0.1 and v0.9.0.
		 * Not that it particularly matters any more, since the
		 * resulting compressed stream is now the same regardless
		 * of whether or not we use the main sort or fallback sort.
		 */
		state->budget = state->nblock * ((wfact-1) / 3);
		mainSort(state);
		if (state->budget >= 0)
			goto good;
	}
	fallbackSort(state);
 good:

#if BZ_LIGHT_DEBUG
	origPtr = -1;
#endif
	for (i = 0; i < state->nblock; i++) {
		if (state->ptr[i] == 0) {
			origPtr = i;
			break;
		}
	}

	AssertH(origPtr != -1, 1003);
	return origPtr;
}


/*-------------------------------------------------------------*/
/*--- end                                       blocksort.c ---*/
/*-------------------------------------------------------------*/
