/* Cypress West Bridge API header file (cyascast.h)
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

#ifndef _INCLUDED_CYASCAST_H_
#define _INCLUDED_CYASCAST_H_

#ifndef __doxygen__

#ifdef _DEBUG
#define cy_cast_int2U_int16(v) \
	(cy_as_hal_assert(v < 65536), (uint16_t)(v))
#else		   /* _DEBUG */
#define cy_cast_int2U_int16(v) ((uint16_t)(v))
#endif		  /* _DEBUG */

#endif	  /* __doxygen__ */
#endif		  /* _INCLUDED_CYASCAST_H_ */
