// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Error Disconnect Recover support
 * Author: Kuppuswamy Sathyanarayanan <sathyanarayanan.kuppuswamy@linux.intel.com>
 *
 * Copyright (C) 2020 Intel Corp.
 */

#define dev_fmt(fmt) "EDR: " fmt

#include <linux/pci.h>
#include <linux/pci-acpi.h>

#include "portdrv.h"
#include "../pci.h"

#define EDR_PORT_DPC_ENABLE_DSM		0x0C
#define EDR_PORT_LOCATE_DSM		0x0D
#define EDR_OST_SUCCESS			0x80
#define EDR_OST_FAILED			0x81

/*
 * _DSM wrapper function to enable/disable DPC
 * @pdev   : PCI device structure
 *
 * returns 0 on success or errno on failure.
 */
static int acpi_enable_dpc(struct pci_dev *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	union acpi_object *obj, argv4, req;
	int status = 0;

	/*
	 * Behavior when calling unsupported _DSM functions is undefined,
	 * so check whether EDR_PORT_DPC_ENABLE_DSM is supported.
	 */
	if (!acpi_check_dsm(adev->handle, &pci_acpi_dsm_guid, 5,
			    1ULL << EDR_PORT_DPC_ENABLE_DSM))
		return 0;

	req.type = ACPI_TYPE_INTEGER;
	req.integer.value = 1;

	argv4.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = 1;
	argv4.package.elements = &req;

	/*
	 * Per Downstream Port Containment Related Enhancements ECN to PCI
	 * Firmware Specification r3.2, sec 4.6.12, EDR_PORT_DPC_ENABLE_DSM is
	 * optional.  Return success if it's not implemented.
	 */
	obj = acpi_evaluate_dsm(adev->handle, &pci_acpi_dsm_guid, 5,
				EDR_PORT_DPC_ENABLE_DSM, &argv4);
	if (!obj)
		return 0;

	if (obj->type != ACPI_TYPE_INTEGER) {
		pci_err(pdev, FW_BUG "Enable DPC _DSM returned non integer\n");
		status = -EIO;
	}

	if (obj->integer.value != 1) {
		pci_err(pdev, "Enable DPC _DSM failed to enable DPC\n");
		status = -EIO;
	}

	ACPI_FREE(obj);

	return status;
}

/*
 * _DSM wrapper function to locate DPC port
 * @pdev   : Device which received EDR event
 *
 * Returns pci_dev or NULL.  Caller is responsible for dropping a reference
 * on the returned pci_dev with pci_dev_put().
 */
static struct pci_dev *acpi_dpc_port_get(struct pci_dev *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	union acpi_object *obj;
	u16 port;

	/*
	 * Behavior when calling unsupported _DSM functions is undefined,
	 * so check whether EDR_PORT_DPC_ENABLE_DSM is supported.
	 */
	if (!acpi_check_dsm(adev->handle, &pci_acpi_dsm_guid, 5,
			    1ULL << EDR_PORT_LOCATE_DSM))
		return pci_dev_get(pdev);

	obj = acpi_evaluate_dsm(adev->handle, &pci_acpi_dsm_guid, 5,
				EDR_PORT_LOCATE_DSM, NULL);
	if (!obj)
		return pci_dev_get(pdev);

	if (obj->type != ACPI_TYPE_INTEGER) {
		ACPI_FREE(obj);
		pci_err(pdev, FW_BUG "Locate Port _DSM returned non integer\n");
		return NULL;
	}

	/*
	 * Firmware returns DPC port BDF details in following format:
	 *	15:8 = bus
	 *	 7:3 = device
	 *	 2:0 = function
	 */
	port = obj->integer.value;

	ACPI_FREE(obj);

	return pci_get_domain_bus_and_slot(pci_domain_nr(pdev->bus),
					   PCI_BUS_NUM(port), port & 0xff);
}

/*
 * _OST wrapper function to let firmware know the status of EDR event
 * @pdev   : Device used to send _OST
 * @edev   : Device which experienced EDR event
 * @status : Status of EDR event
 */
static int acpi_send_edr_status(struct pci_dev *pdev, struct pci_dev *edev,
				u16 status)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	u32 ost_status;

	pci_dbg(pdev, "Status for %s: %#x\n", pci_name(edev), status);

	ost_status = PCI_DEVID(edev->bus->number, edev->devfn) << 16;
	ost_status |= status;

	status = acpi_evaluate_ost(adev->handle, ACPI_NOTIFY_DISCONNECT_RECOVER,
				   ost_status, NULL);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	return 0;
}

static void edr_handle_event(acpi_handle handle, u32 event, void *data)
{
	struct pci_dev *pdev = data, *edev;
	pci_ers_result_t estate = PCI_ERS_RESULT_DISCONNECT;
	u16 status;

	pci_info(pdev, "ACPI event %#x received\n", event);

	if (event != ACPI_NOTIFY_DISCONNECT_RECOVER)
		return;

	/* Locate the port which issued EDR event */
	edev = acpi_dpc_port_get(pdev);
	if (!edev) {
		pci_err(pdev, "Firmware failed to locate DPC port\n");
		return;
	}

	pci_dbg(pdev, "Reported EDR dev: %s\n", pci_name(edev));

	/* If port does not support DPC, just send the OST */
	if (!edev->dpc_cap) {
		pci_err(edev, FW_BUG "This device doesn't support DPC\n");
		goto send_ost;
	}

	/* Check if there is a valid DPC trigger */
	pci_read_config_word(edev, edev->dpc_cap + PCI_EXP_DPC_STATUS, &status);
	if (!(status & PCI_EXP_DPC_STATUS_TRIGGER)) {
		pci_err(edev, "Invalid DPC trigger %#010x\n", status);
		goto send_ost;
	}

	dpc_process_error(edev);
	pci_aer_raw_clear_status(edev);

	/*
	 * Irrespective of whether the DPC event is triggered by ERR_FATAL
	 * or ERR_NONFATAL, since the link is already down, use the FATAL
	 * error recovery path for both cases.
	 */
	estate = pcie_do_recovery(edev, pci_channel_io_frozen, dpc_reset_link);

send_ost:

	/*
	 * If recovery is successful, send _OST(0xF, BDF << 16 | 0x80)
	 * to firmware. If not successful, send _OST(0xF, BDF << 16 | 0x81).
	 */
	if (estate == PCI_ERS_RESULT_RECOVERED) {
		pci_dbg(edev, "DPC port successfully recovered\n");
		acpi_send_edr_status(pdev, edev, EDR_OST_SUCCESS);
	} else {
		pci_dbg(edev, "DPC port recovery failed\n");
		acpi_send_edr_status(pdev, edev, EDR_OST_FAILED);
	}

	pci_dev_put(edev);
}

void pci_acpi_add_edr_notifier(struct pci_dev *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	acpi_status status;

	if (!adev) {
		pci_dbg(pdev, "No valid ACPI node, skipping EDR init\n");
		return;
	}

	status = acpi_install_notify_handler(adev->handle, ACPI_SYSTEM_NOTIFY,
					     edr_handle_event, pdev);
	if (ACPI_FAILURE(status)) {
		pci_err(pdev, "Failed to install notify handler\n");
		return;
	}

	if (acpi_enable_dpc(pdev))
		acpi_remove_notify_handler(adev->handle, ACPI_SYSTEM_NOTIFY,
					   edr_handle_event);
	else
		pci_dbg(pdev, "Notify handler installed\n");
}

void pci_acpi_remove_edr_notifier(struct pci_dev *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);

	if (!adev)
		return;

	acpi_remove_notify_handler(adev->handle, ACPI_SYSTEM_NOTIFY,
				   edr_handle_event);
	pci_dbg(pdev, "Notify handler removed\n");
}
