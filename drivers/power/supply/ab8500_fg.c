// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2012
 *
 * Main and Back-up battery management driver.
 *
 * Note: Backup battery management is required in case of Li-Ion battery and not
 * for capacitive battery. HREF boards have capacitive battery and hence backup
 * battery management is not used and the supported code is available in this
 * driver.
 *
 * Author:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/time64.h>
#include <linux/of.h>
#include <linux/completion.h>
#include <linux/mfd/core.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/iio/consumer.h>
#include <linux/kernel.h>
#include <linux/fixp-arith.h>

#include "ab8500-bm.h"

#define FG_LSB_IN_MA			1627
#define QLSB_NANO_AMP_HOURS_X10		1071
#define INS_CURR_TIMEOUT		(3 * HZ)

#define SEC_TO_SAMPLE(S)		(S * 4)

#define NBR_AVG_SAMPLES			20
#define WAIT_FOR_INST_CURRENT_MAX	70

#define LOW_BAT_CHECK_INTERVAL		(HZ / 16) /* 62.5 ms */

#define VALID_CAPACITY_SEC		(45 * 60) /* 45 minutes */
#define BATT_OK_MIN			2360 /* mV */
#define BATT_OK_INCREMENT		50 /* mV */
#define BATT_OK_MAX_NR_INCREMENTS	0xE

/* FG constants */
#define BATT_OVV			0x01

/**
 * struct ab8500_fg_interrupts - ab8500 fg interrupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_fg_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

enum ab8500_fg_discharge_state {
	AB8500_FG_DISCHARGE_INIT,
	AB8500_FG_DISCHARGE_INITMEASURING,
	AB8500_FG_DISCHARGE_INIT_RECOVERY,
	AB8500_FG_DISCHARGE_RECOVERY,
	AB8500_FG_DISCHARGE_READOUT_INIT,
	AB8500_FG_DISCHARGE_READOUT,
	AB8500_FG_DISCHARGE_WAKEUP,
};

static char *discharge_state[] = {
	"DISCHARGE_INIT",
	"DISCHARGE_INITMEASURING",
	"DISCHARGE_INIT_RECOVERY",
	"DISCHARGE_RECOVERY",
	"DISCHARGE_READOUT_INIT",
	"DISCHARGE_READOUT",
	"DISCHARGE_WAKEUP",
};

enum ab8500_fg_charge_state {
	AB8500_FG_CHARGE_INIT,
	AB8500_FG_CHARGE_READOUT,
};

static char *charge_state[] = {
	"CHARGE_INIT",
	"CHARGE_READOUT",
};

enum ab8500_fg_calibration_state {
	AB8500_FG_CALIB_INIT,
	AB8500_FG_CALIB_WAIT,
	AB8500_FG_CALIB_END,
};

struct ab8500_fg_avg_cap {
	int avg;
	int samples[NBR_AVG_SAMPLES];
	time64_t time_stamps[NBR_AVG_SAMPLES];
	int pos;
	int nbr_samples;
	int sum;
};

struct ab8500_fg_cap_scaling {
	bool enable;
	int cap_to_scale[2];
	int disable_cap_level;
	int scaled_cap;
};

struct ab8500_fg_battery_capacity {
	int max_mah_design;
	int max_mah;
	int mah;
	int permille;
	int level;
	int prev_mah;
	int prev_percent;
	int prev_level;
	int user_mah;
	struct ab8500_fg_cap_scaling cap_scale;
};

struct ab8500_fg_flags {
	bool fg_enabled;
	bool conv_done;
	bool charging;
	bool fully_charged;
	bool force_full;
	bool low_bat_delay;
	bool low_bat;
	bool bat_ovv;
	bool batt_unknown;
	bool calibrate;
	bool user_cap;
	bool batt_id_received;
};

struct inst_curr_result_list {
	struct list_head list;
	int *result;
};

/**
 * struct ab8500_fg - ab8500 FG device information
 * @dev:		Pointer to the structure device
 * @node:		a list of AB8500 FGs, hence prepared for reentrance
 * @irq			holds the CCEOC interrupt number
 * @vbat_uv:		Battery voltage in uV
 * @vbat_nom_uv:	Nominal battery voltage in uV
 * @inst_curr_ua:	Instantenous battery current in uA
 * @avg_curr_ua:	Average battery current in uA
 * @bat_temp		battery temperature
 * @fg_samples:		Number of samples used in the FG accumulation
 * @accu_charge:	Accumulated charge from the last conversion
 * @recovery_cnt:	Counter for recovery mode
 * @high_curr_cnt:	Counter for high current mode
 * @init_cnt:		Counter for init mode
 * @low_bat_cnt		Counter for number of consecutive low battery measures
 * @nbr_cceoc_irq_cnt	Counter for number of CCEOC irqs received since enabled
 * @recovery_needed:	Indicate if recovery is needed
 * @high_curr_mode:	Indicate if we're in high current mode
 * @init_capacity:	Indicate if initial capacity measuring should be done
 * @turn_off_fg:	True if fg was off before current measurement
 * @calib_state		State during offset calibration
 * @discharge_state:	Current discharge state
 * @charge_state:	Current charge state
 * @ab8500_fg_started	Completion struct used for the instant current start
 * @ab8500_fg_complete	Completion struct used for the instant current reading
 * @flags:		Structure for information about events triggered
 * @bat_cap:		Structure for battery capacity specific parameters
 * @avg_cap:		Average capacity filter
 * @parent:		Pointer to the struct ab8500
 * @main_bat_v:		ADC channel for the main battery voltage
 * @bm:           	Platform specific battery management information
 * @fg_psy:		Structure that holds the FG specific battery properties
 * @fg_wq:		Work queue for running the FG algorithm
 * @fg_periodic_work:	Work to run the FG algorithm periodically
 * @fg_low_bat_work:	Work to check low bat condition
 * @fg_reinit_work	Work used to reset and reinitialise the FG algorithm
 * @fg_work:		Work to run the FG algorithm instantly
 * @fg_acc_cur_work:	Work to read the FG accumulator
 * @fg_check_hw_failure_work:	Work for checking HW state
 * @cc_lock:		Mutex for locking the CC
 * @fg_kobject:		Structure of type kobject
 */
struct ab8500_fg {
	struct device *dev;
	struct list_head node;
	int irq;
	int vbat_uv;
	int vbat_nom_uv;
	int inst_curr_ua;
	int avg_curr_ua;
	int bat_temp;
	int fg_samples;
	int accu_charge;
	int recovery_cnt;
	int high_curr_cnt;
	int init_cnt;
	int low_bat_cnt;
	int nbr_cceoc_irq_cnt;
	bool recovery_needed;
	bool high_curr_mode;
	bool init_capacity;
	bool turn_off_fg;
	enum ab8500_fg_calibration_state calib_state;
	enum ab8500_fg_discharge_state discharge_state;
	enum ab8500_fg_charge_state charge_state;
	struct completion ab8500_fg_started;
	struct completion ab8500_fg_complete;
	struct ab8500_fg_flags flags;
	struct ab8500_fg_battery_capacity bat_cap;
	struct ab8500_fg_avg_cap avg_cap;
	struct ab8500 *parent;
	struct iio_channel *main_bat_v;
	struct ab8500_bm_data *bm;
	struct power_supply *fg_psy;
	struct workqueue_struct *fg_wq;
	struct delayed_work fg_periodic_work;
	struct delayed_work fg_low_bat_work;
	struct delayed_work fg_reinit_work;
	struct work_struct fg_work;
	struct work_struct fg_acc_cur_work;
	struct delayed_work fg_check_hw_failure_work;
	struct mutex cc_lock;
	struct kobject fg_kobject;
};
static LIST_HEAD(ab8500_fg_list);

/**
 * ab8500_fg_get() - returns a reference to the primary AB8500 fuel gauge
 * (i.e. the first fuel gauge in the instance list)
 */
struct ab8500_fg *ab8500_fg_get(void)
{
	return list_first_entry_or_null(&ab8500_fg_list, struct ab8500_fg,
					node);
}

/* Main battery properties */
static enum power_supply_property ab8500_fg_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

/*
 * This array maps the raw hex value to lowbat voltage used by the AB8500
 * Values taken from the UM0836, in microvolts.
 */
static int ab8500_fg_lowbat_voltage_map[] = {
	2300000,
	2325000,
	2350000,
	2375000,
	2400000,
	2425000,
	2450000,
	2475000,
	2500000,
	2525000,
	2550000,
	2575000,
	2600000,
	2625000,
	2650000,
	2675000,
	2700000,
	2725000,
	2750000,
	2775000,
	2800000,
	2825000,
	2850000,
	2875000,
	2900000,
	2925000,
	2950000,
	2975000,
	3000000,
	3025000,
	3050000,
	3075000,
	3100000,
	3125000,
	3150000,
	3175000,
	3200000,
	3225000,
	3250000,
	3275000,
	3300000,
	3325000,
	3350000,
	3375000,
	3400000,
	3425000,
	3450000,
	3475000,
	3500000,
	3525000,
	3550000,
	3575000,
	3600000,
	3625000,
	3650000,
	3675000,
	3700000,
	3725000,
	3750000,
	3775000,
	3800000,
	3825000,
	3850000,
	3850000,
};

static u8 ab8500_volt_to_regval(int voltage_uv)
{
	int i;

	if (voltage_uv < ab8500_fg_lowbat_voltage_map[0])
		return 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_fg_lowbat_voltage_map); i++) {
		if (voltage_uv < ab8500_fg_lowbat_voltage_map[i])
			return (u8) i - 1;
	}

	/* If not captured above, return index of last element */
	return (u8) ARRAY_SIZE(ab8500_fg_lowbat_voltage_map) - 1;
}

/**
 * ab8500_fg_is_low_curr() - Low or high current mode
 * @di:		pointer to the ab8500_fg structure
 * @curr_ua:	the current to base or our decision on in microampere
 *
 * Low current mode if the current consumption is below a certain threshold
 */
static int ab8500_fg_is_low_curr(struct ab8500_fg *di, int curr_ua)
{
	/*
	 * We want to know if we're in low current mode
	 */
	if (curr_ua > -di->bm->fg_params->high_curr_threshold_ua)
		return true;
	else
		return false;
}

/**
 * ab8500_fg_add_cap_sample() - Add capacity to average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to add to the filter
 *
 * A capacity is added to the filter and a new mean capacity is calculated and
 * returned
 */
static int ab8500_fg_add_cap_sample(struct ab8500_fg *di, int sample)
{
	time64_t now = ktime_get_boottime_seconds();
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	do {
		avg->sum += sample - avg->samples[avg->pos];
		avg->samples[avg->pos] = sample;
		avg->time_stamps[avg->pos] = now;
		avg->pos++;

		if (avg->pos == NBR_AVG_SAMPLES)
			avg->pos = 0;

		if (avg->nbr_samples < NBR_AVG_SAMPLES)
			avg->nbr_samples++;

		/*
		 * Check the time stamp for each sample. If too old,
		 * replace with latest sample
		 */
	} while (now - VALID_CAPACITY_SEC > avg->time_stamps[avg->pos]);

	avg->avg = avg->sum / avg->nbr_samples;

	return avg->avg;
}

/**
 * ab8500_fg_clear_cap_samples() - Clear average filter
 * @di:		pointer to the ab8500_fg structure
 *
 * The capacity filter is is reset to zero.
 */
static void ab8500_fg_clear_cap_samples(struct ab8500_fg *di)
{
	int i;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	avg->pos = 0;
	avg->nbr_samples = 0;
	avg->sum = 0;
	avg->avg = 0;

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = 0;
		avg->time_stamps[i] = 0;
	}
}

/**
 * ab8500_fg_fill_cap_sample() - Fill average filter
 * @di:		pointer to the ab8500_fg structure
 * @sample:	the capacity in mAh to fill the filter with
 *
 * The capacity filter is filled with a capacity in mAh
 */
static void ab8500_fg_fill_cap_sample(struct ab8500_fg *di, int sample)
{
	int i;
	time64_t now;
	struct ab8500_fg_avg_cap *avg = &di->avg_cap;

	now = ktime_get_boottime_seconds();

	for (i = 0; i < NBR_AVG_SAMPLES; i++) {
		avg->samples[i] = sample;
		avg->time_stamps[i] = now;
	}

	avg->pos = 0;
	avg->nbr_samples = NBR_AVG_SAMPLES;
	avg->sum = sample * NBR_AVG_SAMPLES;
	avg->avg = sample;
}

/**
 * ab8500_fg_coulomb_counter() - enable coulomb counter
 * @di:		pointer to the ab8500_fg structure
 * @enable:	enable/disable
 *
 * Enable/Disable coulomb counter.
 * On failure returns negative value.
 */
static int ab8500_fg_coulomb_counter(struct ab8500_fg *di, bool enable)
{
	int ret = 0;
	mutex_lock(&di->cc_lock);
	if (enable) {
		/* To be able to reprogram the number of samples, we have to
		 * first stop the CC and then enable it again */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0x00);
		if (ret)
			goto cc_err;

		/* Program the samples */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU,
			di->fg_samples);
		if (ret)
			goto cc_err;

		/* Start the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG,
			(CC_DEEP_SLEEP_ENA | CC_PWR_UP_ENA));
		if (ret)
			goto cc_err;

		di->flags.fg_enabled = true;
	} else {
		/* Clear any pending read requests */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			(RESET_ACCU | READ_REQ), 0);
		if (ret)
			goto cc_err;

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU_CTRL, 0);
		if (ret)
			goto cc_err;

		/* Stop the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0);
		if (ret)
			goto cc_err;

		di->flags.fg_enabled = false;

	}
	dev_dbg(di->dev, " CC enabled: %d Samples: %d\n",
		enable, di->fg_samples);

	mutex_unlock(&di->cc_lock);

	return ret;
cc_err:
	dev_err(di->dev, "%s Enabling coulomb counter failed\n", __func__);
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_start() - start battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 0 or error code
 * Note: This is part "one" and has to be called before
 * ab8500_fg_inst_curr_finalize()
 */
int ab8500_fg_inst_curr_start(struct ab8500_fg *di)
{
	u8 reg_val;
	int ret;

	mutex_lock(&di->cc_lock);

	di->nbr_cceoc_irq_cnt = 0;
	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8500_RTC_CC_CONF_REG, &reg_val);
	if (ret < 0)
		goto fail;

	if (!(reg_val & CC_PWR_UP_ENA)) {
		dev_dbg(di->dev, "%s Enable FG\n", __func__);
		di->turn_off_fg = true;

		/* Program the samples */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_NCOV_ACCU,
			SEC_TO_SAMPLE(10));
		if (ret)
			goto fail;

		/* Start the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG,
			(CC_DEEP_SLEEP_ENA | CC_PWR_UP_ENA));
		if (ret)
			goto fail;
	} else {
		di->turn_off_fg = false;
	}

	/* Return and WFI */
	reinit_completion(&di->ab8500_fg_started);
	reinit_completion(&di->ab8500_fg_complete);
	enable_irq(di->irq);

	/* Note: cc_lock is still locked */
	return 0;
fail:
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_started() - check if fg conversion has started
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 1 if conversion started, 0 if still waiting
 */
int ab8500_fg_inst_curr_started(struct ab8500_fg *di)
{
	return completion_done(&di->ab8500_fg_started);
}

/**
 * ab8500_fg_inst_curr_done() - check if fg conversion is done
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns 1 if conversion done, 0 if still waiting
 */
int ab8500_fg_inst_curr_done(struct ab8500_fg *di)
{
	return completion_done(&di->ab8500_fg_complete);
}

/**
 * ab8500_fg_inst_curr_finalize() - battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 * @curr_ua:	battery instantenous current in microampere (on success)
 *
 * Returns 0 or an error code
 * Note: This is part "two" and has to be called at earliest 250 ms
 * after ab8500_fg_inst_curr_start()
 */
int ab8500_fg_inst_curr_finalize(struct ab8500_fg *di, int *curr_ua)
{
	u8 low, high;
	int val;
	int ret;
	unsigned long timeout;

	if (!completion_done(&di->ab8500_fg_complete)) {
		timeout = wait_for_completion_timeout(
			&di->ab8500_fg_complete,
			INS_CURR_TIMEOUT);
		dev_dbg(di->dev, "Finalize time: %d ms\n",
			jiffies_to_msecs(INS_CURR_TIMEOUT - timeout));
		if (!timeout) {
			ret = -ETIME;
			disable_irq(di->irq);
			di->nbr_cceoc_irq_cnt = 0;
			dev_err(di->dev, "completion timed out [%d]\n",
				__LINE__);
			goto fail;
		}
	}

	disable_irq(di->irq);
	di->nbr_cceoc_irq_cnt = 0;

	ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			READ_REQ, READ_REQ);

	/* 100uS between read request and read is needed */
	usleep_range(100, 100);

	/* Read CC Sample conversion value Low and high */
	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_SMPL_CNVL_REG,  &low);
	if (ret < 0)
		goto fail;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_SMPL_CNVH_REG,  &high);
	if (ret < 0)
		goto fail;

	/*
	 * negative value for Discharging
	 * convert 2's complement into decimal
	 */
	if (high & 0x10)
		val = (low | (high << 8) | 0xFFFFE000);
	else
		val = (low | (high << 8));

	/*
	 * Convert to unit value in mA
	 * Full scale input voltage is
	 * 63.160mV => LSB = 63.160mV/(4096*res) = 1.542.000 uA
	 * Given a 250ms conversion cycle time the LSB corresponds
	 * to 107.1 nAh. Convert to current by dividing by the conversion
	 * time in hours (250ms = 1 / (3600 * 4)h)
	 * 107.1nAh assumes 10mOhm, but fg_res is in 0.1mOhm
	 */
	val = (val * QLSB_NANO_AMP_HOURS_X10 * 36 * 4) / di->bm->fg_res;

	if (di->turn_off_fg) {
		dev_dbg(di->dev, "%s Disable FG\n", __func__);

		/* Clear any pending read requests */
		ret = abx500_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG, 0);
		if (ret)
			goto fail;

		/* Stop the CC */
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8500_RTC_CC_CONF_REG, 0);
		if (ret)
			goto fail;
	}
	mutex_unlock(&di->cc_lock);
	*curr_ua = val;

	return 0;
fail:
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_inst_curr_blocking() - battery instantaneous current
 * @di:         pointer to the ab8500_fg structure
 *
 * Returns battery instantenous current in microampere (on success)
 * else error code
 */
int ab8500_fg_inst_curr_blocking(struct ab8500_fg *di)
{
	int ret;
	unsigned long timeout;
	int curr_ua = 0;

	ret = ab8500_fg_inst_curr_start(di);
	if (ret) {
		dev_err(di->dev, "Failed to initialize fg_inst\n");
		return 0;
	}

	/* Wait for CC to actually start */
	if (!completion_done(&di->ab8500_fg_started)) {
		timeout = wait_for_completion_timeout(
			&di->ab8500_fg_started,
			INS_CURR_TIMEOUT);
		dev_dbg(di->dev, "Start time: %d ms\n",
			jiffies_to_msecs(INS_CURR_TIMEOUT - timeout));
		if (!timeout) {
			ret = -ETIME;
			dev_err(di->dev, "completion timed out [%d]\n",
				__LINE__);
			goto fail;
		}
	}

	ret = ab8500_fg_inst_curr_finalize(di, &curr_ua);
	if (ret) {
		dev_err(di->dev, "Failed to finalize fg_inst\n");
		return 0;
	}

	dev_dbg(di->dev, "%s instant current: %d uA", __func__, curr_ua);
	return curr_ua;
fail:
	disable_irq(di->irq);
	mutex_unlock(&di->cc_lock);
	return ret;
}

/**
 * ab8500_fg_acc_cur_work() - average battery current
 * @work:	pointer to the work_struct structure
 *
 * Updated the average battery current obtained from the
 * coulomb counter.
 */
static void ab8500_fg_acc_cur_work(struct work_struct *work)
{
	int val;
	int ret;
	u8 low, med, high;

	struct ab8500_fg *di = container_of(work,
		struct ab8500_fg, fg_acc_cur_work);

	mutex_lock(&di->cc_lock);
	ret = abx500_set_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_CTRL, RD_NCONV_ACCU_REQ);
	if (ret)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_LOW,  &low);
	if (ret < 0)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_MED,  &med);
	if (ret < 0)
		goto exit;

	ret = abx500_get_register_interruptible(di->dev, AB8500_GAS_GAUGE,
		AB8500_GASG_CC_NCOV_ACCU_HIGH, &high);
	if (ret < 0)
		goto exit;

	/* Check for sign bit in case of negative value, 2's complement */
	if (high & 0x10)
		val = (low | (med << 8) | (high << 16) | 0xFFE00000);
	else
		val = (low | (med << 8) | (high << 16));

	/*
	 * Convert to uAh
	 * Given a 250ms conversion cycle time the LSB corresponds
	 * to 112.9 nAh.
	 * 112.9nAh assumes 10mOhm, but fg_res is in 0.1mOhm
	 */
	di->accu_charge = (val * QLSB_NANO_AMP_HOURS_X10) /
		(100 * di->bm->fg_res);

	/*
	 * Convert to unit value in uA
	 * by dividing by the conversion
	 * time in hours (= samples / (3600 * 4)h)
	 */
	di->avg_curr_ua = (val * QLSB_NANO_AMP_HOURS_X10 * 36) /
		(di->bm->fg_res * (di->fg_samples / 4));

	di->flags.conv_done = true;

	mutex_unlock(&di->cc_lock);

	queue_work(di->fg_wq, &di->fg_work);

	dev_dbg(di->dev, "fg_res: %d, fg_samples: %d, gasg: %d, accu_charge: %d \n",
				di->bm->fg_res, di->fg_samples, val, di->accu_charge);
	return;
exit:
	dev_err(di->dev,
		"Failed to read or write gas gauge registers\n");
	mutex_unlock(&di->cc_lock);
	queue_work(di->fg_wq, &di->fg_work);
}

/**
 * ab8500_fg_bat_voltage() - get battery voltage
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery voltage in microvolts (on success) else error code
 */
static int ab8500_fg_bat_voltage(struct ab8500_fg *di)
{
	int vbat, ret;
	static int prev;

	ret = iio_read_channel_processed(di->main_bat_v, &vbat);
	if (ret < 0) {
		dev_err(di->dev,
			"%s ADC conversion failed, using previous value\n",
			__func__);
		return prev;
	}

	/* IIO returns millivolts but we want microvolts */
	vbat *= 1000;
	prev = vbat;
	return vbat;
}

/**
 * ab8500_fg_volt_to_capacity() - Voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 * @voltage_uv:	The voltage to convert to a capacity in microvolt
 *
 * Returns battery capacity in per mille based on voltage
 */
static int ab8500_fg_volt_to_capacity(struct ab8500_fg *di, int voltage_uv)
{
	struct power_supply_battery_info *bi = di->bm->bi;

	/* Multiply by 10 because the capacity is tracked in per mille */
	return power_supply_batinfo_ocv2cap(bi, voltage_uv, di->bat_temp) *  10;
}

/**
 * ab8500_fg_uncomp_volt_to_capacity() - Uncompensated voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery capacity based on battery voltage that is not compensated
 * for the voltage drop due to the load
 */
static int ab8500_fg_uncomp_volt_to_capacity(struct ab8500_fg *di)
{
	di->vbat_uv = ab8500_fg_bat_voltage(di);
	return ab8500_fg_volt_to_capacity(di, di->vbat_uv);
}

/**
 * ab8500_fg_battery_resistance() - Returns the battery inner resistance
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery inner resistance added with the fuel gauge resistor value
 * to get the total resistance in the whole link from gnd to bat+ node
 * in milliohm.
 */
static int ab8500_fg_battery_resistance(struct ab8500_fg *di)
{
	struct power_supply_battery_info *bi = di->bm->bi;
	int resistance_percent = 0;
	int resistance;

	resistance_percent = power_supply_temp2resist_simple(bi->resist_table,
						 bi->resist_table_size,
						 di->bat_temp / 10);
	/*
	 * We get a percentage of factory resistance here so first get
	 * the factory resistance in milliohms then calculate how much
	 * resistance we have at this temperature.
	 */
	resistance = (bi->factory_internal_resistance_uohm / 1000);
	resistance = resistance * resistance_percent / 100;

	dev_dbg(di->dev, "%s Temp: %d battery internal resistance: %d"
	    " fg resistance %d, total: %d (mOhm)\n",
		__func__, di->bat_temp, resistance, di->bm->fg_res / 10,
		(di->bm->fg_res / 10) + resistance);

	/* fg_res variable is in 0.1mOhm */
	resistance += di->bm->fg_res / 10;

	return resistance;
}

/**
 * ab8500_fg_load_comp_volt_to_capacity() - Load compensated voltage based capacity
 * @di:		pointer to the ab8500_fg structure
 *
 * Returns battery capacity based on battery voltage that is load compensated
 * for the voltage drop
 */
static int ab8500_fg_load_comp_volt_to_capacity(struct ab8500_fg *di)
{
	int vbat_comp_uv, res;
	int i = 0;
	int vbat_uv = 0;

	ab8500_fg_inst_curr_start(di);

	do {
		vbat_uv += ab8500_fg_bat_voltage(di);
		i++;
		usleep_range(5000, 6000);
	} while (!ab8500_fg_inst_curr_done(di) &&
		 i <= WAIT_FOR_INST_CURRENT_MAX);

	if (i > WAIT_FOR_INST_CURRENT_MAX) {
		dev_err(di->dev,
			"TIMEOUT: return capacity based on uncompensated measurement of VBAT\n");
		goto calc_cap;
	}

	ab8500_fg_inst_curr_finalize(di, &di->inst_curr_ua);

calc_cap:
	di->vbat_uv = vbat_uv / i;
	res = ab8500_fg_battery_resistance(di);

	/*
	 * Use Ohms law to get the load compensated voltage.
	 * Divide by 1000 to get from milliohms to ohms.
	 */
	vbat_comp_uv = di->vbat_uv - (di->inst_curr_ua * res) / 1000;

	dev_dbg(di->dev, "%s Measured Vbat: %d uV,Compensated Vbat %d uV, "
		"R: %d mOhm, Current: %d uA Vbat Samples: %d\n",
		__func__, di->vbat_uv, vbat_comp_uv, res, di->inst_curr_ua, i);

	return ab8500_fg_volt_to_capacity(di, vbat_comp_uv);
}

/**
 * ab8500_fg_convert_mah_to_permille() - Capacity in mAh to permille
 * @di:		pointer to the ab8500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in permille
 */
static int ab8500_fg_convert_mah_to_permille(struct ab8500_fg *di, int cap_mah)
{
	return (cap_mah * 1000) / di->bat_cap.max_mah_design;
}

/**
 * ab8500_fg_convert_permille_to_mah() - Capacity in permille to mAh
 * @di:		pointer to the ab8500_fg structure
 * @cap_pm:	capacity in permille
 *
 * Converts capacity in permille to capacity in mAh
 */
static int ab8500_fg_convert_permille_to_mah(struct ab8500_fg *di, int cap_pm)
{
	return cap_pm * di->bat_cap.max_mah_design / 1000;
}

/**
 * ab8500_fg_convert_mah_to_uwh() - Capacity in mAh to uWh
 * @di:		pointer to the ab8500_fg structure
 * @cap_mah:	capacity in mAh
 *
 * Converts capacity in mAh to capacity in uWh
 */
static int ab8500_fg_convert_mah_to_uwh(struct ab8500_fg *di, int cap_mah)
{
	u64 div_res;
	u32 div_rem;

	/*
	 * Capacity is in milli ampere hours (10^-3)Ah
	 * Nominal voltage is in microvolts (10^-6)V
	 * divide by 1000000 after multiplication to get to mWh
	 */
	div_res = ((u64) cap_mah) * ((u64) di->vbat_nom_uv);
	div_rem = do_div(div_res, 1000000);

	/* Make sure to round upwards if necessary */
	if (div_rem >= 1000000 / 2)
		div_res++;

	return (int) div_res;
}

/**
 * ab8500_fg_calc_cap_charging() - Calculate remaining capacity while charging
 * @di:		pointer to the ab8500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. The filter is filled with this capacity
 */
static int ab8500_fg_calc_cap_charging(struct ab8500_fg *di)
{
	dev_dbg(di->dev, "%s cap_mah %d accu_charge %d\n",
		__func__,
		di->bat_cap.mah,
		di->accu_charge);

	/* Capacity should not be less than 0 */
	if (di->bat_cap.mah + di->accu_charge > 0)
		di->bat_cap.mah += di->accu_charge;
	else
		di->bat_cap.mah = 0;
	/*
	 * We force capacity to 100% once when the algorithm
	 * reports that it's full.
	 */
	if (di->bat_cap.mah >= di->bat_cap.max_mah_design ||
		di->flags.force_full) {
		di->bat_cap.mah = di->bat_cap.max_mah_design;
	}

	ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	/* We need to update battery voltage and inst current when charging */
	di->vbat_uv = ab8500_fg_bat_voltage(di);
	di->inst_curr_ua = ab8500_fg_inst_curr_blocking(di);

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_calc_cap_discharge_voltage() - Capacity in discharge with voltage
 * @di:		pointer to the ab8500_fg structure
 * @comp:	if voltage should be load compensated before capacity calc
 *
 * Return the capacity in mAh based on the battery voltage. The voltage can
 * either be load compensated or not. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab8500_fg_calc_cap_discharge_voltage(struct ab8500_fg *di, bool comp)
{
	int permille, mah;

	if (comp)
		permille = ab8500_fg_load_comp_volt_to_capacity(di);
	else
		permille = ab8500_fg_uncomp_volt_to_capacity(di);

	mah = ab8500_fg_convert_permille_to_mah(di, permille);

	di->bat_cap.mah = ab8500_fg_add_cap_sample(di, mah);
	di->bat_cap.permille =
		ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_calc_cap_discharge_fg() - Capacity in discharge with FG
 * @di:		pointer to the ab8500_fg structure
 *
 * Return the capacity in mAh based on previous calculated capcity and the FG
 * accumulator register value. This value is added to the filter and a
 * new mean value is calculated and returned.
 */
static int ab8500_fg_calc_cap_discharge_fg(struct ab8500_fg *di)
{
	int permille_volt, permille;

	dev_dbg(di->dev, "%s cap_mah %d accu_charge %d\n",
		__func__,
		di->bat_cap.mah,
		di->accu_charge);

	/* Capacity should not be less than 0 */
	if (di->bat_cap.mah + di->accu_charge > 0)
		di->bat_cap.mah += di->accu_charge;
	else
		di->bat_cap.mah = 0;

	if (di->bat_cap.mah >= di->bat_cap.max_mah_design)
		di->bat_cap.mah = di->bat_cap.max_mah_design;

	/*
	 * Check against voltage based capacity. It can not be lower
	 * than what the uncompensated voltage says
	 */
	permille = ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
	permille_volt = ab8500_fg_uncomp_volt_to_capacity(di);

	if (permille < permille_volt) {
		di->bat_cap.permille = permille_volt;
		di->bat_cap.mah = ab8500_fg_convert_permille_to_mah(di,
			di->bat_cap.permille);

		dev_dbg(di->dev, "%s voltage based: perm %d perm_volt %d\n",
			__func__,
			permille,
			permille_volt);

		ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
	} else {
		ab8500_fg_fill_cap_sample(di, di->bat_cap.mah);
		di->bat_cap.permille =
			ab8500_fg_convert_mah_to_permille(di, di->bat_cap.mah);
	}

	return di->bat_cap.mah;
}

/**
 * ab8500_fg_capacity_level() - Get the battery capacity level
 * @di:		pointer to the ab8500_fg structure
 *
 * Get the battery capacity level based on the capacity in percent
 */
static int ab8500_fg_capacity_level(struct ab8500_fg *di)
{
	int ret, percent;

	percent = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	if (percent <= di->bm->cap_levels->critical ||
		di->flags.low_bat)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (percent <= di->bm->cap_levels->low)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (percent <= di->bm->cap_levels->normal)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (percent <= di->bm->cap_levels->high)
		ret = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else
		ret = POWER_SUPPLY_CAPACITY_LEVEL_FULL;

	return ret;
}

/**
 * ab8500_fg_calculate_scaled_capacity() - Capacity scaling
 * @di:		pointer to the ab8500_fg structure
 *
 * Calculates the capacity to be shown to upper layers. Scales the capacity
 * to have 100% as a reference from the actual capacity upon removal of charger
 * when charging is in maintenance mode.
 */
static int ab8500_fg_calculate_scaled_capacity(struct ab8500_fg *di)
{
	struct ab8500_fg_cap_scaling *cs = &di->bat_cap.cap_scale;
	int capacity = di->bat_cap.prev_percent;

	if (!cs->enable)
		return capacity;

	/*
	 * As long as we are in fully charge mode scale the capacity
	 * to show 100%.
	 */
	if (di->flags.fully_charged) {
		cs->cap_to_scale[0] = 100;
		cs->cap_to_scale[1] =
			max(capacity, di->bm->fg_params->maint_thres);
		dev_dbg(di->dev, "Scale cap with %d/%d\n",
			 cs->cap_to_scale[0], cs->cap_to_scale[1]);
	}

	/* Calculates the scaled capacity. */
	if ((cs->cap_to_scale[0] != cs->cap_to_scale[1])
					&& (cs->cap_to_scale[1] > 0))
		capacity = min(100,
				 DIV_ROUND_CLOSEST(di->bat_cap.prev_percent *
						 cs->cap_to_scale[0],
						 cs->cap_to_scale[1]));

	if (di->flags.charging) {
		if (capacity < cs->disable_cap_level) {
			cs->disable_cap_level = capacity;
			dev_dbg(di->dev, "Cap to stop scale lowered %d%%\n",
				cs->disable_cap_level);
		} else if (!di->flags.fully_charged) {
			if (di->bat_cap.prev_percent >=
			    cs->disable_cap_level) {
				dev_dbg(di->dev, "Disabling scaled capacity\n");
				cs->enable = false;
				capacity = di->bat_cap.prev_percent;
			} else {
				dev_dbg(di->dev,
					"Waiting in cap to level %d%%\n",
					cs->disable_cap_level);
				capacity = cs->disable_cap_level;
			}
		}
	}

	return capacity;
}

/**
 * ab8500_fg_update_cap_scalers() - Capacity scaling
 * @di:		pointer to the ab8500_fg structure
 *
 * To be called when state change from charge<->discharge to update
 * the capacity scalers.
 */
static void ab8500_fg_update_cap_scalers(struct ab8500_fg *di)
{
	struct ab8500_fg_cap_scaling *cs = &di->bat_cap.cap_scale;

	if (!cs->enable)
		return;
	if (di->flags.charging) {
		di->bat_cap.cap_scale.disable_cap_level =
			di->bat_cap.cap_scale.scaled_cap;
		dev_dbg(di->dev, "Cap to stop scale at charge %d%%\n",
				di->bat_cap.cap_scale.disable_cap_level);
	} else {
		if (cs->scaled_cap != 100) {
			cs->cap_to_scale[0] = cs->scaled_cap;
			cs->cap_to_scale[1] = di->bat_cap.prev_percent;
		} else {
			cs->cap_to_scale[0] = 100;
			cs->cap_to_scale[1] =
				max(di->bat_cap.prev_percent,
				    di->bm->fg_params->maint_thres);
		}

		dev_dbg(di->dev, "Cap to scale at discharge %d/%d\n",
				cs->cap_to_scale[0], cs->cap_to_scale[1]);
	}
}

/**
 * ab8500_fg_check_capacity_limits() - Check if capacity has changed
 * @di:		pointer to the ab8500_fg structure
 * @init:	capacity is allowed to go up in init mode
 *
 * Check if capacity or capacity limit has changed and notify the system
 * about it using the power_supply framework
 */
static void ab8500_fg_check_capacity_limits(struct ab8500_fg *di, bool init)
{
	bool changed = false;
	int percent = DIV_ROUND_CLOSEST(di->bat_cap.permille, 10);

	di->bat_cap.level = ab8500_fg_capacity_level(di);

	if (di->bat_cap.level != di->bat_cap.prev_level) {
		/*
		 * We do not allow reported capacity level to go up
		 * unless we're charging or if we're in init
		 */
		if (!(!di->flags.charging && di->bat_cap.level >
			di->bat_cap.prev_level) || init) {
			dev_dbg(di->dev, "level changed from %d to %d\n",
				di->bat_cap.prev_level,
				di->bat_cap.level);
			di->bat_cap.prev_level = di->bat_cap.level;
			changed = true;
		} else {
			dev_dbg(di->dev, "level not allowed to go up "
				"since no charger is connected: %d to %d\n",
				di->bat_cap.prev_level,
				di->bat_cap.level);
		}
	}

	/*
	 * If we have received the LOW_BAT IRQ, set capacity to 0 to initiate
	 * shutdown
	 */
	if (di->flags.low_bat) {
		dev_dbg(di->dev, "Battery low, set capacity to 0\n");
		di->bat_cap.prev_percent = 0;
		di->bat_cap.permille = 0;
		percent = 0;
		di->bat_cap.prev_mah = 0;
		di->bat_cap.mah = 0;
		changed = true;
	} else if (di->flags.fully_charged) {
		/*
		 * We report 100% if algorithm reported fully charged
		 * and show 100% during maintenance charging (scaling).
		 */
		if (di->flags.force_full) {
			di->bat_cap.prev_percent = percent;
			di->bat_cap.prev_mah = di->bat_cap.mah;

			changed = true;

			if (!di->bat_cap.cap_scale.enable &&
						di->bm->capacity_scaling) {
				di->bat_cap.cap_scale.enable = true;
				di->bat_cap.cap_scale.cap_to_scale[0] = 100;
				di->bat_cap.cap_scale.cap_to_scale[1] =
						di->bat_cap.prev_percent;
				di->bat_cap.cap_scale.disable_cap_level = 100;
			}
		} else if (di->bat_cap.prev_percent != percent) {
			dev_dbg(di->dev,
				"battery reported full "
				"but capacity dropping: %d\n",
				percent);
			di->bat_cap.prev_percent = percent;
			di->bat_cap.prev_mah = di->bat_cap.mah;

			changed = true;
		}
	} else if (di->bat_cap.prev_percent != percent) {
		if (percent == 0) {
			/*
			 * We will not report 0% unless we've got
			 * the LOW_BAT IRQ, no matter what the FG
			 * algorithm says.
			 */
			di->bat_cap.prev_percent = 1;
			percent = 1;

			changed = true;
		} else if (!(!di->flags.charging &&
			percent > di->bat_cap.prev_percent) || init) {
			/*
			 * We do not allow reported capacity to go up
			 * unless we're charging or if we're in init
			 */
			dev_dbg(di->dev,
				"capacity changed from %d to %d (%d)\n",
				di->bat_cap.prev_percent,
				percent,
				di->bat_cap.permille);
			di->bat_cap.prev_percent = percent;
			di->bat_cap.prev_mah = di->bat_cap.mah;

			changed = true;
		} else {
			dev_dbg(di->dev, "capacity not allowed to go up since "
				"no charger is connected: %d to %d (%d)\n",
				di->bat_cap.prev_percent,
				percent,
				di->bat_cap.permille);
		}
	}

	if (changed) {
		if (di->bm->capacity_scaling) {
			di->bat_cap.cap_scale.scaled_cap =
				ab8500_fg_calculate_scaled_capacity(di);

			dev_info(di->dev, "capacity=%d (%d)\n",
				di->bat_cap.prev_percent,
				di->bat_cap.cap_scale.scaled_cap);
		}
		power_supply_changed(di->fg_psy);
		if (di->flags.fully_charged && di->flags.force_full) {
			dev_dbg(di->dev, "Battery full, notifying.\n");
			di->flags.force_full = false;
			sysfs_notify(&di->fg_kobject, NULL, "charge_full");
		}
		sysfs_notify(&di->fg_kobject, NULL, "charge_now");
	}
}

static void ab8500_fg_charge_state_to(struct ab8500_fg *di,
	enum ab8500_fg_charge_state new_state)
{
	dev_dbg(di->dev, "Charge state from %d [%s] to %d [%s]\n",
		di->charge_state,
		charge_state[di->charge_state],
		new_state,
		charge_state[new_state]);

	di->charge_state = new_state;
}

static void ab8500_fg_discharge_state_to(struct ab8500_fg *di,
	enum ab8500_fg_discharge_state new_state)
{
	dev_dbg(di->dev, "Discharge state from %d [%s] to %d [%s]\n",
		di->discharge_state,
		discharge_state[di->discharge_state],
		new_state,
		discharge_state[new_state]);

	di->discharge_state = new_state;
}

/**
 * ab8500_fg_algorithm_charging() - FG algorithm for when charging
 * @di:		pointer to the ab8500_fg structure
 *
 * Battery capacity calculation state machine for when we're charging
 */
static void ab8500_fg_algorithm_charging(struct ab8500_fg *di)
{
	/*
	 * If we change to discharge mode
	 * we should start with recovery
	 */
	if (di->discharge_state != AB8500_FG_DISCHARGE_INIT_RECOVERY)
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_INIT_RECOVERY);

	switch (di->charge_state) {
	case AB8500_FG_CHARGE_INIT:
		di->fg_samples = SEC_TO_SAMPLE(
			di->bm->fg_params->accu_charging);

		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_READOUT);

		break;

	case AB8500_FG_CHARGE_READOUT:
		/*
		 * Read the FG and calculate the new capacity
		 */
		mutex_lock(&di->cc_lock);
		if (!di->flags.conv_done && !di->flags.force_full) {
			/* Wasn't the CC IRQ that got us here */
			mutex_unlock(&di->cc_lock);
			dev_dbg(di->dev, "%s CC conv not done\n",
				__func__);

			break;
		}
		di->flags.conv_done = false;
		mutex_unlock(&di->cc_lock);

		ab8500_fg_calc_cap_charging(di);

		break;

	default:
		break;
	}

	/* Check capacity limits */
	ab8500_fg_check_capacity_limits(di, false);
}

static void force_capacity(struct ab8500_fg *di)
{
	int cap;

	ab8500_fg_clear_cap_samples(di);
	cap = di->bat_cap.user_mah;
	if (cap > di->bat_cap.max_mah_design) {
		dev_dbg(di->dev, "Remaining cap %d can't be bigger than total"
			" %d\n", cap, di->bat_cap.max_mah_design);
		cap = di->bat_cap.max_mah_design;
	}
	ab8500_fg_fill_cap_sample(di, di->bat_cap.user_mah);
	di->bat_cap.permille = ab8500_fg_convert_mah_to_permille(di, cap);
	di->bat_cap.mah = cap;
	ab8500_fg_check_capacity_limits(di, true);
}

static bool check_sysfs_capacity(struct ab8500_fg *di)
{
	int cap, lower, upper;
	int cap_permille;

	cap = di->bat_cap.user_mah;

	cap_permille = ab8500_fg_convert_mah_to_permille(di,
		di->bat_cap.user_mah);

	lower = di->bat_cap.permille - di->bm->fg_params->user_cap_limit * 10;
	upper = di->bat_cap.permille + di->bm->fg_params->user_cap_limit * 10;

	if (lower < 0)
		lower = 0;
	/* 1000 is permille, -> 100 percent */
	if (upper > 1000)
		upper = 1000;

	dev_dbg(di->dev, "Capacity limits:"
		" (Lower: %d User: %d Upper: %d) [user: %d, was: %d]\n",
		lower, cap_permille, upper, cap, di->bat_cap.mah);

	/* If within limits, use the saved capacity and exit estimation...*/
	if (cap_permille > lower && cap_permille < upper) {
		dev_dbg(di->dev, "OK! Using users cap %d uAh now\n", cap);
		force_capacity(di);
		return true;
	}
	dev_dbg(di->dev, "Capacity from user out of limits, ignoring");
	return false;
}

/**
 * ab8500_fg_algorithm_discharging() - FG algorithm for when discharging
 * @di:		pointer to the ab8500_fg structure
 *
 * Battery capacity calculation state machine for when we're discharging
 */
static void ab8500_fg_algorithm_discharging(struct ab8500_fg *di)
{
	int sleep_time;

	/* If we change to charge mode we should start with init */
	if (di->charge_state != AB8500_FG_CHARGE_INIT)
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);

	switch (di->discharge_state) {
	case AB8500_FG_DISCHARGE_INIT:
		/* We use the FG IRQ to work on */
		di->init_cnt = 0;
		di->fg_samples = SEC_TO_SAMPLE(di->bm->fg_params->init_timer);
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_INITMEASURING);

		fallthrough;
	case AB8500_FG_DISCHARGE_INITMEASURING:
		/*
		 * Discard a number of samples during startup.
		 * After that, use compensated voltage for a few
		 * samples to get an initial capacity.
		 * Then go to READOUT
		 */
		sleep_time = di->bm->fg_params->init_timer;

		/* Discard the first [x] seconds */
		if (di->init_cnt > di->bm->fg_params->init_discard_time) {
			ab8500_fg_calc_cap_discharge_voltage(di, true);

			ab8500_fg_check_capacity_limits(di, true);
		}

		di->init_cnt += sleep_time;
		if (di->init_cnt > di->bm->fg_params->init_total_time)
			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT_INIT);

		break;

	case AB8500_FG_DISCHARGE_INIT_RECOVERY:
		di->recovery_cnt = 0;
		di->recovery_needed = true;
		ab8500_fg_discharge_state_to(di,
			AB8500_FG_DISCHARGE_RECOVERY);

		fallthrough;

	case AB8500_FG_DISCHARGE_RECOVERY:
		sleep_time = di->bm->fg_params->recovery_sleep_timer;

		/*
		 * We should check the power consumption
		 * If low, go to READOUT (after x min) or
		 * RECOVERY_SLEEP if time left.
		 * If high, go to READOUT
		 */
		di->inst_curr_ua = ab8500_fg_inst_curr_blocking(di);

		if (ab8500_fg_is_low_curr(di, di->inst_curr_ua)) {
			if (di->recovery_cnt >
				di->bm->fg_params->recovery_total_time) {
				di->fg_samples = SEC_TO_SAMPLE(
					di->bm->fg_params->accu_high_curr);
				ab8500_fg_coulomb_counter(di, true);
				ab8500_fg_discharge_state_to(di,
					AB8500_FG_DISCHARGE_READOUT);
				di->recovery_needed = false;
			} else {
				queue_delayed_work(di->fg_wq,
					&di->fg_periodic_work,
					sleep_time * HZ);
			}
			di->recovery_cnt += sleep_time;
		} else {
			di->fg_samples = SEC_TO_SAMPLE(
				di->bm->fg_params->accu_high_curr);
			ab8500_fg_coulomb_counter(di, true);
			ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
		}
		break;

	case AB8500_FG_DISCHARGE_READOUT_INIT:
		di->fg_samples = SEC_TO_SAMPLE(
			di->bm->fg_params->accu_high_curr);
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);
		break;

	case AB8500_FG_DISCHARGE_READOUT:
		di->inst_curr_ua = ab8500_fg_inst_curr_blocking(di);

		if (ab8500_fg_is_low_curr(di, di->inst_curr_ua)) {
			/* Detect mode change */
			if (di->high_curr_mode) {
				di->high_curr_mode = false;
				di->high_curr_cnt = 0;
			}

			if (di->recovery_needed) {
				ab8500_fg_discharge_state_to(di,
					AB8500_FG_DISCHARGE_INIT_RECOVERY);

				queue_delayed_work(di->fg_wq,
					&di->fg_periodic_work, 0);

				break;
			}

			ab8500_fg_calc_cap_discharge_voltage(di, true);
		} else {
			mutex_lock(&di->cc_lock);
			if (!di->flags.conv_done) {
				/* Wasn't the CC IRQ that got us here */
				mutex_unlock(&di->cc_lock);
				dev_dbg(di->dev, "%s CC conv not done\n",
					__func__);

				break;
			}
			di->flags.conv_done = false;
			mutex_unlock(&di->cc_lock);

			/* Detect mode change */
			if (!di->high_curr_mode) {
				di->high_curr_mode = true;
				di->high_curr_cnt = 0;
			}

			di->high_curr_cnt +=
				di->bm->fg_params->accu_high_curr;
			if (di->high_curr_cnt >
				di->bm->fg_params->high_curr_time)
				di->recovery_needed = true;

			ab8500_fg_calc_cap_discharge_fg(di);
		}

		ab8500_fg_check_capacity_limits(di, false);

		break;

	case AB8500_FG_DISCHARGE_WAKEUP:
		ab8500_fg_calc_cap_discharge_voltage(di, true);

		di->fg_samples = SEC_TO_SAMPLE(
			di->bm->fg_params->accu_high_curr);
		ab8500_fg_coulomb_counter(di, true);
		ab8500_fg_discharge_state_to(di,
				AB8500_FG_DISCHARGE_READOUT);

		ab8500_fg_check_capacity_limits(di, false);

		break;

	default:
		break;
	}
}

/**
 * ab8500_fg_algorithm_calibrate() - Internal columb counter offset calibration
 * @di:		pointer to the ab8500_fg structure
 *
 */
static void ab8500_fg_algorithm_calibrate(struct ab8500_fg *di)
{
	int ret;

	switch (di->calib_state) {
	case AB8500_FG_CALIB_INIT:
		dev_dbg(di->dev, "Calibration ongoing...\n");

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_INT_CAL_N_AVG_MASK, CC_INT_CAL_SAMPLES_8);
		if (ret < 0)
			goto err;

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_INTAVGOFFSET_ENA, CC_INTAVGOFFSET_ENA);
		if (ret < 0)
			goto err;
		di->calib_state = AB8500_FG_CALIB_WAIT;
		break;
	case AB8500_FG_CALIB_END:
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_GAS_GAUGE, AB8500_GASG_CC_CTRL_REG,
			CC_MUXOFFSET, CC_MUXOFFSET);
		if (ret < 0)
			goto err;
		di->flags.calibrate = false;
		dev_dbg(di->dev, "Calibration done...\n");
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
		break;
	case AB8500_FG_CALIB_WAIT:
		dev_dbg(di->dev, "Calibration WFI\n");
		break;
	default:
		break;
	}
	return;
err:
	/* Something went wrong, don't calibrate then */
	dev_err(di->dev, "failed to calibrate the CC\n");
	di->flags.calibrate = false;
	di->calib_state = AB8500_FG_CALIB_INIT;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
}

/**
 * ab8500_fg_algorithm() - Entry point for the FG algorithm
 * @di:		pointer to the ab8500_fg structure
 *
 * Entry point for the battery capacity calculation state machine
 */
static void ab8500_fg_algorithm(struct ab8500_fg *di)
{
	if (di->flags.calibrate)
		ab8500_fg_algorithm_calibrate(di);
	else {
		if (di->flags.charging)
			ab8500_fg_algorithm_charging(di);
		else
			ab8500_fg_algorithm_discharging(di);
	}

	dev_dbg(di->dev, "[FG_DATA] %d %d %d %d %d %d %d %d %d %d "
		"%d %d %d %d %d %d %d\n",
		di->bat_cap.max_mah_design,
		di->bat_cap.max_mah,
		di->bat_cap.mah,
		di->bat_cap.permille,
		di->bat_cap.level,
		di->bat_cap.prev_mah,
		di->bat_cap.prev_percent,
		di->bat_cap.prev_level,
		di->vbat_uv,
		di->inst_curr_ua,
		di->avg_curr_ua,
		di->accu_charge,
		di->flags.charging,
		di->charge_state,
		di->discharge_state,
		di->high_curr_mode,
		di->recovery_needed);
}

/**
 * ab8500_fg_periodic_work() - Run the FG state machine periodically
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for periodic work
 */
static void ab8500_fg_periodic_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_periodic_work.work);

	if (di->init_capacity) {
		/* Get an initial capacity calculation */
		ab8500_fg_calc_cap_discharge_voltage(di, true);
		ab8500_fg_check_capacity_limits(di, true);
		di->init_capacity = false;

		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	} else if (di->flags.user_cap) {
		if (check_sysfs_capacity(di)) {
			ab8500_fg_check_capacity_limits(di, true);
			if (di->flags.charging)
				ab8500_fg_charge_state_to(di,
					AB8500_FG_CHARGE_INIT);
			else
				ab8500_fg_discharge_state_to(di,
					AB8500_FG_DISCHARGE_READOUT_INIT);
		}
		di->flags.user_cap = false;
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	} else
		ab8500_fg_algorithm(di);

}

/**
 * ab8500_fg_check_hw_failure_work() - Check OVV_BAT condition
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the OVV_BAT condition
 */
static void ab8500_fg_check_hw_failure_work(struct work_struct *work)
{
	int ret;
	u8 reg_value;

	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_check_hw_failure_work.work);

	/*
	 * If we have had a battery over-voltage situation,
	 * check ovv-bit to see if it should be reset.
	 */
	ret = abx500_get_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_CH_STAT_REG,
		&reg_value);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		return;
	}
	if ((reg_value & BATT_OVV) == BATT_OVV) {
		if (!di->flags.bat_ovv) {
			dev_dbg(di->dev, "Battery OVV\n");
			di->flags.bat_ovv = true;
			power_supply_changed(di->fg_psy);
		}
		/* Not yet recovered from ovv, reschedule this test */
		queue_delayed_work(di->fg_wq, &di->fg_check_hw_failure_work,
				   HZ);
		} else {
			dev_dbg(di->dev, "Battery recovered from OVV\n");
			di->flags.bat_ovv = false;
			power_supply_changed(di->fg_psy);
	}
}

/**
 * ab8500_fg_low_bat_work() - Check LOW_BAT condition
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for checking the LOW_BAT condition
 */
static void ab8500_fg_low_bat_work(struct work_struct *work)
{
	int vbat_uv;

	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_low_bat_work.work);

	vbat_uv = ab8500_fg_bat_voltage(di);

	/* Check if LOW_BAT still fulfilled */
	if (vbat_uv < di->bm->fg_params->lowbat_threshold_uv) {
		/* Is it time to shut down? */
		if (di->low_bat_cnt < 1) {
			di->flags.low_bat = true;
			dev_warn(di->dev, "Shut down pending...\n");
		} else {
			/*
			* Else we need to re-schedule this check to be able to detect
			* if the voltage increases again during charging or
			* due to decreasing load.
			*/
			di->low_bat_cnt--;
			dev_warn(di->dev, "Battery voltage still LOW\n");
			queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
				round_jiffies(LOW_BAT_CHECK_INTERVAL));
		}
	} else {
		di->flags.low_bat_delay = false;
		di->low_bat_cnt = 10;
		dev_warn(di->dev, "Battery voltage OK again\n");
	}

	/* This is needed to dispatch LOW_BAT */
	ab8500_fg_check_capacity_limits(di, false);
}

/**
 * ab8500_fg_battok_calc - calculate the bit pattern corresponding
 * to the target voltage.
 * @di:       pointer to the ab8500_fg structure
 * @target:   target voltage
 *
 * Returns bit pattern closest to the target voltage
 * valid return values are 0-14. (0-BATT_OK_MAX_NR_INCREMENTS)
 */

static int ab8500_fg_battok_calc(struct ab8500_fg *di, int target)
{
	if (target > BATT_OK_MIN +
		(BATT_OK_INCREMENT * BATT_OK_MAX_NR_INCREMENTS))
		return BATT_OK_MAX_NR_INCREMENTS;
	if (target < BATT_OK_MIN)
		return 0;
	return (target - BATT_OK_MIN) / BATT_OK_INCREMENT;
}

/**
 * ab8500_fg_battok_init_hw_register - init battok levels
 * @di:       pointer to the ab8500_fg structure
 *
 */

static int ab8500_fg_battok_init_hw_register(struct ab8500_fg *di)
{
	int selected;
	int sel0;
	int sel1;
	int cbp_sel0;
	int cbp_sel1;
	int ret;
	int new_val;

	sel0 = di->bm->fg_params->battok_falling_th_sel0;
	sel1 = di->bm->fg_params->battok_raising_th_sel1;

	cbp_sel0 = ab8500_fg_battok_calc(di, sel0);
	cbp_sel1 = ab8500_fg_battok_calc(di, sel1);

	selected = BATT_OK_MIN + cbp_sel0 * BATT_OK_INCREMENT;

	if (selected != sel0)
		dev_warn(di->dev, "Invalid voltage step:%d, using %d %d\n",
			sel0, selected, cbp_sel0);

	selected = BATT_OK_MIN + cbp_sel1 * BATT_OK_INCREMENT;

	if (selected != sel1)
		dev_warn(di->dev, "Invalid voltage step:%d, using %d %d\n",
			sel1, selected, cbp_sel1);

	new_val = cbp_sel0 | (cbp_sel1 << 4);

	dev_dbg(di->dev, "using: %x %d %d\n", new_val, cbp_sel0, cbp_sel1);
	ret = abx500_set_register_interruptible(di->dev, AB8500_SYS_CTRL2_BLOCK,
		AB8500_BATT_OK_REG, new_val);
	return ret;
}

/**
 * ab8500_fg_instant_work() - Run the FG state machine instantly
 * @work:	pointer to the work_struct structure
 *
 * Work queue function for instant work
 */
static void ab8500_fg_instant_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg, fg_work);

	ab8500_fg_algorithm(di);
}

/**
 * ab8500_fg_cc_data_end_handler() - end of data conversion isr.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_data_end_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;
	if (!di->nbr_cceoc_irq_cnt) {
		di->nbr_cceoc_irq_cnt++;
		complete(&di->ab8500_fg_started);
	} else {
		di->nbr_cceoc_irq_cnt = 0;
		complete(&di->ab8500_fg_complete);
	}
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_cc_int_calib_handler () - end of calibration isr.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_int_calib_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;
	di->calib_state = AB8500_FG_CALIB_END;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_cc_convend_handler() - isr to get battery avg current.
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_cc_convend_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;

	queue_work(di->fg_wq, &di->fg_acc_cur_work);

	return IRQ_HANDLED;
}

/**
 * ab8500_fg_batt_ovv_handler() - Battery OVV occured
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_batt_ovv_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;

	dev_dbg(di->dev, "Battery OVV\n");

	/* Schedule a new HW failure check */
	queue_delayed_work(di->fg_wq, &di->fg_check_hw_failure_work, 0);

	return IRQ_HANDLED;
}

/**
 * ab8500_fg_lowbatf_handler() - Battery voltage is below LOW threshold
 * @irq:       interrupt number
 * @_di:       pointer to the ab8500_fg structure
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_fg_lowbatf_handler(int irq, void *_di)
{
	struct ab8500_fg *di = _di;

	/* Initiate handling in ab8500_fg_low_bat_work() if not already initiated. */
	if (!di->flags.low_bat_delay) {
		dev_warn(di->dev, "Battery voltage is below LOW threshold\n");
		di->flags.low_bat_delay = true;
		/*
		 * Start a timer to check LOW_BAT again after some time
		 * This is done to avoid shutdown on single voltage dips
		 */
		queue_delayed_work(di->fg_wq, &di->fg_low_bat_work,
			round_jiffies(LOW_BAT_CHECK_INTERVAL));
	}
	return IRQ_HANDLED;
}

/**
 * ab8500_fg_get_property() - get the fg properties
 * @psy:	pointer to the power_supply structure
 * @psp:	pointer to the power_supply_property structure
 * @val:	pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the
 * fg properties by reading the sysfs files.
 * voltage_now:		battery voltage
 * current_now:		battery instant current
 * current_avg:		battery average current
 * charge_full_design:	capacity where battery is considered full
 * charge_now:		battery capacity in nAh
 * capacity:		capacity in percent
 * capacity_level:	capacity level
 *
 * Returns error code in case of failure else 0 on success
 */
static int ab8500_fg_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	/*
	 * If battery is identified as unknown and charging of unknown
	 * batteries is disabled, we always report 100% capacity and
	 * capacity level UNKNOWN, since we can't calculate
	 * remaining capacity
	 */

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (di->flags.bat_ovv)
			val->intval = BATT_OVV_VALUE;
		else
			val->intval = di->vbat_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->inst_curr_ua;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = di->avg_curr_ua;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = ab8500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah_design);
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = ab8500_fg_convert_mah_to_uwh(di,
				di->bat_cap.max_mah);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (di->flags.batt_unknown && !di->bm->chg_unknown_bat &&
				di->flags.batt_id_received)
			val->intval = ab8500_fg_convert_mah_to_uwh(di,
					di->bat_cap.max_mah);
		else
			val->intval = ab8500_fg_convert_mah_to_uwh(di,
					di->bat_cap.prev_mah);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = di->bat_cap.max_mah_design;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = di->bat_cap.max_mah;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (di->flags.batt_unknown && !di->bm->chg_unknown_bat &&
				di->flags.batt_id_received)
			val->intval = di->bat_cap.max_mah;
		else
			val->intval = di->bat_cap.prev_mah;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (di->flags.batt_unknown && !di->bm->chg_unknown_bat &&
				di->flags.batt_id_received)
			val->intval = 100;
		else
			val->intval = di->bat_cap.prev_percent;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (di->flags.batt_unknown && !di->bm->chg_unknown_bat &&
				di->flags.batt_id_received)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		else
			val->intval = di->bat_cap.prev_level;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ab8500_fg_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext = dev_get_drvdata(dev);
	const char **supplicants = (const char **)ext->supplied_to;
	struct ab8500_fg *di;
	struct power_supply_battery_info *bi;
	union power_supply_propval ret;
	int j;

	psy = (struct power_supply *)data;
	di = power_supply_get_drvdata(psy);
	bi = di->bm->bi;

	/*
	 * For all psy where the name of your driver
	 * appears in any supplied_to
	 */
	j = match_string(supplicants, ext->num_supplicants, psy->desc->name);
	if (j < 0)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->desc->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->desc->properties[j];

		if (power_supply_get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_STATUS:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				switch (ret.intval) {
				case POWER_SUPPLY_STATUS_UNKNOWN:
				case POWER_SUPPLY_STATUS_DISCHARGING:
				case POWER_SUPPLY_STATUS_NOT_CHARGING:
					if (!di->flags.charging)
						break;
					di->flags.charging = false;
					di->flags.fully_charged = false;
					if (di->bm->capacity_scaling)
						ab8500_fg_update_cap_scalers(di);
					queue_work(di->fg_wq, &di->fg_work);
					break;
				case POWER_SUPPLY_STATUS_FULL:
					if (di->flags.fully_charged)
						break;
					di->flags.fully_charged = true;
					di->flags.force_full = true;
					/* Save current capacity as maximum */
					di->bat_cap.max_mah = di->bat_cap.mah;
					queue_work(di->fg_wq, &di->fg_work);
					break;
				case POWER_SUPPLY_STATUS_CHARGING:
					if (di->flags.charging &&
						!di->flags.fully_charged)
						break;
					di->flags.charging = true;
					di->flags.fully_charged = false;
					if (di->bm->capacity_scaling)
						ab8500_fg_update_cap_scalers(di);
					queue_work(di->fg_wq, &di->fg_work);
					break;
				}
				break;
			default:
				break;
			}
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				if (!di->flags.batt_id_received &&
				    (bi && (bi->technology !=
					    POWER_SUPPLY_TECHNOLOGY_UNKNOWN))) {
					const struct ab8500_battery_type *b;

					b = di->bm->bat_type;

					di->flags.batt_id_received = true;

					di->bat_cap.max_mah_design =
						di->bm->bi->charge_full_design_uah;

					di->bat_cap.max_mah =
						di->bat_cap.max_mah_design;

					di->vbat_nom_uv =
						di->bm->bi->voltage_max_design_uv;
				}

				if (ret.intval)
					di->flags.batt_unknown = false;
				else
					di->flags.batt_unknown = true;
				break;
			default:
				break;
			}
			break;
		case POWER_SUPPLY_PROP_TEMP:
			switch (ext->desc->type) {
			case POWER_SUPPLY_TYPE_BATTERY:
				if (di->flags.batt_id_received)
					di->bat_temp = ret.intval;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_fg_init_hw_registers() - Set up FG related registers
 * @di:		pointer to the ab8500_fg structure
 *
 * Set up battery OVV, low battery voltage registers
 */
static int ab8500_fg_init_hw_registers(struct ab8500_fg *di)
{
	int ret;

	/*
	 * Set VBAT OVV (overvoltage) threshold to 4.75V (typ) this is what
	 * the hardware supports, nothing else can be configured in hardware.
	 * See this as an "outer limit" where the charger will certainly
	 * shut down. Other (lower) overvoltage levels need to be implemented
	 * in software.
	 */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_BATT_OVV,
		BATT_OVV_TH_4P75,
		BATT_OVV_TH_4P75);
	if (ret) {
		dev_err(di->dev, "failed to set BATT_OVV\n");
		goto out;
	}

	/* Enable VBAT OVV detection */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,
		AB8500_BATT_OVV,
		BATT_OVV_ENA,
		BATT_OVV_ENA);
	if (ret) {
		dev_err(di->dev, "failed to enable BATT_OVV\n");
		goto out;
	}

	/* Low Battery Voltage */
	ret = abx500_set_register_interruptible(di->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_LOW_BAT_REG,
		ab8500_volt_to_regval(
			di->bm->fg_params->lowbat_threshold_uv) << 1 |
		LOW_BAT_ENABLE);
	if (ret) {
		dev_err(di->dev, "%s write failed\n", __func__);
		goto out;
	}

	/* Battery OK threshold */
	ret = ab8500_fg_battok_init_hw_register(di);
	if (ret) {
		dev_err(di->dev, "BattOk init write failed.\n");
		goto out;
	}

	if (is_ab8505(di->parent)) {
		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_MAX_TIME_REG, di->bm->fg_params->pcut_max_time);

		if (ret) {
			dev_err(di->dev, "%s write failed AB8505_RTC_PCUT_MAX_TIME_REG\n", __func__);
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_FLAG_TIME_REG, di->bm->fg_params->pcut_flag_time);

		if (ret) {
			dev_err(di->dev, "%s write failed AB8505_RTC_PCUT_FLAG_TIME_REG\n", __func__);
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_RESTART_REG, di->bm->fg_params->pcut_max_restart);

		if (ret) {
			dev_err(di->dev, "%s write failed AB8505_RTC_PCUT_RESTART_REG\n", __func__);
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_DEBOUNCE_REG, di->bm->fg_params->pcut_debounce_time);

		if (ret) {
			dev_err(di->dev, "%s write failed AB8505_RTC_PCUT_DEBOUNCE_REG\n", __func__);
			goto out;
		}

		ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
			AB8505_RTC_PCUT_CTL_STATUS_REG, di->bm->fg_params->pcut_enable);

		if (ret) {
			dev_err(di->dev, "%s write failed AB8505_RTC_PCUT_CTL_STATUS_REG\n", __func__);
			goto out;
		}
	}
out:
	return ret;
}

/**
 * ab8500_fg_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is the entry point of the pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in any external power
 * supply that this driver needs to be notified of.
 */
static void ab8500_fg_external_power_changed(struct power_supply *psy)
{
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	class_for_each_device(power_supply_class, NULL,
		di->fg_psy, ab8500_fg_get_ext_psy_data);
}

/**
 * ab8500_fg_reinit_work() - work to reset the FG algorithm
 * @work:	pointer to the work_struct structure
 *
 * Used to reset the current battery capacity to be able to
 * retrigger a new voltage base capacity calculation. For
 * test and verification purpose.
 */
static void ab8500_fg_reinit_work(struct work_struct *work)
{
	struct ab8500_fg *di = container_of(work, struct ab8500_fg,
		fg_reinit_work.work);

	if (!di->flags.calibrate) {
		dev_dbg(di->dev, "Resetting FG state machine to init.\n");
		ab8500_fg_clear_cap_samples(di);
		ab8500_fg_calc_cap_discharge_voltage(di, true);
		ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);
		queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	} else {
		dev_err(di->dev, "Residual offset calibration ongoing "
			"retrying..\n");
		/* Wait one second until next try*/
		queue_delayed_work(di->fg_wq, &di->fg_reinit_work,
			round_jiffies(1));
	}
}

/* Exposure to the sysfs interface */

struct ab8500_fg_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct ab8500_fg *, char *);
	ssize_t (*store)(struct ab8500_fg *, const char *, size_t);
};

static ssize_t charge_full_show(struct ab8500_fg *di, char *buf)
{
	return sprintf(buf, "%d\n", di->bat_cap.max_mah);
}

static ssize_t charge_full_store(struct ab8500_fg *di, const char *buf,
				 size_t count)
{
	unsigned long charge_full;
	int ret;

	ret = kstrtoul(buf, 10, &charge_full);
	if (ret)
		return ret;

	di->bat_cap.max_mah = (int) charge_full;
	return count;
}

static ssize_t charge_now_show(struct ab8500_fg *di, char *buf)
{
	return sprintf(buf, "%d\n", di->bat_cap.prev_mah);
}

static ssize_t charge_now_store(struct ab8500_fg *di, const char *buf,
				 size_t count)
{
	unsigned long charge_now;
	int ret;

	ret = kstrtoul(buf, 10, &charge_now);
	if (ret)
		return ret;

	di->bat_cap.user_mah = (int) charge_now;
	di->flags.user_cap = true;
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);
	return count;
}

static struct ab8500_fg_sysfs_entry charge_full_attr =
	__ATTR(charge_full, 0644, charge_full_show, charge_full_store);

static struct ab8500_fg_sysfs_entry charge_now_attr =
	__ATTR(charge_now, 0644, charge_now_show, charge_now_store);

static ssize_t
ab8500_fg_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct ab8500_fg_sysfs_entry *entry;
	struct ab8500_fg *di;

	entry = container_of(attr, struct ab8500_fg_sysfs_entry, attr);
	di = container_of(kobj, struct ab8500_fg, fg_kobject);

	if (!entry->show)
		return -EIO;

	return entry->show(di, buf);
}
static ssize_t
ab8500_fg_store(struct kobject *kobj, struct attribute *attr, const char *buf,
		size_t count)
{
	struct ab8500_fg_sysfs_entry *entry;
	struct ab8500_fg *di;

	entry = container_of(attr, struct ab8500_fg_sysfs_entry, attr);
	di = container_of(kobj, struct ab8500_fg, fg_kobject);

	if (!entry->store)
		return -EIO;

	return entry->store(di, buf, count);
}

static const struct sysfs_ops ab8500_fg_sysfs_ops = {
	.show = ab8500_fg_show,
	.store = ab8500_fg_store,
};

static struct attribute *ab8500_fg_attrs[] = {
	&charge_full_attr.attr,
	&charge_now_attr.attr,
	NULL,
};

static struct kobj_type ab8500_fg_ktype = {
	.sysfs_ops = &ab8500_fg_sysfs_ops,
	.default_attrs = ab8500_fg_attrs,
};

/**
 * ab8500_fg_sysfs_exit() - de-init of sysfs entry
 * @di:                pointer to the struct ab8500_chargalg
 *
 * This function removes the entry in sysfs.
 */
static void ab8500_fg_sysfs_exit(struct ab8500_fg *di)
{
	kobject_del(&di->fg_kobject);
}

/**
 * ab8500_fg_sysfs_init() - init of sysfs entry
 * @di:                pointer to the struct ab8500_chargalg
 *
 * This function adds an entry in sysfs.
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_fg_sysfs_init(struct ab8500_fg *di)
{
	int ret = 0;

	ret = kobject_init_and_add(&di->fg_kobject,
		&ab8500_fg_ktype,
		NULL, "battery");
	if (ret < 0)
		dev_err(di->dev, "failed to create sysfs entry\n");

	return ret;
}

static ssize_t ab8505_powercut_flagtime_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_FLAG_TIME_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_FLAG_TIME_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7F));

fail:
	return ret;
}

static ssize_t ab8505_powercut_flagtime_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	if (kstrtoint(buf, 10, &reg_value))
		goto fail;

	if (reg_value > 0x7F) {
		dev_err(dev, "Incorrect parameter, echo 0 (1.98s) - 127 (15.625ms) for flagtime\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_FLAG_TIME_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_FLAG_TIME_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_maxtime_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_MAX_TIME_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_MAX_TIME_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7F));

fail:
	return ret;

}

static ssize_t ab8505_powercut_maxtime_write(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	if (kstrtoint(buf, 10, &reg_value))
		goto fail;

	if (reg_value > 0x7F) {
		dev_err(dev, "Incorrect parameter, echo 0 (0.0s) - 127 (1.98s) for maxtime\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_MAX_TIME_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_MAX_TIME_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_restart_read(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
		AB8505_RTC_PCUT_RESTART_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_RESTART_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0xF));

fail:
	return ret;
}

static ssize_t ab8505_powercut_restart_write(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	if (kstrtoint(buf, 10, &reg_value))
		goto fail;

	if (reg_value > 0xF) {
		dev_err(dev, "Incorrect parameter, echo 0 - 15 for number of restart\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_RESTART_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_RESTART_REG\n");

fail:
	return count;

}

static ssize_t ab8505_powercut_timer_read(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_TIME_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_TIME_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7F));

fail:
	return ret;
}

static ssize_t ab8505_powercut_restart_counter_read(struct device *dev,
						    struct device_attribute *attr,
						    char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_RESTART_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_RESTART_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0xF0) >> 4);

fail:
	return ret;
}

static ssize_t ab8505_powercut_read(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_CTL_STATUS_REG, &reg_value);

	if (ret < 0)
		goto fail;

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x1));

fail:
	return ret;
}

static ssize_t ab8505_powercut_write(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	if (kstrtoint(buf, 10, &reg_value))
		goto fail;

	if (reg_value > 0x1) {
		dev_err(dev, "Incorrect parameter, echo 0/1 to disable/enable Pcut feature\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_CTL_STATUS_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_CTL_STATUS_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_flag_read(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{

	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_CTL_STATUS_REG,  &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_CTL_STATUS_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ((reg_value & 0x10) >> 4));

fail:
	return ret;
}

static ssize_t ab8505_powercut_debounce_read(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_DEBOUNCE_REG,  &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_DEBOUNCE_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", (reg_value & 0x7));

fail:
	return ret;
}

static ssize_t ab8505_powercut_debounce_write(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	int ret;
	int reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	if (kstrtoint(buf, 10, &reg_value))
		goto fail;

	if (reg_value > 0x7) {
		dev_err(dev, "Incorrect parameter, echo 0 to 7 for debounce setting\n");
		goto fail;
	}

	ret = abx500_set_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_DEBOUNCE_REG, (u8)reg_value);

	if (ret < 0)
		dev_err(dev, "Failed to set AB8505_RTC_PCUT_DEBOUNCE_REG\n");

fail:
	return count;
}

static ssize_t ab8505_powercut_enable_status_read(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	int ret;
	u8 reg_value;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct ab8500_fg *di = power_supply_get_drvdata(psy);

	ret = abx500_get_register_interruptible(di->dev, AB8500_RTC,
						AB8505_RTC_PCUT_CTL_STATUS_REG, &reg_value);

	if (ret < 0) {
		dev_err(dev, "Failed to read AB8505_RTC_PCUT_CTL_STATUS_REG\n");
		goto fail;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", ((reg_value & 0x20) >> 5));

fail:
	return ret;
}

static struct device_attribute ab8505_fg_sysfs_psy_attrs[] = {
	__ATTR(powercut_flagtime, (S_IRUGO | S_IWUSR | S_IWGRP),
		ab8505_powercut_flagtime_read, ab8505_powercut_flagtime_write),
	__ATTR(powercut_maxtime, (S_IRUGO | S_IWUSR | S_IWGRP),
		ab8505_powercut_maxtime_read, ab8505_powercut_maxtime_write),
	__ATTR(powercut_restart_max, (S_IRUGO | S_IWUSR | S_IWGRP),
		ab8505_powercut_restart_read, ab8505_powercut_restart_write),
	__ATTR(powercut_timer, S_IRUGO, ab8505_powercut_timer_read, NULL),
	__ATTR(powercut_restart_counter, S_IRUGO,
		ab8505_powercut_restart_counter_read, NULL),
	__ATTR(powercut_enable, (S_IRUGO | S_IWUSR | S_IWGRP),
		ab8505_powercut_read, ab8505_powercut_write),
	__ATTR(powercut_flag, S_IRUGO, ab8505_powercut_flag_read, NULL),
	__ATTR(powercut_debounce_time, (S_IRUGO | S_IWUSR | S_IWGRP),
		ab8505_powercut_debounce_read, ab8505_powercut_debounce_write),
	__ATTR(powercut_enable_status, S_IRUGO,
		ab8505_powercut_enable_status_read, NULL),
};

static int ab8500_fg_sysfs_psy_create_attrs(struct ab8500_fg *di)
{
	unsigned int i;

	if (is_ab8505(di->parent)) {
		for (i = 0; i < ARRAY_SIZE(ab8505_fg_sysfs_psy_attrs); i++)
			if (device_create_file(&di->fg_psy->dev,
					       &ab8505_fg_sysfs_psy_attrs[i]))
				goto sysfs_psy_create_attrs_failed_ab8505;
	}
	return 0;
sysfs_psy_create_attrs_failed_ab8505:
	dev_err(&di->fg_psy->dev, "Failed creating sysfs psy attrs for ab8505.\n");
	while (i--)
		device_remove_file(&di->fg_psy->dev,
				   &ab8505_fg_sysfs_psy_attrs[i]);

	return -EIO;
}

static void ab8500_fg_sysfs_psy_remove_attrs(struct ab8500_fg *di)
{
	unsigned int i;

	if (is_ab8505(di->parent)) {
		for (i = 0; i < ARRAY_SIZE(ab8505_fg_sysfs_psy_attrs); i++)
			(void)device_remove_file(&di->fg_psy->dev,
						 &ab8505_fg_sysfs_psy_attrs[i]);
	}
}

/* Exposure to the sysfs interface <<END>> */

static int __maybe_unused ab8500_fg_resume(struct device *dev)
{
	struct ab8500_fg *di = dev_get_drvdata(dev);

	/*
	 * Change state if we're not charging. If we're charging we will wake
	 * up on the FG IRQ
	 */
	if (!di->flags.charging) {
		ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_WAKEUP);
		queue_work(di->fg_wq, &di->fg_work);
	}

	return 0;
}

static int __maybe_unused ab8500_fg_suspend(struct device *dev)
{
	struct ab8500_fg *di = dev_get_drvdata(dev);

	flush_delayed_work(&di->fg_periodic_work);
	flush_work(&di->fg_work);
	flush_work(&di->fg_acc_cur_work);
	flush_delayed_work(&di->fg_reinit_work);
	flush_delayed_work(&di->fg_low_bat_work);
	flush_delayed_work(&di->fg_check_hw_failure_work);

	/*
	 * If the FG is enabled we will disable it before going to suspend
	 * only if we're not charging
	 */
	if (di->flags.fg_enabled && !di->flags.charging)
		ab8500_fg_coulomb_counter(di, false);

	return 0;
}

/* ab8500 fg driver interrupts and their respective isr */
static struct ab8500_fg_interrupts ab8500_fg_irq[] = {
	{"NCONV_ACCU", ab8500_fg_cc_convend_handler},
	{"BATT_OVV", ab8500_fg_batt_ovv_handler},
	{"LOW_BAT_F", ab8500_fg_lowbatf_handler},
	{"CC_INT_CALIB", ab8500_fg_cc_int_calib_handler},
	{"CCEOC", ab8500_fg_cc_data_end_handler},
};

static char *supply_interface[] = {
	"ab8500_chargalg",
	"ab8500_usb",
};

static const struct power_supply_desc ab8500_fg_desc = {
	.name			= "ab8500_fg",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= ab8500_fg_props,
	.num_properties		= ARRAY_SIZE(ab8500_fg_props),
	.get_property		= ab8500_fg_get_property,
	.external_power_changed	= ab8500_fg_external_power_changed,
};

static int ab8500_fg_bind(struct device *dev, struct device *master,
			  void *data)
{
	struct ab8500_fg *di = dev_get_drvdata(dev);

	/* Create a work queue for running the FG algorithm */
	di->fg_wq = alloc_ordered_workqueue("ab8500_fg_wq", WQ_MEM_RECLAIM);
	if (di->fg_wq == NULL) {
		dev_err(dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	di->bat_cap.max_mah_design = di->bm->bi->charge_full_design_uah;
	di->bat_cap.max_mah = di->bat_cap.max_mah_design;
	di->vbat_nom_uv = di->bm->bi->voltage_max_design_uv;

	/* Start the coulomb counter */
	ab8500_fg_coulomb_counter(di, true);
	/* Run the FG algorithm */
	queue_delayed_work(di->fg_wq, &di->fg_periodic_work, 0);

	return 0;
}

static void ab8500_fg_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct ab8500_fg *di = dev_get_drvdata(dev);
	int ret;

	/* Disable coulomb counter */
	ret = ab8500_fg_coulomb_counter(di, false);
	if (ret)
		dev_err(dev, "failed to disable coulomb counter\n");

	destroy_workqueue(di->fg_wq);
	flush_scheduled_work();
}

static const struct component_ops ab8500_fg_component_ops = {
	.bind = ab8500_fg_bind,
	.unbind = ab8500_fg_unbind,
};

static int ab8500_fg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_supply_config psy_cfg = {};
	struct ab8500_fg *di;
	int i, irq;
	int ret = 0;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->bm = &ab8500_bm_data;

	mutex_init(&di->cc_lock);

	/* get parent data */
	di->dev = dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	di->main_bat_v = devm_iio_channel_get(dev, "main_bat_v");
	if (IS_ERR(di->main_bat_v)) {
		ret = dev_err_probe(dev, PTR_ERR(di->main_bat_v),
				    "failed to get main battery ADC channel\n");
		return ret;
	}

	psy_cfg.supplied_to = supply_interface;
	psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);
	psy_cfg.drv_data = di;

	di->init_capacity = true;

	ab8500_fg_charge_state_to(di, AB8500_FG_CHARGE_INIT);
	ab8500_fg_discharge_state_to(di, AB8500_FG_DISCHARGE_INIT);

	/* Init work for running the fg algorithm instantly */
	INIT_WORK(&di->fg_work, ab8500_fg_instant_work);

	/* Init work for getting the battery accumulated current */
	INIT_WORK(&di->fg_acc_cur_work, ab8500_fg_acc_cur_work);

	/* Init work for reinitialising the fg algorithm */
	INIT_DEFERRABLE_WORK(&di->fg_reinit_work,
		ab8500_fg_reinit_work);

	/* Work delayed Queue to run the state machine */
	INIT_DEFERRABLE_WORK(&di->fg_periodic_work,
		ab8500_fg_periodic_work);

	/* Work to check low battery condition */
	INIT_DEFERRABLE_WORK(&di->fg_low_bat_work,
		ab8500_fg_low_bat_work);

	/* Init work for HW failure check */
	INIT_DEFERRABLE_WORK(&di->fg_check_hw_failure_work,
		ab8500_fg_check_hw_failure_work);

	/* Reset battery low voltage flag */
	di->flags.low_bat = false;

	/* Initialize low battery counter */
	di->low_bat_cnt = 10;

	/* Initialize OVV, and other registers */
	ret = ab8500_fg_init_hw_registers(di);
	if (ret) {
		dev_err(dev, "failed to initialize registers\n");
		return ret;
	}

	/* Consider battery unknown until we're informed otherwise */
	di->flags.batt_unknown = true;
	di->flags.batt_id_received = false;

	/* Register FG power supply class */
	di->fg_psy = devm_power_supply_register(dev, &ab8500_fg_desc, &psy_cfg);
	if (IS_ERR(di->fg_psy)) {
		dev_err(dev, "failed to register FG psy\n");
		return PTR_ERR(di->fg_psy);
	}

	di->fg_samples = SEC_TO_SAMPLE(di->bm->fg_params->init_timer);

	/*
	 * Initialize completion used to notify completion and start
	 * of inst current
	 */
	init_completion(&di->ab8500_fg_started);
	init_completion(&di->ab8500_fg_complete);

	/* Register primary interrupt handlers */
	for (i = 0; i < ARRAY_SIZE(ab8500_fg_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_fg_irq[i].name);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(dev, irq, NULL,
				  ab8500_fg_irq[i].isr,
				  IRQF_SHARED | IRQF_NO_SUSPEND | IRQF_ONESHOT,
				  ab8500_fg_irq[i].name, di);

		if (ret != 0) {
			dev_err(dev, "failed to request %s IRQ %d: %d\n",
				ab8500_fg_irq[i].name, irq, ret);
			return ret;
		}
		dev_dbg(dev, "Requested %s IRQ %d: %d\n",
			ab8500_fg_irq[i].name, irq, ret);
	}

	di->irq = platform_get_irq_byname(pdev, "CCEOC");
	disable_irq(di->irq);
	di->nbr_cceoc_irq_cnt = 0;

	platform_set_drvdata(pdev, di);

	ret = ab8500_fg_sysfs_init(di);
	if (ret) {
		dev_err(dev, "failed to create sysfs entry\n");
		return ret;
	}

	ret = ab8500_fg_sysfs_psy_create_attrs(di);
	if (ret) {
		dev_err(dev, "failed to create FG psy\n");
		ab8500_fg_sysfs_exit(di);
		return ret;
	}

	/* Calibrate the fg first time */
	di->flags.calibrate = true;
	di->calib_state = AB8500_FG_CALIB_INIT;

	/* Use room temp as default value until we get an update from driver. */
	di->bat_temp = 210;

	list_add_tail(&di->node, &ab8500_fg_list);

	return component_add(dev, &ab8500_fg_component_ops);
}

static int ab8500_fg_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct ab8500_fg *di = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &ab8500_fg_component_ops);
	list_del(&di->node);
	ab8500_fg_sysfs_exit(di);
	ab8500_fg_sysfs_psy_remove_attrs(di);

	return ret;
}

static SIMPLE_DEV_PM_OPS(ab8500_fg_pm_ops, ab8500_fg_suspend, ab8500_fg_resume);

static const struct of_device_id ab8500_fg_match[] = {
	{ .compatible = "stericsson,ab8500-fg", },
	{ },
};
MODULE_DEVICE_TABLE(of, ab8500_fg_match);

struct platform_driver ab8500_fg_driver = {
	.probe = ab8500_fg_probe,
	.remove = ab8500_fg_remove,
	.driver = {
		.name = "ab8500-fg",
		.of_match_table = ab8500_fg_match,
		.pm = &ab8500_fg_pm_ops,
	},
};
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski");
MODULE_ALIAS("platform:ab8500-fg");
MODULE_DESCRIPTION("AB8500 Fuel Gauge driver");
