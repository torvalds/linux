/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_MBOX_H_
#define HINIC_MBOX_H_

#define HINIC_MBOX_PF_SEND_ERR		0x1
#define HINIC_MBOX_PF_BUSY_ACTIVE_FW	0x2
#define HINIC_MBOX_VF_CMD_ERROR		0x3

#define HINIC_MAX_FUNCTIONS		512

#define HINIC_MAX_PF_FUNCS		16

#define HINIC_MBOX_WQ_NAME		"hinic_mbox"

#define HINIC_FUNC_CSR_MAILBOX_DATA_OFF			0x80
#define HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF		0x0100
#define HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF		0x0104
#define HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF		0x0108
#define HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF		0x010C

#define MAX_FUNCTION_NUM		512

struct vf_cmd_check_handle {
	u8 cmd;
	bool (*check_cmd)(struct hinic_hwdev *hwdev, u16 src_func_idx,
			  void *buf_in, u16 in_size);
};

enum hinic_mbox_ack_type {
	MBOX_ACK,
	MBOX_NO_ACK,
};

struct mbox_msg_info {
	u8 msg_id;
	u8 status;
};

struct hinic_recv_mbox {
	struct completion	recv_done;
	void			*mbox;
	u8			cmd;
	enum hinic_mod_type	mod;
	u16			mbox_len;
	void			*buf_out;
	enum hinic_mbox_ack_type ack_type;
	struct mbox_msg_info	msg_info;
	u8			seq_id;
	atomic_t		msg_cnt;
};

struct hinic_send_mbox {
	struct completion	send_done;
	u8			*data;

	u64			*wb_status;
	void			*wb_vaddr;
	dma_addr_t		wb_paddr;
};

typedef void (*hinic_vf_mbox_cb)(void *handle, u8 cmd, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size);
typedef int (*hinic_pf_mbox_cb)(void *handle, u16 vf_id, u8 cmd, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size);

enum mbox_event_state {
	EVENT_START = 0,
	EVENT_FAIL,
	EVENT_TIMEOUT,
	EVENT_END,
};

enum hinic_mbox_cb_state {
	HINIC_VF_MBOX_CB_REG = 0,
	HINIC_VF_MBOX_CB_RUNNING,
	HINIC_PF_MBOX_CB_REG,
	HINIC_PF_MBOX_CB_RUNNING,
	HINIC_PPF_MBOX_CB_REG,
	HINIC_PPF_MBOX_CB_RUNNING,
	HINIC_PPF_TO_PF_MBOX_CB_REG,
	HINIC_PPF_TO_PF_MBOX_CB_RUNNIG,
};

struct hinic_mbox_func_to_func {
	struct hinic_hwdev	*hwdev;
	struct hinic_hwif		*hwif;

	struct semaphore	mbox_send_sem;
	struct semaphore	msg_send_sem;
	struct hinic_send_mbox	send_mbox;

	struct workqueue_struct *workq;

	struct hinic_recv_mbox	mbox_resp[HINIC_MAX_FUNCTIONS];
	struct hinic_recv_mbox	mbox_send[HINIC_MAX_FUNCTIONS];

	hinic_vf_mbox_cb	vf_mbox_cb[HINIC_MOD_MAX];
	hinic_pf_mbox_cb	pf_mbox_cb[HINIC_MOD_MAX];
	unsigned long		pf_mbox_cb_state[HINIC_MOD_MAX];
	unsigned long		vf_mbox_cb_state[HINIC_MOD_MAX];

	u8 send_msg_id;
	enum mbox_event_state event_flag;

	/* lock for mbox event flag */
	spinlock_t mbox_lock;

	u32 vf_mbx_old_rand_id[MAX_FUNCTION_NUM];
	u32 vf_mbx_rand_id[MAX_FUNCTION_NUM];
	bool support_vf_random;
};

struct hinic_mbox_work {
	struct work_struct work;
	u16 src_func_idx;
	struct hinic_mbox_func_to_func *func_to_func;
	struct hinic_recv_mbox *recv_mbox;
};

struct vf_cmd_msg_handle {
	u8 cmd;
	int (*cmd_msg_handler)(void *hwdev, u16 vf_id,
			       void *buf_in, u16 in_size,
			       void *buf_out, u16 *out_size);
};

bool hinic_mbox_check_func_id_8B(struct hinic_hwdev *hwdev, u16 func_idx,
				 void *buf_in, u16 in_size);

bool hinic_mbox_check_cmd_valid(struct hinic_hwdev *hwdev,
				struct vf_cmd_check_handle *cmd_handle,
				u16 vf_id, u8 cmd, void *buf_in,
				u16 in_size, u8 size);

int hinic_register_pf_mbox_cb(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod,
			      hinic_pf_mbox_cb callback);

int hinic_register_vf_mbox_cb(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod,
			      hinic_vf_mbox_cb callback);

void hinic_unregister_pf_mbox_cb(struct hinic_hwdev *hwdev,
				 enum hinic_mod_type mod);

void hinic_unregister_vf_mbox_cb(struct hinic_hwdev *hwdev,
				 enum hinic_mod_type mod);

int hinic_func_to_func_init(struct hinic_hwdev *hwdev);

void hinic_func_to_func_free(struct hinic_hwdev *hwdev);

int hinic_mbox_to_pf(struct hinic_hwdev *hwdev, enum hinic_mod_type mod,
		     u8 cmd, void *buf_in, u16 in_size, void *buf_out,
		     u16 *out_size, u32 timeout);

int hinic_mbox_to_func(struct hinic_mbox_func_to_func *func_to_func,
		       enum hinic_mod_type mod, u16 cmd, u16 dst_func,
		       void *buf_in, u16 in_size, void *buf_out,
		       u16 *out_size, u32 timeout);

int hinic_mbox_to_vf(struct hinic_hwdev *hwdev,
		     enum hinic_mod_type mod, u16 vf_id, u8 cmd, void *buf_in,
		     u16 in_size, void *buf_out, u16 *out_size, u32 timeout);

int hinic_vf_mbox_random_id_init(struct hinic_hwdev *hwdev);

#endif
