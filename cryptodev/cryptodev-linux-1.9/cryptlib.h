#ifndef CRYPTLIB_H
# define CRYPTLIB_H

#include <linux/version.h>

struct cryptodev_result {
	struct completion completion;
	int err;
};

#include "cipherapi.h"

struct cipher_data {
	int init; /* 0 uninitialized */
	int blocksize;
	int aead;
	int stream;
	int ivsize;
	int alignmask;
	struct {
		/* block ciphers */
		cryptodev_crypto_blkcipher_t *s;
		cryptodev_blkcipher_request_t *request;

		/* AEAD ciphers */
		struct crypto_aead *as;
		struct aead_request *arequest;

		struct cryptodev_result result;
		uint8_t iv[EALG_MAX_BLOCK_LEN];
	} async;
};

int cryptodev_cipher_init(struct cipher_data *out, const char *alg_name,
			  uint8_t *key, size_t keylen, int stream, int aead);
void cryptodev_cipher_deinit(struct cipher_data *cdata);
int cryptodev_get_cipher_key(uint8_t *key, struct session_op *sop, int aead);
int cryptodev_get_cipher_keylen(unsigned int *keylen, struct session_op *sop,
		int aead);
ssize_t cryptodev_cipher_decrypt(struct cipher_data *cdata,
			const struct scatterlist *sg1,
			struct scatterlist *sg2, size_t len);
ssize_t cryptodev_cipher_encrypt(struct cipher_data *cdata,
				const struct scatterlist *sg1,
				struct scatterlist *sg2, size_t len);

/* AEAD */
static inline void cryptodev_cipher_auth(struct cipher_data *cdata,
					 struct scatterlist *sg1, size_t len)
{
	/* for some reason we _have_ to call that even for zero length sgs */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0))
	aead_request_set_assoc(cdata->async.arequest, len ? sg1 : NULL, len);
#else
	aead_request_set_ad(cdata->async.arequest, len);
#endif
}

static inline void cryptodev_cipher_set_tag_size(struct cipher_data *cdata, int size)
{
	if (likely(cdata->aead != 0))
		crypto_aead_setauthsize(cdata->async.as, size);
}

static inline int cryptodev_cipher_get_tag_size(struct cipher_data *cdata)
{
	if (likely(cdata->init && cdata->aead != 0))
		return crypto_aead_authsize(cdata->async.as);
	else
		return 0;
}

static inline void cryptodev_cipher_set_iv(struct cipher_data *cdata,
				void *iv, size_t iv_size)
{
	memcpy(cdata->async.iv, iv, min(iv_size, sizeof(cdata->async.iv)));
}

static inline void cryptodev_cipher_get_iv(struct cipher_data *cdata,
				void *iv, size_t iv_size)
{
	memcpy(iv, cdata->async.iv, min(iv_size, sizeof(cdata->async.iv)));
}

/* Hash */
struct hash_data {
	int init; /* 0 uninitialized */
	int digestsize;
	int alignmask;
	struct {
		struct crypto_ahash *s;
		struct cryptodev_result result;
		struct ahash_request *request;
	} async;
};

int cryptodev_hash_final(struct hash_data *hdata, void *output);
ssize_t cryptodev_hash_update(struct hash_data *hdata,
			struct scatterlist *sg, size_t len);
int cryptodev_hash_reset(struct hash_data *hdata);
void cryptodev_hash_deinit(struct hash_data *hdata);
int cryptodev_hash_init(struct hash_data *hdata, const char *alg_name,
			int hmac_mode, void *mackey, size_t mackeylen);


#endif
