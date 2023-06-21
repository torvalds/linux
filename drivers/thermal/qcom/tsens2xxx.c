// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/vmalloc.h>
#include "tsens2xxx.h"
#include <linux/qcom_scm.h>
#include "../thermal_core.h"

#define TSENS_TM_INT_EN(n)			((n) + 0x4)
#define TSENS_TM_CRITICAL_INT_STATUS(n)		((n) + 0x14)
#define TSENS_TM_CRITICAL_INT_CLEAR(n)		((n) + 0x18)
#define TSENS_TM_CRITICAL_INT_MASK(n)		((n) + 0x1c)
#define TSENS_TM_CRITICAL_WD_BARK		BIT(31)
#define TSENS_TM_CRITICAL_CYCLE_MONITOR		BIT(30)
#define TSENS_TM_CRITICAL_INT_EN		BIT(2)
#define TSENS_TM_UPPER_INT_EN			BIT(1)
#define TSENS_TM_LOWER_INT_EN			BIT(0)
#define TSENS_TM_UPPER_LOWER_INT_DISABLE	0xffffffff
#define TSENS_TM_SN_UPPER_LOWER_THRESHOLD(n)	((n) + 0x20)
#define TSENS_TM_SN_ADDR_OFFSET			0x4
#define TSENS_TM_UPPER_THRESHOLD_SET(n)		((n) << 12)
#define TSENS_TM_UPPER_THRESHOLD_VALUE_SHIFT(n)	((n) >> 12)
#define TSENS_TM_LOWER_THRESHOLD_VALUE(n)	((n) & 0xfff)
#define TSENS_TM_UPPER_THRESHOLD_VALUE(n)	(((n) & 0xfff000) >> 12)
#define TSENS_TM_UPPER_THRESHOLD_MASK		0xfff000
#define TSENS_TM_LOWER_THRESHOLD_MASK		0xfff
#define TSENS_TM_UPPER_THRESHOLD_SHIFT		12
#define TSENS_TM_SN_CRITICAL_THRESHOLD(n)	((n) + 0x60)
#define TSENS_STATUS_ADDR_OFFSET		2
#define TSENS_TM_UPPER_INT_MASK(n)		(((n) & 0xffff0000) >> 16)
#define TSENS_TM_LOWER_INT_MASK(n)		((n) & 0xffff)
#define TSENS_TM_UPPER_LOWER_INT_STATUS(n)	((n) + 0x8)
#define TSENS_TM_UPPER_LOWER_INT_CLEAR(n)	((n) + 0xc)
#define TSENS_TM_UPPER_LOWER_INT_MASK(n)	((n) + 0x10)
#define TSENS_TM_UPPER_INT_SET(n)		(1 << (n + 16))
#define TSENS_TM_SN_CRITICAL_THRESHOLD_MASK	0xfff
#define TSENS_TM_SN_STATUS_VALID_BIT		BIT(21)
#define TSENS_TM_SN_STATUS_CRITICAL_STATUS	BIT(19)
#define TSENS_TM_SN_STATUS_UPPER_STATUS		BIT(18)
#define TSENS_TM_SN_STATUS_LOWER_STATUS		BIT(17)
#define TSENS_TM_SN_LAST_TEMP_MASK		0xfff
#define TSENS_TM_CODE_BIT_MASK			0xfff
#define TSENS_TM_0C_THR_MASK			0xfff
#define TSENS_TM_0C_THR_OFFSET			12
#define TSENS_TM_CODE_SIGN_BIT			0x800
#define TSENS_TM_SCALE_DECI_MILLIDEG		100
#define TSENS_DEBUG_WDOG_TRIGGER_COUNT		5
#define TSENS_TM_WATCHDOG_LOG(n)		((n) + 0x13c)
#define TSENS_TM_WATCHDOG_LOG_v23(n)		((n) + 0x170)
#define TSENS_EN				BIT(0)
#define TSENS_CTRL_SENSOR_EN_MASK(n)		((n >> 3) & 0xffff)
#define TSENS_TM_TRDY(n)			((n) + 0xe4)
#define TSENS_TM_TRDY_FIRST_ROUND_COMPLETE	BIT(3)
#define TSENS_TM_TRDY_FIRST_ROUND_COMPLETE_SHIFT	3
#define TSENS_TM_0C_INT_STATUS(n)	((n) + 0xe0)
#define TSENS_TM_0C_THRESHOLDS(n)		((n) + 0x1c)
#define TSENS_MAX_READ_FAIL			50

#define TSENS_INIT_ID	0x5
#define TSENS_RECOVERY_LOOP_COUNT 5

static void msm_tsens_convert_temp(int last_temp, int *temp)
{
	int code_mask = ~TSENS_TM_CODE_BIT_MASK;

	if (last_temp & TSENS_TM_CODE_SIGN_BIT) {
		/* Sign extension for negative value */
		last_temp |= code_mask;
	}

	*temp = last_temp * TSENS_TM_SCALE_DECI_MILLIDEG;
}

static int __tsens2xxx_hw_init(struct tsens_device *tmdev)
{
	void __iomem *srot_addr;
	void __iomem *sensor_int_mask_addr;
	unsigned int srot_val, crit_mask, crit_val;
	void __iomem *int_mask_addr;

	srot_addr = TSENS_CTRL_ADDR(tmdev->tsens_srot_addr + 0x4);
	srot_val = readl_relaxed(srot_addr);
	if (!(srot_val & TSENS_EN)) {
		pr_err("TSENS device is not enabled\n");
		return -ENODEV;
	}

	if (tmdev->ctrl_data->cycle_monitor) {
		sensor_int_mask_addr =
			TSENS_TM_CRITICAL_INT_MASK(tmdev->tsens_tm_addr);
		crit_mask = readl_relaxed(sensor_int_mask_addr);
		crit_val = TSENS_TM_CRITICAL_CYCLE_MONITOR;
		if (tmdev->ctrl_data->cycle_compltn_monitor_mask)
			writel_relaxed((crit_mask | crit_val),
				(TSENS_TM_CRITICAL_INT_MASK
				(tmdev->tsens_tm_addr)));
		else
			writel_relaxed((crit_mask & ~crit_val),
				(TSENS_TM_CRITICAL_INT_MASK
				(tmdev->tsens_tm_addr)));
		/*Update critical cycle monitoring*/
		mb();
	}

	if (tmdev->ctrl_data->wd_bark) {
		sensor_int_mask_addr =
			TSENS_TM_CRITICAL_INT_MASK(tmdev->tsens_tm_addr);
		crit_mask = readl_relaxed(sensor_int_mask_addr);
		crit_val = TSENS_TM_CRITICAL_WD_BARK;
		if (tmdev->ctrl_data->wd_bark_mask)
			writel_relaxed((crit_mask | crit_val),
			(TSENS_TM_CRITICAL_INT_MASK
			(tmdev->tsens_tm_addr)));
		else
			writel_relaxed((crit_mask & ~crit_val),
			(TSENS_TM_CRITICAL_INT_MASK
			(tmdev->tsens_tm_addr)));
		/*Update watchdog monitoring*/
		mb();
	}

	int_mask_addr = TSENS_TM_UPPER_LOWER_INT_MASK(tmdev->tsens_tm_addr);
	writel_relaxed(TSENS_TM_UPPER_LOWER_INT_DISABLE, int_mask_addr);

	writel_relaxed(TSENS_TM_CRITICAL_INT_EN |
		TSENS_TM_UPPER_INT_EN | TSENS_TM_LOWER_INT_EN,
		TSENS_TM_INT_EN(tmdev->tsens_tm_addr));

	return 0;
}

static int tsens2xxx_get_temp(struct tsens_sensor *sensor, int *temp)
{
	struct tsens_device *tmdev = NULL, *tmdev_itr;
	unsigned int code, ret;
	void __iomem *sensor_addr, *trdy;
	int rc = 0, last_temp = 0, last_temp2 = 0, last_temp3 = 0, count = 0;
	int tsens_ret;
	static atomic_t in_tsens_reinit;

	if (!sensor)
		return -EINVAL;

	tmdev = sensor->tmdev;
	sensor_addr = TSENS_TM_SN_STATUS(tmdev->tsens_tm_addr);
	trdy = TSENS_TM_TRDY(tmdev->tsens_tm_addr);

	if (sensor->cached_temp != INT_MIN) {
		*temp = sensor->cached_temp;
		goto dbg;
	}

	code = readl_relaxed(trdy);

	if (!((code & TSENS_TM_TRDY_FIRST_ROUND_COMPLETE) >>
			TSENS_TM_TRDY_FIRST_ROUND_COMPLETE_SHIFT)) {
		if (atomic_read(&in_tsens_reinit)) {
			pr_err("%s: tsens re-init is in progress\n", __func__);
			return -EAGAIN;
		}

		pr_err("%s: tsens device first round not complete0x%x\n",
			__func__, code);

		/* Wait for 2.5 ms for tsens controller to recover */
		do {
			udelay(500);
			code = readl_relaxed(trdy);
			if (code & TSENS_TM_TRDY_FIRST_ROUND_COMPLETE) {
				TSENS_DUMP(tmdev, "%s",
					"tsens controller recovered\n");
				goto sensor_read;
			}
		} while (++count < TSENS_RECOVERY_LOOP_COUNT);

		/*
		 * TSENS controller did not recover,
		 * proceed with SCM call to re-init it
		 */
		if (tmdev->tsens_reinit_wa) {
			int scm_cnt = 0, reg_write_cnt = 0;

			if (atomic_read(&in_tsens_reinit)) {
				pr_err("%s: tsens re-init is in progress\n",
					__func__);
				return -EAGAIN;
			}

			atomic_set(&in_tsens_reinit, 1);

			if (tmdev->ops->dbg)
				tmdev->ops->dbg(tmdev, 0,
					TSENS_DBG_LOG_BUS_ID_DATA, NULL);

			while (1) {
				/*
				 * Invoke scm call only if SW register write is
				 * reflecting in controller. If not, wait for
				 * 2 ms and then retry.
				 */
				if (reg_write_cnt >= 100) {
					msleep(100);
					pr_err(
					"%s: Tsens write is failed. cnt:%d\n",
						__func__, reg_write_cnt);
					BUG();
				}
				writel_relaxed(BIT(2),
					TSENS_TM_INT_EN(tmdev->tsens_tm_addr));
				code = readl_relaxed(
					TSENS_TM_INT_EN(tmdev->tsens_tm_addr));
				if (!(code & BIT(2))) {
					udelay(2000);
					TSENS_DBG(tmdev, "%s cnt:%d\n",
					"Re-try TSENS write prior to scm",
						reg_write_cnt++);
					continue;
				}
				reg_write_cnt = 0;

				/* Make an scm call to re-init TSENS */
				TSENS_DBG(tmdev, "%s",
						"Calling TZ to re-init TSENS\n");
				ret = qcom_scm_tsens_reinit(&tsens_ret);
				TSENS_DBG(tmdev, "%s",
						"return from scm call\n");
				if (ret) {
					msleep(100);
					pr_err("%s: scm call failed, ret:%d\n",
						__func__, ret);
					BUG();
				}
				if (tsens_ret) {
					msleep(100);
					pr_err("%s: scm call failed to init tsens, ret:%d\n",
						__func__, tsens_ret);
					BUG();
				}

				scm_cnt++;
				rc = 0;
				list_for_each_entry(tmdev_itr,
						&tsens_device_list, list) {
					rc = __tsens2xxx_hw_init(tmdev_itr);
					if (rc) {
						pr_err(
						"%s: TSENS hw_init error\n",
							__func__);
						break;
					}
				}

				if (!rc)
					break;

				if (scm_cnt >= 100) {
					msleep(100);
					pr_err(
					"%s: Tsens is not up after %d scm\n",
						__func__, scm_cnt);
					BUG();
				}
				udelay(2000);
				TSENS_DBG(tmdev, "%s cnt:%d\n",
					"Re-try TSENS scm call", scm_cnt);
			}

			tmdev->tsens_reinit_cnt++;
			atomic_set(&in_tsens_reinit, 0);

			/* Notify thermal fwk */
			list_for_each_entry(tmdev_itr,
						&tsens_device_list, list) {
				queue_work(tmdev_itr->tsens_reinit_work,
					&tmdev_itr->therm_fwk_notify);
			}

		} else {
			pr_err("%s: tsens controller got reset\n", __func__);
			BUG();
		}
		return -EAGAIN;
	}

sensor_read:

	tmdev->trdy_fail_ctr = 0;

	code = readl_relaxed(sensor_addr +
			(sensor->hw_id << TSENS_STATUS_ADDR_OFFSET));
	last_temp = code & TSENS_TM_SN_LAST_TEMP_MASK;

	if (code & TSENS_TM_SN_STATUS_VALID_BIT) {
		msm_tsens_convert_temp(last_temp, temp);
		goto dbg;
	}

	code = readl_relaxed(sensor_addr +
		(sensor->hw_id << TSENS_STATUS_ADDR_OFFSET));
	last_temp2 = code & TSENS_TM_SN_LAST_TEMP_MASK;
	if (code & TSENS_TM_SN_STATUS_VALID_BIT) {
		last_temp = last_temp2;
		msm_tsens_convert_temp(last_temp, temp);
		goto dbg;
	}

	code = readl_relaxed(sensor_addr +
			(sensor->hw_id <<
			TSENS_STATUS_ADDR_OFFSET));
	last_temp3 = code & TSENS_TM_SN_LAST_TEMP_MASK;
	if (code & TSENS_TM_SN_STATUS_VALID_BIT) {
		last_temp = last_temp3;
		msm_tsens_convert_temp(last_temp, temp);
		goto dbg;
	}

	if (last_temp == last_temp2)
		last_temp = last_temp2;
	else if (last_temp2 == last_temp3)
		last_temp = last_temp3;

	msm_tsens_convert_temp(last_temp, temp);

dbg:
	if (tmdev->ops->dbg)
		tmdev->ops->dbg(tmdev, (u32) sensor->hw_id,
					TSENS_DBG_LOG_TEMP_READS, temp);

	return 0;
}

int tsens_2xxx_get_zeroc_status(struct tsens_sensor *sensor, int *status)
{
	struct tsens_device *tmdev = NULL;
	void __iomem *addr;

	if (!sensor)
		return -EINVAL;

	tmdev = sensor->tmdev;
	addr = TSENS_TM_0C_INT_STATUS(tmdev->tsens_tm_addr);
	*status = readl_relaxed(addr);

	TSENS_DBG(tmdev, "ZeroC status: %d\n", *status);

	return 0;
}

static int tsens_tm_activate_trip_type(struct tsens_sensor *tm_sensor,
			int trip, enum thermal_device_mode mode)
{
	struct tsens_device *tmdev = NULL;
	unsigned int reg_cntl, mask;
	int rc = 0;

	/* clear the interrupt and unmask */
	if (!tm_sensor || trip < 0)
		return -EINVAL;

	tmdev = tm_sensor->tmdev;
	if (!tmdev)
		return -EINVAL;


	mask = (tm_sensor->hw_id);
	switch (trip) {
	case THERMAL_TRIP_CRITICAL:
		tmdev->sensor[tm_sensor->hw_id].thr_state.crit_th_state = mode;
		reg_cntl = readl_relaxed(TSENS_TM_CRITICAL_INT_MASK
						(tmdev->tsens_tm_addr));
		if (mode == THERMAL_DEVICE_DISABLED)
			writel_relaxed(reg_cntl | (1 << mask),
				(TSENS_TM_CRITICAL_INT_MASK
				(tmdev->tsens_tm_addr)));
		else
			writel_relaxed(reg_cntl & ~(1 << mask),
				(TSENS_TM_CRITICAL_INT_MASK
				(tmdev->tsens_tm_addr)));
		break;
	case TSENS_TRIP_CONFIGURABLE_HI:
		tmdev->sensor[tm_sensor->hw_id].thr_state.high_th_state = mode;
		reg_cntl = readl_relaxed(TSENS_TM_UPPER_LOWER_INT_MASK
						(tmdev->tsens_tm_addr));
		if (mode == THERMAL_DEVICE_DISABLED)
			writel_relaxed(reg_cntl |
				(TSENS_TM_UPPER_INT_SET(mask)),
				(TSENS_TM_UPPER_LOWER_INT_MASK
				(tmdev->tsens_tm_addr)));
		else
			writel_relaxed(reg_cntl &
				~(TSENS_TM_UPPER_INT_SET(mask)),
				(TSENS_TM_UPPER_LOWER_INT_MASK
				(tmdev->tsens_tm_addr)));
		break;
	case TSENS_TRIP_CONFIGURABLE_LOW:
		tmdev->sensor[tm_sensor->hw_id].thr_state.low_th_state = mode;
		reg_cntl = readl_relaxed(TSENS_TM_UPPER_LOWER_INT_MASK
						(tmdev->tsens_tm_addr));
		if (mode == THERMAL_DEVICE_DISABLED)
			writel_relaxed(reg_cntl | (1 << mask),
			(TSENS_TM_UPPER_LOWER_INT_MASK
						(tmdev->tsens_tm_addr)));
		else
			writel_relaxed(reg_cntl & ~(1 << mask),
			(TSENS_TM_UPPER_LOWER_INT_MASK
						(tmdev->tsens_tm_addr)));
		break;
	default:
		rc = -EINVAL;
	}

	/* Activate and enable the respective trip threshold setting */
	mb();

	return rc;
}

static int tsens2xxx_set_trip_temp(struct tsens_sensor *tm_sensor,
						int low_temp, int high_temp)
{
	unsigned int reg_cntl;
	unsigned long flags;
	struct tsens_device *tmdev = NULL;
	int rc = 0;

	if (!tm_sensor)
		return -EINVAL;

	tmdev = tm_sensor->tmdev;
	if (!tmdev)
		return -EINVAL;

	pr_debug("%s: sensor:%d low_temp(mdegC):%d, high_temp(mdegC):%d\n",
			__func__, tm_sensor->hw_id, low_temp, high_temp);

	spin_lock_irqsave(&tmdev->tsens_upp_low_lock, flags);

	if (high_temp != INT_MAX) {
		tmdev->sensor[tm_sensor->hw_id].thr_state.high_temp = high_temp;
		reg_cntl = readl_relaxed((TSENS_TM_SN_UPPER_LOWER_THRESHOLD
				(tmdev->tsens_tm_addr)) +
				(tm_sensor->hw_id *
				TSENS_TM_SN_ADDR_OFFSET));
		high_temp /= TSENS_TM_SCALE_DECI_MILLIDEG;
		high_temp = TSENS_TM_UPPER_THRESHOLD_SET(high_temp);
		high_temp &= TSENS_TM_UPPER_THRESHOLD_MASK;
		reg_cntl &= ~TSENS_TM_UPPER_THRESHOLD_MASK;
		writel_relaxed(reg_cntl | high_temp,
			(TSENS_TM_SN_UPPER_LOWER_THRESHOLD
				(tmdev->tsens_tm_addr) +
			(tm_sensor->hw_id * TSENS_TM_SN_ADDR_OFFSET)));
	}

	if (low_temp != -INT_MAX) {
		tmdev->sensor[tm_sensor->hw_id].thr_state.low_temp = low_temp;
		reg_cntl = readl_relaxed((TSENS_TM_SN_UPPER_LOWER_THRESHOLD
				(tmdev->tsens_tm_addr)) +
				(tm_sensor->hw_id *
				TSENS_TM_SN_ADDR_OFFSET));
		low_temp /= TSENS_TM_SCALE_DECI_MILLIDEG;
		low_temp &= TSENS_TM_LOWER_THRESHOLD_MASK;
		reg_cntl &= ~TSENS_TM_LOWER_THRESHOLD_MASK;
		writel_relaxed(reg_cntl | low_temp,
			(TSENS_TM_SN_UPPER_LOWER_THRESHOLD
				(tmdev->tsens_tm_addr) +
			(tm_sensor->hw_id * TSENS_TM_SN_ADDR_OFFSET)));
	}

	/* Set trip temperature thresholds */
	mb();

	if (high_temp != INT_MAX) {
		rc = tsens_tm_activate_trip_type(tm_sensor,
				TSENS_TRIP_CONFIGURABLE_HI,
				THERMAL_DEVICE_ENABLED);
		if (rc) {
			pr_err("trip high enable error :%d\n", rc);
			goto fail;
		}
	} else {
		rc = tsens_tm_activate_trip_type(tm_sensor,
				TSENS_TRIP_CONFIGURABLE_HI,
				THERMAL_DEVICE_DISABLED);
		if (rc) {
			pr_err("trip high disable error :%d\n", rc);
			goto fail;
		}
	}

	if (low_temp != -INT_MAX) {
		rc = tsens_tm_activate_trip_type(tm_sensor,
				TSENS_TRIP_CONFIGURABLE_LOW,
				THERMAL_DEVICE_ENABLED);
		if (rc) {
			pr_err("trip low enable activation error :%d\n", rc);
			goto fail;
		}
	} else {
		rc = tsens_tm_activate_trip_type(tm_sensor,
				TSENS_TRIP_CONFIGURABLE_LOW,
				THERMAL_DEVICE_DISABLED);
		if (rc) {
			pr_err("trip low disable error :%d\n", rc);
			goto fail;
		}
	}

fail:
	spin_unlock_irqrestore(&tmdev->tsens_upp_low_lock, flags);
	return rc;
}

static irqreturn_t tsens_tm_critical_irq_thread(int irq, void *data)
{
	struct tsens_device *tm = data;
	unsigned int i, status, wd_log, wd_mask;
	unsigned long flags;
	void __iomem *sensor_status_addr, *sensor_int_mask_addr;
	void __iomem *sensor_critical_addr;
	void __iomem *wd_critical_addr, *wd_log_addr;

	sensor_status_addr = TSENS_TM_SN_STATUS(tm->tsens_tm_addr);
	sensor_int_mask_addr =
		TSENS_TM_CRITICAL_INT_MASK(tm->tsens_tm_addr);
	sensor_critical_addr =
		TSENS_TM_SN_CRITICAL_THRESHOLD(tm->tsens_tm_addr);
	wd_critical_addr =
		TSENS_TM_CRITICAL_INT_STATUS(tm->tsens_tm_addr);
	if (tm->ctrl_data->ver_major == 2 && tm->ctrl_data->ver_minor == 3)
		wd_log_addr = TSENS_TM_WATCHDOG_LOG_v23(tm->tsens_tm_addr);
	else
		wd_log_addr = TSENS_TM_WATCHDOG_LOG(tm->tsens_tm_addr);

	if (tm->ctrl_data->wd_bark) {
		wd_mask = readl_relaxed(wd_critical_addr);
		if (wd_mask & TSENS_TM_CRITICAL_WD_BARK) {
			/*
			 * Clear watchdog interrupt and
			 * increment global wd count
			 */
			writel_relaxed(wd_mask | TSENS_TM_CRITICAL_WD_BARK,
				(TSENS_TM_CRITICAL_INT_CLEAR
				(tm->tsens_tm_addr)));
			writel_relaxed(wd_mask & ~(TSENS_TM_CRITICAL_WD_BARK),
				(TSENS_TM_CRITICAL_INT_CLEAR
				(tm->tsens_tm_addr)));
			wd_log = readl_relaxed(wd_log_addr);
			if (wd_log >= TSENS_DEBUG_WDOG_TRIGGER_COUNT) {
				pr_err("Watchdog count:%d\n", wd_log);
				if (tm->ops->dbg)
					tm->ops->dbg(tm, 0,
					TSENS_DBG_LOG_BUS_ID_DATA, NULL);
				BUG();
			}

			return IRQ_HANDLED;
		}
	}

	for (i = 0; i < TSENS_MAX_SENSORS; i++) {
		int int_mask, int_mask_val;
		u32 addr_offset;

		if (IS_ERR(tm->sensor[i].tzd))
			continue;

		spin_lock_irqsave(&tm->tsens_crit_lock, flags);
		addr_offset = tm->sensor[i].hw_id *
						TSENS_TM_SN_ADDR_OFFSET;
		status = readl_relaxed(sensor_status_addr + addr_offset);
		int_mask = readl_relaxed(sensor_int_mask_addr);

		if ((status & TSENS_TM_SN_STATUS_CRITICAL_STATUS) &&
			!(int_mask & (1 << tm->sensor[i].hw_id))) {
			int_mask = readl_relaxed(sensor_int_mask_addr);
			int_mask_val = (1 << tm->sensor[i].hw_id);
			/* Mask the corresponding interrupt for the sensors */
			writel_relaxed(int_mask | int_mask_val,
				TSENS_TM_CRITICAL_INT_MASK(
					tm->tsens_tm_addr));
			/* Clear the corresponding sensors interrupt */
			writel_relaxed(int_mask_val,
				TSENS_TM_CRITICAL_INT_CLEAR
					(tm->tsens_tm_addr));
			writel_relaxed(0,
				TSENS_TM_CRITICAL_INT_CLEAR(
					tm->tsens_tm_addr));
			tm->sensor[i].thr_state.crit_th_state =
						THERMAL_DEVICE_DISABLED;
		}
		spin_unlock_irqrestore(&tm->tsens_crit_lock, flags);
	}

	/* Mask critical interrupt */
	mb();

	return IRQ_HANDLED;
}

static irqreturn_t tsens_tm_irq_thread(int irq, void *data)
{
	struct tsens_device *tm = data;
	unsigned int i, status, threshold, temp;
	unsigned long flags;
	void __iomem *sensor_status_addr;
	void __iomem *sensor_int_mask_addr;
	void __iomem *sensor_upper_lower_addr;
	u32 addr_offset = 0;

	sensor_status_addr = TSENS_TM_SN_STATUS(tm->tsens_tm_addr);
	sensor_int_mask_addr =
		TSENS_TM_UPPER_LOWER_INT_MASK(tm->tsens_tm_addr);
	sensor_upper_lower_addr =
		TSENS_TM_SN_UPPER_LOWER_THRESHOLD(tm->tsens_tm_addr);

	for (i = 0; i < TSENS_MAX_SENSORS; i++) {
		bool upper_thr = false, lower_thr = false;
		int int_mask, int_mask_val = 0, rc;

		if (IS_ERR(tm->sensor[i].tzd))
			continue;

		rc = tsens2xxx_get_temp(&tm->sensor[i], &temp);
		if (rc) {
			pr_debug("Error:%d reading temp sensor:%d\n", rc, i);
			continue;
		}

		spin_lock_irqsave(&tm->tsens_upp_low_lock, flags);
		addr_offset = tm->sensor[i].hw_id *
						TSENS_TM_SN_ADDR_OFFSET;
		status = readl_relaxed(sensor_status_addr + addr_offset);
		threshold = readl_relaxed(sensor_upper_lower_addr +
								addr_offset);
		int_mask = readl_relaxed(sensor_int_mask_addr);

		if ((status & TSENS_TM_SN_STATUS_UPPER_STATUS) &&
			!(int_mask &
				(1 << (tm->sensor[i].hw_id + 16)))) {
			int_mask = readl_relaxed(sensor_int_mask_addr);
			int_mask_val = TSENS_TM_UPPER_INT_SET(
					tm->sensor[i].hw_id);
			/* Mask the corresponding interrupt for the sensors */
			writel_relaxed(int_mask | int_mask_val,
					TSENS_TM_UPPER_LOWER_INT_MASK(
						tm->tsens_tm_addr));
			/* Clear the corresponding sensors interrupt */
			writel_relaxed(int_mask_val,
				TSENS_TM_UPPER_LOWER_INT_CLEAR(
					tm->tsens_tm_addr));
			writel_relaxed(0,
				TSENS_TM_UPPER_LOWER_INT_CLEAR(
					tm->tsens_tm_addr));
			if (TSENS_TM_UPPER_THRESHOLD_VALUE(threshold) >
				(temp/TSENS_TM_SCALE_DECI_MILLIDEG)) {
				pr_debug("Re-arm high threshold\n");
				rc = tsens_tm_activate_trip_type(
					&tm->sensor[i],
					TSENS_TRIP_CONFIGURABLE_HI,
					THERMAL_DEVICE_ENABLED);
				if (rc)
					pr_err("high rearm failed:%d\n", rc);
			} else {
				upper_thr = true;
				tm->sensor[i].thr_state.high_th_state =
						THERMAL_DEVICE_DISABLED;
			}
		}

		if ((status & TSENS_TM_SN_STATUS_LOWER_STATUS) &&
			!(int_mask &
				(1 << tm->sensor[i].hw_id))) {
			int_mask = readl_relaxed(sensor_int_mask_addr);
			int_mask_val = (1 << tm->sensor[i].hw_id);
			/* Mask the corresponding interrupt for the sensors */
			writel_relaxed(int_mask | int_mask_val,
					TSENS_TM_UPPER_LOWER_INT_MASK(
						tm->tsens_tm_addr));
			/* Clear the corresponding sensors interrupt */
			writel_relaxed(int_mask_val,
				TSENS_TM_UPPER_LOWER_INT_CLEAR(
					tm->tsens_tm_addr));
			writel_relaxed(0,
				TSENS_TM_UPPER_LOWER_INT_CLEAR(
					tm->tsens_tm_addr));
			if (TSENS_TM_LOWER_THRESHOLD_VALUE(threshold)
				< (temp/TSENS_TM_SCALE_DECI_MILLIDEG)) {
				pr_debug("Re-arm low threshold\n");
				rc = tsens_tm_activate_trip_type(
					&tm->sensor[i],
					TSENS_TRIP_CONFIGURABLE_LOW,
					THERMAL_DEVICE_ENABLED);
				if (rc)
					pr_err("low rearm failed:%d\n", rc);
			} else {
				lower_thr = true;
				tm->sensor[i].thr_state.low_th_state =
						THERMAL_DEVICE_DISABLED;
			}
		}
		spin_unlock_irqrestore(&tm->tsens_upp_low_lock, flags);

		if (upper_thr || lower_thr) {
			/* Use id for multiple controllers */
			tm->sensor[i].cached_temp = temp;
			pr_debug("sensor:%d trigger temp (%d degC)\n",
				tm->sensor[i].hw_id, temp);
			thermal_zone_device_update(tm->sensor[i].tzd,
						THERMAL_EVENT_UNSPECIFIED);
		}
		tm->sensor[i].cached_temp = INT_MIN;
	}

	/* Disable monitoring sensor trip threshold for triggered sensor */
	mb();

	if (tm->ops->dbg)
		tm->ops->dbg(tm, 0, TSENS_DBG_LOG_INTERRUPT_TIMESTAMP, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t tsens_tm_zeroc_irq_thread(int irq, void *data)
{
	struct tsens_device *tm = data;
	int status, thrs, set_thr, reset_thr;
	void __iomem *srot_addr, *addr;

	addr = TSENS_TM_0C_INT_STATUS(tm->tsens_tm_addr);
	status = readl_relaxed(addr);

	srot_addr = TSENS_CTRL_ADDR(tm->tsens_srot_addr);
	thrs = readl_relaxed(TSENS_TM_0C_THRESHOLDS(srot_addr));

	msm_tsens_convert_temp(thrs & TSENS_TM_0C_THR_MASK, &reset_thr);
	msm_tsens_convert_temp(((thrs >> TSENS_TM_0C_THR_OFFSET)
			& TSENS_TM_0C_THR_MASK), &set_thr);

	TSENS_DBG(tm, "Tsens ZeroC status: %d set_t:%d reset_t:%d\n",
		status, set_thr, reset_thr);
	thermal_zone_device_update(tm->zeroc.tzd,
				THERMAL_EVENT_UNSPECIFIED);
	return IRQ_HANDLED;
}

static int tsens2xxx_hw_sensor_en(struct tsens_device *tmdev,
					u32 sensor_id)
{
	void __iomem *srot_addr;
	unsigned int srot_val, sensor_en;

	srot_addr = TSENS_CTRL_ADDR(tmdev->tsens_srot_addr + 0x4);
	srot_val = readl_relaxed(srot_addr);
	srot_val = TSENS_CTRL_SENSOR_EN_MASK(srot_val);

	sensor_en = ((1 << sensor_id) & srot_val);

	return sensor_en;
}

static int tsens2xxx_hw_init(struct tsens_device *tmdev)
{
	int rc = 0;

	rc = __tsens2xxx_hw_init(tmdev);
	if (rc)
		return rc;

	spin_lock_init(&tmdev->tsens_crit_lock);
	spin_lock_init(&tmdev->tsens_upp_low_lock);

	return 0;
}

static const struct tsens_irqs tsens2xxx_irqs[] = {
	{ "tsens-upper-lower", tsens_tm_irq_thread},
	{ "tsens-critical", tsens_tm_critical_irq_thread},
	{ "tsens-0C", tsens_tm_zeroc_irq_thread},
};

static int tsens2xxx_tsens_suspend(struct tsens_device *tmdev)
{
	int i, irq;
	struct platform_device *pdev;

	if (!tmdev)
		return -EINVAL;

	pdev = tmdev->pdev;
	for (i = 0; i < (ARRAY_SIZE(tsens2xxx_irqs) - 1); i++) {
		irq = platform_get_irq_byname(pdev, tsens2xxx_irqs[i].name);
		if (irq < 0) {
			dev_err(&pdev->dev, "failed to get irq %s\n",
				tsens2xxx_irqs[i].name);
			return irq;
		}
		disable_irq_nosync(irq);
	}
	/* Vote for zeroC Voltage restrictions before deep sleep entry */
	if (tmdev->zeroc.tzd)
		thermal_zone_device_update(tmdev->zeroc.tzd,
					THERMAL_EVENT_UNSPECIFIED);
	return 0;
}

static int tsens2xxx_tsens_resume(struct tsens_device *tmdev)
{
	int rc, i, irq;
	struct platform_device *pdev;

	if (!tmdev)
		return -EINVAL;

	rc = tsens2xxx_hw_init(tmdev);

	if (rc) {
		pr_err("Error initializing TSENS controller\n");
		return rc;
	}

	pdev = tmdev->pdev;
	for (i = 0; i < (ARRAY_SIZE(tsens2xxx_irqs) - 1); i++) {
		irq = platform_get_irq_byname(pdev, tsens2xxx_irqs[i].name);
		if (irq < 0) {
			dev_err(&pdev->dev, "failed to get irq %s\n",
				tsens2xxx_irqs[i].name);
			return irq;
		}
		enable_irq(irq);
		enable_irq_wake(irq);
	}
	queue_work(tmdev->tsens_reinit_work,
					&tmdev->therm_fwk_notify);
	return 0;
}

static int tsens2xxx_register_interrupts(struct tsens_device *tmdev)
{
	struct platform_device *pdev;
	int i, rc, irq_no;

	if (!tmdev)
		return -EINVAL;

	if (tmdev->zeroc_sensor_id != MIN_TEMP_DEF_OFFSET)
		irq_no = ARRAY_SIZE(tsens2xxx_irqs);
	else
		irq_no = ARRAY_SIZE(tsens2xxx_irqs) - 1;

	pdev = tmdev->pdev;

	for (i = 0; i < irq_no; i++) {
		int irq;

		irq = platform_get_irq_byname(pdev, tsens2xxx_irqs[i].name);
		if (irq < 0) {
			dev_err(&pdev->dev, "failed to get irq %s\n",
					tsens2xxx_irqs[i].name);
			return irq;
		}

		rc = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				tsens2xxx_irqs[i].handler,
				IRQF_ONESHOT, tsens2xxx_irqs[i].name, tmdev);
		if (rc) {
			dev_err(&pdev->dev, "failed to get irq %s\n",
					tsens2xxx_irqs[i].name);
			return rc;
		}
		enable_irq_wake(irq);
	}

	return 0;
}

static const struct tsens_ops ops_tsens2xxx = {
	.hw_init	= tsens2xxx_hw_init,
	.get_temp	= tsens2xxx_get_temp,
	.set_trips	= tsens2xxx_set_trip_temp,
	.interrupts_reg	= tsens2xxx_register_interrupts,
	.dbg		= tsens2xxx_dbg,
	.sensor_en	= tsens2xxx_hw_sensor_en,
	.suspend = tsens2xxx_tsens_suspend,
	.resume = tsens2xxx_tsens_resume,
};

const struct tsens_data data_tsens2xxx = {
	.cycle_monitor			= false,
	.cycle_compltn_monitor_mask	= 1,
	.wd_bark			= false,
	.wd_bark_mask			= 1,
	.ops				= &ops_tsens2xxx,
};

const struct tsens_data data_tsens23xx = {
	.cycle_monitor			= true,
	.cycle_compltn_monitor_mask	= 1,
	.wd_bark			= true,
	.wd_bark_mask			= 1,
	.ops				= &ops_tsens2xxx,
	.ver_major			= 2,
	.ver_minor			= 3,
};

const struct tsens_data data_tsens24xx = {
	.cycle_monitor			= true,
	.cycle_compltn_monitor_mask	= 1,
	.wd_bark			= true,
	/* Enable Watchdog monitoring by unmasking */
	.wd_bark_mask			= 0,
	.ops				= &ops_tsens2xxx,
};

const struct tsens_data data_tsens26xx = {
	.cycle_monitor			= true,
	.cycle_compltn_monitor_mask	= 1,
	.wd_bark			= true,
	.wd_bark_mask			= 0,
	.ops				= &ops_tsens2xxx,
	.ver_major			= 2,
	.ver_minor			= 6,
};
