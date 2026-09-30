// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025-2026 Intel Corporation
 */

#include <linux/pci.h>
#include <linux/gfp.h>

#include "iwl-io.h"
#include "pcie/utils.h"

void iwl_trans_pcie_dump_regs(struct iwl_trans *trans, struct pci_dev *pdev)
{
#define PCI_DUMP_SIZE		352
#define PCI_MEM_DUMP_SIZE	64
#define PCI_PARENT_DUMP_SIZE	524
#define PREFIX_LEN		32

	static bool pcie_dbg_dumped_once = 0;
	u32 i, pos, alloc_size, *ptr, *buf;
	char *prefix;

	if (pcie_dbg_dumped_once)
		return;

	/* Should be a multiple of 4 */
	BUILD_BUG_ON(PCI_DUMP_SIZE > 4096 || PCI_DUMP_SIZE & 0x3);
	BUILD_BUG_ON(PCI_MEM_DUMP_SIZE > 4096 || PCI_MEM_DUMP_SIZE & 0x3);
	BUILD_BUG_ON(PCI_PARENT_DUMP_SIZE > 4096 || PCI_PARENT_DUMP_SIZE & 0x3);

	/* Alloc a max size buffer */
	alloc_size = PCI_ERR_ROOT_ERR_SRC +  4 + PREFIX_LEN;
	alloc_size = max_t(u32, alloc_size, PCI_DUMP_SIZE + PREFIX_LEN);
	alloc_size = max_t(u32, alloc_size, PCI_MEM_DUMP_SIZE + PREFIX_LEN);
	alloc_size = max_t(u32, alloc_size, PCI_PARENT_DUMP_SIZE + PREFIX_LEN);

	buf = kmalloc(alloc_size, GFP_ATOMIC);
	if (!buf)
		return;
	prefix = (char *)buf + alloc_size - PREFIX_LEN;

	IWL_ERR(trans, "iwlwifi transaction failed, dumping registers\n");

	/* Print wifi device registers */
	sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));
	IWL_ERR(trans, "iwlwifi device config registers:\n");
	for (i = 0, ptr = buf; i < PCI_DUMP_SIZE; i += 4, ptr++)
		if (pci_read_config_dword(pdev, i, ptr))
			goto err_read;
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	IWL_ERR(trans, "iwlwifi device memory mapped registers:\n");
	for (i = 0, ptr = buf; i < PCI_MEM_DUMP_SIZE; i += 4, ptr++)
		*ptr = iwl_read32(trans, i);
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		IWL_ERR(trans, "iwlwifi device AER capability structure:\n");
		for (i = 0, ptr = buf; i < PCI_ERR_ROOT_COMMAND; i += 4, ptr++)
			if (pci_read_config_dword(pdev, pos + i, ptr))
				goto err_read;
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET,
			       32, 4, buf, i, 0);
	}

	/* Print parent device registers next */
	if (!pdev->bus->self)
		goto out;

	pdev = pdev->bus->self;
	sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));

	IWL_ERR(trans, "iwlwifi parent port (%s) config registers:\n",
		pci_name(pdev));
	for (i = 0, ptr = buf; i < PCI_PARENT_DUMP_SIZE; i += 4, ptr++)
		if (pci_read_config_dword(pdev, i, ptr))
			goto err_read;
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);

	/* Print root port AER registers */
	pos = 0;
	pdev = pcie_find_root_port(pdev);
	if (pdev)
		pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ERR);
	if (pos) {
		IWL_ERR(trans, "iwlwifi root port (%s) AER cap structure:\n",
			pci_name(pdev));
		sprintf(prefix, "iwlwifi %s: ", pci_name(pdev));
		for (i = 0, ptr = buf; i <= PCI_ERR_ROOT_ERR_SRC; i += 4, ptr++)
			if (pci_read_config_dword(pdev, pos + i, ptr))
				goto err_read;
		print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32,
			       4, buf, i, 0);
	}
	goto out;

err_read:
	print_hex_dump(KERN_ERR, prefix, DUMP_PREFIX_OFFSET, 32, 4, buf, i, 0);
	IWL_ERR(trans, "Read failed at 0x%X\n", i);
out:
	pcie_dbg_dumped_once = 1;
	kfree(buf);
}

u32 iwl_pcie_read_direct32(struct iwl_trans *trans, u32 reg)
{
	if (iwl_trans_grab_nic_access(trans)) {
		u32 value = iwl_read32(trans, reg);

		iwl_trans_release_nic_access(trans);
		return value;
	}

	/* return as if we have a HW timeout/failure */
	return 0x5a5a5a5a;
}

#define IWL_PCIE_POLL_INTERVAL 10	/* microseconds */

int iwl_pcie_poll_direct_bit(struct iwl_trans *trans,
			     u32 addr, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_pcie_read_direct32(trans, addr) & mask) == mask)
			return t;
		udelay(IWL_PCIE_POLL_INTERVAL);
		t += IWL_PCIE_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

int iwl_pcie_poll_prph_bit(struct iwl_trans *trans, u32 addr,
			   u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_prph(trans, addr) & mask) == (bits & mask))
			return 0;
		udelay(IWL_PCIE_POLL_INTERVAL);
		t += IWL_PCIE_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

int iwl_pcie_poll_umac_prph_bit(struct iwl_trans *trans,
				u32 addr, u32 bits, u32 mask,
				int timeout)
{
	return iwl_pcie_poll_prph_bit(trans, addr +
				      trans->mac_cfg->umac_prph_offset,
				      bits, mask, timeout);
}

int iwl_pcie_poll_umac_prph_bits_no_grab(struct iwl_trans *trans, u32 addr,
					 u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_umac_prph_no_grab(trans, addr) & mask) ==
		    (bits & mask))
			return 0;
		udelay(IWL_PCIE_POLL_INTERVAL);
		t += IWL_PCIE_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

void iwl_pcie_write_prph64_no_grab(struct iwl_trans *trans, u32 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite_prph64(trans->dev, ofs, val);
	iwl_write_prph_no_grab(trans, ofs, val & 0xffffffff);
	iwl_write_prph_no_grab(trans, ofs + 4, val >> 32);
}

void iwl_pcie_write_direct64(struct iwl_trans *trans, u64 reg, u64 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_pcie_write64(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}
