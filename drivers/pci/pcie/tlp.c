// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe TLP Log handling
 *
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/aer.h>
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/build_bug.h>
#include <linux/minmax.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "../pci.h"

/**
 * aer_tlp_log_len - Calculate AER Capability TLP Header/Prefix Log length
 * @dev: PCIe device
 * @aercc: AER Capabilities and Control register value
 *
 * Return: TLP Header/Prefix Log length
 */
unsigned int aer_tlp_log_len(struct pci_dev *dev, u32 aercc)
{
	if (aercc & PCI_ERR_CAP_TLP_LOG_FLIT)
		return FIELD_GET(PCI_ERR_CAP_TLP_LOG_SIZE, aercc);

	return PCIE_STD_NUM_TLP_HEADERLOG +
	       ((aercc & PCI_ERR_CAP_PREFIX_LOG_PRESENT) ?
		dev->eetlp_prefix_max : 0);
}

#ifdef CONFIG_PCIE_DPC
/**
 * dpc_tlp_log_len - Calculate DPC RP PIO TLP Header/Prefix Log length
 * @dev: PCIe device
 *
 * Return: TLP Header/Prefix Log length
 */
unsigned int dpc_tlp_log_len(struct pci_dev *dev)
{
	/* Remove ImpSpec Log register from the count */
	if (dev->dpc_rp_log_size >= PCIE_STD_NUM_TLP_HEADERLOG + 1)
		return dev->dpc_rp_log_size - 1;

	return dev->dpc_rp_log_size;
}
#endif

/**
 * pcie_read_tlp_log - read TLP Header Log
 * @dev: PCIe device
 * @where: PCI Config offset of TLP Header Log
 * @where2: PCI Config offset of TLP Prefix Log
 * @tlp_len: TLP Log length (Header Log + TLP Prefix Log in DWORDs)
 * @flit: TLP Logged in Flit mode
 * @log: TLP Log structure to fill
 *
 * Fill @log from TLP Header Log registers, e.g., AER or DPC.
 *
 * Return: 0 on success and filled TLP Log structure, <0 on error.
 */
int pcie_read_tlp_log(struct pci_dev *dev, int where, int where2,
		      unsigned int tlp_len, bool flit, struct pcie_tlp_log *log)
{
	unsigned int i;
	int off, ret;

	if (tlp_len > ARRAY_SIZE(log->dw))
		tlp_len = ARRAY_SIZE(log->dw);

	memset(log, 0, sizeof(*log));

	for (i = 0; i < tlp_len; i++) {
		if (i < PCIE_STD_NUM_TLP_HEADERLOG)
			off = where + i * 4;
		else
			off = where2 + (i - PCIE_STD_NUM_TLP_HEADERLOG) * 4;

		ret = pci_read_config_dword(dev, off, &log->dw[i]);
		if (ret)
			return pcibios_err_to_errno(ret);
	}

	/*
	 * Hard-code non-Flit mode to 4 DWORDs, for now. The exact length
	 * can only be known if the TLP is parsed.
	 */
	log->header_len = flit ? tlp_len : 4;
	log->flit = flit;

	return 0;
}

/* The Prefix Log registers hold dw[4..13] in Flit mode. */
static_assert((PCI_ERR_PREFIX_LOG +
	       (PCIE_STD_MAX_TLP_HEADERLOG - PCIE_STD_NUM_TLP_HEADERLOG) *
	       sizeof(u32)) == PCIE_AER_CAP_HW_SIZE);

/**
 * aer_cap_regs_unpack - Convert a raw AER Capability image to the kernel layout
 * @regs: Destination, fully initialised
 * @raw: AER Capability register block in hardware order, little-endian
 * @raw_len: Bytes readable at @raw
 *
 * struct aer_capability_regs is not the hardware layout: struct pcie_tlp_log
 * spans 60 bytes where the Header Log it stands in for is 16, so a flat copy
 * misplaces everything behind it. Map the registers one by one instead,
 * taking the TLP Log layout from the Flit bit as pcie_read_tlp_log() does.
 *
 * @raw is not necessarily aligned. Registers past @raw_len are left zero, so
 * a short image cannot be read past.
 */
void aer_cap_regs_unpack(struct aer_capability_regs *regs, const void *raw,
			 size_t raw_len)
{
	unsigned int i, tlp_len;
	bool flit;

	memset(regs, 0, sizeof(*regs));

	if (raw_len < PCI_ERR_HEADER_LOG)
		return;

	/* The Extended Capability Header, at offset 0, has no define */
	regs->header = get_unaligned_le32(raw);
	regs->uncor_status = get_unaligned_le32(raw + PCI_ERR_UNCOR_STATUS);
	regs->uncor_mask = get_unaligned_le32(raw + PCI_ERR_UNCOR_MASK);
	regs->uncor_severity = get_unaligned_le32(raw + PCI_ERR_UNCOR_SEVER);
	regs->cor_status = get_unaligned_le32(raw + PCI_ERR_COR_STATUS);
	regs->cor_mask = get_unaligned_le32(raw + PCI_ERR_COR_MASK);
	regs->cap_control = get_unaligned_le32(raw + PCI_ERR_CAP);

	flit = FIELD_GET(PCI_ERR_CAP_TLP_LOG_FLIT, regs->cap_control);
	if (flit) {
		tlp_len = FIELD_GET(PCI_ERR_CAP_TLP_LOG_SIZE, regs->cap_control);
	} else {
		/*
		 * dw[4..7] alias the Prefix Log. Take all four whatever
		 * eetlp_prefix_max says; pcie_print_tlp_log() stops at the
		 * first zero one.
		 */
		tlp_len = PCIE_STD_NUM_TLP_HEADERLOG + PCIE_STD_MAX_TLP_PREFIXLOG;
	}

	tlp_len = min(tlp_len, ARRAY_SIZE(regs->header_log.dw));

	for (i = 0; i < tlp_len; i++) {
		unsigned int off;

		if (i < PCIE_STD_NUM_TLP_HEADERLOG)
			off = PCI_ERR_HEADER_LOG + i * sizeof(u32);
		else
			off = PCI_ERR_PREFIX_LOG +
			      (i - PCIE_STD_NUM_TLP_HEADERLOG) * sizeof(u32);

		if (off + sizeof(u32) > raw_len)
			break;
		regs->header_log.dw[i] = get_unaligned_le32(raw + off);
	}

	/* @i may be short of tlp_len; non-Flit needs the TLP parsed, so cap at 4 */
	regs->header_log.header_len = flit ? i : min(i, PCIE_STD_NUM_TLP_HEADERLOG);
	regs->header_log.flit = flit;

	if (raw_len >= PCI_ERR_ROOT_COMMAND + sizeof(u32))
		regs->root_command = get_unaligned_le32(raw + PCI_ERR_ROOT_COMMAND);
	if (raw_len >= PCI_ERR_ROOT_STATUS + sizeof(u32))
		regs->root_status = get_unaligned_le32(raw + PCI_ERR_ROOT_STATUS);
	if (raw_len >= PCI_ERR_ROOT_ERR_SRC + sizeof(u32)) {
		u32 src = get_unaligned_le32(raw + PCI_ERR_ROOT_ERR_SRC);

		/* One register: ERR_COR in 15:0, ERR_FATAL/NONFATAL in 31:16 */
		regs->cor_err_source = src;
		regs->uncor_err_source = src >> 16;
	}
}
EXPORT_SYMBOL_GPL(aer_cap_regs_unpack);

#define EE_PREFIX_STR " E-E Prefixes:"

/**
 * pcie_print_tlp_log - Print TLP Header / Prefix Log contents
 * @dev: PCIe device
 * @log: TLP Log structure
 * @level: Printk log level
 * @pfx: String prefix
 *
 * Prints TLP Header and Prefix Log information held by @log.
 */
void pcie_print_tlp_log(const struct pci_dev *dev,
			const struct pcie_tlp_log *log, const char *level,
			const char *pfx)
{
	/* EE_PREFIX_STR fits the extended DW space needed for the Flit mode */
	char buf[11 * PCIE_STD_MAX_TLP_HEADERLOG + 1];
	unsigned int i;
	int len;

	len = scnprintf(buf, sizeof(buf), "%#010x %#010x %#010x %#010x",
			log->dw[0], log->dw[1], log->dw[2], log->dw[3]);

	if (log->flit) {
		for (i = PCIE_STD_NUM_TLP_HEADERLOG; i < log->header_len; i++) {
			len += scnprintf(buf + len, sizeof(buf) - len,
					 " %#010x", log->dw[i]);
		}
	} else {
		if (log->prefix[0])
			len += scnprintf(buf + len, sizeof(buf) - len,
					 EE_PREFIX_STR);
		for (i = 0; i < ARRAY_SIZE(log->prefix); i++) {
			if (!log->prefix[i])
				break;
			len += scnprintf(buf + len, sizeof(buf) - len,
					 " %#010x", log->prefix[i]);
		}
	}

	dev_printk(level, &dev->dev, "%sTLP Header%s: %s\n", pfx,
		log->flit ? " (Flit)" : "", buf);
}
