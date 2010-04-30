/*
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * BCM1250-specific PCI support
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
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/vt.h>

#include <asm/io.h>

#include <asm/sibyte/sb1250_defs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/board.h>

/*
 * Macros for calculating offsets into config space given a device
 * structure or dev/fun/reg
 */
#define CFGOFFSET(bus, devfn, where) (((bus)<<16) + ((devfn)<<8) + (where))
#define CFGADDR(bus, devfn, where)   CFGOFFSET((bus)->number, (devfn), where)

static void *cfg_space;

#define PCI_BUS_ENABLED	1
#define LDT_BUS_ENABLED	2
#define PCI_DEVICE_MODE	4

static int sb1250_bus_status;

#define PCI_BRIDGE_DEVICE  0
#define LDT_BRIDGE_DEVICE  1

#ifdef CONFIG_SIBYTE_HAS_LDT
/*
 * HT's level-sensitive interrupts require EOI, which is generated
 * through a 4MB memory-mapped region
 */
unsigned long ldt_eoi_space;
#endif

/*
 * Read/write 32-bit values in config space.
 */
static inline u32 READCFG32(u32 addr)
{
	return *(u32 *) (cfg_space + (addr & ~3));
}

static inline void WRITECFG32(u32 addr, u32 data)
{
	*(u32 *) (cfg_space + (addr & ~3)) = data;
}

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return dev->irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

/*
 * Some checks before doing config cycles:
 * In PCI Device Mode, hide everything on bus 0 except the LDT host
 * bridge.  Otherwise, access is controlled by bridge MasterEn bits.
 */
static int sb1250_pci_can_access(struct pci_bus *bus, int devfn)
{
	u32 devno;

	if (!(sb1250_bus_status & (PCI_BUS_ENABLED | PCI_DEVICE_MODE)))
		return 0;

	if (bus->number == 0) {
		devno = PCI_SLOT(devfn);
		if (devno == LDT_BRIDGE_DEVICE)
			return (sb1250_bus_status & LDT_BUS_ENABLED) != 0;
		else if (sb1250_bus_status & PCI_DEVICE_MODE)
			return 0;
		else
			return 1;
	} else
		return 1;
}

/*
 * Read/write access functions for various sizes of values
 * in config space.  Return all 1's for disallowed accesses
 * for a kludgy but adequate simulation of master aborts.
 */

static int sb1250_pcibios_read(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 * val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (sb1250_pci_can_access(bus, devfn))
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

static int sb1250_pcibios_write(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	u32 cfgaddr = CFGADDR(bus, devfn, where);
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (!sb1250_pci_can_access(bus, devfn))
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

struct pci_ops sb1250_pci_ops = {
	.read	= sb1250_pcibios_read,
	.write	= sb1250_pcibios_write,
};

static struct resource sb1250_mem_resource = {
	.name	= "SB1250 PCI MEM",
	.start	= 0x40000000UL,
	.end	= 0x5fffffffUL,
	.flags	= IORESOURCE_MEM,
};

static struct resource sb1250_io_resource = {
	.name	= "SB1250 PCI I/O",
	.start	= 0x00000000UL,
	.end	= 0x01ffffffUL,
	.flags	= IORESOURCE_IO,
};

struct pci_controller sb1250_controller = {
	.pci_ops	= &sb1250_pci_ops,
	.mem_resource	= &sb1250_mem_resource,
	.io_resource	= &sb1250_io_resource,
};

static int __init sb1250_pcibios_init(void)
{
	void __iomem *io_map_base;
	uint32_t cmdreg;
	uint64_t reg;

	/* CFE will assign PCI resources */
	pci_probe_only = 1;

	/* Avoid ISA compat ranges.  */
	PCIBIOS_MIN_IO = 0x00008000UL;
	PCIBIOS_MIN_MEM = 0x01000000UL;

	/* Set I/O resource limits.  */
	ioport_resource.end = 0x01ffffffUL;	/* 32MB accessible by sb1250 */
	iomem_resource.end = 0xffffffffUL;	/* no HT support yet */

	cfg_space =
	    ioremap(A_PHYS_LDTPCI_CFG_MATCH_BITS, 16 * 1024 * 1024);

	/*
	 * See if the PCI bus has been configured by the firmware.
	 */
	reg = __raw_readq(IOADDR(A_SCD_SYSTEM_CFG));
	if (!(reg & M_SYS_PCI_HOST)) {
		sb1250_bus_status |= PCI_DEVICE_MODE;
	} else {
		cmdreg =
		    READCFG32(CFGOFFSET
			      (0, PCI_DEVFN(PCI_BRIDGE_DEVICE, 0),
			       PCI_COMMAND));
		if (!(cmdreg & PCI_COMMAND_MASTER)) {
			printk
			    ("PCI: Skipping PCI probe.  Bus is not initialized.\n");
			iounmap(cfg_space);
			return 0;
		}
		sb1250_bus_status |= PCI_BUS_ENABLED;
	}

	/*
	 * Establish mappings in KSEG2 (kernel virtual) to PCI I/O
	 * space.  Use "match bytes" policy to make everything look
	 * little-endian.  So, you need to also set
	 * CONFIG_SWAP_IO_SPACE, but this is the combination that
	 * works correctly with most of Linux's drivers.
	 * XXX ehs: Should this happen in PCI Device mode?
	 */
	io_map_base = ioremap(A_PHYS_LDTPCI_IO_MATCH_BYTES, 1024 * 1024);
	sb1250_controller.io_map_base = (unsigned long)io_map_base;
	set_io_port_base((unsigned long)io_map_base);

#ifdef CONFIG_SIBYTE_HAS_LDT
	/*
	 * Also check the LDT bridge's enable, just in case we didn't
	 * initialize that one.
	 */

	cmdreg = READCFG32(CFGOFFSET(0, PCI_DEVFN(LDT_BRIDGE_DEVICE, 0),
				     PCI_COMMAND));
	if (cmdreg & PCI_COMMAND_MASTER) {
		sb1250_bus_status |= LDT_BUS_ENABLED;

		/*
		 * Need bits 23:16 to convey vector number.  Note that
		 * this consumes 4MB of kernel-mapped memory
		 * (Kseg2/Kseg3) for 32-bit kernel.
		 */
		ldt_eoi_space = (unsigned long)
		    ioremap(A_PHYS_LDT_SPECIAL_MATCH_BYTES,
			    4 * 1024 * 1024);
	}
#endif

	register_pci_controller(&sb1250_controller);

#ifdef CONFIG_VGA_CONSOLE
	take_over_console(&vga_con, 0, MAX_NR_CONSOLES - 1, 1);
#endif
	return 0;
}
arch_initcall(sb1250_pcibios_init);
