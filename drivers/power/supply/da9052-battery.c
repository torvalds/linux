// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Batttery Driver for Dialog DA9052 PMICs
 *
 * Copyright(c) 2011 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 */

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/pdata.h>
#include <linux/mfd/da9052/reg.h>

/* STATIC CONFIGURATION */
#define DA9052_BAT_CUTOFF_VOLT		2800
#define DA9052_BAT_TSH			62000
#define DA9052_BAT_LOW_CAP		4
#define DA9052_AVG_SZ			4
#define DA9052_VC_TBL_SZ		68
#define DA9052_VC_TBL_REF_SZ		3

#define DA9052_ISET_USB_MASK		0x0F
#define DA9052_CHG_USB_ILIM_MASK	0x40
#define DA9052_CHG_LIM_COLS		16

#define DA9052_MEAN(x, y)		((x + y) / 2)

enum charger_type_enum {
	DA9052_NOCHARGER = 1,
	DA9052_CHARGER,
};

static const u16 da9052_chg_current_lim[2][DA9052_CHG_LIM_COLS] = {
	{70,  80,  90,  100, 110, 120, 400,  450,
	 500, 550, 600, 650, 700, 900, 1100, 1300},
	{80,  90,  100, 110,  120,  400,  450,  500,
	 550, 600, 800, 1000, 1200, 1400, 1600, 1800},
};

static const u16 vc_tbl_ref[3] = {10, 25, 40};
/* Lookup table for voltage vs capacity */
static u32 const vc_tbl[3][68][2] = {
	/* For temperature 10 degree Celsius */
	{
	{4082, 100}, {4036, 98},
	{4020, 96}, {4008, 95},
	{3997, 93}, {3983, 91},
	{3964, 90}, {3943, 88},
	{3926, 87}, {3912, 85},
	{3900, 84}, {3890, 82},
	{3881, 80}, {3873, 79},
	{3865, 77}, {3857, 76},
	{3848, 74}, {3839, 73},
	{3829, 71}, {3820, 70},
	{3811, 68}, {3802, 67},
	{3794, 65}, {3785, 64},
	{3778, 62}, {3770, 61},
	{3763, 59}, {3756, 58},
	{3750, 56}, {3744, 55},
	{3738, 53}, {3732, 52},
	{3727, 50}, {3722, 49},
	{3717, 47}, {3712, 46},
	{3708, 44}, {3703, 43},
	{3700, 41}, {3696, 40},
	{3693, 38}, {3691, 37},
	{3688, 35}, {3686, 34},
	{3683, 32}, {3681, 31},
	{3678, 29}, {3675, 28},
	{3672, 26}, {3669, 25},
	{3665, 23}, {3661, 22},
	{3656, 21}, {3651, 19},
	{3645, 18}, {3639, 16},
	{3631, 15}, {3622, 13},
	{3611, 12}, {3600, 10},
	{3587, 9}, {3572, 7},
	{3548, 6}, {3503, 5},
	{3420, 3}, {3268, 2},
	{2992, 1}, {2746, 0}
	},
	/* For temperature 25 degree Celsius */
	{
	{4102, 100}, {4065, 98},
	{4048, 96}, {4034, 95},
	{4021, 93}, {4011, 92},
	{4001, 90}, {3986, 88},
	{3968, 87}, {3952, 85},
	{3938, 84}, {3926, 82},
	{3916, 81}, {3908, 79},
	{3900, 77}, {3892, 76},
	{3883, 74}, {3874, 73},
	{3864, 71}, {3855, 70},
	{3846, 68}, {3836, 67},
	{3827, 65}, {3819, 64},
	{3810, 62}, {3801, 61},
	{3793, 59}, {3786, 58},
	{3778, 56}, {3772, 55},
	{3765, 53}, {3759, 52},
	{3754, 50}, {3748, 49},
	{3743, 47}, {3738, 46},
	{3733, 44}, {3728, 43},
	{3724, 41}, {3720, 40},
	{3716, 38}, {3712, 37},
	{3709, 35}, {3706, 34},
	{3703, 33}, {3701, 31},
	{3698, 30}, {3696, 28},
	{3693, 27}, {3690, 25},
	{3687, 24}, {3683, 22},
	{3680, 21}, {3675, 19},
	{3671, 18}, {3666, 17},
	{3660, 15}, {3654, 14},
	{3647, 12}, {3639, 11},
	{3630, 9}, {3621, 8},
	{3613, 6}, {3606, 5},
	{3597, 4}, {3582, 2},
	{3546, 1}, {2747, 0}
	},
	/* For temperature 40 degree Celsius */
	{
	{4114, 100}, {4081, 98},
	{4065, 96}, {4050, 95},
	{4036, 93}, {4024, 92},
	{4013, 90}, {4002, 88},
	{3990, 87}, {3976, 85},
	{3962, 84}, {3950, 82},
	{3939, 81}, {3930, 79},
	{3921, 77}, {3912, 76},
	{3902, 74}, {3893, 73},
	{3883, 71}, {3874, 70},
	{3865, 68}, {3856, 67},
	{3847, 65}, {3838, 64},
	{3829, 62}, {3820, 61},
	{3812, 59}, {3803, 58},
	{3795, 56}, {3787, 55},
	{3780, 53}, {3773, 52},
	{3767, 50}, {3761, 49},
	{3756, 47}, {3751, 46},
	{3746, 44}, {3741, 43},
	{3736, 41}, {3732, 40},
	{3728, 38}, {3724, 37},
	{3720, 35}, {3716, 34},
	{3713, 33}, {3710, 31},
	{3707, 30}, {3704, 28},
	{3701, 27}, {3698, 25},
	{3695, 24}, {3691, 22},
	{3686, 21}, {3681, 19},
	{3676, 18}, {3671, 17},
	{3666, 15}, {3661, 14},
	{3655, 12}, {3648, 11},
	{3640, 9}, {3632, 8},
	{3622, 6}, {3616, 5},
	{3611, 4}, {3604, 2},
	{3594, 1}, {2747, 0}
	}
};

struct da9052_battery {
	struct da9052 *da9052;
	struct power_supply *psy;
	struct notifier_block nb;
	int charger_type;
	int status;
	int health;
};

static inline int volt_reg_to_mV(int value)
{
	return ((value * 1000) / 512) + 2500;
}

static inline int ichg_reg_to_mA(int value)
{
	return (value * 3900) / 1000;
}

static int da9052_read_chgend_current(struct da9052_battery *bat,
				       int *current_mA)
{
	int ret;

	if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING)
		return -EINVAL;

	ret = da9052_reg_read(bat->da9052, DA9052_ICHG_END_REG);
	if (ret < 0)
		return ret;

	*current_mA = ichg_reg_to_mA(ret & DA9052_ICHGEND_ICHGEND);

	return 0;
}

static int da9052_read_chg_current(struct da9052_battery *bat, int *current_mA)
{
	int ret;

	if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING)
		return -EINVAL;

	ret = da9052_reg_read(bat->da9052, DA9052_ICHG_AV_REG);
	if (ret < 0)
		return ret;

	*current_mA = ichg_reg_to_mA(ret & DA9052_ICHGAV_ICHGAV);

	return 0;
}

static int da9052_bat_check_status(struct da9052_battery *bat, int *status)
{
	u8 v[2] = {0, 0};
	u8 bat_status;
	u8 chg_end;
	int ret;
	int chg_current;
	int chg_end_current;
	bool dcinsel;
	bool dcindet;
	bool vbussel;
	bool vbusdet;
	bool dc;
	bool vbus;

	ret = da9052_group_read(bat->da9052, DA9052_STATUS_A_REG, 2, v);
	if (ret < 0)
		return ret;

	bat_status = v[0];
	chg_end = v[1];

	dcinsel = bat_status & DA9052_STATUSA_DCINSEL;
	dcindet = bat_status & DA9052_STATUSA_DCINDET;
	vbussel = bat_status & DA9052_STATUSA_VBUSSEL;
	vbusdet = bat_status & DA9052_STATUSA_VBUSDET;
	dc = dcinsel && dcindet;
	vbus = vbussel && vbusdet;

	/* Preference to WALL(DCIN) charger unit */
	if (dc || vbus) {
		bat->charger_type = DA9052_CHARGER;

		/* If charging end flag is set and Charging current is greater
		 * than charging end limit then battery is charging
		*/
		if ((chg_end & DA9052_STATUSB_CHGEND) != 0) {
			ret = da9052_read_chg_current(bat, &chg_current);
			if (ret < 0)
				return ret;
			ret = da9052_read_chgend_current(bat, &chg_end_current);
			if (ret < 0)
				return ret;

			if (chg_current >= chg_end_current)
				bat->status = POWER_SUPPLY_STATUS_CHARGING;
			else
				bat->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else {
			/* If Charging end flag is cleared then battery is
			 * charging
			*/
			bat->status = POWER_SUPPLY_STATUS_CHARGING;
		}
	} else if (dcindet || vbusdet) {
			bat->charger_type = DA9052_CHARGER;
			bat->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		bat->charger_type = DA9052_NOCHARGER;
		bat->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (status != NULL)
		*status = bat->status;
	return 0;
}

static int da9052_bat_read_volt(struct da9052_battery *bat, int *volt_mV)
{
	int volt;

	volt = da9052_adc_manual_read(bat->da9052, DA9052_ADC_MAN_MUXSEL_VBAT);
	if (volt < 0)
		return volt;

	*volt_mV = volt_reg_to_mV(volt);

	return 0;
}

static int da9052_bat_check_presence(struct da9052_battery *bat, int *illegal)
{
	int bat_temp;

	bat_temp = da9052_adc_read_temp(bat->da9052);
	if (bat_temp < 0)
		return bat_temp;

	if (bat_temp > DA9052_BAT_TSH)
		*illegal = 1;
	else
		*illegal = 0;

	return 0;
}

static int da9052_bat_interpolate(int vbat_lower, int  vbat_upper,
				   int level_lower, int level_upper,
				   int bat_voltage)
{
	int tmp;

	tmp = ((level_upper - level_lower) * 1000) / (vbat_upper - vbat_lower);
	tmp = level_lower + (((bat_voltage - vbat_lower) * tmp) / 1000);

	return tmp;
}

static unsigned char da9052_determine_vc_tbl_index(unsigned char adc_temp)
{
	int i;

	if (adc_temp <= vc_tbl_ref[0])
		return 0;

	if (adc_temp > vc_tbl_ref[DA9052_VC_TBL_REF_SZ - 1])
		return DA9052_VC_TBL_REF_SZ - 1;

	for (i = 0; i < DA9052_VC_TBL_REF_SZ - 1; i++) {
		if ((adc_temp > vc_tbl_ref[i]) &&
		    (adc_temp <= DA9052_MEAN(vc_tbl_ref[i], vc_tbl_ref[i + 1])))
				return i;
		if ((adc_temp > DA9052_MEAN(vc_tbl_ref[i], vc_tbl_ref[i + 1]))
		     && (adc_temp <= vc_tbl_ref[i]))
				return i + 1;
	}
	/*
	 * For some reason authors of the driver didn't presume that we can
	 * end up here. It might be OK, but might be not, no one knows for
	 * sure. Go check your battery, is it on fire?
	 */
	WARN_ON(1);
	return 0;
}

static int da9052_bat_read_capacity(struct da9052_battery *bat, int *capacity)
{
	int adc_temp;
	int bat_voltage;
	int vbat_lower;
	int vbat_upper;
	int level_upper;
	int level_lower;
	int ret;
	int flag;
	int i = 0;
	int j;

	ret = da9052_bat_read_volt(bat, &bat_voltage);
	if (ret < 0)
		return ret;

	adc_temp = da9052_adc_read_temp(bat->da9052);
	if (adc_temp < 0)
		return adc_temp;

	i = da9052_determine_vc_tbl_index(adc_temp);

	if (bat_voltage >= vc_tbl[i][0][0]) {
		*capacity = 100;
		return 0;
	}
	if (bat_voltage <= vc_tbl[i][DA9052_VC_TBL_SZ - 1][0]) {
		*capacity = 0;
		return 0;
	}
	flag = 0;

	for (j = 0; j < (DA9052_VC_TBL_SZ-1); j++) {
		if ((bat_voltage <= vc_tbl[i][j][0]) &&
		    (bat_voltage >= vc_tbl[i][j + 1][0])) {
			vbat_upper = vc_tbl[i][j][0];
			vbat_lower = vc_tbl[i][j + 1][0];
			level_upper = vc_tbl[i][j][1];
			level_lower = vc_tbl[i][j + 1][1];
			flag = 1;
			break;
		}
	}
	if (!flag)
		return -EIO;

	*capacity = da9052_bat_interpolate(vbat_lower, vbat_upper, level_lower,
					   level_upper, bat_voltage);

	return 0;
}

static int da9052_bat_check_health(struct da9052_battery *bat, int *health)
{
	int ret;
	int bat_illegal;
	int capacity;

	ret = da9052_bat_check_presence(bat, &bat_illegal);
	if (ret < 0)
		return ret;

	if (bat_illegal) {
		bat->health = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;
	}

	if (bat->health != POWER_SUPPLY_HEALTH_OVERHEAT) {
		ret = da9052_bat_read_capacity(bat, &capacity);
		if (ret < 0)
			return ret;
		if (capacity < DA9052_BAT_LOW_CAP)
			bat->health = POWER_SUPPLY_HEALTH_DEAD;
		else
			bat->health = POWER_SUPPLY_HEALTH_GOOD;
	}

	*health = bat->health;

	return 0;
}

static irqreturn_t da9052_bat_irq(int irq, void *data)
{
	struct da9052_battery *bat = data;
	int virq;

	virq = regmap_irq_get_virq(bat->da9052->irq_data, irq);
	irq -= virq;

	if (irq == DA9052_IRQ_CHGEND)
		bat->status = POWER_SUPPLY_STATUS_FULL;
	else
		da9052_bat_check_status(bat, NULL);

	if (irq == DA9052_IRQ_CHGEND || irq == DA9052_IRQ_DCIN ||
	    irq == DA9052_IRQ_VBUS || irq == DA9052_IRQ_TBAT) {
		power_supply_changed(bat->psy);
	}

	return IRQ_HANDLED;
}

static int da9052_USB_current_notifier(struct notifier_block *nb,
					unsigned long events, void *data)
{
	u8 row;
	u8 col;
	int *current_mA = data;
	int ret;
	struct da9052_battery *bat = container_of(nb, struct da9052_battery,
						  nb);

	if (bat->status == POWER_SUPPLY_STATUS_DISCHARGING)
		return -EPERM;

	ret = da9052_reg_read(bat->da9052, DA9052_CHGBUCK_REG);
	if (ret & DA9052_CHG_USB_ILIM_MASK)
		return -EPERM;

	if (bat->da9052->chip_id == DA9052)
		row = 0;
	else
		row = 1;

	if (*current_mA < da9052_chg_current_lim[row][0] ||
	    *current_mA > da9052_chg_current_lim[row][DA9052_CHG_LIM_COLS - 1])
		return -EINVAL;

	for (col = 0; col <= DA9052_CHG_LIM_COLS - 1 ; col++) {
		if (*current_mA <= da9052_chg_current_lim[row][col])
			break;
	}

	return da9052_reg_update(bat->da9052, DA9052_ISET_REG,
				 DA9052_ISET_USB_MASK, col);
}

static int da9052_bat_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	int ret;
	int illegal;
	struct da9052_battery *bat = power_supply_get_drvdata(psy);

	ret = da9052_bat_check_presence(bat, &illegal);
	if (ret < 0)
		return ret;

	if (illegal && psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = da9052_bat_check_status(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval =
			(bat->charger_type == DA9052_NOCHARGER) ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = da9052_bat_check_presence(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = da9052_bat_check_health(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = DA9052_BAT_CUTOFF_VOLT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = da9052_bat_read_volt(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = da9052_read_chg_current(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = da9052_bat_read_capacity(bat, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = da9052_adc_read_temp(bat->da9052);
		ret = val->intval;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static enum power_supply_property da9052_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static struct power_supply_desc psy_desc = {
	.name		= "da9052-bat",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= da9052_bat_props,
	.num_properties	= ARRAY_SIZE(da9052_bat_props),
	.get_property	= da9052_bat_get_property,
};

static char *da9052_bat_irqs[] = {
	"BATT TEMP",
	"DCIN DET",
	"DCIN REM",
	"VBUS DET",
	"VBUS REM",
	"CHG END",
};

static int da9052_bat_irq_bits[] = {
	DA9052_IRQ_TBAT,
	DA9052_IRQ_DCIN,
	DA9052_IRQ_DCINREM,
	DA9052_IRQ_VBUS,
	DA9052_IRQ_VBUSREM,
	DA9052_IRQ_CHGEND,
};

static s32 da9052_bat_probe(struct platform_device *pdev)
{
	struct da9052_pdata *pdata;
	struct da9052_battery *bat;
	struct power_supply_config psy_cfg = {};
	int ret;
	int i;

	bat = devm_kzalloc(&pdev->dev, sizeof(struct da9052_battery),
				GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	psy_cfg.drv_data = bat;

	bat->da9052 = dev_get_drvdata(pdev->dev.parent);
	bat->charger_type = DA9052_NOCHARGER;
	bat->status = POWER_SUPPLY_STATUS_UNKNOWN;
	bat->health = POWER_SUPPLY_HEALTH_UNKNOWN;
	bat->nb.notifier_call = da9052_USB_current_notifier;

	pdata = bat->da9052->dev->platform_data;
	if (pdata != NULL && pdata->use_for_apm)
		psy_desc.use_for_apm = pdata->use_for_apm;
	else
		psy_desc.use_for_apm = 1;

	for (i = 0; i < ARRAY_SIZE(da9052_bat_irqs); i++) {
		ret = da9052_request_irq(bat->da9052,
				da9052_bat_irq_bits[i], da9052_bat_irqs[i],
				da9052_bat_irq, bat);

		if (ret != 0) {
			dev_err(bat->da9052->dev,
				"DA9052 failed to request %s IRQ: %d\n",
				da9052_bat_irqs[i], ret);
			goto err;
		}
	}

	bat->psy = power_supply_register(&pdev->dev, &psy_desc, &psy_cfg);
	if (IS_ERR(bat->psy)) {
		ret = PTR_ERR(bat->psy);
		goto err;
	}

	platform_set_drvdata(pdev, bat);
	return 0;

err:
	while (--i >= 0)
		da9052_free_irq(bat->da9052, da9052_bat_irq_bits[i], bat);

	return ret;
}
static void da9052_bat_remove(struct platform_device *pdev)
{
	int i;
	struct da9052_battery *bat = platform_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(da9052_bat_irqs); i++)
		da9052_free_irq(bat->da9052, da9052_bat_irq_bits[i], bat);

	power_supply_unregister(bat->psy);
}

static struct platform_driver da9052_bat_driver = {
	.probe = da9052_bat_probe,
	.remove_new = da9052_bat_remove,
	.driver = {
		.name = "da9052-bat",
	},
};
module_platform_driver(da9052_bat_driver);

MODULE_DESCRIPTION("DA9052 BAT Device Driver");
MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-bat");
