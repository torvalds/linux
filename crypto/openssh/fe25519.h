/* $OpenBSD: fe25519.h,v 1.3 2013/12/09 11:03:45 markus Exp $ */

/*
 * Public Domain, Authors: Daniel J. Bernstein, Niels Duif, Tanja Lange,
 * Peter Schwabe, Bo-Yin Yang.
 * Copied from supercop-20130419/crypto_sign/ed25519/ref/fe25519.h
 */

#ifndef FE25519_H
#define FE25519_H

#include "crypto_api.h"

#define fe25519              crypto_sign_ed25519_ref_fe25519
#define fe25519_freeze       crypto_sign_ed25519_ref_fe25519_freeze
#define fe25519_unpack       crypto_sign_ed25519_ref_fe25519_unpack
#define fe25519_pack         crypto_sign_ed25519_ref_fe25519_pack
#define fe25519_iszero       crypto_sign_ed25519_ref_fe25519_iszero
#define fe25519_iseq_vartime crypto_sign_ed25519_ref_fe25519_iseq_vartime
#define fe25519_cmov         crypto_sign_ed25519_ref_fe25519_cmov
#define fe25519_setone       crypto_sign_ed25519_ref_fe25519_setone
#define fe25519_setzero      crypto_sign_ed25519_ref_fe25519_setzero
#define fe25519_neg          crypto_sign_ed25519_ref_fe25519_neg
#define fe25519_getparity    crypto_sign_ed25519_ref_fe25519_getparity
#define fe25519_add          crypto_sign_ed25519_ref_fe25519_add
#define fe25519_sub          crypto_sign_ed25519_ref_fe25519_sub
#define fe25519_mul          crypto_sign_ed25519_ref_fe25519_mul
#define fe25519_square       crypto_sign_ed25519_ref_fe25519_square
#define fe25519_invert       crypto_sign_ed25519_ref_fe25519_invert
#define fe25519_pow2523      crypto_sign_ed25519_ref_fe25519_pow2523

typedef struct 
{
  crypto_uint32 v[32]; 
}
fe25519;

void fe25519_freeze(fe25519 *r);

void fe25519_unpack(fe25519 *r, const unsigned char x[32]);

void fe25519_pack(unsigned char r[32], const fe25519 *x);

int fe25519_iszero(const fe25519 *x);

int fe25519_iseq_vartime(const fe25519 *x, const fe25519 *y);

void fe25519_cmov(fe25519 *r, const fe25519 *x, unsigned char b);

void fe25519_setone(fe25519 *r);

void fe25519_setzero(fe25519 *r);

void fe25519_neg(fe25519 *r, const fe25519 *x);

unsigned char fe25519_getparity(const fe25519 *x);

void fe25519_add(fe25519 *r, const fe25519 *x, const fe25519 *y);

void fe25519_sub(fe25519 *r, const fe25519 *x, const fe25519 *y);

void fe25519_mul(fe25519 *r, const fe25519 *x, const fe25519 *y);

void fe25519_square(fe25519 *r, const fe25519 *x);

void fe25519_invert(fe25519 *r, const fe25519 *x);

void fe25519_pow2523(fe25519 *r, const fe25519 *x);

#endif
