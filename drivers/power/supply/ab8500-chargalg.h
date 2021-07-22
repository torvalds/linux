/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2012
 * Author: Johan Gardsmark <johan.gardsmark@stericsson.com> for ST-Ericsson.
 */

#ifndef _AB8500_CHARGALG_H_
#define _AB8500_CHARGALG_H_

#include <linux/power_supply.h>

/*
 * Valid only for supplies of type:
 * - POWER_SUPPLY_TYPE_MAINS,
 * - POWER_SUPPLY_TYPE_USB,
 * because only them store as drv_data pointer to struct ux500_charger.
 */
#define psy_to_ux500_charger(x) power_supply_get_drvdata(x)

/* Forward declaration */
struct ux500_charger;

struct ux500_charger_ops {
	int (*enable) (struct ux500_charger *, int, int, int);
	int (*check_enable) (struct ux500_charger *, int, int);
	int (*kick_wd) (struct ux500_charger *);
	int (*update_curr) (struct ux500_charger *, int);
};

/**
 * struct ux500_charger - power supply ux500 charger sub class
 * @psy			power supply base class
 * @ops			ux500 charger operations
 * @max_out_volt	maximum output charger voltage in mV
 * @max_out_curr	maximum output charger current in mA
 * @enabled		indicates if this charger is used or not
 * @external		external charger unit (pm2xxx)
 */
struct ux500_charger {
	struct power_supply *psy;
	struct ux500_charger_ops ops;
	int max_out_volt;
	int max_out_curr;
	int wdt_refresh;
	bool enabled;
	bool external;
};

extern struct blocking_notifier_head charger_notifier_list;

#endif /* _AB8500_CHARGALG_H_ */
