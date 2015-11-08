/*******************************************************************************
 *
 * Module Name: dbnames - Debugger commands for the acpi namespace
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acnamesp.h"
#include "acdebug.h"
#include "acpredef.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbnames")

/* Local prototypes */
static acpi_status
acpi_db_walk_and_match_name(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value);

static acpi_status
acpi_db_walk_for_predefined_names(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value);

static acpi_status
acpi_db_walk_for_specific_objects(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value);

static acpi_status
acpi_db_walk_for_object_counts(acpi_handle obj_handle,
			       u32 nesting_level,
			       void *context, void **return_value);

static acpi_status
acpi_db_integrity_walk(acpi_handle obj_handle,
		       u32 nesting_level, void *context, void **return_value);

static acpi_status
acpi_db_walk_for_references(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value);

static acpi_status
acpi_db_bus_walk(acpi_handle obj_handle,
		 u32 nesting_level, void *context, void **return_value);

/*
 * Arguments for the Objects command
 * These object types map directly to the ACPI_TYPES
 */
static struct acpi_db_argument_info acpi_db_object_types[] = {
	{"ANY"},
	{"INTEGERS"},
	{"STRINGS"},
	{"BUFFERS"},
	{"PACKAGES"},
	{"FIELDS"},
	{"DEVICES"},
	{"EVENTS"},
	{"METHODS"},
	{"MUTEXES"},
	{"REGIONS"},
	{"POWERRESOURCES"},
	{"PROCESSORS"},
	{"THERMALZONES"},
	{"BUFFERFIELDS"},
	{"DDBHANDLES"},
	{"DEBUG"},
	{"REGIONFIELDS"},
	{"BANKFIELDS"},
	{"INDEXFIELDS"},
	{"REFERENCES"},
	{"ALIASES"},
	{"METHODALIASES"},
	{"NOTIFY"},
	{"ADDRESSHANDLER"},
	{"RESOURCE"},
	{"RESOURCEFIELD"},
	{"SCOPES"},
	{NULL}			/* Must be null terminated */
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_set_scope
 *
 * PARAMETERS:  name                - New scope path
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set the "current scope" as maintained by this utility.
 *              The scope is used as a prefix to ACPI paths.
 *
 ******************************************************************************/

void acpi_db_set_scope(char *name)
{
	acpi_status status;
	struct acpi_namespace_node *node;

	if (!name || name[0] == 0) {
		acpi_os_printf("Current scope: %s\n", acpi_gbl_db_scope_buf);
		return;
	}

	acpi_db_prep_namestring(name);

	if (ACPI_IS_ROOT_PREFIX(name[0])) {

		/* Validate new scope from the root */

		status = acpi_ns_get_node(acpi_gbl_root_node, name,
					  ACPI_NS_NO_UPSEARCH, &node);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		acpi_gbl_db_scope_buf[0] = 0;
	} else {
		/* Validate new scope relative to old scope */

		status = acpi_ns_get_node(acpi_gbl_db_scope_node, name,
					  ACPI_NS_NO_UPSEARCH, &node);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}
	}

	/* Build the final pathname */

	if (acpi_ut_safe_strcat
	    (acpi_gbl_db_scope_buf, sizeof(acpi_gbl_db_scope_buf), name)) {
		status = AE_BUFFER_OVERFLOW;
		goto error_exit;
	}

	if (acpi_ut_safe_strcat
	    (acpi_gbl_db_scope_buf, sizeof(acpi_gbl_db_scope_buf), "\\")) {
		status = AE_BUFFER_OVERFLOW;
		goto error_exit;
	}

	acpi_gbl_db_scope_node = node;
	acpi_os_printf("New scope: %s\n", acpi_gbl_db_scope_buf);
	return;

error_exit:

	acpi_os_printf("Could not attach scope: %s, %s\n",
		       name, acpi_format_exception(status));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_namespace
 *
 * PARAMETERS:  start_arg       - Node to begin namespace dump
 *              depth_arg       - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire namespace or a subtree. Each node is displayed
 *              with type and other information.
 *
 ******************************************************************************/

void acpi_db_dump_namespace(char *start_arg, char *depth_arg)
{
	acpi_handle subtree_entry = acpi_gbl_root_node;
	u32 max_depth = ACPI_UINT32_MAX;

	/* No argument given, just start at the root and dump entire namespace */

	if (start_arg) {
		subtree_entry = acpi_db_convert_to_node(start_arg);
		if (!subtree_entry) {
			return;
		}

		/* Now we can check for the depth argument */

		if (depth_arg) {
			max_depth = strtoul(depth_arg, NULL, 0);
		}
	}

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf("ACPI Namespace (from %4.4s (%p) subtree):\n",
		       ((struct acpi_namespace_node *)subtree_entry)->name.
		       ascii, subtree_entry);

	/* Display the subtree */

	acpi_db_set_output_destination(ACPI_DB_REDIRECTABLE_OUTPUT);
	acpi_ns_dump_objects(ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, max_depth,
			     ACPI_OWNER_ID_MAX, subtree_entry);
	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_namespace_paths
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire namespace with full object pathnames and object
 *              type information. Alternative to "namespace" command.
 *
 ******************************************************************************/

void acpi_db_dump_namespace_paths(void)
{

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf("ACPI Namespace (from root):\n");

	/* Display the entire namespace */

	acpi_db_set_output_destination(ACPI_DB_REDIRECTABLE_OUTPUT);
	acpi_ns_dump_object_paths(ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY,
				  ACPI_UINT32_MAX, ACPI_OWNER_ID_MAX,
				  acpi_gbl_root_node);

	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_dump_namespace_by_owner
 *
 * PARAMETERS:  owner_arg       - Owner ID whose nodes will be displayed
 *              depth_arg       - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump elements of the namespace that are owned by the owner_id.
 *
 ******************************************************************************/

void acpi_db_dump_namespace_by_owner(char *owner_arg, char *depth_arg)
{
	acpi_handle subtree_entry = acpi_gbl_root_node;
	u32 max_depth = ACPI_UINT32_MAX;
	acpi_owner_id owner_id;

	owner_id = (acpi_owner_id) strtoul(owner_arg, NULL, 0);

	/* Now we can check for the depth argument */

	if (depth_arg) {
		max_depth = strtoul(depth_arg, NULL, 0);
	}

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf("ACPI Namespace by owner %X:\n", owner_id);

	/* Display the subtree */

	acpi_db_set_output_destination(ACPI_DB_REDIRECTABLE_OUTPUT);
	acpi_ns_dump_objects(ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, max_depth,
			     owner_id, subtree_entry);
	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_and_match_name
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find a particular name/names within the namespace. Wildcards
 *              are supported -- '?' matches any character.
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_and_match_name(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value)
{
	acpi_status status;
	char *requested_name = (char *)context;
	u32 i;
	struct acpi_buffer buffer;
	struct acpi_walk_info info;

	/* Check for a name match */

	for (i = 0; i < 4; i++) {

		/* Wildcard support */

		if ((requested_name[i] != '?') &&
		    (requested_name[i] != ((struct acpi_namespace_node *)
					   obj_handle)->name.ascii[i])) {

			/* No match, just exit */

			return (AE_OK);
		}
	}

	/* Get the full pathname to this object */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname(obj_handle, &buffer, TRUE);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could Not get pathname for object %p\n",
			       obj_handle);
	} else {
		info.owner_id = ACPI_OWNER_ID_MAX;
		info.debug_level = ACPI_UINT32_MAX;
		info.display_type = ACPI_DISPLAY_SUMMARY | ACPI_DISPLAY_SHORT;

		acpi_os_printf("%32s", (char *)buffer.pointer);
		(void)acpi_ns_dump_one_object(obj_handle, nesting_level, &info,
					      NULL);
		ACPI_FREE(buffer.pointer);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_find_name_in_namespace
 *
 * PARAMETERS:  name_arg        - The 4-character ACPI name to find.
 *                                wildcards are supported.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search the namespace for a given name (with wildcards)
 *
 ******************************************************************************/

acpi_status acpi_db_find_name_in_namespace(char *name_arg)
{
	char acpi_name[5] = "____";
	char *acpi_name_ptr = acpi_name;

	if (strlen(name_arg) > ACPI_NAME_SIZE) {
		acpi_os_printf("Name must be no longer than 4 characters\n");
		return (AE_OK);
	}

	/* Pad out name with underscores as necessary to create a 4-char name */

	acpi_ut_strupr(name_arg);
	while (*name_arg) {
		*acpi_name_ptr = *name_arg;
		acpi_name_ptr++;
		name_arg++;
	}

	/* Walk the namespace from the root */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_walk_and_match_name,
				  NULL, acpi_name, NULL);

	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_for_predefined_names
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Detect and display predefined ACPI names (names that start with
 *              an underscore)
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_for_predefined_names(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value)
{
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	u32 *count = (u32 *)context;
	const union acpi_predefined_info *predefined;
	const union acpi_predefined_info *package = NULL;
	char *pathname;
	char string_buffer[48];

	predefined = acpi_ut_match_predefined_method(node->name.ascii);
	if (!predefined) {
		return (AE_OK);
	}

	pathname = acpi_ns_get_external_pathname(node);
	if (!pathname) {
		return (AE_OK);
	}

	/* If method returns a package, the info is in the next table entry */

	if (predefined->info.expected_btypes & ACPI_RTYPE_PACKAGE) {
		package = predefined + 1;
	}

	acpi_ut_get_expected_return_types(string_buffer,
					  predefined->info.expected_btypes);

	acpi_os_printf("%-32s Arguments %X, Return Types: %s", pathname,
		       METHOD_GET_ARG_COUNT(predefined->info.argument_list),
		       string_buffer);

	if (package) {
		acpi_os_printf(" (PkgType %2.2X, ObjType %2.2X, Count %2.2X)",
			       package->ret_info.type,
			       package->ret_info.object_type1,
			       package->ret_info.count1);
	}

	acpi_os_printf("\n");

	/* Check that the declared argument count matches the ACPI spec */

	acpi_ns_check_acpi_compliance(pathname, node, predefined);

	ACPI_FREE(pathname);
	(*count)++;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_check_predefined_names
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Validate all predefined names in the namespace
 *
 ******************************************************************************/

void acpi_db_check_predefined_names(void)
{
	u32 count = 0;

	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX,
				  acpi_db_walk_for_predefined_names, NULL,
				  (void *)&count, NULL);

	acpi_os_printf("Found %u predefined names in the namespace\n", count);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_for_object_counts
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display short info about objects in the namespace
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_for_object_counts(acpi_handle obj_handle,
			       u32 nesting_level,
			       void *context, void **return_value)
{
	struct acpi_object_info *info = (struct acpi_object_info *)context;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;

	if (node->type > ACPI_TYPE_NS_NODE_MAX) {
		acpi_os_printf("[%4.4s]: Unknown object type %X\n",
			       node->name.ascii, node->type);
	} else {
		info->types[node->type]++;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_for_specific_objects
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display short info about objects in the namespace
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_for_specific_objects(acpi_handle obj_handle,
				  u32 nesting_level,
				  void *context, void **return_value)
{
	struct acpi_walk_info *info = (struct acpi_walk_info *)context;
	struct acpi_buffer buffer;
	acpi_status status;

	info->count++;

	/* Get and display the full pathname to this object */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname(obj_handle, &buffer, TRUE);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could Not get pathname for object %p\n",
			       obj_handle);
		return (AE_OK);
	}

	acpi_os_printf("%32s", (char *)buffer.pointer);
	ACPI_FREE(buffer.pointer);

	/* Dump short info about the object */

	(void)acpi_ns_dump_one_object(obj_handle, nesting_level, info, NULL);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_display_objects
 *
 * PARAMETERS:  obj_type_arg        - Type of object to display
 *              display_count_arg   - Max depth to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display objects in the namespace of the requested type
 *
 ******************************************************************************/

acpi_status acpi_db_display_objects(char *obj_type_arg, char *display_count_arg)
{
	struct acpi_walk_info info;
	acpi_object_type type;
	struct acpi_object_info *object_info;
	u32 i;
	u32 total_objects = 0;

	/* No argument means display summary/count of all object types */

	if (!obj_type_arg) {
		object_info =
		    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_object_info));

		/* Walk the namespace from the root */

		(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
					  ACPI_UINT32_MAX,
					  acpi_db_walk_for_object_counts, NULL,
					  (void *)object_info, NULL);

		acpi_os_printf("\nSummary of namespace objects:\n\n");

		for (i = 0; i < ACPI_TOTAL_TYPES; i++) {
			acpi_os_printf("%8u %s\n", object_info->types[i],
				       acpi_ut_get_type_name(i));

			total_objects += object_info->types[i];
		}

		acpi_os_printf("\n%8u Total namespace objects\n\n",
			       total_objects);

		ACPI_FREE(object_info);
		return (AE_OK);
	}

	/* Get the object type */

	type = acpi_db_match_argument(obj_type_arg, acpi_db_object_types);
	if (type == ACPI_TYPE_NOT_FOUND) {
		acpi_os_printf("Invalid or unsupported argument\n");
		return (AE_OK);
	}

	acpi_db_set_output_destination(ACPI_DB_DUPLICATE_OUTPUT);
	acpi_os_printf
	    ("Objects of type [%s] defined in the current ACPI Namespace:\n",
	     acpi_ut_get_type_name(type));

	acpi_db_set_output_destination(ACPI_DB_REDIRECTABLE_OUTPUT);

	info.count = 0;
	info.owner_id = ACPI_OWNER_ID_MAX;
	info.debug_level = ACPI_UINT32_MAX;
	info.display_type = ACPI_DISPLAY_SUMMARY | ACPI_DISPLAY_SHORT;

	/* Walk the namespace from the root */

	(void)acpi_walk_namespace(type, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
				  acpi_db_walk_for_specific_objects, NULL,
				  (void *)&info, NULL);

	acpi_os_printf
	    ("\nFound %u objects of type [%s] in the current ACPI Namespace\n",
	     info.count, acpi_ut_get_type_name(type));

	acpi_db_set_output_destination(ACPI_DB_CONSOLE_OUTPUT);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_integrity_walk
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Examine one NS node for valid values.
 *
 ******************************************************************************/

static acpi_status
acpi_db_integrity_walk(acpi_handle obj_handle,
		       u32 nesting_level, void *context, void **return_value)
{
	struct acpi_integrity_info *info =
	    (struct acpi_integrity_info *)context;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	union acpi_operand_object *object;
	u8 alias = TRUE;

	info->nodes++;

	/* Verify the NS node, and dereference aliases */

	while (alias) {
		if (ACPI_GET_DESCRIPTOR_TYPE(node) != ACPI_DESC_TYPE_NAMED) {
			acpi_os_printf
			    ("Invalid Descriptor Type for Node %p [%s] - "
			     "is %2.2X should be %2.2X\n", node,
			     acpi_ut_get_descriptor_name(node),
			     ACPI_GET_DESCRIPTOR_TYPE(node),
			     ACPI_DESC_TYPE_NAMED);
			return (AE_OK);
		}

		if ((node->type == ACPI_TYPE_LOCAL_ALIAS) ||
		    (node->type == ACPI_TYPE_LOCAL_METHOD_ALIAS)) {
			node = (struct acpi_namespace_node *)node->object;
		} else {
			alias = FALSE;
		}
	}

	if (node->type > ACPI_TYPE_LOCAL_MAX) {
		acpi_os_printf("Invalid Object Type for Node %p, Type = %X\n",
			       node, node->type);
		return (AE_OK);
	}

	if (!acpi_ut_valid_acpi_name(node->name.ascii)) {
		acpi_os_printf("Invalid AcpiName for Node %p\n", node);
		return (AE_OK);
	}

	object = acpi_ns_get_attached_object(node);
	if (object) {
		info->objects++;
		if (ACPI_GET_DESCRIPTOR_TYPE(object) != ACPI_DESC_TYPE_OPERAND) {
			acpi_os_printf
			    ("Invalid Descriptor Type for Object %p [%s]\n",
			     object, acpi_ut_get_descriptor_name(object));
		}
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_check_integrity
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check entire namespace for data structure integrity
 *
 ******************************************************************************/

void acpi_db_check_integrity(void)
{
	struct acpi_integrity_info info = { 0, 0 };

	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_integrity_walk, NULL,
				  (void *)&info, NULL);

	acpi_os_printf("Verified %u namespace nodes with %u Objects\n",
		       info.nodes, info.objects);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_for_references
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check if this namespace object refers to the target object
 *              that is passed in as the context value.
 *
 * Note: Currently doesn't check subobjects within the Node's object
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_for_references(acpi_handle obj_handle,
			    u32 nesting_level,
			    void *context, void **return_value)
{
	union acpi_operand_object *obj_desc =
	    (union acpi_operand_object *)context;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;

	/* Check for match against the namespace node itself */

	if (node == (void *)obj_desc) {
		acpi_os_printf("Object is a Node [%4.4s]\n",
			       acpi_ut_get_node_name(node));
	}

	/* Check for match against the object attached to the node */

	if (acpi_ns_get_attached_object(node) == obj_desc) {
		acpi_os_printf("Reference at Node->Object %p [%4.4s]\n",
			       node, acpi_ut_get_node_name(node));
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_find_references
 *
 * PARAMETERS:  object_arg      - String with hex value of the object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search namespace for all references to the input object
 *
 ******************************************************************************/

void acpi_db_find_references(char *object_arg)
{
	union acpi_operand_object *obj_desc;
	acpi_size address;

	/* Convert string to object pointer */

	address = strtoul(object_arg, NULL, 16);
	obj_desc = ACPI_TO_POINTER(address);

	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_walk_for_references,
				  NULL, (void *)obj_desc, NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_bus_walk
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display info about device objects that have a corresponding
 *              _PRT method.
 *
 ******************************************************************************/

static acpi_status
acpi_db_bus_walk(acpi_handle obj_handle,
		 u32 nesting_level, void *context, void **return_value)
{
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	acpi_status status;
	struct acpi_buffer buffer;
	struct acpi_namespace_node *temp_node;
	struct acpi_device_info *info;
	u32 i;

	if ((node->type != ACPI_TYPE_DEVICE) &&
	    (node->type != ACPI_TYPE_PROCESSOR)) {
		return (AE_OK);
	}

	/* Exit if there is no _PRT under this device */

	status = acpi_get_handle(node, METHOD_NAME__PRT,
				 ACPI_CAST_PTR(acpi_handle, &temp_node));
	if (ACPI_FAILURE(status)) {
		return (AE_OK);
	}

	/* Get the full path to this device object */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname(obj_handle, &buffer, TRUE);
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could Not get pathname for object %p\n",
			       obj_handle);
		return (AE_OK);
	}

	status = acpi_get_object_info(obj_handle, &info);
	if (ACPI_FAILURE(status)) {
		return (AE_OK);
	}

	/* Display the full path */

	acpi_os_printf("%-32s Type %X", (char *)buffer.pointer, node->type);
	ACPI_FREE(buffer.pointer);

	if (info->flags & ACPI_PCI_ROOT_BRIDGE) {
		acpi_os_printf(" - Is PCI Root Bridge");
	}
	acpi_os_printf("\n");

	/* _PRT info */

	acpi_os_printf("_PRT: %p\n", temp_node);

	/* Dump _ADR, _HID, _UID, _CID */

	if (info->valid & ACPI_VALID_ADR) {
		acpi_os_printf("_ADR: %8.8X%8.8X\n",
			       ACPI_FORMAT_UINT64(info->address));
	} else {
		acpi_os_printf("_ADR: <Not Present>\n");
	}

	if (info->valid & ACPI_VALID_HID) {
		acpi_os_printf("_HID: %s\n", info->hardware_id.string);
	} else {
		acpi_os_printf("_HID: <Not Present>\n");
	}

	if (info->valid & ACPI_VALID_UID) {
		acpi_os_printf("_UID: %s\n", info->unique_id.string);
	} else {
		acpi_os_printf("_UID: <Not Present>\n");
	}

	if (info->valid & ACPI_VALID_CID) {
		for (i = 0; i < info->compatible_id_list.count; i++) {
			acpi_os_printf("_CID: %s\n",
				       info->compatible_id_list.ids[i].string);
		}
	} else {
		acpi_os_printf("_CID: <Not Present>\n");
	}

	ACPI_FREE(info);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_get_bus_info
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display info about system busses.
 *
 ******************************************************************************/

void acpi_db_get_bus_info(void)
{
	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_bus_walk, NULL, NULL,
				  NULL);
}
