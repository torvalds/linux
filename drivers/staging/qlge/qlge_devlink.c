// SPDX-License-Identifier: GPL-2.0-or-later
#include "qlge.h"
#include "qlge_devlink.h"

static int
qlge_reporter_coredump(struct devlink_health_reporter *reporter,
		       struct devlink_fmsg *fmsg, void *priv_ctx,
		       struct netlink_ext_ack *extack)
{
	return 0;
}

static const struct devlink_health_reporter_ops qlge_reporter_ops = {
	.name = "dummy",
	.dump = qlge_reporter_coredump,
};

void qlge_health_create_reporters(struct qlge_adapter *priv)
{
	struct devlink_health_reporter *reporter;
	struct devlink *devlink;

	devlink = priv_to_devlink(priv);
	priv->reporter =
		devlink_health_reporter_create(devlink, &qlge_reporter_ops,
					       0, priv);
	if (IS_ERR(priv->reporter))
		netdev_warn(priv->ndev,
			    "Failed to create reporter, err = %ld\n",
			    PTR_ERR(reporter));
}
