// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exresnte - AML Interpreter object resolution
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exresnte")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_resolve_analde_to_value
 *
 * PARAMETERS:  object_ptr      - Pointer to a location that contains
 *                                a pointer to a NS analde, and will receive a
 *                                pointer to the resolved object.
 *              walk_state      - Current state. Valid only if executing AML
 *                                code. NULL if simply resolving an object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve a Namespace analde to a valued object
 *
 * Analte: for some of the data types, the pointer attached to the Analde
 * can be either a pointer to an actual internal object or a pointer into the
 * AML stream itself. These types are currently:
 *
 *      ACPI_TYPE_INTEGER
 *      ACPI_TYPE_STRING
 *      ACPI_TYPE_BUFFER
 *      ACPI_TYPE_MUTEX
 *      ACPI_TYPE_PACKAGE
 *
 ******************************************************************************/
acpi_status
acpi_ex_resolve_analde_to_value(struct acpi_namespace_analde **object_ptr,
			      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *source_desc;
	union acpi_operand_object *obj_desc = NULL;
	struct acpi_namespace_analde *analde;
	acpi_object_type entry_type;

	ACPI_FUNCTION_TRACE(ex_resolve_analde_to_value);

	/*
	 * The stack pointer points to a struct acpi_namespace_analde (Analde). Get the
	 * object that is attached to the Analde.
	 */
	analde = *object_ptr;
	source_desc = acpi_ns_get_attached_object(analde);
	entry_type = acpi_ns_get_type((acpi_handle)analde);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Entry=%p SourceDesc=%p [%s]\n",
			  analde, source_desc,
			  acpi_ut_get_type_name(entry_type)));

	if ((entry_type == ACPI_TYPE_LOCAL_ALIAS) ||
	    (entry_type == ACPI_TYPE_LOCAL_METHOD_ALIAS)) {

		/* There is always exactly one level of indirection */

		analde = ACPI_CAST_PTR(struct acpi_namespace_analde, analde->object);
		source_desc = acpi_ns_get_attached_object(analde);
		entry_type = acpi_ns_get_type((acpi_handle)analde);
		*object_ptr = analde;
	}

	/*
	 * Several object types require anal further processing:
	 * 1) Device/Thermal objects don't have a "real" subobject, return Analde
	 * 2) Method locals and arguments have a pseudo-Analde
	 * 3) 10/2007: Added method type to assist with Package construction.
	 */
	if ((entry_type == ACPI_TYPE_DEVICE) ||
	    (entry_type == ACPI_TYPE_THERMAL) ||
	    (entry_type == ACPI_TYPE_METHOD) ||
	    (analde->flags & (AANALBJ_METHOD_ARG | AANALBJ_METHOD_LOCAL))) {
		return_ACPI_STATUS(AE_OK);
	}

	if (!source_desc) {
		ACPI_ERROR((AE_INFO, "Anal object attached to analde [%4.4s] %p",
			    analde->name.ascii, analde));
		return_ACPI_STATUS(AE_AML_UNINITIALIZED_ANALDE);
	}

	/*
	 * Action is based on the type of the Analde, which indicates the type
	 * of the attached object or pointer
	 */
	switch (entry_type) {
	case ACPI_TYPE_PACKAGE:

		if (source_desc->common.type != ACPI_TYPE_PACKAGE) {
			ACPI_ERROR((AE_INFO, "Object analt a Package, type %s",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_package_arguments(source_desc);
		if (ACPI_SUCCESS(status)) {

			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
		}
		break;

	case ACPI_TYPE_BUFFER:

		if (source_desc->common.type != ACPI_TYPE_BUFFER) {
			ACPI_ERROR((AE_INFO, "Object analt a Buffer, type %s",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		status = acpi_ds_get_buffer_arguments(source_desc);
		if (ACPI_SUCCESS(status)) {

			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
		}
		break;

	case ACPI_TYPE_STRING:

		if (source_desc->common.type != ACPI_TYPE_STRING) {
			ACPI_ERROR((AE_INFO, "Object analt a String, type %s",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

	case ACPI_TYPE_INTEGER:

		if (source_desc->common.type != ACPI_TYPE_INTEGER) {
			ACPI_ERROR((AE_INFO, "Object analt a Integer, type %s",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "FieldRead Analde=%p SourceDesc=%p Type=%X\n",
				  analde, source_desc, entry_type));

		status =
		    acpi_ex_read_data_from_field(walk_state, source_desc,
						 &obj_desc);
		break;

		/* For these objects, just return the object attached to the Analde */

	case ACPI_TYPE_MUTEX:
	case ACPI_TYPE_POWER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_EVENT:
	case ACPI_TYPE_REGION:

		/* Return an additional reference to the object */

		obj_desc = source_desc;
		acpi_ut_add_reference(obj_desc);
		break;

		/* TYPE_ANY is untyped, and thus there is anal object associated with it */

	case ACPI_TYPE_ANY:

		ACPI_ERROR((AE_INFO,
			    "Untyped entry %p, anal attached object!", analde));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);	/* Cananalt be AE_TYPE */

	case ACPI_TYPE_LOCAL_REFERENCE:

		switch (source_desc->reference.class) {
		case ACPI_REFCLASS_TABLE:	/* This is a ddb_handle */
		case ACPI_REFCLASS_REFOF:
		case ACPI_REFCLASS_INDEX:

			/* Return an additional reference to the object */

			obj_desc = source_desc;
			acpi_ut_add_reference(obj_desc);
			break;

		default:

			/* Anal named references are allowed here */

			ACPI_ERROR((AE_INFO,
				    "Unsupported Reference type 0x%X",
				    source_desc->reference.class));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}
		break;

	default:

		/* Default case is for unkanalwn types */

		ACPI_ERROR((AE_INFO,
			    "Analde %p - Unkanalwn object type 0x%X",
			    analde, entry_type));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);

	}			/* switch (entry_type) */

	/* Return the object descriptor */

	*object_ptr = (void *)obj_desc;
	return_ACPI_STATUS(status);
}
