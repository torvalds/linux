/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_SMD_RPM_H
#define __DRIVERS_INTERCONNECT_QCOM_SMD_RPM_H

#include <linux/soc/qcom/smd-rpm.h>

bool qcom_icc_rpm_smd_available(void);
int qcom_icc_rpm_smd_send(int ctx, int rsc_type, int id, u32 val);

#endif
