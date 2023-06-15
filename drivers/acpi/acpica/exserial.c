// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exserial - field_unit support for serial address spaces
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exserial")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_read_gpio
 *
 * PARAMETERS:  obj_desc            - The named field to read
 *              buffer              - Where the return data is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field that references a Generic Serial Bus
 *              field
 *
 ******************************************************************************/
acpi_status acpi_ex_read_gpio(union acpi_operand_object *obj_desc, void *buffer)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ex_read_gpio, obj_desc);

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

	/* Perform the read */

	status = acpi_ex_access_region(obj_desc, 0, (u64 *)buffer, ACPI_READ);

	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_write_gpio
 *
 * PARAMETERS:  source_desc         - Contains data to write. Expect to be
 *                                    an Integer object.
 *              obj_desc            - The named field
 *              result_desc         - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field that references a General Purpose I/O
 *              field.
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_gpio(union acpi_operand_object *source_desc,
		   union acpi_operand_object *obj_desc,
		   union acpi_operand_object **return_buffer)
{
	acpi_status status;
	void *buffer;

	ACPI_FUNCTION_TRACE_PTR(ex_write_gpio, obj_desc);

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
			  "GPIO FieldWrite [FROM]: (%s:%X), Value %.8X  [TO]: Pin %u Bits %u\n",
			  acpi_ut_get_type_name(source_desc->common.type),
			  source_desc->common.type,
			  (u32)source_desc->integer.value,
			  obj_desc->field.pin_number_index,
			  obj_desc->field.bit_length));

	buffer = &source_desc->integer.value;

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/* Perform the write */

	status = acpi_ex_access_region(obj_desc, 0, (u64 *)buffer, ACPI_WRITE);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_read_serial_bus
 *
 * PARAMETERS:  obj_desc            - The named field to read
 *              return_buffer       - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field that references a serial bus
 *              (SMBus, IPMI, or GSBus).
 *
 ******************************************************************************/

acpi_status
acpi_ex_read_serial_bus(union acpi_operand_object *obj_desc,
			union acpi_operand_object **return_buffer)
{
	acpi_status status;
	u32 buffer_length;
	union acpi_operand_object *buffer_desc;
	u32 function;
	u16 accessor_type;

	ACPI_FUNCTION_TRACE_PTR(ex_read_serial_bus, obj_desc);

	/*
	 * This is an SMBus, GSBus or IPMI read. We must create a buffer to
	 * hold the data and then directly access the region handler.
	 *
	 * Note: SMBus and GSBus protocol value is passed in upper 16-bits
	 * of Function
	 *
	 * Common buffer format:
	 *     Status;    (Byte 0 of the data buffer)
	 *     Length;    (Byte 1 of the data buffer)
	 *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
	 */
	switch (obj_desc->field.region_obj->region.space_id) {
	case ACPI_ADR_SPACE_SMBUS:

		buffer_length = ACPI_SMBUS_BUFFER_SIZE;
		function = ACPI_READ | (obj_desc->field.attribute << 16);
		break;

	case ACPI_ADR_SPACE_IPMI:

		buffer_length = ACPI_IPMI_BUFFER_SIZE;
		function = ACPI_READ;
		break;

	case ACPI_ADR_SPACE_GSBUS:

		accessor_type = obj_desc->field.attribute;
		if (accessor_type == AML_FIELD_ATTRIB_RAW_PROCESS_BYTES) {
			ACPI_ERROR((AE_INFO,
				    "Invalid direct read using bidirectional write-then-read protocol"));

			return_ACPI_STATUS(AE_AML_PROTOCOL);
		}

		status =
		    acpi_ex_get_protocol_buffer_length(accessor_type,
						       &buffer_length);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Invalid protocol ID for GSBus: 0x%4.4X",
				    accessor_type));

			return_ACPI_STATUS(status);
		}

		/* Add header length to get the full size of the buffer */

		buffer_length += ACPI_SERIAL_HEADER_SIZE;
		function = ACPI_READ | (accessor_type << 16);
		break;

	case ACPI_ADR_SPACE_PLATFORM_RT:

		buffer_length = ACPI_PRM_INPUT_BUFFER_SIZE;
		function = ACPI_READ;
		break;

	default:
		return_ACPI_STATUS(AE_AML_INVALID_SPACE_ID);
	}

	/* Create the local transfer buffer that is returned to the caller */

	buffer_desc = acpi_ut_create_buffer_object(buffer_length);
	if (!buffer_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/* Call the region handler for the write-then-read */

	status = acpi_ex_access_region(obj_desc, 0,
				       ACPI_CAST_PTR(u64,
						     buffer_desc->buffer.
						     pointer), function);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);

	*return_buffer = buffer_desc;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_write_serial_bus
 *
 * PARAMETERS:  source_desc         - Contains data to write
 *              obj_desc            - The named field
 *              return_buffer       - Where the return value is returned, if any
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field that references a serial bus
 *              (SMBus, IPMI, GSBus).
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_serial_bus(union acpi_operand_object *source_desc,
			 union acpi_operand_object *obj_desc,
			 union acpi_operand_object **return_buffer)
{
	acpi_status status;
	u32 buffer_length;
	u32 data_length;
	void *buffer;
	union acpi_operand_object *buffer_desc;
	u32 function;
	u16 accessor_type;

	ACPI_FUNCTION_TRACE_PTR(ex_write_serial_bus, obj_desc);

	/*
	 * This is an SMBus, GSBus or IPMI write. We will bypass the entire
	 * field mechanism and handoff the buffer directly to the handler.
	 * For these address spaces, the buffer is bidirectional; on a
	 * write, return data is returned in the same buffer.
	 *
	 * Source must be a buffer of sufficient size, these are fixed size:
	 * ACPI_SMBUS_BUFFER_SIZE, or ACPI_IPMI_BUFFER_SIZE.
	 *
	 * Note: SMBus and GSBus protocol type is passed in upper 16-bits
	 * of Function
	 *
	 * Common buffer format:
	 *     Status;    (Byte 0 of the data buffer)
	 *     Length;    (Byte 1 of the data buffer)
	 *     Data[x-1]: (Bytes 2-x of the arbitrary length data buffer)
	 */
	if (source_desc->common.type != ACPI_TYPE_BUFFER) {
		ACPI_ERROR((AE_INFO,
			    "SMBus/IPMI/GenericSerialBus write requires "
			    "Buffer, found type %s",
			    acpi_ut_get_object_type_name(source_desc)));

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	switch (obj_desc->field.region_obj->region.space_id) {
	case ACPI_ADR_SPACE_SMBUS:

		buffer_length = ACPI_SMBUS_BUFFER_SIZE;
		function = ACPI_WRITE | (obj_desc->field.attribute << 16);
		break;

	case ACPI_ADR_SPACE_IPMI:

		buffer_length = ACPI_IPMI_BUFFER_SIZE;
		function = ACPI_WRITE;
		break;

	case ACPI_ADR_SPACE_GSBUS:

		accessor_type = obj_desc->field.attribute;
		status =
		    acpi_ex_get_protocol_buffer_length(accessor_type,
						       &buffer_length);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Invalid protocol ID for GSBus: 0x%4.4X",
				    accessor_type));

			return_ACPI_STATUS(status);
		}

		/* Add header length to get the full size of the buffer */

		buffer_length += ACPI_SERIAL_HEADER_SIZE;
		function = ACPI_WRITE | (accessor_type << 16);
		break;

	case ACPI_ADR_SPACE_PLATFORM_RT:

		buffer_length = ACPI_PRM_INPUT_BUFFER_SIZE;
		function = ACPI_WRITE;
		break;

	case ACPI_ADR_SPACE_FIXED_HARDWARE:

		buffer_length = ACPI_FFH_INPUT_BUFFER_SIZE;
		function = ACPI_WRITE;
		break;

	default:
		return_ACPI_STATUS(AE_AML_INVALID_SPACE_ID);
	}

	/* Create the transfer/bidirectional/return buffer */

	buffer_desc = acpi_ut_create_buffer_object(buffer_length);
	if (!buffer_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Copy the input buffer data to the transfer buffer */

	buffer = buffer_desc->buffer.pointer;
	data_length = (buffer_length < source_desc->buffer.length ?
		       buffer_length : source_desc->buffer.length);
	memcpy(buffer, source_desc->buffer.pointer, data_length);

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/*
	 * Perform the write (returns status and perhaps data in the
	 * same buffer)
	 */
	status = acpi_ex_access_region(obj_desc, 0, (u64 *)buffer, function);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);

	*return_buffer = buffer_desc;
	return_ACPI_STATUS(status);
}
