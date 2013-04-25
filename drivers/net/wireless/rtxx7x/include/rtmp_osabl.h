/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifndef __RTMP_OS_ABL_H__
#define __RTMP_OS_ABL_H__

#ifdef OS_ABL_FUNC_SUPPORT

#ifdef OS_ABL_OS_PCI_SUPPORT
#define RTMP_MAC_PCI
#define RTMP_PCI_SUPPORT
#endif /* OS_ABL_OS_PCI_SUPPORT */

#ifdef OS_ABL_OS_USB_SUPPORT
#include <linux/usb.h>

#ifndef RTMP_MAC_USB
#define RTMP_MAC_USB
#endif /* RTMP_MAC_USB */
#ifndef RTMP_USB_SUPPORT
#define RTMP_USB_SUPPORT
#endif /* RTMP_USB_SUPPORT */
#endif /* OS_ABL_OS_USB_SUPPORT */

#ifdef OS_ABL_OS_RBUS_SUPPORT
#define RTMP_RBUS_SUPPORT
#endif /* OS_ABL_OS_RBUS_SUPPORT */

#ifdef OS_ABL_OS_AP_SUPPORT
#define CONFIG_AP_SUPPORT
#endif /* OS_ABL_OS_AP_SUPPORT */

#ifdef OS_ABL_OS_STA_SUPPORT
#ifndef CONFIG_STA_SUPPORT
#define CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */
#endif /* OS_ABL_OS_STA_SUPPORT */

/* AP & STA con-current */
#undef RT_CONFIG_IF_OPMODE_ON_AP
#undef RT_CONFIG_IF_OPMODE_ON_STA

#if defined(CONFIG_AP_SUPPORT) && defined(CONFIG_STA_SUPPORT)
#define RT_CONFIG_IF_OPMODE_ON_AP(__OpMode)		if (__OpMode == OPMODE_AP)
#define RT_CONFIG_IF_OPMODE_ON_STA(__OpMode)	if (__OpMode == OPMODE_STA)
#else
#define RT_CONFIG_IF_OPMODE_ON_AP(__OpMode)
#define RT_CONFIG_IF_OPMODE_ON_STA(__OpMode)
#endif

#endif /* OS_ABL_FUNC_SUPPORT */

#endif /* __RTMP_OS_ABL_H__ */

/* End of rtmp_osabl.h */
