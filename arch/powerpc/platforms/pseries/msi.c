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
#define RTAS_CHANGE_32MSI_FN	5

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
		if (func == RTAS_CHANGE_MSI_FN || func == RTAS_CHANGE_MSIX_FN ||
		    func == RTAS_CHANGE_32MSI_FN)
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

	pdn = get_pdn(pdev);
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

static void rtas_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		irq_set_msi_desc(entry->irq, NULL);
		irq_dispose_mapping(entry->irq);
	}

	rtas_disable_msi(pdev);
}

static int check_req(struct pci_dev *pdev, int nvec, char *prop_name)
{
	struct device_node *dn;
	struct pci_dn *pdn;
	const u32 *req_msi;

	pdn = get_pdn(pdev);
	if (!pdn)
		return -ENODEV;

	dn = pdn->node;

	req_msi = of_get_property(dn, prop_name, NULL);
	if (!req_msi) {
		pr_debug("rtas_msi: No %s on %s\n", prop_name, dn->full_name);
		return -ENOENT;
	}

	if (*req_msi < nvec) {
		pr_debug("rtas_msi: %s requests < %d MSIs\n", prop_name, nvec);

		if (*req_msi == 0) /* Be paranoid */
			return -ENOSPC;

		return *req_msi;
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

static struct device_node *find_pe_total_msi(struct pci_dev *dev, int *total)
{
	struct device_node *dn;
	const u32 *p;

	dn = of_node_get(pci_device_to_OF_node(dev));
	while (dn) {
		p = of_get_property(dn, "ibm,pe-total-#msi", NULL);
		if (p) {
			pr_debug("rtas_msi: found prop on dn %s\n",
				dn->full_name);
			*total = *p;
			return dn;
		}

		dn = of_get_next_parent(dn);
	}

	return NULL;
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
	edev = of_node_to_eeh_dev(dn);
	if (edev->pe)
		edev = list_first_entry(&edev->pe->edevs, struct eeh_dev, list);
	dn = eeh_dev_to_of_node(edev);
	if (!dn)
		return NULL;

	/* We actually want the parent */
	dn = of_get_parent(dn);
	if (!dn)
		return NULL;

	/* Hardcode of 8 for old firmwares */
	*total = 8;
	pr_debug("rtas_msi: using PE dn %s\n", dn->full_name);

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
	const u32 *p;
	u32 class;

	pr_debug("rtas_msi: counting %s\n", dn->full_name);

	p = of_get_property(dn, "class-code", NULL);
	class = p ? *p : 0;

	if ((class >> 8) != PCI_CLASS_BRIDGE_PCI)
		counts->num_devices++;

	return NULL;
}

static void *count_spare_msis(struct device_node *dn, void *data)
{
	struct msi_counts *counts = data;
	const u32 *p;
	int req;

	if (dn == counts->requestor)
		req = counts->request;
	else {
		/* We don't know if a driver will try to use MSI or MSI-X,
		 * so we just have to punt and use the larger of the two. */
		req = 0;
		p = of_get_property(dn, "ibm,req#msi", NULL);
		if (p)
			req = *p;

		p = of_get_property(dn, "ibm,req#msi-x", NULL);
		if (p)
			req = max(req, (int)*p);
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

	pr_debug("rtas_msi: found PE %s\n", pe_dn->full_name);

	memset(&counts, 0, sizeof(struct msi_counts));

	/* Work out how many devices we have below this PE */
	traverse_pci_devices(pe_dn, count_non_bridge_devices, &counts);

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
	traverse_pci_devices(pe_dn, count_spare_msis, &counts);

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

static int rtas_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	int quota, rc;

	if (type == PCI_CAP_ID_MSIX)
		rc = check_req_msix(pdev, nvec);
	else
		rc = check_req_msi(pdev, nvec);

	if (rc)
		return rc;

	quota = msi_quota_for_device(pdev, nvec);

	if (quota && quota < nvec)
		return quota;

	return 0;
}

static int check_msix_entries(struct pci_dev *pdev)
{
	struct msi_desc *entry;
	int expected;

	/* There's no way for us to express to firmware that we want
	 * a discontiguous, or non-zero based, range of MSI-X entries.
	 * So we must reject such requests. */

	expected = 0;
	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->msi_attrib.entry_nr != expected) {
			pr_debug("rtas_msi: bad MSI-X entries.\n");
			return -EINVAL;
		}
		expected++;
	}

	return 0;
}

static int rtas_setup_msi_irqs(struct pci_dev *pdev, int nvec_in, int type)
{
	struct pci_dn *pdn;
	int hwirq, virq, i, rc;
	struct msi_desc *entry;
	struct msi_msg msg;
	int nvec = nvec_in;

	pdn = get_pdn(pdev);
	if (!pdn)
		return -ENODEV;

	if (type == PCI_CAP_ID_MSIX && check_msix_entries(pdev))
		return -EINVAL;

	/*
	 * Firmware currently refuse any non power of two allocation
	 * so we round up if the quota will allow it.
	 */
	if (type == PCI_CAP_ID_MSIX) {
		int m = roundup_pow_of_two(nvec);
		int quota = msi_quota_for_device(pdev, m);

		if (quota >= m)
			nvec = m;
	}

	/*
	 * Try the new more explicit firmware interface, if that fails fall
	 * back to the old interface. The old interface is known to never
	 * return MSI-Xs.
	 */
again:
	if (type == PCI_CAP_ID_MSI) {
		if (pdn->force_32bit_msi)
			rc = rtas_change_msi(pdn, RTAS_CHANGE_32MSI_FN, nvec);
		else
			rc = rtas_change_msi(pdn, RTAS_CHANGE_MSI_FN, nvec);

		if (rc < 0 && !pdn->force_32bit_msi) {
			pr_debug("rtas_msi: trying the old firmware call.\n");
			rc = rtas_change_msi(pdn, RTAS_CHANGE_FN, nvec);
		}
	} else
		rc = rtas_change_msi(pdn, RTAS_CHANGE_MSIX_FN, nvec);

	if (rc != nvec) {
		if (nvec != nvec_in) {
			nvec = nvec_in;
			goto again;
		}
		pr_debug("rtas_msi: rtas_change_msi() failed\n");
		return rc;
	}

	i = 0;
	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = rtas_query_irq_number(pdn, i++);
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
		irq_set_msi_desc(virq, entry);

		/* Read config space back so we can restore after reset */
		read_msi_msg(virq, &msg);
		entry->msg = msg;
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
	if (check_req_msi(pdev, 1) && check_req_msix(pdev, 1)) {
		dev_dbg(&pdev->dev, "rtas_msi: no req#msi/x, nothing to do.\n");
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

static void quirk_radeon(struct pci_dev *dev)
{
	struct pci_dn *pdn = get_pdn(dev);

	if (pdn)
		pdn->force_32bit_msi = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0x68f2, quirk_radeon);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI, 0xaa68, quirk_radeon);
