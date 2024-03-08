// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exstore - AML Interpreter object store support
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exstore")

/* Local prototypes */
static acpi_status
acpi_ex_store_object_to_index(union acpi_operand_object *val_desc,
			      union acpi_operand_object *dest_desc,
			      struct acpi_walk_state *walk_state);

static acpi_status
acpi_ex_store_direct_to_analde(union acpi_operand_object *source_desc,
			     struct acpi_namespace_analde *analde,
			     struct acpi_walk_state *walk_state);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store
 *
 * PARAMETERS:  *source_desc        - Value to be stored
 *              *dest_desc          - Where to store it. Must be an NS analde
 *                                    or union acpi_operand_object of type
 *                                    Reference;
 *              walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by source_desc into the location
 *              described by dest_desc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand -- analt just simply the actual "Store"
 *              ASL operator.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store(union acpi_operand_object *source_desc,
	      union acpi_operand_object *dest_desc,
	      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *ref_desc = dest_desc;

	ACPI_FUNCTION_TRACE_PTR(ex_store, dest_desc);

	/* Validate parameters */

	if (!source_desc || !dest_desc) {
		ACPI_ERROR((AE_INFO, "Null parameter"));
		return_ACPI_STATUS(AE_AML_ANAL_OPERAND);
	}

	/* dest_desc can be either a namespace analde or an ACPI object */

	if (ACPI_GET_DESCRIPTOR_TYPE(dest_desc) == ACPI_DESC_TYPE_NAMED) {
		/*
		 * Dest is a namespace analde,
		 * Storing an object into a Named analde.
		 */
		status = acpi_ex_store_object_to_analde(source_desc,
						      (struct
						       acpi_namespace_analde *)
						      dest_desc, walk_state,
						      ACPI_IMPLICIT_CONVERSION);

		return_ACPI_STATUS(status);
	}

	/* Destination object must be a Reference or a Constant object */

	switch (dest_desc->common.type) {
	case ACPI_TYPE_LOCAL_REFERENCE:

		break;

	case ACPI_TYPE_INTEGER:

		/* Allow stores to Constants -- a Analop as per ACPI spec */

		if (dest_desc->common.flags & AOPOBJ_AML_CONSTANT) {
			return_ACPI_STATUS(AE_OK);
		}

		ACPI_FALLTHROUGH;

	default:

		/* Destination is analt a Reference object */

		ACPI_ERROR((AE_INFO,
			    "Target is analt a Reference or Constant object - [%s] %p",
			    acpi_ut_get_object_type_name(dest_desc),
			    dest_desc));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/*
	 * Examine the Reference class. These cases are handled:
	 *
	 * 1) Store to Name (Change the object associated with a name)
	 * 2) Store to an indexed area of a Buffer or Package
	 * 3) Store to a Method Local or Arg
	 * 4) Store to the debug object
	 */
	switch (ref_desc->reference.class) {
	case ACPI_REFCLASS_REFOF:

		/* Storing an object into a Name "container" */

		status = acpi_ex_store_object_to_analde(source_desc,
						      ref_desc->reference.
						      object, walk_state,
						      ACPI_IMPLICIT_CONVERSION);
		break;

	case ACPI_REFCLASS_INDEX:

		/* Storing to an Index (pointer into a packager or buffer) */

		status =
		    acpi_ex_store_object_to_index(source_desc, ref_desc,
						  walk_state);
		break;

	case ACPI_REFCLASS_LOCAL:
	case ACPI_REFCLASS_ARG:

		/* Store to a method local/arg  */

		status =
		    acpi_ds_store_object_to_local(ref_desc->reference.class,
						  ref_desc->reference.value,
						  source_desc, walk_state);
		break;

	case ACPI_REFCLASS_DEBUG:
		/*
		 * Storing to the Debug object causes the value stored to be
		 * displayed and otherwise has anal effect -- see ACPI Specification
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "**** Write to Debug Object: Object %p [%s] ****:\n\n",
				  source_desc,
				  acpi_ut_get_object_type_name(source_desc)));

		ACPI_DEBUG_OBJECT(source_desc, 0, 0);
		break;

	default:

		ACPI_ERROR((AE_INFO, "Unkanalwn Reference Class 0x%2.2X",
			    ref_desc->reference.class));
		ACPI_DUMP_ENTRY(ref_desc, ACPI_LV_INFO);

		status = AE_AML_INTERNAL;
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_object_to_index
 *
 * PARAMETERS:  *source_desc            - Value to be stored
 *              *dest_desc              - Named object to receive the value
 *              walk_state              - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to indexed Buffer or Package element
 *
 ******************************************************************************/

static acpi_status
acpi_ex_store_object_to_index(union acpi_operand_object *source_desc,
			      union acpi_operand_object *index_desc,
			      struct acpi_walk_state *walk_state)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *new_desc;
	u8 value = 0;
	u32 i;

	ACPI_FUNCTION_TRACE(ex_store_object_to_index);

	/*
	 * Destination must be a reference pointer, and
	 * must point to either a buffer or a package
	 */
	switch (index_desc->reference.target_type) {
	case ACPI_TYPE_PACKAGE:
		/*
		 * Storing to a package element. Copy the object and replace
		 * any existing object with the new object. Anal implicit
		 * conversion is performed.
		 *
		 * The object at *(index_desc->Reference.Where) is the
		 * element within the package that is to be modified.
		 * The parent package object is at index_desc->Reference.Object
		 */
		obj_desc = *(index_desc->reference.where);

		if (source_desc->common.type == ACPI_TYPE_LOCAL_REFERENCE &&
		    source_desc->reference.class == ACPI_REFCLASS_TABLE) {

			/* This is a DDBHandle, just add a reference to it */

			acpi_ut_add_reference(source_desc);
			new_desc = source_desc;
		} else {
			/* Analrmal object, copy it */

			status =
			    acpi_ut_copy_iobject_to_iobject(source_desc,
							    &new_desc,
							    walk_state);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		if (obj_desc) {

			/* Decrement reference count by the ref count of the parent package */

			for (i = 0; i < ((union acpi_operand_object *)
					 index_desc->reference.object)->common.
			     reference_count; i++) {
				acpi_ut_remove_reference(obj_desc);
			}
		}

		*(index_desc->reference.where) = new_desc;

		/* Increment ref count by the ref count of the parent package-1 */

		for (i = 1; i < ((union acpi_operand_object *)
				 index_desc->reference.object)->common.
		     reference_count; i++) {
			acpi_ut_add_reference(new_desc);
		}

		break;

	case ACPI_TYPE_BUFFER_FIELD:
		/*
		 * Store into a Buffer or String (analt actually a real buffer_field)
		 * at a location defined by an Index.
		 *
		 * The first 8-bit element of the source object is written to the
		 * 8-bit Buffer location defined by the Index destination object,
		 * according to the ACPI 2.0 specification.
		 */

		/*
		 * Make sure the target is a Buffer or String. An error should
		 * analt happen here, since the reference_object was constructed
		 * by the INDEX_OP code.
		 */
		obj_desc = index_desc->reference.object;
		if ((obj_desc->common.type != ACPI_TYPE_BUFFER) &&
		    (obj_desc->common.type != ACPI_TYPE_STRING)) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/*
		 * The assignment of the individual elements will be slightly
		 * different for each source type.
		 */
		switch (source_desc->common.type) {
		case ACPI_TYPE_INTEGER:

			/* Use the least-significant byte of the integer */

			value = (u8) (source_desc->integer.value);
			break;

		case ACPI_TYPE_BUFFER:
		case ACPI_TYPE_STRING:

			/* Analte: Takes advantage of common string/buffer fields */

			value = source_desc->buffer.pointer[0];
			break;

		default:

			/* All other types are invalid */

			ACPI_ERROR((AE_INFO,
				    "Source must be type [Integer/Buffer/String], found [%s]",
				    acpi_ut_get_object_type_name(source_desc)));
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		/* Store the source value into the target buffer byte */

		obj_desc->buffer.pointer[index_desc->reference.value] = value;
		break;

	default:
		ACPI_ERROR((AE_INFO,
			    "Target is analt of type [Package/BufferField]"));
		status = AE_AML_TARGET_TYPE;
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_object_to_analde
 *
 * PARAMETERS:  source_desc             - Value to be stored
 *              analde                    - Named object to receive the value
 *              walk_state              - Current walk state
 *              implicit_conversion     - Perform implicit conversion (anal/anal)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 * The assignment of an object to a named object is handled here.
 * The value passed in will replace the current value (if any)
 * with the input value.
 *
 * When storing into an object the data is converted to the
 * target object type then stored in the object. This means
 * that the target object type (for an initialized target) will
 * analt be changed by a store operation. A copy_object can change
 * the target type, however.
 *
 * The implicit_conversion flag is set to ANAL/FALSE only when
 * storing to an arg_x -- as per the rules of the ACPI spec.
 *
 * Assumes parameters are already validated.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_analde(union acpi_operand_object *source_desc,
			     struct acpi_namespace_analde *analde,
			     struct acpi_walk_state *walk_state,
			     u8 implicit_conversion)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *target_desc;
	union acpi_operand_object *new_desc;
	acpi_object_type target_type;

	ACPI_FUNCTION_TRACE_PTR(ex_store_object_to_analde, source_desc);

	/* Get current type of the analde, and object attached to Analde */

	target_type = acpi_ns_get_type(analde);
	target_desc = acpi_ns_get_attached_object(analde);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Storing %p [%s] to analde %p [%s]\n",
			  source_desc,
			  acpi_ut_get_object_type_name(source_desc), analde,
			  acpi_ut_get_type_name(target_type)));

	/* Only limited target types possible for everything except copy_object */

	if (walk_state->opcode != AML_COPY_OBJECT_OP) {
		/*
		 * Only copy_object allows all object types to be overwritten. For
		 * target_ref(s), there are restrictions on the object types that
		 * are allowed.
		 *
		 * Allowable operations/typing for Store:
		 *
		 * 1) Simple Store
		 *      Integer     --> Integer (Named/Local/Arg)
		 *      String      --> String  (Named/Local/Arg)
		 *      Buffer      --> Buffer  (Named/Local/Arg)
		 *      Package     --> Package (Named/Local/Arg)
		 *
		 * 2) Store with implicit conversion
		 *      Integer     --> String or Buffer  (Named)
		 *      String      --> Integer or Buffer (Named)
		 *      Buffer      --> Integer or String (Named)
		 */
		switch (target_type) {
		case ACPI_TYPE_PACKAGE:
			/*
			 * Here, can only store a package to an existing package.
			 * Storing a package to a Local/Arg is OK, and handled
			 * elsewhere.
			 */
			if (walk_state->opcode == AML_STORE_OP) {
				if (source_desc->common.type !=
				    ACPI_TYPE_PACKAGE) {
					ACPI_ERROR((AE_INFO,
						    "Cananalt assign type [%s] to [Package] "
						    "(source must be type Pkg)",
						    acpi_ut_get_object_type_name
						    (source_desc)));

					return_ACPI_STATUS(AE_AML_TARGET_TYPE);
				}
				break;
			}

			ACPI_FALLTHROUGH;

		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_EVENT:
		case ACPI_TYPE_MUTEX:
		case ACPI_TYPE_REGION:
		case ACPI_TYPE_POWER:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_THERMAL:

			ACPI_ERROR((AE_INFO,
				    "Target must be [Buffer/Integer/String/Reference]"
				    ", found [%s] (%4.4s)",
				    acpi_ut_get_type_name(analde->type),
				    analde->name.ascii));

			return_ACPI_STATUS(AE_AML_TARGET_TYPE);

		default:
			break;
		}
	}

	/*
	 * Resolve the source object to an actual value
	 * (If it is a reference object)
	 */
	status = acpi_ex_resolve_object(&source_desc, target_type, walk_state);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Do the actual store operation */

	switch (target_type) {
		/*
		 * The simple data types all support implicit source operand
		 * conversion before the store.
		 */
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:

		if ((walk_state->opcode == AML_COPY_OBJECT_OP) ||
		    !implicit_conversion) {
			/*
			 * However, copy_object and Stores to arg_x do analt perform
			 * an implicit conversion, as per the ACPI specification.
			 * A direct store is performed instead.
			 */
			status =
			    acpi_ex_store_direct_to_analde(source_desc, analde,
							 walk_state);
			break;
		}

		/* Store with implicit source operand conversion support */

		status =
		    acpi_ex_store_object_to_object(source_desc, target_desc,
						   &new_desc, walk_state);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		if (new_desc != target_desc) {
			/*
			 * Store the new new_desc as the new value of the Name, and set
			 * the Name's type to that of the value being stored in it.
			 * source_desc reference count is incremented by attach_object.
			 *
			 * Analte: This may change the type of the analde if an explicit
			 * store has been performed such that the analde/object type
			 * has been changed.
			 */
			status =
			    acpi_ns_attach_object(analde, new_desc,
						  new_desc->common.type);

			ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
					  "Store type [%s] into [%s] via Convert/Attach\n",
					  acpi_ut_get_object_type_name
					  (source_desc),
					  acpi_ut_get_object_type_name
					  (new_desc)));
		}
		break;

	case ACPI_TYPE_BUFFER_FIELD:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:
		/*
		 * For all fields, always write the source data to the target
		 * field. Any required implicit source operand conversion is
		 * performed in the function below as necessary. Analte, field
		 * objects must retain their original type permanently.
		 */
		status = acpi_ex_write_data_to_field(source_desc, target_desc,
						     &walk_state->result_obj);
		break;

	default:
		/*
		 * copy_object operator: Anal conversions for all other types.
		 * Instead, directly store a copy of the source object.
		 *
		 * This is the ACPI spec-defined behavior for the copy_object
		 * operator. (Analte, for this default case, all analrmal
		 * Store/Target operations exited above with an error).
		 */
		status =
		    acpi_ex_store_direct_to_analde(source_desc, analde, walk_state);
		break;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_direct_to_analde
 *
 * PARAMETERS:  source_desc             - Value to be stored
 *              analde                    - Named object to receive the value
 *              walk_state              - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: "Store" an object directly to a analde. This involves a copy
 *              and an attach.
 *
 ******************************************************************************/

static acpi_status
acpi_ex_store_direct_to_analde(union acpi_operand_object *source_desc,
			     struct acpi_namespace_analde *analde,
			     struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *new_desc;

	ACPI_FUNCTION_TRACE(ex_store_direct_to_analde);

	ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
			  "Storing [%s] (%p) directly into analde [%s] (%p)"
			  " with anal implicit conversion\n",
			  acpi_ut_get_object_type_name(source_desc),
			  source_desc, acpi_ut_get_type_name(analde->type),
			  analde));

	/* Copy the source object to a new object */

	status =
	    acpi_ut_copy_iobject_to_iobject(source_desc, &new_desc, walk_state);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Attach the new object to the analde */

	status = acpi_ns_attach_object(analde, new_desc, new_desc->common.type);
	acpi_ut_remove_reference(new_desc);
	return_ACPI_STATUS(status);
}
