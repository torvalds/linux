// SPDX-License-Identifier: GPL-2.0-or-later
/* ROHM BD71815, BD71828 and BD71878 Charger driver */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rohm-bd71815.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/mfd/rohm-bd72720.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

/* common defines */
#define BD7182x_MASK_VBAT_U			0x1f
#define BD7182x_MASK_VDCIN_U			0x0f
#define BD7182x_MASK_IBAT_U			0x3f
#define BD7182x_MASK_CURDIR_DISCHG		0x80
#define BD7182x_MASK_CHG_STATE			0x7f
#define BD7182x_MASK_BAT_TEMP			0x07
#define BD7182x_MASK_DCIN_DET			BIT(0)
#define BD7182x_MASK_CONF_PON			BIT(0)
#define BD71815_MASK_CONF_XSTB			BIT(1)
#define BD7182x_MASK_BAT_STAT			0x3f
#define BD7182x_MASK_DCIN_STAT			0x07

#define BD7182x_MASK_WDT_AUTO			0x40
#define BD7182x_MASK_VBAT_ALM_LIMIT_U		0x01
#define BD7182x_MASK_CHG_EN			0x01

#define BD7182x_DCIN_COLLAPSE_DEFAULT		0x36

#define MAX_CURRENT_DEFAULT			890000		/* uA */
#define AC_NAME					"bd71828_ac"
#define BAT_NAME				"bd71828_bat"

#define BAT_OPEN	0x7

/*
 * VBAT Low voltage detection Threshold
 * 0x00D4*16mV = 212*0.016 = 3.392v
 */
#define VBAT_LOW_TH			0x00D4

struct pwr_regs {
	unsigned int vbat_avg;
	unsigned int ibat;
	unsigned int ibat_avg;
	unsigned int btemp_vth;
	unsigned int chg_state;
	unsigned int bat_temp;
	unsigned int dcin_stat;
	unsigned int dcin_online_mask;
	unsigned int dcin_collapse_limit;
	unsigned int chg_set1;
	unsigned int chg_en;
	unsigned int vbat_alm_limit_u;
	unsigned int conf;
	unsigned int vdcin;
	unsigned int vdcin_himask;
};

static const struct pwr_regs pwr_regs_bd71828 = {
	.vbat_avg = BD71828_REG_VBAT_U,
	.ibat = BD71828_REG_IBAT_U,
	.ibat_avg = BD71828_REG_IBAT_AVG_U,
	.btemp_vth = BD71828_REG_VM_BTMP_U,
	.chg_state = BD71828_REG_CHG_STATE,
	.bat_temp = BD71828_REG_BAT_TEMP,
	.dcin_stat = BD71828_REG_DCIN_STAT,
	.dcin_online_mask = BD7182x_MASK_DCIN_DET,
	.dcin_collapse_limit = BD71828_REG_DCIN_CLPS,
	.chg_set1 = BD71828_REG_CHG_SET1,
	.chg_en   = BD71828_REG_CHG_EN,
	.vbat_alm_limit_u = BD71828_REG_ALM_VBAT_LIMIT_U,
	.conf = BD71828_REG_CONF,
	.vdcin = BD71828_REG_VDCIN_U,
	.vdcin_himask = BD7182x_MASK_VDCIN_U,
};

static const struct pwr_regs pwr_regs_bd71815 = {
	.vbat_avg = BD71815_REG_VM_SA_VBAT_U,
	/* BD71815 does not have separate current and current avg */
	.ibat = BD71815_REG_CC_CURCD_U,
	.ibat_avg = BD71815_REG_CC_CURCD_U,

	.btemp_vth = BD71815_REG_VM_BTMP,
	.chg_state = BD71815_REG_CHG_STATE,
	.bat_temp = BD71815_REG_BAT_TEMP,
	.dcin_stat = BD71815_REG_DCIN_STAT,
	.dcin_online_mask = BD7182x_MASK_DCIN_DET,
	.dcin_collapse_limit = BD71815_REG_DCIN_CLPS,
	.chg_set1 = BD71815_REG_CHG_SET1,
	.chg_en   = BD71815_REG_CHG_SET1,
	.vbat_alm_limit_u = BD71815_REG_ALM_VBAT_TH_U,
	.conf = BD71815_REG_CONF,

	.vdcin = BD71815_REG_VM_DCIN_U,
	.vdcin_himask = BD7182x_MASK_VDCIN_U,
};

static struct pwr_regs pwr_regs_bd72720 = {
	.vbat_avg = BD72720_REG_VM_SA_VBAT_U,
	.ibat = BD72720_REG_CC_CURCD_U,
	.ibat_avg = BD72720_REG_CC_SA_CURCD_U,
	.btemp_vth = BD72720_REG_VM_BTMP_U,
	/*
	 * Note, state 0x40 IMP_CHK. not documented
	 * on other variants but was still handled in
	 * existing code. No memory traces as to why.
	 */
	.chg_state = BD72720_REG_CHG_STATE,
	.bat_temp = BD72720_REG_CHG_BAT_TEMP_STAT,
	.dcin_stat = BD72720_REG_INT_VBUS_SRC,
	.dcin_online_mask = BD72720_MASK_DCIN_DET,
	.dcin_collapse_limit = -1, /* Automatic. Setting not supported */
	.chg_set1 = BD72720_REG_CHG_SET_1,
	.chg_en = BD72720_REG_CHG_EN,
	/* 15mV note in data-sheet */
	.vbat_alm_limit_u = BD72720_REG_ALM_VBAT_TH_U,
	.conf = BD72720_REG_CONF, /* o XSTB, only PON. Seprate slave addr */
	.vdcin = BD72720_REG_VM_VBUS_U, /* 10 bits not 11 as with other ICs */
	.vdcin_himask = BD72720_MASK_VDCIN_U,
};

struct bd71828_power {
	struct regmap *regmap;
	enum rohm_chip_type chip_type;
	struct device *dev;
	struct power_supply *ac;
	struct power_supply *bat;

	const struct pwr_regs *regs;
	/* Reg val to uA */
	int curr_factor;
	int rsens;
	int (*get_temp)(struct bd71828_power *pwr, int *temp);
	int (*bat_inserted)(struct bd71828_power *pwr);
};

static int bd7182x_write16(struct bd71828_power *pwr, int reg, u16 val)
{
	__be16 tmp;

	tmp = cpu_to_be16(val);

	return regmap_bulk_write(pwr->regmap, reg, &tmp, sizeof(tmp));
}

static int bd7182x_read16_himask(struct bd71828_power *pwr, int reg, int himask,
				 u16 *val)
{
	struct regmap *regmap = pwr->regmap;
	int ret;
	__be16 rvals;
	u8 *tmp = (u8 *)&rvals;

	ret = regmap_bulk_read(regmap, reg, &rvals, sizeof(*val));
	if (!ret) {
		*tmp &= himask;
		*val = be16_to_cpu(rvals);
	}

	return ret;
}

static int bd71828_get_vbat(struct bd71828_power *pwr, int *vcell)
{
	u16 tmp_vcell;
	int ret;

	ret = bd7182x_read16_himask(pwr, pwr->regs->vbat_avg,
				    BD7182x_MASK_VBAT_U, &tmp_vcell);
	if (ret)
		dev_err(pwr->dev, "Failed to read battery average voltage\n");
	else
		*vcell = ((int)tmp_vcell) * 1000;

	return ret;
}

static int bd71828_get_current_ds_adc(struct bd71828_power *pwr, int *curr, int *curr_avg)
{
	__be16 tmp_curr;
	char *tmp = (char *)&tmp_curr;
	int dir = 1;
	int regs[] = { pwr->regs->ibat, pwr->regs->ibat_avg };
	int *vals[] = { curr, curr_avg };
	int ret, i;

	for (dir = 1, i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = regmap_bulk_read(pwr->regmap, regs[i], &tmp_curr,
				       sizeof(tmp_curr));
		if (ret)
			break;

		if (*tmp & BD7182x_MASK_CURDIR_DISCHG)
			dir = -1;

		*tmp &= BD7182x_MASK_IBAT_U;

		*vals[i] = dir * ((int)be16_to_cpu(tmp_curr)) * pwr->curr_factor;
	}

	return ret;
}

/* Unit is tenths of degree C */
static int bd71815_get_temp(struct bd71828_power *pwr, int *temp)
{
	struct regmap *regmap = pwr->regmap;
	int ret;
	int t;

	ret = regmap_read(regmap, pwr->regs->btemp_vth, &t);
	if (ret)
		return ret;

	t = 200 - t;

	if (t > 200) {
		dev_err(pwr->dev, "Failed to read battery temperature\n");
		return -ENODATA;
	}

	return 0;
}

/* Unit is tenths of degree C */
static int bd71828_get_temp(struct bd71828_power *pwr, int *temp)
{
	u16 t;
	int ret;
	int tmp = 200 * 10000;

	ret = bd7182x_read16_himask(pwr, pwr->regs->btemp_vth,
				    BD71828_MASK_VM_BTMP_U, &t);
	if (ret)
		return ret;

	if (t > 3200) {
		dev_err(pwr->dev,
			"Failed to read battery temperature\n");
		return -ENODATA;
	}

	tmp -= 625ULL * (unsigned int)t;
	*temp = tmp / 1000;

	return ret;
}

static int bd71828_charge_status(struct bd71828_power *pwr,
				 int *s, int *h)
{
	unsigned int state;
	int status, health;
	int ret = 1;

	ret = regmap_read(pwr->regmap, pwr->regs->chg_state, &state);
	if (ret) {
		dev_err(pwr->dev, "charger status reading failed (%d)\n", ret);
		return ret;
	}

	state &= BD7182x_MASK_CHG_STATE;

	dev_dbg(pwr->dev, "CHG_STATE %d\n", state);

	switch (state) {
	case 0x00:
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x0E:
		status = POWER_SUPPLY_STATUS_CHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x0F:
		status = POWER_SUPPLY_STATUS_FULL;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x40:
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x7f:
	default:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		health = POWER_SUPPLY_HEALTH_DEAD;
		break;
	}

	if (s)
		*s = status;
	if (h)
		*h = health;

	return ret;
}

static int get_chg_online(struct bd71828_power *pwr, int *chg_online)
{
	int r, ret;

	ret = regmap_read(pwr->regmap, pwr->regs->dcin_stat, &r);
	if (ret) {
		dev_err(pwr->dev, "Failed to read DCIN status\n");
		return ret;
	}
	*chg_online = ((r & pwr->regs->dcin_online_mask) != 0);

	return 0;
}

static int get_bat_online(struct bd71828_power *pwr, int *bat_online)
{
	int r, ret;

	ret = regmap_read(pwr->regmap, pwr->regs->bat_temp, &r);
	if (ret) {
		dev_err(pwr->dev, "Failed to read battery temperature\n");
		return ret;
	}
	*bat_online = ((r & BD7182x_MASK_BAT_TEMP) != BAT_OPEN);

	return 0;
}

static int bd71828_bat_inserted(struct bd71828_power *pwr)
{
	int ret, val;

	ret = regmap_read(pwr->regmap, pwr->regs->conf, &val);
	if (ret) {
		dev_err(pwr->dev, "Failed to read CONF register\n");
		return 0;
	}
	ret = val & BD7182x_MASK_CONF_PON;

	if (ret)
		if (regmap_update_bits(pwr->regmap, pwr->regs->conf, BD7182x_MASK_CONF_PON, 0))
			dev_err(pwr->dev, "Failed to write CONF register\n");

	return ret;
}

static int bd71815_bat_inserted(struct bd71828_power *pwr)
{
	int ret, val;

	ret = regmap_read(pwr->regmap, pwr->regs->conf, &val);
	if (ret) {
		dev_err(pwr->dev, "Failed to read CONF register\n");
		return ret;
	}

	ret = !(val & BD71815_MASK_CONF_XSTB);
	if (ret)
		regmap_write(pwr->regmap, pwr->regs->conf,  val |
			     BD71815_MASK_CONF_XSTB);

	return ret;
}

static int bd71828_init_hardware(struct bd71828_power *pwr)
{
	int ret;

	/* TODO: Collapse limit should come from device-tree ? */
	if (pwr->regs->dcin_collapse_limit != (unsigned int)-1) {
		ret = regmap_write(pwr->regmap, pwr->regs->dcin_collapse_limit,
				   BD7182x_DCIN_COLLAPSE_DEFAULT);
		if (ret) {
			dev_err(pwr->dev, "Failed to write DCIN collapse limit\n");
			return ret;
		}
	}

	ret = pwr->bat_inserted(pwr);
	if (ret < 0)
		return ret;

	if (ret) {
		/* WDT_FST auto set */
		ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_set1,
					 BD7182x_MASK_WDT_AUTO,
					 BD7182x_MASK_WDT_AUTO);
		if (ret)
			return ret;

		ret = bd7182x_write16(pwr, pwr->regs->vbat_alm_limit_u,
				      VBAT_LOW_TH);
		if (ret)
			return ret;

		/*
		 * On BD71815 "we mask the power-state" from relax detection.
		 * I am unsure what the impact of the power-state would be if
		 * we didn't - but this is what the vendor driver did - and
		 * that driver has been used in few projects so I just assume
		 * this is needed.
		 */
		if (pwr->chip_type == ROHM_CHIP_TYPE_BD71815) {
			ret = regmap_set_bits(pwr->regmap,
					      BD71815_REG_REX_CTRL_1,
					      REX_PMU_STATE_MASK);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int bd71828_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct bd71828_power *pwr = dev_get_drvdata(psy->dev.parent);
	u32 vot;
	u16 tmp;
	int online;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = get_chg_online(pwr, &online);
		if (!ret)
			val->intval = online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bd7182x_read16_himask(pwr, pwr->regs->vdcin,
					    pwr->regs->vdcin_himask, &tmp);
		if (ret)
			return ret;

		vot = tmp;
		/* 5 milli volt steps */
		val->intval = 5000 * vot;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bd71828_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct bd71828_power *pwr = dev_get_drvdata(psy->dev.parent);
	int ret = 0;
	int status, health, tmp, curr, curr_avg, chg_en;

	if (psp == POWER_SUPPLY_PROP_STATUS ||
	    psp == POWER_SUPPLY_PROP_HEALTH ||
	    psp == POWER_SUPPLY_PROP_CHARGE_TYPE)
		ret = bd71828_charge_status(pwr, &status, &health);
	else if (psp == POWER_SUPPLY_PROP_CURRENT_AVG ||
		 psp == POWER_SUPPLY_PROP_CURRENT_NOW)
		ret = bd71828_get_current_ds_adc(pwr, &curr, &curr_avg);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = get_bat_online(pwr, &tmp);
		if (!ret)
			val->intval = tmp;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bd71828_get_vbat(pwr, &tmp);
		val->intval = tmp;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = curr_avg;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = curr;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = MAX_CURRENT_DEFAULT;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = pwr->get_temp(pwr, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		ret = regmap_read(pwr->regmap, pwr->regs->chg_en, &chg_en);
		if (ret)
			return ret;

		val->intval = (chg_en & BD7182x_MASK_CHG_EN) ?
			POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO :
			POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bd71828_battery_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct bd71828_power *pwr = dev_get_drvdata(psy->dev.parent);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		if (val->intval == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO)
			ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_en,
						 BD7182x_MASK_CHG_EN,
						 BD7182x_MASK_CHG_EN);
		else
			ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_en,
						 BD7182x_MASK_CHG_EN,
						 0);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int bd71828_battery_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return true;
	default:
		return false;
	}
}

/** @brief ac properties */
static const enum power_supply_property bd71828_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static const enum power_supply_property bd71828_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
};

/** @brief powers supplied by bd71828_ac */
static char *bd71828_ac_supplied_to[] = {
	BAT_NAME,
};

static const struct power_supply_desc bd71828_ac_desc = {
	.name		= AC_NAME,
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= bd71828_charger_props,
	.num_properties	= ARRAY_SIZE(bd71828_charger_props),
	.get_property	= bd71828_charger_get_property,
};

static const struct power_supply_desc bd71828_bat_desc = {
	.name		= BAT_NAME,
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.charge_behaviours = BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) |
			     BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE),
	.properties	= bd71828_battery_props,
	.num_properties = ARRAY_SIZE(bd71828_battery_props),
	.get_property	= bd71828_battery_get_property,
	.set_property	= bd71828_battery_set_property,
	.property_is_writeable   = bd71828_battery_property_is_writeable,
};

#define RSENS_CURR 10000000LLU

#define BD_ISR_NAME(name) \
bd7181x_##name##_isr

#define BD_ISR_BAT(name, print, run_gauge)				\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71828_power *pwr = (struct bd71828_power *)data;	\
									\
	dev_dbg(pwr->dev, "%s\n", print);				\
	power_supply_changed(pwr->bat);				\
									\
	return IRQ_HANDLED;						\
}

#define BD_ISR_AC(name, print, run_gauge)				\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71828_power *pwr = (struct bd71828_power *)data;	\
									\
	power_supply_changed(pwr->ac);					\
	dev_dbg(pwr->dev, "%s\n", print);				\
	power_supply_changed(pwr->bat);				\
									\
	return IRQ_HANDLED;						\
}

#define BD_ISR_DUMMY(name, print)					\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71828_power *pwr = (struct bd71828_power *)data;	\
									\
	dev_dbg(pwr->dev, "%s\n", print);				\
									\
	return IRQ_HANDLED;						\
}

BD_ISR_BAT(chg_state_changed, "CHG state changed", true)
/* DCIN voltage changes */
BD_ISR_AC(dcin_removed, "DCIN removed", true)
BD_ISR_AC(clps_out, "DCIN voltage back to normal", true)
BD_ISR_AC(clps_in, "DCIN voltage collapsed", false)
BD_ISR_AC(dcin_ovp_res, "DCIN voltage normal", true)
BD_ISR_AC(dcin_ovp_det, "DCIN OVER VOLTAGE", true)

BD_ISR_DUMMY(dcin_mon_det, "DCIN voltage below threshold")
BD_ISR_DUMMY(dcin_mon_res, "DCIN voltage above threshold")

BD_ISR_DUMMY(vbus_curr_limit, "VBUS current limited")
BD_ISR_DUMMY(vsys_ov_res, "VSYS over-voltage cleared")
BD_ISR_DUMMY(vsys_ov_det, "VSYS over-voltage")
BD_ISR_DUMMY(vsys_uv_res, "VSYS under-voltage cleared")
BD_ISR_DUMMY(vsys_uv_det, "VSYS under-voltage")
BD_ISR_DUMMY(vsys_low_res, "'VSYS low' cleared")
BD_ISR_DUMMY(vsys_low_det, "VSYS low")
BD_ISR_DUMMY(vsys_mon_res, "VSYS mon - resumed")
BD_ISR_DUMMY(vsys_mon_det, "VSYS mon - detected")
BD_ISR_BAT(chg_wdg_temp, "charger temperature watchdog triggered", true)
BD_ISR_BAT(chg_wdg, "charging watchdog triggered", true)
BD_ISR_BAT(bat_removed, "Battery removed", true)
BD_ISR_BAT(bat_det, "Battery detected", true)
/* TODO: Verify the meaning of these interrupts */
BD_ISR_BAT(rechg_det, "Recharging", true)
BD_ISR_BAT(rechg_res, "Recharge ending", true)
BD_ISR_DUMMY(temp_transit, "Temperature transition")
BD_ISR_BAT(therm_rmv, "bd71815-therm-rmv", false)
BD_ISR_BAT(therm_det, "bd71815-therm-det", true)
BD_ISR_BAT(bat_dead, "bd71815-bat-dead", false)
BD_ISR_BAT(bat_short_res, "bd71815-bat-short-res", true)
BD_ISR_BAT(bat_short, "bd71815-bat-short-det", false)
BD_ISR_BAT(bat_low_res, "bd71815-bat-low-res", true)
BD_ISR_BAT(bat_low, "bd71815-bat-low-det", true)
BD_ISR_BAT(bat_ov_res, "bd71815-bat-over-res", true)
/* What should we do here? */
BD_ISR_BAT(bat_ov, "bd71815-bat-over-det", false)
BD_ISR_BAT(bat_mon_res, "bd71815-bat-mon-res", true)
BD_ISR_BAT(bat_mon, "bd71815-bat-mon-det", true)
BD_ISR_BAT(bat_cc_mon, "bd71815-bat-cc-mon2", false)
BD_ISR_BAT(bat_oc1_res, "bd71815-bat-oc1-res", true)
BD_ISR_BAT(bat_oc1, "bd71815-bat-oc1-det", false)
BD_ISR_BAT(bat_oc2_res, "bd71815-bat-oc2-res", true)
BD_ISR_BAT(bat_oc2, "bd71815-bat-oc2-det", false)
BD_ISR_BAT(bat_oc3_res, "bd71815-bat-oc3-res", true)
BD_ISR_BAT(bat_oc3, "bd71815-bat-oc3-det", false)
BD_ISR_BAT(temp_bat_low_res, "bd71815-temp-bat-low-res", true)
BD_ISR_BAT(temp_bat_low, "bd71815-temp-bat-low-det", true)
BD_ISR_BAT(temp_bat_hi_res, "bd71815-temp-bat-hi-res", true)
BD_ISR_BAT(temp_bat_hi, "bd71815-temp-bat-hi-det", true)

static irqreturn_t bd7182x_dcin_removed(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	power_supply_changed(pwr->ac);
	dev_dbg(pwr->dev, "DCIN removed\n");

	return IRQ_HANDLED;
}

static irqreturn_t bd718x7_chg_done(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd7182x_dcin_detected(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "DCIN inserted\n");
	power_supply_changed(pwr->ac);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_vbat_low_res(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VBAT LOW Resumed\n");

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_vbat_low_det(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VBAT LOW Detected\n");

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_bat_hi_det(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_warn(pwr->dev, "Overtemp Detected\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_bat_hi_res(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "Overtemp Resumed\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_bat_low_det(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "Lowtemp Detected\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_bat_low_res(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "Lowtemp Resumed\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_vf_det(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VF Detected\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_vf_res(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VF Resumed\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_vf125_det(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VF125 Detected\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

static irqreturn_t bd71828_temp_vf125_res(int irq, void *data)
{
	struct bd71828_power *pwr = (struct bd71828_power *)data;

	dev_dbg(pwr->dev, "VF125 Resumed\n");
	power_supply_changed(pwr->bat);

	return IRQ_HANDLED;
}

struct bd7182x_irq_res {
	const char *name;
	irq_handler_t handler;
};

#define BDIRQ(na, hn) { .name = (na), .handler = (hn) }

static int bd7182x_get_irqs(struct platform_device *pdev,
			    struct bd71828_power *pwr)
{
	int i, irq, ret;
	static const struct bd7182x_irq_res bd71815_irqs[] = {
		BDIRQ("bd71815-dcin-rmv", BD_ISR_NAME(dcin_removed)),
		BDIRQ("bd71815-dcin-clps-out", BD_ISR_NAME(clps_out)),
		BDIRQ("bd71815-dcin-clps-in", BD_ISR_NAME(clps_in)),
		BDIRQ("bd71815-dcin-ovp-res", BD_ISR_NAME(dcin_ovp_res)),
		BDIRQ("bd71815-dcin-ovp-det", BD_ISR_NAME(dcin_ovp_det)),
		BDIRQ("bd71815-dcin-mon-res", BD_ISR_NAME(dcin_mon_res)),
		BDIRQ("bd71815-dcin-mon-det", BD_ISR_NAME(dcin_mon_det)),

		BDIRQ("bd71815-vsys-uv-res", BD_ISR_NAME(vsys_uv_res)),
		BDIRQ("bd71815-vsys-uv-det", BD_ISR_NAME(vsys_uv_det)),
		BDIRQ("bd71815-vsys-low-res", BD_ISR_NAME(vsys_low_res)),
		BDIRQ("bd71815-vsys-low-det",  BD_ISR_NAME(vsys_low_det)),
		BDIRQ("bd71815-vsys-mon-res",  BD_ISR_NAME(vsys_mon_res)),
		BDIRQ("bd71815-vsys-mon-det",  BD_ISR_NAME(vsys_mon_det)),
		BDIRQ("bd71815-chg-wdg-temp", BD_ISR_NAME(chg_wdg_temp)),
		BDIRQ("bd71815-chg-wdg",  BD_ISR_NAME(chg_wdg)),
		BDIRQ("bd71815-rechg-det", BD_ISR_NAME(rechg_det)),
		BDIRQ("bd71815-rechg-res", BD_ISR_NAME(rechg_res)),
		BDIRQ("bd71815-ranged-temp-transit", BD_ISR_NAME(temp_transit)),
		BDIRQ("bd71815-chg-state-change", BD_ISR_NAME(chg_state_changed)),
		BDIRQ("bd71815-bat-temp-normal", bd71828_temp_bat_hi_res),
		BDIRQ("bd71815-bat-temp-erange", bd71828_temp_bat_hi_det),
		BDIRQ("bd71815-bat-rmv", BD_ISR_NAME(bat_removed)),
		BDIRQ("bd71815-bat-det", BD_ISR_NAME(bat_det)),

		/* Add ISRs for these */
		BDIRQ("bd71815-therm-rmv", BD_ISR_NAME(therm_rmv)),
		BDIRQ("bd71815-therm-det", BD_ISR_NAME(therm_det)),
		BDIRQ("bd71815-bat-dead", BD_ISR_NAME(bat_dead)),
		BDIRQ("bd71815-bat-short-res", BD_ISR_NAME(bat_short_res)),
		BDIRQ("bd71815-bat-short-det", BD_ISR_NAME(bat_short)),
		BDIRQ("bd71815-bat-low-res", BD_ISR_NAME(bat_low_res)),
		BDIRQ("bd71815-bat-low-det", BD_ISR_NAME(bat_low)),
		BDIRQ("bd71815-bat-over-res", BD_ISR_NAME(bat_ov_res)),
		BDIRQ("bd71815-bat-over-det", BD_ISR_NAME(bat_ov)),
		BDIRQ("bd71815-bat-mon-res", BD_ISR_NAME(bat_mon_res)),
		BDIRQ("bd71815-bat-mon-det", BD_ISR_NAME(bat_mon)),
		/* cc-mon 1 & 3 ? */
		BDIRQ("bd71815-bat-cc-mon2", BD_ISR_NAME(bat_cc_mon)),
		BDIRQ("bd71815-bat-oc1-res", BD_ISR_NAME(bat_oc1_res)),
		BDIRQ("bd71815-bat-oc1-det", BD_ISR_NAME(bat_oc1)),
		BDIRQ("bd71815-bat-oc2-res", BD_ISR_NAME(bat_oc2_res)),
		BDIRQ("bd71815-bat-oc2-det", BD_ISR_NAME(bat_oc2)),
		BDIRQ("bd71815-bat-oc3-res", BD_ISR_NAME(bat_oc3_res)),
		BDIRQ("bd71815-bat-oc3-det", BD_ISR_NAME(bat_oc3)),
		BDIRQ("bd71815-temp-bat-low-res", BD_ISR_NAME(temp_bat_low_res)),
		BDIRQ("bd71815-temp-bat-low-det", BD_ISR_NAME(temp_bat_low)),
		BDIRQ("bd71815-temp-bat-hi-res", BD_ISR_NAME(temp_bat_hi_res)),
		BDIRQ("bd71815-temp-bat-hi-det", BD_ISR_NAME(temp_bat_hi)),
		/*
		 * TODO: add rest of the IRQs and re-check the handling.
		 * Check the bd71815-bat-cc-mon1, bd71815-bat-cc-mon3,
		 * bd71815-bat-low-res, bd71815-bat-low-det,
		 * bd71815-bat-hi-res, bd71815-bat-hi-det.
		 */
	};
	static const struct bd7182x_irq_res bd71828_irqs[] = {
		BDIRQ("bd71828-chg-done", bd718x7_chg_done),
		BDIRQ("bd71828-pwr-dcin-in", bd7182x_dcin_detected),
		BDIRQ("bd71828-pwr-dcin-out", bd7182x_dcin_removed),
		BDIRQ("bd71828-vbat-normal", bd71828_vbat_low_res),
		BDIRQ("bd71828-vbat-low", bd71828_vbat_low_det),
		BDIRQ("bd71828-btemp-hi", bd71828_temp_bat_hi_det),
		BDIRQ("bd71828-btemp-cool", bd71828_temp_bat_hi_res),
		BDIRQ("bd71828-btemp-lo", bd71828_temp_bat_low_det),
		BDIRQ("bd71828-btemp-warm", bd71828_temp_bat_low_res),
		BDIRQ("bd71828-temp-hi", bd71828_temp_vf_det),
		BDIRQ("bd71828-temp-norm", bd71828_temp_vf_res),
		BDIRQ("bd71828-temp-125-over", bd71828_temp_vf125_det),
		BDIRQ("bd71828-temp-125-under", bd71828_temp_vf125_res),
	};
	static const struct bd7182x_irq_res bd72720_irqs[] = {
		BDIRQ("bd72720_int_vbus_rmv", BD_ISR_NAME(dcin_removed)),
		BDIRQ("bd72720_int_vbus_det", bd7182x_dcin_detected),
		BDIRQ("bd72720_int_vbus_mon_res", BD_ISR_NAME(dcin_mon_res)),
		BDIRQ("bd72720_int_vbus_mon_det", BD_ISR_NAME(dcin_mon_det)),
		BDIRQ("bd72720_int_vsys_mon_res", BD_ISR_NAME(vsys_mon_res)),
		BDIRQ("bd72720_int_vsys_mon_det", BD_ISR_NAME(vsys_mon_det)),
		BDIRQ("bd72720_int_vsys_uv_res", BD_ISR_NAME(vsys_uv_res)),
		BDIRQ("bd72720_int_vsys_uv_det", BD_ISR_NAME(vsys_uv_det)),
		BDIRQ("bd72720_int_vsys_lo_res", BD_ISR_NAME(vsys_low_res)),
		BDIRQ("bd72720_int_vsys_lo_det", BD_ISR_NAME(vsys_low_det)),
		BDIRQ("bd72720_int_vsys_ov_res", BD_ISR_NAME(vsys_ov_res)),
		BDIRQ("bd72720_int_vsys_ov_det", BD_ISR_NAME(vsys_ov_det)),
		BDIRQ("bd72720_int_bat_ilim", BD_ISR_NAME(vbus_curr_limit)),
		BDIRQ("bd72720_int_chg_done", bd718x7_chg_done),
		BDIRQ("bd72720_int_extemp_tout", BD_ISR_NAME(chg_wdg_temp)),
		BDIRQ("bd72720_int_chg_wdt_exp", BD_ISR_NAME(chg_wdg)),
		BDIRQ("bd72720_int_bat_mnt_out", BD_ISR_NAME(rechg_res)),
		BDIRQ("bd72720_int_bat_mnt_in", BD_ISR_NAME(rechg_det)),
		BDIRQ("bd72720_int_chg_trns", BD_ISR_NAME(chg_state_changed)),

		BDIRQ("bd72720_int_vbat_mon_res", BD_ISR_NAME(bat_mon_res)),
		BDIRQ("bd72720_int_vbat_mon_det", BD_ISR_NAME(bat_mon)),
		BDIRQ("bd72720_int_vbat_sht_res", BD_ISR_NAME(bat_short_res)),
		BDIRQ("bd72720_int_vbat_sht_det", BD_ISR_NAME(bat_short)),
		BDIRQ("bd72720_int_vbat_lo_res", BD_ISR_NAME(bat_low_res)),
		BDIRQ("bd72720_int_vbat_lo_det", BD_ISR_NAME(bat_low)),
		BDIRQ("bd72720_int_vbat_ov_res", BD_ISR_NAME(bat_ov_res)),
		BDIRQ("bd72720_int_vbat_ov_det", BD_ISR_NAME(bat_ov)),
		BDIRQ("bd72720_int_bat_rmv", BD_ISR_NAME(bat_removed)),
		BDIRQ("bd72720_int_bat_det", BD_ISR_NAME(bat_det)),
		BDIRQ("bd72720_int_dbat_det", BD_ISR_NAME(bat_dead)),
		BDIRQ("bd72720_int_bat_temp_trns", BD_ISR_NAME(temp_transit)),
		BDIRQ("bd72720_int_lobtmp_res", BD_ISR_NAME(temp_bat_low_res)),
		BDIRQ("bd72720_int_lobtmp_det", BD_ISR_NAME(temp_bat_low)),
		BDIRQ("bd72720_int_ovbtmp_res", BD_ISR_NAME(temp_bat_hi_res)),
		BDIRQ("bd72720_int_ovbtmp_det", BD_ISR_NAME(temp_bat_hi)),
		BDIRQ("bd72720_int_ocur1_res", BD_ISR_NAME(bat_oc1_res)),
		BDIRQ("bd72720_int_ocur1_det", BD_ISR_NAME(bat_oc1)),
		BDIRQ("bd72720_int_ocur2_res", BD_ISR_NAME(bat_oc2_res)),
		BDIRQ("bd72720_int_ocur2_det", BD_ISR_NAME(bat_oc2)),
		BDIRQ("bd72720_int_ocur3_res", BD_ISR_NAME(bat_oc3_res)),
		BDIRQ("bd72720_int_ocur3_det", BD_ISR_NAME(bat_oc3)),
		BDIRQ("bd72720_int_cc_mon2_det", BD_ISR_NAME(bat_cc_mon)),
	};
	int num_irqs;
	const struct bd7182x_irq_res *irqs;

	switch (pwr->chip_type) {
	case ROHM_CHIP_TYPE_BD71828:
		irqs = &bd71828_irqs[0];
		num_irqs = ARRAY_SIZE(bd71828_irqs);
		break;
	case ROHM_CHIP_TYPE_BD71815:
		irqs = &bd71815_irqs[0];
		num_irqs = ARRAY_SIZE(bd71815_irqs);
		break;
	case ROHM_CHIP_TYPE_BD72720:
		irqs = &bd72720_irqs[0];
		num_irqs = ARRAY_SIZE(bd72720_irqs);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < num_irqs; i++) {
		irq = platform_get_irq_byname(pdev, irqs[i].name);

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						irqs[i].handler, 0,
						irqs[i].name, pwr);
		if (ret)
			break;
	}

	return ret;
}

#define RSENS_DEFAULT_30MOHM 30000 /* 30 mOhm in uOhms*/

static int bd7182x_get_rsens(struct bd71828_power *pwr)
{
	u64 tmp = RSENS_CURR;
	int rsens_ohm = RSENS_DEFAULT_30MOHM;
	struct fwnode_handle *node = NULL;

	if (pwr->dev->parent)
		node = dev_fwnode(pwr->dev->parent);

	if (node) {
		int ret;
		u32 rs;

		ret = fwnode_property_read_u32(node,
					       "rohm,charger-sense-resistor-micro-ohms",
					       &rs);
		if (ret) {
			if (ret == -EINVAL) {
				rs = RSENS_DEFAULT_30MOHM;
			} else {
				dev_err(pwr->dev, "Bad RSENS dt property\n");
				return ret;
			}
		}
		if (!rs) {
			dev_err(pwr->dev, "Bad RSENS value\n");
			return -EINVAL;
		}

		rsens_ohm = (int)rs;
	}

	/* Reg val to uA */
	do_div(tmp, rsens_ohm);

	pwr->curr_factor = tmp;
	pwr->rsens = rsens_ohm;
	dev_dbg(pwr->dev, "Setting rsens to %u micro ohm\n", pwr->rsens);
	dev_dbg(pwr->dev, "Setting curr-factor to %u\n", pwr->curr_factor);

	return 0;
}

static int bd71828_power_probe(struct platform_device *pdev)
{
	struct bd71828_power *pwr;
	struct power_supply_config ac_cfg = {};
	struct power_supply_config bat_cfg = {};
	int ret;

	pwr = devm_kzalloc(&pdev->dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	/*
	 * The BD72720 MFD device registers two regmaps. Power-supply driver
	 * uses the "wrap-map", which provides access to both of the I2C slave
	 * addresses used by the BD72720
	 */
	pwr->chip_type = platform_get_device_id(pdev)->driver_data;
	if (pwr->chip_type != ROHM_CHIP_TYPE_BD72720)
		pwr->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	else
		pwr->regmap = dev_get_regmap(pdev->dev.parent, "wrap-map");
	if (!pwr->regmap)
		return dev_err_probe(&pdev->dev, -EINVAL, "No parent regmap\n");

	pwr->dev = &pdev->dev;

	switch (pwr->chip_type) {
	case ROHM_CHIP_TYPE_BD71828:
		pwr->bat_inserted = bd71828_bat_inserted;
		pwr->get_temp = bd71828_get_temp;
		pwr->regs = &pwr_regs_bd71828;
		break;
	case ROHM_CHIP_TYPE_BD71815:
		pwr->bat_inserted = bd71815_bat_inserted;
		pwr->get_temp = bd71815_get_temp;
		pwr->regs = &pwr_regs_bd71815;
		break;
	case ROHM_CHIP_TYPE_BD72720:
		pwr->bat_inserted = bd71828_bat_inserted;
		pwr->regs = &pwr_regs_bd72720;
		pwr->get_temp = bd71828_get_temp;
		dev_dbg(pwr->dev, "Found ROHM BD72720\n");
		break;
	default:
		return dev_err_probe(pwr->dev, -EINVAL, "Unknown PMIC\n");
	}

	ret = bd7182x_get_rsens(pwr);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "sense resistor missing\n");

	dev_set_drvdata(&pdev->dev, pwr);
	bd71828_init_hardware(pwr);

	bat_cfg.drv_data	= pwr;
	bat_cfg.fwnode		= dev_fwnode(&pdev->dev);

	ac_cfg.supplied_to	= bd71828_ac_supplied_to;
	ac_cfg.num_supplicants	= ARRAY_SIZE(bd71828_ac_supplied_to);
	ac_cfg.drv_data		= pwr;

	pwr->ac = devm_power_supply_register(&pdev->dev, &bd71828_ac_desc,
					     &ac_cfg);
	if (IS_ERR(pwr->ac))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwr->ac),
				     "failed to register ac\n");

	pwr->bat = devm_power_supply_register(&pdev->dev, &bd71828_bat_desc,
					      &bat_cfg);
	if (IS_ERR(pwr->bat))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwr->bat),
				     "failed to register bat\n");

	ret = bd7182x_get_irqs(pdev, pwr);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to request IRQs");

	/* Configure wakeup capable */
	device_set_wakeup_capable(pwr->dev, 1);
	device_set_wakeup_enable(pwr->dev, 1);

	return 0;
}

static const struct platform_device_id bd71828_charger_id[] = {
	{ "bd71815-power", ROHM_CHIP_TYPE_BD71815 },
	{ "bd71828-power", ROHM_CHIP_TYPE_BD71828 },
	{ "bd72720-power", ROHM_CHIP_TYPE_BD72720 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd71828_charger_id);

static struct platform_driver bd71828_power_driver = {
	.driver = {
		.name = "bd718xx-power",
	},
	.probe = bd71828_power_probe,
	.id_table = bd71828_charger_id,
};

module_platform_driver(bd71828_power_driver);

MODULE_AUTHOR("Cong Pham <cpham2403@gmail.com>");
MODULE_DESCRIPTION("ROHM BD718(15/28/78) PMIC Battery Charger driver");
MODULE_LICENSE("GPL");
