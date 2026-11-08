/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001 Keith M Wesolowski
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
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

/*
 * On CRIME 1.1, a MACE PCI register read followed by two register writes
 * can hang the machine under DMA load, so every register write in this
 * file is followed by a read.  Config cycles to empty slots end in a
 * master abort; ack it with interrupts still off so the bridge error
 * interrupt does not report it.
 */
static void mace_pci_ack_abort(void)
{
	u32 error = mace->pci.error;

	if (error & MACEPCI_ERROR_MASTER_ABORT) {
		mace->pci.error = error & ~MACEPCI_ERROR_MASTER_ABORT;
		mace->pci.error;
	}
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
	mace_pci_ack_abort();
	/*
	 * someone forgot to set the ultra bit for the onboard
	 * scsi chips; we fake it here
	 */
	if (bus->number == 0 && reg == 0x40 && size == 4 &&
	    (devfn == (1 << 3) || devfn == (2 << 3)))
		*val |= 0x1000;

	DPRINTK("read%d: reg=%08x,val=%02x\n", size * 8, reg, *val);

	return PCIBIOS_SUCCESSFUL;
}

static int
mace_pci_write_config(struct pci_bus *bus, unsigned int devfn,
		      int reg, int size, u32 val)
{
	mace->pci.config_addr = mkaddr(bus, devfn, reg);
	mace->pci.error;
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
	mace_pci_ack_abort();

	DPRINTK("write%d: reg=%08x,val=%02x\n", size * 8, reg, val);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mace_pci_ops = {
	.read = mace_pci_read_config,
	.write = mace_pci_write_config,
};
