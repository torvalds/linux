// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence PCIe host controller library.
 *
 * Copyright (c) 2017 Cadence
 * Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list_sort.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>

#include "pcie-cadence.h"
#include "pcie-cadence-host-common.h"

#define LINK_RETRAIN_TIMEOUT HZ

u64 bar_max_size[] = {
	[RP_BAR0] = _ULL(128 * SZ_2G),
	[RP_BAR1] = SZ_2G,
	[RP_NO_BAR] = _BITULL(63),
};
EXPORT_SYMBOL_GPL(bar_max_size);

int cdns_pcie_host_training_complete(struct cdns_pcie *pcie)
{
	u32 pcie_cap_off = CDNS_PCIE_RP_CAP_OFFSET;
	unsigned long end_jiffies;
	u16 lnk_stat;

	/* Wait for link training to complete. Exit after timeout. */
	end_jiffies = jiffies + LINK_RETRAIN_TIMEOUT;
	do {
		lnk_stat = cdns_pcie_rp_readw(pcie, pcie_cap_off + PCI_EXP_LNKSTA);
		if (!(lnk_stat & PCI_EXP_LNKSTA_LT))
			break;
		usleep_range(0, 1000);
	} while (time_before(jiffies, end_jiffies));

	if (!(lnk_stat & PCI_EXP_LNKSTA_LT))
		return 0;

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_training_complete);

int cdns_pcie_host_wait_for_link(struct cdns_pcie *pcie,
				 cdns_pcie_linkup_func pcie_link_up)
{
	struct device *dev = pcie->dev;
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (pcie_link_up(pcie)) {
			dev_info(dev, "Link up\n");
			return 0;
		}
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_wait_for_link);

int cdns_pcie_retrain(struct cdns_pcie *pcie,
		      cdns_pcie_linkup_func pcie_link_up)
{
	u32 lnk_cap_sls, pcie_cap_off = CDNS_PCIE_RP_CAP_OFFSET;
	u16 lnk_stat, lnk_ctl;
	int ret = 0;

	/*
	 * Set retrain bit if current speed is 2.5 GB/s,
	 * but the PCIe root port support is > 2.5 GB/s.
	 */

	lnk_cap_sls = cdns_pcie_readl(pcie, (CDNS_PCIE_RP_BASE + pcie_cap_off +
					     PCI_EXP_LNKCAP));
	if ((lnk_cap_sls & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
		return ret;

	lnk_stat = cdns_pcie_rp_readw(pcie, pcie_cap_off + PCI_EXP_LNKSTA);
	if ((lnk_stat & PCI_EXP_LNKSTA_CLS) == PCI_EXP_LNKSTA_CLS_2_5GB) {
		lnk_ctl = cdns_pcie_rp_readw(pcie,
					     pcie_cap_off + PCI_EXP_LNKCTL);
		lnk_ctl |= PCI_EXP_LNKCTL_RL;
		cdns_pcie_rp_writew(pcie, pcie_cap_off + PCI_EXP_LNKCTL,
				    lnk_ctl);

		ret = cdns_pcie_host_training_complete(pcie);
		if (ret)
			return ret;

		ret = cdns_pcie_host_wait_for_link(pcie, pcie_link_up);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cdns_pcie_retrain);

int cdns_pcie_host_start_link(struct cdns_pcie_rc *rc,
			      cdns_pcie_linkup_func pcie_link_up)
{
	struct cdns_pcie *pcie = &rc->pcie;
	int ret;

	ret = cdns_pcie_host_wait_for_link(pcie, pcie_link_up);

	/*
	 * Retrain link for Gen2 training defect
	 * if quirk flag is set.
	 */
	if (!ret && rc->quirk_retrain_flag)
		ret = cdns_pcie_retrain(pcie, pcie_link_up);

	return ret;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_start_link);

enum cdns_pcie_rp_bar
cdns_pcie_host_find_min_bar(struct cdns_pcie_rc *rc, u64 size)
{
	enum cdns_pcie_rp_bar bar, sel_bar;

	sel_bar = RP_BAR_UNDEFINED;
	for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++) {
		if (!rc->avail_ib_bar[bar])
			continue;

		if (size <= bar_max_size[bar]) {
			if (sel_bar == RP_BAR_UNDEFINED) {
				sel_bar = bar;
				continue;
			}

			if (bar_max_size[bar] < bar_max_size[sel_bar])
				sel_bar = bar;
		}
	}

	return sel_bar;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_find_min_bar);

enum cdns_pcie_rp_bar
cdns_pcie_host_find_max_bar(struct cdns_pcie_rc *rc, u64 size)
{
	enum cdns_pcie_rp_bar bar, sel_bar;

	sel_bar = RP_BAR_UNDEFINED;
	for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++) {
		if (!rc->avail_ib_bar[bar])
			continue;

		if (size >= bar_max_size[bar]) {
			if (sel_bar == RP_BAR_UNDEFINED) {
				sel_bar = bar;
				continue;
			}

			if (bar_max_size[bar] > bar_max_size[sel_bar])
				sel_bar = bar;
		}
	}

	return sel_bar;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_find_max_bar);

int cdns_pcie_host_dma_ranges_cmp(void *priv, const struct list_head *a,
				  const struct list_head *b)
{
	struct resource_entry *entry1, *entry2;
	u64 size1, size2;

	entry1 = container_of(a, struct resource_entry, node);
	entry2 = container_of(b, struct resource_entry, node);

	size1 = resource_size(entry1->res);
	size2 = resource_size(entry2->res);

	if (size1 > size2)
		return -1;

	if (size1 < size2)
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(cdns_pcie_host_dma_ranges_cmp);

int cdns_pcie_host_bar_config(struct cdns_pcie_rc *rc,
			      struct resource_entry *entry,
			      cdns_pcie_host_bar_ib_cfg pci_host_ib_config)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct device *dev = pcie->dev;
	u64 cpu_addr, size, winsize;
	enum cdns_pcie_rp_bar bar;
	unsigned long flags;
	int ret;

	cpu_addr = entry->res->start;
	flags = entry->res->flags;
	size = resource_size(entry->res);

	while (size > 0) {
		/*
		 * Try to find a minimum BAR whose size is greater than
		 * or equal to the remaining resource_entry size. This will
		 * fail if the size of each of the available BARs is less than
		 * the remaining resource_entry size.
		 *
		 * If a minimum BAR is found, IB ATU will be configured and
		 * exited.
		 */
		bar = cdns_pcie_host_find_min_bar(rc, size);
		if (bar != RP_BAR_UNDEFINED) {
			ret = pci_host_ib_config(rc, bar, cpu_addr, size, flags);
			if (ret)
				dev_err(dev, "IB BAR: %d config failed\n", bar);
			return ret;
		}

		/*
		 * If the control reaches here, it would mean the remaining
		 * resource_entry size cannot be fitted in a single BAR. So we
		 * find a maximum BAR whose size is less than or equal to the
		 * remaining resource_entry size and split the resource entry
		 * so that part of resource entry is fitted inside the maximum
		 * BAR. The remaining size would be fitted during the next
		 * iteration of the loop.
		 *
		 * If a maximum BAR is not found, there is no way we can fit
		 * this resource_entry, so we error out.
		 */
		bar = cdns_pcie_host_find_max_bar(rc, size);
		if (bar == RP_BAR_UNDEFINED) {
			dev_err(dev, "No free BAR to map cpu_addr %llx\n",
				cpu_addr);
			return -EINVAL;
		}

		winsize = bar_max_size[bar];
		ret = pci_host_ib_config(rc, bar, cpu_addr, winsize, flags);
		if (ret) {
			dev_err(dev, "IB BAR: %d config failed\n", bar);
			return ret;
		}

		size -= winsize;
		cpu_addr += winsize;
	}

	return 0;
}

int cdns_pcie_host_map_dma_ranges(struct cdns_pcie_rc *rc,
				  cdns_pcie_host_bar_ib_cfg pci_host_ib_config)
{
	struct cdns_pcie *pcie = &rc->pcie;
	struct device *dev = pcie->dev;
	struct device_node *np = dev->of_node;
	struct pci_host_bridge *bridge;
	struct resource_entry *entry;
	u32 no_bar_nbits = 32;
	int err;

	bridge = pci_host_bridge_from_priv(rc);
	if (!bridge)
		return -ENOMEM;

	if (list_empty(&bridge->dma_ranges)) {
		of_property_read_u32(np, "cdns,no-bar-match-nbits",
				     &no_bar_nbits);
		err = pci_host_ib_config(rc, RP_NO_BAR, 0x0, (u64)1 << no_bar_nbits, 0);
		if (err)
			dev_err(dev, "IB BAR: %d config failed\n", RP_NO_BAR);
		return err;
	}

	list_sort(NULL, &bridge->dma_ranges, cdns_pcie_host_dma_ranges_cmp);

	resource_list_for_each_entry(entry, &bridge->dma_ranges) {
		err = cdns_pcie_host_bar_config(rc, entry, pci_host_ib_config);
		if (err) {
			dev_err(dev, "Fail to configure IB using dma-ranges\n");
			return err;
		}
	}

	return 0;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence PCIe host controller driver");
