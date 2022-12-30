// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_data.h>
#include <linux/jiffies.h>


/* Define global variables */
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

#define REG_EFUSE_CTRL		0x0030
#define EFUSE_CTRL			REG_EFUSE_CTRL		/*  E-Fuse Control. */

static bool
Efuse_Read1ByteFromFakeContent(u16 Offset, u8 *Value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0)
		*Value = fakeEfuseContent[Offset];
	else
		*Value = fakeBTEfuseContent[fakeEfuseBank-1][Offset];
	return true;
}

static bool
Efuse_Write1ByteToFakeContent(u16 Offset, u8 Value)
{
	if (Offset >= EFUSE_MAX_HW_SIZE)
		return false;
	if (fakeEfuseBank == 0)
		fakeEfuseContent[Offset] = Value;
	else
		fakeBTEfuseContent[fakeEfuseBank-1][Offset] = Value;
	return true;
}

/*-----------------------------------------------------------------------------
 * Function:	Efuse_PowerSwitch
 *
 * Overview:	When we want to enable write operation, we should change to
 *			pwr on state. When we stop write, we should switch to 500k mode
 *			and disable LDO 2.5V.
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
void
Efuse_PowerSwitch(
struct adapter *padapter,
u8 bWrite,
u8 PwrState)
{
	padapter->HalFunc.EfusePowerSwitch(padapter, bWrite, PwrState);
}

/*-----------------------------------------------------------------------------
 * Function:	Efuse_GetCurrentSize
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
	struct adapter *padapter,
	u8	efuseType,
	bool		bPseudoTest)
{
	return padapter->HalFunc.EfuseGetCurrentSize(padapter, efuseType,
						     bPseudoTest);
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
/* Description: */
/*		1. Execute E-Fuse read byte operation according as map offset and */
/*			save to E-Fuse table. */
/*		2. Referred from SD1 Richard. */
/*  */
/* Assumption: */
/*		1. Boot from E-Fuse and successfully auto-load. */
/*		2. PASSIVE_LEVEL (USB interface) */
/*  */
/* Created by Roger, 2008.10.21. */
/*  */
/* 2008/12/12 MH	1. Reorganize code flow and reserve bytes. and add description. */
/*					2. Add efuse utilization collect. */
/* 2008/12/22 MH	Read Efuse must check if we write section 1 data again!!! Sec1 */
/*					write addr must be after sec5. */
/*  */

void
efuse_ReadEFuse(
	struct adapter *Adapter,
	u8 efuseType,
	u16		_offset,
	u16		_size_byte,
	u8 *pbuf,
bool	bPseudoTest
	);
void
efuse_ReadEFuse(
	struct adapter *Adapter,
	u8 efuseType,
	u16		_offset,
	u16		_size_byte,
	u8 *pbuf,
bool	bPseudoTest
	)
{
	Adapter->HalFunc.ReadEFuse(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
}

void
EFUSE_GetEfuseDefinition(
	struct adapter *padapter,
	u8 efuseType,
	u8 type,
	void	*pOut,
	bool		bPseudoTest
	)
{
	padapter->HalFunc.EFUSEGetEfuseDefinition(padapter, efuseType, type, pOut, bPseudoTest);
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
u8
EFUSE_Read1Byte(
struct adapter *Adapter,
u16		Address)
{
	u8 Bytetemp = {0x00};
	u8 temp = {0x00};
	u32 k = 0;
	u16 contentLen = 0;

	EFUSE_GetEfuseDefinition(Adapter, EFUSE_WIFI, TYPE_EFUSE_REAL_CONTENT_LEN, (void *)&contentLen, false);

	if (Address < contentLen) {/* E-fuse 512Byte */
		/* Write E-fuse Register address bit0~7 */
		temp = Address & 0xFF;
		rtw_write8(Adapter, EFUSE_CTRL+1, temp);
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+2);
		/* Write E-fuse Register address bit8~9 */
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		rtw_write8(Adapter, EFUSE_CTRL+2, temp);

		/* Write 0x30[31]= 0 */
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		temp = Bytetemp & 0x7F;
		rtw_write8(Adapter, EFUSE_CTRL+3, temp);

		/* Wait Write-ready (0x30[31]= 1) */
		Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
		while (!(Bytetemp & 0x80)) {
			Bytetemp = rtw_read8(Adapter, EFUSE_CTRL+3);
			k++;
			if (k == 1000)
				break;
		}
		return rtw_read8(Adapter, EFUSE_CTRL);
	} else
		return 0xFF;

} /* EFUSE_Read1Byte */

/*  11/16/2008 MH Read one byte from real Efuse. */
u8
efuse_OneByteRead(
struct adapter *padapter,
u16	addr,
u8	*data,
bool		bPseudoTest)
{
	u32 tmpidx = 0;
	u8 bResult;
	u8 readbyte;

	if (bPseudoTest)
		return Efuse_Read1ByteFromFakeContent(addr, data);

	/*  <20130121, Kordan> For SMIC EFUSE specificatoin. */
	/* 0x34[11]: SW force PGMEN input of efuse to high. (for the bank selected by 0x34[9:8]) */
	/* PHY_SetMacReg(padapter, 0x34, BIT11, 0); */
	rtw_write16(padapter, 0x34, rtw_read16(padapter, 0x34) & (~BIT11));

	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */
	rtw_write8(padapter, EFUSE_CTRL+1, (u8)(addr&0xff));
	rtw_write8(padapter, EFUSE_CTRL+2, ((u8)((addr>>8) & 0x03)) |
	(rtw_read8(padapter, EFUSE_CTRL+2)&0xFC));

	/* rtw_write8(padapter, EFUSE_CTRL+3,  0x72); read cmd */
	/* Write bit 32 0 */
	readbyte = rtw_read8(padapter, EFUSE_CTRL+3);
	rtw_write8(padapter, EFUSE_CTRL+3, (readbyte & 0x7f));

	while (!(0x80 & rtw_read8(padapter, EFUSE_CTRL+3)) && (tmpidx < 1000)) {
		mdelay(1);
		tmpidx++;
	}
	if (tmpidx < 100) {
		*data = rtw_read8(padapter, EFUSE_CTRL);
		bResult = true;
	} else {
		*data = 0xff;
		bResult = false;
	}

	return bResult;
}

/*  11/16/2008 MH Write one byte to reald Efuse. */
u8 efuse_OneByteWrite(struct adapter *padapter, u16 addr, u8 data, bool bPseudoTest)
{
	u8 tmpidx = 0;
	u8 bResult = false;
	u32 efuseValue;

	if (bPseudoTest)
		return Efuse_Write1ByteToFakeContent(addr, data);


	/*  -----------------e-fuse reg ctrl --------------------------------- */
	/* address */


	efuseValue = rtw_read32(padapter, EFUSE_CTRL);
	efuseValue |= (BIT21|BIT31);
	efuseValue &= ~(0x3FFFF);
	efuseValue |= ((addr<<8 | data) & 0x3FFFF);


	/*  <20130227, Kordan> 8192E MP chip A-cut had better not set 0x34[11] until B-Cut. */

	/*  <20130121, Kordan> For SMIC EFUSE specificatoin. */
	/* 0x34[11]: SW force PGMEN input of efuse to high. (for the bank selected by 0x34[9:8]) */
	/* PHY_SetMacReg(padapter, 0x34, BIT11, 1); */
	rtw_write16(padapter, 0x34, rtw_read16(padapter, 0x34) | (BIT11));
	rtw_write32(padapter, EFUSE_CTRL, 0x90600000|((addr<<8 | data)));

	while ((0x80 &  rtw_read8(padapter, EFUSE_CTRL+3)) && (tmpidx < 100)) {
		mdelay(1);
		tmpidx++;
	}

	if (tmpidx < 100)
		bResult = true;
	else
		bResult = false;

	/*  disable Efuse program enable */
	PHY_SetMacReg(padapter, EFUSE_TEST, BIT(11), 0);

	return bResult;
}

int
Efuse_PgPacketRead(struct adapter *padapter,
				u8	offset,
				u8	*data,
				bool		bPseudoTest)
{
	return padapter->HalFunc.Efuse_PgPacketRead(padapter, offset, data,
						    bPseudoTest);
}

int
Efuse_PgPacketWrite(struct adapter *padapter,
				u8	offset,
				u8	word_en,
				u8	*data,
				bool		bPseudoTest)
{
	return padapter->HalFunc.Efuse_PgPacketWrite(padapter, offset, word_en,
						     data, bPseudoTest);
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
void
efuse_WordEnableDataRead(u8 word_en,
						u8 *sourdata,
						u8 *targetdata)
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


u8
Efuse_WordEnableDataWrite(struct adapter *padapter,
						u16		efuse_addr,
						u8 word_en,
						u8 *data,
						bool		bPseudoTest)
{
	return padapter->HalFunc.Efuse_WordEnableDataWrite(padapter, efuse_addr,
							   word_en, data,
							   bPseudoTest);
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
Efuse_ReadAllMap(
	struct adapter *padapter,
	u8 efuseType,
	u8 *Efuse,
	bool		bPseudoTest);
void Efuse_ReadAllMap(struct adapter *padapter, u8 efuseType, u8 *Efuse, bool bPseudoTest)
{
	u16 mapLen = 0;

	Efuse_PowerSwitch(padapter, false, true);

	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, bPseudoTest);

	efuse_ReadEFuse(padapter, efuseType, 0, mapLen, Efuse, bPseudoTest);

	Efuse_PowerSwitch(padapter, false, false);
}

/*-----------------------------------------------------------------------------
 * Function:	efuse_ShadowRead1Byte
 *		efuse_ShadowRead2Byte
 *		efuse_ShadowRead4Byte
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
static void efuse_ShadowRead1Byte(struct adapter *padapter, u16 Offset, u8 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];

}	/*  EFUSE_ShadowRead1Byte */

/* Read Two Bytes */
static void efuse_ShadowRead2Byte(struct adapter *padapter, u16 Offset, u16 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;

}	/*  EFUSE_ShadowRead2Byte */

/* Read Four Bytes */
static void efuse_ShadowRead4Byte(struct adapter *padapter, u16 Offset, u32 *Value)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	*Value = pEEPROM->efuse_eeprom_data[Offset];
	*Value |= pEEPROM->efuse_eeprom_data[Offset+1]<<8;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+2]<<16;
	*Value |= pEEPROM->efuse_eeprom_data[Offset+3]<<24;

}	/*  efuse_ShadowRead4Byte */

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
void EFUSE_ShadowMapUpdate(struct adapter *padapter, u8 efuseType, bool bPseudoTest)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u16 mapLen = 0;

	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_EFUSE_MAP_LEN, (void *)&mapLen, bPseudoTest);

	if (pEEPROM->bautoload_fail_flag)
		memset(pEEPROM->efuse_eeprom_data, 0xFF, mapLen);
	else
		Efuse_ReadAllMap(padapter, efuseType, pEEPROM->efuse_eeprom_data, bPseudoTest);

	/* PlatformMoveMemory((void *)&pHalData->EfuseMap[EFUSE_MODIFY_MAP][0], */
	/* void *)&pHalData->EfuseMap[EFUSE_INIT_MAP][0], mapLen); */
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
void EFUSE_ShadowRead(struct adapter *padapter, u8 Type, u16 Offset, u32 *Value)
{
	if (Type == 1)
		efuse_ShadowRead1Byte(padapter, Offset, (u8 *)Value);
	else if (Type == 2)
		efuse_ShadowRead2Byte(padapter, Offset, (u16 *)Value);
	else if (Type == 4)
		efuse_ShadowRead4Byte(padapter, Offset, (u32 *)Value);

}	/* EFUSE_ShadowRead*/
