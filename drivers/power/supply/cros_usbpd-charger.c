// SPDX-License-Identifier: GPL-2.0+
/*
 * Power supply driver for ChromeOS EC based USB PD Charger.
 *
 * Copyright (c) 2014 - 2018 Google, Inc
 */

#include <linux/mfd/cros_ec.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#define CHARGER_USBPD_DIR_NAME			"CROS_USBPD_CHARGER%d"
#define CHARGER_DEDICATED_DIR_NAME		"CROS_DEDICATED_CHARGER"
#define CHARGER_DIR_NAME_LENGTH		(sizeof(CHARGER_USBPD_DIR_NAME) >= \
					 sizeof(CHARGER_DEDICATED_DIR_NAME) ? \
					 sizeof(CHARGER_USBPD_DIR_NAME) : \
					 sizeof(CHARGER_DEDICATED_DIR_NAME))
#define CHARGER_CACHE_UPDATE_DELAY		msecs_to_jiffies(500)
#define CHARGER_MANUFACTURER_MODEL_LENGTH	32

#define DRV_NAME "cros-usbpd-charger"

struct port_data {
	int port_number;
	char name[CHARGER_DIR_NAME_LENGTH];
	char manufacturer[CHARGER_MANUFACTURER_MODEL_LENGTH];
	char model_name[CHARGER_MANUFACTURER_MODEL_LENGTH];
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	int psy_usb_type;
	int psy_online;
	int psy_status;
	int psy_current_max;
	int psy_voltage_max_design;
	int psy_voltage_now;
	int psy_power_max;
	struct charger_data *charger;
	unsigned long last_update;
};

struct charger_data {
	struct device *dev;
	struct cros_ec_dev *ec_dev;
	struct cros_ec_device *ec_device;
	int num_charger_ports;
	int num_usbpd_ports;
	int num_registered_psy;
	struct port_data *ports[EC_USB_PD_MAX_PORTS];
	struct notifier_block notifier;
};

static enum power_supply_property cros_usbpd_charger_props[] = {
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_USB_TYPE
};

static enum power_supply_property cros_usbpd_dedicated_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_usb_type cros_usbpd_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

/* Input voltage/current limit in mV/mA. Default to none. */
static u16 input_voltage_limit = EC_POWER_LIMIT_NONE;
static u16 input_current_limit = EC_POWER_LIMIT_NONE;

static bool cros_usbpd_charger_port_is_dedicated(struct port_data *port)
{
	return port->port_number >= port->charger->num_usbpd_ports;
}

static int cros_usbpd_charger_ec_command(struct charger_data *charger,
					 unsigned int version,
					 unsigned int command,
					 void *outdata,
					 unsigned int outsize,
					 void *indata,
					 unsigned int insize)
{
	struct cros_ec_dev *ec_dev = charger->ec_dev;
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + max(outsize, insize), GFP_KERNEL);
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

static int cros_usbpd_charger_get_num_ports(struct charger_data *charger)
{
	struct ec_response_charge_port_count resp;
	int ret;

	ret = cros_usbpd_charger_ec_command(charger, 0,
					    EC_CMD_CHARGE_PORT_COUNT,
					    NULL, 0, &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	return resp.port_count;
}

static int cros_usbpd_charger_get_usbpd_num_ports(struct charger_data *charger)
{
	struct ec_response_usb_pd_ports resp;
	int ret;

	ret = cros_usbpd_charger_ec_command(charger, 0, EC_CMD_USB_PD_PORTS,
					    NULL, 0, &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	return resp.num_ports;
}

static int cros_usbpd_charger_get_discovery_info(struct port_data *port)
{
	struct charger_data *charger = port->charger;
	struct ec_params_usb_pd_discovery_entry resp;
	struct ec_params_usb_pd_info_request req;
	int ret;

	req.port = port->port_number;

	ret = cros_usbpd_charger_ec_command(charger, 0,
					    EC_CMD_USB_PD_DISCOVERY,
					    &req, sizeof(req),
					    &resp, sizeof(resp));
	if (ret < 0) {
		dev_err(charger->dev,
			"Unable to query discovery info (err:0x%x)\n", ret);
		return ret;
	}

	dev_dbg(charger->dev, "Port %d: VID = 0x%x, PID=0x%x, PTYPE=0x%x\n",
		port->port_number, resp.vid, resp.pid, resp.ptype);

	snprintf(port->manufacturer, sizeof(port->manufacturer), "%x",
		 resp.vid);
	snprintf(port->model_name, sizeof(port->model_name), "%x", resp.pid);

	return 0;
}

static int cros_usbpd_charger_get_power_info(struct port_data *port)
{
	struct charger_data *charger = port->charger;
	struct ec_response_usb_pd_power_info resp;
	struct ec_params_usb_pd_power_info req;
	int last_psy_status, last_psy_usb_type;
	struct device *dev = charger->dev;
	int ret;

	req.port = port->port_number;
	ret = cros_usbpd_charger_ec_command(charger, 0,
					    EC_CMD_USB_PD_POWER_INFO,
					    &req, sizeof(req),
					    &resp, sizeof(resp));
	if (ret < 0) {
		dev_err(dev, "Unable to query PD power info (err:0x%x)\n", ret);
		return ret;
	}

	last_psy_status = port->psy_status;
	last_psy_usb_type = port->psy_usb_type;

	switch (resp.role) {
	case USB_PD_PORT_POWER_DISCONNECTED:
		port->psy_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		port->psy_online = 0;
		break;
	case USB_PD_PORT_POWER_SOURCE:
		port->psy_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		port->psy_online = 0;
		break;
	case USB_PD_PORT_POWER_SINK:
		port->psy_status = POWER_SUPPLY_STATUS_CHARGING;
		port->psy_online = 1;
		break;
	case USB_PD_PORT_POWER_SINK_NOT_CHARGING:
		port->psy_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		port->psy_online = 1;
		break;
	default:
		dev_err(dev, "Unknown role %d\n", resp.role);
		break;
	}

	port->psy_voltage_max_design = resp.meas.voltage_max;
	port->psy_voltage_now = resp.meas.voltage_now;
	port->psy_current_max = resp.meas.current_max;
	port->psy_power_max = resp.max_power;

	switch (resp.type) {
	case USB_CHG_TYPE_BC12_SDP:
	case USB_CHG_TYPE_VBUS:
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case USB_CHG_TYPE_NONE:
		/*
		 * For dual-role devices when we are a source, the firmware
		 * reports the type as NONE. Report such chargers as type
		 * USB_PD_DRP.
		 */
		if (resp.role == USB_PD_PORT_POWER_SOURCE && resp.dualrole)
			port->psy_usb_type = POWER_SUPPLY_USB_TYPE_PD_DRP;
		else
			port->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case USB_CHG_TYPE_OTHER:
	case USB_CHG_TYPE_PROPRIETARY:
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
		break;
	case USB_CHG_TYPE_C:
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_C;
		break;
	case USB_CHG_TYPE_BC12_DCP:
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case USB_CHG_TYPE_BC12_CDP:
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case USB_CHG_TYPE_PD:
		if (resp.dualrole)
			port->psy_usb_type = POWER_SUPPLY_USB_TYPE_PD_DRP;
		else
			port->psy_usb_type = POWER_SUPPLY_USB_TYPE_PD;
		break;
	case USB_CHG_TYPE_UNKNOWN:
		/*
		 * While the EC is trying to determine the type of charger that
		 * has been plugged in, it will report the charger type as
		 * unknown. Additionally since the power capabilities are
		 * unknown, report the max current and voltage as zero.
		 */
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		port->psy_voltage_max_design = 0;
		port->psy_current_max = 0;
		break;
	default:
		dev_err(dev, "Port %d: default case!\n", port->port_number);
		port->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
	}

	if (cros_usbpd_charger_port_is_dedicated(port))
		port->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	else
		port->psy_desc.type = POWER_SUPPLY_TYPE_USB;

	dev_dbg(dev,
		"Port %d: type=%d vmax=%d vnow=%d cmax=%d clim=%d pmax=%d\n",
		port->port_number, resp.type, resp.meas.voltage_max,
		resp.meas.voltage_now, resp.meas.current_max,
		resp.meas.current_lim, resp.max_power);

	/*
	 * If power supply type or status changed, explicitly call
	 * power_supply_changed. This results in udev event getting generated
	 * and allows user mode apps to react quicker instead of waiting for
	 * their next poll of power supply status.
	 */
	if (last_psy_usb_type != port->psy_usb_type ||
	    last_psy_status != port->psy_status)
		power_supply_changed(port->psy);

	return 0;
}

static int cros_usbpd_charger_get_port_status(struct port_data *port,
					      bool ratelimit)
{
	int ret;

	if (ratelimit &&
	    time_is_after_jiffies(port->last_update +
				  CHARGER_CACHE_UPDATE_DELAY))
		return 0;

	ret = cros_usbpd_charger_get_power_info(port);
	if (ret < 0)
		return ret;

	if (!cros_usbpd_charger_port_is_dedicated(port))
		ret = cros_usbpd_charger_get_discovery_info(port);
	port->last_update = jiffies;

	return ret;
}

static int cros_usbpd_charger_set_ext_power_limit(struct charger_data *charger,
						  u16 current_lim,
						  u16 voltage_lim)
{
	struct ec_params_external_power_limit_v1 req;
	int ret;

	req.current_lim = current_lim;
	req.voltage_lim = voltage_lim;

	ret = cros_usbpd_charger_ec_command(charger, 0,
					    EC_CMD_EXTERNAL_POWER_LIMIT,
					    &req, sizeof(req), NULL, 0);
	if (ret < 0)
		dev_err(charger->dev,
			"Unable to set the 'External Power Limit': %d\n", ret);

	return ret;
}

static void cros_usbpd_charger_power_changed(struct power_supply *psy)
{
	struct port_data *port = power_supply_get_drvdata(psy);
	struct charger_data *charger = port->charger;
	int i;

	for (i = 0; i < charger->num_registered_psy; i++)
		cros_usbpd_charger_get_port_status(charger->ports[i], false);
}

static int cros_usbpd_charger_get_prop(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct port_data *port = power_supply_get_drvdata(psy);
	struct charger_data *charger = port->charger;
	struct cros_ec_device *ec_device = charger->ec_device;
	struct device *dev = charger->dev;
	int ret;

	/* Only refresh ec_port_status for dynamic properties */
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		/*
		 * If mkbp_event_supported, then we can be assured that
		 * the driver's state for the online property is consistent
		 * with the hardware. However, if we aren't event driven,
		 * the optimization before to skip an ec_port_status get
		 * and only returned cached values of the online property will
		 * cause a delay in detecting a cable attach until one of the
		 * other properties are read.
		 *
		 * Allow an ec_port_status refresh for online property check
		 * if we're not already online to check for plug events if
		 * not mkbp_event_supported.
		 */
		if (ec_device->mkbp_event_supported || port->psy_online)
			break;
		/* fall through */
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = cros_usbpd_charger_get_port_status(port, true);
		if (ret < 0) {
			dev_err(dev, "Failed to get port status (err:0x%x)\n",
				ret);
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = port->psy_online;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = port->psy_status;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = port->psy_current_max * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = port->psy_voltage_max_design * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = port->psy_voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = port->psy_usb_type;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (input_current_limit == EC_POWER_LIMIT_NONE)
			val->intval = -1;
		else
			val->intval = input_current_limit * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		if (input_voltage_limit == EC_POWER_LIMIT_NONE)
			val->intval = -1;
		else
			val->intval = input_voltage_limit * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = port->model_name;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = port->manufacturer;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cros_usbpd_charger_set_prop(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct port_data *port = power_supply_get_drvdata(psy);
	struct charger_data *charger = port->charger;
	struct device *dev = charger->dev;
	u16 intval;
	int ret;

	/* U16_MAX in mV/mA is the maximum supported value */
	if (val->intval >= U16_MAX * 1000)
		return -EINVAL;
	/* A negative number is used to clear the limit */
	if (val->intval < 0)
		intval = EC_POWER_LIMIT_NONE;
	else	/* Convert from uA/uV to mA/mV */
		intval = val->intval / 1000;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = cros_usbpd_charger_set_ext_power_limit(charger, intval,
							input_voltage_limit);
		if (ret < 0)
			break;

		input_current_limit = intval;
		if (input_current_limit == EC_POWER_LIMIT_NONE)
			dev_info(dev,
			  "External Current Limit cleared for all ports\n");
		else
			dev_info(dev,
			  "External Current Limit set to %dmA for all ports\n",
			  input_current_limit);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = cros_usbpd_charger_set_ext_power_limit(charger,
							input_current_limit,
							intval);
		if (ret < 0)
			break;

		input_voltage_limit = intval;
		if (input_voltage_limit == EC_POWER_LIMIT_NONE)
			dev_info(dev,
			  "External Voltage Limit cleared for all ports\n");
		else
			dev_info(dev,
			  "External Voltage Limit set to %dmV for all ports\n",
			  input_voltage_limit);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int cros_usbpd_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static int cros_usbpd_charger_ec_event(struct notifier_block *nb,
				       unsigned long queued_during_suspend,
				       void *_notify)
{
	struct cros_ec_device *ec_device;
	struct charger_data *charger;
	u32 host_event;

	charger = container_of(nb, struct charger_data, notifier);
	ec_device = charger->ec_device;

	host_event = cros_ec_get_host_event(ec_device);
	if (host_event & EC_HOST_EVENT_MASK(EC_HOST_EVENT_PD_MCU)) {
		cros_usbpd_charger_power_changed(charger->ports[0]->psy);
		return NOTIFY_OK;
	} else {
		return NOTIFY_DONE;
	}
}

static void cros_usbpd_charger_unregister_notifier(void *data)
{
	struct charger_data *charger = data;
	struct cros_ec_device *ec_device = charger->ec_device;

	blocking_notifier_chain_unregister(&ec_device->event_notifier,
					   &charger->notifier);
}

static int cros_usbpd_charger_probe(struct platform_device *pd)
{
	struct cros_ec_dev *ec_dev = dev_get_drvdata(pd->dev.parent);
	struct cros_ec_device *ec_device = ec_dev->ec_dev;
	struct power_supply_desc *psy_desc;
	struct device *dev = &pd->dev;
	struct charger_data *charger;
	struct power_supply *psy;
	struct port_data *port;
	int ret = -EINVAL;
	int i;

	charger = devm_kzalloc(dev, sizeof(struct charger_data),
			       GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->dev = dev;
	charger->ec_dev = ec_dev;
	charger->ec_device = ec_device;

	platform_set_drvdata(pd, charger);

	/*
	 * We need to know the number of USB PD ports in order to know whether
	 * there is a dedicated port. The dedicated port will always be
	 * after the USB PD ports, and there should be only one.
	 */
	charger->num_usbpd_ports =
		cros_usbpd_charger_get_usbpd_num_ports(charger);
	if (charger->num_usbpd_ports <= 0) {
		/*
		 * This can happen on a system that doesn't support USB PD.
		 * Log a message, but no need to warn.
		 */
		dev_info(dev, "No USB PD charging ports found\n");
	}

	charger->num_charger_ports = cros_usbpd_charger_get_num_ports(charger);
	if (charger->num_charger_ports < 0) {
		/*
		 * This can happen on a system that doesn't support USB PD.
		 * Log a message, but no need to warn.
		 * Older ECs do not support the above command, in that case
		 * let's set up the number of charger ports equal to the number
		 * of USB PD ports
		 */
		dev_info(dev, "Could not get charger port count\n");
		charger->num_charger_ports = charger->num_usbpd_ports;
	}

	if (charger->num_charger_ports <= 0) {
		/*
		 * This can happen on a system that doesn't support USB PD and
		 * doesn't have a dedicated port.
		 * Log a message, but no need to warn.
		 */
		dev_info(dev, "No charging ports found\n");
		ret = -ENODEV;
		goto fail_nowarn;
	}

	/*
	 * Sanity checks on the number of ports:
	 *  there should be at most 1 dedicated port
	 */
	if (charger->num_charger_ports < charger->num_usbpd_ports ||
	    charger->num_charger_ports > (charger->num_usbpd_ports + 1)) {
		dev_err(dev, "Unexpected number of charge port count\n");
		ret = -EPROTO;
		goto fail_nowarn;
	}

	for (i = 0; i < charger->num_charger_ports; i++) {
		struct power_supply_config psy_cfg = {};

		port = devm_kzalloc(dev, sizeof(struct port_data), GFP_KERNEL);
		if (!port) {
			ret = -ENOMEM;
			goto fail;
		}

		port->charger = charger;
		port->port_number = i;

		psy_desc = &port->psy_desc;
		psy_desc->get_property = cros_usbpd_charger_get_prop;
		psy_desc->set_property = cros_usbpd_charger_set_prop;
		psy_desc->property_is_writeable =
				cros_usbpd_charger_property_is_writeable;
		psy_desc->external_power_changed =
					cros_usbpd_charger_power_changed;
		psy_cfg.drv_data = port;

		if (cros_usbpd_charger_port_is_dedicated(port)) {
			sprintf(port->name, CHARGER_DEDICATED_DIR_NAME);
			psy_desc->type = POWER_SUPPLY_TYPE_MAINS;
			psy_desc->properties =
				cros_usbpd_dedicated_charger_props;
			psy_desc->num_properties =
				ARRAY_SIZE(cros_usbpd_dedicated_charger_props);
		} else {
			sprintf(port->name, CHARGER_USBPD_DIR_NAME, i);
			psy_desc->type = POWER_SUPPLY_TYPE_USB;
			psy_desc->properties = cros_usbpd_charger_props;
			psy_desc->num_properties =
				ARRAY_SIZE(cros_usbpd_charger_props);
			psy_desc->usb_types = cros_usbpd_charger_usb_types;
			psy_desc->num_usb_types =
				ARRAY_SIZE(cros_usbpd_charger_usb_types);
		}

		psy_desc->name = port->name;

		psy = devm_power_supply_register_no_ws(dev, psy_desc,
						       &psy_cfg);
		if (IS_ERR(psy)) {
			dev_err(dev, "Failed to register power supply\n");
			continue;
		}
		port->psy = psy;

		charger->ports[charger->num_registered_psy++] = port;
	}

	if (!charger->num_registered_psy) {
		ret = -ENODEV;
		dev_err(dev, "No power supplies registered\n");
		goto fail;
	}

	if (ec_device->mkbp_event_supported) {
		/* Get PD events from the EC */
		charger->notifier.notifier_call = cros_usbpd_charger_ec_event;
		ret = blocking_notifier_chain_register(
						&ec_device->event_notifier,
						&charger->notifier);
		if (ret < 0) {
			dev_warn(dev, "failed to register notifier\n");
		} else {
			ret = devm_add_action_or_reset(dev,
					cros_usbpd_charger_unregister_notifier,
					charger);
			if (ret < 0)
				goto fail;
		}
	}

	return 0;

fail:
	WARN(1, "%s: Failing probe (err:0x%x)\n", dev_name(dev), ret);

fail_nowarn:
	dev_info(dev, "Failing probe (err:0x%x)\n", ret);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int cros_usbpd_charger_resume(struct device *dev)
{
	struct charger_data *charger = dev_get_drvdata(dev);
	int i;

	if (!charger)
		return 0;

	for (i = 0; i < charger->num_registered_psy; i++) {
		power_supply_changed(charger->ports[i]->psy);
		charger->ports[i]->last_update =
				jiffies - CHARGER_CACHE_UPDATE_DELAY;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cros_usbpd_charger_pm_ops, NULL,
			 cros_usbpd_charger_resume);

static struct platform_driver cros_usbpd_charger_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_usbpd_charger_pm_ops,
	},
	.probe = cros_usbpd_charger_probe
};

module_platform_driver(cros_usbpd_charger_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC USBPD charger");
MODULE_ALIAS("platform:" DRV_NAME);
