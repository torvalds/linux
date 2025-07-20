// SPDX-License-Identifier: GPL-2.0
/*
 * PCI-related functions used by the EFI stub on multiple
 * architectures.
 *
 * Copyright 2019 Google, LLC
 */

#include <linux/efi.h>
#include <linux/pci.h>

#include <asm/efi.h>

#include "efistub.h"

void efi_pci_disable_bridge_busmaster(void)
{
	efi_guid_t pci_proto = EFI_PCI_IO_PROTOCOL_GUID;
	efi_handle_t *pci_handle __free(efi_pool) = NULL;
	unsigned long pci_handle_num;
	efi_handle_t handle;
	efi_status_t status;
	u16 class, command;

	status = efi_bs_call(locate_handle_buffer, EFI_LOCATE_BY_PROTOCOL,
			     &pci_proto, NULL, &pci_handle_num, &pci_handle);
	if (status != EFI_SUCCESS) {
		efi_err("Failed to locate PCI I/O handles\n");
		return;
	}

	for_each_efi_handle(handle, pci_handle, pci_handle_num) {
		efi_pci_io_protocol_t *pci;
		unsigned long segment_nr, bus_nr, device_nr, func_nr;

		status = efi_bs_call(handle_protocol, handle, &pci_proto,
				     (void **)&pci);
		if (status != EFI_SUCCESS)
			continue;

		/*
		 * Disregard devices living on bus 0 - these are not behind a
		 * bridge so no point in disconnecting them from their drivers.
		 */
		status = efi_call_proto(pci, get_location, &segment_nr, &bus_nr,
					&device_nr, &func_nr);
		if (status != EFI_SUCCESS || bus_nr == 0)
			continue;

		/*
		 * Don't disconnect VGA controllers so we don't risk losing
		 * access to the framebuffer. Drivers for true PCIe graphics
		 * controllers that are behind a PCIe root port do not use
		 * DMA to implement the GOP framebuffer anyway [although they
		 * may use it in their implementation of Gop->Blt()], and so
		 * disabling DMA in the PCI bridge should not interfere with
		 * normal operation of the device.
		 */
		status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
					PCI_CLASS_DEVICE, 1, &class);
		if (status != EFI_SUCCESS || class == PCI_CLASS_DISPLAY_VGA)
			continue;

		/* Disconnect this handle from all its drivers */
		efi_bs_call(disconnect_controller, handle, NULL, NULL);
	}

	for_each_efi_handle(handle, pci_handle, pci_handle_num) {
		efi_pci_io_protocol_t *pci;

		status = efi_bs_call(handle_protocol, handle, &pci_proto,
				     (void **)&pci);
		if (status != EFI_SUCCESS || !pci)
			continue;

		status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
					PCI_CLASS_DEVICE, 1, &class);

		if (status != EFI_SUCCESS || class != PCI_CLASS_BRIDGE_PCI)
			continue;

		/* Disable busmastering */
		status = efi_call_proto(pci, pci.read, EfiPciIoWidthUint16,
					PCI_COMMAND, 1, &command);
		if (status != EFI_SUCCESS || !(command & PCI_COMMAND_MASTER))
			continue;

		command &= ~PCI_COMMAND_MASTER;
		status = efi_call_proto(pci, pci.write, EfiPciIoWidthUint16,
					PCI_COMMAND, 1, &command);
		if (status != EFI_SUCCESS)
			efi_err("Failed to disable PCI busmastering\n");
	}
}
