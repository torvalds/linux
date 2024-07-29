/* SPDX-License-Identifier: GPL-2.0 */

#ifndef SPACC_DEVICE_H_
#define SPACC_DEVICE_H_

#include <crypto/hash.h>
#include <crypto/ctr.h>
#include <crypto/internal/aead.h>
#include <linux/of.h>
#include "spacc_core.h"

#define MODE_TAB_AEAD(_name, _ciph, _hash, _hashlen, _ivlen, _blocklen) \
	.name = _name, .aead = { .ciph = _ciph, .hash = _hash }, \
	.hashlen = _hashlen, .ivlen = _ivlen, .blocklen = _blocklen

/* Helper macros for initializing the hash/cipher tables. */
#define MODE_TAB_COMMON(_name, _id_name, _blocklen) \
	.name = _name, .id = CRYPTO_MODE_##_id_name, .blocklen = _blocklen

#define MODE_TAB_HASH(_name, _id_name, _hashlen, _blocklen) \
	MODE_TAB_COMMON(_name, _id_name, _blocklen), \
	.hashlen = _hashlen, .testlen = _hashlen

#define MODE_TAB_CIPH(_name, _id_name, _ivlen, _blocklen) \
	MODE_TAB_COMMON(_name, _id_name, _blocklen), \
	.ivlen = _ivlen

#define MODE_TAB_HASH_XCBC	0x8000

#define SPACC_MAX_DIGEST_SIZE	64
#define SPACC_MAX_KEY_SIZE	32
#define SPACC_MAX_IV_SIZE	16

#define SPACC_DMA_ALIGN		4
#define SPACC_DMA_BOUNDARY	0x10000

#define MAX_DEVICES		2
/* flag means the IV is computed from setkey and crypt*/
#define SPACC_MANGLE_IV_FLAG	0x8000

/* we're doing a CTR mangle (for RFC3686/IPsec)*/
#define SPACC_MANGLE_IV_RFC3686	0x0100

/* we're doing GCM */
#define SPACC_MANGLE_IV_RFC4106	0x0200

/* we're doing GMAC */
#define SPACC_MANGLE_IV_RFC4543	0x0300

/* we're doing CCM */
#define SPACC_MANGLE_IV_RFC4309	0x0400

/* we're doing SM4 GCM/CCM */
#define SPACC_MANGLE_IV_RFC8998	0x0500

#define CRYPTO_MODE_AES_CTR_RFC3686 (CRYPTO_MODE_AES_CTR \
		| SPACC_MANGLE_IV_FLAG \
		| SPACC_MANGLE_IV_RFC3686)
#define CRYPTO_MODE_AES_GCM_RFC4106 (CRYPTO_MODE_AES_GCM \
		| SPACC_MANGLE_IV_FLAG \
		| SPACC_MANGLE_IV_RFC4106)
#define CRYPTO_MODE_AES_GCM_RFC4543 (CRYPTO_MODE_AES_GCM \
		| SPACC_MANGLE_IV_FLAG \
		| SPACC_MANGLE_IV_RFC4543)
#define CRYPTO_MODE_AES_CCM_RFC4309 (CRYPTO_MODE_AES_CCM \
		| SPACC_MANGLE_IV_FLAG \
		| SPACC_MANGLE_IV_RFC4309)
#define CRYPTO_MODE_SM4_GCM_RFC8998 (CRYPTO_MODE_SM4_GCM)
#define CRYPTO_MODE_SM4_CCM_RFC8998 (CRYPTO_MODE_SM4_CCM)

struct spacc_crypto_ctx {
	struct device *dev;

	spinlock_t lock;
	struct list_head jobs;
	int handle, mode, auth_size, key_len;
	unsigned char *cipher_key;

	/*
	 * Indicates that the H/W context has been setup and can be used for
	 * crypto; otherwise, the software fallback will be used.
	 */
	bool ctx_valid;
	unsigned int flag_ppp;

	/* salt used for rfc3686/givencrypt mode */
	unsigned char csalt[16];
	u8 ipad[128] __aligned(sizeof(u32));
	u8 digest_ctx_buf[128] __aligned(sizeof(u32));
	u8 tmp_buffer[128] __aligned(sizeof(u32));

	/* Save keylen from setkey */
	int keylen;
	u8  key[256];
	int zero_key;
	unsigned char *tmp_sgl_buff;
	struct scatterlist *tmp_sgl;

	union{
		struct crypto_ahash      *hash;
		struct crypto_aead       *aead;
		struct crypto_skcipher   *cipher;
	} fb;
};

struct spacc_crypto_reqctx {
	struct pdu_ddt src, dst;
	void *digest_buf, *iv_buf;
	dma_addr_t digest_dma;
	int dst_nents, src_nents, aead_nents, total_nents;
	int encrypt_op, mode, single_shot;
	unsigned int spacc_cipher_cryptlen, rem_nents;

	struct aead_cb_data {
		int new_handle;
		struct spacc_crypto_ctx    *tctx;
		struct spacc_crypto_reqctx *ctx;
		struct aead_request        *req;
		struct spacc_device        *spacc;
	} cb;

	struct ahash_cb_data {
		int new_handle;
		struct spacc_crypto_ctx    *tctx;
		struct spacc_crypto_reqctx *ctx;
		struct ahash_request       *req;
		struct spacc_device        *spacc;
	} acb;

	struct cipher_cb_data {
		int new_handle;
		struct spacc_crypto_ctx    *tctx;
		struct spacc_crypto_reqctx *ctx;
		struct skcipher_request    *req;
		struct spacc_device        *spacc;
	} ccb;

	union {
		struct ahash_request hash_req;
		struct skcipher_request cipher_req;
		struct aead_request aead_req;
	} fb;
};

struct mode_tab {
	char name[128];

	int valid;

	/* mode ID used in hash/cipher mode but not aead*/
	int id;

	/* ciph/hash mode used in aead */
	struct {
		int ciph, hash;
	} aead;

	unsigned int hashlen, ivlen, blocklen, keylen[3];
	unsigned int keylen_mask, testlen;
	unsigned int chunksize, walksize, min_keysize, max_keysize;

	bool sw_fb;

	union {
		unsigned char hash_test[SPACC_MAX_DIGEST_SIZE];
		unsigned char ciph_test[3][2 * SPACC_MAX_IV_SIZE];
	};
};

struct spacc_alg {
	struct mode_tab *mode;
	unsigned int keylen_mask;

	struct device *dev[MAX_DEVICES];

	struct list_head list;
	struct crypto_alg *calg;
	struct crypto_tfm *tfm;

	union {
		struct ahash_alg hash;
		struct aead_alg aead;
		struct skcipher_alg skcipher;
	} alg;
};

static inline const struct spacc_alg *spacc_tfm_ahash(struct crypto_tfm *tfm)
{
	const struct crypto_alg *calg = tfm->__crt_alg;

	if ((calg->cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_AHASH)
		return container_of(calg, struct spacc_alg, alg.hash.halg.base);

	return NULL;
}

static inline const struct spacc_alg *spacc_tfm_skcipher(struct crypto_tfm *tfm)
{
	const struct crypto_alg *calg = tfm->__crt_alg;

	if ((calg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
					CRYPTO_ALG_TYPE_SKCIPHER)
		return container_of(calg, struct spacc_alg, alg.skcipher.base);

	return NULL;
}

static inline const struct spacc_alg *spacc_tfm_aead(struct crypto_tfm *tfm)
{
	const struct crypto_alg *calg = tfm->__crt_alg;

	if ((calg->cra_flags & CRYPTO_ALG_TYPE_MASK) == CRYPTO_ALG_TYPE_AEAD)
		return container_of(calg, struct spacc_alg, alg.aead.base);

	return NULL;
}

int probe_hashes(struct platform_device *spacc_pdev);
int spacc_unregister_hash_algs(void);

int probe_aeads(struct platform_device *spacc_pdev);
int spacc_unregister_aead_algs(void);

int probe_ciphers(struct platform_device *spacc_pdev);
int spacc_unregister_cipher_algs(void);

int spacc_probe(struct platform_device *pdev,
		const struct of_device_id snps_spacc_id[]);

irqreturn_t spacc_irq_handler(int irq, void *dev);
#endif
