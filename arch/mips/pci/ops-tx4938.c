/*
 * Define the pci_ops for the Toshiba rbtx4938
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/addrspace.h>
#include <asm/tx4938/rbtx4938.h>

/* initialize in setup */
struct resource pci_io_resource = {
	.name	= "pci IO space",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_IO
};

/* initialize in setup */
struct resource pci_mem_resource = {
	.name	= "pci memory space",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_MEM
};

struct resource tx4938_pcic1_pci_io_resource = {
	.name	= "PCI1 IO",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_IO
};
struct resource tx4938_pcic1_pci_mem_resource = {
	.name	= "PCI1 mem",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_MEM
};

static int mkaddr(int bus, int dev_fn, int where,
		  struct tx4938_pcic_reg *pcicptr)
{
	if (bus > 0) {
		/* Type 1 configuration */
		pcicptr->g2pcfgadrs = ((bus & 0xff) << 0x10) |
		    ((dev_fn & 0xff) << 0x08) | (where & 0xfc) | 1;
	} else {
		if (dev_fn >= PCI_DEVFN(TX4938_PCIC_MAX_DEVNU, 0))
			return -1;

		/* Type 0 configuration */
		pcicptr->g2pcfgadrs = ((bus & 0xff) << 0x10) |
		    ((dev_fn & 0xff) << 0x08) | (where & 0xfc);
	}
	/* clear M_ABORT and Disable M_ABORT Int. */
	pcicptr->pcistatus =
	    (pcicptr->pcistatus & 0x0000ffff) |
	    (PCI_STATUS_REC_MASTER_ABORT << 16);
	pcicptr->pcimask &= ~PCI_STATUS_REC_MASTER_ABORT;

	return 0;
}

static int check_abort(struct tx4938_pcic_reg *pcicptr)
{
	int code = PCIBIOS_SUCCESSFUL;
	/* wait write cycle completion before checking error status */
	while (pcicptr->pcicstatus & TX4938_PCIC_PCICSTATUS_IWB)
				;
	if (pcicptr->pcistatus & (PCI_STATUS_REC_MASTER_ABORT << 16)) {
		pcicptr->pcistatus =
		    (pcicptr->
		     pcistatus & 0x0000ffff) | (PCI_STATUS_REC_MASTER_ABORT
						<< 16);
		pcicptr->pcimask |= PCI_STATUS_REC_MASTER_ABORT;
		code = PCIBIOS_DEVICE_NOT_FOUND;
	}
	return code;
}

extern struct pci_controller tx4938_pci_controller[];
extern struct tx4938_pcic_reg *get_tx4938_pcicptr(int ch);

static struct tx4938_pcic_reg *pci_bus_to_pcicptr(struct pci_bus *bus)
{
	struct pci_controller *channel = bus->sysdata;
	return get_tx4938_pcicptr(channel - &tx4938_pci_controller[0]);
}

static int tx4938_pcibios_read_config(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 * val)
{
	int retval, dev, busno, func;
	struct tx4938_pcic_reg *pcicptr = pci_bus_to_pcicptr(bus);
	void __iomem *cfgdata =
		(void __iomem *)(unsigned long)&pcicptr->g2pcfgdata;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* check if the bus is top-level */
	if (bus->parent != NULL)
		busno = bus->number;
	else {
		busno = 0;
	}

	if (mkaddr(busno, devfn, where, pcicptr))
		return -1;

	switch (size) {
	case 1:
#ifdef __BIG_ENDIAN
		cfgdata += (where & 3) ^ 3;
#else
		cfgdata += where & 3;
#endif
		*val = __raw_readb(cfgdata);
		break;
	case 2:
#ifdef __BIG_ENDIAN
		cfgdata += (where & 2) ^ 2;
#else
		cfgdata += where & 2;
#endif
		*val = __raw_readw(cfgdata);
		break;
	case 4:
		*val = __raw_readl(cfgdata);
		break;
	}

	retval = check_abort(pcicptr);
	if (retval == PCIBIOS_DEVICE_NOT_FOUND)
		*val = 0xffffffff;

	return retval;
}

static int tx4938_pcibios_write_config(struct pci_bus *bus, unsigned int devfn, int where,
						int size, u32 val)
{
	int dev, busno, func;
	struct tx4938_pcic_reg *pcicptr = pci_bus_to_pcicptr(bus);
	void __iomem *cfgdata =
		(void __iomem *)(unsigned long)&pcicptr->g2pcfgdata;

	busno = bus->number;
	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);

	/* check if the bus is top-level */
	if (bus->parent != NULL) {
		busno = bus->number;
	} else {
		busno = 0;
	}

	if (mkaddr(busno, devfn, where, pcicptr))
		return -1;

	switch (size) {
	case 1:
#ifdef __BIG_ENDIAN
		cfgdata += (where & 3) ^ 3;
#else
		cfgdata += where & 3;
#endif
		__raw_writeb(val, cfgdata);
		break;
	case 2:
#ifdef __BIG_ENDIAN
		cfgdata += (where & 2) ^ 2;
#else
		cfgdata += where & 2;
#endif
		__raw_writew(val, cfgdata);
		break;
	case 4:
		__raw_writel(val, cfgdata);
		break;
	}

	return check_abort(pcicptr);
}

struct pci_ops tx4938_pci_ops = {
	tx4938_pcibios_read_config,
	tx4938_pcibios_write_config
};

struct pci_controller tx4938_pci_controller[] = {
	/* h/w only supports devices 0x00 to 0x14 */
	{
		.pci_ops        = &tx4938_pci_ops,
		.io_resource    = &pci_io_resource,
		.mem_resource   = &pci_mem_resource,
	},
	/* h/w only supports devices 0x00 to 0x14 */
	{
		.pci_ops        = &tx4938_pci_ops,
		.io_resource    = &tx4938_pcic1_pci_io_resource,
		.mem_resource   = &tx4938_pcic1_pci_mem_resource,
        }
};
