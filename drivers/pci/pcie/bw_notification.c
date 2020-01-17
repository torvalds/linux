// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Express Link Bandwidth Notification services driver
 * Author: Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Copyright (C) 2019, Dell Inc
 *
 * The PCIe Link Bandwidth Notification provides a way to yestify the
 * operating system when the link width or data rate changes.  This
 * capability is required for all root ports and downstream ports
 * supporting links wider than x1 and/or multiple link speeds.
 *
 * This service port driver hooks into the bandwidth yestification interrupt
 * and warns when links become degraded in operation.
 */

#include "../pci.h"
#include "portdrv.h"

static bool pcie_link_bandwidth_yestification_supported(struct pci_dev *dev)
{
	int ret;
	u32 lnk_cap;

	ret = pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnk_cap);
	return (ret == PCIBIOS_SUCCESSFUL) && (lnk_cap & PCI_EXP_LNKCAP_LBNC);
}

static void pcie_enable_link_bandwidth_yestification(struct pci_dev *dev)
{
	u16 lnk_ctl;

	pcie_capability_write_word(dev, PCI_EXP_LNKSTA, PCI_EXP_LNKSTA_LBMS);

	pcie_capability_read_word(dev, PCI_EXP_LNKCTL, &lnk_ctl);
	lnk_ctl |= PCI_EXP_LNKCTL_LBMIE;
	pcie_capability_write_word(dev, PCI_EXP_LNKCTL, lnk_ctl);
}

static void pcie_disable_link_bandwidth_yestification(struct pci_dev *dev)
{
	u16 lnk_ctl;

	pcie_capability_read_word(dev, PCI_EXP_LNKCTL, &lnk_ctl);
	lnk_ctl &= ~PCI_EXP_LNKCTL_LBMIE;
	pcie_capability_write_word(dev, PCI_EXP_LNKCTL, lnk_ctl);
}

static irqreturn_t pcie_bw_yestification_irq(int irq, void *context)
{
	struct pcie_device *srv = context;
	struct pci_dev *port = srv->port;
	u16 link_status, events;
	int ret;

	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	events = link_status & PCI_EXP_LNKSTA_LBMS;

	if (ret != PCIBIOS_SUCCESSFUL || !events)
		return IRQ_NONE;

	pcie_capability_write_word(port, PCI_EXP_LNKSTA, events);
	pcie_update_link_speed(port->subordinate, link_status);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t pcie_bw_yestification_handler(int irq, void *context)
{
	struct pcie_device *srv = context;
	struct pci_dev *port = srv->port;
	struct pci_dev *dev;

	/*
	 * Print status from downstream devices, yest this root port or
	 * downstream switch port.
	 */
	down_read(&pci_bus_sem);
	list_for_each_entry(dev, &port->subordinate->devices, bus_list)
		pcie_report_downtraining(dev);
	up_read(&pci_bus_sem);

	return IRQ_HANDLED;
}

static int pcie_bandwidth_yestification_probe(struct pcie_device *srv)
{
	int ret;

	/* Single-width or single-speed ports do yest have to support this. */
	if (!pcie_link_bandwidth_yestification_supported(srv->port))
		return -ENODEV;

	ret = request_threaded_irq(srv->irq, pcie_bw_yestification_irq,
				   pcie_bw_yestification_handler,
				   IRQF_SHARED, "PCIe BW yestif", srv);
	if (ret)
		return ret;

	pcie_enable_link_bandwidth_yestification(srv->port);

	return 0;
}

static void pcie_bandwidth_yestification_remove(struct pcie_device *srv)
{
	pcie_disable_link_bandwidth_yestification(srv->port);
	free_irq(srv->irq, srv);
}

static int pcie_bandwidth_yestification_suspend(struct pcie_device *srv)
{
	pcie_disable_link_bandwidth_yestification(srv->port);
	return 0;
}

static int pcie_bandwidth_yestification_resume(struct pcie_device *srv)
{
	pcie_enable_link_bandwidth_yestification(srv->port);
	return 0;
}

static struct pcie_port_service_driver pcie_bandwidth_yestification_driver = {
	.name		= "pcie_bw_yestification",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_BWNOTIF,
	.probe		= pcie_bandwidth_yestification_probe,
	.suspend	= pcie_bandwidth_yestification_suspend,
	.resume		= pcie_bandwidth_yestification_resume,
	.remove		= pcie_bandwidth_yestification_remove,
};

int __init pcie_bandwidth_yestification_init(void)
{
	return pcie_port_service_register(&pcie_bandwidth_yestification_driver);
}
