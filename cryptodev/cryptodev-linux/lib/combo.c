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
#include "benchmark.h"
#include "hash.h"

int aead_ctx_init(struct cryptodev_ctx* ctx, int cipher, int hash, void* key, int key_size, int cfd)
{
#ifdef CIOCGSESSINFO
	struct session_info_op siop;
#endif

	memset(ctx, 0, sizeof(*ctx));
	ctx->cfd = cfd;

	ctx->sess.mac = hash;
	ctx->sess.cipher = cipher;
	ctx->sess.key = key;
	ctx->sess.keylen = key_size;

	if (ioctl(ctx->cfd, CIOCGSESSION, &ctx->sess)) {
		perror("ioctl(CIOCGSESSION)");
		return -1;
	}

#ifdef CIOCGSESSINFO
	memset(&siop, 0, sizeof(siop));
	siop.ses = ctx->sess.ses;
	if (ioctl(ctx->cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return -1;
	}
#ifdef DEBUG
	printf("Got %s-%s with drivers %s and %s\n",
			siop.cipher_info.cra_name, siop.hash_info.cra_name,
			siop.cipher_info.cra_driver_name, siop.hash_info.cra_driver_name);
#endif
	/*printf("Alignmask is %x\n", (unsigned int)siop.alignmask);*/
	ctx->alignmask = siop.alignmask;
#endif
	return 0;
}

void aead_ctx_deinit(struct cryptodev_ctx* ctx)
{
	if (ioctl(ctx->cfd, CIOCFSESSION, &ctx->sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
	}
}

int
aead_encrypt(struct cryptodev_ctx* ctx, const void* iv, const void* plaintext, void* ciphertext, size_t size, void* digest)
{
	struct crypt_auth_op cryp;

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = ctx->sess.ses;
	cryp.len = size;
	cryp.iv = (void*)iv;
	cryp.iv_len = 16;
	cryp.src = (void*)plaintext;
	cryp.dst = (void*)ciphertext;
	cryp.flags = COP_FLAG_AEAD_TLS_TYPE;

	if (ioctl(ctx->cfd, CIOCAUTHCRYPT, &cryp)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return -1;
	}

	return 0;
}

static const int sizes[] = {64, 256, 512, 1024, 4096, 16*1024};


int aead_test(int cipher, int mac, void* ukey, int ukey_size,
		void* user_ctx, void (*user_combo)(void* user_ctx, void* plaintext, void* ciphertext, int size, void* res))
{
	int cfd = -1, i, ret;
	struct cryptodev_ctx ctx;
	uint8_t digest[AALG_MAX_RESULT_LEN];
	char text[16*1024];
	char ctext[16*1024];
	char iv[16];
	unsigned long elapsed, counted;
	double t1, t2;
	struct benchmark_st bst;

	/* Open the crypto device */
	cfd = open("/dev/crypto", O_RDWR, 0);
	if (cfd < 0) {
		perror("open(/dev/crypto)");
		return -1;
	}

	aead_ctx_init(&ctx, cipher, mac, ukey, ukey_size, cfd);

	for (i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
		counted = 0;
		ret = start_benchmark(&bst);
		if (ret < 0) {
			ret = -1;
			goto finish;
		}

		do {
			if (aead_encrypt(&ctx, iv, text, text, sizes[i], digest) < 0)
				return -2;
			counted += sizes[i];
		} while(benchmark_must_finish==0);

		ret = stop_benchmark(&bst, &elapsed);
		if (ret < 0) {
			ret = -1;
			goto finish;
		}

		t1 = (double)counted/(double)elapsed;

		/* now check the user function */
		counted = 0;
		ret = start_benchmark(&bst);
		if (ret < 0) {
			ret = -1;
			goto finish;
		}

		do {
			user_combo(user_ctx, text, ctext, sizes[i], digest);
			counted += sizes[i];
		} while(benchmark_must_finish==0);

		ret = stop_benchmark(&bst, &elapsed);
		if (ret < 0) {
			ret = -1;
			goto finish;
		}

		t2 = (double)counted/(double)elapsed;

#ifdef DEBUG
		printf("%d: kernel: %.4f bytes/msec, user: %.4f bytes/msec\n", sizes[i], t1, t2);
#endif
		if (t1 > t2) {
			ret = sizes[i];
			goto finish;
		}
	}

	ret = -1;
finish:
	aead_ctx_deinit(&ctx);

	/* Close the original descriptor */
	if (close(cfd)) {
		perror("close(cfd)");
		return 1;
	}
	return ret;
}
