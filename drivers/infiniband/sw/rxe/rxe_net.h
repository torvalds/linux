/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#ifndef RXE_NET_H
#define RXE_NET_H

#include <net/sock.h>
#include <net/if_inet6.h>
#include <linux/module.h>

struct rxe_recv_sockets {
	struct socket *sk4;
	struct socket *sk6;
};

int rxe_net_add(const char *ibdev_name, struct net_device *ndev);

int rxe_net_init(void);
void rxe_net_exit(void);

#endif /* RXE_NET_H */
