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
#include "threshold.h"

int main()
{
int ret;

	ret = get_sha1_threshold();
	if (ret > 0)
		printf("SHA1 in kernel outperforms user-space after %d input bytes\n", ret);

	ret = get_aes_sha1_threshold();
	if (ret > 0)
		printf("AES-SHA1 in kernel outperforms user-space after %d input bytes\n", ret);

	return 0;
}
