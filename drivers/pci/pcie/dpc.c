// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Express Downstream Port Containment services driver
 * Author: Keith Busch <keith.busch@intel.com>
 *
 * Copyright (C) 2016 Intel Corp.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>

#include "portdrv.h"
#include "../pci.h"
#include "aer/aerdrv.h"

struct dpc_dev {
	struct pcie_device	*dev;
	struct work_struct	work;
	u16			cap_pos;
	bool			rp_extensions;
	u32			rp_pio_status;
	u8			rp_log_size;
};

static const char * const rp_pio_error_string[] = {
	"Configuration Request received UR Completion",	 /* Bit Position 0  */
	"Configuration Request received CA Completion",	 /* Bit Position 1  */
	"Configuration Request Completion Timeout",	 /* Bit Position 2  */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"I/O Request received UR Completion",		 /* Bit Position 8  */
	"I/O Request received CA Completion",		 /* Bit Position 9  */
	"I/O Request Completion Timeout",		 /* Bit Position 10 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Memory Request received UR Completion",	 /* Bit Position 16 */
	"Memory Request received CA Completion",	 /* Bit Position 17 */
	"Memory Request Completion Timeout",		 /* Bit Position 18 */
};

static int dpc_wait_rp_inactive(struct dpc_dev *dpc)
{
	unsigned long timeout = jiffies + HZ;
	struct pci_dev *pdev = dpc->dev->port;
	struct device *dev = &dpc->dev->device;
	u16 cap = dpc->cap_pos, status;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);
	while (status & PCI_EXP_DPC_RP_BUSY &&
					!time_after(jiffies, timeout)) {
		msleep(10);
		pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);
	}
	if (status & PCI_EXP_DPC_RP_BUSY) {
		dev_warn(dev, "DPC root port still busy\n");
		return -EBUSY;
	}
	return 0;
}

static void dpc_wait_link_inactive(struct dpc_dev *dpc)
{
	unsigned long timeout = jiffies + HZ;
	struct pci_dev *pdev = dpc->dev->port;
	struct device *dev = &dpc->dev->device;
	u16 lnk_status;

	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	while (lnk_status & PCI_EXP_LNKSTA_DLLLA &&
					!time_after(jiffies, timeout)) {
		msleep(10);
		pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &lnk_status);
	}
	if (lnk_status & PCI_EXP_LNKSTA_DLLLA)
		dev_warn(dev, "Link state not disabled for DPC event\n");
}

static void dpc_work(struct work_struct *work)
{
	struct dpc_dev *dpc = container_of(work, struct dpc_dev, work);
	struct pci_dev *dev, *temp, *pdev = dpc->dev->port;
	struct pci_bus *parent = pdev->subordinate;
	u16 cap = dpc->cap_pos, ctl;

	pci_lock_rescan_remove();
	list_for_each_entry_safe_reverse(dev, temp, &parent->devices,
					 bus_list) {
		pci_dev_get(dev);
		pci_dev_set_disconnected(dev, NULL);
		if (pci_has_subordinate(dev))
			pci_walk_bus(dev->subordinate,
				     pci_dev_set_disconnected, NULL);
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	}
	pci_unlock_rescan_remove();

	dpc_wait_link_inactive(dpc);
	if (dpc->rp_extensions && dpc_wait_rp_inactive(dpc))
		return;
	if (dpc->rp_extensions && dpc->rp_pio_status) {
		pci_write_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_STATUS,
				       dpc->rp_pio_status);
		dpc->rp_pio_status = 0;
	}

	pci_write_config_word(pdev, cap + PCI_EXP_DPC_STATUS,
		PCI_EXP_DPC_STATUS_TRIGGER | PCI_EXP_DPC_STATUS_INTERRUPT);

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_CTL, &ctl);
	pci_write_config_word(pdev, cap + PCI_EXP_DPC_CTL,
			      ctl | PCI_EXP_DPC_CTL_INT_EN);
}

static void dpc_process_rp_pio_error(struct dpc_dev *dpc)
{
	struct device *dev = &dpc->dev->device;
	struct pci_dev *pdev = dpc->dev->port;
	u16 cap = dpc->cap_pos, dpc_status, first_error;
	u32 status, mask, sev, syserr, exc, dw0, dw1, dw2, dw3, log, prefix;
	int i;

	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_STATUS, &status);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_MASK, &mask);
	dev_err(dev, "rp_pio_status: %#010x, rp_pio_mask: %#010x\n",
		status, mask);

	dpc->rp_pio_status = status;

	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_SEVERITY, &sev);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_SYSERROR, &syserr);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_EXCEPTION, &exc);
	dev_err(dev, "RP PIO severity=%#010x, syserror=%#010x, exception=%#010x\n",
		sev, syserr, exc);

	/* Get First Error Pointer */
	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &dpc_status);
	first_error = (dpc_status & 0x1f00) >> 8;

	status &= ~mask;
	for (i = 0; i < ARRAY_SIZE(rp_pio_error_string); i++) {
		if (status & (1 << i))
			dev_err(dev, "[%2d] %s%s\n", i, rp_pio_error_string[i],
				first_error == i ? " (First)" : "");
	}

	if (dpc->rp_log_size < 4)
		return;
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG,
			      &dw0);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 4,
			      &dw1);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 8,
			      &dw2);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 12,
			      &dw3);
	dev_err(dev, "TLP Header: %#010x %#010x %#010x %#010x\n",
		dw0, dw1, dw2, dw3);

	if (dpc->rp_log_size < 5)
		return;
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_IMPSPEC_LOG, &log);
	dev_err(dev, "RP PIO ImpSpec Log %#010x\n", log);

	for (i = 0; i < dpc->rp_log_size - 5; i++) {
		pci_read_config_dword(pdev,
			cap + PCI_EXP_DPC_RP_PIO_TLPPREFIX_LOG, &prefix);
		dev_err(dev, "TLP Prefix Header: dw%d, %#010x\n", i, prefix);
	}
}

static irqreturn_t dpc_irq(int irq, void *context)
{
	struct dpc_dev *dpc = (struct dpc_dev *)context;
	struct pci_dev *pdev = dpc->dev->port;
	struct device *dev = &dpc->dev->device;
	u16 cap = dpc->cap_pos, ctl, status, source, reason, ext_reason;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_CTL, &ctl);

	if (!(ctl & PCI_EXP_DPC_CTL_INT_EN) || ctl == (u16)(~0))
		return IRQ_NONE;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);

	if (!(status & PCI_EXP_DPC_STATUS_INTERRUPT))
		return IRQ_NONE;

	if (!(status & PCI_EXP_DPC_STATUS_TRIGGER)) {
		pci_write_config_word(pdev, cap + PCI_EXP_DPC_STATUS,
				      PCI_EXP_DPC_STATUS_INTERRUPT);
		return IRQ_HANDLED;
	}

	pci_write_config_word(pdev, cap + PCI_EXP_DPC_CTL,
			      ctl & ~PCI_EXP_DPC_CTL_INT_EN);

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_SOURCE_ID,
			     &source);

	dev_info(dev, "DPC containment event, status:%#06x source:%#06x\n",
		status, source);

	reason = (status & PCI_EXP_DPC_STATUS_TRIGGER_RSN) >> 1;
	ext_reason = (status & PCI_EXP_DPC_STATUS_TRIGGER_RSN_EXT) >> 5;

	dev_warn(dev, "DPC %s detected, remove downstream devices\n",
		 (reason == 0) ? "unmasked uncorrectable error" :
		 (reason == 1) ? "ERR_NONFATAL" :
		 (reason == 2) ? "ERR_FATAL" :
		 (ext_reason == 0) ? "RP PIO error" :
		 (ext_reason == 1) ? "software trigger" :
				     "reserved error");
	/* show RP PIO error detail information */
	if (dpc->rp_extensions && reason == 3 && ext_reason == 0)
		dpc_process_rp_pio_error(dpc);

	schedule_work(&dpc->work);

	return IRQ_HANDLED;
}

#define FLAG(x, y) (((x) & (y)) ? '+' : '-')
static int dpc_probe(struct pcie_device *dev)
{
	struct dpc_dev *dpc;
	struct pci_dev *pdev = dev->port;
	struct device *device = &dev->device;
	int status;
	u16 ctl, cap;

	if (pcie_aer_get_firmware_first(pdev))
		return -ENOTSUPP;

	dpc = devm_kzalloc(device, sizeof(*dpc), GFP_KERNEL);
	if (!dpc)
		return -ENOMEM;

	dpc->cap_pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_DPC);
	dpc->dev = dev;
	INIT_WORK(&dpc->work, dpc_work);
	set_service_data(dev, dpc);

	status = devm_request_irq(device, dev->irq, dpc_irq, IRQF_SHARED,
				  "pcie-dpc", dpc);
	if (status) {
		dev_warn(device, "request IRQ%d failed: %d\n", dev->irq,
			 status);
		return status;
	}

	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CAP, &cap);
	pci_read_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, &ctl);

	dpc->rp_extensions = (cap & PCI_EXP_DPC_CAP_RP_EXT);
	if (dpc->rp_extensions) {
		dpc->rp_log_size = (cap & PCI_EXP_DPC_RP_PIO_LOG_SIZE) >> 8;
		if (dpc->rp_log_size < 4 || dpc->rp_log_size > 9) {
			dev_err(device, "RP PIO log size %u is invalid\n",
				dpc->rp_log_size);
			dpc->rp_log_size = 0;
		}
	}

	ctl = (ctl & 0xfff4) | PCI_EXP_DPC_CTL_EN_NONFATAL | PCI_EXP_DPC_CTL_INT_EN;
	pci_write_config_word(pdev, dpc->cap_pos + PCI_EXP_DPC_CTL, ctl);

	dev_info(device, "DPC error containment capabilities: Int Msg #%d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c\n",
		cap & PCI_EXP_DPC_IRQ, FLAG(cap, PCI_EXP_DPC_CAP_RP_EXT),
		FLAG(cap, PCI_EXP_DPC_CAP_POISONED_TLP),
		FLAG(cap, PCI_EXP_DPC_CAP_SW_TRIGGER), dpc->rp_log_size,
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
