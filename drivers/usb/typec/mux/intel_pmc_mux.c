// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Intel PMC USB mux control
 *
 * Copyright (C) 2020 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/usb/pd.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_tbt.h>

#include <asm/intel_scu_ipc.h>

#define PMC_USBC_CMD		0xa7

/* Response status bits */
#define PMC_USB_RESP_STATUS_FAILURE	BIT(0)
#define PMC_USB_RESP_STATUS_FATAL	BIT(1)

/* "Usage" OOB Message field values */
enum {
	PMC_USB_CONNECT,
	PMC_USB_DISCONNECT,
	PMC_USB_SAFE_MODE,
	PMC_USB_ALT_MODE,
	PMC_USB_DP_HPD,
};

#define PMC_USB_MSG_USB2_PORT_SHIFT	0
#define PMC_USB_MSG_USB3_PORT_SHIFT	4
#define PMC_USB_MSG_UFP_SHIFT		4
#define PMC_USB_MSG_ORI_HSL_SHIFT	5
#define PMC_USB_MSG_ORI_AUX_SHIFT	6

/* Alt Mode Request */
struct altmode_req {
	u8 usage;
	u8 mode_type;
	u8 mode_id;
	u8 reserved;
	u32 mode_data;
} __packed;

#define PMC_USB_MODE_TYPE_SHIFT		4

enum {
	PMC_USB_MODE_TYPE_USB,
	PMC_USB_MODE_TYPE_DP,
	PMC_USB_MODE_TYPE_TBT,
};

/* Common Mode Data bits */
#define PMC_USB_ALTMODE_ACTIVE_CABLE	BIT(2)

#define PMC_USB_ALTMODE_ORI_SHIFT	1
#define PMC_USB_ALTMODE_UFP_SHIFT	3

/* DP specific Mode Data bits */
#define PMC_USB_ALTMODE_DP_MODE_SHIFT	8

/* TBT specific Mode Data bits */
#define PMC_USB_ALTMODE_TBT_TYPE	BIT(17)
#define PMC_USB_ALTMODE_CABLE_TYPE	BIT(18)
#define PMC_USB_ALTMODE_ACTIVE_LINK	BIT(20)
#define PMC_USB_ALTMODE_FORCE_LSR	BIT(23)
#define PMC_USB_ALTMODE_CABLE_SPD(_s_)	(((_s_) & GENMASK(2, 0)) << 25)
#define   PMC_USB_ALTMODE_CABLE_USB31	1
#define   PMC_USB_ALTMODE_CABLE_10GPS	2
#define   PMC_USB_ALTMODE_CABLE_20GPS	3
#define PMC_USB_ALTMODE_TBT_GEN(_g_)	(((_g_) & GENMASK(1, 0)) << 28)

/* Display HPD Request bits */
#define PMC_USB_DP_HPD_LVL		BIT(4)
#define PMC_USB_DP_HPD_IRQ		BIT(5)

/*
 * Input Output Manager (IOM) PORT STATUS
 */
#define IOM_PORT_STATUS_ACTIVITY_TYPE_MASK		GENMASK(9, 6)
#define IOM_PORT_STATUS_ACTIVITY_TYPE_SHIFT		6
#define IOM_PORT_STATUS_ACTIVITY_TYPE_USB		0x03
/* activity type: Safe Mode */
#define IOM_PORT_STATUS_ACTIVITY_TYPE_SAFE_MODE		0x04
/* activity type: Display Port */
#define IOM_PORT_STATUS_ACTIVITY_TYPE_DP		0x05
/* activity type: Display Port Multi Function Device */
#define IOM_PORT_STATUS_ACTIVITY_TYPE_DP_MFD		0x06
/* activity type: Thunderbolt */
#define IOM_PORT_STATUS_ACTIVITY_TYPE_TBT		0x07
#define IOM_PORT_STATUS_ACTIVITY_TYPE_ALT_MODE_USB	0x0c
#define IOM_PORT_STATUS_ACTIVITY_TYPE_ALT_MODE_TBT_USB	0x0d
/* Upstream Facing Port Information */
#define IOM_PORT_STATUS_UFP				BIT(10)
/* Display Port Hot Plug Detect status */
#define IOM_PORT_STATUS_DHPD_HPD_STATUS_MASK		GENMASK(13, 12)
#define IOM_PORT_STATUS_DHPD_HPD_STATUS_SHIFT		12
#define IOM_PORT_STATUS_DHPD_HPD_STATUS_ASSERT		0x01
#define IOM_PORT_STATUS_DHPD_HPD_SOURCE_TBT		BIT(14)
#define IOM_PORT_STATUS_CONNECTED			BIT(31)

#define IOM_PORT_ACTIVITY_IS(_status_, _type_)				\
	((((_status_) & IOM_PORT_STATUS_ACTIVITY_TYPE_MASK) >>		\
	  IOM_PORT_STATUS_ACTIVITY_TYPE_SHIFT) ==			\
	 (IOM_PORT_STATUS_ACTIVITY_TYPE_##_type_))

#define IOM_PORT_HPD_ASSERTED(_status_)					\
	((((_status_) & IOM_PORT_STATUS_DHPD_HPD_STATUS_MASK) >>	\
	  IOM_PORT_STATUS_DHPD_HPD_STATUS_SHIFT) &			\
	 IOM_PORT_STATUS_DHPD_HPD_STATUS_ASSERT)

struct pmc_usb;

struct pmc_usb_port {
	int num;
	u32 iom_status;
	struct pmc_usb *pmc;
	struct typec_mux *typec_mux;
	struct typec_switch *typec_sw;
	struct usb_role_switch *usb_sw;

	enum typec_orientation orientation;
	enum usb_role role;

	u8 usb2_port;
	u8 usb3_port;

	enum typec_orientation sbu_orientation;
	enum typec_orientation hsl_orientation;
};

struct pmc_usb {
	u8 num_ports;
	struct device *dev;
	struct intel_scu_ipc_dev *ipc;
	struct pmc_usb_port *port;
	struct acpi_device *iom_adev;
	void __iomem *iom_base;
	u32 iom_port_status_offset;
};

static void update_port_status(struct pmc_usb_port *port)
{
	u8 port_num;

	/* SoC expects the USB Type-C port numbers to start with 0 */
	port_num = port->usb3_port - 1;

	port->iom_status = readl(port->pmc->iom_base +
				 port->pmc->iom_port_status_offset +
				 port_num * sizeof(u32));
}

static int sbu_orientation(struct pmc_usb_port *port)
{
	if (port->sbu_orientation)
		return port->sbu_orientation - 1;

	return port->orientation - 1;
}

static int hsl_orientation(struct pmc_usb_port *port)
{
	if (port->hsl_orientation)
		return port->hsl_orientation - 1;

	return port->orientation - 1;
}

static int pmc_usb_command(struct pmc_usb_port *port, u8 *msg, u32 len)
{
	u8 response[4];
	u8 status_res;
	int ret;

	/*
	 * Error bit will always be 0 with the USBC command.
	 * Status can be checked from the response message if the
	 * function intel_scu_ipc_dev_command succeeds.
	 */
	ret = intel_scu_ipc_dev_command(port->pmc->ipc, PMC_USBC_CMD, 0, msg,
					len, response, sizeof(response));

	if (ret)
		return ret;

	status_res = (msg[0] & 0xf) < PMC_USB_SAFE_MODE ?
		     response[2] : response[1];

	if (status_res & PMC_USB_RESP_STATUS_FAILURE) {
		if (status_res & PMC_USB_RESP_STATUS_FATAL)
			return -EIO;

		return -EBUSY;
	}

	return 0;
}

static int
pmc_usb_mux_dp_hpd(struct pmc_usb_port *port, struct typec_displayport_data *dp)
{
	u8 msg[2] = { };
	int ret;

	msg[0] = PMC_USB_DP_HPD;
	msg[0] |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;

	/* Configure HPD first if HPD,IRQ comes together */
	if (!IOM_PORT_HPD_ASSERTED(port->iom_status) &&
	    dp->status & DP_STATUS_IRQ_HPD &&
	    dp->status & DP_STATUS_HPD_STATE) {
		msg[1] = PMC_USB_DP_HPD_LVL;
		ret = pmc_usb_command(port, msg, sizeof(msg));
		if (ret)
			return ret;
	}

	if (dp->status & DP_STATUS_IRQ_HPD)
		msg[1] = PMC_USB_DP_HPD_IRQ;

	if (dp->status & DP_STATUS_HPD_STATE)
		msg[1] |= PMC_USB_DP_HPD_LVL;

	return pmc_usb_command(port, msg, sizeof(msg));
}

static int
pmc_usb_mux_dp(struct pmc_usb_port *port, struct typec_mux_state *state)
{
	struct typec_displayport_data *data = state->data;
	struct altmode_req req = { };
	int ret;

	if (IOM_PORT_ACTIVITY_IS(port->iom_status, DP) ||
	    IOM_PORT_ACTIVITY_IS(port->iom_status, DP_MFD)) {
		if (IOM_PORT_HPD_ASSERTED(port->iom_status) &&
		    (!(data->status & DP_STATUS_IRQ_HPD) &&
		     data->status & DP_STATUS_HPD_STATE))
			return 0;

		return pmc_usb_mux_dp_hpd(port, state->data);
	}

	req.usage = PMC_USB_ALT_MODE;
	req.usage |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;
	req.mode_type = PMC_USB_MODE_TYPE_DP << PMC_USB_MODE_TYPE_SHIFT;

	req.mode_data = (port->orientation - 1) << PMC_USB_ALTMODE_ORI_SHIFT;
	req.mode_data |= (port->role - 1) << PMC_USB_ALTMODE_UFP_SHIFT;

	req.mode_data |= (state->mode - TYPEC_STATE_MODAL) <<
			 PMC_USB_ALTMODE_DP_MODE_SHIFT;

	ret = pmc_usb_command(port, (void *)&req, sizeof(req));
	if (ret)
		return ret;

	if (data->status & (DP_STATUS_IRQ_HPD | DP_STATUS_HPD_STATE))
		return pmc_usb_mux_dp_hpd(port, state->data);

	return 0;
}

static int
pmc_usb_mux_tbt(struct pmc_usb_port *port, struct typec_mux_state *state)
{
	struct typec_thunderbolt_data *data = state->data;
	u8 cable_rounded = TBT_CABLE_ROUNDED_SUPPORT(data->cable_mode);
	u8 cable_speed = TBT_CABLE_SPEED(data->cable_mode);
	struct altmode_req req = { };

	if (IOM_PORT_ACTIVITY_IS(port->iom_status, TBT) ||
	    IOM_PORT_ACTIVITY_IS(port->iom_status, ALT_MODE_TBT_USB))
		return 0;

	req.usage = PMC_USB_ALT_MODE;
	req.usage |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;
	req.mode_type = PMC_USB_MODE_TYPE_TBT << PMC_USB_MODE_TYPE_SHIFT;

	req.mode_data = (port->orientation - 1) << PMC_USB_ALTMODE_ORI_SHIFT;
	req.mode_data |= (port->role - 1) << PMC_USB_ALTMODE_UFP_SHIFT;

	if (TBT_ADAPTER(data->device_mode) == TBT_ADAPTER_TBT3)
		req.mode_data |= PMC_USB_ALTMODE_TBT_TYPE;

	if (data->cable_mode & TBT_CABLE_OPTICAL)
		req.mode_data |= PMC_USB_ALTMODE_CABLE_TYPE;

	if (data->cable_mode & TBT_CABLE_LINK_TRAINING)
		req.mode_data |= PMC_USB_ALTMODE_ACTIVE_LINK;

	if (data->enter_vdo & TBT_ENTER_MODE_ACTIVE_CABLE)
		req.mode_data |= PMC_USB_ALTMODE_ACTIVE_CABLE;

	req.mode_data |= PMC_USB_ALTMODE_CABLE_SPD(cable_speed);

	req.mode_data |= PMC_USB_ALTMODE_TBT_GEN(cable_rounded);

	return pmc_usb_command(port, (void *)&req, sizeof(req));
}

static int
pmc_usb_mux_usb4(struct pmc_usb_port *port, struct typec_mux_state *state)
{
	struct enter_usb_data *data = state->data;
	struct altmode_req req = { };
	u8 cable_speed;

	if (IOM_PORT_ACTIVITY_IS(port->iom_status, TBT) ||
	    IOM_PORT_ACTIVITY_IS(port->iom_status, ALT_MODE_TBT_USB))
		return 0;

	req.usage = PMC_USB_ALT_MODE;
	req.usage |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;
	req.mode_type = PMC_USB_MODE_TYPE_TBT << PMC_USB_MODE_TYPE_SHIFT;

	/* USB4 Mode */
	req.mode_data = PMC_USB_ALTMODE_FORCE_LSR;

	if (data->active_link_training)
		req.mode_data |= PMC_USB_ALTMODE_ACTIVE_LINK;

	req.mode_data |= (port->orientation - 1) << PMC_USB_ALTMODE_ORI_SHIFT;
	req.mode_data |= (port->role - 1) << PMC_USB_ALTMODE_UFP_SHIFT;

	switch ((data->eudo & EUDO_CABLE_TYPE_MASK) >> EUDO_CABLE_TYPE_SHIFT) {
	case EUDO_CABLE_TYPE_PASSIVE:
		break;
	case EUDO_CABLE_TYPE_OPTICAL:
		req.mode_data |= PMC_USB_ALTMODE_CABLE_TYPE;
		fallthrough;
	default:
		req.mode_data |= PMC_USB_ALTMODE_ACTIVE_CABLE;

		/* Configure data rate to rounded in the case of Active TBT3
		 * and USB4 cables.
		 */
		req.mode_data |= PMC_USB_ALTMODE_TBT_GEN(1);
		break;
	}

	cable_speed = (data->eudo & EUDO_CABLE_SPEED_MASK) >> EUDO_CABLE_SPEED_SHIFT;
	req.mode_data |= PMC_USB_ALTMODE_CABLE_SPD(cable_speed);

	return pmc_usb_command(port, (void *)&req, sizeof(req));
}

static int pmc_usb_mux_safe_state(struct pmc_usb_port *port)
{
	u8 msg;

	if (IOM_PORT_ACTIVITY_IS(port->iom_status, SAFE_MODE))
		return 0;

	msg = PMC_USB_SAFE_MODE;
	msg |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;

	return pmc_usb_command(port, &msg, sizeof(msg));
}

static int pmc_usb_disconnect(struct pmc_usb_port *port)
{
	struct typec_displayport_data data = { };
	u8 msg[2];

	if (!(port->iom_status & IOM_PORT_STATUS_CONNECTED))
		return 0;

	/* Clear DisplayPort HPD if it's still asserted. */
	if (IOM_PORT_HPD_ASSERTED(port->iom_status))
		pmc_usb_mux_dp_hpd(port, &data);

	msg[0] = PMC_USB_DISCONNECT;
	msg[0] |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;

	msg[1] = port->usb2_port << PMC_USB_MSG_USB2_PORT_SHIFT;

	return pmc_usb_command(port, msg, sizeof(msg));
}

static int pmc_usb_connect(struct pmc_usb_port *port, enum usb_role role)
{
	u8 ufp = role == USB_ROLE_DEVICE ? 1 : 0;
	u8 msg[2];
	int ret;

	if (port->orientation == TYPEC_ORIENTATION_NONE)
		return -EINVAL;

	if (port->iom_status & IOM_PORT_STATUS_CONNECTED) {
		if (port->role == role || port->role == USB_ROLE_NONE)
			return 0;

		/* Role swap */
		ret = pmc_usb_disconnect(port);
		if (ret)
			return ret;
	}

	msg[0] = PMC_USB_CONNECT;
	msg[0] |= port->usb3_port << PMC_USB_MSG_USB3_PORT_SHIFT;

	msg[1] = port->usb2_port << PMC_USB_MSG_USB2_PORT_SHIFT;
	msg[1] |= ufp << PMC_USB_MSG_UFP_SHIFT;
	msg[1] |= hsl_orientation(port) << PMC_USB_MSG_ORI_HSL_SHIFT;
	msg[1] |= sbu_orientation(port) << PMC_USB_MSG_ORI_AUX_SHIFT;

	return pmc_usb_command(port, msg, sizeof(msg));
}

static int
pmc_usb_mux_set(struct typec_mux *mux, struct typec_mux_state *state)
{
	struct pmc_usb_port *port = typec_mux_get_drvdata(mux);

	update_port_status(port);

	if (port->orientation == TYPEC_ORIENTATION_NONE || port->role == USB_ROLE_NONE)
		return 0;

	if (state->mode == TYPEC_STATE_SAFE)
		return pmc_usb_mux_safe_state(port);
	if (state->mode == TYPEC_STATE_USB)
		return pmc_usb_connect(port, port->role);

	if (state->alt) {
		switch (state->alt->svid) {
		case USB_TYPEC_TBT_SID:
			return pmc_usb_mux_tbt(port, state);
		case USB_TYPEC_DP_SID:
			return pmc_usb_mux_dp(port, state);
		}
	} else {
		switch (state->mode) {
		case TYPEC_MODE_USB2:
			/* REVISIT: Try with usb3_port set to 0? */
			break;
		case TYPEC_MODE_USB3:
			return pmc_usb_connect(port, port->role);
		case TYPEC_MODE_USB4:
			return pmc_usb_mux_usb4(port, state);
		}
	}

	return -EOPNOTSUPP;
}

static int pmc_usb_set_orientation(struct typec_switch *sw,
				   enum typec_orientation orientation)
{
	struct pmc_usb_port *port = typec_switch_get_drvdata(sw);

	update_port_status(port);

	port->orientation = orientation;

	return 0;
}

static int pmc_usb_set_role(struct usb_role_switch *sw, enum usb_role role)
{
	struct pmc_usb_port *port = usb_role_switch_get_drvdata(sw);
	int ret;

	update_port_status(port);

	if (role == USB_ROLE_NONE)
		ret = pmc_usb_disconnect(port);
	else
		ret = pmc_usb_connect(port, role);

	port->role = role;

	return ret;
}

static int pmc_usb_register_port(struct pmc_usb *pmc, int index,
				 struct fwnode_handle *fwnode)
{
	struct pmc_usb_port *port = &pmc->port[index];
	struct usb_role_switch_desc desc = { };
	struct typec_switch_desc sw_desc = { };
	struct typec_mux_desc mux_desc = { };
	const char *str;
	int ret;

	ret = fwnode_property_read_u8(fwnode, "usb2-port-number", &port->usb2_port);
	if (ret)
		return ret;

	ret = fwnode_property_read_u8(fwnode, "usb3-port-number", &port->usb3_port);
	if (ret)
		return ret;

	ret = fwnode_property_read_string(fwnode, "sbu-orientation", &str);
	if (!ret)
		port->sbu_orientation = typec_find_orientation(str);

	ret = fwnode_property_read_string(fwnode, "hsl-orientation", &str);
	if (!ret)
		port->hsl_orientation = typec_find_orientation(str);

	port->num = index;
	port->pmc = pmc;

	sw_desc.fwnode = fwnode;
	sw_desc.drvdata = port;
	sw_desc.name = fwnode_get_name(fwnode);
	sw_desc.set = pmc_usb_set_orientation;

	port->typec_sw = typec_switch_register(pmc->dev, &sw_desc);
	if (IS_ERR(port->typec_sw))
		return PTR_ERR(port->typec_sw);

	mux_desc.fwnode = fwnode;
	mux_desc.drvdata = port;
	mux_desc.name = fwnode_get_name(fwnode);
	mux_desc.set = pmc_usb_mux_set;

	port->typec_mux = typec_mux_register(pmc->dev, &mux_desc);
	if (IS_ERR(port->typec_mux)) {
		ret = PTR_ERR(port->typec_mux);
		goto err_unregister_switch;
	}

	desc.fwnode = fwnode;
	desc.driver_data = port;
	desc.name = fwnode_get_name(fwnode);
	desc.set = pmc_usb_set_role;

	port->usb_sw = usb_role_switch_register(pmc->dev, &desc);
	if (IS_ERR(port->usb_sw)) {
		ret = PTR_ERR(port->usb_sw);
		goto err_unregister_mux;
	}

	return 0;

err_unregister_mux:
	typec_mux_unregister(port->typec_mux);

err_unregister_switch:
	typec_switch_unregister(port->typec_sw);

	return ret;
}

static int is_memory(struct acpi_resource *res, void *data)
{
	struct resource_win win = {};
	struct resource *r = &win.res;

	return !(acpi_dev_resource_memory(res, r) ||
		 acpi_dev_resource_address_space(res, &win));
}

/* IOM ACPI IDs and IOM_PORT_STATUS_OFFSET */
static const struct acpi_device_id iom_acpi_ids[] = {
	/* TigerLake */
	{ "INTC1072", 0x560, },

	/* AlderLake */
	{ "INTC1079", 0x160, },

	/* Meteor Lake */
	{ "INTC107A", 0x160, },
	{}
};

static int pmc_usb_probe_iom(struct pmc_usb *pmc)
{
	struct list_head resource_list;
	struct resource_entry *rentry;
	static const struct acpi_device_id *dev_id;
	struct acpi_device *adev = NULL;
	int ret;

	for (dev_id = &iom_acpi_ids[0]; dev_id->id[0]; dev_id++) {
		if (acpi_dev_present(dev_id->id, NULL, -1)) {
			pmc->iom_port_status_offset = (u32)dev_id->driver_data;
			adev = acpi_dev_get_first_match_dev(dev_id->id, NULL, -1);
			break;
		}
	}

	if (!adev)
		return -ENODEV;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, is_memory, NULL);
	if (ret < 0)
		return ret;

	rentry = list_first_entry_or_null(&resource_list, struct resource_entry, node);
	if (rentry)
		pmc->iom_base = devm_ioremap_resource(pmc->dev, rentry->res);

	acpi_dev_free_resource_list(&resource_list);

	if (!pmc->iom_base) {
		acpi_dev_put(adev);
		return -ENOMEM;
	}

	if (IS_ERR(pmc->iom_base)) {
		acpi_dev_put(adev);
		return PTR_ERR(pmc->iom_base);
	}

	pmc->iom_adev = adev;

	return 0;
}

static int pmc_usb_probe(struct platform_device *pdev)
{
	struct fwnode_handle *fwnode = NULL;
	struct pmc_usb *pmc;
	int i = 0;
	int ret;

	pmc = devm_kzalloc(&pdev->dev, sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	device_for_each_child_node(&pdev->dev, fwnode)
		pmc->num_ports++;

	/* The IOM microcontroller has a limitation of max 4 ports. */
	if (pmc->num_ports > 4) {
		dev_err(&pdev->dev, "driver limited to 4 ports\n");
		return -ERANGE;
	}

	pmc->port = devm_kcalloc(&pdev->dev, pmc->num_ports,
				 sizeof(struct pmc_usb_port), GFP_KERNEL);
	if (!pmc->port)
		return -ENOMEM;

	pmc->ipc = devm_intel_scu_ipc_dev_get(&pdev->dev);
	if (!pmc->ipc)
		return -ENODEV;

	pmc->dev = &pdev->dev;

	ret = pmc_usb_probe_iom(pmc);
	if (ret)
		return ret;

	/*
	 * For every physical USB connector (USB2 and USB3 combo) there is a
	 * child ACPI device node under the PMC mux ACPI device object.
	 */
	for (i = 0; i < pmc->num_ports; i++) {
		fwnode = device_get_next_child_node(pmc->dev, fwnode);
		if (!fwnode)
			break;

		ret = pmc_usb_register_port(pmc, i, fwnode);
		if (ret) {
			fwnode_handle_put(fwnode);
			goto err_remove_ports;
		}
	}

	platform_set_drvdata(pdev, pmc);

	return 0;

err_remove_ports:
	for (i = 0; i < pmc->num_ports; i++) {
		typec_switch_unregister(pmc->port[i].typec_sw);
		typec_mux_unregister(pmc->port[i].typec_mux);
		usb_role_switch_unregister(pmc->port[i].usb_sw);
	}

	acpi_dev_put(pmc->iom_adev);

	return ret;
}

static int pmc_usb_remove(struct platform_device *pdev)
{
	struct pmc_usb *pmc = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < pmc->num_ports; i++) {
		typec_switch_unregister(pmc->port[i].typec_sw);
		typec_mux_unregister(pmc->port[i].typec_mux);
		usb_role_switch_unregister(pmc->port[i].usb_sw);
	}

	acpi_dev_put(pmc->iom_adev);

	return 0;
}

static const struct acpi_device_id pmc_usb_acpi_ids[] = {
	{ "INTC105C", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmc_usb_acpi_ids);

static struct platform_driver pmc_usb_driver = {
	.driver = {
		.name = "intel_pmc_usb",
		.acpi_match_table = ACPI_PTR(pmc_usb_acpi_ids),
	},
	.probe = pmc_usb_probe,
	.remove = pmc_usb_remove,
};

module_platform_driver(pmc_usb_driver);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC USB mux control");
