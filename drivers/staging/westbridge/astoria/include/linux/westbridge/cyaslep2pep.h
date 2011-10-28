/* Cypress West Bridge API header file (cyaslep2pep.h)
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

#ifndef _INCLUDED_CYASLEP2PEP_H_
#define _INCLUDED_CYASLEP2PEP_H_

#include "cyasdevice.h"

extern cy_as_return_status_t
cy_as_usb_map_logical2_physical(cy_as_device *dev_p);

extern cy_as_return_status_t
cy_as_usb_setup_dma(cy_as_device *dev_p);

extern cy_as_return_status_t
cy_as_usb_set_dma_sizes(cy_as_device *dev_p);

#endif
