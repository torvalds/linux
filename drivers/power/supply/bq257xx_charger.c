// SPDX-License-Identifier: GPL-2.0
/*
 * BQ257XX Battery Charger Driver
 * Copyright (C) 2025 Chris Morgan <macromorgan@hotmail.com>
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/bq257xx.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>

/* Forward declaration of driver data. */
struct bq257xx_chg;

/**
 * struct bq257xx_chip_info - chip specific routines
 * @bq257xx_hw_init: init function for hw
 * @bq257xx_hw_shutdown: shutdown function for hw
 * @bq257xx_get_state: get and update state of hardware
 * @bq257xx_set_ichg: set maximum charge current (in uA)
 * @bq257xx_set_vbatreg: set maximum charge voltage (in uV)
 * @bq257xx_set_iindpm: set maximum input current (in uA)
 */
struct bq257xx_chip_info {
	int (*bq257xx_hw_init)(struct bq257xx_chg *pdata);
	void (*bq257xx_hw_shutdown)(struct bq257xx_chg *pdata);
	int (*bq257xx_get_state)(struct bq257xx_chg *pdata);
	int (*bq257xx_set_ichg)(struct bq257xx_chg *pdata, int ichg);
	int (*bq257xx_set_vbatreg)(struct bq257xx_chg *pdata, int vbatreg);
	int (*bq257xx_set_iindpm)(struct bq257xx_chg *pdata, int iindpm);
};

/**
 * struct bq257xx_chg - driver data for charger
 * @chip: hw specific functions
 * @bq: parent MFD device
 * @charger: power supply device
 * @online: charger input is present
 * @fast_charge: charger is in fast charge mode
 * @pre_charge: charger is in pre-charge mode
 * @ov_fault: charger reports over voltage fault
 * @batoc_fault: charger reports battery over current fault
 * @oc_fault: charger reports over current fault
 * @usb_type: USB type reported from parent power supply
 * @supplied: Status of parent power supply
 * @iindpm_max: maximum input current limit (uA)
 * @vbat_max: maximum charge voltage (uV)
 * @ichg_max: maximum charge current (uA)
 * @vsys_min: minimum system voltage (uV)
 */
struct bq257xx_chg {
	const struct bq257xx_chip_info *chip;
	struct bq257xx_device *bq;
	struct power_supply *charger;
	bool online;
	bool fast_charge;
	bool pre_charge;
	bool ov_fault;
	bool batoc_fault;
	bool oc_fault;
	int usb_type;
	int supplied;
	u32 iindpm_max;
	u32 vbat_max;
	u32 ichg_max;
	u32 vsys_min;
};

/**
 * bq25703_get_state() - Get the current state of the device
 * @pdata: driver platform data
 *
 * Get the current state of the charger. Check if the charger is
 * powered, what kind of charge state (if any) the device is in,
 * and if there are any active faults.
 *
 * Return: Returns 0 on success, or error on failure to read device.
 */
static int bq25703_get_state(struct bq257xx_chg *pdata)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_CHARGER_STATUS, &reg);
	if (ret)
		return ret;

	pdata->online = reg & BQ25703_STS_AC_STAT;
	pdata->fast_charge = reg & BQ25703_STS_IN_FCHRG;
	pdata->pre_charge = reg & BQ25703_STS_IN_PCHRG;
	pdata->ov_fault = reg & BQ25703_STS_FAULT_ACOV;
	pdata->batoc_fault = reg & BQ25703_STS_FAULT_BATOC;
	pdata->oc_fault = reg & BQ25703_STS_FAULT_ACOC;

	return 0;
}

/**
 * bq25703_get_min_vsys() - Get the minimum system voltage
 * @pdata: driver platform data
 * @intval: value for minimum voltage
 *
 * Return: Returns 0 on success or error on failure to read.
 */
static int bq25703_get_min_vsys(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_MIN_VSYS,
			  &reg);
	if (ret)
		return ret;

	reg = FIELD_GET(BQ25703_MINVSYS_MASK, reg);
	*intval = (reg * BQ25703_MINVSYS_STEP_UV) + BQ25703_MINVSYS_MIN_UV;

	return ret;
}

/**
 * bq25703_set_min_vsys() - Set the minimum system voltage
 * @pdata: driver platform data
 * @vsys: voltage value to set in uV.
 *
 * This function takes a requested minimum system voltage value, clamps
 * it between the minimum supported value by the charger and a user
 * defined minimum system value, and then writes the value to the
 * appropriate register.
 *
 * Return: Returns 0 on success or error if an error occurs.
 */
static int bq25703_set_min_vsys(struct bq257xx_chg *pdata, int vsys)
{
	unsigned int reg;
	int vsys_min = pdata->vsys_min;

	vsys = clamp(vsys, BQ25703_MINVSYS_MIN_UV, vsys_min);
	reg = ((vsys - BQ25703_MINVSYS_MIN_UV) / BQ25703_MINVSYS_STEP_UV);
	reg = FIELD_PREP(BQ25703_MINVSYS_MASK, reg);

	return regmap_write(pdata->bq->regmap, BQ25703_MIN_VSYS,
			    reg);
}

/**
 * bq25703_get_cur() - Get the reported current from the battery
 * @pdata: driver platform data
 * @intval: value of reported battery current
 *
 * Read the reported current from the battery. Since value is always
 * positive set sign to negative if discharging.
 *
 * Return: Returns 0 on success or error if unable to read value.
 */
static int bq25703_get_cur(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_ADCIBAT_CHG, &reg);
	if (ret < 0)
		return ret;

	if (pdata->online)
		*intval = FIELD_GET(BQ25703_ADCIBAT_CHG_MASK, reg) *
			  BQ25703_ADCIBAT_CHG_STEP_UA;
	else
		*intval = -(FIELD_GET(BQ25703_ADCIBAT_DISCHG_MASK, reg) *
			    BQ25703_ADCIBAT_DIS_STEP_UA);

	return ret;
}

/**
 * bq25703_get_ichg_cur() - Get the maximum reported charge current
 * @pdata: driver platform data
 * @intval: value of maximum reported charge current
 *
 * Get the maximum reported charge current from the battery.
 *
 * Return: Returns 0 on success or error if unable to read value.
 */
static int bq25703_get_ichg_cur(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_CHARGE_CURRENT, &reg);
	if (ret)
		return ret;

	*intval = FIELD_GET(BQ25703_ICHG_MASK, reg) * BQ25703_ICHG_STEP_UA;

	return ret;
}

/**
 * bq25703_set_ichg_cur() - Set the maximum charge current
 * @pdata: driver platform data
 * @ichg: current value to set in uA.
 *
 * This function takes a requested maximum charge current value, clamps
 * it between the minimum supported value by the charger and a user
 * defined maximum charging value, and then writes the value to the
 * appropriate register.
 *
 * Return: Returns 0 on success or error if an error occurs.
 */
static int bq25703_set_ichg_cur(struct bq257xx_chg *pdata, int ichg)
{
	unsigned int reg;
	int ichg_max = pdata->ichg_max;

	ichg = clamp(ichg, BQ25703_ICHG_MIN_UA, ichg_max);
	reg = FIELD_PREP(BQ25703_ICHG_MASK, (ichg / BQ25703_ICHG_STEP_UA));

	return regmap_write(pdata->bq->regmap, BQ25703_CHARGE_CURRENT,
			    reg);
}

/**
 * bq25703_get_chrg_volt() - Get the maximum set charge voltage
 * @pdata: driver platform data
 * @intval: maximum charge voltage value
 *
 * Return: Returns 0 on success or error if unable to read value.
 */
static int bq25703_get_chrg_volt(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_MAX_CHARGE_VOLT,
			  &reg);
	if (ret)
		return ret;

	*intval = FIELD_GET(BQ25703_MAX_CHARGE_VOLT_MASK, reg) *
		  BQ25703_VBATREG_STEP_UV;

	return ret;
}

/**
 * bq25703_set_chrg_volt() - Set the maximum charge voltage
 * @pdata: driver platform data
 * @vbat: voltage value to set in uV.
 *
 * This function takes a requested maximum charge voltage value, clamps
 * it between the minimum supported value by the charger and a user
 * defined maximum charging value, and then writes the value to the
 * appropriate register.
 *
 * Return: Returns 0 on success or error if an error occurs.
 */
static int bq25703_set_chrg_volt(struct bq257xx_chg *pdata, int vbat)
{
	unsigned int reg;
	int vbat_max = pdata->vbat_max;

	vbat = clamp(vbat, BQ25703_VBATREG_MIN_UV, vbat_max);

	reg = FIELD_PREP(BQ25703_MAX_CHARGE_VOLT_MASK,
			 (vbat / BQ25703_VBATREG_STEP_UV));

	return regmap_write(pdata->bq->regmap, BQ25703_MAX_CHARGE_VOLT,
			    reg);
}

/**
 * bq25703_get_iindpm() - Get the maximum set input current
 * @pdata: driver platform data
 * @intval: maximum input current value
 *
 * Read the actual input current limit from the device into intval.
 * This can differ from the value programmed due to some autonomous
 * functions that may be enabled (but are not currently). This is why
 * there is a different register used.
 *
 * Return: Returns 0 on success or error if unable to read register
 * value.
 */
static int bq25703_get_iindpm(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_IIN_DPM, &reg);
	if (ret)
		return ret;

	reg = FIELD_GET(BQ25703_IINDPM_MASK, reg);
	*intval = (reg * BQ25703_IINDPM_STEP_UA) + BQ25703_IINDPM_OFFSET_UA;

	return ret;
}

/**
 * bq25703_set_iindpm() - Set the maximum input current
 * @pdata: driver platform data
 * @iindpm: current value in uA.
 *
 * This function takes a requested maximum input current value, clamps
 * it between the minimum supported value by the charger and a user
 * defined maximum input value, and then writes the value to the
 * appropriate register.
 *
 * Return: Returns 0 on success or error if an error occurs.
 */
static int bq25703_set_iindpm(struct bq257xx_chg *pdata, int iindpm)
{
	unsigned int reg;
	int iindpm_max = pdata->iindpm_max;

	iindpm = clamp(iindpm, BQ25703_IINDPM_MIN_UA, iindpm_max);

	reg = ((iindpm - BQ25703_IINDPM_OFFSET_UA) / BQ25703_IINDPM_STEP_UA);

	return regmap_write(pdata->bq->regmap, BQ25703_IIN_HOST,
			    FIELD_PREP(BQ25703_IINDPM_MASK, reg));
}

/**
 * bq25703_get_vbat() - Get the reported voltage from the battery
 * @pdata: driver platform data
 * @intval: value of reported battery voltage
 *
 * Read value of battery voltage into intval.
 *
 * Return: Returns 0 on success or error if unable to read value.
 */
static int bq25703_get_vbat(struct bq257xx_chg *pdata, int *intval)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(pdata->bq->regmap, BQ25703_ADCVSYSVBAT, &reg);
	if (ret)
		return ret;

	reg = FIELD_GET(BQ25703_ADCVBAT_MASK, reg);
	*intval = (reg * BQ25703_ADCVSYSVBAT_STEP) + BQ25703_ADCVSYSVBAT_OFFSET_UV;

	return ret;
}

/**
 * bq25703_hw_init() - Set all the required registers to init the charger
 * @pdata: driver platform data
 *
 * Initialize the BQ25703 by first disabling the watchdog timer (which
 * shuts off the charger in the absence of periodic writes). Then, set
 * the charge current, charge voltage, minimum system voltage, and
 * input current limit. Disable low power mode to allow ADCs and
 * interrupts. Enable the ADC, start the ADC, set the ADC scale to
 * full, and enable each individual ADC channel.
 *
 * Return: Returns 0 on success or error code on error.
 */
static int bq25703_hw_init(struct bq257xx_chg *pdata)
{
	struct regmap *regmap = pdata->bq->regmap;
	int ret = 0;

	regmap_update_bits(regmap, BQ25703_CHARGE_OPTION_0,
			   BQ25703_WDTMR_ADJ_MASK,
			   FIELD_PREP(BQ25703_WDTMR_ADJ_MASK,
			   BQ25703_WDTMR_DISABLE));

	ret = pdata->chip->bq257xx_set_ichg(pdata, pdata->ichg_max);
	if (ret)
		return ret;

	ret = pdata->chip->bq257xx_set_vbatreg(pdata, pdata->vbat_max);
	if (ret)
		return ret;

	ret = bq25703_set_min_vsys(pdata, pdata->vsys_min);
	if (ret)
		return ret;

	ret = pdata->chip->bq257xx_set_iindpm(pdata, pdata->iindpm_max);
	if (ret)
		return ret;

	/* Disable low power mode by writing 0 to the register. */
	regmap_update_bits(regmap, BQ25703_CHARGE_OPTION_0,
			   BQ25703_EN_LWPWR, 0);

	/* Enable the ADC. */
	regmap_update_bits(regmap, BQ25703_ADC_OPTION,
			   BQ25703_ADC_CONV_EN, BQ25703_ADC_CONV_EN);

	/* Start the ADC. */
	regmap_update_bits(regmap, BQ25703_ADC_OPTION,
			   BQ25703_ADC_START, BQ25703_ADC_START);

	/* Set the scale of the ADC. */
	regmap_update_bits(regmap, BQ25703_ADC_OPTION,
			   BQ25703_ADC_FULL_SCALE, BQ25703_ADC_FULL_SCALE);

	/* Enable each of the ADC channels available. */
	regmap_update_bits(regmap, BQ25703_ADC_OPTION,
			   BQ25703_ADC_CH_MASK,
			   (BQ25703_ADC_CMPIN_EN | BQ25703_ADC_VBUS_EN |
			   BQ25703_ADC_PSYS_EN | BQ25703_ADC_IIN_EN |
			   BQ25703_ADC_IDCHG_EN | BQ25703_ADC_ICHG_EN |
			   BQ25703_ADC_VSYS_EN | BQ25703_ADC_VBAT_EN));

	return ret;
}

/**
 * bq25703_hw_shutdown() - Set registers for shutdown
 * @pdata: driver platform data
 *
 * Enable low power mode for the device while in shutdown.
 */
static void bq25703_hw_shutdown(struct bq257xx_chg *pdata)
{
	regmap_update_bits(pdata->bq->regmap, BQ25703_CHARGE_OPTION_0,
			   BQ25703_EN_LWPWR, BQ25703_EN_LWPWR);
}

static int bq257xx_set_charger_property(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	struct bq257xx_chg *pdata = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return pdata->chip->bq257xx_set_iindpm(pdata, val->intval);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return pdata->chip->bq257xx_set_vbatreg(pdata, val->intval);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return pdata->chip->bq257xx_set_ichg(pdata, val->intval);

	default:
		break;
	}

	return -EINVAL;
}

static int bq257xx_get_charger_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct bq257xx_chg *pdata = power_supply_get_drvdata(psy);
	int ret = 0;

	ret = pdata->chip->bq257xx_get_state(pdata);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!pdata->online)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (pdata->fast_charge || pdata->pre_charge)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (pdata->ov_fault || pdata->batoc_fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (pdata->oc_fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERCURRENT;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Texas Instruments";
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = pdata->online;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return bq25703_get_iindpm(pdata, &val->intval);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return bq25703_get_chrg_volt(pdata, &val->intval);

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return bq25703_get_cur(pdata, &val->intval);

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return bq25703_get_vbat(pdata, &val->intval);

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return bq25703_get_ichg_cur(pdata, &val->intval);

	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return bq25703_get_min_vsys(pdata, &val->intval);

	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = pdata->usb_type;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static enum power_supply_property bq257xx_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int bq257xx_property_is_writeable(struct power_supply *psy,
					 enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return true;
	default:
		return false;
	}
}

/**
 * bq257xx_external_power_changed() - Handler for external power change
 * @psy: Power supply data
 *
 * When the external power into the charger is changed, check the USB
 * type so that it can be reported. Additionally, update the max input
 * current and max charging current to the value reported if it is a
 * USB PD charger, otherwise use the default value. Note that each time
 * a charger is removed the max charge current register is erased, so
 * it must be set again each time the input changes or the device will
 * not charge.
 */
static void bq257xx_external_power_changed(struct power_supply *psy)
{
	struct bq257xx_chg *pdata = power_supply_get_drvdata(psy);
	union power_supply_propval val;
	int ret;
	int imax = pdata->iindpm_max;

	pdata->chip->bq257xx_get_state(pdata);

	pdata->supplied = power_supply_am_i_supplied(pdata->charger);
	if (pdata->supplied < 0)
		return;

	if (pdata->supplied == 0)
		goto out;

	ret = power_supply_get_property_from_supplier(psy,
						      POWER_SUPPLY_PROP_USB_TYPE,
						      &val);
	if (ret)
		return;

	pdata->usb_type = val.intval;

	if ((pdata->usb_type == POWER_SUPPLY_USB_TYPE_PD) ||
	    (pdata->usb_type == POWER_SUPPLY_USB_TYPE_PD_DRP) ||
	    (pdata->usb_type == POWER_SUPPLY_USB_TYPE_PD_PPS)) {
		ret = power_supply_get_property_from_supplier(psy,
							      POWER_SUPPLY_PROP_CURRENT_MAX,
							      &val);
		if (ret)
			return;

		if (val.intval)
			imax = val.intval;
	}

	if (pdata->supplied) {
		pdata->chip->bq257xx_set_ichg(pdata, pdata->ichg_max);
		pdata->chip->bq257xx_set_iindpm(pdata, imax);
		pdata->chip->bq257xx_set_vbatreg(pdata, pdata->vbat_max);
	}

out:
	power_supply_changed(psy);
}

static irqreturn_t bq257xx_irq_handler_thread(int irq, void *private)
{
	struct bq257xx_chg *pdata = private;

	bq257xx_external_power_changed(pdata->charger);
	return IRQ_HANDLED;
}

static const struct power_supply_desc bq257xx_power_supply_desc = {
	.name = "bq257xx-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_C) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_DRP) |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_PPS) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = bq257xx_power_supply_props,
	.num_properties = ARRAY_SIZE(bq257xx_power_supply_props),
	.get_property = bq257xx_get_charger_property,
	.set_property = bq257xx_set_charger_property,
	.property_is_writeable = bq257xx_property_is_writeable,
	.external_power_changed = bq257xx_external_power_changed,
};

static const struct bq257xx_chip_info bq25703_chip_info = {
		.bq257xx_hw_init = &bq25703_hw_init,
		.bq257xx_hw_shutdown = &bq25703_hw_shutdown,
		.bq257xx_get_state = &bq25703_get_state,
		.bq257xx_set_ichg = &bq25703_set_ichg_cur,
		.bq257xx_set_vbatreg = &bq25703_set_chrg_volt,
		.bq257xx_set_iindpm = &bq25703_set_iindpm,
};

/**
 * bq257xx_parse_dt() - Parse the device tree for required properties
 * @pdata: driver platform data
 * @psy_cfg: power supply config data
 * @dev: device struct
 *
 * Read the device tree to identify the minimum system voltage, the
 * maximum charge current, the maximum charge voltage, and the maximum
 * input current.
 *
 * Return: Returns 0 on success or error code on error.
 */
static int bq257xx_parse_dt(struct bq257xx_chg *pdata,
		struct power_supply_config *psy_cfg, struct device *dev)
{
	struct power_supply_battery_info *bat_info;
	int ret;

	ret = power_supply_get_battery_info(pdata->charger,
					    &bat_info);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Unable to get battery info\n");

	if ((bat_info->voltage_min_design_uv <= 0) ||
	    (bat_info->constant_charge_voltage_max_uv <= 0) ||
	    (bat_info->constant_charge_current_max_ua <= 0))
		return dev_err_probe(dev, -EINVAL,
				     "Required bat info missing or invalid\n");

	pdata->vsys_min = bat_info->voltage_min_design_uv;
	pdata->vbat_max = bat_info->constant_charge_voltage_max_uv;
	pdata->ichg_max = bat_info->constant_charge_current_max_ua;

	power_supply_put_battery_info(pdata->charger, bat_info);

	ret = device_property_read_u32(dev,
				       "input-current-limit-microamp",
				       &pdata->iindpm_max);
	if (ret)
		pdata->iindpm_max = BQ25703_IINDPM_DEFAULT_UA;

	return 0;
}

static int bq257xx_charger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bq257xx_device *bq = dev_get_drvdata(pdev->dev.parent);
	struct bq257xx_chg *pdata;
	struct power_supply_config psy_cfg = { };
	int ret;

	device_set_of_node_from_dev(dev, pdev->dev.parent);

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->bq = bq;
	pdata->chip = &bq25703_chip_info;

	platform_set_drvdata(pdev, pdata);

	psy_cfg.drv_data = pdata;
	psy_cfg.fwnode = dev_fwnode(dev);

	pdata->charger = devm_power_supply_register(dev,
						    &bq257xx_power_supply_desc,
						    &psy_cfg);
	if (IS_ERR(pdata->charger))
		return dev_err_probe(dev, PTR_ERR(pdata->charger),
				     "Power supply register charger failed\n");

	ret = bq257xx_parse_dt(pdata, &psy_cfg, dev);
	if (ret)
		return ret;

	ret = pdata->chip->bq257xx_hw_init(pdata);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot initialize the charger\n");

	platform_set_drvdata(pdev, pdata);

	if (bq->client->irq) {
		ret = devm_request_threaded_irq(dev, bq->client->irq, NULL,
						bq257xx_irq_handler_thread,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(&bq->client->dev), pdata);
		if (ret < 0)
			dev_err_probe(dev, ret, "Charger get irq failed\n");
	}

	return ret;
}

static void bq257xx_charger_shutdown(struct platform_device *pdev)
{
	struct bq257xx_chg *pdata = platform_get_drvdata(pdev);

	pdata->chip->bq257xx_hw_shutdown(pdata);
}

static struct platform_driver bq257xx_chg_driver = {
	.driver = {
		.name = "bq257xx-charger",
	},
	.probe = bq257xx_charger_probe,
	.shutdown = bq257xx_charger_shutdown,
};
module_platform_driver(bq257xx_chg_driver);

MODULE_DESCRIPTION("bq257xx charger driver");
MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_LICENSE("GPL");
