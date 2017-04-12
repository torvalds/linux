/*
 * Greybus Component Authentication User Header
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
} __attribute__ ((__packed__));

struct cap_ioc_get_ims_certificate {
	__u32			certificate_class;
	__u32			certificate_id;

	__u8			result_code;
	__u32			cert_size;
	__u8			certificate[CAP_CERTIFICATE_MAX_SIZE];
} __attribute__ ((__packed__));

struct cap_ioc_authenticate {
	__u32			auth_type;
	__u8			uid[8];
	__u8			challenge[32];

	__u8			result_code;
	__u8			response[64];
	__u32			signature_size;
	__u8			signature[CAP_SIGNATURE_MAX_SIZE];
} __attribute__ ((__packed__));

#define CAP_IOCTL_BASE			'C'
#define CAP_IOC_GET_ENDPOINT_UID	_IOR(CAP_IOCTL_BASE, 0, struct cap_ioc_get_endpoint_uid)
#define CAP_IOC_GET_IMS_CERTIFICATE	_IOWR(CAP_IOCTL_BASE, 1, struct cap_ioc_get_ims_certificate)
#define CAP_IOC_AUTHENTICATE		_IOWR(CAP_IOCTL_BASE, 2, struct cap_ioc_authenticate)

#endif /* __GREYBUS_AUTHENTICATION_USER_H */
