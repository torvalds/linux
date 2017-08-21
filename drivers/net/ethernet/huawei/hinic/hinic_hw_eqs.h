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

#define HINIC_AEQ_CTRL_0_INT_IDX_SHIFT          0
#define HINIC_AEQ_CTRL_0_DMA_ATTR_SHIFT         12
#define HINIC_AEQ_CTRL_0_PCI_INTF_IDX_SHIFT     20
#define HINIC_AEQ_CTRL_0_INT_MODE_SHIFT         31

#define HINIC_AEQ_CTRL_0_INT_IDX_MASK           0x3FF
#define HINIC_AEQ_CTRL_0_DMA_ATTR_MASK          0x3F
#define HINIC_AEQ_CTRL_0_PCI_INTF_IDX_MASK      0x3
#define HINIC_AEQ_CTRL_0_INT_MODE_MASK          0x1

#define HINIC_AEQ_CTRL_0_SET(val, member)       \
			(((u32)(val) & HINIC_AEQ_CTRL_0_##member##_MASK) << \
			 HINIC_AEQ_CTRL_0_##member##_SHIFT)

#define HINIC_AEQ_CTRL_0_CLEAR(val, member)     \
			((val) & (~(HINIC_AEQ_CTRL_0_##member##_MASK \
			 << HINIC_AEQ_CTRL_0_##member##_SHIFT)))

#define HINIC_AEQ_CTRL_1_LEN_SHIFT              0
#define HINIC_AEQ_CTRL_1_ELEM_SIZE_SHIFT        24
#define HINIC_AEQ_CTRL_1_PAGE_SIZE_SHIFT        28

#define HINIC_AEQ_CTRL_1_LEN_MASK               0x1FFFFF
#define HINIC_AEQ_CTRL_1_ELEM_SIZE_MASK         0x3
#define HINIC_AEQ_CTRL_1_PAGE_SIZE_MASK         0xF

#define HINIC_AEQ_CTRL_1_SET(val, member)       \
			(((u32)(val) & HINIC_AEQ_CTRL_1_##member##_MASK) << \
			 HINIC_AEQ_CTRL_1_##member##_SHIFT)

#define HINIC_AEQ_CTRL_1_CLEAR(val, member)     \
			((val) & (~(HINIC_AEQ_CTRL_1_##member##_MASK \
			 << HINIC_AEQ_CTRL_1_##member##_SHIFT)))

#define HINIC_EQ_ELEM_DESC_TYPE_SHIFT           0
#define HINIC_EQ_ELEM_DESC_SRC_SHIFT            7
#define HINIC_EQ_ELEM_DESC_SIZE_SHIFT           8
#define HINIC_EQ_ELEM_DESC_WRAPPED_SHIFT        31

#define HINIC_EQ_ELEM_DESC_TYPE_MASK            0x7F
#define HINIC_EQ_ELEM_DESC_SRC_MASK             0x1
#define HINIC_EQ_ELEM_DESC_SIZE_MASK            0xFF
#define HINIC_EQ_ELEM_DESC_WRAPPED_MASK         0x1

#define HINIC_EQ_ELEM_DESC_SET(val, member)     \
			(((u32)(val) & HINIC_EQ_ELEM_DESC_##member##_MASK) << \
			 HINIC_EQ_ELEM_DESC_##member##_SHIFT)

#define HINIC_EQ_ELEM_DESC_GET(val, member)     \
			(((val) >> HINIC_EQ_ELEM_DESC_##member##_SHIFT) & \
			 HINIC_EQ_ELEM_DESC_##member##_MASK)

#define HINIC_EQ_CI_IDX_SHIFT                   0
#define HINIC_EQ_CI_WRAPPED_SHIFT               20
#define HINIC_EQ_CI_XOR_CHKSUM_SHIFT            24
#define HINIC_EQ_CI_INT_ARMED_SHIFT             31

#define HINIC_EQ_CI_IDX_MASK                    0xFFFFF
#define HINIC_EQ_CI_WRAPPED_MASK                0x1
#define HINIC_EQ_CI_XOR_CHKSUM_MASK             0xF
#define HINIC_EQ_CI_INT_ARMED_MASK              0x1

#define HINIC_EQ_CI_SET(val, member)            \
			(((u32)(val) & HINIC_EQ_CI_##member##_MASK) << \
			 HINIC_EQ_CI_##member##_SHIFT)

#define HINIC_EQ_CI_CLEAR(val, member)          \
			((val) & (~(HINIC_EQ_CI_##member##_MASK \
			 << HINIC_EQ_CI_##member##_SHIFT)))

#define HINIC_MAX_AEQS                  4

#define HINIC_AEQE_SIZE                 64

#define HINIC_AEQE_DESC_SIZE            4
#define HINIC_AEQE_DATA_SIZE            \
			(HINIC_AEQE_SIZE - HINIC_AEQE_DESC_SIZE)

#define HINIC_DEFAULT_AEQ_LEN           64

#define HINIC_EQ_PAGE_SIZE              SZ_4K

#define HINIC_CEQ_ID_CMDQ               0

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

struct hinic_aeq_elem {
	u8      data[HINIC_AEQE_DATA_SIZE];
	u32     desc;
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
