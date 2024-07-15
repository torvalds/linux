/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Greybus Component Authentication User Header
 *
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
 */

#ifndef __GREYBUS_AUTHENTICATION_USER_H
#define __GREYBUS_AUTHENTICATION_USER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CAP_CERTIFICATE_MAX_SIZE	1600
#define CAP_SIGNATURE_MAX_SIZE		320

/* Certificate class types */
#define CAP_CERT_IMS_EAPC		0x00000001
#define CAP_CERT_IMS_EASC		0x00000002
#define CAP_CERT_IMS_EARC		0x00000003
#define CAP_CERT_IMS_IAPC		0x00000004
#define CAP_CERT_IMS_IASC		0x00000005
#define CAP_CERT_IMS_IARC		0x00000006

/* IMS Certificate response result codes */
#define CAP_IMS_RESULT_CERT_FOUND	0x00
#define CAP_IMS_RESULT_CERT_CLASS_INVAL	0x01
#define CAP_IMS_RESULT_CERT_CORRUPT	0x02
#define CAP_IMS_RESULT_CERT_NOT_FOUND	0x03

/* Authentication types */
#define CAP_AUTH_IMS_PRI		0x00000001
#define CAP_AUTH_IMS_SEC		0x00000002
#define CAP_AUTH_IMS_RSA		0x00000003

/* Authenticate response result codes */
#define CAP_AUTH_RESULT_CR_SUCCESS	0x00
#define CAP_AUTH_RESULT_CR_BAD_TYPE	0x01
#define CAP_AUTH_RESULT_CR_WRONG_EP	0x02
#define CAP_AUTH_RESULT_CR_NO_KEY	0x03
#define CAP_AUTH_RESULT_CR_SIG_FAIL	0x04

/* IOCTL support */
struct cap_ioc_get_endpoint_uid {
	__u8			uid[8];
} __packed;

struct cap_ioc_get_ims_certificate {
	__u32			certificate_class;
	__u32			certificate_id;

	__u8			result_code;
	__u32			cert_size;
	__u8			certificate[CAP_CERTIFICATE_MAX_SIZE];
} __packed;

struct cap_ioc_authenticate {
	__u32			auth_type;
	__u8			uid[8];
	__u8			challenge[32];

	__u8			result_code;
	__u8			response[64];
	__u32			signature_size;
	__u8			signature[CAP_SIGNATURE_MAX_SIZE];
} __packed;

#define CAP_IOCTL_BASE			'C'
#define CAP_IOC_GET_ENDPOINT_UID	_IOR(CAP_IOCTL_BASE, 0, struct cap_ioc_get_endpoint_uid)
#define CAP_IOC_GET_IMS_CERTIFICATE	_IOWR(CAP_IOCTL_BASE, 1, struct cap_ioc_get_ims_certificate)
#define CAP_IOC_AUTHENTICATE		_IOWR(CAP_IOCTL_BASE, 2, struct cap_ioc_authenticate)

#endif /* __GREYBUS_AUTHENTICATION_USER_H */
