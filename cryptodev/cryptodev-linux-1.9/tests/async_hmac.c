/*
 * Demo on how to use /dev/crypto device for HMAC.
 *
 * Placed under public domain.
 *
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>

#include <sys/ioctl.h>
#include <crypto/cryptodev.h>

#include "asynchelper.h"

#ifdef ENABLE_ASYNC

static int debug = 0;

#define	DATA_SIZE	4096
#define	BLOCK_SIZE	16
#define	KEY_SIZE	16
#define SHA1_HASH_LEN   20

static int
test_crypto(int cfd)
{
	struct {
		uint8_t	in[DATA_SIZE],
			encrypted[DATA_SIZE],
			decrypted[DATA_SIZE],
			iv[BLOCK_SIZE],
			key[KEY_SIZE];
	} data;
	struct session_op sess;
	struct crypt_op cryp;
	uint8_t mac[AALG_MAX_RESULT_LEN];
	uint8_t oldmac[AALG_MAX_RESULT_LEN];
	uint8_t md5_hmac_out[] = "\x75\x0c\x78\x3e\x6a\xb0\xb5\x03\xea\xa8\x6e\x31\x0a\x5d\xb7\x38";
	uint8_t sha1_out[] = "\x8f\x82\x03\x94\xf9\x53\x35\x18\x20\x45\xda\x24\xf3\x4d\xe5\x2b\xf8\xbc\x34\x32";
	int i;

	memset(&sess, 0, sizeof(sess));
	memset(&cryp, 0, sizeof(cryp));

	/* Use the garbage that is on the stack :-) */
	/* memset(&data, 0, sizeof(data)); */

	/* SHA1 plain test */
	memset(mac, 0, sizeof(mac));

	sess.cipher = 0;
	sess.mac = CRYPTO_SHA1;
	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	cryp.ses = sess.ses;
	cryp.len = sizeof("what do ya want for nothing?")-1;
	cryp.src = (uint8_t *)"what do ya want for nothing?";
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	if (memcmp(mac, sha1_out, 20)!=0) {
		printf("mac: ");
		for (i=0;i<SHA1_HASH_LEN;i++) {
			printf("%.2x", (uint8_t)mac[i]);
		}
		puts("\n");
		fprintf(stderr, "HASH test 1: failed\n");
	} else {
		if (debug) fprintf(stderr, "HASH test 1: passed\n");
	}

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* MD5-HMAC test */
	memset(mac, 0, sizeof(mac));

	sess.cipher = 0;
	sess.mackey = (uint8_t *)"Jefe";
	sess.mackeylen = 4;
	sess.mac = CRYPTO_MD5_HMAC;
	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	cryp.ses = sess.ses;
	cryp.len = sizeof("what do ya want for nothing?")-1;
	cryp.src = (uint8_t *)"what do ya want for nothing?";
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	if (memcmp(mac, md5_hmac_out, 16)!=0) {
		printf("mac: ");
		for (i=0;i<SHA1_HASH_LEN;i++) {
			printf("%.2x", (uint8_t)mac[i]);
		}
		puts("\n");
		fprintf(stderr, "HMAC test 1: failed\n");
	} else {
		if (debug) fprintf(stderr, "HMAC test 1: passed\n");
	}

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Hash and encryption in one step test */
	sess.cipher = CRYPTO_AES_CBC;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.keylen = KEY_SIZE;
	sess.key = data.key;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";
	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	/* Encrypt data.in to data.encrypted */
	cryp.ses = sess.ses;
	cryp.len = sizeof(data.in);
	cryp.src = data.in;
	cryp.dst = data.encrypted;
	cryp.iv = data.iv;
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	memcpy(oldmac, mac, sizeof(mac));

	/* Decrypt data.encrypted to data.decrypted */
	cryp.src = data.encrypted;
	cryp.dst = data.decrypted;
	cryp.op = COP_DECRYPT;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	/* Verify the result */
	if (memcmp(data.in, data.decrypted, sizeof(data.in)) != 0) {
		fprintf(stderr,
			"FAIL: Decrypted data are different from the input data.\n");
		return 1;
	} else if (debug) 
		printf("Crypt Test: passed\n");

	if (memcmp(mac, oldmac, 20) != 0) {
		fprintf(stderr,
			"FAIL: Hash in decrypted data different than in encrypted.\n");
		return 1;
	} else if (debug)
		printf("HMAC Test 2: passed\n");

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}

static int
test_extras(int cfd)
{
	struct session_op sess;
	struct crypt_op cryp;
	uint8_t mac[AALG_MAX_RESULT_LEN];
	uint8_t sha1_out[] = "\x8f\x82\x03\x94\xf9\x53\x35\x18\x20\x45\xda\x24\xf3\x4d\xe5\x2b\xf8\xbc\x34\x32";
	int i;

	memset(&sess, 0, sizeof(sess));
	memset(&cryp, 0, sizeof(cryp));

	/* Use the garbage that is on the stack :-) */
	/* memset(&data, 0, sizeof(data)); */

	/* SHA1 plain test */
	memset(mac, 0, sizeof(mac));

	sess.cipher = 0;
	sess.mac = CRYPTO_SHA1;
	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	cryp.ses = sess.ses;
	cryp.len = sizeof("what do")-1;
	cryp.src = (uint8_t *)"what do";
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_UPDATE;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	cryp.ses = sess.ses;
	cryp.len = sizeof(" ya want for nothing?")-1;
	cryp.src = (uint8_t *)" ya want for nothing?";
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_FINAL;

	DO_OR_DIE(do_async_crypt(cfd, &cryp), 0);
	DO_OR_DIE(do_async_fetch(cfd, &cryp), 0);

	if (memcmp(mac, sha1_out, 20)!=0) {
		printf("mac: ");
		for (i=0;i<SHA1_HASH_LEN;i++) {
			printf("%.2x", (uint8_t)mac[i]);
		}
		puts("\n");
		fprintf(stderr, "HASH test [update]: failed\n");
	} else {
		if (debug) fprintf(stderr, "HASH test [update]: passed\n");
	}

	memset(mac, 0, sizeof(mac));

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}


int
main()
{
	int fd = -1, cfd = -1;

	/* Open the crypto device */
	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0) {
		perror("open(/dev/crypto)");
		return 1;
	}

	/* Clone file descriptor */
	if (ioctl(fd, CRIOGET, &cfd)) {
		perror("ioctl(CRIOGET)");
		return 1;
	}

	/* Set close-on-exec (not really neede here) */
	if (fcntl(cfd, F_SETFD, 1) == -1) {
		perror("fcntl(F_SETFD)");
		return 1;
	}

	/* Run the test itself */
	if (test_crypto(cfd))
		return 1;

	if (test_extras(cfd))
		return 1;

	/* Close cloned descriptor */
	if (close(cfd)) {
		perror("close(cfd)");
		return 1;
	}

	/* Close the original descriptor */
	if (close(fd)) {
		perror("close(fd)");
		return 1;
	}

	return 0;
}
#else
int
main(int argc, char** argv)
{
	return (0);
}
#endif
