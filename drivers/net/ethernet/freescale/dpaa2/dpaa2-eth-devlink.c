// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
#include "dpaa2-eth.h"
/* Copyright 2020 NXP
 */

static int dpaa2_eth_dl_info_get(struct devlink *devlink,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct dpaa2_eth_devlink_priv *dl_priv = devlink_priv(devlink);
	struct dpaa2_eth_priv *priv = dl_priv->dpaa2_priv;
	char buf[10];
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	scnprintf(buf, 10, "%d.%d", priv->dpni_ver_major, priv->dpni_ver_minor);
	err = devlink_info_version_running_put(req, "dpni", buf);
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops dpaa2_eth_devlink_ops = {
	.info_get = dpaa2_eth_dl_info_get,
};

int dpaa2_eth_dl_register(struct dpaa2_eth_priv *priv)
{
	struct net_device *net_dev = priv->net_dev;
	struct device *dev = net_dev->dev.parent;
	struct dpaa2_eth_devlink_priv *dl_priv;
	int err;

	priv->devlink = devlink_alloc(&dpaa2_eth_devlink_ops, sizeof(*dl_priv));
	if (!priv->devlink) {
		dev_err(dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}
	dl_priv = devlink_priv(priv->devlink);
	dl_priv->dpaa2_priv = priv;

	err = devlink_register(priv->devlink, dev);
	if (err) {
		dev_err(dev, "devlink_register() = %d\n", err);
		goto devlink_free;
	}

	return 0;

devlink_free:
	devlink_free(priv->devlink);

	return err;
}

void dpaa2_eth_dl_unregister(struct dpaa2_eth_priv *priv)
{
	devlink_unregister(priv->devlink);
	devlink_free(priv->devlink);
}

int dpaa2_eth_dl_port_add(struct dpaa2_eth_priv *priv)
{
	struct devlink_port *devlink_port = &priv->devlink_port;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	devlink_port_attrs_set(devlink_port, &attrs);

	err = devlink_port_register(priv->devlink, devlink_port, 0);
	if (err)
		return err;

	devlink_port_type_eth_set(devlink_port, priv->net_dev);

	return 0;
}

void dpaa2_eth_dl_port_del(struct dpaa2_eth_priv *priv)
{
	struct devlink_port *devlink_port = &priv->devlink_port;

	devlink_port_type_clear(devlink_port);
	devlink_port_unregister(devlink_port);
}
