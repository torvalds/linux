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
#include <linux/platform_device.h>
#include <linux/usb/typec.h>

#define DRV_NAME "cros-ec-typec"

/* Platform-specific data for the Chrome OS EC Type C controller. */
struct cros_typec_data {
	struct device *dev;
	struct cros_ec_device *ec;
	int num_ports;
	/* Array of ports, indexed by port number. */
	struct typec_port *ports[EC_USB_PD_MAX_PORTS];
	/* Initial capabilities for each port. */
	struct typec_capability *caps[EC_USB_PD_MAX_PORTS];
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

static int cros_typec_init_ports(struct cros_typec_data *typec)
{
	struct device *dev = typec->dev;
	struct typec_capability *cap;
	struct fwnode_handle *fwnode;
	const char *port_prop;
	int ret;
	int i;
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

		cap = devm_kzalloc(dev, sizeof(*cap), GFP_KERNEL);
		if (!cap) {
			ret = -ENOMEM;
			goto unregister_ports;
		}

		typec->caps[port_num] = cap;

		ret = cros_typec_parse_port_props(cap, fwnode, dev);
		if (ret < 0)
			goto unregister_ports;

		typec->ports[port_num] = typec_register_port(dev, cap);
		if (IS_ERR(typec->ports[port_num])) {
			dev_err(dev, "Failed to register port %d\n", port_num);
			ret = PTR_ERR(typec->ports[port_num]);
			goto unregister_ports;
		}
	}

	return 0;

unregister_ports:
	for (i = 0; i < typec->num_ports; i++)
		typec_unregister_port(typec->ports[i]);
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
	int ret;

	typec = devm_kzalloc(dev, sizeof(*typec), GFP_KERNEL);
	if (!typec)
		return -ENOMEM;

	typec->dev = dev;
	typec->ec = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, typec);

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

	return 0;
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
