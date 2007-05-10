/******************************************************************************
 *
 * Module Name: evrgnini- ACPI address_space (op_region) init
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2007, R. Byron Moore
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
#include <acpi/acevents.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evrgnini")

/* Local prototypes */
static u8 acpi_ev_match_pci_root_bridge(char *id);

static u8 acpi_ev_is_pci_root_bridge(struct acpi_namespace_node *node);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_system_memory_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a system_memory operation region
 *
 ******************************************************************************/

acpi_status
acpi_ev_system_memory_region_setup(acpi_handle handle,
				   u32 function,
				   void *handler_context, void **region_context)
{
	union acpi_operand_object *region_desc =
	    (union acpi_operand_object *)handle;
	struct acpi_mem_space_context *local_region_context;

	ACPI_FUNCTION_TRACE(ev_system_memory_region_setup);

	if (function == ACPI_REGION_DEACTIVATE) {
		if (*region_context) {
			local_region_context =
			    (struct acpi_mem_space_context *)*region_context;

			/* Delete a cached mapping if present */

			if (local_region_context->mapped_length) {
				acpi_os_unmap_memory(local_region_context->
						     mapped_logical_address,
						     local_region_context->
						     mapped_length);
			}
			ACPI_FREE(local_region_context);
			*region_context = NULL;
		}
		return_ACPI_STATUS(AE_OK);
	}

	/* Create a new context */

	local_region_context =
	    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_mem_space_context));
	if (!(local_region_context)) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Save the region length and address for use in the handler */

	local_region_context->length = region_desc->region.length;
	local_region_context->address = region_desc->region.address;

	*region_context = local_region_context;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_io_space_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a IO operation region
 *
 ******************************************************************************/

acpi_status
acpi_ev_io_space_region_setup(acpi_handle handle,
			      u32 function,
			      void *handler_context, void **region_context)
{
	ACPI_FUNCTION_TRACE(ev_io_space_region_setup);

	if (function == ACPI_REGION_DEACTIVATE) {
		*region_context = NULL;
	} else {
		*region_context = handler_context;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_pci_config_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a PCI_Config operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_pci_config_region_setup(acpi_handle handle,
				u32 function,
				void *handler_context, void **region_context)
{
	acpi_status status = AE_OK;
	acpi_integer pci_value;
	struct acpi_pci_id *pci_id = *region_context;
	union acpi_operand_object *handler_obj;
	struct acpi_namespace_node *parent_node;
	struct acpi_namespace_node *pci_root_node;
	struct acpi_namespace_node *pci_device_node;
	union acpi_operand_object *region_obj =
	    (union acpi_operand_object *)handle;

	ACPI_FUNCTION_TRACE(ev_pci_config_region_setup);

	handler_obj = region_obj->region.handler;
	if (!handler_obj) {
		/*
		 * No installed handler. This shouldn't happen because the dispatch
		 * routine checks before we get here, but we check again just in case.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
				  "Attempting to init a region %p, with no handler\n",
				  region_obj));
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	*region_context = NULL;
	if (function == ACPI_REGION_DEACTIVATE) {
		if (pci_id) {
			ACPI_FREE(pci_id);
		}
		return_ACPI_STATUS(status);
	}

	parent_node = acpi_ns_get_parent_node(region_obj->region.node);

	/*
	 * Get the _SEG and _BBN values from the device upon which the handler
	 * is installed.
	 *
	 * We need to get the _SEG and _BBN objects relative to the PCI BUS device.
	 * This is the device the handler has been registered to handle.
	 */

	/*
	 * If the address_space.Node is still pointing to the root, we need
	 * to scan upward for a PCI Root bridge and re-associate the op_region
	 * handlers with that device.
	 */
	if (handler_obj->address_space.node == acpi_gbl_root_node) {

		/* Start search from the parent object */

		pci_root_node = parent_node;
		while (pci_root_node != acpi_gbl_root_node) {

			/* Get the _HID/_CID in order to detect a root_bridge */

			if (acpi_ev_is_pci_root_bridge(pci_root_node)) {

				/* Install a handler for this PCI root bridge */

				status =
				    acpi_install_address_space_handler((acpi_handle) pci_root_node, ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);
				if (ACPI_FAILURE(status)) {
					if (status == AE_SAME_HANDLER) {
						/*
						 * It is OK if the handler is already installed on the root
						 * bridge.  Still need to return a context object for the
						 * new PCI_Config operation region, however.
						 */
						status = AE_OK;
					} else {
						ACPI_EXCEPTION((AE_INFO, status,
								"Could not install PciConfig handler for Root Bridge %4.4s",
								acpi_ut_get_node_name
								(pci_root_node)));
					}
				}
				break;
			}

			pci_root_node = acpi_ns_get_parent_node(pci_root_node);
		}

		/* PCI root bridge not found, use namespace root node */
	} else {
		pci_root_node = handler_obj->address_space.node;
	}

	/*
	 * If this region is now initialized, we are done.
	 * (install_address_space_handler could have initialized it)
	 */
	if (region_obj->region.flags & AOPOBJ_SETUP_COMPLETE) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Region is still not initialized. Create a new context */

	pci_id = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_pci_id));
	if (!pci_id) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/*
	 * For PCI_Config space access, we need the segment, bus,
	 * device and function numbers.  Acquire them here.
	 *
	 * Find the parent device object. (This allows the operation region to be
	 * within a subscope under the device, such as a control method.)
	 */
	pci_device_node = region_obj->region.node;
	while (pci_device_node && (pci_device_node->type != ACPI_TYPE_DEVICE)) {
		pci_device_node = acpi_ns_get_parent_node(pci_device_node);
	}

	if (!pci_device_node) {
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/*
	 * Get the PCI device and function numbers from the _ADR object
	 * contained in the parent's scope.
	 */
	status =
	    acpi_ut_evaluate_numeric_object(METHOD_NAME__ADR, pci_device_node,
					    &pci_value);

	/*
	 * The default is zero, and since the allocation above zeroed
	 * the data, just do nothing on failure.
	 */
	if (ACPI_SUCCESS(status)) {
		pci_id->device = ACPI_HIWORD(ACPI_LODWORD(pci_value));
		pci_id->function = ACPI_LOWORD(ACPI_LODWORD(pci_value));
	}

	/* The PCI segment number comes from the _SEG method */

	status =
	    acpi_ut_evaluate_numeric_object(METHOD_NAME__SEG, pci_root_node,
					    &pci_value);
	if (ACPI_SUCCESS(status)) {
		pci_id->segment = ACPI_LOWORD(pci_value);
	}

	/* The PCI bus number comes from the _BBN method */

	status =
	    acpi_ut_evaluate_numeric_object(METHOD_NAME__BBN, pci_root_node,
					    &pci_value);
	if (ACPI_SUCCESS(status)) {
		pci_id->bus = ACPI_LOWORD(pci_value);
	}

	/* Complete this device's pci_id */

	acpi_os_derive_pci_id(pci_root_node, region_obj->region.node, &pci_id);

	*region_context = pci_id;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_match_pci_root_bridge
 *
 * PARAMETERS:  Id              - The HID/CID in string format
 *
 * RETURN:      TRUE if the Id is a match for a PCI/PCI-Express Root Bridge
 *
 * DESCRIPTION: Determine if the input ID is a PCI Root Bridge ID.
 *
 ******************************************************************************/

static u8 acpi_ev_match_pci_root_bridge(char *id)
{

	/*
	 * Check if this is a PCI root.
	 * ACPI 3.0+: check for a PCI Express root also.
	 */
	if (!(ACPI_STRNCMP(id,
			   PCI_ROOT_HID_STRING,
			   sizeof(PCI_ROOT_HID_STRING))) ||
	    !(ACPI_STRNCMP(id,
			   PCI_EXPRESS_ROOT_HID_STRING,
			   sizeof(PCI_EXPRESS_ROOT_HID_STRING)))) {
		return (TRUE);
	}

	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_is_pci_root_bridge
 *
 * PARAMETERS:  Node            - Device node being examined
 *
 * RETURN:      TRUE if device is a PCI/PCI-Express Root Bridge
 *
 * DESCRIPTION: Determine if the input device represents a PCI Root Bridge by
 *              examining the _HID and _CID for the device.
 *
 ******************************************************************************/

static u8 acpi_ev_is_pci_root_bridge(struct acpi_namespace_node *node)
{
	acpi_status status;
	struct acpi_device_id hid;
	struct acpi_compatible_id_list *cid;
	acpi_native_uint i;

	/*
	 * Get the _HID and check for a PCI Root Bridge
	 */
	status = acpi_ut_execute_HID(node, &hid);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	if (acpi_ev_match_pci_root_bridge(hid.value)) {
		return (TRUE);
	}

	/*
	 * The _HID did not match.
	 * Get the _CID and check for a PCI Root Bridge
	 */
	status = acpi_ut_execute_CID(node, &cid);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	/* Check all _CIDs in the returned list */

	for (i = 0; i < cid->count; i++) {
		if (acpi_ev_match_pci_root_bridge(cid->id[i].value)) {
			ACPI_FREE(cid);
			return (TRUE);
		}
	}

	ACPI_FREE(cid);
	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_pci_bar_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a pci_bAR operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_pci_bar_region_setup(acpi_handle handle,
			     u32 function,
			     void *handler_context, void **region_context)
{
	ACPI_FUNCTION_TRACE(ev_pci_bar_region_setup);

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_cmos_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a CMOS operation region
 *
 * MUTEX:       Assumes namespace is not locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_cmos_region_setup(acpi_handle handle,
			  u32 function,
			  void *handler_context, void **region_context)
{
	ACPI_FUNCTION_TRACE(ev_cmos_region_setup);

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_default_region_setup
 *
 * PARAMETERS:  Handle              - Region we are interested in
 *              Function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Default region initialization
 *
 ******************************************************************************/

acpi_status
acpi_ev_default_region_setup(acpi_handle handle,
			     u32 function,
			     void *handler_context, void **region_context)
{
	ACPI_FUNCTION_TRACE(ev_default_region_setup);

	if (function == ACPI_REGION_DEACTIVATE) {
		*region_context = NULL;
	} else {
		*region_context = handler_context;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize_region
 *
 * PARAMETERS:  region_obj      - Region we are initializing
 *              acpi_ns_locked  - Is namespace locked?
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific initialization.  For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the definition.  This address is
 *              required to perform an access to PCI config space.
 *
 * MUTEX:       Interpreter should be unlocked, because we may run the _REG
 *              method for this region.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize_region(union acpi_operand_object *region_obj,
			  u8 acpi_ns_locked)
{
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *obj_desc;
	acpi_adr_space_type space_id;
	struct acpi_namespace_node *node;
	acpi_status status;
	struct acpi_namespace_node *method_node;
	acpi_name *reg_name_ptr = (acpi_name *) METHOD_NAME__REG;
	union acpi_operand_object *region_obj2;

	ACPI_FUNCTION_TRACE_U32(ev_initialize_region, acpi_ns_locked);

	if (!region_obj) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (region_obj->common.flags & AOPOBJ_OBJECT_INITIALIZED) {
		return_ACPI_STATUS(AE_OK);
	}

	region_obj2 = acpi_ns_get_secondary_object(region_obj);
	if (!region_obj2) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	node = acpi_ns_get_parent_node(region_obj->region.node);
	space_id = region_obj->region.space_id;

	/* Setup defaults */

	region_obj->region.handler = NULL;
	region_obj2->extra.method_REG = NULL;
	region_obj->common.flags &= ~(AOPOBJ_SETUP_COMPLETE);
	region_obj->common.flags |= AOPOBJ_OBJECT_INITIALIZED;

	/* Find any "_REG" method associated with this region definition */

	status =
	    acpi_ns_search_one_scope(*reg_name_ptr, node, ACPI_TYPE_METHOD,
				     &method_node);
	if (ACPI_SUCCESS(status)) {
		/*
		 * The _REG method is optional and there can be only one per region
		 * definition.  This will be executed when the handler is attached
		 * or removed
		 */
		region_obj2->extra.method_REG = method_node;
	}

	/*
	 * The following loop depends upon the root Node having no parent
	 * ie: acpi_gbl_root_node->parent_entry being set to NULL
	 */
	while (node) {

		/* Check to see if a handler exists */

		handler_obj = NULL;
		obj_desc = acpi_ns_get_attached_object(node);
		if (obj_desc) {

			/* Can only be a handler if the object exists */

			switch (node->type) {
			case ACPI_TYPE_DEVICE:

				handler_obj = obj_desc->device.handler;
				break;

			case ACPI_TYPE_PROCESSOR:

				handler_obj = obj_desc->processor.handler;
				break;

			case ACPI_TYPE_THERMAL:

				handler_obj = obj_desc->thermal_zone.handler;
				break;

			default:
				/* Ignore other objects */
				break;
			}

			while (handler_obj) {

				/* Is this handler of the correct type? */

				if (handler_obj->address_space.space_id ==
				    space_id) {

					/* Found correct handler */

					ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
							  "Found handler %p for region %p in obj %p\n",
							  handler_obj,
							  region_obj,
							  obj_desc));

					status =
					    acpi_ev_attach_region(handler_obj,
								  region_obj,
								  acpi_ns_locked);

					/*
					 * Tell all users that this region is usable by running the _REG
					 * method
					 */
					if (acpi_ns_locked) {
						status =
						    acpi_ut_release_mutex
						    (ACPI_MTX_NAMESPACE);
						if (ACPI_FAILURE(status)) {
							return_ACPI_STATUS
							    (status);
						}
					}

					status =
					    acpi_ev_execute_reg_method
					    (region_obj, 1);

					if (acpi_ns_locked) {
						status =
						    acpi_ut_acquire_mutex
						    (ACPI_MTX_NAMESPACE);
						if (ACPI_FAILURE(status)) {
							return_ACPI_STATUS
							    (status);
						}
					}

					return_ACPI_STATUS(AE_OK);
				}

				/* Try next handler in the list */

				handler_obj = handler_obj->address_space.next;
			}
		}

		/*
		 * This node does not have the handler we need;
		 * Pop up one level
		 */
		node = acpi_ns_get_parent_node(node);
	}

	/* If we get here, there is no handler for this region */

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "No handler for RegionType %s(%X) (RegionObj %p)\n",
			  acpi_ut_get_region_name(space_id), space_id,
			  region_obj));

	return_ACPI_STATUS(AE_NOT_EXIST);
}
