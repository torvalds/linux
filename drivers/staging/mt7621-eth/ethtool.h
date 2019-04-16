/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef MTK_ETHTOOL_H
#define MTK_ETHTOOL_H

#include <linux/ethtool.h>

void mtk_set_ethtool_ops(struct net_device *netdev);

#endif /* MTK_ETHTOOL_H */
