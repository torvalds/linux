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

#ifndef __GDM_WIMAX_H__
#define __GDM_WIMAX_H__

#include <linux/netdevice.h>
#include <linux/types.h>
#include "wm_ioctl.h"
#if defined(CONFIG_WIMAX_GDM72XX_QOS)
#include "gdm_qos.h"
#endif

#define DRIVER_VERSION		"3.2.3"

/*#define ETH_P_IP	0x0800 */
/*#define ETH_P_ARP	0x0806 */
/*#define ETH_P_IPV6	0x86DD */

#define H2L(x)		__cpu_to_le16(x)
#define L2H(x)		__le16_to_cpu(x)
#define DH2L(x)		__cpu_to_le32(x)
#define DL2H(x)		__le32_to_cpu(x)

#define H2B(x)		__cpu_to_be16(x)
#define B2H(x)		__be16_to_cpu(x)
#define DH2B(x)		__cpu_to_be32(x)
#define DB2H(x)		__be32_to_cpu(x)

struct phy_dev {
	void	*priv_dev;
	struct net_device	*netdev;

	int	(*send_func)(void *priv_dev, void *data, int len,
			void (*cb)(void *cb_data), void *cb_data);
	int	(*rcv_func)(void *priv_dev,
			void (*cb)(void *cb_data, void *data, int len),
			void *cb_data);
};

struct nic {
	struct net_device	*netdev;
	struct phy_dev		*phy_dev;

	struct net_device_stats	stats;

	struct data_s	sdk_data[SIOC_DATA_MAX];

#if defined(CONFIG_WIMAX_GDM72XX_QOS)
	struct qos_cb_s	qos;
#endif

};

/*#define LOOPBACK_TEST */

extern int register_wimax_device(struct phy_dev *phy_dev, struct device *pdev);
extern int gdm_wimax_send_tx(struct sk_buff *skb, struct net_device *dev);
extern void unregister_wimax_device(struct phy_dev *phy_dev);

#endif
