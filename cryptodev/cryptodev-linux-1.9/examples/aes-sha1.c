/*
 * Demo on how to use /dev/crypto device for ciphering.
 *
 * Placed under public domain.
 *
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <crypto/cryptodev.h>
#include "aes-sha1.h"

/* This is the TLS version of AES-CBC with HMAC-SHA1. 
 */

int aes_sha1_ctx_init(struct cryptodev_ctx* ctx, int cfd, 
	const uint8_t *key, unsigned int key_size,
	const uint8_t *mac_key, unsigned int mac_key_size)
{
#ifdef CIOCGSESSINFO
	struct session_info_op siop;
#endif

	memset(ctx, 0, sizeof(*ctx));
	ctx->cfd = cfd;

	ctx->sess.cipher = CRYPTO_AES_CBC;
	ctx->sess.keylen = key_size;
	ctx->sess.key = (void*)key;

	ctx->sess.mac = CRYPTO_SHA1_HMAC;
	ctx->sess.mackeylen = mac_key_size;
	ctx->sess.mackey = (void*)mac_key;

	if (ioctl(ctx->cfd, CIOCGSESSION, &ctx->sess)) {
		perror("ioctl(CIOCGSESSION)");
		return -1;
	}

#ifdef CIOCGSESSINFO
	siop.ses = ctx->sess.ses;
	if (ioctl(ctx->cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return -1;
	}
	printf("Got %s with driver %s\n",
			siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);
	if (!(siop.flags & SIOP_FLAG_KERNEL_DRIVER_ONLY)) {
		printf("Note: This is not an accelerated cipher\n");
	}
	/*printf("Alignmask is %x\n", (unsigned int)siop.alignmask); */
	ctx->alignmask = siop.alignmask;
#endif
	return 0;
}

void aes_sha1_ctx_deinit(struct cryptodev_ctx* ctx) 
{
	if (ioctl(ctx->cfd, CIOCFSESSION, &ctx->sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
	}
}

int
aes_sha1_encrypt(struct cryptodev_ctx* ctx, const void* iv, 
	const void* auth, size_t auth_size,
	void* plaintext, size_t size)
{
	struct crypt_auth_op cryp;
	void* p;
	
	/* check plaintext and ciphertext alignment */
	if (ctx->alignmask) {
		p = (void*)(((unsigned long)plaintext + ctx->alignmask) & ~ctx->alignmask);
		if (plaintext != p) {
			fprintf(stderr, "plaintext is not aligned\n");
			return -1;
		}
	}

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = ctx->sess.ses;
	cryp.iv = (void*)iv;
	cryp.op = COP_ENCRYPT;
	cryp.auth_len = auth_size;
	cryp.auth_src = (void*)auth;
	cryp.len = size;
	cryp.src = (void*)plaintext;
	cryp.dst = plaintext;
	cryp.flags = COP_FLAG_AEAD_TLS_TYPE;
	if (ioctl(ctx->cfd, CIOCAUTHCRYPT, &cryp)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return -1;
	}

	return 0;
}

int
aes_sha1_decrypt(struct cryptodev_ctx* ctx, const void* iv, 
	const void* auth, size_t auth_size,
	void* ciphertext, size_t size)
{
	struct crypt_auth_op cryp;
	void* p;
	
	/* check plaintext and ciphertext alignment */
	if (ctx->alignmask) {
		p = (void*)(((unsigned long)ciphertext + ctx->alignmask) & ~ctx->alignmask);
		if (ciphertext != p) {
			fprintf(stderr, "ciphertext is not aligned\n");
			return -1;
		}
	}

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = ctx->sess.ses;
	cryp.iv = (void*)iv;
	cryp.op = COP_DECRYPT;
	cryp.auth_len = auth_size;
	cryp.auth_src = (void*)auth;
	cryp.len = size;
	cryp.src = (void*)ciphertext;
	cryp.dst = ciphertext;
	cryp.flags = COP_FLAG_AEAD_TLS_TYPE;
	if (ioctl(ctx->cfd, CIOCAUTHCRYPT, &cryp)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return -1;
	}

	return 0;
}

