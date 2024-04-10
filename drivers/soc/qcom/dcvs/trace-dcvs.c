// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <soc/qcom/dcvs.h>

#define CREATE_TRACE_POINTS
#include "trace-dcvs.h"

EXPORT_TRACEPOINT_SYMBOL(qcom_dcvs_update);
EXPORT_TRACEPOINT_SYMBOL(qcom_dcvs_boost);
EXPORT_TRACEPOINT_SYMBOL(memlat_dev_meas);
EXPORT_TRACEPOINT_SYMBOL(memlat_dev_update);
EXPORT_TRACEPOINT_SYMBOL(bw_hwmon_meas);
EXPORT_TRACEPOINT_SYMBOL(bw_hwmon_update);
EXPORT_TRACEPOINT_SYMBOL(bw_hwmon_debug);
EXPORT_TRACEPOINT_SYMBOL(bwprof_last_sample);
