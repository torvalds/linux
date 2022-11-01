// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/dcbnl.h>

#include "sparx5_port.h"

/* Validate app entry.
 *
 * Check for valid selectors and valid protocol and priority ranges.
 */
static int sparx5_dcb_app_validate(struct net_device *dev,
				   const struct dcb_app *app)
{
	int err = 0;

	switch (app->selector) {
	/* Pcp checks */
	case DCB_APP_SEL_PCP:
		if (app->protocol >= SPARX5_PORT_QOS_PCP_DEI_COUNT)
			err = -EINVAL;
		else if (app->priority >= SPX5_PRIOS)
			err = -ERANGE;
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err)
		netdev_err(dev, "Invalid entry: %d:%d\n", app->protocol,
			   app->priority);

	return err;
}

static int sparx5_dcb_app_update(struct net_device *dev)
{
	struct dcb_app app_itr = { .selector = DCB_APP_SEL_PCP };
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5_port_qos_pcp_map *pcp_map;
	struct sparx5_port_qos qos = {0};
	int i;

	pcp_map = &qos.pcp.map;

	/* Get pcp ingress mapping */
	for (i = 0; i < ARRAY_SIZE(pcp_map->map); i++) {
		app_itr.protocol = i;
		pcp_map->map[i] = dcb_getapp(dev, &app_itr);
	}

	return sparx5_port_qos_set(port, &qos);
}

static int sparx5_dcb_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	struct dcb_app app_itr;
	int err = 0;
	u8 prio;

	err = sparx5_dcb_app_validate(dev, app);
	if (err)
		goto out;

	/* Delete current mapping, if it exists */
	prio = dcb_getapp(dev, app);
	if (prio) {
		app_itr = *app;
		app_itr.priority = prio;
		dcb_ieee_delapp(dev, &app_itr);
	}

	err = dcb_ieee_setapp(dev, app);
	if (err)
		goto out;

	sparx5_dcb_app_update(dev);

out:
	return err;
}

static int sparx5_dcb_ieee_delapp(struct net_device *dev, struct dcb_app *app)
{
	int err;

	err = dcb_ieee_delapp(dev, app);
	if (err < 0)
		return err;

	return sparx5_dcb_app_update(dev);
}

const struct dcbnl_rtnl_ops sparx5_dcbnl_ops = {
	.ieee_setapp = sparx5_dcb_ieee_setapp,
	.ieee_delapp = sparx5_dcb_ieee_delapp,
};

int sparx5_dcb_init(struct sparx5 *sparx5)
{
	struct sparx5_port *port;
	int i;

	for (i = 0; i < SPX5_PORTS; i++) {
		port = sparx5->ports[i];
		if (!port)
			continue;
		port->ndev->dcbnl_ops = &sparx5_dcbnl_ops;
	}

	return 0;
}
