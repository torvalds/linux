// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM BD99954 charger driver
 *
 * Copyright (C) 2020 Rohm Semiconductors
 *	Originally written by:
 *		Mikko Mutanen <mikko.mutanen@fi.rohmeurope.com>
 *		Markus Laine <markus.laine@fi.rohmeurope.com>
 *	Bugs added by:
 *		Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>
 */

/*
 *   The battery charging profile of BD99954.
 *
 *   Curve (1) represents charging current.
 *   Curve (2) represents battery voltage.
 *
 *   The BD99954 data sheet divides charging to three phases.
 *   a) Trickle-charge with constant current (8).
 *   b) pre-charge with constant current (6)
 *   c) fast-charge, first with constant current (5) phase. After
 *      the battery voltage has reached target level (4) we have constant
 *      voltage phase until charging current has dropped to termination
 *      level (7)
 *
 *    V ^                                                        ^ I
 *      .                                                        .
 *      .                                                        .
 *(4)` `.` ` ` ` ` ` ` ` ` ` ` ` ` ` ----------------------------.
 *      .                           :/                           .
 *      .                     o----+/:/ ` ` ` ` ` ` ` ` ` ` ` ` `.` ` (5)
 *      .                     +   ::  +                          .
 *      .                     +  /-   --                         .
 *      .                     +`/-     +                         .
 *      .                     o/-      -:                        .
 *      .                    .s.        +`                       .
 *      .                  .--+         `/                       .
 *      .               ..``  +          .:                      .
 *      .             -`      +           --                     .
 *      .    (2)  ...``       +            :-                    .
 *      .    ...``            +             -:                   .
 *(3)` `.`.""  ` ` ` `+-------- ` ` ` ` ` ` `.:` ` ` ` ` ` ` ` ` .` ` (6)
 *      .             +                       `:.                .
 *      .             +                         -:               .
 *      .             +                           -:.            .
 *      .             +                             .--.         .
 *      .   (1)       +                                `.+` ` ` `.` ` (7)
 *      -..............` ` ` ` ` ` ` ` ` ` ` ` ` ` ` ` ` + ` ` ` .` ` (8)
 *      .                                                +       -
 *      -------------------------------------------------+++++++++-->
 *      |   trickle   |  pre  |          fast            |
 *
 * Details of DT properties for different limits can be found from BD99954
 * device tree binding documentation.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "bd99954-charger.h"

struct battery_data {
	u16 precharge_current;	/* Trickle-charge Current */
	u16 fc_reg_voltage;	/* Fast Charging Regulation Voltage */
	u16 voltage_min;
	u16 voltage_max;
};

/* Initial field values, converted to initial register values */
struct bd9995x_init_data {
	u16 vsysreg_set;	/* VSYS Regulation Setting */
	u16 ibus_lim_set;	/* VBUS input current limitation */
	u16 icc_lim_set;	/* VCC/VACP Input Current Limit Setting */
	u16 itrich_set;		/* Trickle-charge Current Setting */
	u16 iprech_set;		/* Pre-Charge Current Setting */
	u16 ichg_set;		/* Fast-Charge constant current */
	u16 vfastchg_reg_set1;	/* Fast Charging Regulation Voltage */
	u16 vprechg_th_set;	/* Pre-charge Voltage Threshold Setting */
	u16 vrechg_set;		/* Re-charge Battery Voltage Setting */
	u16 vbatovp_set;	/* Battery Over Voltage Threshold Setting */
	u16 iterm_set;		/* Charging termination current */
};

struct bd9995x_state {
	u8 online;
	u16 chgstm_status;
	u16 vbat_vsys_status;
	u16 vbus_vcc_status;
};

struct bd9995x_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;

	struct regmap *rmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	int chip_id;
	int chip_rev;
	struct bd9995x_init_data init_data;
	struct bd9995x_state state;

	struct mutex lock; /* Protect state data */
};

static const struct regmap_range bd9995x_readonly_reg_ranges[] = {
	regmap_reg_range(CHGSTM_STATUS, SEL_ILIM_VAL),
	regmap_reg_range(IOUT_DACIN_VAL, IOUT_DACIN_VAL),
	regmap_reg_range(VCC_UCD_STATUS, VCC_IDD_STATUS),
	regmap_reg_range(VBUS_UCD_STATUS, VBUS_IDD_STATUS),
	regmap_reg_range(CHIP_ID, CHIP_REV),
	regmap_reg_range(SYSTEM_STATUS, SYSTEM_STATUS),
	regmap_reg_range(IBATP_VAL, VBAT_AVE_VAL),
	regmap_reg_range(VTH_VAL, EXTIADP_AVE_VAL),
};

static const struct regmap_access_table bd9995x_writeable_regs = {
	.no_ranges = bd9995x_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(bd9995x_readonly_reg_ranges),
};

static const struct regmap_range bd9995x_volatile_reg_ranges[] = {
	regmap_reg_range(CHGSTM_STATUS, WDT_STATUS),
	regmap_reg_range(VCC_UCD_STATUS, VCC_IDD_STATUS),
	regmap_reg_range(VBUS_UCD_STATUS, VBUS_IDD_STATUS),
	regmap_reg_range(INT0_STATUS, INT7_STATUS),
	regmap_reg_range(SYSTEM_STATUS, SYSTEM_CTRL_SET),
	regmap_reg_range(IBATP_VAL, EXTIADP_AVE_VAL), /* Measurement regs */
};

static const struct regmap_access_table bd9995x_volatile_regs = {
	.yes_ranges = bd9995x_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bd9995x_volatile_reg_ranges),
};

static const struct regmap_range_cfg regmap_range_cfg[] = {
	{
	.selector_reg     = MAP_SET,
	.selector_mask    = 0xFFFF,
	.selector_shift   = 0,
	.window_start     = 0,
	.window_len       = 0x100,
	.range_min        = 0 * 0x100,
	.range_max        = 3 * 0x100,
	},
};

static const struct regmap_config bd9995x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.reg_stride = 1,

	.max_register = 3 * 0x100,
	.cache_type = REGCACHE_RBTREE,

	.ranges = regmap_range_cfg,
	.num_ranges = ARRAY_SIZE(regmap_range_cfg),
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.wr_table = &bd9995x_writeable_regs,
	.volatile_table = &bd9995x_volatile_regs,
};

enum bd9995x_chrg_fault {
	CHRG_FAULT_NORMAL,
	CHRG_FAULT_INPUT,
	CHRG_FAULT_THERMAL_SHUTDOWN,
	CHRG_FAULT_TIMER_EXPIRED,
};

static int bd9995x_get_prop_batt_health(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_BATTEMP], &tmp);
	if (ret)
		return POWER_SUPPLY_HEALTH_UNKNOWN;

	/* TODO: Check these against datasheet page 34 */

	switch (tmp) {
	case ROOM:
		return POWER_SUPPLY_HEALTH_GOOD;
	case HOT1:
	case HOT2:
	case HOT3:
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	case COLD1:
	case COLD2:
		return POWER_SUPPLY_HEALTH_COLD;
	case TEMP_DIS:
	case BATT_OPEN:
	default:
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}
}

static int bd9995x_get_prop_charge_type(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_CHGSTM_STATE], &tmp);
	if (ret)
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

	switch (tmp) {
	case CHGSTM_TRICKLE_CHARGE:
	case CHGSTM_PRE_CHARGE:
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	case CHGSTM_FAST_CHARGE:
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	case CHGSTM_TOP_OFF:
	case CHGSTM_DONE:
	case CHGSTM_SUSPEND:
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	default: /* Rest of the states are error related, no charging */
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}
}

static bool bd9995x_get_prop_batt_present(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_BATTEMP], &tmp);
	if (ret)
		return false;

	return tmp != BATT_OPEN;
}

static int bd9995x_get_prop_batt_voltage(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_VBAT_VAL], &tmp);
	if (ret)
		return 0;

	tmp = min(tmp, 19200);

	return tmp * 1000;
}

static int bd9995x_get_prop_batt_current(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_IBATP_VAL], &tmp);
	if (ret)
		return 0;

	return tmp * 1000;
}

#define DEFAULT_BATTERY_TEMPERATURE 250

static int bd9995x_get_prop_batt_temp(struct bd9995x_device *bd)
{
	int ret, tmp;

	ret = regmap_field_read(bd->rmap_fields[F_THERM_VAL], &tmp);
	if (ret)
		return DEFAULT_BATTERY_TEMPERATURE;

	return (200 - tmp) * 10;
}

static int bd9995x_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	int ret, tmp;
	struct bd9995x_device *bd = power_supply_get_drvdata(psy);
	struct bd9995x_state state;

	mutex_lock(&bd->lock);
	state = bd->state;
	mutex_unlock(&bd->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		switch (state.chgstm_status) {
		case CHGSTM_TRICKLE_CHARGE:
		case CHGSTM_PRE_CHARGE:
		case CHGSTM_FAST_CHARGE:
		case CHGSTM_TOP_OFF:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;

		case CHGSTM_DONE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;

		case CHGSTM_SUSPEND:
		case CHGSTM_TEMPERATURE_ERROR_1:
		case CHGSTM_TEMPERATURE_ERROR_2:
		case CHGSTM_TEMPERATURE_ERROR_3:
		case CHGSTM_TEMPERATURE_ERROR_4:
		case CHGSTM_TEMPERATURE_ERROR_5:
		case CHGSTM_TEMPERATURE_ERROR_6:
		case CHGSTM_TEMPERATURE_ERROR_7:
		case CHGSTM_THERMAL_SHUT_DOWN_1:
		case CHGSTM_THERMAL_SHUT_DOWN_2:
		case CHGSTM_THERMAL_SHUT_DOWN_3:
		case CHGSTM_THERMAL_SHUT_DOWN_4:
		case CHGSTM_THERMAL_SHUT_DOWN_5:
		case CHGSTM_THERMAL_SHUT_DOWN_6:
		case CHGSTM_THERMAL_SHUT_DOWN_7:
		case CHGSTM_BATTERY_ERROR:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;

		default:
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BD9995X_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.online;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = regmap_field_read(bd->rmap_fields[F_IBATP_VAL], &tmp);
		if (ret)
			return ret;
		val->intval = tmp * 1000;
		break;

	case POWER_SUPPLY_PROP_CHARGE_AVG:
		ret = regmap_field_read(bd->rmap_fields[F_IBATP_AVE_VAL], &tmp);
		if (ret)
			return ret;
		val->intval = tmp * 1000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		/*
		 * Currently the DT uses this property to give the
		 * target current for fast-charging constant current phase.
		 * I think it is correct in a sense.
		 *
		 * Yet, this prop we read and return here is the programmed
		 * safety limit for combined input currents. This feels
		 * also correct in a sense.
		 *
		 * However, this results a mismatch to DT value and value
		 * read from sysfs.
		 */
		ret = regmap_field_read(bd->rmap_fields[F_SEL_ILIM_VAL], &tmp);
		if (ret)
			return ret;
		val->intval = tmp * 1000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (!state.online) {
			val->intval = 0;
			break;
		}

		ret = regmap_field_read(bd->rmap_fields[F_VFASTCHG_REG_SET1],
					&tmp);
		if (ret)
			return ret;

		/*
		 * The actual range : 2560 to 19200 mV. No matter what the
		 * register says
		 */
		val->intval = clamp_val(tmp << 4, 2560, 19200);
		val->intval *= 1000;
		break;

	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = regmap_field_read(bd->rmap_fields[F_ITERM_SET], &tmp);
		if (ret)
			return ret;
		/* Start step is 64 mA */
		val->intval = tmp << 6;
		/* Maximum is 1024 mA - no matter what register says */
		val->intval = min(val->intval, 1024);
		val->intval *= 1000;
		break;

	/* Battery properties which we access through charger */
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bd9995x_get_prop_batt_present(bd);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bd9995x_get_prop_batt_voltage(bd);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bd9995x_get_prop_batt_current(bd);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = bd9995x_get_prop_charge_type(bd);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bd9995x_get_prop_batt_health(bd);
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bd9995x_get_prop_batt_temp(bd);
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "bd99954";
		break;

	default:
		return -EINVAL;

	}

	return 0;
}

static int bd9995x_get_chip_state(struct bd9995x_device *bd,
				  struct bd9995x_state *state)
{
	int i, ret, tmp;
	struct {
		struct regmap_field *id;
		u16 *data;
	} state_fields[] = {
		{
			bd->rmap_fields[F_CHGSTM_STATE], &state->chgstm_status,
		}, {
			bd->rmap_fields[F_VBAT_VSYS_STATUS],
			&state->vbat_vsys_status,
		}, {
			bd->rmap_fields[F_VBUS_VCC_STATUS],
			&state->vbus_vcc_status,
		},
	};


	for (i = 0; i < ARRAY_SIZE(state_fields); i++) {
		ret = regmap_field_read(state_fields[i].id, &tmp);
		if (ret)
			return ret;

		*state_fields[i].data = tmp;
	}

	if (state->vbus_vcc_status & STATUS_VCC_DET ||
	    state->vbus_vcc_status & STATUS_VBUS_DET)
		state->online = 1;
	else
		state->online = 0;

	return 0;
}

static irqreturn_t bd9995x_irq_handler_thread(int irq, void *private)
{
	struct bd9995x_device *bd = private;
	int ret, status, mask, i;
	unsigned long tmp;
	struct bd9995x_state state;

	/*
	 * The bd9995x does not seem to generate big amount of interrupts.
	 * The logic regarding which interrupts can cause relevant
	 * status changes seem to be pretty complex.
	 *
	 * So lets implement really simple and hopefully bullet-proof handler:
	 * It does not really matter which IRQ we handle, we just go and
	 * re-read all interesting statuses + give the framework a nudge.
	 *
	 * Other option would be building a _complex_ and error prone logic
	 * trying to decide what could have been changed (resulting this IRQ
	 * we are now handling). During the normal operation the BD99954 does
	 * not seem to be generating much of interrupts so benefit from such
	 * logic would probably be minimal.
	 */

	ret = regmap_read(bd->rmap, INT0_STATUS, &status);
	if (ret) {
		dev_err(bd->dev, "Failed to read IRQ status\n");
		return IRQ_NONE;
	}

	ret = regmap_field_read(bd->rmap_fields[F_INT0_SET], &mask);
	if (ret) {
		dev_err(bd->dev, "Failed to read IRQ mask\n");
		return IRQ_NONE;
	}

	/* Handle only IRQs that are not masked */
	status &= mask;
	tmp = status;

	/* Lowest bit does not represent any sub-registers */
	tmp >>= 1;

	/*
	 * Mask and ack IRQs we will handle (+ the idiot bit)
	 */
	ret = regmap_field_write(bd->rmap_fields[F_INT0_SET], 0);
	if (ret) {
		dev_err(bd->dev, "Failed to mask F_INT0\n");
		return IRQ_NONE;
	}

	ret = regmap_write(bd->rmap, INT0_STATUS, status);
	if (ret) {
		dev_err(bd->dev, "Failed to ack F_INT0\n");
		goto err_umask;
	}

	for_each_set_bit(i, &tmp, 7) {
		int sub_status, sub_mask;
		int sub_status_reg[] = {
			INT1_STATUS, INT2_STATUS, INT3_STATUS, INT4_STATUS,
			INT5_STATUS, INT6_STATUS, INT7_STATUS,
		};
		struct regmap_field *sub_mask_f[] = {
			bd->rmap_fields[F_INT1_SET],
			bd->rmap_fields[F_INT2_SET],
			bd->rmap_fields[F_INT3_SET],
			bd->rmap_fields[F_INT4_SET],
			bd->rmap_fields[F_INT5_SET],
			bd->rmap_fields[F_INT6_SET],
			bd->rmap_fields[F_INT7_SET],
		};

		/* Clear sub IRQs */
		ret = regmap_read(bd->rmap, sub_status_reg[i], &sub_status);
		if (ret) {
			dev_err(bd->dev, "Failed to read IRQ sub-status\n");
			goto err_umask;
		}

		ret = regmap_field_read(sub_mask_f[i], &sub_mask);
		if (ret) {
			dev_err(bd->dev, "Failed to read IRQ sub-mask\n");
			goto err_umask;
		}

		/* Ack active sub-statuses */
		sub_status &= sub_mask;

		ret = regmap_write(bd->rmap, sub_status_reg[i], sub_status);
		if (ret) {
			dev_err(bd->dev, "Failed to ack sub-IRQ\n");
			goto err_umask;
		}
	}

	ret = regmap_field_write(bd->rmap_fields[F_INT0_SET], mask);
	if (ret)
		/* May as well retry once */
		goto err_umask;

	/* Read whole chip state */
	ret = bd9995x_get_chip_state(bd, &state);
	if (ret < 0) {
		dev_err(bd->dev, "Failed to read chip state\n");
	} else {
		mutex_lock(&bd->lock);
		bd->state = state;
		mutex_unlock(&bd->lock);

		power_supply_changed(bd->charger);
	}

	return IRQ_HANDLED;

err_umask:
	ret = regmap_field_write(bd->rmap_fields[F_INT0_SET], mask);
	if (ret)
		dev_err(bd->dev,
		"Failed to un-mask F_INT0 - IRQ permanently disabled\n");

	return IRQ_NONE;
}

static int __bd9995x_chip_reset(struct bd9995x_device *bd)
{
	int ret, state;
	int rst_check_counter = 10;
	u16 tmp = ALLRST | OTPLD;

	ret = regmap_raw_write(bd->rmap, SYSTEM_CTRL_SET, &tmp, 2);
	if (ret < 0)
		return ret;

	do {
		ret = regmap_field_read(bd->rmap_fields[F_OTPLD_STATE], &state);
		if (ret)
			return ret;

		msleep(10);
	} while (state == 0 && --rst_check_counter);

	if (!rst_check_counter) {
		dev_err(bd->dev, "chip reset not completed\n");
		return -ETIMEDOUT;
	}

	tmp = 0;
	ret = regmap_raw_write(bd->rmap, SYSTEM_CTRL_SET, &tmp, 2);

	return ret;
}

static int bd9995x_hw_init(struct bd9995x_device *bd)
{
	int ret;
	int i;
	struct bd9995x_state state;
	struct bd9995x_init_data *id = &bd->init_data;

	const struct {
		enum bd9995x_fields id;
		u16 value;
	} init_data[] = {
		/* Enable the charging trigger after SDP charger attached */
		{F_SDP_CHG_TRIG_EN,	1},
		/* Enable charging trigger after SDP charger attached */
		{F_SDP_CHG_TRIG,	1},
		/* Disable charging trigger by BC1.2 detection */
		{F_VBUS_BC_DISEN,	1},
		/* Disable charging trigger by BC1.2 detection */
		{F_VCC_BC_DISEN,	1},
		/* Disable automatic limitation of the input current */
		{F_ILIM_AUTO_DISEN,	1},
		/* Select current limitation when SDP charger attached*/
		{F_SDP_500_SEL,		1},
		/* Select current limitation when DCP charger attached */
		{F_DCP_2500_SEL,	1},
		{F_VSYSREG_SET,		id->vsysreg_set},
		/* Activate USB charging and DC/DC converter */
		{F_USB_SUS,		0},
		/* DCDC clock: 1200 kHz*/
		{F_DCDC_CLK_SEL,	3},
		/* Enable charging */
		{F_CHG_EN,		1},
		/* Disable Input current Limit setting voltage measurement */
		{F_EXTIADPEN,		0},
		/* Disable input current limiting */
		{F_VSYS_PRIORITY,	1},
		{F_IBUS_LIM_SET,	id->ibus_lim_set},
		{F_ICC_LIM_SET,		id->icc_lim_set},
		/* Charge Termination Current Setting to 0*/
		{F_ITERM_SET,		id->iterm_set},
		/* Trickle-charge Current Setting */
		{F_ITRICH_SET,		id->itrich_set},
		/* Pre-charge Current setting */
		{F_IPRECH_SET,		id->iprech_set},
		/* Fast Charge Current for constant current phase */
		{F_ICHG_SET,		id->ichg_set},
		/* Fast Charge Voltage Regulation Setting */
		{F_VFASTCHG_REG_SET1,	id->vfastchg_reg_set1},
		/* Set Pre-charge Voltage Threshold for trickle charging. */
		{F_VPRECHG_TH_SET,	id->vprechg_th_set},
		{F_VRECHG_SET,		id->vrechg_set},
		{F_VBATOVP_SET,		id->vbatovp_set},
		/* Reverse buck boost voltage Setting */
		{F_VRBOOST_SET,		0},
		/* Disable fast-charging watchdog */
		{F_WDT_FST,		0},
		/* Disable pre-charging watchdog */
		{F_WDT_PRE,		0},
		/* Power save off */
		{F_POWER_SAVE_MODE,	0},
		{F_INT1_SET,		INT1_ALL},
		{F_INT2_SET,		INT2_ALL},
		{F_INT3_SET,		INT3_ALL},
		{F_INT4_SET,		INT4_ALL},
		{F_INT5_SET,		INT5_ALL},
		{F_INT6_SET,		INT6_ALL},
		{F_INT7_SET,		INT7_ALL},
	};

	/*
	 * Currently we initialize charger to a known state at startup.
	 * If we want to allow for example the boot code to initialize
	 * charger we should get rid of this.
	 */
	ret = __bd9995x_chip_reset(bd);
	if (ret < 0)
		return ret;

	/* Initialize currents/voltages and other parameters */
	for (i = 0; i < ARRAY_SIZE(init_data); i++) {
		ret = regmap_field_write(bd->rmap_fields[init_data[i].id],
					 init_data[i].value);
		if (ret) {
			dev_err(bd->dev, "failed to initialize charger (%d)\n",
				ret);
			return ret;
		}
	}

	ret = bd9995x_get_chip_state(bd, &state);
	if (ret < 0)
		return ret;

	mutex_lock(&bd->lock);
	bd->state = state;
	mutex_unlock(&bd->lock);

	return 0;
}

static enum power_supply_property bd9995x_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	/* Battery props we access through charger */
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static const struct power_supply_desc bd9995x_power_supply_desc = {
	.name = "bd9995x-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = bd9995x_power_supply_props,
	.num_properties = ARRAY_SIZE(bd9995x_power_supply_props),
	.get_property = bd9995x_power_supply_get_property,
};

/*
 * Limit configurations for vbus-input-current and vcc-vacp-input-current
 * Minimum limit is 0 uA. Max is 511 * 32000 uA = 16352000 uA. This is
 * configured by writing a register so that each increment in register
 * value equals to 32000 uA limit increment.
 *
 * Eg, value 0x0 is limit 0, value 0x1 is limit 32000, ...
 * Describe the setting in linear_range table.
 */
static const struct linear_range input_current_limit_ranges[] = {
	{
		.min = 0,
		.step = 32000,
		.min_sel = 0x0,
		.max_sel = 0x1ff,
	},
};

/* Possible trickle, pre-charging and termination current values */
static const struct linear_range charging_current_ranges[] = {
	{
		.min = 0,
		.step = 64000,
		.min_sel = 0x0,
		.max_sel = 0x10,
	}, {
		.min = 1024000,
		.step = 0,
		.min_sel = 0x11,
		.max_sel = 0x1f,
	},
};

/*
 * Fast charging voltage regulation, starting re-charging limit
 * and battery over voltage protection have same possible values
 */
static const struct linear_range charge_voltage_regulation_ranges[] = {
	{
		.min = 2560000,
		.step = 0,
		.min_sel = 0,
		.max_sel = 0xA0,
	}, {
		.min = 2560000,
		.step = 16000,
		.min_sel = 0xA0,
		.max_sel = 0x4B0,
	}, {
		.min = 19200000,
		.step = 0,
		.min_sel = 0x4B0,
		.max_sel = 0x7FF,
	},
};

/* Possible VSYS voltage regulation values */
static const struct linear_range vsys_voltage_regulation_ranges[] = {
	{
		.min = 2560000,
		.step = 0,
		.min_sel = 0,
		.max_sel = 0x28,
	}, {
		.min = 2560000,
		.step = 64000,
		.min_sel = 0x28,
		.max_sel = 0x12C,
	}, {
		.min = 19200000,
		.step = 0,
		.min_sel = 0x12C,
		.max_sel = 0x1FF,
	},
};

/* Possible settings for switching from trickle to pre-charging limits */
static const struct linear_range trickle_to_pre_threshold_ranges[] = {
	{
		.min = 2048000,
		.step = 0,
		.min_sel = 0,
		.max_sel = 0x20,
	}, {
		.min = 2048000,
		.step = 64000,
		.min_sel = 0x20,
		.max_sel = 0x12C,
	}, {
		.min = 19200000,
		.step = 0,
		.min_sel = 0x12C,
		.max_sel = 0x1FF
	}
};

/* Possible current values for fast-charging constant current phase */
static const struct linear_range fast_charge_current_ranges[] = {
	{
		.min = 0,
		.step = 64000,
		.min_sel = 0,
		.max_sel = 0xFF,
	}
};

struct battery_init {
	const char *name;
	int *info_data;
	const struct linear_range *range;
	int ranges;
	u16 *data;
};

struct dt_init {
	char *prop;
	const struct linear_range *range;
	int ranges;
	u16 *data;
};

static int bd9995x_fw_probe(struct bd9995x_device *bd)
{
	int ret;
	struct power_supply_battery_info info;
	u32 property;
	int i;
	int regval;
	bool found;
	struct bd9995x_init_data *init = &bd->init_data;
	struct battery_init battery_inits[] = {
		{
			.name = "trickle-charging current",
			.info_data = &info.tricklecharge_current_ua,
			.range = &charging_current_ranges[0],
			.ranges = 2,
			.data = &init->itrich_set,
		}, {
			.name = "pre-charging current",
			.info_data = &info.precharge_current_ua,
			.range = &charging_current_ranges[0],
			.ranges = 2,
			.data = &init->iprech_set,
		}, {
			.name = "pre-to-trickle charge voltage threshold",
			.info_data = &info.precharge_voltage_max_uv,
			.range = &trickle_to_pre_threshold_ranges[0],
			.ranges = 2,
			.data = &init->vprechg_th_set,
		}, {
			.name = "charging termination current",
			.info_data = &info.charge_term_current_ua,
			.range = &charging_current_ranges[0],
			.ranges = 2,
			.data = &init->iterm_set,
		}, {
			.name = "charging re-start voltage",
			.info_data = &info.charge_restart_voltage_uv,
			.range = &charge_voltage_regulation_ranges[0],
			.ranges = 2,
			.data = &init->vrechg_set,
		}, {
			.name = "battery overvoltage limit",
			.info_data = &info.overvoltage_limit_uv,
			.range = &charge_voltage_regulation_ranges[0],
			.ranges = 2,
			.data = &init->vbatovp_set,
		}, {
			.name = "fast-charging max current",
			.info_data = &info.constant_charge_current_max_ua,
			.range = &fast_charge_current_ranges[0],
			.ranges = 1,
			.data = &init->ichg_set,
		}, {
			.name = "fast-charging voltage",
			.info_data = &info.constant_charge_voltage_max_uv,
			.range = &charge_voltage_regulation_ranges[0],
			.ranges = 2,
			.data = &init->vfastchg_reg_set1,
		},
	};
	struct dt_init props[] = {
		{
			.prop = "rohm,vsys-regulation-microvolt",
			.range = &vsys_voltage_regulation_ranges[0],
			.ranges = 2,
			.data = &init->vsysreg_set,
		}, {
			.prop = "rohm,vbus-input-current-limit-microamp",
			.range = &input_current_limit_ranges[0],
			.ranges = 1,
			.data = &init->ibus_lim_set,
		}, {
			.prop = "rohm,vcc-input-current-limit-microamp",
			.range = &input_current_limit_ranges[0],
			.ranges = 1,
			.data = &init->icc_lim_set,
		},
	};

	/*
	 * The power_supply_get_battery_info() does not support getting values
	 * from ACPI. Let's fix it if ACPI is required here.
	 */
	ret = power_supply_get_battery_info(bd->charger, &info);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(battery_inits); i++) {
		int val = *battery_inits[i].info_data;
		const struct linear_range *range = battery_inits[i].range;
		int ranges = battery_inits[i].ranges;

		if (val == -EINVAL)
			continue;

		ret = linear_range_get_selector_low_array(range, ranges, val,
							  &regval, &found);
		if (ret) {
			dev_err(bd->dev, "Unsupported value for %s\n",
				battery_inits[i].name);

			power_supply_put_battery_info(bd->charger, &info);
			return -EINVAL;
		}
		if (!found) {
			dev_warn(bd->dev,
				 "Unsupported value for %s - using smaller\n",
				 battery_inits[i].name);
		}
		*(battery_inits[i].data) = regval;
	}

	power_supply_put_battery_info(bd->charger, &info);

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = device_property_read_u32(bd->dev, props[i].prop,
					       &property);
		if (ret < 0) {
			dev_err(bd->dev, "failed to read %s", props[i].prop);

			return ret;
		}

		ret = linear_range_get_selector_low_array(props[i].range,
							  props[i].ranges,
							  property, &regval,
							  &found);
		if (ret) {
			dev_err(bd->dev, "Unsupported value for '%s'\n",
				props[i].prop);

			return -EINVAL;
		}

		if (!found) {
			dev_warn(bd->dev,
				 "Unsupported value for '%s' - using smaller\n",
				 props[i].prop);
		}

		*(props[i].data) = regval;
	}

	return 0;
}

static void bd9995x_chip_reset(void *bd)
{
	__bd9995x_chip_reset(bd);
}

static int bd9995x_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct bd9995x_device *bd;
	struct power_supply_config psy_cfg = {};
	int ret;
	int i;

	bd = devm_kzalloc(dev, sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->client = client;
	bd->dev = dev;
	psy_cfg.drv_data = bd;
	psy_cfg.of_node = dev->of_node;

	mutex_init(&bd->lock);

	bd->rmap = devm_regmap_init_i2c(client, &bd9995x_regmap_config);
	if (IS_ERR(bd->rmap)) {
		dev_err(dev, "Failed to setup register access via i2c\n");
		return PTR_ERR(bd->rmap);
	}

	for (i = 0; i < ARRAY_SIZE(bd9995x_reg_fields); i++) {
		const struct reg_field *reg_fields = bd9995x_reg_fields;

		bd->rmap_fields[i] = devm_regmap_field_alloc(dev, bd->rmap,
							     reg_fields[i]);
		if (IS_ERR(bd->rmap_fields[i])) {
			dev_err(dev, "cannot allocate regmap field\n");
			return PTR_ERR(bd->rmap_fields[i]);
		}
	}

	i2c_set_clientdata(client, bd);

	ret = regmap_field_read(bd->rmap_fields[F_CHIP_ID], &bd->chip_id);
	if (ret) {
		dev_err(dev, "Cannot read chip ID.\n");
		return ret;
	}

	if (bd->chip_id != BD99954_ID) {
		dev_err(dev, "Chip with ID=0x%x, not supported!\n",
			bd->chip_id);
		return -ENODEV;
	}

	ret = regmap_field_read(bd->rmap_fields[F_CHIP_REV], &bd->chip_rev);
	if (ret) {
		dev_err(dev, "Cannot read revision.\n");
		return ret;
	}

	dev_info(bd->dev, "Found BD99954 chip rev %d\n", bd->chip_rev);

	/*
	 * We need to init the psy before we can call
	 * power_supply_get_battery_info() for it
	 */
	bd->charger = devm_power_supply_register(bd->dev,
						 &bd9995x_power_supply_desc,
						&psy_cfg);
	if (IS_ERR(bd->charger)) {
		dev_err(dev, "Failed to register power supply\n");
		return PTR_ERR(bd->charger);
	}

	ret = bd9995x_fw_probe(bd);
	if (ret < 0) {
		dev_err(dev, "Cannot read device properties.\n");
		return ret;
	}

	ret = bd9995x_hw_init(bd);
	if (ret < 0) {
		dev_err(dev, "Cannot initialize the chip.\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, bd9995x_chip_reset, bd);
	if (ret)
		return ret;

	return devm_request_threaded_irq(dev, client->irq, NULL,
					 bd9995x_irq_handler_thread,
					 IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					 BD9995X_IRQ_PIN, bd);
}

static const struct of_device_id bd9995x_of_match[] = {
	{ .compatible = "rohm,bd99954", },
	{ }
};
MODULE_DEVICE_TABLE(of, bd9995x_of_match);

static struct i2c_driver bd9995x_driver = {
	.driver = {
		.name = "bd9995x-charger",
		.of_match_table = bd9995x_of_match,
	},
	.probe_new = bd9995x_probe,
};
module_i2c_driver(bd9995x_driver);

MODULE_AUTHOR("Laine Markus <markus.laine@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD99954 charger driver");
MODULE_LICENSE("GPL");
