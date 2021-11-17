/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PCI_BRIDGE_EMUL_H__
#define __PCI_BRIDGE_EMUL_H__

#include <linux/kernel.h>

/* PCI configuration space of a PCI-to-PCI bridge. */
struct pci_bridge_emul_conf {
	__le16 vendor;
	__le16 device;
	__le16 command;
	__le16 status;
	__le32 class_revision;
	u8 cache_line_size;
	u8 latency_timer;
	u8 header_type;
	u8 bist;
	__le32 bar[2];
	u8 primary_bus;
	u8 secondary_bus;
	u8 subordinate_bus;
	u8 secondary_latency_timer;
	u8 iobase;
	u8 iolimit;
	__le16 secondary_status;
	__le16 membase;
	__le16 memlimit;
	__le16 pref_mem_base;
	__le16 pref_mem_limit;
	__le32 prefbaseupper;
	__le32 preflimitupper;
	__le16 iobaseupper;
	__le16 iolimitupper;
	u8 capabilities_pointer;
	u8 reserve[3];
	__le32 romaddr;
	u8 intline;
	u8 intpin;
	__le16 bridgectrl;
};

/* PCI configuration space of the PCIe capabilities */
struct pci_bridge_emul_pcie_conf {
	u8 cap_id;
	u8 next;
	__le16 cap;
	__le32 devcap;
	__le16 devctl;
	__le16 devsta;
	__le32 lnkcap;
	__le16 lnkctl;
	__le16 lnksta;
	__le32 slotcap;
	__le16 slotctl;
	__le16 slotsta;
	__le16 rootctl;
	__le16 rootcap;
	__le32 rootsta;
	__le32 devcap2;
	__le16 devctl2;
	__le16 devsta2;
	__le32 lnkcap2;
	__le16 lnkctl2;
	__le16 lnksta2;
	__le32 slotcap2;
	__le16 slotctl2;
	__le16 slotsta2;
};

struct pci_bridge_emul;

typedef enum { PCI_BRIDGE_EMUL_HANDLED,
	       PCI_BRIDGE_EMUL_NOT_HANDLED } pci_bridge_emul_read_status_t;

struct pci_bridge_emul_ops {
	/*
	 * Called when reading from the regular PCI bridge
	 * configuration space. Return PCI_BRIDGE_EMUL_HANDLED when the
	 * operation has handled the read operation and filled in the
	 * *value, or PCI_BRIDGE_EMUL_NOT_HANDLED when the read should
	 * be emulated by the common code by reading from the
	 * in-memory copy of the configuration space.
	 */
	pci_bridge_emul_read_status_t (*read_base)(struct pci_bridge_emul *bridge,
						   int reg, u32 *value);

	/*
	 * Same as ->read_base(), except it is for reading from the
	 * PCIe capability configuration space.
	 */
	pci_bridge_emul_read_status_t (*read_pcie)(struct pci_bridge_emul *bridge,
						   int reg, u32 *value);
	/*
	 * Called when writing to the regular PCI bridge configuration
	 * space. old is the current value, new is the new value being
	 * written, and mask indicates which parts of the value are
	 * being changed.
	 */
	void (*write_base)(struct pci_bridge_emul *bridge, int reg,
			   u32 old, u32 new, u32 mask);

	/*
	 * Same as ->write_base(), except it is for writing from the
	 * PCIe capability configuration space.
	 */
	void (*write_pcie)(struct pci_bridge_emul *bridge, int reg,
			   u32 old, u32 new, u32 mask);
};

struct pci_bridge_reg_behavior;

struct pci_bridge_emul {
	struct pci_bridge_emul_conf conf;
	struct pci_bridge_emul_pcie_conf pcie_conf;
	struct pci_bridge_emul_ops *ops;
	struct pci_bridge_reg_behavior *pci_regs_behavior;
	struct pci_bridge_reg_behavior *pcie_cap_regs_behavior;
	void *data;
	bool has_pcie;
};

enum {
	PCI_BRIDGE_EMUL_NO_PREFETCHABLE_BAR = BIT(0),
};

int pci_bridge_emul_init(struct pci_bridge_emul *bridge,
			 unsigned int flags);
void pci_bridge_emul_cleanup(struct pci_bridge_emul *bridge);

int pci_bridge_emul_conf_read(struct pci_bridge_emul *bridge, int where,
			      int size, u32 *value);
int pci_bridge_emul_conf_write(struct pci_bridge_emul *bridge, int where,
			       int size, u32 value);

#endif /* __PCI_BRIDGE_EMUL_H__ */
