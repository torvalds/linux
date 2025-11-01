/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - 2025 Intel Corporation
 */

#ifndef IPU7_FW_INSYS_CONFIG_ABI_H
#define IPU7_FW_INSYS_CONFIG_ABI_H

#include "ipu7_fw_boot_abi.h"
#include "ipu7_fw_config_abi.h"
#include "ipu7_fw_isys_abi.h"

struct ipu7_insys_config {
	u32 timeout_val_ms;
	struct ia_gofo_logger_config logger_config;
	struct ipu7_wdt_abi wdt_config;
};

#endif
