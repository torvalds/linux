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
	eeprom.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
*/
#include "../rt_config.h"

// IRQL = PASSIVE_LEVEL
VOID RaiseClock(
    IN	PRTMP_ADAPTER	pAd,
    IN  UINT32 *x)
{
    *x = *x | EESK;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
    RTMPusecDelay(1);				// Max frequency = 1MHz in Spec. definition
}

// IRQL = PASSIVE_LEVEL
VOID LowerClock(
    IN	PRTMP_ADAPTER	pAd,
    IN  UINT32 *x)
{
    *x = *x & ~EESK;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, *x);
    RTMPusecDelay(1);
}

// IRQL = PASSIVE_LEVEL
USHORT ShiftInBits(
    IN	PRTMP_ADAPTER	pAd)
{
    UINT32		x,i;
	USHORT      data=0;

    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

    x &= ~( EEDO | EEDI);

    for(i=0; i<16; i++)
    {
        data = data << 1;
        RaiseClock(pAd, &x);

        RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
#ifdef RT30xx
		LowerClock(pAd, &x); //prevent read failed
#endif
        x &= ~(EEDI);
        if(x & EEDO)
            data |= 1;

#ifndef RT30xx
        LowerClock(pAd, &x);
#endif
    }

    return data;
}

// IRQL = PASSIVE_LEVEL
VOID ShiftOutBits(
    IN	PRTMP_ADAPTER	pAd,
    IN  USHORT data,
    IN  USHORT count)
{
    UINT32       x,mask;

    mask = 0x01 << (count - 1);
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

    x &= ~(EEDO | EEDI);

    do
    {
        x &= ~EEDI;
        if(data & mask)		x |= EEDI;

        RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

        RaiseClock(pAd, &x);
        LowerClock(pAd, &x);

        mask = mask >> 1;
    } while(mask);

    x &= ~EEDI;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);
}

// IRQL = PASSIVE_LEVEL
VOID EEpromCleanup(
    IN	PRTMP_ADAPTER	pAd)
{
    UINT32 x;

    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

    x &= ~(EECS | EEDI);
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

    RaiseClock(pAd, &x);
    LowerClock(pAd, &x);
}

VOID EWEN(
	IN	PRTMP_ADAPTER	pAd)
{
    UINT32	x;

    // reset bits and set EECS
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
    x &= ~(EEDI | EEDO | EESK);
    x |= EECS;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	// kick a pulse
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

    // output the read_opcode and six pulse in that order
    ShiftOutBits(pAd, EEPROM_EWEN_OPCODE, 5);
    ShiftOutBits(pAd, 0, 6);

    EEpromCleanup(pAd);
}

VOID EWDS(
	IN	PRTMP_ADAPTER	pAd)
{
    UINT32	x;

    // reset bits and set EECS
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
    x &= ~(EEDI | EEDO | EESK);
    x |= EECS;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

	// kick a pulse
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);

    // output the read_opcode and six pulse in that order
    ShiftOutBits(pAd, EEPROM_EWDS_OPCODE, 5);
    ShiftOutBits(pAd, 0, 6);

    EEpromCleanup(pAd);
}

// IRQL = PASSIVE_LEVEL
USHORT RTMP_EEPROM_READ16(
    IN	PRTMP_ADAPTER	pAd,
    IN  USHORT Offset)
{
    UINT32		x;
    USHORT		data;

#ifdef RT30xx
	if (pAd->NicConfig2.field.AntDiversity)
    {
    	pAd->EepromAccess = TRUE;
    }
//2008/09/11:KH add to support efuse<--
//2008/09/11:KH add to support efuse-->
{
#endif
    Offset /= 2;
    // reset bits and set EECS
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
    x &= ~(EEDI | EEDO | EESK);
    x |= EECS;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

#ifdef RT30xx
	// patch can not access e-Fuse issue
    if (!IS_RT3090(pAd))
    {
#endif
	// kick a pulse
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);
#ifdef RT30xx
    }
#endif

    // output the read_opcode and register number in that order
    ShiftOutBits(pAd, EEPROM_READ_OPCODE, 3);
    ShiftOutBits(pAd, Offset, pAd->EEPROMAddressNum);

    // Now read the data (16 bits) in from the selected EEPROM word
    data = ShiftInBits(pAd);

    EEpromCleanup(pAd);

#ifdef RT30xx
	// Antenna and EEPROM access are both using EESK pin,
    // Therefor we should avoid accessing EESK at the same time
    // Then restore antenna after EEPROM access
	if ((pAd->NicConfig2.field.AntDiversity) || (pAd->RfIcType == RFIC_3020))
    {
	    pAd->EepromAccess = FALSE;
	    AsicSetRxAnt(pAd, pAd->RxAnt.Pair1PrimaryRxAnt);
    }
}
#endif
    return data;
}	//ReadEEprom

VOID RTMP_EEPROM_WRITE16(
    IN	PRTMP_ADAPTER	pAd,
    IN  USHORT Offset,
    IN  USHORT Data)
{
    UINT32 x;

#ifdef RT30xx
	if (pAd->NicConfig2.field.AntDiversity)
    {
    	pAd->EepromAccess = TRUE;
    }
	//2008/09/11:KH add to support efuse<--
//2008/09/11:KH add to support efuse-->
	{
#endif
	Offset /= 2;

	EWEN(pAd);

    // reset bits and set EECS
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);
    x &= ~(EEDI | EEDO | EESK);
    x |= EECS;
    RTMP_IO_WRITE32(pAd, E2PROM_CSR, x);

#ifdef RT30xx
	// patch can not access e-Fuse issue
    if (!IS_RT3090(pAd))
    {
#endif
	// kick a pulse
	RaiseClock(pAd, &x);
	LowerClock(pAd, &x);
#ifdef RT30xx
    }
#endif

    // output the read_opcode ,register number and data in that order
    ShiftOutBits(pAd, EEPROM_WRITE_OPCODE, 3);
    ShiftOutBits(pAd, Offset, pAd->EEPROMAddressNum);
	ShiftOutBits(pAd, Data, 16);		// 16-bit access

    // read DO status
    RTMP_IO_READ32(pAd, E2PROM_CSR, &x);

	EEpromCleanup(pAd);

	RTMPusecDelay(10000);	//delay for twp(MAX)=10ms

	EWDS(pAd);

    EEpromCleanup(pAd);

#ifdef RT30xx
	// Antenna and EEPROM access are both using EESK pin,
    // Therefor we should avoid accessing EESK at the same time
    // Then restore antenna after EEPROM access
	if ((pAd->NicConfig2.field.AntDiversity) || (pAd->RfIcType == RFIC_3020))
    {
	    pAd->EepromAccess = FALSE;
	    AsicSetRxAnt(pAd, pAd->RxAnt.Pair1PrimaryRxAnt);
    }
}
#endif
}

//2008/09/11:KH add to support efuse<--
#ifdef RT30xx
/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

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

	RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

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
	while(i < 100)
	{
		//rtmp.HwMemoryReadDword(EFUSE_CTRL, (DWORD *) &eFuseCtrlStruc, 4);
		RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);
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
		efuseDataOffset =  EFUSE_DATA3 - (Offset & 0xC)  ;
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
		data = data >> (8*(Offset & 0x3));

		NdisMoveMemory(pData, &data, Length);
	}

	return (UCHAR) eFuseCtrlStruc.field.EFSROM_AOUT;

}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

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

	RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

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
	while(i < 100)
	{
		RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);
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

	data = data >> (8*(Offset & 0x3));

	NdisMoveMemory(pData, &data, Length);

}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
VOID eFuseReadPhysical(
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
	int 		i;

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

	IRQL =

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
	NTSTATUS	Status = STATUS_SUCCESS;
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

	IRQL =

	Note:

	========================================================================
*/
VOID eFusePhysicalWriteRegisters(
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
	RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

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
	while(i < 100)
	{
		RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

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
	eFuseCtrlStruc.field.EFSROM_AIN = Offset & 0xfff0;

	//Step2. Write EFSROM_MODE (0x580, bit7:bit6) to 3.
	eFuseCtrlStruc.field.EFSROM_MODE = 3;

	//Step3. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical write procedure.
	eFuseCtrlStruc.field.EFSROM_KICK = 1;

	NdisMoveMemory(&data, &eFuseCtrlStruc, 4);
	RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

	//Step4. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. It¡¦s done.
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

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
NTSTATUS eFuseWriteRegisters(
	IN	PRTMP_ADAPTER	pAd,
	IN	USHORT Offset,
	IN	USHORT Length,
	IN	USHORT* pData)
{
	USHORT	i;
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
	{
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
	while(!bWriteSuccess);

	return TRUE;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
VOID eFuseWritePhysical(
	IN	PRTMP_ADAPTER	pAd,
  	PUSHORT lpInBuffer,
	ULONG nInBufferSize,
  	PUCHAR lpOutBuffer,
  	ULONG nOutBufferSize
)
{
	USHORT* pInBuf = (USHORT*)lpInBuffer;
	int 		i;
	//USHORT* pOutBuf = (USHORT*)ioBuffer;

	USHORT Offset = pInBuf[0];					//addr
	USHORT Length = pInBuf[1];					//length
	USHORT* pValueX = &pInBuf[2];				//value ...
		// Little-endian		S	|	S	Big-endian
		// addr	3	2	1	0	|	0	1	2	3
		// Ori-V	D	C	B	A	|	A	B	C	D
		//After swapping
		//		D	C	B	A	|	D	C	B	A
		//Both the little and big-endian use the same sequence to write  data.
		//Therefore, we only need swap data when read the data.
	for(i=0; i<Length; i+=2)
	{
		eFusePhysicalWriteRegisters(pAd, Offset+i, 2, &pValueX[i/2]);
	}
}


/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

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
		//The input value=3070 will be stored as following
 		// Little-endian		S	|	S	Big-endian
		// addr			1	0	|	0	1
		// Ori-V			30	70	|	30	70
		//After swapping
		//				30	70	|	70	30
		//Casting
		//				3070	|	7030 (x)
		//The swapping should be removed for big-endian
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

	IRQL =

	Note:

	========================================================================
*/
INT set_eFuseGetFreeBlockCount_Proc(
   	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg)
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
	IN	PUCHAR			arg)
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
	IN	PUCHAR			arg)
{
	CHAR					*src;
	struct file				*srcf;
	INT 					retval, orgfsuid, orgfsgid;
   	mm_segment_t			orgfs;
	UCHAR					*buffer;
	UCHAR					BinFileSize=0;
	INT						i = 0,j=0,k=1;
	USHORT					*PDATA;
	USHORT					DATA;
	BinFileSize=strlen("RT30xxEEPROM.bin");
	src = kmalloc(128, MEM_ALLOC_FLAG);
	NdisZeroMemory(src, 128);

 	if(strlen(arg)>0)
	{

		NdisMoveMemory(src, arg, strlen(arg));
 	}

	else
	{

		NdisMoveMemory(src, "RT30xxEEPROM.bin", BinFileSize);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("FileName=%s\n",src));
	buffer = kmalloc(MAX_EEPROM_BIN_FILE_SIZE, MEM_ALLOC_FLAG);

	if(buffer == NULL)
	{
		kfree(src);
		 return FALSE;
}
	PDATA=kmalloc(sizeof(USHORT)*8,MEM_ALLOC_FLAG);

	if(PDATA==NULL)
	{
		kfree(src);

		kfree(buffer);
		return FALSE;
	}
	/* Don't change to uid 0, let the file be opened as the "normal" user */
#if 0
	orgfsuid = current->fsuid;
	orgfsgid = current->fsgid;
	current->fsuid=current->fsgid = 0;
#endif
    	orgfs = get_fs();
   	 set_fs(KERNEL_DS);

	if (src && *src)
	{
		srcf = filp_open(src, O_RDONLY, 0);
		if (IS_ERR(srcf))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("--> Error %ld opening %s\n", -PTR_ERR(srcf),src));
			return FALSE;
		}
		else
		{
			// The object must have a read method
			if (srcf->f_op && srcf->f_op->read)
			{
				memset(buffer, 0x00, MAX_EEPROM_BIN_FILE_SIZE);
				while(srcf->f_op->read(srcf, &buffer[i], 1, &srcf->f_pos)==1)
				{
					DBGPRINT(RT_DEBUG_TRACE, ("%02X ",buffer[i]));
					if((i+1)%8==0)
						DBGPRINT(RT_DEBUG_TRACE, ("\n"));
              			i++;
						if(i>=MAX_EEPROM_BIN_FILE_SIZE)
							{
								DBGPRINT(RT_DEBUG_ERROR, ("--> Error %ld reading %s, The file is too large[1024]\n", -PTR_ERR(srcf),src));
								kfree(PDATA);
								kfree(buffer);
								kfree(src);
								return FALSE;
							}
			       }
			}
			else
			{
						DBGPRINT(RT_DEBUG_ERROR, ("--> Error!! System doest not support read function\n"));
						kfree(PDATA);
						kfree(buffer);
						kfree(src);
						return FALSE;
			}
      		}


	}
	else
		{
					DBGPRINT(RT_DEBUG_ERROR, ("--> Error src  or srcf is null\n"));
					kfree(PDATA);
					kfree(buffer);
					return FALSE;

		}


	retval=filp_close(srcf,NULL);

	if (retval)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("--> Error %d closing %s\n", -retval, src));
	}
	set_fs(orgfs);
#if 0
	current->fsuid = orgfsuid;
	current->fsgid = orgfsgid;
#endif
	for(j=0;j<i;j++)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%02X ",buffer[j]));
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


	kfree(PDATA);
	kfree(buffer);
	kfree(src);
	return TRUE;
}
NTSTATUS eFuseWriteRegistersFromBin(
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
		eFuseCtrlStruc.field.EFSROM_AIN = BlkNum* 0x10 ;

		//Step1.1.2. Write EFSROM_MODE (0x580, bit7:bit6) to 3.
		eFuseCtrlStruc.field.EFSROM_MODE = 3;

		//Step1.1.3. Write EFSROM_KICK (0x580, bit30) to 1 to kick-off physical write procedure.
		eFuseCtrlStruc.field.EFSROM_KICK = 1;

		NdisMoveMemory(&data, &eFuseCtrlStruc, 4);

		RTMP_IO_WRITE32(pAd, EFUSE_CTRL, data);

		//Step1.1.4. Polling EFSROM_KICK(0x580, bit30) until it become 0 again. It¡¦s done.
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
		RTMP_IO_READ32(pAd, EFUSE_CTRL, (PUINT32) &eFuseCtrlStruc);

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
		while(i < 100)
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

#endif // RT30xx //
//2008/09/11:KH add to support efuse-->
