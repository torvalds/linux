/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_SWITCHDEV_H_
#define _PRESTERA_SWITCHDEV_H_

int prestera_switchdev_init(struct prestera_switch *sw);
void prestera_switchdev_fini(struct prestera_switch *sw);

int prestera_bridge_port_event(struct net_device *dev, unsigned long event,
			       void *ptr);

#endif /* _PRESTERA_SWITCHDEV_H_ */
