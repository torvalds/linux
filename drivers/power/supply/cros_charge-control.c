// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ChromeOS EC driver for charge control
 *
 *  Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */
#include <acpi/battery.h>
#include <linux/container_of.h>
#include <linux/dmi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define EC_CHARGE_CONTROL_BEHAVIOURS	(BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO)             | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)   | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE))

enum CROS_CHCTL_ATTR {
	CROS_CHCTL_ATTR_START_THRESHOLD,
	CROS_CHCTL_ATTR_END_THRESHOLD,
	CROS_CHCTL_ATTR_CHARGE_BEHAVIOUR,
	_CROS_CHCTL_ATTR_COUNT
};

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
	struct cros_ec_device *cros_ec;
	struct acpi_battery_hook battery_hook;
	struct power_supply *hooked_battery;
	u8 cmd_version;

	/* The callbacks need to access this priv structure.
	 * As neither the struct device nor power_supply are under the drivers
	 * control, embed the attributes within priv to use with container_of().
	 */
	struct device_attribute device_attrs[_CROS_CHCTL_ATTR_COUNT];
	struct attribute *attributes[_CROS_CHCTL_ATTR_COUNT];
	struct attribute_group group;

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

static struct cros_chctl_priv *cros_chctl_attr_to_priv(struct attribute *attr,
						       enum CROS_CHCTL_ATTR idx)
{
	struct device_attribute *dev_attr = container_of(attr, struct device_attribute, attr);

	return container_of(dev_attr, struct cros_chctl_priv, device_attrs[idx]);
}

static ssize_t cros_chctl_store_threshold(struct device *dev, struct cros_chctl_priv *priv,
					  int is_end_threshold, const char *buf, size_t count)
{
	int ret, val;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;
	if (val < 0 || val > 100)
		return -EINVAL;

	if (is_end_threshold) {
		if (val <= priv->current_start_threshold)
			return -EINVAL;
		priv->current_end_threshold = val;
	} else {
		if (val >= priv->current_end_threshold)
			return -EINVAL;
		priv->current_start_threshold = val;
	}

	if (priv->current_behaviour == POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO) {
		ret = cros_chctl_configure_ec(priv);
		if (ret < 0)
			return ret;
	}

	return count;
}

static ssize_t charge_control_start_threshold_show(struct device *dev,
						   struct device_attribute *attr,
						   char *buf)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_START_THRESHOLD);

	return sysfs_emit(buf, "%u\n", (unsigned int)priv->current_start_threshold);
}

static ssize_t charge_control_start_threshold_store(struct device *dev,
						    struct device_attribute *attr,
						    const char *buf, size_t count)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_START_THRESHOLD);

	return cros_chctl_store_threshold(dev, priv, 0, buf, count);
}

static ssize_t charge_control_end_threshold_show(struct device *dev, struct device_attribute *attr,
						 char *buf)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_END_THRESHOLD);

	return sysfs_emit(buf, "%u\n", (unsigned int)priv->current_end_threshold);
}

static ssize_t charge_control_end_threshold_store(struct device *dev, struct device_attribute *attr,
						  const char *buf, size_t count)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_END_THRESHOLD);

	return cros_chctl_store_threshold(dev, priv, 1, buf, count);
}

static ssize_t charge_behaviour_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_CHARGE_BEHAVIOUR);

	return power_supply_charge_behaviour_show(dev, EC_CHARGE_CONTROL_BEHAVIOURS,
						  priv->current_behaviour, buf);
}

static ssize_t charge_behaviour_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(&attr->attr,
							       CROS_CHCTL_ATTR_CHARGE_BEHAVIOUR);
	int ret;

	ret = power_supply_charge_behaviour_parse(EC_CHARGE_CONTROL_BEHAVIOURS, buf);
	if (ret < 0)
		return ret;

	priv->current_behaviour = ret;

	ret = cros_chctl_configure_ec(priv);
	if (ret < 0)
		return ret;

	return count;
}

static umode_t cros_chtl_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct cros_chctl_priv *priv = cros_chctl_attr_to_priv(attr, n);

	if (priv->cmd_version < 2) {
		if (n == CROS_CHCTL_ATTR_START_THRESHOLD)
			return 0;
		if (n == CROS_CHCTL_ATTR_END_THRESHOLD)
			return 0;
	}

	return attr->mode;
}

static int cros_chctl_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct cros_chctl_priv *priv = container_of(hook, struct cros_chctl_priv, battery_hook);

	if (priv->hooked_battery)
		return 0;

	priv->hooked_battery = battery;
	return device_add_group(&battery->dev, &priv->group);
}

static int cros_chctl_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct cros_chctl_priv *priv = container_of(hook, struct cros_chctl_priv, battery_hook);

	if (priv->hooked_battery == battery) {
		device_remove_group(&battery->dev, &priv->group);
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
	size_t i;
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

	priv->cros_ec = cros_ec;
	priv->device_attrs[CROS_CHCTL_ATTR_START_THRESHOLD] =
		(struct device_attribute)__ATTR_RW(charge_control_start_threshold);
	priv->device_attrs[CROS_CHCTL_ATTR_END_THRESHOLD] =
		(struct device_attribute)__ATTR_RW(charge_control_end_threshold);
	priv->device_attrs[CROS_CHCTL_ATTR_CHARGE_BEHAVIOUR] =
		(struct device_attribute)__ATTR_RW(charge_behaviour);
	for (i = 0; i < _CROS_CHCTL_ATTR_COUNT; i++) {
		sysfs_attr_init(&priv->device_attrs[i].attr);
		priv->attributes[i] = &priv->device_attrs[i].attr;
	}
	priv->group.is_visible = cros_chtl_attr_is_visible;
	priv->group.attrs = priv->attributes;

	priv->battery_hook.name = dev_name(dev);
	priv->battery_hook.add_battery = cros_chctl_add_battery;
	priv->battery_hook.remove_battery = cros_chctl_remove_battery;

	priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
	priv->current_start_threshold = 0;
	priv->current_end_threshold = 100;

	/* Bring EC into well-known state */
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
