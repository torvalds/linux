/******************************************************************************
 *
 * Module Name: evrgnini- ACPI address_space (op_region) init
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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
#include "acevents.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evrgnini")

/* Local prototypes */
static u8 acpi_ev_is_pci_root_bridge(struct acpi_namespace_node *node);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_system_memory_region_setup
 *
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
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
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
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
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
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
	u64 pci_value;
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

	parent_node = region_obj->region.node->parent;

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

				status = acpi_install_address_space_handler((acpi_handle)pci_root_node, ACPI_ADR_SPACE_PCI_CONFIG, ACPI_DEFAULT_HANDLER, NULL, NULL);
				if (ACPI_FAILURE(status)) {
					if (status == AE_SAME_HANDLER) {
						/*
						 * It is OK if the handler is already installed on the
						 * root bridge. Still need to return a context object
						 * for the new PCI_Config operation region, however.
						 */
						status = AE_OK;
					} else {
						ACPI_EXCEPTION((AE_INFO, status,
								"Could not install PciConfig handler "
								"for Root Bridge %4.4s",
								acpi_ut_get_node_name
								(pci_root_node)));
					}
				}
				break;
			}

			pci_root_node = pci_root_node->parent;
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
	 * For PCI_Config space access, we need the segment, bus, device and
	 * function numbers. Acquire them here.
	 *
	 * Find the parent device object. (This allows the operation region to be
	 * within a subscope under the device, such as a control method.)
	 */
	pci_device_node = region_obj->region.node;
	while (pci_device_node && (pci_device_node->type != ACPI_TYPE_DEVICE)) {
		pci_device_node = pci_device_node->parent;
	}

	if (!pci_device_node) {
		ACPI_FREE(pci_id);
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/*
	 * Get the PCI device and function numbers from the _ADR object
	 * contained in the parent's scope.
	 */
	status = acpi_ut_evaluate_numeric_object(METHOD_NAME__ADR,
						 pci_device_node, &pci_value);

	/*
	 * The default is zero, and since the allocation above zeroed the data,
	 * just do nothing on failure.
	 */
	if (ACPI_SUCCESS(status)) {
		pci_id->device = ACPI_HIWORD(ACPI_LODWORD(pci_value));
		pci_id->function = ACPI_LOWORD(ACPI_LODWORD(pci_value));
	}

	/* The PCI segment number comes from the _SEG method */

	status = acpi_ut_evaluate_numeric_object(METHOD_NAME__SEG,
						 pci_root_node, &pci_value);
	if (ACPI_SUCCESS(status)) {
		pci_id->segment = ACPI_LOWORD(pci_value);
	}

	/* The PCI bus number comes from the _BBN method */

	status = acpi_ut_evaluate_numeric_object(METHOD_NAME__BBN,
						 pci_root_node, &pci_value);
	if (ACPI_SUCCESS(status)) {
		pci_id->bus = ACPI_LOWORD(pci_value);
	}

	/* Complete/update the PCI ID for this device */

	status =
	    acpi_hw_derive_pci_id(pci_id, pci_root_node,
				  region_obj->region.node);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(pci_id);
		return_ACPI_STATUS(status);
	}

	*region_context = pci_id;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_is_pci_root_bridge
 *
 * PARAMETERS:  node            - Device node being examined
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
	struct acpi_pnp_device_id *hid;
	struct acpi_pnp_device_id_list *cid;
	u32 i;
	u8 match;

	/* Get the _HID and check for a PCI Root Bridge */

	status = acpi_ut_execute_HID(node, &hid);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	match = acpi_ut_is_pci_root_bridge(hid->string);
	ACPI_FREE(hid);

	if (match) {
		return (TRUE);
	}

	/* The _HID did not match. Get the _CID and check for a PCI Root Bridge */

	status = acpi_ut_execute_CID(node, &cid);
	if (ACPI_FAILURE(status)) {
		return (FALSE);
	}

	/* Check all _CIDs in the returned list */

	for (i = 0; i < cid->count; i++) {
		if (acpi_ut_is_pci_root_bridge(cid->ids[i].string)) {
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
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
 *              handler_context     - Address space handler context
 *              region_context      - Region specific context
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Setup a pci_BAR operation region
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
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
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
 * PARAMETERS:  handle              - Region we are interested in
 *              function            - Start or stop
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
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initializes the region, finds any _REG methods and saves them
 *              for execution at a later time
 *
 *              Get the appropriate address space handler for a newly
 *              created region.
 *
 *              This also performs address space specific initialization. For
 *              example, PCI regions must have an _ADR object that contains
 *              a PCI address in the scope of the definition. This address is
 *              required to perform an access to PCI config space.
 *
 * MUTEX:       Interpreter should be unlocked, because we may run the _REG
 *              method for this region.
 *
 * NOTE:        Possible incompliance:
 *              There is a behavior conflict in automatic _REG execution:
 *              1. When the interpreter is evaluating a method, we can only
 *                 automatically run _REG for the following case:
 *                   operation_region (OPR1, 0x80, 0x1000010, 0x4)
 *              2. When the interpreter is loading a table, we can also
 *                 automatically run _REG for the following case:
 *                   operation_region (OPR1, 0x80, 0x1000010, 0x4)
 *              Though this may not be compliant to the de-facto standard, the
 *              logic is kept in order not to trigger regressions. And keeping
 *              this logic should be taken care by the caller of this function.
 *
 ******************************************************************************/

acpi_status acpi_ev_initialize_region(union acpi_operand_object *region_obj)
{
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *obj_desc;
	acpi_adr_space_type space_id;
	struct acpi_namespace_node *node;

	ACPI_FUNCTION_TRACE(ev_initialize_region);

	if (!region_obj) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (region_obj->common.flags & AOPOBJ_OBJECT_INITIALIZED) {
		return_ACPI_STATUS(AE_OK);
	}

	region_obj->common.flags |= AOPOBJ_OBJECT_INITIALIZED;

	node = region_obj->region.node->parent;
	space_id = region_obj->region.space_id;

	/*
	 * The following loop depends upon the root Node having no parent
	 * ie: acpi_gbl_root_node->Parent being set to NULL
	 */
	while (node) {

		/* Check to see if a handler exists */

		handler_obj = NULL;
		obj_desc = acpi_ns_get_attached_object(node);
		if (obj_desc) {

			/* Can only be a handler if the object exists */

			switch (node->type) {
			case ACPI_TYPE_DEVICE:
			case ACPI_TYPE_PROCESSOR:
			case ACPI_TYPE_THERMAL:

				handler_obj = obj_desc->common_notify.handler;
				break;

			case ACPI_TYPE_METHOD:
				/*
				 * If we are executing module level code, the original
				 * Node's object was replaced by this Method object and we
				 * saved the handler in the method object.
				 *
				 * See acpi_ns_exec_module_code
				 */
				if (!acpi_gbl_parse_table_as_term_list &&
				    obj_desc->method.
				    info_flags & ACPI_METHOD_MODULE_LEVEL) {
					handler_obj =
					    obj_desc->method.dispatch.handler;
				}
				break;

			default:

				/* Ignore other objects */

				break;
			}

			handler_obj =
			    acpi_ev_find_region_handler(space_id, handler_obj);
			if (handler_obj) {

				/* Found correct handler */

				ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
						  "Found handler %p for region %p in obj %p\n",
						  handler_obj, region_obj,
						  obj_desc));

				(void)acpi_ev_attach_region(handler_obj,
							    region_obj, FALSE);

				/*
				 * Tell all users that this region is usable by
				 * running the _REG method
				 */
				acpi_ex_exit_interpreter();
				(void)acpi_ev_execute_reg_method(region_obj,
								 ACPI_REG_CONNECT);
				acpi_ex_enter_interpreter();
				return_ACPI_STATUS(AE_OK);
			}
		}

		/* This node does not have the handler we need; Pop up one level */

		node = node->parent;
	}

	/*
	 * If we get here, there is no handler for this region. This is not
	 * fatal because many regions get created before a handler is installed
	 * for said region.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "No handler for RegionType %s(%X) (RegionObj %p)\n",
			  acpi_ut_get_region_name(space_id), space_id,
			  region_obj));

	return_ACPI_STATUS(AE_OK);
}
