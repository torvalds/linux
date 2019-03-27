/*
 * SHA-384 hash implementation and interface functions
 * Copyright (c) 2015, Pali Rohár <pali.rohar@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "sha384_i.h"
#include "crypto.h"


/**
 * sha384_vector - SHA384 hash for data vector
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash
 * Returns: 0 on success, -1 of failure
 */
int sha384_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	struct sha384_state ctx;
	size_t i;

	sha384_init(&ctx);
	for (i = 0; i < num_elem; i++)
		if (sha384_process(&ctx, addr[i], len[i]))
			return -1;
	if (sha384_done(&ctx, mac))
		return -1;
	return 0;
}


/* ===== start - public domain SHA384 implementation ===== */

/* This is based on SHA384 implementation in LibTomCrypt that was released into
 * public domain by Tom St Denis. */

#define CONST64(n) n ## ULL

/**
   Initialize the hash state
   @param md   The hash state you wish to initialize
   @return CRYPT_OK if successful
*/
void sha384_init(struct sha384_state *md)
{
	md->curlen = 0;
	md->length = 0;
	md->state[0] = CONST64(0xcbbb9d5dc1059ed8);
	md->state[1] = CONST64(0x629a292a367cd507);
	md->state[2] = CONST64(0x9159015a3070dd17);
	md->state[3] = CONST64(0x152fecd8f70e5939);
	md->state[4] = CONST64(0x67332667ffc00b31);
	md->state[5] = CONST64(0x8eb44a8768581511);
	md->state[6] = CONST64(0xdb0c2e0d64f98fa7);
	md->state[7] = CONST64(0x47b5481dbefa4fa4);
}

int sha384_process(struct sha384_state *md, const unsigned char *in,
		   unsigned long inlen)
{
	return sha512_process(md, in, inlen);
}

/**
   Terminate the hash to get the digest
   @param md  The hash state
   @param out [out] The destination of the hash (48 bytes)
   @return CRYPT_OK if successful
*/
int sha384_done(struct sha384_state *md, unsigned char *out)
{
	unsigned char buf[64];

	if (md->curlen >= sizeof(md->buf))
		return -1;

	if (sha512_done(md, buf) != 0)
		return -1;

	os_memcpy(out, buf, 48);
	return 0;
}

/* ===== end - public domain SHA384 implementation ===== */
