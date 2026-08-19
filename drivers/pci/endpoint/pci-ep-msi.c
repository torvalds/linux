// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Endpoint *Controller* (EPC) MSI library
 *
 * Copyright (C) 2025 NXP
 * Author: Frank Li <Frank.Li@nxp.com>
 */

#include <linux/align.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/interrupt.h>
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

static int pci_epf_alloc_doorbell_embedded(struct pci_epf *epf, u16 num_db)
{
	const struct pci_epc_aux_resource *doorbell = NULL;
	struct pci_epf_doorbell_msg *msg;
	struct pci_epc *epc = epf->epc;
	size_t map_size = 0, off = 0;
	dma_addr_t iova_base = 0;
	phys_addr_t phys_base;
	int count, ret, i;
	u64 addr;

	count = pci_epc_get_aux_resources_count(epc, epf->func_no,
						epf->vfunc_no);
	if (count < 0)
		return count;
	if (!count)
		return -ENODEV;

	struct pci_epc_aux_resource *res __free(kfree) =
				kcalloc(count, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = pci_epc_get_aux_resources(epc, epf->func_no, epf->vfunc_no,
					res, count);
	if (ret)
		return ret;

	/* TODO: Support multiple DOORBELL_MMIO resources per EPC. */
	for (i = 0; i < count; i++) {
		if (res[i].type != PCI_EPC_AUX_DOORBELL_MMIO)
			continue;

		doorbell = &res[i];
		break;
	}
	if (!doorbell)
		return -ENODEV;
	addr = doorbell->phys_addr;
	if (!IS_ALIGNED(addr, sizeof(u32)))
		return -EINVAL;

	/*
	 * Reuse the pre-exposed BAR window if available. Otherwise map the MMIO
	 * doorbell resource here. Any required IOMMU mapping is handled
	 * internally, matching the MSI doorbell semantics.
	 */
	if (doorbell->bar == NO_BAR) {
		phys_base = addr & PAGE_MASK;
		off = addr - phys_base;
		map_size = PAGE_ALIGN(off + sizeof(u32));

		iova_base = dma_map_resource(epc->dev.parent, phys_base,
					     map_size, DMA_FROM_DEVICE, 0);
		if (dma_mapping_error(epc->dev.parent, iova_base))
			return -EIO;

		addr = iova_base + off;
	}

	msg = kcalloc(num_db, sizeof(*msg), GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/*
	 * Embedded doorbell backends (e.g. DesignWare eDMA interrupt emulation)
	 * typically provide a single IRQ and do not offer per-doorbell
	 * distinguishable address/data pairs. The EPC aux resource therefore
	 * exposes one DOORBELL_MMIO entry (u.db_mmio.irq).
	 *
	 * Still, pci_epf_alloc_doorbell() allows requesting multiple doorbells.
	 * For such backends we replicate the same address/data for each entry
	 * and mark the IRQ as shared (IRQF_SHARED). Consumers must treat them
	 * as equivalent "kick" doorbells.
	 */
	for (i = 0; i < num_db; i++)
		msg[i] = (struct pci_epf_doorbell_msg) {
			.msg.address_lo = (u32)addr,
			.msg.address_hi = (u32)(addr >> 32),
			.msg.data = doorbell->u.db_mmio.data,
			.virq = doorbell->u.db_mmio.irq,
			.irq_flags = IRQF_SHARED,
			.type = PCI_EPF_DOORBELL_EMBEDDED,
			.bar = doorbell->bar,
			.offset = (doorbell->bar == NO_BAR) ? 0 :
				  doorbell->bar_offset,
			.iova_base = iova_base,
			.iova_size = map_size,
		};

	epf->num_db = num_db;
	epf->db_msg = msg;
	return 0;

err_unmap:
	if (map_size)
		dma_unmap_resource(epc->dev.parent, iova_base, map_size,
				   DMA_FROM_DEVICE, 0);
	return ret;
}

static int pci_epf_alloc_doorbell_msi(struct pci_epf *epf, u16 num_db)
{
	struct pci_epf_doorbell_msg *msg;
	struct device *dev = &epf->dev;
	struct pci_epc *epc = epf->epc;
	struct irq_domain *domain;
	int ret, i;

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

	msg = kzalloc_objs(struct pci_epf_doorbell_msg, num_db);
	if (!msg)
		return -ENOMEM;

	for (i = 0; i < num_db; i++)
		msg[i] = (struct pci_epf_doorbell_msg) {
			.type = PCI_EPF_DOORBELL_MSI,
			.bar = NO_BAR,
		};

	epf->num_db = num_db;
	epf->db_msg = msg;

	ret = platform_device_msi_init_and_alloc_irqs(epc->dev.parent, num_db,
						      pci_epf_write_msi_msg);
	if (ret) {
		dev_err(dev, "Failed to allocate MSI\n");
		kfree(msg);
		epf->db_msg = NULL;
		epf->num_db = 0;
		return ret;
	}

	for (i = 0; i < num_db; i++)
		epf->db_msg[i].virq = msi_get_virq(epc->dev.parent, i);

	return 0;
}

int pci_epf_alloc_doorbell(struct pci_epf *epf, u16 num_db)
{
	struct pci_epc *epc = epf->epc;
	struct device *dev = &epf->dev;
	int ret;

	/* TODO: Multi-EPF support */
	if (list_first_entry_or_null(&epc->pci_epf, struct pci_epf, list) != epf) {
		dev_err(dev, "Doorbell doesn't support multiple EPF\n");
		return -EINVAL;
	}

	if (epf->db_msg)
		return -EBUSY;

	ret = pci_epf_alloc_doorbell_msi(epf, num_db);
	if (!ret)
		return 0;

	/*
	 * Fall back to embedded doorbell only when platform MSI is unavailable
	 * for this EPC.
	 */
	if (ret != -ENODEV)
		return ret;

	ret = pci_epf_alloc_doorbell_embedded(epf, num_db);
	if (ret) {
		dev_err(dev, "Failed to allocate doorbell: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Using embedded (DMA) doorbell fallback\n");
	return 0;
}
EXPORT_SYMBOL_GPL(pci_epf_alloc_doorbell);

void pci_epf_free_doorbell(struct pci_epf *epf)
{
	struct pci_epf_doorbell_msg *msg0;
	struct pci_epc *epc = epf->epc;

	if (!epf->db_msg)
		return;

	msg0 = &epf->db_msg[0];
	if (msg0->type == PCI_EPF_DOORBELL_MSI)
		platform_device_msi_free_irqs_all(epf->epc->dev.parent);
	else if (msg0->type == PCI_EPF_DOORBELL_EMBEDDED && msg0->iova_size)
		dma_unmap_resource(epc->dev.parent, msg0->iova_base,
				   msg0->iova_size, DMA_FROM_DEVICE, 0);

	kfree(epf->db_msg);
	epf->db_msg = NULL;
	epf->num_db = 0;
}
EXPORT_SYMBOL_GPL(pci_epf_free_doorbell);
