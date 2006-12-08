/*
 *  Philips PNX8550 pci fixups.
 *
 *  Copyright 2005 Embedded Alley Solutions, Inc
 *  source@embeddealley.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-pnx8550/pci.h>
#include <asm/mach-pnx8550/int.h>


#undef	DEBUG
#ifdef 	DEBUG
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)
#endif

extern char pnx8550_irq_tab[][5];

void __init pcibios_fixup_resources(struct pci_dev *dev)
{
	/* no need to fixup IO resources */
}

void __init pcibios_fixup(void)
{
	/* nothing to do here */
}

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return pnx8550_irq_tab[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
