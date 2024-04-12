/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Greybus Firmware Management User Header
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 */

#ifndef __GREYBUS_FIRMWARE_USER_H
#define __GREYBUS_FIRMWARE_USER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define GB_FIRMWARE_U_TAG_MAX_SIZE		10

#define GB_FW_U_LOAD_METHOD_UNIPRO		0x01
#define GB_FW_U_LOAD_METHOD_INTERNAL		0x02

#define GB_FW_U_LOAD_STATUS_FAILED		0x00
#define GB_FW_U_LOAD_STATUS_UNVALIDATED		0x01
#define GB_FW_U_LOAD_STATUS_VALIDATED		0x02
#define GB_FW_U_LOAD_STATUS_VALIDATION_FAILED	0x03

#define GB_FW_U_BACKEND_FW_STATUS_SUCCESS	0x01
#define GB_FW_U_BACKEND_FW_STATUS_FAIL_FIND	0x02
#define GB_FW_U_BACKEND_FW_STATUS_FAIL_FETCH	0x03
#define GB_FW_U_BACKEND_FW_STATUS_FAIL_WRITE	0x04
#define GB_FW_U_BACKEND_FW_STATUS_INT		0x05
#define GB_FW_U_BACKEND_FW_STATUS_RETRY		0x06
#define GB_FW_U_BACKEND_FW_STATUS_NOT_SUPPORTED	0x07

#define GB_FW_U_BACKEND_VERSION_STATUS_SUCCESS		0x01
#define GB_FW_U_BACKEND_VERSION_STATUS_NOT_AVAILABLE	0x02
#define GB_FW_U_BACKEND_VERSION_STATUS_NOT_SUPPORTED	0x03
#define GB_FW_U_BACKEND_VERSION_STATUS_RETRY		0x04
#define GB_FW_U_BACKEND_VERSION_STATUS_FAIL_INT		0x05

/* IOCTL support */
struct fw_mgmt_ioc_get_intf_version {
	__u8 firmware_tag[GB_FIRMWARE_U_TAG_MAX_SIZE];
	__u16 major;
	__u16 minor;
} __packed;

struct fw_mgmt_ioc_get_backend_version {
	__u8 firmware_tag[GB_FIRMWARE_U_TAG_MAX_SIZE];
	__u16 major;
	__u16 minor;
	__u8 status;
} __packed;

struct fw_mgmt_ioc_intf_load_and_validate {
	__u8 firmware_tag[GB_FIRMWARE_U_TAG_MAX_SIZE];
	__u8 load_method;
	__u8 status;
	__u16 major;
	__u16 minor;
} __packed;

struct fw_mgmt_ioc_backend_fw_update {
	__u8 firmware_tag[GB_FIRMWARE_U_TAG_MAX_SIZE];
	__u8 status;
} __packed;

#define FW_MGMT_IOCTL_BASE			'F'
#define FW_MGMT_IOC_GET_INTF_FW			_IOR(FW_MGMT_IOCTL_BASE, 0, struct fw_mgmt_ioc_get_intf_version)
#define FW_MGMT_IOC_GET_BACKEND_FW		_IOWR(FW_MGMT_IOCTL_BASE, 1, struct fw_mgmt_ioc_get_backend_version)
#define FW_MGMT_IOC_INTF_LOAD_AND_VALIDATE	_IOWR(FW_MGMT_IOCTL_BASE, 2, struct fw_mgmt_ioc_intf_load_and_validate)
#define FW_MGMT_IOC_INTF_BACKEND_FW_UPDATE	_IOWR(FW_MGMT_IOCTL_BASE, 3, struct fw_mgmt_ioc_backend_fw_update)
#define FW_MGMT_IOC_SET_TIMEOUT_MS		_IOW(FW_MGMT_IOCTL_BASE, 4, unsigned int)
#define FW_MGMT_IOC_MODE_SWITCH			_IO(FW_MGMT_IOCTL_BASE, 5)

#endif /* __GREYBUS_FIRMWARE_USER_H */

