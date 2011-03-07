/*******************************************************************************
 *
 * Module Name: rsmisc - Miscellaneous resource descriptors
 *
 ******************************************************************************/

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
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsmisc")
#define INIT_RESOURCE_TYPE(i)       i->resource_offset
#define INIT_RESOURCE_LENGTH(i)     i->aml_offset
#define INIT_TABLE_LENGTH(i)        i->value
#define COMPARE_OPCODE(i)           i->resource_offset
#define COMPARE_TARGET(i)           i->aml_offset
#define COMPARE_VALUE(i)            i->value
/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_convert_aml_to_resource
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *              Info                - Pointer to appropriate conversion table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an external AML resource descriptor to the corresponding
 *              internal resource descriptor
 *
 ******************************************************************************/
acpi_status
acpi_rs_convert_aml_to_resource(struct acpi_resource *resource,
				union aml_resource *aml,
				struct acpi_rsconvert_info *info)
{
	acpi_rs_length aml_resource_length;
	void *source;
	void *destination;
	char *target;
	u8 count;
	u8 flags_mode = FALSE;
	u16 item_count = 0;
	u16 temp16 = 0;

	ACPI_FUNCTION_TRACE(rs_convert_aml_to_resource);

	if (((acpi_size) resource) & 0x3) {

		/* Each internal resource struct is expected to be 32-bit aligned */

		ACPI_WARNING((AE_INFO,
			      "Misaligned resource pointer (get): %p Type 0x%2.2X Length %u",
			      resource, resource->type, resource->length));
	}

	/* Extract the resource Length field (does not include header length) */

	aml_resource_length = acpi_ut_get_resource_length(aml);

	/*
	 * First table entry must be ACPI_RSC_INITxxx and must contain the
	 * table length (# of table entries)
	 */
	count = INIT_TABLE_LENGTH(info);

	while (count) {
		/*
		 * Source is the external AML byte stream buffer,
		 * destination is the internal resource descriptor
		 */
		source = ACPI_ADD_PTR(void, aml, info->aml_offset);
		destination =
		    ACPI_ADD_PTR(void, resource, info->resource_offset);

		switch (info->opcode) {
		case ACPI_RSC_INITGET:
			/*
			 * Get the resource type and the initial (minimum) length
			 */
			ACPI_MEMSET(resource, 0, INIT_RESOURCE_LENGTH(info));
			resource->type = INIT_RESOURCE_TYPE(info);
			resource->length = INIT_RESOURCE_LENGTH(info);
			break;

		case ACPI_RSC_INITSET:
			break;

		case ACPI_RSC_FLAGINIT:

			flags_mode = TRUE;
			break;

		case ACPI_RSC_1BITFLAG:
			/*
			 * Mask and shift the flag bit
			 */
			ACPI_SET8(destination) = (u8)
			    ((ACPI_GET8(source) >> info->value) & 0x01);
			break;

		case ACPI_RSC_2BITFLAG:
			/*
			 * Mask and shift the flag bits
			 */
			ACPI_SET8(destination) = (u8)
			    ((ACPI_GET8(source) >> info->value) & 0x03);
			break;

		case ACPI_RSC_COUNT:

			item_count = ACPI_GET8(source);
			ACPI_SET8(destination) = (u8) item_count;

			resource->length = resource->length +
			    (info->value * (item_count - 1));
			break;

		case ACPI_RSC_COUNT16:

			item_count = aml_resource_length;
			ACPI_SET16(destination) = item_count;

			resource->length = resource->length +
			    (info->value * (item_count - 1));
			break;

		case ACPI_RSC_LENGTH:

			resource->length = resource->length + info->value;
			break;

		case ACPI_RSC_MOVE8:
		case ACPI_RSC_MOVE16:
		case ACPI_RSC_MOVE32:
		case ACPI_RSC_MOVE64:
			/*
			 * Raw data move. Use the Info value field unless item_count has
			 * been previously initialized via a COUNT opcode
			 */
			if (info->value) {
				item_count = info->value;
			}
			acpi_rs_move_data(destination, source, item_count,
					  info->opcode);
			break;

		case ACPI_RSC_SET8:

			ACPI_MEMSET(destination, info->aml_offset, info->value);
			break;

		case ACPI_RSC_DATA8:

			target = ACPI_ADD_PTR(char, resource, info->value);
			ACPI_MEMCPY(destination, source, ACPI_GET16(target));
			break;

		case ACPI_RSC_ADDRESS:
			/*
			 * Common handler for address descriptor flags
			 */
			if (!acpi_rs_get_address_common(resource, aml)) {
				return_ACPI_STATUS
				    (AE_AML_INVALID_RESOURCE_TYPE);
			}
			break;

		case ACPI_RSC_SOURCE:
			/*
			 * Optional resource_source (Index and String)
			 */
			resource->length +=
			    acpi_rs_get_resource_source(aml_resource_length,
							info->value,
							destination, aml, NULL);
			break;

		case ACPI_RSC_SOURCEX:
			/*
			 * Optional resource_source (Index and String). This is the more
			 * complicated case used by the Interrupt() macro
			 */
			target =
			    ACPI_ADD_PTR(char, resource,
					 info->aml_offset + (item_count * 4));

			resource->length +=
			    acpi_rs_get_resource_source(aml_resource_length,
							(acpi_rs_length) (((item_count - 1) * sizeof(u32)) + info->value), destination, aml, target);
			break;

		case ACPI_RSC_BITMASK:
			/*
			 * 8-bit encoded bitmask (DMA macro)
			 */
			item_count =
			    acpi_rs_decode_bitmask(ACPI_GET8(source),
						   destination);
			if (item_count) {
				resource->length += (item_count - 1);
			}

			target = ACPI_ADD_PTR(char, resource, info->value);
			ACPI_SET8(target) = (u8) item_count;
			break;

		case ACPI_RSC_BITMASK16:
			/*
			 * 16-bit encoded bitmask (IRQ macro)
			 */
			ACPI_MOVE_16_TO_16(&temp16, source);

			item_count =
			    acpi_rs_decode_bitmask(temp16, destination);
			if (item_count) {
				resource->length += (item_count - 1);
			}

			target = ACPI_ADD_PTR(char, resource, info->value);
			ACPI_SET8(target) = (u8) item_count;
			break;

		case ACPI_RSC_EXIT_NE:
			/*
			 * Control - Exit conversion if not equal
			 */
			switch (info->resource_offset) {
			case ACPI_RSC_COMPARE_AML_LENGTH:
				if (aml_resource_length != info->value) {
					goto exit;
				}
				break;

			case ACPI_RSC_COMPARE_VALUE:
				if (ACPI_GET8(source) != info->value) {
					goto exit;
				}
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Invalid conversion sub-opcode"));
				return_ACPI_STATUS(AE_BAD_PARAMETER);
			}
			break;

		default:

			ACPI_ERROR((AE_INFO, "Invalid conversion opcode"));
			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		count--;
		info++;
	}

      exit:
	if (!flags_mode) {

		/* Round the resource struct length up to the next boundary (32 or 64) */

		resource->length =
		    (u32) ACPI_ROUND_UP_TO_NATIVE_WORD(resource->length);
	}
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_convert_resource_to_aml
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *              Info                - Pointer to appropriate conversion table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_convert_resource_to_aml(struct acpi_resource *resource,
				union aml_resource *aml,
				struct acpi_rsconvert_info *info)
{
	void *source = NULL;
	void *destination;
	acpi_rsdesc_size aml_length = 0;
	u8 count;
	u16 temp16 = 0;
	u16 item_count = 0;

	ACPI_FUNCTION_TRACE(rs_convert_resource_to_aml);

	/*
	 * First table entry must be ACPI_RSC_INITxxx and must contain the
	 * table length (# of table entries)
	 */
	count = INIT_TABLE_LENGTH(info);

	while (count) {
		/*
		 * Source is the internal resource descriptor,
		 * destination is the external AML byte stream buffer
		 */
		source = ACPI_ADD_PTR(void, resource, info->resource_offset);
		destination = ACPI_ADD_PTR(void, aml, info->aml_offset);

		switch (info->opcode) {
		case ACPI_RSC_INITSET:

			ACPI_MEMSET(aml, 0, INIT_RESOURCE_LENGTH(info));
			aml_length = INIT_RESOURCE_LENGTH(info);
			acpi_rs_set_resource_header(INIT_RESOURCE_TYPE(info),
						    aml_length, aml);
			break;

		case ACPI_RSC_INITGET:
			break;

		case ACPI_RSC_FLAGINIT:
			/*
			 * Clear the flag byte
			 */
			ACPI_SET8(destination) = 0;
			break;

		case ACPI_RSC_1BITFLAG:
			/*
			 * Mask and shift the flag bit
			 */
			ACPI_SET8(destination) |= (u8)
			    ((ACPI_GET8(source) & 0x01) << info->value);
			break;

		case ACPI_RSC_2BITFLAG:
			/*
			 * Mask and shift the flag bits
			 */
			ACPI_SET8(destination) |= (u8)
			    ((ACPI_GET8(source) & 0x03) << info->value);
			break;

		case ACPI_RSC_COUNT:

			item_count = ACPI_GET8(source);
			ACPI_SET8(destination) = (u8) item_count;

			aml_length =
			    (u16) (aml_length +
				   (info->value * (item_count - 1)));
			break;

		case ACPI_RSC_COUNT16:

			item_count = ACPI_GET16(source);
			aml_length = (u16) (aml_length + item_count);
			acpi_rs_set_resource_length(aml_length, aml);
			break;

		case ACPI_RSC_LENGTH:

			acpi_rs_set_resource_length(info->value, aml);
			break;

		case ACPI_RSC_MOVE8:
		case ACPI_RSC_MOVE16:
		case ACPI_RSC_MOVE32:
		case ACPI_RSC_MOVE64:

			if (info->value) {
				item_count = info->value;
			}
			acpi_rs_move_data(destination, source, item_count,
					  info->opcode);
			break;

		case ACPI_RSC_ADDRESS:

			/* Set the Resource Type, General Flags, and Type-Specific Flags */

			acpi_rs_set_address_common(aml, resource);
			break;

		case ACPI_RSC_SOURCEX:
			/*
			 * Optional resource_source (Index and String)
			 */
			aml_length =
			    acpi_rs_set_resource_source(aml, (acpi_rs_length)
							aml_length, source);
			acpi_rs_set_resource_length(aml_length, aml);
			break;

		case ACPI_RSC_SOURCE:
			/*
			 * Optional resource_source (Index and String). This is the more
			 * complicated case used by the Interrupt() macro
			 */
			aml_length =
			    acpi_rs_set_resource_source(aml, info->value,
							source);
			acpi_rs_set_resource_length(aml_length, aml);
			break;

		case ACPI_RSC_BITMASK:
			/*
			 * 8-bit encoded bitmask (DMA macro)
			 */
			ACPI_SET8(destination) = (u8)
			    acpi_rs_encode_bitmask(source,
						   *ACPI_ADD_PTR(u8, resource,
								 info->value));
			break;

		case ACPI_RSC_BITMASK16:
			/*
			 * 16-bit encoded bitmask (IRQ macro)
			 */
			temp16 = acpi_rs_encode_bitmask(source,
							*ACPI_ADD_PTR(u8,
								      resource,
								      info->
								      value));
			ACPI_MOVE_16_TO_16(destination, &temp16);
			break;

		case ACPI_RSC_EXIT_LE:
			/*
			 * Control - Exit conversion if less than or equal
			 */
			if (item_count <= info->value) {
				goto exit;
			}
			break;

		case ACPI_RSC_EXIT_NE:
			/*
			 * Control - Exit conversion if not equal
			 */
			switch (COMPARE_OPCODE(info)) {
			case ACPI_RSC_COMPARE_VALUE:

				if (*ACPI_ADD_PTR(u8, resource,
						  COMPARE_TARGET(info)) !=
				    COMPARE_VALUE(info)) {
					goto exit;
				}
				break;

			default:

				ACPI_ERROR((AE_INFO,
					    "Invalid conversion sub-opcode"));
				return_ACPI_STATUS(AE_BAD_PARAMETER);
			}
			break;

		case ACPI_RSC_EXIT_EQ:
			/*
			 * Control - Exit conversion if equal
			 */
			if (*ACPI_ADD_PTR(u8, resource,
					  COMPARE_TARGET(info)) ==
			    COMPARE_VALUE(info)) {
				goto exit;
			}
			break;

		default:

			ACPI_ERROR((AE_INFO, "Invalid conversion opcode"));
			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		count--;
		info++;
	}

      exit:
	return_ACPI_STATUS(AE_OK);
}

#if 0
/* Previous resource validations */

if (aml->ext_address64.revision_iD != AML_RESOURCE_EXTENDED_ADDRESS_REVISION) {
	return_ACPI_STATUS(AE_SUPPORT);
}

if (resource->data.start_dpf.performance_robustness >= 3) {
	return_ACPI_STATUS(AE_AML_BAD_RESOURCE_VALUE);
}

if (((aml->irq.flags & 0x09) == 0x00) || ((aml->irq.flags & 0x09) == 0x09)) {
	/*
	 * Only [active_high, edge_sensitive] or [active_low, level_sensitive]
	 * polarity/trigger interrupts are allowed (ACPI spec, section
	 * "IRQ Format"), so 0x00 and 0x09 are illegal.
	 */
	ACPI_ERROR((AE_INFO,
		    "Invalid interrupt polarity/trigger in resource list, 0x%X",
		    aml->irq.flags));
	return_ACPI_STATUS(AE_BAD_DATA);
}

resource->data.extended_irq.interrupt_count = temp8;
if (temp8 < 1) {

	/* Must have at least one IRQ */

	return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
}

if (resource->data.dma.transfer == 0x03) {
	ACPI_ERROR((AE_INFO, "Invalid DMA.Transfer preference (3)"));
	return_ACPI_STATUS(AE_BAD_DATA);
}
#endif
