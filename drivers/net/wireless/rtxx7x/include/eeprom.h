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


#ifndef __EEPROM_H__
#define __EEPROM_H__


#ifdef RTMP_MAC_USB
#define EEPROM_SIZE					0x400
#endif /* RTMP_MAC_USB */




#ifdef RTMP_USB_SUPPORT
/*************************************************************************
  *	Public function declarations for usb-based prom chipset
  ************************************************************************/
NTSTATUS RTUSBReadEEPROM16(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT			offset,
	OUT	PUSHORT			pData);


NTSTATUS RTUSBWriteEEPROM16(
	IN RTMP_ADAPTER *pAd, 
	IN USHORT offset, 
	IN USHORT value);

#endif /* RTMP_USB_SUPPORT */


#if defined(RTMP_RBUS_SUPPORT) || defined(RTMP_FLASH_SUPPORT)
/*************************************************************************
  *	Public function declarations for flash-based chipset
  ************************************************************************/
NDIS_STATUS rtmp_nv_init(
	IN PRTMP_ADAPTER pAd);

int rtmp_ee_flash_read(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT Offset,
	OUT USHORT *pValue);

int rtmp_ee_flash_write(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT Offset, 
	IN USHORT Data);

VOID rtmp_ee_flash_read_all(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT *Data);

VOID rtmp_ee_flash_write_all(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT *Data);

#endif /* defined(RTMP_RBUS_SUPPORT) || defined(RTMP_FLASH_SUPPORT) */

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
int rtmp_ee_efuse_read16(
	IN RTMP_ADAPTER *pAd, 
	IN USHORT Offset,
	OUT USHORT *pValue);

int rtmp_ee_efuse_write16(
	IN RTMP_ADAPTER *pAd, 
	IN USHORT Offset, 
	IN USHORT data);
#endif /* RTMP_EFUSE_SUPPORT */
#endif /* RT30xx */

/*************************************************************************
  *	Public function declarations for prom operation callback functions setting
  ************************************************************************/
INT RtmpChipOpsEepromHook(
	IN RTMP_ADAPTER *pAd,
	IN INT			infType);

#endif /* __EEPROM_H__ */
