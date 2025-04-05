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

static struct pci_dev **amd_roots;

/* Protect the PCI config register pairs used for SMN. */
static DEFINE_MUTEX(smn_mutex);

#define SMN_INDEX_OFFSET	0x60
#define SMN_DATA_OFFSET		0x64

/*
 * SMN accesses may fail in ways that are difficult to detect here in the called
 * functions amd_smn_read() and amd_smn_write(). Therefore, callers must do
 * their own checking based on what behavior they expect.
 *
 * For SMN reads, the returned value may be zero if the register is Read-as-Zero.
 * Or it may be a "PCI Error Response", e.g. all 0xFFs. The "PCI Error Response"
 * can be checked here, and a proper error code can be returned.
 *
 * But the Read-as-Zero response cannot be verified here. A value of 0 may be
 * correct in some cases, so callers must check that this correct is for the
 * register/fields they need.
 *
 * For SMN writes, success can be determined through a "write and read back"
 * However, this is not robust when done here.
 *
 * Possible issues:
 *
 * 1) Bits that are "Write-1-to-Clear". In this case, the read value should
 *    *not* match the write value.
 *
 * 2) Bits that are "Read-as-Zero"/"Writes-Ignored". This information cannot be
 *    known here.
 *
 * 3) Bits that are "Reserved / Set to 1". Ditto above.
 *
 * Callers of amd_smn_write() should do the "write and read back" check
 * themselves, if needed.
 *
 * For #1, they can see if their target bits got cleared.
 *
 * For #2 and #3, they can check if their target bits got set as intended.
 *
 * This matches what is done for RDMSR/WRMSR. As long as there's no #GP, then
 * the operation is considered a success, and the caller does their own
 * checking.
 */
static int __amd_smn_rw(u8 i_off, u8 d_off, u16 node, u32 address, u32 *value, bool write)
{
	struct pci_dev *root;
	int err = -ENODEV;

	if (node >= amd_num_nodes())
		return err;

	root = amd_roots[node];
	if (!root)
		return err;

	guard(mutex)(&smn_mutex);

	err = pci_write_config_dword(root, i_off, address);
	if (err) {
		pr_warn("Error programming SMN address 0x%x.\n", address);
		return pcibios_err_to_errno(err);
	}

	err = (write ? pci_write_config_dword(root, d_off, *value)
		     : pci_read_config_dword(root, d_off, value));

	return pcibios_err_to_errno(err);
}

int __must_check amd_smn_read(u16 node, u32 address, u32 *value)
{
	int err = __amd_smn_rw(SMN_INDEX_OFFSET, SMN_DATA_OFFSET, node, address, value, false);

	if (PCI_POSSIBLE_ERROR(*value)) {
		err = -ENODEV;
		*value = 0;
	}

	return err;
}
EXPORT_SYMBOL_GPL(amd_smn_read);

int __must_check amd_smn_write(u16 node, u32 address, u32 value)
{
	return __amd_smn_rw(SMN_INDEX_OFFSET, SMN_DATA_OFFSET, node, address, &value, true);
}
EXPORT_SYMBOL_GPL(amd_smn_write);

static int amd_cache_roots(void)
{
	u16 node, num_nodes = amd_num_nodes();

	amd_roots = kcalloc(num_nodes, sizeof(*amd_roots), GFP_KERNEL);
	if (!amd_roots)
		return -ENOMEM;

	for (node = 0; node < num_nodes; node++)
		amd_roots[node] = amd_node_get_root(node);

	return 0;
}

static int __init amd_smn_init(void)
{
	int err;

	if (!cpu_feature_enabled(X86_FEATURE_ZEN))
		return 0;

	guard(mutex)(&smn_mutex);

	if (amd_roots)
		return 0;

	err = amd_cache_roots();
	if (err)
		return err;

	return 0;
}

fs_initcall(amd_smn_init);
