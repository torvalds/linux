// SPDX-License-Identifier: GPL-2.0
/*
 * Chrager driver for Sc89890
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/types.h>

/* Module parameters. */
static int debug;
module_param_named(debug, debug, int, 0644);
MODULE_PARM_DESC(debug, "Set to one to enable debugging messages.");

#define DBG(args...) \
	do { \
		if (debug) { \
			pr_info(args); \
		} \
	} while (0)

#define SC89890_MANUFACTURER		"SOUTHCHIP"
#define SC89890_IRQ			"sc89890_irq"
#define SC89890_ID			4
#define SC89890_DEBUG_BUF_LEN		30
enum sc89890_fields {
	F_EN_HIZ, F_EN_ILIM, F_IILIM,				     /* Reg00 */
	F_BHOT, F_BCOLD, F_VINDPM_OFS,				     /* Reg01 */
	F_CONV_START, F_CONV_RATE, F_BOOSTF, F_ICO_EN,
	F_HVDCP_EN, F_MAXC_EN, F_FORCE_DPM, F_AUTO_DPDM_EN,	     /* Reg02 */
	F_BAT_LOAD_EN, F_WD_RST, F_OTG_CFG, F_CHG_CFG, F_SYSVMIN,
	F_MIN_VBAT_SEL,						     /* Reg03 */
	F_PUMPX_EN, F_ICHG,					     /* Reg04 */
	F_IPRECHG, F_ITERM,					     /* Reg05 */
	F_VREG, F_BATLOWV, F_VRECHG,				     /* Reg06 */
	F_TERM_EN, F_STAT_DIS, F_WD, F_TMR_EN, F_CHG_TMR,
	F_JEITA_ISET,						     /* Reg07 */
	F_BATCMP, F_VCLAMP, F_TREG,				     /* Reg08 */
	F_FORCE_ICO, F_TMR2X_EN, F_BATFET_DIS, F_JEITA_VSET,
	F_BATFET_DLY, F_BATFET_RST_EN, F_PUMPX_UP, F_PUMPX_DN,	     /* Reg09 */
	F_BOOSTV, F_PFM_OTG_DIS, F_BOOSTI,			     /* Reg0A */
	F_VBUS_STAT, F_CHG_STAT, F_PG_STAT, F_SDP_STAT, F_0B_RSVD,
	F_VSYS_STAT,						     /* Reg0B */
	F_WD_FAULT, F_BOOST_FAULT, F_CHG_FAULT, F_BAT_FAULT,
	F_NTC_FAULT,						     /* Reg0C */
	F_FORCE_VINDPM, F_VINDPM,				     /* Reg0D */
	F_THERM_STAT, F_BATV,					     /* Reg0E */
	F_SYSV,							     /* Reg0F */
	F_TSPCT,						     /* Reg10 */
	F_VBUS_GD, F_VBUSV,					     /* Reg11 */
	F_ICHGR,						     /* Reg12 */
	F_VDPM_STAT, F_IDPM_STAT, F_IDPM_LIM,			     /* Reg13 */
	F_REG_RST, F_ICO_OPTIMIZED, F_PN, F_TS_PROFILE, F_DEV_REV,   /* Reg14 */

	F_MAX_FIELDS
};

/* initial field values, converted to register values */
struct sc89890_init_data {
	u8 ichg;	/* charge current		*/
	u8 vreg;	/* regulation voltage		*/
	u8 iterm;	/* termination current		*/
	u8 iprechg;	/* precharge current		*/
	u8 sysvmin;	/* minimum system voltage limit */
	u8 boostv;	/* boost regulation voltage	*/
	u8 boosti;	/* boost current limit		*/
	u8 boostf;	/* boost frequency		*/
	u8 ilim_en;	/* enable ILIM pin		*/
	u8 treg;	/* thermal regulation threshold */
	u8 rbatcomp;	/* IBAT sense resistor value    */
	u8 vclamp;	/* IBAT compensation voltage limit */
};

struct sc89890_state {
	u8 online;
	u8 chrg_status;
	u8 chrg_fault;
	u8 vsys_status;
	u8 boost_fault;
	u8 bat_fault;
};

struct sc89890_device {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *charger;

	struct regulator_dev *otg_vbus_reg;
	unsigned long usb_event;

	struct regmap *rmap;
	struct regmap_field *rmap_fields[F_MAX_FIELDS];

	struct sc89890_init_data init_data;
	struct sc89890_state state;

	struct mutex lock; /* protect state data */
};

static const struct regmap_range sc89890_readonly_reg_ranges[] = {
	regmap_reg_range(0x0b, 0x0c),
	regmap_reg_range(0x0e, 0x13),
};

static const struct regmap_access_table sc89890_writeable_regs = {
	.no_ranges = sc89890_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(sc89890_readonly_reg_ranges),
};

static const struct regmap_range sc89890_volatile_reg_ranges[] = {
	regmap_reg_range(0x00, 0x00),
	regmap_reg_range(0x02, 0x02),
	regmap_reg_range(0x09, 0x09),
	regmap_reg_range(0x0b, 0x0b),
	regmap_reg_range(0x0c, 0x0c),
	regmap_reg_range(0x0d, 0x14),
};

static const struct regmap_access_table sc89890_volatile_regs = {
	.yes_ranges = sc89890_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sc89890_volatile_reg_ranges),
};

static const struct regmap_config sc89890_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x14,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &sc89890_writeable_regs,
	.volatile_table = &sc89890_volatile_regs,
};

static const struct reg_field sc89890_reg_fields[] = {
	/* REG00 */
	[F_EN_HIZ]		= REG_FIELD(0x00, 7, 7),
	[F_EN_ILIM]		= REG_FIELD(0x00, 6, 6),
	[F_IILIM]		= REG_FIELD(0x00, 0, 5),
	/* REG01 */
	[F_BHOT]		= REG_FIELD(0x01, 6, 7),
	[F_BCOLD]		= REG_FIELD(0x01, 5, 5),
	[F_VINDPM_OFS]		= REG_FIELD(0x01, 0, 4),
	/* REG02 */
	[F_CONV_START]		= REG_FIELD(0x02, 7, 7),
	[F_CONV_RATE]		= REG_FIELD(0x02, 6, 6),
	[F_BOOSTF]		= REG_FIELD(0x02, 5, 5),
	[F_ICO_EN]		= REG_FIELD(0x02, 4, 4),
	[F_HVDCP_EN]		= REG_FIELD(0x02, 3, 3),
	[F_MAXC_EN]		= REG_FIELD(0x02, 2, 2),
	[F_FORCE_DPM]		= REG_FIELD(0x02, 1, 1),
	[F_AUTO_DPDM_EN]	= REG_FIELD(0x02, 0, 0),
	/* REG03 */
	[F_BAT_LOAD_EN]		= REG_FIELD(0x03, 7, 7),
	[F_WD_RST]		= REG_FIELD(0x03, 6, 6),
	[F_OTG_CFG]		= REG_FIELD(0x03, 5, 5),
	[F_CHG_CFG]		= REG_FIELD(0x03, 4, 4),
	[F_SYSVMIN]		= REG_FIELD(0x03, 1, 3),
	[F_MIN_VBAT_SEL]	= REG_FIELD(0x03, 0, 0),
	/* REG04 */
	[F_PUMPX_EN]		= REG_FIELD(0x04, 7, 7),
	[F_ICHG]		= REG_FIELD(0x04, 0, 6),
	/* REG05 */
	[F_IPRECHG]		= REG_FIELD(0x05, 4, 7),
	[F_ITERM]		= REG_FIELD(0x05, 0, 3),
	/* REG06 */
	[F_VREG]		= REG_FIELD(0x06, 2, 7),
	[F_BATLOWV]		= REG_FIELD(0x06, 1, 1),
	[F_VRECHG]		= REG_FIELD(0x06, 0, 0),
	/* REG07 */
	[F_TERM_EN]		= REG_FIELD(0x07, 7, 7),
	[F_STAT_DIS]		= REG_FIELD(0x07, 6, 6),
	[F_WD]			= REG_FIELD(0x07, 4, 5),
	[F_TMR_EN]		= REG_FIELD(0x07, 3, 3),
	[F_CHG_TMR]		= REG_FIELD(0x07, 1, 2),
	[F_JEITA_ISET]		= REG_FIELD(0x07, 0, 0),
	/* REG08 */
	[F_BATCMP]		= REG_FIELD(0x08, 5, 7),
	[F_VCLAMP]		= REG_FIELD(0x08, 2, 4),
	[F_TREG]		= REG_FIELD(0x08, 0, 1),
	/* REG09 */
	[F_FORCE_ICO]		= REG_FIELD(0x09, 7, 7),
	[F_TMR2X_EN]		= REG_FIELD(0x09, 6, 6),
	[F_BATFET_DIS]		= REG_FIELD(0x09, 5, 5),
	[F_JEITA_VSET]		= REG_FIELD(0x09, 4, 4),
	[F_BATFET_DLY]		= REG_FIELD(0x09, 3, 3),
	[F_BATFET_RST_EN]	= REG_FIELD(0x09, 2, 2),
	[F_PUMPX_UP]		= REG_FIELD(0x09, 1, 1),
	[F_PUMPX_DN]		= REG_FIELD(0x09, 0, 0),
	/* REG0A */
	[F_BOOSTV]		= REG_FIELD(0x0A, 4, 7),
	[F_BOOSTI]		= REG_FIELD(0x0A, 0, 2),
	[F_PFM_OTG_DIS]		= REG_FIELD(0x0A, 3, 3),
	/* REG0B */
	[F_VBUS_STAT]		= REG_FIELD(0x0B, 5, 7),
	[F_CHG_STAT]		= REG_FIELD(0x0B, 3, 4),
	[F_PG_STAT]		= REG_FIELD(0x0B, 2, 2),
	[F_SDP_STAT]		= REG_FIELD(0x0B, 1, 1),
	[F_VSYS_STAT]		= REG_FIELD(0x0B, 0, 0),
	/* REG0C */
	[F_WD_FAULT]		= REG_FIELD(0x0C, 7, 7),
	[F_BOOST_FAULT]		= REG_FIELD(0x0C, 6, 6),
	[F_CHG_FAULT]		= REG_FIELD(0x0C, 4, 5),
	[F_BAT_FAULT]		= REG_FIELD(0x0C, 3, 3),
	[F_NTC_FAULT]		= REG_FIELD(0x0C, 0, 2),
	/* REG0D */
	[F_FORCE_VINDPM]	= REG_FIELD(0x0D, 7, 7),
	[F_VINDPM]		= REG_FIELD(0x0D, 0, 6),
	/* REG0E */
	[F_THERM_STAT]		= REG_FIELD(0x0E, 7, 7),
	[F_BATV]		= REG_FIELD(0x0E, 0, 6),
	/* REG0F */
	[F_SYSV]		= REG_FIELD(0x0F, 0, 6),
	/* REG10 */
	[F_TSPCT]		= REG_FIELD(0x10, 0, 6),
	/* REG11 */
	[F_VBUS_GD]		= REG_FIELD(0x11, 7, 7),
	[F_VBUSV]		= REG_FIELD(0x11, 0, 6),
	/* REG12 */
	[F_ICHGR]		= REG_FIELD(0x12, 0, 6),
	/* REG13 */
	[F_VDPM_STAT]		= REG_FIELD(0x13, 7, 7),
	[F_IDPM_STAT]		= REG_FIELD(0x13, 6, 6),
	[F_IDPM_LIM]		= REG_FIELD(0x13, 0, 5),
	/* REG14 */
	[F_REG_RST]		= REG_FIELD(0x14, 7, 7),
	[F_ICO_OPTIMIZED]	= REG_FIELD(0x14, 6, 6),
	[F_PN]			= REG_FIELD(0x14, 3, 5),
	[F_TS_PROFILE]		= REG_FIELD(0x14, 2, 2),
	[F_DEV_REV]		= REG_FIELD(0x14, 0, 1)
};

enum sc89890_status {
	STATUS_NOT_CHARGING,
	STATUS_PRE_CHARGING,
	STATUS_FAST_CHARGING,
	STATUS_TERMINATION_DONE,
};

enum sc89890_chrg_fault {
	CHRG_FAULT_NORMAL,
	CHRG_FAULT_INPUT,
	CHRG_FAULT_THERMAL_SHUTDOWN,
	CHRG_FAULT_TIMER_EXPIRED,
};

/*
 * Most of the val -> idx conversions can be computed, given the minimum,
 * maximum and the step between values. For the rest of conversions, we use
 * lookup tables.
 */
enum sc89890_table_ids {
	/* range tables */
	TBL_ICHG,
	TBL_ITERM,
	TBL_IILIM,
	TBL_VREG,
	TBL_BOOSTV,
	TBL_SYSVMIN,
	TBL_VBATCOMP,
	TBL_RBATCOMP,

	/* lookup tables */
	TBL_TREG,
	TBL_BOOSTI,
};

/* Thermal Regulation Threshold lookup table, in degrees Celsius */
static const u32 sc89890_treg_tbl[] = { 60, 80, 100, 120 };

#define SC89890_TREG_TBL_SIZE		ARRAY_SIZE(sc89890_treg_tbl)

/* Boost mode current limit lookup table, in uA */
static const u32 sc89890_boosti_tbl[] = {
	500000, 700000, 1100000, 1300000, 1600000, 1800000, 2100000, 2400000
};

#define SC89890_BOOSTI_TBL_SIZE		ARRAY_SIZE(sc89890_boosti_tbl)

struct sc89890_range {
	u32 min;
	u32 max;
	u32 step;
};

struct sc89890_lookup {
	const u32 *tbl;
	u32 size;
};

static const union {
	struct sc89890_range rt;
	struct sc89890_lookup lt;
} sc89890_tables[] = {
	/* range tables */
	[TBL_ICHG] = { .rt = {0, 5056000, 64000} }, /* uA */
	[TBL_ITERM] = { .rt = {64000, 1024000, 64000} }, /* uA */
	[TBL_IILIM] = { .rt = {100000, 3250000, 50000} }, /* uA */
	[TBL_VREG] = { .rt = {3840000, 4608000, 16000} }, /* uV */
	[TBL_BOOSTV] = { .rt = {4550000, 5510000, 64000} }, /* uV */
	[TBL_SYSVMIN] = { .rt = {3000000, 3700000, 100000} }, /* uV */
	[TBL_VBATCOMP] = { .rt = {0, 224000, 32000} }, /* uV */
	[TBL_RBATCOMP] = { .rt = {0, 140000, 20000} }, /* uOhm */

	/* lookup tables */
	[TBL_TREG] = { .lt = {sc89890_treg_tbl, SC89890_TREG_TBL_SIZE} },
	[TBL_BOOSTI] = { .lt = {sc89890_boosti_tbl, SC89890_BOOSTI_TBL_SIZE} }
};

static int sc89890_field_read(struct sc89890_device *sc89890,
			      enum sc89890_fields field_id)
{
	int ret;
	int val;

	ret = regmap_field_read(sc89890->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int sc89890_field_write(struct sc89890_device *sc89890,
			       enum sc89890_fields field_id, u8 val)
{
	return regmap_field_write(sc89890->rmap_fields[field_id], val);
}

static u8 sc89890_find_idx(u32 value, enum sc89890_table_ids id)
{
	u8 idx;

	if (id >= TBL_TREG) {
		const u32 *tbl = sc89890_tables[id].lt.tbl;
		u32 tbl_size = sc89890_tables[id].lt.size;

		for (idx = 1; idx < tbl_size && tbl[idx] <= value; idx++)
			;
	} else {
		const struct sc89890_range *rtbl = &sc89890_tables[id].rt;
		u8 rtbl_size;

		rtbl_size = (rtbl->max - rtbl->min) / rtbl->step + 1;

		for (idx = 1;
		     idx < rtbl_size && (idx * rtbl->step + rtbl->min <= value);
		     idx++)
			;
	}

	return idx - 1;
}

static u32 sc89890_find_val(u8 idx, enum sc89890_table_ids id)
{
	const struct sc89890_range *rtbl;

	/* lookup table? */
	if (id >= TBL_TREG)
		return sc89890_tables[id].lt.tbl[idx];

	/* range table */
	rtbl = &sc89890_tables[id].rt;

	return (rtbl->min + idx * rtbl->step);
}

static bool sc89890_is_adc_property(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return true;

	default:
		return false;
	}
}

static int sc89890_get_chip_state(struct sc89890_device *sc89890,
				  struct sc89890_state *state)
{
	int i, ret;

	struct {
		enum sc89890_fields id;
		u8 *data;
	} state_fields[] = {
		{F_CHG_STAT, &state->chrg_status},
		{F_PG_STAT, &state->online},
		{F_VSYS_STAT, &state->vsys_status},
		{F_BOOST_FAULT, &state->boost_fault},
		{F_BAT_FAULT, &state->bat_fault},
		{F_CHG_FAULT, &state->chrg_fault}
	};

	for (i = 0; i < ARRAY_SIZE(state_fields); i++) {
		ret = sc89890_field_read(sc89890, state_fields[i].id);
		if (ret < 0)
			return ret;

		*state_fields[i].data = ret;
	}

	DBG("SC89890: S:CHG/PG/VSYS=%d/%d/%d, F:CHG/BOOST/BAT=%d/%d/%d\n",
	    state->chrg_status, state->online, state->vsys_status,
	    state->chrg_fault, state->boost_fault, state->bat_fault);

	return 0;
}

static irqreturn_t __sc89890_handle_irq(struct sc89890_device *sc89890)
{
	struct sc89890_state new_state;
	int ret;

	ret = sc89890_get_chip_state(sc89890, &new_state);
	if (ret < 0)
		return IRQ_NONE;

	if (!memcmp(&sc89890->state, &new_state, sizeof(new_state)))
		return IRQ_NONE;

	if (!new_state.online && sc89890->state.online) {	/* power removed */
		/* disable ADC */
		ret = sc89890_field_write(sc89890, F_CONV_START, 0);
		if (ret < 0)
			goto error;
	} else if (new_state.online && !sc89890->state.online) { /* power inserted */

		/* enable ADC, to have control of charge current/voltage */
		ret = sc89890_field_write(sc89890, F_CONV_START, 1);
		if (ret < 0)
			goto error;
	}

	sc89890->state = new_state;
	power_supply_changed(sc89890->charger);

	return IRQ_HANDLED;
error:
	dev_err(sc89890->dev, "Error communicating with the chip: %pe\n",
		ERR_PTR(ret));
	return IRQ_HANDLED;
}

static int sc89890_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct sc89890_device *sc89890 = power_supply_get_drvdata(psy);
	struct sc89890_state state;
	bool do_adc_conv;
	int ret;

	mutex_lock(&sc89890->lock);
	/* update state in case we lost an interrupt */
	__sc89890_handle_irq(sc89890);
	state = sc89890->state;
	do_adc_conv = !state.online && sc89890_is_adc_property(psp);
	if (do_adc_conv)
		sc89890_field_write(sc89890, F_CONV_START, 1);
	mutex_unlock(&sc89890->lock);

	if (do_adc_conv)
		regmap_field_read_poll_timeout(sc89890->rmap_fields[F_CONV_START],
			ret, !ret, 25000, 1000000);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.online)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (state.chrg_status == STATUS_NOT_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (state.chrg_status == STATUS_PRE_CHARGING ||
			 state.chrg_status == STATUS_FAST_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (state.chrg_status == STATUS_TERMINATION_DONE)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (!state.online || state.chrg_status == STATUS_NOT_CHARGING ||
		    state.chrg_status == STATUS_TERMINATION_DONE)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		else if (state.chrg_status == STATUS_PRE_CHARGING)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		else if (state.chrg_status == STATUS_FAST_CHARGING)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else /* unreachable */
			val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = SC89890_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "SC89890";
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!state.chrg_status;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (!state.chrg_fault && !state.bat_fault && !state.boost_fault)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		else if (state.bat_fault)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (state.chrg_fault == CHRG_FAULT_TIMER_EXPIRED)
			val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		else if (state.chrg_fault == CHRG_FAULT_THERMAL_SHUTDOWN)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = sc89890_find_val(sc89890->init_data.ichg, TBL_ICHG);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (!state.online) {
			val->intval = 0;
			break;
		}

		ret = sc89890_field_read(sc89890, F_BATV); /* read measured value */
		if (ret < 0)
			return ret;

		/* converted_val = 2.304V + ADC_val * 20mV (table 10.3.15) */
		val->intval = 2304000 + ret * 20000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = sc89890_find_val(sc89890->init_data.vreg, TBL_VREG);
		break;

	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		val->intval = sc89890_find_val(sc89890->init_data.iprechg, TBL_ITERM);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		val->intval = sc89890_find_val(sc89890->init_data.iterm, TBL_ITERM);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = sc89890_field_read(sc89890, F_IILIM);
		if (ret < 0)
			return ret;

		val->intval = sc89890_find_val(ret, TBL_IILIM);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		val->intval = 13500000; /* uV */
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sc89890_field_read(sc89890, F_SYSV); /* read measured value */
		if (ret < 0)
			return ret;

		/* converted_val = 2.304V + ADC_val * 20mV (table 10.3.15) */
		val->intval = 2304000 + ret * 20000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sc89890_field_read(sc89890, F_ICHGR); /* read measured value */
		if (ret < 0)
			return ret;

		/* converted_val = ADC_val * 50mA (table 10.3.19) */
		val->intval = ret * -50000;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int sc89890_power_supply_set_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     const union power_supply_propval *val)
{
	struct sc89890_device *sc89890 = power_supply_get_drvdata(psy);
	int index, ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		index = sc89890_find_idx(val->intval, TBL_ICHG);
		ret = sc89890_field_write(sc89890, F_ICHG, index);
		if (ret < 0)
			dev_err(sc89890->dev, "set input voltage limit failed\n");
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		index = sc89890_find_idx(val->intval, TBL_IILIM);
		ret = sc89890_field_write(sc89890, F_IILIM, index);
		if (ret < 0)
			dev_err(sc89890->dev, "set input current limit failed\n");
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static irqreturn_t sc89890_irq_handler_thread(int irq, void *private)
{
	struct sc89890_device *sc89890 = private;
	irqreturn_t ret;

	mutex_lock(&sc89890->lock);
	ret = __sc89890_handle_irq(sc89890);
	mutex_unlock(&sc89890->lock);

	return ret;
}

static int sc89890_chip_reset(struct sc89890_device *sc89890)
{
	int ret;
	int rst_check_counter = 10;

	ret = sc89890_field_write(sc89890, F_REG_RST, 1);
	if (ret < 0)
		return ret;

	do {
		ret = sc89890_field_read(sc89890, F_REG_RST);
		if (ret < 0)
			return ret;

		usleep_range(5, 10);
	} while (ret == 1 && --rst_check_counter);

	if (!rst_check_counter)
		return -ETIMEDOUT;

	return 0;
}

static int sc89890_hw_init(struct sc89890_device *sc89890)
{
	int ret;
	int i;

	const struct {
		enum sc89890_fields id;
		u32 value;
	} init_data[] = {
		{F_ICHG, sc89890->init_data.ichg},
		{F_VREG, sc89890->init_data.vreg},
		{F_ITERM, sc89890->init_data.iterm},
		{F_IPRECHG, sc89890->init_data.iprechg},
		{F_SYSVMIN, sc89890->init_data.sysvmin},
		{F_BOOSTV, sc89890->init_data.boostv},
		{F_BOOSTI, sc89890->init_data.boosti},
		{F_BOOSTF, sc89890->init_data.boostf},
		{F_EN_ILIM, sc89890->init_data.ilim_en},
		{F_TREG, sc89890->init_data.treg},
		{F_BATCMP, sc89890->init_data.rbatcomp},
		{F_VCLAMP, sc89890->init_data.vclamp},
	};

	ret = sc89890_chip_reset(sc89890);
	if (ret < 0) {
		dev_dbg(sc89890->dev, "Reset failed %d\n", ret);
		return ret;
	}

	/* disable watchdog */
	ret = sc89890_field_write(sc89890, F_WD, 0);
	if (ret < 0) {
		dev_dbg(sc89890->dev, "Disabling watchdog failed %d\n", ret);
		return ret;
	}

	/* initialize currents/voltages and other parameters */
	for (i = 0; i < ARRAY_SIZE(init_data); i++) {
		ret = sc89890_field_write(sc89890, init_data[i].id,
					  init_data[i].value);
		if (ret < 0) {
			dev_dbg(sc89890->dev, "Writing init data failed %d\n", ret);
			return ret;
		}
	}

	/* Configure ADC for continuous conversions when charging */
	ret = sc89890_field_write(sc89890, F_CONV_RATE, !!sc89890->state.online);
	if (ret < 0) {
		dev_err(sc89890->dev, "Config ADC failed %d\n", ret);
		return ret;
	}

	ret = sc89890_field_write(sc89890, F_AUTO_DPDM_EN, 0);
	if (ret < 0) {
		dev_err(sc89890->dev, "Config F_AUTO_DPDM_EN failed %d\n", ret);
		return ret;
	}

	ret = sc89890_field_write(sc89890, F_HVDCP_EN, 0);
	if (ret < 0) {
		dev_err(sc89890->dev, "Config F_HVDCP_EN failed %d\n", ret);
		return ret;
	}

	ret = sc89890_get_chip_state(sc89890, &sc89890->state);
	if (ret < 0) {
		dev_err(sc89890->dev, "Get state failed %d\n", ret);
		return ret;
	}

	return 0;
}

static const enum power_supply_property sc89890_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static char *sc89890_charger_supplied_to[] = {
	"usb",
};

static const struct power_supply_desc sc89890_power_supply_desc = {
	.name = "sc89890-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = sc89890_power_supply_props,
	.num_properties = ARRAY_SIZE(sc89890_power_supply_props),
	.set_property = sc89890_power_supply_set_property,
	.get_property = sc89890_power_supply_get_property,
};

static int sc89890_power_supply_init(struct sc89890_device *sc89890)
{
	struct power_supply_config psy_cfg = { .drv_data = sc89890, };

	psy_cfg.of_node = sc89890->dev->of_node;
	psy_cfg.supplied_to = sc89890_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(sc89890_charger_supplied_to);

	sc89890->charger = devm_power_supply_register(sc89890->dev,
						      &sc89890_power_supply_desc,
						      &psy_cfg);

	if (PTR_ERR_OR_ZERO(sc89890->charger)) {
		dev_err(sc89890->dev, "failed to register power supply\n");
		return PTR_ERR(sc89890->charger);
	}

	return 0;
}

static int sc89890_get_chip_version(struct sc89890_device *sc89890)
{
	int id;

	id = sc89890_field_read(sc89890, F_PN);
	if (id < 0) {
		dev_err(sc89890->dev, "Cannot read chip ID.\n");
		return id;
	} else if (id != SC89890_ID) {
		dev_err(sc89890->dev, "Unknown chip ID %d\n", id);
		return -ENODEV;
	}

	DBG("charge IC: SC89890\n");

	return 0;
}

static void sc89890_set_otg_vbus(struct sc89890_device *sc, bool enable)
{
	sc89890_field_write(sc, F_OTG_CFG, enable);
}

static int sc89890_otg_vbus_enable(struct regulator_dev *dev)
{
	struct sc89890_device *sc = rdev_get_drvdata(dev);

	sc89890_set_otg_vbus(sc, true);

	return 0;
}

static int sc89890_otg_vbus_disable(struct regulator_dev *dev)
{
	struct sc89890_device *sc = rdev_get_drvdata(dev);

	sc89890_set_otg_vbus(sc, false);

	return 0;
}

static int sc89890_otg_vbus_is_enabled(struct regulator_dev *dev)
{
	struct sc89890_device *sc = rdev_get_drvdata(dev);
	u8 val;

	val = sc89890_field_read(sc, F_OTG_CFG);

	return val;
}

static const struct regulator_ops sc89890_otg_vbus_ops = {
	.enable = sc89890_otg_vbus_enable,
	.disable = sc89890_otg_vbus_disable,
	.is_enabled = sc89890_otg_vbus_is_enabled,
};

static const struct regulator_desc sc89890_otg_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.regulators_node = of_match_ptr("regulators"),
	.owner = THIS_MODULE,
	.ops = &sc89890_otg_vbus_ops,
	.type = REGULATOR_VOLTAGE,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int sc89890_register_otg_vbus_regulator(struct sc89890_device *sc)
{
	struct regulator_config config = { };
	struct device_node *np;

	np = of_get_child_by_name(sc->dev->of_node, "regulators");
	if (!np) {
		dev_warn(sc->dev, "cannot find regulators node\n");
		return -ENXIO;
	}

	config.dev = sc->dev;
	config.driver_data = sc;

	sc->otg_vbus_reg = devm_regulator_register(sc->dev,
						   &sc89890_otg_vbus_desc,
						   &config);
	if (IS_ERR(sc->otg_vbus_reg))
		return PTR_ERR(sc->otg_vbus_reg);

	return 0;
}

static ssize_t registers_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct sc89890_device *sc89890 = dev_get_drvdata(dev);
	u8 tmpbuf[SC89890_DEBUG_BUF_LEN];
	int idx = 0;
	u8 addr;
	int val;
	int len;
	int ret;

	sc89890_field_write(sc89890, F_CONV_START, 1);

	regmap_field_read_poll_timeout(sc89890->rmap_fields[F_CONV_START],
		ret, !ret, 25000, 1000000);

	for (addr = 0x0; addr <= 0x14; addr++) {
		ret = regmap_read(sc89890->rmap, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, SC89890_DEBUG_BUF_LEN,
					"Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	val = sc89890_find_val(sc89890->init_data.vreg, TBL_VREG);
	pr_info("CHARGE_VOLTAGE_MAX: %d\n", val / 1000);

	val = sc89890_find_val(sc89890->init_data.iprechg, TBL_ITERM);
	pr_info("PRECHARGE_CURRENT: %d\n", val / 1000);

	val = sc89890_find_val(sc89890->init_data.iterm, TBL_ITERM);
	pr_info("CHARGE_TERM_CURRENT: %d\n", val / 1000);

	ret = sc89890_field_read(sc89890, F_BATV); /* read measured value */
	if (ret)
		dev_err(dev, "read F_BAT error!\n");
	else {
		/* converted_val = 2.304V + ADC_val * 20mV (table 10.3.15) */
		val = 2304000 + ret * 20000;
		pr_info("charge voltage: %d\n", val / 1000);
	}

	ret = sc89890_field_read(sc89890, F_IILIM);
	if (ret)
		dev_err(dev, "read F_IILIM error!\n");
	else {
		val = sc89890_find_val(ret, TBL_IILIM);
		pr_info("INPUT_CURRENT_LIMIT: %d\n", val / 1000);
	}
	ret = sc89890_field_read(sc89890, F_SYSV); /* read measured value */
	if (ret)
		dev_err(dev, "read F_SYSV error!\n");
	else {
		/* converted_val = 2.304V + ADC_val * 20mV (table 10.3.15) */
		val = 2304000 + ret * 20000;
		pr_info("VOLTAGE_NOW: %d\n", val / 1000);
	}
	ret = sc89890_field_read(sc89890, F_ICHGR); /* read measured value */
	if (ret)
		dev_err(dev, "read F_ICHRG error!\n");
	else {
		/* converted_val = ADC_val * 50mA (table 10.3.19) */
		val = ret * -50000;
		pr_info("CURRENT_NOW: %d\n", val / 1000);
	}
	return idx;
}

static ssize_t registers_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct sc89890_device *sc89890 = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x14)
		regmap_write(sc89890->rmap, (unsigned char)reg, val);

	return count;
}

static DEVICE_ATTR_RW(registers);

static void sc89890_create_device_node(struct device *dev)
{
	device_create_file(dev, &dev_attr_registers);
}

static int sc89890_fw_read_u32_props(struct sc89890_device *sc89890)
{
	struct sc89890_init_data *init = &sc89890->init_data;
	u32 property;
	int ret;
	int i;
	struct {
		char *name;
		bool optional;
		enum sc89890_table_ids tbl_id;
		u8 *conv_data; /* holds converted value from given property */
	} props[] = {
		/* required properties */
		{"sc,charge-current", false, TBL_ICHG, &init->ichg},
		{"sc,battery-regulation-voltage", false, TBL_VREG, &init->vreg},
		{"sc,termination-current", false, TBL_ITERM, &init->iterm},
		{"sc,precharge-current", false, TBL_ITERM, &init->iprechg},
		{"sc,minimum-sys-voltage", false, TBL_SYSVMIN, &init->sysvmin},
		{"sc,boost-voltage", false, TBL_BOOSTV, &init->boostv},
		{"sc,boost-max-current", false, TBL_BOOSTI, &init->boosti},

		/* optional properties */
		{"sc,thermal-regulation-threshold", true, TBL_TREG, &init->treg},
		{"sc,ibatcomp-micro-ohms", true, TBL_RBATCOMP, &init->rbatcomp},
		{"sc,ibatcomp-clamp-microvolt", true, TBL_VBATCOMP, &init->vclamp},
	};

	/* initialize data for optional properties */
	init->treg = 3; /* 120 degrees Celsius */
	init->rbatcomp = 0;
	init->vclamp = 0; /* IBAT compensation disabled */

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = device_property_read_u32(sc89890->dev,
					       props[i].name,
					       &property);
		if (ret < 0) {
			if (props[i].optional)
				continue;

			dev_err(sc89890->dev, "Unable to read property %d %s\n", ret,
				props[i].name);

			return ret;
		}

		*props[i].conv_data = sc89890_find_idx(property,
						       props[i].tbl_id);
	}

	return 0;
}

static int sc89890_fw_probe(struct sc89890_device *sc89890)
{
	int ret;
	struct sc89890_init_data *init = &sc89890->init_data;

	ret = sc89890_fw_read_u32_props(sc89890);
	if (ret < 0)
		return ret;

	init->ilim_en = device_property_read_bool(sc89890->dev, "sc,use-ilim-pin");
	init->boostf = device_property_read_bool(sc89890->dev, "sc,boost-low-freq");

	return 0;
}

static int sc89890_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct sc89890_device *sc89890;
	int ret;
	int i;

	sc89890 = devm_kzalloc(dev, sizeof(*sc89890), GFP_KERNEL);
	if (!sc89890)
		return -ENOMEM;

	sc89890->client = client;
	sc89890->dev = dev;

	mutex_init(&sc89890->lock);
	sc89890->rmap = devm_regmap_init_i2c(client, &sc89890_regmap_config);
	if (IS_ERR(sc89890->rmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(sc89890->rmap);
	}

	for (i = 0; i < ARRAY_SIZE(sc89890_reg_fields); i++) {
		const struct reg_field *reg_fields = sc89890_reg_fields;

		sc89890->rmap_fields[i] = devm_regmap_field_alloc(dev,
								  sc89890->rmap,
								  reg_fields[i]);
		if (IS_ERR(sc89890->rmap_fields[i])) {
			dev_err(dev, "cannot allocate regmap field\n");
			return PTR_ERR(sc89890->rmap_fields[i]);
		}
	}

	i2c_set_clientdata(client, sc89890);

	ret = sc89890_get_chip_version(sc89890);
	if (ret) {
		dev_err(dev, "Cannot read chip ID or unknown chip.\n");
		return ret;
	}

	ret = sc89890_power_supply_init(sc89890);
	if (ret < 0) {
		dev_err(dev, "Failed to register power supply\n");
		goto irq_fail;
	}

	if (!dev->platform_data) {
		ret = sc89890_fw_probe(sc89890);
		if (ret < 0) {
			dev_err(dev, "Cannot read device properties.\n");
			return ret;
		}
	} else {
		return -ENODEV;
	}

	ret = sc89890_hw_init(sc89890);
	if (ret < 0) {
		dev_err(dev, "Cannot initialize the chip.\n");
		return ret;
	}


	if (client->irq < 0) {
		dev_err(dev, "No irq resource found.\n");
		return client->irq;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					sc89890_irq_handler_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					SC89890_IRQ, sc89890);
	if (ret)
		goto irq_fail;

	sc89890_register_otg_vbus_regulator(sc89890);
	sc89890_create_device_node(sc89890->dev);

	return 0;

irq_fail:

	return ret;
}

static int sc89890_remove(struct i2c_client *client)
{
	struct sc89890_device *sc89890 = i2c_get_clientdata(client);

	/* reset all registers to default values */
	sc89890_chip_reset(sc89890);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sc89890_suspend(struct device *dev)
{
	struct sc89890_device *sc89890 = dev_get_drvdata(dev);

	/*
	 * If charger is removed, while in suspend, make sure ADC is disabled
	 * since it consumes slightly more power.
	 */
	return sc89890_field_write(sc89890, F_CONV_RATE, 0);
}

static int sc89890_resume(struct device *dev)
{
	int ret;
	struct sc89890_device *sc89890 = dev_get_drvdata(dev);

	mutex_lock(&sc89890->lock);

	ret = sc89890_get_chip_state(sc89890, &sc89890->state);
	if (ret < 0)
		goto unlock;

	/* Re-enable ADC only if charger is plugged in. */
	if (sc89890->state.online) {
		ret = sc89890_field_write(sc89890, F_CONV_RATE, 1);
		if (ret < 0)
			goto unlock;
	}

	/* signal userspace, maybe state changed while suspended */
	power_supply_changed(sc89890->charger);

unlock:
	mutex_unlock(&sc89890->lock);

	return ret;
}
#endif

static const struct dev_pm_ops sc89890_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(sc89890_suspend, sc89890_resume)
};

static const struct i2c_device_id sc89890_i2c_ids[] = {
	{ "sc89890", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc89890_i2c_ids);

static const struct of_device_id sc89890_of_match[] = {
	{ .compatible = "sc,sc89890", },
	{ },
};
MODULE_DEVICE_TABLE(of, sc89890_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id sc89890_acpi_match[] = {
	{"SC898900", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, sc89890_acpi_match);
#endif

static struct i2c_driver sc89890_driver = {
	.driver = {
		.name = "sc89890-charger",
		.of_match_table = of_match_ptr(sc89890_of_match),
		.acpi_match_table = ACPI_PTR(sc89890_acpi_match),
		.pm = &sc89890_pm,
	},
	.probe = sc89890_probe,
	.remove = sc89890_remove,
	.id_table = sc89890_i2c_ids,
};
module_i2c_driver(sc89890_driver);

MODULE_AUTHOR("xsf<xsf@rock-chips.com>");
MODULE_DESCRIPTION("sc89890 charger driver");
MODULE_LICENSE("GPL");
