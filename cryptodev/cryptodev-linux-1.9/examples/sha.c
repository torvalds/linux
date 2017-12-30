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
#include "sha.h"

int sha_ctx_init(struct cryptodev_ctx* ctx, int cfd, const uint8_t *key, unsigned int key_size)
{
#ifdef CIOCGSESSINFO
	struct session_info_op siop;
#endif

	memset(ctx, 0, sizeof(*ctx));
	ctx->cfd = cfd;

	if (key == NULL)
		ctx->sess.mac = CRYPTO_SHA1;
	else {
		ctx->sess.mac = CRYPTO_SHA1_HMAC;
		ctx->sess.mackeylen = key_size;
		ctx->sess.mackey = (void*)key;
	}
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
			siop.hash_info.cra_name, siop.hash_info.cra_driver_name);
	if (!(siop.flags & SIOP_FLAG_KERNEL_DRIVER_ONLY)) {
		printf("Note: This is not an accelerated cipher\n");
	}
	/*printf("Alignmask is %x\n", (unsigned int)siop.alignmask);*/
	ctx->alignmask = siop.alignmask;
#endif
	return 0;
}

void sha_ctx_deinit(struct cryptodev_ctx* ctx) 
{
	if (ioctl(ctx->cfd, CIOCFSESSION, &ctx->sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
	}
}

int
sha_hash(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest)
{
	struct crypt_op cryp;
	void* p;
	
	/* check text and ciphertext alignment */
	if (ctx->alignmask) {
		p = (void*)(((unsigned long)text + ctx->alignmask) & ~ctx->alignmask);
		if (text != p) {
			fprintf(stderr, "text is not aligned\n");
			return -1;
		}
	}

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = ctx->sess.ses;
	cryp.len = size;
	cryp.src = (void*)text;
	cryp.mac = digest;
	if (ioctl(ctx->cfd, CIOCCRYPT, &cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return -1;
	}

	return 0;
}

int
main()
{
	int cfd = -1, i;
	struct cryptodev_ctx ctx;
	uint8_t digest[20];
	char text[] = "The quick brown fox jumps over the lazy dog";
	uint8_t expected[] = "\x2f\xd4\xe1\xc6\x7a\x2d\x28\xfc\xed\x84\x9e\xe1\xbb\x76\xe7\x39\x1b\x93\xeb\x12";

	/* Open the crypto device */
	cfd = open("/dev/crypto", O_RDWR, 0);
	if (cfd < 0) {
		perror("open(/dev/crypto)");
		return 1;
	}

	/* Set close-on-exec (not really neede here) */
	if (fcntl(cfd, F_SETFD, 1) == -1) {
		perror("fcntl(F_SETFD)");
		return 1;
	}

	sha_ctx_init(&ctx, cfd, NULL, 0);
	
	sha_hash(&ctx, text, strlen(text), digest);
	
	sha_ctx_deinit(&ctx);

	printf("digest: ");
	for (i = 0; i < 20; i++) {
		printf("%02x:", digest[i]);
	}
	printf("\n");
	
	if (memcmp(digest, expected, 20) != 0) {
		fprintf(stderr, "SHA1 hashing failed\n");
		return 1;
	}

	/* Close the original descriptor */
	if (close(cfd)) {
		perror("close(cfd)");
		return 1;
	}

	return 0;
}

