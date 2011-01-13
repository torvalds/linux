/******************************************************************************
 *
 * Module Name: exdebug - Support for stores to the AML Debug Object
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exdebug")

#ifndef ACPI_NO_ERROR_MESSAGES
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_debug_object
 *
 * PARAMETERS:  source_desc         - Object to be output to "Debug Object"
 *              Level               - Indentation level (used for packages)
 *              Index               - Current package element, zero if not pkg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Handles stores to the AML Debug Object. For example:
 *              Store(INT1, Debug)
 *
 * This function is not compiled if ACPI_NO_ERROR_MESSAGES is set.
 *
 * This function is only enabled if acpi_gbl_enable_aml_debug_object is set, or
 * if ACPI_LV_DEBUG_OBJECT is set in the acpi_dbg_level. Thus, in the normal
 * operational case, stores to the debug object are ignored but can be easily
 * enabled if necessary.
 *
 ******************************************************************************/
void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index)
{
	u32 i;

	ACPI_FUNCTION_TRACE_PTR(ex_do_debug_object, source_desc);

	/* Output must be enabled via the debug_object global or the dbg_level */

	if (!acpi_gbl_enable_aml_debug_object &&
	    !(acpi_dbg_level & ACPI_LV_DEBUG_OBJECT)) {
		return_VOID;
	}

	/*
	 * Print line header as long as we are not in the middle of an
	 * object display
	 */
	if (!((level > 0) && index == 0)) {
		acpi_os_printf("[ACPI Debug] %*s", level, " ");
	}

	/* Display the index for package output only */

	if (index > 0) {
		acpi_os_printf("(%.2u) ", index - 1);
	}

	if (!source_desc) {
		acpi_os_printf("[Null Object]\n");
		return_VOID;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) == ACPI_DESC_TYPE_OPERAND) {
		acpi_os_printf("%s ",
			       acpi_ut_get_object_type_name(source_desc));

		if (!acpi_ut_valid_internal_object(source_desc)) {
			acpi_os_printf("%p, Invalid Internal Object!\n",
				       source_desc);
			return_VOID;
		}
	} else if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) ==
		   ACPI_DESC_TYPE_NAMED) {
		acpi_os_printf("%s: %p\n",
			       acpi_ut_get_type_name(((struct
						       acpi_namespace_node *)
						      source_desc)->type),
			       source_desc);
		return_VOID;
	} else {
		return_VOID;
	}

	/* source_desc is of type ACPI_DESC_TYPE_OPERAND */

	switch (source_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/* Output correct integer width */

		if (acpi_gbl_integer_byte_width == 4) {
			acpi_os_printf("0x%8.8X\n",
				       (u32)source_desc->integer.value);
		} else {
			acpi_os_printf("0x%8.8X%8.8X\n",
				       ACPI_FORMAT_UINT64(source_desc->integer.
							  value));
		}
		break;

	case ACPI_TYPE_BUFFER:

		acpi_os_printf("[0x%.2X]\n", (u32)source_desc->buffer.length);
		acpi_ut_dump_buffer2(source_desc->buffer.pointer,
				     (source_desc->buffer.length < 256) ?
				     source_desc->buffer.length : 256,
				     DB_BYTE_DISPLAY);
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("[0x%.2X] \"%s\"\n",
			       source_desc->string.length,
			       source_desc->string.pointer);
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("[Contains 0x%.2X Elements]\n",
			       source_desc->package.count);

		/* Output the entire contents of the package */

		for (i = 0; i < source_desc->package.count; i++) {
			acpi_ex_do_debug_object(source_desc->package.
						elements[i], level + 4, i + 1);
		}
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_os_printf("[%s] ",
			       acpi_ut_get_reference_name(source_desc));

		/* Decode the reference */

		switch (source_desc->reference.class) {
		case ACPI_REFCLASS_INDEX:

			acpi_os_printf("0x%X\n", source_desc->reference.value);
			break;

		case ACPI_REFCLASS_TABLE:

			/* Case for ddb_handle */

			acpi_os_printf("Table Index 0x%X\n",
				       source_desc->reference.value);
			return;

		default:
			break;
		}

		acpi_os_printf(" ");

		/* Check for valid node first, then valid object */

		if (source_desc->reference.node) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.node) !=
			    ACPI_DESC_TYPE_NAMED) {
				acpi_os_printf
				    (" %p - Not a valid namespace node\n",
				     source_desc->reference.node);
			} else {
				acpi_os_printf("Node %p [%4.4s] ",
					       source_desc->reference.node,
					       (source_desc->reference.node)->
					       name.ascii);

				switch ((source_desc->reference.node)->type) {

					/* These types have no attached object */

				case ACPI_TYPE_DEVICE:
					acpi_os_printf("Device\n");
					break;

				case ACPI_TYPE_THERMAL:
					acpi_os_printf("Thermal Zone\n");
					break;

				default:
					acpi_ex_do_debug_object((source_desc->
								 reference.
								 node)->object,
								level + 4, 0);
					break;
				}
			}
		} else if (source_desc->reference.object) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.object) ==
			    ACPI_DESC_TYPE_NAMED) {
				acpi_ex_do_debug_object(((struct
							  acpi_namespace_node *)
							 source_desc->reference.
							 object)->object,
							level + 4, 0);
			} else {
				acpi_ex_do_debug_object(source_desc->reference.
							object, level + 4, 0);
			}
		}
		break;

	default:

		acpi_os_printf("%p\n", source_desc);
		break;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EXEC, "\n"));
	return_VOID;
}
#endif
