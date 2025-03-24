// SPDX-License-Identifier: GPL-2.0
/*
 * Power Supply for UCSI
 *
 * Copyright (C) 2020, Intel Corporation
 * Author: K V, Abhilash <abhilash.k.v@intel.com>
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/property.h>
#include <linux/usb/pd.h>

#include "ucsi.h"

/* Power Supply access to expose source power information */
enum ucsi_psy_online_states {
	UCSI_PSY_OFFLINE = 0,
	UCSI_PSY_FIXED_ONLINE,
	UCSI_PSY_PROG_ONLINE,
};

static enum power_supply_property ucsi_psy_props[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_SCOPE,
};

static int ucsi_psy_get_scope(struct ucsi_connector *con,
			      union power_supply_propval *val)
{
	u8 scope = POWER_SUPPLY_SCOPE_UNKNOWN;
	struct device *dev = con->ucsi->dev;

	device_property_read_u8(dev, "scope", &scope);
	if (scope == POWER_SUPPLY_SCOPE_UNKNOWN) {
		u32 mask = UCSI_CAP_ATTR_POWER_AC_SUPPLY |
			   UCSI_CAP_ATTR_BATTERY_CHARGING;

		if (con->ucsi->cap.attributes & mask)
			scope = POWER_SUPPLY_SCOPE_SYSTEM;
		else
			scope = POWER_SUPPLY_SCOPE_DEVICE;
	}
	val->intval = scope;
	return 0;
}

static int ucsi_psy_get_online(struct ucsi_connector *con,
			       union power_supply_propval *val)
{
	val->intval = UCSI_PSY_OFFLINE;
	if (UCSI_CONSTAT(con, CONNECTED) &&
	    (UCSI_CONSTAT(con, PWR_DIR) == TYPEC_SINK))
		val->intval = UCSI_PSY_FIXED_ONLINE;
	return 0;
}

static int ucsi_psy_get_voltage_min(struct ucsi_connector *con,
				    union power_supply_propval *val)
{
	u32 pdo;

	switch (UCSI_CONSTAT(con, PWR_OPMODE)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		pdo = con->src_pdos[0];
		val->intval = pdo_fixed_voltage(pdo) * 1000;
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0:
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5:
	case UCSI_CONSTAT_PWR_OPMODE_BC:
	case UCSI_CONSTAT_PWR_OPMODE_DEFAULT:
		val->intval = UCSI_TYPEC_VSAFE5V * 1000;
		break;
	default:
		val->intval = 0;
		break;
	}
	return 0;
}

static int ucsi_psy_get_voltage_max(struct ucsi_connector *con,
				    union power_supply_propval *val)
{
	u32 pdo;

	switch (UCSI_CONSTAT(con, PWR_OPMODE)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		if (con->num_pdos > 0) {
			pdo = con->src_pdos[con->num_pdos - 1];
			val->intval = pdo_fixed_voltage(pdo) * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0:
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5:
	case UCSI_CONSTAT_PWR_OPMODE_BC:
	case UCSI_CONSTAT_PWR_OPMODE_DEFAULT:
		val->intval = UCSI_TYPEC_VSAFE5V * 1000;
		break;
	default:
		val->intval = 0;
		break;
	}
	return 0;
}

static int ucsi_psy_get_voltage_now(struct ucsi_connector *con,
				    union power_supply_propval *val)
{
	int index;
	u32 pdo;

	switch (UCSI_CONSTAT(con, PWR_OPMODE)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		index = rdo_index(con->rdo);
		if (index > 0) {
			pdo = con->src_pdos[index - 1];
			val->intval = pdo_fixed_voltage(pdo) * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0:
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5:
	case UCSI_CONSTAT_PWR_OPMODE_BC:
	case UCSI_CONSTAT_PWR_OPMODE_DEFAULT:
		val->intval = UCSI_TYPEC_VSAFE5V * 1000;
		break;
	default:
		val->intval = 0;
		break;
	}
	return 0;
}

static int ucsi_psy_get_current_max(struct ucsi_connector *con,
				    union power_supply_propval *val)
{
	u32 pdo;

	switch (UCSI_CONSTAT(con, PWR_OPMODE)) {
	case UCSI_CONSTAT_PWR_OPMODE_PD:
		if (con->num_pdos > 0) {
			pdo = con->src_pdos[con->num_pdos - 1];
			val->intval = pdo_max_current(pdo) * 1000;
		} else {
			val->intval = 0;
		}
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC1_5:
		val->intval = UCSI_TYPEC_1_5_CURRENT * 1000;
		break;
	case UCSI_CONSTAT_PWR_OPMODE_TYPEC3_0:
		val->intval = UCSI_TYPEC_3_0_CURRENT * 1000;
		break;
	case UCSI_CONSTAT_PWR_OPMODE_BC:
	case UCSI_CONSTAT_PWR_OPMODE_DEFAULT:
	/* UCSI can't tell b/w DCP/CDP or USB2/3x1/3x2 SDP chargers */
	default:
		val->intval = 0;
		break;
	}
	return 0;
}

static int ucsi_psy_get_current_now(struct ucsi_connector *con,
				    union power_supply_propval *val)
{
	if (UCSI_CONSTAT(con, PWR_OPMODE) == UCSI_CONSTAT_PWR_OPMODE_PD)
		val->intval = rdo_op_current(con->rdo) * 1000;
	else
		val->intval = 0;
	return 0;
}

static int ucsi_psy_get_usb_type(struct ucsi_connector *con,
				 union power_supply_propval *val)
{
	val->intval = POWER_SUPPLY_USB_TYPE_C;
	if (UCSI_CONSTAT(con, CONNECTED) &&
	    UCSI_CONSTAT(con, PWR_OPMODE) == UCSI_CONSTAT_PWR_OPMODE_PD)
		val->intval = POWER_SUPPLY_USB_TYPE_PD;

	return 0;
}

static int ucsi_psy_get_charge_type(struct ucsi_connector *con, union power_supply_propval *val)
{
	if (!(UCSI_CONSTAT(con, CONNECTED))) {
		val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		return 0;
	}

	/* The Battery Charging Cabability Status field is only valid in sink role. */
	if (UCSI_CONSTAT(con, PWR_DIR) != TYPEC_SINK) {
		val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		return 0;
	}

	switch (UCSI_CONSTAT(con, BC_STATUS)) {
	case UCSI_CONSTAT_BC_NOMINAL_CHARGING:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	case UCSI_CONSTAT_BC_SLOW_CHARGING:
	case UCSI_CONSTAT_BC_TRICKLE_CHARGING:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	default:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}

	return 0;
}

static int ucsi_psy_get_prop(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct ucsi_connector *con = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		return ucsi_psy_get_charge_type(con, val);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return ucsi_psy_get_usb_type(con, val);
	case POWER_SUPPLY_PROP_ONLINE:
		return ucsi_psy_get_online(con, val);
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		return ucsi_psy_get_voltage_min(con, val);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return ucsi_psy_get_voltage_max(con, val);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return ucsi_psy_get_voltage_now(con, val);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return ucsi_psy_get_current_max(con, val);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return ucsi_psy_get_current_now(con, val);
	case POWER_SUPPLY_PROP_SCOPE:
		return ucsi_psy_get_scope(con, val);
	default:
		return -EINVAL;
	}
}

int ucsi_register_port_psy(struct ucsi_connector *con)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = con->ucsi->dev;
	char *psy_name;

	psy_cfg.drv_data = con;
	psy_cfg.fwnode = dev_fwnode(dev);

	psy_name = devm_kasprintf(dev, GFP_KERNEL, "ucsi-source-psy-%s%d",
				  dev_name(dev), con->num);
	if (!psy_name)
		return -ENOMEM;

	con->psy_desc.name = psy_name;
	con->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	con->psy_desc.usb_types = BIT(POWER_SUPPLY_USB_TYPE_C)  |
				  BIT(POWER_SUPPLY_USB_TYPE_PD) |
				  BIT(POWER_SUPPLY_USB_TYPE_PD_PPS);
	con->psy_desc.properties = ucsi_psy_props;
	con->psy_desc.num_properties = ARRAY_SIZE(ucsi_psy_props);
	con->psy_desc.get_property = ucsi_psy_get_prop;

	con->psy = power_supply_register(dev, &con->psy_desc, &psy_cfg);

	return PTR_ERR_OR_ZERO(con->psy);
}

void ucsi_unregister_port_psy(struct ucsi_connector *con)
{
	if (IS_ERR_OR_NULL(con->psy))
		return;

	power_supply_unregister(con->psy);
	con->psy = NULL;
}

void ucsi_port_psy_changed(struct ucsi_connector *con)
{
	if (IS_ERR_OR_NULL(con->psy))
		return;

	power_supply_changed(con->psy);
}
