/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001 Keith M Wesolowski
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/pci.h>
#include <asm/ip32/mace.h>

#if 0
# define DPRINTK(args...) printk(args);
#else
# define DPRINTK(args...)
#endif

/*
 * O2 has up to 5 PCI devices connected into the MACE bridge.  The device
 * map looks like this:
 *
 * 0  aic7xxx 0
 * 1  aic7xxx 1
 * 2  expansion slot
 * 3  N/C
 * 4  N/C
 */

static inline int mkaddr(struct pci_bus *bus, unsigned int devfn,
	unsigned int reg)
{
	return ((bus->number & 0xff) << 16) |
		((devfn & 0xff) << 8) |
		(reg & 0xfc);
}


static int
mace_pci_read_config(struct pci_bus *bus, unsigned int devfn,
		     int reg, int size, u32 *val)
{
	mace->pci.config_addr = mkaddr(bus, devfn, reg);
	switch (size) {
	case 1:
		*val = mace->pci.config_data.b[(reg & 3) ^ 3];
		break;
	case 2:
		*val = mace->pci.config_data.w[((reg >> 1) & 1) ^ 1];
		break;
	case 4:
		*val = mace->pci.config_data.l;
		break;
	}

	DPRINTK("read%d: reg=%08x,val=%02x\n", size * 8, reg, *val);

	return PCIBIOS_SUCCESSFUL;
}

static int
mace_pci_write_config(struct pci_bus *bus, unsigned int devfn,
		      int reg, int size, u32 val)
{
	mace->pci.config_addr = mkaddr(bus, devfn, reg);
	switch (size) {
	case 1:
		mace->pci.config_data.b[(reg & 3) ^ 3] = val;
		break;
	case 2:
		mace->pci.config_data.w[((reg >> 1) & 1) ^ 1] = val;
		break;
	case 4:
		mace->pci.config_data.l = val;
		break;
	}

	DPRINTK("write%d: reg=%08x,val=%02x\n", size * 8, reg, val);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mace_pci_ops = {
	.read = mace_pci_read_config,
	.write = mace_pci_write_config,
};
