/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2013 Cisco Systems, Inc.  All rights reserved. */

#ifndef __ENIC_API_H__
#define __ENIC_API_H__

#include <linux/netdevice.h>

#include "vnic_dev.h"
#include "vnic_devcmd.h"

int enic_api_devcmd_proxy_by_index(struct net_device *netdev, int vf,
	enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1, int wait);

#endif
