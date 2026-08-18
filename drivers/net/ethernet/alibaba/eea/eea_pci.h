/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_PCI_H__
#define __EEA_PCI_H__

#include <linux/pci.h>

#include "eea_net.h"
#include "eea_ring.h"

enum eea_pci_status {
	EEA_PCI_STATUS_NONE,
	EEA_PCI_STATUS_ERR,
	EEA_PCI_STATUS_READY,
	EEA_PCI_STATUS_DONE,
};

struct eea_irq_blk;

struct eea_pci_cap {
	__u8 cap_vndr;
	__u8 cap_next;
	__u8 cap_len;
	__u8 cfg_type;
};

struct eea_pci_reset_reg {
	struct eea_pci_cap cap;
	__le16 driver;
	__le16 device;
};

struct eea_pci_device;

struct eea_device {
	struct eea_pci_device *ep_dev;
	struct device         *dma_dev;
	struct eea_net        *enet;

	u64 features;

	enum eea_pci_status status;
	bool ha_reset_netdev_running;

	/* ha lock for the race between ha work and pci remove */
	struct mutex ha_lock;

	u32 rx_num;
	u32 tx_num;
	u32 db_blk_size;
};

const char *eea_pci_name(struct eea_device *edev);
int eea_pci_domain_nr(struct eea_device *edev);
u16 eea_pci_bdf(struct eea_device *edev);

int eea_device_reset(struct eea_device *dev);
int eea_pci_set_aq_up(struct eea_device *dev);
int eea_pci_active_aq(struct eea_ring *ering, int msix_vec);

int eea_pci_request_irq(struct eea_device *edev, struct eea_irq_blk *blk,
			irqreturn_t (*callback)(int irq, void *data));
void eea_pci_free_irq(struct eea_irq_blk *blk);

u64 eea_pci_device_ts(struct eea_device *edev);

void __iomem *eea_pci_db_addr(struct eea_device *edev, u32 off);
#endif
