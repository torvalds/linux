/******************************************************************************
 *
 * Module Name: evregion - ACPI address_space (op_region) handler dispatch
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
ACPI_MODULE_NAME("evregion")

/* Local prototypes */
static u8
acpi_ev_has_default_handler(struct acpi_namespace_node *node,
			    acpi_adr_space_type space_id);

static acpi_status
acpi_ev_reg_run(acpi_handle obj_handle,
		u32 level, void *context, void **return_value);

static acpi_status
acpi_ev_install_handler(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);

/* These are the address spaces that will get default handlers */

#define ACPI_NUM_DEFAULT_SPACES     4

static u8 acpi_gbl_default_address_spaces[ACPI_NUM_DEFAULT_SPACES] = {
	ACPI_ADR_SPACE_SYSTEM_MEMORY,
	ACPI_ADR_SPACE_SYSTEM_IO,
	ACPI_ADR_SPACE_PCI_CONFIG,
	ACPI_ADR_SPACE_DATA_TABLE
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_install_region_handlers
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Installs the core subsystem default address space handlers.
 *
 ******************************************************************************/

acpi_status acpi_ev_install_region_handlers(void)
{
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(ev_install_region_handlers);

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * All address spaces (PCI Config, EC, SMBus) are scope dependent and
	 * registration must occur for a specific device.
	 *
	 * In the case of the system memory and IO address spaces there is
	 * currently no device associated with the address space. For these we
	 * use the root.
	 *
	 * We install the default PCI config space handler at the root so that
	 * this space is immediately available even though the we have not
	 * enumerated all the PCI Root Buses yet. This is to conform to the ACPI
	 * specification which states that the PCI config space must be always
	 * available -- even though we are nowhere near ready to find the PCI root
	 * buses at this point.
	 *
	 * NOTE: We ignore AE_ALREADY_EXISTS because this means that a handler
	 * has already been installed (via acpi_install_address_space_handler).
	 * Similar for AE_SAME_HANDLER.
	 */
	for (i = 0; i < ACPI_NUM_DEFAULT_SPACES; i++) {
		status = acpi_ev_install_space_handler(acpi_gbl_root_node,
						       acpi_gbl_default_address_spaces
						       [i],
						       ACPI_DEFAULT_HANDLER,
						       NULL, NULL);
		switch (status) {
		case AE_OK:
		case AE_SAME_HANDLER:
		case AE_ALREADY_EXISTS:

			/* These exceptions are all OK */

			status = AE_OK;
			break;

		default:

			goto unlock_and_exit;
		}
	}

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_has_default_handler
 *
 * PARAMETERS:  Node                - Namespace node for the device
 *              space_id            - The address space ID
 *
 * RETURN:      TRUE if default handler is installed, FALSE otherwise
 *
 * DESCRIPTION: Check if the default handler is installed for the requested
 *              space ID.
 *
 ******************************************************************************/

static u8
acpi_ev_has_default_handler(struct acpi_namespace_node *node,
			    acpi_adr_space_type space_id)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;

	/* Must have an existing internal object */

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {
		handler_obj = obj_desc->device.handler;

		/* Walk the linked list of handlers for this object */

		while (handler_obj) {
			if (handler_obj->address_space.space_id == space_id) {
				if (handler_obj->address_space.handler_flags &
				    ACPI_ADDR_HANDLER_DEFAULT_INSTALLED) {
					return (TRUE);
				}
			}

			handler_obj = handler_obj->address_space.next;
		}
	}

	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize_op_regions
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute _REG methods for all Operation Regions that have
 *              an installed default region handler.
 *
 ******************************************************************************/

acpi_status acpi_ev_initialize_op_regions(void)
{
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(ev_initialize_op_regions);

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Run the _REG methods for op_regions in each default address space */

	for (i = 0; i < ACPI_NUM_DEFAULT_SPACES; i++) {
		/*
		 * Make sure the installed handler is the DEFAULT handler. If not the
		 * default, the _REG methods will have already been run (when the
		 * handler was installed)
		 */
		if (acpi_ev_has_default_handler(acpi_gbl_root_node,
						acpi_gbl_default_address_spaces
						[i])) {
			status =
			    acpi_ev_execute_reg_methods(acpi_gbl_root_node,
							acpi_gbl_default_address_spaces
							[i]);
		}
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_execute_reg_method
 *
 * PARAMETERS:  region_obj          - Region object
 *              Function            - Passed to _REG: On (1) or Off (0)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute _REG method for a region
 *
 ******************************************************************************/

acpi_status
acpi_ev_execute_reg_method(union acpi_operand_object *region_obj, u32 function)
{
	struct acpi_evaluate_info *info;
	union acpi_operand_object *args[3];
	union acpi_operand_object *region_obj2;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_execute_reg_method);

	region_obj2 = acpi_ns_get_secondary_object(region_obj);
	if (!region_obj2) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	if (region_obj2->extra.method_REG == NULL) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Allocate and initialize the evaluation information block */

	info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
	if (!info) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	info->prefix_node = region_obj2->extra.method_REG;
	info->pathname = NULL;
	info->parameters = args;
	info->flags = ACPI_IGNORE_RETURN_VALUE;

	/*
	 * The _REG method has two arguments:
	 *
	 * Arg0 - Integer:
	 *  Operation region space ID Same value as region_obj->Region.space_id
	 *
	 * Arg1 - Integer:
	 *  connection status 1 for connecting the handler, 0 for disconnecting
	 *  the handler (Passed as a parameter)
	 */
	args[0] =
	    acpi_ut_create_integer_object((u64) region_obj->region.space_id);
	if (!args[0]) {
		status = AE_NO_MEMORY;
		goto cleanup1;
	}

	args[1] = acpi_ut_create_integer_object((u64) function);
	if (!args[1]) {
		status = AE_NO_MEMORY;
		goto cleanup2;
	}

	args[2] = NULL;		/* Terminate list */

	/* Execute the method, no return value */

	ACPI_DEBUG_EXEC(acpi_ut_display_init_pathname
			(ACPI_TYPE_METHOD, info->prefix_node, NULL));

	status = acpi_ns_evaluate(info);
	acpi_ut_remove_reference(args[1]);

      cleanup2:
	acpi_ut_remove_reference(args[0]);

      cleanup1:
	ACPI_FREE(info);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_address_space_dispatch
 *
 * PARAMETERS:  region_obj          - Internal region object
 *              Function            - Read or Write operation
 *              region_offset       - Where in the region to read or write
 *              bit_width           - Field width in bits (8, 16, 32, or 64)
 *              Value               - Pointer to in or out value, must be
 *                                    full 64-bit acpi_integer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dispatch an address space or operation region access to
 *              a previously installed handler.
 *
 ******************************************************************************/

acpi_status
acpi_ev_address_space_dispatch(union acpi_operand_object *region_obj,
			       u32 function,
			       u32 region_offset,
			       u32 bit_width, acpi_integer * value)
{
	acpi_status status;
	acpi_adr_space_handler handler;
	acpi_adr_space_setup region_setup;
	union acpi_operand_object *handler_desc;
	union acpi_operand_object *region_obj2;
	void *region_context = NULL;

	ACPI_FUNCTION_TRACE(ev_address_space_dispatch);

	region_obj2 = acpi_ns_get_secondary_object(region_obj);
	if (!region_obj2) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Ensure that there is a handler associated with this region */

	handler_desc = region_obj->region.handler;
	if (!handler_desc) {
		ACPI_ERROR((AE_INFO,
			    "No handler for Region [%4.4s] (%p) [%s]",
			    acpi_ut_get_node_name(region_obj->region.node),
			    region_obj,
			    acpi_ut_get_region_name(region_obj->region.
						    space_id)));

		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/*
	 * It may be the case that the region has never been initialized.
	 * Some types of regions require special init code
	 */
	if (!(region_obj->region.flags & AOPOBJ_SETUP_COMPLETE)) {

		/* This region has not been initialized yet, do it */

		region_setup = handler_desc->address_space.setup;
		if (!region_setup) {

			/* No initialization routine, exit with error */

			ACPI_ERROR((AE_INFO,
				    "No init routine for region(%p) [%s]",
				    region_obj,
				    acpi_ut_get_region_name(region_obj->region.
							    space_id)));
			return_ACPI_STATUS(AE_NOT_EXIST);
		}

		/*
		 * We must exit the interpreter because the region setup will
		 * potentially execute control methods (for example, the _REG method
		 * for this region)
		 */
		acpi_ex_exit_interpreter();

		status = region_setup(region_obj, ACPI_REGION_ACTIVATE,
				      handler_desc->address_space.context,
				      &region_context);

		/* Re-enter the interpreter */

		acpi_ex_enter_interpreter();

		/* Check for failure of the Region Setup */

		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"During region initialization: [%s]",
					acpi_ut_get_region_name(region_obj->
								region.
								space_id)));
			return_ACPI_STATUS(status);
		}

		/* Region initialization may have been completed by region_setup */

		if (!(region_obj->region.flags & AOPOBJ_SETUP_COMPLETE)) {
			region_obj->region.flags |= AOPOBJ_SETUP_COMPLETE;

			if (region_obj2->extra.region_context) {

				/* The handler for this region was already installed */

				ACPI_FREE(region_context);
			} else {
				/*
				 * Save the returned context for use in all accesses to
				 * this particular region
				 */
				region_obj2->extra.region_context =
				    region_context;
			}
		}
	}

	/* We have everything we need, we can invoke the address space handler */

	handler = handler_desc->address_space.handler;

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Handler %p (@%p) Address %8.8X%8.8X [%s]\n",
			  &region_obj->region.handler->address_space, handler,
			  ACPI_FORMAT_NATIVE_UINT(region_obj->region.address +
						  region_offset),
			  acpi_ut_get_region_name(region_obj->region.
						  space_id)));

	if (!(handler_desc->address_space.handler_flags &
	      ACPI_ADDR_HANDLER_DEFAULT_INSTALLED)) {
		/*
		 * For handlers other than the default (supplied) handlers, we must
		 * exit the interpreter because the handler *might* block -- we don't
		 * know what it will do, so we can't hold the lock on the intepreter.
		 */
		acpi_ex_exit_interpreter();
	}

	/* Call the handler */

	status = handler(function,
			 (region_obj->region.address + region_offset),
			 bit_width, value, handler_desc->address_space.context,
			 region_obj2->extra.region_context);

	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Returned by Handler for [%s]",
				acpi_ut_get_region_name(region_obj->region.
							space_id)));
	}

	if (!(handler_desc->address_space.handler_flags &
	      ACPI_ADDR_HANDLER_DEFAULT_INSTALLED)) {
		/*
		 * We just returned from a non-default handler, we must re-enter the
		 * interpreter
		 */
		acpi_ex_enter_interpreter();
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_detach_region
 *
 * PARAMETERS:  region_obj          - Region Object
 *              acpi_ns_is_locked   - Namespace Region Already Locked?
 *
 * RETURN:      None
 *
 * DESCRIPTION: Break the association between the handler and the region
 *              this is a two way association.
 *
 ******************************************************************************/

void
acpi_ev_detach_region(union acpi_operand_object *region_obj,
		      u8 acpi_ns_is_locked)
{
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *obj_desc;
	union acpi_operand_object **last_obj_ptr;
	acpi_adr_space_setup region_setup;
	void **region_context;
	union acpi_operand_object *region_obj2;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_detach_region);

	region_obj2 = acpi_ns_get_secondary_object(region_obj);
	if (!region_obj2) {
		return_VOID;
	}
	region_context = &region_obj2->extra.region_context;

	/* Get the address handler from the region object */

	handler_obj = region_obj->region.handler;
	if (!handler_obj) {

		/* This region has no handler, all done */

		return_VOID;
	}

	/* Find this region in the handler's list */

	obj_desc = handler_obj->address_space.region_list;
	last_obj_ptr = &handler_obj->address_space.region_list;

	while (obj_desc) {

		/* Is this the correct Region? */

		if (obj_desc == region_obj) {
			ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
					  "Removing Region %p from address handler %p\n",
					  region_obj, handler_obj));

			/* This is it, remove it from the handler's list */

			*last_obj_ptr = obj_desc->region.next;
			obj_desc->region.next = NULL;	/* Must clear field */

			if (acpi_ns_is_locked) {
				status =
				    acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
				if (ACPI_FAILURE(status)) {
					return_VOID;
				}
			}

			/* Now stop region accesses by executing the _REG method */

			status = acpi_ev_execute_reg_method(region_obj, 0);
			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
						"from region _REG, [%s]",
						acpi_ut_get_region_name
						(region_obj->region.space_id)));
			}

			if (acpi_ns_is_locked) {
				status =
				    acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
				if (ACPI_FAILURE(status)) {
					return_VOID;
				}
			}

			/*
			 * If the region has been activated, call the setup handler with
			 * the deactivate notification
			 */
			if (region_obj->region.flags & AOPOBJ_SETUP_COMPLETE) {
				region_setup = handler_obj->address_space.setup;
				status =
				    region_setup(region_obj,
						 ACPI_REGION_DEACTIVATE,
						 handler_obj->address_space.
						 context, region_context);

				/* Init routine may fail, Just ignore errors */

				if (ACPI_FAILURE(status)) {
					ACPI_EXCEPTION((AE_INFO, status,
							"from region handler - deactivate, [%s]",
							acpi_ut_get_region_name
							(region_obj->region.
							 space_id)));
				}

				region_obj->region.flags &=
				    ~(AOPOBJ_SETUP_COMPLETE);
			}

			/*
			 * Remove handler reference in the region
			 *
			 * NOTE: this doesn't mean that the region goes away, the region
			 * is just inaccessible as indicated to the _REG method
			 *
			 * If the region is on the handler's list, this must be the
			 * region's handler
			 */
			region_obj->region.handler = NULL;
			acpi_ut_remove_reference(handler_obj);

			return_VOID;
		}

		/* Walk the linked list of handlers */

		last_obj_ptr = &obj_desc->region.next;
		obj_desc = obj_desc->region.next;
	}

	/* If we get here, the region was not in the handler's region list */

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Cannot remove region %p from address handler %p\n",
			  region_obj, handler_obj));

	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_attach_region
 *
 * PARAMETERS:  handler_obj         - Handler Object
 *              region_obj          - Region Object
 *              acpi_ns_is_locked   - Namespace Region Already Locked?
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create the association between the handler and the region
 *              this is a two way association.
 *
 ******************************************************************************/

acpi_status
acpi_ev_attach_region(union acpi_operand_object *handler_obj,
		      union acpi_operand_object *region_obj,
		      u8 acpi_ns_is_locked)
{

	ACPI_FUNCTION_TRACE(ev_attach_region);

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Adding Region [%4.4s] %p to address handler %p [%s]\n",
			  acpi_ut_get_node_name(region_obj->region.node),
			  region_obj, handler_obj,
			  acpi_ut_get_region_name(region_obj->region.
						  space_id)));

	/* Link this region to the front of the handler's list */

	region_obj->region.next = handler_obj->address_space.region_list;
	handler_obj->address_space.region_list = region_obj;

	/* Install the region's handler */

	if (region_obj->region.handler) {
		return_ACPI_STATUS(AE_ALREADY_EXISTS);
	}

	region_obj->region.handler = handler_obj;
	acpi_ut_add_reference(handler_obj);

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_install_handler
 *
 * PARAMETERS:  walk_namespace callback
 *
 * DESCRIPTION: This routine installs an address handler into objects that are
 *              of type Region or Device.
 *
 *              If the Object is a Device, and the device has a handler of
 *              the same type then the search is terminated in that branch.
 *
 *              This is because the existing handler is closer in proximity
 *              to any more regions than the one we are trying to install.
 *
 ******************************************************************************/

static acpi_status
acpi_ev_install_handler(acpi_handle obj_handle,
			u32 level, void *context, void **return_value)
{
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *next_handler_obj;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_NAME(ev_install_handler);

	handler_obj = (union acpi_operand_object *)context;

	/* Parameter validation */

	if (!handler_obj) {
		return (AE_OK);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(obj_handle);
	if (!node) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * We only care about regions and objects that are allowed to have
	 * address space handlers
	 */
	if ((node->type != ACPI_TYPE_DEVICE) &&
	    (node->type != ACPI_TYPE_REGION) && (node != acpi_gbl_root_node)) {
		return (AE_OK);
	}

	/* Check for an existing internal object */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		/* No object, just exit */

		return (AE_OK);
	}

	/* Devices are handled different than regions */

	if (obj_desc->common.type == ACPI_TYPE_DEVICE) {

		/* Check if this Device already has a handler for this address space */

		next_handler_obj = obj_desc->device.handler;
		while (next_handler_obj) {

			/* Found a handler, is it for the same address space? */

			if (next_handler_obj->address_space.space_id ==
			    handler_obj->address_space.space_id) {
				ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
						  "Found handler for region [%s] in device %p(%p) "
						  "handler %p\n",
						  acpi_ut_get_region_name
						  (handler_obj->address_space.
						   space_id), obj_desc,
						  next_handler_obj,
						  handler_obj));

				/*
				 * Since the object we found it on was a device, then it
				 * means that someone has already installed a handler for
				 * the branch of the namespace from this device on. Just
				 * bail out telling the walk routine to not traverse this
				 * branch. This preserves the scoping rule for handlers.
				 */
				return (AE_CTRL_DEPTH);
			}

			/* Walk the linked list of handlers attached to this device */

			next_handler_obj = next_handler_obj->address_space.next;
		}

		/*
		 * As long as the device didn't have a handler for this space we
		 * don't care about it. We just ignore it and proceed.
		 */
		return (AE_OK);
	}

	/* Object is a Region */

	if (obj_desc->region.space_id != handler_obj->address_space.space_id) {

		/* This region is for a different address space, just ignore it */

		return (AE_OK);
	}

	/*
	 * Now we have a region and it is for the handler's address space type.
	 *
	 * First disconnect region for any previous handler (if any)
	 */
	acpi_ev_detach_region(obj_desc, FALSE);

	/* Connect the region to the new handler */

	status = acpi_ev_attach_region(handler_obj, obj_desc, FALSE);
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_install_space_handler
 *
 * PARAMETERS:  Node            - Namespace node for the device
 *              space_id        - The address space ID
 *              Handler         - Address of the handler
 *              Setup           - Address of the setup function
 *              Context         - Value passed to the handler on each access
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for all op_regions of a given space_id.
 *              Assumes namespace is locked
 *
 ******************************************************************************/

acpi_status
acpi_ev_install_space_handler(struct acpi_namespace_node * node,
			      acpi_adr_space_type space_id,
			      acpi_adr_space_handler handler,
			      acpi_adr_space_setup setup, void *context)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	acpi_status status;
	acpi_object_type type;
	u8 flags = 0;

	ACPI_FUNCTION_TRACE(ev_install_space_handler);

	/*
	 * This registration is valid for only the types below and the root. This
	 * is where the default handlers get placed.
	 */
	if ((node->type != ACPI_TYPE_DEVICE) &&
	    (node->type != ACPI_TYPE_PROCESSOR) &&
	    (node->type != ACPI_TYPE_THERMAL) && (node != acpi_gbl_root_node)) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	if (handler == ACPI_DEFAULT_HANDLER) {
		flags = ACPI_ADDR_HANDLER_DEFAULT_INSTALLED;

		switch (space_id) {
		case ACPI_ADR_SPACE_SYSTEM_MEMORY:
			handler = acpi_ex_system_memory_space_handler;
			setup = acpi_ev_system_memory_region_setup;
			break;

		case ACPI_ADR_SPACE_SYSTEM_IO:
			handler = acpi_ex_system_io_space_handler;
			setup = acpi_ev_io_space_region_setup;
			break;

		case ACPI_ADR_SPACE_PCI_CONFIG:
			handler = acpi_ex_pci_config_space_handler;
			setup = acpi_ev_pci_config_region_setup;
			break;

		case ACPI_ADR_SPACE_CMOS:
			handler = acpi_ex_cmos_space_handler;
			setup = acpi_ev_cmos_region_setup;
			break;

		case ACPI_ADR_SPACE_PCI_BAR_TARGET:
			handler = acpi_ex_pci_bar_space_handler;
			setup = acpi_ev_pci_bar_region_setup;
			break;

		case ACPI_ADR_SPACE_DATA_TABLE:
			handler = acpi_ex_data_table_space_handler;
			setup = NULL;
			break;

		default:
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* If the caller hasn't specified a setup routine, use the default */

	if (!setup) {
		setup = acpi_ev_default_region_setup;
	}

	/* Check for an existing internal object */

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {
		/*
		 * The attached device object already exists. Make sure the handler
		 * is not already installed.
		 */
		handler_obj = obj_desc->device.handler;

		/* Walk the handler list for this device */

		while (handler_obj) {

			/* Same space_id indicates a handler already installed */

			if (handler_obj->address_space.space_id == space_id) {
				if (handler_obj->address_space.handler ==
				    handler) {
					/*
					 * It is (relatively) OK to attempt to install the SAME
					 * handler twice. This can easily happen with the
					 * PCI_Config space.
					 */
					status = AE_SAME_HANDLER;
					goto unlock_and_exit;
				} else {
					/* A handler is already installed */

					status = AE_ALREADY_EXISTS;
				}
				goto unlock_and_exit;
			}

			/* Walk the linked list of handlers */

			handler_obj = handler_obj->address_space.next;
		}
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
				  "Creating object on Device %p while installing handler\n",
				  node));

		/* obj_desc does not exist, create one */

		if (node->type == ACPI_TYPE_ANY) {
			type = ACPI_TYPE_DEVICE;
		} else {
			type = node->type;
		}

		obj_desc = acpi_ut_create_internal_object(type);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		/* Init new descriptor */

		obj_desc->common.type = (u8) type;

		/* Attach the new object to the Node */

		status = acpi_ns_attach_object(node, obj_desc, type);

		/* Remove local reference to the object */

		acpi_ut_remove_reference(obj_desc);

		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_OPREGION,
			  "Installing address handler for region %s(%X) on Device %4.4s %p(%p)\n",
			  acpi_ut_get_region_name(space_id), space_id,
			  acpi_ut_get_node_name(node), node, obj_desc));

	/*
	 * Install the handler
	 *
	 * At this point there is no existing handler. Just allocate the object
	 * for the handler and link it into the list.
	 */
	handler_obj =
	    acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_ADDRESS_HANDLER);
	if (!handler_obj) {
		status = AE_NO_MEMORY;
		goto unlock_and_exit;
	}

	/* Init handler obj */

	handler_obj->address_space.space_id = (u8) space_id;
	handler_obj->address_space.handler_flags = flags;
	handler_obj->address_space.region_list = NULL;
	handler_obj->address_space.node = node;
	handler_obj->address_space.handler = handler;
	handler_obj->address_space.context = context;
	handler_obj->address_space.setup = setup;

	/* Install at head of Device.address_space list */

	handler_obj->address_space.next = obj_desc->device.handler;

	/*
	 * The Device object is the first reference on the handler_obj.
	 * Each region that uses the handler adds a reference.
	 */
	obj_desc->device.handler = handler_obj;

	/*
	 * Walk the namespace finding all of the regions this
	 * handler will manage.
	 *
	 * Start at the device and search the branch toward
	 * the leaf nodes until either the leaf is encountered or
	 * a device is detected that has an address handler of the
	 * same type.
	 *
	 * In either case, back up and search down the remainder
	 * of the branch
	 */
	status = acpi_ns_walk_namespace(ACPI_TYPE_ANY, node, ACPI_UINT32_MAX,
					ACPI_NS_WALK_UNLOCK,
					acpi_ev_install_handler, NULL,
					handler_obj, NULL);

      unlock_and_exit:
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_execute_reg_methods
 *
 * PARAMETERS:  Node            - Namespace node for the device
 *              space_id        - The address space ID
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Run all _REG methods for the input Space ID;
 *              Note: assumes namespace is locked, or system init time.
 *
 ******************************************************************************/

acpi_status
acpi_ev_execute_reg_methods(struct acpi_namespace_node *node,
			    acpi_adr_space_type space_id)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_execute_reg_methods);

	/*
	 * Run all _REG methods for all Operation Regions for this space ID. This
	 * is a separate walk in order to handle any interdependencies between
	 * regions and _REG methods. (i.e. handlers must be installed for all
	 * regions of this Space ID before we can run any _REG methods)
	 */
	status = acpi_ns_walk_namespace(ACPI_TYPE_ANY, node, ACPI_UINT32_MAX,
					ACPI_NS_WALK_UNLOCK, acpi_ev_reg_run,
					NULL, &space_id, NULL);

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_reg_run
 *
 * PARAMETERS:  walk_namespace callback
 *
 * DESCRIPTION: Run _REG method for region objects of the requested space_iD
 *
 ******************************************************************************/

static acpi_status
acpi_ev_reg_run(acpi_handle obj_handle,
		u32 level, void *context, void **return_value)
{
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	acpi_adr_space_type space_id;
	acpi_status status;

	space_id = *ACPI_CAST_PTR(acpi_adr_space_type, context);

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(obj_handle);
	if (!node) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * We only care about regions.and objects that are allowed to have address
	 * space handlers
	 */
	if ((node->type != ACPI_TYPE_REGION) && (node != acpi_gbl_root_node)) {
		return (AE_OK);
	}

	/* Check for an existing internal object */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		/* No object, just exit */

		return (AE_OK);
	}

	/* Object is a Region */

	if (obj_desc->region.space_id != space_id) {

		/* This region is for a different address space, just ignore it */

		return (AE_OK);
	}

	status = acpi_ev_execute_reg_method(obj_desc, 1);
	return (status);
}
