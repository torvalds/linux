/*
 * DRBG: Deterministic Random Bits Generator
 *       Based on NIST Recommended DRBG from NIST SP800-90A with the following
 *       properties:
 *		* CTR DRBG with DF with AES-128, AES-192, AES-256 cores
 *		* Hash DRBG with DF with SHA-1, SHA-256, SHA-384, SHA-512 cores
 *		* HMAC DRBG with DF with SHA-1, SHA-256, SHA-384, SHA-512 cores
 *		* with and without prediction resistance
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2014
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * DRBG Usage
 * ==========
 * The SP 800-90A DRBG allows the user to specify a personalization string
 * for initialization as well as an additional information string for each
 * random number request. The following code fragments show how a caller
 * uses the kernel crypto API to use the full functionality of the DRBG.
 *
 * Usage without any additional data
 * ---------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 *
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * err = crypto_rng_get_bytes(drng, &data, DATALEN);
 * crypto_free_rng(drng);
 *
 *
 * Usage with personalization string during initialization
 * -------------------------------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 * struct drbg_string pers;
 * char personalization[11] = "some-string";
 *
 * drbg_string_fill(&pers, personalization, strlen(personalization));
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * // The reset completely re-initializes the DRBG with the provided
 * // personalization string
 * err = crypto_rng_reset(drng, &personalization, strlen(personalization));
 * err = crypto_rng_get_bytes(drng, &data, DATALEN);
 * crypto_free_rng(drng);
 *
 *
 * Usage with additional information string during random number request
 * ---------------------------------------------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 * char addtl_string[11] = "some-string";
 * string drbg_string addtl;
 *
 * drbg_string_fill(&addtl, addtl_string, strlen(addtl_string));
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * // The following call is a wrapper to crypto_rng_get_bytes() and returns
 * // the same error codes.
 * err = crypto_drbg_get_bytes_addtl(drng, &data, DATALEN, &addtl);
 * crypto_free_rng(drng);
 *
 *
 * Usage with personalization and additional information strings
 * -------------------------------------------------------------
 * Just mix both scenarios above.
 */

#include <crypto/drbg.h>

/***************************************************************
 * Backend cipher definitions available to DRBG
 ***************************************************************/

/*
 * The order of the DRBG definitions here matter: every DRBG is registered
 * as stdrng. Each DRBG receives an increasing cra_priority values the later
 * they are defined in this array (see drbg_fill_array).
 *
 * HMAC DRBGs are favored over Hash DRBGs over CTR DRBGs, and
 * the SHA256 / AES 256 over other ciphers. Thus, the favored
 * DRBGs are the latest entries in this array.
 */
static const struct drbg_core drbg_cores[] = {
#ifdef CONFIG_CRYPTO_DRBG_CTR
	{
		.flags = DRBG_CTR | DRBG_STRENGTH128,
		.statelen = 32, /* 256 bits as defined in 10.2.1 */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes128",
		.backend_cra_name = "ecb(aes)",
	}, {
		.flags = DRBG_CTR | DRBG_STRENGTH192,
		.statelen = 40, /* 320 bits as defined in 10.2.1 */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes192",
		.backend_cra_name = "ecb(aes)",
	}, {
		.flags = DRBG_CTR | DRBG_STRENGTH256,
		.statelen = 48, /* 384 bits as defined in 10.2.1 */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes256",
		.backend_cra_name = "ecb(aes)",
	},
#endif /* CONFIG_CRYPTO_DRBG_CTR */
#ifdef CONFIG_CRYPTO_DRBG_HASH
	{
		.flags = DRBG_HASH | DRBG_STRENGTH128,
		.statelen = 55, /* 440 bits */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 20,
		.cra_name = "sha1",
		.backend_cra_name = "sha1",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 111, /* 888 bits */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 48,
		.cra_name = "sha384",
		.backend_cra_name = "sha384",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 111, /* 888 bits */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 64,
		.cra_name = "sha512",
		.backend_cra_name = "sha512",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 55, /* 440 bits */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 32,
		.cra_name = "sha256",
		.backend_cra_name = "sha256",
	},
#endif /* CONFIG_CRYPTO_DRBG_HASH */
#ifdef CONFIG_CRYPTO_DRBG_HMAC
	{
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 20, /* block length of cipher */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 20,
		.cra_name = "hmac_sha1",
		.backend_cra_name = "hmac(sha1)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 48, /* block length of cipher */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 48,
		.cra_name = "hmac_sha384",
		.backend_cra_name = "hmac(sha384)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 64, /* block length of cipher */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 64,
		.cra_name = "hmac_sha512",
		.backend_cra_name = "hmac(sha512)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 32, /* block length of cipher */
		.max_addtllen = 35,
		.max_bits = 19,
		.max_req = 48,
		.blocklen_bytes = 32,
		.cra_name = "hmac_sha256",
		.backend_cra_name = "hmac(sha256)",
	},
#endif /* CONFIG_CRYPTO_DRBG_HMAC */
};

/******************************************************************
 * Generic helper functions
 ******************************************************************/

/*
 * Return strength of DRBG according to SP800-90A section 8.4
 *
 * @flags DRBG flags reference
 *
 * Return: normalized strength in *bytes* value or 32 as default
 *	   to counter programming errors
 */
static inline unsigned short drbg_sec_strength(drbg_flag_t flags)
{
	switch (flags & DRBG_STRENGTH_MASK) {
	case DRBG_STRENGTH128:
		return 16;
	case DRBG_STRENGTH192:
		return 24;
	case DRBG_STRENGTH256:
		return 32;
	default:
		return 32;
	}
}

/*
 * FIPS 140-2 continuous self test
 * The test is performed on the result of one round of the output
 * function. Thus, the function implicitly knows the size of the
 * buffer.
 *
 * The FIPS test can be called in an endless loop until it returns
 * true. Although the code looks like a potential for a deadlock, it
 * is not the case, because returning a false cannot mathematically
 * occur (except once when a reseed took place and the updated state
 * would is now set up such that the generation of new value returns
 * an identical one -- this is most unlikely and would happen only once).
 * Thus, if this function repeatedly returns false and thus would cause
 * a deadlock, the integrity of the entire kernel is lost.
 *
 * @drbg DRBG handle
 * @buf output buffer of random data to be checked
 *
 * return:
 *	true on success
 *	false on error
 */
static bool drbg_fips_continuous_test(struct drbg_state *drbg,
				      const unsigned char *buf)
{
#ifdef CONFIG_CRYPTO_FIPS
	int ret = 0;
	/* skip test if we test the overall system */
	if (drbg->test_data)
		return true;
	/* only perform test in FIPS mode */
	if (0 == fips_enabled)
		return true;
	if (!drbg->fips_primed) {
		/* Priming of FIPS test */
		memcpy(drbg->prev, buf, drbg_blocklen(drbg));
		drbg->fips_primed = true;
		/* return false due to priming, i.e. another round is needed */
		return false;
	}
	ret = memcmp(drbg->prev, buf, drbg_blocklen(drbg));
	memcpy(drbg->prev, buf, drbg_blocklen(drbg));
	/* the test shall pass when the two compared values are not equal */
	return ret != 0;
#else
	return true;
#endif /* CONFIG_CRYPTO_FIPS */
}

/*
 * Convert an integer into a byte representation of this integer.
 * The byte representation is big-endian
 *
 * @buf buffer holding the converted integer
 * @val value to be converted
 * @buflen length of buffer
 */
#if (defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_CTR))
static inline void drbg_int2byte(unsigned char *buf, uint64_t val,
				 size_t buflen)
{
	unsigned char *byte;
	uint64_t i;

	byte = buf + (buflen - 1);
	for (i = 0; i < buflen; i++)
		*(byte--) = val >> (i * 8) & 0xff;
}

/*
 * Increment buffer
 *
 * @dst buffer to increment
 * @add value to add
 */
static inline void drbg_add_buf(unsigned char *dst, size_t dstlen,
				const unsigned char *add, size_t addlen)
{
	/* implied: dstlen > addlen */
	unsigned char *dstptr;
	const unsigned char *addptr;
	unsigned int remainder = 0;
	size_t len = addlen;

	dstptr = dst + (dstlen-1);
	addptr = add + (addlen-1);
	while (len) {
		remainder += *dstptr + *addptr;
		*dstptr = remainder & 0xff;
		remainder >>= 8;
		len--; dstptr--; addptr--;
	}
	len = dstlen - addlen;
	while (len && remainder > 0) {
		remainder = *dstptr + 1;
		*dstptr = remainder & 0xff;
		remainder >>= 8;
		len--; dstptr--;
	}
}
#endif /* defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_CTR) */

/******************************************************************
 * CTR DRBG callback functions
 ******************************************************************/

#ifdef CONFIG_CRYPTO_DRBG_CTR
#define CRYPTO_DRBG_CTR_STRING "CTR "
static int drbg_kcapi_sym(struct drbg_state *drbg, const unsigned char *key,
			  unsigned char *outval, const struct drbg_string *in);
static int drbg_init_sym_kernel(struct drbg_state *drbg);
static int drbg_fini_sym_kernel(struct drbg_state *drbg);

/* BCC function for CTR DRBG as defined in 10.4.3 */
static int drbg_ctr_bcc(struct drbg_state *drbg,
			unsigned char *out, const unsigned char *key,
			struct list_head *in)
{
	int ret = 0;
	struct drbg_string *curr = NULL;
	struct drbg_string data;
	short cnt = 0;

	drbg_string_fill(&data, out, drbg_blocklen(drbg));

	/* 10.4.3 step 1 */
	memset(out, 0, drbg_blocklen(drbg));

	/* 10.4.3 step 2 / 4 */
	list_for_each_entry(curr, in, list) {
		const unsigned char *pos = curr->buf;
		size_t len = curr->len;
		/* 10.4.3 step 4.1 */
		while (len) {
			/* 10.4.3 step 4.2 */
			if (drbg_blocklen(drbg) == cnt) {
				cnt = 0;
				ret = drbg_kcapi_sym(drbg, key, out, &data);
				if (ret)
					return ret;
			}
			out[cnt] ^= *pos;
			pos++;
			cnt++;
			len--;
		}
	}
	/* 10.4.3 step 4.2 for last block */
	if (cnt)
		ret = drbg_kcapi_sym(drbg, key, out, &data);

	return ret;
}

/*
 * scratchpad usage: drbg_ctr_update is interlinked with drbg_ctr_df
 * (and drbg_ctr_bcc, but this function does not need any temporary buffers),
 * the scratchpad is used as follows:
 * drbg_ctr_update:
 *	temp
 *		start: drbg->scratchpad
 *		length: drbg_statelen(drbg) + drbg_blocklen(drbg)
 *			note: the cipher writing into this variable works
 *			blocklen-wise. Now, when the statelen is not a multiple
 *			of blocklen, the generateion loop below "spills over"
 *			by at most blocklen. Thus, we need to give sufficient
 *			memory.
 *	df_data
 *		start: drbg->scratchpad +
 *				drbg_statelen(drbg) + drbg_blocklen(drbg)
 *		length: drbg_statelen(drbg)
 *
 * drbg_ctr_df:
 *	pad
 *		start: df_data + drbg_statelen(drbg)
 *		length: drbg_blocklen(drbg)
 *	iv
 *		start: pad + drbg_blocklen(drbg)
 *		length: drbg_blocklen(drbg)
 *	temp
 *		start: iv + drbg_blocklen(drbg)
 *		length: drbg_satelen(drbg) + drbg_blocklen(drbg)
 *			note: temp is the buffer that the BCC function operates
 *			on. BCC operates blockwise. drbg_statelen(drbg)
 *			is sufficient when the DRBG state length is a multiple
 *			of the block size. For AES192 (and maybe other ciphers)
 *			this is not correct and the length for temp is
 *			insufficient (yes, that also means for such ciphers,
 *			the final output of all BCC rounds are truncated).
 *			Therefore, add drbg_blocklen(drbg) to cover all
 *			possibilities.
 */

/* Derivation Function for CTR DRBG as defined in 10.4.2 */
static int drbg_ctr_df(struct drbg_state *drbg,
		       unsigned char *df_data, size_t bytes_to_return,
		       struct list_head *seedlist)
{
	int ret = -EFAULT;
	unsigned char L_N[8];
	/* S3 is input */
	struct drbg_string S1, S2, S4, cipherin;
	LIST_HEAD(bcc_list);
	unsigned char *pad = df_data + drbg_statelen(drbg);
	unsigned char *iv = pad + drbg_blocklen(drbg);
	unsigned char *temp = iv + drbg_blocklen(drbg);
	size_t padlen = 0;
	unsigned int templen = 0;
	/* 10.4.2 step 7 */
	unsigned int i = 0;
	/* 10.4.2 step 8 */
	const unsigned char *K = (unsigned char *)
			   "\x00\x01\x02\x03\x04\x05\x06\x07"
			   "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
			   "\x10\x11\x12\x13\x14\x15\x16\x17"
			   "\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";
	unsigned char *X;
	size_t generated_len = 0;
	size_t inputlen = 0;
	struct drbg_string *seed = NULL;

	memset(pad, 0, drbg_blocklen(drbg));
	memset(iv, 0, drbg_blocklen(drbg));
	memset(temp, 0, drbg_statelen(drbg));

	/* 10.4.2 step 1 is implicit as we work byte-wise */

	/* 10.4.2 step 2 */
	if ((512/8) < bytes_to_return)
		return -EINVAL;

	/* 10.4.2 step 2 -- calculate the entire length of all input data */
	list_for_each_entry(seed, seedlist, list)
		inputlen += seed->len;
	drbg_int2byte(&L_N[0], inputlen, 4);

	/* 10.4.2 step 3 */
	drbg_int2byte(&L_N[4], bytes_to_return, 4);

	/* 10.4.2 step 5: length is L_N, input_string, one byte, padding */
	padlen = (inputlen + sizeof(L_N) + 1) % (drbg_blocklen(drbg));
	/* wrap the padlen appropriately */
	if (padlen)
		padlen = drbg_blocklen(drbg) - padlen;
	/*
	 * pad / padlen contains the 0x80 byte and the following zero bytes.
	 * As the calculated padlen value only covers the number of zero
	 * bytes, this value has to be incremented by one for the 0x80 byte.
	 */
	padlen++;
	pad[0] = 0x80;

	/* 10.4.2 step 4 -- first fill the linked list and then order it */
	drbg_string_fill(&S1, iv, drbg_blocklen(drbg));
	list_add_tail(&S1.list, &bcc_list);
	drbg_string_fill(&S2, L_N, sizeof(L_N));
	list_add_tail(&S2.list, &bcc_list);
	list_splice_tail(seedlist, &bcc_list);
	drbg_string_fill(&S4, pad, padlen);
	list_add_tail(&S4.list, &bcc_list);

	/* 10.4.2 step 9 */
	while (templen < (drbg_keylen(drbg) + (drbg_blocklen(drbg)))) {
		/*
		 * 10.4.2 step 9.1 - the padding is implicit as the buffer
		 * holds zeros after allocation -- even the increment of i
		 * is irrelevant as the increment remains within length of i
		 */
		drbg_int2byte(iv, i, 4);
		/* 10.4.2 step 9.2 -- BCC and concatenation with temp */
		ret = drbg_ctr_bcc(drbg, temp + templen, K, &bcc_list);
		if (ret)
			goto out;
		/* 10.4.2 step 9.3 */
		i++;
		templen += drbg_blocklen(drbg);
	}

	/* 10.4.2 step 11 */
	X = temp + (drbg_keylen(drbg));
	drbg_string_fill(&cipherin, X, drbg_blocklen(drbg));

	/* 10.4.2 step 12: overwriting of outval is implemented in next step */

	/* 10.4.2 step 13 */
	while (generated_len < bytes_to_return) {
		short blocklen = 0;
		/*
		 * 10.4.2 step 13.1: the truncation of the key length is
		 * implicit as the key is only drbg_blocklen in size based on
		 * the implementation of the cipher function callback
		 */
		ret = drbg_kcapi_sym(drbg, temp, X, &cipherin);
		if (ret)
			goto out;
		blocklen = (drbg_blocklen(drbg) <
				(bytes_to_return - generated_len)) ?
			    drbg_blocklen(drbg) :
				(bytes_to_return - generated_len);
		/* 10.4.2 step 13.2 and 14 */
		memcpy(df_data + generated_len, X, blocklen);
		generated_len += blocklen;
	}

	ret = 0;

out:
	memset(iv, 0, drbg_blocklen(drbg));
	memset(temp, 0, drbg_statelen(drbg));
	memset(pad, 0, drbg_blocklen(drbg));
	return ret;
}

/* update function of CTR DRBG as defined in 10.2.1.2 */
static int drbg_ctr_update(struct drbg_state *drbg, struct list_head *seed,
			   int reseed)
{
	int ret = -EFAULT;
	/* 10.2.1.2 step 1 */
	unsigned char *temp = drbg->scratchpad;
	unsigned char *df_data = drbg->scratchpad + drbg_statelen(drbg) +
				 drbg_blocklen(drbg);
	unsigned char *temp_p, *df_data_p; /* pointer to iterate over buffers */
	unsigned int len = 0;
	struct drbg_string cipherin;
	unsigned char prefix = DRBG_PREFIX1;

	memset(temp, 0, drbg_statelen(drbg) + drbg_blocklen(drbg));
	memset(df_data, 0, drbg_statelen(drbg));

	/* 10.2.1.3.2 step 2 and 10.2.1.4.2 step 2 */
	if (seed) {
		ret = drbg_ctr_df(drbg, df_data, drbg_statelen(drbg), seed);
		if (ret)
			goto out;
	}

	drbg_string_fill(&cipherin, drbg->V, drbg_blocklen(drbg));
	/*
	 * 10.2.1.3.2 steps 2 and 3 are already covered as the allocation
	 * zeroizes all memory during initialization
	 */
	while (len < (drbg_statelen(drbg))) {
		/* 10.2.1.2 step 2.1 */
		drbg_add_buf(drbg->V, drbg_blocklen(drbg), &prefix, 1);
		/*
		 * 10.2.1.2 step 2.2 */
		ret = drbg_kcapi_sym(drbg, drbg->C, temp + len, &cipherin);
		if (ret)
			goto out;
		/* 10.2.1.2 step 2.3 and 3 */
		len += drbg_blocklen(drbg);
	}

	/* 10.2.1.2 step 4 */
	temp_p = temp;
	df_data_p = df_data;
	for (len = 0; len < drbg_statelen(drbg); len++) {
		*temp_p ^= *df_data_p;
		df_data_p++; temp_p++;
	}

	/* 10.2.1.2 step 5 */
	memcpy(drbg->C, temp, drbg_keylen(drbg));
	/* 10.2.1.2 step 6 */
	memcpy(drbg->V, temp + drbg_keylen(drbg), drbg_blocklen(drbg));
	ret = 0;

out:
	memset(temp, 0, drbg_statelen(drbg) + drbg_blocklen(drbg));
	memset(df_data, 0, drbg_statelen(drbg));
	return ret;
}

/*
 * scratchpad use: drbg_ctr_update is called independently from
 * drbg_ctr_extract_bytes. Therefore, the scratchpad is reused
 */
/* Generate function of CTR DRBG as defined in 10.2.1.5.2 */
static int drbg_ctr_generate(struct drbg_state *drbg,
			     unsigned char *buf, unsigned int buflen,
			     struct drbg_string *addtl)
{
	int len = 0;
	int ret = 0;
	struct drbg_string data;
	unsigned char prefix = DRBG_PREFIX1;

	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));

	/* 10.2.1.5.2 step 2 */
	if (addtl && 0 < addtl->len) {
		LIST_HEAD(addtllist);

		list_add_tail(&addtl->list, &addtllist);
		ret = drbg_ctr_update(drbg, &addtllist, 1);
		if (ret)
			return 0;
	}

	/* 10.2.1.5.2 step 4.1 */
	drbg_add_buf(drbg->V, drbg_blocklen(drbg), &prefix, 1);
	drbg_string_fill(&data, drbg->V, drbg_blocklen(drbg));
	while (len < buflen) {
		int outlen = 0;
		/* 10.2.1.5.2 step 4.2 */
		ret = drbg_kcapi_sym(drbg, drbg->C, drbg->scratchpad, &data);
		if (ret) {
			len = ret;
			goto out;
		}
		outlen = (drbg_blocklen(drbg) < (buflen - len)) ?
			  drbg_blocklen(drbg) : (buflen - len);
		if (!drbg_fips_continuous_test(drbg, drbg->scratchpad)) {
			/* 10.2.1.5.2 step 6 */
			drbg_add_buf(drbg->V, drbg_blocklen(drbg), &prefix, 1);
			continue;
		}
		/* 10.2.1.5.2 step 4.3 */
		memcpy(buf + len, drbg->scratchpad, outlen);
		len += outlen;
		/* 10.2.1.5.2 step 6 */
		if (len < buflen)
			drbg_add_buf(drbg->V, drbg_blocklen(drbg), &prefix, 1);
	}

	/*
	 * 10.2.1.5.2 step 6
	 * The following call invokes the DF function again which could be
	 * optimized. In step 2, the "additional_input" after step 2 is the
	 * output of the DF function. If this result would be saved, the DF
	 * function would not need to be invoked again at this point.
	 */
	if (addtl && 0 < addtl->len) {
		LIST_HEAD(addtllist);

		list_add_tail(&addtl->list, &addtllist);
		ret = drbg_ctr_update(drbg, &addtllist, 1);
	} else {
		ret = drbg_ctr_update(drbg, NULL, 1);
	}
	if (ret)
		len = ret;

out:
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));
	return len;
}

static struct drbg_state_ops drbg_ctr_ops = {
	.update		= drbg_ctr_update,
	.generate	= drbg_ctr_generate,
	.crypto_init	= drbg_init_sym_kernel,
	.crypto_fini	= drbg_fini_sym_kernel,
};
#endif /* CONFIG_CRYPTO_DRBG_CTR */

/******************************************************************
 * HMAC DRBG callback functions
 ******************************************************************/

#if defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_HMAC)
static int drbg_kcapi_hash(struct drbg_state *drbg, const unsigned char *key,
			   unsigned char *outval, const struct list_head *in);
static int drbg_init_hash_kernel(struct drbg_state *drbg);
static int drbg_fini_hash_kernel(struct drbg_state *drbg);
#endif /* (CONFIG_CRYPTO_DRBG_HASH || CONFIG_CRYPTO_DRBG_HMAC) */

#ifdef CONFIG_CRYPTO_DRBG_HMAC
#define CRYPTO_DRBG_HMAC_STRING "HMAC "
/* update function of HMAC DRBG as defined in 10.1.2.2 */
static int drbg_hmac_update(struct drbg_state *drbg, struct list_head *seed,
			    int reseed)
{
	int ret = -EFAULT;
	int i = 0;
	struct drbg_string seed1, seed2, vdata;
	LIST_HEAD(seedlist);
	LIST_HEAD(vdatalist);

	if (!reseed) {
		/* 10.1.2.3 step 2 */
		memset(drbg->C, 0, drbg_statelen(drbg));
		memset(drbg->V, 1, drbg_statelen(drbg));
	}

	drbg_string_fill(&seed1, drbg->V, drbg_statelen(drbg));
	list_add_tail(&seed1.list, &seedlist);
	/* buffer of seed2 will be filled in for loop below with one byte */
	drbg_string_fill(&seed2, NULL, 1);
	list_add_tail(&seed2.list, &seedlist);
	/* input data of seed is allowed to be NULL at this point */
	if (seed)
		list_splice_tail(seed, &seedlist);

	drbg_string_fill(&vdata, drbg->V, drbg_statelen(drbg));
	list_add_tail(&vdata.list, &vdatalist);
	for (i = 2; 0 < i; i--) {
		/* first round uses 0x0, second 0x1 */
		unsigned char prefix = DRBG_PREFIX0;
		if (1 == i)
			prefix = DRBG_PREFIX1;
		/* 10.1.2.2 step 1 and 4 -- concatenation and HMAC for key */
		seed2.buf = &prefix;
		ret = drbg_kcapi_hash(drbg, drbg->C, drbg->C, &seedlist);
		if (ret)
			return ret;

		/* 10.1.2.2 step 2 and 5 -- HMAC for V */
		ret = drbg_kcapi_hash(drbg, drbg->C, drbg->V, &vdatalist);
		if (ret)
			return ret;

		/* 10.1.2.2 step 3 */
		if (!seed)
			return ret;
	}

	return 0;
}

/* generate function of HMAC DRBG as defined in 10.1.2.5 */
static int drbg_hmac_generate(struct drbg_state *drbg,
			      unsigned char *buf,
			      unsigned int buflen,
			      struct drbg_string *addtl)
{
	int len = 0;
	int ret = 0;
	struct drbg_string data;
	LIST_HEAD(datalist);

	/* 10.1.2.5 step 2 */
	if (addtl && 0 < addtl->len) {
		LIST_HEAD(addtllist);

		list_add_tail(&addtl->list, &addtllist);
		ret = drbg_hmac_update(drbg, &addtllist, 1);
		if (ret)
			return ret;
	}

	drbg_string_fill(&data, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data.list, &datalist);
	while (len < buflen) {
		unsigned int outlen = 0;
		/* 10.1.2.5 step 4.1 */
		ret = drbg_kcapi_hash(drbg, drbg->C, drbg->V, &datalist);
		if (ret)
			return ret;
		outlen = (drbg_blocklen(drbg) < (buflen - len)) ?
			  drbg_blocklen(drbg) : (buflen - len);
		if (!drbg_fips_continuous_test(drbg, drbg->V))
			continue;

		/* 10.1.2.5 step 4.2 */
		memcpy(buf + len, drbg->V, outlen);
		len += outlen;
	}

	/* 10.1.2.5 step 6 */
	if (addtl && 0 < addtl->len) {
		LIST_HEAD(addtllist);

		list_add_tail(&addtl->list, &addtllist);
		ret = drbg_hmac_update(drbg, &addtllist, 1);
	} else {
		ret = drbg_hmac_update(drbg, NULL, 1);
	}
	if (ret)
		return ret;

	return len;
}

static struct drbg_state_ops drbg_hmac_ops = {
	.update		= drbg_hmac_update,
	.generate	= drbg_hmac_generate,
	.crypto_init	= drbg_init_hash_kernel,
	.crypto_fini	= drbg_fini_hash_kernel,

};
#endif /* CONFIG_CRYPTO_DRBG_HMAC */

/******************************************************************
 * Hash DRBG callback functions
 ******************************************************************/

#ifdef CONFIG_CRYPTO_DRBG_HASH
#define CRYPTO_DRBG_HASH_STRING "HASH "
/*
 * scratchpad usage: as drbg_hash_update and drbg_hash_df are used
 * interlinked, the scratchpad is used as follows:
 * drbg_hash_update
 *	start: drbg->scratchpad
 *	length: drbg_statelen(drbg)
 * drbg_hash_df:
 *	start: drbg->scratchpad + drbg_statelen(drbg)
 *	length: drbg_blocklen(drbg)
 *
 * drbg_hash_process_addtl uses the scratchpad, but fully completes
 * before either of the functions mentioned before are invoked. Therefore,
 * drbg_hash_process_addtl does not need to be specifically considered.
 */

/* Derivation Function for Hash DRBG as defined in 10.4.1 */
static int drbg_hash_df(struct drbg_state *drbg,
			unsigned char *outval, size_t outlen,
			struct list_head *entropylist)
{
	int ret = 0;
	size_t len = 0;
	unsigned char input[5];
	unsigned char *tmp = drbg->scratchpad + drbg_statelen(drbg);
	struct drbg_string data;

	memset(tmp, 0, drbg_blocklen(drbg));

	/* 10.4.1 step 3 */
	input[0] = 1;
	drbg_int2byte(&input[1], (outlen * 8), 4);

	/* 10.4.1 step 4.1 -- concatenation of data for input into hash */
	drbg_string_fill(&data, input, 5);
	list_add(&data.list, entropylist);

	/* 10.4.1 step 4 */
	while (len < outlen) {
		short blocklen = 0;
		/* 10.4.1 step 4.1 */
		ret = drbg_kcapi_hash(drbg, NULL, tmp, entropylist);
		if (ret)
			goto out;
		/* 10.4.1 step 4.2 */
		input[0]++;
		blocklen = (drbg_blocklen(drbg) < (outlen - len)) ?
			    drbg_blocklen(drbg) : (outlen - len);
		memcpy(outval + len, tmp, blocklen);
		len += blocklen;
	}

out:
	memset(tmp, 0, drbg_blocklen(drbg));
	return ret;
}

/* update function for Hash DRBG as defined in 10.1.1.2 / 10.1.1.3 */
static int drbg_hash_update(struct drbg_state *drbg, struct list_head *seed,
			    int reseed)
{
	int ret = 0;
	struct drbg_string data1, data2;
	LIST_HEAD(datalist);
	LIST_HEAD(datalist2);
	unsigned char *V = drbg->scratchpad;
	unsigned char prefix = DRBG_PREFIX1;

	memset(drbg->scratchpad, 0, drbg_statelen(drbg));
	if (!seed)
		return -EINVAL;

	if (reseed) {
		/* 10.1.1.3 step 1 */
		memcpy(V, drbg->V, drbg_statelen(drbg));
		drbg_string_fill(&data1, &prefix, 1);
		list_add_tail(&data1.list, &datalist);
		drbg_string_fill(&data2, V, drbg_statelen(drbg));
		list_add_tail(&data2.list, &datalist);
	}
	list_splice_tail(seed, &datalist);

	/* 10.1.1.2 / 10.1.1.3 step 2 and 3 */
	ret = drbg_hash_df(drbg, drbg->V, drbg_statelen(drbg), &datalist);
	if (ret)
		goto out;

	/* 10.1.1.2 / 10.1.1.3 step 4  */
	prefix = DRBG_PREFIX0;
	drbg_string_fill(&data1, &prefix, 1);
	list_add_tail(&data1.list, &datalist2);
	drbg_string_fill(&data2, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data2.list, &datalist2);
	/* 10.1.1.2 / 10.1.1.3 step 4 */
	ret = drbg_hash_df(drbg, drbg->C, drbg_statelen(drbg), &datalist2);

out:
	memset(drbg->scratchpad, 0, drbg_statelen(drbg));
	return ret;
}

/* processing of additional information string for Hash DRBG */
static int drbg_hash_process_addtl(struct drbg_state *drbg,
				   struct drbg_string *addtl)
{
	int ret = 0;
	struct drbg_string data1, data2;
	LIST_HEAD(datalist);
	unsigned char prefix = DRBG_PREFIX2;

	/* this is value w as per documentation */
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));

	/* 10.1.1.4 step 2 */
	if (!addtl || 0 == addtl->len)
		return 0;

	/* 10.1.1.4 step 2a */
	drbg_string_fill(&data1, &prefix, 1);
	drbg_string_fill(&data2, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data1.list, &datalist);
	list_add_tail(&data2.list, &datalist);
	list_add_tail(&addtl->list, &datalist);
	ret = drbg_kcapi_hash(drbg, NULL, drbg->scratchpad, &datalist);
	if (ret)
		goto out;

	/* 10.1.1.4 step 2b */
	drbg_add_buf(drbg->V, drbg_statelen(drbg),
		     drbg->scratchpad, drbg_blocklen(drbg));

out:
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));
	return ret;
}

/* Hashgen defined in 10.1.1.4 */
static int drbg_hash_hashgen(struct drbg_state *drbg,
			     unsigned char *buf,
			     unsigned int buflen)
{
	int len = 0;
	int ret = 0;
	unsigned char *src = drbg->scratchpad;
	unsigned char *dst = drbg->scratchpad + drbg_statelen(drbg);
	struct drbg_string data;
	LIST_HEAD(datalist);
	unsigned char prefix = DRBG_PREFIX1;

	memset(src, 0, drbg_statelen(drbg));
	memset(dst, 0, drbg_blocklen(drbg));

	/* 10.1.1.4 step hashgen 2 */
	memcpy(src, drbg->V, drbg_statelen(drbg));

	drbg_string_fill(&data, src, drbg_statelen(drbg));
	list_add_tail(&data.list, &datalist);
	while (len < buflen) {
		unsigned int outlen = 0;
		/* 10.1.1.4 step hashgen 4.1 */
		ret = drbg_kcapi_hash(drbg, NULL, dst, &datalist);
		if (ret) {
			len = ret;
			goto out;
		}
		outlen = (drbg_blocklen(drbg) < (buflen - len)) ?
			  drbg_blocklen(drbg) : (buflen - len);
		if (!drbg_fips_continuous_test(drbg, dst)) {
			drbg_add_buf(src, drbg_statelen(drbg), &prefix, 1);
			continue;
		}
		/* 10.1.1.4 step hashgen 4.2 */
		memcpy(buf + len, dst, outlen);
		len += outlen;
		/* 10.1.1.4 hashgen step 4.3 */
		if (len < buflen)
			drbg_add_buf(src, drbg_statelen(drbg), &prefix, 1);
	}

out:
	memset(drbg->scratchpad, 0,
	       (drbg_statelen(drbg) + drbg_blocklen(drbg)));
	return len;
}

/* generate function for Hash DRBG as defined in  10.1.1.4 */
static int drbg_hash_generate(struct drbg_state *drbg,
			      unsigned char *buf, unsigned int buflen,
			      struct drbg_string *addtl)
{
	int len = 0;
	int ret = 0;
	unsigned char req[8];
	unsigned char prefix = DRBG_PREFIX3;
	struct drbg_string data1, data2;
	LIST_HEAD(datalist);

	/* 10.1.1.4 step 2 */
	ret = drbg_hash_process_addtl(drbg, addtl);
	if (ret)
		return ret;
	/* 10.1.1.4 step 3 */
	len = drbg_hash_hashgen(drbg, buf, buflen);

	/* this is the value H as documented in 10.1.1.4 */
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));
	/* 10.1.1.4 step 4 */
	drbg_string_fill(&data1, &prefix, 1);
	list_add_tail(&data1.list, &datalist);
	drbg_string_fill(&data2, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data2.list, &datalist);
	ret = drbg_kcapi_hash(drbg, NULL, drbg->scratchpad, &datalist);
	if (ret) {
		len = ret;
		goto out;
	}

	/* 10.1.1.4 step 5 */
	drbg_add_buf(drbg->V, drbg_statelen(drbg),
		     drbg->scratchpad, drbg_blocklen(drbg));
	drbg_add_buf(drbg->V, drbg_statelen(drbg),
		     drbg->C, drbg_statelen(drbg));
	drbg_int2byte(req, drbg->reseed_ctr, sizeof(req));
	drbg_add_buf(drbg->V, drbg_statelen(drbg), req, 8);

out:
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));
	return len;
}

/*
 * scratchpad usage: as update and generate are used isolated, both
 * can use the scratchpad
 */
static struct drbg_state_ops drbg_hash_ops = {
	.update		= drbg_hash_update,
	.generate	= drbg_hash_generate,
	.crypto_init	= drbg_init_hash_kernel,
	.crypto_fini	= drbg_fini_hash_kernel,
};
#endif /* CONFIG_CRYPTO_DRBG_HASH */

/******************************************************************
 * Functions common for DRBG implementations
 ******************************************************************/

/*
 * Seeding or reseeding of the DRBG
 *
 * @drbg: DRBG state struct
 * @pers: personalization / additional information buffer
 * @reseed: 0 for initial seed process, 1 for reseeding
 *
 * return:
 *	0 on success
 *	error value otherwise
 */
static int drbg_seed(struct drbg_state *drbg, struct drbg_string *pers,
		     bool reseed)
{
	int ret = 0;
	unsigned char *entropy = NULL;
	size_t entropylen = 0;
	struct drbg_string data1;
	LIST_HEAD(seedlist);

	/* 9.1 / 9.2 / 9.3.1 step 3 */
	if (pers && pers->len > (drbg_max_addtl(drbg))) {
		pr_devel("DRBG: personalization string too long %zu\n",
			 pers->len);
		return -EINVAL;
	}

	if (drbg->test_data && drbg->test_data->testentropy) {
		drbg_string_fill(&data1, drbg->test_data->testentropy->buf,
				 drbg->test_data->testentropy->len);
		pr_devel("DRBG: using test entropy\n");
	} else {
		/*
		 * Gather entropy equal to the security strength of the DRBG.
		 * With a derivation function, a nonce is required in addition
		 * to the entropy. A nonce must be at least 1/2 of the security
		 * strength of the DRBG in size. Thus, entropy * nonce is 3/2
		 * of the strength. The consideration of a nonce is only
		 * applicable during initial seeding.
		 */
		entropylen = drbg_sec_strength(drbg->core->flags);
		if (!entropylen)
			return -EFAULT;
		if (!reseed)
			entropylen = ((entropylen + 1) / 2) * 3;
		pr_devel("DRBG: (re)seeding with %zu bytes of entropy\n",
			 entropylen);
		entropy = kzalloc(entropylen, GFP_KERNEL);
		if (!entropy)
			return -ENOMEM;
		get_random_bytes(entropy, entropylen);
		drbg_string_fill(&data1, entropy, entropylen);
	}
	list_add_tail(&data1.list, &seedlist);

	/*
	 * concatenation of entropy with personalization str / addtl input)
	 * the variable pers is directly handed in by the caller, so check its
	 * contents whether it is appropriate
	 */
	if (pers && pers->buf && 0 < pers->len) {
		list_add_tail(&pers->list, &seedlist);
		pr_devel("DRBG: using personalization string\n");
	}

	ret = drbg->d_ops->update(drbg, &seedlist, reseed);
	if (ret)
		goto out;

	drbg->seeded = true;
	/* 10.1.1.2 / 10.1.1.3 step 5 */
	drbg->reseed_ctr = 1;

out:
	if (entropy)
		kzfree(entropy);
	return ret;
}

/* Free all substructures in a DRBG state without the DRBG state structure */
static inline void drbg_dealloc_state(struct drbg_state *drbg)
{
	if (!drbg)
		return;
	if (drbg->V)
		kzfree(drbg->V);
	drbg->V = NULL;
	if (drbg->C)
		kzfree(drbg->C);
	drbg->C = NULL;
	if (drbg->scratchpad)
		kzfree(drbg->scratchpad);
	drbg->scratchpad = NULL;
	drbg->reseed_ctr = 0;
#ifdef CONFIG_CRYPTO_FIPS
	if (drbg->prev)
		kzfree(drbg->prev);
	drbg->prev = NULL;
	drbg->fips_primed = false;
#endif
}

/*
 * Allocate all sub-structures for a DRBG state.
 * The DRBG state structure must already be allocated.
 */
static inline int drbg_alloc_state(struct drbg_state *drbg)
{
	int ret = -ENOMEM;
	unsigned int sb_size = 0;

	if (!drbg)
		return -EINVAL;

	drbg->V = kzalloc(drbg_statelen(drbg), GFP_KERNEL);
	if (!drbg->V)
		goto err;
	drbg->C = kzalloc(drbg_statelen(drbg), GFP_KERNEL);
	if (!drbg->C)
		goto err;
#ifdef CONFIG_CRYPTO_FIPS
	drbg->prev = kzalloc(drbg_blocklen(drbg), GFP_KERNEL);
	if (!drbg->prev)
		goto err;
	drbg->fips_primed = false;
#endif
	/* scratchpad is only generated for CTR and Hash */
	if (drbg->core->flags & DRBG_HMAC)
		sb_size = 0;
	else if (drbg->core->flags & DRBG_CTR)
		sb_size = drbg_statelen(drbg) + drbg_blocklen(drbg) + /* temp */
			  drbg_statelen(drbg) +	/* df_data */
			  drbg_blocklen(drbg) +	/* pad */
			  drbg_blocklen(drbg) +	/* iv */
			  drbg_statelen(drbg) + drbg_blocklen(drbg); /* temp */
	else
		sb_size = drbg_statelen(drbg) + drbg_blocklen(drbg);

	if (0 < sb_size) {
		drbg->scratchpad = kzalloc(sb_size, GFP_KERNEL);
		if (!drbg->scratchpad)
			goto err;
	}
	spin_lock_init(&drbg->drbg_lock);
	return 0;

err:
	drbg_dealloc_state(drbg);
	return ret;
}

/*
 * Strategy to avoid holding long term locks: generate a shadow copy of DRBG
 * and perform all operations on this shadow copy. After finishing, restore
 * the updated state of the shadow copy into original drbg state. This way,
 * only the read and write operations of the original drbg state must be
 * locked
 */
static inline void drbg_copy_drbg(struct drbg_state *src,
				  struct drbg_state *dst)
{
	if (!src || !dst)
		return;
	memcpy(dst->V, src->V, drbg_statelen(src));
	memcpy(dst->C, src->C, drbg_statelen(src));
	dst->reseed_ctr = src->reseed_ctr;
	dst->seeded = src->seeded;
	dst->pr = src->pr;
#ifdef CONFIG_CRYPTO_FIPS
	dst->fips_primed = src->fips_primed;
	memcpy(dst->prev, src->prev, drbg_blocklen(src));
#endif
	/*
	 * Not copied:
	 * scratchpad is initialized drbg_alloc_state;
	 * priv_data is initialized with call to crypto_init;
	 * d_ops and core are set outside, as these parameters are const;
	 * test_data is set outside to prevent it being copied back.
	 */
}

static int drbg_make_shadow(struct drbg_state *drbg, struct drbg_state **shadow)
{
	int ret = -ENOMEM;
	struct drbg_state *tmp = NULL;

	if (!drbg || !drbg->core || !drbg->V || !drbg->C) {
		pr_devel("DRBG: attempt to generate shadow copy for "
			 "uninitialized DRBG state rejected\n");
		return -EINVAL;
	}
	/* HMAC does not have a scratchpad */
	if (!(drbg->core->flags & DRBG_HMAC) && NULL == drbg->scratchpad)
		return -EINVAL;

	tmp = kzalloc(sizeof(struct drbg_state), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* read-only data as they are defined as const, no lock needed */
	tmp->core = drbg->core;
	tmp->d_ops = drbg->d_ops;

	ret = drbg_alloc_state(tmp);
	if (ret)
		goto err;

	spin_lock_bh(&drbg->drbg_lock);
	drbg_copy_drbg(drbg, tmp);
	/* only make a link to the test buffer, as we only read that data */
	tmp->test_data = drbg->test_data;
	spin_unlock_bh(&drbg->drbg_lock);
	*shadow = tmp;
	return 0;

err:
	if (tmp)
		kzfree(tmp);
	return ret;
}

static void drbg_restore_shadow(struct drbg_state *drbg,
				struct drbg_state **shadow)
{
	struct drbg_state *tmp = *shadow;

	spin_lock_bh(&drbg->drbg_lock);
	drbg_copy_drbg(tmp, drbg);
	spin_unlock_bh(&drbg->drbg_lock);
	drbg_dealloc_state(tmp);
	kzfree(tmp);
	*shadow = NULL;
}

/*************************************************************************
 * DRBG interface functions
 *************************************************************************/

/*
 * DRBG generate function as required by SP800-90A - this function
 * generates random numbers
 *
 * @drbg DRBG state handle
 * @buf Buffer where to store the random numbers -- the buffer must already
 *      be pre-allocated by caller
 * @buflen Length of output buffer - this value defines the number of random
 *	   bytes pulled from DRBG
 * @addtl Additional input that is mixed into state, may be NULL -- note
 *	  the entropy is pulled by the DRBG internally unconditionally
 *	  as defined in SP800-90A. The additional input is mixed into
 *	  the state in addition to the pulled entropy.
 *
 * return: generated number of bytes
 */
static int drbg_generate(struct drbg_state *drbg,
			 unsigned char *buf, unsigned int buflen,
			 struct drbg_string *addtl)
{
	int len = 0;
	struct drbg_state *shadow = NULL;

	if (0 == buflen || !buf) {
		pr_devel("DRBG: no output buffer provided\n");
		return -EINVAL;
	}
	if (addtl && NULL == addtl->buf && 0 < addtl->len) {
		pr_devel("DRBG: wrong format of additional information\n");
		return -EINVAL;
	}

	len = drbg_make_shadow(drbg, &shadow);
	if (len) {
		pr_devel("DRBG: shadow copy cannot be generated\n");
		return len;
	}

	/* 9.3.1 step 2 */
	len = -EINVAL;
	if (buflen > (drbg_max_request_bytes(shadow))) {
		pr_devel("DRBG: requested random numbers too large %u\n",
			 buflen);
		goto err;
	}

	/* 9.3.1 step 3 is implicit with the chosen DRBG */

	/* 9.3.1 step 4 */
	if (addtl && addtl->len > (drbg_max_addtl(shadow))) {
		pr_devel("DRBG: additional information string too long %zu\n",
			 addtl->len);
		goto err;
	}
	/* 9.3.1 step 5 is implicit with the chosen DRBG */

	/*
	 * 9.3.1 step 6 and 9 supplemented by 9.3.2 step c is implemented
	 * here. The spec is a bit convoluted here, we make it simpler.
	 */
	if ((drbg_max_requests(shadow)) < shadow->reseed_ctr)
		shadow->seeded = false;

	/* allocate cipher handle */
	if (shadow->d_ops->crypto_init) {
		len = shadow->d_ops->crypto_init(shadow);
		if (len)
			goto err;
	}

	if (shadow->pr || !shadow->seeded) {
		pr_devel("DRBG: reseeding before generation (prediction "
			 "resistance: %s, state %s)\n",
			 drbg->pr ? "true" : "false",
			 drbg->seeded ? "seeded" : "unseeded");
		/* 9.3.1 steps 7.1 through 7.3 */
		len = drbg_seed(shadow, addtl, true);
		if (len)
			goto err;
		/* 9.3.1 step 7.4 */
		addtl = NULL;
	}
	/* 9.3.1 step 8 and 10 */
	len = shadow->d_ops->generate(shadow, buf, buflen, addtl);

	/* 10.1.1.4 step 6, 10.1.2.5 step 7, 10.2.1.5.2 step 7 */
	shadow->reseed_ctr++;
	if (0 >= len)
		goto err;

	/*
	 * Section 11.3.3 requires to re-perform self tests after some
	 * generated random numbers. The chosen value after which self
	 * test is performed is arbitrary, but it should be reasonable.
	 * However, we do not perform the self tests because of the following
	 * reasons: it is mathematically impossible that the initial self tests
	 * were successfully and the following are not. If the initial would
	 * pass and the following would not, the kernel integrity is violated.
	 * In this case, the entire kernel operation is questionable and it
	 * is unlikely that the integrity violation only affects the
	 * correct operation of the DRBG.
	 *
	 * Albeit the following code is commented out, it is provided in
	 * case somebody has a need to implement the test of 11.3.3.
	 */
#if 0
	if (shadow->reseed_ctr && !(shadow->reseed_ctr % 4096)) {
		int err = 0;
		pr_devel("DRBG: start to perform self test\n");
		if (drbg->core->flags & DRBG_HMAC)
			err = alg_test("drbg_pr_hmac_sha256",
				       "drbg_pr_hmac_sha256", 0, 0);
		else if (drbg->core->flags & DRBG_CTR)
			err = alg_test("drbg_pr_ctr_aes128",
				       "drbg_pr_ctr_aes128", 0, 0);
		else
			err = alg_test("drbg_pr_sha256",
				       "drbg_pr_sha256", 0, 0);
		if (err) {
			pr_err("DRBG: periodical self test failed\n");
			/*
			 * uninstantiate implies that from now on, only errors
			 * are returned when reusing this DRBG cipher handle
			 */
			drbg_uninstantiate(drbg);
			drbg_dealloc_state(shadow);
			kzfree(shadow);
			return 0;
		} else {
			pr_devel("DRBG: self test successful\n");
		}
	}
#endif

err:
	if (shadow->d_ops->crypto_fini)
		shadow->d_ops->crypto_fini(shadow);
	drbg_restore_shadow(drbg, &shadow);
	return len;
}

/*
 * Wrapper around drbg_generate which can pull arbitrary long strings
 * from the DRBG without hitting the maximum request limitation.
 *
 * Parameters: see drbg_generate
 * Return codes: see drbg_generate -- if one drbg_generate request fails,
 *		 the entire drbg_generate_long request fails
 */
static int drbg_generate_long(struct drbg_state *drbg,
			      unsigned char *buf, unsigned int buflen,
			      struct drbg_string *addtl)
{
	int len = 0;
	unsigned int slice = 0;
	do {
		int tmplen = 0;
		unsigned int chunk = 0;
		slice = ((buflen - len) / drbg_max_request_bytes(drbg));
		chunk = slice ? drbg_max_request_bytes(drbg) : (buflen - len);
		tmplen = drbg_generate(drbg, buf + len, chunk, addtl);
		if (0 >= tmplen)
			return tmplen;
		len += tmplen;
	} while (slice > 0);
	return len;
}

/*
 * DRBG instantiation function as required by SP800-90A - this function
 * sets up the DRBG handle, performs the initial seeding and all sanity
 * checks required by SP800-90A
 *
 * @drbg memory of state -- if NULL, new memory is allocated
 * @pers Personalization string that is mixed into state, may be NULL -- note
 *	 the entropy is pulled by the DRBG internally unconditionally
 *	 as defined in SP800-90A. The additional input is mixed into
 *	 the state in addition to the pulled entropy.
 * @coreref reference to core
 * @pr prediction resistance enabled
 *
 * return
 *	0 on success
 *	error value otherwise
 */
static int drbg_instantiate(struct drbg_state *drbg, struct drbg_string *pers,
			    int coreref, bool pr)
{
	int ret = -ENOMEM;

	pr_devel("DRBG: Initializing DRBG core %d with prediction resistance "
		 "%s\n", coreref, pr ? "enabled" : "disabled");
	drbg->core = &drbg_cores[coreref];
	drbg->pr = pr;
	drbg->seeded = false;
	switch (drbg->core->flags & DRBG_TYPE_MASK) {
#ifdef CONFIG_CRYPTO_DRBG_HMAC
	case DRBG_HMAC:
		drbg->d_ops = &drbg_hmac_ops;
		break;
#endif /* CONFIG_CRYPTO_DRBG_HMAC */
#ifdef CONFIG_CRYPTO_DRBG_HASH
	case DRBG_HASH:
		drbg->d_ops = &drbg_hash_ops;
		break;
#endif /* CONFIG_CRYPTO_DRBG_HASH */
#ifdef CONFIG_CRYPTO_DRBG_CTR
	case DRBG_CTR:
		drbg->d_ops = &drbg_ctr_ops;
		break;
#endif /* CONFIG_CRYPTO_DRBG_CTR */
	default:
		return -EOPNOTSUPP;
	}

	/* 9.1 step 1 is implicit with the selected DRBG type */

	/*
	 * 9.1 step 2 is implicit as caller can select prediction resistance
	 * and the flag is copied into drbg->flags --
	 * all DRBG types support prediction resistance
	 */

	/* 9.1 step 4 is implicit in  drbg_sec_strength */

	ret = drbg_alloc_state(drbg);
	if (ret)
		return ret;

	ret = -EFAULT;
	if (drbg->d_ops->crypto_init && drbg->d_ops->crypto_init(drbg))
		goto err;
	ret = drbg_seed(drbg, pers, false);
	if (drbg->d_ops->crypto_fini)
		drbg->d_ops->crypto_fini(drbg);
	if (ret)
		goto err;

	return 0;

err:
	drbg_dealloc_state(drbg);
	return ret;
}

/*
 * DRBG uninstantiate function as required by SP800-90A - this function
 * frees all buffers and the DRBG handle
 *
 * @drbg DRBG state handle
 *
 * return
 *	0 on success
 */
static int drbg_uninstantiate(struct drbg_state *drbg)
{
	spin_lock_bh(&drbg->drbg_lock);
	drbg_dealloc_state(drbg);
	/* no scrubbing of test_data -- this shall survive an uninstantiate */
	spin_unlock_bh(&drbg->drbg_lock);
	return 0;
}

/*
 * Helper function for setting the test data in the DRBG
 *
 * @drbg DRBG state handle
 * @test_data test data to sets
 */
static inline void drbg_set_testdata(struct drbg_state *drbg,
				     struct drbg_test_data *test_data)
{
	if (!test_data || !test_data->testentropy)
		return;
	spin_lock_bh(&drbg->drbg_lock);
	drbg->test_data = test_data;
	spin_unlock_bh(&drbg->drbg_lock);
}

/***************************************************************
 * Kernel crypto API cipher invocations requested by DRBG
 ***************************************************************/

#if defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_HMAC)
struct sdesc {
	struct shash_desc shash;
	char ctx[];
};

static int drbg_init_hash_kernel(struct drbg_state *drbg)
{
	struct sdesc *sdesc;
	struct crypto_shash *tfm;

	tfm = crypto_alloc_shash(drbg->core->backend_cra_name, 0, 0);
	if (IS_ERR(tfm)) {
		pr_info("DRBG: could not allocate digest TFM handle\n");
		return PTR_ERR(tfm);
	}
	BUG_ON(drbg_blocklen(drbg) != crypto_shash_digestsize(tfm));
	sdesc = kzalloc(sizeof(struct shash_desc) + crypto_shash_descsize(tfm),
			GFP_KERNEL);
	if (!sdesc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	sdesc->shash.tfm = tfm;
	sdesc->shash.flags = 0;
	drbg->priv_data = sdesc;
	return 0;
}

static int drbg_fini_hash_kernel(struct drbg_state *drbg)
{
	struct sdesc *sdesc = (struct sdesc *)drbg->priv_data;
	if (sdesc) {
		crypto_free_shash(sdesc->shash.tfm);
		kzfree(sdesc);
	}
	drbg->priv_data = NULL;
	return 0;
}

static int drbg_kcapi_hash(struct drbg_state *drbg, const unsigned char *key,
			   unsigned char *outval, const struct list_head *in)
{
	struct sdesc *sdesc = (struct sdesc *)drbg->priv_data;
	struct drbg_string *input = NULL;

	if (key)
		crypto_shash_setkey(sdesc->shash.tfm, key, drbg_statelen(drbg));
	crypto_shash_init(&sdesc->shash);
	list_for_each_entry(input, in, list)
		crypto_shash_update(&sdesc->shash, input->buf, input->len);
	return crypto_shash_final(&sdesc->shash, outval);
}
#endif /* (CONFIG_CRYPTO_DRBG_HASH || CONFIG_CRYPTO_DRBG_HMAC) */

#ifdef CONFIG_CRYPTO_DRBG_CTR
static int drbg_init_sym_kernel(struct drbg_state *drbg)
{
	int ret = 0;
	struct crypto_blkcipher *tfm;

	tfm = crypto_alloc_blkcipher(drbg->core->backend_cra_name, 0, 0);
	if (IS_ERR(tfm)) {
		pr_info("DRBG: could not allocate cipher TFM handle\n");
		return PTR_ERR(tfm);
	}
	BUG_ON(drbg_blocklen(drbg) != crypto_blkcipher_blocksize(tfm));
	drbg->priv_data = tfm;
	return ret;
}

static int drbg_fini_sym_kernel(struct drbg_state *drbg)
{
	struct crypto_blkcipher *tfm =
		(struct crypto_blkcipher *)drbg->priv_data;
	if (tfm)
		crypto_free_blkcipher(tfm);
	drbg->priv_data = NULL;
	return 0;
}

static int drbg_kcapi_sym(struct drbg_state *drbg, const unsigned char *key,
			  unsigned char *outval, const struct drbg_string *in)
{
	int ret = 0;
	struct scatterlist sg_in, sg_out;
	struct blkcipher_desc desc;
	struct crypto_blkcipher *tfm =
		(struct crypto_blkcipher *)drbg->priv_data;

	desc.tfm = tfm;
	desc.flags = 0;
	crypto_blkcipher_setkey(tfm, key, (drbg_keylen(drbg)));
	/* there is only component in *in */
	sg_init_one(&sg_in, in->buf, in->len);
	sg_init_one(&sg_out, outval, drbg_blocklen(drbg));
	ret = crypto_blkcipher_encrypt(&desc, &sg_out, &sg_in, in->len);

	return ret;
}
#endif /* CONFIG_CRYPTO_DRBG_CTR */

/***************************************************************
 * Kernel crypto API interface to register DRBG
 ***************************************************************/

/*
 * Look up the DRBG flags by given kernel crypto API cra_name
 * The code uses the drbg_cores definition to do this
 *
 * @cra_name kernel crypto API cra_name
 * @coreref reference to integer which is filled with the pointer to
 *  the applicable core
 * @pr reference for setting prediction resistance
 *
 * return: flags
 */
static inline void drbg_convert_tfm_core(const char *cra_driver_name,
					 int *coreref, bool *pr)
{
	int i = 0;
	size_t start = 0;
	int len = 0;

	*pr = true;
	/* disassemble the names */
	if (!memcmp(cra_driver_name, "drbg_nopr_", 10)) {
		start = 10;
		*pr = false;
	} else if (!memcmp(cra_driver_name, "drbg_pr_", 8)) {
		start = 8;
	} else {
		return;
	}

	/* remove the first part */
	len = strlen(cra_driver_name) - start;
	for (i = 0; ARRAY_SIZE(drbg_cores) > i; i++) {
		if (!memcmp(cra_driver_name + start, drbg_cores[i].cra_name,
			    len)) {
			*coreref = i;
			return;
		}
	}
}

static int drbg_kcapi_init(struct crypto_tfm *tfm)
{
	struct drbg_state *drbg = crypto_tfm_ctx(tfm);
	bool pr = false;
	int coreref = 0;

	drbg_convert_tfm_core(crypto_tfm_alg_name(tfm), &coreref, &pr);
	/*
	 * when personalization string is needed, the caller must call reset
	 * and provide the personalization string as seed information
	 */
	return drbg_instantiate(drbg, NULL, coreref, pr);
}

static void drbg_kcapi_cleanup(struct crypto_tfm *tfm)
{
	drbg_uninstantiate(crypto_tfm_ctx(tfm));
}

/*
 * Generate random numbers invoked by the kernel crypto API:
 * The API of the kernel crypto API is extended as follows:
 *
 * If dlen is larger than zero, rdata is interpreted as the output buffer
 * where random data is to be stored.
 *
 * If dlen is zero, rdata is interpreted as a pointer to a struct drbg_gen
 * which holds the additional information string that is used for the
 * DRBG generation process. The output buffer that is to be used to store
 * data is also pointed to by struct drbg_gen.
 */
static int drbg_kcapi_random(struct crypto_rng *tfm, u8 *rdata,
			     unsigned int dlen)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);
	if (0 < dlen) {
		return drbg_generate_long(drbg, rdata, dlen, NULL);
	} else {
		struct drbg_gen *data = (struct drbg_gen *)rdata;
		struct drbg_string addtl;
		/* catch NULL pointer */
		if (!data)
			return 0;
		drbg_set_testdata(drbg, data->test_data);
		/* linked list variable is now local to allow modification */
		drbg_string_fill(&addtl, data->addtl->buf, data->addtl->len);
		return drbg_generate_long(drbg, data->outbuf, data->outlen,
					  &addtl);
	}
}

/*
 * Reset the DRBG invoked by the kernel crypto API
 * The reset implies a full re-initialization of the DRBG. Similar to the
 * generate function of drbg_kcapi_random, this function extends the
 * kernel crypto API interface with struct drbg_gen
 */
static int drbg_kcapi_reset(struct crypto_rng *tfm, u8 *seed, unsigned int slen)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);
	struct crypto_tfm *tfm_base = crypto_rng_tfm(tfm);
	bool pr = false;
	struct drbg_string seed_string;
	int coreref = 0;

	drbg_uninstantiate(drbg);
	drbg_convert_tfm_core(crypto_tfm_alg_driver_name(tfm_base), &coreref,
			      &pr);
	if (0 < slen) {
		drbg_string_fill(&seed_string, seed, slen);
		return drbg_instantiate(drbg, &seed_string, coreref, pr);
	} else {
		struct drbg_gen *data = (struct drbg_gen *)seed;
		/* allow invocation of API call with NULL, 0 */
		if (!data)
			return drbg_instantiate(drbg, NULL, coreref, pr);
		drbg_set_testdata(drbg, data->test_data);
		/* linked list variable is now local to allow modification */
		drbg_string_fill(&seed_string, data->addtl->buf,
				 data->addtl->len);
		return drbg_instantiate(drbg, &seed_string, coreref, pr);
	}
}

/***************************************************************
 * Kernel module: code to load the module
 ***************************************************************/

/*
 * Tests as defined in 11.3.2 in addition to the cipher tests: testing
 * of the error handling.
 *
 * Note: testing of failing seed source as defined in 11.3.2 is not applicable
 * as seed source of get_random_bytes does not fail.
 *
 * Note 2: There is no sensible way of testing the reseed counter
 * enforcement, so skip it.
 */
static inline int __init drbg_healthcheck_sanity(void)
{
#ifdef CONFIG_CRYPTO_FIPS
	int len = 0;
#define OUTBUFLEN 16
	unsigned char buf[OUTBUFLEN];
	struct drbg_state *drbg = NULL;
	int ret = -EFAULT;
	int rc = -EFAULT;
	bool pr = false;
	int coreref = 0;
	struct drbg_string addtl;
	size_t max_addtllen, max_request_bytes;

	/* only perform test in FIPS mode */
	if (!fips_enabled)
		return 0;

#ifdef CONFIG_CRYPTO_DRBG_CTR
	drbg_convert_tfm_core("drbg_nopr_ctr_aes128", &coreref, &pr);
#elif defined CONFIG_CRYPTO_DRBG_HASH
	drbg_convert_tfm_core("drbg_nopr_sha256", &coreref, &pr);
#else
	drbg_convert_tfm_core("drbg_nopr_hmac_sha256", &coreref, &pr);
#endif

	drbg = kzalloc(sizeof(struct drbg_state), GFP_KERNEL);
	if (!drbg)
		return -ENOMEM;

	/*
	 * if the following tests fail, it is likely that there is a buffer
	 * overflow as buf is much smaller than the requested or provided
	 * string lengths -- in case the error handling does not succeed
	 * we may get an OOPS. And we want to get an OOPS as this is a
	 * grave bug.
	 */

	/* get a valid instance of DRBG for following tests */
	ret = drbg_instantiate(drbg, NULL, coreref, pr);
	if (ret) {
		rc = ret;
		goto outbuf;
	}
	max_addtllen = drbg_max_addtl(drbg);
	max_request_bytes = drbg_max_request_bytes(drbg);
	drbg_string_fill(&addtl, buf, max_addtllen + 1);
	/* overflow addtllen with additonal info string */
	len = drbg_generate(drbg, buf, OUTBUFLEN, &addtl);
	BUG_ON(0 < len);
	/* overflow max_bits */
	len = drbg_generate(drbg, buf, (max_request_bytes + 1), NULL);
	BUG_ON(0 < len);
	drbg_uninstantiate(drbg);

	/* overflow max addtllen with personalization string */
	ret = drbg_instantiate(drbg, &addtl, coreref, pr);
	BUG_ON(0 == ret);
	/* test uninstantated DRBG */
	len = drbg_generate(drbg, buf, (max_request_bytes + 1), NULL);
	BUG_ON(0 < len);
	/* all tests passed */
	rc = 0;

	pr_devel("DRBG: Sanity tests for failure code paths successfully "
		 "completed\n");

	drbg_uninstantiate(drbg);
outbuf:
	kzfree(drbg);
	return rc;
#else /* CONFIG_CRYPTO_FIPS */
	return 0;
#endif /* CONFIG_CRYPTO_FIPS */
}

static struct crypto_alg drbg_algs[22];

/*
 * Fill the array drbg_algs used to register the different DRBGs
 * with the kernel crypto API. To fill the array, the information
 * from drbg_cores[] is used.
 */
static inline void __init drbg_fill_array(struct crypto_alg *alg,
					  const struct drbg_core *core, int pr)
{
	int pos = 0;
	static int priority = 100;

	memset(alg, 0, sizeof(struct crypto_alg));
	memcpy(alg->cra_name, "stdrng", 6);
	if (pr) {
		memcpy(alg->cra_driver_name, "drbg_pr_", 8);
		pos = 8;
	} else {
		memcpy(alg->cra_driver_name, "drbg_nopr_", 10);
		pos = 10;
	}
	memcpy(alg->cra_driver_name + pos, core->cra_name,
	       strlen(core->cra_name));

	alg->cra_priority = priority;
	priority++;
	/*
	 * If FIPS mode enabled, the selected DRBG shall have the
	 * highest cra_priority over other stdrng instances to ensure
	 * it is selected.
	 */
	if (fips_enabled)
		alg->cra_priority += 200;

	alg->cra_flags		= CRYPTO_ALG_TYPE_RNG;
	alg->cra_ctxsize 	= sizeof(struct drbg_state);
	alg->cra_type		= &crypto_rng_type;
	alg->cra_module		= THIS_MODULE;
	alg->cra_init		= drbg_kcapi_init;
	alg->cra_exit		= drbg_kcapi_cleanup;
	alg->cra_u.rng.rng_make_random	= drbg_kcapi_random;
	alg->cra_u.rng.rng_reset	= drbg_kcapi_reset;
	alg->cra_u.rng.seedsize	= 0;
}

static int __init drbg_init(void)
{
	unsigned int i = 0; /* pointer to drbg_algs */
	unsigned int j = 0; /* pointer to drbg_cores */
	int ret = -EFAULT;

	ret = drbg_healthcheck_sanity();
	if (ret)
		return ret;

	if (ARRAY_SIZE(drbg_cores) * 2 > ARRAY_SIZE(drbg_algs)) {
		pr_info("DRBG: Cannot register all DRBG types"
			"(slots needed: %zu, slots available: %zu)\n",
			ARRAY_SIZE(drbg_cores) * 2, ARRAY_SIZE(drbg_algs));
		return ret;
	}

	/*
	 * each DRBG definition can be used with PR and without PR, thus
	 * we instantiate each DRBG in drbg_cores[] twice.
	 *
	 * As the order of placing them into the drbg_algs array matters
	 * (the later DRBGs receive a higher cra_priority) we register the
	 * prediction resistance DRBGs first as the should not be too
	 * interesting.
	 */
	for (j = 0; ARRAY_SIZE(drbg_cores) > j; j++, i++)
		drbg_fill_array(&drbg_algs[i], &drbg_cores[j], 1);
	for (j = 0; ARRAY_SIZE(drbg_cores) > j; j++, i++)
		drbg_fill_array(&drbg_algs[i], &drbg_cores[j], 0);
	return crypto_register_algs(drbg_algs, (ARRAY_SIZE(drbg_cores) * 2));
}

void __exit drbg_exit(void)
{
	crypto_unregister_algs(drbg_algs, (ARRAY_SIZE(drbg_cores) * 2));
}

module_init(drbg_init);
module_exit(drbg_exit);
#ifndef CRYPTO_DRBG_HASH_STRING
#define CRYPTO_DRBG_HASH_STRING ""
#endif
#ifndef CRYPTO_DRBG_HMAC_STRING
#define CRYPTO_DRBG_HMAC_STRING ""
#endif
#ifndef CRYPTO_DRBG_CTR_STRING
#define CRYPTO_DRBG_CTR_STRING ""
#endif
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("NIST SP800-90A Deterministic Random Bit Generator (DRBG) "
		   "using following cores: "
		   CRYPTO_DRBG_HASH_STRING
		   CRYPTO_DRBG_HMAC_STRING
		   CRYPTO_DRBG_CTR_STRING);
