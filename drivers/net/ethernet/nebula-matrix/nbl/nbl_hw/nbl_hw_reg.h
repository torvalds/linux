/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */

#ifndef _NBL_HW_REG_H_
#define _NBL_HW_REG_H_

#include <linux/types.h>

#include "../nbl_include/nbl_def_hw.h"
#include "../nbl_include/nbl_def_common.h"
#include "../nbl_core.h"

#define NBL_MEMORY_BAR				0
#define NBL_MAILBOX_BAR				2
#define NBL_RDMA_NOTIFY_LEN			(8ULL << 10)
#define NBL_REG_NET_ONLY_LEN			(8ULL << 10)
/*
 * PCI MEMORY BAR total size: 64MiB.
 */
#define NBL_MEM_BAR_TOTAL_SIZE			(64ULL << 20)

struct nbl_hw_mgt {
	struct nbl_common_info *common;
	u8 __iomem *hw_addr;
	u8 __iomem *mailbox_bar_hw_addr;
	resource_size_t mailbox_bar_size;
};

#endif
