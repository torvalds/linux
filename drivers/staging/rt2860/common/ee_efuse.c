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
	ee_efuse.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/

#include	"../rt_config.h"

#define EFUSE_USAGE_MAP_START	0x2d0
#define EFUSE_USAGE_MAP_END		0x2fc
#define EFUSE_USAGE_MAP_SIZE	45

#define EFUSE_EEPROM_DEFULT_FILE	"RT30xxEEPROM.bin"
#define MAX_EEPROM_BIN_FILE_SIZE	1024

#define EFUSE_TAG				0x2fe

typedef union _EFUSE_CTRL_STRUC {
	struct {
		u32 EFSROM_AOUT:6;
		u32 EFSROM_MODE:2;
		u32 EFSROM_LDO_OFF_TIME:6;
		u32 EFSROM_LDO_ON_TIME:2;
		u32 EFSROM_AIN:10;
		u32 RESERVED:4;
		u32 EFSROM_KICK:1;
		u32 SEL_EFUSE:1;
	} field;
	u32 word;
} EFUSE_CTRL_STRUC, *PEFUSE_CTRL_STRUC;

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
u8 eFuseReadRegisters(struct rt_rtmp_adapter *pAd,
			 u16 Offset, u16 Length, u16 * pData)
{
	EFUSE_CTRL_STRUC eFuseCtrlStruc;
	int i;
	u16 efuseDataOffset;
	u32 data;

	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

	/*Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment. */
	/*Use the eeprom logical address and covert to address to block number */
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 0. */
	eFuseCtrlStruc.field.EFSROM_MODE = 0;

	/*Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure. */
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	/*Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. */
	i = 0;
	while (i < 500) {
		/*rtmp.HwMemoryReadDword(EFUSE_CTRL, (DWORD *) &eFuseCtrlStruc, 4); */
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		if (eFuseCtrlStruc.field.EFSROM_KICK == 0) {
			break;
		}
		RTMPusecDelay(2);
		i++;
	}

	/*if EFSROM_AOUT is not found in physical address, write 0xffff */
	if (eFuseCtrlStruc.field.EFSROM_AOUT == 0x3f) {
		for (i = 0; i < Length / 2; i++)
			*(pData + 2 * i) = 0xffff;
	} else {
		/*Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x590-0x59C) */
		efuseDataOffset = EFUSE_DATA3 - (Offset & 0xC);
		/*data hold 4 bytes data. */
		/*In RTMP_IO_READ32 will automatically execute 32-bytes swapping */
		RTMP_IO_READ32(pAd, efuseDataOffset, &data);
		/*Decide the upper 2 bytes or the bottom 2 bytes. */
		/* Little-endian                S       |       S       Big-endian */
		/* addr 3       2       1       0       |       0       1       2       3 */
		/* Ori-V        D       C       B       A       |       A       B       C       D */
		/*After swapping */
		/*              D       C       B       A       |       D       C       B       A */
		/*Return 2-bytes */
		/*The return byte statrs from S. Therefore, the little-endian will return BA, the Big-endian will return DC. */
		/*For returning the bottom 2 bytes, the Big-endian should shift right 2-bytes. */
		data = data >> (8 * (Offset & 0x3));

		NdisMoveMemory(pData, &data, Length);
	}

	return (u8)eFuseCtrlStruc.field.EFSROM_AOUT;

}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
void eFusePhysicalReadRegisters(struct rt_rtmp_adapter *pAd,
				u16 Offset,
				u16 Length, u16 * pData)
{
	EFUSE_CTRL_STRUC eFuseCtrlStruc;
	int i;
	u16 efuseDataOffset;
	u32 data;

	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

	/*Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment. */
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	/*Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 1. */
	/*Read in physical view */
	eFuseCtrlStruc.field.EFSROM_MODE = 1;

	/*Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure. */
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	/*Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. */
	i = 0;
	while (i < 500) {
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		if (eFuseCtrlStruc.field.EFSROM_KICK == 0)
			break;
		RTMPusecDelay(2);
		i++;
	}

	/*Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590) */
	/*Because the size of each EFUSE_DATA is 4 Bytes, the size of address of each is 2 bits. */
	/*The previous 2 bits is the EFUSE_DATA number, the last 2 bits is used to decide which bytes */
	/*Decide which EFUSE_DATA to read */
	/*590:F E D C */
	/*594:B A 9 8 */
	/*598:7 6 5 4 */
	/*59C:3 2 1 0 */
	efuseDataOffset = EFUSE_DATA3 - (Offset & 0xC);

	RTMP_IO_READ32(pAd, efuseDataOffset, &data);

	data = data >> (8 * (Offset & 0x3));

	NdisMoveMemory(pData, &data, Length);

}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
static void eFuseReadPhysical(struct rt_rtmp_adapter *pAd,
			      u16 *lpInBuffer,
			      unsigned long nInBufferSize,
			      u16 *lpOutBuffer, unsigned long nOutBufferSize)
{
	u16 *pInBuf = (u16 *) lpInBuffer;
	u16 *pOutBuf = (u16 *) lpOutBuffer;

	u16 Offset = pInBuf[0];	/*addr */
	u16 Length = pInBuf[1];	/*length */
	int i;

	for (i = 0; i < Length; i += 2) {
		eFusePhysicalReadRegisters(pAd, Offset + i, 2, &pOutBuf[i / 2]);
	}
}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
int set_eFuseGetFreeBlockCount_Proc(struct rt_rtmp_adapter *pAd, char *arg)
{
	u16 i;
	u16 LogicalAddress;
	u16 efusefreenum = 0;
	if (!pAd->bUseEfuse)
		return FALSE;
	for (i = EFUSE_USAGE_MAP_START; i <= EFUSE_USAGE_MAP_END; i += 2) {
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
		if ((LogicalAddress & 0xff) == 0) {
			efusefreenum = (u8)(EFUSE_USAGE_MAP_END - i + 1);
			break;
		} else if (((LogicalAddress >> 8) & 0xff) == 0) {
			efusefreenum = (u8)(EFUSE_USAGE_MAP_END - i);
			break;
		}

		if (i == EFUSE_USAGE_MAP_END)
			efusefreenum = 0;
	}
	printk("efuseFreeNumber is %d\n", efusefreenum);
	return TRUE;
}

int set_eFusedump_Proc(struct rt_rtmp_adapter *pAd, char *arg)
{
	u16 InBuf[3];
	int i = 0;
	if (!pAd->bUseEfuse)
		return FALSE;
	for (i = 0; i < EFUSE_USAGE_MAP_END / 2; i++) {
		InBuf[0] = 2 * i;
		InBuf[1] = 2;
		InBuf[2] = 0x0;

		eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);
		if (i % 4 == 0)
			printk("\nBlock %x:", i / 8);
		printk("%04x ", InBuf[2]);
	}
	return TRUE;
}

int rtmp_ee_efuse_read16(struct rt_rtmp_adapter *pAd,
			 u16 Offset, u16 * pValue)
{
	eFuseReadRegisters(pAd, Offset, 2, pValue);
	return (*pValue);
}

int RtmpEfuseSupportCheck(struct rt_rtmp_adapter *pAd)
{
	u16 value;

	if (IS_RT30xx(pAd)) {
		eFusePhysicalReadRegisters(pAd, EFUSE_TAG, 2, &value);
		pAd->EFuseTag = (value & 0xff);
	}
	return 0;
}

void eFuseGetFreeBlockCount(struct rt_rtmp_adapter *pAd, u32 *EfuseFreeBlock)
{
	u16 i;
	u16 LogicalAddress;
	if (!pAd->bUseEfuse) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("eFuseGetFreeBlockCount Only supports efuse Mode\n"));
		return;
	}
	for (i = EFUSE_USAGE_MAP_START; i <= EFUSE_USAGE_MAP_END; i += 2) {
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
		if ((LogicalAddress & 0xff) == 0) {
			*EfuseFreeBlock = (u8)(EFUSE_USAGE_MAP_END - i + 1);
			break;
		} else if (((LogicalAddress >> 8) & 0xff) == 0) {
			*EfuseFreeBlock = (u8)(EFUSE_USAGE_MAP_END - i);
			break;
		}

		if (i == EFUSE_USAGE_MAP_END)
			*EfuseFreeBlock = 0;
	}
	DBGPRINT(RT_DEBUG_TRACE,
		 ("eFuseGetFreeBlockCount is 0x%x\n", *EfuseFreeBlock));
}

int eFuse_init(struct rt_rtmp_adapter *pAd)
{
	u32 EfuseFreeBlock = 0;
	DBGPRINT(RT_DEBUG_ERROR,
		 ("NVM is Efuse and its size =%x[%x-%x] \n",
		  EFUSE_USAGE_MAP_SIZE, EFUSE_USAGE_MAP_START,
		  EFUSE_USAGE_MAP_END));
	eFuseGetFreeBlockCount(pAd, &EfuseFreeBlock);

	return 0;
}
