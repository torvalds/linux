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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/types.h>

#define EC_CHARGE_CONTROL_BEHAVIOURS	(BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO)             | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE)   | \
					 BIT(POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE))

/*
 * Semantics of data *returned* from the EC API and Linux sysfs differ
 * slightly:
 *  - EC sustainer off is lower=upper=-1
 *  - Linux "no limit" is start=0, end=100
 * Also the v1 API can not return any data.
 *
 * While the sustainer is active the EC may report IDLE or DISCHARGE as the
 * current charge-control mode; that is an internal hold/discharge step, not a
 * Linux inhibit-charge / force-discharge request. Valid sustainer limits are
 * therefore adopted as AUTO with the reported thresholds.
 *
 * Sysfs reads come from a driver-side cache. On probe, command versions that
 * support GET (v2+) are initialized from the EC so firmware or firmware-setup
 * programmed limits and modes are preserved (unlike earlier behaviour that
 * always forced AUTO with no limits). v1 still forces a well-known EC state.
 * Subsequent sysfs writes keep the cache and EC in sync; changes that bypass
 * the driver are not reflected until the next probe.
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
					      u8 cmd_version,
					      struct ec_params_charge_control *req,
					      struct ec_response_charge_control *resp)
{
	static const u8 outsizes[] = {
		[1] = offsetof(struct ec_params_charge_control, cmd),
		[2] = sizeof(struct ec_params_charge_control),
		[3] = sizeof(struct ec_params_charge_control),
	};
	size_t insize = resp ? sizeof(*resp) : 0;
	int ret;

	if (resp)
		*resp = (struct ec_response_charge_control){};

	ret = cros_ec_cmd(cros_ec, cmd_version, EC_CMD_CHARGE_CONTROL, req,
			  outsizes[cmd_version], resp, insize);
	if (ret < 0)
		return ret;

	return 0;
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

	return cros_chctl_send_charge_control_cmd(priv->cros_ec, priv->cmd_version,
						  &req, NULL);
}

static int cros_chctl_get_ec_status(struct cros_chctl_priv *priv,
				    struct ec_response_charge_control *resp)
{
	struct ec_params_charge_control req = {
		.cmd = EC_CHARGE_CONTROL_CMD_GET,
	};

	return cros_chctl_send_charge_control_cmd(priv->cros_ec, priv->cmd_version,
						  &req, resp);
}

static void cros_chctl_set_default_state(struct cros_chctl_priv *priv)
{
	lockdep_assert_held(&priv->lock);

	priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
	priv->current_start_threshold = 0;
	priv->current_end_threshold = 100;
}

static bool cros_chctl_sustainer_limits_valid(s8 lower, s8 upper)
{
	return lower >= 0 && upper >= 0 && lower <= 100 && upper <= 100 && lower <= upper;
}

static int cros_chctl_adopt_ec_mode(struct cros_chctl_priv *priv, u32 mode)
{
	lockdep_assert_held(&priv->lock);

	switch (mode) {
	case CHARGE_CONTROL_NORMAL:
		priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		return 0;
	case CHARGE_CONTROL_IDLE:
		priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE;
		return 0;
	case CHARGE_CONTROL_DISCHARGE:
		priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE;
		return 0;
	default:
		dev_warn(priv->dev, "unknown charge control mode %u\n", mode);
		return -EINVAL;
	}
}

static int cros_chctl_init_state(struct cros_chctl_priv *priv)
{
	struct ec_response_charge_control resp;
	s8 lower, upper;
	int ret;

	guard(mutex)(&priv->lock);

	/* v1 cannot report current state; force a well-known EC configuration. */
	if (priv->cmd_version < 2) {
		cros_chctl_set_default_state(priv);
		return cros_chctl_configure_ec(priv);
	}

	ret = cros_chctl_get_ec_status(priv, &resp);
	if (ret < 0) {
		dev_warn(priv->dev,
			 "failed to read EC charge state (%pe), applying defaults\n",
			 ERR_PTR(ret));
		goto defaults;
	}

	lower = resp.sustain_soc.lower;
	upper = resp.sustain_soc.upper;

	/*
	 * Valid sustainer limits mean "auto with thresholds". The EC mode may
	 * be IDLE/DISCHARGE while the sustainer holds or bleeds SoC; do not
	 * expose that as inhibit-charge / force-discharge.
	 */
	if (cros_chctl_sustainer_limits_valid(lower, upper)) {
		priv->current_behaviour = POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO;
		priv->current_start_threshold = lower;
		priv->current_end_threshold = upper;
	} else {
		ret = cros_chctl_adopt_ec_mode(priv, resp.mode);
		if (ret < 0)
			goto defaults;

		/*
		 * Sustainer off is lower=upper=-1 → Linux "no limit" (0/100).
		 * Any other non-valid pair is unexpected; remap, warn, and
		 * push the cleaned state back to the EC.
		 */
		priv->current_start_threshold = 0;
		priv->current_end_threshold = 100;

		if (!(lower == -1 && upper == -1)) {
			dev_warn(priv->dev,
				 "invalid EC sustainer limits (%d/%d), treating as no limit\n",
				 lower, upper);
			return cros_chctl_configure_ec(priv);
		}
	}

	dev_dbg(priv->dev,
		"adopted EC charge state: behaviour=%d start=%u end=%u (ec mode=%u)\n",
		priv->current_behaviour, priv->current_start_threshold,
		priv->current_end_threshold, resp.mode);

	return 0;

defaults:
	cros_chctl_set_default_state(priv);
	return cros_chctl_configure_ec(priv);
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
	dev_set_drvdata(dev, priv);

	if (priv->cmd_version == 1)
		priv->psy_ext = &cros_chctl_psy_ext_v1;
	else if (priv->cmd_version == 2)
		priv->psy_ext = &cros_chctl_psy_ext_v2;
	else
		priv->psy_ext = &cros_chctl_psy_ext_v3;

	priv->battery_hook.name = dev_name(dev);
	priv->battery_hook.add_battery = cros_chctl_add_battery;
	priv->battery_hook.remove_battery = cros_chctl_remove_battery;

	ret = cros_chctl_init_state(priv);
	if (ret < 0)
		return ret;

	return devm_battery_hook_register(dev, &priv->battery_hook);
}

static int cros_chctl_resume(struct device *dev)
{
	struct cros_chctl_priv *priv = dev_get_drvdata(dev);
	int ret;

	/*
	 * The EC resets its charge control state when the AP powers off, so the
	 * configuration is lost across hibernation, while the values exposed to
	 * userspace still describe it. The cached state is the only remaining
	 * record of that configuration: reprogram the EC from it rather than
	 * re-reading the EC, which would adopt the reset defaults instead.
	 */
	guard(mutex)(&priv->lock);
	ret = cros_chctl_configure_ec(priv);
	if (ret)
		dev_err(dev, "failed to restore charge control state: %d\n", ret);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(cros_chctl_pm_ops, NULL, cros_chctl_resume);

static const struct platform_device_id cros_chctl_id[] = {
	{ .name = "cros-charge-control" },
	{ }
};
MODULE_DEVICE_TABLE(platform, cros_chctl_id);

static struct platform_driver cros_chctl_driver = {
	.driver = {
		.name	= "cros-charge-control",
		.pm	= pm_sleep_ptr(&cros_chctl_pm_ops),
	},
	.probe		= cros_chctl_probe,
	.id_table	= cros_chctl_id,
};
module_platform_driver(cros_chctl_driver);

MODULE_DESCRIPTION("ChromeOS EC charge control");
MODULE_AUTHOR("Thomas Weißschuh <linux@weissschuh.net>");
MODULE_LICENSE("GPL");
