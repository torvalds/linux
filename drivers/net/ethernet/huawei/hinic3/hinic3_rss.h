/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_RSS_H_
#define _HINIC3_RSS_H_

#include <linux/netdevice.h>

int hinic3_rss_init(struct net_device *netdev);
void hinic3_rss_uninit(struct net_device *netdev);
void hinic3_try_to_enable_rss(struct net_device *netdev);
void hinic3_clear_rss_config(struct net_device *netdev);

#endif
