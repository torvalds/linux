/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Shared descriptors for ahash algorithms
 *
 * Copyright 2017 NXP
 */

#ifndef _CAAMHASH_DESC_H_
#define _CAAMHASH_DESC_H_

/* length of descriptors text */
#define DESC_AHASH_BASE			(3 * CAAM_CMD_SZ)
#define DESC_AHASH_UPDATE_LEN		(6 * CAAM_CMD_SZ)
#define DESC_AHASH_UPDATE_FIRST_LEN	(DESC_AHASH_BASE + 4 * CAAM_CMD_SZ)
#define DESC_AHASH_FINAL_LEN		(DESC_AHASH_BASE + 5 * CAAM_CMD_SZ)
#define DESC_AHASH_DIGEST_LEN		(DESC_AHASH_BASE + 4 * CAAM_CMD_SZ)

void cnstr_shdsc_ahash(u32 * const desc, struct alginfo *adata, u32 state,
		       int digestsize, int ctx_len, bool import_ctx, int era);

#endif /* _CAAMHASH_DESC_H_ */
