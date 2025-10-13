/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_EQS_H_
#define _HINIC3_EQS_H_

#include <linux/interrupt.h>

#include "hinic3_hw_cfg.h"
#include "hinic3_queue_common.h"

#define HINIC3_MAX_AEQS              4
#define HINIC3_MAX_CEQS              32

#define HINIC3_AEQ_MAX_PAGES         4
#define HINIC3_CEQ_MAX_PAGES         8

#define HINIC3_AEQE_SIZE             64
#define HINIC3_CEQE_SIZE             4

#define HINIC3_AEQE_DESC_SIZE        4
#define HINIC3_AEQE_DATA_SIZE        (HINIC3_AEQE_SIZE - HINIC3_AEQE_DESC_SIZE)

#define HINIC3_DEFAULT_AEQ_LEN       0x10000
#define HINIC3_DEFAULT_CEQ_LEN       0x10000

#define HINIC3_EQ_IRQ_NAME_LEN       64

#define HINIC3_EQ_USLEEP_LOW_BOUND   900
#define HINIC3_EQ_USLEEP_HIGH_BOUND  1000

enum hinic3_eq_type {
	HINIC3_AEQ = 0,
	HINIC3_CEQ = 1,
};

enum hinic3_eq_intr_mode {
	HINIC3_INTR_MODE_ARMED  = 0,
	HINIC3_INTR_MODE_ALWAYS = 1,
};

enum hinic3_eq_ci_arm_state {
	HINIC3_EQ_NOT_ARMED = 0,
	HINIC3_EQ_ARMED     = 1,
};

struct hinic3_eq {
	struct hinic3_hwdev       *hwdev;
	struct hinic3_queue_pages qpages;
	u16                       q_id;
	enum hinic3_eq_type       type;
	u32                       eq_len;
	u32                       cons_idx;
	u8                        wrapped;
	u32                       irq_id;
	u16                       msix_entry_idx;
	char                      irq_name[HINIC3_EQ_IRQ_NAME_LEN];
	struct work_struct        aeq_work;
};

struct hinic3_aeq_elem {
	u8     aeqe_data[HINIC3_AEQE_DATA_SIZE];
	__be32 desc;
};

enum hinic3_aeq_type {
	HINIC3_HW_INTER_INT   = 0,
	HINIC3_MBX_FROM_FUNC  = 1,
	HINIC3_MSG_FROM_FW    = 2,
	HINIC3_MAX_AEQ_EVENTS = 6,
};

typedef void (*hinic3_aeq_event_cb)(struct hinic3_hwdev *hwdev, u8 *data,
				    u8 size);

struct hinic3_aeqs {
	struct hinic3_hwdev     *hwdev;
	hinic3_aeq_event_cb     aeq_cb[HINIC3_MAX_AEQ_EVENTS];
	struct hinic3_eq        aeq[HINIC3_MAX_AEQS];
	u16                     num_aeqs;
	struct workqueue_struct *workq;
	/* lock for aeq event flag */
	spinlock_t              aeq_lock;
};

enum hinic3_ceq_event {
	HINIC3_CMDQ           = 3,
	HINIC3_MAX_CEQ_EVENTS = 6,
};

typedef void (*hinic3_ceq_event_cb)(struct hinic3_hwdev *hwdev,
				    __le32 ceqe_data);

struct hinic3_ceqs {
	struct hinic3_hwdev *hwdev;

	hinic3_ceq_event_cb ceq_cb[HINIC3_MAX_CEQ_EVENTS];

	struct hinic3_eq    ceq[HINIC3_MAX_CEQS];
	u16                 num_ceqs;
	/* lock for ceq event flag */
	spinlock_t          ceq_lock;
};

int hinic3_aeqs_init(struct hinic3_hwdev *hwdev, u16 num_aeqs,
		     struct msix_entry *msix_entries);
void hinic3_aeqs_free(struct hinic3_hwdev *hwdev);
int hinic3_aeq_register_cb(struct hinic3_hwdev *hwdev,
			   enum hinic3_aeq_type event,
			   hinic3_aeq_event_cb hwe_cb);
void hinic3_aeq_unregister_cb(struct hinic3_hwdev *hwdev,
			      enum hinic3_aeq_type event);
int hinic3_ceqs_init(struct hinic3_hwdev *hwdev, u16 num_ceqs,
		     struct msix_entry *msix_entries);
void hinic3_ceqs_free(struct hinic3_hwdev *hwdev);
int hinic3_ceq_register_cb(struct hinic3_hwdev *hwdev,
			   enum hinic3_ceq_event event,
			   hinic3_ceq_event_cb callback);
void hinic3_ceq_unregister_cb(struct hinic3_hwdev *hwdev,
			      enum hinic3_ceq_event event);

#endif
