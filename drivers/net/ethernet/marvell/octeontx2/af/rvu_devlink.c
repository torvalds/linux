// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Devlink
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include "rvu.h"

#define DRV_NAME "octeontx2-af"

static int rvu_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	return devlink_info_driver_name_put(req, DRV_NAME);
}

static const struct devlink_ops rvu_devlink_ops = {
	.info_get = rvu_devlink_info_get,
};

int rvu_register_dl(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl;
	struct devlink *dl;
	int err;

	rvu_dl = kzalloc(sizeof(*rvu_dl), GFP_KERNEL);
	if (!rvu_dl)
		return -ENOMEM;

	dl = devlink_alloc(&rvu_devlink_ops, sizeof(struct rvu_devlink));
	if (!dl) {
		dev_warn(rvu->dev, "devlink_alloc failed\n");
		kfree(rvu_dl);
		return -ENOMEM;
	}

	err = devlink_register(dl, rvu->dev);
	if (err) {
		dev_err(rvu->dev, "devlink register failed with error %d\n", err);
		devlink_free(dl);
		kfree(rvu_dl);
		return err;
	}

	rvu_dl->dl = dl;
	rvu_dl->rvu = rvu;
	rvu->rvu_dl = rvu_dl;
	return 0;
}

void rvu_unregister_dl(struct rvu *rvu)
{
	struct rvu_devlink *rvu_dl = rvu->rvu_dl;
	struct devlink *dl = rvu_dl->dl;

	if (!dl)
		return;

	devlink_unregister(dl);
	devlink_free(dl);
	kfree(rvu_dl);
}
