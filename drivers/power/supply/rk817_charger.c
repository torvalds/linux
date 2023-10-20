// SPDX-License-Identifier: GPL-2.0+
/*
 * Charger Driver for Rockchip rk817
 *
 * Copyright (c) 2021 Maya Matuszczyk <maccraft123mc@gmail.com>
 *
 * Authors: Maya Matuszczyk <maccraft123mc@gmail.com>
 *	    Chris Morgan <macromorgan@hotmail.com>
 */

#include <asm/unaligned.h>
#include <linux/devm-helpers.h>
#include <linux/mfd/rk808.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

/* Charging statuses reported by hardware register */
enum rk817_charge_status {
	CHRG_OFF,
	DEAD_CHRG,
	TRICKLE_CHRG,
	CC_OR_CV_CHRG,
	CHARGE_FINISH,
	USB_OVER_VOL,
	BAT_TMP_ERR,
	BAT_TIM_ERR,
};

/*
 * Max charging current read to/written from hardware register.
 * Note how highest value corresponding to 0x7 is the lowest
 * current, this is per the datasheet.
 */
enum rk817_chg_cur {
	CHG_1A,
	CHG_1_5A,
	CHG_2A,
	CHG_2_5A,
	CHG_2_75A,
	CHG_3A,
	CHG_3_5A,
	CHG_0_5A,
};

struct rk817_charger {
	struct device *dev;
	struct rk808 *rk808;

	struct power_supply *bat_ps;
	struct power_supply *chg_ps;
	bool plugged_in;
	bool battery_present;

	/*
	 * voltage_k and voltage_b values are used to calibrate the ADC
	 * voltage readings. While they are documented in the BSP kernel and
	 * datasheet as voltage_k and voltage_b, there is no further
	 * information explaining them in more detail.
	 */

	uint32_t voltage_k;
	uint32_t voltage_b;

	/*
	 * soc - state of charge - like the BSP this is stored as a percentage,
	 * to the thousandth. BSP has a display state of charge (dsoc) and a
	 * remaining state of charge (rsoc). This value will be used for both
	 * purposes here so we don't do any fancy math to try and "smooth" the
	 * charge and just report it as it is. Note for example an soc of 100
	 * is stored as 100000, an soc of 50 is stored as 50000, etc.
	 */
	int soc;

	/*
	 * Capacity of battery when fully charged, equal or less than design
	 * capacity depending upon wear. BSP kernel saves to nvram in mAh,
	 * so this value is in mAh not the standard uAh.
	 */
	int fcc_mah;

	/*
	 * Calibrate the SOC on a fully charged battery, this way we can use
	 * the calibrated SOC value to correct for columb counter drift.
	 */
	bool soc_cal;

	/* Implementation specific immutable properties from device tree */
	int res_div;
	int sleep_enter_current_ua;
	int sleep_filter_current_ua;
	int bat_charge_full_design_uah;
	int bat_voltage_min_design_uv;
	int bat_voltage_max_design_uv;

	/* Values updated periodically by driver for display. */
	int charge_now_uah;
	int volt_avg_uv;
	int cur_avg_ua;
	int max_chg_cur_ua;
	int max_chg_volt_uv;
	int charge_status;
	int charger_input_volt_avg_uv;

	/* Work queue to periodically update values. */
	struct delayed_work work;
};

/* ADC coefficients extracted from BSP kernel */
#define ADC_TO_CURRENT(adc_value, res_div)	\
	(adc_value * 172 / res_div)

#define CURRENT_TO_ADC(current, samp_res)	\
	(current * samp_res / 172)

#define CHARGE_TO_ADC(capacity, res_div)	\
	(capacity * res_div * 3600 / 172 * 1000)

#define ADC_TO_CHARGE_UAH(adc_value, res_div)	\
	(adc_value / 3600 * 172 / res_div)

static int rk817_chg_cur_to_reg(u32 chg_cur_ma)
{
	if (chg_cur_ma >= 3500)
		return CHG_3_5A;
	else if (chg_cur_ma >= 3000)
		return CHG_3A;
	else if (chg_cur_ma >= 2750)
		return CHG_2_75A;
	else if (chg_cur_ma >= 2500)
		return CHG_2_5A;
	else if (chg_cur_ma >= 2000)
		return CHG_2A;
	else if (chg_cur_ma >= 1500)
		return CHG_1_5A;
	else if (chg_cur_ma >= 1000)
		return CHG_1A;
	else if (chg_cur_ma >= 500)
		return CHG_0_5A;
	else
		return -EINVAL;
}

static int rk817_chg_cur_from_reg(u8 reg)
{
	switch (reg) {
	case CHG_0_5A:
		return 500000;
	case CHG_1A:
		return 1000000;
	case CHG_1_5A:
		return 1500000;
	case CHG_2A:
		return 2000000;
	case CHG_2_5A:
		return 2500000;
	case CHG_2_75A:
		return 2750000;
	case CHG_3A:
		return 3000000;
	case CHG_3_5A:
		return 3500000;
	default:
		return -EINVAL;
	}
}

static void rk817_bat_calib_vol(struct rk817_charger *charger)
{
	uint32_t vcalib0 = 0;
	uint32_t vcalib1 = 0;
	u8 bulk_reg[2];

	/* calibrate voltage */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_VCALIB0_H,
			 bulk_reg, 2);
	vcalib0 = get_unaligned_be16(bulk_reg);

	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_VCALIB1_H,
			 bulk_reg, 2);
	vcalib1 = get_unaligned_be16(bulk_reg);

	/* values were taken from BSP kernel */
	charger->voltage_k = (4025 - 2300) * 1000 /
			     ((vcalib1 - vcalib0) ? (vcalib1 - vcalib0) : 1);
	charger->voltage_b = 4025 - (charger->voltage_k * vcalib1) / 1000;
}

static void rk817_bat_calib_cur(struct rk817_charger *charger)
{
	u8 bulk_reg[2];

	/* calibrate current */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_IOFFSET_H,
			 bulk_reg, 2);
	regmap_bulk_write(charger->rk808->regmap, RK817_GAS_GAUGE_CAL_OFFSET_H,
			  bulk_reg, 2);
}

/*
 * note that only the fcc_mah is really used by this driver, the other values
 * are to ensure we can remain backwards compatible with the BSP kernel.
 */
static int rk817_record_battery_nvram_values(struct rk817_charger *charger)
{
	u8 bulk_reg[3];
	int ret, rsoc;

	/*
	 * write the soc value to the nvram location used by the BSP kernel
	 * for the dsoc value.
	 */
	put_unaligned_le24(charger->soc, bulk_reg);
	ret = regmap_bulk_write(charger->rk808->regmap, RK817_GAS_GAUGE_BAT_R1,
				bulk_reg, 3);
	if (ret < 0)
		return ret;
	/*
	 * write the remaining capacity in mah to the nvram location used by
	 * the BSP kernel for the rsoc value.
	 */
	rsoc = (charger->soc * charger->fcc_mah) / 100000;
	put_unaligned_le24(rsoc, bulk_reg);
	ret = regmap_bulk_write(charger->rk808->regmap, RK817_GAS_GAUGE_DATA0,
				bulk_reg, 3);
	if (ret < 0)
		return ret;
	/* write the fcc_mah in mAh, just as the BSP kernel does. */
	put_unaligned_le24(charger->fcc_mah, bulk_reg);
	ret = regmap_bulk_write(charger->rk808->regmap, RK817_GAS_GAUGE_DATA3,
				bulk_reg, 3);
	if (ret < 0)
		return ret;

	return 0;
}

static int rk817_bat_calib_cap(struct rk817_charger *charger)
{
	struct rk808 *rk808 = charger->rk808;
	int tmp, charge_now, charge_now_adc, volt_avg;
	u8 bulk_reg[4];

	/* Calibrate the soc and fcc on a fully charged battery */

	if (charger->charge_status == CHARGE_FINISH && (!charger->soc_cal)) {
		/*
		 * soc should be 100000 and columb counter should show the full
		 * charge capacity. Note that if the device is unplugged for a
		 * period of several days the columb counter will have a large
		 * margin of error, so setting it back to the full charge on
		 * a completed charge cycle should correct this (my device was
		 * showing 33% battery after 3 days unplugged when it should
		 * have been closer to 95% based on voltage and charge
		 * current).
		 */

		charger->soc = 100000;
		charge_now_adc = CHARGE_TO_ADC(charger->fcc_mah,
					       charger->res_div);
		put_unaligned_be32(charge_now_adc, bulk_reg);
		regmap_bulk_write(rk808->regmap, RK817_GAS_GAUGE_Q_INIT_H3,
				  bulk_reg, 4);

		charger->soc_cal = 1;
		dev_dbg(charger->dev,
			"Fully charged. SOC is %d, full capacity is %d\n",
			charger->soc, charger->fcc_mah * 1000);
	}

	/*
	 * The columb counter can drift up slightly, so we should correct for
	 * it. But don't correct it until we're at 100% soc.
	 */
	if (charger->charge_status == CHARGE_FINISH && charger->soc_cal) {
		regmap_bulk_read(rk808->regmap, RK817_GAS_GAUGE_Q_PRES_H3,
				 bulk_reg, 4);
		charge_now_adc = get_unaligned_be32(bulk_reg);
		if (charge_now_adc < 0)
			return charge_now_adc;
		charge_now = ADC_TO_CHARGE_UAH(charge_now_adc,
					       charger->res_div);

		/*
		 * Re-init columb counter with updated values to correct drift.
		 */
		if (charge_now / 1000 > charger->fcc_mah) {
			dev_dbg(charger->dev,
				"Recalibrating columb counter to %d uah\n",
				charge_now);
			/*
			 * Order of operations matters here to ensure we keep
			 * enough precision until the last step to keep from
			 * making needless updates to columb counter.
			 */
			charge_now_adc = CHARGE_TO_ADC(charger->fcc_mah,
					 charger->res_div);
			put_unaligned_be32(charge_now_adc, bulk_reg);
			regmap_bulk_write(rk808->regmap,
					  RK817_GAS_GAUGE_Q_INIT_H3,
					  bulk_reg, 4);
		}
	}

	/*
	 * Calibrate the fully charged capacity when we previously had a full
	 * battery (soc_cal = 1) and are now empty (at or below minimum design
	 * voltage). If our columb counter is still positive, subtract that
	 * from our fcc value to get a calibrated fcc, and if our columb
	 * counter is negative add that to our fcc (but not to exceed our
	 * design capacity).
	 */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_BAT_VOL_H,
			 bulk_reg, 2);
	tmp = get_unaligned_be16(bulk_reg);
	volt_avg = (charger->voltage_k * tmp) + 1000 * charger->voltage_b;
	if (volt_avg <= charger->bat_voltage_min_design_uv &&
	    charger->soc_cal) {
		regmap_bulk_read(rk808->regmap, RK817_GAS_GAUGE_Q_PRES_H3,
				 bulk_reg, 4);
		charge_now_adc = get_unaligned_be32(bulk_reg);
		charge_now = ADC_TO_CHARGE_UAH(charge_now_adc,
					       charger->res_div);
		/*
		 * Note, if charge_now is negative this will add it (what we
		 * want) and if it's positive this will subtract (also what
		 * we want).
		 */
		charger->fcc_mah = charger->fcc_mah - (charge_now / 1000);

		dev_dbg(charger->dev,
			"Recalibrating full charge capacity to %d uah\n",
			charger->fcc_mah * 1000);
	}

	/*
	 * Set the SOC to 0 if we are below the minimum system voltage.
	 */
	if (volt_avg <= charger->bat_voltage_min_design_uv) {
		charger->soc = 0;
		charge_now_adc = CHARGE_TO_ADC(0, charger->res_div);
		put_unaligned_be32(charge_now_adc, bulk_reg);
		regmap_bulk_write(rk808->regmap,
				  RK817_GAS_GAUGE_Q_INIT_H3, bulk_reg, 4);
		dev_warn(charger->dev,
			 "Battery voltage %d below minimum voltage %d\n",
			 volt_avg, charger->bat_voltage_min_design_uv);
		}

	rk817_record_battery_nvram_values(charger);

	return 0;
}

static void rk817_read_props(struct rk817_charger *charger)
{
	int tmp, reg;
	u8 bulk_reg[4];

	/*
	 * Recalibrate voltage and current readings if we need to BSP does both
	 * on CUR_CALIB_UPD, ignoring VOL_CALIB_UPD. Curiously enough, both
	 * documentation and the BSP show that you perform an update if bit 7
	 * is 1, but you clear the status by writing a 1 to bit 7.
	 */
	regmap_read(charger->rk808->regmap, RK817_GAS_GAUGE_ADC_CONFIG1, &reg);
	if (reg & RK817_VOL_CUR_CALIB_UPD) {
		rk817_bat_calib_cur(charger);
		rk817_bat_calib_vol(charger);
		regmap_write_bits(charger->rk808->regmap,
				  RK817_GAS_GAUGE_ADC_CONFIG1,
				  RK817_VOL_CUR_CALIB_UPD,
				  RK817_VOL_CUR_CALIB_UPD);
	}

	/* Update reported charge. */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_Q_PRES_H3,
			 bulk_reg, 4);
	tmp = get_unaligned_be32(bulk_reg);
	charger->charge_now_uah = ADC_TO_CHARGE_UAH(tmp, charger->res_div);
	if (charger->charge_now_uah < 0)
		charger->charge_now_uah = 0;
	if (charger->charge_now_uah > charger->fcc_mah * 1000)
		charger->charge_now_uah = charger->fcc_mah * 1000;

	/* Update soc based on reported charge. */
	charger->soc = charger->charge_now_uah * 100 / charger->fcc_mah;

	/* Update reported voltage. */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_BAT_VOL_H,
			 bulk_reg, 2);
	tmp = get_unaligned_be16(bulk_reg);
	charger->volt_avg_uv = (charger->voltage_k * tmp) + 1000 *
				charger->voltage_b;

	/*
	 * Update reported current. Note value from registers is a signed 16
	 * bit int.
	 */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_BAT_CUR_H,
			 bulk_reg, 2);
	tmp = (short int)get_unaligned_be16(bulk_reg);
	charger->cur_avg_ua = ADC_TO_CURRENT(tmp, charger->res_div);

	/*
	 * Update the max charge current. This value shouldn't change, but we
	 * can read it to report what the PMIC says it is instead of simply
	 * returning the default value.
	 */
	regmap_read(charger->rk808->regmap, RK817_PMIC_CHRG_OUT, &reg);
	charger->max_chg_cur_ua =
		rk817_chg_cur_from_reg(reg & RK817_CHRG_CUR_SEL);

	/*
	 * Update max charge voltage. Like the max charge current this value
	 * shouldn't change, but we can report what the PMIC says.
	 */
	regmap_read(charger->rk808->regmap, RK817_PMIC_CHRG_OUT, &reg);
	charger->max_chg_volt_uv = ((((reg & RK817_CHRG_VOL_SEL) >> 4) *
				    50000) + 4100000);

	/* Check if battery still present. */
	regmap_read(charger->rk808->regmap, RK817_PMIC_CHRG_STS, &reg);
	charger->battery_present = (reg & RK817_BAT_EXS);

	/* Get which type of charge we are using (if any). */
	regmap_read(charger->rk808->regmap, RK817_PMIC_CHRG_STS, &reg);
	charger->charge_status = (reg >> 4) & 0x07;

	/*
	 * Get charger input voltage. Note that on my example hardware (an
	 * Odroid Go Advance) the voltage of the power connector is measured
	 * on the register labelled USB in the datasheet; I don't know if this
	 * is how it is designed or just a quirk of the implementation. I
	 * believe this will also measure the voltage of the USB output when in
	 * OTG mode, if that is the case we may need to change this in the
	 * future to return 0 if the power supply status is offline (I can't
	 * test this with my current implementation. Also, when the voltage
	 * should be zero sometimes the ADC still shows a single bit (which
	 * would register as 20000uv). When this happens set it to 0.
	 */
	regmap_bulk_read(charger->rk808->regmap, RK817_GAS_GAUGE_USB_VOL_H,
			 bulk_reg, 2);
	reg = get_unaligned_be16(bulk_reg);
	if (reg > 1) {
		tmp = ((charger->voltage_k * reg / 1000 + charger->voltage_b) *
		       60 / 46);
		charger->charger_input_volt_avg_uv = tmp * 1000;
	} else {
		charger->charger_input_volt_avg_uv = 0;
	}

	/* Calibrate battery capacity and soc. */
	rk817_bat_calib_cap(charger);
}

static int rk817_bat_get_prop(struct power_supply *ps,
		enum power_supply_property prop,
		union power_supply_propval *val)
{
	struct rk817_charger *charger = power_supply_get_drvdata(ps);

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = charger->battery_present;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (charger->cur_avg_ua < 0) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		}
		switch (charger->charge_status) {
		case CHRG_OFF:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		/*
		 * Dead charge is documented, but not explained. I never
		 * observed it but assume it's a pre-charge for a dead
		 * battery.
		 */
		case DEAD_CHRG:
		case TRICKLE_CHRG:
		case CC_OR_CV_CHRG:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case CHARGE_FINISH:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			return -EINVAL;

		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (charger->charge_status) {
		case CHRG_OFF:
		case CHARGE_FINISH:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
			break;
		case TRICKLE_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
			break;
		case DEAD_CHRG:
		case CC_OR_CV_CHRG:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
			break;
		default:
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = charger->fcc_mah * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = charger->bat_charge_full_design_uah;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = charger->charge_now_uah;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = charger->bat_voltage_min_design_uv;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* Add 500 so that values like 99999 are 100% not 99%. */
		val->intval = (charger->soc + 500) / 1000;
		if (val->intval > 100)
			val->intval = 100;
		if (val->intval < 0)
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = charger->volt_avg_uv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = charger->cur_avg_ua;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = charger->max_chg_cur_ua;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = charger->max_chg_volt_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = charger->bat_voltage_max_design_uv;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int rk817_chg_get_prop(struct power_supply *ps,
			      enum power_supply_property prop,
			      union power_supply_propval *val)
{
	struct rk817_charger *charger = power_supply_get_drvdata(ps);

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = charger->plugged_in;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		/* max voltage from datasheet at 5.5v (default 5.0v) */
		val->intval = 5500000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		/* min voltage from datasheet at 3.8v (default 5.0v) */
		val->intval = 3800000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		val->intval = charger->charger_input_volt_avg_uv;
		break;
	/*
	 * While it's possible that other implementations could use different
	 * USB types, the current implementation for this PMIC (the Odroid Go
	 * Advance) only uses a dedicated charging port with no rx/tx lines.
	 */
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	default:
		return -EINVAL;
	}
	return 0;

}

static irqreturn_t rk817_plug_in_isr(int irq, void *cg)
{
	struct rk817_charger *charger;

	charger = (struct rk817_charger *)cg;
	charger->plugged_in = 1;
	power_supply_changed(charger->chg_ps);
	power_supply_changed(charger->bat_ps);
	/* try to recalibrate capacity if we hit full charge. */
	charger->soc_cal = 0;

	rk817_read_props(charger);

	dev_dbg(charger->dev, "Power Cord Inserted\n");

	return IRQ_HANDLED;
}

static irqreturn_t rk817_plug_out_isr(int irq, void *cg)
{
	struct rk817_charger *charger;
	struct rk808 *rk808;

	charger = (struct rk817_charger *)cg;
	rk808 = charger->rk808;
	charger->plugged_in = 0;
	power_supply_changed(charger->bat_ps);
	power_supply_changed(charger->chg_ps);

	/*
	 * For some reason the bits of RK817_PMIC_CHRG_IN reset whenever the
	 * power cord is unplugged. This was not documented in the BSP kernel
	 * or the datasheet and only discovered by trial and error. Set minimum
	 * USB input voltage to 4.5v and enable USB voltage input limit.
	 */
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN,
			  RK817_USB_VLIM_SEL, (0x05 << 4));
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN, RK817_USB_VLIM_EN,
			  (0x01 << 7));

	/*
	 * Set average USB input current limit to 1.5A and enable USB current
	 * input limit.
	 */
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN,
			  RK817_USB_ILIM_SEL, 0x03);
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN, RK817_USB_ILIM_EN,
			  (0x01 << 3));

	rk817_read_props(charger);

	dev_dbg(charger->dev, "Power Cord Removed\n");

	return IRQ_HANDLED;
}

static enum power_supply_property rk817_bat_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static enum power_supply_property rk817_chg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
};

static enum power_supply_usb_type rk817_usb_type[] = {
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
};

static const struct power_supply_desc rk817_bat_desc = {
	.name = "rk817-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = rk817_bat_props,
	.num_properties = ARRAY_SIZE(rk817_bat_props),
	.get_property = rk817_bat_get_prop,
};

static const struct power_supply_desc rk817_chg_desc = {
	.name = "rk817-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = rk817_usb_type,
	.num_usb_types = ARRAY_SIZE(rk817_usb_type),
	.properties = rk817_chg_props,
	.num_properties = ARRAY_SIZE(rk817_chg_props),
	.get_property = rk817_chg_get_prop,
};

static int rk817_read_battery_nvram_values(struct rk817_charger *charger)
{
	u8 bulk_reg[3];
	int ret;

	/* Read the nvram data for full charge capacity. */
	ret = regmap_bulk_read(charger->rk808->regmap,
			       RK817_GAS_GAUGE_DATA3, bulk_reg, 3);
	if (ret < 0)
		return ret;
	charger->fcc_mah = get_unaligned_le24(bulk_reg);

	/*
	 * Sanity checking for values equal to zero or less than would be
	 * practical for this device (BSP Kernel assumes 500mAH or less) for
	 * practicality purposes. Also check if the value is too large and
	 * correct it.
	 */
	if ((charger->fcc_mah < 500) ||
	   ((charger->fcc_mah * 1000) > charger->bat_charge_full_design_uah)) {
		dev_info(charger->dev,
			 "Invalid NVRAM max charge, setting to %u uAH\n",
			 charger->bat_charge_full_design_uah);
		charger->fcc_mah = charger->bat_charge_full_design_uah / 1000;
	}

	/*
	 * Read the nvram for state of charge. Sanity check for values greater
	 * than 100 (10000) or less than 0, because other things (BSP kernels,
	 * U-Boot, or even i2cset) can write to this register. If the value is
	 * off it should get corrected automatically when the voltage drops to
	 * the min (soc is 0) or when the battery is full (soc is 100).
	 */
	ret = regmap_bulk_read(charger->rk808->regmap,
			       RK817_GAS_GAUGE_BAT_R1, bulk_reg, 3);
	if (ret < 0)
		return ret;
	charger->soc = get_unaligned_le24(bulk_reg);
	if (charger->soc > 10000)
		charger->soc = 10000;
	if (charger->soc < 0)
		charger->soc = 0;

	return 0;
}

static int
rk817_read_or_set_full_charge_on_boot(struct rk817_charger *charger,
				struct power_supply_battery_info *bat_info)
{
	struct rk808 *rk808 = charger->rk808;
	u8 bulk_reg[4];
	u32 boot_voltage, boot_charge_mah;
	int ret, reg, off_time, tmp;
	bool first_boot;

	/*
	 * Check if the battery is uninitalized. If it is, the columb counter
	 * needs to be set up.
	 */
	ret = regmap_read(rk808->regmap, RK817_GAS_GAUGE_GG_STS, &reg);
	if (ret < 0)
		return ret;
	first_boot = reg & RK817_BAT_CON;
	/*
	 * If the battery is uninitialized, use the poweron voltage and an ocv
	 * lookup to guess our charge. The number won't be very accurate until
	 * we hit either our minimum voltage (0%) or full charge (100%).
	 */
	if (first_boot) {
		regmap_bulk_read(rk808->regmap, RK817_GAS_GAUGE_PWRON_VOL_H,
				 bulk_reg, 2);
		tmp = get_unaligned_be16(bulk_reg);
		boot_voltage = (charger->voltage_k * tmp) +
				1000 * charger->voltage_b;
		/*
		 * Since only implementation has no working thermistor, assume
		 * 20C for OCV lookup. If lookup fails, report error with OCV
		 * table.
		 */
		charger->soc = power_supply_batinfo_ocv2cap(bat_info,
							    boot_voltage,
							    20) * 1000;
		if (charger->soc < 0)
			charger->soc = 0;

		/* Guess that full charge capacity is the design capacity */
		charger->fcc_mah = charger->bat_charge_full_design_uah / 1000;
		/*
		 * Set battery as "set up". BSP driver uses this value even
		 * though datasheet claims it's a read-only value.
		 */
		regmap_write_bits(rk808->regmap, RK817_GAS_GAUGE_GG_STS,
				  RK817_BAT_CON, 0);
		/* Save nvram values */
		ret = rk817_record_battery_nvram_values(charger);
		if (ret < 0)
			return ret;
	} else {
		ret = rk817_read_battery_nvram_values(charger);
		if (ret < 0)
			return ret;

		regmap_bulk_read(rk808->regmap, RK817_GAS_GAUGE_Q_PRES_H3,
				 bulk_reg, 4);
		tmp = get_unaligned_be32(bulk_reg);
		if (tmp < 0)
			tmp = 0;
		boot_charge_mah = ADC_TO_CHARGE_UAH(tmp,
						    charger->res_div) / 1000;
		/*
		 * Check if the columb counter has been off for more than 30
		 * minutes as it tends to drift downward. If so, re-init soc
		 * with the boot voltage instead. Note the unit values for the
		 * OFF_CNT register appear to be in decaminutes and stops
		 * counting at 2550 (0xFF) minutes. BSP kernel used OCV, but
		 * for me occasionally that would show invalid values. Boot
		 * voltage is only accurate for me on first poweron (not
		 * reboots), but we shouldn't ever encounter an OFF_CNT more
		 * than 0 on a reboot anyway.
		 */
		regmap_read(rk808->regmap, RK817_GAS_GAUGE_OFF_CNT, &off_time);
		if (off_time >= 3) {
			regmap_bulk_read(rk808->regmap,
					 RK817_GAS_GAUGE_PWRON_VOL_H,
					 bulk_reg, 2);
			tmp = get_unaligned_be16(bulk_reg);
			boot_voltage = (charger->voltage_k * tmp) +
					1000 * charger->voltage_b;
			charger->soc =
				power_supply_batinfo_ocv2cap(bat_info,
							     boot_voltage,
							     20) * 1000;
		} else {
			charger->soc = (boot_charge_mah * 1000 * 100 /
					charger->fcc_mah);
		}
	}

	/*
	 * Now we have our full charge capacity and soc, init the columb
	 * counter.
	 */
	boot_charge_mah = charger->soc * charger->fcc_mah / 100 / 1000;
	if (boot_charge_mah > charger->fcc_mah)
		boot_charge_mah = charger->fcc_mah;
	tmp = CHARGE_TO_ADC(boot_charge_mah, charger->res_div);
	put_unaligned_be32(tmp, bulk_reg);
	ret = regmap_bulk_write(rk808->regmap, RK817_GAS_GAUGE_Q_INIT_H3,
			  bulk_reg, 4);
	if (ret < 0)
		return ret;

	/* Set QMAX value to max design capacity. */
	tmp = CHARGE_TO_ADC((charger->bat_charge_full_design_uah / 1000),
			    charger->res_div);
	put_unaligned_be32(tmp, bulk_reg);
	ret = regmap_bulk_write(rk808->regmap, RK817_GAS_GAUGE_Q_MAX_H3,
				bulk_reg, 4);
	if (ret < 0)
		return ret;

	return 0;
}

static int rk817_battery_init(struct rk817_charger *charger,
			      struct power_supply_battery_info *bat_info)
{
	struct rk808 *rk808 = charger->rk808;
	u32 tmp, max_chg_vol_mv, max_chg_cur_ma;
	u8 max_chg_vol_reg, chg_term_i_reg;
	int ret, chg_term_ma, max_chg_cur_reg;
	u8 bulk_reg[2];

	/* Get initial plug state */
	regmap_read(rk808->regmap, RK817_SYS_STS, &tmp);
	charger->plugged_in = (tmp & RK817_PLUG_IN_STS);

	/*
	 * Turn on all ADC functions to measure battery, USB, and sys voltage,
	 * as well as batt temp. Note only tested implementation so far does
	 * not use a battery with a thermistor.
	 */
	regmap_write(rk808->regmap, RK817_GAS_GAUGE_ADC_CONFIG0, 0xfc);

	/*
	 * Set relax mode voltage sampling interval and ADC offset calibration
	 * interval to 8 minutes to mirror BSP kernel. Set voltage and current
	 * modes to average to mirror BSP kernel.
	 */
	regmap_write(rk808->regmap, RK817_GAS_GAUGE_GG_CON, 0x04);

	/* Calibrate voltage like the BSP does here. */
	rk817_bat_calib_vol(charger);

	/* Write relax threshold, derived from sleep enter current. */
	tmp = CURRENT_TO_ADC(charger->sleep_enter_current_ua,
			     charger->res_div);
	put_unaligned_be16(tmp, bulk_reg);
	regmap_bulk_write(rk808->regmap, RK817_GAS_GAUGE_RELAX_THRE_H,
			  bulk_reg, 2);

	/* Write sleep sample current, derived from sleep filter current. */
	tmp = CURRENT_TO_ADC(charger->sleep_filter_current_ua,
			     charger->res_div);
	put_unaligned_be16(tmp, bulk_reg);
	regmap_bulk_write(rk808->regmap, RK817_GAS_GAUGE_SLEEP_CON_SAMP_CUR_H,
			  bulk_reg, 2);

	/* Restart battery relax voltage */
	regmap_write_bits(rk808->regmap, RK817_GAS_GAUGE_GG_STS,
			  RK817_RELAX_VOL_UPD, (0x0 << 2));

	/*
	 * Set OCV Threshold Voltage to 127.5mV. This was hard coded like this
	 * in the BSP.
	 */
	regmap_write(rk808->regmap, RK817_GAS_GAUGE_OCV_THRE_VOL, 0xff);

	/*
	 * Set maximum charging voltage to battery max voltage. Trying to be
	 * incredibly safe with these value, as setting them wrong could
	 * overcharge the battery, which would be very bad.
	 */
	max_chg_vol_mv = bat_info->constant_charge_voltage_max_uv / 1000;
	max_chg_cur_ma = bat_info->constant_charge_current_max_ua / 1000;

	if (max_chg_vol_mv < 4100) {
		return dev_err_probe(charger->dev, -EINVAL,
		       "invalid max charger voltage, value %u unsupported\n",
			max_chg_vol_mv * 1000);
	}
	if (max_chg_vol_mv > 4450) {
		dev_info(charger->dev,
			 "Setting max charge voltage to 4450000uv\n");
		max_chg_vol_mv = 4450;
	}

	if (max_chg_cur_ma < 500) {
		return dev_err_probe(charger->dev, -EINVAL,
		       "invalid max charger current, value %u unsupported\n",
		       max_chg_cur_ma * 1000);
	}
	if (max_chg_cur_ma > 3500)
		dev_info(charger->dev,
			 "Setting max charge current to 3500000ua\n");

	/*
	 * Now that the values are sanity checked, if we subtract 4100 from the
	 * max voltage and divide by 50, we conviently get the exact value for
	 * the registers, which are 4.1v, 4.15v, 4.2v, 4.25v, 4.3v, 4.35v,
	 * 4.4v, and 4.45v; these correspond to values 0x00 through 0x07.
	 */
	max_chg_vol_reg = (max_chg_vol_mv - 4100) / 50;

	max_chg_cur_reg = rk817_chg_cur_to_reg(max_chg_cur_ma);

	if (max_chg_vol_reg < 0 || max_chg_vol_reg > 7) {
		return dev_err_probe(charger->dev, -EINVAL,
		       "invalid max charger voltage, value %u unsupported\n",
		       max_chg_vol_mv * 1000);
	}
	if (max_chg_cur_reg < 0 || max_chg_cur_reg > 7) {
		return dev_err_probe(charger->dev, -EINVAL,
		       "invalid max charger current, value %u unsupported\n",
		       max_chg_cur_ma * 1000);
	}

	/*
	 * Write the values to the registers, and deliver an emergency warning
	 * in the event they are not written correctly.
	 */
	ret = regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_OUT,
				RK817_CHRG_VOL_SEL, (max_chg_vol_reg << 4));
	if (ret) {
		dev_emerg(charger->dev,
			  "Danger, unable to set max charger voltage: %u\n",
			  ret);
	}

	ret = regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_OUT,
				RK817_CHRG_CUR_SEL, max_chg_cur_reg);
	if (ret) {
		dev_emerg(charger->dev,
			  "Danger, unable to set max charger current: %u\n",
			  ret);
	}

	/* Set charge finishing mode to analog */
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_TERM,
			  RK817_CHRG_TERM_ANA_DIG, (0x0 << 2));

	/*
	 * Set charge finish current, warn if value not in range and keep
	 * default.
	 */
	chg_term_ma = bat_info->charge_term_current_ua / 1000;
	if (chg_term_ma < 150 || chg_term_ma > 400) {
		dev_warn(charger->dev,
			 "Invalid charge termination %u, keeping default\n",
			 chg_term_ma * 1000);
		chg_term_ma = 200;
	}

	/*
	 * Values of 150ma, 200ma, 300ma, and 400ma correspond to 00, 01, 10,
	 * and 11.
	 */
	chg_term_i_reg = (chg_term_ma - 100) / 100;
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_TERM,
			  RK817_CHRG_TERM_ANA_SEL, chg_term_i_reg);

	ret = rk817_read_or_set_full_charge_on_boot(charger, bat_info);
	if (ret < 0)
		return ret;

	/*
	 * Set minimum USB input voltage to 4.5v and enable USB voltage input
	 * limit.
	 */
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN,
			  RK817_USB_VLIM_SEL, (0x05 << 4));
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN, RK817_USB_VLIM_EN,
			  (0x01 << 7));

	/*
	 * Set average USB input current limit to 1.5A and enable USB current
	 * input limit.
	 */
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN,
			  RK817_USB_ILIM_SEL, 0x03);
	regmap_write_bits(rk808->regmap, RK817_PMIC_CHRG_IN, RK817_USB_ILIM_EN,
			  (0x01 << 3));

	return 0;
}

static void rk817_charging_monitor(struct work_struct *work)
{
	struct rk817_charger *charger;

	charger = container_of(work, struct rk817_charger, work.work);

	rk817_read_props(charger);

	/* Run every 8 seconds like the BSP driver did. */
	queue_delayed_work(system_wq, &charger->work, msecs_to_jiffies(8000));
}

static void rk817_cleanup_node(void *data)
{
	struct device_node *node = data;

	of_node_put(node);
}

static int rk817_charger_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct rk817_charger *charger;
	struct device_node *node;
	struct power_supply_battery_info *bat_info;
	struct device *dev = &pdev->dev;
	struct power_supply_config pscfg = {};
	int plugin_irq, plugout_irq;
	int of_value;
	int ret;

	node = of_get_child_by_name(dev->parent->of_node, "charger");
	if (!node)
		return -ENODEV;

	ret = devm_add_action_or_reset(&pdev->dev, rk817_cleanup_node, node);
	if (ret)
		return ret;

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->rk808 = rk808;

	charger->dev = &pdev->dev;
	platform_set_drvdata(pdev, charger);

	rk817_bat_calib_vol(charger);

	pscfg.drv_data = charger;
	pscfg.of_node = node;

	/*
	 * Get sample resistor value. Note only values of 10000 or 20000
	 * microohms are allowed. Schematic for my test implementation (an
	 * Odroid Go Advance) shows a 10 milliohm resistor for reference.
	 */
	ret = of_property_read_u32(node, "rockchip,resistor-sense-micro-ohms",
				   &of_value);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
				     "Error reading sample resistor value\n");
	}
	/*
	 * Store as a 1 or a 2, since all we really use the value for is as a
	 * divisor in some calculations.
	 */
	charger->res_div = (of_value == 20000) ? 2 : 1;

	/*
	 * Get sleep enter current value. Not sure what this value is for
	 * other than to help calibrate the relax threshold.
	 */
	ret = of_property_read_u32(node,
				   "rockchip,sleep-enter-current-microamp",
				   &of_value);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
				     "Error reading sleep enter cur value\n");
	}
	charger->sleep_enter_current_ua = of_value;

	/* Get sleep filter current value */
	ret = of_property_read_u32(node,
				   "rockchip,sleep-filter-current-microamp",
				   &of_value);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
				     "Error reading sleep filter cur value\n");
	}

	charger->sleep_filter_current_ua = of_value;

	charger->bat_ps = devm_power_supply_register(&pdev->dev,
						     &rk817_bat_desc, &pscfg);
	if (IS_ERR(charger->bat_ps))
		return dev_err_probe(dev, -EINVAL,
				     "Battery failed to probe\n");

	charger->chg_ps = devm_power_supply_register(&pdev->dev,
						     &rk817_chg_desc, &pscfg);
	if (IS_ERR(charger->chg_ps))
		return dev_err_probe(dev, -EINVAL,
				     "Charger failed to probe\n");

	ret = power_supply_get_battery_info(charger->bat_ps,
					    &bat_info);
	if (ret) {
		return dev_err_probe(dev, ret,
				     "Unable to get battery info\n");
	}

	if ((bat_info->charge_full_design_uah <= 0) ||
	    (bat_info->voltage_min_design_uv <= 0) ||
	    (bat_info->voltage_max_design_uv <= 0) ||
	    (bat_info->constant_charge_voltage_max_uv <= 0) ||
	    (bat_info->constant_charge_current_max_ua <= 0) ||
	    (bat_info->charge_term_current_ua <= 0)) {
		return dev_err_probe(dev, -EINVAL,
				     "Required bat info missing or invalid\n");
	}

	charger->bat_charge_full_design_uah = bat_info->charge_full_design_uah;
	charger->bat_voltage_min_design_uv = bat_info->voltage_min_design_uv;
	charger->bat_voltage_max_design_uv = bat_info->voltage_max_design_uv;

	/*
	 * Has to run after power_supply_get_battery_info as it depends on some
	 * values discovered from that routine.
	 */
	ret = rk817_battery_init(charger, bat_info);
	if (ret)
		return ret;

	power_supply_put_battery_info(charger->bat_ps, bat_info);

	plugin_irq = platform_get_irq(pdev, 0);
	if (plugin_irq < 0)
		return plugin_irq;

	plugout_irq = platform_get_irq(pdev, 1);
	if (plugout_irq < 0)
		return plugout_irq;

	ret = devm_request_threaded_irq(charger->dev, plugin_irq, NULL,
					rk817_plug_in_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_in", charger);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				      "plug_in_irq request failed!\n");
	}

	ret = devm_request_threaded_irq(charger->dev, plugout_irq, NULL,
					rk817_plug_out_isr,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"rk817_plug_out", charger);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "plug_out_irq request failed!\n");
	}

	ret = devm_delayed_work_autocancel(&pdev->dev, &charger->work,
					   rk817_charging_monitor);
	if (ret)
		return ret;

	/* Force the first update immediately. */
	mod_delayed_work(system_wq, &charger->work, 0);

	return 0;
}

static int __maybe_unused rk817_resume(struct device *dev)
{

	struct rk817_charger *charger = dev_get_drvdata(dev);

	/* force an immediate update */
	mod_delayed_work(system_wq, &charger->work, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rk817_charger_pm, NULL, rk817_resume);

static struct platform_driver rk817_charger_driver = {
	.probe    = rk817_charger_probe,
	.driver   = {
		.name  = "rk817-charger",
		.pm		= &rk817_charger_pm,
	},
};
module_platform_driver(rk817_charger_driver);

MODULE_DESCRIPTION("Battery power supply driver for RK817 PMIC");
MODULE_AUTHOR("Maya Matuszczyk <maccraft123mc@gmail.com>");
MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk817-charger");
