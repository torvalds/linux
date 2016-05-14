/* Sample code to test firmware-management protocol */

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
		printf("\nUsage: ./firmware <Path of the fw-mgmt-X dev>\n");
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
