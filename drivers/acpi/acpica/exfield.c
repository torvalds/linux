// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exfield - AML execution - field_unit read/write
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exfield")

/*
 * This table maps the various Attrib protocols to the byte transfer
 * length. Used for the generic serial bus.
 */
#define ACPI_INVALID_PROTOCOL_ID        0x80
#define ACPI_MAX_PROTOCOL_ID            0x0F
static const u8 acpi_protocol_lengths[] = {
	ACPI_INVALID_PROTOCOL_ID,	/* 0 - reserved */
	ACPI_INVALID_PROTOCOL_ID,	/* 1 - reserved */
	0x00,			/* 2 - ATTRIB_QUICK */
	ACPI_INVALID_PROTOCOL_ID,	/* 3 - reserved */
	0x01,			/* 4 - ATTRIB_SEND_RECEIVE */
	ACPI_INVALID_PROTOCOL_ID,	/* 5 - reserved */
	0x01,			/* 6 - ATTRIB_BYTE */
	ACPI_INVALID_PROTOCOL_ID,	/* 7 - reserved */
	0x02,			/* 8 - ATTRIB_WORD */
	ACPI_INVALID_PROTOCOL_ID,	/* 9 - reserved */
	0xFF,			/* A - ATTRIB_BLOCK  */
	0xFF,			/* B - ATTRIB_BYTES */
	0x02,			/* C - ATTRIB_PROCESS_CALL */
	0xFF,			/* D - ATTRIB_BLOCK_PROCESS_CALL */
	0xFF,			/* E - ATTRIB_RAW_BYTES */
	0xFF			/* F - ATTRIB_RAW_PROCESS_BYTES */
};

#define PCC_MASTER_SUBSPACE     3

/*
 * The following macros determine a given offset is a COMD field.
 * According to the specification, generic subspaces (types 0-2) contains a
 * 2-byte COMD field at offset 4 and master subspaces (type 3) contains a 4-byte
 * COMD field starting at offset 12.
 */
#define GENERIC_SUBSPACE_COMMAND(a)     (4 == a || a == 5)
#define MASTER_SUBSPACE_COMMAND(a)      (12 <= a && a <= 15)

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_get_protocol_buffer_length
 *
 * PARAMETERS:  protocol_id     - The type of the protocol indicated by region
 *                                field access attributes
 *              return_length   - Where the protocol byte transfer length is
 *                                returned
 *
 * RETURN:      Status and decoded byte transfer length
 *
 * DESCRIPTION: This routine returns the length of the generic_serial_bus
 *              protocol bytes
 *
 ******************************************************************************/

acpi_status
acpi_ex_get_protocol_buffer_length(u32 protocol_id, u32 *return_length)
{

	if ((protocol_id > ACPI_MAX_PROTOCOL_ID) ||
	    (acpi_protocol_lengths[protocol_id] == ACPI_INVALID_PROTOCOL_ID)) {
		ACPI_ERROR((AE_INFO,
			    "Invalid Field/AccessAs protocol ID: 0x%4.4X",
			    protocol_id));

		return (AE_AML_PROTOCOL);
	}

	*return_length = acpi_protocol_lengths[protocol_id];
	return (AE_OK);
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
 *              Buffer, depending on the size of the field and whether if a
 *              field is created by the create_field() operator.
 *
 ******************************************************************************/

acpi_status
acpi_ex_read_data_from_field(struct acpi_walk_state *walk_state,
			     union acpi_operand_object *obj_desc,
			     union acpi_operand_object **ret_buffer_desc)
{
	acpi_status status;
	union acpi_operand_object *buffer_desc;
	void *buffer;
	u32 buffer_length;

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

		/* SMBus, GSBus, IPMI serial */

		status = acpi_ex_read_serial_bus(obj_desc, ret_buffer_desc);
		return_ACPI_STATUS(status);
	}

	/*
	 * Allocate a buffer for the contents of the field.
	 *
	 * If the field is larger than the current integer width, create
	 * a BUFFER to hold it. Otherwise, use an INTEGER. This allows
	 * the use of arithmetic operators on the returned value if the
	 * field size is equal or smaller than an Integer.
	 *
	 * However, all buffer fields created by create_field operator needs to
	 * remain as a buffer to match other AML interpreter implementations.
	 *
	 * Note: Field.length is in bits.
	 */
	buffer_length =
	    (acpi_size)ACPI_ROUND_BITS_UP_TO_BYTES(obj_desc->field.bit_length);

	if (buffer_length > acpi_gbl_integer_byte_width ||
	    (obj_desc->common.type == ACPI_TYPE_BUFFER_FIELD &&
	     obj_desc->buffer_field.is_create_field)) {

		/* Field is too large for an Integer, create a Buffer instead */

		buffer_desc = acpi_ut_create_buffer_object(buffer_length);
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

		buffer_length = acpi_gbl_integer_byte_width;
		buffer = &buffer_desc->integer.value;
	}

	if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
	    (obj_desc->field.region_obj->region.space_id ==
	     ACPI_ADR_SPACE_GPIO)) {

		/* General Purpose I/O */

		status = acpi_ex_read_gpio(obj_desc, buffer);
		goto exit;
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_PLATFORM_COMM)) {
		/*
		 * Reading from a PCC field unit does not require the handler because
		 * it only requires reading from the internal_pcc_buffer.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
				  "PCC FieldRead bits %u\n",
				  obj_desc->field.bit_length));

		memcpy(buffer,
		       obj_desc->field.region_obj->field.internal_pcc_buffer +
		       obj_desc->field.base_byte_offset,
		       (acpi_size)ACPI_ROUND_BITS_UP_TO_BYTES(obj_desc->field.
							      bit_length));

		*ret_buffer_desc = buffer_desc;
		return AE_OK;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldRead [TO]:   Obj %p, Type %X, Buf %p, ByteLen %X\n",
			  obj_desc, obj_desc->common.type, buffer,
			  buffer_length));
	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldRead [FROM]: BitLen %X, BitOff %X, ByteOff %X\n",
			  obj_desc->common_field.bit_length,
			  obj_desc->common_field.start_field_bit_offset,
			  obj_desc->common_field.base_byte_offset));

	/* Lock entire transaction if requested */

	acpi_ex_acquire_global_lock(obj_desc->common_field.field_flags);

	/* Read from the field */

	status = acpi_ex_extract_from_field(obj_desc, buffer, buffer_length);
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
	u32 buffer_length;
	u32 data_length;
	void *buffer;

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
		    ACPI_ADR_SPACE_GPIO)) {

		/* General Purpose I/O */

		status = acpi_ex_write_gpio(source_desc, obj_desc, result_desc);
		return_ACPI_STATUS(status);
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_SMBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_GSBUS
		    || obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_IPMI)) {

		/* SMBus, GSBus, IPMI serial */

		status =
		    acpi_ex_write_serial_bus(source_desc, obj_desc,
					     result_desc);
		return_ACPI_STATUS(status);
	} else if ((obj_desc->common.type == ACPI_TYPE_LOCAL_REGION_FIELD) &&
		   (obj_desc->field.region_obj->region.space_id ==
		    ACPI_ADR_SPACE_PLATFORM_COMM)) {
		/*
		 * According to the spec a write to the COMD field will invoke the
		 * region handler. Otherwise, write to the pcc_internal buffer. This
		 * implementation will use the offsets specified rather than the name
		 * of the field. This is considered safer because some firmware tools
		 * are known to obfiscate named objects.
		 */
		data_length =
		    (acpi_size)ACPI_ROUND_BITS_UP_TO_BYTES(obj_desc->field.
							   bit_length);
		memcpy(obj_desc->field.region_obj->field.internal_pcc_buffer +
		       obj_desc->field.base_byte_offset,
		       source_desc->buffer.pointer, data_length);

		if (MASTER_SUBSPACE_COMMAND(obj_desc->field.base_byte_offset)) {

			/* Perform the write */

			ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
					  "PCC COMD field has been written. Invoking PCC handler now.\n"));

			status =
			    acpi_ex_access_region(obj_desc, 0,
						  (u64 *)obj_desc->field.
						  region_obj->field.
						  internal_pcc_buffer,
						  ACPI_WRITE);
			return_ACPI_STATUS(status);
		}
		return (AE_OK);
	}

	/* Get a pointer to the data to be written */

	switch (source_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		buffer = &source_desc->integer.value;
		buffer_length = sizeof(source_desc->integer.value);
		break;

	case ACPI_TYPE_BUFFER:

		buffer = source_desc->buffer.pointer;
		buffer_length = source_desc->buffer.length;
		break;

	case ACPI_TYPE_STRING:

		buffer = source_desc->string.pointer;
		buffer_length = source_desc->string.length;
		break;

	default:
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_BFIELD,
			  "FieldWrite [FROM]: Obj %p (%s:%X), Buf %p, ByteLen %X\n",
			  source_desc,
			  acpi_ut_get_type_name(source_desc->common.type),
			  source_desc->common.type, buffer, buffer_length));

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

	status = acpi_ex_insert_into_field(obj_desc, buffer, buffer_length);
	acpi_ex_release_global_lock(obj_desc->common_field.field_flags);
	return_ACPI_STATUS(status);
}
