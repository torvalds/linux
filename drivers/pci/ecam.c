/*
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-ecam.h>
#include <linux/slab.h>

/*
 * On 64-bit systems, we do a single ioremap for the whole config space
 * since we have enough virtual address range available.  On 32-bit, we
 * ioremap the config space for each bus individually.
 */
static const bool per_bus_mapping = !IS_ENABLED(CONFIG_64BIT);

/*
 * Create a PCI config space window
 *  - reserve mem region
 *  - alloc struct pci_config_window with space for all mappings
 *  - ioremap the config space
 */
struct pci_config_window *pci_ecam_create(struct device *dev,
		struct resource *cfgres, struct resource *busr,
		struct pci_ecam_ops *ops)
{
	struct pci_config_window *cfg;
	unsigned int bus_range, bus_range_max, bsz;
	struct resource *conflict;
	int i, err;

	if (busr->start > busr->end)
		return ERR_PTR(-EINVAL);

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	cfg->parent = dev;
	cfg->ops = ops;
	cfg->busr.start = busr->start;
	cfg->busr.end = busr->end;
	cfg->busr.flags = IORESOURCE_BUS;
	bus_range = resource_size(&cfg->busr);
	bus_range_max = resource_size(cfgres) >> ops->bus_shift;
	if (bus_range > bus_range_max) {
		bus_range = bus_range_max;
		cfg->busr.end = busr->start + bus_range - 1;
		dev_warn(dev, "ECAM area %pR can only accommodate %pR (reduced from %pR desired)\n",
			 cfgres, &cfg->busr, busr);
	}
	bsz = 1 << ops->bus_shift;

	cfg->res.start = cfgres->start;
	cfg->res.end = cfgres->end;
	cfg->res.flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	cfg->res.name = "PCI ECAM";

	conflict = request_resource_conflict(&iomem_resource, &cfg->res);
	if (conflict) {
		err = -EBUSY;
		dev_err(dev, "can't claim ECAM area %pR: address conflict with %s %pR\n",
			&cfg->res, conflict->name, conflict);
		goto err_exit;
	}

	if (per_bus_mapping) {
		cfg->winp = kcalloc(bus_range, sizeof(*cfg->winp), GFP_KERNEL);
		if (!cfg->winp)
			goto err_exit_malloc;
		for (i = 0; i < bus_range; i++) {
			cfg->winp[i] = ioremap(cfgres->start + i * bsz, bsz);
			if (!cfg->winp[i])
				goto err_exit_iomap;
		}
	} else {
		cfg->win = ioremap(cfgres->start, bus_range * bsz);
		if (!cfg->win)
			goto err_exit_iomap;
	}

	if (ops->init) {
		err = ops->init(cfg);
		if (err)
			goto err_exit;
	}
	dev_info(dev, "ECAM at %pR for %pR\n", &cfg->res, &cfg->busr);
	return cfg;

err_exit_iomap:
	dev_err(dev, "ECAM ioremap failed\n");
err_exit_malloc:
	err = -ENOMEM;
err_exit:
	pci_ecam_free(cfg);
	return ERR_PTR(err);
}

void pci_ecam_free(struct pci_config_window *cfg)
{
	int i;

	if (per_bus_mapping) {
		if (cfg->winp) {
			for (i = 0; i < resource_size(&cfg->busr); i++)
				if (cfg->winp[i])
					iounmap(cfg->winp[i]);
			kfree(cfg->winp);
		}
	} else {
		if (cfg->win)
			iounmap(cfg->win);
	}
	if (cfg->res.parent)
		release_resource(&cfg->res);
	kfree(cfg);
}

/*
 * Function to implement the pci_ops ->map_bus method
 */
void __iomem *pci_ecam_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	unsigned int devfn_shift = cfg->ops->bus_shift - 8;
	unsigned int busn = bus->number;
	void __iomem *base;

	if (busn < cfg->busr.start || busn > cfg->busr.end)
		return NULL;

	busn -= cfg->busr.start;
	if (per_bus_mapping)
		base = cfg->winp[busn];
	else
		base = cfg->win + (busn << cfg->ops->bus_shift);
	return base + (devfn << devfn_shift) + where;
}

/* ECAM ops */
struct pci_ecam_ops pci_generic_ecam_ops = {
	.bus_shift	= 20,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
/* ECAM ops for 32-bit access only (non-compliant) */
struct pci_ecam_ops pci_32b_ops = {
	.bus_shift	= 20,
	.pci_ops	= {
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read32,
		.write		= pci_generic_config_write32,
	}
};
#endif
