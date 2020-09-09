/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_HW_DEV_H
#define HINIC_HW_DEV_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <net/devlink.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"
#include "hinic_hw_mbox.h"

#define HINIC_MAX_QPS   32

#define HINIC_MGMT_NUM_MSG_CMD  (HINIC_MGMT_MSG_CMD_MAX - \
				 HINIC_MGMT_MSG_CMD_BASE)

#define HINIC_PF_SET_VF_ALREADY				0x4
#define HINIC_MGMT_STATUS_EXIST				0x6
#define HINIC_MGMT_CMD_UNSUPPORTED			0xFF

#define HINIC_CMD_VER_FUNC_ID				2

struct hinic_cap {
	u16     max_qps;
	u16     num_qps;
	u8		max_vf;
	u16     max_vf_qps;
};

enum hw_ioctxt_set_cmdq_depth {
	HW_IOCTXT_SET_CMDQ_DEPTH_DEFAULT,
	HW_IOCTXT_SET_CMDQ_DEPTH_ENABLE,
};

enum hinic_port_cmd {
	HINIC_PORT_CMD_VF_REGISTER = 0x0,
	HINIC_PORT_CMD_VF_UNREGISTER = 0x1,

	HINIC_PORT_CMD_CHANGE_MTU       = 2,

	HINIC_PORT_CMD_ADD_VLAN         = 3,
	HINIC_PORT_CMD_DEL_VLAN         = 4,

	HINIC_PORT_CMD_SET_PFC		= 5,

	HINIC_PORT_CMD_SET_MAC          = 9,
	HINIC_PORT_CMD_GET_MAC          = 10,
	HINIC_PORT_CMD_DEL_MAC          = 11,

	HINIC_PORT_CMD_SET_RX_MODE      = 12,

	HINIC_PORT_CMD_GET_PAUSE_INFO	= 20,
	HINIC_PORT_CMD_SET_PAUSE_INFO	= 21,

	HINIC_PORT_CMD_GET_LINK_STATE   = 24,

	HINIC_PORT_CMD_SET_LRO		= 25,

	HINIC_PORT_CMD_SET_RX_CSUM	= 26,

	HINIC_PORT_CMD_SET_RX_VLAN_OFFLOAD = 27,

	HINIC_PORT_CMD_GET_PORT_STATISTICS = 28,

	HINIC_PORT_CMD_CLEAR_PORT_STATISTICS = 29,

	HINIC_PORT_CMD_GET_VPORT_STAT	= 30,

	HINIC_PORT_CMD_CLEAN_VPORT_STAT	= 31,

	HINIC_PORT_CMD_GET_RSS_TEMPLATE_INDIR_TBL = 37,

	HINIC_PORT_CMD_SET_PORT_STATE   = 41,

	HINIC_PORT_CMD_SET_RSS_TEMPLATE_TBL = 43,

	HINIC_PORT_CMD_GET_RSS_TEMPLATE_TBL = 44,

	HINIC_PORT_CMD_SET_RSS_HASH_ENGINE = 45,

	HINIC_PORT_CMD_GET_RSS_HASH_ENGINE = 46,

	HINIC_PORT_CMD_GET_RSS_CTX_TBL  = 47,

	HINIC_PORT_CMD_SET_RSS_CTX_TBL  = 48,

	HINIC_PORT_CMD_RSS_TEMP_MGR	= 49,

	HINIC_PORT_CMD_RSS_CFG		= 66,

	HINIC_PORT_CMD_FWCTXT_INIT      = 69,

	HINIC_PORT_CMD_GET_LOOPBACK_MODE = 72,
	HINIC_PORT_CMD_SET_LOOPBACK_MODE,

	HINIC_PORT_CMD_ENABLE_SPOOFCHK = 78,

	HINIC_PORT_CMD_GET_MGMT_VERSION = 88,

	HINIC_PORT_CMD_SET_FUNC_STATE   = 93,

	HINIC_PORT_CMD_GET_GLOBAL_QPN   = 102,

	HINIC_PORT_CMD_SET_VF_RATE = 105,

	HINIC_PORT_CMD_SET_VF_VLAN	= 106,

	HINIC_PORT_CMD_CLR_VF_VLAN,

	HINIC_PORT_CMD_SET_TSO          = 112,

	HINIC_PORT_CMD_UPDATE_FW	= 114,

	HINIC_PORT_CMD_SET_RQ_IQ_MAP	= 115,

	HINIC_PORT_CMD_LINK_STATUS_REPORT = 160,

	HINIC_PORT_CMD_UPDATE_MAC = 164,

	HINIC_PORT_CMD_GET_CAP          = 170,

	HINIC_PORT_CMD_GET_LINK_MODE	= 217,

	HINIC_PORT_CMD_SET_SPEED	= 218,

	HINIC_PORT_CMD_SET_AUTONEG	= 219,

	HINIC_PORT_CMD_GET_STD_SFP_INFO = 240,

	HINIC_PORT_CMD_SET_LRO_TIMER	= 244,

	HINIC_PORT_CMD_SET_VF_MAX_MIN_RATE = 249,

	HINIC_PORT_CMD_GET_SFP_ABS	= 251,
};

/* cmd of mgmt CPU message for HILINK module */
enum hinic_hilink_cmd {
	HINIC_HILINK_CMD_GET_LINK_INFO		= 0x3,
	HINIC_HILINK_CMD_SET_LINK_SETTINGS	= 0x8,
};

enum hinic_ucode_cmd {
	HINIC_UCODE_CMD_MODIFY_QUEUE_CONTEXT    = 0,
	HINIC_UCODE_CMD_CLEAN_QUEUE_CONTEXT,
	HINIC_UCODE_CMD_ARM_SQ,
	HINIC_UCODE_CMD_ARM_RQ,
	HINIC_UCODE_CMD_SET_RSS_INDIR_TABLE,
	HINIC_UCODE_CMD_SET_RSS_CONTEXT_TABLE,
	HINIC_UCODE_CMD_GET_RSS_INDIR_TABLE,
	HINIC_UCODE_CMD_GET_RSS_CONTEXT_TABLE,
	HINIC_UCODE_CMD_SET_IQ_ENABLE,
	HINIC_UCODE_CMD_SET_RQ_FLUSH            = 10
};

#define NIC_RSS_CMD_TEMP_ALLOC  0x01
#define NIC_RSS_CMD_TEMP_FREE   0x02

enum hinic_mgmt_msg_cmd {
	HINIC_MGMT_MSG_CMD_BASE         = 0xA0,

	HINIC_MGMT_MSG_CMD_LINK_STATUS  = 0xA0,

	HINIC_MGMT_MSG_CMD_CABLE_PLUG_EVENT	= 0xE5,
	HINIC_MGMT_MSG_CMD_LINK_ERR_EVENT	= 0xE6,

	HINIC_MGMT_MSG_CMD_MAX,
};

enum hinic_cb_state {
	HINIC_CB_ENABLED = BIT(0),
	HINIC_CB_RUNNING = BIT(1),
};

enum hinic_res_state {
	HINIC_RES_CLEAN         = 0,
	HINIC_RES_ACTIVE        = 1,
};

struct hinic_cmd_fw_ctxt {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rx_buf_sz;

	u32     rsvd1;
};

struct hinic_cmd_hw_ioctxt {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;

	u16     rsvd1;

	u8      set_cmdq_depth;
	u8      cmdq_depth;

	u8      lro_en;
	u8      rsvd3;
	u8      ppf_idx;
	u8      rsvd4;

	u16     rq_depth;
	u16     rx_buf_sz_idx;
	u16     sq_depth;
};

struct hinic_cmd_io_status {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      rsvd1;
	u8      rsvd2;
	u32     io_status;
};

struct hinic_cmd_clear_io_res {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      rsvd1;
	u8      rsvd2;
};

struct hinic_cmd_set_res_state {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      state;
	u8      rsvd1;
	u32     rsvd2;
};

struct hinic_ceq_ctrl_reg {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u16 func_id;
	u16 q_id;
	u32 ctrl0;
	u32 ctrl1;
};

struct hinic_cmd_base_qpn {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     qpn;
};

struct hinic_cmd_hw_ci {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;

	u8      dma_attr_off;
	u8      pending_limit;
	u8      coalesc_timer;

	u8      msix_en;
	u16     msix_entry_idx;

	u32     sq_id;
	u32     rsvd1;
	u64     ci_addr;
};

struct hinic_cmd_l2nic_reset {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	reset_flag;
};

struct hinic_msix_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	msix_index;
	u8	pending_cnt;
	u8	coalesce_timer_cnt;
	u8	lli_timer_cnt;
	u8	lli_credit_cnt;
	u8	resend_timer_cnt;
	u8	rsvd1[3];
};

struct hinic_set_random_id {
	u8    status;
	u8    version;
	u8    rsvd0[6];

	u8    vf_in_pf;
	u8    rsvd1;
	u16   func_idx;
	u32   random_id;
};

struct hinic_board_info {
	u32	board_type;
	u32	port_num;
	u32	port_speed;
	u32	pcie_width;
	u32	host_num;
	u32	pf_num;
	u32	vf_total_num;
	u32	tile_num;
	u32	qcm_num;
	u32	core_num;
	u32	work_mode;
	u32	service_mode;
	u32	pcie_mode;
	u32	cfg_addr;
	u32	boot_sel;
	u32	board_id;
};

struct hinic_comm_board_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_board_info info;

	u32	rsvd1[4];
};

struct hinic_hwdev {
	struct hinic_hwif               *hwif;
	struct msix_entry               *msix_entries;

	struct hinic_aeqs               aeqs;
	struct hinic_func_to_io         func_to_io;
	struct hinic_mbox_func_to_func  *func_to_func;

	struct hinic_cap                nic_cap;
	u8				port_id;
	struct hinic_devlink_priv	*devlink_dev;
};

struct hinic_nic_cb {
	void    (*handler)(void *handle, void *buf_in,
			   u16 in_size, void *buf_out,
			   u16 *out_size);

	void            *handle;
	unsigned long   cb_state;
};

#define HINIC_COMM_SELF_CMD_MAX 4

typedef void (*comm_mgmt_self_msg_proc)(void *handle, void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size);

struct comm_mgmt_self_msg_sub_info {
	u8 cmd;
	comm_mgmt_self_msg_proc proc;
};

struct comm_mgmt_self_msg_info {
	u8 cmd_num;
	struct comm_mgmt_self_msg_sub_info info[HINIC_COMM_SELF_CMD_MAX];
};

struct hinic_pfhwdev {
	struct hinic_hwdev              hwdev;

	struct hinic_pf_to_mgmt         pf_to_mgmt;

	struct hinic_nic_cb             nic_cb[HINIC_MGMT_NUM_MSG_CMD];

	struct comm_mgmt_self_msg_info	proc;
};

struct hinic_dev_cap {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u8      rsvd1[5];
	u8      intr_type;
	u8	max_cos_id;
	u8	er_id;
	u8	port_id;
	u8      max_vf;
	u8      rsvd2[62];
	u16     max_sqs;
	u16	max_rqs;
	u16	max_vf_sqs;
	u16     max_vf_rqs;
	u8      rsvd3[204];
};

union hinic_fault_hw_mgmt {
	u32 val[4];
	/* valid only type == FAULT_TYPE_CHIP */
	struct {
		u8 node_id;
		u8 err_level;
		u16 err_type;
		u32 err_csr_addr;
		u32 err_csr_value;
		/* func_id valid only if err_level == FAULT_LEVEL_SERIOUS_FLR */
		u16 func_id;
		u16 rsvd2;
	} chip;

	/* valid only if type == FAULT_TYPE_UCODE */
	struct {
		u8 cause_id;
		u8 core_id;
		u8 c_id;
		u8 rsvd3;
		u32 epc;
		u32 rsvd4;
		u32 rsvd5;
	} ucode;

	/* valid only if type == FAULT_TYPE_MEM_RD_TIMEOUT ||
	 * FAULT_TYPE_MEM_WR_TIMEOUT
	 */
	struct {
		u32 err_csr_ctrl;
		u32 err_csr_data;
		u32 ctrl_tab;
		u32 mem_index;
	} mem_timeout;

	/* valid only if type == FAULT_TYPE_REG_RD_TIMEOUT ||
	 * FAULT_TYPE_REG_WR_TIMEOUT
	 */
	struct {
		u32 err_csr;
		u32 rsvd6;
		u32 rsvd7;
		u32 rsvd8;
	} reg_timeout;

	struct {
		/* 0: read; 1: write */
		u8 op_type;
		u8 port_id;
		u8 dev_ad;
		u8 rsvd9;
		u32 csr_addr;
		u32 op_data;
		u32 rsvd10;
	} phy_fault;
};

struct hinic_fault_event {
	u8 type;
	u8 fault_level;
	u8 rsvd0[2];
	union hinic_fault_hw_mgmt event;
};

struct hinic_cmd_fault_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_fault_event event;
};

enum hinic_fault_type {
	FAULT_TYPE_CHIP,
	FAULT_TYPE_UCODE,
	FAULT_TYPE_MEM_RD_TIMEOUT,
	FAULT_TYPE_MEM_WR_TIMEOUT,
	FAULT_TYPE_REG_RD_TIMEOUT,
	FAULT_TYPE_REG_WR_TIMEOUT,
	FAULT_TYPE_PHY_FAULT,
	FAULT_TYPE_MAX,
};

enum hinic_fault_err_level {
	FAULT_LEVEL_FATAL,
	FAULT_LEVEL_SERIOUS_RESET,
	FAULT_LEVEL_SERIOUS_FLR,
	FAULT_LEVEL_GENERAL,
	FAULT_LEVEL_SUGGESTION,
	FAULT_LEVEL_MAX
};

struct hinic_mgmt_watchdog_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 curr_time_h;
	u32 curr_time_l;
	u32 task_id;
	u32 rsv;

	u32 reg[13];
	u32 pc;
	u32 lr;
	u32 cpsr;

	u32 stack_top;
	u32 stack_bottom;
	u32 sp;
	u32 curr_used;
	u32 peak_used;
	u32 is_overflow;

	u32 stack_actlen;
	u8 data[1024];
};

void hinic_hwdev_cb_register(struct hinic_hwdev *hwdev,
			     enum hinic_mgmt_msg_cmd cmd, void *handle,
			     void (*handler)(void *handle, void *buf_in,
					     u16 in_size, void *buf_out,
					     u16 *out_size));

void hinic_hwdev_cb_unregister(struct hinic_hwdev *hwdev,
			       enum hinic_mgmt_msg_cmd cmd);

int hinic_port_msg_cmd(struct hinic_hwdev *hwdev, enum hinic_port_cmd cmd,
		       void *buf_in, u16 in_size, void *buf_out,
		       u16 *out_size);

int hinic_hilink_msg_cmd(struct hinic_hwdev *hwdev, enum hinic_hilink_cmd cmd,
			 void *buf_in, u16 in_size, void *buf_out,
			 u16 *out_size);

int hinic_hwdev_ifup(struct hinic_hwdev *hwdev, u16 sq_depth, u16 rq_depth);

void hinic_hwdev_ifdown(struct hinic_hwdev *hwdev);

struct hinic_hwdev *hinic_init_hwdev(struct pci_dev *pdev, struct devlink *devlink);

void hinic_free_hwdev(struct hinic_hwdev *hwdev);

int hinic_hwdev_max_num_qps(struct hinic_hwdev *hwdev);

int hinic_hwdev_num_qps(struct hinic_hwdev *hwdev);

struct hinic_sq *hinic_hwdev_get_sq(struct hinic_hwdev *hwdev, int i);

struct hinic_rq *hinic_hwdev_get_rq(struct hinic_hwdev *hwdev, int i);

int hinic_hwdev_msix_cnt_set(struct hinic_hwdev *hwdev, u16 msix_index);

int hinic_hwdev_msix_set(struct hinic_hwdev *hwdev, u16 msix_index,
			 u8 pending_limit, u8 coalesc_timer,
			 u8 lli_timer_cfg, u8 lli_credit_limit,
			 u8 resend_timer);

int hinic_hwdev_hw_ci_addr_set(struct hinic_hwdev *hwdev, struct hinic_sq *sq,
			       u8 pending_limit, u8 coalesc_timer);

void hinic_hwdev_set_msix_state(struct hinic_hwdev *hwdev, u16 msix_index,
				enum hinic_msix_state flag);

int hinic_get_interrupt_cfg(struct hinic_hwdev *hwdev,
			    struct hinic_msix_config *interrupt_info);

int hinic_set_interrupt_cfg(struct hinic_hwdev *hwdev,
			    struct hinic_msix_config *interrupt_info);

int hinic_get_board_info(struct hinic_hwdev *hwdev,
			 struct hinic_comm_board_info *board_info);

#endif
