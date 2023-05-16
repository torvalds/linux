// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

static void lan966x_dcb_app_update(struct net_device *dev, bool enable)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x_port_qos qos = {0};
	struct dcb_app app_itr;

	/* Get pcp ingress mapping */
	for (int i = 0; i < ARRAY_SIZE(qos.pcp.map); i++) {
		app_itr.selector = DCB_APP_SEL_PCP;
		app_itr.protocol = i;
		qos.pcp.map[i] = dcb_getapp(dev, &app_itr);
	}

	qos.pcp.enable = enable;
	lan966x_port_qos_set(port, &qos);
}

static int lan966x_dcb_app_validate(struct net_device *dev,
				    const struct dcb_app *app)
{
	int err = 0;

	switch (app->selector) {
	/* Pcp checks */
	case DCB_APP_SEL_PCP:
		if (app->protocol >= LAN966X_PORT_QOS_PCP_DEI_COUNT)
			err = -EINVAL;
		else if (app->priority >= NUM_PRIO_QUEUES)
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

static int lan966x_dcb_ieee_delapp(struct net_device *dev, struct dcb_app *app)
{
	int err;

	err = dcb_ieee_delapp(dev, app);
	if (err < 0)
		return err;

	lan966x_dcb_app_update(dev, false);

	return 0;
}

static int lan966x_dcb_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	struct dcb_app app_itr;
	int err;
	u8 prio;

	err = lan966x_dcb_app_validate(dev, app);
	if (err)
		return err;

	/* Delete current mapping, if it exists */
	prio = dcb_getapp(dev, app);
	if (prio) {
		app_itr = *app;
		app_itr.priority = prio;
		dcb_ieee_delapp(dev, &app_itr);
	}

	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

	lan966x_dcb_app_update(dev, true);

	return 0;
}

static const struct dcbnl_rtnl_ops lan966x_dcbnl_ops = {
	.ieee_setapp = lan966x_dcb_ieee_setapp,
	.ieee_delapp = lan966x_dcb_ieee_delapp,
};

void lan966x_dcb_init(struct lan966x *lan966x)
{
	for (int p = 0; p < lan966x->num_phys_ports; ++p) {
		struct lan966x_port *port;

		port = lan966x->ports[p];
		if (!port)
			continue;

		port->dev->dcbnl_ops = &lan966x_dcbnl_ops;
	}
}
