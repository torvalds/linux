// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 *
 * This driver provides the ability to configure Type-C muxes and retimers which are controlled by
 * the ChromeOS EC.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/usb/typec_altmode.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>

/* Handles and other relevant data required for each port's switches. */
struct cros_typec_port {
	int port_num;
	struct typec_mux_dev *mode_switch;
	struct typec_retimer *retimer;
	struct cros_typec_switch_data *sdata;
};

/* Driver-specific data. */
struct cros_typec_switch_data {
	struct device *dev;
	struct cros_ec_device *ec;
	struct cros_typec_port *ports[EC_USB_PD_MAX_PORTS];
};

static int cros_typec_cmd_mux_set(struct cros_typec_switch_data *sdata, int port_num, u8 index,
				  u8 state)
{
	struct ec_params_typec_control req = {
		.port = port_num,
		.command = TYPEC_CONTROL_COMMAND_USB_MUX_SET,
		.mux_params = {
			.mux_index = index,
			.mux_flags = state,
		},
	};

	return cros_ec_cmd(sdata->ec, 0, EC_CMD_TYPEC_CONTROL, &req, sizeof(req), NULL, 0);
}

static int cros_typec_get_mux_state(unsigned long mode, struct typec_altmode *alt)
{
	int ret = -EOPNOTSUPP;
	u8 pin_assign;

	if (mode == TYPEC_STATE_SAFE) {
		ret = USB_PD_MUX_SAFE_MODE;
	} else if (mode == TYPEC_STATE_USB) {
		ret = USB_PD_MUX_USB_ENABLED;
	} else if (alt && alt->svid == USB_TYPEC_DP_SID) {
		ret = USB_PD_MUX_DP_ENABLED;
		pin_assign = mode - TYPEC_STATE_MODAL;
		if (pin_assign & DP_PIN_ASSIGN_D)
			ret |= USB_PD_MUX_USB_ENABLED;
	}

	return ret;
}

static int cros_typec_send_clear_event(struct cros_typec_switch_data *sdata, int port_num,
				       u32 events_mask)
{
	struct ec_params_typec_control req = {
		.port = port_num,
		.command = TYPEC_CONTROL_COMMAND_CLEAR_EVENTS,
		.clear_events_mask = events_mask,
	};

	return cros_ec_cmd(sdata->ec, 0, EC_CMD_TYPEC_CONTROL, &req, sizeof(req), NULL, 0);
}

static bool cros_typec_check_event(struct cros_typec_switch_data *sdata, int port_num, u32 mask)
{
	struct ec_response_typec_status resp;
	struct ec_params_typec_status req = {
		.port = port_num,
	};
	int ret;

	ret = cros_ec_cmd(sdata->ec, 0, EC_CMD_TYPEC_STATUS, &req, sizeof(req),
			  &resp, sizeof(resp));
	if (ret < 0) {
		dev_warn(sdata->dev, "EC_CMD_TYPEC_STATUS failed for port: %d\n", port_num);
		return false;
	}

	if (resp.events & mask)
		return true;

	return false;
}

/*
 * The ChromeOS EC treats both mode-switches and retimers as "muxes" for the purposes of the
 * host command API. This common function configures and verifies the retimer/mode-switch
 * according to the provided setting.
 */
static int cros_typec_configure_mux(struct cros_typec_switch_data *sdata, int port_num, int index,
				    unsigned long mode, struct typec_altmode *alt)
{
	unsigned long end;
	u32 event_mask;
	u8 mux_state;
	int ret;

	ret = cros_typec_get_mux_state(mode, alt);
	if (ret < 0)
		return ret;
	mux_state = (u8)ret;

	/* Clear any old mux set done event. */
	if (index == 0)
		event_mask = PD_STATUS_EVENT_MUX_0_SET_DONE;
	else
		event_mask = PD_STATUS_EVENT_MUX_1_SET_DONE;

	ret = cros_typec_send_clear_event(sdata, port_num, event_mask);
	if (ret < 0)
		return ret;

	/* Send the set command. */
	ret = cros_typec_cmd_mux_set(sdata, port_num, index, mux_state);
	if (ret < 0)
		return ret;

	/* Check for the mux set done event. */
	end = jiffies + msecs_to_jiffies(1000);
	do {
		if (cros_typec_check_event(sdata, port_num, event_mask))
			return 0;

		usleep_range(500, 1000);
	} while (time_before(jiffies, end));

	dev_err(sdata->dev, "Timed out waiting for mux set done on index: %d, state: %d\n",
		index, mux_state);

	return -ETIMEDOUT;
}

static int cros_typec_mode_switch_set(struct typec_mux_dev *mode_switch,
				      struct typec_mux_state *state)
{
	struct cros_typec_port *port = typec_mux_get_drvdata(mode_switch);

	/* Mode switches have index 0. */
	return cros_typec_configure_mux(port->sdata, port->port_num, 0, state->mode, state->alt);
}

static int cros_typec_retimer_set(struct typec_retimer *retimer, struct typec_retimer_state *state)
{
	struct cros_typec_port *port = typec_retimer_get_drvdata(retimer);

	/* Retimers have index 1. */
	return cros_typec_configure_mux(port->sdata, port->port_num, 1, state->mode, state->alt);
}

static void cros_typec_unregister_switches(struct cros_typec_switch_data *sdata)
{
	int i;

	for (i = 0; i < EC_USB_PD_MAX_PORTS; i++) {
		if (!sdata->ports[i])
			continue;
		typec_retimer_unregister(sdata->ports[i]->retimer);
		typec_mux_unregister(sdata->ports[i]->mode_switch);
	}
}

static int cros_typec_register_mode_switch(struct cros_typec_port *port,
					   struct fwnode_handle *fwnode)
{
	struct typec_mux_desc mode_switch_desc = {
		.fwnode = fwnode,
		.drvdata = port,
		.name = fwnode_get_name(fwnode),
		.set = cros_typec_mode_switch_set,
	};

	port->mode_switch = typec_mux_register(port->sdata->dev, &mode_switch_desc);

	return PTR_ERR_OR_ZERO(port->mode_switch);
}

static int cros_typec_register_retimer(struct cros_typec_port *port, struct fwnode_handle *fwnode)
{
	struct typec_retimer_desc retimer_desc = {
		.fwnode = fwnode,
		.drvdata = port,
		.name = fwnode_get_name(fwnode),
		.set = cros_typec_retimer_set,
	};

	port->retimer = typec_retimer_register(port->sdata->dev, &retimer_desc);

	return PTR_ERR_OR_ZERO(port->retimer);
}

static int cros_typec_register_switches(struct cros_typec_switch_data *sdata)
{
	struct cros_typec_port *port;
	struct device *dev = sdata->dev;
	struct fwnode_handle *fwnode;
	struct acpi_device *adev;
	unsigned long long index;
	int nports, ret;

	nports = device_get_child_node_count(dev);
	if (nports == 0) {
		dev_err(dev, "No switch devices found.\n");
		return -ENODEV;
	}

	device_for_each_child_node(dev, fwnode) {
		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port) {
			ret = -ENOMEM;
			goto err_switch;
		}

		adev = to_acpi_device_node(fwnode);
		if (!adev) {
			dev_err(fwnode->dev, "Couldn't get ACPI device handle\n");
			ret = -ENODEV;
			goto err_switch;
		}

		ret = acpi_evaluate_integer(adev->handle, "_ADR", NULL, &index);
		if (ACPI_FAILURE(ret)) {
			dev_err(fwnode->dev, "_ADR wasn't evaluated\n");
			ret = -ENODATA;
			goto err_switch;
		}

		if (index >= EC_USB_PD_MAX_PORTS) {
			dev_err(fwnode->dev, "Invalid port index number: %llu\n", index);
			ret = -EINVAL;
			goto err_switch;
		}
		port->sdata = sdata;
		port->port_num = index;
		sdata->ports[index] = port;

		if (fwnode_property_present(fwnode, "retimer-switch")) {
			ret = cros_typec_register_retimer(port, fwnode);
			if (ret) {
				dev_err(dev, "Retimer switch register failed\n");
				goto err_switch;
			}

			dev_dbg(dev, "Retimer switch registered for index %llu\n", index);
		}

		if (!fwnode_property_present(fwnode, "mode-switch"))
			continue;

		ret = cros_typec_register_mode_switch(port, fwnode);
		if (ret) {
			dev_err(dev, "Mode switch register failed\n");
			goto err_switch;
		}

		dev_dbg(dev, "Mode switch registered for index %llu\n", index);
	}

	return 0;
err_switch:
	fwnode_handle_put(fwnode);
	cros_typec_unregister_switches(sdata);
	return ret;
}

static int cros_typec_switch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_typec_switch_data *sdata;

	sdata = devm_kzalloc(dev, sizeof(*sdata), GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	sdata->dev = dev;
	sdata->ec = dev_get_drvdata(pdev->dev.parent);

	platform_set_drvdata(pdev, sdata);

	return cros_typec_register_switches(sdata);
}

static void cros_typec_switch_remove(struct platform_device *pdev)
{
	struct cros_typec_switch_data *sdata = platform_get_drvdata(pdev);

	cros_typec_unregister_switches(sdata);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id cros_typec_switch_acpi_id[] = {
	{ "GOOG001A", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, cros_typec_switch_acpi_id);
#endif

static struct platform_driver cros_typec_switch_driver = {
	.driver	= {
		.name = "cros-typec-switch",
		.acpi_match_table = ACPI_PTR(cros_typec_switch_acpi_id),
	},
	.probe = cros_typec_switch_probe,
	.remove_new = cros_typec_switch_remove,
};

module_platform_driver(cros_typec_switch_driver);

MODULE_AUTHOR("Prashant Malani <pmalani@chromium.org>");
MODULE_DESCRIPTION("ChromeOS EC Type-C Switch control");
MODULE_LICENSE("GPL");
