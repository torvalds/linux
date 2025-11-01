// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Sample code to test firmware-management protocol
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../greybus_firmware.h"

#define FW_DEV_DEFAULT		"/dev/gb-fw-mgmt-0"
#define FW_TAG_INT_DEFAULT	"s3f"
#define FW_TAG_BCND_DEFAULT	"bf_01"
#define FW_UPDATE_TYPE_DEFAULT	0
#define FW_TIMEOUT_DEFAULT	10000

static const char *firmware_tag;
static const char *fwdev = FW_DEV_DEFAULT;
static unsigned int fw_update_type = FW_UPDATE_TYPE_DEFAULT;
static unsigned int fw_timeout = FW_TIMEOUT_DEFAULT;

static struct fw_mgmt_ioc_get_intf_version intf_fw_info;
static struct fw_mgmt_ioc_get_backend_version backend_fw_info;
static struct fw_mgmt_ioc_intf_load_and_validate intf_load;
static struct fw_mgmt_ioc_backend_fw_update backend_update;

static void usage(void)
{
	printf("\nUsage: ./firmware <gb-fw-mgmt-X (default: gb-fw-mgmt-0)> <interface: 0, backend: 1 (default: 0)> <firmware-tag> (default: \"s3f\"/\"bf_01\") <timeout (default: 10000 ms)>\n");
}

static int update_intf_firmware(int fd)
{
	int ret;

	/* Get Interface Firmware Version */
	printf("Get Interface Firmware Version\n");

	ret = ioctl(fd, FW_MGMT_IOC_GET_INTF_FW, &intf_fw_info);
	if (ret < 0) {
		printf("Failed to get interface firmware version: %s (%d)\n",
		       fwdev, ret);
		return -1;
	}

	printf("Interface Firmware tag (%s), major (%d), minor (%d)\n",
	       intf_fw_info.firmware_tag, intf_fw_info.major,
		intf_fw_info.minor);

	/* Try Interface Firmware load over Unipro */
	printf("Loading Interface Firmware\n");

	intf_load.load_method = GB_FW_U_LOAD_METHOD_UNIPRO;
	intf_load.status = 0;
	intf_load.major = 0;
	intf_load.minor = 0;

	strncpy((char *)&intf_load.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_SIZE);

	ret = ioctl(fd, FW_MGMT_IOC_INTF_LOAD_AND_VALIDATE, &intf_load);
	if (ret < 0) {
		printf("Failed to load interface firmware: %s (%d)\n", fwdev,
		       ret);
		return -1;
	}

	if (intf_load.status != GB_FW_U_LOAD_STATUS_VALIDATED &&
	    intf_load.status != GB_FW_U_LOAD_STATUS_UNVALIDATED) {
		printf("Load status says loading failed: %d\n",
		       intf_load.status);
		return -1;
	}

	printf("Interface Firmware (%s) Load done: major: %d, minor: %d, status: %d\n",
	       firmware_tag, intf_load.major, intf_load.minor,
	       intf_load.status);

	/* Initiate Mode-switch to the newly loaded firmware */
	printf("Initiate Mode switch\n");

	ret = ioctl(fd, FW_MGMT_IOC_MODE_SWITCH);
	if (ret < 0)
		printf("Failed to initiate mode-switch (%d)\n", ret);

	return ret;
}

static int update_backend_firmware(int fd)
{
	int ret;

	/* Get Backend Firmware Version */
	printf("Getting Backend Firmware Version\n");

	strncpy((char *)&backend_fw_info.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_SIZE);

retry_fw_version:
	ret = ioctl(fd, FW_MGMT_IOC_GET_BACKEND_FW, &backend_fw_info);
	if (ret < 0) {
		printf("Failed to get backend firmware version: %s (%d)\n",
		       fwdev, ret);
		return -1;
	}

	printf("Backend Firmware tag (%s), major (%d), minor (%d), status (%d)\n",
	       backend_fw_info.firmware_tag, backend_fw_info.major,
		backend_fw_info.minor, backend_fw_info.status);

	if (backend_fw_info.status == GB_FW_U_BACKEND_VERSION_STATUS_RETRY)
		goto retry_fw_version;

	if ((backend_fw_info.status != GB_FW_U_BACKEND_VERSION_STATUS_SUCCESS) &&
	    (backend_fw_info.status != GB_FW_U_BACKEND_VERSION_STATUS_NOT_AVAILABLE)) {
		printf("Failed to get backend firmware version: %s (%d)\n",
		       fwdev, backend_fw_info.status);
		return -1;
	}

	/* Try Backend Firmware Update over Unipro */
	printf("Updating Backend Firmware\n");

	strncpy((char *)&backend_update.firmware_tag, firmware_tag,
		GB_FIRMWARE_U_TAG_MAX_SIZE);

retry_fw_update:
	backend_update.status = 0;

	ret = ioctl(fd, FW_MGMT_IOC_INTF_BACKEND_FW_UPDATE, &backend_update);
	if (ret < 0) {
		printf("Failed to load backend firmware: %s (%d)\n", fwdev, ret);
		return -1;
	}

	if (backend_update.status == GB_FW_U_BACKEND_FW_STATUS_RETRY) {
		printf("Retrying firmware update: %d\n", backend_update.status);
		goto retry_fw_update;
	}

	if (backend_update.status != GB_FW_U_BACKEND_FW_STATUS_SUCCESS) {
		printf("Load status says loading failed: %d\n",
		       backend_update.status);
	} else {
		printf("Backend Firmware (%s) Load done: status: %d\n",
		       firmware_tag, backend_update.status);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int fd, ret;
	char *endptr;

	if (argc > 1 &&
	    (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage();
		return -1;
	}

	if (argc > 1)
		fwdev = argv[1];

	if (argc > 2)
		fw_update_type = strtoul(argv[2], &endptr, 10);

	if (argc > 3)
		firmware_tag = argv[3];
	else if (!fw_update_type)
		firmware_tag = FW_TAG_INT_DEFAULT;
	else
		firmware_tag = FW_TAG_BCND_DEFAULT;

	if (argc > 4)
		fw_timeout = strtoul(argv[4], &endptr, 10);

	printf("Trying Firmware update: fwdev: %s, type: %s, tag: %s, timeout: %u\n",
	       fwdev, fw_update_type == 0 ? "interface" : "backend",
		firmware_tag, fw_timeout);

	printf("Opening %s firmware management device\n", fwdev);

	fd = open(fwdev, O_RDWR);
	if (fd < 0) {
		printf("Failed to open: %s\n", fwdev);
		return -1;
	}

	/* Set Timeout */
	printf("Setting timeout to %u ms\n", fw_timeout);

	ret = ioctl(fd, FW_MGMT_IOC_SET_TIMEOUT_MS, &fw_timeout);
	if (ret < 0) {
		printf("Failed to set timeout: %s (%d)\n", fwdev, ret);
		ret = -1;
		goto close_fd;
	}

	if (!fw_update_type)
		ret = update_intf_firmware(fd);
	else
		ret = update_backend_firmware(fd);

close_fd:
	close(fd);

	return ret;
}
