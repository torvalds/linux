/*
 * Copyright 2006 Jake Moilanen <moilanen@austin.ibm.com>, IBM Corp.
 * Copyright 2006-2007 Michael Ellerman, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 *
 */

#include <linux/device.h>
#include <linux/irq.h>
#include <linux/msi.h>

#include <asm/rtas.h>
#include <asm/hw_irq.h>
#include <asm/ppc-pci.h>

static int query_token, change_token;

#define RTAS_QUERY_FN		0
#define RTAS_CHANGE_FN		1
#define RTAS_RESET_FN		2
#define RTAS_CHANGE_MSI_FN	3
#define RTAS_CHANGE_MSIX_FN	4

static struct pci_dn *get_pdn(struct pci_dev *pdev)
{
	struct device_node *dn;
	struct pci_dn *pdn;

	dn = pci_device_to_OF_node(pdev);
	if (!dn) {
		dev_dbg(&pdev->dev, "rtas_msi: No OF device node\n");
		return NULL;
	}

	pdn = PCI_DN(dn);
	if (!pdn) {
		dev_dbg(&pdev->dev, "rtas_msi: No PCI DN\n");
		return NULL;
	}

	return pdn;
}

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
		if (func == RTAS_CHANGE_MSI_FN || func == RTAS_CHANGE_MSIX_FN)
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
	 * If the RTAS call succeeded, check the number of irqs is actually
	 * what we asked for. If not, return an error.
	 */
	if (rc == 0 && rtas_ret[0] != num_irqs)
		rc = -ENOSPC;

	pr_debug("rtas_msi: ibm,change_msi(func=%d,num=%d), got %d rc = %d\n",
		 func, num_irqs, rtas_ret[0], rc);

	return rc;
}

static void rtas_disable_msi(struct pci_dev *pdev)
{
	struct pci_dn *pdn;

	pdn = get_pdn(pdev);
	if (!pdn)
		return;

	if (rtas_change_msi(pdn, RTAS_CHANGE_FN, 0))
		pr_debug("rtas_msi: Setting MSIs to 0 failed!\n");
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

static void rtas_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		set_irq_msi(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
	}

	rtas_disable_msi(pdev);
}

static int check_req_msi(struct pci_dev *pdev, int nvec)
{
	struct device_node *dn;
	struct pci_dn *pdn;
	const u32 *req_msi;

	pdn = get_pdn(pdev);
	if (!pdn)
		return -ENODEV;

	dn = pdn->node;

	req_msi = of_get_property(dn, "ibm,req#msi", NULL);
	if (!req_msi) {
		pr_debug("rtas_msi: No ibm,req#msi on %s\n", dn->full_name);
		return -ENOENT;
	}

	if (*req_msi < nvec) {
		pr_debug("rtas_msi: ibm,req#msi requests < %d MSIs\n", nvec);
		return -ENOSPC;
	}

	return 0;
}

static int rtas_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	if (type == PCI_CAP_ID_MSIX)
		pr_debug("rtas_msi: MSI-X untested, trying anyway.\n");

	return check_req_msi(pdev, nvec);
}

static int rtas_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	struct pci_dn *pdn;
	int hwirq, virq, i, rc;
	struct msi_desc *entry;

	pdn = get_pdn(pdev);
	if (!pdn)
		return -ENODEV;

	/*
	 * Try the new more explicit firmware interface, if that fails fall
	 * back to the old interface. The old interface is known to never
	 * return MSI-Xs.
	 */
	if (type == PCI_CAP_ID_MSI) {
		rc = rtas_change_msi(pdn, RTAS_CHANGE_MSI_FN, nvec);

		if (rc) {
			pr_debug("rtas_msi: trying the old firmware call.\n");
			rc = rtas_change_msi(pdn, RTAS_CHANGE_FN, nvec);
		}
	} else
		rc = rtas_change_msi(pdn, RTAS_CHANGE_MSIX_FN, nvec);

	if (rc) {
		pr_debug("rtas_msi: rtas_change_msi() failed\n");
		return rc;
	}

	i = 0;
	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = rtas_query_irq_number(pdn, i);
		if (hwirq < 0) {
			pr_debug("rtas_msi: error (%d) getting hwirq\n", rc);
			return hwirq;
		}

		virq = irq_create_mapping(NULL, hwirq);

		if (virq == NO_IRQ) {
			pr_debug("rtas_msi: Failed mapping hwirq %d\n", hwirq);
			return -ENOSPC;
		}

		dev_dbg(&pdev->dev, "rtas_msi: allocated virq %d\n", virq);
		set_irq_msi(virq, entry);
		unmask_msi_irq(virq);
	}

	return 0;
}

static void rtas_msi_pci_irq_fixup(struct pci_dev *pdev)
{
	/* No LSI -> leave MSIs (if any) configured */
	if (pdev->irq == NO_IRQ) {
		dev_dbg(&pdev->dev, "rtas_msi: no LSI, nothing to do.\n");
		return;
	}

	/* No MSI -> MSIs can't have been assigned by fw, leave LSI */
	if (check_req_msi(pdev, 1)) {
		dev_dbg(&pdev->dev, "rtas_msi: no req#msi, nothing to do.\n");
		return;
	}

	dev_dbg(&pdev->dev, "rtas_msi: disabling existing MSI.\n");
	rtas_disable_msi(pdev);
}

static int rtas_msi_init(void)
{
	query_token  = rtas_token("ibm,query-interrupt-source-number");
	change_token = rtas_token("ibm,change-msi");

	if ((query_token == RTAS_UNKNOWN_SERVICE) ||
			(change_token == RTAS_UNKNOWN_SERVICE)) {
		pr_debug("rtas_msi: no RTAS tokens, no MSI support.\n");
		return -1;
	}

	pr_debug("rtas_msi: Registering RTAS MSI callbacks.\n");

	WARN_ON(ppc_md.setup_msi_irqs);
	ppc_md.setup_msi_irqs = rtas_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = rtas_teardown_msi_irqs;
	ppc_md.msi_check_device = rtas_msi_check_device;

	WARN_ON(ppc_md.pci_irq_fixup);
	ppc_md.pci_irq_fixup = rtas_msi_pci_irq_fixup;

	return 0;
}
arch_initcall(rtas_msi_init);
