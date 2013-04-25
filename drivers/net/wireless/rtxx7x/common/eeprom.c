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


#include "rt_config.h"


INT RtmpChipOpsEepromHook(
	IN RTMP_ADAPTER *pAd,
	IN INT			infType)
{
	RTMP_CHIP_OP	*pChipOps = &pAd->chipOps;

#ifdef RTMP_FLASH_SUPPORT
	pChipOps->eeinit = rtmp_nv_init;
	pChipOps->eeread = rtmp_ee_flash_read;
	pChipOps->eewrite = rtmp_ee_flash_write;
	return 0;
#endif /* RTMP_FLASH_SUPPORT */

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
	UINT32			eFuseCtrl, MacCsr0;
	int index;
	
	index = 0;
	do
	{
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))			
			return -1;
		
		RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);
		pAd->MACVersion = MacCsr0;

		if ((pAd->MACVersion != 0x00) && (pAd->MACVersion != 0xFFFFFFFF))
			break;

		RTMPusecDelay(10);
	} while (index++ < 100);
	
	pAd->bUseEfuse=FALSE;
	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrl);
	pAd->bUseEfuse = ( (eFuseCtrl & 0x80000000) == 0x80000000) ? 1 : 0;	
	if(pAd->bUseEfuse)
	{
		pChipOps->eeinit = eFuse_init;
		pChipOps->eeread = rtmp_ee_efuse_read16;
		pChipOps->eewrite = rtmp_ee_efuse_write16;
		DBGPRINT(RT_DEBUG_TRACE, ("NVM is EFUSE\n"));
		return 0 ;	
	}
	else
	{
		pAd->bFroceEEPROMBuffer = FALSE;
		DBGPRINT(RT_DEBUG_TRACE, ("NVM is EEPROM\n"));
	}
#endif /* RTMP_EFUSE_SUPPORT */
#endif /* RT30xx */

	switch(infType) 
	{


#ifdef RTMP_USB_SUPPORT
		case RTMP_DEV_INF_USB:
			pChipOps->eeinit = NULL;
			pChipOps->eeread = RTUSBReadEEPROM16;
			pChipOps->eewrite = RTUSBWriteEEPROM16;
			break;
#endif /* RTMP_USB_SUPPORT */
		default:
			DBGPRINT(RT_DEBUG_ERROR, ("RtmpChipOpsEepromHook() failed!\n"));
			break;
	}

	return 0;
}

