// SPDX-License-Identifier: GPL-2.0

/*
 * NIST SP800-90A DRBG derivation function
 *
 * Copyright (C) 2014, Stephan Mueller <smueller@chronox.de>
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <crypto/aes.h>
#include <crypto/df_sp80090a.h>
#include <crypto/internal/drbg.h>

static void drbg_kcapi_sym(struct aes_enckey *aeskey, unsigned char *outval,
			   const struct drbg_string *in, u8 blocklen_bytes)
{
	/* there is only component in *in */
	BUG_ON(in->len < blocklen_bytes);
	aes_encrypt(aeskey, outval, in->buf);
}

/* BCC function for CTR DRBG as defined in 10.4.3 */

static void drbg_ctr_bcc(struct aes_enckey *aeskey,
			 unsigned char *out, const unsigned char *key,
			 struct list_head *in,
			 u8 blocklen_bytes,
			 u8 keylen)
{
	struct drbg_string *curr = NULL;
	struct drbg_string data;
	short cnt = 0;

	drbg_string_fill(&data, out, blocklen_bytes);

	/* 10.4.3 step 2 / 4 */
	aes_prepareenckey(aeskey, key, keylen);
	list_for_each_entry(curr, in, list) {
		const unsigned char *pos = curr->buf;
		size_t len = curr->len;
		/* 10.4.3 step 4.1 */
		while (len) {
			/* 10.4.3 step 4.2 */
			if (blocklen_bytes == cnt) {
				cnt = 0;
				drbg_kcapi_sym(aeskey, out, &data, blocklen_bytes);
			}
			out[cnt] ^= *pos;
			pos++;
			cnt++;
			len--;
		}
	}
	/* 10.4.3 step 4.2 for last block */
	if (cnt)
		drbg_kcapi_sym(aeskey, out, &data, blocklen_bytes);
}

/*
 * scratchpad usage: drbg_ctr_update is interlinked with crypto_drbg_ctr_df
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
 * crypto_drbg_ctr_df:
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
 * refer to crypto_drbg_ctr_df_datalen() to get required length
 */

/* Derivation Function for CTR DRBG as defined in 10.4.2 */
int crypto_drbg_ctr_df(struct aes_enckey *aeskey,
		       unsigned char *df_data, size_t bytes_to_return,
		       struct list_head *seedlist,
		       u8 blocklen_bytes,
		       u8 statelen)
{
	unsigned char L_N[8];
	/* S3 is input */
	struct drbg_string S1, S2, S4, cipherin;
	LIST_HEAD(bcc_list);
	unsigned char *pad = df_data + statelen;
	unsigned char *iv = pad + blocklen_bytes;
	unsigned char *temp = iv + blocklen_bytes;
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
	u8 keylen;

	memset(pad, 0, blocklen_bytes);
	memset(iv, 0, blocklen_bytes);
	keylen = statelen - blocklen_bytes;
	/* 10.4.2 step 1 is implicit as we work byte-wise */

	/* 10.4.2 step 2 */
	if ((512 / 8) < bytes_to_return)
		return -EINVAL;

	/* 10.4.2 step 2 -- calculate the entire length of all input data */
	list_for_each_entry(seed, seedlist, list)
		inputlen += seed->len;
	drbg_cpu_to_be32(inputlen, &L_N[0]);

	/* 10.4.2 step 3 */
	drbg_cpu_to_be32(bytes_to_return, &L_N[4]);

	/* 10.4.2 step 5: length is L_N, input_string, one byte, padding */
	padlen = (inputlen + sizeof(L_N) + 1) % (blocklen_bytes);
	/* wrap the padlen appropriately */
	if (padlen)
		padlen = blocklen_bytes - padlen;
	/*
	 * pad / padlen contains the 0x80 byte and the following zero bytes.
	 * As the calculated padlen value only covers the number of zero
	 * bytes, this value has to be incremented by one for the 0x80 byte.
	 */
	padlen++;
	pad[0] = 0x80;

	/* 10.4.2 step 4 -- first fill the linked list and then order it */
	drbg_string_fill(&S1, iv, blocklen_bytes);
	list_add_tail(&S1.list, &bcc_list);
	drbg_string_fill(&S2, L_N, sizeof(L_N));
	list_add_tail(&S2.list, &bcc_list);
	list_splice_tail(seedlist, &bcc_list);
	drbg_string_fill(&S4, pad, padlen);
	list_add_tail(&S4.list, &bcc_list);

	/* 10.4.2 step 9 */
	while (templen < (keylen + (blocklen_bytes))) {
		/*
		 * 10.4.2 step 9.1 - the padding is implicit as the buffer
		 * holds zeros after allocation -- even the increment of i
		 * is irrelevant as the increment remains within length of i
		 */
		drbg_cpu_to_be32(i, iv);
		/* 10.4.2 step 9.2 -- BCC and concatenation with temp */
		drbg_ctr_bcc(aeskey, temp + templen, K, &bcc_list,
			     blocklen_bytes, keylen);
		/* 10.4.2 step 9.3 */
		i++;
		templen += blocklen_bytes;
	}

	/* 10.4.2 step 11 */
	X = temp + (keylen);
	drbg_string_fill(&cipherin, X, blocklen_bytes);

	/* 10.4.2 step 12: overwriting of outval is implemented in next step */

	/* 10.4.2 step 13 */
	aes_prepareenckey(aeskey, temp, keylen);
	while (generated_len < bytes_to_return) {
		short blocklen = 0;
		/*
		 * 10.4.2 step 13.1: the truncation of the key length is
		 * implicit as the key is only drbg_blocklen in size based on
		 * the implementation of the cipher function callback
		 */
		drbg_kcapi_sym(aeskey, X, &cipherin, blocklen_bytes);
		blocklen = (blocklen_bytes <
				(bytes_to_return - generated_len)) ?
			    blocklen_bytes :
				(bytes_to_return - generated_len);
		/* 10.4.2 step 13.2 and 14 */
		memcpy(df_data + generated_len, X, blocklen);
		generated_len += blocklen;
	}

	memset(iv, 0, blocklen_bytes);
	memset(temp, 0, statelen + blocklen_bytes);
	memset(pad, 0, blocklen_bytes);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_drbg_ctr_df);

MODULE_IMPORT_NS("CRYPTO_INTERNAL");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Derivation Function conformant to SP800-90A");
