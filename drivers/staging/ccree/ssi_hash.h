/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file ssi_hash.h
   ARM CryptoCell Hash Crypto API
 */

#ifndef __SSI_HASH_H__
#define __SSI_HASH_H__

#include "ssi_buffer_mgr.h"

#define HMAC_IPAD_CONST	0x36363636
#define HMAC_OPAD_CONST	0x5C5C5C5C
#if (DX_DEV_SHA_MAX > 256)
#define HASH_LEN_SIZE 16
#define SSI_MAX_HASH_DIGEST_SIZE	SHA512_DIGEST_SIZE
#define SSI_MAX_HASH_BLCK_SIZE SHA512_BLOCK_SIZE
#else
#define HASH_LEN_SIZE 8
#define SSI_MAX_HASH_DIGEST_SIZE	SHA256_DIGEST_SIZE
#define SSI_MAX_HASH_BLCK_SIZE SHA256_BLOCK_SIZE
#endif

#define XCBC_MAC_K1_OFFSET 0
#define XCBC_MAC_K2_OFFSET 16
#define XCBC_MAC_K3_OFFSET 32

// this struct was taken from drivers/crypto/nx/nx-aes-xcbc.c and it is used for xcbc/cmac statesize
struct aeshash_state {
	u8 state[AES_BLOCK_SIZE];
	unsigned int count;
	u8 buffer[AES_BLOCK_SIZE];
};

/* ahash state */
struct ahash_req_ctx {
	uint8_t* buff0;
	uint8_t* buff1;
	uint8_t* digest_result_buff;
	struct async_gen_req_ctx gen_ctx;
	enum ssi_req_dma_buf_type data_dma_buf_type;
	uint8_t *digest_buff;
	uint8_t *opad_digest_buff;
	uint8_t *digest_bytes_len;
	dma_addr_t opad_digest_dma_addr;
	dma_addr_t digest_buff_dma_addr;
	dma_addr_t digest_bytes_len_dma_addr;
	dma_addr_t digest_result_dma_addr;
	uint32_t buff0_cnt;
	uint32_t buff1_cnt;
	uint32_t buff_index;
	uint32_t xcbc_count; /* count xcbc update operatations */
	struct scatterlist buff_sg[2];
	struct scatterlist *curr_sg;
	uint32_t in_nents;
	uint32_t mlli_nents;
	struct mlli_params mlli_params;	
};

int ssi_hash_alloc(struct ssi_drvdata *drvdata);
int ssi_hash_init_sram_digest_consts(struct ssi_drvdata *drvdata);
int ssi_hash_free(struct ssi_drvdata *drvdata);

/*!
 * Gets the initial digest length
 * 
 * \param drvdata 
 * \param mode The Hash mode. Supported modes: MD5/SHA1/SHA224/SHA256/SHA384/SHA512
 * 
 * \return uint32_t returns the address of the initial digest length in SRAM
 */
ssi_sram_addr_t
ssi_ahash_get_initial_digest_len_sram_addr(void *drvdata, uint32_t mode);

/*!
 * Gets the address of the initial digest in SRAM 
 * according to the given hash mode
 * 
 * \param drvdata 
 * \param mode The Hash mode. Supported modes: MD5/SHA1/SHA224/SHA256/SHA384/SHA512
 * 
 * \return uint32_t The address of the inital digest in SRAM
 */
ssi_sram_addr_t ssi_ahash_get_larval_digest_sram_addr(void *drvdata, uint32_t mode);

#endif /*__SSI_HASH_H__*/

