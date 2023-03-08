// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phy.h>

#include "wx_type.h"
#include "wx_ethtool.h"

void wx_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info)
{
	struct wx *wx = netdev_priv(netdev);

	strscpy(info->driver, wx->driver_name, sizeof(info->driver));
	strscpy(info->fw_version, wx->eeprom_id, sizeof(info->fw_version));
	strscpy(info->bus_info, pci_name(wx->pdev), sizeof(info->bus_info));
}
EXPORT_SYMBOL(wx_get_drvinfo);
