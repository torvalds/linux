/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Cryptographic Coprocessor (CCP) crypto API support
 *
 * Copyright (C) 2013,2017 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#ifndef __CCP_CRYPTO_H__
#define __CCP_CRYPTO_H__

#include <linux/list.h>
#include <linux/wait.h>
#include <linux/ccp.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/internal/aead.h>
#include <crypto/aead.h>
#include <crypto/ctr.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/akcipher.h>
#include <crypto/skcipher.h>
#include <crypto/internal/rsa.h>

/* We want the module name in front of our messages */
#undef pr_fmt
#define	pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#define	CCP_LOG_LEVEL	KERN_INFO

#define CCP_CRA_PRIORITY	300

struct ccp_crypto_skcipher_alg {
	struct list_head entry;

	u32 mode;

	struct skcipher_alg alg;
};

struct ccp_crypto_aead {
	struct list_head entry;

	u32 mode;

	struct aead_alg alg;
};

struct ccp_crypto_ahash_alg {
	struct list_head entry;

	const __be32 *init;
	u32 type;
	u32 mode;

	/* Child algorithm used for HMAC, CMAC, etc */
	char child_alg[CRYPTO_MAX_ALG_NAME];

	struct ahash_alg alg;
};

struct ccp_crypto_akcipher_alg {
	struct list_head entry;

	struct akcipher_alg alg;
};

static inline struct ccp_crypto_skcipher_alg *
	ccp_crypto_skcipher_alg(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);

	return container_of(alg, struct ccp_crypto_skcipher_alg, alg);
}

static inline struct ccp_crypto_ahash_alg *
	ccp_crypto_ahash_alg(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct ahash_alg *ahash_alg;

	ahash_alg = container_of(alg, struct ahash_alg, halg.base);

	return container_of(ahash_alg, struct ccp_crypto_ahash_alg, alg);
}

/***** AES related defines *****/
struct ccp_aes_ctx {
	/* Fallback cipher for XTS with unsupported unit sizes */
	struct crypto_sync_skcipher *tfm_skcipher;

	enum ccp_engine engine;
	enum ccp_aes_type type;
	enum ccp_aes_mode mode;

	struct scatterlist key_sg;
	unsigned int key_len;
	u8 key[AES_MAX_KEY_SIZE * 2];

	u8 nonce[CTR_RFC3686_NONCE_SIZE];

	/* CMAC key structures */
	struct scatterlist k1_sg;
	struct scatterlist k2_sg;
	unsigned int kn_len;
	u8 k1[AES_BLOCK_SIZE];
	u8 k2[AES_BLOCK_SIZE];
};

struct ccp_aes_req_ctx {
	struct scatterlist iv_sg;
	u8 iv[AES_BLOCK_SIZE];

	struct scatterlist tag_sg;
	u8 tag[AES_BLOCK_SIZE];

	/* Fields used for RFC3686 requests */
	u8 *rfc3686_info;
	u8 rfc3686_iv[AES_BLOCK_SIZE];

	struct ccp_cmd cmd;
};

struct ccp_aes_cmac_req_ctx {
	unsigned int null_msg;
	unsigned int final;

	struct scatterlist *src;
	unsigned int nbytes;

	u64 hash_cnt;
	unsigned int hash_rem;

	struct sg_table data_sg;

	struct scatterlist iv_sg;
	u8 iv[AES_BLOCK_SIZE];

	struct scatterlist buf_sg;
	unsigned int buf_count;
	u8 buf[AES_BLOCK_SIZE];

	struct scatterlist pad_sg;
	unsigned int pad_count;
	u8 pad[AES_BLOCK_SIZE];

	struct ccp_cmd cmd;
};

struct ccp_aes_cmac_exp_ctx {
	unsigned int null_msg;

	u8 iv[AES_BLOCK_SIZE];

	unsigned int buf_count;
	u8 buf[AES_BLOCK_SIZE];
};

/***** 3DES related defines *****/
struct ccp_des3_ctx {
	enum ccp_engine engine;
	enum ccp_des3_type type;
	enum ccp_des3_mode mode;

	struct scatterlist key_sg;
	unsigned int key_len;
	u8 key[AES_MAX_KEY_SIZE];
};

struct ccp_des3_req_ctx {
	struct scatterlist iv_sg;
	u8 iv[AES_BLOCK_SIZE];

	struct ccp_cmd cmd;
};

/* SHA-related defines
 * These values must be large enough to accommodate any variant
 */
#define MAX_SHA_CONTEXT_SIZE	SHA512_DIGEST_SIZE
#define MAX_SHA_BLOCK_SIZE	SHA512_BLOCK_SIZE

struct ccp_sha_ctx {
	struct scatterlist opad_sg;
	unsigned int opad_count;

	unsigned int key_len;
	u8 key[MAX_SHA_BLOCK_SIZE];
	u8 ipad[MAX_SHA_BLOCK_SIZE];
	u8 opad[MAX_SHA_BLOCK_SIZE];
	struct crypto_shash *hmac_tfm;
};

struct ccp_sha_req_ctx {
	enum ccp_sha_type type;

	u64 msg_bits;

	unsigned int first;
	unsigned int final;

	struct scatterlist *src;
	unsigned int nbytes;

	u64 hash_cnt;
	unsigned int hash_rem;

	struct sg_table data_sg;

	struct scatterlist ctx_sg;
	u8 ctx[MAX_SHA_CONTEXT_SIZE];

	struct scatterlist buf_sg;
	unsigned int buf_count;
	u8 buf[MAX_SHA_BLOCK_SIZE];

	/* CCP driver command */
	struct ccp_cmd cmd;
};

struct ccp_sha_exp_ctx {
	enum ccp_sha_type type;

	u64 msg_bits;

	unsigned int first;

	u8 ctx[MAX_SHA_CONTEXT_SIZE];

	unsigned int buf_count;
	u8 buf[MAX_SHA_BLOCK_SIZE];
};

/***** RSA related defines *****/

struct ccp_rsa_ctx {
	unsigned int key_len; /* in bits */
	struct scatterlist e_sg;
	u8 *e_buf;
	unsigned int e_len;
	struct scatterlist n_sg;
	u8 *n_buf;
	unsigned int n_len;
	struct scatterlist d_sg;
	u8 *d_buf;
	unsigned int d_len;
};

struct ccp_rsa_req_ctx {
	struct ccp_cmd cmd;
};

#define	CCP_RSA_MAXMOD	(4 * 1024 / 8)
#define	CCP5_RSA_MAXMOD	(16 * 1024 / 8)

/***** Common Context Structure *****/
struct ccp_ctx {
	int (*complete)(struct crypto_async_request *req, int ret);

	union {
		struct ccp_aes_ctx aes;
		struct ccp_rsa_ctx rsa;
		struct ccp_sha_ctx sha;
		struct ccp_des3_ctx des3;
	} u;
};

int ccp_crypto_enqueue_request(struct crypto_async_request *req,
			       struct ccp_cmd *cmd);
struct scatterlist *ccp_crypto_sg_table_add(struct sg_table *table,
					    struct scatterlist *sg_add);

int ccp_register_aes_algs(struct list_head *head);
int ccp_register_aes_cmac_algs(struct list_head *head);
int ccp_register_aes_xts_algs(struct list_head *head);
int ccp_register_aes_aeads(struct list_head *head);
int ccp_register_sha_algs(struct list_head *head);
int ccp_register_des3_algs(struct list_head *head);
int ccp_register_rsa_algs(struct list_head *head);

#endif
