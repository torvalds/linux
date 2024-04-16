/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_HW_MGMT_H
#define HINIC_HW_MGMT_H

#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/bitops.h>

#include "hinic_hw_if.h"
#include "hinic_hw_api_cmd.h"

#define HINIC_MSG_HEADER_MSG_LEN_SHIFT                          0
#define HINIC_MSG_HEADER_MODULE_SHIFT                           11
#define HINIC_MSG_HEADER_SEG_LEN_SHIFT                          16
#define HINIC_MSG_HEADER_NO_ACK_SHIFT                           22
#define HINIC_MSG_HEADER_ASYNC_MGMT_TO_PF_SHIFT                 23
#define HINIC_MSG_HEADER_SEQID_SHIFT                            24
#define HINIC_MSG_HEADER_LAST_SHIFT                             30
#define HINIC_MSG_HEADER_DIRECTION_SHIFT                        31
#define HINIC_MSG_HEADER_CMD_SHIFT                              32
#define HINIC_MSG_HEADER_ZEROS_SHIFT                            40
#define HINIC_MSG_HEADER_PCI_INTF_SHIFT                         48
#define HINIC_MSG_HEADER_PF_IDX_SHIFT                           50
#define HINIC_MSG_HEADER_MSG_ID_SHIFT                           54

#define HINIC_MSG_HEADER_MSG_LEN_MASK                           0x7FF
#define HINIC_MSG_HEADER_MODULE_MASK                            0x1F
#define HINIC_MSG_HEADER_SEG_LEN_MASK                           0x3F
#define HINIC_MSG_HEADER_NO_ACK_MASK                            0x1
#define HINIC_MSG_HEADER_ASYNC_MGMT_TO_PF_MASK                  0x1
#define HINIC_MSG_HEADER_SEQID_MASK                             0x3F
#define HINIC_MSG_HEADER_LAST_MASK                              0x1
#define HINIC_MSG_HEADER_DIRECTION_MASK                         0x1
#define HINIC_MSG_HEADER_CMD_MASK                               0xFF
#define HINIC_MSG_HEADER_ZEROS_MASK                             0xFF
#define HINIC_MSG_HEADER_PCI_INTF_MASK                          0x3
#define HINIC_MSG_HEADER_PF_IDX_MASK                            0xF
#define HINIC_MSG_HEADER_MSG_ID_MASK                            0x3FF

#define HINIC_MSG_HEADER_SET(val, member)                       \
		((u64)((val) & HINIC_MSG_HEADER_##member##_MASK) << \
		 HINIC_MSG_HEADER_##member##_SHIFT)

#define HINIC_MSG_HEADER_GET(val, member)                       \
		(((val) >> HINIC_MSG_HEADER_##member##_SHIFT) & \
		 HINIC_MSG_HEADER_##member##_MASK)

enum hinic_mgmt_msg_type {
	HINIC_MGMT_MSG_SYNC = 1,
};

enum hinic_cfg_cmd {
	HINIC_CFG_NIC_CAP = 0,
};

enum hinic_comm_cmd {
	HINIC_COMM_CMD_START_FLR          = 0x1,
	HINIC_COMM_CMD_IO_STATUS_GET    = 0x3,
	HINIC_COMM_CMD_DMA_ATTR_SET	    = 0x4,

	HINIC_COMM_CMD_CMDQ_CTXT_SET    = 0x10,
	HINIC_COMM_CMD_CMDQ_CTXT_GET    = 0x11,

	HINIC_COMM_CMD_HWCTXT_SET       = 0x12,
	HINIC_COMM_CMD_HWCTXT_GET       = 0x13,

	HINIC_COMM_CMD_SQ_HI_CI_SET     = 0x14,

	HINIC_COMM_CMD_RES_STATE_SET    = 0x24,

	HINIC_COMM_CMD_IO_RES_CLEAR     = 0x29,

	HINIC_COMM_CMD_CEQ_CTRL_REG_WR_BY_UP = 0x33,

	HINIC_COMM_CMD_MSI_CTRL_REG_WR_BY_UP,
	HINIC_COMM_CMD_MSI_CTRL_REG_RD_BY_UP,

	HINIC_COMM_CMD_FAULT_REPORT	= 0x37,

	HINIC_COMM_CMD_SET_LED_STATUS	= 0x4a,

	HINIC_COMM_CMD_L2NIC_RESET	= 0x4b,

	HINIC_COMM_CMD_PAGESIZE_SET	= 0x50,

	HINIC_COMM_CMD_GET_BOARD_INFO	= 0x52,

	HINIC_COMM_CMD_WATCHDOG_INFO	= 0x56,

	HINIC_MGMT_CMD_SET_VF_RANDOM_ID = 0x61,

	HINIC_COMM_CMD_MAX,
};

enum hinic_mgmt_cb_state {
	HINIC_MGMT_CB_ENABLED = BIT(0),
	HINIC_MGMT_CB_RUNNING = BIT(1),
};

struct hinic_recv_msg {
	u8                      *msg;
	u8                      *buf_out;

	struct completion       recv_done;

	u16                     cmd;
	enum hinic_mod_type     mod;
	int                     async_mgmt_to_pf;

	u16                     msg_len;
	u16                     msg_id;
};

struct hinic_mgmt_cb {
	void    (*cb)(void *handle, u8 cmd,
		      void *buf_in, u16 in_size,
		      void *buf_out, u16 *out_size);

	void            *handle;
	unsigned long   state;
};

struct hinic_pf_to_mgmt {
	struct hinic_hwif               *hwif;
	struct hinic_hwdev		*hwdev;
	struct semaphore                sync_msg_lock;
	u16                             sync_msg_id;
	u8                              *sync_msg_buf;
	void				*mgmt_ack_buf;

	struct hinic_recv_msg           recv_resp_msg_from_mgmt;
	struct hinic_recv_msg           recv_msg_from_mgmt;

	struct hinic_api_cmd_chain      *cmd_chain[HINIC_API_CMD_MAX];

	struct hinic_mgmt_cb            mgmt_cb[HINIC_MOD_MAX];

	struct workqueue_struct		*workq;
};

struct hinic_mgmt_msg_handle_work {
	struct work_struct work;
	struct hinic_pf_to_mgmt *pf_to_mgmt;

	void			*msg;
	u16			msg_len;

	enum hinic_mod_type	mod;
	u8			cmd;
	u16			msg_id;
	int			async_mgmt_to_pf;
};

void hinic_register_mgmt_msg_cb(struct hinic_pf_to_mgmt *pf_to_mgmt,
				enum hinic_mod_type mod,
				void *handle,
				void (*callback)(void *handle,
						 u8 cmd, void *buf_in,
						 u16 in_size, void *buf_out,
						 u16 *out_size));

void hinic_unregister_mgmt_msg_cb(struct hinic_pf_to_mgmt *pf_to_mgmt,
				  enum hinic_mod_type mod);

int hinic_msg_to_mgmt(struct hinic_pf_to_mgmt *pf_to_mgmt,
		      enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size, void *buf_out, u16 *out_size,
		      enum hinic_mgmt_msg_type sync);

int hinic_pf_to_mgmt_init(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  struct hinic_hwif *hwif);

void hinic_pf_to_mgmt_free(struct hinic_pf_to_mgmt *pf_to_mgmt);

#endif
