/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2022 Intel Corporation */
#ifndef ADF_GEN4_PM_H
#define ADF_GEN4_PM_H

#include "adf_accel_devices.h"

/* Power management registers */
#define ADF_GEN4_PM_HOST_MSG (0x50A01C)

/* Power management */
#define ADF_GEN4_PM_POLL_DELAY_US	20
#define ADF_GEN4_PM_POLL_TIMEOUT_US	USEC_PER_SEC
#define ADF_GEN4_PM_MSG_POLL_DELAY_US	(10 * USEC_PER_MSEC)
#define ADF_GEN4_PM_STATUS		(0x50A00C)
#define ADF_GEN4_PM_INTERRUPT		(0x50A028)

/* Power management source in ERRSOU2 and ERRMSK2 */
#define ADF_GEN4_PM_SOU			BIT(18)

#define ADF_GEN4_PM_IDLE_INT_EN		BIT(18)
#define ADF_GEN4_PM_THROTTLE_INT_EN	BIT(19)
#define ADF_GEN4_PM_DRV_ACTIVE		BIT(20)
#define ADF_GEN4_PM_INIT_STATE		BIT(21)
#define ADF_GEN4_PM_INT_EN_DEFAULT	(ADF_GEN4_PM_IDLE_INT_EN | \
					ADF_GEN4_PM_THROTTLE_INT_EN)

#define ADF_GEN4_PM_THR_STS	BIT(0)
#define ADF_GEN4_PM_IDLE_STS	BIT(1)
#define ADF_GEN4_PM_FW_INT_STS	BIT(2)
#define ADF_GEN4_PM_INT_STS_MASK (ADF_GEN4_PM_THR_STS | \
				 ADF_GEN4_PM_IDLE_STS | \
				 ADF_GEN4_PM_FW_INT_STS)

#define ADF_GEN4_PM_MSG_PENDING			BIT(0)
#define ADF_GEN4_PM_MSG_PAYLOAD_BIT_MASK	GENMASK(28, 1)

#define ADF_GEN4_PM_DEFAULT_IDLE_FILTER		(0x6)
#define ADF_GEN4_PM_MAX_IDLE_FILTER		(0x7)
#define ADF_GEN4_PM_DEFAULT_IDLE_SUPPORT	(0x1)

int adf_gen4_enable_pm(struct adf_accel_dev *accel_dev);
bool adf_gen4_handle_pm_interrupt(struct adf_accel_dev *accel_dev);

#endif
