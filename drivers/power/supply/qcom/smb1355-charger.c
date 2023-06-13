// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "SMB1355: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/power_supply.h>
#include <linux/qti_power_supply.h>
#include <linux/workqueue.h>
#include <linux/pmic-voter.h>
#include <linux/string.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

/* SMB1355 registers, different than mentioned in smb-reg.h */

#define REVID_BASE	0x0100
#define I2C_SS_DIG_BASE 0x0E00
#define CHGR_BASE	0x1000
#define ANA2_BASE	0x1100
#define BATIF_BASE	0x1200
#define USBIN_BASE	0x1300
#define ANA1_BASE	0x1400
#define MISC_BASE	0x1600

#define REVID_MFG_ID_SPARE_REG                  (REVID_BASE + 0xFF)

#define I2C_SS_DIG_PMIC_SID_REG			(I2C_SS_DIG_BASE + 0x45)
#define PMIC_SID_MASK				GENMASK(3, 0)
#define PMIC_SID0_BIT				BIT(0)

#define BATTERY_STATUS_2_REG			(CHGR_BASE + 0x0B)
#define DISABLE_CHARGING_BIT			BIT(3)

#define BATTERY_STATUS_3_REG			(CHGR_BASE + 0x0C)
#define BATT_GT_PRE_TO_FAST_BIT			BIT(4)
#define ENABLE_CHARGING_BIT			BIT(3)

#define CHGR_CHARGING_ENABLE_CMD_REG		(CHGR_BASE + 0x42)
#define CHARGING_ENABLE_CMD_BIT			BIT(0)

#define CHGR_CFG2_REG				(CHGR_BASE + 0x51)
#define CHG_EN_SRC_BIT				BIT(7)
#define CHG_EN_POLARITY_BIT			BIT(6)

#define CFG_REG					(CHGR_BASE + 0x53)
#define CHG_OPTION_PIN_TRIM_BIT			BIT(7)
#define BATN_SNS_CFG_BIT			BIT(4)
#define CFG_TAPER_DIS_AFVC_BIT			BIT(3)
#define BATFET_SHUTDOWN_CFG_BIT			BIT(2)
#define VDISCHG_EN_CFG_BIT			BIT(1)
#define VCHG_EN_CFG_BIT				BIT(0)

#define FAST_CHARGE_CURRENT_CFG_REG		(CHGR_BASE + 0x61)
#define FAST_CHARGE_CURRENT_SETTING_MASK	GENMASK(7, 0)

#define CHGR_BATTOV_CFG_REG			(CHGR_BASE + 0x70)
#define BATTOV_SETTING_MASK			GENMASK(7, 0)

#define CHGR_PRE_TO_FAST_THRESHOLD_CFG_REG	(CHGR_BASE + 0x74)
#define PRE_TO_FAST_CHARGE_THRESHOLD_MASK	GENMASK(2, 0)

#define ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG	(ANA2_BASE + 0xF5)
#define TR_SBQ_ICL_1X_REF_OFFSET		GENMASK(4, 0)

#define POWER_MODE_HICCUP_CFG			(BATIF_BASE + 0x72)
#define MAX_HICCUP_DUETO_BATDIS_MASK		GENMASK(5, 2)
#define HICCUP_TIMEOUT_CFG_MASK			GENMASK(1, 0)

#define BATIF_CFG_SMISC_BATID_REG		(BATIF_BASE + 0x73)
#define CFG_SMISC_RBIAS_EXT_CTRL_BIT		BIT(2)

#define SMB2CHG_BATIF_ENG_SMISC_DIETEMP	(BATIF_BASE + 0xC0)
#define TDIE_COMPARATOR_THRESHOLD		GENMASK(5, 0)
#define DIE_LOW_RANGE_BASE_DEGC			34
#define DIE_LOW_RANGE_DELTA			16
#define DIE_LOW_RANGE_MAX_DEGC			97
#define DIE_LOW_RANGE_SHIFT			4

#define BATIF_ENG_SCMISC_SPARE1_REG		(BATIF_BASE + 0xC2)
#define EXT_BIAS_PIN_BIT			BIT(2)
#define DIE_TEMP_COMP_HYST_BIT			BIT(1)

#define ANA1_ENG_SREFGEN_CFG2_REG		(ANA1_BASE + 0xC1)
#define VALLEY_COMPARATOR_EN_BIT		BIT(0)

#define TEMP_COMP_STATUS_REG			(MISC_BASE + 0x07)
#define TEMP_RST_HOT_BIT			BIT(2)
#define TEMP_UB_HOT_BIT				BIT(1)
#define TEMP_LB_HOT_BIT				BIT(0)
#define SKIN_TEMP_SHIFT				4

#define MISC_RT_STS_REG				(MISC_BASE + 0x10)
#define HARD_ILIMIT_RT_STS_BIT			BIT(5)

#define BANDGAP_ENABLE_REG			(MISC_BASE + 0x42)
#define BANDGAP_ENABLE_CMD_BIT			BIT(0)

#define BARK_BITE_WDOG_PET_REG			(MISC_BASE + 0x43)
#define BARK_BITE_WDOG_PET_BIT			BIT(0)

#define CLOCK_REQUEST_REG			(MISC_BASE + 0x44)
#define CLOCK_REQUEST_CMD_BIT			BIT(0)

#define WD_CFG_REG				(MISC_BASE + 0x51)
#define WATCHDOG_TRIGGER_AFP_EN_BIT		BIT(7)
#define BARK_WDOG_INT_EN_BIT			BIT(6)
#define BITE_WDOG_INT_EN_BIT			BIT(5)
#define WDOG_IRQ_SFT_BIT			BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT		BIT(1)
#define WDOG_TIMER_EN_BIT			BIT(0)

#define MISC_CUST_SDCDC_CLK_CFG_REG		(MISC_BASE + 0xA0)
#define SWITCHER_CLK_FREQ_MASK			GENMASK(3, 0)

#define MISC_CUST_SDCDC_ILIMIT_CFG_REG		(MISC_BASE + 0xA1)
#define LS_VALLEY_THRESH_PCT_BIT		BIT(3)
#define PCL_LIMIT_MASK				GENMASK(1, 0)

#define SNARL_BARK_BITE_WD_CFG_REG		(MISC_BASE + 0x53)
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT	BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK			GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK			GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK			GENMASK(1, 0)

#define MISC_THERMREG_SRC_CFG_REG		(MISC_BASE + 0x70)
#define BYP_THERM_CHG_CURR_ADJUST_BIT		BIT(2)
#define THERMREG_SKIN_CMP_SRC_EN_BIT		BIT(1)
#define THERMREG_DIE_CMP_SRC_EN_BIT		BIT(0)

#define MISC_CHGR_TRIM_OPTIONS_REG		(MISC_BASE + 0x55)
#define CMD_RBIAS_EN_BIT			BIT(2)

#define MISC_ENG_SDCDC_RESERVE1_REG		(MISC_BASE + 0xC4)
#define MINOFF_TIME_MASK			BIT(6)

#define MISC_ENG_SDCDC_CFG8_REG			(MISC_BASE + 0xC7)
#define DEAD_TIME_MASK				GENMASK(2, 0)
#define DEAD_TIME_32NS				0x4

#define MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG	(MISC_BASE + 0xC8)
#define PROLONG_ISENSE_MASK			GENMASK(7, 6)
#define PROLONG_ISENSEM_SHIFT			6
#define SAMPLE_HOLD_DELAY_MASK			GENMASK(5, 2)
#define SAMPLE_HOLD_DELAY_SHIFT			2
#define DISABLE_ILIMIT_BIT			BIT(0)

#define MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG	(MISC_BASE + 0xC9)
#define INPUT_CURRENT_LIMIT_SOURCE_BIT		BIT(7)
#define TC_ISENSE_AMPLIFIER_MASK		GENMASK(6, 4)
#define TC_ISENSE_AMPLIFIER_SHIFT		4
#define HS_II_CORRECTION_MASK			GENMASK(3, 0)

#define MISC_ENG_SDCDC_RESERVE3_REG		(MISC_BASE + 0xCB)
#define VDDCAP_SHORT_DISABLE_TRISTATE_BIT	BIT(7)
#define PCL_SHUTDOWN_BUCK_BIT			BIT(6)
#define ISENSE_TC_CORRECTION_BIT		BIT(5)
#define II_SOURCE_BIT				BIT(4)
#define SCALE_SLOPE_COMP_MASK			GENMASK(3, 0)

#define USBIN_CURRENT_LIMIT_CFG_REG		(USBIN_BASE + 0x70)
#define USB_TR_SCPATH_ICL_1X_GAIN_REG		(USBIN_BASE + 0xF2)
#define TR_SCPATH_ICL_1X_GAIN_MASK		GENMASK(5, 0)

#define IS_USBIN(mode)				\
	((mode == QTI_POWER_SUPPLY_PL_USBIN_USBIN) \
	 || (mode == QTI_POWER_SUPPLY_PL_USBIN_USBIN_EXT))

#define PARALLEL_ENABLE_VOTER			"PARALLEL_ENABLE_VOTER"

struct smb_chg_param {
	const char	*name;
	u16		reg;
	int		min_u;
	int		max_u;
	int		step_u;
};

struct smb_params {
	struct smb_chg_param	fcc;
	struct smb_chg_param	ov;
	struct smb_chg_param	usb_icl;
};

static struct smb_params v1_params = {
	.fcc		= {
		.name	= "fast charge current",
		.reg	= FAST_CHARGE_CURRENT_CFG_REG,
		.min_u	= 0,
		.max_u	= 6000000,
		.step_u	= 25000,
	},
	.ov		= {
		.name	= "battery over voltage",
		.reg	= CHGR_BATTOV_CFG_REG,
		.min_u	= 2450000,
		.max_u	= 5000000,
		.step_u	= 10000,
	},
	.usb_icl	= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 100000,
		.max_u  = 5000000,
		.step_u = 30000,
	},
};

struct smb_irq_info {
	const char		*name;
	const irq_handler_t	handler;
	const bool		wake;
	int			irq;
};

struct smb_dt_props {
	bool	disable_ctm;
	int	pl_mode;
	int	pl_batfet_mode;
	bool	hw_die_temp_mitigation;
	u32	die_temp_threshold;
};

struct smb1355 {
	struct device		*dev;
	char			*name;
	struct regmap		*regmap;

	int			max_fcc;

	struct smb_dt_props	dt;
	struct smb_params	param;

	struct mutex		write_lock;
	struct mutex		suspend_lock;

	struct power_supply	*parallel_psy;
	struct iio_dev		*indio_dev;
	struct iio_chan_spec	*iio_chan;
	int			d_health;
	int			c_health;
	int			c_charger_temp_max;
	int			die_temp_deciDegC;
	int			suspended_usb_icl;
	int			charge_type;
	int			vbatt_uv;
	int			fcc_ua;
	bool			exit_die_temp;
	struct delayed_work	die_temp_work;
	bool			disabled;
	bool			suspended;
	bool			charging_enabled;
	bool			pin_status;

	struct votable		*irq_disable_votable;
	struct votable		*fcc_votable;
	struct votable		*fv_votable;
};

enum {
	CONNECTOR_TEMP = 0,
	DIE_TEMP,
};

struct smb1355_iio_channel {
	const char	*datasheet_name;
	int		channel_num;
	enum		iio_chan_type type;
	long		info_mask;
};

#define PL_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define PL_CHAN_ENERGY(_name, _num)			\
	PL_IIO_CHAN(_name, _num, IIO_ENERGY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define PL_CHAN_INDEX(_name, _num)			\
	PL_IIO_CHAN(_name, _num, IIO_INDEX,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define PL_CHAN_TEMP(_name, _num)			\
	PL_IIO_CHAN(_name, _num, IIO_TEMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define PL_CHAN_VOLT(_name, _num)			\
	PL_IIO_CHAN(_name, _num, IIO_VOLTAGE,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define PL_CHAN_CURR(_name, _num)			\
	PL_IIO_CHAN(_name, _num, IIO_CURRENT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

static const struct smb1355_iio_channel smb1355_iio_channels[] = {
	PL_CHAN_ENERGY("charge_type", PSY_IIO_CHARGE_TYPE)
	PL_CHAN_INDEX("online", PSY_IIO_ONLINE)
	PL_CHAN_INDEX("charging_enabled", PSY_IIO_CHARGING_ENABLED)
	PL_CHAN_INDEX("pin_enabled", PSY_IIO_PIN_ENABLED)
	PL_CHAN_INDEX("input_suspend", PSY_IIO_INPUT_SUSPEND)
	PL_CHAN_TEMP("charger_temp", PSY_IIO_CHARGER_TEMP)
	PL_CHAN_TEMP("charger_temp_max", PSY_IIO_CHARGER_TEMP_MAX)
	PL_CHAN_VOLT("voltage_max", PSY_IIO_VOLTAGE_MAX)
	PL_CHAN_CURR("constant_charge_current_max",
			PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX)
	PL_CHAN_INDEX("parallel_mode", PSY_IIO_PARALLEL_MODE)
	PL_CHAN_INDEX("connector_health", PSY_IIO_CONNECTOR_HEALTH)
	PL_CHAN_INDEX("parallel_batfet_mode", PSY_IIO_PARALLEL_BATFET_MODE)
	PL_CHAN_CURR("parallel_fcc_max", PSY_IIO_PARALLEL_FCC_MAX)
	PL_CHAN_CURR("input_current_limited", PSY_IIO_INPUT_CURRENT_LIMITED)
	PL_CHAN_CURR("min_icl", PSY_IIO_MIN_ICL)
	PL_CHAN_CURR("current_max", PSY_IIO_CURRENT_MAX)
	PL_CHAN_INDEX("set_ship_mode", PSY_IIO_SET_SHIP_MODE)
	PL_CHAN_INDEX("die_health", PSY_IIO_DIE_HEALTH)
};

static bool is_secure(struct smb1355 *chip, int addr)
{
	if (addr == CLOCK_REQUEST_REG || addr == I2C_SS_DIG_PMIC_SID_REG)
		return true;

	/* assume everything above 0xA0 is secure */
	return (addr & 0xFF) >= 0xA0;
}

static bool is_voter_available(struct smb1355 *chip)
{
	if (!chip->fcc_votable) {
		chip->fcc_votable = find_votable("FCC");
		if (!chip->fcc_votable) {
			pr_debug("Couldn't find FCC votable\n");
			return false;
		}
	}

	if (!chip->fv_votable) {
		chip->fv_votable = find_votable("FV");
		if (!chip->fv_votable) {
			pr_debug("Couldn't find FV votable\n");
			return false;
		}
	}

	return true;
}

static int smb1355_read(struct smb1355 *chip, u16 addr, u8 *val)
{
	unsigned int temp;
	int rc;

	rc = regmap_read(chip->regmap, addr, &temp);
	if (rc >= 0)
		*val = (u8)temp;

	return rc;
}

static int smb1355_masked_force_write(struct smb1355 *chip, u16 addr, u8 mask,
					u8 val)
{
	int rc;

	mutex_lock(&chip->write_lock);
	if (is_secure(chip, addr)) {
		rc = regmap_write(chip->regmap, (addr & 0xFF00) | 0xD0, 0xA5);
		if (rc < 0)
			goto unlock;
	}

	rc = regmap_write_bits(chip->regmap, addr, mask, val);

unlock:
	mutex_unlock(&chip->write_lock);
	return rc;
}

static int smb1355_masked_write(struct smb1355 *chip, u16 addr, u8 mask, u8 val)
{
	int rc;

	mutex_lock(&chip->write_lock);
	if (is_secure(chip, addr)) {
		rc = regmap_write(chip->regmap, (addr & 0xFF00) | 0xD0, 0xA5);
		if (rc < 0)
			goto unlock;
	}

	rc = regmap_update_bits(chip->regmap, addr, mask, val);

unlock:
	mutex_unlock(&chip->write_lock);
	return rc;
}

static int smb1355_write(struct smb1355 *chip, u16 addr, u8 val)
{
	int rc;

	mutex_lock(&chip->write_lock);

	if (is_secure(chip, addr)) {
		rc = regmap_write(chip->regmap, (addr & ~(0xFF)) | 0xD0, 0xA5);
		if (rc < 0)
			goto unlock;
	}

	rc = regmap_write(chip->regmap, addr, val);

unlock:
	mutex_unlock(&chip->write_lock);
	return rc;
}

static int smb1355_set_charge_param(struct smb1355 *chip,
			struct smb_chg_param *param, int val_u)
{
	int rc;
	u8 val_raw;

	if (val_u > param->max_u || val_u < param->min_u) {
		pr_err("%s: %d is out of range [%d, %d]\n",
			param->name, val_u, param->min_u, param->max_u);
		return -EINVAL;
	}

	val_raw = (val_u - param->min_u) / param->step_u;

	rc = smb1355_write(chip, param->reg, val_raw);
	if (rc < 0) {
		pr_err("%s: Couldn't write 0x%02x to 0x%04x rc=%d\n",
			param->name, val_raw, param->reg, rc);
		return rc;
	}

	return rc;
}

static int smb1355_get_charge_param(struct smb1355 *chip,
			struct smb_chg_param *param, int *val_u)
{
	int rc;
	u8 val_raw;

	rc = smb1355_read(chip, param->reg, &val_raw);
	if (rc < 0) {
		pr_err("%s: Couldn't read from 0x%04x rc=%d\n",
			param->name, param->reg, rc);
		return rc;
	}

	*val_u = val_raw * param->step_u + param->min_u;

	return rc;
}

#define UB_COMP_OFFSET_DEGC		34
#define DIE_TEMP_MEAS_PERIOD_MS		10000
static void die_temp_work(struct work_struct *work)
{
	struct smb1355 *chip = container_of(work, struct smb1355,
							die_temp_work.work);
	int rc, i;
	u8 temp_stat;

	for (i = 0; i < BIT(5); i++) {
		rc = smb1355_masked_write(chip, SMB2CHG_BATIF_ENG_SMISC_DIETEMP,
				TDIE_COMPARATOR_THRESHOLD, i);
		if (rc < 0) {
			pr_err("Couldn't set temp comp threshold rc=%d\n", rc);
			continue;
		}

		if (chip->exit_die_temp)
			return;

		/* wait for the comparator output to deglitch */
		msleep(100);

		rc = smb1355_read(chip, TEMP_COMP_STATUS_REG, &temp_stat);
		if (rc < 0) {
			pr_err("Couldn't read temp comp status rc=%d\n", rc);
			continue;
		}

		if (!(temp_stat & TEMP_UB_HOT_BIT)) {
			/* found the temp */
			break;
		}
	}

	chip->die_temp_deciDegC = 10 * (i + UB_COMP_OFFSET_DEGC);

	schedule_delayed_work(&chip->die_temp_work,
			msecs_to_jiffies(DIE_TEMP_MEAS_PERIOD_MS));
}

static int smb1355_get_prop_input_current_limited(struct smb1355 *chip,
							int *val)
{
	int rc;
	u8 stat = 0;

	rc = smb1355_read(chip, MISC_RT_STS_REG, &stat);
	if (rc < 0)
		pr_err("Couldn't read SMB1355_BATTERY_STATUS_3 rc=%d\n", rc);

	*val = !!(stat & HARD_ILIMIT_RT_STS_BIT);

	return 0;
}

static irqreturn_t smb1355_handle_chg_state_change(int irq, void *data)
{
	struct smb1355 *chip = data;

	if (chip->parallel_psy)
		power_supply_changed(chip->parallel_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb1355_handle_wdog_bark(int irq, void *data)
{
	struct smb1355 *chip = data;
	int rc;

	rc = smb1355_write(chip, BARK_BITE_WDOG_PET_REG,
					BARK_BITE_WDOG_PET_BIT);
	if (rc < 0)
		pr_err("Couldn't pet the dog rc=%d\n", rc);

	return IRQ_HANDLED;
}

static irqreturn_t smb1355_handle_temperature_change(int irq, void *data)
{
	struct smb1355 *chip = data;

	if (chip->parallel_psy)
		power_supply_changed(chip->parallel_psy);

	return IRQ_HANDLED;
}

static int smb1355_determine_initial_status(struct smb1355 *chip)
{
	smb1355_handle_temperature_change(0, chip);
	return 0;
}

#define DEFAULT_DIE_TEMP_LOW_THRESHOLD		90
static int smb1355_parse_dt(struct smb1355 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chip->dt.disable_ctm =
		of_property_read_bool(node, "qcom,disable-ctm");

	/*
	 * If parallel-mode property is not present default
	 * parallel configuration is USBMID-USBMID.
	 */
	rc = of_property_read_u32(node,
		"qcom,parallel-mode", &chip->dt.pl_mode);
	if (rc < 0)
		chip->dt.pl_mode = QTI_POWER_SUPPLY_PL_USBMID_USBMID;

	/*
	 * If stacked-batfet property is not present default
	 * configuration is NON-STACKED-BATFET.
	 */
	chip->dt.pl_batfet_mode = QTI_POWER_SUPPLY_PL_NON_STACKED_BATFET;
	if (of_property_read_bool(node, "qcom,stacked-batfet"))
		chip->dt.pl_batfet_mode = QTI_POWER_SUPPLY_PL_STACKED_BATFET;

	chip->dt.hw_die_temp_mitigation = of_property_read_bool(node,
					"qcom,hw-die-temp-mitigation");

	chip->dt.die_temp_threshold = DEFAULT_DIE_TEMP_LOW_THRESHOLD;
	of_property_read_u32(node, "qcom,die-temp-threshold-degc",
				&chip->dt.die_temp_threshold);
	if (chip->dt.die_temp_threshold > DIE_LOW_RANGE_MAX_DEGC)
		chip->dt.die_temp_threshold = DIE_LOW_RANGE_MAX_DEGC;

	return 0;
}

static int smb1355_get_prop_batt_charge_type(struct smb1355 *chip,
					int *val)
{
	int rc;
	u8 stat;

	rc = smb1355_read(chip, BATTERY_STATUS_3_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read SMB1355_BATTERY_STATUS_3 rc=%d\n", rc);
		return rc;
	}

	if (stat & ENABLE_CHARGING_BIT) {
		if (stat & BATT_GT_PRE_TO_FAST_BIT)
			*val = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else
			*val = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	} else {
		*val = POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	return rc;
}

static int smb1355_get_prop_health(struct smb1355 *chip, int type)
{
	u8 temp;
	int rc, shift;

	/* Connector-temp uses skin-temp configuration */
	shift = (type == CONNECTOR_TEMP) ? SKIN_TEMP_SHIFT : 0;

	rc = smb1355_read(chip, TEMP_COMP_STATUS_REG, &temp);
	if (rc < 0) {
		pr_err("Couldn't read comp stat reg rc = %d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (temp & (TEMP_RST_HOT_BIT << shift))
		return POWER_SUPPLY_HEALTH_OVERHEAT;

	if (temp & (TEMP_UB_HOT_BIT << shift))
		return POWER_SUPPLY_HEALTH_HOT;

	if (temp & (TEMP_LB_HOT_BIT << shift))
		return POWER_SUPPLY_HEALTH_WARM;

	return POWER_SUPPLY_HEALTH_COOL;
}

static int smb1355_get_prop_voltage_max(struct smb1355 *chip, int *val)
{
	int rc = 0;

	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = chip->vbatt_uv;
		goto done;
	}
	rc = smb1355_get_charge_param(chip, &chip->param.ov, val);
	if (rc < 0)
		pr_err("failed to read vbatt rc=%d\n", rc);
	else
		chip->vbatt_uv = *val;
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

static int smb1355_get_prop_constant_charge_current_max(struct smb1355 *chip,
							int *val)
{
	int rc = 0;

	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = chip->fcc_ua;
		goto done;
	}
	rc = smb1355_get_charge_param(chip, &chip->param.fcc, val);
	if (rc < 0)
		pr_err("failed to read fcc rc=%d\n", rc);
	else
		chip->fcc_ua = *val;
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

static int smb1355_get_prop_health_value(struct smb1355 *chip,
						int *val, int type)
{
	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = (type == DIE_TEMP) ? chip->d_health :
						chip->c_health;
	} else {
		*val = smb1355_get_prop_health(chip, (type == DIE_TEMP) ?
						DIE_TEMP : CONNECTOR_TEMP);
		if (type == DIE_TEMP)
			chip->d_health = *val;
		else
			chip->c_health = *val;
	}

	mutex_unlock(&chip->suspend_lock);

	return 0;
}

static int smb1355_get_prop_online(struct smb1355 *chip, int *val)
{
	int rc = 0;
	u8 stat;

	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = chip->charging_enabled;
		goto done;
	}
	rc = smb1355_read(chip, BATTERY_STATUS_3_REG, &stat);
	if (rc < 0) {
		pr_err("failed to read BATTERY_STATUS_3_REG %d\n", rc);
	} else {
		*val = (bool)(stat & ENABLE_CHARGING_BIT);
		chip->charging_enabled = *val;
	}
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

static int smb1355_get_prop_pin_enabled(struct smb1355 *chip, int *val)
{
	int rc = 0;
	u8 stat;

	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = chip->pin_status;
		goto done;
	}
	rc = smb1355_read(chip, BATTERY_STATUS_2_REG, &stat);
	if (rc < 0) {
		pr_err("failed to read BATTERY_STATUS_2_REG %d\n", rc);
	} else {
		*val = !(stat & DISABLE_CHARGING_BIT);
		chip->pin_status = *val;
	}
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

static int smb1355_get_prop_charge_type(struct smb1355 *chip, int *val)
{
	int rc = 0;

	/*
	 * In case of system suspend we should not allow
	 * register reads and writes to the device as it
	 * leads to i2c transaction failures.
	 */
	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		*val = chip->charge_type;
		goto done;
	}
	rc = smb1355_get_prop_batt_charge_type(chip, val);
	if (rc < 0)
		pr_err("failed to read batt_charge_type %d\n", rc);
	else
		chip->charge_type = *val;
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

#define MIN_PARALLEL_ICL_UA		250000
#define SUSPEND_CURRENT_UA		2000
static int smb1355_set_parallel_charging(struct smb1355 *chip, bool disable)
{
	int rc;

	if (chip->disabled == disable)
		return 0;

	if (IS_USBIN(chip->dt.pl_mode)) {
		/*
		 * Initialize ICL configuration to minimum value while
		 * depending upon the set icl configuration method to properly
		 * configure the ICL value. At the same time, cache the value
		 * of ICL to be reported as 2mA.
		 */
		chip->suspended_usb_icl = SUSPEND_CURRENT_UA;
		smb1355_set_charge_param(chip,
				&chip->param.usb_icl, MIN_PARALLEL_ICL_UA);
	}

	rc = smb1355_masked_write(chip, WD_CFG_REG, WDOG_TIMER_EN_BIT,
				 disable ? 0 : WDOG_TIMER_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't %s watchdog rc=%d\n",
		       disable ? "disable" : "enable", rc);
		disable = true;
	}

	/*
	 * Configure charge enable for high polarity and
	 * When disabling charging set it to cmd register control(cmd bit=0)
	 * When enabling charging set it to pin control
	 */
	rc = smb1355_masked_write(chip, CHGR_CFG2_REG,
			CHG_EN_POLARITY_BIT | CHG_EN_SRC_BIT,
			disable ? 0 : CHG_EN_SRC_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure charge enable source rc=%d\n", rc);
		disable = true;
	}

	chip->die_temp_deciDegC = -EINVAL;
	/* Only enable temperature measurement for s/w based mitigation */
	if (!chip->dt.hw_die_temp_mitigation) {
		if (disable) {
			chip->exit_die_temp = true;
			cancel_delayed_work_sync(&chip->die_temp_work);
		} else {
			/* start the work to measure temperature */
			chip->exit_die_temp = false;
			schedule_delayed_work(&chip->die_temp_work, 0);
		}
	}

	if (chip->irq_disable_votable)
		vote(chip->irq_disable_votable, PARALLEL_ENABLE_VOTER,
				disable, 0);

	rc = smb1355_masked_write(chip, BANDGAP_ENABLE_REG,
				BANDGAP_ENABLE_CMD_BIT,
				disable ? 0 : BANDGAP_ENABLE_CMD_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure bandgap enable rc=%d\n", rc);
		return rc;
	}

	chip->disabled = disable;

	return 0;
}

static int smb1355_set_current_max(struct smb1355 *chip, int curr)
{
	int rc = 0;

	if (!IS_USBIN(chip->dt.pl_mode))
		return 0;

	if ((curr / 1000) < 100) {
		/* disable parallel path (ICL < 100mA) */
		rc = smb1355_set_parallel_charging(chip, true);
	} else {
		rc = smb1355_set_parallel_charging(chip, false);
		if (rc < 0)
			return rc;

		rc = smb1355_set_charge_param(chip,
				&chip->param.usb_icl, curr);
		chip->suspended_usb_icl = 0;
	}

	return rc;
}

static int smb1355_clk_request(struct smb1355 *chip, bool enable)
{
	int rc;

	rc = smb1355_masked_force_write(chip, CLOCK_REQUEST_REG,
				CLOCK_REQUEST_CMD_BIT,
				enable ? CLOCK_REQUEST_CMD_BIT : 0);
	if (rc < 0)
		pr_err("Couldn't %s clock rc=%d\n",
			       enable ? "enable" : "disable", rc);

	return rc;
}

static int smb1355_parallel_get_prop(struct power_supply *psy,
			       enum power_supply_property prop,
			       union power_supply_propval *pval)
{
	struct smb1355 *chip = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = chip->name;
		break;
	default:
		pr_err_ratelimited("parallel psy get prop %d not supported\n",
			prop);
		return -EINVAL;
	}

	return 0;
}

/*****************************
 * PARALLEL PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb1355_parallel_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static struct power_supply_desc parallel_psy_desc = {
	.name			= "parallel",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.properties		= smb1355_parallel_props,
	.num_properties		= ARRAY_SIZE(smb1355_parallel_props),
	.get_property		= smb1355_parallel_get_prop,
};

static int smb1355_init_parallel_psy(struct smb1355 *chip)
{
	struct power_supply_config parallel_cfg = {};

	parallel_cfg.drv_data = chip;
	parallel_cfg.of_node = chip->dev->of_node;

	/* change to smb1355's property list */
	parallel_psy_desc.properties = smb1355_parallel_props;
	parallel_psy_desc.num_properties = ARRAY_SIZE(smb1355_parallel_props);
	chip->parallel_psy = devm_power_supply_register(chip->dev,
						   &parallel_psy_desc,
						   &parallel_cfg);
	if (IS_ERR(chip->parallel_psy)) {
		pr_err("Couldn't register parallel power supply\n");
		return PTR_ERR(chip->parallel_psy);
	}

	return 0;
}

static int smb1355_iio_read_raw(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val1, int *val2, long mask)
{
	struct smb1355 *chip = iio_priv(indio_dev);
	int rc = 0;

	*val1 = 0;

	switch (chan->channel) {
	case PSY_IIO_CHARGE_TYPE:
		rc = smb1355_get_prop_charge_type(chip, val1);
		break;
	case PSY_IIO_CHARGING_ENABLED:
	case PSY_IIO_ONLINE:
		rc = smb1355_get_prop_online(chip, val1);
		break;
	case PSY_IIO_PIN_ENABLED:
		rc = smb1355_get_prop_pin_enabled(chip, val1);
		break;
	case PSY_IIO_CHARGER_TEMP:
		*val1 = chip->die_temp_deciDegC;
		break;
	case PSY_IIO_CHARGER_TEMP_MAX:
		/*
		 * In case of h/w controlled die_temp mitigation,
		 * die_temp/die_temp_max can not be reported as this
		 * requires run time manipulation of DIE_TEMP low
		 * threshold which will interfere with h/w mitigation
		 * scheme.
		 */
		if (chip->dt.hw_die_temp_mitigation)
			*val1 = -EINVAL;
		else
			*val1 = chip->c_charger_temp_max;
		break;
	case PSY_IIO_INPUT_SUSPEND:
		*val1 = chip->disabled;
		break;
	case PSY_IIO_VOLTAGE_MAX:
		rc = smb1355_get_prop_voltage_max(chip, val1);
		break;
	case PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smb1355_get_prop_constant_charge_current_max(chip, val1);
		break;
	case PSY_IIO_PARALLEL_MODE:
		*val1 = chip->dt.pl_mode;
		break;
	case PSY_IIO_CONNECTOR_HEALTH:
		if (chip->c_health == -EINVAL)
			rc = smb1355_get_prop_health_value(chip, val1,
							CONNECTOR_TEMP);
		else
			*val1 = chip->c_health;
		break;
	case PSY_IIO_DIE_HEALTH:
		rc = smb1355_get_prop_health_value(chip, val1, DIE_TEMP);
		break;
	case PSY_IIO_PARALLEL_BATFET_MODE:
		*val1 = chip->dt.pl_batfet_mode;
		break;
	case PSY_IIO_INPUT_CURRENT_LIMITED:
		if (IS_USBIN(chip->dt.pl_mode))
			rc = smb1355_get_prop_input_current_limited(
						chip, val1);
		else
			*val1 = 0;
		break;
	case PSY_IIO_CURRENT_MAX:
		if (IS_USBIN(chip->dt.pl_mode)) {
			/* Report cached ICL until its configured correctly */
			if (chip->suspended_usb_icl)
				*val1 = chip->suspended_usb_icl;
			else
				rc = smb1355_get_charge_param(chip,
					&chip->param.usb_icl, val1);
		} else {
			*val1 = 0;
		}
		break;
	case PSY_IIO_MIN_ICL:
		*val1 = MIN_PARALLEL_ICL_UA;
		break;
	case PSY_IIO_PARALLEL_FCC_MAX:
		*val1 = chip->max_fcc;
		break;
	case PSY_IIO_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		*val1 = 0;
		break;
	default:
		pr_err_ratelimited("SMB1355 IIO channel %x not supported\n",
			chan->channel);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't read channel %x rc = %d\n",
				chan->channel, rc);
		return -ENODATA;
	}

	return IIO_VAL_INT;
}

static int smb1355_iio_write_raw(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int val1, int val2, long mask)
{
	struct smb1355 *chip = iio_priv(indio_dev);
	int rc = 0;

	mutex_lock(&chip->suspend_lock);
	if (chip->suspended) {
		pr_debug("SMB1355 IIO write channel %d\n",
				chan->channel);
		goto done;
	}
	switch (chan->channel) {
	case PSY_IIO_INPUT_SUSPEND:
		rc = smb1355_set_parallel_charging(chip, (bool)val1);
		break;
	case PSY_IIO_CURRENT_MAX:
		rc = smb1355_set_current_max(chip, val1);
		break;
	case PSY_IIO_VOLTAGE_MAX:
		rc = smb1355_set_charge_param(chip, &chip->param.ov,
						val1);
		break;
	case PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smb1355_set_charge_param(chip, &chip->param.fcc,
						val1);
		break;
	case PSY_IIO_CONNECTOR_HEALTH:
		chip->c_health = val1;
		power_supply_changed(chip->parallel_psy);
		break;
	case PSY_IIO_CHARGER_TEMP_MAX:
		chip->c_charger_temp_max = val1;
		break;
	case PSY_IIO_SET_SHIP_MODE:
		if (!val1)
			break;
		rc = smb1355_clk_request(chip, false);
		break;
	default:
		rc = -EINVAL;
	}

	if (rc < 0)
		pr_debug("Couldn't write to channel %x rc = %d\n",
				chan->channel, rc);
done:
	mutex_unlock(&chip->suspend_lock);
	return rc;
}

static int smb1355_iio_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	struct smb1355 *chip = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = chip->iio_chan;
	int i;

	for (i = 0; i < ARRAY_SIZE(smb1355_iio_channels);
						i++, iio_chan++)
		if (iio_chan->channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static const struct iio_info smb1355_iio_info = {
	.read_raw	= smb1355_iio_read_raw,
	.write_raw	= smb1355_iio_write_raw,
	.fwnode_xlate	= smb1355_iio_fwnode_xlate,
};

static int smb1355_init_iio_psy(struct smb1355 *chip)
{
	struct iio_dev *indio_dev = chip->indio_dev;
	struct iio_chan_spec *chan;
	int smb1355_num_iio_channels = ARRAY_SIZE(smb1355_iio_channels);
	int rc, i;

	chip->iio_chan = devm_kcalloc(chip->dev,
				smb1355_num_iio_channels,
				sizeof(*chip->iio_chan),
				GFP_KERNEL);
	if (!chip->iio_chan)
		return -ENOMEM;

	indio_dev->info = &smb1355_iio_info;
	indio_dev->dev.parent = chip->dev;
	indio_dev->dev.of_node = chip->dev->of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip->iio_chan;
	indio_dev->num_channels = smb1355_num_iio_channels;
	indio_dev->name = "smb1355-charger";

	for (i = 0; i < smb1355_num_iio_channels; i++) {
		chan = &chip->iio_chan[i];
		chan->address = i;
		chan->channel = smb1355_iio_channels[i].channel_num;
		chan->type = smb1355_iio_channels[i].type;
		chan->datasheet_name =
			smb1355_iio_channels[i].datasheet_name;
		chan->extend_name =
			smb1355_iio_channels[i].datasheet_name;
		chan->info_mask_separate =
			smb1355_iio_channels[i].info_mask;
	}

	rc = devm_iio_device_register(chip->dev, indio_dev);
	if (rc) {
		pr_err("Failed to register SMB1355 Parallel IIO device, rc=%d\n",
			rc);
		return rc;
	}

	return rc;

}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/

#define MFG_ID_SMB1354			0x01
#define MFG_ID_SMB1355			0xFF
#define SMB1354_MAX_PARALLEL_FCC_UA	2500000
#define SMB1355_MAX_PARALLEL_FCC_UA	6000000
static int smb1355_detect_version(struct smb1355 *chip)
{
	int rc;
	u8 val;

	rc = smb1355_read(chip, REVID_MFG_ID_SPARE_REG, &val);
	if (rc < 0) {
		pr_err("Unable to read REVID rc=%d\n", rc);
		return rc;
	}

	switch (val) {
	case MFG_ID_SMB1354:
		chip->name = "smb1354";
		chip->max_fcc = SMB1354_MAX_PARALLEL_FCC_UA;
		break;
	case MFG_ID_SMB1355:
		chip->name = "smb1355";
		chip->max_fcc = SMB1355_MAX_PARALLEL_FCC_UA;
		break;
	default:
		pr_err("Invalid value of REVID val=%d\n", val);
		return -EINVAL;
	}

	return rc;
}

static int smb1355_tskin_sensor_config(struct smb1355 *chip)
{
	int rc;

	if (chip->dt.disable_ctm) {
		/*
		 * the TSKIN sensor with external resistor needs a bias,
		 * disable it here.
		 */
		rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
					 EXT_BIAS_PIN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't enable ext bias pin path rc=%d\n",
				rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, BATIF_CFG_SMISC_BATID_REG,
					CFG_SMISC_RBIAS_EXT_CTRL_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set  BATIF_CFG_SMISC_BATID rc=%d\n",
				rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, MISC_CHGR_TRIM_OPTIONS_REG,
					CMD_RBIAS_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CHGR_TRIM_OPTIONS rc=%d\n",
				rc);
			return rc;
		}

		/* disable skin temperature comparator source */
		rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
			THERMREG_SKIN_CMP_SRC_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set Skin temp comparator src rc=%d\n",
				rc);
			return rc;
		}
	} else {
		/*
		 * the TSKIN sensor with external resistor needs a bias,
		 * enable it here.
		 */
		rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
					 EXT_BIAS_PIN_BIT, EXT_BIAS_PIN_BIT);
		if (rc < 0) {
			pr_err("Couldn't enable ext bias pin path rc=%d\n",
				rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, BATIF_CFG_SMISC_BATID_REG,
					CFG_SMISC_RBIAS_EXT_CTRL_BIT,
					CFG_SMISC_RBIAS_EXT_CTRL_BIT);
		if (rc < 0) {
			pr_err("Couldn't set  BATIF_CFG_SMISC_BATID rc=%d\n",
				rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, MISC_CHGR_TRIM_OPTIONS_REG,
					CMD_RBIAS_EN_BIT,
					CMD_RBIAS_EN_BIT);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CHGR_TRIM_OPTIONS rc=%d\n",
				rc);
			return rc;
		}

		/* Enable skin temperature comparator source */
		rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
			THERMREG_SKIN_CMP_SRC_EN_BIT,
			THERMREG_SKIN_CMP_SRC_EN_BIT);
		if (rc < 0) {
			pr_err("Couldn't set Skin temp comparator src rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

static int smb1355_init_hw(struct smb1355 *chip)
{
	int rc;
	u8 val, range;

	/* request clock always on */
	rc = smb1355_clk_request(chip, true);
	if (rc < 0)
		return rc;

	/* Change to let SMB1355 only respond to address 0x0C  */
	rc = smb1355_masked_write(chip, I2C_SS_DIG_PMIC_SID_REG,
					PMIC_SID_MASK, PMIC_SID0_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure the I2C_SS_DIG_PMIC_SID_REG rc=%d\n",
					rc);
		return rc;
	}

	/* disable charging when watchdog bites & set bite-timeout to 8secs */
	val = BITE_WDOG_DISABLE_CHARGING_CFG_BIT | 0x3;
	rc = smb1355_masked_write(chip, SNARL_BARK_BITE_WD_CFG_REG,
				BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
				BITE_WDOG_TIMEOUT_MASK, val);
	if (rc < 0) {
		pr_err("Couldn't configure the watchdog bite rc=%d\n", rc);
		return rc;
	}

	/* enable watchdog bark and bite interrupts, and disable the watchdog */
	rc = smb1355_masked_write(chip, WD_CFG_REG, WDOG_TIMER_EN_BIT
			| WDOG_TIMER_EN_ON_PLUGIN_BIT | BITE_WDOG_INT_EN_BIT
			| BARK_WDOG_INT_EN_BIT,
			BITE_WDOG_INT_EN_BIT | BARK_WDOG_INT_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure the watchdog rc=%d\n", rc);
		return rc;
	}

	/*
	 * Disable command based SMB1355 enablement and disable parallel
	 * charging path by switching to command based mode.
	 */
	rc = smb1355_masked_write(chip, CHGR_CHARGING_ENABLE_CMD_REG,
				CHARGING_ENABLE_CMD_BIT, 0);
	if (rc < 0) {
		pr_err("Coudln't configure command bit, rc=%d\n", rc);
		return rc;
	}

	rc = smb1355_set_parallel_charging(chip, true);
	if (rc < 0) {
		pr_err("Couldn't disable parallel path rc=%d\n", rc);
		return rc;
	}

	/* initialize FCC to 0 */
	rc = smb1355_set_charge_param(chip, &chip->param.fcc, 0);
	if (rc < 0) {
		pr_err("Couldn't set 0 FCC rc=%d\n", rc);
		return rc;
	}

	/* HICCUP setting, unlimited retry with 250ms interval */
	rc = smb1355_masked_write(chip, POWER_MODE_HICCUP_CFG,
			HICCUP_TIMEOUT_CFG_MASK | MAX_HICCUP_DUETO_BATDIS_MASK,
			0);
	if (rc < 0) {
		pr_err("Couldn't set HICCUP interval rc=%d\n",
			rc);
		return rc;
	}

	/* enable parallel current sensing */
	rc = smb1355_masked_write(chip, CFG_REG,
				 VCHG_EN_CFG_BIT, VCHG_EN_CFG_BIT);
	if (rc < 0) {
		pr_err("Couldn't enable parallel current sensing rc=%d\n",
			rc);
		return rc;
	}

	/* set Pre-to-Fast Charging Threshold 2.6V */
	rc = smb1355_masked_write(chip, CHGR_PRE_TO_FAST_THRESHOLD_CFG_REG,
				 PRE_TO_FAST_CHARGE_THRESHOLD_MASK, 0);
	if (rc < 0) {
		pr_err("Couldn't set PRE_TO_FAST_CHARGE_THRESHOLD rc=%d\n",
			rc);
		return rc;
	}

	/* Extend min-offtime same as blanking time */
	rc = smb1355_masked_write(chip, MISC_ENG_SDCDC_RESERVE1_REG,
						MINOFF_TIME_MASK, 0);
	if (rc < 0) {
		pr_err("Couldn't set MINOFF_TIME rc=%d\n", rc);
		return rc;
	}

	/* Set dead-time to 32ns */
	rc = smb1355_masked_write(chip, MISC_ENG_SDCDC_CFG8_REG,
					DEAD_TIME_MASK, DEAD_TIME_32NS);
	if (rc < 0) {
		pr_err("Couldn't set DEAD_TIME to 32ns rc=%d\n", rc);
		return rc;
	}

	/* Configure DIE temp Low threshold */
	if (chip->dt.hw_die_temp_mitigation) {
		range = (chip->dt.die_temp_threshold - DIE_LOW_RANGE_BASE_DEGC)
						/ (DIE_LOW_RANGE_DELTA);
		val = (chip->dt.die_temp_threshold
				- ((range * DIE_LOW_RANGE_DELTA)
						+ DIE_LOW_RANGE_BASE_DEGC))
				% DIE_LOW_RANGE_DELTA;

		rc = smb1355_masked_write(chip, SMB2CHG_BATIF_ENG_SMISC_DIETEMP,
				TDIE_COMPARATOR_THRESHOLD,
				(range << DIE_LOW_RANGE_SHIFT) | val);
		if (rc < 0) {
			pr_err("Couldn't set temp comp threshold rc=%d\n", rc);
			return rc;
		}
	}

	/*
	 * Enable thermal Die temperature comparator source and
	 * enable hardware controlled current adjustment for die temp
	 * if charger is configured in h/w controlled die temp mitigation.
	 */
	val = THERMREG_DIE_CMP_SRC_EN_BIT;
	if (!chip->dt.hw_die_temp_mitigation)
		val |= BYP_THERM_CHG_CURR_ADJUST_BIT;
	rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
		THERMREG_DIE_CMP_SRC_EN_BIT | BYP_THERM_CHG_CURR_ADJUST_BIT,
		val);
	if (rc < 0) {
		pr_err("Couldn't set Skin temperature comparator src rc=%d\n",
			rc);
		return rc;
	}

	/*
	 * Disable hysterisis for die temperature. This is so that sw can run
	 * stepping scheme quickly
	 */
	val = chip->dt.hw_die_temp_mitigation ? DIE_TEMP_COMP_HYST_BIT : 0;
	rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
				DIE_TEMP_COMP_HYST_BIT, val);
	if (rc < 0) {
		pr_err("Couldn't disable hyst. for die rc=%d\n", rc);
		return rc;
	}

	/* Enable valley current comparator all the time */
	rc = smb1355_masked_write(chip, ANA1_ENG_SREFGEN_CFG2_REG,
		VALLEY_COMPARATOR_EN_BIT, VALLEY_COMPARATOR_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't enable valley current comparator rc=%d\n", rc);
		return rc;
	}

	/* Set LS_VALLEY threshold to 85% */
	rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_ILIMIT_CFG_REG,
		LS_VALLEY_THRESH_PCT_BIT, LS_VALLEY_THRESH_PCT_BIT);
	if (rc < 0) {
		pr_err("Couldn't set LS valley threshold to 85pc rc=%d\n", rc);
		return rc;
	}

	/* For SMB1354, set PCL to 8.6 A */
	if (!strcmp(chip->name, "smb1354")) {
		rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_ILIMIT_CFG_REG,
				PCL_LIMIT_MASK, PCL_LIMIT_MASK);
		if (rc < 0) {
			pr_err("Couldn't set PCL limit to 8.6A rc=%d\n", rc);
			return rc;
		}
	}

	rc = smb1355_tskin_sensor_config(chip);
	if (rc < 0) {
		pr_err("Couldn't configure tskin regs rc=%d\n", rc);
		return rc;
	}

	/* USBIN-USBIN configuration */
	if (IS_USBIN(chip->dt.pl_mode)) {
		/* set swicther clock frequency to 700kHz */
		rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_CLK_CFG_REG,
				SWITCHER_CLK_FREQ_MASK, 0x03);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CUST_SDCDC_CLK_CFG rc=%d\n",
				rc);
			return rc;
		}

		/*
		 * configure compensation for input current limit (ICL) loop
		 * accuracy, scale slope compensation using 30k resistor.
		 */
		rc = smb1355_masked_write(chip, MISC_ENG_SDCDC_RESERVE3_REG,
				II_SOURCE_BIT | SCALE_SLOPE_COMP_MASK,
				II_SOURCE_BIT);
		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_RESERVE3_REG rc=%d\n",
				rc);
			return rc;
		}

		/* configuration to improve ICL accuracy */
		rc = smb1355_masked_write(chip,
				MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG,
				PROLONG_ISENSE_MASK | SAMPLE_HOLD_DELAY_MASK,
				((uint8_t)0x0C << SAMPLE_HOLD_DELAY_SHIFT));
		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG rc=%d\n",
				rc);
			return rc;
		}

		rc = smb1355_masked_write(chip,
				MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG,
				INPUT_CURRENT_LIMIT_SOURCE_BIT
				| HS_II_CORRECTION_MASK,
			       INPUT_CURRENT_LIMIT_SOURCE_BIT | 0xC);

		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG rc=%d\n",
				rc);
			return rc;
		}

		/* configure DAC offset */
		rc = smb1355_masked_write(chip,
				ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG,
				TR_SBQ_ICL_1X_REF_OFFSET, 0x00);
		if (rc < 0) {
			pr_err("Couldn't set ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG rc=%d\n",
				rc);
			return rc;
		}

		/* configure DAC gain */
		rc = smb1355_masked_write(chip, USB_TR_SCPATH_ICL_1X_GAIN_REG,
				TR_SCPATH_ICL_1X_GAIN_MASK, 0x22);
		if (rc < 0) {
			pr_err("Couldn't set USB_TR_SCPATH_ICL_1X_GAIN_REG rc=%d\n",
				rc);
			return rc;
		}
	}

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/
static struct smb_irq_info smb1355_irqs[] = {
	[0] = {
		.name		= "wdog-bark",
		.handler	= smb1355_handle_wdog_bark,
		.wake		= true,
	},
	[1] = {
		.name		= "chg-state-change",
		.handler	= smb1355_handle_chg_state_change,
		.wake		= true,
	},
	[2] = {
		.name		= "temperature-change",
		.handler	= smb1355_handle_temperature_change,
	},
};

static int smb1355_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb1355_irqs); i++) {
		if (strcmp(smb1355_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb1355_request_interrupt(struct smb1355 *chip,
				struct device_node *node,
				const char *irq_name)
{
	int rc = 0, irq, irq_index;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb1355_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb1355_irqs[irq_index].handler)
		return 0;

	rc = devm_request_threaded_irq(chip->dev, irq, NULL,
				smb1355_irqs[irq_index].handler,
				IRQF_ONESHOT, irq_name, chip);
	if (rc < 0) {
		pr_err("Couldn't request irq %d rc=%d\n", irq, rc);
		return rc;
	}

	smb1355_irqs[irq_index].irq = irq;
	if (smb1355_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb1355_request_interrupts(struct smb1355 *chip)
{
	struct device_node *node = chip->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					prop, name) {
			rc = smb1355_request_interrupt(chip, child, name);
			if (rc < 0) {
				pr_err("Couldn't request interrupt %s rc=%d\n",
					name, rc);
				return rc;
			}
		}
	}

	return rc;
}
static int smb1355_irq_disable_callback(struct votable *votable, void *data,
			int disable, const char *client)

{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb1355_irqs); i++) {
		if (smb1355_irqs[i].irq) {
			if (disable)
				disable_irq(smb1355_irqs[i].irq);
			else
				enable_irq(smb1355_irqs[i].irq);
		}
	}

	return 0;
}

/*********
 * PROBE *
 *********/
static const struct of_device_id match_table[] = {
	{
		.compatible	= "qcom,smb1355",
	},
	{ },
};

static int smb1355_probe(struct platform_device *pdev)
{
	struct smb1355 *chip;
	const struct of_device_id *id;
	struct iio_dev *indio_dev;
	int rc = 0;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->indio_dev = indio_dev;

	chip->dev = &pdev->dev;
	chip->param = v1_params;
	chip->c_health = -EINVAL;
	chip->d_health = -EINVAL;
	chip->c_charger_temp_max = -EINVAL;
	mutex_init(&chip->write_lock);
	mutex_init(&chip->suspend_lock);
	INIT_DELAYED_WORK(&chip->die_temp_work, die_temp_work);
	chip->disabled = false;
	chip->die_temp_deciDegC = -EINVAL;

	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	id = of_match_device(of_match_ptr(match_table), chip->dev);
	if (!id) {
		pr_err("Couldn't find a matching device\n");
		return -ENODEV;
	}

	rc = smb1355_detect_version(chip);
	if (rc < 0) {
		pr_err("Couldn't detect SMB1355/1354 chip type rc=%d\n", rc);
		goto cleanup;
	}

	platform_set_drvdata(pdev, chip);

	rc = smb1355_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb1355_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb1355_init_parallel_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize parallel psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb1355_init_iio_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize parallel IIO device rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb1355_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb1355_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

	chip->irq_disable_votable = create_votable("SMB1355_IRQ_DISABLE",
			VOTE_SET_ANY, smb1355_irq_disable_callback, chip);
	if (IS_ERR(chip->irq_disable_votable)) {
		rc = PTR_ERR(chip->irq_disable_votable);
		goto cleanup;
	}
	/* keep IRQ's disabled until parallel is enabled */
	vote(chip->irq_disable_votable, PARALLEL_ENABLE_VOTER, true, 0);

	pr_info("%s probed successfully pl_mode=%s batfet_mode=%s\n",
		chip->name,
		IS_USBIN(chip->dt.pl_mode) ? "USBIN-USBIN" : "USBMID-USBMID",
		(chip->dt.pl_batfet_mode == QTI_POWER_SUPPLY_PL_STACKED_BATFET)
			? "STACKED_BATFET" : "NON-STACKED_BATFET");
	return rc;

cleanup:
	platform_set_drvdata(pdev, NULL);
	return rc;
}

static int smb1355_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void smb1355_shutdown(struct platform_device *pdev)
{
	struct smb1355 *chip = platform_get_drvdata(pdev);
	int rc;

	/* disable parallel charging path */
	rc = smb1355_set_parallel_charging(chip, true);
	if (rc < 0)
		pr_err("Couldn't disable parallel path rc=%d\n", rc);

	smb1355_clk_request(chip, false);
}

#ifdef CONFIG_PM_SLEEP
static int smb1355_suspend(struct device *dev)
{
	struct smb1355 *chip = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&chip->die_temp_work);

	mutex_lock(&chip->suspend_lock);
	chip->suspended = true;
	mutex_unlock(&chip->suspend_lock);

	return 0;
}

static int smb1355_resume(struct device *dev)
{
	struct smb1355 *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->suspend_lock);
	chip->suspended = false;
	mutex_unlock(&chip->suspend_lock);

	/*
	 * During suspend i2c failures are fixed by reporting cached
	 * chip state, to report correct values we need to invoke
	 * callbacks for the fcc and fv votables. To avoid excessive
	 * invokes to callbacks invoke only when smb1355 is enabled.
	 */
	if (is_voter_available(chip) && chip->charging_enabled) {
		rerun_election(chip->fcc_votable);
		rerun_election(chip->fv_votable);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(smb1355_pm_ops, smb1355_suspend, smb1355_resume);

static struct platform_driver smb1355_driver = {
	.driver	= {
		.name		= "qcom,smb1355-charger",
		.pm		= &smb1355_pm_ops,
		.of_match_table	= match_table,
	},
	.probe		= smb1355_probe,
	.remove		= smb1355_remove,
	.shutdown	= smb1355_shutdown,
};
module_platform_driver(smb1355_driver);

MODULE_DESCRIPTION("QPNP SMB1355 Charger Driver");
MODULE_LICENSE("GPL");
