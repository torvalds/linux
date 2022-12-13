// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/dcbnl.h>

#include "sparx5_port.h"

enum sparx5_dcb_apptrust_values {
	SPARX5_DCB_APPTRUST_EMPTY,
	SPARX5_DCB_APPTRUST_DSCP,
	SPARX5_DCB_APPTRUST_PCP,
	SPARX5_DCB_APPTRUST_DSCP_PCP,
	__SPARX5_DCB_APPTRUST_MAX
};

static const struct sparx5_dcb_apptrust {
	u8 selectors[IEEE_8021QAZ_APP_SEL_MAX + 1];
	int nselectors;
} *sparx5_port_apptrust[SPX5_PORTS];

static const char *sparx5_dcb_apptrust_names[__SPARX5_DCB_APPTRUST_MAX] = {
	[SPARX5_DCB_APPTRUST_EMPTY]    = "empty",
	[SPARX5_DCB_APPTRUST_DSCP]     = "dscp",
	[SPARX5_DCB_APPTRUST_PCP]      = "pcp",
	[SPARX5_DCB_APPTRUST_DSCP_PCP] = "dscp pcp"
};

/* Sparx5 supported apptrust policies */
static const struct sparx5_dcb_apptrust
	sparx5_dcb_apptrust_policies[__SPARX5_DCB_APPTRUST_MAX] = {
	/* Empty *must* be first */
	[SPARX5_DCB_APPTRUST_EMPTY]    = { { 0 }, 0 },
	[SPARX5_DCB_APPTRUST_DSCP]     = { { IEEE_8021QAZ_APP_SEL_DSCP }, 1 },
	[SPARX5_DCB_APPTRUST_PCP]      = { { DCB_APP_SEL_PCP }, 1 },
	[SPARX5_DCB_APPTRUST_DSCP_PCP] = { { IEEE_8021QAZ_APP_SEL_DSCP,
					     DCB_APP_SEL_PCP }, 2 },
};

/* Validate app entry.
 *
 * Check for valid selectors and valid protocol and priority ranges.
 */
static int sparx5_dcb_app_validate(struct net_device *dev,
				   const struct dcb_app *app)
{
	int err = 0;

	switch (app->selector) {
	/* Default priority checks */
	case IEEE_8021QAZ_APP_SEL_ETHERTYPE:
		if (app->protocol != 0)
			err = -EINVAL;
		else if (app->priority >= SPX5_PRIOS)
			err = -ERANGE;
		break;
	/* Dscp checks */
	case IEEE_8021QAZ_APP_SEL_DSCP:
		if (app->protocol >= SPARX5_PORT_QOS_DSCP_COUNT)
			err = -EINVAL;
		else if (app->priority >= SPX5_PRIOS)
			err = -ERANGE;
		break;
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

/* Validate apptrust configuration.
 *
 * Return index of supported apptrust configuration if valid, otherwise return
 * error.
 */
static int sparx5_dcb_apptrust_validate(struct net_device *dev, u8 *selectors,
					int nselectors, int *err)
{
	bool match = false;
	int i, ii;

	for (i = 0; i < ARRAY_SIZE(sparx5_dcb_apptrust_policies); i++) {
		if (sparx5_dcb_apptrust_policies[i].nselectors != nselectors)
			continue;
		match = true;
		for (ii = 0; ii < nselectors; ii++) {
			if (sparx5_dcb_apptrust_policies[i].selectors[ii] !=
			    *(selectors + ii)) {
				match = false;
				break;
			}
		}
		if (match)
			break;
	}

	/* Requested trust configuration is not supported */
	if (!match) {
		netdev_err(dev, "Valid apptrust configurations are:\n");
		for (i = 0; i < ARRAY_SIZE(sparx5_dcb_apptrust_names); i++)
			pr_info("order: %s\n", sparx5_dcb_apptrust_names[i]);
		*err = -EOPNOTSUPP;
	}

	return i;
}

static bool sparx5_dcb_apptrust_contains(int portno, u8 selector)
{
	const struct sparx5_dcb_apptrust *conf = sparx5_port_apptrust[portno];
	int i;

	for (i = 0; i < conf->nselectors; i++)
		if (conf->selectors[i] == selector)
			return true;

	return false;
}

static int sparx5_dcb_app_update(struct net_device *dev)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5_port_qos_dscp_map *dscp_map;
	struct sparx5_port_qos_pcp_map *pcp_map;
	struct sparx5_port_qos qos = {0};
	struct dcb_app app_itr = {0};
	int portno = port->portno;
	int i;

	dscp_map = &qos.dscp.map;
	pcp_map = &qos.pcp.map;

	/* Get default prio. */
	qos.default_prio = dcb_ieee_getapp_default_prio_mask(dev);
	if (qos.default_prio)
		qos.default_prio = fls(qos.default_prio) - 1;

	/* Get dscp ingress mapping */
	for (i = 0; i < ARRAY_SIZE(dscp_map->map); i++) {
		app_itr.selector = IEEE_8021QAZ_APP_SEL_DSCP;
		app_itr.protocol = i;
		dscp_map->map[i] = dcb_getapp(dev, &app_itr);
	}

	/* Get pcp ingress mapping */
	for (i = 0; i < ARRAY_SIZE(pcp_map->map); i++) {
		app_itr.selector = DCB_APP_SEL_PCP;
		app_itr.protocol = i;
		pcp_map->map[i] = dcb_getapp(dev, &app_itr);
	}

	/* Enable use of pcp for queue classification ? */
	if (sparx5_dcb_apptrust_contains(portno, DCB_APP_SEL_PCP)) {
		qos.pcp.qos_enable = true;
		qos.pcp.dp_enable = qos.pcp.qos_enable;
	}

	/* Enable use of dscp for queue classification ? */
	if (sparx5_dcb_apptrust_contains(portno, IEEE_8021QAZ_APP_SEL_DSCP)) {
		qos.dscp.qos_enable = true;
		qos.dscp.dp_enable = qos.dscp.qos_enable;
	}

	return sparx5_port_qos_set(port, &qos);
}

/* Set or delete dscp app entry.
 *
 * Dscp mapping is global for all ports, so set and delete app entries are
 * replicated for each port.
 */
static int sparx5_dcb_ieee_dscp_setdel_app(struct net_device *dev,
					   struct dcb_app *app, bool del)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct dcb_app apps[SPX5_PORTS];
	struct sparx5_port *port_itr;
	int err, i;

	for (i = 0; i < SPX5_PORTS; i++) {
		port_itr = port->sparx5->ports[i];
		if (!port_itr)
			continue;
		memcpy(&apps[i], app, sizeof(struct dcb_app));
		if (del)
			err = dcb_ieee_delapp(port_itr->ndev, &apps[i]);
		else
			err = dcb_ieee_setapp(port_itr->ndev, &apps[i]);
		if (err)
			return err;
	}

	return 0;
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

	if (app->selector == IEEE_8021QAZ_APP_SEL_DSCP)
		err = sparx5_dcb_ieee_dscp_setdel_app(dev, app, false);
	else
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

	if (app->selector == IEEE_8021QAZ_APP_SEL_DSCP)
		err = sparx5_dcb_ieee_dscp_setdel_app(dev, app, true);
	else
		err = dcb_ieee_delapp(dev, app);

	if (err < 0)
		return err;

	return sparx5_dcb_app_update(dev);
}

static int sparx5_dcb_setapptrust(struct net_device *dev, u8 *selectors,
				  int nselectors)
{
	struct sparx5_port *port = netdev_priv(dev);
	int err = 0, idx;

	idx = sparx5_dcb_apptrust_validate(dev, selectors, nselectors, &err);
	if (err < 0)
		return err;

	sparx5_port_apptrust[port->portno] = &sparx5_dcb_apptrust_policies[idx];

	return sparx5_dcb_app_update(dev);
}

static int sparx5_dcb_getapptrust(struct net_device *dev, u8 *selectors,
				  int *nselectors)
{
	struct sparx5_port *port = netdev_priv(dev);
	const struct sparx5_dcb_apptrust *trust;

	trust = sparx5_port_apptrust[port->portno];

	memcpy(selectors, trust->selectors, trust->nselectors);
	*nselectors = trust->nselectors;

	return 0;
}

const struct dcbnl_rtnl_ops sparx5_dcbnl_ops = {
	.ieee_setapp = sparx5_dcb_ieee_setapp,
	.ieee_delapp = sparx5_dcb_ieee_delapp,
	.dcbnl_setapptrust = sparx5_dcb_setapptrust,
	.dcbnl_getapptrust = sparx5_dcb_getapptrust,
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
		/* Initialize [dscp, pcp] default trust */
		sparx5_port_apptrust[port->portno] =
			&sparx5_dcb_apptrust_policies
				[SPARX5_DCB_APPTRUST_DSCP_PCP];
	}

	return 0;
}
