/******************************************************************************
 *
 * Module Name: dspkginit - Completion of deferred package initialization
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("dspkginit")

/* Local prototypes */
static void
acpi_ds_resolve_package_element(union acpi_operand_object **element);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_build_internal_package_obj
 *
 * PARAMETERS:  walk_state      - Current walk state
 *              op              - Parser object to be translated
 *              element_count   - Number of elements in the package - this is
 *                                the num_elements argument to Package()
 *              obj_desc_ptr    - Where the ACPI internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a parser Op package object to the equivalent
 *              namespace object
 *
 * NOTE: The number of elements in the package will be always be the num_elements
 * count, regardless of the number of elements in the package list. If
 * num_elements is smaller, only that many package list elements are used.
 * if num_elements is larger, the Package object is padded out with
 * objects of type Uninitialized (as per ACPI spec.)
 *
 * Even though the ASL compilers do not allow num_elements to be smaller
 * than the Package list length (for the fixed length package opcode), some
 * BIOS code modifies the AML on the fly to adjust the num_elements, and
 * this code compensates for that. This also provides compatibility with
 * other AML interpreters.
 *
 ******************************************************************************/

acpi_status
acpi_ds_build_internal_package_obj(struct acpi_walk_state *walk_state,
				   union acpi_parse_object *op,
				   u32 element_count,
				   union acpi_operand_object **obj_desc_ptr)
{
	union acpi_parse_object *arg;
	union acpi_parse_object *parent;
	union acpi_operand_object *obj_desc = NULL;
	acpi_status status = AE_OK;
	u16 reference_count;
	u32 index;
	u32 i;

	ACPI_FUNCTION_TRACE(ds_build_internal_package_obj);

	/* Find the parent of a possibly nested package */

	parent = op->common.parent;
	while ((parent->common.aml_opcode == AML_PACKAGE_OP) ||
	       (parent->common.aml_opcode == AML_VARIABLE_PACKAGE_OP)) {
		parent = parent->common.parent;
	}

	/*
	 * If we are evaluating a Named package object of the form:
	 *      Name (xxxx, Package)
	 * the package object already exists, otherwise it must be created.
	 */
	obj_desc = *obj_desc_ptr;
	if (!obj_desc) {
		obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_PACKAGE);
		*obj_desc_ptr = obj_desc;
		if (!obj_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		obj_desc->package.node = parent->common.node;
	}

	if (obj_desc->package.flags & AOPOBJ_DATA_VALID) {	/* Just in case */
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Allocate the element array (array of pointers to the individual
	 * objects) based on the num_elements parameter. Add an extra pointer slot
	 * so that the list is always null terminated.
	 */
	obj_desc->package.elements = ACPI_ALLOCATE_ZEROED(((acpi_size)
							   element_count +
							   1) * sizeof(void *));

	if (!obj_desc->package.elements) {
		acpi_ut_delete_object_desc(obj_desc);
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	obj_desc->package.count = element_count;
	arg = op->common.value.arg;
	arg = arg->common.next;

	if (arg) {
		obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	}

	/*
	 * Initialize the elements of the package, up to the num_elements count.
	 * Package is automatically padded with uninitialized (NULL) elements
	 * if num_elements is greater than the package list length. Likewise,
	 * Package is truncated if num_elements is less than the list length.
	 */
	for (i = 0; arg && (i < element_count); i++) {
		if (arg->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
			if (arg->common.node->type == ACPI_TYPE_METHOD) {
				/*
				 * A method reference "looks" to the parser to be a method
				 * invocation, so we special case it here
				 */
				arg->common.aml_opcode = AML_INT_NAMEPATH_OP;
				status =
				    acpi_ds_build_internal_object(walk_state,
								  arg,
								  &obj_desc->
								  package.
								  elements[i]);
			} else {
				/* This package element is already built, just get it */

				obj_desc->package.elements[i] =
				    ACPI_CAST_PTR(union acpi_operand_object,
						  arg->common.node);
			}
		} else {
			status =
			    acpi_ds_build_internal_object(walk_state, arg,
							  &obj_desc->package.
							  elements[i]);
			if (status == AE_NOT_FOUND) {
				ACPI_ERROR((AE_INFO, "%-48s",
					    "****DS namepath not found"));
			}

			/*
			 * Initialize this package element. This function handles the
			 * resolution of named references within the package.
			 */
			acpi_ds_init_package_element(0,
						     obj_desc->package.
						     elements[i], NULL,
						     &obj_desc->package.
						     elements[i]);
		}

		if (*obj_desc_ptr) {

			/* Existing package, get existing reference count */

			reference_count =
			    (*obj_desc_ptr)->common.reference_count;
			if (reference_count > 1) {

				/* Make new element ref count match original ref count */
				/* TBD: Probably need an acpi_ut_add_references function */

				for (index = 0;
				     index < ((u32)reference_count - 1);
				     index++) {
					acpi_ut_add_reference((obj_desc->
							       package.
							       elements[i]));
				}
			}
		}

		arg = arg->common.next;
	}

	/* Check for match between num_elements and actual length of package_list */

	if (arg) {
		/*
		 * num_elements was exhausted, but there are remaining elements in
		 * the package_list. Truncate the package to num_elements.
		 *
		 * Note: technically, this is an error, from ACPI spec: "It is an
		 * error for NumElements to be less than the number of elements in
		 * the PackageList". However, we just print a message and no
		 * exception is returned. This provides compatibility with other
		 * ACPI implementations. Some firmware implementations will alter
		 * the num_elements on the fly, possibly creating this type of
		 * ill-formed package object.
		 */
		while (arg) {
			/*
			 * We must delete any package elements that were created earlier
			 * and are not going to be used because of the package truncation.
			 */
			if (arg->common.node) {
				acpi_ut_remove_reference(ACPI_CAST_PTR
							 (union
							  acpi_operand_object,
							  arg->common.node));
				arg->common.node = NULL;
			}

			/* Find out how many elements there really are */

			i++;
			arg = arg->common.next;
		}

		ACPI_INFO(("Actual Package length (%u) is larger than "
			   "NumElements field (%u), truncated",
			   i, element_count));
	} else if (i < element_count) {
		/*
		 * Arg list (elements) was exhausted, but we did not reach
		 * num_elements count.
		 *
		 * Note: this is not an error, the package is padded out
		 * with NULLs.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Package List length (%u) smaller than NumElements "
				  "count (%u), padded with null elements\n",
				  i, element_count));
	}

	obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	op->common.node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_desc);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_package_element
 *
 * PARAMETERS:  acpi_pkg_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Resolve a named reference element within a package object
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_package_element(u8 object_type,
			     union acpi_operand_object *source_object,
			     union acpi_generic_state *state, void *context)
{
	union acpi_operand_object **element_ptr;

	ACPI_FUNCTION_TRACE(ds_init_package_element);

	if (!source_object) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * The following code is a bit of a hack to workaround a (current)
	 * limitation of the acpi_pkg_callback interface. We need a pointer
	 * to the location within the element array because a new object
	 * may be created and stored there.
	 */
	if (context) {

		/* A direct call was made to this function */

		element_ptr = (union acpi_operand_object **)context;
	} else {
		/* Call came from acpi_ut_walk_package_tree */

		element_ptr = state->pkg.this_target_obj;
	}

	/* We are only interested in reference objects/elements */

	if (source_object->common.type == ACPI_TYPE_LOCAL_REFERENCE) {

		/* Attempt to resolve the (named) reference to a namespace node */

		acpi_ds_resolve_package_element(element_ptr);
	} else if (source_object->common.type == ACPI_TYPE_PACKAGE) {
		source_object->package.flags |= AOPOBJ_DATA_VALID;
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_resolve_package_element
 *
 * PARAMETERS:  element_ptr         - Pointer to a reference object
 *
 * RETURN:      Possible new element is stored to the indirect element_ptr
 *
 * DESCRIPTION: Resolve a package element that is a reference to a named
 *              object.
 *
 ******************************************************************************/

static void
acpi_ds_resolve_package_element(union acpi_operand_object **element_ptr)
{
	acpi_status status;
	union acpi_generic_state scope_info;
	union acpi_operand_object *element = *element_ptr;
	struct acpi_namespace_node *resolved_node;
	struct acpi_namespace_node *original_node;
	char *external_path = NULL;
	acpi_object_type type;

	ACPI_FUNCTION_TRACE(ds_resolve_package_element);

	/* Check if reference element is already resolved */

	if (element->reference.resolved) {
		return_VOID;
	}

	/* Element must be a reference object of correct type */

	scope_info.scope.node = element->reference.node;	/* Prefix node */

	status = acpi_ns_lookup(&scope_info, (char *)element->reference.aml,	/* Pointer to AML path */
				ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
				ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
				NULL, &resolved_node);
	if (ACPI_FAILURE(status)) {
		status = acpi_ns_externalize_name(ACPI_UINT32_MAX,
						  (char *)element->reference.
						  aml, NULL, &external_path);

		ACPI_EXCEPTION((AE_INFO, status,
				"Could not find/resolve named package element: %s",
				external_path));

		ACPI_FREE(external_path);
		*element_ptr = NULL;
		return_VOID;
	} else if (resolved_node->type == ACPI_TYPE_ANY) {

		/* Named reference not resolved, return a NULL package element */

		ACPI_ERROR((AE_INFO,
			    "Could not resolve named package element [%4.4s] in [%4.4s]",
			    resolved_node->name.ascii,
			    scope_info.scope.node->name.ascii));
		*element_ptr = NULL;
		return_VOID;
	}
#if 0
	else if (resolved_node->flags & ANOBJ_TEMPORARY) {
		/*
		 * A temporary node found here indicates that the reference is
		 * to a node that was created within this method. We are not
		 * going to allow it (especially if the package is returned
		 * from the method) -- the temporary node will be deleted out
		 * from under the method. (05/2017).
		 */
		ACPI_ERROR((AE_INFO,
			    "Package element refers to a temporary name [%4.4s], "
			    "inserting a NULL element",
			    resolved_node->name.ascii));
		*element_ptr = NULL;
		return_VOID;
	}
#endif

	/*
	 * Special handling for Alias objects. We need resolved_node to point
	 * to the Alias target. This effectively "resolves" the alias.
	 */
	if (resolved_node->type == ACPI_TYPE_LOCAL_ALIAS) {
		resolved_node = ACPI_CAST_PTR(struct acpi_namespace_node,
					      resolved_node->object);
	}

	/* Update the reference object */

	element->reference.resolved = TRUE;
	element->reference.node = resolved_node;
	type = element->reference.node->type;

	/*
	 * Attempt to resolve the node to a value before we insert it into
	 * the package. If this is a reference to a common data type,
	 * resolve it immediately. According to the ACPI spec, package
	 * elements can only be "data objects" or method references.
	 * Attempt to resolve to an Integer, Buffer, String or Package.
	 * If cannot, return the named reference (for things like Devices,
	 * Methods, etc.) Buffer Fields and Fields will resolve to simple
	 * objects (int/buf/str/pkg).
	 *
	 * NOTE: References to things like Devices, Methods, Mutexes, etc.
	 * will remain as named references. This behavior is not described
	 * in the ACPI spec, but it appears to be an oversight.
	 */
	original_node = resolved_node;
	status = acpi_ex_resolve_node_to_value(&resolved_node, NULL);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}
#if 0
/* TBD - alias support */
	/*
	 * Special handling for Alias objects. We need to setup the type
	 * and the Op->Common.Node to point to the Alias target. Note,
	 * Alias has at most one level of indirection internally.
	 */
	type = op->common.node->type;
	if (type == ACPI_TYPE_LOCAL_ALIAS) {
		type = obj_desc->common.type;
		op->common.node = ACPI_CAST_PTR(struct acpi_namespace_node,
						op->common.node->object);
	}
#endif

	switch (type) {
		/*
		 * These object types are a result of named references, so we will
		 * leave them as reference objects. In other words, these types
		 * have no intrinsic "value".
		 */
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_THERMAL:
	case ACPI_TYPE_METHOD:
		break;

	case ACPI_TYPE_MUTEX:
	case ACPI_TYPE_POWER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_EVENT:
	case ACPI_TYPE_REGION:

		/* acpi_ex_resolve_node_to_value gave these an extra reference */

		acpi_ut_remove_reference(original_node->object);
		break;

	default:
		/*
		 * For all other types - the node was resolved to an actual
		 * operand object with a value, return the object. Remove
		 * a reference on the existing object.
		 */
		acpi_ut_remove_reference(element);
		*element_ptr = (union acpi_operand_object *)resolved_node;
		break;
	}

	return_VOID;
}
