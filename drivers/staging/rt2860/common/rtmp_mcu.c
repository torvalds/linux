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

#if defined(RT2860) || defined(RT3090)
#include "firmware.h"
#include "../../rt3090/firmware.h"
#endif
#ifdef RT2870
#include "../../rt3070/firmware.h"
#include "firmware_3070.h"
#endif

#include <linux/bitrev.h>

#ifdef RTMP_MAC_USB
/* */
/* RT2870 Firmware Spec only used 1 oct for version expression */
/* */
#define FIRMWARE_MINOR_VERSION	7
#endif /* RTMP_MAC_USB // */

/* New 8k byte firmware size for RT3071/RT3072 */
#define FIRMWAREIMAGE_MAX_LENGTH	0x2000
#define FIRMWAREIMAGE_LENGTH			(sizeof (FirmwareImage) / sizeof(u8))
#define FIRMWARE_MAJOR_VERSION		0

#define FIRMWAREIMAGEV1_LENGTH		0x1000
#define FIRMWAREIMAGEV2_LENGTH		0x1000

#ifdef RTMP_MAC_PCI
#define FIRMWARE_MINOR_VERSION		2
#endif /* RTMP_MAC_PCI // */

/*
	========================================================================

	Routine Description:
		erase 8051 firmware image in MAC ASIC

	Arguments:
		Adapter						Pointer to our adapter

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
int RtmpAsicEraseFirmware(struct rt_rtmp_adapter *pAd)
{
	unsigned long i;

	for (i = 0; i < MAX_FIRMWARE_IMAGE_SIZE; i += 4)
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
int RtmpAsicLoadFirmware(struct rt_rtmp_adapter *pAd)
{

	int Status = NDIS_STATUS_SUCCESS;
	u8 *pFirmwareImage = NULL;
	unsigned long FileLength, Index;
	u32 MacReg = 0;
#ifdef RTMP_MAC_USB
	u32 Version = (pAd->MACVersion >> 16);
#endif

	/* New 8k byte firmware size for RT3071/RT3072 */
	{
#ifdef RTMP_MAC_PCI
		if (IS_RT3090(pAd) || IS_RT3390(pAd)) {
			pFirmwareImage = FirmwareImage_3090;
			FileLength = FIRMWAREIMAGE_MAX_LENGTH;
		} else {
			pFirmwareImage = FirmwareImage_2860;
			FileLength = FIRMWAREIMAGE_MAX_LENGTH;
		}
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
		/* the firmware image consists of two parts */
		if ((Version != 0x2860) && (Version != 0x2872) && (Version != 0x3070)) {	/* use the second part */
			/*printk("KH:Use New Version,part2\n"); */
			pFirmwareImage =
			    (u8 *)&
			    FirmwareImage_3070[FIRMWAREIMAGEV1_LENGTH];
			FileLength = FIRMWAREIMAGEV2_LENGTH;
		} else {
			/*printk("KH:Use New Version,part1\n"); */
			if (Version == 0x3070)
				pFirmwareImage = FirmwareImage_3070;
			else
				pFirmwareImage = FirmwareImage_2870;
			FileLength = FIRMWAREIMAGEV1_LENGTH;
		}
#endif /* RTMP_MAC_USB // */
	}

	RTMP_WRITE_FIRMWARE(pAd, pFirmwareImage, FileLength);

	/* check if MCU is ready */
	Index = 0;
	do {
		RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &MacReg);

		if (MacReg & 0x80)
			break;

		RTMPusecDelay(1000);
	} while (Index++ < 1000);

	if (Index > 1000) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("NICLoadFirmware: MCU is not ready\n"));
		Status = NDIS_STATUS_FAILURE;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<=== %s (status=%d)\n", __func__, Status));

	return Status;
}

int RtmpAsicSendCommandToMcu(struct rt_rtmp_adapter *pAd,
			     u8 Command,
			     u8 Token, u8 Arg0, u8 Arg1)
{
	HOST_CMD_CSR_STRUC H2MCmd;
	H2M_MAILBOX_STRUC H2MMailbox;
	unsigned long i = 0;

#ifdef PCIE_PS_SUPPORT
	/* 3090F power solution 3 has hw limitation that needs to ban all mcu command */
	/* when firmware is in radio state.  For other chip doesn't have this limitation. */
	if (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	     && IS_VERSION_AFTER_F(pAd)) && IS_VERSION_AFTER_F(pAd)
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
		RTMP_SEM_LOCK(&pAd->McuCmdLock);
		if ((pAd->brt30xxBanMcuCmd == TRUE)
		    && (Command != WAKE_MCU_CMD) && (Command != RFOFF_MCU_CMD)) {
			RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
			DBGPRINT(RT_DEBUG_TRACE,
				 (" Ban Mcu Cmd %x in sleep mode\n", Command));
			return FALSE;
		} else if ((Command == SLEEP_MCU_CMD)
			   || (Command == RFOFF_MCU_CMD)) {
			pAd->brt30xxBanMcuCmd = TRUE;
		} else if (Command != WAKE_MCU_CMD) {
			pAd->brt30xxBanMcuCmd = FALSE;
		}

		RTMP_SEM_UNLOCK(&pAd->McuCmdLock);

	}
	if (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	     && IS_VERSION_AFTER_F(pAd)) && IS_VERSION_AFTER_F(pAd)
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)
	    && (Command == WAKE_MCU_CMD)) {

		do {
			RTMP_IO_FORCE_READ32(pAd, H2M_MAILBOX_CSR,
					     &H2MMailbox.word);
			if (H2MMailbox.field.Owner == 0)
				break;

			RTMPusecDelay(2);
			DBGPRINT(RT_DEBUG_INFO,
				 ("AsicSendCommanToMcu::Mail box is busy\n"));
		} while (i++ < 100);

		if (i >= 100) {
			DBGPRINT_ERR(("H2M_MAILBOX still hold by MCU. command fail\n"));
			return FALSE;
		}

		H2MMailbox.field.Owner = 1;	/* pass ownership to MCU */
		H2MMailbox.field.CmdToken = Token;
		H2MMailbox.field.HighByte = Arg1;
		H2MMailbox.field.LowByte = Arg0;
		RTMP_IO_FORCE_WRITE32(pAd, H2M_MAILBOX_CSR, H2MMailbox.word);

		H2MCmd.word = 0;
		H2MCmd.field.HostCommand = Command;
		RTMP_IO_FORCE_WRITE32(pAd, HOST_CMD_CSR, H2MCmd.word);

	} else
#endif /* PCIE_PS_SUPPORT // */
	{
		do {
			RTMP_IO_READ32(pAd, H2M_MAILBOX_CSR, &H2MMailbox.word);
			if (H2MMailbox.field.Owner == 0)
				break;

			RTMPusecDelay(2);
		} while (i++ < 100);

		if (i > 100) {
#ifdef RTMP_MAC_PCI
#endif /* RTMP_MAC_PCI // */
			{
				DBGPRINT_ERR(("H2M_MAILBOX still hold by MCU. command fail\n"));
			}
			return FALSE;
		}
#ifdef RTMP_MAC_PCI
#endif /* RTMP_MAC_PCI // */

		H2MMailbox.field.Owner = 1;	/* pass ownership to MCU */
		H2MMailbox.field.CmdToken = Token;
		H2MMailbox.field.HighByte = Arg1;
		H2MMailbox.field.LowByte = Arg0;
		RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, H2MMailbox.word);

		H2MCmd.word = 0;
		H2MCmd.field.HostCommand = Command;
		RTMP_IO_WRITE32(pAd, HOST_CMD_CSR, H2MCmd.word);

		if (Command != 0x80) {
		}
	}
#ifdef PCIE_PS_SUPPORT
	/* 3090 MCU Wakeup command needs more time to be stable. */
	/* Before stable, don't issue other MCU command to prevent from firmware error. */
	if (((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	     && IS_VERSION_AFTER_F(pAd)) && IS_VERSION_AFTER_F(pAd)
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)
	    && (Command == WAKE_MCU_CMD)) {
		RTMPusecDelay(2000);
		/*Put this is after RF programming. */
		/*NdisAcquireSpinLock(&pAd->McuCmdLock); */
		/*pAd->brt30xxBanMcuCmd = FALSE; */
		/*NdisReleaseSpinLock(&pAd->McuCmdLock); */
	}
#endif /* PCIE_PS_SUPPORT // */

	return TRUE;
}
