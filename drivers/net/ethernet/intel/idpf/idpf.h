/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_H_
#define _IDPF_H_

#include <linux/aer.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "idpf_controlq.h"

/* available message levels */
#define IDPF_AVAIL_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

/**
 * struct idpf_adapter - Device data struct generated on probe
 * @pdev: PCI device struct given on probe
 * @msg_enable: Debug message level enabled
 * @hw: Device access data
 */
struct idpf_adapter {
	struct pci_dev *pdev;
	u32 msg_enable;
	struct idpf_hw hw;
};

#endif /* !_IDPF_H_ */
