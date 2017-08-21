/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_EQS_H
#define HINIC_HW_EQS_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/bitops.h>

#include "hinic_hw_if.h"

#define HINIC_MAX_AEQS                  4

#define HINIC_DEFAULT_AEQ_LEN           64

#define HINIC_EQ_PAGE_SIZE              SZ_4K

enum hinic_eq_type {
	HINIC_AEQ,
};

enum hinic_aeq_type {
	HINIC_MSG_FROM_MGMT_CPU = 2,

	HINIC_MAX_AEQ_EVENTS,
};

enum hinic_eqe_state {
	HINIC_EQE_ENABLED = BIT(0),
	HINIC_EQE_RUNNING = BIT(1),
};

struct hinic_eq_work {
	struct work_struct      work;
	void                    *data;
};

struct hinic_eq {
	struct hinic_hwif       *hwif;

	enum hinic_eq_type      type;
	int                     q_id;
	u32                     q_len;
	u32                     page_size;

	u32                     cons_idx;
	int                     wrapped;

	size_t                  elem_size;
	int                     num_pages;
	int                     num_elem_in_pg;

	struct msix_entry       msix_entry;

	dma_addr_t              *dma_addr;
	void                    **virt_addr;

	struct hinic_eq_work    aeq_work;
};

struct hinic_hw_event_cb {
	void    (*hwe_handler)(void *handle, void *data, u8 size);
	void                    *handle;
	unsigned long           hwe_state;
};

struct hinic_aeqs {
	struct hinic_hwif       *hwif;

	struct hinic_eq         aeq[HINIC_MAX_AEQS];
	int                     num_aeqs;

	struct hinic_hw_event_cb hwe_cb[HINIC_MAX_AEQ_EVENTS];

	struct workqueue_struct *workq;
};

void hinic_aeq_register_hw_cb(struct hinic_aeqs *aeqs,
			      enum hinic_aeq_type event, void *handle,
			      void (*hwe_handler)(void *handle, void *data,
						  u8 size));

void hinic_aeq_unregister_hw_cb(struct hinic_aeqs *aeqs,
				enum hinic_aeq_type event);

int hinic_aeqs_init(struct hinic_aeqs *aeqs, struct hinic_hwif *hwif,
		    int num_aeqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries);

void hinic_aeqs_free(struct hinic_aeqs *aeqs);

#endif
