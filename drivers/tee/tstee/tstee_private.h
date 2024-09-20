/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Arm Limited
 */

#ifndef TSTEE_PRIVATE_H
#define TSTEE_PRIVATE_H

#include <linux/arm_ffa.h>
#include <linux/bitops.h>
#include <linux/tee_core.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/xarray.h>

/*
 * The description of the ABI implemented in this file is available at
 * https://trusted-services.readthedocs.io/en/v1.0.0/developer/service-access-protocols.html#abi
 */

/* UUID of this protocol */
#define TS_RPC_UUID UUID_INIT(0xbdcd76d7, 0x825e, 0x4751, \
			      0x96, 0x3b, 0x86, 0xd4, 0xf8, 0x49, 0x43, 0xac)

/* Protocol version*/
#define TS_RPC_PROTOCOL_VERSION		(1)

/* Status codes */
#define TS_RPC_OK			(0)

/* RPC control register */
#define TS_RPC_CTRL_REG			(0)
#define OPCODE_MASK			GENMASK(15, 0)
#define IFACE_ID_MASK			GENMASK(23, 16)
#define TS_RPC_CTRL_OPCODE(x)		((u16)(FIELD_GET(OPCODE_MASK, (x))))
#define TS_RPC_CTRL_IFACE_ID(x)		((u8)(FIELD_GET(IFACE_ID_MASK, (x))))
#define TS_RPC_CTRL_PACK_IFACE_OPCODE(i, o)	\
	(FIELD_PREP(IFACE_ID_MASK, (i)) | FIELD_PREP(OPCODE_MASK, (o)))
#define TS_RPC_CTRL_SAP_RC		BIT(30)
#define TS_RPC_CTRL_SAP_ERR		BIT(31)

/* Interface ID for RPC management operations */
#define TS_RPC_MGMT_IFACE_ID		(0xff)

/* Management calls */
#define TS_RPC_OP_GET_VERSION		(0x0000)
#define TS_RPC_GET_VERSION_RESP		(1)

#define TS_RPC_OP_RETRIEVE_MEM		(0x0001)
#define TS_RPC_RETRIEVE_MEM_HANDLE_LSW	(1)
#define TS_RPC_RETRIEVE_MEM_HANDLE_MSW	(2)
#define TS_RPC_RETRIEVE_MEM_TAG_LSW	(3)
#define TS_RPC_RETRIEVE_MEM_TAG_MSW	(4)
#define TS_RPC_RETRIEVE_MEM_RPC_STATUS	(1)

#define TS_RPC_OP_RELINQ_MEM		(0x0002)
#define TS_RPC_RELINQ_MEM_HANDLE_LSW	(1)
#define TS_RPC_RELINQ_MEM_HANDLE_MSW	(2)
#define TS_RPC_RELINQ_MEM_RPC_STATUS	(1)

#define TS_RPC_OP_SERVICE_INFO		(0x0003)
#define TS_RPC_SERVICE_INFO_UUID0	(1)
#define TS_RPC_SERVICE_INFO_UUID1	(2)
#define TS_RPC_SERVICE_INFO_UUID2	(3)
#define TS_RPC_SERVICE_INFO_UUID3	(4)
#define TS_RPC_SERVICE_INFO_RPC_STATUS	(1)
#define TS_RPC_SERVICE_INFO_IFACE	(2)

/* Service call */
#define TS_RPC_SERVICE_MEM_HANDLE_LSW	(1)
#define TS_RPC_SERVICE_MEM_HANDLE_MSW	(2)
#define TS_RPC_SERVICE_REQ_LEN		(3)
#define TS_RPC_SERVICE_CLIENT_ID	(4)
#define TS_RPC_SERVICE_RPC_STATUS	(1)
#define TS_RPC_SERVICE_STATUS		(2)
#define TS_RPC_SERVICE_RESP_LEN		(3)

struct tstee {
	struct ffa_device *ffa_dev;
	struct tee_device *teedev;
	struct tee_shm_pool *pool;
};

struct ts_session {
	u8 iface_id;
};

struct ts_context_data {
	struct xarray sess_list;
};

#endif /* TSTEE_PRIVATE_H */
