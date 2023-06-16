// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"QG-K: %s: " fmt, __func__

#include <linux/alarmtimer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <uapi/linux/qg.h>
#include <uapi/linux/qg-profile.h>
#include "fg-alg.h"
#include "qg-sdam.h"
#include "qg-core.h"
#include "qg-iio.h"
#include "qg-reg.h"
#include "qg-util.h"
#include "qg-defs.h"
#include "qg-profile-lib.h"
#include "qg-soc.h"

enum soc_scaling_feature {
	QG_FVSS = BIT(0),
	QG_TCSS = BIT(1),
	QG_BASS = BIT(2),
};

#define DEFAULT_UPDATE_TIME_MS			64000
#define SOC_SCALE_HYST_MS			2000
#define VBAT_LOW_HYST_UV			50000
#define FULL_SOC				100

static int qg_ss_feature;
static ssize_t qg_ss_feature_show(struct device *dev, struct device_attribute
				     *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%4x\n", qg_ss_feature);
}

static ssize_t qg_ss_feature_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_ss_feature = val;

	return count;
}
DEVICE_ATTR_RW(qg_ss_feature);

static int qg_delta_soc_interval_ms = 20000;
static ssize_t soc_interval_ms_show(struct device *dev, struct device_attribute
				     *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", qg_delta_soc_interval_ms);
}

static ssize_t soc_interval_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_delta_soc_interval_ms = val;

	return count;
}
DEVICE_ATTR_RW(soc_interval_ms);

static int qg_fvss_delta_soc_interval_ms = 10000;
static ssize_t fvss_delta_soc_interval_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", qg_fvss_delta_soc_interval_ms);
}

static ssize_t fvss_delta_soc_interval_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_fvss_delta_soc_interval_ms = val;

	return count;
}
DEVICE_ATTR_RW(fvss_delta_soc_interval_ms);

static int qg_delta_soc_cold_interval_ms = 4000;
static ssize_t soc_cold_interval_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", qg_delta_soc_cold_interval_ms);
}

static ssize_t soc_cold_interval_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_delta_soc_cold_interval_ms = val;

	return count;
}
DEVICE_ATTR_RW(soc_cold_interval_ms);

static int qg_maint_soc_update_ms = 120000;
static ssize_t maint_soc_update_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", qg_maint_soc_update_ms);
}

static ssize_t maint_soc_update_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_maint_soc_update_ms = val;

	return count;
}
DEVICE_ATTR_RW(maint_soc_update_ms);

/* FVSS scaling only based on VBAT */
static int qg_fvss_vbat_scaling = 1;
static ssize_t fvss_vbat_scaling_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", qg_fvss_vbat_scaling);
}

static ssize_t fvss_vbat_scaling_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;

	if (kstrtos32(buf, 0, &val))
		return -EINVAL;

	qg_fvss_vbat_scaling = val;

	return count;
}
DEVICE_ATTR_RW(fvss_vbat_scaling);

static int qg_process_fvss_soc(struct qpnp_qg *chip, int sys_soc)
{
	int rc, vbat_uv = 0, vbat_cutoff_uv = chip->dt.vbatt_cutoff_mv * 1000;
	int soc_vbat = 0, wt_vbat = 0, wt_sys = 0, soc_fvss = 0;

	if (!chip->dt.fvss_enable && !(qg_ss_feature & QG_FVSS))
		goto exit_soc_scale;

	if (chip->charge_status == POWER_SUPPLY_STATUS_CHARGING)
		goto exit_soc_scale;

	rc = qg_get_battery_voltage(chip, &vbat_uv);
	if (rc < 0)
		goto exit_soc_scale;

	if (!chip->last_fifo_v_uv)
		chip->last_fifo_v_uv = vbat_uv;

	if (chip->last_fifo_v_uv > (chip->dt.fvss_vbat_mv * 1000)) {
		qg_dbg(chip, QG_DEBUG_SOC, "FVSS: last_fifo_v=%d fvss_entry_uv=%d - exit\n",
			chip->last_fifo_v_uv, chip->dt.fvss_vbat_mv * 1000);
		goto exit_soc_scale;
	}

	/* Enter FVSS */
	if (!chip->fvss_active) {
		chip->vbat_fvss_entry = CAP(vbat_cutoff_uv,
					chip->dt.fvss_vbat_mv * 1000,
					chip->last_fifo_v_uv);
		chip->soc_fvss_entry = sys_soc;
		chip->fvss_active = true;
	} else if (chip->last_fifo_v_uv > chip->vbat_fvss_entry) {
		/* VBAT has gone beyond the entry voltage */
		chip->vbat_fvss_entry = chip->last_fifo_v_uv;
		chip->soc_fvss_entry = sys_soc;
	}

	soc_vbat = qg_linear_interpolate(chip->soc_fvss_entry,
					chip->vbat_fvss_entry,
					0,
					vbat_cutoff_uv,
					chip->last_fifo_v_uv);
	soc_vbat = CAP(0, 100, soc_vbat);

	if (qg_fvss_vbat_scaling) {
		wt_vbat = 100;
		wt_sys = 0;
	} else {
		wt_sys = qg_linear_interpolate(100,
					chip->soc_fvss_entry,
					0,
					0,
					sys_soc);
		wt_sys = CAP(0, 100, wt_sys);
		wt_vbat = 100 - wt_sys;
	}

	soc_fvss = ((soc_vbat * wt_vbat) + (sys_soc * wt_sys)) / 100;
	soc_fvss = CAP(0, 100, soc_fvss);

	qg_dbg(chip, QG_DEBUG_SOC, "FVSS: vbat_fvss_entry=%d soc_fvss_entry=%d cutoff_uv=%d vbat_uv=%d fifo_avg_v=%d soc_vbat=%d sys_soc=%d wt_vbat=%d wt_sys=%d soc_fvss=%d\n",
			chip->vbat_fvss_entry, chip->soc_fvss_entry,
			vbat_cutoff_uv, vbat_uv, chip->last_fifo_v_uv,
			soc_vbat, sys_soc, wt_vbat, wt_sys, soc_fvss);

	return soc_fvss;

exit_soc_scale:
	chip->fvss_active = false;
	return sys_soc;
}

#define IBAT_HYST_PC			10
#define TCSS_ENTRY_COUNT		2
static int qg_process_tcss_soc(struct qpnp_qg *chip, int sys_soc)
{
	int rc, ibatt_diff = 0, ibat_inc_hyst = 0;
	int qg_iterm_ua = (-1 * chip->dt.iterm_ma * 1000);
	int soc_ibat, wt_ibat, wt_sys, val;
	union power_supply_propval prop = {0, };

	if (!chip->dt.tcss_enable && !(qg_ss_feature & QG_TCSS))
		goto exit_soc_scale;

	if (chip->sys_soc < (chip->dt.tcss_entry_soc * 100))
		goto exit_soc_scale;

	if (chip->sys_soc >= QG_MAX_SOC && chip->soc_tcss >= QG_MAX_SOC)
		goto exit_soc_scale;

	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_HEALTH, &prop);
	if (!rc && (prop.intval == POWER_SUPPLY_HEALTH_COOL ||
			prop.intval == POWER_SUPPLY_HEALTH_WARM))
		goto exit_soc_scale;

	if (chip->last_fifo_i_ua >= 0)
		goto exit_soc_scale;
	else if (++chip->tcss_entry_count < TCSS_ENTRY_COUNT)
		goto skip_entry_count;

	if (!chip->tcss_active) {
		chip->soc_tcss = sys_soc;
		chip->soc_tcss_entry = sys_soc;
		chip->ibat_tcss_entry = min(chip->last_fifo_i_ua, qg_iterm_ua);
		chip->prev_fifo_i_ua = chip->last_fifo_i_ua;
		chip->tcss_active = true;
	}

	rc = qg_read_iio_chan(chip, INPUT_CURRENT_LIMITED, &val);
	if (!rc && val) {
		qg_dbg(chip, QG_DEBUG_SOC,
			"Input limited sys_soc=%d soc_tcss=%d\n",
					sys_soc, chip->soc_tcss);
		if (chip->soc_tcss > sys_soc)
			sys_soc = chip->soc_tcss;
		goto exit_soc_scale;
	}

	ibatt_diff = chip->last_fifo_i_ua - chip->prev_fifo_i_ua;
	if (ibatt_diff > 0) {
		/*
		 * if the battery charge current has suddendly dropped, allow it
		 * to decrease only by a small fraction to avoid a SOC jump.
		 */
		ibat_inc_hyst = (chip->prev_fifo_i_ua * IBAT_HYST_PC) / 100;
		if (ibatt_diff > abs(ibat_inc_hyst))
			chip->prev_fifo_i_ua -= ibat_inc_hyst;
		else
			chip->prev_fifo_i_ua = chip->last_fifo_i_ua;
	}

	chip->prev_fifo_i_ua = min(chip->prev_fifo_i_ua, qg_iterm_ua);
	soc_ibat = qg_linear_interpolate(chip->soc_tcss_entry,
					chip->ibat_tcss_entry,
					QG_MAX_SOC,
					qg_iterm_ua,
					chip->prev_fifo_i_ua);
	soc_ibat = CAP(QG_MIN_SOC, QG_MAX_SOC, soc_ibat);

	wt_ibat = qg_linear_interpolate(1, chip->soc_tcss_entry,
					10000, 10000, soc_ibat);
	wt_ibat = CAP(QG_MIN_SOC, QG_MAX_SOC, wt_ibat);
	wt_sys = 10000 - wt_ibat;

	chip->soc_tcss = DIV_ROUND_CLOSEST((soc_ibat * wt_ibat) +
					(wt_sys * sys_soc), 10000);
	chip->soc_tcss = CAP(QG_MIN_SOC, QG_MAX_SOC, chip->soc_tcss);

	qg_dbg(chip, QG_DEBUG_SOC,
		"TCSS: fifo_i=%d prev_fifo_i=%d ibatt_tcss_entry=%d qg_term=%d soc_tcss_entry=%d sys_soc=%d soc_ibat=%d wt_ibat=%d wt_sys=%d soc_tcss=%d\n",
			chip->last_fifo_i_ua, chip->prev_fifo_i_ua,
			chip->ibat_tcss_entry, qg_iterm_ua,
			chip->soc_tcss_entry, sys_soc, soc_ibat,
			wt_ibat, wt_sys, chip->soc_tcss);

	return chip->soc_tcss;

exit_soc_scale:
	chip->tcss_entry_count = 0;
skip_entry_count:
	chip->tcss_active = false;
	if (chip->dt.tcss_enable || (qg_ss_feature & QG_TCSS))
		qg_dbg(chip, QG_DEBUG_SOC, "TCSS: Quit - enabled=%d sys_soc=%d tcss_entry_count=%d fifo_i_ua=%d\n",
			chip->dt.tcss_enable, sys_soc, chip->tcss_entry_count,
			chip->last_fifo_i_ua);
	return sys_soc;
}

#define BASS_SYS_MSOC_DELTA			2
static int qg_process_bass_soc(struct qpnp_qg *chip, int sys_soc)
{
	int bass_soc = sys_soc, msoc = chip->msoc;

	if (!chip->dt.bass_enable && !(qg_ss_feature & QG_BASS))
		goto exit_soc_scale;

	qg_dbg(chip, QG_DEBUG_SOC, "BASS Entry: fifo_i=%d sys_soc=%d msoc=%d batt_soc=%d fvss_active=%d\n",
			chip->last_fifo_i_ua, sys_soc, msoc,
			chip->batt_soc, chip->fvss_active);

	/* Skip BASS if FVSS is active */
	if (chip->fvss_active)
		goto exit_soc_scale;

	if (((sys_soc - msoc) < BASS_SYS_MSOC_DELTA) ||
				chip->last_fifo_i_ua <= 0)
		goto exit_soc_scale;

	if (!chip->bass_active) {
		chip->bass_active = true;
		chip->bsoc_bass_entry = chip->batt_soc;
	}

	/* Drop the sys_soc by 1% if batt_soc has dropped */
	if ((chip->bsoc_bass_entry - chip->batt_soc) >= 100) {
		bass_soc = (msoc > 0) ? msoc - 1 : 0;
		chip->bass_active = false;
	}

	qg_dbg(chip, QG_DEBUG_SOC, "BASS Exit: fifo_i_ua=%d sys_soc=%d msoc=%d bsoc_bass_entry=%d batt_soc=%d bass_soc=%d\n",
			chip->last_fifo_i_ua, sys_soc, msoc,
			chip->bsoc_bass_entry, chip->batt_soc, bass_soc);

	return bass_soc;

exit_soc_scale:
	chip->bass_active = false;
	if (chip->dt.bass_enable || (qg_ss_feature & QG_BASS))
		qg_dbg(chip, QG_DEBUG_SOC, "BASS Quit: enabled=%d fifo_i_ua=%d sys_soc=%d msoc=%d batt_soc=%d\n",
			chip->dt.bass_enable, chip->last_fifo_i_ua,
			sys_soc, msoc, chip->batt_soc);
	return sys_soc;
}

int qg_adjust_sys_soc(struct qpnp_qg *chip)
{
	int soc, vbat_uv, rc;
	int vcutoff_uv = chip->dt.vbatt_cutoff_mv * 1000;

	chip->sys_soc = CAP(QG_MIN_SOC, QG_MAX_SOC, chip->sys_soc);

	/* TCSS */
	chip->sys_soc = qg_process_tcss_soc(chip, chip->sys_soc);

	if (chip->sys_soc == QG_MAX_SOC) {
		soc = FULL_SOC;
	} else if (chip->sys_soc >= (QG_MAX_SOC - 100)) {
		/* Hold SOC to 100% if we are dropping from 100 to 99 */
		if (chip->last_adj_ssoc == FULL_SOC)
			soc = FULL_SOC;
		else /* Hold SOC at 99% until we hit 100% */
			soc = FULL_SOC - 1;
	} else {
		soc = DIV_ROUND_CLOSEST(chip->sys_soc, 100);
	}

	/* FVSS */
	soc = qg_process_fvss_soc(chip, soc);

	/* BASS */
	soc = qg_process_bass_soc(chip, soc);

	if (soc == 0) {
		/* Hold SOC to 1% if we have not dropped below cutoff */
		rc = qg_get_vbat_avg(chip, &vbat_uv);
		if (!rc && (vbat_uv >= (vcutoff_uv + VBAT_LOW_HYST_UV))) {
			soc = 1;
			qg_dbg(chip, QG_DEBUG_SOC, "vbat_uv=%duV holding 1%% SOC\n",
						vbat_uv);
		}
	}

	qg_dbg(chip, QG_DEBUG_SOC, "sys_soc=%d adjusted sys_soc=%d\n",
					chip->sys_soc, soc);

	chip->last_adj_ssoc = soc;

	return soc;
}

static void get_next_update_time(struct qpnp_qg *chip)
{
	int soc_points = 0, batt_temp = 0;
	int min_delta_soc_interval_ms = qg_delta_soc_interval_ms;
	int rc = 0, rt_time_ms = 0, full_time_ms = DEFAULT_UPDATE_TIME_MS;

	get_fifo_done_time(chip, false, &full_time_ms);
	get_fifo_done_time(chip, true, &rt_time_ms);

	full_time_ms = CAP(0, DEFAULT_UPDATE_TIME_MS,
				full_time_ms - rt_time_ms);

	soc_points = abs(chip->msoc - chip->catch_up_soc);
	if (chip->maint_soc > 0)
		soc_points = max(abs(chip->msoc - chip->maint_soc), soc_points);
	soc_points /= chip->dt.delta_soc;

	/* Lower the delta soc interval by half at cold */
	rc = qg_get_battery_temp(chip, &batt_temp);
	if (!rc && batt_temp < chip->dt.cold_temp_threshold)
		min_delta_soc_interval_ms = qg_delta_soc_cold_interval_ms;
	else if (chip->maint_soc > 0 && chip->maint_soc >= chip->recharge_soc)
		/* if in maintenance mode scale slower */
		min_delta_soc_interval_ms = qg_maint_soc_update_ms;
	else if (chip->fvss_active)
		min_delta_soc_interval_ms = qg_fvss_delta_soc_interval_ms;

	if (!min_delta_soc_interval_ms)
		min_delta_soc_interval_ms = 1000;	/* 1 second */

	chip->next_wakeup_ms = (full_time_ms / (soc_points + 1))
					- SOC_SCALE_HYST_MS;
	chip->next_wakeup_ms = max(chip->next_wakeup_ms,
				min_delta_soc_interval_ms);

	qg_dbg(chip, QG_DEBUG_SOC, "fifo_full_time=%d secs fifo_real_time=%d secs soc_scale_points=%d\n",
			full_time_ms / 1000, rt_time_ms / 1000, soc_points);
}

static bool is_scaling_required(struct qpnp_qg *chip)
{
	bool input_present = is_input_present(chip);

	if (!chip->profile_loaded)
		return false;

	if (chip->maint_soc > 0 &&
		(abs(chip->maint_soc - chip->msoc) >= chip->dt.delta_soc))
		return true;

	if ((abs(chip->catch_up_soc - chip->msoc) < chip->dt.delta_soc) &&
		chip->catch_up_soc != 0 && chip->catch_up_soc != 100)
		return false;

	if (chip->catch_up_soc == chip->msoc)
		/* SOC has not changed */
		return false;


	if (chip->catch_up_soc > chip->msoc && !input_present)
		/* input is not present and SOC has increased */
		return false;

	if (chip->catch_up_soc > chip->msoc && input_present &&
			(chip->charge_status != POWER_SUPPLY_STATUS_CHARGING &&
			chip->charge_status != POWER_SUPPLY_STATUS_FULL))
		/* USB is present, but not charging */
		return false;

	return true;
}

static bool maint_soc_timeout(struct qpnp_qg *chip)
{
	unsigned long now;
	int rc;

	if (chip->maint_soc < 0)
		return false;

	rc = get_rtc_time(&now);
	if (rc < 0)
		return true;

	/* Do not scale if we have dropped below recharge-soc */
	if (chip->maint_soc < chip->recharge_soc)
		return true;

	if ((now - chip->last_maint_soc_update_time) >=
			(qg_maint_soc_update_ms / 1000)) {
		chip->last_maint_soc_update_time = now;
		return true;
	}

	return false;
}

static void update_msoc(struct qpnp_qg *chip)
{
	int rc = 0, sdam_soc, batt_temp = 0;
	bool input_present = is_input_present(chip);

	if (chip->catch_up_soc > chip->msoc) {
		/* SOC increased */
		if (input_present) /* Increment if input is present */
			chip->msoc += chip->dt.delta_soc;
	} else if (chip->catch_up_soc < chip->msoc) {
		/* SOC dropped */
		chip->msoc -= chip->dt.delta_soc;
	}
	chip->msoc = CAP(0, 100, chip->msoc);

	if (chip->maint_soc > 0 && chip->msoc < chip->maint_soc
				&& maint_soc_timeout(chip)) {
		chip->maint_soc -= chip->dt.delta_soc;
		chip->maint_soc = CAP(0, 100, chip->maint_soc);
	}

	/* maint_soc dropped below msoc, skip using it */
	if (chip->maint_soc <= chip->msoc)
		chip->maint_soc = -EINVAL;

	/* update the SOC register */
	rc = qg_write_monotonic_soc(chip, chip->msoc);
	if (rc < 0)
		pr_err("Failed to update MSOC register rc=%d\n", rc);

	/* update SDAM with the new MSOC */
	sdam_soc = (chip->maint_soc > 0) ? chip->maint_soc : chip->msoc;
	chip->sdam_data[SDAM_SOC] = sdam_soc;
	rc = qg_sdam_write(SDAM_SOC, sdam_soc);
	if (rc < 0)
		pr_err("Failed to update SDAM with MSOC rc=%d\n", rc);

	if (!chip->dt.cl_disable && chip->cl->active) {
		rc = qg_get_battery_temp(chip, &batt_temp);
		if (rc < 0) {
			pr_err("Failed to read BATT_TEMP rc=%d\n", rc);
		} else if (chip->batt_soc >= 0) {
			cap_learning_update(chip->cl, batt_temp, chip->batt_soc,
					chip->charge_status, chip->charge_done,
					input_present, false);
		}
	}

	cycle_count_update(chip->counter,
			DIV_ROUND_CLOSEST(chip->msoc * 255, 100),
			chip->charge_status, chip->charge_done,
			input_present);

	qg_dbg(chip, QG_DEBUG_SOC,
		"SOC scale: Update maint_soc=%d msoc=%d catch_up_soc=%d delta_soc=%d\n",
				chip->maint_soc, chip->msoc,
				chip->catch_up_soc, chip->dt.delta_soc);
}

static void scale_soc_stop(struct qpnp_qg *chip)
{
	chip->next_wakeup_ms = 0;
	alarm_cancel(&chip->alarm_timer);

	qg_dbg(chip, QG_DEBUG_SOC,
			"SOC scale stopped: msoc=%d catch_up_soc=%d\n",
			chip->msoc, chip->catch_up_soc);
}

static void scale_soc_work(struct work_struct *work)
{
	struct qpnp_qg *chip = container_of(work,
			struct qpnp_qg, scale_soc_work);

	mutex_lock(&chip->soc_lock);

	if (!is_scaling_required(chip)) {
		scale_soc_stop(chip);
		goto done;
	}

	update_msoc(chip);

	if (is_scaling_required(chip)) {
		alarm_start_relative(&chip->alarm_timer,
				ms_to_ktime(chip->next_wakeup_ms));
	} else {
		scale_soc_stop(chip);
		goto done_psy;
	}

	qg_dbg(chip, QG_DEBUG_SOC,
		"SOC scale: Work msoc=%d catch_up_soc=%d delta_soc=%d next_wakeup=%d sec\n",
			chip->msoc, chip->catch_up_soc, chip->dt.delta_soc,
			chip->next_wakeup_ms / 1000);

done_psy:
	power_supply_changed(chip->qg_psy);
done:
	pm_relax(chip->dev);
	mutex_unlock(&chip->soc_lock);
}

static enum alarmtimer_restart
	qpnp_msoc_timer(struct alarm *alarm, ktime_t now)
{
	struct qpnp_qg *chip = container_of(alarm,
				struct qpnp_qg, alarm_timer);

	/* timer callback runs in atomic context, cannot use voter */
	pm_stay_awake(chip->dev);
	schedule_work(&chip->scale_soc_work);

	return ALARMTIMER_NORESTART;
}

int qg_scale_soc(struct qpnp_qg *chip, bool force_soc)
{
	int rc = 0;

	mutex_lock(&chip->soc_lock);

	qg_dbg(chip, QG_DEBUG_SOC,
			"SOC scale: Start msoc=%d catch_up_soc=%d delta_soc=%d\n",
			chip->msoc, chip->catch_up_soc, chip->dt.delta_soc);

	if (force_soc) {
		chip->msoc = chip->catch_up_soc;
		rc = qg_write_monotonic_soc(chip, chip->msoc);
		if (rc < 0)
			pr_err("Failed to update MSOC register rc=%d\n", rc);

		qg_dbg(chip, QG_DEBUG_SOC,
			"SOC scale: Forced msoc=%d\n", chip->msoc);
		goto done_psy;
	}

	if (!is_scaling_required(chip)) {
		scale_soc_stop(chip);
		goto done;
	}

	update_msoc(chip);

	if (is_scaling_required(chip)) {
		get_next_update_time(chip);
		alarm_start_relative(&chip->alarm_timer,
					ms_to_ktime(chip->next_wakeup_ms));
	} else {
		scale_soc_stop(chip);
		goto done_psy;
	}

	qg_dbg(chip, QG_DEBUG_SOC,
		"SOC scale: msoc=%d catch_up_soc=%d delta_soc=%d next_wakeup=%d sec\n",
			chip->msoc, chip->catch_up_soc, chip->dt.delta_soc,
			chip->next_wakeup_ms / 1000);

done_psy:
	power_supply_changed(chip->qg_psy);
done:
	mutex_unlock(&chip->soc_lock);
	return rc;
}

int qg_soc_init(struct qpnp_qg *chip)
{
	if (alarmtimer_get_rtcdev()) {
		alarm_init(&chip->alarm_timer, ALARM_BOOTTIME,
			qpnp_msoc_timer);
	} else {
		pr_err("Failed to get soc alarm-timer\n");
		return -EINVAL;
	}
	INIT_WORK(&chip->scale_soc_work, scale_soc_work);

	return 0;
}

void qg_soc_exit(struct qpnp_qg *chip)
{
	alarm_cancel(&chip->alarm_timer);
}
