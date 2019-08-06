// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: utosi - Support for the _OSI predefined control method
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utosi")

/******************************************************************************
 *
 * ACPICA policy for new _OSI strings:
 *
 * It is the stated policy of ACPICA that new _OSI strings will be integrated
 * into this module as soon as possible after they are defined. It is strongly
 * recommended that all ACPICA hosts mirror this policy and integrate any
 * changes to this module as soon as possible. There are several historical
 * reasons behind this policy:
 *
 * 1) New BIOSs tend to test only the case where the host responds TRUE to
 *    the latest version of Windows, which would respond to the latest/newest
 *    _OSI string. Not responding TRUE to the latest version of Windows will
 *    risk executing untested code paths throughout the DSDT and SSDTs.
 *
 * 2) If a new _OSI string is recognized only after a significant delay, this
 *    has the potential to cause problems on existing working machines because
 *    of the possibility that a new and different path through the ASL code
 *    will be executed.
 *
 * 3) New _OSI strings are tending to come out about once per year. A delay
 *    in recognizing a new string for a significant amount of time risks the
 *    release of another string which only compounds the initial problem.
 *
 *****************************************************************************/
/*
 * Strings supported by the _OSI predefined control method (which is
 * implemented internally within this module.)
 *
 * March 2009: Removed "Linux" as this host no longer wants to respond true
 * for this string. Basically, the only safe OS strings are windows-related
 * and in many or most cases represent the only test path within the
 * BIOS-provided ASL code.
 *
 * The last element of each entry is used to track the newest version of
 * Windows that the BIOS has requested.
 */
static struct acpi_interface_info acpi_default_supported_interfaces[] = {
	/* Operating System Vendor Strings */

	{"Windows 2000", NULL, 0, ACPI_OSI_WIN_2000},	/* Windows 2000 */
	{"Windows 2001", NULL, 0, ACPI_OSI_WIN_XP},	/* Windows XP */
	{"Windows 2001 SP1", NULL, 0, ACPI_OSI_WIN_XP_SP1},	/* Windows XP SP1 */
	{"Windows 2001.1", NULL, 0, ACPI_OSI_WINSRV_2003},	/* Windows Server 2003 */
	{"Windows 2001 SP2", NULL, 0, ACPI_OSI_WIN_XP_SP2},	/* Windows XP SP2 */
	{"Windows 2001.1 SP1", NULL, 0, ACPI_OSI_WINSRV_2003_SP1},	/* Windows Server 2003 SP1 - Added 03/2006 */
	{"Windows 2006", NULL, 0, ACPI_OSI_WIN_VISTA},	/* Windows vista - Added 03/2006 */
	{"Windows 2006.1", NULL, 0, ACPI_OSI_WINSRV_2008},	/* Windows Server 2008 - Added 09/2009 */
	{"Windows 2006 SP1", NULL, 0, ACPI_OSI_WIN_VISTA_SP1},	/* Windows Vista SP1 - Added 09/2009 */
	{"Windows 2006 SP2", NULL, 0, ACPI_OSI_WIN_VISTA_SP2},	/* Windows Vista SP2 - Added 09/2010 */
	{"Windows 2009", NULL, 0, ACPI_OSI_WIN_7},	/* Windows 7 and Server 2008 R2 - Added 09/2009 */
	{"Windows 2012", NULL, 0, ACPI_OSI_WIN_8},	/* Windows 8 and Server 2012 - Added 08/2012 */
	{"Windows 2013", NULL, 0, ACPI_OSI_WIN_8},	/* Windows 8.1 and Server 2012 R2 - Added 01/2014 */
	{"Windows 2015", NULL, 0, ACPI_OSI_WIN_10},	/* Windows 10 - Added 03/2015 */
	{"Windows 2016", NULL, 0, ACPI_OSI_WIN_10_RS1},	/* Windows 10 version 1607 - Added 12/2017 */
	{"Windows 2017", NULL, 0, ACPI_OSI_WIN_10_RS2},	/* Windows 10 version 1703 - Added 12/2017 */
	{"Windows 2017.2", NULL, 0, ACPI_OSI_WIN_10_RS3},	/* Windows 10 version 1709 - Added 02/2018 */
	{"Windows 2018", NULL, 0, ACPI_OSI_WIN_10_RS4},	/* Windows 10 version 1803 - Added 11/2018 */
	{"Windows 2018.2", NULL, 0, ACPI_OSI_WIN_10_RS5},	/* Windows 10 version 1809 - Added 11/2018 */

	/* Feature Group Strings */

	{"Extended Address Space Descriptor", NULL, ACPI_OSI_FEATURE, 0},

	/*
	 * All "optional" feature group strings (features that are implemented
	 * by the host) should be dynamically modified to VALID by the host via
	 * acpi_install_interface or acpi_update_interfaces. Such optional feature
	 * group strings are set as INVALID by default here.
	 */

	{"Module Device", NULL, ACPI_OSI_OPTIONAL_FEATURE, 0},
	{"Processor Device", NULL, ACPI_OSI_OPTIONAL_FEATURE, 0},
	{"3.0 Thermal Model", NULL, ACPI_OSI_OPTIONAL_FEATURE, 0},
	{"3.0 _SCP Extensions", NULL, ACPI_OSI_OPTIONAL_FEATURE, 0},
	{"Processor Aggregator Device", NULL, ACPI_OSI_OPTIONAL_FEATURE, 0}
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_initialize_interfaces
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the global _OSI supported interfaces list
 *
 ******************************************************************************/

acpi_status acpi_ut_initialize_interfaces(void)
{
	acpi_status status;
	u32 i;

	status = acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	acpi_gbl_supported_interfaces = acpi_default_supported_interfaces;

	/* Link the static list of supported interfaces */

	for (i = 0;
	     i < (ACPI_ARRAY_LENGTH(acpi_default_supported_interfaces) - 1);
	     i++) {
		acpi_default_supported_interfaces[i].next =
		    &acpi_default_supported_interfaces[(acpi_size)i + 1];
	}

	acpi_os_release_mutex(acpi_gbl_osi_mutex);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_interface_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete all interfaces in the global list. Sets
 *              acpi_gbl_supported_interfaces to NULL.
 *
 ******************************************************************************/

acpi_status acpi_ut_interface_terminate(void)
{
	acpi_status status;
	struct acpi_interface_info *next_interface;

	status = acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	next_interface = acpi_gbl_supported_interfaces;
	while (next_interface) {
		acpi_gbl_supported_interfaces = next_interface->next;

		if (next_interface->flags & ACPI_OSI_DYNAMIC) {

			/* Only interfaces added at runtime can be freed */

			ACPI_FREE(next_interface->name);
			ACPI_FREE(next_interface);
		} else {
			/* Interface is in static list. Reset it to invalid or valid. */

			if (next_interface->flags & ACPI_OSI_DEFAULT_INVALID) {
				next_interface->flags |= ACPI_OSI_INVALID;
			} else {
				next_interface->flags &= ~ACPI_OSI_INVALID;
			}
		}

		next_interface = acpi_gbl_supported_interfaces;
	}

	acpi_os_release_mutex(acpi_gbl_osi_mutex);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_install_interface
 *
 * PARAMETERS:  interface_name      - The interface to install
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install the interface into the global interface list.
 *              Caller MUST hold acpi_gbl_osi_mutex
 *
 ******************************************************************************/

acpi_status acpi_ut_install_interface(acpi_string interface_name)
{
	struct acpi_interface_info *interface_info;

	/* Allocate info block and space for the name string */

	interface_info =
	    ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_interface_info));
	if (!interface_info) {
		return (AE_NO_MEMORY);
	}

	interface_info->name = ACPI_ALLOCATE_ZEROED(strlen(interface_name) + 1);
	if (!interface_info->name) {
		ACPI_FREE(interface_info);
		return (AE_NO_MEMORY);
	}

	/* Initialize new info and insert at the head of the global list */

	strcpy(interface_info->name, interface_name);
	interface_info->flags = ACPI_OSI_DYNAMIC;
	interface_info->next = acpi_gbl_supported_interfaces;

	acpi_gbl_supported_interfaces = interface_info;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_remove_interface
 *
 * PARAMETERS:  interface_name      - The interface to remove
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove the interface from the global interface list.
 *              Caller MUST hold acpi_gbl_osi_mutex
 *
 ******************************************************************************/

acpi_status acpi_ut_remove_interface(acpi_string interface_name)
{
	struct acpi_interface_info *previous_interface;
	struct acpi_interface_info *next_interface;

	previous_interface = next_interface = acpi_gbl_supported_interfaces;
	while (next_interface) {
		if (!strcmp(interface_name, next_interface->name)) {
			/*
			 * Found: name is in either the static list
			 * or was added at runtime
			 */
			if (next_interface->flags & ACPI_OSI_DYNAMIC) {

				/* Interface was added dynamically, remove and free it */

				if (previous_interface == next_interface) {
					acpi_gbl_supported_interfaces =
					    next_interface->next;
				} else {
					previous_interface->next =
					    next_interface->next;
				}

				ACPI_FREE(next_interface->name);
				ACPI_FREE(next_interface);
			} else {
				/*
				 * Interface is in static list. If marked invalid, then
				 * it does not actually exist. Else, mark it invalid.
				 */
				if (next_interface->flags & ACPI_OSI_INVALID) {
					return (AE_NOT_EXIST);
				}

				next_interface->flags |= ACPI_OSI_INVALID;
			}

			return (AE_OK);
		}

		previous_interface = next_interface;
		next_interface = next_interface->next;
	}

	/* Interface was not found */

	return (AE_NOT_EXIST);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_update_interfaces
 *
 * PARAMETERS:  action              - Actions to be performed during the
 *                                    update
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Update _OSI interface strings, disabling or enabling OS vendor
 *              strings or/and feature group strings.
 *              Caller MUST hold acpi_gbl_osi_mutex
 *
 ******************************************************************************/

acpi_status acpi_ut_update_interfaces(u8 action)
{
	struct acpi_interface_info *next_interface;

	next_interface = acpi_gbl_supported_interfaces;
	while (next_interface) {
		if (((next_interface->flags & ACPI_OSI_FEATURE) &&
		     (action & ACPI_FEATURE_STRINGS)) ||
		    (!(next_interface->flags & ACPI_OSI_FEATURE) &&
		     (action & ACPI_VENDOR_STRINGS))) {
			if (action & ACPI_DISABLE_INTERFACES) {

				/* Mark the interfaces as invalid */

				next_interface->flags |= ACPI_OSI_INVALID;
			} else {
				/* Mark the interfaces as valid */

				next_interface->flags &= ~ACPI_OSI_INVALID;
			}
		}

		next_interface = next_interface->next;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_get_interface
 *
 * PARAMETERS:  interface_name      - The interface to find
 *
 * RETURN:      struct acpi_interface_info if found. NULL if not found.
 *
 * DESCRIPTION: Search for the specified interface name in the global list.
 *              Caller MUST hold acpi_gbl_osi_mutex
 *
 ******************************************************************************/

struct acpi_interface_info *acpi_ut_get_interface(acpi_string interface_name)
{
	struct acpi_interface_info *next_interface;

	next_interface = acpi_gbl_supported_interfaces;
	while (next_interface) {
		if (!strcmp(interface_name, next_interface->name)) {
			return (next_interface);
		}

		next_interface = next_interface->next;
	}

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_osi_implementation
 *
 * PARAMETERS:  walk_state          - Current walk state
 *
 * RETURN:      Status
 *              Integer: TRUE (0) if input string is matched
 *                       FALSE (-1) if string is not matched
 *
 * DESCRIPTION: Implementation of the _OSI predefined control method. When
 *              an invocation of _OSI is encountered in the system AML,
 *              control is transferred to this function.
 *
 * (August 2016)
 * Note:  _OSI is now defined to return "Ones" to indicate a match, for
 * compatibility with other ACPI implementations. On a 32-bit DSDT, Ones
 * is 0xFFFFFFFF. On a 64-bit DSDT, Ones is 0xFFFFFFFFFFFFFFFF
 * (ACPI_UINT64_MAX).
 *
 * This function always returns ACPI_UINT64_MAX for TRUE, and later code
 * will truncate this to 32 bits if necessary.
 *
 ******************************************************************************/

acpi_status acpi_ut_osi_implementation(struct acpi_walk_state *walk_state)
{
	union acpi_operand_object *string_desc;
	union acpi_operand_object *return_desc;
	struct acpi_interface_info *interface_info;
	acpi_interface_handler interface_handler;
	acpi_status status;
	u64 return_value;

	ACPI_FUNCTION_TRACE(ut_osi_implementation);

	/* Validate the string input argument (from the AML caller) */

	string_desc = walk_state->arguments[0].object;
	if (!string_desc || (string_desc->common.type != ACPI_TYPE_STRING)) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Create a return object */

	return_desc = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
	if (!return_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Default return value is 0, NOT SUPPORTED */

	return_value = 0;
	status = acpi_os_acquire_mutex(acpi_gbl_osi_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(return_desc);
		return_ACPI_STATUS(status);
	}

	/* Lookup the interface in the global _OSI list */

	interface_info = acpi_ut_get_interface(string_desc->string.pointer);
	if (interface_info && !(interface_info->flags & ACPI_OSI_INVALID)) {
		/*
		 * The interface is supported.
		 * Update the osi_data if necessary. We keep track of the latest
		 * version of Windows that has been requested by the BIOS.
		 */
		if (interface_info->value > acpi_gbl_osi_data) {
			acpi_gbl_osi_data = interface_info->value;
		}

		return_value = ACPI_UINT64_MAX;
	}

	acpi_os_release_mutex(acpi_gbl_osi_mutex);

	/*
	 * Invoke an optional _OSI interface handler. The host OS may wish
	 * to do some interface-specific handling. For example, warn about
	 * certain interfaces or override the true/false support value.
	 */
	interface_handler = acpi_gbl_interface_handler;
	if (interface_handler) {
		if (interface_handler
		    (string_desc->string.pointer, (u32)return_value)) {
			return_value = ACPI_UINT64_MAX;
		}
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INFO,
			      "ACPI: BIOS _OSI(\"%s\") is %ssupported\n",
			      string_desc->string.pointer,
			      return_value == 0 ? "not " : ""));

	/* Complete the return object */

	return_desc->integer.value = return_value;
	walk_state->return_desc = return_desc;
	return_ACPI_STATUS(AE_OK);
}
