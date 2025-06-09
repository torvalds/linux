/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_HW_INTF_H_
#define _HINIC3_HW_INTF_H_

#include <linux/bits.h>
#include <linux/types.h>

#define MGMT_MSG_CMD_OP_SET   1
#define MGMT_MSG_CMD_OP_GET   0

#define MGMT_STATUS_PF_SET_VF_ALREADY  0x4
#define MGMT_STATUS_EXIST              0x6
#define MGMT_STATUS_CMD_UNSUPPORTED    0xFF

#define MGMT_MSG_POLLING_TIMEOUT 0

struct mgmt_msg_head {
	u8 status;
	u8 version;
	u8 rsvd0[6];
};

struct mgmt_msg_params {
	const void  *buf_in;
	u32         in_size;
	void        *buf_out;
	u32         expected_out_size;
	u32         timeout_ms;
};

/* CMDQ MODULE_TYPE */
enum mgmt_mod_type {
	/* HW communication module */
	MGMT_MOD_COMM   = 0,
	/* L2NIC module */
	MGMT_MOD_L2NIC  = 1,
	/* Configuration module */
	MGMT_MOD_CFGM   = 7,
	MGMT_MOD_HILINK = 14,
};

static inline void mgmt_msg_params_init_default(struct mgmt_msg_params *msg_params,
						void *inout_buf, u32 buf_size)
{
	msg_params->buf_in = inout_buf;
	msg_params->buf_out = inout_buf;
	msg_params->in_size = buf_size;
	msg_params->expected_out_size = buf_size;
	msg_params->timeout_ms = 0;
}

/* COMM Commands between Driver to fw */
enum comm_cmd {
	/* Commands for clearing FLR and resources */
	COMM_CMD_FUNC_RESET              = 0,
	COMM_CMD_FEATURE_NEGO            = 1,
	COMM_CMD_FLUSH_DOORBELL          = 2,
	COMM_CMD_START_FLUSH             = 3,
	COMM_CMD_GET_GLOBAL_ATTR         = 5,
	COMM_CMD_SET_FUNC_SVC_USED_STATE = 7,

	/* Driver Configuration Commands */
	COMM_CMD_SET_CMDQ_CTXT           = 20,
	COMM_CMD_SET_VAT                 = 21,
	COMM_CMD_CFG_PAGESIZE            = 22,
	COMM_CMD_CFG_MSIX_CTRL_REG       = 23,
	COMM_CMD_SET_CEQ_CTRL_REG        = 24,
	COMM_CMD_SET_DMA_ATTR            = 25,
};

enum comm_func_reset_bits {
	COMM_FUNC_RESET_BIT_FLUSH        = BIT(0),
	COMM_FUNC_RESET_BIT_MQM          = BIT(1),
	COMM_FUNC_RESET_BIT_SMF          = BIT(2),
	COMM_FUNC_RESET_BIT_PF_BW_CFG    = BIT(3),

	COMM_FUNC_RESET_BIT_COMM         = BIT(10),
	/* clear mbox and aeq, The COMM_FUNC_RESET_BIT_COMM bit must be set */
	COMM_FUNC_RESET_BIT_COMM_MGMT_CH = BIT(11),
	/* clear cmdq and ceq, The COMM_FUNC_RESET_BIT_COMM bit must be set */
	COMM_FUNC_RESET_BIT_COMM_CMD_CH  = BIT(12),
	COMM_FUNC_RESET_BIT_NIC          = BIT(13),
};

struct comm_cmd_func_reset {
	struct mgmt_msg_head head;
	u16                  func_id;
	u16                  rsvd1[3];
	u64                  reset_flag;
};

#define COMM_MAX_FEATURE_QWORD  4
struct comm_cmd_feature_nego {
	struct mgmt_msg_head head;
	u16                  func_id;
	u8                   opcode;
	u8                   rsvd;
	u64                  s_feature[COMM_MAX_FEATURE_QWORD];
};

/* Services supported by HW. HW uses these values when delivering events.
 * HW supports multiple services that are not yet supported by driver
 * (e.g. RoCE).
 */
enum hinic3_service_type {
	HINIC3_SERVICE_T_NIC = 0,
	/* MAX is only used by SW for array sizes. */
	HINIC3_SERVICE_T_MAX = 1,
};

#endif
