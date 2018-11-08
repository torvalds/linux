// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <asm/io.h>
#include <asm/pci_x86.h>

/* Direct PCI access. This is used for PCI accesses in early boot before
   the PCI subsystem works. */

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
	u32 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inl(0xcfc);
	return v;
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
	u8 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inb(0xcfc + (offset&3));
	return v;
}

u16 read_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset)
{
	u16 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inw(0xcfc + (offset&2));
	return v;
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset,
				    u32 val)
{
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outl(val, 0xcfc);
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outb(val, 0xcfc + (offset&3));
}

void write_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outw(val, 0xcfc + (offset&2));
}

u32 pci_early_find_cap(int bus, int slot, int func, int cap)
{
	int bytes;
	u8 pos;

	if (!(read_pci_config_16(bus, slot, func, PCI_STATUS) &
						PCI_STATUS_CAP_LIST))
		return 0;

	pos = read_pci_config_byte(bus, slot, func, PCI_CAPABILITY_LIST);
	for (bytes = 0; bytes < 48 && pos >= 0x40; bytes++) {
		u8 id;

		pos &= ~3;
		id = read_pci_config_byte(bus, slot, func, pos+PCI_CAP_LIST_ID);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = read_pci_config_byte(bus, slot, func,
						pos+PCI_CAP_LIST_NEXT);
	}
	return 0;
}

int early_pci_allowed(void)
{
	return (pci_probe & (PCI_PROBE_CONF1|PCI_PROBE_NOEARLY)) ==
			PCI_PROBE_CONF1;
}

