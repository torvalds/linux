/* Cypress West Bridge API header file (cyastypes.h)
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

#ifndef _INCLUDED_CYASTYPES_H_
#define _INCLUDED_CYASTYPES_H_
/* moved to staging location, eventual implementation
 * considered is here
#include <mach/westbridge/cyashaldef.h>
*/
 #include "../../../arch/arm/plat-omap/include/mach/westbridge/cyashaldef.h"

/* Types that are not available on specific platforms.
 * These are used only in the reference HAL implementations and
 * are not required for using the API.
 */
#ifdef __unix__
typedef unsigned long DWORD;
typedef void *LPVOID;
#define WINAPI
#define INFINITE		(0xFFFFFFFF)
#define ptr_to_uint(ptr)  ((unsigned int)(ptr))
#endif

/* Basic types used by the entire API */

/* Summary
   This type represents an endpoint number
*/
typedef uint8_t cy_as_end_point_number_t ;

/* Summary
   This type is used to return status information from
	an API call.
*/
typedef uint16_t cy_as_return_status_t ;

/* Summary
   This type represents a bus number
*/
typedef uint32_t cy_as_bus_number_t ;

/* Summary
   All APIs provided with this release are marked extern
   through this definition. This definition can be changed
   to meet the scope changes required in the user build
   environment.

   For example, this can be changed to __declspec(exportdll)
   to enable exporting the API from a DLL.
 */
#define EXTERN		  extern

#endif
