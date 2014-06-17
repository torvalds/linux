/*******************************************************************************
 *
 * Module Name: utresrc - Resource management utilities
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2014, Intel Corp.
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
#include "acresrc.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utresrc")

#if defined(ACPI_DEBUG_OUTPUT) || defined (ACPI_DISASSEMBLER) || defined (ACPI_DEBUGGER)
/*
 * Strings used to decode resource descriptors.
 * Used by both the disassembler and the debugger resource dump routines
 */
const char *acpi_gbl_bm_decode[] = {
	"NotBusMaster",
	"BusMaster"
};

const char *acpi_gbl_config_decode[] = {
	"0 - Good Configuration",
	"1 - Acceptable Configuration",
	"2 - Suboptimal Configuration",
	"3 - ***Invalid Configuration***",
};

const char *acpi_gbl_consume_decode[] = {
	"ResourceProducer",
	"ResourceConsumer"
};

const char *acpi_gbl_dec_decode[] = {
	"PosDecode",
	"SubDecode"
};

const char *acpi_gbl_he_decode[] = {
	"Level",
	"Edge"
};

const char *acpi_gbl_io_decode[] = {
	"Decode10",
	"Decode16"
};

const char *acpi_gbl_ll_decode[] = {
	"ActiveHigh",
	"ActiveLow"
};

const char *acpi_gbl_max_decode[] = {
	"MaxNotFixed",
	"MaxFixed"
};

const char *acpi_gbl_mem_decode[] = {
	"NonCacheable",
	"Cacheable",
	"WriteCombining",
	"Prefetchable"
};

const char *acpi_gbl_min_decode[] = {
	"MinNotFixed",
	"MinFixed"
};

const char *acpi_gbl_mtp_decode[] = {
	"AddressRangeMemory",
	"AddressRangeReserved",
	"AddressRangeACPI",
	"AddressRangeNVS"
};

const char *acpi_gbl_rng_decode[] = {
	"InvalidRanges",
	"NonISAOnlyRanges",
	"ISAOnlyRanges",
	"EntireRange"
};

const char *acpi_gbl_rw_decode[] = {
	"ReadOnly",
	"ReadWrite"
};

const char *acpi_gbl_shr_decode[] = {
	"Exclusive",
	"Shared",
	"ExclusiveAndWake",	/* ACPI 5.0 */
	"SharedAndWake"		/* ACPI 5.0 */
};

const char *acpi_gbl_siz_decode[] = {
	"Transfer8",
	"Transfer8_16",
	"Transfer16",
	"InvalidSize"
};

const char *acpi_gbl_trs_decode[] = {
	"DenseTranslation",
	"SparseTranslation"
};

const char *acpi_gbl_ttp_decode[] = {
	"TypeStatic",
	"TypeTranslation"
};

const char *acpi_gbl_typ_decode[] = {
	"Compatibility",
	"TypeA",
	"TypeB",
	"TypeF"
};

const char *acpi_gbl_ppc_decode[] = {
	"PullDefault",
	"PullUp",
	"PullDown",
	"PullNone"
};

const char *acpi_gbl_ior_decode[] = {
	"IoRestrictionNone",
	"IoRestrictionInputOnly",
	"IoRestrictionOutputOnly",
	"IoRestrictionNoneAndPreserve"
};

const char *acpi_gbl_dts_decode[] = {
	"Width8bit",
	"Width16bit",
	"Width32bit",
	"Width64bit",
	"Width128bit",
	"Width256bit",
};

/* GPIO connection type */

const char *acpi_gbl_ct_decode[] = {
	"Interrupt",
	"I/O"
};

/* Serial bus type */

const char *acpi_gbl_sbt_decode[] = {
	"/* UNKNOWN serial bus type */",
	"I2C",
	"SPI",
	"UART"
};

/* I2C serial bus access mode */

const char *acpi_gbl_am_decode[] = {
	"AddressingMode7Bit",
	"AddressingMode10Bit"
};

/* I2C serial bus slave mode */

const char *acpi_gbl_sm_decode[] = {
	"ControllerInitiated",
	"DeviceInitiated"
};

/* SPI serial bus wire mode */

const char *acpi_gbl_wm_decode[] = {
	"FourWireMode",
	"ThreeWireMode"
};

/* SPI serial clock phase */

const char *acpi_gbl_cph_decode[] = {
	"ClockPhaseFirst",
	"ClockPhaseSecond"
};

/* SPI serial bus clock polarity */

const char *acpi_gbl_cpo_decode[] = {
	"ClockPolarityLow",
	"ClockPolarityHigh"
};

/* SPI serial bus device polarity */

const char *acpi_gbl_dp_decode[] = {
	"PolarityLow",
	"PolarityHigh"
};

/* UART serial bus endian */

const char *acpi_gbl_ed_decode[] = {
	"LittleEndian",
	"BigEndian"
};

/* UART serial bus bits per byte */

const char *acpi_gbl_bpb_decode[] = {
	"DataBitsFive",
	"DataBitsSix",
	"DataBitsSeven",
	"DataBitsEight",
	"DataBitsNine",
	"/* UNKNOWN Bits per byte */",
	"/* UNKNOWN Bits per byte */",
	"/* UNKNOWN Bits per byte */"
};

/* UART serial bus stop bits */

const char *acpi_gbl_sb_decode[] = {
	"StopBitsNone",
	"StopBitsOne",
	"StopBitsOnePlusHalf",
	"StopBitsTwo"
};

/* UART serial bus flow control */

const char *acpi_gbl_fc_decode[] = {
	"FlowControlNone",
	"FlowControlHardware",
	"FlowControlXON",
	"/* UNKNOWN flow control keyword */"
};

/* UART serial bus parity type */

const char *acpi_gbl_pt_decode[] = {
	"ParityTypeNone",
	"ParityTypeEven",
	"ParityTypeOdd",
	"ParityTypeMark",
	"ParityTypeSpace",
	"/* UNKNOWN parity keyword */",
	"/* UNKNOWN parity keyword */",
	"/* UNKNOWN parity keyword */"
};

#endif

/*
 * Base sizes of the raw AML resource descriptors, indexed by resource type.
 * Zero indicates a reserved (and therefore invalid) resource type.
 */
const u8 acpi_gbl_resource_aml_sizes[] = {
	/* Small descriptors */

	0,
	0,
	0,
	0,
	ACPI_AML_SIZE_SMALL(struct aml_resource_irq),
	ACPI_AML_SIZE_SMALL(struct aml_resource_dma),
	ACPI_AML_SIZE_SMALL(struct aml_resource_start_dependent),
	ACPI_AML_SIZE_SMALL(struct aml_resource_end_dependent),
	ACPI_AML_SIZE_SMALL(struct aml_resource_io),
	ACPI_AML_SIZE_SMALL(struct aml_resource_fixed_io),
	ACPI_AML_SIZE_SMALL(struct aml_resource_fixed_dma),
	0,
	0,
	0,
	ACPI_AML_SIZE_SMALL(struct aml_resource_vendor_small),
	ACPI_AML_SIZE_SMALL(struct aml_resource_end_tag),

	/* Large descriptors */

	0,
	ACPI_AML_SIZE_LARGE(struct aml_resource_memory24),
	ACPI_AML_SIZE_LARGE(struct aml_resource_generic_register),
	0,
	ACPI_AML_SIZE_LARGE(struct aml_resource_vendor_large),
	ACPI_AML_SIZE_LARGE(struct aml_resource_memory32),
	ACPI_AML_SIZE_LARGE(struct aml_resource_fixed_memory32),
	ACPI_AML_SIZE_LARGE(struct aml_resource_address32),
	ACPI_AML_SIZE_LARGE(struct aml_resource_address16),
	ACPI_AML_SIZE_LARGE(struct aml_resource_extended_irq),
	ACPI_AML_SIZE_LARGE(struct aml_resource_address64),
	ACPI_AML_SIZE_LARGE(struct aml_resource_extended_address64),
	ACPI_AML_SIZE_LARGE(struct aml_resource_gpio),
	0,
	ACPI_AML_SIZE_LARGE(struct aml_resource_common_serialbus),
};

const u8 acpi_gbl_resource_aml_serial_bus_sizes[] = {
	0,
	ACPI_AML_SIZE_LARGE(struct aml_resource_i2c_serialbus),
	ACPI_AML_SIZE_LARGE(struct aml_resource_spi_serialbus),
	ACPI_AML_SIZE_LARGE(struct aml_resource_uart_serialbus),
};

/*
 * Resource types, used to validate the resource length field.
 * The length of fixed-length types must match exactly, variable
 * lengths must meet the minimum required length, etc.
 * Zero indicates a reserved (and therefore invalid) resource type.
 */
static const u8 acpi_gbl_resource_types[] = {
	/* Small descriptors */

	0,
	0,
	0,
	0,
	ACPI_SMALL_VARIABLE_LENGTH,	/* 04 IRQ */
	ACPI_FIXED_LENGTH,	/* 05 DMA */
	ACPI_SMALL_VARIABLE_LENGTH,	/* 06 start_dependent_functions */
	ACPI_FIXED_LENGTH,	/* 07 end_dependent_functions */
	ACPI_FIXED_LENGTH,	/* 08 IO */
	ACPI_FIXED_LENGTH,	/* 09 fixed_IO */
	ACPI_FIXED_LENGTH,	/* 0A fixed_DMA */
	0,
	0,
	0,
	ACPI_VARIABLE_LENGTH,	/* 0E vendor_short */
	ACPI_FIXED_LENGTH,	/* 0F end_tag */

	/* Large descriptors */

	0,
	ACPI_FIXED_LENGTH,	/* 01 Memory24 */
	ACPI_FIXED_LENGTH,	/* 02 generic_register */
	0,
	ACPI_VARIABLE_LENGTH,	/* 04 vendor_long */
	ACPI_FIXED_LENGTH,	/* 05 Memory32 */
	ACPI_FIXED_LENGTH,	/* 06 memory32_fixed */
	ACPI_VARIABLE_LENGTH,	/* 07 Dword* address */
	ACPI_VARIABLE_LENGTH,	/* 08 Word* address */
	ACPI_VARIABLE_LENGTH,	/* 09 extended_IRQ */
	ACPI_VARIABLE_LENGTH,	/* 0A Qword* address */
	ACPI_FIXED_LENGTH,	/* 0B Extended* address */
	ACPI_VARIABLE_LENGTH,	/* 0C Gpio* */
	0,
	ACPI_VARIABLE_LENGTH	/* 0E *serial_bus */
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_walk_aml_resources
 *
 * PARAMETERS:  walk_state          - Current walk info
 * PARAMETERS:  aml                 - Pointer to the raw AML resource template
 *              aml_length          - Length of the entire template
 *              user_function       - Called once for each descriptor found. If
 *                                    NULL, a pointer to the end_tag is returned
 *              context             - Passed to user_function
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk a raw AML resource list(buffer). User function called
 *              once for each resource found.
 *
 ******************************************************************************/

acpi_status
acpi_ut_walk_aml_resources(struct acpi_walk_state *walk_state,
			   u8 *aml,
			   acpi_size aml_length,
			   acpi_walk_aml_callback user_function, void **context)
{
	acpi_status status;
	u8 *end_aml;
	u8 resource_index;
	u32 length;
	u32 offset = 0;
	u8 end_tag[2] = { 0x79, 0x00 };

	ACPI_FUNCTION_TRACE(ut_walk_aml_resources);

	/* The absolute minimum resource template is one end_tag descriptor */

	if (aml_length < sizeof(struct aml_resource_end_tag)) {
		return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
	}

	/* Point to the end of the resource template buffer */

	end_aml = aml + aml_length;

	/* Walk the byte list, abort on any invalid descriptor type or length */

	while (aml < end_aml) {

		/* Validate the Resource Type and Resource Length */

		status =
		    acpi_ut_validate_resource(walk_state, aml, &resource_index);
		if (ACPI_FAILURE(status)) {
			/*
			 * Exit on failure. Cannot continue because the descriptor length
			 * may be bogus also.
			 */
			return_ACPI_STATUS(status);
		}

		/* Get the length of this descriptor */

		length = acpi_ut_get_descriptor_length(aml);

		/* Invoke the user function */

		if (user_function) {
			status =
			    user_function(aml, length, offset, resource_index,
					  context);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		/* An end_tag descriptor terminates this resource template */

		if (acpi_ut_get_resource_type(aml) ==
		    ACPI_RESOURCE_NAME_END_TAG) {
			/*
			 * There must be at least one more byte in the buffer for
			 * the 2nd byte of the end_tag
			 */
			if ((aml + 1) >= end_aml) {
				return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
			}

			/* Return the pointer to the end_tag if requested */

			if (!user_function) {
				*context = aml;
			}

			/* Normal exit */

			return_ACPI_STATUS(AE_OK);
		}

		aml += length;
		offset += length;
	}

	/* Did not find an end_tag descriptor */

	if (user_function) {

		/* Insert an end_tag anyway. acpi_rs_get_list_length always leaves room */

		(void)acpi_ut_validate_resource(walk_state, end_tag,
						&resource_index);
		status =
		    user_function(end_tag, 2, offset, resource_index, context);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	return_ACPI_STATUS(AE_AML_NO_RESOURCE_END_TAG);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_validate_resource
 *
 * PARAMETERS:  walk_state          - Current walk info
 *              aml                 - Pointer to the raw AML resource descriptor
 *              return_index        - Where the resource index is returned. NULL
 *                                    if the index is not required.
 *
 * RETURN:      Status, and optionally the Index into the global resource tables
 *
 * DESCRIPTION: Validate an AML resource descriptor by checking the Resource
 *              Type and Resource Length. Returns an index into the global
 *              resource information/dispatch tables for later use.
 *
 ******************************************************************************/

acpi_status
acpi_ut_validate_resource(struct acpi_walk_state *walk_state,
			  void *aml, u8 *return_index)
{
	union aml_resource *aml_resource;
	u8 resource_type;
	u8 resource_index;
	acpi_rs_length resource_length;
	acpi_rs_length minimum_resource_length;

	ACPI_FUNCTION_ENTRY();

	/*
	 * 1) Validate the resource_type field (Byte 0)
	 */
	resource_type = ACPI_GET8(aml);

	/*
	 * Byte 0 contains the descriptor name (Resource Type)
	 * Examine the large/small bit in the resource header
	 */
	if (resource_type & ACPI_RESOURCE_NAME_LARGE) {

		/* Verify the large resource type (name) against the max */

		if (resource_type > ACPI_RESOURCE_NAME_LARGE_MAX) {
			goto invalid_resource;
		}

		/*
		 * Large Resource Type -- bits 6:0 contain the name
		 * Translate range 0x80-0x8B to index range 0x10-0x1B
		 */
		resource_index = (u8) (resource_type - 0x70);
	} else {
		/*
		 * Small Resource Type -- bits 6:3 contain the name
		 * Shift range to index range 0x00-0x0F
		 */
		resource_index = (u8)
		    ((resource_type & ACPI_RESOURCE_NAME_SMALL_MASK) >> 3);
	}

	/*
	 * Check validity of the resource type, via acpi_gbl_resource_types. Zero
	 * indicates an invalid resource.
	 */
	if (!acpi_gbl_resource_types[resource_index]) {
		goto invalid_resource;
	}

	/*
	 * Validate the resource_length field. This ensures that the length
	 * is at least reasonable, and guarantees that it is non-zero.
	 */
	resource_length = acpi_ut_get_resource_length(aml);
	minimum_resource_length = acpi_gbl_resource_aml_sizes[resource_index];

	/* Validate based upon the type of resource - fixed length or variable */

	switch (acpi_gbl_resource_types[resource_index]) {
	case ACPI_FIXED_LENGTH:

		/* Fixed length resource, length must match exactly */

		if (resource_length != minimum_resource_length) {
			goto bad_resource_length;
		}
		break;

	case ACPI_VARIABLE_LENGTH:

		/* Variable length resource, length must be at least the minimum */

		if (resource_length < minimum_resource_length) {
			goto bad_resource_length;
		}
		break;

	case ACPI_SMALL_VARIABLE_LENGTH:

		/* Small variable length resource, length can be (Min) or (Min-1) */

		if ((resource_length > minimum_resource_length) ||
		    (resource_length < (minimum_resource_length - 1))) {
			goto bad_resource_length;
		}
		break;

	default:

		/* Shouldn't happen (because of validation earlier), but be sure */

		goto invalid_resource;
	}

	aml_resource = ACPI_CAST_PTR(union aml_resource, aml);
	if (resource_type == ACPI_RESOURCE_NAME_SERIAL_BUS) {

		/* Validate the bus_type field */

		if ((aml_resource->common_serial_bus.type == 0) ||
		    (aml_resource->common_serial_bus.type >
		     AML_RESOURCE_MAX_SERIALBUSTYPE)) {
			if (walk_state) {
				ACPI_ERROR((AE_INFO,
					    "Invalid/unsupported SerialBus resource descriptor: BusType 0x%2.2X",
					    aml_resource->common_serial_bus.
					    type));
			}
			return (AE_AML_INVALID_RESOURCE_TYPE);
		}
	}

	/* Optionally return the resource table index */

	if (return_index) {
		*return_index = resource_index;
	}

	return (AE_OK);

invalid_resource:

	if (walk_state) {
		ACPI_ERROR((AE_INFO,
			    "Invalid/unsupported resource descriptor: Type 0x%2.2X",
			    resource_type));
	}
	return (AE_AML_INVALID_RESOURCE_TYPE);

bad_resource_length:

	if (walk_state) {
		ACPI_ERROR((AE_INFO,
			    "Invalid resource descriptor length: Type "
			    "0x%2.2X, Length 0x%4.4X, MinLength 0x%4.4X",
			    resource_type, resource_length,
			    minimum_resource_length));
	}
	return (AE_AML_BAD_RESOURCE_LENGTH);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_type
 *
 * PARAMETERS:  aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      The Resource Type with no extraneous bits (except the
 *              Large/Small descriptor bit -- this is left alone)
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

u8 acpi_ut_get_resource_type(void *aml)
{
	ACPI_FUNCTION_ENTRY();

	/*
	 * Byte 0 contains the descriptor name (Resource Type)
	 * Examine the large/small bit in the resource header
	 */
	if (ACPI_GET8(aml) & ACPI_RESOURCE_NAME_LARGE) {

		/* Large Resource Type -- bits 6:0 contain the name */

		return (ACPI_GET8(aml));
	} else {
		/* Small Resource Type -- bits 6:3 contain the name */

		return ((u8) (ACPI_GET8(aml) & ACPI_RESOURCE_NAME_SMALL_MASK));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_length
 *
 * PARAMETERS:  aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte Length
 *
 * DESCRIPTION: Get the "Resource Length" of a raw AML descriptor. By
 *              definition, this does not include the size of the descriptor
 *              header or the length field itself.
 *
 ******************************************************************************/

u16 acpi_ut_get_resource_length(void *aml)
{
	acpi_rs_length resource_length;

	ACPI_FUNCTION_ENTRY();

	/*
	 * Byte 0 contains the descriptor name (Resource Type)
	 * Examine the large/small bit in the resource header
	 */
	if (ACPI_GET8(aml) & ACPI_RESOURCE_NAME_LARGE) {

		/* Large Resource type -- bytes 1-2 contain the 16-bit length */

		ACPI_MOVE_16_TO_16(&resource_length, ACPI_ADD_PTR(u8, aml, 1));

	} else {
		/* Small Resource type -- bits 2:0 of byte 0 contain the length */

		resource_length = (u16) (ACPI_GET8(aml) &
					 ACPI_RESOURCE_NAME_SMALL_LENGTH_MASK);
	}

	return (resource_length);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_header_length
 *
 * PARAMETERS:  aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Length of the AML header (depends on large/small descriptor)
 *
 * DESCRIPTION: Get the length of the header for this resource.
 *
 ******************************************************************************/

u8 acpi_ut_get_resource_header_length(void *aml)
{
	ACPI_FUNCTION_ENTRY();

	/* Examine the large/small bit in the resource header */

	if (ACPI_GET8(aml) & ACPI_RESOURCE_NAME_LARGE) {
		return (sizeof(struct aml_resource_large_header));
	} else {
		return (sizeof(struct aml_resource_small_header));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_descriptor_length
 *
 * PARAMETERS:  aml             - Pointer to the raw AML resource descriptor
 *
 * RETURN:      Byte length
 *
 * DESCRIPTION: Get the total byte length of a raw AML descriptor, including the
 *              length of the descriptor header and the length field itself.
 *              Used to walk descriptor lists.
 *
 ******************************************************************************/

u32 acpi_ut_get_descriptor_length(void *aml)
{
	ACPI_FUNCTION_ENTRY();

	/*
	 * Get the Resource Length (does not include header length) and add
	 * the header length (depends on if this is a small or large resource)
	 */
	return (acpi_ut_get_resource_length(aml) +
		acpi_ut_get_resource_header_length(aml));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_resource_end_tag
 *
 * PARAMETERS:  obj_desc        - The resource template buffer object
 *              end_tag         - Where the pointer to the end_tag is returned
 *
 * RETURN:      Status, pointer to the end tag
 *
 * DESCRIPTION: Find the end_tag resource descriptor in an AML resource template
 *              Note: allows a buffer length of zero.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_resource_end_tag(union acpi_operand_object *obj_desc, u8 **end_tag)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_get_resource_end_tag);

	/* Allow a buffer length of zero */

	if (!obj_desc->buffer.length) {
		*end_tag = obj_desc->buffer.pointer;
		return_ACPI_STATUS(AE_OK);
	}

	/* Validate the template and get a pointer to the end_tag */

	status = acpi_ut_walk_aml_resources(NULL, obj_desc->buffer.pointer,
					    obj_desc->buffer.length, NULL,
					    (void **)end_tag);

	return_ACPI_STATUS(status);
}
