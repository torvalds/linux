// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dsinit - Object initialization namespace walk
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "actables.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_DISPATCHER
ACPI_MODULE_NAME("dsinit")

/* Local prototypes */
static acpi_status
acpi_ds_init_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_one_object
 *
 * PARAMETERS:  obj_handle      - Node for the object
 *              level           - Current nesting level
 *              context         - Points to a init info struct
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every object
 *              within the namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Operation Regions
 *
 ******************************************************************************/

static acpi_status
acpi_ds_init_one_object(acpi_handle obj_handle,
			u32 level, void *context, void **return_value)
{
	struct acpi_init_walk_info *info =
	    (struct acpi_init_walk_info *)context;
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	acpi_status status;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_ENTRY();

	/*
	 * We are only interested in NS nodes owned by the table that
	 * was just loaded
	 */
	if (node->owner_id != info->owner_id) {
		return (AE_OK);
	}

	info->object_count++;

	/* And even then, we are only interested in a few object types */

	switch (acpi_ns_get_type(obj_handle)) {
	case ACPI_TYPE_REGION:

		status = acpi_ds_initialize_region(obj_handle);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"During Region initialization %p [%4.4s]",
					obj_handle,
					acpi_ut_get_node_name(obj_handle)));
		}

		info->op_region_count++;
		break;

	case ACPI_TYPE_METHOD:
		/*
		 * Auto-serialization support. We will examine each method that is
		 * not_serialized to determine if it creates any Named objects. If
		 * it does, it will be marked serialized to prevent problems if
		 * the method is entered by two or more threads and an attempt is
		 * made to create the same named object twice -- which results in
		 * an AE_ALREADY_EXISTS exception and method abort.
		 */
		info->method_count++;
		obj_desc = acpi_ns_get_attached_object(node);
		if (!obj_desc) {
			break;
		}

		/* Ignore if already serialized */

		if (obj_desc->method.info_flags & ACPI_METHOD_SERIALIZED) {
			info->serial_method_count++;
			break;
		}

		if (acpi_gbl_auto_serialize_methods) {

			/* Parse/scan method and serialize it if necessary */

			acpi_ds_auto_serialize_method(node, obj_desc);
			if (obj_desc->method.
			    info_flags & ACPI_METHOD_SERIALIZED) {

				/* Method was just converted to Serialized */

				info->serial_method_count++;
				info->serialized_method_count++;
				break;
			}
		}

		info->non_serial_method_count++;
		break;

	case ACPI_TYPE_DEVICE:

		info->device_count++;
		break;

	default:

		break;
	}

	/*
	 * We ignore errors from above, and always return OK, since
	 * we don't want to abort the walk on a single error.
	 */
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_initialize_objects
 *
 * PARAMETERS:  table_desc      - Descriptor for parent ACPI table
 *              start_node      - Root of subtree to be initialized.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the namespace starting at "StartNode" and perform any
 *              necessary initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ds_initialize_objects(u32 table_index,
			   struct acpi_namespace_node *start_node)
{
	acpi_status status;
	struct acpi_init_walk_info info;
	struct acpi_table_header *table;
	acpi_owner_id owner_id;

	ACPI_FUNCTION_TRACE(ds_initialize_objects);

	status = acpi_tb_get_owner_id(table_index, &owner_id);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH,
			  "**** Starting initialization of namespace objects ****\n"));

	/* Set all init info to zero */

	memset(&info, 0, sizeof(struct acpi_init_walk_info));

	info.owner_id = owner_id;
	info.table_index = table_index;

	/* Walk entire namespace from the supplied root */

	/*
	 * We don't use acpi_walk_namespace since we do not want to acquire
	 * the namespace reader lock.
	 */
	status =
	    acpi_ns_walk_namespace(ACPI_TYPE_ANY, start_node, ACPI_UINT32_MAX,
				   ACPI_NS_WALK_NO_UNLOCK,
				   acpi_ds_init_one_object, NULL, &info, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "During WalkNamespace"));
	}

	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* DSDT is always the first AML table */

	if (ACPI_COMPARE_NAMESEG(table->signature, ACPI_SIG_DSDT)) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
				      "\nACPI table initialization:\n"));
	}

	/* Summary of objects initialized */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
			      "Table [%4.4s: %-8.8s] (id %.2X) - %4u Objects with %3u Devices, "
			      "%3u Regions, %4u Methods (%u/%u/%u Serial/Non/Cvt)\n",
			      table->signature, table->oem_table_id, owner_id,
			      info.object_count, info.device_count,
			      info.op_region_count, info.method_count,
			      info.serial_method_count,
			      info.non_serial_method_count,
			      info.serialized_method_count));

	ACPI_DEBUG_PRINT((ACPI_DB_DISPATCH, "%u Methods, %u Regions\n",
			  info.method_count, info.op_region_count));

	return_ACPI_STATUS(AE_OK);
}
