/*
 * PCI Express Downstream Port Containment services driver
 * Author: Keith Busch <keith.busch@intel.com>
 *
 * Copyright (C) 2016 Intel Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pcieport_if.h>

struct dpc_dev {
	struct pcie_device	*dev;
	struct work_struct	work;
	int			cap_pos;
	bool			rp;
};

static int dpc_wait_rp_inactive(struct dpc_dev *dpc)
{
	unsigned long timeout = jiffies + HZ;
	struct pci_dev *pdev = dpc->dev->port;
	u16 status;

	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_STATUS, &status);
	while (status & PCI_EXP_DPC_RP_BUSY &&
					!time_after(jiffies, timeout)) {
		msleep(10);
		pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_STATUS, &status);
	}
	if (status & PCI_EXP_DPC_RP_BUSY) {
		dev_warn(&pdev->dev, "DPC root port still busy\n");
		return -EBUSY;
	}
	return 0;
}

static void dpc_wait_link_inactive(struct pci_dev *pdev)
{
	unsigned long timeout = jiffies + HZ;
	u16 lnk_status;

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	while (lnk_status & PCI_EXP_LNKSTA_DLLLA &&
					!time_after(jiffies, timeout)) {
		msleep(10);
		pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	}
	if (lnk_status & PCI_EXP_LNKSTA_DLLLA)
		dev_warn(&pdev->dev, "Link state not disabled for DPC event\n");
}

static void interrupt_event_handler(struct work_struct *work)
{
	struct dpc_dev *dpc = container_of(work, struct dpc_dev, work);
	struct pci_dev *dev, *temp, *pdev = dpc->dev->port;
	struct pci_bus *parent = pdev->subordinate;

	pci_lock_rescan_remove();
	list_for_each_entry_safe_reverse(dev, temp, &parent->devices,
					 bus_list) {
		pci_dev_get(dev);
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	}
	pci_unlock_rescan_remove();

	dpc_wait_link_inactive(pdev);
	if (dpc->rp && dpc_wait_rp_inactive(dpc))
		return;
	pci_write_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_STATUS,
		PCI_EXP_DPC_STATUS_TRIGGER | PCI_EXP_DPC_STATUS_INTERRUPT);
}

static irqreturn_t dpc_irq(int irq, void *context)
{
	struct dpc_dev *dpc = (struct dpc_dev *)context;
	struct pci_dev *pdev = dpc->dev->port;
	u16 status, source;

	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_STATUS, &status);
	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_SOURCE_ID,
			     &source);
	if (!status)
		return IRQ_NONE;

	dev_info(&dpc->dev->device, "DPC containment event, status:%#06x source:%#06x\n",
		status, source);

	if (status & PCI_EXP_DPC_STATUS_TRIGGER) {
		u16 reason = (status >> 1) & 0x3;
		u16 ext_reason = (status >> 5) & 0x3;

		dev_warn(&dpc->dev->device, "DPC %s detected, remove downstream devices\n",
			 (reason == 0) ? "unmasked uncorrectable error" :
			 (reason == 1) ? "ERR_NONFATAL" :
			 (reason == 2) ? "ERR_FATAL" :
			 (ext_reason == 0) ? "RP PIO error" :
			 (ext_reason == 1) ? "software trigger" :
					     "reserved error");
		schedule_work(&dpc->work);
	}
	return IRQ_HANDLED;
}

#define FLAG(x, y) (((x) & (y)) ? '+' : '-')
static int dpc_probe(struct pcie_device *dev)
{
	struct dpc_dev *dpc;
	struct pci_dev *pdev = dev->port;
	int status;
	u16 ctl, cap;

	dpc = devm_kzalloc(&dev->device, sizeof(*dpc), GFP_KERNEL);
	if (!dpc)
		return -ENOMEM;

	dpc->cap_pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_DPC);
	dpc->dev = dev;
	INIT_WORK(&dpc->work, interrupt_event_handler);
	set_service_data(dev, dpc);

	status = devm_request_irq(&dev->device, dev->irq, dpc_irq, IRQF_SHARED,
				  "pcie-dpc", dpc);
	if (status) {
		dev_warn(&dev->device, "request IRQ%d failed: %d\n", dev->irq,
			 status);
		return status;
	}

	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CAP, &cap);
	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, &ctl);

	dpc->rp = (cap & PCI_EXP_DPC_CAP_RP_EXT);

	ctl |= PCI_EXP_DPC_CTL_EN_NONFATAL | PCI_EXP_DPC_CTL_INT_EN;
	pci_write_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, ctl);

	dev_info(&dev->device, "DPC error containment capabilities: Int Msg #%d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c\n",
		cap & 0xf, FLAG(cap, PCI_EXP_DPC_CAP_RP_EXT),
		FLAG(cap, PCI_EXP_DPC_CAP_POISONED_TLP),
		FLAG(cap, PCI_EXP_DPC_CAP_SW_TRIGGER), (cap >> 8) & 0xf,
		FLAG(cap, PCI_EXP_DPC_CAP_DL_ACTIVE));
	return status;
}

static void dpc_remove(struct pcie_device *dev)
{
	struct dpc_dev *dpc = get_service_data(dev);
	struct pci_dev *pdev = dev->port;
	u16 ctl;

	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, &ctl);
	ctl &= ~(PCI_EXP_DPC_CTL_EN_NONFATAL | PCI_EXP_DPC_CTL_INT_EN);
	pci_write_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, ctl);
}

static struct pcie_port_service_driver dpcdriver = {
	.name		= "dpc",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_DPC,
	.probe		= dpc_probe,
	.remove		= dpc_remove,
};

static int __init dpc_service_init(void)
{
	return pcie_port_service_register(&dpcdriver);
}
device_initcall(dpc_service_init);
