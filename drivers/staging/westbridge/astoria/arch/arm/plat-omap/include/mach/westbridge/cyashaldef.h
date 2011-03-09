/* Cypress West Bridge API header file (cyashaldef.h)
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
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASHALDEF_H_
#define _INCLUDED_CYASHALDEF_H_

/* Summary
 * If set to TRUE, the basic numeric types are defined by the
 * West Bridge API code
 *
 * Description
 * The West Bridge API relies on some basic integral types to be
 * defined.  These types include uint8_t, int8_t, uint16_t,
 * int16_t, uint32_t, and int32_t.  If this macro is defined the
 * West Bridge API will define these types based on some basic
 * assumptions.  If this value is set and the West Bridge API is
 * used to set these types, the definition of these types must be
 * examined to insure that they are appropriate for the given
 * target architecture and compiler.
 *
 * Notes
 * It is preferred that if the basic platform development
 * environment defines these types that the CY_DEFINE_BASIC_TYPES
 * macro be undefined and the appropriate target system header file
 * be added to the file cyashaldef.h.
 */

#include <linux/types.h>


#if !defined(__doxygen__)
typedef int cy_bool;
#define cy_true				(1)
#define cy_false				(0)
#endif

#endif			/* _INCLUDED_CYASHALDEF_H_ */
