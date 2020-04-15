// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2020 Google LLC
 *
 * This driver provides the ability to view and manage Type C ports through the
 * Chrome OS EC.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_usbpd_notify.h>
#include <linux/platform_device.h>
#include <linux/usb/typec.h>

#define DRV_NAME "cros-ec-typec"

/* Per port data. */
struct cros_typec_port {
	struct typec_port *port;
	/* Initial capabilities for the port. */
	struct typec_capability caps;
	struct typec_partner *partner;
	/* Port partner PD identity info. */
	struct usb_pd_identity p_identity;
};

/* Platform-specific data for the Chrome OS EC Type C controller. */
struct cros_typec_data {
	struct device *dev;
	struct cros_ec_device *ec;
	int num_ports;
	unsigned int cmd_ver;
	/* Array of ports, indexed by port number. */
	struct cros_typec_port *ports[EC_USB_PD_MAX_PORTS];
	struct notifier_block nb;
};

static int cros_typec_parse_port_props(struct typec_capability *cap,
				       struct fwnode_handle *fwnode,
				       struct device *dev)
{
	const char *buf;
	int ret;

	memset(cap, 0, sizeof(*cap));
	ret = fwnode_property_read_string(fwnode, "power-role", &buf);
	if (ret) {
		dev_err(dev, "power-role not found: %d\n", ret);
		return ret;
	}

	ret = typec_find_port_power_role(buf);
	if (ret < 0)
		return ret;
	cap->type = ret;

	ret = fwnode_property_read_string(fwnode, "data-role", &buf);
	if (ret) {
		dev_err(dev, "data-role not found: %d\n", ret);
		return ret;
	}

	ret = typec_find_port_data_role(buf);
	if (ret < 0)
		return ret;
	cap->data = ret;

	ret = fwnode_property_read_string(fwnode, "try-power-role", &buf);
	if (ret) {
		dev_err(dev, "try-power-role not found: %d\n", ret);
		return ret;
	}

	ret = typec_find_power_role(buf);
	if (ret < 0)
		return ret;
	cap->prefer_role = ret;

	cap->fwnode = fwnode;

	return 0;
}

static void cros_unregister_ports(struct cros_typec_data *typec)
{
	int i;

	for (i = 0; i < typec->num_ports; i++) {
		if (!typec->ports[i])
			continue;
		typec_unregister_port(typec->ports[i]->port);
	}
}

static int cros_typec_init_ports(struct cros_typec_data *typec)
{
	struct device *dev = typec->dev;
	struct typec_capability *cap;
	struct fwnode_handle *fwnode;
	struct cros_typec_port *cros_port;
	const char *port_prop;
	int ret;
	int nports;
	u32 port_num = 0;

	nports = device_get_child_node_count(dev);
	if (nports == 0) {
		dev_err(dev, "No port entries found.\n");
		return -ENODEV;
	}

	if (nports > typec->num_ports) {
		dev_err(dev, "More ports listed than can be supported.\n");
		return -EINVAL;
	}

	/* DT uses "reg" to specify port number. */
	port_prop = dev->of_node ? "reg" : "port-number";
	device_for_each_child_node(dev, fwnode) {
		if (fwnode_property_read_u32(fwnode, port_prop, &port_num)) {
			ret = -EINVAL;
			dev_err(dev, "No port-number for port, aborting.\n");
			goto unregister_ports;
		}

		if (port_num >= typec->num_ports) {
			dev_err(dev, "Invalid port number.\n");
			ret = -EINVAL;
			goto unregister_ports;
		}

		dev_dbg(dev, "Registering port %d\n", port_num);

		cros_port = devm_kzalloc(dev, sizeof(*cros_port), GFP_KERNEL);
		if (!cros_port) {
			ret = -ENOMEM;
			goto unregister_ports;
		}

		typec->ports[port_num] = cros_port;
		cap = &cros_port->caps;

		ret = cros_typec_parse_port_props(cap, fwnode, dev);
		if (ret < 0)
			goto unregister_ports;

		cros_port->port = typec_register_port(dev, cap);
		if (IS_ERR(cros_port->port)) {
			dev_err(dev, "Failed to register port %d\n", port_num);
			ret = PTR_ERR(cros_port->port);
			goto unregister_ports;
		}
	}

	return 0;

unregister_ports:
	cros_unregister_ports(typec);
	return ret;
}

static int cros_typec_ec_command(struct cros_typec_data *typec,
				 unsigned int version,
				 unsigned int command,
				 void *outdata,
				 unsigned int outsize,
				 void *indata,
				 unsigned int insize)
{
	struct cros_ec_command *msg;
	int ret;

	msg = kzalloc(sizeof(*msg) + max(outsize, insize), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = version;
	msg->command = command;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, outdata, outsize);

	ret = cros_ec_cmd_xfer_status(typec->ec, msg);
	if (ret >= 0 && insize)
		memcpy(indata, msg->data, insize);

	kfree(msg);
	return ret;
}

static int cros_typec_add_partner(struct cros_typec_data *typec, int port_num,
				  bool pd_en)
{
	struct cros_typec_port *port = typec->ports[port_num];
	struct typec_partner_desc p_desc = {
		.usb_pd = pd_en,
	};
	int ret = 0;

	/*
	 * Fill an initial PD identity, which will then be updated with info
	 * from the EC.
	 */
	p_desc.identity = &port->p_identity;

	port->partner = typec_register_partner(port->port, &p_desc);
	if (IS_ERR(port->partner)) {
		ret = PTR_ERR(port->partner);
		port->partner = NULL;
	}

	return ret;
}

static void cros_typec_set_port_params_v0(struct cros_typec_data *typec,
		int port_num, struct ec_response_usb_pd_control *resp)
{
	struct typec_port *port = typec->ports[port_num]->port;
	enum typec_orientation polarity;

	if (!resp->enabled)
		polarity = TYPEC_ORIENTATION_NONE;
	else if (!resp->polarity)
		polarity = TYPEC_ORIENTATION_NORMAL;
	else
		polarity = TYPEC_ORIENTATION_REVERSE;

	typec_set_pwr_role(port, resp->role ? TYPEC_SOURCE : TYPEC_SINK);
	typec_set_orientation(port, polarity);
}

static void cros_typec_set_port_params_v1(struct cros_typec_data *typec,
		int port_num, struct ec_response_usb_pd_control_v1 *resp)
{
	struct typec_port *port = typec->ports[port_num]->port;
	enum typec_orientation polarity;
	bool pd_en;
	int ret;

	if (!(resp->enabled & PD_CTRL_RESP_ENABLED_CONNECTED))
		polarity = TYPEC_ORIENTATION_NONE;
	else if (!resp->polarity)
		polarity = TYPEC_ORIENTATION_NORMAL;
	else
		polarity = TYPEC_ORIENTATION_REVERSE;
	typec_set_orientation(port, polarity);
	typec_set_data_role(port, resp->role & PD_CTRL_RESP_ROLE_DATA ?
			TYPEC_HOST : TYPEC_DEVICE);
	typec_set_pwr_role(port, resp->role & PD_CTRL_RESP_ROLE_POWER ?
			TYPEC_SOURCE : TYPEC_SINK);
	typec_set_vconn_role(port, resp->role & PD_CTRL_RESP_ROLE_VCONN ?
			TYPEC_SOURCE : TYPEC_SINK);

	/* Register/remove partners when a connect/disconnect occurs. */
	if (resp->enabled & PD_CTRL_RESP_ENABLED_CONNECTED) {
		if (typec->ports[port_num]->partner)
			return;

		pd_en = resp->enabled & PD_CTRL_RESP_ENABLED_PD_CAPABLE;
		ret = cros_typec_add_partner(typec, port_num, pd_en);
		if (!ret)
			dev_warn(typec->dev,
				 "Failed to register partner on port: %d\n",
				 port_num);
	} else {
		if (!typec->ports[port_num]->partner)
			return;

		typec_unregister_partner(typec->ports[port_num]->partner);
		typec->ports[port_num]->partner = NULL;
	}
}

static int cros_typec_port_update(struct cros_typec_data *typec, int port_num)
{
	struct ec_params_usb_pd_control req;
	struct ec_response_usb_pd_control_v1 resp;
	int ret;

	if (port_num < 0 || port_num >= typec->num_ports) {
		dev_err(typec->dev, "cannot get status for invalid port %d\n",
			port_num);
		return -EINVAL;
	}

	req.port = port_num;
	req.role = USB_PD_CTRL_ROLE_NO_CHANGE;
	req.mux = USB_PD_CTRL_MUX_NO_CHANGE;
	req.swap = USB_PD_CTRL_SWAP_NONE;

	ret = cros_typec_ec_command(typec, typec->cmd_ver,
				    EC_CMD_USB_PD_CONTROL, &req, sizeof(req),
				    &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	dev_dbg(typec->dev, "Enabled %d: 0x%hhx\n", port_num, resp.enabled);
	dev_dbg(typec->dev, "Role %d: 0x%hhx\n", port_num, resp.role);
	dev_dbg(typec->dev, "Polarity %d: 0x%hhx\n", port_num, resp.polarity);
	dev_dbg(typec->dev, "State %d: %s\n", port_num, resp.state);

	if (typec->cmd_ver == 1)
		cros_typec_set_port_params_v1(typec, port_num, &resp);
	else
		cros_typec_set_port_params_v0(typec, port_num,
			(struct ec_response_usb_pd_control *) &resp);

	return 0;
}

static int cros_typec_get_cmd_version(struct cros_typec_data *typec)
{
	struct ec_params_get_cmd_versions_v1 req_v1;
	struct ec_response_get_cmd_versions resp;
	int ret;

	/* We're interested in the PD control command version. */
	req_v1.cmd = EC_CMD_USB_PD_CONTROL;
	ret = cros_typec_ec_command(typec, 1, EC_CMD_GET_CMD_VERSIONS,
				    &req_v1, sizeof(req_v1), &resp,
				    sizeof(resp));
	if (ret < 0)
		return ret;

	if (resp.version_mask & EC_VER_MASK(1))
		typec->cmd_ver = 1;
	else
		typec->cmd_ver = 0;

	dev_dbg(typec->dev, "PD Control has version mask 0x%hhx\n",
		typec->cmd_ver);

	return 0;
}

static int cros_ec_typec_event(struct notifier_block *nb,
			       unsigned long host_event, void *_notify)
{
	struct cros_typec_data *typec = container_of(nb, struct cros_typec_data,
						     nb);
	int ret, i;

	for (i = 0; i < typec->num_ports; i++) {
		ret = cros_typec_port_update(typec, i);
		if (ret < 0)
			dev_warn(typec->dev, "Update failed for port: %d\n", i);
	}

	return NOTIFY_OK;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_typec_acpi_id[] = {
	{ "GOOG0014", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, cros_typec_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id cros_typec_of_match[] = {
	{ .compatible = "google,cros-ec-typec", },
	{}
};
MODULE_DEVICE_TABLE(of, cros_typec_of_match);
#endif

static int cros_typec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_typec_data *typec;
	struct ec_response_usb_pd_ports resp;
	int ret, i;

	typec = devm_kzalloc(dev, sizeof(*typec), GFP_KERNEL);
	if (!typec)
		return -ENOMEM;

	typec->dev = dev;
	typec->ec = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, typec);

	ret = cros_typec_get_cmd_version(typec);
	if (ret < 0) {
		dev_err(dev, "failed to get PD command version info\n");
		return ret;
	}

	ret = cros_typec_ec_command(typec, 0, EC_CMD_USB_PD_PORTS, NULL, 0,
				    &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	typec->num_ports = resp.num_ports;
	if (typec->num_ports > EC_USB_PD_MAX_PORTS) {
		dev_warn(typec->dev,
			 "Too many ports reported: %d, limiting to max: %d\n",
			 typec->num_ports, EC_USB_PD_MAX_PORTS);
		typec->num_ports = EC_USB_PD_MAX_PORTS;
	}

	ret = cros_typec_init_ports(typec);
	if (ret < 0)
		return ret;

	for (i = 0; i < typec->num_ports; i++) {
		ret = cros_typec_port_update(typec, i);
		if (ret < 0)
			goto unregister_ports;
	}

	typec->nb.notifier_call = cros_ec_typec_event;
	ret = cros_usbpd_register_notify(&typec->nb);
	if (ret < 0)
		goto unregister_ports;

	return 0;

unregister_ports:
	cros_unregister_ports(typec);
	return ret;
}

static struct platform_driver cros_typec_driver = {
	.driver	= {
		.name = DRV_NAME,
		.acpi_match_table = ACPI_PTR(cros_typec_acpi_id),
		.of_match_table = of_match_ptr(cros_typec_of_match),
	},
	.probe = cros_typec_probe,
};

module_platform_driver(cros_typec_driver);

MODULE_AUTHOR("Prashant Malani <pmalani@chromium.org>");
MODULE_DESCRIPTION("Chrome OS EC Type C control");
MODULE_LICENSE("GPL");
