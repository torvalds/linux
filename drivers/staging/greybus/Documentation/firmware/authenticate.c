/*
 * Sample code to test CAP protocol
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. or Linaro Ltd. nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL GOOGLE INC. OR
 * LINARO LTD. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

	printf("UID received: 0x%llx\n", *(long long unsigned int *)(uid.uid));

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
