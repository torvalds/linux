/*******************************************************************************
 *
 * Module Name: rsirq - IRQ resource descriptors
 *
 ******************************************************************************/

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
#include <acpi/acresrc.h>

#define _COMPONENT          ACPI_RESOURCES
	 ACPI_MODULE_NAME    ("rsirq")


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_irq_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_irq_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size)
{
	u8                              *buffer = byte_stream_buffer;
	struct acpi_resource            *output_struct = (void *) *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	u8                              index;
	u8                              i;
	acpi_size                       struct_size = ACPI_SIZEOF_RESOURCE (struct acpi_resource_irq);


	ACPI_FUNCTION_TRACE ("rs_irq_resource");


	/*
	 * The number of bytes consumed are contained in the descriptor
	 *  (Bits:0-1)
	 */
	temp8 = *buffer;
	*bytes_consumed = (temp8 & 0x03) + 1;
	output_struct->id = ACPI_RSTYPE_IRQ;

	/*
	 * Point to the 16-bits of Bytes 1 and 2
	 */
	buffer += 1;
	ACPI_MOVE_16_TO_16 (&temp16, buffer);

	output_struct->data.irq.number_of_interrupts = 0;

	/* Decode the IRQ bits */

	for (i = 0, index = 0; index < 16; index++) {
		if ((temp16 >> index) & 0x01) {
			output_struct->data.irq.interrupts[i] = index;
			i++;
		}
	}

	/* Zero interrupts is valid */

	output_struct->data.irq.number_of_interrupts = i;
	if (i > 0) {
		/*
		 * Calculate the structure size based upon the number of interrupts
		 */
		struct_size += ((acpi_size) i - 1) * 4;
	}

	/*
	 * Point to Byte 3 if it is used
	 */
	if (4 == *bytes_consumed) {
		buffer += 2;
		temp8 = *buffer;

		/*
		 * Check for HE, LL interrupts
		 */
		switch (temp8 & 0x09) {
		case 0x01: /* HE */
			output_struct->data.irq.edge_level = ACPI_EDGE_SENSITIVE;
			output_struct->data.irq.active_high_low = ACPI_ACTIVE_HIGH;
			break;

		case 0x08: /* LL */
			output_struct->data.irq.edge_level = ACPI_LEVEL_SENSITIVE;
			output_struct->data.irq.active_high_low = ACPI_ACTIVE_LOW;
			break;

		default:
			/*
			 * Only _LL and _HE polarity/trigger interrupts
			 * are allowed (ACPI spec, section "IRQ Format")
			 * so 0x00 and 0x09 are illegal.
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Invalid interrupt polarity/trigger in resource list, %X\n", temp8));
			return_ACPI_STATUS (AE_BAD_DATA);
		}

		/*
		 * Check for sharable
		 */
		output_struct->data.irq.shared_exclusive = (temp8 >> 3) & 0x01;
	}
	else {
		/*
		 * Assume Edge Sensitive, Active High, Non-Sharable
		 * per ACPI Specification
		 */
		output_struct->data.irq.edge_level = ACPI_EDGE_SENSITIVE;
		output_struct->data.irq.active_high_low = ACPI_ACTIVE_HIGH;
		output_struct->data.irq.shared_exclusive = ACPI_EXCLUSIVE;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_irq_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_irq_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed)
{
	u8                              *buffer = *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	u8                              index;
	u8                              IRqinfo_byte_needed;


	ACPI_FUNCTION_TRACE ("rs_irq_stream");


	/*
	 * The descriptor field is set based upon whether a third byte is
	 * needed to contain the IRQ Information.
	 */
	if (ACPI_EDGE_SENSITIVE == linked_list->data.irq.edge_level &&
		ACPI_ACTIVE_HIGH == linked_list->data.irq.active_high_low &&
		ACPI_EXCLUSIVE == linked_list->data.irq.shared_exclusive) {
		*buffer = 0x22;
		IRqinfo_byte_needed = FALSE;
	}
	else {
		*buffer = 0x23;
		IRqinfo_byte_needed = TRUE;
	}

	buffer += 1;
	temp16 = 0;

	/*
	 * Loop through all of the interrupts and set the mask bits
	 */
	for(index = 0;
		index < linked_list->data.irq.number_of_interrupts;
		index++) {
		temp8 = (u8) linked_list->data.irq.interrupts[index];
		temp16 |= 0x1 << temp8;
	}

	ACPI_MOVE_16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the IRQ Info byte if needed.
	 */
	if (IRqinfo_byte_needed) {
		temp8 = 0;
		temp8 = (u8) ((linked_list->data.irq.shared_exclusive &
				 0x01) << 4);

		if (ACPI_LEVEL_SENSITIVE == linked_list->data.irq.edge_level &&
			ACPI_ACTIVE_LOW == linked_list->data.irq.active_high_low) {
			temp8 |= 0x08;
		}
		else {
			temp8 |= 0x01;
		}

		*buffer = temp8;
		buffer += 1;
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_extended_irq_resource
 *
 * PARAMETERS:  byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the byte_stream_buffer is
 *                                        returned
 *              output_buffer           - Pointer to the return data buffer
 *              structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_extended_irq_resource (
	u8                              *byte_stream_buffer,
	acpi_size                       *bytes_consumed,
	u8                              **output_buffer,
	acpi_size                       *structure_size)
{
	u8                              *buffer = byte_stream_buffer;
	struct acpi_resource            *output_struct = (void *) *output_buffer;
	u16                             temp16 = 0;
	u8                              temp8 = 0;
	u8                              *temp_ptr;
	u8                              index;
	acpi_size                       struct_size = ACPI_SIZEOF_RESOURCE (struct acpi_resource_ext_irq);


	ACPI_FUNCTION_TRACE ("rs_extended_irq_resource");


	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;
	ACPI_MOVE_16_TO_16 (&temp16, buffer);

	/* Validate minimum descriptor length */

	if (temp16 < 6) {
		return_ACPI_STATUS (AE_AML_BAD_RESOURCE_LENGTH);
	}

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_EXT_IRQ;

	/*
	 * Point to the Byte3
	 */
	buffer += 2;
	temp8 = *buffer;

	output_struct->data.extended_irq.producer_consumer = temp8 & 0x01;

	/*
	 * Check for Interrupt Mode
	 *
	 * The definition of an Extended IRQ changed between ACPI spec v1.0b
	 * and ACPI spec 2.0 (section 6.4.3.6 in both).
	 *
	 * - Edge/Level are defined opposite in the table vs the headers
	 */
	output_struct->data.extended_irq.edge_level =
			   (temp8 & 0x2) ? ACPI_EDGE_SENSITIVE : ACPI_LEVEL_SENSITIVE;

	/*
	 * Check Interrupt Polarity
	 */
	output_struct->data.extended_irq.active_high_low = (temp8 >> 2) & 0x1;

	/*
	 * Check for sharable
	 */
	output_struct->data.extended_irq.shared_exclusive = (temp8 >> 3) & 0x01;

	/*
	 * Point to Byte4 (IRQ Table length)
	 */
	buffer += 1;
	temp8 = *buffer;

	/* Must have at least one IRQ */

	if (temp8 < 1) {
		return_ACPI_STATUS (AE_AML_BAD_RESOURCE_LENGTH);
	}

	output_struct->data.extended_irq.number_of_interrupts = temp8;

	/*
	 * Add any additional structure size to properly calculate
	 * the next pointer at the end of this function
	 */
	struct_size += (temp8 - 1) * 4;

	/*
	 * Point to Byte5 (First IRQ Number)
	 */
	buffer += 1;

	/*
	 * Cycle through every IRQ in the table
	 */
	for (index = 0; index < temp8; index++) {
		ACPI_MOVE_32_TO_32 (
			&output_struct->data.extended_irq.interrupts[index], buffer);

		/* Point to the next IRQ */

		buffer += 4;
	}

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 * Each Interrupt takes 32-bits + the 5 bytes of the
	 * stream that are default.
	 *
	 * Note: Some resource descriptors will have an additional null, so
	 * we add 1 to the length.
	 */
	if (*bytes_consumed >
		((acpi_size) output_struct->data.extended_irq.number_of_interrupts * 4) + (5 + 1)) {
		/* Dereference the Index */

		temp8 = *buffer;
		output_struct->data.extended_irq.resource_source.index = (u32) temp8;

		/* Point to the String */

		buffer += 1;

		/*
		 * Point the String pointer to the end of this structure.
		 */
		output_struct->data.extended_irq.resource_source.string_ptr =
				(char *)((char *) output_struct + struct_size);

		temp_ptr = (u8 *) output_struct->data.extended_irq.resource_source.string_ptr;

		/* Copy the string into the buffer */

		index = 0;
		while (0x00 != *buffer) {
			*temp_ptr = *buffer;

			temp_ptr += 1;
			buffer += 1;
			index += 1;
		}

		/*
		 * Add the terminating null
		 */
		*temp_ptr = 0x00;
		output_struct->data.extended_irq.resource_source.string_length = index + 1;

		/*
		 * In order for the struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ACPI_ROUND_UP_to_32_bITS (temp8);
	}
	else {
		output_struct->data.extended_irq.resource_source.index = 0x00;
		output_struct->data.extended_irq.resource_source.string_length = 0;
		output_struct->data.extended_irq.resource_source.string_ptr = NULL;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_extended_irq_stream
 *
 * PARAMETERS:  linked_list             - Pointer to the resource linked list
 *              output_buffer           - Pointer to the user's return buffer
 *              bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_extended_irq_stream (
	struct acpi_resource            *linked_list,
	u8                              **output_buffer,
	acpi_size                       *bytes_consumed)
{
	u8                              *buffer = *output_buffer;
	u16                             *length_field;
	u8                              temp8 = 0;
	u8                              index;
	char                            *temp_pointer = NULL;


	ACPI_FUNCTION_TRACE ("rs_extended_irq_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x89;
	buffer += 1;

	/*
	 * Set a pointer to the Length field - to be filled in later
	 */
	length_field = ACPI_CAST_PTR (u16, buffer);
	buffer += 2;

	/*
	 * Set the Interrupt vector flags
	 */
	temp8 = (u8)(linked_list->data.extended_irq.producer_consumer & 0x01);
	temp8 |= ((linked_list->data.extended_irq.shared_exclusive & 0x01) << 3);

	/*
	 * Set the Interrupt Mode
	 *
	 * The definition of an Extended IRQ changed between ACPI spec v1.0b
	 * and ACPI spec 2.0 (section 6.4.3.6 in both).  This code does not
	 * implement the more restrictive definition of 1.0b
	 *
	 * - Edge/Level are defined opposite in the table vs the headers
	 */
	if (ACPI_EDGE_SENSITIVE == linked_list->data.extended_irq.edge_level) {
		temp8 |= 0x2;
	}

	/*
	 * Set the Interrupt Polarity
	 */
	temp8 |= ((linked_list->data.extended_irq.active_high_low & 0x1) << 2);

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the Interrupt table length
	 */
	temp8 = (u8) linked_list->data.extended_irq.number_of_interrupts;

	*buffer = temp8;
	buffer += 1;

	for (index = 0; index < linked_list->data.extended_irq.number_of_interrupts;
		 index++) {
		ACPI_MOVE_32_TO_32 (buffer,
				  &linked_list->data.extended_irq.interrupts[index]);
		buffer += 4;
	}

	/*
	 * Resource Source Index and Resource Source are optional
	 */
	if (0 != linked_list->data.extended_irq.resource_source.string_length) {
		*buffer = (u8) linked_list->data.extended_irq.resource_source.index;
		buffer += 1;

		temp_pointer = (char *) buffer;

		/*
		 * Copy the string
		 */
		ACPI_STRCPY (temp_pointer,
			linked_list->data.extended_irq.resource_source.string_ptr);

		/*
		 * Buffer needs to be set to the length of the sting + one for the
		 * terminating null
		 */
		buffer += (acpi_size)(ACPI_STRLEN (linked_list->data.extended_irq.resource_source.string_ptr) + 1);
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	*length_field = (u16) (*bytes_consumed - 3);
	return_ACPI_STATUS (AE_OK);
}

