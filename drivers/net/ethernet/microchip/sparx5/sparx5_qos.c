// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main.h"
#include "sparx5_qos.h"

int sparx5_tc_mqprio_add(struct net_device *ndev, u8 num_tc)
{
	int i;

	if (num_tc != SPX5_PRIOS) {
		netdev_err(ndev, "Only %d traffic classes supported\n",
			   SPX5_PRIOS);
		return -EINVAL;
	}

	netdev_set_num_tc(ndev, num_tc);

	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(ndev, i, 1, i);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

int sparx5_tc_mqprio_del(struct net_device *ndev)
{
	netdev_reset_tc(ndev);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}
