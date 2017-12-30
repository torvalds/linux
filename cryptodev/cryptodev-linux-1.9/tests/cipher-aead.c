/*
 * Demo on how to use /dev/crypto device for ciphering.
 *
 * Placed under public domain.
 *
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <crypto/cryptodev.h>
#include "testhelper.h"

#define	DATA_SIZE	(8*1024)
#define AUTH_SIZE       31
#define	BLOCK_SIZE	16
#define	KEY_SIZE	16

#define MAC_SIZE 20 /* SHA1 */

static int debug = 0;

static int
get_sha1_hmac(int cfd, void* key, int key_size, void* data1, int data1_size, void* data2, int data2_size, void* mac)
{
	struct session_op sess;
	struct crypt_op cryp;

	memset(&sess, 0, sizeof(sess));
	memset(&cryp, 0, sizeof(cryp));

	sess.cipher = 0;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = key_size;
	sess.mackey = key;
	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	/* Encrypt data.in to data.encrypted */
	cryp.ses = sess.ses;
	cryp.len = data1_size;
	cryp.src = data1;
	cryp.dst = NULL;
	cryp.iv = NULL;
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_UPDATE;
	if (ioctl(cfd, CIOCCRYPT, &cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
	}

	cryp.ses = sess.ses;
	cryp.len = data2_size;
	cryp.src = data2;
	cryp.dst = NULL;
	cryp.iv = NULL;
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	cryp.flags = COP_FLAG_FINAL;
	if (ioctl(cfd, CIOCCRYPT, &cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
	}

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}

static void print_buf(char* desc, unsigned char* buf, int size)
{
int i;
	fputs(desc, stdout);
	for (i=0;i<size;i++) {
		printf("%.2x", (uint8_t)buf[i]);
	}
	fputs("\n", stdout);
}

static int
test_crypto(int cfd)
{
	uint8_t plaintext_raw[DATA_SIZE + 63], *plaintext;
	uint8_t ciphertext_raw[DATA_SIZE + 63], *ciphertext;
	uint8_t iv[BLOCK_SIZE];
	uint8_t key[KEY_SIZE];
	uint8_t auth[AUTH_SIZE];
	uint8_t sha1mac[20];
	int pad, i;

	struct session_op sess;
	struct crypt_op co;
	struct crypt_auth_op cao;
	struct session_info_op siop;

	memset(&sess, 0, sizeof(sess));
	memset(&cao, 0, sizeof(cao));
	memset(&co, 0, sizeof(co));

	memset(key,0x33,  sizeof(key));
	memset(iv, 0x03,  sizeof(iv));
	memset(auth, 0xf1,  sizeof(auth));

	/* Get crypto session for AES128 */
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = (void*)key;

	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	siop.ses = sess.ses;
	if (ioctl(cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
	if (debug)
		printf("requested cipher CRYPTO_AES_CBC/HMAC-SHA1, got %s with driver %s\n",
			siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);

	plaintext = buf_align(plaintext_raw, siop.alignmask);
	ciphertext = buf_align(ciphertext_raw, siop.alignmask);
	memset(plaintext, 0x15, DATA_SIZE);

	if (get_sha1_hmac(cfd, sess.mackey, sess.mackeylen, auth, sizeof(auth), plaintext, DATA_SIZE, sha1mac) != 0) {
		fprintf(stderr, "SHA1 MAC failed\n");
		return 1;
	}

	memcpy(ciphertext, plaintext, DATA_SIZE);

	/* Encrypt data.in to data.encrypted */
	cao.ses = sess.ses;
	cao.auth_src = auth;
	cao.auth_len = sizeof(auth);
	cao.len = DATA_SIZE;
	cao.src = ciphertext;
	cao.dst = ciphertext;
	cao.iv = iv;
	cao.op = COP_ENCRYPT;
	cao.flags = COP_FLAG_AEAD_TLS_TYPE;

	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	//printf("Original plaintext size: %d, ciphertext: %d\n", DATA_SIZE, cao.len);

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Get crypto session for AES128 */
	memset(&sess, 0, sizeof(sess));
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	/* Decrypt data.encrypted to data.decrypted */
	co.ses = sess.ses;
	co.len = cao.len;
	co.src = ciphertext;
	co.dst = ciphertext;
	co.iv = iv;
	co.op = COP_DECRYPT;
	if (ioctl(cfd, CIOCCRYPT, &co)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
	}

	/* Verify the result */
	if (memcmp(plaintext, ciphertext, DATA_SIZE) != 0) {
		int i;
		fprintf(stderr,
			"FAIL: Decrypted data are different from the input data.\n");
		printf("plaintext:");
		for (i = 0; i < DATA_SIZE; i++) {
			if ((i % 30) == 0)
				printf("\n");
			printf("%02x ", plaintext[i]);
		}
		printf("ciphertext:");
		for (i = 0; i < DATA_SIZE; i++) {
			if ((i % 30) == 0)
				printf("\n");
			printf("%02x ", ciphertext[i]);
		}
		printf("\n");
		return 1;
	}

	pad = ciphertext[cao.len-1];
	if (memcmp(&ciphertext[cao.len-MAC_SIZE-pad-1], sha1mac, 20) != 0) {
		fprintf(stderr, "AEAD SHA1 MAC does not match plain MAC\n");
		print_buf("SHA1: ", sha1mac, 20);
		print_buf("SHA1-TLS: ", &ciphertext[cao.len-MAC_SIZE-pad-1], 20);
		return 1;
	}


	for (i=0;i<pad;i++)
		if (ciphertext[cao.len-1-i] != pad) {
			fprintf(stderr, "Pad does not match (expected %d)\n", pad);
			print_buf("PAD: ", &ciphertext[cao.len-1-pad], pad);
			return 1;
		}

	if (debug) printf("Test passed\n");


	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}

static int
test_encrypt_decrypt(int cfd)
{
	uint8_t plaintext_raw[DATA_SIZE + 63], *plaintext;
	uint8_t ciphertext_raw[DATA_SIZE + 63], *ciphertext;
	uint8_t iv[BLOCK_SIZE];
	uint8_t key[KEY_SIZE];
	uint8_t auth[AUTH_SIZE];
	uint8_t sha1mac[20];
	int enc_len;

	struct session_op sess;
	struct crypt_op co;
	struct crypt_auth_op cao;
	struct session_info_op siop;

	memset(&sess, 0, sizeof(sess));
	memset(&cao, 0, sizeof(cao));
	memset(&co, 0, sizeof(co));

	memset(key,0x33,  sizeof(key));
	memset(iv, 0x03,  sizeof(iv));
	memset(auth, 0xf1,  sizeof(auth));

	/* Get crypto session for AES128 */
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	siop.ses = sess.ses;
	if (ioctl(cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
//	printf("requested cipher CRYPTO_AES_CBC/HMAC-SHA1, got %s with driver %s\n",
//			siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);

	plaintext = buf_align(plaintext_raw, siop.alignmask);
	ciphertext = buf_align(ciphertext_raw, siop.alignmask);

	memset(plaintext, 0x15, DATA_SIZE);

	if (get_sha1_hmac(cfd, sess.mackey, sess.mackeylen, auth, sizeof(auth), plaintext, DATA_SIZE, sha1mac) != 0) {
		fprintf(stderr, "SHA1 MAC failed\n");
		return 1;
	}

	memcpy(ciphertext, plaintext, DATA_SIZE);

	/* Encrypt data.in to data.encrypted */
	cao.ses = sess.ses;
	cao.auth_src = (void*)auth;
	cao.auth_len = sizeof(auth);
	cao.len = DATA_SIZE;
	cao.src = (void*)ciphertext;
	cao.dst = (void*)ciphertext;
	cao.iv = iv;
	cao.op = COP_ENCRYPT;
	cao.flags = COP_FLAG_AEAD_TLS_TYPE;

	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	enc_len = cao.len;
	//printf("Original plaintext size: %d, ciphertext: %d\n", DATA_SIZE, enc_len);

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Get crypto session for AES128 */
	memset(&sess, 0, sizeof(sess));
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	/* Decrypt data.encrypted to data.decrypted */
	cao.ses = sess.ses;
	cao.auth_src = auth;
	cao.auth_len = sizeof(auth);
	cao.len = enc_len;
	cao.src = ciphertext;
	cao.dst = ciphertext;
	cao.iv = iv;
	cao.op = COP_DECRYPT;
	cao.flags = COP_FLAG_AEAD_TLS_TYPE;
	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	if (cao.len != DATA_SIZE) {
		fprintf(stderr, "decrypted data size incorrect!\n");
		return 1;
	}

	/* Verify the result */
	if (memcmp(plaintext, ciphertext, DATA_SIZE) != 0) {
		int i;
		fprintf(stderr,
			"FAIL: Decrypted data are different from the input data.\n");
		printf("plaintext:");
		for (i = 0; i < DATA_SIZE; i++) {
			if ((i % 30) == 0)
				printf("\n");
			printf("%02x ", plaintext[i]);
		}
		printf("ciphertext:");
		for (i = 0; i < DATA_SIZE; i++) {
			if ((i % 30) == 0)
				printf("\n");
			printf("%02x ", ciphertext[i]);
		}
		printf("\n");
		return 1;
	}

	if (debug) printf("Test passed\n");


	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}

static int
test_encrypt_decrypt_error(int cfd, int err)
{
	uint8_t plaintext_raw[DATA_SIZE + 63], *plaintext;
	uint8_t ciphertext_raw[DATA_SIZE + 63], *ciphertext;
	uint8_t iv[BLOCK_SIZE];
	uint8_t key[KEY_SIZE];
	uint8_t auth[AUTH_SIZE];
	uint8_t sha1mac[20];
	int enc_len;

	struct session_op sess;
	struct crypt_op co;
	struct crypt_auth_op cao;
	struct session_info_op siop;

	memset(&sess, 0, sizeof(sess));
	memset(&cao, 0, sizeof(cao));
	memset(&co, 0, sizeof(co));

	memset(key,0x33,  sizeof(key));
	memset(iv, 0x03,  sizeof(iv));
	memset(auth, 0xf1,  sizeof(auth));

	/* Get crypto session for AES128 */
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	siop.ses = sess.ses;
	if (ioctl(cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
//	printf("requested cipher CRYPTO_AES_CBC/HMAC-SHA1, got %s with driver %s\n",
//			siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);

	plaintext = buf_align(plaintext_raw, siop.alignmask);
	ciphertext = buf_align(ciphertext_raw, siop.alignmask);
	memset(plaintext, 0x15, DATA_SIZE);

	if (get_sha1_hmac(cfd, sess.mackey, sess.mackeylen, auth, sizeof(auth), plaintext, DATA_SIZE, sha1mac) != 0) {
		fprintf(stderr, "SHA1 MAC failed\n");
		return 1;
	}
	
	memcpy(ciphertext, plaintext, DATA_SIZE);

	/* Encrypt data.in to data.encrypted */
	cao.ses = sess.ses;
	cao.auth_src = auth;
	cao.auth_len = sizeof(auth);
	cao.len = DATA_SIZE;
	cao.src = ciphertext;
	cao.dst = ciphertext;
	cao.iv = iv;
	cao.op = COP_ENCRYPT;
	cao.flags = COP_FLAG_AEAD_TLS_TYPE;

	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	enc_len = cao.len;
	//printf("Original plaintext size: %d, ciphertext: %d\n", DATA_SIZE, enc_len);

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Get crypto session for AES128 */
	memset(&sess, 0, sizeof(sess));
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey = (uint8_t *)"\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	if (err == 0)
		auth[2]++;
	else
		ciphertext[4]++;

	/* Decrypt data.encrypted to data.decrypted */
	cao.ses = sess.ses;
	cao.auth_src = auth;
	cao.auth_len = sizeof(auth);
	cao.len = enc_len;
	cao.src = ciphertext;
	cao.dst = ciphertext;
	cao.iv = iv;
	cao.op = COP_DECRYPT;
	cao.flags = COP_FLAG_AEAD_TLS_TYPE;
	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
			perror("ioctl(CIOCFSESSION)");
			return 1;
		}

		if (debug) printf("Test passed\n");
		return 0;
	}

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
		return 1;
	}


	fprintf(stderr, "Modification to ciphertext was not detected\n");
	return 1;
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

	if (test_encrypt_decrypt(cfd))
		return 1;

	if (test_encrypt_decrypt_error(cfd, 0))
		return 1;

	if (test_encrypt_decrypt_error(cfd, 1))
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

