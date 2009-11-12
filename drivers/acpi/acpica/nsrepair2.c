/******************************************************************************
 *
 * Module Name: nsrepair2 - Repair for objects returned by specific
 *                          predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2009, Intel Corp.
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

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsrepair2")

/*
 * Information structure and handler for ACPI predefined names that can
 * be repaired on a per-name basis.
 */
typedef
acpi_status(*acpi_repair_function) (struct acpi_predefined_data *data,
				    union acpi_operand_object **return_object_ptr);

typedef struct acpi_repair_info {
	char name[ACPI_NAME_SIZE];
	acpi_repair_function repair_function;

} acpi_repair_info;

/* Local prototypes */

static const struct acpi_repair_info *acpi_ns_match_repairable_name(struct
								    acpi_namespace_node
								    *node);

static acpi_status
acpi_ns_repair_ALR(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_PSS(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_TSS(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_check_sorted_list(struct acpi_predefined_data *data,
			  union acpi_operand_object *return_object,
			  u32 expected_count,
			  u32 sort_index,
			  u8 sort_direction, char *sort_key_name);

static acpi_status
acpi_ns_remove_null_elements(union acpi_operand_object *package);

static acpi_status
acpi_ns_sort_list(union acpi_operand_object **elements,
		  u32 count, u32 index, u8 sort_direction);

/* Values for sort_direction above */

#define ACPI_SORT_ASCENDING     0
#define ACPI_SORT_DESCENDING    1

/*
 * This table contains the names of the predefined methods for which we can
 * perform more complex repairs.
 *
 * _ALR: Sort the list ascending by ambient_illuminance if necessary
 * _PSS: Sort the list descending by Power if necessary
 * _TSS: Sort the list descending by Power if necessary
 */
static const struct acpi_repair_info acpi_ns_repairable_names[] = {
	{"_ALR", acpi_ns_repair_ALR},
	{"_PSS", acpi_ns_repair_PSS},
	{"_TSS", acpi_ns_repair_TSS},
	{{0, 0, 0, 0}, NULL}	/* Table terminator */
};

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_complex_repairs
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              Node                - Namespace node for the method/object
 *              validate_status     - Original status of earlier validation
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if repair was successful. If name is not
 *              matched, validate_status is returned.
 *
 * DESCRIPTION: Attempt to repair/convert a return object of a type that was
 *              not expected.
 *
 *****************************************************************************/

acpi_status
acpi_ns_complex_repairs(struct acpi_predefined_data *data,
			struct acpi_namespace_node *node,
			acpi_status validate_status,
			union acpi_operand_object **return_object_ptr)
{
	const struct acpi_repair_info *predefined;
	acpi_status status;

	/* Check if this name is in the list of repairable names */

	predefined = acpi_ns_match_repairable_name(node);
	if (!predefined) {
		return (validate_status);
	}

	status = predefined->repair_function(data, return_object_ptr);
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_match_repairable_name
 *
 * PARAMETERS:  Node                - Namespace node for the method/object
 *
 * RETURN:      Pointer to entry in repair table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the repairable object list.
 *
 *****************************************************************************/

static const struct acpi_repair_info *acpi_ns_match_repairable_name(struct
								    acpi_namespace_node
								    *node)
{
	const struct acpi_repair_info *this_name;

	/* Search info table for a repairable predefined method/object name */

	this_name = acpi_ns_repairable_names;
	while (this_name->repair_function) {
		if (ACPI_COMPARE_NAME(node->name.ascii, this_name->name)) {
			return (this_name);
		}
		this_name++;
	}

	return (NULL);		/* Not found */
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_ALR
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _ALR object. If necessary, sort the object list
 *              ascending by the ambient illuminance values.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_ALR(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status;

	status = acpi_ns_check_sorted_list(data, return_object, 2, 1,
					   ACPI_SORT_ASCENDING,
					   "AmbientIlluminance");

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_TSS
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _TSS object. If necessary, sort the object list
 *              descending by the power dissipation values.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_TSS(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status;

	status = acpi_ns_check_sorted_list(data, return_object, 5, 1,
					   ACPI_SORT_DESCENDING,
					   "PowerDissipation");

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_PSS
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _PSS object. If necessary, sort the object list
 *              by the CPU frequencies. Check that the power dissipation values
 *              are all proportional to CPU frequency (i.e., sorting by
 *              frequency should be the same as sorting by power.)
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_PSS(struct acpi_predefined_data *data,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object **outer_elements;
	u32 outer_element_count;
	union acpi_operand_object **elements;
	union acpi_operand_object *obj_desc;
	u32 previous_value;
	acpi_status status;
	u32 i;

	/*
	 * Entries (sub-packages) in the _PSS Package must be sorted by power
	 * dissipation, in descending order. If it appears that the list is
	 * incorrectly sorted, sort it. We sort by cpu_frequency, since this
	 * should be proportional to the power.
	 */
	status = acpi_ns_check_sorted_list(data, return_object, 6, 0,
					   ACPI_SORT_DESCENDING,
					   "CpuFrequency");
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/*
	 * We now know the list is correctly sorted by CPU frequency. Check if
	 * the power dissipation values are proportional.
	 */
	previous_value = ACPI_UINT32_MAX;
	outer_elements = return_object->package.elements;
	outer_element_count = return_object->package.count;

	for (i = 0; i < outer_element_count; i++) {
		elements = (*outer_elements)->package.elements;
		obj_desc = elements[1];	/* Index1 = power_dissipation */

		if ((u32) obj_desc->integer.value > previous_value) {
			ACPI_WARN_PREDEFINED((AE_INFO, data->pathname,
					      data->node_flags,
					      "SubPackage[%u,%u] - suspicious power dissipation values",
					      i - 1, i));
		}

		previous_value = (u32) obj_desc->integer.value;
		outer_elements++;
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_sorted_list
 *
 * PARAMETERS:  Data                - Pointer to validation data structure
 *              return_object       - Pointer to the top-level returned object
 *              expected_count      - Minimum length of each sub-package
 *              sort_index          - Sub-package entry to sort on
 *              sort_direction      - Ascending or descending
 *              sort_key_name       - Name of the sort_index field
 *
 * RETURN:      Status. AE_OK if the list is valid and is sorted correctly or
 *              has been repaired by sorting the list.
 *
 * DESCRIPTION: Check if the package list is valid and sorted correctly by the
 *              sort_index. If not, then sort the list.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_check_sorted_list(struct acpi_predefined_data *data,
			  union acpi_operand_object *return_object,
			  u32 expected_count,
			  u32 sort_index,
			  u8 sort_direction, char *sort_key_name)
{
	u32 outer_element_count;
	union acpi_operand_object **outer_elements;
	union acpi_operand_object **elements;
	union acpi_operand_object *obj_desc;
	u32 i;
	u32 previous_value;
	acpi_status status;

	/* The top-level object must be a package */

	if (return_object->common.type != ACPI_TYPE_PACKAGE) {
		return (AE_AML_OPERAND_TYPE);
	}

	/*
	 * Detect any NULL package elements and remove them from the
	 * package.
	 *
	 * TBD: We may want to do this for all predefined names that
	 * return a variable-length package of packages.
	 */
	status = acpi_ns_remove_null_elements(return_object);
	if (status == AE_NULL_ENTRY) {
		ACPI_INFO_PREDEFINED((AE_INFO, data->pathname, data->node_flags,
				      "NULL elements removed from package"));

		/* Exit if package is now zero length */

		if (!return_object->package.count) {
			return (AE_NULL_ENTRY);
		}
	}

	outer_elements = return_object->package.elements;
	outer_element_count = return_object->package.count;
	if (!outer_element_count) {
		return (AE_AML_PACKAGE_LIMIT);
	}

	previous_value = 0;
	if (sort_direction == ACPI_SORT_DESCENDING) {
		previous_value = ACPI_UINT32_MAX;
	}

	/* Examine each subpackage */

	for (i = 0; i < outer_element_count; i++) {

		/* Each element of the top-level package must also be a package */

		if ((*outer_elements)->common.type != ACPI_TYPE_PACKAGE) {
			return (AE_AML_OPERAND_TYPE);
		}

		/* Each sub-package must have the minimum length */

		if ((*outer_elements)->package.count < expected_count) {
			return (AE_AML_PACKAGE_LIMIT);
		}

		elements = (*outer_elements)->package.elements;
		obj_desc = elements[sort_index];

		if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
			return (AE_AML_OPERAND_TYPE);
		}

		/*
		 * The list must be sorted in the specified order. If we detect a
		 * discrepancy, issue a warning and sort the entire list
		 */
		if (((sort_direction == ACPI_SORT_ASCENDING) &&
		     (obj_desc->integer.value < previous_value)) ||
		    ((sort_direction == ACPI_SORT_DESCENDING) &&
		     (obj_desc->integer.value > previous_value))) {
			status =
			    acpi_ns_sort_list(return_object->package.elements,
					      outer_element_count, sort_index,
					      sort_direction);
			if (ACPI_FAILURE(status)) {
				return (status);
			}

			data->flags |= ACPI_OBJECT_REPAIRED;

			ACPI_INFO_PREDEFINED((AE_INFO, data->pathname,
					      data->node_flags,
					      "Repaired unsorted list - now sorted by %s",
					      sort_key_name));
			return (AE_OK);
		}

		previous_value = (u32) obj_desc->integer.value;
		outer_elements++;
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_remove_null_elements
 *
 * PARAMETERS:  obj_desc            - A Package object
 *
 * RETURN:      Status. AE_NULL_ENTRY means that one or more elements were
 *              removed.
 *
 * DESCRIPTION: Remove all NULL package elements and update the package count.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_remove_null_elements(union acpi_operand_object *obj_desc)
{
	union acpi_operand_object **source;
	union acpi_operand_object **dest;
	acpi_status status = AE_OK;
	u32 count;
	u32 new_count;
	u32 i;

	count = obj_desc->package.count;
	new_count = count;

	source = obj_desc->package.elements;
	dest = source;

	/* Examine all elements of the package object */

	for (i = 0; i < count; i++) {
		if (!*source) {
			status = AE_NULL_ENTRY;
			new_count--;
		} else {
			*dest = *source;
			dest++;
		}
		source++;
	}

	if (status == AE_NULL_ENTRY) {

		/* NULL terminate list and update the package count */

		*dest = NULL;
		obj_desc->package.count = new_count;
	}

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_sort_list
 *
 * PARAMETERS:  Elements            - Package object element list
 *              Count               - Element count for above
 *              Index               - Sort by which package element
 *              sort_direction      - Ascending or Descending sort
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sort the objects that are in a package element list.
 *
 * NOTE: Assumes that all NULL elements have been removed from the package.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_sort_list(union acpi_operand_object **elements,
		  u32 count, u32 index, u8 sort_direction)
{
	union acpi_operand_object *obj_desc1;
	union acpi_operand_object *obj_desc2;
	union acpi_operand_object *temp_obj;
	u32 i;
	u32 j;

	/* Simple bubble sort */

	for (i = 1; i < count; i++) {
		for (j = (count - 1); j >= i; j--) {
			obj_desc1 = elements[j - 1]->package.elements[index];
			obj_desc2 = elements[j]->package.elements[index];

			if (((sort_direction == ACPI_SORT_ASCENDING) &&
			     (obj_desc1->integer.value >
			      obj_desc2->integer.value))
			    || ((sort_direction == ACPI_SORT_DESCENDING)
				&& (obj_desc1->integer.value <
				    obj_desc2->integer.value))) {
				temp_obj = elements[j - 1];
				elements[j - 1] = elements[j];
				elements[j] = temp_obj;
			}
		}
	}

	return (AE_OK);
}
