/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
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

#ifndef __ARPC_H
#define __ARPC_H

/* APBridgeA RPC (ARPC) */

enum arpc_result {
	ARPC_SUCCESS		= 0x00,
	ARPC_NO_MEMORY		= 0x01,
	ARPC_INVALID		= 0x02,
	ARPC_TIMEOUT		= 0x03,
	ARPC_UNKNOWN_ERROR	= 0xff,
};

struct arpc_request_message {
	__le16	id;		/* RPC unique id */
	__le16	size;		/* Size in bytes of header + payload */
	__u8	type;		/* RPC type */
	__u8	data[0];	/* ARPC data */
} __packed;

struct arpc_response_message {
	__le16	id;		/* RPC unique id */
	__u8	result;		/* Result of RPC */
} __packed;

/* ARPC requests */
#define ARPC_TYPE_CPORT_CONNECTED		0x01
#define ARPC_TYPE_CPORT_QUIESCE			0x02
#define ARPC_TYPE_CPORT_CLEAR			0x03
#define ARPC_TYPE_CPORT_FLUSH			0x04
#define ARPC_TYPE_CPORT_SHUTDOWN		0x05

struct arpc_cport_connected_req {
	__le16 cport_id;
} __packed;

struct arpc_cport_quiesce_req {
	__le16 cport_id;
	__le16 peer_space;
	__le16 timeout;
} __packed;

struct arpc_cport_clear_req {
	__le16 cport_id;
} __packed;

struct arpc_cport_flush_req {
	__le16 cport_id;
} __packed;

struct arpc_cport_shutdown_req {
	__le16 cport_id;
	__le16 timeout;
	__u8 phase;
} __packed;

#endif	/* __ARPC_H */
