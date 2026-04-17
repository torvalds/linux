// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SMC Power/Battery Management Driver
 *
 * This driver exposes battery telemetry (voltage, current, temperature, health)
 * and AC adapter status provided by the Apple SMC (System Management Controller)
 * on Apple Silicon systems.
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mfd/macsmc.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/workqueue.h>

#define MAX_STRING_LENGTH 256

/*
 * The SMC reports charge in mAh (Coulombs) but energy in mWh (Joules).
 * We lack a register for "Nominal Voltage" or "Energy Accumulator".
 * We use a fixed 3.8V/cell constant to approximate energy stats for userspace,
 * derived from empirical data across supported MacBook models.
 */
#define MACSMC_NOMINAL_CELL_VOLTAGE_MV 3800

/* SMC Key Flags */
#define CHNC_BATTERY_FULL	BIT(0)
#define CHNC_NO_CHARGER		BIT(7)
#define CHNC_NOCHG_CH0C		BIT(14)
#define CHNC_NOCHG_CH0B_CH0K	BIT(15)
#define CHNC_BATTERY_FULL_2	BIT(18)
#define CHNC_BMS_BUSY		BIT(23)
#define CHNC_CHLS_LIMIT		BIT(24)
#define CHNC_NOAC_CH0J		BIT(53)
#define CHNC_NOAC_CH0I		BIT(54)

#define CH0R_LOWER_FLAGS	GENMASK(15, 0)
#define CH0R_NOAC_CH0I		BIT(0)
#define CH0R_NOAC_DISCONNECTED	BIT(4)
#define CH0R_NOAC_CH0J		BIT(5)
#define CH0R_BMS_BUSY		BIT(8)
#define CH0R_NOAC_CH0K		BIT(9)
#define CH0R_NOAC_CHWA		BIT(11)

#define CH0X_CH0C		BIT(0)
#define CH0X_CH0B		BIT(1)

#define ACSt_CAN_BOOT_AP	BIT(2)
#define ACSt_CAN_BOOT_IBOOT	BIT(1)

#define CHWA_CHLS_FIXED_START_OFFSET	5
#define CHLS_MIN_END_THRESHOLD		10
#define CHLS_FORCE_DISCHARGE		0x100
#define CHWA_FIXED_END_THRESHOLD	80
#define CHWA_PROP_WRITE_THRESHOLD	95

#define MACSMC_MAX_BATT_PROPS		50
#define MACSMC_MAX_AC_PROPS		10

struct macsmc_power {
	struct device *dev;
	struct apple_smc *smc;

	struct power_supply_desc ac_desc;
	struct power_supply_desc batt_desc;

	struct power_supply *batt;
	struct power_supply *ac;

	char model_name[MAX_STRING_LENGTH];
	char serial_number[MAX_STRING_LENGTH];
	char mfg_date[MAX_STRING_LENGTH];

	/* Supported feature flags based on SMC key presence */
	bool has_chwa; /* Charge limit (Modern firmware) */
	bool has_chls; /* Charge limit (Older firmware) */
	bool has_ch0i; /* Force discharge (Older firmware) */
	bool has_ch0c; /* Inhibit charge (Older firmware) */
	bool has_chte; /* Inhibit charge (Modern firmware) */

	u8 num_cells;
	int nominal_voltage_mv;

	struct notifier_block nb;
	struct work_struct critical_work;
	bool emergency_shutdown_triggered;
	bool orderly_shutdown_triggered;
};

static int macsmc_battery_get_status(struct macsmc_power *power)
{
	u64 nocharge_flags;
	u32 nopower_flags;
	u16 ac_current;
	int charge_limit = 0;
	bool limited = false;
	bool flag;
	int ret;

	/*
	 * B0AV (Voltage) is fundamental. If we can't read it, we assume the
	 * battery is gone. CHCE (Hardware charger present) / CHCC (Hardware
	 * charger capable) are fundamental status flags.
	 * BSFC (System full charge) / CHSC (System charging) are fundamental
	 * status flags.
	 */

	/* Check if power input is inhibited (e.g. BMS balancing cycle) */
	ret = apple_smc_read_u32(power->smc, SMC_KEY(CH0R), &nopower_flags);
	if (!ret && (nopower_flags & CH0R_LOWER_FLAGS & ~CH0R_BMS_BUSY))
		return POWER_SUPPLY_STATUS_DISCHARGING;

	/* Check if charger is present */
	ret = apple_smc_read_flag(power->smc, SMC_KEY(CHCE), &flag);
	if (ret < 0)
		return ret;
	if (!flag)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	/* Check if AC is charge capable */
	ret = apple_smc_read_flag(power->smc, SMC_KEY(CHCC), &flag);
	if (ret < 0)
		return ret;
	if (!flag)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	/* Check if AC input limit is too low */
	ret = apple_smc_read_u16(power->smc, SMC_KEY(AC-i), &ac_current);
	if (!ret && ac_current < 100)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	/* Check if battery is full */
	ret = apple_smc_read_flag(power->smc, SMC_KEY(BSFC), &flag);
	if (ret < 0)
		return ret;
	if (flag)
		return POWER_SUPPLY_STATUS_FULL;

	/* Check for user-defined charge limits */
	if (power->has_chls) {
		u16 vu16;

		ret = apple_smc_read_u16(power->smc, SMC_KEY(CHLS), &vu16);
		if (ret == 0 && (vu16 & 0xff) >= CHLS_MIN_END_THRESHOLD)
			charge_limit = (vu16 & 0xff) - CHWA_CHLS_FIXED_START_OFFSET;
	} else if (power->has_chwa) {
		ret = apple_smc_read_flag(power->smc, SMC_KEY(CHWA), &flag);
		if (ret == 0 && flag)
			charge_limit = CHWA_FIXED_END_THRESHOLD - CHWA_CHLS_FIXED_START_OFFSET;
	}

	if (charge_limit > 0) {
		u8 buic = 0;

		if (apple_smc_read_u8(power->smc, SMC_KEY(BUIC), &buic) >= 0 &&
		    buic >= charge_limit)
			limited = true;
	}

	/* Check charging inhibitors */
	ret = apple_smc_read_u64(power->smc, SMC_KEY(CHNC), &nocharge_flags);
	if (!ret) {
		if (nocharge_flags & CHNC_BATTERY_FULL)
			return POWER_SUPPLY_STATUS_FULL;
		/* BMS busy shows up as inhibit, but we treat it as charging */
		else if (nocharge_flags == CHNC_BMS_BUSY && !limited)
			return POWER_SUPPLY_STATUS_CHARGING;
		else if (nocharge_flags)
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			return POWER_SUPPLY_STATUS_CHARGING;
	}

	/* Fallback: System charging flag */
	ret = apple_smc_read_flag(power->smc, SMC_KEY(CHSC), &flag);
	if (ret < 0)
		return ret;
	if (!flag)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	return POWER_SUPPLY_STATUS_CHARGING;
}

static int macsmc_battery_get_charge_behaviour(struct macsmc_power *power)
{
	int ret;
	u8 val8;
	u8 chte_buf[4];

	if (power->has_ch0i) {
		ret = apple_smc_read_u8(power->smc, SMC_KEY(CH0I), &val8);
		if (ret)
			return ret;
		if (val8 & CH0R_NOAC_CH0I)
			return POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE;
	}

	if (power->has_chte) {
		ret = apple_smc_read(power->smc, SMC_KEY(CHTE), chte_buf, 4);
		if (ret < 0)
			return ret;

		if (chte_buf[0] == 0x01)
			return POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
	} else if (power->has_ch0c) {
		ret = apple_smc_read_u8(power->smc, SMC_KEY(CH0C), &val8);
		if (ret)
			return ret;
		if (val8 & CH0X_CH0C)
			return POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
	}

	return POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
}

static int macsmc_battery_set_charge_behaviour(struct macsmc_power *power, int val)
{
	int ret;

	switch (val) {
	case POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
		/* Reset all inhibitors to a known-good 'auto' state */
		if (power->has_ch0i) {
			ret = apple_smc_write_u8(power->smc, SMC_KEY(CH0I), 0);
			if (ret)
				return ret;
		}

		if (power->has_chte) {
			ret = apple_smc_write_u32(power->smc, SMC_KEY(CHTE), 0);
			if (ret)
				return ret;
		} else if (power->has_ch0c) {
			ret = apple_smc_write_u8(power->smc, SMC_KEY(CH0C), 0);
			if (ret)
				return ret;
		}
		return 0;

	case POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE:
		if (power->has_chte)
			return apple_smc_write_u32(power->smc, SMC_KEY(CHTE), 1);
		else if (power->has_ch0c)
			return apple_smc_write_u8(power->smc, SMC_KEY(CH0C), 1);
		else
			return -EOPNOTSUPP;

	case POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE:
		if (!power->has_ch0i)
			return -EOPNOTSUPP;
		return apple_smc_write_u8(power->smc, SMC_KEY(CH0I), 1);

	default:
		return -EINVAL;
	}
}

static int macsmc_battery_get_date(const char *s, int *out)
{
	if (!isdigit(s[0]) || !isdigit(s[1]))
		return -EOPNOTSUPP;

	*out = (s[0] - '0') * 10 + s[1] - '0';
	return 0;
}

static int macsmc_battery_get_capacity_level(struct macsmc_power *power)
{
	bool flag;
	u32 val;
	int ret;

	/* Check for emergency shutdown condition */
	if (apple_smc_read_u32(power->smc, SMC_KEY(BCF0), &val) >= 0 && val)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	/* Check AC status for whether we could boot in this state */
	if (apple_smc_read_u32(power->smc, SMC_KEY(ACSt), &val) >= 0) {
		if (!(val & ACSt_CAN_BOOT_IBOOT))
			return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

		if (!(val & ACSt_CAN_BOOT_AP))
			return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	}

	/* BSFC = Battery System Full Charge */
	ret = apple_smc_read_flag(power->smc, SMC_KEY(BSFC), &flag);
	if (ret < 0)
		return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

	if (flag)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
}

static int macsmc_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct macsmc_power *power = power_supply_get_drvdata(psy);
	int ret = 0;
	u8 vu8;
	u16 vu16;
	s16 vs16;
	s32 vs32;
	s64 vs64;
	bool flag;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = macsmc_battery_get_status(power);
		ret = val->intval < 0 ? val->intval : 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = macsmc_battery_get_charge_behaviour(power);
		ret = val->intval < 0 ? val->intval : 0;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0TE), &vu16);
		val->intval = vu16 == 0xffff ? 0 : vu16 * 60;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0TF), &vu16);
		val->intval = vu16 == 0xffff ? 0 : vu16 * 60;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = apple_smc_read_u8(power->smc, SMC_KEY(BUIC), &vu8);
		val->intval = vu8;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = macsmc_battery_get_capacity_level(power);
		ret = val->intval < 0 ? val->intval : 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0AV), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = apple_smc_read_s16(power->smc, SMC_KEY(B0AC), &vs16);
		val->intval = vs16 * 1000;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		ret = apple_smc_read_s32(power->smc, SMC_KEY(B0AP), &vs32);
		val->intval = vs32 * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(BITV), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		/* Calculate total max design voltage from per-cell maximum voltage */
		ret = apple_smc_read_u16(power->smc, SMC_KEY(BVVN), &vu16);
		val->intval = vu16 * 1000 * power->num_cells;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		/* Lifetime min */
		ret = apple_smc_read_s16(power->smc, SMC_KEY(BLPM), &vs16);
		val->intval = vs16 * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		/* Lifetime max */
		ret = apple_smc_read_s16(power->smc, SMC_KEY(BLPX), &vs16);
		val->intval = vs16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0RC), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0RI), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0RV), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0DC), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0FC), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0RM), &vu16);
		/* B0RM is Big Endian, likely pass through from TI gas gauge */
		val->intval = (s16)swab16(vu16) * 1000;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0DC), &vu16);
		val->intval = vu16 * power->nominal_voltage_mv;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0FC), &vu16);
		val->intval = vu16 * power->nominal_voltage_mv;
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0RM), &vu16);
		/* B0RM is Big Endian, likely pass through from TI gas gauge */
		val->intval = (s16)swab16(vu16) * power->nominal_voltage_mv;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0AT), &vu16);
		val->intval = vu16 - 2732; /* Kelvin x10 to Celsius x10 */
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = apple_smc_read_s64(power->smc, SMC_KEY(BAAC), &vs64);
		val->intval = vs64;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(B0CT), &vu16);
		val->intval = vu16;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		flag = false;
		ret = apple_smc_read_flag(power->smc, SMC_KEY(BBAD), &flag);
		val->intval = flag ? POWER_SUPPLY_HEALTH_DEAD : POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = power->model_name;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = power->serial_number;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_YEAR:
		ret = macsmc_battery_get_date(&power->mfg_date[0], &val->intval);
		/* The SMC reports the manufacture year as an offset from 1992. */
		val->intval += 1992;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_MONTH:
		ret = macsmc_battery_get_date(&power->mfg_date[2], &val->intval);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_DAY:
		ret = macsmc_battery_get_date(&power->mfg_date[4], &val->intval);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int macsmc_battery_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct macsmc_power *power = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return macsmc_battery_set_charge_behaviour(power, val->intval);
	default:
		return -EINVAL;
	}
}

static int macsmc_battery_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		return true;
	default:
		return false;
	}
}

static const struct power_supply_desc macsmc_battery_desc_template = {
	.name			= "macsmc-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.get_property		= macsmc_battery_get_property,
	.set_property		= macsmc_battery_set_property,
	.property_is_writeable	= macsmc_battery_property_is_writeable,
};

static int macsmc_ac_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct macsmc_power *power = power_supply_get_drvdata(psy);
	int ret = 0;
	u16 vu16;
	u32 vu32;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = apple_smc_read_u32(power->smc, SMC_KEY(CHIS), &vu32);
		val->intval = !!vu32;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(AC-n), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = apple_smc_read_u16(power->smc, SMC_KEY(AC-i), &vu16);
		val->intval = vu16 * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_POWER_LIMIT:
		ret = apple_smc_read_u32(power->smc, SMC_KEY(ACPW), &vu32);
		val->intval = vu32 * 1000;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct power_supply_desc macsmc_ac_desc_template = {
	.name			= "macsmc-ac",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.get_property		= macsmc_ac_get_property,
};

static void macsmc_power_critical_work(struct work_struct *wrk)
{
	struct macsmc_power *power = container_of(wrk, struct macsmc_power, critical_work);
	u16 bitv, b0av;
	u32 bcf0;

	if (!power->batt)
		return;

	/*
	 * Avoid duplicate atempts at emergency shutdown
	 */
	if (power->emergency_shutdown_triggered || system_state > SYSTEM_RUNNING)
		return;

	/*
	 * EMERGENCY: Check voltage vs design minimum.
	 * If we are below BITV, the battery is physically exhausted.
	 * We must shut down NOW to protect the filesystem.
	 */
	if (apple_smc_read_u16(power->smc, SMC_KEY(BITV), &bitv) >= 0 &&
	    apple_smc_read_u16(power->smc, SMC_KEY(B0AV), &b0av) >= 0 &&
	    b0av < bitv) {
		power->emergency_shutdown_triggered = true;
		dev_emerg(power->dev,
			  "Battery voltage (%d mV) below design minimum (%d mV)! Emergency shutdown.\n",
			  b0av, bitv);

		/*
		 * Shutdown is now imminent. Kick userspace again and give it some
		 * brief time to (hopefully) flush what's needed, before forcing.
		 */
		hw_protection_trigger("Battery voltage below design minimum", 1500);
	}

	/*
	 * Avoid duplicate attempts at orderly shutdown.
	 * Voltage check is above this as we may want to
	 * "upgrade" an orderly shutdown to a critical power
	 * off if voltage drops.
	 */
	if (power->orderly_shutdown_triggered || system_state > SYSTEM_RUNNING)
		return;

	/*
	 * Check if SMC flagged the battery as empty.
	 * We trigger a graceful shutdown to let the OS save data.
	 */
	if (apple_smc_read_u32(power->smc, SMC_KEY(BCF0), &bcf0) == 0 && bcf0 != 0) {
		power->orderly_shutdown_triggered = true;
		dev_crit(power->dev, "Battery critical (empty flag set). Triggering orderly shutdown.\n");
		orderly_poweroff(true);
	}
}

static int macsmc_power_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct macsmc_power *power = container_of(nb, struct macsmc_power, nb);

	/*
	 * SMC Event IDs are correlated to physical events (e.g. charger
	 * connect/disconnect) but the exact meaning of each ID is predicted.
	 * 0x71... indicates power/battery events.
	 */
	if ((event & 0xffffff00) == 0x71010100 || /* Charger status change */
	    (event & 0xffff0000) == 0x71060000 || /* Port charge state change */
	    (event & 0xffff0000) == 0x71130000) { /* Connector insert/remove event */
		if (power->batt)
			power_supply_changed(power->batt);
		if (power->ac)
			power_supply_changed(power->ac);
		return NOTIFY_OK;
	} else if (event == 0x71020000) {
		/* Critical battery warning */
		if (power->batt)
			schedule_work(&power->critical_work);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int macsmc_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct apple_smc *smc = dev_get_drvdata(pdev->dev.parent);
	struct power_supply_config psy_cfg = {};
	struct macsmc_power *power;
	bool has_battery = false;
	bool has_ac_adapter = false;
	int ret = -ENODEV;
	bool flag;
	u16 vu16;
	u32 val32;
	enum power_supply_property *props;
	size_t nprops;

	if (!smc)
		return -ENODEV;

	power = devm_kzalloc(dev, sizeof(*power), GFP_KERNEL);
	if (!power)
		return -ENOMEM;

	power->dev = dev;
	power->smc = smc;
	dev_set_drvdata(dev, power);

	INIT_WORK(&power->critical_work, macsmc_power_critical_work);
	ret = devm_work_autocancel(dev, &power->critical_work, macsmc_power_critical_work);
	if (ret)
		return ret;

	/*
	 * Check for battery presence.
	 * B0AV is a fundamental key.
	 */
	if (apple_smc_read_u16(power->smc, SMC_KEY(B0AV), &vu16) == 0 &&
	    macsmc_battery_get_status(power) > POWER_SUPPLY_STATUS_UNKNOWN)
		has_battery = true;

	/*
	 * Check for AC adapter presence.
	 * CHIS is a fundamental key.
	 */
	if (apple_smc_key_exists(smc, SMC_KEY(CHIS)))
		has_ac_adapter = true;

	if (!has_battery && !has_ac_adapter)
		return -ENODEV;

	if (has_battery) {
		power->batt_desc = macsmc_battery_desc_template;
		props = devm_kcalloc(dev, MACSMC_MAX_BATT_PROPS,
				     sizeof(enum power_supply_property),
				     GFP_KERNEL);
		if (!props)
			return -ENOMEM;

		nprops = 0;

		/* Fundamental properties */
		props[nprops++] = POWER_SUPPLY_PROP_STATUS;
		props[nprops++] = POWER_SUPPLY_PROP_PRESENT;
		props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_CURRENT_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_POWER_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_CAPACITY;
		props[nprops++] = POWER_SUPPLY_PROP_CAPACITY_LEVEL;
		props[nprops++] = POWER_SUPPLY_PROP_TEMP;
		props[nprops++] = POWER_SUPPLY_PROP_CYCLE_COUNT;
		props[nprops++] = POWER_SUPPLY_PROP_HEALTH;
		props[nprops++] = POWER_SUPPLY_PROP_SCOPE;
		props[nprops++] = POWER_SUPPLY_PROP_MODEL_NAME;
		props[nprops++] = POWER_SUPPLY_PROP_SERIAL_NUMBER;
		props[nprops++] = POWER_SUPPLY_PROP_MANUFACTURE_YEAR;
		props[nprops++] = POWER_SUPPLY_PROP_MANUFACTURE_MONTH;
		props[nprops++] = POWER_SUPPLY_PROP_MANUFACTURE_DAY;

		/* Extended properties usually present */
		props[nprops++] = POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_TIME_TO_FULL_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN;
		props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN;
		props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_MIN;
		props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_MAX;
		props[nprops++] = POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT;
		props[nprops++] = POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX;
		props[nprops++] = POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE;
		props[nprops++] = POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN;
		props[nprops++] = POWER_SUPPLY_PROP_CHARGE_FULL;
		props[nprops++] = POWER_SUPPLY_PROP_CHARGE_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN;
		props[nprops++] = POWER_SUPPLY_PROP_ENERGY_FULL;
		props[nprops++] = POWER_SUPPLY_PROP_ENERGY_NOW;
		props[nprops++] = POWER_SUPPLY_PROP_CHARGE_COUNTER;

		/* Detect features based on key availability */
		if (apple_smc_key_exists(smc, SMC_KEY(CHTE)))
			power->has_chte = true;
		if (apple_smc_key_exists(smc, SMC_KEY(CH0C)))
			power->has_ch0c = true;
		if (apple_smc_key_exists(smc, SMC_KEY(CH0I)))
			power->has_ch0i = true;

		/* Reset "Optimised Battery Charging" flags to default state */
		if (power->has_chte)
			apple_smc_write_u32(smc, SMC_KEY(CHTE), 0);
		else if (power->has_ch0c)
			apple_smc_write_u8(smc, SMC_KEY(CH0C), 0);

		if (power->has_ch0i)
			apple_smc_write_u8(smc, SMC_KEY(CH0I), 0);

		apple_smc_write_u8(smc, SMC_KEY(CH0K), 0);
		apple_smc_write_u8(smc, SMC_KEY(CH0B), 0);

		/* Configure charge behaviour if supported */
		if (power->has_ch0i || power->has_ch0c || power->has_chte) {
			props[nprops++] = POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR;

			power->batt_desc.charge_behaviours =
				BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO);

			if (power->has_ch0i)
				power->batt_desc.charge_behaviours |=
					BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE);

			if (power->has_chte || power->has_ch0c)
				power->batt_desc.charge_behaviours |=
					BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE);
		}

		/* Detect charge limit method (CHWA vs CHLS) */
		if (apple_smc_read_flag(power->smc, SMC_KEY(CHWA), &flag) == 0)
			power->has_chwa = true;
		else if (apple_smc_read_u16(power->smc, SMC_KEY(CHLS), &vu16) >= 0)
			power->has_chls = true;

		if (nprops > MACSMC_MAX_BATT_PROPS)
			return -ENOMEM;

		power->batt_desc.properties = props;
		power->batt_desc.num_properties = nprops;

		/* Fetch identity strings */
		apple_smc_read(smc, SMC_KEY(BMDN), power->model_name,
			       sizeof(power->model_name) - 1);
		apple_smc_read(smc, SMC_KEY(BMSN), power->serial_number,
			       sizeof(power->serial_number) - 1);
		apple_smc_read(smc, SMC_KEY(BMDT), power->mfg_date,
			       sizeof(power->mfg_date) - 1);

		apple_smc_read_u8(power->smc, SMC_KEY(BNCB), &power->num_cells);
		power->nominal_voltage_mv = MACSMC_NOMINAL_CELL_VOLTAGE_MV * power->num_cells;

		/* Enable critical shutdown notifications by reading status once */
		apple_smc_read_u32(power->smc, SMC_KEY(BCF0), &val32);

		psy_cfg.drv_data = power;
		power->batt = devm_power_supply_register(dev, &power->batt_desc, &psy_cfg);
		if (IS_ERR(power->batt)) {
			dev_err_probe(dev, PTR_ERR(power->batt),
				      "Failed to register battery\n");
			/* Don't return failure yet; try AC registration first */
			power->batt = NULL;
		}
	}

	if (has_ac_adapter) {
		power->ac_desc = macsmc_ac_desc_template;
		props = devm_kcalloc(dev, MACSMC_MAX_AC_PROPS,
				     sizeof(enum power_supply_property),
				     GFP_KERNEL);
		if (!props)
			return -ENOMEM;

		nprops = 0;

		/* Online status is fundamental */
		props[nprops++] = POWER_SUPPLY_PROP_ONLINE;

		/* Input power limits are usually available */
		if (apple_smc_key_exists(power->smc, SMC_KEY(ACPW)))
			props[nprops++] = POWER_SUPPLY_PROP_INPUT_POWER_LIMIT;

		/* macOS 15.4+ firmware dropped legacy AC keys (AC-n, AC-i) */
		if (apple_smc_read_u16(power->smc, SMC_KEY(AC-n), &vu16) >= 0) {
			props[nprops++] = POWER_SUPPLY_PROP_VOLTAGE_NOW;
			props[nprops++] = POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT;
		}

		if (nprops > MACSMC_MAX_AC_PROPS)
			return -ENOMEM;

		power->ac_desc.properties = props;
		power->ac_desc.num_properties = nprops;

		psy_cfg.drv_data = power;
		power->ac = devm_power_supply_register(dev, &power->ac_desc, &psy_cfg);
		if (IS_ERR(power->ac)) {
			dev_err_probe(dev, PTR_ERR(power->ac),
				      "Failed to register AC adapter\n");
			power->ac = NULL;
		}
	}

	/* Final check: did we register anything? */
	if (!power->batt && !power->ac)
		return -ENODEV;

	power->nb.notifier_call = macsmc_power_event;
	blocking_notifier_chain_register(&smc->event_handlers, &power->nb);

	return 0;
}

static void macsmc_power_remove(struct platform_device *pdev)
{
	struct macsmc_power *power = dev_get_drvdata(&pdev->dev);

	blocking_notifier_chain_unregister(&power->smc->event_handlers, &power->nb);
}

static const struct platform_device_id macsmc_power_id[] = {
	{ "macsmc-power" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, macsmc_power_id);

static struct platform_driver macsmc_power_driver = {
	.driver = {
		.name = "macsmc-power",
	},
	.id_table = macsmc_power_id,
	.probe = macsmc_power_probe,
	.remove = macsmc_power_remove,
};
module_platform_driver(macsmc_power_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Apple SMC battery and power management driver");
MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_AUTHOR("Michael Reeves <michael.reeves077@gmail.com>");
