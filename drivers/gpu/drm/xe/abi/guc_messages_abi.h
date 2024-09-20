/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_MESSAGES_ABI_H
#define _ABI_GUC_MESSAGES_ABI_H

/**
 * DOC: HXG Message
 *
 * All messages exchanged with GuC are defined using 32 bit dwords.
 * First dword is treated as a message header. Remaining dwords are optional.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  |   |       |                                                              |
 *  | 0 |    31 | **ORIGIN** - originator of the message                       |
 *  |   |       |   - _`GUC_HXG_ORIGIN_HOST` = 0                               |
 *  |   |       |   - _`GUC_HXG_ORIGIN_GUC` = 1                                |
 *  |   |       |                                                              |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | **TYPE** - message type                                      |
 *  |   |       |   - _`GUC_HXG_TYPE_REQUEST` = 0                              |
 *  |   |       |   - _`GUC_HXG_TYPE_EVENT` = 1                                |
 *  |   |       |   - _`GUC_HXG_TYPE_FAST_REQUEST` = 2                         |
 *  |   |       |   - _`GUC_HXG_TYPE_NO_RESPONSE_BUSY` = 3                     |
 *  |   |       |   - _`GUC_HXG_TYPE_NO_RESPONSE_RETRY` = 5                    |
 *  |   |       |   - _`GUC_HXG_TYPE_RESPONSE_FAILURE` = 6                     |
 *  |   |       |   - _`GUC_HXG_TYPE_RESPONSE_SUCCESS` = 7                     |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **AUX** - auxiliary data (depends on TYPE)                   |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 |                                                              |
 *  +---+-------+                                                              |
 *  |...|       | **PAYLOAD** - optional payload (depends on TYPE)             |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_MSG_MIN_LEN			1u
#define GUC_HXG_MSG_0_ORIGIN			(0x1u << 31)
#define   GUC_HXG_ORIGIN_HOST			0u
#define   GUC_HXG_ORIGIN_GUC			1u
#define GUC_HXG_MSG_0_TYPE			(0x7u << 28)
#define   GUC_HXG_TYPE_REQUEST			0u
#define   GUC_HXG_TYPE_EVENT			1u
#define   GUC_HXG_TYPE_FAST_REQUEST		2u
#define   GUC_HXG_TYPE_NO_RESPONSE_BUSY		3u
#define   GUC_HXG_TYPE_NO_RESPONSE_RETRY	5u
#define   GUC_HXG_TYPE_RESPONSE_FAILURE		6u
#define   GUC_HXG_TYPE_RESPONSE_SUCCESS		7u
#define GUC_HXG_MSG_0_AUX			(0xfffffffu << 0)
#define GUC_HXG_MSG_n_PAYLOAD			(0xffffffffu << 0)

/**
 * DOC: HXG Request
 *
 * The `HXG Request`_ message should be used to initiate synchronous activity
 * for which confirmation or return data is expected.
 *
 * The recipient of this message shall use `HXG Response`_, `HXG Failure`_
 * or `HXG Retry`_ message as a definite reply, and may use `HXG Busy`_
 * message as a intermediate reply.
 *
 * Format of @DATA0 and all @DATAn fields depends on the @ACTION code.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_REQUEST_                                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | **DATA0** - request data (depends on ACTION)                 |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **ACTION** - requested action code                           |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 |                                                              |
 *  +---+-------+                                                              |
 *  |...|       | **DATAn** - optional data (depends on ACTION)                |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_REQUEST_MSG_MIN_LEN		GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_REQUEST_MSG_0_DATA0		(0xfffu << 16)
#define GUC_HXG_REQUEST_MSG_0_ACTION		(0xffffu << 0)
#define GUC_HXG_REQUEST_MSG_n_DATAn		GUC_HXG_MSG_n_PAYLOAD

/**
 * DOC: HXG Fast Request
 *
 * The `HXG Request`_ message should be used to initiate asynchronous activity
 * for which confirmation or return data is not expected.
 *
 * If confirmation is required then `HXG Request`_ shall be used instead.
 *
 * The recipient of this message may only use `HXG Failure`_ message if it was
 * unable to accept this request (like invalid data).
 *
 * Format of `HXG Fast Request`_ message is same as `HXG Request`_ except @TYPE.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN - see `HXG Message`_                                  |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = `GUC_HXG_TYPE_FAST_REQUEST`_                          |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | DATA0 - see `HXG Request`_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | ACTION - see `HXG Request`_                                  |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       | DATAn - see `HXG Request`_                                   |
 *  +---+-------+--------------------------------------------------------------+
 */

/**
 * DOC: HXG Event
 *
 * The `HXG Event`_ message should be used to initiate asynchronous activity
 * that does not involves immediate confirmation nor data.
 *
 * Format of @DATA0 and all @DATAn fields depends on the @ACTION code.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_EVENT_                                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | **DATA0** - event data (depends on ACTION)                   |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **ACTION** - event action code                               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 |                                                              |
 *  +---+-------+                                                              |
 *  |...|       | **DATAn** - optional event  data (depends on ACTION)         |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_EVENT_MSG_MIN_LEN		GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_EVENT_MSG_0_DATA0		(0xfffu << 16)
#define GUC_HXG_EVENT_MSG_0_ACTION		(0xffffu << 0)
#define GUC_HXG_EVENT_MSG_n_DATAn		GUC_HXG_MSG_n_PAYLOAD

/**
 * DOC: HXG Busy
 *
 * The `HXG Busy`_ message may be used to acknowledge reception of the `HXG Request`_
 * message if the recipient expects that it processing will be longer than default
 * timeout.
 *
 * The @COUNTER field may be used as a progress indicator.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_NO_RESPONSE_BUSY_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **COUNTER** - progress indicator                             |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_BUSY_MSG_LEN			GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_BUSY_MSG_0_COUNTER		GUC_HXG_MSG_0_AUX

/**
 * DOC: HXG Retry
 *
 * The `HXG Retry`_ message should be used by recipient to indicate that the
 * `HXG Request`_ message was dropped and it should be resent again.
 *
 * The @REASON field may be used to provide additional information.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_NO_RESPONSE_RETRY_                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **REASON** - reason for retry                                |
 *  |   |       |  - _`GUC_HXG_RETRY_REASON_UNSPECIFIED` = 0                   |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_RETRY_MSG_LEN			GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_RETRY_MSG_0_REASON		GUC_HXG_MSG_0_AUX
#define   GUC_HXG_RETRY_REASON_UNSPECIFIED	0u

/**
 * DOC: HXG Failure
 *
 * The `HXG Failure`_ message shall be used as a reply to the `HXG Request`_
 * message that could not be processed due to an error.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_FAILURE_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 27:16 | **HINT** - additional error hint                             |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  15:0 | **ERROR** - error/result code                                |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_FAILURE_MSG_LEN			GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_FAILURE_MSG_0_HINT		(0xfffu << 16)
#define GUC_HXG_FAILURE_MSG_0_ERROR		(0xffffu << 0)

/**
 * DOC: HXG Response
 *
 * The `HXG Response`_ message shall be used as a reply to the `HXG Request`_
 * message that was successfully processed without an error.
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |    31 | ORIGIN                                                       |
 *  |   +-------+--------------------------------------------------------------+
 *  |   | 30:28 | TYPE = GUC_HXG_TYPE_RESPONSE_SUCCESS_                        |
 *  |   +-------+--------------------------------------------------------------+
 *  |   |  27:0 | **DATA0** - data (depends on ACTION from `HXG Request`_)     |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 |                                                              |
 *  +---+-------+                                                              |
 *  |...|       | **DATAn** - data (depends on ACTION from `HXG Request`_)     |
 *  +---+-------+                                                              |
 *  | n |  31:0 |                                                              |
 *  +---+-------+--------------------------------------------------------------+
 */

#define GUC_HXG_RESPONSE_MSG_MIN_LEN		GUC_HXG_MSG_MIN_LEN
#define GUC_HXG_RESPONSE_MSG_0_DATA0		GUC_HXG_MSG_0_AUX
#define GUC_HXG_RESPONSE_MSG_n_DATAn		GUC_HXG_MSG_n_PAYLOAD

#endif
