/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

struct hfi1_devdata;

int eprom_init(struct hfi1_devdata *dd);
int eprom_read_platform_config(struct hfi1_devdata *dd, void **buf_ret,
			       u32 *size_ret);
