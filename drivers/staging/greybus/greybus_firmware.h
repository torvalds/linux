/*
 * Greybus Firmware Management User Header
 *
 * This file is provided under the GPLv2 license.  When using or
 * redistributing this file, you may do so under that license.
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

#ifndef __GREYBUS_FIRMWARE_USER_H
#define __GREYBUS_FIRMWARE_USER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define GB_FIRMWARE_U_TAG_MAX_LEN		10

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

/* IOCTL support */
struct fw_mgmt_ioc_get_fw {
	__u8			firmware_tag[GB_FIRMWARE_U_TAG_MAX_LEN];
	__u16			major;
	__u16			minor;
} __attribute__ ((__packed__));

struct fw_mgmt_ioc_intf_load_and_validate {
	__u8			firmware_tag[GB_FIRMWARE_U_TAG_MAX_LEN];
	__u8			load_method;
	__u8			status;
	__u16			major;
	__u16			minor;
} __attribute__ ((__packed__));

struct fw_mgmt_ioc_backend_fw_update {
	__u8			firmware_tag[GB_FIRMWARE_U_TAG_MAX_LEN];
	__u8			status;
} __attribute__ ((__packed__));

#define FW_MGMT_IOCTL_BASE			'F'
#define FW_MGMT_IOC_GET_INTF_FW			_IOR(FW_MGMT_IOCTL_BASE, 0, struct fw_mgmt_ioc_get_fw)
#define FW_MGMT_IOC_GET_BACKEND_FW		_IOWR(FW_MGMT_IOCTL_BASE, 1, struct fw_mgmt_ioc_get_fw)
#define FW_MGMT_IOC_INTF_LOAD_AND_VALIDATE	_IOWR(FW_MGMT_IOCTL_BASE, 2, struct fw_mgmt_ioc_intf_load_and_validate)
#define FW_MGMT_IOC_INTF_BACKEND_FW_UPDATE	_IOWR(FW_MGMT_IOCTL_BASE, 3, struct fw_mgmt_ioc_backend_fw_update)
#define FW_MGMT_IOC_SET_TIMEOUT_MS		_IOW(FW_MGMT_IOCTL_BASE, 4, unsigned int)

#endif /* __GREYBUS_FIRMWARE_USER_H */

