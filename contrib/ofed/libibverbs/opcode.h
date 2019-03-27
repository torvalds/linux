/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INFINIBAND_OPCODE_H
#define INFINIBAND_OPCODE_H

/*
 * This macro cleans up the definitions of constants for BTH opcodes.
 * It is used to define constants such as IBV_OPCODE_UD_SEND_ONLY,
 * which becomes IBV_OPCODE_UD + IBV_OPCODE_SEND_ONLY, and this gives
 * the correct value.
 *
 * In short, user code should use the constants defined using the
 * macro rather than worrying about adding together other constants.
*/
#define IBV_OPCODE(transport, op) \
	IBV_OPCODE_ ## transport ## _ ## op = \
		IBV_OPCODE_ ## transport + IBV_OPCODE_ ## op

enum {
	/* transport types -- just used to define real constants */
	IBV_OPCODE_RC                                = 0x00,
	IBV_OPCODE_UC                                = 0x20,
	IBV_OPCODE_RD                                = 0x40,
	IBV_OPCODE_UD                                = 0x60,

	/* operations -- just used to define real constants */
	IBV_OPCODE_SEND_FIRST                        = 0x00,
	IBV_OPCODE_SEND_MIDDLE                       = 0x01,
	IBV_OPCODE_SEND_LAST                         = 0x02,
	IBV_OPCODE_SEND_LAST_WITH_IMMEDIATE          = 0x03,
	IBV_OPCODE_SEND_ONLY                         = 0x04,
	IBV_OPCODE_SEND_ONLY_WITH_IMMEDIATE          = 0x05,
	IBV_OPCODE_RDMA_WRITE_FIRST                  = 0x06,
	IBV_OPCODE_RDMA_WRITE_MIDDLE                 = 0x07,
	IBV_OPCODE_RDMA_WRITE_LAST                   = 0x08,
	IBV_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE    = 0x09,
	IBV_OPCODE_RDMA_WRITE_ONLY                   = 0x0a,
	IBV_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE    = 0x0b,
	IBV_OPCODE_RDMA_READ_REQUEST                 = 0x0c,
	IBV_OPCODE_RDMA_READ_RESPONSE_FIRST          = 0x0d,
	IBV_OPCODE_RDMA_READ_RESPONSE_MIDDLE         = 0x0e,
	IBV_OPCODE_RDMA_READ_RESPONSE_LAST           = 0x0f,
	IBV_OPCODE_RDMA_READ_RESPONSE_ONLY           = 0x10,
	IBV_OPCODE_ACKNOWLEDGE                       = 0x11,
	IBV_OPCODE_ATOMIC_ACKNOWLEDGE                = 0x12,
	IBV_OPCODE_COMPARE_SWAP                      = 0x13,
	IBV_OPCODE_FETCH_ADD                         = 0x14,

	/* real constants follow -- see comment about above IBV_OPCODE()
	   macro for more details */

	/* RC */
	IBV_OPCODE(RC, SEND_FIRST),
	IBV_OPCODE(RC, SEND_MIDDLE),
	IBV_OPCODE(RC, SEND_LAST),
	IBV_OPCODE(RC, SEND_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(RC, SEND_ONLY),
	IBV_OPCODE(RC, SEND_ONLY_WITH_IMMEDIATE),
	IBV_OPCODE(RC, RDMA_WRITE_FIRST),
	IBV_OPCODE(RC, RDMA_WRITE_MIDDLE),
	IBV_OPCODE(RC, RDMA_WRITE_LAST),
	IBV_OPCODE(RC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(RC, RDMA_WRITE_ONLY),
	IBV_OPCODE(RC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
	IBV_OPCODE(RC, RDMA_READ_REQUEST),
	IBV_OPCODE(RC, RDMA_READ_RESPONSE_FIRST),
	IBV_OPCODE(RC, RDMA_READ_RESPONSE_MIDDLE),
	IBV_OPCODE(RC, RDMA_READ_RESPONSE_LAST),
	IBV_OPCODE(RC, RDMA_READ_RESPONSE_ONLY),
	IBV_OPCODE(RC, ACKNOWLEDGE),
	IBV_OPCODE(RC, ATOMIC_ACKNOWLEDGE),
	IBV_OPCODE(RC, COMPARE_SWAP),
	IBV_OPCODE(RC, FETCH_ADD),

	/* UC */
	IBV_OPCODE(UC, SEND_FIRST),
	IBV_OPCODE(UC, SEND_MIDDLE),
	IBV_OPCODE(UC, SEND_LAST),
	IBV_OPCODE(UC, SEND_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(UC, SEND_ONLY),
	IBV_OPCODE(UC, SEND_ONLY_WITH_IMMEDIATE),
	IBV_OPCODE(UC, RDMA_WRITE_FIRST),
	IBV_OPCODE(UC, RDMA_WRITE_MIDDLE),
	IBV_OPCODE(UC, RDMA_WRITE_LAST),
	IBV_OPCODE(UC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(UC, RDMA_WRITE_ONLY),
	IBV_OPCODE(UC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),

	/* RD */
	IBV_OPCODE(RD, SEND_FIRST),
	IBV_OPCODE(RD, SEND_MIDDLE),
	IBV_OPCODE(RD, SEND_LAST),
	IBV_OPCODE(RD, SEND_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(RD, SEND_ONLY),
	IBV_OPCODE(RD, SEND_ONLY_WITH_IMMEDIATE),
	IBV_OPCODE(RD, RDMA_WRITE_FIRST),
	IBV_OPCODE(RD, RDMA_WRITE_MIDDLE),
	IBV_OPCODE(RD, RDMA_WRITE_LAST),
	IBV_OPCODE(RD, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IBV_OPCODE(RD, RDMA_WRITE_ONLY),
	IBV_OPCODE(RD, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
	IBV_OPCODE(RD, RDMA_READ_REQUEST),
	IBV_OPCODE(RD, RDMA_READ_RESPONSE_FIRST),
	IBV_OPCODE(RD, RDMA_READ_RESPONSE_MIDDLE),
	IBV_OPCODE(RD, RDMA_READ_RESPONSE_LAST),
	IBV_OPCODE(RD, RDMA_READ_RESPONSE_ONLY),
	IBV_OPCODE(RD, ACKNOWLEDGE),
	IBV_OPCODE(RD, ATOMIC_ACKNOWLEDGE),
	IBV_OPCODE(RD, COMPARE_SWAP),
	IBV_OPCODE(RD, FETCH_ADD),

	/* UD */
	IBV_OPCODE(UD, SEND_ONLY),
	IBV_OPCODE(UD, SEND_ONLY_WITH_IMMEDIATE)
};

#endif /* INFINIBAND_OPCODE_H */
