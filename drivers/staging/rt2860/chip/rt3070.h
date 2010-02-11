/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
	rt3070.h

    Abstract:

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
 */

#ifndef __RT3070_H__
#define __RT3070_H__

#ifdef RT3070

#ifndef RTMP_USB_SUPPORT
#error "For RT3070, you should define the compile flag -DRTMP_USB_SUPPORT"
#endif

#ifndef RTMP_MAC_USB
#error "For RT3070, you should define the compile flag -DRTMP_MAC_USB"
#endif

#ifndef RTMP_RF_RW_SUPPORT
#error "For RT3070, you should define the compile flag -DRTMP_RF_RW_SUPPORT"
#endif

#ifndef RT30xx
#error "For RT3070, you should define the compile flag -DRT30xx"
#endif

#include "mac_usb.h"
#include "rt30xx.h"

/* */
/* Device ID & Vendor ID, these values should match EEPROM value */
/* */

#endif /* RT3070 // */

#endif /*__RT3070_H__ // */
