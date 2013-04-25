/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_EFUSE_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <rtw_efuse.h>



/*------------------------Define local variable------------------------------*/
u8	fakeEfuseBank=0;
u32	fakeEfuseUsedBytes=0;
u8	fakeEfuseContent[EFUSE_MAX_HW_SIZE]={0};
u8	fakeEfuseInitMap[EFUSE_MAX_MAP_LEN]={0};
u8	fakeEfuseModifiedMap[EFUSE_MAX_MAP_LEN]={0};

u32	BTEfuseUsedBytes=0;
u8	BTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
u8	BTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN]={0};
u8	BTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN]={0};

u32	fakeBTEfuseUsedBytes=0;
u8	fakeBTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
u8	fakeBTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN]={0};
u8	fakeBTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN]={0};
/*------------------------Define local variable------------------------------*/

//------------------------------------------------------------------------------
#define REG_EFUSE_CTRL		0x0030
#define EFUSE_CTRL			REG_EFUSE_CTRL		// E-Fuse Control.
//------------------------------------------------------------------------------

BOOLEAN
Efuse_Read1ByteFromFakeContent(
	IN		PADAPTER	pAdapter,
	IN		u16		Offset,
	IN OUT	u8		*Value	);
BOOLEAN
Efuse_Read1ByteFromFakeContent(
	IN		PADAPTER	pAdapter,
	IN		u16		Offset,
	IN OUT	u8		*Value	)
{
	if(Offset >= EFUSE_MAX_HW_SIZE)
	{
		return _FALSE;
	}
	//DbgPrint("Read fake content, offset = %d\n", Offset);
	if(fakeEfuseBank == 0)
		*Value = fakeEfuseContent[Offset];
	else
		*Value = fakeBTEfuseContent[fakeEfuseBank-1][Offset];
	return _TRUE;
}

BOOLEAN
Efuse_Write1ByteToFakeContent(
	IN		PADAPTER	pAdapter,
	IN		u16		Offset,
	IN 		u8		Value	);
BOOLEAN
Efuse_Write1ByteToFakeContent(
	IN		PADAPTER	pAdapter,
	IN		u16		Offset,
	IN 		u8		Value	)
{
	if(Offset >= EFUSE_MAX_HW_SIZE)
	{
		return _FALSE;
	}
	if(fakeEfuseBank == 0)
		fakeEfuseContent[Offset] = Value;
	else
	{
		fakeBTEfuseContent[fakeEfuseBank-1][Offset] = Value;
	}
	return _TRUE;
}

/*-----------------------------------------------------------------------------
 * Function:	Efuse_PowerSwitch
 *
 * Overview:	When we want to enable write operation, we should change to 
 *				pwr on state. When we stop write, we should switch to 500k mode
 *				and disable LDO 2.5V.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/17/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
VOID
Efuse_PowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	pAdapter->HalFunc.EfusePowerSwitch(pAdapter, bWrite, PwrState);
}	

/*-----------------------------------------------------------------------------
 * Function:	efuse_GetCurrentSize
 *
 * Overview:	Get current efuse size!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
u16
Efuse_GetCurrentSize(
	IN PADAPTER		pAdapter,
	IN u8			efuseType,
	IN BOOLEAN		bPseudoTest)
{
	u16 ret=0;

	ret = pAdapter->HalFunc.EfuseGetCurrentSize(pAdapter, efuseType, bPseudoTest);

	return ret;
}

/*  11/16/2008 MH Add description. Get current efuse area enabled word!!. */
u8
Efuse_CalculateWordCnts(IN u8	word_en)
{
	u8 word_cnts = 0;
	if(!(word_en & BIT(0)))	word_cnts++; // 0 : write enable
	if(!(word_en & BIT(1)))	word_cnts++;
	if(!(word_en & BIT(2)))	word_cnts++;
	if(!(word_en & BIT(3)))	word_cnts++;
	return word_cnts;
}

//
//	Description:
//		Execute E-Fuse read byte operation.
//		Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
VOID
ReadEFuseByte(
		PADAPTER	Adapter,
		u16 			_offset, 
		u8 			*pbuf, 
		IN BOOLEAN	bPseudoTest) 
{
	u32	value32;
	u8	readbyte;
	u16	retry;
	//u32 start=rtw_get_current_time();

	if(bPseudoTest)
	{
		Efuse_Read1ByteFromFakeContent(Adapter, _offset, pbuf);
		return;
	}

	//Write Address
	rtw_write8(Adapter, EFUSE_CTRL+1, (_offset & 0xff));  		
	readbyte = rtw_read8(Adapter, EFUSE_CTRL+2);
	rtw_write8(Adapter, EFUSE_CTRL+2, ((_offset >> 8) & 0x03) | (readbyte & 0xfc));  		

	//Write bit 32 0
	readbyte = rtw_read8(Adapter, EFUSE_CTRL+3);		
	rtw_write8(Adapter, EFUSE_CTRL+3, (readbyte & 0x7f));  	
	
	//Check bit 32 read-ready
	retry = 0;
	value32 = rtw_read32(Adapter, EFUSE_CTRL);
	//while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10))
	while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10000))
	{
		value32 = rtw_read32(Adapter, EFUSE_CTRL);
		retry++;
	}

	// 20100205 Joseph: Add delay suggested by SD1 Victor.
	// This fix the problem that Efuse read error in high temperature condition.
	// Designer says that there shall be some delay after ready bit is set, or the
	// result will always stay on last data we read.
	rtw_udelay_os(50);
	value32 = rtw_read32(Adapter, EFUSE_CTRL);
	
	*pbuf = (u8)(value32 & 0xff);
	//DBG_871X("ReadEFuseByte _offset:%08u, in %d ms\n",_offset ,rtw_get_passing_time_ms(start));
	
}


//
//	Description:
//		1. Execute E-Fuse read byte operation according as map offset and 
//		    save to E-Fuse table.
//		2. Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
//	2008/12/12 MH 	1. Reorganize code flow and reserve bytes. and add description.
//					2. Add efuse utilization collect.
//	2008/12/22 MH	Read Efuse must check if we write section 1 data again!!! Sec1
//					write addr must be after sec5.
//

VOID
efuse_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	);
VOID
efuse_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	Adapter->HalFunc.ReadEFuse(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
}

VOID
EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		void		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	pAdapter->HalFunc.EFUSEGetEfuseDefinition(pAdapter, efuseType, type, pOut, bPseudoTest);
}

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Read1Byte
 *
 * Overview:	Copy from WMAC fot EFUSE read 1 byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/23/2008 	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
u8
EFUSE_Read1Byte(	
	IN	PADAPTER	Adapter, 
	IN	u16		Address)
{
	u8	data;
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k=0;
	u16	contentLen=0;

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI , TYPE_EFUSE_REAL_CONTENT_LEN, (PVOID)&contentLen, _FALSE);

	if (Address < contentLen)	//E-fuse 512Byte
	{
		//Write E-fuse Register address bit0~7
		temp = Address & 0xFF;	
		rtw_write8(Adapter, EFUSE_CTRL+1, temp);	
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+2);	
		//Write E-fuse Register address bit8~9
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);	
		rtw_write8(Adapter, EFUSE_CTRL+2, temp);	

		//Write 0x30[31]=0
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		temp = Bytetemp & 0x7F;
		rtw_write8(Adapter, EFUSE_CTRL+3, temp);

		//Wait Write-ready (0x30[31]=1)
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		while(!(Bytetemp & 0x80))
		{				
			Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
			k++;
			if(k==1000)
			{
				k=0;
				break;
			}
		}
		data=rtw_read8(Adapter, EFUSE_CTRL);
		return data;
	}
	else
		return 0xFF;
	
}/* EFUSE_Read1Byte */

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Write1Byte
 *
 * Overview:	Copy from WMAC fot EFUSE write 1 byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/23/2008 	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/

void	
EFUSE_Write1Byte(	
	IN	PADAPTER	Adapter, 
	IN	u16		Address,
	IN	u8		Value);
void	
EFUSE_Write1Byte(	
	IN	PADAPTER	Adapter, 
	IN	u16		Address,
	IN	u8		Value)
{
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k=0;
	u16	contentLen=0;

	//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("Addr=%x Data =%x\n", Address, Value));
	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI , TYPE_EFUSE_REAL_CONTENT_LEN, (PVOID)&contentLen, _FALSE);

	if( Address < contentLen)	//E-fuse 512Byte
	{
		rtw_write8(Adapter, EFUSE_CTRL, Value);

		//Write E-fuse Register address bit0~7
		temp = Address & 0xFF;	
		rtw_write8(Adapter, EFUSE_CTRL+1, temp);	
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+2);	
		
		//Write E-fuse Register address bit8~9
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);	
		rtw_write8(Adapter, EFUSE_CTRL+2, temp);	

		//Write 0x30[31]=1
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		temp = Bytetemp | 0x80;
		rtw_write8(Adapter, EFUSE_CTRL+3, temp);

		//Wait Write-ready (0x30[31]=0)
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		while(Bytetemp & 0x80)
		{
			Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);			
			k++;
			if(k==100)
			{
				k=0;
				break;
			}
		}
	}
}/* EFUSE_Write1Byte */

/*  11/16/2008 MH Read one byte from real Efuse. */
u8
efuse_OneByteRead(
	IN	PADAPTER	pAdapter, 
	IN	u16			addr,
	IN	u8			*data,
	IN	BOOLEAN		bPseudoTest)
{
	u8	tmpidx = 0;
	u8	bResult;

	if(bPseudoTest)
	{
		bResult = Efuse_Read1ByteFromFakeContent(pAdapter, addr, data);
		return bResult;
	}
	// -----------------e-fuse reg ctrl ---------------------------------
	//address			
	rtw_write8(pAdapter, EFUSE_CTRL+1, (u8)(addr&0xff));		
	rtw_write8(pAdapter, EFUSE_CTRL+2, ((u8)((addr>>8) &0x03) ) | 
	(rtw_read8(pAdapter, EFUSE_CTRL+2)&0xFC ));	

	rtw_write8(pAdapter, EFUSE_CTRL+3,  0x72);//read cmd	

	while(!(0x80 &rtw_read8(pAdapter, EFUSE_CTRL+3))&&(tmpidx<100))
	{
		tmpidx++;
	}
	if(tmpidx<100)
	{			
		*data=rtw_read8(pAdapter, EFUSE_CTRL);		
		bResult = _TRUE;
	}
	else
	{
		*data = 0xff;	
		bResult = _FALSE;
	}
	return bResult;
}
		
/*  11/16/2008 MH Write one byte to reald Efuse. */
u8
efuse_OneByteWrite(
	IN	PADAPTER	pAdapter,  
	IN	u16			addr, 
	IN	u8			data,
	IN	BOOLEAN		bPseudoTest)
{
	u8	tmpidx = 0;
	u8	bResult;

	if(bPseudoTest)
	{
		bResult = Efuse_Write1ByteToFakeContent(pAdapter, addr, data);
		return bResult;
	}
	//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("Addr = %x Data=%x\n", addr, data));

	//return	0;

	// -----------------e-fuse reg ctrl ---------------------------------	
	//address			
	rtw_write8(pAdapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtw_write8(pAdapter, EFUSE_CTRL+2, 
	(rtw_read8(pAdapter, EFUSE_CTRL+2)&0xFC )|(u8)((addr>>8)&0x03) );	
	rtw_write8(pAdapter, EFUSE_CTRL, data);//data		

	rtw_write8(pAdapter, EFUSE_CTRL+3, 0xF2);//write cmd
		
	while((0x80 &  rtw_read8(pAdapter, EFUSE_CTRL+3)) && (tmpidx<100) ){
		tmpidx++;
	}
	
	if(tmpidx<100)
	{					
		bResult = _TRUE;
	}
	else
	{			
		bResult = _FALSE;
	}		
	
	return bResult;	
}

int
Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	ret =  pAdapter->HalFunc.Efuse_PgPacketRead(pAdapter, offset, data, bPseudoTest);

	return ret;
}

int 
Efuse_PgPacketWrite(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int ret;

	ret =  pAdapter->HalFunc.Efuse_PgPacketWrite(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}


int 
Efuse_PgPacketWrite_BT(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int ret;

	ret =  pAdapter->HalFunc.Efuse_PgPacketWrite_BT(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

/*-----------------------------------------------------------------------------
 * Function:	efuse_WordEnableDataRead
 *
 * Overview:	Read allowed word in current efuse section data.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Create Version 0.
 * 11/21/2008 	MHC		Fix Write bug when we only enable late word.
 *
 *---------------------------------------------------------------------------*/
void
efuse_WordEnableDataRead(IN	u8	word_en,
							IN	u8	*sourdata,
							IN	u8	*targetdata)
{	
	if (!(word_en&BIT(0)))
	{
		targetdata[0] = sourdata[0];
		targetdata[1] = sourdata[1];
	}
	if (!(word_en&BIT(1)))
	{
		targetdata[2] = sourdata[2];
		targetdata[3] = sourdata[3];
	}
	if (!(word_en&BIT(2)))
	{
		targetdata[4] = sourdata[4];
		targetdata[5] = sourdata[5];
	}
	if (!(word_en&BIT(3)))
	{
		targetdata[6] = sourdata[6];
		targetdata[7] = sourdata[7];
	}
}


u8
Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en, 
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	ret =  pAdapter->HalFunc.Efuse_WordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	
	return ret;
}

static u8 efuse_read8(PADAPTER padapter, u16 address, u8 *value)
{
	return efuse_OneByteRead(padapter,address, value, _FALSE);
}

static u8 efuse_write8(PADAPTER padapter, u16 address, u8 *value)
{
	return efuse_OneByteWrite(padapter,address, *value, _FALSE);
}

/*
 * read/wirte raw efuse data
 */
u8 rtw_efuse_access(PADAPTER padapter, u8 bWrite, u16 start_addr, u16 cnts, u8 *data)
{
	int i = 0;
	u16	real_content_len = 0, max_available_size = 0;
	u8 res = _FAIL ;
	u8 (*rw8)(PADAPTER, u16, u8*);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_REAL_CONTENT_LEN, (PVOID)&real_content_len, _FALSE);
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);

	if (start_addr > real_content_len)
		return _FAIL;

	if (_TRUE == bWrite) {
		if ((start_addr + cnts) > max_available_size)
			return _FAIL;
		rw8 = &efuse_write8;
	} else
		rw8 = &efuse_read8;

	Efuse_PowerSwitch(padapter, bWrite, _TRUE);

	// e-fuse one byte read / write
	for (i = 0; i < cnts; i++) {
		if (start_addr >= real_content_len) {
			res = _FAIL;
			break;
		}

		res = rw8(padapter, start_addr++, data++);
		if (_FAIL == res) break;
	}

	Efuse_PowerSwitch(padapter, bWrite, _FALSE);

	return res;
}
//------------------------------------------------------------------------------
u16 efuse_GetMaxSize(PADAPTER padapter)
{
	u16	max_size;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI , TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_size, _FALSE);
	return max_size;
}
//------------------------------------------------------------------------------
u8 efuse_GetCurrentSize(PADAPTER padapter, u16 *size)
{
	Efuse_PowerSwitch(padapter, _FALSE, _TRUE);
	*size = Efuse_GetCurrentSize(padapter, EFUSE_WIFI, _FALSE);
	Efuse_PowerSwitch(padapter, _FALSE, _FALSE);

	return _SUCCESS;
}
//------------------------------------------------------------------------------
u8 rtw_efuse_map_read(PADAPTER padapter, u16 addr, u16 cnts, u8 *data)
{
	u16	mapLen=0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, _FALSE);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	Efuse_PowerSwitch(padapter, _FALSE, _TRUE);

	efuse_ReadEFuse(padapter, EFUSE_WIFI, addr, cnts, data, _FALSE);

	Efuse_PowerSwitch(padapter, _FALSE, _FALSE);

	return _SUCCESS;
}

u8 rtw_BT_efuse_map_read(PADAPTER padapter, u16 addr, u16 cnts, u8 *data)
{
	u16	mapLen=0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, _FALSE);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	Efuse_PowerSwitch(padapter, _FALSE, _TRUE);

	efuse_ReadEFuse(padapter, EFUSE_BT, addr, cnts, data, _FALSE);

	Efuse_PowerSwitch(padapter, _FALSE, _FALSE);

	return _SUCCESS;
}
//------------------------------------------------------------------------------
u8 rtw_efuse_map_write(PADAPTER padapter, u16 addr, u16 cnts, u8 *data)
{
	u8	offset, word_en;
	u8	*map;
	u8	newdata[PGPKT_DATA_SIZE];
	s32	i, j, idx;
	u8	ret = _SUCCESS;
	u16	mapLen=0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, _FALSE);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	map = rtw_zmalloc(mapLen);
	if(map == NULL){
		return _FAIL;
	}

	ret = rtw_efuse_map_read(padapter, 0, mapLen, map);
	if (ret == _FAIL) goto exit;

	Efuse_PowerSwitch(padapter, _TRUE, _TRUE);

	offset = (addr >> 3);
	word_en = 0xF;
	_rtw_memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	i = addr & 0x7;	// index of one package
	j = 0;		// index of new package
	idx = 0;	// data index

	if (i & 0x1) {
		// odd start
		if (data[idx] != map[addr+idx]) {
			word_en &= ~BIT(i >> 1);
			newdata[i-1] = map[addr+idx-1];
			newdata[i] = data[idx];
		}
		i++;
		idx++;
	}
	do {
		for (; i < PGPKT_DATA_SIZE; i += 2)
		{
			if (cnts == idx) break;
			if ((cnts - idx) == 1) {
				if (data[idx] != map[addr+idx]) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i+1] = map[addr+idx+1];
				}
				idx++;
				break;
			} else {
				if ((data[idx] != map[addr+idx]) ||
				    (data[idx+1] != map[addr+idx+1]))
				{
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i+1] = data[idx + 1];
				}
				idx += 2;
			}
			if (idx == cnts) break;
		}

		if (word_en != 0xF) {
			ret = Efuse_PgPacketWrite(padapter, offset, word_en, newdata, _FALSE);
			DBG_871X("offset=%x \n",offset);
			DBG_871X("word_en=%x \n",word_en);

			for(i=0;i<PGPKT_DATA_SIZE;i++)
			{
				DBG_871X("data=%x \t",newdata[i]);
			}
			if (ret == _FAIL) break;
		}

		if (idx == cnts) break;

		offset++;
		i = 0;
		j = 0;
		word_en = 0xF;
		_rtw_memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	} while (1);

	Efuse_PowerSwitch(padapter, _TRUE, _FALSE);

exit:

	rtw_mfree(map, mapLen);

	return ret;
}


//------------------------------------------------------------------------------
u8 rtw_BT_efuse_map_write(PADAPTER padapter, u16 addr, u16 cnts, u8 *data)
{
	u8	offset, word_en;
	u8	*map;
	u8	newdata[PGPKT_DATA_SIZE];
	s32	i, j, idx;
	u8	ret = _SUCCESS;
	u16	mapLen=0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, _FALSE);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	map = rtw_zmalloc(mapLen);
	if(map == NULL){
		return _FAIL;
	}

	ret = rtw_BT_efuse_map_read(padapter, 0, mapLen, map);
	if (ret == _FAIL) goto exit;

	Efuse_PowerSwitch(padapter, _TRUE, _TRUE);

	offset = (addr >> 3);
	word_en = 0xF;
	_rtw_memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	i = addr & 0x7;	// index of one package
	j = 0;		// index of new package
	idx = 0;	// data index

	if (i & 0x1) {
		// odd start
		if (data[idx] != map[addr+idx]) {
			word_en &= ~BIT(i >> 1);
			newdata[i-1] = map[addr+idx-1];
			newdata[i] = data[idx];
		}
		i++;
		idx++;
	}
	do {
		for (; i < PGPKT_DATA_SIZE; i += 2)
		{
			if (cnts == idx) break;
			if ((cnts - idx) == 1) {
				if (data[idx] != map[addr+idx]) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i+1] = map[addr+idx+1];
				}
				idx++;
				break;
			} else {
				if ((data[idx] != map[addr+idx]) ||
				    (data[idx+1] != map[addr+idx+1]))
				{
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i+1] = data[idx + 1];
				}
				idx += 2;
			}
			if (idx == cnts) break;
		}

		if (word_en != 0xF)
		{
			DBG_871X("%s: offset=%#X\n", __FUNCTION__, offset);
			DBG_871X("%s: word_en=%#X\n", __FUNCTION__, word_en);
			DBG_871X("%s: data=", __FUNCTION__);
			for (i=0; i<PGPKT_DATA_SIZE; i++)
			{
				DBG_871X("0x%02X ", newdata[i]);
			}
			DBG_871X("\n");

			ret = Efuse_PgPacketWrite_BT(padapter, offset, word_en, newdata, _FALSE);
			if (ret == _FAIL) break;
		}

		if (idx == cnts) break;

		offset++;
		i = 0;
		j = 0;
		word_en = 0xF;
		_rtw_memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	} while (1);

	Efuse_PowerSwitch(padapter, _TRUE, _FALSE);

exit:

	rtw_mfree(map, mapLen);

	return ret;
}

/*-----------------------------------------------------------------------------
 * Function:	Efuse_ReadAllMap
 *
 * Overview:	Read All Efuse content
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/11/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
VOID 
Efuse_ReadAllMap(
	IN		PADAPTER	pAdapter, 
	IN		u8		efuseType,
	IN OUT	u8		*Efuse,
	IN		BOOLEAN		bPseudoTest);
VOID 
Efuse_ReadAllMap(
	IN		PADAPTER	pAdapter, 
	IN		u8		efuseType,
	IN OUT	u8		*Efuse,
	IN		BOOLEAN		bPseudoTest)
{
	u16	mapLen=0;

	Efuse_PowerSwitch(pAdapter,_FALSE, _TRUE);

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, bPseudoTest);

	efuse_ReadEFuse(pAdapter, efuseType, 0, mapLen, Efuse, bPseudoTest);

	Efuse_PowerSwitch(pAdapter,_FALSE, _FALSE);
}

/*-----------------------------------------------------------------------------
 * Function:	efuse_ShadowRead1Byte
 *			efuse_ShadowRead2Byte
 *			efuse_ShadowRead4Byte
 *
 * Overview:	Read from efuse init map by one/two/four bytes !!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static VOID
efuse_ShadowRead1Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN OUT	u8		*Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];

}	// EFUSE_ShadowRead1Byte

//---------------Read Two Bytes
static VOID
efuse_ShadowRead2Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN OUT	u16		*Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;

}	// EFUSE_ShadowRead2Byte

//---------------Read Four Bytes
static VOID
efuse_ShadowRead4Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN OUT	u32		*Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+2]<<16;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+3]<<24;

}	// efuse_ShadowRead4Byte


/*-----------------------------------------------------------------------------
 * Function:	efuse_ShadowWrite1Byte
 *			efuse_ShadowWrite2Byte
 *			efuse_ShadowWrite4Byte
 *
 * Overview:	Write efuse modify map by one/two/four byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
#ifdef PLATFORM
static VOID
efuse_ShadowWrite1Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN 	u8		Value);
#endif //PLATFORM
static VOID
efuse_ShadowWrite1Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN 	u8		Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	pEEPROM->efuse_eeprom_data[Offset] = Value;

}	// efuse_ShadowWrite1Byte

//---------------Write Two Bytes
static VOID
efuse_ShadowWrite2Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN 	u16		Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	pEEPROM->efuse_eeprom_data[Offset] = Value&0x00FF;
	pEEPROM->efuse_eeprom_data[Offset+1] = Value>>8;

}	// efuse_ShadowWrite1Byte

//---------------Write Four Bytes
static VOID
efuse_ShadowWrite4Byte(
	IN	PADAPTER	pAdapter,
	IN	u16		Offset,
	IN	u32		Value)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	pEEPROM->efuse_eeprom_data[Offset] = (u8)(Value&0x000000FF);
	pEEPROM->efuse_eeprom_data[Offset+1] = (u8)((Value>>8)&0x0000FF);
	pEEPROM->efuse_eeprom_data[Offset+2] = (u8)((Value>>16)&0x00FF);
	pEEPROM->efuse_eeprom_data[Offset+3] = (u8)((Value>>24)&0xFF);

}	// efuse_ShadowWrite1Byte

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowMapUpdate
 *
 * Overview:	Transfer current EFUSE content to shadow init and modify map.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/13/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void EFUSE_ShadowMapUpdate(
	IN PADAPTER	pAdapter,
	IN u8		efuseType,
	IN BOOLEAN	bPseudoTest)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	u16	mapLen=0;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_MAP_LEN, (PVOID)&mapLen, bPseudoTest);

	if (pEEPROM->bautoload_fail_flag == _TRUE)
	{
		_rtw_memset(pEEPROM->efuse_eeprom_data, 0xFF, mapLen);
	}
	else
	{
		#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE			
		if(_SUCCESS != retriveAdaptorInfoFile(pAdapter->registrypriv.adaptor_info_caching_file_path, pEEPROM)) {
		#endif
		
		Efuse_ReadAllMap(pAdapter, efuseType, pEEPROM->efuse_eeprom_data, bPseudoTest);
		
		#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
			storeAdaptorInfoFile(pAdapter->registrypriv.adaptor_info_caching_file_path, pEEPROM);
		}
		#endif
	}

	//PlatformMoveMemory((PVOID)&pHalData->EfuseMap[EFUSE_MODIFY_MAP][0], 
	//(PVOID)&pHalData->EfuseMap[EFUSE_INIT_MAP][0], mapLen);
}// EFUSE_ShadowMapUpdate


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowRead
 *
 * Overview:	Read from efuse init map !!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void
EFUSE_ShadowRead(
	IN		PADAPTER	pAdapter,
	IN		u8		Type,
	IN		u16		Offset,
	IN OUT	u32		*Value	)
{
	if (Type == 1)
		efuse_ShadowRead1Byte(pAdapter, Offset, (u8 *)Value);
	else if (Type == 2)
		efuse_ShadowRead2Byte(pAdapter, Offset, (u16 *)Value);
	else if (Type == 4)
		efuse_ShadowRead4Byte(pAdapter, Offset, (u32 *)Value);
	
}	// EFUSE_ShadowRead

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowWrite
 *
 * Overview:	Write efuse modify map for later update operation to use!!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
VOID
EFUSE_ShadowWrite(
	IN	PADAPTER	pAdapter,
	IN	u8		Type,
	IN	u16		Offset,
	IN OUT	u32		Value);
VOID
EFUSE_ShadowWrite(
	IN	PADAPTER	pAdapter,
	IN	u8		Type,
	IN	u16		Offset,
	IN OUT	u32		Value)
{
#if (MP_DRIVER == 0)
	return;
#endif

	if (Type == 1)
		efuse_ShadowWrite1Byte(pAdapter, Offset, (u8)Value);
	else if (Type == 2)
		efuse_ShadowWrite2Byte(pAdapter, Offset, (u16)Value);
	else if (Type == 4)
		efuse_ShadowWrite4Byte(pAdapter, Offset, (u32)Value);

}	// EFUSE_ShadowWrite

VOID
Efuse_InitSomeVar(
	IN		PADAPTER	pAdapter
	);
VOID
Efuse_InitSomeVar(
	IN		PADAPTER	pAdapter
	)
{
	u8 i;
	
	_rtw_memset((PVOID)&fakeEfuseContent[0], 0xff, EFUSE_MAX_HW_SIZE);
	_rtw_memset((PVOID)&fakeEfuseInitMap[0], 0xff, EFUSE_MAX_MAP_LEN);
	_rtw_memset((PVOID)&fakeEfuseModifiedMap[0], 0xff, EFUSE_MAX_MAP_LEN);

	for(i=0; i<EFUSE_MAX_BT_BANK; i++)
	{
		_rtw_memset((PVOID)&BTEfuseContent[i][0], EFUSE_MAX_HW_SIZE, 0xff);
	}
	_rtw_memset((PVOID)&BTEfuseInitMap[0], 0xff, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset((PVOID)&BTEfuseModifiedMap[0], 0xff, EFUSE_BT_MAX_MAP_LEN);

	for(i=0; i<EFUSE_MAX_BT_BANK; i++)
	{
		_rtw_memset((PVOID)&fakeBTEfuseContent[i][0], 0xff, EFUSE_MAX_HW_SIZE);
	}
	_rtw_memset((PVOID)&fakeBTEfuseInitMap[0], 0xff, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset((PVOID)&fakeBTEfuseModifiedMap[0], 0xff, EFUSE_BT_MAX_MAP_LEN);
}

#ifdef PLATFORM_LINUX
#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
//#include <rtw_eeprom.h>

 int isAdaptorInfoFileValid(void)
{
	return _TRUE;
}

int storeAdaptorInfoFile(char *path, struct eeprom_priv * eeprom_priv)
{
	int ret =_SUCCESS;

	if(path && eeprom_priv) {
		ret = rtw_store_to_file(path, eeprom_priv->efuse_eeprom_data, EEPROM_MAX_SIZE_512);
		if(ret == EEPROM_MAX_SIZE)
			ret = _SUCCESS;
		else
			ret = _FAIL;
	} else {
		DBG_871X("%s NULL pointer\n",__FUNCTION__);
		ret =  _FAIL;
	}
	return ret;
}

int retriveAdaptorInfoFile(char *path, struct eeprom_priv * eeprom_priv)
{
	int ret = _SUCCESS;
	mm_segment_t oldfs;
	struct file *fp;
	
	if(path && eeprom_priv) {

		ret = rtw_retrive_from_file(path, eeprom_priv->efuse_eeprom_data, EEPROM_MAX_SIZE);
		
		if(ret == EEPROM_MAX_SIZE)
			ret = _SUCCESS;
		else
			ret = _FAIL;

		#if 0
		if(isAdaptorInfoFileValid()) {	
			return 0;
		} else {
			return _FAIL;
		}
		#endif
		
	} else {
		DBG_871X("%s NULL pointer\n",__FUNCTION__);
		ret = _FAIL;
	}
	return ret;
}
#endif //CONFIG_ADAPTOR_INFO_CACHING_FILE
#endif //PLATFORM_LINUX


