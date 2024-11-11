/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>

#ifndef _QCOM_BUS_PROF_H
#define _QCOM_BUS_PROF_H
#define MAX_CONCURRENT_MASTERS	2
struct llcc_miss_buf {
	u8		master_id;
	uint16_t	miss_info;
	u32		rd_miss;
	u32		wr_miss;
	u32		all_access;
} __packed;

#endif /* _QCOM_BUS_PROF_H */
