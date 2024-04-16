// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Sample code to test CAP protocol
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../greybus_authentication.h"

struct cap_ioc_get_endpoint_uid uid;
struct cap_ioc_get_ims_certificate cert = {
	.certificate_class = 0,
	.certificate_id = 0,
};

struct cap_ioc_authenticate authenticate = {
	.auth_type = 0,
	.challenge = {0},
};

int main(int argc, char *argv[])
{
	unsigned int timeout = 10000;
	char *capdev;
	int fd, ret;

	/* Make sure arguments are correct */
	if (argc != 2) {
		printf("\nUsage: ./firmware <Path of the gb-cap-X dev>\n");
		return 0;
	}

	capdev = argv[1];

	printf("Opening %s authentication device\n", capdev);

	fd = open(capdev, O_RDWR);
	if (fd < 0) {
		printf("Failed to open: %s\n", capdev);
		return -1;
	}

	/* Get UID */
	printf("Get UID\n");

	ret = ioctl(fd, CAP_IOC_GET_ENDPOINT_UID, &uid);
	if (ret < 0) {
		printf("Failed to get UID: %s (%d)\n", capdev, ret);
		ret = -1;
		goto close_fd;
	}

	printf("UID received: 0x%llx\n", *(unsigned long long int *)(uid.uid));

	/* Get certificate */
	printf("Get IMS certificate\n");

	ret = ioctl(fd, CAP_IOC_GET_IMS_CERTIFICATE, &cert);
	if (ret < 0) {
		printf("Failed to get IMS certificate: %s (%d)\n", capdev, ret);
		ret = -1;
		goto close_fd;
	}

	printf("IMS Certificate size: %d\n", cert.cert_size);

	/* Authenticate */
	printf("Authenticate module\n");

	memcpy(authenticate.uid, uid.uid, 8);

	ret = ioctl(fd, CAP_IOC_AUTHENTICATE, &authenticate);
	if (ret < 0) {
		printf("Failed to authenticate module: %s (%d)\n", capdev, ret);
		ret = -1;
		goto close_fd;
	}

	printf("Authenticated, result (%02x), sig-size (%02x)\n",
		authenticate.result_code, authenticate.signature_size);

close_fd:
	close(fd);

	return ret;
}
