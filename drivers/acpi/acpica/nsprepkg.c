// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nsprepkg - Validation of package objects for predefined names
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acpredef.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsprepkg")

/* Local prototypes */
static acpi_status
acpi_ns_check_package_list(struct acpi_evaluate_info *info,
			   const union acpi_predefined_info *package,
			   union acpi_operand_object **elements, u32 count);

static acpi_status
acpi_ns_check_package_elements(struct acpi_evaluate_info *info,
			       union acpi_operand_object **elements,
			       u8 type1,
			       u32 count1,
			       u8 type2, u32 count2, u32 start_index);

static acpi_status
acpi_ns_custom_package(struct acpi_evaluate_info *info,
		       union acpi_operand_object **elements, u32 count);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_package
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned package object for the correct count and
 *              correct type of all sub-objects.
 *
 ******************************************************************************/

acpi_status
acpi_ns_check_package(struct acpi_evaluate_info *info,
		      union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	const union acpi_predefined_info *package;
	union acpi_operand_object **elements;
	acpi_status status = AE_OK;
	u32 expected_count;
	u32 count;
	u32 i;

	ACPI_FUNCTION_NAME(ns_check_package);

	/* The package info for this name is in the next table entry */

	package = info->predefined + 1;

	ACPI_DEBUG_PRINT((ACPI_DB_NAMES,
			  "%s Validating return Package of Type %X, Count %X\n",
			  info->full_pathname, package->ret_info.type,
			  return_object->package.count));

	/*
	 * For variable-length Packages, we can safely remove all embedded
	 * and trailing NULL package elements
	 */
	acpi_ns_remove_null_elements(info, package->ret_info.type,
				     return_object);

	/* Extract package count and elements array */

	elements = return_object->package.elements;
	count = return_object->package.count;

	/*
	 * Most packages must have at least one element. The only exception
	 * is the variable-length package (ACPI_PTYPE1_VAR).
	 */
	if (!count) {
		if (package->ret_info.type == ACPI_PTYPE1_VAR) {
			return (AE_OK);
		}

		ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
				      info->node_flags,
				      "Return Package has no elements (empty)"));

		return (AE_AML_OPERAND_VALUE);
	}

	/*
	 * Decode the type of the expected package contents
	 *
	 * PTYPE1 packages contain no subpackages
	 * PTYPE2 packages contain subpackages
	 */
	switch (package->ret_info.type) {
	case ACPI_PTYPE_CUSTOM:

		status = acpi_ns_custom_package(info, elements, count);
		break;

	case ACPI_PTYPE1_FIXED:
		/*
		 * The package count is fixed and there are no subpackages
		 *
		 * If package is too small, exit.
		 * If package is larger than expected, issue warning but continue
		 */
		expected_count =
		    package->ret_info.count1 + package->ret_info.count2;
		if (count < expected_count) {
			goto package_too_small;
		} else if (count > expected_count) {
			ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
					  "%s: Return Package is larger than needed - "
					  "found %u, expected %u\n",
					  info->full_pathname, count,
					  expected_count));
		}

		/* Validate all elements of the returned package */

		status = acpi_ns_check_package_elements(info, elements,
							package->ret_info.
							object_type1,
							package->ret_info.
							count1,
							package->ret_info.
							object_type2,
							package->ret_info.
							count2, 0);
		break;

	case ACPI_PTYPE1_VAR:
		/*
		 * The package count is variable, there are no subpackages, and all
		 * elements must be of the same type
		 */
		for (i = 0; i < count; i++) {
			status = acpi_ns_check_object_type(info, elements,
							   package->ret_info.
							   object_type1, i);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			elements++;
		}
		break;

	case ACPI_PTYPE1_OPTION:
		/*
		 * The package count is variable, there are no subpackages. There are
		 * a fixed number of required elements, and a variable number of
		 * optional elements.
		 *
		 * Check if package is at least as large as the minimum required
		 */
		expected_count = package->ret_info3.count;
		if (count < expected_count) {
			goto package_too_small;
		}

		/* Variable number of sub-objects */

		for (i = 0; i < count; i++) {
			if (i < package->ret_info3.count) {

				/* These are the required package elements (0, 1, or 2) */

				status =
				    acpi_ns_check_object_type(info, elements,
							      package->
							      ret_info3.
							      object_type[i],
							      i);
				if (ACPI_FAILURE(status)) {
					return (status);
				}
			} else {
				/* These are the optional package elements */

				status =
				    acpi_ns_check_object_type(info, elements,
							      package->
							      ret_info3.
							      tail_object_type,
							      i);
				if (ACPI_FAILURE(status)) {
					return (status);
				}
			}

			elements++;
		}
		break;

	case ACPI_PTYPE2_REV_FIXED:

		/* First element is the (Integer) revision */

		status =
		    acpi_ns_check_object_type(info, elements,
					      ACPI_RTYPE_INTEGER, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		elements++;
		count--;

		/* Examine the subpackages */

		status =
		    acpi_ns_check_package_list(info, package, elements, count);
		break;

	case ACPI_PTYPE2_PKG_COUNT:

		/* First element is the (Integer) count of subpackages to follow */

		status =
		    acpi_ns_check_object_type(info, elements,
					      ACPI_RTYPE_INTEGER, 0);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/*
		 * Count cannot be larger than the parent package length, but allow it
		 * to be smaller. The >= accounts for the Integer above.
		 */
		expected_count = (u32)(*elements)->integer.value;
		if (expected_count >= count) {
			goto package_too_small;
		}

		count = expected_count;
		elements++;

		/* Examine the subpackages */

		status =
		    acpi_ns_check_package_list(info, package, elements, count);
		break;

	case ACPI_PTYPE2:
	case ACPI_PTYPE2_FIXED:
	case ACPI_PTYPE2_MIN:
	case ACPI_PTYPE2_COUNT:
	case ACPI_PTYPE2_FIX_VAR:
		/*
		 * These types all return a single Package that consists of a
		 * variable number of subpackages.
		 *
		 * First, ensure that the first element is a subpackage. If not,
		 * the BIOS may have incorrectly returned the object as a single
		 * package instead of a Package of Packages (a common error if
		 * there is only one entry). We may be able to repair this by
		 * wrapping the returned Package with a new outer Package.
		 */
		if (*elements
		    && ((*elements)->common.type != ACPI_TYPE_PACKAGE)) {

			/* Create the new outer package and populate it */

			status =
			    acpi_ns_wrap_with_package(info, return_object,
						      return_object_ptr);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			/* Update locals to point to the new package (of 1 element) */

			return_object = *return_object_ptr;
			elements = return_object->package.elements;
			count = 1;
		}

		/* Examine the subpackages */

		status =
		    acpi_ns_check_package_list(info, package, elements, count);
		break;

	case ACPI_PTYPE2_VAR_VAR:
		/*
		 * Returns a variable list of packages, each with a variable list
		 * of objects.
		 */
		break;

	case ACPI_PTYPE2_UUID_PAIR:

		/* The package must contain pairs of (UUID + type) */

		if (count & 1) {
			expected_count = count + 1;
			goto package_too_small;
		}

		while (count > 0) {
			status = acpi_ns_check_object_type(info, elements,
							   package->ret_info.
							   object_type1, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			/* Validate length of the UUID buffer */

			if ((*elements)->buffer.length != 16) {
				ACPI_WARN_PREDEFINED((AE_INFO,
						      info->full_pathname,
						      info->node_flags,
						      "Invalid length for UUID Buffer"));
				return (AE_AML_OPERAND_VALUE);
			}

			status = acpi_ns_check_object_type(info, elements + 1,
							   package->ret_info.
							   object_type2, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			elements += 2;
			count -= 2;
		}
		break;

	default:

		/* Should not get here if predefined info table is correct */

		ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
				      info->node_flags,
				      "Invalid internal return type in table entry: %X",
				      package->ret_info.type));

		return (AE_AML_INTERNAL);
	}

	return (status);

package_too_small:

	/* Error exit for the case with an incorrect package count */

	ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname, info->node_flags,
			      "Return Package is too small - found %u elements, expected %u",
			      count, expected_count));

	return (AE_AML_OPERAND_VALUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_package_list
 *
 * PARAMETERS:  info            - Method execution information block
 *              package         - Pointer to package-specific info for method
 *              elements        - Element list of parent package. All elements
 *                                of this list should be of type Package.
 *              count           - Count of subpackages
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Examine a list of subpackages
 *
 ******************************************************************************/

static acpi_status
acpi_ns_check_package_list(struct acpi_evaluate_info *info,
			   const union acpi_predefined_info *package,
			   union acpi_operand_object **elements, u32 count)
{
	union acpi_operand_object *sub_package;
	union acpi_operand_object **sub_elements;
	acpi_status status;
	u32 expected_count;
	u32 i;
	u32 j;

	/*
	 * Validate each subpackage in the parent Package
	 *
	 * NOTE: assumes list of subpackages contains no NULL elements.
	 * Any NULL elements should have been removed by earlier call
	 * to acpi_ns_remove_null_elements.
	 */
	for (i = 0; i < count; i++) {
		sub_package = *elements;
		sub_elements = sub_package->package.elements;
		info->parent_package = sub_package;

		/* Each sub-object must be of type Package */

		status = acpi_ns_check_object_type(info, &sub_package,
						   ACPI_RTYPE_PACKAGE, i);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Examine the different types of expected subpackages */

		info->parent_package = sub_package;
		switch (package->ret_info.type) {
		case ACPI_PTYPE2:
		case ACPI_PTYPE2_PKG_COUNT:
		case ACPI_PTYPE2_REV_FIXED:

			/* Each subpackage has a fixed number of elements */

			expected_count =
			    package->ret_info.count1 + package->ret_info.count2;
			if (sub_package->package.count < expected_count) {
				goto package_too_small;
			}

			status =
			    acpi_ns_check_package_elements(info, sub_elements,
							   package->ret_info.
							   object_type1,
							   package->ret_info.
							   count1,
							   package->ret_info.
							   object_type2,
							   package->ret_info.
							   count2, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			break;

		case ACPI_PTYPE2_FIX_VAR:
			/*
			 * Each subpackage has a fixed number of elements and an
			 * optional element
			 */
			expected_count =
			    package->ret_info.count1 + package->ret_info.count2;
			if (sub_package->package.count < expected_count) {
				goto package_too_small;
			}

			status =
			    acpi_ns_check_package_elements(info, sub_elements,
							   package->ret_info.
							   object_type1,
							   package->ret_info.
							   count1,
							   package->ret_info.
							   object_type2,
							   sub_package->package.
							   count -
							   package->ret_info.
							   count1, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			break;

		case ACPI_PTYPE2_VAR_VAR:
			/*
			 * Each subpackage has a fixed or variable number of elements
			 */
			break;

		case ACPI_PTYPE2_FIXED:

			/* Each subpackage has a fixed length */

			expected_count = package->ret_info2.count;
			if (sub_package->package.count < expected_count) {
				goto package_too_small;
			}

			/* Check the type of each subpackage element */

			for (j = 0; j < expected_count; j++) {
				status =
				    acpi_ns_check_object_type(info,
							      &sub_elements[j],
							      package->
							      ret_info2.
							      object_type[j],
							      j);
				if (ACPI_FAILURE(status)) {
					return (status);
				}
			}
			break;

		case ACPI_PTYPE2_MIN:

			/* Each subpackage has a variable but minimum length */

			expected_count = package->ret_info.count1;
			if (sub_package->package.count < expected_count) {
				goto package_too_small;
			}

			/* Check the type of each subpackage element */

			status =
			    acpi_ns_check_package_elements(info, sub_elements,
							   package->ret_info.
							   object_type1,
							   sub_package->package.
							   count, 0, 0, 0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			break;

		case ACPI_PTYPE2_COUNT:
			/*
			 * First element is the (Integer) count of elements, including
			 * the count field (the ACPI name is num_elements)
			 */
			status = acpi_ns_check_object_type(info, sub_elements,
							   ACPI_RTYPE_INTEGER,
							   0);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			/*
			 * Make sure package is large enough for the Count and is
			 * is as large as the minimum size
			 */
			expected_count = (u32)(*sub_elements)->integer.value;
			if (sub_package->package.count < expected_count) {
				goto package_too_small;
			}

			if (sub_package->package.count <
			    package->ret_info.count1) {
				expected_count = package->ret_info.count1;
				goto package_too_small;
			}

			if (expected_count == 0) {
				/*
				 * Either the num_entries element was originally zero or it was
				 * a NULL element and repaired to an Integer of value zero.
				 * In either case, repair it by setting num_entries to be the
				 * actual size of the subpackage.
				 */
				expected_count = sub_package->package.count;
				(*sub_elements)->integer.value = expected_count;
			}

			/* Check the type of each subpackage element */

			status =
			    acpi_ns_check_package_elements(info,
							   (sub_elements + 1),
							   package->ret_info.
							   object_type1,
							   (expected_count - 1),
							   0, 0, 1);
			if (ACPI_FAILURE(status)) {
				return (status);
			}
			break;

		default:	/* Should not get here, type was validated by caller */

			ACPI_ERROR((AE_INFO, "Invalid Package type: %X",
				    package->ret_info.type));
			return (AE_AML_INTERNAL);
		}

		elements++;
	}

	return (AE_OK);

package_too_small:

	/* The subpackage count was smaller than required */

	ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname, info->node_flags,
			      "Return SubPackage[%u] is too small - found %u elements, expected %u",
			      i, sub_package->package.count, expected_count));

	return (AE_AML_OPERAND_VALUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_custom_package
 *
 * PARAMETERS:  info                - Method execution information block
 *              elements            - Pointer to the package elements array
 *              count               - Element count for the package
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a returned package object for the correct count and
 *              correct type of all sub-objects.
 *
 * NOTE: Currently used for the _BIX method only. When needed for two or more
 * methods, probably a detect/dispatch mechanism will be required.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_custom_package(struct acpi_evaluate_info *info,
		       union acpi_operand_object **elements, u32 count)
{
	u32 expected_count;
	u32 version;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_NAME(ns_custom_package);

	/* Get version number, must be Integer */

	if ((*elements)->common.type != ACPI_TYPE_INTEGER) {
		ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
				      info->node_flags,
				      "Return Package has invalid object type for version number"));
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	version = (u32)(*elements)->integer.value;
	expected_count = 21;	/* Version 1 */

	if (version == 0) {
		expected_count = 20;	/* Version 0 */
	}

	if (count < expected_count) {
		ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
				      info->node_flags,
				      "Return Package is too small - found %u elements, expected %u",
				      count, expected_count));
		return_ACPI_STATUS(AE_AML_OPERAND_VALUE);
	} else if (count > expected_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s: Return Package is larger than needed - "
				  "found %u, expected %u\n",
				  info->full_pathname, count, expected_count));
	}

	/* Validate all elements of the returned package */

	status = acpi_ns_check_package_elements(info, elements,
						ACPI_RTYPE_INTEGER, 16,
						ACPI_RTYPE_STRING, 4, 0);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Version 1 has a single trailing integer */

	if (version > 0) {
		status = acpi_ns_check_package_elements(info, elements + 20,
							ACPI_RTYPE_INTEGER, 1,
							0, 0, 20);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_package_elements
 *
 * PARAMETERS:  info            - Method execution information block
 *              elements        - Pointer to the package elements array
 *              type1           - Object type for first group
 *              count1          - Count for first group
 *              type2           - Object type for second group
 *              count2          - Count for second group
 *              start_index     - Start of the first group of elements
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check that all elements of a package are of the correct object
 *              type. Supports up to two groups of different object types.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_check_package_elements(struct acpi_evaluate_info *info,
			       union acpi_operand_object **elements,
			       u8 type1,
			       u32 count1,
			       u8 type2, u32 count2, u32 start_index)
{
	union acpi_operand_object **this_element = elements;
	acpi_status status;
	u32 i;

	/*
	 * Up to two groups of package elements are supported by the data
	 * structure. All elements in each group must be of the same type.
	 * The second group can have a count of zero.
	 */
	for (i = 0; i < count1; i++) {
		status = acpi_ns_check_object_type(info, this_element,
						   type1, i + start_index);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		this_element++;
	}

	for (i = 0; i < count2; i++) {
		status = acpi_ns_check_object_type(info, this_element,
						   type2,
						   (i + count1 + start_index));
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		this_element++;
	}

	return (AE_OK);
}
