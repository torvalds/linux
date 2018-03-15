/*
 * Compare digest results with ones from openssl.
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
	uint8_t *data;
	uint8_t mac[AALG_MAX_RESULT_LEN];
	uint8_t mac_comp[AALG_MAX_RESULT_LEN];

	struct crypt_op cryp;

	int ret = 0;

	data = malloc(datalen);
	memset(data, datalen & 0xff, datalen);

	memset(mac, 0, sizeof(mac));
	memset(mac_comp, 0, sizeof(mac_comp));

	memset(&cryp, 0, sizeof(cryp));

	/* Encrypt data.in to data.encrypted */
	cryp.ses = sess->ses;
	cryp.len = datalen;
	cryp.src = data;
	cryp.mac = mac;
	cryp.op = COP_ENCRYPT;
	if ((ret = ioctl(cfd, CIOCCRYPT, &cryp))) {
		perror("ioctl(CIOCCRYPT)");
		goto out;
	}

	cryp.mac = mac_comp;

	if ((ret = openssl_cioccrypt(sess, &cryp))) {
		fprintf(stderr, "openssl_cioccrypt() failed!\n");
		goto out;
	}

	if (memcmp(mac, mac_comp, AALG_MAX_RESULT_LEN)) {
		printf("fail for datalen %d, MACs do not match!\n", datalen);
		ret = 1;
		printf("wrong mac: ");
		printhex(mac, 20);
		printf("right mac: ");
		printhex(mac_comp, 20);
	}

out:
	free(data);
	return ret;
}

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

int
main(int argc, char **argv)
{
	int fd;
	struct session_op sess;
	int datalen = BLOCK_SIZE;
	int datalen_end = MAX_DATALEN;

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

	memset(&sess, 0, sizeof(sess));

	/* Hash test */
	sess.mac = CRYPTO_SHA1;
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
			printf("requested mac CRYPTO_SHA1, got hash %s with driver %s\n",
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
