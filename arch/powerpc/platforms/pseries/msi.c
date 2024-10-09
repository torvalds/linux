// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006 Jake Moilanen <moilanen@austin.ibm.com>, IBM Corp.
 * Copyright 2006-2007 Michael Ellerman, IBM Corp.
 */

#include <linux/crash_dump.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/seq_file.h>

#include <asm/rtas.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>
#include <asm/machdep.h>
#include <asm/xive.h>

#include "pseries.h"

static int query_token, change_token;

#define RTAS_QUERY_FN		0
#define RTAS_CHANGE_FN		1
#define RTAS_RESET_FN		2
#define RTAS_CHANGE_MSI_FN	3
#define RTAS_CHANGE_MSIX_FN	4
#define RTAS_CHANGE_32MSI_FN	5
#define RTAS_CHANGE_32MSIX_FN	6

/* RTAS Helpers */

static int rtas_change_msi(struct pci_dn *pdn, u32 func, u32 num_irqs)
{
	u32 addr, seq_num, rtas_ret[3];
	unsigned long buid;
	int rc;

	addr = rtas_config_addr(pdn->busno, pdn->devfn, 0);
	buid = pdn->phb->buid;

	seq_num = 1;
	do {
		if (func == RTAS_CHANGE_MSI_FN || func == RTAS_CHANGE_MSIX_FN ||
		    func == RTAS_CHANGE_32MSI_FN || func == RTAS_CHANGE_32MSIX_FN)
			rc = rtas_call(change_token, 6, 4, rtas_ret, addr,
					BUID_HI(buid), BUID_LO(buid),
					func, num_irqs, seq_num);
		else
			rc = rtas_call(change_token, 6, 3, rtas_ret, addr,
					BUID_HI(buid), BUID_LO(buid),
					func, num_irqs, seq_num);

		seq_num = rtas_ret[1];
	} while (rtas_busy_delay(rc));

	/*
	 * If the RTAS call succeeded, return the number of irqs allocated.
	 * If not, make sure we return a negative error code.
	 */
	if (rc == 0)
		rc = rtas_ret[0];
	else if (rc > 0)
		rc = -rc;

	pr_debug("rtas_msi: ibm,change_msi(func=%d,num=%d), got %d rc = %d\n",
		 func, num_irqs, rtas_ret[0], rc);

	return rc;
}

static void rtas_disable_msi(struct pci_dev *pdev)
{
	struct pci_dn *pdn;

	pdn = pci_get_pdn(pdev);
	if (!pdn)
		return;

	/*
	 * disabling MSI with the explicit interface also disables MSI-X
	 */
	if (rtas_change_msi(pdn, RTAS_CHANGE_MSI_FN, 0) != 0) {
		/* 
		 * may have failed because explicit interface is not
		 * present
		 */
		if (rtas_change_msi(pdn, RTAS_CHANGE_FN, 0) != 0) {
			pr_debug("rtas_msi: Setting MSIs to 0 failed!\n");
		}
	}
}

static int rtas_query_irq_number(struct pci_dn *pdn, int offset)
{
	u32 addr, rtas_ret[2];
	unsigned long buid;
	int rc;

	addr = rtas_config_addr(pdn->busno, pdn->devfn, 0);
	buid = pdn->phb->buid;

	do {
		rc = rtas_call(query_token, 4, 3, rtas_ret, addr,
			       BUID_HI(buid), BUID_LO(buid), offset);
	} while (rtas_busy_delay(rc));

	if (rc) {
		pr_debug("rtas_msi: error (%d) querying source number\n", rc);
		return rc;
	}

	return rtas_ret[0];
}

static int check_req(struct pci_dev *pdev, int nvec, char *prop_name)
{
	struct device_node *dn;
	const __be32 *p;
	u32 req_msi;

	dn = pci_device_to_OF_node(pdev);

	p = of_get_property(dn, prop_name, NULL);
	if (!p) {
		pr_debug("rtas_msi: No %s on %pOF\n", prop_name, dn);
		return -ENOENT;
	}

	req_msi = be32_to_cpup(p);
	if (req_msi < nvec) {
		pr_debug("rtas_msi: %s requests < %d MSIs\n", prop_name, nvec);

		if (req_msi == 0) /* Be paranoid */
			return -ENOSPC;

		return req_msi;
	}

	return 0;
}

static int check_req_msi(struct pci_dev *pdev, int nvec)
{
	return check_req(pdev, nvec, "ibm,req#msi");
}

static int check_req_msix(struct pci_dev *pdev, int nvec)
{
	return check_req(pdev, nvec, "ibm,req#msi-x");
}

/* Quota calculation */

static struct device_node *__find_pe_total_msi(struct device_node *node, int *total)
{
	struct device_node *dn;
	const __be32 *p;

	dn = of_node_get(node);
	while (dn) {
		p = of_get_property(dn, "ibm,pe-total-#msi", NULL);
		if (p) {
			pr_debug("rtas_msi: found prop on dn %pOF\n",
				dn);
			*total = be32_to_cpup(p);
			return dn;
		}

		dn = of_get_next_parent(dn);
	}

	return NULL;
}

static struct device_node *find_pe_total_msi(struct pci_dev *dev, int *total)
{
	return __find_pe_total_msi(pci_device_to_OF_node(dev), total);
}

static struct device_node *find_pe_dn(struct pci_dev *dev, int *total)
{
	struct device_node *dn;
	struct eeh_dev *edev;

	/* Found our PE and assume 8 at that point. */

	dn = pci_device_to_OF_node(dev);
	if (!dn)
		return NULL;

	/* Get the top level device in the PE */
	edev = pdn_to_eeh_dev(PCI_DN(dn));
	if (edev->pe)
		edev = list_first_entry(&edev->pe->edevs, struct eeh_dev,
					entry);
	dn = pci_device_to_OF_node(edev->pdev);
	if (!dn)
		return NULL;

	/* We actually want the parent */
	dn = of_get_parent(dn);
	if (!dn)
		return NULL;

	/* Hardcode of 8 for old firmwares */
	*total = 8;
	pr_debug("rtas_msi: using PE dn %pOF\n", dn);

	return dn;
}

struct msi_counts {
	struct device_node *requestor;
	int num_devices;
	int request;
	int quota;
	int spare;
	int over_quota;
};

static void *count_non_bridge_devices(struct device_node *dn, void *data)
{
	struct msi_counts *counts = data;
	const __be32 *p;
	u32 class;

	pr_debug("rtas_msi: counting %pOF\n", dn);

	p = of_get_property(dn, "class-code", NULL);
	class = p ? be32_to_cpup(p) : 0;

	if ((class >> 8) != PCI_CLASS_BRIDGE_PCI)
		counts->num_devices++;

	return NULL;
}

static void *count_spare_msis(struct device_node *dn, void *data)
{
	struct msi_counts *counts = data;
	const __be32 *p;
	int req;

	if (dn == counts->requestor)
		req = counts->request;
	else {
		/* We don't know if a driver will try to use MSI or MSI-X,
		 * so we just have to punt and use the larger of the two. */
		req = 0;
		p = of_get_property(dn, "ibm,req#msi", NULL);
		if (p)
			req = be32_to_cpup(p);

		p = of_get_property(dn, "ibm,req#msi-x", NULL);
		if (p)
			req = max(req, (int)be32_to_cpup(p));
	}

	if (req < counts->quota)
		counts->spare += counts->quota - req;
	else if (req > counts->quota)
		counts->over_quota++;

	return NULL;
}

static int msi_quota_for_device(struct pci_dev *dev, int request)
{
	struct device_node *pe_dn;
	struct msi_counts counts;
	int total;

	pr_debug("rtas_msi: calc quota for %s, request %d\n", pci_name(dev),
		  request);

	pe_dn = find_pe_total_msi(dev, &total);
	if (!pe_dn)
		pe_dn = find_pe_dn(dev, &total);

	if (!pe_dn) {
		pr_err("rtas_msi: couldn't find PE for %s\n", pci_name(dev));
		goto out;
	}

	pr_debug("rtas_msi: found PE %pOF\n", pe_dn);

	memset(&counts, 0, sizeof(struct msi_counts));

	/* Work out how many devices we have below this PE */
	pci_traverse_device_nodes(pe_dn, count_non_bridge_devices, &counts);

	if (counts.num_devices == 0) {
		pr_err("rtas_msi: found 0 devices under PE for %s\n",
			pci_name(dev));
		goto out;
	}

	counts.quota = total / counts.num_devices;
	if (request <= counts.quota)
		goto out;

	/* else, we have some more calculating to do */
	counts.requestor = pci_device_to_OF_node(dev);
	counts.request = request;
	pci_traverse_device_nodes(pe_dn, count_spare_msis, &counts);

	/* If the quota isn't an integer multiple of the total, we can
	 * use the remainder as spare MSIs for anyone that wants them. */
	counts.spare += total % counts.num_devices;

	/* Divide any spare by the number of over-quota requestors */
	if (counts.over_quota)
		counts.quota += counts.spare / counts.over_quota;

	/* And finally clamp the request to the possibly adjusted quota */
	request = min(counts.quota, request);

	pr_debug("rtas_msi: request clamped to quota %d\n", request);
out:
	of_node_put(pe_dn);

	return request;
}

static void rtas_hack_32bit_msi_gen2(struct pci_dev *pdev)
{
	u32 addr_hi, addr_lo;

	/*
	 * We should only get in here for IODA1 configs. This is based on the
	 * fact that we using RTAS for MSIs, we don't have the 32 bit MSI RTAS
	 * support, and we are in a PCIe Gen2 slot.
	 */
	dev_info(&pdev->dev,
		 "rtas_msi: No 32 bit MSI firmware support, forcing 32 bit MSI\n");
	pci_read_config_dword(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_HI, &addr_hi);
	addr_lo = 0xffff0000 | ((addr_hi >> (48 - 32)) << 4);
	pci_write_config_dword(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_LO, addr_lo);
	pci_write_config_dword(pdev, pdev->msi_cap + PCI_MSI_ADDRESS_HI, 0);
}

static int rtas_prepare_msi_irqs(struct pci_dev *pdev, int nvec_in, int type,
				 msi_alloc_info_t *arg)
{
	struct pci_dn *pdn;
	int quota, rc;
	int nvec = nvec_in;
	int use_32bit_msi_hack = 0;

	if (type == PCI_CAP_ID_MSIX)
		rc = check_req_msix(pdev, nvec);
	else
		rc = check_req_msi(pdev, nvec);

	if (rc)
		return rc;

	quota = msi_quota_for_device(pdev, nvec);

	if (quota && quota < nvec)
		return quota;

	/*
	 * Firmware currently refuse any non power of two allocation
	 * so we round up if the quota will allow it.
	 */
	if (type == PCI_CAP_ID_MSIX) {
		int m = roundup_pow_of_two(nvec);
		quota = msi_quota_for_device(pdev, m);

		if (quota >= m)
			nvec = m;
	}

	pdn = pci_get_pdn(pdev);

	/*
	 * Try the new more explicit firmware interface, if that fails fall
	 * back to the old interface. The old interface is known to never
	 * return MSI-Xs.
	 */
again:
	if (type == PCI_CAP_ID_MSI) {
		if (pdev->no_64bit_msi) {
			rc = rtas_change_msi(pdn, RTAS_CHANGE_32MSI_FN, nvec);
			if (rc < 0) {
				/*
				 * We only want to run the 32 bit MSI hack below if
				 * the max bus speed is Gen2 speed
				 */
				if (pdev->bus->max_bus_speed != PCIE_SPEED_5_0GT)
					return rc;

				use_32bit_msi_hack = 1;
			}
		} else
			rc = -1;

		if (rc < 0)
			rc = rtas_change_msi(pdn, RTAS_CHANGE_MSI_FN, nvec);

		if (rc < 0) {
			pr_debug("rtas_msi: trying the old firmware call.\n");
			rc = rtas_change_msi(pdn, RTAS_CHANGE_FN, nvec);
		}

		if (use_32bit_msi_hack && rc > 0)
			rtas_hack_32bit_msi_gen2(pdev);
	} else {
		if (pdev->no_64bit_msi)
			rc = rtas_change_msi(pdn, RTAS_CHANGE_32MSIX_FN, nvec);
		else
			rc = rtas_change_msi(pdn, RTAS_CHANGE_MSIX_FN, nvec);
	}

	if (rc != nvec) {
		if (nvec != nvec_in) {
			nvec = nvec_in;
			goto again;
		}
		pr_debug("rtas_msi: rtas_change_msi() failed\n");
		return rc;
	}

	return 0;
}

static int pseries_msi_ops_prepare(struct irq_domain *domain, struct device *dev,
				   int nvec, msi_alloc_info_t *arg)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int type = pdev->msix_enabled ? PCI_CAP_ID_MSIX : PCI_CAP_ID_MSI;

	return rtas_prepare_msi_irqs(pdev, nvec, type, arg);
}

/*
 * ->msi_free() is called before irq_domain_free_irqs_top() when the
 * handler data is still available. Use that to clear the XIVE
 * controller data.
 */
static void pseries_msi_ops_msi_free(struct irq_domain *domain,
				     struct msi_domain_info *info,
				     unsigned int irq)
{
	if (xive_enabled())
		xive_irq_free_data(irq);
}

/*
 * RTAS can not disable one MSI at a time. It's all or nothing. Do it
 * at the end after all IRQs have been freed.
 */
static void pseries_msi_post_free(struct irq_domain *domain, struct device *dev)
{
	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return;

	rtas_disable_msi(to_pci_dev(dev));
}

static struct msi_domain_ops pseries_pci_msi_domain_ops = {
	.msi_prepare	= pseries_msi_ops_prepare,
	.msi_free	= pseries_msi_ops_msi_free,
	.msi_post_free	= pseries_msi_post_free,
};

static void pseries_msi_shutdown(struct irq_data *d)
{
	d = d->parent_data;
	if (d->chip->irq_shutdown)
		d->chip->irq_shutdown(d);
}

static void pseries_msi_mask(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void pseries_msi_unmask(struct irq_data *d)
{
	pci_msi_unmask_irq(d);
	irq_chip_unmask_parent(d);
}

static void pseries_msi_write_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct msi_desc *entry = irq_data_get_msi_desc(data);

	/*
	 * Do not update the MSIx vector table. It's not strictly necessary
	 * because the table is initialized by the underlying hypervisor, PowerVM
	 * or QEMU/KVM. However, if the MSIx vector entry is cleared, any further
	 * activation will fail. This can happen in some drivers (eg. IPR) which
	 * deactivate an IRQ used for testing MSI support.
	 */
	entry->msg = *msg;
}

static struct irq_chip pseries_pci_msi_irq_chip = {
	.name		= "pSeries-PCI-MSI",
	.irq_shutdown	= pseries_msi_shutdown,
	.irq_mask	= pseries_msi_mask,
	.irq_unmask	= pseries_msi_unmask,
	.irq_eoi	= irq_chip_eoi_parent,
	.irq_write_msi_msg	= pseries_msi_write_msg,
};


/*
 * Set MSI_FLAG_MSIX_CONTIGUOUS as there is no way to express to
 * firmware to request a discontiguous or non-zero based range of
 * MSI-X entries. Core code will reject such setup attempts.
 */
static struct msi_domain_info pseries_msi_domain_info = {
	.flags = (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		  MSI_FLAG_MULTI_PCI_MSI  | MSI_FLAG_PCI_MSIX |
		  MSI_FLAG_MSIX_CONTIGUOUS),
	.ops   = &pseries_pci_msi_domain_ops,
	.chip  = &pseries_pci_msi_irq_chip,
};

static void pseries_msi_compose_msg(struct irq_data *data, struct msi_msg *msg)
{
	__pci_read_msi_msg(irq_data_get_msi_desc(data), msg);
}

static struct irq_chip pseries_msi_irq_chip = {
	.name			= "pSeries-MSI",
	.irq_shutdown		= pseries_msi_shutdown,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_compose_msi_msg	= pseries_msi_compose_msg,
};

static int pseries_irq_parent_domain_alloc(struct irq_domain *domain, unsigned int virq,
					   irq_hw_number_t hwirq)
{
	struct irq_fwspec parent_fwspec;
	int ret;

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 2;
	parent_fwspec.param[0] = hwirq;
	parent_fwspec.param[1] = IRQ_TYPE_EDGE_RISING;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
	if (ret)
		return ret;

	return 0;
}

static int pseries_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs, void *arg)
{
	struct pci_controller *phb = domain->host_data;
	msi_alloc_info_t *info = arg;
	struct msi_desc *desc = info->desc;
	struct pci_dev *pdev = msi_desc_to_pci_dev(desc);
	int hwirq;
	int i, ret;

	hwirq = rtas_query_irq_number(pci_get_pdn(pdev), desc->msi_index);
	if (hwirq < 0) {
		dev_err(&pdev->dev, "Failed to query HW IRQ: %d\n", hwirq);
		return hwirq;
	}

	dev_dbg(&pdev->dev, "%s bridge %pOF %d/%x #%d\n", __func__,
		phb->dn, virq, hwirq, nr_irqs);

	for (i = 0; i < nr_irqs; i++) {
		ret = pseries_irq_parent_domain_alloc(domain, virq + i, hwirq + i);
		if (ret)
			goto out;

		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &pseries_msi_irq_chip, domain->host_data);
	}

	return 0;

out:
	/* TODO: handle RTAS cleanup in ->msi_finish() ? */
	irq_domain_free_irqs_parent(domain, virq, i - 1);
	return ret;
}

static void pseries_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct pci_controller *phb = irq_data_get_irq_chip_data(d);

	pr_debug("%s bridge %pOF %d #%d\n", __func__, phb->dn, virq, nr_irqs);

	/* XIVE domain data is cleared through ->msi_free() */
}

static const struct irq_domain_ops pseries_irq_domain_ops = {
	.alloc  = pseries_irq_domain_alloc,
	.free   = pseries_irq_domain_free,
};

static int __pseries_msi_allocate_domains(struct pci_controller *phb,
					  unsigned int count)
{
	struct irq_domain *parent = irq_get_default_host();

	phb->fwnode = irq_domain_alloc_named_id_fwnode("pSeries-MSI",
						       phb->global_number);
	if (!phb->fwnode)
		return -ENOMEM;

	phb->dev_domain = irq_domain_create_hierarchy(parent, 0, count,
						      phb->fwnode,
						      &pseries_irq_domain_ops, phb);
	if (!phb->dev_domain) {
		pr_err("PCI: failed to create IRQ domain bridge %pOF (domain %d)\n",
		       phb->dn, phb->global_number);
		irq_domain_free_fwnode(phb->fwnode);
		return -ENOMEM;
	}

	phb->msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(phb->dn),
						    &pseries_msi_domain_info,
						    phb->dev_domain);
	if (!phb->msi_domain) {
		pr_err("PCI: failed to create MSI IRQ domain bridge %pOF (domain %d)\n",
		       phb->dn, phb->global_number);
		irq_domain_free_fwnode(phb->fwnode);
		irq_domain_remove(phb->dev_domain);
		return -ENOMEM;
	}

	return 0;
}

int pseries_msi_allocate_domains(struct pci_controller *phb)
{
	int count;

	if (!__find_pe_total_msi(phb->dn, &count)) {
		pr_err("PCI: failed to find MSIs for bridge %pOF (domain %d)\n",
		       phb->dn, phb->global_number);
		return -ENOSPC;
	}

	return __pseries_msi_allocate_domains(phb, count);
}

void pseries_msi_free_domains(struct pci_controller *phb)
{
	if (phb->msi_domain)
		irq_domain_remove(phb->msi_domain);
	if (phb->dev_domain)
		irq_domain_remove(phb->dev_domain);
	if (phb->fwnode)
		irq_domain_free_fwnode(phb->fwnode);
}

static void rtas_msi_pci_irq_fixup(struct pci_dev *pdev)
{
	/* No LSI -> leave MSIs (if any) configured */
	if (!pdev->irq) {
		dev_dbg(&pdev->dev, "rtas_msi: no LSI, nothing to do.\n");
		return;
	}

	/* No MSI -> MSIs can't have been assigned by fw, leave LSI */
	if (check_req_msi(pdev, 1) && check_req_msix(pdev, 1)) {
		dev_dbg(&pdev->dev, "rtas_msi: no req#msi/x, nothing to do.\n");
		return;
	}

	dev_dbg(&pdev->dev, "rtas_msi: disabling existing MSI.\n");
	rtas_disable_msi(pdev);
}

static int rtas_msi_init(void)
{
	query_token  = rtas_function_token(RTAS_FN_IBM_QUERY_INTERRUPT_SOURCE_NUMBER);
	change_token = rtas_function_token(RTAS_FN_IBM_CHANGE_MSI);

	if ((query_token == RTAS_UNKNOWN_SERVICE) ||
			(change_token == RTAS_UNKNOWN_SERVICE)) {
		pr_debug("rtas_msi: no RTAS tokens, no MSI support.\n");
		return -1;
	}

	pr_debug("rtas_msi: Registering RTAS MSI callbacks.\n");

	WARN_ON(ppc_md.pci_irq_fixup);
	ppc_md.pci_irq_fixup = rtas_msi_pci_irq_fixup;

	return 0;
}
machine_arch_initcall(pseries, rtas_msi_init);
