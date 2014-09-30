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
 ******************************************************************************/
#define _RTW_EFUSE_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <rtw_efuse.h>
#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

/*------------------------Define local variable------------------------------*/

/*  */
#define REG_EFUSE_CTRL		0x0030
#define EFUSE_CTRL			REG_EFUSE_CTRL		/*  E-Fuse Control. */
/*  */

#define VOLTAGE_V25		0x03
#define LDOE25_SHIFT		28

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
 * 11/17/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void Efuse_PowerSwitch(struct rtw_adapter *padapter,
			      u8 bWrite, u8 PwrState)
{
	u8 tempval;
	u16 tmpV16;

	if (PwrState == true) {
		rtl8723au_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		/*  1.2V Power: From VDDON with Power
		    Cut(0x0000h[15]), default valid */
		tmpV16 = rtl8723au_read16(padapter, REG_SYS_ISO_CTRL);
		if (!(tmpV16 & PWC_EV12V)) {
			tmpV16 |= PWC_EV12V;
			rtl8723au_write16(padapter, REG_SYS_ISO_CTRL, tmpV16);
		}
		/*  Reset: 0x0000h[28], default valid */
		tmpV16 = rtl8723au_read16(padapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR;
			rtl8723au_write16(padapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/*  Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock
		    from ANA, default valid */
		tmpV16 = rtl8723au_read16(padapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN)) || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtl8723au_write16(padapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == true) {
			/*  Enable LDO 2.5V before read/write action */
			tempval = rtl8723au_read8(padapter, EFUSE_TEST + 3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtl8723au_write8(padapter, EFUSE_TEST + 3,
					 tempval | 0x80);
		}
	} else {
		rtl8723au_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite == true) {
			/*  Disable LDO 2.5V after read/write action */
			tempval = rtl8723au_read8(padapter, EFUSE_TEST + 3);
			rtl8723au_write8(padapter, EFUSE_TEST + 3,
					 tempval & 0x7F);
		}
	}
}

u16
Efuse_GetCurrentSize23a(struct rtw_adapter *pAdapter, u8 efuseType)
{
	u16 ret = 0;

	if (efuseType == EFUSE_WIFI)
		ret = rtl8723a_EfuseGetCurrentSize_WiFi(pAdapter);
	else
		ret = rtl8723a_EfuseGetCurrentSize_BT(pAdapter);

	return ret;
}

/*  11/16/2008 MH Add description. Get current efuse area enabled word!!. */
u8
Efuse_CalculateWordCnts23a(u8 word_en)
{
	u8 word_cnts = 0;
	if (!(word_en & BIT(0)))	word_cnts++; /*  0 : write enable */
	if (!(word_en & BIT(1)))	word_cnts++;
	if (!(word_en & BIT(2)))	word_cnts++;
	if (!(word_en & BIT(3)))	word_cnts++;
	return word_cnts;
}

/*  */
/*	Description: */
/*		Execute E-Fuse read byte operation. */
/*		Referred from SD1 Richard. */
/*  */
/*	Assumption: */
/*		1. Boot from E-Fuse and successfully auto-load. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
/*	Created by Roger, 2008.10.21. */
/*  */
void
ReadEFuseByte23a(struct rtw_adapter *Adapter, u16 _offset, u8 *pbuf)
{
	u32	value32;
	u8	readbyte;
	u16	retry;

	/* Write Address */
	rtl8723au_write8(Adapter, EFUSE_CTRL+1, (_offset & 0xff));
	readbyte = rtl8723au_read8(Adapter, EFUSE_CTRL+2);
	rtl8723au_write8(Adapter, EFUSE_CTRL+2,
			 ((_offset >> 8) & 0x03) | (readbyte & 0xfc));

	/* Write bit 32 0 */
	readbyte = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
	rtl8723au_write8(Adapter, EFUSE_CTRL+3, readbyte & 0x7f);

	/* Check bit 32 read-ready */
	retry = 0;
	value32 = rtl8723au_read32(Adapter, EFUSE_CTRL);
	/* while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10)) */
	while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10000))
	{
		value32 = rtl8723au_read32(Adapter, EFUSE_CTRL);
		retry++;
	}

	/*  20100205 Joseph: Add delay suggested by SD1 Victor. */
	/*  This fix the problem that Efuse read error in high temperature condition. */
	/*  Designer says that there shall be some delay after ready bit is set, or the */
	/*  result will always stay on last data we read. */
	udelay(50);
	value32 = rtl8723au_read32(Adapter, EFUSE_CTRL);

	*pbuf = (u8)(value32 & 0xff);
}

void
EFUSE_GetEfuseDefinition23a(struct rtw_adapter *pAdapter, u8 efuseType,
			    u8 type, void *pOut)
{
	u8 *pu1Tmp;
	u16 *pu2Tmp;
	u8 *pMax_section;

	switch (type) {
	case TYPE_EFUSE_MAX_SECTION:
		pMax_section = (u8 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pMax_section = EFUSE_MAX_SECTION_8723A;
		else
			*pMax_section = EFUSE_BT_MAX_SECTION;
		break;

	case TYPE_EFUSE_REAL_CONTENT_LEN:
		pu2Tmp = (u16 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723A;
		else
			*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
		break;

	case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
		pu2Tmp = (u16 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723A -
				   EFUSE_OOB_PROTECT_BYTES);
		else
			*pu2Tmp = (EFUSE_BT_REAL_BANK_CONTENT_LEN -
				   EFUSE_PROTECT_BYTES_BANK);
		break;

	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
		pu2Tmp = (u16 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723A -
				   EFUSE_OOB_PROTECT_BYTES);
		else
			*pu2Tmp = (EFUSE_BT_REAL_CONTENT_LEN -
				   (EFUSE_PROTECT_BYTES_BANK * 3));
		break;

	case TYPE_EFUSE_MAP_LEN:
		pu2Tmp = (u16 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_MAP_LEN_8723A;
		else
			*pu2Tmp = EFUSE_BT_MAP_LEN;
		break;

	case TYPE_EFUSE_PROTECT_BYTES_BANK:
		pu1Tmp = (u8 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu1Tmp = EFUSE_OOB_PROTECT_BYTES;
		else
			*pu1Tmp = EFUSE_PROTECT_BYTES_BANK;
		break;

	case TYPE_EFUSE_CONTENT_LEN_BANK:
		pu2Tmp = (u16 *) pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723A;
		else
			*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN;
		break;

	default:
		pu1Tmp = (u8 *) pOut;
		*pu1Tmp = 0;
		break;
	}
}

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Read1Byte23a
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
 * 09/23/2008	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
u8
EFUSE_Read1Byte23a(struct rtw_adapter *Adapter, u16 Address)
{
	u8	data;
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k = 0;
	u16	contentLen = 0;

	EFUSE_GetEfuseDefinition23a(Adapter, EFUSE_WIFI,
				 TYPE_EFUSE_REAL_CONTENT_LEN,
				 (void *)&contentLen);

	if (Address < contentLen)	/* E-fuse 512Byte */
	{
		/* Write E-fuse Register address bit0~7 */
		temp = Address & 0xFF;
		rtl8723au_write8(Adapter, EFUSE_CTRL+1, temp);
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+2);
		/* Write E-fuse Register address bit8~9 */
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		rtl8723au_write8(Adapter, EFUSE_CTRL+2, temp);

		/* Write 0x30[31]= 0 */
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
		temp = Bytetemp & 0x7F;
		rtl8723au_write8(Adapter, EFUSE_CTRL+3, temp);

		/* Wait Write-ready (0x30[31]= 1) */
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
		while(!(Bytetemp & 0x80))
		{
			Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
			k++;
			if (k == 1000)
			{
				k = 0;
				break;
			}
		}
		data = rtl8723au_read8(Adapter, EFUSE_CTRL);
		return data;
	}
	else
		return 0xFF;
}/* EFUSE_Read1Byte23a */

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
 * 09/23/2008	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/

void
EFUSE_Write1Byte(
	struct rtw_adapter *	Adapter,
	u16		Address,
	u8		Value);
void
EFUSE_Write1Byte(
	struct rtw_adapter *	Adapter,
	u16		Address,
	u8		Value)
{
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k = 0;
	u16	contentLen = 0;

	/* RT_TRACE(COMP_EFUSE, DBG_LOUD, ("Addr =%x Data =%x\n", Address, Value)); */
	EFUSE_GetEfuseDefinition23a(Adapter, EFUSE_WIFI,
				 TYPE_EFUSE_REAL_CONTENT_LEN,
				 (void *)&contentLen);

	if (Address < contentLen)	/* E-fuse 512Byte */
	{
		rtl8723au_write8(Adapter, EFUSE_CTRL, Value);

		/* Write E-fuse Register address bit0~7 */
		temp = Address & 0xFF;
		rtl8723au_write8(Adapter, EFUSE_CTRL+1, temp);
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+2);

		/* Write E-fuse Register address bit8~9 */
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		rtl8723au_write8(Adapter, EFUSE_CTRL+2, temp);

		/* Write 0x30[31]= 1 */
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
		temp = Bytetemp | 0x80;
		rtl8723au_write8(Adapter, EFUSE_CTRL+3, temp);

		/* Wait Write-ready (0x30[31]= 0) */
		Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
		while(Bytetemp & 0x80)
		{
			Bytetemp = rtl8723au_read8(Adapter, EFUSE_CTRL+3);
			k++;
			if (k == 100)
			{
				k = 0;
				break;
			}
		}
	}
}/* EFUSE_Write1Byte */

/*  11/16/2008 MH Read one byte from real Efuse. */
int
efuse_OneByteRead23a(struct rtw_adapter *pAdapter, u16 addr, u8 *data)
{
	u8	tmpidx = 0;
	int	bResult;

	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */
	rtl8723au_write8(pAdapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtl8723au_write8(pAdapter, EFUSE_CTRL+2, ((u8)((addr>>8) &0x03)) |
	(rtl8723au_read8(pAdapter, EFUSE_CTRL+2)&0xFC));

	rtl8723au_write8(pAdapter, EFUSE_CTRL+3,  0x72);/* read cmd */

	while(!(0x80 &rtl8723au_read8(pAdapter, EFUSE_CTRL+3)) && (tmpidx<100))
		tmpidx++;
	if (tmpidx < 100) {
		*data = rtl8723au_read8(pAdapter, EFUSE_CTRL);
		bResult = _SUCCESS;
	} else {
		*data = 0xff;
		bResult = _FAIL;
	}
	return bResult;
}

/*  11/16/2008 MH Write one byte to reald Efuse. */
int
efuse_OneByteWrite23a(struct rtw_adapter *pAdapter, u16 addr, u8 data)
{
	u8	tmpidx = 0;
	int	bResult;

	/* RT_TRACE(COMP_EFUSE, DBG_LOUD, ("Addr = %x Data =%x\n", addr, data)); */

	/* return	0; */

	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */
	rtl8723au_write8(pAdapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtl8723au_write8(pAdapter, EFUSE_CTRL+2,
	(rtl8723au_read8(pAdapter, EFUSE_CTRL+2)&0xFC)|(u8)((addr>>8)&0x03));
	rtl8723au_write8(pAdapter, EFUSE_CTRL, data);/* data */

	rtl8723au_write8(pAdapter, EFUSE_CTRL+3, 0xF2);/* write cmd */

	while((0x80 & rtl8723au_read8(pAdapter, EFUSE_CTRL+3)) &&
	      (tmpidx<100)) {
		tmpidx++;
	}

	if (tmpidx < 100)
		bResult = _SUCCESS;
	else
		bResult = _FAIL;

	return bResult;
}

/*-----------------------------------------------------------------------------
 * Function:	efuse_WordEnableDataRead23a
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
 * 11/16/2008	MHC		Create Version 0.
 * 11/21/2008	MHC		Fix Write bug when we only enable late word.
 *
 *---------------------------------------------------------------------------*/
void
efuse_WordEnableDataRead23a(u8	word_en,
			 u8	*sourdata,
			 u8	*targetdata)
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

static int efuse_read8(struct rtw_adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteRead23a(padapter, address, value);
}

static int efuse_write8(struct rtw_adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteWrite23a(padapter, address, *value);
}

/*
 * read/write raw efuse data
 */
int rtw_efuse_access23a(struct rtw_adapter *padapter, u8 bWrite, u16 start_addr,
			u16 cnts, u8 *data)
{
	int i = 0;
	u16 real_content_len = 0, max_available_size = 0;
	int res = _FAIL ;
	int (*rw8)(struct rtw_adapter *, u16, u8*);

	EFUSE_GetEfuseDefinition23a(padapter, EFUSE_WIFI,
				 TYPE_EFUSE_REAL_CONTENT_LEN,
				 (void *)&real_content_len);
	EFUSE_GetEfuseDefinition23a(padapter, EFUSE_WIFI,
				 TYPE_AVAILABLE_EFUSE_BYTES_TOTAL,
				 (void *)&max_available_size);

	if (start_addr > real_content_len)
		return _FAIL;

	if (true == bWrite) {
		if ((start_addr + cnts) > max_available_size)
			return _FAIL;
		rw8 = &efuse_write8;
	} else
		rw8 = &efuse_read8;

	Efuse_PowerSwitch(padapter, bWrite, true);

	/*  e-fuse one byte read / write */
	for (i = 0; i < cnts; i++) {
		if (start_addr >= real_content_len) {
			res = _FAIL;
			break;
		}

		res = rw8(padapter, start_addr++, data++);
		if (res == _FAIL)
			break;
	}

	Efuse_PowerSwitch(padapter, bWrite, false);

	return res;
}
/*  */
u16 efuse_GetMaxSize23a(struct rtw_adapter *padapter)
{
	u16 max_size;
	EFUSE_GetEfuseDefinition23a(padapter, EFUSE_WIFI,
				 TYPE_AVAILABLE_EFUSE_BYTES_TOTAL,
				 (void *)&max_size);
	return max_size;
}
/*  */
int rtw_efuse_map_read23a(struct rtw_adapter *padapter,
			  u16 addr, u16 cnts, u8 *data)
{
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition23a(padapter, EFUSE_WIFI,
				 TYPE_EFUSE_MAP_LEN, (void *)&mapLen);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	Efuse_PowerSwitch(padapter, false, true);

	rtl8723a_readefuse(padapter, EFUSE_WIFI, addr, cnts, data);

	Efuse_PowerSwitch(padapter, false, false);

	return _SUCCESS;
}

int rtw_BT_efuse_map_read23a(struct rtw_adapter *padapter,
			     u16 addr, u16 cnts, u8 *data)
{
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition23a(padapter, EFUSE_BT,
				 TYPE_EFUSE_MAP_LEN, (void *)&mapLen);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	Efuse_PowerSwitch(padapter, false, true);

	rtl8723a_readefuse(padapter, EFUSE_BT, addr, cnts, data);

	Efuse_PowerSwitch(padapter, false, false);

	return _SUCCESS;
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
 * 11/11/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void
Efuse_ReadAllMap(struct rtw_adapter *pAdapter, u8 efuseType, u8 *Efuse);
void
Efuse_ReadAllMap(struct rtw_adapter *pAdapter, u8 efuseType, u8 *Efuse)
{
	u16	mapLen = 0;

	Efuse_PowerSwitch(pAdapter, false, true);

	EFUSE_GetEfuseDefinition23a(pAdapter, efuseType, TYPE_EFUSE_MAP_LEN,
				 (void *)&mapLen);

	rtl8723a_readefuse(pAdapter, efuseType, 0, mapLen, Efuse);

	Efuse_PowerSwitch(pAdapter, false, false);
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
 * 11/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void
efuse_ShadowRead1Byte(
	struct rtw_adapter *	pAdapter,
	u16		Offset,
	u8		*Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
}	/*  EFUSE_ShadowRead23a1Byte */

/* Read Two Bytes */
static void
efuse_ShadowRead2Byte(
	struct rtw_adapter *	pAdapter,
	u16		Offset,
	u16		*Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
}	/*  EFUSE_ShadowRead23a2Byte */

/* Read Four Bytes */
static void
efuse_ShadowRead4Byte(
	struct rtw_adapter *	pAdapter,
	u16		Offset,
	u32		*Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+2]<<16;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+3]<<24;
}	/*  efuse_ShadowRead4Byte */

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowMapUpdate23a
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
 * 11/13/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void EFUSE_ShadowMapUpdate23a(struct rtw_adapter *pAdapter, u8 efuseType)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	u16	mapLen = 0;

	EFUSE_GetEfuseDefinition23a(pAdapter, efuseType,
				 TYPE_EFUSE_MAP_LEN, (void *)&mapLen);

	if (pEEPROM->bautoload_fail_flag == true)
		memset(pEEPROM->efuse_eeprom_data, 0xFF, mapLen);
	else
		Efuse_ReadAllMap(pAdapter, efuseType,
				 pEEPROM->efuse_eeprom_data);

}/*  EFUSE_ShadowMapUpdate23a */

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowRead23a
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
 * 11/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void
EFUSE_ShadowRead23a(
	struct rtw_adapter *	pAdapter,
	u8		Type,
	u16		Offset,
	u32		*Value	)
{
	if (Type == 1)
		efuse_ShadowRead1Byte(pAdapter, Offset, (u8 *)Value);
	else if (Type == 2)
		efuse_ShadowRead2Byte(pAdapter, Offset, (u16 *)Value);
	else if (Type == 4)
		efuse_ShadowRead4Byte(pAdapter, Offset, (u32 *)Value);
}	/*  EFUSE_ShadowRead23a */
