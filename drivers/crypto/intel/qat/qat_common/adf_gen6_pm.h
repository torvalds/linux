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
#define ADF_GEN6_PM_CPM_PM_STATE_MASK		GENMASK(22, 20)

/* fusectl0 bitfields */
#define ADF_GEN6_PM_ENABLE_PM_MASK		BIT(21)
#define ADF_GEN6_PM_ENABLE_PM_IDLE_MASK		BIT(22)
#define ADF_GEN6_PM_ENABLE_DEEP_PM_IDLE_MASK	BIT(23)

/* cpm_pm_fw_init bitfields */
#define ADF_GEN6_PM_IDLE_FILTER_MASK		GENMASK(5, 3)
#define ADF_GEN6_PM_IDLE_ENABLE_MASK		BIT(2)

/* ssm_pm_enable bitfield */
#define ADF_GEN6_PM_SSM_PM_ENABLE_MASK		BIT(0)

/* ssm_pm_domain_status bitfield */
#define ADF_GEN6_PM_DOMAIN_POWERED_UP_MASK	BIT(0)

#ifdef CONFIG_DEBUG_FS
void adf_gen6_init_dev_pm_data(struct adf_accel_dev *accel_dev);
#else
static inline void adf_gen6_init_dev_pm_data(struct adf_accel_dev *accel_dev)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif /* ADF_GEN6_PM_H */
