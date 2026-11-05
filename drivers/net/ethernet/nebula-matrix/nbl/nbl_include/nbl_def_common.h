/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */

#ifndef _NBL_DEF_COMMON_H_
#define _NBL_DEF_COMMON_H_

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include "nbl_include.h"

struct nbl_common_info {
	struct pci_dev *pdev;
	struct device *dev;
	u16 vsi_id;
	u8 eth_id;
	u8 logic_eth_id;
	u8 eth_num;

	u8 function;
	u8 devid;
	u8 bus;
	u8 hw_bus;

	u8 has_ctrl;
	u8 has_net;
};

#endif
