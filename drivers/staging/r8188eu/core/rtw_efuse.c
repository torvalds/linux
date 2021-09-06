// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_EFUSE_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_efuse.h"

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
static bool Efuse_Read1ByteFromFakeContent(struct adapter *pAdapter,
					   u16 Offset,
					   u8 *Value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0)
		*Value = fakeEfuseContent[Offset];
	else
		*Value = fakeBTEfuseContent[fakeEfuseBank - 1][Offset];
	return true;
}

static bool
Efuse_Write1ByteToFakeContent(
			struct adapter *pAdapter,
			u16 Offset,
			u8 Value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0) {
		fakeEfuseContent[Offset] = Value;
	} else {
		fakeBTEfuseContent[fakeEfuseBank - 1][Offset] = Value;
	}
	return true;
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
u16
Efuse_GetCurrentSize(
	struct adapter *pAdapter,
	u8 efuseType,
	bool pseudo)
{
	u16 ret = 0;

	ret = pAdapter->HalFunc.EfuseGetCurrentSize(pAdapter, efuseType, pseudo);

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
/* 	Description: */
/* 		Execute E-Fuse read byte operation. */
/* 		Referred from SD1 Richard. */
/*  */
/* 	Assumption: */
/* 		1. Boot from E-Fuse and successfully auto-load. */
/* 		2. PASSIVE_LEVEL (USB interface) */
/*  */
/* 	Created by Roger, 2008.10.21. */
/*  */
void
ReadEFuseByte(
		struct adapter *Adapter,
		u16 _offset,
		u8 *pbuf,
		bool pseudo)
{
	u32 value32;
	u8 readbyte;
	u16 retry;

	if (pseudo) {
		Efuse_Read1ByteFromFakeContent(Adapter, _offset, pbuf);
		return;
	}

	/* Write Address */
	rtw_write8(Adapter, EFUSE_CTRL + 1, (_offset & 0xff));
	readbyte = rtw_read8(Adapter, EFUSE_CTRL + 2);
	rtw_write8(Adapter, EFUSE_CTRL + 2, ((_offset >> 8) & 0x03) | (readbyte & 0xfc));

	/* Write bit 32 0 */
	readbyte = rtw_read8(Adapter, EFUSE_CTRL + 3);
	rtw_write8(Adapter, EFUSE_CTRL + 3, (readbyte & 0x7f));

	/* Check bit 32 read-ready */
	retry = 0;
	value32 = rtw_read32(Adapter, EFUSE_CTRL);
	while (!(((value32 >> 24) & 0xff) & 0x80)  && (retry < 10000)) {
		value32 = rtw_read32(Adapter, EFUSE_CTRL);
		retry++;
	}

	/*  20100205 Joseph: Add delay suggested by SD1 Victor. */
	/*  This fix the problem that Efuse read error in high temperature condition. */
	/*  Designer says that there shall be some delay after ready bit is set, or the */
	/*  result will always stay on last data we read. */
	udelay(50);
	value32 = rtw_read32(Adapter, EFUSE_CTRL);

	*pbuf = (u8)(value32 & 0xff);
}

/*  */
/* 	Description: */
/* 		1. Execute E-Fuse read byte operation according as map offset and */
/* 		    save to E-Fuse table. */
/* 		2. Referred from SD1 Richard. */
/*  */
/* 	Assumption: */
/* 		1. Boot from E-Fuse and successfully auto-load. */
/* 		2. PASSIVE_LEVEL (USB interface) */
/*  */
/* 	Created by Roger, 2008.10.21. */
/*  */
/* 	2008/12/12 MH	1. Reorganize code flow and reserve bytes. and add description. */
/* 					2. Add efuse utilization collect. */
/* 	2008/12/22 MH	Read Efuse must check if we write section 1 data again!!! Sec1 */
/* 					write addr must be after sec5. */
/*  */

void EFUSE_GetEfuseDefinition(struct adapter *pAdapter, u8 efuseType, u8 type, void *pOut, bool pseudo
	)
{
	pAdapter->HalFunc.EFUSEGetEfuseDefinition(pAdapter, efuseType, type, pOut, pseudo);
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
u8 EFUSE_Read1Byte(struct adapter *Adapter, u16 Address)
{
	u8 data;
	u8 Bytetemp = {0x00};
	u8 temp = {0x00};
	u32 k = 0;
	u16 contentLen = 0;

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&contentLen, false);

	if (Address < contentLen) {	/* E-fuse 512Byte */
		/* Write E-fuse Register address bit0~7 */
		temp = Address & 0xFF;
		rtw_write8(Adapter, EFUSE_CTRL + 1, temp);
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL + 2);
		/* Write E-fuse Register address bit8~9 */
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		rtw_write8(Adapter, EFUSE_CTRL + 2, temp);

		/* Write 0x30[31]= 0 */
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL + 3);
		temp = Bytetemp & 0x7F;
		rtw_write8(Adapter, EFUSE_CTRL + 3, temp);

		/* Wait Write-ready (0x30[31]= 1) */
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL + 3);
		while (!(Bytetemp & 0x80)) {
			Bytetemp = rtw_read8(Adapter, EFUSE_CTRL + 3);
			k++;
			if (k == 1000) {
				k = 0;
				break;
			}
		}
		data = rtw_read8(Adapter, EFUSE_CTRL);
		return data;
	} else {
		return 0xFF;
	}

} /* EFUSE_Read1Byte */

/*  11/16/2008 MH Read one byte from real Efuse. */
u8 efuse_OneByteRead(struct adapter *pAdapter, u16 addr, u8 *data, bool pseudo)
{
	u8 tmpidx = 0;
	u8 result;

	if (pseudo) {
		result = Efuse_Read1ByteFromFakeContent(pAdapter, addr, data);
		return result;
	}
	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */
	rtw_write8(pAdapter, EFUSE_CTRL + 1, (u8)(addr & 0xff));
	rtw_write8(pAdapter, EFUSE_CTRL + 2, ((u8)((addr >> 8) & 0x03)) |
		   (rtw_read8(pAdapter, EFUSE_CTRL + 2) & 0xFC));

	rtw_write8(pAdapter, EFUSE_CTRL + 3,  0x72);/* read cmd */

	while (!(0x80 & rtw_read8(pAdapter, EFUSE_CTRL + 3)) && (tmpidx < 100))
		tmpidx++;
	if (tmpidx < 100) {
		*data = rtw_read8(pAdapter, EFUSE_CTRL);
		result = true;
	} else {
		*data = 0xff;
		result = false;
	}
	return result;
}

/*  11/16/2008 MH Write one byte to reald Efuse. */
u8 efuse_OneByteWrite(struct adapter *pAdapter, u16 addr, u8 data, bool pseudo)
{
	u8 tmpidx = 0;
	u8 result;

	if (pseudo) {
		result = Efuse_Write1ByteToFakeContent(pAdapter, addr, data);
		return result;
	}

	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */
	rtw_write8(pAdapter, EFUSE_CTRL + 1, (u8)(addr & 0xff));
	rtw_write8(pAdapter, EFUSE_CTRL + 2,
		   (rtw_read8(pAdapter, EFUSE_CTRL + 2) & 0xFC) |
		   (u8)((addr >> 8) & 0x03));
	rtw_write8(pAdapter, EFUSE_CTRL, data);/* data */

	rtw_write8(pAdapter, EFUSE_CTRL + 3, 0xF2);/* write cmd */

	while ((0x80 &  rtw_read8(pAdapter, EFUSE_CTRL + 3)) && (tmpidx < 100))
		tmpidx++;

	if (tmpidx < 100)
		result = true;
	else
		result = false;

	return result;
}

int Efuse_PgPacketRead(struct adapter *pAdapter, u8 offset, u8 *data, bool pseudo)
{
	int	ret = 0;

	ret =  pAdapter->HalFunc.Efuse_PgPacketRead(pAdapter, offset, data, pseudo);

	return ret;
}

int Efuse_PgPacketWrite(struct adapter *pAdapter, u8 offset, u8 word_en, u8 *data, bool pseudo)
{
	int ret;

	ret =  pAdapter->HalFunc.Efuse_PgPacketWrite(pAdapter, offset, word_en, data, pseudo);

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
	if (!(word_en & BIT(0))) {
		targetdata[0] = sourdata[0];
		targetdata[1] = sourdata[1];
	}
	if (!(word_en & BIT(1))) {
		targetdata[2] = sourdata[2];
		targetdata[3] = sourdata[3];
	}
	if (!(word_en & BIT(2))) {
		targetdata[4] = sourdata[4];
		targetdata[5] = sourdata[5];
	}
	if (!(word_en & BIT(3))) {
		targetdata[6] = sourdata[6];
		targetdata[7] = sourdata[7];
	}
}

u8 Efuse_WordEnableDataWrite(struct adapter *pAdapter, u16 efuse_addr, u8 word_en, u8 *data, bool pseudo)
{
	u8 ret = 0;

	ret =  pAdapter->HalFunc.Efuse_WordEnableDataWrite(pAdapter, efuse_addr, word_en, data, pseudo);

	return ret;
}

static u8 efuse_read8(struct adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteRead(padapter, address, value, false);
}

static u8 efuse_write8(struct adapter *padapter, u16 address, u8 *value)
{
	return efuse_OneByteWrite(padapter, address, *value, false);
}

/*
 * read/wirte raw efuse data
 */
u8 rtw_efuse_access(struct adapter *padapter, u8 write, u16 start_addr, u16 cnts, u8 *data)
{
	int i = 0;
	u16 real_content_len = 0, max_available_size = 0;
	u8 res = _FAIL;
	u8 (*rw8)(struct adapter *, u16, u8*);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&real_content_len, false);
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

	if (start_addr > real_content_len)
		return _FAIL;

	if (write) {
		if ((start_addr + cnts) > max_available_size)
			return _FAIL;
		rw8 = &efuse_write8;
	} else {
		rw8 = &efuse_read8;
	}

	rtl8188e_EfusePowerSwitch(padapter, write, true);

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

	rtl8188e_EfusePowerSwitch(padapter, write, false);

	return res;
}
/*  */
u16 efuse_GetMaxSize(struct adapter *padapter)
{
	u16 max_size;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_size, false);
	return max_size;
}
/*  */
u8 efuse_GetCurrentSize(struct adapter *padapter, u16 *size)
{
	rtl8188e_EfusePowerSwitch(padapter, false, true);
	*size = Efuse_GetCurrentSize(padapter, EFUSE_WIFI, false);
	rtl8188e_EfusePowerSwitch(padapter, false, false);

	return _SUCCESS;
}
/*  */
u8 rtw_efuse_map_read(struct adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, false);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	rtl8188e_EfusePowerSwitch(padapter, false, true);

	rtl8188e_ReadEFuse(padapter, EFUSE_WIFI, addr, cnts, data, false);

	rtl8188e_EfusePowerSwitch(padapter, false, false);

	return _SUCCESS;
}

u8 rtw_BT_efuse_map_read(struct adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, false);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	rtl8188e_EfusePowerSwitch(padapter, false, true);

	rtl8188e_ReadEFuse(padapter, EFUSE_BT, addr, cnts, data, false);

	rtl8188e_EfusePowerSwitch(padapter, false, false);

	return _SUCCESS;
}
/*  */
u8 rtw_efuse_map_write(struct adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u8 offset, word_en;
	u8 *map;
	u8 newdata[PGPKT_DATA_SIZE + 1];
	s32	i, idx;
	u8 ret = _SUCCESS;
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, false);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	map = kzalloc(mapLen, GFP_KERNEL);
	if (!map)
		return _FAIL;

	ret = rtw_efuse_map_read(padapter, 0, mapLen, map);
	if (ret == _FAIL)
		goto exit;

	rtl8188e_EfusePowerSwitch(padapter, true, true);

	offset = (addr >> 3);
	word_en = 0xF;
	memset(newdata, 0xFF, PGPKT_DATA_SIZE + 1);
	i = addr & 0x7;	/*  index of one package */
	idx = 0;	/*  data index */

	if (i & 0x1) {
		/*  odd start */
		if (data[idx] != map[addr + idx]) {
			word_en &= ~BIT(i >> 1);
			newdata[i - 1] = map[addr + idx - 1];
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
				if (data[idx] != map[addr + idx]) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i + 1] = map[addr + idx + 1];
				}
				idx++;
				break;
			} else {
				if ((data[idx] != map[addr + idx]) ||
				    (data[idx + 1] != map[addr + idx + 1])) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i + 1] = data[idx + 1];
				}
				idx += 2;
			}
			if (idx == cnts)
				break;
		}

		if (word_en != 0xF) {
			ret = Efuse_PgPacketWrite(padapter, offset, word_en, newdata, false);
			DBG_88E("offset=%x\n", offset);
			DBG_88E("word_en=%x\n", word_en);

			for (i = 0; i < PGPKT_DATA_SIZE; i++)
				DBG_88E("data=%x \t", newdata[i]);
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

	rtl8188e_EfusePowerSwitch(padapter, true, false);
exit:
	kfree(map);
	return ret;
}

/*  */
u8 rtw_BT_efuse_map_write(struct adapter *padapter, u16 addr, u16 cnts, u8 *data)
{
	u8 offset, word_en;
	u8 *map;
	u8 newdata[PGPKT_DATA_SIZE + 1];
	s32	i, idx;
	u8 ret = _SUCCESS;
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, false);

	if ((addr + cnts) > mapLen)
		return _FAIL;

	map = kzalloc(mapLen, GFP_KERNEL);
	if (!map)
		return _FAIL;

	ret = rtw_BT_efuse_map_read(padapter, 0, mapLen, map);
	if (ret == _FAIL)
		goto exit;

	rtl8188e_EfusePowerSwitch(padapter, true, true);

	offset = (addr >> 3);
	word_en = 0xF;
	memset(newdata, 0xFF, PGPKT_DATA_SIZE + 1);
	i = addr & 0x7;	/*  index of one package */
	idx = 0;	/*  data index */

	if (i & 0x1) {
		/*  odd start */
		if (data[idx] != map[addr + idx]) {
			word_en &= ~BIT(i >> 1);
			newdata[i - 1] = map[addr + idx - 1];
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
				if (data[idx] != map[addr + idx]) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i + 1] = map[addr + idx + 1];
				}
				idx++;
				break;
			} else {
				if ((data[idx] != map[addr + idx]) ||
				    (data[idx + 1] != map[addr + idx + 1])) {
					word_en &= ~BIT(i >> 1);
					newdata[i] = data[idx];
					newdata[i + 1] = data[idx + 1];
				}
				idx += 2;
			}
			if (idx == cnts)
				break;
		}

		if (word_en != 0xF) {
			DBG_88E("%s: offset=%#X\n", __func__, offset);
			DBG_88E("%s: word_en=%#X\n", __func__, word_en);
			DBG_88E("%s: data=", __func__);
			for (i = 0; i < PGPKT_DATA_SIZE; i++)
				DBG_88E("0x%02X ", newdata[i]);
			DBG_88E("\n");

			break;
		}

		if (idx == cnts)
			break;

		offset++;
		i = 0;
		word_en = 0xF;
		memset(newdata, 0xFF, PGPKT_DATA_SIZE);
	} while (1);

	rtl8188e_EfusePowerSwitch(padapter, true, false);

exit:

	kfree(map);

	return ret;
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
		struct adapter *pAdapter,
		u16 Offset,
		u8 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];

}	/*  EFUSE_ShadowRead1Byte */

/* Read Two Bytes */
static void
efuse_ShadowRead2Byte(
		struct adapter *pAdapter,
		u16 Offset,
		u16 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset + 1] << 8;

}	/*  EFUSE_ShadowRead2Byte */

/* Read Four Bytes */
static void
efuse_ShadowRead4Byte(
		struct adapter *pAdapter,
		u16 Offset,
		u32 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset + 1] << 8;
	*Value |= pEEPROM->efuse_eeprom_data[Offset + 2] << 16;
	*Value |= pEEPROM->efuse_eeprom_data[Offset + 3] << 24;

}	/*  efuse_ShadowRead4Byte */

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
static void Efuse_ReadAllMap(struct adapter *pAdapter, u8 efuseType, u8 *Efuse, bool pseudo)
{
	u16 mapLen = 0;

	rtl8188e_EfusePowerSwitch(pAdapter, false, true);

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, pseudo);

	rtl8188e_ReadEFuse(pAdapter, efuseType, 0, mapLen, Efuse, pseudo);

	rtl8188e_EfusePowerSwitch(pAdapter, false, false);
}

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
void EFUSE_ShadowMapUpdate(
	struct adapter *pAdapter,
	u8 efuseType,
	bool pseudo)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, pseudo);

	if (pEEPROM->bautoload_fail_flag)
		memset(pEEPROM->efuse_eeprom_data, 0xFF, mapLen);
	else
		Efuse_ReadAllMap(pAdapter, efuseType, pEEPROM->efuse_eeprom_data, pseudo);
} /*  EFUSE_ShadowMapUpdate */

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
 * 11/12/2008	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
void EFUSE_ShadowRead(struct adapter *pAdapter, u8 Type, u16 Offset, u32 *Value)
{
	if (Type == 1)
		efuse_ShadowRead1Byte(pAdapter, Offset, (u8 *)Value);
	else if (Type == 2)
		efuse_ShadowRead2Byte(pAdapter, Offset, (u16 *)Value);
	else if (Type == 4)
		efuse_ShadowRead4Byte(pAdapter, Offset, (u32 *)Value);

}	/*  EFUSE_ShadowRead */
