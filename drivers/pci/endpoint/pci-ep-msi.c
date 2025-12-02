// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Endpoint *Controller* (EPC) MSI library
 *
 * Copyright (C) 2025 NXP
 * Author: Frank Li <Frank.Li@nxp.com>
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci-ep-cfs.h>
#include <linux/pci-ep-msi.h>
#include <linux/slab.h>

static void pci_epf_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct pci_epc *epc;
	struct pci_epf *epf;

	epc = pci_epc_get(dev_name(msi_desc_to_dev(desc)));
	if (IS_ERR(epc))
		return;

	epf = list_first_entry_or_null(&epc->pci_epf, struct pci_epf, list);

	if (epf && epf->db_msg && desc->msi_index < epf->num_db)
		memcpy(&epf->db_msg[desc->msi_index].msg, msg, sizeof(*msg));

	pci_epc_put(epc);
}

int pci_epf_alloc_doorbell(struct pci_epf *epf, u16 num_db)
{
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	struct irq_domain *domain;
	void *msg;
	int ret;
	int i;

	/* TODO: Multi-EPF support */
	if (list_first_entry_or_null(&epc->pci_epf, struct pci_epf, list) != epf) {
		dev_err(dev, "MSI doorbell doesn't support multiple EPF\n");
		return -EINVAL;
	}

	domain = of_msi_map_get_device_domain(epc->dev.parent, 0,
					      DOMAIN_BUS_PLATFORM_MSI);
	if (!domain) {
		dev_err(dev, "Can't find MSI domain for EPC\n");
		return -ENODEV;
	}

	if (!irq_domain_is_msi_parent(domain))
		return -ENODEV;

	if (!irq_domain_is_msi_immutable(domain)) {
		dev_err(dev, "Mutable MSI controller not supported\n");
		return -ENODEV;
	}

	dev_set_msi_domain(epc->dev.parent, domain);

	msg = kcalloc(num_db, sizeof(struct pci_epf_doorbell_msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	epf->num_db = num_db;
	epf->db_msg = msg;

	ret = platform_device_msi_init_and_alloc_irqs(epc->dev.parent, num_db,
						      pci_epf_write_msi_msg);
	if (ret) {
		dev_err(dev, "Failed to allocate MSI\n");
		kfree(msg);
		return ret;
	}

	for (i = 0; i < num_db; i++)
		epf->db_msg[i].virq = msi_get_virq(epc->dev.parent, i);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_epf_alloc_doorbell);

void pci_epf_free_doorbell(struct pci_epf *epf)
{
	platform_device_msi_free_irqs_all(epf->epc->dev.parent);

	kfree(epf->db_msg);
	epf->db_msg = NULL;
	epf->num_db = 0;
}
EXPORT_SYMBOL_GPL(pci_epf_free_doorbell);
