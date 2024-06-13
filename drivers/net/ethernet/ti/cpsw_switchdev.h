/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments Ethernet Switch Driver
 */

#ifndef DRIVERS_NET_ETHERNET_TI_CPSW_SWITCHDEV_H_
#define DRIVERS_NET_ETHERNET_TI_CPSW_SWITCHDEV_H_

#include <net/switchdev.h>

bool cpsw_port_dev_check(const struct net_device *dev);
int cpsw_switchdev_register_notifiers(struct cpsw_common *cpsw);
void cpsw_switchdev_unregister_notifiers(struct cpsw_common *cpsw);

#endif /* DRIVERS_NET_ETHERNET_TI_CPSW_SWITCHDEV_H_ */
