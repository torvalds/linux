/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_GEN6_PM_H
#define ADF_GEN6_PM_H

#include <linux/bits.h>
#include <linux/time.h>

struct adf_accel_dev;

/* Power management */
#define ADF_GEN6_PM_POLL_DELAY_US	20
#define ADF_GEN6_PM_POLL_TIMEOUT_US	USEC_PER_SEC
#define ADF_GEN6_PM_STATUS		0x50A00C
#define ADF_GEN6_PM_INTERRUPT		0x50A028

/* Power management source in ERRSOU2 and ERRMSK2 */
#define ADF_GEN6_PM_SOU			BIT(18)

/* cpm_pm_interrupt bitfields */
#define ADF_GEN6_PM_DRV_ACTIVE		BIT(20)

#define ADF_GEN6_PM_DEFAULT_IDLE_FILTER	0x6

/* cpm_pm_status bitfields */
#define ADF_GEN6_PM_INIT_STATE			BIT(21)

#endif /* ADF_GEN6_PM_H */
