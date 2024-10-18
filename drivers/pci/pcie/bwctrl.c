// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe bandwidth controller
 *
 * Author: Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Copyright (C) 2019 Dell Inc
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * This service port driver hooks into the Bandwidth Notification interrupt
 * watching for changes or links becoming degraded in operation. It updates
 * the cached Current Link Speed that is exposed to user space through sysfs.
 */

#define dev_fmt(fmt) "bwctrl: " fmt

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "../pci.h"
#include "portdrv.h"

/**
 * struct pcie_bwctrl_data - PCIe bandwidth controller
 * @lbms_count:		Count for LBMS (since last reset)
 */
struct pcie_bwctrl_data {
	atomic_t lbms_count;
};

/* Prevents port removal during LBMS count accessors */
static DECLARE_RWSEM(pcie_bwctrl_lbms_rwsem);

static void pcie_bwnotif_enable(struct pcie_device *srv)
{
	struct pcie_bwctrl_data *data = srv->port->link_bwctrl;
	struct pci_dev *port = srv->port;
	u16 link_status;
	int ret;

	/* Count LBMS seen so far as one */
	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret == PCIBIOS_SUCCESSFUL && link_status & PCI_EXP_LNKSTA_LBMS)
		atomic_inc(&data->lbms_count);

	pcie_capability_set_word(port, PCI_EXP_LNKCTL,
				 PCI_EXP_LNKCTL_LBMIE | PCI_EXP_LNKCTL_LABIE);
	pcie_capability_write_word(port, PCI_EXP_LNKSTA,
				   PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_LABS);

	/*
	 * Update after enabling notifications & clearing status bits ensures
	 * link speed is up to date.
	 */
	pcie_update_link_speed(port->subordinate);
}

static void pcie_bwnotif_disable(struct pci_dev *port)
{
	pcie_capability_clear_word(port, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_LBMIE | PCI_EXP_LNKCTL_LABIE);
}

static irqreturn_t pcie_bwnotif_irq(int irq, void *context)
{
	struct pcie_device *srv = context;
	struct pcie_bwctrl_data *data = srv->port->link_bwctrl;
	struct pci_dev *port = srv->port;
	u16 link_status, events;
	int ret;

	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret != PCIBIOS_SUCCESSFUL)
		return IRQ_NONE;

	events = link_status & (PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_LABS);
	if (!events)
		return IRQ_NONE;

	if (events & PCI_EXP_LNKSTA_LBMS)
		atomic_inc(&data->lbms_count);

	pcie_capability_write_word(port, PCI_EXP_LNKSTA, events);

	/*
	 * Interrupts will not be triggered from any further Link Speed
	 * change until LBMS is cleared by the write. Therefore, re-read the
	 * speed (inside pcie_update_link_speed()) after LBMS has been
	 * cleared to avoid missing link speed changes.
	 */
	pcie_update_link_speed(port->subordinate);

	return IRQ_HANDLED;
}

void pcie_reset_lbms_count(struct pci_dev *port)
{
	struct pcie_bwctrl_data *data;

	guard(rwsem_read)(&pcie_bwctrl_lbms_rwsem);
	data = port->link_bwctrl;
	if (data)
		atomic_set(&data->lbms_count, 0);
	else
		pcie_capability_write_word(port, PCI_EXP_LNKSTA,
					   PCI_EXP_LNKSTA_LBMS);
}

int pcie_lbms_count(struct pci_dev *port, unsigned long *val)
{
	struct pcie_bwctrl_data *data;

	guard(rwsem_read)(&pcie_bwctrl_lbms_rwsem);
	data = port->link_bwctrl;
	if (!data)
		return -ENOTTY;

	*val = atomic_read(&data->lbms_count);

	return 0;
}

static int pcie_bwnotif_probe(struct pcie_device *srv)
{
	struct pci_dev *port = srv->port;
	int ret;

	struct pcie_bwctrl_data *data = devm_kzalloc(&srv->device,
						     sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = devm_request_irq(&srv->device, srv->irq, pcie_bwnotif_irq,
			       IRQF_SHARED, "PCIe bwctrl", srv);
	if (ret)
		return ret;

	scoped_guard(rwsem_write, &pcie_bwctrl_lbms_rwsem) {
		port->link_bwctrl = no_free_ptr(data);
		pcie_bwnotif_enable(srv);
	}

	pci_dbg(port, "enabled with IRQ %d\n", srv->irq);

	return 0;
}

static void pcie_bwnotif_remove(struct pcie_device *srv)
{
	pcie_bwnotif_disable(srv->port);
	scoped_guard(rwsem_write, &pcie_bwctrl_lbms_rwsem)
		srv->port->link_bwctrl = NULL;
}

static int pcie_bwnotif_suspend(struct pcie_device *srv)
{
	pcie_bwnotif_disable(srv->port);
	return 0;
}

static int pcie_bwnotif_resume(struct pcie_device *srv)
{
	pcie_bwnotif_enable(srv);
	return 0;
}

static struct pcie_port_service_driver pcie_bwctrl_driver = {
	.name		= "pcie_bwctrl",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_BWCTRL,
	.probe		= pcie_bwnotif_probe,
	.suspend	= pcie_bwnotif_suspend,
	.resume		= pcie_bwnotif_resume,
	.remove		= pcie_bwnotif_remove,
};

int __init pcie_bwctrl_init(void)
{
	return pcie_port_service_register(&pcie_bwctrl_driver);
}
