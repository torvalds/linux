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
 *
 ******************************************************************************/
#define _RTW_EFUSE_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>

/*------------------------Define local variable------------------------------*/
u8 fakeEfuseBank;
u32 fakeEfuseUsedBytes;
u8 fakeEfuseContent[EFUSE_MAX_HW_SIZE] = {0};
u8 fakeEfuseInitMap[EFUSE_MAX_MAP_LEN] = {0};
u8 fakeEfuseModifiedMap[EFUSE_MAX_MAP_LEN] = {0};

u32 BTEfuseUsedBytes;
u8 BTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
u8 BTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN] = {0};
u8 BTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN] = {0};

u32 fakeBTEfuseUsedBytes;
u8 fakeBTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
u8 fakeBTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN] = {0};
u8 fakeBTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN] = {0};
/*------------------------Define local variable------------------------------*/

/*  */
#define REG_EFUSE_CTRL		0x0030
#define EFUSE_CTRL			REG_EFUSE_CTRL		/*  E-Fuse Control. */
/*  */

static bool Efuse_Read1ByteFromFakeContent(struct rtw_adapter *adapter, u16 Offset, u8 *value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0)
		*value = fakeEfuseContent[Offset];
	else
		*value = fakeBTEfuseContent[fakeEfuseBank-1][Offset];
	return true;
}

static bool Efuse_Write1ByteToFakeContent(struct rtw_adapter *adapter, u16 Offset, u8 value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0)
		fakeEfuseContent[Offset] = value;
	else
		fakeBTEfuseContent[fakeEfuseBank-1][Offset] = value;
	return true;
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
 * 11/17/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void Efuse_PowerSwitch(struct rtw_adapter *adapter, u8 write, u8 pwrstate)
{
	adapter->HalFunc.EfusePowerSwitch(adapter, write, pwrstate);
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
 * 11/16/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
u16 Efuse_GetCurrentSize(struct rtw_adapter *adapter, u8 efusetype, bool test)
{
	u16 ret = 0;

	ret = adapter->HalFunc.EfuseGetCurrentSize(adapter, efusetype, test);

	return ret;
}

/*  11/16/2008 MH Add description. Get current efuse area enabled word!!. */
u8
Efuse_CalculateWordCnts(u8 word_en)
{
	u8 word_cnts = 0;
	if (!(word_en & BIT(0)))
		word_cnts++; /*  0 : write enable */
	if (!(word_en & BIT(1)))
		word_cnts++;
	if (!(word_en & BIT(2)))
		word_cnts++;
	if (!(word_en & BIT(3)))
		word_cnts++;
	return word_cnts;
}

/*  */
/*	Description: */
/*		Execute E-Fuse read byte operation. */
/*		Refered from SD1 Richard. */
/*  */
/*	Assumption: */
/*		1. Boot from E-Fuse and successfully auto-load. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
/*	Created by Roger, 2008.10.21. */
/*  */
void ReadEFuseByte(struct rtw_adapter *adapter, u16 _offset, u8 *pbuf, bool test)
{
	u32 value32;
	u8 readbyte;
	u16 retry;

	if (test) {
		Efuse_Read1ByteFromFakeContent(adapter, _offset, pbuf);
		return;
	}

	/* Write Address */
	rtw_write8(adapter, EFUSE_CTRL+1, (_offset & 0xff));
	readbyte = rtw_read8(adapter, EFUSE_CTRL+2);
	rtw_write8(adapter, EFUSE_CTRL+2, ((_offset >> 8) & 0x03) | (readbyte & 0xfc));

	/* Write bit 32 0 */
	readbyte = rtw_read8(adapter, EFUSE_CTRL+3);
	rtw_write8(adapter, EFUSE_CTRL+3, (readbyte & 0x7f));

	/* Check bit 32 read-ready */
	retry = 0;
	value32 = rtw_read32(adapter, EFUSE_CTRL);
	while (!(((value32 >> 24) & 0xff) & 0x80) && (retry < 10000)) {
		value32 = rtw_read32(adapter, EFUSE_CTRL);
		retry++;
	}

	/*  20100205 Joseph: Add delay suggested by SD1 Victor. */
	/*  This fix the problem that Efuse read error in high temperature condition. */
	/*  Designer says that there shall be some delay after ready bit is set, or the */
	/*  result will always stay on last data we read. */
	rtw_udelay_os(50);
	value32 = rtw_read32(adapter, EFUSE_CTRL);

	*pbuf = (u8)(value32 & 0xff);
}

/*	Description: */
/*		1. Execute E-Fuse read byte operation according as map offset and */
/*		    save to E-Fuse table. */
/*		2. Refered from SD1 Richard. */
/*	Assumption: */
/*		1. Boot from E-Fuse and successfully auto-load. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*	Created by Roger, 2008.10.21. */
/*	2008/12/12 MH	1. Reorganize code flow and reserve bytes. and add description. */
/*					2. Add efuse utilization collect. */
/*	2008/12/22 MH	Read Efuse must check if we write section 1 data again!!! Sec1 */
/*					write addr must be after sec5. */

static void efuse_ReadEFuse(struct rtw_adapter *adapter, u8 efusetype, u16 _offset, u16 _size_byte, u8 *pbuf, bool test)
{
	adapter->HalFunc.ReadEFuse(adapter, efusetype, _offset, _size_byte, pbuf, test);
}

void EFUSE_GetEfuseDefinition(struct rtw_adapter *adapter, u8 efusetype, u8 type, void *out, bool test)
{
	adapter->HalFunc.EFUSEGetEfuseDefinition(adapter, efusetype, type, out, test);
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
 * 09/23/2008	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
u8 EFUSE_Read1Byte(struct rtw_adapter *adapter, u16 address)
{
	u8 data;
	u8 bytetemp = {0x00};
	u8 temp = {0x00};
	u32 k = 0;
	u16 contentlen = 0;

	EFUSE_GetEfuseDefinition(adapter, EFUSE_WIFI , TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&contentlen, false);

	if (address < contentlen) {	/* E-fuse 512Byte */
		/* Write E-fuse Register address bit0~7 */
		temp = address & 0xFF;
		rtw_write8(adapter, EFUSE_CTRL+1, temp);
		bytetemp = rtw_read8(adapter, EFUSE_CTRL+2);
		/* Write E-fuse Register address bit8~9 */
		temp = ((address >> 8) & 0x03) | (bytetemp & 0xFC);
		rtw_write8(adapter, EFUSE_CTRL+2, temp);

		/* Write 0x30[31]= 0 */
		bytetemp = rtw_read8(adapter, EFUSE_CTRL+3);
		temp = bytetemp & 0x7F;
		rtw_write8(adapter, EFUSE_CTRL+3, temp);

		/* Wait Write-ready (0x30[31]= 1) */
		bytetemp = rtw_read8(adapter, EFUSE_CTRL+3);
		while (!(bytetemp & 0x80)) {
			bytetemp = rtw_read8(adapter, EFUSE_CTRL+3);
			k++;
			if (k == 1000) {
				k = 0;
				break;
			}
		}
		data = rtw_read8(adapter, EFUSE_CTRL);
		return data;
	} else {
		return 0xFF;
	}
} /* EFUSE_Read1Byte */

/*  11/16/2008 MH Read one byte from real Efuse. */
u8 efuse_OneByteRead(struct rtw_adapter *adapter, u16 addr, u8 *data, bool test)
{
	u8 tmpidx = 0;
	u8 result;

	if (test) {
		result = Efuse_Read1ByteFromFakeContent(adapter, addr, data);
		return result;
	}
	/*  -----------------e-fuse reg ctrl -------------------------------- */
	/* address */
	rtw_write8(adapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtw_write8(adapter, EFUSE_CTRL+2, ((u8)((addr >> 8) & 0x03)) |
	(rtw_read8(adapter, EFUSE_CTRL+2)&0xFC));

	rtw_write8(adapter, EFUSE_CTRL+3,  0x72);/* read cmd */

	while (!(0x80 & rtw_read8(adapter, EFUSE_CTRL+3)) && (tmpidx < 100))
		tmpidx++;
	if (tmpidx < 100) {
		*data = rtw_read8(adapter, EFUSE_CTRL);
		result = true;
	} else {
		*data = 0xff;
		result = false;
	}
	return result;
}

/*  11/16/2008 MH Write one byte to reald Efuse. */
u8 efuse_OneByteWrite(struct rtw_adapter *adapter, u16 addr, u8 data, bool test)
{
	u8 tmpidx = 0;
	u8 result;

	if (test) {
		result = Efuse_Write1ByteToFakeContent(adapter, addr, data);
		return result;
	}

	/*  -----------------e-fuse reg ctrl ---------------------------- */
	/* address */
	rtw_write8(adapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtw_write8(adapter, EFUSE_CTRL+2,
		   (rtw_read8(adapter, EFUSE_CTRL+2)&0xFC) | (u8)((addr>>8)&0x03));
	rtw_write8(adapter, EFUSE_CTRL, data);/* data */

	rtw_write8(adapter, EFUSE_CTRL+3, 0xF2);/* write cmd */

	while ((0x80 & rtw_read8(adapter, EFUSE_CTRL+3)) && (tmpidx < 100))
		tmpidx++;

	if (tmpidx < 100)
		result = true;
	else
		result = false;
	return result;
}

int Efuse_PgPacketRead(struct rtw_adapter *adapter, u8 offset, u8 *data, bool test)
{
	int	ret = 0;

	ret =  adapter->HalFunc.Efuse_PgPacketRead(adapter, offset, data, test);
	return ret;
}

int Efuse_PgPacketWrite(struct rtw_adapter *adapter, u8 offset, u8 word_en,
			u8 *data, bool test)
{
	int ret;

	ret =  adapter->HalFunc.Efuse_PgPacketWrite(adapter, offset, word_en, data, test);

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
 * 11/16/2008	MHC		Create Version 0.
 * 11/21/2008	MHC		Fix Write bug when we only enable late word.
 *
 *---------------------------------------------------------------------------*/
void efuse_WordEnableDataRead(u8 word_en, u8 *sourdata, u8 *targetdata)
{
	if (!(word_en&BIT(0))) {
		targetdata[0] = sourdata[0];
		targetdata[1] = sourdata[1];
	}
	if (!(word_en&BIT(1))) {
		targetdata[2] = sourdata[2];
		targetdata[3] = sourdata[3];
	}
	if (!(word_en&BIT(2))) {
		targetdata[4] = sourdata[4];
		targetdata[5] = sourdata[5];
	}
	if (!(word_en&BIT(3))) {
		targetdata[6] = sourdata[6];
		targetdata[7] = sourdata[7];
	}
}

u8 Efuse_WordEnableDataWrite(struct rtw_adapter *adapter, u16 efuse_addr,
			     u8 word_en, u8 *data, bool test)
{
	u8 ret = 0;

	ret =  adapter->HalFunc.Efuse_WordEnableDataWrite(adapter, efuse_addr, word_en, data, test);

	return ret;
}

static u8 efuse_read8(struct rtw_adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteRead(padapter, address, value, false);
}

static u8 efuse_write8(struct rtw_adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteWrite(padapter, address, *value, false);
}

/*
 * read/wirte raw efuse data
 */
u8 rtw_efuse_access(struct rtw_adapter *padapter, u8 write, u16 start_addr, u16 cnts, u8 *data)
{
	int i = 0;
	u16 real_content_len = 0, max_available_size = 0;
	u8 res = _FAIL;
	u8 (*rw8)(struct rtw_adapter *, u16, u8*);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&real_content_len, false);
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

	if (start_addr > real_content_len)
		return _FAIL;

	if (true == write) {
		if ((start_addr + cnts) > max_available_size)
			return _FAIL;
		rw8 = &efuse_write8;
	} else {
		rw8 = &efuse_read8;
	}

	Efuse_PowerSwitch(padapter, write, true);

	/*  e-fuse one byte read / write */
	for (i = 0; i < cnts; i++) {
		if (start_addr >= real_content_len) {
			res = _FAIL;
			break;
		}

		res = rw8(padapter, start_addr++, data++);
		if (_FAIL == res)
			break;
	}

	Efuse_PowerSwitch(padapter, write, false);

	return res;
}
/*  */
u16 efuse_GetMaxSize(struct rtw_adapter *padapter)
{
	u16 max_size;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI , TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_size, false);
	return max_size;
}
/*  */
u8 efuse_GetCurrentSize(struct rtw_adapter *padapter, u16 *size)
{
	Efuse_PowerSwitch(padapter, false, true);
	*size = Efuse_GetCurrentSize(padapter, EFUSE_WIFI, false);
	Efuse_PowerSwitch(padapter, false, false);

	return _SUCCESS;
}

u8 rtw_efuse_map_read(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u16 maplen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&maplen, false);

	if ((addr + cnts) > maplen)
		return _FAIL;

	Efuse_PowerSwitch(padapter, false, true);

	efuse_ReadEFuse(padapter, EFUSE_WIFI, addr, cnts, data, false);

	Efuse_PowerSwitch(padapter, false, false);

	return _SUCCESS;
}

u8 rtw_efuse_map_write(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u8 offset, word_en;
	u8 *map;
	u8 newdata[PGPKT_DATA_SIZE + 1];
	s32	i, idx;
	u8 ret = _SUCCESS;
	u16 maplen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&maplen, false);

	if ((addr + cnts) > maplen)
		return _FAIL;

	map = kzalloc(maplen, GFP_KERNEL);
	if (map == NULL)
		return _FAIL;

	ret = rtw_efuse_map_read(padapter, 0, maplen, map);
	if (ret == _FAIL)
		goto exit;

	Efuse_PowerSwitch(padapter, true, true);

	offset = (addr >> 3);
	word_en = 0xF;
	memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	i = addr & 0x7;	/*  index of one package */
	idx = 0;	/*  data index */

	if (i & 0x1) {
		/*  odd start */
		if (data[idx] != map[addr+idx]) {
			word_en &= ~BIT(i >> 1);
			newdata[i-1] = map[addr+idx-1];
			newdata[i] = data[idx];
		}
		i++;
		idx++;
	}
	do {
		for (; i < PGPKT_DATA_SIZE; i += 2) {
			if (cnts == idx)
				 break;
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
				    (data[idx+1] != map[addr+idx+1])) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i+1] = data[idx + 1];
				}
				idx += 2;
			}
			if (idx == cnts)
				break;
		}

		if (word_en != 0xF) {
			ret = Efuse_PgPacketWrite(padapter, offset, word_en, newdata, false);
			DBG_8192D("offset =%x\n", offset);
			DBG_8192D("word_en =%x\n", word_en);

			for (i = 0; i < PGPKT_DATA_SIZE; i++)
				DBG_8192D("data =%x \t", newdata[i]);
			if (ret == _FAIL)
				break;
		}

		if (idx == cnts)
			break;

		offset++;
		i = 0;
		word_en = 0xF;
		memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	} while (1);

	Efuse_PowerSwitch(padapter, true, false);

exit:

	kfree(map);

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
 * 11/11/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void Efuse_ReadAllMap(struct rtw_adapter *adapter, u8 efusetype, u8 *efuse, bool test)
{
	u16 maplen = 0;

	Efuse_PowerSwitch(adapter, false, true);
	EFUSE_GetEfuseDefinition(adapter, efusetype, TYPE_EFUSE_MAP_LEN, (void *)&maplen, test);
	efuse_ReadEFuse(adapter, efusetype, 0, maplen, efuse, test);
	Efuse_PowerSwitch(adapter, false, false);
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
static void efuse_ShadowRead1Byte(struct rtw_adapter *adapter, u16 Offset, u8 *value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	*value = pEEPROM->efuse_eeprom_data[Offset];
}	/*  EFUSE_ShadowRead1Byte */

/* Read Two Bytes */
static void efuse_ShadowRead2Byte(struct rtw_adapter *adapter, u16 Offset, u16 *value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	*value = pEEPROM->efuse_eeprom_data[Offset];
	*value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
}	/*  EFUSE_ShadowRead2Byte */

/* Read Four Bytes */
static void efuse_ShadowRead4Byte(struct rtw_adapter *adapter, u16 Offset, u32 *value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	*value = pEEPROM->efuse_eeprom_data[Offset];
	*value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
	*value |= pEEPROM->efuse_eeprom_data[Offset+2]<<16;
	*value |= pEEPROM->efuse_eeprom_data[Offset+3]<<24;
}	/*  efuse_ShadowRead4Byte */

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
 * 11/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void efuse_ShadowWrite1Byte(struct rtw_adapter *adapter, u16 Offset, u8 value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	pEEPROM->efuse_eeprom_data[Offset] = value;
}	/*  efuse_ShadowWrite1Byte */

/* Write Two Bytes */
static void efuse_ShadowWrite2Byte(struct rtw_adapter *adapter, u16 Offset, u16 value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	pEEPROM->efuse_eeprom_data[Offset] = value&0x00FF;
	pEEPROM->efuse_eeprom_data[Offset+1] = value>>8;
}	/*  efuse_ShadowWrite1Byte */

/* Write Four Bytes */
static void efuse_ShadowWrite4Byte(struct rtw_adapter *adapter, u16 Offset, u32 value)
{
	struct eeprom_priv *EEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	EEPROM->efuse_eeprom_data[Offset] = (u8)(value&0x000000FF);
	EEPROM->efuse_eeprom_data[Offset+1] = (u8)((value>>8)&0x0000FF);
	EEPROM->efuse_eeprom_data[Offset+2] = (u8)((value>>16)&0x00FF);
	EEPROM->efuse_eeprom_data[Offset+3] = (u8)((value>>24)&0xFF);
}	/*  efuse_ShadowWrite1Byte */

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
 * 11/13/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void EFUSE_ShadowMapUpdate(struct rtw_adapter *adapter, u8 efusetype, bool test)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);
	u16 maplen = 0;

	EFUSE_GetEfuseDefinition(adapter, efusetype, TYPE_EFUSE_MAP_LEN, (void *)&maplen, test);

	if (pEEPROM->bautoload_fail_flag == true) {
		memset(pEEPROM->efuse_eeprom_data, 0xFF, maplen);
	} else {
		#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
		if (_SUCCESS != retriveAdaptorInfoFile(adapter->registrypriv.adaptor_info_caching_file_path, pEEPROM)) {
		#endif

			Efuse_ReadAllMap(adapter, efusetype, pEEPROM->efuse_eeprom_data, test);

		#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
			storeAdaptorInfoFile(adapter->registrypriv.adaptor_info_caching_file_path, pEEPROM);
		}
		#endif
	}
} /*  EFUSE_ShadowMapUpdate */

#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE

int storeAdaptorInfoFile(char *path, struct eeprom_priv *eeprom_priv)
{
	int ret = _SUCCESS;

	if (path && eeprom_priv) {
		ret = rtw_store_to_file(path, eeprom_priv->efuse_eeprom_data,
					EEPROM_MAX_SIZE);
		if (ret == EEPROM_MAX_SIZE)
			ret = _SUCCESS;
		else
			ret = _FAIL;
	} else {
		DBG_8192D("%s NULL pointer\n", __func__);
		ret =  _FAIL;
	}
	return ret;
}

int retriveAdaptorInfoFile(char *path, struct eeprom_priv *eeprom_priv)
{
	int ret = _SUCCESS;
	mm_segment_t oldfs;
	struct file *fp;

	if (path && eeprom_priv) {
		ret = rtw_retrive_from_file(path,
					    eeprom_priv->efuse_eeprom_data,
					    EEPROM_MAX_SIZE);

		if (ret == EEPROM_MAX_SIZE)
			ret = _SUCCESS;
		else
			ret = _FAIL;
	} else {
		DBG_8192D("%s NULL pointer\n", __func__);
		ret = _FAIL;
	}
	return ret;
}
#endif /* CONFIG_ADAPTOR_INFO_CACHING_FILE */
