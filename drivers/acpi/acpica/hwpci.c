/*******************************************************************************
 *
 * Module Name: hwpci - Obtain PCI bus, device, and function numbers
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("hwpci")

/* PCI configuration space values */
#define PCI_CFG_HEADER_TYPE_REG             0x0E
#define PCI_CFG_PRIMARY_BUS_NUMBER_REG      0x18
#define PCI_CFG_SECONDARY_BUS_NUMBER_REG    0x19
/* PCI header values */
#define PCI_HEADER_TYPE_MASK                0x7F
#define PCI_TYPE_BRIDGE                     0x01
#define PCI_TYPE_CARDBUS_BRIDGE             0x02
typedef struct acpi_pci_device {
	acpi_handle device;
	struct acpi_pci_device *next;

} acpi_pci_device;

/* Local prototypes */

static acpi_status
acpi_hw_build_pci_list(acpi_handle root_pci_device,
		       acpi_handle pci_region,
		       struct acpi_pci_device **return_list_head);

static acpi_status
acpi_hw_process_pci_list(struct acpi_pci_id *pci_id,
			 struct acpi_pci_device *list_head);

static void acpi_hw_delete_pci_list(struct acpi_pci_device *list_head);

static acpi_status
acpi_hw_get_pci_device_info(struct acpi_pci_id *pci_id,
			    acpi_handle pci_device,
			    u16 *bus_number, u8 *is_bridge);

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_derive_pci_id
 *
 * PARAMETERS:  pci_id              - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              root_pci_device     - A handle to a PCI device object. This
 *                                    object must be a PCI Root Bridge having a
 *                                    _HID value of either PNP0A03 or PNP0A08
 *              pci_region          - A handle to a PCI configuration space
 *                                    Operation Region being initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function derives a full PCI ID for a PCI device,
 *              consisting of a Segment number, Bus number, Device number,
 *              and function code.
 *
 *              The PCI hardware dynamically configures PCI bus numbers
 *              depending on the bus topology discovered during system
 *              initialization. This function is invoked during configuration
 *              of a PCI_Config Operation Region in order to (possibly) update
 *              the Bus/Device/Function numbers in the pci_id with the actual
 *              values as determined by the hardware and operating system
 *              configuration.
 *
 *              The pci_id parameter is initially populated during the Operation
 *              Region initialization. This function is then called, and is
 *              will make any necessary modifications to the Bus, Device, or
 *              Function number PCI ID subfields as appropriate for the
 *              current hardware and OS configuration.
 *
 * NOTE:        Created 08/2010. Replaces the previous OSL acpi_os_derive_pci_id
 *              interface since this feature is OS-independent. This module
 *              specifically avoids any use of recursion by building a local
 *              temporary device list.
 *
 ******************************************************************************/

acpi_status
acpi_hw_derive_pci_id(struct acpi_pci_id *pci_id,
		      acpi_handle root_pci_device, acpi_handle pci_region)
{
	acpi_status status;
	struct acpi_pci_device *list_head;

	ACPI_FUNCTION_TRACE(hw_derive_pci_id);

	if (!pci_id) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Build a list of PCI devices, from pci_region up to root_pci_device */

	status =
	    acpi_hw_build_pci_list(root_pci_device, pci_region, &list_head);
	if (ACPI_SUCCESS(status)) {

		/* Walk the list, updating the PCI device/function/bus numbers */

		status = acpi_hw_process_pci_list(pci_id, list_head);

		/* Delete the list */

		acpi_hw_delete_pci_list(list_head);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_build_pci_list
 *
 * PARAMETERS:  root_pci_device     - A handle to a PCI device object. This
 *                                    object is guaranteed to be a PCI Root
 *                                    Bridge having a _HID value of either
 *                                    PNP0A03 or PNP0A08
 *              pci_region          - A handle to the PCI configuration space
 *                                    Operation Region
 *              return_list_head    - Where the PCI device list is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Builds a list of devices from the input PCI region up to the
 *              Root PCI device for this namespace subtree.
 *
 ******************************************************************************/

static acpi_status
acpi_hw_build_pci_list(acpi_handle root_pci_device,
		       acpi_handle pci_region,
		       struct acpi_pci_device **return_list_head)
{
	acpi_handle current_device;
	acpi_handle parent_device;
	acpi_status status;
	struct acpi_pci_device *list_element;

	/*
	 * Ascend namespace branch until the root_pci_device is reached, building
	 * a list of device nodes. Loop will exit when either the PCI device is
	 * found, or the root of the namespace is reached.
	 */
	*return_list_head = NULL;
	current_device = pci_region;
	while (1) {
		status = acpi_get_parent(current_device, &parent_device);
		if (ACPI_FAILURE(status)) {

			/* Must delete the list before exit */

			acpi_hw_delete_pci_list(*return_list_head);
			return (status);
		}

		/* Finished when we reach the PCI root device (PNP0A03 or PNP0A08) */

		if (parent_device == root_pci_device) {
			return (AE_OK);
		}

		list_element = ACPI_ALLOCATE(sizeof(struct acpi_pci_device));
		if (!list_element) {

			/* Must delete the list before exit */

			acpi_hw_delete_pci_list(*return_list_head);
			return (AE_NO_MEMORY);
		}

		/* Put new element at the head of the list */

		list_element->next = *return_list_head;
		list_element->device = parent_device;
		*return_list_head = list_element;

		current_device = parent_device;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_process_pci_list
 *
 * PARAMETERS:  pci_id              - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              list_head           - Device list created by
 *                                    acpi_hw_build_pci_list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk downward through the PCI device list, getting the device
 *              info for each, via the PCI configuration space and updating
 *              the PCI ID as necessary. Deletes the list during traversal.
 *
 ******************************************************************************/

static acpi_status
acpi_hw_process_pci_list(struct acpi_pci_id *pci_id,
			 struct acpi_pci_device *list_head)
{
	acpi_status status = AE_OK;
	struct acpi_pci_device *info;
	u16 bus_number;
	u8 is_bridge = TRUE;

	ACPI_FUNCTION_NAME(hw_process_pci_list);

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Input PciId:  Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X\n",
			  pci_id->segment, pci_id->bus, pci_id->device,
			  pci_id->function));

	bus_number = pci_id->bus;

	/*
	 * Descend down the namespace tree, collecting PCI device, function,
	 * and bus numbers. bus_number is only important for PCI bridges.
	 * Algorithm: As we descend the tree, use the last valid PCI device,
	 * function, and bus numbers that are discovered, and assign them
	 * to the PCI ID for the target device.
	 */
	info = list_head;
	while (info) {
		status = acpi_hw_get_pci_device_info(pci_id, info->device,
						     &bus_number, &is_bridge);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		info = info->next;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Output PciId: Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X "
			  "Status %X BusNumber %X IsBridge %X\n",
			  pci_id->segment, pci_id->bus, pci_id->device,
			  pci_id->function, status, bus_number, is_bridge));

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_delete_pci_list
 *
 * PARAMETERS:  list_head           - Device list created by
 *                                    acpi_hw_build_pci_list
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free the entire PCI list.
 *
 ******************************************************************************/

static void acpi_hw_delete_pci_list(struct acpi_pci_device *list_head)
{
	struct acpi_pci_device *next;
	struct acpi_pci_device *previous;

	next = list_head;
	while (next) {
		previous = next;
		next = previous->next;
		ACPI_FREE(previous);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_pci_device_info
 *
 * PARAMETERS:  pci_id              - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              pci_device          - Handle for the PCI device object
 *              bus_number          - Where a PCI bridge bus number is returned
 *              is_bridge           - Return value, indicates if this PCI
 *                                    device is a PCI bridge
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the device info for a single PCI device object. Get the
 *              _ADR (contains PCI device and function numbers), and for PCI
 *              bridge devices, get the bus number from PCI configuration
 *              space.
 *
 ******************************************************************************/

static acpi_status
acpi_hw_get_pci_device_info(struct acpi_pci_id *pci_id,
			    acpi_handle pci_device,
			    u16 *bus_number, u8 *is_bridge)
{
	acpi_status status;
	acpi_object_type object_type;
	u64 return_value;
	u64 pci_value;

	/* We only care about objects of type Device */

	status = acpi_get_type(pci_device, &object_type);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	if (object_type != ACPI_TYPE_DEVICE) {
		return (AE_OK);
	}

	/* We need an _ADR. Ignore device if not present */

	status = acpi_ut_evaluate_numeric_object(METHOD_NAME__ADR,
						 pci_device, &return_value);
	if (ACPI_FAILURE(status)) {
		return (AE_OK);
	}

	/*
	 * From _ADR, get the PCI Device and Function and
	 * update the PCI ID.
	 */
	pci_id->device = ACPI_HIWORD(ACPI_LODWORD(return_value));
	pci_id->function = ACPI_LOWORD(ACPI_LODWORD(return_value));

	/*
	 * If the previous device was a bridge, use the previous
	 * device bus number
	 */
	if (*is_bridge) {
		pci_id->bus = *bus_number;
	}

	/*
	 * Get the bus numbers from PCI Config space:
	 *
	 * First, get the PCI header_type
	 */
	*is_bridge = FALSE;
	status = acpi_os_read_pci_configuration(pci_id,
						PCI_CFG_HEADER_TYPE_REG,
						&pci_value, 8);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* We only care about bridges (1=pci_bridge, 2=card_bus_bridge) */

	pci_value &= PCI_HEADER_TYPE_MASK;

	if ((pci_value != PCI_TYPE_BRIDGE) &&
	    (pci_value != PCI_TYPE_CARDBUS_BRIDGE)) {
		return (AE_OK);
	}

	/* Bridge: Get the Primary bus_number */

	status = acpi_os_read_pci_configuration(pci_id,
						PCI_CFG_PRIMARY_BUS_NUMBER_REG,
						&pci_value, 8);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	*is_bridge = TRUE;
	pci_id->bus = (u16)pci_value;

	/* Bridge: Get the Secondary bus_number */

	status = acpi_os_read_pci_configuration(pci_id,
						PCI_CFG_SECONDARY_BUS_NUMBER_REG,
						&pci_value, 8);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	*bus_number = (u16)pci_value;
	return (AE_OK);
}
