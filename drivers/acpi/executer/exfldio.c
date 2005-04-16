/******************************************************************************
 *
 * Module Name: exfldio - Aml Field I/O
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acevents.h>
#include <acpi/acdispat.h>


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exfldio")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_setup_region
 *
 * PARAMETERS:  *obj_desc               - Field to be read or written
 *              field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common processing for acpi_ex_extract_from_field and
 *              acpi_ex_insert_into_field. Initialize the Region if necessary and
 *              validate the request.
 *
 ******************************************************************************/

acpi_status
acpi_ex_setup_region (
	union acpi_operand_object       *obj_desc,
	u32                             field_datum_byte_offset)
{
	acpi_status                     status = AE_OK;
	union acpi_operand_object       *rgn_desc;


	ACPI_FUNCTION_TRACE_U32 ("ex_setup_region", field_datum_byte_offset);


	rgn_desc = obj_desc->common_field.region_obj;

	/* We must have a valid region */

	if (ACPI_GET_OBJECT_TYPE (rgn_desc) != ACPI_TYPE_REGION) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Needed Region, found type %X (%s)\n",
			ACPI_GET_OBJECT_TYPE (rgn_desc),
			acpi_ut_get_object_type_name (rgn_desc)));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	/*
	 * If the Region Address and Length have not been previously evaluated,
	 * evaluate them now and save the results.
	 */
	if (!(rgn_desc->common.flags & AOPOBJ_DATA_VALID)) {
		status = acpi_ds_get_region_arguments (rgn_desc);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	if (rgn_desc->region.space_id == ACPI_ADR_SPACE_SMBUS) {
		/* SMBus has a non-linear address space */

		return_ACPI_STATUS (AE_OK);
	}

#ifdef ACPI_UNDER_DEVELOPMENT
	/*
	 * If the Field access is any_acc, we can now compute the optimal
	 * access (because we know know the length of the parent region)
	 */
	if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}
#endif

	/*
	 * Validate the request.  The entire request from the byte offset for a
	 * length of one field datum (access width) must fit within the region.
	 * (Region length is specified in bytes)
	 */
	if (rgn_desc->region.length < (obj_desc->common_field.base_byte_offset
			   + field_datum_byte_offset
			   + obj_desc->common_field.access_byte_width)) {
		if (acpi_gbl_enable_interpreter_slack) {
			/*
			 * Slack mode only:  We will go ahead and allow access to this
			 * field if it is within the region length rounded up to the next
			 * access width boundary.
			 */
			if (ACPI_ROUND_UP (rgn_desc->region.length,
					   obj_desc->common_field.access_byte_width) >=
				(obj_desc->common_field.base_byte_offset +
				 (acpi_native_uint) obj_desc->common_field.access_byte_width +
				 field_datum_byte_offset)) {
				return_ACPI_STATUS (AE_OK);
			}
		}

		if (rgn_desc->region.length < obj_desc->common_field.access_byte_width) {
			/*
			 * This is the case where the access_type (acc_word, etc.) is wider
			 * than the region itself.  For example, a region of length one
			 * byte, and a field with Dword access specified.
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Field [%4.4s] access width (%d bytes) too large for region [%4.4s] (length %X)\n",
				acpi_ut_get_node_name (obj_desc->common_field.node),
				obj_desc->common_field.access_byte_width,
				acpi_ut_get_node_name (rgn_desc->region.node), rgn_desc->region.length));
		}

		/*
		 * Offset rounded up to next multiple of field width
		 * exceeds region length, indicate an error
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Field [%4.4s] Base+Offset+Width %X+%X+%X is beyond end of region [%4.4s] (length %X)\n",
			acpi_ut_get_node_name (obj_desc->common_field.node),
			obj_desc->common_field.base_byte_offset,
			field_datum_byte_offset, obj_desc->common_field.access_byte_width,
			acpi_ut_get_node_name (rgn_desc->region.node), rgn_desc->region.length));

		return_ACPI_STATUS (AE_AML_REGION_LIMIT);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_access_region
 *
 * PARAMETERS:  *obj_desc               - Field to be read
 *              field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *              *Value                  - Where to store value (must at least
 *                                        the size of acpi_integer)
 *              Function                - Read or Write flag plus other region-
 *                                        dependent flags
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single field datum to an Operation Region.
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_region (
	union acpi_operand_object       *obj_desc,
	u32                             field_datum_byte_offset,
	acpi_integer                    *value,
	u32                             function)
{
	acpi_status                     status;
	union acpi_operand_object       *rgn_desc;
	acpi_physical_address           address;


	ACPI_FUNCTION_TRACE ("ex_access_region");


	/*
	 * Ensure that the region operands are fully evaluated and verify
	 * the validity of the request
	 */
	status = acpi_ex_setup_region (obj_desc, field_datum_byte_offset);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * The physical address of this field datum is:
	 *
	 * 1) The base of the region, plus
	 * 2) The base offset of the field, plus
	 * 3) The current offset into the field
	 */
	rgn_desc = obj_desc->common_field.region_obj;
	address = rgn_desc->region.address
			 + obj_desc->common_field.base_byte_offset
			 + field_datum_byte_offset;

	if ((function & ACPI_IO_MASK) == ACPI_READ) {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[READ]"));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[WRITE]"));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_BFIELD,
		" Region [%s:%X], Width %X, byte_base %X, Offset %X at %8.8X%8.8X\n",
		acpi_ut_get_region_name (rgn_desc->region.space_id),
		rgn_desc->region.space_id,
		obj_desc->common_field.access_byte_width,
		obj_desc->common_field.base_byte_offset,
		field_datum_byte_offset,
		ACPI_FORMAT_UINT64 (address)));

	/* Invoke the appropriate address_space/op_region handler */

	status = acpi_ev_address_space_dispatch (rgn_desc, function,
			  address, ACPI_MUL_8 (obj_desc->common_field.access_byte_width), value);

	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_IMPLEMENTED) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Region %s(%X) not implemented\n",
				acpi_ut_get_region_name (rgn_desc->region.space_id),
				rgn_desc->region.space_id));
		}
		else if (status == AE_NOT_EXIST) {
			ACPI_REPORT_ERROR ((
				"Region %s(%X) has no handler\n",
				acpi_ut_get_region_name (rgn_desc->region.space_id),
				rgn_desc->region.space_id));
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_register_overflow
 *
 * PARAMETERS:  *obj_desc               - Register(Field) to be written
 *              Value                   - Value to be stored
 *
 * RETURN:      TRUE if value overflows the field, FALSE otherwise
 *
 * DESCRIPTION: Check if a value is out of range of the field being written.
 *              Used to check if the values written to Index and Bank registers
 *              are out of range.  Normally, the value is simply truncated
 *              to fit the field, but this case is most likely a serious
 *              coding error in the ASL.
 *
 ******************************************************************************/

u8
acpi_ex_register_overflow (
	union acpi_operand_object       *obj_desc,
	acpi_integer                    value)
{

	if (obj_desc->common_field.bit_length >= ACPI_INTEGER_BIT_SIZE) {
		/*
		 * The field is large enough to hold the maximum integer, so we can
		 * never overflow it.
		 */
		return (FALSE);
	}

	if (value >= ((acpi_integer) 1 << obj_desc->common_field.bit_length)) {
		/*
		 * The Value is larger than the maximum value that can fit into
		 * the register.
		 */
		return (TRUE);
	}

	/* The Value will fit into the field with no truncation */

	return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_field_datum_io
 *
 * PARAMETERS:  *obj_desc               - Field to be read
 *              field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *              *Value                  - Where to store value (must be 64 bits)
 *              read_write              - Read or Write flag
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single datum of a field.  The field_type is
 *              demultiplexed here to handle the different types of fields
 *              (buffer_field, region_field, index_field, bank_field)
 *
 ******************************************************************************/

acpi_status
acpi_ex_field_datum_io (
	union acpi_operand_object       *obj_desc,
	u32                             field_datum_byte_offset,
	acpi_integer                    *value,
	u32                             read_write)
{
	acpi_status                     status;
	acpi_integer                    local_value;


	ACPI_FUNCTION_TRACE_U32 ("ex_field_datum_io", field_datum_byte_offset);


	if (read_write == ACPI_READ) {
		if (!value) {
			local_value = 0;
			value = &local_value; /* To support reads without saving return value */
		}

		/* Clear the entire return buffer first, [Very Important!] */

		*value = 0;
	}

	/*
	 * The four types of fields are:
	 *
	 * buffer_field - Read/write from/to a Buffer
	 * region_field - Read/write from/to a Operation Region.
	 * bank_field  - Write to a Bank Register, then read/write from/to an op_region
	 * index_field - Write to an Index Register, then read/write from/to a Data Register
	 */
	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_BUFFER_FIELD:
		/*
		 * If the buffer_field arguments have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_buffer_field_arguments (obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}

		if (read_write == ACPI_READ) {
			/*
			 * Copy the data from the source buffer.
			 * Length is the field width in bytes.
			 */
			ACPI_MEMCPY (value, (obj_desc->buffer_field.buffer_obj)->buffer.pointer
					  + obj_desc->buffer_field.base_byte_offset
					  + field_datum_byte_offset,
					  obj_desc->common_field.access_byte_width);
		}
		else {
			/*
			 * Copy the data to the target buffer.
			 * Length is the field width in bytes.
			 */
			ACPI_MEMCPY ((obj_desc->buffer_field.buffer_obj)->buffer.pointer
					+ obj_desc->buffer_field.base_byte_offset
					+ field_datum_byte_offset,
					value, obj_desc->common_field.access_byte_width);
		}

		status = AE_OK;
		break;


	case ACPI_TYPE_LOCAL_BANK_FIELD:

		/* Ensure that the bank_value is not beyond the capacity of the register */

		if (acpi_ex_register_overflow (obj_desc->bank_field.bank_obj,
				  (acpi_integer) obj_desc->bank_field.value)) {
			return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
		}

		/*
		 * For bank_fields, we must write the bank_value to the bank_register
		 * (itself a region_field) before we can access the data.
		 */
		status = acpi_ex_insert_into_field (obj_desc->bank_field.bank_obj,
				 &obj_desc->bank_field.value,
				 sizeof (obj_desc->bank_field.value));
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Now that the Bank has been selected, fall through to the
		 * region_field case and write the datum to the Operation Region
		 */

		/*lint -fallthrough */


	case ACPI_TYPE_LOCAL_REGION_FIELD:
		/*
		 * For simple region_fields, we just directly access the owning
		 * Operation Region.
		 */
		status = acpi_ex_access_region (obj_desc, field_datum_byte_offset, value,
				  read_write);
		break;


	case ACPI_TYPE_LOCAL_INDEX_FIELD:


		/* Ensure that the index_value is not beyond the capacity of the register */

		if (acpi_ex_register_overflow (obj_desc->index_field.index_obj,
				  (acpi_integer) obj_desc->index_field.value)) {
			return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
		}

		/* Write the index value to the index_register (itself a region_field) */

		field_datum_byte_offset += obj_desc->index_field.value;

		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
				"Write to Index Register: Value %8.8X\n",
				field_datum_byte_offset));

		status = acpi_ex_insert_into_field (obj_desc->index_field.index_obj,
				 &field_datum_byte_offset,
				 sizeof (field_datum_byte_offset));
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
				"I/O to Data Register: value_ptr %p\n",
				value));

		if (read_write == ACPI_READ) {
			/* Read the datum from the data_register */

			status = acpi_ex_extract_from_field (obj_desc->index_field.data_obj,
					  value, sizeof (acpi_integer));
		}
		else {
			/* Write the datum to the data_register */

			status = acpi_ex_insert_into_field (obj_desc->index_field.data_obj,
					  value, sizeof (acpi_integer));
		}
		break;


	default:

		ACPI_REPORT_ERROR (("Wrong object type in field I/O %X\n",
			ACPI_GET_OBJECT_TYPE (obj_desc)));
		status = AE_AML_INTERNAL;
		break;
	}

	if (ACPI_SUCCESS (status)) {
		if (read_write == ACPI_READ) {
			ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "Value Read %8.8X%8.8X, Width %d\n",
					   ACPI_FORMAT_UINT64 (*value),
					   obj_desc->common_field.access_byte_width));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "Value Written %8.8X%8.8X, Width %d\n",
					   ACPI_FORMAT_UINT64 (*value),
					   obj_desc->common_field.access_byte_width));
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_write_with_update_rule
 *
 * PARAMETERS:  *obj_desc           - Field to be set
 *              Value               - Value to store
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Apply the field update rule to a field write
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_with_update_rule (
	union acpi_operand_object       *obj_desc,
	acpi_integer                    mask,
	acpi_integer                    field_value,
	u32                             field_datum_byte_offset)
{
	acpi_status                     status = AE_OK;
	acpi_integer                    merged_value;
	acpi_integer                    current_value;


	ACPI_FUNCTION_TRACE_U32 ("ex_write_with_update_rule", mask);


	/* Start with the new bits  */

	merged_value = field_value;

	/* If the mask is all ones, we don't need to worry about the update rule */

	if (mask != ACPI_INTEGER_MAX) {
		/* Decode the update rule */

		switch (obj_desc->common_field.field_flags & AML_FIELD_UPDATE_RULE_MASK) {
		case AML_FIELD_UPDATE_PRESERVE:
			/*
			 * Check if update rule needs to be applied (not if mask is all
			 * ones)  The left shift drops the bits we want to ignore.
			 */
			if ((~mask << (ACPI_MUL_8 (sizeof (mask)) -
					 ACPI_MUL_8 (obj_desc->common_field.access_byte_width))) != 0) {
				/*
				 * Read the current contents of the byte/word/dword containing
				 * the field, and merge with the new field value.
				 */
				status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
						  &current_value, ACPI_READ);
				if (ACPI_FAILURE (status)) {
					return_ACPI_STATUS (status);
				}

				merged_value |= (current_value & ~mask);
			}
			break;

		case AML_FIELD_UPDATE_WRITE_AS_ONES:

			/* Set positions outside the field to all ones */

			merged_value |= ~mask;
			break;

		case AML_FIELD_UPDATE_WRITE_AS_ZEROS:

			/* Set positions outside the field to all zeros */

			merged_value &= mask;
			break;

		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"write_with_update_rule: Unknown update_rule setting: %X\n",
				(obj_desc->common_field.field_flags & AML_FIELD_UPDATE_RULE_MASK)));
			return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Mask %8.8X%8.8X, datum_offset %X, Width %X, Value %8.8X%8.8X, merged_value %8.8X%8.8X\n",
		ACPI_FORMAT_UINT64 (mask),
		field_datum_byte_offset,
		obj_desc->common_field.access_byte_width,
		ACPI_FORMAT_UINT64 (field_value),
		ACPI_FORMAT_UINT64 (merged_value)));

	/* Write the merged value */

	status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
			  &merged_value, ACPI_WRITE);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_extract_from_field
 *
 * PARAMETERS:  obj_desc            - Field to be read
 *              Buffer              - Where to store the field data
 *              buffer_length       - Length of Buffer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the current value of the given field
 *
 ******************************************************************************/

acpi_status
acpi_ex_extract_from_field (
	union acpi_operand_object       *obj_desc,
	void                            *buffer,
	u32                             buffer_length)
{
	acpi_status                     status;
	acpi_integer                    raw_datum;
	acpi_integer                    merged_datum;
	u32                             field_offset = 0;
	u32                             buffer_offset = 0;
	u32                             buffer_tail_bits;
	u32                             datum_count;
	u32                             field_datum_count;
	u32                             i;


	ACPI_FUNCTION_TRACE ("ex_extract_from_field");


	/* Validate target buffer and clear it */

	if (buffer_length < ACPI_ROUND_BITS_UP_TO_BYTES (
			 obj_desc->common_field.bit_length)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Field size %X (bits) is too large for buffer (%X)\n",
			obj_desc->common_field.bit_length, buffer_length));

		return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
	}
	ACPI_MEMSET (buffer, 0, buffer_length);

	/* Compute the number of datums (access width data items) */

	datum_count = ACPI_ROUND_UP_TO (
			   obj_desc->common_field.bit_length,
			   obj_desc->common_field.access_bit_width);
	field_datum_count = ACPI_ROUND_UP_TO (
			   obj_desc->common_field.bit_length +
			   obj_desc->common_field.start_field_bit_offset,
			   obj_desc->common_field.access_bit_width);

	/* Priming read from the field */

	status = acpi_ex_field_datum_io (obj_desc, field_offset, &raw_datum, ACPI_READ);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}
	merged_datum = raw_datum >> obj_desc->common_field.start_field_bit_offset;

	/* Read the rest of the field */

	for (i = 1; i < field_datum_count; i++) {
		/* Get next input datum from the field */

		field_offset += obj_desc->common_field.access_byte_width;
		status = acpi_ex_field_datum_io (obj_desc, field_offset,
				  &raw_datum, ACPI_READ);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Merge with previous datum if necessary */

		merged_datum |= raw_datum <<
			(obj_desc->common_field.access_bit_width - obj_desc->common_field.start_field_bit_offset);

		if (i == datum_count) {
			break;
		}

		/* Write merged datum to target buffer */

		ACPI_MEMCPY (((char *) buffer) + buffer_offset, &merged_datum,
			ACPI_MIN(obj_desc->common_field.access_byte_width,
					 buffer_length - buffer_offset));

		buffer_offset += obj_desc->common_field.access_byte_width;
		merged_datum = raw_datum >> obj_desc->common_field.start_field_bit_offset;
	}

	/* Mask off any extra bits in the last datum */

	buffer_tail_bits = obj_desc->common_field.bit_length % obj_desc->common_field.access_bit_width;
	if (buffer_tail_bits) {
		merged_datum &= ACPI_MASK_BITS_ABOVE (buffer_tail_bits);
	}

	/* Write the last datum to the buffer */

	ACPI_MEMCPY (((char *) buffer) + buffer_offset, &merged_datum,
		ACPI_MIN(obj_desc->common_field.access_byte_width,
				 buffer_length - buffer_offset));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_insert_into_field
 *
 * PARAMETERS:  obj_desc            - Field to be written
 *              Buffer              - Data to be written
 *              buffer_length       - Length of Buffer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the Buffer contents into the given field
 *
 ******************************************************************************/

acpi_status
acpi_ex_insert_into_field (
	union acpi_operand_object       *obj_desc,
	void                            *buffer,
	u32                             buffer_length)
{
	acpi_status                     status;
	acpi_integer                    mask;
	acpi_integer                    merged_datum;
	acpi_integer                    raw_datum = 0;
	u32                             field_offset = 0;
	u32                             buffer_offset = 0;
	u32                             buffer_tail_bits;
	u32                             datum_count;
	u32                             field_datum_count;
	u32                             i;


	ACPI_FUNCTION_TRACE ("ex_insert_into_field");


	/* Validate input buffer */

	if (buffer_length < ACPI_ROUND_BITS_UP_TO_BYTES (
			 obj_desc->common_field.bit_length)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Field size %X (bits) is too large for buffer (%X)\n",
			obj_desc->common_field.bit_length, buffer_length));

		return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
	}

	/* Compute the number of datums (access width data items) */

	mask = ACPI_MASK_BITS_BELOW (obj_desc->common_field.start_field_bit_offset);
	datum_count = ACPI_ROUND_UP_TO (obj_desc->common_field.bit_length,
			  obj_desc->common_field.access_bit_width);
	field_datum_count = ACPI_ROUND_UP_TO (obj_desc->common_field.bit_length +
			   obj_desc->common_field.start_field_bit_offset,
			   obj_desc->common_field.access_bit_width);

	/* Get initial Datum from the input buffer */

	ACPI_MEMCPY (&raw_datum, buffer,
		ACPI_MIN(obj_desc->common_field.access_byte_width,
				 buffer_length - buffer_offset));

	merged_datum = raw_datum << obj_desc->common_field.start_field_bit_offset;

	/* Write the entire field */

	for (i = 1; i < field_datum_count; i++) {
		/* Write merged datum to the target field */

		merged_datum &= mask;
		status = acpi_ex_write_with_update_rule (obj_desc, mask, merged_datum, field_offset);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Start new output datum by merging with previous input datum */

		field_offset += obj_desc->common_field.access_byte_width;
		merged_datum = raw_datum >>
			(obj_desc->common_field.access_bit_width - obj_desc->common_field.start_field_bit_offset);
		mask = ACPI_INTEGER_MAX;

		if (i == datum_count) {
			break;
		}

		/* Get the next input datum from the buffer */

		buffer_offset += obj_desc->common_field.access_byte_width;
		ACPI_MEMCPY (&raw_datum, ((char *) buffer) + buffer_offset,
			ACPI_MIN(obj_desc->common_field.access_byte_width,
					 buffer_length - buffer_offset));
		merged_datum |= raw_datum << obj_desc->common_field.start_field_bit_offset;
	}

	/* Mask off any extra bits in the last datum */

	buffer_tail_bits = (obj_desc->common_field.bit_length +
			obj_desc->common_field.start_field_bit_offset) % obj_desc->common_field.access_bit_width;
	if (buffer_tail_bits) {
		mask &= ACPI_MASK_BITS_ABOVE (buffer_tail_bits);
	}

	/* Write the last datum to the field */

	merged_datum &= mask;
	status = acpi_ex_write_with_update_rule (obj_desc, mask, merged_datum, field_offset);

	return_ACPI_STATUS (status);
}


