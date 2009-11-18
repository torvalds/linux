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

#include "../rt_config.h"


#define EFUSE_USAGE_MAP_START	0x2d0
#define EFUSE_USAGE_MAP_END		0x2fc
#define EFUSE_USAGE_MAP_SIZE	45



#define EFUSE_EEPROM_DEFULT_FILE	"RT30xxEEPROM.bin"
#define MAX_EEPROM_BIN_FILE_SIZE	1024



#define EFUSE_TAG				0x2fe


#ifdef RT_BIG_ENDIAN
typedef	union	_EFUSE_CTRL_STRUC {
	struct	{
		UINT32            SEL_EFUSE:1;
		UINT32            EFSROM_KICK:1;
		UINT32            RESERVED:4;
		UINT32            EFSROM_AIN:10;
		UINT32            EFSROM_LDO_ON_TIME:2;
		UINT32            EFSROM_LDO_OFF_TIME:6;
		UINT32            EFSROM_MODE:2;
		UINT32            EFSROM_AOUT:6;
	}	field;
	UINT32			word;
}	EFUSE_CTRL_STRUC, *PEFUSE_CTRL_STRUC;
#else
typedef	union	_EFUSE_CTRL_STRUC {
	struct	{
		UINT32            EFSROM_AOUT:6;
		UINT32            EFSROM_MODE:2;
		UINT32            EFSROM_LDO_OFF_TIME:6;
		UINT32            EFSROM_LDO_ON_TIME:2;
		UINT32            EFSROM_AIN:10;
		UINT32            RESERVED:4;
		UINT32            EFSROM_KICK:1;
		UINT32            SEL_EFUSE:1;
	}	field;
	UINT32			word;
}	EFUSE_CTRL_STRUC, *PEFUSE_CTRL_STRUC;
#endif // RT_BIG_ENDIAN //

static UCHAR eFuseReadRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData);

static VOID eFuseReadPhysical(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUSHORT lpInBuffer,
	IN	ULONG nInBufferSize,
	OUT	PUSHORT lpOutBuffer,
	IN	ULONG nOutBufferSize);

static VOID eFusePhysicalWriteRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData);

static NTSTATUS eFuseWriteRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData);

static VOID eFuseWritePhysical(
	IN	PRTMP_ADAPTER	pAd,
	PUSHORT lpInBuffer,
	ULONG nInBufferSize,
	PUCHAR lpOutBuffer,
	ULONG nOutBufferSize);


static NTSTATUS eFuseWriteRegistersFromBin(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData);


/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
UCHAR eFuseReadRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset;
	UINT32	data;

	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

	//Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.
	//Use the eeprom logical address and covert to address to block number
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	//Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 0.
	eFuseCtrlStruc.field.EFSROM_MODE = 0;

	//Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	//Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.
	i = 0;
	while(i < 500)
	{
		//rtmp.HwMemoryReadDword(EFUSE_CTRL, (DWORD *) &eFuseCtrlStruc, 4);
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
		{
			break;
		}
		RTMPusecDelay(2);
		i++;
	}

	//if EFSROM_AOUT is not found in physical address, write 0xffff
	if (eFuseCtrlStruc.field.EFSROM_AOUT == 0x3f)
	{
		for(i=0; i<Length/2; i++)
			*(pData+2*i) = 0xffff;
	}
	else
	{
		//Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x590-0x59C)
		efuseDataOffset =  EFUSE_DATA3 - (Offset & 0xC);
		//data hold 4 bytes data.
		//In RTMP_IO_READ32 will automatically execute 32-bytes swapping
		RTMP_IO_READ32(pAd, efuseDataOffset, &data);
		//Decide the upper 2 bytes or the bottom 2 bytes.
		// Little-endian		S	|	S	Big-endian
		// addr	3	2	1	0	|	0	1	2	3
		// Ori-V	D	C	B	A	|	A	B	C	D
		//After swapping
		//		D	C	B	A	|	D	C	B	A
		//Return 2-bytes
		//The return byte statrs from S. Therefore, the little-endian will return BA, the Big-endian will return DC.
		//For returning the bottom 2 bytes, the Big-endian should shift right 2-bytes.
#ifdef RT_BIG_ENDIAN
		data = data << (8*((Offset & 0x3)^0x2));
#else
		data = data >> (8*(Offset & 0x3));
#endif // RT_BIG_ENDIAN //

		NdisMoveMemory(pData, &data, Length);
	}

	return (UCHAR) eFuseCtrlStruc.field.EFSROM_AOUT;

}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
VOID eFusePhysicalReadRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset;
	UINT32	data;

	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

	//Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	//Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 1.
	//Read in physical view
	eFuseCtrlStruc.field.EFSROM_MODE = 1;

	//Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	//Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.
	i = 0;
	while(i < 500)
	{
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
			break;
		RTMPusecDelay(2);
		i++;
	}

	//Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590)
	//Because the size of each EFUSE_DATA is 4 Bytes, the size of address of each is 2 bits.
	//The previous 2 bits is the EFUSE_DATA number, the last 2 bits is used to decide which bytes
	//Decide which EFUSE_DATA to read
	//590:F E D C
	//594:B A 9 8
	//598:7 6 5 4
	//59C:3 2 1 0
	efuseDataOffset =  EFUSE_DATA3 - (Offset & 0xC)  ;

	RTMP_IO_READ32(pAd, efuseDataOffset, &data);

#ifdef RT_BIG_ENDIAN
		data = data << (8*((Offset & 0x3)^0x2));
#else
	data = data >> (8*(Offset & 0x3));
#endif // RT_BIG_ENDIAN //

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
static VOID eFuseReadPhysical(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUSHORT lpInBuffer,
	IN	ULONG nInBufferSize,
	OUT	PUSHORT lpOutBuffer,
	IN	ULONG nOutBufferSize
)
{
	USHORT* pInBuf = (USHORT*)lpInBuffer;
	USHORT* pOutBuf = (USHORT*)lpOutBuffer;

	USHORT Offset = pInBuf[0];					//addr
	USHORT Length = pInBuf[1];					//length
	int		i;

	for(i=0; i<Length; i+=2)
	{
		eFusePhysicalReadRegisters(pAd,Offset+i, 2, &pOutBuf[i/2]);
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
NTSTATUS eFuseRead(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT			Offset,
	OUT	PUCHAR			pData,
	IN	USHORT			Length)
{
	USHORT* pOutBuf = (USHORT*)pData;
	NTSTATUS Status = STATUS_SUCCESS;
	UCHAR	EFSROM_AOUT;
	int	i;

	for(i=0; i<Length; i+=2)
	{
		EFSROM_AOUT = eFuseReadRegisters(pAd, Offset+i, 2, &pOutBuf[i/2]);
	}
	return Status;
}

/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
static VOID eFusePhysicalWriteRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	OUT	USHORT* pData)
{
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	int	i;
	USHORT	efuseDataOffset;
	UINT32	data, eFuseDataBuffer[4];

	//Step0. Write 16-byte of data to EFUSE_DATA0-3 (0x590-0x59C), where EFUSE_DATA0 is the LSB DW, EFUSE_DATA3 is the MSB DW.

	/////////////////////////////////////////////////////////////////
	//read current values of 16-byte block
	RTMP_IO_READ32(pAd, EFUSE_CTRL,  &eFuseCtrlStruc.word);

	//Step0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	//Step1. Write EFSROM_MODE (0x580, bit7:bit6) to 1.
	eFuseCtrlStruc.field.EFSROM_MODE = 1;

	//Step2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	//Step3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.
	i = 0;
	while(i < 500)
	{
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
			break;
		RTMPusecDelay(2);
		i++;
	}

	//Step4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590)
	efuseDataOffset =  EFUSE_DATA3;
	for(i=0; i< 4; i++)
	{
		RTMP_IO_READ32(pAd, efuseDataOffset, (PUINT32) &eFuseDataBuffer[i]);
		efuseDataOffset -=  4;
	}

	//Update the value, the offset is multiple of 2, length is 2
	efuseDataOffset = (Offset & 0xc) >> 2;
	data = pData[0] & 0xffff;
	//The offset should be 0x***10 or 0x***00
	if((Offset % 4) != 0)
	{
		eFuseDataBuffer[efuseDataOffset] = (eFuseDataBuffer[efuseDataOffset] & 0xffff) | (data << 16);
	}
	else
	{
		eFuseDataBuffer[efuseDataOffset] = (eFuseDataBuffer[efuseDataOffset] & 0xffff0000) | data;
	}

	efuseDataOffset =  EFUSE_DATA3;
	for(i=0; i< 4; i++)
	{
		RTMP_IO_WRITE32(pAd, efuseDataOffset, eFuseDataBuffer[i]);
		efuseDataOffset -= 4;
	}
	/////////////////////////////////////////////////////////////////

	//Step1. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.

	RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	//Step2. Write EFSROM_MODE (0x580, bit7:bit6) to 3.
	eFuseCtrlStruc.field.EFSROM_MODE = 3;

	//Step3. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical write procedure.
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	//Step4. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. It��s done.
	i = 0;

	while(i < 500)
	{
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

		if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
			break;

		RTMPusecDelay(2);
		i++;
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
static NTSTATUS eFuseWriteRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData)
{
	USHORT	i,Loop=0;
	USHORT	eFuseData;
	USHORT	LogicalAddress, BlkNum = 0xffff;
	UCHAR	EFSROM_AOUT;

	USHORT addr,tmpaddr, InBuf[3], tmpOffset;
	USHORT buffer[8];
	BOOLEAN		bWriteSuccess = TRUE;

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters Offset=%x, pData=%x\n", Offset, *pData));

	//Step 0. find the entry in the mapping table
	//The address of EEPROM is 2-bytes alignment.
	//The last bit is used for alignment, so it must be 0.
	tmpOffset = Offset & 0xfffe;
	EFSROM_AOUT = eFuseReadRegisters(pAd, tmpOffset, 2, &eFuseData);

	if( EFSROM_AOUT == 0x3f)
	{	//find available logical address pointer
		//the logical address does not exist, find an empty one
		//from the first address of block 45=16*45=0x2d0 to the last address of block 47
		//==>48*16-3(reserved)=2FC
		for (i=EFUSE_USAGE_MAP_START; i<=EFUSE_USAGE_MAP_END; i+=2)
		{
			//Retrive the logical block nubmer form each logical address pointer
			//It will access two logical address pointer each time.
			eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
			if( (LogicalAddress & 0xff) == 0)
			{//Not used logical address pointer
				BlkNum = i-EFUSE_USAGE_MAP_START;
				break;
			}
			else if(( (LogicalAddress >> 8) & 0xff) == 0)
			{//Not used logical address pointer
				if (i != EFUSE_USAGE_MAP_END)
				{
					BlkNum = i-EFUSE_USAGE_MAP_START+1;
				}
				break;
			}
		}
	}
	else
	{
		BlkNum = EFSROM_AOUT;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters BlkNum = %d \n", BlkNum));

	if(BlkNum == 0xffff)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters: out of free E-fuse space!!!\n"));
		return FALSE;
	}

	//Step 1. Save data of this block	which is pointed by the avaible logical address pointer
	// read and save the original block data
	for(i =0; i<8; i++)
	{
		addr = BlkNum * 0x10 ;

		InBuf[0] = addr+2*i;
		InBuf[1] = 2;
		InBuf[2] = 0x0;

		eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);

		buffer[i] = InBuf[2];
	}

	//Step 2. Update the data in buffer, and write the data to Efuse
	buffer[ (Offset >> 1) % 8] = pData[0];

	do
	{	Loop++;
		//Step 3. Write the data to Efuse
		if(!bWriteSuccess)
		{
			for(i =0; i<8; i++)
			{
				addr = BlkNum * 0x10 ;

				InBuf[0] = addr+2*i;
				InBuf[1] = 2;
				InBuf[2] = buffer[i];

				eFuseWritePhysical(pAd, &InBuf[0], 6, NULL, 2);
			}
		}
		else
		{
				addr = BlkNum * 0x10 ;

				InBuf[0] = addr+(Offset % 16);
				InBuf[1] = 2;
				InBuf[2] = pData[0];

				eFuseWritePhysical(pAd, &InBuf[0], 6, NULL, 2);
		}

		//Step 4. Write mapping table
		addr = EFUSE_USAGE_MAP_START+BlkNum;

		tmpaddr = addr;

		if(addr % 2 != 0)
			addr = addr -1;
		InBuf[0] = addr;
		InBuf[1] = 2;

		//convert the address from 10 to 8 bit ( bit7, 6 = parity and bit5 ~ 0 = bit9~4), and write to logical map entry
		tmpOffset = Offset;
		tmpOffset >>= 4;
		tmpOffset |= ((~((tmpOffset & 0x01) ^ ( tmpOffset >> 1 & 0x01) ^  (tmpOffset >> 2 & 0x01) ^  (tmpOffset >> 3 & 0x01))) << 6) & 0x40;
		tmpOffset |= ((~( (tmpOffset >> 2 & 0x01) ^ (tmpOffset >> 3 & 0x01) ^ (tmpOffset >> 4 & 0x01) ^ ( tmpOffset >> 5 & 0x01))) << 7) & 0x80;

		// write the logical address
		if(tmpaddr%2 != 0)
			InBuf[2] = tmpOffset<<8;
		else
			InBuf[2] = tmpOffset;

		eFuseWritePhysical(pAd,&InBuf[0], 6, NULL, 0);

		//Step 5. Compare data if not the same, invalidate the mapping entry, then re-write the data until E-fuse is exhausted
		bWriteSuccess = TRUE;
		for(i =0; i<8; i++)
		{
			addr = BlkNum * 0x10 ;

			InBuf[0] = addr+2*i;
			InBuf[1] = 2;
			InBuf[2] = 0x0;

			eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);

			if(buffer[i] != InBuf[2])
			{
				bWriteSuccess = FALSE;
				break;
			}
		}

		//Step 6. invlidate mapping entry and find a free mapping entry if not succeed
		if (!bWriteSuccess)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Not bWriteSuccess BlkNum = %d\n", BlkNum));

			// the offset of current mapping entry
			addr = EFUSE_USAGE_MAP_START+BlkNum;

			//find a new mapping entry
			BlkNum = 0xffff;
			for (i=EFUSE_USAGE_MAP_START; i<=EFUSE_USAGE_MAP_END; i+=2)
			{
				eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
				if( (LogicalAddress & 0xff) == 0)
				{
					BlkNum = i-EFUSE_USAGE_MAP_START;
					break;
				}
				else if(( (LogicalAddress >> 8) & 0xff) == 0)
				{
					if (i != EFUSE_USAGE_MAP_END)
					{
						BlkNum = i+1-EFUSE_USAGE_MAP_START;
					}
					break;
				}
			}
			DBGPRINT(RT_DEBUG_TRACE, ("Not bWriteSuccess new BlkNum = %d\n", BlkNum));
			if(BlkNum == 0xffff)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters: out of free E-fuse space!!!\n"));
				return FALSE;
			}

			//invalidate the original mapping entry if new entry is not found
			tmpaddr = addr;

			if(addr % 2 != 0)
				addr = addr -1;
			InBuf[0] = addr;
			InBuf[1] = 2;

			eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);

			// write the logical address
			if(tmpaddr%2 != 0)
			{
				// Invalidate the high byte
				for (i=8; i<15; i++)
				{
					if( ( (InBuf[2] >> i) & 0x01) == 0)
					{
						InBuf[2] |= (0x1 <<i);
						break;
					}
				}
			}
			else
			{
				// invalidate the low byte
				for (i=0; i<8; i++)
				{
					if( ( (InBuf[2] >> i) & 0x01) == 0)
					{
						InBuf[2] |= (0x1 <<i);
						break;
					}
				}
			}
			eFuseWritePhysical(pAd, &InBuf[0], 6, NULL, 0);
		}
	}
	while (!bWriteSuccess&&Loop<2);
	if(!bWriteSuccess)
		DBGPRINT(RT_DEBUG_ERROR,("Efsue Write Failed!!\n"));
	return TRUE;
}


/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
static VOID eFuseWritePhysical(
	IN	PRTMP_ADAPTER	pAd,
	PUSHORT lpInBuffer,
	ULONG nInBufferSize,
	PUCHAR lpOutBuffer,
	ULONG nOutBufferSize
)
{
	USHORT* pInBuf = (USHORT*)lpInBuffer;
	int		i;
	//USHORT* pOutBuf = (USHORT*)ioBuffer;
	USHORT Offset = pInBuf[0];					// addr
	USHORT Length = pInBuf[1];					// length
	USHORT* pValueX = &pInBuf[2];				// value ...

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWritePhysical Offset=0x%x, length=%d\n", Offset, Length));

	{
		// Little-endian		S	|	S	Big-endian
		// addr	3	2	1	0	|	0	1	2	3
		// Ori-V	D	C	B	A	|	A	B	C	D
		// After swapping
		//		D	C	B	A	|	D	C	B	A
		// Both the little and big-endian use the same sequence to write  data.
		// Therefore, we only need swap data when read the data.
		for (i=0; i<Length; i+=2)
		{
			eFusePhysicalWriteRegisters(pAd, Offset+i, 2, &pValueX[i/2]);
		}
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
NTSTATUS eFuseWrite(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT			Offset,
	IN	PUCHAR			pData,
	IN	USHORT			length)
{
	int i;
	USHORT* pValueX = (PUSHORT) pData;				//value ...

	// The input value=3070 will be stored as following
	// Little-endian		S	|	S	Big-endian
	// addr			1	0	|	0	1
	// Ori-V			30	70	|	30	70
	// After swapping
	//				30	70	|	70	30
	// Casting
	//				3070	|	7030 (x)
	// The swapping should be removed for big-endian
	for(i=0; i<length; i+=2)
	{
		eFuseWriteRegisters(pAd, Offset+i, 2, &pValueX[i/2]);
	}

	return TRUE;
}




/*
========================================================================

	Routine Description:

	Arguments:

	Return Value:

	Note:

========================================================================
*/
INT set_eFuseGetFreeBlockCount_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	USHORT i;
	USHORT	LogicalAddress;
	USHORT efusefreenum=0;
	if(!pAd->bUseEfuse)
		return FALSE;
	for (i = EFUSE_USAGE_MAP_START; i <= EFUSE_USAGE_MAP_END; i+=2)
	{
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
		if( (LogicalAddress & 0xff) == 0)
		{
			efusefreenum= (UCHAR) (EFUSE_USAGE_MAP_END-i+1);
			break;
		}
		else if(( (LogicalAddress >> 8) & 0xff) == 0)
		{
			efusefreenum = (UCHAR) (EFUSE_USAGE_MAP_END-i);
			break;
		}

		if(i == EFUSE_USAGE_MAP_END)
			efusefreenum = 0;
	}
	printk("efuseFreeNumber is %d\n",efusefreenum);
	return TRUE;
}


INT set_eFusedump_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
USHORT InBuf[3];
	INT i=0;
	if(!pAd->bUseEfuse)
		return FALSE;
	for(i =0; i<EFUSE_USAGE_MAP_END/2; i++)
	{
		InBuf[0] = 2*i;
		InBuf[1] = 2;
		InBuf[2] = 0x0;

		eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);
		if(i%4==0)
		printk("\nBlock %x:",i/8);
		printk("%04x ",InBuf[2]);
	}
	return TRUE;
}


INT	set_eFuseLoadFromBin_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	PSTRING					src;
	RTMP_OS_FD				srcf;
	RTMP_OS_FS_INFO			osfsInfo;
	INT						retval, memSize;
	PSTRING					buffer, memPtr;
	INT						i = 0,j=0,k=1;
	USHORT					*PDATA;
	USHORT					DATA;

	memSize = 128 + MAX_EEPROM_BIN_FILE_SIZE + sizeof(USHORT) * 8;
	memPtr = kmalloc(memSize, MEM_ALLOC_FLAG);
	if (memPtr == NULL)
		return FALSE;

	NdisZeroMemory(memPtr, memSize);
	src = memPtr; // kmalloc(128, MEM_ALLOC_FLAG);
	buffer = src + 128;		// kmalloc(MAX_EEPROM_BIN_FILE_SIZE, MEM_ALLOC_FLAG);
	PDATA = (USHORT*)(buffer + MAX_EEPROM_BIN_FILE_SIZE);	// kmalloc(sizeof(USHORT)*8,MEM_ALLOC_FLAG);

	if(strlen(arg)>0)
		NdisMoveMemory(src, arg, strlen(arg));
	else
		NdisMoveMemory(src, EFUSE_EEPROM_DEFULT_FILE, strlen(EFUSE_EEPROM_DEFULT_FILE));
	DBGPRINT(RT_DEBUG_TRACE, ("FileName=%s\n",src));

	RtmpOSFSInfoChange(&osfsInfo, TRUE);

	srcf = RtmpOSFileOpen(src, O_RDONLY, 0);
	if (IS_FILE_OPEN_ERR(srcf))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("--> Error opening file %s\n", src));
		retval = FALSE;
		goto recoverFS;
	}
	else
	{
		// The object must have a read method
		while(RtmpOSFileRead(srcf, &buffer[i], 1)==1)
		{
		i++;
			if(i>MAX_EEPROM_BIN_FILE_SIZE)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("--> Error reading file %s, file size too large[>%d]\n", src, MAX_EEPROM_BIN_FILE_SIZE));
				retval = FALSE;
				goto closeFile;
			}
		}

		retval = RtmpOSFileClose(srcf);
		if (retval)
			DBGPRINT(RT_DEBUG_TRACE, ("--> Error closing file %s\n", src));
	}


	RtmpOSFSInfoChange(&osfsInfo, FALSE);

	for(j=0;j<i;j++)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%02X ",buffer[j]&0xff));
		if((j+1)%2==0)
			PDATA[j/2%8]=((buffer[j]<<8)&0xff00)|(buffer[j-1]&0xff);
		if(j%16==0)
		{
			k=buffer[j];
		}
		else
		{
			k&=buffer[j];
			if((j+1)%16==0)
			{
				DBGPRINT(RT_DEBUG_TRACE, (" result=%02X,blk=%02x\n",k,j/16));
				if(k!=0xff)
					eFuseWriteRegistersFromBin(pAd,(USHORT)j-15, 16, PDATA);
				else
				{
					if(eFuseReadRegisters(pAd,j, 2,(PUSHORT)&DATA)!=0x3f)
						eFuseWriteRegistersFromBin(pAd,(USHORT)j-15, 16, PDATA);
				}
				/*
				for(l=0;l<8;l++)
					printk("%04x ",PDATA[l]);
				printk("\n");
				*/
				NdisZeroMemory(PDATA,16);
			}
		}
	}

	return TRUE;

closeFile:
	if (srcf)
		RtmpOSFileClose(srcf);

recoverFS:
	RtmpOSFSInfoChange(&osfsInfo, FALSE);


	if (memPtr)
		kfree(memPtr);

	return retval;
}


static NTSTATUS eFuseWriteRegistersFromBin(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData)
{
	USHORT	i;
	USHORT	eFuseData;
	USHORT	LogicalAddress, BlkNum = 0xffff;
	UCHAR	EFSROM_AOUT,Loop=0;
	EFUSE_CTRL_STRUC		eFuseCtrlStruc;
	USHORT	efuseDataOffset;
	UINT32	data,tempbuffer;
	USHORT addr,tmpaddr, InBuf[3], tmpOffset;
	UINT32 buffer[4];
	BOOLEAN		bWriteSuccess = TRUE;
	BOOLEAN		bNotWrite=TRUE;
	BOOLEAN		bAllocateNewBlk=TRUE;

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegistersFromBin Offset=%x, pData=%04x:%04x:%04x:%04x\n", Offset, *pData,*(pData+1),*(pData+2),*(pData+3)));

	do
	{
	//Step 0. find the entry in the mapping table
	//The address of EEPROM is 2-bytes alignment.
	//The last bit is used for alignment, so it must be 0.
	Loop++;
	tmpOffset = Offset & 0xfffe;
	EFSROM_AOUT = eFuseReadRegisters(pAd, tmpOffset, 2, &eFuseData);

	if( EFSROM_AOUT == 0x3f)
	{	//find available logical address pointer
		//the logical address does not exist, find an empty one
		//from the first address of block 45=16*45=0x2d0 to the last address of block 47
		//==>48*16-3(reserved)=2FC
		bAllocateNewBlk=TRUE;
		for (i=EFUSE_USAGE_MAP_START; i<=EFUSE_USAGE_MAP_END; i+=2)
		{
			//Retrive the logical block nubmer form each logical address pointer
			//It will access two logical address pointer each time.
			eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
			if( (LogicalAddress & 0xff) == 0)
			{//Not used logical address pointer
				BlkNum = i-EFUSE_USAGE_MAP_START;
				break;
			}
			else if(( (LogicalAddress >> 8) & 0xff) == 0)
			{//Not used logical address pointer
				if (i != EFUSE_USAGE_MAP_END)
				{
					BlkNum = i-EFUSE_USAGE_MAP_START+1;
				}
				break;
			}
		}
	}
	else
	{
		bAllocateNewBlk=FALSE;
		BlkNum = EFSROM_AOUT;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters BlkNum = %d \n", BlkNum));

	if(BlkNum == 0xffff)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegisters: out of free E-fuse space!!!\n"));
		return FALSE;
	}
	//Step 1.1.0
	//If the block is not existing in mapping table, create one
	//and write down the 16-bytes data to the new block
	if(bAllocateNewBlk)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("Allocate New Blk\n"));
		efuseDataOffset =  EFUSE_DATA3;
		for(i=0; i< 4; i++)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("Allocate New Blk, Data%d=%04x%04x\n",3-i,pData[2*i+1],pData[2*i]));
			tempbuffer=((pData[2*i+1]<<16)&0xffff0000)|pData[2*i];


			RTMP_IO_WRITE32(pAd, efuseDataOffset,tempbuffer);
			efuseDataOffset -= 4;

		}
		/////////////////////////////////////////////////////////////////

		//Step1.1.1. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);
		eFuseCtrlStruc.field.EFSROM_AIN = BlkNum* 0x10 ;

		//Step1.1.2. Write EFSROM_MODE (0x580, bit7:bit6) to 3.
		eFuseCtrlStruc.field.EFSROM_MODE = 3;

		//Step1.1.3. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical write procedure.
		eFuseCtrlStruc.field.EFSROM_KICK = 1;

		NdisMoveMemory(&data, &eFuseCtrlStruc, 4);

		RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

		//Step1.1.4. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. It��s done.
		i = 0;
		while(i < 100)
		{
			RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

			if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
				break;

			RTMPusecDelay(2);
			i++;
		}

	}
	else
	{	//Step1.2.
		//If the same logical number is existing, check if the writting data and the data
		//saving in this block are the same.
		/////////////////////////////////////////////////////////////////
		//read current values of 16-byte block
		RTMP_IO_READ32(pAd, EFUSE_CTRL, &eFuseCtrlStruc.word);

		//Step1.2.0. Write 10-bit of address to EFSROM_AIN (0x580, bit25:bit16). The address must be 16-byte alignment.
		eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

		//Step1.2.1. Write EFSROM_MODE (0x580, bit7:bit6) to 1.
		eFuseCtrlStruc.field.EFSROM_MODE = 0;

		//Step1.2.2. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical read procedure.
		eFuseCtrlStruc.field.EFSROM_KICK = 1;

		NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
		RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

		//Step1.2.3. Polling EFSROM_KICK(0x580, bit30) until it become 0 again.
		i = 0;
		while(i < 500)
		{
			RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

			if(eFuseCtrlStruc.field.EFSROM_KICK == 0)
				break;
			RTMPusecDelay(2);
			i++;
		}

		//Step1.2.4. Read 16-byte of data from EFUSE_DATA0-3 (0x59C-0x590)
		efuseDataOffset =  EFUSE_DATA3;
		for(i=0; i< 4; i++)
		{
			RTMP_IO_READ32(pAd, efuseDataOffset, (PUINT32) &buffer[i]);
			efuseDataOffset -=  4;
		}
		//Step1.2.5. Check if the data of efuse and the writing data are the same.
		for(i =0; i<4; i++)
		{
			tempbuffer=((pData[2*i+1]<<16)&0xffff0000)|pData[2*i];
			DBGPRINT(RT_DEBUG_TRACE, ("buffer[%d]=%x,pData[%d]=%x,pData[%d]=%x,tempbuffer=%x\n",i,buffer[i],2*i,pData[2*i],2*i+1,pData[2*i+1],tempbuffer));

			if(((buffer[i]&0xffff0000)==(pData[2*i+1]<<16))&&((buffer[i]&0xffff)==pData[2*i]))
				bNotWrite&=TRUE;
			else
			{
				bNotWrite&=FALSE;
				break;
			}
		}
		if(!bNotWrite)
		{
		printk("The data is not the same\n");

			for(i =0; i<8; i++)
			{
				addr = BlkNum * 0x10 ;

				InBuf[0] = addr+2*i;
				InBuf[1] = 2;
				InBuf[2] = pData[i];

				eFuseWritePhysical(pAd, &InBuf[0], 6, NULL, 2);
			}

		}
		else
			return TRUE;
	     }



		//Step 2. Write mapping table
		addr = EFUSE_USAGE_MAP_START+BlkNum;

		tmpaddr = addr;

		if(addr % 2 != 0)
			addr = addr -1;
		InBuf[0] = addr;
		InBuf[1] = 2;

		//convert the address from 10 to 8 bit ( bit7, 6 = parity and bit5 ~ 0 = bit9~4), and write to logical map entry
		tmpOffset = Offset;
		tmpOffset >>= 4;
		tmpOffset |= ((~((tmpOffset & 0x01) ^ ( tmpOffset >> 1 & 0x01) ^  (tmpOffset >> 2 & 0x01) ^  (tmpOffset >> 3 & 0x01))) << 6) & 0x40;
		tmpOffset |= ((~( (tmpOffset >> 2 & 0x01) ^ (tmpOffset >> 3 & 0x01) ^ (tmpOffset >> 4 & 0x01) ^ ( tmpOffset >> 5 & 0x01))) << 7) & 0x80;

		// write the logical address
		if(tmpaddr%2 != 0)
			InBuf[2] = tmpOffset<<8;
		else
			InBuf[2] = tmpOffset;

		eFuseWritePhysical(pAd,&InBuf[0], 6, NULL, 0);

		//Step 3. Compare data if not the same, invalidate the mapping entry, then re-write the data until E-fuse is exhausted
		bWriteSuccess = TRUE;
		for(i =0; i<8; i++)
		{
			addr = BlkNum * 0x10 ;

			InBuf[0] = addr+2*i;
			InBuf[1] = 2;
			InBuf[2] = 0x0;

			eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);
			DBGPRINT(RT_DEBUG_TRACE, ("addr=%x, buffer[i]=%x,InBuf[2]=%x\n",InBuf[0],pData[i],InBuf[2]));
			if(pData[i] != InBuf[2])
			{
				bWriteSuccess = FALSE;
				break;
			}
		}

		//Step 4. invlidate mapping entry and find a free mapping entry if not succeed

		if (!bWriteSuccess&&Loop<2)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegistersFromBin::Not bWriteSuccess BlkNum = %d\n", BlkNum));

			// the offset of current mapping entry
			addr = EFUSE_USAGE_MAP_START+BlkNum;

			//find a new mapping entry
			BlkNum = 0xffff;
			for (i=EFUSE_USAGE_MAP_START; i<=EFUSE_USAGE_MAP_END; i+=2)
			{
				eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
				if( (LogicalAddress & 0xff) == 0)
				{
					BlkNum = i-EFUSE_USAGE_MAP_START;
					break;
				}
				else if(( (LogicalAddress >> 8) & 0xff) == 0)
				{
					if (i != EFUSE_USAGE_MAP_END)
					{
						BlkNum = i+1-EFUSE_USAGE_MAP_START;
					}
					break;
				}
			}
			DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegistersFromBin::Not bWriteSuccess new BlkNum = %d\n", BlkNum));
			if(BlkNum == 0xffff)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("eFuseWriteRegistersFromBin: out of free E-fuse space!!!\n"));
				return FALSE;
			}

			//invalidate the original mapping entry if new entry is not found
			tmpaddr = addr;

			if(addr % 2 != 0)
				addr = addr -1;
			InBuf[0] = addr;
			InBuf[1] = 2;

			eFuseReadPhysical(pAd, &InBuf[0], 4, &InBuf[2], 2);

			// write the logical address
			if(tmpaddr%2 != 0)
			{
				// Invalidate the high byte
				for (i=8; i<15; i++)
				{
					if( ( (InBuf[2] >> i) & 0x01) == 0)
					{
						InBuf[2] |= (0x1 <<i);
						break;
					}
				}
			}
			else
			{
				// invalidate the low byte
				for (i=0; i<8; i++)
				{
					if( ( (InBuf[2] >> i) & 0x01) == 0)
					{
						InBuf[2] |= (0x1 <<i);
						break;
					}
				}
			}
			eFuseWritePhysical(pAd, &InBuf[0], 6, NULL, 0);
		}

	}
	while(!bWriteSuccess&&Loop<2);

	return TRUE;
}


int rtmp_ee_efuse_read16(
	IN RTMP_ADAPTER *pAd,
	IN USHORT Offset,
	OUT USHORT *pValue)
{
	if(pAd->bFroceEEPROMBuffer || pAd->bEEPROMFile)
	{
	    DBGPRINT(RT_DEBUG_TRACE,  ("Read from EEPROM Buffer\n"));
	    NdisMoveMemory(pValue, &(pAd->EEPROMImage[Offset]), 2);
	}
	else
	    eFuseReadRegisters(pAd, Offset, 2, pValue);
	return (*pValue);
}


int rtmp_ee_efuse_write16(
	IN RTMP_ADAPTER *pAd,
	IN USHORT Offset,
	IN USHORT data)
{
    if(pAd->bFroceEEPROMBuffer||pAd->bEEPROMFile)
    {
        DBGPRINT(RT_DEBUG_TRACE,  ("Write to EEPROM Buffer\n"));
        NdisMoveMemory(&(pAd->EEPROMImage[Offset]), &data, 2);
    }
    else
        eFuseWriteRegisters(pAd, Offset, 2, &data);
	return 0;
}


int RtmpEfuseSupportCheck(
	IN RTMP_ADAPTER *pAd)
{
	USHORT value;

	if (IS_RT30xx(pAd))
	{
		eFusePhysicalReadRegisters(pAd, EFUSE_TAG, 2, &value);
		pAd->EFuseTag = (value & 0xff);
	}
	return 0;
}

INT set_eFuseBufferModeWriteBack_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg)
{
	UINT Enable;


	if(strlen(arg)>0)
	{
		Enable= simple_strtol(arg, 0, 16);
	}
	else
		return FALSE;
	if(Enable==1)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("set_eFuseBufferMode_Proc:: Call WRITEEEPROMBUF"));
		eFuseWriteEeeppromBuf(pAd);
	}
	else
		return FALSE;
	return TRUE;
}


/*
	========================================================================

	Routine Description:
		Load EEPROM from bin file for eFuse mode

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS         firmware image load ok
		NDIS_STATUS_FAILURE         image not found

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
INT eFuseLoadEEPROM(
	IN PRTMP_ADAPTER pAd)
{
	PSTRING					src = NULL;
	INT						retval;
	RTMP_OS_FD				srcf;
	RTMP_OS_FS_INFO			osFSInfo;


	src=EFUSE_BUFFER_PATH;
	DBGPRINT(RT_DEBUG_TRACE, ("FileName=%s\n",src));


	RtmpOSFSInfoChange(&osFSInfo, TRUE);

	if (src && *src)
	{
		srcf = RtmpOSFileOpen(src, O_RDONLY, 0);
		if (IS_FILE_OPEN_ERR(srcf))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("--> Error %ld opening %s\n", -PTR_ERR(srcf),src));
			return FALSE;
		}
		else
		{

				memset(pAd->EEPROMImage, 0x00, MAX_EEPROM_BIN_FILE_SIZE);


			retval =RtmpOSFileRead(srcf, (PSTRING)pAd->EEPROMImage, MAX_EEPROM_BIN_FILE_SIZE);
			if (retval > 0)
							{
				RTMPSetProfileParameters(pAd, (PSTRING)pAd->EEPROMImage);
				retval = NDIS_STATUS_SUCCESS;
			}
			else
				DBGPRINT(RT_DEBUG_ERROR, ("Read file \"%s\" failed(errCode=%d)!\n", src, retval));

		}


	}
	else
		{
					DBGPRINT(RT_DEBUG_ERROR, ("--> Error src  or srcf is null\n"));
					return FALSE;

		}

	retval=RtmpOSFileClose(srcf);

	if (retval)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("--> Error %d closing %s\n", -retval, src));
	}


	RtmpOSFSInfoChange(&osFSInfo, FALSE);

	return TRUE;
}

INT eFuseWriteEeeppromBuf(
	IN PRTMP_ADAPTER pAd)
{

	PSTRING					src = NULL;
	INT						retval;
	RTMP_OS_FD				srcf;
	RTMP_OS_FS_INFO			osFSInfo;


	src=EFUSE_BUFFER_PATH;
	DBGPRINT(RT_DEBUG_TRACE, ("FileName=%s\n",src));

	RtmpOSFSInfoChange(&osFSInfo, TRUE);



	if (src && *src)
	{
		srcf = RtmpOSFileOpen(src, O_WRONLY|O_CREAT, 0);

		if (IS_FILE_OPEN_ERR(srcf))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("--> Error %ld opening %s\n", -PTR_ERR(srcf),src));
			return FALSE;
		}
		else
		{
/*
			// The object must have a read method
			if (srcf->f_op && srcf->f_op->write)
			{
				// The object must have a read method
                        srcf->f_op->write(srcf, pAd->EEPROMImage, 1024, &srcf->f_pos);

			}
			else
			{
						DBGPRINT(RT_DEBUG_ERROR, ("--> Error!! System doest not support read function\n"));
						return FALSE;
			}
*/

			RtmpOSFileWrite(srcf, (PSTRING)pAd->EEPROMImage,MAX_EEPROM_BIN_FILE_SIZE);

		}


	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("--> Error src  or srcf is null\n"));
		return FALSE;

	}

	retval=RtmpOSFileClose(srcf);

	if (retval)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("--> Error %d closing %s\n", -retval, src));
	}

	RtmpOSFSInfoChange(&osFSInfo, FALSE);
	return TRUE;
}


VOID eFuseGetFreeBlockCount(IN PRTMP_ADAPTER pAd,
	PUINT EfuseFreeBlock)
{
	USHORT i;
	USHORT	LogicalAddress;
	if(!pAd->bUseEfuse)
		{
		DBGPRINT(RT_DEBUG_TRACE,("eFuseGetFreeBlockCount Only supports efuse Mode\n"));
		return ;
		}
	for (i = EFUSE_USAGE_MAP_START; i <= EFUSE_USAGE_MAP_END; i+=2)
	{
		eFusePhysicalReadRegisters(pAd, i, 2, &LogicalAddress);
		if( (LogicalAddress & 0xff) == 0)
		{
			*EfuseFreeBlock= (UCHAR) (EFUSE_USAGE_MAP_END-i+1);
			break;
		}
		else if(( (LogicalAddress >> 8) & 0xff) == 0)
		{
			*EfuseFreeBlock = (UCHAR) (EFUSE_USAGE_MAP_END-i);
			break;
		}

		if(i == EFUSE_USAGE_MAP_END)
			*EfuseFreeBlock = 0;
	}
	DBGPRINT(RT_DEBUG_TRACE,("eFuseGetFreeBlockCount is 0x%x\n",*EfuseFreeBlock));
}

INT eFuse_init(
	IN PRTMP_ADAPTER pAd)
{
	UINT	EfuseFreeBlock=0;
	DBGPRINT(RT_DEBUG_ERROR, ("NVM is Efuse and its size =%x[%x-%x] \n",EFUSE_USAGE_MAP_SIZE,EFUSE_USAGE_MAP_START,EFUSE_USAGE_MAP_END));
	eFuseGetFreeBlockCount(pAd, &EfuseFreeBlock);
	//If the used block of efuse is less than 5. We assume the default value
	// of this efuse is empty and change to the buffer mode in odrder to
	//bring up interfaces successfully.
	if(EfuseFreeBlock > (EFUSE_USAGE_MAP_END-5))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("NVM is Efuse and the information is too less to bring up interface. Force to use EEPROM Buffer Mode\n"));
		pAd->bFroceEEPROMBuffer = TRUE;
		eFuseLoadEEPROM(pAd);
	}
	else
		pAd->bFroceEEPROMBuffer = FALSE;
	DBGPRINT(RT_DEBUG_TRACE, ("NVM is Efuse and force to use EEPROM Buffer Mode=%x\n",pAd->bFroceEEPROMBuffer));

	return 0;
}
