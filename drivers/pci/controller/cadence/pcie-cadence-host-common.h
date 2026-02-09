/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence PCIe Host controller driver.
 *
 * Copyright (c) 2017 Cadence
 * Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>
 */
#ifndef _PCIE_CADENCE_HOST_COMMON_H
#define _PCIE_CADENCE_HOST_COMMON_H

#include <linux/kernel.h>
#include <linux/pci.h>

extern u64 bar_max_size[];

typedef int (*cdns_pcie_host_bar_ib_cfg)(struct cdns_pcie_rc *,
					 enum cdns_pcie_rp_bar,
					 u64,
					 u64,
					 unsigned long);
typedef bool (*cdns_pcie_linkup_func)(struct cdns_pcie *);

int cdns_pcie_host_training_complete(struct cdns_pcie *pcie);
int cdns_pcie_host_wait_for_link(struct cdns_pcie *pcie,
				 cdns_pcie_linkup_func pcie_link_up);
int cdns_pcie_retrain(struct cdns_pcie *pcie, cdns_pcie_linkup_func pcie_linkup_func);
int cdns_pcie_host_start_link(struct cdns_pcie_rc *rc,
			      cdns_pcie_linkup_func pcie_link_up);
enum cdns_pcie_rp_bar
cdns_pcie_host_find_min_bar(struct cdns_pcie_rc *rc, u64 size);
enum cdns_pcie_rp_bar
cdns_pcie_host_find_max_bar(struct cdns_pcie_rc *rc, u64 size);
int cdns_pcie_host_dma_ranges_cmp(void *priv, const struct list_head *a,
				  const struct list_head *b);
int cdns_pcie_host_bar_ib_config(struct cdns_pcie_rc *rc,
				 enum cdns_pcie_rp_bar bar,
				 u64 cpu_addr,
				 u64 size,
				 unsigned long flags);
int cdns_pcie_host_bar_config(struct cdns_pcie_rc *rc,
			      struct resource_entry *entry,
			      cdns_pcie_host_bar_ib_cfg pci_host_ib_config);
int cdns_pcie_host_map_dma_ranges(struct cdns_pcie_rc *rc,
				  cdns_pcie_host_bar_ib_cfg pci_host_ib_config);

#endif /* _PCIE_CADENCE_HOST_COMMON_H */
