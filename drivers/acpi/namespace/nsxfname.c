/******************************************************************************
 *
 * Module Name: nsxfname - Public interfaces to the ACPI subsystem
 *                         ACPI Namespace oriented interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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

#include <linux/module.h>

#include <acpi/acpi.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsxfname")

/******************************************************************************
 *
 * FUNCTION:    acpi_get_handle
 *
 * PARAMETERS:  Parent          - Object to search under (search scope).
 *              Pathname        - Pointer to an asciiz string containing the
 *                                name
 *              ret_handle      - Where the return handle is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine will search for a caller specified name in the
 *              name space.  The caller can restrict the search region by
 *              specifying a non NULL parent.  The parent value is itself a
 *              namespace handle.
 *
 ******************************************************************************/
acpi_status
acpi_get_handle(acpi_handle parent,
		acpi_string pathname, acpi_handle * ret_handle)
{
	acpi_status status;
	struct acpi_namespace_node *node = NULL;
	struct acpi_namespace_node *prefix_node = NULL;

	ACPI_FUNCTION_ENTRY();

	/* Parameter Validation */

	if (!ret_handle || !pathname) {
		return (AE_BAD_PARAMETER);
	}

	/* Convert a parent handle to a prefix node */

	if (parent) {
		status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		prefix_node = acpi_ns_map_handle_to_node(parent);
		if (!prefix_node) {
			(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
			return (AE_BAD_PARAMETER);
		}

		status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	/* Special case for root, since we can't search for it */

	if (ACPI_STRCMP(pathname, ACPI_NS_ROOT_PATH) == 0) {
		*ret_handle =
		    acpi_ns_convert_entry_to_handle(acpi_gbl_root_node);
		return (AE_OK);
	}

	/*
	 *  Find the Node and convert to a handle
	 */
	status =
	    acpi_ns_get_node_by_path(pathname, prefix_node, ACPI_NS_NO_UPSEARCH,
				     &node);

	*ret_handle = NULL;
	if (ACPI_SUCCESS(status)) {
		*ret_handle = acpi_ns_convert_entry_to_handle(node);
	}

	return (status);
}

EXPORT_SYMBOL(acpi_get_handle);

/******************************************************************************
 *
 * FUNCTION:    acpi_get_name
 *
 * PARAMETERS:  Handle          - Handle to be converted to a pathname
 *              name_type       - Full pathname or single segment
 *              Buffer          - Buffer for returned path
 *
 * RETURN:      Pointer to a string containing the fully qualified Name.
 *
 * DESCRIPTION: This routine returns the fully qualified name associated with
 *              the Handle parameter.  This and the acpi_pathname_to_handle are
 *              complementary functions.
 *
 ******************************************************************************/

acpi_status
acpi_get_name(acpi_handle handle, u32 name_type, struct acpi_buffer * buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	/* Parameter validation */

	if (name_type > ACPI_NAME_TYPE_MAX) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(buffer);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	if (name_type == ACPI_FULL_PATHNAME) {
		/* Get the full pathname (From the namespace root) */

		status = acpi_ns_handle_to_pathname(handle, buffer);
		return (status);
	}

	/*
	 * Wants the single segment ACPI name.
	 * Validate handle and convert to a namespace Node
	 */
	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node(handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, ACPI_PATH_SEGMENT_LENGTH);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/* Just copy the ACPI name from the Node and zero terminate it */

	ACPI_STRNCPY(buffer->pointer, acpi_ut_get_node_name(node),
		     ACPI_NAME_SIZE);
	((char *)buffer->pointer)[ACPI_NAME_SIZE] = 0;
	status = AE_OK;

      unlock_and_exit:

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return (status);
}

EXPORT_SYMBOL(acpi_get_name);

/******************************************************************************
 *
 * FUNCTION:    acpi_get_object_info
 *
 * PARAMETERS:  Handle          - Object Handle
 *              Buffer          - Where the info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns information about an object as gleaned from the
 *              namespace node and possibly by running several standard
 *              control methods (Such as in the case of a device.)
 *
 ******************************************************************************/

acpi_status
acpi_get_object_info(acpi_handle handle, struct acpi_buffer * buffer)
{
	acpi_status status;
	struct acpi_namespace_node *node;
	struct acpi_device_info *info;
	struct acpi_device_info *return_info;
	struct acpi_compatible_id_list *cid_list = NULL;
	acpi_size size;

	/* Parameter validation */

	if (!handle || !buffer) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer(buffer);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	info = ACPI_MEM_CALLOCATE(sizeof(struct acpi_device_info));
	if (!info) {
		return (AE_NO_MEMORY);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	node = acpi_ns_map_handle_to_node(handle);
	if (!node) {
		(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		goto cleanup;
	}

	/* Init return structure */

	size = sizeof(struct acpi_device_info);

	info->type = node->type;
	info->name = node->name.integer;
	info->valid = 0;

	status = acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* If not a device, we are all done */

	if (info->type == ACPI_TYPE_DEVICE) {
		/*
		 * Get extra info for ACPI Devices objects only:
		 * Run the Device _HID, _UID, _CID, _STA, _ADR and _sx_d methods.
		 *
		 * Note: none of these methods are required, so they may or may
		 * not be present for this device.  The Info->Valid bitfield is used
		 * to indicate which methods were found and ran successfully.
		 */

		/* Execute the Device._HID method */

		status = acpi_ut_execute_HID(node, &info->hardware_id);
		if (ACPI_SUCCESS(status)) {
			info->valid |= ACPI_VALID_HID;
		}

		/* Execute the Device._UID method */

		status = acpi_ut_execute_UID(node, &info->unique_id);
		if (ACPI_SUCCESS(status)) {
			info->valid |= ACPI_VALID_UID;
		}

		/* Execute the Device._CID method */

		status = acpi_ut_execute_CID(node, &cid_list);
		if (ACPI_SUCCESS(status)) {
			size += cid_list->size;
			info->valid |= ACPI_VALID_CID;
		}

		/* Execute the Device._STA method */

		status = acpi_ut_execute_STA(node, &info->current_status);
		if (ACPI_SUCCESS(status)) {
			info->valid |= ACPI_VALID_STA;
		}

		/* Execute the Device._ADR method */

		status = acpi_ut_evaluate_numeric_object(METHOD_NAME__ADR, node,
							 &info->address);
		if (ACPI_SUCCESS(status)) {
			info->valid |= ACPI_VALID_ADR;
		}

		/* Execute the Device._sx_d methods */

		status = acpi_ut_execute_sxds(node, info->highest_dstates);
		if (ACPI_SUCCESS(status)) {
			info->valid |= ACPI_VALID_SXDS;
		}
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer(buffer, size);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/* Populate the return buffer */

	return_info = buffer->pointer;
	ACPI_MEMCPY(return_info, info, sizeof(struct acpi_device_info));

	if (cid_list) {
		ACPI_MEMCPY(&return_info->compatibility_id, cid_list,
			    cid_list->size);
	}

      cleanup:
	ACPI_MEM_FREE(info);
	if (cid_list) {
		ACPI_MEM_FREE(cid_list);
	}
	return (status);
}

EXPORT_SYMBOL(acpi_get_object_info);
