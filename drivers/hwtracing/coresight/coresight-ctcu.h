/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_CTCU_H
#define _CORESIGHT_CTCU_H
#include "coresight-trace-id.h"

/* Maximum number of supported ETR devices for a single CTCU. */
#define ETR_MAX_NUM	2

/**
 * struct ctcu_etr_config
 * @atid_offset:	offset to the ATID0 Register.
 * @port_num:		in-port number of CTCU device that connected to ETR.
 */
struct ctcu_etr_config {
	const u32 atid_offset;
	const u32 port_num;
};

struct ctcu_config {
	const struct ctcu_etr_config *etr_cfgs;
	int num_etr_config;
};

struct ctcu_drvdata {
	void __iomem		*base;
	struct clk		*apb_clk;
	struct device		*dev;
	struct coresight_device	*csdev;
	raw_spinlock_t		spin_lock;
	u32			atid_offset[ETR_MAX_NUM];
	/* refcnt for each traceid of each sink */
	u8			traceid_refcnt[ETR_MAX_NUM][CORESIGHT_TRACE_ID_RES_TOP];
};

#endif
