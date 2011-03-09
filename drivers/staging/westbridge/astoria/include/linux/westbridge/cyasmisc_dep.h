/* Cypress West Bridge API header file (cyasmisc_dep.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

/* This header will contain Antioch specific declaration
 * of the APIs that are deprecated in Astoria SDK. This is
 * for maintaining backward compatibility with prior releases
 * of the Antioch SDK.
 */
#ifndef __INCLUDED_CYASMISC_DEP_H__
#define __INCLUDED_CYASMISC_DEP_H__

#ifndef __doxygen__

EXTERN cy_as_return_status_t
cy_as_misc_acquire_resource_dep(cy_as_device_handle handle,
		cy_as_resource_type resource,
		cy_bool force);
EXTERN cy_as_return_status_t
cy_as_misc_get_firmware_version_dep(cy_as_device_handle handle,
		uint16_t *major,
		uint16_t *minor,
		uint16_t *build,
		uint8_t *media_type,
		cy_bool *is_debug_mode);
EXTERN cy_as_return_status_t
cy_as_misc_set_trace_level_dep(cy_as_device_handle handle,
		uint8_t level,
		cy_as_media_type media,
		uint32_t device,
		uint32_t unit,
		cy_as_function_callback cb,
		uint32_t client);
#endif /*__doxygen*/

#endif /*__INCLUDED_CYANSTORAGE_DEP_H__*/
