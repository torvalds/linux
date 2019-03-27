/*
 * Crypto wrapper for Linux kernel AF_ALG
 * Copyright (c) 2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <linux/if_alg.h>

#include "common.h"
#include "crypto.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha384.h"
#include "aes.h"


#ifndef SOL_ALG
#define SOL_ALG 279
#endif /* SOL_ALG */


static int linux_af_alg_socket(const char *type, const char *name)
{
	struct sockaddr_alg sa;
	int s;

	if (TEST_FAIL())
		return -1;

	s = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (s < 0) {
		wpa_printf(MSG_ERROR, "%s: Failed to open AF_ALG socket: %s",
			   __func__, strerror(errno));
		return -1;
	}

	os_memset(&sa, 0, sizeof(sa));
	sa.salg_family = AF_ALG;
	os_strlcpy((char *) sa.salg_type, type, sizeof(sa.salg_type));
	os_strlcpy((char *) sa.salg_name, name, sizeof(sa.salg_type));
	if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		wpa_printf(MSG_ERROR,
			   "%s: Failed to bind AF_ALG socket(%s,%s): %s",
			   __func__, type, name, strerror(errno));
		close(s);
		return -1;
	}

	return s;
}


static int linux_af_alg_hash_vector(const char *alg, const u8 *key,
				    size_t key_len, size_t num_elem,
				    const u8 *addr[], const size_t *len,
				    u8 *mac, size_t mac_len)
{
	int s, t;
	size_t i;
	ssize_t res;
	int ret = -1;

	s = linux_af_alg_socket("hash", alg);
	if (s < 0)
		return -1;

	if (key && setsockopt(s, SOL_ALG, ALG_SET_KEY, key, key_len) < 0) {
		wpa_printf(MSG_ERROR, "%s: setsockopt(ALG_SET_KEY) failed: %s",
			   __func__, strerror(errno));
		close(s);
		return -1;
	}

	t = accept(s, NULL, NULL);
	if (t < 0) {
		wpa_printf(MSG_ERROR, "%s: accept on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		close(s);
		return -1;
	}

	for (i = 0; i < num_elem; i++) {
		res = send(t, addr[i], len[i], i + 1 < num_elem ? MSG_MORE : 0);
		if (res < 0) {
			wpa_printf(MSG_ERROR,
				   "%s: send on AF_ALG socket failed: %s",
				   __func__, strerror(errno));
			goto fail;
		}
		if ((size_t) res < len[i]) {
			wpa_printf(MSG_ERROR,
				   "%s: send on AF_ALG socket did not accept full buffer (%d/%d)",
				   __func__, (int) res, (int) len[i]);
			goto fail;
		}
	}

	res = recv(t, mac, mac_len, 0);
	if (res < 0) {
		wpa_printf(MSG_ERROR,
			   "%s: recv on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		goto fail;
	}
	if ((size_t) res < mac_len) {
		wpa_printf(MSG_ERROR,
			   "%s: recv on AF_ALG socket did not return full buffer (%d/%d)",
			   __func__, (int) res, (int) mac_len);
		goto fail;
	}

	ret = 0;
fail:
	close(t);
	close(s);

	return ret;
}


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("md4", NULL, 0, num_elem, addr, len,
					mac, 16);
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("md5", NULL, 0, num_elem, addr, len,
					mac, MD5_MAC_LEN);
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		u8 *mac)
{
	return linux_af_alg_hash_vector("sha1", NULL, 0, num_elem, addr, len,
					mac, SHA1_MAC_LEN);
}


int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	return linux_af_alg_hash_vector("sha256", NULL, 0, num_elem, addr, len,
					mac, SHA256_MAC_LEN);
}


int sha384_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	return linux_af_alg_hash_vector("sha384", NULL, 0, num_elem, addr, len,
					mac, SHA384_MAC_LEN);
}


int sha512_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	return linux_af_alg_hash_vector("sha512", NULL, 0, num_elem, addr, len,
					mac, 64);
}


int hmac_md5_vector(const u8 *key, size_t key_len, size_t num_elem,
		    const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("hmac(md5)", key, key_len, num_elem,
					addr, len, mac, 16);
}


int hmac_md5(const u8 *key, size_t key_len, const u8 *data, size_t data_len,
	     u8 *mac)
{
	return hmac_md5_vector(key, key_len, 1, &data, &data_len, mac);
}


int hmac_sha1_vector(const u8 *key, size_t key_len, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("hmac(sha1)", key, key_len, num_elem,
					addr, len, mac, SHA1_MAC_LEN);
}


int hmac_sha1(const u8 *key, size_t key_len, const u8 *data, size_t data_len,
	      u8 *mac)
{
	return hmac_sha1_vector(key, key_len, 1, &data, &data_len, mac);
}


int hmac_sha256_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("hmac(sha256)", key, key_len, num_elem,
					addr, len, mac, SHA256_MAC_LEN);
}


int hmac_sha256(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha256_vector(key, key_len, 1, &data, &data_len, mac);
}


int hmac_sha384_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("hmac(sha384)", key, key_len, num_elem,
					addr, len, mac, SHA384_MAC_LEN);
}


int hmac_sha384(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha384_vector(key, key_len, 1, &data, &data_len, mac);
}


struct crypto_hash {
	int s;
	int t;
	size_t mac_len;
	int failed;
};


struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len)
{
	struct crypto_hash *ctx;
	const char *name;

	ctx = os_zalloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	switch (alg) {
	case CRYPTO_HASH_ALG_MD5:
		name = "md5";
		ctx->mac_len = MD5_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_SHA1:
		name = "sha1";
		ctx->mac_len = SHA1_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
		name = "hmac(md5)";
		ctx->mac_len = MD5_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		name = "hmac(sha1)";
		ctx->mac_len = SHA1_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_SHA256:
		name = "sha256";
		ctx->mac_len = SHA256_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA256:
		name = "hmac(sha256)";
		ctx->mac_len = SHA256_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_SHA384:
		name = "sha384";
		ctx->mac_len = SHA384_MAC_LEN;
		break;
	case CRYPTO_HASH_ALG_SHA512:
		name = "sha512";
		ctx->mac_len = 64;
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	ctx->s = linux_af_alg_socket("hash", name);
	if (ctx->s < 0) {
		os_free(ctx);
		return NULL;
	}

	if (key && key_len &&
	    setsockopt(ctx->s, SOL_ALG, ALG_SET_KEY, key, key_len) < 0) {
		wpa_printf(MSG_ERROR, "%s: setsockopt(ALG_SET_KEY) failed: %s",
			   __func__, strerror(errno));
		close(ctx->s);
		os_free(ctx);
		return NULL;
	}

	ctx->t = accept(ctx->s, NULL, NULL);
	if (ctx->t < 0) {
		wpa_printf(MSG_ERROR, "%s: accept on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		close(ctx->s);
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len)
{
	ssize_t res;

	if (!ctx)
		return;

	res = send(ctx->t, data, len, MSG_MORE);
	if (res < 0) {
		wpa_printf(MSG_ERROR,
			   "%s: send on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		ctx->failed = 1;
		return;
	}
	if ((size_t) res < len) {
		wpa_printf(MSG_ERROR,
			   "%s: send on AF_ALG socket did not accept full buffer (%d/%d)",
			   __func__, (int) res, (int) len);
		ctx->failed = 1;
		return;
	}
}


static void crypto_hash_deinit(struct crypto_hash *ctx)
{
	close(ctx->s);
	close(ctx->t);
	os_free(ctx);
}


int crypto_hash_finish(struct crypto_hash *ctx, u8 *mac, size_t *len)
{
	ssize_t res;

	if (!ctx)
		return -2;

	if (!mac || !len) {
		crypto_hash_deinit(ctx);
		return 0;
	}

	if (ctx->failed) {
		crypto_hash_deinit(ctx);
		return -2;
	}

	if (*len < ctx->mac_len) {
		crypto_hash_deinit(ctx);
		*len = ctx->mac_len;
		return -1;
	}
	*len = ctx->mac_len;

	res = recv(ctx->t, mac, ctx->mac_len, 0);
	if (res < 0) {
		wpa_printf(MSG_ERROR,
			   "%s: recv on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		crypto_hash_deinit(ctx);
		return -2;
	}
	if ((size_t) res < ctx->mac_len) {
		wpa_printf(MSG_ERROR,
			   "%s: recv on AF_ALG socket did not return full buffer (%d/%d)",
			   __func__, (int) res, (int) ctx->mac_len);
		crypto_hash_deinit(ctx);
		return -2;
	}

	crypto_hash_deinit(ctx);
	return 0;
}


struct linux_af_alg_skcipher {
	int s;
	int t;
};


static void linux_af_alg_skcipher_deinit(struct linux_af_alg_skcipher *skcipher)
{
	if (!skcipher)
		return;
	if (skcipher->s >= 0)
		close(skcipher->s);
	if (skcipher->t >= 0)
		close(skcipher->t);
	os_free(skcipher);
}


static struct linux_af_alg_skcipher *
linux_af_alg_skcipher(const char *alg, const u8 *key, size_t key_len)
{
	struct linux_af_alg_skcipher *skcipher;

	skcipher = os_zalloc(sizeof(*skcipher));
	if (!skcipher)
		goto fail;
	skcipher->t = -1;

	skcipher->s = linux_af_alg_socket("skcipher", alg);
	if (skcipher->s < 0)
		goto fail;

	if (setsockopt(skcipher->s, SOL_ALG, ALG_SET_KEY, key, key_len) < 0) {
		wpa_printf(MSG_ERROR, "%s: setsockopt(ALG_SET_KEY) failed: %s",
			   __func__, strerror(errno));
		goto fail;
	}

	skcipher->t = accept(skcipher->s, NULL, NULL);
	if (skcipher->t < 0) {
		wpa_printf(MSG_ERROR, "%s: accept on AF_ALG socket failed: %s",
			   __func__, strerror(errno));
		goto fail;
	}

	return skcipher;
fail:
	linux_af_alg_skcipher_deinit(skcipher);
	return NULL;
}


static int linux_af_alg_skcipher_oper(struct linux_af_alg_skcipher *skcipher,
				      int enc, const u8 *in, u8 *out)
{
	char buf[CMSG_SPACE(sizeof(u32))];
	struct iovec io[1];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;

	io[0].iov_base = (void *) in;
	io[0].iov_len = AES_BLOCK_SIZE;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32));
	msg.msg_iov = io;
	msg.msg_iovlen = 1;
	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = enc ? ALG_OP_ENCRYPT : ALG_OP_DECRYPT;

	ret = sendmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		return -1;
	}

	ret = read(skcipher->t, out, AES_BLOCK_SIZE);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: read failed: %s",
			   __func__, strerror(errno));
		return -1;
	}
	if (ret < AES_BLOCK_SIZE) {
		wpa_printf(MSG_ERROR,
			   "%s: read did not return full data (%d/%d)",
			   __func__, (int) ret, AES_BLOCK_SIZE);
		return -1;
	}

	return 0;
}


void * aes_encrypt_init(const u8 *key, size_t len)
{
	return linux_af_alg_skcipher("ecb(aes)", key, len);
}


int aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	struct linux_af_alg_skcipher *skcipher = ctx;

	return linux_af_alg_skcipher_oper(skcipher, 1, plain, crypt);
}


void aes_encrypt_deinit(void *ctx)
{
	linux_af_alg_skcipher_deinit(ctx);
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	return linux_af_alg_skcipher("ecb(aes)", key, len);
}


int aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	struct linux_af_alg_skcipher *skcipher = ctx;

	return linux_af_alg_skcipher_oper(skcipher, 0, crypt, plain);
}


void aes_decrypt_deinit(void *ctx)
{
	linux_af_alg_skcipher_deinit(ctx);
}


int rc4_skip(const u8 *key, size_t keylen, size_t skip,
	     u8 *data, size_t data_len)
{
	struct linux_af_alg_skcipher *skcipher;
	u8 *skip_buf;
	char buf[CMSG_SPACE(sizeof(u32))];
	struct iovec io[2];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;

	skip_buf = os_zalloc(skip + 1);
	if (!skip_buf)
		return -1;
	skcipher = linux_af_alg_skcipher("ecb(arc4)", key, keylen);
	if (!skcipher) {
		os_free(skip_buf);
		return -1;
	}

	io[0].iov_base = skip_buf;
	io[0].iov_len = skip;
	io[1].iov_base = data;
	io[1].iov_len = data_len;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32));
	msg.msg_iov = io;
	msg.msg_iovlen = 2;
	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = ALG_OP_ENCRYPT;

	ret = sendmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		os_free(skip_buf);
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}
	os_free(skip_buf);

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	ret = recvmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: recvmsg failed: %s",
			   __func__, strerror(errno));
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}
	linux_af_alg_skcipher_deinit(skcipher);

	if ((size_t) ret < skip + data_len) {
		wpa_printf(MSG_ERROR,
			   "%s: recvmsg did not return full data (%d/%d)",
			   __func__, (int) ret, (int) (skip + data_len));
		return -1;
	}

	return 0;
}


int des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	u8 pkey[8], next, tmp;
	int i;
	struct linux_af_alg_skcipher *skcipher;
	char buf[CMSG_SPACE(sizeof(u32))];
	struct iovec io[1];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;
	int res = -1;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		pkey[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	pkey[i] = next | 1;

	skcipher = linux_af_alg_skcipher("ecb(des)", pkey, sizeof(pkey));
	if (!skcipher)
		goto fail;

	io[0].iov_base = (void *) clear;
	io[0].iov_len = 8;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32));
	msg.msg_iov = io;
	msg.msg_iovlen = 1;
	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = ALG_OP_ENCRYPT;

	ret = sendmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		goto fail;
	}

	ret = read(skcipher->t, cypher, 8);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: read failed: %s",
			   __func__, strerror(errno));
		goto fail;
	}
	if (ret < 8) {
		wpa_printf(MSG_ERROR,
			   "%s: read did not return full data (%d/8)",
			   __func__, (int) ret);
		goto fail;
	}

	res = 0;
fail:
	linux_af_alg_skcipher_deinit(skcipher);
	return res;
}


static int aes_128_cbc_oper(const u8 *key, int enc, const u8 *iv,
			    u8 *data, size_t data_len)
{
	struct linux_af_alg_skcipher *skcipher;
	char buf[100];
	struct iovec io[1];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;
	struct af_alg_iv *alg_iv;
	size_t iv_len = AES_BLOCK_SIZE;

	skcipher = linux_af_alg_skcipher("cbc(aes)", key, 16);
	if (!skcipher)
		return -1;

	io[0].iov_base = (void *) data;
	io[0].iov_len = data_len;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32)) +
		CMSG_SPACE(sizeof(*alg_iv) + iv_len);
	msg.msg_iov = io;
	msg.msg_iovlen = 1;

	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = enc ? ALG_OP_ENCRYPT : ALG_OP_DECRYPT;

	hdr = CMSG_NXTHDR(&msg, hdr);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_IV;
	hdr->cmsg_len = CMSG_SPACE(sizeof(*alg_iv) + iv_len);
	alg_iv = (struct af_alg_iv *) CMSG_DATA(hdr);
	alg_iv->ivlen = iv_len;
	os_memcpy(alg_iv->iv, iv, iv_len);

	ret = sendmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}

	ret = recvmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: recvmsg failed: %s",
			   __func__, strerror(errno));
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}
	if ((size_t) ret < data_len) {
		wpa_printf(MSG_ERROR,
			   "%s: recvmsg not return full data (%d/%d)",
			   __func__, (int) ret, (int) data_len);
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}

	linux_af_alg_skcipher_deinit(skcipher);
	return 0;
}


int aes_128_cbc_encrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	return aes_128_cbc_oper(key, 1, iv, data, data_len);
}


int aes_128_cbc_decrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	return aes_128_cbc_oper(key, 0, iv, data, data_len);
}


int omac1_aes_vector(const u8 *key, size_t key_len, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac)
{
	return linux_af_alg_hash_vector("cmac(aes)", key, key_len, num_elem,
					addr, len, mac, AES_BLOCK_SIZE);
}


int omac1_aes_128_vector(const u8 *key, size_t num_elem,
			 const u8 *addr[], const size_t *len, u8 *mac)
{
	return omac1_aes_vector(key, 16, num_elem, addr, len, mac);
}


int omac1_aes_128(const u8 *key, const u8 *data, size_t data_len, u8 *mac)
{
	return omac1_aes_128_vector(key, 1, &data, &data_len, mac);
}


int omac1_aes_256(const u8 *key, const u8 *data, size_t data_len, u8 *mac)
{
	return omac1_aes_vector(key, 32, 1, &data, &data_len, mac);
}


int aes_unwrap(const u8 *kek, size_t kek_len, int n, const u8 *cipher,
	       u8 *plain)
{
	struct linux_af_alg_skcipher *skcipher;
	char buf[100];
	struct iovec io[1];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;
	struct af_alg_iv *alg_iv;
	size_t iv_len = 8;

	skcipher = linux_af_alg_skcipher("kw(aes)", kek, kek_len);
	if (!skcipher)
		return -1;

	io[0].iov_base = (void *) (cipher + iv_len);
	io[0].iov_len = n * 8;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32)) +
		CMSG_SPACE(sizeof(*alg_iv) + iv_len);
	msg.msg_iov = io;
	msg.msg_iovlen = 1;

	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = ALG_OP_DECRYPT;

	hdr = CMSG_NXTHDR(&msg, hdr);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_IV;
	hdr->cmsg_len = CMSG_SPACE(sizeof(*alg_iv) + iv_len);
	alg_iv = (struct af_alg_iv *) CMSG_DATA(hdr);
	alg_iv->ivlen = iv_len;
	os_memcpy(alg_iv->iv, cipher, iv_len);

	ret = sendmsg(skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		return -1;
	}

	ret = read(skcipher->t, plain, n * 8);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: read failed: %s",
			   __func__, strerror(errno));
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}
	if (ret < n * 8) {
		wpa_printf(MSG_ERROR,
			   "%s: read not return full data (%d/%d)",
			   __func__, (int) ret, n * 8);
		linux_af_alg_skcipher_deinit(skcipher);
		return -1;
	}

	linux_af_alg_skcipher_deinit(skcipher);
	return 0;
}


struct crypto_cipher {
	struct linux_af_alg_skcipher *skcipher;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;
	const char *name;
	struct af_alg_iv *alg_iv;
	size_t iv_len = 0;
	char buf[100];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;

	ctx = os_zalloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		name = "ecb(arc4)";
		break;
	case CRYPTO_CIPHER_ALG_AES:
		name = "cbc(aes)";
		iv_len = AES_BLOCK_SIZE;
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		name = "cbc(des3_ede)";
		iv_len = 8;
		break;
	case CRYPTO_CIPHER_ALG_DES:
		name = "cbc(des)";
		iv_len = 8;
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	ctx->skcipher = linux_af_alg_skcipher(name, key, key_len);
	if (!ctx->skcipher) {
		os_free(ctx);
		return NULL;
	}

	if (iv && iv_len) {
		os_memset(&msg, 0, sizeof(msg));
		os_memset(buf, 0, sizeof(buf));
		msg.msg_control = buf;
		msg.msg_controllen = CMSG_SPACE(sizeof(*alg_iv) + iv_len);
		hdr = CMSG_FIRSTHDR(&msg);
		hdr->cmsg_level = SOL_ALG;
		hdr->cmsg_type = ALG_SET_IV;
		hdr->cmsg_len = CMSG_SPACE(sizeof(*alg_iv) + iv_len);
		alg_iv = (struct af_alg_iv *) CMSG_DATA(hdr);
		alg_iv->ivlen = iv_len;
		os_memcpy(alg_iv->iv, iv, iv_len);

		ret = sendmsg(ctx->skcipher->t, &msg, 0);
		if (ret < 0) {
			wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
				   __func__, strerror(errno));
			linux_af_alg_skcipher_deinit(ctx->skcipher);
			os_free(ctx);
			return NULL;
		}
	}

	return ctx;
}


static int crypto_cipher_oper(struct crypto_cipher *ctx, u32 type, const u8 *in,
			      u8 *out, size_t len)
{
	char buf[CMSG_SPACE(sizeof(u32))];
	struct iovec io[1];
	struct msghdr msg;
	struct cmsghdr *hdr;
	ssize_t ret;
	u32 *op;

	io[0].iov_base = (void *) in;
	io[0].iov_len = len;
	os_memset(&msg, 0, sizeof(msg));
	os_memset(buf, 0, sizeof(buf));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(u32));
	msg.msg_iov = io;
	msg.msg_iovlen = 1;
	hdr = CMSG_FIRSTHDR(&msg);
	hdr->cmsg_level = SOL_ALG;
	hdr->cmsg_type = ALG_SET_OP;
	hdr->cmsg_len = CMSG_LEN(sizeof(u32));
	op = (u32 *) CMSG_DATA(hdr);
	*op = type;

	ret = sendmsg(ctx->skcipher->t, &msg, 0);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: sendmsg failed: %s",
			   __func__, strerror(errno));
		return -1;
	}

	ret = read(ctx->skcipher->t, out, len);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "%s: read failed: %s",
			   __func__, strerror(errno));
		return -1;
	}
	if (ret < (ssize_t) len) {
		wpa_printf(MSG_ERROR,
			   "%s: read did not return full data (%d/%d)",
			   __func__, (int) ret, (int) len);
		return -1;
	}

	return 0;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	return crypto_cipher_oper(ctx, ALG_OP_ENCRYPT, plain, crypt, len);
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	return crypto_cipher_oper(ctx, ALG_OP_DECRYPT, crypt, plain, len);
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	if (ctx) {
		linux_af_alg_skcipher_deinit(ctx->skcipher);
		os_free(ctx);
	}
}


int crypto_global_init(void)
{
	return 0;
}


void crypto_global_deinit(void)
{
}
