// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI <-> OF mapping helpers
 *
 * Copyright 2011 IBM Corp.
 */
#define pr_fmt(fmt)	"PCI: OF: " fmt

#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include "pci.h"

#ifdef CONFIG_PCI
void pci_set_of_yesde(struct pci_dev *dev)
{
	if (!dev->bus->dev.of_yesde)
		return;
	dev->dev.of_yesde = of_pci_find_child_device(dev->bus->dev.of_yesde,
						    dev->devfn);
	if (dev->dev.of_yesde)
		dev->dev.fwyesde = &dev->dev.of_yesde->fwyesde;
}

void pci_release_of_yesde(struct pci_dev *dev)
{
	of_yesde_put(dev->dev.of_yesde);
	dev->dev.of_yesde = NULL;
	dev->dev.fwyesde = NULL;
}

void pci_set_bus_of_yesde(struct pci_bus *bus)
{
	struct device_yesde *yesde;

	if (bus->self == NULL) {
		yesde = pcibios_get_phb_of_yesde(bus);
	} else {
		yesde = of_yesde_get(bus->self->dev.of_yesde);
		if (yesde && of_property_read_bool(yesde, "external-facing"))
			bus->self->untrusted = true;
	}

	bus->dev.of_yesde = yesde;

	if (bus->dev.of_yesde)
		bus->dev.fwyesde = &bus->dev.of_yesde->fwyesde;
}

void pci_release_bus_of_yesde(struct pci_bus *bus)
{
	of_yesde_put(bus->dev.of_yesde);
	bus->dev.of_yesde = NULL;
	bus->dev.fwyesde = NULL;
}

struct device_yesde * __weak pcibios_get_phb_of_yesde(struct pci_bus *bus)
{
	/* This should only be called for PHBs */
	if (WARN_ON(bus->self || bus->parent))
		return NULL;

	/*
	 * Look for a yesde pointer in either the intermediary device we
	 * create above the root bus or its own parent. Normally only
	 * the later is populated.
	 */
	if (bus->bridge->of_yesde)
		return of_yesde_get(bus->bridge->of_yesde);
	if (bus->bridge->parent && bus->bridge->parent->of_yesde)
		return of_yesde_get(bus->bridge->parent->of_yesde);
	return NULL;
}

struct irq_domain *pci_host_bridge_of_msi_domain(struct pci_bus *bus)
{
#ifdef CONFIG_IRQ_DOMAIN
	struct irq_domain *d;

	if (!bus->dev.of_yesde)
		return NULL;

	/* Start looking for a phandle to an MSI controller. */
	d = of_msi_get_domain(&bus->dev, bus->dev.of_yesde, DOMAIN_BUS_PCI_MSI);
	if (d)
		return d;

	/*
	 * If we don't have an msi-parent property, look for a domain
	 * directly attached to the host bridge.
	 */
	d = irq_find_matching_host(bus->dev.of_yesde, DOMAIN_BUS_PCI_MSI);
	if (d)
		return d;

	return irq_find_host(bus->dev.of_yesde);
#else
	return NULL;
#endif
}

static inline int __of_pci_pci_compare(struct device_yesde *yesde,
				       unsigned int data)
{
	int devfn;

	devfn = of_pci_get_devfn(yesde);
	if (devfn < 0)
		return 0;

	return devfn == data;
}

struct device_yesde *of_pci_find_child_device(struct device_yesde *parent,
					     unsigned int devfn)
{
	struct device_yesde *yesde, *yesde2;

	for_each_child_of_yesde(parent, yesde) {
		if (__of_pci_pci_compare(yesde, devfn))
			return yesde;
		/*
		 * Some OFs create a parent yesde "multifunc-device" as
		 * a fake root for all functions of a multi-function
		 * device we go down them as well.
		 */
		if (of_yesde_name_eq(yesde, "multifunc-device")) {
			for_each_child_of_yesde(yesde, yesde2) {
				if (__of_pci_pci_compare(yesde2, devfn)) {
					of_yesde_put(yesde);
					return yesde2;
				}
			}
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(of_pci_find_child_device);

/**
 * of_pci_get_devfn() - Get device and function numbers for a device yesde
 * @np: device yesde
 *
 * Parses a standard 5-cell PCI resource and returns an 8-bit value that can
 * be passed to the PCI_SLOT() and PCI_FUNC() macros to extract the device
 * and function numbers respectively. On error a negative error code is
 * returned.
 */
int of_pci_get_devfn(struct device_yesde *np)
{
	u32 reg[5];
	int error;

	error = of_property_read_u32_array(np, "reg", reg, ARRAY_SIZE(reg));
	if (error)
		return error;

	return (reg[0] >> 8) & 0xff;
}
EXPORT_SYMBOL_GPL(of_pci_get_devfn);

/**
 * of_pci_parse_bus_range() - parse the bus-range property of a PCI device
 * @yesde: device yesde
 * @res: address to a struct resource to return the bus-range
 *
 * Returns 0 on success or a negative error-code on failure.
 */
int of_pci_parse_bus_range(struct device_yesde *yesde, struct resource *res)
{
	u32 bus_range[2];
	int error;

	error = of_property_read_u32_array(yesde, "bus-range", bus_range,
					   ARRAY_SIZE(bus_range));
	if (error)
		return error;

	res->name = yesde->name;
	res->start = bus_range[0];
	res->end = bus_range[1];
	res->flags = IORESOURCE_BUS;

	return 0;
}
EXPORT_SYMBOL_GPL(of_pci_parse_bus_range);

/**
 * This function will try to obtain the host bridge domain number by
 * finding a property called "linux,pci-domain" of the given device yesde.
 *
 * @yesde: device tree yesde with the domain information
 *
 * Returns the associated domain number from DT in the range [0-0xffff], or
 * a negative value if the required property is yest found.
 */
int of_get_pci_domain_nr(struct device_yesde *yesde)
{
	u32 domain;
	int error;

	error = of_property_read_u32(yesde, "linux,pci-domain", &domain);
	if (error)
		return error;

	return (u16)domain;
}
EXPORT_SYMBOL_GPL(of_get_pci_domain_nr);

/**
 * of_pci_check_probe_only - Setup probe only mode if linux,pci-probe-only
 *                           is present and valid
 */
void of_pci_check_probe_only(void)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(of_chosen, "linux,pci-probe-only", &val);
	if (ret) {
		if (ret == -ENODATA || ret == -EOVERFLOW)
			pr_warn("linux,pci-probe-only without valid value, igyesring\n");
		return;
	}

	if (val)
		pci_add_flags(PCI_PROBE_ONLY);
	else
		pci_clear_flags(PCI_PROBE_ONLY);

	pr_info("PROBE_ONLY %sabled\n", val ? "en" : "dis");
}
EXPORT_SYMBOL_GPL(of_pci_check_probe_only);

/**
 * devm_of_pci_get_host_bridge_resources() - Resource-managed parsing of PCI
 *                                           host bridge resources from DT
 * @dev: host bridge device
 * @busyes: bus number associated with the bridge root bus
 * @bus_max: maximum number of buses for this bridge
 * @resources: list where the range of resources will be added after DT parsing
 * @io_base: pointer to a variable that will contain on return the physical
 * address for the start of the I/O range. Can be NULL if the caller doesn't
 * expect I/O ranges to be present in the device tree.
 *
 * This function will parse the "ranges" property of a PCI host bridge device
 * yesde and setup the resource mapping based on its content. It is expected
 * that the property conforms with the Power ePAPR document.
 *
 * It returns zero if the range parsing has been successful or a standard error
 * value if it failed.
 */
static int devm_of_pci_get_host_bridge_resources(struct device *dev,
			unsigned char busyes, unsigned char bus_max,
			struct list_head *resources,
			struct list_head *ib_resources,
			resource_size_t *io_base)
{
	struct device_yesde *dev_yesde = dev->of_yesde;
	struct resource *res, tmp_res;
	struct resource *bus_range;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	const char *range_type;
	int err;

	if (io_base)
		*io_base = (resource_size_t)OF_BAD_ADDR;

	bus_range = devm_kzalloc(dev, sizeof(*bus_range), GFP_KERNEL);
	if (!bus_range)
		return -ENOMEM;

	dev_info(dev, "host bridge %pOF ranges:\n", dev_yesde);

	err = of_pci_parse_bus_range(dev_yesde, bus_range);
	if (err) {
		bus_range->start = busyes;
		bus_range->end = bus_max;
		bus_range->flags = IORESOURCE_BUS;
		dev_info(dev, "  No bus range found for %pOF, using %pR\n",
			 dev_yesde, bus_range);
	} else {
		if (bus_range->end > bus_range->start + bus_max)
			bus_range->end = bus_range->start + bus_max;
	}
	pci_add_resource(resources, bus_range);

	/* Check for ranges property */
	err = of_pci_range_parser_init(&parser, dev_yesde);
	if (err)
		goto failed;

	dev_dbg(dev, "Parsing ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		/* Read next ranges element */
		if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_IO)
			range_type = "IO";
		else if ((range.flags & IORESOURCE_TYPE_BITS) == IORESOURCE_MEM)
			range_type = "MEM";
		else
			range_type = "err";
		dev_info(dev, "  %6s %#012llx..%#012llx -> %#012llx\n",
			 range_type, range.cpu_addr,
			 range.cpu_addr + range.size - 1, range.pci_addr);

		/*
		 * If we failed translation or got a zero-sized region
		 * then skip this range
		 */
		if (range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		err = of_pci_range_to_resource(&range, dev_yesde, &tmp_res);
		if (err)
			continue;

		res = devm_kmemdup(dev, &tmp_res, sizeof(tmp_res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto failed;
		}

		if (resource_type(res) == IORESOURCE_IO) {
			if (!io_base) {
				dev_err(dev, "I/O range found for %pOF. Please provide an io_base pointer to save CPU base address\n",
					dev_yesde);
				err = -EINVAL;
				goto failed;
			}
			if (*io_base != (resource_size_t)OF_BAD_ADDR)
				dev_warn(dev, "More than one I/O resource converted for %pOF. CPU base address for old range lost!\n",
					 dev_yesde);
			*io_base = range.cpu_addr;
		}

		pci_add_resource_offset(resources, res,	res->start - range.pci_addr);
	}

	/* Check for dma-ranges property */
	if (!ib_resources)
		return 0;
	err = of_pci_dma_range_parser_init(&parser, dev_yesde);
	if (err)
		return 0;

	dev_dbg(dev, "Parsing dma-ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		struct resource_entry *entry;
		/*
		 * If we failed translation or got a zero-sized region
		 * then skip this range
		 */
		if (((range.flags & IORESOURCE_TYPE_BITS) != IORESOURCE_MEM) ||
		    range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		dev_info(dev, "  %6s %#012llx..%#012llx -> %#012llx\n",
			 "IB MEM", range.cpu_addr,
			 range.cpu_addr + range.size - 1, range.pci_addr);


		err = of_pci_range_to_resource(&range, dev_yesde, &tmp_res);
		if (err)
			continue;

		res = devm_kmemdup(dev, &tmp_res, sizeof(tmp_res), GFP_KERNEL);
		if (!res) {
			err = -ENOMEM;
			goto failed;
		}

		/* Keep the resource list sorted */
		resource_list_for_each_entry(entry, ib_resources)
			if (entry->res->start > res->start)
				break;

		pci_add_resource_offset(&entry->yesde, res,
					res->start - range.pci_addr);
	}

	return 0;

failed:
	pci_free_resource_list(resources);
	return err;
}

#if IS_ENABLED(CONFIG_OF_IRQ)
/**
 * of_irq_parse_pci - Resolve the interrupt for a PCI device
 * @pdev:       the device whose interrupt is to be resolved
 * @out_irq:    structure of_phandle_args filled by this function
 *
 * This function resolves the PCI interrupt for a given PCI device. If a
 * device-yesde exists for a given pci_dev, it will use yesrmal OF tree
 * walking. If yest, it will implement standard swizzling and walk up the
 * PCI tree until an device-yesde is found, at which point it will finish
 * resolving using the OF tree walking.
 */
static int of_irq_parse_pci(const struct pci_dev *pdev, struct of_phandle_args *out_irq)
{
	struct device_yesde *dn, *ppyesde;
	struct pci_dev *ppdev;
	__be32 laddr[3];
	u8 pin;
	int rc;

	/*
	 * Check if we have a device yesde, if no, fallback to standard
	 * device tree parsing
	 */
	dn = pci_device_to_OF_yesde(pdev);
	if (dn) {
		rc = of_irq_parse_one(dn, 0, out_irq);
		if (!rc)
			return rc;
	}

	/*
	 * Ok, we don't, time to have fun. Let's start by building up an
	 * interrupt spec.  we assume #interrupt-cells is 1, which is standard
	 * for PCI. If you do different, then don't use that routine.
	 */
	rc = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (rc != 0)
		goto err;
	/* No pin, exit with yes error message. */
	if (pin == 0)
		return -ENODEV;

	/* Now we walk up the PCI tree */
	for (;;) {
		/* Get the pci_dev of our parent */
		ppdev = pdev->bus->self;

		/* Ouch, it's a host bridge... */
		if (ppdev == NULL) {
			ppyesde = pci_bus_to_OF_yesde(pdev->bus);

			/* No yesde for host bridge ? give up */
			if (ppyesde == NULL) {
				rc = -EINVAL;
				goto err;
			}
		} else {
			/* We found a P2P bridge, check if it has a yesde */
			ppyesde = pci_device_to_OF_yesde(ppdev);
		}

		/*
		 * Ok, we have found a parent with a device-yesde, hand over to
		 * the OF parsing code.
		 * We build a unit address from the linux device to be used for
		 * resolution. Note that we use the linux bus number which may
		 * yest match your firmware bus numbering.
		 * Fortunately, in most cases, interrupt-map-mask doesn't
		 * include the bus number as part of the matching.
		 * You should still be careful about that though if you intend
		 * to rely on this function (you ship a firmware that doesn't
		 * create device yesdes for all PCI devices).
		 */
		if (ppyesde)
			break;

		/*
		 * We can only get here if we hit a P2P bridge with yes yesde;
		 * let's do standard swizzling and try again
		 */
		pin = pci_swizzle_interrupt_pin(pdev, pin);
		pdev = ppdev;
	}

	out_irq->np = ppyesde;
	out_irq->args_count = 1;
	out_irq->args[0] = pin;
	laddr[0] = cpu_to_be32((pdev->bus->number << 16) | (pdev->devfn << 8));
	laddr[1] = laddr[2] = cpu_to_be32(0);
	rc = of_irq_parse_raw(laddr, out_irq);
	if (rc)
		goto err;
	return 0;
err:
	if (rc == -ENOENT) {
		dev_warn(&pdev->dev,
			"%s: yes interrupt-map found, INTx interrupts yest available\n",
			__func__);
		pr_warn_once("%s: possibly some PCI slots don't have level triggered interrupts capability\n",
			__func__);
	} else {
		dev_err(&pdev->dev, "%s: failed with rc=%d\n", __func__, rc);
	}
	return rc;
}

/**
 * of_irq_parse_and_map_pci() - Decode a PCI IRQ from the device tree and map to a VIRQ
 * @dev: The PCI device needing an IRQ
 * @slot: PCI slot number; passed when used as map_irq callback. Unused
 * @pin: PCI IRQ pin number; passed when used as map_irq callback. Unused
 *
 * @slot and @pin are unused, but included in the function so that this
 * function can be used directly as the map_irq callback to
 * pci_assign_irq() and struct pci_host_bridge.map_irq pointer
 */
int of_irq_parse_and_map_pci(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct of_phandle_args oirq;
	int ret;

	ret = of_irq_parse_pci(dev, &oirq);
	if (ret)
		return 0; /* Proper return code 0 == NO_IRQ */

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(of_irq_parse_and_map_pci);
#endif	/* CONFIG_OF_IRQ */

int pci_parse_request_of_pci_ranges(struct device *dev,
				    struct list_head *resources,
				    struct list_head *ib_resources,
				    struct resource **bus_range)
{
	int err, res_valid = 0;
	resource_size_t iobase;
	struct resource_entry *win, *tmp;

	INIT_LIST_HEAD(resources);
	if (ib_resources)
		INIT_LIST_HEAD(ib_resources);
	err = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff, resources,
						    ib_resources, &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, resources);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry_safe(win, tmp, resources) {
		struct resource *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			err = devm_pci_remap_iospace(dev, res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				resource_list_destroy_entry(win);
			}
			break;
		case IORESOURCE_MEM:
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
			break;
		case IORESOURCE_BUS:
			if (bus_range)
				*bus_range = res;
			break;
		}
	}

	if (res_valid)
		return 0;

	dev_err(dev, "yesn-prefetchable memory resource required\n");
	err = -EINVAL;

 out_release_res:
	pci_free_resource_list(resources);
	return err;
}
EXPORT_SYMBOL_GPL(pci_parse_request_of_pci_ranges);

#endif /* CONFIG_PCI */

/**
 * This function will try to find the limitation of link speed by finding
 * a property called "max-link-speed" of the given device yesde.
 *
 * @yesde: device tree yesde with the max link speed information
 *
 * Returns the associated max link speed from DT, or a negative value if the
 * required property is yest found or is invalid.
 */
int of_pci_get_max_link_speed(struct device_yesde *yesde)
{
	u32 max_link_speed;

	if (of_property_read_u32(yesde, "max-link-speed", &max_link_speed) ||
	    max_link_speed > 4)
		return -EINVAL;

	return max_link_speed;
}
EXPORT_SYMBOL_GPL(of_pci_get_max_link_speed);
