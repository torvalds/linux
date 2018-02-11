/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAAM/SEC 4.x definitions for handling key-generation jobs
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */

struct split_key_result {
	struct completion completion;
	int err;
};

void split_key_done(struct device *dev, u32 *desc, u32 err, void *context);

int gen_split_key(struct device *jrdev, u8 *key_out,
		  struct alginfo * const adata, const u8 *key_in, u32 keylen,
		  int max_keylen);
