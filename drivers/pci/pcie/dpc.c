// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Express Downstream Port Containment services driver
 * Author: Keith Busch <keith.busch@intel.com>
 *
 * Copyright (C) 2016 Intel Corp.
 */

#define dev_fmt(fmt) "DPC: " fmt

#include <linux/aer.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>

#include "portdrv.h"
#include "../pci.h"

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

void pci_save_dpc_state(struct pci_dev *dev)
{
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	if (!pci_is_pcie(dev))
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_DPC);
	if (!save_state)
		return;

	cap = (u16 *)&save_state->cap.data[0];
	pci_read_config_word(dev, dev->dpc_cap + PCI_EXP_DPC_CTL, cap);
}

void pci_restore_dpc_state(struct pci_dev *dev)
{
	struct pci_cap_saved_state *save_state;
	u16 *cap;

	if (!pci_is_pcie(dev))
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_DPC);
	if (!save_state)
		return;

	cap = (u16 *)&save_state->cap.data[0];
	pci_write_config_word(dev, dev->dpc_cap + PCI_EXP_DPC_CTL, *cap);
}

static DECLARE_WAIT_QUEUE_HEAD(dpc_completed_waitqueue);

#ifdef CONFIG_HOTPLUG_PCI_PCIE
static bool dpc_completed(struct pci_dev *pdev)
{
	u16 status;

	pci_read_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_STATUS, &status);
	if ((!PCI_POSSIBLE_ERROR(status)) && (status & PCI_EXP_DPC_STATUS_TRIGGER))
		return false;

	if (test_bit(PCI_DPC_RECOVERING, &pdev->priv_flags))
		return false;

	return true;
}

/**
 * pci_dpc_recovered - whether DPC triggered and has recovered successfully
 * @pdev: PCI device
 *
 * Return true if DPC was triggered for @pdev and has recovered successfully.
 * Wait for recovery if it hasn't completed yet.  Called from the PCIe hotplug
 * driver to recognize and ignore Link Down/Up events caused by DPC.
 */
bool pci_dpc_recovered(struct pci_dev *pdev)
{
	struct pci_host_bridge *host;

	if (!pdev->dpc_cap)
		return false;

	/*
	 * Synchronization between hotplug and DPC is not supported
	 * if DPC is owned by firmware and EDR is not enabled.
	 */
	host = pci_find_host_bridge(pdev->bus);
	if (!host->native_dpc && !IS_ENABLED(CONFIG_PCIE_EDR))
		return false;

	/*
	 * Need a timeout in case DPC never completes due to failure of
	 * dpc_wait_rp_inactive().  The spec doesn't mandate a time limit,
	 * but reports indicate that DPC completes within 4 seconds.
	 */
	wait_event_timeout(dpc_completed_waitqueue, dpc_completed(pdev),
			   msecs_to_jiffies(4000));

	return test_and_clear_bit(PCI_DPC_RECOVERED, &pdev->priv_flags);
}
#endif /* CONFIG_HOTPLUG_PCI_PCIE */

static int dpc_wait_rp_inactive(struct pci_dev *pdev)
{
	unsigned long timeout = jiffies + HZ;
	u16 cap = pdev->dpc_cap, status;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);
	while (status & PCI_EXP_DPC_RP_BUSY &&
					!time_after(jiffies, timeout)) {
		msleep(10);
		pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);
	}
	if (status & PCI_EXP_DPC_RP_BUSY) {
		pci_warn(pdev, "root port still busy\n");
		return -EBUSY;
	}
	return 0;
}

pci_ers_result_t dpc_reset_link(struct pci_dev *pdev)
{
	pci_ers_result_t ret;
	u16 cap;

	set_bit(PCI_DPC_RECOVERING, &pdev->priv_flags);

	/*
	 * DPC disables the Link automatically in hardware, so it has
	 * already been reset by the time we get here.
	 */
	cap = pdev->dpc_cap;

	/*
	 * Wait until the Link is inactive, then clear DPC Trigger Status
	 * to allow the Port to leave DPC.
	 */
	if (!pcie_wait_for_link(pdev, false))
		pci_info(pdev, "Data Link Layer Link Active not cleared in 1000 msec\n");

	if (pdev->dpc_rp_extensions && dpc_wait_rp_inactive(pdev)) {
		clear_bit(PCI_DPC_RECOVERED, &pdev->priv_flags);
		ret = PCI_ERS_RESULT_DISCONNECT;
		goto out;
	}

	pci_write_config_word(pdev, cap + PCI_EXP_DPC_STATUS,
			      PCI_EXP_DPC_STATUS_TRIGGER);

	if (pci_bridge_wait_for_secondary_bus(pdev, "DPC",
					      PCIE_RESET_READY_POLL_MS)) {
		clear_bit(PCI_DPC_RECOVERED, &pdev->priv_flags);
		ret = PCI_ERS_RESULT_DISCONNECT;
	} else {
		set_bit(PCI_DPC_RECOVERED, &pdev->priv_flags);
		ret = PCI_ERS_RESULT_RECOVERED;
	}
out:
	clear_bit(PCI_DPC_RECOVERING, &pdev->priv_flags);
	wake_up_all(&dpc_completed_waitqueue);
	return ret;
}

static void dpc_process_rp_pio_error(struct pci_dev *pdev)
{
	u16 cap = pdev->dpc_cap, dpc_status, first_error;
	u32 status, mask, sev, syserr, exc, dw0, dw1, dw2, dw3, log, prefix;
	int i;

	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_STATUS, &status);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_MASK, &mask);
	pci_err(pdev, "rp_pio_status: %#010x, rp_pio_mask: %#010x\n",
		status, mask);

	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_SEVERITY, &sev);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_SYSERROR, &syserr);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_EXCEPTION, &exc);
	pci_err(pdev, "RP PIO severity=%#010x, syserror=%#010x, exception=%#010x\n",
		sev, syserr, exc);

	/* Get First Error Pointer */
	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &dpc_status);
	first_error = (dpc_status & 0x1f00) >> 8;

	for (i = 0; i < ARRAY_SIZE(rp_pio_error_string); i++) {
		if ((status & ~mask) & (1 << i))
			pci_err(pdev, "[%2d] %s%s\n", i, rp_pio_error_string[i],
				first_error == i ? " (First)" : "");
	}

	if (pdev->dpc_rp_log_size < 4)
		goto clear_status;
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG,
			      &dw0);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 4,
			      &dw1);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 8,
			      &dw2);
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_HEADER_LOG + 12,
			      &dw3);
	pci_err(pdev, "TLP Header: %#010x %#010x %#010x %#010x\n",
		dw0, dw1, dw2, dw3);

	if (pdev->dpc_rp_log_size < 5)
		goto clear_status;
	pci_read_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_IMPSPEC_LOG, &log);
	pci_err(pdev, "RP PIO ImpSpec Log %#010x\n", log);

	for (i = 0; i < pdev->dpc_rp_log_size - 5; i++) {
		pci_read_config_dword(pdev,
			cap + PCI_EXP_DPC_RP_PIO_TLPPREFIX_LOG, &prefix);
		pci_err(pdev, "TLP Prefix Header: dw%d, %#010x\n", i, prefix);
	}
 clear_status:
	pci_write_config_dword(pdev, cap + PCI_EXP_DPC_RP_PIO_STATUS, status);
}

static int dpc_get_aer_uncorrect_severity(struct pci_dev *dev,
					  struct aer_err_info *info)
{
	int pos = dev->aer_cap;
	u32 status, mask, sev;

	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, &mask);
	status &= ~mask;
	if (!status)
		return 0;

	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &sev);
	status &= sev;
	if (status)
		info->severity = AER_FATAL;
	else
		info->severity = AER_NONFATAL;

	return 1;
}

void dpc_process_error(struct pci_dev *pdev)
{
	u16 cap = pdev->dpc_cap, status, source, reason, ext_reason;
	struct aer_err_info info;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);
	pci_read_config_word(pdev, cap + PCI_EXP_DPC_SOURCE_ID, &source);

	pci_info(pdev, "containment event, status:%#06x source:%#06x\n",
		 status, source);

	reason = (status & PCI_EXP_DPC_STATUS_TRIGGER_RSN) >> 1;
	ext_reason = (status & PCI_EXP_DPC_STATUS_TRIGGER_RSN_EXT) >> 5;
	pci_warn(pdev, "%s detected\n",
		 (reason == 0) ? "unmasked uncorrectable error" :
		 (reason == 1) ? "ERR_NONFATAL" :
		 (reason == 2) ? "ERR_FATAL" :
		 (ext_reason == 0) ? "RP PIO error" :
		 (ext_reason == 1) ? "software trigger" :
				     "reserved error");

	/* show RP PIO error detail information */
	if (pdev->dpc_rp_extensions && reason == 3 && ext_reason == 0)
		dpc_process_rp_pio_error(pdev);
	else if (reason == 0 &&
		 dpc_get_aer_uncorrect_severity(pdev, &info) &&
		 aer_get_device_error_info(pdev, &info)) {
		aer_print_error(pdev, &info);
		pci_aer_clear_nonfatal_status(pdev);
		pci_aer_clear_fatal_status(pdev);
	}
}

static irqreturn_t dpc_handler(int irq, void *context)
{
	struct pci_dev *pdev = context;

	dpc_process_error(pdev);

	/* We configure DPC so it only triggers on ERR_FATAL */
	pcie_do_recovery(pdev, pci_channel_io_frozen, dpc_reset_link);

	return IRQ_HANDLED;
}

static irqreturn_t dpc_irq(int irq, void *context)
{
	struct pci_dev *pdev = context;
	u16 cap = pdev->dpc_cap, status;

	pci_read_config_word(pdev, cap + PCI_EXP_DPC_STATUS, &status);

	if (!(status & PCI_EXP_DPC_STATUS_INTERRUPT) || PCI_POSSIBLE_ERROR(status))
		return IRQ_NONE;

	pci_write_config_word(pdev, cap + PCI_EXP_DPC_STATUS,
			      PCI_EXP_DPC_STATUS_INTERRUPT);
	if (status & PCI_EXP_DPC_STATUS_TRIGGER)
		return IRQ_WAKE_THREAD;
	return IRQ_HANDLED;
}

void pci_dpc_init(struct pci_dev *pdev)
{
	u16 cap;

	pdev->dpc_cap = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_DPC);
	if (!pdev->dpc_cap)
		return;

	pci_read_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CAP, &cap);
	if (!(cap & PCI_EXP_DPC_CAP_RP_EXT))
		return;

	pdev->dpc_rp_extensions = true;

	/* Quirks may set dpc_rp_log_size if device or firmware is buggy */
	if (!pdev->dpc_rp_log_size) {
		pdev->dpc_rp_log_size =
			(cap & PCI_EXP_DPC_RP_PIO_LOG_SIZE) >> 8;
		if (pdev->dpc_rp_log_size < 4 || pdev->dpc_rp_log_size > 9) {
			pci_err(pdev, "RP PIO log size %u is invalid\n",
				pdev->dpc_rp_log_size);
			pdev->dpc_rp_log_size = 0;
		}
	}
}

#define FLAG(x, y) (((x) & (y)) ? '+' : '-')
static int dpc_probe(struct pcie_device *dev)
{
	struct pci_dev *pdev = dev->port;
	struct device *device = &dev->device;
	int status;
	u16 ctl, cap;

	if (!pcie_aer_is_native(pdev) && !pcie_ports_dpc_native)
		return -ENOTSUPP;

	status = devm_request_threaded_irq(device, dev->irq, dpc_irq,
					   dpc_handler, IRQF_SHARED,
					   "pcie-dpc", pdev);
	if (status) {
		pci_warn(pdev, "request IRQ%d failed: %d\n", dev->irq,
			 status);
		return status;
	}

	pci_read_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CAP, &cap);
	pci_read_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CTL, &ctl);

	ctl = (ctl & 0xfff4) | PCI_EXP_DPC_CTL_EN_FATAL | PCI_EXP_DPC_CTL_INT_EN;
	pci_write_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CTL, ctl);
	pci_info(pdev, "enabled with IRQ %d\n", dev->irq);

	pci_info(pdev, "error containment capabilities: Int Msg #%d, RPExt%c PoisonedTLP%c SwTrigger%c RP PIO Log %d, DL_ActiveErr%c\n",
		 cap & PCI_EXP_DPC_IRQ, FLAG(cap, PCI_EXP_DPC_CAP_RP_EXT),
		 FLAG(cap, PCI_EXP_DPC_CAP_POISONED_TLP),
		 FLAG(cap, PCI_EXP_DPC_CAP_SW_TRIGGER), pdev->dpc_rp_log_size,
		 FLAG(cap, PCI_EXP_DPC_CAP_DL_ACTIVE));

	pci_add_ext_cap_save_buffer(pdev, PCI_EXT_CAP_ID_DPC, sizeof(u16));
	return status;
}

static void dpc_remove(struct pcie_device *dev)
{
	struct pci_dev *pdev = dev->port;
	u16 ctl;

	pci_read_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CTL, &ctl);
	ctl &= ~(PCI_EXP_DPC_CTL_EN_FATAL | PCI_EXP_DPC_CTL_INT_EN);
	pci_write_config_word(pdev, pdev->dpc_cap + PCI_EXP_DPC_CTL, ctl);
}

static struct pcie_port_service_driver dpcdriver = {
	.name		= "dpc",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_DPC,
	.probe		= dpc_probe,
	.remove		= dpc_remove,
};

int __init pcie_dpc_init(void)
{
	return pcie_port_service_register(&dpcdriver);
}
