/******************************************************************************
 *
 * Module Name: exfield - ACPI AML (p-code) execution - field manipulation
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exfield")

/* Local prototypes */
static u32
acpi_ex_get_serial_access_length(u32 accessor_type, u32 access_length);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_get_serial_access_length
 *
 * PARAMETERS:  accessor_type   - The type of the protocol indicated by region
 *                                field access attributes
 *              access_length   - The access length of the region field
 *
 * RETURN:      Decoded access length
 *
 * DESCRIPTION: This routine returns the length of the generic_serial_bus
 *              protocol bytes
 *
 ******************************************************************************/

static u32
acpi_ex_get_serial_access_length(u32 accessor_type, u32 access_length)
{
	u32 length;

	switch (accessor_type) {
	case AML_FIELD_ATTRIB_QUICK:

		length = 0;
		break;

	case AML_FIELD_ATTRIB_SEND_RCV:
	case AML_FIELD_ATTRIB_BYTE:

		length = 1;
		break;

	case AML_FIELD_ATTRIB_WORD:
	case AML_FIELD_ATTRIB_WORD_CALL:

		length = 2;
		break;

	case AML_FIELD_ATTRIB_MULTIBYTE:
	case AML_FIELD_ATTRIB_RAW_BYTES:
	case AML_FIELD_ATTRIB_RAW_PROCESS:

		length = access_length;
		break;

	case AML_FIELD_ATTRIB_BLOCK:
	case AML_FIELD_ATTRIB_BLOCK_CALL:
	default:

		length = ACPI_GSBUS_BUFFER_SIZE - 2;
		break;
	}

	return (length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_read_data_from_field
 *
 * PARAMETERS:  walk_state          - Current execution state
 *              obj_desc            - The named field
 *              ret_buffer_desc     - Where the return data object is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field. Returns either an Integer or a
 *              Buffer, depending on the size of the field.
 *
 ******************************************************************************/

acpi_status
acpi_ex_read_data_from_field(struct acpi_walk_state * walk_state,
			     union acpi_operand_object *obj_desc,
			     union acpi_operand_object **ret_buffer_desc)
{
	acpi_status status;
	union acpi_operand_object *buffer_desc;
	acpi_size length;
	void *buffer;
	u32 function;
	u16 accessor_type;

	ACPI_FUNCTION_TRACE_PTR(ex_read_data_from_field, obj_desc);

	/* Parameter validation */

	if (!obj_desc) {
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}
	if (!ret_buffer_desc) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * If the buffer_field arguments have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_buffer_field_arguments(obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_SMBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_GSBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_IPMI)) {
		/*
		 * This is an SMBus, GSBus or IPMI read. We must create a buffer to
		 * hold the data and then directly access the region handler.
		 *
		 * Note: SMBus and GSBus protocol value is passed in upper 16-bits
		 * of Function
		 */
		if (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_SMBUS) {
			length = ACPI_SMBUS_BUFFER_SIZE;
			function =
			    ACPI_READ | (obj_desc->field.attribute << 16);
		} else if (obj_desc->field.region_obj->region.space_id ==
			   ACPI_ADR_SPACE_GSBUS) {
			accessor_type = obj_desc->field.attribute;
			length =
			    acpi_ex_get_serial_access_length(accessor_type,
							     obj_desc->field.
							     access_length);

			/*
			 * Add additional 2 bytes for the generic_serial_bus data buffer:
			 *
			 *     Status;    (Byte 0 of the data buffer)
			 *     Length;    (Byte 1 of the data buffer)
			 *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
			 */
			length += 2;
			function = ACPI_READ | (accessor_type << 16);
		} else {	/* IPMI */

			length = ACPI_IPMI_BUFFER_SIZE;
			function = ACPI_READ;
		}

		buffer_desc = acpi_ut_create_buffer_object(length);
		if (!buffer_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Lock entire transaction if requested */

		acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

		/* Call the region handler for the read */

		status = acpi_ex_access_region(obj_desc, 0,
					       ACPI_CAST_PTR(u64,
							     buffer_desc->
							     buffer.pointer),
					       function);

		acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
		goto exit;
	}

	/*
	 * Allocate a buffer for the contents of the field.
	 *
	 * If the field is larger than the current integer width, create
	 * a BUFFER to hold it. Otherwise, use an INTEGER. This allows
	 * the use of arithmetic operators on the returned value if the
	 * field size is equal or smaller than an Integer.
	 *
	 * Note: Field.length is in bits.
	 */
	length =
	    (acpi_size) ACPI_ROUND_BITS_UP_TO_BYTES(obj_desc->field.bit_length);

	if (length > acpi_gbl_integer_byte_width) {

		/* Field is too large for an Integer, create a Buffer instead */

		buffer_desc = acpi_ut_create_buffer_object(length);
		if (!buffer_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}
		buffer = buffer_desc->buffer.pointer;
	} else {
		/* Field will fit within an Integer (normal case) */

		buffer_desc = acpi_ut_create_integer_object((u64) 0);
		if (!buffer_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		length = acpi_gbl_integer_byte_width;
		buffer = &buffer_desc->integer.value;
	}

	if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
	    (obj_desc->field.region_obj->region.space_id ==
	     ACPI_ADR_SPACE_GPIO)) {
		/*
		 * For GPIO (general_purpose_io), the Address will be the bit offset
		 * from the previous Connection() operator, making it effectively a
		 * pin number index. The bit_length is the length of the field, which
		 * is thus the number of pins.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "GPIO FieldRead [FROM]:  Pin %u Bits %u\n",
				  obj_desc->field.pin_number_index,
				  obj_desc->field.bit_length));

		/* Lock entire transaction if requested */

		acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

		/* Perform the write */

		status =
		    acpi_ex_access_region(obj_desc, 0, (u64 *)buffer,
					  ACPI_READ);

		acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
		if (ACPI_FAILURE(status)) {
			acpi_ut_remove_reference(buffer_desc);
		} else {
			*ret_buffer_desc = buffer_desc;
		}
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldRead [TO]:   Obj %p, Type %X, Buf %p, ByteLen %X\n",
			  obj_desc, obj_desc->common.type, buffer,
			  (u32) length));
	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldRead [FROM]: BitLen %X, BitOff %X, ByteOff %X\n",
			  obj_desc->common_field.bit_length,
			  obj_desc->common_field.start_field_bit_offset,
			  obj_desc->common_field.base_byte_offset));

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/* Read from the field */

	status = acpi_ex_extract_from_field(obj_desc, buffer, (u32) length);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);

exit:
	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(buffer_desc);
	} else {
		*ret_buffer_desc = buffer_desc;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_write_data_to_field
 *
 * PARAMETERS:  source_desc         - Contains data to write
 *              obj_desc            - The named field
 *              result_desc         - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_data_to_field(union acpi_operand_object *source_desc,
			    union acpi_operand_object *obj_desc,
			    union acpi_operand_object **result_desc)
{
	acpi_status status;
	u32 length;
	void *buffer;
	union acpi_operand_object *buffer_desc;
	u32 function;
	u16 accessor_type;

	ACPI_FUNCTION_TRACE_PTR(ex_write_data_to_field, obj_desc);

	/* Parameter validation */

	if (!source_desc || !obj_desc) {
		return_ACPI_STATUS(AE_AML_NO_OPERAND);
	}

	if (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * If the buffer_field arguments have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_buffer_field_arguments(obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_SMBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_GSBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_IPMI)) {
		/*
		 * This is an SMBus, GSBus or IPMI write. We will bypass the entire
		 * field mechanism and handoff the buffer directly to the handler.
		 * For these address spaces, the buffer is bi-directional; on a
		 * write, return data is returned in the same buffer.
		 *
		 * Source must be a buffer of sufficient size:
		 * ACPI_SMBUS_BUFFER_SIZE, ACPI_GSBUS_BUFFER_SIZE, or
		 * ACPI_IPMI_BUFFER_SIZE.
		 *
		 * Note: SMBus and GSBus protocol type is passed in upper 16-bits
		 * of Function
		 */
		if (source_desc->common.type != ACPI_TYPE_BUFFER) {
			ACPI_ERROR((AE_INFO,
				    "SMBus/IPMI/GenericSerialBus write requires "
				    "Buffer, found type %s",
				    acpi_ut_get_object_type_name(source_desc)));

			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		if (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_SMBUS) {
			length = ACPI_SMBUS_BUFFER_SIZE;
			function =
			    ACPI_WRITE | (obj_desc->field.attribute << 16);
		} else if (obj_desc->field.region_obj->region.space_id ==
			   ACPI_ADR_SPACE_GSBUS) {
			accessor_type = obj_desc->field.attribute;
			length =
			    acpi_ex_get_serial_access_length(accessor_type,
							     obj_desc->field.
							     access_length);

			/*
			 * Add additional 2 bytes for the generic_serial_bus data buffer:
			 *
			 *     Status;    (Byte 0 of the data buffer)
			 *     Length;    (Byte 1 of the data buffer)
			 *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
			 */
			length += 2;
			function = ACPI_WRITE | (accessor_type << 16);
		} else {	/* IPMI */

			length = ACPI_IPMI_BUFFER_SIZE;
			function = ACPI_WRITE;
		}

		if (source_desc->buffer.length < length) {
			ACPI_ERROR((AE_INFO,
				    "SMBus/IPMI/GenericSerialBus write requires "
				    "Buffer of length %u, found length %u",
				    length, source_desc->buffer.length));

			return_ACPI_STATUS(AE_AML_BUFFER_LIMIT);
		}

		/* Create the bi-directional buffer */

		buffer_desc = acpi_ut_create_buffer_object(length);
		if (!buffer_desc) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		buffer = buffer_desc->buffer.pointer;
		memcpy(buffer, source_desc->buffer.pointer, length);

		/* Lock entire transaction if requested */

		acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

		/*
		 * Perform the write (returns status and perhaps data in the
		 * same buffer)
		 */
		status =
		    acpi_ex_access_region(obj_desc, 0, (u64 *)buffer, function);
		acpi_ex_release_global_lock(obj_desc->common_field.field_flags);

		*result_desc = buffer_desc;
		return_ACPI_STATUS(status);
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_GPIO)) {
		/*
		 * For GPIO (general_purpose_io), we will bypass the entire field
		 * mechanism and handoff the bit address and bit width directly to
		 * the handler. The Address will be the bit offset
		 * from the previous Connection() operator, making it effectively a
		 * pin number index. The bit_length is the length of the field, which
		 * is thus the number of pins.
		 */
		if (source_desc->common.type != ACPI_TYPE_INTEGER) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "GPIO FieldWrite [FROM]: (%s:%X), Val %.8X  [TO]: Pin %u Bits %u\n",
				  acpi_ut_get_type_name(source_desc->common.
							type),
				  source_desc->common.type,
				  (u32)source_desc->integer.value,
				  obj_desc->field.pin_number_index,
				  obj_desc->field.bit_length));

		buffer = &source_desc->integer.value;

		/* Lock entire transaction if requested */

		acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

		/* Perform the write */

		status =
		    acpi_ex_access_region(obj_desc, 0, (u64 *)buffer,
					  ACPI_WRITE);
		acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
		return_ACPI_STATUS(status);
	}

	/* Get a pointer to the data to be written */

	switch (source_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		buffer = &source_desc->integer.value;
		length = sizeof(source_desc->integer.value);
		break;

	case ACPI_TYPE_BUFFER:

		buffer = source_desc->buffer.pointer;
		length = source_desc->buffer.length;
		break;

	case ACPI_TYPE_STRING:

		buffer = source_desc->string.pointer;
		length = source_desc->string.length;
		break;

	default:

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldWrite [FROM]: Obj %p (%s:%X), Buf %p, ByteLen %X\n",
			  source_desc,
			  acpi_ut_get_type_name(source_desc->common.type),
			  source_desc->common.type, buffer, length));

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldWrite [TO]:   Obj %p (%s:%X), BitLen %X, BitOff %X, ByteOff %X\n",
			  obj_desc,
			  acpi_ut_get_type_name(obj_desc->common.type),
			  obj_desc->common.type,
			  obj_desc->common_field.bit_length,
			  obj_desc->common_field.start_field_bit_offset,
			  obj_desc->common_field.base_byte_offset));

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/* Write to the field */

	status = acpi_ex_insert_into_field(obj_desc, buffer, length);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);

	return_ACPI_STATUS(status);
}
