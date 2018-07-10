/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "tls.h"

/* The file is taken almost verbatim from matrixssl-3-7-2b-open/crypto/math/.
 * Changes are flagged with //bbox
 */

/**
 *	@file    pstm.c
 *	@version 33ef80f (HEAD, tag: MATRIXSSL-3-7-2-OPEN, tag: MATRIXSSL-3-7-2-COMM, origin/master, origin/HEAD, master)
 *
 *	Multiprecision number implementation.
 */
/*
 *	Copyright (c) 2013-2015 INSIDE Secure Corporation
 *	Copyright (c) PeerSec Networks, 2002-2011
 *	All Rights Reserved
 *
 *	The latest version of this code is available at http://www.matrixssl.org
 *
 *	This software is open source; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This General Public License does NOT permit incorporating this software
 *	into proprietary programs.  If you are unable to comply with the GPL, a
 *	commercial license for this software may be purchased from INSIDE at
 *	http://www.insidesecure.com/eng/Company/Locations
 *
 *	This program is distributed in WITHOUT ANY WARRANTY; without even the
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *	See the GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *	http://www.gnu.org/copyleft/gpl.html
 */
/******************************************************************************/

//bbox
//#include "../cryptoApi.h"
#ifndef DISABLE_PSTM

static int32 pstm_mul_2d(pstm_int *a, int b, pstm_int *c); //bbox: was int16 b

/******************************************************************************/
/*
	init an pstm_int for a given size
 */
int32 pstm_init_size(psPool_t *pool, pstm_int * a, uint32 size)
{
//bbox
//	uint16		x;

/*
	alloc mem
 */
	a->dp = xzalloc(sizeof (pstm_digit) * size);//bbox
//bbox	a->pool = pool;
	a->used  = 0;
	a->alloc = size;
	a->sign  = PSTM_ZPOS;
/*
	zero the digits
 */
//bbox
//	for (x = 0; x < size; x++) {
//		a->dp[x] = 0;
//	}
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	Init a new pstm_int.
*/
int32 pstm_init(psPool_t *pool, pstm_int * a)
{
//bbox
//	int32		i;
/*
	allocate memory required and clear it
 */
	a->dp = xzalloc(sizeof (pstm_digit) * PSTM_DEFAULT_INIT);//bbox
/*
	set the digits to zero
 */
//bbox
//	for (i = 0; i < PSTM_DEFAULT_INIT; i++) {
//		a->dp[i] = 0;
//	}
/*
	set the used to zero, allocated digits to the default precision and sign
	to positive
 */
//bbox	a->pool = pool;
	a->used  = 0;
	a->alloc = PSTM_DEFAULT_INIT;
	a->sign  = PSTM_ZPOS;

	return PSTM_OKAY;
}

/******************************************************************************/
/*
	Grow as required
 */
int32 pstm_grow(pstm_int * a, int size)
{
	int			i; //bbox: was int16
	pstm_digit		*tmp;

/*
	If the alloc size is smaller alloc more ram.
 */
	if (a->alloc < size) {
/*
		Reallocate the array a->dp

		We store the return in a temporary variable in case the operation
		failed we don't want to overwrite the dp member of a.
*/
		tmp = xrealloc(a->dp, sizeof (pstm_digit) * size);//bbox
/*
		reallocation succeeded so set a->dp
 */
		a->dp = tmp;
/*
		zero excess digits
 */
		i			= a->alloc;
		a->alloc	= size;
		for (; i < a->alloc; i++) {
			a->dp[i] = 0;
		}
	}
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	copy, b = a (b must be pre-allocated)
 */
int32 pstm_copy(pstm_int * a, pstm_int * b)
{
	int32	res, n;

/*
	If dst == src do nothing
 */
	if (a == b) {
		return PSTM_OKAY;
	}
/*
	Grow dest
 */
	if (b->alloc < a->used) {
		if ((res = pstm_grow (b, a->used)) != PSTM_OKAY) {
			return res;
		}
	}
/*
	Zero b and copy the parameters over
 */
	{
		register pstm_digit *tmpa, *tmpb;

		/* pointer aliases */
		/* source */
		tmpa = a->dp;

		/* destination */
		tmpb = b->dp;

		/* copy all the digits */
		for (n = 0; n < a->used; n++) {
			*tmpb++ = *tmpa++;
		}

		/* clear high digits */
		for (; n < b->used; n++) {
			*tmpb++ = 0;
		}
	}
/*
	copy used count and sign
 */
	b->used = a->used;
	b->sign = a->sign;
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	Trim unused digits

	This is used to ensure that leading zero digits are trimed and the
	leading "used" digit will be non-zero. Typically very fast.  Also fixes
	the sign if there are no more leading digits
*/
void pstm_clamp(pstm_int * a)
{
/*	decrease used while the most significant digit is zero. */
	while (a->used > 0 && a->dp[a->used - 1] == 0) {
		--(a->used);
	}
/*	reset the sign flag if used == 0 */
	if (a->used == 0) {
		a->sign = PSTM_ZPOS;
	}
}

/******************************************************************************/
/*
	clear one (frees).
 */
void pstm_clear(pstm_int * a)
{
	int32		i;
/*
	only do anything if a hasn't been freed previously
 */
	if (a != NULL && a->dp != NULL) {
/*
		first zero the digits
 */
		for (i = 0; i < a->used; i++) {
			a->dp[i] = 0;
		}

		psFree (a->dp, a->pool);
/*
		reset members to make debugging easier
 */
		a->dp		= NULL;
		a->alloc	= a->used = 0;
		a->sign		= PSTM_ZPOS;
	}
}

/******************************************************************************/
/*
	clear many (frees).
 */
void pstm_clear_multi(pstm_int *mp0, pstm_int *mp1, pstm_int *mp2,
					pstm_int *mp3, pstm_int *mp4, pstm_int *mp5,
					pstm_int *mp6, pstm_int *mp7)
{
	int32		n;		/* Number of ok inits */

	pstm_int	*tempArray[9];

	tempArray[0] = mp0;
	tempArray[1] = mp1;
	tempArray[2] = mp2;
	tempArray[3] = mp3;
	tempArray[4] = mp4;
	tempArray[5] = mp5;
	tempArray[6] = mp6;
	tempArray[7] = mp7;
	tempArray[8] = NULL;

	for (n = 0; tempArray[n] != NULL; n++) {
		if ((tempArray[n] != NULL) && (tempArray[n]->dp != NULL)) {
			pstm_clear(tempArray[n]);
		}
	}
}

/******************************************************************************/
/*
	Set to zero.
 */
void pstm_zero(pstm_int * a)
{
	int32		n;
	pstm_digit	*tmp;

	a->sign = PSTM_ZPOS;
	a->used = 0;

	tmp = a->dp;
	for (n = 0; n < a->alloc; n++) {
		*tmp++ = 0;
	}
}


/******************************************************************************/
/*
	Compare maginitude of two ints (unsigned).
 */
int32 pstm_cmp_mag(pstm_int * a, pstm_int * b)
{
	int		n; //bbox: was int16
	pstm_digit	*tmpa, *tmpb;

/*
	compare based on # of non-zero digits
 */
	if (a->used > b->used) {
		return PSTM_GT;
	}

	if (a->used < b->used) {
		return PSTM_LT;
	}

	/* alias for a */
	tmpa = a->dp + (a->used - 1);

	/* alias for b */
	tmpb = b->dp + (a->used - 1);

/*
	compare based on digits
 */
	for (n = 0; n < a->used; ++n, --tmpa, --tmpb) {
		if (*tmpa > *tmpb) {
			return PSTM_GT;
		}
		if (*tmpa < *tmpb) {
			return PSTM_LT;
		}
	}
	return PSTM_EQ;
}

/******************************************************************************/
/*
	Compare two ints (signed)
 */
int32 pstm_cmp(pstm_int * a, pstm_int * b)
{
/*
	compare based on sign
 */
	if (a->sign != b->sign) {
		if (a->sign == PSTM_NEG) {
			return PSTM_LT;
		} else {
			return PSTM_GT;
		}
	}
/*
	compare digits
 */
	if (a->sign == PSTM_NEG) {
		/* if negative compare opposite direction */
		return pstm_cmp_mag(b, a);
	} else {
		return pstm_cmp_mag(a, b);
	}
}

/******************************************************************************/
/*
	pstm_ints can be initialized more precisely when they will populated
	using pstm_read_unsigned_bin since the length of the byte stream is known
*/
int32 pstm_init_for_read_unsigned_bin(psPool_t *pool, pstm_int *a, uint32 len)
{
	int32 size;
/*
	Need to set this based on how many words max it will take to store the bin.
	The magic + 2:
		1 to round up for the remainder of this integer math
		1 for the initial carry of '1' bits that fall between DIGIT_BIT and 8
*/
	size = (((len / sizeof(pstm_digit)) * (sizeof(pstm_digit) * CHAR_BIT))
		/ DIGIT_BIT) + 2;
	return pstm_init_size(pool, a, size);
}


/******************************************************************************/
/*
	Reads a unsigned char array into pstm_int format.  User should have
	called pstm_init_for_read_unsigned_bin first.  There is some grow logic
	here if the default pstm_init was used but we don't really want to hit it.
*/
int32 pstm_read_unsigned_bin(pstm_int *a, unsigned char *b, int32 c)
{
	/* zero the int */
	pstm_zero (a);

/*
	If we know the endianness of this architecture, and we're using
	32-bit pstm_digits, we can optimize this
*/
#if (defined(ENDIAN_LITTLE) || defined(ENDIAN_BIG)) && !defined(PSTM_64BIT)
  /* But not for both simultaneously */
#if defined(ENDIAN_LITTLE) && defined(ENDIAN_BIG)
#error Both ENDIAN_LITTLE and ENDIAN_BIG defined.
#endif
	{
		unsigned char *pd;
		if ((unsigned)c > (PSTM_MAX_SIZE * sizeof(pstm_digit))) {
			uint32 excess = c - (PSTM_MAX_SIZE * sizeof(pstm_digit));
			c -= excess;
			b += excess;
		}
		a->used = ((c + sizeof(pstm_digit) - 1)/sizeof(pstm_digit));
		if (a->alloc < a->used) {
			if (pstm_grow(a, a->used) != PSTM_OKAY) {
				return PSTM_MEM;
			}
		}
		pd = (unsigned char *)a->dp;
		/* read the bytes in */
#ifdef ENDIAN_BIG
		{
			/* Use Duff's device to unroll the loop. */
			int32 idx = (c - 1) & ~3;
			switch (c % 4) {
				case 0:	do { pd[idx+0] = *b++;
					case 3:	     pd[idx+1] = *b++;
					case 2:	     pd[idx+2] = *b++;
					case 1:	     pd[idx+3] = *b++;
					idx -= 4;
				} while ((c -= 4) > 0);
			}
		}
#else
		for (c -= 1; c >= 0; c -= 1) {
			pd[c] = *b++;
		}
#endif
	}
#else
	/* Big enough based on the len? */
	a->used = (((c / sizeof(pstm_digit)) * (sizeof(pstm_digit) * CHAR_BIT))
		/ DIGIT_BIT) + 2;

	if (a->alloc < a->used) {
		if (pstm_grow(a, a->used) != PSTM_OKAY) {
			return PSTM_MEM;
		}
	}
	/* read the bytes in */
	for (; c > 0; c--) {
		if (pstm_mul_2d (a, 8, a) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
		a->dp[0] |= *b++;
		a->used += 1;
	}
#endif

	pstm_clamp (a);
	return PS_SUCCESS;
}

/******************************************************************************/
/*
*/
int pstm_count_bits (pstm_int * a)
{
	int     r; //bbox: was int16
	pstm_digit q;

	if (a->used == 0) {
		return 0;
	}

	/* get number of digits and add that */
	r = (a->used - 1) * DIGIT_BIT;

	/* take the last digit and count the bits in it */
	q = a->dp[a->used - 1];
	while (q > ((pstm_digit) 0)) {
		++r;
		q >>= ((pstm_digit) 1);
	}
	return r;
}

/******************************************************************************/
int32 pstm_unsigned_bin_size(pstm_int *a)
{
	int32     size = pstm_count_bits (a);
	return (size / 8 + ((size & 7) != 0 ? 1 : 0));
}

/******************************************************************************/
void pstm_set(pstm_int *a, pstm_digit b)
{
   pstm_zero(a);
   a->dp[0] = b;
   a->used  = a->dp[0] ? 1 : 0;
}

/******************************************************************************/
/*
	Right shift
*/
void pstm_rshd(pstm_int *a, int x)
{
	int y; //bbox: was int16

	/* too many digits just zero and return */
	if (x >= a->used) {
		pstm_zero(a);
		return;
	}

	/* shift */
	for (y = 0; y < a->used - x; y++) {
		a->dp[y] = a->dp[y+x];
	}

	/* zero rest */
	for (; y < a->used; y++) {
		a->dp[y] = 0;
	}

	/* decrement count */
	a->used -= x;
	pstm_clamp(a);
}

/******************************************************************************/
/*
	Shift left a certain amount of digits.
 */
int32 pstm_lshd(pstm_int * a, int b)
{
	int	x; //bbox: was int16
	int32	res;

/*
	If its less than zero return.
 */
	if (b <= 0) {
		return PSTM_OKAY;
	}
/*
	Grow to fit the new digits.
 */
	if (a->alloc < a->used + b) {
		if ((res = pstm_grow (a, a->used + b)) != PSTM_OKAY) {
			return res;
		}
	}

	{
		register pstm_digit *top, *bottom;
/*
		Increment the used by the shift amount then copy upwards.
 */
		a->used += b;

		/* top */
		top = a->dp + a->used - 1;

		/* base */
		bottom = a->dp + a->used - 1 - b;
/*
		This is implemented using a sliding window except the window goes the
		other way around.  Copying from the bottom to the top.
 */
		for (x = a->used - 1; x >= b; x--) {
			*top-- = *bottom--;
		}

		/* zero the lower digits */
		top = a->dp;
		for (x = 0; x < b; x++) {
			*top++ = 0;
		}
	}
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	computes a = 2**b
*/
int32 pstm_2expt(pstm_int *a, int b)
{
	int     z; //bbox: was int16

   /* zero a as per default */
	pstm_zero (a);

	if (b < 0) {
		return PSTM_OKAY;
	}

	z = b / DIGIT_BIT;
	if (z >= PSTM_MAX_SIZE) {
		return PS_LIMIT_FAIL;
	}

	/* set the used count of where the bit will go */
	a->used = z + 1;

	if (a->used > a->alloc) {
		if (pstm_grow(a, a->used) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}

	/* put the single bit in its place */
	a->dp[z] = ((pstm_digit)1) << (b % DIGIT_BIT);
	return PSTM_OKAY;
}

/******************************************************************************/
/*

*/
int32 pstm_mul_2(pstm_int * a, pstm_int * b)
{
	int32	res;
	int	x, oldused; //bbox: was int16

/*
	grow to accomodate result
 */
	if (b->alloc < a->used + 1) {
		if ((res = pstm_grow (b, a->used + 1)) != PSTM_OKAY) {
			return res;
		}
	}
	oldused = b->used;
	b->used = a->used;

	{
		register pstm_digit r, rr, *tmpa, *tmpb;

		/* alias for source */
		tmpa = a->dp;

		/* alias for dest */
		tmpb = b->dp;

		/* carry */
		r = 0;
		for (x = 0; x < a->used; x++) {
/*
			get what will be the *next* carry bit from the
			MSB of the current digit
*/
			rr = *tmpa >> ((pstm_digit)(DIGIT_BIT - 1));
/*
			now shift up this digit, add in the carry [from the previous]
*/
			*tmpb++ = ((*tmpa++ << ((pstm_digit)1)) | r);
/*
			copy the carry that would be from the source
			digit into the next iteration
*/
			r = rr;
		}

		/* new leading digit? */
		if (r != 0 && b->used != (PSTM_MAX_SIZE-1)) {
			/* add a MSB which is always 1 at this point */
			*tmpb = 1;
			++(b->used);
		}
/*
		now zero any excess digits on the destination that we didn't write to
*/
		tmpb = b->dp + b->used;
		for (x = b->used; x < oldused; x++) {
			*tmpb++ = 0;
		}
	}
	b->sign = a->sign;
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	unsigned subtraction ||a|| >= ||b|| ALWAYS!
*/
int32 s_pstm_sub(pstm_int *a, pstm_int *b, pstm_int *c)
{
	int		oldbused, oldused; //bbox: was int16
	int32		x;
	pstm_word	t;

	if (b->used > a->used) {
		return PS_LIMIT_FAIL;
	}
	if (c->alloc < a->used) {
		if ((x = pstm_grow (c, a->used)) != PSTM_OKAY) {
			return x;
		}
	}
	oldused  = c->used;
	oldbused = b->used;
	c->used  = a->used;
	t = 0;

	for (x = 0; x < oldbused; x++) {
		t = ((pstm_word)a->dp[x]) - (((pstm_word)b->dp[x]) + t);
		c->dp[x] = (pstm_digit)t;
		t = (t >> DIGIT_BIT)&1;
	}
	for (; x < a->used; x++) {
		t = ((pstm_word)a->dp[x]) - t;
		c->dp[x] = (pstm_digit)t;
		t = (t >> DIGIT_BIT);
	}
	for (; x < oldused; x++) {
		c->dp[x] = 0;
	}
	pstm_clamp(c);
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	unsigned addition
*/
static int32 s_pstm_add(pstm_int *a, pstm_int *b, pstm_int *c)
{
	int				x, y, oldused; //bbox: was int16
	register pstm_word	t, adp, bdp;

	y = a->used;
	if (b->used > y) {
		y = b->used;
	}
	oldused = c->used;
	c->used = y;

	if (c->used > c->alloc) {
		if (pstm_grow(c, c->used) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}

	t = 0;
	for (x = 0; x < y; x++) {
		if (a->used < x) {
			adp = 0;
		} else {
			adp = (pstm_word)a->dp[x];
		}
		if (b->used < x) {
			bdp = 0;
		} else {
			bdp = (pstm_word)b->dp[x];
		}
		t         += (adp) + (bdp);
		c->dp[x]   = (pstm_digit)t;
		t        >>= DIGIT_BIT;
	}
	if (t != 0 && x < PSTM_MAX_SIZE) {
		if (c->used == c->alloc) {
			if (pstm_grow(c, c->alloc + 1) != PSTM_OKAY) {
				return PS_MEM_FAIL;
			}
		}
		c->dp[c->used++] = (pstm_digit)t;
		++x;
	}

	c->used = x;
	for (; x < oldused; x++) {
		c->dp[x] = 0;
	}
	pstm_clamp(c);
	return PSTM_OKAY;
}


/******************************************************************************/
/*

*/
int32 pstm_sub(pstm_int *a, pstm_int *b, pstm_int *c)
{
	int32	res;
	int	sa, sb; //bbox: was int16

	sa = a->sign;
	sb = b->sign;

	if (sa != sb) {
/*
		subtract a negative from a positive, OR a positive from a negative.
		For both, ADD their magnitudes, and use the sign of the first number.
 */
		c->sign = sa;
		if ((res = s_pstm_add (a, b, c)) != PSTM_OKAY) {
			return res;
		}
	} else {
/*
		subtract a positive from a positive, OR a negative from a negative.
		First, take the difference between their magnitudes, then...
 */
		if (pstm_cmp_mag (a, b) != PSTM_LT) {
			/* Copy the sign from the first */
			c->sign = sa;
			/* The first has a larger or equal magnitude */
			if ((res = s_pstm_sub (a, b, c)) != PSTM_OKAY) {
				return res;
			}
		} else {
			/* The result has the _opposite_ sign from the first number. */
			c->sign = (sa == PSTM_ZPOS) ? PSTM_NEG : PSTM_ZPOS;
			/* The second has a larger magnitude */
			if ((res = s_pstm_sub (b, a, c)) != PSTM_OKAY) {
				return res;
			}
		}
	}
	return PS_SUCCESS;
}

/******************************************************************************/
/*
	c = a - b
*/
int32 pstm_sub_d(psPool_t *pool, pstm_int *a, pstm_digit b, pstm_int *c)
{
	pstm_int	tmp;
	int32		res;

	if (pstm_init_size(pool, &tmp, sizeof(pstm_digit)) != PSTM_OKAY) {
		return PS_MEM_FAIL;
	}
	pstm_set(&tmp, b);
	res = pstm_sub(a, &tmp, c);
	pstm_clear(&tmp);
	return res;
}

/******************************************************************************/
/*
	setups the montgomery reduction
*/
int32 pstm_montgomery_setup(pstm_int *a, pstm_digit *rho)
{
	pstm_digit x, b;

/*
	fast inversion mod 2**k
	Based on the fact that
	XA = 1 (mod 2**n)	=>  (X(2-XA)) A = 1 (mod 2**2n)
						=>  2*X*A - X*X*A*A = 1
						=>  2*(1) - (1)     = 1
 */
	b = a->dp[0];

	if ((b & 1) == 0) {
		psTraceCrypto("pstm_montogomery_setup failure\n");
		return PS_ARG_FAIL;
	}

	x = (((b + 2) & 4) << 1) + b; /* here x*a==1 mod 2**4 */
	x *= 2 - b * x;               /* here x*a==1 mod 2**8 */
	x *= 2 - b * x;               /* here x*a==1 mod 2**16 */
	x *= 2 - b * x;               /* here x*a==1 mod 2**32 */
#ifdef PSTM_64BIT
	x *= 2 - b * x;               /* here x*a==1 mod 2**64 */
#endif
	/* rho = -1/m mod b */
	*rho = (pstm_digit)(((pstm_word) 1 << ((pstm_word) DIGIT_BIT)) -
		((pstm_word)x));
	return PSTM_OKAY;
}

/******************************************************************************/
/*
 *	computes a = B**n mod b without division or multiplication useful for
 *	normalizing numbers in a Montgomery system.
 */
int32 pstm_montgomery_calc_normalization(pstm_int *a, pstm_int *b)
{
	int32     x;
	int	bits; //bbox: was int16

	/* how many bits of last digit does b use */
	bits = pstm_count_bits (b) % DIGIT_BIT;
	if (!bits) bits = DIGIT_BIT;

	/* compute A = B^(n-1) * 2^(bits-1) */
	if (b->used > 1) {
		if ((x = pstm_2expt (a, (b->used - 1) * DIGIT_BIT + bits - 1)) !=
				PSTM_OKAY) {
			return x;
		}
	} else {
		pstm_set(a, 1);
		bits = 1;
	}

	/* now compute C = A * B mod b */
	for (x = bits - 1; x < (int32)DIGIT_BIT; x++) {
		if (pstm_mul_2 (a, a) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
		if (pstm_cmp_mag (a, b) != PSTM_LT) {
			if (s_pstm_sub (a, b, a) != PSTM_OKAY) {
				return PS_MEM_FAIL;
			}
		}
	}
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	c = a * 2**d
*/
static int32 pstm_mul_2d(pstm_int *a, int b, pstm_int *c)
{
	pstm_digit	carry, carrytmp, shift;
	int		x; //bbox: was int16

	/* copy it */
	if (pstm_copy(a, c) != PSTM_OKAY) {
		return PS_MEM_FAIL;
	}

	/* handle whole digits */
	if (b >= DIGIT_BIT) {
		if (pstm_lshd(c, b/DIGIT_BIT) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}
	b %= DIGIT_BIT;

	/* shift the digits */
	if (b != 0) {
		carry = 0;
		shift = DIGIT_BIT - b;
		for (x = 0; x < c->used; x++) {
			carrytmp = c->dp[x] >> shift;
			c->dp[x] = (c->dp[x] << b) + carry;
			carry = carrytmp;
		}
		/* store last carry if room */
		if (carry && x < PSTM_MAX_SIZE) {
			if (c->used == c->alloc) {
				if (pstm_grow(c, c->alloc + 1) != PSTM_OKAY) {
					return PS_MEM_FAIL;
				}
			}
			c->dp[c->used++] = carry;
		}
	}
	pstm_clamp(c);
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	c = a mod 2**d
*/
static int32 pstm_mod_2d(pstm_int *a, int b, pstm_int *c) //bbox: was int16 b
{
	int	x; //bbox: was int16

	/* zero if count less than or equal to zero */
	if (b <= 0) {
		pstm_zero(c);
		return PSTM_OKAY;
	}

	/* get copy of input */
	if (pstm_copy(a, c) != PSTM_OKAY) {
		return PS_MEM_FAIL;
	}

	/* if 2**d is larger than we just return */
	if (b >= (DIGIT_BIT * a->used)) {
		return PSTM_OKAY;
	}

	/* zero digits above the last digit of the modulus */
	for (x = (b / DIGIT_BIT) + ((b % DIGIT_BIT) == 0 ? 0 : 1); x < c->used; x++)
	{
		c->dp[x] = 0;
	}
	/* clear the digit that is not completely outside/inside the modulus */
	c->dp[b / DIGIT_BIT] &= ~((pstm_digit)0) >> (DIGIT_BIT - b);
	pstm_clamp (c);
	return PSTM_OKAY;
}


/******************************************************************************/
/*
	c = a * b
*/
int32 pstm_mul_d(pstm_int *a, pstm_digit b, pstm_int *c)
{
	pstm_word	w;
	int32		res;
	int		x, oldused; //bbox: was int16

	if (c->alloc < a->used + 1) {
		if ((res = pstm_grow (c, a->used + 1)) != PSTM_OKAY) {
			return res;
		}
	}
	oldused = c->used;
	c->used = a->used;
	c->sign = a->sign;
	w       = 0;
	for (x = 0; x < a->used; x++) {
		w         = ((pstm_word)a->dp[x]) * ((pstm_word)b) + w;
		c->dp[x]  = (pstm_digit)w;
		w         = w >> DIGIT_BIT;
	}
	if (w != 0 && (a->used != PSTM_MAX_SIZE)) {
		c->dp[c->used++] = (pstm_digit)w;
		++x;
	}
	for (; x < oldused; x++) {
		c->dp[x] = 0;
	}
	pstm_clamp(c);
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	c = a / 2**b
*/
int32 pstm_div_2d(psPool_t *pool, pstm_int *a, int b, pstm_int *c,
					pstm_int *d)
{
	pstm_digit	D, r, rr;
	int32		res;
	int		x; //bbox: was int16
	pstm_int	t;

	/* if the shift count is <= 0 then we do no work */
	if (b <= 0) {
		if (pstm_copy (a, c) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
		if (d != NULL) {
			pstm_zero (d);
		}
		return PSTM_OKAY;
	}

	/* get the remainder */
	if (d != NULL) {
		if (pstm_init(pool, &t) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
		if (pstm_mod_2d (a, b, &t) != PSTM_OKAY) {
			res = PS_MEM_FAIL;
			goto LBL_DONE;
		}
	}

	/* copy */
	if (pstm_copy(a, c) != PSTM_OKAY) {
		res = PS_MEM_FAIL;
		goto LBL_DONE;
	}

	/* shift by as many digits in the bit count */
	if (b >= (int32)DIGIT_BIT) {
		pstm_rshd (c, b / DIGIT_BIT);
	}

	/* shift any bit count < DIGIT_BIT */
	D = (pstm_digit) (b % DIGIT_BIT);
	if (D != 0) {
		register pstm_digit *tmpc, mask, shift;

		/* mask */
		mask = (((pstm_digit)1) << D) - 1;

		/* shift for lsb */
		shift = DIGIT_BIT - D;

		/* alias */
		tmpc = c->dp + (c->used - 1);

		/* carry */
		r = 0;
		for (x = c->used - 1; x >= 0; x--) {
			/* get the lower  bits of this word in a temp */
			rr = *tmpc & mask;

			/* shift the current word and mix in the carry bits from previous */
			*tmpc = (*tmpc >> D) | (r << shift);
			--tmpc;

			/* set the carry to the carry bits of the current word above */
			r = rr;
		}
	}
	pstm_clamp (c);

	res = PSTM_OKAY;
LBL_DONE:
	if (d != NULL) {
		if (pstm_copy(&t, d) != PSTM_OKAY) {
			res = PS_MEM_FAIL;
		}
		pstm_clear(&t);
	}
	return res;
}

/******************************************************************************/
/*
	b = a/2
*/
int32 pstm_div_2(pstm_int * a, pstm_int * b)
{
	int	x, oldused; //bbox: was int16

	if (b->alloc < a->used) {
		if (pstm_grow(b, a->used) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}
	oldused = b->used;
	b->used = a->used;
	{
		register pstm_digit r, rr, *tmpa, *tmpb;

		/* source alias */
		tmpa = a->dp + b->used - 1;

		/* dest alias */
		tmpb = b->dp + b->used - 1;

		/* carry */
		r = 0;
		for (x = b->used - 1; x >= 0; x--) {
			/* get the carry for the next iteration */
			rr = *tmpa & 1;

			/* shift the current digit, add in carry and store */
			*tmpb-- = (*tmpa-- >> 1) | (r << (DIGIT_BIT - 1));

			/* forward carry to next iteration */
			r = rr;
		}

		/* zero excess digits */
		tmpb = b->dp + b->used;
		for (x = b->used; x < oldused; x++) {
			*tmpb++ = 0;
		}
	}
	b->sign = a->sign;
	pstm_clamp (b);
	return PSTM_OKAY;
}

/******************************************************************************/
/*
	Creates "a" then copies b into it
 */
int32 pstm_init_copy(psPool_t *pool, pstm_int * a, pstm_int * b, int toSqr)
{
	int	x; //bbox: was int16
	int32	res;

	if (a == b) {
		return PSTM_OKAY;
	}
	x = b->alloc;

	if (toSqr) {
/*
		Smart-size:  Increasing size of a if b->used is roughly half
		of b->alloc because usage has shown that a lot of these copies
		go on to be squared and need these extra digits
*/
		if ((b->used * 2) + 2 >= x) {
			x = (b->used * 2) + 3;
		}
	}
	if ((res = pstm_init_size(pool, a, x)) != PSTM_OKAY) {
		return res;
	}
	return pstm_copy(b, a);
}

/******************************************************************************/
/*
	With some compilers, we have seen issues linking with the builtin
	64 bit division routine. The issues with either manifest in a failure
	to find 'udivdi3' at link time, or a runtime invalid instruction fault
	during an RSA operation.
	The routine below divides a 64 bit unsigned int by a 32 bit unsigned int
	explicitly, rather than using the division operation
		The 64 bit result is placed in the 'numerator' parameter
		The 32 bit mod (remainder) of the division is the return parameter
	Based on implementations by:
		Copyright (C) 2003 Bernardo Innocenti <bernie@develer.com>
		Copyright (C) 1999 Hewlett-Packard Co
		Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
*/
#if defined(USE_MATRIX_DIV64) && defined(PSTM_32BIT)
static uint32 psDiv64(uint64 *numerator, uint32 denominator)
{
	uint64	rem = *numerator;
	uint64	b = denominator;
	uint64	res = 0;
	uint64	d = 1;
	uint32	high = rem >> 32;

	if (high >= denominator) {
		high /= denominator;
		res = (uint64) high << 32;
		rem -= (uint64) (high * denominator) << 32;
	}
	while ((int64)b > 0 && b < rem) {
		b = b+b;
		d = d+d;
	}
	do {
		if (rem >= b) {
			rem -= b;
			res += d;
		}
		b >>= 1;
		d >>= 1;
	} while (d);
	*numerator = res;
	return rem;
}
#endif /* USE_MATRIX_DIV64 */

#if defined(USE_MATRIX_DIV128) && defined(PSTM_64BIT)
typedef unsigned long	uint128 __attribute__ ((mode(TI)));
static uint64 psDiv128(uint128 *numerator, uint64 denominator)
{
	uint128	rem = *numerator;
	uint128	b = denominator;
	uint128	res = 0;
	uint128	d = 1;
	uint64	high = rem >> 64;

	if (high >= denominator) {
		high /= denominator;
		res = (uint128) high << 64;
		rem -= (uint128) (high * denominator) << 64;
	}
	while ((uint128)b > 0 && b < rem) {
		b = b+b;
		d = d+d;
	}
	do {
		if (rem >= b) {
			rem -= b;
			res += d;
		}
		b >>= 1;
		d >>= 1;
	} while (d);
	*numerator = res;
	return rem;
}
#endif /* USE_MATRIX_DIV128 */

/******************************************************************************/
/*
	a/b => cb + d == a
*/
int32 pstm_div(psPool_t *pool, pstm_int *a, pstm_int *b, pstm_int *c,
				pstm_int *d)
{
	pstm_int	q, x, y, t1, t2;
	int32		res;
	int		n, t, i, norm, neg; //bbox: was int16

	/* is divisor zero ? */
	if (pstm_iszero (b) == 1) {
		return PS_LIMIT_FAIL;
	}

	/* if a < b then q=0, r = a */
	if (pstm_cmp_mag (a, b) == PSTM_LT) {
		if (d != NULL) {
			if (pstm_copy(a, d) != PSTM_OKAY) {
				return PS_MEM_FAIL;
			}
		}
		if (c != NULL) {
			pstm_zero (c);
		}
		return PSTM_OKAY;
	}
/*
	Smart-size inits
*/
	if ((res = pstm_init_size(pool, &t1, a->alloc)) != PSTM_OKAY) {
		return res;
	}
	if ((res = pstm_init_size(pool, &t2, 3)) != PSTM_OKAY) {
		goto LBL_T1;
	}
	if ((res = pstm_init_copy(pool, &x, a, 0)) != PSTM_OKAY) {
		goto LBL_T2;
	}
/*
	Used to be an init_copy on b but pstm_grow was always hit with triple size
*/
	if ((res = pstm_init_size(pool, &y, b->used * 3)) != PSTM_OKAY) {
		goto LBL_X;
	}
	if ((res = pstm_copy(b, &y)) != PSTM_OKAY) {
		goto LBL_Y;
	}

	/* fix the sign */
	neg = (a->sign == b->sign) ? PSTM_ZPOS : PSTM_NEG;
	x.sign = y.sign = PSTM_ZPOS;

	/* normalize both x and y, ensure that y >= b/2, [b == 2**DIGIT_BIT] */
	norm = pstm_count_bits(&y) % DIGIT_BIT;
	if (norm < (int32)(DIGIT_BIT-1)) {
		norm = (DIGIT_BIT-1) - norm;
		if ((res = pstm_mul_2d(&x, norm, &x)) != PSTM_OKAY) {
			goto LBL_Y;
		}
		if ((res = pstm_mul_2d(&y, norm, &y)) != PSTM_OKAY) {
			goto LBL_Y;
		}
	} else {
		norm = 0;
	}

	/* note hac does 0 based, so if used==5 then its 0,1,2,3,4, e.g. use 4 */
	n = x.used - 1;
	t = y.used - 1;

	if ((res = pstm_init_size(pool, &q, n - t + 1)) != PSTM_OKAY) {
		goto LBL_Y;
	}
	q.used = n - t + 1;

	/* while (x >= y*b**n-t) do { q[n-t] += 1; x -= y*b**{n-t} } */
	if ((res = pstm_lshd(&y, n - t)) != PSTM_OKAY) { /* y = y*b**{n-t} */
		goto LBL_Q;
	}

	while (pstm_cmp (&x, &y) != PSTM_LT) {
		++(q.dp[n - t]);
		if ((res = pstm_sub(&x, &y, &x)) != PSTM_OKAY) {
			goto LBL_Q;
		}
	}

	/* reset y by shifting it back down */
	pstm_rshd (&y, n - t);

	/* step 3. for i from n down to (t + 1) */
	for (i = n; i >= (t + 1); i--) {
		if (i > x.used) {
			continue;
		}

		/* step 3.1 if xi == yt then set q{i-t-1} to b-1,
		 * otherwise set q{i-t-1} to (xi*b + x{i-1})/yt */
		if (x.dp[i] == y.dp[t]) {
			q.dp[i - t - 1] = (pstm_digit)((((pstm_word)1) << DIGIT_BIT) - 1);
		} else {
			pstm_word tmp;
			tmp = ((pstm_word) x.dp[i]) << ((pstm_word) DIGIT_BIT);
			tmp |= ((pstm_word) x.dp[i - 1]);
#if defined(USE_MATRIX_DIV64) && defined(PSTM_32BIT)
			psDiv64(&tmp, y.dp[t]);
#elif defined(USE_MATRIX_DIV128) && defined(PSTM_64BIT)
			psDiv128(&tmp, y.dp[t]);
#else
			tmp /= ((pstm_word) y.dp[t]);
#endif /* USE_MATRIX_DIV64 */
			q.dp[i - t - 1] = (pstm_digit) (tmp);
		}

		/* while (q{i-t-1} * (yt * b + y{t-1})) >
			 xi * b**2 + xi-1 * b + xi-2

			do q{i-t-1} -= 1;
		*/
		q.dp[i - t - 1] = (q.dp[i - t - 1] + 1);
		do {
			q.dp[i - t - 1] = (q.dp[i - t - 1] - 1);

			/* find left hand */
			pstm_zero (&t1);
			t1.dp[0] = (t - 1 < 0) ? 0 : y.dp[t - 1];
			t1.dp[1] = y.dp[t];
			t1.used = 2;
			if ((res = pstm_mul_d (&t1, q.dp[i - t - 1], &t1)) != PSTM_OKAY) {
				goto LBL_Q;
			}

			/* find right hand */
			t2.dp[0] = (i - 2 < 0) ? 0 : x.dp[i - 2];
			t2.dp[1] = (i - 1 < 0) ? 0 : x.dp[i - 1];
			t2.dp[2] = x.dp[i];
			t2.used = 3;
		} while (pstm_cmp_mag(&t1, &t2) == PSTM_GT);

		/* step 3.3 x = x - q{i-t-1} * y * b**{i-t-1} */
		if ((res = pstm_mul_d(&y, q.dp[i - t - 1], &t1)) != PSTM_OKAY) {
			goto LBL_Q;
		}

		if ((res = pstm_lshd(&t1, i - t - 1)) != PSTM_OKAY) {
			goto LBL_Q;
		}

		if ((res = pstm_sub(&x, &t1, &x)) != PSTM_OKAY) {
			goto LBL_Q;
		}

		/* if x < 0 then { x = x + y*b**{i-t-1}; q{i-t-1} -= 1; } */
		if (x.sign == PSTM_NEG) {
			if ((res = pstm_copy(&y, &t1)) != PSTM_OKAY) {
				goto LBL_Q;
			}
			if ((res = pstm_lshd (&t1, i - t - 1)) != PSTM_OKAY) {
				goto LBL_Q;
			}
			if ((res = pstm_add (&x, &t1, &x)) != PSTM_OKAY) {
				goto LBL_Q;
			}
			q.dp[i - t - 1] = q.dp[i - t - 1] - 1;
		}
	}
/*
	now q is the quotient and x is the remainder (which we have to normalize)
*/
	/* get sign before writing to c */
	x.sign = x.used == 0 ? PSTM_ZPOS : a->sign;

	if (c != NULL) {
		pstm_clamp (&q);
		if (pstm_copy (&q, c) != PSTM_OKAY) {
			res = PS_MEM_FAIL;
			goto LBL_Q;
		}
		c->sign = neg;
	}

	if (d != NULL) {
		if ((res = pstm_div_2d (pool, &x, norm, &x, NULL)) != PSTM_OKAY) {
			goto LBL_Q;
		}
/*
		the following is a kludge, essentially we were seeing the right
		remainder but with excess digits that should have been zero
 */
		for (i = b->used; i < x.used; i++) {
			x.dp[i] = 0;
		}
		pstm_clamp(&x);
		if (pstm_copy (&x, d) != PSTM_OKAY) {
			res = PS_MEM_FAIL;
			goto LBL_Q;
		}
	}

	res = PSTM_OKAY;

LBL_Q:pstm_clear (&q);
LBL_Y:pstm_clear (&y);
LBL_X:pstm_clear (&x);
LBL_T2:pstm_clear (&t2);
LBL_T1:pstm_clear (&t1);

	return res;
}

/******************************************************************************/
/*
	Swap the elements of two integers, for cases where you can't simply swap
	the pstm_int pointers around
*/
void pstm_exch(pstm_int * a, pstm_int * b)
{
	pstm_int		t;

	t	= *a;
	*a	= *b;
	*b	= t;
}

/******************************************************************************/
/*
	c = a mod b, 0 <= c < b
*/
int32 pstm_mod(psPool_t *pool, pstm_int *a, pstm_int *b, pstm_int *c)
{
	pstm_int	t;
	int32		err;
/*
	Smart-size
*/
	if ((err = pstm_init_size(pool, &t, b->alloc)) != PSTM_OKAY) {
		return err;
	}
	if ((err = pstm_div(pool, a, b, NULL, &t)) != PSTM_OKAY) {
		pstm_clear (&t);
		return err;
	}
	if (t.sign != b->sign) {
		err = pstm_add(&t, b, c);
	} else {
		pstm_exch (&t, c);
	}
	pstm_clear (&t);
	return err;
}

/******************************************************************************/
/*
	d = a * b (mod c)
*/
int32 pstm_mulmod(psPool_t *pool, pstm_int *a, pstm_int *b, pstm_int *c,
			pstm_int *d)
{
	int32		res;
	int		size; //bbox: was int16
	pstm_int	tmp;

/*
	Smart-size pstm_inits.  d is an output that is influenced by this local 't'
	so don't shrink 'd' if it wants to becuase this will lead to an pstm_grow
	in RSA operations
*/
	size = a->used + b->used + 1;
	if ((a == d) && (size < a->alloc)) {
		size = a->alloc;
	}
	if ((res = pstm_init_size(pool, &tmp, size)) != PSTM_OKAY) {
		return res;
	}
	if ((res = pstm_mul_comba(pool, a, b, &tmp, NULL, 0)) != PSTM_OKAY) {
		pstm_clear(&tmp);
		return res;
	}
	res = pstm_mod(pool, &tmp, c, d);
	pstm_clear(&tmp);
	return res;
}

/******************************************************************************/
/*
 *	y = g**x (mod b)
 *	Some restrictions... x must be positive and < b
 */
int32 pstm_exptmod(psPool_t *pool, pstm_int *G, pstm_int *X, pstm_int *P,
			pstm_int *Y)
{
	pstm_int	M[32], res; /* Keep this winsize based: (1 << max_winsize) */
	pstm_digit	buf, mp;
	pstm_digit	*paD;
	int32		err, bitbuf;
	int		bitcpy, bitcnt, mode, digidx, x, y, winsize; //bbox: was int16
	uint32		paDlen;

	/* set window size from what user set as optimization */
	x = pstm_count_bits(X);
	if (x < 50) {
		winsize = 2;
	} else {
		winsize = PS_EXPTMOD_WINSIZE;
	}

	/* now setup montgomery  */
	if ((err = pstm_montgomery_setup (P, &mp)) != PSTM_OKAY) {
		return err;
	}

	/* setup result */
	if ((err = pstm_init_size(pool, &res, (P->used * 2) + 1)) != PSTM_OKAY) {
		return err;
	}
/*
	create M table
	The M table contains powers of the input base, e.g. M[x] = G^x mod P
	The first half of the table is not computed though except for M[0] and M[1]
 */
	/* now we need R mod m */
	if ((err = pstm_montgomery_calc_normalization (&res, P)) != PSTM_OKAY) {
		goto LBL_RES;
	}
/*
	init M array
	init first cell
 */
	if ((err = pstm_init_size(pool, &M[1], res.used)) != PSTM_OKAY) {
		goto LBL_RES;
	}

	/* now set M[1] to G * R mod m */
	if (pstm_cmp_mag(P, G) != PSTM_GT) {
		/* G > P so we reduce it first */
		if ((err = pstm_mod(pool, G, P, &M[1])) != PSTM_OKAY) {
			goto LBL_M;
		}
	} else {
		if ((err = pstm_copy(G, &M[1])) != PSTM_OKAY) {
			goto LBL_M;
		}
	}
	if ((err = pstm_mulmod (pool, &M[1], &res, P, &M[1])) != PSTM_OKAY) {
		goto LBL_M;
	}
/*
	Pre-allocated digit.  Used for mul, sqr, AND reduce
*/
	paDlen = ((M[1].used + 3) * 2) * sizeof(pstm_digit);
	paD = xzalloc(paDlen);//bbox
/*
	compute the value at M[1<<(winsize-1)] by squaring M[1] (winsize-1) times
 */
	if (pstm_init_copy(pool, &M[1 << (winsize - 1)], &M[1], 1) != PSTM_OKAY) {
		err = PS_MEM_FAIL;
		goto LBL_PAD;
	}
	for (x = 0; x < (winsize - 1); x++) {
		if ((err = pstm_sqr_comba (pool, &M[1 << (winsize - 1)],
				&M[1 << (winsize - 1)], paD, paDlen)) != PSTM_OKAY) {
			goto LBL_PAD;
		}
		if ((err = pstm_montgomery_reduce(pool, &M[1 << (winsize - 1)], P, mp,
				paD, paDlen)) != PSTM_OKAY) {
			goto LBL_PAD;
		}
	}
/*
	now init the second half of the array
*/
	for (x = (1<<(winsize-1)) + 1; x < (1 << winsize); x++) {
		if ((err = pstm_init_size(pool, &M[x], M[1<<(winsize-1)].alloc + 1))
				!= PSTM_OKAY) {
			for (y = 1<<(winsize-1); y < x; y++) {
				pstm_clear(&M[y]);
			}
			goto LBL_PAD;
		}
	}

	/* create upper table */
	for (x = (1 << (winsize - 1)) + 1; x < (1 << winsize); x++) {
		if ((err = pstm_mul_comba(pool, &M[x - 1], &M[1], &M[x], paD, paDlen))
				!= PSTM_OKAY) {
			goto LBL_MARRAY;
		}
		if ((err = pstm_montgomery_reduce(pool, &M[x], P, mp, paD, paDlen)) !=
				PSTM_OKAY) {
			goto LBL_MARRAY;
		}
	}

	/* set initial mode and bit cnt */
	mode   = 0;
	bitcnt = 1;
	buf    = 0;
	digidx = X->used - 1;
	bitcpy = 0;
	bitbuf = 0;

	for (;;) {
		/* grab next digit as required */
		if (--bitcnt == 0) {
			/* if digidx == -1 we are out of digits so break */
			if (digidx == -1) {
				break;
			}
			/* read next digit and reset bitcnt */
			buf    = X->dp[digidx--];
			bitcnt = (int32)DIGIT_BIT;
		}

		/* grab the next msb from the exponent */
		y     = (pstm_digit)(buf >> (DIGIT_BIT - 1)) & 1;
		buf <<= (pstm_digit)1;
/*
		 If the bit is zero and mode == 0 then we ignore it.
		 These represent the leading zero bits before the first 1 bit
		 in the exponent.  Technically this opt is not required but it
		 does lower the # of trivial squaring/reductions used
*/
		if (mode == 0 && y == 0) {
			continue;
		}

		/* if the bit is zero and mode == 1 then we square */
		if (mode == 1 && y == 0) {
			if ((err = pstm_sqr_comba(pool, &res, &res, paD, paDlen)) !=
					PSTM_OKAY) {
				goto LBL_MARRAY;
			}
			if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD, paDlen))
					!= PSTM_OKAY) {
				goto LBL_MARRAY;
			}
			continue;
		}

		/* else we add it to the window */
		bitbuf |= (y << (winsize - ++bitcpy));
		mode    = 2;

		if (bitcpy == winsize) {
			/* ok window is filled so square as required and mul square first */
			for (x = 0; x < winsize; x++) {
				if ((err = pstm_sqr_comba(pool, &res, &res, paD, paDlen)) !=
						PSTM_OKAY) {
					goto LBL_MARRAY;
				}
				if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD,
						paDlen)) != PSTM_OKAY) {
					goto LBL_MARRAY;
				}
			}

			/* then multiply */
			if ((err = pstm_mul_comba(pool, &res, &M[bitbuf], &res, paD,
					paDlen)) != PSTM_OKAY) {
				goto LBL_MARRAY;
			}
			if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD, paDlen))
					!= PSTM_OKAY) {
				goto LBL_MARRAY;
			}

			/* empty window and reset */
			bitcpy = 0;
			bitbuf = 0;
			mode   = 1;
		}
	}

	/* if bits remain then square/multiply */
	if (mode == 2 && bitcpy > 0) {
		/* square then multiply if the bit is set */
		for (x = 0; x < bitcpy; x++) {
			if ((err = pstm_sqr_comba(pool, &res, &res, paD, paDlen)) !=
					PSTM_OKAY) {
				goto LBL_MARRAY;
			}
			if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD, paDlen))
					!= PSTM_OKAY) {
				goto LBL_MARRAY;
			}

			/* get next bit of the window */
			bitbuf <<= 1;
			if ((bitbuf & (1 << winsize)) != 0) {
			/* then multiply */
				if ((err = pstm_mul_comba(pool, &res, &M[1], &res, paD, paDlen))
						!= PSTM_OKAY) {
					goto LBL_MARRAY;
				}
				if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD,
						paDlen)) != PSTM_OKAY) {
					goto LBL_MARRAY;
				}
			}
		}
	}
/*
	Fix up result if Montgomery reduction is used recall that any value in a
	Montgomery system is actually multiplied by R mod n.  So we have to reduce
	one more time to cancel out the factor of R.
*/
	if ((err = pstm_montgomery_reduce(pool, &res, P, mp, paD, paDlen)) !=
			PSTM_OKAY) {
		goto LBL_MARRAY;
	}
	/* swap res with Y */
	if ((err = pstm_copy (&res, Y)) != PSTM_OKAY) {
		goto LBL_MARRAY;
	}
	err = PSTM_OKAY;
LBL_MARRAY:
	for (x = 1<<(winsize-1); x < (1 << winsize); x++) {
		pstm_clear(&M[x]);
	}
LBL_PAD:psFree(paD, pool);
LBL_M: pstm_clear(&M[1]);
LBL_RES:pstm_clear(&res);
	return err;
}

/******************************************************************************/
/*

*/
int32 pstm_add(pstm_int *a, pstm_int *b, pstm_int *c)
{
	int32	res;
	int	sa, sb; //bbox: was int16

	/* get sign of both inputs */
	sa = a->sign;
	sb = b->sign;

	/* handle two cases, not four */
	if (sa == sb) {
		/* both positive or both negative, add their mags, copy the sign */
		c->sign = sa;
		if ((res = s_pstm_add (a, b, c)) != PSTM_OKAY) {
			return res;
		}
	} else {
/*
		one positive, the other negative
		subtract the one with the greater magnitude from the one of the lesser
		magnitude. The result gets the sign of the one with the greater mag.
 */
		if (pstm_cmp_mag (a, b) == PSTM_LT) {
			c->sign = sb;
			if ((res = s_pstm_sub (b, a, c)) != PSTM_OKAY) {
				return res;
			}
		} else {
			c->sign = sa;
			if ((res = s_pstm_sub (a, b, c)) != PSTM_OKAY) {
				return res;
			}
		}
	}
	return PS_SUCCESS;
}

/******************************************************************************/
/*
	reverse an array, used for radix code
*/
static void pstm_reverse (unsigned char *s, int len) //bbox: was int16 len
{
	int32     ix, iy;
	unsigned char t;

	ix = 0;
	iy = len - 1;
	while (ix < iy) {
		t     = s[ix];
		s[ix] = s[iy];
		s[iy] = t;
		++ix;
		--iy;
	}
}
/******************************************************************************/
/*
	No reverse.  Useful in some of the EIP-154 PKA stuff where special byte
	order seems to come into play more often
*/
int32 pstm_to_unsigned_bin_nr(psPool_t *pool, pstm_int *a, unsigned char *b)
{
	int32     res;
	int     x; //bbox: was int16
	pstm_int  t = { 0 };

	if ((res = pstm_init_copy(pool, &t, a, 0)) != PSTM_OKAY) {
		return res;
	}

	x = 0;
	while (pstm_iszero (&t) == 0) {
		b[x++] = (unsigned char) (t.dp[0] & 255);
		if ((res = pstm_div_2d (pool, &t, 8, &t, NULL)) != PSTM_OKAY) {
			pstm_clear(&t);
			return res;
		}
	}
	pstm_clear(&t);
	return PS_SUCCESS;
}
/******************************************************************************/
/*

*/
int32 pstm_to_unsigned_bin(psPool_t *pool, pstm_int *a, unsigned char *b)
{
	int32     res;
	int	x; //bbox: was int16
	pstm_int  t = { 0 };

	if ((res = pstm_init_copy(pool, &t, a, 0)) != PSTM_OKAY) {
		return res;
	}

	x = 0;
	while (pstm_iszero (&t) == 0) {
		b[x++] = (unsigned char) (t.dp[0] & 255);
		if ((res = pstm_div_2d (pool, &t, 8, &t, NULL)) != PSTM_OKAY) {
			pstm_clear(&t);
			return res;
		}
	}
	pstm_reverse (b, x);
	pstm_clear(&t);
	return PS_SUCCESS;
}

/******************************************************************************/
/*
	compare against a single digit
*/
int32 pstm_cmp_d(pstm_int *a, pstm_digit b)
{
	/* compare based on sign */
	if ((b && a->used == 0) || a->sign == PSTM_NEG) {
		return PSTM_LT;
	}

	/* compare based on magnitude */
	if (a->used > 1) {
		return PSTM_GT;
	}

	/* compare the only digit of a to b */
	if (a->dp[0] > b) {
		return PSTM_GT;
	} else if (a->dp[0] < b) {
		return PSTM_LT;
	} else {
		return PSTM_EQ;
	}
}

/*
	Need invmod for ECC and also private key loading for hardware crypto
	in cases where dQ > dP.  The values must be switched and a new qP must be
	calculated using this function
*/
//bbox: pool unused
#define pstm_invmod_slow(pool, a, b, c) \
        pstm_invmod_slow(      a, b, c)
static int32 pstm_invmod_slow(psPool_t *pool, pstm_int * a, pstm_int * b,
				pstm_int * c)
{
	pstm_int  x, y, u, v, A, B, C, D;
	int32     res;

	/* b cannot be negative */
	if (b->sign == PSTM_NEG || pstm_iszero(b) == 1) {
		return PS_LIMIT_FAIL;
	}

	/* init temps */
	if (pstm_init_size(pool, &x, b->used) != PSTM_OKAY) {
		return PS_MEM_FAIL;
	}

	/* x = a, y = b */
	if ((res = pstm_mod(pool, a, b, &x)) != PSTM_OKAY) {
		goto LBL_X;
	}

	if (pstm_init_copy(pool, &y, b, 0) != PSTM_OKAY) {
		goto LBL_X;
	}

	/* 2. [modified] if x,y are both even then return an error! */
	if (pstm_iseven (&x) == 1 && pstm_iseven (&y) == 1) {
		res = PS_FAILURE;
		goto LBL_Y;
	}

	/* 3. u=x, v=y, A=1, B=0, C=0,D=1 */
	if ((res = pstm_init_copy(pool, &u, &x, 0)) != PSTM_OKAY) {
		goto LBL_Y;
	}
	if ((res = pstm_init_copy(pool, &v, &y, 0)) != PSTM_OKAY) {
		goto LBL_U;
	}

	if ((res = pstm_init_size(pool, &A, sizeof(pstm_digit))) != PSTM_OKAY) {
		goto LBL_V;
	}

	if ((res = pstm_init_size(pool, &D, sizeof(pstm_digit))) != PSTM_OKAY) {
		goto LBL_A;
	}
	pstm_set (&A, 1);
	pstm_set (&D, 1);

	if ((res = pstm_init(pool, &B)) != PSTM_OKAY) {
		goto LBL_D;
	}
	if ((res = pstm_init(pool, &C)) != PSTM_OKAY) {
		goto LBL_B;
	}

top:
	/* 4.  while u is even do */
	while (pstm_iseven (&u) == 1) {
		/* 4.1 u = u/2 */
		if ((res = pstm_div_2 (&u, &u)) != PSTM_OKAY) {
			goto LBL_C;
		}

		/* 4.2 if A or B is odd then */
		if (pstm_isodd (&A) == 1 || pstm_isodd (&B) == 1) {
			/* A = (A+y)/2, B = (B-x)/2 */
			if ((res = pstm_add (&A, &y, &A)) != PSTM_OKAY) {
				goto LBL_C;
			}
			if ((res = pstm_sub (&B, &x, &B)) != PSTM_OKAY) {
				goto LBL_C;
			}
		}
		/* A = A/2, B = B/2 */
		if ((res = pstm_div_2 (&A, &A)) != PSTM_OKAY) {
			goto LBL_C;
		}
		if ((res = pstm_div_2 (&B, &B)) != PSTM_OKAY) {
			goto LBL_C;
		}
	}

	/* 5.  while v is even do */
	while (pstm_iseven (&v) == 1) {
		/* 5.1 v = v/2 */
		if ((res = pstm_div_2 (&v, &v)) != PSTM_OKAY) {
			goto LBL_C;
		}

		/* 5.2 if C or D is odd then */
		if (pstm_isodd (&C) == 1 || pstm_isodd (&D) == 1) {
			/* C = (C+y)/2, D = (D-x)/2 */
			if ((res = pstm_add (&C, &y, &C)) != PSTM_OKAY) {
				goto LBL_C;
			}
			if ((res = pstm_sub (&D, &x, &D)) != PSTM_OKAY) {
				goto LBL_C;
			}
		}
		/* C = C/2, D = D/2 */
		if ((res = pstm_div_2 (&C, &C)) != PSTM_OKAY) {
			goto LBL_C;
		}
		if ((res = pstm_div_2 (&D, &D)) != PSTM_OKAY) {
			goto LBL_C;
		}
	}

	/* 6.  if u >= v then */
	if (pstm_cmp (&u, &v) != PSTM_LT) {
		/* u = u - v, A = A - C, B = B - D */
		if ((res = pstm_sub (&u, &v, &u)) != PSTM_OKAY) {
				goto LBL_C;
		}
		if ((res = pstm_sub (&A, &C, &A)) != PSTM_OKAY) {
				goto LBL_C;
		}
		if ((res = pstm_sub (&B, &D, &B)) != PSTM_OKAY) {
				goto LBL_C;
		}
	} else {
		/* v - v - u, C = C - A, D = D - B */
		if ((res = pstm_sub (&v, &u, &v)) != PSTM_OKAY) {
				goto LBL_C;
		}
		if ((res = pstm_sub (&C, &A, &C)) != PSTM_OKAY) {
				goto LBL_C;
		}
		if ((res = pstm_sub (&D, &B, &D)) != PSTM_OKAY) {
				goto LBL_C;
		}
	}

	/* if not zero goto step 4 */
	if (pstm_iszero (&u) == 0)
		goto top;

	/* now a = C, b = D, gcd == g*v */

	/* if v != 1 then there is no inverse */
	if (pstm_cmp_d (&v, 1) != PSTM_EQ) {
		res = PS_FAILURE;
		goto LBL_C;
	}

	/* if its too low */
	while (pstm_cmp_d(&C, 0) == PSTM_LT) {
		if ((res = pstm_add(&C, b, &C)) != PSTM_OKAY) {
			goto LBL_C;
		}
	}

	/* too big */
	while (pstm_cmp_mag(&C, b) != PSTM_LT) {
		if ((res = pstm_sub(&C, b, &C)) != PSTM_OKAY) {
			goto LBL_C;
		}
	}

	/* C is now the inverse */
	if ((res = pstm_copy(&C, c)) != PSTM_OKAY) {
		goto LBL_C;
	}
	res = PSTM_OKAY;

LBL_C: pstm_clear(&C);
LBL_D: pstm_clear(&D);
LBL_B: pstm_clear(&B);
LBL_A: pstm_clear(&A);
LBL_V: pstm_clear(&v);
LBL_U: pstm_clear(&u);
LBL_Y: pstm_clear(&y);
LBL_X: pstm_clear(&x);

	return res;
}

/* c = 1/a (mod b) for odd b only */
int32 pstm_invmod(psPool_t *pool, pstm_int *a, pstm_int *b, pstm_int *c)
{
	pstm_int	x, y, u, v, B, D;
	int32		res;
	int		neg, sanity; //bbox: was uint16

	/* 2. [modified] b must be odd   */
	if (pstm_iseven (b) == 1) {
		return pstm_invmod_slow(pool, a,b,c);
	}

	/* x == modulus, y == value to invert */
	if ((res = pstm_init_copy(pool, &x, b, 0)) != PSTM_OKAY) {
		return res;
	}

	if ((res = pstm_init_size(pool, &y, a->alloc)) != PSTM_OKAY) {
		goto LBL_X;
	}

	/* we need y = |a| */
	pstm_abs(a, &y);

	/* 3. u=x, v=y, A=1, B=0, C=0,D=1 */
	if ((res = pstm_init_copy(pool, &u, &x, 0)) != PSTM_OKAY) {
		goto LBL_Y;
	}
	if ((res = pstm_init_copy(pool, &v, &y, 0)) != PSTM_OKAY) {
		goto LBL_U;
	}
	if ((res = pstm_init(pool, &B)) != PSTM_OKAY) {
		goto LBL_V;
	}
	if ((res = pstm_init(pool, &D)) != PSTM_OKAY) {
		goto LBL_B;
	}

	pstm_set (&D, 1);

	sanity = 0;
top:
	/* 4.  while u is even do */
	while (pstm_iseven (&u) == 1) {
		/* 4.1 u = u/2 */
		if ((res = pstm_div_2 (&u, &u)) != PSTM_OKAY) {
			goto LBL_D;
		}

		/* 4.2 if B is odd then */
		if (pstm_isodd (&B) == 1) {
			if ((res = pstm_sub (&B, &x, &B)) != PSTM_OKAY) {
				goto LBL_D;
			}
		}
		/* B = B/2 */
		if ((res = pstm_div_2 (&B, &B)) !=  PSTM_OKAY) {
			goto LBL_D;
		}
	}

	/* 5.  while v is even do */
	while (pstm_iseven (&v) == 1) {
		/* 5.1 v = v/2 */
		if ((res = pstm_div_2 (&v, &v)) != PSTM_OKAY) {
			goto LBL_D;
		}
		/* 5.2 if D is odd then */
		if (pstm_isodd (&D) == 1) {
			/* D = (D-x)/2 */
			if ((res = pstm_sub (&D, &x, &D)) != PSTM_OKAY) {
				goto LBL_D;
			}
		}
		/* D = D/2 */
		if ((res = pstm_div_2 (&D, &D)) !=  PSTM_OKAY) {
			goto LBL_D;
		}
	}

	/* 6.  if u >= v then */
	if (pstm_cmp (&u, &v) != PSTM_LT) {
		/* u = u - v, B = B - D */
		if ((res = pstm_sub (&u, &v, &u)) != PSTM_OKAY) {
				goto LBL_D;
		}
		if ((res = pstm_sub (&B, &D, &B)) != PSTM_OKAY) {
				goto LBL_D;
		}
	} else {
		/* v - v - u, D = D - B */
		if ((res = pstm_sub (&v, &u, &v)) != PSTM_OKAY) {
				goto LBL_D;
		}
		if ((res = pstm_sub (&D, &B, &D)) != PSTM_OKAY) {
				goto LBL_D;
		}
	}

	/* if not zero goto step 4 */
	if (sanity++ > 1000) {
		res = PS_LIMIT_FAIL;
		goto LBL_D;
	}
	if (pstm_iszero (&u) == 0) {
		goto top;
	}

	/* now a = C, b = D, gcd == g*v */

	/* if v != 1 then there is no inverse */
	if (pstm_cmp_d (&v, 1) != PSTM_EQ) {
		res = PS_FAILURE;
		goto LBL_D;
	}

	/* b is now the inverse */
	neg = a->sign;
	while (D.sign == PSTM_NEG) {
		if ((res = pstm_add (&D, b, &D)) != PSTM_OKAY) {
			goto LBL_D;
		}
	}
	if ((res = pstm_copy (&D, c)) != PSTM_OKAY) {
		goto LBL_D;
	}
	c->sign = neg;
	res = PSTM_OKAY;

LBL_D: pstm_clear(&D);
LBL_B: pstm_clear(&B);
LBL_V: pstm_clear(&v);
LBL_U: pstm_clear(&u);
LBL_Y: pstm_clear(&y);
LBL_X: pstm_clear(&x);
	return res;
}
#endif /* !DISABLE_PSTM */
/******************************************************************************/
