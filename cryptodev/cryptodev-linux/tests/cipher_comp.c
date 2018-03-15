/*
 * Compare encryption results with ones from openssl.
 *
 * Placed under public domain.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#include <sys/ioctl.h>
#include <crypto/cryptodev.h>

#include "openssl_wrapper.h"

#define	BLOCK_SIZE	16
#define	KEY_SIZE	16
#define MAX_DATALEN	(64 * 1024)


static int
test_crypto(int cfd, struct session_op *sess, int datalen)
{
	uint8_t *data, *encrypted;
	uint8_t *encrypted_comp;

	uint8_t iv_in[BLOCK_SIZE];
	uint8_t iv[BLOCK_SIZE];
	uint8_t iv_comp[BLOCK_SIZE];

	struct crypt_op cryp;

	int ret = 0;

	data = malloc(datalen);
	encrypted = malloc(datalen);
	encrypted_comp = malloc(datalen);
	memset(data, datalen & 0xff, datalen);
	memset(encrypted, 0x27, datalen);
	memset(encrypted_comp, 0x41, datalen);

	memset(iv_in, 0x23, sizeof(iv_in));
	memcpy(iv, iv_in, sizeof(iv));
	memcpy(iv_comp, iv_in, sizeof(iv_comp));

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = sess->ses;
	cryp.len = datalen;
	cryp.src = data;
	cryp.dst = encrypted;
	cryp.iv = iv;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_WRITE_IV;
	if ((ret = ioctl(cfd, CIOCCRYPT, &cryp))) {
		perror("ioctl(CIOCCRYPT)");
		goto out;
	}

	cryp.dst = encrypted_comp;
	cryp.iv = iv_comp;

	if ((ret = openssl_cioccrypt(sess, &cryp))) {
		fprintf(stderr, "openssl_cioccrypt() failed!\n");
		goto out;
	}

	if ((ret = memcmp(encrypted, encrypted_comp, cryp.len))) {
		printf("fail for datalen %d, cipher texts do not match!\n", datalen);
	}
	if ((ret = memcmp(iv, iv_comp, BLOCK_SIZE))) {
		printf("fail for datalen %d, IVs do not match!\n", datalen);
	}
out:
	free(data);
	free(encrypted);
	free(encrypted_comp);
	return ret;
}

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

int
main(int argc, char **argv)
{
	int fd;
	struct session_op sess;
	uint8_t key[KEY_SIZE];
	int datalen = BLOCK_SIZE;
	int datalen_end = MAX_DATALEN;
	int i;

	if (argc > 1) {
		datalen = min(max(atoi(argv[1]), BLOCK_SIZE), MAX_DATALEN);
		datalen_end = datalen;
	}
	if (argc > 2) {
		datalen_end = min(atoi(argv[2]), MAX_DATALEN);
		if (datalen_end < datalen)
			datalen_end = datalen;
	}

	/* Open the crypto device */
	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0) {
		perror("open(/dev/crypto)");
		return 1;
	}

	for (i = 0; i < KEY_SIZE; i++)
		key[i] = i & 0xff;
	memset(&sess, 0, sizeof(sess));

	/* encryption test */
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;
	if (ioctl(fd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

#ifdef CIOCGSESSINFO
	{
		struct session_info_op siop = {
			.ses = sess.ses,
		};

		if (ioctl(fd, CIOCGSESSINFO, &siop)) {
			perror("ioctl(CIOCGSESSINFO)");
		} else {
			printf("requested cipher CRYPTO_AES_CBC and mac CRYPTO_SHA1_HMAC,"
			       " got cipher %s with driver %s and hash %s with driver %s\n",
					siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name,
					siop.hash_info.cra_name, siop.hash_info.cra_driver_name);
		}
	}
#endif

	for (; datalen <= datalen_end; datalen += BLOCK_SIZE) {
		if (test_crypto(fd, &sess, datalen)) {
			printf("test_crypto() failed for datalen of %d\n", datalen);
			return 1;
		}
	}

	/* Finish crypto session */
	if (ioctl(fd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
	}

	close(fd);
	return 0;
}
