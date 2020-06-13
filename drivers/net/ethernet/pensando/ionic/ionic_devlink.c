// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/module.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_devlink.h"

static int ionic_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			     struct netlink_ext_ack *extack)
{
	struct ionic *ionic = devlink_priv(dl);
	struct ionic_dev *idev = &ionic->idev;
	char buf[16];
	int err = 0;

	err = devlink_info_driver_name_put(req, IONIC_DRV_NAME);
	if (err)
		return err;

	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       idev->dev_info.fw_version);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "0x%x", idev->dev_info.asic_type);
	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					     buf);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "0x%x", idev->dev_info.asic_rev);
	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_ASIC_REV,
					     buf);
	if (err)
		return err;

	err = devlink_info_serial_number_put(req, idev->dev_info.serial_num);

	return err;
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

int ionic_devlink_register(struct ionic *ionic)
{
	struct devlink *dl = priv_to_devlink(ionic);
	int err;

	err = devlink_register(dl, ionic->dev);
	if (err) {
		dev_warn(ionic->dev, "devlink_register failed: %d\n", err);
		return err;
	}

	devlink_port_attrs_set(&ionic->dl_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       0, false, 0, NULL, 0);
	err = devlink_port_register(dl, &ionic->dl_port, 0);
	if (err)
		dev_err(ionic->dev, "devlink_port_register failed: %d\n", err);
	else
		devlink_port_type_eth_set(&ionic->dl_port,
					  ionic->master_lif->netdev);

	return err;
}

void ionic_devlink_unregister(struct ionic *ionic)
{
	struct devlink *dl = priv_to_devlink(ionic);

	if (ionic->dl_port.registered)
		devlink_port_unregister(&ionic->dl_port);
	devlink_unregister(dl);
}
