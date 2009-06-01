/*
 *  pci_irq.c - ACPI PCI Interrupt Routing ($Revision: 11 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002       Dominik Brodowski <devel@brodo.de>
 *  (c) Copyright 2008 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */


#include <linux/dmi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_irq");

struct acpi_prt_entry {
	struct list_head	list;
	struct acpi_pci_id	id;
	u8			pin;
	acpi_handle		link;
	u32			index;		/* GSI, or link _CRS index */
};

static LIST_HEAD(acpi_prt_list);
static DEFINE_SPINLOCK(acpi_prt_lock);

static inline char pin_name(int pin)
{
	return 'A' + pin - 1;
}

/* --------------------------------------------------------------------------
                         PCI IRQ Routing Table (PRT) Support
   -------------------------------------------------------------------------- */

static struct acpi_prt_entry *acpi_pci_irq_find_prt_entry(struct pci_dev *dev,
							  int pin)
{
	struct acpi_prt_entry *entry;
	int segment = pci_domain_nr(dev->bus);
	int bus = dev->bus->number;
	int device = PCI_SLOT(dev->devfn);

	spin_lock(&acpi_prt_lock);
	list_for_each_entry(entry, &acpi_prt_list, list) {
		if ((segment == entry->id.segment)
		    && (bus == entry->id.bus)
		    && (device == entry->id.device)
		    && (pin == entry->pin)) {
			spin_unlock(&acpi_prt_lock);
			return entry;
		}
	}
	spin_unlock(&acpi_prt_lock);
	return NULL;
}

/* http://bugzilla.kernel.org/show_bug.cgi?id=4773 */
static const struct dmi_system_id medion_md9580[] = {
	{
		.ident = "Medion MD9580-F laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MEDIONNB"),
			DMI_MATCH(DMI_PRODUCT_NAME, "A555"),
		},
	},
	{ }
};

/* http://bugzilla.kernel.org/show_bug.cgi?id=5044 */
static const struct dmi_system_id dell_optiplex[] = {
	{
		.ident = "Dell Optiplex GX1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex GX1 600S+"),
		},
	},
	{ }
};

/* http://bugzilla.kernel.org/show_bug.cgi?id=10138 */
static const struct dmi_system_id hp_t5710[] = {
	{
		.ident = "HP t5710",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "hp t5000 series"),
			DMI_MATCH(DMI_BOARD_NAME, "098Ch"),
		},
	},
	{ }
};

struct prt_quirk {
	const struct dmi_system_id *system;
	unsigned int		segment;
	unsigned int		bus;
	unsigned int		device;
	unsigned char		pin;
	const char		*source;	/* according to BIOS */
	const char		*actual_source;
};

#define PCI_INTX_PIN(c)		(c - 'A' + 1)

/*
 * These systems have incorrect _PRT entries.  The BIOS claims the PCI
 * interrupt at the listed segment/bus/device/pin is connected to the first
 * link device, but it is actually connected to the second.
 */
static const struct prt_quirk prt_quirks[] = {
	{ medion_md9580, 0, 0, 9, PCI_INTX_PIN('A'),
		"\\_SB_.PCI0.ISA_.LNKA",
		"\\_SB_.PCI0.ISA_.LNKB"},
	{ dell_optiplex, 0, 0, 0xd, PCI_INTX_PIN('A'),
		"\\_SB_.LNKB",
		"\\_SB_.LNKA"},
	{ hp_t5710, 0, 0, 1, PCI_INTX_PIN('A'),
		"\\_SB_.PCI0.LNK1",
		"\\_SB_.PCI0.LNK3"},
};

static void do_prt_fixups(struct acpi_prt_entry *entry,
			  struct acpi_pci_routing_table *prt)
{
	int i;
	const struct prt_quirk *quirk;

	for (i = 0; i < ARRAY_SIZE(prt_quirks); i++) {
		quirk = &prt_quirks[i];

		/* All current quirks involve link devices, not GSIs */
		if (!prt->source)
			continue;

		if (dmi_check_system(quirk->system) &&
		    entry->id.segment == quirk->segment &&
		    entry->id.bus == quirk->bus &&
		    entry->id.device == quirk->device &&
		    entry->pin == quirk->pin &&
		    !strcmp(prt->source, quirk->source) &&
		    strlen(prt->source) >= strlen(quirk->actual_source)) {
			printk(KERN_WARNING PREFIX "firmware reports "
				"%04x:%02x:%02x PCI INT %c connected to %s; "
				"changing to %s\n",
				entry->id.segment, entry->id.bus,
				entry->id.device, pin_name(entry->pin),
				prt->source, quirk->actual_source);
			strcpy(prt->source, quirk->actual_source);
		}
	}
}

static int acpi_pci_irq_add_entry(acpi_handle handle, int segment, int bus,
				  struct acpi_pci_routing_table *prt)
{
	struct acpi_prt_entry *entry;

	entry = kzalloc(sizeof(struct acpi_prt_entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	/*
	 * Note that the _PRT uses 0=INTA, 1=INTB, etc, while PCI uses
	 * 1=INTA, 2=INTB.  We use the PCI encoding throughout, so convert
	 * it here.
	 */
	entry->id.segment = segment;
	entry->id.bus = bus;
	entry->id.device = (prt->address >> 16) & 0xFFFF;
	entry->pin = prt->pin + 1;

	do_prt_fixups(entry, prt);

	entry->index = prt->source_index;

	/*
	 * Type 1: Dynamic
	 * ---------------
	 * The 'source' field specifies the PCI interrupt link device used to
	 * configure the IRQ assigned to this slot|dev|pin.  The 'source_index'
	 * indicates which resource descriptor in the resource template (of
	 * the link device) this interrupt is allocated from.
	 * 
	 * NOTE: Don't query the Link Device for IRQ information at this time
	 *       because Link Device enumeration may not have occurred yet
	 *       (e.g. exists somewhere 'below' this _PRT entry in the ACPI
	 *       namespace).
	 */
	if (prt->source[0])
		acpi_get_handle(handle, prt->source, &entry->link);

	/*
	 * Type 2: Static
	 * --------------
	 * The 'source' field is NULL, and the 'source_index' field specifies
	 * the IRQ value, which is hardwired to specific interrupt inputs on
	 * the interrupt controller.
	 */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INFO,
			      "      %04x:%02x:%02x[%c] -> %s[%d]\n",
			      entry->id.segment, entry->id.bus,
			      entry->id.device, pin_name(entry->pin),
			      prt->source, entry->index));

	spin_lock(&acpi_prt_lock);
	list_add_tail(&entry->list, &acpi_prt_list);
	spin_unlock(&acpi_prt_lock);

	return 0;
}

int acpi_pci_irq_add_prt(acpi_handle handle, int segment, int bus)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_pci_routing_table *entry;

	/* 'handle' is the _PRT's parent (root bridge or PCI-PCI bridge) */
	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	printk(KERN_DEBUG "ACPI: PCI Interrupt Routing Table [%s._PRT]\n",
	       (char *) buffer.pointer);

	kfree(buffer.pointer);

	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;

	status = acpi_get_irq_routing_table(handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRT [%s]",
				acpi_format_exception(status)));
		kfree(buffer.pointer);
		return -ENODEV;
	}

	entry = buffer.pointer;
	while (entry && (entry->length > 0)) {
		acpi_pci_irq_add_entry(handle, segment, bus, entry);
		entry = (struct acpi_pci_routing_table *)
		    ((unsigned long)entry + entry->length);
	}

	kfree(buffer.pointer);
	return 0;
}

void acpi_pci_irq_del_prt(int segment, int bus)
{
	struct acpi_prt_entry *entry, *tmp;

	printk(KERN_DEBUG
	       "ACPI: Delete PCI Interrupt Routing Table for %04x:%02x\n",
	       segment, bus);
	spin_lock(&acpi_prt_lock);
	list_for_each_entry_safe(entry, tmp, &acpi_prt_list, list) {
		if (segment == entry->id.segment && bus == entry->id.bus) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
	spin_unlock(&acpi_prt_lock);
}

/* --------------------------------------------------------------------------
                          PCI Interrupt Routing Support
   -------------------------------------------------------------------------- */
static struct acpi_prt_entry *acpi_pci_irq_lookup(struct pci_dev *dev, int pin)
{
	struct acpi_prt_entry *entry;
	struct pci_dev *bridge;
	u8 bridge_pin, orig_pin = pin;

	entry = acpi_pci_irq_find_prt_entry(dev, pin);
	if (entry) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %s[%c] _PRT entry\n",
				  pci_name(dev), pin_name(pin)));
		return entry;
	}

	/* 
	 * Attempt to derive an IRQ for this device from a parent bridge's
	 * PCI interrupt routing entry (eg. yenta bridge and add-in card bridge).
	 */
	bridge = dev->bus->self;
	while (bridge) {
		pin = pci_swizzle_interrupt_pin(dev, pin);

		if ((bridge->class >> 8) == PCI_CLASS_BRIDGE_CARDBUS) {
			/* PC card has the same IRQ as its cardbridge */
			bridge_pin = bridge->pin;
			if (!bridge_pin) {
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "No interrupt pin configured for device %s\n",
						  pci_name(bridge)));
				return NULL;
			}
			pin = bridge_pin;
		}

		entry = acpi_pci_irq_find_prt_entry(bridge, pin);
		if (entry) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					 "Derived GSI for %s INT %c from %s\n",
					 pci_name(dev), pin_name(orig_pin),
					 pci_name(bridge)));
			return entry;
		}

		dev = bridge;
		bridge = dev->bus->self;
	}

	dev_warn(&dev->dev, "can't derive routing for PCI INT %c\n",
		 pin_name(orig_pin));
	return NULL;
}

int acpi_pci_irq_enable(struct pci_dev *dev)
{
	struct acpi_prt_entry *entry;
	int gsi;
	u8 pin;
	int triggering = ACPI_LEVEL_SENSITIVE;
	int polarity = ACPI_ACTIVE_LOW;
	char *link = NULL;
	char link_desc[16];
	int rc;

	pin = dev->pin;
	if (!pin) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No interrupt pin configured for device %s\n",
				  pci_name(dev)));
		return 0;
	}

	entry = acpi_pci_irq_lookup(dev, pin);
	if (!entry) {
		/*
		 * IDE legacy mode controller IRQs are magic. Why do compat
		 * extensions always make such a nasty mess.
		 */
		if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE &&
				(dev->class & 0x05) == 0)
			return 0;
	}

	if (entry) {
		if (entry->link)
			gsi = acpi_pci_link_allocate_irq(entry->link,
							 entry->index,
							 &triggering, &polarity,
							 &link);
		else
			gsi = entry->index;
	} else
		gsi = -1;

	/*
	 * No IRQ known to the ACPI subsystem - maybe the BIOS / 
	 * driver reported one, then use it. Exit in any case.
	 */
	if (gsi < 0) {
		dev_warn(&dev->dev, "PCI INT %c: no GSI", pin_name(pin));
		/* Interrupt Line values above 0xF are forbidden */
		if (dev->irq > 0 && (dev->irq <= 0xF)) {
			printk(" - using IRQ %d\n", dev->irq);
			acpi_register_gsi(&dev->dev, dev->irq,
					  ACPI_LEVEL_SENSITIVE,
					  ACPI_ACTIVE_LOW);
			return 0;
		} else {
			printk("\n");
			return 0;
		}
	}

	rc = acpi_register_gsi(&dev->dev, gsi, triggering, polarity);
	if (rc < 0) {
		dev_warn(&dev->dev, "PCI INT %c: failed to register GSI\n",
			 pin_name(pin));
		return rc;
	}
	dev->irq = rc;

	if (link)
		snprintf(link_desc, sizeof(link_desc), " -> Link[%s]", link);
	else
		link_desc[0] = '\0';

	dev_info(&dev->dev, "PCI INT %c%s -> GSI %u (%s, %s) -> IRQ %d\n",
		 pin_name(pin), link_desc, gsi,
		 (triggering == ACPI_LEVEL_SENSITIVE) ? "level" : "edge",
		 (polarity == ACPI_ACTIVE_LOW) ? "low" : "high", dev->irq);

	return 0;
}

/* FIXME: implement x86/x86_64 version */
void __attribute__ ((weak)) acpi_unregister_gsi(u32 i)
{
}

void acpi_pci_irq_disable(struct pci_dev *dev)
{
	struct acpi_prt_entry *entry;
	int gsi;
	u8 pin;

	pin = dev->pin;
	if (!pin)
		return;

	entry = acpi_pci_irq_lookup(dev, pin);
	if (!entry)
		return;

	if (entry->link)
		gsi = acpi_pci_link_free_irq(entry->link);
	else
		gsi = entry->index;

	/*
	 * TBD: It might be worth clearing dev->irq by magic constant
	 * (e.g. PCI_UNDEFINED_IRQ).
	 */

	dev_info(&dev->dev, "PCI INT %c disabled\n", pin_name(pin));
	acpi_unregister_gsi(gsi);
}
