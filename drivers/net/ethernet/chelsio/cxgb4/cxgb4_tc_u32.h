/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CXGB4_TC_U32_H
#define __CXGB4_TC_U32_H

#include <net/pkt_cls.h>

#define CXGB4_MAX_LINK_HANDLE 32

static inline bool can_tc_u32_offload(struct net_device *dev)
{
	struct adapter *adap = netdev2adap(dev);

	return (dev->features & NETIF_F_HW_TC) && adap->tc_u32 ? true : false;
}

int cxgb4_config_knode(struct net_device *dev, __be16 protocol,
		       struct tc_cls_u32_offload *cls);
int cxgb4_delete_knode(struct net_device *dev, __be16 protocol,
		       struct tc_cls_u32_offload *cls);

void cxgb4_cleanup_tc_u32(struct adapter *adapter);
struct cxgb4_tc_u32_table *cxgb4_init_tc_u32(struct adapter *adap,
					     unsigned int size);
#endif /* __CXGB4_TC_U32_H */
