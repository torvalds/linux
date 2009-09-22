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
	rtmp_mcu.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/


#include	"../rt_config.h"

#ifdef RT2860
#include "firmware.h"
#endif
#ifdef RT2870
#include "../../rt3070/firmware.h"
#include "firmware_3070.h"
#endif

#include <linux/bitrev.h>

//#define BIN_IN_FILE /* use *.bin firmware */

#ifdef RTMP_MAC_USB
//
// RT2870 Firmware Spec only used 1 oct for version expression
//
#define FIRMWARE_MINOR_VERSION	7
#endif // RTMP_MAC_USB //

// New 8k byte firmware size for RT3071/RT3072
#define FIRMWAREIMAGE_MAX_LENGTH	0x2000
#define FIRMWAREIMAGE_LENGTH			(sizeof (FirmwareImage) / sizeof(UCHAR))
#define FIRMWARE_MAJOR_VERSION		0

#define FIRMWAREIMAGEV1_LENGTH		0x1000
#define FIRMWAREIMAGEV2_LENGTH		0x1000

#ifdef RTMP_MAC_PCI
#define FIRMWARE_MINOR_VERSION		2
#endif // RTMP_MAC_PCI //

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

	NDIS_STATUS		Status = NDIS_STATUS_SUCCESS;
	PUCHAR			pFirmwareImage;
	ULONG			FileLength, Index;
	//ULONG			firm;
	UINT32			MacReg = 0;
	UINT32			Version = (pAd->MACVersion >> 16);

//	pFirmwareImage = FirmwareImage;
//	FileLength = sizeof(FirmwareImage);

	// New 8k byte firmware size for RT3071/RT3072
	{
#ifdef RTMP_MAC_PCI
		if ((Version == 0x2860) || (Version == 0x3572) || IS_RT3090(pAd))
		{
			pFirmwareImage = FirmwareImage_2860;
			FileLength = FIRMWAREIMAGE_MAX_LENGTH;
		}
#endif // RTMP_MAC_PCI //
#ifdef RTMP_MAC_USB
		/* the firmware image consists of two parts */
		if ((Version != 0x2860) && (Version != 0x2872) && (Version != 0x3070))
		{	/* use the second part */
			//printk("KH:Use New Version,part2\n");
			pFirmwareImage = (PUCHAR)&FirmwareImage_3070[FIRMWAREIMAGEV1_LENGTH];
			FileLength = FIRMWAREIMAGEV2_LENGTH;
		}
		else
		{
			//printk("KH:Use New Version,part1\n");
			if (Version == 0x3070)
				pFirmwareImage = FirmwareImage_3070;
			else
				pFirmwareImage = FirmwareImage_2870;
			FileLength = FIRMWAREIMAGEV1_LENGTH;
		}
#endif // RTMP_MAC_USB //
	}

	RTMP_WRITE_FIRMWARE(pAd, pFirmwareImage, FileLength);


	/* check if MCU is ready */
	Index = 0;
	do
	{
		RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &MacReg);

		if (MacReg & 0x80)
			break;

		RTMPusecDelay(1000);
	} while (Index++ < 1000);

	if (Index > 1000)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("NICLoadFirmware: MCU is not ready\n\n\n"));
		Status = NDIS_STATUS_FAILURE;
	}

    DBGPRINT(RT_DEBUG_TRACE, ("<=== %s (status=%d)\n", __func__, Status));

    return Status;
}


INT RtmpAsicSendCommandToMcu(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Command,
	IN UCHAR		 Token,
	IN UCHAR		 Arg0,
	IN UCHAR		 Arg1)
{
	HOST_CMD_CSR_STRUC	H2MCmd;
	H2M_MAILBOX_STRUC	H2MMailbox;
	ULONG				i = 0;
#ifdef RTMP_MAC_PCI
#endif // RTMP_MAC_PCI //

	do
	{
		RTMP_IO_READ32(pAd, H2M_MAILBOX_CSR, &H2MMailbox.word);
		if (H2MMailbox.field.Owner == 0)
			break;

		RTMPusecDelay(2);
	} while(i++ < 100);

	if (i > 100)
	{
#ifdef RTMP_MAC_PCI
#endif // RTMP_MAC_PCI //
		{
		DBGPRINT_ERR(("H2M_MAILBOX still hold by MCU. command fail\n"));
		}
		return FALSE;
	}

#ifdef RTMP_MAC_PCI
#endif // RTMP_MAC_PCI //

	H2MMailbox.field.Owner	  = 1;	   // pass ownership to MCU
	H2MMailbox.field.CmdToken = Token;
	H2MMailbox.field.HighByte = Arg1;
	H2MMailbox.field.LowByte  = Arg0;
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, H2MMailbox.word);

	H2MCmd.word			  = 0;
	H2MCmd.field.HostCommand  = Command;
	RTMP_IO_WRITE32(pAd, HOST_CMD_CSR, H2MCmd.word);

	if (Command != 0x80)
	{
	}

	return TRUE;
}
