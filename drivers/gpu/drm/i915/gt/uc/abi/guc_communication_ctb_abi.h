/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_COMMUNICATION_CTB_ABI_H
#define _ABI_GUC_COMMUNICATION_CTB_ABI_H

#include <linux/types.h>

/**
 * DOC: CTB based communication
 *
 * The CTB (command transport buffer) communication between Host and GuC
 * is based on u32 data stream written to the shared buffer. One buffer can
 * be used to transmit data only in one direction (one-directional channel).
 *
 * Current status of the each buffer is stored in the buffer descriptor.
 * Buffer descriptor holds tail and head fields that represents active data
 * stream. The tail field is updated by the data producer (sender), and head
 * field is updated by the data consumer (receiver)::
 *
 *      +------------+
 *      | DESCRIPTOR |          +=================+============+========+
 *      +============+          |                 | MESSAGE(s) |        |
 *      | address    |--------->+=================+============+========+
 *      +------------+
 *      | head       |          ^-----head--------^
 *      +------------+
 *      | tail       |          ^---------tail-----------------^
 *      +------------+
 *      | size       |          ^---------------size--------------------^
 *      +------------+
 *
 * Each message in data stream starts with the single u32 treated as a header,
 * followed by optional set of u32 data that makes message specific payload::
 *
 *      +------------+---------+---------+---------+
 *      |         MESSAGE                          |
 *      +------------+---------+---------+---------+
 *      |   msg[0]   |   [1]   |   ...   |  [n-1]  |
 *      +------------+---------+---------+---------+
 *      |   MESSAGE  |       MESSAGE PAYLOAD       |
 *      +   HEADER   +---------+---------+---------+
 *      |            |    0    |   ...   |    n    |
 *      +======+=====+=========+=========+=========+
 *      | 31:16| code|         |         |         |
 *      +------+-----+         |         |         |
 *      |  15:5|flags|         |         |         |
 *      +------+-----+         |         |         |
 *      |   4:0|  len|         |         |         |
 *      +------+-----+---------+---------+---------+
 *
 *                   ^-------------len-------------^
 *
 * The message header consists of:
 *
 * - **len**, indicates length of the message payload (in u32)
 * - **code**, indicates message code
 * - **flags**, holds various bits to control message handling
 */

/*
 * Describes single command transport buffer.
 * Used by both guc-master and clients.
 */
struct guc_ct_buffer_desc {
	u32 addr;		/* gfx address */
	u64 host_private;	/* host private data */
	u32 size;		/* size in bytes */
	u32 head;		/* offset updated by GuC*/
	u32 tail;		/* offset updated by owner */
	u32 is_in_error;	/* error indicator */
	u32 reserved1;
	u32 reserved2;
	u32 owner;		/* id of the channel owner */
	u32 owner_sub_id;	/* owner-defined field for extra tracking */
	u32 reserved[5];
} __packed;

/* Type of command transport buffer */
#define INTEL_GUC_CT_BUFFER_TYPE_SEND	0x0u
#define INTEL_GUC_CT_BUFFER_TYPE_RECV	0x1u

/*
 * Definition of the command transport message header (DW0)
 *
 * bit[4..0]	message len (in dwords)
 * bit[7..5]	reserved
 * bit[8]	response (G2H only)
 * bit[8]	write fence to desc (H2G only)
 * bit[9]	write status to H2G buff (H2G only)
 * bit[10]	send status back via G2H (H2G only)
 * bit[15..11]	reserved
 * bit[31..16]	action code
 */
#define GUC_CT_MSG_LEN_SHIFT			0
#define GUC_CT_MSG_LEN_MASK			0x1F
#define GUC_CT_MSG_IS_RESPONSE			(1 << 8)
#define GUC_CT_MSG_WRITE_FENCE_TO_DESC		(1 << 8)
#define GUC_CT_MSG_WRITE_STATUS_TO_BUFF		(1 << 9)
#define GUC_CT_MSG_SEND_STATUS			(1 << 10)
#define GUC_CT_MSG_ACTION_SHIFT			16
#define GUC_CT_MSG_ACTION_MASK			0xFFFF

#endif /* _ABI_GUC_COMMUNICATION_CTB_ABI_H */
