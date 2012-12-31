/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _UMP_IMPORT_H_
#define _UMP_IMPORT_H_

#include <ump/ump_kernel_interface.h>
#include <linux/module.h>

/**
 * UMP import module info.
 * Contains information about the Linux module providing the import module,
 * used to block unloading of the Linux module while imported memory exists.
 * Lists the functions implementing the UMP import functions.
 */
struct ump_import_handler
{
	/**
	 * Linux module of the import handler
	 */
	struct module * linux_module;

	/**
	 * UMP session usage begin.
	 *
	 * Called when a UMP session first is bound to the handler.
	 * Typically used to set up any import module specific per-session data.
	 * The function returns a pointer to this data in the output pointer custom_session_data
	 * which will be passed to \a session_end and \a import.
	 * 
	 * Note: custom_session_data must be set to non-NULL if successful.
	 * If no pointer is needed set it a magic value to validate instead.
	 *
	 * @param[out] custom_session_data Pointer to a generic pointer where any data can be stored
	 * @return 0 on success, error code if the session could not be initiated.
	 */
	int (*session_begin)(void ** custom_session_data);

	/**
	 * UMP session usage end.
	 *
	 * Called when a UMP session is no longer using the handler.
	 * Only called if @a session_begin returned OK.
	 *
	 * @param[in] custom_session_data The value set by the session_begin handler
	 */
	void (*session_end)(void * custom_session_data);

	/**
	 * Import request.
	 *
	 * Called when a client has asked to import a resource of the type the import module was installed for.
	 * Only called if @a session_begin returned OK.
	 *
	 * The requested flags must be verified to be valid to apply to the imported memory.
	 * If not valid return UMP_DD_INVALID_MEMORY_HANDLE.
	 * If the flags are found to be valid call \a ump_dd_create_from_phys_blocks_64 to create a handle.
	 *
	 * @param[in] custom_session_data The value set by the session_begin handler
	 * @param[in] phandle Pointer to the handle to import
	 * @param     flags The requested UMPv2 flags to assign to the imported handle
	 * @return UMP_DD_INVALID_MEMORY_HANDLE if the import failed, a valid ump handle on success
	 */
	ump_dd_handle (*import)(void * custom_session_data, void * phandle, ump_alloc_flags flags);
};

/**
 * Import module registration.
 * Registers a ump_import_handler structure for a memory type.
 * @param     type    Type of the memory to register a handler for
 * @param[in] handler Handler strcture to install
 * @return 0 on success, a Linux error code on failure
 */
int ump_import_module_register(enum ump_external_memory_type type, struct ump_import_handler * handler);

/**
 * Import module deregistration.
 * Uninstalls the handler for the given memory type.
 * @param type Type of the memory to unregister the handler for
 */
void ump_import_module_unregister(enum ump_external_memory_type type);

#endif /* _UMP_IMPORT_H_ */
