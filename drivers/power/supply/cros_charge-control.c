// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ChromeOS EC driver for charge control
 *
 *  Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */
#include <acpi/battery.h>
#include <linux/container_of.h>
#include <linux/dmi.h>
#include <linux/lockdep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define EC_CHARGE_CONTROL_BEHAVIOURS	(BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO)             | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)   | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE))

/*
 * Semantics of data *returned* from the EC API and Linux sysfs differ
 * slightly, also the v1 API can not return any data.
 * To match the expected sysfs API, data is never read back from the EC but
 * cached in the driver.
 *
 * Changes to the EC bypassing the driver will not be reflected in sysfs.
 * Any change to "charge_behaviour" will synchronize the EC with the driver state.
 */

struct cros_chctl_priv {
	struct device *dev;
	struct cros_ec_device *cros_ec;
	struct acpi_battery_hook battery_hook;
	struct power_supply *hooked_battery;
	u8 cmd_version;

	const struct power_supply_ext *psy_ext;

	struct mutex lock; /* protects fields below and cros_ec */
	enum power_supply_charge_behaviour current_behaviour;
	u8 current_start_threshold, current_end_threshold;
};

static int cros_chctl_send_charge_control_cmd(struct cros_ec_device *cros_ec,
					      u8 cmd_version, struct ec_params_charge_control *req)
{
	static const u8 outsizes[] = {
		[1] = offsetof(struct ec_params_charge_control, cmd),
		[2] = sizeof(struct ec_params_charge_control),
		[3] = sizeof(struct ec_params_charge_control),
	};

	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_charge_control req;
			struct ec_response_charge_control resp;
		} __packed data;
	} __packed buf = {
		.msg = {
			.command = EC_CMD_CHARGE_CONTROL,
			.version = cmd_version,
			.insize  = 0,
			.outsize = outsizes[cmd_version],
		},
		.data.req = *req,
	};

	return cros_ec_cmd_xfer_status(cros_ec, &buf.msg);
}

static int cros_chctl_configure_ec(struct cros_chctl_priv *priv)
{
	struct ec_params_charge_control req = {};

	lockdep_assert_held(&priv->lock);

	req.cmd = EC_CHARGE_CONTROL_CMD_SET;

	switch (priv->current_behaviour) {
	case POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO:
		req.mode = CHARGE_CONTROL_NORMAL;
		break;
	case POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE:
		req.mode = CHARGE_CONTROL_IDLE;
		break;
	case POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE:
		req.mode = CHARGE_CONTROL_DISCHARGE;
		break;
	default:
		return -EINVAL;
	}

	if (priv->current_behaviour == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO &&
	    !(priv->current_start_threshold == 0 && priv->current_end_threshold == 100)) {
		req.sustain_soc.lower = priv->current_start_threshold;
		req.sustain_soc.upper = priv->current_end_threshold;
	} else {
		/* Disable charging limits */
		req.sustain_soc.lower = -1;
		req.sustain_soc.upper = -1;
	}

	return cros_chctl_send_charge_control_cmd(priv->cros_ec, priv->cmd_version, &req);
}

static int cros_chctl_psy_ext_get_prop(struct power_supply *psy,
				       const struct power_supply_ext *ext,
				       void *data,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct cros_chctl_priv *priv = data;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		val->intval = priv->current_start_threshold;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		val->intval = priv->current_end_threshold;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		val->intval = priv->current_behaviour;
		return 0;
	default:
		return -EINVAL;
	}
}

static int cros_chctl_psy_ext_set_threshold(struct cros_chctl_priv *priv,
					    enum power_supply_property psp,
					    int val)
{
	int ret;

	if (val < 0 || val > 100)
		return -EINVAL;

	if (psp == POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD) {
		/* Start threshold is not exposed, use fixed value */
		if (priv->cmd_version == 2)
			priv->current_start_threshold = val == 100 ? 0 : val;

		if (val < priv->current_start_threshold)
			return -EINVAL;
		priv->current_end_threshold = val;
	} else {
		if (val > priv->current_end_threshold)
			return -EINVAL;
		priv->current_start_threshold = val;
	}

	if (priv->current_behaviour == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) {
		ret = cros_chctl_configure_ec(priv);
		if (ret < 0)
			return ret;
	}

	return 0;
}


static int cros_chctl_psy_ext_set_prop(struct power_supply *psy,
				       const struct power_supply_ext *ext,
				       void *data,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct cros_chctl_priv *priv = data;
	int ret;

	guard(mutex)(&priv->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		return cros_chctl_psy_ext_set_threshold(priv, psp, val->intval);
	case POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR:
		priv->current_behaviour = val->intval;
		ret = cros_chctl_configure_ec(priv);
		if (ret < 0)
			return ret;
		return 0;
	default:
		return -EINVAL;
	}
}

static int cros_chctl_psy_prop_is_writeable(struct power_supply *psy,
					    const struct power_supply_ext *ext,
					    void *data,
					    enum power_supply_property psp)
{
	return true;
}

#define DEFINE_CROS_CHCTL_POWER_SUPPLY_EXTENSION(_name, ...)			\
	static const enum power_supply_property _name ## _props[] = {		\
		__VA_ARGS__,							\
	};									\
										\
	static const struct power_supply_ext _name = {				\
		.name			= "cros-charge-control",		\
		.properties		= _name ## _props,			\
		.num_properties		= ARRAY_SIZE(_name ## _props),		\
		.charge_behaviours	= EC_CHARGE_CONTROL_BEHAVIOURS,		\
		.get_property		= cros_chctl_psy_ext_get_prop,		\
		.set_property		= cros_chctl_psy_ext_set_prop,		\
		.property_is_writeable	= cros_chctl_psy_prop_is_writeable,	\
	}

DEFINE_CROS_CHCTL_POWER_SUPPLY_EXTENSION(cros_chctl_psy_ext_v1,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR
);

DEFINE_CROS_CHCTL_POWER_SUPPLY_EXTENSION(cros_chctl_psy_ext_v2,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD
);

DEFINE_CROS_CHCTL_POWER_SUPPLY_EXTENSION(cros_chctl_psy_ext_v3,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD
);

static int cros_chctl_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct cros_chctl_priv *priv = container_of(hook, struct cros_chctl_priv, battery_hook);

	if (priv->hooked_battery)
		return 0;

	priv->hooked_battery = battery;
	return power_supply_register_extension(battery, priv->psy_ext, priv->dev, priv);
}

static int cros_chctl_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct cros_chctl_priv *priv = container_of(hook, struct cros_chctl_priv, battery_hook);

	if (priv->hooked_battery == battery) {
		power_supply_unregister_extension(battery, priv->psy_ext);
		priv->hooked_battery = NULL;
	}

	return 0;
}

static bool probe_with_fwk_charge_control;
module_param(probe_with_fwk_charge_control, bool, 0644);
MODULE_PARM_DESC(probe_with_fwk_charge_control,
		 "Probe the driver in the presence of the custom Framework EC charge control");

static int cros_chctl_fwk_charge_control_versions(struct cros_ec_device *cros_ec)
{
	if (!dmi_match(DMI_SYS_VENDOR, "Framework"))
		return 0;

	return cros_ec_get_cmd_versions(cros_ec, 0x3E03 /* FW_EC_CMD_CHARGE_LIMIT */);
}

static int cros_chctl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct cros_chctl_priv *priv;
	int ret;

	ret = cros_chctl_fwk_charge_control_versions(cros_ec);
	if (ret < 0)
		return ret;
	if (ret > 0 && !probe_with_fwk_charge_control) {
		dev_info(dev, "Framework charge control detected, preventing load\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return ret;

	ret = cros_ec_get_cmd_versions(cros_ec, EC_CMD_CHARGE_CONTROL);
	if (ret < 0)
		return ret;
	else if (ret & EC_VER_MASK(3))
		priv->cmd_version = 3;
	else if (ret & EC_VER_MASK(2))
		priv->cmd_version = 2;
	else if (ret & EC_VER_MASK(1))
		priv->cmd_version = 1;
	else
		return -ENODEV;

	dev_dbg(dev, "Command version: %u\n", (unsigned int)priv->cmd_version);

	priv->dev = dev;
	priv->cros_ec = cros_ec;

	if (priv->cmd_version == 1)
		priv->psy_ext = &cros_chctl_psy_ext_v1;
	else if (priv->cmd_version == 2)
		priv->psy_ext = &cros_chctl_psy_ext_v2;
	else
		priv->psy_ext = &cros_chctl_psy_ext_v3;

	priv->battery_hook.name = dev_name(dev);
	priv->battery_hook.add_battery = cros_chctl_add_battery;
	priv->battery_hook.remove_battery = cros_chctl_remove_battery;

	priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
	priv->current_start_threshold = 0;
	priv->current_end_threshold = 100;

	/* Bring EC into well-known state */
	scoped_guard(mutex, &priv->lock)
		ret = cros_chctl_configure_ec(priv);
	if (ret < 0)
		return ret;

	return devm_battery_hook_register(dev, &priv->battery_hook);
}

static const struct platform_device_id cros_chctl_id[] = {
	{ "cros-charge-control", 0 },
	{}
};

static struct platform_driver cros_chctl_driver = {
	.driver.name	= "cros-charge-control",
	.probe		= cros_chctl_probe,
	.id_table	= cros_chctl_id,
};
module_platform_driver(cros_chctl_driver);

MODULE_DEVICE_TABLE(platform, cros_chctl_id);
MODULE_DESCRIPTION("ChromeOS EC charge control");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net>");
MODULE_LICENSE("GPL");
