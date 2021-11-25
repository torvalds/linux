// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_EFUSE_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_efuse.h"
#include "../include/rtl8188e_hal.h"

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
		u8 *pbuf)
{
	u32 value32;
	u8 readbyte;
	u16 retry;

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
void EFUSE_ShadowMapUpdate(struct adapter *pAdapter)
{
	struct eeprom_priv *pEEPROM = &pAdapter->eeprompriv;

	if (pEEPROM->bautoload_fail_flag) {
		memset(pEEPROM->efuse_eeprom_data, 0xFF, EFUSE_MAP_LEN_88E);
		return;
	}

	rtl8188e_EfusePowerSwitch(pAdapter, true);
	rtl8188e_ReadEFuse(pAdapter, 0, EFUSE_MAP_LEN_88E, pEEPROM->efuse_eeprom_data);
	rtl8188e_EfusePowerSwitch(pAdapter, false);
}
