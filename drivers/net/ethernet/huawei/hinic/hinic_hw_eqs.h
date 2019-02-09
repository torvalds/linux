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
#include <linux/interrupt.h>

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

#define HINIC_CEQ_CTRL_0_INTR_IDX_SHIFT         0
#define HINIC_CEQ_CTRL_0_DMA_ATTR_SHIFT         12
#define HINIC_CEQ_CTRL_0_KICK_THRESH_SHIFT      20
#define HINIC_CEQ_CTRL_0_PCI_INTF_IDX_SHIFT     24
#define HINIC_CEQ_CTRL_0_INTR_MODE_SHIFT        31

#define HINIC_CEQ_CTRL_0_INTR_IDX_MASK          0x3FF
#define HINIC_CEQ_CTRL_0_DMA_ATTR_MASK          0x3F
#define HINIC_CEQ_CTRL_0_KICK_THRESH_MASK       0xF
#define HINIC_CEQ_CTRL_0_PCI_INTF_IDX_MASK      0x3
#define HINIC_CEQ_CTRL_0_INTR_MODE_MASK         0x1

#define HINIC_CEQ_CTRL_0_SET(val, member)       \
			(((u32)(val) & HINIC_CEQ_CTRL_0_##member##_MASK) << \
			 HINIC_CEQ_CTRL_0_##member##_SHIFT)

#define HINIC_CEQ_CTRL_0_CLEAR(val, member)     \
			((val) & (~(HINIC_CEQ_CTRL_0_##member##_MASK \
			 << HINIC_CEQ_CTRL_0_##member##_SHIFT)))

#define HINIC_CEQ_CTRL_1_LEN_SHIFT              0
#define HINIC_CEQ_CTRL_1_PAGE_SIZE_SHIFT        28

#define HINIC_CEQ_CTRL_1_LEN_MASK               0x1FFFFF
#define HINIC_CEQ_CTRL_1_PAGE_SIZE_MASK         0xF

#define HINIC_CEQ_CTRL_1_SET(val, member)       \
			(((u32)(val) & HINIC_CEQ_CTRL_1_##member##_MASK) << \
			 HINIC_CEQ_CTRL_1_##member##_SHIFT)

#define HINIC_CEQ_CTRL_1_CLEAR(val, member)     \
			((val) & (~(HINIC_CEQ_CTRL_1_##member##_MASK \
			 << HINIC_CEQ_CTRL_1_##member##_SHIFT)))

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
#define HINIC_MAX_CEQS                  32

#define HINIC_AEQE_SIZE                 64
#define HINIC_CEQE_SIZE                 4

#define HINIC_AEQE_DESC_SIZE            4
#define HINIC_AEQE_DATA_SIZE            \
			(HINIC_AEQE_SIZE - HINIC_AEQE_DESC_SIZE)

#define HINIC_DEFAULT_AEQ_LEN           64
#define HINIC_DEFAULT_CEQ_LEN           1024

#define HINIC_EQ_PAGE_SIZE              SZ_4K

#define HINIC_CEQ_ID_CMDQ               0

enum hinic_eq_type {
	HINIC_AEQ,
	HINIC_CEQ,
};

enum hinic_aeq_type {
	HINIC_MSG_FROM_MGMT_CPU = 2,

	HINIC_MAX_AEQ_EVENTS,
};

enum hinic_ceq_type {
	HINIC_CEQ_CMDQ = 3,

	HINIC_MAX_CEQ_EVENTS,
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

	struct tasklet_struct   ceq_tasklet;
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

struct hinic_ceq_cb {
	void    (*handler)(void *handle, u32 ceqe_data);
	void                    *handle;
	enum hinic_eqe_state    ceqe_state;
};

struct hinic_ceqs {
	struct hinic_hwif       *hwif;

	struct hinic_eq         ceq[HINIC_MAX_CEQS];
	int                     num_ceqs;

	struct hinic_ceq_cb     ceq_cb[HINIC_MAX_CEQ_EVENTS];
};

void hinic_aeq_register_hw_cb(struct hinic_aeqs *aeqs,
			      enum hinic_aeq_type event, void *handle,
			      void (*hwe_handler)(void *handle, void *data,
						  u8 size));

void hinic_aeq_unregister_hw_cb(struct hinic_aeqs *aeqs,
				enum hinic_aeq_type event);

void hinic_ceq_register_cb(struct hinic_ceqs *ceqs,
			   enum hinic_ceq_type event, void *handle,
			   void (*ceq_cb)(void *handle, u32 ceqe_data));

void hinic_ceq_unregister_cb(struct hinic_ceqs *ceqs,
			     enum hinic_ceq_type event);

int hinic_aeqs_init(struct hinic_aeqs *aeqs, struct hinic_hwif *hwif,
		    int num_aeqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries);

void hinic_aeqs_free(struct hinic_aeqs *aeqs);

int hinic_ceqs_init(struct hinic_ceqs *ceqs, struct hinic_hwif *hwif,
		    int num_ceqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries);

void hinic_ceqs_free(struct hinic_ceqs *ceqs);

#endif
