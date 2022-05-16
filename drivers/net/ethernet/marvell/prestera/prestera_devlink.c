// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <net/devlink.h>

#include "prestera_devlink.h"

static int prestera_dl_info_get(struct devlink *dl,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct prestera_switch *sw = devlink_priv(dl);
	char buf[16];
	int err;

	err = devlink_info_driver_name_put(req, PRESTERA_DRV_NAME);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "%d.%d.%d",
		 sw->dev->fw_rev.maj,
		 sw->dev->fw_rev.min,
		 sw->dev->fw_rev.sub);

	return devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       buf);
}

static const struct devlink_ops prestera_dl_ops = {
	.info_get = prestera_dl_info_get,
};

struct prestera_switch *prestera_devlink_alloc(void)
{
	struct devlink *dl;

	dl = devlink_alloc(&prestera_dl_ops, sizeof(struct prestera_switch));

	return devlink_priv(dl);
}

void prestera_devlink_free(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);

	devlink_free(dl);
}

int prestera_devlink_register(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);
	int err;

	err = devlink_register(dl, sw->dev->dev);
	if (err)
		dev_err(prestera_dev(sw), "devlink_register failed: %d\n", err);

	return err;
}

void prestera_devlink_unregister(struct prestera_switch *sw)
{
	struct devlink *dl = priv_to_devlink(sw);

	devlink_unregister(dl);
}

int prestera_devlink_port_register(struct prestera_port *port)
{
	struct prestera_switch *sw = port->sw;
	struct devlink *dl = priv_to_devlink(sw);
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = port->fp_id;
	attrs.switch_id.id_len = sizeof(sw->id);
	memcpy(attrs.switch_id.id, &sw->id, attrs.switch_id.id_len);

	devlink_port_attrs_set(&port->dl_port, &attrs);

	err = devlink_port_register(dl, &port->dl_port, port->fp_id);
	if (err) {
		dev_err(prestera_dev(sw), "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

void prestera_devlink_port_unregister(struct prestera_port *port)
{
	devlink_port_unregister(&port->dl_port);
}

void prestera_devlink_port_set(struct prestera_port *port)
{
	devlink_port_type_eth_set(&port->dl_port, port->dev);
}

void prestera_devlink_port_clear(struct prestera_port *port)
{
	devlink_port_type_clear(&port->dl_port);
}

struct devlink_port *prestera_devlink_get_port(struct net_device *dev)
{
	struct prestera_port *port = netdev_priv(dev);

	return &port->dl_port;
}
