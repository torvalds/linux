/*
 * This file is part of the Chelsio T6 Crypto driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __CHCR_CRYPTO_H__
#define __CHCR_CRYPTO_H__

#define GHASH_BLOCK_SIZE    16
#define GHASH_DIGEST_SIZE   16

#define CCM_B0_SIZE             16
#define CCM_AAD_FIELD_SIZE      2
#define T6_MAX_AAD_SIZE 511


/* Define following if h/w is not dropping the AAD and IV data before
 * giving the processed data
 */

#define CHCR_CRA_PRIORITY 500
#define CHCR_AEAD_PRIORITY 6000
#define CHCR_AES_MAX_KEY_LEN  (2 * (AES_MAX_KEY_SIZE)) /* consider xts */
#define CHCR_MAX_CRYPTO_IV_LEN 16 /* AES IV len */

#define CHCR_MAX_AUTHENC_AES_KEY_LEN 32 /* max aes key length*/
#define CHCR_MAX_AUTHENC_SHA_KEY_LEN 128 /* max sha key length*/

#define CHCR_GIVENCRYPT_OP 2
/* CPL/SCMD parameters */

#define CHCR_ENCRYPT_OP 0
#define CHCR_DECRYPT_OP 1

#define CHCR_SCMD_SEQ_NO_CTRL_32BIT     1
#define CHCR_SCMD_SEQ_NO_CTRL_48BIT     2
#define CHCR_SCMD_SEQ_NO_CTRL_64BIT     3

#define CHCR_SCMD_PROTO_VERSION_GENERIC 4

#define CHCR_SCMD_AUTH_CTRL_AUTH_CIPHER 0
#define CHCR_SCMD_AUTH_CTRL_CIPHER_AUTH 1

#define CHCR_SCMD_CIPHER_MODE_NOP               0
#define CHCR_SCMD_CIPHER_MODE_AES_CBC           1
#define CHCR_SCMD_CIPHER_MODE_AES_GCM           2
#define CHCR_SCMD_CIPHER_MODE_AES_CTR           3
#define CHCR_SCMD_CIPHER_MODE_GENERIC_AES       4
#define CHCR_SCMD_CIPHER_MODE_AES_XTS           6
#define CHCR_SCMD_CIPHER_MODE_AES_CCM           7

#define CHCR_SCMD_AUTH_MODE_NOP             0
#define CHCR_SCMD_AUTH_MODE_SHA1            1
#define CHCR_SCMD_AUTH_MODE_SHA224          2
#define CHCR_SCMD_AUTH_MODE_SHA256          3
#define CHCR_SCMD_AUTH_MODE_GHASH           4
#define CHCR_SCMD_AUTH_MODE_SHA512_224      5
#define CHCR_SCMD_AUTH_MODE_SHA512_256      6
#define CHCR_SCMD_AUTH_MODE_SHA512_384      7
#define CHCR_SCMD_AUTH_MODE_SHA512_512      8
#define CHCR_SCMD_AUTH_MODE_CBCMAC          9
#define CHCR_SCMD_AUTH_MODE_CMAC            10

#define CHCR_SCMD_HMAC_CTRL_NOP             0
#define CHCR_SCMD_HMAC_CTRL_NO_TRUNC        1
#define CHCR_SCMD_HMAC_CTRL_TRUNC_RFC4366   2
#define CHCR_SCMD_HMAC_CTRL_IPSEC_96BIT     3
#define CHCR_SCMD_HMAC_CTRL_PL1		    4
#define CHCR_SCMD_HMAC_CTRL_PL2		    5
#define CHCR_SCMD_HMAC_CTRL_PL3		    6
#define CHCR_SCMD_HMAC_CTRL_DIV2	    7
#define VERIFY_HW 0
#define VERIFY_SW 1

#define CHCR_SCMD_IVGEN_CTRL_HW             0
#define CHCR_SCMD_IVGEN_CTRL_SW             1
/* This are not really mac key size. They are intermediate values
 * of sha engine and its size
 */
#define CHCR_KEYCTX_MAC_KEY_SIZE_128        0
#define CHCR_KEYCTX_MAC_KEY_SIZE_160        1
#define CHCR_KEYCTX_MAC_KEY_SIZE_192        2
#define CHCR_KEYCTX_MAC_KEY_SIZE_256        3
#define CHCR_KEYCTX_MAC_KEY_SIZE_512        4
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_128     0
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_192     1
#define CHCR_KEYCTX_CIPHER_KEY_SIZE_256     2
#define CHCR_KEYCTX_NO_KEY                  15

#define CHCR_CPL_FW4_PLD_IV_OFFSET          (5 * 64) /* bytes. flt #5 and #6 */
#define CHCR_CPL_FW4_PLD_HASH_RESULT_OFFSET (7 * 64) /* bytes. flt #7 */
#define CHCR_CPL_FW4_PLD_DATA_SIZE          (4 * 64) /* bytes. flt #4 to #7 */

#define KEY_CONTEXT_HDR_SALT_AND_PAD	    16
#define flits_to_bytes(x)  (x * 8)

#define IV_NOP                  0
#define IV_IMMEDIATE            1
#define IV_DSGL			2

#define AEAD_H_SIZE             16

#define CRYPTO_ALG_SUB_TYPE_MASK            0x0f000000
#define CRYPTO_ALG_SUB_TYPE_HASH_HMAC       0x01000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_RFC4106    0x02000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_GCM	    0x03000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_AUTHENC    0x04000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_CCM        0x05000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_RFC4309    0x06000000
#define CRYPTO_ALG_SUB_TYPE_AEAD_NULL       0x07000000
#define CRYPTO_ALG_SUB_TYPE_CTR             0x08000000
#define CRYPTO_ALG_SUB_TYPE_CTR_RFC3686     0x09000000
#define CRYPTO_ALG_SUB_TYPE_XTS		    0x0a000000
#define CRYPTO_ALG_SUB_TYPE_CBC		    0x0b000000
#define CRYPTO_ALG_TYPE_HMAC (CRYPTO_ALG_TYPE_AHASH |\
			      CRYPTO_ALG_SUB_TYPE_HASH_HMAC)

#define MAX_SCRATCH_PAD_SIZE    32

#define CHCR_HASH_MAX_BLOCK_SIZE_64  64
#define CHCR_HASH_MAX_BLOCK_SIZE_128 128
#define CHCR_SG_SIZE 2048

/* Aligned to 128 bit boundary */

struct ablk_ctx {
	struct crypto_skcipher *sw_cipher;
	struct crypto_cipher *aes_generic;
	__be32 key_ctx_hdr;
	unsigned int enckey_len;
	unsigned char ciph_mode;
	u8 key[CHCR_AES_MAX_KEY_LEN];
	u8 nonce[4];
	u8 rrkey[AES_MAX_KEY_SIZE];
};
struct chcr_aead_reqctx {
	struct	sk_buff	*skb;
	struct scatterlist *dst;
	struct scatterlist *newdstsg;
	struct scatterlist srcffwd[2];
	struct scatterlist dstffwd[2];
	short int dst_nents;
	u16 verify;
	u8 iv[CHCR_MAX_CRYPTO_IV_LEN];
	unsigned char scratch_pad[MAX_SCRATCH_PAD_SIZE];
};

struct chcr_gcm_ctx {
	u8 ghash_h[AEAD_H_SIZE];
};

struct chcr_authenc_ctx {
	u8 dec_rrkey[AES_MAX_KEY_SIZE];
	u8 h_iopad[2 * CHCR_HASH_MAX_DIGEST_SIZE];
	unsigned char auth_mode;
};

struct __aead_ctx {
	struct chcr_gcm_ctx gcm[0];
	struct chcr_authenc_ctx authenc[0];
};



struct chcr_aead_ctx {
	__be32 key_ctx_hdr;
	unsigned int enckey_len;
	struct crypto_skcipher *null;
	struct crypto_aead *sw_cipher;
	u8 salt[MAX_SALT];
	u8 key[CHCR_AES_MAX_KEY_LEN];
	u16 hmac_ctrl;
	u16 mayverify;
	struct	__aead_ctx ctx[0];
};



struct hmac_ctx {
	struct crypto_shash *base_hash;
	u8 ipad[CHCR_HASH_MAX_BLOCK_SIZE_128];
	u8 opad[CHCR_HASH_MAX_BLOCK_SIZE_128];
};

struct __crypto_ctx {
	struct hmac_ctx hmacctx[0];
	struct ablk_ctx ablkctx[0];
	struct chcr_aead_ctx aeadctx[0];
};

struct chcr_context {
	struct chcr_dev *dev;
	unsigned char tx_qidx;
	unsigned char rx_qidx;
	unsigned char tx_chan_id;
	unsigned char pci_chan_id;
	struct __crypto_ctx crypto_ctx[0];
};

struct chcr_ahash_req_ctx {
	u32 result;
	u8 bfr1[CHCR_HASH_MAX_BLOCK_SIZE_128];
	u8 bfr2[CHCR_HASH_MAX_BLOCK_SIZE_128];
	u8 *reqbfr;
	u8 *skbfr;
	u8 reqlen;
	/* DMA the partial hash in it */
	u8 partial_hash[CHCR_HASH_MAX_DIGEST_SIZE];
	u64 data_len;  /* Data len till time */
	/* SKB which is being sent to the hardware for processing */
	struct sk_buff *skb;
};

struct chcr_blkcipher_req_ctx {
	struct sk_buff *skb;
	struct scatterlist srcffwd[2];
	struct scatterlist dstffwd[2];
	struct scatterlist *dstsg;
	struct scatterlist *dst;
	struct scatterlist *newdstsg;
	unsigned int processed;
	unsigned int op;
	short int dst_nents;
	u8 iv[CHCR_MAX_CRYPTO_IV_LEN];
};

struct chcr_alg_template {
	u32 type;
	u32 is_registered;
	union {
		struct crypto_alg crypto;
		struct ahash_alg hash;
		struct aead_alg aead;
	} alg;
};

struct chcr_req_ctx {
	union {
		struct ahash_request *ahash_req;
		struct aead_request *aead_req;
		struct ablkcipher_request *ablk_req;
	} req;
	union {
		struct chcr_ahash_req_ctx *ahash_ctx;
		struct chcr_aead_reqctx *reqctx;
		struct chcr_blkcipher_req_ctx *ablk_ctx;
	} ctx;
};

struct sge_opaque_hdr {
	void *dev;
	dma_addr_t addr[MAX_SKB_FRAGS + 1];
};

typedef struct sk_buff *(*create_wr_t)(struct aead_request *req,
				       unsigned short qid,
				       int size,
				       unsigned short op_type);

static int chcr_aead_op(struct aead_request *req_base,
			  unsigned short op_type,
			  int size,
			  create_wr_t create_wr_fn);
static inline int get_aead_subtype(struct crypto_aead *aead);
static int is_newsg(struct scatterlist *sgl, unsigned int *newents);
static struct scatterlist *alloc_new_sg(struct scatterlist *sgl,
					unsigned int nents);
static inline void free_new_sg(struct scatterlist *sgl);
static int chcr_handle_cipher_resp(struct ablkcipher_request *req,
				   unsigned char *input, int err);
#endif /* __CHCR_CRYPTO_H__ */
