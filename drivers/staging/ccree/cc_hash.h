/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

/* \file cc_hash.h
 * ARM CryptoCell Hash Crypto API
 */

#ifndef __CC_HASH_H__
#define __CC_HASH_H__

#include "cc_buffer_mgr.h"

#define HMAC_IPAD_CONST	0x36363636
#define HMAC_OPAD_CONST	0x5C5C5C5C
#if (CC_DEV_SHA_MAX > 256)
#define HASH_LEN_SIZE 16
#define CC_MAX_HASH_DIGEST_SIZE	SHA512_DIGEST_SIZE
#define CC_MAX_HASH_BLCK_SIZE SHA512_BLOCK_SIZE
#else
#define HASH_LEN_SIZE 8
#define CC_MAX_HASH_DIGEST_SIZE	SHA256_DIGEST_SIZE
#define CC_MAX_HASH_BLCK_SIZE SHA256_BLOCK_SIZE
#endif

#define XCBC_MAC_K1_OFFSET 0
#define XCBC_MAC_K2_OFFSET 16
#define XCBC_MAC_K3_OFFSET 32

#define CC_EXPORT_MAGIC 0xC2EE1070U

/* this struct was taken from drivers/crypto/nx/nx-aes-xcbc.c and it is used
 * for xcbc/cmac statesize
 */
struct aeshash_state {
	u8 state[AES_BLOCK_SIZE];
	unsigned int count;
	u8 buffer[AES_BLOCK_SIZE];
};

/* ahash state */
struct ahash_req_ctx {
	u8 buffers[2][CC_MAX_HASH_BLCK_SIZE] ____cacheline_aligned;
	u8 digest_result_buff[CC_MAX_HASH_DIGEST_SIZE] ____cacheline_aligned;
	u8 digest_buff[CC_MAX_HASH_DIGEST_SIZE] ____cacheline_aligned;
	u8 opad_digest_buff[CC_MAX_HASH_DIGEST_SIZE] ____cacheline_aligned;
	u8 digest_bytes_len[HASH_LEN_SIZE] ____cacheline_aligned;
	struct async_gen_req_ctx gen_ctx ____cacheline_aligned;
	enum cc_req_dma_buf_type data_dma_buf_type;
	dma_addr_t opad_digest_dma_addr;
	dma_addr_t digest_buff_dma_addr;
	dma_addr_t digest_bytes_len_dma_addr;
	dma_addr_t digest_result_dma_addr;
	u32 buf_cnt[2];
	u32 buff_index;
	u32 xcbc_count; /* count xcbc update operatations */
	struct scatterlist buff_sg[2];
	struct scatterlist *curr_sg;
	u32 in_nents;
	u32 mlli_nents;
	struct mlli_params mlli_params;
};

static inline u32 *cc_hash_buf_cnt(struct ahash_req_ctx *state)
{
	return &state->buf_cnt[state->buff_index];
}

static inline u8 *cc_hash_buf(struct ahash_req_ctx *state)
{
	return state->buffers[state->buff_index];
}

static inline u32 *cc_next_buf_cnt(struct ahash_req_ctx *state)
{
	return &state->buf_cnt[state->buff_index ^ 1];
}

static inline u8 *cc_next_buf(struct ahash_req_ctx *state)
{
	return state->buffers[state->buff_index ^ 1];
}

int cc_hash_alloc(struct cc_drvdata *drvdata);
int cc_init_hash_sram(struct cc_drvdata *drvdata);
int cc_hash_free(struct cc_drvdata *drvdata);

/*!
 * Gets the initial digest length
 *
 * \param drvdata
 * \param mode The Hash mode. Supported modes:
 *             MD5/SHA1/SHA224/SHA256/SHA384/SHA512
 *
 * \return u32 returns the address of the initial digest length in SRAM
 */
cc_sram_addr_t
cc_digest_len_addr(void *drvdata, u32 mode);

/*!
 * Gets the address of the initial digest in SRAM
 * according to the given hash mode
 *
 * \param drvdata
 * \param mode The Hash mode. Supported modes:
 *             MD5/SHA1/SHA224/SHA256/SHA384/SHA512
 *
 * \return u32 The address of the initial digest in SRAM
 */
cc_sram_addr_t cc_larval_digest_addr(void *drvdata, u32 mode);

void cc_hash_global_init(void);

#endif /*__CC_HASH_H__*/

