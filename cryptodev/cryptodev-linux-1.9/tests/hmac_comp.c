/*
 * Compare HMAC results with ones from openssl.
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
#define MACKEY_SIZE	20
#define MAX_DATALEN	(64 * 1024)

static void printhex(unsigned char *buf, int buflen)
{
	while (buflen-- > 0) {
		printf("\\x%.2x", *(buf++));
	}
	printf("\n");
}

static int
test_crypto(int cfd, struct session_op *sess, int datalen)
{
	unsigned char *data, *encrypted;
	unsigned char *encrypted_comp;

	unsigned char iv[BLOCK_SIZE];
	unsigned char mac[AALG_MAX_RESULT_LEN];

	unsigned char iv_comp[BLOCK_SIZE];
	unsigned char mac_comp[AALG_MAX_RESULT_LEN];

	struct crypt_op cryp;

	int ret = 0;

	data = malloc(datalen);
	encrypted = malloc(datalen);
	encrypted_comp = malloc(datalen);
	memset(data, datalen & 0xff, datalen);
	memset(encrypted, 0x27, datalen);
	memset(encrypted_comp, 0x28, datalen);

	memset(iv, 0x23, sizeof(iv));
	memset(iv_comp, 0x23, sizeof(iv));
	memset(mac, 0, sizeof(mac));
	memset(mac_comp, 1, sizeof(mac_comp));

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = sess->ses;
	cryp.len = datalen;
	cryp.src = data;
	cryp.dst = encrypted;
	cryp.iv = iv;
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_WRITE_IV;
	if ((ret = ioctl(cfd, CIOCCRYPT, &cryp))) {
		perror("ioctl(CIOCCRYPT)");
		goto out;
	}

	cryp.dst = encrypted_comp;
	cryp.mac = mac_comp;
	cryp.iv = iv_comp;

	if ((ret = openssl_cioccrypt(sess, &cryp))) {
		fprintf(stderr, "openssl_cioccrypt() failed!\n");
		goto out;
	}

	if ((ret = memcmp(encrypted, encrypted_comp, cryp.len))) {
		printf("fail for datalen %d, cipher texts do not match!\n", datalen);
	}
	if ((ret = memcmp(iv, iv_comp, BLOCK_SIZE))) {
		printf("fail for datalen %d, updated IVs do not match!\n", datalen);
	}
	if ((ret = memcmp(mac, mac_comp, AALG_MAX_RESULT_LEN))) {
		printf("fail for datalen 0x%x, MACs do not match!\n", datalen);
		printf("wrong mac: ");
		printhex(mac, 20);
		printf("right mac: ");
		printhex(mac_comp, 20);

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
	unsigned char key[KEY_SIZE], mackey[MACKEY_SIZE];
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
	for (i = 0; i < MACKEY_SIZE; i++)
		mackey[i] = i & 0xff;

	memset(&sess, 0, sizeof(sess));

	/* Hash and encryption in one step test */
	sess.cipher = CRYPTO_AES_CBC;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.keylen = KEY_SIZE;
	sess.key = key;
	sess.mackeylen = MACKEY_SIZE;
	sess.mackey = mackey;
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
