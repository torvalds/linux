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
ACPI_MODULE_NAME("rsirq")

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_irq
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/
acpi_status
acpi_rs_get_irq(union aml_resource *aml,
		u16 aml_resource_length, struct acpi_resource *resource)
{
	u16 temp16 = 0;
	u32 interrupt_count = 0;
	u32 i;
	u32 resource_length;

	ACPI_FUNCTION_TRACE("rs_get_irq");

	/* Get the IRQ mask (bytes 1:2) */

	ACPI_MOVE_16_TO_16(&temp16, &aml->irq.irq_mask);

	/* Decode the IRQ bits (up to 16 possible) */

	for (i = 0; i < 16; i++) {
		if ((temp16 >> i) & 0x01) {
			resource->data.irq.interrupts[interrupt_count] = i;
			interrupt_count++;
		}
	}

	/* Zero interrupts is valid */

	resource_length = 0;
	resource->data.irq.interrupt_count = interrupt_count;
	if (interrupt_count > 0) {
		/* Calculate the structure size based upon the number of interrupts */

		resource_length = (u32) (interrupt_count - 1) * 4;
	}

	/* Get Flags (Byte 3) if it is used */

	if (aml_resource_length == 3) {
		/* Check for HE, LL interrupts */

		switch (aml->irq.flags & 0x09) {
		case 0x01:	/* HE */
			resource->data.irq.triggering = ACPI_EDGE_SENSITIVE;
			resource->data.irq.polarity = ACPI_ACTIVE_HIGH;
			break;

		case 0x08:	/* LL */
			resource->data.irq.triggering = ACPI_LEVEL_SENSITIVE;
			resource->data.irq.polarity = ACPI_ACTIVE_LOW;
			break;

		default:
			/*
			 * Only _LL and _HE polarity/trigger interrupts
			 * are allowed (ACPI spec, section "IRQ Format")
			 * so 0x00 and 0x09 are illegal.
			 */
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid interrupt polarity/trigger in resource list, %X\n",
					  aml->irq.flags));
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		/* Get Sharing flag */

		resource->data.irq.sharable = (aml->irq.flags >> 3) & 0x01;
	} else {
		/*
		 * Default configuration: assume Edge Sensitive, Active High,
		 * Non-Sharable as per the ACPI Specification
		 */
		resource->data.irq.triggering = ACPI_EDGE_SENSITIVE;
		resource->data.irq.polarity = ACPI_ACTIVE_HIGH;
		resource->data.irq.sharable = ACPI_EXCLUSIVE;
	}

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_IRQ;
	resource->length =
	    resource_length + ACPI_SIZEOF_RESOURCE(struct acpi_resource_irq);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_irq
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_irq(struct acpi_resource *resource, union aml_resource *aml)
{
	acpi_size descriptor_length;
	u16 irq_mask;
	u8 i;

	ACPI_FUNCTION_TRACE("rs_set_irq");

	/* Convert interrupt list to 16-bit IRQ bitmask */

	irq_mask = 0;
	for (i = 0; i < resource->data.irq.interrupt_count; i++) {
		irq_mask |= (1 << resource->data.irq.interrupts[i]);
	}

	/* Set the interrupt mask */

	ACPI_MOVE_16_TO_16(&aml->irq.irq_mask, &irq_mask);

	/*
	 * The descriptor field is set based upon whether a third byte is
	 * needed to contain the IRQ Information.
	 */
	if ((resource->data.irq.triggering == ACPI_EDGE_SENSITIVE) &&
	    (resource->data.irq.polarity == ACPI_ACTIVE_HIGH) &&
	    (resource->data.irq.sharable == ACPI_EXCLUSIVE)) {
		/* irq_no_flags() descriptor can be used */

		descriptor_length = sizeof(struct aml_resource_irq_noflags);
	} else {
		/* Irq() descriptor must be used */

		descriptor_length = sizeof(struct aml_resource_irq);

		/* Set the IRQ Info byte */

		aml->irq.flags = (u8)
		    ((resource->data.irq.sharable & 0x01) << 4);

		if (ACPI_LEVEL_SENSITIVE == resource->data.irq.triggering &&
		    ACPI_ACTIVE_LOW == resource->data.irq.polarity) {
			aml->irq.flags |= 0x08;
		} else {
			aml->irq.flags |= 0x01;
		}
	}

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_IRQ, descriptor_length,
				    aml);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_get_ext_irq
 *
 * PARAMETERS:  Aml                 - Pointer to the AML resource descriptor
 *              aml_resource_length - Length of the resource from the AML header
 *              Resource            - Where the internal resource is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert a raw AML resource descriptor to the corresponding
 *              internal resource descriptor, simplifying bitflags and handling
 *              alignment and endian issues if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_rs_get_ext_irq(union aml_resource *aml,
		    u16 aml_resource_length, struct acpi_resource *resource)
{
	char *out_resource_string;
	u8 temp8;

	ACPI_FUNCTION_TRACE("rs_get_ext_irq");

	/* Get the flag bits */

	temp8 = aml->extended_irq.flags;
	resource->data.extended_irq.producer_consumer = temp8 & 0x01;
	resource->data.extended_irq.polarity = (temp8 >> 2) & 0x01;
	resource->data.extended_irq.sharable = (temp8 >> 3) & 0x01;

	/*
	 * Check for Interrupt Mode
	 *
	 * The definition of an Extended IRQ changed between ACPI spec v1.0b
	 * and ACPI spec 2.0 (section 6.4.3.6 in both).
	 *
	 * - Edge/Level are defined opposite in the table vs the headers
	 */
	resource->data.extended_irq.triggering =
	    (temp8 & 0x2) ? ACPI_EDGE_SENSITIVE : ACPI_LEVEL_SENSITIVE;

	/* Get the IRQ Table length (Byte4) */

	temp8 = aml->extended_irq.table_length;
	resource->data.extended_irq.interrupt_count = temp8;
	if (temp8 < 1) {
		/* Must have at least one IRQ */

		return_ACPI_STATUS(AE_AML_BAD_RESOURCE_LENGTH);
	}

	/*
	 * Add any additional structure size to properly calculate
	 * the next pointer at the end of this function
	 */
	resource->length = (temp8 - 1) * 4;
	out_resource_string = ACPI_CAST_PTR(char,
					    (&resource->data.extended_irq.
					     interrupts[0] + temp8));

	/* Get every IRQ in the table, each is 32 bits */

	acpi_rs_move_data(resource->data.extended_irq.interrupts,
			  aml->extended_irq.interrupt_number,
			  (u16) temp8, ACPI_MOVE_TYPE_32_TO_32);

	/* Get the optional resource_source (index and string) */

	resource->length +=
	    acpi_rs_get_resource_source(aml_resource_length,
					(acpi_size) resource->length +
					sizeof(struct
					       aml_resource_extended_irq),
					&resource->data.extended_irq.
					resource_source, aml,
					out_resource_string);

	/* Complete the resource header */

	resource->type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
	resource->length +=
	    ACPI_SIZEOF_RESOURCE(struct acpi_resource_extended_irq);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_rs_set_ext_irq
 *
 * PARAMETERS:  Resource            - Pointer to the resource descriptor
 *              Aml                 - Where the AML descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an internal resource descriptor to the corresponding
 *              external AML resource descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_rs_set_ext_irq(struct acpi_resource *resource, union aml_resource *aml)
{
	acpi_size descriptor_length;

	ACPI_FUNCTION_TRACE("rs_set_ext_irq");

	/* Set the Interrupt vector flags */

	aml->extended_irq.flags = (u8)
	    ((resource->data.extended_irq.producer_consumer & 0x01) |
	     ((resource->data.extended_irq.sharable & 0x01) << 3) |
	     ((resource->data.extended_irq.polarity & 0x1) << 2));

	/*
	 * Set the Interrupt Mode
	 *
	 * The definition of an Extended IRQ changed between ACPI spec v1.0b
	 * and ACPI spec 2.0 (section 6.4.3.6 in both).  This code does not
	 * implement the more restrictive definition of 1.0b
	 *
	 * - Edge/Level are defined opposite in the table vs the headers
	 */
	if (resource->data.extended_irq.triggering == ACPI_EDGE_SENSITIVE) {
		aml->extended_irq.flags |= 0x02;
	}

	/* Set the Interrupt table length */

	aml->extended_irq.table_length = (u8)
	    resource->data.extended_irq.interrupt_count;

	descriptor_length = (sizeof(struct aml_resource_extended_irq) - 4) +
	    ((acpi_size) resource->data.extended_irq.interrupt_count *
	     sizeof(u32));

	/* Set each interrupt value */

	acpi_rs_move_data(aml->extended_irq.interrupt_number,
			  resource->data.extended_irq.interrupts,
			  (u16) resource->data.extended_irq.interrupt_count,
			  ACPI_MOVE_TYPE_32_TO_32);

	/* Resource Source Index and Resource Source are optional */

	descriptor_length = acpi_rs_set_resource_source(aml, descriptor_length,
							&resource->data.
							extended_irq.
							resource_source);

	/* Complete the AML descriptor header */

	acpi_rs_set_resource_header(ACPI_RESOURCE_NAME_EXTENDED_IRQ,
				    descriptor_length, aml);
	return_ACPI_STATUS(AE_OK);
}
