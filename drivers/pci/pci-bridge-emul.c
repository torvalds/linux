// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell
 *
 * Author: Thomas Petazzoni <thomas.petazzoni@bootlin.com>
 *
 * This file helps PCI controller drivers implement a fake root port
 * PCI bridge when the HW doesn't provide such a root port PCI
 * bridge.
 *
 * It emulates a PCI bridge by providing a fake PCI configuration
 * space (and optionally a PCIe capability configuration space) in
 * memory. By default the read/write operations simply read and update
 * this fake configuration space in memory. However, PCI controller
 * drivers can provide through the 'struct pci_sw_bridge_ops'
 * structure a set of operations to override or complement this
 * default behavior.
 */

#include <linux/pci.h>
#include "pci-bridge-emul.h"

#define PCI_BRIDGE_CONF_END	PCI_STD_HEADER_SIZEOF
#define PCI_CAP_PCIE_START	PCI_BRIDGE_CONF_END
#define PCI_CAP_PCIE_END	(PCI_CAP_PCIE_START + PCI_EXP_SLTSTA2 + 2)

struct pci_bridge_reg_behavior {
	/* Read-only bits */
	u32 ro;

	/* Read-write bits */
	u32 rw;

	/* Write-1-to-clear bits */
	u32 w1c;

	/* Reserved bits (hardwired to 0) */
	u32 rsvd;
};

const static struct pci_bridge_reg_behavior pci_regs_behavior[] = {
	[PCI_VENDOR_ID / 4] = { .ro = ~0 },
	[PCI_COMMAND / 4] = {
		.rw = (PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		       PCI_COMMAND_MASTER | PCI_COMMAND_PARITY |
		       PCI_COMMAND_SERR),
		.ro = ((PCI_COMMAND_SPECIAL | PCI_COMMAND_INVALIDATE |
			PCI_COMMAND_VGA_PALETTE | PCI_COMMAND_WAIT |
			PCI_COMMAND_FAST_BACK) |
		       (PCI_STATUS_CAP_LIST | PCI_STATUS_66MHZ |
			PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MASK) << 16),
		.rsvd = GENMASK(15, 10) | ((BIT(6) | GENMASK(3, 0)) << 16),
		.w1c = (PCI_STATUS_PARITY |
			PCI_STATUS_SIG_TARGET_ABORT |
			PCI_STATUS_REC_TARGET_ABORT |
			PCI_STATUS_REC_MASTER_ABORT |
			PCI_STATUS_SIG_SYSTEM_ERROR |
			PCI_STATUS_DETECTED_PARITY) << 16,
	},
	[PCI_CLASS_REVISION / 4] = { .ro = ~0 },

	/*
	 * Cache Line Size register: implement as read-only, we do not
	 * pretend implementing "Memory Write and Invalidate"
	 * transactions"
	 *
	 * Latency Timer Register: implemented as read-only, as "A
	 * bridge that is not capable of a burst transfer of more than
	 * two data phases on its primary interface is permitted to
	 * hardwire the Latency Timer to a value of 16 or less"
	 *
	 * Header Type: always read-only
	 *
	 * BIST register: implemented as read-only, as "A bridge that
	 * does not support BIST must implement this register as a
	 * read-only register that returns 0 when read"
	 */
	[PCI_CACHE_LINE_SIZE / 4] = { .ro = ~0 },

	/*
	 * Base Address registers not used must be implemented as
	 * read-only registers that return 0 when read.
	 */
	[PCI_BASE_ADDRESS_0 / 4] = { .ro = ~0 },
	[PCI_BASE_ADDRESS_1 / 4] = { .ro = ~0 },

	[PCI_PRIMARY_BUS / 4] = {
		/* Primary, secondary and subordinate bus are RW */
		.rw = GENMASK(24, 0),
		/* Secondary latency is read-only */
		.ro = GENMASK(31, 24),
	},

	[PCI_IO_BASE / 4] = {
		/* The high four bits of I/O base/limit are RW */
		.rw = (GENMASK(15, 12) | GENMASK(7, 4)),

		/* The low four bits of I/O base/limit are RO */
		.ro = (((PCI_STATUS_66MHZ | PCI_STATUS_FAST_BACK |
			 PCI_STATUS_DEVSEL_MASK) << 16) |
		       GENMASK(11, 8) | GENMASK(3, 0)),

		.w1c = (PCI_STATUS_PARITY |
			PCI_STATUS_SIG_TARGET_ABORT |
			PCI_STATUS_REC_TARGET_ABORT |
			PCI_STATUS_REC_MASTER_ABORT |
			PCI_STATUS_SIG_SYSTEM_ERROR |
			PCI_STATUS_DETECTED_PARITY) << 16,

		.rsvd = ((BIT(6) | GENMASK(4, 0)) << 16),
	},

	[PCI_MEMORY_BASE / 4] = {
		/* The high 12-bits of mem base/limit are RW */
		.rw = GENMASK(31, 20) | GENMASK(15, 4),

		/* The low four bits of mem base/limit are RO */
		.ro = GENMASK(19, 16) | GENMASK(3, 0),
	},

	[PCI_PREF_MEMORY_BASE / 4] = {
		/* The high 12-bits of pref mem base/limit are RW */
		.rw = GENMASK(31, 20) | GENMASK(15, 4),

		/* The low four bits of pref mem base/limit are RO */
		.ro = GENMASK(19, 16) | GENMASK(3, 0),
	},

	[PCI_PREF_BASE_UPPER32 / 4] = {
		.rw = ~0,
	},

	[PCI_PREF_LIMIT_UPPER32 / 4] = {
		.rw = ~0,
	},

	[PCI_IO_BASE_UPPER16 / 4] = {
		.rw = ~0,
	},

	[PCI_CAPABILITY_LIST / 4] = {
		.ro = GENMASK(7, 0),
		.rsvd = GENMASK(31, 8),
	},

	[PCI_ROM_ADDRESS1 / 4] = {
		.rw = GENMASK(31, 11) | BIT(0),
		.rsvd = GENMASK(10, 1),
	},

	/*
	 * Interrupt line (bits 7:0) are RW, interrupt pin (bits 15:8)
	 * are RO, and bridge control (31:16) are a mix of RW, RO,
	 * reserved and W1C bits
	 */
	[PCI_INTERRUPT_LINE / 4] = {
		/* Interrupt line is RW */
		.rw = (GENMASK(7, 0) |
		       ((PCI_BRIDGE_CTL_PARITY |
			 PCI_BRIDGE_CTL_SERR |
			 PCI_BRIDGE_CTL_ISA |
			 PCI_BRIDGE_CTL_VGA |
			 PCI_BRIDGE_CTL_MASTER_ABORT |
			 PCI_BRIDGE_CTL_BUS_RESET |
			 BIT(8) | BIT(9) | BIT(11)) << 16)),

		/* Interrupt pin is RO */
		.ro = (GENMASK(15, 8) | ((PCI_BRIDGE_CTL_FAST_BACK) << 16)),

		.w1c = BIT(10) << 16,

		.rsvd = (GENMASK(15, 12) | BIT(4)) << 16,
	},
};

const static struct pci_bridge_reg_behavior pcie_cap_regs_behavior[] = {
	[PCI_CAP_LIST_ID / 4] = {
		/*
		 * Capability ID, Next Capability Pointer and
		 * Capabilities register are all read-only.
		 */
		.ro = ~0,
	},

	[PCI_EXP_DEVCAP / 4] = {
		.ro = ~0,
	},

	[PCI_EXP_DEVCTL / 4] = {
		/* Device control register is RW */
		.rw = GENMASK(15, 0),

		/*
		 * Device status register has 4 bits W1C, then 2 bits
		 * RO, the rest is reserved
		 */
		.w1c = GENMASK(19, 16),
		.ro = GENMASK(20, 19),
		.rsvd = GENMASK(31, 21),
	},

	[PCI_EXP_LNKCAP / 4] = {
		/* All bits are RO, except bit 23 which is reserved */
		.ro = lower_32_bits(~BIT(23)),
		.rsvd = BIT(23),
	},

	[PCI_EXP_LNKCTL / 4] = {
		/*
		 * Link control has bits [1:0] and [11:3] RW, the
		 * other bits are reserved.
		 * Link status has bits [13:0] RO, and bits [14:15]
		 * W1C.
		 */
		.rw = GENMASK(11, 3) | GENMASK(1, 0),
		.ro = GENMASK(13, 0) << 16,
		.w1c = GENMASK(15, 14) << 16,
		.rsvd = GENMASK(15, 12) | BIT(2),
	},

	[PCI_EXP_SLTCAP / 4] = {
		.ro = ~0,
	},

	[PCI_EXP_SLTCTL / 4] = {
		/*
		 * Slot control has bits [12:0] RW, the rest is
		 * reserved.
		 *
		 * Slot status has a mix of W1C and RO bits, as well
		 * as reserved bits.
		 */
		.rw = GENMASK(12, 0),
		.w1c = (PCI_EXP_SLTSTA_ABP | PCI_EXP_SLTSTA_PFD |
			PCI_EXP_SLTSTA_MRLSC | PCI_EXP_SLTSTA_PDC |
			PCI_EXP_SLTSTA_CC | PCI_EXP_SLTSTA_DLLSC) << 16,
		.ro = (PCI_EXP_SLTSTA_MRLSS | PCI_EXP_SLTSTA_PDS |
		       PCI_EXP_SLTSTA_EIS) << 16,
		.rsvd = GENMASK(15, 12) | (GENMASK(15, 9) << 16),
	},

	[PCI_EXP_RTCTL / 4] = {
		/*
		 * Root control has bits [4:0] RW, the rest is
		 * reserved.
		 *
		 * Root status has bit 0 RO, the rest is reserved.
		 */
		.rw = (PCI_EXP_RTCTL_SECEE | PCI_EXP_RTCTL_SENFEE |
		       PCI_EXP_RTCTL_SEFEE | PCI_EXP_RTCTL_PMEIE |
		       PCI_EXP_RTCTL_CRSSVE),
		.ro = PCI_EXP_RTCAP_CRSVIS << 16,
		.rsvd = GENMASK(15, 5) | (GENMASK(15, 1) << 16),
	},

	[PCI_EXP_RTSTA / 4] = {
		.ro = GENMASK(15, 0) | PCI_EXP_RTSTA_PENDING,
		.w1c = PCI_EXP_RTSTA_PME,
		.rsvd = GENMASK(31, 18),
	},
};

/*
 * Initialize a pci_bridge_emul structure to represent a fake PCI
 * bridge configuration space. The caller needs to have initialized
 * the PCI configuration space with whatever values make sense
 * (typically at least vendor, device, revision), the ->ops pointer,
 * and optionally ->data and ->has_pcie.
 */
int pci_bridge_emul_init(struct pci_bridge_emul *bridge,
			 unsigned int flags)
{
	bridge->conf.class_revision |= PCI_CLASS_BRIDGE_PCI << 16;
	bridge->conf.header_type = PCI_HEADER_TYPE_BRIDGE;
	bridge->conf.cache_line_size = 0x10;
	bridge->conf.status = PCI_STATUS_CAP_LIST;
	bridge->pci_regs_behavior = kmemdup(pci_regs_behavior,
					    sizeof(pci_regs_behavior),
					    GFP_KERNEL);
	if (!bridge->pci_regs_behavior)
		return -ENOMEM;

	if (bridge->has_pcie) {
		bridge->conf.capabilities_pointer = PCI_CAP_PCIE_START;
		bridge->pcie_conf.cap_id = PCI_CAP_ID_EXP;
		/* Set PCIe v2, root port, slot support */
		bridge->pcie_conf.cap = PCI_EXP_TYPE_ROOT_PORT << 4 | 2 |
			PCI_EXP_FLAGS_SLOT;
		bridge->pcie_cap_regs_behavior =
			kmemdup(pcie_cap_regs_behavior,
				sizeof(pcie_cap_regs_behavior),
				GFP_KERNEL);
		if (!bridge->pcie_cap_regs_behavior) {
			kfree(bridge->pci_regs_behavior);
			return -ENOMEM;
		}
	}

	if (flags & PCI_BRIDGE_EMUL_NO_PREFETCHABLE_BAR) {
		bridge->pci_regs_behavior[PCI_PREF_MEMORY_BASE / 4].ro = ~0;
		bridge->pci_regs_behavior[PCI_PREF_MEMORY_BASE / 4].rw = 0;
	}

	return 0;
}

/*
 * Cleanup a pci_bridge_emul structure that was previously initilized
 * using pci_bridge_emul_init().
 */
void pci_bridge_emul_cleanup(struct pci_bridge_emul *bridge)
{
	if (bridge->has_pcie)
		kfree(bridge->pcie_cap_regs_behavior);
	kfree(bridge->pci_regs_behavior);
}

/*
 * Should be called by the PCI controller driver when reading the PCI
 * configuration space of the fake bridge. It will call back the
 * ->ops->read_base or ->ops->read_pcie operations.
 */
int pci_bridge_emul_conf_read(struct pci_bridge_emul *bridge, int where,
			      int size, u32 *value)
{
	int ret;
	int reg = where & ~3;
	pci_bridge_emul_read_status_t (*read_op)(struct pci_bridge_emul *bridge,
						 int reg, u32 *value);
	u32 *cfgspace;
	const struct pci_bridge_reg_behavior *behavior;

	if (bridge->has_pcie && reg >= PCI_CAP_PCIE_END) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	if (!bridge->has_pcie && reg >= PCI_BRIDGE_CONF_END) {
		*value = 0;
		return PCIBIOS_SUCCESSFUL;
	}

	if (bridge->has_pcie && reg >= PCI_CAP_PCIE_START) {
		reg -= PCI_CAP_PCIE_START;
		read_op = bridge->ops->read_pcie;
		cfgspace = (u32 *) &bridge->pcie_conf;
		behavior = bridge->pcie_cap_regs_behavior;
	} else {
		read_op = bridge->ops->read_base;
		cfgspace = (u32 *) &bridge->conf;
		behavior = bridge->pci_regs_behavior;
	}

	if (read_op)
		ret = read_op(bridge, reg, value);
	else
		ret = PCI_BRIDGE_EMUL_NOT_HANDLED;

	if (ret == PCI_BRIDGE_EMUL_NOT_HANDLED)
		*value = cfgspace[reg / 4];

	/*
	 * Make sure we never return any reserved bit with a value
	 * different from 0.
	 */
	*value &= ~behavior[reg / 4].rsvd;

	if (size == 1)
		*value = (*value >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*value = (*value >> (8 * (where & 3))) & 0xffff;
	else if (size != 4)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

/*
 * Should be called by the PCI controller driver when writing the PCI
 * configuration space of the fake bridge. It will call back the
 * ->ops->write_base or ->ops->write_pcie operations.
 */
int pci_bridge_emul_conf_write(struct pci_bridge_emul *bridge, int where,
			       int size, u32 value)
{
	int reg = where & ~3;
	int mask, ret, old, new, shift;
	void (*write_op)(struct pci_bridge_emul *bridge, int reg,
			 u32 old, u32 new, u32 mask);
	u32 *cfgspace;
	const struct pci_bridge_reg_behavior *behavior;

	if (bridge->has_pcie && reg >= PCI_CAP_PCIE_END)
		return PCIBIOS_SUCCESSFUL;

	if (!bridge->has_pcie && reg >= PCI_BRIDGE_CONF_END)
		return PCIBIOS_SUCCESSFUL;

	shift = (where & 0x3) * 8;

	if (size == 4)
		mask = 0xffffffff;
	else if (size == 2)
		mask = 0xffff << shift;
	else if (size == 1)
		mask = 0xff << shift;
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ret = pci_bridge_emul_conf_read(bridge, reg, 4, &old);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (bridge->has_pcie && reg >= PCI_CAP_PCIE_START) {
		reg -= PCI_CAP_PCIE_START;
		write_op = bridge->ops->write_pcie;
		cfgspace = (u32 *) &bridge->pcie_conf;
		behavior = bridge->pcie_cap_regs_behavior;
	} else {
		write_op = bridge->ops->write_base;
		cfgspace = (u32 *) &bridge->conf;
		behavior = bridge->pci_regs_behavior;
	}

	/* Keep all bits, except the RW bits */
	new = old & (~mask | ~behavior[reg / 4].rw);

	/* Update the value of the RW bits */
	new |= (value << shift) & (behavior[reg / 4].rw & mask);

	/* Clear the W1C bits */
	new &= ~((value << shift) & (behavior[reg / 4].w1c & mask));

	cfgspace[reg / 4] = new;

	if (write_op)
		write_op(bridge, reg, old, new, mask);

	return PCIBIOS_SUCCESSFUL;
}
