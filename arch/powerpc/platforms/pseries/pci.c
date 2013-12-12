/*
 * Copyright (C) 2001 Dave Engebretsen, IBM Corporation
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * pSeries specific routines for PCI.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *    
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>

#include <asm/eeh.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <asm/ppc-pci.h>

#if 0
void pcibios_name_device(struct pci_dev *dev)
{
	struct device_node *dn;

	/*
	 * Add IBM loc code (slot) as a prefix to the device names for service
	 */
	dn = pci_device_to_OF_node(dev);
	if (dn) {
		const char *loc_code = of_get_property(dn, "ibm,loc-code",
				NULL);
		if (loc_code) {
			int loc_len = strlen(loc_code);
			if (loc_len < sizeof(dev->dev.name)) {
				memmove(dev->dev.name+loc_len+1, dev->dev.name,
					sizeof(dev->dev.name)-loc_len-1);
				memcpy(dev->dev.name, loc_code, loc_len);
				dev->dev.name[loc_len] = ' ';
				dev->dev.name[sizeof(dev->dev.name)-1] = '\0';
			}
		}
	}
}   
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_name_device);
#endif

static void __init pSeries_request_regions(void)
{
	if (!isa_io_base)
		return;

	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
}

void __init pSeries_final_fixup(void)
{
	pSeries_request_regions();

	eeh_addr_cache_build();
}

/*
 * Assume the winbond 82c105 is the IDE controller on a
 * p610/p615/p630. We should probably be more careful in case
 * someone tries to plug in a similar adapter.
 */
static void fixup_winbond_82c105(struct pci_dev* dev)
{
	int i;
	unsigned int reg;

	if (!machine_is(pseries))
		return;

	printk("Using INTC for W82c105 IDE controller.\n");
	pci_read_config_dword(dev, 0x40, &reg);
	/* Enable LEGIRQ to use INTC instead of ISA interrupts */
	pci_write_config_dword(dev, 0x40, reg | (1<<11));

	for (i = 0; i < DEVICE_COUNT_RESOURCE; ++i) {
		/* zap the 2nd function of the winbond chip */
		if (dev->resource[i].flags & IORESOURCE_IO
		    && dev->bus->number == 0 && dev->devfn == 0x81)
			dev->resource[i].flags &= ~IORESOURCE_IO;
		if (dev->resource[i].start == 0 && dev->resource[i].end) {
			dev->resource[i].flags = 0;
			dev->resource[i].end = 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_82C105,
			 fixup_winbond_82c105);

int pseries_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	struct device_node *dn, *pdn;
	struct pci_bus *bus;
	const __be32 *pcie_link_speed_stats;

	bus = bridge->bus;

	dn = pcibios_get_phb_of_node(bus);
	if (!dn)
		return 0;

	for (pdn = dn; pdn != NULL; pdn = of_get_next_parent(pdn)) {
		pcie_link_speed_stats = of_get_property(pdn,
			"ibm,pcie-link-speed-stats", NULL);
		if (pcie_link_speed_stats)
			break;
	}

	of_node_put(pdn);

	if (!pcie_link_speed_stats) {
		pr_err("no ibm,pcie-link-speed-stats property\n");
		return 0;
	}

	switch (be32_to_cpup(pcie_link_speed_stats)) {
	case 0x01:
		bus->max_bus_speed = PCIE_SPEED_2_5GT;
		break;
	case 0x02:
		bus->max_bus_speed = PCIE_SPEED_5_0GT;
		break;
	default:
		bus->max_bus_speed = PCI_SPEED_UNKNOWN;
		break;
	}

	switch (be32_to_cpup(pcie_link_speed_stats)) {
	case 0x01:
		bus->cur_bus_speed = PCIE_SPEED_2_5GT;
		break;
	case 0x02:
		bus->cur_bus_speed = PCIE_SPEED_5_0GT;
		break;
	default:
		bus->cur_bus_speed = PCI_SPEED_UNKNOWN;
		break;
	}

	return 0;
}
