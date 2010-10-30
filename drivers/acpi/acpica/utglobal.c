/******************************************************************************
 *
 * Module Name: utglobal - Global variables for the ACPI subsystem
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

#define DEFINE_ACPI_GLOBALS

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utglobal")

/*******************************************************************************
 *
 * Static global variable initialization.
 *
 ******************************************************************************/
/*
 * We want the debug switches statically initialized so they
 * are already set when the debugger is entered.
 */
/* Debug switch - level and trace mask */
u32 acpi_dbg_level = ACPI_DEBUG_DEFAULT;

/* Debug switch - layer (component) mask */

u32 acpi_dbg_layer = 0;
u32 acpi_gbl_nesting_level = 0;

/* Debugger globals */

u8 acpi_gbl_db_terminate_threads = FALSE;
u8 acpi_gbl_abort_method = FALSE;
u8 acpi_gbl_method_executing = FALSE;

/* System flags */

u32 acpi_gbl_startup_flags = 0;

/* System starts uninitialized */

u8 acpi_gbl_shutdown = TRUE;

const char *acpi_gbl_sleep_state_names[ACPI_S_STATE_COUNT] = {
	"\\_S0_",
	"\\_S1_",
	"\\_S2_",
	"\\_S3_",
	"\\_S4_",
	"\\_S5_"
};

const char *acpi_gbl_lowest_dstate_names[ACPI_NUM_sx_w_METHODS] = {
	"_S0W",
	"_S1W",
	"_S2W",
	"_S3W",
	"_S4W"
};

const char *acpi_gbl_highest_dstate_names[ACPI_NUM_sx_d_METHODS] = {
	"_S1D",
	"_S2D",
	"_S3D",
	"_S4D"
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_format_exception
 *
 * PARAMETERS:  Status       - The acpi_status code to be formatted
 *
 * RETURN:      A string containing the exception text. A valid pointer is
 *              always returned.
 *
 * DESCRIPTION: This function translates an ACPI exception into an ASCII string
 *              It is here instead of utxface.c so it is always present.
 *
 ******************************************************************************/

const char *acpi_format_exception(acpi_status status)
{
	const char *exception = NULL;

	ACPI_FUNCTION_ENTRY();

	exception = acpi_ut_validate_exception(status);
	if (!exception) {

		/* Exception code was not recognized */

		ACPI_ERROR((AE_INFO,
			    "Unknown exception code: 0x%8.8X", status));

		exception = "UNKNOWN_STATUS_CODE";
		dump_stack();
	}

	return (ACPI_CAST_PTR(const char, exception));
}

ACPI_EXPORT_SYMBOL(acpi_format_exception)

/*******************************************************************************
 *
 * Namespace globals
 *
 ******************************************************************************/
/*
 * Predefined ACPI Names (Built-in to the Interpreter)
 *
 * NOTES:
 * 1) _SB_ is defined to be a device to allow \_SB_._INI to be run
 *    during the initialization sequence.
 * 2) _TZ_ is defined to be a thermal zone in order to allow ASL code to
 *    perform a Notify() operation on it. 09/2010: Changed to type Device.
 *    This still allows notifies, but does not confuse host code that
 *    searches for valid thermal_zone objects.
 */
const struct acpi_predefined_names acpi_gbl_pre_defined_names[] = {
	{"_GPE", ACPI_TYPE_LOCAL_SCOPE, NULL},
	{"_PR_", ACPI_TYPE_LOCAL_SCOPE, NULL},
	{"_SB_", ACPI_TYPE_DEVICE, NULL},
	{"_SI_", ACPI_TYPE_LOCAL_SCOPE, NULL},
	{"_TZ_", ACPI_TYPE_DEVICE, NULL},
	{"_REV", ACPI_TYPE_INTEGER, (char *)ACPI_CA_SUPPORT_LEVEL},
	{"_OS_", ACPI_TYPE_STRING, ACPI_OS_NAME},
	{"_GL_", ACPI_TYPE_MUTEX, (char *)1},

#if !defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)
	{"_OSI", ACPI_TYPE_METHOD, (char *)1},
#endif

	/* Table terminator */

	{NULL, ACPI_TYPE_ANY, NULL}
};

/*
 * Properties of the ACPI Object Types, both internal and external.
 * The table is indexed by values of acpi_object_type
 */
const u8 acpi_gbl_ns_properties[] = {
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

/* Hex to ASCII conversion table */

static const char acpi_gbl_hex_to_ascii[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_hex_to_ascii_char
 *
 * PARAMETERS:  Integer             - Contains the hex digit
 *              Position            - bit position of the digit within the
 *                                    integer (multiple of 4)
 *
 * RETURN:      The converted Ascii character
 *
 * DESCRIPTION: Convert a hex digit to an Ascii character
 *
 ******************************************************************************/

char acpi_ut_hex_to_ascii_char(u64 integer, u32 position)
{

	return (acpi_gbl_hex_to_ascii[(integer >> position) & 0xF]);
}

/******************************************************************************
 *
 * Event and Hardware globals
 *
 ******************************************************************************/

struct acpi_bit_register_info acpi_gbl_bit_register_info[ACPI_NUM_BITREG] = {
	/* Name                                     Parent Register             Register Bit Position                   Register Bit Mask       */

	/* ACPI_BITREG_TIMER_STATUS         */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_TIMER_STATUS,
						ACPI_BITMASK_TIMER_STATUS},
	/* ACPI_BITREG_BUS_MASTER_STATUS    */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_BUS_MASTER_STATUS,
						ACPI_BITMASK_BUS_MASTER_STATUS},
	/* ACPI_BITREG_GLOBAL_LOCK_STATUS   */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_GLOBAL_LOCK_STATUS,
						ACPI_BITMASK_GLOBAL_LOCK_STATUS},
	/* ACPI_BITREG_POWER_BUTTON_STATUS  */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_POWER_BUTTON_STATUS,
						ACPI_BITMASK_POWER_BUTTON_STATUS},
	/* ACPI_BITREG_SLEEP_BUTTON_STATUS  */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_SLEEP_BUTTON_STATUS,
						ACPI_BITMASK_SLEEP_BUTTON_STATUS},
	/* ACPI_BITREG_RT_CLOCK_STATUS      */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_RT_CLOCK_STATUS,
						ACPI_BITMASK_RT_CLOCK_STATUS},
	/* ACPI_BITREG_WAKE_STATUS          */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_WAKE_STATUS,
						ACPI_BITMASK_WAKE_STATUS},
	/* ACPI_BITREG_PCIEXP_WAKE_STATUS   */ {ACPI_REGISTER_PM1_STATUS,
						ACPI_BITPOSITION_PCIEXP_WAKE_STATUS,
						ACPI_BITMASK_PCIEXP_WAKE_STATUS},

	/* ACPI_BITREG_TIMER_ENABLE         */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_TIMER_ENABLE,
						ACPI_BITMASK_TIMER_ENABLE},
	/* ACPI_BITREG_GLOBAL_LOCK_ENABLE   */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_GLOBAL_LOCK_ENABLE,
						ACPI_BITMASK_GLOBAL_LOCK_ENABLE},
	/* ACPI_BITREG_POWER_BUTTON_ENABLE  */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_POWER_BUTTON_ENABLE,
						ACPI_BITMASK_POWER_BUTTON_ENABLE},
	/* ACPI_BITREG_SLEEP_BUTTON_ENABLE  */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_SLEEP_BUTTON_ENABLE,
						ACPI_BITMASK_SLEEP_BUTTON_ENABLE},
	/* ACPI_BITREG_RT_CLOCK_ENABLE      */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_RT_CLOCK_ENABLE,
						ACPI_BITMASK_RT_CLOCK_ENABLE},
	/* ACPI_BITREG_PCIEXP_WAKE_DISABLE  */ {ACPI_REGISTER_PM1_ENABLE,
						ACPI_BITPOSITION_PCIEXP_WAKE_DISABLE,
						ACPI_BITMASK_PCIEXP_WAKE_DISABLE},

	/* ACPI_BITREG_SCI_ENABLE           */ {ACPI_REGISTER_PM1_CONTROL,
						ACPI_BITPOSITION_SCI_ENABLE,
						ACPI_BITMASK_SCI_ENABLE},
	/* ACPI_BITREG_BUS_MASTER_RLD       */ {ACPI_REGISTER_PM1_CONTROL,
						ACPI_BITPOSITION_BUS_MASTER_RLD,
						ACPI_BITMASK_BUS_MASTER_RLD},
	/* ACPI_BITREG_GLOBAL_LOCK_RELEASE  */ {ACPI_REGISTER_PM1_CONTROL,
						ACPI_BITPOSITION_GLOBAL_LOCK_RELEASE,
						ACPI_BITMASK_GLOBAL_LOCK_RELEASE},
	/* ACPI_BITREG_SLEEP_TYPE           */ {ACPI_REGISTER_PM1_CONTROL,
						ACPI_BITPOSITION_SLEEP_TYPE,
						ACPI_BITMASK_SLEEP_TYPE},
	/* ACPI_BITREG_SLEEP_ENABLE         */ {ACPI_REGISTER_PM1_CONTROL,
						ACPI_BITPOSITION_SLEEP_ENABLE,
						ACPI_BITMASK_SLEEP_ENABLE},

	/* ACPI_BITREG_ARB_DIS              */ {ACPI_REGISTER_PM2_CONTROL,
						ACPI_BITPOSITION_ARB_DISABLE,
						ACPI_BITMASK_ARB_DISABLE}
};

struct acpi_fixed_event_info acpi_gbl_fixed_event_info[ACPI_NUM_FIXED_EVENTS] = {
	/* ACPI_EVENT_PMTIMER       */ {ACPI_BITREG_TIMER_STATUS,
					ACPI_BITREG_TIMER_ENABLE,
					ACPI_BITMASK_TIMER_STATUS,
					ACPI_BITMASK_TIMER_ENABLE},
	/* ACPI_EVENT_GLOBAL        */ {ACPI_BITREG_GLOBAL_LOCK_STATUS,
					ACPI_BITREG_GLOBAL_LOCK_ENABLE,
					ACPI_BITMASK_GLOBAL_LOCK_STATUS,
					ACPI_BITMASK_GLOBAL_LOCK_ENABLE},
	/* ACPI_EVENT_POWER_BUTTON  */ {ACPI_BITREG_POWER_BUTTON_STATUS,
					ACPI_BITREG_POWER_BUTTON_ENABLE,
					ACPI_BITMASK_POWER_BUTTON_STATUS,
					ACPI_BITMASK_POWER_BUTTON_ENABLE},
	/* ACPI_EVENT_SLEEP_BUTTON  */ {ACPI_BITREG_SLEEP_BUTTON_STATUS,
					ACPI_BITREG_SLEEP_BUTTON_ENABLE,
					ACPI_BITMASK_SLEEP_BUTTON_STATUS,
					ACPI_BITMASK_SLEEP_BUTTON_ENABLE},
	/* ACPI_EVENT_RTC           */ {ACPI_BITREG_RT_CLOCK_STATUS,
					ACPI_BITREG_RT_CLOCK_ENABLE,
					ACPI_BITMASK_RT_CLOCK_STATUS,
					ACPI_BITMASK_RT_CLOCK_ENABLE},
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_region_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a Space ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Region type decoding */

const char *acpi_gbl_region_types[ACPI_NUM_PREDEFINED_REGIONS] = {
	"SystemMemory",
	"SystemIO",
	"PCI_Config",
	"EmbeddedControl",
	"SMBus",
	"SystemCMOS",
	"PCIBARTarget",
	"IPMI",
	"DataTable"
};

char *acpi_ut_get_region_name(u8 space_id)
{

	if (space_id >= ACPI_USER_REGION_BEGIN) {
		return ("UserDefinedRegion");
	} else if (space_id >= ACPI_NUM_PREDEFINED_REGIONS) {
		return ("InvalidSpaceId");
	}

	return (ACPI_CAST_PTR(char, acpi_gbl_region_types[space_id]));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_event_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
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
 * PARAMETERS:  None.
 *
 * RETURN:      Status
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
 * PARAMETERS:  Object               - A namespace node
 *
 * RETURN:      Pointer to a string
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

	/* Name must be a valid ACPI name */

	if (!acpi_ut_valid_acpi_name(node->name.integer)) {
		node->name.integer = acpi_ut_repair_name(node->name.ascii);
	}

	/* Return the name */

	return (node->name.ascii);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_descriptor_name
 *
 * PARAMETERS:  Object               - An ACPI object
 *
 * RETURN:      Pointer to a string
 *
 * DESCRIPTION: Validate object and return the descriptor type
 *
 ******************************************************************************/

/* Printable names of object descriptor types */

static const char *acpi_gbl_desc_type_names[] = {
	/* 00 */ "Invalid",
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
		return (ACPI_CAST_PTR(char, acpi_gbl_bad_type));
	}

	return (ACPI_CAST_PTR(char,
			      acpi_gbl_desc_type_names[ACPI_GET_DESCRIPTOR_TYPE
						       (object)]));

}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_reference_name
 *
 * PARAMETERS:  Object               - An ACPI reference object
 *
 * RETURN:      Pointer to a string
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
	if (!object)
		return "NULL Object";

	if (ACPI_GET_DESCRIPTOR_TYPE(object) != ACPI_DESC_TYPE_OPERAND)
		return "Not an Operand object";

	if (object->common.type != ACPI_TYPE_LOCAL_REFERENCE)
		return "Not a Reference object";

	if (object->reference.class > ACPI_REFCLASS_MAX)
		return "Unknown Reference class";

	return acpi_gbl_ref_class_names[object->reference.class];
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
 * RETURN:      String containing the name of the mutex. Always returns a valid
 *              pointer.
 *
 * DESCRIPTION: Translate a mutex ID into a name string (Debug only)
 *
 ******************************************************************************/

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
 * RETURN:      String corresponding to the Notify Value.
 *
 * DESCRIPTION: Translate a Notify Value to a notify namestring.
 *
 ******************************************************************************/

/* Names for Notify() values, used for debug output */

static const char *acpi_gbl_notify_value_names[] = {
	"Bus Check",
	"Device Check",
	"Device Wake",
	"Eject Request",
	"Device Check Light",
	"Frequency Mismatch",
	"Bus Mode Mismatch",
	"Power Fault",
	"Capabilities Check",
	"Device PLD Check",
	"Reserved",
	"System Locality Update"
};

const char *acpi_ut_get_notify_name(u32 notify_value)
{

	if (notify_value <= ACPI_NOTIFY_MAX) {
		return (acpi_gbl_notify_value_names[notify_value]);
	} else if (notify_value <= ACPI_MAX_SYS_NOTIFY) {
		return ("Reserved");
	} else {		/* Greater or equal to 0x80 */

		return ("**Device Specific**");
	}
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_valid_object_type
 *
 * PARAMETERS:  Type            - Object type to be validated
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

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_init_globals
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init library globals.  All globals that require specific
 *              initialization should be initialized here!
 *
 ******************************************************************************/

acpi_status acpi_ut_init_globals(void)
{
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(ut_init_globals);

	/* Create all memory caches */

	status = acpi_ut_create_caches();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Mutex locked flags */

	for (i = 0; i < ACPI_NUM_MUTEX; i++) {
		acpi_gbl_mutex_info[i].mutex = NULL;
		acpi_gbl_mutex_info[i].thread_id = ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_mutex_info[i].use_count = 0;
	}

	for (i = 0; i < ACPI_NUM_OWNERID_MASKS; i++) {
		acpi_gbl_owner_id_mask[i] = 0;
	}

	/* Last owner_iD is never valid */

	acpi_gbl_owner_id_mask[ACPI_NUM_OWNERID_MASKS - 1] = 0x80000000;

	/* GPE support */

	acpi_gbl_gpe_xrupt_list_head = NULL;
	acpi_gbl_gpe_fadt_blocks[0] = NULL;
	acpi_gbl_gpe_fadt_blocks[1] = NULL;
	acpi_current_gpe_count = 0;
	acpi_all_gpes_initialized = FALSE;

	/* Global handlers */

	acpi_gbl_system_notify.handler = NULL;
	acpi_gbl_device_notify.handler = NULL;
	acpi_gbl_exception_handler = NULL;
	acpi_gbl_init_handler = NULL;
	acpi_gbl_table_handler = NULL;
	acpi_gbl_interface_handler = NULL;

	/* Global Lock support */

	acpi_gbl_global_lock_semaphore = NULL;
	acpi_gbl_global_lock_mutex = NULL;
	acpi_gbl_global_lock_acquired = FALSE;
	acpi_gbl_global_lock_handle = 0;
	acpi_gbl_global_lock_present = FALSE;

	/* Miscellaneous variables */

	acpi_gbl_DSDT = NULL;
	acpi_gbl_cm_single_step = FALSE;
	acpi_gbl_db_terminate_threads = FALSE;
	acpi_gbl_shutdown = FALSE;
	acpi_gbl_ns_lookup_count = 0;
	acpi_gbl_ps_find_count = 0;
	acpi_gbl_acpi_hardware_present = TRUE;
	acpi_gbl_last_owner_id_index = 0;
	acpi_gbl_next_owner_id_offset = 0;
	acpi_gbl_trace_method_name = 0;
	acpi_gbl_trace_dbg_level = 0;
	acpi_gbl_trace_dbg_layer = 0;
	acpi_gbl_debugger_configuration = DEBUGGER_THREADING;
	acpi_gbl_db_output_flags = ACPI_DB_CONSOLE_OUTPUT;
	acpi_gbl_osi_data = 0;
	acpi_gbl_osi_mutex = NULL;

	/* Hardware oriented */

	acpi_gbl_events_initialized = FALSE;
	acpi_gbl_system_awake_and_running = TRUE;

	/* Namespace */

	acpi_gbl_module_code_list = NULL;
	acpi_gbl_root_node = NULL;
	acpi_gbl_root_node_struct.name.integer = ACPI_ROOT_NAME;
	acpi_gbl_root_node_struct.descriptor_type = ACPI_DESC_TYPE_NAMED;
	acpi_gbl_root_node_struct.type = ACPI_TYPE_DEVICE;
	acpi_gbl_root_node_struct.parent = NULL;
	acpi_gbl_root_node_struct.child = NULL;
	acpi_gbl_root_node_struct.peer = NULL;
	acpi_gbl_root_node_struct.object = NULL;

#ifdef ACPI_DEBUG_OUTPUT
	acpi_gbl_lowest_stack_pointer = ACPI_CAST_PTR(acpi_size, ACPI_SIZE_MAX);
#endif

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
	acpi_gbl_display_final_mem_stats = FALSE;
#endif

	return_ACPI_STATUS(AE_OK);
}

ACPI_EXPORT_SYMBOL(acpi_gbl_FADT)
ACPI_EXPORT_SYMBOL(acpi_dbg_level)
ACPI_EXPORT_SYMBOL(acpi_dbg_layer)
ACPI_EXPORT_SYMBOL(acpi_current_gpe_count)
