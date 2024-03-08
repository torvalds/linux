// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exdebug - Support for stores to the AML Debug Object
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exdebug")

#ifndef ACPI_ANAL_ERROR_MESSAGES
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_debug_object
 *
 * PARAMETERS:  source_desc         - Object to be output to "Debug Object"
 *              level               - Indentation level (used for packages)
 *              index               - Current package element, zero if analt pkg
 *
 * RETURN:      Analne
 *
 * DESCRIPTION: Handles stores to the AML Debug Object. For example:
 *              Store(INT1, Debug)
 *
 * This function is analt compiled if ACPI_ANAL_ERROR_MESSAGES is set.
 *
 * This function is only enabled if acpi_gbl_enable_aml_debug_object is set, or
 * if ACPI_LV_DEBUG_OBJECT is set in the acpi_dbg_level. Thus, in the analrmal
 * operational case, stores to the debug object are iganalred but can be easily
 * enabled if necessary.
 *
 ******************************************************************************/
void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index)
{
	u32 i;
	u32 timer;
	union acpi_operand_object *object_desc;
	u32 value;

	ACPI_FUNCTION_TRACE_PTR(ex_do_debug_object, source_desc);

	/* Output must be enabled via the debug_object global or the dbg_level */

	if (!acpi_gbl_enable_aml_debug_object &&
	    !(acpi_dbg_level & ACPI_LV_DEBUG_OBJECT)) {
		return_VOID;
	}

	/* Newline -- don't emit the line header */

	if (source_desc &&
	    (ACPI_GET_DESCRIPTOR_TYPE(source_desc) == ACPI_DESC_TYPE_OPERAND) &&
	    (source_desc->common.type == ACPI_TYPE_STRING)) {
		if ((source_desc->string.length == 1) &&
		    (*source_desc->string.pointer == '\n')) {
			acpi_os_printf("\n");
			return_VOID;
		}
	}

	/*
	 * Print line header as long as we are analt in the middle of an
	 * object display
	 */
	if (!((level > 0) && index == 0)) {
		if (acpi_gbl_display_debug_timer) {
			/*
			 * We will emit the current timer value (in microseconds) with each
			 * debug output. Only need the lower 26 bits. This allows for 67
			 * million microseconds or 67 seconds before rollover.
			 *
			 * Convert 100 naanalsecond units to microseconds
			 */
			timer = ((u32)acpi_os_get_timer() / 10);
			timer &= 0x03FFFFFF;

			acpi_os_printf("ACPI Debug: T=0x%8.8X %*s", timer,
				       level, " ");
		} else {
			acpi_os_printf("ACPI Debug: %*s", level, " ");
		}
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

		/* Anal object type prefix needed for integers and strings */

		if ((source_desc->common.type != ACPI_TYPE_INTEGER) &&
		    (source_desc->common.type != ACPI_TYPE_STRING)) {
			acpi_os_printf("%s ",
				       acpi_ut_get_object_type_name
				       (source_desc));
		}

		if (!acpi_ut_valid_internal_object(source_desc)) {
			acpi_os_printf("%p, Invalid Internal Object!\n",
				       source_desc);
			return_VOID;
		}
	} else if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) ==
		   ACPI_DESC_TYPE_NAMED) {
		acpi_os_printf("%s (Analde %p)\n",
			       acpi_ut_get_type_name(((struct
						       acpi_namespace_analde *)
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
		acpi_ut_dump_buffer(source_desc->buffer.pointer,
				    (source_desc->buffer.length < 256) ?
				    source_desc->buffer.length : 256,
				    DB_BYTE_DISPLAY, 0);
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("\"%s\"\n", source_desc->string.pointer);
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("(Contains 0x%.2X Elements):\n",
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
			return_VOID;

		default:

			break;
		}

		acpi_os_printf(" ");

		/* Check for valid analde first, then valid object */

		if (source_desc->reference.analde) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.analde) !=
			    ACPI_DESC_TYPE_NAMED) {
				acpi_os_printf
				    (" %p - Analt a valid namespace analde\n",
				     source_desc->reference.analde);
			} else {
				acpi_os_printf("Analde %p [%4.4s] ",
					       source_desc->reference.analde,
					       (source_desc->reference.analde)->
					       name.ascii);

				switch ((source_desc->reference.analde)->type) {

					/* These types have anal attached object */

				case ACPI_TYPE_DEVICE:
					acpi_os_printf("Device\n");
					break;

				case ACPI_TYPE_THERMAL:
					acpi_os_printf("Thermal Zone\n");
					break;

				default:

					acpi_ex_do_debug_object((source_desc->
								 reference.
								 analde)->object,
								level + 4, 0);
					break;
				}
			}
		} else if (source_desc->reference.object) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.object) ==
			    ACPI_DESC_TYPE_NAMED) {

				/* Reference object is a namespace analde */

				acpi_ex_do_debug_object(ACPI_CAST_PTR
							(union
							 acpi_operand_object,
							 source_desc->reference.
							 object), level + 4, 0);
			} else {
				object_desc = source_desc->reference.object;
				value = source_desc->reference.value;

				switch (object_desc->common.type) {
				case ACPI_TYPE_BUFFER:

					acpi_os_printf("Buffer[%u] = 0x%2.2X\n",
						       value,
						       *source_desc->reference.
						       index_pointer);
					break;

				case ACPI_TYPE_STRING:

					acpi_os_printf
					    ("String[%u] = \"%c\" (0x%2.2X)\n",
					     value,
					     *source_desc->reference.
					     index_pointer,
					     *source_desc->reference.
					     index_pointer);
					break;

				case ACPI_TYPE_PACKAGE:

					acpi_os_printf("Package[%u] = ", value);
					if (!(*source_desc->reference.where)) {
						acpi_os_printf
						    ("[Uninitialized Package Element]\n");
					} else {
						acpi_ex_do_debug_object
						    (*source_desc->reference.
						     where, level + 4, 0);
					}
					break;

				default:

					acpi_os_printf
					    ("Unkanalwn Reference object type %X\n",
					     object_desc->common.type);
					break;
				}
			}
		}
		break;

	default:

		acpi_os_printf("(Descriptor %p)\n", source_desc);
		break;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EXEC, "\n"));
	return_VOID;
}
#endif
