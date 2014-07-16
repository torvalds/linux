/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <uapi/asm/bitfield.h>
#include <asm/byteorder.h>
#include <asm/io.h>

#define PCI_CONFIG_ADDRESS	0xcf8
#define PCI_CONFIG_DATA		0xcfc

union pci_config_address {
	struct {
		__BITFIELD_FIELD(unsigned enable_bit	  : 1,	/* 31       */
		__BITFIELD_FIELD(unsigned reserved	  : 7,	/* 30 .. 24 */
		__BITFIELD_FIELD(unsigned bus_number	  : 8,	/* 23 .. 16 */
		__BITFIELD_FIELD(unsigned devfn_number	  : 8,	/* 15 .. 8  */
		__BITFIELD_FIELD(unsigned register_number : 8,	/* 7  .. 0  */
		)))));
	};
	u32 w;
};

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return ((pin + slot) % 4)+ MIPS_IRQ_PCIA;
}

static void pci_virtio_guest_write_config_addr(struct pci_bus *bus,
					unsigned int devfn, int reg)
{
	union pci_config_address pca = { .w = 0 };

	pca.register_number = reg;
	pca.devfn_number = devfn;
	pca.bus_number = bus->number;
	pca.enable_bit = 1;

	outl(pca.w, PCI_CONFIG_ADDRESS);
}

static int pci_virtio_guest_write_config(struct pci_bus *bus,
		unsigned int devfn, int reg, int size, u32 val)
{
	pci_virtio_guest_write_config_addr(bus, devfn, reg);

	switch (size) {
	case 1:
		outb(val, PCI_CONFIG_DATA + (reg & 3));
		break;
	case 2:
		outw(val, PCI_CONFIG_DATA + (reg & 2));
		break;
	case 4:
		outl(val, PCI_CONFIG_DATA);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int pci_virtio_guest_read_config(struct pci_bus *bus, unsigned int devfn,
					int reg, int size, u32 *val)
{
	pci_virtio_guest_write_config_addr(bus, devfn, reg);

	switch (size) {
	case 1:
		*val = inb(PCI_CONFIG_DATA + (reg & 3));
		break;
	case 2:
		*val = inw(PCI_CONFIG_DATA + (reg & 2));
		break;
	case 4:
		*val = inl(PCI_CONFIG_DATA);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_virtio_guest_ops = {
	.read  = pci_virtio_guest_read_config,
	.write = pci_virtio_guest_write_config,
};

static struct resource pci_virtio_guest_mem_resource = {
	.name = "Virtio MEM",
	.flags = IORESOURCE_MEM,
	.start	= 0x10000000,
	.end	= 0x1dffffff
};

static struct resource pci_virtio_guest_io_resource = {
	.name = "Virtio IO",
	.flags = IORESOURCE_IO,
	.start	= 0,
	.end	= 0xffff
};

static struct pci_controller pci_virtio_guest_controller = {
	.pci_ops = &pci_virtio_guest_ops,
	.mem_resource = &pci_virtio_guest_mem_resource,
	.io_resource = &pci_virtio_guest_io_resource,
};

static int __init pci_virtio_guest_setup(void)
{
	pr_err("pci_virtio_guest_setup\n");

	/* Virtio comes pre-assigned */
	pci_set_flags(PCI_PROBE_ONLY);

	pci_virtio_guest_controller.io_map_base = mips_io_port_base;
	register_pci_controller(&pci_virtio_guest_controller);
	return 0;
}
arch_initcall(pci_virtio_guest_setup);
