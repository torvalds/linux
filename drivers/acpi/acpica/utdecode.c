/******************************************************************************
 *
 * Module Name: utdecode - Utility decoding routines (value-to-string)
 *
 *****************************************************************************/

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
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utdecode")

/*
 * Properties of the ACPI Object Types, both internal and external.
 * The table is indexed by values of acpi_object_type
 */
const u8 acpi_gbl_ns_properties[ACPI_NUM_NS_TYPES] = {
	ACPI_NS_NORMAL,		/* 00 Any              */
	ACPI_NS_NORMAL,		/* 01 Number           */
	ACPI_NS_NORMAL,		/* 02 String           */
	ACPI_NS_NORMAL,		/* 03 Buffer           */
	ACPI_NS_NORMAL,		/* 04 Package          */
	ACPI_NS_NORMAL,		/* 05 field_unit       */
	ACPI_NS_NEWSCOPE,	/* 06 Device           */
	ACPI_NS_NORMAL,		/* 07 Event            */
	ACPI_NS_NEWSCOPE,	/* 08 Method           */
	ACPI_NS_NORMAL,		/* 09 Mutex            */
	ACPI_NS_NORMAL,		/* 10 Region           */
	ACPI_NS_NEWSCOPE,	/* 11 Power            */
	ACPI_NS_NEWSCOPE,	/* 12 Processor        */
	ACPI_NS_NEWSCOPE,	/* 13 Thermal          */
	ACPI_NS_NORMAL,		/* 14 buffer_field     */
	ACPI_NS_NORMAL,		/* 15 ddb_handle       */
	ACPI_NS_NORMAL,		/* 16 Debug Object     */
	ACPI_NS_NORMAL,		/* 17 def_field        */
	ACPI_NS_NORMAL,		/* 18 bank_field       */
	ACPI_NS_NORMAL,		/* 19 index_field      */
	ACPI_NS_NORMAL,		/* 20 Reference        */
	ACPI_NS_NORMAL,		/* 21 Alias            */
	ACPI_NS_NORMAL,		/* 22 method_alias     */
	ACPI_NS_NORMAL,		/* 23 Notify           */
	ACPI_NS_NORMAL,		/* 24 Address Handler  */
	ACPI_NS_NEWSCOPE | ACPI_NS_LOCAL,	/* 25 Resource Desc    */
	ACPI_NS_NEWSCOPE | ACPI_NS_LOCAL,	/* 26 Resource Field   */
	ACPI_NS_NEWSCOPE,	/* 27 Scope            */
	ACPI_NS_NORMAL,		/* 28 Extra            */
	ACPI_NS_NORMAL,		/* 29 Data             */
	ACPI_NS_NORMAL		/* 30 Invalid          */
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_hex_to_ascii_char
 *
 * PARAMETERS:  integer             - Contains the hex digit
 *              position            - bit position of the digit within the
 *                                    integer (multiple of 4)
 *
 * RETURN:      The converted Ascii character
 *
 * DESCRIPTION: Convert a hex digit to an Ascii character
 *
 ******************************************************************************/

/* Hex to ASCII conversion table */

static const char acpi_gbl_hex_to_ascii[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

char acpi_ut_hex_to_ascii_char(u64 integer, u32 position)
{

	return (acpi_gbl_hex_to_ascii[(integer >> position) & 0xF]);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_region_name
 *
 * PARAMETERS:  Space ID            - ID for the region
 *
 * RETURN:      Decoded region space_id name
 *
 * DESCRIPTION: Translate a Space ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Region type decoding */

const char *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS] = {
	"SystemMemory",		/* 0x00 */
	"SystemIO",		/* 0x01 */
	"PCI_Config",		/* 0x02 */
	"EmbeddedControl",	/* 0x03 */
	"SMBus",		/* 0x04 */
	"SystemCMOS",		/* 0x05 */
	"PCIBARTarget",		/* 0x06 */
	"IPMI",			/* 0x07 */
	"GeneralPurposeIo",	/* 0x08 */
	"GenericSerialBus",	/* 0x09 */
	"PCC"			/* 0x0A */
};

char *acpi_ut_get_region_name(u8 space_id)
{

	if (space_id >= ACPI_USER_REGION_BEGIN) {
		return ("UserDefinedRegion");
	} else if (space_id == ACPI_ADR_SPACE_DATA_TABLE) {
		return ("DataTable");
	} else if (space_id == ACPI_ADR_SPACE_FIXED_HARDWARE) {
		return ("FunctionalFixedHW");
	} else if (space_id >= ACPI_NUM_PREDEFINED_REGIONS) {
		return ("InvalidSpaceId");
	}

	return (ACPI_CAST_PTR(char, acpi_gbl_region_types[space_id]));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_event_name
 *
 * PARAMETERS:  event_id            - Fixed event ID
 *
 * RETURN:      Decoded event ID name
 *
 * DESCRIPTION: Translate a Event ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Event type decoding */

static const char *acpi_gbl_event_types[ACPI_NUM_FIXED_EVENTS] = {
	"PM_Timer",
	"GlobalLock",
	"PowerButton",
	"SleepButton",
	"RealTimeClock",
};

char *acpi_ut_get_event_name(u32 event_id)
{

	if (event_id > ACPI_EVENT_MAX) {
		return ("InvalidEventID");
	}

	return (ACPI_CAST_PTR(char, acpi_gbl_event_types[event_id]));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_type_name
 *
 * PARAMETERS:  type                - An ACPI object type
 *
 * RETURN:      Decoded ACPI object type name
 *
 * DESCRIPTION: Translate a Type ID into a name string (Debug only)
 *
 ******************************************************************************/

/*
 * Elements of acpi_gbl_ns_type_names below must match
 * one-to-one with values of acpi_object_type
 *
 * The type ACPI_TYPE_ANY (Untyped) is used as a "don't care" when searching;
 * when stored in a table it really means that we have thus far seen no
 * evidence to indicate what type is actually going to be stored for this entry.
 */
static const char acpi_gbl_bad_type[] = "UNDEFINED";

/* Printable names of the ACPI object types */

static const char *acpi_gbl_ns_type_names[] = {
	/* 00 */ "Untyped",
	/* 01 */ "Integer",
	/* 02 */ "String",
	/* 03 */ "Buffer",
	/* 04 */ "Package",
	/* 05 */ "FieldUnit",
	/* 06 */ "Device",
	/* 07 */ "Event",
	/* 08 */ "Method",
	/* 09 */ "Mutex",
	/* 10 */ "Region",
	/* 11 */ "Power",
	/* 12 */ "Processor",
	/* 13 */ "Thermal",
	/* 14 */ "BufferField",
	/* 15 */ "DdbHandle",
	/* 16 */ "DebugObject",
	/* 17 */ "RegionField",
	/* 18 */ "BankField",
	/* 19 */ "IndexField",
	/* 20 */ "Reference",
	/* 21 */ "Alias",
	/* 22 */ "MethodAlias",
	/* 23 */ "Notify",
	/* 24 */ "AddrHandler",
	/* 25 */ "ResourceDesc",
	/* 26 */ "ResourceFld",
	/* 27 */ "Scope",
	/* 28 */ "Extra",
	/* 29 */ "Data",
	/* 30 */ "Invalid"
};

char *acpi_ut_get_type_name(acpi_object_type type)
{

	if (type > ACPI_TYPE_INVALID) {
		return (ACPI_CAST_PTR(char, acpi_gbl_bad_type));
	}

	return (ACPI_CAST_PTR(char, acpi_gbl_ns_type_names[type]));
}

char *acpi_ut_get_object_type_name(union acpi_operand_object *obj_desc)
{

	if (!obj_desc) {
		return ("[NULL Object Descriptor]");
	}

	return (acpi_ut_get_type_name(obj_desc->common.type));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_node_name
 *
 * PARAMETERS:  object               - A namespace node
 *
 * RETURN:      ASCII name of the node
 *
 * DESCRIPTION: Validate the node and return the node's ACPI name.
 *
 ******************************************************************************/

char *acpi_ut_get_node_name(void *object)
{
	struct acpi_namespace_node *node = (struct acpi_namespace_node *)object;

	/* Must return a string of exactly 4 characters == ACPI_NAME_SIZE */

	if (!object) {
		return ("NULL");
	}

	/* Check for Root node */

	if ((object == ACPI_ROOT_OBJECT) || (object == acpi_gbl_root_node)) {
		return ("\"\\\" ");
	}

	/* Descriptor must be a namespace node */

	if (ACPI_GET_DESCRIPTOR_TYPE(node) != ACPI_DESC_TYPE_NAMED) {
		return ("####");
	}

	/*
	 * Ensure name is valid. The name was validated/repaired when the node
	 * was created, but make sure it has not been corrupted.
	 */
	acpi_ut_repair_name(node->name.ascii);

	/* Return the name */

	return (node->name.ascii);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_descriptor_name
 *
 * PARAMETERS:  object               - An ACPI object
 *
 * RETURN:      Decoded name of the descriptor type
 *
 * DESCRIPTION: Validate object and return the descriptor type
 *
 ******************************************************************************/

/* Printable names of object descriptor types */

static const char *acpi_gbl_desc_type_names[] = {
	/* 00 */ "Not a Descriptor",
	/* 01 */ "Cached",
	/* 02 */ "State-Generic",
	/* 03 */ "State-Update",
	/* 04 */ "State-Package",
	/* 05 */ "State-Control",
	/* 06 */ "State-RootParseScope",
	/* 07 */ "State-ParseScope",
	/* 08 */ "State-WalkScope",
	/* 09 */ "State-Result",
	/* 10 */ "State-Notify",
	/* 11 */ "State-Thread",
	/* 12 */ "Walk",
	/* 13 */ "Parser",
	/* 14 */ "Operand",
	/* 15 */ "Node"
};

char *acpi_ut_get_descriptor_name(void *object)
{

	if (!object) {
		return ("NULL OBJECT");
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(object) > ACPI_DESC_TYPE_MAX) {
		return ("Not a Descriptor");
	}

	return (ACPI_CAST_PTR(char,
			      acpi_gbl_desc_type_names[ACPI_GET_DESCRIPTOR_TYPE
						       (object)]));

}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_reference_name
 *
 * PARAMETERS:  object               - An ACPI reference object
 *
 * RETURN:      Decoded name of the type of reference
 *
 * DESCRIPTION: Decode a reference object sub-type to a string.
 *
 ******************************************************************************/

/* Printable names of reference object sub-types */

static const char *acpi_gbl_ref_class_names[] = {
	/* 00 */ "Local",
	/* 01 */ "Argument",
	/* 02 */ "RefOf",
	/* 03 */ "Index",
	/* 04 */ "DdbHandle",
	/* 05 */ "Named Object",
	/* 06 */ "Debug"
};

const char *acpi_ut_get_reference_name(union acpi_operand_object *object)
{

	if (!object) {
		return ("NULL Object");
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(object) != ACPI_DESC_TYPE_OPERAND) {
		return ("Not an Operand object");
	}

	if (object->common.type != ACPI_TYPE_LOCAL_REFERENCE) {
		return ("Not a Reference object");
	}

	if (object->reference.class > ACPI_REFCLASS_MAX) {
		return ("Unknown Reference class");
	}

	return (acpi_gbl_ref_class_names[object->reference.class]);
}

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/*
 * Strings and procedures used for debug only
 */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_mutex_name
 *
 * PARAMETERS:  mutex_id        - The predefined ID for this mutex.
 *
 * RETURN:      Decoded name of the internal mutex
 *
 * DESCRIPTION: Translate a mutex ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Names for internal mutex objects, used for debug output */

static char *acpi_gbl_mutex_names[ACPI_NUM_MUTEX] = {
	"ACPI_MTX_Interpreter",
	"ACPI_MTX_Namespace",
	"ACPI_MTX_Tables",
	"ACPI_MTX_Events",
	"ACPI_MTX_Caches",
	"ACPI_MTX_Memory",
	"ACPI_MTX_CommandComplete",
	"ACPI_MTX_CommandReady"
};

char *acpi_ut_get_mutex_name(u32 mutex_id)
{

	if (mutex_id > ACPI_MAX_MUTEX) {
		return ("Invalid Mutex ID");
	}

	return (acpi_gbl_mutex_names[mutex_id]);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_notify_name
 *
 * PARAMETERS:  notify_value    - Value from the Notify() request
 *
 * RETURN:      Decoded name for the notify value
 *
 * DESCRIPTION: Translate a Notify Value to a notify namestring.
 *
 ******************************************************************************/

/* Names for Notify() values, used for debug output */

static const char *acpi_gbl_notify_value_names[ACPI_NOTIFY_MAX + 1] = {
	/* 00 */ "Bus Check",
	/* 01 */ "Device Check",
	/* 02 */ "Device Wake",
	/* 03 */ "Eject Request",
	/* 04 */ "Device Check Light",
	/* 05 */ "Frequency Mismatch",
	/* 06 */ "Bus Mode Mismatch",
	/* 07 */ "Power Fault",
	/* 08 */ "Capabilities Check",
	/* 09 */ "Device PLD Check",
	/* 10 */ "Reserved",
	/* 11 */ "System Locality Update",
	/* 12 */ "Shutdown Request"
};

const char *acpi_ut_get_notify_name(u32 notify_value)
{

	if (notify_value <= ACPI_NOTIFY_MAX) {
		return (acpi_gbl_notify_value_names[notify_value]);
	} else if (notify_value <= ACPI_MAX_SYS_NOTIFY) {
		return ("Reserved");
	} else if (notify_value <= ACPI_MAX_DEVICE_SPECIFIC_NOTIFY) {
		return ("Device Specific");
	} else {
		return ("Hardware Specific");
	}
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_object_type
 *
 * PARAMETERS:  type            - Object type to be validated
 *
 * RETURN:      TRUE if valid object type, FALSE otherwise
 *
 * DESCRIPTION: Validate an object type
 *
 ******************************************************************************/

u8 acpi_ut_valid_object_type(acpi_object_type type)
{

	if (type > ACPI_TYPE_LOCAL_MAX) {

		/* Note: Assumes all TYPEs are contiguous (external/local) */

		return (FALSE);
	}

	return (TRUE);
}
