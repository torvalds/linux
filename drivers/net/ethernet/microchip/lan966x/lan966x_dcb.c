// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

enum lan966x_dcb_apptrust_values {
	LAN966X_DCB_APPTRUST_EMPTY,
	LAN966X_DCB_APPTRUST_DSCP,
	LAN966X_DCB_APPTRUST_PCP,
	LAN966X_DCB_APPTRUST_DSCP_PCP,
	__LAN966X_DCB_APPTRUST_MAX
};

static const struct lan966x_dcb_apptrust {
	u8 selectors[IEEE_8021QAZ_APP_SEL_MAX + 1];
	int nselectors;
} *lan966x_port_apptrust[NUM_PHYS_PORTS];

static const char *lan966x_dcb_apptrust_names[__LAN966X_DCB_APPTRUST_MAX] = {
	[LAN966X_DCB_APPTRUST_EMPTY]    = "empty",
	[LAN966X_DCB_APPTRUST_DSCP]     = "dscp",
	[LAN966X_DCB_APPTRUST_PCP]      = "pcp",
	[LAN966X_DCB_APPTRUST_DSCP_PCP] = "dscp pcp"
};

/* Lan966x supported apptrust policies */
static const struct lan966x_dcb_apptrust
	lan966x_dcb_apptrust_policies[__LAN966X_DCB_APPTRUST_MAX] = {
	/* Empty *must* be first */
	[LAN966X_DCB_APPTRUST_EMPTY]    = { { 0 }, 0 },
	[LAN966X_DCB_APPTRUST_DSCP]     = { { IEEE_8021QAZ_APP_SEL_DSCP }, 1 },
	[LAN966X_DCB_APPTRUST_PCP]      = { { DCB_APP_SEL_PCP }, 1 },
	[LAN966X_DCB_APPTRUST_DSCP_PCP] = { { IEEE_8021QAZ_APP_SEL_DSCP,
					      DCB_APP_SEL_PCP }, 2 },
};

static bool lan966x_dcb_apptrust_contains(int portno, u8 selector)
{
	const struct lan966x_dcb_apptrust *conf = lan966x_port_apptrust[portno];

	for (int i = 0; i < conf->nselectors; i++)
		if (conf->selectors[i] == selector)
			return true;

	return false;
}

static void lan966x_dcb_app_update(struct net_device *dev)
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

	/* Enable use of pcp for queue classification */
	if (lan966x_dcb_apptrust_contains(port->chip_port, DCB_APP_SEL_PCP))
		qos.pcp.enable = true;

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

	lan966x_dcb_app_update(dev);

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

	lan966x_dcb_app_update(dev);

	return 0;
}

static int lan966x_dcb_apptrust_validate(struct net_device *dev,
					 u8 *selectors,
					 int nselectors)
{
	for (int i = 0; i < ARRAY_SIZE(lan966x_dcb_apptrust_policies); i++) {
		bool match;

		if (lan966x_dcb_apptrust_policies[i].nselectors != nselectors)
			continue;

		match = true;
		for (int j = 0; j < nselectors; j++) {
			if (lan966x_dcb_apptrust_policies[i].selectors[j] !=
			    *(selectors + j)) {
				match = false;
				break;
			}
		}
		if (match)
			return i;
	}

	netdev_err(dev, "Valid apptrust configurations are:\n");
	for (int i = 0; i < ARRAY_SIZE(lan966x_dcb_apptrust_names); i++)
		pr_info("order: %s\n", lan966x_dcb_apptrust_names[i]);

	return -EOPNOTSUPP;
}

static int lan966x_dcb_setapptrust(struct net_device *dev,
				   u8 *selectors,
				   int nselectors)
{
	struct lan966x_port *port = netdev_priv(dev);
	int idx;

	idx = lan966x_dcb_apptrust_validate(dev, selectors, nselectors);
	if (idx < 0)
		return idx;

	lan966x_port_apptrust[port->chip_port] = &lan966x_dcb_apptrust_policies[idx];
	lan966x_dcb_app_update(dev);

	return 0;
}

static int lan966x_dcb_getapptrust(struct net_device *dev, u8 *selectors,
				   int *nselectors)
{
	struct lan966x_port *port = netdev_priv(dev);
	const struct lan966x_dcb_apptrust *trust;

	trust = lan966x_port_apptrust[port->chip_port];

	memcpy(selectors, trust->selectors, trust->nselectors);
	*nselectors = trust->nselectors;

	return 0;
}

static const struct dcbnl_rtnl_ops lan966x_dcbnl_ops = {
	.ieee_setapp = lan966x_dcb_ieee_setapp,
	.ieee_delapp = lan966x_dcb_ieee_delapp,
	.dcbnl_setapptrust = lan966x_dcb_setapptrust,
	.dcbnl_getapptrust = lan966x_dcb_getapptrust,
};

void lan966x_dcb_init(struct lan966x *lan966x)
{
	for (int p = 0; p < lan966x->num_phys_ports; ++p) {
		struct lan966x_port *port;

		port = lan966x->ports[p];
		if (!port)
			continue;

		port->dev->dcbnl_ops = &lan966x_dcbnl_ops;

		lan966x_port_apptrust[port->chip_port] =
			&lan966x_dcb_apptrust_policies[LAN966X_DCB_APPTRUST_DSCP_PCP];
	}
}
