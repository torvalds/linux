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

#define my_perror(x) {fprintf(stderr, "%s: %d\n", __func__, __LINE__); perror(x); }

static int debug = 0;

static void print_buf(char *desc, const unsigned char *buf, int size)
{
	int i;
	fputs(desc, stdout);
	for (i = 0; i < size; i++) {
		printf("%.2x", (uint8_t) buf[i]);
	}
	fputs("\n", stdout);
}

struct aes_gcm_vectors_st {
	const uint8_t *key;
	const uint8_t *auth;
	int auth_size;
	const uint8_t *plaintext;
	int plaintext_size;
	const uint8_t *iv;
	const uint8_t *ciphertext;
	const uint8_t *tag;
};

struct aes_gcm_vectors_st aes_gcm_vectors[] = {
	{
	 .key = (uint8_t *)
	 "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	 .auth = NULL,
	 .auth_size = 0,
	 .plaintext = (uint8_t *)
	 "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	 .plaintext_size = 16,
	 .ciphertext = (uint8_t *)
	 "\x03\x88\xda\xce\x60\xb6\xa3\x92\xf3\x28\xc2\xb9\x71\xb2\xfe\x78",
	 .iv = (uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	 .tag = (uint8_t *)
	 "\xab\x6e\x47\xd4\x2c\xec\x13\xbd\xf5\x3a\x67\xb2\x12\x57\xbd\xdf"
	},
	{
	 .key = (uint8_t *)
	 "\xfe\xff\xe9\x92\x86\x65\x73\x1c\x6d\x6a\x8f\x94\x67\x30\x83\x08",
	 .auth = NULL,
	 .auth_size = 0,
	 .plaintext = (uint8_t *)
	 "\xd9\x31\x32\x25\xf8\x84\x06\xe5\xa5\x59\x09\xc5\xaf\xf5\x26\x9a\x86\xa7\xa9\x53\x15\x34\xf7\xda\x2e\x4c\x30\x3d\x8a\x31\x8a\x72\x1c\x3c\x0c\x95\x95\x68\x09\x53\x2f\xcf\x0e\x24\x49\xa6\xb5\x25\xb1\x6a\xed\xf5\xaa\x0d\xe6\x57\xba\x63\x7b\x39\x1a\xaf\xd2\x55",
	 .plaintext_size = 64,
	 .ciphertext = (uint8_t *)
	 "\x42\x83\x1e\xc2\x21\x77\x74\x24\x4b\x72\x21\xb7\x84\xd0\xd4\x9c\xe3\xaa\x21\x2f\x2c\x02\xa4\xe0\x35\xc1\x7e\x23\x29\xac\xa1\x2e\x21\xd5\x14\xb2\x54\x66\x93\x1c\x7d\x8f\x6a\x5a\xac\x84\xaa\x05\x1b\xa3\x0b\x39\x6a\x0a\xac\x97\x3d\x58\xe0\x91\x47\x3f\x59\x85",
	 .iv = (uint8_t *)"\xca\xfe\xba\xbe\xfa\xce\xdb\xad\xde\xca\xf8\x88",
	 .tag = (uint8_t *)"\x4d\x5c\x2a\xf3\x27\xcd\x64\xa6\x2c\xf3\x5a\xbd\x2b\xa6\xfa\xb4"
	},
	{
	 .key = (uint8_t *)
	 "\xfe\xff\xe9\x92\x86\x65\x73\x1c\x6d\x6a\x8f\x94\x67\x30\x83\x08",
	 .auth = (uint8_t *)
	 "\xfe\xed\xfa\xce\xde\xad\xbe\xef\xfe\xed\xfa\xce\xde\xad\xbe\xef\xab\xad\xda\xd2",
	 .auth_size = 20,
	 .plaintext = (uint8_t *)
	 "\xd9\x31\x32\x25\xf8\x84\x06\xe5\xa5\x59\x09\xc5\xaf\xf5\x26\x9a\x86\xa7\xa9\x53\x15\x34\xf7\xda\x2e\x4c\x30\x3d\x8a\x31\x8a\x72\x1c\x3c\x0c\x95\x95\x68\x09\x53\x2f\xcf\x0e\x24\x49\xa6\xb5\x25\xb1\x6a\xed\xf5\xaa\x0d\xe6\x57\xba\x63\x7b\x39",
	 .plaintext_size = 60,
	 .ciphertext = (uint8_t *)
	 "\x42\x83\x1e\xc2\x21\x77\x74\x24\x4b\x72\x21\xb7\x84\xd0\xd4\x9c\xe3\xaa\x21\x2f\x2c\x02\xa4\xe0\x35\xc1\x7e\x23\x29\xac\xa1\x2e\x21\xd5\x14\xb2\x54\x66\x93\x1c\x7d\x8f\x6a\x5a\xac\x84\xaa\x05\x1b\xa3\x0b\x39\x6a\x0a\xac\x97\x3d\x58\xe0\x91",
	 .iv = (uint8_t *)"\xca\xfe\xba\xbe\xfa\xce\xdb\xad\xde\xca\xf8\x88",
	 .tag = (uint8_t *)
	 "\x5b\xc9\x4f\xbc\x32\x21\xa5\xdb\x94\xfa\xe9\x5a\xe7\x12\x1a\x47"
	}
};


/* Test against AES-GCM test vectors.
 */
static int test_crypto(int cfd)
{
	int i;
	uint8_t tmp[128];

	struct session_op sess;
	struct crypt_auth_op cao;

	/* Get crypto session for AES128 */

	if (debug) {
		fprintf(stdout, "Tests on AES-GCM vectors: ");
		fflush(stdout);
	}
	for (i = 0;
	     i < sizeof(aes_gcm_vectors) / sizeof(aes_gcm_vectors[0]);
	     i++) {
		memset(&sess, 0, sizeof(sess));
		memset(tmp, 0, sizeof(tmp));

		sess.cipher = CRYPTO_AES_GCM;
		sess.keylen = 16;
		sess.key = (void *) aes_gcm_vectors[i].key;

		if (ioctl(cfd, CIOCGSESSION, &sess)) {
			my_perror("ioctl(CIOCGSESSION)");
			return 1;
		}

		memset(&cao, 0, sizeof(cao));

		cao.ses = sess.ses;
		cao.dst = tmp;
		cao.iv = (void *) aes_gcm_vectors[i].iv;
		cao.iv_len = 12;
		cao.op = COP_ENCRYPT;
		cao.flags = 0;

		if (aes_gcm_vectors[i].auth_size > 0) {
			cao.auth_src = (void *) aes_gcm_vectors[i].auth;
			cao.auth_len = aes_gcm_vectors[i].auth_size;
		}

		if (aes_gcm_vectors[i].plaintext_size > 0) {
			cao.src = (void *) aes_gcm_vectors[i].plaintext;
			cao.len = aes_gcm_vectors[i].plaintext_size;
		}

		if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
			my_perror("ioctl(CIOCAUTHCRYPT)");
			return 1;
		}

		if (aes_gcm_vectors[i].plaintext_size > 0)
			if (memcmp
			    (tmp, aes_gcm_vectors[i].ciphertext,
			     aes_gcm_vectors[i].plaintext_size) != 0) {
				fprintf(stderr,
					"AES-GCM test vector %d failed!\n",
					i);

				print_buf("Cipher: ", tmp, aes_gcm_vectors[i].plaintext_size);
				print_buf("Expected: ", aes_gcm_vectors[i].ciphertext, aes_gcm_vectors[i].plaintext_size);
				return 1;
			}

		if (memcmp
		    (&tmp[cao.len - cao.tag_len], aes_gcm_vectors[i].tag,
		     16) != 0) {
			fprintf(stderr,
				"AES-GCM test vector %d failed (tag)!\n",
				i);

			print_buf("Tag: ", &tmp[cao.len - cao.tag_len], cao.tag_len);
			print_buf("Expected tag: ",
				  aes_gcm_vectors[i].tag, 16);
			return 1;
		}

	}
	
	if (debug) {
		fprintf(stdout, "ok\n");
		fprintf(stdout, "\n");
	}

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		my_perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	return 0;
}

/* Checks if encryption and subsequent decryption 
 * produces the same data.
 */
static int test_encrypt_decrypt(int cfd)
{
	uint8_t plaintext_raw[DATA_SIZE + 63], *plaintext;
	uint8_t ciphertext_raw[DATA_SIZE + 63], *ciphertext;
	uint8_t iv[BLOCK_SIZE];
	uint8_t key[KEY_SIZE];
	uint8_t auth[AUTH_SIZE];
	int enc_len;

	struct session_op sess;
	struct crypt_auth_op cao;
	struct session_info_op siop;

	if (debug) {
		fprintf(stdout, "Tests on AES-GCM encryption/decryption: ");
		fflush(stdout);
	}

	memset(&sess, 0, sizeof(sess));
	memset(&cao, 0, sizeof(cao));

	memset(key, 0x33, sizeof(key));
	memset(iv, 0x03, sizeof(iv));
	memset(auth, 0xf1, sizeof(auth));

	/* Get crypto session for AES128 */
	sess.cipher = CRYPTO_AES_GCM;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		my_perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	siop.ses = sess.ses;
	if (ioctl(cfd, CIOCGSESSINFO, &siop)) {
		my_perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
//      printf("requested cipher CRYPTO_AES_CBC/HMAC-SHA1, got %s with driver %s\n",
//                      siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);

	plaintext = (uint8_t *)buf_align(plaintext_raw, siop.alignmask);
	ciphertext = (uint8_t *)buf_align(ciphertext_raw, siop.alignmask);

	memset(plaintext, 0x15, DATA_SIZE);

	/* Encrypt data.in to data.encrypted */
	cao.ses = sess.ses;
	cao.auth_src = auth;
	cao.auth_len = sizeof(auth);
	cao.len = DATA_SIZE;
	cao.src = plaintext;
	cao.dst = ciphertext;
	cao.iv = iv;
	cao.iv_len = 12;
	cao.op = COP_ENCRYPT;
	cao.flags = 0;

	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		my_perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	enc_len = cao.len;
	//printf("Original plaintext size: %d, ciphertext: %d\n", DATA_SIZE, enc_len);

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		my_perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Get crypto session for AES128 */
	memset(&sess, 0, sizeof(sess));
	sess.cipher = CRYPTO_AES_GCM;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		my_perror("ioctl(CIOCGSESSION)");
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
	cao.iv_len = 12;
	cao.op = COP_DECRYPT;
	cao.flags = 0;

	if (ioctl(cfd, CIOCAUTHCRYPT, &cao)) {
		my_perror("ioctl(CIOCAUTHCRYPT)");
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

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		my_perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	if (debug) {
		fprintf(stdout, "ok\n");
		fprintf(stdout, "\n");
	}

	return 0;
}

static int test_encrypt_decrypt_error(int cfd, int err)
{
	uint8_t plaintext_raw[DATA_SIZE + 63], *plaintext;
	uint8_t ciphertext_raw[DATA_SIZE + 63], *ciphertext;
	uint8_t iv[BLOCK_SIZE];
	uint8_t key[KEY_SIZE];
	uint8_t auth[AUTH_SIZE];
	int enc_len;

	struct session_op sess;
	struct crypt_op co;
	struct crypt_auth_op cao;
	struct session_info_op siop;

	if (debug) {
		fprintf(stdout, "Tests on AES-GCM tag verification: ");
		fflush(stdout);
	}

	memset(&sess, 0, sizeof(sess));
	memset(&cao, 0, sizeof(cao));
	memset(&co, 0, sizeof(co));

	memset(key, 0x33, sizeof(key));
	memset(iv, 0x03, sizeof(iv));
	memset(auth, 0xf1, sizeof(auth));

	/* Get crypto session for AES128 */
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;

	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey =
	    (uint8_t *)
	    "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		my_perror("ioctl(CIOCGSESSION)");
		return 1;
	}

	siop.ses = sess.ses;
	if (ioctl(cfd, CIOCGSESSINFO, &siop)) {
		my_perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
//      printf("requested cipher CRYPTO_AES_CBC/HMAC-SHA1, got %s with driver %s\n",
//                      siop.cipher_info.cra_name, siop.cipher_info.cra_driver_name);

	plaintext = (uint8_t *)buf_align(plaintext_raw, siop.alignmask);
	ciphertext = (uint8_t *)buf_align(ciphertext_raw, siop.alignmask);

	memset(plaintext, 0x15, DATA_SIZE);
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
		my_perror("ioctl(CIOCAUTHCRYPT)");
		return 1;
	}

	enc_len = cao.len;
	//printf("Original plaintext size: %d, ciphertext: %d\n", DATA_SIZE, enc_len);

	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		my_perror("ioctl(CIOCFSESSION)");
		return 1;
	}

	/* Get crypto session for AES128 */
	memset(&sess, 0, sizeof(sess));
	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key = key;
	sess.mac = CRYPTO_SHA1_HMAC;
	sess.mackeylen = 16;
	sess.mackey =
	    (uint8_t *)
	    "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b";

	if (ioctl(cfd, CIOCGSESSION, &sess)) {
		my_perror("ioctl(CIOCGSESSION)");
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
			my_perror("ioctl(CIOCFSESSION)");
			return 1;
		}

		if (debug) {
			fprintf(stdout, "ok\n");
			fprintf(stdout, "\n");
		}
		return 0;
	}

	/* Finish crypto session */
	if (ioctl(cfd, CIOCFSESSION, &sess.ses)) {
		my_perror("ioctl(CIOCFSESSION)");
		return 1;
	}


	fprintf(stderr, "Modification to ciphertext was not detected\n");
	return 1;
}

int main(int argc, char** argv)
{
	int fd = -1, cfd = -1;

	if (argc > 1) debug = 1;

	/* Open the crypto device */
	fd = open("/dev/crypto", O_RDWR, 0);
	if (fd < 0) {
		my_perror("open(/dev/crypto)");
		return 1;
	}

	/* Clone file descriptor */
	if (ioctl(fd, CRIOGET, &cfd)) {
		my_perror("ioctl(CRIOGET)");
		return 1;
	}

	/* Set close-on-exec (not really neede here) */
	if (fcntl(cfd, F_SETFD, 1) == -1) {
		my_perror("fcntl(F_SETFD)");
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
		my_perror("close(cfd)");
		return 1;
	}

	/* Close the original descriptor */
	if (close(fd)) {
		my_perror("close(fd)");
		return 1;
	}

	return 0;
}
