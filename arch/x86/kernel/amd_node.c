// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Node helper functions and common defines
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include <asm/amd_node.h>

/*
 * AMD Nodes are a physical collection of I/O devices within an SoC. There can be one
 * or more nodes per package.
 *
 * The nodes are software-visible through PCI config space. All nodes are enumerated
 * on segment 0 bus 0. The device (slot) numbers range from 0x18 to 0x1F (maximum 8
 * nodes) with 0x18 corresponding to node 0, 0x19 to node 1, etc. Each node can be a
 * multi-function device.
 *
 * On legacy systems, these node devices represent integrated Northbridge functionality.
 * On Zen-based systems, these node devices represent Data Fabric functionality.
 *
 * See "Configuration Space Accesses" section in BKDGs or
 * "Processor x86 Core" -> "Configuration Space" section in PPRs.
 */
struct pci_dev *amd_node_get_func(u16 node, u8 func)
{
	if (node >= MAX_AMD_NUM_NODES)
		return NULL;

	return pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(AMD_NODE0_PCI_SLOT + node, func));
}
