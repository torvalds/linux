/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_VERSION_H
#define __IA_CSS_VERSION_H

/* @file
 * This file contains functions to retrieve CSS-API version information
 */

#include <ia_css_err.h>

/* a common size for the version arrays */
#define MAX_VERSION_SIZE	500

/* @brief Retrieves the current CSS version
 * @param[out]	version		A pointer to a buffer where to put the generated
 *				version string. NULL is ignored.
 * @param[in]	max_size	Size of the version buffer. If version string
 *				would be larger than max_size, an error is
 *				returned by this function.
 *
 * This function generates and returns the version string. If FW is loaded, it
 * attaches the FW version.
 */
enum ia_css_err
ia_css_get_version(char *version, int max_size);

#endif /* __IA_CSS_VERSION_H */
