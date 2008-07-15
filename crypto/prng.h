/*
 * PRNG: Pseudo Random Number Generator
 *
 *  (C) Neil Horman <nhorman@tuxdriver.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  any later version.
 *
 *
 */

#ifndef _PRNG_H_
#define _PRNG_H_
struct prng_context;

int get_prng_bytes(char *buf, int nbytes, struct prng_context *ctx);
struct prng_context *alloc_prng_context(void);
int reset_prng_context(struct prng_context *ctx,
			unsigned char *key, unsigned char *iv,
			unsigned char *V,
			unsigned char *DT);
void free_prng_context(struct prng_context *ctx);

#endif

