// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"

#include "efct_hw.h"
#include "efct_unsol.h"
#include "efct_scsi.h"

LIST_HEAD(efct_devices);

static int logmask;
module_param(logmask, int, 0444);
MODULE_PARM_DESC(logmask, "logging bitmask (default 0)");

static struct libefc_function_template efct_libefc_templ = {
	.issue_mbox_rqst = efct_issue_mbox_rqst,
	.send_els = efct_els_hw_srrs_send,
	.send_bls = efct_efc_bls_send,

	.new_nport = efct_scsi_tgt_new_nport,
	.del_nport = efct_scsi_tgt_del_nport,
	.scsi_new_node = efct_scsi_new_initiator,
	.scsi_del_node = efct_scsi_del_initiator,
	.hw_seq_free = efct_efc_hw_sequence_free,
};

static int
efct_device_init(void)
{
	int rc;

	/* driver-wide init for target-server */
	rc = efct_scsi_tgt_driver_init();
	if (rc) {
		pr_err("efct_scsi_tgt_init failed rc=%d\n", rc);
		return rc;
	}

	rc = efct_scsi_reg_fc_transport();
	if (rc) {
		efct_scsi_tgt_driver_exit();
		pr_err("failed to register to FC host\n");
		return rc;
	}

	return 0;
}

static void
efct_device_shutdown(void)
{
	efct_scsi_release_fc_transport();

	efct_scsi_tgt_driver_exit();
}

static void *
efct_device_alloc(u32 nid)
{
	struct efct *efct = NULL;

	efct = kzalloc_node(sizeof(*efct), GFP_KERNEL, nid);
	if (!efct)
		return efct;

	INIT_LIST_HEAD(&efct->list_entry);
	list_add_tail(&efct->list_entry, &efct_devices);

	return efct;
}

static void
efct_teardown_msix(struct efct *efct)
{
	u32 i;

	for (i = 0; i < efct->n_msix_vec; i++) {
		free_irq(pci_irq_vector(efct->pci, i),
			 &efct->intr_context[i]);
	}

	pci_free_irq_vectors(efct->pci);
}

static int
efct_efclib_config(struct efct *efct, struct libefc_function_template *tt)
{
	struct efc *efc;
	struct sli4 *sli;
	int rc = 0;

	efc = kzalloc(sizeof(*efc), GFP_KERNEL);
	if (!efc)
		return -ENOMEM;

	efct->efcport = efc;

	memcpy(&efc->tt, tt, sizeof(*tt));
	efc->base = efct;
	efc->pci = efct->pci;

	efc->def_wwnn = efct_get_wwnn(&efct->hw);
	efc->def_wwpn = efct_get_wwpn(&efct->hw);
	efc->enable_tgt = 1;
	efc->log_level = EFC_LOG_LIB;

	sli = &efct->hw.sli;
	efc->max_xfer_size = sli->sge_supported_length *
			     sli_get_max_sgl(&efct->hw.sli);
	efc->sli = sli;
	efc->fcfi = efct->hw.fcf_indicator;

	rc = efcport_init(efc);
	if (rc)
		efc_log_err(efc, "efcport_init failed\n");

	return rc;
}

static int efct_request_firmware_update(struct efct *efct);

static const char*
efct_pci_model(u16 device)
{
	switch (device) {
	case EFCT_DEVICE_LANCER_G6:	return "LPE31004";
	case EFCT_DEVICE_LANCER_G7:	return "LPE36000";
	default:			return "unknown";
	}
}

static int
efct_device_attach(struct efct *efct)
{
	u32 rc = 0, i = 0;

	if (efct->attached) {
		efc_log_err(efct, "Device is already attached\n");
		return -EIO;
	}

	snprintf(efct->name, sizeof(efct->name), "[%s%d] ", "fc",
		 efct->instance_index);

	efct->logmask = logmask;
	efct->filter_def = EFCT_DEFAULT_FILTER;
	efct->max_isr_time_msec = EFCT_OS_MAX_ISR_TIME_MSEC;

	efct->model = efct_pci_model(efct->pci->device);

	efct->efct_req_fw_upgrade = true;

	/* Allocate transport object and bring online */
	efct->xport = efct_xport_alloc(efct);
	if (!efct->xport) {
		efc_log_err(efct, "failed to allocate transport object\n");
		rc = -ENOMEM;
		goto out;
	}

	rc = efct_xport_attach(efct->xport);
	if (rc) {
		efc_log_err(efct, "failed to attach transport object\n");
		goto xport_out;
	}

	rc = efct_xport_initialize(efct->xport);
	if (rc) {
		efc_log_err(efct, "failed to initialize transport object\n");
		goto xport_out;
	}

	rc = efct_efclib_config(efct, &efct_libefc_templ);
	if (rc) {
		efc_log_err(efct, "failed to init efclib\n");
		goto efclib_out;
	}

	for (i = 0; i < efct->n_msix_vec; i++) {
		efc_log_debug(efct, "irq %d enabled\n", i);
		enable_irq(pci_irq_vector(efct->pci, i));
	}

	efct->attached = true;

	if (efct->efct_req_fw_upgrade)
		efct_request_firmware_update(efct);

	return rc;

efclib_out:
	efct_xport_detach(efct->xport);
xport_out:
	efct_xport_free(efct->xport);
	efct->xport = NULL;
out:
	return rc;
}

static int
efct_device_detach(struct efct *efct)
{
	int i;

	if (!efct || !efct->attached) {
		pr_err("Device is not attached\n");
		return -EIO;
	}

	if (efct_xport_control(efct->xport, EFCT_XPORT_SHUTDOWN))
		efc_log_err(efct, "Transport Shutdown timed out\n");

	for (i = 0; i < efct->n_msix_vec; i++)
		disable_irq(pci_irq_vector(efct->pci, i));

	efct_xport_detach(efct->xport);

	efct_xport_free(efct->xport);
	efct->xport = NULL;

	efcport_destroy(efct->efcport);
	kfree(efct->efcport);

	efct->attached = false;

	return 0;
}

static void
efct_fw_write_cb(int status, u32 actual_write_length,
		 u32 change_status, void *arg)
{
	struct efct_fw_write_result *result = arg;

	result->status = status;
	result->actual_xfer = actual_write_length;
	result->change_status = change_status;

	complete(&result->done);
}

static int
efct_firmware_write(struct efct *efct, const u8 *buf, size_t buf_len,
		    u8 *change_status)
{
	int rc = 0;
	u32 bytes_left;
	u32 xfer_size;
	u32 offset;
	struct efc_dma dma;
	int last = 0;
	struct efct_fw_write_result result;

	init_completion(&result.done);

	bytes_left = buf_len;
	offset = 0;

	dma.size = FW_WRITE_BUFSIZE;
	dma.virt = dma_alloc_coherent(&efct->pci->dev,
				      dma.size, &dma.phys, GFP_KERNEL);
	if (!dma.virt)
		return -ENOMEM;

	while (bytes_left > 0) {
		if (bytes_left > FW_WRITE_BUFSIZE)
			xfer_size = FW_WRITE_BUFSIZE;
		else
			xfer_size = bytes_left;

		memcpy(dma.virt, buf + offset, xfer_size);

		if (bytes_left == xfer_size)
			last = 1;

		efct_hw_firmware_write(&efct->hw, &dma, xfer_size, offset,
				       last, efct_fw_write_cb, &result);

		if (wait_for_completion_interruptible(&result.done) != 0) {
			rc = -ENXIO;
			break;
		}

		if (result.actual_xfer == 0 || result.status != 0) {
			rc = -EFAULT;
			break;
		}

		if (last)
			*change_status = result.change_status;

		bytes_left -= result.actual_xfer;
		offset += result.actual_xfer;
	}

	dma_free_coherent(&efct->pci->dev, dma.size, dma.virt, dma.phys);
	return rc;
}

static int
efct_fw_reset(struct efct *efct)
{
	/*
	 * Firmware reset to activate the new firmware.
	 * Function 0 will update and load the new firmware
	 * during attach.
	 */
	if (timer_pending(&efct->xport->stats_timer))
		timer_delete(&efct->xport->stats_timer);

	if (efct_hw_reset(&efct->hw, EFCT_HW_RESET_FIRMWARE)) {
		efc_log_info(efct, "failed to reset firmware\n");
		return -EIO;
	}

	efc_log_info(efct, "successfully reset firmware.Now resetting port\n");

	efct_device_detach(efct);
	return efct_device_attach(efct);
}

static int
efct_request_firmware_update(struct efct *efct)
{
	int rc = 0;
	u8 file_name[256], fw_change_status = 0;
	const struct firmware *fw;
	struct efct_hw_grp_hdr *fw_image;

	snprintf(file_name, 256, "%s.grp", efct->model);

	rc = request_firmware(&fw, file_name, &efct->pci->dev);
	if (rc) {
		efc_log_debug(efct, "Firmware file(%s) not found.\n", file_name);
		return rc;
	}

	fw_image = (struct efct_hw_grp_hdr *)fw->data;

	if (!strncmp(efct->hw.sli.fw_name[0], fw_image->revision,
		     strnlen(fw_image->revision, 16))) {
		efc_log_debug(efct,
			      "Skip update. Firmware is already up to date.\n");
		goto exit;
	}

	efc_log_info(efct, "Firmware update is initiated. %s -> %s\n",
		     efct->hw.sli.fw_name[0], fw_image->revision);

	rc = efct_firmware_write(efct, fw->data, fw->size, &fw_change_status);
	if (rc) {
		efc_log_err(efct, "Firmware update failed. rc = %d\n", rc);
		goto exit;
	}

	efc_log_info(efct, "Firmware updated successfully\n");
	switch (fw_change_status) {
	case 0x00:
		efc_log_info(efct, "New firmware is active.\n");
		break;
	case 0x01:
		efc_log_info(efct,
			"System reboot needed to activate the new firmware\n");
		break;
	case 0x02:
	case 0x03:
		efc_log_info(efct,
			     "firmware reset to activate the new firmware\n");
		efct_fw_reset(efct);
		break;
	default:
		efc_log_info(efct, "Unexpected value change_status:%d\n",
			     fw_change_status);
		break;
	}

exit:
	release_firmware(fw);

	return rc;
}

static void
efct_device_free(struct efct *efct)
{
	if (efct) {
		list_del(&efct->list_entry);
		kfree(efct);
	}
}

static int
efct_device_interrupts_required(struct efct *efct)
{
	int rc;

	rc = efct_hw_setup(&efct->hw, efct, efct->pci);
	if (rc < 0)
		return rc;

	return efct->hw.config.n_eq;
}

static irqreturn_t
efct_intr_thread(int irq, void *handle)
{
	struct efct_intr_context *intr_ctx = handle;
	struct efct *efct = intr_ctx->efct;

	efct_hw_process(&efct->hw, intr_ctx->index, efct->max_isr_time_msec);
	return IRQ_HANDLED;
}

static irqreturn_t
efct_intr_msix(int irq, void *handle)
{
	return IRQ_WAKE_THREAD;
}

static int
efct_setup_msix(struct efct *efct, u32 num_intrs)
{
	int rc = 0, i;

	if (!pci_find_capability(efct->pci, PCI_CAP_ID_MSIX)) {
		dev_err(&efct->pci->dev,
			"%s : MSI-X not available\n", __func__);
		return -EIO;
	}

	efct->n_msix_vec = num_intrs;

	rc = pci_alloc_irq_vectors(efct->pci, num_intrs, num_intrs,
				   PCI_IRQ_MSIX | PCI_IRQ_AFFINITY);

	if (rc < 0) {
		dev_err(&efct->pci->dev, "Failed to alloc irq : %d\n", rc);
		return rc;
	}

	for (i = 0; i < num_intrs; i++) {
		struct efct_intr_context *intr_ctx = NULL;

		intr_ctx = &efct->intr_context[i];
		intr_ctx->efct = efct;
		intr_ctx->index = i;

		rc = request_threaded_irq(pci_irq_vector(efct->pci, i),
					  efct_intr_msix, efct_intr_thread, 0,
					  EFCT_DRIVER_NAME, intr_ctx);
		if (rc) {
			dev_err(&efct->pci->dev,
				"Failed to register %d vector: %d\n", i, rc);
			goto out;
		}
	}

	return rc;

out:
	while (--i >= 0)
		free_irq(pci_irq_vector(efct->pci, i),
			 &efct->intr_context[i]);

	pci_free_irq_vectors(efct->pci);
	return rc;
}

static const struct pci_device_id efct_pci_table[] = {
	{PCI_DEVICE(EFCT_VENDOR_ID, EFCT_DEVICE_LANCER_G6), 0},
	{PCI_DEVICE(EFCT_VENDOR_ID, EFCT_DEVICE_LANCER_G7), 0},
	{}	/* terminate list */
};

static int
efct_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct efct *efct = NULL;
	int rc;
	u32 i, r;
	int num_interrupts = 0;
	int nid;

	dev_info(&pdev->dev, "%s\n", EFCT_DRIVER_NAME);

	rc = pci_enable_device_mem(pdev);
	if (rc)
		return rc;

	pci_set_master(pdev);

	rc = pci_set_mwi(pdev);
	if (rc) {
		dev_info(&pdev->dev, "pci_set_mwi returned %d\n", rc);
		goto mwi_out;
	}

	rc = pci_request_regions(pdev, EFCT_DRIVER_NAME);
	if (rc) {
		dev_err(&pdev->dev, "pci_request_regions failed %d\n", rc);
		goto req_regions_out;
	}

	/* Fetch the Numa node id for this device */
	nid = dev_to_node(&pdev->dev);
	if (nid < 0) {
		dev_err(&pdev->dev, "Warning Numa node ID is %d\n", nid);
		nid = 0;
	}

	/* Allocate efct */
	efct = efct_device_alloc(nid);
	if (!efct) {
		dev_err(&pdev->dev, "Failed to allocate efct\n");
		rc = -ENOMEM;
		goto alloc_out;
	}

	efct->pci = pdev;
	efct->numa_node = nid;

	/* Map all memory BARs */
	for (i = 0, r = 0; i < EFCT_PCI_MAX_REGS; i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			efct->reg[r] = ioremap(pci_resource_start(pdev, i),
					       pci_resource_len(pdev, i));
			r++;
		}

		/*
		 * If the 64-bit attribute is set, both this BAR and the
		 * next form the complete address. Skip processing the
		 * next BAR.
		 */
		if (pci_resource_flags(pdev, i) & IORESOURCE_MEM_64)
			i++;
	}

	pci_set_drvdata(pdev, efct);

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "setting DMA_BIT_MASK failed\n");
		goto dma_mask_out;
	}

	num_interrupts = efct_device_interrupts_required(efct);
	if (num_interrupts < 0) {
		efc_log_err(efct, "efct_device_interrupts_required failed\n");
		rc = -1;
		goto dma_mask_out;
	}

	/*
	 * Initialize MSIX interrupts, note,
	 * efct_setup_msix() enables the interrupt
	 */
	rc = efct_setup_msix(efct, num_interrupts);
	if (rc) {
		dev_err(&pdev->dev, "Can't setup msix\n");
		goto dma_mask_out;
	}
	/* Disable interrupt for now */
	for (i = 0; i < efct->n_msix_vec; i++) {
		efc_log_debug(efct, "irq %d disabled\n", i);
		disable_irq(pci_irq_vector(efct->pci, i));
	}

	rc = efct_device_attach(efct);
	if (rc)
		goto attach_out;

	return 0;

attach_out:
	efct_teardown_msix(efct);
dma_mask_out:
	pci_set_drvdata(pdev, NULL);

	for (i = 0; i < EFCT_PCI_MAX_REGS; i++) {
		if (efct->reg[i])
			iounmap(efct->reg[i]);
	}
	efct_device_free(efct);
alloc_out:
	pci_release_regions(pdev);
req_regions_out:
	pci_clear_mwi(pdev);
mwi_out:
	pci_disable_device(pdev);
	return rc;
}

static void
efct_pci_remove(struct pci_dev *pdev)
{
	struct efct *efct = pci_get_drvdata(pdev);
	u32 i;

	if (!efct)
		return;

	efct_device_detach(efct);

	efct_teardown_msix(efct);

	for (i = 0; i < EFCT_PCI_MAX_REGS; i++) {
		if (efct->reg[i])
			iounmap(efct->reg[i]);
	}

	pci_set_drvdata(pdev, NULL);

	efct_device_free(efct);

	pci_release_regions(pdev);

	pci_disable_device(pdev);
}

static void
efct_device_prep_for_reset(struct efct *efct, struct pci_dev *pdev)
{
	if (efct) {
		efc_log_debug(efct,
			      "PCI channel disable preparing for reset\n");
		efct_device_detach(efct);
		/* Disable interrupt and pci device */
		efct_teardown_msix(efct);
	}
	pci_disable_device(pdev);
}

static void
efct_device_prep_for_recover(struct efct *efct)
{
	if (efct) {
		efc_log_debug(efct, "PCI channel preparing for recovery\n");
		efct_hw_io_abort_all(&efct->hw);
	}
}

/**
 * efct_pci_io_error_detected - method for handling PCI I/O error
 * @pdev: pointer to PCI device.
 * @state: the current PCI connection state.
 *
 * This routine is registered to the PCI subsystem for error handling. This
 * function is called by the PCI subsystem after a PCI bus error affecting
 * this device has been detected. When this routine is invoked, it dispatches
 * device error detected handling routine, which will perform the proper
 * error detected operation.
 *
 * Return codes
 * PCI_ERS_RESULT_NEED_RESET - need to reset before recovery
 * PCI_ERS_RESULT_DISCONNECT - device could not be recovered
 */
static pci_ers_result_t
efct_pci_io_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct efct *efct = pci_get_drvdata(pdev);
	pci_ers_result_t rc;

	switch (state) {
	case pci_channel_io_normal:
		efct_device_prep_for_recover(efct);
		rc = PCI_ERS_RESULT_CAN_RECOVER;
		break;
	case pci_channel_io_frozen:
		efct_device_prep_for_reset(efct, pdev);
		rc = PCI_ERS_RESULT_NEED_RESET;
		break;
	case pci_channel_io_perm_failure:
		efct_device_detach(efct);
		rc = PCI_ERS_RESULT_DISCONNECT;
		break;
	default:
		efc_log_debug(efct, "Unknown PCI error state:0x%x\n", state);
		efct_device_prep_for_reset(efct, pdev);
		rc = PCI_ERS_RESULT_NEED_RESET;
		break;
	}

	return rc;
}

static pci_ers_result_t
efct_pci_io_slot_reset(struct pci_dev *pdev)
{
	int rc;
	struct efct *efct = pci_get_drvdata(pdev);

	rc = pci_enable_device_mem(pdev);
	if (rc) {
		efc_log_err(efct, "failed to enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/*
	 * As the new kernel behavior of pci_restore_state() API call clears
	 * device saved_state flag, need to save the restored state again.
	 */

	pci_save_state(pdev);

	pci_set_master(pdev);

	rc = efct_setup_msix(efct, efct->n_msix_vec);
	if (rc)
		efc_log_err(efct, "rc %d returned, IRQ allocation failed\n",
			    rc);

	/* Perform device reset */
	efct_device_detach(efct);
	/* Bring device to online*/
	efct_device_attach(efct);

	return PCI_ERS_RESULT_RECOVERED;
}

static void
efct_pci_io_resume(struct pci_dev *pdev)
{
	struct efct *efct = pci_get_drvdata(pdev);

	/* Perform device reset */
	efct_device_detach(efct);
	/* Bring device to online*/
	efct_device_attach(efct);
}

MODULE_DEVICE_TABLE(pci, efct_pci_table);

static const struct pci_error_handlers efct_pci_err_handler = {
	.error_detected = efct_pci_io_error_detected,
	.slot_reset = efct_pci_io_slot_reset,
	.resume = efct_pci_io_resume,
};

static struct pci_driver efct_pci_driver = {
	.name		= EFCT_DRIVER_NAME,
	.id_table	= efct_pci_table,
	.probe		= efct_pci_probe,
	.remove		= efct_pci_remove,
	.err_handler	= &efct_pci_err_handler,
};

static
int __init efct_init(void)
{
	int rc;

	rc = efct_device_init();
	if (rc) {
		pr_err("efct_device_init failed rc=%d\n", rc);
		return rc;
	}

	rc = pci_register_driver(&efct_pci_driver);
	if (rc) {
		pr_err("pci_register_driver failed rc=%d\n", rc);
		efct_device_shutdown();
	}

	return rc;
}

static void __exit efct_exit(void)
{
	pci_unregister_driver(&efct_pci_driver);
	efct_device_shutdown();
}

module_init(efct_init);
module_exit(efct_exit);
MODULE_VERSION(EFCT_DRIVER_VERSION);
MODULE_DESCRIPTION("Emulex Fibre Channel Target driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Broadcom");
