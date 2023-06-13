// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "QCOM-BATT: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/printk.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <linux/pmic-voter.h>
#include <linux/qti_power_supply.h>
#include <linux/workqueue.h>
#include <linux/iio/consumer.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>
#include "battery.h"
#include "smb5-iio.h"

#define DRV_MAJOR_VERSION	1
#define DRV_MINOR_VERSION	0

#define BATT_PROFILE_VOTER		"BATT_PROFILE_VOTER"
#define CHG_STATE_VOTER			"CHG_STATE_VOTER"
#define TAPER_STEPPER_VOTER		"TAPER_STEPPER_VOTER"
#define TAPER_END_VOTER			"TAPER_END_VOTER"
#define PL_TAPER_EARLY_BAD_VOTER	"PL_TAPER_EARLY_BAD_VOTER"
#define PARALLEL_PSY_VOTER		"PARALLEL_PSY_VOTER"
#define PL_HW_ABSENT_VOTER		"PL_HW_ABSENT_VOTER"
#define PL_VOTER			"PL_VOTER"
#define RESTRICT_CHG_VOTER		"RESTRICT_CHG_VOTER"
#define ICL_CHANGE_VOTER		"ICL_CHANGE_VOTER"
#define PL_INDIRECT_VOTER		"PL_INDIRECT_VOTER"
#define USBIN_I_VOTER			"USBIN_I_VOTER"
#define PL_FCC_LOW_VOTER		"PL_FCC_LOW_VOTER"
#define ICL_LIMIT_VOTER			"ICL_LIMIT_VOTER"
#define FCC_STEPPER_VOTER		"FCC_STEPPER_VOTER"
#define FCC_VOTER			"FCC_VOTER"
#define MAIN_FCC_VOTER			"MAIN_FCC_VOTER"
#define PD_VOTER			"PD_VOTER"

/* PMI8998 */
#define	PMI8998_SUBTYPE	0x15

/* PM660 */
#define	PM660_SUBTYPE	0x1B

struct pl_data {
	struct device		*dev;
	int			pl_mode;
	int			pl_batfet_mode;
	int			pl_min_icl_ua;
	int			slave_pct;
	int			slave_fcc_ua;
	int			main_fcc_ua;
	int			restricted_current;
	bool			restricted_charging_enabled;
	struct votable		*fcc_votable;
	struct votable		*fv_votable;
	struct votable		*pl_disable_votable;
	struct votable		*pl_awake_votable;
	struct votable		*hvdcp_hw_inov_dis_votable;
	struct votable		*usb_icl_votable;
	struct votable		*pl_enable_votable_indirect;
	struct votable		*cp_ilim_votable;
	struct votable		*cp_disable_votable;
	struct votable		*fcc_main_votable;
	struct votable		*cp_slave_disable_votable;
	struct delayed_work	status_change_work;
	struct work_struct	pl_disable_forever_work;
	struct work_struct	pl_taper_work;
	struct delayed_work	fcc_stepper_work;
	bool			taper_work_running;
	struct power_supply	*pl_psy;
	struct power_supply	*batt_psy;
	struct power_supply	*usb_psy;
	struct power_supply	*dc_psy;
	struct power_supply	*cp_master_psy;
	int			charge_type;
	int			total_settled_ua;
	int			pl_settled_ua;
	int			pl_fcc_max;
	int			fcc_stepper_enable;
	int			main_step_fcc_dir;
	int			main_step_fcc_count;
	int			main_step_fcc_residual;
	int			parallel_step_fcc_dir;
	int			parallel_step_fcc_count;
	int			parallel_step_fcc_residual;
	int			step_fcc;
	int			override_main_fcc_ua;
	int			total_fcc_ua;
	u32			wa_flags;
	struct class		qcom_batt_class;
	struct wakeup_source	*pl_ws;
	struct notifier_block	nb;
	struct charger_param	*chg_param;
	bool			pl_disable;
	bool			cp_disabled;
	int			taper_entry_fv;
	int			main_fcc_max;
	int			charger_type;
	/* debugfs directory */
	struct dentry		*dfs_root;
	u32			float_voltage_uv;
	struct iio_channel	**iio_chan_list_cp;
	struct iio_channel	*iio_chan_list_cp_slave;
	struct iio_channel	**iio_chan_list_smb_parallel;
};

static struct pl_data *the_chip;

enum print_reason {
	PR_PARALLEL	= BIT(0),
};

enum {
	AICL_RERUN_WA_BIT	= BIT(0),
	FORCE_INOV_DISABLE_BIT	= BIT(1),
};

static int debug_mask;

#define pl_dbg(chip, reason, fmt, ...)				\
	do {								\
		if (debug_mask & (reason))				\
			pr_info(fmt, ##__VA_ARGS__);	\
		else							\
			pr_debug(fmt, ##__VA_ARGS__);		\
	} while (0)

#define IS_USBIN(mode)	((mode == QTI_POWER_SUPPLY_PL_USBIN_USBIN) \
			|| (mode == QTI_POWER_SUPPLY_PL_USBIN_USBIN_EXT))
enum {
	VER = 0,
	SLAVE_PCT,
	RESTRICT_CHG_ENABLE,
	RESTRICT_CHG_CURRENT,
	FCC_STEPPING_IN_PROGRESS,
};

enum {
	PARALLEL_INPUT_MODE,
	PARALLEL_OUTPUT_MODE,
};

/* CP Channels */
static const char * const bat_cp_ext_iio_chan[] = {
	[BAT_CP_PARALLEL_MODE] = "cp_parallel_mode",
	[BAT_CP_PARALLEL_OUTPUT_MODE] = "cp_parallel_output_mode",
	[BAT_CP_MIN_ICL] = "cp_min_icl",
	[BAT_CP_SWITCHER_EN] = "cp_switcher_en",
};

/* SMB1355 Channels */
static const char * const bat_smb_parallel_ext_iio_chan[] = {
	[BAT_SMB_PARALLEL_INPUT_SUSPEND] = "pl_input_suspend",
	[BAT_SMB_PARALLEL_MODE] = "pl_mode",
	[BAT_SMB_PARALLEL_BATFET_MODE] = "pl_batfet_mode",
	[BAT_SMB_PARALLEL_MIN_ICL] = "pl_min_icl",
	[BAT_SMB_PARALLEL_FCC_MAX] = "pl_fcc_max",
	[BAT_SMB_PARALLEL_CURRENT_MAX] = "pl_current_max",
	[BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX] =
			"pl_constant_charge_current_max",
	[BAT_SMB_PARALLEL_VOLTAGE_MAX] = "pl_voltage_max",
	[BAT_SMB_PARALLEL_CHARGE_TYPE] = "pl_charge_type",
};

/*********
 * HELPER*
 *********/
static bool is_usb_available(struct pl_data *chip)
{
	if (!chip->usb_psy)
		chip->usb_psy =
			power_supply_get_by_name("usb");

	return !!chip->usb_psy;
}

static bool is_cp_available(struct pl_data *chip)
{
	int rc;
	struct iio_channel **iio_list;

	if (IS_ERR(chip->iio_chan_list_cp))
		return false;

	if (!chip->iio_chan_list_cp) {
		iio_list = get_ext_channels(chip->dev,
				bat_cp_ext_iio_chan,
				ARRAY_SIZE(bat_cp_ext_iio_chan));
		if (IS_ERR(iio_list)) {
			rc = PTR_ERR(iio_list);
			if (rc != -EPROBE_DEFER) {
				dev_err(chip->dev, "Failed to get channels, %d\n",
					rc);
				chip->iio_chan_list_cp = ERR_PTR(-EINVAL);
			}
			return false;
		}
		chip->iio_chan_list_cp = iio_list;
	}

	if (!chip->cp_master_psy) {
		chip->cp_master_psy =
			power_supply_get_by_name("charge_pump_master");
		if (!chip->cp_master_psy)
			return false;
	}

	return true;
}

static int battery_read_iio_prop(struct pl_data *chip,
		enum iio_type type, int iio_chan, int *val)
{
	struct iio_channel *iio_chan_list;
	int rc;

	switch (type) {
	case CP:
		if (IS_ERR_OR_NULL(chip->iio_chan_list_cp))
			return -ENODEV;
		iio_chan_list = chip->iio_chan_list_cp[iio_chan];
		break;
	case SMB_PARALLEL:
		if (IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel))
			return -ENODEV;
		iio_chan_list = chip->iio_chan_list_smb_parallel[iio_chan];
		break;
	default:
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}

	rc = iio_read_channel_processed(iio_chan_list, val);
	return (rc < 0) ? rc : 0;
}

static int battery_write_iio_prop(struct pl_data *chip,
		enum iio_type type, int iio_chan, int val)
{
	struct iio_channel *iio_chan_list;

	switch (type) {
	case CP:
		if (IS_ERR_OR_NULL(chip->iio_chan_list_cp))
			return -ENODEV;
		iio_chan_list = chip->iio_chan_list_cp[iio_chan];
		break;
	case SMB_PARALLEL:
		if (IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel))
			return -ENODEV;
		iio_chan_list = chip->iio_chan_list_smb_parallel[iio_chan];
		break;
	default:
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}

	return iio_write_channel_raw(iio_chan_list, val);
}

static int cp_get_parallel_mode(struct pl_data *chip, int mode)
{
	int rc = -EINVAL, val = -EINVAL;

	if (!is_cp_available(chip))
		return -EINVAL;

	switch (mode) {
	case PARALLEL_INPUT_MODE:
		rc = battery_read_iio_prop(chip, CP,
			BAT_CP_PARALLEL_MODE, &val);
		break;
	case PARALLEL_OUTPUT_MODE:
		rc = battery_read_iio_prop(chip, CP,
			BAT_CP_PARALLEL_OUTPUT_MODE,
			&val);
		break;
	default:
		pr_err("Invalid mode request %d\n", mode);
		break;
	}

	if (rc < 0)
		pr_err("Failed to read CP topology for mode=%d rc=%d\n",
				mode, rc);

	return val;
}

static int get_adapter_icl_based_ilim(struct pl_data *chip)
{
	int main_icl = -EINVAL, adapter_icl = -EINVAL, final_icl = -EINVAL;
	int rc = -EINVAL, pval = 0;

	rc = chip->chg_param->iio_read(chip->dev,
			PSY_IIO_PD_ACTIVE, &pval);
	if (rc < 0)
		pr_err("Failed to read PD_ACTIVE status rc=%d\n",
				rc);
	/* Check for QC 3, 3.5 and PPS adapters, return if its none of them */
	if (chip->charger_type != QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3 &&
		chip->charger_type != QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3P5 &&
		pval != QTI_POWER_SUPPLY_PD_PPS_ACTIVE)
		return final_icl;

	/*
	 * For HVDCP3/HVDCP_3P5 adapters, limit max. ILIM as:
	 * HVDCP3_ICL: Maximum ICL of HVDCP3 adapter(from DT
	 * configuration).
	 *
	 * For PPS adapters, limit max. ILIM to
	 * MIN(qc4_max_icl, PD_CURRENT_MAX)
	 */
	if (pval == QTI_POWER_SUPPLY_PD_PPS_ACTIVE) {
		adapter_icl = min_t(int, chip->chg_param->qc4_max_icl_ua,
				get_client_vote_locked(chip->usb_icl_votable,
				PD_VOTER));
		if (adapter_icl <= 0)
			adapter_icl = chip->chg_param->qc4_max_icl_ua;
	} else {
		adapter_icl = chip->chg_param->hvdcp3_max_icl_ua;
	}

	/*
	 * For Parallel input configurations:
	 * VBUS: final_icl = adapter_icl - main_ICL
	 * VMID: final_icl = adapter_icl
	 */
	final_icl = adapter_icl;
	if (cp_get_parallel_mode(chip, PARALLEL_INPUT_MODE)
					== QTI_POWER_SUPPLY_PL_USBIN_USBIN) {
		main_icl = get_effective_result_locked(chip->usb_icl_votable);
		if ((main_icl >= 0) && (main_icl < adapter_icl))
			final_icl = adapter_icl - main_icl;
	}

	pr_debug("charger_type=%d final_icl=%d adapter_icl=%d main_icl=%d\n",
		chip->charger_type, final_icl, adapter_icl, main_icl);

	return final_icl;
}

/*
 * Adapter CC Mode: ILIM over-ridden explicitly, below takes no effect.
 *
 * Adapter CV mode: Configuration of ILIM for different topology is as below:
 * MID-VPH:
 *	SMB1390 ILIM: independent of FCC and based on the AICL result or
 *			PD advertised current,  handled directly in SMB1390
 *			driver.
 * MID-VBAT:
 *	 SMB1390 ILIM: based on minimum of FCC portion of SMB1390 or ICL.
 * USBIN-VBAT:
 *	SMB1390 ILIM: based on FCC portion of SMB1390 and independent of ICL.
 */
static void cp_configure_ilim(struct pl_data *chip, const char *voter, int ilim)
{
	int rc = 0, fcc, target_icl, val;

	if (!is_usb_available(chip))
		return;

	if (!is_cp_available(chip))
		return;

	if (cp_get_parallel_mode(chip, PARALLEL_OUTPUT_MODE)
					== QTI_POWER_SUPPLY_PL_OUTPUT_VPH)
		return;

	target_icl = get_adapter_icl_based_ilim(chip);
	ilim = (target_icl > 0) ? min(ilim, target_icl) : ilim;

	rc = battery_read_iio_prop(chip, CP, BAT_CP_MIN_ICL, &val);
	if (rc < 0) {
		pr_err("Failed to read min_icl rc=%d\n", rc);
		return;
	}

	if (!chip->cp_ilim_votable)
		chip->cp_ilim_votable = find_votable("CP_ILIM");

	if (chip->cp_ilim_votable) {
		fcc = get_effective_result_locked(chip->fcc_votable);
		/*
		 * If FCC >= (2 * MIN_ICL) then it is safe to enable CP
		 * with MIN_ICL.
		 * Configure ILIM as follows:
		 * if request_ilim < MIN_ICL cofigure ILIM to MIN_ICL.
		 * otherwise configure ILIM to requested_ilim.
		 */
		if ((fcc >= (val * 2)) && (ilim < val))
			vote(chip->cp_ilim_votable, voter, true, val);
		else
			vote(chip->cp_ilim_votable, voter, true, ilim);

		/*
		 * Rerun FCC votable to ensure offset for ILIM compensation is
		 * recalculated based on new ILIM.
		 */
		if (!chip->fcc_main_votable)
			chip->fcc_main_votable = find_votable("FCC_MAIN");
		if ((chip->charger_type == QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3)
				&& chip->fcc_main_votable)
			rerun_election(chip->fcc_main_votable);

		pl_dbg(chip, PR_PARALLEL,
			"ILIM: vote: %d voter:%s min_ilim=%d fcc = %d\n",
			ilim, voter, val, fcc);
	}
}

/*******
 * ICL *
 ********/
static int get_settled_split(struct pl_data *chip, int *main_icl_ua,
				int *slave_icl_ua, int *total_settled_icl_ua)
{
	int slave_icl_pct, total_current_ua;
	int slave_ua = 0, main_settled_ua = 0;
	int rc, total_settled_ua = 0;
	int val;

	if (!IS_USBIN(chip->pl_mode))
		return -EINVAL;

	if (!get_effective_result_locked(chip->pl_disable_votable)) {
		/* read the aicl settled value */
		rc = chip->chg_param->iio_read(chip->dev,
			PSY_IIO_MAIN_INPUT_CURRENT_SETTLED, &val);
		if (rc < 0) {
			pr_err("Couldn't get aicl settled value rc=%d\n", rc);
			return rc;
		}
		main_settled_ua = val;
		slave_icl_pct = max(0, chip->slave_pct);
		slave_ua = ((main_settled_ua + chip->pl_settled_ua)
						* slave_icl_pct) / 100;
		total_settled_ua = main_settled_ua + chip->pl_settled_ua;
	}

	total_current_ua = get_effective_result_locked(chip->usb_icl_votable);
	if (total_current_ua < 0) {
		if (!chip->usb_psy)
			chip->usb_psy = power_supply_get_by_name("usb");
		if (!chip->usb_psy) {
			pr_err("Couldn't get usbpsy while splitting settled\n");
			return -ENOENT;
		}
		/* no client is voting, so get the total current from charger */
		rc = chip->chg_param->iio_read(chip->dev,
			PSY_IIO_HW_CURRENT_MAX, &val);
		if (rc < 0) {
			pr_err("Couldn't get max current rc=%d\n", rc);
			return rc;
		}
		total_current_ua = val;
	}

	*main_icl_ua = total_current_ua - slave_ua;
	*slave_icl_ua = slave_ua;
	*total_settled_icl_ua = total_settled_ua;

	pl_dbg(chip, PR_PARALLEL,
		"Split total_current_ua=%d total_settled_ua=%d main_settled_ua=%d slave_ua=%d\n",
		total_current_ua, total_settled_ua, main_settled_ua, slave_ua);

	return 0;
}

static int validate_parallel_icl(struct pl_data *chip, bool *disable)
{
	int rc = 0;
	int main_ua = 0, slave_ua = 0, total_settled_ua = 0;

	if (!IS_USBIN(chip->pl_mode)
		|| get_effective_result_locked(chip->pl_disable_votable))
		return 0;

	rc = get_settled_split(chip, &main_ua, &slave_ua, &total_settled_ua);
	if (rc < 0) {
		pr_err("Couldn't  get split current rc=%d\n", rc);
		return rc;
	}

	if (slave_ua < chip->pl_min_icl_ua)
		*disable = true;
	else
		*disable = false;

	return 0;
}

static void split_settled(struct pl_data *chip)
{
	int rc, main_ua, slave_ua, total_settled_ua;

	rc = get_settled_split(chip, &main_ua, &slave_ua, &total_settled_ua);
	if (rc < 0) {
		pr_err("Couldn't  get split current rc=%d\n", rc);
		return;
	}

	/*
	 * If there is an increase in slave share
	 * (Also handles parallel enable case)
	 *	Set Main ICL then slave ICL
	 * else
	 * (Also handles parallel disable case)
	 *	Set slave ICL then main ICL.
	 */
	if (slave_ua > chip->pl_settled_ua) {
		/* Set ICL on main charger */
		rc = chip->chg_param->iio_write(chip->dev, PSY_IIO_CURRENT_MAX,
			main_ua);
		if (rc < 0) {
			pr_err("Couldn't change slave suspend state rc=%d\n",
					rc);
			return;
		}

		/* set parallel's ICL  could be 0mA when pl is disabled */
		rc = battery_write_iio_prop(chip, SMB_PARALLEL,
				BAT_SMB_PARALLEL_CURRENT_MAX, slave_ua);
		if (rc < 0) {
			pr_err("Couldn't set parallel icl, rc=%d\n", rc);
			return;
		}
	} else {
		/* set parallel's ICL  could be 0mA when pl is disabled */
		rc = battery_write_iio_prop(chip, SMB_PARALLEL,
			BAT_SMB_PARALLEL_CURRENT_MAX, slave_ua);
		if (rc < 0) {
			pr_err("Couldn't set parallel icl, rc=%d\n", rc);
			return;
		}
		/* Set ICL on main charger */
		rc = chip->chg_param->iio_write(chip->dev, PSY_IIO_CURRENT_MAX,
			main_ua);
		if (rc < 0) {
			pr_err("Couldn't change slave suspend state rc=%d\n",
					rc);
			return;
		}
	}

	chip->total_settled_ua = total_settled_ua;
	chip->pl_settled_ua = slave_ua;
}

static ssize_t version_show(struct class *c, struct class_attribute *attr,
			char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d.%d\n",
			DRV_MAJOR_VERSION, DRV_MINOR_VERSION);
}
static CLASS_ATTR_RO(version);

/*************
 * SLAVE PCT *
 **************/
static ssize_t slave_pct_show(struct class *c, struct class_attribute *attr,
			char *ubuf)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);

	return scnprintf(ubuf, PAGE_SIZE, "%d\n", chip->slave_pct);
}

static ssize_t slave_pct_store(struct class *c, struct class_attribute *attr,
			const char *ubuf, size_t count)
{
	struct pl_data *chip = container_of(c, struct pl_data, qcom_batt_class);
	int rc;
	unsigned long val;
	bool disable = false;

	if (kstrtoul(ubuf, 10, &val))
		return -EINVAL;

	chip->slave_pct = val;

	rc = validate_parallel_icl(chip, &disable);
	if (rc < 0)
		return rc;

	vote(chip->pl_disable_votable, ICL_LIMIT_VOTER, disable, 0);
	rerun_election(chip->fcc_votable);
	rerun_election(chip->fv_votable);
	if (IS_USBIN(chip->pl_mode))
		split_settled(chip);

	return count;
}
static struct class_attribute class_attr_slave_pct =
		__ATTR(parallel_pct, 0644, slave_pct_show, slave_pct_store);

/************************
 * RESTRICTED CHARGIGNG *
 ************************/
static ssize_t restrict_chg_show(struct class *c, struct class_attribute *attr,
		char *ubuf)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);

	return scnprintf(ubuf, PAGE_SIZE, "%d\n",
			chip->restricted_charging_enabled);
}

static ssize_t restrict_chg_store(struct class *c, struct class_attribute *attr,
			const char *ubuf, size_t count)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);
	unsigned long val;

	if (kstrtoul(ubuf, 10, &val))
		return -EINVAL;

	if (chip->restricted_charging_enabled == !!val)
		goto no_change;

	chip->restricted_charging_enabled = !!val;

	/* disable parallel charger in case of restricted charging */
	vote(chip->pl_disable_votable, RESTRICT_CHG_VOTER,
				chip->restricted_charging_enabled, 0);

	vote(chip->fcc_votable, RESTRICT_CHG_VOTER,
				chip->restricted_charging_enabled,
				chip->restricted_current);

no_change:
	return count;
}
static CLASS_ATTR_RW(restrict_chg);

static ssize_t restrict_cur_show(struct class *c, struct class_attribute *attr,
			char *ubuf)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);

	return scnprintf(ubuf, PAGE_SIZE, "%d\n", chip->restricted_current);
}

static ssize_t restrict_cur_store(struct class *c, struct class_attribute *attr,
			const char *ubuf, size_t count)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);
	unsigned long val;

	if (kstrtoul(ubuf, 10, &val))
		return -EINVAL;

	chip->restricted_current = val;

	vote(chip->fcc_votable, RESTRICT_CHG_VOTER,
				chip->restricted_charging_enabled,
				chip->restricted_current);

	return count;
}
static CLASS_ATTR_RW(restrict_cur);

/****************************
 * FCC STEPPING IN PROGRESS *
 ****************************/
static ssize_t fcc_stepping_in_progress_show(struct class *c,
				struct class_attribute *attr, char *ubuf)
{
	struct pl_data *chip = container_of(c, struct pl_data,
			qcom_batt_class);

	return scnprintf(ubuf, PAGE_SIZE, "%d\n", chip->step_fcc);
}
static CLASS_ATTR_RO(fcc_stepping_in_progress);

static struct attribute *batt_class_attrs[] = {
	[VER]			= &class_attr_version.attr,
	[SLAVE_PCT]		= &class_attr_slave_pct.attr,
	[RESTRICT_CHG_ENABLE]	= &class_attr_restrict_chg.attr,
	[RESTRICT_CHG_CURRENT]	= &class_attr_restrict_cur.attr,
	[FCC_STEPPING_IN_PROGRESS]
				= &class_attr_fcc_stepping_in_progress.attr,
	NULL,
};
ATTRIBUTE_GROUPS(batt_class);

/*********
 *  FCC  *
 **********/
#define EFFICIENCY_PCT	80
#define STEP_UP 1
#define STEP_DOWN -1
static void get_fcc_split(struct pl_data *chip, int total_ua,
			int *master_ua, int *slave_ua)
{
	int rc, effective_total_ua, slave_limited_ua, hw_cc_delta_ua = 0,
		icl_ua, adapter_uv, bcl_ua, val;

	rc = chip->chg_param->iio_read(chip->dev, PSY_IIO_FCC_DELTA, &val);
	if (rc < 0) {
		pr_err("Couldn't get fcc_delta rc=%d\n", rc);
		hw_cc_delta_ua = 0;
	} else {
		hw_cc_delta_ua = val;
	}

	bcl_ua = INT_MAX;
	if (chip->pl_mode == QTI_POWER_SUPPLY_PL_USBMID_USBMID) {
		rc = chip->chg_param->iio_read(chip->dev,
			PSY_IIO_MAIN_INPUT_CURRENT_SETTLED, &val);
		if (rc < 0) {
			pr_err("Couldn't get aicl settled value rc=%d\n", rc);
			return;
		}
		icl_ua = val;

		rc = chip->chg_param->iio_read(chip->dev,
			PSY_IIO_MAIN_INPUT_VOLTAGE_SETTLED, &val);
		if (rc < 0) {
			pr_err("Couldn't get adaptive voltage rc=%d\n", rc);
			return;
		}
		adapter_uv = val;

		bcl_ua = div64_s64((s64)icl_ua * adapter_uv * EFFICIENCY_PCT,
			(s64)get_effective_result(chip->fv_votable) * 100);
	}

	effective_total_ua = max(0, total_ua + hw_cc_delta_ua);
	slave_limited_ua = min(effective_total_ua, bcl_ua);
	*slave_ua = (slave_limited_ua * chip->slave_pct) / 100;
	*slave_ua = min(*slave_ua, chip->pl_fcc_max);

	/*
	 * In stacked BATFET configuration charger's current goes
	 * through main charger's BATFET, keep the main charger's FCC
	 * to the votable result.
	 */
	if (chip->pl_batfet_mode == QTI_POWER_SUPPLY_PL_STACKED_BATFET) {
		*master_ua = max(0, total_ua);
		if (chip->main_fcc_max)
			*master_ua = min(*master_ua,
					chip->main_fcc_max + *slave_ua);
	} else {
		*master_ua = max(0, total_ua - *slave_ua);
		if (chip->main_fcc_max)
			*master_ua = min(*master_ua, chip->main_fcc_max);
	}
}

static void get_main_fcc_config(struct pl_data *chip, int *total_fcc)
{
	int rc = 0, val;

	if (!is_cp_available(chip))
		goto out;

	rc = battery_read_iio_prop(chip, CP, BAT_CP_SWITCHER_EN, &val);
	if (rc < 0) {
		pr_err("Couldn't get switcher enable status, rc=%d\n", rc);
		goto out;
	}

	if (!val) {
		/*
		 * To honor main charger upper FCC limit, on CP switcher
		 * disable, skip fcc slewing as it will cause delay in limiting
		 * the charge current flowing through main charger.
		 */
		if (!chip->cp_disabled) {
			chip->fcc_stepper_enable = false;
			pl_dbg(chip, PR_PARALLEL,
				"Disabling FCC slewing on CP Switcher disable\n");
		}
		chip->cp_disabled = true;
	} else {
		chip->cp_disabled = false;
		pl_dbg(chip, PR_PARALLEL,
			"CP Switcher is enabled, don't limit main fcc\n");
		return;
	}
out:
	*total_fcc = min(*total_fcc, chip->main_fcc_max);
}

static void get_fcc_stepper_params(struct pl_data *chip, int main_fcc_ua,
			int parallel_fcc_ua)
{
	int main_set_fcc_ua, total_fcc_ua, target_icl;
	bool override;

	if (!chip->chg_param->fcc_step_size_ua) {
		pr_err("Invalid fcc stepper step size, value 0\n");
		return;
	}

	total_fcc_ua = main_fcc_ua + parallel_fcc_ua;
	override = is_override_vote_enabled_locked(chip->fcc_main_votable);
	if (override) {
		/*
		 * FCC stepper params need re-calculation in override mode
		 * only if there is change in Main or total FCC
		 */

		main_set_fcc_ua = get_effective_result_locked(
							chip->fcc_main_votable);
		if ((main_set_fcc_ua != chip->override_main_fcc_ua)
				|| (total_fcc_ua != chip->total_fcc_ua)) {
			chip->override_main_fcc_ua = main_set_fcc_ua;
			chip->total_fcc_ua = total_fcc_ua;
		} else {
			goto skip_fcc_step_update;
		}
	}

	/*
	 * If override vote is removed then start main FCC from the
	 * last overridden value.
	 * Clear slave_fcc if requested parallel current is 0 i.e.
	 * parallel is disabled.
	 */
	if (chip->override_main_fcc_ua && !override) {
		chip->main_fcc_ua = chip->override_main_fcc_ua;
		chip->override_main_fcc_ua = 0;
		if (!parallel_fcc_ua)
			chip->slave_fcc_ua = 0;
	} else {
		chip->main_fcc_ua = get_effective_result_locked(
						chip->fcc_main_votable);
	}

	/* Skip stepping if override vote is applied on main */
	if (override) {
		chip->main_step_fcc_count = 0;
		chip->main_step_fcc_residual = 0;
	} else {
		chip->main_step_fcc_dir =
				(main_fcc_ua > chip->main_fcc_ua) ?
					STEP_UP : STEP_DOWN;
		chip->main_step_fcc_count =
				abs(main_fcc_ua - chip->main_fcc_ua) /
					chip->chg_param->fcc_step_size_ua;
		chip->main_step_fcc_residual =
				abs(main_fcc_ua - chip->main_fcc_ua) %
					chip->chg_param->fcc_step_size_ua;
	}

	 /* Calculate CP_ILIM based on adapter limit and max. FCC */
	if (!parallel_fcc_ua && is_cp_available(chip) && override) {
		if (!chip->cp_ilim_votable)
			chip->cp_ilim_votable = find_votable("CP_ILIM");

		target_icl = get_adapter_icl_based_ilim(chip) * 2;
		total_fcc_ua -= chip->main_fcc_ua;

		/*
		 * CP_ILIM = parallel_fcc_ua / 2.
		 * Calculate parallel_fcc_ua as follows:
		 * parallel_fcc_ua is based minimum of total FCC
		 * or adapter's maximum allowed ICL limitation(if adapter
		 * has max. ICL limitations).
		 */
		parallel_fcc_ua = (target_icl > 0) ?
				min(target_icl, total_fcc_ua) : total_fcc_ua;
	}

	/* Skip stepping if override vote is applied on CP */
	if (chip->cp_ilim_votable
		&& is_override_vote_enabled(chip->cp_ilim_votable)) {
		chip->parallel_step_fcc_count = 0;
		chip->parallel_step_fcc_residual = 0;
	} else {
		chip->parallel_step_fcc_dir =
				(parallel_fcc_ua > chip->slave_fcc_ua) ?
					STEP_UP : STEP_DOWN;
		chip->parallel_step_fcc_count =
				abs(parallel_fcc_ua - chip->slave_fcc_ua) /
					chip->chg_param->fcc_step_size_ua;
		chip->parallel_step_fcc_residual =
				abs(parallel_fcc_ua - chip->slave_fcc_ua) %
					chip->chg_param->fcc_step_size_ua;
	}
skip_fcc_step_update:
	if (chip->parallel_step_fcc_count || chip->parallel_step_fcc_residual
		|| chip->main_step_fcc_count || chip->main_step_fcc_residual)
		chip->step_fcc = 1;

	pl_dbg(chip, PR_PARALLEL,
		"Main FCC Stepper parameters: target_main_fcc: %d, current_main_fcc: %d main_step_direction: %d, main_step_count: %d, main_residual_fcc: %d override_main_fcc_ua: %d override: %d\n",
		main_fcc_ua, chip->main_fcc_ua, chip->main_step_fcc_dir,
		chip->main_step_fcc_count, chip->main_step_fcc_residual,
		chip->override_main_fcc_ua, override);
	pl_dbg(chip, PR_PARALLEL,
		"Parallel FCC Stepper parameters: target_pl_fcc: %d current_pl_fcc: %d parallel_step_direction: %d, parallel_step_count: %d, parallel_residual_fcc: %d\n",
		parallel_fcc_ua, chip->slave_fcc_ua,
		chip->parallel_step_fcc_dir, chip->parallel_step_fcc_count,
		chip->parallel_step_fcc_residual);
	pl_dbg(chip, PR_PARALLEL, "FCC Stepper parameters: step_fcc=%d\n",
		chip->step_fcc);
}

#define MINIMUM_PARALLEL_FCC_UA		500000
#define PL_TAPER_WORK_DELAY_MS		500
#define TAPER_RESIDUAL_PCT		90
#define TAPER_REDUCTION_UA		200000
static void pl_taper_work(struct work_struct *work)
{
	struct pl_data *chip = container_of(work, struct pl_data,
						pl_taper_work);
	union power_supply_propval pval = {0, };
	int rc;
	int fcc_ua, total_fcc_ua, master_fcc_ua, slave_fcc_ua = 0;

	chip->taper_entry_fv = get_effective_result(chip->fv_votable);
	chip->taper_work_running = true;
	fcc_ua = get_client_vote(chip->fcc_votable, BATT_PROFILE_VOTER);
	vote(chip->fcc_votable, TAPER_STEPPER_VOTER, true, fcc_ua);
	while (true) {
		if (get_effective_result(chip->pl_disable_votable)) {
			/*
			 * if parallel's FCC share is low, simply disable
			 * parallel with TAPER_END_VOTER
			 */
			total_fcc_ua = get_effective_result_locked(
					chip->fcc_votable);
			get_fcc_split(chip, total_fcc_ua, &master_fcc_ua,
					&slave_fcc_ua);
			if (slave_fcc_ua <= MINIMUM_PARALLEL_FCC_UA) {
				pl_dbg(chip, PR_PARALLEL, "terminating: parallel's share is low\n");
				vote(chip->pl_disable_votable, TAPER_END_VOTER,
						true, 0);
			} else {
				pl_dbg(chip, PR_PARALLEL, "terminating: parallel disabled\n");
			}
			goto done;
		}

		/*
		 * Due to reduction of float voltage in JEITA condition taper
		 * charging can be initiated at a lower FV. On removal of JEITA
		 * condition, FV readjusts itself. However, once taper charging
		 * is initiated, it doesn't exits until parallel chaging is
		 * disabled due to which FCC doesn't scale back to its original
		 * value, leading to slow charging thereafter.
		 * Check if FV increases in comparison to FV at which taper
		 * charging was initiated, and if yes, exit taper charging.
		 */
		if (get_effective_result(chip->fv_votable) >
						chip->taper_entry_fv) {
			pl_dbg(chip, PR_PARALLEL, "Float voltage increased. Exiting taper\n");
			goto done;
		} else {
			chip->taper_entry_fv =
					get_effective_result(chip->fv_votable);
		}

		rc = power_supply_get_property(chip->batt_psy,
				       POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
		if (rc < 0) {
			pr_err("Couldn't get batt charge type rc=%d\n", rc);
			goto done;
		}

		chip->charge_type = pval.intval;
		if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE) {
			fcc_ua = get_client_vote(chip->fcc_votable,
					TAPER_STEPPER_VOTER);
			if (fcc_ua < 0) {
				pr_err("Couldn't get fcc, exiting taper work\n");
				goto done;
			}
			fcc_ua -= TAPER_REDUCTION_UA;
			if (fcc_ua < 0) {
				pr_err("Can't reduce FCC any more\n");
				goto done;
			}

			pl_dbg(chip, PR_PARALLEL, "master is taper charging; reducing FCC to %dua\n",
					fcc_ua);
			vote(chip->fcc_votable, TAPER_STEPPER_VOTER,
					true, fcc_ua);
		} else {
			pl_dbg(chip, PR_PARALLEL, "master is fast charging; waiting for next taper\n");
		}

		/* wait for the charger state to deglitch after FCC change */
		msleep(PL_TAPER_WORK_DELAY_MS);
	}
done:
	chip->taper_work_running = false;
	vote(chip->fcc_votable, TAPER_STEPPER_VOTER, false, 0);
	vote(chip->pl_awake_votable, TAPER_END_VOTER, false, 0);
}

static int pl_fcc_main_vote_callback(struct votable *votable, void *data,
			int fcc_main_ua, const char *client)
{
	struct pl_data *chip = data;
	int rc;

	rc = chip->chg_param->iio_write(chip->dev,
		PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX, fcc_main_ua);
	if (rc < 0)
		pr_err("Couldn't set constant_charge_current_max, rc=%d\n", rc);

	return rc;
}

static int pl_fcc_vote_callback(struct votable *votable, void *data,
			int total_fcc_ua, const char *client)
{
	struct pl_data *chip = data;
	int master_fcc_ua = total_fcc_ua, slave_fcc_ua = 0;
	int cp_fcc_ua = 0, rc, val = 0;

	if (total_fcc_ua < 0)
		return 0;

	if (!chip->cp_disable_votable)
		chip->cp_disable_votable = find_votable("CP_DISABLE");

	if (IS_ERR_OR_NULL(chip->iio_chan_list_cp))
		is_cp_available(chip);

	/*
	 * Search for a slave charger channel specifically,
	 * which would be available only if slave got probed.
	 */
	if (!chip->iio_chan_list_cp_slave) {
		chip->iio_chan_list_cp_slave = devm_iio_channel_get(chip->dev,
			"current_capability");
		if (PTR_ERR(chip->iio_chan_list_cp_slave) == -EPROBE_DEFER)
			chip->iio_chan_list_cp_slave = NULL;
	}

	if (!chip->cp_slave_disable_votable)
		chip->cp_slave_disable_votable =
			find_votable("CP_SLAVE_DISABLE");

	/*
	 * CP charger current = Total FCC - Main charger's FCC.
	 * Main charger FCC is userspace's override vote on main.
	 */
	cp_fcc_ua = total_fcc_ua - chip->chg_param->forced_main_fcc;
	pl_dbg(chip, PR_PARALLEL,
		"cp_fcc_ua=%d total_fcc_ua=%d forced_main_fcc=%d\n",
		cp_fcc_ua, total_fcc_ua, chip->chg_param->forced_main_fcc);
	if (cp_fcc_ua > 0) {
		if (!IS_ERR_OR_NULL(chip->iio_chan_list_cp)) {
			rc = battery_read_iio_prop(chip, CP, BAT_CP_MIN_ICL,
				&val);
			if (rc < 0)
				pr_err("Couldn't get MIN ICL threshold rc=%d\n",
									rc);
		}

		if (!IS_ERR_OR_NULL(chip->iio_chan_list_cp_slave) &&
			chip->cp_slave_disable_votable) {
			/*
			 * Disable Slave CP if FCC share
			 * falls below 3 * min ICL threshold.
			 */
			vote(chip->cp_slave_disable_votable, FCC_VOTER,
				(cp_fcc_ua < (3 * val)), 0);
		}

		if (chip->cp_disable_votable) {
			/*
			 * Disable Master CP if FCC share
			 * falls below 2 * min ICL threshold.
			 */
			vote(chip->cp_disable_votable, FCC_VOTER,
			     (cp_fcc_ua < (2 * val)), 0);
		}
	}

	if (chip->pl_mode != QTI_POWER_SUPPLY_PL_NONE) {
		get_fcc_split(chip, total_fcc_ua, &master_fcc_ua,
				&slave_fcc_ua);

		if (slave_fcc_ua > MINIMUM_PARALLEL_FCC_UA) {
			vote(chip->pl_disable_votable, PL_FCC_LOW_VOTER,
							false, 0);
		} else {
			vote(chip->pl_disable_votable, PL_FCC_LOW_VOTER,
							true, 0);
		}
	}

	rerun_election(chip->pl_disable_votable);
	/* When FCC changes, trigger psy changed event for CC mode */

	if (!IS_ERR_OR_NULL(chip->iio_chan_list_cp))
		power_supply_changed(chip->cp_master_psy);

	return 0;
}

static void fcc_stepper_work(struct work_struct *work)
{
	struct pl_data *chip = container_of(work, struct pl_data,
			fcc_stepper_work.work);
	union power_supply_propval pval = {0, };
	int reschedule_ms = 0, rc = 0, charger_present = 0;
	int main_fcc = chip->main_fcc_ua;
	int parallel_fcc = chip->slave_fcc_ua;

	/* Check whether USB is present or not */
	rc = power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
	if (rc < 0)
		pr_err("Couldn't get USB Present status, rc=%d\n", rc);

	charger_present = pval.intval;

	/*Check whether DC charger is present or not */
	if (!chip->dc_psy)
		chip->dc_psy = power_supply_get_by_name("dc");
	if (chip->dc_psy) {
		rc = power_supply_get_property(chip->dc_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
		if (rc < 0)
			pr_err("Couldn't get DC Present status, rc=%d\n", rc);

		charger_present |= pval.intval;
	}

	/*
	 * If USB is not present, then set parallel FCC to min value and
	 * main FCC to the effective value of FCC votable and exit.
	 */
	if (!charger_present) {
		/* Disable parallel */
		parallel_fcc = 0;

		if (!IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel)) {
			rc = battery_write_iio_prop(chip, SMB_PARALLEL,
				BAT_SMB_PARALLEL_INPUT_SUSPEND, 1);
			if (rc < 0) {
				pr_err("Couldn't change slave suspend state rc=%d\n",
					rc);
				goto out;
			}
			chip->pl_disable = true;
			power_supply_changed(chip->pl_psy);
		}

		main_fcc = get_effective_result_locked(chip->fcc_votable);
		vote(chip->fcc_main_votable, FCC_STEPPER_VOTER, true, main_fcc);
		goto stepper_exit;
	}

	if (chip->main_step_fcc_count) {
		main_fcc += (chip->chg_param->fcc_step_size_ua
					* chip->main_step_fcc_dir);
		chip->main_step_fcc_count--;
		reschedule_ms = chip->chg_param->fcc_step_delay_ms;
	} else if (chip->main_step_fcc_residual) {
		main_fcc += chip->main_step_fcc_residual
					* chip->main_step_fcc_dir;
		chip->main_step_fcc_residual = 0;
	}

	if (chip->parallel_step_fcc_count) {
		parallel_fcc += (chip->chg_param->fcc_step_size_ua
					* chip->parallel_step_fcc_dir);
		chip->parallel_step_fcc_count--;
		reschedule_ms = chip->chg_param->fcc_step_delay_ms;
	} else if (chip->parallel_step_fcc_residual) {
		parallel_fcc += chip->parallel_step_fcc_residual;
		chip->parallel_step_fcc_residual = 0;
	}

	if (parallel_fcc < chip->slave_fcc_ua) {
		/* Set parallel FCC */
		if (!IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel) &&
			!chip->pl_disable) {
			if (parallel_fcc < MINIMUM_PARALLEL_FCC_UA) {
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_INPUT_SUSPEND, 1);

				if (rc < 0) {
					pr_err("Couldn't change slave suspend state rc=%d\n",
						rc);
					goto out;
				}

				if (IS_USBIN(chip->pl_mode))
					split_settled(chip);

				parallel_fcc = 0;
				chip->parallel_step_fcc_count = 0;
				chip->parallel_step_fcc_residual = 0;
				chip->total_settled_ua = 0;
				chip->pl_settled_ua = 0;
				chip->pl_disable = true;
				power_supply_changed(chip->pl_psy);
			} else {
				/* Set Parallel FCC */
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX,
					parallel_fcc);
				if (rc < 0) {
					pr_err("Couldn't set parallel charger fcc, rc=%d\n",
						rc);
					goto out;
				}
			}
		}

		/* Set main FCC */
		vote(chip->fcc_main_votable, FCC_STEPPER_VOTER, true, main_fcc);
	} else {
		/* Set main FCC */
		vote(chip->fcc_main_votable, FCC_STEPPER_VOTER, true, main_fcc);

		/* Set parallel FCC */
		if (!IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel)) {
			rc = battery_write_iio_prop(chip, SMB_PARALLEL,
				BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX,
				parallel_fcc);
			if (rc < 0) {
				pr_err("Couldn't set parallel charger fcc, rc=%d\n",
					rc);
				goto out;
			}

			/*
			 * Enable parallel charger only if it was disabled
			 * earlier and configured slave fcc is greater than or
			 * equal to minimum parallel FCC value.
			 */
			if (chip->pl_disable && parallel_fcc
					>= MINIMUM_PARALLEL_FCC_UA) {
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_INPUT_SUSPEND, 0);

				if (rc < 0) {
					pr_err("Couldn't change slave suspend state rc=%d\n",
						rc);
					goto out;
				}

				if (IS_USBIN(chip->pl_mode))
					split_settled(chip);

				chip->pl_disable = false;
				power_supply_changed(chip->pl_psy);
			}
		}
	}

stepper_exit:
	chip->main_fcc_ua = main_fcc;
	chip->slave_fcc_ua = parallel_fcc;
	cp_configure_ilim(chip, FCC_VOTER, chip->slave_fcc_ua / 2);

	if (reschedule_ms) {
		schedule_delayed_work(&chip->fcc_stepper_work,
				msecs_to_jiffies(reschedule_ms));
		pr_debug("Rescheduling FCC_STEPPER work\n");
		return;
	}
out:
	chip->step_fcc = 0;
	vote(chip->pl_awake_votable, FCC_STEPPER_VOTER, false, 0);
}

static bool is_batt_available(struct pl_data *chip)
{
	if (!chip->batt_psy)
		chip->batt_psy = power_supply_get_by_name("battery");

	if (!chip->batt_psy)
		return false;

	return true;
}

#define PARALLEL_FLOAT_VOLTAGE_DELTA_UV 50000
static int pl_fv_vote_callback(struct votable *votable, void *data,
			int fv_uv, const char *client)
{
	struct pl_data *chip = data;
	union power_supply_propval pval = {0, };
	int rc = 0, val;

	if (fv_uv < 0)
		return 0;

	val = fv_uv;

	rc = chip->chg_param->iio_write(chip->dev, PSY_IIO_VOLTAGE_MAX, val);
	if (rc < 0) {
		pr_err("Couldn't set main fv, rc=%d\n", rc);
		return rc;
	}

	if (chip->pl_mode != QTI_POWER_SUPPLY_PL_NONE) {
		val += PARALLEL_FLOAT_VOLTAGE_DELTA_UV;
		rc = battery_write_iio_prop(chip, SMB_PARALLEL,
			BAT_SMB_PARALLEL_VOLTAGE_MAX, val);
		if (rc < 0) {
			pr_err("Couldn't set float on parallel rc=%d\n", rc);
			return rc;
		}
	}

	/*
	 * check for termination at reduced float voltage and re-trigger
	 * charging if new float voltage is above last FV.
	 */
	if ((chip->float_voltage_uv < fv_uv) && is_batt_available(chip)) {
		rc = power_supply_get_property(chip->batt_psy,
				POWER_SUPPLY_PROP_STATUS, &pval);
		if (rc < 0) {
			pr_err("Couldn't get battery status rc=%d\n", rc);
		} else {
			if (pval.intval == POWER_SUPPLY_STATUS_FULL) {
				pr_debug("re-triggering charging\n");
				val = 1;
				rc = chip->chg_param->iio_write(chip->dev,
					PSY_IIO_FORCE_RECHARGE, val);
				if (rc < 0)
					pr_err("Couldn't set force recharge rc=%d\n",
							rc);
			}
		}
	}

	chip->float_voltage_uv = fv_uv;

	return 0;
}

#define ICL_STEP_UA	25000
#define PL_DELAY_MS     3000
static int usb_icl_vote_callback(struct votable *votable, void *data,
			int icl_ua, const char *client)
{
	int rc, val;
	struct pl_data *chip = data;
	union power_supply_propval pval = {0, };
	bool rerun_aicl = false, dc_present = false;

	if (client == NULL)
		icl_ua = INT_MAX;

	/*
	 * Disable parallel for new ICL vote - the call to split_settled will
	 * ensure that all the input current limit gets assigned to the main
	 * charger.
	 */
	vote(chip->pl_disable_votable, ICL_CHANGE_VOTER, true, 0);

	/*
	 * if (ICL < 1400)
	 *	disable parallel charger using USBIN_I_VOTER
	 * else
	 *	instead of re-enabling here rely on status_changed_work
	 *	(triggered via AICL completed or scheduled from here to
	 *	unvote USBIN_I_VOTER) the status_changed_work enables
	 *	USBIN_I_VOTER based on settled current.
	 */
	if (icl_ua <= 1400000)
		vote(chip->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
	else
		schedule_delayed_work(&chip->status_change_work,
						msecs_to_jiffies(PL_DELAY_MS));

	/* rerun AICL */
	/* get the settled current */
	rc = chip->chg_param->iio_read(chip->dev,
		PSY_IIO_MAIN_INPUT_CURRENT_SETTLED, &val);
	if (rc < 0) {
		pr_err("Couldn't get aicl settled value rc=%d\n", rc);
		return rc;
	}

	/* rerun AICL if new ICL is above settled ICL */
	if (icl_ua > val)
		rerun_aicl = true;

	if (rerun_aicl && (chip->wa_flags & AICL_RERUN_WA_BIT)) {
		/* set a lower ICL */
		val = max(val - ICL_STEP_UA, ICL_STEP_UA);
		rc = chip->chg_param->iio_write(chip->dev, PSY_IIO_CURRENT_MAX,
			val);
		if (rc < 0)
			pr_err("Couldn't set main current_max, rc=%d\n", rc);
	}

	/* set the effective ICL */
	val = icl_ua;
	rc = chip->chg_param->iio_write(chip->dev, PSY_IIO_CURRENT_MAX, val);
	if (rc < 0)
		pr_err("Couldn't set main current_max, rc=%d\n", rc);

	vote(chip->pl_disable_votable, ICL_CHANGE_VOTER, false, 0);

	/* Configure ILIM based on AICL result only if input mode is USBMID */
	if (cp_get_parallel_mode(chip, PARALLEL_INPUT_MODE)
					== QTI_POWER_SUPPLY_PL_USBMID_USBMID) {
		if (chip->dc_psy) {
			rc = power_supply_get_property(chip->dc_psy,
					POWER_SUPPLY_PROP_PRESENT, &pval);
			if (rc < 0) {
				pr_err("Couldn't get DC PRESENT rc=%d\n", rc);
				return rc;
			}
			dc_present = pval.intval;
		}

		/* Don't configure ILIM if DC is present */
		if (!dc_present)
			cp_configure_ilim(chip, ICL_CHANGE_VOTER, icl_ua);
	}

	return 0;
}

static void pl_disable_forever_work(struct work_struct *work)
{
	struct pl_data *chip = container_of(work,
			struct pl_data, pl_disable_forever_work);

	/* Disable Parallel charger forever */
	vote(chip->pl_disable_votable, PL_HW_ABSENT_VOTER, true, 0);

	/* Re-enable autonomous mode */
	if (chip->hvdcp_hw_inov_dis_votable)
		vote(chip->hvdcp_hw_inov_dis_votable, PL_VOTER, false, 0);
}

static int pl_disable_vote_callback(struct votable *votable,
		void *data, int pl_disable, const char *client)
{
	struct pl_data *chip = data;
	union power_supply_propval pval = {0, };
	int master_fcc_ua = 0, total_fcc_ua = 0, slave_fcc_ua = 0;
	int rc = 0, cp_ilim;
	bool disable = false;

	if (!is_batt_available(chip))
		return -ENODEV;

	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("Couldn't get usb psy\n");
		return -ENODEV;
	}

	rc = chip->chg_param->iio_read(chip->dev, PSY_IIO_FCC_STEPPER_ENABLE,
		&pval.intval);
	if (rc < 0) {
		pr_err("Couldn't read FCC step update status, rc=%d\n", rc);
		return rc;
	}
	chip->fcc_stepper_enable = pval.intval;
	pr_debug("FCC Stepper %s\n", pval.intval ? "enabled" : "disabled");

	rc = chip->chg_param->iio_read(chip->dev, PSY_IIO_MAIN_FCC_MAX,
		&pval.intval);
	if (rc < 0) {
		pl_dbg(chip, PR_PARALLEL,
			"Couldn't read primary charger FCC upper limit, rc=%d\n",
			rc);
	} else if (pval.intval > 0) {
		chip->main_fcc_max = pval.intval;
	}

	if (chip->fcc_stepper_enable) {
		cancel_delayed_work_sync(&chip->fcc_stepper_work);
		vote(chip->pl_awake_votable, FCC_STEPPER_VOTER, false, 0);
	}

	total_fcc_ua = get_effective_result_locked(chip->fcc_votable);

	if (chip->pl_mode != QTI_POWER_SUPPLY_PL_NONE && !pl_disable) {
		rc = validate_parallel_icl(chip, &disable);
		if (rc < 0)
			return rc;

		if (disable) {
			pr_info("Parallel ICL is less than min ICL(%d), skipping parallel enable\n",
					chip->pl_min_icl_ua);
			return 0;
		}

		 /* enable parallel charging */
		rc = battery_read_iio_prop(chip, SMB_PARALLEL,
				BAT_SMB_PARALLEL_CHARGE_TYPE, &pval.intval);
		if (rc == -ENODEV) {
			/*
			 * -ENODEV is returned only if parallel chip
			 * is not present in the system.
			 * Disable parallel charger forever.
			 */
			schedule_work(&chip->pl_disable_forever_work);
			return rc;
		}
		rerun_election(chip->fv_votable);

		get_fcc_split(chip, total_fcc_ua, &master_fcc_ua,
				&slave_fcc_ua);

		if (chip->fcc_stepper_enable) {
			get_fcc_stepper_params(chip, master_fcc_ua,
					slave_fcc_ua);
			if (chip->step_fcc) {
				vote(chip->pl_awake_votable, FCC_STEPPER_VOTER,
					true, 0);
				schedule_delayed_work(&chip->fcc_stepper_work,
					0);
			}
		} else {
			/*
			 * If there is an increase in slave share
			 * (Also handles parallel enable case)
			 *	Set Main ICL then slave FCC
			 * else
			 * (Also handles parallel disable case)
			 *	Set slave ICL then main FCC.
			 */
			if (slave_fcc_ua > chip->slave_fcc_ua) {
				vote(chip->fcc_main_votable, MAIN_FCC_VOTER,
							true, master_fcc_ua);
				pval.intval = slave_fcc_ua;
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX,
					pval.intval);
				if (rc < 0) {
					pr_err("Couldn't set parallel fcc, rc=%d\n",
						rc);
					return rc;
				}
				chip->slave_fcc_ua = slave_fcc_ua;
			} else {
				pval.intval = slave_fcc_ua;
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX,
					pval.intval);
				if (rc < 0) {
					pr_err("Couldn't set parallel fcc, rc=%d\n",
						rc);
					return rc;
				}
				chip->slave_fcc_ua = slave_fcc_ua;
				vote(chip->fcc_main_votable, MAIN_FCC_VOTER,
							true, master_fcc_ua);
			}

			/*
			 * Enable will be called with a valid pl_psy always. The
			 * PARALLEL_PSY_VOTER keeps it disabled unless a pl_psy
			 * is seen.
			 */
			pval.intval = 0;
			rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_INPUT_SUSPEND,
					pval.intval);
			if (rc < 0)
				pr_err("Couldn't change slave suspend state rc=%d\n",
					rc);
			if (IS_USBIN(chip->pl_mode))
				split_settled(chip);
		}

		/*
		 * we could have been enabled while in taper mode,
		 *  start the taper work if so
		 */
		rc = power_supply_get_property(chip->batt_psy,
				       POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
		if (rc < 0) {
			pr_err("Couldn't get batt charge type rc=%d\n", rc);
		} else {
			if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE
				&& !chip->taper_work_running) {
				pl_dbg(chip, PR_PARALLEL,
					"pl enabled in Taper scheduing work\n");
				vote(chip->pl_awake_votable, TAPER_END_VOTER,
						true, 0);
				queue_work(system_long_wq,
						&chip->pl_taper_work);
			}
		}

		pl_dbg(chip, PR_PARALLEL, "master_fcc=%d slave_fcc=%d distribution=(%d/%d)\n",
			master_fcc_ua, slave_fcc_ua,
			(master_fcc_ua * 100) / total_fcc_ua,
			(slave_fcc_ua * 100) / total_fcc_ua);
	} else {
		if (chip->main_fcc_max)
			get_main_fcc_config(chip, &total_fcc_ua);

		if (!chip->fcc_stepper_enable) {
			if (IS_USBIN(chip->pl_mode))
				split_settled(chip);

			/* pl_psy may be NULL while in the disable branch */
			if (!IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel)) {
				pval.intval = 1;
				rc = battery_write_iio_prop(chip, SMB_PARALLEL,
					BAT_SMB_PARALLEL_INPUT_SUSPEND,
					pval.intval);
				if (rc < 0)
					pr_err("Couldn't change slave suspend state rc=%d\n",
						rc);
			}

			/* main psy gets all share */
			vote(chip->fcc_main_votable, MAIN_FCC_VOTER, true,
								total_fcc_ua);
			cp_ilim = total_fcc_ua - get_effective_result_locked(
							chip->fcc_main_votable);
			if (cp_ilim > 0)
				cp_configure_ilim(chip, FCC_VOTER, cp_ilim / 2);

			/* reset parallel FCC */
			chip->slave_fcc_ua = 0;
			chip->total_settled_ua = 0;
			chip->pl_settled_ua = 0;
		} else {
			get_fcc_stepper_params(chip, total_fcc_ua, 0);
			if (chip->step_fcc) {
				vote(chip->pl_awake_votable, FCC_STEPPER_VOTER,
					true, 0);
				schedule_delayed_work(&chip->fcc_stepper_work,
					0);
			}
		}

		rerun_election(chip->fv_votable);
	}

	/* notify parallel state change */
	if (!IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel) &&
		(chip->pl_disable != pl_disable)
				&& !chip->fcc_stepper_enable) {
		power_supply_changed(chip->pl_psy);
		chip->pl_disable = (bool)pl_disable;
	}

	pl_dbg(chip, PR_PARALLEL, "parallel charging %s\n",
		   pl_disable ? "disabled" : "enabled");

	return 0;
}

static int pl_enable_indirect_vote_callback(struct votable *votable,
			void *data, int pl_enable, const char *client)
{
	struct pl_data *chip = data;

	vote(chip->pl_disable_votable, PL_INDIRECT_VOTER, !pl_enable, 0);

	return 0;
}

static int pl_awake_vote_callback(struct votable *votable,
			void *data, int awake, const char *client)
{
	struct pl_data *chip = data;

	if (awake)
		__pm_stay_awake(chip->pl_ws);
	else
		__pm_relax(chip->pl_ws);

	pr_debug("client: %s awake: %d\n", client, awake);
	return 0;
}

static bool is_parallel_available(struct pl_data *chip)
{
	union power_supply_propval pval = {0, };
	int rc = 0;
	struct iio_channel **iio_list;

	if (IS_ERR(chip->iio_chan_list_smb_parallel))
		return false;

	if (!chip->iio_chan_list_smb_parallel) {
		iio_list = get_ext_channels(chip->dev,
			bat_smb_parallel_ext_iio_chan,
			ARRAY_SIZE(bat_smb_parallel_ext_iio_chan));
		if (IS_ERR(iio_list)) {
			rc = PTR_ERR(iio_list);
			if (rc != -EPROBE_DEFER) {
				dev_err(chip->dev, "Failed to get channels, %d\n",
					rc);
				chip->iio_chan_list_smb_parallel =
				ERR_PTR(-EINVAL);
			}
			return false;
		}
		chip->iio_chan_list_smb_parallel = iio_list;
	}

	if (!chip->pl_psy) {
		chip->pl_psy = power_supply_get_by_name("parallel");
		if (!chip->pl_psy)
			return false;
	}

	vote(chip->pl_disable_votable, PARALLEL_PSY_VOTER, false, 0);

	rc = battery_read_iio_prop(chip, SMB_PARALLEL, BAT_SMB_PARALLEL_MODE,
			&pval.intval);
	if (rc < 0) {
		pr_err("Couldn't get parallel mode from parallel rc=%d\n",
				rc);
		return false;
	}

	/*
	 * Note that pl_mode will be updated to anything other than a _NONE
	 * only after pl_psy is found. IOW pl_mode != _NONE implies that
	 * pl_psy is present and valid.
	 */
	chip->pl_mode = pval.intval;

	/* Disable autonomous votage increments for USBIN-USBIN */
	if (IS_USBIN(chip->pl_mode)
			&& (chip->wa_flags & FORCE_INOV_DISABLE_BIT)) {
		if (!chip->hvdcp_hw_inov_dis_votable)
			chip->hvdcp_hw_inov_dis_votable =
					find_votable("HVDCP_HW_INOV_DIS");
		if (chip->hvdcp_hw_inov_dis_votable)
			/* Read current pulse count */
			vote(chip->hvdcp_hw_inov_dis_votable, PL_VOTER,
					true, 0);
		else
			return false;
	}

	rc = battery_read_iio_prop(chip, SMB_PARALLEL,
		BAT_SMB_PARALLEL_BATFET_MODE, &pval.intval);
	if (rc < 0) {
		pr_err("Couldn't get parallel batfet mode rc=%d\n",
				rc);
		return false;
	}
	chip->pl_batfet_mode = pval.intval;

	pval.intval = 0;
	rc = battery_read_iio_prop(chip, SMB_PARALLEL, BAT_SMB_PARALLEL_MIN_ICL,
			&pval.intval);
	if (rc < 0)
		pr_err("Couldn't get min_icl rc=%d\n", rc);
	else
		chip->pl_min_icl_ua = pval.intval;

	chip->pl_fcc_max = INT_MAX;

	rc = battery_read_iio_prop(chip, SMB_PARALLEL, BAT_SMB_PARALLEL_FCC_MAX,
			&pval.intval);
	if (!rc)
		chip->pl_fcc_max = pval.intval;
	else
		pr_err("Couldn't get fcc_max rc=%d\n", rc);

	return true;
}

static void handle_main_charge_type(struct pl_data *chip)
{
	union power_supply_propval pval = {0, };
	int rc;

	rc = power_supply_get_property(chip->batt_psy,
			       POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		return;
	}

	/* not fast/not taper state to disables parallel */
	if ((pval.intval != POWER_SUPPLY_CHARGE_TYPE_FAST)
		&& (pval.intval != POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE)) {
		vote(chip->pl_disable_votable, CHG_STATE_VOTER, true, 0);
		chip->charge_type = pval.intval;
		return;
	}

	/* handle taper charge entry */
	if (chip->charge_type == POWER_SUPPLY_CHARGE_TYPE_FAST
		&& (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE)) {
		chip->charge_type = pval.intval;
		if (!chip->taper_work_running) {
			pl_dbg(chip, PR_PARALLEL, "taper entry scheduling work\n");
			vote(chip->pl_awake_votable, TAPER_END_VOTER, true, 0);
			queue_work(system_long_wq, &chip->pl_taper_work);
		}
		return;
	}

	/* handle fast/taper charge entry */
	if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE
			|| pval.intval == POWER_SUPPLY_CHARGE_TYPE_FAST) {
		/*
		 * Undo parallel charging termination if entered taper in
		 * reduced float voltage condition due to jeita mitigation.
		 */
		if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_FAST &&
			(chip->taper_entry_fv <
			get_effective_result(chip->fv_votable))) {
			vote(chip->pl_disable_votable, TAPER_END_VOTER,
				false, 0);
		}
		pl_dbg(chip, PR_PARALLEL, "chg_state enabling parallel\n");
		vote(chip->pl_disable_votable, CHG_STATE_VOTER, false, 0);
		chip->charge_type = pval.intval;
		return;
	}

	/* remember the new state only if it isn't any of the above */
	chip->charge_type = pval.intval;
}

#define MIN_ICL_CHANGE_DELTA_UA		300000
static void handle_settled_icl_change(struct pl_data *chip)
{
	union power_supply_propval pval = {0, };
	int new_total_settled_ua;
	int rc, val;
	int main_settled_ua;
	int main_limited;
	int total_current_ua;
	bool disable = false;

	total_current_ua = get_effective_result_locked(chip->usb_icl_votable);

	/*
	 * call aicl split only when USBIN_USBIN and enabled
	 * and if aicl changed
	 */
	rc = chip->chg_param->iio_read(chip->dev,
		PSY_IIO_MAIN_INPUT_CURRENT_SETTLED, &val);
	if (rc < 0) {
		pr_err("Couldn't get aicl settled value rc=%d\n", rc);
		return;
	}
	main_settled_ua = val;

	rc = chip->chg_param->iio_read(chip->dev, PSY_IIO_INPUT_CURRENT_LIMITED,
		&val);
	if (rc < 0) {
		pr_err("Couldn't get aicl settled value rc=%d\n", rc);
		return;
	}
	main_limited = val;

	if ((main_limited && (main_settled_ua + chip->pl_settled_ua) < 1400000)
			|| (main_settled_ua == 0)
			|| ((total_current_ua >= 0) &&
				(total_current_ua <= 1400000)))
		vote(chip->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
	else
		vote(chip->pl_enable_votable_indirect, USBIN_I_VOTER, true, 0);

	rerun_election(chip->fcc_votable);

	if (IS_USBIN(chip->pl_mode)) {
		/*
		 * call aicl split only when USBIN_USBIN and enabled
		 * and if settled current has changed by more than 300mA
		 */

		new_total_settled_ua = main_settled_ua + chip->pl_settled_ua;
		pl_dbg(chip, PR_PARALLEL,
			"total_settled_ua=%d settled_ua=%d new_total_settled_ua=%d\n",
			chip->total_settled_ua, pval.intval,
			new_total_settled_ua);

		/* If ICL change is small skip splitting */
		if (abs(new_total_settled_ua - chip->total_settled_ua)
						> MIN_ICL_CHANGE_DELTA_UA) {
			rc = validate_parallel_icl(chip, &disable);
			if (rc < 0)
				return;

			vote(chip->pl_disable_votable, ICL_LIMIT_VOTER,
						disable, 0);
			if (!get_effective_result_locked(
						chip->pl_disable_votable))
				split_settled(chip);
		}
	}
}

static void handle_parallel_in_taper(struct pl_data *chip)
{
	union power_supply_propval pval = {0, };
	int rc;

	if (get_effective_result_locked(chip->pl_disable_votable))
		return;

	if (IS_ERR_OR_NULL(chip->iio_chan_list_smb_parallel))
		return;

	rc = battery_read_iio_prop(chip, SMB_PARALLEL,
			BAT_SMB_PARALLEL_CHARGE_TYPE, &pval.intval);
	if (rc < 0) {
		pr_err("Couldn't get pl charge type rc=%d\n", rc);
		return;
	}

	/*
	 * if parallel is seen in taper mode ever, that is an anomaly and
	 * we disable parallel charger
	 */
	if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE) {
		vote(chip->pl_disable_votable, PL_TAPER_EARLY_BAD_VOTER,
				true, 0);
		return;
	}
}

static void handle_usb_change(struct pl_data *chip)
{
	int rc, val;
	union power_supply_propval pval = {0, };

	if (!chip->usb_psy)
		chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("Couldn't get usbpsy\n");
		return;
	}

	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
	if (rc < 0) {
		pr_err("Couldn't get present from USB rc=%d\n", rc);
		return;
	}

	if (!pval.intval) {
		/* USB removed: remove all stale votes */
		vote(chip->pl_disable_votable, TAPER_END_VOTER, false, 0);
		vote(chip->pl_disable_votable, PL_TAPER_EARLY_BAD_VOTER,
				false, 0);
		vote(chip->pl_disable_votable, ICL_LIMIT_VOTER, false, 0);
		chip->override_main_fcc_ua = 0;
		chip->total_fcc_ua = 0;
		chip->slave_fcc_ua = 0;
		chip->main_fcc_ua = 0;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	} else {
		rc = chip->chg_param->iio_read(chip->dev, PSY_IIO_USB_REAL_TYPE,
			&val);
		if (rc < 0)
			pr_err("Couldn't get USB real type rc=%d\n", rc);
		else
			chip->charger_type = val;
	}
}

static void status_change_work(struct work_struct *work)
{
	struct pl_data *chip = container_of(work,
			struct pl_data, status_change_work.work);

	/*
	 * re-run election for FCC/FV/ICL to ensure all
	 * votes are reflected on hardware
	 */
	rerun_election(chip->usb_icl_votable);
	rerun_election(chip->fcc_votable);
	rerun_election(chip->fv_votable);

	if (!is_batt_available(chip))
		return;

	is_parallel_available(chip);

	handle_usb_change(chip);
	handle_main_charge_type(chip);
	handle_settled_icl_change(chip);
	handle_parallel_in_taper(chip);
}

static int pl_notifier_call(struct notifier_block *nb,
		unsigned long ev, void *v)
{
	struct power_supply *psy = v;
	struct pl_data *chip = container_of(nb, struct pl_data, nb);

	if (ev != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if ((strcmp(psy->desc->name, "parallel") == 0)
		|| (strcmp(psy->desc->name, "battery") == 0))
		schedule_delayed_work(&chip->status_change_work, 0);

	return NOTIFY_OK;
}

static int pl_register_notifier(struct pl_data *chip)
{
	int rc;

	chip->nb.notifier_call = pl_notifier_call;
	rc = power_supply_reg_notifier(&chip->nb);
	if (rc < 0) {
		pr_err("Couldn't register psy notifier rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int pl_determine_initial_status(struct pl_data *chip)
{
	status_change_work(&chip->status_change_work.work);
	return 0;
}

static void pl_config_init(struct pl_data *chip, int smb_version)
{
	switch (smb_version) {
	case PMI8998_SUBTYPE:
	case PM660_SUBTYPE:
		chip->wa_flags = AICL_RERUN_WA_BIT | FORCE_INOV_DISABLE_BIT;
		break;
	default:
		break;
	}
}

static void qcom_batt_create_debugfs(struct pl_data *chip)
{

	chip->dfs_root = debugfs_create_dir("battery", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create battery debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	debugfs_create_u32("debug_mask", 0600, chip->dfs_root,
			&debug_mask);
}

#define DEFAULT_RESTRICTED_CURRENT_UA	1000000
int qcom_batt_init(struct device *dev, struct charger_param *chg_param)
{
	struct pl_data *chip;
	int rc = 0;

	if (!chg_param) {
		pr_err("invalid charger parameter\n");
		return -EINVAL;
	}

	if (!chg_param->iio_read || !chg_param->iio_write) {
		pr_err("Invalid iio read/write pointers\n");
		return -EINVAL;
	}

	/* initialize just once */
	if (the_chip) {
		pr_err("was initialized earlier. Failing now\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	qcom_batt_create_debugfs(chip);

	chip->slave_pct = 50;
	chip->chg_param = chg_param;
	pl_config_init(chip, chg_param->smb_version);
	chip->restricted_current = DEFAULT_RESTRICTED_CURRENT_UA;

	chip->pl_ws = wakeup_source_register(NULL, "qcom-battery");
	if (!chip->pl_ws)
		goto cleanup;

	INIT_DELAYED_WORK(&chip->status_change_work, status_change_work);
	INIT_WORK(&chip->pl_taper_work, pl_taper_work);
	INIT_WORK(&chip->pl_disable_forever_work, pl_disable_forever_work);
	INIT_DELAYED_WORK(&chip->fcc_stepper_work, fcc_stepper_work);

	chip->fcc_main_votable = create_votable("FCC_MAIN", VOTE_MIN,
					pl_fcc_main_vote_callback,
					chip);
	if (IS_ERR(chip->fcc_main_votable)) {
		rc = PTR_ERR(chip->fcc_main_votable);
		chip->fcc_main_votable = NULL;
		goto release_wakeup_source;
	}

	chip->fcc_votable = create_votable("FCC", VOTE_MIN,
					pl_fcc_vote_callback,
					chip);
	if (IS_ERR(chip->fcc_votable)) {
		rc = PTR_ERR(chip->fcc_votable);
		chip->fcc_votable = NULL;
		goto destroy_votable;
	}

	chip->fv_votable = create_votable("FV", VOTE_MIN,
					pl_fv_vote_callback,
					chip);
	if (IS_ERR(chip->fv_votable)) {
		rc = PTR_ERR(chip->fv_votable);
		chip->fv_votable = NULL;
		goto destroy_votable;
	}

	chip->usb_icl_votable = create_votable("USB_ICL", VOTE_MIN,
					usb_icl_vote_callback,
					chip);
	if (IS_ERR(chip->usb_icl_votable)) {
		rc = PTR_ERR(chip->usb_icl_votable);
		chip->usb_icl_votable = NULL;
		goto destroy_votable;
	}

	chip->pl_disable_votable = create_votable("PL_DISABLE", VOTE_SET_ANY,
					pl_disable_vote_callback,
					chip);
	if (IS_ERR(chip->pl_disable_votable)) {
		rc = PTR_ERR(chip->pl_disable_votable);
		chip->pl_disable_votable = NULL;
		goto destroy_votable;
	}
	vote(chip->pl_disable_votable, CHG_STATE_VOTER, true, 0);
	vote(chip->pl_disable_votable, TAPER_END_VOTER, false, 0);
	vote(chip->pl_disable_votable, PARALLEL_PSY_VOTER, true, 0);

	chip->pl_awake_votable = create_votable("PL_AWAKE", VOTE_SET_ANY,
					pl_awake_vote_callback,
					chip);
	if (IS_ERR(chip->pl_awake_votable)) {
		rc = PTR_ERR(chip->pl_awake_votable);
		chip->pl_awake_votable = NULL;
		goto destroy_votable;
	}

	chip->pl_enable_votable_indirect = create_votable("PL_ENABLE_INDIRECT",
					VOTE_SET_ANY,
					pl_enable_indirect_vote_callback,
					chip);
	if (IS_ERR(chip->pl_enable_votable_indirect)) {
		rc = PTR_ERR(chip->pl_enable_votable_indirect);
		chip->pl_enable_votable_indirect = NULL;
		goto destroy_votable;
	}

	vote(chip->pl_disable_votable, PL_INDIRECT_VOTER, true, 0);

	rc = pl_register_notifier(chip);
	if (rc < 0) {
		pr_err("Couldn't register psy notifier rc = %d\n", rc);
		goto unreg_notifier;
	}

	rc = pl_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n", rc);
		goto unreg_notifier;
	}

	chip->pl_disable = true;
	chip->cp_disabled = true;
	chip->qcom_batt_class.name = "qcom-battery",
	chip->qcom_batt_class.owner = THIS_MODULE,
	chip->qcom_batt_class.class_groups = batt_class_groups;

	rc = class_register(&chip->qcom_batt_class);
	if (rc < 0) {
		pr_err("couldn't register pl_data sysfs class rc = %d\n", rc);
		goto unreg_notifier;
	}

	the_chip = chip;

	return 0;

unreg_notifier:
	power_supply_unreg_notifier(&chip->nb);
destroy_votable:
	destroy_votable(chip->pl_enable_votable_indirect);
	destroy_votable(chip->pl_awake_votable);
	destroy_votable(chip->pl_disable_votable);
	destroy_votable(chip->fv_votable);
	destroy_votable(chip->fcc_votable);
	destroy_votable(chip->fcc_main_votable);
	destroy_votable(chip->usb_icl_votable);
release_wakeup_source:
	wakeup_source_unregister(chip->pl_ws);
cleanup:
	kfree(chip);
	return rc;
}

void qcom_batt_deinit(void)
{
	struct pl_data *chip = the_chip;

	if (chip == NULL)
		return;

	cancel_delayed_work_sync(&chip->status_change_work);
	cancel_work_sync(&chip->pl_taper_work);
	cancel_work_sync(&chip->pl_disable_forever_work);
	cancel_delayed_work_sync(&chip->fcc_stepper_work);

	power_supply_unreg_notifier(&chip->nb);
	destroy_votable(chip->pl_enable_votable_indirect);
	destroy_votable(chip->pl_awake_votable);
	destroy_votable(chip->pl_disable_votable);
	destroy_votable(chip->fv_votable);
	destroy_votable(chip->fcc_votable);
	destroy_votable(chip->fcc_main_votable);
	wakeup_source_unregister(chip->pl_ws);
	the_chip = NULL;
	kfree(chip);
}
