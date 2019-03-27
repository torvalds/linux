/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <gmp.h>

#include "bearssl.h"
#include "inner.h"

/*
 * Pointers to implementations.
 */
typedef struct {
	uint32_t word_size;
	void (*zero)(uint32_t *x, uint32_t bit_len);
	void (*decode)(uint32_t *x, const void *src, size_t len);
	uint32_t (*decode_mod)(uint32_t *x,
		const void *src, size_t len, const uint32_t *m);
	void (*reduce)(uint32_t *x, const uint32_t *a, const uint32_t *m);
	void (*decode_reduce)(uint32_t *x,
		const void *src, size_t len, const uint32_t *m);
	void (*encode)(void *dst, size_t len, const uint32_t *x);
	uint32_t (*add)(uint32_t *a, const uint32_t *b, uint32_t ctl);
	uint32_t (*sub)(uint32_t *a, const uint32_t *b, uint32_t ctl);
	uint32_t (*ninv)(uint32_t x);
	void (*montymul)(uint32_t *d, const uint32_t *x, const uint32_t *y,
		const uint32_t *m, uint32_t m0i);
	void (*to_monty)(uint32_t *x, const uint32_t *m);
	void (*from_monty)(uint32_t *x, const uint32_t *m, uint32_t m0i);
	void (*modpow)(uint32_t *x, const unsigned char *e, size_t elen,
		const uint32_t *m, uint32_t m0i, uint32_t *t1, uint32_t *t2);
} int_impl;

static const int_impl i31_impl = {
	31,
	&br_i31_zero,
	&br_i31_decode,
	&br_i31_decode_mod,
	&br_i31_reduce,
	&br_i31_decode_reduce,
	&br_i31_encode,
	&br_i31_add,
	&br_i31_sub,
	&br_i31_ninv31,
	&br_i31_montymul,
	&br_i31_to_monty,
	&br_i31_from_monty,
	&br_i31_modpow
};
static const int_impl i32_impl = {
	32,
	&br_i32_zero,
	&br_i32_decode,
	&br_i32_decode_mod,
	&br_i32_reduce,
	&br_i32_decode_reduce,
	&br_i32_encode,
	&br_i32_add,
	&br_i32_sub,
	&br_i32_ninv32,
	&br_i32_montymul,
	&br_i32_to_monty,
	&br_i32_from_monty,
	&br_i32_modpow
};

static const int_impl *impl;

static gmp_randstate_t RNG;

/*
 * Get a random prime of length 'size' bits. This function also guarantees
 * that x-1 is not a multiple of 65537.
 */
static void
rand_prime(mpz_t x, int size)
{
	for (;;) {
		mpz_urandomb(x, RNG, size - 1);
		mpz_setbit(x, 0);
		mpz_setbit(x, size - 1);
		if (mpz_probab_prime_p(x, 50)) {
			mpz_sub_ui(x, x, 1);
			if (mpz_divisible_ui_p(x, 65537)) {
				continue;
			}
			mpz_add_ui(x, x, 1);
			return;
		}
	}
}

/*
 * Print out a GMP integer (for debug).
 */
static void
print_z(mpz_t z)
{
	unsigned char zb[1000];
	size_t zlen, k;

	mpz_export(zb, &zlen, 1, 1, 0, 0, z);
	if (zlen == 0) {
		printf(" 00");
		return;
	}
	if ((zlen & 3) != 0) {
		k = 4 - (zlen & 3);
		memmove(zb + k, zb, zlen);
		memset(zb, 0, k);
		zlen += k;
	}
	for (k = 0; k < zlen; k += 4) {
		printf(" %02X%02X%02X%02X",
			zb[k], zb[k + 1], zb[k + 2], zb[k + 3]);
	}
}

/*
 * Print out an i31 or i32 integer (for debug).
 */
static void
print_u(uint32_t *x)
{
	size_t k;

	if (x[0] == 0) {
		printf(" 00000000 (0, 0)");
		return;
	}
	for (k = (x[0] + 31) >> 5; k > 0; k --) {
		printf(" %08lX", (unsigned long)x[k]);
	}
	printf(" (%u, %u)", (unsigned)(x[0] >> 5), (unsigned)(x[0] & 31));
}

/*
 * Check that an i31/i32 number and a GMP number are equal.
 */
static void
check_eqz(uint32_t *x, mpz_t z)
{
	unsigned char xb[1000];
	unsigned char zb[1000];
	size_t xlen, zlen;
	int good;

	xlen = ((x[0] + 31) & ~(uint32_t)31) >> 3;
	impl->encode(xb, xlen, x);
	mpz_export(zb, &zlen, 1, 1, 0, 0, z);
	good = 1;
	if (xlen < zlen) {
		good = 0;
	} else if (xlen > zlen) {
		size_t u;

		for (u = xlen; u > zlen; u --) {
			if (xb[xlen - u] != 0) {
				good = 0;
				break;
			}
		}
	}
	good = good && memcmp(xb + xlen - zlen, zb, zlen) == 0;
	if (!good) {
		size_t u;

		printf("Mismatch:\n");
		printf("  x = ");
		print_u(x);
		printf("\n");
		printf("  ex = ");
		for (u = 0; u < xlen; u ++) {
			printf("%02X", xb[u]);
		}
		printf("\n");
		printf("  z = ");
		print_z(z);
		printf("\n");
		exit(EXIT_FAILURE);
	}
}

/* obsolete
static void
mp_to_br(uint32_t *mx, uint32_t x_bitlen, mpz_t x)
{
	uint32_t x_ebitlen;
	size_t xlen;

	if (mpz_sizeinbase(x, 2) > x_bitlen) {
		abort();
	}
	x_ebitlen = ((x_bitlen / 31) << 5) + (x_bitlen % 31);
	br_i31_zero(mx, x_ebitlen);
	mpz_export(mx + 1, &xlen, -1, sizeof *mx, 0, 1, x);
}
*/

static void
test_modint(void)
{
	int i, j, k;
	mpz_t p, a, b, v, t1;

	printf("Test modular integers: ");
	fflush(stdout);

	gmp_randinit_mt(RNG);
	mpz_init(p);
	mpz_init(a);
	mpz_init(b);
	mpz_init(v);
	mpz_init(t1);
	mpz_set_ui(t1, (unsigned long)time(NULL));
	gmp_randseed(RNG, t1);
	for (k = 2; k <= 128; k ++) {
		for (i = 0; i < 10; i ++) {
			unsigned char ep[100], ea[100], eb[100], ev[100];
			size_t plen, alen, blen, vlen;
			uint32_t mp[40], ma[40], mb[40], mv[60], mx[100];
			uint32_t mt1[40], mt2[40], mt3[40];
			uint32_t ctl;
			uint32_t mp0i;

			rand_prime(p, k);
			mpz_urandomm(a, RNG, p);
			mpz_urandomm(b, RNG, p);
			mpz_urandomb(v, RNG, k + 60);
			if (mpz_sgn(b) == 0) {
				mpz_set_ui(b, 1);
			}
			mpz_export(ep, &plen, 1, 1, 0, 0, p);
			mpz_export(ea, &alen, 1, 1, 0, 0, a);
			mpz_export(eb, &blen, 1, 1, 0, 0, b);
			mpz_export(ev, &vlen, 1, 1, 0, 0, v);

			impl->decode(mp, ep, plen);
			if (impl->decode_mod(ma, ea, alen, mp) != 1) {
				printf("Decode error\n");
				printf("  ea = ");
				print_z(a);
				printf("\n");
				printf("  p = ");
				print_u(mp);
				printf("\n");
				exit(EXIT_FAILURE);
			}
			mp0i = impl->ninv(mp[1]);
			if (impl->decode_mod(mb, eb, blen, mp) != 1) {
				printf("Decode error\n");
				printf("  eb = ");
				print_z(b);
				printf("\n");
				printf("  p = ");
				print_u(mp);
				printf("\n");
				exit(EXIT_FAILURE);
			}
			impl->decode(mv, ev, vlen);
			check_eqz(mp, p);
			check_eqz(ma, a);
			check_eqz(mb, b);
			check_eqz(mv, v);

			impl->decode_mod(ma, ea, alen, mp);
			impl->decode_mod(mb, eb, blen, mp);
			ctl = impl->add(ma, mb, 1);
			ctl |= impl->sub(ma, mp, 0) ^ (uint32_t)1;
			impl->sub(ma, mp, ctl);
			mpz_add(t1, a, b);
			mpz_mod(t1, t1, p);
			check_eqz(ma, t1);

			impl->decode_mod(ma, ea, alen, mp);
			impl->decode_mod(mb, eb, blen, mp);
			impl->add(ma, mp, impl->sub(ma, mb, 1));
			mpz_sub(t1, a, b);
			mpz_mod(t1, t1, p);
			check_eqz(ma, t1);

			impl->decode_reduce(ma, ev, vlen, mp);
			mpz_mod(t1, v, p);
			check_eqz(ma, t1);

			impl->decode(mv, ev, vlen);
			impl->reduce(ma, mv, mp);
			mpz_mod(t1, v, p);
			check_eqz(ma, t1);

			impl->decode_mod(ma, ea, alen, mp);
			impl->to_monty(ma, mp);
			mpz_mul_2exp(t1, a, ((k + impl->word_size - 1)
				/ impl->word_size) * impl->word_size);
			mpz_mod(t1, t1, p);
			check_eqz(ma, t1);
			impl->from_monty(ma, mp, mp0i);
			check_eqz(ma, a);

			impl->decode_mod(ma, ea, alen, mp);
			impl->decode_mod(mb, eb, blen, mp);
			impl->to_monty(ma, mp);
			impl->montymul(mt1, ma, mb, mp, mp0i);
			mpz_mul(t1, a, b);
			mpz_mod(t1, t1, p);
			check_eqz(mt1, t1);

			impl->decode_mod(ma, ea, alen, mp);
			impl->modpow(ma, ev, vlen, mp, mp0i, mt1, mt2);
			mpz_powm(t1, a, v, p);
			check_eqz(ma, t1);

			/*
			br_modint_decode(ma, mp, ea, alen);
			br_modint_decode(mb, mp, eb, blen);
			if (!br_modint_div(ma, mb, mp, mt1, mt2, mt3)) {
				fprintf(stderr, "division failed\n");
				exit(EXIT_FAILURE);
			}
			mpz_sub_ui(t1, p, 2);
			mpz_powm(t1, b, t1, p);
			mpz_mul(t1, a, t1);
			mpz_mod(t1, t1, p);
			check_eqz(ma, t1);

			br_modint_decode(ma, mp, ea, alen);
			br_modint_decode(mb, mp, eb, blen);
			for (j = 0; j <= (2 * k + 5); j ++) {
				br_int_add(mx, j, ma, mb);
				mpz_add(t1, a, b);
				mpz_tdiv_r_2exp(t1, t1, j);
				check_eqz(mx, t1);

				br_int_mul(mx, j, ma, mb);
				mpz_mul(t1, a, b);
				mpz_tdiv_r_2exp(t1, t1, j);
				check_eqz(mx, t1);
			}
			*/
		}
		printf(".");
		fflush(stdout);
	}
	mpz_clear(p);
	mpz_clear(a);
	mpz_clear(b);
	mpz_clear(v);
	mpz_clear(t1);

	printf(" done.\n");
	fflush(stdout);
}

#if 0
static void
test_RSA_core(void)
{
	int i, j, k;
	mpz_t n, e, d, p, q, dp, dq, iq, t1, t2, phi;

	printf("Test RSA core: ");
	fflush(stdout);

	gmp_randinit_mt(RNG);
	mpz_init(n);
	mpz_init(e);
	mpz_init(d);
	mpz_init(p);
	mpz_init(q);
	mpz_init(dp);
	mpz_init(dq);
	mpz_init(iq);
	mpz_init(t1);
	mpz_init(t2);
	mpz_init(phi);
	mpz_set_ui(t1, (unsigned long)time(NULL));
	gmp_randseed(RNG, t1);

	/*
	 * To test corner cases, we want to try RSA keys such that the
	 * lengths of both factors can be arbitrary modulo 2^32. Factors
	 * p and q need not be of the same length; p can be greater than
	 * q and q can be greater than p.
	 *
	 * To keep computation time reasonable, we use p and q factors of
	 * less than 128 bits; this is way too small for secure RSA,
	 * but enough to exercise all code paths (since we work only with
	 * 32-bit words).
	 */
	for (i = 64; i <= 96; i ++) {
		rand_prime(p, i);
		for (j = i - 33; j <= i + 33; j ++) {
			uint32_t mp[40], mq[40], mdp[40], mdq[40], miq[40];

			/*
			 * Generate a RSA key pair, with p of length i bits,
			 * and q of length j bits.
			 */
			do {
				rand_prime(q, j);
			} while (mpz_cmp(p, q) == 0);
			mpz_mul(n, p, q);
			mpz_set_ui(e, 65537);
			mpz_sub_ui(t1, p, 1);
			mpz_sub_ui(t2, q, 1);
			mpz_mul(phi, t1, t2);
			mpz_invert(d, e, phi);
			mpz_mod(dp, d, t1);
			mpz_mod(dq, d, t2);
			mpz_invert(iq, q, p);

			/*
			 * Convert the key pair elements to BearSSL arrays.
			 */
			mp_to_br(mp, mpz_sizeinbase(p, 2), p);
			mp_to_br(mq, mpz_sizeinbase(q, 2), q);
			mp_to_br(mdp, mpz_sizeinbase(dp, 2), dp);
			mp_to_br(mdq, mpz_sizeinbase(dq, 2), dq);
			mp_to_br(miq, mp[0], iq);

			/*
			 * Compute and check ten public/private operations.
			 */
			for (k = 0; k < 10; k ++) {
				uint32_t mx[40];

				mpz_urandomm(t1, RNG, n);
				mpz_powm(t2, t1, e, n);
				mp_to_br(mx, mpz_sizeinbase(n, 2), t2);
				br_rsa_private_core(mx, mp, mq, mdp, mdq, miq);
				check_eqz(mx, t1);
			}
		}
		printf(".");
		fflush(stdout);
	}

	printf(" done.\n");
	fflush(stdout);
}
#endif

int
main(void)
{
	printf("===== i32 ======\n");
	impl = &i32_impl;
	test_modint();
	printf("===== i31 ======\n");
	impl = &i31_impl;
	test_modint();
	/*
	test_RSA_core();
	*/
	return 0;
}
