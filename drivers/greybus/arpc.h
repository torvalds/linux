/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2016 Google Inc. All rights reserved.
 * Copyright(c) 2016 Linaro Ltd. All rights reserved.
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
