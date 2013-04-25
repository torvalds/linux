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


#include	"rt_config.h"
#include 	"firmware.h"


#ifdef RTMP_MAC_USB

/* RT2870 Firmware Spec only used 1 oct for version expression*/

#define FIRMWARE_MINOR_VERSION	7
#endif /* RTMP_MAC_USB */

/* New 8k byte firmware size for RT3071/RT3072*/
#define FIRMWAREIMAGE_MAX_LENGTH	0x2000
#define FIRMWAREIMAGE_LENGTH			(sizeof (FirmwareImage) / sizeof(UCHAR))
#define FIRMWARE_MAJOR_VERSION		0

#define FIRMWAREIMAGEV1_LENGTH		0x1000
#define FIRMWAREIMAGEV2_LENGTH		0x1000


const unsigned short ccitt_16Table[] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};
#define ByteCRC16(v, crc) \
	(unsigned short)((crc << 8) ^  ccitt_16Table[((crc >> 8) ^ (v)) & 255])

unsigned char BitReverse(unsigned char x)
{
	int i;
	unsigned char Temp=0;
	for(i=0; ; i++)
	{
		if(x & 0x80)	Temp |= 0x80;
		if(i==7)		break;
		x	<<= 1;
		Temp >>= 1;
	}
	return Temp;
}

NDIS_STATUS isMCUNeedToLoadFIrmware(
	IN PRTMP_ADAPTER pAd)
{
	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	ULONG			Index;
	UINT32			MacReg;
	
	Index = 0;

	do {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))			
			return NDIS_STATUS_FAILURE;
		
		RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &MacReg);

		if (MacReg & 0x100) /* check bit 8*/
			break;
		
		RTMPusecDelay(1000);
	} while (Index++ < 1000);

	if (Index >= 1000)
		Status = NDIS_STATUS_FAILURE;

	return Status;
}

NDIS_STATUS isMCUnotReady(
	IN PRTMP_ADAPTER pAd)
{
	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	ULONG			Index;
	UINT32			MacReg;
	
	Index = 0;

	do {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))			
			return NDIS_STATUS_FAILURE;
		
		RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &MacReg);
		DBGPRINT(RT_DEBUG_ERROR, ("MacReg = %x, %s\n", MacReg, __func__));
		if (MacReg & 0x80) /* check bit 7*/
			break;
		
		RTMPusecDelay(1000);
	} while (Index++ < 1000);

	if (Index >= 1000)
		Status = NDIS_STATUS_FAILURE;

	return Status;
}
/*
	========================================================================
	
	Routine Description:
		erase 8051 firmware image in MAC ASIC

	Arguments:
		Adapter						Pointer to our adapter

	IRQL = PASSIVE_LEVEL
		
	========================================================================
*/
INT RtmpAsicEraseFirmware(
	IN PRTMP_ADAPTER pAd)
{
	ULONG i;

	for(i=0; i<MAX_FIRMWARE_IMAGE_SIZE; i+=4)
		RTMP_IO_WRITE32(pAd, FIRMWARE_IMAGE_BASE + i, 0);

	return 0;
}

/*
	========================================================================
	
	Routine Description:
		Load 8051 firmware file into MAC ASIC

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS         firmware image load ok
		NDIS_STATUS_FAILURE         image not found

	IRQL = PASSIVE_LEVEL
		
	========================================================================
*/
NDIS_STATUS RtmpAsicLoadFirmware(
	IN PRTMP_ADAPTER pAd)
{
#ifdef BIN_IN_FILE
#define NICLF_DEFAULT_USE()	\
	flg_default_firm_use = TRUE; \
	DBGPRINT(RT_DEBUG_OFF, ("%s - Use default firmware!\n", __FUNCTION__));

	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	PUCHAR			src;
	RTMP_OS_FD		srcf;
	INT 				retval, i;
	PUCHAR			pFirmwareImage;
	INT				FileLength = 0;
	UINT32			MacReg;
	ULONG			Index;
	ULONG			firm;
	BOOLEAN			flg_default_firm_use = FALSE;
	RTMP_OS_FS_INFO	osFSInfo;

	DBGPRINT(RT_DEBUG_TRACE, ("===> %s\n", __FUNCTION__));

	/* init */
	pFirmwareImage = NULL;
	src = RTMP_FIRMWARE_FILE_NAME;

	RtmpOSFSInfoChange(&osFSInfo, TRUE);

	pAd->FirmwareVersion = (FIRMWARE_MAJOR_VERSION << 8) + \
						   FIRMWARE_MINOR_VERSION;


	/* allocate firmware buffer */
/*	pFirmwareImage = kmalloc(MAX_FIRMWARE_IMAGE_SIZE, MEM_ALLOC_FLAG);*/
	os_alloc_mem(pAd, (UCHAR **)&pFirmwareImage, MAX_FIRMWARE_IMAGE_SIZE);
	if (pFirmwareImage == NULL)
	{
		/* allocate fail, use default firmware array in firmware.h */
		DBGPRINT(RT_DEBUG_ERROR, ("%s - Allocate memory fail!\n", __FUNCTION__));
		NICLF_DEFAULT_USE();
	}
	else
	{
		/* allocate ok! zero the firmware buffer */
		memset(pFirmwareImage, 0x00, MAX_FIRMWARE_IMAGE_SIZE);
	} /* End of if */


	/* if ok, read firmware file from *.bin file */
	if (flg_default_firm_use == FALSE)
	{
		do
		{
			/* open the bin file */
			srcf = RtmpOSFileOpen(src, O_RDONLY, 0);

			if (IS_FILE_OPEN_ERR(srcf)) 
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s - Error opening file %s\n", __FUNCTION__, src));
				NICLF_DEFAULT_USE();
				break;
			}


			/* read the firmware from the file *.bin */
			FileLength = RtmpOSFileRead(srcf, pFirmwareImage, MAX_FIRMWARE_IMAGE_SIZE);
			if (FileLength != MAX_FIRMWARE_IMAGE_SIZE)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: error file length (=%d) in RT2860AP.BIN\n",
					   __FUNCTION__, FileLength));
				NICLF_DEFAULT_USE();
				break;
			}
			else
			{
				PUCHAR ptr = pFirmwareImage;
				USHORT crc = 0xffff;


				/* calculate firmware CRC */
				for(i=0; i<(MAX_FIRMWARE_IMAGE_SIZE-2); i++, ptr++)
					crc = ByteCRC16(BitReverse(*ptr), crc);
				/* End of for */

				if ((pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-2] != \
								(UCHAR)BitReverse((UCHAR)(crc>>8))) ||
					(pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-1] != \
								(UCHAR)BitReverse((UCHAR)crc)))
				{
					/* CRC fail */
					DBGPRINT(RT_DEBUG_ERROR, ("%s: CRC = 0x%02x 0x%02x "
						   "error, should be 0x%02x 0x%02x\n",
						   __FUNCTION__,
						   pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-2],
						   pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-1],
						   (UCHAR)(crc>>8), (UCHAR)(crc)));
					NICLF_DEFAULT_USE();
					break;
				}
				else
				{
					/* firmware is ok */
					pAd->FirmwareVersion = \
						(pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-4] << 8) +
						pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-3];

					/* check if firmware version of the file is too old */
					if ((pAd->FirmwareVersion) < \
											((FIRMWARE_MAJOR_VERSION << 8) +
									  	 	 FIRMWARE_MINOR_VERSION))
					{
						DBGPRINT(RT_DEBUG_ERROR, ("%s: firmware version too old!\n", __FUNCTION__));
						NICLF_DEFAULT_USE();
						break;
					} /* End of if */
				} /* End of if */

				DBGPRINT(RT_DEBUG_TRACE,
						 ("NICLoadFirmware: CRC ok, ver=%d.%d\n",
						  pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-4],
						  pFirmwareImage[MAX_FIRMWARE_IMAGE_SIZE-3]));
			} /* End of if (FileLength == MAX_FIRMWARE_IMAGE_SIZE) */
			break;
		} while(TRUE);

		/* close firmware file */
		if (IS_FILE_OPEN_ERR(srcf))
			;
		else
		{
			retval = RtmpOSFileClose(srcf);
			if (retval)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("--> Error %d closing %s\n", -retval, src));
			}
		}
	}


	/* write firmware to ASIC */
	if (flg_default_firm_use == TRUE)
	{
		/* use default fimeware, free allocated buffer */
		if (pFirmwareImage != NULL)
/*			kfree(pFirmwareImage);*/
			os_free_mem(NULL, pFirmwareImage);
		/* End of if */

		/* use default *.bin array */
		pFirmwareImage = FirmwareImage;
		FileLength = sizeof(FirmwareImage);
	} /* End of if */

	/* enable Host program ram write selection */
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0x10000); 

	for(i=0; i<FileLength; i+=4)
	{
		firm = pFirmwareImage[i] +
			   (pFirmwareImage[i+3] << 24) +
			   (pFirmwareImage[i+2] << 16) +
			   (pFirmwareImage[i+1] << 8);

		RTMP_IO_WRITE32(pAd, FIRMWARE_IMAGE_BASE + i, firm);
	} /* End of for */

	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0x00000);
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0x00001);

	/* initialize BBP R/W access agent */
	RTMP_IO_WRITE32(pAd, H2M_BBP_AGENT, 0);
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, 0);

	if (flg_default_firm_use == FALSE)
	{
		/* use file firmware, free allocated buffer */
		if (pFirmwareImage != NULL)
/*			kfree(pFirmwareImage);*/
			os_free_mem(NULL, pFirmwareImage);
		/* End of if */
	} /* End of if */

	RtmpOSFSInfoChange(&osFSInfo, FALSE);
#else

	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	PUCHAR			pFirmwareImage;
	ULONG			FileLength, Index;
	/*ULONG			firm;*/
	UINT32			MacReg = 0;
	UINT32			Version = (pAd->MACVersion >> 16);

	pFirmwareImage = FirmwareImage;
	FileLength = sizeof(FirmwareImage);

	/* New 8k byte firmware size for RT3071/RT3072*/
	/*DBGPRINT(RT_DEBUG_TRACE, ("Usb Chip\n"));*/
	if (FIRMWAREIMAGE_LENGTH == FIRMWAREIMAGE_MAX_LENGTH)
	/*The firmware image consists of two parts. One is the origianl and the other is the new.*/
	/*Use Second Part*/
	{
#ifdef RTMP_MAC_USB
		if ((Version != 0x2860) && (Version != 0x2872) && (Version != 0x3070)) 
		{	/* Use Firmware V2.*/
			/*printk("KH:Use New Version,part2\n");*/
			pFirmwareImage = (PUCHAR)&FirmwareImage[FIRMWAREIMAGEV1_LENGTH];
			FileLength = FIRMWAREIMAGEV2_LENGTH;
		}
		else
		{
			/*printk("KH:Use New Version,part1\n");*/
			pFirmwareImage = FirmwareImage;
			FileLength = FIRMWAREIMAGEV1_LENGTH;
		}
#endif /* RTMP_MAC_USB */
	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("KH: bin file should be 8KB.\n"));
		Status = NDIS_STATUS_FAILURE;
	}


#ifdef RTMP_MAC_USB
	if (isMCUNeedToLoadFIrmware(pAd))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("NICLoadFirmware: We need to load firmware\n"));
		RTMP_WRITE_FIRMWARE(pAd, pFirmwareImage, FileLength);
	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("NICLoadFirmware: firmware loaded already\n"));
	}
#else
	RTMP_WRITE_FIRMWARE(pAd, pFirmwareImage, FileLength);
#endif /* end of RTMP_MAC_USB */

#endif

	/* check if MCU is ready */
	if (isMCUnotReady(pAd))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("NICLoadFirmware: MCU is not ready\n"));
		Status = NDIS_STATUS_FAILURE;
		return Status;
	}
#ifdef RTMP_USB_SUPPORT
	else
	{
		RTUSBWriteMACRegister(pAd, H2M_BBP_AGENT, 0); /* initialize BBP R/W access agent. */
		RTUSBWriteMACRegister(pAd,H2M_MAILBOX_CSR,0);
		RTUSBWriteMACRegister(pAd, H2M_INT_SRC, 0);
		AsicSendCommandToMcu(pAd, 0x72, 0x00, 0x00, 0x00); /* reset rf by MCU supported by new firmware */
	}
#endif /* RTMP_USB_SUPPORT */

    DBGPRINT(RT_DEBUG_TRACE, ("<=== %s (status=%d)\n", __FUNCTION__, Status));

    return Status;
}


INT RtmpAsicSendCommandToMcu(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Command,
	IN UCHAR			Token,
	IN UCHAR			Arg0,
	IN UCHAR			Arg1,
	IN BOOLEAN			FlgIsNeedLocked)
{
	HOST_CMD_CSR_STRUC	H2MCmd;
	H2M_MAILBOX_STRUC	H2MMailbox;
	ULONG				i = 0;
	POS_COOKIE pObj;
	ULONG	IrqFlags = 0;
#ifdef CONFIG_STA_SUPPORT
#ifdef PCIE_PS_SUPPORT
	ULONG	Configuration;
	ULONG	offset;
#endif /* PCIE_PS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */


#ifdef CONFIG_STA_SUPPORT
#ifdef PCIE_PS_SUPPORT
	pObj = (POS_COOKIE) pAd->OS_Cookie;



	/* 3090F power solution 3 has hw limitation that needs to ban all mcu command */
	/* when firmware is in radio state.  For other chip doesn't have this limitation. */
	if ((IS_RT3090(pAd) || IS_RT3572(pAd) ||
		IS_RT3390(pAd) || IS_RT3593(pAd) || IS_RT5390(pAd)) && IS_VERSION_AFTER_F(pAd)
		&& (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3) 
		&& (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE))
	{

		RTMP_SEM_LOCK(&pAd->McuCmdLock);

		if ((pAd->brt30xxBanMcuCmd == TRUE)
			&& (Command != WAKE_MCU_CMD) && (Command != RFOFF_MCU_CMD))
		{

			DBGPRINT(RT_DEBUG_TRACE, (" Ban Mcu Cmd %x in sleep mode\n",  Command));
			RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
			return FALSE;
		}
		else if ((Command == SLEEP_MCU_CMD)
			||(Command == RFOFF_MCU_CMD))
		{
			pAd->brt30xxBanMcuCmd = TRUE;
		}
		else if (Command != WAKE_MCU_CMD)
		{
			pAd->brt30xxBanMcuCmd = FALSE;
		}

		RTMP_SEM_UNLOCK(&pAd->McuCmdLock);


	}


	if ((IS_RT3090(pAd) || IS_RT3572(pAd) ||
		IS_RT3390(pAd) || IS_RT3593(pAd) || IS_RT5390(pAd)) && IS_VERSION_AFTER_F(pAd)
		&& (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3) 
		&& (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)
		&& (Command == WAKE_MCU_CMD))
	{

#ifdef SPECIFIC_BCN_BUF_SUPPORT
		if (FlgIsNeedLocked == TRUE)
		{
		           RTMP_MAC_SHR_MSEL_PROTECT_LOCK(pAd, IrqFlags);
		}
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

		/* don't check MailBox for 0x84, 0x31*/
		if ((Command != 0x84) && (Command != WAKE_MCU_CMD))
		{
			do
			{
				RTMP_IO_FORCE_READ32(pAd, H2M_MAILBOX_CSR, &H2MMailbox.word);
				if (H2MMailbox.field.Owner == 0)
					break;

				if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))				
				{
#ifdef SPECIFIC_BCN_BUF_SUPPORT	
					if (FlgIsNeedLocked == TRUE)
						RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
					return FALSE;
				}
				RTMPusecDelay(2);
				DBGPRINT(RT_DEBUG_INFO, ("AsicSendCommanToMcu::Mail box is busy\n"));
			} while(i++ < 100);

			if (i >= 100)
			{
				DBGPRINT_ERR(("H2M_MAILBOX still hold by MCU. command fail\n"));
#ifdef SPECIFIC_BCN_BUF_SUPPORT	
				if (FlgIsNeedLocked == TRUE)
					RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

					return FALSE;
			}
		}

		H2MMailbox.field.Owner	  = 1;	   /* pass ownership to MCU*/
		H2MMailbox.field.CmdToken = Token;
		H2MMailbox.field.HighByte = Arg1;
		H2MMailbox.field.LowByte  = Arg0;
		RTMP_IO_FORCE_WRITE32(pAd, H2M_MAILBOX_CSR, H2MMailbox.word);

		H2MCmd.word 			  = 0;
		H2MCmd.field.HostCommand  = Command;
		RTMP_IO_FORCE_WRITE32(pAd, HOST_CMD_CSR, H2MCmd.word);
#ifdef SPECIFIC_BCN_BUF_SUPPORT
			if (FlgIsNeedLocked == TRUE)
				RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */


	}
	else
#endif /* PCIE_PS_SUPPORT */
#endif /* CONFIG_STA_SUPPORT */
	{

#ifdef SPECIFIC_BCN_BUF_SUPPORT
		if (FlgIsNeedLocked == TRUE)
		{
		           RTMP_MAC_SHR_MSEL_PROTECT_LOCK(pAd, IrqFlags);
		}
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

	do
	{
		RTMP_IO_READ32(pAd, H2M_MAILBOX_CSR, &H2MMailbox.word);
		if (H2MMailbox.field.Owner == 0)
			break;

		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))				
		{
#ifdef SPECIFIC_BCN_BUF_SUPPORT	
			if (FlgIsNeedLocked == TRUE)
				RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
			return FALSE;
		}
		RTMPusecDelay(2);
	} while(i++ < 100);

	if (i >= 100)
	{
		{
		DBGPRINT_ERR(("H2M_MAILBOX still hold by MCU. command fail\n"));
		}
#ifdef SPECIFIC_BCN_BUF_SUPPORT	
		if (FlgIsNeedLocked == TRUE)
			RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
		return FALSE;
	}


	H2MMailbox.field.Owner	  = 1;	   /* pass ownership to MCU*/
	H2MMailbox.field.CmdToken = Token;
	H2MMailbox.field.HighByte = Arg1;
	H2MMailbox.field.LowByte  = Arg0;
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, H2MMailbox.word);

	H2MCmd.word 			  = 0;
	H2MCmd.field.HostCommand  = Command;
	RTMP_IO_WRITE32(pAd, HOST_CMD_CSR, H2MCmd.word);

#ifdef SPECIFIC_BCN_BUF_SUPPORT
			if (FlgIsNeedLocked == TRUE)
				RTMP_MAC_SHR_MSEL_PROTECT_UNLOCK(pAd, IrqFlags);
#endif // SPECIFIC_BCN_BUF_SUPPORT //

	if (Command != 0x80)
	{
	}
}

#ifdef CONFIG_STA_SUPPORT
#ifdef PCIE_PS_SUPPORT
	/* 3090 MCU Wakeup command needs more time to be stable. */
	/* Before stable, don't issue other MCU command to prevent from firmware error.*/
	if ((IS_RT3090(pAd) || IS_RT3572(pAd)
		|| IS_RT3390(pAd) || IS_RT3593(pAd) || IS_RT5390(pAd)) && IS_VERSION_AFTER_F(pAd)
		&& (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3) 
		&& (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE))
	{
		/*Put this is after RF programming.  */
		/*NdisAcquireSpinLock(&pAd->McuCmdLock);*/
		/*pAd->brt30xxBanMcuCmd = FALSE;*/
		/*NdisReleaseSpinLock(&pAd->McuCmdLock);*/
		switch (Command)
		{
			case WAKE_MCU_CMD :
				RTMPusecDelay(2500);

				if ((pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3) &&
					(pAd->StaCfg.PSControl.field.EnableNewPS == TRUE))
				{
					/* Put this is after RF program. */ 
					pAd->brt30xxBanMcuCmd = FALSE;
				}
				break;
			case SLEEP_MCU_CMD :
				RTMPusecDelay(2000);
				break;
	}
	}

#endif /* PCIE_PS_SUPPORT */	
#endif /* CONFIG_STA_SUPPORT */

	if (Command == WAKE_MCU_CMD)
		pAd->LastMCUCmd = Command;

	return TRUE;


}
