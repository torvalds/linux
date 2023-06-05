// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI BQ24257 charger driver
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Datasheets:
 * https://www.ti.com/product/bq24250
 * https://www.ti.com/product/bq24251
 * https://www.ti.com/product/bq24257
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/acpi.h>
#include <linux/of.h>

#define BQ24257_REG_1			0x00
#define BQ24257_REG_2			0x01
#define BQ24257_REG_3			0x02
#define BQ24257_REG_4			0x03
#define BQ24257_REG_5			0x04
#define BQ24257_REG_6			0x05
#define BQ24257_REG_7			0x06

#define BQ24257_MANUFACTURER		"Texas Instruments"
#define BQ24257_PG_GPIO			"pg"

#define BQ24257_ILIM_SET_DELAY		1000	/* msec */

/*
 * When adding support for new devices make sure that enum bq2425x_chip and
 * bq2425x_chip_name[] always stay in sync!
 */
enum bq2425x_chip {
	BQ24250,
	BQ24251,
	BQ24257,
};

static const char *const bq2425x_chip_name[] = {
	"bq24250",
	"bq24251",
	"bq24257",
};

enum bq24257_fields {
	F_WD_FAULT, F_WD_EN, F_STAT, F_FAULT,			    /* REG 1 */
	F_RESET, F_IILIMIT, F_EN_STAT, F_EN_TERM, F_CE, F_HZ_MODE,  /* REG 2 */
	F_VBAT, F_USB_DET,					    /* REG 3 */
	F_ICHG, F_ITERM,					    /* REG 4 */
	F_LOOP_STATUS, F_LOW_CHG, F_DPDM_EN, F_CE_STATUS, F_VINDPM, /* REG 5 */
	F_X2_TMR_EN, F_TMR, F_SYSOFF, F_TS_EN, F_TS_STAT,	    /* REG 6 */
	F_VOVP, F_CLR_VDP, F_FORCE_BATDET, F_FORCE_PTM,		    /* REG 7 */

	F_MAX_FIELDS
};

/* initial field values, converted from uV/uA */
struct bq24257_init_data {
	u8 ichg;	/* charge current      */
	u8 vbat;	/* regulation voltage  */
	u8 iterm;	/* termination current */
	u8 iilimit;	/* input current limit */
	u8 vovp;	/* over voltage protection voltage */
	u8 vindpm;	/* VDMP input threshold voltage */
};

struct bq24257_state {
	u8 status;
	u8 fault;
	bool power_good;
};

struct bq24257_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;

	enum bq2425x_chip chip;

	struct regmap *rmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct gpio_desc *pg;

	struct delayed_work iilimit_setup_work;

	struct bq24257_init_data init_data;
	struct bq24257_state state;

	struct mutex lock; /* protect state data */

	bool iilimit_autoset_enable;
};

static bool bq24257_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BQ24257_REG_2:
	case BQ24257_REG_4:
		return false;

	default:
		return true;
	}
}

static const struct regmap_config bq24257_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BQ24257_REG_7,
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = bq24257_is_volatile_reg,
};

static const struct reg_field bq24257_reg_fields[] = {
	/* REG 1 */
	[F_WD_FAULT]		= REG_FIELD(BQ24257_REG_1, 7, 7),
	[F_WD_EN]		= REG_FIELD(BQ24257_REG_1, 6, 6),
	[F_STAT]		= REG_FIELD(BQ24257_REG_1, 4, 5),
	[F_FAULT]		= REG_FIELD(BQ24257_REG_1, 0, 3),
	/* REG 2 */
	[F_RESET]		= REG_FIELD(BQ24257_REG_2, 7, 7),
	[F_IILIMIT]		= REG_FIELD(BQ24257_REG_2, 4, 6),
	[F_EN_STAT]		= REG_FIELD(BQ24257_REG_2, 3, 3),
	[F_EN_TERM]		= REG_FIELD(BQ24257_REG_2, 2, 2),
	[F_CE]			= REG_FIELD(BQ24257_REG_2, 1, 1),
	[F_HZ_MODE]		= REG_FIELD(BQ24257_REG_2, 0, 0),
	/* REG 3 */
	[F_VBAT]		= REG_FIELD(BQ24257_REG_3, 2, 7),
	[F_USB_DET]		= REG_FIELD(BQ24257_REG_3, 0, 1),
	/* REG 4 */
	[F_ICHG]		= REG_FIELD(BQ24257_REG_4, 3, 7),
	[F_ITERM]		= REG_FIELD(BQ24257_REG_4, 0, 2),
	/* REG 5 */
	[F_LOOP_STATUS]		= REG_FIELD(BQ24257_REG_5, 6, 7),
	[F_LOW_CHG]		= REG_FIELD(BQ24257_REG_5, 5, 5),
	[F_DPDM_EN]		= REG_FIELD(BQ24257_REG_5, 4, 4),
	[F_CE_STATUS]		= REG_FIELD(BQ24257_REG_5, 3, 3),
	[F_VINDPM]		= REG_FIELD(BQ24257_REG_5, 0, 2),
	/* REG 6 */
	[F_X2_TMR_EN]		= REG_FIELD(BQ24257_REG_6, 7, 7),
	[F_TMR]			= REG_FIELD(BQ24257_REG_6, 5, 6),
	[F_SYSOFF]		= REG_FIELD(BQ24257_REG_6, 4, 4),
	[F_TS_EN]		= REG_FIELD(BQ24257_REG_6, 3, 3),
	[F_TS_STAT]		= REG_FIELD(BQ24257_REG_6, 0, 2),
	/* REG 7 */
	[F_VOVP]		= REG_FIELD(BQ24257_REG_7, 5, 7),
	[F_CLR_VDP]		= REG_FIELD(BQ24257_REG_7, 4, 4),
	[F_FORCE_BATDET]	= REG_FIELD(BQ24257_REG_7, 3, 3),
	[F_FORCE_PTM]		= REG_FIELD(BQ24257_REG_7, 2, 2)
};

static const u32 bq24257_vbat_map[] = {
	3500000, 3520000, 3540000, 3560000, 3580000, 3600000, 3620000, 3640000,
	3660000, 3680000, 3700000, 3720000, 3740000, 3760000, 3780000, 3800000,
	3820000, 3840000, 3860000, 3880000, 3900000, 3920000, 3940000, 3960000,
	3980000, 4000000, 4020000, 4040000, 4060000, 4080000, 4100000, 4120000,
	4140000, 4160000, 4180000, 4200000, 4220000, 4240000, 4260000, 4280000,
	4300000, 4320000, 4340000, 4360000, 4380000, 4400000, 4420000, 4440000
};

#define BQ24257_VBAT_MAP_SIZE		ARRAY_SIZE(bq24257_vbat_map)

static const u32 bq24257_ichg_map[] = {
	500000, 550000, 600000, 650000, 700000, 750000, 800000, 850000, 900000,
	950000, 1000000, 1050000, 1100000, 1150000, 1200000, 1250000, 1300000,
	1350000, 1400000, 1450000, 1500000, 1550000, 1600000, 1650000, 1700000,
	1750000, 1800000, 1850000, 1900000, 1950000, 2000000
};

#define BQ24257_ICHG_MAP_SIZE		ARRAY_SIZE(bq24257_ichg_map)

static const u32 bq24257_iterm_map[] = {
	50000, 75000, 100000, 125000, 150000, 175000, 200000, 225000
};

#define BQ24257_ITERM_MAP_SIZE		ARRAY_SIZE(bq24257_iterm_map)

static const u32 bq24257_iilimit_map[] = {
	100000, 150000, 500000, 900000, 1500000, 2000000
};

#define BQ24257_IILIMIT_MAP_SIZE	ARRAY_SIZE(bq24257_iilimit_map)

static const u32 bq24257_vovp_map[] = {
	6000000, 6500000, 7000000, 8000000, 9000000, 9500000, 10000000,
	10500000
};

#define BQ24257_VOVP_MAP_SIZE		ARRAY_SIZE(bq24257_vovp_map)

static const u32 bq24257_vindpm_map[] = {
	4200000, 4280000, 4360000, 4440000, 4520000, 4600000, 4680000,
	4760000
};

#define BQ24257_VINDPM_MAP_SIZE		ARRAY_SIZE(bq24257_vindpm_map)

static int bq24257_field_read(struct bq24257_device *bq,
			      enum bq24257_fields field_id)
{
	int ret;
	int val;

	ret = regmap_field_read(bq->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int bq24257_field_write(struct bq24257_device *bq,
			       enum bq24257_fields field_id, u8 val)
{
	return regmap_field_write(bq->rmap_fields[field_id], val);
}

static u8 bq24257_find_idx(u32 value, const u32 *map, u8 map_size)
{
	u8 idx;

	for (idx = 1; idx < map_size; idx++)
		if (value < map[idx])
			break;

	return idx - 1;
}

enum bq24257_status {
	STATUS_READY,
	STATUS_CHARGE_IN_PROGRESS,
	STATUS_CHARGE_DONE,
	STATUS_FAULT,
};

enum bq24257_fault {
	FAULT_NORMAL,
	FAULT_INPUT_OVP,
	FAULT_INPUT_UVLO,
	FAULT_SLEEP,
	FAULT_BAT_TS,
	FAULT_BAT_OVP,
	FAULT_TS,
	FAULT_TIMER,
	FAULT_NO_BAT,
	FAULT_ISET,
	FAULT_INPUT_LDO_LOW,
};

static int bq24257_get_input_current_limit(struct bq24257_device *bq,
					   union power_supply_propval *val)
{
	int ret;

	ret = bq24257_field_read(bq, F_IILIMIT);
	if (ret < 0)
		return ret;

	/*
	 * The "External ILIM" and "Production & Test" modes are not exposed
	 * through this driver and not being covered by the lookup table.
	 * Should such a mode have become active let's return an error rather
	 * than exceeding the bounds of the lookup table and returning
	 * garbage.
	 */
	if (ret >= BQ24257_IILIMIT_MAP_SIZE)
		return -ENODATA;

	val->intval = bq24257_iilimit_map[ret];

	return 0;
}

static int bq24257_set_input_current_limit(struct bq24257_device *bq,
					const union power_supply_propval *val)
{
	/*
	 * Address the case where the user manually sets an input current limit
	 * while the charger auto-detection mechanism is active. In this
	 * case we want to abort and go straight to the user-specified value.
	 */
	if (bq->iilimit_autoset_enable)
		cancel_delayed_work_sync(&bq->iilimit_setup_work);

	return bq24257_field_write(bq, F_IILIMIT,
				   bq24257_find_idx(val->intval,
						    bq24257_iilimit_map,
						    BQ24257_IILIMIT_MAP_SIZE));
}

static int bq24257_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct bq24257_device *bq = power_supply_get_drvdata(psy);
	struct bq24257_state state;

	mutex_lock(&bq->lock);
	state = bq->state;
	mutex_unlock(&bq->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.power_good)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (state.status == STATUS_READY)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (state.status == STATUS_CHARGE_IN_PROGRESS)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (state.status == STATUS_CHARGE_DONE)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ24257_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bq2425x_chip_name[bq->chip];
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.power_good;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		switch (state.fault) {
		case FAULT_NORMAL:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;

		case FAULT_INPUT_OVP:
		case FAULT_BAT_OVP:
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			break;

		case FAULT_TS:
		case FAULT_BAT_TS:
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;

		case FAULT_TIMER:
			val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
			break;

		default:
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			break;
		}

		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = bq24257_ichg_map[bq->init_data.ichg];
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = bq24257_ichg_map[BQ24257_ICHG_MAP_SIZE - 1];
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = bq24257_vbat_map[bq->init_data.vbat];
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = bq24257_vbat_map[BQ24257_VBAT_MAP_SIZE - 1];
		break;

	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		val->intval = bq24257_iterm_map[bq->init_data.iterm];
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return bq24257_get_input_current_limit(bq, val);

	default:
		return -EINVAL;
	}

	return 0;
}

static int bq24257_power_supply_set_property(struct power_supply *psy,
					enum power_supply_property prop,
					const union power_supply_propval *val)
{
	struct bq24257_device *bq = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return bq24257_set_input_current_limit(bq, val);
	default:
		return -EINVAL;
	}
}

static int bq24257_power_supply_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return true;
	default:
		return false;
	}
}

static int bq24257_get_chip_state(struct bq24257_device *bq,
				  struct bq24257_state *state)
{
	int ret;

	ret = bq24257_field_read(bq, F_STAT);
	if (ret < 0)
		return ret;

	state->status = ret;

	ret = bq24257_field_read(bq, F_FAULT);
	if (ret < 0)
		return ret;

	state->fault = ret;

	if (bq->pg)
		state->power_good = !gpiod_get_value_cansleep(bq->pg);
	else
		/*
		 * If we have a chip without a dedicated power-good GPIO or
		 * some other explicit bit that would provide this information
		 * assume the power is good if there is no supply related
		 * fault - and not good otherwise. There is a possibility for
		 * other errors to mask that power in fact is not good but this
		 * is probably the best we can do here.
		 */
		switch (state->fault) {
		case FAULT_INPUT_OVP:
		case FAULT_INPUT_UVLO:
		case FAULT_INPUT_LDO_LOW:
			state->power_good = false;
			break;
		default:
			state->power_good = true;
		}

	return 0;
}

static bool bq24257_state_changed(struct bq24257_device *bq,
				  struct bq24257_state *new_state)
{
	int ret;

	mutex_lock(&bq->lock);
	ret = (bq->state.status != new_state->status ||
	       bq->state.fault != new_state->fault ||
	       bq->state.power_good != new_state->power_good);
	mutex_unlock(&bq->lock);

	return ret;
}

enum bq24257_loop_status {
	LOOP_STATUS_NONE,
	LOOP_STATUS_IN_DPM,
	LOOP_STATUS_IN_CURRENT_LIMIT,
	LOOP_STATUS_THERMAL,
};

enum bq24257_in_ilimit {
	IILIMIT_100,
	IILIMIT_150,
	IILIMIT_500,
	IILIMIT_900,
	IILIMIT_1500,
	IILIMIT_2000,
	IILIMIT_EXT,
	IILIMIT_NONE,
};

enum bq24257_vovp {
	VOVP_6000,
	VOVP_6500,
	VOVP_7000,
	VOVP_8000,
	VOVP_9000,
	VOVP_9500,
	VOVP_10000,
	VOVP_10500
};

enum bq24257_vindpm {
	VINDPM_4200,
	VINDPM_4280,
	VINDPM_4360,
	VINDPM_4440,
	VINDPM_4520,
	VINDPM_4600,
	VINDPM_4680,
	VINDPM_4760
};

enum bq24257_port_type {
	PORT_TYPE_DCP,		/* Dedicated Charging Port */
	PORT_TYPE_CDP,		/* Charging Downstream Port */
	PORT_TYPE_SDP,		/* Standard Downstream Port */
	PORT_TYPE_NON_STANDARD,
};

enum bq24257_safety_timer {
	SAFETY_TIMER_45,
	SAFETY_TIMER_360,
	SAFETY_TIMER_540,
	SAFETY_TIMER_NONE,
};

static int bq24257_iilimit_autoset(struct bq24257_device *bq)
{
	int loop_status;
	int iilimit;
	int port_type;
	int ret;
	const u8 new_iilimit[] = {
		[PORT_TYPE_DCP] = IILIMIT_2000,
		[PORT_TYPE_CDP] = IILIMIT_2000,
		[PORT_TYPE_SDP] = IILIMIT_500,
		[PORT_TYPE_NON_STANDARD] = IILIMIT_500
	};

	ret = bq24257_field_read(bq, F_LOOP_STATUS);
	if (ret < 0)
		goto error;

	loop_status = ret;

	ret = bq24257_field_read(bq, F_IILIMIT);
	if (ret < 0)
		goto error;

	iilimit = ret;

	/*
	 * All USB ports should be able to handle 500mA. If not, DPM will lower
	 * the charging current to accommodate the power source. No need to set
	 * a lower IILIMIT value.
	 */
	if (loop_status == LOOP_STATUS_IN_DPM && iilimit == IILIMIT_500)
		return 0;

	ret = bq24257_field_read(bq, F_USB_DET);
	if (ret < 0)
		goto error;

	port_type = ret;

	ret = bq24257_field_write(bq, F_IILIMIT, new_iilimit[port_type]);
	if (ret < 0)
		goto error;

	ret = bq24257_field_write(bq, F_TMR, SAFETY_TIMER_360);
	if (ret < 0)
		goto error;

	ret = bq24257_field_write(bq, F_CLR_VDP, 1);
	if (ret < 0)
		goto error;

	dev_dbg(bq->dev, "port/loop = %d/%d -> iilimit = %d\n",
		port_type, loop_status, new_iilimit[port_type]);

	return 0;

error:
	dev_err(bq->dev, "%s: Error communicating with the chip.\n", __func__);
	return ret;
}

static void bq24257_iilimit_setup_work(struct work_struct *work)
{
	struct bq24257_device *bq = container_of(work, struct bq24257_device,
						 iilimit_setup_work.work);

	bq24257_iilimit_autoset(bq);
}

static void bq24257_handle_state_change(struct bq24257_device *bq,
					struct bq24257_state *new_state)
{
	int ret;
	struct bq24257_state old_state;

	mutex_lock(&bq->lock);
	old_state = bq->state;
	mutex_unlock(&bq->lock);

	/*
	 * Handle BQ2425x state changes observing whether the D+/D- based input
	 * current limit autoset functionality is enabled.
	 */
	if (!new_state->power_good) {
		dev_dbg(bq->dev, "Power removed\n");
		if (bq->iilimit_autoset_enable) {
			cancel_delayed_work_sync(&bq->iilimit_setup_work);

			/* activate D+/D- port detection algorithm */
			ret = bq24257_field_write(bq, F_DPDM_EN, 1);
			if (ret < 0)
				goto error;
		}
		/*
		 * When power is removed always return to the default input
		 * current limit as configured during probe.
		 */
		ret = bq24257_field_write(bq, F_IILIMIT, bq->init_data.iilimit);
		if (ret < 0)
			goto error;
	} else if (!old_state.power_good) {
		dev_dbg(bq->dev, "Power inserted\n");

		if (bq->iilimit_autoset_enable)
			/* configure input current limit */
			schedule_delayed_work(&bq->iilimit_setup_work,
				      msecs_to_jiffies(BQ24257_ILIM_SET_DELAY));
	} else if (new_state->fault == FAULT_NO_BAT) {
		dev_warn(bq->dev, "Battery removed\n");
	} else if (new_state->fault == FAULT_TIMER) {
		dev_err(bq->dev, "Safety timer expired! Battery dead?\n");
	}

	return;

error:
	dev_err(bq->dev, "%s: Error communicating with the chip.\n", __func__);
}

static irqreturn_t bq24257_irq_handler_thread(int irq, void *private)
{
	int ret;
	struct bq24257_device *bq = private;
	struct bq24257_state state;

	ret = bq24257_get_chip_state(bq, &state);
	if (ret < 0)
		return IRQ_HANDLED;

	if (!bq24257_state_changed(bq, &state))
		return IRQ_HANDLED;

	dev_dbg(bq->dev, "irq(state changed): status/fault/pg = %d/%d/%d\n",
		state.status, state.fault, state.power_good);

	bq24257_handle_state_change(bq, &state);

	mutex_lock(&bq->lock);
	bq->state = state;
	mutex_unlock(&bq->lock);

	power_supply_changed(bq->charger);

	return IRQ_HANDLED;
}

static int bq24257_hw_init(struct bq24257_device *bq)
{
	int ret;
	int i;
	struct bq24257_state state;

	const struct {
		int field;
		u32 value;
	} init_data[] = {
		{F_ICHG, bq->init_data.ichg},
		{F_VBAT, bq->init_data.vbat},
		{F_ITERM, bq->init_data.iterm},
		{F_VOVP, bq->init_data.vovp},
		{F_VINDPM, bq->init_data.vindpm},
	};

	/*
	 * Disable the watchdog timer to prevent the IC from going back to
	 * default settings after 50 seconds of I2C inactivity.
	 */
	ret = bq24257_field_write(bq, F_WD_EN, 0);
	if (ret < 0)
		return ret;

	/* configure the charge currents and voltages */
	for (i = 0; i < ARRAY_SIZE(init_data); i++) {
		ret = bq24257_field_write(bq, init_data[i].field,
					  init_data[i].value);
		if (ret < 0)
			return ret;
	}

	ret = bq24257_get_chip_state(bq, &state);
	if (ret < 0)
		return ret;

	mutex_lock(&bq->lock);
	bq->state = state;
	mutex_unlock(&bq->lock);

	if (!bq->iilimit_autoset_enable) {
		dev_dbg(bq->dev, "manually setting iilimit = %u\n",
			bq->init_data.iilimit);

		/* program fixed input current limit */
		ret = bq24257_field_write(bq, F_IILIMIT,
					  bq->init_data.iilimit);
		if (ret < 0)
			return ret;
	} else if (!state.power_good)
		/* activate D+/D- detection algorithm */
		ret = bq24257_field_write(bq, F_DPDM_EN, 1);
	else if (state.fault != FAULT_NO_BAT)
		ret = bq24257_iilimit_autoset(bq);

	return ret;
}

static enum power_supply_property bq24257_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static char *bq24257_charger_supplied_to[] = {
	"main-battery",
};

static const struct power_supply_desc bq24257_power_supply_desc = {
	.name = "bq24257-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = bq24257_power_supply_props,
	.num_properties = ARRAY_SIZE(bq24257_power_supply_props),
	.get_property = bq24257_power_supply_get_property,
	.set_property = bq24257_power_supply_set_property,
	.property_is_writeable = bq24257_power_supply_property_is_writeable,
};

static ssize_t bq24257_show_ovp_voltage(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24257_device *bq = power_supply_get_drvdata(psy);

	return sysfs_emit(buf, "%u\n", bq24257_vovp_map[bq->init_data.vovp]);
}

static ssize_t bq24257_show_in_dpm_voltage(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24257_device *bq = power_supply_get_drvdata(psy);

	return sysfs_emit(buf, "%u\n", bq24257_vindpm_map[bq->init_data.vindpm]);
}

static ssize_t bq24257_sysfs_show_enable(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24257_device *bq = power_supply_get_drvdata(psy);
	int ret;

	if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		ret = bq24257_field_read(bq, F_HZ_MODE);
	else if (strcmp(attr->attr.name, "sysoff_enable") == 0)
		ret = bq24257_field_read(bq, F_SYSOFF);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", ret);
}

static ssize_t bq24257_sysfs_set_enable(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24257_device *bq = power_supply_get_drvdata(psy);
	long val;
	int ret;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		ret = bq24257_field_write(bq, F_HZ_MODE, (bool)val);
	else if (strcmp(attr->attr.name, "sysoff_enable") == 0)
		ret = bq24257_field_write(bq, F_SYSOFF, (bool)val);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(ovp_voltage, S_IRUGO, bq24257_show_ovp_voltage, NULL);
static DEVICE_ATTR(in_dpm_voltage, S_IRUGO, bq24257_show_in_dpm_voltage, NULL);
static DEVICE_ATTR(high_impedance_enable, S_IWUSR | S_IRUGO,
		   bq24257_sysfs_show_enable, bq24257_sysfs_set_enable);
static DEVICE_ATTR(sysoff_enable, S_IWUSR | S_IRUGO,
		   bq24257_sysfs_show_enable, bq24257_sysfs_set_enable);

static struct attribute *bq24257_charger_sysfs_attrs[] = {
	&dev_attr_ovp_voltage.attr,
	&dev_attr_in_dpm_voltage.attr,
	&dev_attr_high_impedance_enable.attr,
	&dev_attr_sysoff_enable.attr,
	NULL,
};

ATTRIBUTE_GROUPS(bq24257_charger_sysfs);

static int bq24257_power_supply_init(struct bq24257_device *bq)
{
	struct power_supply_config psy_cfg = { .drv_data = bq, };

	psy_cfg.attr_grp = bq24257_charger_sysfs_groups;
	psy_cfg.supplied_to = bq24257_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(bq24257_charger_supplied_to);

	bq->charger = devm_power_supply_register(bq->dev,
						 &bq24257_power_supply_desc,
						 &psy_cfg);

	return PTR_ERR_OR_ZERO(bq->charger);
}

static void bq24257_pg_gpio_probe(struct bq24257_device *bq)
{
	bq->pg = devm_gpiod_get_optional(bq->dev, BQ24257_PG_GPIO, GPIOD_IN);

	if (PTR_ERR(bq->pg) == -EPROBE_DEFER) {
		dev_info(bq->dev, "probe retry requested for PG pin\n");
		return;
	} else if (IS_ERR(bq->pg)) {
		dev_err(bq->dev, "error probing PG pin\n");
		bq->pg = NULL;
		return;
	}

	if (bq->pg)
		dev_dbg(bq->dev, "probed PG pin = %d\n", desc_to_gpio(bq->pg));
}

static int bq24257_fw_probe(struct bq24257_device *bq)
{
	int ret;
	u32 property;

	/* Required properties */
	ret = device_property_read_u32(bq->dev, "ti,charge-current", &property);
	if (ret < 0)
		return ret;

	bq->init_data.ichg = bq24257_find_idx(property, bq24257_ichg_map,
					      BQ24257_ICHG_MAP_SIZE);

	ret = device_property_read_u32(bq->dev, "ti,battery-regulation-voltage",
				       &property);
	if (ret < 0)
		return ret;

	bq->init_data.vbat = bq24257_find_idx(property, bq24257_vbat_map,
					      BQ24257_VBAT_MAP_SIZE);

	ret = device_property_read_u32(bq->dev, "ti,termination-current",
				       &property);
	if (ret < 0)
		return ret;

	bq->init_data.iterm = bq24257_find_idx(property, bq24257_iterm_map,
					       BQ24257_ITERM_MAP_SIZE);

	/* Optional properties. If not provided use reasonable default. */
	ret = device_property_read_u32(bq->dev, "ti,current-limit",
				       &property);
	if (ret < 0) {
		bq->iilimit_autoset_enable = true;

		/*
		 * Explicitly set a default value which will be needed for
		 * devices that don't support the automatic setting of the input
		 * current limit through the charger type detection mechanism.
		 */
		bq->init_data.iilimit = IILIMIT_500;
	} else
		bq->init_data.iilimit =
				bq24257_find_idx(property,
						 bq24257_iilimit_map,
						 BQ24257_IILIMIT_MAP_SIZE);

	ret = device_property_read_u32(bq->dev, "ti,ovp-voltage",
				       &property);
	if (ret < 0)
		bq->init_data.vovp = VOVP_6500;
	else
		bq->init_data.vovp = bq24257_find_idx(property,
						      bq24257_vovp_map,
						      BQ24257_VOVP_MAP_SIZE);

	ret = device_property_read_u32(bq->dev, "ti,in-dpm-voltage",
				       &property);
	if (ret < 0)
		bq->init_data.vindpm = VINDPM_4360;
	else
		bq->init_data.vindpm =
				bq24257_find_idx(property,
						 bq24257_vindpm_map,
						 BQ24257_VINDPM_MAP_SIZE);

	return 0;
}

static int bq24257_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	const struct acpi_device_id *acpi_id;
	struct bq24257_device *bq;
	int ret;
	int i;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	bq = devm_kzalloc(dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->client = client;
	bq->dev = dev;

	if (ACPI_HANDLE(dev)) {
		acpi_id = acpi_match_device(dev->driver->acpi_match_table,
					    &client->dev);
		if (!acpi_id) {
			dev_err(dev, "Failed to match ACPI device\n");
			return -ENODEV;
		}
		bq->chip = (enum bq2425x_chip)acpi_id->driver_data;
	} else {
		bq->chip = (enum bq2425x_chip)id->driver_data;
	}

	mutex_init(&bq->lock);

	bq->rmap = devm_regmap_init_i2c(client, &bq24257_regmap_config);
	if (IS_ERR(bq->rmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(bq->rmap);
	}

	for (i = 0; i < ARRAY_SIZE(bq24257_reg_fields); i++) {
		const struct reg_field *reg_fields = bq24257_reg_fields;

		bq->rmap_fields[i] = devm_regmap_field_alloc(dev, bq->rmap,
							     reg_fields[i]);
		if (IS_ERR(bq->rmap_fields[i])) {
			dev_err(dev, "cannot allocate regmap field\n");
			return PTR_ERR(bq->rmap_fields[i]);
		}
	}

	i2c_set_clientdata(client, bq);

	if (!dev->platform_data) {
		ret = bq24257_fw_probe(bq);
		if (ret < 0) {
			dev_err(dev, "Cannot read device properties.\n");
			return ret;
		}
	} else {
		return -ENODEV;
	}

	/*
	 * The BQ24250 doesn't support the D+/D- based charger type detection
	 * used for the automatic setting of the input current limit setting so
	 * explicitly disable that feature.
	 */
	if (bq->chip == BQ24250)
		bq->iilimit_autoset_enable = false;

	if (bq->iilimit_autoset_enable)
		INIT_DELAYED_WORK(&bq->iilimit_setup_work,
				  bq24257_iilimit_setup_work);

	/*
	 * The BQ24250 doesn't have a dedicated Power Good (PG) pin so let's
	 * not probe for it and instead use a SW-based approach to determine
	 * the PG state. We also use a SW-based approach for all other devices
	 * if the PG pin is either not defined or can't be probed.
	 */
	if (bq->chip != BQ24250)
		bq24257_pg_gpio_probe(bq);

	if (PTR_ERR(bq->pg) == -EPROBE_DEFER)
		return PTR_ERR(bq->pg);
	else if (!bq->pg)
		dev_info(bq->dev, "using SW-based power-good detection\n");

	/* reset all registers to defaults */
	ret = bq24257_field_write(bq, F_RESET, 1);
	if (ret < 0)
		return ret;

	/*
	 * Put the RESET bit back to 0, in cache. For some reason the HW always
	 * returns 1 on this bit, so this is the only way to avoid resetting the
	 * chip every time we update another field in this register.
	 */
	ret = bq24257_field_write(bq, F_RESET, 0);
	if (ret < 0)
		return ret;

	ret = bq24257_hw_init(bq);
	if (ret < 0) {
		dev_err(dev, "Cannot initialize the chip.\n");
		return ret;
	}

	ret = bq24257_power_supply_init(bq);
	if (ret < 0) {
		dev_err(dev, "Failed to register power supply\n");
		return ret;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					bq24257_irq_handler_thread,
					IRQF_TRIGGER_FALLING |
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					bq2425x_chip_name[bq->chip], bq);
	if (ret) {
		dev_err(dev, "Failed to request IRQ #%d\n", client->irq);
		return ret;
	}

	return 0;
}

static void bq24257_remove(struct i2c_client *client)
{
	struct bq24257_device *bq = i2c_get_clientdata(client);

	if (bq->iilimit_autoset_enable)
		cancel_delayed_work_sync(&bq->iilimit_setup_work);

	bq24257_field_write(bq, F_RESET, 1); /* reset to defaults */
}

#ifdef CONFIG_PM_SLEEP
static int bq24257_suspend(struct device *dev)
{
	struct bq24257_device *bq = dev_get_drvdata(dev);
	int ret = 0;

	if (bq->iilimit_autoset_enable)
		cancel_delayed_work_sync(&bq->iilimit_setup_work);

	/* reset all registers to default (and activate standalone mode) */
	ret = bq24257_field_write(bq, F_RESET, 1);
	if (ret < 0)
		dev_err(bq->dev, "Cannot reset chip to standalone mode.\n");

	return ret;
}

static int bq24257_resume(struct device *dev)
{
	int ret;
	struct bq24257_device *bq = dev_get_drvdata(dev);

	ret = regcache_drop_region(bq->rmap, BQ24257_REG_1, BQ24257_REG_7);
	if (ret < 0)
		return ret;

	ret = bq24257_field_write(bq, F_RESET, 0);
	if (ret < 0)
		return ret;

	ret = bq24257_hw_init(bq);
	if (ret < 0) {
		dev_err(bq->dev, "Cannot init chip after resume.\n");
		return ret;
	}

	/* signal userspace, maybe state changed while suspended */
	power_supply_changed(bq->charger);

	return 0;
}
#endif

static const struct dev_pm_ops bq24257_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(bq24257_suspend, bq24257_resume)
};

static const struct i2c_device_id bq24257_i2c_ids[] = {
	{ "bq24250", BQ24250 },
	{ "bq24251", BQ24251 },
	{ "bq24257", BQ24257 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq24257_i2c_ids);

static const struct of_device_id bq24257_of_match[] __maybe_unused = {
	{ .compatible = "ti,bq24250", },
	{ .compatible = "ti,bq24251", },
	{ .compatible = "ti,bq24257", },
	{ },
};
MODULE_DEVICE_TABLE(of, bq24257_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id bq24257_acpi_match[] = {
	{ "BQ242500", BQ24250 },
	{ "BQ242510", BQ24251 },
	{ "BQ242570", BQ24257 },
	{},
};
MODULE_DEVICE_TABLE(acpi, bq24257_acpi_match);
#endif

static struct i2c_driver bq24257_driver = {
	.driver = {
		.name = "bq24257-charger",
		.of_match_table = of_match_ptr(bq24257_of_match),
		.acpi_match_table = ACPI_PTR(bq24257_acpi_match),
		.pm = &bq24257_pm,
	},
	.probe_new = bq24257_probe,
	.remove = bq24257_remove,
	.id_table = bq24257_i2c_ids,
};
module_i2c_driver(bq24257_driver);

MODULE_AUTHOR("Laurentiu Palcu <laurentiu.palcu@intel.com>");
MODULE_DESCRIPTION("bq24257 charger driver");
MODULE_LICENSE("GPL");
