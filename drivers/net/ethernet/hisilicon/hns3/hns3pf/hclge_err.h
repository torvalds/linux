/* SPDX-License-Identifier: GPL-2.0+ */
/*  Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGE_ERR_H
#define __HCLGE_ERR_H

#include "hclge_main.h"

#define HCLGE_RAS_PF_OTHER_INT_STS_REG   0x20B00
#define HCLGE_RAS_REG_FE_MASK    0xFF
#define HCLGE_RAS_REG_NFE_MASK   0xFF00
#define HCLGE_RAS_REG_NFE_SHIFT	8

enum hclge_err_int_type {
	HCLGE_ERR_INT_MSIX = 0,
	HCLGE_ERR_INT_RAS_CE = 1,
	HCLGE_ERR_INT_RAS_NFE = 2,
	HCLGE_ERR_INT_RAS_FE = 3,
};

struct hclge_hw_blk {
	u32 msk;
	const char *name;
	int (*enable_error)(struct hclge_dev *hdev, bool en);
	void (*process_error)(struct hclge_dev *hdev,
			      enum hclge_err_int_type type);
};

int hclge_hw_error_set_state(struct hclge_dev *hdev, bool state);
pci_ers_result_t hclge_process_ras_hw_error(struct hnae3_ae_dev *ae_dev);
#endif
