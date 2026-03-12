/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020-2021 Texas Instruments Incorporated - https://www.ti.com
 */

#ifndef __NET_TI_ICSSM_SWITCHDEV_H
#define __NET_TI_ICSSM_SWITCHDEV_H

#include "icssm_prueth.h"

int icssm_prueth_sw_register_notifiers(struct prueth *prueth);
void icssm_prueth_sw_unregister_notifiers(struct prueth *prueth);
bool icssm_prueth_sw_port_dev_check(const struct net_device *ndev);
#endif /* __NET_TI_ICSSM_SWITCHDEV_H */
