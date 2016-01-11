/*
 * drivers/net/ethernet/mellanox/mlxsw/emad.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_EMAD_H
#define _MLXSW_EMAD_H

#define MLXSW_EMAD_MAX_FRAME_LEN 1518	/* Length in u8 */
#define MLXSW_EMAD_MAX_RETRY 5

/* EMAD Ethernet header */
#define MLXSW_EMAD_ETH_HDR_LEN 0x10	/* Length in u8 */
#define MLXSW_EMAD_EH_DMAC "\x01\x02\xc9\x00\x00\x01"
#define MLXSW_EMAD_EH_SMAC "\x00\x02\xc9\x01\x02\x03"
#define MLXSW_EMAD_EH_ETHERTYPE 0x8932
#define MLXSW_EMAD_EH_MLX_PROTO 0
#define MLXSW_EMAD_EH_PROTO_VERSION 0

/* EMAD TLV Types */
enum {
	MLXSW_EMAD_TLV_TYPE_END,
	MLXSW_EMAD_TLV_TYPE_OP,
	MLXSW_EMAD_TLV_TYPE_DR,
	MLXSW_EMAD_TLV_TYPE_REG,
	MLXSW_EMAD_TLV_TYPE_USERDATA,
	MLXSW_EMAD_TLV_TYPE_OOBETH,
};

/* OP TLV */
#define MLXSW_EMAD_OP_TLV_LEN 4		/* Length in u32 */

enum {
	MLXSW_EMAD_OP_TLV_CLASS_REG_ACCESS = 1,
	MLXSW_EMAD_OP_TLV_CLASS_IPC = 2,
};

enum mlxsw_emad_op_tlv_status {
	MLXSW_EMAD_OP_TLV_STATUS_SUCCESS,
	MLXSW_EMAD_OP_TLV_STATUS_BUSY,
	MLXSW_EMAD_OP_TLV_STATUS_VERSION_NOT_SUPPORTED,
	MLXSW_EMAD_OP_TLV_STATUS_UNKNOWN_TLV,
	MLXSW_EMAD_OP_TLV_STATUS_REGISTER_NOT_SUPPORTED,
	MLXSW_EMAD_OP_TLV_STATUS_CLASS_NOT_SUPPORTED,
	MLXSW_EMAD_OP_TLV_STATUS_METHOD_NOT_SUPPORTED,
	MLXSW_EMAD_OP_TLV_STATUS_BAD_PARAMETER,
	MLXSW_EMAD_OP_TLV_STATUS_RESOURCE_NOT_AVAILABLE,
	MLXSW_EMAD_OP_TLV_STATUS_MESSAGE_RECEIPT_ACK,
	MLXSW_EMAD_OP_TLV_STATUS_INTERNAL_ERROR = 0x70,
};

static inline char *mlxsw_emad_op_tlv_status_str(u8 status)
{
	switch (status) {
	case MLXSW_EMAD_OP_TLV_STATUS_SUCCESS:
		return "operation performed";
	case MLXSW_EMAD_OP_TLV_STATUS_BUSY:
		return "device is busy";
	case MLXSW_EMAD_OP_TLV_STATUS_VERSION_NOT_SUPPORTED:
		return "version not supported";
	case MLXSW_EMAD_OP_TLV_STATUS_UNKNOWN_TLV:
		return "unknown TLV";
	case MLXSW_EMAD_OP_TLV_STATUS_REGISTER_NOT_SUPPORTED:
		return "register not supported";
	case MLXSW_EMAD_OP_TLV_STATUS_CLASS_NOT_SUPPORTED:
		return "class not supported";
	case MLXSW_EMAD_OP_TLV_STATUS_METHOD_NOT_SUPPORTED:
		return "method not supported";
	case MLXSW_EMAD_OP_TLV_STATUS_BAD_PARAMETER:
		return "bad parameter";
	case MLXSW_EMAD_OP_TLV_STATUS_RESOURCE_NOT_AVAILABLE:
		return "resource not available";
	case MLXSW_EMAD_OP_TLV_STATUS_MESSAGE_RECEIPT_ACK:
		return "acknowledged. retransmit";
	case MLXSW_EMAD_OP_TLV_STATUS_INTERNAL_ERROR:
		return "internal error";
	default:
		return "*UNKNOWN*";
	}
}

enum {
	MLXSW_EMAD_OP_TLV_REQUEST,
	MLXSW_EMAD_OP_TLV_RESPONSE
};

enum {
	MLXSW_EMAD_OP_TLV_METHOD_QUERY = 1,
	MLXSW_EMAD_OP_TLV_METHOD_WRITE = 2,
	MLXSW_EMAD_OP_TLV_METHOD_SEND = 3,
	MLXSW_EMAD_OP_TLV_METHOD_EVENT = 5,
};

/* END TLV */
#define MLXSW_EMAD_END_TLV_LEN 1	/* Length in u32 */

#endif
