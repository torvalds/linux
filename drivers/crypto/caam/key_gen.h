/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM/SEC 4.x definitions for handling key-generation jobs
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */

/**
 * split_key_len - Compute MDHA split key length for a given algorithm
 * @hash: Hashing algorithm selection, one of OP_ALG_ALGSEL_* - MD5, SHA1,
 *        SHA224, SHA384, SHA512.
 *
 * Return: MDHA split key length
 */
static inline u32 split_key_len(u32 hash)
{
	/* Sizes for MDHA pads (*not* keys): MD5, SHA1, 224, 256, 384, 512 */
	static const u8 mdpadlen[] = { 16, 20, 32, 32, 64, 64 };
	u32 idx;

	idx = (hash & OP_ALG_ALGSEL_SUBMASK) >> OP_ALG_ALGSEL_SHIFT;

	return (u32)(mdpadlen[idx] * 2);
}

/**
 * split_key_pad_len - Compute MDHA split key pad length for a given algorithm
 * @hash: Hashing algorithm selection, one of OP_ALG_ALGSEL_* - MD5, SHA1,
 *        SHA224, SHA384, SHA512.
 *
 * Return: MDHA split key pad length
 */
static inline u32 split_key_pad_len(u32 hash)
{
	return ALIGN(split_key_len(hash), 16);
}

struct split_key_result {
	struct completion completion;
	int err;
};

void split_key_done(struct device *dev, u32 *desc, u32 err, void *context);

int gen_split_key(struct device *jrdev, u8 *key_out,
		  struct alginfo * const adata, const u8 *key_in, u32 keylen,
		  int max_keylen);
