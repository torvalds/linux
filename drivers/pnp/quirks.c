/*
 *  This file contains quirk handling code for PnP devices
 *  Some devices do not report all their resources, and need to have extra
 *  resources added. This is most easily accomplished at initialisation time
 *  when building up the resource structure for the first time.
 *
 *  Copyright (c) 2000 Peter Denison <peterd@pnd-pc.demon.co.uk>
 *  Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 *  Heavily based on PCI quirks handling which is
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include "base.h"

static void quirk_awe32_add_ports(struct pnp_dev *dev,
				  struct pnp_option *option,
				  unsigned int offset)
{
	struct pnp_option *new_option;

	new_option = kmalloc(sizeof(struct pnp_option), GFP_KERNEL);
	if (!new_option) {
		dev_err(&dev->dev, "couldn't add ioport region to option set "
			"%d\n", pnp_option_set(option));
		return;
	}

	*new_option = *option;
	new_option->u.port.min += offset;
	new_option->u.port.max += offset;
	list_add(&new_option->list, &option->list);

	dev_info(&dev->dev, "added ioport region %#llx-%#llx to set %d\n",
		(unsigned long long) new_option->u.port.min,
		(unsigned long long) new_option->u.port.max,
		pnp_option_set(option));
}

static void quirk_awe32_resources(struct pnp_dev *dev)
{
	struct pnp_option *option;
	unsigned int set = ~0;

	/*
	 * Add two extra ioport regions (at offset 0x400 and 0x800 from the
	 * one given) to every dependent option set.
	 */
	list_for_each_entry(option, &dev->options, list) {
		if (pnp_option_is_dependent(option) &&
		    pnp_option_set(option) != set) {
			set = pnp_option_set(option);
			quirk_awe32_add_ports(dev, option, 0x800);
			quirk_awe32_add_ports(dev, option, 0x400);
		}
	}
}

static void quirk_cmi8330_resources(struct pnp_dev *dev)
{
	struct pnp_option *option;
	struct pnp_irq *irq;
	struct pnp_dma *dma;

	list_for_each_entry(option, &dev->options, list) {
		if (!pnp_option_is_dependent(option))
			continue;

		if (option->type == IORESOURCE_IRQ) {
			irq = &option->u.irq;
			bitmap_zero(irq->map.bits, PNP_IRQ_NR);
			__set_bit(5, irq->map.bits);
			__set_bit(7, irq->map.bits);
			__set_bit(10, irq->map.bits);
			dev_info(&dev->dev, "set possible IRQs in "
				 "option set %d to 5, 7, 10\n",
				 pnp_option_set(option));
		} else if (option->type == IORESOURCE_DMA) {
			dma = &option->u.dma;
			if ((dma->flags & IORESOURCE_DMA_TYPE_MASK) ==
						IORESOURCE_DMA_8BIT &&
			    dma->map != 0x0A) {
				dev_info(&dev->dev, "changing possible "
					 "DMA channel mask in option set %d "
					 "from %#02x to 0x0A (1, 3)\n",
					 pnp_option_set(option), dma->map);
				dma->map = 0x0A;
			}
		}
	}
}

static void quirk_sb16audio_resources(struct pnp_dev *dev)
{
	struct pnp_option *option;
	unsigned int prev_option_flags = ~0, n = 0;
	struct pnp_port *port;

	/*
	 * The default range on the OPL port for these devices is 0x388-0x388.
	 * Here we increase that range so that two such cards can be
	 * auto-configured.
	 */
	list_for_each_entry(option, &dev->options, list) {
		if (prev_option_flags != option->flags) {
			prev_option_flags = option->flags;
			n = 0;
		}

		if (pnp_option_is_dependent(option) &&
		    option->type == IORESOURCE_IO) {
			n++;
			port = &option->u.port;
			if (n == 3 && port->min == port->max) {
				port->max += 0x70;
				dev_info(&dev->dev, "increased option port "
					 "range from %#llx-%#llx to "
					 "%#llx-%#llx\n",
					 (unsigned long long) port->min,
					 (unsigned long long) port->min,
					 (unsigned long long) port->min,
					 (unsigned long long) port->max);
			}
		}
	}
}

static struct pnp_option *pnp_clone_dependent_set(struct pnp_dev *dev,
						  unsigned int set)
{
	struct pnp_option *tail = NULL, *first_new_option = NULL;
	struct pnp_option *option, *new_option;
	unsigned int flags;

	list_for_each_entry(option, &dev->options, list) {
		if (pnp_option_is_dependent(option))
			tail = option;
	}
	if (!tail) {
		dev_err(&dev->dev, "no dependent option sets\n");
		return NULL;
	}

	flags = pnp_new_dependent_set(dev, PNP_RES_PRIORITY_FUNCTIONAL);
	list_for_each_entry(option, &dev->options, list) {
		if (pnp_option_is_dependent(option) &&
		    pnp_option_set(option) == set) {
			new_option = kmalloc(sizeof(struct pnp_option),
					     GFP_KERNEL);
			if (!new_option) {
				dev_err(&dev->dev, "couldn't clone dependent "
					"set %d\n", set);
				return NULL;
			}

			*new_option = *option;
			new_option->flags = flags;
			if (!first_new_option)
				first_new_option = new_option;

			list_add(&new_option->list, &tail->list);
			tail = new_option;
		}
	}

	return first_new_option;
}


static void quirk_add_irq_optional_dependent_sets(struct pnp_dev *dev)
{
	struct pnp_option *new_option;
	unsigned int num_sets, i, set;
	struct pnp_irq *irq;

	num_sets = dev->num_dependent_sets;
	for (i = 0; i < num_sets; i++) {
		new_option = pnp_clone_dependent_set(dev, i);
		if (!new_option)
			return;

		set = pnp_option_set(new_option);
		while (new_option && pnp_option_set(new_option) == set) {
			if (new_option->type == IORESOURCE_IRQ) {
				irq = &new_option->u.irq;
				irq->flags |= IORESOURCE_IRQ_OPTIONAL;
			}
			dbg_pnp_show_option(dev, new_option);
			new_option = list_entry(new_option->list.next,
						struct pnp_option, list);
		}

		dev_info(&dev->dev, "added dependent option set %d (same as "
			 "set %d except IRQ optional)\n", set, i);
	}
}

static void quirk_ad1815_mpu_resources(struct pnp_dev *dev)
{
	struct pnp_option *option;
	struct pnp_irq *irq = NULL;
	unsigned int independent_irqs = 0;

	list_for_each_entry(option, &dev->options, list) {
		if (option->type == IORESOURCE_IRQ &&
		    !pnp_option_is_dependent(option)) {
			independent_irqs++;
			irq = &option->u.irq;
		}
	}

	if (independent_irqs != 1)
		return;

	irq->flags |= IORESOURCE_IRQ_OPTIONAL;
	dev_info(&dev->dev, "made independent IRQ optional\n");
}

#include <linux/pci.h>

static void quirk_system_pci_resources(struct pnp_dev *dev)
{
	struct pci_dev *pdev = NULL;
	struct resource *res;
	resource_size_t pnp_start, pnp_end, pci_start, pci_end;
	int i, j;

	/*
	 * Some BIOSes have PNP motherboard devices with resources that
	 * partially overlap PCI BARs.  The PNP system driver claims these
	 * motherboard resources, which prevents the normal PCI driver from
	 * requesting them later.
	 *
	 * This patch disables the PNP resources that conflict with PCI BARs
	 * so they won't be claimed by the PNP system driver.
	 */
	for_each_pci_dev(pdev) {
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			unsigned int type;

			type = pci_resource_flags(pdev, i) &
					(IORESOURCE_IO | IORESOURCE_MEM);
			if (!type || pci_resource_len(pdev, i) == 0)
				continue;

			pci_start = pci_resource_start(pdev, i);
			pci_end = pci_resource_end(pdev, i);
			for (j = 0;
			     (res = pnp_get_resource(dev, type, j)); j++) {
				if (res->start == 0 && res->end == 0)
					continue;

				pnp_start = res->start;
				pnp_end = res->end;

				/*
				 * If the PNP region doesn't overlap the PCI
				 * region at all, there's no problem.
				 */
				if (pnp_end < pci_start || pnp_start > pci_end)
					continue;

				/*
				 * If the PNP region completely encloses (or is
				 * at least as large as) the PCI region, that's
				 * also OK.  For example, this happens when the
				 * PNP device describes a bridge with PCI
				 * behind it.
				 */
				if (pnp_start <= pci_start &&
				    pnp_end >= pci_end)
					continue;

				/*
				 * Otherwise, the PNP region overlaps *part* of
				 * the PCI region, and that might prevent a PCI
				 * driver from requesting its resources.
				 */
				dev_warn(&dev->dev, "%s resource "
					"(0x%llx-0x%llx) overlaps %s BAR %d "
					"(0x%llx-0x%llx), disabling\n",
					pnp_resource_type_name(res),
					(unsigned long long) pnp_start,
					(unsigned long long) pnp_end,
					pci_name(pdev), i,
					(unsigned long long) pci_start,
					(unsigned long long) pci_end);
				res->flags |= IORESOURCE_DISABLED;
			}
		}
	}
}

/*
 *  PnP Quirks
 *  Cards or devices that need some tweaking due to incomplete resource info
 */

static struct pnp_fixup pnp_fixups[] = {
	/* Soundblaster awe io port quirk */
	{"CTL0021", quirk_awe32_resources},
	{"CTL0022", quirk_awe32_resources},
	{"CTL0023", quirk_awe32_resources},
	/* CMI 8330 interrupt and dma fix */
	{"@X@0001", quirk_cmi8330_resources},
	/* Soundblaster audio device io port range quirk */
	{"CTL0001", quirk_sb16audio_resources},
	{"CTL0031", quirk_sb16audio_resources},
	{"CTL0041", quirk_sb16audio_resources},
	{"CTL0042", quirk_sb16audio_resources},
	{"CTL0043", quirk_sb16audio_resources},
	{"CTL0044", quirk_sb16audio_resources},
	{"CTL0045", quirk_sb16audio_resources},
	/* Add IRQ-optional MPU options */
	{"ADS7151", quirk_ad1815_mpu_resources},
	{"ADS7181", quirk_add_irq_optional_dependent_sets},
	{"AZT0002", quirk_add_irq_optional_dependent_sets},
	/* PnP resources that might overlap PCI BARs */
	{"PNP0c01", quirk_system_pci_resources},
	{"PNP0c02", quirk_system_pci_resources},
	{""}
};

void pnp_fixup_device(struct pnp_dev *dev)
{
	struct pnp_fixup *f;

	for (f = pnp_fixups; *f->id; f++) {
		if (!compare_pnp_id(dev->id, f->id))
			continue;
		pnp_dbg(&dev->dev, "%s: calling %pF\n", f->id,
			f->quirk_function);
		f->quirk_function(dev);
	}
}
