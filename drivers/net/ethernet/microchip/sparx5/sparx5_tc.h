/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_TC_H__
#define __SPARX5_TC_H__

#include <linux/netdevice.h>

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data);

#endif	/* __SPARX5_TC_H__ */
