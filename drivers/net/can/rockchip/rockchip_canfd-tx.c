// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include "rockchip_canfd.h"

int rkcanfd_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	return NETDEV_TX_OK;
}
