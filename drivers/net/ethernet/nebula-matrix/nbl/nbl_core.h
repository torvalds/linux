/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */

#ifndef _NBL_CORE_H_
#define _NBL_CORE_H_

#include <linux/pci.h>
#include "nbl_include/nbl_include.h"
#include "nbl_include/nbl_def_common.h"

enum {
	NBL_CAP_HAS_NET_BIT,
};

struct nbl_core {
	struct nbl_hw_mgt *hw_mgt;
};

struct nbl_adapter {
	struct pci_dev *pdev;
	struct nbl_core core;
	struct nbl_common_info common;
};

struct nbl_adapter *nbl_core_init(struct pci_dev *pdev,
				  struct nbl_init_param *param);
void nbl_core_remove(struct nbl_adapter *adapter);

#endif
