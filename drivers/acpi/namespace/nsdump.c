/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
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
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsdump")

/* Local prototypes */
#ifdef ACPI_OBSOLETE_FUNCTIONS
void acpi_ns_dump_root_devices(void);

static acpi_status
acpi_ns_dump_one_device(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);
#endif

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_print_pathname
 *
 * PARAMETERS:  num_segments        - Number of ACPI name segments
 *              Pathname            - The compressed (internal) path
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *
 ******************************************************************************/

void acpi_ns_print_pathname(u32 num_segments, char *pathname)
{
	acpi_native_uint i;

	ACPI_FUNCTION_NAME(ns_print_pathname);

	if (!(acpi_dbg_level & ACPI_LV_NAMES)
	    || !(acpi_dbg_layer & ACPI_NAMESPACE)) {
		return;
	}

	/* Print the entire name */

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES, "["));

	while (num_segments) {
		for (i = 0; i < 4; i++) {
			ACPI_IS_PRINT(pathname[i]) ?
			    acpi_os_printf("%c", pathname[i]) :
			    acpi_os_printf("?");
		}

		pathname += ACPI_NAME_SIZE;
		num_segments--;
		if (num_segments) {
			acpi_os_printf(".");
		}
	}

	acpi_os_printf("]\n");
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_pathname
 *
 * PARAMETERS:  Handle              - Object
 *              Msg                 - Prefix message
 *              Level               - Desired debug level
 *              Component           - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

void
acpi_ns_dump_pathname(acpi_handle handle, char *msg, u32 level, u32 component)
{

	ACPI_FUNCTION_TRACE(ns_dump_pathname);

	/* Do this only if the requested debug level and component are enabled */

	if (!(acpi_dbg_level & level) || !(acpi_dbg_layer & component)) {
		return_VOID;
	}

	/* Convert handle to a full pathname and print it (with supplied message) */

	acpi_ns_print_node_pathname(handle, msg);
	acpi_os_printf("\n");
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_one_object
 *
 * PARAMETERS:  obj_handle          - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into walk_namespace
 *              return_value        - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dump a single Node
 *              This procedure is a user_function called by acpi_ns_walk_namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ns_dump_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value)
{
	struct acpi_walk_info *info = (struct acpi_walk_info *)context;
	struct acpi_namespace_node *this_node;
	union acpi_operand_object *obj_desc = NULL;
	acpi_object_type obj_type;
	acpi_object_type type;
	u32 bytes_to_dump;
	u32 dbg_level;
	u32 i;

	ACPI_FUNCTION_NAME(ns_dump_one_object);

	/* Is output enabled? */

	if (!(acpi_dbg_level & info->debug_level)) {
		return (AE_OK);
	}

	if (!obj_handle) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Null object handle\n"));
		return (AE_OK);
	}

	this_node = acpi_ns_map_handle_to_node(obj_handle);
	type = this_node->type;

	/* Check if the owner matches */

	if ((info->owner_id != ACPI_OWNER_ID_MAX) &&
	    (info->owner_id != this_node->owner_id)) {
		return (AE_OK);
	}

	if (!(info->display_type & ACPI_DISPLAY_SHORT)) {

		/* Indent the object according to the level */

		acpi_os_printf("%2d%*s", (u32) level - 1, (int)level * 2, " ");

		/* Check the node type and name */

		if (type > ACPI_TYPE_LOCAL_MAX) {
			ACPI_WARNING((AE_INFO, "Invalid ACPI Object Type %08X",
				      type));
		}

		if (!acpi_ut_valid_acpi_name(this_node->name.integer)) {
			this_node->name.integer =
			    acpi_ut_repair_name(this_node->name.ascii);

			ACPI_WARNING((AE_INFO, "Invalid ACPI Name %08X",
				      this_node->name.integer));
		}

		acpi_os_printf("%4.4s", acpi_ut_get_node_name(this_node));
	}

	/*
	 * Now we can print out the pertinent information
	 */
	acpi_os_printf(" %-12s %p %2.2X ",
		       acpi_ut_get_type_name(type), this_node,
		       this_node->owner_id);

	dbg_level = acpi_dbg_level;
	acpi_dbg_level = 0;
	obj_desc = acpi_ns_get_attached_object(this_node);
	acpi_dbg_level = dbg_level;

	/* Temp nodes are those nodes created by a control method */

	if (this_node->flags & ANOBJ_TEMPORARY) {
		acpi_os_printf("(T) ");
	}

	switch (info->display_type & ACPI_DISPLAY_MASK) {
	case ACPI_DISPLAY_SUMMARY:

		if (!obj_desc) {

			/* No attached object, we are done */

			acpi_os_printf("\n");
			return (AE_OK);
		}

		switch (type) {
		case ACPI_TYPE_PROCESSOR:

			acpi_os_printf("ID %X Len %.4X Addr %p\n",
				       obj_desc->processor.proc_id,
				       obj_desc->processor.length,
				       (char *)obj_desc->processor.address);
			break;

		case ACPI_TYPE_DEVICE:

			acpi_os_printf("Notify Object: %p\n", obj_desc);
			break;

		case ACPI_TYPE_METHOD:

			acpi_os_printf("Args %X Len %.4X Aml %p\n",
				       (u32) obj_desc->method.param_count,
				       obj_desc->method.aml_length,
				       obj_desc->method.aml_start);
			break;

		case ACPI_TYPE_INTEGER:

			acpi_os_printf("= %8.8X%8.8X\n",
				       ACPI_FORMAT_UINT64(obj_desc->integer.
							  value));
			break;

		case ACPI_TYPE_PACKAGE:

			if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf("Elements %.2X\n",
					       obj_desc->package.count);
			} else {
				acpi_os_printf("[Length not yet evaluated]\n");
			}
			break;

		case ACPI_TYPE_BUFFER:

			if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf("Len %.2X",
					       obj_desc->buffer.length);

				/* Dump some of the buffer */

				if (obj_desc->buffer.length > 0) {
					acpi_os_printf(" =");
					for (i = 0;
					     (i < obj_desc->buffer.length
					      && i < 12); i++) {
						acpi_os_printf(" %.2hX",
							       obj_desc->buffer.
							       pointer[i]);
					}
				}
				acpi_os_printf("\n");
			} else {
				acpi_os_printf("[Length not yet evaluated]\n");
			}
			break;

		case ACPI_TYPE_STRING:

			acpi_os_printf("Len %.2X ", obj_desc->string.length);
			acpi_ut_print_string(obj_desc->string.pointer, 32);
			acpi_os_printf("\n");
			break;

		case ACPI_TYPE_REGION:

			acpi_os_printf("[%s]",
				       acpi_ut_get_region_name(obj_desc->region.
							       space_id));
			if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf(" Addr %8.8X%8.8X Len %.4X\n",
					       ACPI_FORMAT_UINT64(obj_desc->
								  region.
								  address),
					       obj_desc->region.length);
			} else {
				acpi_os_printf
				    (" [Address/Length not yet evaluated]\n");
			}
			break;

		case ACPI_TYPE_LOCAL_REFERENCE:

			acpi_os_printf("[%s]\n",
				       acpi_ps_get_opcode_name(obj_desc->
							       reference.
							       opcode));
			break;

		case ACPI_TYPE_BUFFER_FIELD:

			if (obj_desc->buffer_field.buffer_obj &&
			    obj_desc->buffer_field.buffer_obj->buffer.node) {
				acpi_os_printf("Buf [%4.4s]",
					       acpi_ut_get_node_name(obj_desc->
								     buffer_field.
								     buffer_obj->
								     buffer.
								     node));
			}
			break;

		case ACPI_TYPE_LOCAL_REGION_FIELD:

			acpi_os_printf("Rgn [%4.4s]",
				       acpi_ut_get_node_name(obj_desc->
							     common_field.
							     region_obj->region.
							     node));
			break;

		case ACPI_TYPE_LOCAL_BANK_FIELD:

			acpi_os_printf("Rgn [%4.4s] Bnk [%4.4s]",
				       acpi_ut_get_node_name(obj_desc->
							     common_field.
							     region_obj->region.
							     node),
				       acpi_ut_get_node_name(obj_desc->
							     bank_field.
							     bank_obj->
							     common_field.
							     node));
			break;

		case ACPI_TYPE_LOCAL_INDEX_FIELD:

			acpi_os_printf("Idx [%4.4s] Dat [%4.4s]",
				       acpi_ut_get_node_name(obj_desc->
							     index_field.
							     index_obj->
							     common_field.node),
				       acpi_ut_get_node_name(obj_desc->
							     index_field.
							     data_obj->
							     common_field.
							     node));
			break;

		case ACPI_TYPE_LOCAL_ALIAS:
		case ACPI_TYPE_LOCAL_METHOD_ALIAS:

			acpi_os_printf("Target %4.4s (%p)\n",
				       acpi_ut_get_node_name(obj_desc),
				       obj_desc);
			break;

		default:

			acpi_os_printf("Object %p\n", obj_desc);
			break;
		}

		/* Common field handling */

		switch (type) {
		case ACPI_TYPE_BUFFER_FIELD:
		case ACPI_TYPE_LOCAL_REGION_FIELD:
		case ACPI_TYPE_LOCAL_BANK_FIELD:
		case ACPI_TYPE_LOCAL_INDEX_FIELD:

			acpi_os_printf(" Off %.3X Len %.2X Acc %.2hd\n",
				       (obj_desc->common_field.
					base_byte_offset * 8)
				       +
				       obj_desc->common_field.
				       start_field_bit_offset,
				       obj_desc->common_field.bit_length,
				       obj_desc->common_field.
				       access_byte_width);
			break;

		default:
			break;
		}
		break;

	case ACPI_DISPLAY_OBJECTS:

		acpi_os_printf("O:%p", obj_desc);
		if (!obj_desc) {

			/* No attached object, we are done */

			acpi_os_printf("\n");
			return (AE_OK);
		}

		acpi_os_printf("(R%d)", obj_desc->common.reference_count);

		switch (type) {
		case ACPI_TYPE_METHOD:

			/* Name is a Method and its AML offset/length are set */

			acpi_os_printf(" M:%p-%X\n", obj_desc->method.aml_start,
				       obj_desc->method.aml_length);
			break;

		case ACPI_TYPE_INTEGER:

			acpi_os_printf(" I:%8.8X8.8%X\n",
				       ACPI_FORMAT_UINT64(obj_desc->integer.
							  value));
			break;

		case ACPI_TYPE_STRING:

			acpi_os_printf(" S:%p-%X\n", obj_desc->string.pointer,
				       obj_desc->string.length);
			break;

		case ACPI_TYPE_BUFFER:

			acpi_os_printf(" B:%p-%X\n", obj_desc->buffer.pointer,
				       obj_desc->buffer.length);
			break;

		default:

			acpi_os_printf("\n");
			break;
		}
		break;

	default:
		acpi_os_printf("\n");
		break;
	}

	/* If debug turned off, done */

	if (!(acpi_dbg_level & ACPI_LV_VALUES)) {
		return (AE_OK);
	}

	/* If there is an attached object, display it */

	dbg_level = acpi_dbg_level;
	acpi_dbg_level = 0;
	obj_desc = acpi_ns_get_attached_object(this_node);
	acpi_dbg_level = dbg_level;

	/* Dump attached objects */

	while (obj_desc) {
		obj_type = ACPI_TYPE_INVALID;
		acpi_os_printf("Attached Object %p: ", obj_desc);

		/* Decode the type of attached object and dump the contents */

		switch (ACPI_GET_DESCRIPTOR_TYPE(obj_desc)) {
		case ACPI_DESC_TYPE_NAMED:

			acpi_os_printf("(Ptr to Node)\n");
			bytes_to_dump = sizeof(struct acpi_namespace_node);
			ACPI_DUMP_BUFFER(obj_desc, bytes_to_dump);
			break;

		case ACPI_DESC_TYPE_OPERAND:

			obj_type = ACPI_GET_OBJECT_TYPE(obj_desc);

			if (obj_type > ACPI_TYPE_LOCAL_MAX) {
				acpi_os_printf
				    ("(Ptr to ACPI Object type %X [UNKNOWN])\n",
				     obj_type);
				bytes_to_dump = 32;
			} else {
				acpi_os_printf
				    ("(Ptr to ACPI Object type %X [%s])\n",
				     obj_type, acpi_ut_get_type_name(obj_type));
				bytes_to_dump =
				    sizeof(union acpi_operand_object);
			}

			ACPI_DUMP_BUFFER(obj_desc, bytes_to_dump);
			break;

		default:

			break;
		}

		/* If value is NOT an internal object, we are done */

		if (ACPI_GET_DESCRIPTOR_TYPE(obj_desc) !=
		    ACPI_DESC_TYPE_OPERAND) {
			goto cleanup;
		}

		/*
		 * Valid object, get the pointer to next level, if any
		 */
		switch (obj_type) {
		case ACPI_TYPE_BUFFER:
		case ACPI_TYPE_STRING:
			/*
			 * NOTE: takes advantage of common fields between string/buffer
			 */
			bytes_to_dump = obj_desc->string.length;
			obj_desc = (void *)obj_desc->string.pointer;
			acpi_os_printf("(Buffer/String pointer %p length %X)\n",
				       obj_desc, bytes_to_dump);
			ACPI_DUMP_BUFFER(obj_desc, bytes_to_dump);
			goto cleanup;

		case ACPI_TYPE_BUFFER_FIELD:
			obj_desc =
			    (union acpi_operand_object *)obj_desc->buffer_field.
			    buffer_obj;
			break;

		case ACPI_TYPE_PACKAGE:
			obj_desc = (void *)obj_desc->package.elements;
			break;

		case ACPI_TYPE_METHOD:
			obj_desc = (void *)obj_desc->method.aml_start;
			break;

		case ACPI_TYPE_LOCAL_REGION_FIELD:
			obj_desc = (void *)obj_desc->field.region_obj;
			break;

		case ACPI_TYPE_LOCAL_BANK_FIELD:
			obj_desc = (void *)obj_desc->bank_field.region_obj;
			break;

		case ACPI_TYPE_LOCAL_INDEX_FIELD:
			obj_desc = (void *)obj_desc->index_field.index_obj;
			break;

		default:
			goto cleanup;
		}

		obj_type = ACPI_TYPE_INVALID;	/* Terminate loop after next pass */
	}

      cleanup:
	acpi_os_printf("\n");
	return (AE_OK);
}

#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_objects
 *
 * PARAMETERS:  Type                - Object type to be dumped
 *              display_type        - 0 or ACPI_DISPLAY_SUMMARY
 *              max_depth           - Maximum depth of dump. Use ACPI_UINT32_MAX
 *                                    for an effectively unlimited depth.
 *              owner_id            - Dump only objects owned by this ID.  Use
 *                                    ACPI_UINT32_MAX to match all owners.
 *              start_handle        - Where in namespace to start/end search
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump typed objects within the loaded namespace.
 *              Uses acpi_ns_walk_namespace in conjunction with acpi_ns_dump_one_object.
 *
 ******************************************************************************/

void
acpi_ns_dump_objects(acpi_object_type type,
		     u8 display_type,
		     u32 max_depth,
		     acpi_owner_id owner_id, acpi_handle start_handle)
{
	struct acpi_walk_info info;

	ACPI_FUNCTION_ENTRY();

	info.debug_level = ACPI_LV_TABLES;
	info.owner_id = owner_id;
	info.display_type = display_type;

	(void)acpi_ns_walk_namespace(type, start_handle, max_depth,
				     ACPI_NS_WALK_NO_UNLOCK |
				     ACPI_NS_WALK_TEMP_NODES,
				     acpi_ns_dump_one_object, (void *)&info,
				     NULL);
}
#endif				/* ACPI_FUTURE_USAGE */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_entry
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              debug_level         - Output level
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump a single Node
 *
 ******************************************************************************/

void acpi_ns_dump_entry(acpi_handle handle, u32 debug_level)
{
	struct acpi_walk_info info;

	ACPI_FUNCTION_ENTRY();

	info.debug_level = debug_level;
	info.owner_id = ACPI_OWNER_ID_MAX;
	info.display_type = ACPI_DISPLAY_SUMMARY;

	(void)acpi_ns_dump_one_object(handle, 1, &info, NULL);
}

#ifdef ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_dump_tables
 *
 * PARAMETERS:  search_base         - Root of subtree to be dumped, or
 *                                    NS_ALL to dump the entire namespace
 *              max_depth           - Maximum depth of dump.  Use INT_MAX
 *                                    for an effectively unlimited depth.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the name space, or a portion of it.
 *
 ******************************************************************************/

void acpi_ns_dump_tables(acpi_handle search_base, u32 max_depth)
{
	acpi_handle search_handle = search_base;

	ACPI_FUNCTION_TRACE(ns_dump_tables);

	if (!acpi_gbl_root_node) {
		/*
		 * If the name space has not been initialized,
		 * there is nothing to dump.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_TABLES,
				  "namespace not initialized!\n"));
		return_VOID;
	}

	if (ACPI_NS_ALL == search_base) {

		/* Entire namespace */

		search_handle = acpi_gbl_root_node;
		ACPI_DEBUG_PRINT((ACPI_DB_TABLES, "\\\n"));
	}

	acpi_ns_dump_objects(ACPI_TYPE_ANY, ACPI_DISPLAY_OBJECTS, max_depth,
			     ACPI_OWNER_ID_MAX, search_handle);
	return_VOID;
}
#endif				/* _ACPI_ASL_COMPILER */
#endif				/* defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER) */
