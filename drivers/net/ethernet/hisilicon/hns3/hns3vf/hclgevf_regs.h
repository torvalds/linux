/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2023 Hisilicon Limited. */

#ifndef __HCLGEVF_REGS_H
#define __HCLGEVF_REGS_H
#include <linux/types.h>

struct hnae3_handle;

int hclgevf_get_regs_len(struct hnae3_handle *handle);
void hclgevf_get_regs(struct hnae3_handle *handle, u32 *version,
		      void *data);
#endif
