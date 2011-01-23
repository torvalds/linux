
/******************************************************************************
 *
 * Module Name: exprep - ACPI AML (p-code) execution - field prep utilities
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2011, Intel Corp.
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exprep")

/* Local prototypes */
static u32
acpi_ex_decode_field_access(union acpi_operand_object *obj_desc,
			    u8 field_flags, u32 * return_byte_alignment);

#ifdef ACPI_UNDER_DEVELOPMENT

static u32
acpi_ex_generate_access(u32 field_bit_offset,
			u32 field_bit_length, u32 region_length);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_generate_access
 *
 * PARAMETERS:  field_bit_offset    - Start of field within parent region/buffer
 *              field_bit_length    - Length of field in bits
 *              region_length       - Length of parent in bytes
 *
 * RETURN:      Field granularity (8, 16, 32 or 64) and
 *              byte_alignment (1, 2, 3, or 4)
 *
 * DESCRIPTION: Generate an optimal access width for fields defined with the
 *              any_acc keyword.
 *
 * NOTE: Need to have the region_length in order to check for boundary
 *       conditions (end-of-region).  However, the region_length is a deferred
 *       operation.  Therefore, to complete this implementation, the generation
 *       of this access width must be deferred until the region length has
 *       been evaluated.
 *
 ******************************************************************************/

static u32
acpi_ex_generate_access(u32 field_bit_offset,
			u32 field_bit_length, u32 region_length)
{
	u32 field_byte_length;
	u32 field_byte_offset;
	u32 field_byte_end_offset;
	u32 access_byte_width;
	u32 field_start_offset;
	u32 field_end_offset;
	u32 minimum_access_width = 0xFFFFFFFF;
	u32 minimum_accesses = 0xFFFFFFFF;
	u32 accesses;

	ACPI_FUNCTION_TRACE(ex_generate_access);

	/* Round Field start offset and length to "minimal" byte boundaries */

	field_byte_offset = ACPI_DIV_8(ACPI_ROUND_DOWN(field_bit_offset, 8));
	field_byte_end_offset = ACPI_DIV_8(ACPI_ROUND_UP(field_bit_length +
							 field_bit_offset, 8));
	field_byte_length = field_byte_end_offset - field_byte_offset;

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "Bit length %u, Bit offset %u\n",
			  field_bit_length, field_bit_offset));

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "Byte Length %u, Byte Offset %u, End Offset %u\n",
			  field_byte_length, field_byte_offset,
			  field_byte_end_offset));

	/*
	 * Iterative search for the maximum access width that is both aligned
	 * and does not go beyond the end of the region
	 *
	 * Start at byte_acc and work upwards to qword_acc max. (1,2,4,8 bytes)
	 */
	for (access_byte_width = 1; access_byte_width <= 8;
	     access_byte_width <<= 1) {
		/*
		 * 1) Round end offset up to next access boundary and make sure that
		 *    this does not go beyond the end of the parent region.
		 * 2) When the Access width is greater than the field_byte_length, we
		 *    are done. (This does not optimize for the perfectly aligned
		 *    case yet).
		 */
		if (ACPI_ROUND_UP(field_byte_end_offset, access_byte_width) <=
		    region_length) {
			field_start_offset =
			    ACPI_ROUND_DOWN(field_byte_offset,
					    access_byte_width) /
			    access_byte_width;

			field_end_offset =
			    ACPI_ROUND_UP((field_byte_length +
					   field_byte_offset),
					  access_byte_width) /
			    access_byte_width;

			accesses = field_end_offset - field_start_offset;

			ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
					  "AccessWidth %u end is within region\n",
					  access_byte_width));

			ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
					  "Field Start %u, Field End %u -- requires %u accesses\n",
					  field_start_offset, field_end_offset,
					  accesses));

			/* Single access is optimal */

			if (accesses <= 1) {
				ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
						  "Entire field can be accessed with one operation of size %u\n",
						  access_byte_width));
				return_VALUE(access_byte_width);
			}

			/*
			 * Fits in the region, but requires more than one read/write.
			 * try the next wider access on next iteration
			 */
			if (accesses < minimum_accesses) {
				minimum_accesses = accesses;
				minimum_access_width = access_byte_width;
			}
		} else {
			ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
					  "AccessWidth %u end is NOT within region\n",
					  access_byte_width));
			if (access_byte_width == 1) {
				ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
						  "Field goes beyond end-of-region!\n"));

				/* Field does not fit in the region at all */

				return_VALUE(0);
			}

			/*
			 * This width goes beyond the end-of-region, back off to
			 * previous access
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
					  "Backing off to previous optimal access width of %u\n",
					  minimum_access_width));
			return_VALUE(minimum_access_width);
		}
	}

	/*
	 * Could not read/write field with one operation,
	 * just use max access width
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "Cannot access field in one operation, using width 8\n"));
	return_VALUE(8);
}
#endif				/* ACPI_UNDER_DEVELOPMENT */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_decode_field_access
 *
 * PARAMETERS:  obj_desc            - Field object
 *              field_flags         - Encoded fieldflags (contains access bits)
 *              return_byte_alignment - Where the byte alignment is returned
 *
 * RETURN:      Field granularity (8, 16, 32 or 64) and
 *              byte_alignment (1, 2, 3, or 4)
 *
 * DESCRIPTION: Decode the access_type bits of a field definition.
 *
 ******************************************************************************/

static u32
acpi_ex_decode_field_access(union acpi_operand_object *obj_desc,
			    u8 field_flags, u32 * return_byte_alignment)
{
	u32 access;
	u32 byte_alignment;
	u32 bit_length;

	ACPI_FUNCTION_TRACE(ex_decode_field_access);

	access = (field_flags & AML_FIELD_ACCESS_TYPE_MASK);

	switch (access) {
	case AML_FIELD_ACCESS_ANY:

#ifdef ACPI_UNDER_DEVELOPMENT
		byte_alignment =
		    acpi_ex_generate_access(obj_desc->common_field.
					    start_field_bit_offset,
					    obj_desc->common_field.bit_length,
					    0xFFFFFFFF
					    /* Temp until we pass region_length as parameter */
		    );
		bit_length = byte_alignment * 8;
#endif

		byte_alignment = 1;
		bit_length = 8;
		break;

	case AML_FIELD_ACCESS_BYTE:
	case AML_FIELD_ACCESS_BUFFER:	/* ACPI 2.0 (SMBus Buffer) */
		byte_alignment = 1;
		bit_length = 8;
		break;

	case AML_FIELD_ACCESS_WORD:
		byte_alignment = 2;
		bit_length = 16;
		break;

	case AML_FIELD_ACCESS_DWORD:
		byte_alignment = 4;
		bit_length = 32;
		break;

	case AML_FIELD_ACCESS_QWORD:	/* ACPI 2.0 */
		byte_alignment = 8;
		bit_length = 64;
		break;

	default:
		/* Invalid field access type */

		ACPI_ERROR((AE_INFO, "Unknown field access type 0x%X", access));
		return_UINT32(0);
	}

	if (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * buffer_field access can be on any byte boundary, so the
		 * byte_alignment is always 1 byte -- regardless of any byte_alignment
		 * implied by the field access type.
		 */
		byte_alignment = 1;
	}

	*return_byte_alignment = byte_alignment;
	return_UINT32(bit_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_prep_common_field_object
 *
 * PARAMETERS:  obj_desc            - The field object
 *              field_flags         - Access, lock_rule, and update_rule.
 *                                    The format of a field_flag is described
 *                                    in the ACPI specification
 *              field_attribute     - Special attributes (not used)
 *              field_bit_position  - Field start position
 *              field_bit_length    - Field length in number of bits
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the areas of the field object that are common
 *              to the various types of fields.  Note: This is very "sensitive"
 *              code because we are solving the general case for field
 *              alignment.
 *
 ******************************************************************************/

acpi_status
acpi_ex_prep_common_field_object(union acpi_operand_object *obj_desc,
				 u8 field_flags,
				 u8 field_attribute,
				 u32 field_bit_position, u32 field_bit_length)
{
	u32 access_bit_width;
	u32 byte_alignment;
	u32 nearest_byte_address;

	ACPI_FUNCTION_TRACE(ex_prep_common_field_object);

	/*
	 * Note: the structure being initialized is the
	 * ACPI_COMMON_FIELD_INFO;  No structure fields outside of the common
	 * area are initialized by this procedure.
	 */
	obj_desc->common_field.field_flags = field_flags;
	obj_desc->common_field.attribute = field_attribute;
	obj_desc->common_field.bit_length = field_bit_length;

	/*
	 * Decode the access type so we can compute offsets.  The access type gives
	 * two pieces of information - the width of each field access and the
	 * necessary byte_alignment (address granularity) of the access.
	 *
	 * For any_acc, the access_bit_width is the largest width that is both
	 * necessary and possible in an attempt to access the whole field in one
	 * I/O operation.  However, for any_acc, the byte_alignment is always one
	 * byte.
	 *
	 * For all Buffer Fields, the byte_alignment is always one byte.
	 *
	 * For all other access types (Byte, Word, Dword, Qword), the Bitwidth is
	 * the same (equivalent) as the byte_alignment.
	 */
	access_bit_width = acpi_ex_decode_field_access(obj_desc, field_flags,
						       &byte_alignment);
	if (!access_bit_width) {
		return_ACPI_STATUS(AE_AML_OPERAND_VALUE);
	}

	/* Setup width (access granularity) fields (values are: 1, 2, 4, 8) */

	obj_desc->common_field.access_byte_width = (u8)
	    ACPI_DIV_8(access_bit_width);

	/*
	 * base_byte_offset is the address of the start of the field within the
	 * region.  It is the byte address of the first *datum* (field-width data
	 * unit) of the field. (i.e., the first datum that contains at least the
	 * first *bit* of the field.)
	 *
	 * Note: byte_alignment is always either equal to the access_bit_width or 8
	 * (Byte access), and it defines the addressing granularity of the parent
	 * region or buffer.
	 */
	nearest_byte_address =
	    ACPI_ROUND_BITS_DOWN_TO_BYTES(field_bit_position);
	obj_desc->common_field.base_byte_offset = (u32)
	    ACPI_ROUND_DOWN(nearest_byte_address, byte_alignment);

	/*
	 * start_field_bit_offset is the offset of the first bit of the field within
	 * a field datum.
	 */
	obj_desc->common_field.start_field_bit_offset = (u8)
	    (field_bit_position -
	     ACPI_MUL_8(obj_desc->common_field.base_byte_offset));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_prep_field_value
 *
 * PARAMETERS:  Info    - Contains all field creation info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Construct a union acpi_operand_object of type def_field and
 *              connect it to the parent Node.
 *
 ******************************************************************************/

acpi_status acpi_ex_prep_field_value(struct acpi_create_field_info *info)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *second_desc = NULL;
	acpi_status status;
	u32 access_byte_width;
	u32 type;

	ACPI_FUNCTION_TRACE(ex_prep_field_value);

	/* Parameter validation */

	if (info->field_type != ACPI_TYPE_LOCAL_INDEX_FIELD) {
		if (!info->region_node) {
			ACPI_ERROR((AE_INFO, "Null RegionNode"));
			return_ACPI_STATUS(AE_AML_NO_OPERAND);
		}

		type = acpi_ns_get_type(info->region_node);
		if (type != ACPI_TYPE_REGION) {
			ACPI_ERROR((AE_INFO,
				    "Needed Region, found type 0x%X (%s)", type,
				    acpi_ut_get_type_name(type)));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}
	}

	/* Allocate a new field object */

	obj_desc = acpi_ut_create_internal_object(info->field_type);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Initialize areas of the object that are common to all fields */

	obj_desc->common_field.node = info->field_node;
	status = acpi_ex_prep_common_field_object(obj_desc,
						  info->field_flags,
						  info->attribute,
						  info->field_bit_position,
						  info->field_bit_length);
	if (ACPI_FAILURE(status)) {
		acpi_ut_delete_object_desc(obj_desc);
		return_ACPI_STATUS(status);
	}

	/* Initialize areas of the object that are specific to the field type */

	switch (info->field_type) {
	case ACPI_TYPE_LOCAL_REGION_FIELD:

		obj_desc->field.region_obj =
		    acpi_ns_get_attached_object(info->region_node);

		/* Allow full data read from EC address space */

		if ((obj_desc->field.region_obj->region.space_id ==
		     ACPI_ADR_SPACE_EC)
		    && (obj_desc->common_field.bit_length > 8)) {
			access_byte_width =
			    ACPI_ROUND_BITS_UP_TO_BYTES(obj_desc->common_field.
							bit_length);

			/* Maximum byte width supported is 255 */

			if (access_byte_width < 256) {
				obj_desc->common_field.access_byte_width =
				    (u8)access_byte_width;
			}
		}
		/* An additional reference for the container */

		acpi_ut_add_reference(obj_desc->field.region_obj);

		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "RegionField: BitOff %X, Off %X, Gran %X, Region %p\n",
				  obj_desc->field.start_field_bit_offset,
				  obj_desc->field.base_byte_offset,
				  obj_desc->field.access_byte_width,
				  obj_desc->field.region_obj));
		break;

	case ACPI_TYPE_LOCAL_BANK_FIELD:

		obj_desc->bank_field.value = info->bank_value;
		obj_desc->bank_field.region_obj =
		    acpi_ns_get_attached_object(info->region_node);
		obj_desc->bank_field.bank_obj =
		    acpi_ns_get_attached_object(info->register_node);

		/* An additional reference for the attached objects */

		acpi_ut_add_reference(obj_desc->bank_field.region_obj);
		acpi_ut_add_reference(obj_desc->bank_field.bank_obj);

		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "Bank Field: BitOff %X, Off %X, Gran %X, Region %p, BankReg %p\n",
				  obj_desc->bank_field.start_field_bit_offset,
				  obj_desc->bank_field.base_byte_offset,
				  obj_desc->field.access_byte_width,
				  obj_desc->bank_field.region_obj,
				  obj_desc->bank_field.bank_obj));

		/*
		 * Remember location in AML stream of the field unit
		 * opcode and operands -- since the bank_value
		 * operands must be evaluated.
		 */
		second_desc = obj_desc->common.next_object;
		second_desc->extra.aml_start =
		    ACPI_CAST_PTR(union acpi_parse_object,
				  info->data_register_node)->named.data;
		second_desc->extra.aml_length =
		    ACPI_CAST_PTR(union acpi_parse_object,
				  info->data_register_node)->named.length;

		break;

	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		/* Get the Index and Data registers */

		obj_desc->index_field.index_obj =
		    acpi_ns_get_attached_object(info->register_node);
		obj_desc->index_field.data_obj =
		    acpi_ns_get_attached_object(info->data_register_node);

		if (!obj_desc->index_field.data_obj
		    || !obj_desc->index_field.index_obj) {
			ACPI_ERROR((AE_INFO,
				    "Null Index Object during field prep"));
			acpi_ut_delete_object_desc(obj_desc);
			return_ACPI_STATUS(AE_AML_INTERNAL);
		}

		/* An additional reference for the attached objects */

		acpi_ut_add_reference(obj_desc->index_field.data_obj);
		acpi_ut_add_reference(obj_desc->index_field.index_obj);

		/*
		 * April 2006: Changed to match MS behavior
		 *
		 * The value written to the Index register is the byte offset of the
		 * target field in units of the granularity of the index_field
		 *
		 * Previously, the value was calculated as an index in terms of the
		 * width of the Data register, as below:
		 *
		 *      obj_desc->index_field.Value = (u32)
		 *          (Info->field_bit_position / ACPI_MUL_8 (
		 *              obj_desc->Field.access_byte_width));
		 *
		 * February 2006: Tried value as a byte offset:
		 *      obj_desc->index_field.Value = (u32)
		 *          ACPI_DIV_8 (Info->field_bit_position);
		 */
		obj_desc->index_field.value =
		    (u32) ACPI_ROUND_DOWN(ACPI_DIV_8(info->field_bit_position),
					  obj_desc->index_field.
					  access_byte_width);

		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "IndexField: BitOff %X, Off %X, Value %X, Gran %X, Index %p, Data %p\n",
				  obj_desc->index_field.start_field_bit_offset,
				  obj_desc->index_field.base_byte_offset,
				  obj_desc->index_field.value,
				  obj_desc->field.access_byte_width,
				  obj_desc->index_field.index_obj,
				  obj_desc->index_field.data_obj));
		break;

	default:
		/* No other types should get here */
		break;
	}

	/*
	 * Store the constructed descriptor (obj_desc) into the parent Node,
	 * preserving the current type of that named_obj.
	 */
	status = acpi_ns_attach_object(info->field_node, obj_desc,
				       acpi_ns_get_type(info->field_node));

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "Set NamedObj %p [%4.4s], ObjDesc %p\n",
			  info->field_node,
			  acpi_ut_get_node_name(info->field_node), obj_desc));

	/* Remove local reference to the object */

	acpi_ut_remove_reference(obj_desc);
	return_ACPI_STATUS(status);
}
