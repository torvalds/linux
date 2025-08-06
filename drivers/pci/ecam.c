// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2016 Broadcom
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
		const struct pci_ecam_ops *ops)
{
	unsigned int bus_shift = ops->bus_shift;
	struct pci_config_window *cfg;
	unsigned int bus_range, bus_range_max, bsz;
	struct resource *conflict;
	int err;

	if (busr->start > busr->end)
		return ERR_PTR(-EINVAL);

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	/* ECAM-compliant platforms need not supply ops->bus_shift */
	if (!bus_shift)
		bus_shift = PCIE_ECAM_BUS_SHIFT;

	cfg->parent = dev;
	cfg->ops = ops;
	cfg->busr.start = busr->start;
	cfg->busr.end = busr->end;
	cfg->busr.flags = IORESOURCE_BUS;
	cfg->bus_shift = bus_shift;
	bus_range = resource_size(&cfg->busr);
	bus_range_max = resource_size(cfgres) >> bus_shift;
	if (bus_range > bus_range_max) {
		bus_range = bus_range_max;
		resource_set_size(&cfg->busr, bus_range);
		dev_warn(dev, "ECAM area %pR can only accommodate %pR (reduced from %pR desired)\n",
			 cfgres, &cfg->busr, busr);
	}
	bsz = 1 << bus_shift;

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
	} else {
		cfg->win = pci_remap_cfgspace(cfgres->start, bus_range * bsz);
		if (!cfg->win)
			goto err_exit_iomap;
	}

	cfg->priv = dev_get_drvdata(dev);

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
EXPORT_SYMBOL_GPL(pci_ecam_create);

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
EXPORT_SYMBOL_GPL(pci_ecam_free);

static int pci_ecam_add_bus(struct pci_bus *bus)
{
	struct pci_config_window *cfg = bus->sysdata;
	unsigned int bsz = 1 << cfg->bus_shift;
	unsigned int busn = bus->number;
	phys_addr_t start;

	if (!per_bus_mapping)
		return 0;

	if (busn < cfg->busr.start || busn > cfg->busr.end)
		return -EINVAL;

	busn -= cfg->busr.start;
	start = cfg->res.start + busn * bsz;

	cfg->winp[busn] = pci_remap_cfgspace(start, bsz);
	if (!cfg->winp[busn])
		return -ENOMEM;

	return 0;
}

static void pci_ecam_remove_bus(struct pci_bus *bus)
{
	struct pci_config_window *cfg = bus->sysdata;
	unsigned int busn = bus->number;

	if (!per_bus_mapping || busn < cfg->busr.start || busn > cfg->busr.end)
		return;

	busn -= cfg->busr.start;
	if (cfg->winp[busn]) {
		iounmap(cfg->winp[busn]);
		cfg->winp[busn] = NULL;
	}
}

/*
 * Function to implement the pci_ops ->map_bus method
 */
void __iomem *pci_ecam_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where)
{
	struct pci_config_window *cfg = bus->sysdata;
	unsigned int bus_shift = cfg->ops->bus_shift;
	unsigned int devfn_shift = cfg->ops->bus_shift - 8;
	unsigned int busn = bus->number;
	void __iomem *base;
	u32 bus_offset, devfn_offset;

	if (busn < cfg->busr.start || busn > cfg->busr.end)
		return NULL;

	busn -= cfg->busr.start;
	if (per_bus_mapping) {
		base = cfg->winp[busn];
		busn = 0;
	} else
		base = cfg->win;

	if (cfg->ops->bus_shift) {
		bus_offset = (busn & PCIE_ECAM_BUS_MASK) << bus_shift;
		devfn_offset = (devfn & PCIE_ECAM_DEVFN_MASK) << devfn_shift;
		where &= PCIE_ECAM_REG_MASK;

		return base + (bus_offset | devfn_offset | where);
	}

	return base + PCIE_ECAM_OFFSET(busn, devfn, where);
}
EXPORT_SYMBOL_GPL(pci_ecam_map_bus);

/* ECAM ops */
const struct pci_ecam_ops pci_generic_ecam_ops = {
	.pci_ops	= {
		.add_bus	= pci_ecam_add_bus,
		.remove_bus	= pci_ecam_remove_bus,
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read,
		.write		= pci_generic_config_write,
	}
};
EXPORT_SYMBOL_GPL(pci_generic_ecam_ops);

#if defined(CONFIG_ACPI) && defined(CONFIG_PCI_QUIRKS)
/* ECAM ops for 32-bit access only (non-compliant) */
const struct pci_ecam_ops pci_32b_ops = {
	.pci_ops	= {
		.add_bus	= pci_ecam_add_bus,
		.remove_bus	= pci_ecam_remove_bus,
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read32,
		.write		= pci_generic_config_write32,
	}
};

/* ECAM ops for 32-bit read only (non-compliant) */
const struct pci_ecam_ops pci_32b_read_ops = {
	.pci_ops	= {
		.add_bus	= pci_ecam_add_bus,
		.remove_bus	= pci_ecam_remove_bus,
		.map_bus	= pci_ecam_map_bus,
		.read		= pci_generic_config_read32,
		.write		= pci_generic_config_write,
	}
};
#endif
