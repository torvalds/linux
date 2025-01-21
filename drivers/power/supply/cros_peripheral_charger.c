// SPDX-License-Identifier: GPL-2.0
/*
 * Power supply driver for ChromeOS EC based Peripheral Device Charger.
 *
 * Copyright 2020 Google LLC.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#define DRV_NAME		"cros-ec-pchg"
#define PCHG_DIR_PREFIX		"peripheral"
#define PCHG_DIR_NAME		PCHG_DIR_PREFIX "%d"
#define PCHG_DIR_NAME_LENGTH \
		sizeof(PCHG_DIR_PREFIX __stringify(EC_PCHG_MAX_PORTS))
#define PCHG_CACHE_UPDATE_DELAY	msecs_to_jiffies(500)

struct port_data {
	int port_number;
	char name[PCHG_DIR_NAME_LENGTH];
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	int psy_status;
	int battery_percentage;
	int charge_type;
	struct charger_data *charger;
	unsigned long last_update;
};

struct charger_data {
	struct device *dev;
	struct cros_ec_dev *ec_dev;
	struct cros_ec_device *ec_device;
	int num_registered_psy;
	struct port_data *ports[EC_PCHG_MAX_PORTS];
	struct notifier_block notifier;
};

static enum power_supply_property cros_pchg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
};

static int cros_pchg_ec_command(const struct charger_data *charger,
				unsigned int version,
				unsigned int command,
				const void *outdata,
				unsigned int outsize,
				void *indata,
				unsigned int insize)
{
	struct cros_ec_dev *ec_dev = charger->ec_dev;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(struct_size(msg, data, max(outsize, insize)), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = version;
	msg->command = ec_dev->cmd_offset + command;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, outdata, outsize);

	ret = cros_ec_cmd_xfer_status(charger->ec_device, msg);
	if (ret >= 0 && insize)
		memcpy(indata, msg->data, insize);

	kfree(msg);
	return ret;
}

static const unsigned int pchg_cmd_version = 1;

static bool cros_pchg_cmd_ver_check(const struct charger_data *charger)
{
	struct ec_params_get_cmd_versions_v1 req;
	struct ec_response_get_cmd_versions rsp;
	int ret;

	req.cmd = EC_CMD_PCHG;
	ret = cros_pchg_ec_command(charger, 1, EC_CMD_GET_CMD_VERSIONS,
				   &req, sizeof(req), &rsp, sizeof(rsp));
	if (ret < 0) {
		dev_warn(charger->dev,
			 "Unable to get versions of EC_CMD_PCHG (err:%d)\n",
			 ret);
		return false;
	}

	return !!(rsp.version_mask & BIT(pchg_cmd_version));
}

static int cros_pchg_port_count(const struct charger_data *charger)
{
	struct ec_response_pchg_count rsp;
	int ret;

	ret = cros_pchg_ec_command(charger, 0, EC_CMD_PCHG_COUNT,
				   NULL, 0, &rsp, sizeof(rsp));
	if (ret < 0) {
		dev_warn(charger->dev,
			 "Unable to get number or ports (err:%d)\n", ret);
		return ret;
	}

	return rsp.port_count;
}

static int cros_pchg_get_status(struct port_data *port)
{
	struct charger_data *charger = port->charger;
	struct ec_params_pchg req;
	struct ec_response_pchg rsp;
	struct device *dev = charger->dev;
	int old_status = port->psy_status;
	int old_percentage = port->battery_percentage;
	int ret;

	req.port = port->port_number;
	ret = cros_pchg_ec_command(charger, pchg_cmd_version, EC_CMD_PCHG,
				   &req, sizeof(req), &rsp, sizeof(rsp));
	if (ret < 0) {
		dev_err(dev, "Unable to get port.%d status (err:%d)\n",
			port->port_number, ret);
		return ret;
	}

	switch (rsp.state) {
	case PCHG_STATE_RESET:
	case PCHG_STATE_INITIALIZED:
	case PCHG_STATE_ENABLED:
	default:
		port->psy_status = POWER_SUPPLY_STATUS_UNKNOWN;
		port->charge_type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case PCHG_STATE_DETECTED:
		port->psy_status = POWER_SUPPLY_STATUS_CHARGING;
		port->charge_type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case PCHG_STATE_CHARGING:
		port->psy_status = POWER_SUPPLY_STATUS_CHARGING;
		port->charge_type = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	case PCHG_STATE_FULL:
		port->psy_status = POWER_SUPPLY_STATUS_FULL;
		port->charge_type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}

	port->battery_percentage = rsp.battery_percentage;

	if (port->psy_status != old_status ||
			port->battery_percentage != old_percentage)
		power_supply_changed(port->psy);

	dev_dbg(dev,
		"Port %d: state=%d battery=%d%%\n",
		port->port_number, rsp.state, rsp.battery_percentage);

	return 0;
}

static int cros_pchg_get_port_status(struct port_data *port, bool ratelimit)
{
	int ret;

	if (ratelimit &&
	    time_is_after_jiffies(port->last_update + PCHG_CACHE_UPDATE_DELAY))
		return 0;

	ret = cros_pchg_get_status(port);
	if (ret < 0)
		return ret;

	port->last_update = jiffies;

	return ret;
}

static int cros_pchg_get_prop(struct power_supply *psy,
			      enum power_supply_property psp,
			      union power_supply_propval *val)
{
	struct port_data *port = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		cros_pchg_get_port_status(port, true);
		break;
	default:
		break;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = port->psy_status;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = port->battery_percentage;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = port->charge_type;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cros_pchg_event(const struct charger_data *charger)
{
	int i;

	for (i = 0; i < charger->num_registered_psy; i++)
		cros_pchg_get_port_status(charger->ports[i], false);

	return NOTIFY_OK;
}

static int cros_ec_notify(struct notifier_block *nb,
			  unsigned long queued_during_suspend,
			  void *data)
{
	struct cros_ec_device *ec_dev = data;
	struct charger_data *charger =
			container_of(nb, struct charger_data, notifier);
	u32 host_event;

	if (ec_dev->event_data.event_type != EC_MKBP_EVENT_PCHG ||
			ec_dev->event_size != sizeof(host_event))
		return NOTIFY_DONE;

	host_event = get_unaligned_le32(&ec_dev->event_data.data.host_event);

	if (!(host_event & EC_MKBP_PCHG_DEVICE_EVENT))
		return NOTIFY_DONE;

	return cros_pchg_event(charger);
}

static int cros_pchg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *ec_device = ec_dev->ec_dev;
	struct power_supply_desc *psy_desc;
	struct charger_data *charger;
	struct power_supply *psy;
	struct port_data *port;
	struct notifier_block *nb;
	int num_ports;
	int ret;
	int i;

	charger = devm_kzalloc(dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->dev = dev;
	charger->ec_dev = ec_dev;
	charger->ec_device = ec_device;

	platform_set_drvdata(pdev, charger);

	ret = cros_pchg_port_count(charger);
	if (ret <= 0) {
		/*
		 * This feature is enabled by the EC and the kernel driver is
		 * included by default for CrOS devices. Don't need to be loud
		 * since this error can be normal.
		 */
		dev_info(dev, "No peripheral charge ports (err:%d)\n", ret);
		return -ENODEV;
	}

	if (!cros_pchg_cmd_ver_check(charger)) {
		dev_err(dev, "EC_CMD_PCHG version %d isn't available.\n",
			pchg_cmd_version);
		return -EOPNOTSUPP;
	}

	num_ports = ret;
	if (num_ports > EC_PCHG_MAX_PORTS) {
		dev_err(dev, "Too many peripheral charge ports (%d)\n",
			num_ports);
		return -ENOBUFS;
	}

	dev_info(dev, "%d peripheral charge ports found\n", num_ports);

	for (i = 0; i < num_ports; i++) {
		struct power_supply_config psy_cfg = {};

		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return -ENOMEM;

		port->charger = charger;
		port->port_number = i;
		snprintf(port->name, sizeof(port->name), PCHG_DIR_NAME, i);

		psy_desc = &port->psy_desc;
		psy_desc->name = port->name;
		psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
		psy_desc->get_property = cros_pchg_get_prop;
		psy_desc->external_power_changed = NULL;
		psy_desc->properties = cros_pchg_props;
		psy_desc->num_properties = ARRAY_SIZE(cros_pchg_props);
		psy_cfg.drv_data = port;

		psy = devm_power_supply_register(dev, psy_desc, &psy_cfg);
		if (IS_ERR(psy))
			return dev_err_probe(dev, PTR_ERR(psy),
					"Failed to register power supply\n");
		port->psy = psy;

		charger->ports[charger->num_registered_psy++] = port;
	}

	if (!charger->num_registered_psy)
		return -ENODEV;

	nb = &charger->notifier;
	nb->notifier_call = cros_ec_notify;
	ret = blocking_notifier_chain_register(&ec_dev->ec_dev->event_notifier,
					       nb);
	if (ret < 0)
		dev_err(dev, "Failed to register notifier (err:%d)\n", ret);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int __maybe_unused cros_pchg_resume(struct device *dev)
{
	struct charger_data *charger = dev_get_drvdata(dev);

	/*
	 * Sync all ports on resume in case reports from EC are lost during
	 * the last suspend.
	 */
	cros_pchg_event(charger);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cros_pchg_pm_ops, NULL, cros_pchg_resume);

static const struct platform_device_id cros_pchg_id[] = {
	{ DRV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_pchg_id);

static struct platform_driver cros_pchg_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_pchg_pm_ops,
	},
	.probe = cros_pchg_probe,
	.id_table = cros_pchg_id,
};

module_platform_driver(cros_pchg_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC peripheral device charger");
