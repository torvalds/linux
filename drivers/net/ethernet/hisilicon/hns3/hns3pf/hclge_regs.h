/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2023 Hisilicon Limited.

#ifndef __HCLGE_REGS_H
#define __HCLGE_REGS_H
#include <linux/types.h>
#include "hclge_comm_cmd.h"

struct hnae3_handle;
struct hclge_dev;

int hclge_query_bd_num_cmd_send(struct hclge_dev *hdev,
				struct hclge_desc *desc);
int hclge_get_regs_len(struct hnae3_handle *handle);
void hclge_get_regs(struct hnae3_handle *handle, u32 *version,
		    void *data);
#endif
