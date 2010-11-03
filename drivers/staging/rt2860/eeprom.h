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
	eeprom.h

	Abstract:
	Miniport header file for eeprom related information

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/
#ifndef __EEPROM_H__
#define __EEPROM_H__

#ifdef RTMP_PCI_SUPPORT
/*************************************************************************
  *	Public function declarations for prom-based chipset
  ************************************************************************/
int rtmp_ee_prom_read16(struct rt_rtmp_adapter *pAd,
			u16 Offset, u16 *pValue);
#endif /* RTMP_PCI_SUPPORT // */
#ifdef RTMP_USB_SUPPORT
/*************************************************************************
  *	Public function declarations for usb-based prom chipset
  ************************************************************************/
int RTUSBReadEEPROM16(struct rt_rtmp_adapter *pAd,
			   u16 offset, u16 *pData);
#endif /* RTMP_USB_SUPPORT // */

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
int rtmp_ee_efuse_read16(struct rt_rtmp_adapter *pAd,
			 u16 Offset, u16 *pValue);
#endif /* RTMP_EFUSE_SUPPORT // */
#endif /* RT30xx // */

/*************************************************************************
  *	Public function declarations for prom operation callback functions setting
  ************************************************************************/
int RtmpChipOpsEepromHook(struct rt_rtmp_adapter *pAd, int infType);

#endif /* __EEPROM_H__ // */
