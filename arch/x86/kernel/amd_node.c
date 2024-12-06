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

#define DF_BLK_INST_CNT		0x040
#define	DF_CFG_ADDR_CNTL_LEGACY	0x084
#define	DF_CFG_ADDR_CNTL_DF4	0xC04

#define DF_MAJOR_REVISION	GENMASK(27, 24)

static u16 get_cfg_addr_cntl_offset(struct pci_dev *df_f0)
{
	u32 reg;

	/*
	 * Revision fields added for DF4 and later.
	 *
	 * Major revision of '0' is found pre-DF4. Field is Read-as-Zero.
	 */
	if (pci_read_config_dword(df_f0, DF_BLK_INST_CNT, &reg))
		return 0;

	if (reg & DF_MAJOR_REVISION)
		return DF_CFG_ADDR_CNTL_DF4;

	return DF_CFG_ADDR_CNTL_LEGACY;
}

struct pci_dev *amd_node_get_root(u16 node)
{
	struct pci_dev *root;
	u16 cntl_off;
	u8 bus;

	if (!cpu_feature_enabled(X86_FEATURE_ZEN))
		return NULL;

	/*
	 * D18F0xXXX [Config Address Control] (DF::CfgAddressCntl)
	 * Bits [7:0] (SecBusNum) holds the bus number of the root device for
	 * this Data Fabric instance. The segment, device, and function will be 0.
	 */
	struct pci_dev *df_f0 __free(pci_dev_put) = amd_node_get_func(node, 0);
	if (!df_f0)
		return NULL;

	cntl_off = get_cfg_addr_cntl_offset(df_f0);
	if (!cntl_off)
		return NULL;

	if (pci_read_config_byte(df_f0, cntl_off, &bus))
		return NULL;

	/* Grab the pointer for the actual root device instance. */
	root = pci_get_domain_bus_and_slot(0, bus, 0);

	pci_dbg(root, "is root for AMD node %u\n", node);
	return root;
}
