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

enum cfg_cmd {
	CFG_CMD_GET_DEV_CAP = 0,
};

/* Device capabilities, defined by hw */
struct cfg_cmd_dev_cap {
	struct mgmt_msg_head head;

	u16                  func_id;
	u16                  rsvd1;

	/* Public resources */
	u8                   host_id;
	u8                   ep_id;
	u8                   er_id;
	u8                   port_id;

	u16                  host_total_func;
	u8                   host_pf_num;
	u8                   pf_id_start;
	u16                  host_vf_num;
	u16                  vf_id_start;
	u8                   host_oq_id_mask_val;
	u8                   timer_en;
	u8                   host_valid_bitmap;
	u8                   rsvd_host;

	u16                  svc_cap_en;
	u16                  max_vf;
	u8                   flexq_en;
	u8                   valid_cos_bitmap;
	u8                   port_cos_valid_bitmap;
	u8                   rsvd2[45];

	/* l2nic */
	u16                  nic_max_sq_id;
	u16                  nic_max_rq_id;
	u16                  nic_default_num_queues;

	u8                   rsvd3[250];
};

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

struct comm_cmd_cfg_msix_ctrl_reg {
	struct mgmt_msg_head head;
	u16                  func_id;
	u8                   opcode;
	u8                   rsvd1;
	u16                  msix_index;
	u8                   pending_cnt;
	u8                   coalesce_timer_cnt;
	u8                   resend_timer_cnt;
	u8                   lli_timer_cnt;
	u8                   lli_credit_cnt;
	u8                   rsvd2[5];
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

#define COMM_FUNC_RESET_FLAG \
	(COMM_FUNC_RESET_BIT_COMM | COMM_FUNC_RESET_BIT_COMM_CMD_CH | \
	 COMM_FUNC_RESET_BIT_FLUSH | COMM_FUNC_RESET_BIT_MQM | \
	 COMM_FUNC_RESET_BIT_SMF | COMM_FUNC_RESET_BIT_PF_BW_CFG)

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

struct comm_global_attr {
	u8  max_host_num;
	u8  max_pf_num;
	u16 vf_id_start;
	/* for api cmd to mgmt cpu */
	u8  mgmt_host_node_id;
	u8  cmdq_num;
	u8  rsvd1[34];
};

struct comm_cmd_get_glb_attr {
	struct mgmt_msg_head    head;
	struct comm_global_attr attr;
};

enum comm_func_svc_type {
	COMM_FUNC_SVC_T_COMM = 0,
	COMM_FUNC_SVC_T_NIC  = 1,
};

struct comm_cmd_set_func_svc_used_state {
	struct mgmt_msg_head head;
	u16                  func_id;
	u16                  svc_type;
	u8                   used_state;
	u8                   rsvd[35];
};

struct comm_cmd_set_dma_attr {
	struct mgmt_msg_head head;
	u16                  func_id;
	u8                   entry_idx;
	u8                   st;
	u8                   at;
	u8                   ph;
	u8                   no_snooping;
	u8                   tph_en;
	u32                  resv1;
};

struct comm_cmd_set_ceq_ctrl_reg {
	struct mgmt_msg_head head;
	u16                  func_id;
	u16                  q_id;
	u32                  ctrl0;
	u32                  ctrl1;
	u32                  rsvd1;
};

struct comm_cmd_cfg_wq_page_size {
	struct mgmt_msg_head head;
	u16                  func_id;
	u8                   opcode;
	/* real_size=4KB*2^page_size, range(0~20) must be checked by driver */
	u8                   page_size;
	u32                  rsvd1;
};

struct comm_cmd_set_root_ctxt {
	struct mgmt_msg_head head;
	u16                  func_id;
	u8                   set_cmdq_depth;
	u8                   cmdq_depth;
	u16                  rx_buf_sz;
	u8                   lro_en;
	u8                   rsvd1;
	u16                  sq_depth;
	u16                  rq_depth;
	u64                  rsvd2;
};

struct comm_cmdq_ctxt_info {
	__le64 curr_wqe_page_pfn;
	__le64 wq_block_pfn;
};

struct comm_cmd_set_cmdq_ctxt {
	struct mgmt_msg_head       head;
	u16                        func_id;
	u8                         cmdq_id;
	u8                         rsvd1[5];
	struct comm_cmdq_ctxt_info ctxt;
};

struct comm_cmd_clear_resource {
	struct mgmt_msg_head head;
	u16                  func_id;
	u16                  rsvd1[3];
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
