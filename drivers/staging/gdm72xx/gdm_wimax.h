/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __GDM72XX_GDM_WIMAX_H__
#define __GDM72XX_GDM_WIMAX_H__

#include <linux/netdevice.h>
#include <linux/types.h>
#include "wm_ioctl.h"
#if defined(CONFIG_WIMAX_GDM72XX_QOS)
#include "gdm_qos.h"
#endif

#define DRIVER_VERSION		"3.2.3"

struct phy_dev {
	void			*priv_dev;
	struct net_device	*netdev;
	int (*send_func)(void *priv_dev, void *data, int len,
			 void (*cb)(void *cb_data), void *cb_data);
	int (*rcv_func)(void *priv_dev,
			void (*cb)(void *cb_data, void *data, int len),
			void *cb_data);
};

struct nic {
	struct net_device	*netdev;
	struct phy_dev		*phy_dev;
	struct data_s		sdk_data[SIOC_DATA_MAX];
#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	struct qos_cb_s		qos;
#endif
};

int register_wimax_device(struct phy_dev *phy_dev, struct device *pdev);
int gdm_wimax_send_tx(struct sk_buff *skb, struct net_device *dev);
void unregister_wimax_device(struct phy_dev *phy_dev);

#endif /* __GDM72XX_GDM_WIMAX_H__ */
