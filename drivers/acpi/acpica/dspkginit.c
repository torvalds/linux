// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: dspkginit - Completion of deferred package initialization
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acparser.h"

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
	u8 module_level_code = FALSE;
	u16 reference_count;
	u32 index;
	u32 i;

	ACPI_FUNCTION_TRACE(ds_build_internal_package_obj);

	/* Check if we are executing module level code */

	if (walk_state->parse_flags & ACPI_PARSE_MODULE_LEVEL) {
		module_level_code = TRUE;
	}

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
	 * objects) if necessary. the count is based on the num_elements
	 * parameter. Add an extra pointer slot so that the list is always
	 * null terminated.
	 */
	if (!obj_desc->package.elements) {
		obj_desc->package.elements = ACPI_ALLOCATE_ZEROED(((acpi_size)
								   element_count
								   +
								   1) *
								  sizeof(void
									 *));

		if (!obj_desc->package.elements) {
			acpi_ut_delete_object_desc(obj_desc);
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		obj_desc->package.count = element_count;
	}

	/* First arg is element count. Second arg begins the initializer list */

	arg = op->common.value.arg;
	arg = arg->common.next;

	/*
	 * If we are executing module-level code, we will defer the
	 * full resolution of the package elements in order to support
	 * forward references from the elements. This provides
	 * compatibility with other ACPI implementations.
	 */
	if (module_level_code) {
		obj_desc->package.aml_start = walk_state->aml;
		obj_desc->package.aml_length = 0;

		ACPI_DEBUG_PRINT_RAW((ACPI_DB_PARSE,
				      "%s: Deferring resolution of Package elements\n",
				      ACPI_GET_FUNCTION_NAME));
	}

	/*
	 * Initialize the elements of the package, up to the num_elements count.
	 * Package is automatically padded with uninitialized (NULL) elements
	 * if num_elements is greater than the package list length. Likewise,
	 * Package is truncated if num_elements is less than the list length.
	 */
	for (i = 0; arg && (i < element_count); i++) {
		if (arg->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
			if (!arg->common.node) {
				/*
				 * This is the case where an expression has returned a value.
				 * The use of expressions (term_args) within individual
				 * package elements is not supported by the AML interpreter,
				 * even though the ASL grammar supports it. Example:
				 *
				 *      Name (INT1, 0x1234)
				 *
				 *      Name (PKG3, Package () {
				 *          Add (INT1, 0xAAAA0000)
				 *      })
				 *
				 *  1) No known AML interpreter supports this type of construct
				 *  2) This fixes a fault if the construct is encountered
				 */
				ACPI_EXCEPTION((AE_INFO, AE_SUPPORT,
						"Expressions within package elements are not supported"));

				/* Cleanup the return object, it is not needed */

				acpi_ut_remove_reference(walk_state->results->
							 results.obj_desc[0]);
				return_ACPI_STATUS(AE_SUPPORT);
			}

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

			if (!module_level_code) {
				/*
				 * Initialize this package element. This function handles the
				 * resolution of named references within the package.
				 * Forward references from module-level code are deferred
				 * until all ACPI tables are loaded.
				 */
				acpi_ds_init_package_element(0,
							     obj_desc->package.
							     elements[i], NULL,
							     &obj_desc->package.
							     elements[i]);
			}
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
		 * with NULLs as per the ACPI specification.
		 */
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_INFO,
				      "%s: Package List length (%u) smaller than NumElements "
				      "count (%u), padded with null elements\n",
				      ACPI_GET_FUNCTION_NAME, i,
				      element_count));
	}

	/* Module-level packages will be resolved later */

	if (!module_level_code) {
		obj_desc->package.flags |= AOPOBJ_DATA_VALID;
	}

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
	acpi_status status2;
	union acpi_generic_state scope_info;
	union acpi_operand_object *element = *element_ptr;
	struct acpi_namespace_node *resolved_node;
	struct acpi_namespace_node *original_node;
	char *external_path = "";
	acpi_object_type type;

	ACPI_FUNCTION_TRACE(ds_resolve_package_element);

	/* Check if reference element is already resolved */

	if (element->reference.resolved) {
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_PARSE,
				      "%s: Package element is already resolved\n",
				      ACPI_GET_FUNCTION_NAME));

		return_VOID;
	}

	/* Element must be a reference object of correct type */

	scope_info.scope.node = element->reference.node;	/* Prefix node */

	status = acpi_ns_lookup(&scope_info, (char *)element->reference.aml,
				ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
				ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE,
				NULL, &resolved_node);
	if (ACPI_FAILURE(status)) {
		if ((status == AE_NOT_FOUND)
		    && acpi_gbl_ignore_package_resolution_errors) {
			/*
			 * Optionally be silent about the NOT_FOUND case for the referenced
			 * name. Although this is potentially a serious problem,
			 * it can generate a lot of noise/errors on platforms whose
			 * firmware carries around a bunch of unused Package objects.
			 * To disable these errors, set this global to TRUE:
			 *     acpi_gbl_ignore_package_resolution_errors
			 *
			 * If the AML actually tries to use such a package, the unresolved
			 * element(s) will be replaced with NULL elements.
			 */

			/* Referenced name not found, set the element to NULL */

			acpi_ut_remove_reference(*element_ptr);
			*element_ptr = NULL;
			return_VOID;
		}

		status2 = acpi_ns_externalize_name(ACPI_UINT32_MAX,
						   (char *)element->reference.
						   aml, NULL, &external_path);

		ACPI_EXCEPTION((AE_INFO, status,
				"While resolving a named reference package element - %s",
				external_path));
		if (ACPI_SUCCESS(status2)) {
			ACPI_FREE(external_path);
		}

		/* Could not resolve name, set the element to NULL */

		acpi_ut_remove_reference(*element_ptr);
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
