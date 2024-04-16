/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_RXTX_H_
#define _PRESTERA_RXTX_H_

#include <linux/netdevice.h>

struct prestera_switch;
struct prestera_port;

int prestera_rxtx_switch_init(struct prestera_switch *sw);
void prestera_rxtx_switch_fini(struct prestera_switch *sw);

int prestera_rxtx_port_init(struct prestera_port *port);

netdev_tx_t prestera_rxtx_xmit(struct prestera_port *port, struct sk_buff *skb);

#endif /* _PRESTERA_RXTX_H_ */
