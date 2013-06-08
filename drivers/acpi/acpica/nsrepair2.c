/******************************************************************************
 *
 * Module Name: nsrepair2 - Repair for objects returned by specific
 *                          predefined methods
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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
acpi_status(*acpi_repair_function) (struct acpi_evaluate_info * info,
				    union acpi_operand_object
				    **return_object_ptr);

typedef struct acpi_repair_info {
	char name[ACPI_NAME_SIZE];
	acpi_repair_function repair_function;

} acpi_repair_info;

/* Local prototypes */

static const struct acpi_repair_info *acpi_ns_match_complex_repair(struct
								   acpi_namespace_node
								   *node);

static acpi_status
acpi_ns_repair_ALR(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_CID(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_CST(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_FDE(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_HID(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_PRT(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_PSS(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_repair_TSS(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr);

static acpi_status
acpi_ns_check_sorted_list(struct acpi_evaluate_info *info,
			  union acpi_operand_object *return_object,
			  u32 start_index,
			  u32 expected_count,
			  u32 sort_index,
			  u8 sort_direction, char *sort_key_name);

/* Values for sort_direction above */

#define ACPI_SORT_ASCENDING     0
#define ACPI_SORT_DESCENDING    1

static void
acpi_ns_remove_element(union acpi_operand_object *obj_desc, u32 index);

static void
acpi_ns_sort_list(union acpi_operand_object **elements,
		  u32 count, u32 index, u8 sort_direction);

/*
 * This table contains the names of the predefined methods for which we can
 * perform more complex repairs.
 *
 * As necessary:
 *
 * _ALR: Sort the list ascending by ambient_illuminance
 * _CID: Strings: uppercase all, remove any leading asterisk
 * _CST: Sort the list ascending by C state type
 * _FDE: Convert Buffer of BYTEs to a Buffer of DWORDs
 * _GTM: Convert Buffer of BYTEs to a Buffer of DWORDs
 * _HID: Strings: uppercase all, remove any leading asterisk
 * _PRT: Fix reversed source_name and source_index
 * _PSS: Sort the list descending by Power
 * _TSS: Sort the list descending by Power
 *
 * Names that must be packages, but cannot be sorted:
 *
 * _BCL: Values are tied to the Package index where they appear, and cannot
 * be moved or sorted. These index values are used for _BQC and _BCM.
 * However, we can fix the case where a buffer is returned, by converting
 * it to a Package of integers.
 */
static const struct acpi_repair_info acpi_ns_repairable_names[] = {
	{"_ALR", acpi_ns_repair_ALR},
	{"_CID", acpi_ns_repair_CID},
	{"_CST", acpi_ns_repair_CST},
	{"_FDE", acpi_ns_repair_FDE},
	{"_GTM", acpi_ns_repair_FDE},	/* _GTM has same repair as _FDE */
	{"_HID", acpi_ns_repair_HID},
	{"_PRT", acpi_ns_repair_PRT},
	{"_PSS", acpi_ns_repair_PSS},
	{"_TSS", acpi_ns_repair_TSS},
	{{0, 0, 0, 0}, NULL}	/* Table terminator */
};

#define ACPI_FDE_FIELD_COUNT        5
#define ACPI_FDE_BYTE_BUFFER_SIZE   5
#define ACPI_FDE_DWORD_BUFFER_SIZE  (ACPI_FDE_FIELD_COUNT * sizeof (u32))

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_complex_repairs
 *
 * PARAMETERS:  info                - Method execution information block
 *              node                - Namespace node for the method/object
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
acpi_ns_complex_repairs(struct acpi_evaluate_info *info,
			struct acpi_namespace_node *node,
			acpi_status validate_status,
			union acpi_operand_object **return_object_ptr)
{
	const struct acpi_repair_info *predefined;
	acpi_status status;

	/* Check if this name is in the list of repairable names */

	predefined = acpi_ns_match_complex_repair(node);
	if (!predefined) {
		return (validate_status);
	}

	status = predefined->repair_function(info, return_object_ptr);
	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_match_complex_repair
 *
 * PARAMETERS:  node                - Namespace node for the method/object
 *
 * RETURN:      Pointer to entry in repair table. NULL indicates not found.
 *
 * DESCRIPTION: Check an object name against the repairable object list.
 *
 *****************************************************************************/

static const struct acpi_repair_info *acpi_ns_match_complex_repair(struct
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
 * PARAMETERS:  info                - Method execution information block
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
acpi_ns_repair_ALR(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status;

	status = acpi_ns_check_sorted_list(info, return_object, 0, 2, 1,
					   ACPI_SORT_ASCENDING,
					   "AmbientIlluminance");

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_FDE
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _FDE and _GTM objects. The expected return
 *              value is a Buffer of 5 DWORDs. This function repairs a common
 *              problem where the return value is a Buffer of BYTEs, not
 *              DWORDs.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_FDE(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object *buffer_object;
	u8 *byte_buffer;
	u32 *dword_buffer;
	u32 i;

	ACPI_FUNCTION_NAME(ns_repair_FDE);

	switch (return_object->common.type) {
	case ACPI_TYPE_BUFFER:

		/* This is the expected type. Length should be (at least) 5 DWORDs */

		if (return_object->buffer.length >= ACPI_FDE_DWORD_BUFFER_SIZE) {
			return (AE_OK);
		}

		/* We can only repair if we have exactly 5 BYTEs */

		if (return_object->buffer.length != ACPI_FDE_BYTE_BUFFER_SIZE) {
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      info->node_flags,
					      "Incorrect return buffer length %u, expected %u",
					      return_object->buffer.length,
					      ACPI_FDE_DWORD_BUFFER_SIZE));

			return (AE_AML_OPERAND_TYPE);
		}

		/* Create the new (larger) buffer object */

		buffer_object =
		    acpi_ut_create_buffer_object(ACPI_FDE_DWORD_BUFFER_SIZE);
		if (!buffer_object) {
			return (AE_NO_MEMORY);
		}

		/* Expand each byte to a DWORD */

		byte_buffer = return_object->buffer.pointer;
		dword_buffer =
		    ACPI_CAST_PTR(u32, buffer_object->buffer.pointer);

		for (i = 0; i < ACPI_FDE_FIELD_COUNT; i++) {
			*dword_buffer = (u32) *byte_buffer;
			dword_buffer++;
			byte_buffer++;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s Expanded Byte Buffer to expected DWord Buffer\n",
				  info->full_pathname));
		break;

	default:

		return (AE_AML_OPERAND_TYPE);
	}

	/* Delete the original return object, return the new buffer object */

	acpi_ut_remove_reference(return_object);
	*return_object_ptr = buffer_object;

	info->return_flags |= ACPI_OBJECT_REPAIRED;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_CID
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _CID object. If a string, ensure that all
 *              letters are uppercase and that there is no leading asterisk.
 *              If a Package, ensure same for all string elements.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_CID(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	acpi_status status;
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object **element_ptr;
	union acpi_operand_object *original_element;
	u16 original_ref_count;
	u32 i;

	/* Check for _CID as a simple string */

	if (return_object->common.type == ACPI_TYPE_STRING) {
		status = acpi_ns_repair_HID(info, return_object_ptr);
		return (status);
	}

	/* Exit if not a Package */

	if (return_object->common.type != ACPI_TYPE_PACKAGE) {
		return (AE_OK);
	}

	/* Examine each element of the _CID package */

	element_ptr = return_object->package.elements;
	for (i = 0; i < return_object->package.count; i++) {
		original_element = *element_ptr;
		original_ref_count = original_element->common.reference_count;

		status = acpi_ns_repair_HID(info, element_ptr);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Take care with reference counts */

		if (original_element != *element_ptr) {

			/* Element was replaced */

			(*element_ptr)->common.reference_count =
			    original_ref_count;

			acpi_ut_remove_reference(original_element);
		}

		element_ptr++;
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_CST
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _CST object:
 *              1. Sort the list ascending by C state type
 *              2. Ensure type cannot be zero
 *              3. A sub-package count of zero means _CST is meaningless
 *              4. Count must match the number of C state sub-packages
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_CST(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object **outer_elements;
	u32 outer_element_count;
	union acpi_operand_object *obj_desc;
	acpi_status status;
	u8 removing;
	u32 i;

	ACPI_FUNCTION_NAME(ns_repair_CST);

	/*
	 * Entries (subpackages) in the _CST Package must be sorted by the
	 * C-state type, in ascending order.
	 */
	status = acpi_ns_check_sorted_list(info, return_object, 1, 4, 1,
					   ACPI_SORT_ASCENDING, "C-State Type");
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/*
	 * We now know the list is correctly sorted by C-state type. Check if
	 * the C-state type values are proportional.
	 */
	outer_element_count = return_object->package.count - 1;
	i = 0;
	while (i < outer_element_count) {
		outer_elements = &return_object->package.elements[i + 1];
		removing = FALSE;

		if ((*outer_elements)->package.count == 0) {
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      info->node_flags,
					      "SubPackage[%u] - removing entry due to zero count",
					      i));
			removing = TRUE;
			goto remove_element;
		}

		obj_desc = (*outer_elements)->package.elements[1];	/* Index1 = Type */
		if ((u32)obj_desc->integer.value == 0) {
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      info->node_flags,
					      "SubPackage[%u] - removing entry due to invalid Type(0)",
					      i));
			removing = TRUE;
		}

	      remove_element:
		if (removing) {
			acpi_ns_remove_element(return_object, i + 1);
			outer_element_count--;
		} else {
			i++;
		}
	}

	/* Update top-level package count, Type "Integer" checked elsewhere */

	obj_desc = return_object->package.elements[0];
	obj_desc->integer.value = outer_element_count;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_HID
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _HID object. If a string, ensure that all
 *              letters are uppercase and that there is no leading asterisk.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_HID(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	union acpi_operand_object *new_string;
	char *source;
	char *dest;

	ACPI_FUNCTION_NAME(ns_repair_HID);

	/* We only care about string _HID objects (not integers) */

	if (return_object->common.type != ACPI_TYPE_STRING) {
		return (AE_OK);
	}

	if (return_object->string.length == 0) {
		ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
				      info->node_flags,
				      "Invalid zero-length _HID or _CID string"));

		/* Return AE_OK anyway, let driver handle it */

		info->return_flags |= ACPI_OBJECT_REPAIRED;
		return (AE_OK);
	}

	/* It is simplest to always create a new string object */

	new_string = acpi_ut_create_string_object(return_object->string.length);
	if (!new_string) {
		return (AE_NO_MEMORY);
	}

	/*
	 * Remove a leading asterisk if present. For some unknown reason, there
	 * are many machines in the field that contains IDs like this.
	 *
	 * Examples: "*PNP0C03", "*ACPI0003"
	 */
	source = return_object->string.pointer;
	if (*source == '*') {
		source++;
		new_string->string.length--;

		ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
				  "%s: Removed invalid leading asterisk\n",
				  info->full_pathname));
	}

	/*
	 * Copy and uppercase the string. From the ACPI 5.0 specification:
	 *
	 * A valid PNP ID must be of the form "AAA####" where A is an uppercase
	 * letter and # is a hex digit. A valid ACPI ID must be of the form
	 * "NNNN####" where N is an uppercase letter or decimal digit, and
	 * # is a hex digit.
	 */
	for (dest = new_string->string.pointer; *source; dest++, source++) {
		*dest = (char)ACPI_TOUPPER(*source);
	}

	acpi_ut_remove_reference(return_object);
	*return_object_ptr = new_string;
	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_PRT
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object_ptr   - Pointer to the object returned from the
 *                                    evaluation of a method or object
 *
 * RETURN:      Status. AE_OK if object is OK or was repaired successfully
 *
 * DESCRIPTION: Repair for the _PRT object. If necessary, fix reversed
 *              source_name and source_index field, a common BIOS bug.
 *
 *****************************************************************************/

static acpi_status
acpi_ns_repair_PRT(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *package_object = *return_object_ptr;
	union acpi_operand_object **top_object_list;
	union acpi_operand_object **sub_object_list;
	union acpi_operand_object *obj_desc;
	u32 element_count;
	u32 index;

	/* Each element in the _PRT package is a subpackage */

	top_object_list = package_object->package.elements;
	element_count = package_object->package.count;

	for (index = 0; index < element_count; index++) {
		sub_object_list = (*top_object_list)->package.elements;

		/*
		 * If the BIOS has erroneously reversed the _PRT source_name (index 2)
		 * and the source_index (index 3), fix it. _PRT is important enough to
		 * workaround this BIOS error. This also provides compatibility with
		 * other ACPI implementations.
		 */
		obj_desc = sub_object_list[3];
		if (!obj_desc || (obj_desc->common.type != ACPI_TYPE_INTEGER)) {
			sub_object_list[3] = sub_object_list[2];
			sub_object_list[2] = obj_desc;
			info->return_flags |= ACPI_OBJECT_REPAIRED;

			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      info->node_flags,
					      "PRT[%X]: Fixed reversed SourceName and SourceIndex",
					      index));
		}

		/* Point to the next union acpi_operand_object in the top level package */

		top_object_list++;
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_repair_PSS
 *
 * PARAMETERS:  info                - Method execution information block
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
acpi_ns_repair_PSS(struct acpi_evaluate_info *info,
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
	status = acpi_ns_check_sorted_list(info, return_object, 0, 6, 0,
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
			ACPI_WARN_PREDEFINED((AE_INFO, info->full_pathname,
					      info->node_flags,
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
 * FUNCTION:    acpi_ns_repair_TSS
 *
 * PARAMETERS:  info                - Method execution information block
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
acpi_ns_repair_TSS(struct acpi_evaluate_info *info,
		   union acpi_operand_object **return_object_ptr)
{
	union acpi_operand_object *return_object = *return_object_ptr;
	acpi_status status;
	struct acpi_namespace_node *node;

	/*
	 * We can only sort the _TSS return package if there is no _PSS in the
	 * same scope. This is because if _PSS is present, the ACPI specification
	 * dictates that the _TSS Power Dissipation field is to be ignored, and
	 * therefore some BIOSs leave garbage values in the _TSS Power field(s).
	 * In this case, it is best to just return the _TSS package as-is.
	 * (May, 2011)
	 */
	status = acpi_ns_get_node(info->node, "^_PSS",
				  ACPI_NS_NO_UPSEARCH, &node);
	if (ACPI_SUCCESS(status)) {
		return (AE_OK);
	}

	status = acpi_ns_check_sorted_list(info, return_object, 0, 5, 1,
					   ACPI_SORT_DESCENDING,
					   "PowerDissipation");

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_check_sorted_list
 *
 * PARAMETERS:  info                - Method execution information block
 *              return_object       - Pointer to the top-level returned object
 *              start_index         - Index of the first sub-package
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
acpi_ns_check_sorted_list(struct acpi_evaluate_info *info,
			  union acpi_operand_object *return_object,
			  u32 start_index,
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

	ACPI_FUNCTION_NAME(ns_check_sorted_list);

	/* The top-level object must be a package */

	if (return_object->common.type != ACPI_TYPE_PACKAGE) {
		return (AE_AML_OPERAND_TYPE);
	}

	/*
	 * NOTE: assumes list of sub-packages contains no NULL elements.
	 * Any NULL elements should have been removed by earlier call
	 * to acpi_ns_remove_null_elements.
	 */
	outer_element_count = return_object->package.count;
	if (!outer_element_count || start_index >= outer_element_count) {
		return (AE_AML_PACKAGE_LIMIT);
	}

	outer_elements = &return_object->package.elements[start_index];
	outer_element_count -= start_index;

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
		 * discrepancy, sort the entire list.
		 */
		if (((sort_direction == ACPI_SORT_ASCENDING) &&
		     (obj_desc->integer.value < previous_value)) ||
		    ((sort_direction == ACPI_SORT_DESCENDING) &&
		     (obj_desc->integer.value > previous_value))) {
			acpi_ns_sort_list(&return_object->package.
					  elements[start_index],
					  outer_element_count, sort_index,
					  sort_direction);

			info->return_flags |= ACPI_OBJECT_REPAIRED;

			ACPI_DEBUG_PRINT((ACPI_DB_REPAIR,
					  "%s: Repaired unsorted list - now sorted by %s\n",
					  info->full_pathname, sort_key_name));
			return (AE_OK);
		}

		previous_value = (u32) obj_desc->integer.value;
		outer_elements++;
	}

	return (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_sort_list
 *
 * PARAMETERS:  elements            - Package object element list
 *              count               - Element count for above
 *              index               - Sort by which package element
 *              sort_direction      - Ascending or Descending sort
 *
 * RETURN:      None
 *
 * DESCRIPTION: Sort the objects that are in a package element list.
 *
 * NOTE: Assumes that all NULL elements have been removed from the package,
 *       and that all elements have been verified to be of type Integer.
 *
 *****************************************************************************/

static void
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
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ns_remove_element
 *
 * PARAMETERS:  obj_desc            - Package object element list
 *              index               - Index of element to remove
 *
 * RETURN:      None
 *
 * DESCRIPTION: Remove the requested element of a package and delete it.
 *
 *****************************************************************************/

static void
acpi_ns_remove_element(union acpi_operand_object *obj_desc, u32 index)
{
	union acpi_operand_object **source;
	union acpi_operand_object **dest;
	u32 count;
	u32 new_count;
	u32 i;

	ACPI_FUNCTION_NAME(ns_remove_element);

	count = obj_desc->package.count;
	new_count = count - 1;

	source = obj_desc->package.elements;
	dest = source;

	/* Examine all elements of the package object, remove matched index */

	for (i = 0; i < count; i++) {
		if (i == index) {
			acpi_ut_remove_reference(*source);	/* Remove one ref for being in pkg */
			acpi_ut_remove_reference(*source);
		} else {
			*dest = *source;
			dest++;
		}
		source++;
	}

	/* NULL terminate list and update the package count */

	*dest = NULL;
	obj_desc->package.count = new_count;
}
