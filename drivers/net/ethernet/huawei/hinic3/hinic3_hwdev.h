/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_HWDEV_H_
#define _HINIC3_HWDEV_H_

#include <linux/auxiliary_bus.h>
#include <linux/pci.h>

#include "hinic3_hw_intf.h"

struct hinic3_cmdqs;
struct hinic3_hwif;

enum hinic3_event_service_type {
	HINIC3_EVENT_SRV_COMM = 0,
	HINIC3_EVENT_SRV_NIC  = 1
};

enum hinic3_comm_event_type {
	HINIC3_COMM_EVENT_PCIE_LINK_DOWN = 0,
	HINIC3_COMM_EVENT_HEART_LOST = 1,
	HINIC3_COMM_EVENT_FAULT = 2,
	HINIC3_COMM_EVENT_SRIOV_STATE_CHANGE = 3,
	HINIC3_COMM_EVENT_CARD_REMOVE = 4,
	HINIC3_COMM_EVENT_MGMT_WATCHDOG = 5,
};

enum hinic3_fault_err_level {
	HINIC3_FAULT_LEVEL_SERIOUS_FLR = 3,
};

enum hinic3_fault_source_type {
	HINIC3_FAULT_SRC_HW_PHY_FAULT = 9,
	HINIC3_FAULT_SRC_TX_TIMEOUT   = 22,
};

#define HINIC3_SRV_EVENT_TYPE(svc, type)    (((svc) << 16) | (type))

/* driver-specific data of pci_dev */
struct hinic3_pcidev {
	struct pci_dev       *pdev;
	struct hinic3_hwdev  *hwdev;
	/* Auxiliary devices */
	struct hinic3_adev   *hadev[HINIC3_SERVICE_T_MAX];

	void __iomem         *cfg_reg_base;
	void __iomem         *intr_reg_base;
	void __iomem         *mgmt_reg_base;
	void __iomem         *db_base;
	u64                  db_dwqe_len;
	u64                  db_base_phy;

	/* lock for attach/detach uld */
	struct mutex         pdev_mutex;
	unsigned long        state;
};

struct hinic3_hwdev {
	struct hinic3_pcidev        *adapter;
	struct pci_dev              *pdev;
	struct device               *dev;
	int                         dev_id;
	struct hinic3_hwif          *hwif;
	struct hinic3_cfg_mgmt_info *cfg_mgmt;
	struct hinic3_aeqs          *aeqs;
	struct hinic3_ceqs          *ceqs;
	struct hinic3_mbox          *mbox;
	struct hinic3_cmdqs         *cmdqs;
	struct delayed_work         sync_time_task;
	struct workqueue_struct     *workq;
	struct hinic3_msg_pf_to_mgmt *pf_to_mgmt;
	/* protect channel init and uninit */
	spinlock_t                  channel_lock;
	u64                         features[COMM_MAX_FEATURE_QWORD];
	u32                         wq_page_size;
	u8                          max_cmdq;
	ulong                       func_state;
};

struct hinic3_event_info {
	/* enum hinic3_event_service_type */
	u16 service;
	u16 type;
	u8  event_data[104];
};

struct hinic3_adev {
	struct auxiliary_device  adev;
	struct hinic3_hwdev      *hwdev;
	enum hinic3_service_type svc_type;

	void (*event)(struct auxiliary_device *adev,
		      struct hinic3_event_info *event);
};

int hinic3_init_hwdev(struct pci_dev *pdev);
void hinic3_free_hwdev(struct hinic3_hwdev *hwdev);

void hinic3_set_api_stop(struct hinic3_hwdev *hwdev);

#endif
