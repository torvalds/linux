/*******************************************************************************
 *
 * Module Name: nsxfeval - Public interfaces to the ACPI subsystem
 *                         ACPI Object evaluation interfaces
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
#include <acpi/acinterp.h>


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsxfeval")


/*******************************************************************************
 *
 * FUNCTION:    acpi_evaluate_object_typed
 *
 * PARAMETERS:  Handle              - Object handle (optional)
 *              *Pathname           - Object pathname (optional)
 *              **external_params   - List of parameters to pass to method,
 *                                    terminated by NULL.  May be NULL
 *                                    if no parameters are being passed.
 *              *return_buffer      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              return_type         - Expected type of return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and evaluate the given object, passing the given
 *              parameters if necessary.  One of "Handle" or "Pathname" must
 *              be valid (non-null)
 *
 ******************************************************************************/
#ifdef ACPI_FUTURE_USAGE
acpi_status
acpi_evaluate_object_typed (
	acpi_handle                     handle,
	acpi_string                     pathname,
	struct acpi_object_list         *external_params,
	struct acpi_buffer              *return_buffer,
	acpi_object_type                return_type)
{
	acpi_status                     status;
	u8                              must_free = FALSE;


	ACPI_FUNCTION_TRACE ("acpi_evaluate_object_typed");


	/* Return buffer must be valid */

	if (!return_buffer) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (return_buffer->length == ACPI_ALLOCATE_BUFFER) {
		must_free = TRUE;
	}

	/* Evaluate the object */

	status = acpi_evaluate_object (handle, pathname, external_params, return_buffer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Type ANY means "don't care" */

	if (return_type == ACPI_TYPE_ANY) {
		return_ACPI_STATUS (AE_OK);
	}

	if (return_buffer->length == 0) {
		/* Error because caller specifically asked for a return value */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"No return value\n"));

		return_ACPI_STATUS (AE_NULL_OBJECT);
	}

	/* Examine the object type returned from evaluate_object */

	if (((union acpi_object *) return_buffer->pointer)->type == return_type) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Return object type does not match requested type */

	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
		"Incorrect return type [%s] requested [%s]\n",
		acpi_ut_get_type_name (((union acpi_object *) return_buffer->pointer)->type),
		acpi_ut_get_type_name (return_type)));

	if (must_free) {
		/* Caller used ACPI_ALLOCATE_BUFFER, free the return buffer */

		acpi_os_free (return_buffer->pointer);
		return_buffer->pointer = NULL;
	}

	return_buffer->length = 0;
	return_ACPI_STATUS (AE_TYPE);
}
#endif  /*  ACPI_FUTURE_USAGE  */


/*******************************************************************************
 *
 * FUNCTION:    acpi_evaluate_object
 *
 * PARAMETERS:  Handle              - Object handle (optional)
 *              Pathname            - Object pathname (optional)
 *              external_params     - List of parameters to pass to method,
 *                                    terminated by NULL.  May be NULL
 *                                    if no parameters are being passed.
 *              return_buffer       - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and evaluate the given object, passing the given
 *              parameters if necessary.  One of "Handle" or "Pathname" must
 *              be valid (non-null)
 *
 ******************************************************************************/

acpi_status
acpi_evaluate_object (
	acpi_handle                     handle,
	acpi_string                     pathname,
	struct acpi_object_list         *external_params,
	struct acpi_buffer              *return_buffer)
{
	acpi_status                     status;
	acpi_status                     status2;
	struct acpi_parameter_info      info;
	acpi_size                       buffer_space_needed;
	u32                             i;


	ACPI_FUNCTION_TRACE ("acpi_evaluate_object");


	info.node = handle;
	info.parameters = NULL;
	info.return_object = NULL;
	info.parameter_type = ACPI_PARAM_ARGS;

	/*
	 * If there are parameters to be passed to the object
	 * (which must be a control method), the external objects
	 * must be converted to internal objects
	 */
	if (external_params && external_params->count) {
		/*
		 * Allocate a new parameter block for the internal objects
		 * Add 1 to count to allow for null terminated internal list
		 */
		info.parameters = ACPI_MEM_CALLOCATE (
				 ((acpi_size) external_params->count + 1) *
				 sizeof (void *));
		if (!info.parameters) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/*
		 * Convert each external object in the list to an
		 * internal object
		 */
		for (i = 0; i < external_params->count; i++) {
			status = acpi_ut_copy_eobject_to_iobject (&external_params->pointer[i],
					  &info.parameters[i]);
			if (ACPI_FAILURE (status)) {
				acpi_ut_delete_internal_object_list (info.parameters);
				return_ACPI_STATUS (status);
			}
		}
		info.parameters[external_params->count] = NULL;
	}


	/*
	 * Three major cases:
	 * 1) Fully qualified pathname
	 * 2) No handle, not fully qualified pathname (error)
	 * 3) Valid handle
	 */
	if ((pathname) &&
		(acpi_ns_valid_root_prefix (pathname[0]))) {
		/*
		 *  The path is fully qualified, just evaluate by name
		 */
		status = acpi_ns_evaluate_by_name (pathname, &info);
	}
	else if (!handle) {
		/*
		 * A handle is optional iff a fully qualified pathname
		 * is specified.  Since we've already handled fully
		 * qualified names above, this is an error
		 */
		if (!pathname) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Both Handle and Pathname are NULL\n"));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Handle is NULL and Pathname is relative\n"));
		}

		status = AE_BAD_PARAMETER;
	}
	else {
		/*
		 * We get here if we have a handle -- and if we have a
		 * pathname it is relative.  The handle will be validated
		 * in the lower procedures
		 */
		if (!pathname) {
			/*
			 * The null pathname case means the handle is for
			 * the actual object to be evaluated
			 */
			status = acpi_ns_evaluate_by_handle (&info);
		}
		else {
		   /*
			* Both a Handle and a relative Pathname
			*/
			status = acpi_ns_evaluate_relative (pathname, &info);
		}
	}


	/*
	 * If we are expecting a return value, and all went well above,
	 * copy the return value to an external object.
	 */
	if (return_buffer) {
		if (!info.return_object) {
			return_buffer->length = 0;
		}
		else {
			if (ACPI_GET_DESCRIPTOR_TYPE (info.return_object) == ACPI_DESC_TYPE_NAMED) {
				/*
				 * If we received a NS Node as a return object, this means that
				 * the object we are evaluating has nothing interesting to
				 * return (such as a mutex, etc.)  We return an error because
				 * these types are essentially unsupported by this interface.
				 * We don't check up front because this makes it easier to add
				 * support for various types at a later date if necessary.
				 */
				status = AE_TYPE;
				info.return_object = NULL;  /* No need to delete a NS Node */
				return_buffer->length = 0;
			}

			if (ACPI_SUCCESS (status)) {
				/*
				 * Find out how large a buffer is needed
				 * to contain the returned object
				 */
				status = acpi_ut_get_object_size (info.return_object,
						   &buffer_space_needed);
				if (ACPI_SUCCESS (status)) {
					/* Validate/Allocate/Clear caller buffer */

					status = acpi_ut_initialize_buffer (return_buffer, buffer_space_needed);
					if (ACPI_FAILURE (status)) {
						/*
						 * Caller's buffer is too small or a new one can't be allocated
						 */
						ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
							"Needed buffer size %X, %s\n",
							(u32) buffer_space_needed,
							acpi_format_exception (status)));
					}
					else {
						/*
						 *  We have enough space for the object, build it
						 */
						status = acpi_ut_copy_iobject_to_eobject (info.return_object,
								  return_buffer);
					}
				}
			}
		}
	}

	if (info.return_object) {
		/*
		 * Delete the internal return object.  NOTE: Interpreter
		 * must be locked to avoid race condition.
		 */
		status2 = acpi_ex_enter_interpreter ();
		if (ACPI_SUCCESS (status2)) {
			/*
			 * Delete the internal return object. (Or at least
			 * decrement the reference count by one)
			 */
			acpi_ut_remove_reference (info.return_object);
			acpi_ex_exit_interpreter ();
		}
	}

	/*
	 * Free the input parameter list (if we created one),
	 */
	if (info.parameters) {
		/* Free the allocated parameter block */

		acpi_ut_delete_internal_object_list (info.parameters);
	}

	return_ACPI_STATUS (status);
}
EXPORT_SYMBOL(acpi_evaluate_object);


/*******************************************************************************
 *
 * FUNCTION:    acpi_walk_namespace
 *
 * PARAMETERS:  Type                - acpi_object_type to search for
 *              start_object        - Handle in namespace where search begins
 *              max_depth           - Depth to which search is to reach
 *              user_function       - Called when an object of "Type" is found
 *              Context             - Passed to user function
 *              return_value        - Location where return value of
 *                                    user_function is put if terminated early
 *
 * RETURNS      Return value from the user_function if terminated early.
 *              Otherwise, returns NULL.
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the object specified by start_handle.
 *              The user_function is called whenever an object that matches
 *              the type parameter is found.  If the user function returns
 *              a non-zero value, the search is terminated immediately and this
 *              value is returned to the caller.
 *
 *              The point of this procedure is to provide a generic namespace
 *              walk routine that can be called from multiple places to
 *              provide multiple services;  the User Function can be tailored
 *              to each task, whether it is a print function, a compare
 *              function, etc.
 *
 ******************************************************************************/

acpi_status
acpi_walk_namespace (
	acpi_object_type                type,
	acpi_handle                     start_object,
	u32                             max_depth,
	acpi_walk_callback              user_function,
	void                            *context,
	void                            **return_value)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("acpi_walk_namespace");


	/* Parameter validation */

	if ((type > ACPI_TYPE_EXTERNAL_MAX) ||
		(!max_depth)                    ||
		(!user_function)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Lock the namespace around the walk.
	 * The namespace will be unlocked/locked around each call
	 * to the user function - since this function
	 * must be allowed to make Acpi calls itself.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ns_walk_namespace (type, start_object, max_depth, ACPI_NS_WALK_UNLOCK,
			  user_function, context, return_value);

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}
EXPORT_SYMBOL(acpi_walk_namespace);


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_get_device_callback
 *
 * PARAMETERS:  Callback from acpi_get_device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes callbacks from walk_namespace and filters out all non-
 *              present devices, or if they specified a HID, it filters based
 *              on that.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_get_device_callback (
	acpi_handle                     obj_handle,
	u32                             nesting_level,
	void                            *context,
	void                            **return_value)
{
	struct acpi_get_devices_info    *info = context;
	acpi_status                     status;
	struct acpi_namespace_node      *node;
	u32                             flags;
	struct acpi_device_id           hid;
	struct acpi_compatible_id_list *cid;
	acpi_native_uint                i;


	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node (obj_handle);
	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (!node) {
		return (AE_BAD_PARAMETER);
	}

	/* Run _STA to determine if device is present */

	status = acpi_ut_execute_STA (node, &flags);
	if (ACPI_FAILURE (status)) {
		return (AE_CTRL_DEPTH);
	}

	if (!(flags & 0x01)) {
		/* Don't return at the device or children of the device if not there */

		return (AE_CTRL_DEPTH);
	}

	/* Filter based on device HID & CID */

	if (info->hid != NULL) {
		status = acpi_ut_execute_HID (node, &hid);
		if (status == AE_NOT_FOUND) {
			return (AE_OK);
		}
		else if (ACPI_FAILURE (status)) {
			return (AE_CTRL_DEPTH);
		}

		if (ACPI_STRNCMP (hid.value, info->hid, sizeof (hid.value)) != 0) {
			/* Get the list of Compatible IDs */

			status = acpi_ut_execute_CID (node, &cid);
			if (status == AE_NOT_FOUND) {
				return (AE_OK);
			}
			else if (ACPI_FAILURE (status)) {
				return (AE_CTRL_DEPTH);
			}

			/* Walk the CID list */

			for (i = 0; i < cid->count; i++) {
				if (ACPI_STRNCMP (cid->id[i].value, info->hid,
						 sizeof (struct acpi_compatible_id)) != 0) {
					ACPI_MEM_FREE (cid);
					return (AE_OK);
				}
			}
			ACPI_MEM_FREE (cid);
		}
	}

	status = info->user_function (obj_handle, nesting_level, info->context, return_value);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_devices
 *
 * PARAMETERS:  HID                 - HID to search for. Can be NULL.
 *              user_function       - Called when a matching object is found
 *              Context             - Passed to user function
 *              return_value        - Location where return value of
 *                                    user_function is put if terminated early
 *
 * RETURNS      Return value from the user_function if terminated early.
 *              Otherwise, returns NULL.
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the object specified by start_handle.
 *              The user_function is called whenever an object of type
 *              Device is found.  If the user function returns
 *              a non-zero value, the search is terminated immediately and this
 *              value is returned to the caller.
 *
 *              This is a wrapper for walk_namespace, but the callback performs
 *              additional filtering. Please see acpi_get_device_callback.
 *
 ******************************************************************************/

acpi_status
acpi_get_devices (
	char                            *HID,
	acpi_walk_callback              user_function,
	void                            *context,
	void                            **return_value)
{
	acpi_status                     status;
	struct acpi_get_devices_info    info;


	ACPI_FUNCTION_TRACE ("acpi_get_devices");


	/* Parameter validation */

	if (!user_function) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * We're going to call their callback from OUR callback, so we need
	 * to know what it is, and their context parameter.
	 */
	info.context      = context;
	info.user_function = user_function;
	info.hid          = HID;

	/*
	 * Lock the namespace around the walk.
	 * The namespace will be unlocked/locked around each call
	 * to the user function - since this function
	 * must be allowed to make Acpi calls itself.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ns_walk_namespace (ACPI_TYPE_DEVICE,
			   ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			   ACPI_NS_WALK_UNLOCK,
			   acpi_ns_get_device_callback, &info,
			   return_value);

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}
EXPORT_SYMBOL(acpi_get_devices);


/*******************************************************************************
 *
 * FUNCTION:    acpi_attach_data
 *
 * PARAMETERS:  obj_handle          - Namespace node
 *              Handler             - Handler for this attachment
 *              Data                - Pointer to data to be attached
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Attach arbitrary data and handler to a namespace node.
 *
 ******************************************************************************/

acpi_status
acpi_attach_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler,
	void                            *data)
{
	struct acpi_namespace_node      *node;
	acpi_status                     status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler    ||
		!data) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_attach_data (node, handler, data);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_detach_data
 *
 * PARAMETERS:  obj_handle          - Namespace node handle
 *              Handler             - Handler used in call to acpi_attach_data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove data that was previously attached to a node.
 *
 ******************************************************************************/

acpi_status
acpi_detach_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler)
{
	struct acpi_namespace_node      *node;
	acpi_status                     status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_detach_data (node, handler);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_data
 *
 * PARAMETERS:  obj_handle          - Namespace node
 *              Handler             - Handler used in call to attach_data
 *              Data                - Where the data is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve data that was previously attached to a namespace node.
 *
 ******************************************************************************/

acpi_status
acpi_get_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler,
	void                            **data)
{
	struct acpi_namespace_node      *node;
	acpi_status                     status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler    ||
		!data) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_get_attached_data (node, handler, data);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


