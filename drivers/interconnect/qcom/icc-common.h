/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Linaro Ltd.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_COMMON_H__
#define __DRIVERS_INTERCONNECT_QCOM_ICC_COMMON_H__

#include <linux/interconnect-provider.h>

struct icc_node_data *qcom_icc_xlate_extended(struct of_phandle_args *spec, void *data);

#endif
