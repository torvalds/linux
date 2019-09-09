// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>

#include <asm/mips-boards/bonito64.h>

#include <loongson.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#define HT1LO_PCICFG_BASE      0x1a000000
#define HT1LO_PCICFG_BASE_TP1  0x1b000000

static int loongson3_pci_config_access(unsigned char access_type,
		struct pci_bus *bus, unsigned int devfn,
		int where, u32 *data)
{
	unsigned char busnum = bus->number;
	int function = PCI_FUNC(devfn);
	int device = PCI_SLOT(devfn);
	int reg = where & ~3;
	void *addrp;
	u64 addr;

	if (where < PCI_CFG_SPACE_SIZE) { /* standard config */
		addr = (busnum << 16) | (device << 11) | (function << 8) | reg;
		if (busnum == 0) {
			if (device > 31)
				return PCIBIOS_DEVICE_NOT_FOUND;
			addrp = (void *)TO_UNCAC(HT1LO_PCICFG_BASE | addr);
		} else {
			addrp = (void *)TO_UNCAC(HT1LO_PCICFG_BASE_TP1 | addr);
		}
	} else if (where < PCI_CFG_SPACE_EXP_SIZE) {  /* extended config */
		struct pci_dev *rootdev;

		rootdev = pci_get_domain_bus_and_slot(0, 0, 0);
		if (!rootdev)
			return PCIBIOS_DEVICE_NOT_FOUND;

		addr = pci_resource_start(rootdev, 3);
		if (!addr)
			return PCIBIOS_DEVICE_NOT_FOUND;

		addr |= busnum << 20 | device << 15 | function << 12 | reg;
		addrp = (void *)TO_UNCAC(addr);
	} else {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (access_type == PCI_ACCESS_WRITE)
		writel(*data, addrp);
	else {
		*data = readl(addrp);
		if (*data == 0xffffffff) {
			*data = -1;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}
	return PCIBIOS_SUCCESSFUL;
}

static int loongson3_pci_pcibios_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	u32 data = 0;
	int ret = loongson3_pci_config_access(PCI_ACCESS_READ,
			bus, devfn, where, &data);

	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int loongson3_pci_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	u32 data = 0;
	int ret;

	if (size == 4)
		data = val;
	else {
		ret = loongson3_pci_config_access(PCI_ACCESS_READ,
				bus, devfn, where, &data);
		if (ret != PCIBIOS_SUCCESSFUL)
			return ret;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
	}

	ret = loongson3_pci_config_access(PCI_ACCESS_WRITE,
			bus, devfn, where, &data);

	return ret;
}

struct pci_ops loongson_pci_ops = {
	.read = loongson3_pci_pcibios_read,
	.write = loongson3_pci_pcibios_write
};
