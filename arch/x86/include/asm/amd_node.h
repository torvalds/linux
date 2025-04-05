/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Node helper functions and common defines
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 *
 * Note:
 * Items in this file may only be used in a single place.
 * However, it's prudent to keep all AMD Node functionality
 * in a unified place rather than spreading throughout the
 * kernel.
 */

#ifndef _ASM_X86_AMD_NODE_H_
#define _ASM_X86_AMD_NODE_H_

#include <linux/pci.h>

#define MAX_AMD_NUM_NODES	8
#define AMD_NODE0_PCI_SLOT	0x18

struct pci_dev *amd_node_get_func(u16 node, u8 func);
struct pci_dev *amd_node_get_root(u16 node);

static inline u16 amd_num_nodes(void)
{
	return topology_amd_nodes_per_pkg() * topology_max_packages();
}

int __must_check amd_smn_read(u16 node, u32 address, u32 *value);
int __must_check amd_smn_write(u16 node, u32 address, u32 value);

#endif /*_ASM_X86_AMD_NODE_H_*/
