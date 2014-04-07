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

#ifndef _GDM_LTE_H_
#define _GDM_LTE_H_

#include <linux/netdevice.h>
#include <linux/types.h>

#include "gdm_endian.h"

#define MAX_NIC_TYPE		4
#define MAX_RX_SUBMIT_COUNT	3
#define DRIVER_VERSION		"3.7.17.0"

enum TX_ERROR_CODE {
	TX_NO_ERROR = 0,
	TX_NO_DEV,
	TX_NO_SPC,
	TX_NO_BUFFER,
};

enum CALLBACK_CONTEXT {
	KERNEL_THREAD = 0,
	USB_COMPLETE,
};

struct pdn_table {
	u8 activate;
	u32 dft_eps_id;
	u32 nic_type;
} __packed;

struct nic;

struct phy_dev {
	void	*priv_dev;
	struct net_device *dev[MAX_NIC_TYPE];
	int	(*send_hci_func)(void *priv_dev, void *data, int len,
			void (*cb)(void *cb_data), void *cb_data);
	int	(*send_sdu_func)(void *priv_dev, void *data, int len,
			unsigned int dftEpsId, unsigned int epsId,
			void (*cb)(void *cb_data), void *cb_data,
			int dev_idx, int nic_type);
	int	(*rcv_func)(void *priv_dev,
			int (*cb)(void *cb_data, void *data, int len,
				  int context),
			void *cb_data, int context);
	struct gdm_endian * (*get_endian)(void *priv_dev);
};

struct nic {
	struct net_device *netdev;
	struct phy_dev *phy_dev;
	struct net_device_stats stats;
	struct pdn_table pdn_table;
	u8 dest_mac_addr[ETH_ALEN];
	u8 src_mac_addr[ETH_ALEN];
	u32 nic_id;
	u16 vlan_id;
};

int gdm_lte_event_init(void);
void gdm_lte_event_exit(void);

void start_rx_proc(struct phy_dev *phy_dev);
int register_lte_device(struct phy_dev *phy_dev, struct device *dev,
			u8 *mac_address);
void unregister_lte_device(struct phy_dev *phy_dev);

#endif /* _GDM_LTE_H_ */
