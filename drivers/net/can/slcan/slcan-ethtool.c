// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2022 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 *
 */

#include <linux/can/dev.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>

#include "slcan.h"

static const char slcan_priv_flags_strings[][ETH_GSTRING_LEN] = {
#define SLCAN_PRIV_FLAGS_ERR_RST_ON_OPEN BIT(0)
	"err-rst-on-open",
};

static void slcan_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		memcpy(data, slcan_priv_flags_strings,
		       sizeof(slcan_priv_flags_strings));
	}
}

static u32 slcan_get_priv_flags(struct net_device *ndev)
{
	u32 flags = 0;

	if (slcan_err_rst_on_open(ndev))
		flags |= SLCAN_PRIV_FLAGS_ERR_RST_ON_OPEN;

	return flags;
}

static int slcan_set_priv_flags(struct net_device *ndev, u32 flags)
{
	bool err_rst_op_open = !!(flags & SLCAN_PRIV_FLAGS_ERR_RST_ON_OPEN);

	return slcan_enable_err_rst_on_open(ndev, err_rst_op_open);
}

static int slcan_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_PRIV_FLAGS:
		return ARRAY_SIZE(slcan_priv_flags_strings);
	default:
		return -EOPNOTSUPP;
	}
}

const struct ethtool_ops slcan_ethtool_ops = {
	.get_strings = slcan_get_strings,
	.get_priv_flags = slcan_get_priv_flags,
	.set_priv_flags = slcan_set_priv_flags,
	.get_sset_count = slcan_get_sset_count,
	.get_ts_info = ethtool_op_get_ts_info,
};
