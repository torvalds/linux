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
#include <linux/kernel.h>

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
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes128",
		.backend_cra_name = "aes",
	}, {
		.flags = DRBG_CTR | DRBG_STRENGTH192,
		.statelen = 40, /* 320 bits as defined in 10.2.1 */
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes192",
		.backend_cra_name = "aes",
	}, {
		.flags = DRBG_CTR | DRBG_STRENGTH256,
		.statelen = 48, /* 384 bits as defined in 10.2.1 */
		.blocklen_bytes = 16,
		.cra_name = "ctr_aes256",
		.backend_cra_name = "aes",
	},
#endif /* CONFIG_CRYPTO_DRBG_CTR */
#ifdef CONFIG_CRYPTO_DRBG_HASH
	{
		.flags = DRBG_HASH | DRBG_STRENGTH128,
		.statelen = 55, /* 440 bits */
		.blocklen_bytes = 20,
		.cra_name = "sha1",
		.backend_cra_name = "sha1",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 111, /* 888 bits */
		.blocklen_bytes = 48,
		.cra_name = "sha384",
		.backend_cra_name = "sha384",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 111, /* 888 bits */
		.blocklen_bytes = 64,
		.cra_name = "sha512",
		.backend_cra_name = "sha512",
	}, {
		.flags = DRBG_HASH | DRBG_STRENGTH256,
		.statelen = 55, /* 440 bits */
		.blocklen_bytes = 32,
		.cra_name = "sha256",
		.backend_cra_name = "sha256",
	},
#endif /* CONFIG_CRYPTO_DRBG_HASH */
#ifdef CONFIG_CRYPTO_DRBG_HMAC
	{
		.flags = DRBG_HMAC | DRBG_STRENGTH128,
		.statelen = 20, /* block length of cipher */
		.blocklen_bytes = 20,
		.cra_name = "hmac_sha1",
		.backend_cra_name = "hmac(sha1)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 48, /* block length of cipher */
		.blocklen_bytes = 48,
		.cra_name = "hmac_sha384",
		.backend_cra_name = "hmac(sha384)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 64, /* block length of cipher */
		.blocklen_bytes = 64,
		.cra_name = "hmac_sha512",
		.backend_cra_name = "hmac(sha512)",
	}, {
		.flags = DRBG_HMAC | DRBG_STRENGTH256,
		.statelen = 32, /* block length of cipher */
		.blocklen_bytes = 32,
		.cra_name = "hmac_sha256",
		.backend_cra_name = "hmac(sha256)",
	},
#endif /* CONFIG_CRYPTO_DRBG_HMAC */
};

static int drbg_uninstantiate(struct drbg_state *drbg);

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
 * Convert an integer into a byte representation of this integer.
 * The byte representation is big-endian
 *
 * @val value to be converted
 * @buf buffer holding the converted integer -- caller must ensure that
 *      buffer size is at least 32 bit
 */
#if (defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_CTR))
static inline void drbg_cpu_to_be32(__u32 val, unsigned char *buf)
{
	struct s {
		__be32 conv;
	};
	struct s *conversion = (struct s *) buf;

	conversion->conv = cpu_to_be32(val);
}
#endif /* defined(CONFIG_CRYPTO_DRBG_HASH) || defined(CONFIG_CRYPTO_DRBG_CTR) */

/******************************************************************
 * CTR DRBG callback functions
 ******************************************************************/

#ifdef CONFIG_CRYPTO_DRBG_CTR
#define CRYPTO_DRBG_CTR_STRING "CTR "
MODULE_ALIAS_CRYPTO("drbg_pr_ctr_aes256");
MODULE_ALIAS_CRYPTO("drbg_nopr_ctr_aes256");
MODULE_ALIAS_CRYPTO("drbg_pr_ctr_aes192");
MODULE_ALIAS_CRYPTO("drbg_nopr_ctr_aes192");
MODULE_ALIAS_CRYPTO("drbg_pr_ctr_aes128");
MODULE_ALIAS_CRYPTO("drbg_nopr_ctr_aes128");

static void drbg_kcapi_symsetkey(struct drbg_state *drbg,
				 const unsigned char *key);
static int drbg_kcapi_sym(struct drbg_state *drbg, unsigned char *outval,
			  const struct drbg_string *in);
static int drbg_init_sym_kernel(struct drbg_state *drbg);
static int drbg_fini_sym_kernel(struct drbg_state *drbg);
static int drbg_kcapi_sym_ctr(struct drbg_state *drbg,
			      u8 *inbuf, u32 inbuflen,
			      u8 *outbuf, u32 outlen);
#define DRBG_CTR_NULL_LEN 128
#define DRBG_OUTSCRATCHLEN DRBG_CTR_NULL_LEN

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

	/* 10.4.3 step 2 / 4 */
	drbg_kcapi_symsetkey(drbg, key);
	list_for_each_entry(curr, in, list) {
		const unsigned char *pos = curr->buf;
		size_t len = curr->len;
		/* 10.4.3 step 4.1 */
		while (len) {
			/* 10.4.3 step 4.2 */
			if (drbg_blocklen(drbg) == cnt) {
				cnt = 0;
				ret = drbg_kcapi_sym(drbg, out, &data);
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
		ret = drbg_kcapi_sym(drbg, out, &data);

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

	/* 10.4.2 step 1 is implicit as we work byte-wise */

	/* 10.4.2 step 2 */
	if ((512/8) < bytes_to_return)
		return -EINVAL;

	/* 10.4.2 step 2 -- calculate the entire length of all input data */
	list_for_each_entry(seed, seedlist, list)
		inputlen += seed->len;
	drbg_cpu_to_be32(inputlen, &L_N[0]);

	/* 10.4.2 step 3 */
	drbg_cpu_to_be32(bytes_to_return, &L_N[4]);

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
		drbg_cpu_to_be32(i, iv);
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
	drbg_kcapi_symsetkey(drbg, temp);
	while (generated_len < bytes_to_return) {
		short blocklen = 0;
		/*
		 * 10.4.2 step 13.1: the truncation of the key length is
		 * implicit as the key is only drbg_blocklen in size based on
		 * the implementation of the cipher function callback
		 */
		ret = drbg_kcapi_sym(drbg, X, &cipherin);
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
	memset(temp, 0, drbg_statelen(drbg) + drbg_blocklen(drbg));
	memset(pad, 0, drbg_blocklen(drbg));
	return ret;
}

/*
 * update function of CTR DRBG as defined in 10.2.1.2
 *
 * The reseed variable has an enhanced meaning compared to the update
 * functions of the other DRBGs as follows:
 * 0 => initial seed from initialization
 * 1 => reseed via drbg_seed
 * 2 => first invocation from drbg_ctr_update when addtl is present. In
 *      this case, the df_data scratchpad is not deleted so that it is
 *      available for another calls to prevent calling the DF function
 *      again.
 * 3 => second invocation from drbg_ctr_update. When the update function
 *      was called with addtl, the df_data memory already contains the
 *      DFed addtl information and we do not need to call DF again.
 */
static int drbg_ctr_update(struct drbg_state *drbg, struct list_head *seed,
			   int reseed)
{
	int ret = -EFAULT;
	/* 10.2.1.2 step 1 */
	unsigned char *temp = drbg->scratchpad;
	unsigned char *df_data = drbg->scratchpad + drbg_statelen(drbg) +
				 drbg_blocklen(drbg);

	if (3 > reseed)
		memset(df_data, 0, drbg_statelen(drbg));

	if (!reseed) {
		/*
		 * The DRBG uses the CTR mode of the underlying AES cipher. The
		 * CTR mode increments the counter value after the AES operation
		 * but SP800-90A requires that the counter is incremented before
		 * the AES operation. Hence, we increment it at the time we set
		 * it by one.
		 */
		crypto_inc(drbg->V, drbg_blocklen(drbg));

		ret = crypto_skcipher_setkey(drbg->ctr_handle, drbg->C,
					     drbg_keylen(drbg));
		if (ret)
			goto out;
	}

	/* 10.2.1.3.2 step 2 and 10.2.1.4.2 step 2 */
	if (seed) {
		ret = drbg_ctr_df(drbg, df_data, drbg_statelen(drbg), seed);
		if (ret)
			goto out;
	}

	ret = drbg_kcapi_sym_ctr(drbg, df_data, drbg_statelen(drbg),
				 temp, drbg_statelen(drbg));
	if (ret)
		return ret;

	/* 10.2.1.2 step 5 */
	ret = crypto_skcipher_setkey(drbg->ctr_handle, temp,
				     drbg_keylen(drbg));
	if (ret)
		goto out;
	/* 10.2.1.2 step 6 */
	memcpy(drbg->V, temp + drbg_keylen(drbg), drbg_blocklen(drbg));
	/* See above: increment counter by one to compensate timing of CTR op */
	crypto_inc(drbg->V, drbg_blocklen(drbg));
	ret = 0;

out:
	memset(temp, 0, drbg_statelen(drbg) + drbg_blocklen(drbg));
	if (2 != reseed)
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
			     struct list_head *addtl)
{
	int ret;
	int len = min_t(int, buflen, INT_MAX);

	/* 10.2.1.5.2 step 2 */
	if (addtl && !list_empty(addtl)) {
		ret = drbg_ctr_update(drbg, addtl, 2);
		if (ret)
			return 0;
	}

	/* 10.2.1.5.2 step 4.1 */
	ret = drbg_kcapi_sym_ctr(drbg, drbg->ctr_null_value, DRBG_CTR_NULL_LEN,
				 buf, len);
	if (ret)
		return ret;

	/* 10.2.1.5.2 step 6 */
	ret = drbg_ctr_update(drbg, NULL, 3);
	if (ret)
		len = ret;

	return len;
}

static const struct drbg_state_ops drbg_ctr_ops = {
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
static int drbg_kcapi_hash(struct drbg_state *drbg, unsigned char *outval,
			   const struct list_head *in);
static void drbg_kcapi_hmacsetkey(struct drbg_state *drbg,
				  const unsigned char *key);
static int drbg_init_hash_kernel(struct drbg_state *drbg);
static int drbg_fini_hash_kernel(struct drbg_state *drbg);
#endif /* (CONFIG_CRYPTO_DRBG_HASH || CONFIG_CRYPTO_DRBG_HMAC) */

#ifdef CONFIG_CRYPTO_DRBG_HMAC
#define CRYPTO_DRBG_HMAC_STRING "HMAC "
MODULE_ALIAS_CRYPTO("drbg_pr_hmac_sha512");
MODULE_ALIAS_CRYPTO("drbg_nopr_hmac_sha512");
MODULE_ALIAS_CRYPTO("drbg_pr_hmac_sha384");
MODULE_ALIAS_CRYPTO("drbg_nopr_hmac_sha384");
MODULE_ALIAS_CRYPTO("drbg_pr_hmac_sha256");
MODULE_ALIAS_CRYPTO("drbg_nopr_hmac_sha256");
MODULE_ALIAS_CRYPTO("drbg_pr_hmac_sha1");
MODULE_ALIAS_CRYPTO("drbg_nopr_hmac_sha1");

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
		/* 10.1.2.3 step 2 -- memset(0) of C is implicit with kzalloc */
		memset(drbg->V, 1, drbg_statelen(drbg));
		drbg_kcapi_hmacsetkey(drbg, drbg->C);
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
		ret = drbg_kcapi_hash(drbg, drbg->C, &seedlist);
		if (ret)
			return ret;
		drbg_kcapi_hmacsetkey(drbg, drbg->C);

		/* 10.1.2.2 step 2 and 5 -- HMAC for V */
		ret = drbg_kcapi_hash(drbg, drbg->V, &vdatalist);
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
			      struct list_head *addtl)
{
	int len = 0;
	int ret = 0;
	struct drbg_string data;
	LIST_HEAD(datalist);

	/* 10.1.2.5 step 2 */
	if (addtl && !list_empty(addtl)) {
		ret = drbg_hmac_update(drbg, addtl, 1);
		if (ret)
			return ret;
	}

	drbg_string_fill(&data, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data.list, &datalist);
	while (len < buflen) {
		unsigned int outlen = 0;
		/* 10.1.2.5 step 4.1 */
		ret = drbg_kcapi_hash(drbg, drbg->V, &datalist);
		if (ret)
			return ret;
		outlen = (drbg_blocklen(drbg) < (buflen - len)) ?
			  drbg_blocklen(drbg) : (buflen - len);

		/* 10.1.2.5 step 4.2 */
		memcpy(buf + len, drbg->V, outlen);
		len += outlen;
	}

	/* 10.1.2.5 step 6 */
	if (addtl && !list_empty(addtl))
		ret = drbg_hmac_update(drbg, addtl, 1);
	else
		ret = drbg_hmac_update(drbg, NULL, 1);
	if (ret)
		return ret;

	return len;
}

static const struct drbg_state_ops drbg_hmac_ops = {
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
MODULE_ALIAS_CRYPTO("drbg_pr_sha512");
MODULE_ALIAS_CRYPTO("drbg_nopr_sha512");
MODULE_ALIAS_CRYPTO("drbg_pr_sha384");
MODULE_ALIAS_CRYPTO("drbg_nopr_sha384");
MODULE_ALIAS_CRYPTO("drbg_pr_sha256");
MODULE_ALIAS_CRYPTO("drbg_nopr_sha256");
MODULE_ALIAS_CRYPTO("drbg_pr_sha1");
MODULE_ALIAS_CRYPTO("drbg_nopr_sha1");

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

	/* 10.4.1 step 3 */
	input[0] = 1;
	drbg_cpu_to_be32((outlen * 8), &input[1]);

	/* 10.4.1 step 4.1 -- concatenation of data for input into hash */
	drbg_string_fill(&data, input, 5);
	list_add(&data.list, entropylist);

	/* 10.4.1 step 4 */
	while (len < outlen) {
		short blocklen = 0;
		/* 10.4.1 step 4.1 */
		ret = drbg_kcapi_hash(drbg, tmp, entropylist);
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
				   struct list_head *addtl)
{
	int ret = 0;
	struct drbg_string data1, data2;
	LIST_HEAD(datalist);
	unsigned char prefix = DRBG_PREFIX2;

	/* 10.1.1.4 step 2 */
	if (!addtl || list_empty(addtl))
		return 0;

	/* 10.1.1.4 step 2a */
	drbg_string_fill(&data1, &prefix, 1);
	drbg_string_fill(&data2, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data1.list, &datalist);
	list_add_tail(&data2.list, &datalist);
	list_splice_tail(addtl, &datalist);
	ret = drbg_kcapi_hash(drbg, drbg->scratchpad, &datalist);
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

	/* 10.1.1.4 step hashgen 2 */
	memcpy(src, drbg->V, drbg_statelen(drbg));

	drbg_string_fill(&data, src, drbg_statelen(drbg));
	list_add_tail(&data.list, &datalist);
	while (len < buflen) {
		unsigned int outlen = 0;
		/* 10.1.1.4 step hashgen 4.1 */
		ret = drbg_kcapi_hash(drbg, dst, &datalist);
		if (ret) {
			len = ret;
			goto out;
		}
		outlen = (drbg_blocklen(drbg) < (buflen - len)) ?
			  drbg_blocklen(drbg) : (buflen - len);
		/* 10.1.1.4 step hashgen 4.2 */
		memcpy(buf + len, dst, outlen);
		len += outlen;
		/* 10.1.1.4 hashgen step 4.3 */
		if (len < buflen)
			crypto_inc(src, drbg_statelen(drbg));
	}

out:
	memset(drbg->scratchpad, 0,
	       (drbg_statelen(drbg) + drbg_blocklen(drbg)));
	return len;
}

/* generate function for Hash DRBG as defined in  10.1.1.4 */
static int drbg_hash_generate(struct drbg_state *drbg,
			      unsigned char *buf, unsigned int buflen,
			      struct list_head *addtl)
{
	int len = 0;
	int ret = 0;
	union {
		unsigned char req[8];
		__be64 req_int;
	} u;
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
	/* 10.1.1.4 step 4 */
	drbg_string_fill(&data1, &prefix, 1);
	list_add_tail(&data1.list, &datalist);
	drbg_string_fill(&data2, drbg->V, drbg_statelen(drbg));
	list_add_tail(&data2.list, &datalist);
	ret = drbg_kcapi_hash(drbg, drbg->scratchpad, &datalist);
	if (ret) {
		len = ret;
		goto out;
	}

	/* 10.1.1.4 step 5 */
	drbg_add_buf(drbg->V, drbg_statelen(drbg),
		     drbg->scratchpad, drbg_blocklen(drbg));
	drbg_add_buf(drbg->V, drbg_statelen(drbg),
		     drbg->C, drbg_statelen(drbg));
	u.req_int = cpu_to_be64(drbg->reseed_ctr);
	drbg_add_buf(drbg->V, drbg_statelen(drbg), u.req, 8);

out:
	memset(drbg->scratchpad, 0, drbg_blocklen(drbg));
	return len;
}

/*
 * scratchpad usage: as update and generate are used isolated, both
 * can use the scratchpad
 */
static const struct drbg_state_ops drbg_hash_ops = {
	.update		= drbg_hash_update,
	.generate	= drbg_hash_generate,
	.crypto_init	= drbg_init_hash_kernel,
	.crypto_fini	= drbg_fini_hash_kernel,
};
#endif /* CONFIG_CRYPTO_DRBG_HASH */

/******************************************************************
 * Functions common for DRBG implementations
 ******************************************************************/

static inline int __drbg_seed(struct drbg_state *drbg, struct list_head *seed,
			      int reseed)
{
	int ret = drbg->d_ops->update(drbg, seed, reseed);

	if (ret)
		return ret;

	drbg->seeded = true;
	/* 10.1.1.2 / 10.1.1.3 step 5 */
	drbg->reseed_ctr = 1;

	return ret;
}

static void drbg_async_seed(struct work_struct *work)
{
	struct drbg_string data;
	LIST_HEAD(seedlist);
	struct drbg_state *drbg = container_of(work, struct drbg_state,
					       seed_work);
	unsigned int entropylen = drbg_sec_strength(drbg->core->flags);
	unsigned char entropy[32];

	BUG_ON(!entropylen);
	BUG_ON(entropylen > sizeof(entropy));
	get_random_bytes(entropy, entropylen);

	drbg_string_fill(&data, entropy, entropylen);
	list_add_tail(&data.list, &seedlist);

	mutex_lock(&drbg->drbg_mutex);

	/* If nonblocking pool is initialized, deactivate Jitter RNG */
	crypto_free_rng(drbg->jent);
	drbg->jent = NULL;

	/* Set seeded to false so that if __drbg_seed fails the
	 * next generate call will trigger a reseed.
	 */
	drbg->seeded = false;

	__drbg_seed(drbg, &seedlist, true);

	if (drbg->seeded)
		drbg->reseed_threshold = drbg_max_requests(drbg);

	mutex_unlock(&drbg->drbg_mutex);

	memzero_explicit(entropy, entropylen);
}

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
	int ret;
	unsigned char entropy[((32 + 16) * 2)];
	unsigned int entropylen = drbg_sec_strength(drbg->core->flags);
	struct drbg_string data1;
	LIST_HEAD(seedlist);

	/* 9.1 / 9.2 / 9.3.1 step 3 */
	if (pers && pers->len > (drbg_max_addtl(drbg))) {
		pr_devel("DRBG: personalization string too long %zu\n",
			 pers->len);
		return -EINVAL;
	}

	if (list_empty(&drbg->test_data.list)) {
		drbg_string_fill(&data1, drbg->test_data.buf,
				 drbg->test_data.len);
		pr_devel("DRBG: using test entropy\n");
	} else {
		/*
		 * Gather entropy equal to the security strength of the DRBG.
		 * With a derivation function, a nonce is required in addition
		 * to the entropy. A nonce must be at least 1/2 of the security
		 * strength of the DRBG in size. Thus, entropy + nonce is 3/2
		 * of the strength. The consideration of a nonce is only
		 * applicable during initial seeding.
		 */
		BUG_ON(!entropylen);
		if (!reseed)
			entropylen = ((entropylen + 1) / 2) * 3;
		BUG_ON((entropylen * 2) > sizeof(entropy));

		/* Get seed from in-kernel /dev/urandom */
		get_random_bytes(entropy, entropylen);

		if (!drbg->jent) {
			drbg_string_fill(&data1, entropy, entropylen);
			pr_devel("DRBG: (re)seeding with %u bytes of entropy\n",
				 entropylen);
		} else {
			/* Get seed from Jitter RNG */
			ret = crypto_rng_get_bytes(drbg->jent,
						   entropy + entropylen,
						   entropylen);
			if (ret) {
				pr_devel("DRBG: jent failed with %d\n", ret);
				return ret;
			}

			drbg_string_fill(&data1, entropy, entropylen * 2);
			pr_devel("DRBG: (re)seeding with %u bytes of entropy\n",
				 entropylen * 2);
		}
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

	if (!reseed) {
		memset(drbg->V, 0, drbg_statelen(drbg));
		memset(drbg->C, 0, drbg_statelen(drbg));
	}

	ret = __drbg_seed(drbg, &seedlist, reseed);

	memzero_explicit(entropy, entropylen * 2);

	return ret;
}

/* Free all substructures in a DRBG state without the DRBG state structure */
static inline void drbg_dealloc_state(struct drbg_state *drbg)
{
	if (!drbg)
		return;
	kzfree(drbg->V);
	drbg->Vbuf = NULL;
	kzfree(drbg->C);
	drbg->Cbuf = NULL;
	kzfree(drbg->scratchpadbuf);
	drbg->scratchpadbuf = NULL;
	drbg->reseed_ctr = 0;
	drbg->d_ops = NULL;
	drbg->core = NULL;
}

/*
 * Allocate all sub-structures for a DRBG state.
 * The DRBG state structure must already be allocated.
 */
static inline int drbg_alloc_state(struct drbg_state *drbg)
{
	int ret = -ENOMEM;
	unsigned int sb_size = 0;

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
		ret = -EOPNOTSUPP;
		goto err;
	}

	ret = drbg->d_ops->crypto_init(drbg);
	if (ret < 0)
		goto err;

	drbg->Vbuf = kmalloc(drbg_statelen(drbg) + ret, GFP_KERNEL);
	if (!drbg->Vbuf) {
		ret = -ENOMEM;
		goto fini;
	}
	drbg->V = PTR_ALIGN(drbg->Vbuf, ret + 1);
	drbg->Cbuf = kmalloc(drbg_statelen(drbg) + ret, GFP_KERNEL);
	if (!drbg->Cbuf) {
		ret = -ENOMEM;
		goto fini;
	}
	drbg->C = PTR_ALIGN(drbg->Cbuf, ret + 1);
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
		drbg->scratchpadbuf = kzalloc(sb_size + ret, GFP_KERNEL);
		if (!drbg->scratchpadbuf) {
			ret = -ENOMEM;
			goto fini;
		}
		drbg->scratchpad = PTR_ALIGN(drbg->scratchpadbuf, ret + 1);
	}

	return 0;

fini:
	drbg->d_ops->crypto_fini(drbg);
err:
	drbg_dealloc_state(drbg);
	return ret;
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
 * return: 0 when all bytes are generated; < 0 in case of an error
 */
static int drbg_generate(struct drbg_state *drbg,
			 unsigned char *buf, unsigned int buflen,
			 struct drbg_string *addtl)
{
	int len = 0;
	LIST_HEAD(addtllist);

	if (!drbg->core) {
		pr_devel("DRBG: not yet seeded\n");
		return -EINVAL;
	}
	if (0 == buflen || !buf) {
		pr_devel("DRBG: no output buffer provided\n");
		return -EINVAL;
	}
	if (addtl && NULL == addtl->buf && 0 < addtl->len) {
		pr_devel("DRBG: wrong format of additional information\n");
		return -EINVAL;
	}

	/* 9.3.1 step 2 */
	len = -EINVAL;
	if (buflen > (drbg_max_request_bytes(drbg))) {
		pr_devel("DRBG: requested random numbers too large %u\n",
			 buflen);
		goto err;
	}

	/* 9.3.1 step 3 is implicit with the chosen DRBG */

	/* 9.3.1 step 4 */
	if (addtl && addtl->len > (drbg_max_addtl(drbg))) {
		pr_devel("DRBG: additional information string too long %zu\n",
			 addtl->len);
		goto err;
	}
	/* 9.3.1 step 5 is implicit with the chosen DRBG */

	/*
	 * 9.3.1 step 6 and 9 supplemented by 9.3.2 step c is implemented
	 * here. The spec is a bit convoluted here, we make it simpler.
	 */
	if (drbg->reseed_threshold < drbg->reseed_ctr)
		drbg->seeded = false;

	if (drbg->pr || !drbg->seeded) {
		pr_devel("DRBG: reseeding before generation (prediction "
			 "resistance: %s, state %s)\n",
			 drbg->pr ? "true" : "false",
			 drbg->seeded ? "seeded" : "unseeded");
		/* 9.3.1 steps 7.1 through 7.3 */
		len = drbg_seed(drbg, addtl, true);
		if (len)
			goto err;
		/* 9.3.1 step 7.4 */
		addtl = NULL;
	}

	if (addtl && 0 < addtl->len)
		list_add_tail(&addtl->list, &addtllist);
	/* 9.3.1 step 8 and 10 */
	len = drbg->d_ops->generate(drbg, buf, buflen, &addtllist);

	/* 10.1.1.4 step 6, 10.1.2.5 step 7, 10.2.1.5.2 step 7 */
	drbg->reseed_ctr++;
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
	if (drbg->reseed_ctr && !(drbg->reseed_ctr % 4096)) {
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
			return 0;
		} else {
			pr_devel("DRBG: self test successful\n");
		}
	}
#endif

	/*
	 * All operations were successful, return 0 as mandated by
	 * the kernel crypto API interface.
	 */
	len = 0;
err:
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
	unsigned int len = 0;
	unsigned int slice = 0;
	do {
		int err = 0;
		unsigned int chunk = 0;
		slice = ((buflen - len) / drbg_max_request_bytes(drbg));
		chunk = slice ? drbg_max_request_bytes(drbg) : (buflen - len);
		mutex_lock(&drbg->drbg_mutex);
		err = drbg_generate(drbg, buf + len, chunk, addtl);
		mutex_unlock(&drbg->drbg_mutex);
		if (0 > err)
			return err;
		len += chunk;
	} while (slice > 0 && (len < buflen));
	return 0;
}

static void drbg_schedule_async_seed(struct random_ready_callback *rdy)
{
	struct drbg_state *drbg = container_of(rdy, struct drbg_state,
					       random_ready);

	schedule_work(&drbg->seed_work);
}

static int drbg_prepare_hrng(struct drbg_state *drbg)
{
	int err;

	/* We do not need an HRNG in test mode. */
	if (list_empty(&drbg->test_data.list))
		return 0;

	INIT_WORK(&drbg->seed_work, drbg_async_seed);

	drbg->random_ready.owner = THIS_MODULE;
	drbg->random_ready.func = drbg_schedule_async_seed;

	err = add_random_ready_callback(&drbg->random_ready);

	switch (err) {
	case 0:
		break;

	case -EALREADY:
		err = 0;
		/* fall through */

	default:
		drbg->random_ready.func = NULL;
		return err;
	}

	drbg->jent = crypto_alloc_rng("jitterentropy_rng", 0, 0);

	/*
	 * Require frequent reseeds until the seed source is fully
	 * initialized.
	 */
	drbg->reseed_threshold = 50;

	return err;
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
	int ret;
	bool reseed = true;

	pr_devel("DRBG: Initializing DRBG core %d with prediction resistance "
		 "%s\n", coreref, pr ? "enabled" : "disabled");
	mutex_lock(&drbg->drbg_mutex);

	/* 9.1 step 1 is implicit with the selected DRBG type */

	/*
	 * 9.1 step 2 is implicit as caller can select prediction resistance
	 * and the flag is copied into drbg->flags --
	 * all DRBG types support prediction resistance
	 */

	/* 9.1 step 4 is implicit in  drbg_sec_strength */

	if (!drbg->core) {
		drbg->core = &drbg_cores[coreref];
		drbg->pr = pr;
		drbg->seeded = false;
		drbg->reseed_threshold = drbg_max_requests(drbg);

		ret = drbg_alloc_state(drbg);
		if (ret)
			goto unlock;

		ret = drbg_prepare_hrng(drbg);
		if (ret)
			goto free_everything;

		if (IS_ERR(drbg->jent)) {
			ret = PTR_ERR(drbg->jent);
			drbg->jent = NULL;
			if (fips_enabled || ret != -ENOENT)
				goto free_everything;
			pr_info("DRBG: Continuing without Jitter RNG\n");
		}

		reseed = false;
	}

	ret = drbg_seed(drbg, pers, reseed);

	if (ret && !reseed)
		goto free_everything;

	mutex_unlock(&drbg->drbg_mutex);
	return ret;

unlock:
	mutex_unlock(&drbg->drbg_mutex);
	return ret;

free_everything:
	mutex_unlock(&drbg->drbg_mutex);
	drbg_uninstantiate(drbg);
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
	if (drbg->random_ready.func) {
		del_random_ready_callback(&drbg->random_ready);
		cancel_work_sync(&drbg->seed_work);
		crypto_free_rng(drbg->jent);
		drbg->jent = NULL;
	}

	if (drbg->d_ops)
		drbg->d_ops->crypto_fini(drbg);
	drbg_dealloc_state(drbg);
	/* no scrubbing of test_data -- this shall survive an uninstantiate */
	return 0;
}

/*
 * Helper function for setting the test data in the DRBG
 *
 * @drbg DRBG state handle
 * @data test data
 * @len test data length
 */
static void drbg_kcapi_set_entropy(struct crypto_rng *tfm,
				   const u8 *data, unsigned int len)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);

	mutex_lock(&drbg->drbg_mutex);
	drbg_string_fill(&drbg->test_data, data, len);
	mutex_unlock(&drbg->drbg_mutex);
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
		pr_info("DRBG: could not allocate digest TFM handle: %s\n",
				drbg->core->backend_cra_name);
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

	return crypto_shash_alignmask(tfm);
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

static void drbg_kcapi_hmacsetkey(struct drbg_state *drbg,
				  const unsigned char *key)
{
	struct sdesc *sdesc = (struct sdesc *)drbg->priv_data;

	crypto_shash_setkey(sdesc->shash.tfm, key, drbg_statelen(drbg));
}

static int drbg_kcapi_hash(struct drbg_state *drbg, unsigned char *outval,
			   const struct list_head *in)
{
	struct sdesc *sdesc = (struct sdesc *)drbg->priv_data;
	struct drbg_string *input = NULL;

	crypto_shash_init(&sdesc->shash);
	list_for_each_entry(input, in, list)
		crypto_shash_update(&sdesc->shash, input->buf, input->len);
	return crypto_shash_final(&sdesc->shash, outval);
}
#endif /* (CONFIG_CRYPTO_DRBG_HASH || CONFIG_CRYPTO_DRBG_HMAC) */

#ifdef CONFIG_CRYPTO_DRBG_CTR
static int drbg_fini_sym_kernel(struct drbg_state *drbg)
{
	struct crypto_cipher *tfm =
		(struct crypto_cipher *)drbg->priv_data;
	if (tfm)
		crypto_free_cipher(tfm);
	drbg->priv_data = NULL;

	if (drbg->ctr_handle)
		crypto_free_skcipher(drbg->ctr_handle);
	drbg->ctr_handle = NULL;

	if (drbg->ctr_req)
		skcipher_request_free(drbg->ctr_req);
	drbg->ctr_req = NULL;

	kfree(drbg->ctr_null_value_buf);
	drbg->ctr_null_value = NULL;

	kfree(drbg->outscratchpadbuf);
	drbg->outscratchpadbuf = NULL;

	return 0;
}

static void drbg_skcipher_cb(struct crypto_async_request *req, int error)
{
	struct drbg_state *drbg = req->data;

	if (error == -EINPROGRESS)
		return;
	drbg->ctr_async_err = error;
	complete(&drbg->ctr_completion);
}

static int drbg_init_sym_kernel(struct drbg_state *drbg)
{
	struct crypto_cipher *tfm;
	struct crypto_skcipher *sk_tfm;
	struct skcipher_request *req;
	unsigned int alignmask;
	char ctr_name[CRYPTO_MAX_ALG_NAME];

	tfm = crypto_alloc_cipher(drbg->core->backend_cra_name, 0, 0);
	if (IS_ERR(tfm)) {
		pr_info("DRBG: could not allocate cipher TFM handle: %s\n",
				drbg->core->backend_cra_name);
		return PTR_ERR(tfm);
	}
	BUG_ON(drbg_blocklen(drbg) != crypto_cipher_blocksize(tfm));
	drbg->priv_data = tfm;

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)",
	    drbg->core->backend_cra_name) >= CRYPTO_MAX_ALG_NAME) {
		drbg_fini_sym_kernel(drbg);
		return -EINVAL;
	}
	sk_tfm = crypto_alloc_skcipher(ctr_name, 0, 0);
	if (IS_ERR(sk_tfm)) {
		pr_info("DRBG: could not allocate CTR cipher TFM handle: %s\n",
				ctr_name);
		drbg_fini_sym_kernel(drbg);
		return PTR_ERR(sk_tfm);
	}
	drbg->ctr_handle = sk_tfm;
	init_completion(&drbg->ctr_completion);

	req = skcipher_request_alloc(sk_tfm, GFP_KERNEL);
	if (!req) {
		pr_info("DRBG: could not allocate request queue\n");
		drbg_fini_sym_kernel(drbg);
		return -ENOMEM;
	}
	drbg->ctr_req = req;
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					drbg_skcipher_cb, drbg);

	alignmask = crypto_skcipher_alignmask(sk_tfm);
	drbg->ctr_null_value_buf = kzalloc(DRBG_CTR_NULL_LEN + alignmask,
					   GFP_KERNEL);
	if (!drbg->ctr_null_value_buf) {
		drbg_fini_sym_kernel(drbg);
		return -ENOMEM;
	}
	drbg->ctr_null_value = (u8 *)PTR_ALIGN(drbg->ctr_null_value_buf,
					       alignmask + 1);

	drbg->outscratchpadbuf = kmalloc(DRBG_OUTSCRATCHLEN + alignmask,
					 GFP_KERNEL);
	if (!drbg->outscratchpadbuf) {
		drbg_fini_sym_kernel(drbg);
		return -ENOMEM;
	}
	drbg->outscratchpad = (u8 *)PTR_ALIGN(drbg->outscratchpadbuf,
					      alignmask + 1);

	return alignmask;
}

static void drbg_kcapi_symsetkey(struct drbg_state *drbg,
				 const unsigned char *key)
{
	struct crypto_cipher *tfm =
		(struct crypto_cipher *)drbg->priv_data;

	crypto_cipher_setkey(tfm, key, (drbg_keylen(drbg)));
}

static int drbg_kcapi_sym(struct drbg_state *drbg, unsigned char *outval,
			  const struct drbg_string *in)
{
	struct crypto_cipher *tfm =
		(struct crypto_cipher *)drbg->priv_data;

	/* there is only component in *in */
	BUG_ON(in->len < drbg_blocklen(drbg));
	crypto_cipher_encrypt_one(tfm, outval, in->buf);
	return 0;
}

static int drbg_kcapi_sym_ctr(struct drbg_state *drbg,
			      u8 *inbuf, u32 inlen,
			      u8 *outbuf, u32 outlen)
{
	struct scatterlist sg_in;
	int ret;

	sg_init_one(&sg_in, inbuf, inlen);

	while (outlen) {
		u32 cryptlen = min3(inlen, outlen, (u32)DRBG_OUTSCRATCHLEN);
		struct scatterlist sg_out;

		/* Output buffer may not be valid for SGL, use scratchpad */
		sg_init_one(&sg_out, drbg->outscratchpad, cryptlen);
		skcipher_request_set_crypt(drbg->ctr_req, &sg_in, &sg_out,
					   cryptlen, drbg->V);
		ret = crypto_skcipher_encrypt(drbg->ctr_req);
		switch (ret) {
		case 0:
			break;
		case -EINPROGRESS:
		case -EBUSY:
			wait_for_completion(&drbg->ctr_completion);
			if (!drbg->ctr_async_err) {
				reinit_completion(&drbg->ctr_completion);
				break;
			}
		default:
			goto out;
		}
		init_completion(&drbg->ctr_completion);

		memcpy(outbuf, drbg->outscratchpad, cryptlen);

		outlen -= cryptlen;
	}
	ret = 0;

out:
	memzero_explicit(drbg->outscratchpad, DRBG_OUTSCRATCHLEN);
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

	mutex_init(&drbg->drbg_mutex);

	return 0;
}

static void drbg_kcapi_cleanup(struct crypto_tfm *tfm)
{
	drbg_uninstantiate(crypto_tfm_ctx(tfm));
}

/*
 * Generate random numbers invoked by the kernel crypto API:
 * The API of the kernel crypto API is extended as follows:
 *
 * src is additional input supplied to the RNG.
 * slen is the length of src.
 * dst is the output buffer where random data is to be stored.
 * dlen is the length of dst.
 */
static int drbg_kcapi_random(struct crypto_rng *tfm,
			     const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int dlen)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);
	struct drbg_string *addtl = NULL;
	struct drbg_string string;

	if (slen) {
		/* linked list variable is now local to allow modification */
		drbg_string_fill(&string, src, slen);
		addtl = &string;
	}

	return drbg_generate_long(drbg, dst, dlen, addtl);
}

/*
 * Seed the DRBG invoked by the kernel crypto API
 */
static int drbg_kcapi_seed(struct crypto_rng *tfm,
			   const u8 *seed, unsigned int slen)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);
	struct crypto_tfm *tfm_base = crypto_rng_tfm(tfm);
	bool pr = false;
	struct drbg_string string;
	struct drbg_string *seed_string = NULL;
	int coreref = 0;

	drbg_convert_tfm_core(crypto_tfm_alg_driver_name(tfm_base), &coreref,
			      &pr);
	if (0 < slen) {
		drbg_string_fill(&string, seed, slen);
		seed_string = &string;
	}

	return drbg_instantiate(drbg, seed_string, coreref, pr);
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

	mutex_init(&drbg->drbg_mutex);
	drbg->core = &drbg_cores[coreref];
	drbg->reseed_threshold = drbg_max_requests(drbg);

	/*
	 * if the following tests fail, it is likely that there is a buffer
	 * overflow as buf is much smaller than the requested or provided
	 * string lengths -- in case the error handling does not succeed
	 * we may get an OOPS. And we want to get an OOPS as this is a
	 * grave bug.
	 */

	max_addtllen = drbg_max_addtl(drbg);
	max_request_bytes = drbg_max_request_bytes(drbg);
	drbg_string_fill(&addtl, buf, max_addtllen + 1);
	/* overflow addtllen with additonal info string */
	len = drbg_generate(drbg, buf, OUTBUFLEN, &addtl);
	BUG_ON(0 < len);
	/* overflow max_bits */
	len = drbg_generate(drbg, buf, (max_request_bytes + 1), NULL);
	BUG_ON(0 < len);

	/* overflow max addtllen with personalization string */
	ret = drbg_seed(drbg, &addtl, false);
	BUG_ON(0 == ret);
	/* all tests passed */
	rc = 0;

	pr_devel("DRBG: Sanity tests for failure code paths successfully "
		 "completed\n");

	kfree(drbg);
	return rc;
}

static struct rng_alg drbg_algs[22];

/*
 * Fill the array drbg_algs used to register the different DRBGs
 * with the kernel crypto API. To fill the array, the information
 * from drbg_cores[] is used.
 */
static inline void __init drbg_fill_array(struct rng_alg *alg,
					  const struct drbg_core *core, int pr)
{
	int pos = 0;
	static int priority = 200;

	memcpy(alg->base.cra_name, "stdrng", 6);
	if (pr) {
		memcpy(alg->base.cra_driver_name, "drbg_pr_", 8);
		pos = 8;
	} else {
		memcpy(alg->base.cra_driver_name, "drbg_nopr_", 10);
		pos = 10;
	}
	memcpy(alg->base.cra_driver_name + pos, core->cra_name,
	       strlen(core->cra_name));

	alg->base.cra_priority = priority;
	priority++;
	/*
	 * If FIPS mode enabled, the selected DRBG shall have the
	 * highest cra_priority over other stdrng instances to ensure
	 * it is selected.
	 */
	if (fips_enabled)
		alg->base.cra_priority += 200;

	alg->base.cra_ctxsize 	= sizeof(struct drbg_state);
	alg->base.cra_module	= THIS_MODULE;
	alg->base.cra_init	= drbg_kcapi_init;
	alg->base.cra_exit	= drbg_kcapi_cleanup;
	alg->generate		= drbg_kcapi_random;
	alg->seed		= drbg_kcapi_seed;
	alg->set_ent		= drbg_kcapi_set_entropy;
	alg->seedsize		= 0;
}

static int __init drbg_init(void)
{
	unsigned int i = 0; /* pointer to drbg_algs */
	unsigned int j = 0; /* pointer to drbg_cores */
	int ret;

	ret = drbg_healthcheck_sanity();
	if (ret)
		return ret;

	if (ARRAY_SIZE(drbg_cores) * 2 > ARRAY_SIZE(drbg_algs)) {
		pr_info("DRBG: Cannot register all DRBG types"
			"(slots needed: %zu, slots available: %zu)\n",
			ARRAY_SIZE(drbg_cores) * 2, ARRAY_SIZE(drbg_algs));
		return -EFAULT;
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
	return crypto_register_rngs(drbg_algs, (ARRAY_SIZE(drbg_cores) * 2));
}

static void __exit drbg_exit(void)
{
	crypto_unregister_rngs(drbg_algs, (ARRAY_SIZE(drbg_cores) * 2));
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
MODULE_ALIAS_CRYPTO("stdrng");
