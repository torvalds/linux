/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_QOS_H__
#define __SPARX5_QOS_H__

#include <linux/netdevice.h>

/* Multi-Queue Priority */
int sparx5_tc_mqprio_add(struct net_device *ndev, u8 num_tc);
int sparx5_tc_mqprio_del(struct net_device *ndev);

#endif	/* __SPARX5_QOS_H__ */
