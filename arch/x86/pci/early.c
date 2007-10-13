#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <asm/io.h>
#include "pci.h"

/* Direct PCI access. This is used for PCI accesses in early boot before
   the PCI subsystem works. */

#define PDprintk(x...)

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
	u32 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inl(0xcfc);
	if (v != 0xffffffff)
		PDprintk("%x reading 4 from %x: %x\n", slot, offset, v);
	return v;
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
	u8 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inb(0xcfc + (offset&3));
	PDprintk("%x reading 1 from %x: %x\n", slot, offset, v);
	return v;
}

u16 read_pci_config_16(u8 bus, u8 slot, u8 func, u8 offset)
{
	u16 v;
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	v = inw(0xcfc + (offset&2));
	PDprintk("%x reading 2 from %x: %x\n", slot, offset, v);
	return v;
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset,
				    u32 val)
{
	PDprintk("%x writing to %x: %x\n", slot, offset, val);
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outl(val, 0xcfc);
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
	PDprintk("%x writing to %x: %x\n", slot, offset, val);
	outl(0x80000000 | (bus<<16) | (slot<<11) | (func<<8) | offset, 0xcf8);
	outb(val, 0xcfc);
}

int early_pci_allowed(void)
{
	return (pci_probe & (PCI_PROBE_CONF1|PCI_PROBE_NOEARLY)) ==
			PCI_PROBE_CONF1;
}
