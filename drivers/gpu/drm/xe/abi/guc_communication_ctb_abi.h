/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_COMMUNICATION_CTB_ABI_H
#define _ABI_GUC_COMMUNICATION_CTB_ABI_H

#include <linux/types.h>
#include <linux/build_bug.h>

#include "guc_messages_abi.h"

/**
 * DOC: CT Buffer
 *
 * Circular buffer used to send `CTB Message`_
 */

/**
 * DOC: CTB Descriptor
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |  31:0 | **HEAD** - offset (in dwords) to the last dword that was     |
 *  |   |       | read from the `CT Buffer`_.                                  |
 *  |   |       | It can only be updated by the receiver.                      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **TAIL** - offset (in dwords) to the last dword that was     |
 *  |   |       | written to the `CT Buffer`_.                                 |
 *  |   |       | It can only be updated by the sender.                        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **STATUS** - status of the CTB                               |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_CTB_STATUS_NO_ERROR` = 0 (normal operation)        |
 *  |   |       |   - _`GUC_CTB_STATUS_OVERFLOW` = 1 (head/tail too large)     |
 *  |   |       |   - _`GUC_CTB_STATUS_UNDERFLOW` = 2 (truncated message)      |
 *  |   |       |   - _`GUC_CTB_STATUS_MISMATCH` = 4 (head/tail modified)      |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       | RESERVED = MBZ                                               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 15|  31:0 | RESERVED = MBZ                                               |
 *  +---+-------+--------------------------------------------------------------+
 */

struct guc_ct_buffer_desc {
	u32 head;
	u32 tail;
	u32 status;
#define GUC_CTB_STATUS_NO_ERROR				0
#define GUC_CTB_STATUS_OVERFLOW				(1 << 0)
#define GUC_CTB_STATUS_UNDERFLOW			(1 << 1)
#define GUC_CTB_STATUS_MISMATCH				(1 << 2)
	u32 reserved[13];
} __packed;
static_assert(sizeof(struct guc_ct_buffer_desc) == 64);

/**
 * DOC: CTB Message
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 | 31:16 | **FENCE** - message identifier                               |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 15:12 | **FORMAT** - format of the CTB message                       |
 *  |   |       |  - _`GUC_CTB_FORMAT_HXG` = 0 - see `CTB HXG Message`_        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  11:8 | **RESERVED**                                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | **NUM_DWORDS** - length of the CTB message (w/o header)      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | optional (depends on FORMAT)                                 |
 *  +---+-------+                                                              |
 *  |...|       |                                                              |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_CTB_HDR_LEN				1u
#define GUC_CTB_MSG_MIN_LEN			GUC_CTB_HDR_LEN
#define GUC_CTB_MSG_MAX_LEN			256u
#define GUC_CTB_MSG_0_FENCE			(0xffff << 16)
#define GUC_CTB_MSG_0_FORMAT			(0xf << 12)
#define   GUC_CTB_FORMAT_HXG			0u
#define GUC_CTB_MSG_0_RESERVED			(0xf << 8)
#define GUC_CTB_MSG_0_NUM_DWORDS		(0xff << 0)

/**
 * DOC: CTB HXG Message
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 | 31:16 | FENCE                                                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 15:12 | FORMAT = GUC_CTB_FORMAT_HXG_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  11:8 | RESERVED = MBZ                                               |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |   7:0 | NUM_DWORDS = length (in dwords) of the embedded HXG message  |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 |                                                              |
 *  +---+-------+                                                              |
 *  |...|       | [Embedded `HXG Message`_]                                    |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_CTB_HXG_MSG_MIN_LEN		(GUC_CTB_MSG_MIN_LEN + GUC_HXG_MSG_MIN_LEN)
#define GUC_CTB_HXG_MSG_MAX_LEN		GUC_CTB_MSG_MAX_LEN

/**
 * DOC: CTB based communication
 *
 * The CTB (command transport buffer) communication between Host and GuC
 * is based on u32 data stream written to the shared buffer. One buffer can
 * be used to transmit data only in one direction (one-directional channel).
 *
 * Current status of the each buffer is maintained in the `CTB Descriptor`_.
 * Each message in data stream is encoded as `CTB HXG Message`_.
 */

#endif
