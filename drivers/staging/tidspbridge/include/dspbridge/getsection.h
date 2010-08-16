/*
 * getsection.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * This file provides an API add-on to the dynamic loader that allows the user
 * to query section information and extract section data from dynamic load
 * modules.
 *
 * Notes:
 *   Functions in this API assume that the supplied dynamic_loader_stream
 *   object supports the set_file_posn method.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _GETSECTION_H_
#define _GETSECTION_H_

#include "dynamic_loader.h"

/*
 * Procedure dload_module_open
 *
 * Parameters:
 *  module  The input stream that supplies the module image
 *  syms    Host-side malloc/free and error reporting functions.
 *          Other methods are unused.
 *
 * Effect:
 *  Reads header information from a dynamic loader module using the specified
 * stream object, and returns a handle for the module information.  This
 * handle may be used in subsequent query calls to obtain information
 * contained in the module.
 *
 * Returns:
 *  NULL if an error is encountered, otherwise a module handle for use
 * in subsequent operations.
 */
extern void *dload_module_open(struct dynamic_loader_stream
					   *module, struct dynamic_loader_sym
					   *syms);

/*
 * Procedure dload_get_section_info
 *
 * Parameters:
 *  minfo       Handle from dload_module_open for this module
 *  section_name Pointer to the string name of the section desired
 *  section_info Address of a section info structure pointer to be initialized
 *
 * Effect:
 *  Finds the specified section in the module information, and fills in
 * the provided ldr_section_info structure.
 *
 * Returns:
 *  TRUE for success, FALSE for section not found
 */
extern int dload_get_section_info(void *minfo,
				  const char *section_name,
				  const struct ldr_section_info
				  **const section_info);

/*
 * Procedure dload_get_section
 *
 * Parameters:
 *  minfo       Handle from dload_module_open for this module
 *  section_info Pointer to a section info structure for the desired section
 *  section_data Buffer to contain the section initialized data
 *
 * Effect:
 *  Copies the initialized data for the specified section into the
 * supplied buffer.
 *
 * Returns:
 *  TRUE for success, FALSE for section not found
 */
extern int dload_get_section(void *minfo,
			     const struct ldr_section_info *section_info,
			     void *section_data);

/*
 * Procedure dload_module_close
 *
 * Parameters:
 *  minfo       Handle from dload_module_open for this module
 *
 * Effect:
 *  Releases any storage associated with the module handle.  On return,
 * the module handle is invalid.
 *
 * Returns:
 *  Zero for success. On error, the number of errors detected is returned.
 * Individual errors are reported using syms->error_report(), where syms was
 * an argument to dload_module_open
 */
extern void dload_module_close(void *minfo);

#endif /* _GETSECTION_H_ */
