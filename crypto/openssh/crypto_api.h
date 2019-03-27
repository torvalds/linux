/* $OpenBSD: crypto_api.h,v 1.4 2017/12/14 21:07:39 naddy Exp $ */

/*
 * Assembled from generated headers and source files by Markus Friedl.
 * Placed in the public domain.
 */

#ifndef crypto_api_h
#define crypto_api_h

#include "includes.h"

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <stdlib.h>

typedef int32_t crypto_int32;
typedef uint32_t crypto_uint32;

#define randombytes(buf, buf_len) arc4random_buf((buf), (buf_len))

#define crypto_hash_sha512_BYTES 64U

int	crypto_hash_sha512(unsigned char *, const unsigned char *,
    unsigned long long);

int	crypto_verify_32(const unsigned char *, const unsigned char *);

#define crypto_sign_ed25519_SECRETKEYBYTES 64U
#define crypto_sign_ed25519_PUBLICKEYBYTES 32U
#define crypto_sign_ed25519_BYTES 64U

int	crypto_sign_ed25519(unsigned char *, unsigned long long *,
    const unsigned char *, unsigned long long, const unsigned char *);
int	crypto_sign_ed25519_open(unsigned char *, unsigned long long *,
    const unsigned char *, unsigned long long, const unsigned char *);
int	crypto_sign_ed25519_keypair(unsigned char *, unsigned char *);

#endif /* crypto_api_h */
