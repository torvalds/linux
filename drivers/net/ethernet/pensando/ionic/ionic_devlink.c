// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_devlink.h"

static int ionic_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			     struct netlink_ext_ack *extack)
{
	return devlink_info_driver_name_put(req, IONIC_DRV_NAME);
}

static const struct devlink_ops ionic_dl_ops = {
	.info_get	= ionic_dl_info_get,
};

struct ionic *ionic_devlink_alloc(struct device *dev)
{
	struct devlink *dl;

	dl = devlink_alloc(&ionic_dl_ops, sizeof(struct ionic));

	return devlink_priv(dl);
}

void ionic_devlink_free(struct ionic *ionic)
{
	struct devlink *dl = priv_to_devlink(ionic);

	devlink_free(dl);
}
