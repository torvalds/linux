/* Cypress West Bridge API header file (cyashalcb.h)
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

#ifndef _INCLUDED_CYASHALCB_H_
#define _INCLUDED_CYASHALCB_H_

/* Summary
   This type defines a callback function type called when a
   DMA operation has completed.

   Description

   See Also
   * CyAsHalDmaRegisterCallback
   * CyAsHalDmaSetupWrite
   * CyAsHalDmaSetupRead
*/
typedef void (*cy_as_hal_dma_complete_callback)(
	cy_as_hal_device_tag tag,
	cy_as_end_point_number_t ep,
	uint32_t cnt,
	cy_as_return_status_t ret);

typedef cy_as_hal_dma_complete_callback \
	cy_an_hal_dma_complete_callback;
#endif
