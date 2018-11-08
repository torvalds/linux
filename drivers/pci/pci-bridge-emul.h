/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PCI_BRIDGE_EMUL_H__
#define __PCI_BRIDGE_EMUL_H__

#include <linux/kernel.h>

/* PCI configuration space of a PCI-to-PCI bridge. */
struct pci_bridge_emul_conf {
	u16 vendor;
	u16 device;
	u16 command;
	u16 status;
	u32 class_revision;
	u8 cache_line_size;
	u8 latency_timer;
	u8 header_type;
	u8 bist;
	u32 bar[2];
	u8 primary_bus;
	u8 secondary_bus;
	u8 subordinate_bus;
	u8 secondary_latency_timer;
	u8 iobase;
	u8 iolimit;
	u16 secondary_status;
	u16 membase;
	u16 memlimit;
	u16 pref_mem_base;
	u16 pref_mem_limit;
	u32 prefbaseupper;
	u32 preflimitupper;
	u16 iobaseupper;
	u16 iolimitupper;
	u8 capabilities_pointer;
	u8 reserve[3];
	u32 romaddr;
	u8 intline;
	u8 intpin;
	u16 bridgectrl;
};

/* PCI configuration space of the PCIe capabilities */
struct pci_bridge_emul_pcie_conf {
	u8 cap_id;
	u8 next;
	u16 cap;
	u32 devcap;
	u16 devctl;
	u16 devsta;
	u32 lnkcap;
	u16 lnkctl;
	u16 lnksta;
	u32 slotcap;
	u16 slotctl;
	u16 slotsta;
	u16 rootctl;
	u16 rsvd;
	u32 rootsta;
	u32 devcap2;
	u16 devctl2;
	u16 devsta2;
	u32 lnkcap2;
	u16 lnkctl2;
	u16 lnksta2;
	u32 slotcap2;
	u16 slotctl2;
	u16 slotsta2;
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

struct pci_bridge_emul {
	struct pci_bridge_emul_conf conf;
	struct pci_bridge_emul_pcie_conf pcie_conf;
	struct pci_bridge_emul_ops *ops;
	void *data;
	bool has_pcie;
};

void pci_bridge_emul_init(struct pci_bridge_emul *bridge);
int pci_bridge_emul_conf_read(struct pci_bridge_emul *bridge, int where,
			      int size, u32 *value);
int pci_bridge_emul_conf_write(struct pci_bridge_emul *bridge, int where,
			       int size, u32 value);

#endif /* __PCI_BRIDGE_EMUL_H__ */
