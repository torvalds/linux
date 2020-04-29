// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001,2002,2005 Broadcom Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 */

/*
 * BCM1480/1455-specific HT support (looking like PCI)
 *
 * This module provides the glue between Linux's PCI subsystem
 * and the hardware.  We basically provide glue for accessing
 * configuration space, and set up the translation for I/O
 * space accesses.
 *
 * To access configuration space, we use ioremap.  In the 32-bit
 * kernel, this consumes either 4 or 8 page table pages, and 16MB of
 * kernel mapped memory.  Hopefully neither of these should be a huge
 * problem.
 *
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/tty.h>

#include <asm/sibyte/bcm1480_regs.h>
#include <asm/sibyte/bcm1480_scd.h>
#include <asm/sibyte/board.h>
#include <asm/io.h>

/*
 * Macros for calculating offsets into config space given a device
 * structure or dev/fun/reg
 */
#define CFGOFFSET(bus, devfn, where) (((bus)<<16)+((devfn)<<8)+(where))
#define CFGADDR(bus, devfn, where)   CFGOFFSET((bus)->number, (devfn), where)

static void *ht_cfg_space;

#define PCI_BUS_ENABLED 1
#define PCI_DEVICE_MODE 2

static int bcm1480ht_bus_status;

#define PCI_BRIDGE_DEVICE  0
#define HT_BRIDGE_DEVICE   1

/*
 * HT's level-sensitive interrupts require EOI, which is generated
 * through a 4MB memory-mapped region
 */
unsigned long ht_eoi_space;

/*
 * Read/write 32-bit values in config space.
 */
static inline u32 READCFG32(u32 addr)
{
	return *(u32 *)(ht_cfg_space + (addr&~3));
}

static inline void WRITECFG32(u32 addr, u32 data)
{
	*(u32 *)(ht_cfg_space + (addr & ~3)) = data;
}

/*
 * Some checks before doing config cycles:
 * In PCI Device Mode, hide everything on bus 0 except the LDT host
 * bridge.  Otherwise, access is controlled by bridge MasterEn bits.
 */
static int bcm1480ht_can_access(struct pci_bus *bus, int devfn)
{
	u32 devno;

	if (!(bcm1480ht_bus_status & (PCI_BUS_ENABLED | PCI_DEVICE_MODE)))
		return 0;

	if (bus->number == 0) {
		devno = PCI_SLOT(devfn);
		if (bcm1480ht_bus_status & PCI_DEVICE_MODE)
			return 0;
	}
	return 1;
}

/*
 * Read/write access functions for various sizes of values
 * in config space.  Return all 1's for disallowed accesses
 * for a kludgy but adequate simulation of master aborts.
 */

static int bcm1480ht_pcibios_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 * val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (bcm1480ht_can_access(bus, devfn))
		data = READCFG32(CFGADDR(bus, devfn, where));
	else
		data = 0xFFFFFFFF;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int bcm1480ht_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	u32 cfgaddr = CFGADDR(bus, devfn, where);
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!bcm1480ht_can_access(bus, devfn))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	data = READCFG32(cfgaddr);

	if (size == 1)
		data = (data & ~(0xff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else if (size == 2)
		data = (data & ~(0xffff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else
		data = val;

	WRITECFG32(cfgaddr, data);

	return PCIBIOS_SUCCESSFUL;
}

static int bcm1480ht_pcibios_get_busno(void)
{
	return 0;
}

struct pci_ops bcm1480ht_pci_ops = {
	.read	= bcm1480ht_pcibios_read,
	.write	= bcm1480ht_pcibios_write,
};

static struct resource bcm1480ht_mem_resource = {
	.name	= "BCM1480 HT MEM",
	.start	= A_BCM1480_PHYS_HT_MEM_MATCH_BYTES,
	.end	= A_BCM1480_PHYS_HT_MEM_MATCH_BYTES + 0x1fffffffUL,
	.flags	= IORESOURCE_MEM,
};

static struct resource bcm1480ht_io_resource = {
	.name	= "BCM1480 HT I/O",
	.start	= A_BCM1480_PHYS_HT_IO_MATCH_BYTES,
	.end	= A_BCM1480_PHYS_HT_IO_MATCH_BYTES + 0x01ffffffUL,
	.flags	= IORESOURCE_IO,
};

struct pci_controller bcm1480ht_controller = {
	.pci_ops	= &bcm1480ht_pci_ops,
	.mem_resource	= &bcm1480ht_mem_resource,
	.io_resource	= &bcm1480ht_io_resource,
	.index		= 1,
	.get_busno	= bcm1480ht_pcibios_get_busno,
	.io_offset	= A_BCM1480_PHYS_HT_IO_MATCH_BYTES,
};

static int __init bcm1480ht_pcibios_init(void)
{
	ht_cfg_space = ioremap(A_BCM1480_PHYS_HT_CFG_MATCH_BITS, 16*1024*1024);

	/* CFE doesn't always init all HT paths, so we always scan */
	bcm1480ht_bus_status |= PCI_BUS_ENABLED;

	ht_eoi_space = (unsigned long)
		ioremap(A_BCM1480_PHYS_HT_SPECIAL_MATCH_BYTES,
			4 * 1024 * 1024);
	bcm1480ht_controller.io_map_base = (unsigned long)
		ioremap(A_BCM1480_PHYS_HT_IO_MATCH_BYTES, 65536);
	bcm1480ht_controller.io_map_base -= bcm1480ht_controller.io_offset;

	register_pci_controller(&bcm1480ht_controller);

	return 0;
}

arch_initcall(bcm1480ht_pcibios_init);
