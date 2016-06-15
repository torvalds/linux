/*
 * Sample code to test firmware-management protocol
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

#include "../../greybus_firmware.h"

static const char *firmware_tag = "03";	/* S3 firmware */

static struct fw_mgmt_ioc_get_fw fw_info;
static struct fw_mgmt_ioc_intf_load_and_validate intf_load;
static struct fw_mgmt_ioc_backend_fw_update backend_update;

int main(int argc, char *argv[])
{
	unsigned int timeout = 10000;
	char *fwdev;
	int fd, ret;

	/* Make sure arguments are correct */
	if (argc != 2) {
		printf("\nUsage: ./firmware <Path of the gb-fw-mgmt-X dev>\n");
		return 0;
	}

	fwdev = argv[1];

	printf("Opening %s firmware management device\n", fwdev);

	fd = open(fwdev, O_RDWR);
	if (fd < 0) {
		printf("Failed to open: %s\n", fwdev);
		ret = -1;
		goto close_fd;
	}

	/* Set Timeout */
	printf("Setting timeout to %u ms\n", timeout);

	ret = ioctl(fd, FW_MGMT_IOC_SET_TIMEOUT_MS, &timeout);
	if (ret < 0) {
		printf("Failed to set timeout: %s (%d)\n", fwdev, ret);
		ret = -1;
		goto close_fd;
	}

	/* Get Interface Firmware Version */
	printf("Get Interface Firmware Version\n");

	ret = ioctl(fd, FW_MGMT_IOC_GET_INTF_FW, &fw_info);
	if (ret < 0) {
		printf("Failed to get interface firmware version: %s (%d)\n",
			fwdev, ret);
		ret = -1;
		goto close_fd;
	}

	printf("Interface Firmware tag (%s), major (%d), minor (%d)\n",
		fw_info.firmware_tag, fw_info.major, fw_info.minor);

	/* Try Interface Firmware load over Unipro */
	printf("Loading Interface Firmware\n");

	intf_load.load_method = GB_FW_U_LOAD_METHOD_UNIPRO;
	intf_load.status = 0;
	intf_load.major = 0;
	intf_load.minor = 0;
	strncpy((char *)&intf_load.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_LEN);

	ret = ioctl(fd, FW_MGMT_IOC_INTF_LOAD_AND_VALIDATE, &intf_load);
	if (ret < 0) {
		printf("Failed to load interface firmware: %s (%d)\n", fwdev,
			ret);
		ret = -1;
		goto close_fd;
	}

	if (intf_load.status != GB_FW_U_LOAD_STATUS_VALIDATED &&
	    intf_load.status != GB_FW_U_LOAD_STATUS_UNVALIDATED) {
		printf("Load status says loading failed: %d\n",
			intf_load.status);
		ret = -1;
		goto close_fd;
	}

	printf("Interface Firmware (%s) Load done: major: %d, minor: %d, status: %d\n",
		firmware_tag, intf_load.major, intf_load.minor,
		intf_load.status);

	/* Get Backend Firmware Version */
	printf("Getting Backend Firmware Version\n");

	strncpy((char *)&fw_info.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_LEN);
	fw_info.major = 0;
	fw_info.minor = 0;

	ret = ioctl(fd, FW_MGMT_IOC_GET_BACKEND_FW, &fw_info);
	if (ret < 0) {
		printf("Failed to get backend firmware version: %s (%d)\n",
			fwdev, ret);
		goto mode_switch;
	}

	printf("Backend Firmware tag (%s), major (%d), minor (%d)\n",
		fw_info.firmware_tag, fw_info.major, fw_info.minor);

	/* Try Backend Firmware Update over Unipro */
	printf("Updating Backend Firmware\n");

	backend_update.status = 0;
	strncpy((char *)&backend_update.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_LEN);

	ret = ioctl(fd, FW_MGMT_IOC_INTF_BACKEND_FW_UPDATE, &backend_update);
	if (ret < 0) {
		printf("Failed to load backend firmware: %s (%d)\n", fwdev, ret);
		goto mode_switch;
	}

	printf("Backend Firmware (%s) Load done: status: %d\n",
		firmware_tag, backend_update.status);

	if (backend_update.status != GB_FW_U_BACKEND_FW_STATUS_SUCCESS) {
		printf("Load status says loading failed: %d\n",
			backend_update.status);
	}

mode_switch:
	/* Initiate Mode-switch to the newly loaded firmware */
	printf("Initiate Mode switch\n");

	ret = ioctl(fd, FW_MGMT_IOC_MODE_SWITCH);
	if (ret < 0)
		printf("Failed to initiate mode-switch (%d)\n", ret);

close_fd:
	close(fd);

	return ret;
}
