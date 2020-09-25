// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105.h"

static int sja1105_best_effort_vlan_filtering_get(struct sja1105_private *priv,
						  bool *be_vlan)
{
	*be_vlan = priv->best_effort_vlan_filtering;

	return 0;
}

static int sja1105_best_effort_vlan_filtering_set(struct sja1105_private *priv,
						  bool be_vlan)
{
	struct dsa_switch *ds = priv->ds;
	bool vlan_filtering;
	int port;
	int rc;

	priv->best_effort_vlan_filtering = be_vlan;

	rtnl_lock();
	for (port = 0; port < ds->num_ports; port++) {
		struct dsa_port *dp;

		if (!dsa_is_user_port(ds, port))
			continue;

		dp = dsa_to_port(ds, port);
		vlan_filtering = dsa_port_is_vlan_filtering(dp);

		rc = sja1105_vlan_filtering(ds, port, vlan_filtering);
		if (rc)
			break;
	}
	rtnl_unlock();

	return rc;
}

enum sja1105_devlink_param_id {
	SJA1105_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	SJA1105_DEVLINK_PARAM_ID_BEST_EFFORT_VLAN_FILTERING,
};

int sja1105_devlink_param_get(struct dsa_switch *ds, u32 id,
			      struct devlink_param_gset_ctx *ctx)
{
	struct sja1105_private *priv = ds->priv;
	int err;

	switch (id) {
	case SJA1105_DEVLINK_PARAM_ID_BEST_EFFORT_VLAN_FILTERING:
		err = sja1105_best_effort_vlan_filtering_get(priv,
							     &ctx->val.vbool);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

int sja1105_devlink_param_set(struct dsa_switch *ds, u32 id,
			      struct devlink_param_gset_ctx *ctx)
{
	struct sja1105_private *priv = ds->priv;
	int err;

	switch (id) {
	case SJA1105_DEVLINK_PARAM_ID_BEST_EFFORT_VLAN_FILTERING:
		err = sja1105_best_effort_vlan_filtering_set(priv,
							     ctx->val.vbool);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static const struct devlink_param sja1105_devlink_params[] = {
	DSA_DEVLINK_PARAM_DRIVER(SJA1105_DEVLINK_PARAM_ID_BEST_EFFORT_VLAN_FILTERING,
				 "best_effort_vlan_filtering",
				 DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
};

static int sja1105_setup_devlink_params(struct dsa_switch *ds)
{
	return dsa_devlink_params_register(ds, sja1105_devlink_params,
					   ARRAY_SIZE(sja1105_devlink_params));
}

static void sja1105_teardown_devlink_params(struct dsa_switch *ds)
{
	dsa_devlink_params_unregister(ds, sja1105_devlink_params,
				      ARRAY_SIZE(sja1105_devlink_params));
}

int sja1105_devlink_setup(struct dsa_switch *ds)
{
	int rc;

	rc = sja1105_setup_devlink_params(ds);
	if (rc)
		return rc;

	return 0;
}

void sja1105_devlink_teardown(struct dsa_switch *ds)
{
	sja1105_teardown_devlink_params(ds);
}
