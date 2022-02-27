// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)

#include "funeth.h"
#include "funeth_devlink.h"

static int fun_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			   struct netlink_ext_ack *extack)
{
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	return 0;
}

static const struct devlink_ops fun_dl_ops = {
	.info_get = fun_dl_info_get,
};

struct devlink *fun_devlink_alloc(struct device *dev)
{
	return devlink_alloc(&fun_dl_ops, sizeof(struct fun_ethdev), dev);
}

void fun_devlink_free(struct devlink *devlink)
{
	devlink_free(devlink);
}

void fun_devlink_register(struct devlink *devlink)
{
	devlink_register(devlink);
}

void fun_devlink_unregister(struct devlink *devlink)
{
	devlink_unregister(devlink);
}
