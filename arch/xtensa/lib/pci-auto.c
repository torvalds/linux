/*
 * arch/xtensa/lib/pci-auto.c
 *
 * PCI autoconfiguration library
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel <zankel@tensilica.com, cez@zankel.net>
 *
 * Based on work from Matt Porter <mporter@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/pci-bridge.h>


/*
 *
 * Setting up a PCI
 *
 * pci_ctrl->first_busno = <first bus number (0)>
 * pci_ctrl->last_busno = <last bus number (0xff)>
 * pci_ctrl->ops = <PCI config operations>
 * pci_ctrl->map_irq = <function to return the interrupt number for a device>
 *
 * pci_ctrl->io_space.start = <IO space start address (PCI view)>
 * pci_ctrl->io_space.end = <IO space end address (PCI view)>
 * pci_ctrl->io_space.base = <IO space offset: address 0 from CPU space>
 * pci_ctrl->mem_space.start = <MEM space start address (PCI view)>
 * pci_ctrl->mem_space.end = <MEM space end address (PCI view)>
 * pci_ctrl->mem_space.base = <MEM space offset: address 0 from CPU space>
 *
 * pcibios_init_resource(&pci_ctrl->io_resource, <IO space start>,
 * 			 <IO space end>, IORESOURCE_IO, "PCI host bridge");
 * pcibios_init_resource(&pci_ctrl->mem_resources[0], <MEM space start>,
 * 			 <MEM space end>, IORESOURCE_MEM, "PCI host bridge");
 *
 * pci_ctrl->last_busno = pciauto_bus_scan(pci_ctrl,pci_ctrl->first_busno);
 *
 * int __init pciauto_bus_scan(struct pci_controller *pci_ctrl, int current_bus)
 *
 */


/* define DEBUG to print some debugging messages. */

#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

static int pciauto_upper_iospc;
static int pciauto_upper_memspc;

static struct pci_dev pciauto_dev;
static struct pci_bus pciauto_bus;

/*
 * Helper functions
 */

/* Initialize the bars of a PCI device.  */

static void __init
pciauto_setup_bars(struct pci_dev *dev, int bar_limit)
{
	int bar_size;
	int bar, bar_nr;
	int *upper_limit;
	int found_mem64 = 0;

	for (bar = PCI_BASE_ADDRESS_0, bar_nr = 0;
	     bar <= bar_limit;
	     bar+=4, bar_nr++)
	{
		/* Tickle the BAR and get the size */
		pci_write_config_dword(dev, bar, 0xffffffff);
		pci_read_config_dword(dev, bar, &bar_size);

		/* If BAR is not implemented go to the next BAR */
		if (!bar_size)
			continue;

		/* Check the BAR type and set our address mask */
		if (bar_size & PCI_BASE_ADDRESS_SPACE_IO)
		{
			bar_size &= PCI_BASE_ADDRESS_IO_MASK;
			upper_limit = &pciauto_upper_iospc;
			DBG("PCI Autoconfig: BAR %d, I/O, ", bar_nr);
		}
		else
		{
			if ((bar_size & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
			    PCI_BASE_ADDRESS_MEM_TYPE_64)
				found_mem64 = 1;

			bar_size &= PCI_BASE_ADDRESS_MEM_MASK;
			upper_limit = &pciauto_upper_memspc;
			DBG("PCI Autoconfig: BAR %d, Mem, ", bar_nr);
		}

		/* Allocate a base address (bar_size is negative!) */
		*upper_limit = (*upper_limit + bar_size) & bar_size;

		/* Write it out and update our limit */
		pci_write_config_dword(dev, bar, *upper_limit);

		/*
		 * If we are a 64-bit decoder then increment to the
		 * upper 32 bits of the bar and force it to locate
		 * in the lower 4GB of memory.
		 */

		if (found_mem64)
			pci_write_config_dword(dev, (bar+=4), 0x00000000);

		DBG("size=0x%x, address=0x%x\n", ~bar_size + 1, *upper_limit);
	}
}

/* Initialize the interrupt number. */

static void __init
pciauto_setup_irq(struct pci_controller* pci_ctrl,struct pci_dev *dev,int devfn)
{
	u8 pin;
	int irq = 0;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);

	/* Fix illegal pin numbers. */

	if (pin == 0 || pin > 4)
		pin = 1;

	if (pci_ctrl->map_irq)
		irq = pci_ctrl->map_irq(dev, PCI_SLOT(devfn), pin);

	if (irq == -1)
		irq = 0;

	DBG("PCI Autoconfig: Interrupt %d, pin %d\n", irq, pin);

	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}


static void __init
pciauto_prescan_setup_bridge(struct pci_dev *dev, int current_bus,
			     int sub_bus, int *iosave, int *memsave)
{
	/* Configure bus number registers */
	pci_write_config_byte(dev, PCI_PRIMARY_BUS, current_bus);
	pci_write_config_byte(dev, PCI_SECONDARY_BUS, sub_bus + 1);
	pci_write_config_byte(dev, PCI_SUBORDINATE_BUS,	0xff);

	/* Round memory allocator to 1MB boundary */
	pciauto_upper_memspc &= ~(0x100000 - 1);
	*memsave = pciauto_upper_memspc;

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);
	*iosave = pciauto_upper_iospc;

	/* Set up memory and I/O filter limits, assume 32-bit I/O space */
	pci_write_config_word(dev, PCI_MEMORY_LIMIT,
			      ((pciauto_upper_memspc - 1) & 0xfff00000) >> 16);
	pci_write_config_byte(dev, PCI_IO_LIMIT,
			      ((pciauto_upper_iospc - 1) & 0x0000f000) >> 8);
	pci_write_config_word(dev, PCI_IO_LIMIT_UPPER16,
			      ((pciauto_upper_iospc - 1) & 0xffff0000) >> 16);
}

static void __init
pciauto_postscan_setup_bridge(struct pci_dev *dev, int current_bus, int sub_bus,
			      int *iosave, int *memsave)
{
	int cmdstat;

	/* Configure bus number registers */
	pci_write_config_byte(dev, PCI_SUBORDINATE_BUS,	sub_bus);

	/*
	 * Round memory allocator to 1MB boundary.
	 * If no space used, allocate minimum.
	 */
	pciauto_upper_memspc &= ~(0x100000 - 1);
	if (*memsave == pciauto_upper_memspc)
		pciauto_upper_memspc -= 0x00100000;

	pci_write_config_word(dev, PCI_MEMORY_BASE, pciauto_upper_memspc >> 16);

	/* Allocate 1MB for pre-fretch */
	pci_write_config_word(dev, PCI_PREF_MEMORY_LIMIT,
			      ((pciauto_upper_memspc - 1) & 0xfff00000) >> 16);

	pciauto_upper_memspc -= 0x100000;

	pci_write_config_word(dev, PCI_PREF_MEMORY_BASE,
			      pciauto_upper_memspc >> 16);

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);
	if (*iosave == pciauto_upper_iospc)
		pciauto_upper_iospc -= 0x1000;

	pci_write_config_byte(dev, PCI_IO_BASE,
			      (pciauto_upper_iospc & 0x0000f000) >> 8);
	pci_write_config_word(dev, PCI_IO_BASE_UPPER16,
			      pciauto_upper_iospc >> 16);

	/* Enable memory and I/O accesses, enable bus master */
	pci_read_config_dword(dev, PCI_COMMAND, &cmdstat);
	pci_write_config_dword(dev, PCI_COMMAND,
			       cmdstat |
			       PCI_COMMAND_IO |
			       PCI_COMMAND_MEMORY |
			       PCI_COMMAND_MASTER);
}

/*
 * Scan the current PCI bus.
 */


int __init pciauto_bus_scan(struct pci_controller *pci_ctrl, int current_bus)
{
	int sub_bus, pci_devfn, pci_class, cmdstat, found_multi=0;
	unsigned short vid;
	unsigned char header_type;
	struct pci_dev *dev = &pciauto_dev;

	pciauto_dev.bus = &pciauto_bus;
	pciauto_dev.sysdata = pci_ctrl;
	pciauto_bus.ops = pci_ctrl->ops;

	/*
	 * Fetch our I/O and memory space upper boundaries used
	 * to allocated base addresses on this pci_controller.
	 */

	if (current_bus == pci_ctrl->first_busno)
	{
		pciauto_upper_iospc = pci_ctrl->io_resource.end + 1;
		pciauto_upper_memspc = pci_ctrl->mem_resources[0].end + 1;
	}

	sub_bus = current_bus;

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++)
	{
		/* Skip our host bridge */
		if ((current_bus == pci_ctrl->first_busno) && (pci_devfn == 0))
			continue;

		if (PCI_FUNC(pci_devfn) && !found_multi)
			continue;

		pciauto_bus.number = current_bus;
		pciauto_dev.devfn = pci_devfn;

		/* If config space read fails from this device, move on */
		if (pci_read_config_byte(dev, PCI_HEADER_TYPE, &header_type))
			continue;

		if (!PCI_FUNC(pci_devfn))
			found_multi = header_type & 0x80;
		pci_read_config_word(dev, PCI_VENDOR_ID, &vid);

		if (vid == 0xffff || vid == 0x0000) {
			found_multi = 0;
			continue;
		}

		pci_read_config_dword(dev, PCI_CLASS_REVISION, &pci_class);

		if ((pci_class >> 16) == PCI_CLASS_BRIDGE_PCI) {

			int iosave, memsave;

			DBG("PCI Autoconfig: Found P2P bridge, device %d\n",
			    PCI_SLOT(pci_devfn));

			/* Allocate PCI I/O and/or memory space */
			pciauto_setup_bars(dev, PCI_BASE_ADDRESS_1);

			pciauto_prescan_setup_bridge(dev, current_bus, sub_bus,
					&iosave, &memsave);
			sub_bus = pciauto_bus_scan(pci_ctrl, sub_bus+1);
			pciauto_postscan_setup_bridge(dev, current_bus, sub_bus,
					&iosave, &memsave);
			pciauto_bus.number = current_bus;

			continue;

		}


#if 0
		/* Skip legacy mode IDE controller */

		if ((pci_class >> 16) == PCI_CLASS_STORAGE_IDE) {

			unsigned char prg_iface;
			pci_read_config_byte(dev, PCI_CLASS_PROG, &prg_iface);

			if (!(prg_iface & PCIAUTO_IDE_MODE_MASK)) {
				DBG("PCI Autoconfig: Skipping legacy mode "
				    "IDE controller\n");
				continue;
			}
		}
#endif

		/*
		 * Found a peripheral, enable some standard
		 * settings
		 */

		pci_read_config_dword(dev, PCI_COMMAND,	&cmdstat);
		pci_write_config_dword(dev, PCI_COMMAND,
				cmdstat |
					PCI_COMMAND_IO |
					PCI_COMMAND_MEMORY |
					PCI_COMMAND_MASTER);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x80);

		/* Allocate PCI I/O and/or memory space */
		DBG("PCI Autoconfig: Found Bus %d, Device %d, Function %d\n",
		    current_bus, PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn) );

		pciauto_setup_bars(dev, PCI_BASE_ADDRESS_5);
		pciauto_setup_irq(pci_ctrl, dev, pci_devfn);
	}
	return sub_bus;
}
