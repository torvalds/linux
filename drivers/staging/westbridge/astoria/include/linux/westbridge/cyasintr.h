/* Cypress West Bridge API header file (cyasintr.h)
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

#ifndef _INCLUDED_CYASINTR_H_
#define _INCLUDED_CYASINTR_H_

#include "cyasdevice.h"

#include "cyas_cplus_start.h"

/* Summary
   Initialize the interrupt manager module

   Description
   This function is called to initialize the interrupt module.
   This module enables interrupts as well as servies West Bridge
   related interrupts by determining the source of the interrupt
   and calling the appropriate handler function.

   Notes
   If the dmaintr parameter is TRUE, the initialization code
   initializes the interrupt mask to have the DMA related interrupt
   enabled via the general purpose interrupt. However, the interrupt
   service function assumes that the DMA interrupt is handled by the
   HAL layer before the interrupt module handler function is called.

   Returns
   * CY_AS_ERROR_SUCCESS - the interrupt module was initialized
   * correctly
   * CY_AS_ERROR_ALREADY_RUNNING - the interrupt module was already
   * started

   See Also
   * CyAsIntrStop
   * CyAsServiceInterrupt
*/
cy_as_return_status_t
cy_as_intr_start(
	/* Device being initialized */
	cy_as_device *dev_p,
	/* If true, enable the DMA interrupt through the INT signal */
	cy_bool dmaintr
	);

/* Summary
   Stop the interrupt manager module

   Description
   This function stops the interrupt module and masks all interrupts
   from the West Bridge device.

   Returns
   * CY_AS_ERROR_SUCCESS - the interrupt module was stopped
   *	sucessfully
   * CY_AS_ERROR_NOT_RUNNING - the interrupt module was not
   *	running

   See Also
   * CyAsIntrStart
   * CyAsServiceInterrupt
*/
cy_as_return_status_t
cy_as_intr_stop(
	/* Device bein stopped */
	cy_as_device *dev_p
	);


/* Summary
   The interrupt service routine for West Bridge

   Description
   When an interrupt is detected, this function is called to
   service the West Bridge interrupt. It is safe and efficient
   for this function to be called when no West Bridge interrupt
   has occurred. This function will determine it is not an West
   Bridge interrupt quickly and return.
*/
void cy_as_intr_service_interrupt(
	/* The USER supplied tag for this device */
	cy_as_hal_device_tag tag
	);

#include "cyas_cplus_end.h"

#endif				  /* _INCLUDED_CYASINTR_H_ */
